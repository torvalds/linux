/* Procedure integration for GCC.
   Copyright (C) 1988, 1991, 1993, 1994, 1995, 1996, 1997, 1998, 1999,
   2000, 2001, 2002, 2003, 2004, 2005 Free Software Foundation, Inc.
   Contributed by Michael Tiemann (tiemann@cygnus.com)

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
#include "tree.h"
#include "tm_p.h"
#include "regs.h"
#include "flags.h"
#include "debug.h"
#include "insn-config.h"
#include "expr.h"
#include "output.h"
#include "recog.h"
#include "integrate.h"
#include "real.h"
#include "except.h"
#include "function.h"
#include "toplev.h"
#include "intl.h"
#include "params.h"
#include "ggc.h"
#include "target.h"
#include "langhooks.h"
#include "tree-pass.h"

/* Round to the next highest integer that meets the alignment.  */
#define CEIL_ROUND(VALUE,ALIGN)	(((VALUE) + (ALIGN) - 1) & ~((ALIGN)- 1))


/* Private type used by {get/has}_hard_reg_initial_val.  */
typedef struct initial_value_pair GTY(()) {
  rtx hard_reg;
  rtx pseudo;
} initial_value_pair;
typedef struct initial_value_struct GTY(()) {
  int num_entries;
  int max_entries;
  initial_value_pair * GTY ((length ("%h.num_entries"))) entries;
} initial_value_struct;

static void set_block_origin_self (tree);
static void set_block_abstract_flags (tree, int);


/* Return false if the function FNDECL cannot be inlined on account of its
   attributes, true otherwise.  */
bool
function_attribute_inlinable_p (tree fndecl)
{
  if (targetm.attribute_table)
    {
      tree a;

      for (a = DECL_ATTRIBUTES (fndecl); a; a = TREE_CHAIN (a))
	{
	  tree name = TREE_PURPOSE (a);
	  int i;

	  for (i = 0; targetm.attribute_table[i].name != NULL; i++)
	    if (is_attribute_p (targetm.attribute_table[i].name, name))
	      return targetm.function_attribute_inlinable_p (fndecl);
	}
    }

  return true;
}

/* Given a pointer to some BLOCK node, if the BLOCK_ABSTRACT_ORIGIN for the
   given BLOCK node is NULL, set the BLOCK_ABSTRACT_ORIGIN for the node so
   that it points to the node itself, thus indicating that the node is its
   own (abstract) origin.  Additionally, if the BLOCK_ABSTRACT_ORIGIN for
   the given node is NULL, recursively descend the decl/block tree which
   it is the root of, and for each other ..._DECL or BLOCK node contained
   therein whose DECL_ABSTRACT_ORIGINs or BLOCK_ABSTRACT_ORIGINs are also
   still NULL, set *their* DECL_ABSTRACT_ORIGIN or BLOCK_ABSTRACT_ORIGIN
   values to point to themselves.  */

static void
set_block_origin_self (tree stmt)
{
  if (BLOCK_ABSTRACT_ORIGIN (stmt) == NULL_TREE)
    {
      BLOCK_ABSTRACT_ORIGIN (stmt) = stmt;

      {
	tree local_decl;

	for (local_decl = BLOCK_VARS (stmt);
	     local_decl != NULL_TREE;
	     local_decl = TREE_CHAIN (local_decl))
	  set_decl_origin_self (local_decl);	/* Potential recursion.  */
      }

      {
	tree subblock;

	for (subblock = BLOCK_SUBBLOCKS (stmt);
	     subblock != NULL_TREE;
	     subblock = BLOCK_CHAIN (subblock))
	  set_block_origin_self (subblock);	/* Recurse.  */
      }
    }
}

/* Given a pointer to some ..._DECL node, if the DECL_ABSTRACT_ORIGIN for
   the given ..._DECL node is NULL, set the DECL_ABSTRACT_ORIGIN for the
   node to so that it points to the node itself, thus indicating that the
   node represents its own (abstract) origin.  Additionally, if the
   DECL_ABSTRACT_ORIGIN for the given node is NULL, recursively descend
   the decl/block tree of which the given node is the root of, and for
   each other ..._DECL or BLOCK node contained therein whose
   DECL_ABSTRACT_ORIGINs or BLOCK_ABSTRACT_ORIGINs are also still NULL,
   set *their* DECL_ABSTRACT_ORIGIN or BLOCK_ABSTRACT_ORIGIN values to
   point to themselves.  */

void
set_decl_origin_self (tree decl)
{
  if (DECL_ABSTRACT_ORIGIN (decl) == NULL_TREE)
    {
      DECL_ABSTRACT_ORIGIN (decl) = decl;
      if (TREE_CODE (decl) == FUNCTION_DECL)
	{
	  tree arg;

	  for (arg = DECL_ARGUMENTS (decl); arg; arg = TREE_CHAIN (arg))
	    DECL_ABSTRACT_ORIGIN (arg) = arg;
	  if (DECL_INITIAL (decl) != NULL_TREE
	      && DECL_INITIAL (decl) != error_mark_node)
	    set_block_origin_self (DECL_INITIAL (decl));
	}
    }
}

/* Given a pointer to some BLOCK node, and a boolean value to set the
   "abstract" flags to, set that value into the BLOCK_ABSTRACT flag for
   the given block, and for all local decls and all local sub-blocks
   (recursively) which are contained therein.  */

static void
set_block_abstract_flags (tree stmt, int setting)
{
  tree local_decl;
  tree subblock;

  BLOCK_ABSTRACT (stmt) = setting;

  for (local_decl = BLOCK_VARS (stmt);
       local_decl != NULL_TREE;
       local_decl = TREE_CHAIN (local_decl))
    set_decl_abstract_flags (local_decl, setting);

  for (subblock = BLOCK_SUBBLOCKS (stmt);
       subblock != NULL_TREE;
       subblock = BLOCK_CHAIN (subblock))
    set_block_abstract_flags (subblock, setting);
}

/* Given a pointer to some ..._DECL node, and a boolean value to set the
   "abstract" flags to, set that value into the DECL_ABSTRACT flag for the
   given decl, and (in the case where the decl is a FUNCTION_DECL) also
   set the abstract flags for all of the parameters, local vars, local
   blocks and sub-blocks (recursively) to the same setting.  */

void
set_decl_abstract_flags (tree decl, int setting)
{
  DECL_ABSTRACT (decl) = setting;
  if (TREE_CODE (decl) == FUNCTION_DECL)
    {
      tree arg;

      for (arg = DECL_ARGUMENTS (decl); arg; arg = TREE_CHAIN (arg))
	DECL_ABSTRACT (arg) = setting;
      if (DECL_INITIAL (decl) != NULL_TREE
	  && DECL_INITIAL (decl) != error_mark_node)
	set_block_abstract_flags (DECL_INITIAL (decl), setting);
    }
}

/* Functions to keep track of the values hard regs had at the start of
   the function.  */

rtx
get_hard_reg_initial_reg (struct function *fun, rtx reg)
{
  struct initial_value_struct *ivs = fun->hard_reg_initial_vals;
  int i;

  if (ivs == 0)
    return NULL_RTX;

  for (i = 0; i < ivs->num_entries; i++)
    if (rtx_equal_p (ivs->entries[i].pseudo, reg))
      return ivs->entries[i].hard_reg;

  return NULL_RTX;
}

/* Make sure that there's a pseudo register of mode MODE that stores the
   initial value of hard register REGNO.  Return an rtx for such a pseudo.  */

rtx
get_hard_reg_initial_val (enum machine_mode mode, unsigned int regno)
{
  struct initial_value_struct *ivs;
  rtx rv;

  rv = has_hard_reg_initial_val (mode, regno);
  if (rv)
    return rv;

  ivs = cfun->hard_reg_initial_vals;
  if (ivs == 0)
    {
      ivs = ggc_alloc (sizeof (initial_value_struct));
      ivs->num_entries = 0;
      ivs->max_entries = 5;
      ivs->entries = ggc_alloc (5 * sizeof (initial_value_pair));
      cfun->hard_reg_initial_vals = ivs;
    }

  if (ivs->num_entries >= ivs->max_entries)
    {
      ivs->max_entries += 5;
      ivs->entries = ggc_realloc (ivs->entries,
				  ivs->max_entries
				  * sizeof (initial_value_pair));
    }

  ivs->entries[ivs->num_entries].hard_reg = gen_rtx_REG (mode, regno);
  ivs->entries[ivs->num_entries].pseudo = gen_reg_rtx (mode);

  return ivs->entries[ivs->num_entries++].pseudo;
}

/* See if get_hard_reg_initial_val has been used to create a pseudo
   for the initial value of hard register REGNO in mode MODE.  Return
   the associated pseudo if so, otherwise return NULL.  */

rtx
has_hard_reg_initial_val (enum machine_mode mode, unsigned int regno)
{
  struct initial_value_struct *ivs;
  int i;

  ivs = cfun->hard_reg_initial_vals;
  if (ivs != 0)
    for (i = 0; i < ivs->num_entries; i++)
      if (GET_MODE (ivs->entries[i].hard_reg) == mode
	  && REGNO (ivs->entries[i].hard_reg) == regno)
	return ivs->entries[i].pseudo;

  return NULL_RTX;
}

unsigned int
emit_initial_value_sets (void)
{
  struct initial_value_struct *ivs = cfun->hard_reg_initial_vals;
  int i;
  rtx seq;

  if (ivs == 0)
    return 0;

  start_sequence ();
  for (i = 0; i < ivs->num_entries; i++)
    emit_move_insn (ivs->entries[i].pseudo, ivs->entries[i].hard_reg);
  seq = get_insns ();
  end_sequence ();

  emit_insn_at_entry (seq);
  return 0;
}

struct tree_opt_pass pass_initial_value_sets =
{
  "initvals",                           /* name */
  NULL,                                 /* gate */
  emit_initial_value_sets,              /* execute */
  NULL,                                 /* sub */
  NULL,                                 /* next */
  0,                                    /* static_pass_number */
  0,                                    /* tv_id */
  0,                                    /* properties_required */
  0,                                    /* properties_provided */
  0,                                    /* properties_destroyed */
  0,                                    /* todo_flags_start */
  TODO_dump_func,                       /* todo_flags_finish */
  0                                     /* letter */
};

/* If the backend knows where to allocate pseudos for hard
   register initial values, register these allocations now.  */
void
allocate_initial_values (rtx *reg_equiv_memory_loc ATTRIBUTE_UNUSED)
{
  if (targetm.allocate_initial_value)
    {
      struct initial_value_struct *ivs = cfun->hard_reg_initial_vals;
      int i;

      if (ivs == 0)
	return;

      for (i = 0; i < ivs->num_entries; i++)
	{
	  int regno = REGNO (ivs->entries[i].pseudo);
	  rtx x = targetm.allocate_initial_value (ivs->entries[i].hard_reg);
  
	  if (x && REG_N_SETS (REGNO (ivs->entries[i].pseudo)) <= 1)
	    {
	      if (MEM_P (x))
		reg_equiv_memory_loc[regno] = x;
	      else
		{
		  basic_block bb;
		  int new_regno;

		  gcc_assert (REG_P (x));
		  new_regno = REGNO (x);
		  reg_renumber[regno] = new_regno;
		  /* Poke the regno right into regno_reg_rtx so that even
		     fixed regs are accepted.  */
		  REGNO (ivs->entries[i].pseudo) = new_regno;
		  /* Update global register liveness information.  */
		  FOR_EACH_BB (bb)
		    {
		      struct rtl_bb_info *info = bb->il.rtl;

		      if (REGNO_REG_SET_P(info->global_live_at_start, regno))
			SET_REGNO_REG_SET (info->global_live_at_start,
					   new_regno);
		      if (REGNO_REG_SET_P(info->global_live_at_end, regno))
			SET_REGNO_REG_SET (info->global_live_at_end,
					   new_regno);
		    }
		}
	    }
	}
    }
}

#include "gt-integrate.h"
