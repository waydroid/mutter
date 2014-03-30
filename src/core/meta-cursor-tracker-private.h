/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */

/*
 * Copyright (C) 2013 Red Hat, Inc.
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
 *
 * Author: Giovanni Campagna <gcampagn@redhat.com>
 */

#ifndef META_CURSOR_TRACKER_PRIVATE_H
#define META_CURSOR_TRACKER_PRIVATE_H

#include <meta/meta-cursor-tracker.h>

gboolean meta_cursor_tracker_handle_xevent (MetaCursorTracker *tracker,
					    XEvent            *xevent);

void     meta_cursor_tracker_set_root_cursor (MetaCursorTracker *tracker,
                                              MetaCursor         cursor);
#endif
