/* Instruction scheduling pass.  This file computes dependencies between
   instructions.
   Copyright (C) 1992, 1993, 1994, 1995, 1996, 1997, 1998,
   1999, 2000, 2001, 2002, 2003, 2004, 2005, 2006
   Free Software Foundation, Inc.
   Contributed by Michael Tiemann (tiemann@cygnus.com) Enhanced by,
   and currently maintained by, Jim Wilson (wilson@cygnus.com)

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

#include "config.h"
#include "system.h"
#include "coretypes.h"
#include "tm.h"
#include "toplev.h"
#include "rtl.h"
#include "tm_p.h"
#include "hard-reg-set.h"
#include "regs.h"
#include "function.h"
#include "flags.h"
#include "insn-config.h"
#include "insn-attr.h"
#include "except.h"
#include "toplev.h"
#include "recog.h"
#include "sched-int.h"
#include "params.h"
#include "cselib.h"
#include "df.h"


static regset reg_pending_sets;
static regset reg_pending_clobbers;
static regset reg_pending_uses;

/* The following enumeration values tell us what dependencies we
   should use to implement the barrier.  We use true-dependencies for
   TRUE_BARRIER and anti-dependencies for MOVE_BARRIER.  */
enum reg_pending_barrier_mode
{
  NOT_A_BARRIER = 0,
  MOVE_BARRIER,
  TRUE_BARRIER
};

static enum reg_pending_barrier_mode reg_pending_barrier;

/* To speed up the test for duplicate dependency links we keep a
   record of dependencies created by add_dependence when the average
   number of instructions in a basic block is very large.

   Studies have shown that there is typically around 5 instructions between
   branches for typical C code.  So we can make a guess that the average
   basic block is approximately 5 instructions long; we will choose 100X
   the average size as a very large basic block.

   Each insn has associated bitmaps for its dependencies.  Each bitmap
   has enough entries to represent a dependency on any other insn in
   the insn chain.  All bitmap for true dependencies cache is
   allocated then the rest two ones are also allocated.  */
static bitmap_head *true_dependency_cache;
static bitmap_head *output_dependency_cache;
static bitmap_head *anti_dependency_cache;
static bitmap_head *spec_dependency_cache;
static int cache_size;

/* To speed up checking consistency of formed forward insn
   dependencies we use the following cache.  Another possible solution
   could be switching off checking duplication of insns in forward
   dependencies.  */
#ifdef ENABLE_CHECKING
static bitmap_head *forward_dependency_cache;
#endif

static int deps_may_trap_p (rtx);
static void add_dependence_list (rtx, rtx, int, enum reg_note);
static void add_dependence_list_and_free (rtx, rtx *, int, enum reg_note);
static void delete_all_dependences (rtx);
static void fixup_sched_groups (rtx);

static void flush_pending_lists (struct deps *, rtx, int, int);
static void sched_analyze_1 (struct deps *, rtx, rtx);
static void sched_analyze_2 (struct deps *, rtx, rtx);
static void sched_analyze_insn (struct deps *, rtx, rtx);

static rtx sched_get_condition (rtx);
static int conditions_mutex_p (rtx, rtx);

static enum DEPS_ADJUST_RESULT maybe_add_or_update_back_dep_1 (rtx, rtx, 
			       enum reg_note, ds_t, rtx, rtx, rtx **);
static enum DEPS_ADJUST_RESULT add_or_update_back_dep_1 (rtx, rtx, 
                               enum reg_note, ds_t, rtx, rtx, rtx **);
static void add_back_dep (rtx, rtx, enum reg_note, ds_t);

static void adjust_add_sorted_back_dep (rtx, rtx, rtx *);
static void adjust_back_add_forw_dep (rtx, rtx *);
static void delete_forw_dep (rtx, rtx);
static dw_t estimate_dep_weak (rtx, rtx);
#ifdef INSN_SCHEDULING
#ifdef ENABLE_CHECKING
static void check_dep_status (enum reg_note, ds_t, bool);
#endif
#endif

/* Return nonzero if a load of the memory reference MEM can cause a trap.  */

static int
deps_may_trap_p (rtx mem)
{
  rtx addr = XEXP (mem, 0);

  if (REG_P (addr) && REGNO (addr) >= FIRST_PSEUDO_REGISTER)
    {
      rtx t = get_reg_known_value (REGNO (addr));
      if (t)
	addr = t;
    }
  return rtx_addr_can_trap_p (addr);
}

/* Return the INSN_LIST containing INSN in LIST, or NULL
   if LIST does not contain INSN.  */

rtx
find_insn_list (rtx insn, rtx list)
{
  while (list)
    {
      if (XEXP (list, 0) == insn)
	return list;
      list = XEXP (list, 1);
    }
  return 0;
}

/* Find the condition under which INSN is executed.  */

static rtx
sched_get_condition (rtx insn)
{
  rtx pat = PATTERN (insn);
  rtx src;

  if (pat == 0)
    return 0;

  if (GET_CODE (pat) == COND_EXEC)
    return COND_EXEC_TEST (pat);

  if (!any_condjump_p (insn) || !onlyjump_p (insn))
    return 0;

  src = SET_SRC (pc_set (insn));

  if (XEXP (src, 2) == pc_rtx)
    return XEXP (src, 0);
  else if (XEXP (src, 1) == pc_rtx)
    {
      rtx cond = XEXP (src, 0);
      enum rtx_code revcode = reversed_comparison_code (cond, insn);

      if (revcode == UNKNOWN)
	return 0;
      return gen_rtx_fmt_ee (revcode, GET_MODE (cond), XEXP (cond, 0),
			     XEXP (cond, 1));
    }

  return 0;
}


/* Return nonzero if conditions COND1 and COND2 can never be both true.  */

static int
conditions_mutex_p (rtx cond1, rtx cond2)
{
  if (COMPARISON_P (cond1)
      && COMPARISON_P (cond2)
      && GET_CODE (cond1) == reversed_comparison_code (cond2, NULL)
      && XEXP (cond1, 0) == XEXP (cond2, 0)
      && XEXP (cond1, 1) == XEXP (cond2, 1))
    return 1;
  return 0;
}

/* Return true if insn1 and insn2 can never depend on one another because
   the conditions under which they are executed are mutually exclusive.  */
bool
sched_insns_conditions_mutex_p (rtx insn1, rtx insn2)
{
  rtx cond1, cond2;

  /* flow.c doesn't handle conditional lifetimes entirely correctly;
     calls mess up the conditional lifetimes.  */
  if (!CALL_P (insn1) && !CALL_P (insn2))
    {
      cond1 = sched_get_condition (insn1);
      cond2 = sched_get_condition (insn2);
      if (cond1 && cond2
	  && conditions_mutex_p (cond1, cond2)
	  /* Make sure first instruction doesn't affect condition of second
	     instruction if switched.  */
	  && !modified_in_p (cond1, insn2)
	  /* Make sure second instruction doesn't affect condition of first
	     instruction if switched.  */
	  && !modified_in_p (cond2, insn1))
	return true;
    }
  return false;
}

/* Add ELEM wrapped in an INSN_LIST with reg note kind DEP_TYPE to the
   LOG_LINKS of INSN, if it is not already there.  DEP_TYPE indicates the
   type of dependence that this link represents.  DS, if nonzero,
   indicates speculations, through which this dependence can be overcome.
   MEM1 and MEM2, if non-null, corresponds to memory locations in case of
   data speculation.  The function returns a value indicating if an old entry
   has been changed or a new entry has been added to insn's LOG_LINK.
   In case of changed entry CHANGED_LINKPP sets to its address.
   See also the definition of enum DEPS_ADJUST_RESULT in sched-int.h.  
   Actual manipulation of dependence data structures is performed in 
   add_or_update_back_dep_1.  */

static enum DEPS_ADJUST_RESULT
maybe_add_or_update_back_dep_1 (rtx insn, rtx elem, enum reg_note dep_type,
				ds_t ds, rtx mem1, rtx mem2,
				rtx **changed_linkpp)
{
  gcc_assert (INSN_P (insn) && INSN_P (elem));

  /* Don't depend an insn on itself.  */
  if (insn == elem)
    {
#ifdef INSN_SCHEDULING
      if (current_sched_info->flags & DO_SPECULATION)
        /* INSN has an internal dependence, which we can't overcome.  */
        HAS_INTERNAL_DEP (insn) = 1;
#endif
      return 0;
    }

  return add_or_update_back_dep_1 (insn, elem, dep_type,
				   ds, mem1, mem2, changed_linkpp);
}

/* This function has the same meaning of parameters and return values
   as maybe_add_or_update_back_dep_1.  The only difference between these
   two functions is that INSN and ELEM are guaranteed not to be the same
   in this one.  */
static enum DEPS_ADJUST_RESULT
add_or_update_back_dep_1 (rtx insn, rtx elem, enum reg_note dep_type, 
			  ds_t ds ATTRIBUTE_UNUSED,
			  rtx mem1 ATTRIBUTE_UNUSED, rtx mem2 ATTRIBUTE_UNUSED,
			  rtx **changed_linkpp ATTRIBUTE_UNUSED)
{
  bool maybe_present_p = true, present_p = false;

  gcc_assert (INSN_P (insn) && INSN_P (elem) && insn != elem);
  
#ifdef INSN_SCHEDULING

#ifdef ENABLE_CHECKING
  check_dep_status (dep_type, ds, mem1 != NULL);
#endif

  /* If we already have a dependency for ELEM, then we do not need to
     do anything.  Avoiding the list walk below can cut compile times
     dramatically for some code.  */
  if (true_dependency_cache != NULL)
    {
      enum reg_note present_dep_type;
      
      gcc_assert (output_dependency_cache);
      gcc_assert (anti_dependency_cache);
      if (!(current_sched_info->flags & USE_DEPS_LIST))
        {          
          if (bitmap_bit_p (&true_dependency_cache[INSN_LUID (insn)],
			    INSN_LUID (elem)))
            present_dep_type = REG_DEP_TRUE;
          else if (bitmap_bit_p (&output_dependency_cache[INSN_LUID (insn)],
				 INSN_LUID (elem)))
            present_dep_type = REG_DEP_OUTPUT;
          else if (bitmap_bit_p (&anti_dependency_cache[INSN_LUID (insn)],
				 INSN_LUID (elem)))
            present_dep_type = REG_DEP_ANTI;
          else
            maybe_present_p = false;

	  if (maybe_present_p)
	    {
	      if ((int) dep_type >= (int) present_dep_type)
		return DEP_PRESENT;
	      
	      present_p = true;
	    }
        }
      else
        {      
          ds_t present_dep_types = 0;
          
          if (bitmap_bit_p (&true_dependency_cache[INSN_LUID (insn)],
			    INSN_LUID (elem)))
            present_dep_types |= DEP_TRUE;
          if (bitmap_bit_p (&output_dependency_cache[INSN_LUID (insn)],
			    INSN_LUID (elem)))
            present_dep_types |= DEP_OUTPUT;
          if (bitmap_bit_p (&anti_dependency_cache[INSN_LUID (insn)],
			    INSN_LUID (elem)))
            present_dep_types |= DEP_ANTI;

          if (present_dep_types)
	    {
	      if (!(current_sched_info->flags & DO_SPECULATION)
		  || !bitmap_bit_p (&spec_dependency_cache[INSN_LUID (insn)],
				    INSN_LUID (elem)))
		{
		  if ((present_dep_types | (ds & DEP_TYPES))
		      == present_dep_types)
		    /* We already have all these bits.  */
		    return DEP_PRESENT;
		}
	      else
		{
		  /* Only true dependencies can be data speculative and
		     only anti dependencies can be control speculative.  */
		  gcc_assert ((present_dep_types & (DEP_TRUE | DEP_ANTI))
			      == present_dep_types);
		  
		  /* if (additional dep is SPECULATIVE) then
 		       we should update DEP_STATUS
		     else
		       we should reset existing dep to non-speculative.  */
		}
	  	
	      present_p = true;
	    }
	  else
	    maybe_present_p = false;
        }
    }
#endif

  /* Check that we don't already have this dependence.  */
  if (maybe_present_p)
    {
      rtx *linkp;

      for (linkp = &LOG_LINKS (insn); *linkp; linkp = &XEXP (*linkp, 1))
        {
          rtx link = *linkp;

	  gcc_assert (true_dependency_cache == 0 || present_p);
	  
          if (XEXP (link, 0) == elem)
            {
              enum DEPS_ADJUST_RESULT changed_p = DEP_PRESENT;

#ifdef INSN_SCHEDULING
              if (current_sched_info->flags & USE_DEPS_LIST)
                {
                  ds_t new_status = ds | DEP_STATUS (link);

		  if (new_status & SPECULATIVE)
		    {
		      if (!(ds & SPECULATIVE)
			  || !(DEP_STATUS (link) & SPECULATIVE))
			/* Then this dep can't be speculative.  */
			{
			  new_status &= ~SPECULATIVE;
			  if (true_dependency_cache
			      && (DEP_STATUS (link) & SPECULATIVE))
			    bitmap_clear_bit (&spec_dependency_cache
					      [INSN_LUID (insn)],
					      INSN_LUID (elem));
			}
		      else
			{
			  /* Both are speculative.  Merging probabilities.  */
			  if (mem1)
			    {
			      dw_t dw;

			      dw = estimate_dep_weak (mem1, mem2);
			      ds = set_dep_weak (ds, BEGIN_DATA, dw);
			    }
							 
			  new_status = ds_merge (DEP_STATUS (link), ds);
			}
		    }

		  ds = new_status;
                }

              /* Clear corresponding cache entry because type of the link
                 may have changed.  Keep them if we use_deps_list.  */
              if (true_dependency_cache != NULL
		  && !(current_sched_info->flags & USE_DEPS_LIST))
		{
		  enum reg_note kind = REG_NOTE_KIND (link);

		  switch (kind)
		    {
		    case REG_DEP_OUTPUT:
		      bitmap_clear_bit (&output_dependency_cache
					[INSN_LUID (insn)], INSN_LUID (elem));
		      break;
		    case REG_DEP_ANTI:
		      bitmap_clear_bit (&anti_dependency_cache
					[INSN_LUID (insn)], INSN_LUID (elem));
		      break;
		    default:
		      gcc_unreachable ();                        
                    }
                }

              if ((current_sched_info->flags & USE_DEPS_LIST)
		  && DEP_STATUS (link) != ds)
		{
		  DEP_STATUS (link) = ds;
		  changed_p = DEP_CHANGED;
		}
#endif

              /* If this is a more restrictive type of dependence than the
		 existing one, then change the existing dependence to this
		 type.  */
              if ((int) dep_type < (int) REG_NOTE_KIND (link))
                {
                  PUT_REG_NOTE_KIND (link, dep_type);
                  changed_p = DEP_CHANGED;
                }

#ifdef INSN_SCHEDULING
              /* If we are adding a dependency to INSN's LOG_LINKs, then
                 note that in the bitmap caches of dependency information.  */
              if (true_dependency_cache != NULL)
                {
                  if (!(current_sched_info->flags & USE_DEPS_LIST))
                    {
                      if (REG_NOTE_KIND (link) == REG_DEP_TRUE)
                        bitmap_set_bit (&true_dependency_cache
					[INSN_LUID (insn)], INSN_LUID (elem));
                      else if (REG_NOTE_KIND (link) == REG_DEP_OUTPUT)
                        bitmap_set_bit (&output_dependency_cache
					[INSN_LUID (insn)], INSN_LUID (elem));
                      else if (REG_NOTE_KIND (link) == REG_DEP_ANTI)
                        bitmap_set_bit (&anti_dependency_cache
					[INSN_LUID (insn)], INSN_LUID (elem));
                    }
                  else
                    {
                      if (ds & DEP_TRUE)
                        bitmap_set_bit (&true_dependency_cache
					[INSN_LUID (insn)], INSN_LUID (elem));
                      if (ds & DEP_OUTPUT)
                        bitmap_set_bit (&output_dependency_cache
					[INSN_LUID (insn)], INSN_LUID (elem));
                      if (ds & DEP_ANTI)
                        bitmap_set_bit (&anti_dependency_cache
					[INSN_LUID (insn)], INSN_LUID (elem));
                      /* Note, that dep can become speculative only 
                         at the moment of creation. Thus, we don't need to 
		         check for it here.  */
                    }
                }
              
              if (changed_linkpp && changed_p == DEP_CHANGED)
                *changed_linkpp = linkp;
#endif
              return changed_p;
            }	  
        }
      /* We didn't find a dep. It shouldn't be present in the cache.  */
      gcc_assert (!present_p);
    }

  /* Might want to check one level of transitivity to save conses.
     This check should be done in maybe_add_or_update_back_dep_1.
     Since we made it to add_or_update_back_dep_1, we must create
     (or update) a link.  */

  if (mem1)
    {
      gcc_assert (current_sched_info->flags & DO_SPECULATION);
      ds = set_dep_weak (ds, BEGIN_DATA, estimate_dep_weak (mem1, mem2));
    }
  
  add_back_dep (insn, elem, dep_type, ds);
  
  return DEP_CREATED;
}

/* This function creates a link between INSN and ELEM under any
   conditions.  DS describes speculative status of the link.  */
static void
add_back_dep (rtx insn, rtx elem, enum reg_note dep_type, ds_t ds)
{
  gcc_assert (INSN_P (insn) && INSN_P (elem) && insn != elem);

  if (current_sched_info->flags & USE_DEPS_LIST)
    LOG_LINKS (insn) = alloc_DEPS_LIST (elem, LOG_LINKS (insn), ds);
  else
    LOG_LINKS (insn) = alloc_INSN_LIST (elem, LOG_LINKS (insn));
  
  /* Insn dependency, not data dependency.  */
  PUT_REG_NOTE_KIND (LOG_LINKS (insn), dep_type);
    
#ifdef INSN_SCHEDULING
#ifdef ENABLE_CHECKING
  check_dep_status (dep_type, ds, false);
#endif

  /* If we are adding a dependency to INSN's LOG_LINKs, then note that
     in the bitmap caches of dependency information.  */
  if (true_dependency_cache != NULL)
    {
      if (!(current_sched_info->flags & USE_DEPS_LIST))
        {
          if (dep_type == REG_DEP_TRUE)
            bitmap_set_bit (&true_dependency_cache[INSN_LUID (insn)],
			    INSN_LUID (elem));
          else if (dep_type == REG_DEP_OUTPUT)
            bitmap_set_bit (&output_dependency_cache[INSN_LUID (insn)],
			    INSN_LUID (elem));
          else if (dep_type == REG_DEP_ANTI)
                bitmap_set_bit (&anti_dependency_cache[INSN_LUID (insn)],
				INSN_LUID (elem));
        }
      else
        {
          if (ds & DEP_TRUE)
            bitmap_set_bit (&true_dependency_cache[INSN_LUID (insn)],
			    INSN_LUID (elem));
          if (ds & DEP_OUTPUT)
            bitmap_set_bit (&output_dependency_cache[INSN_LUID (insn)],
			    INSN_LUID (elem));
          if (ds & DEP_ANTI)
            bitmap_set_bit (&anti_dependency_cache[INSN_LUID (insn)],
			    INSN_LUID (elem));
          if (ds & SPECULATIVE)
	    {
	      gcc_assert (current_sched_info->flags & DO_SPECULATION);
	      bitmap_set_bit (&spec_dependency_cache[INSN_LUID (insn)],
			      INSN_LUID (elem));
	    }
        }
    }
#endif
}

/* A convenience wrapper to operate on an entire list.  */

static void
add_dependence_list (rtx insn, rtx list, int uncond, enum reg_note dep_type)
{
  for (; list; list = XEXP (list, 1))
    {
      if (uncond || ! sched_insns_conditions_mutex_p (insn, XEXP (list, 0)))
	add_dependence (insn, XEXP (list, 0), dep_type);
    }
}

/* Similar, but free *LISTP at the same time.  */

static void
add_dependence_list_and_free (rtx insn, rtx *listp, int uncond,
			      enum reg_note dep_type)
{
  rtx list, next;
  for (list = *listp, *listp = NULL; list ; list = next)
    {
      next = XEXP (list, 1);
      if (uncond || ! sched_insns_conditions_mutex_p (insn, XEXP (list, 0)))
	add_dependence (insn, XEXP (list, 0), dep_type);
      free_INSN_LIST_node (list);
    }
}

/* Clear all dependencies for an insn.  */

static void
delete_all_dependences (rtx insn)
{
  /* Clear caches, if they exist, as well as free the dependence.  */

#ifdef INSN_SCHEDULING
  if (true_dependency_cache != NULL)
    {
      bitmap_clear (&true_dependency_cache[INSN_LUID (insn)]);
      bitmap_clear (&output_dependency_cache[INSN_LUID (insn)]);
      bitmap_clear (&anti_dependency_cache[INSN_LUID (insn)]);
      /* We don't have to clear forward_dependency_cache here,
	 because it is formed later.  */
      if (current_sched_info->flags & DO_SPECULATION)
        bitmap_clear (&spec_dependency_cache[INSN_LUID (insn)]);
    }
#endif

  if (!(current_sched_info->flags & USE_DEPS_LIST))
    /* In this case LOG_LINKS are formed from the DEPS_LISTs,
       not the INSN_LISTs.  */
    free_INSN_LIST_list (&LOG_LINKS (insn));  
  else
    free_DEPS_LIST_list (&LOG_LINKS (insn));
}

/* All insns in a scheduling group except the first should only have
   dependencies on the previous insn in the group.  So we find the
   first instruction in the scheduling group by walking the dependence
   chains backwards. Then we add the dependencies for the group to
   the previous nonnote insn.  */

static void
fixup_sched_groups (rtx insn)
{
  rtx link, prev_nonnote;

  for (link = LOG_LINKS (insn); link ; link = XEXP (link, 1))
    {
      rtx i = insn;
      do
	{
	  i = prev_nonnote_insn (i);

	  if (XEXP (link, 0) == i)
	    goto next_link;
	} while (SCHED_GROUP_P (i));
      if (! sched_insns_conditions_mutex_p (i, XEXP (link, 0)))
	add_dependence (i, XEXP (link, 0), REG_NOTE_KIND (link));
    next_link:;
    }

  delete_all_dependences (insn);

  prev_nonnote = prev_nonnote_insn (insn);
  if (BLOCK_FOR_INSN (insn) == BLOCK_FOR_INSN (prev_nonnote)
      && ! sched_insns_conditions_mutex_p (insn, prev_nonnote))
    add_dependence (insn, prev_nonnote, REG_DEP_ANTI);
}

/* Process an insn's memory dependencies.  There are four kinds of
   dependencies:

   (0) read dependence: read follows read
   (1) true dependence: read follows write
   (2) output dependence: write follows write
   (3) anti dependence: write follows read

   We are careful to build only dependencies which actually exist, and
   use transitivity to avoid building too many links.  */

/* Add an INSN and MEM reference pair to a pending INSN_LIST and MEM_LIST.
   The MEM is a memory reference contained within INSN, which we are saving
   so that we can do memory aliasing on it.  */

static void
add_insn_mem_dependence (struct deps *deps, rtx *insn_list, rtx *mem_list,
			 rtx insn, rtx mem)
{
  rtx link;

  link = alloc_INSN_LIST (insn, *insn_list);
  *insn_list = link;

  if (current_sched_info->use_cselib)
    {
      mem = shallow_copy_rtx (mem);
      XEXP (mem, 0) = cselib_subst_to_values (XEXP (mem, 0));
    }
  link = alloc_EXPR_LIST (VOIDmode, canon_rtx (mem), *mem_list);
  *mem_list = link;

  deps->pending_lists_length++;
}

/* Make a dependency between every memory reference on the pending lists
   and INSN, thus flushing the pending lists.  FOR_READ is true if emitting
   dependencies for a read operation, similarly with FOR_WRITE.  */

static void
flush_pending_lists (struct deps *deps, rtx insn, int for_read,
		     int for_write)
{
  if (for_write)
    {
      add_dependence_list_and_free (insn, &deps->pending_read_insns, 1,
				    REG_DEP_ANTI);
      free_EXPR_LIST_list (&deps->pending_read_mems);
    }

  add_dependence_list_and_free (insn, &deps->pending_write_insns, 1,
				for_read ? REG_DEP_ANTI : REG_DEP_OUTPUT);
  free_EXPR_LIST_list (&deps->pending_write_mems);
  deps->pending_lists_length = 0;

  add_dependence_list_and_free (insn, &deps->last_pending_memory_flush, 1,
				for_read ? REG_DEP_ANTI : REG_DEP_OUTPUT);
  deps->last_pending_memory_flush = alloc_INSN_LIST (insn, NULL_RTX);
  deps->pending_flush_length = 1;
}

/* Analyze a single reference to register (reg:MODE REGNO) in INSN.
   The type of the reference is specified by REF and can be SET,
   CLOBBER, PRE_DEC, POST_DEC, PRE_INC, POST_INC or USE.  */

static void
sched_analyze_reg (struct deps *deps, int regno, enum machine_mode mode,
		   enum rtx_code ref, rtx insn)
{
  /* A hard reg in a wide mode may really be multiple registers.
     If so, mark all of them just like the first.  */
  if (regno < FIRST_PSEUDO_REGISTER)
    {
      int i = hard_regno_nregs[regno][mode];
      if (ref == SET)
	{
	  while (--i >= 0)
	    SET_REGNO_REG_SET (reg_pending_sets, regno + i);
	}
      else if (ref == USE)
	{
	  while (--i >= 0)
	    SET_REGNO_REG_SET (reg_pending_uses, regno + i);
	}
      else
	{
	  while (--i >= 0)
	    SET_REGNO_REG_SET (reg_pending_clobbers, regno + i);
	}
    }

  /* ??? Reload sometimes emits USEs and CLOBBERs of pseudos that
     it does not reload.  Ignore these as they have served their
     purpose already.  */
  else if (regno >= deps->max_reg)
    {
      enum rtx_code code = GET_CODE (PATTERN (insn));
      gcc_assert (code == USE || code == CLOBBER);
    }

  else
    {
      if (ref == SET)
	SET_REGNO_REG_SET (reg_pending_sets, regno);
      else if (ref == USE)
	SET_REGNO_REG_SET (reg_pending_uses, regno);
      else
	SET_REGNO_REG_SET (reg_pending_clobbers, regno);

      /* Pseudos that are REG_EQUIV to something may be replaced
	 by that during reloading.  We need only add dependencies for
	the address in the REG_EQUIV note.  */
      if (!reload_completed && get_reg_known_equiv_p (regno))
	{
	  rtx t = get_reg_known_value (regno);
	  if (MEM_P (t))
	    sched_analyze_2 (deps, XEXP (t, 0), insn);
	}

      /* Don't let it cross a call after scheduling if it doesn't
	 already cross one.  */
      if (REG_N_CALLS_CROSSED (regno) == 0)
	{
	  if (ref == USE)
	    deps->sched_before_next_call
	      = alloc_INSN_LIST (insn, deps->sched_before_next_call);
	  else
	    add_dependence_list (insn, deps->last_function_call, 1,
				 REG_DEP_ANTI);
	}
    }
}

/* Analyze a single SET, CLOBBER, PRE_DEC, POST_DEC, PRE_INC or POST_INC
   rtx, X, creating all dependencies generated by the write to the
   destination of X, and reads of everything mentioned.  */

static void
sched_analyze_1 (struct deps *deps, rtx x, rtx insn)
{
  rtx dest = XEXP (x, 0);
  enum rtx_code code = GET_CODE (x);

  if (dest == 0)
    return;

  if (GET_CODE (dest) == PARALLEL)
    {
      int i;

      for (i = XVECLEN (dest, 0) - 1; i >= 0; i--)
	if (XEXP (XVECEXP (dest, 0, i), 0) != 0)
	  sched_analyze_1 (deps,
			   gen_rtx_CLOBBER (VOIDmode,
					    XEXP (XVECEXP (dest, 0, i), 0)),
			   insn);

      if (GET_CODE (x) == SET)
	sched_analyze_2 (deps, SET_SRC (x), insn);
      return;
    }

  while (GET_CODE (dest) == STRICT_LOW_PART || GET_CODE (dest) == SUBREG
	 || GET_CODE (dest) == ZERO_EXTRACT)
    {
      if (GET_CODE (dest) == STRICT_LOW_PART
	 || GET_CODE (dest) == ZERO_EXTRACT
	 || df_read_modify_subreg_p (dest))
        {
	  /* These both read and modify the result.  We must handle
             them as writes to get proper dependencies for following
             instructions.  We must handle them as reads to get proper
             dependencies from this to previous instructions.
             Thus we need to call sched_analyze_2.  */

	  sched_analyze_2 (deps, XEXP (dest, 0), insn);
	}
      if (GET_CODE (dest) == ZERO_EXTRACT)
	{
	  /* The second and third arguments are values read by this insn.  */
	  sched_analyze_2 (deps, XEXP (dest, 1), insn);
	  sched_analyze_2 (deps, XEXP (dest, 2), insn);
	}
      dest = XEXP (dest, 0);
    }

  if (REG_P (dest))
    {
      int regno = REGNO (dest);
      enum machine_mode mode = GET_MODE (dest);

      sched_analyze_reg (deps, regno, mode, code, insn);

#ifdef STACK_REGS
      /* Treat all writes to a stack register as modifying the TOS.  */
      if (regno >= FIRST_STACK_REG && regno <= LAST_STACK_REG)
	{
	  /* Avoid analyzing the same register twice.  */
	  if (regno != FIRST_STACK_REG)
	    sched_analyze_reg (deps, FIRST_STACK_REG, mode, code, insn);
	  sched_analyze_reg (deps, FIRST_STACK_REG, mode, USE, insn);
	}
#endif
    }
  else if (MEM_P (dest))
    {
      /* Writing memory.  */
      rtx t = dest;

      if (current_sched_info->use_cselib)
	{
	  t = shallow_copy_rtx (dest);
	  cselib_lookup (XEXP (t, 0), Pmode, 1);
	  XEXP (t, 0) = cselib_subst_to_values (XEXP (t, 0));
	}
      t = canon_rtx (t);

      if (deps->pending_lists_length > MAX_PENDING_LIST_LENGTH)
	{
	  /* Flush all pending reads and writes to prevent the pending lists
	     from getting any larger.  Insn scheduling runs too slowly when
	     these lists get long.  When compiling GCC with itself,
	     this flush occurs 8 times for sparc, and 10 times for m88k using
	     the default value of 32.  */
	  flush_pending_lists (deps, insn, false, true);
	}
      else
	{
	  rtx pending, pending_mem;

	  pending = deps->pending_read_insns;
	  pending_mem = deps->pending_read_mems;
	  while (pending)
	    {
	      if (anti_dependence (XEXP (pending_mem, 0), t)
		  && ! sched_insns_conditions_mutex_p (insn, XEXP (pending, 0)))
		add_dependence (insn, XEXP (pending, 0), REG_DEP_ANTI);

	      pending = XEXP (pending, 1);
	      pending_mem = XEXP (pending_mem, 1);
	    }

	  pending = deps->pending_write_insns;
	  pending_mem = deps->pending_write_mems;
	  while (pending)
	    {
	      if (output_dependence (XEXP (pending_mem, 0), t)
		  && ! sched_insns_conditions_mutex_p (insn, XEXP (pending, 0)))
		add_dependence (insn, XEXP (pending, 0), REG_DEP_OUTPUT);

	      pending = XEXP (pending, 1);
	      pending_mem = XEXP (pending_mem, 1);
	    }

	  add_dependence_list (insn, deps->last_pending_memory_flush, 1,
			       REG_DEP_ANTI);

	  add_insn_mem_dependence (deps, &deps->pending_write_insns,
				   &deps->pending_write_mems, insn, dest);
	}
      sched_analyze_2 (deps, XEXP (dest, 0), insn);
    }

  /* Analyze reads.  */
  if (GET_CODE (x) == SET)
    sched_analyze_2 (deps, SET_SRC (x), insn);
}

/* Analyze the uses of memory and registers in rtx X in INSN.  */

static void
sched_analyze_2 (struct deps *deps, rtx x, rtx insn)
{
  int i;
  int j;
  enum rtx_code code;
  const char *fmt;

  if (x == 0)
    return;

  code = GET_CODE (x);

  switch (code)
    {
    case CONST_INT:
    case CONST_DOUBLE:
    case CONST_VECTOR:
    case SYMBOL_REF:
    case CONST:
    case LABEL_REF:
      /* Ignore constants.  Note that we must handle CONST_DOUBLE here
         because it may have a cc0_rtx in its CONST_DOUBLE_CHAIN field, but
         this does not mean that this insn is using cc0.  */
      return;

#ifdef HAVE_cc0
    case CC0:
      /* User of CC0 depends on immediately preceding insn.  */
      SCHED_GROUP_P (insn) = 1;
       /* Don't move CC0 setter to another block (it can set up the
        same flag for previous CC0 users which is safe).  */
      CANT_MOVE (prev_nonnote_insn (insn)) = 1;
      return;
#endif

    case REG:
      {
	int regno = REGNO (x);
	enum machine_mode mode = GET_MODE (x);

	sched_analyze_reg (deps, regno, mode, USE, insn);

#ifdef STACK_REGS
      /* Treat all reads of a stack register as modifying the TOS.  */
      if (regno >= FIRST_STACK_REG && regno <= LAST_STACK_REG)
	{
	  /* Avoid analyzing the same register twice.  */
	  if (regno != FIRST_STACK_REG)
	    sched_analyze_reg (deps, FIRST_STACK_REG, mode, USE, insn);
	  sched_analyze_reg (deps, FIRST_STACK_REG, mode, SET, insn);
	}
#endif
	return;
      }

    case MEM:
      {
	/* Reading memory.  */
	rtx u;
	rtx pending, pending_mem;
	rtx t = x;

	if (current_sched_info->use_cselib)
	  {
	    t = shallow_copy_rtx (t);
	    cselib_lookup (XEXP (t, 0), Pmode, 1);
	    XEXP (t, 0) = cselib_subst_to_values (XEXP (t, 0));
	  }
	t = canon_rtx (t);
	pending = deps->pending_read_insns;
	pending_mem = deps->pending_read_mems;
	while (pending)
	  {
	    if (read_dependence (XEXP (pending_mem, 0), t)
		&& ! sched_insns_conditions_mutex_p (insn, XEXP (pending, 0)))
	      add_dependence (insn, XEXP (pending, 0), REG_DEP_ANTI);

	    pending = XEXP (pending, 1);
	    pending_mem = XEXP (pending_mem, 1);
	  }

	pending = deps->pending_write_insns;
	pending_mem = deps->pending_write_mems;
	while (pending)
	  {
	    if (true_dependence (XEXP (pending_mem, 0), VOIDmode,
				 t, rtx_varies_p)
		&& ! sched_insns_conditions_mutex_p (insn, XEXP (pending, 0)))
              {
                if (current_sched_info->flags & DO_SPECULATION)
                  maybe_add_or_update_back_dep_1 (insn, XEXP (pending, 0),
						  REG_DEP_TRUE,
						  BEGIN_DATA | DEP_TRUE,
						  XEXP (pending_mem, 0), t, 0);
                else
                  add_dependence (insn, XEXP (pending, 0), REG_DEP_TRUE);
              }

	    pending = XEXP (pending, 1);
	    pending_mem = XEXP (pending_mem, 1);
	  }

	for (u = deps->last_pending_memory_flush; u; u = XEXP (u, 1))
	  if (! JUMP_P (XEXP (u, 0)) || deps_may_trap_p (x))
	    add_dependence (insn, XEXP (u, 0), REG_DEP_ANTI);

	/* Always add these dependencies to pending_reads, since
	   this insn may be followed by a write.  */
	add_insn_mem_dependence (deps, &deps->pending_read_insns,
				 &deps->pending_read_mems, insn, x);

	/* Take advantage of tail recursion here.  */
	sched_analyze_2 (deps, XEXP (x, 0), insn);
	return;
      }

    /* Force pending stores to memory in case a trap handler needs them.  */
    case TRAP_IF:
      flush_pending_lists (deps, insn, true, false);
      break;

    case ASM_OPERANDS:
    case ASM_INPUT:
    case UNSPEC_VOLATILE:
      {
	/* Traditional and volatile asm instructions must be considered to use
	   and clobber all hard registers, all pseudo-registers and all of
	   memory.  So must TRAP_IF and UNSPEC_VOLATILE operations.

	   Consider for instance a volatile asm that changes the fpu rounding
	   mode.  An insn should not be moved across this even if it only uses
	   pseudo-regs because it might give an incorrectly rounded result.  */
	if (code != ASM_OPERANDS || MEM_VOLATILE_P (x))
	  reg_pending_barrier = TRUE_BARRIER;

	/* For all ASM_OPERANDS, we must traverse the vector of input operands.
	   We can not just fall through here since then we would be confused
	   by the ASM_INPUT rtx inside ASM_OPERANDS, which do not indicate
	   traditional asms unlike their normal usage.  */

	if (code == ASM_OPERANDS)
	  {
	    for (j = 0; j < ASM_OPERANDS_INPUT_LENGTH (x); j++)
	      sched_analyze_2 (deps, ASM_OPERANDS_INPUT (x, j), insn);
	    return;
	  }
	break;
      }

    case PRE_DEC:
    case POST_DEC:
    case PRE_INC:
    case POST_INC:
      /* These both read and modify the result.  We must handle them as writes
         to get proper dependencies for following instructions.  We must handle
         them as reads to get proper dependencies from this to previous
         instructions.  Thus we need to pass them to both sched_analyze_1
         and sched_analyze_2.  We must call sched_analyze_2 first in order
         to get the proper antecedent for the read.  */
      sched_analyze_2 (deps, XEXP (x, 0), insn);
      sched_analyze_1 (deps, x, insn);
      return;

    case POST_MODIFY:
    case PRE_MODIFY:
      /* op0 = op0 + op1 */
      sched_analyze_2 (deps, XEXP (x, 0), insn);
      sched_analyze_2 (deps, XEXP (x, 1), insn);
      sched_analyze_1 (deps, x, insn);
      return;

    default:
      break;
    }

  /* Other cases: walk the insn.  */
  fmt = GET_RTX_FORMAT (code);
  for (i = GET_RTX_LENGTH (code) - 1; i >= 0; i--)
    {
      if (fmt[i] == 'e')
	sched_analyze_2 (deps, XEXP (x, i), insn);
      else if (fmt[i] == 'E')
	for (j = 0; j < XVECLEN (x, i); j++)
	  sched_analyze_2 (deps, XVECEXP (x, i, j), insn);
    }
}

/* Analyze an INSN with pattern X to find all dependencies.  */

static void
sched_analyze_insn (struct deps *deps, rtx x, rtx insn)
{
  RTX_CODE code = GET_CODE (x);
  rtx link;
  unsigned i;
  reg_set_iterator rsi;

  if (code == COND_EXEC)
    {
      sched_analyze_2 (deps, COND_EXEC_TEST (x), insn);

      /* ??? Should be recording conditions so we reduce the number of
	 false dependencies.  */
      x = COND_EXEC_CODE (x);
      code = GET_CODE (x);
    }
  if (code == SET || code == CLOBBER)
    {
      sched_analyze_1 (deps, x, insn);

      /* Bare clobber insns are used for letting life analysis, reg-stack
	 and others know that a value is dead.  Depend on the last call
	 instruction so that reg-stack won't get confused.  */
      if (code == CLOBBER)
	add_dependence_list (insn, deps->last_function_call, 1, REG_DEP_OUTPUT);
    }
  else if (code == PARALLEL)
    {
      for (i = XVECLEN (x, 0); i--;)
	{
	  rtx sub = XVECEXP (x, 0, i);
	  code = GET_CODE (sub);

	  if (code == COND_EXEC)
	    {
	      sched_analyze_2 (deps, COND_EXEC_TEST (sub), insn);
	      sub = COND_EXEC_CODE (sub);
	      code = GET_CODE (sub);
	    }
	  if (code == SET || code == CLOBBER)
	    sched_analyze_1 (deps, sub, insn);
	  else
	    sched_analyze_2 (deps, sub, insn);
	}
    }
  else
    sched_analyze_2 (deps, x, insn);

  /* Mark registers CLOBBERED or used by called function.  */
  if (CALL_P (insn))
    {
      for (link = CALL_INSN_FUNCTION_USAGE (insn); link; link = XEXP (link, 1))
	{
	  if (GET_CODE (XEXP (link, 0)) == CLOBBER)
	    sched_analyze_1 (deps, XEXP (link, 0), insn);
	  else
	    sched_analyze_2 (deps, XEXP (link, 0), insn);
	}
      if (find_reg_note (insn, REG_SETJMP, NULL))
	reg_pending_barrier = MOVE_BARRIER;
    }

  if (JUMP_P (insn))
    {
      rtx next;
      next = next_nonnote_insn (insn);
      if (next && BARRIER_P (next))
	reg_pending_barrier = TRUE_BARRIER;
      else
	{
	  rtx pending, pending_mem;
	  regset_head tmp_uses, tmp_sets;
	  INIT_REG_SET (&tmp_uses);
	  INIT_REG_SET (&tmp_sets);

	  (*current_sched_info->compute_jump_reg_dependencies)
	    (insn, &deps->reg_conditional_sets, &tmp_uses, &tmp_sets);
	  /* Make latency of jump equal to 0 by using anti-dependence.  */
	  EXECUTE_IF_SET_IN_REG_SET (&tmp_uses, 0, i, rsi)
	    {
	      struct deps_reg *reg_last = &deps->reg_last[i];
	      add_dependence_list (insn, reg_last->sets, 0, REG_DEP_ANTI);
	      add_dependence_list (insn, reg_last->clobbers, 0, REG_DEP_ANTI);
	      reg_last->uses_length++;
	      reg_last->uses = alloc_INSN_LIST (insn, reg_last->uses);
	    }
	  IOR_REG_SET (reg_pending_sets, &tmp_sets);

	  CLEAR_REG_SET (&tmp_uses);
	  CLEAR_REG_SET (&tmp_sets);

	  /* All memory writes and volatile reads must happen before the
	     jump.  Non-volatile reads must happen before the jump iff
	     the result is needed by the above register used mask.  */

	  pending = deps->pending_write_insns;
	  pending_mem = deps->pending_write_mems;
	  while (pending)
	    {
	      if (! sched_insns_conditions_mutex_p (insn, XEXP (pending, 0)))
		add_dependence (insn, XEXP (pending, 0), REG_DEP_OUTPUT);
	      pending = XEXP (pending, 1);
	      pending_mem = XEXP (pending_mem, 1);
	    }

	  pending = deps->pending_read_insns;
	  pending_mem = deps->pending_read_mems;
	  while (pending)
	    {
	      if (MEM_VOLATILE_P (XEXP (pending_mem, 0))
		  && ! sched_insns_conditions_mutex_p (insn, XEXP (pending, 0)))
		add_dependence (insn, XEXP (pending, 0), REG_DEP_OUTPUT);
	      pending = XEXP (pending, 1);
	      pending_mem = XEXP (pending_mem, 1);
	    }

	  add_dependence_list (insn, deps->last_pending_memory_flush, 1,
			       REG_DEP_ANTI);
	}
    }

  /* If this instruction can throw an exception, then moving it changes
     where block boundaries fall.  This is mighty confusing elsewhere.
     Therefore, prevent such an instruction from being moved.  Same for
     non-jump instructions that define block boundaries.
     ??? Unclear whether this is still necessary in EBB mode.  If not,
     add_branch_dependences should be adjusted for RGN mode instead.  */
  if (((CALL_P (insn) || JUMP_P (insn)) && can_throw_internal (insn))
      || (NONJUMP_INSN_P (insn) && control_flow_insn_p (insn)))
    reg_pending_barrier = MOVE_BARRIER;

  /* Add dependencies if a scheduling barrier was found.  */
  if (reg_pending_barrier)
    {
      /* In the case of barrier the most added dependencies are not
         real, so we use anti-dependence here.  */
      if (sched_get_condition (insn))
	{
	  EXECUTE_IF_SET_IN_REG_SET (&deps->reg_last_in_use, 0, i, rsi)
	    {
	      struct deps_reg *reg_last = &deps->reg_last[i];
	      add_dependence_list (insn, reg_last->uses, 0, REG_DEP_ANTI);
	      add_dependence_list
		(insn, reg_last->sets, 0,
		 reg_pending_barrier == TRUE_BARRIER ? REG_DEP_TRUE : REG_DEP_ANTI);
	      add_dependence_list
		(insn, reg_last->clobbers, 0,
		 reg_pending_barrier == TRUE_BARRIER ? REG_DEP_TRUE : REG_DEP_ANTI);
	    }
	}
      else
	{
	  EXECUTE_IF_SET_IN_REG_SET (&deps->reg_last_in_use, 0, i, rsi)
	    {
	      struct deps_reg *reg_last = &deps->reg_last[i];
	      add_dependence_list_and_free (insn, &reg_last->uses, 0,
					    REG_DEP_ANTI);
	      add_dependence_list_and_free
		(insn, &reg_last->sets, 0,
		 reg_pending_barrier == TRUE_BARRIER ? REG_DEP_TRUE : REG_DEP_ANTI);
	      add_dependence_list_and_free
		(insn, &reg_last->clobbers, 0,
		 reg_pending_barrier == TRUE_BARRIER ? REG_DEP_TRUE : REG_DEP_ANTI);
	      reg_last->uses_length = 0;
	      reg_last->clobbers_length = 0;
	    }
	}

      for (i = 0; i < (unsigned)deps->max_reg; i++)
	{
	  struct deps_reg *reg_last = &deps->reg_last[i];
	  reg_last->sets = alloc_INSN_LIST (insn, reg_last->sets);
	  SET_REGNO_REG_SET (&deps->reg_last_in_use, i);
	}

      flush_pending_lists (deps, insn, true, true);
      CLEAR_REG_SET (&deps->reg_conditional_sets);
      reg_pending_barrier = NOT_A_BARRIER;
    }
  else
    {
      /* If the current insn is conditional, we can't free any
	 of the lists.  */
      if (sched_get_condition (insn))
	{
	  EXECUTE_IF_SET_IN_REG_SET (reg_pending_uses, 0, i, rsi)
	    {
	      struct deps_reg *reg_last = &deps->reg_last[i];
	      add_dependence_list (insn, reg_last->sets, 0, REG_DEP_TRUE);
	      add_dependence_list (insn, reg_last->clobbers, 0, REG_DEP_TRUE);
	      reg_last->uses = alloc_INSN_LIST (insn, reg_last->uses);
	      reg_last->uses_length++;
	    }
	  EXECUTE_IF_SET_IN_REG_SET (reg_pending_clobbers, 0, i, rsi)
	    {
	      struct deps_reg *reg_last = &deps->reg_last[i];
	      add_dependence_list (insn, reg_last->sets, 0, REG_DEP_OUTPUT);
	      add_dependence_list (insn, reg_last->uses, 0, REG_DEP_ANTI);
	      reg_last->clobbers = alloc_INSN_LIST (insn, reg_last->clobbers);
	      reg_last->clobbers_length++;
	    }
	  EXECUTE_IF_SET_IN_REG_SET (reg_pending_sets, 0, i, rsi)
	    {
	      struct deps_reg *reg_last = &deps->reg_last[i];
	      add_dependence_list (insn, reg_last->sets, 0, REG_DEP_OUTPUT);
	      add_dependence_list (insn, reg_last->clobbers, 0, REG_DEP_OUTPUT);
	      add_dependence_list (insn, reg_last->uses, 0, REG_DEP_ANTI);
	      reg_last->sets = alloc_INSN_LIST (insn, reg_last->sets);
	      SET_REGNO_REG_SET (&deps->reg_conditional_sets, i);
	    }
	}
      else
	{
	  EXECUTE_IF_SET_IN_REG_SET (reg_pending_uses, 0, i, rsi)
	    {
	      struct deps_reg *reg_last = &deps->reg_last[i];
	      add_dependence_list (insn, reg_last->sets, 0, REG_DEP_TRUE);
	      add_dependence_list (insn, reg_last->clobbers, 0, REG_DEP_TRUE);
	      reg_last->uses_length++;
	      reg_last->uses = alloc_INSN_LIST (insn, reg_last->uses);
	    }
	  EXECUTE_IF_SET_IN_REG_SET (reg_pending_clobbers, 0, i, rsi)
	    {
	      struct deps_reg *reg_last = &deps->reg_last[i];
	      if (reg_last->uses_length > MAX_PENDING_LIST_LENGTH
		  || reg_last->clobbers_length > MAX_PENDING_LIST_LENGTH)
		{
		  add_dependence_list_and_free (insn, &reg_last->sets, 0,
					        REG_DEP_OUTPUT);
		  add_dependence_list_and_free (insn, &reg_last->uses, 0,
						REG_DEP_ANTI);
		  add_dependence_list_and_free (insn, &reg_last->clobbers, 0,
						REG_DEP_OUTPUT);
		  reg_last->sets = alloc_INSN_LIST (insn, reg_last->sets);
		  reg_last->clobbers_length = 0;
		  reg_last->uses_length = 0;
		}
	      else
		{
		  add_dependence_list (insn, reg_last->sets, 0, REG_DEP_OUTPUT);
		  add_dependence_list (insn, reg_last->uses, 0, REG_DEP_ANTI);
		}
	      reg_last->clobbers_length++;
	      reg_last->clobbers = alloc_INSN_LIST (insn, reg_last->clobbers);
	    }
	  EXECUTE_IF_SET_IN_REG_SET (reg_pending_sets, 0, i, rsi)
	    {
	      struct deps_reg *reg_last = &deps->reg_last[i];
	      add_dependence_list_and_free (insn, &reg_last->sets, 0,
					    REG_DEP_OUTPUT);
	      add_dependence_list_and_free (insn, &reg_last->clobbers, 0,
					    REG_DEP_OUTPUT);
	      add_dependence_list_and_free (insn, &reg_last->uses, 0,
					    REG_DEP_ANTI);
	      reg_last->sets = alloc_INSN_LIST (insn, reg_last->sets);
	      reg_last->uses_length = 0;
	      reg_last->clobbers_length = 0;
	      CLEAR_REGNO_REG_SET (&deps->reg_conditional_sets, i);
	    }
	}

      IOR_REG_SET (&deps->reg_last_in_use, reg_pending_uses);
      IOR_REG_SET (&deps->reg_last_in_use, reg_pending_clobbers);
      IOR_REG_SET (&deps->reg_last_in_use, reg_pending_sets);
    }
  CLEAR_REG_SET (reg_pending_uses);
  CLEAR_REG_SET (reg_pending_clobbers);
  CLEAR_REG_SET (reg_pending_sets);

  /* If we are currently in a libcall scheduling group, then mark the
     current insn as being in a scheduling group and that it can not
     be moved into a different basic block.  */

  if (deps->libcall_block_tail_insn)
    {
      SCHED_GROUP_P (insn) = 1;
      CANT_MOVE (insn) = 1;
    }

  /* If a post-call group is still open, see if it should remain so.
     This insn must be a simple move of a hard reg to a pseudo or
     vice-versa.

     We must avoid moving these insns for correctness on
     SMALL_REGISTER_CLASS machines, and for special registers like
     PIC_OFFSET_TABLE_REGNUM.  For simplicity, extend this to all
     hard regs for all targets.  */

  if (deps->in_post_call_group_p)
    {
      rtx tmp, set = single_set (insn);
      int src_regno, dest_regno;

      if (set == NULL)
	goto end_call_group;

      tmp = SET_DEST (set);
      if (GET_CODE (tmp) == SUBREG)
	tmp = SUBREG_REG (tmp);
      if (REG_P (tmp))
	dest_regno = REGNO (tmp);
      else
	goto end_call_group;

      tmp = SET_SRC (set);
      if (GET_CODE (tmp) == SUBREG)
	tmp = SUBREG_REG (tmp);
      if ((GET_CODE (tmp) == PLUS
	   || GET_CODE (tmp) == MINUS)
	  && REG_P (XEXP (tmp, 0))
	  && REGNO (XEXP (tmp, 0)) == STACK_POINTER_REGNUM
	  && dest_regno == STACK_POINTER_REGNUM)
	src_regno = STACK_POINTER_REGNUM;
      else if (REG_P (tmp))
	src_regno = REGNO (tmp);
      else
	goto end_call_group;

      if (src_regno < FIRST_PSEUDO_REGISTER
	  || dest_regno < FIRST_PSEUDO_REGISTER)
	{
	  if (deps->in_post_call_group_p == post_call_initial)
	    deps->in_post_call_group_p = post_call;

	  SCHED_GROUP_P (insn) = 1;
	  CANT_MOVE (insn) = 1;
	}
      else
	{
	end_call_group:
	  deps->in_post_call_group_p = not_post_call;
	}
    }

  /* Fixup the dependencies in the sched group.  */
  if (SCHED_GROUP_P (insn))
    fixup_sched_groups (insn);
}

/* Analyze every insn between HEAD and TAIL inclusive, creating LOG_LINKS
   for every dependency.  */

void
sched_analyze (struct deps *deps, rtx head, rtx tail)
{
  rtx insn;

  if (current_sched_info->use_cselib)
    cselib_init (true);

  /* Before reload, if the previous block ended in a call, show that
     we are inside a post-call group, so as to keep the lifetimes of
     hard registers correct.  */
  if (! reload_completed && !LABEL_P (head))
    {
      insn = prev_nonnote_insn (head);
      if (insn && CALL_P (insn))
	deps->in_post_call_group_p = post_call_initial;
    }
  for (insn = head;; insn = NEXT_INSN (insn))
    {
      rtx link, end_seq, r0, set;

      if (NONJUMP_INSN_P (insn) || JUMP_P (insn))
	{
	  /* Clear out the stale LOG_LINKS from flow.  */
	  free_INSN_LIST_list (&LOG_LINKS (insn));

	  /* Make each JUMP_INSN a scheduling barrier for memory
             references.  */
	  if (JUMP_P (insn))
	    {
	      /* Keep the list a reasonable size.  */
	      if (deps->pending_flush_length++ > MAX_PENDING_LIST_LENGTH)
		flush_pending_lists (deps, insn, true, true);
	      else
		deps->last_pending_memory_flush
		  = alloc_INSN_LIST (insn, deps->last_pending_memory_flush);
	    }
	  sched_analyze_insn (deps, PATTERN (insn), insn);
	}
      else if (CALL_P (insn))
	{
	  int i;

	  CANT_MOVE (insn) = 1;

	  /* Clear out the stale LOG_LINKS from flow.  */
	  free_INSN_LIST_list (&LOG_LINKS (insn));

	  if (find_reg_note (insn, REG_SETJMP, NULL))
	    {
	      /* This is setjmp.  Assume that all registers, not just
		 hard registers, may be clobbered by this call.  */
	      reg_pending_barrier = MOVE_BARRIER;
	    }
	  else
	    {
	      for (i = 0; i < FIRST_PSEUDO_REGISTER; i++)
		/* A call may read and modify global register variables.  */
		if (global_regs[i])
		  {
		    SET_REGNO_REG_SET (reg_pending_sets, i);
		    SET_REGNO_REG_SET (reg_pending_uses, i);
		  }
		/* Other call-clobbered hard regs may be clobbered.
		   Since we only have a choice between 'might be clobbered'
		   and 'definitely not clobbered', we must include all
		   partly call-clobbered registers here.  */
		else if (HARD_REGNO_CALL_PART_CLOBBERED (i, reg_raw_mode[i])
			 || TEST_HARD_REG_BIT (regs_invalidated_by_call, i))
		  SET_REGNO_REG_SET (reg_pending_clobbers, i);
		/* We don't know what set of fixed registers might be used
		   by the function, but it is certain that the stack pointer
		   is among them, but be conservative.  */
		else if (fixed_regs[i])
		  SET_REGNO_REG_SET (reg_pending_uses, i);
		/* The frame pointer is normally not used by the function
		   itself, but by the debugger.  */
		/* ??? MIPS o32 is an exception.  It uses the frame pointer
		   in the macro expansion of jal but does not represent this
		   fact in the call_insn rtl.  */
		else if (i == FRAME_POINTER_REGNUM
			 || (i == HARD_FRAME_POINTER_REGNUM
			     && (! reload_completed || frame_pointer_needed)))
		  SET_REGNO_REG_SET (reg_pending_uses, i);
	    }

	  /* For each insn which shouldn't cross a call, add a dependence
	     between that insn and this call insn.  */
	  add_dependence_list_and_free (insn, &deps->sched_before_next_call, 1,
					REG_DEP_ANTI);

	  sched_analyze_insn (deps, PATTERN (insn), insn);

	  /* In the absence of interprocedural alias analysis, we must flush
	     all pending reads and writes, and start new dependencies starting
	     from here.  But only flush writes for constant calls (which may
	     be passed a pointer to something we haven't written yet).  */
	  flush_pending_lists (deps, insn, true, !CONST_OR_PURE_CALL_P (insn));

	  /* Remember the last function call for limiting lifetimes.  */
	  free_INSN_LIST_list (&deps->last_function_call);
	  deps->last_function_call = alloc_INSN_LIST (insn, NULL_RTX);

	  /* Before reload, begin a post-call group, so as to keep the
	     lifetimes of hard registers correct.  */
	  if (! reload_completed)
	    deps->in_post_call_group_p = post_call;
	}

      /* EH_REGION insn notes can not appear until well after we complete
	 scheduling.  */
      if (NOTE_P (insn))
	gcc_assert (NOTE_LINE_NUMBER (insn) != NOTE_INSN_EH_REGION_BEG
		    && NOTE_LINE_NUMBER (insn) != NOTE_INSN_EH_REGION_END);

      if (current_sched_info->use_cselib)
	cselib_process_insn (insn);

      /* Now that we have completed handling INSN, check and see if it is
	 a CLOBBER beginning a libcall block.   If it is, record the
	 end of the libcall sequence.

	 We want to schedule libcall blocks as a unit before reload.  While
	 this restricts scheduling, it preserves the meaning of a libcall
	 block.

	 As a side effect, we may get better code due to decreased register
	 pressure as well as less chance of a foreign insn appearing in
	 a libcall block.  */
      if (!reload_completed
	  /* Note we may have nested libcall sequences.  We only care about
	     the outermost libcall sequence.  */
	  && deps->libcall_block_tail_insn == 0
	  /* The sequence must start with a clobber of a register.  */
	  && NONJUMP_INSN_P (insn)
	  && GET_CODE (PATTERN (insn)) == CLOBBER
          && (r0 = XEXP (PATTERN (insn), 0), REG_P (r0))
	  && REG_P (XEXP (PATTERN (insn), 0))
	  /* The CLOBBER must also have a REG_LIBCALL note attached.  */
	  && (link = find_reg_note (insn, REG_LIBCALL, NULL_RTX)) != 0
	  && (end_seq = XEXP (link, 0)) != 0
	  /* The insn referenced by the REG_LIBCALL note must be a
	     simple nop copy with the same destination as the register
	     mentioned in the clobber.  */
	  && (set = single_set (end_seq)) != 0
	  && SET_DEST (set) == r0 && SET_SRC (set) == r0
	  /* And finally the insn referenced by the REG_LIBCALL must
	     also contain a REG_EQUAL note and a REG_RETVAL note.  */
	  && find_reg_note (end_seq, REG_EQUAL, NULL_RTX) != 0
	  && find_reg_note (end_seq, REG_RETVAL, NULL_RTX) != 0)
	deps->libcall_block_tail_insn = XEXP (link, 0);

      /* If we have reached the end of a libcall block, then close the
	 block.  */
      if (deps->libcall_block_tail_insn == insn)
	deps->libcall_block_tail_insn = 0;

      if (insn == tail)
	{
	  if (current_sched_info->use_cselib)
	    cselib_finish ();
	  return;
	}
    }
  gcc_unreachable ();
}


/* The following function adds forward dependence (FROM, TO) with
   given DEP_TYPE.  The forward dependence should be not exist before.  */

void
add_forw_dep (rtx to, rtx link)
{
  rtx new_link, from;

  from = XEXP (link, 0);

#ifdef ENABLE_CHECKING
  /* If add_dependence is working properly there should never
     be notes, deleted insns or duplicates in the backward
     links.  Thus we need not check for them here.

     However, if we have enabled checking we might as well go
     ahead and verify that add_dependence worked properly.  */
  gcc_assert (INSN_P (from));
  gcc_assert (!INSN_DELETED_P (from));
  if (true_dependency_cache)
    {
      gcc_assert (!bitmap_bit_p (&forward_dependency_cache[INSN_LUID (from)],
				 INSN_LUID (to)));
      bitmap_set_bit (&forward_dependency_cache[INSN_LUID (from)],
		      INSN_LUID (to));
    }
  else
    gcc_assert (!find_insn_list (to, INSN_DEPEND (from)));
#endif

  if (!(current_sched_info->flags & USE_DEPS_LIST))
    new_link = alloc_INSN_LIST (to, INSN_DEPEND (from));
  else
    new_link = alloc_DEPS_LIST (to, INSN_DEPEND (from), DEP_STATUS (link));

  PUT_REG_NOTE_KIND (new_link, REG_NOTE_KIND (link));

  INSN_DEPEND (from) = new_link;
  INSN_DEP_COUNT (to) += 1;
}

/* Examine insns in the range [ HEAD, TAIL ] and Use the backward
   dependences from LOG_LINKS to build forward dependences in
   INSN_DEPEND.  */

void
compute_forward_dependences (rtx head, rtx tail)
{
  rtx insn;
  rtx next_tail;

  next_tail = NEXT_INSN (tail);
  for (insn = head; insn != next_tail; insn = NEXT_INSN (insn))
    {
      rtx link;
      
      if (! INSN_P (insn))
	continue;
      
      if (current_sched_info->flags & DO_SPECULATION)
        {
          rtx new = 0, link, next;

          for (link = LOG_LINKS (insn); link; link = next)
            {
              next = XEXP (link, 1);
              adjust_add_sorted_back_dep (insn, link, &new);
            }

          LOG_LINKS (insn) = new;
        }

      for (link = LOG_LINKS (insn); link; link = XEXP (link, 1))
        add_forw_dep (insn, link);
    }
}

/* Initialize variables for region data dependence analysis.
   n_bbs is the number of region blocks.  */

void
init_deps (struct deps *deps)
{
  int max_reg = (reload_completed ? FIRST_PSEUDO_REGISTER : max_reg_num ());

  deps->max_reg = max_reg;
  deps->reg_last = XCNEWVEC (struct deps_reg, max_reg);
  INIT_REG_SET (&deps->reg_last_in_use);
  INIT_REG_SET (&deps->reg_conditional_sets);

  deps->pending_read_insns = 0;
  deps->pending_read_mems = 0;
  deps->pending_write_insns = 0;
  deps->pending_write_mems = 0;
  deps->pending_lists_length = 0;
  deps->pending_flush_length = 0;
  deps->last_pending_memory_flush = 0;
  deps->last_function_call = 0;
  deps->sched_before_next_call = 0;
  deps->in_post_call_group_p = not_post_call;
  deps->libcall_block_tail_insn = 0;
}

/* Free insn lists found in DEPS.  */

void
free_deps (struct deps *deps)
{
  unsigned i;
  reg_set_iterator rsi;

  free_INSN_LIST_list (&deps->pending_read_insns);
  free_EXPR_LIST_list (&deps->pending_read_mems);
  free_INSN_LIST_list (&deps->pending_write_insns);
  free_EXPR_LIST_list (&deps->pending_write_mems);
  free_INSN_LIST_list (&deps->last_pending_memory_flush);

  /* Without the EXECUTE_IF_SET, this loop is executed max_reg * nr_regions
     times.  For a testcase with 42000 regs and 8000 small basic blocks,
     this loop accounted for nearly 60% (84 sec) of the total -O2 runtime.  */
  EXECUTE_IF_SET_IN_REG_SET (&deps->reg_last_in_use, 0, i, rsi)
    {
      struct deps_reg *reg_last = &deps->reg_last[i];
      if (reg_last->uses)
	free_INSN_LIST_list (&reg_last->uses);
      if (reg_last->sets)
	free_INSN_LIST_list (&reg_last->sets);
      if (reg_last->clobbers)
	free_INSN_LIST_list (&reg_last->clobbers);
    }
  CLEAR_REG_SET (&deps->reg_last_in_use);
  CLEAR_REG_SET (&deps->reg_conditional_sets);

  free (deps->reg_last);
}

/* If it is profitable to use them, initialize caches for tracking
   dependency information.  LUID is the number of insns to be scheduled,
   it is used in the estimate of profitability.  */

void
init_dependency_caches (int luid)
{
  /* ?!? We could save some memory by computing a per-region luid mapping
     which could reduce both the number of vectors in the cache and the size
     of each vector.  Instead we just avoid the cache entirely unless the
     average number of instructions in a basic block is very high.  See
     the comment before the declaration of true_dependency_cache for
     what we consider "very high".  */
  if (luid / n_basic_blocks > 100 * 5)
    {
      cache_size = 0;
      extend_dependency_caches (luid, true);
    }
}

/* Create or extend (depending on CREATE_P) dependency caches to
   size N.  */
void
extend_dependency_caches (int n, bool create_p)
{
  if (create_p || true_dependency_cache)
    {
      int i, luid = cache_size + n;

      true_dependency_cache = XRESIZEVEC (bitmap_head, true_dependency_cache,
					  luid);
      output_dependency_cache = XRESIZEVEC (bitmap_head,
					    output_dependency_cache, luid);
      anti_dependency_cache = XRESIZEVEC (bitmap_head, anti_dependency_cache,
					  luid);
#ifdef ENABLE_CHECKING
      forward_dependency_cache = XRESIZEVEC (bitmap_head,
					     forward_dependency_cache, luid);
#endif
      if (current_sched_info->flags & DO_SPECULATION)
        spec_dependency_cache = XRESIZEVEC (bitmap_head, spec_dependency_cache,
					    luid);

      for (i = cache_size; i < luid; i++)
	{
	  bitmap_initialize (&true_dependency_cache[i], 0);
	  bitmap_initialize (&output_dependency_cache[i], 0);
	  bitmap_initialize (&anti_dependency_cache[i], 0);
#ifdef ENABLE_CHECKING
	  bitmap_initialize (&forward_dependency_cache[i], 0);
#endif
          if (current_sched_info->flags & DO_SPECULATION)
            bitmap_initialize (&spec_dependency_cache[i], 0);
	}
      cache_size = luid;
    }
}

/* Free the caches allocated in init_dependency_caches.  */

void
free_dependency_caches (void)
{
  if (true_dependency_cache)
    {
      int i;

      for (i = 0; i < cache_size; i++)
	{
	  bitmap_clear (&true_dependency_cache[i]);
	  bitmap_clear (&output_dependency_cache[i]);
	  bitmap_clear (&anti_dependency_cache[i]);
#ifdef ENABLE_CHECKING
	  bitmap_clear (&forward_dependency_cache[i]);
#endif
          if (current_sched_info->flags & DO_SPECULATION)
            bitmap_clear (&spec_dependency_cache[i]);
	}
      free (true_dependency_cache);
      true_dependency_cache = NULL;
      free (output_dependency_cache);
      output_dependency_cache = NULL;
      free (anti_dependency_cache);
      anti_dependency_cache = NULL;
#ifdef ENABLE_CHECKING
      free (forward_dependency_cache);
      forward_dependency_cache = NULL;
#endif
      if (current_sched_info->flags & DO_SPECULATION)
        {
          free (spec_dependency_cache);
          spec_dependency_cache = NULL;
        }
    }
}

/* Initialize some global variables needed by the dependency analysis
   code.  */

void
init_deps_global (void)
{
  reg_pending_sets = ALLOC_REG_SET (&reg_obstack);
  reg_pending_clobbers = ALLOC_REG_SET (&reg_obstack);
  reg_pending_uses = ALLOC_REG_SET (&reg_obstack);
  reg_pending_barrier = NOT_A_BARRIER;
}

/* Free everything used by the dependency analysis code.  */

void
finish_deps_global (void)
{
  FREE_REG_SET (reg_pending_sets);
  FREE_REG_SET (reg_pending_clobbers);
  FREE_REG_SET (reg_pending_uses);
}

/* Insert LINK into the dependence chain pointed to by LINKP and 
   maintain the sort order.  */
static void
adjust_add_sorted_back_dep (rtx insn, rtx link, rtx *linkp)
{
  gcc_assert (current_sched_info->flags & DO_SPECULATION);
  
  /* If the insn cannot move speculatively, but the link is speculative,   
     make it hard dependence.  */
  if (HAS_INTERNAL_DEP (insn)
      && (DEP_STATUS (link) & SPECULATIVE))
    {      
      DEP_STATUS (link) &= ~SPECULATIVE;
      
      if (true_dependency_cache)
        bitmap_clear_bit (&spec_dependency_cache[INSN_LUID (insn)],
			  INSN_LUID (XEXP (link, 0)));
    }

  /* Non-speculative links go at the head of LOG_LINKS, followed by
     speculative links.  */
  if (DEP_STATUS (link) & SPECULATIVE)
    while (*linkp && !(DEP_STATUS (*linkp) & SPECULATIVE))
      linkp = &XEXP (*linkp, 1);

  XEXP (link, 1) = *linkp;
  *linkp = link;
}

/* Move the dependence pointed to by LINKP to the back dependencies  
   of INSN, and also add this dependence to the forward ones.  All LOG_LINKS,
   except one pointed to by LINKP, must be sorted.  */
static void
adjust_back_add_forw_dep (rtx insn, rtx *linkp)
{
  rtx link;

  gcc_assert (current_sched_info->flags & DO_SPECULATION);

  link = *linkp;
  *linkp = XEXP (*linkp, 1);  

  adjust_add_sorted_back_dep (insn, link, &LOG_LINKS (insn));
  add_forw_dep (insn, link);
}

/* Remove forward dependence ELEM from the DEPS_LIST of INSN.  */
static void
delete_forw_dep (rtx insn, rtx elem)
{
  gcc_assert (current_sched_info->flags & DO_SPECULATION);

#ifdef ENABLE_CHECKING
  if (true_dependency_cache)
    bitmap_clear_bit (&forward_dependency_cache[INSN_LUID (elem)],
		      INSN_LUID (insn));
#endif

  remove_free_DEPS_LIST_elem (insn, &INSN_DEPEND (elem));    
  INSN_DEP_COUNT (insn)--;
}

/* Estimate the weakness of dependence between MEM1 and MEM2.  */
static dw_t
estimate_dep_weak (rtx mem1, rtx mem2)
{
  rtx r1, r2;

  if (mem1 == mem2)
    /* MEMs are the same - don't speculate.  */
    return MIN_DEP_WEAK;

  r1 = XEXP (mem1, 0);
  r2 = XEXP (mem2, 0);

  if (r1 == r2
      || (REG_P (r1) && REG_P (r2)
	  && REGNO (r1) == REGNO (r2)))
    /* Again, MEMs are the same.  */
    return MIN_DEP_WEAK;
  else if ((REG_P (r1) && !REG_P (r2))
	   || (!REG_P (r1) && REG_P (r2)))
    /* Different addressing modes - reason to be more speculative,
       than usual.  */
    return NO_DEP_WEAK - (NO_DEP_WEAK - UNCERTAIN_DEP_WEAK) / 2;
  else
    /* We can't say anything about the dependence.  */
    return UNCERTAIN_DEP_WEAK;
}

/* Add or update backward dependence between INSN and ELEM with type DEP_TYPE.
   This function can handle same INSN and ELEM (INSN == ELEM).
   It is a convenience wrapper.  */
void
add_dependence (rtx insn, rtx elem, enum reg_note dep_type)
{
  ds_t ds;
  
  if (dep_type == REG_DEP_TRUE)
    ds = DEP_TRUE;
  else if (dep_type == REG_DEP_OUTPUT)
    ds = DEP_OUTPUT;
  else if (dep_type == REG_DEP_ANTI)
    ds = DEP_ANTI;
  else
    gcc_unreachable ();

  maybe_add_or_update_back_dep_1 (insn, elem, dep_type, ds, 0, 0, 0);
}

/* Add or update backward dependence between INSN and ELEM
   with given type DEP_TYPE and dep_status DS.
   This function is a convenience wrapper.  */
enum DEPS_ADJUST_RESULT
add_or_update_back_dep (rtx insn, rtx elem, enum reg_note dep_type, ds_t ds)
{
  return add_or_update_back_dep_1 (insn, elem, dep_type, ds, 0, 0, 0);
}

/* Add or update both backward and forward dependencies between INSN and ELEM
   with given type DEP_TYPE and dep_status DS.  */
void
add_or_update_back_forw_dep (rtx insn, rtx elem, enum reg_note dep_type,
			     ds_t ds)
{
  enum DEPS_ADJUST_RESULT res;
  rtx *linkp;

  res = add_or_update_back_dep_1 (insn, elem, dep_type, ds, 0, 0, &linkp);

  if (res == DEP_CHANGED || res == DEP_CREATED)
    {
      if (res == DEP_CHANGED)
	delete_forw_dep (insn, elem);
      else if (res == DEP_CREATED)
	linkp = &LOG_LINKS (insn);

      adjust_back_add_forw_dep (insn, linkp);
    }
}

/* Add both backward and forward dependencies between INSN and ELEM
   with given type DEP_TYPE and dep_status DS.  */
void
add_back_forw_dep (rtx insn, rtx elem, enum reg_note dep_type, ds_t ds)
{
  add_back_dep (insn, elem, dep_type, ds);  
  adjust_back_add_forw_dep (insn, &LOG_LINKS (insn));    
}

/* Remove both backward and forward dependencies between INSN and ELEM.  */
void
delete_back_forw_dep (rtx insn, rtx elem)
{
  gcc_assert (current_sched_info->flags & DO_SPECULATION);

  if (true_dependency_cache != NULL)
    {
      bitmap_clear_bit (&true_dependency_cache[INSN_LUID (insn)],
			INSN_LUID (elem));
      bitmap_clear_bit (&anti_dependency_cache[INSN_LUID (insn)],
			INSN_LUID (elem));
      bitmap_clear_bit (&output_dependency_cache[INSN_LUID (insn)],
			INSN_LUID (elem));
      bitmap_clear_bit (&spec_dependency_cache[INSN_LUID (insn)],
			INSN_LUID (elem));
    }

  remove_free_DEPS_LIST_elem (elem, &LOG_LINKS (insn));
  delete_forw_dep (insn, elem);
}

/* Return weakness of speculative type TYPE in the dep_status DS.  */
dw_t
get_dep_weak (ds_t ds, ds_t type)
{
  ds = ds & type;
  switch (type)
    {
    case BEGIN_DATA: ds >>= BEGIN_DATA_BITS_OFFSET; break;
    case BE_IN_DATA: ds >>= BE_IN_DATA_BITS_OFFSET; break;
    case BEGIN_CONTROL: ds >>= BEGIN_CONTROL_BITS_OFFSET; break;
    case BE_IN_CONTROL: ds >>= BE_IN_CONTROL_BITS_OFFSET; break;
    default: gcc_unreachable ();
    }

  gcc_assert (MIN_DEP_WEAK <= ds && ds <= MAX_DEP_WEAK);
  return (dw_t) ds;
}

/* Return the dep_status, which has the same parameters as DS, except for
   speculative type TYPE, that will have weakness DW.  */
ds_t
set_dep_weak (ds_t ds, ds_t type, dw_t dw)
{
  gcc_assert (MIN_DEP_WEAK <= dw && dw <= MAX_DEP_WEAK);

  ds &= ~type;
  switch (type)
    {
    case BEGIN_DATA: ds |= ((ds_t) dw) << BEGIN_DATA_BITS_OFFSET; break;
    case BE_IN_DATA: ds |= ((ds_t) dw) << BE_IN_DATA_BITS_OFFSET; break;
    case BEGIN_CONTROL: ds |= ((ds_t) dw) << BEGIN_CONTROL_BITS_OFFSET; break;
    case BE_IN_CONTROL: ds |= ((ds_t) dw) << BE_IN_CONTROL_BITS_OFFSET; break;
    default: gcc_unreachable ();
    }
  return ds;
}

/* Return the join of two dep_statuses DS1 and DS2.  */
ds_t
ds_merge (ds_t ds1, ds_t ds2)
{
  ds_t ds, t;

  gcc_assert ((ds1 & SPECULATIVE) && (ds2 & SPECULATIVE));

  ds = (ds1 & DEP_TYPES) | (ds2 & DEP_TYPES);

  t = FIRST_SPEC_TYPE;
  do
    {
      if ((ds1 & t) && !(ds2 & t))
	ds |= ds1 & t;
      else if (!(ds1 & t) && (ds2 & t))
	ds |= ds2 & t;
      else if ((ds1 & t) && (ds2 & t))
	{
	  ds_t dw;

	  dw = ((ds_t) get_dep_weak (ds1, t)) * ((ds_t) get_dep_weak (ds2, t));
	  dw /= MAX_DEP_WEAK;
	  if (dw < MIN_DEP_WEAK)
	    dw = MIN_DEP_WEAK;

	  ds = set_dep_weak (ds, t, (dw_t) dw);
	}

      if (t == LAST_SPEC_TYPE)
	break;
      t <<= SPEC_TYPE_SHIFT;
    }
  while (1);

  return ds;
}

#ifdef INSN_SCHEDULING
#ifdef ENABLE_CHECKING
/* Verify that dependence type and status are consistent.
   If RELAXED_P is true, then skip dep_weakness checks.  */
static void
check_dep_status (enum reg_note dt, ds_t ds, bool relaxed_p)
{
  /* Check that dependence type contains the same bits as the status.  */
  if (dt == REG_DEP_TRUE)
    gcc_assert (ds & DEP_TRUE);
  else if (dt == REG_DEP_OUTPUT)
    gcc_assert ((ds & DEP_OUTPUT)
		&& !(ds & DEP_TRUE));    
  else 
    gcc_assert ((dt == REG_DEP_ANTI)
		&& (ds & DEP_ANTI)
		&& !(ds & (DEP_OUTPUT | DEP_TRUE)));

  /* HARD_DEP can not appear in dep_status of a link.  */
  gcc_assert (!(ds & HARD_DEP));	  

  /* Check that dependence status is set correctly when speculation is not
     supported.  */
  if (!(current_sched_info->flags & DO_SPECULATION))
    gcc_assert (!(ds & SPECULATIVE));
  else if (ds & SPECULATIVE)
    {
      if (!relaxed_p)
	{
	  ds_t type = FIRST_SPEC_TYPE;

	  /* Check that dependence weakness is in proper range.  */
	  do
	    {
	      if (ds & type)
		get_dep_weak (ds, type);

	      if (type == LAST_SPEC_TYPE)
		break;
	      type <<= SPEC_TYPE_SHIFT;
	    }
	  while (1);
	}

      if (ds & BEGIN_SPEC)
	{
	  /* Only true dependence can be data speculative.  */
	  if (ds & BEGIN_DATA)
	    gcc_assert (ds & DEP_TRUE);

	  /* Control dependencies in the insn scheduler are represented by
	     anti-dependencies, therefore only anti dependence can be
	     control speculative.  */
	  if (ds & BEGIN_CONTROL)
	    gcc_assert (ds & DEP_ANTI);
	}
      else
	{
	  /* Subsequent speculations should resolve true dependencies.  */
	  gcc_assert ((ds & DEP_TYPES) == DEP_TRUE);
	}
          
      /* Check that true and anti dependencies can't have other speculative 
	 statuses.  */
      if (ds & DEP_TRUE)
	gcc_assert (ds & (BEGIN_DATA | BE_IN_SPEC));
      /* An output dependence can't be speculative at all.  */
      gcc_assert (!(ds & DEP_OUTPUT));
      if (ds & DEP_ANTI)
	gcc_assert (ds & BEGIN_CONTROL);
    }
}
#endif
#endif  
