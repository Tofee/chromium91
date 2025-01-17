// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ASH_APP_LIST_MODEL_APP_LIST_ITEM_H_
#define ASH_APP_LIST_MODEL_APP_LIST_ITEM_H_

#include <stddef.h>

#include <map>
#include <memory>
#include <string>
#include <utility>

#include "ash/app_list/model/app_list_model_export.h"
#include "ash/public/cpp/app_list/app_list_types.h"
#include "ash/public/cpp/shelf_types.h"
#include "base/macros.h"
#include "base/observer_list.h"
#include "components/sync/model/string_ordinal.h"
#include "ui/gfx/image/image_skia.h"

namespace ash {
enum class AppListConfigType;
class AppListControllerImpl;
class AppListItemList;
class AppListItemListTest;
class AppListItemObserver;
class AppListModel;

// AppListItem provides icon and title to be shown in a AppListItemView
// and action to be executed when the AppListItemView is activated.
class APP_LIST_MODEL_EXPORT AppListItem {
 public:
  using AppListItemMetadata = ash::AppListItemMetadata;

  explicit AppListItem(const std::string& id);
  virtual ~AppListItem();

  void SetIcon(AppListConfigType config_type, const gfx::ImageSkia& icon);
  const gfx::ImageSkia& GetIcon(AppListConfigType config_type) const;

  // Setter and getter for the default app list item icon. Used as a base to
  // generate appropriate app list item icon for an app list config if an icon
  // for the config has not been set using `SetIcon()`.
  void SetDefaultIcon(const gfx::ImageSkia& icon);
  const gfx::ImageSkia& GetDefaultIcon() const;

  void SetNotificationBadgeColor(const SkColor color);

  const std::string& GetDisplayName() const {
    return short_name_.empty() ? name() : short_name_;
  }

  const std::string& name() const { return metadata_->name; }
  // Should only be used in tests; otherwise use GetDisplayName().
  const std::string& short_name() const { return short_name_; }

  bool IsInFolder() const { return !folder_id().empty(); }

  const std::string& id() const { return metadata_->id; }
  const std::string& folder_id() const { return metadata_->folder_id; }
  const syncer::StringOrdinal& position() const { return metadata_->position; }

  void SetMetadata(std::unique_ptr<AppListItemMetadata> metadata) {
    metadata_ = std::move(metadata);
  }
  const AppListItemMetadata* GetMetadata() const { return metadata_.get(); }
  std::unique_ptr<AppListItemMetadata> CloneMetadata() const {
    return std::make_unique<AppListItemMetadata>(*metadata_);
  }

  void AddObserver(AppListItemObserver* observer);
  void RemoveObserver(AppListItemObserver* observer);

  // Returns a static const char* identifier for the subclass (defaults to "").
  // Pointers can be compared for quick type checking.
  virtual const char* GetItemType() const;

  // Returns the item matching |id| contained in this item (e.g. if the item is
  // a folder), or nullptr if the item was not found or this is not a container.
  virtual AppListItem* FindChildItem(const std::string& id);

  // Returns the number of child items if it has any (e.g. is a folder) or 0.
  virtual size_t ChildItemCount() const;

  std::string ToDebugString() const;

  bool is_folder() const { return metadata_->is_folder; }

  void set_is_page_break(bool is_page_break) {
    metadata_->is_page_break = is_page_break;
  }
  bool is_page_break() const { return metadata_->is_page_break; }

  bool has_notification_badge() const { return has_notification_badge_; }

  SkColor notification_badge_color() const { return notification_badge_color_; }

  void UpdateNotificationBadgeForTesting(bool has_badge) {
    UpdateNotificationBadge(has_badge);
  }

  AppStatus app_status() const { return metadata_->app_status; }

  void UpdateAppStatusForTesting(AppStatus app_status) {
    metadata_->app_status = app_status;
  }

 protected:
  // Subclasses also have mutable access to the metadata ptr.
  AppListItemMetadata* metadata() { return metadata_.get(); }

  friend class AppListControllerImpl;
  friend class AppListItemList;
  friend class AppListItemListTest;
  friend class AppListModel;

  // These should only be called by AppListModel or in tests so that name
  // changes trigger update notifications.

  // Sets the full name of the item. Clears any shortened name.
  void SetName(const std::string& name);

  // Sets the full name and an optional shortened name of the item (e.g. to use
  // if the full name is too long to fit in a view).
  void SetNameAndShortName(const std::string& name,
                           const std::string& short_name);

  // Updates whether the notification badge is shown on the view.
  void UpdateNotificationBadge(bool has_badge);

  void set_position(const syncer::StringOrdinal& new_position) {
    DCHECK(new_position.IsValid());
    metadata_->position = new_position;
  }

  void set_folder_id(const std::string& folder_id) {
    metadata_->folder_id = folder_id;
  }

  void set_is_folder(bool is_folder) { metadata_->is_folder = is_folder; }

 private:
  friend class AppListModelTest;

  std::unique_ptr<AppListItemMetadata> metadata_;

  // Contains icons for AppListConfigTypes different than kShared. For kShared
  // config type, the item will always use the icon provided by |metadata_|.
  // This is currently used for folder icons only (which are all generated in
  // ash), when app_list_features::kScalableAppList feature is enabled.
  std::map<AppListConfigType, gfx::ImageSkia> per_config_icons_;

  // A shortened name for the item, used for display.
  std::string short_name_;

  // Whether this item currently has a notification badge that should be shown.
  bool has_notification_badge_ = false;

  // The color for the notification badge displayed over the app icon.
  SkColor notification_badge_color_ = SK_ColorWHITE;

  base::ObserverList<AppListItemObserver> observers_;

  DISALLOW_COPY_AND_ASSIGN(AppListItem);
};

}  // namespace ash

#endif  // ASH_APP_LIST_MODEL_APP_LIST_ITEM_H_
