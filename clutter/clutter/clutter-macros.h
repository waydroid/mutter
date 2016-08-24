/*
 * Clutter.
 *
 * An OpenGL based 'interactive canvas' library.
 *
 * Copyright (C) 2012 Intel Corporation
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
 */

#ifndef __CLUTTER_MACROS_H__
#define __CLUTTER_MACROS_H__

#if !defined(__CLUTTER_H_INSIDE__) && !defined(CLUTTER_COMPILATION)
#error "Only <clutter/clutter.h> can be included directly."
#endif

#include <clutter/clutter-version.h>

/**
 * CLUTTER_FLAVOUR:
 *
 * GL Windowing system used
 *
 * Since: 0.4
 *
 * Deprecated: 1.10: The macro evaluates to "deprecated" as Clutter can be
 *   compiled with multiple windowing system backends. Use the various
 *   CLUTTER_WINDOWING_* macros to detect the windowing system that Clutter
 *   is being compiled against, and the type check macros for the
 *   #ClutterBackend for a run-time check.
 */
#define CLUTTER_FLAVOUR         "deprecated"

/**
 * CLUTTER_COGL:
 *
 * Cogl (internal GL abstraction utility library) backend. Can be "gl" or
 * "gles" currently
 *
 * Since: 0.4
 *
 * Deprecated: 1.10: The macro evaluates to "deprecated" as Cogl can be
 *   compiled against multiple GL implementations.
 */
#define CLUTTER_COGL            "deprecated"

/**
 * CLUTTER_STAGE_TYPE:
 *
 * The default GObject type for the Clutter stage.
 *
 * Since: 0.8
 *
 * Deprecated: 1.10: The macro evaluates to "deprecated" as Clutter can
 *   be compiled against multiple windowing systems. You can use the
 *   CLUTTER_WINDOWING_* macros for compile-time checks, and the type
 *   check macros for run-time checks.
 */
#define CLUTTER_STAGE_TYPE      "deprecated"

/**
 * CLUTTER_NO_FPU:
 *
 * Set to 1 if Clutter was built without FPU (i.e fixed math), 0 otherwise
 *
 * Deprecated: 0.6: This macro is no longer defined (identical code is used
 *  regardless the presence of FPU).
 */
#define CLUTTER_NO_FPU          (0)

/* some structures are meant to be opaque and still be allocated on the stack;
 * in order to avoid people poking at their internals, we use this macro to
 * ensure that users don't accidentally access a struct private members.
 *
 * we use the CLUTTER_COMPILATION define to allow us easier access, though.
 */
#ifdef CLUTTER_COMPILATION
#define CLUTTER_PRIVATE_FIELD(x)        x
#else
#define CLUTTER_PRIVATE_FIELD(x)        clutter_private_ ## x
#endif

#ifndef _CLUTTER_EXTERN
#define _CLUTTER_EXTERN extern
#endif

#if __GNUC__ > 4 || (__GNUC__ == 4 && __GNUC_MINOR__ >= 6) || \
  __clang_major__ > 3 || (__clang_major__ == 3 && __clang_minor__ >= 4)
#define _CLUTTER_GNUC_DO_PRAGMA(x) _Pragma(G_STRINGIFY (x))
#define _CLUTTER_DEPRECATED_MACRO _CLUTTER_GNUC_DO_PRAGMA(GCC warning "Deprecated macro")
#define _CLUTTER_DEPRECATED_MACRO_FOR(f) _CLUTTER_GNUC_DO_PRAGMA(GCC warning #f)
#else
#define _CLUTTER_DEPRECATED_MACRO
#define _CLUTTER_DEPRECATED_MACRO_FOR(f)
#endif

/* these macros are used to mark deprecated functions, and thus have to be
 * exposed in a public header.
 *
 * do *not* use them in other libraries depending on Clutter: use G_DEPRECATED
 * and G_DEPRECATED_FOR, or use your own wrappers around them.
 */
#ifdef CLUTTER_DISABLE_DEPRECATION_WARNINGS
#define CLUTTER_DEPRECATED _CLUTTER_EXTERN
#define CLUTTER_DEPRECATED_FOR(f) _CLUTTER_EXTERN
#define CLUTTER_UNAVAILABLE(maj,min) _CLUTTER_EXTERN
#define CLUTTER_DEPRECATED_MACRO
#define CLUTTER_DEPRECATED_MACRO_FOR(f)
#else
#define CLUTTER_DEPRECATED G_DEPRECATED _CLUTTER_EXTERN
#define CLUTTER_DEPRECATED_FOR(f) G_DEPRECATED_FOR(f) _CLUTTER_EXTERN
#define CLUTTER_UNAVAILABLE(maj,min) G_UNAVAILABLE(maj,min) _CLUTTER_EXTERN
#define CLUTTER_DEPRECATED_MACRO _CLUTTER_DEPRECATED_MACRO
#define CLUTTER_DEPRECATED_MACRO_FOR(f) _CLUTTER_DEPRECATED_MACRO_FOR(f)
#endif

#define CLUTTER_AVAILABLE_IN_ALL _CLUTTER_EXTERN

#define CLUTTER_AVAILABLE_IN_MUTTER _CLUTTER_EXTERN

/**
 * CLUTTER_VERSION_MIN_REQUIRED:
 *
 * A macro that should be defined by the user prior to including the
 * clutter.h header.
 *
 * The definition should be one of the predefined Clutter version macros,
 * such as: %CLUTTER_VERSION_1_0, %CLUTTER_VERSION_1_2, ...
 *
 * This macro defines the lower bound for the Clutter API to be used.
 *
 * If a function has been deprecated in a newer version of Clutter, it
 * is possible to use this symbol to avoid the compiler warnings without
 * disabling warnings for every deprecated function.
 *
 * Since: 1.10
 */
#ifndef CLUTTER_VERSION_MIN_REQUIRED
# define CLUTTER_VERSION_MIN_REQUIRED   (CLUTTER_VERSION_CUR_STABLE)
#endif

/**
 * CLUTTER_VERSION_MAX_ALLOWED:
 *
 * A macro that should be define by the user prior to including the
 * clutter.h header.
 *
 * The definition should be one of the predefined Clutter version macros,
 * such as: %CLUTTER_VERSION_1_0, %CLUTTER_VERSION_1_2, ...
 *
 * This macro defines the upper bound for the Clutter API to be used.
 *
 * If a function has been introduced in a newer version of Clutter, it
 * is possible to use this symbol to get compiler warnings when trying
 * to use that function.
 *
 * Since: 1.10
 */
#ifndef CLUTTER_VERSION_MAX_ALLOWED
# if CLUTTER_VERSION_MIN_REQUIRED > CLUTTER_VERSION_PREV_STABLE
#  define CLUTTER_VERSION_MAX_ALLOWED   CLUTTER_VERSION_MIN_REQUIRED
# else
#  define CLUTTER_VERSION_MAX_ALLOWED   CLUTTER_VERSION_CUR_STABLE
# endif
#endif

/* sanity checks */
#if CLUTTER_VERSION_MAX_ALLOWED < CLUTTER_VERSION_MIN_REQUIRED
# error "CLUTTER_VERSION_MAX_ALLOWED must be >= CLUTTER_VERSION_MIN_REQUIRED"
#endif
#if CLUTTER_VERSION_MIN_REQUIRED < CLUTTER_VERSION_1_0
# error "CLUTTER_VERSION_MIN_REQUIRED must be >= CLUTTER_VERSION_1_0"
#endif

/* XXX: Every new stable minor release should add a set of macros here */

#if CLUTTER_VERSION_MIN_REQUIRED >= CLUTTER_VERSION_1_0
# define CLUTTER_DEPRECATED_IN_1_0              CLUTTER_DEPRECATED
# define CLUTTER_DEPRECATED_IN_1_0_FOR(f)       CLUTTER_DEPRECATED_FOR(f)
#else
# define CLUTTER_DEPRECATED_IN_1_0              _CLUTTER_EXTERN
# define CLUTTER_DEPRECATED_IN_1_0_FOR(f)       _CLUTTER_EXTERN
#endif

#if CLUTTER_VERSION_MAX_ALLOWED < CLUTTER_VERSION_1_0
# define CLUTTER_AVAILABLE_IN_1_0               CLUTTER_UNAVAILABLE(1, 0)
#else
# define CLUTTER_AVAILABLE_IN_1_0               _CLUTTER_EXTERN
#endif

#if CLUTTER_VERSION_MIN_REQUIRED >= CLUTTER_VERSION_1_2
# define CLUTTER_DEPRECATED_IN_1_2              CLUTTER_DEPRECATED
# define CLUTTER_DEPRECATED_IN_1_2_FOR(f)       CLUTTER_DEPRECATED_FOR(f)
#else
# define CLUTTER_DEPRECATED_IN_1_2              _CLUTTER_EXTERN
# define CLUTTER_DEPRECATED_IN_1_2_FOR(f)       _CLUTTER_EXTERN
#endif

#if CLUTTER_VERSION_MAX_ALLOWED < CLUTTER_VERSION_1_2
# define CLUTTER_AVAILABLE_IN_1_2               CLUTTER_UNAVAILABLE(1, 2)
#else
# define CLUTTER_AVAILABLE_IN_1_2               _CLUTTER_EXTERN
#endif

#if CLUTTER_VERSION_MIN_REQUIRED >= CLUTTER_VERSION_1_4
# define CLUTTER_DEPRECATED_IN_1_4              CLUTTER_DEPRECATED
# define CLUTTER_DEPRECATED_IN_1_4_FOR(f)       CLUTTER_DEPRECATED_FOR(f)
#else
# define CLUTTER_DEPRECATED_IN_1_4              _CLUTTER_EXTERN
# define CLUTTER_DEPRECATED_IN_1_4_FOR(f)       _CLUTTER_EXTERN
#endif

#if CLUTTER_VERSION_MAX_ALLOWED < CLUTTER_VERSION_1_4
# define CLUTTER_AVAILABLE_IN_1_4               CLUTTER_UNAVAILABLE(1, 4)
#else
# define CLUTTER_AVAILABLE_IN_1_4               _CLUTTER_EXTERN
#endif

#if CLUTTER_VERSION_MIN_REQUIRED >= CLUTTER_VERSION_1_6
# define CLUTTER_DEPRECATED_IN_1_6              CLUTTER_DEPRECATED
# define CLUTTER_DEPRECATED_IN_1_6_FOR(f)       CLUTTER_DEPRECATED_FOR(f)
#else
# define CLUTTER_DEPRECATED_IN_1_6              _CLUTTER_EXTERN
# define CLUTTER_DEPRECATED_IN_1_6_FOR(f)       _CLUTTER_EXTERN
#endif

#if CLUTTER_VERSION_MAX_ALLOWED < CLUTTER_VERSION_1_6
# define CLUTTER_AVAILABLE_IN_1_6               CLUTTER_UNAVAILABLE(1, 6)
#else
# define CLUTTER_AVAILABLE_IN_1_6               _CLUTTER_EXTERN
#endif

#if CLUTTER_VERSION_MIN_REQUIRED >= CLUTTER_VERSION_1_8
# define CLUTTER_DEPRECATED_IN_1_8              CLUTTER_DEPRECATED
# define CLUTTER_DEPRECATED_IN_1_8_FOR(f)       CLUTTER_DEPRECATED_FOR(f)
#else
# define CLUTTER_DEPRECATED_IN_1_8              _CLUTTER_EXTERN
# define CLUTTER_DEPRECATED_IN_1_8_FOR(f)       _CLUTTER_EXTERN
#endif

#if CLUTTER_VERSION_MAX_ALLOWED < CLUTTER_VERSION_1_8
# define CLUTTER_AVAILABLE_IN_1_8               CLUTTER_UNAVAILABLE(1, 8)
#else
# define CLUTTER_AVAILABLE_IN_1_8               _CLUTTER_EXTERN
#endif

#if CLUTTER_VERSION_MIN_REQUIRED >= CLUTTER_VERSION_1_10
# define CLUTTER_DEPRECATED_IN_1_10             CLUTTER_DEPRECATED
# define CLUTTER_DEPRECATED_IN_1_10_FOR(f)      CLUTTER_DEPRECATED_FOR(f)
#else
# define CLUTTER_DEPRECATED_IN_1_10             _CLUTTER_EXTERN
# define CLUTTER_DEPRECATED_IN_1_10_FOR(f)      _CLUTTER_EXTERN
#endif

#if CLUTTER_VERSION_MAX_ALLOWED < CLUTTER_VERSION_1_10
# define CLUTTER_AVAILABLE_IN_1_10              CLUTTER_UNAVAILABLE(1, 10)
#else
# define CLUTTER_AVAILABLE_IN_1_10              _CLUTTER_EXTERN
#endif

#if CLUTTER_VERSION_MIN_REQUIRED >= CLUTTER_VERSION_1_12
# define CLUTTER_DEPRECATED_IN_1_12             CLUTTER_DEPRECATED
# define CLUTTER_DEPRECATED_IN_1_12_FOR(f)      CLUTTER_DEPRECATED_FOR(f)
#else
# define CLUTTER_DEPRECATED_IN_1_12             _CLUTTER_EXTERN
# define CLUTTER_DEPRECATED_IN_1_12_FOR(f)      _CLUTTER_EXTERN
#endif

#define CLUTTER_DEPRECATED_IN_MUTTER            CLUTTER_DEPRECATED

#if CLUTTER_VERSION_MAX_ALLOWED < CLUTTER_VERSION_1_12
# define CLUTTER_AVAILABLE_IN_1_12              CLUTTER_UNAVAILABLE(1, 12)
#else
# define CLUTTER_AVAILABLE_IN_1_12              _CLUTTER_EXTERN
#endif

#if CLUTTER_VERSION_MIN_REQUIRED >= CLUTTER_VERSION_1_14
# define CLUTTER_DEPRECATED_IN_1_14             CLUTTER_DEPRECATED
# define CLUTTER_DEPRECATED_IN_1_14_FOR(f)      CLUTTER_DEPRECATED_FOR(f)
#else
# define CLUTTER_DEPRECATED_IN_1_14             _CLUTTER_EXTERN
# define CLUTTER_DEPRECATED_IN_1_14_FOR(f)      _CLUTTER_EXTERN
#endif

#if CLUTTER_VERSION_MAX_ALLOWED < CLUTTER_VERSION_1_14
# define CLUTTER_AVAILABLE_IN_1_14              CLUTTER_UNAVAILABLE(1, 14)
#else
# define CLUTTER_AVAILABLE_IN_1_14              _CLUTTER_EXTERN
#endif

#if CLUTTER_VERSION_MIN_REQUIRED >= CLUTTER_VERSION_1_16
# define CLUTTER_DEPRECATED_IN_1_16             CLUTTER_DEPRECATED
# define CLUTTER_DEPRECATED_IN_1_16_FOR(f)      CLUTTER_DEPRECATED_FOR(f)
#else
# define CLUTTER_DEPRECATED_IN_1_16             _CLUTTER_EXTERN
# define CLUTTER_DEPRECATED_IN_1_16_FOR(f)      _CLUTTER_EXTERN
#endif

#if CLUTTER_VERSION_MAX_ALLOWED < CLUTTER_VERSION_1_16
# define CLUTTER_AVAILABLE_IN_1_16              CLUTTER_UNAVAILABLE(1, 16)
#else
# define CLUTTER_AVAILABLE_IN_1_16              _CLUTTER_EXTERN
#endif

#if CLUTTER_VERSION_MIN_REQUIRED >= CLUTTER_VERSION_1_18
# define CLUTTER_DEPRECATED_IN_1_18             CLUTTER_DEPRECATED
# define CLUTTER_DEPRECATED_IN_1_18_FOR(f)      CLUTTER_DEPRECATED_FOR(f)
#else
# define CLUTTER_DEPRECATED_IN_1_18             _CLUTTER_EXTERN
# define CLUTTER_DEPRECATED_IN_1_18_FOR(f)      _CLUTTER_EXTERN
#endif

#if CLUTTER_VERSION_MAX_ALLOWED < CLUTTER_VERSION_1_18
# define CLUTTER_AVAILABLE_IN_1_18              CLUTTER_UNAVAILABLE(1, 18)
#else
# define CLUTTER_AVAILABLE_IN_1_18              _CLUTTER_EXTERN
#endif

#if CLUTTER_VERSION_MIN_REQUIRED >= CLUTTER_VERSION_1_20
# define CLUTTER_DEPRECATED_IN_1_20             CLUTTER_DEPRECATED
# define CLUTTER_DEPRECATED_IN_1_20_FOR(f)      CLUTTER_DEPRECATED_FOR(f)
#else
# define CLUTTER_DEPRECATED_IN_1_20             _CLUTTER_EXTERN
# define CLUTTER_DEPRECATED_IN_1_20_FOR(f)      _CLUTTER_EXTERN
#endif

#if CLUTTER_VERSION_MAX_ALLOWED < CLUTTER_VERSION_1_20
# define CLUTTER_AVAILABLE_IN_1_20              CLUTTER_UNAVAILABLE(1, 20)
#else
# define CLUTTER_AVAILABLE_IN_1_20              _CLUTTER_EXTERN
#endif

#if CLUTTER_VERSION_MIN_REQUIRED >= CLUTTER_VERSION_1_22
# define CLUTTER_DEPRECATED_IN_1_22             CLUTTER_DEPRECATED
# define CLUTTER_DEPRECATED_IN_1_22_FOR(f)      CLUTTER_DEPRECATED_FOR(f)
#else
# define CLUTTER_DEPRECATED_IN_1_22             _CLUTTER_EXTERN
# define CLUTTER_DEPRECATED_IN_1_22_FOR(f)      _CLUTTER_EXTERN
#endif

#if CLUTTER_VERSION_MAX_ALLOWED < CLUTTER_VERSION_1_22
# define CLUTTER_AVAILABLE_IN_1_22              CLUTTER_UNAVAILABLE(1, 22)
#else
# define CLUTTER_AVAILABLE_IN_1_22              _CLUTTER_EXTERN
#endif

#if CLUTTER_VERSION_MIN_REQUIRED >= CLUTTER_VERSION_1_24
# define CLUTTER_DEPRECATED_IN_1_24             CLUTTER_DEPRECATED
# define CLUTTER_DEPRECATED_IN_1_24_FOR(f)      CLUTTER_DEPRECATED_FOR(f)
# define CLUTTER_MACRO_DEPRECATED_IN_1_24       CLUTTER_DEPRECATED_MACRO
# define CLUTTER_MACRO_DEPRECATED_IN_1_24_FOR(f) CLUTTER_DEPRECATED_MACRO_FOR(f)
#else
# define CLUTTER_DEPRECATED_IN_1_24             _CLUTTER_EXTERN
# define CLUTTER_DEPRECATED_IN_1_24_FOR(f)      _CLUTTER_EXTERN
# define CLUTTER_MACRO_DEPRECATED_IN_1_24
# define CLUTTER_MACRO_DEPRECATED_IN_1_24_FOR(f)
#endif

#if CLUTTER_VERSION_MAX_ALLOWED < CLUTTER_VERSION_1_24
# define CLUTTER_AVAILABLE_IN_1_24              CLUTTER_UNAVAILABLE(1, 24)
#else
# define CLUTTER_AVAILABLE_IN_1_24              _CLUTTER_EXTERN
#endif

#if CLUTTER_VERSION_MIN_REQUIRED >= CLUTTER_VERSION_1_26
# define CLUTTER_DEPRECATED_IN_1_26             CLUTTER_DEPRECATED
# define CLUTTER_DEPRECATED_IN_1_26_FOR(f)      CLUTTER_DEPRECATED_FOR(f)
# define CLUTTER_MACRO_DEPRECATED_IN_1_26       CLUTTER_DEPRECATED_MACRO
# define CLUTTER_MACRO_DEPRECATED_IN_1_26_FOR(f) CLUTTER_DEPRECATED_MACRO_FOR(f)
#else
# define CLUTTER_DEPRECATED_IN_1_26             _CLUTTER_EXTERN
# define CLUTTER_DEPRECATED_IN_1_26_FOR(f)      _CLUTTER_EXTERN
# define CLUTTER_MACRO_DEPRECATED_IN_1_26
# define CLUTTER_MACRO_DEPRECATED_IN_1_26_FOR(f)
#endif

#if CLUTTER_VERSION_MAX_ALLOWED < CLUTTER_VERSION_1_26
# define CLUTTER_AVAILABLE_IN_1_26              CLUTTER_UNAVAILABLE(1, 26)
#else
# define CLUTTER_AVAILABLE_IN_1_26              _CLUTTER_EXTERN
#endif

#endif /* __CLUTTER_MACROS_H__ */
