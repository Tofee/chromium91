// Copyright 2020 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#ifndef CONTENT_BROWSER_NATIVE_IO_NATIVE_IO_HOST_H_
#define CONTENT_BROWSER_NATIVE_IO_NATIVE_IO_HOST_H_

#include <map>
#include <memory>
#include <string>

#include "base/files/file_path.h"
#include "base/memory/scoped_refptr.h"
#include "base/memory/weak_ptr.h"
#include "base/sequence_checker.h"
#include "build/build_config.h"
#include "mojo/public/cpp/bindings/pending_receiver.h"
#include "mojo/public/cpp/bindings/receiver_set.h"
#include "third_party/blink/public/mojom/native_io/native_io.mojom.h"
#include "url/origin.h"

namespace base {
class TaskRunner;
}  // namespace base

namespace content {

class NativeIOManager;
class NativeIOFileHost;

// Implements the NativeIO Web Platform feature for an origin.
//
// NativeIOManager owns an instance of this class for each origin that is
// actively using NativeIO.
//
// This class is not thread-safe, so all access to an instance must happen on
// the same sequence. However, origins are completely isolated from each other,
// so different NativeIOHost instances can safely be used on different
// sequences, if desired.
class NativeIOHost : public blink::mojom::NativeIOHost {
 public:
  using DeleteAllDataCallback =
      base::OnceCallback<void(base::File::Error result, NativeIOHost* host)>;

  // `allow_set_length_ipc` gates NativeIOFileHost::SetLength(), which works
  // around a sandboxing limitation on macOS < 10.15. This is plumbed as a flag
  // all the from NativeIOManager to facilitate testing.
  explicit NativeIOHost(const url::Origin& origin,
                        base::FilePath root_path,
#if defined(OS_MAC)
                        bool allow_set_length_ipc,
#endif  // defined(OS_MAC)
                        NativeIOManager* manager);

  NativeIOHost(const NativeIOHost&) = delete;
  NativeIOHost& operator=(const NativeIOHost&) = delete;

  ~NativeIOHost() override;

  // Binds |receiver| to the NativeIOHost. The |receiver| must belong to a frame
  // or worker for this host's origin.
  void BindReceiver(mojo::PendingReceiver<blink::mojom::NativeIOHost> receiver);

  // True if there are no receivers connected to this host.
  //
  // The NativeIOManager that owns this host is expected to destroy the host
  // when it isn't serving any receivers.
  bool has_empty_receiver_set() const { return receivers_.empty(); }

  // The origin served by this host.
  const url::Origin& origin() const { return origin_; }

  // True if this host's data is currently being deleted.
  bool delete_all_data_in_progress() const {
    return !delete_all_data_callbacks_.empty();
  }

  bool is_incognito_mode() const { return root_path_.empty(); }

  // blink::mojom::NativeIOHost:
  void OpenFile(
      const std::string& name,
      mojo::PendingReceiver<blink::mojom::NativeIOFileHost> file_host_receiver,
      OpenFileCallback callback) override;
  void DeleteFile(const std::string& name,
                  DeleteFileCallback callback) override;
  void GetAllFileNames(GetAllFileNamesCallback callback) override;
  void RenameFile(const std::string& old_name,
                  const std::string& new_name,
                  RenameFileCallback callback) override;
  void RequestCapacityChange(int64_t capacity_delta,
                             RequestCapacityChangeCallback callback) override;

  // Removes all data stored for the host's origin from disk. All mojo
  // connections for open files are closed.
  void DeleteAllData(DeleteAllDataCallback callback);

  // Called when one of the open files for this origin closes.
  //
  // |file_host| must be owned by this origin host. This method should only be
  // called by NativeIOFileHost.
  void OnFileClose(NativeIOFileHost* file_host);

 private:
  // Called when a receiver in the receiver set is disconnected.
  void OnReceiverDisconnect();

  // Called after the file I/O part of OpenFile() completed.
  void DidOpenFile(
      const std::string& name,
      mojo::PendingReceiver<blink::mojom::NativeIOFileHost> file_host_receiver,
      OpenFileCallback callback,
      std::pair<base::File, int64_t> result);

  // Called after the file I/O part of DeleteFile() completed.
  void DidDeleteFile(const std::string& name,
                     DeleteFileCallback callback,
                     std::pair<blink::mojom::NativeIOErrorPtr, int64_t> result);

  // Called after the file I/O part of RenameFile() completed.
  void DidRenameFile(const std::string& old_name,
                     const std::string& new_name,
                     RenameFileCallback callback,
                     blink::mojom::NativeIOErrorPtr rename_error);

  // Called after the file I/O part of DeleteAllData() completed.
  void DidDeleteAllData(base::File::Error error);

  SEQUENCE_CHECKER(sequence_checker_);

  // The origin served by this host.
  const url::Origin origin_;

  // Deletion requests issued during an ongoing deletion are coalesced with that
  // deletion request. All coalesced callbacks are stored and invoked
  // together.
  std::vector<DeleteAllDataCallback> delete_all_data_callbacks_;

  // The directory holding all the files for this origin.
  const base::FilePath root_path_;

#if defined(OS_MAC)
  const bool allow_set_length_ipc_;
#endif  // defined(OS_MAC)

  // Raw pointer use is safe because NativeIOManager owns this NativeIOHost, and
  // therefore is guaranteed to outlive it.
  NativeIOManager* const manager_;

  // Schedules all operations involving file I/O done by this NativeIOHost.
  const scoped_refptr<base::TaskRunner> file_task_runner_;

  // All receivers for frames and workers whose origin is `origin_` associated
  // with the StoragePartition that owns `manager_`.
  mojo::ReceiverSet<blink::mojom::NativeIOHost> receivers_;

  // The names of files that have pending I/O tasks.
  //
  // This set's contents must not overlap with the keys in |open_file_hosts_|.
  std::set<std::string> io_pending_files_;

  // Maps open file names to their corresponding receivers.
  //
  // This map's keys must not overlap with the contents of |io_pending_files_|.
  std::map<std::string, std::unique_ptr<NativeIOFileHost>> open_file_hosts_;

  base::WeakPtrFactory<NativeIOHost> weak_factory_{this};
};

}  // namespace content

#endif  // CONTENT_BROWSER_NATIVE_IO_NATIVE_IO_HOST_H_
