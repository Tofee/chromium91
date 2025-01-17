// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "media/audio/audio_manager.h"

#include <stdint.h>

#include <utility>

#include "base/bind.h"
#include "base/callback_helpers.h"
#include "base/command_line.h"
#include "base/logging.h"
#include "base/macros.h"
#include "base/metrics/histogram_macros.h"
#include "base/power_monitor/power_monitor.h"
#include "base/single_thread_task_runner.h"
#include "base/strings/stringprintf.h"
#include "base/thread_annotations.h"
#include "build/build_config.h"
#include "media/audio/fake_audio_log_factory.h"
#include "media/base/media_switches.h"

#if defined(OS_WIN)
#include "base/win/scoped_com_initializer.h"
#endif

namespace media {
namespace {

// The singleton instance of AudioManager. This is set when Create() is called.
AudioManager* g_last_created = nullptr;

// Helper class for managing global AudioManager data.
class AudioManagerHelper {
 public:
  AudioManagerHelper() = default;
  ~AudioManagerHelper() = default;

  AudioLogFactory* fake_log_factory() { return &fake_log_factory_; }

#if defined(OS_WIN)
  // This should be called before creating an AudioManager in tests to ensure
  // that the creating thread is COM initialized.
  void InitializeCOMForTesting() {
    com_initializer_for_testing_.reset(new base::win::ScopedCOMInitializer());
  }
#endif

  void set_app_name(const std::string& app_name) { app_name_ = app_name; }
  const std::string& app_name() const { return app_name_; }

  FakeAudioLogFactory fake_log_factory_;

#if defined(OS_WIN)
  std::unique_ptr<base::win::ScopedCOMInitializer> com_initializer_for_testing_;
#endif

  std::string app_name_;

  DISALLOW_COPY_AND_ASSIGN(AudioManagerHelper);
};

AudioManagerHelper* GetHelper() {
  static AudioManagerHelper* helper = new AudioManagerHelper();
  return helper;
}

}  // namespace

// Forward declaration of the platform specific AudioManager factory function.
std::unique_ptr<AudioManager> CreateAudioManager(
    std::unique_ptr<AudioThread> audio_thread,
    AudioLogFactory* audio_log_factory);

void AudioManager::SetMaxStreamCountForTesting(int max_input, int max_output) {
  NOTREACHED();
}

AudioManager::AudioManager(std::unique_ptr<AudioThread> audio_thread)
    : audio_thread_(std::move(audio_thread)) {
  DCHECK(audio_thread_);

  if (g_last_created) {
    // We create multiple instances of AudioManager only when testing.
    // We should not encounter this case in production.
    LOG(WARNING) << "Multiple instances of AudioManager detected";
  }
  // We always override |g_last_created| irrespective of whether it is already
  // set or not because it represents the last created instance.
  g_last_created = this;
}

AudioManager::~AudioManager() {
  DCHECK_CALLED_ON_VALID_THREAD(thread_checker_);
  DCHECK(shutdown_);

  if (g_last_created == this) {
    g_last_created = nullptr;
  } else {
    // We create multiple instances of AudioManager only when testing.
    // We should not encounter this case in production.
    LOG(WARNING) << "Multiple instances of AudioManager detected";
  }
}

// static
std::unique_ptr<AudioManager> AudioManager::Create(
    std::unique_ptr<AudioThread> audio_thread,
    AudioLogFactory* audio_log_factory) {
  std::unique_ptr<AudioManager> manager =
      CreateAudioManager(std::move(audio_thread), audio_log_factory);
  manager->InitializeDebugRecording();
  return manager;
}

// static
std::unique_ptr<AudioManager> AudioManager::CreateForTesting(
    std::unique_ptr<AudioThread> audio_thread) {
#if defined(OS_WIN)
  GetHelper()->InitializeCOMForTesting();
#endif
  return Create(std::move(audio_thread), GetHelper()->fake_log_factory());
}

// static
void AudioManager::SetGlobalAppName(const std::string& app_name) {
  GetHelper()->set_app_name(app_name);
}

// static
const std::string& AudioManager::GetGlobalAppName() {
  return GetHelper()->app_name();
}

// static
AudioManager* AudioManager::Get() {
  return g_last_created;
}

bool AudioManager::Shutdown() {
  DCHECK_CALLED_ON_VALID_THREAD(thread_checker_);

  if (audio_thread_->GetTaskRunner()->BelongsToCurrentThread()) {
    // If this is the audio thread, there is no need to check if it's hung
    // (since it's clearly not). https://crbug.com/919854.
    ShutdownOnAudioThread();
  } else {
    // Do not attempt to stop the audio thread if it is hung.
    // Otherwise the current thread will hang too: https://crbug.com/729494
    // TODO(olka, grunell): Will be fixed when audio is its own process.
    if (audio_thread_->IsHung())
      return false;

    audio_thread_->GetTaskRunner()->PostTask(
        FROM_HERE, base::BindOnce(&AudioManager::ShutdownOnAudioThread,
                                  base::Unretained(this)));
  }
  audio_thread_->Stop();
  shutdown_ = true;
  return true;
}

void AudioManager::SetDiverterCallbacks(
    AddDiverterCallback add_callback,
    RemoveDiverterCallback remove_callback) {
  add_diverter_callback_ = std::move(add_callback);
  remove_diverter_callback_ = std::move(remove_callback);
}

void AudioManager::AddDiverter(const base::UnguessableToken& group_id,
                               media::AudioSourceDiverter* diverter) {
  if (!add_diverter_callback_.is_null())
    add_diverter_callback_.Run(group_id, diverter);
}

void AudioManager::RemoveDiverter(media::AudioSourceDiverter* diverter) {
  if (!remove_diverter_callback_.is_null())
    remove_diverter_callback_.Run(diverter);
}

}  // namespace media
