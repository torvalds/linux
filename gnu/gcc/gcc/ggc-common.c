/* Simple garbage collection for the GNU compiler.
   Copyright (C) 1999, 2000, 2001, 2002, 2003, 2004, 2005, 2006
   Free Software Foundation, Inc.

This file is part of GCC.

GCC is free software; you can redistribute it and/or modify it under
the terms of the GNU General Public License as published by the Free
Software Foundation; either version 2, or (at your option) any later
version.

GCC is distributed in the hope that it will be useful, but WITHOUT ANY
WARRANTY; without even the implied warranty of MERCHANTABILITY or
FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
for more details.

You should have received a copy of the GNU General Public License
along with GCC; see the file COPYING.  If not, write to the Free
Software Foundation, 51 Franklin Street, Fifth Floor, Boston, MA
02110-1301, USA.  */

/* Generic garbage collection (GC) functions and data, not specific to
   any particular GC implementation.  */

#include "config.h"
#include "system.h"
#include "coretypes.h"
#include "hashtab.h"
#include "ggc.h"
#include "toplev.h"
#include "params.h"
#include "hosthooks.h"
#include "hosthooks-def.h"

#ifdef HAVE_SYS_RESOURCE_H
# include <sys/resource.h>
#endif

#ifdef HAVE_MMAP_FILE
# include <sys/mman.h>
# ifdef HAVE_MINCORE
/* This is on Solaris.  */
#  include <sys/types.h> 
# endif
#endif

#ifndef MAP_FAILED
# define MAP_FAILED ((void *)-1)
#endif

#ifdef ENABLE_VALGRIND_CHECKING
# ifdef HAVE_VALGRIND_MEMCHECK_H
#  include <valgrind/memcheck.h>
# elif defined HAVE_MEMCHECK_H
#  include <memcheck.h>
# else
#  include <valgrind.h>
# endif
#else
/* Avoid #ifdef:s when we can help it.  */
#define VALGRIND_DISCARD(x)
#endif

/* When set, ggc_collect will do collection.  */
bool ggc_force_collect;

/* Statistics about the allocation.  */
static ggc_statistics *ggc_stats;

struct traversal_state;

static int ggc_htab_delete (void **, void *);
static hashval_t saving_htab_hash (const void *);
static int saving_htab_eq (const void *, const void *);
static int call_count (void **, void *);
static int call_alloc (void **, void *);
static int compare_ptr_data (const void *, const void *);
static void relocate_ptrs (void *, void *);
static void write_pch_globals (const struct ggc_root_tab * const *tab,
			       struct traversal_state *state);
static double ggc_rlimit_bound (double);

/* Maintain global roots that are preserved during GC.  */

/* Process a slot of an htab by deleting it if it has not been marked.  */

static int
ggc_htab_delete (void **slot, void *info)
{
  const struct ggc_cache_tab *r = (const struct ggc_cache_tab *) info;

  if (! (*r->marked_p) (*slot))
    htab_clear_slot (*r->base, slot);
  else
    (*r->cb) (*slot);

  return 1;
}

/* Iterate through all registered roots and mark each element.  */

void
ggc_mark_roots (void)
{
  const struct ggc_root_tab *const *rt;
  const struct ggc_root_tab *rti;
  const struct ggc_cache_tab *const *ct;
  const struct ggc_cache_tab *cti;
  size_t i;

  for (rt = gt_ggc_deletable_rtab; *rt; rt++)
    for (rti = *rt; rti->base != NULL; rti++)
      memset (rti->base, 0, rti->stride);

  for (rt = gt_ggc_rtab; *rt; rt++)
    for (rti = *rt; rti->base != NULL; rti++)
      for (i = 0; i < rti->nelt; i++)
	(*rti->cb)(*(void **)((char *)rti->base + rti->stride * i));

  ggc_mark_stringpool ();

  /* Now scan all hash tables that have objects which are to be deleted if
     they are not already marked.  */
  for (ct = gt_ggc_cache_rtab; *ct; ct++)
    for (cti = *ct; cti->base != NULL; cti++)
      if (*cti->base)
	{
	  ggc_set_mark (*cti->base);
	  htab_traverse_noresize (*cti->base, ggc_htab_delete, (void *) cti);
	  ggc_set_mark ((*cti->base)->entries);
	}
}

/* Allocate a block of memory, then clear it.  */
void *
ggc_alloc_cleared_stat (size_t size MEM_STAT_DECL)
{
  void *buf = ggc_alloc_stat (size PASS_MEM_STAT);
  memset (buf, 0, size);
  return buf;
}

/* Resize a block of memory, possibly re-allocating it.  */
void *
ggc_realloc_stat (void *x, size_t size MEM_STAT_DECL)
{
  void *r;
  size_t old_size;

  if (x == NULL)
    return ggc_alloc_stat (size PASS_MEM_STAT);

  old_size = ggc_get_size (x);

  if (size <= old_size)
    {
      /* Mark the unwanted memory as unaccessible.  We also need to make
	 the "new" size accessible, since ggc_get_size returns the size of
	 the pool, not the size of the individually allocated object, the
	 size which was previously made accessible.  Unfortunately, we
	 don't know that previously allocated size.  Without that
	 knowledge we have to lose some initialization-tracking for the
	 old parts of the object.  An alternative is to mark the whole
	 old_size as reachable, but that would lose tracking of writes
	 after the end of the object (by small offsets).  Discard the
	 handle to avoid handle leak.  */
      VALGRIND_DISCARD (VALGRIND_MAKE_NOACCESS ((char *) x + size,
						old_size - size));
      VALGRIND_DISCARD (VALGRIND_MAKE_READABLE (x, size));
      return x;
    }

  r = ggc_alloc_stat (size PASS_MEM_STAT);

  /* Since ggc_get_size returns the size of the pool, not the size of the
     individually allocated object, we'd access parts of the old object
     that were marked invalid with the memcpy below.  We lose a bit of the
     initialization-tracking since some of it may be uninitialized.  */
  VALGRIND_DISCARD (VALGRIND_MAKE_READABLE (x, old_size));

  memcpy (r, x, old_size);

  /* The old object is not supposed to be used anymore.  */
  ggc_free (x);

  return r;
}

/* Like ggc_alloc_cleared, but performs a multiplication.  */
void *
ggc_calloc (size_t s1, size_t s2)
{
  return ggc_alloc_cleared (s1 * s2);
}

/* These are for splay_tree_new_ggc.  */
void *
ggc_splay_alloc (int sz, void *nl)
{
  gcc_assert (!nl);
  return ggc_alloc (sz);
}

void
ggc_splay_dont_free (void * x ATTRIBUTE_UNUSED, void *nl)
{
  gcc_assert (!nl);
}

/* Print statistics that are independent of the collector in use.  */
#define SCALE(x) ((unsigned long) ((x) < 1024*10 \
		  ? (x) \
		  : ((x) < 1024*1024*10 \
		     ? (x) / 1024 \
		     : (x) / (1024*1024))))
#define LABEL(x) ((x) < 1024*10 ? ' ' : ((x) < 1024*1024*10 ? 'k' : 'M'))

void
ggc_print_common_statistics (FILE *stream ATTRIBUTE_UNUSED,
			     ggc_statistics *stats)
{
  /* Set the pointer so that during collection we will actually gather
     the statistics.  */
  ggc_stats = stats;

  /* Then do one collection to fill in the statistics.  */
  ggc_collect ();

  /* At present, we don't really gather any interesting statistics.  */

  /* Don't gather statistics any more.  */
  ggc_stats = NULL;
}

/* Functions for saving and restoring GCable memory to disk.  */

static htab_t saving_htab;

struct ptr_data
{
  void *obj;
  void *note_ptr_cookie;
  gt_note_pointers note_ptr_fn;
  gt_handle_reorder reorder_fn;
  size_t size;
  void *new_addr;
  enum gt_types_enum type;
};

#define POINTER_HASH(x) (hashval_t)((long)x >> 3)

/* Register an object in the hash table.  */

int
gt_pch_note_object (void *obj, void *note_ptr_cookie,
		    gt_note_pointers note_ptr_fn,
		    enum gt_types_enum type)
{
  struct ptr_data **slot;

  if (obj == NULL || obj == (void *) 1)
    return 0;

  slot = (struct ptr_data **)
    htab_find_slot_with_hash (saving_htab, obj, POINTER_HASH (obj),
			      INSERT);
  if (*slot != NULL)
    {
      gcc_assert ((*slot)->note_ptr_fn == note_ptr_fn
		  && (*slot)->note_ptr_cookie == note_ptr_cookie);
      return 0;
    }

  *slot = xcalloc (sizeof (struct ptr_data), 1);
  (*slot)->obj = obj;
  (*slot)->note_ptr_fn = note_ptr_fn;
  (*slot)->note_ptr_cookie = note_ptr_cookie;
  if (note_ptr_fn == gt_pch_p_S)
    (*slot)->size = strlen (obj) + 1;
  else
    (*slot)->size = ggc_get_size (obj);
  (*slot)->type = type;
  return 1;
}

/* Register an object in the hash table.  */

void
gt_pch_note_reorder (void *obj, void *note_ptr_cookie,
		     gt_handle_reorder reorder_fn)
{
  struct ptr_data *data;

  if (obj == NULL || obj == (void *) 1)
    return;

  data = htab_find_with_hash (saving_htab, obj, POINTER_HASH (obj));
  gcc_assert (data && data->note_ptr_cookie == note_ptr_cookie);

  data->reorder_fn = reorder_fn;
}

/* Hash and equality functions for saving_htab, callbacks for htab_create.  */

static hashval_t
saving_htab_hash (const void *p)
{
  return POINTER_HASH (((struct ptr_data *)p)->obj);
}

static int
saving_htab_eq (const void *p1, const void *p2)
{
  return ((struct ptr_data *)p1)->obj == p2;
}

/* Handy state for the traversal functions.  */

struct traversal_state
{
  FILE *f;
  struct ggc_pch_data *d;
  size_t count;
  struct ptr_data **ptrs;
  size_t ptrs_i;
};

/* Callbacks for htab_traverse.  */

static int
call_count (void **slot, void *state_p)
{
  struct ptr_data *d = (struct ptr_data *)*slot;
  struct traversal_state *state = (struct traversal_state *)state_p;

  ggc_pch_count_object (state->d, d->obj, d->size,
			d->note_ptr_fn == gt_pch_p_S,
			d->type);
  state->count++;
  return 1;
}

static int
call_alloc (void **slot, void *state_p)
{
  struct ptr_data *d = (struct ptr_data *)*slot;
  struct traversal_state *state = (struct traversal_state *)state_p;

  d->new_addr = ggc_pch_alloc_object (state->d, d->obj, d->size,
				      d->note_ptr_fn == gt_pch_p_S,
				      d->type);
  state->ptrs[state->ptrs_i++] = d;
  return 1;
}

/* Callback for qsort.  */

static int
compare_ptr_data (const void *p1_p, const void *p2_p)
{
  struct ptr_data *p1 = *(struct ptr_data *const *)p1_p;
  struct ptr_data *p2 = *(struct ptr_data *const *)p2_p;
  return (((size_t)p1->new_addr > (size_t)p2->new_addr)
	  - ((size_t)p1->new_addr < (size_t)p2->new_addr));
}

/* Callbacks for note_ptr_fn.  */

static void
relocate_ptrs (void *ptr_p, void *state_p)
{
  void **ptr = (void **)ptr_p;
  struct traversal_state *state ATTRIBUTE_UNUSED
    = (struct traversal_state *)state_p;
  struct ptr_data *result;

  if (*ptr == NULL || *ptr == (void *)1)
    return;

  result = htab_find_with_hash (saving_htab, *ptr, POINTER_HASH (*ptr));
  gcc_assert (result);
  *ptr = result->new_addr;
}

/* Write out, after relocation, the pointers in TAB.  */
static void
write_pch_globals (const struct ggc_root_tab * const *tab,
		   struct traversal_state *state)
{
  const struct ggc_root_tab *const *rt;
  const struct ggc_root_tab *rti;
  size_t i;

  for (rt = tab; *rt; rt++)
    for (rti = *rt; rti->base != NULL; rti++)
      for (i = 0; i < rti->nelt; i++)
	{
	  void *ptr = *(void **)((char *)rti->base + rti->stride * i);
	  struct ptr_data *new_ptr;
	  if (ptr == NULL || ptr == (void *)1)
	    {
	      if (fwrite (&ptr, sizeof (void *), 1, state->f)
		  != 1)
		fatal_error ("can't write PCH file: %m");
	    }
	  else
	    {
	      new_ptr = htab_find_with_hash (saving_htab, ptr,
					     POINTER_HASH (ptr));
	      if (fwrite (&new_ptr->new_addr, sizeof (void *), 1, state->f)
		  != 1)
		fatal_error ("can't write PCH file: %m");
	    }
	}
}

/* Hold the information we need to mmap the file back in.  */

struct mmap_info
{
  size_t offset;
  size_t size;
  void *preferred_base;
};

/* Write out the state of the compiler to F.  */

void
gt_pch_save (FILE *f)
{
  const struct ggc_root_tab *const *rt;
  const struct ggc_root_tab *rti;
  size_t i;
  struct traversal_state state;
  char *this_object = NULL;
  size_t this_object_size = 0;
  struct mmap_info mmi;
  const size_t mmap_offset_alignment = host_hooks.gt_pch_alloc_granularity();

  gt_pch_save_stringpool ();

  saving_htab = htab_create (50000, saving_htab_hash, saving_htab_eq, free);

  for (rt = gt_ggc_rtab; *rt; rt++)
    for (rti = *rt; rti->base != NULL; rti++)
      for (i = 0; i < rti->nelt; i++)
	(*rti->pchw)(*(void **)((char *)rti->base + rti->stride * i));

  for (rt = gt_pch_cache_rtab; *rt; rt++)
    for (rti = *rt; rti->base != NULL; rti++)
      for (i = 0; i < rti->nelt; i++)
	(*rti->pchw)(*(void **)((char *)rti->base + rti->stride * i));

  /* Prepare the objects for writing, determine addresses and such.  */
  state.f = f;
  state.d = init_ggc_pch();
  state.count = 0;
  htab_traverse (saving_htab, call_count, &state);

  mmi.size = ggc_pch_total_size (state.d);

  /* Try to arrange things so that no relocation is necessary, but
     don't try very hard.  On most platforms, this will always work,
     and on the rest it's a lot of work to do better.  
     (The extra work goes in HOST_HOOKS_GT_PCH_GET_ADDRESS and
     HOST_HOOKS_GT_PCH_USE_ADDRESS.)  */
  mmi.preferred_base = host_hooks.gt_pch_get_address (mmi.size, fileno (f));
      
  ggc_pch_this_base (state.d, mmi.preferred_base);

  state.ptrs = XNEWVEC (struct ptr_data *, state.count);
  state.ptrs_i = 0;
  htab_traverse (saving_htab, call_alloc, &state);
  qsort (state.ptrs, state.count, sizeof (*state.ptrs), compare_ptr_data);

  /* Write out all the scalar variables.  */
  for (rt = gt_pch_scalar_rtab; *rt; rt++)
    for (rti = *rt; rti->base != NULL; rti++)
      if (fwrite (rti->base, rti->stride, 1, f) != 1)
	fatal_error ("can't write PCH file: %m");

  /* Write out all the global pointers, after translation.  */
  write_pch_globals (gt_ggc_rtab, &state);
  write_pch_globals (gt_pch_cache_rtab, &state);

  /* Pad the PCH file so that the mmapped area starts on an allocation
     granularity (usually page) boundary.  */
  {
    long o;
    o = ftell (state.f) + sizeof (mmi);
    if (o == -1)
      fatal_error ("can't get position in PCH file: %m");
    mmi.offset = mmap_offset_alignment - o % mmap_offset_alignment;
    if (mmi.offset == mmap_offset_alignment)
      mmi.offset = 0;
    mmi.offset += o;
  }
  if (fwrite (&mmi, sizeof (mmi), 1, state.f) != 1)
    fatal_error ("can't write PCH file: %m");
  if (mmi.offset != 0
      && fseek (state.f, mmi.offset, SEEK_SET) != 0)
    fatal_error ("can't write padding to PCH file: %m");

  ggc_pch_prepare_write (state.d, state.f);

  /* Actually write out the objects.  */
  for (i = 0; i < state.count; i++)
    {
      if (this_object_size < state.ptrs[i]->size)
	{
	  this_object_size = state.ptrs[i]->size;
	  this_object = xrealloc (this_object, this_object_size);
	}
      memcpy (this_object, state.ptrs[i]->obj, state.ptrs[i]->size);
      if (state.ptrs[i]->reorder_fn != NULL)
	state.ptrs[i]->reorder_fn (state.ptrs[i]->obj,
				   state.ptrs[i]->note_ptr_cookie,
				   relocate_ptrs, &state);
      state.ptrs[i]->note_ptr_fn (state.ptrs[i]->obj,
				  state.ptrs[i]->note_ptr_cookie,
				  relocate_ptrs, &state);
      ggc_pch_write_object (state.d, state.f, state.ptrs[i]->obj,
			    state.ptrs[i]->new_addr, state.ptrs[i]->size,
			    state.ptrs[i]->note_ptr_fn == gt_pch_p_S);
      if (state.ptrs[i]->note_ptr_fn != gt_pch_p_S)
	memcpy (state.ptrs[i]->obj, this_object, state.ptrs[i]->size);
    }
  ggc_pch_finish (state.d, state.f);
  gt_pch_fixup_stringpool ();

  free (state.ptrs);
  htab_delete (saving_htab);
}

/* Read the state of the compiler back in from F.  */

void
gt_pch_restore (FILE *f)
{
  const struct ggc_root_tab *const *rt;
  const struct ggc_root_tab *rti;
  size_t i;
  struct mmap_info mmi;
  int result;

  /* Delete any deletable objects.  This makes ggc_pch_read much
     faster, as it can be sure that no GCable objects remain other
     than the ones just read in.  */
  for (rt = gt_ggc_deletable_rtab; *rt; rt++)
    for (rti = *rt; rti->base != NULL; rti++)
      memset (rti->base, 0, rti->stride);

  /* Read in all the scalar variables.  */
  for (rt = gt_pch_scalar_rtab; *rt; rt++)
    for (rti = *rt; rti->base != NULL; rti++)
      if (fread (rti->base, rti->stride, 1, f) != 1)
	fatal_error ("can't read PCH file: %m");

  /* Read in all the global pointers, in 6 easy loops.  */
  for (rt = gt_ggc_rtab; *rt; rt++)
    for (rti = *rt; rti->base != NULL; rti++)
      for (i = 0; i < rti->nelt; i++)
	if (fread ((char *)rti->base + rti->stride * i,
		   sizeof (void *), 1, f) != 1)
	  fatal_error ("can't read PCH file: %m");

  for (rt = gt_pch_cache_rtab; *rt; rt++)
    for (rti = *rt; rti->base != NULL; rti++)
      for (i = 0; i < rti->nelt; i++)
	if (fread ((char *)rti->base + rti->stride * i,
		   sizeof (void *), 1, f) != 1)
	  fatal_error ("can't read PCH file: %m");

  if (fread (&mmi, sizeof (mmi), 1, f) != 1)
    fatal_error ("can't read PCH file: %m");

  result = host_hooks.gt_pch_use_address (mmi.preferred_base, mmi.size,
					  fileno (f), mmi.offset);
  if (result < 0)
    fatal_error ("had to relocate PCH");
  if (result == 0)
    {
      if (fseek (f, mmi.offset, SEEK_SET) != 0
	  || fread (mmi.preferred_base, mmi.size, 1, f) != 1)
	fatal_error ("can't read PCH file: %m");
    }
  else if (fseek (f, mmi.offset + mmi.size, SEEK_SET) != 0)
    fatal_error ("can't read PCH file: %m");

  ggc_pch_read (f, mmi.preferred_base);

  gt_pch_restore_stringpool ();
}

/* Default version of HOST_HOOKS_GT_PCH_GET_ADDRESS when mmap is not present.
   Select no address whatsoever, and let gt_pch_save choose what it will with
   malloc, presumably.  */

void *
default_gt_pch_get_address (size_t size ATTRIBUTE_UNUSED,
			    int fd ATTRIBUTE_UNUSED)
{
  return NULL;
}

/* Default version of HOST_HOOKS_GT_PCH_USE_ADDRESS when mmap is not present.
   Allocate SIZE bytes with malloc.  Return 0 if the address we got is the
   same as base, indicating that the memory has been allocated but needs to
   be read in from the file.  Return -1 if the address differs, to relocation
   of the PCH file would be required.  */

int
default_gt_pch_use_address (void *base, size_t size, int fd ATTRIBUTE_UNUSED,
			    size_t offset ATTRIBUTE_UNUSED)
{
  void *addr = xmalloc (size);
  return (addr == base) - 1;
}

/* Default version of HOST_HOOKS_GT_PCH_GET_ADDRESS.   Return the
   alignment required for allocating virtual memory. Usually this is the
   same as pagesize.  */

size_t
default_gt_pch_alloc_granularity (void)
{
  return getpagesize();
}

#if HAVE_MMAP_FILE
/* Default version of HOST_HOOKS_GT_PCH_GET_ADDRESS when mmap is present.
   We temporarily allocate SIZE bytes, and let the kernel place the data
   wherever it will.  If it worked, that's our spot, if not we're likely
   to be in trouble.  */

void *
mmap_gt_pch_get_address (size_t size, int fd)
{
  void *ret;

  ret = mmap (NULL, size, PROT_READ | PROT_WRITE, MAP_PRIVATE, fd, 0);
  if (ret == (void *) MAP_FAILED)
    ret = NULL;
  else
    munmap (ret, size);

  return ret;
}

/* Default version of HOST_HOOKS_GT_PCH_USE_ADDRESS when mmap is present.
   Map SIZE bytes of FD+OFFSET at BASE.  Return 1 if we succeeded at 
   mapping the data at BASE, -1 if we couldn't.

   This version assumes that the kernel honors the START operand of mmap
   even without MAP_FIXED if START through START+SIZE are not currently
   mapped with something.  */

int
mmap_gt_pch_use_address (void *base, size_t size, int fd, size_t offset)
{
  void *addr;

  /* We're called with size == 0 if we're not planning to load a PCH
     file at all.  This allows the hook to free any static space that
     we might have allocated at link time.  */
  if (size == 0)
    return -1;

  addr = mmap (base, size, PROT_READ | PROT_WRITE, MAP_PRIVATE,
	       fd, offset);

  return addr == base ? 1 : -1;
}
#endif /* HAVE_MMAP_FILE */

/* Modify the bound based on rlimits.  */
static double
ggc_rlimit_bound (double limit)
{
#if defined(HAVE_GETRLIMIT)
  struct rlimit rlim;
# if defined (RLIMIT_AS)
  /* RLIMIT_AS is what POSIX says is the limit on mmap.  Presumably
     any OS which has RLIMIT_AS also has a working mmap that GCC will use.  */
  if (getrlimit (RLIMIT_AS, &rlim) == 0
      && rlim.rlim_cur != (rlim_t) RLIM_INFINITY
      && rlim.rlim_cur < limit)
    limit = rlim.rlim_cur;
# elif defined (RLIMIT_DATA)
  /* ... but some older OSs bound mmap based on RLIMIT_DATA, or we
     might be on an OS that has a broken mmap.  (Others don't bound
     mmap at all, apparently.)  */
  if (getrlimit (RLIMIT_DATA, &rlim) == 0
      && rlim.rlim_cur != (rlim_t) RLIM_INFINITY
      && rlim.rlim_cur < limit
      /* Darwin has this horribly bogus default setting of
	 RLIMIT_DATA, to 6144Kb.  No-one notices because RLIMIT_DATA
	 appears to be ignored.  Ignore such silliness.  If a limit
	 this small was actually effective for mmap, GCC wouldn't even
	 start up.  */
      && rlim.rlim_cur >= 8 * 1024 * 1024)
    limit = rlim.rlim_cur;
# endif /* RLIMIT_AS or RLIMIT_DATA */
#endif /* HAVE_GETRLIMIT */

  return limit;
}

/* Heuristic to set a default for GGC_MIN_EXPAND.  */
int
ggc_min_expand_heuristic (void)
{
  double min_expand = physmem_total();

  /* Adjust for rlimits.  */
  min_expand = ggc_rlimit_bound (min_expand);

  /* The heuristic is a percentage equal to 30% + 70%*(RAM/1GB), yielding
     a lower bound of 30% and an upper bound of 100% (when RAM >= 1GB).  */
  min_expand /= 1024*1024*1024;
  min_expand *= 70;
  min_expand = MIN (min_expand, 70);
  min_expand += 30;

  return min_expand;
}

/* Heuristic to set a default for GGC_MIN_HEAPSIZE.  */
int
ggc_min_heapsize_heuristic (void)
{
  double phys_kbytes = physmem_total();
  double limit_kbytes = ggc_rlimit_bound (phys_kbytes * 2);

  phys_kbytes /= 1024; /* Convert to Kbytes.  */
  limit_kbytes /= 1024;

  /* The heuristic is RAM/8, with a lower bound of 4M and an upper
     bound of 128M (when RAM >= 1GB).  */
  phys_kbytes /= 8;

#if defined(HAVE_GETRLIMIT) && defined (RLIMIT_RSS)
  /* Try not to overrun the RSS limit while doing garbage collection.  
     The RSS limit is only advisory, so no margin is subtracted.  */
 {
   struct rlimit rlim;
   if (getrlimit (RLIMIT_RSS, &rlim) == 0
       && rlim.rlim_cur != (rlim_t) RLIM_INFINITY)
     phys_kbytes = MIN (phys_kbytes, rlim.rlim_cur / 1024);
 }
# endif

  /* Don't blindly run over our data limit; do GC at least when the
     *next* GC would be within 16Mb of the limit.  If GCC does hit the
     data limit, compilation will fail, so this tries to be
     conservative.  */
  limit_kbytes = MAX (0, limit_kbytes - 16 * 1024);
  limit_kbytes = (limit_kbytes * 100) / (110 + ggc_min_expand_heuristic());
  phys_kbytes = MIN (phys_kbytes, limit_kbytes);

  phys_kbytes = MAX (phys_kbytes, 4 * 1024);
  phys_kbytes = MIN (phys_kbytes, 128 * 1024);

  return phys_kbytes;
}

void
init_ggc_heuristics (void)
{
#if !defined ENABLE_GC_CHECKING && !defined ENABLE_GC_ALWAYS_COLLECT
  set_param_value ("ggc-min-expand", ggc_min_expand_heuristic());
  set_param_value ("ggc-min-heapsize", ggc_min_heapsize_heuristic());
#endif
}

#ifdef GATHER_STATISTICS

/* Datastructure used to store per-call-site statistics.  */
struct loc_descriptor
{
  const char *file;
  int line;
  const char *function;
  int times;
  size_t allocated;
  size_t overhead;
  size_t freed;
  size_t collected;
};

/* Hashtable used for statistics.  */
static htab_t loc_hash;

/* Hash table helpers functions.  */
static hashval_t
hash_descriptor (const void *p)
{
  const struct loc_descriptor *d = p;

  return htab_hash_pointer (d->function) | d->line;
}

static int
eq_descriptor (const void *p1, const void *p2)
{
  const struct loc_descriptor *d = p1;
  const struct loc_descriptor *d2 = p2;

  return (d->file == d2->file && d->line == d2->line
	  && d->function == d2->function);
}

/* Hashtable converting address of allocated field to loc descriptor.  */
static htab_t ptr_hash;
struct ptr_hash_entry
{
  void *ptr;
  struct loc_descriptor *loc;
  size_t size;
};

/* Hash table helpers functions.  */
static hashval_t
hash_ptr (const void *p)
{
  const struct ptr_hash_entry *d = p;

  return htab_hash_pointer (d->ptr);
}

static int
eq_ptr (const void *p1, const void *p2)
{
  const struct ptr_hash_entry *p = p1;

  return (p->ptr == p2);
}

/* Return descriptor for given call site, create new one if needed.  */
static struct loc_descriptor *
loc_descriptor (const char *name, int line, const char *function)
{
  struct loc_descriptor loc;
  struct loc_descriptor **slot;

  loc.file = name;
  loc.line = line;
  loc.function = function;
  if (!loc_hash)
    loc_hash = htab_create (10, hash_descriptor, eq_descriptor, NULL);

  slot = (struct loc_descriptor **) htab_find_slot (loc_hash, &loc, 1);
  if (*slot)
    return *slot;
  *slot = xcalloc (sizeof (**slot), 1);
  (*slot)->file = name;
  (*slot)->line = line;
  (*slot)->function = function;
  return *slot;
}

/* Record ALLOCATED and OVERHEAD bytes to descriptor NAME:LINE (FUNCTION).  */
void
ggc_record_overhead (size_t allocated, size_t overhead, void *ptr,
		     const char *name, int line, const char *function)
{
  struct loc_descriptor *loc = loc_descriptor (name, line, function);
  struct ptr_hash_entry *p = XNEW (struct ptr_hash_entry);
  PTR *slot;

  p->ptr = ptr;
  p->loc = loc;
  p->size = allocated + overhead;
  if (!ptr_hash)
    ptr_hash = htab_create (10, hash_ptr, eq_ptr, NULL);
  slot = htab_find_slot_with_hash (ptr_hash, ptr, htab_hash_pointer (ptr), INSERT);
  gcc_assert (!*slot);
  *slot = p;

  loc->times++;
  loc->allocated+=allocated;
  loc->overhead+=overhead;
}

/* Helper function for prune_overhead_list.  See if SLOT is still marked and
   remove it from hashtable if it is not.  */
static int
ggc_prune_ptr (void **slot, void *b ATTRIBUTE_UNUSED)
{
  struct ptr_hash_entry *p = *slot;
  if (!ggc_marked_p (p->ptr))
    {
      p->loc->collected += p->size;
      htab_clear_slot (ptr_hash, slot);
      free (p);
    }
  return 1;
}

/* After live values has been marked, walk all recorded pointers and see if
   they are still live.  */
void
ggc_prune_overhead_list (void)
{
  htab_traverse (ptr_hash, ggc_prune_ptr, NULL);
}

/* Notice that the pointer has been freed.  */
void
ggc_free_overhead (void *ptr)
{
  PTR *slot = htab_find_slot_with_hash (ptr_hash, ptr, htab_hash_pointer (ptr),
					NO_INSERT);
  struct ptr_hash_entry *p = *slot;
  p->loc->freed += p->size;
  htab_clear_slot (ptr_hash, slot);
  free (p);
}

/* Helper for qsort; sort descriptors by amount of memory consumed.  */
static int
cmp_statistic (const void *loc1, const void *loc2)
{
  struct loc_descriptor *l1 = *(struct loc_descriptor **) loc1;
  struct loc_descriptor *l2 = *(struct loc_descriptor **) loc2;
  return ((l1->allocated + l1->overhead - l1->freed) -
	  (l2->allocated + l2->overhead - l2->freed));
}

/* Collect array of the descriptors from hashtable.  */
struct loc_descriptor **loc_array;
static int
add_statistics (void **slot, void *b)
{
  int *n = (int *)b;
  loc_array[*n] = (struct loc_descriptor *) *slot;
  (*n)++;
  return 1;
}

/* Dump per-site memory statistics.  */
#endif
void
dump_ggc_loc_statistics (void)
{
#ifdef GATHER_STATISTICS
  int nentries = 0;
  char s[4096];
  size_t collected = 0, freed = 0, allocated = 0, overhead = 0, times = 0;
  int i;

  ggc_force_collect = true;
  ggc_collect ();

  loc_array = xcalloc (sizeof (*loc_array), loc_hash->n_elements);
  fprintf (stderr, "-------------------------------------------------------\n");
  fprintf (stderr, "\n%-48s %10s       %10s       %10s       %10s       %10s\n",
	   "source location", "Garbage", "Freed", "Leak", "Overhead", "Times");
  fprintf (stderr, "-------------------------------------------------------\n");
  htab_traverse (loc_hash, add_statistics, &nentries);
  qsort (loc_array, nentries, sizeof (*loc_array), cmp_statistic);
  for (i = 0; i < nentries; i++)
    {
      struct loc_descriptor *d = loc_array[i];
      allocated += d->allocated;
      times += d->times;
      freed += d->freed;
      collected += d->collected;
      overhead += d->overhead;
    }
  for (i = 0; i < nentries; i++)
    {
      struct loc_descriptor *d = loc_array[i];
      if (d->allocated)
	{
	  const char *s1 = d->file;
	  const char *s2;
	  while ((s2 = strstr (s1, "gcc/")))
	    s1 = s2 + 4;
	  sprintf (s, "%s:%i (%s)", s1, d->line, d->function);
	  s[48] = 0;
	  fprintf (stderr, "%-48s %10li:%4.1f%% %10li:%4.1f%% %10li:%4.1f%% %10li:%4.1f%% %10li\n", s,
		   (long)d->collected,
		   (d->collected) * 100.0 / collected,
		   (long)d->freed,
		   (d->freed) * 100.0 / freed,
		   (long)(d->allocated + d->overhead - d->freed - d->collected),
		   (d->allocated + d->overhead - d->freed - d->collected) * 100.0
		   / (allocated + overhead - freed - collected),
		   (long)d->overhead,
		   d->overhead * 100.0 / overhead,
		   (long)d->times);
	}
    }
  fprintf (stderr, "%-48s %10ld       %10ld       %10ld       %10ld       %10ld\n",
	   "Total", (long)collected, (long)freed,
	   (long)(allocated + overhead - freed - collected), (long)overhead,
	   (long)times);
  fprintf (stderr, "%-48s %10s       %10s       %10s       %10s       %10s\n",
	   "source location", "Garbage", "Freed", "Leak", "Overhead", "Times");
  fprintf (stderr, "-------------------------------------------------------\n");
#endif
}
