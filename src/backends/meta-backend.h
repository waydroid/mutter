/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */

/*
 * Copyright (C) 2014 Red Hat
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
 *
 * Written by:
 *     Jasper St. Pierre <jstpierre@mecheye.net>
 */

#ifndef META_BACKEND_H
#define META_BACKEND_H

#include <glib-object.h>

#include <meta/meta-idle-monitor.h>
#include "meta-monitor-manager.h"
#include "meta-cursor-renderer.h"

typedef struct _MetaBackend        MetaBackend;
typedef struct _MetaBackendClass   MetaBackendClass;

GType meta_backend_get_type (void);

MetaBackend * meta_get_backend (void);

MetaIdleMonitor * meta_backend_get_idle_monitor (MetaBackend *backend,
                                                 int          device_id);
MetaMonitorManager * meta_backend_get_monitor_manager (MetaBackend *backend);
MetaCursorRenderer * meta_backend_get_cursor_renderer (MetaBackend *backend);

gboolean meta_backend_grab_device (MetaBackend *backend,
                                   int          device_id,
                                   uint32_t     timestamp);
gboolean meta_backend_ungrab_device (MetaBackend *backend,
                                     int          device_id,
                                     uint32_t     timestamp);

void meta_clutter_init (void);

#endif /* META_BACKEND_H */
