// Copyright 2020 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROMEOS_SERVICES_CELLULAR_SETUP_ESIM_MANAGER_H_
#define CHROMEOS_SERVICES_CELLULAR_SETUP_ESIM_MANAGER_H_

#include "base/memory/weak_ptr.h"
#include "chromeos/dbus/hermes/hermes_euicc_client.h"
#include "chromeos/dbus/hermes/hermes_manager_client.h"
#include "chromeos/dbus/hermes/hermes_profile_client.h"
#include "chromeos/network/cellular_esim_profile_handler.h"
#include "chromeos/services/cellular_setup/public/mojom/esim_manager.mojom.h"
#include "mojo/public/cpp/bindings/pending_remote.h"
#include "mojo/public/cpp/bindings/receiver_set.h"
#include "mojo/public/cpp/bindings/remote_set.h"

namespace dbus {
class ObjectPath;
}  // namespace dbus

namespace chromeos {

class CellularConnectionHandler;
class CellularESimUninstallHandler;
class CellularInhibitor;
class NetworkConnectionHandler;
class NetworkStateHandler;

namespace cellular_setup {

class Euicc;
class ESimProfile;

// Implementation of mojom::ESimManager. This class uses the Hermes
// DBus clients to communicate with the Hermes daemon and provide
// eSIM management methods. ESimManager mojo interface is provided
// in WebUI for cellular settings and eSIM setup.
class ESimManager : public mojom::ESimManager,
                    CellularESimProfileHandler::Observer,
                    HermesManagerClient::Observer,
                    HermesEuiccClient::Observer {
 public:
  ESimManager();
  ESimManager(CellularConnectionHandler* cellular_connection_handler,
              CellularESimProfileHandler* cellular_esim_profile_handler,
              CellularESimUninstallHandler* cellular_esim_uninstall_handler,
              CellularInhibitor* cellular_inhibitor,
              NetworkConnectionHandler* network_connection_handler,
              NetworkStateHandler* network_state_handler);
  ESimManager(const ESimManager&) = delete;
  ESimManager& operator=(const ESimManager&) = delete;
  ~ESimManager() override;

  // mojom::ESimManager
  void AddObserver(
      mojo::PendingRemote<mojom::ESimManagerObserver> observer) override;
  void GetAvailableEuiccs(GetAvailableEuiccsCallback callback) override;

  // HermesManagerClient::Observer:
  void OnAvailableEuiccListChanged() override;

  // HermesEuiccClient::Observer:
  void OnEuiccPropertyChanged(const dbus::ObjectPath& euicc_path,
                              const std::string& property_name) override;

  // CellularESimProfileHandler::Observer:
  void OnESimProfileListUpdated() override;

  // Binds receiver to this instance.
  void BindReceiver(mojo::PendingReceiver<mojom::ESimManager> receiver);

  // Notifies observers of changes to ESimProfiles.
  void NotifyESimProfileChanged(ESimProfile* esim_profile);

  // Notifies observers of changes to ESimProfile Lists.
  void NotifyESimProfileListChanged(Euicc* euicc);

  CellularESimProfileHandler* cellular_esim_profile_handler() {
    return cellular_esim_profile_handler_;
  }

  CellularConnectionHandler* cellular_connection_handler() {
    return cellular_connection_handler_;
  }

  CellularESimUninstallHandler* cellular_esim_uninstall_handler() {
    return cellular_esim_uninstall_handler_;
  }

  CellularInhibitor* cellular_inhibitor() { return cellular_inhibitor_; }

  NetworkConnectionHandler* network_connection_handler() {
    return network_connection_handler_;
  }

  NetworkStateHandler* network_state_handler() {
    return network_state_handler_;
  }

 private:
  void UpdateAvailableEuiccs();
  // Removes Euicc objects in |available_euiiccs_| that are not in
  // |new_euicc_paths|. Returns true if any euicc objects were removed.
  bool RemoveUntrackedEuiccs(const std::set<dbus::ObjectPath> new_euicc_paths);
  Euicc* GetEuiccFromPath(const dbus::ObjectPath& path);
  // Creates a new Euicc object in |available_euiccs_| if it doesn't already
  // exist. Returns true if a new object was created.
  bool CreateEuiccIfNew(const dbus::ObjectPath& euicc_path);

  CellularConnectionHandler* cellular_connection_handler_;
  CellularESimProfileHandler* cellular_esim_profile_handler_;
  CellularESimUninstallHandler* cellular_esim_uninstall_handler_;
  CellularInhibitor* cellular_inhibitor_;

  NetworkConnectionHandler* network_connection_handler_;
  NetworkStateHandler* network_state_handler_;

  std::vector<std::unique_ptr<Euicc>> available_euiccs_;
  mojo::RemoteSet<mojom::ESimManagerObserver> observers_;
  mojo::ReceiverSet<mojom::ESimManager> receivers_;

  base::WeakPtrFactory<ESimManager> weak_ptr_factory_{this};
};

}  // namespace cellular_setup
}  // namespace chromeos

#endif  // CHROMEOS_SERVICES_CELLULAR_SETUP_ESIM_MANAGER_H_
