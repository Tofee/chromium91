// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef COMPONENTS_PAGE_INFO_PAGE_INFO_H_
#define COMPONENTS_PAGE_INFO_PAGE_INFO_H_

#include <string>
#include <vector>

#include "base/macros.h"
#include "build/build_config.h"
#include "components/browsing_data/content/local_shared_objects_container.h"
#include "components/content_settings/core/common/content_settings.h"
#include "components/content_settings/core/common/content_settings_types.h"
#include "components/password_manager/core/browser/password_manager_metrics_util.h"
#include "components/safe_browsing/buildflags.h"
#include "components/security_state/core/security_state.h"
#include "content/public/browser/web_contents_observer.h"
#include "ui/gfx/vector_icon_types.h"
#include "url/gurl.h"

namespace content {
class WebContents;
}

namespace content_settings {
class PageSpecificContentSettings;
}

namespace net {
class X509Certificate;
}

namespace permissions {
class ChooserContextBase;
}

namespace ui {
class Event;
}

class HostContentSettingsMap;
class PageInfoDelegate;
class PageInfoUI;
class PageInfoBubbleViewBrowserTest;

using password_manager::metrics_util::PasswordType;

// The |PageInfo| provides information about a website's permissions,
// connection state and its identity. It owns a UI that displays the
// information and allows users to change the permissions. |PageInfo|
// objects must be created on the heap. They destroy themselves after the UI is
// closed.
class PageInfo : public content::WebContentsObserver {
 public:
  // TODO(palmer): Figure out if it is possible to unify SiteConnectionStatus
  // and SiteIdentityStatus.
  //
  // Status of a connection to a website.
  enum SiteConnectionStatus {
    SITE_CONNECTION_STATUS_UNKNOWN = 0,  // No status available.
    SITE_CONNECTION_STATUS_ENCRYPTED,    // Connection is encrypted.
    SITE_CONNECTION_STATUS_INSECURE_PASSIVE_SUBRESOURCE,  // Non-secure passive
                                                          // content.
    SITE_CONNECTION_STATUS_INSECURE_FORM_ACTION,          // Non-secure form
                                                          // target.
    SITE_CONNECTION_STATUS_INSECURE_ACTIVE_SUBRESOURCE,   // Non-secure active
                                                          // content.
    SITE_CONNECTION_STATUS_UNENCRYPTED,      // Connection is not encrypted.
    SITE_CONNECTION_STATUS_ENCRYPTED_ERROR,  // Connection error occurred.
    SITE_CONNECTION_STATUS_INTERNAL_PAGE,    // Internal site.
    SITE_CONNECTION_STATUS_LEGACY_TLS,  // Connection used a legacy TLS version.
  };

  // Validation status of a website's identity.
  enum SiteIdentityStatus {
    // No status about the website's identity available.
    SITE_IDENTITY_STATUS_UNKNOWN = 0,
    // The website provided a valid certificate.
    SITE_IDENTITY_STATUS_CERT,
    // The website provided a valid EV certificate.
    SITE_IDENTITY_STATUS_EV_CERT,
    // Site identity could not be verified because the site did not provide a
    // certificate. This is the expected state for HTTP connections.
    SITE_IDENTITY_STATUS_NO_CERT,
    // An error occured while verifying the site identity.
    SITE_IDENTITY_STATUS_ERROR,
    // The site is a trusted internal chrome page.
    SITE_IDENTITY_STATUS_INTERNAL_PAGE,
    // The profile has accessed data using an administrator-provided
    // certificate, so the administrator might be able to intercept data.
    SITE_IDENTITY_STATUS_ADMIN_PROVIDED_CERT,
    // The website provided a valid certificate, but the certificate or chain
    // is using a deprecated signature algorithm.
    SITE_IDENTITY_STATUS_DEPRECATED_SIGNATURE_ALGORITHM,
  };

  // Safe Browsing status of a website.
  enum SafeBrowsingStatus {
    SAFE_BROWSING_STATUS_NONE = 0,
    // The website has been flagged by Safe Browsing as dangerous for
    // containing malware, social engineering, unwanted software, or password
    // reuse on a low reputation site.
    SAFE_BROWSING_STATUS_MALWARE,
    SAFE_BROWSING_STATUS_SOCIAL_ENGINEERING,
    SAFE_BROWSING_STATUS_UNWANTED_SOFTWARE,
    SAFE_BROWSING_STATUS_SAVED_PASSWORD_REUSE,
    SAFE_BROWSING_STATUS_SIGNED_IN_SYNC_PASSWORD_REUSE,
    SAFE_BROWSING_STATUS_SIGNED_IN_NON_SYNC_PASSWORD_REUSE,
    SAFE_BROWSING_STATUS_ENTERPRISE_PASSWORD_REUSE,
    SAFE_BROWSING_STATUS_BILLING,
  };

  // Events for UMA. Do not reorder or change! Exposed in header so enum is
  // accessible from test.
  enum SSLCertificateDecisionsDidRevoke {
    USER_CERT_DECISIONS_NOT_REVOKED = 0,
    USER_CERT_DECISIONS_REVOKED = 1,
    END_OF_SSL_CERTIFICATE_DECISIONS_DID_REVOKE_ENUM
  };

  // UMA statistics for PageInfo. Do not reorder or remove existing
  // fields. A Java counterpart will be generated for this enum.
  // GENERATED_JAVA_ENUM_PACKAGE: org.chromium.components.page_info
  // All values here should have corresponding entries in
  // WebsiteSettingsAction area of enums.xml.
  enum PageInfoAction {
    PAGE_INFO_OPENED = 0,
    // No longer used; indicated actions for the old version of Page Info that
    // had a "Permissions" tab and a "Connection" tab.
    // PAGE_INFO_PERMISSIONS_TAB_SELECTED = 1,
    // PAGE_INFO_CONNECTION_TAB_SELECTED = 2,
    // PAGE_INFO_CONNECTION_TAB_SHOWN_IMMEDIATELY = 3,
    PAGE_INFO_COOKIES_DIALOG_OPENED = 4,
    PAGE_INFO_CHANGED_PERMISSION = 5,
    PAGE_INFO_CERTIFICATE_DIALOG_OPENED = 6,
    // No longer used; indicated a UI viewer for SCTs.
    // PAGE_INFO_TRANSPARENCY_VIEWER_OPENED = 7,
    PAGE_INFO_CONNECTION_HELP_OPENED = 8,
    PAGE_INFO_SITE_SETTINGS_OPENED = 9,
    PAGE_INFO_SECURITY_DETAILS_OPENED = 10,
    PAGE_INFO_COOKIES_ALLOWED_FOR_SITE = 11,
    PAGE_INFO_COOKIES_BLOCKED_FOR_SITE = 12,
    PAGE_INFO_COOKIES_CLEARED = 13,
    PAGE_INFO_PERMISSION_DIALOG_OPENED = 14,
    PAGE_INFO_PERMISSIONS_CLEARED = 15,
    // No longer used; indicated permission change but was a duplicate metric.
    // PAGE_INFO_PERMISSIONS_CHANGED = 16,
    PAGE_INFO_FORGET_SITE_OPENED = 17,
    PAGE_INFO_FORGET_SITE_CLEARED = 18,
    PAGE_INFO_HISTORY_OPENED = 19,
    PAGE_INFO_COUNT
  };

  struct ChooserUIInfo {
    ContentSettingsType content_settings_type;
    int description_string_id;
    int allowed_by_policy_description_string_id;
    int delete_tooltip_string_id;
  };

  // |PermissionInfo| contains information about a single permission |type| for
  // the current website.
  struct PermissionInfo {
    PermissionInfo() = default;
    // Site permission |type|.
    ContentSettingsType type = ContentSettingsType::DEFAULT;
    // The current value for the permission |type| (e.g. ALLOW or BLOCK).
    ContentSetting setting = CONTENT_SETTING_DEFAULT;
    // The global default settings for this permission |type|.
    ContentSetting default_setting = CONTENT_SETTING_DEFAULT;
    // The settings source e.g. user, extensions, policy, ... .
    content_settings::SettingSource source =
        content_settings::SETTING_SOURCE_NONE;
    bool is_one_time = false;
  };

  // Creates a PageInfo for the passed |url| using the given |ssl| status
  // object to determine the status of the site's connection.
  PageInfo(std::unique_ptr<PageInfoDelegate> delegate,
           content::WebContents* web_contents,
           const GURL& url);
  ~PageInfo() override;

  // Checks whether this permission is currently the factory default, as set by
  // Chrome. Specifically, that the following three conditions are true:
  //   - The current active setting comes from the default or pref provider.
  //   - The setting is the factory default setting (as opposed to a global
  //     default setting set by the user).
  //   - The setting is a wildcard setting applying to all origins (which can
  //     only be set from the default provider).
  static bool IsPermissionFactoryDefault(const PermissionInfo& info,
                                         bool is_incognito);

  // Returns whether this page info is for an internal page.
  static bool IsFileOrInternalPage(const GURL& url);

  // Initializes UI state that is dependent on having access to the PageInfoUI
  // object associated with this object. This explicit post-construction
  // initialization step is necessary as PageInfoUI subclasses create this
  // object and also may invoke it as part of the initialization flow that
  // occurs in this method. If this initialization flow was done as part of
  // PageInfo's constructor, those subclasses would not have their PageInfo
  // member set and crashes would ensue.
  void InitializeUiState(PageInfoUI* ui);

  // This method is called to update the presenter's security state and forwards
  // that change on to the UI to be redrawn.
  void UpdateSecurityState();

  void RecordPageInfoAction(PageInfoAction action);

  void UpdatePermissions();

  // This method is called when ever a permission setting is changed.
  void OnSitePermissionChanged(ContentSettingsType type,
                               ContentSetting value,
                               bool is_one_time);

  // This method is called whenever access to an object is revoked.
  void OnSiteChosenObjectDeleted(const ChooserUIInfo& ui_info,
                                 const base::Value& object);

  // This method is called by the UI when the UI is closing.
  // If specified, |reload_prompt| is set to whether closing the UI resulted in
  // a prompt to the user to reload the page.
  void OnUIClosing(bool* reload_prompt);

  // This method is called when the revoke SSL error bypass button is pressed.
  void OnRevokeSSLErrorBypassButtonPressed();

  // Handles opening the link to show more site settings and records the event.
  void OpenSiteSettingsView();

  // Handles opening the cookies dialog and records the event.
  void OpenCookiesDialog();

  // Handles opening the certificate dialog and records the event.
  void OpenCertificateDialog(net::X509Certificate* certificate);

  // Handles opening the safery tip help center page.
  void OpenSafetyTipHelpCenterPage();

  // Handles opening the connection help center page and records the event.
  void OpenConnectionHelpCenterPage(const ui::Event& event);

  // This method is called when the user pressed "Change password" button.
  void OnChangePasswordButtonPressed();

  // This method is called when the user pressed "Mark as legitimate" button.
  void OnAllowlistPasswordReuseButtonPressed();

  // Return a pointer to the ChooserContextBase corresponding to the
  // content settings type, |type|. Returns nullptr for content settings
  // for which there's no ChooserContextBase.
  permissions::ChooserContextBase* GetChooserContextFromUIInfo(
      const ChooserUIInfo& ui_info) const;

  // Accessors.
  const SiteConnectionStatus& site_connection_status() const {
    return site_connection_status_;
  }

  const GURL& site_url() const { return site_url_; }

  const SiteIdentityStatus& site_identity_status() const {
    return site_identity_status_;
  }

  const SafeBrowsingStatus& safe_browsing_status() const {
    return safe_browsing_status_;
  }

 private:
  FRIEND_TEST_ALL_PREFIXES(PageInfoTest,
                           NonFactoryDefaultAndRecentlyChangedPermissionsShown);
  FRIEND_TEST_ALL_PREFIXES(PageInfoTest, IncognitoPermissionsEmptyByDefault);
  FRIEND_TEST_ALL_PREFIXES(PageInfoTest, IncognitoPermissionsDontShowAsk);
  friend class PageInfoBubbleViewBrowserTest;

  // Populates this object's UI state with provided security context. This
  // function does not update visible UI-- that's part of Present*().
  void ComputeUIInputs(const GURL& url);

  // Sets (presents) the information about the site's permissions in the |ui_|.
  void PresentSitePermissions();

  // Sets (presents) the information about the site's data in the |ui_|.
  void PresentSiteData();

  // Sets (presents) the information about the site's identity and connection
  // in the |ui_|.
  void PresentSiteIdentity();

  // Presents feature related info in the |ui_|; like, if VR content is being
  // presented in a headset.
  void PresentPageFeatureInfo();

#if BUILDFLAG(FULL_SAFE_BROWSING)
  // Records a password reuse event. If FULL_SAFE_BROWSING is defined, this
  // function WILL record an event. Callers should check conditions beforehand.
  void RecordPasswordReuseEvent();
#endif

  // Helper function to get the |HostContentSettingsMap| associated with
  // |PageInfo|.
  HostContentSettingsMap* GetContentSettings() const;

  // Helper function to get the Safe Browsing status and details by malicious
  // content status.
  // TODO(jdeblasio): Eliminate this and just use MaliciousContentStatus?
  void GetSafeBrowsingStatusByMaliciousContentStatus(
      security_state::MaliciousContentStatus malicious_content_status,
      PageInfo::SafeBrowsingStatus* status,
      std::u16string* details);

  // Retrieves all the permissions that are shown in Page Info.
  // Exposed for testing.
  static std::vector<ContentSettingsType> GetAllPermissionsForTesting();

  // Returns PageSpecificContentSettings for the observed WebContents if
  // present, nullptr otherwise.
  content_settings::PageSpecificContentSettings*
  GetPageSpecificContentSettings() const;

  // Whether the content setting of type |type| has changed via Page Info UI.
  bool HasContentSettingChangedViaPageInfo(ContentSettingsType type);

  // Notifies the delegate that the content setting of type |type| has changed
  // via Page Info UI.
  void ContentSettingChangedViaPageInfo(ContentSettingsType type);

  // Get counts of allowed and blocked cookies.
  int GetFirstPartyAllowedCookiesCount(const GURL& site_url);
  int GetFirstPartyBlockedCookiesCount(const GURL& site_url);
  int GetThirdPartyAllowedCookiesCount(const GURL& site_url);
  int GetThirdPartyBlockedCookiesCount(const GURL& site_url);

  // The page info UI displays information and controls for site-
  // specific data (local stored objects like cookies), site-specific
  // permissions (location, pop-up, plugin, etc. permissions) and site-specific
  // information (identity, connection status, etc.).
  PageInfoUI* ui_;

  // The delegate allows the embedder to customize |PageInfo|'s behavior.
  std::unique_ptr<PageInfoDelegate> delegate_;

  // The flag that controls whether an infobar is displayed after the website
  // settings UI is closed or not.
  bool show_info_bar_;

  // The Omnibox URL of the website for which to display site permissions and
  // site information.
  GURL site_url_;

  // Status of the website's identity verification check.
  SiteIdentityStatus site_identity_status_;

  // Safe Browsing status of the website.
  SafeBrowsingStatus safe_browsing_status_;

  // Safety tip info of the website. Set regardless of whether the feature is
  // enabled to show the UI.
  security_state::SafetyTipInfo safety_tip_info_;

  // For secure connection |certificate_| is set to the server certificate.
  scoped_refptr<net::X509Certificate> certificate_;

  // Status of the connection to the website.
  SiteConnectionStatus site_connection_status_;

  // TODO(markusheintz): Move the creation of all the std::u16string typed UI
  // strings below to the corresponding UI code, in order to prevent
  // unnecessary UTF-8 string conversions.

#if defined(OS_ANDROID)
  // Details about the website's identity. If the website's identity has been
  // verified then |identity_status_description_android_| contains who verified
  // the identity. This string will be displayed in the UI.
  std::u16string identity_status_description_android_;
#endif

  // Set when the user has explicitly bypassed an SSL error for this host or
  // explicitly denied it (the latter of which is not currently possible in the
  // Chrome UI). When |show_ssl_decision_revoke_button| is true, the connection
  // area of the page info will include an option for the user to revoke their
  // decision to bypass the SSL error for this host.
  bool show_ssl_decision_revoke_button_;

  // Details about the connection to the website. In case of an encrypted
  // connection |site_connection_details_| contains encryption details, like
  // encryption strength and ssl protocol version. This string will be
  // displayed in the UI.
  std::u16string site_connection_details_;

  // For websites that provided an EV certificate |orgainization_name_|
  // contains the organization name of the certificate. In all other cases
  // |organization_name| is an empty string. This string will be displayed in
  // the UI.
  std::u16string organization_name_;

  bool did_revoke_user_ssl_decisions_;

  security_state::SecurityLevel security_level_;

  security_state::VisibleSecurityState visible_security_state_for_metrics_;

  // Set when the user ignored the password reuse modal warning dialog. When
  // |show_change_password_buttons_| is true, the page identity area of the page
  // info will include buttons to change corresponding password, and to
  // whitelist current site.
  bool show_change_password_buttons_;

  // The time the Page Info UI is opened, for measuring total time open.
  base::TimeTicks start_time_;

  // Records whether the user interacted with the bubble beyond opening it.
  bool did_perform_action_;

  // Description of the Safe Browsing status. Non-empty if
  // MaliciousContentStatus isn't NONE.
  std::u16string safe_browsing_details_;

  DISALLOW_COPY_AND_ASSIGN(PageInfo);
};

#endif  // COMPONENTS_PAGE_INFO_PAGE_INFO_H_
