// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ui/gtk/gtk_util.h"

#include <gdk/gdk.h>
#include <gtk/gtk.h>
#include <locale.h>
#include <stddef.h>

#include <memory>

#include "base/command_line.h"
#include "base/compiler_specific.h"
#include "base/environment.h"
#include "base/strings/strcat.h"
#include "base/strings/string_split.h"
#include "base/strings/string_tokenizer.h"
#include "base/strings/string_util.h"
#include "ui/aura/window_tree_host.h"
#include "ui/base/accelerators/accelerator.h"
#include "ui/events/event.h"
#include "ui/events/event_utils.h"
#include "ui/events/keycodes/dom/keycode_converter.h"
#include "ui/gfx/color_palette.h"
#include "ui/gfx/color_utils.h"
#include "ui/gfx/geometry/size.h"
#include "ui/gfx/native_widget_types.h"
#include "ui/gtk/gtk_ui.h"
#include "ui/gtk/gtk_ui_delegate.h"
#include "ui/native_theme/common_theme.h"
#include "ui/ozone/public/ozone_platform.h"
#include "ui/views/linux_ui/linux_ui.h"

using base::StrCat;

namespace gtk {

#if BUILDFLAG(GTK_VERSION) >= 4
const char kGtkCSSMenu[] = "#popover.background.menu #contents";
const char kGtkCSSMenuItem[] = "#modelbutton.flat";
const char kGtkCSSMenuScrollbar[] = "#scrollbar #range";
#else
const char kGtkCSSMenu[] = "GtkMenu#menu";
const char kGtkCSSMenuItem[] = "GtkMenuItem#menuitem";
const char kGtkCSSMenuScrollbar[] = "GtkScrollbar#scrollbar #trough";
#endif

namespace {

const char kAuraTransientParent[] = "aura-transient-parent";

GtkCssContext GetTooltipContext() {
  return AppendCssNodeToStyleContext(
      {}, GtkCheckVersion(3, 20) ? "#tooltip.background"
                                 : "GtkWindow#window.background.tooltip");
}

void CommonInitFromCommandLine(const base::CommandLine& command_line) {
  // Callers should have already called setlocale(LC_ALL, "") and
  // setlocale(LC_NUMERIC, "C") by now. Chrome does this in
  // service_manager::Main.
  DCHECK_EQ(strcmp(setlocale(LC_NUMERIC, nullptr), "C"), 0);
  // This prevents GTK from calling setlocale(LC_ALL, ""), which potentially
  // overwrites the LC_NUMERIC locale to something other than "C".
  gtk_disable_setlocale();
  GtkInit(command_line.argv());
}

GdkModifierType GetImeFlags(const ui::KeyEvent& key_event) {
  auto* properties = key_event.properties();
  if (!properties)
    return static_cast<GdkModifierType>(0);
  auto it = properties->find(ui::kPropertyKeyboardImeFlag);
  DCHECK(it == properties->end() || it->second.size() == 1);
  uint8_t flags = (it != properties->end()) ? it->second[0] : 0;
  return static_cast<GdkModifierType>(flags
                                      << ui::kPropertyKeyboardImeFlagOffset);
}

GtkCssContext AppendCssNodeToStyleContextImpl(
    GtkCssContext context,
    GType gtype,
    const std::string& name,
    const std::string& object_name,
    const std::vector<std::string>& classes,
    GtkStateFlags state,
    float scale) {
  if (GtkCheckVersion(4)) {
    // GTK_TYPE_BOX is used instead of GTK_TYPE_WIDGET because:
    // 1. Widgets are abstract and cannot be created directly.
    // 2. The widget must be a container type so that it unrefs child widgets
    //    on destruction.
    auto* widget_object = object_name.empty()
                              ? g_object_new(GTK_TYPE_BOX, nullptr)
                              : g_object_new(GTK_TYPE_BOX, "css-name",
                                             object_name.c_str(), nullptr);
    auto widget = TakeGObject(GTK_WIDGET(widget_object));

    if (!name.empty())
      gtk_widget_set_name(widget, name.c_str());

    std::vector<const char*> css_classes;
    css_classes.reserve(classes.size() + 1);
    for (const auto& css_class : classes)
      css_classes.push_back(css_class.c_str());
    css_classes.push_back(nullptr);
    gtk_widget_set_css_classes(widget, css_classes.data());

    gtk_widget_set_state_flags(widget, state, false);

    if (context)
      gtk_widget_set_parent(widget, context.widget());

    gtk_style_context_set_scale(gtk_widget_get_style_context(widget), scale);

    return GtkCssContext(widget, context ? context.root() : widget);
  } else {
    GtkWidgetPath* path =
        context ? gtk_widget_path_copy(gtk_style_context_get_path(context))
                : gtk_widget_path_new();
    gtk_widget_path_append_type(path, gtype);

    if (!object_name.empty()) {
      if (GtkCheckVersion(3, 20))
        gtk_widget_path_iter_set_object_name(path, -1, object_name.c_str());
      else
        gtk_widget_path_iter_add_class(path, -1, object_name.c_str());
    }

    if (!name.empty())
      gtk_widget_path_iter_set_name(path, -1, name.c_str());

    for (const auto& css_class : classes)
      gtk_widget_path_iter_add_class(path, -1, css_class.c_str());

    if (GtkCheckVersion(3, 14))
      gtk_widget_path_iter_set_state(path, -1, state);

    GtkCssContext child_context(TakeGObject(gtk_style_context_new()));
    gtk_style_context_set_path(child_context, path);
    if (GtkCheckVersion(3, 14)) {
      gtk_style_context_set_state(child_context, state);
    } else {
      GtkStateFlags child_state = state;
      if (context) {
        child_state = static_cast<GtkStateFlags>(
            child_state | gtk_style_context_get_state(context));
      }
      gtk_style_context_set_state(child_context, child_state);
    }

    gtk_style_context_set_scale(child_context, scale);

    gtk_style_context_set_parent(child_context, context);

    gtk_widget_path_unref(path);
    return GtkCssContext(child_context);
  }
}

GtkWidget* CreateDummyWindow() {
#if BUILDFLAG(GTK_VERSION) >= 4
  GtkWidget* window = gtk_window_new();
#else
  GtkWidget* window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
#endif
  gtk_widget_realize(window);
  return window;
}

}  // namespace

void GtkInitFromCommandLine(const base::CommandLine& command_line) {
  CommonInitFromCommandLine(command_line);
}

void SetGtkTransientForAura(GtkWidget* dialog, aura::Window* parent) {
  if (!parent || !parent->GetHost())
    return;

  gtk_widget_realize(dialog);
  gfx::AcceleratedWidget parent_id = parent->GetHost()->GetAcceleratedWidget();
  GtkUi::GetDelegate()->SetGtkWidgetTransientFor(dialog, parent_id);

  // We also set the |parent| as a property of |dialog|, so that we can unlink
  // the two later.
  g_object_set_data(G_OBJECT(dialog), kAuraTransientParent, parent);
}

aura::Window* GetAuraTransientParent(GtkWidget* dialog) {
  return reinterpret_cast<aura::Window*>(
      g_object_get_data(G_OBJECT(dialog), kAuraTransientParent));
}

void ClearAuraTransientParent(GtkWidget* dialog, aura::Window* parent) {
  g_object_set_data(G_OBJECT(dialog), kAuraTransientParent, nullptr);
  GtkUi::GetDelegate()->ClearTransientFor(
      parent->GetHost()->GetAcceleratedWidget());
}

void ParseButtonLayout(const std::string& button_string,
                       std::vector<views::FrameButton>* leading_buttons,
                       std::vector<views::FrameButton>* trailing_buttons) {
  leading_buttons->clear();
  trailing_buttons->clear();
  bool left_side = true;
  base::StringTokenizer tokenizer(button_string, ":,");
  tokenizer.set_options(base::StringTokenizer::RETURN_DELIMS);
  while (tokenizer.GetNext()) {
    if (tokenizer.token_is_delim()) {
      if (*tokenizer.token_begin() == ':')
        left_side = false;
    } else {
      base::StringPiece token = tokenizer.token_piece();
      if (token == "minimize") {
        (left_side ? leading_buttons : trailing_buttons)
            ->push_back(views::FrameButton::kMinimize);
      } else if (token == "maximize") {
        (left_side ? leading_buttons : trailing_buttons)
            ->push_back(views::FrameButton::kMaximize);
      } else if (token == "close") {
        (left_side ? leading_buttons : trailing_buttons)
            ->push_back(views::FrameButton::kClose);
      }
    }
  }
}

CairoSurface::CairoSurface(SkBitmap& bitmap)
    : surface_(cairo_image_surface_create_for_data(
          static_cast<unsigned char*>(bitmap.getAddr(0, 0)),
          CAIRO_FORMAT_ARGB32,
          bitmap.width(),
          bitmap.height(),
          cairo_format_stride_for_width(CAIRO_FORMAT_ARGB32, bitmap.width()))),
      cairo_(cairo_create(surface_)) {}

CairoSurface::CairoSurface(const gfx::Size& size)
    : surface_(cairo_image_surface_create(CAIRO_FORMAT_ARGB32,
                                          size.width(),
                                          size.height())),
      cairo_(cairo_create(surface_)) {
  DCHECK(cairo_surface_status(surface_) == CAIRO_STATUS_SUCCESS);
  // Clear the surface.
  cairo_save(cairo_);
  cairo_set_source_rgba(cairo_, 0, 0, 0, 0);
  cairo_set_operator(cairo_, CAIRO_OPERATOR_SOURCE);
  cairo_paint(cairo_);
  cairo_restore(cairo_);
}

CairoSurface::~CairoSurface() {
  cairo_destroy(cairo_);
  cairo_surface_destroy(surface_);
}

SkColor CairoSurface::GetAveragePixelValue(bool frame) {
  cairo_surface_flush(surface_);
  SkColor* data =
      reinterpret_cast<SkColor*>(cairo_image_surface_get_data(surface_));
  int width = cairo_image_surface_get_width(surface_);
  int height = cairo_image_surface_get_height(surface_);
  DCHECK(4 * width == cairo_image_surface_get_stride(surface_));
  long a = 0, r = 0, g = 0, b = 0;
  unsigned int max_alpha = 0;
  for (int i = 0; i < width * height; i++) {
    SkColor color = data[i];
    max_alpha = std::max(SkColorGetA(color), max_alpha);
    a += SkColorGetA(color);
    r += SkColorGetR(color);
    g += SkColorGetG(color);
    b += SkColorGetB(color);
  }
  if (a == 0)
    return SK_ColorTRANSPARENT;
  return SkColorSetARGB(frame ? max_alpha : a / (width * height), r * 255 / a,
                        g * 255 / a, b * 255 / a);
}

GtkCssContext::GtkCssContext(GtkWidget* widget, GtkWidget* root)
    : widget_(widget), root_(WrapGObject(root)) {
  DCHECK(GtkCheckVersion(4));
}

GtkCssContext::GtkCssContext(GtkStyleContext* context)
    : context_(WrapGObject(context)) {
  DCHECK(!GtkCheckVersion(4));
}

GtkCssContext::GtkCssContext() = default;
GtkCssContext::GtkCssContext(const GtkCssContext&) = default;
GtkCssContext::GtkCssContext(GtkCssContext&&) = default;
GtkCssContext& GtkCssContext::operator=(const GtkCssContext&) = default;
GtkCssContext& GtkCssContext::operator=(GtkCssContext&&) = default;
GtkCssContext::~GtkCssContext() = default;

GtkCssContext::operator GtkStyleContext*() {
  if (GtkCheckVersion(4))
    return widget_ ? gtk_widget_get_style_context(widget_) : nullptr;
  return context_;
}

GtkCssContext GtkCssContext::GetParent() {
  if (GtkCheckVersion(4)) {
    return GtkCssContext(WrapGObject(gtk_widget_get_parent(widget_)),
                         root_ == widget_ ? ScopedGObject<GtkWidget>() : root_);
  }
  return GtkCssContext(WrapGObject(gtk_style_context_get_parent(context_)));
}

GtkWidget* GtkCssContext::widget() {
  DCHECK(GtkCheckVersion(4));
  return widget_;
}

GtkWidget* GtkCssContext::root() {
  DCHECK(GtkCheckVersion(4));
  return root_;
}

GtkStateFlags StateToStateFlags(ui::NativeTheme::State state) {
  switch (state) {
    case ui::NativeTheme::kDisabled:
      return GTK_STATE_FLAG_INSENSITIVE;
    case ui::NativeTheme::kHovered:
      return GTK_STATE_FLAG_PRELIGHT;
    case ui::NativeTheme::kNormal:
      return GTK_STATE_FLAG_NORMAL;
    case ui::NativeTheme::kPressed:
      return static_cast<GtkStateFlags>(GTK_STATE_FLAG_PRELIGHT |
                                        GTK_STATE_FLAG_ACTIVE);
    default:
      NOTREACHED();
      return GTK_STATE_FLAG_NORMAL;
  }
}

SkColor GdkRgbaToSkColor(const GdkRGBA& color) {
  return SkColorSetARGB(color.alpha * 255, color.red * 255, color.green * 255,
                        color.blue * 255);
}

NO_SANITIZE("cfi-icall")
GtkCssContext AppendCssNodeToStyleContext(GtkCssContext context,
                                          const std::string& css_node) {
  enum {
    CSS_TYPE,
    CSS_NAME,
    CSS_OBJECT_NAME,
    CSS_CLASS,
    CSS_PSEUDOCLASS,
    CSS_NONE,
  } part_type = CSS_TYPE;

  static const struct {
    const char* name;
    GtkStateFlags state_flag;
  } pseudo_classes[] = {
      {"active", GTK_STATE_FLAG_ACTIVE},
      {"hover", GTK_STATE_FLAG_PRELIGHT},
      {"selected", GTK_STATE_FLAG_SELECTED},
      {"disabled", GTK_STATE_FLAG_INSENSITIVE},
      {"indeterminate", GTK_STATE_FLAG_INCONSISTENT},
      {"focus", GTK_STATE_FLAG_FOCUSED},
      {"backdrop", GTK_STATE_FLAG_BACKDROP},
      {"link", GTK_STATE_FLAG_LINK},
      {"visited", GTK_STATE_FLAG_VISITED},
      {"checked", GTK_STATE_FLAG_CHECKED},
  };

  GType gtype = G_TYPE_NONE;
  std::string name;
  std::string object_name;
  std::vector<std::string> classes;
  GtkStateFlags state = GTK_STATE_FLAG_NORMAL;

  base::StringTokenizer t(css_node, ".:#()");
  t.set_options(base::StringTokenizer::RETURN_DELIMS);
  while (t.GetNext()) {
    if (t.token_is_delim()) {
      switch (*t.token_begin()) {
        case '(':
          part_type = CSS_NAME;
          break;
        case ')':
          part_type = CSS_NONE;
          break;
        case '#':
          part_type = CSS_OBJECT_NAME;
          break;
        case '.':
          part_type = CSS_CLASS;
          break;
        case ':':
          part_type = CSS_PSEUDOCLASS;
          break;
        default:
          NOTREACHED();
      }
    } else {
      switch (part_type) {
        case CSS_NAME:
          name = t.token();
          break;
        case CSS_OBJECT_NAME:
          object_name = t.token();
          break;
        case CSS_TYPE: {
#if BUILDFLAG(GTK_VERSION) < 4
          gtype = g_type_from_name(t.token().c_str());
          DCHECK(gtype);
#endif
          break;
        }
        case CSS_CLASS:
          classes.push_back(t.token());
          break;
        case CSS_PSEUDOCLASS: {
          GtkStateFlags state_flag = GTK_STATE_FLAG_NORMAL;
          for (const auto& pseudo_class_entry : pseudo_classes) {
            if (strcmp(pseudo_class_entry.name, t.token().c_str()) == 0) {
              state_flag = pseudo_class_entry.state_flag;
              break;
            }
          }
          state = static_cast<GtkStateFlags>(state | state_flag);
          break;
        }
        case CSS_NONE:
          NOTREACHED();
      }
    }
  }

  // Always add a "chromium" class so that themes can style chromium
  // widgets specially if they want to.
  classes.push_back("chromium");

  float scale = std::round(GetDeviceScaleFactor());

  return AppendCssNodeToStyleContextImpl(context, gtype, name, object_name,
                                         classes, state, scale);
}

GtkCssContext GetStyleContextFromCss(const std::string& css_selector) {
  // Prepend a window node to the selector since all widgets must live
  // in a window, but we don't want to specify that every time.
  auto context = AppendCssNodeToStyleContext({}, "GtkWindow#window.background");

  for (const auto& widget_type :
       base::SplitString(css_selector, base::kWhitespaceASCII,
                         base::TRIM_WHITESPACE, base::SPLIT_WANT_NONEMPTY)) {
    context = AppendCssNodeToStyleContext(context, widget_type);
  }
  return context;
}

SkColor GetFgColorFromStyleContext(GtkStyleContext* context) {
  GdkRGBA color;
#if BUILDFLAG(GTK_VERSION) >= 4
  gtk_style_context_get_color(context, &color);
#else
  gtk_style_context_get_color(context, gtk_style_context_get_state(context),
                              &color);
#endif
  return GdkRgbaToSkColor(color);
}

SkColor GetBgColorFromStyleContext(GtkCssContext context) {
  // Backgrounds are more general than solid colors (eg. gradients),
  // but chromium requires us to boil this down to one color.  We
  // cannot use the background-color here because some themes leave it
  // set to a garbage color because a background-image will cover it
  // anyway.  So we instead render the background into a 24x24 bitmap,
  // removing any borders, and hope that we get a good color.
  ApplyCssToContext(context,
                    "* {"
                    "border-radius: 0px;"
                    "border-style: none;"
                    "box-shadow: none;"
                    "}");
  gfx::Size size(24, 24);
  CairoSurface surface(size);
  RenderBackground(size, surface.cairo(), context);
  return surface.GetAveragePixelValue(false);
}

SkColor GetFgColor(const std::string& css_selector) {
  return GetFgColorFromStyleContext(GetStyleContextFromCss(css_selector));
}

ScopedCssProvider GetCssProvider(const std::string& css) {
  auto provider = TakeGObject(gtk_css_provider_new());
#if BUILDFLAG(GTK_VERSION) >= 4
  gtk_css_provider_load_from_data(provider, css.c_str(), -1);
#else
  GError* error = nullptr;
  gtk_css_provider_load_from_data(provider, css.c_str(), -1, &error);
  DCHECK(!error);
#endif
  return provider;
}

void ApplyCssProviderToContext(GtkCssContext context,
                               GtkCssProvider* provider) {
  while (context) {
    gtk_style_context_add_provider(context, GTK_STYLE_PROVIDER(provider),
                                   G_MAXUINT);
    context = context.GetParent();
  }
}

void ApplyCssToContext(GtkCssContext context, const std::string& css) {
  auto provider = GetCssProvider(css);
  ApplyCssProviderToContext(context, provider);
}

void RenderBackground(const gfx::Size& size,
                      cairo_t* cr,
                      GtkCssContext context) {
  if (!context)
    return;
  RenderBackground(size, cr, context.GetParent());
  gtk_render_background(context, cr, 0, 0, size.width(), size.height());
}

SkColor GetBgColor(const std::string& css_selector) {
  return GetBgColorFromStyleContext(GetStyleContextFromCss(css_selector));
}

SkColor GetBorderColor(const std::string& css_selector) {
  // Borders have the same issue as backgrounds, due to the
  // border-image property.
  auto context = GetStyleContextFromCss(css_selector);
  gfx::Size size(24, 24);
  CairoSurface surface(size);
  gtk_render_frame(context, surface.cairo(), 0, 0, size.width(), size.height());
  return surface.GetAveragePixelValue(true);
}

SkColor GetSelectionBgColor(const std::string& css_selector) {
  auto context = GetStyleContextFromCss(css_selector);
  if (GtkCheckVersion(3, 20))
    return GetBgColorFromStyleContext(context);
#if BUILDFLAG(GTK_VERSION) < 4
  // This is verbatim how Gtk gets the selection color on versions before 3.20.
  GdkRGBA selection_color;
  G_GNUC_BEGIN_IGNORE_DEPRECATIONS;
  gtk_style_context_get_background_color(
      context, gtk_style_context_get_state(context), &selection_color);
  G_GNUC_END_IGNORE_DEPRECATIONS;
  return GdkRgbaToSkColor(selection_color);
#else
  NOTREACHED();
  return gfx::kPlaceholderColor;
#endif
}

bool ContextHasClass(GtkCssContext context, const std::string& style_class) {
#if BUILDFLAG(GTK_VERSION) >= 4
  return gtk_style_context_has_class(context, style_class.c_str());
#else
  return gtk_style_context_has_class(context, style_class.c_str()) ||
         gtk_widget_path_iter_has_class(gtk_style_context_get_path(context), -1,
                                        style_class.c_str());
#endif
}

SkColor GetSeparatorColor(const std::string& css_selector) {
  if (!GtkCheckVersion(3, 20))
    return GetFgColor(css_selector);

  auto context = GetStyleContextFromCss(css_selector);
  bool horizontal = ContextHasClass(context, "horizontal");

  int w = 1, h = 1;
  GtkBorder border, padding;
#if BUILDFLAG(GTK_VERSION) >= 4
  auto size = GetSeparatorSize(horizontal);
  w = size.width();
  h = size.height();
  gtk_style_context_get_border(context, &border);
  gtk_style_context_get_padding(context, &padding);
#else
  gtk_style_context_get(context, gtk_style_context_get_state(context),
                        "min-width", &w, "min-height", &h, nullptr);
  GtkStateFlags state = gtk_style_context_get_state(context);
  gtk_style_context_get_border(context, state, &border);
  gtk_style_context_get_padding(context, state, &padding);
#endif
  w += border.left + padding.left + padding.right + border.right;
  h += border.top + padding.top + padding.bottom + border.bottom;

  if (horizontal) {
    w = 24;
    h = std::max(h, 1);
  } else {
    DCHECK(ContextHasClass(context, "vertical"));
    h = 24;
    w = std::max(w, 1);
  }

  CairoSurface surface(gfx::Size(w, h));
  gtk_render_background(context, surface.cairo(), 0, 0, w, h);
  gtk_render_frame(context, surface.cairo(), 0, 0, w, h);
  return surface.GetAveragePixelValue(false);
}

std::string GetGtkSettingsStringProperty(GtkSettings* settings,
                                         const gchar* prop_name) {
  GValue layout = G_VALUE_INIT;
  g_value_init(&layout, G_TYPE_STRING);
  g_object_get_property(G_OBJECT(settings), prop_name, &layout);
  DCHECK(G_VALUE_HOLDS_STRING(&layout));
  std::string prop_value(g_value_get_string(&layout));
  g_value_unset(&layout);
  return prop_value;
}

int BuildXkbStateFromGdkEvent(unsigned int state, unsigned char group) {
  return state | ((group & 0x3) << 13);
}

int GetKeyEventProperty(const ui::KeyEvent& key_event,
                        const char* property_key) {
  auto* properties = key_event.properties();
  if (!properties)
    return 0;
  auto it = properties->find(property_key);
  DCHECK(it == properties->end() || it->second.size() == 1);
  return (it != properties->end()) ? it->second[0] : 0;
}

GdkModifierType GetGdkKeyEventState(const ui::KeyEvent& key_event) {
  // ui::KeyEvent uses a normalized modifier state which is not respected by
  // Gtk, so we need to get the state from the display backend. Gtk instead
  // follows the X11 spec in which the state of a key event is expected to be
  // the mask of modifier keys _prior_ to this event. Some IMEs rely on this
  // behavior. See https://crbug.com/1086946#c11.

  GdkModifierType state = GetImeFlags(key_event);
  if (key_event.key_code() != ui::VKEY_PROCESSKEY) {
    // This is an synthetized event when |key_code| is VKEY_PROCESSKEY.
    // In such a case there is no event being dispatching in the display
    // backend.
    state = static_cast<GdkModifierType>(
        state | ui::GtkUiDelegate::instance()->GetGdkKeyState());
  }

  return state;
}

GdkEvent* GdkEventFromKeyEvent(const ui::KeyEvent& key_event) {
  DCHECK(!GtkCheckVersion(4));
  GdkEventType event_type =
      key_event.type() == ui::ET_KEY_PRESSED ? GDK_KEY_PRESS : GDK_KEY_RELEASE;
  auto event_time = key_event.time_stamp() - base::TimeTicks();
  int hw_code = GetKeyEventProperty(key_event, ui::kPropertyKeyboardHwKeyCode);
  int group = GetKeyEventProperty(key_event, ui::kPropertyKeyboardGroup);

  // Get GdkKeymap
  GdkKeymap* keymap = GtkUi::GetDelegate()->GetGdkKeymap();

  // Get keyval and state
  GdkModifierType state = GetGdkKeyEventState(key_event);
  guint keyval = GDK_KEY_VoidSymbol;
  GdkModifierType consumed;
  gdk_keymap_translate_keyboard_state(keymap, hw_code, state, group, &keyval,
                                      nullptr, nullptr, &consumed);
  gdk_keymap_add_virtual_modifiers(keymap, &state);
  DCHECK(keyval != GDK_KEY_VoidSymbol);

  // Build GdkEvent
  GdkEvent* gdk_event = gdk_event_new(event_type);
  GdkEventKey* gdk_event_key = reinterpret_cast<GdkEventKey*>(gdk_event);
  gdk_event_key->type = event_type;
  gdk_event_key->time = event_time.InMilliseconds();
  gdk_event_key->hardware_keycode = hw_code;
  gdk_event_key->keyval = keyval;
  gdk_event_key->state = BuildXkbStateFromGdkEvent(state, group);
  gdk_event_key->group = group;
  gdk_event_key->send_event = key_event.flags() & ui::EF_FINAL;
  gdk_event_key->is_modifier = state & GDK_MODIFIER_MASK;
  gdk_event_key->length = 0;
  gdk_event_key->string = nullptr;

  return gdk_event;
}

GtkIconTheme* GetDefaultIconTheme() {
#if BUILDFLAG(GTK_VERSION) >= 4
  return gtk_icon_theme_get_for_display(gdk_display_get_default());
#else
  return gtk_icon_theme_get_default();
#endif
}

void GtkWindowDestroy(GtkWidget* widget) {
#if BUILDFLAG(GTK_VERSION) >= 4
  gtk_window_destroy(GTK_WINDOW(widget));
#else
  gtk_widget_destroy(widget);
#endif
}

GtkWidget* GetDummyWindow() {
  static GtkWidget* window = CreateDummyWindow();
  return window;
}

gfx::Size GetSeparatorSize(bool horizontal) {
  auto widget = TakeGObject(gtk_separator_new(
      horizontal ? GTK_ORIENTATION_HORIZONTAL : GTK_ORIENTATION_VERTICAL));
  GtkRequisition natural_size;
  gtk_widget_get_preferred_size(widget, nullptr, &natural_size);
  return {natural_size.width, natural_size.height};
}

float GetDeviceScaleFactor() {
  views::LinuxUI* linux_ui = views::LinuxUI::instance();
  return linux_ui ? linux_ui->GetDeviceScaleFactor() : 1;
}

GdkTexture* GetTextureFromRenderNode(GskRenderNode* node) {
  DCHECK(GtkCheckVersion(4));
  struct {
    GskRenderNodeType node_type;
    GskRenderNode* (*get_child)(GskRenderNode*);
  } constexpr simple_getters[] = {
      {GSK_TRANSFORM_NODE, gsk_transform_node_get_child},
      {GSK_OPACITY_NODE, gsk_opacity_node_get_child},
      {GSK_COLOR_MATRIX_NODE, gsk_color_matrix_node_get_child},
      {GSK_REPEAT_NODE, gsk_repeat_node_get_child},
      {GSK_CLIP_NODE, gsk_clip_node_get_child},
      {GSK_ROUNDED_CLIP_NODE, gsk_rounded_clip_node_get_child},
      {GSK_SHADOW_NODE, gsk_shadow_node_get_child},
      {GSK_BLUR_NODE, gsk_blur_node_get_child},
      {GSK_DEBUG_NODE, gsk_debug_node_get_child},
  };
  struct {
    GskRenderNodeType node_type;
    guint (*get_n_children)(GskRenderNode*);
    GskRenderNode* (*get_child)(GskRenderNode*, guint);
  } constexpr container_getters[] = {
      {GSK_CONTAINER_NODE, gsk_container_node_get_n_children,
       gsk_container_node_get_child},
      {GSK_GL_SHADER_NODE, gsk_gl_shader_node_get_n_children,
       gsk_gl_shader_node_get_child},
  };

  if (!node)
    return nullptr;

  auto node_type = gsk_render_node_get_node_type(node);
  if (node_type == GSK_TEXTURE_NODE)
    return gsk_texture_node_get_texture(node);
  for (const auto& getter : simple_getters) {
    if (node_type == getter.node_type) {
      if (auto* texture = GetTextureFromRenderNode(getter.get_child(node)))
        return texture;
    }
  }
  for (const auto& getter : container_getters) {
    if (node_type != getter.node_type)
      continue;
    for (guint i = 0; i < getter.get_n_children(node); ++i) {
      if (auto* texture = GetTextureFromRenderNode(getter.get_child(node, i)))
        return texture;
    }
    return nullptr;
  }
  return nullptr;
}

// TODO(tluk): Refactor this to make better use of the hierarchical nature of
// ColorPipeline.
base::Optional<SkColor> SkColorFromColorId(ui::ColorId color_id) {
  switch (color_id) {
    case ui::kColorWindowBackground:
    case ui::kColorDialogBackground:
    case ui::kColorBubbleBackground:
    case ui::kColorNotificationBackgroundInactive:
      return GetBgColor("");
    case ui::kColorDialogForeground:
    case ui::kColorAvatarIconIncognito:
      return GetFgColor("GtkLabel#label");
    case ui::kColorBubbleFooterBackground:
    case ui::kColorNotificationActionsBackground:
    case ui::kColorNotificationBackgroundActive:
    case ui::kColorNotificationImageBackground:
    case ui::kColorSyncInfoBackground:
      return GetBgColor("#statusbar");

    // FocusableBorder
    case ui::kColorFocusableBorderFocused:
      // GetBorderColor("GtkEntry#entry:focus") is correct here.  The focus ring
      // around widgets is usually a lighter version of the "canonical theme
      // color" - orange on Ambiance, blue on Adwaita, etc.  However, Chrome
      // lightens the color we give it, so it would look wrong if we give it an
      // already-lightened color.  This workaround returns the theme color
      // directly, taken from a selected table row.  This has matched the theme
      // color on every theme that I've tested.
      return GetBgColor(
          "GtkTreeView#treeview.view "
          "GtkTreeView#treeview.view.cell:selected:focus");
    case ui::kColorFocusableBorderUnfocused:
      return GetBorderColor("GtkEntry#entry");

    // Menu
    case ui::kColorMenuBackground:
    case ui::kColorMenuItemBackgroundHighlighted:
    case ui::kColorMenuItemBackgroundAlertedInitial:
    case ui::kColorMenuItemBackgroundAlertedTarget:
    case ui::kColorSubtleEmphasisBackground:
      return GetBgColor(kGtkCSSMenu);
    case ui::kColorMenuBorder:
      return GetBorderColor(kGtkCSSMenu);
    case ui::kColorMenuItemBackgroundSelected:
      return GetBgColor(StrCat({kGtkCSSMenu, " ", kGtkCSSMenuItem, ":hover"}));
    case ui::kColorMenuItemForeground:
    case ui::kColorMenuDropmarker:
    case ui::kColorMenuItemForegroundHighlighted:
      return GetFgColor(
          StrCat({kGtkCSSMenu, " ", kGtkCSSMenuItem, " GtkLabel#label"}));
    case ui::kColorMenuItemForegroundSelected:
      return GetFgColor(
          StrCat({kGtkCSSMenu, " ", kGtkCSSMenuItem, ":hover GtkLabel#label"}));
    case ui::kColorMenuItemForegroundDisabled:
      return GetFgColor(StrCat(
          {kGtkCSSMenu, " ", kGtkCSSMenuItem, ":disabled GtkLabel#label"}));
    case ui::kColorAvatarIconGuest:
    case ui::kColorMenuItemForegroundSecondary:
      if (GtkCheckVersion(3, 20)) {
        return GetFgColor(
            StrCat({kGtkCSSMenu, " ", kGtkCSSMenuItem, " #accelerator"}));
      }
      return GetFgColor(StrCat(
          {kGtkCSSMenu, " ", kGtkCSSMenuItem, " GtkLabel#label.accelerator"}));
    case ui::kColorMenuSeparator:
    case ui::kColorAvatarHeaderArt:
      if (GtkCheckVersion(3, 20)) {
        return GetSeparatorColor(
            StrCat({kGtkCSSMenu, " GtkSeparator#separator.horizontal"}));
      }
      return GetFgColor(
          StrCat({kGtkCSSMenu, " ", kGtkCSSMenuItem, ".separator"}));

    // Dropdown
    case ui::kColorDropdownBackground:
      return GetBgColor(
          StrCat({"GtkComboBoxText#combobox GtkWindow#window.background.popup ",
                  "GtkTreeMenu#menu(gtk-combobox-popup-menu) ", kGtkCSSMenuItem,
                  " ", "GtkCellView#cellview"}));
    case ui::kColorDropdownForeground:
      return GetFgColor(
          StrCat({"GtkComboBoxText#combobox GtkWindow#window.background.popup ",
                  "GtkTreeMenu#menu(gtk-combobox-popup-menu) ", kGtkCSSMenuItem,
                  " ", "GtkCellView#cellview"}));
    case ui::kColorDropdownBackgroundSelected:
      return GetBgColor(
          StrCat({"GtkComboBoxText#combobox GtkWindow#window.background.popup ",
                  "GtkTreeMenu#menu(gtk-combobox-popup-menu) ", kGtkCSSMenuItem,
                  ":hover GtkCellView#cellview"}));
    case ui::kColorDropdownForegroundSelected:
      return GetFgColor(
          StrCat({"GtkComboBoxText#combobox GtkWindow#window.background.popup ",
                  "GtkTreeMenu#menu(gtk-combobox-popup-menu) ", kGtkCSSMenuItem,
                  ":hover GtkCellView#cellview"}));

    // Label
    case ui::kColorLabelForeground:
    case ui::kColorPrimaryForeground:
      return GetFgColor("GtkLabel#label");
    case ui::kColorLabelForegroundDisabled:
    case ui::kColorLabelForegroundSecondary:
    case ui::kColorDisabledForeground:
    case ui::kColorSecondaryForeground:
      return GetFgColor("GtkLabel#label:disabled");
    case ui::kColorLabelSelectionForeground:
      return GetFgColor(GtkCheckVersion(3, 20) ? "GtkLabel#label #selection"
                                               : "GtkLabel#label:selected");
    case ui::kColorLabelSelectionBackground:
      return GetSelectionBgColor(GtkCheckVersion(3, 20)
                                     ? "GtkLabel#label #selection"
                                     : "GtkLabel#label:selected");

    // Link
    case ui::kColorLinkForegroundDisabled:
      if (GtkCheckVersion(3, 12))
        return GetFgColor("GtkLabel#label.link:link:disabled");
      FALLTHROUGH;
    case ui::kColorLinkForegroundPressed:
      if (GtkCheckVersion(3, 12))
        return GetFgColor("GtkLabel#label.link:link:hover:active");
      FALLTHROUGH;
    case ui::kColorLinkForeground: {
      if (GtkCheckVersion(3, 12))
        return GetFgColor("GtkLabel#label.link:link");
#if BUILDFLAG(GTK_VERSION) < 4
      auto link_context = GetStyleContextFromCss("GtkLabel#label.view");
      GdkColor* color;
      gtk_style_context_get_style(link_context, "link-color", &color, nullptr);
      if (color) {
        SkColor ret_color =
            SkColorSetRGB(color->red >> 8, color->green >> 8, color->blue >> 8);
        // gdk_color_free() was deprecated in Gtk3.14.  This code path is only
        // taken on versions earlier than Gtk3.12, but the compiler doesn't know
        // that, so silence the deprecation warnings.
        G_GNUC_BEGIN_IGNORE_DEPRECATIONS;
        gdk_color_free(color);
        G_GNUC_END_IGNORE_DEPRECATIONS;
        return ret_color;
      }
#endif
      // Default color comes from gtklinkbutton.c.
      return SkColorSetRGB(0x00, 0x00, 0xEE);
    }

    // Scrollbar
    case ui::kColorOverlayScrollbarStroke:
      return GetBgColor("#GtkScrollbar#scrollbar #trough");
    case ui::kColorOverlayScrollbarStrokeHovered:
      return GetBgColor("#GtkScrollbar#scrollbar #trough:hover");
    case ui::kColorOverlayScrollbarFill:
      return GetBgColor("#GtkScrollbar#scrollbar #slider");
    case ui::kColorOverlayScrollbarFillHovered:
      return GetBgColor("#GtkScrollbar#scrollbar #slider:hover");

    // Slider
    case ui::kColorSliderThumb:
      return GetBgColor("GtkScale#scale #highlight");
    case ui::kColorSliderTrack:
      return GetBgColor("GtkScale#scale #trough");
    case ui::kColorSliderThumbMinimal:
      return GetBgColor("GtkScale#scale:disabled #highlight");
    case ui::kColorSliderTrackMinimal:
      return GetBgColor("GtkScale#scale:disabled #trough");

    // Separator
    case ui::kColorMidground:
    case ui::kColorSeparator:
      return GetSeparatorColor("GtkSeparator#separator.horizontal");

    // Button
    case ui::kColorButtonBackground:
      return GetBgColor("GtkButton#button");
    case ui::kColorButtonForeground:
    case ui::kColorButtonForegroundUnchecked:
      return GetFgColor("GtkButton#button.text-button GtkLabel#label");
    case ui::kColorButtonForegroundDisabled:
      return GetFgColor("GtkButton#button.text-button:disabled GtkLabel#label");
    // TODO(thomasanderson): Add this once this CL lands:
    // https://chromium-review.googlesource.com/c/chromium/src/+/2053144
    // case ui::kColorId_ButtonHoverColor:
    //   return GetBgColor("GtkButton#button:hover");

    // ProminentButton
    case ui::kColorAccent:
    case ui::kColorButtonForegroundChecked:
    case ui::kColorButtonBackgroundProminent:
    case ui::kColorButtonBackgroundProminentFocused:
    case ui::kColorNotificationInputBackground:
      return GetBgColor(
          "GtkTreeView#treeview.view "
          "GtkTreeView#treeview.view.cell:selected:focus");
    case ui::kColorButtonForegroundProminent:
    case ui::kColorNotificationInputForeground:
      return GetFgColor(
          "GtkTreeView#treeview.view "
          "GtkTreeView#treeview.view.cell:selected:focus GtkLabel#label");
    case ui::kColorButtonBackgroundProminentDisabled:
    case ui::kColorButtonBorderDisabled:
      return GetBgColor("GtkButton#button.text-button:disabled");
    case ui::kColorButtonBorder:
      return GetBorderColor("GtkButton#button.text-button");
    // TODO(thomasanderson): Add this once this CL lands:
    // https://chromium-review.googlesource.com/c/chromium/src/+/2053144
    // case ui::kColorId_ProminentButtonHoverColor:
    //   return GetBgColor(
    //       "GtkTreeView#treeview.view "
    //       "GtkTreeView#treeview.view.cell:selected:focus:hover");

    // ToggleButton
    case ui::kColorToggleButtonTrackOff:
      return GetBgColor("GtkButton#button.text-button.toggle");
    case ui::kColorToggleButtonTrackOn:
      return GetBgColor("GtkButton#button.text-button.toggle:checked");

    // TabbedPane
    case ui::kColorTabForegroundSelected:
      return GetFgColor("GtkLabel#label");
    case ui::kColorTabForeground:
      return GetFgColor("GtkLabel#label:disabled");
    case ui::kColorTabContentSeparator:
      return GetBorderColor(GtkCheckVersion(3, 20) ? "GtkFrame#frame #border"
                                                   : "GtkFrame#frame");
    case ui::kColorTabBackgroundHighlighted:
      return GetBgColor("GtkNotebook#notebook #tab:checked");
    case ui::kColorTabBackgroundHighlightedFocused:
      return GetBgColor("GtkNotebook#notebook:focus #tab:checked");

    // Textfield
    case ui::kColorTextfieldForeground:
      return GetFgColor(GtkCheckVersion(3, 20)
                            ? "GtkTextView#textview.view #text"
                            : "GtkTextView.view");
    case ui::kColorTextfieldBackground:
      return GetBgColor(GtkCheckVersion(3, 20) ? "GtkTextView#textview.view"
                                               : "GtkTextView.view");
    case ui::kColorTextfieldForegroundPlaceholder:
      if (!GtkCheckVersion(3, 90)) {
        auto context = GetStyleContextFromCss("GtkEntry#entry");
        // This is copied from gtkentry.c.
        GdkRGBA fg = {0.5, 0.5, 0.5};
        gtk_style_context_lookup_color(context, "placeholder_text_color", &fg);
        return GdkRgbaToSkColor(fg);
      }
      return GetFgColor("GtkEntry#entry #text #placeholder");
    case ui::kColorTextfieldForegroundDisabled:
      return GetFgColor(GtkCheckVersion(3, 20)
                            ? "GtkTextView#textview.view:disabled #text"
                            : "GtkTextView.view:disabled");
    case ui::kColorTextfieldBackgroundDisabled:
      return GetBgColor(GtkCheckVersion(3, 20)
                            ? "GtkTextView#textview.view:disabled"
                            : "GtkTextView.view:disabled");
    case ui::kColorTextfieldSelectionForeground:
      return GetFgColor(GtkCheckVersion(3, 20)
                            ? "GtkTextView#textview.view #text #selection"
                            : "GtkTextView.view:selected");
    case ui::kColorTextfieldSelectionBackground:
      return GetSelectionBgColor(
          GtkCheckVersion(3, 20) ? "GtkTextView#textview.view #text #selection"
                                 : "GtkTextView.view:selected");

    // Tooltips
    case ui::kColorTooltipBackground:
      return GetBgColorFromStyleContext(GetTooltipContext());
    case ui::kColorHelpIconInactive:
      return GetFgColor("GtkButton#button.image-button");
    case ui::kColorHelpIconActive:
      return GetFgColor("GtkButton#button.image-button:hover");
    case ui::kColorTooltipForeground: {
      auto context = GetTooltipContext();
      context = AppendCssNodeToStyleContext(context, "GtkLabel#label");
      return GetFgColorFromStyleContext(context);
    }

    // Trees and Tables (implemented on GTK using the same class)
    case ui::kColorTableBackground:
    case ui::kColorTableBackgroundAlternate:
    case ui::kColorTreeBackground:
      return GetBgColor(
          "GtkTreeView#treeview.view GtkTreeView#treeview.view.cell");
    case ui::kColorTableForeground:
    case ui::kColorTreeNodeForeground:
    case ui::kColorTableGroupingIndicator:
      return GetFgColor(
          "GtkTreeView#treeview.view GtkTreeView#treeview.view.cell "
          "GtkLabel#label");
    case ui::kColorTableForegroundSelectedFocused:
    case ui::kColorTableForegroundSelectedUnfocused:
    case ui::kColorTreeNodeForegroundSelectedFocused:
    case ui::kColorTreeNodeForegroundSelectedUnfocused:
      return GetFgColor(
          "GtkTreeView#treeview.view "
          "GtkTreeView#treeview.view.cell:selected:focus GtkLabel#label");
    case ui::kColorTableBackgroundSelectedFocused:
    case ui::kColorTableBackgroundSelectedUnfocused:
    case ui::kColorTreeNodeBackgroundSelectedFocused:
    case ui::kColorTreeNodeBackgroundSelectedUnfocused:
      return GetBgColor(
          "GtkTreeView#treeview.view "
          "GtkTreeView#treeview.view.cell:selected:focus");

    // Table Header
    case ui::kColorTableHeaderForeground:
      return GetFgColor(
          "GtkTreeView#treeview.view GtkButton#button GtkLabel#label");
    case ui::kColorTableHeaderBackground:
      return GetBgColor("GtkTreeView#treeview.view GtkButton#button");
    case ui::kColorTableHeaderSeparator:
      return GetBorderColor("GtkTreeView#treeview.view GtkButton#button");

    // Throbber
    // TODO(thomasanderson): Render GtkSpinner directly.
    case ui::kColorThrobber:
      return GetFgColor("GtkSpinner#spinner");
    case ui::kColorThrobberPreconnect:
      return GetFgColor("GtkSpinner#spinner:disabled");

    // Alert icons
    // Fallback to the same colors as Aura.
    case ui::kColorAlertLowSeverity:
    case ui::kColorAlertMediumSeverity:
    case ui::kColorAlertHighSeverity: {
      // Alert icons appear on the toolbar, so use the toolbar BG
      // color (the GTK window bg color) to determine if the dark
      // or light native theme should be used for the icons.
      return ui::GetAlertSeverityColor(color_id,
                                       color_utils::IsDark(GetBgColor("")));
    }

    case ui::kColorMenuIcon:
      if (GtkCheckVersion(3, 20))
        return GetFgColor(
            StrCat({kGtkCSSMenu, " ", kGtkCSSMenuItem, " #radio"}));
      return GetFgColor(StrCat({kGtkCSSMenu, " ", kGtkCSSMenuItem, ".radio"}));

    case ui::kColorIcon:
      return GetFgColor("GtkButton#button.flat.scale GtkImage#image");

    default:
      break;
  }
  return base::nullopt;
}

}  // namespace gtk
