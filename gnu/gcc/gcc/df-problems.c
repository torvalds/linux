/* Standard problems for dataflow support routines.
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
02110-1301, USA.  */

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
#include "df.h"
#include "vecprim.h"
#include "except.h"

#if 0
#define REG_DEAD_DEBUGGING
#endif

#define DF_SPARSE_THRESHOLD 32

static bitmap seen_in_block = NULL;
static bitmap seen_in_insn = NULL;
static void df_ri_dump (struct dataflow *, FILE *);


/*----------------------------------------------------------------------------
   Public functions access functions for the dataflow problems.
----------------------------------------------------------------------------*/

/* Create a du or ud chain from SRC to DST and link it into SRC.   */

struct df_link *
df_chain_create (struct dataflow *dflow, struct df_ref *src, struct df_ref *dst)
{
  struct df_link *head = DF_REF_CHAIN (src);
  struct df_link *link = pool_alloc (dflow->block_pool);;
  
  DF_REF_CHAIN (src) = link;
  link->next = head;
  link->ref = dst;
  return link;
}


/* Delete a du or ud chain for REF.  If LINK is NULL, delete all
   chains for ref and check to see if the reverse chains can also be
   deleted.  If LINK is not NULL it must be a link off of ref.  In
   this case, the other end is not deleted.  */

void
df_chain_unlink (struct dataflow *dflow, struct df_ref *ref, struct df_link *link)
{
  struct df_link *chain = DF_REF_CHAIN (ref);
  if (link)
    {
      /* Link was the first element in the chain.  */
      if (chain == link)
	DF_REF_CHAIN (ref) = link->next;
      else
	{
	  /* Link is an internal element in the chain.  */
	  struct df_link *prev = chain;
	  while (chain)
	    {
	      if (chain == link)
		{
		  prev->next = chain->next;
		  break;
		}
	      prev = chain;
	      chain = chain->next;
	    }
	}
      pool_free (dflow->block_pool, link);
    }
  else
    {
      /* If chain is NULL here, it was because of a recursive call
	 when the other flavor of chains was not built.  Just run thru
	 the entire chain calling the other side and then deleting the
	 link.  */
      while (chain)
	{
	  struct df_link *next = chain->next;
	  /* Delete the other side if it exists.  */
	  df_chain_unlink (dflow, chain->ref, chain);
	  chain = next;
	}
    }
}


/* Copy the du or ud chain starting at FROM_REF and attach it to
   TO_REF.  */ 

void 
df_chain_copy (struct dataflow *dflow, 
	       struct df_ref *to_ref, 
	       struct df_link *from_ref)
{
  while (from_ref)
    {
      df_chain_create (dflow, to_ref, from_ref->ref);
      from_ref = from_ref->next;
    }
}


/* Get the live in set for BB no matter what problem happens to be
   defined.  */

bitmap
df_get_live_in (struct df *df, basic_block bb)
{
  gcc_assert (df->problems_by_index[DF_LR]);

  if (df->problems_by_index[DF_UREC])
    return DF_RA_LIVE_IN (df, bb);
  else if (df->problems_by_index[DF_UR])
    return DF_LIVE_IN (df, bb);
  else 
    return DF_UPWARD_LIVE_IN (df, bb);
}


/* Get the live out set for BB no matter what problem happens to be
   defined.  */

bitmap
df_get_live_out (struct df *df, basic_block bb)
{
  gcc_assert (df->problems_by_index[DF_LR]);

  if (df->problems_by_index[DF_UREC])
    return DF_RA_LIVE_OUT (df, bb);
  else if (df->problems_by_index[DF_UR])
    return DF_LIVE_OUT (df, bb);
  else 
    return DF_UPWARD_LIVE_OUT (df, bb);
}


/*----------------------------------------------------------------------------
   Utility functions.
----------------------------------------------------------------------------*/

/* Generic versions to get the void* version of the block info.  Only
   used inside the problem instance vectors.  */

/* Grow the bb_info array.  */

void
df_grow_bb_info (struct dataflow *dflow)
{
  unsigned int new_size = last_basic_block + 1;
  if (dflow->block_info_size < new_size)
    {
      new_size += new_size / 4;
      dflow->block_info = xrealloc (dflow->block_info, 
				    new_size *sizeof (void*));
      memset (dflow->block_info + dflow->block_info_size, 0,
	      (new_size - dflow->block_info_size) *sizeof (void *));
      dflow->block_info_size = new_size;
    }
}

/* Dump a def-use or use-def chain for REF to FILE.  */

void
df_chain_dump (struct df_link *link, FILE *file)
{
  fprintf (file, "{ ");
  for (; link; link = link->next)
    {
      fprintf (file, "%c%d(bb %d insn %d) ",
	       DF_REF_REG_DEF_P (link->ref) ? 'd' : 'u',
	       DF_REF_ID (link->ref),
	       DF_REF_BBNO (link->ref),
	       DF_REF_INSN (link->ref) ? DF_REF_INSN_UID (link->ref) : -1);
    }
  fprintf (file, "}");
}


/* Print some basic block info as part of df_dump.  */

void 
df_print_bb_index (basic_block bb, FILE *file)
{
  edge e;
  edge_iterator ei;

  fprintf (file, "( ");
    FOR_EACH_EDGE (e, ei, bb->preds)
    {
      basic_block pred = e->src;
      fprintf (file, "%d ", pred->index);
    } 
  fprintf (file, ")->[%d]->( ", bb->index);
  FOR_EACH_EDGE (e, ei, bb->succs)
    {
      basic_block succ = e->dest;
      fprintf (file, "%d ", succ->index);
    } 
  fprintf (file, ")\n");
}


/* Return a bitmap for REGNO from the cache MAPS.  The bitmap is to
   contain COUNT bits starting at START.  These bitmaps are not to be
   changed since there is a cache of them.  */

static inline bitmap
df_ref_bitmap (bitmap *maps, unsigned int regno, int start, int count)
{
  bitmap ids = maps[regno];
  if (!ids)
    {
      unsigned int i;
      unsigned int end = start + count;;
      ids = BITMAP_ALLOC (NULL);
      maps[regno] = ids;
      for (i = start; i < end; i++)
	bitmap_set_bit (ids, i);
    }
  return ids;
}


/* Make sure that the seen_in_insn and seen_in_block sbitmaps are set
   up correctly. */

static void
df_set_seen (void)
{
  seen_in_block = BITMAP_ALLOC (NULL);
  seen_in_insn = BITMAP_ALLOC (NULL);
}


static void
df_unset_seen (void)
{
  BITMAP_FREE (seen_in_block);
  BITMAP_FREE (seen_in_insn);
}



/*----------------------------------------------------------------------------
   REACHING USES

   Find the locations in the function where each use site for a pseudo
   can reach backwards.  In and out bitvectors are built for each basic
   block.  The id field in the ref is used to index into these sets.
   See df.h for details.

----------------------------------------------------------------------------*/

/* This problem plays a large number of games for the sake of
   efficiency.  
   
   1) The order of the bits in the bitvectors.  After the scanning
   phase, all of the uses are sorted.  All of the uses for the reg 0
   are first, followed by all uses for reg 1 and so on.
   
   2) There are two kill sets, one if the number of uses is less or
   equal to DF_SPARSE_THRESHOLD and another if it is greater.

   <= : There is a bitmap for each register, uses_sites[N], that is
   built on demand.  This bitvector contains a 1 for each use or reg
   N.

   > : One level of indirection is used to keep from generating long
   strings of 1 bits in the kill sets.  Bitvectors that are indexed
   by the regnum are used to represent that there is a killing def
   for the register.  The confluence and transfer functions use
   these along with the bitmap_clear_range call to remove ranges of
   bits without actually generating a knockout vector.

   The kill and sparse_kill and the dense_invalidated_by_call and
   sparse_invalidated_by call both play this game.  */

/* Private data used to compute the solution for this problem.  These
   data structures are not accessible outside of this module.  */
struct df_ru_problem_data
{

  bitmap *use_sites;            /* Bitmap of uses for each pseudo.  */
  unsigned int use_sites_size;  /* Size of use_sites.  */
  /* The set of defs to regs invalidated by call.  */
  bitmap sparse_invalidated_by_call;  
  /* The set of defs to regs invalidated by call for ru.  */  
  bitmap dense_invalidated_by_call;   
};

/* Get basic block info.  */

struct df_ru_bb_info *
df_ru_get_bb_info (struct dataflow *dflow, unsigned int index)
{
  return (struct df_ru_bb_info *) dflow->block_info[index];
}


/* Set basic block info.  */

static void
df_ru_set_bb_info (struct dataflow *dflow, unsigned int index, 
		   struct df_ru_bb_info *bb_info)
{
  dflow->block_info[index] = bb_info;
}


/* Free basic block info.  */

static void
df_ru_free_bb_info (struct dataflow *dflow, 
		    basic_block bb ATTRIBUTE_UNUSED, 
		    void *vbb_info)
{
  struct df_ru_bb_info *bb_info = (struct df_ru_bb_info *) vbb_info;
  if (bb_info)
    {
      BITMAP_FREE (bb_info->kill);
      BITMAP_FREE (bb_info->sparse_kill);
      BITMAP_FREE (bb_info->gen);
      BITMAP_FREE (bb_info->in);
      BITMAP_FREE (bb_info->out);
      pool_free (dflow->block_pool, bb_info);
    }
}


/* Allocate or reset bitmaps for DFLOW blocks. The solution bits are
   not touched unless the block is new.  */

static void 
df_ru_alloc (struct dataflow *dflow, 
	     bitmap blocks_to_rescan ATTRIBUTE_UNUSED,
	     bitmap all_blocks)
{
  unsigned int bb_index;
  bitmap_iterator bi;
  unsigned int reg_size = max_reg_num ();

  if (!dflow->block_pool)
    dflow->block_pool = create_alloc_pool ("df_ru_block pool", 
					   sizeof (struct df_ru_bb_info), 50);

  if (dflow->problem_data)
    {
      unsigned int i;
      struct df_ru_problem_data *problem_data
	= (struct df_ru_problem_data *) dflow->problem_data;

      for (i = 0; i < problem_data->use_sites_size; i++)
	{
	  bitmap bm = problem_data->use_sites[i];
	  if (bm)
	    {
	      BITMAP_FREE (bm);
	      problem_data->use_sites[i] = NULL;
	    }
	}
      
      if (problem_data->use_sites_size < reg_size)
	{
	  problem_data->use_sites 
	    = xrealloc (problem_data->use_sites, reg_size * sizeof (bitmap));
	  memset (problem_data->use_sites + problem_data->use_sites_size, 0,
		  (reg_size - problem_data->use_sites_size) * sizeof (bitmap));
	  problem_data->use_sites_size = reg_size;
	}

      bitmap_clear (problem_data->sparse_invalidated_by_call);
      bitmap_clear (problem_data->dense_invalidated_by_call);
    }
  else 
    {
      struct df_ru_problem_data *problem_data = XNEW (struct df_ru_problem_data);
      dflow->problem_data = problem_data;

      problem_data->use_sites = XCNEWVEC (bitmap, reg_size);
      problem_data->use_sites_size = reg_size;
      problem_data->sparse_invalidated_by_call = BITMAP_ALLOC (NULL);
      problem_data->dense_invalidated_by_call = BITMAP_ALLOC (NULL);
    }

  df_grow_bb_info (dflow);

  /* Because of the clustering of all def sites for the same pseudo,
     we have to process all of the blocks before doing the
     analysis.  */

  EXECUTE_IF_SET_IN_BITMAP (all_blocks, 0, bb_index, bi)
    {
      struct df_ru_bb_info *bb_info = df_ru_get_bb_info (dflow, bb_index);
      if (bb_info)
	{ 
	  bitmap_clear (bb_info->kill);
	  bitmap_clear (bb_info->sparse_kill);
	  bitmap_clear (bb_info->gen);
	}
      else
	{ 
	  bb_info = (struct df_ru_bb_info *) pool_alloc (dflow->block_pool);
	  df_ru_set_bb_info (dflow, bb_index, bb_info);
	  bb_info->kill = BITMAP_ALLOC (NULL);
	  bb_info->sparse_kill = BITMAP_ALLOC (NULL);
	  bb_info->gen = BITMAP_ALLOC (NULL);
	  bb_info->in = BITMAP_ALLOC (NULL);
	  bb_info->out = BITMAP_ALLOC (NULL);
	}
    }
}


/* Process a list of DEFs for df_ru_bb_local_compute.  */

static void
df_ru_bb_local_compute_process_def (struct dataflow *dflow,
				    struct df_ru_bb_info *bb_info, 
				    struct df_ref *def,
				    enum df_ref_flags top_flag)
{
  struct df *df = dflow->df;
  while (def)
    {
      if ((top_flag == (DF_REF_FLAGS (def) & DF_REF_AT_TOP))
	  /* If the def is to only part of the reg, it is as if it did
	     not happen, since some of the bits may get thru.  */
	  && (!(DF_REF_FLAGS (def) & DF_REF_PARTIAL)))
	{
	  unsigned int regno = DF_REF_REGNO (def);
	  unsigned int begin = DF_REG_USE_GET (df, regno)->begin;
	  unsigned int n_uses = DF_REG_USE_GET (df, regno)->n_refs;
	  if (!bitmap_bit_p (seen_in_block, regno))
	    {
	      /* The first def for regno in the insn, causes the kill
		 info to be generated.  Do not modify the gen set
		 because the only values in it are the uses from here
		 to the top of the block and this def does not effect
		 them.  */
	      if (!bitmap_bit_p (seen_in_insn, regno))
		{
		  if (n_uses > DF_SPARSE_THRESHOLD)
		    bitmap_set_bit (bb_info->sparse_kill, regno);
		  else
		    {
		      struct df_ru_problem_data * problem_data
			= (struct df_ru_problem_data *)dflow->problem_data;
		      bitmap uses 
			= df_ref_bitmap (problem_data->use_sites, regno, 
				       begin, n_uses);
		      bitmap_ior_into (bb_info->kill, uses);
		    }
		}
	      bitmap_set_bit (seen_in_insn, regno);
	    }
	}
      def = def->next_ref;
    }
}


/* Process a list of USEs for df_ru_bb_local_compute.  */

static void
df_ru_bb_local_compute_process_use (struct df_ru_bb_info *bb_info, 
				    struct df_ref *use,
				    enum df_ref_flags top_flag)
{
  while (use)
    {
      if (top_flag == (DF_REF_FLAGS (use) & DF_REF_AT_TOP))
	{
	  /* Add use to set of gens in this BB unless we have seen a
	     def in a previous instruction.  */
	  unsigned int regno = DF_REF_REGNO (use);
	  if (!bitmap_bit_p (seen_in_block, regno))
	    bitmap_set_bit (bb_info->gen, DF_REF_ID (use));
	}
      use = use->next_ref;
    }
}

/* Compute local reaching use (upward exposed use) info for basic
   block BB.  USE_INFO->REGS[R] caches the set of uses for register R.  */
static void
df_ru_bb_local_compute (struct dataflow *dflow, unsigned int bb_index)
{
  struct df *df = dflow->df;
  basic_block bb = BASIC_BLOCK (bb_index);
  struct df_ru_bb_info *bb_info = df_ru_get_bb_info (dflow, bb_index);
  rtx insn;

  /* Set when a def for regno is seen.  */
  bitmap_clear (seen_in_block);
  bitmap_clear (seen_in_insn);

#ifdef EH_USES
  /* Variables defined in the prolog that are used by the exception
     handler.  */
  df_ru_bb_local_compute_process_use (bb_info, 
				      df_get_artificial_uses (df, bb_index),
				      DF_REF_AT_TOP);
#endif
  df_ru_bb_local_compute_process_def (dflow, bb_info, 
				      df_get_artificial_defs (df, bb_index),
				      DF_REF_AT_TOP);

  FOR_BB_INSNS (bb, insn)
    {
      unsigned int uid = INSN_UID (insn);
      if (!INSN_P (insn))
	continue;

      df_ru_bb_local_compute_process_use (bb_info, 
					  DF_INSN_UID_USES (df, uid), 0);

      df_ru_bb_local_compute_process_def (dflow, bb_info, 
					  DF_INSN_UID_DEFS (df, uid), 0);

      bitmap_ior_into (seen_in_block, seen_in_insn);
      bitmap_clear (seen_in_insn);
    }

  /* Process the hardware registers that are always live.  */
  df_ru_bb_local_compute_process_use (bb_info, 
				      df_get_artificial_uses (df, bb_index), 0);

  df_ru_bb_local_compute_process_def (dflow, bb_info, 
				      df_get_artificial_defs (df, bb_index), 0);
}


/* Compute local reaching use (upward exposed use) info for each basic
   block within BLOCKS.  */
static void
df_ru_local_compute (struct dataflow *dflow, 
		     bitmap all_blocks,
		     bitmap rescan_blocks  ATTRIBUTE_UNUSED)
{
  struct df *df = dflow->df;
  unsigned int bb_index;
  bitmap_iterator bi;
  unsigned int regno;
  struct df_ru_problem_data *problem_data
    = (struct df_ru_problem_data *) dflow->problem_data;
  bitmap sparse_invalidated = problem_data->sparse_invalidated_by_call;
  bitmap dense_invalidated = problem_data->dense_invalidated_by_call;

  df_set_seen ();

  if (!df->use_info.refs_organized)
    df_reorganize_refs (&df->use_info);

  EXECUTE_IF_SET_IN_BITMAP (all_blocks, 0, bb_index, bi)
    {
      df_ru_bb_local_compute (dflow, bb_index);
    }
  
  /* Set up the knockout bit vectors to be applied across EH_EDGES.  */
  EXECUTE_IF_SET_IN_BITMAP (df_invalidated_by_call, 0, regno, bi)
    {
      struct df_reg_info *reg_info = DF_REG_USE_GET (df, regno);
      if (reg_info->n_refs > DF_SPARSE_THRESHOLD)
	bitmap_set_bit (sparse_invalidated, regno);
      else
	{
	  bitmap defs = df_ref_bitmap (problem_data->use_sites, regno, 
				       reg_info->begin, reg_info->n_refs);
	  bitmap_ior_into (dense_invalidated, defs);
	}
    }

  df_unset_seen ();
}


/* Initialize the solution bit vectors for problem.  */

static void 
df_ru_init_solution (struct dataflow *dflow, bitmap all_blocks)
{
  unsigned int bb_index;
  bitmap_iterator bi;

  EXECUTE_IF_SET_IN_BITMAP (all_blocks, 0, bb_index, bi)
    {
      struct df_ru_bb_info *bb_info = df_ru_get_bb_info (dflow, bb_index);
      bitmap_copy (bb_info->in, bb_info->gen);
      bitmap_clear (bb_info->out);
    }
}


/* Out of target gets or of in of source.  */

static void
df_ru_confluence_n (struct dataflow *dflow, edge e)
{
  bitmap op1 = df_ru_get_bb_info (dflow, e->src->index)->out;
  bitmap op2 = df_ru_get_bb_info (dflow, e->dest->index)->in;

  if (e->flags & EDGE_EH)
    {
      struct df_ru_problem_data *problem_data
	= (struct df_ru_problem_data *) dflow->problem_data;
      bitmap sparse_invalidated = problem_data->sparse_invalidated_by_call;
      bitmap dense_invalidated = problem_data->dense_invalidated_by_call;
      struct df *df = dflow->df;
      bitmap_iterator bi;
      unsigned int regno;
      bitmap tmp = BITMAP_ALLOC (NULL);

      bitmap_copy (tmp, op2);
      bitmap_and_compl_into (tmp, dense_invalidated);

      EXECUTE_IF_SET_IN_BITMAP (sparse_invalidated, 0, regno, bi)
	{
 	  bitmap_clear_range (tmp, 
 			      DF_REG_USE_GET (df, regno)->begin, 
 			      DF_REG_USE_GET (df, regno)->n_refs);
	}
      bitmap_ior_into (op1, tmp);
      BITMAP_FREE (tmp);
    }
  else
    bitmap_ior_into (op1, op2);
}


/* Transfer function.  */

static bool
df_ru_transfer_function (struct dataflow *dflow, int bb_index)
{
  struct df_ru_bb_info *bb_info = df_ru_get_bb_info (dflow, bb_index);
  unsigned int regno;
  bitmap_iterator bi;
  bitmap in = bb_info->in;
  bitmap out = bb_info->out;
  bitmap gen = bb_info->gen;
  bitmap kill = bb_info->kill;
  bitmap sparse_kill = bb_info->sparse_kill;

  if (bitmap_empty_p (sparse_kill))
    return  bitmap_ior_and_compl (in, gen, out, kill);
  else 
    {
      struct df *df = dflow->df;
      bool changed = false;
      bitmap tmp = BITMAP_ALLOC (NULL);
      bitmap_copy (tmp, out);
      EXECUTE_IF_SET_IN_BITMAP (sparse_kill, 0, regno, bi)
	{
	  bitmap_clear_range (tmp, 
 			      DF_REG_USE_GET (df, regno)->begin, 
 			      DF_REG_USE_GET (df, regno)->n_refs);
	}
      bitmap_and_compl_into (tmp, kill);
      bitmap_ior_into (tmp, gen);
      changed = !bitmap_equal_p (tmp, in);
      if (changed)
	{
	  BITMAP_FREE (in);
	  bb_info->in = tmp;
	}
      else 
	BITMAP_FREE (tmp);
      return changed;
    }
}


/* Free all storage associated with the problem.  */

static void
df_ru_free (struct dataflow *dflow)
{
  unsigned int i;
  struct df_ru_problem_data *problem_data
    = (struct df_ru_problem_data *) dflow->problem_data;

  if (problem_data)
    {
      for (i = 0; i < dflow->block_info_size; i++)
	{
	  struct df_ru_bb_info *bb_info = df_ru_get_bb_info (dflow, i);
	  if (bb_info)
	    {
	      BITMAP_FREE (bb_info->kill);
	      BITMAP_FREE (bb_info->sparse_kill);
	      BITMAP_FREE (bb_info->gen);
	      BITMAP_FREE (bb_info->in);
	      BITMAP_FREE (bb_info->out);
	    }
	}
      
      free_alloc_pool (dflow->block_pool);
      
      for (i = 0; i < problem_data->use_sites_size; i++)
	{
	  bitmap bm = problem_data->use_sites[i];
	  if (bm)
	    BITMAP_FREE (bm);
	}
      
      free (problem_data->use_sites);
      BITMAP_FREE (problem_data->sparse_invalidated_by_call);
      BITMAP_FREE (problem_data->dense_invalidated_by_call);
      
      dflow->block_info_size = 0;
      free (dflow->block_info);
      free (dflow->problem_data);
    }
  free (dflow);
}


/* Debugging info.  */

static void
df_ru_dump (struct dataflow *dflow, FILE *file)
{
  basic_block bb;
  struct df *df = dflow->df;
  struct df_ru_problem_data *problem_data
    = (struct df_ru_problem_data *) dflow->problem_data;
  unsigned int m = max_reg_num ();
  unsigned int regno;
  
  if (!dflow->block_info) 
    return;

  fprintf (file, "Reaching uses:\n");

  fprintf (file, "  sparse invalidated \t");
  dump_bitmap (file, problem_data->sparse_invalidated_by_call);
  fprintf (file, "  dense invalidated \t");
  dump_bitmap (file, problem_data->dense_invalidated_by_call);
  
  for (regno = 0; regno < m; regno++)
    if (DF_REG_USE_GET (df, regno)->n_refs)
      fprintf (file, "%d[%d,%d] ", regno, 
	       DF_REG_USE_GET (df, regno)->begin, 
	       DF_REG_USE_GET (df, regno)->n_refs);
  fprintf (file, "\n");

  FOR_ALL_BB (bb)
    {
      struct df_ru_bb_info *bb_info = df_ru_get_bb_info (dflow, bb->index);
      df_print_bb_index (bb, file);
      
      if (!bb_info->in)
	continue;
      
      fprintf (file, "  in  \t(%d)\n", (int) bitmap_count_bits (bb_info->in));
      dump_bitmap (file, bb_info->in);
      fprintf (file, "  gen \t(%d)\n", (int) bitmap_count_bits (bb_info->gen));
      dump_bitmap (file, bb_info->gen);
      fprintf (file, "  kill\t(%d)\n", (int) bitmap_count_bits (bb_info->kill));
      dump_bitmap (file, bb_info->kill);
      fprintf (file, "  out \t(%d)\n", (int) bitmap_count_bits (bb_info->out));
      dump_bitmap (file, bb_info->out);
    }
}

/* All of the information associated with every instance of the problem.  */

static struct df_problem problem_RU =
{
  DF_RU,                      /* Problem id.  */
  DF_BACKWARD,                /* Direction.  */
  df_ru_alloc,                /* Allocate the problem specific data.  */
  NULL,                       /* Reset global information.  */
  df_ru_free_bb_info,         /* Free basic block info.  */
  df_ru_local_compute,        /* Local compute function.  */
  df_ru_init_solution,        /* Init the solution specific data.  */
  df_iterative_dataflow,      /* Iterative solver.  */
  NULL,                       /* Confluence operator 0.  */ 
  df_ru_confluence_n,         /* Confluence operator n.  */ 
  df_ru_transfer_function,    /* Transfer function.  */
  NULL,                       /* Finalize function.  */
  df_ru_free,                 /* Free all of the problem information.  */
  df_ru_dump,                 /* Debugging.  */
  NULL,                       /* Dependent problem.  */
  0                           /* Changeable flags.  */
};



/* Create a new DATAFLOW instance and add it to an existing instance
   of DF.  The returned structure is what is used to get at the
   solution.  */

struct dataflow *
df_ru_add_problem (struct df *df, int flags)
{
  return df_add_problem (df, &problem_RU, flags);
}


/*----------------------------------------------------------------------------
   REACHING DEFINITIONS

   Find the locations in the function where each definition site for a
   pseudo reaches.  In and out bitvectors are built for each basic
   block.  The id field in the ref is used to index into these sets.
   See df.h for details.
   ----------------------------------------------------------------------------*/

/* See the comment at the top of the Reaching Uses problem for how the
   uses are represented in the kill sets. The same games are played
   here for the defs.  */

/* Private data used to compute the solution for this problem.  These
   data structures are not accessible outside of this module.  */
struct df_rd_problem_data
{
  /* If the number of defs for regnum N is less than
     DF_SPARSE_THRESHOLD, uses_sites[N] contains a mask of the all of
     the defs of reg N indexed by the id in the ref structure.  If
     there are more than DF_SPARSE_THRESHOLD defs for regnum N a
     different mechanism is used to mask the def.  */
  bitmap *def_sites;            /* Bitmap of defs for each pseudo.  */
  unsigned int def_sites_size;  /* Size of def_sites.  */
  /* The set of defs to regs invalidated by call.  */
  bitmap sparse_invalidated_by_call;  
  /* The set of defs to regs invalidate by call for rd.  */  
  bitmap dense_invalidated_by_call;   
};

/* Get basic block info.  */

struct df_rd_bb_info *
df_rd_get_bb_info (struct dataflow *dflow, unsigned int index)
{
  return (struct df_rd_bb_info *) dflow->block_info[index];
}


/* Set basic block info.  */

static void
df_rd_set_bb_info (struct dataflow *dflow, unsigned int index, 
		   struct df_rd_bb_info *bb_info)
{
  dflow->block_info[index] = bb_info;
}


/* Free basic block info.  */

static void
df_rd_free_bb_info (struct dataflow *dflow, 
		    basic_block bb ATTRIBUTE_UNUSED, 
		    void *vbb_info)
{
  struct df_rd_bb_info *bb_info = (struct df_rd_bb_info *) vbb_info;
  if (bb_info)
    {
      BITMAP_FREE (bb_info->kill);
      BITMAP_FREE (bb_info->sparse_kill);
      BITMAP_FREE (bb_info->gen);
      BITMAP_FREE (bb_info->in);
      BITMAP_FREE (bb_info->out);
      pool_free (dflow->block_pool, bb_info);
    }
}


/* Allocate or reset bitmaps for DFLOW blocks. The solution bits are
   not touched unless the block is new.  */

static void 
df_rd_alloc (struct dataflow *dflow, 
	     bitmap blocks_to_rescan ATTRIBUTE_UNUSED,
	     bitmap all_blocks)
{
  unsigned int bb_index;
  bitmap_iterator bi;
  unsigned int reg_size = max_reg_num ();

  if (!dflow->block_pool)
    dflow->block_pool = create_alloc_pool ("df_rd_block pool", 
					   sizeof (struct df_rd_bb_info), 50);

  if (dflow->problem_data)
    {
      unsigned int i;
      struct df_rd_problem_data *problem_data
	= (struct df_rd_problem_data *) dflow->problem_data;

      for (i = 0; i < problem_data->def_sites_size; i++)
	{
	  bitmap bm = problem_data->def_sites[i];
	  if (bm)
	    {
	      BITMAP_FREE (bm);
	      problem_data->def_sites[i] = NULL;
	    }
	}
      
      if (problem_data->def_sites_size < reg_size)
	{
	  problem_data->def_sites 
	    = xrealloc (problem_data->def_sites, reg_size *sizeof (bitmap));
	  memset (problem_data->def_sites + problem_data->def_sites_size, 0,
		  (reg_size - problem_data->def_sites_size) *sizeof (bitmap));
	  problem_data->def_sites_size = reg_size;
	}

      bitmap_clear (problem_data->sparse_invalidated_by_call);
      bitmap_clear (problem_data->dense_invalidated_by_call);
    }
  else 
    {
      struct df_rd_problem_data *problem_data = XNEW (struct df_rd_problem_data);
      dflow->problem_data = problem_data;

      problem_data->def_sites = XCNEWVEC (bitmap, reg_size);
      problem_data->def_sites_size = reg_size;
      problem_data->sparse_invalidated_by_call = BITMAP_ALLOC (NULL);
      problem_data->dense_invalidated_by_call = BITMAP_ALLOC (NULL);
    }

  df_grow_bb_info (dflow);

  /* Because of the clustering of all use sites for the same pseudo,
     we have to process all of the blocks before doing the
     analysis.  */

  EXECUTE_IF_SET_IN_BITMAP (all_blocks, 0, bb_index, bi)
    {
      struct df_rd_bb_info *bb_info = df_rd_get_bb_info (dflow, bb_index);
      if (bb_info)
	{ 
	  bitmap_clear (bb_info->kill);
	  bitmap_clear (bb_info->sparse_kill);
	  bitmap_clear (bb_info->gen);
	}
      else
	{ 
	  bb_info = (struct df_rd_bb_info *) pool_alloc (dflow->block_pool);
	  df_rd_set_bb_info (dflow, bb_index, bb_info);
	  bb_info->kill = BITMAP_ALLOC (NULL);
	  bb_info->sparse_kill = BITMAP_ALLOC (NULL);
	  bb_info->gen = BITMAP_ALLOC (NULL);
	  bb_info->in = BITMAP_ALLOC (NULL);
	  bb_info->out = BITMAP_ALLOC (NULL);
	}
    }
}


/* Process a list of DEFs for df_rd_bb_local_compute.  */

static void
df_rd_bb_local_compute_process_def (struct dataflow *dflow,
				    struct df_rd_bb_info *bb_info, 
				    struct df_ref *def,
				    enum df_ref_flags top_flag)
{
  struct df *df = dflow->df;
  while (def)
    {
      if (top_flag == (DF_REF_FLAGS (def) & DF_REF_AT_TOP))
	{
	  unsigned int regno = DF_REF_REGNO (def);
	  unsigned int begin = DF_REG_DEF_GET (df, regno)->begin;
	  unsigned int n_defs = DF_REG_DEF_GET (df, regno)->n_refs;
	  
	  /* Only the last def(s) for a regno in the block has any
	     effect.  */ 
	  if (!bitmap_bit_p (seen_in_block, regno))
	    {
	      /* The first def for regno in insn gets to knock out the
		 defs from other instructions.  */
	      if ((!bitmap_bit_p (seen_in_insn, regno))
		  /* If the def is to only part of the reg, it does
		     not kill the other defs that reach here.  */
		  && (!((DF_REF_FLAGS (def) & DF_REF_PARTIAL)
			 || (DF_REF_FLAGS (def) & DF_REF_MAY_CLOBBER))))
		{
		  if (n_defs > DF_SPARSE_THRESHOLD)
		    {
		      bitmap_set_bit (bb_info->sparse_kill, regno);
		      bitmap_clear_range(bb_info->gen, begin, n_defs);
		    }
		  else
		    {
		      struct df_rd_problem_data * problem_data
			= (struct df_rd_problem_data *)dflow->problem_data;
		      bitmap defs = df_ref_bitmap (problem_data->def_sites, 
						   regno, begin, n_defs);
		      bitmap_ior_into (bb_info->kill, defs);
		      bitmap_and_compl_into (bb_info->gen, defs);
		    }
		}
	      
	      bitmap_set_bit (seen_in_insn, regno);
	      /* All defs for regno in the instruction may be put into
		 the gen set.  */
	      if (!(DF_REF_FLAGS (def) 
		     & (DF_REF_MUST_CLOBBER | DF_REF_MAY_CLOBBER)))
		bitmap_set_bit (bb_info->gen, DF_REF_ID (def));
	    }
	}
      def = def->next_ref;
    }
}

/* Compute local reaching def info for basic block BB.  */

static void
df_rd_bb_local_compute (struct dataflow *dflow, unsigned int bb_index)
{
  struct df *df = dflow->df;
  basic_block bb = BASIC_BLOCK (bb_index);
  struct df_rd_bb_info *bb_info = df_rd_get_bb_info (dflow, bb_index);
  rtx insn;

  bitmap_clear (seen_in_block);
  bitmap_clear (seen_in_insn);

  df_rd_bb_local_compute_process_def (dflow, bb_info, 
				      df_get_artificial_defs (df, bb_index), 0);

  FOR_BB_INSNS_REVERSE (bb, insn)
    {
      unsigned int uid = INSN_UID (insn);

      if (!INSN_P (insn))
	continue;

      df_rd_bb_local_compute_process_def (dflow, bb_info, 
					  DF_INSN_UID_DEFS (df, uid), 0);

      /* This complex dance with the two bitmaps is required because
	 instructions can assign twice to the same pseudo.  This
	 generally happens with calls that will have one def for the
	 result and another def for the clobber.  If only one vector
	 is used and the clobber goes first, the result will be
	 lost.  */
      bitmap_ior_into (seen_in_block, seen_in_insn);
      bitmap_clear (seen_in_insn);
    }

  /* Process the artificial defs at the top of the block last since we
     are going backwards through the block and these are logically at
     the start.  */
  df_rd_bb_local_compute_process_def (dflow, bb_info, 
				      df_get_artificial_defs (df, bb_index),
				      DF_REF_AT_TOP);
}


/* Compute local reaching def info for each basic block within BLOCKS.  */

static void
df_rd_local_compute (struct dataflow *dflow, 
		     bitmap all_blocks,
		     bitmap rescan_blocks  ATTRIBUTE_UNUSED)
{
  struct df *df = dflow->df;
  unsigned int bb_index;
  bitmap_iterator bi;
  unsigned int regno;
  struct df_rd_problem_data *problem_data
    = (struct df_rd_problem_data *) dflow->problem_data;
  bitmap sparse_invalidated = problem_data->sparse_invalidated_by_call;
  bitmap dense_invalidated = problem_data->dense_invalidated_by_call;

  df_set_seen ();

  if (!df->def_info.refs_organized)
    df_reorganize_refs (&df->def_info);

  EXECUTE_IF_SET_IN_BITMAP (all_blocks, 0, bb_index, bi)
    {
      df_rd_bb_local_compute (dflow, bb_index);
    }
  
  /* Set up the knockout bit vectors to be applied across EH_EDGES.  */
  EXECUTE_IF_SET_IN_BITMAP (df_invalidated_by_call, 0, regno, bi)
    {
      struct df_reg_info *reg_info = DF_REG_DEF_GET (df, regno);
      if (reg_info->n_refs > DF_SPARSE_THRESHOLD)
	{
	  bitmap_set_bit (sparse_invalidated, regno);
	}
      else
	{
	  bitmap defs = df_ref_bitmap (problem_data->def_sites, regno, 
				       reg_info->begin, reg_info->n_refs);
	  bitmap_ior_into (dense_invalidated, defs);
	}
    }
  df_unset_seen ();
}


/* Initialize the solution bit vectors for problem.  */

static void 
df_rd_init_solution (struct dataflow *dflow, bitmap all_blocks)
{
  unsigned int bb_index;
  bitmap_iterator bi;

  EXECUTE_IF_SET_IN_BITMAP (all_blocks, 0, bb_index, bi)
    {
      struct df_rd_bb_info *bb_info = df_rd_get_bb_info (dflow, bb_index);
      
      bitmap_copy (bb_info->out, bb_info->gen);
      bitmap_clear (bb_info->in);
    }
}

/* In of target gets or of out of source.  */

static void
df_rd_confluence_n (struct dataflow *dflow, edge e)
{
  bitmap op1 = df_rd_get_bb_info (dflow, e->dest->index)->in;
  bitmap op2 = df_rd_get_bb_info (dflow, e->src->index)->out;

  if (e->flags & EDGE_EH)
    {
      struct df_rd_problem_data *problem_data
	= (struct df_rd_problem_data *) dflow->problem_data;
      bitmap sparse_invalidated = problem_data->sparse_invalidated_by_call;
      bitmap dense_invalidated = problem_data->dense_invalidated_by_call;
      struct df *df = dflow->df;
      bitmap_iterator bi;
      unsigned int regno;
      bitmap tmp = BITMAP_ALLOC (NULL);

      bitmap_copy (tmp, op2);
      bitmap_and_compl_into (tmp, dense_invalidated);

      EXECUTE_IF_SET_IN_BITMAP (sparse_invalidated, 0, regno, bi)
 	{
 	  bitmap_clear_range (tmp, 
 			      DF_REG_DEF_GET (df, regno)->begin, 
 			      DF_REG_DEF_GET (df, regno)->n_refs);
	}
      bitmap_ior_into (op1, tmp);
      BITMAP_FREE (tmp);
    }
  else
    bitmap_ior_into (op1, op2);
}


/* Transfer function.  */

static bool
df_rd_transfer_function (struct dataflow *dflow, int bb_index)
{
  struct df_rd_bb_info *bb_info = df_rd_get_bb_info (dflow, bb_index);
  unsigned int regno;
  bitmap_iterator bi;
  bitmap in = bb_info->in;
  bitmap out = bb_info->out;
  bitmap gen = bb_info->gen;
  bitmap kill = bb_info->kill;
  bitmap sparse_kill = bb_info->sparse_kill;

  if (bitmap_empty_p (sparse_kill))
    return  bitmap_ior_and_compl (out, gen, in, kill);
  else 
    {
      struct df *df = dflow->df;
      bool changed = false;
      bitmap tmp = BITMAP_ALLOC (NULL);
      bitmap_copy (tmp, in);
      EXECUTE_IF_SET_IN_BITMAP (sparse_kill, 0, regno, bi)
	{
	  bitmap_clear_range (tmp, 
			      DF_REG_DEF_GET (df, regno)->begin, 
			      DF_REG_DEF_GET (df, regno)->n_refs);
	}
      bitmap_and_compl_into (tmp, kill);
      bitmap_ior_into (tmp, gen);
      changed = !bitmap_equal_p (tmp, out);
      if (changed)
	{
	  BITMAP_FREE (out);
	  bb_info->out = tmp;
	}
      else 
	  BITMAP_FREE (tmp);
      return changed;
    }
}


/* Free all storage associated with the problem.  */

static void
df_rd_free (struct dataflow *dflow)
{
  unsigned int i;
  struct df_rd_problem_data *problem_data
    = (struct df_rd_problem_data *) dflow->problem_data;

  if (problem_data)
    {
      for (i = 0; i < dflow->block_info_size; i++)
	{
	  struct df_rd_bb_info *bb_info = df_rd_get_bb_info (dflow, i);
	  if (bb_info)
	    {
	      BITMAP_FREE (bb_info->kill);
	      BITMAP_FREE (bb_info->sparse_kill);
	      BITMAP_FREE (bb_info->gen);
	      BITMAP_FREE (bb_info->in);
	      BITMAP_FREE (bb_info->out);
	    }
	}
      
      free_alloc_pool (dflow->block_pool);
      
      for (i = 0; i < problem_data->def_sites_size; i++)
	{
	  bitmap bm = problem_data->def_sites[i];
	  if (bm)
	    BITMAP_FREE (bm);
	}
      
      free (problem_data->def_sites);
      BITMAP_FREE (problem_data->sparse_invalidated_by_call);
      BITMAP_FREE (problem_data->dense_invalidated_by_call);
      
      dflow->block_info_size = 0;
      free (dflow->block_info);
      free (dflow->problem_data);
    }
  free (dflow);
}


/* Debugging info.  */

static void
df_rd_dump (struct dataflow *dflow, FILE *file)
{
  struct df *df = dflow->df;
  basic_block bb;
  struct df_rd_problem_data *problem_data
    = (struct df_rd_problem_data *) dflow->problem_data;
  unsigned int m = max_reg_num ();
  unsigned int regno;
  
  if (!dflow->block_info) 
    return;

  fprintf (file, "Reaching defs:\n\n");

  fprintf (file, "  sparse invalidated \t");
  dump_bitmap (file, problem_data->sparse_invalidated_by_call);
  fprintf (file, "  dense invalidated \t");
  dump_bitmap (file, problem_data->dense_invalidated_by_call);

  for (regno = 0; regno < m; regno++)
    if (DF_REG_DEF_GET (df, regno)->n_refs)
      fprintf (file, "%d[%d,%d] ", regno, 
	       DF_REG_DEF_GET (df, regno)->begin, 
	       DF_REG_DEF_GET (df, regno)->n_refs);
  fprintf (file, "\n");

  FOR_ALL_BB (bb)
    {
      struct df_rd_bb_info *bb_info = df_rd_get_bb_info (dflow, bb->index);
      df_print_bb_index (bb, file);
      
      if (!bb_info->in)
	continue;
      
      fprintf (file, "  in  \t(%d)\n", (int) bitmap_count_bits (bb_info->in));
      dump_bitmap (file, bb_info->in);
      fprintf (file, "  gen \t(%d)\n", (int) bitmap_count_bits (bb_info->gen));
      dump_bitmap (file, bb_info->gen);
      fprintf (file, "  kill\t(%d)\n", (int) bitmap_count_bits (bb_info->kill));
      dump_bitmap (file, bb_info->kill);
      fprintf (file, "  out \t(%d)\n", (int) bitmap_count_bits (bb_info->out));
      dump_bitmap (file, bb_info->out);
    }
}

/* All of the information associated with every instance of the problem.  */

static struct df_problem problem_RD =
{
  DF_RD,                      /* Problem id.  */
  DF_FORWARD,                 /* Direction.  */
  df_rd_alloc,                /* Allocate the problem specific data.  */
  NULL,                       /* Reset global information.  */
  df_rd_free_bb_info,         /* Free basic block info.  */
  df_rd_local_compute,        /* Local compute function.  */
  df_rd_init_solution,        /* Init the solution specific data.  */
  df_iterative_dataflow,      /* Iterative solver.  */
  NULL,                       /* Confluence operator 0.  */ 
  df_rd_confluence_n,         /* Confluence operator n.  */ 
  df_rd_transfer_function,    /* Transfer function.  */
  NULL,                       /* Finalize function.  */
  df_rd_free,                 /* Free all of the problem information.  */
  df_rd_dump,                 /* Debugging.  */
  NULL,                       /* Dependent problem.  */
  0                           /* Changeable flags.  */
};



/* Create a new DATAFLOW instance and add it to an existing instance
   of DF.  The returned structure is what is used to get at the
   solution.  */

struct dataflow *
df_rd_add_problem (struct df *df, int flags)
{
  return df_add_problem (df, &problem_RD, flags);
}



/*----------------------------------------------------------------------------
   LIVE REGISTERS

   Find the locations in the function where any use of a pseudo can
   reach in the backwards direction.  In and out bitvectors are built
   for each basic block.  The regnum is used to index into these sets.
   See df.h for details.
   ----------------------------------------------------------------------------*/

/* Get basic block info.  */

struct df_lr_bb_info *
df_lr_get_bb_info (struct dataflow *dflow, unsigned int index)
{
  return (struct df_lr_bb_info *) dflow->block_info[index];
}


/* Set basic block info.  */

static void
df_lr_set_bb_info (struct dataflow *dflow, unsigned int index, 
		   struct df_lr_bb_info *bb_info)
{
  dflow->block_info[index] = bb_info;
}

 
/* Free basic block info.  */

static void
df_lr_free_bb_info (struct dataflow *dflow, 
		    basic_block bb ATTRIBUTE_UNUSED, 
		    void *vbb_info)
{
  struct df_lr_bb_info *bb_info = (struct df_lr_bb_info *) vbb_info;
  if (bb_info)
    {
      BITMAP_FREE (bb_info->use);
      BITMAP_FREE (bb_info->def);
      BITMAP_FREE (bb_info->in);
      BITMAP_FREE (bb_info->out);
      pool_free (dflow->block_pool, bb_info);
    }
}


/* Allocate or reset bitmaps for DFLOW blocks. The solution bits are
   not touched unless the block is new.  */

static void 
df_lr_alloc (struct dataflow *dflow, bitmap blocks_to_rescan,
	     bitmap all_blocks ATTRIBUTE_UNUSED)
{
  unsigned int bb_index;
  bitmap_iterator bi;

  if (!dflow->block_pool)
    dflow->block_pool = create_alloc_pool ("df_lr_block pool", 
					   sizeof (struct df_lr_bb_info), 50);

  df_grow_bb_info (dflow);

  EXECUTE_IF_SET_IN_BITMAP (blocks_to_rescan, 0, bb_index, bi)
    {
      struct df_lr_bb_info *bb_info = df_lr_get_bb_info (dflow, bb_index);
      if (bb_info)
	{ 
	  bitmap_clear (bb_info->def);
	  bitmap_clear (bb_info->use);
	}
      else
	{ 
	  bb_info = (struct df_lr_bb_info *) pool_alloc (dflow->block_pool);
	  df_lr_set_bb_info (dflow, bb_index, bb_info);
	  bb_info->use = BITMAP_ALLOC (NULL);
	  bb_info->def = BITMAP_ALLOC (NULL);
	  bb_info->in = BITMAP_ALLOC (NULL);
	  bb_info->out = BITMAP_ALLOC (NULL);
	}
    }
}


/* Compute local live register info for basic block BB.  */

static void
df_lr_bb_local_compute (struct dataflow *dflow, 
			struct df *df, unsigned int bb_index)
{
  basic_block bb = BASIC_BLOCK (bb_index);
  struct df_lr_bb_info *bb_info = df_lr_get_bb_info (dflow, bb_index);
  rtx insn;
  struct df_ref *def;
  struct df_ref *use;

  /* Process the registers set in an exception handler.  */
  for (def = df_get_artificial_defs (df, bb_index); def; def = def->next_ref)
    if (((DF_REF_FLAGS (def) & DF_REF_AT_TOP) == 0)
	&& (!(DF_REF_FLAGS (def) & DF_REF_PARTIAL)))
      {
	unsigned int dregno = DF_REF_REGNO (def);
	bitmap_set_bit (bb_info->def, dregno);
	bitmap_clear_bit (bb_info->use, dregno);
      }

  /* Process the hardware registers that are always live.  */
  for (use = df_get_artificial_uses (df, bb_index); use; use = use->next_ref)
    /* Add use to set of uses in this BB.  */
    if ((DF_REF_FLAGS (use) & DF_REF_AT_TOP) == 0)
      bitmap_set_bit (bb_info->use, DF_REF_REGNO (use));

  FOR_BB_INSNS_REVERSE (bb, insn)
    {
      unsigned int uid = INSN_UID (insn);

      if (!INSN_P (insn))
	continue;	

      if (CALL_P (insn))
	{
	  for (def = DF_INSN_UID_DEFS (df, uid); def; def = def->next_ref)
	    {
	      unsigned int dregno = DF_REF_REGNO (def);
	      
	      if (dregno >= FIRST_PSEUDO_REGISTER
		  || !(SIBLING_CALL_P (insn)
		       && bitmap_bit_p (df->exit_block_uses, dregno)
		       && !refers_to_regno_p (dregno, dregno+1,
					      current_function_return_rtx,
					      (rtx *)0)))
		{
		  /* If the def is to only part of the reg, it does
		     not kill the other defs that reach here.  */
		  if (!(DF_REF_FLAGS (def) & DF_REF_PARTIAL))
		    {
		      bitmap_set_bit (bb_info->def, dregno);
		      bitmap_clear_bit (bb_info->use, dregno);
		    }
		}
	    }
	}
      else
	{
	  for (def = DF_INSN_UID_DEFS (df, uid); def; def = def->next_ref)
	    {
	      unsigned int dregno = DF_REF_REGNO (def);
	      
	      if (DF_INSN_CONTAINS_ASM (df, insn) 
		  && dregno < FIRST_PSEUDO_REGISTER)
		{
		  unsigned int i;
		  unsigned int end = dregno 
		    + hard_regno_nregs[dregno][GET_MODE (DF_REF_REG (def))] - 1;
		  for (i = dregno; i <= end; ++i)
		    regs_asm_clobbered[i] = 1;
		}
	      /* If the def is to only part of the reg, it does
		     not kill the other defs that reach here.  */
	      if (!(DF_REF_FLAGS (def) & DF_REF_PARTIAL))
		{
		  bitmap_set_bit (bb_info->def, dregno);
		  bitmap_clear_bit (bb_info->use, dregno);
		}
	    }
	}

      for (use = DF_INSN_UID_USES (df, uid); use; use = use->next_ref)
	/* Add use to set of uses in this BB.  */
	bitmap_set_bit (bb_info->use, DF_REF_REGNO (use));
    }

  /* Process the registers set in an exception handler.  */
  for (def = df_get_artificial_defs (df, bb_index); def; def = def->next_ref)
    if ((DF_REF_FLAGS (def) & DF_REF_AT_TOP)
	&& (!(DF_REF_FLAGS (def) & DF_REF_PARTIAL)))
      {
	unsigned int dregno = DF_REF_REGNO (def);
	bitmap_set_bit (bb_info->def, dregno);
	bitmap_clear_bit (bb_info->use, dregno);
      }
  
#ifdef EH_USES
  /* Process the uses that are live into an exception handler.  */
  for (use = df_get_artificial_uses (df, bb_index); use; use = use->next_ref)
    /* Add use to set of uses in this BB.  */
    if (DF_REF_FLAGS (use) & DF_REF_AT_TOP)
      bitmap_set_bit (bb_info->use, DF_REF_REGNO (use));
#endif
}


/* Compute local live register info for each basic block within BLOCKS.  */

static void
df_lr_local_compute (struct dataflow *dflow, 
		     bitmap all_blocks,
		     bitmap rescan_blocks)
{
  struct df *df = dflow->df;
  unsigned int bb_index;
  bitmap_iterator bi;
    
  /* Assume that the stack pointer is unchanging if alloca hasn't
     been used.  */
  if (bitmap_equal_p (all_blocks, rescan_blocks))
    memset (regs_asm_clobbered, 0, sizeof (regs_asm_clobbered));
  
  bitmap_clear (df->hardware_regs_used);
  
  /* The all-important stack pointer must always be live.  */
  bitmap_set_bit (df->hardware_regs_used, STACK_POINTER_REGNUM);
  
  /* Before reload, there are a few registers that must be forced
     live everywhere -- which might not already be the case for
     blocks within infinite loops.  */
  if (!reload_completed)
    {
      /* Any reference to any pseudo before reload is a potential
	 reference of the frame pointer.  */
      bitmap_set_bit (df->hardware_regs_used, FRAME_POINTER_REGNUM);
      
#if FRAME_POINTER_REGNUM != ARG_POINTER_REGNUM
      /* Pseudos with argument area equivalences may require
	 reloading via the argument pointer.  */
      if (fixed_regs[ARG_POINTER_REGNUM])
	bitmap_set_bit (df->hardware_regs_used, ARG_POINTER_REGNUM);
#endif
      
      /* Any constant, or pseudo with constant equivalences, may
	 require reloading from memory using the pic register.  */
      if ((unsigned) PIC_OFFSET_TABLE_REGNUM != INVALID_REGNUM
	  && fixed_regs[PIC_OFFSET_TABLE_REGNUM])
	bitmap_set_bit (df->hardware_regs_used, PIC_OFFSET_TABLE_REGNUM);
    }
  
  if (bitmap_bit_p (rescan_blocks, EXIT_BLOCK))
    {
      /* The exit block is special for this problem and its bits are
	 computed from thin air.  */
      struct df_lr_bb_info *bb_info = df_lr_get_bb_info (dflow, EXIT_BLOCK);
      bitmap_copy (bb_info->use, df->exit_block_uses);
    }
  
  EXECUTE_IF_SET_IN_BITMAP (rescan_blocks, 0, bb_index, bi)
    {
      if (bb_index == EXIT_BLOCK)
	continue;
      df_lr_bb_local_compute (dflow, df, bb_index);
    }
}


/* Initialize the solution vectors.  */

static void 
df_lr_init (struct dataflow *dflow, bitmap all_blocks)
{
  unsigned int bb_index;
  bitmap_iterator bi;

  EXECUTE_IF_SET_IN_BITMAP (all_blocks, 0, bb_index, bi)
    {
      struct df_lr_bb_info *bb_info = df_lr_get_bb_info (dflow, bb_index);
      bitmap_copy (bb_info->in, bb_info->use);
      bitmap_clear (bb_info->out);
    }
}


/* Confluence function that processes infinite loops.  This might be a
   noreturn function that throws.  And even if it isn't, getting the
   unwind info right helps debugging.  */
static void
df_lr_confluence_0 (struct dataflow *dflow, basic_block bb)
{
  struct df *df = dflow->df;

  bitmap op1 = df_lr_get_bb_info (dflow, bb->index)->out;
  if (bb != EXIT_BLOCK_PTR)
    bitmap_copy (op1, df->hardware_regs_used);
} 


/* Confluence function that ignores fake edges.  */

static void
df_lr_confluence_n (struct dataflow *dflow, edge e)
{
  bitmap op1 = df_lr_get_bb_info (dflow, e->src->index)->out;
  bitmap op2 = df_lr_get_bb_info (dflow, e->dest->index)->in;
 
  /* Call-clobbered registers die across exception and call edges.  */
  /* ??? Abnormal call edges ignored for the moment, as this gets
     confused by sibling call edges, which crashes reg-stack.  */
  if (e->flags & EDGE_EH)
    bitmap_ior_and_compl_into (op1, op2, df_invalidated_by_call);
  else
    bitmap_ior_into (op1, op2);

  bitmap_ior_into (op1, dflow->df->hardware_regs_used);
} 


/* Transfer function.  */

static bool
df_lr_transfer_function (struct dataflow *dflow, int bb_index)
{
  struct df_lr_bb_info *bb_info = df_lr_get_bb_info (dflow, bb_index);
  bitmap in = bb_info->in;
  bitmap out = bb_info->out;
  bitmap use = bb_info->use;
  bitmap def = bb_info->def;

  return bitmap_ior_and_compl (in, use, out, def);
}


/* Free all storage associated with the problem.  */

static void
df_lr_free (struct dataflow *dflow)
{
  if (dflow->block_info)
    {
      unsigned int i;
      for (i = 0; i < dflow->block_info_size; i++)
	{
	  struct df_lr_bb_info *bb_info = df_lr_get_bb_info (dflow, i);
	  if (bb_info)
	    {
	      BITMAP_FREE (bb_info->use);
	      BITMAP_FREE (bb_info->def);
	      BITMAP_FREE (bb_info->in);
	      BITMAP_FREE (bb_info->out);
	    }
	}
      free_alloc_pool (dflow->block_pool);
      
      dflow->block_info_size = 0;
      free (dflow->block_info);
    }

  free (dflow->problem_data);
  free (dflow);
}


/* Debugging info.  */

static void
df_lr_dump (struct dataflow *dflow, FILE *file)
{
  basic_block bb;
  
  if (!dflow->block_info) 
    return;

  fprintf (file, "Live Registers:\n");
  FOR_ALL_BB (bb)
    {
      struct df_lr_bb_info *bb_info = df_lr_get_bb_info (dflow, bb->index);
      df_print_bb_index (bb, file);
      
      if (!bb_info->in)
	continue;
      
      fprintf (file, "  in  \t");
      dump_bitmap (file, bb_info->in);
      fprintf (file, "  use \t");
      dump_bitmap (file, bb_info->use);
      fprintf (file, "  def \t");
      dump_bitmap (file, bb_info->def);
      fprintf (file, "  out \t");
      dump_bitmap (file, bb_info->out);
    }
}

/* All of the information associated with every instance of the problem.  */

static struct df_problem problem_LR =
{
  DF_LR,                      /* Problem id.  */
  DF_BACKWARD,                /* Direction.  */
  df_lr_alloc,                /* Allocate the problem specific data.  */
  NULL,                       /* Reset global information.  */
  df_lr_free_bb_info,         /* Free basic block info.  */
  df_lr_local_compute,        /* Local compute function.  */
  df_lr_init,                 /* Init the solution specific data.  */
  df_iterative_dataflow,      /* Iterative solver.  */
  df_lr_confluence_0,         /* Confluence operator 0.  */ 
  df_lr_confluence_n,         /* Confluence operator n.  */ 
  df_lr_transfer_function,    /* Transfer function.  */
  NULL,                       /* Finalize function.  */
  df_lr_free,                 /* Free all of the problem information.  */
  df_lr_dump,                 /* Debugging.  */
  NULL,                       /* Dependent problem.  */
  0
};


/* Create a new DATAFLOW instance and add it to an existing instance
   of DF.  The returned structure is what is used to get at the
   solution.  */

struct dataflow *
df_lr_add_problem (struct df *df, int flags)
{
  return df_add_problem (df, &problem_LR, flags);
}



/*----------------------------------------------------------------------------
   UNINITIALIZED REGISTERS

   Find the set of uses for registers that are reachable from the entry
   block without passing thru a definition.  In and out bitvectors are built
   for each basic block.  The regnum is used to index into these sets.
   See df.h for details.
----------------------------------------------------------------------------*/

/* Get basic block info.  */

struct df_ur_bb_info *
df_ur_get_bb_info (struct dataflow *dflow, unsigned int index)
{
  return (struct df_ur_bb_info *) dflow->block_info[index];
}


/* Set basic block info.  */

static void
df_ur_set_bb_info (struct dataflow *dflow, unsigned int index, 
		   struct df_ur_bb_info *bb_info)
{
  dflow->block_info[index] = bb_info;
}


/* Free basic block info.  */

static void
df_ur_free_bb_info (struct dataflow *dflow, 
		    basic_block bb ATTRIBUTE_UNUSED, 
		    void *vbb_info)
{
  struct df_ur_bb_info *bb_info = (struct df_ur_bb_info *) vbb_info;
  if (bb_info)
    {
      BITMAP_FREE (bb_info->gen);
      BITMAP_FREE (bb_info->kill);
      BITMAP_FREE (bb_info->in);
      BITMAP_FREE (bb_info->out);
      pool_free (dflow->block_pool, bb_info);
    }
}


/* Allocate or reset bitmaps for DFLOW blocks. The solution bits are
   not touched unless the block is new.  */

static void 
df_ur_alloc (struct dataflow *dflow, bitmap blocks_to_rescan,
	     bitmap all_blocks ATTRIBUTE_UNUSED)
{
  unsigned int bb_index;
  bitmap_iterator bi;

  if (!dflow->block_pool)
    dflow->block_pool = create_alloc_pool ("df_ur_block pool", 
					   sizeof (struct df_ur_bb_info), 100);

  df_grow_bb_info (dflow);

  EXECUTE_IF_SET_IN_BITMAP (blocks_to_rescan, 0, bb_index, bi)
    {
      struct df_ur_bb_info *bb_info = df_ur_get_bb_info (dflow, bb_index);
      if (bb_info)
	{ 
	  bitmap_clear (bb_info->kill);
	  bitmap_clear (bb_info->gen);
	}
      else
	{ 
	  bb_info = (struct df_ur_bb_info *) pool_alloc (dflow->block_pool);
	  df_ur_set_bb_info (dflow, bb_index, bb_info);
	  bb_info->kill = BITMAP_ALLOC (NULL);
	  bb_info->gen = BITMAP_ALLOC (NULL);
	  bb_info->in = BITMAP_ALLOC (NULL);
	  bb_info->out = BITMAP_ALLOC (NULL);
	}
    }
}


/* Compute local uninitialized register info for basic block BB.  */

static void
df_ur_bb_local_compute (struct dataflow *dflow, unsigned int bb_index)
{
  struct df *df = dflow->df;
  basic_block bb = BASIC_BLOCK (bb_index);
  struct df_ur_bb_info *bb_info = df_ur_get_bb_info (dflow, bb_index);
  rtx insn;
  struct df_ref *def;

  bitmap_clear (seen_in_block);
  bitmap_clear (seen_in_insn);

  for (def = df_get_artificial_defs (df, bb_index); def; def = def->next_ref)
    if ((DF_REF_FLAGS (def) & DF_REF_AT_TOP) == 0)
      {
	unsigned int regno = DF_REF_REGNO (def);
	if (!bitmap_bit_p (seen_in_block, regno))
	  {
	    bitmap_set_bit (seen_in_block, regno);
	    bitmap_set_bit (bb_info->gen, regno);
	  }
      }

  FOR_BB_INSNS_REVERSE (bb, insn)
    {
      unsigned int uid = INSN_UID (insn);
      if (!INSN_P (insn))
	continue;

      for (def = DF_INSN_UID_DEFS (df, uid); def; def = def->next_ref)
	{
	  unsigned int regno = DF_REF_REGNO (def);
	  /* Only the last def counts.  */
	  if (!bitmap_bit_p (seen_in_block, regno))
	    {
	      bitmap_set_bit (seen_in_insn, regno);
	      
	      if (DF_REF_FLAGS (def) 
		  & (DF_REF_MUST_CLOBBER | DF_REF_MAY_CLOBBER))
		{
		  /* Only must clobbers for the entire reg destroy the
		     value.  */
		  if ((DF_REF_FLAGS (def) & DF_REF_MUST_CLOBBER)
		      && (!DF_REF_FLAGS (def) & DF_REF_PARTIAL))
		    bitmap_set_bit (bb_info->kill, regno);
		}
	      else
		bitmap_set_bit (bb_info->gen, regno);
	    }
	}
      bitmap_ior_into (seen_in_block, seen_in_insn);
      bitmap_clear (seen_in_insn);
    }

  for (def = df_get_artificial_defs (df, bb_index); def; def = def->next_ref)
    if (DF_REF_FLAGS (def) & DF_REF_AT_TOP)
      {
	unsigned int regno = DF_REF_REGNO (def);
	if (!bitmap_bit_p (seen_in_block, regno))
	  {
	    bitmap_set_bit (seen_in_block, regno);
	    bitmap_set_bit (bb_info->gen, regno);
	  }
      }
}


/* Compute local uninitialized register info.  */

static void
df_ur_local_compute (struct dataflow *dflow, 
		     bitmap all_blocks ATTRIBUTE_UNUSED,
		     bitmap rescan_blocks)
{
  unsigned int bb_index;
  bitmap_iterator bi;

  df_set_seen ();

  EXECUTE_IF_SET_IN_BITMAP (rescan_blocks, 0, bb_index, bi)
    {
      df_ur_bb_local_compute (dflow, bb_index);
    }

  df_unset_seen ();
}


/* Initialize the solution vectors.  */

static void 
df_ur_init (struct dataflow *dflow, bitmap all_blocks)
{
  unsigned int bb_index;
  bitmap_iterator bi;

  EXECUTE_IF_SET_IN_BITMAP (all_blocks, 0, bb_index, bi)
    {
      struct df_ur_bb_info *bb_info = df_ur_get_bb_info (dflow, bb_index);

      bitmap_copy (bb_info->out, bb_info->gen);
      bitmap_clear (bb_info->in);
    }
}


/* Or in the stack regs, hard regs and early clobber regs into the the
   ur_in sets of all of the blocks.  */

static void
df_ur_local_finalize (struct dataflow *dflow, bitmap all_blocks)
{
  struct df *df = dflow->df;
  struct dataflow *lr_dflow = df->problems_by_index[DF_LR];
  bitmap tmp = BITMAP_ALLOC (NULL);
  bitmap_iterator bi;
  unsigned int bb_index;

  EXECUTE_IF_SET_IN_BITMAP (all_blocks, 0, bb_index, bi)
    {
      struct df_ur_bb_info *bb_info = df_ur_get_bb_info (dflow, bb_index);
      struct df_lr_bb_info *bb_lr_info = df_lr_get_bb_info (lr_dflow, bb_index);
      
      /* No register may reach a location where it is not used.  Thus
	 we trim the rr result to the places where it is used.  */
      bitmap_and_into (bb_info->in, bb_lr_info->in);
      bitmap_and_into (bb_info->out, bb_lr_info->out);
      
#if 1
      /* Hard registers may still stick in the ur_out set, but not
	 be in the ur_in set, if their only mention was in a call
	 in this block.  This is because a call kills in the lr
	 problem but does not kill in the ur problem.  To clean
	 this up, we execute the transfer function on the lr_in
	 set and then use that to knock bits out of ur_out.  */
      bitmap_ior_and_compl (tmp, bb_info->gen, bb_lr_info->in, 
			    bb_info->kill);
      bitmap_and_into (bb_info->out, tmp);
#endif
    }

  BITMAP_FREE (tmp);
}


/* Confluence function that ignores fake edges.  */

static void
df_ur_confluence_n (struct dataflow *dflow, edge e)
{
  bitmap op1 = df_ur_get_bb_info (dflow, e->dest->index)->in;
  bitmap op2 = df_ur_get_bb_info (dflow, e->src->index)->out;
 
  if (e->flags & EDGE_FAKE) 
    return;

  bitmap_ior_into (op1, op2);
} 


/* Transfer function.  */

static bool
df_ur_transfer_function (struct dataflow *dflow, int bb_index)
{
  struct df_ur_bb_info *bb_info = df_ur_get_bb_info (dflow, bb_index);
  bitmap in = bb_info->in;
  bitmap out = bb_info->out;
  bitmap gen = bb_info->gen;
  bitmap kill = bb_info->kill;

  return bitmap_ior_and_compl (out, gen, in, kill);
}


/* Free all storage associated with the problem.  */

static void
df_ur_free (struct dataflow *dflow)
{
  if (dflow->block_info)
    {
      unsigned int i;
      
      for (i = 0; i < dflow->block_info_size; i++)
	{
	  struct df_ur_bb_info *bb_info = df_ur_get_bb_info (dflow, i);
	  if (bb_info)
	    {
	      BITMAP_FREE (bb_info->gen);
	      BITMAP_FREE (bb_info->kill);
	      BITMAP_FREE (bb_info->in);
	      BITMAP_FREE (bb_info->out);
	    }
	}
      
      free_alloc_pool (dflow->block_pool);
      dflow->block_info_size = 0;
      free (dflow->block_info);
    }
  free (dflow);
}


/* Debugging info.  */

static void
df_ur_dump (struct dataflow *dflow, FILE *file)
{
  basic_block bb;
  
  if (!dflow->block_info) 
    return;

  fprintf (file, "Undefined regs:\n");
 
  FOR_ALL_BB (bb)
    {
      struct df_ur_bb_info *bb_info = df_ur_get_bb_info (dflow, bb->index);
      df_print_bb_index (bb, file);
      
      if (!bb_info->in)
	continue;
      
      fprintf (file, "  in  \t");
      dump_bitmap (file, bb_info->in);
      fprintf (file, "  gen \t");
      dump_bitmap (file, bb_info->gen);
      fprintf (file, "  kill\t");
      dump_bitmap (file, bb_info->kill);
      fprintf (file, "  out \t");
      dump_bitmap (file, bb_info->out);
    }
}

/* All of the information associated with every instance of the problem.  */

static struct df_problem problem_UR =
{
  DF_UR,                      /* Problem id.  */
  DF_FORWARD,                 /* Direction.  */
  df_ur_alloc,                /* Allocate the problem specific data.  */
  NULL,                       /* Reset global information.  */
  df_ur_free_bb_info,         /* Free basic block info.  */
  df_ur_local_compute,        /* Local compute function.  */
  df_ur_init,                 /* Init the solution specific data.  */
  df_iterative_dataflow,      /* Iterative solver.  */
  NULL,                       /* Confluence operator 0.  */ 
  df_ur_confluence_n,         /* Confluence operator n.  */ 
  df_ur_transfer_function,    /* Transfer function.  */
  df_ur_local_finalize,       /* Finalize function.  */
  df_ur_free,                 /* Free all of the problem information.  */
  df_ur_dump,                 /* Debugging.  */
  df_lr_add_problem,          /* Dependent problem.  */
  0                           /* Changeable flags.  */
};


/* Create a new DATAFLOW instance and add it to an existing instance
   of DF.  The returned structure is what is used to get at the
   solution.  */

struct dataflow *
df_ur_add_problem (struct df *df, int flags)
{
  return df_add_problem (df, &problem_UR, flags);
}



/*----------------------------------------------------------------------------
   UNINITIALIZED REGISTERS WITH EARLYCLOBBER

   Find the set of uses for registers that are reachable from the entry
   block without passing thru a definition.  In and out bitvectors are built
   for each basic block.  The regnum is used to index into these sets.
   See df.h for details.

   This is a variant of the UR problem above that has a lot of special
   features just for the register allocation phase.  This problem
   should go a away if someone would fix the interference graph.

   ----------------------------------------------------------------------------*/

/* Private data used to compute the solution for this problem.  These
   data structures are not accessible outside of this module.  */
struct df_urec_problem_data
{
  bool earlyclobbers_found;     /* True if any instruction contains an
				   earlyclobber.  */
#ifdef STACK_REGS
  bitmap stack_regs;		/* Registers that may be allocated to a STACK_REGS.  */
#endif
};


/* Get basic block info.  */

struct df_urec_bb_info *
df_urec_get_bb_info (struct dataflow *dflow, unsigned int index)
{
  return (struct df_urec_bb_info *) dflow->block_info[index];
}


/* Set basic block info.  */

static void
df_urec_set_bb_info (struct dataflow *dflow, unsigned int index, 
		   struct df_urec_bb_info *bb_info)
{
  dflow->block_info[index] = bb_info;
}


/* Free basic block info.  */

static void
df_urec_free_bb_info (struct dataflow *dflow, 
		      basic_block bb ATTRIBUTE_UNUSED, 
		      void *vbb_info)
{
  struct df_urec_bb_info *bb_info = (struct df_urec_bb_info *) vbb_info;
  if (bb_info)
    {
      BITMAP_FREE (bb_info->gen);
      BITMAP_FREE (bb_info->kill);
      BITMAP_FREE (bb_info->in);
      BITMAP_FREE (bb_info->out);
      BITMAP_FREE (bb_info->earlyclobber);
      pool_free (dflow->block_pool, bb_info);
    }
}


/* Allocate or reset bitmaps for DFLOW blocks. The solution bits are
   not touched unless the block is new.  */

static void 
df_urec_alloc (struct dataflow *dflow, bitmap blocks_to_rescan,
	       bitmap all_blocks ATTRIBUTE_UNUSED)

{
  unsigned int bb_index;
  bitmap_iterator bi;
  struct df_urec_problem_data *problem_data
    = (struct df_urec_problem_data *) dflow->problem_data;

  if (!dflow->block_pool)
    dflow->block_pool = create_alloc_pool ("df_urec_block pool", 
					   sizeof (struct df_urec_bb_info), 50);

  if (!dflow->problem_data)
    {
      problem_data = XNEW (struct df_urec_problem_data);
      dflow->problem_data = problem_data;
    }
  problem_data->earlyclobbers_found = false;

  df_grow_bb_info (dflow);

  EXECUTE_IF_SET_IN_BITMAP (blocks_to_rescan, 0, bb_index, bi)
    {
      struct df_urec_bb_info *bb_info = df_urec_get_bb_info (dflow, bb_index);
      if (bb_info)
	{ 
	  bitmap_clear (bb_info->kill);
	  bitmap_clear (bb_info->gen);
	  bitmap_clear (bb_info->earlyclobber);
	}
      else
	{ 
	  bb_info = (struct df_urec_bb_info *) pool_alloc (dflow->block_pool);
	  df_urec_set_bb_info (dflow, bb_index, bb_info);
	  bb_info->kill = BITMAP_ALLOC (NULL);
	  bb_info->gen = BITMAP_ALLOC (NULL);
	  bb_info->in = BITMAP_ALLOC (NULL);
	  bb_info->out = BITMAP_ALLOC (NULL);
	  bb_info->earlyclobber = BITMAP_ALLOC (NULL);
	}
    }
}


/* The function modifies local info for register REG being changed in
   SETTER.  DATA is used to pass the current basic block info.  */

static void
df_urec_mark_reg_change (rtx reg, rtx setter, void *data)
{
  int regno;
  int endregno;
  int i;
  struct df_urec_bb_info *bb_info = (struct df_urec_bb_info*) data;

  if (GET_CODE (reg) == SUBREG)
    reg = SUBREG_REG (reg);

  if (!REG_P (reg))
    return;
  
  
  endregno = regno = REGNO (reg);
  if (regno < FIRST_PSEUDO_REGISTER)
    {
      endregno +=hard_regno_nregs[regno][GET_MODE (reg)];
      
      for (i = regno; i < endregno; i++)
	{
	  bitmap_set_bit (bb_info->kill, i);
	  
	  if (GET_CODE (setter) != CLOBBER)
	    bitmap_set_bit (bb_info->gen, i);
	  else
	    bitmap_clear_bit (bb_info->gen, i);
	}
    }
  else
    {
      bitmap_set_bit (bb_info->kill, regno);
      
      if (GET_CODE (setter) != CLOBBER)
	bitmap_set_bit (bb_info->gen, regno);
      else
	bitmap_clear_bit (bb_info->gen, regno);
    }
}
/* Classes of registers which could be early clobbered in the current
   insn.  */

static VEC(int,heap) *earlyclobber_regclass;

/* This function finds and stores register classes that could be early
   clobbered in INSN.  If any earlyclobber classes are found, the function
   returns TRUE, in all other cases it returns FALSE.  */

static bool
df_urec_check_earlyclobber (rtx insn)
{
  int opno;
  bool found = false;

  extract_insn (insn);

  VEC_truncate (int, earlyclobber_regclass, 0);
  for (opno = 0; opno < recog_data.n_operands; opno++)
    {
      char c;
      bool amp_p;
      int i;
      enum reg_class class;
      const char *p = recog_data.constraints[opno];

      class = NO_REGS;
      amp_p = false;
      for (;;)
	{
	  c = *p;
	  switch (c)
	    {
	    case '=':  case '+':  case '?':
	    case '#':  case '!':
	    case '*':  case '%':
	    case 'm':  case '<':  case '>':  case 'V':  case 'o':
	    case 'E':  case 'F':  case 'G':  case 'H':
	    case 's':  case 'i':  case 'n':
	    case 'I':  case 'J':  case 'K':  case 'L':
	    case 'M':  case 'N':  case 'O':  case 'P':
	    case 'X':
	    case '0': case '1':  case '2':  case '3':  case '4':
	    case '5': case '6':  case '7':  case '8':  case '9':
	      /* These don't say anything we care about.  */
	      break;

	    case '&':
	      amp_p = true;
	      break;
	    case '\0':
	    case ',':
	      if (amp_p && class != NO_REGS)
		{
		  int rc;

		  found = true;
		  for (i = 0;
		       VEC_iterate (int, earlyclobber_regclass, i, rc);
		       i++)
		    {
		      if (rc == (int) class)
			goto found_rc;
		    }

		  /* We use VEC_quick_push here because
		     earlyclobber_regclass holds no more than
		     N_REG_CLASSES elements. */
		  VEC_quick_push (int, earlyclobber_regclass, (int) class);
		found_rc:
		  ;
		}
	      
	      amp_p = false;
	      class = NO_REGS;
	      break;

	    case 'r':
	      class = GENERAL_REGS;
	      break;

	    default:
	      class = REG_CLASS_FROM_CONSTRAINT (c, p);
	      break;
	    }
	  if (c == '\0')
	    break;
	  p += CONSTRAINT_LEN (c, p);
	}
    }

  return found;
}

/* The function checks that pseudo-register *X has a class
   intersecting with the class of pseudo-register could be early
   clobbered in the same insn.

   This function is a no-op if earlyclobber_regclass is empty. 

   Reload can assign the same hard register to uninitialized
   pseudo-register and early clobbered pseudo-register in an insn if
   the pseudo-register is used first time in given BB and not lived at
   the BB start.  To prevent this we don't change life information for
   such pseudo-registers.  */

static int
df_urec_mark_reg_use_for_earlyclobber (rtx *x, void *data)
{
  enum reg_class pref_class, alt_class;
  int i, regno;
  struct df_urec_bb_info *bb_info = (struct df_urec_bb_info*) data;

  if (REG_P (*x) && REGNO (*x) >= FIRST_PSEUDO_REGISTER)
    {
      int rc;

      regno = REGNO (*x);
      if (bitmap_bit_p (bb_info->kill, regno)
	  || bitmap_bit_p (bb_info->gen, regno))
	return 0;
      pref_class = reg_preferred_class (regno);
      alt_class = reg_alternate_class (regno);
      for (i = 0; VEC_iterate (int, earlyclobber_regclass, i, rc); i++)
	{
	  if (reg_classes_intersect_p (rc, pref_class)
	      || (rc != NO_REGS
		  && reg_classes_intersect_p (rc, alt_class)))
	    {
	      bitmap_set_bit (bb_info->earlyclobber, regno);
	      break;
	    }
	}
    }
  return 0;
}

/* The function processes all pseudo-registers in *X with the aid of
   previous function.  */

static void
df_urec_mark_reg_use_for_earlyclobber_1 (rtx *x, void *data)
{
  for_each_rtx (x, df_urec_mark_reg_use_for_earlyclobber, data);
}


/* Compute local uninitialized register info for basic block BB.  */

static void
df_urec_bb_local_compute (struct dataflow *dflow, unsigned int bb_index)
{
  struct df *df = dflow->df;
  basic_block bb = BASIC_BLOCK (bb_index);
  struct df_urec_bb_info *bb_info = df_urec_get_bb_info (dflow, bb_index);
  rtx insn;
  struct df_ref *def;

  for (def = df_get_artificial_defs (df, bb_index); def; def = def->next_ref)
    if (DF_REF_FLAGS (def) & DF_REF_AT_TOP)
      {
	unsigned int regno = DF_REF_REGNO (def);
	bitmap_set_bit (bb_info->gen, regno);
      }
  
  FOR_BB_INSNS (bb, insn)
    {
      if (INSN_P (insn))
	{
	  note_stores (PATTERN (insn), df_urec_mark_reg_change, bb_info);
	  if (df_urec_check_earlyclobber (insn))
	    {
	      struct df_urec_problem_data *problem_data
		= (struct df_urec_problem_data *) dflow->problem_data;
	      problem_data->earlyclobbers_found = true;
	      note_uses (&PATTERN (insn), 
			 df_urec_mark_reg_use_for_earlyclobber_1, bb_info);
	    }
	}
    }

  for (def = df_get_artificial_defs (df, bb_index); def; def = def->next_ref)
    if ((DF_REF_FLAGS (def) & DF_REF_AT_TOP) == 0)
      {
	unsigned int regno = DF_REF_REGNO (def);
	bitmap_set_bit (bb_info->gen, regno);
      }

}


/* Compute local uninitialized register info.  */

static void
df_urec_local_compute (struct dataflow *dflow, 
		     bitmap all_blocks ATTRIBUTE_UNUSED,
		     bitmap rescan_blocks)
{
  unsigned int bb_index;
  bitmap_iterator bi;
#ifdef STACK_REGS
  int i;
  HARD_REG_SET zero, stack_hard_regs, used;
  struct df_urec_problem_data *problem_data
    = (struct df_urec_problem_data *) dflow->problem_data;
  
  /* Any register that MAY be allocated to a register stack (like the
     387) is treated poorly.  Each such register is marked as being
     live everywhere.  This keeps the register allocator and the
     subsequent passes from doing anything useful with these values.

     FIXME: This seems like an incredibly poor idea.  */

  CLEAR_HARD_REG_SET (zero);
  CLEAR_HARD_REG_SET (stack_hard_regs);
  for (i = FIRST_STACK_REG; i <= LAST_STACK_REG; i++)
    SET_HARD_REG_BIT (stack_hard_regs, i);
  problem_data->stack_regs = BITMAP_ALLOC (NULL);
  for (i = FIRST_PSEUDO_REGISTER; i < max_regno; i++)
    {
      COPY_HARD_REG_SET (used, reg_class_contents[reg_preferred_class (i)]);
      IOR_HARD_REG_SET (used, reg_class_contents[reg_alternate_class (i)]);
      AND_HARD_REG_SET (used, stack_hard_regs);
      GO_IF_HARD_REG_EQUAL (used, zero, skip);
      bitmap_set_bit (problem_data->stack_regs, i);
    skip:
      ;
    }
#endif

  /* We know that earlyclobber_regclass holds no more than
    N_REG_CLASSES elements.  See df_urec_check_earlyclobber.  */
  earlyclobber_regclass = VEC_alloc (int, heap, N_REG_CLASSES);

  EXECUTE_IF_SET_IN_BITMAP (rescan_blocks, 0, bb_index, bi)
    {
      df_urec_bb_local_compute (dflow, bb_index);
    }

  VEC_free (int, heap, earlyclobber_regclass);
}


/* Initialize the solution vectors.  */

static void 
df_urec_init (struct dataflow *dflow, bitmap all_blocks)
{
  unsigned int bb_index;
  bitmap_iterator bi;

  EXECUTE_IF_SET_IN_BITMAP (all_blocks, 0, bb_index, bi)
    {
      struct df_urec_bb_info *bb_info = df_urec_get_bb_info (dflow, bb_index);

      bitmap_copy (bb_info->out, bb_info->gen);
      bitmap_clear (bb_info->in);
    }
}


/* Or in the stack regs, hard regs and early clobber regs into the the
   ur_in sets of all of the blocks.  */

static void
df_urec_local_finalize (struct dataflow *dflow, bitmap all_blocks)
{
  struct df *df = dflow->df;
  struct dataflow *lr_dflow = df->problems_by_index[DF_LR];
  bitmap tmp = BITMAP_ALLOC (NULL);
  bitmap_iterator bi;
  unsigned int bb_index;
  struct df_urec_problem_data *problem_data
    = (struct df_urec_problem_data *) dflow->problem_data;

  EXECUTE_IF_SET_IN_BITMAP (all_blocks, 0, bb_index, bi)
    {
      struct df_urec_bb_info *bb_info = df_urec_get_bb_info (dflow, bb_index);
      struct df_lr_bb_info *bb_lr_info = df_lr_get_bb_info (lr_dflow, bb_index);

      if (bb_index != ENTRY_BLOCK && bb_index != EXIT_BLOCK)
	{
	  if (problem_data->earlyclobbers_found)
	    bitmap_ior_into (bb_info->in, bb_info->earlyclobber);
	
#ifdef STACK_REGS
	  /* We can not use the same stack register for uninitialized
	     pseudo-register and another living pseudo-register
	     because if the uninitialized pseudo-register dies,
	     subsequent pass reg-stack will be confused (it will
	     believe that the other register dies).  */
	  bitmap_ior_into (bb_info->in, problem_data->stack_regs);
	  bitmap_ior_into (bb_info->out, problem_data->stack_regs);
#endif
	}

      /* No register may reach a location where it is not used.  Thus
	 we trim the rr result to the places where it is used.  */
      bitmap_and_into (bb_info->in, bb_lr_info->in);
      bitmap_and_into (bb_info->out, bb_lr_info->out);
      
#if 1
      /* Hard registers may still stick in the ur_out set, but not
	 be in the ur_in set, if their only mention was in a call
	 in this block.  This is because a call kills in the lr
	 problem but does not kill in the rr problem.  To clean
	 this up, we execute the transfer function on the lr_in
	 set and then use that to knock bits out of ur_out.  */
      bitmap_ior_and_compl (tmp, bb_info->gen, bb_lr_info->in, 
			    bb_info->kill);
      bitmap_and_into (bb_info->out, tmp);
#endif
    }
  
#ifdef STACK_REGS
  BITMAP_FREE (problem_data->stack_regs);
#endif
  BITMAP_FREE (tmp);
}


/* Confluence function that ignores fake edges.  */

static void
df_urec_confluence_n (struct dataflow *dflow, edge e)
{
  bitmap op1 = df_urec_get_bb_info (dflow, e->dest->index)->in;
  bitmap op2 = df_urec_get_bb_info (dflow, e->src->index)->out;
 
  if (e->flags & EDGE_FAKE) 
    return;

  bitmap_ior_into (op1, op2);
} 


/* Transfer function.  */

static bool
df_urec_transfer_function (struct dataflow *dflow, int bb_index)
{
  struct df_urec_bb_info *bb_info = df_urec_get_bb_info (dflow, bb_index);
  bitmap in = bb_info->in;
  bitmap out = bb_info->out;
  bitmap gen = bb_info->gen;
  bitmap kill = bb_info->kill;

  return bitmap_ior_and_compl (out, gen, in, kill);
}


/* Free all storage associated with the problem.  */

static void
df_urec_free (struct dataflow *dflow)
{
  if (dflow->block_info)
    {
      unsigned int i;
      
      for (i = 0; i < dflow->block_info_size; i++)
	{
	  struct df_urec_bb_info *bb_info = df_urec_get_bb_info (dflow, i);
	  if (bb_info)
	    {
	      BITMAP_FREE (bb_info->gen);
	      BITMAP_FREE (bb_info->kill);
	      BITMAP_FREE (bb_info->in);
	      BITMAP_FREE (bb_info->out);
	      BITMAP_FREE (bb_info->earlyclobber);
	    }
	}
      
      free_alloc_pool (dflow->block_pool);
      
      dflow->block_info_size = 0;
      free (dflow->block_info);
      free (dflow->problem_data);
    }
  free (dflow);
}


/* Debugging info.  */

static void
df_urec_dump (struct dataflow *dflow, FILE *file)
{
  basic_block bb;
  
  if (!dflow->block_info) 
    return;

  fprintf (file, "Undefined regs:\n");
 
  FOR_ALL_BB (bb)
    {
      struct df_urec_bb_info *bb_info = df_urec_get_bb_info (dflow, bb->index);
      df_print_bb_index (bb, file);
      
      if (!bb_info->in)
	continue;
      
      fprintf (file, "  in  \t");
      dump_bitmap (file, bb_info->in);
      fprintf (file, "  gen \t");
      dump_bitmap (file, bb_info->gen);
      fprintf (file, "  kill\t");
      dump_bitmap (file, bb_info->kill);
      fprintf (file, "  ec\t");
      dump_bitmap (file, bb_info->earlyclobber);
      fprintf (file, "  out \t");
      dump_bitmap (file, bb_info->out);
    }
}

/* All of the information associated with every instance of the problem.  */

static struct df_problem problem_UREC =
{
  DF_UREC,                    /* Problem id.  */
  DF_FORWARD,                 /* Direction.  */
  df_urec_alloc,              /* Allocate the problem specific data.  */
  NULL,                       /* Reset global information.  */
  df_urec_free_bb_info,       /* Free basic block info.  */
  df_urec_local_compute,      /* Local compute function.  */
  df_urec_init,               /* Init the solution specific data.  */
  df_iterative_dataflow,      /* Iterative solver.  */
  NULL,                       /* Confluence operator 0.  */ 
  df_urec_confluence_n,       /* Confluence operator n.  */ 
  df_urec_transfer_function,  /* Transfer function.  */
  df_urec_local_finalize,     /* Finalize function.  */
  df_urec_free,               /* Free all of the problem information.  */
  df_urec_dump,               /* Debugging.  */
  df_lr_add_problem,          /* Dependent problem.  */
  0                           /* Changeable flags.  */
};


/* Create a new DATAFLOW instance and add it to an existing instance
   of DF.  The returned structure is what is used to get at the
   solution.  */

struct dataflow *
df_urec_add_problem (struct df *df, int flags)
{
  return df_add_problem (df, &problem_UREC, flags);
}



/*----------------------------------------------------------------------------
   CREATE DEF_USE (DU) and / or USE_DEF (UD) CHAINS

   Link either the defs to the uses and / or the uses to the defs.

   These problems are set up like the other dataflow problems so that
   they nicely fit into the framework.  They are much simpler and only
   involve a single traversal of instructions and an examination of
   the reaching defs information (the dependent problem).
----------------------------------------------------------------------------*/

/* Create def-use or use-def chains.  */

static void  
df_chain_alloc (struct dataflow *dflow, 
		bitmap blocks_to_rescan ATTRIBUTE_UNUSED,
		bitmap all_blocks ATTRIBUTE_UNUSED)

{
  struct df *df = dflow->df;
  unsigned int i;

  /* Wholesale destruction of the old chains.  */ 
  if (dflow->block_pool)
    free_alloc_pool (dflow->block_pool);

  dflow->block_pool = create_alloc_pool ("df_chain_chain_block pool", 
					 sizeof (struct df_link), 100);

  if (dflow->flags & DF_DU_CHAIN)
    {
      if (!df->def_info.refs_organized)
	df_reorganize_refs (&df->def_info);
      
      /* Clear out the pointers from the refs.  */
      for (i = 0; i < DF_DEFS_SIZE (df); i++)
	{
	  struct df_ref *ref = df->def_info.refs[i];
	  DF_REF_CHAIN (ref) = NULL;
	}
    }
  
  if (dflow->flags & DF_UD_CHAIN)
    {
      if (!df->use_info.refs_organized)
	df_reorganize_refs (&df->use_info);
      for (i = 0; i < DF_USES_SIZE (df); i++)
	{
	  struct df_ref *ref = df->use_info.refs[i];
	  DF_REF_CHAIN (ref) = NULL;
	}
    }
}


/* Reset all def_use and use_def chains in INSN.  */

static void 
df_chain_insn_reset (struct dataflow *dflow, rtx insn)
{
  struct df *df = dflow->df;
  unsigned int uid = INSN_UID (insn);
  struct df_insn_info *insn_info = NULL;
  struct df_ref *ref;

  if (uid < df->insns_size)
    insn_info = DF_INSN_UID_GET (df, uid);

  if (insn_info)
    {
      if (dflow->flags & DF_DU_CHAIN)
	{
	  ref = insn_info->defs;
	  while (ref)
	    {
	      ref->chain = NULL;
	      ref = ref->next_ref;
	    }
	}

      if (dflow->flags & DF_UD_CHAIN)
	{
	  ref = insn_info->uses;
	  while (ref) 
	    {
	      ref->chain = NULL;
	      ref = ref->next_ref;
	    }
	}
    }
}


/* Reset all def_use and use_def chains in basic block.  */

static void 
df_chain_bb_reset (struct dataflow *dflow, unsigned int bb_index)
{
  struct df *df = dflow->df; 
  rtx insn;
  basic_block bb = BASIC_BLOCK (bb_index);

  /* Some one deleted the basic block out from under us.  */
  if (!bb)
    return;

  FOR_BB_INSNS (bb, insn)
    {
      if (INSN_P (insn))
	{
	  /* Record defs within INSN.  */
	  df_chain_insn_reset (dflow, insn);
	}
    }
  
  /* Get rid of any chains in artificial uses or defs.  */
  if (dflow->flags & DF_DU_CHAIN)
    {
      struct df_ref *def;
      def = df_get_artificial_defs (df, bb_index);
      while (def)
	{
	  def->chain = NULL;
	  def = def->next_ref;
	}
    }

  if (dflow->flags & DF_UD_CHAIN)
    {
      struct df_ref *use;
      use = df_get_artificial_uses (df, bb_index);
      while (use)
	{
	  use->chain = NULL;
	  use = use->next_ref;
	}
    }
}


/* Reset all of the chains when the set of basic blocks changes.  */


static void
df_chain_reset (struct dataflow *dflow, bitmap blocks_to_clear)
{
  bitmap_iterator bi;
  unsigned int bb_index;
  
  EXECUTE_IF_SET_IN_BITMAP (blocks_to_clear, 0, bb_index, bi)
    {
      df_chain_bb_reset (dflow, bb_index);
    }

  free_alloc_pool (dflow->block_pool);
  dflow->block_pool = NULL;
}


/* Create the chains for a list of USEs.  */

static void
df_chain_create_bb_process_use (struct dataflow *dflow, 
				bitmap local_rd,
				struct df_ref *use,
				enum df_ref_flags top_flag)
{
  struct df *df = dflow->df;
  bitmap_iterator bi;
  unsigned int def_index;
  
  while (use)
    {
      /* Do not want to go through this for an uninitialized var.  */
      unsigned int uregno = DF_REF_REGNO (use);
      int count = DF_REG_DEF_GET (df, uregno)->n_refs;
      if (count)
	{
	  if (top_flag == (DF_REF_FLAGS (use) & DF_REF_AT_TOP))
	    {
	      unsigned int first_index = DF_REG_DEF_GET (df, uregno)->begin;
	      unsigned int last_index = first_index + count - 1;
	      
	      EXECUTE_IF_SET_IN_BITMAP (local_rd, first_index, def_index, bi)
		{
		  struct df_ref *def;
		  if (def_index > last_index) 
		    break;
		  
		  def = DF_DEFS_GET (df, def_index);
		  if (dflow->flags & DF_DU_CHAIN)
		    df_chain_create (dflow, def, use);
		  if (dflow->flags & DF_UD_CHAIN)
		    df_chain_create (dflow, use, def);
		}
	    }
	}
      use = use->next_ref;
    }
}

/* Reset the storage pool that the def-use or use-def chains have been
   allocated in. We do not need to re adjust the pointers in the refs,
   these have already been clean out.*/

/* Create chains from reaching defs bitmaps for basic block BB.  */
static void
df_chain_create_bb (struct dataflow *dflow, 
		    struct dataflow *rd_dflow,
		    unsigned int bb_index)
{
  basic_block bb = BASIC_BLOCK (bb_index);
  struct df_rd_bb_info *bb_info = df_rd_get_bb_info (rd_dflow, bb_index);
  rtx insn;
  bitmap cpy = BITMAP_ALLOC (NULL);
  struct df *df = dflow->df;
  struct df_ref *def;

  bitmap_copy (cpy, bb_info->in);

  /* Since we are going forwards, process the artificial uses first
     then the artificial defs second.  */

#ifdef EH_USES
  /* Create the chains for the artificial uses from the EH_USES at the
     beginning of the block.  */
  df_chain_create_bb_process_use (dflow, cpy,
				  df_get_artificial_uses (df, bb->index), 
				  DF_REF_AT_TOP);
#endif

  for (def = df_get_artificial_defs (df, bb_index); def; def = def->next_ref)
    if (DF_REF_FLAGS (def) & DF_REF_AT_TOP)
      {
	unsigned int dregno = DF_REF_REGNO (def);
	if (!(DF_REF_FLAGS (def) & DF_REF_PARTIAL))
	  bitmap_clear_range (cpy, 
			      DF_REG_DEF_GET (df, dregno)->begin, 
			      DF_REG_DEF_GET (df, dregno)->n_refs);
	bitmap_set_bit (cpy, DF_REF_ID (def));
      }
  
  /* Process the regular instructions next.  */
  FOR_BB_INSNS (bb, insn)
    {
      struct df_ref *def;
      unsigned int uid = INSN_UID (insn);

      if (!INSN_P (insn))
	continue;

      /* Now scan the uses and link them up with the defs that remain
	 in the cpy vector.  */
      
      df_chain_create_bb_process_use (dflow, cpy,
				     DF_INSN_UID_USES (df, uid), 0);

      /* Since we are going forwards, process the defs second.  This
         pass only changes the bits in cpy.  */
      for (def = DF_INSN_UID_DEFS (df, uid); def; def = def->next_ref)
	{
	  unsigned int dregno = DF_REF_REGNO (def);
	  if (!(DF_REF_FLAGS (def) & DF_REF_PARTIAL))
	    bitmap_clear_range (cpy, 
				DF_REG_DEF_GET (df, dregno)->begin, 
				DF_REG_DEF_GET (df, dregno)->n_refs);
	  if (!(DF_REF_FLAGS (def) 
		 & (DF_REF_MUST_CLOBBER | DF_REF_MAY_CLOBBER)))
	    bitmap_set_bit (cpy, DF_REF_ID (def));
	}
    }

  /* Create the chains for the artificial uses of the hard registers
     at the end of the block.  */
  df_chain_create_bb_process_use (dflow, cpy,
				  df_get_artificial_uses (df, bb->index), 0);
}

/* Create def-use chains from reaching use bitmaps for basic blocks
   in BLOCKS.  */

static void
df_chain_finalize (struct dataflow *dflow, bitmap all_blocks)
{
  unsigned int bb_index;
  bitmap_iterator bi;
  struct df *df = dflow->df;
  struct dataflow *rd_dflow = df->problems_by_index [DF_RD];
  
  EXECUTE_IF_SET_IN_BITMAP (all_blocks, 0, bb_index, bi)
    {
      df_chain_create_bb (dflow, rd_dflow, bb_index);
    }
}


/* Free all storage associated with the problem.  */

static void
df_chain_free (struct dataflow *dflow)
{
  free_alloc_pool (dflow->block_pool);
  free (dflow);
}


/* Debugging info.  */

static void
df_chains_dump (struct dataflow *dflow, FILE *file)
{
  struct df *df = dflow->df;
  unsigned int j;

  if (dflow->flags & DF_DU_CHAIN)
    {
      fprintf (file, "Def-use chains:\n");
      for (j = 0; j < df->def_info.bitmap_size; j++)
	{
	  struct df_ref *def = DF_DEFS_GET (df, j);
	  if (def)
	    {
	      fprintf (file, "d%d bb %d luid %d insn %d reg %d ",
		       j, DF_REF_BBNO (def),
		       DF_REF_INSN (def) ? 
		       DF_INSN_LUID (df, DF_REF_INSN (def)):
		       -1,
		       DF_REF_INSN (def) ? DF_REF_INSN_UID (def) : -1,
		       DF_REF_REGNO (def));
	      if (def->flags & DF_REF_READ_WRITE)
		fprintf (file, "read/write ");
	      df_chain_dump (DF_REF_CHAIN (def), file);
	      fprintf (file, "\n");
	    }
	}
    }

  if (dflow->flags & DF_UD_CHAIN)
    {
      fprintf (file, "Use-def chains:\n");
      for (j = 0; j < df->use_info.bitmap_size; j++)
	{
	  struct df_ref *use = DF_USES_GET (df, j);
	  if (use)
	    {
	      fprintf (file, "u%d bb %d luid %d insn %d reg %d ",
		       j, DF_REF_BBNO (use),
		       DF_REF_INSN (use) ? 
		       DF_INSN_LUID (df, DF_REF_INSN (use))
		       : -1,
		       DF_REF_INSN (DF_USES_GET (df, j)) ?
		       DF_REF_INSN_UID (DF_USES_GET (df,j))
		       : -1,
		       DF_REF_REGNO (use));
	      if (use->flags & DF_REF_READ_WRITE)
		fprintf (file, "read/write ");
	      if (use->flags & DF_REF_STRIPPED)
		fprintf (file, "stripped ");
	      if (use->flags & DF_REF_IN_NOTE)
		fprintf (file, "note ");
	      df_chain_dump (DF_REF_CHAIN (use), file);
	      fprintf (file, "\n");
	    }
	}
    }
}


static struct df_problem problem_CHAIN =
{
  DF_CHAIN,                   /* Problem id.  */
  DF_NONE,                    /* Direction.  */
  df_chain_alloc,             /* Allocate the problem specific data.  */
  df_chain_reset,             /* Reset global information.  */
  NULL,                       /* Free basic block info.  */
  NULL,                       /* Local compute function.  */
  NULL,                       /* Init the solution specific data.  */
  NULL,                       /* Iterative solver.  */
  NULL,                       /* Confluence operator 0.  */ 
  NULL,                       /* Confluence operator n.  */ 
  NULL,                       /* Transfer function.  */
  df_chain_finalize,          /* Finalize function.  */
  df_chain_free,              /* Free all of the problem information.  */
  df_chains_dump,             /* Debugging.  */
  df_rd_add_problem,          /* Dependent problem.  */
  0                           /* Changeable flags.  */
};


/* Create a new DATAFLOW instance and add it to an existing instance
   of DF.  The returned structure is what is used to get at the
   solution.  */

struct dataflow *
df_chain_add_problem (struct df *df, int flags)
{
  return df_add_problem (df, &problem_CHAIN, flags);
}


/*----------------------------------------------------------------------------
   REGISTER INFORMATION

   This pass properly computes REG_DEAD and REG_UNUSED notes.

   If the DF_RI_LIFE flag is set the following vectors containing
   information about register usage are properly set: REG_N_REFS,
   REG_N_DEATHS, REG_N_SETS, REG_LIVE_LENGTH, REG_N_CALLS_CROSSED,
   REG_N_THROWING_CALLS_CROSSED and REG_BASIC_BLOCK.

   ----------------------------------------------------------------------------*/

#ifdef REG_DEAD_DEBUGGING
static void 
print_note (char *prefix, rtx insn, rtx note)
{
  fprintf (stderr, "%s %d ", prefix, INSN_UID (insn));
  print_rtl (stderr, note);
  fprintf (stderr, "\n");
}
#endif

/* Allocate the lifetime information.  */

static void 
df_ri_alloc (struct dataflow *dflow, 
	     bitmap blocks_to_rescan ATTRIBUTE_UNUSED,
	     bitmap all_blocks ATTRIBUTE_UNUSED)
{
  int i;
  struct df *df = dflow->df;

  if (dflow->flags & DF_RI_LIFE)
    {
      max_regno = max_reg_num ();
      allocate_reg_info (max_regno, FALSE, FALSE);
      
      /* Reset all the data we'll collect.  */
      for (i = 0; i < max_regno; i++)
	{
	  REG_N_SETS (i) = DF_REG_DEF_COUNT (df, i);
	  REG_N_REFS (i) = DF_REG_USE_COUNT (df, i) + REG_N_SETS (i);
	  REG_N_DEATHS (i) = 0;
	  REG_N_CALLS_CROSSED (i) = 0;
	  REG_N_THROWING_CALLS_CROSSED (i) = 0;
	  REG_LIVE_LENGTH (i) = 0;
	  REG_FREQ (i) = 0;
	  REG_BASIC_BLOCK (i) = REG_BLOCK_UNKNOWN;
	}
    }
}


/* After reg-stack, the x86 floating point stack regs are difficult to
   analyze because of all of the pushes, pops and rotations.  Thus, we
   just leave the notes alone. */

static inline bool 
df_ignore_stack_reg (int regno ATTRIBUTE_UNUSED)
{
#ifdef STACK_REGS
  return (regstack_completed
	  && IN_RANGE (regno, FIRST_STACK_REG, LAST_STACK_REG));
#else
  return false;
#endif
}


/* Remove all of the REG_DEAD or REG_UNUSED notes from INSN.  */

static void
df_kill_notes (rtx insn, int flags)
{
  rtx *pprev = &REG_NOTES (insn);
  rtx link = *pprev;
  
  while (link)
    {
      switch (REG_NOTE_KIND (link))
	{
	case REG_DEAD:
	  if (flags & DF_RI_LIFE)
	    if (df_ignore_stack_reg (REGNO (XEXP (link, 0))))
	      REG_N_DEATHS (REGNO (XEXP (link, 0)))++;

	  /* Fallthru */
	case REG_UNUSED:
	  if (!df_ignore_stack_reg (REGNO (XEXP (link, 0))))
	    {
	      rtx next = XEXP (link, 1);
#ifdef REG_DEAD_DEBUGGING
	      print_note ("deleting: ", insn, link);
#endif
	      free_EXPR_LIST_node (link);
	      *pprev = link = next;
	    }
	  break;
	  
	default:
	  pprev = &XEXP (link, 1);
	  link = *pprev;
	  break;
	}
    }
}


/* Set the REG_UNUSED notes for the multiword hardreg defs in INSN
   based on the bits in LIVE.  Do not generate notes for registers in
   artificial uses.  DO_NOT_GEN is updated so that REG_DEAD notes are
   not generated if the reg is both read and written by the
   instruction.
*/

static void
df_set_unused_notes_for_mw (rtx insn, struct df_mw_hardreg *mws,
			    bitmap live, bitmap do_not_gen, 
			    bitmap artificial_uses, int flags)
{
  bool all_dead = true;
  struct df_link *regs = mws->regs;
  unsigned int regno = DF_REF_REGNO (regs->ref);
  
#ifdef REG_DEAD_DEBUGGING
  fprintf (stderr, "mw unused looking at %d\n", DF_REF_REGNO (regs->ref));
  df_ref_debug (regs->ref, stderr);
#endif
  while (regs)
    {
      unsigned int regno = DF_REF_REGNO (regs->ref);
      if ((bitmap_bit_p (live, regno))
	  || bitmap_bit_p (artificial_uses, regno))
	{
	  all_dead = false;
	  break;
	}
      regs = regs->next;
    }
  
  if (all_dead)
    {
      struct df_link *regs = mws->regs;
      rtx note = alloc_EXPR_LIST (REG_UNUSED, *DF_REF_LOC (regs->ref), 
				  REG_NOTES (insn));
      REG_NOTES (insn) = note;
#ifdef REG_DEAD_DEBUGGING
      print_note ("adding 1: ", insn, note);
#endif
      bitmap_set_bit (do_not_gen, regno);
      /* Only do this if the value is totally dead.  */
      if (flags & DF_RI_LIFE)
	{
	  REG_N_DEATHS (regno) ++;
	  REG_LIVE_LENGTH (regno)++;
	}
    }
  else
    {
      struct df_link *regs = mws->regs;
      while (regs)
	{
	  struct df_ref *ref = regs->ref;
	  
	  regno = DF_REF_REGNO (ref);
	  if ((!bitmap_bit_p (live, regno))
	      && (!bitmap_bit_p (artificial_uses, regno)))
	    {
	      rtx note = alloc_EXPR_LIST (REG_UNUSED, regno_reg_rtx[regno], 
					  REG_NOTES (insn));
	      REG_NOTES (insn) = note;
#ifdef REG_DEAD_DEBUGGING
	      print_note ("adding 2: ", insn, note);
#endif
	    }
	  bitmap_set_bit (do_not_gen, regno);
	  regs = regs->next;
	}
    }
}


/* Set the REG_DEAD notes for the multiword hardreg use in INSN based
   on the bits in LIVE.  DO_NOT_GEN is used to keep REG_DEAD notes
   from being set if the instruction both reads and writes the
   register.  */

static void
df_set_dead_notes_for_mw (rtx insn, struct df_mw_hardreg *mws,
			  bitmap live, bitmap do_not_gen,
			  bitmap artificial_uses, int flags)
{
  bool all_dead = true;
  struct df_link *regs = mws->regs;
  unsigned int regno = DF_REF_REGNO (regs->ref);
  
#ifdef REG_DEAD_DEBUGGING
  fprintf (stderr, "mw looking at %d\n", DF_REF_REGNO (regs->ref));
  df_ref_debug (regs->ref, stderr);
#endif
  while (regs)
    {
      unsigned int regno = DF_REF_REGNO (regs->ref);
      if ((bitmap_bit_p (live, regno))
	  || bitmap_bit_p (artificial_uses, regno))
	{
	  all_dead = false;
	  break;
	}
      regs = regs->next;
    }
  
  if (all_dead)
    {
      if (!bitmap_bit_p (do_not_gen, regno))
	{
	  /* Add a dead note for the entire multi word register.  */
	  struct df_link *regs = mws->regs;
	  rtx note = alloc_EXPR_LIST (REG_DEAD, *DF_REF_LOC (regs->ref), 
				      REG_NOTES (insn));
	  REG_NOTES (insn) = note;
#ifdef REG_DEAD_DEBUGGING
	  print_note ("adding 1: ", insn, note);
#endif

	  if (flags & DF_RI_LIFE)
	    {
	      struct df_link *regs = mws->regs;
	      while (regs)
		{
		  struct df_ref *ref = regs->ref;
		  regno = DF_REF_REGNO (ref);
		  REG_N_DEATHS (regno)++;
		  regs = regs->next;
		}
	    }
	}
    }
  else
    {
      struct df_link *regs = mws->regs;
      while (regs)
	{
	  struct df_ref *ref = regs->ref;

	  regno = DF_REF_REGNO (ref);
	  if ((!bitmap_bit_p (live, regno))
	      && (!bitmap_bit_p (artificial_uses, regno))
	      && (!bitmap_bit_p (do_not_gen, regno)))
	    {
	      rtx note = alloc_EXPR_LIST (REG_DEAD, regno_reg_rtx[regno], 
					  REG_NOTES (insn));
	      REG_NOTES (insn) = note;
	      if (flags & DF_RI_LIFE)
		REG_N_DEATHS (regno)++;
#ifdef REG_DEAD_DEBUGGING
	      print_note ("adding 2: ", insn, note);
#endif
	    }

	  regs = regs->next;
	}
    }
}


/* Create a REG_UNUSED note if necessary for DEF in INSN updating LIVE
   and DO_NOT_GEN.  Do not generate notes for registers in artificial
   uses.  */

static void
df_create_unused_note (basic_block bb, rtx insn, struct df_ref *def, 
		       bitmap live, bitmap do_not_gen, bitmap artificial_uses, 
		       bitmap local_live, bitmap local_processed, 
		       int flags, int luid)
{
  unsigned int dregno = DF_REF_REGNO (def);
  
#ifdef REG_DEAD_DEBUGGING
  fprintf (stderr, "  regular looking at def ");
  df_ref_debug (def, stderr);
#endif

  if (bitmap_bit_p (live, dregno))
    {
      if (flags & DF_RI_LIFE)
	{
	  /* If we have seen this regno, then it has already been
	     processed correctly with the per insn increment.  If we
	     have not seen it we need to add the length from here to
	     the end of the block to the live length.  */
	  if (bitmap_bit_p (local_processed, dregno))
	    {
	      if (!(DF_REF_FLAGS (def) & DF_REF_PARTIAL))
		bitmap_clear_bit (local_live, dregno);
	    }
	  else
	    {
	      bitmap_set_bit (local_processed, dregno);
	      REG_LIVE_LENGTH (dregno) += luid;
	    }
	}
    }
  else if ((!(DF_REF_FLAGS (def) & DF_REF_MW_HARDREG))
	    && (!bitmap_bit_p (artificial_uses, dregno)) 
	    && (!df_ignore_stack_reg (dregno)))
    {
      rtx reg = GET_CODE (*DF_REF_LOC (def)) == SUBREG ?
	SUBREG_REG (*DF_REF_LOC (def)) : *DF_REF_LOC (def);
      rtx note = alloc_EXPR_LIST (REG_UNUSED, reg, REG_NOTES (insn));
      REG_NOTES (insn) = note;
#ifdef REG_DEAD_DEBUGGING
      print_note ("adding 3: ", insn, note);
#endif
      if (flags & DF_RI_LIFE)
	{
	  REG_N_DEATHS (dregno) ++;
	  REG_LIVE_LENGTH (dregno)++;
	}
    }
  
  if ((flags & DF_RI_LIFE) && (dregno >= FIRST_PSEUDO_REGISTER))
    {
      REG_FREQ (dregno) += REG_FREQ_FROM_BB (bb);
      if (REG_BASIC_BLOCK (dregno) == REG_BLOCK_UNKNOWN)
	REG_BASIC_BLOCK (dregno) = bb->index;
      else if (REG_BASIC_BLOCK (dregno) != bb->index)
	REG_BASIC_BLOCK (dregno) = REG_BLOCK_GLOBAL;
    }

  if (!(DF_REF_FLAGS (def) & (DF_REF_MUST_CLOBBER + DF_REF_MAY_CLOBBER)))
    bitmap_set_bit (do_not_gen, dregno);
  
  /* Kill this register if it is not a subreg store.  */
  if (!(DF_REF_FLAGS (def) & DF_REF_PARTIAL))
    bitmap_clear_bit (live, dregno);
}


/* Recompute the REG_DEAD and REG_UNUSED notes and compute register
   info: lifetime, bb, and number of defs and uses for basic block
   BB.  The three bitvectors are scratch regs used here.  */

static void
df_ri_bb_compute (struct dataflow *dflow, unsigned int bb_index, 
		  bitmap live, bitmap do_not_gen, bitmap artificial_uses,
		  bitmap local_live, bitmap local_processed, bitmap setjumps_crossed)
{
  struct df *df = dflow->df;
  basic_block bb = BASIC_BLOCK (bb_index);
  rtx insn;
  struct df_ref *def;
  struct df_ref *use;
  int luid = 0;

  bitmap_copy (live, df_get_live_out (df, bb));
  bitmap_clear (artificial_uses);

  if (dflow->flags & DF_RI_LIFE)
    {
      /* Process the regs live at the end of the block.  Mark them as
	 not local to any one basic block.  */
      bitmap_iterator bi;
      unsigned int regno;
      EXECUTE_IF_SET_IN_BITMAP (live, 0, regno, bi)
	REG_BASIC_BLOCK (regno) = REG_BLOCK_GLOBAL;
    }

  /* Process the artificial defs and uses at the bottom of the block
     to begin processing.  */
  for (def = df_get_artificial_defs (df, bb_index); def; def = def->next_ref)
    if ((DF_REF_FLAGS (def) & DF_REF_AT_TOP) == 0)
      bitmap_clear_bit (live, DF_REF_REGNO (def));

  for (use = df_get_artificial_uses (df, bb_index); use; use = use->next_ref)
    if ((DF_REF_FLAGS (use) & DF_REF_AT_TOP) == 0)
      {
	unsigned int regno = DF_REF_REGNO (use);
	bitmap_set_bit (live, regno);

	/* Notes are not generated for any of the artificial registers
	   at the bottom of the block.  */
	bitmap_set_bit (artificial_uses, regno);
      }
  
  FOR_BB_INSNS_REVERSE (bb, insn)
    {
      unsigned int uid = INSN_UID (insn);
      unsigned int regno;
      bitmap_iterator bi;
      struct df_mw_hardreg *mws;
      
      if (!INSN_P (insn))
	continue;

      if (dflow->flags & DF_RI_LIFE)
	{
	  /* Increment the live_length for all of the registers that
	     are are referenced in this block and live at this
	     particular point.  */
	  bitmap_iterator bi;
	  unsigned int regno;
	  EXECUTE_IF_SET_IN_BITMAP (local_live, 0, regno, bi)
	    {
	      REG_LIVE_LENGTH (regno)++;
	    }
	  luid++;
	}

      bitmap_clear (do_not_gen);
      df_kill_notes (insn, dflow->flags);

      /* Process the defs.  */
      if (CALL_P (insn))
	{
	  if (dflow->flags & DF_RI_LIFE)
	    {
	      bool can_throw = can_throw_internal (insn); 
	      bool set_jump = (find_reg_note (insn, REG_SETJMP, NULL) != NULL);
	      EXECUTE_IF_SET_IN_BITMAP (live, 0, regno, bi)
		{
		  REG_N_CALLS_CROSSED (regno)++;
		  if (can_throw)
		    REG_N_THROWING_CALLS_CROSSED (regno)++;

		  /* We have a problem with any pseudoreg that lives
		     across the setjmp.  ANSI says that if a user
		     variable does not change in value between the
		     setjmp and the longjmp, then the longjmp
		     preserves it.  This includes longjmp from a place
		     where the pseudo appears dead.  (In principle,
		     the value still exists if it is in scope.)  If
		     the pseudo goes in a hard reg, some other value
		     may occupy that hard reg where this pseudo is
		     dead, thus clobbering the pseudo.  Conclusion:
		     such a pseudo must not go in a hard reg.  */
		  if (set_jump && regno >= FIRST_PSEUDO_REGISTER)
		    bitmap_set_bit (setjumps_crossed, regno);
		}
	    }
	  
	  /* We only care about real sets for calls.  Clobbers only
	     may clobber and cannot be depended on.  */
	  for (mws = DF_INSN_UID_MWS (df, uid); mws; mws = mws->next)
	    {
	      if ((mws->type == DF_REF_REG_DEF) 
		  && !df_ignore_stack_reg (REGNO (mws->mw_reg)))
		df_set_unused_notes_for_mw (insn, mws, live, do_not_gen, 
					    artificial_uses, dflow->flags);
	    }

	  /* All of the defs except the return value are some sort of
	     clobber.  This code is for the return.  */
	  for (def = DF_INSN_UID_DEFS (df, uid); def; def = def->next_ref)
	    if (!(DF_REF_FLAGS (def) & (DF_REF_MUST_CLOBBER | DF_REF_MAY_CLOBBER)))
	      df_create_unused_note (bb, insn, def, live, do_not_gen, 
				     artificial_uses, local_live, 
				     local_processed, dflow->flags, luid);

	}
      else
	{
	  /* Regular insn.  */
	  for (mws = DF_INSN_UID_MWS (df, uid); mws; mws = mws->next)
	    {
	      if (mws->type == DF_REF_REG_DEF)
		df_set_unused_notes_for_mw (insn, mws, live, do_not_gen, 
					    artificial_uses, dflow->flags);
	    }

	  for (def = DF_INSN_UID_DEFS (df, uid); def; def = def->next_ref)
	    df_create_unused_note (bb, insn, def, live, do_not_gen, 
				   artificial_uses, local_live, 
				   local_processed, dflow->flags, luid);
	}
      
      /* Process the uses.  */
      for (mws = DF_INSN_UID_MWS (df, uid); mws; mws = mws->next)
	{
	  if ((mws->type != DF_REF_REG_DEF)  
	      && !df_ignore_stack_reg (REGNO (mws->mw_reg)))
	    df_set_dead_notes_for_mw (insn, mws, live, do_not_gen,
				      artificial_uses, dflow->flags);
	}

      for (use = DF_INSN_UID_USES (df, uid); use; use = use->next_ref)
	{
	  unsigned int uregno = DF_REF_REGNO (use);

	  if ((dflow->flags & DF_RI_LIFE) && (uregno >= FIRST_PSEUDO_REGISTER))
	    {
	      REG_FREQ (uregno) += REG_FREQ_FROM_BB (bb);
	      if (REG_BASIC_BLOCK (uregno) == REG_BLOCK_UNKNOWN)
		REG_BASIC_BLOCK (uregno) = bb->index;
	      else if (REG_BASIC_BLOCK (uregno) != bb->index)
		REG_BASIC_BLOCK (uregno) = REG_BLOCK_GLOBAL;
	    }
	  
#ifdef REG_DEAD_DEBUGGING
	  fprintf (stderr, "  regular looking at use ");
	  df_ref_debug (use, stderr);
#endif
	  if (!bitmap_bit_p (live, uregno))
	    {
	      if ( (!(DF_REF_FLAGS (use) & DF_REF_MW_HARDREG))
		   && (!bitmap_bit_p (do_not_gen, uregno))
		   && (!bitmap_bit_p (artificial_uses, uregno))
		   && (!(DF_REF_FLAGS (use) & DF_REF_READ_WRITE))
		   && (!df_ignore_stack_reg (uregno)))
		{
		  rtx reg = GET_CODE (*DF_REF_LOC (use)) == SUBREG ?
		    SUBREG_REG (*DF_REF_LOC (use)) : *DF_REF_LOC (use);
		  rtx note = alloc_EXPR_LIST (REG_DEAD, reg, REG_NOTES (insn));
		  REG_NOTES (insn) = note;
		  if (dflow->flags & DF_RI_LIFE)
		    REG_N_DEATHS (uregno)++;

#ifdef REG_DEAD_DEBUGGING
		  print_note ("adding 4: ", insn, note);
#endif
		}
	      /* This register is now live.  */
	      bitmap_set_bit (live, uregno);

	      if (dflow->flags & DF_RI_LIFE)
		{
		  /* If we have seen this regno, then it has already
		     been processed correctly with the per insn
		     increment.  If we have not seen it we set the bit
		     so that begins to get processed locally.  Note
		     that we don't even get here if the variable was
		     live at the end of the block since just a ref
		     inside the block does not effect the
		     calculations.  */
		  REG_LIVE_LENGTH (uregno) ++;
		  bitmap_set_bit (local_live, uregno);
		  bitmap_set_bit (local_processed, uregno);
		}
	    }
	}
    }
  
  if (dflow->flags & DF_RI_LIFE)
    {
      /* Add the length of the block to all of the registers that were
	 not referenced, but still live in this block.  */
      bitmap_iterator bi;
      unsigned int regno;
      bitmap_and_compl_into (live, local_processed);
      EXECUTE_IF_SET_IN_BITMAP (live, 0, regno, bi)
	{
	  REG_LIVE_LENGTH (regno) += luid;
	}
      bitmap_clear (local_processed);
      bitmap_clear (local_live);
    }
}


/* Compute register info: lifetime, bb, and number of defs and uses.  */
static void
df_ri_compute (struct dataflow *dflow, bitmap all_blocks ATTRIBUTE_UNUSED, 
	       bitmap blocks_to_scan)
{
  unsigned int bb_index;
  bitmap_iterator bi;
  bitmap live = BITMAP_ALLOC (NULL);
  bitmap do_not_gen = BITMAP_ALLOC (NULL);
  bitmap artificial_uses = BITMAP_ALLOC (NULL);
  bitmap local_live = NULL;
  bitmap local_processed = NULL;
  bitmap setjumps_crossed = NULL;

  if (dflow->flags & DF_RI_LIFE)
    {
      local_live = BITMAP_ALLOC (NULL);
      local_processed = BITMAP_ALLOC (NULL);
      setjumps_crossed = BITMAP_ALLOC (NULL);
    }


#ifdef REG_DEAD_DEBUGGING
  df_lr_dump (dflow->df->problems_by_index [DF_LR], stderr);
  print_rtl_with_bb (stderr, get_insns());
#endif

  EXECUTE_IF_SET_IN_BITMAP (blocks_to_scan, 0, bb_index, bi)
  {
    df_ri_bb_compute (dflow, bb_index, live, do_not_gen, artificial_uses,
		      local_live, local_processed, setjumps_crossed);
  }

  BITMAP_FREE (live);
  BITMAP_FREE (do_not_gen);
  BITMAP_FREE (artificial_uses);
  if (dflow->flags & DF_RI_LIFE)
    {
      bitmap_iterator bi;
      unsigned int regno;
      /* See the setjump comment in df_ri_bb_compute.  */
      EXECUTE_IF_SET_IN_BITMAP (setjumps_crossed, 0, regno, bi)
	{
	  REG_BASIC_BLOCK (regno) = REG_BLOCK_UNKNOWN;
	  REG_LIVE_LENGTH (regno) = -1;
	}	  

      BITMAP_FREE (local_live);
      BITMAP_FREE (local_processed);
      BITMAP_FREE (setjumps_crossed);
    }
}


/* Free all storage associated with the problem.  */

static void
df_ri_free (struct dataflow *dflow)
{
  free (dflow->problem_data);
  free (dflow);
}


/* Debugging info.  */

static void
df_ri_dump (struct dataflow *dflow, FILE *file)
{
  print_rtl_with_bb (file, get_insns ());

  if (dflow->flags & DF_RI_LIFE)
    {
      fprintf (file, "Register info:\n");
      dump_flow_info (file, -1);
    }
}

/* All of the information associated every instance of the problem.  */

static struct df_problem problem_RI =
{
  DF_RI,                      /* Problem id.  */
  DF_NONE,                    /* Direction.  */
  df_ri_alloc,                /* Allocate the problem specific data.  */
  NULL,                       /* Reset global information.  */
  NULL,                       /* Free basic block info.  */
  df_ri_compute,              /* Local compute function.  */
  NULL,                       /* Init the solution specific data.  */
  NULL,                       /* Iterative solver.  */
  NULL,                       /* Confluence operator 0.  */ 
  NULL,                       /* Confluence operator n.  */ 
  NULL,                       /* Transfer function.  */
  NULL,                       /* Finalize function.  */
  df_ri_free,                 /* Free all of the problem information.  */
  df_ri_dump,                 /* Debugging.  */

  /* Technically this is only dependent on the live registers problem
     but it will produce information if built one of uninitialized
     register problems (UR, UREC) is also run.  */
  df_lr_add_problem,          /* Dependent problem.  */
  0                           /* Changeable flags.  */
};


/* Create a new DATAFLOW instance and add it to an existing instance
   of DF.  The returned structure is what is used to get at the
   solution.  */

struct dataflow * 
df_ri_add_problem (struct df *df, int flags)
{
  return df_add_problem (df, &problem_RI, flags);
}
