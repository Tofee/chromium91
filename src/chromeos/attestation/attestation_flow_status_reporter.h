// Copyright 2021 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROMEOS_ATTESTATION_ATTESTATION_FLOW_STATUS_REPORTER_H_
#define CHROMEOS_ATTESTATION_ATTESTATION_FLOW_STATUS_REPORTER_H_

#include "base/component_export.h"
#include "base/optional.h"

namespace chromeos {
namespace attestation {

// This class is used to record various attributes and execution results of an
// adaptive attestation flow instance.
class COMPONENT_EXPORT(CHROMEOS_ATTESTATION) AttestationFlowStatusReporter {
 public:
  AttestationFlowStatusReporter();
  ~AttestationFlowStatusReporter();

  // Not copyable or movable.
  AttestationFlowStatusReporter(const AttestationFlowStatusReporter&) = delete;
  AttestationFlowStatusReporter& operator=(
      const AttestationFlowStatusReporter&) = delete;
  AttestationFlowStatusReporter(AttestationFlowStatusReporter&&) = delete;
  AttestationFlowStatusReporter& operator=(
      const AttestationFlowStatusReporter&&) = delete;

  // Called when there is a proxy used to communicate with CA server.
  void OnHasProxy(bool has_proxy);
  // Called when the system proxy is available.
  void OnIsSystemProxyAvailable(bool is_system_proxy_available);
  // Called with the status returned by the default attestation flow.
  void OnDefaultFlowStatus(bool success);
  // Called with the status returned by the fallback attestation flow.
  void OnFallbackFlowStatus(bool success);

 private:
  // Encode the recorded parameters into a UMA entry and report it.
  void Report();
  // The flag that is set by `OnHasProxy()`.
  base::Optional<bool> has_proxy_;
  // The flag that is set by `OnIsSystemProxyAvailable()`.
  base::Optional<bool> is_system_proxy_available_;
  // The flag that is set/unset by `OnDefaultFlowStatus()`.
  base::Optional<bool> does_default_flow_succeed_;
  // The flag that is set/unset by `OnFallbackFlowStatus()`.
  base::Optional<bool> does_fallback_flow_succeed_;
};

}  // namespace attestation
}  // namespace chromeos

#endif  // CHROMEOS_ATTESTATION_ATTESTATION_FLOW_STATUS_REPORTER_H_
