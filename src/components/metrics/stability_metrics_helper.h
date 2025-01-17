// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef COMPONENTS_METRICS_STABILITY_METRICS_HELPER_H_
#define COMPONENTS_METRICS_STABILITY_METRICS_HELPER_H_

#include <string>

#include "base/macros.h"
#include "base/process/kill.h"

class PrefRegistrySimple;
class PrefService;

namespace metrics {

// The values here correspond to values in the Stability message in
// system_profile.proto.
// This must stay 1-1 with the StabilityEventType enum in enums.xml.
enum class StabilityEventType {
  kPageLoad = 2,
  kRendererCrash = 3,
  kRendererHang = 4,
  kExtensionCrash = 5,
  kChildProcessCrash = 6,
  kLaunch = 15,
  kBrowserCrash = 16,
  kIncompleteShutdown = 17,
  kRendererFailedLaunch = 24,
  kExtensionRendererFailedLaunch = 25,
  kRendererLaunch = 26,
  kExtensionRendererLaunch = 27,
  kGpuCrash = 31,
  kMaxValue = kGpuCrash
};

class SystemProfileProto;

// StabilityMetricsHelper is a class that providers functionality common to
// different embedders' stability metrics providers.
class StabilityMetricsHelper {
 public:
  explicit StabilityMetricsHelper(PrefService* local_state);
  ~StabilityMetricsHelper();

  // Provides stability metrics.
  void ProvideStabilityMetrics(SystemProfileProto* system_profile_proto);

  // Clears the gathered stability metrics.
  void ClearSavedStabilityMetrics();

  // Records a utility process launch with name |metrics_name|.
  void BrowserUtilityProcessLaunched(const std::string& metrics_name);

  // Records a utility process crash with name |metrics_name|.
  void BrowserUtilityProcessCrashed(const std::string& metrics_name,
                                    int exit_code);

  // Records a browser child process crash.
  void BrowserChildProcessCrashed();

  // Logs the initiation of a page load.
  void LogLoadStarted();

  // Records a renderer process crash.
  void LogRendererCrash(bool was_extension_process,
                        base::TerminationStatus status,
                        int exit_code);

  // Records that a new renderer process was successfully launched.
  void LogRendererLaunched(bool was_extension_process);

  // Records a renderer process hang.
  void LogRendererHang();

  // Registers local state prefs used by this class.
  static void RegisterPrefs(PrefRegistrySimple* registry);

  // Increments the RendererCrash pref.
  void IncreaseRendererCrashCount();

  // Increments the GpuCrash pref.
  // Note: This is currently only used on Android. If you want to call this on
  // another platform, server-side processing code needs to be updated for that
  // platform to use the new data. Server-side currently assumes Android-only.
  void IncreaseGpuCrashCount();

  // Records a histogram for the input |stability_event_type|.
  static void RecordStabilityEvent(StabilityEventType stability_event_type);

 private:
  // Increments an Integer pref value specified by |path|.
  void IncrementPrefValue(const char* path);

  // Records that a renderer launch failed.
  void LogRendererLaunchFailed(bool was_extension_process);

  PrefService* local_state_;

  DISALLOW_COPY_AND_ASSIGN(StabilityMetricsHelper);
};

}  // namespace metrics

#endif  // COMPONENTS_METRICS_STABILITY_METRICS_HELPER_H_
