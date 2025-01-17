// Copyright 2020 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chromeos/services/libassistant/service_controller.h"

#include <memory>

#include "base/check.h"
#include "chromeos/assistant/internal/internal_util.h"
#include "chromeos/services/assistant/public/cpp/features.h"
#include "chromeos/services/libassistant/chromium_api_delegate.h"
#include "chromeos/services/libassistant/libassistant_factory.h"
#include "chromeos/services/libassistant/settings_controller.h"
#include "chromeos/services/libassistant/util.h"
#include "libassistant/shared/internal_api/assistant_manager_internal.h"
#include "libassistant/shared/public/device_state_listener.h"
#include "services/network/public/cpp/cross_thread_pending_shared_url_loader_factory.h"
#include "services/network/public/cpp/wrapper_shared_url_loader_factory.h"

namespace chromeos {
namespace libassistant {

namespace {

using mojom::ServiceState;

// A macro which ensures we are running on the mojom thread.
#define ENSURE_MOJOM_THREAD(method, ...)                                    \
  if (!mojom_task_runner_->RunsTasksInCurrentSequence()) {                  \
    mojom_task_runner_->PostTask(                                           \
        FROM_HERE,                                                          \
        base::BindOnce(method, weak_factory_.GetWeakPtr(), ##__VA_ARGS__)); \
    return;                                                                 \
  }

constexpr base::Feature kChromeOSAssistantDogfood{
    "ChromeOSAssistantDogfood", base::FEATURE_DISABLED_BY_DEFAULT};

constexpr char kServersideDogfoodExperimentId[] = "20347368";
constexpr char kServersideOpenAppExperimentId[] = "39651593";
constexpr char kServersideResponseProcessingV2ExperimentId[] = "1793869";

std::string ToLibassistantConfig(const mojom::BootupConfig& bootup_config) {
  return CreateLibAssistantConfig(bootup_config.s3_server_uri_override,
                                  bootup_config.device_id_override);
}

std::unique_ptr<network::PendingSharedURLLoaderFactory>
CreatePendingURLLoaderFactory(
    mojo::PendingRemote<network::mojom::URLLoaderFactory>
        url_loader_factory_remote) {
  // First create a wrapped factory that can accept the pending remote.
  auto pending_url_loader_factory =
      std::make_unique<network::WrapperPendingSharedURLLoaderFactory>(
          std::move(url_loader_factory_remote));
  auto wrapped_factory = network::SharedURLLoaderFactory::Create(
      std::move(pending_url_loader_factory));

  // Then move it into a cross thread factory, as the url loader factory will be
  // used from internal Libassistant threads.
  return std::make_unique<network::CrossThreadPendingSharedURLLoaderFactory>(
      std::move(wrapped_factory));
}

void FillServerExperimentIds(std::vector<std::string>* server_experiment_ids) {
  if (base::FeatureList::IsEnabled(kChromeOSAssistantDogfood)) {
    server_experiment_ids->emplace_back(kServersideDogfoodExperimentId);
  }

  if (base::FeatureList::IsEnabled(assistant::features::kAssistantAppSupport))
    server_experiment_ids->emplace_back(kServersideOpenAppExperimentId);

  server_experiment_ids->emplace_back(
      kServersideResponseProcessingV2ExperimentId);
}

void SetServerExperiments(
    assistant_client::AssistantManagerInternal* assistant_manager_internal) {
  std::vector<std::string> server_experiment_ids;
  FillServerExperimentIds(&server_experiment_ids);

  if (server_experiment_ids.size() > 0) {
    assistant_manager_internal->AddExtraExperimentIds(server_experiment_ids);
  }
}

}  // namespace

////////////////////////////////////////////////////////////////////////////////
//  DeviceStateListener
////////////////////////////////////////////////////////////////////////////////

class ServiceController::DeviceStateListener
    : public assistant_client::DeviceStateListener {
 public:
  explicit DeviceStateListener(ServiceController* parent)
      : parent_(parent),
        mojom_task_runner_(base::SequencedTaskRunnerHandle::Get()) {}
  DeviceStateListener(const DeviceStateListener&) = delete;
  DeviceStateListener& operator=(const DeviceStateListener&) = delete;
  ~DeviceStateListener() override = default;

  // assistant_client::DeviceStateListener overrides:
  // Called on Libassistant thread.
  void OnStartFinished() override {
    ENSURE_MOJOM_THREAD(&DeviceStateListener::OnStartFinished);
    parent_->OnStartFinished();
  }

 private:
  ServiceController* const parent_;
  scoped_refptr<base::SequencedTaskRunner> mojom_task_runner_;
  base::WeakPtrFactory<DeviceStateListener> weak_factory_{this};
};

////////////////////////////////////////////////////////////////////////////////
//  ServiceController
////////////////////////////////////////////////////////////////////////////////

ServiceController::ServiceController(LibassistantFactory* factory)
    : libassistant_factory_(*factory) {
  DCHECK(factory);
}

ServiceController::~ServiceController() {
  // Ensure all our observers know this service is no longer running.
  // This will be a noop if we're already stopped.
  Stop();
}

void ServiceController::Bind(
    mojo::PendingReceiver<mojom::ServiceController> receiver,
    mojom::SettingsController* settings_controller) {
  DCHECK(!receiver_.is_bound());
  receiver_.Bind(std::move(receiver));
  settings_controller_ = settings_controller;
}

void ServiceController::Initialize(
    mojom::BootupConfigPtr config,
    mojo::PendingRemote<network::mojom::URLLoaderFactory> url_loader_factory) {
  if (assistant_manager_ != nullptr) {
    LOG(ERROR) << "Initialize() should only be called once.";
    return;
  }

  assistant_manager_ = libassistant_factory_.CreateAssistantManager(
      ToLibassistantConfig(*config));
  assistant_manager_internal_ =
      libassistant_factory_.UnwrapAssistantManagerInternal(
          assistant_manager_.get());

  DCHECK(settings_controller_);
  settings_controller_->SetAuthenticationTokens(
      std::move(config->authentication_tokens));
  settings_controller_->SetLocale(config->locale);
  settings_controller_->SetHotwordEnabled(config->hotword_enabled);
  settings_controller_->SetSpokenFeedbackEnabled(
      config->spoken_feedback_enabled);

  CreateAndRegisterDeviceStateListener();
  CreateAndRegisterChromiumApiDelegate(std::move(url_loader_factory));

  SetServerExperiments(assistant_manager_internal());

  for (auto& observer : assistant_manager_observers_) {
    observer.OnAssistantManagerCreated(assistant_manager(),
                                       assistant_manager_internal());
  }
}

void ServiceController::Start() {
  if (state_ != ServiceState::kStopped)
    return;

  DCHECK(IsInitialized()) << "Initialize() must be called before Start()";
  DVLOG(1) << "Starting Libassistant service";

  assistant_manager()->Start();

  SetStateAndInformObservers(ServiceState::kStarted);

  for (auto& observer : assistant_manager_observers_) {
    observer.OnAssistantManagerStarted(assistant_manager(),
                                       assistant_manager_internal());
  }

  DVLOG(1) << "Started Libassistant service";
}

void ServiceController::Stop() {
  if (state_ == ServiceState::kStopped)
    return;

  DVLOG(1) << "Stopping Libassistant service";
  SetStateAndInformObservers(ServiceState::kStopped);

  for (auto& observer : assistant_manager_observers_) {
    observer.OnDestroyingAssistantManager(assistant_manager(),
                                          assistant_manager_internal());
  }

  assistant_manager_ = nullptr;
  assistant_manager_internal_ = nullptr;
  chromium_api_delegate_ = nullptr;
  device_state_listener_ = nullptr;

  for (auto& observer : assistant_manager_observers_)
    observer.OnAssistantManagerDestroyed();

  DVLOG(1) << "Stopped Libassistant service";
}

void ServiceController::ResetAllDataAndStop() {
  if (assistant_manager()) {
    DVLOG(1) << "Resetting all Libassistant data";
    assistant_manager()->ResetAllDataAndShutdown();
  }
  Stop();
}

void ServiceController::AddAndFireStateObserver(
    mojo::PendingRemote<mojom::StateObserver> pending_observer) {
  mojo::Remote<mojom::StateObserver> observer(std::move(pending_observer));

  observer->OnStateChanged(state_);

  state_observers_.Add(std::move(observer));
}

void ServiceController::AddAndFireAssistantManagerObserver(
    AssistantManagerObserver* observer) {
  DCHECK(observer);

  assistant_manager_observers_.AddObserver(observer);

  if (IsInitialized()) {
    observer->OnAssistantManagerCreated(assistant_manager(),
                                        assistant_manager_internal());
  }
  // Note we do send the |OnAssistantManagerStarted| event even if the service
  // is currently running, to ensure that an observer that only observes
  // |OnAssistantManagerStarted| will not miss a currently running instance
  // when it is being added.
  if (IsStarted()) {
    observer->OnAssistantManagerStarted(assistant_manager(),
                                        assistant_manager_internal());
  }
  if (IsRunning()) {
    observer->OnAssistantManagerRunning(assistant_manager(),
                                        assistant_manager_internal());
  }
}

void ServiceController::RemoveAssistantManagerObserver(
    AssistantManagerObserver* observer) {
  assistant_manager_observers_.RemoveObserver(observer);
}

void ServiceController::RemoveAllAssistantManagerObservers() {
  assistant_manager_observers_.Clear();
}

bool ServiceController::IsStarted() const {
  switch (state_) {
    case ServiceState::kStopped:
      return false;
    case ServiceState::kStarted:
    case ServiceState::kRunning:
      return true;
  }
}

bool ServiceController::IsInitialized() const {
  return assistant_manager_ != nullptr;
}

bool ServiceController::IsRunning() const {
  switch (state_) {
    case ServiceState::kStopped:
    case ServiceState::kStarted:
      return false;
    case ServiceState::kRunning:
      return true;
  }
}

assistant_client::AssistantManager* ServiceController::assistant_manager() {
  return assistant_manager_.get();
}

assistant_client::AssistantManagerInternal*
ServiceController::assistant_manager_internal() {
  return assistant_manager_internal_;
}

void ServiceController::OnStartFinished() {
  DVLOG(1) << "Libassistant start is finished";
  SetStateAndInformObservers(mojom::ServiceState::kRunning);

  for (auto& observer : assistant_manager_observers_) {
    observer.OnAssistantManagerRunning(assistant_manager(),
                                       assistant_manager_internal());
  }
}

void ServiceController::SetStateAndInformObservers(
    mojom::ServiceState new_state) {
  DCHECK_NE(state_, new_state);

  state_ = new_state;

  for (auto& observer : state_observers_)
    observer->OnStateChanged(state_);
}

void ServiceController::CreateAndRegisterDeviceStateListener() {
  device_state_listener_ = std::make_unique<DeviceStateListener>(this);
  assistant_manager()->AddDeviceStateListener(device_state_listener_.get());
}

void ServiceController::CreateAndRegisterChromiumApiDelegate(
    mojo::PendingRemote<network::mojom::URLLoaderFactory>
        url_loader_factory_remote) {
  CreateChromiumApiDelegate(std::move(url_loader_factory_remote));

  assistant_manager_internal()
      ->GetFuchsiaApiHelperOrDie()
      ->SetFuchsiaApiDelegate(chromium_api_delegate_.get());
}

void ServiceController::CreateChromiumApiDelegate(
    mojo::PendingRemote<network::mojom::URLLoaderFactory>
        url_loader_factory_remote) {
  DCHECK(!chromium_api_delegate_);

  chromium_api_delegate_ = std::make_unique<ChromiumApiDelegate>(
      CreatePendingURLLoaderFactory(std::move(url_loader_factory_remote)));
}

}  // namespace libassistant
}  // namespace chromeos
