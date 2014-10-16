/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */

/*
 * Copyright (C) 2001, 2002 Havoc Pennington
 * Copyright (C) 2002, 2003 Red Hat Inc.
 * Some ICCCM manager selection code derived from fvwm2,
 * Copyright (C) 2001 Dominik Vogt, Matthias Clasen, and fvwm2 team
 * Copyright (C) 2003 Rob Adams
 * Copyright (C) 2004-2006 Elijah Newren
 * Copyright (C) 2013 Red Hat Inc.
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
 */

#include "config.h"

#include "meta-monitor-manager.h"

#include <string.h>
#include <math.h>
#include <stdlib.h>
#include <clutter/clutter.h>

#include <meta/main.h>
#include "util-private.h"
#include <meta/errors.h>
#include "meta-monitor-config.h"
#include "backends/x11/meta-monitor-manager-xrandr.h"
#include "meta-backend-private.h"

enum {
  CONFIRM_DISPLAY_CHANGE,
  SIGNALS_LAST
};

static int signals[SIGNALS_LAST];

static void meta_monitor_manager_display_config_init (MetaDBusDisplayConfigIface *iface);

G_DEFINE_ABSTRACT_TYPE_WITH_CODE (MetaMonitorManager, meta_monitor_manager, META_DBUS_TYPE_DISPLAY_CONFIG_SKELETON,
                                  G_IMPLEMENT_INTERFACE (META_DBUS_TYPE_DISPLAY_CONFIG, meta_monitor_manager_display_config_init));

static void initialize_dbus_interface (MetaMonitorManager *manager);

static void
meta_monitor_manager_init (MetaMonitorManager *manager)
{
}

static void
read_current_config (MetaMonitorManager *manager)
{
  manager->serial++;

  META_MONITOR_MANAGER_GET_CLASS (manager)->read_current (manager);
}

/*
 * make_logical_config:
 *
 * Turn outputs and CRTCs into logical MetaMonitorInfo,
 * that will be used by the core and API layer (MetaScreen
 * and friends)
 */
static void
make_logical_config (MetaMonitorManager *manager)
{
  GArray *monitor_infos;
  unsigned int i, j;

  monitor_infos = g_array_sized_new (FALSE, TRUE, sizeof (MetaMonitorInfo),
                                     manager->n_outputs);

  /* Walk the list of MetaCRTCs, and build a MetaMonitorInfo
     for each of them, unless they reference a rectangle that
     is already there.
  */
  for (i = 0; i < manager->n_crtcs; i++)
    {
      MetaCRTC *crtc = &manager->crtcs[i];

      /* Ignore CRTCs not in use */
      if (crtc->current_mode == NULL)
        continue;

      for (j = 0; j < monitor_infos->len; j++)
        {
          MetaMonitorInfo *info = &g_array_index (monitor_infos, MetaMonitorInfo, j);
          if (meta_rectangle_equal (&crtc->rect,
                                    &info->rect))
            {
              crtc->logical_monitor = info;
              break;
            }
        }

      if (crtc->logical_monitor == NULL)
        {
          MetaMonitorInfo info;

          info.number = monitor_infos->len;
          info.rect = crtc->rect;
          info.is_primary = FALSE;
          /* This starts true because we want
             is_presentation only if all outputs are
             marked as such (while for primary it's enough
             that any is marked)
          */
          info.is_presentation = TRUE;
          info.in_fullscreen = -1;
          info.winsys_id = 0;

          g_array_append_val (monitor_infos, info);

          crtc->logical_monitor = &g_array_index (monitor_infos, MetaMonitorInfo,
                                                  info.number);
        }
    }

  /* Now walk the list of outputs applying extended properties (primary
     and presentation)
  */
  for (i = 0; i < manager->n_outputs; i++)
    {
      MetaOutput *output;
      MetaMonitorInfo *info;

      output = &manager->outputs[i];

      /* Ignore outputs that are not active */
      if (output->crtc == NULL)
        continue;

      /* We must have a logical monitor on every CRTC at this point */
      g_assert (output->crtc->logical_monitor != NULL);

      info = output->crtc->logical_monitor;

      info->is_primary = info->is_primary || output->is_primary;
      info->is_presentation = info->is_presentation && output->is_presentation;

      if (output->is_primary || info->winsys_id == 0)
        info->winsys_id = output->winsys_id;

      if (info->is_primary)
        manager->primary_monitor_index = info->number;
    }

  manager->n_monitor_infos = monitor_infos->len;
  manager->monitor_infos = (void*)g_array_free (monitor_infos, FALSE);
}

static void
power_save_mode_changed (MetaMonitorManager *manager,
                         GParamSpec         *pspec,
                         gpointer            user_data)
{
  MetaMonitorManagerClass *klass;
  int mode = meta_dbus_display_config_get_power_save_mode (META_DBUS_DISPLAY_CONFIG (manager));

  if (mode == META_POWER_SAVE_UNSUPPORTED)
    return;

  /* If DPMS is unsupported, force the property back. */
  if (manager->power_save_mode == META_POWER_SAVE_UNSUPPORTED)
    {
      meta_dbus_display_config_set_power_save_mode (META_DBUS_DISPLAY_CONFIG (manager), META_POWER_SAVE_UNSUPPORTED);
      return;
    }

  klass = META_MONITOR_MANAGER_GET_CLASS (manager);
  if (klass->set_power_save_mode)
    klass->set_power_save_mode (manager, mode);

  manager->power_save_mode = mode;
}

static void
meta_monitor_manager_constructed (GObject *object)
{
  MetaMonitorManager *manager = META_MONITOR_MANAGER (object);

  g_signal_connect_object (manager, "notify::power-save-mode",
                           G_CALLBACK (power_save_mode_changed), manager, 0);

  manager->in_init = TRUE;

  manager->config = meta_monitor_config_new ();

  read_current_config (manager);

  if (!meta_monitor_config_apply_stored (manager->config, manager))
    meta_monitor_config_make_default (manager->config, manager);

  /* Under XRandR, we don't rebuild our data structures until we see
     the RRScreenNotify event, but at least at startup we want to have
     the right configuration immediately.

     The other backends keep the data structures always updated,
     so this is not needed.
  */
  if (META_IS_MONITOR_MANAGER_XRANDR (manager))
    {
      MetaOutput *old_outputs;
      MetaCRTC *old_crtcs;
      MetaMonitorMode *old_modes;
      unsigned int n_old_outputs, n_old_modes;

      old_outputs = manager->outputs;
      n_old_outputs = manager->n_outputs;
      old_modes = manager->modes;
      n_old_modes = manager->n_modes;
      old_crtcs = manager->crtcs;

      read_current_config (manager);

      meta_monitor_manager_free_output_array (old_outputs, n_old_outputs);
      meta_monitor_manager_free_mode_array (old_modes, n_old_modes);
      g_free (old_crtcs);
    }

  make_logical_config (manager);
  initialize_dbus_interface (manager);

  manager->in_init = FALSE;
}

void
meta_monitor_manager_free_output_array (MetaOutput *old_outputs,
                                        int         n_old_outputs)
{
  int i;

  for (i = 0; i < n_old_outputs; i++)
    {
      g_free (old_outputs[i].name);
      g_free (old_outputs[i].vendor);
      g_free (old_outputs[i].product);
      g_free (old_outputs[i].serial);
      g_free (old_outputs[i].modes);
      g_free (old_outputs[i].possible_crtcs);
      g_free (old_outputs[i].possible_clones);

      if (old_outputs[i].driver_notify)
        old_outputs[i].driver_notify (&old_outputs[i]);
    }

  g_free (old_outputs);
}

void
meta_monitor_manager_free_mode_array (MetaMonitorMode *old_modes,
                                      int              n_old_modes)
{
  int i;

  for (i = 0; i < n_old_modes; i++)
    {
      g_free (old_modes[i].name);

      if (old_modes[i].driver_notify)
        old_modes[i].driver_notify (&old_modes[i]);
    }

  g_free (old_modes);
}

static void
meta_monitor_manager_finalize (GObject *object)
{
  MetaMonitorManager *manager = META_MONITOR_MANAGER (object);

  meta_monitor_manager_free_output_array (manager->outputs, manager->n_outputs);
  meta_monitor_manager_free_mode_array (manager->modes, manager->n_modes);
  g_free (manager->monitor_infos);
  g_free (manager->crtcs);

  G_OBJECT_CLASS (meta_monitor_manager_parent_class)->finalize (object);
}

static void
meta_monitor_manager_dispose (GObject *object)
{
  MetaMonitorManager *manager = META_MONITOR_MANAGER (object);

  if (manager->dbus_name_id != 0)
    {
      g_bus_unown_name (manager->dbus_name_id);
      manager->dbus_name_id = 0;
    }

  G_OBJECT_CLASS (meta_monitor_manager_parent_class)->dispose (object);
}

static GBytes *
meta_monitor_manager_real_read_edid (MetaMonitorManager *manager,
                                     MetaOutput         *output)
{
  return NULL;
}

static char *
meta_monitor_manager_real_get_edid_file (MetaMonitorManager *manager,
                                         MetaOutput         *output)
{
  return NULL;
}

static void
meta_monitor_manager_class_init (MetaMonitorManagerClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->constructed = meta_monitor_manager_constructed;
  object_class->dispose = meta_monitor_manager_dispose;
  object_class->finalize = meta_monitor_manager_finalize;

  klass->get_edid_file = meta_monitor_manager_real_get_edid_file;
  klass->read_edid = meta_monitor_manager_real_read_edid;

  signals[CONFIRM_DISPLAY_CHANGE] =
    g_signal_new ("confirm-display-change",
		  G_TYPE_FROM_CLASS (object_class),
		  G_SIGNAL_RUN_LAST,
		  0,
                  NULL, NULL, NULL,
		  G_TYPE_NONE, 0);
}

static const double known_diagonals[] = {
    12.1,
    13.3,
    15.6
};

static char *
diagonal_to_str (double d)
{
  unsigned int i;

  for (i = 0; i < G_N_ELEMENTS (known_diagonals); i++)
    {
      double delta;

      delta = fabs(known_diagonals[i] - d);
      if (delta < 0.1)
        return g_strdup_printf ("%0.1lf\"", known_diagonals[i]);
    }

  return g_strdup_printf ("%d\"", (int) (d + 0.5));
}

static char *
make_display_name (MetaMonitorManager *manager,
                   MetaOutput         *output)
{
  char *inches = NULL;
  char *vendor_name = NULL;
  char *ret;

  if (g_str_has_prefix (output->name, "LVDS") ||
      g_str_has_prefix (output->name, "eDP"))
    {
      ret = g_strdup (_("Built-in display"));
      goto out;
    }

  if (output->width_mm > 0 && output->height_mm > 0)
    {
      double d = sqrt (output->width_mm * output->width_mm +
                       output->height_mm * output->height_mm);
      inches = diagonal_to_str (d / 25.4);
    }

  if (g_strcmp0 (output->vendor, "unknown") != 0)
    {
      if (!manager->pnp_ids)
        manager->pnp_ids = gnome_pnp_ids_new ();

      vendor_name = gnome_pnp_ids_get_pnp_id (manager->pnp_ids,
                                              output->vendor);

      if (!vendor_name)
        vendor_name = g_strdup (output->vendor);
    }
  else
    {
      if (inches != NULL)
        vendor_name = g_strdup (_("Unknown"));
      else
        vendor_name = g_strdup (_("Unknown Display"));
    }

  if (inches != NULL)
    {
      /* TRANSLATORS: this is a monitor vendor name, followed by a
       * size in inches, like 'Dell 15"'
       */
      ret = g_strdup_printf (_("%s %s"), vendor_name, inches);
    }
  else
    {
      ret = g_strdup (vendor_name);
    }

 out:
  g_free (inches);
  g_free (vendor_name);

  return ret;
}

static gboolean
meta_monitor_manager_handle_get_resources (MetaDBusDisplayConfig *skeleton,
                                           GDBusMethodInvocation *invocation)
{
  MetaMonitorManager *manager = META_MONITOR_MANAGER (skeleton);
  MetaMonitorManagerClass *manager_class = META_MONITOR_MANAGER_GET_CLASS (skeleton);
  GVariantBuilder crtc_builder, output_builder, mode_builder;
  unsigned int i, j;

  g_variant_builder_init (&crtc_builder, G_VARIANT_TYPE ("a(uxiiiiiuaua{sv})"));
  g_variant_builder_init (&output_builder, G_VARIANT_TYPE ("a(uxiausauaua{sv})"));
  g_variant_builder_init (&mode_builder, G_VARIANT_TYPE ("a(uxuud)"));

  for (i = 0; i < manager->n_crtcs; i++)
    {
      MetaCRTC *crtc = &manager->crtcs[i];
      GVariantBuilder transforms;

      g_variant_builder_init (&transforms, G_VARIANT_TYPE ("au"));
      for (j = 0; j <= META_MONITOR_TRANSFORM_FLIPPED_270; j++)
        if (crtc->all_transforms & (1 << j))
          g_variant_builder_add (&transforms, "u", j);

      g_variant_builder_add (&crtc_builder, "(uxiiiiiuaua{sv})",
                             i, /* ID */
                             (gint64)crtc->crtc_id,
                             (int)crtc->rect.x,
                             (int)crtc->rect.y,
                             (int)crtc->rect.width,
                             (int)crtc->rect.height,
                             (int)(crtc->current_mode ? crtc->current_mode - manager->modes : -1),
                             (guint32)crtc->transform,
                             &transforms,
                             NULL /* properties */);
    }

  for (i = 0; i < manager->n_outputs; i++)
    {
      MetaOutput *output = &manager->outputs[i];
      GVariantBuilder crtcs, modes, clones, properties;
      GBytes *edid;
      char *edid_file;

      g_variant_builder_init (&crtcs, G_VARIANT_TYPE ("au"));
      for (j = 0; j < output->n_possible_crtcs; j++)
        g_variant_builder_add (&crtcs, "u",
                               (unsigned)(output->possible_crtcs[j] - manager->crtcs));

      g_variant_builder_init (&modes, G_VARIANT_TYPE ("au"));
      for (j = 0; j < output->n_modes; j++)
        g_variant_builder_add (&modes, "u",
                               (unsigned)(output->modes[j] - manager->modes));

      g_variant_builder_init (&clones, G_VARIANT_TYPE ("au"));
      for (j = 0; j < output->n_possible_clones; j++)
        g_variant_builder_add (&clones, "u",
                               (unsigned)(output->possible_clones[j] - manager->outputs));

      g_variant_builder_init (&properties, G_VARIANT_TYPE ("a{sv}"));
      g_variant_builder_add (&properties, "{sv}", "vendor",
                             g_variant_new_string (output->vendor));
      g_variant_builder_add (&properties, "{sv}", "product",
                             g_variant_new_string (output->product));
      g_variant_builder_add (&properties, "{sv}", "serial",
                             g_variant_new_string (output->serial));
      g_variant_builder_add (&properties, "{sv}", "width-mm",
                             g_variant_new_int32 (output->width_mm));
      g_variant_builder_add (&properties, "{sv}", "height-mm",
                             g_variant_new_int32 (output->height_mm));
      g_variant_builder_add (&properties, "{sv}", "display-name",
                             g_variant_new_take_string (make_display_name (manager, output)));
      g_variant_builder_add (&properties, "{sv}", "backlight",
                             g_variant_new_int32 (output->backlight));
      g_variant_builder_add (&properties, "{sv}", "min-backlight-step",
                             g_variant_new_int32 ((output->backlight_max - output->backlight_min) ?
                                                  100 / (output->backlight_max - output->backlight_min) : -1));
      g_variant_builder_add (&properties, "{sv}", "primary",
                             g_variant_new_boolean (output->is_primary));
      g_variant_builder_add (&properties, "{sv}", "presentation",
                             g_variant_new_boolean (output->is_presentation));

      edid_file = manager_class->get_edid_file (manager, output);
      if (edid_file)
        {
          g_variant_builder_add (&properties, "{sv}", "edid-file",
                                 g_variant_new_take_string (edid_file));
        }
      else
        {
          edid = manager_class->read_edid (manager, output);

          if (edid)
            {
              g_variant_builder_add (&properties, "{sv}", "edid",
                                     g_variant_new_from_bytes (G_VARIANT_TYPE ("ay"),
                                                               edid, TRUE));
              g_bytes_unref (edid);
            }
        }

      g_variant_builder_add (&output_builder, "(uxiausauaua{sv})",
                             i, /* ID */
                             (gint64)output->winsys_id,
                             (int)(output->crtc ? output->crtc - manager->crtcs : -1),
                             &crtcs,
                             output->name,
                             &modes,
                             &clones,
                             &properties);
    }

  for (i = 0; i < manager->n_modes; i++)
    {
      MetaMonitorMode *mode = &manager->modes[i];

      g_variant_builder_add (&mode_builder, "(uxuud)",
                             i, /* ID */
                             (gint64)mode->mode_id,
                             (guint32)mode->width,
                             (guint32)mode->height,
                             (double)mode->refresh_rate);
    }

  meta_dbus_display_config_complete_get_resources (skeleton,
                                                   invocation,
                                                   manager->serial,
                                                   g_variant_builder_end (&crtc_builder),
                                                   g_variant_builder_end (&output_builder),
                                                   g_variant_builder_end (&mode_builder),
                                                   manager->max_screen_width,
                                                   manager->max_screen_height);
  return TRUE;
}

static gboolean
output_can_config (MetaOutput      *output,
                   MetaCRTC        *crtc,
                   MetaMonitorMode *mode)
{
  unsigned int i;
  gboolean ok = FALSE;

  for (i = 0; i < output->n_possible_crtcs && !ok; i++)
    ok = output->possible_crtcs[i] == crtc;

  if (!ok)
    return FALSE;

  if (mode == NULL)
    return TRUE;

  ok = FALSE;
  for (i = 0; i < output->n_modes && !ok; i++)
    ok = output->modes[i] == mode;

  return ok;
}

static gboolean
output_can_clone (MetaOutput *output,
                  MetaOutput *clone)
{
  unsigned int i;
  gboolean ok = FALSE;

  for (i = 0; i < output->n_possible_clones && !ok; i++)
    ok = output->possible_clones[i] == clone;

  return ok;
}

void
meta_monitor_manager_apply_configuration (MetaMonitorManager *manager,
                                          MetaCRTCInfo       **crtcs,
                                          unsigned int         n_crtcs,
                                          MetaOutputInfo     **outputs,
                                          unsigned int         n_outputs)
{
  META_MONITOR_MANAGER_GET_CLASS (manager)->apply_configuration (manager,
                                                                 crtcs, n_crtcs,
                                                                 outputs, n_outputs);
}

static gboolean
save_config_timeout (gpointer user_data)
{
  MetaMonitorManager *manager = user_data;

  meta_monitor_config_restore_previous (manager->config, manager);

  manager->persistent_timeout_id = 0;
  return G_SOURCE_REMOVE;
}

static gboolean
meta_monitor_manager_handle_apply_configuration  (MetaDBusDisplayConfig *skeleton,
                                                  GDBusMethodInvocation *invocation,
                                                  guint                  serial,
                                                  gboolean               persistent,
                                                  GVariant              *crtcs,
                                                  GVariant              *outputs)
{
  MetaMonitorManager *manager = META_MONITOR_MANAGER (skeleton);
  GVariantIter crtc_iter, output_iter, *nested_outputs;
  GVariant *properties;
  guint crtc_id;
  int new_mode, x, y;
  int new_screen_width, new_screen_height;
  guint transform;
  guint output_index;
  GPtrArray *crtc_infos, *output_infos;

  if (serial != manager->serial)
    {
      g_dbus_method_invocation_return_error (invocation, G_DBUS_ERROR,
                                             G_DBUS_ERROR_ACCESS_DENIED,
                                             "The requested configuration is based on stale information");
      return TRUE;
    }

  crtc_infos = g_ptr_array_new_full (g_variant_n_children (crtcs),
                                     (GDestroyNotify) meta_crtc_info_free);
  output_infos = g_ptr_array_new_full (g_variant_n_children (outputs),
                                       (GDestroyNotify) meta_output_info_free);

  /* Validate all arguments */
  new_screen_width = 0; new_screen_height = 0;
  g_variant_iter_init (&crtc_iter, crtcs);
  while (g_variant_iter_loop (&crtc_iter, "(uiiiuaua{sv})",
                              &crtc_id, &new_mode, &x, &y, &transform,
                              &nested_outputs, NULL))
    {
      MetaCRTCInfo *crtc_info;
      MetaOutput *first_output;
      MetaCRTC *crtc;
      MetaMonitorMode *mode;

      crtc_info = g_slice_new (MetaCRTCInfo);
      crtc_info->outputs = g_ptr_array_new ();

      if (crtc_id >= manager->n_crtcs)
        {
          g_dbus_method_invocation_return_error (invocation, G_DBUS_ERROR,
                                                 G_DBUS_ERROR_INVALID_ARGS,
                                                 "Invalid CRTC id");
          return TRUE;
        }
      crtc = &manager->crtcs[crtc_id];
      crtc_info->crtc = crtc;

      if (new_mode != -1 && (new_mode < 0 || (unsigned)new_mode >= manager->n_modes))
        {
          g_dbus_method_invocation_return_error (invocation, G_DBUS_ERROR,
                                                 G_DBUS_ERROR_INVALID_ARGS,
                                                 "Invalid mode id");
          return TRUE;
        }
      mode = new_mode != -1 ? &manager->modes[new_mode] : NULL;
      crtc_info->mode = mode;

      if (mode)
        {
          int width, height;

          if (meta_monitor_transform_is_rotated (transform))
            {
              width = mode->height;
              height = mode->width;
            }
          else
            {
              width = mode->width;
              height = mode->height;
            }

          if (x < 0 ||
              x + width > manager->max_screen_width ||
              y < 0 ||
              y + height > manager->max_screen_height)
            {
              g_dbus_method_invocation_return_error (invocation, G_DBUS_ERROR,
                                                     G_DBUS_ERROR_INVALID_ARGS,
                                                     "Invalid CRTC geometry");
              return TRUE;
            }

          new_screen_width = MAX (new_screen_width, x + width);
          new_screen_height = MAX (new_screen_height, y + height);
          crtc_info->x = x;
          crtc_info->y = y;
        }
      else
        {
          crtc_info->x = 0;
          crtc_info->y = 0;
        }

      if (transform < META_MONITOR_TRANSFORM_NORMAL ||
          transform > META_MONITOR_TRANSFORM_FLIPPED_270 ||
          ((crtc->all_transforms & (1 << transform)) == 0))
        {
          g_dbus_method_invocation_return_error (invocation, G_DBUS_ERROR,
                                                 G_DBUS_ERROR_INVALID_ARGS,
                                                 "Invalid transform");
          return TRUE;
        }
      crtc_info->transform = transform;

      first_output = NULL;
      while (g_variant_iter_loop (nested_outputs, "u", &output_index))
        {
          MetaOutput *output;

          if (output_index >= manager->n_outputs)
            {
              g_dbus_method_invocation_return_error (invocation, G_DBUS_ERROR,
                                                     G_DBUS_ERROR_INVALID_ARGS,
                                                     "Invalid output id");
              return TRUE;
            }
          output = &manager->outputs[output_index];

          if (!output_can_config (output, crtc, mode))
            {
              g_dbus_method_invocation_return_error (invocation, G_DBUS_ERROR,
                                                     G_DBUS_ERROR_INVALID_ARGS,
                                                     "Output cannot be assigned to this CRTC or mode");
              return TRUE;
            }
          g_ptr_array_add (crtc_info->outputs, output);

          if (first_output)
            {
              if (!output_can_clone (output, first_output))
                {
                  g_dbus_method_invocation_return_error (invocation, G_DBUS_ERROR,
                                                         G_DBUS_ERROR_INVALID_ARGS,
                                                         "Outputs cannot be cloned");
                  return TRUE;
                }
            }
          else
            first_output = output;
        }

      if (!first_output && mode)
        {
          g_dbus_method_invocation_return_error (invocation, G_DBUS_ERROR,
                                                 G_DBUS_ERROR_INVALID_ARGS,
                                                 "Mode specified without outputs?");
          return TRUE;
        }

      g_ptr_array_add (crtc_infos, crtc_info);
    }

  if (new_screen_width == 0 || new_screen_height == 0)
    {
      g_dbus_method_invocation_return_error (invocation, G_DBUS_ERROR,
                                             G_DBUS_ERROR_INVALID_ARGS,
                                             "Refusing to disable all outputs");
      return TRUE;
    }

  g_variant_iter_init (&output_iter, outputs);
  while (g_variant_iter_loop (&output_iter, "(u@a{sv})", &output_index, &properties))
    {
      MetaOutputInfo *output_info;
      gboolean primary, presentation;

      if (output_index >= manager->n_outputs)
        {
          g_dbus_method_invocation_return_error (invocation, G_DBUS_ERROR,
                                                 G_DBUS_ERROR_INVALID_ARGS,
                                                 "Invalid output id");
          return TRUE;
        }

      output_info = g_slice_new0 (MetaOutputInfo);
      output_info->output = &manager->outputs[output_index];

      if (g_variant_lookup (properties, "primary", "b", &primary))
        output_info->is_primary = primary;

      if (g_variant_lookup (properties, "presentation", "b", &presentation))
        output_info->is_presentation = presentation;

      g_ptr_array_add (output_infos, output_info);
    }

  /* If we were in progress of making a persistent change and we see a
     new request, it's likely that the old one failed in some way, so
     don't save it, but also don't queue for restoring it.
  */
  if (manager->persistent_timeout_id && persistent)
    {
      g_source_remove (manager->persistent_timeout_id);
      manager->persistent_timeout_id = 0;
    }

  meta_monitor_manager_apply_configuration (manager,
                                            (MetaCRTCInfo**)crtc_infos->pdata,
                                            crtc_infos->len,
                                            (MetaOutputInfo**)output_infos->pdata,
                                            output_infos->len);

  g_ptr_array_unref (crtc_infos);
  g_ptr_array_unref (output_infos);

  /* Update MetaMonitorConfig data structures immediately so that we
     don't revert the change at the next XRandR event, then ask the plugin
     manager (through MetaScreen) to confirm the display change with the
     appropriate UI. Then wait 20 seconds and if not confirmed, revert the
     configuration.
  */
  meta_monitor_config_update_current (manager->config, manager);
  if (persistent)
    {
      manager->persistent_timeout_id = g_timeout_add_seconds (20, save_config_timeout, manager);
      g_source_set_name_by_id (manager->persistent_timeout_id, "[mutter] save_config_timeout");
      g_signal_emit (manager, signals[CONFIRM_DISPLAY_CHANGE], 0);
    }

  meta_dbus_display_config_complete_apply_configuration (skeleton, invocation);
  return TRUE;
}

void
meta_monitor_manager_confirm_configuration (MetaMonitorManager *manager,
                                            gboolean            ok)
{
  if (!manager->persistent_timeout_id)
    {
      /* too late */
      return;
    }

  g_source_remove (manager->persistent_timeout_id);
  manager->persistent_timeout_id = 0;

  if (ok)
    meta_monitor_config_make_persistent (manager->config);
  else
    meta_monitor_config_restore_previous (manager->config, manager);
}

static gboolean
meta_monitor_manager_handle_change_backlight  (MetaDBusDisplayConfig *skeleton,
                                               GDBusMethodInvocation *invocation,
                                               guint                  serial,
                                               guint                  output_index,
                                               gint                   value)
{
  MetaMonitorManager *manager = META_MONITOR_MANAGER (skeleton);
  MetaOutput *output;

  if (serial != manager->serial)
    {
      g_dbus_method_invocation_return_error (invocation, G_DBUS_ERROR,
                                             G_DBUS_ERROR_ACCESS_DENIED,
                                             "The requested configuration is based on stale information");
      return TRUE;
    }

  if (output_index >= manager->n_outputs)
    {
      g_dbus_method_invocation_return_error (invocation, G_DBUS_ERROR,
                                             G_DBUS_ERROR_INVALID_ARGS,
                                             "Invalid output id");
      return TRUE;
    }
  output = &manager->outputs[output_index];

  if (value < 0 || value > 100)
    {
      g_dbus_method_invocation_return_error (invocation, G_DBUS_ERROR,
                                             G_DBUS_ERROR_INVALID_ARGS,
                                             "Invalid backlight value");
      return TRUE;
    }

  if (output->backlight == -1 ||
      (output->backlight_min == 0 && output->backlight_max == 0))
    {
      g_dbus_method_invocation_return_error (invocation, G_DBUS_ERROR,
                                             G_DBUS_ERROR_INVALID_ARGS,
                                             "Output does not support changing backlight");
      return TRUE;
    }

  META_MONITOR_MANAGER_GET_CLASS (manager)->change_backlight (manager, output, value);

  meta_dbus_display_config_complete_change_backlight (skeleton, invocation, output->backlight);
  return TRUE;
}

static gboolean
meta_monitor_manager_handle_get_crtc_gamma  (MetaDBusDisplayConfig *skeleton,
                                             GDBusMethodInvocation *invocation,
                                             guint                  serial,
                                             guint                  crtc_id)
{
  MetaMonitorManager *manager = META_MONITOR_MANAGER (skeleton);
  MetaMonitorManagerClass *klass;
  MetaCRTC *crtc;
  gsize size;
  unsigned short *red;
  unsigned short *green;
  unsigned short *blue;
  GBytes *red_bytes, *green_bytes, *blue_bytes;
  GVariant *red_v, *green_v, *blue_v;

  if (serial != manager->serial)
    {
      g_dbus_method_invocation_return_error (invocation, G_DBUS_ERROR,
                                             G_DBUS_ERROR_ACCESS_DENIED,
                                             "The requested configuration is based on stale information");
      return TRUE;
    }

  if (crtc_id >= manager->n_crtcs)
    {
      g_dbus_method_invocation_return_error (invocation, G_DBUS_ERROR,
                                             G_DBUS_ERROR_INVALID_ARGS,
                                             "Invalid crtc id");
      return TRUE;
    }
  crtc = &manager->crtcs[crtc_id];

  klass = META_MONITOR_MANAGER_GET_CLASS (manager);
  if (klass->get_crtc_gamma)
    klass->get_crtc_gamma (manager, crtc, &size, &red, &green, &blue);
  else
    {
      size = 0;
      red = green = blue = NULL;
    }

  red_bytes = g_bytes_new_take (red, size * sizeof (unsigned short));
  green_bytes = g_bytes_new_take (green, size * sizeof (unsigned short));
  blue_bytes = g_bytes_new_take (blue, size * sizeof (unsigned short));

  red_v = g_variant_new_from_bytes (G_VARIANT_TYPE ("aq"), red_bytes, TRUE);
  green_v = g_variant_new_from_bytes (G_VARIANT_TYPE ("aq"), green_bytes, TRUE);
  blue_v = g_variant_new_from_bytes (G_VARIANT_TYPE ("aq"), blue_bytes, TRUE);

  meta_dbus_display_config_complete_get_crtc_gamma (skeleton, invocation,
                                                    red_v, green_v, blue_v);

  g_bytes_unref (red_bytes);
  g_bytes_unref (green_bytes);
  g_bytes_unref (blue_bytes);

  return TRUE;
}

static gboolean
meta_monitor_manager_handle_set_crtc_gamma  (MetaDBusDisplayConfig *skeleton,
                                             GDBusMethodInvocation *invocation,
                                             guint                  serial,
                                             guint                  crtc_id,
                                             GVariant              *red_v,
                                             GVariant              *green_v,
                                             GVariant              *blue_v)
{
  MetaMonitorManager *manager = META_MONITOR_MANAGER (skeleton);
  MetaMonitorManagerClass *klass;
  MetaCRTC *crtc;
  gsize size, dummy;
  unsigned short *red;
  unsigned short *green;
  unsigned short *blue;
  GBytes *red_bytes, *green_bytes, *blue_bytes;

  if (serial != manager->serial)
    {
      g_dbus_method_invocation_return_error (invocation, G_DBUS_ERROR,
                                             G_DBUS_ERROR_ACCESS_DENIED,
                                             "The requested configuration is based on stale information");
      return TRUE;
    }

  if (crtc_id >= manager->n_crtcs)
    {
      g_dbus_method_invocation_return_error (invocation, G_DBUS_ERROR,
                                             G_DBUS_ERROR_INVALID_ARGS,
                                             "Invalid crtc id");
      return TRUE;
    }
  crtc = &manager->crtcs[crtc_id];

  red_bytes = g_variant_get_data_as_bytes (red_v);
  green_bytes = g_variant_get_data_as_bytes (green_v);
  blue_bytes = g_variant_get_data_as_bytes (blue_v);

  size = g_bytes_get_size (red_bytes) / sizeof (unsigned short);
  red = (unsigned short*) g_bytes_get_data (red_bytes, &dummy);
  green = (unsigned short*) g_bytes_get_data (green_bytes, &dummy);
  blue = (unsigned short*) g_bytes_get_data (blue_bytes, &dummy);

  klass = META_MONITOR_MANAGER_GET_CLASS (manager);
  if (klass->set_crtc_gamma)
    klass->set_crtc_gamma (manager, crtc, size, red, green, blue);

  meta_dbus_display_config_complete_set_crtc_gamma (skeleton, invocation);

  g_bytes_unref (red_bytes);
  g_bytes_unref (green_bytes);
  g_bytes_unref (blue_bytes);

  return TRUE;
}

static void
meta_monitor_manager_display_config_init (MetaDBusDisplayConfigIface *iface)
{
  iface->handle_get_resources = meta_monitor_manager_handle_get_resources;
  iface->handle_apply_configuration = meta_monitor_manager_handle_apply_configuration;
  iface->handle_change_backlight = meta_monitor_manager_handle_change_backlight;
  iface->handle_get_crtc_gamma = meta_monitor_manager_handle_get_crtc_gamma;
  iface->handle_set_crtc_gamma = meta_monitor_manager_handle_set_crtc_gamma;
}

static void
on_bus_acquired (GDBusConnection *connection,
                 const char      *name,
                 gpointer         user_data)
{
  MetaMonitorManager *manager = user_data;

  g_dbus_interface_skeleton_export (G_DBUS_INTERFACE_SKELETON (manager),
                                    connection,
                                    "/org/gnome/Mutter/DisplayConfig",
                                    NULL);
}

static void
on_name_acquired (GDBusConnection *connection,
                  const char      *name,
                  gpointer         user_data)
{
  meta_topic (META_DEBUG_DBUS, "Acquired name %s\n", name);
}

static void
on_name_lost (GDBusConnection *connection,
              const char      *name,
              gpointer         user_data)
{
  meta_topic (META_DEBUG_DBUS, "Lost or failed to acquire name %s\n", name);
}

static void
initialize_dbus_interface (MetaMonitorManager *manager)
{
  manager->dbus_name_id = g_bus_own_name (G_BUS_TYPE_SESSION,
                                          "org.gnome.Mutter.DisplayConfig",
                                          G_BUS_NAME_OWNER_FLAGS_ALLOW_REPLACEMENT |
                                          (meta_get_replace_current_wm () ?
                                           G_BUS_NAME_OWNER_FLAGS_REPLACE : 0),
                                          on_bus_acquired,
                                          on_name_acquired,
                                          on_name_lost,
                                          g_object_ref (manager),
                                          g_object_unref);
}

MetaMonitorManager *
meta_monitor_manager_get (void)
{
  MetaBackend *backend = meta_get_backend ();

  return meta_backend_get_monitor_manager (backend);
}

MetaMonitorInfo *
meta_monitor_manager_get_monitor_infos (MetaMonitorManager *manager,
                                        unsigned int       *n_infos)
{
  *n_infos = manager->n_monitor_infos;
  return manager->monitor_infos;
}

MetaOutput *
meta_monitor_manager_get_outputs (MetaMonitorManager *manager,
                                  unsigned int       *n_outputs)
{
  *n_outputs = manager->n_outputs;
  return manager->outputs;
}

void
meta_monitor_manager_get_resources (MetaMonitorManager  *manager,
                                    MetaMonitorMode    **modes,
                                    unsigned int        *n_modes,
                                    MetaCRTC           **crtcs,
                                    unsigned int        *n_crtcs,
                                    MetaOutput         **outputs,
                                    unsigned int        *n_outputs)
{
  if (modes)
    {
      *modes = manager->modes;
      *n_modes = manager->n_modes;
    }
  if (crtcs)
    {
      *crtcs = manager->crtcs;
      *n_crtcs = manager->n_crtcs;
    }
  if (outputs)
    {
      *outputs = manager->outputs;
      *n_outputs = manager->n_outputs;
    }
}

int
meta_monitor_manager_get_primary_index (MetaMonitorManager *manager)
{
  return manager->primary_monitor_index;
}

void
meta_monitor_manager_get_screen_size (MetaMonitorManager *manager,
                                      int                *width,
                                      int                *height)
{
  *width = manager->screen_width;
  *height = manager->screen_height;
}

void
meta_monitor_manager_get_screen_limits (MetaMonitorManager *manager,
                                        int                *width,
                                        int                *height)
{
  *width = manager->max_screen_width;
  *height = manager->max_screen_height;
}

void
meta_monitor_manager_rebuild_derived (MetaMonitorManager *manager)
{
  MetaMonitorInfo *old_monitor_infos;

  old_monitor_infos = manager->monitor_infos;

  if (manager->in_init)
    return;

  make_logical_config (manager);

  g_signal_emit_by_name (manager, "monitors-changed");

  g_free (old_monitor_infos);
}

