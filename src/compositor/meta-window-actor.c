/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */

/**
 * SECTION:meta-window-actor
 * @title: MetaWindowActor
 * @short_description: An actor representing a top-level window in the scene graph
 */

#include <config.h>

#include <math.h>

#include <X11/extensions/shape.h>
#include <X11/extensions/Xcomposite.h>
#include <X11/extensions/Xdamage.h>
#include <X11/extensions/Xrender.h>

#include <clutter/x11/clutter-x11.h>
#include <cogl/cogl-texture-pixmap-x11.h>
#include <gdk/gdk.h> /* for gdk_rectangle_union() */
#include <string.h>

#include <meta/display.h>
#include <meta/errors.h>
#include "frame.h"
#include <meta/window.h>
#include <meta/meta-shaped-texture.h>
#include "xprops.h"

#include "compositor-private.h"
#include "meta-shadow-factory-private.h"
#include "meta-window-actor-private.h"
#include "meta-texture-rectangle.h"
#include "region-utils.h"

enum {
  POSITION_CHANGED,
  SIZE_CHANGED,
  LAST_SIGNAL
};

static guint signals[LAST_SIGNAL] = {0};


struct _MetaWindowActorPrivate
{
  MetaWindow       *window;
  Window            xwindow;
  MetaScreen       *screen;

  ClutterActor     *actor;

  /* MetaShadowFactory only caches shadows that are actually in use;
   * to avoid unnecessary recomputation we do two things: 1) we store
   * both a focused and unfocused shadow for the window. If the window
   * doesn't have different focused and unfocused shadow parameters,
   * these will be the same. 2) when the shadow potentially changes we
   * don't immediately unreference the old shadow, we just flag it as
   * dirty and recompute it when we next need it (recompute_focused_shadow,
   * recompute_unfocused_shadow.) Because of our extraction of
   * size-invariant window shape, we'll often find that the new shadow
   * is the same as the old shadow.
   */
  MetaShadow       *focused_shadow;
  MetaShadow       *unfocused_shadow;

  Pixmap            back_pixmap;

  Damage            damage;

  guint8            opacity;
  guint8            shadow_opacity;

  gchar *           desc;

  /* A region that matches the shape of the window, including frame bounds */
  cairo_region_t   *shape_region;
  /* The opaque region, from _NET_WM_OPAQUE_REGION, intersected with
   * the shape region. */
  cairo_region_t   *opaque_region;
  /* The region we should clip to when painting the shadow */
  cairo_region_t   *shadow_clip;

  /* Extracted size-invariant shape used for shadows */
  MetaWindowShape  *shadow_shape;

  gint              last_width;
  gint              last_height;

  gint              freeze_count;

  char *            shadow_class;

  /*
   * These need to be counters rather than flags, since more plugins
   * can implement same effect; the practicality of stacking effects
   * might be dubious, but we have to at least handle it correctly.
   */
  gint              minimize_in_progress;
  gint              maximize_in_progress;
  gint              unmaximize_in_progress;
  gint              map_in_progress;
  gint              destroy_in_progress;

  /* List of FrameData for recent frames */
  GList            *frames;

  guint		    visible                : 1;
  guint		    mapped                 : 1;
  guint		    argb32                 : 1;
  guint		    disposed               : 1;
  guint             redecorating           : 1;

  guint		    needs_damage_all       : 1;
  guint		    received_damage        : 1;
  guint             repaint_scheduled      : 1;

  /* If set, the client needs to be sent a _NET_WM_FRAME_DRAWN
   * client message using the most recent frame in ->frames */
  guint             needs_frame_drawn      : 1;

  guint		    needs_pixmap           : 1;
  guint             needs_reshape          : 1;
  guint             recompute_focused_shadow   : 1;
  guint             recompute_unfocused_shadow : 1;
  guint		    size_changed           : 1;
  guint             updates_frozen         : 1;

  guint		    needs_destroy	   : 1;

  guint             no_shadow              : 1;

  guint             no_more_x_calls        : 1;

  guint             unredirected           : 1;

  /* This is used to detect fullscreen windows that need to be unredirected */
  guint             full_damage_frames_count;
  guint             does_full_damage  : 1;
};

typedef struct _FrameData FrameData;

struct _FrameData
{
  int64_t frame_counter;
  guint64 sync_request_serial;
  gint64 frame_drawn_time;
};

enum
{
  PROP_META_WINDOW = 1,
  PROP_META_SCREEN,
  PROP_X_WINDOW,
  PROP_NO_SHADOW,
  PROP_SHADOW_CLASS
};

static void meta_window_actor_dispose    (GObject *object);
static void meta_window_actor_finalize   (GObject *object);
static void meta_window_actor_constructed (GObject *object);
static void meta_window_actor_set_property (GObject       *object,
                                            guint         prop_id,
                                            const GValue *value,
                                            GParamSpec   *pspec);
static void meta_window_actor_get_property (GObject      *object,
                                            guint         prop_id,
                                            GValue       *value,
                                            GParamSpec   *pspec);

static void meta_window_actor_paint (ClutterActor *actor);

static gboolean meta_window_actor_get_paint_volume (ClutterActor       *actor,
                                                    ClutterPaintVolume *volume);


static void     meta_window_actor_detach     (MetaWindowActor *self);
static gboolean meta_window_actor_has_shadow (MetaWindowActor *self);

static void meta_window_actor_handle_updates (MetaWindowActor *self);

static void check_needs_reshape (MetaWindowActor *self);

G_DEFINE_TYPE (MetaWindowActor, meta_window_actor, CLUTTER_TYPE_ACTOR);

static void
frame_data_free (FrameData *frame)
{
  g_slice_free (FrameData, frame);
}

static void
meta_window_actor_class_init (MetaWindowActorClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  ClutterActorClass *actor_class = CLUTTER_ACTOR_CLASS (klass);
  GParamSpec   *pspec;

  g_type_class_add_private (klass, sizeof (MetaWindowActorPrivate));

  object_class->dispose      = meta_window_actor_dispose;
  object_class->finalize     = meta_window_actor_finalize;
  object_class->set_property = meta_window_actor_set_property;
  object_class->get_property = meta_window_actor_get_property;
  object_class->constructed  = meta_window_actor_constructed;

  actor_class->paint = meta_window_actor_paint;
  actor_class->get_paint_volume = meta_window_actor_get_paint_volume;

  pspec = g_param_spec_object ("meta-window",
                               "MetaWindow",
                               "The displayed MetaWindow",
                               META_TYPE_WINDOW,
                               G_PARAM_READWRITE | G_PARAM_CONSTRUCT);

  g_object_class_install_property (object_class,
                                   PROP_META_WINDOW,
                                   pspec);

  pspec = g_param_spec_pointer ("meta-screen",
				"MetaScreen",
				"MetaScreen",
				G_PARAM_READWRITE | G_PARAM_CONSTRUCT);

  g_object_class_install_property (object_class,
                                   PROP_META_SCREEN,
                                   pspec);

  pspec = g_param_spec_ulong ("x-window",
			      "Window",
			      "Window",
			      0,
			      G_MAXULONG,
			      0,
			      G_PARAM_READWRITE | G_PARAM_CONSTRUCT);

  g_object_class_install_property (object_class,
                                   PROP_X_WINDOW,
                                   pspec);

  pspec = g_param_spec_boolean ("no-shadow",
                                "No shadow",
                                "Do not add shaddow to this window",
                                FALSE,
                                G_PARAM_READWRITE);

  g_object_class_install_property (object_class,
                                   PROP_NO_SHADOW,
                                   pspec);

  pspec = g_param_spec_string ("shadow-class",
                               "Name of the shadow class for this window.",
                               "NULL means to use the default shadow class for this window type",
                               NULL,
                               G_PARAM_READWRITE);

  g_object_class_install_property (object_class,
                                   PROP_SHADOW_CLASS,
                                   pspec);

  signals[POSITION_CHANGED] =
    g_signal_new ("position-changed",
                  G_TYPE_FROM_CLASS (klass),
                  G_SIGNAL_RUN_LAST,
                  0, NULL, NULL, NULL,
                  G_TYPE_NONE, 0);
  signals[SIZE_CHANGED] =
    g_signal_new ("size-changed",
                  G_TYPE_FROM_CLASS (klass),
                  G_SIGNAL_RUN_LAST,
                  0, NULL, NULL, NULL,
                  G_TYPE_NONE, 0);
}

static void
meta_window_actor_init (MetaWindowActor *self)
{
  MetaWindowActorPrivate *priv;

  priv = self->priv = G_TYPE_INSTANCE_GET_PRIVATE (self,
						   META_TYPE_WINDOW_ACTOR,
						   MetaWindowActorPrivate);
  priv->opacity = 0xff;
  priv->shadow_class = NULL;
}

static void
window_decorated_notify (MetaWindow *mw,
                         GParamSpec *arg1,
                         gpointer    data)
{
  MetaWindowActor        *self     = META_WINDOW_ACTOR (data);
  MetaWindowActorPrivate *priv     = self->priv;
  MetaFrame              *frame    = meta_window_get_frame (mw);
  MetaScreen             *screen   = priv->screen;
  MetaDisplay            *display  = meta_screen_get_display (screen);
  Display                *xdisplay = meta_display_get_xdisplay (display);
  Window                  new_xwindow;

  /*
   * Basically, we have to reconstruct the the internals of this object
   * from scratch, as everything has changed.
   */
  priv->redecorating = TRUE;

  if (frame)
    new_xwindow = meta_frame_get_xwindow (frame);
  else
    new_xwindow = meta_window_get_xwindow (mw);

  meta_window_actor_detach (self);

  /*
   * First of all, clean up any resources we are currently using and will
   * be replacing.
   */
  if (priv->damage != None)
    {
      meta_error_trap_push (display);
      XDamageDestroy (xdisplay, priv->damage);
      meta_error_trap_pop (display);
      priv->damage = None;
    }

  g_free (priv->desc);
  priv->desc = NULL;

  priv->xwindow = new_xwindow;

  /*
   * Recreate the contents.
   */
  meta_window_actor_constructed (G_OBJECT (self));
}

static void
window_appears_focused_notify (MetaWindow *mw,
                               GParamSpec *arg1,
                               gpointer    data)
{
  clutter_actor_queue_redraw (CLUTTER_ACTOR (data));
}

static void
meta_window_actor_constructed (GObject *object)
{
  MetaWindowActor        *self     = META_WINDOW_ACTOR (object);
  MetaWindowActorPrivate *priv     = self->priv;
  MetaScreen             *screen   = priv->screen;
  MetaDisplay            *display  = meta_screen_get_display (screen);
  Window                  xwindow  = priv->xwindow;
  MetaWindow             *window   = priv->window;
  Display                *xdisplay = meta_display_get_xdisplay (display);
  XRenderPictFormat      *format;

  priv->damage = XDamageCreate (xdisplay, xwindow,
                                XDamageReportBoundingBox);

  format = XRenderFindVisualFormat (xdisplay, window->xvisual);

  if (format && format->type == PictTypeDirect && format->direct.alphaMask)
    priv->argb32 = TRUE;

  if (!priv->actor)
    {
      priv->actor = meta_shaped_texture_new ();

      clutter_actor_add_child (CLUTTER_ACTOR (self), priv->actor);

      /*
       * Since we are holding a pointer to this actor independently of the
       * ClutterContainer internals, and provide a public API to access it,
       * add a reference here, so that if someone is messing about with us
       * via the container interface, we do not end up with a dangling pointer.
       * We will release it in dispose().
       */
      g_object_ref (priv->actor);

      g_signal_connect (window, "notify::decorated",
                        G_CALLBACK (window_decorated_notify), self);
      g_signal_connect (window, "notify::appears-focused",
                        G_CALLBACK (window_appears_focused_notify), self);
    }
  else
    {
      /*
       * This is the case where existing window is gaining/loosing frame.
       * Just ensure the actor is top most (i.e., above shadow).
       */
      clutter_actor_set_child_above_sibling (CLUTTER_ACTOR (self), priv->actor, NULL);
    }

  meta_window_actor_update_opacity (self);

  /* Start off with an empty region to maintain the invariant that
     the shape region is always set */
  priv->shape_region = cairo_region_create();
}

static void
meta_window_actor_dispose (GObject *object)
{
  MetaWindowActor        *self = META_WINDOW_ACTOR (object);
  MetaWindowActorPrivate *priv = self->priv;
  MetaScreen             *screen;
  MetaDisplay            *display;
  Display                *xdisplay;
  MetaCompScreen         *info;

  if (priv->disposed)
    return;

  priv->disposed = TRUE;

  screen   = priv->screen;
  display  = meta_screen_get_display (screen);
  xdisplay = meta_display_get_xdisplay (display);
  info     = meta_screen_get_compositor_data (screen);

  meta_window_actor_detach (self);

  g_clear_pointer (&priv->shape_region, cairo_region_destroy);
  g_clear_pointer (&priv->opaque_region, cairo_region_destroy);
  g_clear_pointer (&priv->shadow_clip, cairo_region_destroy);

  g_clear_pointer (&priv->shadow_class, g_free);
  g_clear_pointer (&priv->focused_shadow, meta_shadow_unref);
  g_clear_pointer (&priv->unfocused_shadow, meta_shadow_unref);
  g_clear_pointer (&priv->shadow_shape, meta_window_shape_unref);

  if (priv->damage != None)
    {
      meta_error_trap_push (display);
      XDamageDestroy (xdisplay, priv->damage);
      meta_error_trap_pop (display);

      priv->damage = None;
    }

  info->windows = g_list_remove (info->windows, (gconstpointer) self);

  g_clear_object (&priv->window);

  /*
   * Release the extra reference we took on the actor.
   */
  g_clear_object (&priv->actor);

  G_OBJECT_CLASS (meta_window_actor_parent_class)->dispose (object);
}

static void
meta_window_actor_finalize (GObject *object)
{
  MetaWindowActor        *self = META_WINDOW_ACTOR (object);
  MetaWindowActorPrivate *priv = self->priv;

  g_list_free_full (priv->frames, (GDestroyNotify) frame_data_free);
  g_free (priv->desc);

  G_OBJECT_CLASS (meta_window_actor_parent_class)->finalize (object);
}

static void
meta_window_actor_set_property (GObject      *object,
                                guint         prop_id,
                                const GValue *value,
                                GParamSpec   *pspec)
{
  MetaWindowActor        *self   = META_WINDOW_ACTOR (object);
  MetaWindowActorPrivate *priv = self->priv;

  switch (prop_id)
    {
    case PROP_META_WINDOW:
      {
        if (priv->window)
          g_object_unref (priv->window);
        priv->window = g_value_dup_object (value);
      }
      break;
    case PROP_META_SCREEN:
      priv->screen = g_value_get_pointer (value);
      break;
    case PROP_X_WINDOW:
      priv->xwindow = g_value_get_ulong (value);
      break;
    case PROP_NO_SHADOW:
      {
        gboolean newv = g_value_get_boolean (value);

        if (newv == priv->no_shadow)
          return;

        priv->no_shadow = newv;

        meta_window_actor_invalidate_shadow (self);
      }
      break;
    case PROP_SHADOW_CLASS:
      {
        const char *newv = g_value_get_string (value);

        if (g_strcmp0 (newv, priv->shadow_class) == 0)
          return;

        g_free (priv->shadow_class);
        priv->shadow_class = g_strdup (newv);

        meta_window_actor_invalidate_shadow (self);
      }
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}

static void
meta_window_actor_get_property (GObject      *object,
                                guint         prop_id,
                                GValue       *value,
                                GParamSpec   *pspec)
{
  MetaWindowActorPrivate *priv = META_WINDOW_ACTOR (object)->priv;

  switch (prop_id)
    {
    case PROP_META_WINDOW:
      g_value_set_object (value, priv->window);
      break;
    case PROP_META_SCREEN:
      g_value_set_pointer (value, priv->screen);
      break;
    case PROP_X_WINDOW:
      g_value_set_ulong (value, priv->xwindow);
      break;
    case PROP_NO_SHADOW:
      g_value_set_boolean (value, priv->no_shadow);
      break;
    case PROP_SHADOW_CLASS:
      g_value_set_string (value, priv->shadow_class);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}

static const char *
meta_window_actor_get_shadow_class (MetaWindowActor *self)
{
  MetaWindowActorPrivate *priv = self->priv;

  if (priv->shadow_class != NULL)
    return priv->shadow_class;
  else
    {
      MetaWindowType window_type = meta_window_get_window_type (priv->window);

      switch (window_type)
        {
        case META_WINDOW_DROPDOWN_MENU:
          return "dropdown-menu";
        case META_WINDOW_POPUP_MENU:
          return "popup-menu";
        default:
          {
            MetaFrameType frame_type = meta_window_get_frame_type (priv->window);
            return meta_frame_type_to_string (frame_type);
          }
        }
    }
}

static void
meta_window_actor_get_shadow_params (MetaWindowActor  *self,
                                     gboolean          appears_focused,
                                     MetaShadowParams *params)
{
  const char *shadow_class = meta_window_actor_get_shadow_class (self);

  meta_shadow_factory_get_params (meta_shadow_factory_get_default (),
                                  shadow_class, appears_focused,
                                  params);
}

void
meta_window_actor_get_shape_bounds (MetaWindowActor       *self,
                                    cairo_rectangle_int_t *bounds)
{
  MetaWindowActorPrivate *priv = self->priv;

  cairo_region_get_extents (priv->shape_region, bounds);
}

static void
meta_window_actor_get_shadow_bounds (MetaWindowActor       *self,
                                     gboolean               appears_focused,
                                     cairo_rectangle_int_t *bounds)
{
  MetaWindowActorPrivate *priv = self->priv;
  MetaShadow *shadow = appears_focused ? priv->focused_shadow : priv->unfocused_shadow;
  cairo_rectangle_int_t shape_bounds;
  MetaShadowParams params;

  meta_window_actor_get_shape_bounds (self, &shape_bounds);
  meta_window_actor_get_shadow_params (self, appears_focused, &params);

  meta_shadow_get_bounds (shadow,
                          params.x_offset + shape_bounds.x,
                          params.y_offset + shape_bounds.y,
                          shape_bounds.width,
                          shape_bounds.height,
                          bounds);
}

/* If we have an ARGB32 window that we decorate with a frame, it's
 * probably something like a translucent terminal - something where
 * the alpha channel represents transparency rather than a shape.  We
 * don't want to show the shadow through the translucent areas since
 * the shadow is wrong for translucent windows (it should be
 * translucent itself and colored), and not only that, will /look/
 * horribly wrong - a misplaced big black blob. As a hack, what we
 * want to do is just draw the shadow as normal outside the frame, and
 * inside the frame draw no shadow.  This is also not even close to
 * the right result, but looks OK. We also apply this approach to
 * windows set to be partially translucent with _NET_WM_WINDOW_OPACITY.
 */
static gboolean
clip_shadow_under_window (MetaWindowActor *self)
{
  MetaWindowActorPrivate *priv = self->priv;

  return (priv->argb32 || priv->opacity != 0xff) && priv->window->frame;
}

static void
meta_window_actor_paint (ClutterActor *actor)
{
  MetaWindowActor *self = META_WINDOW_ACTOR (actor);
  MetaWindowActorPrivate *priv = self->priv;
  gboolean appears_focused = meta_window_appears_focused (priv->window);
  MetaShadow *shadow = appears_focused ? priv->focused_shadow : priv->unfocused_shadow;

  if (shadow != NULL)
    {
      MetaShadowParams params;
      cairo_rectangle_int_t shape_bounds;
      cairo_region_t *clip = priv->shadow_clip;

      meta_window_actor_get_shape_bounds (self, &shape_bounds);
      meta_window_actor_get_shadow_params (self, appears_focused, &params);

      /* The frame bounds are already subtracted from priv->shadow_clip
       * if that exists.
       */
      if (!clip && clip_shadow_under_window (self))
        {
          cairo_region_t *frame_bounds = meta_window_get_frame_bounds (priv->window);
          cairo_rectangle_int_t bounds;

          meta_window_actor_get_shadow_bounds (self, appears_focused, &bounds);
          clip = cairo_region_create_rectangle (&bounds);

          cairo_region_subtract (clip, frame_bounds);
        }

      meta_shadow_paint (shadow,
                         params.x_offset + shape_bounds.x,
                         params.y_offset + shape_bounds.y,
                         shape_bounds.width,
                         shape_bounds.height,
                         (clutter_actor_get_paint_opacity (actor) * params.opacity * priv->opacity) / (255 * 255),
                         clip,
                         clip_shadow_under_window (self)); /* clip_strictly - not just as an optimization */

      if (clip && clip != priv->shadow_clip)
        cairo_region_destroy (clip);
    }

  CLUTTER_ACTOR_CLASS (meta_window_actor_parent_class)->paint (actor);
}

static gboolean
meta_window_actor_get_paint_volume (ClutterActor       *actor,
                                    ClutterPaintVolume *volume)
{
  MetaWindowActor *self = META_WINDOW_ACTOR (actor);
  MetaWindowActorPrivate *priv = self->priv;
  cairo_rectangle_int_t bounds;
  gboolean appears_focused = meta_window_appears_focused (priv->window);
  ClutterVertex origin;

  /* The paint volume is computed before paint functions are called
   * so our bounds might not be updated yet. Force an update. */
  meta_window_actor_handle_updates (self);

  meta_window_actor_get_shape_bounds (self, &bounds);

  if (appears_focused ? priv->focused_shadow : priv->unfocused_shadow)
    {
      cairo_rectangle_int_t shadow_bounds;

      /* We could compute an full clip region as we do for the window
       * texture, but the shadow is relatively cheap to draw, and
       * a little more complex to clip, so we just catch the case where
       * the shadow is completely obscured and doesn't need to be drawn
       * at all.
       */

      meta_window_actor_get_shadow_bounds (self, appears_focused, &shadow_bounds);
      gdk_rectangle_union (&bounds, &shadow_bounds, &bounds);
    }

  origin.x = bounds.x;
  origin.y = bounds.y;
  origin.z = 0.0f;
  clutter_paint_volume_set_origin (volume, &origin);

  clutter_paint_volume_set_width (volume, bounds.width);
  clutter_paint_volume_set_height (volume, bounds.height);

  return TRUE;
}

static gboolean
meta_window_actor_has_shadow (MetaWindowActor *self)
{
  MetaWindowActorPrivate *priv = self->priv;
  MetaWindowType window_type = meta_window_get_window_type (priv->window);

  if (priv->no_shadow)
    return FALSE;

  /* Leaving out shadows for maximized and fullscreen windows is an effeciency
   * win and also prevents the unsightly effect of the shadow of maximized
   * window appearing on an adjacent window */
  if ((meta_window_get_maximized (priv->window) == (META_MAXIMIZE_HORIZONTAL | META_MAXIMIZE_VERTICAL)) ||
      meta_window_is_fullscreen (priv->window))
    return FALSE;

  /*
   * If we have two snap-tiled windows, we don't want the shadow to obstruct
   * the other window.
   */
  if (meta_window_get_tile_match (priv->window))
    return FALSE;

  /*
   * Always put a shadow around windows with a frame - This should override
   * the restriction about not putting a shadow around ARGB windows.
   */
  if (meta_window_get_frame (priv->window))
    return TRUE;

  /*
   * Do not add shadows to ARGB windows; eventually we should generate a
   * shadow from the input shape for such windows.
   */
  if (priv->argb32 || priv->opacity != 0xff)
    return FALSE;

  /*
   * Add shadows to override redirect windows (e.g., Gtk menus).
   */
  if (priv->window->override_redirect)
    return TRUE;

  /*
   * Don't put shadow around DND icon windows
   */
  if (window_type == META_WINDOW_DND ||
      window_type == META_WINDOW_DESKTOP)
    return FALSE;

  if (window_type == META_WINDOW_MENU
#if 0
      || window_type == META_WINDOW_DROPDOWN_MENU
#endif
      )
    return TRUE;

#if 0
  if (window_type == META_WINDOW_TOOLTIP)
    return TRUE;
#endif

  return FALSE;
}

/**
 * meta_window_actor_get_x_window: (skip)
 * @self: a #MetaWindowActor
 *
 */
Window
meta_window_actor_get_x_window (MetaWindowActor *self)
{
  if (!self)
    return None;

  return self->priv->xwindow;
}

/**
 * meta_window_actor_get_meta_window:
 * @self: a #MetaWindowActor
 *
 * Gets the #MetaWindow object that the the #MetaWindowActor is displaying
 *
 * Return value: (transfer none): the displayed #MetaWindow
 */
MetaWindow *
meta_window_actor_get_meta_window (MetaWindowActor *self)
{
  return self->priv->window;
}

/**
 * meta_window_actor_get_texture:
 * @self: a #MetaWindowActor
 *
 * Gets the ClutterActor that is used to display the contents of the window
 *
 * Return value: (transfer none): the #ClutterActor for the contents
 */
ClutterActor *
meta_window_actor_get_texture (MetaWindowActor *self)
{
  return self->priv->actor;
}

/**
 * meta_window_actor_is_destroyed:
 * @self: a #MetaWindowActor
 *
 * Gets whether the X window that the actor was displaying has been destroyed
 *
 * Return value: %TRUE when the window is destroyed, otherwise %FALSE
 */
gboolean
meta_window_actor_is_destroyed (MetaWindowActor *self)
{
  return self->priv->disposed;
}

gboolean
meta_window_actor_is_override_redirect (MetaWindowActor *self)
{
  return meta_window_is_override_redirect (self->priv->window);
}

const char *meta_window_actor_get_description (MetaWindowActor *self)
{
  /*
   * For windows managed by the WM, we just defer to the WM for the window
   * description. For override-redirect windows, we create the description
   * ourselves, but only on demand.
   */
  if (self->priv->window)
    return meta_window_get_description (self->priv->window);

  if (G_UNLIKELY (self->priv->desc == NULL))
    {
      self->priv->desc = g_strdup_printf ("Override Redirect (0x%x)",
                                         (guint) self->priv->xwindow);
    }

  return self->priv->desc;
}

/**
 * meta_window_actor_get_workspace:
 * @self: #MetaWindowActor
 *
 * Returns the index of workspace on which this window is located; if the
 * window is sticky, or is not currently located on any workspace, returns -1.
 * This function is deprecated  and should not be used in newly written code;
 * meta_window_get_workspace() instead.
 *
 * Return value: (transfer none): index of workspace on which this window is
 * located.
 */
gint
meta_window_actor_get_workspace (MetaWindowActor *self)
{
  MetaWindowActorPrivate *priv;
  MetaWorkspace          *workspace;

  if (!self)
    return -1;

  priv = self->priv;

  if (!priv->window || meta_window_is_on_all_workspaces (priv->window))
    return -1;

  workspace = meta_window_get_workspace (priv->window);

  if (!workspace)
    return -1;

  return meta_workspace_index (workspace);
}

gboolean
meta_window_actor_showing_on_its_workspace (MetaWindowActor *self)
{
  if (!self)
    return FALSE;

  /* If override redirect: */
  if (!self->priv->window)
    return TRUE;

  return meta_window_showing_on_its_workspace (self->priv->window);
}

static void
meta_window_actor_freeze (MetaWindowActor *self)
{
  self->priv->freeze_count++;
}

static void
meta_window_actor_damage_all (MetaWindowActor *self)
{
  MetaWindowActorPrivate *priv = self->priv;
  CoglTexture *texture;

  if (!priv->needs_damage_all)
    return;

  texture = meta_shaped_texture_get_texture (META_SHAPED_TEXTURE (priv->actor));

  if (!priv->mapped || priv->needs_pixmap)
    return;

  meta_shaped_texture_update_area (META_SHAPED_TEXTURE (priv->actor),
                                   0, 0,
                                   cogl_texture_get_width (texture),
                                   cogl_texture_get_height (texture));

  priv->needs_damage_all = FALSE;
  priv->repaint_scheduled = TRUE;
}

static void
meta_window_actor_thaw (MetaWindowActor *self)
{
  self->priv->freeze_count--;

  if (G_UNLIKELY (self->priv->freeze_count < 0))
    {
      g_warning ("Error in freeze/thaw accounting.");
      self->priv->freeze_count = 0;
      return;
    }

  if (self->priv->freeze_count)
    return;

  /* We sometimes ignore moves and resizes on frozen windows */
  meta_window_actor_sync_actor_geometry (self, FALSE);

  /* We do this now since we might be going right back into the
   * frozen state */
  meta_window_actor_handle_updates (self);

  /* Since we ignore damage events while a window is frozen for certain effects
   * we may need to issue an update_area() covering the whole pixmap if we
   * don't know what real damage has happened. */
  if (self->priv->needs_damage_all)
    meta_window_actor_damage_all (self);
}

void
meta_window_actor_queue_frame_drawn (MetaWindowActor *self,
                                     gboolean         no_delay_frame)
{
  MetaWindowActorPrivate *priv = self->priv;
  FrameData *frame = g_slice_new0 (FrameData);

  priv->needs_frame_drawn = TRUE;

  frame->sync_request_serial = priv->window->sync_request_serial;

  priv->frames = g_list_prepend (priv->frames, frame);

  if (no_delay_frame)
    {
      ClutterActor *stage = clutter_actor_get_stage (CLUTTER_ACTOR (self));
      clutter_stage_skip_sync_delay (CLUTTER_STAGE (stage));
    }

  if (!priv->repaint_scheduled)
    {
      /* A frame was marked by the client without actually doing any
       * damage, or while we had the window frozen (e.g. during an
       * interactive resize.) We need to make sure that the
       * pre_paint/post_paint functions get called, enabling us to
       * send a _NET_WM_FRAME_DRAWN. We do a 1-pixel redraw to get
       * consistent timing with non-empty frames.
       */
      if (priv->mapped && !priv->needs_pixmap)
        {
          const cairo_rectangle_int_t clip = { 0, 0, 1, 1 };
          clutter_actor_queue_redraw_with_clip (priv->actor, &clip);
          priv->repaint_scheduled = TRUE;
        }
    }
}

gboolean
meta_window_actor_effect_in_progress (MetaWindowActor *self)
{
  return (self->priv->minimize_in_progress ||
	  self->priv->maximize_in_progress ||
	  self->priv->unmaximize_in_progress ||
	  self->priv->map_in_progress ||
	  self->priv->destroy_in_progress);
}

static gboolean
is_frozen (MetaWindowActor *self)
{
  return self->priv->freeze_count ? TRUE : FALSE;
}

static void
meta_window_actor_queue_create_pixmap (MetaWindowActor *self)
{
  MetaWindowActorPrivate *priv = self->priv;

  priv->needs_pixmap = TRUE;

  if (!priv->mapped)
    return;

  if (is_frozen (self))
    return;

  /* This will cause the compositor paint function to be run
   * if the actor is visible or a clone of the actor is visible.
   * if the actor isn't visible in any way, then we don't
   * need to repair the window anyways, and can wait until
   * the stage is redrawn for some other reason
   *
   * The compositor paint function repairs all windows.
   */
  clutter_actor_queue_redraw (priv->actor);
}

static gboolean
is_freeze_thaw_effect (gulong event)
{
  switch (event)
  {
  case META_PLUGIN_DESTROY:
  case META_PLUGIN_MAXIMIZE:
  case META_PLUGIN_UNMAXIMIZE:
    return TRUE;
    break;
  default:
    return FALSE;
  }
}

static gboolean
start_simple_effect (MetaWindowActor *self,
                     gulong        event)
{
  MetaWindowActorPrivate *priv = self->priv;
  MetaCompScreen *info = meta_screen_get_compositor_data (priv->screen);
  gint *counter = NULL;
  gboolean use_freeze_thaw = FALSE;

  if (!info->plugin_mgr)
    return FALSE;

  switch (event)
  {
  case META_PLUGIN_MINIMIZE:
    counter = &priv->minimize_in_progress;
    break;
  case META_PLUGIN_MAP:
    counter = &priv->map_in_progress;
    break;
  case META_PLUGIN_DESTROY:
    counter = &priv->destroy_in_progress;
    break;
  case META_PLUGIN_UNMAXIMIZE:
  case META_PLUGIN_MAXIMIZE:
  case META_PLUGIN_SWITCH_WORKSPACE:
    g_assert_not_reached ();
    break;
  }

  g_assert (counter);

  use_freeze_thaw = is_freeze_thaw_effect (event);

  if (use_freeze_thaw)
    meta_window_actor_freeze (self);

  (*counter)++;

  if (!meta_plugin_manager_event_simple (info->plugin_mgr,
                                         self,
                                         event))
    {
      (*counter)--;
      if (use_freeze_thaw)
        meta_window_actor_thaw (self);
      return FALSE;
    }

  return TRUE;
}

static void
meta_window_actor_after_effects (MetaWindowActor *self)
{
  MetaWindowActorPrivate *priv = self->priv;

  if (priv->needs_destroy)
    {
      clutter_actor_destroy (CLUTTER_ACTOR (self));
      return;
    }

  meta_window_actor_sync_visibility (self);
  meta_window_actor_sync_actor_geometry (self, FALSE);

  if (!meta_window_is_mapped (priv->window))
    meta_window_actor_detach (self);

  if (priv->needs_pixmap)
    clutter_actor_queue_redraw (priv->actor);
}

void
meta_window_actor_effect_completed (MetaWindowActor *self,
                                    gulong           event)
{
  MetaWindowActorPrivate *priv   = self->priv;

  /* NB: Keep in mind that when effects get completed it possible
   * that the corresponding MetaWindow may have be been destroyed.
   * In this case priv->window will == NULL */

  switch (event)
  {
  case META_PLUGIN_MINIMIZE:
    {
      priv->minimize_in_progress--;
      if (priv->minimize_in_progress < 0)
	{
	  g_warning ("Error in minimize accounting.");
	  priv->minimize_in_progress = 0;
	}
    }
    break;
  case META_PLUGIN_MAP:
    /*
     * Make sure that the actor is at the correct place in case
     * the plugin fscked.
     */
    priv->map_in_progress--;

    if (priv->map_in_progress < 0)
      {
	g_warning ("Error in map accounting.");
	priv->map_in_progress = 0;
      }
    break;
  case META_PLUGIN_DESTROY:
    priv->destroy_in_progress--;

    if (priv->destroy_in_progress < 0)
      {
	g_warning ("Error in destroy accounting.");
	priv->destroy_in_progress = 0;
      }
    break;
  case META_PLUGIN_UNMAXIMIZE:
    priv->unmaximize_in_progress--;
    if (priv->unmaximize_in_progress < 0)
      {
	g_warning ("Error in unmaximize accounting.");
	priv->unmaximize_in_progress = 0;
      }
    break;
  case META_PLUGIN_MAXIMIZE:
    priv->maximize_in_progress--;
    if (priv->maximize_in_progress < 0)
      {
	g_warning ("Error in maximize accounting.");
	priv->maximize_in_progress = 0;
      }
    break;
  case META_PLUGIN_SWITCH_WORKSPACE:
    g_assert_not_reached ();
    break;
  }

  if (is_freeze_thaw_effect (event))
    meta_window_actor_thaw (self);

  if (!meta_window_actor_effect_in_progress (self))
    meta_window_actor_after_effects (self);
}

/* Called to drop our reference to a window backing pixmap that we
 * previously obtained with XCompositeNameWindowPixmap. We do this
 * when the window is unmapped or when we want to update to a new
 * pixmap for a new size.
 */
static void
meta_window_actor_detach (MetaWindowActor *self)
{
  MetaWindowActorPrivate *priv     = self->priv;
  MetaScreen            *screen   = priv->screen;
  MetaDisplay           *display  = meta_screen_get_display (screen);
  Display               *xdisplay = meta_display_get_xdisplay (display);

  if (!priv->back_pixmap)
    return;

  /* Get rid of all references to the pixmap before freeing it; it's unclear whether
   * you are supposed to be able to free a GLXPixmap after freeing the underlying
   * pixmap, but it certainly doesn't work with current DRI/Mesa
   */
  meta_shaped_texture_set_pixmap (META_SHAPED_TEXTURE (priv->actor),
                                  None);
  cogl_flush();

  XFreePixmap (xdisplay, priv->back_pixmap);
  priv->back_pixmap = None;

  meta_window_actor_queue_create_pixmap (self);
}

gboolean
meta_window_actor_should_unredirect (MetaWindowActor *self)
{
  MetaWindow *metaWindow = meta_window_actor_get_meta_window (self);
  MetaWindowActorPrivate *priv = self->priv;

  if (meta_window_requested_dont_bypass_compositor (metaWindow))
    return FALSE;

  if (priv->opacity != 0xff)
    return FALSE;

  if (metaWindow->has_shape)
    return FALSE;

  if (priv->argb32 && !meta_window_requested_bypass_compositor (metaWindow))
    return FALSE;

  if (!meta_window_is_monitor_sized (metaWindow))
    return FALSE;

  if (meta_window_requested_bypass_compositor (metaWindow))
    return TRUE;

  if (meta_window_is_override_redirect (metaWindow))
    return TRUE;

  if (priv->does_full_damage)
    return TRUE;

  return FALSE;
}

void
meta_window_actor_set_redirected (MetaWindowActor *self, gboolean state)
{
  MetaWindow *metaWindow = meta_window_actor_get_meta_window (self);
  MetaDisplay *display = meta_window_get_display (metaWindow);

  Display *xdisplay = meta_display_get_xdisplay (display);
  Window  xwin = meta_window_actor_get_x_window (self);

  if (state)
    {
      meta_error_trap_push (display);
      XCompositeRedirectWindow (xdisplay, xwin, CompositeRedirectManual);
      meta_error_trap_pop (display);
      meta_window_actor_detach (self);
      self->priv->unredirected = FALSE;
    }
  else
    {
      meta_error_trap_push (display);
      XCompositeUnredirectWindow (xdisplay, xwin, CompositeRedirectManual);
      meta_error_trap_pop (display);
      self->priv->unredirected = TRUE;
    }
}

void
meta_window_actor_destroy (MetaWindowActor *self)
{
  MetaWindow	      *window;
  MetaCompScreen      *info;
  MetaWindowActorPrivate *priv;
  MetaWindowType window_type;

  priv = self->priv;

  window = priv->window;
  window_type = meta_window_get_window_type (window);
  meta_window_set_compositor_private (window, NULL);

  /*
   * We remove the window from internal lookup hashes and thus any other
   * unmap events etc fail
   */
  info = meta_screen_get_compositor_data (priv->screen);
  info->windows = g_list_remove (info->windows, (gconstpointer) self);

  if (window_type == META_WINDOW_DROPDOWN_MENU ||
      window_type == META_WINDOW_POPUP_MENU ||
      window_type == META_WINDOW_TOOLTIP ||
      window_type == META_WINDOW_NOTIFICATION ||
      window_type == META_WINDOW_COMBO ||
      window_type == META_WINDOW_DND ||
      window_type == META_WINDOW_OVERRIDE_OTHER)
    {
      /*
       * No effects, just kill it.
       */
      clutter_actor_destroy (CLUTTER_ACTOR (self));
      return;
    }

  priv->needs_destroy = TRUE;

  /*
   * Once the window destruction is initiated we can no longer perform any
   * furter X-based operations. For example, if we have a Map effect running,
   * we cannot query the window geometry once the effect completes. So, flag
   * this.
   */
  priv->no_more_x_calls = TRUE;

  if (!meta_window_actor_effect_in_progress (self))
    clutter_actor_destroy (CLUTTER_ACTOR (self));
}

void
meta_window_actor_sync_actor_geometry (MetaWindowActor *self,
                                       gboolean         did_placement)
{
  MetaWindowActorPrivate *priv = self->priv;
  MetaRectangle window_rect;

  /* Normally we want freezing a window to also freeze its position; this allows
   * windows to atomically move and resize together, either under app control,
   * or because the user is resizing from the left/top. But on initial placement
   * we need to assign a position, since immediately after the window
   * is shown, the map effect will go into effect and prevent further geometry
   * updates.
   */
  if (is_frozen (self) && !did_placement)
    return;

  meta_window_get_input_rect (priv->window, &window_rect);

  if (priv->last_width != window_rect.width ||
      priv->last_height != window_rect.height)
    {
      priv->size_changed = TRUE;
      meta_window_actor_queue_create_pixmap (self);
      meta_window_actor_update_shape (self);

      priv->last_width = window_rect.width;
      priv->last_height = window_rect.height;
    }

  if (meta_window_actor_effect_in_progress (self))
    return;

  clutter_actor_set_position (CLUTTER_ACTOR (self),
                              window_rect.x, window_rect.y);
  clutter_actor_set_size (CLUTTER_ACTOR (self),
                          window_rect.width, window_rect.height);

  g_signal_emit (self, signals[POSITION_CHANGED], 0);
}

void
meta_window_actor_show (MetaWindowActor   *self,
                        MetaCompEffect     effect)
{
  MetaWindowActorPrivate *priv;
  MetaCompScreen         *info;
  gulong                  event;

  priv = self->priv;
  info = meta_screen_get_compositor_data (priv->screen);

  g_return_if_fail (!priv->visible);

  self->priv->visible = TRUE;

  event = 0;
  switch (effect)
    {
    case META_COMP_EFFECT_CREATE:
      event = META_PLUGIN_MAP;
      break;
    case META_COMP_EFFECT_UNMINIMIZE:
      /* FIXME: should have META_PLUGIN_UNMINIMIZE */
      event = META_PLUGIN_MAP;
      break;
    case META_COMP_EFFECT_NONE:
      break;
    case META_COMP_EFFECT_DESTROY:
    case META_COMP_EFFECT_MINIMIZE:
      g_assert_not_reached();
    }

  if (priv->redecorating ||
      info->switch_workspace_in_progress ||
      event == 0 ||
      !start_simple_effect (self, event))
    {
      clutter_actor_show (CLUTTER_ACTOR (self));
      priv->redecorating = FALSE;
    }
}

void
meta_window_actor_hide (MetaWindowActor *self,
                        MetaCompEffect   effect)
{
  MetaWindowActorPrivate *priv;
  MetaCompScreen         *info;
  gulong                  event;

  priv = self->priv;
  info = meta_screen_get_compositor_data (priv->screen);

  g_return_if_fail (priv->visible);

  priv->visible = FALSE;

  /* If a plugin is animating a workspace transition, we have to
   * hold off on hiding the window, and do it after the workspace
   * switch completes
   */
  if (info->switch_workspace_in_progress)
    return;

  event = 0;
  switch (effect)
    {
    case META_COMP_EFFECT_DESTROY:
      event = META_PLUGIN_DESTROY;
      break;
    case META_COMP_EFFECT_MINIMIZE:
      event = META_PLUGIN_MINIMIZE;
      break;
    case META_COMP_EFFECT_NONE:
      break;
    case META_COMP_EFFECT_UNMINIMIZE:
    case META_COMP_EFFECT_CREATE:
      g_assert_not_reached();
    }

  if (event == 0 ||
      !start_simple_effect (self, event))
    clutter_actor_hide (CLUTTER_ACTOR (self));
}

void
meta_window_actor_maximize (MetaWindowActor    *self,
                            MetaRectangle      *old_rect,
                            MetaRectangle      *new_rect)
{
  MetaCompScreen *info = meta_screen_get_compositor_data (self->priv->screen);

  /* The window has already been resized (in order to compute new_rect),
   * which by side effect caused the actor to be resized. Restore it to the
   * old size and position */
  clutter_actor_set_position (CLUTTER_ACTOR (self), old_rect->x, old_rect->y);
  clutter_actor_set_size (CLUTTER_ACTOR (self), old_rect->width, old_rect->height);

  self->priv->maximize_in_progress++;
  meta_window_actor_freeze (self);

  if (!info->plugin_mgr ||
      !meta_plugin_manager_event_maximize (info->plugin_mgr,
                                           self,
                                           META_PLUGIN_MAXIMIZE,
                                           new_rect->x, new_rect->y,
                                           new_rect->width, new_rect->height))

    {
      self->priv->maximize_in_progress--;
      meta_window_actor_thaw (self);
    }
}

void
meta_window_actor_unmaximize (MetaWindowActor   *self,
                              MetaRectangle     *old_rect,
                              MetaRectangle     *new_rect)
{
  MetaCompScreen *info = meta_screen_get_compositor_data (self->priv->screen);

  /* The window has already been resized (in order to compute new_rect),
   * which by side effect caused the actor to be resized. Restore it to the
   * old size and position */
  clutter_actor_set_position (CLUTTER_ACTOR (self), old_rect->x, old_rect->y);
  clutter_actor_set_size (CLUTTER_ACTOR (self), old_rect->width, old_rect->height);

  self->priv->unmaximize_in_progress++;
  meta_window_actor_freeze (self);

  if (!info->plugin_mgr ||
      !meta_plugin_manager_event_maximize (info->plugin_mgr,
                                           self,
                                           META_PLUGIN_UNMAXIMIZE,
                                           new_rect->x, new_rect->y,
                                           new_rect->width, new_rect->height))
    {
      self->priv->unmaximize_in_progress--;
      meta_window_actor_thaw (self);
    }
}

MetaWindowActor *
meta_window_actor_new (MetaWindow *window)
{
  MetaScreen	 	 *screen = meta_window_get_screen (window);
  MetaCompScreen         *info = meta_screen_get_compositor_data (screen);
  MetaWindowActor        *self;
  MetaWindowActorPrivate *priv;
  MetaFrame		 *frame;
  Window		  top_window;
  ClutterActor           *window_group;

  frame = meta_window_get_frame (window);
  if (frame)
    top_window = meta_frame_get_xwindow (frame);
  else
    top_window = meta_window_get_xwindow (window);

  meta_verbose ("add window: Meta %p, xwin 0x%x\n", window, (guint)top_window);

  self = g_object_new (META_TYPE_WINDOW_ACTOR,
                       "meta-window",         window,
                       "x-window",            top_window,
                       "meta-screen",         screen,
                       NULL);

  priv = self->priv;

  priv->last_width = -1;
  priv->last_height = -1;

  priv->mapped = meta_window_toplevel_is_mapped (priv->window);
  if (priv->mapped)
    meta_window_actor_queue_create_pixmap (self);

  meta_window_actor_set_updates_frozen (self,
                                        meta_window_updates_are_frozen (priv->window));

  /* If a window doesn't start off with updates frozen, we should
   * we should send a _NET_WM_FRAME_DRAWN immediately after the first drawn.
   */
  if (priv->window->extended_sync_request_counter && !priv->updates_frozen)
    meta_window_actor_queue_frame_drawn (self, FALSE);

  meta_window_actor_sync_actor_geometry (self, priv->window->placed);

  /* Hang our compositor window state off the MetaWindow for fast retrieval */
  meta_window_set_compositor_private (window, G_OBJECT (self));

  if (window->layer == META_LAYER_OVERRIDE_REDIRECT)
    window_group = info->top_window_group;
  else
    window_group = info->window_group;

  clutter_actor_add_child (window_group, CLUTTER_ACTOR (self));

  clutter_actor_hide (CLUTTER_ACTOR (self));

  clutter_actor_set_reactive (CLUTTER_ACTOR (self), TRUE);

  /* Initial position in the stack is arbitrary; stacking will be synced
   * before we first paint.
   */
  info->windows = g_list_append (info->windows, self);

  return self;
}

void
meta_window_actor_mapped (MetaWindowActor *self)
{
  MetaWindowActorPrivate *priv = self->priv;

  g_return_if_fail (!priv->mapped);

  priv->mapped = TRUE;

  meta_window_actor_queue_create_pixmap (self);
}

void
meta_window_actor_unmapped (MetaWindowActor *self)
{
  MetaWindowActorPrivate *priv = self->priv;

  g_return_if_fail (priv->mapped);

  priv->mapped = FALSE;

  if (meta_window_actor_effect_in_progress (self))
    return;

  meta_window_actor_detach (self);
  priv->needs_pixmap = FALSE;
}

/**
 * meta_window_actor_get_obscured_region:
 * @self: a #MetaWindowActor
 *
 * Gets the region that is completely obscured by the window. Coordinates
 * are relative to the upper-left of the window.
 *
 * Return value: (transfer none): the area obscured by the window,
 *  %NULL is the same as an empty region.
 */
cairo_region_t *
meta_window_actor_get_obscured_region (MetaWindowActor *self)
{
  MetaWindowActorPrivate *priv = self->priv;

  if (priv->back_pixmap && priv->opacity == 0xff)
    return priv->opaque_region;
  else
    return NULL;
}

#if 0
/* Print out a region; useful for debugging */
static void
print_region (cairo_region_t *region)
{
  int n_rects;
  int i;

  n_rects = cairo_region_num_rectangles (region);
  g_print ("[");
  for (i = 0; i < n_rects; i++)
    {
      cairo_rectangle_int_t rect;
      cairo_region_get_rectangle (region, i, &rect);
      g_print ("+%d+%dx%dx%d ",
               rect.x, rect.y, rect.width, rect.height);
    }
  g_print ("]\n");
}
#endif

#if 0
/* Dump a region to a PNG file; useful for debugging */
static void
see_region (cairo_region_t *region,
            int             width,
            int             height,
            char           *filename)
{
  cairo_surface_t *surface = cairo_image_surface_create (CAIRO_FORMAT_A8, width, height);
  cairo_t *cr = cairo_create (surface);

  gdk_cairo_region (cr, region);
  cairo_fill (cr);

  cairo_surface_write_to_png (surface, filename);
  cairo_destroy (cr);
  cairo_surface_destroy (surface);
}
#endif

/**
 * meta_window_actor_set_visible_region:
 * @self: a #MetaWindowActor
 * @visible_region: the region of the screen that isn't completely
 *  obscured.
 *
 * Provides a hint as to what areas of the window need to be
 * drawn. Regions not in @visible_region are completely obscured.
 * This will be set before painting then unset afterwards.
 */
void
meta_window_actor_set_visible_region (MetaWindowActor *self,
                                      cairo_region_t  *visible_region)
{
  MetaWindowActorPrivate *priv = self->priv;

  meta_shaped_texture_set_clip_region (META_SHAPED_TEXTURE (priv->actor),
                                       visible_region);
}

/**
 * meta_window_actor_set_visible_region_beneath:
 * @self: a #MetaWindowActor
 * @visible_region: the region of the screen that isn't completely
 *  obscured beneath the main window texture.
 *
 * Provides a hint as to what areas need to be drawn *beneath*
 * the main window texture.  This is the relevant visible region
 * when drawing the shadow, properly accounting for areas of the
 * shadow hid by the window itself. This will be set before painting
 * then unset afterwards.
 */
void
meta_window_actor_set_visible_region_beneath (MetaWindowActor *self,
                                              cairo_region_t  *beneath_region)
{
  MetaWindowActorPrivate *priv = self->priv;
  gboolean appears_focused = meta_window_appears_focused (priv->window);

  if (appears_focused ? priv->focused_shadow : priv->unfocused_shadow)
    {
      g_clear_pointer (&priv->shadow_clip, cairo_region_destroy);
      priv->shadow_clip = cairo_region_copy (beneath_region);

      if (clip_shadow_under_window (self))
        {
          cairo_region_t *frame_bounds = meta_window_get_frame_bounds (priv->window);
          cairo_region_subtract (priv->shadow_clip, frame_bounds);
        }
    }
}

/**
 * meta_window_actor_reset_visible_regions:
 * @self: a #MetaWindowActor
 *
 * Unsets the regions set by meta_window_actor_reset_visible_region() and
 * meta_window_actor_reset_visible_region_beneath()
 */
void
meta_window_actor_reset_visible_regions (MetaWindowActor *self)
{
  MetaWindowActorPrivate *priv = self->priv;

  meta_shaped_texture_set_clip_region (META_SHAPED_TEXTURE (priv->actor),
                                       NULL);
  g_clear_pointer (&priv->shadow_clip, cairo_region_destroy);
}

static void
check_needs_pixmap (MetaWindowActor *self)
{
  MetaWindowActorPrivate *priv     = self->priv;
  MetaScreen          *screen   = priv->screen;
  MetaDisplay         *display  = meta_screen_get_display (screen);
  Display             *xdisplay = meta_display_get_xdisplay (display);
  MetaCompScreen      *info     = meta_screen_get_compositor_data (screen);
  MetaCompositor      *compositor;
  Window               xwindow  = priv->xwindow;

  if (!priv->needs_pixmap)
    return;

  if (!priv->mapped)
    return;

  if (xwindow == meta_screen_get_xroot (screen) ||
      xwindow == clutter_x11_get_stage_window (CLUTTER_STAGE (info->stage)))
    return;

  compositor = meta_display_get_compositor (display);

  if (priv->size_changed)
    {
      meta_window_actor_detach (self);
      priv->size_changed = FALSE;
    }

  meta_error_trap_push (display);

  if (priv->back_pixmap == None)
    {
      CoglTexture *texture;

      meta_error_trap_push (display);

      priv->back_pixmap = XCompositeNameWindowPixmap (xdisplay, xwindow);

      if (meta_error_trap_pop_with_return (display) != Success)
        {
          /* Probably a BadMatch if the window isn't viewable; we could
           * GrabServer/GetWindowAttributes/NameWindowPixmap/UngrabServer/Sync
           * to avoid this, but there's no reason to take two round trips
           * when one will do. (We need that Sync if we want to handle failures
           * for any reason other than !viewable. That's unlikely, but maybe
           * we'll BadAlloc or something.)
           */
          priv->back_pixmap = None;
        }

      if (priv->back_pixmap == None)
        {
          meta_verbose ("Unable to get named pixmap for %p\n", self);
          goto out;
        }

      if (compositor->no_mipmaps)
        meta_shaped_texture_set_create_mipmaps (META_SHAPED_TEXTURE (priv->actor),
                                                FALSE);

      meta_shaped_texture_set_pixmap (META_SHAPED_TEXTURE (priv->actor),
                                      priv->back_pixmap);

      texture = meta_shaped_texture_get_texture (META_SHAPED_TEXTURE (priv->actor));

      /*
       * This only works *after* actually setting the pixmap, so we have to
       * do it here.
       * See: http://bugzilla.clutter-project.org/show_bug.cgi?id=2236
       */
      if (G_UNLIKELY (!cogl_texture_pixmap_x11_is_using_tfp_extension (COGL_TEXTURE_PIXMAP_X11 (texture))))
        g_warning ("NOTE: Not using GLX TFP!\n");

      /* ::size-changed is supposed to refer to meta_window_get_outer_rect().
       * Emitting it here works pretty much OK because a new value of the
       * *input* rect (which is the outer rect with the addition of invisible
       * borders) forces a new pixmap and we get here. In the rare case where
       * a change to the window size was exactly balanced by a change to the
       * invisible borders, we would miss emitting the signal. We would also
       * emit spurious signals when we get a new pixmap without a new size,
       * but that should be mostly harmless.
       */
      g_signal_emit (self, signals[SIZE_CHANGED], 0);
    }

  priv->needs_pixmap = FALSE;

 out:
  meta_error_trap_pop (display);
}

static void
check_needs_shadow (MetaWindowActor *self)
{
  MetaWindowActorPrivate *priv = self->priv;
  MetaShadow *old_shadow = NULL;
  MetaShadow **shadow_location;
  gboolean recompute_shadow;
  gboolean should_have_shadow;
  gboolean appears_focused;

  if (!priv->mapped)
    return;

  /* Calling meta_window_actor_has_shadow() here at every pre-paint is cheap
   * and avoids the need to explicitly handle window type changes, which
   * we would do if tried to keep track of when we might be adding or removing
   * a shadow more explicitly. We only keep track of changes to the *shape* of
   * the shadow with priv->recompute_shadow.
   */

  should_have_shadow = meta_window_actor_has_shadow (self);
  appears_focused = meta_window_appears_focused (priv->window);

  if (appears_focused)
    {
      recompute_shadow = priv->recompute_focused_shadow;
      priv->recompute_focused_shadow = FALSE;
      shadow_location = &priv->focused_shadow;
    }
  else
    {
      recompute_shadow = priv->recompute_unfocused_shadow;
      priv->recompute_unfocused_shadow = FALSE;
      shadow_location = &priv->unfocused_shadow;
    }

  if (!should_have_shadow || recompute_shadow)
    {
      if (*shadow_location != NULL)
        {
          old_shadow = *shadow_location;
          *shadow_location = NULL;
        }
    }

  if (*shadow_location == NULL && should_have_shadow)
    {
      if (priv->shadow_shape == NULL)
        priv->shadow_shape = meta_window_shape_new (priv->shape_region);

      MetaShadowFactory *factory = meta_shadow_factory_get_default ();
      const char *shadow_class = meta_window_actor_get_shadow_class (self);
      cairo_rectangle_int_t shape_bounds;

      meta_window_actor_get_shape_bounds (self, &shape_bounds);
      *shadow_location = meta_shadow_factory_get_shadow (factory,
                                                         priv->shadow_shape,
                                                         shape_bounds.width, shape_bounds.height,
                                                         shadow_class, appears_focused);
    }

  if (old_shadow != NULL)
    meta_shadow_unref (old_shadow);
}

void
meta_window_actor_process_damage (MetaWindowActor    *self,
                                  XDamageNotifyEvent *event)
{
  MetaWindowActorPrivate *priv = self->priv;
  MetaCompScreen *info = meta_screen_get_compositor_data (priv->screen);

  priv->received_damage = TRUE;

  if (meta_window_is_fullscreen (priv->window) && g_list_last (info->windows)->data == self && !priv->unredirected)
    {
      MetaRectangle window_rect;
      meta_window_get_outer_rect (priv->window, &window_rect);

      if (window_rect.x == event->area.x &&
          window_rect.y == event->area.y &&
          window_rect.width == event->area.width &&
          window_rect.height == event->area.height)
        priv->full_damage_frames_count++;
      else
        priv->full_damage_frames_count = 0;

      if (priv->full_damage_frames_count >= 100)
        priv->does_full_damage = TRUE;
    }

  /* Drop damage event for unredirected windows */
  if (priv->unredirected)
    return;

  if (is_frozen (self))
    {
      /* The window is frozen due to an effect in progress: we ignore damage
       * here on the off chance that this will stop the corresponding
       * texture_from_pixmap from being update.
       *
       * needs_damage_all tracks that some unknown damage happened while the
       * window was frozen so that when the window becomes unfrozen we can
       * issue a full window update to cover any lost damage.
       *
       * It should be noted that this is an unreliable mechanism since it's
       * quite likely that drivers will aim to provide a zero-copy
       * implementation of the texture_from_pixmap extension and in those cases
       * any drawing done to the window is always immediately reflected in the
       * texture regardless of damage event handling.
       */
      priv->needs_damage_all = TRUE;
      return;
    }

  if (!priv->mapped || priv->needs_pixmap)
    return;

  meta_shaped_texture_update_area (META_SHAPED_TEXTURE (priv->actor),
                                   event->area.x,
                                   event->area.y,
                                   event->area.width,
                                   event->area.height);
  priv->repaint_scheduled = TRUE;
}

void
meta_window_actor_sync_visibility (MetaWindowActor *self)
{
  MetaWindowActorPrivate *priv = self->priv;

  if (CLUTTER_ACTOR_IS_VISIBLE (self) != priv->visible)
    {
      if (priv->visible)
        clutter_actor_show (CLUTTER_ACTOR (self));
      else
        clutter_actor_hide (CLUTTER_ACTOR (self));
    }
}

#define TAU (2*M_PI)

static void
install_corners (MetaWindow       *window,
                 MetaFrameBorders *borders,
                 cairo_t          *cr)
{
  float top_left, top_right, bottom_left, bottom_right;
  int x, y;
  MetaRectangle outer;

  meta_frame_get_corner_radiuses (window->frame,
                                  &top_left,
                                  &top_right,
                                  &bottom_left,
                                  &bottom_right);

  meta_window_get_outer_rect (window, &outer);

  /* top left */
  x = borders->invisible.left;
  y = borders->invisible.top;

  cairo_arc (cr,
             x + top_left,
             y + top_left,
             top_left,
             2 * TAU / 4,
             3 * TAU / 4);

  /* top right */
  x = borders->invisible.left + outer.width - top_right;
  y = borders->invisible.top;

  cairo_arc (cr,
             x,
             y + top_right,
             top_right,
             3 * TAU / 4,
             4 * TAU / 4);

  /* bottom right */
  x = borders->invisible.left + outer.width - bottom_right;
  y = borders->invisible.top + outer.height - bottom_right;

  cairo_arc (cr,
             x,
             y,
             bottom_right,
             0 * TAU / 4,
             1 * TAU / 4);

  /* bottom left */
  x = borders->invisible.left;
  y = borders->invisible.top + outer.height - bottom_left;

  cairo_arc (cr,
             x + bottom_left,
             y,
             bottom_left,
             1 * TAU / 4,
             2 * TAU / 4);

  cairo_set_source_rgba (cr, 1, 1, 1, 1);
  cairo_fill (cr);
}

static cairo_region_t *
scan_visible_region (guchar         *mask_data,
                     int             stride,
                     cairo_region_t *scan_area)
{
  int i, n_rects = cairo_region_num_rectangles (scan_area);
  MetaRegionBuilder builder;

  meta_region_builder_init (&builder);

  for (i = 0; i < n_rects; i++)
    {
      int x, y;
      cairo_rectangle_int_t rect;

      cairo_region_get_rectangle (scan_area, i, &rect);

      for (y = rect.y; y < (rect.y + rect.height); y++)
        {
          for (x = rect.x; x < (rect.x + rect.width); x++)
            {
              int x2 = x;
              while (mask_data[y * stride + x2] == 255 && x2 < (rect.x + rect.width))
                x2++;

              if (x2 > x)
                {
                  meta_region_builder_add_rectangle (&builder, x, y, x2 - x, 1);
                  x = x2;
                }
            }
        }
    }

  return meta_region_builder_finish (&builder);
}

static void
build_and_scan_frame_mask (MetaWindowActor       *self,
                           MetaFrameBorders      *borders,
                           cairo_rectangle_int_t *client_area,
                           cairo_region_t        *shape_region)
{
  MetaWindowActorPrivate *priv = self->priv;
  guchar *mask_data;
  guint tex_width, tex_height;
  CoglTexture *paint_tex, *mask_texture;
  int stride;
  cairo_t *cr;
  cairo_surface_t *surface;

  paint_tex = meta_shaped_texture_get_texture (META_SHAPED_TEXTURE (priv->actor));
  if (paint_tex == NULL)
    return;

  tex_width = cogl_texture_get_width (paint_tex);
  tex_height = cogl_texture_get_height (paint_tex);

  stride = cairo_format_stride_for_width (CAIRO_FORMAT_A8, tex_width);

  /* Create data for an empty image */
  mask_data = g_malloc0 (stride * tex_height);

  surface = cairo_image_surface_create_for_data (mask_data,
                                                 CAIRO_FORMAT_A8,
                                                 tex_width,
                                                 tex_height,
                                                 stride);
  cr = cairo_create (surface);

  gdk_cairo_region (cr, shape_region);
  cairo_fill (cr);

  if (priv->window->frame != NULL)
    {
      cairo_region_t *frame_paint_region, *scanned_region;
      cairo_rectangle_int_t rect = { 0, 0, tex_width, tex_height };

      /* Make sure we don't paint the frame over the client window. */
      frame_paint_region = cairo_region_create_rectangle (&rect);
      cairo_region_subtract_rectangle (frame_paint_region, client_area);

      gdk_cairo_region (cr, frame_paint_region);
      cairo_clip (cr);

      install_corners (priv->window, borders, cr);

      cairo_surface_flush (surface);
      scanned_region = scan_visible_region (mask_data, stride, frame_paint_region);
      cairo_region_union (shape_region, scanned_region);
      cairo_region_destroy (scanned_region);
      cairo_region_destroy (frame_paint_region);
    }

  cairo_destroy (cr);
  cairo_surface_destroy (surface);

  if (meta_texture_rectangle_check (paint_tex))
    {
      mask_texture = meta_texture_rectangle_new (tex_width, tex_height,
                                                 COGL_PIXEL_FORMAT_A_8,
                                                 COGL_PIXEL_FORMAT_A_8,
                                                 stride,
                                                 mask_data,
                                                 NULL /* error */);
    }
  else
    {
      /* Note: we don't allow slicing for this texture because we
       * need to use it with multi-texturing which doesn't support
       * sliced textures */
      mask_texture = cogl_texture_new_from_data (tex_width, tex_height,
                                                 COGL_TEXTURE_NO_SLICING,
                                                 COGL_PIXEL_FORMAT_A_8,
                                                 COGL_PIXEL_FORMAT_ANY,
                                                 stride,
                                                 mask_data);
    }

  meta_shaped_texture_set_mask_texture (META_SHAPED_TEXTURE (priv->actor),
                                        mask_texture);
  cogl_object_unref (mask_texture);

  g_free (mask_data);
}

static void
check_needs_reshape (MetaWindowActor *self)
{
  MetaWindowActorPrivate *priv = self->priv;
  MetaScreen *screen = priv->screen;
  MetaDisplay *display = meta_screen_get_display (screen);
  MetaFrameBorders borders;
  cairo_region_t *region = NULL;
  cairo_rectangle_int_t client_area;
  gboolean needs_mask;

  if (!priv->mapped)
    return;

  if (!priv->needs_reshape)
    return;

  if (priv->shadow_shape != NULL)
    {
      meta_window_shape_unref (priv->shadow_shape);
      priv->shadow_shape = NULL;
    }

  meta_frame_calc_borders (priv->window->frame, &borders);

  client_area.x = borders.total.left;
  client_area.y = borders.total.top;
  client_area.width = priv->window->rect.width;
  client_area.height = priv->window->rect.height;

  meta_shaped_texture_set_mask_texture (META_SHAPED_TEXTURE (priv->actor), NULL);
  g_clear_pointer (&priv->shape_region, cairo_region_destroy);
  g_clear_pointer (&priv->opaque_region, cairo_region_destroy);

#ifdef HAVE_SHAPE
  if (priv->window->has_shape)
    {
      /* Translate the set of XShape rectangles that we
       * get from the X server to a cairo_region. */
      Display *xdisplay = meta_display_get_xdisplay (display);
      XRectangle *rects;
      int n_rects, ordering;

      meta_error_trap_push (display);
      rects = XShapeGetRectangles (xdisplay,
                                   priv->window->xwindow,
                                   ShapeBounding,
                                   &n_rects,
                                   &ordering);
      meta_error_trap_pop (display);

      if (rects)
        {
          int i;
          cairo_rectangle_int_t *cairo_rects = g_new (cairo_rectangle_int_t, n_rects);

          for (i = 0; i < n_rects; i ++)
            {
              cairo_rects[i].x = rects[i].x + client_area.x;
              cairo_rects[i].y = rects[i].y + client_area.y;
              cairo_rects[i].width = rects[i].width;
              cairo_rects[i].height = rects[i].height;
            }

          XFree (rects);
          region = cairo_region_create_rectangles (cairo_rects, n_rects);
          g_free (cairo_rects);
        }
    }
#endif

  needs_mask = (region != NULL) || (priv->window->frame != NULL);

  if (region != NULL)
    {
      /* The shape we get back from the client may have coordinates
       * outside of the frame. The X SHAPE Extension requires that
       * the overall shape the client provides never exceeds the
       * "bounding rectangle" of the window -- the shape that the
       * window would have gotten if it was unshaped. In our case,
       * this is simply the client area.
       */
      cairo_region_intersect_rectangle (region, &client_area);
    }
  else
    {
      /* If we don't have a shape on the server, that means that
       * we have an implicit shape of one rectangle covering the
       * entire window. */
      region = cairo_region_create_rectangle (&client_area);
    }

  /* The region at this point should be constrained to the
   * bounds of the client rectangle. */

  if (priv->argb32 && priv->window->opaque_region != NULL)
    {
      /* The opaque region is defined to be a part of the
       * window which ARGB32 will always paint with opaque
       * pixels. For these regions, we want to avoid painting
       * windows and shadows beneath them.
       *
       * If the client gives bad coordinates where it does not
       * fully paint, the behavior is defined by the specification
       * to be undefined, and considered a client bug. In mutter's
       * case, graphical glitches will occur.
       */
      priv->opaque_region = cairo_region_copy (priv->window->opaque_region);
      cairo_region_translate (priv->opaque_region, client_area.x, client_area.y);
      cairo_region_intersect (priv->opaque_region, region);
    }
  else if (priv->argb32)
    priv->opaque_region = NULL;
  else
    priv->opaque_region = cairo_region_reference (region);

  if (needs_mask)
    {
      /* This takes the region, generates a mask using GTK+
       * and scans the mask looking for all opaque pixels,
       * adding it to region.
       */
      build_and_scan_frame_mask (self, &borders, &client_area, region);
    }

  priv->shape_region = region;

  priv->needs_reshape = FALSE;
  meta_window_actor_invalidate_shadow (self);
}

void
meta_window_actor_update_shape (MetaWindowActor *self)
{
  MetaWindowActorPrivate *priv = self->priv;

  priv->needs_reshape = TRUE;

  if (is_frozen (self))
    return;

  clutter_actor_queue_redraw (priv->actor);
}

static void
meta_window_actor_handle_updates (MetaWindowActor *self)
{
  MetaWindowActorPrivate *priv = self->priv;
  MetaScreen          *screen   = priv->screen;
  MetaDisplay         *display  = meta_screen_get_display (screen);
  Display             *xdisplay = meta_display_get_xdisplay (display);

  if (is_frozen (self))
    {
      /* The window is frozen due to a pending animation: we'll wait until
       * the animation finishes to reshape and repair the window */
      return;
    }

  if (priv->unredirected)
    {
      /* Nothing to do here until/if the window gets redirected again */
      return;
    }

  if (priv->received_damage)
    {
      meta_error_trap_push (display);
      XDamageSubtract (xdisplay, priv->damage, None, None);
      meta_error_trap_pop (display);

      /* We need to make sure that any X drawing that happens before the
       * XDamageSubtract() above is visible to subsequent GL rendering;
       * the only standardized way to do this is EXT_x11_sync_object,
       * which isn't yet widely available. For now, we count on details
       * of Xorg and the open source drivers, and hope for the best
       * otherwise.
       *
       * Xorg and open source driver specifics:
       *
       * The X server makes sure to flush drawing to the kernel before
       * sending out damage events, but since we use DamageReportBoundingBox
       * there may be drawing between the last damage event and the
       * XDamageSubtract() that needs to be flushed as well.
       *
       * Xorg always makes sure that drawing is flushed to the kernel
       * before writing events or responses to the client, so any round trip
       * request at this point is sufficient to flush the GLX buffers.
       */
      XSync (xdisplay, False);

      priv->received_damage = FALSE;
    }

  check_needs_pixmap (self);
  check_needs_reshape (self);
  check_needs_shadow (self);
}

void
meta_window_actor_pre_paint (MetaWindowActor *self)
{
  MetaWindowActorPrivate *priv = self->priv;
  GList *l;

  meta_window_actor_handle_updates (self);

  for (l = priv->frames; l != NULL; l = l->next)
    {
      FrameData *frame = l->data;

      if (frame->frame_counter == 0)
        {
          CoglOnscreen *onscreen = COGL_ONSCREEN (cogl_get_draw_framebuffer());
          frame->frame_counter = cogl_onscreen_get_frame_counter (onscreen);
        }
    }
}

void
meta_window_actor_post_paint (MetaWindowActor *self)
{
  MetaWindowActorPrivate *priv = self->priv;

  priv->repaint_scheduled = FALSE;

  if (priv->needs_frame_drawn)
    {
      MetaScreen  *screen  = priv->screen;
      MetaDisplay *display = meta_screen_get_display (screen);
      Display *xdisplay = meta_display_get_xdisplay (display);

      XClientMessageEvent ev = { 0, };

      FrameData *frame = priv->frames->data;

      frame->frame_drawn_time = meta_compositor_monotonic_time_to_server_time (display,
                                                                               g_get_monotonic_time ());
      ev.type = ClientMessage;
      ev.window = meta_window_get_xwindow (priv->window);
      ev.message_type = display->atom__NET_WM_FRAME_DRAWN;
      ev.format = 32;
      ev.data.l[0] = frame->sync_request_serial & G_GUINT64_CONSTANT(0xffffffff);
      ev.data.l[1] = frame->sync_request_serial >> 32;
      ev.data.l[2] = frame->frame_drawn_time & G_GUINT64_CONSTANT(0xffffffff);
      ev.data.l[3] = frame->frame_drawn_time >> 32;

      meta_error_trap_push (display);
      XSendEvent (xdisplay, ev.window, False, 0, (XEvent*) &ev);
      XFlush (xdisplay);
      meta_error_trap_pop (display);

      priv->needs_frame_drawn = FALSE;
    }
}

static void
send_frame_timings (MetaWindowActor  *self,
                    FrameData        *frame,
                    CoglFrameInfo    *frame_info,
                    gint64            presentation_time)
{
  MetaWindowActorPrivate *priv = self->priv;
  MetaDisplay *display = meta_screen_get_display (priv->screen);
  Display *xdisplay = meta_display_get_xdisplay (display);
  float refresh_rate;
  int refresh_interval;

  XClientMessageEvent ev = { 0, };

  ev.type = ClientMessage;
  ev.window = meta_window_get_xwindow (priv->window);
  ev.message_type = display->atom__NET_WM_FRAME_TIMINGS;
  ev.format = 32;
  ev.data.l[0] = frame->sync_request_serial & G_GUINT64_CONSTANT(0xffffffff);
  ev.data.l[1] = frame->sync_request_serial >> 32;

  refresh_rate = cogl_frame_info_get_refresh_rate (frame_info);
  /* 0.0 is a flag for not known, but sanity-check against other odd numbers */
  if (refresh_rate >= 1.0)
    refresh_interval = (int) (0.5 + 1000000 / refresh_rate);
  else
    refresh_interval = 0;

  if (presentation_time != 0)
    {
      gint64 presentation_time_server = meta_compositor_monotonic_time_to_server_time (display,
                                                                                       presentation_time);
      gint64 presentation_time_offset = presentation_time_server - frame->frame_drawn_time;
      if (presentation_time_offset == 0)
        presentation_time_offset = 1;

      if ((gint32)presentation_time_offset == presentation_time_offset)
        ev.data.l[2] = presentation_time_offset;
    }

  ev.data.l[3] = refresh_interval;
  ev.data.l[4] = 1000 * META_SYNC_DELAY;

  meta_error_trap_push (display);
  XSendEvent (xdisplay, ev.window, False, 0, (XEvent*) &ev);
  XFlush (xdisplay);
  meta_error_trap_pop (display);
}

void
meta_window_actor_frame_complete (MetaWindowActor *self,
                                  CoglFrameInfo   *frame_info,
                                  gint64           presentation_time)
{
  MetaWindowActorPrivate *priv = self->priv;
  GList *l;

  for (l = priv->frames; l;)
    {
      GList *l_next = l->next;
      FrameData *frame = l->data;

      if (frame->frame_counter == cogl_frame_info_get_frame_counter (frame_info))
        {
          if (frame->frame_drawn_time != 0)
            {
              priv->frames = g_list_delete_link (priv->frames, l);
              send_frame_timings (self, frame, frame_info, presentation_time);
              frame_data_free (frame);
            }
        }

      l = l_next;
    }
}

void
meta_window_actor_invalidate_shadow (MetaWindowActor *self)
{
  MetaWindowActorPrivate *priv = self->priv;

  priv->recompute_focused_shadow = TRUE;
  priv->recompute_unfocused_shadow = TRUE;

  if (is_frozen (self))
    return;

  clutter_actor_queue_redraw (CLUTTER_ACTOR (self));
}

void
meta_window_actor_update_opacity (MetaWindowActor *self)
{
  MetaWindowActorPrivate *priv = self->priv;
  MetaDisplay *display = meta_screen_get_display (priv->screen);
  MetaCompositor *compositor = meta_display_get_compositor (display);
  Window xwin = meta_window_get_xwindow (priv->window);
  gulong value;
  guint8 opacity;

  if (meta_prop_get_cardinal (display, xwin,
                              compositor->atom_net_wm_window_opacity,
                              &value))
    {
      opacity = (guint8)((gfloat)value * 255.0 / ((gfloat)0xffffffff));
    }
  else
    opacity = 255;

  self->priv->opacity = opacity;
  clutter_actor_set_opacity (self->priv->actor, opacity);
}

void
meta_window_actor_set_updates_frozen (MetaWindowActor *self,
                                      gboolean         updates_frozen)
{
  MetaWindowActorPrivate *priv = self->priv;

  updates_frozen = updates_frozen != FALSE;

  if (priv->updates_frozen != updates_frozen)
    {
      priv->updates_frozen = updates_frozen;
      if (updates_frozen)
        meta_window_actor_freeze (self);
      else
        meta_window_actor_thaw (self);
    }
}