/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */

/*
 * X Wayland Support
 *
 * Copyright (C) 2013 Intel Corporation
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

#include "config.h"

#include "meta-xwayland.h"
#include "meta-xwayland-private.h"

#include <glib.h>
#include <glib-unix.h>
#include <errno.h>
#include <sys/socket.h>
#include <sys/un.h>

static void
associate_window_with_surface (MetaWindow         *window,
                               MetaWaylandSurface *surface)
{
  MetaDisplay *display = window->display;

  /* If the window has an existing surface, like if we're
   * undecorating or decorating the window, then we need
   * to detach the window from its old surface.
   */
  if (window->surface)
    window->surface->window = NULL;

  meta_wayland_surface_set_window (surface, window);
  window->surface = surface;

  meta_compositor_window_surface_changed (display->compositor, window);

  /* Now that we have a surface check if it should have focus. */
  meta_display_sync_wayland_input_focus (display);
}

static gboolean
associate_window_with_surface_id (MetaXWaylandManager *manager,
                                  MetaWindow          *window,
                                  guint32              surface_id)
{
  struct wl_resource *resource;

  resource = wl_client_get_object (manager->client, surface_id);
  if (resource)
    {
      MetaWaylandSurface *surface = wl_resource_get_user_data (resource);
      associate_window_with_surface (window, surface);
      return TRUE;
    }
  else
    return FALSE;
}

typedef struct {
  MetaXWaylandManager *manager;
  MetaWindow *window;
  guint32 surface_id;
  guint later_id;
} AssociateWindowWithSurfaceOp;

static void associate_window_with_surface_window_unmanaged (MetaWindow                   *window,
                                                            AssociateWindowWithSurfaceOp *op);
static void
associate_window_with_surface_op_free (AssociateWindowWithSurfaceOp *op)
{
  if (op->later_id != 0)
    meta_later_remove (op->later_id);
  g_signal_handlers_disconnect_by_func (op->window,
                                        (gpointer) associate_window_with_surface_window_unmanaged,
                                        op);
  g_free (op);
}

static void
associate_window_with_surface_window_unmanaged (MetaWindow                   *window,
                                                AssociateWindowWithSurfaceOp *op)
{
  associate_window_with_surface_op_free (op);
}

static gboolean
associate_window_with_surface_later (gpointer user_data)
{
  AssociateWindowWithSurfaceOp *op = user_data;

  op->later_id = 0;

  if (!associate_window_with_surface_id (op->manager, op->window, op->surface_id))
    {
      /* Not here? Oh well... nothing we can do */
      g_warning ("Unknown surface ID %d (from window %s)", op->surface_id, op->window->desc);
    }

  associate_window_with_surface_op_free (op);

  return G_SOURCE_REMOVE;
}

void
meta_xwayland_handle_wl_surface_id (MetaWindow *window,
                                    guint32     surface_id)
{
  MetaWaylandCompositor *compositor = meta_wayland_compositor_get_default ();
  MetaXWaylandManager *manager = &compositor->xwayland_manager;

  if (!associate_window_with_surface_id (manager, window, surface_id))
    {
      /* No surface ID yet... it should arrive after the next
       * iteration through the loop, so queue a later and see
       * what happens.
       */
      AssociateWindowWithSurfaceOp *op = g_new0 (AssociateWindowWithSurfaceOp, 1);
      op->manager = manager;
      op->window = window;
      op->surface_id = surface_id;
      op->later_id = meta_later_add (META_LATER_BEFORE_REDRAW,
                                     associate_window_with_surface_later,
                                     op,
                                     NULL);

      g_signal_connect (op->window, "unmanaged",
                        G_CALLBACK (associate_window_with_surface_window_unmanaged), op);
    }
}

static gboolean
try_display (int    display,
             char **filename_out,
             int   *fd_out)
{
  gboolean ret = FALSE;
  char *filename;
  int fd;

  filename = g_strdup_printf ("/tmp/.X%d-lock", display);

 again:
  fd = open (filename, O_WRONLY | O_CLOEXEC | O_CREAT | O_EXCL, 0444);

  if (fd < 0 && errno == EEXIST)
    {
      char pid[11];
      char *end;
      pid_t other;

      fd = open (filename, O_CLOEXEC, O_RDONLY);
      if (fd < 0 || read (fd, pid, 11) != 11)
        {
          g_warning ("can't read lock file %s: %m", filename);
          goto out;
        }
      close (fd);
      fd = -1;

      other = strtol (pid, &end, 0);
      if (end != pid + 10)
        {
          g_warning ("can't parse lock file %s", filename);
          goto out;
        }

      if (kill (other, 0) < 0 && errno == ESRCH)
        {
          /* Process is dead. Try unlinking the lockfile and trying again. */
          if (unlink (filename) < 0)
            {
              g_warning ("failed to unlink stale lock file %s: %m", filename);
              goto out;
            }

          goto again;
        }

      goto out;
    }
  else if (fd < 0)
    {
      g_warning ("failed to create lock file %s: %m", filename);
      goto out;
    }

  ret = TRUE;

 out:
  if (!ret)
    {
      g_free (filename);
      filename = NULL;

      if (fd >= 0)
        {
          close (fd);
          fd = -1;
        }
    }

  *filename_out = filename;
  *fd_out = fd;
  return ret;
}

static char *
create_lockfile (int display, int *display_out)
{
  char *filename;
  int fd;

  char pid[11];
  int size;

  while (!try_display (display, &filename, &fd))
    {
      display++;

      /* If display is above 50, then something's wrong. Just
       * abort in this case. */
      if (display > 50)
        return NULL;
    }

  /* Subtle detail: we use the pid of the wayland compositor, not the xserver
   * in the lock file. */
  size = snprintf (pid, 11, "%10d\n", getpid ());
  if (size != 11 || write (fd, pid, 11) != 11)
    {
      unlink (filename);
      close (fd);
      g_warning ("failed to write pid to lock file %s", filename);
      g_free (filename);
      return NULL;
    }

  close (fd);

  *display_out = display;
  return filename;
}

static int
bind_to_abstract_socket (int display)
{
  struct sockaddr_un addr;
  socklen_t size, name_size;
  int fd;

  fd = socket (PF_LOCAL, SOCK_STREAM | SOCK_CLOEXEC, 0);
  if (fd < 0)
    return -1;

  addr.sun_family = AF_LOCAL;
  name_size = snprintf (addr.sun_path, sizeof addr.sun_path,
                        "%c/tmp/.X11-unix/X%d", 0, display);
  size = offsetof (struct sockaddr_un, sun_path) + name_size;
  if (bind (fd, (struct sockaddr *) &addr, size) < 0)
    {
      g_warning ("failed to bind to @%s: %m", addr.sun_path + 1);
      close (fd);
      return -1;
    }

  if (listen (fd, 1) < 0)
    {
      close (fd);
      return -1;
    }

  return fd;
}

static int
bind_to_unix_socket (int display)
{
  struct sockaddr_un addr;
  socklen_t size, name_size;
  int fd;

  fd = socket (PF_LOCAL, SOCK_STREAM | SOCK_CLOEXEC, 0);
  if (fd < 0)
    return -1;

  addr.sun_family = AF_LOCAL;
  name_size = snprintf (addr.sun_path, sizeof addr.sun_path,
                        "/tmp/.X11-unix/X%d", display) + 1;
  size = offsetof (struct sockaddr_un, sun_path) + name_size;
  unlink (addr.sun_path);
  if (bind (fd, (struct sockaddr *) &addr, size) < 0)
    {
      g_warning ("failed to bind to %s: %m\n", addr.sun_path);
      close (fd);
      return -1;
    }

  if (listen (fd, 1) < 0)
    {
      unlink (addr.sun_path);
      close (fd);
      return -1;
    }

  return fd;
}

static void
xserver_died (GPid     pid,
              gint     status,
              gpointer user_data)
{
  if (!WIFEXITED (status))
    g_error ("X Wayland crashed; aborting");
  else
    {
      /* For now we simply abort if we see the server exit.
       *
       * In the future X will only be loaded lazily for legacy X support
       * but for now it's a hard requirement. */
      g_error ("Spurious exit of X Wayland server");
    }
}

static int
x_io_error (Display *display)
{
  g_error ("Connection to xwayland lost");

  return 0;
}

static gboolean
choose_xdisplay (MetaXWaylandManager *manager)
{
  int display = 0;
  char *lockfile = NULL;

  do
    {
      lockfile = create_lockfile (display, &display);
      if (!lockfile)
        {
          g_warning ("Failed to create an X lock file");
          return FALSE;
        }

      manager->abstract_fd = bind_to_abstract_socket (display);
      if (manager->abstract_fd < 0)
        {
          unlink (lockfile);

          if (errno == EADDRINUSE)
            {
              display++;
              continue;
            }
          else
            return FALSE;
        }

      manager->unix_fd = bind_to_unix_socket (display);
      if (manager->abstract_fd < 0)
        {
          unlink (lockfile);
          close (manager->abstract_fd);
          return FALSE;
        }

      break;
    }
  while (1);

  manager->display_index = display;
  manager->display_name = g_strdup_printf (":%d", manager->display_index);
  manager->lockfile = lockfile;

  return TRUE;
}

static void
xserver_finished_init (MetaXWaylandManager *manager)
{
  /* At this point xwayland is all setup to start accepting
   * connections so we can quit the transient initialization mainloop
   * and unblock meta_wayland_init() to continue initializing mutter.
   * */
  g_main_loop_quit (manager->init_loop);
  g_clear_pointer (&manager->init_loop, g_main_loop_unref);
}

static gboolean
on_displayfd_ready (int          fd,
                    GIOCondition condition,
                    gpointer     user_data)
{
  MetaXWaylandManager *manager = user_data;

  /* The server writes its display name to the displayfd
   * socket when it's ready. We don't care about the data
   * in the socket, just that it wrote something, since
   * that means it's ready. */
  xserver_finished_init (manager);

  return G_SOURCE_REMOVE;
}

gboolean
meta_xwayland_start (MetaXWaylandManager *manager,
                     struct wl_display   *wl_display)
{
  int xwayland_client_fd[2];
  int displayfd[2];
  int fd;

  if (!choose_xdisplay (manager))
    return FALSE;

  /* We want xwayland to be a wayland client so we make a socketpair to setup a
   * wayland protocol connection. */
  if (socketpair (AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0, xwayland_client_fd) < 0)
    {
      g_warning ("xwayland_client_fd socketpair failed\n");
      unlink (manager->lockfile);
      return 1;
    }

  if (socketpair (AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0, displayfd) < 0)
    {
      g_warning ("displayfd socketpair failed\n");
      unlink (manager->lockfile);
      return 1;
    }

  manager->pid = fork ();
  if (manager->pid == 0)
    {
      char socket_fd[8], unix_fd[8], abstract_fd[8], displayfd_fd[8];

      /* We passed SOCK_CLOEXEC, so dup the FD so it isn't
       * closed on exec.. */
      fd = dup (xwayland_client_fd[1]);
      snprintf (socket_fd, sizeof (socket_fd), "%d", fd);
      setenv ("WAYLAND_SOCKET", socket_fd, TRUE);

      fd = dup (manager->abstract_fd);
      snprintf (abstract_fd, sizeof (abstract_fd), "%d", fd);

      fd = dup (manager->unix_fd);
      snprintf (unix_fd, sizeof (unix_fd), "%d", fd);

      fd = dup (displayfd[1]);
      snprintf (displayfd_fd, sizeof (displayfd_fd), "%d", fd);

      /* xwayland, please. */
      if (getenv ("XWAYLAND_STFU"))
        {
          int dev_null;
          dev_null = open ("/dev/null", O_WRONLY);

          dup2 (dev_null, STDOUT_FILENO);
          dup2 (dev_null, STDERR_FILENO);
        }

      if (execl (XWAYLAND_PATH, XWAYLAND_PATH,
                 manager->display_name,
                 "-rootless",
                 "-noreset",
                 "-listen", abstract_fd,
                 "-listen", unix_fd,
                 "-displayfd", displayfd_fd,
                 NULL) < 0)
        {
          g_error ("Failed to spawn XWayland: %m");
        }
    }
  else if (manager->pid == -1)
    {
      g_error ("Failed to fork: %m");
    }

  g_child_watch_add (manager->pid, xserver_died, NULL);
  g_unix_fd_add (displayfd[0], G_IO_IN, on_displayfd_ready, manager);
  manager->client = wl_client_create (wl_display, xwayland_client_fd[0]);

  /* We need to run a mainloop until we know xwayland has a binding
   * for our xserver interface at which point we can assume it's
   * ready to start accepting connections. */
  manager->init_loop = g_main_loop_new (NULL, FALSE);
  g_main_loop_run (manager->init_loop);

  return TRUE;
}

/* To be called right after connecting */
void
meta_xwayland_complete_init (void)
{
  /* We install an X IO error handler in addition to the child watch,
     because after Xlib connects our child watch may not be called soon
     enough, and therefore we won't crash when X exits (and most important
     we won't reset the tty).
  */
  XSetIOErrorHandler (x_io_error);
}

void
meta_xwayland_stop (MetaXWaylandManager *manager)
{
  char path[256];

  snprintf (path, sizeof path, "/tmp/.X%d-lock", manager->display_index);
  unlink (path);
  snprintf (path, sizeof path, "/tmp/.X11-unix/X%d", manager->display_index);
  unlink (path);

  unlink (manager->lockfile);
}
