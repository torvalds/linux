/*	$OpenBSD: subr_extent.c,v 1.65 2024/01/19 22:12:24 kettenis Exp $	*/
/*	$NetBSD: subr_extent.c,v 1.7 1996/11/21 18:46:34 cgd Exp $	*/

/*-
 * Copyright (c) 1996, 1998 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Jason R. Thorpe and Matthias Drochner.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE NETBSD FOUNDATION, INC. AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE FOUNDATION OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * General purpose extent manager.
 */

#ifdef _KERNEL
#include <sys/param.h>
#include <sys/extent.h>
#include <sys/malloc.h>
#include <sys/systm.h>
#include <sys/queue.h>
#include <sys/pool.h>
#include <ddb/db_output.h>
#else
/*
 * user-land definitions, so it can fit into a testing harness.
 */
#include <sys/param.h>
#include <sys/extent.h>
#include <sys/queue.h>
#include <errno.h>
#include <err.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#define	malloc(s, t, flags)		malloc(s)
#define	free(p, t, s)			free(p)

#define	tsleep_nsec(c, p, s, t)		(EWOULDBLOCK)
#define	wakeup(chan)			((void)0)

struct pool {
	size_t pr_size;
};

#define	pool_init(a, b, c, d, e, f, g)	do { (a)->pr_size = (b); } while (0)
#define	pool_get(pp, flags)		malloc((pp)->pr_size, 0, 0)
#define	pool_put(pp, rp)		free((rp), 0, 0)

#define	panic(...)		do { warnx(__VA_ARGS__); abort(); } while (0)
#endif

#if defined(DIAGNOSTIC) || defined(DDB)
void	extent_print1(struct extent *, int (*)(const char *, ...)
	    __attribute__((__format__(__kprintf__,1,2))));
#endif

static	void extent_insert_and_optimize(struct extent *, u_long, u_long,
	    struct extent_region *, struct extent_region *);
static	struct extent_region *extent_alloc_region_descriptor(struct extent *, int);
static	void extent_free_region_descriptor(struct extent *,
	    struct extent_region *);
int	extent_do_alloc(struct extent *, u_long, u_long, u_long, u_long,
	    u_long, u_long, int, struct extent_region *, u_long *);

/*
 * Shortcut to align to an arbitrary power-of-two boundary.
 */
static __inline__ u_long
extent_align(u_long start, u_long align, u_long skew)
{
	return ((((start - skew) + (align - 1)) & (-align)) + skew);
}


#if defined(DIAGNOSTIC) || defined(DDB)
/*
 * Register the extent on a doubly linked list.
 * Should work, no?
 */
static LIST_HEAD(listhead, extent) ext_list;
static	void extent_register(struct extent *);

static void
extent_register(struct extent *ex)
{
#ifdef DIAGNOSTIC
	struct extent *ep;
#endif
	static int initialized;

	if (!initialized){
		LIST_INIT(&ext_list);
		initialized = 1;
	}

#ifdef DIAGNOSTIC
	LIST_FOREACH(ep, &ext_list, ex_link) {
		if (ep == ex)
			panic("%s: already registered", __func__);
	}
#endif

	/* Insert into list */
	LIST_INSERT_HEAD(&ext_list, ex, ex_link);
}
#endif	/* DIAGNOSTIC || DDB */

struct pool ex_region_pl;

static void
extent_pool_init(void)
{
	static int inited;

	if (!inited) {
		pool_init(&ex_region_pl, sizeof(struct extent_region), 0,
		    IPL_VM, 0, "extentpl", NULL);
		inited = 1;
	}
}

#ifdef DDB
/*
 * Print out all extents registered.  This is used in
 * DDB show extents
 */
void
extent_print_all(void)
{
	struct extent *ep;

	LIST_FOREACH(ep, &ext_list, ex_link) {
		extent_print1(ep, db_printf);
	}
}
#endif

/*
 * Allocate and initialize an extent map.
 */
struct extent *
extent_create(char *name, u_long start, u_long end, int mtype, caddr_t storage,
    size_t storagesize, int flags)
{
	struct extent *ex;
	caddr_t cp = storage;
	size_t sz = storagesize;
	struct extent_region *rp;
	int fixed_extent = (storage != NULL);

#ifdef DIAGNOSTIC
	/* Check arguments. */
	if (name == NULL)
		panic("%s: name == NULL", __func__);
	if (end < start) {
		printf("%s: extent `%s', start 0x%lx, end 0x%lx\n",
		    __func__, name, start, end);
		panic("%s: end < start", __func__);
	}
	if (fixed_extent && (storagesize < sizeof(struct extent_fixed)))
		panic("%s: fixed extent, bad storagesize 0x%lx",
		    __func__, (u_long)storagesize);
	if (fixed_extent == 0 && (storagesize != 0 || storage != NULL))
		panic("%s: storage provided for non-fixed", __func__);
#endif

	extent_pool_init();

	/* Allocate extent descriptor. */
	if (fixed_extent) {
		struct extent_fixed *fex;

		memset(storage, 0, storagesize);

		/*
		 * Align all descriptors on "long" boundaries.
		 */
		fex = (struct extent_fixed *)cp;
		ex = (struct extent *)fex;
		cp += ALIGN(sizeof(struct extent_fixed));
		sz -= ALIGN(sizeof(struct extent_fixed));
		fex->fex_storage = storage;
		fex->fex_storagesize = storagesize;

		/*
		 * In a fixed extent, we have to pre-allocate region
		 * descriptors and place them in the extent's freelist.
		 */
		LIST_INIT(&fex->fex_freelist);
		while (sz >= ALIGN(sizeof(struct extent_region))) {
			rp = (struct extent_region *)cp;
			cp += ALIGN(sizeof(struct extent_region));
			sz -= ALIGN(sizeof(struct extent_region));
			LIST_INSERT_HEAD(&fex->fex_freelist, rp, er_link);
		}
	} else {
		ex = (struct extent *)malloc(sizeof(struct extent),
		    mtype, (flags & EX_WAITOK) ? M_WAITOK : M_NOWAIT);
		if (ex == NULL)
			return (NULL);
	}

	/* Fill in the extent descriptor and return it to the caller. */
	LIST_INIT(&ex->ex_regions);
	ex->ex_name = name;
	ex->ex_start = start;
	ex->ex_end = end;
	ex->ex_mtype = mtype;
	ex->ex_flags = 0;
	if (fixed_extent)
		ex->ex_flags |= EXF_FIXED;
	if (flags & EX_NOCOALESCE)
		ex->ex_flags |= EXF_NOCOALESCE;

	if (flags & EX_FILLED) {
		rp = extent_alloc_region_descriptor(ex, flags);
		if (rp == NULL) {
			if (!fixed_extent)
				free(ex, mtype, sizeof(struct extent));
			return (NULL);
		}
		rp->er_start = start;
		rp->er_end = end;
		LIST_INSERT_HEAD(&ex->ex_regions, rp, er_link);
	}

#if defined(DIAGNOSTIC) || defined(DDB)
	extent_register(ex);
#endif
	return (ex);
}

/*
 * Destroy an extent map.
 */
void
extent_destroy(struct extent *ex)
{
	struct extent_region *rp, *orp;

#ifdef DIAGNOSTIC
	/* Check arguments. */
	if (ex == NULL)
		panic("%s: NULL extent", __func__);
#endif

	/* Free all region descriptors in extent. */
	for (rp = LIST_FIRST(&ex->ex_regions); rp != NULL; ) {
		orp = rp;
		rp = LIST_NEXT(rp, er_link);
		LIST_REMOVE(orp, er_link);
		extent_free_region_descriptor(ex, orp);
	}

#if defined(DIAGNOSTIC) || defined(DDB)
	/* Remove from the list of all extents. */
	LIST_REMOVE(ex, ex_link);
#endif

	/* If we're not a fixed extent, free the extent descriptor itself. */
	if ((ex->ex_flags & EXF_FIXED) == 0)
		free(ex, ex->ex_mtype, sizeof(*ex));
}

/*
 * Insert a region descriptor into the sorted region list after the
 * entry "after" or at the head of the list (if "after" is NULL).
 * The region descriptor we insert is passed in "rp".  We must
 * allocate the region descriptor before calling this function!
 * If we don't need the region descriptor, it will be freed here.
 */
static void
extent_insert_and_optimize(struct extent *ex, u_long start, u_long size,
    struct extent_region *after, struct extent_region *rp)
{
	struct extent_region *nextr;
	int appended = 0;

	if (after == NULL) {
		/*
		 * We're the first in the region list.  If there's
		 * a region after us, attempt to coalesce to save
		 * descriptor overhead.
		 */
		if (((ex->ex_flags & EXF_NOCOALESCE) == 0) &&
		    !LIST_EMPTY(&ex->ex_regions) &&
		    ((start + size) == LIST_FIRST(&ex->ex_regions)->er_start)) {
			/*
			 * We can coalesce.  Prepend us to the first region.
			 */
			LIST_FIRST(&ex->ex_regions)->er_start = start;
			extent_free_region_descriptor(ex, rp);
			return;
		}

		/*
		 * Can't coalesce.  Fill in the region descriptor
		 * in, and insert us at the head of the region list.
		 */
		rp->er_start = start;
		rp->er_end = start + (size - 1);
		LIST_INSERT_HEAD(&ex->ex_regions, rp, er_link);
		return;
	}

	/*
	 * If EXF_NOCOALESCE is set, coalescing is disallowed.
	 */
	if (ex->ex_flags & EXF_NOCOALESCE)
		goto cant_coalesce;

	/*
	 * Attempt to coalesce with the region before us.
	 */
	if ((after->er_end + 1) == start) {
		/*
		 * We can coalesce.  Append ourselves and make
		 * note of it.
		 */
		after->er_end = start + (size - 1);
		appended = 1;
	}

	/*
	 * Attempt to coalesce with the region after us.
	 */
	if (LIST_NEXT(after, er_link) != NULL &&
	    ((start + size) == LIST_NEXT(after, er_link)->er_start)) {
		/*
		 * We can coalesce.  Note that if we appended ourselves
		 * to the previous region, we exactly fit the gap, and
		 * can free the "next" region descriptor.
		 */
		if (appended) {
			/*
			 * Yup, we can free it up.
			 */
			after->er_end = LIST_NEXT(after, er_link)->er_end;
			nextr = LIST_NEXT(after, er_link);
			LIST_REMOVE(nextr, er_link);
			extent_free_region_descriptor(ex, nextr);
		} else {
			/*
			 * Nope, just prepend us to the next region.
			 */
			LIST_NEXT(after, er_link)->er_start = start;
		}

		extent_free_region_descriptor(ex, rp);
		return;
	}

	/*
	 * We weren't able to coalesce with the next region, but
	 * we don't need to allocate a region descriptor if we
	 * appended ourselves to the previous region.
	 */
	if (appended) {
		extent_free_region_descriptor(ex, rp);
		return;
	}

 cant_coalesce:

	/*
	 * Fill in the region descriptor and insert ourselves
	 * into the region list.
	 */
	rp->er_start = start;
	rp->er_end = start + (size - 1);
	LIST_INSERT_AFTER(after, rp, er_link);
}

/*
 * Allocate a specific region in an extent map.
 */
int
extent_do_alloc_region(struct extent *ex, u_long start, u_long size,
    int flags, struct extent_region *myrp)
{
	struct extent_region *rp, *last;
	u_long end = start + (size - 1);
	int error;

#ifdef DIAGNOSTIC
	/* Check arguments. */
	if (ex == NULL)
		panic("%s: NULL extent", __func__);
	if (size < 1) {
		printf("%s: extent `%s', size 0x%lx\n",
		    __func__, ex->ex_name, size);
		panic("%s: bad size", __func__);
	}
	if (end < start) {
		printf("%s: extent `%s', start 0x%lx, size 0x%lx\n",
		    __func__, ex->ex_name, start, size);
		panic("%s: overflow", __func__);
	}
	if ((flags & EX_CONFLICTOK) && (flags & EX_WAITSPACE))
		panic("%s: EX_CONFLICTOK and EX_WAITSPACE "
		    "are mutually exclusive", __func__);
#endif

	/*
	 * Make sure the requested region lies within the
	 * extent.
	 */
	if ((start < ex->ex_start) || (end > ex->ex_end)) {
#ifdef DIAGNOSTIC
		printf("%s: extent `%s' (0x%lx - 0x%lx)\n",
		    __func__, ex->ex_name, ex->ex_start, ex->ex_end);
		printf("%s: start 0x%lx, end 0x%lx\n",
		    __func__, start, end);
		panic("%s: region lies outside extent", __func__);
#else
		extent_free_region_descriptor(ex, myrp);
		return (EINVAL);
#endif
	}

 alloc_start:
	/*
	 * Attempt to place ourselves in the desired area of the
	 * extent.  We save ourselves some work by keeping the list sorted.
	 * In other words, if the start of the current region is greater
	 * than the end of our region, we don't have to search any further.
	 */

	/*
	 * Keep a pointer to the last region we looked at so
	 * that we don't have to traverse the list again when
	 * we insert ourselves.  If "last" is NULL when we
	 * finally insert ourselves, we go at the head of the
	 * list.  See extent_insert_and_optimize() for details.
	 */
	last = NULL;

	LIST_FOREACH(rp, &ex->ex_regions, er_link) {
		if (rp->er_start > end) {
			/*
			 * We lie before this region and don't
			 * conflict.
			 */
			break;
		}

		/*
		 * The current region begins before we end.
		 * Check for a conflict.
		 */
		if (rp->er_end >= start) {
			/*
			 * We conflict.  If we can (and want to) wait,
			 * do so.
			 */
			if (flags & EX_WAITSPACE) {
				ex->ex_flags |= EXF_WANTED;
				error = tsleep_nsec(ex,
				    PRIBIO | ((flags & EX_CATCH) ? PCATCH : 0),
				    "extnt", INFSLP);
				if (error)
					return (error);
				goto alloc_start;
			}

			/*
			 * If we tolerate conflicts adjust things such
			 * that all space in the requested region is
			 * allocated.
			 */
			if (flags & EX_CONFLICTOK) {
				/*
				 * There are four possibilities:
				 *
				 * 1. The current region overlaps with
				 *    the start of the requested region.
				 *    Adjust the requested region to
				 *    start at the end of the current
				 *    region and try again.
				 *
				 * 2. The current region falls
				 *    completely within the requested
				 *    region.  Free the current region
				 *    and try again.
				 *
				 * 3. The current region overlaps with
				 *    the end of the requested region.
				 *    Adjust the requested region to
				 *    end at the start of the current
				 *    region and try again.
				 *
				 * 4. The requested region falls
				 *    completely within the current
				 *    region.  We're done.
				 */
				if (rp->er_start <= start) {
					if (rp->er_end < ex->ex_end) {
						start = rp->er_end + 1;
						size = end - start + 1;
						goto alloc_start;
					}
				} else if (rp->er_end < end) {
					LIST_REMOVE(rp, er_link);
					extent_free_region_descriptor(ex, rp);
					goto alloc_start;
				} else if (rp->er_start < end) {
					if (rp->er_start > ex->ex_start) {
						end = rp->er_start - 1;
						size = end - start + 1;
						goto alloc_start;
					}
				}
				return (0);
			}

			extent_free_region_descriptor(ex, myrp);
			return (EAGAIN);
		}
		/*
		 * We don't conflict, but this region lies before
		 * us.  Keep a pointer to this region, and keep
		 * trying.
		 */
		last = rp;
	}

	/*
	 * We don't conflict with any regions.  "last" points
	 * to the region we fall after, or is NULL if we belong
	 * at the beginning of the region list.  Insert ourselves.
	 */
	extent_insert_and_optimize(ex, start, size, last, myrp);
	return (0);
}

int
extent_alloc_region(struct extent *ex, u_long start, u_long size, int flags)
{
	struct extent_region *rp;

	/*
	 * Allocate the region descriptor.  It will be freed later
	 * if we can coalesce with another region.
	 */
	rp = extent_alloc_region_descriptor(ex, flags);
	if (rp == NULL) {
#ifdef DIAGNOSTIC
		printf("%s: can't allocate region descriptor\n", __func__);
#endif
		return (ENOMEM);
	}

	return extent_do_alloc_region(ex, start, size, flags, rp);
}

int
extent_alloc_region_with_descr(struct extent *ex, u_long start,
    u_long size, int flags, struct extent_region *rp)
{
#ifdef DIAGNOSTIC
	if ((ex->ex_flags & EXF_NOCOALESCE) == 0)
		panic("%s: EX_NOCOALESCE not set", __func__);
#endif

	rp->er_flags = ER_DISCARD;
	return extent_do_alloc_region(ex, start, size, flags, rp);
}

/*
 * Macro to check (x + y) <= z.  This check is designed to fail
 * if an overflow occurs.
 */
#define LE_OV(x, y, z)	((((x) + (y)) >= (x)) && (((x) + (y)) <= (z)))

/*
 * Allocate a region in an extent map subregion.
 *
 * If EX_FAST is specified, we return the first fit in the map.
 * Otherwise, we try to minimize fragmentation by finding the
 * smallest gap that will hold the request.
 *
 * The allocated region is aligned to "alignment", which must be
 * a power of 2.
 */
int
extent_do_alloc(struct extent *ex, u_long substart, u_long subend,
    u_long size, u_long alignment, u_long skew, u_long boundary, int flags,
    struct extent_region *myrp, u_long *result)
{
	struct extent_region *rp, *last, *bestlast;
	u_long newstart, newend, exend, beststart, bestovh, ovh;
	u_long dontcross;
	int error;

#ifdef DIAGNOSTIC
	/* Check arguments. */
	if (ex == NULL)
		panic("%s: NULL extent", __func__);
	if (myrp == NULL)
		panic("%s: NULL region descriptor", __func__);
	if (result == NULL)
		panic("%s: NULL result pointer", __func__);
	if ((substart < ex->ex_start) || (substart > ex->ex_end) ||
	    (subend > ex->ex_end) || (subend < ex->ex_start)) {
		printf("%s: extent `%s', ex_start 0x%lx, ex_end 0x%lx\n",
		    __func__, ex->ex_name, ex->ex_start, ex->ex_end);
		printf("%s: substart 0x%lx, subend 0x%lx\n",
		    __func__, substart, subend);
		panic("%s: bad subregion", __func__);
	}
	if ((size < 1) || ((size - 1) > (subend - substart))) {
		printf("%s: extent `%s', size 0x%lx\n",
		    __func__, ex->ex_name, size);
		panic("%s: bad size", __func__);
	}
	if (alignment == 0)
		panic("%s: bad alignment", __func__);
	if (boundary && (boundary < size)) {
		printf("%s: extent `%s', size 0x%lx, boundary 0x%lx\n",
		    __func__, ex->ex_name, size, boundary);
		panic("%s: bad boundary", __func__);
	}
#endif

 alloc_start:
	/*
	 * Keep a pointer to the last region we looked at so
	 * that we don't have to traverse the list again when
	 * we insert ourselves.  If "last" is NULL when we
	 * finally insert ourselves, we go at the head of the
	 * list.  See extent_insert_and_optimize() for details.
	 */
	last = NULL;

	/*
	 * Keep track of size and location of the smallest
	 * chunk we fit in.
	 *
	 * Since the extent can be as large as the numeric range
	 * of the CPU (0 - 0xffffffff for 32-bit systems), the
	 * best overhead value can be the maximum unsigned integer.
	 * Thus, we initialize "bestovh" to 0, since we insert ourselves
	 * into the region list immediately on an exact match (which
	 * is the only case where "bestovh" would be set to 0).
	 */
	bestovh = 0;
	beststart = 0;
	bestlast = NULL;

	/*
	 * Keep track of end of free region.  This is either the end of extent
	 * or the start of a region past the subend.
	 */
	exend = ex->ex_end;

	/*
	 * For N allocated regions, we must make (N + 1)
	 * checks for unallocated space.  The first chunk we
	 * check is the area from the beginning of the subregion
	 * to the first allocated region after that point.
	 */
	newstart = extent_align(substart, alignment, skew);
	if (newstart < ex->ex_start) {
#ifdef DIAGNOSTIC
		printf("%s: extent `%s' (0x%lx - 0x%lx), alignment 0x%lx\n",
		    __func__, ex->ex_name, ex->ex_start, ex->ex_end,
		    alignment);
		panic("%s: overflow after alignment", __func__);
#else
		extent_free_region_descriptor(ex, myrp);
		return (EINVAL);
#endif
	}

	/*
	 * Find the first allocated region that begins on or after
	 * the subregion start, advancing the "last" pointer along
	 * the way.
	 */
	LIST_FOREACH(rp, &ex->ex_regions, er_link) {
		if (rp->er_start >= newstart)
			break;
		last = rp;
	}

	/*
	 * Relocate the start of our candidate region to the end of
	 * the last allocated region (if there was one overlapping
	 * our subrange).
	 */
	if (last != NULL && last->er_end >= newstart)
		newstart = extent_align((last->er_end + 1), alignment, skew);

	for (; rp != NULL; rp = LIST_NEXT(rp, er_link)) {
		/*
		 * If the region pasts the subend, bail out and see
		 * if we fit against the subend.
		 */
		if (rp->er_start > subend) {
			exend = rp->er_start;
			break;
		}

		/*
		 * Check the chunk before "rp".  Note that our
		 * comparison is safe from overflow conditions.
		 */
		if (LE_OV(newstart, size, rp->er_start)) {
			/*
			 * Do a boundary check, if necessary.  Note
			 * that a region may *begin* on the boundary,
			 * but it must end before the boundary.
			 */
			if (boundary) {
				newend = newstart + (size - 1);

				/*
				 * Calculate the next boundary after the start
				 * of this region.
				 */
				dontcross = extent_align(newstart+1, boundary, 
				    (flags & EX_BOUNDZERO) ? 0 : ex->ex_start)
				    - 1;

#if 0
				printf("newstart=%lx newend=%lx ex_start=%lx ex_end=%lx boundary=%lx dontcross=%lx\n",
				    newstart, newend, ex->ex_start, ex->ex_end,
				    boundary, dontcross);
#endif

				/* Check for overflow */
				if (dontcross < ex->ex_start)
					dontcross = ex->ex_end;
				else if (newend > dontcross) {
					/*
					 * Candidate region crosses boundary.
					 * Throw away the leading part and see
					 * if we still fit.
					 */
					newstart = dontcross + 1;
					newend = newstart + (size - 1);
					dontcross += boundary;
					if (!LE_OV(newstart, size, rp->er_start))
						goto skip;
				}

				/*
				 * If we run past the end of
				 * the extent or the boundary
				 * overflows, then the request
				 * can't fit.
				 */
				if (newstart + size - 1 > ex->ex_end ||
				    dontcross < newstart)
					goto fail;
			}

			/*
			 * We would fit into this space.  Calculate
			 * the overhead (wasted space).  If we exactly
			 * fit, or we're taking the first fit, insert
			 * ourselves into the region list.
			 */
			ovh = rp->er_start - newstart - size;
			if ((flags & EX_FAST) || (ovh == 0))
				goto found;

			/*
			 * Don't exactly fit, but check to see
			 * if we're better than any current choice.
			 */
			if ((bestovh == 0) || (ovh < bestovh)) {
				bestovh = ovh;
				beststart = newstart;
				bestlast = last;
			}
		}

skip:
		/*
		 * Skip past the current region and check again.
		 */
		newstart = extent_align((rp->er_end + 1), alignment, skew);
		if (newstart < rp->er_end) {
			/*
			 * Overflow condition.  Don't error out, since
			 * we might have a chunk of space that we can
			 * use.
			 */
			goto fail;
		}

		last = rp;
	}

	/*
	 * The final check is from the current starting point to the
	 * end of the subregion.  If there were no allocated regions,
	 * "newstart" is set to the beginning of the subregion, or
	 * just past the end of the last allocated region, adjusted
	 * for alignment in either case.
	 */
	if (LE_OV(newstart, (size - 1), subend)) {
		/*
		 * Do a boundary check, if necessary.  Note
		 * that a region may *begin* on the boundary,
		 * but it must end before the boundary.
		 */
		if (boundary) {
			newend = newstart + (size - 1);

			/*
			 * Calculate the next boundary after the start
			 * of this region.
			 */
			dontcross = extent_align(newstart+1, boundary, 
			    (flags & EX_BOUNDZERO) ? 0 : ex->ex_start)
			    - 1;

#if 0
			printf("newstart=%lx newend=%lx ex_start=%lx ex_end=%lx boundary=%lx dontcross=%lx\n",
			    newstart, newend, ex->ex_start, ex->ex_end,
			    boundary, dontcross);
#endif

			/* Check for overflow */
			if (dontcross < ex->ex_start)
				dontcross = ex->ex_end;
			else if (newend > dontcross) {
				/*
				 * Candidate region crosses boundary.
				 * Throw away the leading part and see
				 * if we still fit.
				 */
				newstart = dontcross + 1;
				newend = newstart + (size - 1);
				dontcross += boundary;
				if (!LE_OV(newstart, (size - 1), subend))
					goto fail;
			}

			/*
			 * If we run past the end of
			 * the extent or the boundary
			 * overflows, then the request
			 * can't fit.
			 */
			if (newstart + size - 1 > ex->ex_end ||
			    dontcross < newstart)
				goto fail;
		}

		/*
		 * We would fit into this space.  Calculate
		 * the overhead (wasted space).  If we exactly
		 * fit, or we're taking the first fit, insert
		 * ourselves into the region list.
		 */
		ovh = exend - newstart - (size - 1);
		if ((flags & EX_FAST) || (ovh == 0))
			goto found;

		/*
		 * Don't exactly fit, but check to see
		 * if we're better than any current choice.
		 */
		if ((bestovh == 0) || (ovh < bestovh)) {
			bestovh = ovh;
			beststart = newstart;
			bestlast = last;
		}
	}

 fail:
	/*
	 * One of the following two conditions have
	 * occurred:
	 *
	 *	There is no chunk large enough to hold the request.
	 *
	 *	If EX_FAST was not specified, there is not an
	 *	exact match for the request.
	 *
	 * Note that if we reach this point and EX_FAST is
	 * set, then we know there is no space in the extent for
	 * the request.
	 */
	if (((flags & EX_FAST) == 0) && (bestovh != 0)) {
		/*
		 * We have a match that's "good enough".
		 */
		newstart = beststart;
		last = bestlast;
		goto found;
	}

	/*
	 * No space currently available.  Wait for it to free up,
	 * if possible.
	 */
	if (flags & EX_WAITSPACE) {
		ex->ex_flags |= EXF_WANTED;
		error = tsleep_nsec(ex,
		    PRIBIO | ((flags & EX_CATCH) ? PCATCH : 0),
		    "extnt", INFSLP);
		if (error)
			return (error);
		goto alloc_start;
	}

	extent_free_region_descriptor(ex, myrp);
	return (EAGAIN);

 found:
	/*
	 * Insert ourselves into the region list.
	 */
	extent_insert_and_optimize(ex, newstart, size, last, myrp);
	*result = newstart;
	return (0);
}

int
extent_alloc_subregion(struct extent *ex, u_long substart, u_long subend,
    u_long size, u_long alignment, u_long skew, u_long boundary, int flags,
    u_long *result)
{
	struct extent_region *rp;

	/*
	 * Allocate the region descriptor.  It will be freed later
	 * if we can coalesce with another region.
	 */
	rp = extent_alloc_region_descriptor(ex, flags);
	if (rp == NULL) {
#ifdef DIAGNOSTIC
		printf("%s: can't allocate region descriptor\n", __func__);
#endif
		return (ENOMEM);
	}

	return extent_do_alloc(ex, substart, subend, size, alignment, skew,
	    boundary, flags, rp, result);
}

int
extent_alloc_subregion_with_descr(struct extent *ex, u_long substart,
    u_long subend, u_long size, u_long alignment, u_long skew,
    u_long boundary, int flags, struct extent_region *rp, u_long *result)
{
#ifdef DIAGNOSTIC
	if ((ex->ex_flags & EXF_NOCOALESCE) == 0)
		panic("%s: EX_NOCOALESCE not set", __func__);
#endif

	rp->er_flags = ER_DISCARD;
	return extent_do_alloc(ex, substart, subend, size, alignment, skew,
	    boundary, flags, rp, result);
}

int
extent_free(struct extent *ex, u_long start, u_long size, int flags)
{
	struct extent_region *rp, *nrp = NULL;
	struct extent_region *tmp;
	u_long end = start + (size - 1);
	int exflags;
	int error = 0;

#ifdef DIAGNOSTIC
	/* Check arguments. */
	if (ex == NULL)
		panic("%s: NULL extent", __func__);
	if ((start < ex->ex_start) || (end > ex->ex_end)) {
		extent_print(ex);
		printf("%s: extent `%s', start 0x%lx, size 0x%lx\n",
		    __func__, ex->ex_name, start, size);
		panic("%s: extent `%s', region not within extent",
		    __func__, ex->ex_name);
	}
	/* Check for an overflow. */
	if (end < start) {
		extent_print(ex);
		printf("%s: extent `%s', start 0x%lx, size 0x%lx\n",
		    __func__, ex->ex_name, start, size);
		panic("%s: overflow", __func__);
	}
#endif

	/*
	 * If we're allowing coalescing, we must allocate a region
	 * descriptor now, since it might block.
	 *
	 * XXX Make a static, create-time flags word, so we don't
	 * XXX have to lock to read it!
	 */
	exflags = ex->ex_flags;

	if ((exflags & EXF_NOCOALESCE) == 0) {
		/* Allocate a region descriptor. */
		nrp = extent_alloc_region_descriptor(ex, flags);
		if (nrp == NULL)
			return (ENOMEM);
	}

	/*
	 * Find region and deallocate.  Several possibilities:
	 *
	 *	1. (start == er_start) && (end == er_end):
	 *	   Free descriptor.
	 *
	 *	2. (start == er_start) && (end < er_end):
	 *	   Adjust er_start.
	 *
	 *	3. (start > er_start) && (end == er_end):
	 *	   Adjust er_end.
	 *
	 *	4. (start > er_start) && (end < er_end):
	 *	   Fragment region.  Requires descriptor alloc.
	 *
	 * Cases 2, 3, and 4 require that the EXF_NOCOALESCE flag
	 * is not set.
	 *
	 * If the EX_CONFLICTOK flag is set, partially overlapping
	 * regions are allowed.  This is handled in cases 1a, 2a and
	 * 3a below.
	 */
	LIST_FOREACH_SAFE(rp, &ex->ex_regions, er_link, tmp) {
		/*
		 * Save ourselves some comparisons; does the current
		 * region end before chunk to be freed begins?  If so,
		 * then we haven't found the appropriate region descriptor.
		 */
		if (rp->er_end < start)
			continue;

		/*
		 * Save ourselves some traversal; does the current
		 * region begin after the chunk to be freed ends?  If so,
		 * then we've already passed any possible region descriptors
		 * that might have contained the chunk to be freed.
		 */
		if (rp->er_start > end)
			break;

		/* Case 1. */
		if ((start == rp->er_start) && (end == rp->er_end)) {
			LIST_REMOVE(rp, er_link);
			extent_free_region_descriptor(ex, rp);
			goto done;
		}

		/*
		 * The following cases all require that EXF_NOCOALESCE
		 * is not set.
		 */
		if (ex->ex_flags & EXF_NOCOALESCE)
			continue;

		/* Case 2. */
		if ((start == rp->er_start) && (end < rp->er_end)) {
			rp->er_start = (end + 1);
			goto done;
		}

		/* Case 3. */
		if ((start > rp->er_start) && (end == rp->er_end)) {
			rp->er_end = (start - 1);
			goto done;
		}

		/* Case 4. */
		if ((start > rp->er_start) && (end < rp->er_end)) {
			/* Fill in new descriptor. */
			nrp->er_start = end + 1;
			nrp->er_end = rp->er_end;

			/* Adjust current descriptor. */
			rp->er_end = start - 1;

			/* Insert new descriptor after current. */
			LIST_INSERT_AFTER(rp, nrp, er_link);

			/* We used the new descriptor, so don't free it below */
			nrp = NULL;
			goto done;
		}

		if ((flags & EX_CONFLICTOK) == 0)
			continue;

		/* Case 1a. */
		if ((start <= rp->er_start && end >= rp->er_end)) {
			LIST_REMOVE(rp, er_link);
			extent_free_region_descriptor(ex, rp);
			continue;
		}

		/* Case 2a. */
		if ((start <= rp->er_start) && (end >= rp->er_start))
			rp->er_start = (end + 1);

		/* Case 3a. */
		if ((start <= rp->er_end) && (end >= rp->er_end))
			rp->er_end = (start - 1);
	}

	if (flags & EX_CONFLICTOK)
		goto done;

	/* Region not found, or request otherwise invalid. */
#if defined(DIAGNOSTIC) || defined(DDB)
	extent_print(ex);
#endif
	printf("%s: start 0x%lx, end 0x%lx\n", __func__, start, end);
	panic("%s: region not found", __func__);

 done:
	if (nrp != NULL)
		extent_free_region_descriptor(ex, nrp);
	if (ex->ex_flags & EXF_WANTED) {
		ex->ex_flags &= ~EXF_WANTED;
		wakeup(ex);
	}
	return (error);
}

static struct extent_region *
extent_alloc_region_descriptor(struct extent *ex, int flags)
{
	struct extent_region *rp;

	if (ex->ex_flags & EXF_FIXED) {
		struct extent_fixed *fex = (struct extent_fixed *)ex;

		while (LIST_EMPTY(&fex->fex_freelist)) {
			if (flags & EX_MALLOCOK)
				goto alloc;

			if ((flags & EX_WAITOK) == 0)
				return (NULL);
			ex->ex_flags |= EXF_FLWANTED;
			if (tsleep_nsec(&fex->fex_freelist,
			    PRIBIO | ((flags & EX_CATCH) ? PCATCH : 0),
			    "extnt", INFSLP))
				return (NULL);
		}
		rp = LIST_FIRST(&fex->fex_freelist);
		LIST_REMOVE(rp, er_link);

		/*
		 * Don't muck with flags after pulling it off the
		 * freelist; it may be a dynamically allocated
		 * region pointer that was kindly given to us,
		 * and we need to preserve that information.
		 */

		return (rp);
	}

 alloc:
	rp = pool_get(&ex_region_pl, (flags & EX_WAITOK) ? PR_WAITOK :
	    PR_NOWAIT);
	if (rp != NULL)
		rp->er_flags = ER_ALLOC;

	return (rp);
}

static void
extent_free_region_descriptor(struct extent *ex, struct extent_region *rp)
{
	if (rp->er_flags & ER_DISCARD)
		return;

	if (ex->ex_flags & EXF_FIXED) {
		struct extent_fixed *fex = (struct extent_fixed *)ex;

		/*
		 * If someone's waiting for a region descriptor,
		 * be nice and give them this one, rather than
		 * just free'ing it back to the system.
		 */
		if (rp->er_flags & ER_ALLOC) {
			if (ex->ex_flags & EXF_FLWANTED) {
				/* Clear all but ER_ALLOC flag. */
				rp->er_flags = ER_ALLOC;
				LIST_INSERT_HEAD(&fex->fex_freelist, rp,
				    er_link);
				goto wake_em_up;
			} else {
				pool_put(&ex_region_pl, rp);
			}
		} else {
			/* Clear all flags. */
			rp->er_flags = 0;
			LIST_INSERT_HEAD(&fex->fex_freelist, rp, er_link);
		}

		if (ex->ex_flags & EXF_FLWANTED) {
 wake_em_up:
			ex->ex_flags &= ~EXF_FLWANTED;
			wakeup(&fex->fex_freelist);
		}
		return;
	}

	/*
	 * We know it's dynamically allocated if we get here.
	 */
	pool_put(&ex_region_pl, rp);
}


#if defined(DIAGNOSTIC) || defined(DDB) || !defined(_KERNEL)

void
extent_print(struct extent *ex)
{
	extent_print1(ex, printf);
}

void
extent_print1(struct extent *ex,
    int (*pr)(const char *, ...) __attribute__((__format__(__kprintf__,1,2))))
{
	struct extent_region *rp;

	if (ex == NULL)
		panic("%s: NULL extent", __func__);

#ifdef _KERNEL
	(*pr)("extent `%s' (0x%lx - 0x%lx), flags=%b\n", ex->ex_name,
	    ex->ex_start, ex->ex_end, ex->ex_flags, EXF_BITS);
#else
	(*pr)("extent `%s' (0x%lx - 0x%lx), flags = 0x%x\n", ex->ex_name,
	    ex->ex_start, ex->ex_end, ex->ex_flags);
#endif

	LIST_FOREACH(rp, &ex->ex_regions, er_link)
		(*pr)("     0x%lx - 0x%lx\n", rp->er_start, rp->er_end);
}
#endif
