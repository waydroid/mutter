/*
 * Clutter.
 *
 * An OpenGL based 'interactive canvas' library.
 *
 * Copyright (C) 2012  Intel Corp.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library. If not, see <http://www.gnu.org/licenses/>.
 *
 *
 */

#ifndef __CLUTTER_EVDEV_H__
#define __CLUTTER_EVDEV_H__

#include <glib.h>
#include <glib-object.h>
#include <xkbcommon/xkbcommon.h>
#include <clutter/clutter.h>
#include <libinput.h>

G_BEGIN_DECLS

#if !defined(CLUTTER_ENABLE_COMPOSITOR_API) && !defined(CLUTTER_COMPILATION)
#error "You need to define CLUTTER_ENABLE_COMPOSITOR_API before including clutter-evdev.h"
#endif

/**
 * ClutterOpenDeviceCallback:
 * @path: the device path
 * @flags: flags to be passed to open
 *
 * This callback will be called when Clutter needs to access an input
 * device. It should return an open file descriptor for the file at @path,
 * or -1 if opening failed.
 */
typedef int (*ClutterOpenDeviceCallback) (const char  *path,
					  int          flags,
					  gpointer     user_data,
					  GError     **error);
typedef void (*ClutterCloseDeviceCallback) (int          fd,
					    gpointer     user_data);

CLUTTER_AVAILABLE_IN_1_16
void  clutter_evdev_set_device_callbacks (ClutterOpenDeviceCallback  open_callback,
                                          ClutterCloseDeviceCallback close_callback,
                                          gpointer                   user_data);

CLUTTER_AVAILABLE_IN_1_10
void  clutter_evdev_release_devices (void);
CLUTTER_AVAILABLE_IN_1_10
void  clutter_evdev_reclaim_devices (void);

/**
 * ClutterPointerConstrainCallback:
 * @device: the core pointer device
 * @time: the event time in milliseconds
 * @x: (inout): the new X coordinate
 * @y: (inout): the new Y coordinate
 * @user_data: user data passed to this function
 *
 * This callback will be called for all pointer motion events, and should
 * update (@x, @y) to constrain the pointer position appropriately.
 * The subsequent motion event will use the updated values as the new coordinates.
 * Note that the coordinates are not clamped to the stage size, and the callback
 * must make sure that this happens before it returns.
 * Also note that the event will be emitted even if the pointer is constrained
 * to be in the same position.
 *
 * Since: 1.16
 */
typedef void (*ClutterPointerConstrainCallback) (ClutterInputDevice *device,
						 guint32             time,
						 float               prev_x,
						 float               prev_y,
						 float              *x,
						 float              *y,
						 gpointer            user_data);

CLUTTER_AVAILABLE_IN_1_16
void  clutter_evdev_set_pointer_constrain_callback (ClutterDeviceManager            *evdev,
						    ClutterPointerConstrainCallback  callback,
						    gpointer                         user_data,
						    GDestroyNotify                   user_data_notify);

CLUTTER_AVAILABLE_IN_1_16
void               clutter_evdev_set_keyboard_map   (ClutterDeviceManager *evdev,
						     struct xkb_keymap    *keymap);

CLUTTER_AVAILABLE_IN_1_18
struct xkb_keymap * clutter_evdev_get_keyboard_map (ClutterDeviceManager *evdev);

CLUTTER_AVAILABLE_IN_1_20
void clutter_evdev_set_keyboard_layout_index (ClutterDeviceManager *evdev,
                                              xkb_layout_index_t    idx);

CLUTTER_AVAILABLE_IN_1_18
void clutter_evdev_set_keyboard_repeat (ClutterDeviceManager *evdev,
                                        gboolean              repeat,
                                        guint32               delay,
                                        guint32               interval);

typedef gboolean (* ClutterEvdevFilterFunc) (struct libinput_event *event,
                                             gpointer               data);

CLUTTER_AVAILABLE_IN_1_20
void clutter_evdev_add_filter    (ClutterEvdevFilterFunc func,
                                  gpointer               data,
                                  GDestroyNotify         destroy_notify);
CLUTTER_AVAILABLE_IN_1_20
void clutter_evdev_remove_filter (ClutterEvdevFilterFunc func,
                                  gpointer               data);
CLUTTER_AVAILABLE_IN_1_20
struct libinput_device * clutter_evdev_input_device_get_libinput_device (ClutterInputDevice *device);

CLUTTER_AVAILABLE_IN_1_20
gint32 clutter_evdev_event_sequence_get_slot (const ClutterEventSequence *sequence);

CLUTTER_AVAILABLE_IN_1_20
void clutter_evdev_warp_pointer (ClutterInputDevice   *pointer_device,
                                 guint32               time_,
                                 int                   x,
                                 int                   y);

CLUTTER_AVAILABLE_IN_1_26
guint32 clutter_evdev_event_get_event_code (const ClutterEvent *event);

CLUTTER_AVAILABLE_IN_1_26
guint64 clutter_evdev_event_get_time_usec (const ClutterEvent *event);

CLUTTER_AVAILABLE_IN_1_26
gboolean clutter_evdev_event_get_relative_motion (const ClutterEvent *event,
                                                  double             *dx,
                                                  double             *dy,
                                                  double             *dx_unaccel,
                                                  double             *dy_unaccel);

G_END_DECLS

#endif /* __CLUTTER_EVDEV_H__ */
