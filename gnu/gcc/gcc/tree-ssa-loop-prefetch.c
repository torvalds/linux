/* Array prefetching.
   Copyright (C) 2005 Free Software Foundation, Inc.
   
This file is part of GCC.
   
GCC is free software; you can redistribute it and/or modify it
under the terms of the GNU General Public License as published by the
Free Software Foundation; either version 2, or (at your option) any
later version.
   
GCC is distributed in the hope that it will be useful, but WITHOUT
ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
for more details.
   
You should have received a copy of the GNU General Public License
along with GCC; see the file COPYING.  If not, write to the Free
Software Foundation, 59 Temple Place - Suite 330, Boston, MA
02111-1307, USA.  */

#include "config.h"
#include "system.h"
#include "coretypes.h"
#include "tm.h"
#include "tree.h"
#include "rtl.h"
#include "tm_p.h"
#include "hard-reg-set.h"
#include "basic-block.h"
#include "output.h"
#include "diagnostic.h"
#include "tree-flow.h"
#include "tree-dump.h"
#include "timevar.h"
#include "cfgloop.h"
#include "varray.h"
#include "expr.h"
#include "tree-pass.h"
#include "ggc.h"
#include "insn-config.h"
#include "recog.h"
#include "hashtab.h"
#include "tree-chrec.h"
#include "tree-scalar-evolution.h"
#include "toplev.h"
#include "params.h"
#include "langhooks.h"

/* This pass inserts prefetch instructions to optimize cache usage during
   accesses to arrays in loops.  It processes loops sequentially and:

   1) Gathers all memory references in the single loop.
   2) For each of the references it decides when it is profitable to prefetch
      it.  To do it, we evaluate the reuse among the accesses, and determines
      two values: PREFETCH_BEFORE (meaning that it only makes sense to do
      prefetching in the first PREFETCH_BEFORE iterations of the loop) and
      PREFETCH_MOD (meaning that it only makes sense to prefetch in the
      iterations of the loop that are zero modulo PREFETCH_MOD).  For example
      (assuming cache line size is 64 bytes, char has size 1 byte and there
      is no hardware sequential prefetch):

      char *a;
      for (i = 0; i < max; i++)
	{
	  a[255] = ...;		(0)
	  a[i] = ...;		(1)
	  a[i + 64] = ...;	(2)
	  a[16*i] = ...;	(3)
	  a[187*i] = ...;	(4)
	  a[187*i + 50] = ...;	(5)
	}

       (0) obviously has PREFETCH_BEFORE 1
       (1) has PREFETCH_BEFORE 64, since (2) accesses the same memory
           location 64 iterations before it, and PREFETCH_MOD 64 (since
	   it hits the same cache line otherwise).
       (2) has PREFETCH_MOD 64
       (3) has PREFETCH_MOD 4
       (4) has PREFETCH_MOD 1.  We do not set PREFETCH_BEFORE here, since
           the cache line accessed by (4) is the same with probability only
	   7/32.
       (5) has PREFETCH_MOD 1 as well.

   3) We determine how much ahead we need to prefetch.  The number of
      iterations needed is time to fetch / time spent in one iteration of
      the loop.  The problem is that we do not know either of these values,
      so we just make a heuristic guess based on a magic (possibly)
      target-specific constant and size of the loop.

   4) Determine which of the references we prefetch.  We take into account
      that there is a maximum number of simultaneous prefetches (provided
      by machine description).  We prefetch as many prefetches as possible
      while still within this bound (starting with those with lowest
      prefetch_mod, since they are responsible for most of the cache
      misses).
      
   5) We unroll and peel loops so that we are able to satisfy PREFETCH_MOD
      and PREFETCH_BEFORE requirements (within some bounds), and to avoid
      prefetching nonaccessed memory.
      TODO -- actually implement peeling.
      
   6) We actually emit the prefetch instructions.  ??? Perhaps emit the
      prefetch instructions with guards in cases where 5) was not sufficient
      to satisfy the constraints?

   Some other TODO:
      -- write and use more general reuse analysis (that could be also used
	 in other cache aimed loop optimizations)
      -- make it behave sanely together with the prefetches given by user
	 (now we just ignore them; at the very least we should avoid
	 optimizing loops in that user put his own prefetches)
      -- we assume cache line size alignment of arrays; this could be
	 improved.  */

/* Magic constants follow.  These should be replaced by machine specific
   numbers.  */

/* A number that should roughly correspond to the number of instructions
   executed before the prefetch is completed.  */

#ifndef PREFETCH_LATENCY
#define PREFETCH_LATENCY 200
#endif

/* Number of prefetches that can run at the same time.  */

#ifndef SIMULTANEOUS_PREFETCHES
#define SIMULTANEOUS_PREFETCHES 3
#endif

/* True if write can be prefetched by a read prefetch.  */

#ifndef WRITE_CAN_USE_READ_PREFETCH
#define WRITE_CAN_USE_READ_PREFETCH 1
#endif

/* True if read can be prefetched by a write prefetch. */

#ifndef READ_CAN_USE_WRITE_PREFETCH
#define READ_CAN_USE_WRITE_PREFETCH 0
#endif

/* Cache line size.  Assumed to be a power of two.  */

#ifndef PREFETCH_BLOCK
#define PREFETCH_BLOCK 32
#endif

/* Do we have a forward hardware sequential prefetching?  */

#ifndef HAVE_FORWARD_PREFETCH
#define HAVE_FORWARD_PREFETCH 0
#endif

/* Do we have a backward hardware sequential prefetching?  */

#ifndef HAVE_BACKWARD_PREFETCH
#define HAVE_BACKWARD_PREFETCH 0
#endif

/* In some cases we are only able to determine that there is a certain
   probability that the two accesses hit the same cache line.  In this
   case, we issue the prefetches for both of them if this probability
   is less then (1000 - ACCEPTABLE_MISS_RATE) promile.  */

#ifndef ACCEPTABLE_MISS_RATE
#define ACCEPTABLE_MISS_RATE 50
#endif

#ifndef HAVE_prefetch
#define HAVE_prefetch 0
#endif

/* The group of references between that reuse may occur.  */

struct mem_ref_group
{
  tree base;			/* Base of the reference.  */
  HOST_WIDE_INT step;		/* Step of the reference.  */
  struct mem_ref *refs;		/* References in the group.  */
  struct mem_ref_group *next;	/* Next group of references.  */
};

/* Assigned to PREFETCH_BEFORE when all iterations are to be prefetched.  */

#define PREFETCH_ALL		(~(unsigned HOST_WIDE_INT) 0)

/* The memory reference.  */

struct mem_ref
{
  tree stmt;			/* Statement in that the reference appears.  */
  tree mem;			/* The reference.  */
  HOST_WIDE_INT delta;		/* Constant offset of the reference.  */
  bool write_p;			/* Is it a write?  */
  struct mem_ref_group *group;	/* The group of references it belongs to.  */
  unsigned HOST_WIDE_INT prefetch_mod;
				/* Prefetch only each PREFETCH_MOD-th
				   iteration.  */
  unsigned HOST_WIDE_INT prefetch_before;
				/* Prefetch only first PREFETCH_BEFORE
				   iterations.  */
  bool issue_prefetch_p;	/* Should we really issue the prefetch?  */
  struct mem_ref *next;		/* The next reference in the group.  */
};

/* Dumps information about reference REF to FILE.  */

static void
dump_mem_ref (FILE *file, struct mem_ref *ref)
{
  fprintf (file, "Reference %p:\n", (void *) ref);

  fprintf (file, "  group %p (base ", (void *) ref->group);
  print_generic_expr (file, ref->group->base, TDF_SLIM);
  fprintf (file, ", step ");
  fprintf (file, HOST_WIDE_INT_PRINT_DEC, ref->group->step);
  fprintf (file, ")\n");

  fprintf (dump_file, "  delta ");
  fprintf (file, HOST_WIDE_INT_PRINT_DEC, ref->delta);
  fprintf (file, "\n");

  fprintf (file, "  %s\n", ref->write_p ? "write" : "read");

  fprintf (file, "\n");
}

/* Finds a group with BASE and STEP in GROUPS, or creates one if it does not
   exist.  */

static struct mem_ref_group *
find_or_create_group (struct mem_ref_group **groups, tree base,
		      HOST_WIDE_INT step)
{
  struct mem_ref_group *group;

  for (; *groups; groups = &(*groups)->next)
    {
      if ((*groups)->step == step
	  && operand_equal_p ((*groups)->base, base, 0))
	return *groups;

      /* Keep the list of groups sorted by decreasing step.  */
      if ((*groups)->step < step)
	break;
    }

  group = xcalloc (1, sizeof (struct mem_ref_group));
  group->base = base;
  group->step = step;
  group->refs = NULL;
  group->next = *groups;
  *groups = group;

  return group;
}

/* Records a memory reference MEM in GROUP with offset DELTA and write status
   WRITE_P.  The reference occurs in statement STMT.  */

static void
record_ref (struct mem_ref_group *group, tree stmt, tree mem,
	    HOST_WIDE_INT delta, bool write_p)
{
  struct mem_ref **aref;

  /* Do not record the same address twice.  */
  for (aref = &group->refs; *aref; aref = &(*aref)->next)
    {
      /* It does not have to be possible for write reference to reuse the read
	 prefetch, or vice versa.  */
      if (!WRITE_CAN_USE_READ_PREFETCH
	  && write_p
	  && !(*aref)->write_p)
	continue;
      if (!READ_CAN_USE_WRITE_PREFETCH
	  && !write_p
	  && (*aref)->write_p)
	continue;

      if ((*aref)->delta == delta)
	return;
    }

  (*aref) = xcalloc (1, sizeof (struct mem_ref));
  (*aref)->stmt = stmt;
  (*aref)->mem = mem;
  (*aref)->delta = delta;
  (*aref)->write_p = write_p;
  (*aref)->prefetch_before = PREFETCH_ALL;
  (*aref)->prefetch_mod = 1;
  (*aref)->issue_prefetch_p = false;
  (*aref)->group = group;
  (*aref)->next = NULL;

  if (dump_file && (dump_flags & TDF_DETAILS))
    dump_mem_ref (dump_file, *aref);
}

/* Release memory references in GROUPS.  */

static void
release_mem_refs (struct mem_ref_group *groups)
{
  struct mem_ref_group *next_g;
  struct mem_ref *ref, *next_r;

  for (; groups; groups = next_g)
    {
      next_g = groups->next;
      for (ref = groups->refs; ref; ref = next_r)
	{
	  next_r = ref->next;
	  free (ref);
	}
      free (groups);
    }
}

/* A structure used to pass arguments to idx_analyze_ref.  */

struct ar_data
{
  struct loop *loop;			/* Loop of the reference.  */
  tree stmt;				/* Statement of the reference.  */
  HOST_WIDE_INT *step;			/* Step of the memory reference.  */
  HOST_WIDE_INT *delta;			/* Offset of the memory reference.  */
};

/* Analyzes a single INDEX of a memory reference to obtain information
   described at analyze_ref.  Callback for for_each_index.  */

static bool
idx_analyze_ref (tree base, tree *index, void *data)
{
  struct ar_data *ar_data = data;
  tree ibase, step, stepsize;
  HOST_WIDE_INT istep, idelta = 0, imult = 1;
  affine_iv iv;

  if (TREE_CODE (base) == MISALIGNED_INDIRECT_REF
      || TREE_CODE (base) == ALIGN_INDIRECT_REF)
    return false;

  if (!simple_iv (ar_data->loop, ar_data->stmt, *index, &iv, false))
    return false;
  ibase = iv.base;
  step = iv.step;

  if (zero_p (step))
    istep = 0;
  else
    {
      if (!cst_and_fits_in_hwi (step))
	return false;
      istep = int_cst_value (step);
    }

  if (TREE_CODE (ibase) == PLUS_EXPR
      && cst_and_fits_in_hwi (TREE_OPERAND (ibase, 1)))
    {
      idelta = int_cst_value (TREE_OPERAND (ibase, 1));
      ibase = TREE_OPERAND (ibase, 0);
    }
  if (cst_and_fits_in_hwi (ibase))
    {
      idelta += int_cst_value (ibase);
      ibase = build_int_cst (TREE_TYPE (ibase), 0);
    }

  if (TREE_CODE (base) == ARRAY_REF)
    {
      stepsize = array_ref_element_size (base);
      if (!cst_and_fits_in_hwi (stepsize))
	return false;
      imult = int_cst_value (stepsize);

      istep *= imult;
      idelta *= imult;
    }

  *ar_data->step += istep;
  *ar_data->delta += idelta;
  *index = ibase;

  return true;
}

/* Tries to express REF_P in shape &BASE + STEP * iter + DELTA, where DELTA and
   STEP are integer constants and iter is number of iterations of LOOP.  The
   reference occurs in statement STMT.  Strips nonaddressable component
   references from REF_P.  */

static bool
analyze_ref (struct loop *loop, tree *ref_p, tree *base,
	     HOST_WIDE_INT *step, HOST_WIDE_INT *delta,
	     tree stmt)
{
  struct ar_data ar_data;
  tree off;
  HOST_WIDE_INT bit_offset;
  tree ref = *ref_p;

  *step = 0;
  *delta = 0;

  /* First strip off the component references.  Ignore bitfields.  */
  if (TREE_CODE (ref) == COMPONENT_REF
      && DECL_NONADDRESSABLE_P (TREE_OPERAND (ref, 1)))
    ref = TREE_OPERAND (ref, 0);

  *ref_p = ref;

  for (; TREE_CODE (ref) == COMPONENT_REF; ref = TREE_OPERAND (ref, 0))
    {
      off = DECL_FIELD_BIT_OFFSET (TREE_OPERAND (ref, 1));
      bit_offset = TREE_INT_CST_LOW (off);
      gcc_assert (bit_offset % BITS_PER_UNIT == 0);
      
      *delta += bit_offset / BITS_PER_UNIT;
    }

  *base = unshare_expr (ref);
  ar_data.loop = loop;
  ar_data.stmt = stmt;
  ar_data.step = step;
  ar_data.delta = delta;
  return for_each_index (base, idx_analyze_ref, &ar_data);
}

/* Record a memory reference REF to the list REFS.  The reference occurs in
   LOOP in statement STMT and it is write if WRITE_P.  */

static void
gather_memory_references_ref (struct loop *loop, struct mem_ref_group **refs,
			      tree ref, bool write_p, tree stmt)
{
  tree base;
  HOST_WIDE_INT step, delta;
  struct mem_ref_group *agrp;

  if (!analyze_ref (loop, &ref, &base, &step, &delta, stmt))
    return;

  /* Now we know that REF = &BASE + STEP * iter + DELTA, where DELTA and STEP
     are integer constants.  */
  agrp = find_or_create_group (refs, base, step);
  record_ref (agrp, stmt, ref, delta, write_p);
}

/* Record the suitable memory references in LOOP.  */

static struct mem_ref_group *
gather_memory_references (struct loop *loop)
{
  basic_block *body = get_loop_body_in_dom_order (loop);
  basic_block bb;
  unsigned i;
  block_stmt_iterator bsi;
  tree stmt, lhs, rhs;
  struct mem_ref_group *refs = NULL;

  /* Scan the loop body in order, so that the former references precede the
     later ones.  */
  for (i = 0; i < loop->num_nodes; i++)
    {
      bb = body[i];
      if (bb->loop_father != loop)
	continue;

      for (bsi = bsi_start (bb); !bsi_end_p (bsi); bsi_next (&bsi))
	{
	  stmt = bsi_stmt (bsi);
	  if (TREE_CODE (stmt) != MODIFY_EXPR)
	    continue;

	  lhs = TREE_OPERAND (stmt, 0);
	  rhs = TREE_OPERAND (stmt, 1);

	  if (REFERENCE_CLASS_P (rhs))
	    gather_memory_references_ref (loop, &refs, rhs, false, stmt);
	  if (REFERENCE_CLASS_P (lhs))
	    gather_memory_references_ref (loop, &refs, lhs, true, stmt);
	}
    }
  free (body);

  return refs;
}

/* Prune the prefetch candidate REF using the self-reuse.  */

static void
prune_ref_by_self_reuse (struct mem_ref *ref)
{
  HOST_WIDE_INT step = ref->group->step;
  bool backward = step < 0;

  if (step == 0)
    {
      /* Prefetch references to invariant address just once.  */
      ref->prefetch_before = 1;
      return;
    }

  if (backward)
    step = -step;

  if (step > PREFETCH_BLOCK)
    return;

  if ((backward && HAVE_BACKWARD_PREFETCH)
      || (!backward && HAVE_FORWARD_PREFETCH))
    {
      ref->prefetch_before = 1;
      return;
    }

  ref->prefetch_mod = PREFETCH_BLOCK / step;
}

/* Divides X by BY, rounding down.  */

static HOST_WIDE_INT
ddown (HOST_WIDE_INT x, unsigned HOST_WIDE_INT by)
{
  gcc_assert (by > 0);

  if (x >= 0)
    return x / by;
  else
    return (x + by - 1) / by;
}

/* Prune the prefetch candidate REF using the reuse with BY.
   If BY_IS_BEFORE is true, BY is before REF in the loop.  */

static void
prune_ref_by_group_reuse (struct mem_ref *ref, struct mem_ref *by,
			  bool by_is_before)
{
  HOST_WIDE_INT step = ref->group->step;
  bool backward = step < 0;
  HOST_WIDE_INT delta_r = ref->delta, delta_b = by->delta;
  HOST_WIDE_INT delta = delta_b - delta_r;
  HOST_WIDE_INT hit_from;
  unsigned HOST_WIDE_INT prefetch_before, prefetch_block;

  if (delta == 0)
    {
      /* If the references has the same address, only prefetch the
	 former.  */
      if (by_is_before)
	ref->prefetch_before = 0;
      
      return;
    }

  if (!step)
    {
      /* If the reference addresses are invariant and fall into the
	 same cache line, prefetch just the first one.  */
      if (!by_is_before)
	return;

      if (ddown (ref->delta, PREFETCH_BLOCK)
	  != ddown (by->delta, PREFETCH_BLOCK))
	return;

      ref->prefetch_before = 0;
      return;
    }

  /* Only prune the reference that is behind in the array.  */
  if (backward)
    {
      if (delta > 0)
	return;

      /* Transform the data so that we may assume that the accesses
	 are forward.  */
      delta = - delta;
      step = -step;
      delta_r = PREFETCH_BLOCK - 1 - delta_r;
      delta_b = PREFETCH_BLOCK - 1 - delta_b;
    }
  else
    {
      if (delta < 0)
	return;
    }

  /* Check whether the two references are likely to hit the same cache
     line, and how distant the iterations in that it occurs are from
     each other.  */

  if (step <= PREFETCH_BLOCK)
    {
      /* The accesses are sure to meet.  Let us check when.  */
      hit_from = ddown (delta_b, PREFETCH_BLOCK) * PREFETCH_BLOCK;
      prefetch_before = (hit_from - delta_r + step - 1) / step;

      if (prefetch_before < ref->prefetch_before)
	ref->prefetch_before = prefetch_before;

      return;
    }

  /* A more complicated case.  First let us ensure that size of cache line
     and step are coprime (here we assume that PREFETCH_BLOCK is a power
     of two.  */
  prefetch_block = PREFETCH_BLOCK;
  while ((step & 1) == 0
	 && prefetch_block > 1)
    {
      step >>= 1;
      prefetch_block >>= 1;
      delta >>= 1;
    }

  /* Now step > prefetch_block, and step and prefetch_block are coprime.
     Determine the probability that the accesses hit the same cache line.  */

  prefetch_before = delta / step;
  delta %= step;
  if ((unsigned HOST_WIDE_INT) delta
      <= (prefetch_block * ACCEPTABLE_MISS_RATE / 1000))
    {
      if (prefetch_before < ref->prefetch_before)
	ref->prefetch_before = prefetch_before;

      return;
    }

  /* Try also the following iteration.  */
  prefetch_before++;
  delta = step - delta;
  if ((unsigned HOST_WIDE_INT) delta
      <= (prefetch_block * ACCEPTABLE_MISS_RATE / 1000))
    {
      if (prefetch_before < ref->prefetch_before)
	ref->prefetch_before = prefetch_before;

      return;
    }

  /* The ref probably does not reuse by.  */
  return;
}

/* Prune the prefetch candidate REF using the reuses with other references
   in REFS.  */

static void
prune_ref_by_reuse (struct mem_ref *ref, struct mem_ref *refs)
{
  struct mem_ref *prune_by;
  bool before = true;

  prune_ref_by_self_reuse (ref);

  for (prune_by = refs; prune_by; prune_by = prune_by->next)
    {
      if (prune_by == ref)
	{
	  before = false;
	  continue;
	}

      if (!WRITE_CAN_USE_READ_PREFETCH
	  && ref->write_p
	  && !prune_by->write_p)
	continue;
      if (!READ_CAN_USE_WRITE_PREFETCH
	  && !ref->write_p
	  && prune_by->write_p)
	continue;

      prune_ref_by_group_reuse (ref, prune_by, before);
    }
}

/* Prune the prefetch candidates in GROUP using the reuse analysis.  */

static void
prune_group_by_reuse (struct mem_ref_group *group)
{
  struct mem_ref *ref_pruned;

  for (ref_pruned = group->refs; ref_pruned; ref_pruned = ref_pruned->next)
    {
      prune_ref_by_reuse (ref_pruned, group->refs);

      if (dump_file && (dump_flags & TDF_DETAILS))
	{
	  fprintf (dump_file, "Reference %p:", (void *) ref_pruned);

	  if (ref_pruned->prefetch_before == PREFETCH_ALL
	      && ref_pruned->prefetch_mod == 1)
	    fprintf (dump_file, " no restrictions");
	  else if (ref_pruned->prefetch_before == 0)
	    fprintf (dump_file, " do not prefetch");
	  else if (ref_pruned->prefetch_before <= ref_pruned->prefetch_mod)
	    fprintf (dump_file, " prefetch once");
	  else
	    {
	      if (ref_pruned->prefetch_before != PREFETCH_ALL)
		{
		  fprintf (dump_file, " prefetch before ");
		  fprintf (dump_file, HOST_WIDE_INT_PRINT_DEC,
			   ref_pruned->prefetch_before);
		}
	      if (ref_pruned->prefetch_mod != 1)
		{
		  fprintf (dump_file, " prefetch mod ");
		  fprintf (dump_file, HOST_WIDE_INT_PRINT_DEC,
			   ref_pruned->prefetch_mod);
		}
	    }
	  fprintf (dump_file, "\n");
	}
    }
}

/* Prune the list of prefetch candidates GROUPS using the reuse analysis.  */

static void
prune_by_reuse (struct mem_ref_group *groups)
{
  for (; groups; groups = groups->next)
    prune_group_by_reuse (groups);
}

/* Returns true if we should issue prefetch for REF.  */

static bool
should_issue_prefetch_p (struct mem_ref *ref)
{
  /* For now do not issue prefetches for only first few of the
     iterations.  */
  if (ref->prefetch_before != PREFETCH_ALL)
    return false;

  return true;
}

/* Decide which of the prefetch candidates in GROUPS to prefetch.
   AHEAD is the number of iterations to prefetch ahead (which corresponds
   to the number of simultaneous instances of one prefetch running at a
   time).  UNROLL_FACTOR is the factor by that the loop is going to be
   unrolled.  Returns true if there is anything to prefetch.  */

static bool
schedule_prefetches (struct mem_ref_group *groups, unsigned unroll_factor,
		     unsigned ahead)
{
  unsigned max_prefetches, n_prefetches;
  struct mem_ref *ref;
  bool any = false;

  max_prefetches = (SIMULTANEOUS_PREFETCHES * unroll_factor) / ahead;
  if (max_prefetches > (unsigned) SIMULTANEOUS_PREFETCHES)
    max_prefetches = SIMULTANEOUS_PREFETCHES;

  if (dump_file && (dump_flags & TDF_DETAILS))
    fprintf (dump_file, "Max prefetches to issue: %d.\n", max_prefetches);

  if (!max_prefetches)
    return false;

  /* For now we just take memory references one by one and issue
     prefetches for as many as possible.  The groups are sorted
     starting with the largest step, since the references with
     large step are more likely to cause many cache misses.  */

  for (; groups; groups = groups->next)
    for (ref = groups->refs; ref; ref = ref->next)
      {
	if (!should_issue_prefetch_p (ref))
	  continue;

	ref->issue_prefetch_p = true;

	/* If prefetch_mod is less then unroll_factor, we need to insert
	   several prefetches for the reference.  */
	n_prefetches = ((unroll_factor + ref->prefetch_mod - 1)
			/ ref->prefetch_mod);
	if (max_prefetches <= n_prefetches)
	  return true;

	max_prefetches -= n_prefetches;
	any = true;
      }

  return any;
}

/* Determine whether there is any reference suitable for prefetching
   in GROUPS.  */

static bool
anything_to_prefetch_p (struct mem_ref_group *groups)
{
  struct mem_ref *ref;

  for (; groups; groups = groups->next)
    for (ref = groups->refs; ref; ref = ref->next)
      if (should_issue_prefetch_p (ref))
	return true;

  return false;
}

/* Issue prefetches for the reference REF into loop as decided before.
   HEAD is the number of iterations to prefetch ahead.  UNROLL_FACTOR
   is the factor by which LOOP was unrolled.  */

static void
issue_prefetch_ref (struct mem_ref *ref, unsigned unroll_factor, unsigned ahead)
{
  HOST_WIDE_INT delta;
  tree addr, addr_base, prefetch, params, write_p;
  block_stmt_iterator bsi;
  unsigned n_prefetches, ap;

  if (dump_file && (dump_flags & TDF_DETAILS))
    fprintf (dump_file, "Issued prefetch for %p.\n", (void *) ref);

  bsi = bsi_for_stmt (ref->stmt);

  n_prefetches = ((unroll_factor + ref->prefetch_mod - 1)
		  / ref->prefetch_mod);
  addr_base = build_fold_addr_expr_with_type (ref->mem, ptr_type_node);
  addr_base = force_gimple_operand_bsi (&bsi, unshare_expr (addr_base), true, NULL);

  for (ap = 0; ap < n_prefetches; ap++)
    {
      /* Determine the address to prefetch.  */
      delta = (ahead + ap * ref->prefetch_mod) * ref->group->step;
      addr = fold_build2 (PLUS_EXPR, ptr_type_node,
			  addr_base, build_int_cst (ptr_type_node, delta));
      addr = force_gimple_operand_bsi (&bsi, unshare_expr (addr), true, NULL);

      /* Create the prefetch instruction.  */
      write_p = ref->write_p ? integer_one_node : integer_zero_node;
      params = tree_cons (NULL_TREE, addr,
			  tree_cons (NULL_TREE, write_p, NULL_TREE));
				 
      prefetch = build_function_call_expr (built_in_decls[BUILT_IN_PREFETCH],
					   params);
      bsi_insert_before (&bsi, prefetch, BSI_SAME_STMT);
    }
}

/* Issue prefetches for the references in GROUPS into loop as decided before.
   HEAD is the number of iterations to prefetch ahead.  UNROLL_FACTOR is the
   factor by that LOOP was unrolled.  */

static void
issue_prefetches (struct mem_ref_group *groups,
		  unsigned unroll_factor, unsigned ahead)
{
  struct mem_ref *ref;

  for (; groups; groups = groups->next)
    for (ref = groups->refs; ref; ref = ref->next)
      if (ref->issue_prefetch_p)
	issue_prefetch_ref (ref, unroll_factor, ahead);
}

/* Determines whether we can profitably unroll LOOP FACTOR times, and if
   this is the case, fill in DESC by the description of number of
   iterations.  */

static bool
should_unroll_loop_p (struct loop *loop, struct tree_niter_desc *desc,
		      unsigned factor)
{
  if (!can_unroll_loop_p (loop, factor, desc))
    return false;

  /* We only consider loops without control flow for unrolling.  This is not
     a hard restriction -- tree_unroll_loop works with arbitrary loops
     as well; but the unrolling/prefetching is usually more profitable for
     loops consisting of a single basic block, and we want to limit the
     code growth.  */
  if (loop->num_nodes > 2)
    return false;

  return true;
}

/* Determine the coefficient by that unroll LOOP, from the information
   contained in the list of memory references REFS.  Description of
   umber of iterations of LOOP is stored to DESC.  AHEAD is the number
   of iterations ahead that we need to prefetch.  NINSNS is number of
   insns of the LOOP.  */

static unsigned
determine_unroll_factor (struct loop *loop, struct mem_ref_group *refs,
			 unsigned ahead, unsigned ninsns,
			 struct tree_niter_desc *desc)
{
  unsigned upper_bound, size_factor, constraint_factor;
  unsigned factor, max_mod_constraint, ahead_factor;
  struct mem_ref_group *agp;
  struct mem_ref *ref;

  upper_bound = PARAM_VALUE (PARAM_MAX_UNROLL_TIMES);

  /* First check whether the loop is not too large to unroll.  */
  size_factor = PARAM_VALUE (PARAM_MAX_UNROLLED_INSNS) / ninsns;
  if (size_factor <= 1)
    return 1;

  if (size_factor < upper_bound)
    upper_bound = size_factor;

  max_mod_constraint = 1;
  for (agp = refs; agp; agp = agp->next)
    for (ref = agp->refs; ref; ref = ref->next)
      if (should_issue_prefetch_p (ref)
	  && ref->prefetch_mod > max_mod_constraint)
	max_mod_constraint = ref->prefetch_mod;

  /* Set constraint_factor as large as needed to be able to satisfy the
     largest modulo constraint.  */
  constraint_factor = max_mod_constraint;

  /* If ahead is too large in comparison with the number of available
     prefetches, unroll the loop as much as needed to be able to prefetch
     at least partially some of the references in the loop.  */
  ahead_factor = ((ahead + SIMULTANEOUS_PREFETCHES - 1)
		  / SIMULTANEOUS_PREFETCHES);

  /* Unroll as much as useful, but bound the code size growth.  */
  if (constraint_factor < ahead_factor)
    factor = ahead_factor;
  else
    factor = constraint_factor;
  if (factor > upper_bound)
    factor = upper_bound;

  if (!should_unroll_loop_p (loop, desc, factor))
    return 1;

  return factor;
}

/* Issue prefetch instructions for array references in LOOP.  Returns
   true if the LOOP was unrolled.  LOOPS is the array containing all
   loops.  */

static bool
loop_prefetch_arrays (struct loops *loops, struct loop *loop)
{
  struct mem_ref_group *refs;
  unsigned ahead, ninsns, unroll_factor;
  struct tree_niter_desc desc;
  bool unrolled = false;

  /* Step 1: gather the memory references.  */
  refs = gather_memory_references (loop);

  /* Step 2: estimate the reuse effects.  */
  prune_by_reuse (refs);

  if (!anything_to_prefetch_p (refs))
    goto fail;

  /* Step 3: determine the ahead and unroll factor.  */

  /* FIXME: We should use not size of the loop, but the average number of
     instructions executed per iteration of the loop.  */
  ninsns = tree_num_loop_insns (loop);
  ahead = (PREFETCH_LATENCY + ninsns - 1) / ninsns;
  unroll_factor = determine_unroll_factor (loop, refs, ahead, ninsns,
					   &desc);
  if (dump_file && (dump_flags & TDF_DETAILS))
    fprintf (dump_file, "Ahead %d, unroll factor %d\n", ahead, unroll_factor);

  /* If the loop rolls less than the required unroll factor, prefetching
     is useless.  */
  if (unroll_factor > 1
      && cst_and_fits_in_hwi (desc.niter)
      && (unsigned HOST_WIDE_INT) int_cst_value (desc.niter) < unroll_factor)
    goto fail;

  /* Step 4: what to prefetch?  */
  if (!schedule_prefetches (refs, unroll_factor, ahead))
    goto fail;

  /* Step 5: unroll the loop.  TODO -- peeling of first and last few
     iterations so that we do not issue superfluous prefetches.  */
  if (unroll_factor != 1)
    {
      tree_unroll_loop (loops, loop, unroll_factor,
			single_dom_exit (loop), &desc);
      unrolled = true;
    }

  /* Step 6: issue the prefetches.  */
  issue_prefetches (refs, unroll_factor, ahead);

fail:
  release_mem_refs (refs);
  return unrolled;
}

/* Issue prefetch instructions for array references in LOOPS.  */

unsigned int
tree_ssa_prefetch_arrays (struct loops *loops)
{
  unsigned i;
  struct loop *loop;
  bool unrolled = false;
  int todo_flags = 0;

  if (!HAVE_prefetch
      /* It is possible to ask compiler for say -mtune=i486 -march=pentium4.
	 -mtune=i486 causes us having PREFETCH_BLOCK 0, since this is part
	 of processor costs and i486 does not have prefetch, but
	 -march=pentium4 causes HAVE_prefetch to be true.  Ugh.  */
      || PREFETCH_BLOCK == 0)
    return 0;

  initialize_original_copy_tables ();

  if (!built_in_decls[BUILT_IN_PREFETCH])
    {
      tree type = build_function_type (void_type_node,
				       tree_cons (NULL_TREE,
						  const_ptr_type_node,
						  NULL_TREE));
      tree decl = lang_hooks.builtin_function ("__builtin_prefetch", type,
			BUILT_IN_PREFETCH, BUILT_IN_NORMAL,
			NULL, NULL_TREE);
      DECL_IS_NOVOPS (decl) = true;
      built_in_decls[BUILT_IN_PREFETCH] = decl;
    }

  /* We assume that size of cache line is a power of two, so verify this
     here.  */
  gcc_assert ((PREFETCH_BLOCK & (PREFETCH_BLOCK - 1)) == 0);

  for (i = loops->num - 1; i > 0; i--)
    {
      loop = loops->parray[i];

      if (dump_file && (dump_flags & TDF_DETAILS))
	fprintf (dump_file, "Processing loop %d:\n", loop->num);

      if (loop)
	unrolled |= loop_prefetch_arrays (loops, loop);

      if (dump_file && (dump_flags & TDF_DETAILS))
	fprintf (dump_file, "\n\n");
    }

  if (unrolled)
    {
      scev_reset ();
      todo_flags |= TODO_cleanup_cfg;
    }

  free_original_copy_tables ();
  return todo_flags;
}
