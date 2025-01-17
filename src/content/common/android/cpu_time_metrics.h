// Copyright 2020 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CONTENT_COMMON_ANDROID_CPU_TIME_METRICS_H_
#define CONTENT_COMMON_ANDROID_CPU_TIME_METRICS_H_

#include "content/common/content_export.h"

namespace content {

// Sets up periodic collection/reporting of the process's CPU time. Should be
// called on the process's main thread.
//
// The current process's CPU time usage is recorded periodically and reported it
// into UMA histograms. The histogram data can later be used to approximate the
// power consumption / efficiency of the app. Currently only supports Android,
// where the sandbox allows isolated processes to read from /proc/self/stats.
CONTENT_EXPORT void SetupCpuTimeMetrics();

// Wait for any current in-progress collection of CPU time UMA metrics on the
// current thread. Beware: Should only be used for testing.
CONTENT_EXPORT void WaitForCpuTimeMetricsForTesting();

CONTENT_EXPORT void SetIgnoreHistogramAllocatorForTesting(bool);

}  // namespace content

#endif  // CONTENT_COMMON_ANDROID_CPU_TIME_METRICS_H_
