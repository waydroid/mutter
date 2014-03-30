/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */

/*
 * PLEASE KEEP IN SYNC WITH GSETTINGS SCHEMAS!
 */

/* 
 * Copyright (C) 2001 Havoc Pennington
 * Copyright (C) 2005 Elijah Newren
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

#ifndef META_COMMON_H
#define META_COMMON_H

/* Don't include core headers here */
#include <X11/Xlib.h>
#include <X11/extensions/XInput.h>
#include <X11/extensions/XInput2.h>
#include <glib.h>
#include <gtk/gtk.h>

/**
 * SECTION:common
 * @Title: Common
 * @Short_Description: Mutter common types
 */

/* This is set in stone and also hard-coded in GDK. */
#define META_VIRTUAL_CORE_POINTER_ID 2
#define META_VIRTUAL_CORE_KEYBOARD_ID 3

typedef struct _MetaResizePopup MetaResizePopup;

/**
 * MetaFrameFlags:
 * @META_FRAME_ALLOWS_DELETE: frame allows delete
 * @META_FRAME_ALLOWS_MENU: frame allows menu
 * @META_FRAME_ALLOWS_MINIMIZE: frame allows minimize
 * @META_FRAME_ALLOWS_MAXIMIZE: frame allows maximize
 * @META_FRAME_ALLOWS_VERTICAL_RESIZE: frame allows vertical resize
 * @META_FRAME_ALLOWS_HORIZONTAL_RESIZE: frame allows horizontal resize
 * @META_FRAME_HAS_FOCUS: frame has focus
 * @META_FRAME_SHADED: frame is shaded
 * @META_FRAME_STUCK: frame is stuck
 * @META_FRAME_MAXIMIZED: frame is maximized
 * @META_FRAME_ALLOWS_SHADE: frame allows shade
 * @META_FRAME_ALLOWS_MOVE: frame allows move
 * @META_FRAME_FULLSCREEN: frame allows fullscreen
 * @META_FRAME_IS_FLASHING: frame is flashing
 * @META_FRAME_ABOVE: frame is above
 * @META_FRAME_TILED_LEFT: frame is tiled to the left
 * @META_FRAME_TILED_RIGHT: frame is tiled to the right
 */
typedef enum
{
  META_FRAME_ALLOWS_DELETE            = 1 << 0,
  META_FRAME_ALLOWS_MENU              = 1 << 1,
  META_FRAME_ALLOWS_MINIMIZE          = 1 << 2,
  META_FRAME_ALLOWS_MAXIMIZE          = 1 << 3,
  META_FRAME_ALLOWS_VERTICAL_RESIZE   = 1 << 4,
  META_FRAME_ALLOWS_HORIZONTAL_RESIZE = 1 << 5,
  META_FRAME_HAS_FOCUS                = 1 << 6,
  META_FRAME_SHADED                   = 1 << 7,
  META_FRAME_STUCK                    = 1 << 8,
  META_FRAME_MAXIMIZED                = 1 << 9,
  META_FRAME_ALLOWS_SHADE             = 1 << 10,
  META_FRAME_ALLOWS_MOVE              = 1 << 11,
  META_FRAME_FULLSCREEN               = 1 << 12,
  META_FRAME_IS_FLASHING              = 1 << 13,
  META_FRAME_ABOVE                    = 1 << 14,
  META_FRAME_TILED_LEFT               = 1 << 15,
  META_FRAME_TILED_RIGHT              = 1 << 16
} MetaFrameFlags;

/**
 * MetaMenuOp:
 * @META_MENU_OP_NONE: No menu operation
 * @META_MENU_OP_DELETE: Menu operation delete
 * @META_MENU_OP_MINIMIZE: Menu operation minimize
 * @META_MENU_OP_UNMAXIMIZE: Menu operation unmaximize
 * @META_MENU_OP_MAXIMIZE: Menu operation maximize
 * @META_MENU_OP_UNSHADE: Menu operation unshade
 * @META_MENU_OP_SHADE: Menu operation shade
 * @META_MENU_OP_UNSTICK: Menu operation unstick
 * @META_MENU_OP_STICK: Menu operation stick
 * @META_MENU_OP_WORKSPACES: Menu operation workspaces
 * @META_MENU_OP_MOVE: Menu operation move
 * @META_MENU_OP_RESIZE: Menu operation resize
 * @META_MENU_OP_ABOVE: Menu operation above
 * @META_MENU_OP_UNABOVE: Menu operation unabove
 * @META_MENU_OP_MOVE_LEFT: Menu operation left
 * @META_MENU_OP_MOVE_RIGHT: Menu operation right
 * @META_MENU_OP_MOVE_UP: Menu operation up
 * @META_MENU_OP_MOVE_DOWN: Menu operation down
 * @META_MENU_OP_RECOVER: Menu operation recover
 */
typedef enum
{
  META_MENU_OP_NONE        = 0,
  META_MENU_OP_DELETE      = 1 << 0,
  META_MENU_OP_MINIMIZE    = 1 << 1,
  META_MENU_OP_UNMAXIMIZE  = 1 << 2,
  META_MENU_OP_MAXIMIZE    = 1 << 3,
  META_MENU_OP_UNSHADE     = 1 << 4,
  META_MENU_OP_SHADE       = 1 << 5,
  META_MENU_OP_UNSTICK     = 1 << 6,
  META_MENU_OP_STICK       = 1 << 7,
  META_MENU_OP_WORKSPACES  = 1 << 8,
  META_MENU_OP_MOVE        = 1 << 9,
  META_MENU_OP_RESIZE      = 1 << 10,
  META_MENU_OP_ABOVE       = 1 << 11,
  META_MENU_OP_UNABOVE     = 1 << 12,
  META_MENU_OP_MOVE_LEFT   = 1 << 13,
  META_MENU_OP_MOVE_RIGHT  = 1 << 14,
  META_MENU_OP_MOVE_UP     = 1 << 15,
  META_MENU_OP_MOVE_DOWN   = 1 << 16,
  META_MENU_OP_RECOVER     = 1 << 17
} MetaMenuOp;

typedef struct _MetaWindowMenu MetaWindowMenu;

typedef void (* MetaWindowMenuFunc) (MetaWindowMenu *menu,
                                     Display        *xdisplay,
                                     Window          client_xwindow,
                                     guint32         timestamp,
                                     MetaMenuOp      op,
                                     int             workspace,
                                     gpointer        user_data);

/**
 * MetaGrabOp:
 * @META_GRAB_OP_NONE: None
 * @META_GRAB_OP_MOVING: Moving with pointer
 * @META_GRAB_OP_RESIZING_SE: Resizing SE with pointer
 * @META_GRAB_OP_RESIZING_S: Resizing S with pointer
 * @META_GRAB_OP_RESIZING_SW: Resizing SW with pointer
 * @META_GRAB_OP_RESIZING_N: Resizing N with pointer
 * @META_GRAB_OP_RESIZING_NE: Resizing NE with pointer
 * @META_GRAB_OP_RESIZING_NW: Resizing NW with pointer
 * @META_GRAB_OP_RESIZING_W: Resizing W with pointer
 * @META_GRAB_OP_RESIZING_E: Resizing E with pointer
 * @META_GRAB_OP_KEYBOARD_MOVING: Moving with keyboard
 * @META_GRAB_OP_KEYBOARD_RESIZING_UNKNOWN: Resizing with keyboard
 * @META_GRAB_OP_KEYBOARD_RESIZING_S: Resizing S with keyboard
 * @META_GRAB_OP_KEYBOARD_RESIZING_N: Resizing N with keyboard
 * @META_GRAB_OP_KEYBOARD_RESIZING_W: Resizing W with keyboard
 * @META_GRAB_OP_KEYBOARD_RESIZING_E: Resizing E with keyboard
 * @META_GRAB_OP_KEYBOARD_RESIZING_SE: Resizing SE with keyboard
 * @META_GRAB_OP_KEYBOARD_RESIZING_NE: Resizing NE with keyboard
 * @META_GRAB_OP_KEYBOARD_RESIZING_SW: Resizing SW with keyboard
 * @META_GRAB_OP_KEYBOARD_RESIZING_NW: Resizing NS with keyboard
 * @META_GRAB_OP_KEYBOARD_TABBING_NORMAL: Tabbing
 * @META_GRAB_OP_KEYBOARD_TABBING_DOCK: Tabbing through docks
 * @META_GRAB_OP_KEYBOARD_ESCAPING_NORMAL: Escaping
 * @META_GRAB_OP_KEYBOARD_ESCAPING_DOCK: Escaping through docks
 * @META_GRAB_OP_KEYBOARD_ESCAPING_GROUP: Escaping through groups
 * @META_GRAB_OP_KEYBOARD_TABBING_GROUP: Tabbing through groups
 * @META_GRAB_OP_KEYBOARD_WORKSPACE_SWITCHING: Switch to another workspace
 * @META_GRAB_OP_CLICKING_MINIMIZE: Clicked minimize button
 * @META_GRAB_OP_CLICKING_MAXIMIZE: Clicked maximize button
 * @META_GRAB_OP_CLICKING_UNMAXIMIZE: Clicked unmaximize button
 * @META_GRAB_OP_CLICKING_DELETE: Clicked delete button
 * @META_GRAB_OP_CLICKING_MENU: Clicked on menu
 * @META_GRAB_OP_CLICKING_SHADE: Clicked shade button
 * @META_GRAB_OP_CLICKING_UNSHADE: Clicked unshade button
 * @META_GRAB_OP_CLICKING_ABOVE: Clicked above button
 * @META_GRAB_OP_CLICKING_UNABOVE: Clicked unabove button
 * @META_GRAB_OP_CLICKING_STICK: Clicked stick button
 * @META_GRAB_OP_CLICKING_UNSTICK: Clicked unstick button
 * @META_GRAB_OP_COMPOSITOR: Compositor asked for grab
 */

/* when changing this enum, there are various switch statements
 * you have to update
 */
typedef enum
{
  META_GRAB_OP_NONE,

  /* Mouse ops */
  META_GRAB_OP_MOVING,
  META_GRAB_OP_RESIZING_SE,
  META_GRAB_OP_RESIZING_S,
  META_GRAB_OP_RESIZING_SW,
  META_GRAB_OP_RESIZING_N,
  META_GRAB_OP_RESIZING_NE,
  META_GRAB_OP_RESIZING_NW,
  META_GRAB_OP_RESIZING_W,
  META_GRAB_OP_RESIZING_E,

  /* Keyboard ops */
  META_GRAB_OP_KEYBOARD_MOVING,
  META_GRAB_OP_KEYBOARD_RESIZING_UNKNOWN,
  META_GRAB_OP_KEYBOARD_RESIZING_S,
  META_GRAB_OP_KEYBOARD_RESIZING_N,
  META_GRAB_OP_KEYBOARD_RESIZING_W,
  META_GRAB_OP_KEYBOARD_RESIZING_E,
  META_GRAB_OP_KEYBOARD_RESIZING_SE,
  META_GRAB_OP_KEYBOARD_RESIZING_NE,
  META_GRAB_OP_KEYBOARD_RESIZING_SW,
  META_GRAB_OP_KEYBOARD_RESIZING_NW,

  /* Frame button ops */
  META_GRAB_OP_CLICKING_MINIMIZE,
  META_GRAB_OP_CLICKING_MAXIMIZE,
  META_GRAB_OP_CLICKING_UNMAXIMIZE,
  META_GRAB_OP_CLICKING_DELETE,
  META_GRAB_OP_CLICKING_MENU,
  META_GRAB_OP_CLICKING_SHADE,
  META_GRAB_OP_CLICKING_UNSHADE,
  META_GRAB_OP_CLICKING_ABOVE,
  META_GRAB_OP_CLICKING_UNABOVE,
  META_GRAB_OP_CLICKING_STICK,
  META_GRAB_OP_CLICKING_UNSTICK,

  /* Special grab op when the compositor asked for a grab */
  META_GRAB_OP_COMPOSITOR
} MetaGrabOp;

/**
 * MetaCursor:
 * @META_CURSOR_DEFAULT: Default cursor
 * @META_CURSOR_NORTH_RESIZE: Resize northern edge cursor
 * @META_CURSOR_SOUTH_RESIZE: Resize southern edge cursor
 * @META_CURSOR_WEST_RESIZE: Resize western edge cursor
 * @META_CURSOR_EAST_RESIZE: Resize eastern edge cursor
 * @META_CURSOR_SE_RESIZE: Resize south-eastern corner cursor
 * @META_CURSOR_SW_RESIZE: Resize south-western corner cursor
 * @META_CURSOR_NE_RESIZE: Resize north-eastern corner cursor
 * @META_CURSOR_NW_RESIZE: Resize north-western corner cursor
 * @META_CURSOR_MOVE_OR_RESIZE_WINDOW: Move or resize cursor
 * @META_CURSOR_BUSY: Busy cursor
 * @META_CURSOR_DND_IN_DRAG: DND in drag cursor
 * @META_CURSOR_DND_MOVE: DND move cursor
 * @META_CURSOR_DND_COPY: DND copy cursor
 * @META_CURSOR_DND_UNSUPPORTED_TARGET: DND unsupported target
 * @META_CURSOR_POINTING_HAND: pointing hand
 * @META_CURSOR_CROSSHAIR: crosshair (action forbidden)
 * @META_CURSOR_IBEAM: I-beam (text input)
 */
typedef enum
{
  META_CURSOR_DEFAULT,
  META_CURSOR_NORTH_RESIZE,
  META_CURSOR_SOUTH_RESIZE,
  META_CURSOR_WEST_RESIZE,
  META_CURSOR_EAST_RESIZE,
  META_CURSOR_SE_RESIZE,
  META_CURSOR_SW_RESIZE,
  META_CURSOR_NE_RESIZE,
  META_CURSOR_NW_RESIZE,
  META_CURSOR_MOVE_OR_RESIZE_WINDOW,
  META_CURSOR_BUSY,
  META_CURSOR_DND_IN_DRAG,
  META_CURSOR_DND_MOVE,
  META_CURSOR_DND_COPY,
  META_CURSOR_DND_UNSUPPORTED_TARGET,
  META_CURSOR_POINTING_HAND,
  META_CURSOR_CROSSHAIR,
  META_CURSOR_IBEAM,
  META_CURSOR_LAST
} MetaCursor;

/**
 * MetaFrameType:
 * @META_FRAME_TYPE_NORMAL: Normal frame
 * @META_FRAME_TYPE_DIALOG: Dialog frame
 * @META_FRAME_TYPE_MODAL_DIALOG: Modal dialog frame
 * @META_FRAME_TYPE_UTILITY: Utility frame
 * @META_FRAME_TYPE_MENU: Menu frame
 * @META_FRAME_TYPE_BORDER: Border frame
 * @META_FRAME_TYPE_ATTACHED: Attached frame
 * @META_FRAME_TYPE_LAST: Marks the end of the #MetaFrameType enumeration
 */
typedef enum
{
  META_FRAME_TYPE_NORMAL,
  META_FRAME_TYPE_DIALOG,
  META_FRAME_TYPE_MODAL_DIALOG,
  META_FRAME_TYPE_UTILITY,
  META_FRAME_TYPE_MENU,
  META_FRAME_TYPE_BORDER,
  META_FRAME_TYPE_ATTACHED,
  META_FRAME_TYPE_LAST
} MetaFrameType;

/**
 * MetaVirtualModifier:
 * @META_VIRTUAL_SHIFT_MASK: Shift mask
 * @META_VIRTUAL_CONTROL_MASK: Control mask
 * @META_VIRTUAL_ALT_MASK: Alt mask
 * @META_VIRTUAL_META_MASK: Meta mask
 * @META_VIRTUAL_SUPER_MASK: Super mask
 * @META_VIRTUAL_HYPER_MASK: Hyper mask
 * @META_VIRTUAL_MOD2_MASK: Mod2 mask
 * @META_VIRTUAL_MOD3_MASK: Mod3 mask
 * @META_VIRTUAL_MOD4_MASK: Mod4 mask
 * @META_VIRTUAL_MOD5_MASK: Mod5 mask
 */
typedef enum
{
  /* Create gratuitous divergence from regular
   * X mod bits, to be sure we find bugs
   */
  META_VIRTUAL_SHIFT_MASK    = 1 << 5,
  META_VIRTUAL_CONTROL_MASK  = 1 << 6,
  META_VIRTUAL_ALT_MASK      = 1 << 7,  
  META_VIRTUAL_META_MASK     = 1 << 8,
  META_VIRTUAL_SUPER_MASK    = 1 << 9,
  META_VIRTUAL_HYPER_MASK    = 1 << 10,
  META_VIRTUAL_MOD2_MASK     = 1 << 11,
  META_VIRTUAL_MOD3_MASK     = 1 << 12,
  META_VIRTUAL_MOD4_MASK     = 1 << 13,
  META_VIRTUAL_MOD5_MASK     = 1 << 14
} MetaVirtualModifier;

/**
 * MetaDirection:
 * @META_DIRECTION_LEFT: Left
 * @META_DIRECTION_RIGHT: Right
 * @META_DIRECTION_TOP: Top
 * @META_DIRECTION_BOTTOM: Bottom
 * @META_DIRECTION_UP: Up
 * @META_DIRECTION_DOWN: Down
 * @META_DIRECTION_HORIZONTAL: Horizontal
 * @META_DIRECTION_VERTICAL: Vertical
 */

/* Relative directions or sides seem to come up all over the place... */
/* FIXME: Replace
 *   screen.[ch]:MetaScreenDirection,
 *   workspace.[ch]:MetaMotionDirection,
 * with the use of MetaDirection.
 */
typedef enum
{
  META_DIRECTION_LEFT       = 1 << 0,
  META_DIRECTION_RIGHT      = 1 << 1,
  META_DIRECTION_TOP        = 1 << 2,
  META_DIRECTION_BOTTOM     = 1 << 3,

  /* Some aliases for making code more readable for various circumstances. */
  META_DIRECTION_UP         = META_DIRECTION_TOP,
  META_DIRECTION_DOWN       = META_DIRECTION_BOTTOM,

  /* A few more definitions using aliases */
  META_DIRECTION_HORIZONTAL = META_DIRECTION_LEFT | META_DIRECTION_RIGHT,
  META_DIRECTION_VERTICAL   = META_DIRECTION_UP   | META_DIRECTION_DOWN,
} MetaDirection;

/**
 * MetaMotionDirection:
 * @META_MOTION_UP: Upwards motion
 * @META_MOTION_DOWN: Downwards motion
 * @META_MOTION_LEFT: Motion to the left
 * @META_MOTION_RIGHT: Motion to the right
 * @META_MOTION_UP_LEFT: Motion up and to the left
 * @META_MOTION_UP_RIGHT: Motion up and to the right
 * @META_MOTION_DOWN_LEFT: Motion down and to the left
 * @META_MOTION_DOWN_RIGHT: Motion down and to the right
 */

/* Negative to avoid conflicting with real workspace
 * numbers
 */
typedef enum
{
  META_MOTION_UP = -1,
  META_MOTION_DOWN = -2,
  META_MOTION_LEFT = -3,
  META_MOTION_RIGHT = -4,
  /* These are only used for effects */
  META_MOTION_UP_LEFT = -5,
  META_MOTION_UP_RIGHT = -6,
  META_MOTION_DOWN_LEFT = -7,
  META_MOTION_DOWN_RIGHT = -8
} MetaMotionDirection;

/**
 * MetaSide:
 * @META_SIDE_LEFT: Left side
 * @META_SIDE_RIGHT: Right side
 * @META_SIDE_TOP: Top side
 * @META_SIDE_BOTTOM: Bottom side
 */

/* Sometimes we want to talk about sides instead of directions; note
 * that the values must be as follows or meta_window_update_struts()
 * won't work. Using these values also is a safety blanket since
 * MetaDirection used to be used as a side.
 */
typedef enum
{
  META_SIDE_LEFT            = META_DIRECTION_LEFT,
  META_SIDE_RIGHT           = META_DIRECTION_RIGHT,
  META_SIDE_TOP             = META_DIRECTION_TOP,
  META_SIDE_BOTTOM          = META_DIRECTION_BOTTOM
} MetaSide;

/**
 * MetaButtonFunction:
 * @META_BUTTON_FUNCTION_MENU: Menu
 * @META_BUTTON_FUNCTION_MINIMIZE: Minimize
 * @META_BUTTON_FUNCTION_MAXIMIZE: Maximize
 * @META_BUTTON_FUNCTION_CLOSE: Close
 * @META_BUTTON_FUNCTION_SHADE: Shade
 * @META_BUTTON_FUNCTION_ABOVE: Above
 * @META_BUTTON_FUNCTION_STICK: Stick
 * @META_BUTTON_FUNCTION_UNSHADE: Unshade
 * @META_BUTTON_FUNCTION_UNABOVE: Unabove
 * @META_BUTTON_FUNCTION_UNSTICK: Unstick
 * @META_BUTTON_FUNCTION_LAST: Marks the end of the #MetaButtonFunction enumeration
 *
 * Function a window button can have.  Note, you can't add stuff here
 * without extending the theme format to draw a new function and
 * breaking all existing themes.
 */
typedef enum
{
  META_BUTTON_FUNCTION_MENU,
  META_BUTTON_FUNCTION_MINIMIZE,
  META_BUTTON_FUNCTION_MAXIMIZE,
  META_BUTTON_FUNCTION_CLOSE,
  META_BUTTON_FUNCTION_SHADE,
  META_BUTTON_FUNCTION_ABOVE,
  META_BUTTON_FUNCTION_STICK,
  META_BUTTON_FUNCTION_UNSHADE,
  META_BUTTON_FUNCTION_UNABOVE,
  META_BUTTON_FUNCTION_UNSTICK,
  META_BUTTON_FUNCTION_LAST
} MetaButtonFunction;

#define MAX_BUTTONS_PER_CORNER META_BUTTON_FUNCTION_LAST

/* Keep array size in sync with MAX_BUTTONS_PER_CORNER */
/**
 * MetaButtonLayout:
 * @left_buttons: (array fixed-size=10):
 * @right_buttons: (array fixed-size=10):
 * @left_buttons_has_spacer: (array fixed-size=10):
 * @right_buttons_has_spacer: (array fixed-size=10):
 */
typedef struct _MetaButtonLayout MetaButtonLayout;
struct _MetaButtonLayout
{
  /* buttons in the group on the left side */
  MetaButtonFunction left_buttons[MAX_BUTTONS_PER_CORNER];
  gboolean left_buttons_has_spacer[MAX_BUTTONS_PER_CORNER];

  /* buttons in the group on the right side */
  MetaButtonFunction right_buttons[MAX_BUTTONS_PER_CORNER];
  gboolean right_buttons_has_spacer[MAX_BUTTONS_PER_CORNER];
};

/**
 * MetaFrameBorders:
 * @visible: inner visible portion of frame border
 * @invisible: outer invisible portion of frame border
 * @total: sum of the two borders above
 */
typedef struct _MetaFrameBorders MetaFrameBorders;
struct _MetaFrameBorders
{
  /* The frame border is made up of two pieces - an inner visible portion
   * and an outer portion that is invisible but responds to events.
   */
  GtkBorder visible;
  GtkBorder invisible;

  /* For convenience, we have a "total" border which is equal to the sum
   * of the two borders above. */
  GtkBorder total;
};

/* sets all dimensions to zero */
void meta_frame_borders_clear (MetaFrameBorders *self);

/* should investigate changing these to whatever most apps use */
#define META_ICON_WIDTH 96
#define META_ICON_HEIGHT 96
#define META_MINI_ICON_WIDTH 16
#define META_MINI_ICON_HEIGHT 16

#define META_DEFAULT_ICON_NAME "window"

/* Main loop priorities determine when activity in the GLib
 * will take precendence over the others. Priorities are sometimes
 * used to enforce ordering: give A a higher priority than B if
 * A must occur before B. But that poses a problem since then
 * if A occurs frequently enough, B will never occur.
 *
 * Anything we want to occur more or less immediately should
 * have a priority of G_PRIORITY_DEFAULT. When we want to
 * coelesce multiple things together, the appropriate place to
 * do it is usually META_PRIORITY_BEFORE_REDRAW.
 *
 * Note that its usually better to use meta_later_add() rather
 * than calling g_idle_add() directly; this will make sure things
 * get run when added from a clutter event handler without
 * waiting for another repaint cycle.
 *
 * If something has a priority lower than the redraw priority
 * (such as a default priority idle), then it may be arbitrarily
 * delayed. This happens if the screen is updating rapidly: we
 * are spending all our time either redrawing or waiting for a
 * vblank-synced buffer swap. (When X is improved to allow
 * clutter to do the buffer-swap asychronously, this will get
 * better.)
 */

/* G_PRIORITY_DEFAULT:
 *  events
 *  many timeouts
 */

/* GTK_PRIORITY_RESIZE:         (G_PRIORITY_HIGH_IDLE + 10) */
#define META_PRIORITY_RESIZE    (G_PRIORITY_HIGH_IDLE + 15)
/* GTK_PRIORITY_REDRAW:         (G_PRIORITY_HIGH_IDLE + 20) */

#define META_PRIORITY_BEFORE_REDRAW  (G_PRIORITY_HIGH_IDLE + 40)
/*  calc-showing idle
 *  update-icon idle
 */

/* CLUTTER_PRIORITY_REDRAW:     (G_PRIORITY_HIGH_IDLE + 50) */
#define META_PRIORITY_REDRAW    (G_PRIORITY_HIGH_IDLE + 50)

/* ==== Anything below here can be starved arbitrarily ==== */

/* G_PRIORITY_DEFAULT_IDLE:
 *  Mutter plugin unloading
 */

#define META_PRIORITY_PREFS_NOTIFY   (G_PRIORITY_DEFAULT_IDLE + 10)

/************************************************************/

#define POINT_IN_RECT(xcoord, ycoord, rect) \
 ((xcoord) >= (rect).x &&                   \
  (xcoord) <  ((rect).x + (rect).width) &&  \
  (ycoord) >= (rect).y &&                   \
  (ycoord) <  ((rect).y + (rect).height))

/**
 * MetaStackLayer:
 * @META_LAYER_DESKTOP: Desktop layer
 * @META_LAYER_BOTTOM: Bottom layer
 * @META_LAYER_NORMAL: Normal layer
 * @META_LAYER_TOP: Top layer
 * @META_LAYER_DOCK: Dock layer
 * @META_LAYER_FULLSCREEN: Fullscreen layer
 * @META_LAYER_FOCUSED_WINDOW: Focused window layer
 * @META_LAYER_OVERRIDE_REDIRECT: Override-redirect layer
 * @META_LAYER_LAST: Marks the end of the #MetaStackLayer enumeration
 *
 * Layers a window can be in.
 * These MUST be in the order of stacking.
 */
typedef enum
{
  META_LAYER_DESKTOP	       = 0,
  META_LAYER_BOTTOM	       = 1,
  META_LAYER_NORMAL	       = 2,
  META_LAYER_TOP	       = 4, /* Same as DOCK; see EWMH and bug 330717 */
  META_LAYER_DOCK	       = 4,
  META_LAYER_FULLSCREEN	       = 5,
  META_LAYER_FOCUSED_WINDOW    = 6,
  META_LAYER_OVERRIDE_REDIRECT = 7,
  META_LAYER_LAST	       = 8
} MetaStackLayer;

#endif
