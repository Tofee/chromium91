// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROMEOS_NETWORK_NETWORK_DEVICE_HANDLER_IMPL_H_
#define CHROMEOS_NETWORK_NETWORK_DEVICE_HANDLER_IMPL_H_

#include <string>
#include <unordered_set>
#include <vector>

#include "base/callback.h"
#include "base/compiler_specific.h"
#include "base/component_export.h"
#include "base/macros.h"
#include "base/memory/weak_ptr.h"
#include "chromeos/network/network_device_handler.h"
#include "chromeos/network/network_handler.h"
#include "chromeos/network/network_handler_callbacks.h"
#include "chromeos/network/network_state_handler_observer.h"

namespace chromeos {

class NetworkStateHandler;

class COMPONENT_EXPORT(CHROMEOS_NETWORK) NetworkDeviceHandlerImpl
    : public NetworkDeviceHandler,
      public NetworkStateHandlerObserver {
 public:
  ~NetworkDeviceHandlerImpl() override;

  // NetworkDeviceHandler overrides
  void GetDeviceProperties(
      const std::string& device_path,
      network_handler::ResultCallback callback) const override;

  void SetDeviceProperty(
      const std::string& device_path,
      const std::string& property_name,
      const base::Value& value,
      base::OnceClosure callback,
      network_handler::ErrorCallback error_callback) override;

  void RegisterCellularNetwork(
      const std::string& device_path,
      const std::string& network_id,
      base::OnceClosure callback,
      network_handler::ErrorCallback error_callback) override;

  void RequirePin(const std::string& device_path,
                  bool require_pin,
                  const std::string& pin,
                  base::OnceClosure callback,
                  network_handler::ErrorCallback error_callback) override;

  void EnterPin(const std::string& device_path,
                const std::string& pin,
                base::OnceClosure callback,
                network_handler::ErrorCallback error_callback) override;

  void UnblockPin(const std::string& device_path,
                  const std::string& puk,
                  const std::string& new_pin,
                  base::OnceClosure callback,
                  network_handler::ErrorCallback error_callback) override;

  void ChangePin(const std::string& device_path,
                 const std::string& old_pin,
                 const std::string& new_pin,
                 base::OnceClosure callback,
                 network_handler::ErrorCallback error_callback) override;

  void SetCellularAllowRoaming(bool allow_roaming) override;

  void SetMACAddressRandomizationEnabled(bool enabled) override;

  void SetUsbEthernetMacAddressSource(const std::string& source) override;

  void AddWifiWakeOnPacketConnection(
      const net::IPEndPoint& ip_endpoint,
      base::OnceClosure callback,
      network_handler::ErrorCallback error_callback) override;

  void AddWifiWakeOnPacketOfTypes(
      const std::vector<std::string>& types,
      base::OnceClosure callback,
      network_handler::ErrorCallback error_callback) override;

  void RemoveWifiWakeOnPacketConnection(
      const net::IPEndPoint& ip_endpoint,
      base::OnceClosure callback,
      network_handler::ErrorCallback error_callback) override;

  void RemoveWifiWakeOnPacketOfTypes(
      const std::vector<std::string>& types,
      base::OnceClosure callback,
      network_handler::ErrorCallback error_callback) override;

  void RemoveAllWifiWakeOnPacketConnections(
      base::OnceClosure callback,
      network_handler::ErrorCallback error_callback) override;

  // NetworkStateHandlerObserver overrides
  void DeviceListChanged() override;
  void DevicePropertiesUpdated(const DeviceState* device) override;

 private:
  friend class NetworkHandler;
  friend class NetworkDeviceHandler;
  friend class NetworkDeviceHandlerTest;

  // Some WiFi feature enablement needs to check supported property before
  // setting. e.g. MAC address randomization, wake on WiFi.
  // When there's no Wi-Fi device or there is one but we haven't asked if
  // the feature is supported yet, the value of the member, e.g.
  // |mac_addr_randomizaton_supported_|, will be |NOT_REQUESTED|. When we
  // try to apply the value e.g. |mac_addr_randomization_enabled_|, we will
  // check whether it is supported and change to one of the other two
  // values.
  enum class WifiFeatureSupport { NOT_REQUESTED, SUPPORTED, UNSUPPORTED };

  NetworkDeviceHandlerImpl();

  void Init(NetworkStateHandler* network_state_handler);

  // Applies the current value of |cellular_allow_roaming_| to all existing
  // cellular devices of Shill.
  void ApplyCellularAllowRoamingToShill();

  // Applies the current value of |mac_addr_randomization_enabled_| to wifi
  // devices.
  void ApplyMACAddressRandomizationToShill();

  // Applies the wake-on-wifi-allowed feature flag to WiFi devices.
  void ApplyWakeOnWifiAllowedToShill();

  // Applies the current value of |usb_ethernet_mac_address_source_| to primary
  // enabled USB Ethernet device. Does nothing if MAC address source is not
  // specified yet.
  void ApplyUsbEthernetMacAddressSourceToShill();

  // Applies the current value of the |cellular-use-attach-apn| flag to all
  // existing cellular devices of Shill.
  void ApplyUseAttachApnToShill();

  // Utility function for applying enabled setting of WiFi features that needs
  // to check if the feature is supported first.
  // This function will update |supported| if it is still NOT_REQUESTED by
  // getting |support_property_name| property of the WiFi device. Then, if it
  // is supported, set |enable_property_name| property of the WiFi device to
  // |enabled|.
  void ApplyWifiFeatureToShillIfSupported(std::string enable_property_name,
                                          bool enabled,
                                          std::string support_property_name,
                                          WifiFeatureSupport* supported);

  // Callback function used by ApplyWifiFeatureToShillIfSupported to get shill
  // property when the supported property is NOT_REQUESTED. It will extract
  // |support_property_name| of GetProperties response and update
  // |feature_support_to_set|, then call ApplyWifiFeatureToShillIfSupported
  // again if the feature is supported.
  void HandleWifiFeatureSupportedProperty(
      std::string enable_property_name,
      bool enabled,
      std::string support_property_name,
      WifiFeatureSupport* feature_support_to_set,
      const std::string& device_path,
      base::Optional<base::Value> properties);

  // Callback to be called on MAC address source change request failure.
  // The request was called on device with |device_path| path and
  // |device_mac_address| MAC address to change MAC address source to the new
  // |mac_address_source| value.
  void OnSetUsbEthernetMacAddressSourceError(
      const std::string& device_path,
      const std::string& device_mac_address,
      const std::string& mac_address_source,
      network_handler::ErrorCallback error_callback,
      const std::string& shill_error_name,
      const std::string& shill_error_message);

  // Checks whether Device is enabled USB Ethernet adapter.
  bool IsUsbEnabledDevice(const DeviceState* device_state) const;

  // Updates the primary enabled USB Ethernet device path.
  void UpdatePrimaryEnabledUsbEthernetDevice();

  // Resets MAC address source property for secondary USB Ethernet devices.
  void ResetMacAddressSourceForSecondaryUsbEthernetDevices() const;

  // Get the DeviceState for the wifi device, if any.
  const DeviceState* GetWifiDeviceState();

  NetworkStateHandler* network_state_handler_ = nullptr;
  bool cellular_allow_roaming_ = false;
  WifiFeatureSupport mac_addr_randomization_supported_ =
      WifiFeatureSupport::NOT_REQUESTED;
  bool mac_addr_randomization_enabled_ = false;
  WifiFeatureSupport wake_on_wifi_supported_ =
      WifiFeatureSupport::NOT_REQUESTED;
  bool wake_on_wifi_allowed_ = false;

  std::string usb_ethernet_mac_address_source_;
  std::string primary_enabled_usb_ethernet_device_path_;
  // Set of device's MAC addresses that do not support MAC address source change
  // to |usb_ethernet_mac_address_source_|. Use MAC address as unique device
  // identifier, because link name can change.
  std::unordered_set<std::string> mac_address_change_not_supported_;

  base::WeakPtrFactory<NetworkDeviceHandlerImpl> weak_ptr_factory_{this};

  DISALLOW_COPY_AND_ASSIGN(NetworkDeviceHandlerImpl);
};

}  // namespace chromeos

#endif  // CHROMEOS_NETWORK_NETWORK_DEVICE_HANDLER_IMPL_H_
