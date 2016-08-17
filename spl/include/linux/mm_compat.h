/*****************************************************************************\
 *  Copyright (C) 2007-2010 Lawrence Livermore National Security, LLC.
 *  Copyright (C) 2007 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Brian Behlendorf <behlendorf1@llnl.gov>.
 *  UCRL-CODE-235197
 *
 *  This file is part of the SPL, Solaris Porting Layer.
 *  For details, see <http://zfsonlinux.org/>.
 *
 *  The SPL is free software; you can redistribute it and/or modify it
 *  under the terms of the GNU General Public License as published by the
 *  Free Software Foundation; either version 2 of the License, or (at your
 *  option) any later version.
 *
 *  The SPL is distributed in the hope that it will be useful, but WITHOUT
 *  ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 *  FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 *  for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with the SPL.  If not, see <http://www.gnu.org/licenses/>.
\*****************************************************************************/

#ifndef _SPL_MM_COMPAT_H
#define _SPL_MM_COMPAT_H

#include <linux/mm.h>
#include <linux/fs.h>

#if !defined(HAVE_SHRINK_CONTROL_STRUCT)
struct shrink_control {
	gfp_t gfp_mask;
	unsigned long nr_to_scan;
};
#endif /* HAVE_SHRINK_CONTROL_STRUCT */

/*
 * Due to frequent changes in the shrinker API the following
 * compatibility wrappers should be used.  They are as follows:
 *
 * SPL_SHRINKER_DECLARE is used to declare the shrinker which is
 * passed to spl_register_shrinker()/spl_unregister_shrinker().  Use
 * shrinker_name to set the shrinker variable name, shrinker_callback
 * to set the callback function, and seek_cost to define the cost of
 * reclaiming an object.
 *
 *   SPL_SHRINKER_DECLARE(shrinker_name, shrinker_callback, seek_cost);
 *
 * SPL_SHRINKER_CALLBACK_FWD_DECLARE is used when a forward declaration
 * of the shrinker callback function is required.  Only the callback
 * function needs to be passed.
 *
 *   SPL_SHRINKER_CALLBACK_FWD_DECLARE(shrinker_callback);
 *
 * SPL_SHRINKER_CALLBACK_WRAPPER is used to declare the callback function
 * which is registered with the shrinker.  This function will call your
 * custom shrinker which must use the following prototype.  Notice the
 * leading __'s, these must be appended to the callback_function name.
 *
 *   int  __shrinker_callback(struct shrinker *, struct shrink_control *)
 *   SPL_SHRINKER_CALLBACK_WRAPPER(shrinker_callback);a
 *
 *
 * Example:
 *
 * SPL_SHRINKER_CALLBACK_FWD_DECLARE(my_shrinker_fn);
 * SPL_SHRINKER_DECLARE(my_shrinker, my_shrinker_fn, 1);
 *
 * static int
 * __my_shrinker_fn(struct shrinker *shrink, struct shrink_control *sc)
 * {
 *	if (sc->nr_to_scan) {
 *		...scan objects in the cache and reclaim them...
 *	}
 *
 *	...calculate number of objects in the cache...
 *
 *	return (number of objects in the cache);
 * }
 * SPL_SHRINKER_CALLBACK_WRAPPER(my_shrinker_fn);
 */

#define	spl_register_shrinker(x)	register_shrinker(x)
#define	spl_unregister_shrinker(x)	unregister_shrinker(x)

/*
 * Linux 2.6.23 - 2.6.34 Shrinker API Compatibility.
 */
#if defined(HAVE_2ARGS_OLD_SHRINKER_CALLBACK)
#define	SPL_SHRINKER_DECLARE(s, x, y)					\
static struct shrinker s = {						\
	.shrink = x,							\
	.seeks = y							\
}

#define	SPL_SHRINKER_CALLBACK_FWD_DECLARE(fn)				\
static int fn(int nr_to_scan, unsigned int gfp_mask)

#define	SPL_SHRINKER_CALLBACK_WRAPPER(fn)				\
static int								\
fn(int nr_to_scan, unsigned int gfp_mask)				\
{									\
	struct shrink_control sc;					\
									\
	sc.nr_to_scan = nr_to_scan;					\
	sc.gfp_mask = gfp_mask;						\
									\
	return (__ ## fn(NULL, &sc));					\
}

/*
 * Linux 2.6.35 to 2.6.39 Shrinker API Compatibility.
 */
#elif defined(HAVE_3ARGS_SHRINKER_CALLBACK)
#define	SPL_SHRINKER_DECLARE(s, x, y)					\
static struct shrinker s = {						\
	.shrink = x,							\
	.seeks = y							\
}

#define	SPL_SHRINKER_CALLBACK_FWD_DECLARE(fn)				\
static int fn(struct shrinker *, int, unsigned int)

#define	SPL_SHRINKER_CALLBACK_WRAPPER(fn)				\
static int								\
fn(struct shrinker *shrink, int nr_to_scan, unsigned int gfp_mask)	\
{									\
	struct shrink_control sc;					\
									\
	sc.nr_to_scan = nr_to_scan;					\
	sc.gfp_mask = gfp_mask;						\
									\
	return (__ ## fn(shrink, &sc));					\
}

/*
 * Linux 3.0 to 3.11 Shrinker API Compatibility.
 */
#elif defined(HAVE_2ARGS_NEW_SHRINKER_CALLBACK)
#define	SPL_SHRINKER_DECLARE(s, x, y)					\
static struct shrinker s = {						\
	.shrink = x,							\
	.seeks = y							\
}

#define	SPL_SHRINKER_CALLBACK_FWD_DECLARE(fn)				\
static int fn(struct shrinker *, struct shrink_control *)

#define	SPL_SHRINKER_CALLBACK_WRAPPER(fn)				\
static int								\
fn(struct shrinker *shrink, struct shrink_control *sc)			\
{									\
	return (__ ## fn(shrink, sc));					\
}

/*
 * Linux 3.12 and later Shrinker API Compatibility.
 */
#elif defined(HAVE_SPLIT_SHRINKER_CALLBACK)
#define	SPL_SHRINKER_DECLARE(s, x, y)					\
static struct shrinker s = {						\
	.count_objects = x ## _count_objects,				\
	.scan_objects = x ## _scan_objects,				\
	.seeks = y							\
}

#define	SPL_SHRINKER_CALLBACK_FWD_DECLARE(fn)				\
static unsigned long fn ## _count_objects(struct shrinker *,		\
    struct shrink_control *);						\
static unsigned long fn ## _scan_objects(struct shrinker *,		\
    struct shrink_control *)

#define	SPL_SHRINKER_CALLBACK_WRAPPER(fn)				\
static unsigned long							\
fn ## _count_objects(struct shrinker *shrink, struct shrink_control *sc)\
{									\
	int __ret__;							\
									\
	sc->nr_to_scan = 0;						\
	__ret__ = __ ## fn(NULL, sc);					\
									\
	/* Errors may not be returned and must be converted to zeros */	\
	return ((__ret__ < 0) ? 0 : __ret__);				\
}									\
									\
static unsigned long							\
fn ## _scan_objects(struct shrinker *shrink, struct shrink_control *sc)	\
{									\
	int __ret__;							\
									\
	__ret__ = __ ## fn(NULL, sc);					\
	return ((__ret__ < 0) ? SHRINK_STOP : __ret__);			\
}
#else
/*
 * Linux 2.x to 2.6.22, or a newer shrinker API has been introduced.
 */
#error "Unknown shrinker callback"
#endif

#if defined(HAVE_SPLIT_SHRINKER_CALLBACK)
typedef unsigned long	spl_shrinker_t;
#else
typedef int		spl_shrinker_t;
#define	SHRINK_STOP	(-1)
#endif

#endif /* SPL_MM_COMPAT_H */
