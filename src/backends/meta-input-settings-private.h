/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */

/*
 * Copyright 2014 Red Hat, Inc.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 *
 * Author: Carlos Garnacho <carlosg@gnome.org>
 */

#ifndef META_INPUT_SETTINGS_PRIVATE_H
#define META_INPUT_SETTINGS_PRIVATE_H

#include "display-private.h"
#include "meta-monitor-manager-private.h"

#include <clutter/clutter.h>

#ifdef HAVE_LIBWACOM
#include <libwacom/libwacom.h>
#endif

#define META_TYPE_INPUT_SETTINGS             (meta_input_settings_get_type ())
#define META_INPUT_SETTINGS(obj)             (G_TYPE_CHECK_INSTANCE_CAST ((obj), META_TYPE_INPUT_SETTINGS, MetaInputSettings))
#define META_INPUT_SETTINGS_CLASS(klass)     (G_TYPE_CHECK_CLASS_CAST ((klass),  META_TYPE_INPUT_SETTINGS, MetaInputSettingsClass))
#define META_IS_INPUT_SETTINGS(obj)          (G_TYPE_CHECK_INSTANCE_TYPE ((obj), META_TYPE_INPUT_SETTINGS))
#define META_IS_INPUT_SETTINGS_CLASS(klass)  (G_TYPE_CHECK_CLASS_TYPE ((klass),  META_TYPE_INPUT_SETTINGS))
#define META_INPUT_SETTINGS_GET_CLASS(obj)   (G_TYPE_INSTANCE_GET_CLASS ((obj),  META_TYPE_INPUT_SETTINGS, MetaInputSettingsClass))

typedef struct _MetaInputSettings MetaInputSettings;
typedef struct _MetaInputSettingsClass MetaInputSettingsClass;

struct _MetaInputSettings
{
  GObject parent_instance;
};

struct _MetaInputSettingsClass
{
  GObjectClass parent_class;

  void (* set_send_events)   (MetaInputSettings        *settings,
                              ClutterInputDevice       *device,
                              GDesktopDeviceSendEvents  mode);
  void (* set_matrix)        (MetaInputSettings  *settings,
                              ClutterInputDevice *device,
                              gfloat              matrix[6]);
  void (* set_speed)         (MetaInputSettings  *settings,
                              ClutterInputDevice *device,
                              gdouble             speed);
  void (* set_left_handed)   (MetaInputSettings  *settings,
                              ClutterInputDevice *device,
                              gboolean            enabled);
  void (* set_tap_enabled)   (MetaInputSettings  *settings,
                              ClutterInputDevice *device,
                              gboolean            enabled);
  void (* set_invert_scroll) (MetaInputSettings  *settings,
                              ClutterInputDevice *device,
                              gboolean            inverted);
  void (* set_edge_scroll)   (MetaInputSettings  *settings,
                              ClutterInputDevice *device,
                              gboolean            enabled);
  void (* set_two_finger_scroll) (MetaInputSettings  *settings,
                                  ClutterInputDevice *device,
                                  gboolean            enabled);
  void (* set_scroll_button) (MetaInputSettings  *settings,
                              ClutterInputDevice *device,
                              guint               button);

  void (* set_click_method)  (MetaInputSettings            *settings,
                              ClutterInputDevice           *device,
                              GDesktopTouchpadClickMethod   mode);

  void (* set_keyboard_repeat) (MetaInputSettings *settings,
                                gboolean           repeat,
                                guint              delay,
                                guint              interval);

  void (* set_tablet_mapping)        (MetaInputSettings      *settings,
                                      ClutterInputDevice     *device,
                                      GDesktopTabletMapping   mapping);
  void (* set_tablet_keep_aspect)    (MetaInputSettings      *settings,
                                      ClutterInputDevice     *device,
                                      MetaOutput             *output,
                                      gboolean                keep_aspect);
  void (* set_tablet_area)           (MetaInputSettings      *settings,
                                      ClutterInputDevice     *device,
                                      gdouble                 padding_left,
                                      gdouble                 padding_right,
                                      gdouble                 padding_top,
                                      gdouble                 padding_bottom);

  void (* set_mouse_accel_profile) (MetaInputSettings          *settings,
                                    ClutterInputDevice         *device,
                                    GDesktopPointerAccelProfile profile);
  void (* set_trackball_accel_profile) (MetaInputSettings          *settings,
                                        ClutterInputDevice         *device,
                                        GDesktopPointerAccelProfile profile);

  gboolean (* has_two_finger_scroll) (MetaInputSettings  *settings,
                                      ClutterInputDevice *device);
};

GType meta_input_settings_get_type (void) G_GNUC_CONST;

MetaInputSettings * meta_input_settings_create (void);

GSettings *           meta_input_settings_get_tablet_settings (MetaInputSettings  *settings,
                                                               ClutterInputDevice *device);
MetaMonitorInfo *     meta_input_settings_get_tablet_monitor_info (MetaInputSettings  *settings,
                                                                   ClutterInputDevice *device);

GDesktopTabletMapping meta_input_settings_get_tablet_mapping (MetaInputSettings  *settings,
                                                              ClutterInputDevice *device);

GDesktopStylusButtonAction meta_input_settings_get_stylus_button_action (MetaInputSettings      *settings,
                                                                         ClutterInputDeviceTool *tool,
                                                                         ClutterInputDevice     *current_device,
                                                                         guint                   button);
gdouble                    meta_input_settings_translate_tablet_tool_pressure (MetaInputSettings      *input_settings,
                                                                               ClutterInputDeviceTool *tool,
                                                                               ClutterInputDevice     *current_tablet,
                                                                               gdouble                 pressure);

gboolean                   meta_input_settings_is_pad_button_grabbed     (MetaInputSettings  *input_settings,
                                                                          ClutterInputDevice *pad,
                                                                          guint               button);

gboolean                   meta_input_settings_handle_pad_button         (MetaInputSettings  *input_settings,
                                                                          ClutterInputDevice *pad,
                                                                          gboolean            is_press,
                                                                          guint               button);
gchar *                    meta_input_settings_get_pad_button_action_label (MetaInputSettings  *input_settings,
                                                                            ClutterInputDevice *pad,
                                                                            guint               button);

#ifdef HAVE_LIBWACOM
WacomDevice * meta_input_settings_get_tablet_wacom_device (MetaInputSettings *settings,
                                                           ClutterInputDevice *device);
#endif

gboolean meta_input_device_is_trackball (ClutterInputDevice *device);

#endif /* META_INPUT_SETTINGS_PRIVATE_H */
