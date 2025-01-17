// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/exo/test/exo_test_helper.h"

#include <memory>

#include "ash/public/cpp/shell_window_ids.h"
#include "ash/wm/desks/desks_util.h"
#include "ash/wm/window_positioner.h"
#include "ash/wm/window_positioning_utils.h"
#include "components/exo/buffer.h"
#include "components/exo/client_controlled_shell_surface.h"
#include "components/exo/display.h"
#include "components/exo/input_method_surface.h"
#include "components/exo/surface.h"
#include "components/exo/toast_surface.h"
#include "components/exo/wm_helper.h"
#include "components/exo/xdg_shell_surface.h"
#include "gpu/command_buffer/client/gpu_memory_buffer_manager.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "ui/aura/env.h"
#include "ui/compositor/compositor.h"
#include "ui/display/display.h"
#include "ui/display/screen.h"
#include "ui/views/widget/widget.h"

namespace exo {
namespace test {

ClientControlledShellSurfaceDelegate::ClientControlledShellSurfaceDelegate(
    ClientControlledShellSurface* shell_surface)
    : shell_surface_(shell_surface) {}
ClientControlledShellSurfaceDelegate::~ClientControlledShellSurfaceDelegate() =
    default;

void ClientControlledShellSurfaceDelegate::OnGeometryChanged(
    const gfx::Rect& geometry) {}
void ClientControlledShellSurfaceDelegate::OnStateChanged(
    chromeos::WindowStateType old_state,
    chromeos::WindowStateType new_state) {
  switch (new_state) {
    case chromeos::WindowStateType::kNormal:
    case chromeos::WindowStateType::kDefault:
      shell_surface_->SetRestored();
      break;
    case chromeos::WindowStateType::kMinimized:
      shell_surface_->SetMinimized();
      break;
    case chromeos::WindowStateType::kMaximized:
      shell_surface_->SetMaximized();
      break;
    case chromeos::WindowStateType::kFullscreen:
      shell_surface_->SetFullscreen(true);
      break;
    default:
      NOTIMPLEMENTED();
      break;
  }
  shell_surface_->OnSurfaceCommit();
}
void ClientControlledShellSurfaceDelegate::OnBoundsChanged(
    chromeos::WindowStateType current_state,
    chromeos::WindowStateType requested_state,
    int64_t display_id,
    const gfx::Rect& bounds_in_screen,
    bool is_resize,
    int bounds_change) {
  ASSERT_TRUE(display_id != display::kInvalidDisplayId);

  auto* window_state =
      ash::WindowState::Get(shell_surface_->GetWidget()->GetNativeWindow());

  if (!shell_surface_->host_window()->GetRootWindow())
    return;

  display::Display target_display;
  const display::Screen* screen = display::Screen::GetScreen();

  if (!screen->GetDisplayWithDisplayId(display_id, &target_display)) {
    return;
  }

  // Don't change the bounds in maximize/fullscreen/pinned state.
  if (window_state->IsMaximizedOrFullscreenOrPinned() &&
      requested_state == window_state->GetStateType()) {
    return;
  }

  gfx::Rect bounds_in_display(bounds_in_screen);
  bounds_in_display.Offset(-target_display.bounds().OffsetFromOrigin());
  shell_surface_->SetBounds(display_id, bounds_in_display);

  if (requested_state != window_state->GetStateType()) {
    DCHECK(requested_state == chromeos::WindowStateType::kLeftSnapped ||
           requested_state == chromeos::WindowStateType::kRightSnapped);

    if (requested_state == chromeos::WindowStateType::kLeftSnapped)
      shell_surface_->SetSnappedToLeft();
    else
      shell_surface_->SetSnappedToRight();
  }

  shell_surface_->OnSurfaceCommit();
}
void ClientControlledShellSurfaceDelegate::OnDragStarted(int component) {}
void ClientControlledShellSurfaceDelegate::OnDragFinished(int x,
                                                          int y,
                                                          bool canceled) {}
void ClientControlledShellSurfaceDelegate::OnZoomLevelChanged(
    ZoomChange zoom_change) {}

////////////////////////////////////////////////////////////////////////////////
// ExoTestHelper, public:

ExoTestHelper::ExoTestHelper() {
  ash::WindowPositioner::DisableAutoPositioning(true);
}

ExoTestHelper::~ExoTestHelper() {}

std::unique_ptr<gfx::GpuMemoryBuffer> ExoTestHelper::CreateGpuMemoryBuffer(
    const gfx::Size& size,
    gfx::BufferFormat format) {
  return aura::Env::GetInstance()
      ->context_factory()
      ->GetGpuMemoryBufferManager()
      ->CreateGpuMemoryBuffer(size, format, gfx::BufferUsage::GPU_READ,
                              gpu::kNullSurfaceHandle);
}

std::unique_ptr<ClientControlledShellSurface>
ExoTestHelper::CreateClientControlledShellSurface(
    Surface* surface,
    bool is_modal,
    bool default_scale_cancellation) {
  int container = is_modal ? ash::kShellWindowId_SystemModalContainer
                           : ash::desks_util::GetActiveDeskContainerId();
  auto shell_surface = Display().CreateClientControlledShellSurface(
      surface, container,
      WMHelper::GetInstance()->GetDefaultDeviceScaleFactor(),
      default_scale_cancellation);
  shell_surface->SetApplicationId("arc");
  shell_surface->set_delegate(
      std::make_unique<ClientControlledShellSurfaceDelegate>(
          shell_surface.get()));

  return shell_surface;
}

std::unique_ptr<InputMethodSurface> ExoTestHelper::CreateInputMethodSurface(
    Surface* surface,
    InputMethodSurfaceManager* surface_manager,
    bool default_scale_cancellation) {
  auto shell_surface = std::make_unique<InputMethodSurface>(
      surface_manager, surface, default_scale_cancellation);

  shell_surface->set_delegate(
      std::make_unique<ClientControlledShellSurfaceDelegate>(
          shell_surface.get()));

  return shell_surface;
}

std::unique_ptr<ToastSurface> ExoTestHelper::CreateToastSurface(
    Surface* surface,
    ToastSurfaceManager* surface_manager,
    bool default_scale_cancellation) {
  auto shell_surface = std::make_unique<ToastSurface>(
      surface_manager, surface, default_scale_cancellation);

  shell_surface->set_delegate(
      std::make_unique<ClientControlledShellSurfaceDelegate>(
          shell_surface.get()));

  return shell_surface;
}

}  // namespace test
}  // namespace exo
