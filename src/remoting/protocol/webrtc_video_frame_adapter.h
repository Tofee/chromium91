// Copyright 2021 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef REMOTING_PROTOCOL_WEBRTC_VIDEO_FRAME_ADAPTER_H_
#define REMOTING_PROTOCOL_WEBRTC_VIDEO_FRAME_ADAPTER_H_

#include <memory>

#include "third_party/webrtc/api/video/video_frame.h"
#include "third_party/webrtc/api/video/video_frame_buffer.h"
#include "third_party/webrtc/modules/desktop_capture/desktop_frame.h"

namespace remoting {
namespace protocol {

// Adapter class to wrap a DesktopFrame produced by the capturer, and provide
// it as a VideoFrame to the WebRTC video sink. The encoder will extract the
// captured DesktopFrame from VideoFrame::video_frame_buffer().
class WebrtcVideoFrameAdapter : public webrtc::VideoFrameBuffer {
 public:
  explicit WebrtcVideoFrameAdapter(std::unique_ptr<webrtc::DesktopFrame> frame);
  ~WebrtcVideoFrameAdapter() override;
  WebrtcVideoFrameAdapter(const WebrtcVideoFrameAdapter&) = delete;
  WebrtcVideoFrameAdapter& operator=(const WebrtcVideoFrameAdapter&) = delete;

  // Returns a VideoFrame that wraps the provided DesktopFrame.
  static webrtc::VideoFrame CreateVideoFrame(
      std::unique_ptr<webrtc::DesktopFrame> desktop_frame);

  // Used by the encoder. After this returns, the adapter no longer wraps a
  // DesktopFrame.
  std::unique_ptr<webrtc::DesktopFrame> TakeDesktopFrame();

  // webrtc::VideoFrameBuffer overrides.
  Type type() const override;
  int width() const override;
  int height() const override;
  rtc::scoped_refptr<webrtc::I420BufferInterface> ToI420() override;

 private:
  std::unique_ptr<webrtc::DesktopFrame> frame_;
  webrtc::DesktopSize frame_size_;
};

}  // namespace protocol
}  // namespace remoting

#endif  // REMOTING_PROTOCOL_WEBRTC_VIDEO_FRAME_ADAPTER_H_
