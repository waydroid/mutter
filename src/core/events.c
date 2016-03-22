/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */

/*
 * Copyright (C) 2001 Havoc Pennington
 * Copyright (C) 2002, 2003, 2004 Red Hat, Inc.
 * Copyright (C) 2003, 2004 Rob Adams
 * Copyright (C) 2004-2006 Elijah Newren
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
#include "events.h"

#include <meta/meta-backend.h>

#include "display-private.h"
#include "window-private.h"
#include "backends/x11/meta-backend-x11.h"
#include "backends/meta-cursor-tracker-private.h"

#ifdef HAVE_NATIVE_BACKEND
#include "backends/native/meta-backend-native.h"
#include "backends/native/meta-idle-monitor-native.h"
#endif

#ifdef HAVE_WAYLAND
#include "wayland/meta-wayland-private.h"
#endif
#include "meta-surface-actor.h"

#define IS_GESTURE_EVENT(e) ((e)->type == CLUTTER_TOUCHPAD_SWIPE || \
                             (e)->type == CLUTTER_TOUCHPAD_PINCH || \
                             (e)->type == CLUTTER_TOUCH_BEGIN || \
                             (e)->type == CLUTTER_TOUCH_UPDATE || \
                             (e)->type == CLUTTER_TOUCH_END || \
                             (e)->type == CLUTTER_TOUCH_CANCEL)

static MetaWindow *
get_window_for_event (MetaDisplay        *display,
                      const ClutterEvent *event)
{
  switch (display->event_route)
    {
    case META_EVENT_ROUTE_NORMAL:
      {
        ClutterActor *source;

        /* Always use the key focused window for key events. */
        switch (event->type)
          {
          case CLUTTER_KEY_PRESS:
          case CLUTTER_KEY_RELEASE:
            return display->focus_window;
          default:
            break;
          }

        source = clutter_event_get_source (event);
        if (META_IS_SURFACE_ACTOR (source))
          return meta_surface_actor_get_window (META_SURFACE_ACTOR (source));
        else
          return NULL;
      }
    case META_EVENT_ROUTE_WINDOW_OP:
    case META_EVENT_ROUTE_COMPOSITOR_GRAB:
    case META_EVENT_ROUTE_WAYLAND_POPUP:
    case META_EVENT_ROUTE_FRAME_BUTTON:
      return display->grab_window;
    default:
      g_assert_not_reached ();
    }
}

static void
handle_idletime_for_event (const ClutterEvent *event)
{
#ifdef HAVE_NATIVE_BACKEND
  /* This is handled by XSync under X11. */
  MetaBackend *backend = meta_get_backend ();

  if (META_IS_BACKEND_NATIVE (backend))
    {
      ClutterInputDevice *device, *source_device;
      MetaIdleMonitor *core_monitor, *device_monitor;
      int device_id;

      device = clutter_event_get_device (event);
      if (device == NULL)
        return;

      if (event->any.flags & CLUTTER_EVENT_FLAG_SYNTHETIC ||
          event->type == CLUTTER_ENTER ||
          event->type == CLUTTER_LEAVE ||
          event->type == CLUTTER_STAGE_STATE ||
          event->type == CLUTTER_DESTROY_NOTIFY ||
          event->type == CLUTTER_CLIENT_MESSAGE ||
          event->type == CLUTTER_DELETE)
        return;

      device_id = clutter_input_device_get_device_id (device);

      core_monitor = meta_idle_monitor_get_core ();
      device_monitor = meta_idle_monitor_get_for_device (device_id);

      meta_idle_monitor_native_reset_idletime (core_monitor);
      meta_idle_monitor_native_reset_idletime (device_monitor);

      source_device = clutter_event_get_source_device (event);
      if (source_device != device)
        {
          device_id = clutter_input_device_get_device_id (device);
          device_monitor = meta_idle_monitor_get_for_device (device_id);
          meta_idle_monitor_native_reset_idletime (device_monitor);
        }
    }
#endif /* HAVE_NATIVE_BACKEND */
}

static gboolean
sequence_is_pointer_emulated (MetaDisplay        *display,
                              const ClutterEvent *event)
{
  ClutterEventSequence *sequence;

  sequence = clutter_event_get_event_sequence (event);

  if (!sequence)
    return FALSE;

  if (clutter_event_is_pointer_emulated (event))
    return TRUE;

#ifdef HAVE_NATIVE_BACKEND
  MetaBackend *backend = meta_get_backend ();

  /* When using Clutter's native input backend there is no concept of
   * pointer emulating sequence, we still must make up our own to be
   * able to implement single-touch (hence pointer alike) behavior.
   *
   * This is implemented similarly to X11, where only the first touch
   * on screen gets the "pointer emulated" flag, and it won't get assigned
   * to another sequence until the next first touch on an idle touchscreen.
   */
  if (META_IS_BACKEND_NATIVE (backend))
    {
      MetaGestureTracker *tracker;

      tracker = meta_display_get_gesture_tracker (display);

      if (event->type == CLUTTER_TOUCH_BEGIN &&
          meta_gesture_tracker_get_n_current_touches (tracker) == 0)
        return TRUE;
    }
#endif /* HAVE_NATIVE_BACKEND */

  return FALSE;
}

static gboolean
meta_display_handle_event (MetaDisplay        *display,
                           const ClutterEvent *event)
{
  MetaWindow *window;
  gboolean bypass_clutter = FALSE;
  G_GNUC_UNUSED gboolean bypass_wayland = FALSE;
  MetaGestureTracker *tracker;
  ClutterEventSequence *sequence;
  ClutterInputDevice *source;

  sequence = clutter_event_get_event_sequence (event);

  /* Set the pointer emulating sequence on touch begin, if eligible */
  if (event->type == CLUTTER_TOUCH_BEGIN)
    {
      if (sequence_is_pointer_emulated (display, event))
        {
          /* This is the new pointer emulating sequence */
          display->pointer_emulating_sequence = sequence;
        }
      else if (display->pointer_emulating_sequence == sequence)
        {
          /* This sequence was "pointer emulating" in a prior incarnation,
           * but now it isn't. We unset the pointer emulating sequence at
           * this point so the current sequence is not mistaken as pointer
           * emulating, while we've ensured that it's been deemed
           * "pointer emulating" throughout all of the event processing
           * of the previous incarnation.
           */
          display->pointer_emulating_sequence = NULL;
        }
    }

#ifdef HAVE_WAYLAND
  MetaWaylandCompositor *compositor = NULL;
  if (meta_is_wayland_compositor ())
    {
      compositor = meta_wayland_compositor_get_default ();
      meta_wayland_compositor_update (compositor, event);
    }
#endif

  source = clutter_event_get_source_device (event);

  if (source)
    {
      meta_backend_update_last_device (meta_get_backend (),
                                       clutter_input_device_get_device_id (source));
    }

  if (meta_is_wayland_compositor () && event->type == CLUTTER_MOTION)
    {
      meta_cursor_tracker_update_position (meta_cursor_tracker_get_for_screen (NULL),
                                           event->motion.x, event->motion.y);
      display->monitor_cache_invalidated = TRUE;
    }

  handle_idletime_for_event (event);

  window = get_window_for_event (display, event);

  display->current_time = event->any.time;

  if (window && !window->override_redirect &&
      (event->type == CLUTTER_KEY_PRESS ||
       event->type == CLUTTER_BUTTON_PRESS ||
       event->type == CLUTTER_TOUCH_BEGIN))
    {
      if (CurrentTime == display->current_time)
        {
          /* We can't use missing (i.e. invalid) timestamps to set user time,
           * nor do we want to use them to sanity check other timestamps.
           * See bug 313490 for more details.
           */
          meta_warning ("Event has no timestamp! You may be using a broken "
                        "program such as xse.  Please ask the authors of that "
                        "program to fix it.\n");
        }
      else
        {
          meta_window_set_user_time (window, display->current_time);
          meta_display_sanity_check_timestamps (display, display->current_time);
        }
    }

  tracker = meta_display_get_gesture_tracker (display);

  if (meta_gesture_tracker_handle_event (tracker, event))
    {
      bypass_wayland = bypass_clutter = TRUE;
      goto out;
    }

  if (display->event_route == META_EVENT_ROUTE_WINDOW_OP)
    {
      if (meta_window_handle_mouse_grab_op_event (window, event))
        {
          bypass_clutter = TRUE;
          bypass_wayland = TRUE;
          goto out;
        }
    }

  /* For key events, it's important to enforce single-handling, or
   * we can get into a confused state. So if a keybinding is
   * handled (because it's one of our hot-keys, or because we are
   * in a keyboard-grabbed mode like moving a window, we don't
   * want to pass the key event to the compositor or Wayland at all.
   */
  if (meta_keybindings_process_event (display, window, event))
    {
      bypass_clutter = TRUE;
      bypass_wayland = TRUE;
      goto out;
    }

  if (window)
    {
      /* Events that are likely to trigger compositor gestures should
       * be known to clutter so they can propagate along the hierarchy.
       * Gesture-wise, there's two groups of events we should be getting
       * here:
       * - CLUTTER_TOUCH_* with a touch sequence that's not yet accepted
       *   by the gesture tracker, these might trigger gesture actions
       *   into recognition. Already accepted touch sequences are handled
       *   directly by meta_gesture_tracker_handle_event().
       * - CLUTTER_TOUCHPAD_* events over windows. These can likewise
       *   trigger ::captured-event handlers along the way.
       */
      bypass_clutter = !IS_GESTURE_EVENT (event);

      meta_window_handle_ungrabbed_event (window, event);

      /* This might start a grab op. If it does, then filter out the
       * event, and if it doesn't, replay the event to release our
       * own sync grab. */

      if (display->event_route == META_EVENT_ROUTE_WINDOW_OP ||
          display->event_route == META_EVENT_ROUTE_FRAME_BUTTON)
        {
          bypass_clutter = TRUE;
          bypass_wayland = TRUE;
        }
      else
        {
          /* Only replay button press events, since that's where we
           * have the synchronous grab. */
          if (event->type == CLUTTER_BUTTON_PRESS)
            {
              MetaBackend *backend = meta_get_backend ();
              if (META_IS_BACKEND_X11 (backend))
                {
                  Display *xdisplay = meta_backend_x11_get_xdisplay (META_BACKEND_X11 (backend));
                  meta_verbose ("Allowing events time %u\n",
                                (unsigned int)event->button.time);
                  XIAllowEvents (xdisplay, clutter_event_get_device_id (event),
                                 XIReplayDevice, event->button.time);
                }
            }
        }

      goto out;
    }

 out:
  /* If the compositor has a grab, don't pass that through to Wayland */
  if (display->event_route == META_EVENT_ROUTE_COMPOSITOR_GRAB)
    bypass_wayland = TRUE;

  /* If a Wayland client has a grab, don't pass that through to Clutter */
  if (display->event_route == META_EVENT_ROUTE_WAYLAND_POPUP)
    bypass_clutter = TRUE;

#ifdef HAVE_WAYLAND
  if (compositor && !bypass_wayland)
    {
      if (meta_wayland_compositor_handle_event (compositor, event))
        bypass_clutter = TRUE;
    }
#endif

  display->current_time = CurrentTime;
  return bypass_clutter;
}

static gboolean
event_callback (const ClutterEvent *event,
                gpointer            data)
{
  MetaDisplay *display = data;

  return meta_display_handle_event (display, event);
}

void
meta_display_init_events (MetaDisplay *display)
{
  display->clutter_event_filter = clutter_event_add_filter (NULL,
                                                            event_callback,
                                                            NULL,
                                                            display);
}

void
meta_display_free_events (MetaDisplay *display)
{
  clutter_event_remove_filter (display->clutter_event_filter);
  display->clutter_event_filter = 0;
}
