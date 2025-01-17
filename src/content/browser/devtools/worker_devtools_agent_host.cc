// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "content/browser/devtools/worker_devtools_agent_host.h"

#include "base/bind.h"
#include "base/callback_helpers.h"
#include "content/browser/devtools/devtools_session.h"
#include "content/browser/devtools/protocol/io_handler.h"
#include "content/browser/devtools/protocol/network_handler.h"
#include "content/browser/devtools/protocol/target_handler.h"
#include "content/browser/devtools/shared_worker_devtools_agent_host.h"
#include "content/browser/renderer_host/render_process_host_impl.h"
#include "content/browser/storage_partition_impl.h"
#include "content/browser/worker_host/dedicated_worker_host.h"
#include "content/public/common/child_process_host.h"

namespace content {

WorkerDevToolsAgentHost::WorkerDevToolsAgentHost(
    int process_id,
    mojo::PendingRemote<blink::mojom::DevToolsAgent> agent_remote,
    mojo::PendingReceiver<blink::mojom::DevToolsAgentHost> host_receiver,
    const GURL& url,
    const std::string& name,
    const base::UnguessableToken& devtools_worker_token,
    const std::string& parent_id,
    base::OnceCallback<void(DevToolsAgentHostImpl*)> destroyed_callback)
    : DevToolsAgentHostImpl(devtools_worker_token.ToString()),
      process_id_(process_id),
      url_(url),
      name_(name),
      parent_id_(parent_id),
      destroyed_callback_(std::move(destroyed_callback)),
      devtools_worker_token_(devtools_worker_token) {
  DCHECK(agent_remote);
  DCHECK(!devtools_worker_token.is_empty());
  AddRef();  // Self keep-alive while the worker agent is alive.
  base::OnceClosure connection_error = (base::BindOnce(
      &WorkerDevToolsAgentHost::Disconnected, base::Unretained(this)));
  NotifyCreated();
  GetRendererChannel()->SetRenderer(std::move(agent_remote),
                                    std::move(host_receiver), process_id,
                                    std::move(connection_error));
}

WorkerDevToolsAgentHost::~WorkerDevToolsAgentHost() = default;

void WorkerDevToolsAgentHost::Disconnected() {
  ForceDetachAllSessions();
  GetRendererChannel()->SetRenderer(mojo::NullRemote(), mojo::NullReceiver(),
                                    ChildProcessHost::kInvalidUniqueID);
  std::move(destroyed_callback_).Run(this);
  Release();  // Matches AddRef() in constructor.
}

BrowserContext* WorkerDevToolsAgentHost::GetBrowserContext() {
  RenderProcessHost* process = RenderProcessHost::FromID(process_id_);
  return process ? process->GetBrowserContext() : nullptr;
}

std::string WorkerDevToolsAgentHost::GetType() {
  return kTypeDedicatedWorker;
}

std::string WorkerDevToolsAgentHost::GetTitle() {
  return name_.empty() ? url_.spec() : name_;
}

std::string WorkerDevToolsAgentHost::GetParentId() {
  return parent_id_;
}

GURL WorkerDevToolsAgentHost::GetURL() {
  return url_;
}

bool WorkerDevToolsAgentHost::Activate() {
  return false;
}

void WorkerDevToolsAgentHost::Reload() {}

bool WorkerDevToolsAgentHost::Close() {
  return false;
}

bool WorkerDevToolsAgentHost::AttachSession(DevToolsSession* session,
                                            bool acquire_wake_lock) {
  session->AddHandler(std::make_unique<protocol::IOHandler>(GetIOContext()));
  session->AddHandler(std::make_unique<protocol::TargetHandler>(
      protocol::TargetHandler::AccessMode::kAutoAttachOnly, GetId(),
      GetRendererChannel(), session->GetRootSession()));
  session->AddHandler(std::make_unique<protocol::NetworkHandler>(
      GetId(), devtools_worker_token_, GetIOContext(), base::DoNothing()));
  return true;
}

void WorkerDevToolsAgentHost::DetachSession(DevToolsSession* session) {
  // Destroying session automatically detaches in renderer.
}

DedicatedWorkerHost* WorkerDevToolsAgentHost::GetDedicatedWorkerHost() {
  RenderProcessHost* process = RenderProcessHost::FromID(process_id_);
  auto* storage_partition_impl =
      static_cast<StoragePartitionImpl*>(process->GetStoragePartition());
  auto* service = storage_partition_impl->GetDedicatedWorkerService();
  return service->GetDedicatedWorkerHostFromToken(
      blink::DedicatedWorkerToken(devtools_worker_token_));
}

base::Optional<network::CrossOriginEmbedderPolicy>
WorkerDevToolsAgentHost::cross_origin_embedder_policy(const std::string&) {
  DedicatedWorkerHost* host = GetDedicatedWorkerHost();
  if (!host) {
    return base::nullopt;
  }
  return host->cross_origin_embedder_policy();
}

}  // namespace content
