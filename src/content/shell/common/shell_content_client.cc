// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "content/shell/common/shell_content_client.h"

#include "base/command_line.h"
#include "base/strings/string_piece.h"
#include "base/strings/utf_string_conversions.h"
#include "base/values.h"
#include "build/build_config.h"
#include "content/app/resources/grit/content_resources.h"
#include "content/public/common/content_switches.h"
#include "content/shell/common/shell_switches.h"
#include "content/shell/grit/shell_resources.h"
#include "third_party/blink/public/strings/grit/blink_strings.h"
#include "ui/base/l10n/l10n_util.h"
#include "ui/base/resource/resource_bundle.h"

namespace content {

ShellContentClient::ShellContentClient() {}

ShellContentClient::~ShellContentClient() {}

std::u16string ShellContentClient::GetLocalizedString(int message_id) {
  if (switches::IsRunWebTestsSwitchPresent()) {
    switch (message_id) {
      case IDS_FORM_OTHER_DATE_LABEL:
        return u"<<OtherDateLabel>>";
      case IDS_FORM_OTHER_MONTH_LABEL:
        return u"<<OtherMonthLabel>>";
      case IDS_FORM_OTHER_WEEK_LABEL:
        return u"<<OtherWeekLabel>>";
      case IDS_FORM_CALENDAR_CLEAR:
        return u"<<CalendarClear>>";
      case IDS_FORM_CALENDAR_TODAY:
        return u"<<CalendarToday>>";
      case IDS_FORM_THIS_MONTH_LABEL:
        return u"<<ThisMonthLabel>>";
      case IDS_FORM_THIS_WEEK_LABEL:
        return u"<<ThisWeekLabel>>";
    }
  }
  return l10n_util::GetStringUTF16(message_id);
}

base::StringPiece ShellContentClient::GetDataResource(
    int resource_id,
    ui::ScaleFactor scale_factor) {
  return ui::ResourceBundle::GetSharedInstance().GetRawDataResourceForScale(
      resource_id, scale_factor);
}

base::RefCountedMemory* ShellContentClient::GetDataResourceBytes(
    int resource_id) {
  return ui::ResourceBundle::GetSharedInstance().LoadDataResourceBytes(
      resource_id);
}

gfx::Image& ShellContentClient::GetNativeImageNamed(int resource_id) {
  return ui::ResourceBundle::GetSharedInstance().GetNativeImageNamed(
      resource_id);
}

blink::OriginTrialPolicy* ShellContentClient::GetOriginTrialPolicy() {
  return &origin_trial_policy_;
}

void ShellContentClient::AddAdditionalSchemes(Schemes* schemes) {
#if defined(OS_ANDROID)
  schemes->local_schemes.push_back(url::kContentScheme);
#endif
}

}  // namespace content
