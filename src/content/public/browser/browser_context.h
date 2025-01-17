// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CONTENT_PUBLIC_BROWSER_BROWSER_CONTEXT_H_
#define CONTENT_PUBLIC_BROWSER_BROWSER_CONTEXT_H_

#include <stddef.h>
#include <stdint.h>

#include <map>
#include <memory>
#include <string>
#include <unordered_set>
#include <vector>

#include "base/callback_forward.h"
#include "base/memory/weak_ptr.h"
#include "base/optional.h"
#include "base/supports_user_data.h"
#include "content/common/content_export.h"
#include "mojo/public/cpp/bindings/pending_receiver.h"
#include "mojo/public/cpp/bindings/pending_remote.h"
#include "services/network/public/mojom/network_context.mojom-forward.h"
#include "third_party/blink/public/mojom/blob/blob.mojom-forward.h"
#include "third_party/blink/public/mojom/push_messaging/push_messaging.mojom-forward.h"
#include "third_party/blink/public/mojom/push_messaging/push_messaging_status.mojom-forward.h"
#include "third_party/perfetto/include/perfetto/tracing/traced_value_forward.h"

#if !defined(OS_ANDROID)
#include "content/public/browser/zoom_level_delegate.h"
#endif

class GURL;

namespace base {
class FilePath;
}  // namespace base

namespace download {
class InProgressDownloadManager;
}

namespace storage {
class ExternalMountPoints;
}

namespace media {
class VideoDecodePerfHistory;
namespace learning {
class LearningSession;
}
}  // namespace media

namespace storage {
class BlobStorageContext;
class SpecialStoragePolicy;
}  // namespace storage

namespace variations {
class VariationsClient;
}  // namespace variations

namespace content {

class BackgroundFetchDelegate;
class BackgroundSyncController;
class BlobHandle;
class BrowserPluginGuestManager;
class BrowsingDataRemover;
class BrowsingDataRemoverDelegate;
class DownloadManager;
class ClientHintsControllerDelegate;
class ContentIndexProvider;
class DownloadManagerDelegate;
class FileSystemAccessPermissionContext;
class PermissionController;
class PermissionControllerDelegate;
class PushMessagingService;
class ResourceContext;
class SharedCorsOriginAccessList;
class SiteInstance;
class StorageNotificationService;
class StoragePartition;
class StoragePartitionConfig;
class SSLHostStateDelegate;

// This class holds the context needed for a browsing session.
// It lives on the UI thread. All these methods must only be called on the UI
// thread.
class CONTENT_EXPORT BrowserContext : public base::SupportsUserData {
 public:
  //////////////////////////////////////////////////////////////////////////////
  // The BrowserContext methods below are provided/implemented by the //content
  // layer (e.g. there is no need to override these methods in layers above
  // //content).
  //
  // The current practice is to make the methods in this section static and have
  // them take `BrowserContext* self` as the first parameter.  (It is known that
  // not all the methods below follow this pattern.)
  //
  // TODO(https://crbug.com/1179776): Consider converting methods in this
  // section into non-virtual instance methods (dropping the `BrowserContext*`
  // parameter along the way).
  //
  // TODO(https://crbug.com/1179776): Consider moving these methods to
  // BrowserContext::Impl or (in the future) BrowserContextImpl class.

  BrowserContext();
  ~BrowserContext() override;

  static DownloadManager* GetDownloadManager(BrowserContext* self);

  // Returns BrowserContext specific external mount points. It may return
  // nullptr if the context doesn't have any BrowserContext specific external
  // mount points. Currently, non-nullptr value is returned only on ChromeOS.
  static storage::ExternalMountPoints* GetMountPoints(BrowserContext* self);

  // Returns a BrowsingDataRemover that can schedule data deletion tasks
  // for this |context|.
  static BrowsingDataRemover* GetBrowsingDataRemover(BrowserContext* self);

  // Returns the PermissionController associated with this context. There's
  // always a PermissionController instance for each BrowserContext.
  static PermissionController* GetPermissionController(BrowserContext* self);

  // Returns a StoragePartition for the given SiteInstance. By default this will
  // create a new StoragePartition if it doesn't exist, unless |can_create| is
  // false.
  static StoragePartition* GetStoragePartition(BrowserContext* self,
                                               SiteInstance* site_instance,
                                               bool can_create = true);

  // Returns a StoragePartition for the given StoragePartitionConfig. By
  // default this will create a new StoragePartition if it doesn't exist,
  // unless |can_create| is false.
  static StoragePartition* GetStoragePartition(
      BrowserContext* self,
      const StoragePartitionConfig& storage_partition_config,
      bool can_create = true);

  // Deprecated. Do not add new callers. Use the SiteInstance or
  // StoragePartitionConfig methods above instead.
  // Returns a StoragePartition for the given URL. By default this will
  // create a new StoragePartition if it doesn't exist, unless |can_create| is
  // false.
  static StoragePartition* GetStoragePartitionForUrl(BrowserContext* self,
                                                     const GURL& url,
                                                     bool can_create = true);

  using StoragePartitionCallback =
      base::RepeatingCallback<void(StoragePartition*)>;
  static void ForEachStoragePartition(BrowserContext* self,
                                      StoragePartitionCallback callback);
  // Returns the number of StoragePartitions that exist for the given
  // |browser_context|.
  static size_t GetStoragePartitionCount(BrowserContext* self);
  static void AsyncObliterateStoragePartition(
      BrowserContext* self,
      const std::string& partition_domain,
      base::OnceClosure on_gc_required);

  // This function clears the contents of |active_paths| but does not take
  // ownership of the pointer.
  static void GarbageCollectStoragePartitions(
      BrowserContext* self,
      std::unique_ptr<std::unordered_set<base::FilePath>> active_paths,
      base::OnceClosure done);

  static StoragePartition* GetDefaultStoragePartition(BrowserContext* self);

  using BlobCallback = base::OnceCallback<void(std::unique_ptr<BlobHandle>)>;
  using BlobContextGetter =
      base::RepeatingCallback<base::WeakPtr<storage::BlobStorageContext>()>;

  // This method should be called on UI thread and calls back on UI thread
  // as well. Note that retrieving a blob ptr out of BlobHandle can only be
  // done on IO. |callback| returns a nullptr on failure.
  static void CreateMemoryBackedBlob(BrowserContext* self,
                                     base::span<const uint8_t> data,
                                     const std::string& content_type,
                                     BlobCallback callback);

  // Get a BlobStorageContext getter that needs to run on IO thread.
  static BlobContextGetter GetBlobStorageContext(BrowserContext* self);

  // Returns a mojom::mojo::PendingRemote<blink::mojom::Blob> for a specific
  // blob. If no blob exists with the given UUID, the
  // mojo::PendingRemote<blink::mojom::Blob> pipe will close. This method should
  // be called on the UI thread.
  // TODO(mek): Blob UUIDs should be entirely internal to the blob system, so
  // eliminate this method in favor of just passing around the
  // mojo::PendingRemote<blink::mojom::Blob> directly.
  static mojo::PendingRemote<blink::mojom::Blob> GetBlobRemote(
      BrowserContext* self,
      const std::string& uuid);

  // Delivers a push message with |data| to the Service Worker identified by
  // |origin| and |service_worker_registration_id|.
  static void DeliverPushMessage(
      BrowserContext* self,
      const GURL& origin,
      int64_t service_worker_registration_id,
      const std::string& message_id,
      base::Optional<std::string> payload,
      base::OnceCallback<void(blink::mojom::PushEventStatus)> callback);

  // Fires a push subscription change event to the Service Worker identified by
  // |origin| and |service_worker_registration_id| with |new_subscription| and
  // |old_subscription| as event information.
  static void FirePushSubscriptionChangeEvent(
      BrowserContext* self,
      const GURL& origin,
      int64_t service_worker_registration_id,
      blink::mojom::PushSubscriptionPtr new_subscription,
      blink::mojom::PushSubscriptionPtr old_subscription,
      base::OnceCallback<void(blink::mojom::PushEventStatus)> callback);

  static void NotifyWillBeDestroyed(BrowserContext* self);

  // Ensures that the corresponding ResourceContext is initialized. Normally the
  // BrowserContext initializs the corresponding getters when its objects are
  // created, but if the embedder wants to pass the ResourceContext to another
  // thread before they use BrowserContext, they should call this to make sure
  // that the ResourceContext is ready.
  static void EnsureResourceContextInitialized(BrowserContext* self);

  // Tells the HTML5 objects on this context to persist their session state
  // across the next restart.
  static void SaveSessionState(BrowserContext* self);

  static void SetDownloadManagerForTesting(
      BrowserContext* self,
      std::unique_ptr<DownloadManager> download_manager);

  static void SetPermissionControllerForTesting(
      BrowserContext* self,
      std::unique_ptr<PermissionController> permission_controller);

  // The list of CORS exemptions.  This list needs to be 1) replicated when
  // creating or re-creating new network::mojom::NetworkContexts (see
  // network::mojom::NetworkContextParams::cors_origin_access_list) and 2)
  // consulted by CORS-aware factories (e.g. passed when constructing
  // FileURLLoaderFactory).
  static SharedCorsOriginAccessList* GetSharedCorsOriginAccessList(
      BrowserContext* self);

  // Shuts down the storage partitions associated to this browser context.
  // This must be called before the browser context is actually destroyed
  // and before a clean-up task for its corresponding IO thread residents (e.g.
  // ResourceContext) is posted, so that the classes that hung on
  // StoragePartition can have time to do necessary cleanups on IO thread.
  void ShutdownStoragePartitions();

  // Returns true if shutdown has been initiated via a
  // NotifyWillBeDestroyed() call. This is a signal that the object will be
  // destroyed soon and no new references to this object should be created.
  bool ShutdownStarted();

  // Returns a unique string associated with this browser context.
  virtual const std::string& UniqueId();

  // Gets media service for storing/retrieving video decoding performance stats.
  // Exposed here rather than StoragePartition because all SiteInstances should
  // have similar decode performance and stats are not exposed to the web
  // directly, so privacy is not compromised.
  media::VideoDecodePerfHistory* GetVideoDecodePerfHistory();

  // Returns a LearningSession associated with |this|. Used as the central
  // source from which to retrieve LearningTaskControllers for media machine
  // learning.
  // Exposed here rather than StoragePartition because learnings will cover
  // general media trends rather than SiteInstance specific behavior. The
  // learnings are not exposed to the web.
  virtual media::learning::LearningSession* GetLearningSession();

  // Retrieves the InProgressDownloadManager associated with this object if
  // available
  virtual download::InProgressDownloadManager*
  RetriveInProgressDownloadManager();

  // Utility function useful for embedders. Only needs to be called if
  // 1) The embedder needs to use a new salt, and
  // 2) The embedder saves its salt across restarts.
  static std::string CreateRandomMediaDeviceIDSalt();

  // Write a representation of this object into a trace.
  void WriteIntoTracedValue(perfetto::TracedValue context);

  //////////////////////////////////////////////////////////////////////////////
  // The //content embedder can override the methods below to change or extend
  // how the //content layer interacts with a BrowserContext.
  //
  // All the methods below should be virtual.  Most of the methods should be
  // pure (i.e. `= 0`) although it may make sense to provide a default
  // implementation for some of the methods.
  //
  // TODO(https://crbug.com/1179776): Migrate method declarations from this
  // section into a separate BrowserContextDelegate class.

#if !defined(OS_ANDROID)
  // Creates a delegate to initialize a HostZoomMap and persist its information.
  // This is called during creation of each StoragePartition.
  virtual std::unique_ptr<ZoomLevelDelegate> CreateZoomLevelDelegate(
      const base::FilePath& partition_path) = 0;
#endif

  // Returns the path of the directory where this context's data is stored.
  virtual base::FilePath GetPath() = 0;

  // Return whether this context is off the record. Default is false.
  // Note that for Chrome this does not imply Incognito as Guest sessions are
  // also off the record.
  virtual bool IsOffTheRecord() = 0;

  // Returns the resource context.
  virtual ResourceContext* GetResourceContext() = 0;

  // Returns the DownloadManagerDelegate for this context. This will be called
  // once per context. The embedder owns the delegate and is responsible for
  // ensuring that it outlives DownloadManager. Note in particular that it is
  // unsafe to destroy the delegate in the destructor of a subclass of
  // BrowserContext, since it needs to be alive in ~BrowserContext.
  // It's valid to return nullptr.
  virtual DownloadManagerDelegate* GetDownloadManagerDelegate() = 0;

  // Returns the guest manager for this context.
  virtual BrowserPluginGuestManager* GetGuestManager() = 0;

  // Returns a special storage policy implementation, or nullptr.
  virtual storage::SpecialStoragePolicy* GetSpecialStoragePolicy() = 0;

  // Returns a push messaging service. The embedder owns the service, and is
  // responsible for ensuring that it outlives RenderProcessHost. It's valid to
  // return nullptr.
  virtual PushMessagingService* GetPushMessagingService() = 0;

  // Returns a storage notification service associated with that context,
  // nullptr otherwise. In the case that nullptr is returned, QuotaManager
  // and the rest of the storage layer will have no connection to the Chrome
  // layer for UI purposes.
  virtual StorageNotificationService* GetStorageNotificationService() = 0;

  // Returns the SSL host state decisions for this context. The context may
  // return nullptr, implementing the default exception storage strategy.
  virtual SSLHostStateDelegate* GetSSLHostStateDelegate() = 0;

  // Returns the PermissionControllerDelegate associated with this context if
  // any, nullptr otherwise.
  //
  // Note: if you want to check a permission status, you probably need
  // BrowserContext::GetPermissionController() instead.
  virtual PermissionControllerDelegate* GetPermissionControllerDelegate() = 0;

  // Returns the ClientHintsControllerDelegate associated with that context if
  // any, nullptr otherwise.
  virtual ClientHintsControllerDelegate* GetClientHintsControllerDelegate() = 0;

  // Returns the BackgroundFetchDelegate associated with that context if any,
  // nullptr otherwise.
  virtual BackgroundFetchDelegate* GetBackgroundFetchDelegate() = 0;

  // Returns the BackgroundSyncController associated with that context if any,
  // nullptr otherwise.
  virtual BackgroundSyncController* GetBackgroundSyncController() = 0;

  // Returns the BrowsingDataRemoverDelegate for this context. This will be
  // called once per context. It's valid to return nullptr.
  virtual BrowsingDataRemoverDelegate* GetBrowsingDataRemoverDelegate() = 0;

  // Returns a random salt string that is used for creating media device IDs.
  // Default implementation uses the BrowserContext's UniqueId.
  virtual std::string GetMediaDeviceIDSalt();

  // Returns the FileSystemAccessPermissionContext associated with this context
  // if any, nullptr otherwise.
  virtual FileSystemAccessPermissionContext*
  GetFileSystemAccessPermissionContext();

  // Returns the ContentIndexProvider associated with that context if any,
  // nullptr otherwise.
  virtual ContentIndexProvider* GetContentIndexProvider();

  // Returns true iff the sandboxed file system implementation should be disk
  // backed, even if this browser context is off the record. By default this
  // returns false, an embedded could override this to return true if for
  // example the off-the-record browser context is stored in a in-memory file
  // system anyway, in which case using the disk backed sandboxed file system
  // API implementation can give some benefits over the in-memory
  // implementation.
  virtual bool CanUseDiskWhenOffTheRecord();

  // Returns the VariationsClient associated with the context if any, or
  // nullptr if there isn't one.
  virtual variations::VariationsClient* GetVariationsClient();

  // Creates the media service for storing/retrieving video decoding performance
  // stats.  Exposed here rather than StoragePartition because all SiteInstances
  // should have similar decode performance and stats are not exposed to the web
  // directly, so privacy is not compromised.
  virtual std::unique_ptr<media::VideoDecodePerfHistory>
  CreateVideoDecodePerfHistory();

 private:
  // Please don't add more fields to BrowserContext.
  //
  // Ideally, BrowserContext would be a pure interface (only pure-virtual
  // methods and no fields), but currently BrowserContext and BrowserContextImpl
  // and BrowserContextDelegate are kind of mixed together in a single class.
  //
  // TODO(https://crbug.com/1179776): Evolve the Impl class into a
  // BrowserContextImpl in //content/browser/browser_context_impl.h / .cc
  // (Removing afterwards the Impl fwd-declaration, `impl_` field, `friend`
  // declaration and `impl` accessor below).
  class Impl;
  std::unique_ptr<Impl> impl_;
  friend class BackgroundSyncScheduler;
  Impl* impl() { return impl_.get(); }
};

}  // namespace content

#endif  // CONTENT_PUBLIC_BROWSER_BROWSER_CONTEXT_H_
