/*
 * Copyright (c) 2019, Alliance for Open Media. All rights reserved
 *
 * This source code is subject to the terms of the BSD 2 Clause License and
 * the Alliance for Open Media Patent License 1.0. If the BSD 2 Clause License
 * was not distributed with this source code in the LICENSE file, you can
 * obtain it at www.aomedia.org/license/software. If the Alliance for Open
 * Media Patent License 1.0 was not distributed with this source code in the
 * PATENTS file, you can obtain it at www.aomedia.org/license/patent.
 */

#include <stdint.h>
#include <float.h>

#include "config/aom_config.h"
#include "config/aom_dsp_rtcd.h"
#include "config/aom_scale_rtcd.h"

#include "aom/aom_codec.h"
#include "aom_ports/system_state.h"

#include "av1/common/av1_common_int.h"
#include "av1/common/enums.h"
#include "av1/common/idct.h"
#include "av1/common/reconintra.h"

#include "av1/encoder/encoder.h"
#include "av1/encoder/ethread.h"
#include "av1/encoder/encodeframe_utils.h"
#include "av1/encoder/encode_strategy.h"
#include "av1/encoder/hybrid_fwd_txfm.h"
#include "av1/encoder/motion_search_facade.h"
#include "av1/encoder/rd.h"
#include "av1/encoder/rdopt.h"
#include "av1/encoder/reconinter_enc.h"
#include "av1/encoder/tpl_model.h"

static AOM_INLINE void tpl_stats_record_txfm_block(TplTxfmStats *tpl_txfm_stats,
                                                   const tran_low_t *coeff,
                                                   int coeff_num) {
  aom_clear_system_state();
  // For transform larger than 16x16, the scale of coeff need to be adjusted.
  // It's not LOSSLESS_Q_STEP.
  assert(coeff_num <= 256);
  for (int i = 0; i < coeff_num; ++i) {
    tpl_txfm_stats->abs_coeff_sum[i] += abs(coeff[i]) / (double)LOSSLESS_Q_STEP;
  }
  ++tpl_txfm_stats->txfm_block_count;
}

static AOM_INLINE void tpl_stats_update_abs_coeff_mean(
    TplParams *tpl_data, TplTxfmStats *tpl_txfm_stats) {
  aom_clear_system_state();
  TplDepFrame *tpl_frame = &tpl_data->tpl_frame[tpl_data->frame_idx];
  tpl_frame->txfm_block_count = tpl_txfm_stats->txfm_block_count;
  for (int i = 0; i < tpl_frame->coeff_num; ++i) {
    tpl_frame->abs_coeff_sum[i] = tpl_txfm_stats->abs_coeff_sum[i];
    tpl_frame->abs_coeff_mean[i] =
        tpl_frame->abs_coeff_sum[i] / tpl_txfm_stats->txfm_block_count;
  }
}

void av1_tpl_stats_init_txfm_stats(TplDepFrame *tpl_frame, int tpl_bsize_1d) {
  aom_clear_system_state();
  tpl_frame->txfm_block_count = 0;
  tpl_frame->coeff_num = tpl_bsize_1d * tpl_bsize_1d;
  memset(tpl_frame->abs_coeff_sum, 0, sizeof(tpl_frame->abs_coeff_sum));
  assert(sizeof(tpl_frame->abs_coeff_mean) /
             sizeof(tpl_frame->abs_coeff_mean[0]) ==
         tpl_frame->coeff_num);
}

static AOM_INLINE void get_quantize_error(const MACROBLOCK *x, int plane,
                                          const tran_low_t *coeff,
                                          tran_low_t *qcoeff,
                                          tran_low_t *dqcoeff, TX_SIZE tx_size,
                                          uint16_t *eob, int64_t *recon_error,
                                          int64_t *sse) {
  const struct macroblock_plane *const p = &x->plane[plane];
  const MACROBLOCKD *xd = &x->e_mbd;
  const SCAN_ORDER *const scan_order = &av1_scan_orders[tx_size][DCT_DCT];
  int pix_num = 1 << num_pels_log2_lookup[txsize_to_bsize[tx_size]];
  const int shift = tx_size == TX_32X32 ? 0 : 2;

  QUANT_PARAM quant_param;
  av1_setup_quant(tx_size, 0, AV1_XFORM_QUANT_FP, 0, &quant_param);

#if CONFIG_AV1_HIGHBITDEPTH
  if (is_cur_buf_hbd(xd)) {
    av1_highbd_quantize_fp_facade(coeff, pix_num, p, qcoeff, dqcoeff, eob,
                                  scan_order, &quant_param);
    *recon_error =
        av1_highbd_block_error(coeff, dqcoeff, pix_num, sse, xd->bd) >> shift;
  } else {
    av1_quantize_fp_facade(coeff, pix_num, p, qcoeff, dqcoeff, eob, scan_order,
                           &quant_param);
    *recon_error = av1_block_error(coeff, dqcoeff, pix_num, sse) >> shift;
  }
#else
  (void)xd;
  av1_quantize_fp_facade(coeff, pix_num, p, qcoeff, dqcoeff, eob, scan_order,
                         &quant_param);
  *recon_error = av1_block_error(coeff, dqcoeff, pix_num, sse) >> shift;
#endif  // CONFIG_AV1_HIGHBITDEPTH

  *recon_error = AOMMAX(*recon_error, 1);

  *sse = (*sse) >> shift;
  *sse = AOMMAX(*sse, 1);
}

static AOM_INLINE void set_tpl_stats_block_size(uint8_t *block_mis_log2,
                                                uint8_t *tpl_bsize_1d) {
  // tpl stats bsize: 2 means 16x16
  *block_mis_log2 = 2;
  // Block size used in tpl motion estimation
  *tpl_bsize_1d = 16;
  // MIN_TPL_BSIZE_1D = 16;
  assert(*tpl_bsize_1d >= 16);
}

void av1_setup_tpl_buffers(AV1_COMMON *const cm, TplParams *const tpl_data,
                           int lag_in_frames) {
  CommonModeInfoParams *const mi_params = &cm->mi_params;
  set_tpl_stats_block_size(&tpl_data->tpl_stats_block_mis_log2,
                           &tpl_data->tpl_bsize_1d);
  const uint8_t block_mis_log2 = tpl_data->tpl_stats_block_mis_log2;
  tpl_data->border_in_pixels =
      ALIGN_POWER_OF_TWO(tpl_data->tpl_bsize_1d + 2 * AOM_INTERP_EXTEND, 5);

  for (int frame = 0; frame < MAX_LENGTH_TPL_FRAME_STATS; ++frame) {
    const int mi_cols =
        ALIGN_POWER_OF_TWO(mi_params->mi_cols, MAX_MIB_SIZE_LOG2);
    const int mi_rows =
        ALIGN_POWER_OF_TWO(mi_params->mi_rows, MAX_MIB_SIZE_LOG2);
    TplDepFrame *tpl_frame = &tpl_data->tpl_stats_buffer[frame];
    tpl_frame->is_valid = 0;
    tpl_frame->width = mi_cols >> block_mis_log2;
    tpl_frame->height = mi_rows >> block_mis_log2;
    tpl_frame->stride = tpl_data->tpl_stats_buffer[frame].width;
    tpl_frame->mi_rows = mi_params->mi_rows;
    tpl_frame->mi_cols = mi_params->mi_cols;
    av1_tpl_stats_init_txfm_stats(tpl_frame, tpl_data->tpl_bsize_1d);
  }
  tpl_data->tpl_frame = &tpl_data->tpl_stats_buffer[REF_FRAMES + 1];

  // If lag_in_frames <= 1, TPL module is not invoked. Hence tpl recon and
  // stats buffers are not allocated.
  if (lag_in_frames <= 1) return;

  // TODO(aomedia:2873): Explore the allocation of tpl buffers based on
  // lag_in_frames.
  for (int frame = 0; frame < MAX_LAG_BUFFERS; ++frame) {
    CHECK_MEM_ERROR(
        cm, tpl_data->tpl_stats_pool[frame],
        aom_calloc(tpl_data->tpl_stats_buffer[frame].width *
                       tpl_data->tpl_stats_buffer[frame].height,
                   sizeof(*tpl_data->tpl_stats_buffer[frame].tpl_stats_ptr)));
    if (aom_alloc_frame_buffer(
            &tpl_data->tpl_rec_pool[frame], cm->width, cm->height,
            cm->seq_params.subsampling_x, cm->seq_params.subsampling_y,
            cm->seq_params.use_highbitdepth, tpl_data->border_in_pixels,
            cm->features.byte_alignment))
      aom_internal_error(&cm->error, AOM_CODEC_MEM_ERROR,
                         "Failed to allocate frame buffer");
  }
}

static AOM_INLINE void tpl_fwd_txfm(const int16_t *src_diff, int bw,
                                    tran_low_t *coeff, TX_SIZE tx_size,
                                    int bit_depth, int is_hbd) {
  TxfmParam txfm_param;
  txfm_param.tx_type = DCT_DCT;
  txfm_param.tx_size = tx_size;
  txfm_param.lossless = 0;
  txfm_param.tx_set_type = EXT_TX_SET_ALL16;

  txfm_param.bd = bit_depth;
  txfm_param.is_hbd = is_hbd;
  av1_fwd_txfm(src_diff, coeff, bw, &txfm_param);
}

static AOM_INLINE int64_t tpl_get_satd_cost(const MACROBLOCK *x,
                                            int16_t *src_diff, int diff_stride,
                                            const uint8_t *src, int src_stride,
                                            const uint8_t *dst, int dst_stride,
                                            tran_low_t *coeff, int bw, int bh,
                                            TX_SIZE tx_size) {
  const MACROBLOCKD *xd = &x->e_mbd;
  const int pix_num = bw * bh;

  av1_subtract_block(xd, bh, bw, src_diff, diff_stride, src, src_stride, dst,
                     dst_stride);
  tpl_fwd_txfm(src_diff, bw, coeff, tx_size, xd->bd, is_cur_buf_hbd(xd));
  return aom_satd(coeff, pix_num);
}

static int rate_estimator(const tran_low_t *qcoeff, int eob, TX_SIZE tx_size) {
  const SCAN_ORDER *const scan_order = &av1_scan_orders[tx_size][DCT_DCT];

  assert((1 << num_pels_log2_lookup[txsize_to_bsize[tx_size]]) >= eob);
  aom_clear_system_state();
  int rate_cost = 1;

  for (int idx = 0; idx < eob; ++idx) {
    int abs_level = abs(qcoeff[scan_order->scan[idx]]);
    rate_cost += (int)(log(abs_level + 1.0) / log(2.0)) + 1;
  }

  return (rate_cost << AV1_PROB_COST_SHIFT);
}

static AOM_INLINE void txfm_quant_rdcost(
    const MACROBLOCK *x, int16_t *src_diff, int diff_stride, uint8_t *src,
    int src_stride, uint8_t *dst, int dst_stride, tran_low_t *coeff,
    tran_low_t *qcoeff, tran_low_t *dqcoeff, int bw, int bh, TX_SIZE tx_size,
    int *rate_cost, int64_t *recon_error, int64_t *sse) {
  const MACROBLOCKD *xd = &x->e_mbd;
  uint16_t eob;
  av1_subtract_block(xd, bh, bw, src_diff, diff_stride, src, src_stride, dst,
                     dst_stride);
  tpl_fwd_txfm(src_diff, diff_stride, coeff, tx_size, xd->bd,
               is_cur_buf_hbd(xd));

  get_quantize_error(x, 0, coeff, qcoeff, dqcoeff, tx_size, &eob, recon_error,
                     sse);

  *rate_cost = rate_estimator(qcoeff, eob, tx_size);

  av1_inverse_transform_block(xd, dqcoeff, 0, DCT_DCT, tx_size, dst, dst_stride,
                              eob, 0);
}

static uint32_t motion_estimation(AV1_COMP *cpi, MACROBLOCK *x,
                                  uint8_t *cur_frame_buf,
                                  uint8_t *ref_frame_buf, int stride,
                                  int stride_ref, BLOCK_SIZE bsize,
                                  MV center_mv, int_mv *best_mv) {
  AV1_COMMON *cm = &cpi->common;
  MACROBLOCKD *const xd = &x->e_mbd;
  TPL_SPEED_FEATURES *tpl_sf = &cpi->sf.tpl_sf;
  int step_param;
  uint32_t bestsme = UINT_MAX;
  int distortion;
  uint32_t sse;
  int cost_list[5];
  FULLPEL_MV start_mv = get_fullmv_from_mv(&center_mv);

  // Setup frame pointers
  x->plane[0].src.buf = cur_frame_buf;
  x->plane[0].src.stride = stride;
  xd->plane[0].pre[0].buf = ref_frame_buf;
  xd->plane[0].pre[0].stride = stride_ref;

  step_param = tpl_sf->reduce_first_step_size;
  step_param = AOMMIN(step_param, MAX_MVSEARCH_STEPS - 2);

  const search_site_config *search_site_cfg =
      cpi->mv_search_params.search_site_cfg[SS_CFG_SRC];
  if (search_site_cfg->stride != stride_ref)
    search_site_cfg = cpi->mv_search_params.search_site_cfg[SS_CFG_LOOKAHEAD];
  assert(search_site_cfg->stride == stride_ref);

  FULLPEL_MOTION_SEARCH_PARAMS full_ms_params;
  av1_make_default_fullpel_ms_params(&full_ms_params, cpi, x, bsize, &center_mv,
                                     search_site_cfg,
                                     /*fine_search_interval=*/0);
  av1_set_mv_search_method(&full_ms_params, search_site_cfg,
                           tpl_sf->search_method);

  av1_full_pixel_search(start_mv, &full_ms_params, step_param,
                        cond_cost_list(cpi, cost_list), &best_mv->as_fullmv,
                        NULL);

  SUBPEL_MOTION_SEARCH_PARAMS ms_params;
  av1_make_default_subpel_ms_params(&ms_params, cpi, x, bsize, &center_mv,
                                    cost_list);
  ms_params.forced_stop = tpl_sf->subpel_force_stop;
  ms_params.var_params.subpel_search_type = USE_2_TAPS;
  ms_params.mv_cost_params.mv_cost_type = MV_COST_NONE;
  MV subpel_start_mv = get_mv_from_fullmv(&best_mv->as_fullmv);
  bestsme = cpi->mv_search_params.find_fractional_mv_step(
      xd, cm, &ms_params, subpel_start_mv, &best_mv->as_mv, &distortion, &sse,
      NULL);

  return bestsme;
}

typedef struct {
  int_mv mv;
  int sad;
} center_mv_t;

static int compare_sad(const void *a, const void *b) {
  const int diff = ((center_mv_t *)a)->sad - ((center_mv_t *)b)->sad;
  if (diff < 0)
    return -1;
  else if (diff > 0)
    return 1;
  return 0;
}

static int is_alike_mv(int_mv candidate_mv, center_mv_t *center_mvs,
                       int center_mvs_count, int skip_alike_starting_mv) {
  // MV difference threshold is in 1/8 precision.
  const int mv_diff_thr[3] = { 1, (8 << 3), (16 << 3) };
  int thr = mv_diff_thr[skip_alike_starting_mv];
  int i;

  for (i = 0; i < center_mvs_count; i++) {
    if (abs(center_mvs[i].mv.as_mv.col - candidate_mv.as_mv.col) < thr &&
        abs(center_mvs[i].mv.as_mv.row - candidate_mv.as_mv.row) < thr)
      return 1;
  }

  return 0;
}

static void get_rate_distortion(
    int *rate_cost, int64_t *recon_error, int16_t *src_diff, tran_low_t *coeff,
    tran_low_t *qcoeff, tran_low_t *dqcoeff, AV1_COMMON *cm, MACROBLOCK *x,
    const YV12_BUFFER_CONFIG *ref_frame_ptr[2], uint8_t *rec_buffer_pool[3],
    const int rec_stride_pool[3], TX_SIZE tx_size, PREDICTION_MODE best_mode,
    int mi_row, int mi_col, int use_y_only_rate_distortion) {
  *rate_cost = 0;
  *recon_error = 1;

  MACROBLOCKD *xd = &x->e_mbd;
  int is_compound = (best_mode == NEW_NEWMV);
  int num_planes = use_y_only_rate_distortion ? 1 : MAX_MB_PLANE;

  uint8_t *src_buffer_pool[MAX_MB_PLANE] = {
    xd->cur_buf->y_buffer,
    xd->cur_buf->u_buffer,
    xd->cur_buf->v_buffer,
  };
  const int src_stride_pool[MAX_MB_PLANE] = {
    xd->cur_buf->y_stride,
    xd->cur_buf->uv_stride,
    xd->cur_buf->uv_stride,
  };

  const int_interpfilters kernel =
      av1_broadcast_interp_filter(EIGHTTAP_REGULAR);

  for (int plane = 0; plane < num_planes; ++plane) {
    struct macroblockd_plane *pd = &xd->plane[plane];
    BLOCK_SIZE bsize_plane =
        ss_size_lookup[txsize_to_bsize[tx_size]][pd->subsampling_x]
                      [pd->subsampling_y];

    int dst_buffer_stride = rec_stride_pool[plane];
    int dst_mb_offset =
        ((mi_row * MI_SIZE * dst_buffer_stride) >> pd->subsampling_y) +
        ((mi_col * MI_SIZE) >> pd->subsampling_x);
    uint8_t *dst_buffer = rec_buffer_pool[plane] + dst_mb_offset;
    for (int ref = 0; ref < 1 + is_compound; ++ref) {
      if (!is_inter_mode(best_mode)) {
        av1_predict_intra_block(
            cm, xd, block_size_wide[bsize_plane], block_size_high[bsize_plane],
            max_txsize_rect_lookup[bsize_plane], best_mode, 0, 0,
            FILTER_INTRA_MODES, dst_buffer, dst_buffer_stride, dst_buffer,
            dst_buffer_stride, 0, 0, plane);
      } else {
        int_mv best_mv = xd->mi[0]->mv[ref];
        uint8_t *ref_buffer_pool[MAX_MB_PLANE] = {
          ref_frame_ptr[ref]->y_buffer,
          ref_frame_ptr[ref]->u_buffer,
          ref_frame_ptr[ref]->v_buffer,
        };
        InterPredParams inter_pred_params;
        struct buf_2d ref_buf = {
          NULL, ref_buffer_pool[plane],
          plane ? ref_frame_ptr[ref]->uv_width : ref_frame_ptr[ref]->y_width,
          plane ? ref_frame_ptr[ref]->uv_height : ref_frame_ptr[ref]->y_height,
          plane ? ref_frame_ptr[ref]->uv_stride : ref_frame_ptr[ref]->y_stride
        };
        av1_init_inter_params(&inter_pred_params, block_size_wide[bsize_plane],
                              block_size_high[bsize_plane],
                              (mi_row * MI_SIZE) >> pd->subsampling_y,
                              (mi_col * MI_SIZE) >> pd->subsampling_x,
                              pd->subsampling_x, pd->subsampling_y, xd->bd,
                              is_cur_buf_hbd(xd), 0,
                              xd->block_ref_scale_factors[0], &ref_buf, kernel);
        if (is_compound) av1_init_comp_mode(&inter_pred_params);
        inter_pred_params.conv_params = get_conv_params_no_round(
            ref, plane, xd->tmp_conv_dst, MAX_SB_SIZE, is_compound, xd->bd);

        av1_enc_build_one_inter_predictor(dst_buffer, dst_buffer_stride,
                                          &best_mv.as_mv, &inter_pred_params);
      }
    }

    int src_stride = src_stride_pool[plane];
    int src_mb_offset = ((mi_row * MI_SIZE * src_stride) >> pd->subsampling_y) +
                        ((mi_col * MI_SIZE) >> pd->subsampling_x);

    int this_rate = 1;
    int64_t this_recon_error = 1;
    int64_t sse;
    txfm_quant_rdcost(
        x, src_diff, block_size_wide[bsize_plane],
        src_buffer_pool[plane] + src_mb_offset, src_stride, dst_buffer,
        dst_buffer_stride, coeff, qcoeff, dqcoeff, block_size_wide[bsize_plane],
        block_size_high[bsize_plane], max_txsize_rect_lookup[bsize_plane],
        &this_rate, &this_recon_error, &sse);

    *recon_error += this_recon_error;
    *rate_cost += this_rate;
  }
}

static AOM_INLINE void mode_estimation(AV1_COMP *cpi,
                                       TplTxfmStats *tpl_txfm_stats,
                                       MACROBLOCK *x, int mi_row, int mi_col,
                                       BLOCK_SIZE bsize, TX_SIZE tx_size,
                                       TplDepStats *tpl_stats) {
  AV1_COMMON *cm = &cpi->common;
  const GF_GROUP *gf_group = &cpi->gf_group;

  (void)gf_group;

  MACROBLOCKD *xd = &x->e_mbd;
  TplParams *tpl_data = &cpi->tpl_data;
  TplDepFrame *tpl_frame = &tpl_data->tpl_frame[tpl_data->frame_idx];
  const uint8_t block_mis_log2 = tpl_data->tpl_stats_block_mis_log2;

  const int bw = 4 << mi_size_wide_log2[bsize];
  const int bh = 4 << mi_size_high_log2[bsize];
  const int_interpfilters kernel =
      av1_broadcast_interp_filter(EIGHTTAP_REGULAR);

  int64_t best_intra_cost = INT64_MAX;
  int64_t intra_cost;
  PREDICTION_MODE best_mode = DC_PRED;

  int mb_y_offset = mi_row * MI_SIZE * xd->cur_buf->y_stride + mi_col * MI_SIZE;
  uint8_t *src_mb_buffer = xd->cur_buf->y_buffer + mb_y_offset;
  int src_stride = xd->cur_buf->y_stride;

  int dst_mb_offset =
      mi_row * MI_SIZE * tpl_frame->rec_picture->y_stride + mi_col * MI_SIZE;
  uint8_t *dst_buffer = tpl_frame->rec_picture->y_buffer + dst_mb_offset;
  int dst_buffer_stride = tpl_frame->rec_picture->y_stride;
  int use_y_only_rate_distortion = cpi->sf.tpl_sf.use_y_only_rate_distortion;

  uint8_t *rec_buffer_pool[3] = {
    tpl_frame->rec_picture->y_buffer,
    tpl_frame->rec_picture->u_buffer,
    tpl_frame->rec_picture->v_buffer,
  };

  const int rec_stride_pool[3] = {
    tpl_frame->rec_picture->y_stride,
    tpl_frame->rec_picture->uv_stride,
    tpl_frame->rec_picture->uv_stride,
  };

  for (int plane = 1; plane < MAX_MB_PLANE; ++plane) {
    struct macroblockd_plane *pd = &xd->plane[plane];
    pd->subsampling_x = xd->cur_buf->subsampling_x;
    pd->subsampling_y = xd->cur_buf->subsampling_y;
  }

  // Number of pixels in a tpl block
  const int tpl_block_pels = tpl_data->tpl_bsize_1d * tpl_data->tpl_bsize_1d;
  // Allocate temporary buffers used in motion estimation.
  uint8_t *predictor8 = aom_memalign(32, tpl_block_pels * 2 * sizeof(uint8_t));
  int16_t *src_diff = aom_memalign(32, tpl_block_pels * sizeof(int16_t));
  tran_low_t *coeff = aom_memalign(32, tpl_block_pels * sizeof(tran_low_t));
  tran_low_t *qcoeff = aom_memalign(32, tpl_block_pels * sizeof(tran_low_t));
  tran_low_t *dqcoeff = aom_memalign(32, tpl_block_pels * sizeof(tran_low_t));
  uint8_t *predictor =
      is_cur_buf_hbd(xd) ? CONVERT_TO_BYTEPTR(predictor8) : predictor8;
  int64_t recon_error = 1;

  memset(tpl_stats, 0, sizeof(*tpl_stats));
  tpl_stats->ref_frame_index[0] = -1;
  tpl_stats->ref_frame_index[1] = -1;

  const int mi_width = mi_size_wide[bsize];
  const int mi_height = mi_size_high[bsize];
  set_mode_info_offsets(&cpi->common.mi_params, &cpi->mbmi_ext_info, x, xd,
                        mi_row, mi_col);
  set_mi_row_col(xd, &xd->tile, mi_row, mi_height, mi_col, mi_width,
                 cm->mi_params.mi_rows, cm->mi_params.mi_cols);
  set_plane_n4(xd, mi_size_wide[bsize], mi_size_high[bsize],
               av1_num_planes(cm));
  xd->mi[0]->bsize = bsize;
  xd->mi[0]->motion_mode = SIMPLE_TRANSLATION;

  // Intra prediction search
  xd->mi[0]->ref_frame[0] = INTRA_FRAME;

  // Pre-load the bottom left line.
  if (xd->left_available &&
      mi_row + tx_size_high_unit[tx_size] < xd->tile.mi_row_end) {
#if CONFIG_AV1_HIGHBITDEPTH
    if (is_cur_buf_hbd(xd)) {
      uint16_t *dst = CONVERT_TO_SHORTPTR(dst_buffer);
      for (int i = 0; i < bw; ++i)
        dst[(bw + i) * dst_buffer_stride - 1] =
            dst[(bw - 1) * dst_buffer_stride - 1];
    } else {
      for (int i = 0; i < bw; ++i)
        dst_buffer[(bw + i) * dst_buffer_stride - 1] =
            dst_buffer[(bw - 1) * dst_buffer_stride - 1];
    }
#else
    for (int i = 0; i < bw; ++i)
      dst_buffer[(bw + i) * dst_buffer_stride - 1] =
          dst_buffer[(bw - 1) * dst_buffer_stride - 1];
#endif
  }

  // if cpi->sf.tpl_sf.prune_intra_modes is on, then search only DC_PRED,
  // H_PRED, and V_PRED
  const PREDICTION_MODE last_intra_mode =
      cpi->sf.tpl_sf.prune_intra_modes ? D45_PRED : INTRA_MODE_END;
  for (PREDICTION_MODE mode = INTRA_MODE_START; mode < last_intra_mode;
       ++mode) {
    av1_predict_intra_block(cm, xd, block_size_wide[bsize],
                            block_size_high[bsize], tx_size, mode, 0, 0,
                            FILTER_INTRA_MODES, dst_buffer, dst_buffer_stride,
                            predictor, bw, 0, 0, 0);

    intra_cost = tpl_get_satd_cost(x, src_diff, bw, src_mb_buffer, src_stride,
                                   predictor, bw, coeff, bw, bh, tx_size);

    if (intra_cost < best_intra_cost) {
      best_intra_cost = intra_cost;
      best_mode = mode;
    }
  }

  // Motion compensated prediction
  xd->mi[0]->ref_frame[0] = INTRA_FRAME;
  xd->mi[0]->ref_frame[1] = NONE_FRAME;
  xd->mi[0]->compound_idx = 1;

  int best_rf_idx = -1;
  int_mv best_mv[2];
  int64_t inter_cost;
  int64_t best_inter_cost = INT64_MAX;
  int rf_idx;
  int_mv single_mv[INTER_REFS_PER_FRAME];

  best_mv[0].as_int = INVALID_MV;
  best_mv[1].as_int = INVALID_MV;

  for (rf_idx = 0; rf_idx < INTER_REFS_PER_FRAME; ++rf_idx) {
    single_mv[rf_idx].as_int = INVALID_MV;
    if (tpl_data->ref_frame[rf_idx] == NULL ||
        tpl_data->src_ref_frame[rf_idx] == NULL) {
      tpl_stats->mv[rf_idx].as_int = INVALID_MV;
      continue;
    }

    const YV12_BUFFER_CONFIG *ref_frame_ptr = tpl_data->src_ref_frame[rf_idx];
    int ref_mb_offset =
        mi_row * MI_SIZE * ref_frame_ptr->y_stride + mi_col * MI_SIZE;
    uint8_t *ref_mb = ref_frame_ptr->y_buffer + ref_mb_offset;
    int ref_stride = ref_frame_ptr->y_stride;

    int_mv best_rfidx_mv = { 0 };
    uint32_t bestsme = UINT32_MAX;

    center_mv_t center_mvs[4] = { { { 0 }, INT_MAX },
                                  { { 0 }, INT_MAX },
                                  { { 0 }, INT_MAX },
                                  { { 0 }, INT_MAX } };
    int refmv_count = 1;
    int idx;

    if (xd->up_available) {
      TplDepStats *ref_tpl_stats = &tpl_frame->tpl_stats_ptr[av1_tpl_ptr_pos(
          mi_row - mi_height, mi_col, tpl_frame->stride, block_mis_log2)];
      if (!is_alike_mv(ref_tpl_stats->mv[rf_idx], center_mvs, refmv_count,
                       cpi->sf.tpl_sf.skip_alike_starting_mv)) {
        center_mvs[refmv_count].mv.as_int = ref_tpl_stats->mv[rf_idx].as_int;
        ++refmv_count;
      }
    }

    if (xd->left_available) {
      TplDepStats *ref_tpl_stats = &tpl_frame->tpl_stats_ptr[av1_tpl_ptr_pos(
          mi_row, mi_col - mi_width, tpl_frame->stride, block_mis_log2)];
      if (!is_alike_mv(ref_tpl_stats->mv[rf_idx], center_mvs, refmv_count,
                       cpi->sf.tpl_sf.skip_alike_starting_mv)) {
        center_mvs[refmv_count].mv.as_int = ref_tpl_stats->mv[rf_idx].as_int;
        ++refmv_count;
      }
    }

    if (xd->up_available && mi_col + mi_width < xd->tile.mi_col_end) {
      TplDepStats *ref_tpl_stats = &tpl_frame->tpl_stats_ptr[av1_tpl_ptr_pos(
          mi_row - mi_height, mi_col + mi_width, tpl_frame->stride,
          block_mis_log2)];
      if (!is_alike_mv(ref_tpl_stats->mv[rf_idx], center_mvs, refmv_count,
                       cpi->sf.tpl_sf.skip_alike_starting_mv)) {
        center_mvs[refmv_count].mv.as_int = ref_tpl_stats->mv[rf_idx].as_int;
        ++refmv_count;
      }
    }

    // Prune starting mvs
    if (cpi->sf.tpl_sf.prune_starting_mv) {
      // Get each center mv's sad.
      for (idx = 0; idx < refmv_count; ++idx) {
        FULLPEL_MV mv = get_fullmv_from_mv(&center_mvs[idx].mv.as_mv);
        clamp_fullmv(&mv, &x->mv_limits);
        center_mvs[idx].sad = (int)cpi->fn_ptr[bsize].sdf(
            src_mb_buffer, src_stride, &ref_mb[mv.row * ref_stride + mv.col],
            ref_stride);
      }

      // Rank center_mv using sad.
      if (refmv_count > 1) {
        qsort(center_mvs, refmv_count, sizeof(center_mvs[0]), compare_sad);
      }
      refmv_count = AOMMIN(4 - cpi->sf.tpl_sf.prune_starting_mv, refmv_count);
      // Further reduce number of refmv based on sad difference.
      if (refmv_count > 1) {
        int last_sad = center_mvs[refmv_count - 1].sad;
        int second_to_last_sad = center_mvs[refmv_count - 2].sad;
        if ((last_sad - second_to_last_sad) * 5 > second_to_last_sad)
          refmv_count--;
      }
    }

    for (idx = 0; idx < refmv_count; ++idx) {
      int_mv this_mv;
      uint32_t thissme = motion_estimation(cpi, x, src_mb_buffer, ref_mb,
                                           src_stride, ref_stride, bsize,
                                           center_mvs[idx].mv.as_mv, &this_mv);

      if (thissme < bestsme) {
        bestsme = thissme;
        best_rfidx_mv = this_mv;
      }
    }

    tpl_stats->mv[rf_idx].as_int = best_rfidx_mv.as_int;
    single_mv[rf_idx] = best_rfidx_mv;

    struct buf_2d ref_buf = { NULL, ref_frame_ptr->y_buffer,
                              ref_frame_ptr->y_width, ref_frame_ptr->y_height,
                              ref_frame_ptr->y_stride };
    InterPredParams inter_pred_params;
    av1_init_inter_params(&inter_pred_params, bw, bh, mi_row * MI_SIZE,
                          mi_col * MI_SIZE, 0, 0, xd->bd, is_cur_buf_hbd(xd), 0,
                          &tpl_data->sf, &ref_buf, kernel);
    inter_pred_params.conv_params = get_conv_params(0, 0, xd->bd);

    av1_enc_build_one_inter_predictor(predictor, bw, &best_rfidx_mv.as_mv,
                                      &inter_pred_params);

    inter_cost = tpl_get_satd_cost(x, src_diff, bw, src_mb_buffer, src_stride,
                                   predictor, bw, coeff, bw, bh, tx_size);
    // Store inter cost for each ref frame
    tpl_stats->pred_error[rf_idx] = AOMMAX(1, inter_cost);

    if (inter_cost < best_inter_cost) {
      best_rf_idx = rf_idx;

      best_inter_cost = inter_cost;
      best_mv[0].as_int = best_rfidx_mv.as_int;
      if (best_inter_cost < best_intra_cost) {
        best_mode = NEWMV;
        xd->mi[0]->ref_frame[0] = best_rf_idx + LAST_FRAME;
        xd->mi[0]->mv[0].as_int = best_mv[0].as_int;
      }
    }
  }

  int comp_ref_frames[3][2] = {
    { 0, 4 },
    { 0, 6 },
    { 3, 6 },
  };

  xd->mi_row = mi_row;
  xd->mi_col = mi_col;
  int best_cmp_rf_idx = -1;
  for (int cmp_rf_idx = 0; cmp_rf_idx < 3 && cpi->sf.tpl_sf.allow_compound_pred;
       ++cmp_rf_idx) {
    int rf_idx0 = comp_ref_frames[cmp_rf_idx][0];
    int rf_idx1 = comp_ref_frames[cmp_rf_idx][1];

    if (tpl_data->ref_frame[rf_idx0] == NULL ||
        tpl_data->src_ref_frame[rf_idx0] == NULL ||
        tpl_data->ref_frame[rf_idx1] == NULL ||
        tpl_data->src_ref_frame[rf_idx1] == NULL) {
      continue;
    }

    const YV12_BUFFER_CONFIG *ref_frame_ptr[2] = {
      tpl_data->src_ref_frame[rf_idx0],
      tpl_data->src_ref_frame[rf_idx1],
    };

    xd->mi[0]->ref_frame[0] = LAST_FRAME;
    xd->mi[0]->ref_frame[1] = ALTREF_FRAME;

    struct buf_2d yv12_mb[2][MAX_MB_PLANE];
    for (int i = 0; i < 2; ++i) {
      av1_setup_pred_block(xd, yv12_mb[i], ref_frame_ptr[i],
                           xd->block_ref_scale_factors[i],
                           xd->block_ref_scale_factors[i], MAX_MB_PLANE);
      for (int plane = 0; plane < MAX_MB_PLANE; ++plane) {
        xd->plane[plane].pre[i] = yv12_mb[i][plane];
      }
    }

    int_mv tmp_mv[2] = { single_mv[rf_idx0], single_mv[rf_idx1] };
    int rate_mv;
    av1_joint_motion_search(cpi, x, bsize, tmp_mv, NULL, 0, &rate_mv,
                            !cpi->sf.mv_sf.disable_second_mv);

    for (int ref = 0; ref < 2; ++ref) {
      struct buf_2d ref_buf = { NULL, ref_frame_ptr[ref]->y_buffer,
                                ref_frame_ptr[ref]->y_width,
                                ref_frame_ptr[ref]->y_height,
                                ref_frame_ptr[ref]->y_stride };
      InterPredParams inter_pred_params;
      av1_init_inter_params(&inter_pred_params, bw, bh, mi_row * MI_SIZE,
                            mi_col * MI_SIZE, 0, 0, xd->bd, is_cur_buf_hbd(xd),
                            0, &tpl_data->sf, &ref_buf, kernel);
      av1_init_comp_mode(&inter_pred_params);

      inter_pred_params.conv_params = get_conv_params_no_round(
          ref, 0, xd->tmp_conv_dst, MAX_SB_SIZE, 1, xd->bd);

      av1_enc_build_one_inter_predictor(predictor, bw, &tmp_mv[ref].as_mv,
                                        &inter_pred_params);
    }
    inter_cost = tpl_get_satd_cost(x, src_diff, bw, src_mb_buffer, src_stride,
                                   predictor, bw, coeff, bw, bh, tx_size);
    if (inter_cost < best_inter_cost) {
      best_cmp_rf_idx = cmp_rf_idx;
      best_inter_cost = inter_cost;
      best_mv[0] = tmp_mv[0];
      best_mv[1] = tmp_mv[1];

      if (best_inter_cost < best_intra_cost) {
        best_mode = NEW_NEWMV;
        xd->mi[0]->ref_frame[0] = rf_idx0 + LAST_FRAME;
        xd->mi[0]->ref_frame[1] = rf_idx1 + LAST_FRAME;
      }
    }
  }

  if (best_inter_cost < INT64_MAX) {
    xd->mi[0]->mv[0].as_int = best_mv[0].as_int;
    xd->mi[0]->mv[1].as_int = best_mv[1].as_int;
    const YV12_BUFFER_CONFIG *ref_frame_ptr[2] = {
      best_cmp_rf_idx >= 0
          ? tpl_data->src_ref_frame[comp_ref_frames[best_cmp_rf_idx][0]]
          : tpl_data->src_ref_frame[best_rf_idx],
      best_cmp_rf_idx >= 0
          ? tpl_data->src_ref_frame[comp_ref_frames[best_cmp_rf_idx][1]]
          : NULL,
    };
    int rate_cost = 1;
    get_rate_distortion(&rate_cost, &recon_error, src_diff, coeff, qcoeff,
                        dqcoeff, cm, x, ref_frame_ptr, rec_buffer_pool,
                        rec_stride_pool, tx_size, best_mode, mi_row, mi_col,
                        use_y_only_rate_distortion);
    tpl_stats->srcrf_rate = rate_cost << TPL_DEP_COST_SCALE_LOG2;
  }

  best_intra_cost = AOMMAX(best_intra_cost, 1);
  best_inter_cost = AOMMIN(best_intra_cost, best_inter_cost);
  tpl_stats->inter_cost = best_inter_cost << TPL_DEP_COST_SCALE_LOG2;
  tpl_stats->intra_cost = best_intra_cost << TPL_DEP_COST_SCALE_LOG2;

  tpl_stats->srcrf_dist = recon_error << (TPL_DEP_COST_SCALE_LOG2);

  // Final encode
  int rate_cost = 0;
  const YV12_BUFFER_CONFIG *ref_frame_ptr[2];

  ref_frame_ptr[0] =
      best_mode == NEW_NEWMV
          ? tpl_data->ref_frame[comp_ref_frames[best_cmp_rf_idx][0]]
          : best_rf_idx >= 0 ? tpl_data->ref_frame[best_rf_idx] : NULL;
  ref_frame_ptr[1] =
      best_mode == NEW_NEWMV
          ? tpl_data->ref_frame[comp_ref_frames[best_cmp_rf_idx][1]]
          : NULL;
  get_rate_distortion(&rate_cost, &recon_error, src_diff, coeff, qcoeff,
                      dqcoeff, cm, x, ref_frame_ptr, rec_buffer_pool,
                      rec_stride_pool, tx_size, best_mode, mi_row, mi_col,
                      use_y_only_rate_distortion);

  tpl_stats_record_txfm_block(tpl_txfm_stats, coeff, tpl_frame->coeff_num);

  tpl_stats->recrf_dist = recon_error << (TPL_DEP_COST_SCALE_LOG2);
  tpl_stats->recrf_rate = rate_cost << TPL_DEP_COST_SCALE_LOG2;
  if (!is_inter_mode(best_mode)) {
    tpl_stats->srcrf_dist = recon_error << (TPL_DEP_COST_SCALE_LOG2);
    tpl_stats->srcrf_rate = rate_cost << TPL_DEP_COST_SCALE_LOG2;
  }

  tpl_stats->recrf_dist = AOMMAX(tpl_stats->srcrf_dist, tpl_stats->recrf_dist);
  tpl_stats->recrf_rate = AOMMAX(tpl_stats->srcrf_rate, tpl_stats->recrf_rate);

  if (best_mode == NEW_NEWMV) {
    ref_frame_ptr[0] = tpl_data->ref_frame[comp_ref_frames[best_cmp_rf_idx][0]];
    ref_frame_ptr[1] =
        tpl_data->src_ref_frame[comp_ref_frames[best_cmp_rf_idx][1]];
    get_rate_distortion(&rate_cost, &recon_error, src_diff, coeff, qcoeff,
                        dqcoeff, cm, x, ref_frame_ptr, rec_buffer_pool,
                        rec_stride_pool, tx_size, best_mode, mi_row, mi_col,
                        use_y_only_rate_distortion);
    tpl_stats->cmp_recrf_dist[0] = recon_error << TPL_DEP_COST_SCALE_LOG2;
    tpl_stats->cmp_recrf_rate[0] = rate_cost << TPL_DEP_COST_SCALE_LOG2;

    tpl_stats->cmp_recrf_dist[0] =
        AOMMAX(tpl_stats->srcrf_dist, tpl_stats->cmp_recrf_dist[0]);
    tpl_stats->cmp_recrf_rate[0] =
        AOMMAX(tpl_stats->srcrf_rate, tpl_stats->cmp_recrf_rate[0]);

    tpl_stats->cmp_recrf_dist[0] =
        AOMMIN(tpl_stats->recrf_dist, tpl_stats->cmp_recrf_dist[0]);
    tpl_stats->cmp_recrf_rate[0] =
        AOMMIN(tpl_stats->recrf_rate, tpl_stats->cmp_recrf_rate[0]);

    rate_cost = 0;
    ref_frame_ptr[0] =
        tpl_data->src_ref_frame[comp_ref_frames[best_cmp_rf_idx][0]];
    ref_frame_ptr[1] = tpl_data->ref_frame[comp_ref_frames[best_cmp_rf_idx][1]];
    get_rate_distortion(&rate_cost, &recon_error, src_diff, coeff, qcoeff,
                        dqcoeff, cm, x, ref_frame_ptr, rec_buffer_pool,
                        rec_stride_pool, tx_size, best_mode, mi_row, mi_col,
                        use_y_only_rate_distortion);
    tpl_stats->cmp_recrf_dist[1] = recon_error << TPL_DEP_COST_SCALE_LOG2;
    tpl_stats->cmp_recrf_rate[1] = rate_cost << TPL_DEP_COST_SCALE_LOG2;

    tpl_stats->cmp_recrf_dist[1] =
        AOMMAX(tpl_stats->srcrf_dist, tpl_stats->cmp_recrf_dist[1]);
    tpl_stats->cmp_recrf_rate[1] =
        AOMMAX(tpl_stats->srcrf_rate, tpl_stats->cmp_recrf_rate[1]);

    tpl_stats->cmp_recrf_dist[1] =
        AOMMIN(tpl_stats->recrf_dist, tpl_stats->cmp_recrf_dist[1]);
    tpl_stats->cmp_recrf_rate[1] =
        AOMMIN(tpl_stats->recrf_rate, tpl_stats->cmp_recrf_rate[1]);
  }

  if (best_mode == NEWMV) {
    tpl_stats->mv[best_rf_idx] = best_mv[0];
    tpl_stats->ref_frame_index[0] = best_rf_idx;
    tpl_stats->ref_frame_index[1] = NONE_FRAME;
  } else if (best_mode == NEW_NEWMV) {
    tpl_stats->ref_frame_index[0] = comp_ref_frames[best_cmp_rf_idx][0];
    tpl_stats->ref_frame_index[1] = comp_ref_frames[best_cmp_rf_idx][1];
    tpl_stats->mv[tpl_stats->ref_frame_index[0]] = best_mv[0];
    tpl_stats->mv[tpl_stats->ref_frame_index[1]] = best_mv[1];
  }

  for (int idy = 0; idy < mi_height; ++idy) {
    for (int idx = 0; idx < mi_width; ++idx) {
      if ((xd->mb_to_right_edge >> (3 + MI_SIZE_LOG2)) + mi_width > idx &&
          (xd->mb_to_bottom_edge >> (3 + MI_SIZE_LOG2)) + mi_height > idy) {
        xd->mi[idx + idy * cm->mi_params.mi_stride] = xd->mi[0];
      }
    }
  }

  // Free temporary buffers.
  aom_free(predictor8);
  aom_free(src_diff);
  aom_free(coeff);
  aom_free(qcoeff);
  aom_free(dqcoeff);
}

static int round_floor(int ref_pos, int bsize_pix) {
  int round;
  if (ref_pos < 0)
    round = -(1 + (-ref_pos - 1) / bsize_pix);
  else
    round = ref_pos / bsize_pix;

  return round;
}

static int get_overlap_area(int grid_pos_row, int grid_pos_col, int ref_pos_row,
                            int ref_pos_col, int block, BLOCK_SIZE bsize) {
  int width = 0, height = 0;
  int bw = 4 << mi_size_wide_log2[bsize];
  int bh = 4 << mi_size_high_log2[bsize];

  switch (block) {
    case 0:
      width = grid_pos_col + bw - ref_pos_col;
      height = grid_pos_row + bh - ref_pos_row;
      break;
    case 1:
      width = ref_pos_col + bw - grid_pos_col;
      height = grid_pos_row + bh - ref_pos_row;
      break;
    case 2:
      width = grid_pos_col + bw - ref_pos_col;
      height = ref_pos_row + bh - grid_pos_row;
      break;
    case 3:
      width = ref_pos_col + bw - grid_pos_col;
      height = ref_pos_row + bh - grid_pos_row;
      break;
    default: assert(0);
  }
  int overlap_area = width * height;
  return overlap_area;
}

int av1_tpl_ptr_pos(int mi_row, int mi_col, int stride, uint8_t right_shift) {
  return (mi_row >> right_shift) * stride + (mi_col >> right_shift);
}

static int64_t delta_rate_cost(int64_t delta_rate, int64_t recrf_dist,
                               int64_t srcrf_dist, int pix_num) {
  double beta = (double)srcrf_dist / recrf_dist;
  int64_t rate_cost = delta_rate;

  if (srcrf_dist <= 128) return rate_cost;

  double dr =
      (double)(delta_rate >> (TPL_DEP_COST_SCALE_LOG2 + AV1_PROB_COST_SHIFT)) /
      pix_num;

  double log_den = log(beta) / log(2.0) + 2.0 * dr;

  if (log_den > log(10.0) / log(2.0)) {
    rate_cost = (int64_t)((log(1.0 / beta) * pix_num) / log(2.0) / 2.0);
    rate_cost <<= (TPL_DEP_COST_SCALE_LOG2 + AV1_PROB_COST_SHIFT);
    return rate_cost;
  }

  double num = pow(2.0, log_den);
  double den = num * beta + (1 - beta) * beta;

  rate_cost = (int64_t)((pix_num * log(num / den)) / log(2.0) / 2.0);

  rate_cost <<= (TPL_DEP_COST_SCALE_LOG2 + AV1_PROB_COST_SHIFT);

  return rate_cost;
}

static AOM_INLINE void tpl_model_update_b(TplParams *const tpl_data, int mi_row,
                                          int mi_col, const BLOCK_SIZE bsize,
                                          int frame_idx, int ref) {
  aom_clear_system_state();
  TplDepFrame *tpl_frame_ptr = &tpl_data->tpl_frame[frame_idx];
  TplDepStats *tpl_ptr = tpl_frame_ptr->tpl_stats_ptr;
  TplDepFrame *tpl_frame = tpl_data->tpl_frame;
  const uint8_t block_mis_log2 = tpl_data->tpl_stats_block_mis_log2;
  TplDepStats *tpl_stats_ptr = &tpl_ptr[av1_tpl_ptr_pos(
      mi_row, mi_col, tpl_frame->stride, block_mis_log2)];

  int is_compound = tpl_stats_ptr->ref_frame_index[1] >= 0;

  if (tpl_stats_ptr->ref_frame_index[ref] < 0) return;
  const int ref_frame_index = tpl_stats_ptr->ref_frame_index[ref];
  TplDepFrame *ref_tpl_frame =
      &tpl_frame[tpl_frame[frame_idx].ref_map_index[ref_frame_index]];
  TplDepStats *ref_stats_ptr = ref_tpl_frame->tpl_stats_ptr;

  if (tpl_frame[frame_idx].ref_map_index[ref_frame_index] < 0) return;

  const FULLPEL_MV full_mv =
      get_fullmv_from_mv(&tpl_stats_ptr->mv[ref_frame_index].as_mv);
  const int ref_pos_row = mi_row * MI_SIZE + full_mv.row;
  const int ref_pos_col = mi_col * MI_SIZE + full_mv.col;

  const int bw = 4 << mi_size_wide_log2[bsize];
  const int bh = 4 << mi_size_high_log2[bsize];
  const int mi_height = mi_size_high[bsize];
  const int mi_width = mi_size_wide[bsize];
  const int pix_num = bw * bh;

  // top-left on grid block location in pixel
  int grid_pos_row_base = round_floor(ref_pos_row, bh) * bh;
  int grid_pos_col_base = round_floor(ref_pos_col, bw) * bw;
  int block;

  int64_t srcrf_dist = is_compound ? tpl_stats_ptr->cmp_recrf_dist[!ref]
                                   : tpl_stats_ptr->srcrf_dist;
  int64_t srcrf_rate = is_compound ? tpl_stats_ptr->cmp_recrf_rate[!ref]
                                   : tpl_stats_ptr->srcrf_rate;

  int64_t cur_dep_dist = tpl_stats_ptr->recrf_dist - srcrf_dist;
  int64_t mc_dep_dist =
      (int64_t)(tpl_stats_ptr->mc_dep_dist *
                ((double)(tpl_stats_ptr->recrf_dist - srcrf_dist) /
                 tpl_stats_ptr->recrf_dist));
  int64_t delta_rate = tpl_stats_ptr->recrf_rate - srcrf_rate;
  int64_t mc_dep_rate =
      delta_rate_cost(tpl_stats_ptr->mc_dep_rate, tpl_stats_ptr->recrf_dist,
                      srcrf_dist, pix_num);

  for (block = 0; block < 4; ++block) {
    int grid_pos_row = grid_pos_row_base + bh * (block >> 1);
    int grid_pos_col = grid_pos_col_base + bw * (block & 0x01);

    if (grid_pos_row >= 0 && grid_pos_row < ref_tpl_frame->mi_rows * MI_SIZE &&
        grid_pos_col >= 0 && grid_pos_col < ref_tpl_frame->mi_cols * MI_SIZE) {
      int overlap_area = get_overlap_area(
          grid_pos_row, grid_pos_col, ref_pos_row, ref_pos_col, block, bsize);
      int ref_mi_row = round_floor(grid_pos_row, bh) * mi_height;
      int ref_mi_col = round_floor(grid_pos_col, bw) * mi_width;
      assert((1 << block_mis_log2) == mi_height);
      assert((1 << block_mis_log2) == mi_width);
      TplDepStats *des_stats = &ref_stats_ptr[av1_tpl_ptr_pos(
          ref_mi_row, ref_mi_col, ref_tpl_frame->stride, block_mis_log2)];
      des_stats->mc_dep_dist +=
          ((cur_dep_dist + mc_dep_dist) * overlap_area) / pix_num;
      des_stats->mc_dep_rate +=
          ((delta_rate + mc_dep_rate) * overlap_area) / pix_num;
    }
  }
}

static AOM_INLINE void tpl_model_update(TplParams *const tpl_data, int mi_row,
                                        int mi_col, int frame_idx) {
  const BLOCK_SIZE tpl_stats_block_size =
      convert_length_to_bsize(MI_SIZE << tpl_data->tpl_stats_block_mis_log2);
  tpl_model_update_b(tpl_data, mi_row, mi_col, tpl_stats_block_size, frame_idx,
                     0);
  tpl_model_update_b(tpl_data, mi_row, mi_col, tpl_stats_block_size, frame_idx,
                     1);
}

static AOM_INLINE void tpl_model_store(TplDepStats *tpl_stats_ptr, int mi_row,
                                       int mi_col, int stride,
                                       const TplDepStats *src_stats,
                                       uint8_t block_mis_log2) {
  int index = av1_tpl_ptr_pos(mi_row, mi_col, stride, block_mis_log2);
  TplDepStats *tpl_ptr = &tpl_stats_ptr[index];
  *tpl_ptr = *src_stats;
  tpl_ptr->intra_cost = AOMMAX(1, tpl_ptr->intra_cost);
  tpl_ptr->inter_cost = AOMMAX(1, tpl_ptr->inter_cost);
  tpl_ptr->srcrf_dist = AOMMAX(1, tpl_ptr->srcrf_dist);
  tpl_ptr->recrf_dist = AOMMAX(1, tpl_ptr->recrf_dist);
  tpl_ptr->srcrf_rate = AOMMAX(1, tpl_ptr->srcrf_rate);
  tpl_ptr->recrf_rate = AOMMAX(1, tpl_ptr->recrf_rate);
  tpl_ptr->cmp_recrf_dist[0] = AOMMAX(1, tpl_ptr->cmp_recrf_dist[0]);
  tpl_ptr->cmp_recrf_dist[1] = AOMMAX(1, tpl_ptr->cmp_recrf_dist[1]);
  tpl_ptr->cmp_recrf_rate[0] = AOMMAX(1, tpl_ptr->cmp_recrf_rate[0]);
  tpl_ptr->cmp_recrf_rate[1] = AOMMAX(1, tpl_ptr->cmp_recrf_rate[1]);
}

// Reset the ref and source frame pointers of tpl_data.
static AOM_INLINE void tpl_reset_src_ref_frames(TplParams *tpl_data) {
  for (int i = 0; i < INTER_REFS_PER_FRAME; ++i) {
    tpl_data->ref_frame[i] = NULL;
    tpl_data->src_ref_frame[i] = NULL;
  }
}

static AOM_INLINE int get_gop_length(const GF_GROUP *gf_group) {
  int gop_length = AOMMIN(gf_group->size, MAX_TPL_FRAME_IDX - 1);
  return gop_length;
}

// Initialize the mc_flow parameters used in computing tpl data.
static AOM_INLINE void init_mc_flow_dispenser(AV1_COMP *cpi, int frame_idx,
                                              int pframe_qindex) {
  TplParams *const tpl_data = &cpi->tpl_data;
  TplDepFrame *tpl_frame = &tpl_data->tpl_frame[frame_idx];
  const YV12_BUFFER_CONFIG *this_frame = tpl_frame->gf_picture;
  const YV12_BUFFER_CONFIG *ref_frames_ordered[INTER_REFS_PER_FRAME];
  uint32_t ref_frame_display_indices[INTER_REFS_PER_FRAME];
  GF_GROUP *gf_group = &cpi->gf_group;
  int ref_pruning_enabled = is_frame_eligible_for_ref_pruning(
      gf_group, cpi->sf.inter_sf.selective_ref_frame,
      cpi->sf.tpl_sf.prune_ref_frames_in_tpl, frame_idx);
  int gop_length = get_gop_length(gf_group);
  int ref_frame_flags;
  AV1_COMMON *cm = &cpi->common;
  int rdmult, idx;
  ThreadData *td = &cpi->td;
  MACROBLOCK *x = &td->mb;
  MACROBLOCKD *xd = &x->e_mbd;
  TplTxfmStats *tpl_txfm_stats = &td->tpl_txfm_stats;
  tpl_data->frame_idx = frame_idx;
  tpl_reset_src_ref_frames(tpl_data);
  av1_tile_init(&xd->tile, cm, 0, 0);

  // Setup scaling factor
  av1_setup_scale_factors_for_frame(
      &tpl_data->sf, this_frame->y_crop_width, this_frame->y_crop_height,
      this_frame->y_crop_width, this_frame->y_crop_height);

  xd->cur_buf = this_frame;

  for (idx = 0; idx < INTER_REFS_PER_FRAME; ++idx) {
    TplDepFrame *tpl_ref_frame =
        &tpl_data->tpl_frame[tpl_frame->ref_map_index[idx]];
    tpl_data->ref_frame[idx] = tpl_ref_frame->rec_picture;
    tpl_data->src_ref_frame[idx] = tpl_ref_frame->gf_picture;
    ref_frame_display_indices[idx] = tpl_ref_frame->frame_display_index;
  }

  // Store the reference frames based on priority order
  for (int i = 0; i < INTER_REFS_PER_FRAME; ++i) {
    ref_frames_ordered[i] =
        tpl_data->ref_frame[ref_frame_priority_order[i] - 1];
  }

  // Work out which reference frame slots may be used.
  ref_frame_flags = get_ref_frame_flags(&cpi->sf, ref_frames_ordered,
                                        cpi->ext_flags.ref_frame_flags);

  enforce_max_ref_frames(cpi, &ref_frame_flags, ref_frame_display_indices,
                         tpl_frame->frame_display_index);

  // Prune reference frames
  for (idx = 0; idx < INTER_REFS_PER_FRAME; ++idx) {
    if ((ref_frame_flags & (1 << idx)) == 0) {
      tpl_data->ref_frame[idx] = NULL;
    }
  }

  // Skip motion estimation w.r.t. reference frames which are not
  // considered in RD search, using "selective_ref_frame" speed feature.
  // The reference frame pruning is not enabled for frames beyond the gop
  // length, as there are fewer reference frames and the reference frames
  // differ from the frames considered during RD search.
  if (ref_pruning_enabled && (frame_idx < gop_length)) {
    for (idx = 0; idx < INTER_REFS_PER_FRAME; ++idx) {
      const MV_REFERENCE_FRAME refs[2] = { idx + 1, NONE_FRAME };
      if (prune_ref_by_selective_ref_frame(cpi, NULL, refs,
                                           ref_frame_display_indices)) {
        tpl_data->ref_frame[idx] = NULL;
      }
    }
  }

  // Make a temporary mbmi for tpl model
  MB_MODE_INFO mbmi;
  memset(&mbmi, 0, sizeof(mbmi));
  MB_MODE_INFO *mbmi_ptr = &mbmi;
  xd->mi = &mbmi_ptr;

  xd->block_ref_scale_factors[0] = &tpl_data->sf;
  xd->block_ref_scale_factors[1] = &tpl_data->sf;

  const int base_qindex = pframe_qindex;
  // Get rd multiplier set up.
  rdmult = (int)av1_compute_rd_mult(cpi, base_qindex);
  if (rdmult < 1) rdmult = 1;
  av1_set_error_per_bit(&x->errorperbit, rdmult);
  av1_set_sad_per_bit(cpi, &x->sadperbit, base_qindex);

  tpl_frame->is_valid = 1;

  cm->quant_params.base_qindex = base_qindex;
  av1_frame_init_quantizer(cpi);

  tpl_frame->base_rdmult = av1_compute_rd_mult_based_on_qindex(
                               cm->seq_params.bit_depth, pframe_qindex) /
                           6;

  memset(tpl_txfm_stats, 0, sizeof(*tpl_txfm_stats));
}

// This function stores the motion estimation dependencies of all the blocks in
// a row
void av1_mc_flow_dispenser_row(AV1_COMP *cpi, TplTxfmStats *tpl_txfm_stats,
                               MACROBLOCK *x, int mi_row, BLOCK_SIZE bsize,
                               TX_SIZE tx_size) {
  AV1_COMMON *const cm = &cpi->common;
  MultiThreadInfo *const mt_info = &cpi->mt_info;
  AV1TplRowMultiThreadInfo *const tpl_row_mt = &mt_info->tpl_row_mt;
  const CommonModeInfoParams *const mi_params = &cm->mi_params;
  const int mi_width = mi_size_wide[bsize];
  TplParams *const tpl_data = &cpi->tpl_data;
  TplDepFrame *tpl_frame = &tpl_data->tpl_frame[tpl_data->frame_idx];
  MACROBLOCKD *xd = &x->e_mbd;

  const int tplb_cols_in_tile =
      ROUND_POWER_OF_TWO(mi_params->mi_cols, mi_size_wide_log2[bsize]);
  const int tplb_row = ROUND_POWER_OF_TWO(mi_row, mi_size_high_log2[bsize]);
  assert(mi_size_high[bsize] == (1 << tpl_data->tpl_stats_block_mis_log2));
  assert(mi_size_wide[bsize] == (1 << tpl_data->tpl_stats_block_mis_log2));

  for (int mi_col = 0, tplb_col_in_tile = 0; mi_col < mi_params->mi_cols;
       mi_col += mi_width, tplb_col_in_tile++) {
    (*tpl_row_mt->sync_read_ptr)(&tpl_data->tpl_mt_sync, tplb_row,
                                 tplb_col_in_tile);
    TplDepStats tpl_stats;

    // Motion estimation column boundary
    av1_set_mv_col_limits(mi_params, &x->mv_limits, mi_col, mi_width,
                          tpl_data->border_in_pixels);
    xd->mb_to_left_edge = -GET_MV_SUBPEL(mi_col * MI_SIZE);
    xd->mb_to_right_edge =
        GET_MV_SUBPEL(mi_params->mi_cols - mi_width - mi_col);
    mode_estimation(cpi, tpl_txfm_stats, x, mi_row, mi_col, bsize, tx_size,
                    &tpl_stats);

    // Motion flow dependency dispenser.
    tpl_model_store(tpl_frame->tpl_stats_ptr, mi_row, mi_col, tpl_frame->stride,
                    &tpl_stats, tpl_data->tpl_stats_block_mis_log2);
    (*tpl_row_mt->sync_write_ptr)(&tpl_data->tpl_mt_sync, tplb_row,
                                  tplb_col_in_tile, tplb_cols_in_tile);
  }
}

static AOM_INLINE void mc_flow_dispenser(AV1_COMP *cpi) {
  AV1_COMMON *cm = &cpi->common;
  const CommonModeInfoParams *const mi_params = &cm->mi_params;
  ThreadData *td = &cpi->td;
  MACROBLOCK *x = &td->mb;
  MACROBLOCKD *xd = &x->e_mbd;
  const BLOCK_SIZE bsize = convert_length_to_bsize(cpi->tpl_data.tpl_bsize_1d);
  const TX_SIZE tx_size = max_txsize_lookup[bsize];
  const int mi_height = mi_size_high[bsize];
  for (int mi_row = 0; mi_row < mi_params->mi_rows; mi_row += mi_height) {
    // Motion estimation row boundary
    av1_set_mv_row_limits(mi_params, &x->mv_limits, mi_row, mi_height,
                          cpi->tpl_data.border_in_pixels);
    xd->mb_to_top_edge = -GET_MV_SUBPEL(mi_row * MI_SIZE);
    xd->mb_to_bottom_edge =
        GET_MV_SUBPEL((mi_params->mi_rows - mi_height - mi_row) * MI_SIZE);
    av1_mc_flow_dispenser_row(cpi, &td->tpl_txfm_stats, x, mi_row, bsize,
                              tx_size);
  }
}

static void mc_flow_synthesizer(AV1_COMP *cpi, int frame_idx) {
  AV1_COMMON *cm = &cpi->common;
  TplParams *const tpl_data = &cpi->tpl_data;

  const BLOCK_SIZE bsize = convert_length_to_bsize(tpl_data->tpl_bsize_1d);
  const int mi_height = mi_size_high[bsize];
  const int mi_width = mi_size_wide[bsize];
  assert(mi_height == (1 << tpl_data->tpl_stats_block_mis_log2));
  assert(mi_width == (1 << tpl_data->tpl_stats_block_mis_log2));

  for (int mi_row = 0; mi_row < cm->mi_params.mi_rows; mi_row += mi_height) {
    for (int mi_col = 0; mi_col < cm->mi_params.mi_cols; mi_col += mi_width) {
      if (frame_idx) {
        tpl_model_update(tpl_data, mi_row, mi_col, frame_idx);
      }
    }
  }
}

static AOM_INLINE void init_gop_frames_for_tpl(
    AV1_COMP *cpi, const EncodeFrameParams *const init_frame_params,
    GF_GROUP *gf_group, int gop_eval, int *tpl_group_frames,
    const EncodeFrameInput *const frame_input, int *pframe_qindex) {
  AV1_COMMON *cm = &cpi->common;
  int cur_frame_idx = cpi->gf_frame_index;
  *pframe_qindex = 0;

  RefBufferStack ref_buffer_stack = cpi->ref_buffer_stack;
  EncodeFrameParams frame_params = *init_frame_params;
  TplParams *const tpl_data = &cpi->tpl_data;

  int ref_picture_map[REF_FRAMES];

  for (int i = 0; i < REF_FRAMES; ++i) {
    if (frame_params.frame_type == KEY_FRAME) {
      tpl_data->tpl_frame[-i - 1].gf_picture = NULL;
      tpl_data->tpl_frame[-1 - 1].rec_picture = NULL;
      tpl_data->tpl_frame[-i - 1].frame_display_index = 0;
    } else {
      tpl_data->tpl_frame[-i - 1].gf_picture = &cm->ref_frame_map[i]->buf;
      tpl_data->tpl_frame[-i - 1].rec_picture = &cm->ref_frame_map[i]->buf;
      tpl_data->tpl_frame[-i - 1].frame_display_index =
          cm->ref_frame_map[i]->display_order_hint;
    }

    ref_picture_map[i] = -i - 1;
  }

  *tpl_group_frames = cur_frame_idx;

  int gf_index;
  int anc_frame_offset = gop_eval ? 0 : gf_group->cur_frame_idx[cur_frame_idx];
  int process_frame_count = 0;
  const int gop_length = get_gop_length(gf_group);

  for (gf_index = cur_frame_idx; gf_index < gop_length; ++gf_index) {
    TplDepFrame *tpl_frame = &tpl_data->tpl_frame[gf_index];
    FRAME_UPDATE_TYPE frame_update_type = gf_group->update_type[gf_index];
    int frame_display_index = gf_index == gf_group->size
                                  ? cpi->rc.baseline_gf_interval
                                  : gf_group->cur_frame_idx[gf_index] +
                                        gf_group->arf_src_offset[gf_index];

    int lookahead_index = frame_display_index - anc_frame_offset;

    frame_params.show_frame = frame_update_type != ARF_UPDATE &&
                              frame_update_type != INTNL_ARF_UPDATE;
    frame_params.show_existing_frame =
        frame_update_type == INTNL_OVERLAY_UPDATE ||
        frame_update_type == OVERLAY_UPDATE;
    frame_params.frame_type = gf_group->frame_type[gf_index];

    if (frame_update_type == LF_UPDATE)
      *pframe_qindex = gf_group->q_val[gf_index];

    struct lookahead_entry *buf;
    if (gf_index == cur_frame_idx) {
      buf = av1_lookahead_peek(cpi->ppi->lookahead, lookahead_index,
                               cpi->compressor_stage);
      tpl_frame->gf_picture = gop_eval ? &buf->img : frame_input->source;
    } else {
      buf = av1_lookahead_peek(cpi->ppi->lookahead, lookahead_index,
                               cpi->compressor_stage);
      if (buf == NULL) break;
      tpl_frame->gf_picture = &buf->img;
    }
    if (gop_eval && cpi->rc.frames_since_key > 0 &&
        gf_group->arf_index == gf_index)
      tpl_frame->gf_picture = &cpi->alt_ref_buffer;

    // 'cm->current_frame.frame_number' is the display number
    // of the current frame.
    // 'anc_frame_offset' is the number of frames displayed so
    // far within the gf group. 'cm->current_frame.frame_number -
    // anc_frame_offset' is the offset of the first frame in the gf group.
    // 'frame display index' is frame offset within the gf group.
    // 'frame_display_index + cm->current_frame.frame_number - anc_frame_offset'
    // is the display index of the frame.
    tpl_frame->frame_display_index =
        frame_display_index + cm->current_frame.frame_number - anc_frame_offset;
    assert(buf->display_idx == cpi->frame_index_set.show_frame_count -
                                   anc_frame_offset + frame_display_index);

    if (frame_update_type != OVERLAY_UPDATE &&
        frame_update_type != INTNL_OVERLAY_UPDATE) {
      tpl_frame->rec_picture = &tpl_data->tpl_rec_pool[process_frame_count];
      tpl_frame->tpl_stats_ptr = tpl_data->tpl_stats_pool[process_frame_count];
      ++process_frame_count;
    }

    av1_get_ref_frames(&ref_buffer_stack, cm->remapped_ref_idx);
    int refresh_mask = av1_get_refresh_frame_flags(
        cpi, &frame_params, frame_update_type, &ref_buffer_stack);

    int refresh_frame_map_index = av1_get_refresh_ref_frame_map(refresh_mask);
    av1_update_ref_frame_map(cpi, frame_update_type, frame_params.frame_type,
                             frame_params.show_existing_frame,
                             refresh_frame_map_index, &ref_buffer_stack);

    for (int i = LAST_FRAME; i <= ALTREF_FRAME; ++i)
      tpl_frame->ref_map_index[i - LAST_FRAME] =
          ref_picture_map[cm->remapped_ref_idx[i - LAST_FRAME]];

    if (refresh_mask) ref_picture_map[refresh_frame_map_index] = gf_index;

    ++*tpl_group_frames;
  }

  if (cpi->rc.frames_since_key == 0) return;

  int extend_frame_count = 0;
  int extend_frame_length = AOMMIN(
      MAX_TPL_EXTEND, cpi->rc.frames_to_key - cpi->rc.baseline_gf_interval);
  int frame_display_index = gf_group->cur_frame_idx[gop_length - 1] +
                            gf_group->arf_src_offset[gop_length - 1] + 1;

  for (;
       gf_index < MAX_TPL_FRAME_IDX && extend_frame_count < extend_frame_length;
       ++gf_index) {
    TplDepFrame *tpl_frame = &tpl_data->tpl_frame[gf_index];
    FRAME_UPDATE_TYPE frame_update_type = LF_UPDATE;
    frame_params.show_frame = frame_update_type != ARF_UPDATE &&
                              frame_update_type != INTNL_ARF_UPDATE;
    frame_params.show_existing_frame =
        frame_update_type == INTNL_OVERLAY_UPDATE;
    frame_params.frame_type = INTER_FRAME;

    int lookahead_index = frame_display_index - anc_frame_offset;
    struct lookahead_entry *buf = av1_lookahead_peek(
        cpi->ppi->lookahead, lookahead_index, cpi->compressor_stage);

    if (buf == NULL) break;

    tpl_frame->gf_picture = &buf->img;
    tpl_frame->rec_picture = &tpl_data->tpl_rec_pool[process_frame_count];
    tpl_frame->tpl_stats_ptr = tpl_data->tpl_stats_pool[process_frame_count];
    // 'cm->current_frame.frame_number' is the display number
    // of the current frame.
    // 'anc_frame_offset' is the number of frames displayed so
    // far within the gf group. 'cm->current_frame.frame_number -
    // anc_frame_offset' is the offset of the first frame in the gf group.
    // 'frame display index' is frame offset within the gf group.
    // 'frame_display_index + cm->current_frame.frame_number - anc_frame_offset'
    // is the display index of the frame.
    tpl_frame->frame_display_index =
        frame_display_index + cm->current_frame.frame_number - anc_frame_offset;

    ++process_frame_count;

    gf_group->update_type[gf_index] = LF_UPDATE;
    gf_group->q_val[gf_index] = *pframe_qindex;

    av1_get_ref_frames(&ref_buffer_stack, cm->remapped_ref_idx);
    int refresh_mask = av1_get_refresh_frame_flags(
        cpi, &frame_params, frame_update_type, &ref_buffer_stack);
    int refresh_frame_map_index = av1_get_refresh_ref_frame_map(refresh_mask);
    av1_update_ref_frame_map(cpi, frame_update_type, frame_params.frame_type,
                             frame_params.show_existing_frame,
                             refresh_frame_map_index, &ref_buffer_stack);

    for (int i = LAST_FRAME; i <= ALTREF_FRAME; ++i)
      tpl_frame->ref_map_index[i - LAST_FRAME] =
          ref_picture_map[cm->remapped_ref_idx[i - LAST_FRAME]];

    tpl_frame->ref_map_index[ALTREF_FRAME - LAST_FRAME] = -1;
    tpl_frame->ref_map_index[LAST3_FRAME - LAST_FRAME] = -1;
    tpl_frame->ref_map_index[BWDREF_FRAME - LAST_FRAME] = -1;
    tpl_frame->ref_map_index[ALTREF2_FRAME - LAST_FRAME] = -1;

    if (refresh_mask) ref_picture_map[refresh_frame_map_index] = gf_index;

    ++*tpl_group_frames;
    ++extend_frame_count;
    ++frame_display_index;
  }

  av1_get_ref_frames(&cpi->ref_buffer_stack, cm->remapped_ref_idx);
}

void av1_init_tpl_stats(TplParams *const tpl_data) {
  int frame_idx;
  set_tpl_stats_block_size(&tpl_data->tpl_stats_block_mis_log2,
                           &tpl_data->tpl_bsize_1d);
  for (frame_idx = 0; frame_idx < MAX_LAG_BUFFERS; ++frame_idx) {
    TplDepFrame *tpl_frame = &tpl_data->tpl_stats_buffer[frame_idx];
    if (tpl_data->tpl_stats_pool[frame_idx] == NULL) continue;
    memset(tpl_data->tpl_stats_pool[frame_idx], 0,
           tpl_frame->height * tpl_frame->width *
               sizeof(*tpl_frame->tpl_stats_ptr));
    tpl_frame->is_valid = 0;
  }
  for (frame_idx = 0; frame_idx < MAX_LENGTH_TPL_FRAME_STATS; ++frame_idx) {
    TplDepFrame *tpl_frame = &tpl_data->tpl_stats_buffer[frame_idx];
    av1_tpl_stats_init_txfm_stats(tpl_frame, tpl_data->tpl_bsize_1d);
  }
}

int av1_tpl_setup_stats(AV1_COMP *cpi, int gop_eval,
                        const EncodeFrameParams *const frame_params,
                        const EncodeFrameInput *const frame_input) {
#if CONFIG_COLLECT_COMPONENT_TIMING
  start_timing(cpi, av1_tpl_setup_stats_time);
#endif
  AV1_COMMON *cm = &cpi->common;
  MultiThreadInfo *const mt_info = &cpi->mt_info;
  AV1TplRowMultiThreadInfo *const tpl_row_mt = &mt_info->tpl_row_mt;
  GF_GROUP *gf_group = &cpi->gf_group;
  int bottom_index, top_index;
  EncodeFrameParams this_frame_params = *frame_params;
  TplParams *const tpl_data = &cpi->tpl_data;

  if (cpi->superres_mode != AOM_SUPERRES_NONE) {
    assert(cpi->superres_mode != AOM_SUPERRES_AUTO);
    av1_init_tpl_stats(tpl_data);
    return 0;
  }

  cm->current_frame.frame_type = frame_params->frame_type;
  for (int gf_index = cpi->gf_frame_index; gf_index < gf_group->size;
       ++gf_index) {
    cm->current_frame.frame_type = gf_group->frame_type[gf_index];
    av1_configure_buffer_updates(cpi, &this_frame_params.refresh_frame,
                                 gf_group->update_type[gf_index],
                                 cm->current_frame.frame_type, 0);

    memcpy(&cpi->refresh_frame, &this_frame_params.refresh_frame,
           sizeof(cpi->refresh_frame));

    cm->show_frame = gf_group->update_type[gf_index] != ARF_UPDATE &&
                     gf_group->update_type[gf_index] != INTNL_ARF_UPDATE;

    gf_group->q_val[gf_index] =
        av1_rc_pick_q_and_bounds(cpi, &cpi->rc, cm->width, cm->height, gf_index,
                                 &bottom_index, &top_index);
  }

  int pframe_qindex;
  int tpl_gf_group_frames;
  init_gop_frames_for_tpl(cpi, frame_params, gf_group, gop_eval,
                          &tpl_gf_group_frames, frame_input, &pframe_qindex);

  cpi->rc.base_layer_qp = pframe_qindex;

  av1_init_tpl_stats(tpl_data);

  tpl_row_mt->sync_read_ptr = av1_tpl_row_mt_sync_read_dummy;
  tpl_row_mt->sync_write_ptr = av1_tpl_row_mt_sync_write_dummy;

  av1_setup_scale_factors_for_frame(&cm->sf_identity, cm->width, cm->height,
                                    cm->width, cm->height);

  if (frame_params->frame_type == KEY_FRAME) {
    av1_init_mv_probs(cm);
  }
  av1_fill_mv_costs(&cm->fc->nmvc, cm->features.cur_frame_force_integer_mv,
                    cm->features.allow_high_precision_mv, cpi->td.mb.mv_costs);

  // Backward propagation from tpl_group_frames to 1.
  for (int frame_idx = cpi->gf_frame_index; frame_idx < tpl_gf_group_frames;
       ++frame_idx) {
    if (gf_group->update_type[frame_idx] == INTNL_OVERLAY_UPDATE ||
        gf_group->update_type[frame_idx] == OVERLAY_UPDATE)
      continue;

    init_mc_flow_dispenser(cpi, frame_idx, pframe_qindex);
    if (mt_info->num_workers > 1) {
      tpl_row_mt->sync_read_ptr = av1_tpl_row_mt_sync_read;
      tpl_row_mt->sync_write_ptr = av1_tpl_row_mt_sync_write;
      av1_mc_flow_dispenser_mt(cpi);
    } else {
      mc_flow_dispenser(cpi);
    }
    tpl_stats_update_abs_coeff_mean(tpl_data, &cpi->td.tpl_txfm_stats);

    aom_extend_frame_borders(tpl_data->tpl_frame[frame_idx].rec_picture,
                             av1_num_planes(cm));
  }

  for (int frame_idx = tpl_gf_group_frames - 1;
       frame_idx >= cpi->gf_frame_index; --frame_idx) {
    if (gf_group->update_type[frame_idx] == INTNL_OVERLAY_UPDATE ||
        gf_group->update_type[frame_idx] == OVERLAY_UPDATE)
      continue;

    mc_flow_synthesizer(cpi, frame_idx);
  }

  av1_configure_buffer_updates(cpi, &this_frame_params.refresh_frame,
                               gf_group->update_type[cpi->gf_frame_index],
                               frame_params->frame_type, 0);
  cm->current_frame.frame_type = frame_params->frame_type;
  cm->show_frame = frame_params->show_frame;

#if CONFIG_COLLECT_COMPONENT_TIMING
  // Record the time if the function returns.
  if (cpi->common.tiles.large_scale || gf_group->max_layer_depth_allowed == 0 ||
      !gop_eval)
    end_timing(cpi, av1_tpl_setup_stats_time);
#endif

  if (cpi->common.tiles.large_scale) return 0;
  if (gf_group->max_layer_depth_allowed == 0) return 1;
  if (!gop_eval) return 0;
  assert(gf_group->arf_index >= 0);

  double beta[2] = { 0.0 };
  for (int frame_idx = gf_group->arf_index;
       frame_idx <= AOMMIN(tpl_gf_group_frames - 1, gf_group->arf_index + 1);
       ++frame_idx) {
    TplDepFrame *tpl_frame = &tpl_data->tpl_frame[frame_idx];
    TplDepStats *tpl_stats = tpl_frame->tpl_stats_ptr;
    int tpl_stride = tpl_frame->stride;
    int64_t intra_cost_base = 0;
    int64_t mc_dep_cost_base = 0;
    const int step = 1 << tpl_data->tpl_stats_block_mis_log2;
    const int row_step = step;
    const int col_step_sr =
        coded_to_superres_mi(step, cm->superres_scale_denominator);
    const int mi_cols_sr = av1_pixels_to_mi(cm->superres_upscaled_width);

    for (int row = 0; row < cm->mi_params.mi_rows; row += row_step) {
      for (int col = 0; col < mi_cols_sr; col += col_step_sr) {
        TplDepStats *this_stats = &tpl_stats[av1_tpl_ptr_pos(
            row, col, tpl_stride, tpl_data->tpl_stats_block_mis_log2)];
        int64_t mc_dep_delta =
            RDCOST(tpl_frame->base_rdmult, this_stats->mc_dep_rate,
                   this_stats->mc_dep_dist);
        intra_cost_base += (this_stats->recrf_dist << RDDIV_BITS);
        mc_dep_cost_base +=
            (this_stats->recrf_dist << RDDIV_BITS) + mc_dep_delta;
      }
    }
    if (intra_cost_base == 0) {
      // This should happen very rarely and if it happens, assign a dummy value
      // to it since it probably wouldn't influence things much
      beta[frame_idx - gf_group->arf_index] = 0;
    } else {
      beta[frame_idx - gf_group->arf_index] =
          (double)mc_dep_cost_base / intra_cost_base;
    }
  }

#if CONFIG_COLLECT_COMPONENT_TIMING
  end_timing(cpi, av1_tpl_setup_stats_time);
#endif

  // Allow larger GOP size if the base layer ARF has higher dependency factor
  // than the intermediate ARF and both ARFs have reasonably high dependency
  // factors.
  return (beta[0] >= beta[1] + 0.7) && beta[0] > 8.0;
}

void av1_tpl_rdmult_setup(AV1_COMP *cpi) {
  const AV1_COMMON *const cm = &cpi->common;
  const int tpl_idx = cpi->gf_frame_index;

  assert(IMPLIES(cpi->gf_group.size > 0, tpl_idx < cpi->gf_group.size));

  TplParams *const tpl_data = &cpi->tpl_data;
  const TplDepFrame *const tpl_frame = &tpl_data->tpl_frame[tpl_idx];

  if (!tpl_frame->is_valid) return;

  const TplDepStats *const tpl_stats = tpl_frame->tpl_stats_ptr;
  const int tpl_stride = tpl_frame->stride;
  const int mi_cols_sr = av1_pixels_to_mi(cm->superres_upscaled_width);

  const int block_size = BLOCK_16X16;
  const int num_mi_w = mi_size_wide[block_size];
  const int num_mi_h = mi_size_high[block_size];
  const int num_cols = (mi_cols_sr + num_mi_w - 1) / num_mi_w;
  const int num_rows = (cm->mi_params.mi_rows + num_mi_h - 1) / num_mi_h;
  const double c = 1.2;
  const int step = 1 << tpl_data->tpl_stats_block_mis_log2;

  aom_clear_system_state();

  // Loop through each 'block_size' X 'block_size' block.
  for (int row = 0; row < num_rows; row++) {
    for (int col = 0; col < num_cols; col++) {
      double intra_cost = 0.0, mc_dep_cost = 0.0;
      // Loop through each mi block.
      for (int mi_row = row * num_mi_h; mi_row < (row + 1) * num_mi_h;
           mi_row += step) {
        for (int mi_col = col * num_mi_w; mi_col < (col + 1) * num_mi_w;
             mi_col += step) {
          if (mi_row >= cm->mi_params.mi_rows || mi_col >= mi_cols_sr) continue;
          const TplDepStats *this_stats = &tpl_stats[av1_tpl_ptr_pos(
              mi_row, mi_col, tpl_stride, tpl_data->tpl_stats_block_mis_log2)];
          int64_t mc_dep_delta =
              RDCOST(tpl_frame->base_rdmult, this_stats->mc_dep_rate,
                     this_stats->mc_dep_dist);
          intra_cost += (double)(this_stats->recrf_dist << RDDIV_BITS);
          mc_dep_cost +=
              (double)(this_stats->recrf_dist << RDDIV_BITS) + mc_dep_delta;
        }
      }
      const double rk = intra_cost / mc_dep_cost;
      const int index = row * num_cols + col;
      cpi->tpl_rdmult_scaling_factors[index] = rk / cpi->rd.r0 + c;
    }
  }
  aom_clear_system_state();
}

void av1_tpl_rdmult_setup_sb(AV1_COMP *cpi, MACROBLOCK *const x,
                             BLOCK_SIZE sb_size, int mi_row, int mi_col) {
  AV1_COMMON *const cm = &cpi->common;
  GF_GROUP *gf_group = &cpi->gf_group;
  assert(IMPLIES(cpi->gf_group.size > 0,
                 cpi->gf_frame_index < cpi->gf_group.size));
  const int tpl_idx = cpi->gf_frame_index;
  TplDepFrame *tpl_frame = &cpi->tpl_data.tpl_frame[tpl_idx];

  if (tpl_frame->is_valid == 0) return;
  if (!is_frame_tpl_eligible(gf_group, cpi->gf_frame_index)) return;
  if (tpl_idx >= MAX_TPL_FRAME_IDX) return;
  if (cpi->oxcf.q_cfg.aq_mode != NO_AQ) return;

  const int mi_col_sr =
      coded_to_superres_mi(mi_col, cm->superres_scale_denominator);
  const int mi_cols_sr = av1_pixels_to_mi(cm->superres_upscaled_width);
  const int sb_mi_width_sr = coded_to_superres_mi(
      mi_size_wide[sb_size], cm->superres_scale_denominator);

  const int bsize_base = BLOCK_16X16;
  const int num_mi_w = mi_size_wide[bsize_base];
  const int num_mi_h = mi_size_high[bsize_base];
  const int num_cols = (mi_cols_sr + num_mi_w - 1) / num_mi_w;
  const int num_rows = (cm->mi_params.mi_rows + num_mi_h - 1) / num_mi_h;
  const int num_bcols = (sb_mi_width_sr + num_mi_w - 1) / num_mi_w;
  const int num_brows = (mi_size_high[sb_size] + num_mi_h - 1) / num_mi_h;
  int row, col;

  double base_block_count = 0.0;
  double log_sum = 0.0;

  aom_clear_system_state();
  for (row = mi_row / num_mi_w;
       row < num_rows && row < mi_row / num_mi_w + num_brows; ++row) {
    for (col = mi_col_sr / num_mi_h;
         col < num_cols && col < mi_col_sr / num_mi_h + num_bcols; ++col) {
      const int index = row * num_cols + col;
      log_sum += log(cpi->tpl_rdmult_scaling_factors[index]);
      base_block_count += 1.0;
    }
  }

  const CommonQuantParams *quant_params = &cm->quant_params;
  const int orig_rdmult = av1_compute_rd_mult(
      cpi, quant_params->base_qindex + quant_params->y_dc_delta_q);
  const int new_rdmult =
      av1_compute_rd_mult(cpi, quant_params->base_qindex + x->delta_qindex +
                                   quant_params->y_dc_delta_q);
  const double scaling_factor = (double)new_rdmult / (double)orig_rdmult;

  double scale_adj = log(scaling_factor) - log_sum / base_block_count;
  scale_adj = exp(scale_adj);

  for (row = mi_row / num_mi_w;
       row < num_rows && row < mi_row / num_mi_w + num_brows; ++row) {
    for (col = mi_col_sr / num_mi_h;
         col < num_cols && col < mi_col_sr / num_mi_h + num_bcols; ++col) {
      const int index = row * num_cols + col;
      cpi->tpl_sb_rdmult_scaling_factors[index] =
          scale_adj * cpi->tpl_rdmult_scaling_factors[index];
    }
  }
  aom_clear_system_state();
}

#define EPSILON (0.0000001)

double av1_exponential_entropy(double q_step, double b) {
  aom_clear_system_state();
  double z = fmax(exp(-q_step / b), EPSILON);
  return -log2(1 - z) - z * log2(z) / (1 - z);
}

double av1_laplace_entropy(double q_step, double b, double zero_bin_ratio) {
  aom_clear_system_state();
  // zero bin's size is zero_bin_ratio * q_step
  // non-zero bin's size is q_step
  double z = fmax(exp(-zero_bin_ratio / 2 * q_step / b), EPSILON);
  double h = av1_exponential_entropy(q_step, b);
  double r = -(1 - z) * log2(1 - z) - z * log2(z) + z * (h + 1);
  return r;
}

double av1_laplace_estimate_frame_rate(int q_index, int block_count,
                                       const double *abs_coeff_mean,
                                       int coeff_num) {
  aom_clear_system_state();
  double zero_bin_ratio = 2;
  double dc_q_step = av1_dc_quant_QTX(q_index, 0, AOM_BITS_8) / 4.;
  double ac_q_step = av1_ac_quant_QTX(q_index, 0, AOM_BITS_8) / 4.;
  double est_rate = 0;
  // dc coeff
  est_rate += av1_laplace_entropy(dc_q_step, abs_coeff_mean[0], zero_bin_ratio);
  // ac coeff
  for (int i = 1; i < coeff_num; ++i) {
    est_rate +=
        av1_laplace_entropy(ac_q_step, abs_coeff_mean[i], zero_bin_ratio);
  }
  est_rate *= block_count;
  return est_rate;
}
