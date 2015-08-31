/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */

/* Mutter window deletion */

/*
 * Copyright (C) 2001, 2002 Havoc Pennington
 * Copyright (C) 2004 Elijah Newren
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

#define _XOPEN_SOURCE /* for kill() */

#include <config.h>
#include "util-private.h"
#include "window-private.h"
#include <meta/errors.h>
#include <meta/workspace.h>

#include <sys/types.h>
#include <sys/wait.h>
#include <signal.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

static void
dialog_exited (GPid pid, int status, gpointer user_data)
{
  MetaWindow *window = user_data;

  window->dialog_pid = -1;

  /* exit status of 1 means the user pressed "Force Quit" */
  if (WIFEXITED (status) && WEXITSTATUS (status) == 1)
    meta_window_kill (window);
}

static void
present_existing_delete_dialog (MetaWindow *window,
                                guint32     timestamp)
{
  meta_topic (META_DEBUG_PING,
              "Presenting existing ping dialog for %s\n",
              window->desc);

  if (window->dialog_pid >= 0)
    {
      GSList *windows;
      GSList *tmp;

      /* Activate transient for window that belongs to
       * mutter-dialog
       */

      windows = meta_display_list_windows (window->display, META_LIST_DEFAULT);
      tmp = windows;
      while (tmp != NULL)
        {
          MetaWindow *w = tmp->data;

          if (w->transient_for == window && w->res_class &&
              g_ascii_strcasecmp (w->res_class, "mutter-dialog") == 0)
            {
              meta_window_activate (w, timestamp);
              break;
            }

          tmp = tmp->next;
        }

      g_slist_free (windows);
    }
}

static void
show_delete_dialog (MetaWindow *window,
                    guint32     timestamp)
{
  char *window_title;
  gchar *window_content, *tmp;
  GPid dialog_pid;

  meta_topic (META_DEBUG_PING,
              "Got delete ping timeout for %s\n",
              window->desc);

  if (window->dialog_pid >= 0)
    {
      present_existing_delete_dialog (window, timestamp);
      return;
    }

  /* This is to get a better string if the title isn't representable
   * in the locale encoding; actual conversion to UTF-8 is done inside
   * meta_show_dialog */

  if (window->title && window->title[0])
    {
      tmp = g_locale_from_utf8 (window->title, -1, NULL, NULL, NULL);
      if (tmp == NULL)
        window_title = NULL;
      else
        window_title = window->title;
      g_free (tmp);
    }
  else
    {
      window_title = NULL;
    }

  /* Translators: %s is a window title */
  if (window_title)
    tmp = g_strdup_printf (_("“%s” is not responding."), window_title);
  else
    tmp = g_strdup (_("Application is not responding."));

  window_content = g_strdup_printf (
      "<big><b>%s</b></big>\n\n%s",
      tmp,
      _("You may choose to wait a short while for it to "
        "continue or force the application to quit entirely."));

  dialog_pid =
    meta_show_dialog ("--question",
                      window_content, NULL,
                      window->screen->screen_name,
                      _("_Wait"), _("_Force Quit"),
                      "face-sad-symbolic", window->xwindow,
                      NULL, NULL);

  g_free (window_content);
  g_free (tmp);

  window->dialog_pid = dialog_pid;
  g_child_watch_add (dialog_pid, dialog_exited, window);
}

static void
kill_delete_dialog (MetaWindow *window)
{
  if (window->dialog_pid > -1)
    kill (window->dialog_pid, SIGTERM);
}

void
meta_window_set_alive (MetaWindow *window,
                       gboolean    is_alive)
{
  if (is_alive)
    kill_delete_dialog (window);
  else
    show_delete_dialog (window, CurrentTime);
}

void
meta_window_check_alive (MetaWindow *window,
                         guint32     timestamp)
{
  meta_display_ping_window (window, timestamp);
}

void
meta_window_delete (MetaWindow  *window,
                    guint32      timestamp)
{
  META_WINDOW_GET_CLASS (window)->delete (window, timestamp);

  meta_window_check_alive (window, timestamp);
}

void
meta_window_kill (MetaWindow *window)
{
  META_WINDOW_GET_CLASS (window)->kill (window);
}

void
meta_window_free_delete_dialog (MetaWindow *window)
{
  if (window->dialog_pid >= 0)
    {
      kill (window->dialog_pid, 9);
      window->dialog_pid = -1;
    }
}
