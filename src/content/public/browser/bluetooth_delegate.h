// Copyright 2020 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CONTENT_PUBLIC_BROWSER_BLUETOOTH_DELEGATE_H_
#define CONTENT_PUBLIC_BROWSER_BLUETOOTH_DELEGATE_H_

#include <string>
#include <vector>

#include "base/containers/flat_set.h"
#include "base/observer_list_types.h"
#include "content/common/content_export.h"
#include "content/public/browser/bluetooth_chooser.h"
#include "content/public/browser/bluetooth_scanning_prompt.h"
#include "third_party/blink/public/mojom/bluetooth/web_bluetooth.mojom-forward.h"
#include "url/origin.h"

namespace blink {
class WebBluetoothDeviceId;
}  // namespace blink

namespace device {
class BluetoothDevice;
class BluetoothUUID;
}  // namespace device

namespace content {

class RenderFrameHost;

// Provides an interface for managing device permissions for Web Bluetooth and
// Web Bluetooth Scanning API. An embedder may implement this to manage these
// permissions.
// TODO(https://crbug.com/1048325): There are several Bluetooth related methods
// in WebContentsDelegate and ContentBrowserClient that can be moved into this
// class.
class CONTENT_EXPORT BluetoothDelegate {
 public:
  // An observer used to track permission revocation events for a particular
  // render frame host.
  class CONTENT_EXPORT FramePermissionObserver : public base::CheckedObserver {
   public:
    // Notify observer that an object permission was revoked for |origin|.
    virtual void OnPermissionRevoked(const url::Origin& origin) = 0;

    // Returns the frame that the observer wishes to watch.
    virtual RenderFrameHost* GetRenderFrameHost() = 0;
  };
  virtual ~BluetoothDelegate() = default;

  // Shows a chooser for the user to select a nearby Bluetooth device. The
  // EventHandler should live at least as long as the returned chooser object.
  virtual std::unique_ptr<BluetoothChooser> RunBluetoothChooser(
      RenderFrameHost* frame,
      const BluetoothChooser::EventHandler& event_handler) = 0;

  // Shows a prompt for the user to allow/block Bluetooth scanning. The
  // EventHandler should live at least as long as the returned prompt object.
  virtual std::unique_ptr<BluetoothScanningPrompt> ShowBluetoothScanningPrompt(
      RenderFrameHost* frame,
      const BluetoothScanningPrompt::EventHandler& event_handler) = 0;

  // This should return the WebBluetoothDeviceId that corresponds to the device
  // with |device_address| in the current |frame|. If there is not a
  // corresponding ID, then an invalid WebBluetoothDeviceId should be returned.
  virtual blink::WebBluetoothDeviceId GetWebBluetoothDeviceId(
      RenderFrameHost* frame,
      const std::string& device_address) = 0;

  // This should return the device address corresponding to a device with
  // |device_id| in the current |frame|. If there is not a corresponding
  // address, then an empty string should be returned.
  virtual std::string GetDeviceAddress(
      RenderFrameHost* frame,
      const blink::WebBluetoothDeviceId& device_id) = 0;

  // This should return the WebBluetoothDeviceId for |device_address| if the
  // device has been assigned an ID previously through AddScannedDevice() or
  // GrantServiceAccessPermission(). If not, a new ID should be generated for
  // |device_address| and stored in a temporary map of address to ID. Service
  // access should not be granted to these devices.
  virtual blink::WebBluetoothDeviceId AddScannedDevice(
      RenderFrameHost* frame,
      const std::string& device_address) = 0;

  // This should grant permission to the requesting and embedding origins
  // represented by |frame| to connect to the device with |device_address| and
  // access its |services|. Once permission is granted, a |WebBluetoothDeviceId|
  // should be generated for the device and returned.
  virtual blink::WebBluetoothDeviceId GrantServiceAccessPermission(
      RenderFrameHost* frame,
      const device::BluetoothDevice* device,
      const blink::mojom::WebBluetoothRequestDeviceOptions* options) = 0;

  // This should return true if |frame| has been granted permission to access
  // the device with |device_id| through GrantServiceAccessPermission().
  // |device_id|s generated with AddScannedDevices() should return false.
  virtual bool HasDevicePermission(
      RenderFrameHost* frame,
      const blink::WebBluetoothDeviceId& device_id) = 0;

  // This should return true if |frame| has permission to access |service| from
  // the device with |device_id|.
  virtual bool IsAllowedToAccessService(
      RenderFrameHost* frame,
      const blink::WebBluetoothDeviceId& device_id,
      const device::BluetoothUUID& service) = 0;

  // This should return true if |frame| can access at least one service from the
  // device with |device_id|.
  virtual bool IsAllowedToAccessAtLeastOneService(
      RenderFrameHost* frame,
      const blink::WebBluetoothDeviceId& device_id) = 0;

  // This should return true if |frame| has permission to access data associated
  // with |manufacturer_code| from advertisement packets from the device with
  // |device_id|.
  virtual bool IsAllowedToAccessManufacturerData(
      RenderFrameHost* frame,
      const blink::WebBluetoothDeviceId& device_id,
      uint16_t manufacturer_code) = 0;

  // This should return a list of devices that the origin in |frame| has been
  // allowed to access. Access permission is granted with
  // GrantServiceAccessPermission() and can be revoked by the user in the
  // embedder's UI. The list of devices returned should be PermittedDevice
  // objects, which contain the necessary fields to create the BluetoothDevice
  // JavaScript objects.
  virtual std::vector<blink::mojom::WebBluetoothDevicePtr> GetPermittedDevices(
      RenderFrameHost* frame) = 0;

  // Add a permission observer to allow observing permission revocation effects
  // for a particular frame.
  virtual void AddFramePermissionObserver(
      FramePermissionObserver* observer) = 0;

  // Remove a previously added permission observer.
  virtual void RemoveFramePermissionObserver(
      FramePermissionObserver* observer) = 0;
};

}  // namespace content

#endif  // CONTENT_PUBLIC_BROWSER_BLUETOOTH_DELEGATE_H_
