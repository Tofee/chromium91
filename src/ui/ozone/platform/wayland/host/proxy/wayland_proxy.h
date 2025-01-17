// Copyright 2021 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef UI_OZONE_PLATFORM_WAYLAND_HOST_PROXY_WAYLAND_PROXY_H_
#define UI_OZONE_PLATFORM_WAYLAND_HOST_PROXY_WAYLAND_PROXY_H_

#include "base/component_export.h"
#include "ui/gfx/native_widget_types.h"
#include "ui/platform_window/platform_window_init_properties.h"

struct wl_buffer;
struct wl_display;
struct wl_surface;

namespace gfx {
class Rect;
class Size;
}  // namespace gfx

namespace wl {

// A proxy interface to Ozone/Wayland that is used by input emulation. The
// reason why this is needed is that input emulation mustn't be part of
// Chromium and only be used and compiled when there is a need to run tests.
// This nicely separates Ozone/Wayland from input emulation and provides just
// core functionality that input emulation needs from Ozone/Wayland.
class COMPONENT_EXPORT(WAYLAND_PROXY) WaylandProxy {
 public:
  class Delegate {
   public:
    // Invoked when a new window is created aka WaylandWindow is added to the
    // list of windows stored by WaylandWindowManager.
    virtual void OnWindowAdded(gfx::AcceleratedWidget widget) = 0;

    // Invoked when an existing surface is removed aka WaylandWindow is removed
    // from the list of windows stored by WaylandWindowManager.
    virtual void OnWindowRemoved(gfx::AcceleratedWidget widget) = 0;

    // Invoked when an existing surface is configured.
    virtual void OnWindowConfigured(gfx::AcceleratedWidget widget) = 0;

   protected:
    virtual ~Delegate() = default;
  };

  virtual ~WaylandProxy() = default;

  static WaylandProxy* GetInstance();

  // Sets the delegate that will be notified about the events described above
  // for the Delegate class.
  virtual void SetDelegate(Delegate* delegate) = 0;

  // Returns the wl_display the WaylandConnection has the connection with.
  virtual wl_display* GetDisplay() = 0;

  // Returns wl_surface that backs the |widget|.
  virtual wl_surface* GetWlSurfaceForAcceleratedWidget(
      gfx::AcceleratedWidget widget) = 0;

  // Creates and returns a shm based wl_buffer with |buffer_size|. The shared
  // memory is hold until DestroyShmForWlBuffer is called.
  virtual wl_buffer* CreateShmBasedWlBuffer(const gfx::Size& buffer_size) = 0;

  // When this is called, |buffer| becomes invalid and mustn't be used any more.
  virtual void DestroyShmForWlBuffer(wl_buffer* buffer) = 0;

  // Schedules display flush that dispatches pending events.
  virtual void ScheduleDisplayFlush() = 0;

  // Returns platform window type of a window backed by the |widget|.
  virtual ui::PlatformWindowType GetWindowType(
      gfx::AcceleratedWidget widget) = 0;

  // Returns bounds in px of the window backed by |widget|.
  virtual gfx::Rect GetWindowBounds(gfx::AcceleratedWidget widget) = 0;

  virtual bool WindowHasPointerFocus(gfx::AcceleratedWidget widget) = 0;
  virtual bool WindowHasKeyboardFocus(gfx::AcceleratedWidget widget) = 0;

 protected:
  static void SetInstance(WaylandProxy* instance);
};

}  // namespace wl

#endif  // UI_OZONE_PLATFORM_WAYLAND_HOST_WAYLAND_PROXY_H_
