// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/permissions/permission_util.h"

#include "base/notreached.h"
#include "build/build_config.h"
#include "build/chromeos_buildflags.h"
#include "content/public/browser/permission_type.h"

using content::PermissionType;

namespace permissions {

// The returned strings must match any Field Trial configs for the Permissions
// kill switch e.g. Permissions.Action.Geolocation etc..
std::string PermissionUtil::GetPermissionString(
    ContentSettingsType content_type) {
  switch (content_type) {
    case ContentSettingsType::GEOLOCATION:
      return "Geolocation";
    case ContentSettingsType::NOTIFICATIONS:
      return "Notifications";
    case ContentSettingsType::MIDI_SYSEX:
      return "MidiSysEx";
    case ContentSettingsType::DURABLE_STORAGE:
      return "DurableStorage";
    case ContentSettingsType::PROTECTED_MEDIA_IDENTIFIER:
      return "ProtectedMediaIdentifier";
    case ContentSettingsType::MEDIASTREAM_MIC:
      return "AudioCapture";
    case ContentSettingsType::MEDIASTREAM_CAMERA:
      return "VideoCapture";
    case ContentSettingsType::MIDI:
      return "Midi";
    case ContentSettingsType::BACKGROUND_SYNC:
      return "BackgroundSync";
    case ContentSettingsType::SENSORS:
      return "Sensors";
    case ContentSettingsType::ACCESSIBILITY_EVENTS:
      return "AccessibilityEvents";
    case ContentSettingsType::CLIPBOARD_READ_WRITE:
      return "ClipboardReadWrite";
    case ContentSettingsType::CLIPBOARD_SANITIZED_WRITE:
      return "ClipboardSanitizedWrite";
    case ContentSettingsType::PAYMENT_HANDLER:
      return "PaymentHandler";
    case ContentSettingsType::BACKGROUND_FETCH:
      return "BackgroundFetch";
    case ContentSettingsType::IDLE_DETECTION:
      return "IdleDetection";
    case ContentSettingsType::PERIODIC_BACKGROUND_SYNC:
      return "PeriodicBackgroundSync";
    case ContentSettingsType::WAKE_LOCK_SCREEN:
      return "WakeLockScreen";
    case ContentSettingsType::WAKE_LOCK_SYSTEM:
      return "WakeLockSystem";
    case ContentSettingsType::NFC:
      return "NFC";
    case ContentSettingsType::VR:
      return "VR";
    case ContentSettingsType::AR:
      return "AR";
    case ContentSettingsType::STORAGE_ACCESS:
      return "StorageAccess";
    case ContentSettingsType::CAMERA_PAN_TILT_ZOOM:
      return "CameraPanTiltZoom";
    case ContentSettingsType::WINDOW_PLACEMENT:
      return "WindowPlacement";
    case ContentSettingsType::FONT_ACCESS:
      return "FontAccess";
    case ContentSettingsType::FILE_HANDLING:
      return "FileHandling";
    case ContentSettingsType::DISPLAY_CAPTURE:
      return "DisplayCapture";
    default:
      break;
  }
  NOTREACHED();
  return std::string();
}

PermissionRequestGestureType PermissionUtil::GetGestureType(bool user_gesture) {
  return user_gesture ? PermissionRequestGestureType::GESTURE
                      : PermissionRequestGestureType::NO_GESTURE;
}

bool PermissionUtil::GetPermissionType(ContentSettingsType type,
                                       PermissionType* out) {
  if (type == ContentSettingsType::GEOLOCATION) {
    *out = PermissionType::GEOLOCATION;
  } else if (type == ContentSettingsType::NOTIFICATIONS) {
    *out = PermissionType::NOTIFICATIONS;
  } else if (type == ContentSettingsType::MIDI) {
    *out = PermissionType::MIDI;
  } else if (type == ContentSettingsType::MIDI_SYSEX) {
    *out = PermissionType::MIDI_SYSEX;
  } else if (type == ContentSettingsType::DURABLE_STORAGE) {
    *out = PermissionType::DURABLE_STORAGE;
  } else if (type == ContentSettingsType::MEDIASTREAM_CAMERA) {
    *out = PermissionType::VIDEO_CAPTURE;
  } else if (type == ContentSettingsType::MEDIASTREAM_MIC) {
    *out = PermissionType::AUDIO_CAPTURE;
  } else if (type == ContentSettingsType::BACKGROUND_SYNC) {
    *out = PermissionType::BACKGROUND_SYNC;
#if defined(OS_ANDROID) || BUILDFLAG(IS_CHROMEOS_ASH)
  } else if (type == ContentSettingsType::PROTECTED_MEDIA_IDENTIFIER) {
    *out = PermissionType::PROTECTED_MEDIA_IDENTIFIER;
#endif
  } else if (type == ContentSettingsType::SENSORS) {
    *out = PermissionType::SENSORS;
  } else if (type == ContentSettingsType::ACCESSIBILITY_EVENTS) {
    *out = PermissionType::ACCESSIBILITY_EVENTS;
  } else if (type == ContentSettingsType::CLIPBOARD_READ_WRITE) {
    *out = PermissionType::CLIPBOARD_READ_WRITE;
  } else if (type == ContentSettingsType::PAYMENT_HANDLER) {
    *out = PermissionType::PAYMENT_HANDLER;
  } else if (type == ContentSettingsType::BACKGROUND_FETCH) {
    *out = PermissionType::BACKGROUND_FETCH;
  } else if (type == ContentSettingsType::PERIODIC_BACKGROUND_SYNC) {
    *out = PermissionType::PERIODIC_BACKGROUND_SYNC;
  } else if (type == ContentSettingsType::WAKE_LOCK_SCREEN) {
    *out = PermissionType::WAKE_LOCK_SCREEN;
  } else if (type == ContentSettingsType::WAKE_LOCK_SYSTEM) {
    *out = PermissionType::WAKE_LOCK_SYSTEM;
  } else if (type == ContentSettingsType::NFC) {
    *out = PermissionType::NFC;
  } else if (type == ContentSettingsType::VR) {
    *out = PermissionType::VR;
  } else if (type == ContentSettingsType::AR) {
    *out = PermissionType::AR;
  } else if (type == ContentSettingsType::STORAGE_ACCESS) {
    *out = PermissionType::STORAGE_ACCESS_GRANT;
  } else if (type == ContentSettingsType::CAMERA_PAN_TILT_ZOOM) {
    *out = PermissionType::CAMERA_PAN_TILT_ZOOM;
  } else if (type == ContentSettingsType::WINDOW_PLACEMENT) {
    *out = PermissionType::WINDOW_PLACEMENT;
  } else if (type == ContentSettingsType::FONT_ACCESS) {
    *out = PermissionType::FONT_ACCESS;
  } else if (type == ContentSettingsType::IDLE_DETECTION) {
    *out = PermissionType::IDLE_DETECTION;
  } else if (type == ContentSettingsType::DISPLAY_CAPTURE) {
    *out = PermissionType::DISPLAY_CAPTURE;
  } else if (type == ContentSettingsType::FILE_HANDLING) {
    *out = PermissionType::FILE_HANDLING;
  } else {
    return false;
  }
  return true;
}

bool PermissionUtil::IsPermission(ContentSettingsType type) {
  switch (type) {
    case ContentSettingsType::GEOLOCATION:
    case ContentSettingsType::NOTIFICATIONS:
    case ContentSettingsType::MIDI:
    case ContentSettingsType::MIDI_SYSEX:
    case ContentSettingsType::DURABLE_STORAGE:
    case ContentSettingsType::MEDIASTREAM_CAMERA:
    case ContentSettingsType::MEDIASTREAM_MIC:
    case ContentSettingsType::BACKGROUND_SYNC:
#if defined(OS_ANDROID) || BUILDFLAG(IS_CHROMEOS_ASH)
    case ContentSettingsType::PROTECTED_MEDIA_IDENTIFIER:
#endif
    case ContentSettingsType::SENSORS:
    case ContentSettingsType::ACCESSIBILITY_EVENTS:
    case ContentSettingsType::CLIPBOARD_READ_WRITE:
    case ContentSettingsType::PAYMENT_HANDLER:
    case ContentSettingsType::BACKGROUND_FETCH:
    case ContentSettingsType::PERIODIC_BACKGROUND_SYNC:
    case ContentSettingsType::WAKE_LOCK_SCREEN:
    case ContentSettingsType::WAKE_LOCK_SYSTEM:
    case ContentSettingsType::NFC:
    case ContentSettingsType::VR:
    case ContentSettingsType::AR:
    case ContentSettingsType::STORAGE_ACCESS:
    case ContentSettingsType::CAMERA_PAN_TILT_ZOOM:
    case ContentSettingsType::WINDOW_PLACEMENT:
    case ContentSettingsType::FONT_ACCESS:
    case ContentSettingsType::IDLE_DETECTION:
    case ContentSettingsType::DISPLAY_CAPTURE:
    case ContentSettingsType::FILE_HANDLING:
      return true;
    default:
      return false;
  }
}

}  // namespace permissions
