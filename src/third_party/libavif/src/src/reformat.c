// Copyright 2019 Joe Drago. All rights reserved.
// SPDX-License-Identifier: BSD-2-Clause

#include "avif/internal.h"

#include <assert.h>
#include <math.h>
#include <string.h>

struct YUVBlock
{
    float y;
    float u;
    float v;
};

static avifBool avifPrepareReformatState(const avifImage * image, const avifRGBImage * rgb, avifReformatState * state)
{
    if ((image->depth != 8) && (image->depth != 10) && (image->depth != 12)) {
        return AVIF_FALSE;
    }
    if ((rgb->depth != 8) && (rgb->depth != 10) && (rgb->depth != 12) && (rgb->depth != 16)) {
        return AVIF_FALSE;
    }

    // These matrix coefficients values are currently unsupported. Revise this list as more support is added.
    //
    // YCgCo performs limited-full range adjustment on R,G,B but the current implementation performs range adjustment
    // on Y,U,V. So YCgCo with limited range is unsupported.
    if ((image->matrixCoefficients == 3 /* CICP reserved */) ||
        ((image->matrixCoefficients == AVIF_MATRIX_COEFFICIENTS_YCGCO) && (image->yuvRange == AVIF_RANGE_LIMITED)) ||
        (image->matrixCoefficients == AVIF_MATRIX_COEFFICIENTS_BT2020_CL) ||
        (image->matrixCoefficients == AVIF_MATRIX_COEFFICIENTS_SMPTE2085) ||
        (image->matrixCoefficients == AVIF_MATRIX_COEFFICIENTS_CHROMA_DERIVED_CL) ||
        (image->matrixCoefficients >= AVIF_MATRIX_COEFFICIENTS_ICTCP)) { // Note the >= catching "future" CICP values here too
        return AVIF_FALSE;
    }

    if ((image->matrixCoefficients == AVIF_MATRIX_COEFFICIENTS_IDENTITY) && (image->yuvFormat != AVIF_PIXEL_FORMAT_YUV444)) {
        return AVIF_FALSE;
    }

    if (image->yuvFormat == AVIF_PIXEL_FORMAT_NONE) {
        return AVIF_FALSE;
    }

    avifGetPixelFormatInfo(image->yuvFormat, &state->formatInfo);
    avifCalcYUVCoefficients(image, &state->kr, &state->kg, &state->kb);
    state->mode = AVIF_REFORMAT_MODE_YUV_COEFFICIENTS;

    if (image->matrixCoefficients == AVIF_MATRIX_COEFFICIENTS_IDENTITY) {
        state->mode = AVIF_REFORMAT_MODE_IDENTITY;
    } else if (image->matrixCoefficients == AVIF_MATRIX_COEFFICIENTS_YCGCO) {
        state->mode = AVIF_REFORMAT_MODE_YCGCO;
    }

    if (state->mode != AVIF_REFORMAT_MODE_YUV_COEFFICIENTS) {
        state->kr = 0.0f;
        state->kg = 0.0f;
        state->kb = 0.0f;
    }

    state->yuvChannelBytes = (image->depth > 8) ? 2 : 1;
    state->rgbChannelBytes = (rgb->depth > 8) ? 2 : 1;
    state->rgbChannelCount = avifRGBFormatChannelCount(rgb->format);
    state->rgbPixelBytes = state->rgbChannelBytes * state->rgbChannelCount;

    switch (rgb->format) {
        case AVIF_RGB_FORMAT_RGB:
            state->rgbOffsetBytesR = state->rgbChannelBytes * 0;
            state->rgbOffsetBytesG = state->rgbChannelBytes * 1;
            state->rgbOffsetBytesB = state->rgbChannelBytes * 2;
            state->rgbOffsetBytesA = 0;
            break;
        case AVIF_RGB_FORMAT_RGBA:
            state->rgbOffsetBytesR = state->rgbChannelBytes * 0;
            state->rgbOffsetBytesG = state->rgbChannelBytes * 1;
            state->rgbOffsetBytesB = state->rgbChannelBytes * 2;
            state->rgbOffsetBytesA = state->rgbChannelBytes * 3;
            break;
        case AVIF_RGB_FORMAT_ARGB:
            state->rgbOffsetBytesA = state->rgbChannelBytes * 0;
            state->rgbOffsetBytesR = state->rgbChannelBytes * 1;
            state->rgbOffsetBytesG = state->rgbChannelBytes * 2;
            state->rgbOffsetBytesB = state->rgbChannelBytes * 3;
            break;
        case AVIF_RGB_FORMAT_BGR:
            state->rgbOffsetBytesB = state->rgbChannelBytes * 0;
            state->rgbOffsetBytesG = state->rgbChannelBytes * 1;
            state->rgbOffsetBytesR = state->rgbChannelBytes * 2;
            state->rgbOffsetBytesA = 0;
            break;
        case AVIF_RGB_FORMAT_BGRA:
            state->rgbOffsetBytesB = state->rgbChannelBytes * 0;
            state->rgbOffsetBytesG = state->rgbChannelBytes * 1;
            state->rgbOffsetBytesR = state->rgbChannelBytes * 2;
            state->rgbOffsetBytesA = state->rgbChannelBytes * 3;
            break;
        case AVIF_RGB_FORMAT_ABGR:
            state->rgbOffsetBytesA = state->rgbChannelBytes * 0;
            state->rgbOffsetBytesB = state->rgbChannelBytes * 1;
            state->rgbOffsetBytesG = state->rgbChannelBytes * 2;
            state->rgbOffsetBytesR = state->rgbChannelBytes * 3;
            break;

        default:
            return AVIF_FALSE;
    }

    state->yuvDepth = image->depth;
    state->yuvRange = image->yuvRange;
    state->yuvMaxChannel = (1 << image->depth) - 1;
    state->rgbMaxChannel = (1 << rgb->depth) - 1;
    state->rgbMaxChannelF = (float)state->rgbMaxChannel;
    state->biasY = (state->yuvRange == AVIF_RANGE_LIMITED) ? (float)(16 << (state->yuvDepth - 8)) : 0.0f;
    state->biasUV = (float)(1 << (state->yuvDepth - 1));
    state->rangeY = (float)((state->yuvRange == AVIF_RANGE_LIMITED) ? (219 << (state->yuvDepth - 8)) : state->yuvMaxChannel);
    state->rangeUV = (float)((state->yuvRange == AVIF_RANGE_LIMITED) ? (224 << (state->yuvDepth - 8)) : state->yuvMaxChannel);

    uint32_t cpCount = 1 << image->depth;
    if (state->mode == AVIF_REFORMAT_MODE_IDENTITY) {
        for (uint32_t cp = 0; cp < cpCount; ++cp) {
            state->unormFloatTableY[cp] = ((float)cp - state->biasY) / state->rangeY;
            state->unormFloatTableUV[cp] = ((float)cp - state->biasY) / state->rangeY;
        }
    } else {
        for (uint32_t cp = 0; cp < cpCount; ++cp) {
            // Review this when implementing YCgCo limited range support.
            state->unormFloatTableY[cp] = ((float)cp - state->biasY) / state->rangeY;
            state->unormFloatTableUV[cp] = ((float)cp - state->biasUV) / state->rangeUV;
        }
    }

    return AVIF_TRUE;
}

// Formulas 20-31 from https://www.itu.int/rec/T-REC-H.273-201612-I/en
static int avifReformatStateYToUNorm(avifReformatState * state, float v)
{
    int unorm = (int)avifRoundf(v * state->rangeY + state->biasY);
    return AVIF_CLAMP(unorm, 0, state->yuvMaxChannel);
}

static int avifReformatStateUVToUNorm(avifReformatState * state, float v)
{
    int unorm;

    // YCgCo performs limited-full range adjustment on R,G,B but the current implementation performs range adjustment
    // on Y,U,V. So YCgCo with limited range is unsupported.
    assert((state->mode != AVIF_REFORMAT_MODE_YCGCO) || (state->yuvRange == AVIF_RANGE_FULL));

    if (state->mode == AVIF_REFORMAT_MODE_IDENTITY) {
        unorm = (int)avifRoundf(v * state->rangeY + state->biasY);
    } else {
        unorm = (int)avifRoundf(v * state->rangeUV + state->biasUV);
    }

    return AVIF_CLAMP(unorm, 0, state->yuvMaxChannel);
}

avifResult avifImageRGBToYUV(avifImage * image, const avifRGBImage * rgb)
{
    if (!rgb->pixels) {
        return AVIF_RESULT_REFORMAT_FAILED;
    }

    avifReformatState state;
    if (!avifPrepareReformatState(image, rgb, &state)) {
        return AVIF_RESULT_REFORMAT_FAILED;
    }

    avifImageAllocatePlanes(image, AVIF_PLANES_YUV);
    if (avifRGBFormatHasAlpha(rgb->format) && !rgb->ignoreAlpha) {
        avifImageAllocatePlanes(image, AVIF_PLANES_A);
    }

    const float kr = state.kr;
    const float kg = state.kg;
    const float kb = state.kb;

    struct YUVBlock yuvBlock[2][2];
    float rgbPixel[3];
    const float rgbMaxChannelF = state.rgbMaxChannelF;
    uint8_t ** yuvPlanes = image->yuvPlanes;
    uint32_t * yuvRowBytes = image->yuvRowBytes;
    for (uint32_t outerJ = 0; outerJ < image->height; outerJ += 2) {
        for (uint32_t outerI = 0; outerI < image->width; outerI += 2) {
            int blockW = 2, blockH = 2;
            if ((outerI + 1) >= image->width) {
                blockW = 1;
            }
            if ((outerJ + 1) >= image->height) {
                blockH = 1;
            }

            // Convert an entire 2x2 block to YUV, and populate any fully sampled channels as we go
            for (int bJ = 0; bJ < blockH; ++bJ) {
                for (int bI = 0; bI < blockW; ++bI) {
                    int i = outerI + bI;
                    int j = outerJ + bJ;

                    // Unpack RGB into normalized float
                    if (state.rgbChannelBytes > 1) {
                        rgbPixel[0] =
                            *((uint16_t *)(&rgb->pixels[state.rgbOffsetBytesR + (i * state.rgbPixelBytes) + (j * rgb->rowBytes)])) /
                            rgbMaxChannelF;
                        rgbPixel[1] =
                            *((uint16_t *)(&rgb->pixels[state.rgbOffsetBytesG + (i * state.rgbPixelBytes) + (j * rgb->rowBytes)])) /
                            rgbMaxChannelF;
                        rgbPixel[2] =
                            *((uint16_t *)(&rgb->pixels[state.rgbOffsetBytesB + (i * state.rgbPixelBytes) + (j * rgb->rowBytes)])) /
                            rgbMaxChannelF;
                    } else {
                        rgbPixel[0] = rgb->pixels[state.rgbOffsetBytesR + (i * state.rgbPixelBytes) + (j * rgb->rowBytes)] / rgbMaxChannelF;
                        rgbPixel[1] = rgb->pixels[state.rgbOffsetBytesG + (i * state.rgbPixelBytes) + (j * rgb->rowBytes)] / rgbMaxChannelF;
                        rgbPixel[2] = rgb->pixels[state.rgbOffsetBytesB + (i * state.rgbPixelBytes) + (j * rgb->rowBytes)] / rgbMaxChannelF;
                    }

                    if (avifRGBFormatHasAlpha(rgb->format) && !rgb->ignoreAlpha &&
                        (rgb->alphaPremultiplied != image->alphaPremultiplied)) {
                        float a;
                        if (state.rgbChannelBytes > 1) {
                            a = *((uint16_t *)(&rgb->pixels[state.rgbOffsetBytesA + (i * state.rgbPixelBytes) + (j * rgb->rowBytes)])) /
                                rgbMaxChannelF;
                        } else {
                            a = rgb->pixels[state.rgbOffsetBytesA + (i * state.rgbPixelBytes) + (j * rgb->rowBytes)] / rgbMaxChannelF;
                        }

                        if (image->alphaPremultiplied) {
                            // multiply
                            if (a == 0) {
                                rgbPixel[0] = 0;
                                rgbPixel[1] = 0;
                                rgbPixel[2] = 0;
                            } else if (a < 1.0f) {
                                rgbPixel[0] *= a;
                                rgbPixel[1] *= a;
                                rgbPixel[2] *= a;
                            }
                        } else {
                            // unmultiply
                            if (a == 0) {
                                rgbPixel[0] = 0;
                                rgbPixel[1] = 0;
                                rgbPixel[2] = 0;
                            } else if (a < 1.0f) {
                                rgbPixel[0] /= a;
                                rgbPixel[1] /= a;
                                rgbPixel[2] /= a;
                                rgbPixel[0] = AVIF_MIN(rgbPixel[0], 1.0f);
                                rgbPixel[1] = AVIF_MIN(rgbPixel[1], 1.0f);
                                rgbPixel[2] = AVIF_MIN(rgbPixel[2], 1.0f);
                            }
                        }
                    }

                    // RGB -> YUV conversion
                    if (state.mode == AVIF_REFORMAT_MODE_IDENTITY) {
                        // Formulas 41,42,43 from https://www.itu.int/rec/T-REC-H.273-201612-I/en
                        yuvBlock[bI][bJ].y = rgbPixel[1]; // G
                        yuvBlock[bI][bJ].u = rgbPixel[2]; // B
                        yuvBlock[bI][bJ].v = rgbPixel[0]; // R
                    } else if (state.mode == AVIF_REFORMAT_MODE_YCGCO) {
                        // Formulas 44,45,46 from https://www.itu.int/rec/T-REC-H.273-201612-I/en
                        yuvBlock[bI][bJ].y = 0.5f * rgbPixel[1] + 0.25f * (rgbPixel[0] + rgbPixel[2]);
                        yuvBlock[bI][bJ].u = 0.5f * rgbPixel[1] - 0.25f * (rgbPixel[0] + rgbPixel[2]);
                        yuvBlock[bI][bJ].v = 0.5f * (rgbPixel[0] - rgbPixel[2]);
                    } else {
                        float Y = (kr * rgbPixel[0]) + (kg * rgbPixel[1]) + (kb * rgbPixel[2]);
                        yuvBlock[bI][bJ].y = Y;
                        yuvBlock[bI][bJ].u = (rgbPixel[2] - Y) / (2 * (1 - kb));
                        yuvBlock[bI][bJ].v = (rgbPixel[0] - Y) / (2 * (1 - kr));
                    }

                    if (state.yuvChannelBytes > 1) {
                        uint16_t * pY = (uint16_t *)&yuvPlanes[AVIF_CHAN_Y][(i * 2) + (j * yuvRowBytes[AVIF_CHAN_Y])];
                        *pY = (uint16_t)avifReformatStateYToUNorm(&state, yuvBlock[bI][bJ].y);
                        if (image->yuvFormat == AVIF_PIXEL_FORMAT_YUV444) {
                            // YUV444, full chroma
                            uint16_t * pU = (uint16_t *)&yuvPlanes[AVIF_CHAN_U][(i * 2) + (j * yuvRowBytes[AVIF_CHAN_U])];
                            *pU = (uint16_t)avifReformatStateUVToUNorm(&state, yuvBlock[bI][bJ].u);
                            uint16_t * pV = (uint16_t *)&yuvPlanes[AVIF_CHAN_V][(i * 2) + (j * yuvRowBytes[AVIF_CHAN_V])];
                            *pV = (uint16_t)avifReformatStateUVToUNorm(&state, yuvBlock[bI][bJ].v);
                        }
                    } else {
                        yuvPlanes[AVIF_CHAN_Y][i + (j * yuvRowBytes[AVIF_CHAN_Y])] =
                            (uint8_t)avifReformatStateYToUNorm(&state, yuvBlock[bI][bJ].y);
                        if (image->yuvFormat == AVIF_PIXEL_FORMAT_YUV444) {
                            // YUV444, full chroma
                            yuvPlanes[AVIF_CHAN_U][i + (j * yuvRowBytes[AVIF_CHAN_U])] =
                                (uint8_t)avifReformatStateUVToUNorm(&state, yuvBlock[bI][bJ].u);
                            yuvPlanes[AVIF_CHAN_V][i + (j * yuvRowBytes[AVIF_CHAN_V])] =
                                (uint8_t)avifReformatStateUVToUNorm(&state, yuvBlock[bI][bJ].v);
                        }
                    }
                }
            }

            // Populate any subsampled channels with averages from the 2x2 block
            if (image->yuvFormat == AVIF_PIXEL_FORMAT_YUV420) {
                // YUV420, average 4 samples (2x2)

                float sumU = 0.0f;
                float sumV = 0.0f;
                for (int bJ = 0; bJ < blockH; ++bJ) {
                    for (int bI = 0; bI < blockW; ++bI) {
                        sumU += yuvBlock[bI][bJ].u;
                        sumV += yuvBlock[bI][bJ].v;
                    }
                }
                float totalSamples = (float)(blockW * blockH);
                float avgU = sumU / totalSamples;
                float avgV = sumV / totalSamples;

                const int chromaShiftX = 1;
                const int chromaShiftY = 1;
                int uvI = outerI >> chromaShiftX;
                int uvJ = outerJ >> chromaShiftY;
                if (state.yuvChannelBytes > 1) {
                    uint16_t * pU = (uint16_t *)&yuvPlanes[AVIF_CHAN_U][(uvI * 2) + (uvJ * yuvRowBytes[AVIF_CHAN_U])];
                    *pU = (uint16_t)avifReformatStateUVToUNorm(&state, avgU);
                    uint16_t * pV = (uint16_t *)&yuvPlanes[AVIF_CHAN_V][(uvI * 2) + (uvJ * yuvRowBytes[AVIF_CHAN_V])];
                    *pV = (uint16_t)avifReformatStateUVToUNorm(&state, avgV);
                } else {
                    yuvPlanes[AVIF_CHAN_U][uvI + (uvJ * yuvRowBytes[AVIF_CHAN_U])] = (uint8_t)avifReformatStateUVToUNorm(&state, avgU);
                    yuvPlanes[AVIF_CHAN_V][uvI + (uvJ * yuvRowBytes[AVIF_CHAN_V])] = (uint8_t)avifReformatStateUVToUNorm(&state, avgV);
                }
            } else if (image->yuvFormat == AVIF_PIXEL_FORMAT_YUV422) {
                // YUV422, average 2 samples (1x2), twice

                for (int bJ = 0; bJ < blockH; ++bJ) {
                    float sumU = 0.0f;
                    float sumV = 0.0f;
                    for (int bI = 0; bI < blockW; ++bI) {
                        sumU += yuvBlock[bI][bJ].u;
                        sumV += yuvBlock[bI][bJ].v;
                    }
                    float totalSamples = (float)blockW;
                    float avgU = sumU / totalSamples;
                    float avgV = sumV / totalSamples;

                    const int chromaShiftX = 1;
                    int uvI = outerI >> chromaShiftX;
                    int uvJ = outerJ + bJ;
                    if (state.yuvChannelBytes > 1) {
                        uint16_t * pU = (uint16_t *)&yuvPlanes[AVIF_CHAN_U][(uvI * 2) + (uvJ * yuvRowBytes[AVIF_CHAN_U])];
                        *pU = (uint16_t)avifReformatStateUVToUNorm(&state, avgU);
                        uint16_t * pV = (uint16_t *)&yuvPlanes[AVIF_CHAN_V][(uvI * 2) + (uvJ * yuvRowBytes[AVIF_CHAN_V])];
                        *pV = (uint16_t)avifReformatStateUVToUNorm(&state, avgV);
                    } else {
                        yuvPlanes[AVIF_CHAN_U][uvI + (uvJ * yuvRowBytes[AVIF_CHAN_U])] =
                            (uint8_t)avifReformatStateUVToUNorm(&state, avgU);
                        yuvPlanes[AVIF_CHAN_V][uvI + (uvJ * yuvRowBytes[AVIF_CHAN_V])] =
                            (uint8_t)avifReformatStateUVToUNorm(&state, avgV);
                    }
                }
            }
        }
    }

    if (image->alphaPlane && image->alphaRowBytes) {
        avifAlphaParams params;

        params.width = image->width;
        params.height = image->height;
        params.dstDepth = image->depth;
        params.dstRange = image->alphaRange;
        params.dstPlane = image->alphaPlane;
        params.dstRowBytes = image->alphaRowBytes;
        params.dstOffsetBytes = 0;
        params.dstPixelBytes = state.yuvChannelBytes;

        if (avifRGBFormatHasAlpha(rgb->format) && !rgb->ignoreAlpha) {
            params.srcDepth = rgb->depth;
            params.srcRange = AVIF_RANGE_FULL;
            params.srcPlane = rgb->pixels;
            params.srcRowBytes = rgb->rowBytes;
            params.srcOffsetBytes = state.rgbOffsetBytesA;
            params.srcPixelBytes = state.rgbPixelBytes;

            avifReformatAlpha(&params);
        } else {
            avifFillAlpha(&params);
        }
    }
    return AVIF_RESULT_OK;
}

static avifResult avifImageYUVAnyToRGBAnySlow(const avifImage * image,
                                              avifRGBImage * rgb,
                                              avifReformatState * state,
                                              const avifChromaUpsampling chromaUpsampling)
{
    // Aliases for some state
    const float kr = state->kr;
    const float kg = state->kg;
    const float kb = state->kb;
    const float * const unormFloatTableY = state->unormFloatTableY;
    const float * const unormFloatTableUV = state->unormFloatTableUV;
    const uint32_t yuvChannelBytes = state->yuvChannelBytes;
    const uint32_t rgbPixelBytes = state->rgbPixelBytes;

    // Aliases for plane data
    const uint8_t * yPlane = image->yuvPlanes[AVIF_CHAN_Y];
    const uint8_t * uPlane = image->yuvPlanes[AVIF_CHAN_U];
    const uint8_t * vPlane = image->yuvPlanes[AVIF_CHAN_V];
    const uint32_t yRowBytes = image->yuvRowBytes[AVIF_CHAN_Y];
    const uint32_t uRowBytes = image->yuvRowBytes[AVIF_CHAN_U];
    const uint32_t vRowBytes = image->yuvRowBytes[AVIF_CHAN_V];

    // Various observations and limits
    const avifBool hasColor = (uPlane && vPlane && (image->yuvFormat != AVIF_PIXEL_FORMAT_YUV400));
    const uint16_t yuvMaxChannel = (uint16_t)state->yuvMaxChannel;
    const float rgbMaxChannelF = state->rgbMaxChannelF;

    // These are the only supported built-ins
    assert((chromaUpsampling == AVIF_CHROMA_UPSAMPLING_BILINEAR) || (chromaUpsampling == AVIF_CHROMA_UPSAMPLING_NEAREST));

    for (uint32_t j = 0; j < image->height; ++j) {
        const uint32_t uvJ = j >> state->formatInfo.chromaShiftY;
        const uint8_t * ptrY8 = &yPlane[j * yRowBytes];
        const uint8_t * ptrU8 = uPlane ? &uPlane[(uvJ * uRowBytes)] : NULL;
        const uint8_t * ptrV8 = vPlane ? &vPlane[(uvJ * vRowBytes)] : NULL;
        const uint16_t * ptrY16 = (const uint16_t *)ptrY8;
        const uint16_t * ptrU16 = (const uint16_t *)ptrU8;
        const uint16_t * ptrV16 = (const uint16_t *)ptrV8;

        uint8_t * ptrR = &rgb->pixels[state->rgbOffsetBytesR + (j * rgb->rowBytes)];
        uint8_t * ptrG = &rgb->pixels[state->rgbOffsetBytesG + (j * rgb->rowBytes)];
        uint8_t * ptrB = &rgb->pixels[state->rgbOffsetBytesB + (j * rgb->rowBytes)];

        for (uint32_t i = 0; i < image->width; ++i) {
            uint32_t uvI = i >> state->formatInfo.chromaShiftX;
            float Y, Cb = 0.5f, Cr = 0.5f;

            // Calculate Y
            uint16_t unormY;
            if (image->depth == 8) {
                unormY = ptrY8[i];
            } else {
                // clamp incoming data to protect against bad LUT lookups
                unormY = AVIF_MIN(ptrY16[i], yuvMaxChannel);
            }
            Y = unormFloatTableY[unormY];

            // Calculate Cb and Cr
            if (hasColor) {
                if (image->yuvFormat == AVIF_PIXEL_FORMAT_YUV444) {
                    uint16_t unormU, unormV;

                    if (image->depth == 8) {
                        unormU = ptrU8[uvI];
                        unormV = ptrV8[uvI];
                    } else {
                        // clamp incoming data to protect against bad LUT lookups
                        unormU = AVIF_MIN(ptrU16[uvI], yuvMaxChannel);
                        unormV = AVIF_MIN(ptrV16[uvI], yuvMaxChannel);
                    }

                    Cb = unormFloatTableUV[unormU];
                    Cr = unormFloatTableUV[unormV];
                } else {
                    // Upsample to 444:
                    //
                    // *   *   *   *
                    //   A       B
                    // *   1   2   *
                    //
                    // *   3   4   *
                    //   C       D
                    // *   *   *   *
                    //
                    // When converting from YUV420 to RGB, for any given "high-resolution" RGB
                    // coordinate (1,2,3,4,*), there are up to four "low-resolution" UV samples
                    // (A,B,C,D) that are "nearest" to the pixel. For RGB pixel #1, A is the closest
                    // UV sample, B and C are "adjacent" to it on the same row and column, and D is
                    // the diagonal. For RGB pixel 3, C is the closest UV sample, A and D are
                    // adjacent, and B is the diagonal. Sometimes the adjacent pixel on the same row
                    // is to the left or right, and sometimes the adjacent pixel on the same column
                    // is up or down. For any edge or corner, there might only be only one or two
                    // samples nearby, so they'll be duplicated.
                    //
                    // The following code attempts to find all four nearest UV samples and put them
                    // in the following unormU and unormV grid as follows:
                    //
                    // unorm[0][0] = closest         ( weights: bilinear: 9/16, nearest: 1 )
                    // unorm[1][0] = adjacent col    ( weights: bilinear: 3/16, nearest: 0 )
                    // unorm[0][1] = adjacent row    ( weights: bilinear: 3/16, nearest: 0 )
                    // unorm[1][1] = diagonal        ( weights: bilinear: 1/16, nearest: 0 )
                    //
                    // It then weights them according to the requested upsampling set in avifRGBImage.

                    uint16_t unormU[2][2], unormV[2][2];

                    // How many bytes to add to a uint8_t pointer index to get to the adjacent (lesser) sample in a given direction
                    int uAdjCol, vAdjCol, uAdjRow, vAdjRow;
                    if ((i == 0) || ((i == (image->width - 1)) && ((i % 2) != 0))) {
                        uAdjCol = 0;
                        vAdjCol = 0;
                    } else {
                        if ((i % 2) != 0) {
                            uAdjCol = yuvChannelBytes;
                            vAdjCol = yuvChannelBytes;
                        } else {
                            uAdjCol = -1 * yuvChannelBytes;
                            vAdjCol = -1 * yuvChannelBytes;
                        }
                    }

                    // For YUV422, uvJ will always be a fresh value (always corresponds to j), so
                    // we'll simply duplicate the sample as if we were on the top or bottom row and
                    // it'll behave as plain old linear (1D) upsampling, which is all we want.
                    if ((j == 0) || ((j == (image->height - 1)) && ((j % 2) != 0)) || (image->yuvFormat == AVIF_PIXEL_FORMAT_YUV422)) {
                        uAdjRow = 0;
                        vAdjRow = 0;
                    } else {
                        if ((j % 2) != 0) {
                            uAdjRow = (int)uRowBytes;
                            vAdjRow = (int)vRowBytes;
                        } else {
                            uAdjRow = -1 * (int)uRowBytes;
                            vAdjRow = -1 * (int)vRowBytes;
                        }
                    }

                    if (image->depth == 8) {
                        unormU[0][0] = uPlane[(uvJ * uRowBytes) + (uvI * yuvChannelBytes)];
                        unormV[0][0] = vPlane[(uvJ * vRowBytes) + (uvI * yuvChannelBytes)];
                        unormU[1][0] = uPlane[(uvJ * uRowBytes) + (uvI * yuvChannelBytes) + uAdjCol];
                        unormV[1][0] = vPlane[(uvJ * vRowBytes) + (uvI * yuvChannelBytes) + vAdjCol];
                        unormU[0][1] = uPlane[(uvJ * uRowBytes) + (uvI * yuvChannelBytes) + uAdjRow];
                        unormV[0][1] = vPlane[(uvJ * vRowBytes) + (uvI * yuvChannelBytes) + vAdjRow];
                        unormU[1][1] = uPlane[(uvJ * uRowBytes) + (uvI * yuvChannelBytes) + uAdjCol + uAdjRow];
                        unormV[1][1] = vPlane[(uvJ * vRowBytes) + (uvI * yuvChannelBytes) + vAdjCol + vAdjRow];
                    } else {
                        unormU[0][0] = *((const uint16_t *)&uPlane[(uvJ * uRowBytes) + (uvI * yuvChannelBytes)]);
                        unormV[0][0] = *((const uint16_t *)&vPlane[(uvJ * vRowBytes) + (uvI * yuvChannelBytes)]);
                        unormU[1][0] = *((const uint16_t *)&uPlane[(uvJ * uRowBytes) + (uvI * yuvChannelBytes) + uAdjCol]);
                        unormV[1][0] = *((const uint16_t *)&vPlane[(uvJ * vRowBytes) + (uvI * yuvChannelBytes) + vAdjCol]);
                        unormU[0][1] = *((const uint16_t *)&uPlane[(uvJ * uRowBytes) + (uvI * yuvChannelBytes) + uAdjRow]);
                        unormV[0][1] = *((const uint16_t *)&vPlane[(uvJ * vRowBytes) + (uvI * yuvChannelBytes) + vAdjRow]);
                        unormU[1][1] = *((const uint16_t *)&uPlane[(uvJ * uRowBytes) + (uvI * yuvChannelBytes) + uAdjCol + uAdjRow]);
                        unormV[1][1] = *((const uint16_t *)&vPlane[(uvJ * vRowBytes) + (uvI * yuvChannelBytes) + vAdjCol + vAdjRow]);

                        // clamp incoming data to protect against bad LUT lookups
                        for (int bJ = 0; bJ < 2; ++bJ) {
                            for (int bI = 0; bI < 2; ++bI) {
                                unormU[bI][bJ] = AVIF_MIN(unormU[bI][bJ], yuvMaxChannel);
                                unormV[bI][bJ] = AVIF_MIN(unormV[bI][bJ], yuvMaxChannel);
                            }
                        }
                    }

                    if (chromaUpsampling == AVIF_CHROMA_UPSAMPLING_BILINEAR) {
                        // Bilinear filtering with weights
                        Cb = (unormFloatTableUV[unormU[0][0]] * (9.0f / 16.0f)) + (unormFloatTableUV[unormU[1][0]] * (3.0f / 16.0f)) +
                             (unormFloatTableUV[unormU[0][1]] * (3.0f / 16.0f)) + (unormFloatTableUV[unormU[1][1]] * (1.0f / 16.0f));
                        Cr = (unormFloatTableUV[unormV[0][0]] * (9.0f / 16.0f)) + (unormFloatTableUV[unormV[1][0]] * (3.0f / 16.0f)) +
                             (unormFloatTableUV[unormV[0][1]] * (3.0f / 16.0f)) + (unormFloatTableUV[unormV[1][1]] * (1.0f / 16.0f));
                    } else {
                        assert(chromaUpsampling == AVIF_CHROMA_UPSAMPLING_NEAREST);

                        // Nearest neighbor; ignore all UVs but the closest one
                        Cb = unormFloatTableUV[unormU[0][0]];
                        Cr = unormFloatTableUV[unormV[0][0]];
                    }
                }
            }

            float R, G, B;
            if (hasColor) {
                if (state->mode == AVIF_REFORMAT_MODE_IDENTITY) {
                    // Identity (GBR): Formulas 41,42,43 from https://www.itu.int/rec/T-REC-H.273-201612-I/en
                    G = Y;
                    B = Cb;
                    R = Cr;
                } else if (state->mode == AVIF_REFORMAT_MODE_YCGCO) {
                    // YCgCo: Formulas 47,48,49,50 from https://www.itu.int/rec/T-REC-H.273-201612-I/en
                    const float t = Y - Cb;
                    G = Y + Cb;
                    B = t - Cr;
                    R = t + Cr;
                } else {
                    // Normal YUV
                    R = Y + (2 * (1 - kr)) * Cr;
                    B = Y + (2 * (1 - kb)) * Cb;
                    G = Y - ((2 * ((kr * (1 - kr) * Cr) + (kb * (1 - kb) * Cb))) / kg);
                }
            } else {
                // Monochrome: just populate all channels with luma (identity mode is irrelevant)
                R = Y;
                G = Y;
                B = Y;
            }

            const float Rc = AVIF_CLAMP(R, 0.0f, 1.0f);
            const float Gc = AVIF_CLAMP(G, 0.0f, 1.0f);
            const float Bc = AVIF_CLAMP(B, 0.0f, 1.0f);

            if (rgb->depth == 8) {
                *ptrR = (uint8_t)(0.5f + (Rc * rgbMaxChannelF));
                *ptrG = (uint8_t)(0.5f + (Gc * rgbMaxChannelF));
                *ptrB = (uint8_t)(0.5f + (Bc * rgbMaxChannelF));
            } else {
                *((uint16_t *)ptrR) = (uint16_t)(0.5f + (Rc * rgbMaxChannelF));
                *((uint16_t *)ptrG) = (uint16_t)(0.5f + (Gc * rgbMaxChannelF));
                *((uint16_t *)ptrB) = (uint16_t)(0.5f + (Bc * rgbMaxChannelF));
            }
            ptrR += rgbPixelBytes;
            ptrG += rgbPixelBytes;
            ptrB += rgbPixelBytes;
        }
    }
    return AVIF_RESULT_OK;
}

static avifResult avifImageYUV16ToRGB16Color(const avifImage * image, avifRGBImage * rgb, avifReformatState * state)
{
    const float kr = state->kr;
    const float kg = state->kg;
    const float kb = state->kb;
    const uint32_t rgbPixelBytes = state->rgbPixelBytes;
    const float * const unormFloatTableY = state->unormFloatTableY;
    const float * const unormFloatTableUV = state->unormFloatTableUV;

    const uint16_t yuvMaxChannel = (uint16_t)state->yuvMaxChannel;
    const float rgbMaxChannelF = state->rgbMaxChannelF;
    for (uint32_t j = 0; j < image->height; ++j) {
        const uint32_t uvJ = j >> state->formatInfo.chromaShiftY;
        const uint16_t * const ptrY = (uint16_t *)&image->yuvPlanes[AVIF_CHAN_Y][(j * image->yuvRowBytes[AVIF_CHAN_Y])];
        const uint16_t * const ptrU = (uint16_t *)&image->yuvPlanes[AVIF_CHAN_U][(uvJ * image->yuvRowBytes[AVIF_CHAN_U])];
        const uint16_t * const ptrV = (uint16_t *)&image->yuvPlanes[AVIF_CHAN_V][(uvJ * image->yuvRowBytes[AVIF_CHAN_V])];
        uint8_t * ptrR = &rgb->pixels[state->rgbOffsetBytesR + (j * rgb->rowBytes)];
        uint8_t * ptrG = &rgb->pixels[state->rgbOffsetBytesG + (j * rgb->rowBytes)];
        uint8_t * ptrB = &rgb->pixels[state->rgbOffsetBytesB + (j * rgb->rowBytes)];

        for (uint32_t i = 0; i < image->width; ++i) {
            uint32_t uvI = i >> state->formatInfo.chromaShiftX;

            // clamp incoming data to protect against bad LUT lookups
            const uint16_t unormY = AVIF_MIN(ptrY[i], yuvMaxChannel);
            const uint16_t unormU = AVIF_MIN(ptrU[uvI], yuvMaxChannel);
            const uint16_t unormV = AVIF_MIN(ptrV[uvI], yuvMaxChannel);

            // Convert unorm to float
            const float Y = unormFloatTableY[unormY];
            const float Cb = unormFloatTableUV[unormU];
            const float Cr = unormFloatTableUV[unormV];

            const float R = Y + (2 * (1 - kr)) * Cr;
            const float B = Y + (2 * (1 - kb)) * Cb;
            const float G = Y - ((2 * ((kr * (1 - kr) * Cr) + (kb * (1 - kb) * Cb))) / kg);
            const float Rc = AVIF_CLAMP(R, 0.0f, 1.0f);
            const float Gc = AVIF_CLAMP(G, 0.0f, 1.0f);
            const float Bc = AVIF_CLAMP(B, 0.0f, 1.0f);

            *((uint16_t *)ptrR) = (uint16_t)(0.5f + (Rc * rgbMaxChannelF));
            *((uint16_t *)ptrG) = (uint16_t)(0.5f + (Gc * rgbMaxChannelF));
            *((uint16_t *)ptrB) = (uint16_t)(0.5f + (Bc * rgbMaxChannelF));

            ptrR += rgbPixelBytes;
            ptrG += rgbPixelBytes;
            ptrB += rgbPixelBytes;
        }
    }
    return AVIF_RESULT_OK;
}

static avifResult avifImageYUV16ToRGB16Mono(const avifImage * image, avifRGBImage * rgb, avifReformatState * state)
{
    const float kr = state->kr;
    const float kg = state->kg;
    const float kb = state->kb;
    const uint32_t rgbPixelBytes = state->rgbPixelBytes;
    const float * const unormFloatTableY = state->unormFloatTableY;

    const uint16_t yuvMaxChannel = (uint16_t)state->yuvMaxChannel;
    const float rgbMaxChannelF = state->rgbMaxChannelF;
    for (uint32_t j = 0; j < image->height; ++j) {
        const uint16_t * const ptrY = (uint16_t *)&image->yuvPlanes[AVIF_CHAN_Y][(j * image->yuvRowBytes[AVIF_CHAN_Y])];
        uint8_t * ptrR = &rgb->pixels[state->rgbOffsetBytesR + (j * rgb->rowBytes)];
        uint8_t * ptrG = &rgb->pixels[state->rgbOffsetBytesG + (j * rgb->rowBytes)];
        uint8_t * ptrB = &rgb->pixels[state->rgbOffsetBytesB + (j * rgb->rowBytes)];

        for (uint32_t i = 0; i < image->width; ++i) {
            // clamp incoming data to protect against bad LUT lookups
            const uint16_t unormY = AVIF_MIN(ptrY[i], yuvMaxChannel);

            // Convert unorm to float
            const float Y = unormFloatTableY[unormY];
            const float Cb = 0.0f;
            const float Cr = 0.0f;

            const float R = Y + (2 * (1 - kr)) * Cr;
            const float B = Y + (2 * (1 - kb)) * Cb;
            const float G = Y - ((2 * ((kr * (1 - kr) * Cr) + (kb * (1 - kb) * Cb))) / kg);
            const float Rc = AVIF_CLAMP(R, 0.0f, 1.0f);
            const float Gc = AVIF_CLAMP(G, 0.0f, 1.0f);
            const float Bc = AVIF_CLAMP(B, 0.0f, 1.0f);

            *((uint16_t *)ptrR) = (uint16_t)(0.5f + (Rc * rgbMaxChannelF));
            *((uint16_t *)ptrG) = (uint16_t)(0.5f + (Gc * rgbMaxChannelF));
            *((uint16_t *)ptrB) = (uint16_t)(0.5f + (Bc * rgbMaxChannelF));

            ptrR += rgbPixelBytes;
            ptrG += rgbPixelBytes;
            ptrB += rgbPixelBytes;
        }
    }
    return AVIF_RESULT_OK;
}

static avifResult avifImageYUV16ToRGB8Color(const avifImage * image, avifRGBImage * rgb, avifReformatState * state)
{
    const float kr = state->kr;
    const float kg = state->kg;
    const float kb = state->kb;
    const uint32_t rgbPixelBytes = state->rgbPixelBytes;
    const float * const unormFloatTableY = state->unormFloatTableY;
    const float * const unormFloatTableUV = state->unormFloatTableUV;

    const uint16_t yuvMaxChannel = (uint16_t)state->yuvMaxChannel;
    const float rgbMaxChannelF = state->rgbMaxChannelF;
    for (uint32_t j = 0; j < image->height; ++j) {
        const uint32_t uvJ = j >> state->formatInfo.chromaShiftY;
        const uint16_t * const ptrY = (uint16_t *)&image->yuvPlanes[AVIF_CHAN_Y][(j * image->yuvRowBytes[AVIF_CHAN_Y])];
        const uint16_t * const ptrU = (uint16_t *)&image->yuvPlanes[AVIF_CHAN_U][(uvJ * image->yuvRowBytes[AVIF_CHAN_U])];
        const uint16_t * const ptrV = (uint16_t *)&image->yuvPlanes[AVIF_CHAN_V][(uvJ * image->yuvRowBytes[AVIF_CHAN_V])];
        uint8_t * ptrR = &rgb->pixels[state->rgbOffsetBytesR + (j * rgb->rowBytes)];
        uint8_t * ptrG = &rgb->pixels[state->rgbOffsetBytesG + (j * rgb->rowBytes)];
        uint8_t * ptrB = &rgb->pixels[state->rgbOffsetBytesB + (j * rgb->rowBytes)];

        for (uint32_t i = 0; i < image->width; ++i) {
            uint32_t uvI = i >> state->formatInfo.chromaShiftX;

            // clamp incoming data to protect against bad LUT lookups
            const uint16_t unormY = AVIF_MIN(ptrY[i], yuvMaxChannel);
            const uint16_t unormU = AVIF_MIN(ptrU[uvI], yuvMaxChannel);
            const uint16_t unormV = AVIF_MIN(ptrV[uvI], yuvMaxChannel);

            // Convert unorm to float
            const float Y = unormFloatTableY[unormY];
            const float Cb = unormFloatTableUV[unormU];
            const float Cr = unormFloatTableUV[unormV];

            const float R = Y + (2 * (1 - kr)) * Cr;
            const float B = Y + (2 * (1 - kb)) * Cb;
            const float G = Y - ((2 * ((kr * (1 - kr) * Cr) + (kb * (1 - kb) * Cb))) / kg);
            const float Rc = AVIF_CLAMP(R, 0.0f, 1.0f);
            const float Gc = AVIF_CLAMP(G, 0.0f, 1.0f);
            const float Bc = AVIF_CLAMP(B, 0.0f, 1.0f);

            *ptrR = (uint8_t)(0.5f + (Rc * rgbMaxChannelF));
            *ptrG = (uint8_t)(0.5f + (Gc * rgbMaxChannelF));
            *ptrB = (uint8_t)(0.5f + (Bc * rgbMaxChannelF));

            ptrR += rgbPixelBytes;
            ptrG += rgbPixelBytes;
            ptrB += rgbPixelBytes;
        }
    }
    return AVIF_RESULT_OK;
}

static avifResult avifImageYUV16ToRGB8Mono(const avifImage * image, avifRGBImage * rgb, avifReformatState * state)
{
    const float kr = state->kr;
    const float kg = state->kg;
    const float kb = state->kb;
    const uint32_t rgbPixelBytes = state->rgbPixelBytes;
    const float * const unormFloatTableY = state->unormFloatTableY;

    const uint16_t yuvMaxChannel = (uint16_t)state->yuvMaxChannel;
    const float rgbMaxChannelF = state->rgbMaxChannelF;
    for (uint32_t j = 0; j < image->height; ++j) {
        const uint16_t * const ptrY = (uint16_t *)&image->yuvPlanes[AVIF_CHAN_Y][(j * image->yuvRowBytes[AVIF_CHAN_Y])];
        uint8_t * ptrR = &rgb->pixels[state->rgbOffsetBytesR + (j * rgb->rowBytes)];
        uint8_t * ptrG = &rgb->pixels[state->rgbOffsetBytesG + (j * rgb->rowBytes)];
        uint8_t * ptrB = &rgb->pixels[state->rgbOffsetBytesB + (j * rgb->rowBytes)];

        for (uint32_t i = 0; i < image->width; ++i) {
            // clamp incoming data to protect against bad LUT lookups
            const uint16_t unormY = AVIF_MIN(ptrY[i], yuvMaxChannel);

            // Convert unorm to float
            const float Y = unormFloatTableY[unormY];
            const float Cb = 0.0f;
            const float Cr = 0.0f;

            const float R = Y + (2 * (1 - kr)) * Cr;
            const float B = Y + (2 * (1 - kb)) * Cb;
            const float G = Y - ((2 * ((kr * (1 - kr) * Cr) + (kb * (1 - kb) * Cb))) / kg);
            const float Rc = AVIF_CLAMP(R, 0.0f, 1.0f);
            const float Gc = AVIF_CLAMP(G, 0.0f, 1.0f);
            const float Bc = AVIF_CLAMP(B, 0.0f, 1.0f);

            *ptrR = (uint8_t)(0.5f + (Rc * rgbMaxChannelF));
            *ptrG = (uint8_t)(0.5f + (Gc * rgbMaxChannelF));
            *ptrB = (uint8_t)(0.5f + (Bc * rgbMaxChannelF));

            ptrR += rgbPixelBytes;
            ptrG += rgbPixelBytes;
            ptrB += rgbPixelBytes;
        }
    }
    return AVIF_RESULT_OK;
}

static avifResult avifImageYUV8ToRGB16Color(const avifImage * image, avifRGBImage * rgb, avifReformatState * state)
{
    const float kr = state->kr;
    const float kg = state->kg;
    const float kb = state->kb;
    const uint32_t rgbPixelBytes = state->rgbPixelBytes;
    const float * const unormFloatTableY = state->unormFloatTableY;
    const float * const unormFloatTableUV = state->unormFloatTableUV;

    const float rgbMaxChannelF = state->rgbMaxChannelF;
    for (uint32_t j = 0; j < image->height; ++j) {
        const uint32_t uvJ = j >> state->formatInfo.chromaShiftY;
        const uint8_t * const ptrY = &image->yuvPlanes[AVIF_CHAN_Y][(j * image->yuvRowBytes[AVIF_CHAN_Y])];
        const uint8_t * const ptrU = &image->yuvPlanes[AVIF_CHAN_U][(uvJ * image->yuvRowBytes[AVIF_CHAN_U])];
        const uint8_t * const ptrV = &image->yuvPlanes[AVIF_CHAN_V][(uvJ * image->yuvRowBytes[AVIF_CHAN_V])];
        uint8_t * ptrR = &rgb->pixels[state->rgbOffsetBytesR + (j * rgb->rowBytes)];
        uint8_t * ptrG = &rgb->pixels[state->rgbOffsetBytesG + (j * rgb->rowBytes)];
        uint8_t * ptrB = &rgb->pixels[state->rgbOffsetBytesB + (j * rgb->rowBytes)];

        for (uint32_t i = 0; i < image->width; ++i) {
            uint32_t uvI = i >> state->formatInfo.chromaShiftX;

            // Convert unorm to float (no clamp necessary, the full uint8_t range is a legal lookup)
            const float Y = unormFloatTableY[ptrY[i]];
            const float Cb = unormFloatTableUV[ptrU[uvI]];
            const float Cr = unormFloatTableUV[ptrV[uvI]];

            const float R = Y + (2 * (1 - kr)) * Cr;
            const float B = Y + (2 * (1 - kb)) * Cb;
            const float G = Y - ((2 * ((kr * (1 - kr) * Cr) + (kb * (1 - kb) * Cb))) / kg);
            const float Rc = AVIF_CLAMP(R, 0.0f, 1.0f);
            const float Gc = AVIF_CLAMP(G, 0.0f, 1.0f);
            const float Bc = AVIF_CLAMP(B, 0.0f, 1.0f);

            *((uint16_t *)ptrR) = (uint16_t)(0.5f + (Rc * rgbMaxChannelF));
            *((uint16_t *)ptrG) = (uint16_t)(0.5f + (Gc * rgbMaxChannelF));
            *((uint16_t *)ptrB) = (uint16_t)(0.5f + (Bc * rgbMaxChannelF));

            ptrR += rgbPixelBytes;
            ptrG += rgbPixelBytes;
            ptrB += rgbPixelBytes;
        }
    }
    return AVIF_RESULT_OK;
}

static avifResult avifImageYUV8ToRGB16Mono(const avifImage * image, avifRGBImage * rgb, avifReformatState * state)
{
    const float kr = state->kr;
    const float kg = state->kg;
    const float kb = state->kb;
    const uint32_t rgbPixelBytes = state->rgbPixelBytes;
    const float * const unormFloatTableY = state->unormFloatTableY;

    const float rgbMaxChannelF = state->rgbMaxChannelF;
    for (uint32_t j = 0; j < image->height; ++j) {
        const uint8_t * const ptrY = &image->yuvPlanes[AVIF_CHAN_Y][(j * image->yuvRowBytes[AVIF_CHAN_Y])];
        uint8_t * ptrR = &rgb->pixels[state->rgbOffsetBytesR + (j * rgb->rowBytes)];
        uint8_t * ptrG = &rgb->pixels[state->rgbOffsetBytesG + (j * rgb->rowBytes)];
        uint8_t * ptrB = &rgb->pixels[state->rgbOffsetBytesB + (j * rgb->rowBytes)];

        for (uint32_t i = 0; i < image->width; ++i) {
            // Convert unorm to float (no clamp necessary, the full uint8_t range is a legal lookup)
            const float Y = unormFloatTableY[ptrY[i]];
            const float Cb = 0.0f;
            const float Cr = 0.0f;

            const float R = Y + (2 * (1 - kr)) * Cr;
            const float B = Y + (2 * (1 - kb)) * Cb;
            const float G = Y - ((2 * ((kr * (1 - kr) * Cr) + (kb * (1 - kb) * Cb))) / kg);
            const float Rc = AVIF_CLAMP(R, 0.0f, 1.0f);
            const float Gc = AVIF_CLAMP(G, 0.0f, 1.0f);
            const float Bc = AVIF_CLAMP(B, 0.0f, 1.0f);

            *((uint16_t *)ptrR) = (uint16_t)(0.5f + (Rc * rgbMaxChannelF));
            *((uint16_t *)ptrG) = (uint16_t)(0.5f + (Gc * rgbMaxChannelF));
            *((uint16_t *)ptrB) = (uint16_t)(0.5f + (Bc * rgbMaxChannelF));

            ptrR += rgbPixelBytes;
            ptrG += rgbPixelBytes;
            ptrB += rgbPixelBytes;
        }
    }
    return AVIF_RESULT_OK;
}

static avifResult avifImageIdentity8ToRGB8ColorFullRange(const avifImage * image, avifRGBImage * rgb, avifReformatState * state)
{
    const uint32_t rgbPixelBytes = state->rgbPixelBytes;
    for (uint32_t j = 0; j < image->height; ++j) {
        const uint8_t * const ptrY = &image->yuvPlanes[AVIF_CHAN_Y][(j * image->yuvRowBytes[AVIF_CHAN_Y])];
        const uint8_t * const ptrU = &image->yuvPlanes[AVIF_CHAN_U][(j * image->yuvRowBytes[AVIF_CHAN_U])];
        const uint8_t * const ptrV = &image->yuvPlanes[AVIF_CHAN_V][(j * image->yuvRowBytes[AVIF_CHAN_V])];
        uint8_t * ptrR = &rgb->pixels[state->rgbOffsetBytesR + (j * rgb->rowBytes)];
        uint8_t * ptrG = &rgb->pixels[state->rgbOffsetBytesG + (j * rgb->rowBytes)];
        uint8_t * ptrB = &rgb->pixels[state->rgbOffsetBytesB + (j * rgb->rowBytes)];

        for (uint32_t i = 0; i < image->width; ++i) {
            *ptrR = ptrV[i];
            *ptrG = ptrY[i];
            *ptrB = ptrU[i];

            ptrR += rgbPixelBytes;
            ptrG += rgbPixelBytes;
            ptrB += rgbPixelBytes;
        }
    }
    return AVIF_RESULT_OK;
}

static avifResult avifImageYUV8ToRGB8Color(const avifImage * image, avifRGBImage * rgb, avifReformatState * state)
{
    const float kr = state->kr;
    const float kg = state->kg;
    const float kb = state->kb;
    const uint32_t rgbPixelBytes = state->rgbPixelBytes;
    const float * const unormFloatTableY = state->unormFloatTableY;
    const float * const unormFloatTableUV = state->unormFloatTableUV;

    const float rgbMaxChannelF = state->rgbMaxChannelF;
    for (uint32_t j = 0; j < image->height; ++j) {
        const uint32_t uvJ = j >> state->formatInfo.chromaShiftY;
        const uint8_t * const ptrY = &image->yuvPlanes[AVIF_CHAN_Y][(j * image->yuvRowBytes[AVIF_CHAN_Y])];
        const uint8_t * const ptrU = &image->yuvPlanes[AVIF_CHAN_U][(uvJ * image->yuvRowBytes[AVIF_CHAN_U])];
        const uint8_t * const ptrV = &image->yuvPlanes[AVIF_CHAN_V][(uvJ * image->yuvRowBytes[AVIF_CHAN_V])];
        uint8_t * ptrR = &rgb->pixels[state->rgbOffsetBytesR + (j * rgb->rowBytes)];
        uint8_t * ptrG = &rgb->pixels[state->rgbOffsetBytesG + (j * rgb->rowBytes)];
        uint8_t * ptrB = &rgb->pixels[state->rgbOffsetBytesB + (j * rgb->rowBytes)];

        for (uint32_t i = 0; i < image->width; ++i) {
            uint32_t uvI = i >> state->formatInfo.chromaShiftX;

            // Convert unorm to float (no clamp necessary, the full uint8_t range is a legal lookup)
            const float Y = unormFloatTableY[ptrY[i]];
            const float Cb = unormFloatTableUV[ptrU[uvI]];
            const float Cr = unormFloatTableUV[ptrV[uvI]];

            const float R = Y + (2 * (1 - kr)) * Cr;
            const float B = Y + (2 * (1 - kb)) * Cb;
            const float G = Y - ((2 * ((kr * (1 - kr) * Cr) + (kb * (1 - kb) * Cb))) / kg);
            const float Rc = AVIF_CLAMP(R, 0.0f, 1.0f);
            const float Gc = AVIF_CLAMP(G, 0.0f, 1.0f);
            const float Bc = AVIF_CLAMP(B, 0.0f, 1.0f);

            *ptrR = (uint8_t)(0.5f + (Rc * rgbMaxChannelF));
            *ptrG = (uint8_t)(0.5f + (Gc * rgbMaxChannelF));
            *ptrB = (uint8_t)(0.5f + (Bc * rgbMaxChannelF));

            ptrR += rgbPixelBytes;
            ptrG += rgbPixelBytes;
            ptrB += rgbPixelBytes;
        }
    }
    return AVIF_RESULT_OK;
}

static avifResult avifImageYUV8ToRGB8Mono(const avifImage * image, avifRGBImage * rgb, avifReformatState * state)
{
    const float kr = state->kr;
    const float kg = state->kg;
    const float kb = state->kb;
    const uint32_t rgbPixelBytes = state->rgbPixelBytes;
    const float * const unormFloatTableY = state->unormFloatTableY;

    const float rgbMaxChannelF = state->rgbMaxChannelF;
    for (uint32_t j = 0; j < image->height; ++j) {
        const uint8_t * const ptrY = &image->yuvPlanes[AVIF_CHAN_Y][(j * image->yuvRowBytes[AVIF_CHAN_Y])];
        uint8_t * ptrR = &rgb->pixels[state->rgbOffsetBytesR + (j * rgb->rowBytes)];
        uint8_t * ptrG = &rgb->pixels[state->rgbOffsetBytesG + (j * rgb->rowBytes)];
        uint8_t * ptrB = &rgb->pixels[state->rgbOffsetBytesB + (j * rgb->rowBytes)];

        for (uint32_t i = 0; i < image->width; ++i) {
            // Convert unorm to float (no clamp necessary, the full uint8_t range is a legal lookup)
            const float Y = unormFloatTableY[ptrY[i]];
            const float Cb = 0.0f;
            const float Cr = 0.0f;

            const float R = Y + (2 * (1 - kr)) * Cr;
            const float B = Y + (2 * (1 - kb)) * Cb;
            const float G = Y - ((2 * ((kr * (1 - kr) * Cr) + (kb * (1 - kb) * Cb))) / kg);
            const float Rc = AVIF_CLAMP(R, 0.0f, 1.0f);
            const float Gc = AVIF_CLAMP(G, 0.0f, 1.0f);
            const float Bc = AVIF_CLAMP(B, 0.0f, 1.0f);

            *ptrR = (uint8_t)(0.5f + (Rc * rgbMaxChannelF));
            *ptrG = (uint8_t)(0.5f + (Gc * rgbMaxChannelF));
            *ptrB = (uint8_t)(0.5f + (Bc * rgbMaxChannelF));

            ptrR += rgbPixelBytes;
            ptrG += rgbPixelBytes;
            ptrB += rgbPixelBytes;
        }
    }
    return AVIF_RESULT_OK;
}

avifResult avifImageYUVToRGB(const avifImage * image, avifRGBImage * rgb)
{
    if (!image->yuvPlanes[AVIF_CHAN_Y]) {
        return AVIF_RESULT_REFORMAT_FAILED;
    }

    avifReformatState state;
    if (!avifPrepareReformatState(image, rgb, &state)) {
        return AVIF_RESULT_REFORMAT_FAILED;
    }

    avifBool convertedWithLibYUV = AVIF_FALSE;
    avifResult libyuvResult = avifImageYUVToRGBLibYUV(image, rgb);
    if (libyuvResult == AVIF_RESULT_OK) {
        convertedWithLibYUV = AVIF_TRUE;
    } else {
        if (libyuvResult != AVIF_RESULT_NOT_IMPLEMENTED) {
            return libyuvResult;
        }
    }

    if (avifRGBFormatHasAlpha(rgb->format) && !rgb->ignoreAlpha) {
        avifAlphaParams params;

        params.width = rgb->width;
        params.height = rgb->height;
        params.dstDepth = rgb->depth;
        params.dstRange = AVIF_RANGE_FULL;
        params.dstPlane = rgb->pixels;
        params.dstRowBytes = rgb->rowBytes;
        params.dstOffsetBytes = state.rgbOffsetBytesA;
        params.dstPixelBytes = state.rgbPixelBytes;

        if (image->alphaPlane && image->alphaRowBytes) {
            params.srcDepth = image->depth;
            params.srcRange = image->alphaRange;
            params.srcPlane = image->alphaPlane;
            params.srcRowBytes = image->alphaRowBytes;
            params.srcOffsetBytes = 0;
            params.srcPixelBytes = state.yuvChannelBytes;

            avifReformatAlpha(&params);
        } else {
            if (!convertedWithLibYUV) { // libyuv fills alpha for us
                avifFillAlpha(&params);
            }
        }
    }

    // Do this after alpha conversion
    if (convertedWithLibYUV) {
        return AVIF_RESULT_OK;
    }

    avifChromaUpsampling chromaUpsampling;
    switch (rgb->chromaUpsampling) {
        case AVIF_CHROMA_UPSAMPLING_AUTOMATIC:
        case AVIF_CHROMA_UPSAMPLING_BEST_QUALITY:
        case AVIF_CHROMA_UPSAMPLING_BILINEAR:
        default:
            chromaUpsampling = AVIF_CHROMA_UPSAMPLING_BILINEAR;
            break;

        case AVIF_CHROMA_UPSAMPLING_FASTEST:
        case AVIF_CHROMA_UPSAMPLING_NEAREST:
            chromaUpsampling = AVIF_CHROMA_UPSAMPLING_NEAREST;
            break;
    }

    const avifBool hasColor =
        (image->yuvRowBytes[AVIF_CHAN_U] && image->yuvRowBytes[AVIF_CHAN_V] && (image->yuvFormat != AVIF_PIXEL_FORMAT_YUV400));

    avifResult convertResult = AVIF_RESULT_NOT_IMPLEMENTED;

    if (!hasColor || (image->yuvFormat == AVIF_PIXEL_FORMAT_YUV444) || (chromaUpsampling == AVIF_CHROMA_UPSAMPLING_NEAREST)) {
        // None of these fast paths currently support bilinear upsampling, so avoid all of them
        // unless the YUV data isn't subsampled or they explicitly requested AVIF_CHROMA_UPSAMPLING_NEAREST.

        if (state.mode == AVIF_REFORMAT_MODE_IDENTITY) {
            if ((image->depth == 8) && (rgb->depth == 8) && (image->yuvFormat == AVIF_PIXEL_FORMAT_YUV444) &&
                (image->yuvRange == AVIF_RANGE_FULL)) {
                convertResult = avifImageIdentity8ToRGB8ColorFullRange(image, rgb, &state);
            }

            // TODO: Add more fast paths for identity
        } else if (state.mode == AVIF_REFORMAT_MODE_YUV_COEFFICIENTS) {
            if (image->depth > 8) {
                // yuv:u16

                if (rgb->depth > 8) {
                    // yuv:u16, rgb:u16

                    if (hasColor) {
                        convertResult = avifImageYUV16ToRGB16Color(image, rgb, &state);
                    } else {
                        convertResult = avifImageYUV16ToRGB16Mono(image, rgb, &state);
                    }
                } else {
                    // yuv:u16, rgb:u8

                    if (hasColor) {
                        convertResult = avifImageYUV16ToRGB8Color(image, rgb, &state);
                    } else {
                        convertResult = avifImageYUV16ToRGB8Mono(image, rgb, &state);
                    }
                }
            } else {
                // yuv:u8

                if (rgb->depth > 8) {
                    // yuv:u8, rgb:u16

                    if (hasColor) {
                        convertResult = avifImageYUV8ToRGB16Color(image, rgb, &state);
                    } else {
                        convertResult = avifImageYUV8ToRGB16Mono(image, rgb, &state);
                    }
                } else {
                    // yuv:u8, rgb:u8

                    if (hasColor) {
                        convertResult = avifImageYUV8ToRGB8Color(image, rgb, &state);
                    } else {
                        convertResult = avifImageYUV8ToRGB8Mono(image, rgb, &state);
                    }
                }
            }
        }
    }

    if (convertResult == AVIF_RESULT_NOT_IMPLEMENTED) {
        // If we get here, there is no fast path for this combination. Time to be slow!
        convertResult = avifImageYUVAnyToRGBAnySlow(image, rgb, &state, chromaUpsampling);
    }

    if (convertResult != AVIF_RESULT_OK) {
        return convertResult;
    }

    if (avifRGBFormatHasAlpha(rgb->format) && !rgb->ignoreAlpha) {
        if (image->alphaPremultiplied && !rgb->alphaPremultiplied) {
            convertResult = avifRGBImageUnpremultiplyAlpha(rgb);
        } else if (!image->alphaPremultiplied && rgb->alphaPremultiplied) {
            convertResult = avifRGBImagePremultiplyAlpha(rgb);
        }
    }

    return convertResult;
}

// Limited -> Full
// Plan: subtract limited offset, then multiply by ratio of FULLSIZE/LIMITEDSIZE (rounding), then clamp.
// RATIO = (FULLY - 0) / (MAXLIMITEDY - MINLIMITEDY)
// -----------------------------------------
// ( ( (v - MINLIMITEDY)                    | subtract limited offset
//     * FULLY                              | multiply numerator of ratio
//   ) + ((MAXLIMITEDY - MINLIMITEDY) / 2)  | add 0.5 (half of denominator) to round
// ) / (MAXLIMITEDY - MINLIMITEDY)          | divide by denominator of ratio
// AVIF_CLAMP(v, 0, FULLY)                  | clamp to full range
// -----------------------------------------
#define LIMITED_TO_FULL(MINLIMITEDY, MAXLIMITEDY, FULLY)                                                 \
    v = (((v - MINLIMITEDY) * FULLY) + ((MAXLIMITEDY - MINLIMITEDY) / 2)) / (MAXLIMITEDY - MINLIMITEDY); \
    v = AVIF_CLAMP(v, 0, FULLY)

// Full -> Limited
// Plan: multiply by ratio of LIMITEDSIZE/FULLSIZE (rounding), then add limited offset, then clamp.
// RATIO = (MAXLIMITEDY - MINLIMITEDY) / (FULLY - 0)
// -----------------------------------------
// ( ( (v * (MAXLIMITEDY - MINLIMITEDY))    | multiply numerator of ratio
//     + (FULLY / 2)                        | add 0.5 (half of denominator) to round
//   ) / FULLY                              | divide by denominator of ratio
// ) + MINLIMITEDY                          | add limited offset
//  AVIF_CLAMP(v, MINLIMITEDY, MAXLIMITEDY) | clamp to limited range
// -----------------------------------------
#define FULL_TO_LIMITED(MINLIMITEDY, MAXLIMITEDY, FULLY)                           \
    v = (((v * (MAXLIMITEDY - MINLIMITEDY)) + (FULLY / 2)) / FULLY) + MINLIMITEDY; \
    v = AVIF_CLAMP(v, MINLIMITEDY, MAXLIMITEDY)

int avifLimitedToFullY(int depth, int v)
{
    switch (depth) {
        case 8:
            LIMITED_TO_FULL(16, 235, 255);
            break;
        case 10:
            LIMITED_TO_FULL(64, 940, 1023);
            break;
        case 12:
            LIMITED_TO_FULL(256, 3760, 4095);
            break;
    }
    return v;
}

int avifLimitedToFullUV(int depth, int v)
{
    switch (depth) {
        case 8:
            LIMITED_TO_FULL(16, 240, 255);
            break;
        case 10:
            LIMITED_TO_FULL(64, 960, 1023);
            break;
        case 12:
            LIMITED_TO_FULL(256, 3840, 4095);
            break;
    }
    return v;
}

int avifFullToLimitedY(int depth, int v)
{
    switch (depth) {
        case 8:
            FULL_TO_LIMITED(16, 235, 255);
            break;
        case 10:
            FULL_TO_LIMITED(64, 940, 1023);
            break;
        case 12:
            FULL_TO_LIMITED(256, 3760, 4095);
            break;
    }
    return v;
}

int avifFullToLimitedUV(int depth, int v)
{
    switch (depth) {
        case 8:
            FULL_TO_LIMITED(16, 240, 255);
            break;
        case 10:
            FULL_TO_LIMITED(64, 960, 1023);
            break;
        case 12:
            FULL_TO_LIMITED(256, 3840, 4095);
            break;
    }
    return v;
}
