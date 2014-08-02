/*
 * Wayland Support
 *
 * Copyright (C) 2012 Intel Corporation
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

#ifndef META_WAYLAND_SEAT_H
#define META_WAYLAND_SEAT_H

#include <wayland-server.h>
#include <clutter/clutter.h>

#include "meta-wayland-types.h"
#include "meta-wayland-keyboard.h"
#include "meta-wayland-pointer.h"

struct _MetaWaylandDataOffer
{
  struct wl_resource *resource;
  MetaWaylandDataSource *source;
  struct wl_listener source_destroy_listener;
};

struct _MetaWaylandDataSource
{
  struct wl_resource *resource;
  struct wl_array mime_types;
};

struct _MetaWaylandSeat
{
  struct wl_list base_resource_list;

  uint32_t selection_serial;
  MetaWaylandDataSource *selection_data_source;
  struct wl_listener selection_data_source_listener;

  struct wl_list data_device_resource_list;
  MetaWaylandPointer pointer;
  MetaWaylandKeyboard keyboard;

  struct wl_display *display;
};

void meta_wayland_seat_init (MetaWaylandCompositor *compositor);

void meta_wayland_seat_free (MetaWaylandSeat *seat);

void meta_wayland_seat_update (MetaWaylandSeat    *seat,
                               const ClutterEvent *event);

gboolean meta_wayland_seat_handle_event (MetaWaylandSeat *seat,
                                         const ClutterEvent *event);

void meta_wayland_seat_repick (MetaWaylandSeat *seat);
void meta_wayland_seat_update_cursor_surface (MetaWaylandSeat *seat);

#endif /* META_WAYLAND_SEAT_H */
