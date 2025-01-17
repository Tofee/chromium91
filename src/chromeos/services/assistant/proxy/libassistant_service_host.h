// Copyright 2020 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROMEOS_SERVICES_ASSISTANT_PROXY_LIBASSISTANT_SERVICE_HOST_H_
#define CHROMEOS_SERVICES_ASSISTANT_PROXY_LIBASSISTANT_SERVICE_HOST_H_

#include <memory>

#include "chromeos/services/libassistant/public/mojom/service.mojom-forward.h"
#include "mojo/public/cpp/bindings/pending_receiver.h"

namespace chromeos {
namespace assistant {

// Interface which can be implemented to control launching and the lifetime of
// the Libassistant service. The API is losely inspired by
// ServiceProcessHost::Launch(), to make it easier to migrate to a real mojom
// service running in its own process.
class LibassistantServiceHost {
 public:
  virtual ~LibassistantServiceHost() = default;

  // Launch the mojom service. Barring crashes, the service will remain running
  // as long as both the receiver and this host class remain alive, or until
  // |Stop| is called.
  virtual void Launch(
      mojo::PendingReceiver<chromeos::libassistant::mojom::LibassistantService>
          receiver) = 0;

  // Stop the mojom service.
  virtual void Stop() = 0;
};

}  // namespace assistant
}  // namespace chromeos

#endif  // CHROMEOS_SERVICES_ASSISTANT_PROXY_LIBASSISTANT_SERVICE_HOST_H_
