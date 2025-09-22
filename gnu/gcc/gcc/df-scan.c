/* FIXME: We need to go back and add the warning messages about code
   moved across setjmp.  */


/* Scanning of rtl for dataflow analysis.
   Copyright (C) 1999, 2000, 2001, 2002, 2003, 2004, 2005, 2006
   Free Software Foundation, Inc.
   Originally contributed by Michael P. Hayes 
             (m.hayes@elec.canterbury.ac.nz, mhayes@redhat.com)
   Major rewrite contributed by Danny Berlin (dberlin@dberlin.org)
             and Kenneth Zadeck (zadeck@naturalbridge.com).

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
02110-1301, USA.
*/

#include "config.h"
#include "system.h"
#include "coretypes.h"
#include "tm.h"
#include "rtl.h"
#include "tm_p.h"
#include "insn-config.h"
#include "recog.h"
#include "function.h"
#include "regs.h"
#include "output.h"
#include "alloc-pool.h"
#include "flags.h"
#include "hard-reg-set.h"
#include "basic-block.h"
#include "sbitmap.h"
#include "bitmap.h"
#include "timevar.h"
#include "tree.h"
#include "target.h"
#include "target-def.h"
#include "df.h"

#ifndef HAVE_epilogue
#define HAVE_epilogue 0
#endif
#ifndef HAVE_prologue
#define HAVE_prologue 0
#endif
#ifndef HAVE_sibcall_epilogue
#define HAVE_sibcall_epilogue 0
#endif

#ifndef EPILOGUE_USES
#define EPILOGUE_USES(REGNO)  0
#endif

/* The bitmap_obstack is used to hold some static variables that
   should not be reset after each function is compiled.  */

static bitmap_obstack persistent_obstack;

/* The set of hard registers in eliminables[i].from. */

static HARD_REG_SET elim_reg_set;

/* This is a bitmap copy of regs_invalidated_by_call so that we can
   easily add it into bitmaps, etc. */ 

bitmap df_invalidated_by_call = NULL;

/* Initialize ur_in and ur_out as if all hard registers were partially
   available.  */

static void df_ref_record (struct dataflow *, rtx, rtx *, 
			   basic_block, rtx, enum df_ref_type,
			   enum df_ref_flags, bool record_live);
static void df_def_record_1 (struct dataflow *, rtx, basic_block, rtx,
			     enum df_ref_flags, bool record_live);
static void df_defs_record (struct dataflow *, rtx, basic_block, rtx);
static void df_uses_record (struct dataflow *, rtx *, enum df_ref_type,
			    basic_block, rtx, enum df_ref_flags);

static void df_insn_refs_record (struct dataflow *, basic_block, rtx);
static void df_bb_refs_record (struct dataflow *, basic_block);
static void df_refs_record (struct dataflow *, bitmap);
static struct df_ref *df_ref_create_structure (struct dataflow *, rtx, rtx *, 
					       basic_block, rtx, enum df_ref_type, 
					       enum df_ref_flags);
static void df_record_entry_block_defs (struct dataflow *);
static void df_record_exit_block_uses (struct dataflow *);
static void df_grow_reg_info (struct dataflow *, struct df_ref_info *);
static void df_grow_ref_info (struct df_ref_info *, unsigned int);
static void df_grow_insn_info (struct df *);


/*----------------------------------------------------------------------------
   SCANNING DATAFLOW PROBLEM

   There are several ways in which scanning looks just like the other
   dataflow problems.  It shares the all the mechanisms for local info
   as well as basic block info.  Where it differs is when and how often
   it gets run.  It also has no need for the iterative solver.
----------------------------------------------------------------------------*/

/* Problem data for the scanning dataflow function.  */
struct df_scan_problem_data
{
  alloc_pool ref_pool;
  alloc_pool insn_pool;
  alloc_pool reg_pool;
  alloc_pool mw_reg_pool;
  alloc_pool mw_link_pool;
};

typedef struct df_scan_bb_info *df_scan_bb_info_t;

static void 
df_scan_free_internal (struct dataflow *dflow)
{
  struct df *df = dflow->df;
  struct df_scan_problem_data *problem_data
    = (struct df_scan_problem_data *) dflow->problem_data;

  free (df->def_info.regs);
  free (df->def_info.refs);
  memset (&df->def_info, 0, (sizeof (struct df_ref_info)));

  free (df->use_info.regs);
  free (df->use_info.refs);
  memset (&df->use_info, 0, (sizeof (struct df_ref_info)));

  free (df->insns);
  df->insns = NULL;
  df->insns_size = 0;

  free (dflow->block_info);
  dflow->block_info = NULL;
  dflow->block_info_size = 0;

  BITMAP_FREE (df->hardware_regs_used);
  BITMAP_FREE (df->entry_block_defs);
  BITMAP_FREE (df->exit_block_uses);

  free_alloc_pool (dflow->block_pool);
  free_alloc_pool (problem_data->ref_pool);
  free_alloc_pool (problem_data->insn_pool);
  free_alloc_pool (problem_data->reg_pool);
  free_alloc_pool (problem_data->mw_reg_pool);
  free_alloc_pool (problem_data->mw_link_pool);
}


/* Get basic block info.  */

struct df_scan_bb_info *
df_scan_get_bb_info (struct dataflow *dflow, unsigned int index)
{
  gcc_assert (index < dflow->block_info_size); 
  return (struct df_scan_bb_info *) dflow->block_info[index];
}


/* Set basic block info.  */

static void
df_scan_set_bb_info (struct dataflow *dflow, unsigned int index, 
		     struct df_scan_bb_info *bb_info)
{
  gcc_assert (index < dflow->block_info_size); 
  dflow->block_info[index] = (void *) bb_info;
}


/* Free basic block info.  */

static void
df_scan_free_bb_info (struct dataflow *dflow, basic_block bb, void *vbb_info)
{
  struct df_scan_bb_info *bb_info = (struct df_scan_bb_info *) vbb_info;
  if (bb_info)
    {
      df_bb_refs_delete (dflow, bb->index);
      pool_free (dflow->block_pool, bb_info);
    }
}


/* Allocate the problem data for the scanning problem.  This should be
   called when the problem is created or when the entire function is to
   be rescanned.  */

static void 
df_scan_alloc (struct dataflow *dflow, bitmap blocks_to_rescan, 
	       bitmap all_blocks ATTRIBUTE_UNUSED)
{
  struct df *df = dflow->df;
  struct df_scan_problem_data *problem_data;
  unsigned int insn_num = get_max_uid () + 1;
  unsigned int block_size = 50;
  unsigned int bb_index;
  bitmap_iterator bi;

  /* Given the number of pools, this is really faster than tearing
     everything apart.  */
  if (dflow->problem_data)
    df_scan_free_internal (dflow);

  dflow->block_pool 
    = create_alloc_pool ("df_scan_block pool", 
			 sizeof (struct df_scan_bb_info), 
			 block_size);

  problem_data = XNEW (struct df_scan_problem_data);
  dflow->problem_data = problem_data;

  problem_data->ref_pool 
    = create_alloc_pool ("df_scan_ref pool", 
			 sizeof (struct df_ref), block_size);
  problem_data->insn_pool 
    = create_alloc_pool ("df_scan_insn pool", 
			 sizeof (struct df_insn_info), block_size);
  problem_data->reg_pool 
    = create_alloc_pool ("df_scan_reg pool", 
			 sizeof (struct df_reg_info), block_size);
  problem_data->mw_reg_pool 
    = create_alloc_pool ("df_scan_mw_reg pool", 
			 sizeof (struct df_mw_hardreg), block_size);
  problem_data->mw_link_pool 
    = create_alloc_pool ("df_scan_mw_link pool", 
			 sizeof (struct df_link), block_size);

  insn_num += insn_num / 4; 
  df_grow_reg_info (dflow, &df->def_info);
  df_grow_ref_info (&df->def_info, insn_num);

  df_grow_reg_info (dflow, &df->use_info);
  df_grow_ref_info (&df->use_info, insn_num *2);

  df_grow_insn_info (df);
  df_grow_bb_info (dflow);

  EXECUTE_IF_SET_IN_BITMAP (blocks_to_rescan, 0, bb_index, bi)
    {
      struct df_scan_bb_info *bb_info = df_scan_get_bb_info (dflow, bb_index);
      if (!bb_info)
	{
	  bb_info = (struct df_scan_bb_info *) pool_alloc (dflow->block_pool);
	  df_scan_set_bb_info (dflow, bb_index, bb_info);
	}
      bb_info->artificial_defs = NULL;
      bb_info->artificial_uses = NULL;
    }

  df->hardware_regs_used = BITMAP_ALLOC (NULL);
  df->entry_block_defs = BITMAP_ALLOC (NULL);
  df->exit_block_uses = BITMAP_ALLOC (NULL);
}


/* Free all of the data associated with the scan problem.  */

static void 
df_scan_free (struct dataflow *dflow)
{
  struct df *df = dflow->df;
  
  if (dflow->problem_data)
    {
      df_scan_free_internal (dflow);
      free (dflow->problem_data);
    }

  if (df->blocks_to_scan)
    BITMAP_FREE (df->blocks_to_scan);
  
  if (df->blocks_to_analyze)
    BITMAP_FREE (df->blocks_to_analyze);

  free (dflow);
}

static void 
df_scan_dump (struct dataflow *dflow ATTRIBUTE_UNUSED, FILE *file ATTRIBUTE_UNUSED)
{
  struct df *df = dflow->df;
  int i;

  fprintf (file, "  invalidated by call \t");
  dump_bitmap (file, df_invalidated_by_call);
  fprintf (file, "  hardware regs used \t");
  dump_bitmap (file, df->hardware_regs_used);
  fprintf (file, "  entry block uses \t");
  dump_bitmap (file, df->entry_block_defs);
  fprintf (file, "  exit block uses \t");
  dump_bitmap (file, df->exit_block_uses);
  fprintf (file, "  regs ever live \t");
  for (i = 0; i < FIRST_PSEUDO_REGISTER; i++)
    if (regs_ever_live[i])
      fprintf (file, "%d ", i);
  fprintf (file, "\n");
}

static struct df_problem problem_SCAN =
{
  DF_SCAN,                    /* Problem id.  */
  DF_NONE,                    /* Direction.  */
  df_scan_alloc,              /* Allocate the problem specific data.  */
  NULL,                       /* Reset global information.  */
  df_scan_free_bb_info,       /* Free basic block info.  */
  NULL,                       /* Local compute function.  */
  NULL,                       /* Init the solution specific data.  */
  NULL,                       /* Iterative solver.  */
  NULL,                       /* Confluence operator 0.  */ 
  NULL,                       /* Confluence operator n.  */ 
  NULL,                       /* Transfer function.  */
  NULL,                       /* Finalize function.  */
  df_scan_free,               /* Free all of the problem information.  */
  df_scan_dump,               /* Debugging.  */
  NULL,                       /* Dependent problem.  */
  0                           /* Changeable flags.  */
};


/* Create a new DATAFLOW instance and add it to an existing instance
   of DF.  The returned structure is what is used to get at the
   solution.  */

struct dataflow *
df_scan_add_problem (struct df *df, int flags)
{
  return df_add_problem (df, &problem_SCAN, flags);
}

/*----------------------------------------------------------------------------
   Storage Allocation Utilities
----------------------------------------------------------------------------*/


/* First, grow the reg_info information.  If the current size is less than
   the number of psuedos, grow to 25% more than the number of
   pseudos.  

   Second, assure that all of the slots up to max_reg_num have been
   filled with reg_info structures.  */

static void 
df_grow_reg_info (struct dataflow *dflow, struct df_ref_info *ref_info)
{
  unsigned int max_reg = max_reg_num ();
  unsigned int new_size = max_reg;
  struct df_scan_problem_data *problem_data
    = (struct df_scan_problem_data *) dflow->problem_data;
  unsigned int i;

  if (ref_info->regs_size < new_size)
    {
      new_size += new_size / 4;
      ref_info->regs = xrealloc (ref_info->regs, 
				 new_size *sizeof (struct df_reg_info*));
      ref_info->regs_size = new_size;
    }

  for (i = ref_info->regs_inited; i < max_reg; i++)
    {
      struct df_reg_info *reg_info = pool_alloc (problem_data->reg_pool);
      memset (reg_info, 0, sizeof (struct df_reg_info));
      ref_info->regs[i] = reg_info;
    }
  
  ref_info->regs_inited = max_reg;
}


/* Grow the ref information.  */

static void 
df_grow_ref_info (struct df_ref_info *ref_info, unsigned int new_size)
{
  if (ref_info->refs_size < new_size)
    {
      ref_info->refs = xrealloc (ref_info->refs, 
				 new_size *sizeof (struct df_ref *));
      memset (ref_info->refs + ref_info->refs_size, 0,
	      (new_size - ref_info->refs_size) *sizeof (struct df_ref *));
      ref_info->refs_size = new_size;
    }
}


/* Grow the ref information.  If the current size is less than the
   number of instructions, grow to 25% more than the number of
   instructions.  */

static void 
df_grow_insn_info (struct df *df)
{
  unsigned int new_size = get_max_uid () + 1;
  if (df->insns_size < new_size)
    {
      new_size += new_size / 4;
      df->insns = xrealloc (df->insns, 
			    new_size *sizeof (struct df_insn_info *));
      memset (df->insns + df->insns_size, 0,
	      (new_size - df->insns_size) *sizeof (struct df_insn_info *));
      df->insns_size = new_size;
    }
}




/*----------------------------------------------------------------------------
   PUBLIC INTERFACES FOR SMALL GRAIN CHANGES TO SCANNING.
----------------------------------------------------------------------------*/

/* Rescan some BLOCKS or all the blocks defined by the last call to
   df_set_blocks if BLOCKS is NULL);  */

void
df_rescan_blocks (struct df *df, bitmap blocks)
{
  bitmap local_blocks_to_scan = BITMAP_ALLOC (NULL);

  struct dataflow *dflow = df->problems_by_index[DF_SCAN];
  basic_block bb;

  df->def_info.refs_organized = false;
  df->use_info.refs_organized = false;

  if (blocks)
    {
      int i;
      unsigned int bb_index;
      bitmap_iterator bi;
      bool cleared_bits = false;

      /* Need to assure that there are space in all of the tables.  */
      unsigned int insn_num = get_max_uid () + 1;
      insn_num += insn_num / 4;

      df_grow_reg_info (dflow, &df->def_info);
      df_grow_ref_info (&df->def_info, insn_num);
      
      df_grow_reg_info (dflow, &df->use_info);
      df_grow_ref_info (&df->use_info, insn_num *2);
      
      df_grow_insn_info (df);
      df_grow_bb_info (dflow);

      bitmap_copy (local_blocks_to_scan, blocks);

      EXECUTE_IF_SET_IN_BITMAP (blocks, 0, bb_index, bi)
	{
	  basic_block bb = BASIC_BLOCK (bb_index);
	  if (!bb)
	    {
	      bitmap_clear_bit (local_blocks_to_scan, bb_index);
	      cleared_bits = true;
	    }
	}

      if (cleared_bits)
	bitmap_copy (blocks, local_blocks_to_scan);

      df->def_info.add_refs_inline = true;
      df->use_info.add_refs_inline = true;

      for (i = df->num_problems_defined; i; i--)
	{
	  bitmap blocks_to_reset = NULL;
	  if (dflow->problem->reset_fun)
	    {
	      if (!blocks_to_reset)
		{
		  blocks_to_reset = BITMAP_ALLOC (NULL);
		  bitmap_copy (blocks_to_reset, local_blocks_to_scan);
		  if (df->blocks_to_scan)
		    bitmap_ior_into (blocks_to_reset, df->blocks_to_scan);
		}
	      dflow->problem->reset_fun (dflow, blocks_to_reset);
	    }
	  if (blocks_to_reset)
	    BITMAP_FREE (blocks_to_reset);
	}

      df_refs_delete (dflow, local_blocks_to_scan);

      /* This may be a mistake, but if an explicit blocks is passed in
         and the set of blocks to analyze has been explicitly set, add
         the extra blocks to blocks_to_analyze.  The alternative is to
         put an assert here.  We do not want this to just go by
         silently or else we may get storage leaks.  */
      if (df->blocks_to_analyze)
	bitmap_ior_into (df->blocks_to_analyze, blocks);
    }
  else
    {
      /* If we are going to do everything, just reallocate everything.
	 Most stuff is allocated in pools so this is faster than
	 walking it.  */
      if (df->blocks_to_analyze)
	bitmap_copy (local_blocks_to_scan, df->blocks_to_analyze);
      else
	FOR_ALL_BB (bb) 
	  {
	    bitmap_set_bit (local_blocks_to_scan, bb->index);
	  }
      df_scan_alloc (dflow, local_blocks_to_scan, NULL);

      df->def_info.add_refs_inline = false;
      df->use_info.add_refs_inline = false;
    }

  df_refs_record (dflow, local_blocks_to_scan);
#if 0
  bitmap_print (stderr, local_blocks_to_scan, "scanning: ", "\n");
#endif
      
  if (!df->blocks_to_scan)
    df->blocks_to_scan = BITMAP_ALLOC (NULL);

  bitmap_ior_into (df->blocks_to_scan, local_blocks_to_scan); 
  BITMAP_FREE (local_blocks_to_scan);
}


/* Create a new ref of type DF_REF_TYPE for register REG at address
   LOC within INSN of BB.  */

struct df_ref *
df_ref_create (struct df *df, rtx reg, rtx *loc, rtx insn, 
	       basic_block bb,
	       enum df_ref_type ref_type, 
	       enum df_ref_flags ref_flags)
{
  struct dataflow *dflow = df->problems_by_index[DF_SCAN];
  struct df_scan_bb_info *bb_info;
  
  df_grow_reg_info (dflow, &df->use_info);
  df_grow_reg_info (dflow, &df->def_info);
  df_grow_bb_info (dflow);
  
  /* Make sure there is the bb_info for this block.  */
  bb_info = df_scan_get_bb_info (dflow, bb->index);
  if (!bb_info)
    {
      bb_info = (struct df_scan_bb_info *) pool_alloc (dflow->block_pool);
      df_scan_set_bb_info (dflow, bb->index, bb_info);
      bb_info->artificial_defs = NULL;
      bb_info->artificial_uses = NULL;
    }

  if (ref_type == DF_REF_REG_DEF)
    df->def_info.add_refs_inline = true;
  else
    df->use_info.add_refs_inline = true;
  
  return df_ref_create_structure (dflow, reg, loc, bb, insn, ref_type, ref_flags);
}



/*----------------------------------------------------------------------------
   UTILITIES TO CREATE AND DESTROY REFS AND CHAINS.
----------------------------------------------------------------------------*/


/* Get the artificial uses for a basic block.  */

struct df_ref *
df_get_artificial_defs (struct df *df, unsigned int bb_index)
{
  struct dataflow *dflow = df->problems_by_index[DF_SCAN];
  return df_scan_get_bb_info (dflow, bb_index)->artificial_defs;
}


/* Get the artificial uses for a basic block.  */

struct df_ref *
df_get_artificial_uses (struct df *df, unsigned int bb_index)
{
  struct dataflow *dflow = df->problems_by_index[DF_SCAN];
  return df_scan_get_bb_info (dflow, bb_index)->artificial_uses;
}


/* Link REF at the front of reg_use or reg_def chain for REGNO.  */

void
df_reg_chain_create (struct df_reg_info *reg_info, 
		     struct df_ref *ref) 
{
  struct df_ref *head = reg_info->reg_chain;
  reg_info->reg_chain = ref;

  DF_REF_NEXT_REG (ref) = head;

  /* We cannot actually link to the head of the chain.  */
  DF_REF_PREV_REG (ref) = NULL;

  if (head)
    DF_REF_PREV_REG (head) = ref;
}


/* Remove REF from the CHAIN.  Return the head of the chain.  This
   will be CHAIN unless the REF was at the beginning of the chain.  */

static struct df_ref *
df_ref_unlink (struct df_ref *chain, struct df_ref *ref)
{
  struct df_ref *orig_chain = chain;
  struct df_ref *prev = NULL;
  while (chain)
    {
      if (chain == ref)
	{
	  if (prev)
	    {
	      prev->next_ref = ref->next_ref;
	      ref->next_ref = NULL;
	      return orig_chain;
	    }
	  else
	    {
	      chain = ref->next_ref;
	      ref->next_ref = NULL;
	      return chain;
	    }
	}

      prev = chain;
      chain = chain->next_ref;
    }

  /* Someone passed in a ref that was not in the chain.  */
  gcc_unreachable ();
  return NULL;
}


/* Unlink and delete REF at the reg_use or reg_def chain.  Also delete
   the def-use or use-def chain if it exists.  Returns the next ref in
   uses or defs chain.  */

struct df_ref *
df_reg_chain_unlink (struct dataflow *dflow, struct df_ref *ref) 
{
  struct df *df = dflow->df;
  struct df_ref *next = DF_REF_NEXT_REG (ref);  
  struct df_ref *prev = DF_REF_PREV_REG (ref);
  struct df_scan_problem_data *problem_data
    = (struct df_scan_problem_data *) dflow->problem_data;
  struct df_reg_info *reg_info;
  struct df_ref *next_ref = ref->next_ref;
  unsigned int id = DF_REF_ID (ref);

  if (DF_REF_TYPE (ref) == DF_REF_REG_DEF)
    {
      reg_info = DF_REG_DEF_GET (df, DF_REF_REGNO (ref));
      df->def_info.bitmap_size--;
      if (df->def_info.refs && (id < df->def_info.refs_size))
	DF_DEFS_SET (df, id, NULL);
    }
  else 
    {
      reg_info = DF_REG_USE_GET (df, DF_REF_REGNO (ref));
      df->use_info.bitmap_size--;
      if (df->use_info.refs && (id < df->use_info.refs_size))
	DF_USES_SET (df, id, NULL);
    }
  
  /* Delete any def-use or use-def chains that start here.  */
  if (DF_REF_CHAIN (ref))
    df_chain_unlink (df->problems_by_index[DF_CHAIN], ref, NULL);

  reg_info->n_refs--;

  /* Unlink from the reg chain.  If there is no prev, this is the
     first of the list.  If not, just join the next and prev.  */
  if (prev)
    {
      DF_REF_NEXT_REG (prev) = next;
      if (next)
	DF_REF_PREV_REG (next) = prev;
    }
  else
    {
      reg_info->reg_chain = next;
      if (next)
	DF_REF_PREV_REG (next) = NULL;
    }

  pool_free (problem_data->ref_pool, ref);
  return next_ref;
}


/* Unlink REF from all def-use/use-def chains, etc.  */

void
df_ref_remove (struct df *df, struct df_ref *ref)
{
  struct dataflow *dflow = df->problems_by_index[DF_SCAN];
  if (DF_REF_REG_DEF_P (ref))
    {
      if (DF_REF_FLAGS (ref) & DF_REF_ARTIFICIAL)
	{
	  struct df_scan_bb_info *bb_info 
	    = df_scan_get_bb_info (dflow, DF_REF_BB (ref)->index);
	  bb_info->artificial_defs 
	    = df_ref_unlink (bb_info->artificial_defs, ref);
	}
      else
	DF_INSN_UID_DEFS (df, DF_REF_INSN_UID (ref))
	  = df_ref_unlink (DF_INSN_UID_DEFS (df, DF_REF_INSN_UID (ref)), ref);

      if (df->def_info.add_refs_inline)
	DF_DEFS_SET (df, DF_REF_ID (ref), NULL);
    }
  else
    {
      if (DF_REF_FLAGS (ref) & DF_REF_ARTIFICIAL)
	{
	  struct df_scan_bb_info *bb_info 
	    = df_scan_get_bb_info (dflow, DF_REF_BB (ref)->index);
	  bb_info->artificial_uses 
	    = df_ref_unlink (bb_info->artificial_uses, ref);
	}
      else
	DF_INSN_UID_USES (df, DF_REF_INSN_UID (ref))
	  = df_ref_unlink (DF_INSN_UID_USES (df, DF_REF_INSN_UID (ref)), ref);
      
      if (df->use_info.add_refs_inline)
	DF_USES_SET (df, DF_REF_ID (ref), NULL);
    }

  df_reg_chain_unlink (dflow, ref);
}


/* Create the insn record for INSN.  If there was one there, zero it out.  */

static struct df_insn_info *
df_insn_create_insn_record (struct dataflow *dflow, rtx insn)
{
  struct df *df = dflow->df;
  struct df_scan_problem_data *problem_data
    = (struct df_scan_problem_data *) dflow->problem_data;

  struct df_insn_info *insn_rec = DF_INSN_GET (df, insn);
  if (!insn_rec)
    {
      insn_rec = pool_alloc (problem_data->insn_pool);
      DF_INSN_SET (df, insn, insn_rec);
    }
  memset (insn_rec, 0, sizeof (struct df_insn_info));

  return insn_rec;
}


/* Delete all of the refs information from INSN.  */

void 
df_insn_refs_delete (struct dataflow *dflow, rtx insn)
{
  struct df *df = dflow->df;
  unsigned int uid = INSN_UID (insn);
  struct df_insn_info *insn_info = NULL;
  struct df_ref *ref;
  struct df_scan_problem_data *problem_data
    = (struct df_scan_problem_data *) dflow->problem_data;

  if (uid < df->insns_size)
    insn_info = DF_INSN_UID_GET (df, uid);

  if (insn_info)
    {
      struct df_mw_hardreg *hardregs = insn_info->mw_hardregs;
      
      while (hardregs)
	{
	  struct df_mw_hardreg *next_hr = hardregs->next;
	  struct df_link *link = hardregs->regs;
	  while (link)
	    {
	      struct df_link *next_l = link->next;
	      pool_free (problem_data->mw_link_pool, link);
	      link = next_l;
	    }
	  
	  pool_free (problem_data->mw_reg_pool, hardregs);
	  hardregs = next_hr;
	}

      ref = insn_info->defs;
      while (ref) 
	ref = df_reg_chain_unlink (dflow, ref);
      
      ref = insn_info->uses;
      while (ref) 
	ref = df_reg_chain_unlink (dflow, ref);

      pool_free (problem_data->insn_pool, insn_info);
      DF_INSN_SET (df, insn, NULL);
    }
}


/* Delete all of the refs information from basic_block with BB_INDEX.  */

void
df_bb_refs_delete (struct dataflow *dflow, int bb_index)
{
  struct df_ref *def;
  struct df_ref *use;

  struct df_scan_bb_info *bb_info 
    = df_scan_get_bb_info (dflow, bb_index);
  rtx insn;
  basic_block bb = BASIC_BLOCK (bb_index);
  FOR_BB_INSNS (bb, insn)
    {
      if (INSN_P (insn))
	{
	  /* Record defs within INSN.  */
	  df_insn_refs_delete (dflow, insn);
	}
    }
  
  /* Get rid of any artificial uses or defs.  */
  if (bb_info)
    {
      def = bb_info->artificial_defs;
      while (def)
	def = df_reg_chain_unlink (dflow, def);
      bb_info->artificial_defs = NULL;
      use = bb_info->artificial_uses;
      while (use)
	use = df_reg_chain_unlink (dflow, use);
      bb_info->artificial_uses = NULL;
    }
}


/* Delete all of the refs information from BLOCKS.  */

void 
df_refs_delete (struct dataflow *dflow, bitmap blocks)
{
  bitmap_iterator bi;
  unsigned int bb_index;

  EXECUTE_IF_SET_IN_BITMAP (blocks, 0, bb_index, bi)
    {
      df_bb_refs_delete (dflow, bb_index);
    }
}


/* Take build ref table for either the uses or defs from the reg-use
   or reg-def chains.  */ 

void 
df_reorganize_refs (struct df_ref_info *ref_info)
{
  unsigned int m = ref_info->regs_inited;
  unsigned int regno;
  unsigned int offset = 0;
  unsigned int size = 0;

  if (ref_info->refs_organized)
    return;

  if (ref_info->refs_size < ref_info->bitmap_size)
    {  
      int new_size = ref_info->bitmap_size + ref_info->bitmap_size / 4;
      df_grow_ref_info (ref_info, new_size);
    }

  for (regno = 0; regno < m; regno++)
    {
      struct df_reg_info *reg_info = ref_info->regs[regno];
      int count = 0;
      if (reg_info)
	{
	  struct df_ref *ref = reg_info->reg_chain;
	  reg_info->begin = offset;
	  while (ref) 
	    {
	      ref_info->refs[offset] = ref;
	      DF_REF_ID (ref) = offset++;
	      ref = DF_REF_NEXT_REG (ref);
	      count++;
	      size++;
	    }
	  reg_info->n_refs = count;
	}
    }

  /* The bitmap size is not decremented when refs are deleted.  So
     reset it now that we have squished out all of the empty
     slots.  */
  ref_info->bitmap_size = size;
  ref_info->refs_organized = true;
  ref_info->add_refs_inline = true;
}


/*----------------------------------------------------------------------------
   Hard core instruction scanning code.  No external interfaces here,
   just a lot of routines that look inside insns.
----------------------------------------------------------------------------*/

/* Create a ref and add it to the reg-def or reg-use chains.  */

static struct df_ref *
df_ref_create_structure (struct dataflow *dflow, rtx reg, rtx *loc,
			 basic_block bb, rtx insn, 
			 enum df_ref_type ref_type, 
			 enum df_ref_flags ref_flags)
{
  struct df_ref *this_ref;
  struct df *df = dflow->df;
  int regno = REGNO (GET_CODE (reg) == SUBREG ? SUBREG_REG (reg) : reg);
  struct df_scan_problem_data *problem_data
    = (struct df_scan_problem_data *) dflow->problem_data;

  this_ref = pool_alloc (problem_data->ref_pool);
  DF_REF_REG (this_ref) = reg;
  DF_REF_REGNO (this_ref) =  regno;
  DF_REF_LOC (this_ref) = loc;
  DF_REF_INSN (this_ref) = insn;
  DF_REF_CHAIN (this_ref) = NULL;
  DF_REF_TYPE (this_ref) = ref_type;
  DF_REF_FLAGS (this_ref) = ref_flags;
  DF_REF_DATA (this_ref) = NULL;
  DF_REF_BB (this_ref) = bb;

  /* Link the ref into the reg_def and reg_use chains and keep a count
     of the instances.  */
  switch (ref_type)
    {
    case DF_REF_REG_DEF:
      {
	struct df_reg_info *reg_info = DF_REG_DEF_GET (df, regno);
	reg_info->n_refs++;
	
	/* Add the ref to the reg_def chain.  */
	df_reg_chain_create (reg_info, this_ref);
	DF_REF_ID (this_ref) = df->def_info.bitmap_size;
	if (df->def_info.add_refs_inline)
	  {
	    if (DF_DEFS_SIZE (df) >= df->def_info.refs_size)
	      {
		int new_size = df->def_info.bitmap_size 
		  + df->def_info.bitmap_size / 4;
		df_grow_ref_info (&df->def_info, new_size);
	      }
	    /* Add the ref to the big array of defs.  */
	    DF_DEFS_SET (df, df->def_info.bitmap_size, this_ref);
	    df->def_info.refs_organized = false;
	  }
	
	df->def_info.bitmap_size++;
	
	if (DF_REF_FLAGS (this_ref) & DF_REF_ARTIFICIAL)
	  {
	    struct df_scan_bb_info *bb_info 
	      = df_scan_get_bb_info (dflow, bb->index);
	    this_ref->next_ref = bb_info->artificial_defs;
	    bb_info->artificial_defs = this_ref;
	  }
	else
	  {
	    this_ref->next_ref = DF_INSN_GET (df, insn)->defs;
	    DF_INSN_GET (df, insn)->defs = this_ref;
	  }
      }
      break;

    case DF_REF_REG_MEM_LOAD:
    case DF_REF_REG_MEM_STORE:
    case DF_REF_REG_USE:
      {
	struct df_reg_info *reg_info = DF_REG_USE_GET (df, regno);
	reg_info->n_refs++;
	
	/* Add the ref to the reg_use chain.  */
	df_reg_chain_create (reg_info, this_ref);
	DF_REF_ID (this_ref) = df->use_info.bitmap_size;
	if (df->use_info.add_refs_inline)
	  {
	    if (DF_USES_SIZE (df) >= df->use_info.refs_size)
	      {
		int new_size = df->use_info.bitmap_size 
		  + df->use_info.bitmap_size / 4;
		df_grow_ref_info (&df->use_info, new_size);
	      }
	    /* Add the ref to the big array of defs.  */
	    DF_USES_SET (df, df->use_info.bitmap_size, this_ref);
	    df->use_info.refs_organized = false;
	  }
	
	df->use_info.bitmap_size++;
	if (DF_REF_FLAGS (this_ref) & DF_REF_ARTIFICIAL)
	  {
	    struct df_scan_bb_info *bb_info 
	      = df_scan_get_bb_info (dflow, bb->index);
	    this_ref->next_ref = bb_info->artificial_uses;
	    bb_info->artificial_uses = this_ref;
	  }
	else
	  {
	    this_ref->next_ref = DF_INSN_GET (df, insn)->uses;
	    DF_INSN_GET (df, insn)->uses = this_ref;
	  }
      }
      break;

    default:
      gcc_unreachable ();

    }
  return this_ref;
}


/* Create new references of type DF_REF_TYPE for each part of register REG
   at address LOC within INSN of BB.  */

static void
df_ref_record (struct dataflow *dflow, rtx reg, rtx *loc, 
	       basic_block bb, rtx insn, 
	       enum df_ref_type ref_type, 
	       enum df_ref_flags ref_flags, 
	       bool record_live)
{
  struct df *df = dflow->df;
  rtx oldreg = reg;
  unsigned int regno;

  gcc_assert (REG_P (reg) || GET_CODE (reg) == SUBREG);

  /* For the reg allocator we are interested in some SUBREG rtx's, but not
     all.  Notably only those representing a word extraction from a multi-word
     reg.  As written in the docu those should have the form
     (subreg:SI (reg:M A) N), with size(SImode) > size(Mmode).
     XXX Is that true?  We could also use the global word_mode variable.  */
  if ((dflow->flags & DF_SUBREGS) == 0
      && GET_CODE (reg) == SUBREG
      && (GET_MODE_SIZE (GET_MODE (reg)) < GET_MODE_SIZE (word_mode)
	  || GET_MODE_SIZE (GET_MODE (reg))
	       >= GET_MODE_SIZE (GET_MODE (SUBREG_REG (reg)))))
    {
      loc = &SUBREG_REG (reg);
      reg = *loc;
      ref_flags |= DF_REF_STRIPPED;
    }

  regno = REGNO (GET_CODE (reg) == SUBREG ? SUBREG_REG (reg) : reg);
  if (regno < FIRST_PSEUDO_REGISTER)
    {
      unsigned int i;
      unsigned int endregno;
      struct df_mw_hardreg *hardreg = NULL;
      struct df_scan_problem_data *problem_data
	= (struct df_scan_problem_data *) dflow->problem_data;

      if (!(dflow->flags & DF_HARD_REGS))
	return;

      /* GET_MODE (reg) is correct here.  We do not want to go into a SUBREG
         for the mode, because we only want to add references to regs, which
	 are really referenced.  E.g., a (subreg:SI (reg:DI 0) 0) does _not_
	 reference the whole reg 0 in DI mode (which would also include
	 reg 1, at least, if 0 and 1 are SImode registers).  */
      endregno = hard_regno_nregs[regno][GET_MODE (reg)];
      if (GET_CODE (reg) == SUBREG)
        regno += subreg_regno_offset (regno, GET_MODE (SUBREG_REG (reg)),
				      SUBREG_BYTE (reg), GET_MODE (reg));
      endregno += regno;

      /*  If this is a multiword hardreg, we create some extra datastructures that 
	  will enable us to easily build REG_DEAD and REG_UNUSED notes.  */
      if ((endregno != regno + 1) && insn)
	{
	  struct df_insn_info *insn_info = DF_INSN_GET (df, insn);
	  /* Sets to a subreg of a multiword register are partial. 
	     Sets to a non-subreg of a multiword register are not.  */
	  if (GET_CODE (oldreg) == SUBREG)
	    ref_flags |= DF_REF_PARTIAL;
	  ref_flags |= DF_REF_MW_HARDREG;
	  hardreg = pool_alloc (problem_data->mw_reg_pool);
	  hardreg->next = insn_info->mw_hardregs;
	  insn_info->mw_hardregs = hardreg;
	  hardreg->type = ref_type;
	  hardreg->flags = ref_flags;
	  hardreg->mw_reg = reg;
	  hardreg->regs = NULL;

	}

      for (i = regno; i < endregno; i++)
	{
	  struct df_ref *ref;

	  /* Calls are handled at call site because regs_ever_live
	     doesn't include clobbered regs, only used ones.  */
	  if (ref_type == DF_REF_REG_DEF && record_live)
	    regs_ever_live[i] = 1;
	  else if ((ref_type == DF_REF_REG_USE 
		   || ref_type == DF_REF_REG_MEM_STORE
		   || ref_type == DF_REF_REG_MEM_LOAD)
		   && ((ref_flags & DF_REF_ARTIFICIAL) == 0))
	    {
	      /* Set regs_ever_live on uses of non-eliminable frame
		 pointers and arg pointers.  */
	      if (!(TEST_HARD_REG_BIT (elim_reg_set, regno)
		     && (regno == FRAME_POINTER_REGNUM 
			 || regno == ARG_POINTER_REGNUM)))
		regs_ever_live[i] = 1;
	    }

	  ref = df_ref_create_structure (dflow, regno_reg_rtx[i], loc, 
					 bb, insn, ref_type, ref_flags);
	  if (hardreg)
	    {
	      struct df_link *link = pool_alloc (problem_data->mw_link_pool);

	      link->next = hardreg->regs;
	      link->ref = ref;
	      hardreg->regs = link;
	    }
	}
    }
  else
    {
      df_ref_create_structure (dflow, reg, loc, 
			       bb, insn, ref_type, ref_flags);
    }
}


/* A set to a non-paradoxical SUBREG for which the number of word_mode units
   covered by the outer mode is smaller than that covered by the inner mode,
   is a read-modify-write operation.
   This function returns true iff the SUBREG X is such a SUBREG.  */

bool
df_read_modify_subreg_p (rtx x)
{
  unsigned int isize, osize;
  if (GET_CODE (x) != SUBREG)
    return false;
  isize = GET_MODE_SIZE (GET_MODE (SUBREG_REG (x)));
  osize = GET_MODE_SIZE (GET_MODE (x));
  return (isize > osize && isize > UNITS_PER_WORD);
}


/* Process all the registers defined in the rtx, X.
   Autoincrement/decrement definitions will be picked up by
   df_uses_record.  */

static void
df_def_record_1 (struct dataflow *dflow, rtx x, 
		 basic_block bb, rtx insn, 
		 enum df_ref_flags flags, bool record_live)
{
  rtx *loc;
  rtx dst;
  bool dst_in_strict_lowpart = false;

 /* We may recursively call ourselves on EXPR_LIST when dealing with PARALLEL
     construct.  */
  if (GET_CODE (x) == EXPR_LIST || GET_CODE (x) == CLOBBER)
    loc = &XEXP (x, 0);
  else
    loc = &SET_DEST (x);
  dst = *loc;

  /* It is legal to have a set destination be a parallel. */
  if (GET_CODE (dst) == PARALLEL)
    {
      int i;

      for (i = XVECLEN (dst, 0) - 1; i >= 0; i--)
	{
	  rtx temp = XVECEXP (dst, 0, i);
	  if (GET_CODE (temp) == EXPR_LIST || GET_CODE (temp) == CLOBBER
	      || GET_CODE (temp) == SET)
	    df_def_record_1 (dflow, temp, bb, insn, 
			     GET_CODE (temp) == CLOBBER 
			     ? flags | DF_REF_MUST_CLOBBER : flags, 
			     record_live);
	}
      return;
    }

  /* Maybe, we should flag the use of STRICT_LOW_PART somehow.  It might
     be handy for the reg allocator.  */
  while (GET_CODE (dst) == STRICT_LOW_PART
	 || GET_CODE (dst) == ZERO_EXTRACT
	 || df_read_modify_subreg_p (dst))
    {
#if 0
      /* Strict low part always contains SUBREG, but we do not want to make
	 it appear outside, as whole register is always considered.  */
      if (GET_CODE (dst) == STRICT_LOW_PART)
	{
	  loc = &XEXP (dst, 0);
	  dst = *loc;
	}
#endif
      loc = &XEXP (dst, 0);
      if (GET_CODE (dst) == STRICT_LOW_PART)
	dst_in_strict_lowpart = true;
      dst = *loc;
      flags |= DF_REF_READ_WRITE;

    }

  /* Sets to a subreg of a single word register are partial sets if
     they are wrapped in a strict lowpart, and not partial otherwise.
  */
  if (GET_CODE (dst) == SUBREG && REG_P (SUBREG_REG (dst))
      && dst_in_strict_lowpart)
    flags |= DF_REF_PARTIAL;
    
  if (REG_P (dst)
      || (GET_CODE (dst) == SUBREG && REG_P (SUBREG_REG (dst))))
    df_ref_record (dflow, dst, loc, bb, insn, 
		   DF_REF_REG_DEF, flags, record_live);
}


/* Process all the registers defined in the pattern rtx, X.  */

static void
df_defs_record (struct dataflow *dflow, rtx x, basic_block bb, rtx insn)
{
  RTX_CODE code = GET_CODE (x);

  if (code == SET || code == CLOBBER)
    {
      /* Mark the single def within the pattern.  */
      df_def_record_1 (dflow, x, bb, insn, 
		       code == CLOBBER ? DF_REF_MUST_CLOBBER : 0, true);
    }
  else if (code == COND_EXEC)
    {
      df_defs_record  (dflow, COND_EXEC_CODE (x), bb, insn);
    }
  else if (code == PARALLEL)
    {
      int i;

      /* Mark the multiple defs within the pattern.  */
      for (i = XVECLEN (x, 0) - 1; i >= 0; i--)
	 df_defs_record (dflow, XVECEXP (x, 0, i), bb, insn);
    }
}


/* Process all the registers used in the rtx at address LOC.  */

static void
df_uses_record (struct dataflow *dflow, rtx *loc, enum df_ref_type ref_type,
		basic_block bb, rtx insn, enum df_ref_flags flags)
{
  RTX_CODE code;
  rtx x;
 retry:
  x = *loc;
  if (!x)
    return;
  code = GET_CODE (x);
  switch (code)
    {
    case LABEL_REF:
    case SYMBOL_REF:
    case CONST_INT:
    case CONST:
    case CONST_DOUBLE:
    case CONST_VECTOR:
    case PC:
    case CC0:
    case ADDR_VEC:
    case ADDR_DIFF_VEC:
      return;

    case CLOBBER:
      /* If we are clobbering a MEM, mark any registers inside the address
	 as being used.  */
      if (MEM_P (XEXP (x, 0)))
	df_uses_record (dflow, &XEXP (XEXP (x, 0), 0),
			DF_REF_REG_MEM_STORE, bb, insn, flags);

      /* If we're clobbering a REG then we have a def so ignore.  */
      return;

    case MEM:
      df_uses_record (dflow, &XEXP (x, 0), DF_REF_REG_MEM_LOAD, bb, insn,
		      flags & DF_REF_IN_NOTE);
      return;

    case SUBREG:
      /* While we're here, optimize this case.  */
      flags |= DF_REF_PARTIAL;
      /* In case the SUBREG is not of a REG, do not optimize.  */
      if (!REG_P (SUBREG_REG (x)))
	{
	  loc = &SUBREG_REG (x);
	  df_uses_record (dflow, loc, ref_type, bb, insn, flags);
	  return;
	}
      /* ... Fall through ...  */

    case REG:
      df_ref_record (dflow, x, loc, bb, insn, ref_type, flags, true);
      return;

    case SET:
      {
	rtx dst = SET_DEST (x);
	gcc_assert (!(flags & DF_REF_IN_NOTE));
	df_uses_record (dflow, &SET_SRC (x), DF_REF_REG_USE, bb, insn, flags);

	switch (GET_CODE (dst))
	  {
	    case SUBREG:
	      if (df_read_modify_subreg_p (dst))
		{
		  df_uses_record (dflow, &SUBREG_REG (dst), 
				  DF_REF_REG_USE, bb,
				  insn, flags | DF_REF_READ_WRITE);
		  break;
		}
	      /* Fall through.  */
	    case REG:
	    case PARALLEL:
	    case SCRATCH:
	    case PC:
	    case CC0:
		break;
	    case MEM:
	      df_uses_record (dflow, &XEXP (dst, 0),
			      DF_REF_REG_MEM_STORE,
			      bb, insn, flags);
	      break;
	    case STRICT_LOW_PART:
	      {
		rtx *temp = &XEXP (dst, 0);
		/* A strict_low_part uses the whole REG and not just the
		 SUBREG.  */
		dst = XEXP (dst, 0);
		df_uses_record (dflow, 
				(GET_CODE (dst) == SUBREG) 
				? &SUBREG_REG (dst) : temp, 
				DF_REF_REG_USE, bb,
				insn, DF_REF_READ_WRITE);
	      }
	      break;
	    case ZERO_EXTRACT:
	    case SIGN_EXTRACT:
	      df_uses_record (dflow, &XEXP (dst, 0), 
			      DF_REF_REG_USE, bb, insn,
			      DF_REF_READ_WRITE);
	      df_uses_record (dflow, &XEXP (dst, 1), 
			      DF_REF_REG_USE, bb, insn, flags);
	      df_uses_record (dflow, &XEXP (dst, 2), 
			      DF_REF_REG_USE, bb, insn, flags);
	      dst = XEXP (dst, 0);
	      break;
	    default:
	      gcc_unreachable ();
	  }
	return;
      }

    case RETURN:
      break;

    case ASM_OPERANDS:
    case UNSPEC_VOLATILE:
    case TRAP_IF:
    case ASM_INPUT:
      {
	/* Traditional and volatile asm instructions must be
	   considered to use and clobber all hard registers, all
	   pseudo-registers and all of memory.  So must TRAP_IF and
	   UNSPEC_VOLATILE operations.

	   Consider for instance a volatile asm that changes the fpu
	   rounding mode.  An insn should not be moved across this
	   even if it only uses pseudo-regs because it might give an
	   incorrectly rounded result.

	   However, flow.c's liveness computation did *not* do this,
	   giving the reasoning as " ?!? Unfortunately, marking all
	   hard registers as live causes massive problems for the
	   register allocator and marking all pseudos as live creates
	   mountains of uninitialized variable warnings."

	   In order to maintain the status quo with regard to liveness
	   and uses, we do what flow.c did and just mark any regs we
	   can find in ASM_OPERANDS as used.  Later on, when liveness
	   is computed, asm insns are scanned and regs_asm_clobbered
	   is filled out.  

	   For all ASM_OPERANDS, we must traverse the vector of input
	   operands.  We can not just fall through here since then we
	   would be confused by the ASM_INPUT rtx inside ASM_OPERANDS,
	   which do not indicate traditional asms unlike their normal
	   usage.  */
	if (code == ASM_OPERANDS)
	  {
	    int j;

	    for (j = 0; j < ASM_OPERANDS_INPUT_LENGTH (x); j++)
	      df_uses_record (dflow, &ASM_OPERANDS_INPUT (x, j),
			      DF_REF_REG_USE, bb, insn, flags);
	    return;
	  }
	break;
      }

    case PRE_DEC:
    case POST_DEC:
    case PRE_INC:
    case POST_INC:
    case PRE_MODIFY:
    case POST_MODIFY:
      /* Catch the def of the register being modified.  */
      flags |= DF_REF_READ_WRITE;
      df_ref_record (dflow, XEXP (x, 0), &XEXP (x, 0), bb, insn, 
		     DF_REF_REG_DEF, flags, true);

      /* ... Fall through to handle uses ...  */

    default:
      break;
    }

  /* Recursively scan the operands of this expression.  */
  {
    const char *fmt = GET_RTX_FORMAT (code);
    int i;

    for (i = GET_RTX_LENGTH (code) - 1; i >= 0; i--)
      {
	if (fmt[i] == 'e')
	  {
	    /* Tail recursive case: save a function call level.  */
	    if (i == 0)
	      {
		loc = &XEXP (x, 0);
		goto retry;
	      }
	    df_uses_record (dflow, &XEXP (x, i), ref_type, bb, insn, flags);
	  }
	else if (fmt[i] == 'E')
	  {
	    int j;
	    for (j = 0; j < XVECLEN (x, i); j++)
	      df_uses_record (dflow, &XVECEXP (x, i, j), ref_type,
			      bb, insn, flags);
	  }
      }
  }
}

/* Return true if *LOC contains an asm.  */

static int
df_insn_contains_asm_1 (rtx *loc, void *data ATTRIBUTE_UNUSED)
{
  if ( !*loc)
    return 0;
  if (GET_CODE (*loc) == ASM_OPERANDS)
    return 1;
  return 0;
}


/* Return true if INSN contains an ASM.  */

static int
df_insn_contains_asm (rtx insn)
{
  return for_each_rtx (&insn, df_insn_contains_asm_1, NULL);
}



/* Record all the refs for DF within INSN of basic block BB.  */

static void
df_insn_refs_record (struct dataflow *dflow, basic_block bb, rtx insn)
{
  struct df *df = dflow->df;
  int i;

  if (INSN_P (insn))
    {
      rtx note;

      if (df_insn_contains_asm (insn))
	DF_INSN_CONTAINS_ASM (df, insn) = true;
      
      /* Record register defs.  */
      df_defs_record (dflow, PATTERN (insn), bb, insn);

      if (dflow->flags & DF_EQUIV_NOTES)
	for (note = REG_NOTES (insn); note;
	     note = XEXP (note, 1))
	  {
	    switch (REG_NOTE_KIND (note))
	      {
	      case REG_EQUIV:
	      case REG_EQUAL:
		df_uses_record (dflow, &XEXP (note, 0), DF_REF_REG_USE,
				bb, insn, DF_REF_IN_NOTE);
	      default:
		break;
	      }
	  }

      if (CALL_P (insn))
	{
	  rtx note;

	  /* Record the registers used to pass arguments, and explicitly
	     noted as clobbered.  */
	  for (note = CALL_INSN_FUNCTION_USAGE (insn); note;
	       note = XEXP (note, 1))
	    {
	      if (GET_CODE (XEXP (note, 0)) == USE)
		df_uses_record (dflow, &XEXP (XEXP (note, 0), 0), 
				DF_REF_REG_USE,
				bb, insn, 0);
              else if (GET_CODE (XEXP (note, 0)) == CLOBBER)
		{
		  df_defs_record (dflow, XEXP (note, 0), bb, insn);
		  if (REG_P (XEXP (XEXP (note, 0), 0)))
		    {
		      rtx reg = XEXP (XEXP (note, 0), 0);
		      int regno_last;
		      int regno_first;
		      int i;
		
		      regno_last = regno_first = REGNO (reg);
		      if (regno_first < FIRST_PSEUDO_REGISTER)
			regno_last 
			  += hard_regno_nregs[regno_first][GET_MODE (reg)] - 1;
		      for (i = regno_first; i <= regno_last; i++)
			regs_ever_live[i] = 1;
		    }
		}
	    }

	  /* The stack ptr is used (honorarily) by a CALL insn.  */
	  df_uses_record (dflow, &regno_reg_rtx[STACK_POINTER_REGNUM],
			  DF_REF_REG_USE, bb, insn, 
			  0);

	  if (dflow->flags & DF_HARD_REGS)
	    {
	      bitmap_iterator bi;
	      unsigned int ui;
	      /* Calls may also reference any of the global registers,
		 so they are recorded as used.  */
	      for (i = 0; i < FIRST_PSEUDO_REGISTER; i++)
		if (global_regs[i])
		  df_uses_record (dflow, &regno_reg_rtx[i],
				  DF_REF_REG_USE, bb, insn, 
				  0);
	      EXECUTE_IF_SET_IN_BITMAP (df_invalidated_by_call, 0, ui, bi)
	        df_ref_record (dflow, regno_reg_rtx[ui], &regno_reg_rtx[ui], bb, 
			       insn, DF_REF_REG_DEF, DF_REF_MAY_CLOBBER, false);
	    }
	}

      /* Record the register uses.  */
      df_uses_record (dflow, &PATTERN (insn),
		      DF_REF_REG_USE, bb, insn, 0);

    }
}

static bool
df_has_eh_preds (basic_block bb)
{
  edge e;
  edge_iterator ei;

  FOR_EACH_EDGE (e, ei, bb->preds)
    {
      if (e->flags & EDGE_EH)
	return true;
    }
  return false;
}

/* Record all the refs within the basic block BB.  */

static void
df_bb_refs_record (struct dataflow *dflow, basic_block bb)
{
  struct df *df = dflow->df;
  rtx insn;
  int luid = 0;
  struct df_scan_bb_info *bb_info = df_scan_get_bb_info (dflow, bb->index);
  bitmap artificial_uses_at_bottom = NULL;

  if (dflow->flags & DF_HARD_REGS)
    artificial_uses_at_bottom = BITMAP_ALLOC (NULL);

  /* Need to make sure that there is a record in the basic block info. */  
  if (!bb_info)
    {
      bb_info = (struct df_scan_bb_info *) pool_alloc (dflow->block_pool);
      df_scan_set_bb_info (dflow, bb->index, bb_info);
      bb_info->artificial_defs = NULL;
      bb_info->artificial_uses = NULL;
    }

  /* Scan the block an insn at a time from beginning to end.  */
  FOR_BB_INSNS (bb, insn)
    {
      df_insn_create_insn_record (dflow, insn);
      if (INSN_P (insn))
	{
	  /* Record defs within INSN.  */
	  DF_INSN_LUID (df, insn) = luid++;
	  df_insn_refs_record (dflow, bb, insn);
	}
      DF_INSN_LUID (df, insn) = luid;
    }

#ifdef EH_RETURN_DATA_REGNO
  if ((dflow->flags & DF_HARD_REGS)
      && df_has_eh_preds (bb))
    {
      unsigned int i;
      /* Mark the registers that will contain data for the handler.  */
      for (i = 0; ; ++i)
	{
	  unsigned regno = EH_RETURN_DATA_REGNO (i);
	  if (regno == INVALID_REGNUM)
	    break;
	  df_ref_record (dflow, regno_reg_rtx[regno], &regno_reg_rtx[regno],
			 bb, NULL,
			 DF_REF_REG_DEF, DF_REF_ARTIFICIAL | DF_REF_AT_TOP,
			 false);
	}
    }
#endif


  if ((dflow->flags & DF_HARD_REGS)
      && df_has_eh_preds (bb))
    {
#ifdef EH_USES
      unsigned int i;
      /* This code is putting in a artificial ref for the use at the
	 TOP of the block that receives the exception.  It is too
	 cumbersome to actually put the ref on the edge.  We could
	 either model this at the top of the receiver block or the
	 bottom of the sender block.

         The bottom of the sender block is problematic because not all
         out-edges of the a block are eh-edges.  However, it is true
         that all edges into a block are either eh-edges or none of
         them are eh-edges.  Thus, we can model this at the top of the
         eh-receiver for all of the edges at once. */
      for (i = 0; i < FIRST_PSEUDO_REGISTER; i++)
	if (EH_USES (i))
	  df_uses_record (dflow, &regno_reg_rtx[i], 
			  DF_REF_REG_USE, bb, NULL,
			  DF_REF_ARTIFICIAL | DF_REF_AT_TOP);
#endif

      /* The following code (down thru the arg_pointer setting APPEARS
	 to be necessary because there is nothing that actually
	 describes what the exception handling code may actually need
	 to keep alive.  */
      if (reload_completed)
	{
	  if (frame_pointer_needed)
	    {
	      bitmap_set_bit (artificial_uses_at_bottom, FRAME_POINTER_REGNUM);
#if FRAME_POINTER_REGNUM != HARD_FRAME_POINTER_REGNUM
	      bitmap_set_bit (artificial_uses_at_bottom, HARD_FRAME_POINTER_REGNUM);
#endif
	    }
#if FRAME_POINTER_REGNUM != ARG_POINTER_REGNUM
	  if (fixed_regs[ARG_POINTER_REGNUM])
	    bitmap_set_bit (artificial_uses_at_bottom, ARG_POINTER_REGNUM);
#endif
	}
    }

  if ((dflow->flags & DF_HARD_REGS) 
      && bb->index >= NUM_FIXED_BLOCKS)
    {
      /* Before reload, there are a few registers that must be forced
	 live everywhere -- which might not already be the case for
	 blocks within infinite loops.  */
      if (!reload_completed)
	{
	  
	  /* Any reference to any pseudo before reload is a potential
	     reference of the frame pointer.  */
	  bitmap_set_bit (artificial_uses_at_bottom, FRAME_POINTER_REGNUM);
	  
#if FRAME_POINTER_REGNUM != ARG_POINTER_REGNUM
	  /* Pseudos with argument area equivalences may require
	     reloading via the argument pointer.  */
	  if (fixed_regs[ARG_POINTER_REGNUM])
	    bitmap_set_bit (artificial_uses_at_bottom, ARG_POINTER_REGNUM);
#endif
	  
	  /* Any constant, or pseudo with constant equivalences, may
	     require reloading from memory using the pic register.  */
	  if ((unsigned) PIC_OFFSET_TABLE_REGNUM != INVALID_REGNUM
	      && fixed_regs[PIC_OFFSET_TABLE_REGNUM])
	    bitmap_set_bit (artificial_uses_at_bottom, PIC_OFFSET_TABLE_REGNUM);
	}
      /* The all-important stack pointer must always be live.  */
      bitmap_set_bit (artificial_uses_at_bottom, STACK_POINTER_REGNUM);
    }

  if (dflow->flags & DF_HARD_REGS)
    {
      bitmap_iterator bi;
      unsigned int regno;

      EXECUTE_IF_SET_IN_BITMAP (artificial_uses_at_bottom, 0, regno, bi)
	{
	  df_uses_record (dflow, &regno_reg_rtx[regno],
			  DF_REF_REG_USE, bb, NULL, DF_REF_ARTIFICIAL);
	}

      BITMAP_FREE (artificial_uses_at_bottom);
    }
}


/* Record all the refs in the basic blocks specified by BLOCKS.  */

static void
df_refs_record (struct dataflow *dflow, bitmap blocks)
{
  unsigned int bb_index;
  bitmap_iterator bi;

  EXECUTE_IF_SET_IN_BITMAP (blocks, 0, bb_index, bi)
    {
      basic_block bb = BASIC_BLOCK (bb_index);
      df_bb_refs_record (dflow, bb);
    }

  if (bitmap_bit_p (blocks, EXIT_BLOCK))
    df_record_exit_block_uses (dflow);

  if (bitmap_bit_p (blocks, ENTRY_BLOCK))
    df_record_entry_block_defs (dflow);
}


/*----------------------------------------------------------------------------
   Specialized hard register scanning functions.
----------------------------------------------------------------------------*/

/* Mark a register in SET.  Hard registers in large modes get all
   of their component registers set as well.  */

static void
df_mark_reg (rtx reg, void *vset)
{
  bitmap set = (bitmap) vset;
  int regno = REGNO (reg);

  gcc_assert (GET_MODE (reg) != BLKmode);

  bitmap_set_bit (set, regno);
  if (regno < FIRST_PSEUDO_REGISTER)
    {
      int n = hard_regno_nregs[regno][GET_MODE (reg)];
      while (--n > 0)
	bitmap_set_bit  (set, regno + n);
    }
}


/* Record the (conservative) set of hard registers that are defined on
   entry to the function.  */

static void
df_record_entry_block_defs (struct dataflow *dflow)
{
  unsigned int i; 
  bitmap_iterator bi;
  rtx r;
  struct df *df = dflow->df;

  bitmap_clear (df->entry_block_defs);

  if (!(dflow->flags & DF_HARD_REGS))
    return;

  for (i = 0; i < FIRST_PSEUDO_REGISTER; i++)
    {
      if (FUNCTION_ARG_REGNO_P (i))
#ifdef INCOMING_REGNO
	bitmap_set_bit (df->entry_block_defs, INCOMING_REGNO (i));
#else
	bitmap_set_bit (df->entry_block_defs, i);
#endif
    }
      
  /* Once the prologue has been generated, all of these registers
     should just show up in the first regular block.  */
  if (HAVE_prologue && epilogue_completed)
    {
      /* Defs for the callee saved registers are inserted so that the
	 pushes have some defining location.  */
      for (i = 0; i < FIRST_PSEUDO_REGISTER; i++)
	if ((call_used_regs[i] == 0) && (regs_ever_live[i]))
	  bitmap_set_bit (df->entry_block_defs, i);
    }
  else
    {
      /* The always important stack pointer.  */
      bitmap_set_bit (df->entry_block_defs, STACK_POINTER_REGNUM);

#ifdef INCOMING_RETURN_ADDR_RTX
      if (REG_P (INCOMING_RETURN_ADDR_RTX))
	bitmap_set_bit (df->entry_block_defs, REGNO (INCOMING_RETURN_ADDR_RTX));
#endif
            
      /* If STATIC_CHAIN_INCOMING_REGNUM == STATIC_CHAIN_REGNUM
	 only STATIC_CHAIN_REGNUM is defined.  If they are different,
	 we only care about the STATIC_CHAIN_INCOMING_REGNUM.  */
#ifdef STATIC_CHAIN_INCOMING_REGNUM
      bitmap_set_bit (df->entry_block_defs, STATIC_CHAIN_INCOMING_REGNUM);
#else 
#ifdef STATIC_CHAIN_REGNUM
      bitmap_set_bit (df->entry_block_defs, STATIC_CHAIN_REGNUM);
#endif
#endif
      
      r = TARGET_STRUCT_VALUE_RTX (current_function_decl, true);
      if (r && REG_P (r))
	bitmap_set_bit (df->entry_block_defs, REGNO (r));
    }

  if ((!reload_completed) || frame_pointer_needed)
    {
      /* Any reference to any pseudo before reload is a potential
	 reference of the frame pointer.  */
      bitmap_set_bit (df->entry_block_defs, FRAME_POINTER_REGNUM);
#if FRAME_POINTER_REGNUM != HARD_FRAME_POINTER_REGNUM
      /* If they are different, also mark the hard frame pointer as live.  */
      if (!LOCAL_REGNO (HARD_FRAME_POINTER_REGNUM))
	bitmap_set_bit (df->entry_block_defs, HARD_FRAME_POINTER_REGNUM);
#endif
    }

  /* These registers are live everywhere.  */
  if (!reload_completed)
    {
#ifdef EH_USES
      /* The ia-64, the only machine that uses this, does not define these 
	 until after reload.  */
      for (i = 0; i < FIRST_PSEUDO_REGISTER; i++)
	if (EH_USES (i))
	  {
	    bitmap_set_bit (df->entry_block_defs, i);
	  }
#endif
      
#if FRAME_POINTER_REGNUM != ARG_POINTER_REGNUM
      /* Pseudos with argument area equivalences may require
	 reloading via the argument pointer.  */
      if (fixed_regs[ARG_POINTER_REGNUM])
	bitmap_set_bit (df->entry_block_defs, ARG_POINTER_REGNUM);
#endif
	  
#ifdef PIC_OFFSET_TABLE_REGNUM
      /* Any constant, or pseudo with constant equivalences, may
	 require reloading from memory using the pic register.  */
      if ((unsigned) PIC_OFFSET_TABLE_REGNUM != INVALID_REGNUM
	  && fixed_regs[PIC_OFFSET_TABLE_REGNUM])
	bitmap_set_bit (df->entry_block_defs, PIC_OFFSET_TABLE_REGNUM);
#endif
    }

  targetm.live_on_entry (df->entry_block_defs);

  EXECUTE_IF_SET_IN_BITMAP (df->entry_block_defs, 0, i, bi)
    {
      df_ref_record (dflow, regno_reg_rtx[i], &regno_reg_rtx[i], 
		     ENTRY_BLOCK_PTR, NULL, 
		     DF_REF_REG_DEF, DF_REF_ARTIFICIAL , false);
    }
}


/* Record the set of hard registers that are used in the exit block.  */

static void
df_record_exit_block_uses (struct dataflow *dflow)
{
  unsigned int i; 
  bitmap_iterator bi;
  struct df *df = dflow->df;

  bitmap_clear (df->exit_block_uses);
  
  if (!(dflow->flags & DF_HARD_REGS))
    return;

  /* If exiting needs the right stack value, consider the stack
     pointer live at the end of the function.  */
  if ((HAVE_epilogue && epilogue_completed)
      || !EXIT_IGNORE_STACK
      || (!FRAME_POINTER_REQUIRED
	  && !current_function_calls_alloca
	  && flag_omit_frame_pointer)
      || current_function_sp_is_unchanging)
    {
      bitmap_set_bit (df->exit_block_uses, STACK_POINTER_REGNUM);
    }
  
  /* Mark the frame pointer if needed at the end of the function.
     If we end up eliminating it, it will be removed from the live
     list of each basic block by reload.  */
  
  if ((!reload_completed) || frame_pointer_needed)
    {
      bitmap_set_bit (df->exit_block_uses, FRAME_POINTER_REGNUM);
#if FRAME_POINTER_REGNUM != HARD_FRAME_POINTER_REGNUM
      /* If they are different, also mark the hard frame pointer as live.  */
      if (!LOCAL_REGNO (HARD_FRAME_POINTER_REGNUM))
	bitmap_set_bit (df->exit_block_uses, HARD_FRAME_POINTER_REGNUM);
#endif
    }

#ifndef PIC_OFFSET_TABLE_REG_CALL_CLOBBERED
  /* Many architectures have a GP register even without flag_pic.
     Assume the pic register is not in use, or will be handled by
     other means, if it is not fixed.  */
  if ((unsigned) PIC_OFFSET_TABLE_REGNUM != INVALID_REGNUM
      && fixed_regs[PIC_OFFSET_TABLE_REGNUM])
    bitmap_set_bit (df->exit_block_uses, PIC_OFFSET_TABLE_REGNUM);
#endif
  
  /* Mark all global registers, and all registers used by the
     epilogue as being live at the end of the function since they
     may be referenced by our caller.  */
  for (i = 0; i < FIRST_PSEUDO_REGISTER; i++)
    if (global_regs[i] || EPILOGUE_USES (i))
      bitmap_set_bit (df->exit_block_uses, i);
  
  if (HAVE_epilogue && epilogue_completed)
    {
      /* Mark all call-saved registers that we actually used.  */
      for (i = 0; i < FIRST_PSEUDO_REGISTER; i++)
	if (regs_ever_live[i] && !LOCAL_REGNO (i)
	    && !TEST_HARD_REG_BIT (regs_invalidated_by_call, i))
	  bitmap_set_bit (df->exit_block_uses, i);
    }
  
#ifdef EH_RETURN_DATA_REGNO
  /* Mark the registers that will contain data for the handler.  */
  if (reload_completed && current_function_calls_eh_return)
    for (i = 0; ; ++i)
      {
	unsigned regno = EH_RETURN_DATA_REGNO (i);
	if (regno == INVALID_REGNUM)
	  break;
	bitmap_set_bit (df->exit_block_uses, regno);
      }
#endif

#ifdef EH_RETURN_STACKADJ_RTX
  if ((!HAVE_epilogue || ! epilogue_completed)
      && current_function_calls_eh_return)
    {
      rtx tmp = EH_RETURN_STACKADJ_RTX;
      if (tmp && REG_P (tmp))
	df_mark_reg (tmp, df->exit_block_uses);
    }
#endif

#ifdef EH_RETURN_HANDLER_RTX
  if ((!HAVE_epilogue || ! epilogue_completed)
      && current_function_calls_eh_return)
    {
      rtx tmp = EH_RETURN_HANDLER_RTX;
      if (tmp && REG_P (tmp))
	df_mark_reg (tmp, df->exit_block_uses);
    }
#endif 
  
  /* Mark function return value.  */
  diddle_return_value (df_mark_reg, (void*) df->exit_block_uses);

  if (dflow->flags & DF_HARD_REGS)
    EXECUTE_IF_SET_IN_BITMAP (df->exit_block_uses, 0, i, bi)
      df_uses_record (dflow, &regno_reg_rtx[i], 
  		      DF_REF_REG_USE, EXIT_BLOCK_PTR, NULL,
		      DF_REF_ARTIFICIAL);
}

static bool initialized = false;

/* Initialize some platform specific structures.  */

void 
df_hard_reg_init (void)
{
  int i;
#ifdef ELIMINABLE_REGS
  static const struct {const int from, to; } eliminables[] = ELIMINABLE_REGS;
#endif
  /* After reload, some ports add certain bits to regs_ever_live so
     this cannot be reset.  */
  
  if (!reload_completed)
    memset (regs_ever_live, 0, sizeof (regs_ever_live));

  if (initialized)
    return;

  bitmap_obstack_initialize (&persistent_obstack);

  /* Record which registers will be eliminated.  We use this in
     mark_used_regs.  */
  CLEAR_HARD_REG_SET (elim_reg_set);
  
#ifdef ELIMINABLE_REGS
  for (i = 0; i < (int) ARRAY_SIZE (eliminables); i++)
    SET_HARD_REG_BIT (elim_reg_set, eliminables[i].from);
#else
  SET_HARD_REG_BIT (elim_reg_set, FRAME_POINTER_REGNUM);
#endif
  
  df_invalidated_by_call = BITMAP_ALLOC (&persistent_obstack);
  
  /* Inconveniently, this is only readily available in hard reg set
     form.  */
  for (i = 0; i < FIRST_PSEUDO_REGISTER; ++i)
    if (TEST_HARD_REG_BIT (regs_invalidated_by_call, i))
      bitmap_set_bit (df_invalidated_by_call, i);
  
  initialized = true;
}
