/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */

#define _ISOC99_SOURCE /* for roundf */
#include <math.h>

#include <X11/extensions/shape.h>
#include <X11/extensions/Xcomposite.h>
#include <X11/extensions/Xdamage.h>
#include <X11/extensions/Xrender.h>

#include <clutter/x11/clutter-x11.h>

#include "display.h"
#include "errors.h"
#include "frame.h"
#include "window.h"
#include "xprops.h"

#include "compositor-private.h"
#include "mutter-shaped-texture.h"
#include "mutter-window-private.h"
#include "shadow.h"
#include "tidy/tidy-texture-frame.h"

struct _MutterWindowPrivate
{
  XWindowAttributes attrs;

  MetaWindow       *window;
  Window            xwindow;
  MetaScreen       *screen;

  ClutterActor     *actor;
  ClutterActor     *shadow;
  Pixmap            back_pixmap;

  MetaCompWindowType  type;
  Damage            damage;

  guint8            opacity;

  gchar *           desc;

  /* If the window is shaped, a region that matches the shape */
  MetaRegion        *shape_region;
  /* A rectangular region with the unshaped extends of the window
   * texture */
  MetaRegion        *bounding_region;

  gint              freeze_count;

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

  guint		    visible                : 1;
  guint		    mapped                 : 1;
  guint		    shaped                 : 1;
  guint		    argb32                 : 1;
  guint		    disposed               : 1;
  guint             redecorating           : 1;

  guint		    needs_damage_all       : 1;
  guint		    received_damage        : 1;

  guint		    needs_pixmap           : 1;
  guint		    needs_reshape          : 1;
  guint		    size_changed           : 1;

  guint		    needs_destroy	   : 1;

  guint             no_shadow              : 1;

  guint             no_more_x_calls        : 1;
};

enum
{
  PROP_MCW_META_WINDOW = 1,
  PROP_MCW_META_SCREEN,
  PROP_MCW_X_WINDOW,
  PROP_MCW_X_WINDOW_ATTRIBUTES,
  PROP_MCW_NO_SHADOW,
};

static void mutter_window_dispose    (GObject *object);
static void mutter_window_finalize   (GObject *object);
static void mutter_window_constructed (GObject *object);
static void mutter_window_set_property (GObject       *object,
					   guint         prop_id,
					   const GValue *value,
					   GParamSpec   *pspec);
static void mutter_window_get_property (GObject      *object,
					   guint         prop_id,
					   GValue       *value,
					   GParamSpec   *pspec);

static void     mutter_window_detach     (MutterWindow *self);
static gboolean mutter_window_has_shadow (MutterWindow *self);

static void mutter_window_clear_shape_region    (MutterWindow *self);
static void mutter_window_clear_bounding_region (MutterWindow *self);

static gboolean is_shaped                (MetaDisplay  *display,
                                          Window        xwindow);
/*
 * Register GType wrapper for XWindowAttributes, so we do not have to
 * query window attributes in the MutterWindow constructor but can pass
 * them as a property to the constructor (so we can gracefully handle the case
 * where no attributes can be retrieved).
 *
 * NB -- we only need a subset of the attributes; at some point we might want
 * to just store the relevant values rather than the whole struct.
 */
#define META_TYPE_XATTRS (meta_xattrs_get_type ())

static GType meta_xattrs_get_type   (void) G_GNUC_CONST;

static XWindowAttributes *
meta_xattrs_copy (const XWindowAttributes *attrs)
{
  XWindowAttributes *result;

  g_return_val_if_fail (attrs != NULL, NULL);

  result = (XWindowAttributes*) g_malloc (sizeof (XWindowAttributes));
  *result = *attrs;

  return result;
}

static void
meta_xattrs_free (XWindowAttributes *attrs)
{
  g_return_if_fail (attrs != NULL);

  g_free (attrs);
}

static GType
meta_xattrs_get_type (void)
{
  static GType our_type = 0;

  if (!our_type)
    our_type = g_boxed_type_register_static ("XWindowAttributes",
		                     (GBoxedCopyFunc) meta_xattrs_copy,
				     (GBoxedFreeFunc) meta_xattrs_free);
  return our_type;
}

G_DEFINE_TYPE (MutterWindow, mutter_window, CLUTTER_TYPE_GROUP);

static void
mutter_window_class_init (MutterWindowClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GParamSpec   *pspec;

  g_type_class_add_private (klass, sizeof (MutterWindowPrivate));

  object_class->dispose      = mutter_window_dispose;
  object_class->finalize     = mutter_window_finalize;
  object_class->set_property = mutter_window_set_property;
  object_class->get_property = mutter_window_get_property;
  object_class->constructed  = mutter_window_constructed;

  pspec = g_param_spec_object ("meta-window",
                               "MetaWindow",
                               "The displayed MetaWindow",
                               META_TYPE_WINDOW,
                               G_PARAM_READWRITE | G_PARAM_CONSTRUCT);

  g_object_class_install_property (object_class,
                                   PROP_MCW_META_WINDOW,
                                   pspec);

  pspec = g_param_spec_pointer ("meta-screen",
				"MetaScreen",
				"MetaScreen",
				G_PARAM_READWRITE | G_PARAM_CONSTRUCT);

  g_object_class_install_property (object_class,
                                   PROP_MCW_META_SCREEN,
                                   pspec);

  pspec = g_param_spec_ulong ("x-window",
			      "Window",
			      "Window",
			      0,
			      G_MAXULONG,
			      0,
			      G_PARAM_READWRITE | G_PARAM_CONSTRUCT);

  g_object_class_install_property (object_class,
                                   PROP_MCW_X_WINDOW,
                                   pspec);

  pspec = g_param_spec_boxed ("x-window-attributes",
			      "XWindowAttributes",
			      "XWindowAttributes",
			      META_TYPE_XATTRS,
			      G_PARAM_READWRITE | G_PARAM_CONSTRUCT);

  g_object_class_install_property (object_class,
                                   PROP_MCW_X_WINDOW_ATTRIBUTES,
                                   pspec);

  pspec = g_param_spec_boolean ("no-shadow",
                                "No shadow",
                                "Do not add shaddow to this window",
                                FALSE,
                                G_PARAM_READWRITE | G_PARAM_CONSTRUCT);

  g_object_class_install_property (object_class,
                                   PROP_MCW_NO_SHADOW,
                                   pspec);
}

static void
mutter_window_init (MutterWindow *self)
{
  MutterWindowPrivate *priv;

  priv = self->priv = G_TYPE_INSTANCE_GET_PRIVATE (self,
						   MUTTER_TYPE_COMP_WINDOW,
						   MutterWindowPrivate);
  priv->opacity = 0xff;
}

static void
mutter_meta_window_decorated_notify (MetaWindow *mw,
                                     GParamSpec *arg1,
                                     gpointer    data)
{
  MutterWindow        *self     = MUTTER_WINDOW (data);
  MutterWindowPrivate *priv     = self->priv;
  MetaFrame           *frame    = meta_window_get_frame (mw);
  MetaScreen          *screen   = priv->screen;
  MetaDisplay         *display  = meta_screen_get_display (screen);
  Display             *xdisplay = meta_display_get_xdisplay (display);
  Window               new_xwindow;
  MetaCompScreen      *info;
  XWindowAttributes    attrs;

  /*
   * Basically, we have to reconstruct the the internals of this object
   * from scratch, as everything has changed.
   */
  priv->redecorating = TRUE;

  if (frame)
    new_xwindow = meta_frame_get_xwindow (frame);
  else
    new_xwindow = meta_window_get_xwindow (mw);

  mutter_window_detach (self);

  info = meta_screen_get_compositor_data (screen);

  /*
   * First of all, clean up any resources we are currently using and will
   * be replacing.
   */
  if (priv->damage != None)
    {
      meta_error_trap_push (display);
      XDamageDestroy (xdisplay, priv->damage);
      meta_error_trap_pop (display, FALSE);
      priv->damage = None;
    }

  g_free (priv->desc);
  priv->desc = NULL;

  priv->xwindow = new_xwindow;

  if (!XGetWindowAttributes (xdisplay, new_xwindow, &attrs))
    {
      g_warning ("Could not obtain attributes for window 0x%x after "
                 "decoration change",
                 (guint) new_xwindow);
      return;
    }

  g_object_set (self, "x-window-attributes", &attrs, NULL);

  if (priv->shadow)
    {
      ClutterActor *p = clutter_actor_get_parent (priv->shadow);

      if (CLUTTER_IS_CONTAINER (p))
        clutter_container_remove_actor (CLUTTER_CONTAINER (p), priv->shadow);
      else
        clutter_actor_unparent (priv->shadow);

      priv->shadow = NULL;
    }

  /*
   * Recreate the contents.
   */
  mutter_window_constructed (G_OBJECT (self));
}

static void
mutter_window_constructed (GObject *object)
{
  MutterWindow        *self     = MUTTER_WINDOW (object);
  MutterWindowPrivate *priv     = self->priv;
  MetaScreen          *screen   = priv->screen;
  MetaDisplay         *display  = meta_screen_get_display (screen);
  Window               xwindow  = priv->xwindow;
  Display             *xdisplay = meta_display_get_xdisplay (display);
  XRenderPictFormat   *format;
  MetaCompositor      *compositor;

  compositor = meta_display_get_compositor (display);

  mutter_window_update_window_type (self);

#ifdef HAVE_SHAPE
  /* Listen for ShapeNotify events on the window */
  if (meta_display_has_shape (display))
    XShapeSelectInput (xdisplay, xwindow, ShapeNotifyMask);
#endif

  priv->shaped = is_shaped (display, xwindow);

  if (priv->attrs.class == InputOnly)
    priv->damage = None;
  else
    priv->damage = XDamageCreate (xdisplay, xwindow,
                                  XDamageReportBoundingBox);

  format = XRenderFindVisualFormat (xdisplay, priv->attrs.visual);

  if (format && format->type == PictTypeDirect && format->direct.alphaMask)
    priv->argb32 = TRUE;

  mutter_window_update_opacity (self);

  if (mutter_window_has_shadow (self))
    {
      priv->shadow = mutter_create_shadow_frame (compositor);

      clutter_container_add_actor (CLUTTER_CONTAINER (self), priv->shadow);
    }

  if (!priv->actor)
    {
      priv->actor = mutter_shaped_texture_new ();

      if (!clutter_glx_texture_pixmap_using_extension (
                                  CLUTTER_GLX_TEXTURE_PIXMAP (priv->actor)))
        g_warning ("NOTE: Not using GLX TFP!\n");

      clutter_container_add_actor (CLUTTER_CONTAINER (self), priv->actor);

      /*
       * Since we are holding a pointer to this actor independently of the
       * ClutterContainer internals, and provide a public API to access it,
       * add a reference here, so that if someone is messing about with us
       * via the container interface, we do not end up with a dangling pointer.
       * We will release it in dispose().
       */
      g_object_ref (priv->actor);

      g_signal_connect (priv->window, "notify::decorated",
                        G_CALLBACK (mutter_meta_window_decorated_notify), self);
    }
  else
    {
      /*
       * This is the case where existing window is gaining/loosing frame.
       * Just ensure the actor is top most (i.e., above shadow).
       */
      clutter_actor_raise_top (priv->actor);
    }


  mutter_window_update_shape (self, priv->shaped);
}

static void
mutter_window_dispose (GObject *object)
{
  MutterWindow        *self = MUTTER_WINDOW (object);
  MutterWindowPrivate *priv = self->priv;
  MetaScreen            *screen;
  MetaDisplay           *display;
  Display               *xdisplay;
  MetaCompScreen        *info;

  if (priv->disposed)
    return;

  priv->disposed = TRUE;

  screen   = priv->screen;
  display  = meta_screen_get_display (screen);
  xdisplay = meta_display_get_xdisplay (display);
  info     = meta_screen_get_compositor_data (screen);

  mutter_window_detach (self);

  mutter_window_clear_shape_region (self);
  mutter_window_clear_bounding_region (self);

  if (priv->damage != None)
    {
      meta_error_trap_push (display);
      XDamageDestroy (xdisplay, priv->damage);
      meta_error_trap_pop (display, FALSE);

      priv->damage = None;
    }

  info->windows = g_list_remove (info->windows, (gconstpointer) self);

  /*
   * Release the extra reference we took on the actor.
   */
  g_object_unref (priv->actor);
  priv->actor = NULL;

  G_OBJECT_CLASS (mutter_window_parent_class)->dispose (object);
}

static void
mutter_window_finalize (GObject *object)
{
  MutterWindow        *self = MUTTER_WINDOW (object);
  MutterWindowPrivate *priv = self->priv;

  g_free (priv->desc);

  G_OBJECT_CLASS (mutter_window_parent_class)->finalize (object);
}

static void
mutter_window_set_property (GObject      *object,
                            guint         prop_id,
                            const GValue *value,
                            GParamSpec   *pspec)
{
  MutterWindow        *self   = MUTTER_WINDOW (object);
  MutterWindowPrivate *priv = self->priv;

  switch (prop_id)
    {
    case PROP_MCW_META_WINDOW:
      priv->window = g_value_get_object (value);
      break;
    case PROP_MCW_META_SCREEN:
      priv->screen = g_value_get_pointer (value);
      break;
    case PROP_MCW_X_WINDOW:
      priv->xwindow = g_value_get_ulong (value);
      break;
    case PROP_MCW_X_WINDOW_ATTRIBUTES:
      priv->attrs = *((XWindowAttributes*)g_value_get_boxed (value));
      break;
    case PROP_MCW_NO_SHADOW:
      {
        gboolean oldv = priv->no_shadow ? TRUE : FALSE;
        gboolean newv = g_value_get_boolean (value);

        if (oldv == newv)
          return;

        priv->no_shadow = newv;

        if (newv && priv->shadow)
          {
            clutter_container_remove_actor (CLUTTER_CONTAINER (object),
                                            priv->shadow);
            priv->shadow = NULL;
          }
        else if (!newv && !priv->shadow && mutter_window_has_shadow (self))
          {
            gfloat       w, h;
            MetaDisplay *display = meta_screen_get_display (priv->screen);
            MetaCompositor *compositor;

            compositor = meta_display_get_compositor (display);

            clutter_actor_get_size (CLUTTER_ACTOR (self), &w, &h);

            priv->shadow = mutter_create_shadow_frame (compositor);

            clutter_actor_set_size (priv->shadow, w, h);

            clutter_container_add_actor (CLUTTER_CONTAINER (self), priv->shadow);
          }
      }
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}

static void
mutter_window_get_property (GObject      *object,
                            guint         prop_id,
                            GValue       *value,
                            GParamSpec   *pspec)
{
  MutterWindowPrivate *priv = MUTTER_WINDOW (object)->priv;

  switch (prop_id)
    {
    case PROP_MCW_META_WINDOW:
      g_value_set_object (value, priv->window);
      break;
    case PROP_MCW_META_SCREEN:
      g_value_set_pointer (value, priv->screen);
      break;
    case PROP_MCW_X_WINDOW:
      g_value_set_ulong (value, priv->xwindow);
      break;
    case PROP_MCW_X_WINDOW_ATTRIBUTES:
      g_value_set_boxed (value, &priv->attrs);
      break;
    case PROP_MCW_NO_SHADOW:
      g_value_set_boolean (value, priv->no_shadow);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}

void
mutter_window_update_window_type (MutterWindow *self)
{
  MutterWindowPrivate *priv = self->priv;
  priv->type = (MetaCompWindowType) meta_window_get_window_type (priv->window);
}

static gboolean
is_shaped (MetaDisplay *display, Window xwindow)
{
  Display *xdisplay = meta_display_get_xdisplay (display);
  gint     xws, yws, xbs, ybs;
  guint    wws, hws, wbs, hbs;
  gint     bounding_shaped, clip_shaped;

  if (meta_display_has_shape (display))
    {
      XShapeQueryExtents (xdisplay, xwindow, &bounding_shaped,
                          &xws, &yws, &wws, &hws, &clip_shaped,
                          &xbs, &ybs, &wbs, &hbs);
      return (bounding_shaped != 0);
    }

  return FALSE;
}

static gboolean
mutter_window_has_shadow (MutterWindow *self)
{
  MutterWindowPrivate * priv = self->priv;

  if (priv->no_shadow)
    return FALSE;

  /*
   * Always put a shadow around windows with a frame - This should override
   * the restriction about not putting a shadow around shaped windows
   * as the frame might be the reason the window is shaped
   */
  if (priv->window)
    {
      if (meta_window_get_frame (priv->window))
	{
	  meta_verbose ("Window 0x%x has shadow because it has a frame\n",
			(guint)priv->xwindow);
	  return TRUE;
	}
    }

  /*
   * Do not add shadows to ARGB windows (since they are probably transparent)
   */
  if (priv->argb32 || priv->opacity != 0xff)
    {
      meta_verbose ("Window 0x%x has no shadow as it is ARGB\n",
		    (guint)priv->xwindow);
      return FALSE;
    }

  /*
   * Never put a shadow around shaped windows
   */
  if (priv->shaped)
    {
      meta_verbose ("Window 0x%x has no shadow as it is shaped\n",
		    (guint)priv->xwindow);
      return FALSE;
    }

  /*
   * Add shadows to override redirect windows (e.g., Gtk menus).
   * This must have lower priority than window shape test.
   */
  if (priv->attrs.override_redirect)
    {
      meta_verbose ("Window 0x%x has shadow because it is override redirect.\n",
		    (guint)priv->xwindow);
      return TRUE;
    }

  /*
   * Don't put shadow around DND icon windows
   */
  if (priv->type == META_COMP_WINDOW_DND ||
      priv->type == META_COMP_WINDOW_DESKTOP)
    {
      meta_verbose ("Window 0x%x has no shadow as it is DND or Desktop\n",
		    (guint)priv->xwindow);
      return FALSE;
    }

  if (priv->type == META_COMP_WINDOW_MENU
#if 0
      || priv->type == META_COMP_WINDOW_DROPDOWN_MENU
#endif
      )
    {
      meta_verbose ("Window 0x%x has shadow as it is a menu\n",
		    (guint)priv->xwindow);
      return TRUE;
    }

#if 0
  if (priv->type == META_COMP_WINDOW_TOOLTIP)
    {
      meta_verbose ("Window 0x%x has shadow as it is a tooltip\n",
		    (guint)priv->xwindow);
      return TRUE;
    }
#endif

  meta_verbose ("Window 0x%x has no shadow as it fell through\n",
		(guint)priv->xwindow);
  return FALSE;
}

Window
mutter_window_get_x_window (MutterWindow *self)
{
  if (!self)
    return None;

  return self->priv->xwindow;
}

/**
 * mutter_window_get_meta_window:
 *
 * Gets the MetaWindow object that the the MutterWindow is displaying
 *
 * Return value: (transfer none): the displayed MetaWindow
 */
MetaWindow *
mutter_window_get_meta_window (MutterWindow *self)
{
  return self->priv->window;
}

/**
 * mutter_window_get_texture:
 *
 * Gets the ClutterActor that is used to display the contents of the window
 *
 * Return value: (transfer none): the ClutterActor for the contents
 */
ClutterActor *
mutter_window_get_texture (MutterWindow *self)
{
  return self->priv->actor;
}

MetaCompWindowType
mutter_window_get_window_type (MutterWindow *self)
{
  if (!self)
    return 0;

  return self->priv->type;
}

gboolean
mutter_window_is_override_redirect (MutterWindow *self)
{
  return meta_window_is_override_redirect (self->priv->window);
}

const char *mutter_window_get_description (MutterWindow *self)
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
 * mutter_window_get_workspace:
 * @self: #MutterWindow
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
mutter_window_get_workspace (MutterWindow *self)
{
  MutterWindowPrivate *priv;
  MetaWorkspace       *workspace;

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
mutter_window_showing_on_its_workspace (MutterWindow *self)
{
  if (!self)
    return FALSE;

  /* If override redirect: */
  if (!self->priv->window)
    return TRUE;

  return meta_window_showing_on_its_workspace (self->priv->window);
}

static void
mutter_window_freeze (MutterWindow *self)
{
  self->priv->freeze_count++;
}

static void
mutter_window_damage_all (MutterWindow *self)
{
  MutterWindowPrivate *priv = self->priv;
  ClutterX11TexturePixmap *texture_x11 = CLUTTER_X11_TEXTURE_PIXMAP (priv->actor);
  guint pixmap_width = 0;
  guint pixmap_height = 0;

  if (!priv->needs_damage_all)
    return;

  g_object_get (texture_x11,
                "pixmap-width", &pixmap_width,
                "pixmap-height", &pixmap_height,
                NULL);

  clutter_x11_texture_pixmap_update_area (texture_x11,
                                          0,
                                          0,
                                          pixmap_width,
                                          pixmap_height);

  priv->needs_damage_all = FALSE;
}

static void
mutter_window_thaw (MutterWindow *self)
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

  /* Since we ignore damage events while a window is frozen for certain effects
   * we may need to issue an update_area() covering the whole pixmap if we
   * don't know what real damage has happened. */

  if (self->priv->needs_damage_all)
    mutter_window_damage_all (self);
}

gboolean
mutter_window_effect_in_progress (MutterWindow *self)
{
  return (self->priv->minimize_in_progress ||
	  self->priv->maximize_in_progress ||
	  self->priv->unmaximize_in_progress ||
	  self->priv->map_in_progress ||
	  self->priv->destroy_in_progress);
}

static void
mutter_window_queue_create_pixmap (MutterWindow *self)
{
  MutterWindowPrivate *priv = self->priv;

  priv->needs_pixmap = TRUE;

  if (!priv->mapped)
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
  case MUTTER_PLUGIN_DESTROY:
  case MUTTER_PLUGIN_MAXIMIZE:
  case MUTTER_PLUGIN_UNMAXIMIZE:
    return TRUE;
    break;
  default:
    return FALSE;
  }
}

static gboolean
start_simple_effect (MutterWindow *self,
                     gulong        event)
{
  MutterWindowPrivate *priv = self->priv;
  MetaCompScreen *info = meta_screen_get_compositor_data (priv->screen);
  gint *counter = NULL;
  gboolean use_freeze_thaw = FALSE;

  if (!info->plugin_mgr)
    return FALSE;

  switch (event)
  {
  case MUTTER_PLUGIN_MINIMIZE:
    counter = &priv->minimize_in_progress;
    break;
  case MUTTER_PLUGIN_MAP:
    counter = &priv->map_in_progress;
    break;
  case MUTTER_PLUGIN_DESTROY:
    counter = &priv->destroy_in_progress;
    break;
  case MUTTER_PLUGIN_UNMAXIMIZE:
  case MUTTER_PLUGIN_MAXIMIZE:
  case MUTTER_PLUGIN_SWITCH_WORKSPACE:
    g_assert_not_reached ();
    break;
  }

  g_assert (counter);

  use_freeze_thaw = is_freeze_thaw_effect (event);

  if (use_freeze_thaw)
    mutter_window_freeze (self);

  (*counter)++;

  if (!mutter_plugin_manager_event_simple (info->plugin_mgr,
                                           self,
                                           event))
    {
      (*counter)--;
      if (use_freeze_thaw)
        mutter_window_thaw (self);
      return FALSE;
    }

  return TRUE;
}

static void
mutter_window_after_effects (MutterWindow *self)
{
  MutterWindowPrivate *priv = self->priv;

  if (priv->needs_destroy)
    {
      clutter_actor_destroy (CLUTTER_ACTOR (self));
      return;
    }

  mutter_window_sync_visibility (self);
  mutter_window_sync_actor_position (self);

  if (!meta_window_is_mapped (priv->window))
    mutter_window_detach (self);

  if (priv->needs_pixmap)
    clutter_actor_queue_redraw (priv->actor);
}

void
mutter_window_effect_completed (MutterWindow *self,
				gulong        event)
{
  MutterWindowPrivate *priv   = self->priv;

  /* NB: Keep in mind that when effects get completed it possible
   * that the corresponding MetaWindow may have be been destroyed.
   * In this case priv->window will == NULL */

  switch (event)
  {
  case MUTTER_PLUGIN_MINIMIZE:
    {
      priv->minimize_in_progress--;
      if (priv->minimize_in_progress < 0)
	{
	  g_warning ("Error in minimize accounting.");
	  priv->minimize_in_progress = 0;
	}
    }
    break;
  case MUTTER_PLUGIN_MAP:
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
  case MUTTER_PLUGIN_DESTROY:
    priv->destroy_in_progress--;

    if (priv->destroy_in_progress < 0)
      {
	g_warning ("Error in destroy accounting.");
	priv->destroy_in_progress = 0;
      }
    break;
  case MUTTER_PLUGIN_UNMAXIMIZE:
    priv->unmaximize_in_progress--;
    if (priv->unmaximize_in_progress < 0)
      {
	g_warning ("Error in unmaximize accounting.");
	priv->unmaximize_in_progress = 0;
      }
    break;
  case MUTTER_PLUGIN_MAXIMIZE:
    priv->maximize_in_progress--;
    if (priv->maximize_in_progress < 0)
      {
	g_warning ("Error in maximize accounting.");
	priv->maximize_in_progress = 0;
      }
    break;
  case MUTTER_PLUGIN_SWITCH_WORKSPACE:
    g_assert_not_reached ();
    break;
  }

  if (is_freeze_thaw_effect (event))
    mutter_window_thaw (self);

  if (!mutter_window_effect_in_progress (self))
    mutter_window_after_effects (self);
}

/* Called to drop our reference to a window backing pixmap that we
 * previously obtained with XCompositeNameWindowPixmap. We do this
 * when the window is unmapped or when we want to update to a new
 * pixmap for a new size.
 */
static void
mutter_window_detach (MutterWindow *self)
{
  MutterWindowPrivate *priv     = self->priv;
  MetaScreen            *screen   = priv->screen;
  MetaDisplay           *display  = meta_screen_get_display (screen);
  Display               *xdisplay = meta_display_get_xdisplay (display);

  if (!priv->back_pixmap)
    return;

  XFreePixmap (xdisplay, priv->back_pixmap);
  priv->back_pixmap = None;
  clutter_x11_texture_pixmap_set_pixmap (CLUTTER_X11_TEXTURE_PIXMAP (priv->actor),
                                         None);

  mutter_window_queue_create_pixmap (self);
}

void
mutter_window_destroy (MutterWindow *self)
{
  MetaWindow	      *window;
  MetaCompScreen      *info;
  MutterWindowPrivate *priv;

  priv = self->priv;

  window = priv->window;
  meta_window_set_compositor_private (window, NULL);

  /*
   * We remove the window from internal lookup hashes and thus any other
   * unmap events etc fail
   */
  info = meta_screen_get_compositor_data (priv->screen);
  info->windows = g_list_remove (info->windows, (gconstpointer) self);

  if (priv->type == META_COMP_WINDOW_DROPDOWN_MENU ||
      priv->type == META_COMP_WINDOW_POPUP_MENU ||
      priv->type == META_COMP_WINDOW_TOOLTIP ||
      priv->type == META_COMP_WINDOW_NOTIFICATION ||
      priv->type == META_COMP_WINDOW_COMBO ||
      priv->type == META_COMP_WINDOW_DND ||
      priv->type == META_COMP_WINDOW_OVERRIDE_OTHER)
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

  if (!mutter_window_effect_in_progress (self))
    clutter_actor_destroy (CLUTTER_ACTOR (self));
}

void
mutter_window_sync_actor_position (MutterWindow *self)
{
  MutterWindowPrivate *priv = self->priv;
  MetaRectangle window_rect;

  meta_window_get_outer_rect (priv->window, &window_rect);

  if (priv->attrs.width != window_rect.width ||
      priv->attrs.height != window_rect.height)
    {
      priv->size_changed = TRUE;
      mutter_window_queue_create_pixmap (self);
    }

  /* XXX deprecated: please use meta_window_get_outer_rect instead */
  priv->attrs.width = window_rect.width;
  priv->attrs.height = window_rect.height;
  priv->attrs.x = window_rect.x;
  priv->attrs.y = window_rect.y;

  if (mutter_window_effect_in_progress (self))
    return;

  clutter_actor_set_position (CLUTTER_ACTOR (self),
                              window_rect.x, window_rect.y);
  clutter_actor_set_size (CLUTTER_ACTOR (self),
                          window_rect.width, window_rect.height);
}

void
mutter_window_show (MutterWindow   *self,
                    MetaCompEffect  effect)
{
  MutterWindowPrivate *priv;
  MetaCompScreen      *info;
  gulong               event;

  priv = self->priv;
  info = meta_screen_get_compositor_data (priv->screen);

  g_return_if_fail (!priv->visible);

  self->priv->visible = TRUE;

  event = 0;
  switch (effect)
    {
    case META_COMP_EFFECT_CREATE:
      event = MUTTER_PLUGIN_MAP;
      break;
    case META_COMP_EFFECT_UNMINIMIZE:
      /* FIXME: should have MUTTER_PLUGIN_UNMINIMIZE */
      event = MUTTER_PLUGIN_MAP;
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
      clutter_actor_show_all (CLUTTER_ACTOR (self));
      priv->redecorating = FALSE;
    }
}

void
mutter_window_hide (MutterWindow  *self,
                    MetaCompEffect effect)
{
  MutterWindowPrivate *priv;
  MetaCompScreen      *info;
  gulong               event;

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
      event = MUTTER_PLUGIN_DESTROY;
      break;
    case META_COMP_EFFECT_MINIMIZE:
      event = MUTTER_PLUGIN_MINIMIZE;
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
mutter_window_maximize (MutterWindow       *self,
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
  mutter_window_freeze (self);

  if (!info->plugin_mgr ||
      !mutter_plugin_manager_event_maximize (info->plugin_mgr,
					     self,
					     MUTTER_PLUGIN_MAXIMIZE,
					     new_rect->x, new_rect->y,
					     new_rect->width, new_rect->height))

    {
      self->priv->maximize_in_progress--;
      mutter_window_thaw (self);
    }
}

void
mutter_window_unmaximize (MutterWindow      *self,
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
  mutter_window_freeze (self);

  if (!info->plugin_mgr ||
      !mutter_plugin_manager_event_maximize (info->plugin_mgr,
					     self,
					     MUTTER_PLUGIN_UNMAXIMIZE,
					     new_rect->x, new_rect->y,
					     new_rect->width, new_rect->height))
    {
      self->priv->unmaximize_in_progress--;
      mutter_window_thaw (self);
    }
}

MutterWindow *
mutter_window_new (MetaWindow *window)
{
  MetaScreen		*screen = meta_window_get_screen (window);
  MetaDisplay           *display = meta_screen_get_display (screen);
  MetaCompScreen        *info = meta_screen_get_compositor_data (screen);
  MutterWindow          *self;
  MutterWindowPrivate   *priv;
  MetaFrame		*frame;
  Window		 top_window;
  XWindowAttributes	 attrs;

  frame = meta_window_get_frame (window);
  if (frame)
    top_window = meta_frame_get_xwindow (frame);
  else
    top_window = meta_window_get_xwindow (window);

  meta_verbose ("add window: Meta %p, xwin 0x%x\n", window, (guint)top_window);

  /* FIXME: Remove the redundant data we store in self->priv->attrs, and
   * simply query metacity core for the data. */
  if (!XGetWindowAttributes (meta_display_get_xdisplay (display), top_window, &attrs))
    return NULL;

  self = g_object_new (MUTTER_TYPE_COMP_WINDOW,
		     "meta-window",         window,
		     "x-window",            top_window,
		     "meta-screen",         screen,
		     "x-window-attributes", &attrs,
		     NULL);

  priv = self->priv;

  priv->mapped = meta_window_toplevel_is_mapped (priv->window);
  if (priv->mapped)
    mutter_window_queue_create_pixmap (self);

  mutter_window_sync_actor_position (self);

  /* Hang our compositor window state off the MetaWindow for fast retrieval */
  meta_window_set_compositor_private (window, G_OBJECT (self));

  clutter_container_add_actor (CLUTTER_CONTAINER (info->window_group),
			       CLUTTER_ACTOR (self));
  clutter_actor_hide (CLUTTER_ACTOR (self));

  /* Initial position in the stack is arbitrary; stacking will be synced
   * before we first paint.
   */
  info->windows = g_list_append (info->windows, self);

  return self;
}

void
mutter_window_mapped (MutterWindow *self)
{
  MutterWindowPrivate *priv = self->priv;

  g_return_if_fail (!priv->mapped);

  priv->mapped = TRUE;

  mutter_window_queue_create_pixmap (self);
}

void
mutter_window_unmapped (MutterWindow *self)
{
  MutterWindowPrivate *priv = self->priv;

  g_return_if_fail (priv->mapped);

  priv->mapped = FALSE;

  if (mutter_window_effect_in_progress (self))
    return;

  mutter_window_detach (self);
  priv->needs_pixmap = FALSE;
}

static void
mutter_window_clear_shape_region (MutterWindow *self)
{
  MutterWindowPrivate *priv = self->priv;

  if (priv->shape_region)
    {
      meta_region_destroy (priv->shape_region);
      priv->shape_region = NULL;
    }
}

static void
mutter_window_clear_bounding_region (MutterWindow *self)
{
  MutterWindowPrivate *priv = self->priv;

  if (priv->bounding_region)
    {
      meta_region_destroy (priv->bounding_region);
      priv->bounding_region = NULL;
    }
}

static void
mutter_window_update_bounding_region (MutterWindow *self,
                                      int           width,
                                      int           height)
{
  MutterWindowPrivate *priv = self->priv;
  GdkRectangle bounding_rectangle = { 0, 0, width, height };

  mutter_window_clear_bounding_region (self);

  priv->bounding_region = meta_region_new_from_rectangle (&bounding_rectangle);
}

static void
mutter_window_update_shape_region (MutterWindow *self,
                                   int           n_rects,
                                   XRectangle   *rects)
{
  MutterWindowPrivate *priv = self->priv;
  int i;

  mutter_window_clear_shape_region (self);

  priv->shape_region = meta_region_new ();
  for (i = 0; i < n_rects; i++)
    {
      GdkRectangle rect = { rects[i].x, rects[i].y, rects[i].width, rects[i].height };
      meta_region_union_rectangle (priv->shape_region, &rect);
    }
}

/**
 * mutter_window_get_obscured_region:
 * @self: a #MutterWindow
 *
 * Gets the region that is completely obscured by the window. Coordinates
 * are relative to the upper-left of the window.
 *
 * Return value: (transfer none): the area obscured by the window,
 *  %NULL is the same as an empty region.
 */
MetaRegion *
mutter_window_get_obscured_region (MutterWindow *self)
{
  MutterWindowPrivate *priv = self->priv;

  if (!priv->argb32 && priv->back_pixmap)
    {
      if (priv->shaped)
        return priv->shape_region;
      else
        return priv->bounding_region;
    }
  else
    return NULL;
}

#if 0
/* Print out a region; useful for debugging */
static void
dump_region (MetaRegion *region)
{
  GdkRectangle *rects;
  int n_rects;
  int i;

  meta_region_get_rectangles (region, &rects, &n_rects);
  g_print ("[");
  for (i = 0; i < n_rects; i++)
    {
      g_print ("+%d+%dx%dx%d ",
               rects[i].x, rects[i].y, rects[i].width, rects[i].height);
    }
  g_print ("]\n");
  g_free (rects);
}
#endif

/**
 * mutter_window_set_visible_region:
 * @self: a #MutterWindow
 * @visible_region: the region of the screen that isn't completely
 *  obscured.
 *
 * Provides a hint as to what areas of the window need to be
 * drawn. Regions not in @visible_region are completely obscured.
 * This will be set before painting then unset afterwards.
 */
void
mutter_window_set_visible_region (MutterWindow *self,
                                  MetaRegion   *visible_region)
{
  MutterWindowPrivate *priv = self->priv;
  MetaRegion *texture_clip_region = NULL;

  /* Get the area of the window texture that would be drawn if
   * we weren't obscured at all
   */
  if (priv->shaped)
    {
      if (priv->shape_region)
        texture_clip_region = meta_region_copy (priv->shape_region);
    }
  else
    {
      if (priv->bounding_region)
        texture_clip_region = meta_region_copy (priv->bounding_region);
    }

  if (!texture_clip_region)
    texture_clip_region = meta_region_new ();

  /* Then intersect that with the visible region to get the region
   * that we actually need to redraw.
   */
  meta_region_intersect (texture_clip_region, visible_region);

  /* Assumes ownership */
  mutter_shaped_texture_set_clip_region (MUTTER_SHAPED_TEXTURE (priv->actor),
                                         texture_clip_region);
}

/**
 * mutter_window_set_visible_region_beneath:
 * @self: a #MutterWindow
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
mutter_window_set_visible_region_beneath (MutterWindow *self,
                                          MetaRegion    *beneath_region)
{
  MutterWindowPrivate *priv = self->priv;

  if (priv->shadow)
    {
      GdkRectangle shadow_rect;
      ClutterActorBox box;
      MetaOverlapType overlap;

      /* We could compute an full clip region as we do for the window
       * texture, but the shadow is relatively cheap to draw, and
       * a little more complex to clip, so we just catch the case where
       * the shadow is completely obscured and doesn't need to be drawn
       * at all.
       */
      clutter_actor_get_allocation_box (priv->shadow, &box);

      shadow_rect.x = roundf (box.x1);
      shadow_rect.y = roundf (box.y1);
      shadow_rect.width = roundf (box.x2 - box.x1);
      shadow_rect.height = roundf (box.y2 - box.y1);

      overlap = meta_region_contains_rectangle (beneath_region, &shadow_rect);

      tidy_texture_frame_set_needs_paint (TIDY_TEXTURE_FRAME (priv->shadow),
                                          overlap != META_REGION_OVERLAP_OUT);
    }
}

/**
 * mutter_window_reset_visible_regions:
 * @self: a #MutterWindow
 *
 * Unsets the regions set by mutter_window_reset_visible_region() and
 *mutter_window_reset_visible_region_beneath()
 */
void
mutter_window_reset_visible_regions (MutterWindow *self)
{
  MutterWindowPrivate *priv = self->priv;

  mutter_shaped_texture_set_clip_region (MUTTER_SHAPED_TEXTURE (priv->actor),
                                         NULL);
  if (priv->shadow)
    tidy_texture_frame_set_needs_paint (TIDY_TEXTURE_FRAME (priv->shadow), TRUE);
}

static void
check_needs_pixmap (MutterWindow *self)
{
  MutterWindowPrivate *priv     = self->priv;
  MetaScreen          *screen   = priv->screen;
  MetaDisplay         *display  = meta_screen_get_display (screen);
  Display             *xdisplay = meta_display_get_xdisplay (display);
  MetaCompScreen      *info     = meta_screen_get_compositor_data (screen);
  MetaCompositor      *compositor;
  Window               xwindow  = priv->xwindow;
  gboolean             full     = FALSE;

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
      mutter_window_detach (self);
      priv->size_changed = FALSE;
    }

  meta_error_trap_push (display);

  if (priv->back_pixmap == None)
    {
      gint pxm_width, pxm_height;

      meta_error_trap_push (display);

      priv->back_pixmap = XCompositeNameWindowPixmap (xdisplay, xwindow);

      if (meta_error_trap_pop_with_return (display, FALSE) != Success)
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
          mutter_window_update_bounding_region (self, 0, 0);
          return;
        }

      /* MUST call before setting pixmap or serious performance issues
       * seemingly caused by cogl_texture_set_filters() in set_filter
       * Not sure if that call is actually needed.
       */
      if (!compositor->no_mipmaps)
        clutter_texture_set_filter_quality (CLUTTER_TEXTURE (priv->actor),
                                            CLUTTER_TEXTURE_QUALITY_HIGH );

      clutter_x11_texture_pixmap_set_pixmap
                       (CLUTTER_X11_TEXTURE_PIXMAP (priv->actor),
                        priv->back_pixmap);

      g_object_get (priv->actor,
                    "pixmap-width", &pxm_width,
                    "pixmap-height", &pxm_height,
                    NULL);

      if (priv->shadow)
        clutter_actor_set_size (priv->shadow, pxm_width, pxm_height);

      mutter_window_update_bounding_region (self, pxm_width, pxm_height);

      full = TRUE;
    }

  meta_error_trap_pop (display, FALSE);

  priv->needs_pixmap = FALSE;
}

static gboolean
is_frozen (MutterWindow *self)
{
  return self->priv->freeze_count ? TRUE : FALSE;
}

void
mutter_window_process_damage (MutterWindow       *self,
			      XDamageNotifyEvent *event)
{
  MutterWindowPrivate *priv = self->priv;
  ClutterX11TexturePixmap *texture_x11 = CLUTTER_X11_TEXTURE_PIXMAP (priv->actor);

  priv->received_damage = TRUE;

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


  clutter_x11_texture_pixmap_update_area (texture_x11,
                                          event->area.x,
                                          event->area.y,
                                          event->area.width,
                                          event->area.height);
}

void
mutter_window_sync_visibility (MutterWindow *self)
{
  MutterWindowPrivate *priv = self->priv;

  if (CLUTTER_ACTOR_IS_VISIBLE (self) != priv->visible)
    {
      if (priv->visible)
        clutter_actor_show (CLUTTER_ACTOR (self));
      else
        clutter_actor_hide (CLUTTER_ACTOR (self));
    }
}

static void
check_needs_reshape (MutterWindow *self)
{
  MutterWindowPrivate *priv = self->priv;

  if (!priv->needs_reshape)
    return;

  mutter_shaped_texture_clear_rectangles (MUTTER_SHAPED_TEXTURE (priv->actor));
  mutter_window_clear_shape_region (self);

#ifdef HAVE_SHAPE
  if (priv->shaped)
    {
      Display *xdisplay = meta_display_get_xdisplay (meta_window_get_display (priv->window));
      XRectangle *rects;
      int n_rects, ordering;

      rects = XShapeGetRectangles (xdisplay,
                                   priv->xwindow,
                                   ShapeBounding,
                                   &n_rects,
                                   &ordering);

      if (rects)
        {
          mutter_shaped_texture_add_rectangles (MUTTER_SHAPED_TEXTURE (priv->actor),
                                              n_rects, rects);

          mutter_window_update_shape_region (self, n_rects, rects);

          XFree (rects);
        }
    }
#endif

  priv->needs_reshape = FALSE;
}

void
mutter_window_update_shape (MutterWindow   *self,
                            gboolean        shaped)
{
  MutterWindowPrivate *priv = self->priv;

  priv->shaped = shaped;
  priv->needs_reshape = TRUE;

  clutter_actor_queue_redraw (priv->actor);
}

void
mutter_window_pre_paint (MutterWindow *self)
{
  MutterWindowPrivate *priv = self->priv;
  MetaScreen          *screen   = priv->screen;
  MetaDisplay         *display  = meta_screen_get_display (screen);
  Display             *xdisplay = meta_display_get_xdisplay (display);

  if (is_frozen (self))
    {
      /* The window is frozen due to a pending animation: we'll wait until
       * the animation finishes to reshape and repair the window */
      return;
    }

  if (priv->received_damage)
    {
      XDamageSubtract (xdisplay, priv->damage, None, None);
      priv->received_damage = FALSE;
    }

  check_needs_reshape (self);
  check_needs_pixmap (self);
}

void
mutter_window_update_opacity (MutterWindow *self)
{
  MutterWindowPrivate *priv = self->priv;
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
  clutter_actor_set_opacity (CLUTTER_ACTOR (self), opacity);
}
