// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CONTENT_CHILD_WEBTHEMEENGINE_IMPL_MAC_H_
#define CONTENT_CHILD_WEBTHEMEENGINE_IMPL_MAC_H_

#include "content/child/webthemeengine_impl_default.h"

namespace content {

class WebThemeEngineMac : public WebThemeEngineDefault {
 public:
  ~WebThemeEngineMac() override {}

  void Paint(cc::PaintCanvas* canvas,
             blink::WebThemeEngine::Part part,
             blink::WebThemeEngine::State state,
             const gfx::Rect& rect,
             const blink::WebThemeEngine::ExtraParams* extra_params,
             blink::mojom::ColorScheme color_scheme,
             const base::Optional<SkColor>& accent_color) override;

  static bool IsScrollbarPart(WebThemeEngine::Part part);
  static void PaintMacScrollBarParts(
      cc::PaintCanvas* canvas,
      WebThemeEngine::Part part,
      WebThemeEngine::State state,
      const gfx::Rect& rect,
      const WebThemeEngine::ExtraParams* extra_params,
      blink::mojom::ColorScheme color_scheme);
};

}  // namespace content

#endif  // CONTENT_CHILD_WEBTHEMEENGINE_IMPL_MAC_H_
