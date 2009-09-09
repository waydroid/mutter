/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */

/*
 * Copyright (c) 2008 Intel Corp.
 *
 * Author: Tomas Frydrych <tf@linux.intel.com>
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
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA
 * 02111-1307, USA.
 */

#include "mutter-plugin.h"
#include "screen.h"
#include "display.h"

#include <string.h>
#include <X11/Xlib.h>
#include <X11/extensions/Xfixes.h>
#include <X11/extensions/shape.h>
#include <clutter/x11/clutter-x11.h>

#include "compositor-private.h"
#include "mutter-window-private.h"

G_DEFINE_ABSTRACT_TYPE (MutterPlugin, mutter_plugin, G_TYPE_OBJECT);

#define MUTTER_PLUGIN_GET_PRIVATE(obj) \
(G_TYPE_INSTANCE_GET_PRIVATE ((obj), MUTTER_TYPE_PLUGIN, MutterPluginPrivate))

enum
{
  PROP_0,
  PROP_SCREEN,
  PROP_PARAMS,
  PROP_FEATURES,
  PROP_DISABLED,
  PROP_DEBUG_MODE,
};

struct _MutterPluginPrivate
{
  MetaScreen   *screen;
  gchar        *params;
  gulong        features;

  gint          running;

  gboolean      disabled : 1;
  gboolean      debug    : 1;
};

static void
mutter_plugin_dispose (GObject *object)
{
  G_OBJECT_CLASS (mutter_plugin_parent_class)->dispose (object);
}

static void
mutter_plugin_finalize (GObject *object)
{
  MutterPluginPrivate *priv = MUTTER_PLUGIN (object)->priv;

  g_free (priv->params);
  priv->params = NULL;

  G_OBJECT_CLASS (mutter_plugin_parent_class)->finalize (object);
}

static void
mutter_plugin_parse_params (MutterPlugin *plugin)
{
  char                  *p;
  gulong                features = 0;
  MutterPluginPrivate  *priv     = plugin->priv;
  MutterPluginClass    *klass    = MUTTER_PLUGIN_GET_CLASS (plugin);

/*
 * Feature flags: identify events that the plugin can handle; a plugin can
 * handle one or more events.
 */
  if (klass->minimize)
    features |= MUTTER_PLUGIN_MINIMIZE;

  if (klass->maximize)
    features |= MUTTER_PLUGIN_MAXIMIZE;

  if (klass->unmaximize)
    features |= MUTTER_PLUGIN_UNMAXIMIZE;

  if (klass->map)
    features |= MUTTER_PLUGIN_MAP;

  if (klass->destroy)
    features |= MUTTER_PLUGIN_DESTROY;

  if (klass->switch_workspace)
    features |= MUTTER_PLUGIN_SWITCH_WORKSPACE;

  if (priv->params)
    {
      gboolean debug = FALSE;

      if ((p = strstr (priv->params, "disable:")))
        {
          gchar *d = g_strdup (p+8);

          p = strchr (d, ';');

          if (p)
            *p = 0;

          if (strstr (d, "minimize"))
            features &= ~ MUTTER_PLUGIN_MINIMIZE;

          if (strstr (d, "maximize"))
            features &= ~ MUTTER_PLUGIN_MAXIMIZE;

          if (strstr (d, "unmaximize"))
            features &= ~ MUTTER_PLUGIN_UNMAXIMIZE;

          if (strstr (d, "map"))
            features &= ~ MUTTER_PLUGIN_MAP;

          if (strstr (d, "destroy"))
            features &= ~ MUTTER_PLUGIN_DESTROY;

          if (strstr (d, "switch-workspace"))
            features &= ~MUTTER_PLUGIN_SWITCH_WORKSPACE;

          g_free (d);
        }

      if (strstr (priv->params, "debug"))
        debug = TRUE;

      if (debug != priv->debug)
        {
          priv->debug = debug;

          g_object_notify (G_OBJECT (plugin), "debug-mode");
        }
    }

  if (features != priv->features)
    {
      priv->features = features;

      g_object_notify (G_OBJECT (plugin), "features");
    }
}

static void
mutter_plugin_set_property (GObject      *object,
			    guint         prop_id,
			    const GValue *value,
			    GParamSpec   *pspec)
{
  MutterPluginPrivate *priv = MUTTER_PLUGIN (object)->priv;

  switch (prop_id)
    {
    case PROP_SCREEN:
      priv->screen = g_value_get_object (value);
      break;
    case PROP_PARAMS:
      priv->params = g_value_dup_string (value);
      mutter_plugin_parse_params (MUTTER_PLUGIN (object));
      break;
    case PROP_DISABLED:
      priv->disabled = g_value_get_boolean (value);
      break;
    case PROP_DEBUG_MODE:
      priv->debug = g_value_get_boolean (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}

static void
mutter_plugin_get_property (GObject    *object,
			    guint       prop_id,
			    GValue     *value,
			    GParamSpec *pspec)
{
  MutterPluginPrivate *priv = MUTTER_PLUGIN (object)->priv;

  switch (prop_id)
    {
    case PROP_SCREEN:
      g_value_set_object (value, priv->screen);
      break;
    case PROP_PARAMS:
      g_value_set_string (value, priv->params);
      break;
    case PROP_DISABLED:
      g_value_set_boolean (value, priv->disabled);
      break;
    case PROP_DEBUG_MODE:
      g_value_set_boolean (value, priv->debug);
      break;
    case PROP_FEATURES:
      g_value_set_ulong (value, priv->features);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}


static void
mutter_plugin_class_init (MutterPluginClass *klass)
{
  GObjectClass      *gobject_class = G_OBJECT_CLASS (klass);

  gobject_class->finalize        = mutter_plugin_finalize;
  gobject_class->dispose         = mutter_plugin_dispose;
  gobject_class->set_property    = mutter_plugin_set_property;
  gobject_class->get_property    = mutter_plugin_get_property;

  g_object_class_install_property (gobject_class,
                                   PROP_SCREEN,
                                   g_param_spec_object ("screen",
                                                        "MetaScreen",
                                                        "MetaScreen",
                                                        META_TYPE_SCREEN,
                                                        G_PARAM_READWRITE |
                                                      G_PARAM_CONSTRUCT_ONLY));

  g_object_class_install_property (gobject_class,
				   PROP_PARAMS,
				   g_param_spec_string ("params",
							"Parameters",
							"Plugin Parameters",
							NULL,
							G_PARAM_READWRITE |
                                                        G_PARAM_CONSTRUCT));

  g_object_class_install_property (gobject_class,
				   PROP_FEATURES,
				   g_param_spec_ulong ("features",
                                                       "Features",
                                                       "Plugin Features",
                                                       0 , G_MAXULONG, 0,
                                                       G_PARAM_READABLE));

  g_object_class_install_property (gobject_class,
				   PROP_DISABLED,
				   g_param_spec_boolean ("disabled",
                                                      "Plugin disabled",
                                                      "Plugin disabled",
                                                      FALSE,
                                                      G_PARAM_READWRITE));

  g_object_class_install_property (gobject_class,
				   PROP_DEBUG_MODE,
				   g_param_spec_boolean ("debug-mode",
                                                      "Debug Mode",
                                                      "Debug Mode",
                                                      FALSE,
                                                      G_PARAM_READABLE));

  g_type_class_add_private (gobject_class, sizeof (MutterPluginPrivate));
}

static void
mutter_plugin_init (MutterPlugin *self)
{
  MutterPluginPrivate *priv;

  self->priv = priv = MUTTER_PLUGIN_GET_PRIVATE (self);
}

gulong
mutter_plugin_features (MutterPlugin *plugin)
{
  MutterPluginPrivate *priv = MUTTER_PLUGIN (plugin)->priv;

  return priv->features;
}

gboolean
mutter_plugin_disabled (MutterPlugin *plugin)
{
  MutterPluginPrivate *priv = MUTTER_PLUGIN (plugin)->priv;

  return priv->disabled;
}

gboolean
mutter_plugin_running  (MutterPlugin *plugin)
{
  MutterPluginPrivate *priv = MUTTER_PLUGIN (plugin)->priv;

  return (priv->running > 0);
}

gboolean
mutter_plugin_debug_mode (MutterPlugin *plugin)
{
  MutterPluginPrivate *priv = MUTTER_PLUGIN (plugin)->priv;

  return priv->debug;
}

const MutterPluginInfo *
mutter_plugin_get_info (MutterPlugin *plugin)
{
  MutterPluginClass  *klass = MUTTER_PLUGIN_GET_CLASS (plugin);

  if (klass && klass->plugin_info)
    return klass->plugin_info (plugin);

  return NULL;
}

ClutterActor *
mutter_plugin_get_overlay_group (MutterPlugin *plugin)
{
  MutterPluginPrivate *priv = MUTTER_PLUGIN (plugin)->priv;

  return mutter_get_overlay_group_for_screen (priv->screen);
}

ClutterActor *
mutter_plugin_get_stage (MutterPlugin *plugin)
{
  MutterPluginPrivate *priv = MUTTER_PLUGIN (plugin)->priv;

  return mutter_get_stage_for_screen (priv->screen);
}

ClutterActor *
mutter_plugin_get_window_group (MutterPlugin *plugin)
{
  MutterPluginPrivate *priv = MUTTER_PLUGIN (plugin)->priv;

  return mutter_get_window_group_for_screen (priv->screen);
}

/**
 * _mutter_plugin_effect_started:
 * @plugin: the plugin
 *
 * Mark that an effect has started for the plugin. This is called
 * internally by MutterPluginManager.
 */
void
_mutter_plugin_effect_started (MutterPlugin *plugin)
{
  MutterPluginPrivate *priv = MUTTER_PLUGIN (plugin)->priv;

  priv->running++;
}

void
mutter_plugin_effect_completed (MutterPlugin *plugin,
                                MutterWindow *actor,
                                unsigned long event)
{
  MutterPluginPrivate *priv = MUTTER_PLUGIN (plugin)->priv;

  if (priv->running-- < 0)
    {
      g_warning ("Error in running effect accounting, adjusting.");
      priv->running = 0;
    }

  if (!actor)
    {
      const MutterPluginInfo *info;
      const gchar            *name = NULL;

      if (plugin && (info = mutter_plugin_get_info (plugin)))
        name = info->name;

      g_warning ("Plugin [%s] passed NULL for actor!",
                 name ? name : "unknown");
    }

  if (event == MUTTER_PLUGIN_SWITCH_WORKSPACE)
    {
      /* The window is just used to identify the screen */
      MetaWindow *window = mutter_window_get_meta_window (actor);
      MetaScreen *screen = meta_window_get_screen (window);
      mutter_switch_workspace_completed (screen);
    }
    else
    {
      mutter_window_effect_completed (actor, event);
    }
}

void
mutter_plugin_query_screen_size (MutterPlugin *plugin,
                                 int          *width,
                                 int          *height)
{
  MutterPluginPrivate *priv = MUTTER_PLUGIN (plugin)->priv;

  meta_screen_get_size (priv->screen, width, height);
}

void
mutter_plugin_set_stage_reactive (MutterPlugin *plugin,
                                  gboolean      reactive)
{
  MutterPluginPrivate *priv = MUTTER_PLUGIN (plugin)->priv;
  MetaScreen  *screen  = priv->screen;

  if (reactive)
    mutter_set_stage_input_region (screen, None);
  else
    mutter_empty_stage_input_region (screen);
}

void
mutter_plugin_set_stage_input_area (MutterPlugin *plugin,
                                    gint x, gint y, gint width, gint height)
{
  MutterPluginPrivate *priv = MUTTER_PLUGIN (plugin)->priv;
  MetaScreen   *screen  = priv->screen;
  MetaDisplay  *display = meta_screen_get_display (screen);
  Display      *xdpy    = meta_display_get_xdisplay (display);
  XRectangle    rect;
  XserverRegion region;

  rect.x = x;
  rect.y = y;
  rect.width = width;
  rect.height = height;

  region = XFixesCreateRegion (xdpy, &rect, 1);
  mutter_set_stage_input_region (screen, region);
  XFixesDestroyRegion (xdpy, region);
}

void
mutter_plugin_set_stage_input_region (MutterPlugin *plugin,
                                      XserverRegion region)
{
  MutterPluginPrivate *priv = MUTTER_PLUGIN (plugin)->priv;
  MetaScreen  *screen  = priv->screen;

  mutter_set_stage_input_region (screen, region);
}

GList *
mutter_plugin_get_windows (MutterPlugin *plugin)
{
  MutterPluginPrivate *priv = MUTTER_PLUGIN (plugin)->priv;

  return mutter_get_windows (priv->screen);
}

/**
 * mutter_plugin_begin_modal:
 * @plugin: a #MutterPlugin
 * @grab_window: the X window to grab the keyboard and mouse on
 * @cursor: the cursor to use for the pointer grab, or None,
 *          to use the normal cursor for the grab window and
 *          its descendants.
 * @options: flags that modify the behavior of the modal grab
 * @timestamp: the timestamp used for establishing grabs
 *
 * This function is used to grab the keyboard and mouse for the exclusive
 * use of the plugin. Correct operation requires that both the keyboard
 * and mouse are grabbed, or thing will break. (In particular, other
 * passive X grabs in Mutter can trigger but not be handled by the normal
 * keybinding handling code.) However, the plugin can establish the keyboard
 * and/or mouse grabs ahead of time and pass in the
 * %META_MODAL_POINTER_ALREADY_GRABBED and/or %META_MODAL_KEYBOARD_ALREADY_GRABBED
 * options. This facility is provided for two reasons: first to allow using
 * this function to establish modality after a passive grab, and second to
 * allow using obscure features of XGrabPointer() and XGrabKeyboard() without
 * having to add them to this API.
 *
 * Return value: whether we successfully grabbed the keyboard and
 *  mouse and made the plugin modal.
 */
gboolean
mutter_plugin_begin_modal (MutterPlugin      *plugin,
                           Window             grab_window,
                           Cursor             cursor,
                           MetaModalOptions   options,
                           guint32            timestamp)
{
  MutterPluginPrivate *priv = MUTTER_PLUGIN (plugin)->priv;

  return mutter_begin_modal_for_plugin (priv->screen, plugin,
                                        grab_window, cursor, options, timestamp);
}

/**
 * mutter_plugin_end_modal
 * @plugin: a #MutterPlugin
 * @timestamp: the time used for releasing grabs
 *
 * Ends the modal operation begun with meta_plugin_begin_modal(). This
 * ungrabs both the mouse and keyboard even when
 * %META_MODAL_POINTER_ALREADY_GRABBED or
 * %META_MODAL_KEYBOARD_ALREADY_GRABBED were provided as options
 * when beginnning the modal operation.
 */
void
mutter_plugin_end_modal (MutterPlugin *plugin,
                         guint32       timestamp)
{
  MutterPluginPrivate *priv = MUTTER_PLUGIN (plugin)->priv;

  mutter_end_modal_for_plugin (priv->screen, plugin, timestamp);
}

Display *
mutter_plugin_get_xdisplay (MutterPlugin *plugin)
{
  MutterPluginPrivate *priv    = MUTTER_PLUGIN (plugin)->priv;
  MetaDisplay         *display = meta_screen_get_display (priv->screen);
  Display             *xdpy    = meta_display_get_xdisplay (display);

  return xdpy;
}

/**
 * mutter_plugin_get_screen:
 * @plugin: a #MutterPlugin
 *
 * Gets the #MetaScreen corresponding to a plugin. Each plugin instance
 * is associated with exactly one screen; if Metacity is managing
 * multiple screens, multiple plugin instances will be created.
 *
 * Return value: (transfer none): the #MetaScreen for the plugin
 */
MetaScreen *
mutter_plugin_get_screen (MutterPlugin *plugin)
{
  MutterPluginPrivate *priv = MUTTER_PLUGIN (plugin)->priv;

  return priv->screen;
}

