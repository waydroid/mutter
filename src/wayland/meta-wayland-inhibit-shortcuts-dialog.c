/*
 * Copyright (C) 2017 Red Hat
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

#include <config.h>

#include "wayland/meta-window-wayland.h"
#include "wayland/meta-wayland.h"
#include "core/window-private.h"
#include "compositor/compositor-private.h"
#include "meta-wayland-inhibit-shortcuts-dialog.h"

static GQuark quark_surface_inhibit_shortcuts_data = 0;

typedef struct _InhibitShortcutsData
{
  MetaWaylandSurface                *surface;
  MetaWaylandSeat                   *seat;
  MetaInhibitShortcutsDialog        *dialog;
  MetaInhibitShortcutsDialogResponse last_response;
} InhibitShortcutsData;

static InhibitShortcutsData *
surface_inhibit_shortcuts_data_get (MetaWaylandSurface *surface)
{
  return g_object_get_qdata (G_OBJECT (surface),
                             quark_surface_inhibit_shortcuts_data);
}

static void
surface_inhibit_shortcuts_data_set (MetaWaylandSurface   *surface,
                                    InhibitShortcutsData *data)
{
  g_object_set_qdata (G_OBJECT (surface),
                      quark_surface_inhibit_shortcuts_data,
                      data);
}

static void
surface_inhibit_shortcuts_data_free (MetaWaylandSurface   *surface,
                                     InhibitShortcutsData *data)
{
  meta_inhibit_shortcuts_dialog_hide (data->dialog);

  g_free (data);
}

static void
surface_inhibit_shortcuts_dialog_free (gpointer  ptr,
                                       GClosure *closure)
{
  InhibitShortcutsData *data = ptr;

  meta_wayland_surface_hide_inhibit_shortcuts_dialog (data->surface);
}

static void
inhibit_shortcuts_dialog_response_apply (InhibitShortcutsData *data)
{
  if (data->last_response == META_INHIBIT_SHORTCUTS_DIALOG_RESPONSE_ALLOW)
    meta_wayland_surface_inhibit_shortcuts (data->surface, data->seat);
  else if (meta_wayland_surface_is_shortcuts_inhibited (data->surface, data->seat))
    meta_wayland_surface_restore_shortcuts (data->surface, data->seat);
}

static void
inhibit_shortcuts_dialog_response_cb (MetaInhibitShortcutsDialog        *dialog,
                                      MetaInhibitShortcutsDialogResponse response,
                                      InhibitShortcutsData              *data)
{
  data->last_response = response;
  inhibit_shortcuts_dialog_response_apply (data);
  meta_wayland_surface_hide_inhibit_shortcuts_dialog (data->surface);
}

static InhibitShortcutsData *
meta_wayland_surface_ensure_inhibit_shortcuts_dialog (MetaWaylandSurface *surface,
                                                      MetaWaylandSeat    *seat)
{
  InhibitShortcutsData *data;
  MetaWindow *window;
  MetaDisplay *display;
  MetaInhibitShortcutsDialog *dialog;

  data = surface_inhibit_shortcuts_data_get (surface);
  if (data == NULL)
    {
      data = g_new (InhibitShortcutsData, 1);
      surface_inhibit_shortcuts_data_set (surface, data);
    }
  else if (data->dialog != NULL)
     /* There is a dialog already created, nothing to do */
    return data;

  window = meta_wayland_surface_get_toplevel_window (surface);
  display = window->display;
  dialog =
    meta_compositor_create_inhibit_shortcuts_dialog (display->compositor,
                                                     window);

  data->surface = surface;
  data->seat = seat;
  data->dialog = dialog;

  g_signal_connect_data (dialog, "response",
                         G_CALLBACK (inhibit_shortcuts_dialog_response_cb),
                         data, surface_inhibit_shortcuts_dialog_free,
                         0);

  g_signal_connect (surface, "destroy",
                    G_CALLBACK (surface_inhibit_shortcuts_data_free),
                    data);

  return data;
}

void
meta_wayland_surface_show_inhibit_shortcuts_dialog (MetaWaylandSurface *surface,
                                                    MetaWaylandSeat    *seat)
{
  InhibitShortcutsData *data;

  g_return_if_fail (META_IS_WAYLAND_SURFACE (surface));

  data = surface_inhibit_shortcuts_data_get (surface);
  if (data != NULL)
    {
      /* The dialog was shown before for this surface but is not showing
       * anymore, reuse the last user response.
       */
      inhibit_shortcuts_dialog_response_apply (data);
      return;
    }

  data = meta_wayland_surface_ensure_inhibit_shortcuts_dialog (surface, seat);
  meta_inhibit_shortcuts_dialog_show (data->dialog);
}

void
meta_wayland_surface_hide_inhibit_shortcuts_dialog (MetaWaylandSurface *surface)
{
  InhibitShortcutsData *data;

  g_return_if_fail (META_IS_WAYLAND_SURFACE (surface));

  /* The closure notify will take care of actually hiding the dialog */
  data = surface_inhibit_shortcuts_data_get (surface);
  g_signal_handlers_disconnect_by_data (surface, data);
}

void
meta_wayland_surface_inhibit_shortcuts_dialog_init (void)
{
  quark_surface_inhibit_shortcuts_data =
    g_quark_from_static_string ("-meta-wayland-surface-inhibit-shortcuts-data");
}
