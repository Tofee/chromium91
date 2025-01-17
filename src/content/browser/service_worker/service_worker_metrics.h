// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CONTENT_BROWSER_SERVICE_WORKER_SERVICE_WORKER_METRICS_H_
#define CONTENT_BROWSER_SERVICE_WORKER_SERVICE_WORKER_METRICS_H_

#include <stddef.h>
#include <map>
#include <set>

#include "base/macros.h"
#include "base/time/time.h"
#include "content/public/browser/service_worker_context.h"
#include "services/network/public/mojom/fetch_api.mojom.h"
#include "third_party/blink/public/common/service_worker/service_worker_status_code.h"
#include "third_party/blink/public/mojom/service_worker/embedded_worker.mojom.h"
#include "ui/base/page_transition_types.h"

class GURL;

namespace content {

class ServiceWorkerMetrics {
 public:
  // Used for UMA. Append-only.
  enum ReadResponseResult {
    READ_OK,
    READ_HEADERS_ERROR,
    READ_DATA_ERROR,
    NUM_READ_RESPONSE_RESULT_TYPES,
  };

  // Used for UMA. Append-only.
  enum WriteResponseResult {
    WRITE_OK,
    WRITE_HEADERS_ERROR,
    WRITE_DATA_ERROR,
    NUM_WRITE_RESPONSE_RESULT_TYPES,
  };

  // Used for UMA. Append-only.
  enum class StopStatus {
    NORMAL,
    DETACH_BY_REGISTRY,
    TIMEOUT,
    // Add new types here.
    kMaxValue = TIMEOUT,
  };

  // Used for UMA. Append-only.
  // This class is used to indicate which event is fired/finished. Most events
  // have only one request that starts the event and one response that finishes
  // the event, but the fetch event has two responses, so there are two types of
  // EventType to break down the measurement into two: FETCH and
  // FETCH_WAITUNTIL. Moreover, FETCH is separated into the four: MAIN_FRAME,
  // SUB_FRAME, SHARED_WORKER and SUB_RESOURCE for more detailed UMA.
  enum class EventType {
    ACTIVATE = 0,
    INSTALL = 1,
    // FETCH = 2,  // Obsolete
    SYNC = 3,
    NOTIFICATION_CLICK = 4,
    PUSH = 5,
    // GEOFENCING = 6,  // Obsolete
    // SERVICE_PORT_CONNECT = 7,  // Obsolete
    MESSAGE = 8,
    NOTIFICATION_CLOSE = 9,
    FETCH_MAIN_FRAME = 10,
    FETCH_SUB_FRAME = 11,
    FETCH_SHARED_WORKER = 12,
    FETCH_SUB_RESOURCE = 13,
    UNKNOWN = 14,  // Used when event type is not known.
    // FOREIGN_FETCH = 15,  // Obsolete
    FETCH_WAITUNTIL = 16,
    // FOREIGN_FETCH_WAITUNTIL = 17,  // Obsolete
    // NAVIGATION_HINT_LINK_MOUSE_DOWN = 18,  // Obsolete
    // NAVIGATION_HINT_LINK_TAP_UNCONFIRMED = 19,  // Obsolete
    // NAVIGATION_HINT_LINK_TAP_DOWN = 20,  // Obsolete
    // Used when external consumers want to add a request to
    // ServiceWorkerVersion to keep it alive.
    EXTERNAL_REQUEST = 21,
    PAYMENT_REQUEST = 22,
    BACKGROUND_FETCH_ABORT = 23,
    BACKGROUND_FETCH_CLICK = 24,
    BACKGROUND_FETCH_FAIL = 25,
    // BACKGROUND_FETCHED = 26,  // Obsolete
    NAVIGATION_HINT = 27,
    CAN_MAKE_PAYMENT = 28,
    ABORT_PAYMENT = 29,
    COOKIE_CHANGE = 30,
    // LONG_RUNNING_MESSAGE = 31, // Obsolete
    BACKGROUND_FETCH_SUCCESS = 32,
    PERIODIC_SYNC = 33,
    CONTENT_DELETE = 34,
    PUSH_SUBSCRIPTION_CHANGE = 35,
    // Add new events to record here.
    kMaxValue = PUSH_SUBSCRIPTION_CHANGE,
  };

  // Used for UMA. Append only.
  enum class Site {
    OTHER,  // Obsolete for UMA. Use WITH_FETCH_HANDLER or
            // WITHOUT_FETCH_HANDLER.
    NEW_TAB_PAGE,
    WITH_FETCH_HANDLER,
    WITHOUT_FETCH_HANDLER,
    PLUS,
    INBOX,
    DOCS,
    kMaxValue = DOCS,
  };

  // Not used for UMA.
  enum class StartSituation {
    // Failed to allocate a process.
    UNKNOWN,
    // The service worker started up during browser startup.
    DURING_STARTUP,
    // The service worker started up in a new process.
    NEW_PROCESS,
    // The service worker started up in an existing unready process. (Ex: The
    // process was created for the navigation but the IPC connection is not
    // established yet.)
    EXISTING_UNREADY_PROCESS,
    // The service worker started up in an existing ready process.
    EXISTING_READY_PROCESS
  };

  // Used for UMA. Append only.
  // Describes the outcome of a time measurement taken between processes.
  enum class CrossProcessTimeDelta {
    NORMAL,
    NEGATIVE,
    INACCURATE_CLOCK,
    // Add new types here.
    kMaxValue = INACCURATE_CLOCK,
  };

  // These are prefixed with "local" or "remote" to indicate whether the browser
  // process or renderer process recorded the timing (browser is local).
  struct StartTimes {
    // The browser started the service worker startup sequence.
    base::TimeTicks local_start;

    // The browser sent the start worker IPC to the renderer.
    base::TimeTicks local_start_worker_sent;

    // The renderer received the start worker IPC.
    base::TimeTicks remote_start_worker_received;

    // The renderer started script evaluation on the worker thread.
    base::TimeTicks remote_script_evaluation_start;

    // The renderer finished script evaluation on the worker thread.
    base::TimeTicks remote_script_evaluation_end;

    // The browser received the worker started IPC.
    base::TimeTicks local_end;
  };

  // Used for UMA. Append-only.
  enum class OfflineCapableReason {
    kTimeout = 0,
    kSuccess = 1,
    kRedirect = 2,
    kMaxValue = kRedirect,
  };

  // Converts an event type to a string. Used for tracing.
  static const char* EventTypeToString(EventType event_type);

  // Converts a start situation to a string. Used for tracing.
  static const char* StartSituationToString(StartSituation start_situation);

  // If the |url| is not a special site, returns Site::OTHER.
  static Site SiteFromURL(const GURL& url);

  // Counts the result of reading a service worker script from storage.
  static void CountReadResponseResult(ReadResponseResult result);
  // Counts the result of writing a service worker script to storage.
  static void CountWriteResponseResult(WriteResponseResult result);

  // Counts the number of page loads controlled by a Service Worker.
  static void CountControlledPageLoad(Site site,
                                      bool is_main_frame_load);

  // Records the result of trying to start an installed worker.
  static void RecordStartInstalledWorkerStatus(
      blink::ServiceWorkerStatusCode status,
      EventType purpose);

  // Records the time taken to successfully start a worker. |is_installed|
  // indicates whether the version has been installed.
  //
  // TODO(crbug.com/855952): Replace this with RecordStartWorkerTiming().
  static void RecordStartWorkerTime(base::TimeDelta time,
                                    bool is_installed,
                                    StartSituation start_situation,
                                    EventType purpose);

  // Records the result of trying to stop a worker.
  static void RecordWorkerStopped(StopStatus status);

  // Records the time taken to successfully stop a worker.
  static void RecordStopWorkerTime(base::TimeDelta time);

  static void RecordActivateEventStatus(blink::ServiceWorkerStatusCode status,
                                        bool is_shutdown);
  static void RecordInstallEventStatus(blink::ServiceWorkerStatusCode status,
                                       uint32_t fetch_count);

  // Records the amount of time spent handling an event.
  static void RecordEventDuration(EventType event,
                                  base::TimeDelta time,
                                  bool was_handled,
                                  uint32_t fetch_count);

  // Records the result of dispatching a fetch event to a service worker.
  static void RecordFetchEventStatus(bool is_main_resource,
                                     blink::ServiceWorkerStatusCode status);

  CONTENT_EXPORT static void RecordStartWorkerTiming(const StartTimes& times,
                                                     StartSituation situation);
  static void RecordStartWorkerTimingClockConsistency(
      CrossProcessTimeDelta type);

  // Records the result of a start attempt that occurred after the worker had
  // failed |failure_count| consecutive times.
  static void RecordStartStatusAfterFailure(
      int failure_count,
      blink::ServiceWorkerStatusCode status);

  // Records the size of Service-Worker-Navigation-Preload header when the
  // navigation preload request is to be sent.
  static void RecordNavigationPreloadRequestHeaderSize(size_t size);

  static void RecordRuntime(base::TimeDelta time);

  // Records the result of starting service worker for a navigation hint.
  static void RecordStartServiceWorkerForNavigationHintResult(
      StartServiceWorkerForNavigationHintResult result);

  // Records the duration of looking up an existing registration.
  // |status| is the result of lookup. The records for the cases where
  // the registration is found (kOk), not found (kErrorNotFound), or an error
  // happens (other errors) are saved separately into a relevant suffixed
  // histogram.
  static void RecordLookupRegistrationTime(
      blink::ServiceWorkerStatusCode status,
      base::TimeDelta duration);

  // Records the reason a service worker was deemed to be offline capable. The
  // reason may be that the service worker responded with 2xx..., 3xx..., or the
  // check timed out.
  static void RecordOfflineCapableReason(blink::ServiceWorkerStatusCode status,
                                         int status_code);

 private:
  DISALLOW_IMPLICIT_CONSTRUCTORS(ServiceWorkerMetrics);
};

}  // namespace content

#endif  // CONTENT_BROWSER_SERVICE_WORKER_SERVICE_WORKER_METRICS_H_
