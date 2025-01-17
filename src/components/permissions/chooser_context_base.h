// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef COMPONENTS_PERMISSIONS_CHOOSER_CONTEXT_BASE_H_
#define COMPONENTS_PERMISSIONS_CHOOSER_CONTEXT_BASE_H_

#include <memory>
#include <string>
#include <vector>

#include "base/observer_list.h"
#include "base/observer_list_types.h"
#include "base/optional.h"
#include "base/values.h"
#include "components/content_settings/core/common/content_settings.h"
#include "components/content_settings/core/common/content_settings_types.h"
#include "components/keyed_service/core/keyed_service.h"
#include "url/gurl.h"

class HostContentSettingsMap;

namespace url {
class Origin;
}

namespace permissions {

// This is the base class for services that manage any type of permission that
// is granted through a chooser-style UI instead of a simple allow/deny prompt.
// Subclasses must define the structure of the objects that are stored.
class ChooserContextBase : public KeyedService {
 public:
  struct Object {
    Object(const url::Origin& origin,
           base::Value value,
           content_settings::SettingSource source,
           bool incognito);
    ~Object();

    GURL origin;
    base::Value value;
    content_settings::SettingSource source;
    bool incognito;
  };

  // This observer can be used to be notified of changes to the permission of a
  // chooser object.
  class PermissionObserver : public base::CheckedObserver {
   public:
    // Notify observers that an object permission changed for the chooser
    // context represented by |guard_content_settings_type| and
    // |data_content_settings_type|.
    virtual void OnChooserObjectPermissionChanged(
        ContentSettingsType guard_content_settings_type,
        ContentSettingsType data_content_settings_type);
    // Notify obsever that an object permission was revoked for |origin|.
    virtual void OnPermissionRevoked(const url::Origin& origin);
  };

  void AddObserver(PermissionObserver* observer);
  void RemoveObserver(PermissionObserver* observer);

  ChooserContextBase(ContentSettingsType guard_content_settings_type,
                     ContentSettingsType data_content_settings_type,
                     HostContentSettingsMap* host_content_settings_map);
  ~ChooserContextBase() override;

  // Checks whether |origin| can request permission to access objects. This is
  // done by checking |guard_content_settings_type_| which will usually be "ask"
  // by default but could be set by the user or group policy.
  bool CanRequestObjectPermission(const url::Origin& origin);

  // Returns the object corresponding to |key| that |origin| has been granted
  // permission to access. This method should only be called if
  // |GetKeyForObject()| is overridden to return sensible keys.
  //
  // This method may be extended by a subclass to return
  // objects not stored in |host_content_settings_map_|.
  virtual std::unique_ptr<Object> GetGrantedObject(const url::Origin& origin,
                                                   const base::StringPiece key);

  // Returns the list of objects that |origin| has been granted permission to
  // access. This method may be extended by a subclass to return objects not
  // stored in |host_content_settings_map_|.
  virtual std::vector<std::unique_ptr<Object>> GetGrantedObjects(
      const url::Origin& origin);

  // Returns the set of all objects that any origin has been granted permission
  // to access.
  //
  // This method may be extended by a subclass to return objects not stored in
  // |host_content_settings_map_|.
  virtual std::vector<std::unique_ptr<Object>> GetAllGrantedObjects();

  // Grants |origin| access to |object| by writing it into
  // |host_content_settings_map_|.
  // TODO(https://crbug.com/1189682): Combine GrantObjectPermission and
  // UpdateObjectPermission methods into key-based GrantOrUpdateObjectPermission
  // once backend is updated to make key-based methods more efficient.
  void GrantObjectPermission(const url::Origin& origin, base::Value object);

  // Updates |old_object| with |new_object| for |origin|, and writes the value
  // into |host_content_settings_map_|.
  void UpdateObjectPermission(const url::Origin& origin,
                              const base::Value& old_object,
                              base::Value new_object);

  // Revokes |origin|'s permission to access |object|.
  //
  // This method may be extended by a subclass to revoke permission to access
  // objects returned by GetGrantedObjects but not stored in
  // |host_content_settings_map_|.
  // TODO(https://crbug.com/1189682): Remove this method once backend is updated
  // to make key-based methods more efficient.
  virtual void RevokeObjectPermission(const url::Origin& origin,
                                      const base::Value& object);

  // Revokes |origin|'s permission to access the object corresponding to |key|.
  // This method should only be called if |GetKeyForObject()| is overridden to
  // return sensible keys.
  //
  // This method may be extended by a subclass to revoke permission to access
  // objects returned by GetGrantedObjects but not stored in
  // |host_content_settings_map_|.
  virtual void RevokeObjectPermission(const url::Origin& origin,
                                      const base::StringPiece key);

  // Returns whether |origin| has granted objects.
  //
  // This method may be extended by a subclass to include permission to access
  // objects returned by GetGrantedObjects but not stored in
  // |host_content_settings_map_|.
  virtual bool HasGrantedObjects(const url::Origin& origin);

  // Returns a string which is used to uniquely identify this object. If this
  // method is extended by a subclass to return unique keys, the new key-based
  // techniques will be used. Otherwise, class methods will fall back to the
  // legacy behavior of matching via an object.
  // TODO(https://crbug.com/1189682): This should be made fully virtual once
  // backend is updated to make key-based methods more efficient.
  virtual std::string GetKeyForObject(const base::Value& object);

  // Validates the structure of an object read from
  // |host_content_settings_map_|.
  virtual bool IsValidObject(const base::Value& object) = 0;

  // Gets the human-readable name for a given object.
  virtual std::u16string GetObjectDisplayName(const base::Value& object) = 0;

 protected:
  // TODO(odejesush): Use this method in all derived classes instead of using a
  // member variable to store this state.
  bool IsOffTheRecord();
  void NotifyPermissionChanged();
  void NotifyPermissionRevoked(const url::Origin& origin);

  const ContentSettingsType guard_content_settings_type_;
  const ContentSettingsType data_content_settings_type_;
  base::ObserverList<PermissionObserver> permission_observer_list_;

 private:
  base::Value GetWebsiteSetting(const url::Origin& origin,
                                content_settings::SettingInfo* info);
  void SetWebsiteSetting(const url::Origin& origin, base::Value value);

  HostContentSettingsMap* const host_content_settings_map_;
};

}  // namespace permissions

#endif  // COMPONENTS_PERMISSIONS_CHOOSER_CONTEXT_BASE_H_
