/* General-purpose hooks.
   Copyright (C) 2002, 2003, 2004, 2005 Free Software Foundation, Inc.

This program is free software; you can redistribute it and/or modify it
under the terms of the GNU General Public License as published by the
Free Software Foundation; either version 2, or (at your option) any
later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.

 In other words, you are welcome to use, share and improve this program.
 You are forbidden to forbid anyone else to use, share and improve
 what you give them.   Help stamp out software-hoarding!  */

/* This file contains generic hooks that can be used as defaults for
   target or language-dependent hook initializers.  */

#include "config.h"
#include "system.h"
#include "coretypes.h"
#include "tm.h"
#include "hooks.h"

/* Generic hook that does absolutely zappo.  */
void
hook_void_void (void)
{
}

/* Generic hook that takes no arguments and returns false.  */
bool
hook_bool_void_false (void)
{
  return false;
}

/* Generic hook that takes no arguments and returns true.  */
bool
hook_bool_void_true (void)
{
  return true;
}

/* Generic hook that takes no arguments and returns NO_REGS.  */
int
hook_int_void_no_regs (void)
{
  return NO_REGS;
}

/* Generic hook that takes (bool) and returns false.  */
bool
hook_bool_bool_false (bool a ATTRIBUTE_UNUSED)
{
  return false;
}

/* Generic hook that takes (enum machine_mode) and returns false.  */
bool
hook_bool_mode_false (enum machine_mode mode ATTRIBUTE_UNUSED)
{
  return false;
}

/* Generic hook that takes (enum machine_mode, rtx) and returns false.  */
bool
hook_bool_mode_rtx_false (enum machine_mode mode ATTRIBUTE_UNUSED,
			  rtx value ATTRIBUTE_UNUSED)
{
  return false;
}

/* Generic hook that takes (enum machine_mode, rtx) and returns true.  */
bool
hook_bool_mode_rtx_true (enum machine_mode mode ATTRIBUTE_UNUSED,
			 rtx value ATTRIBUTE_UNUSED)
{
  return true;
}

/* Generic hook that takes (FILE *, const char *) and does nothing.  */
void
hook_void_FILEptr_constcharptr (FILE *a ATTRIBUTE_UNUSED, const char *b ATTRIBUTE_UNUSED)
{
}

/* Used for the TARGET_ASM_CAN_OUTPUT_MI_THUNK hook.  */
bool
hook_bool_tree_hwi_hwi_tree_false (tree a ATTRIBUTE_UNUSED,
				   HOST_WIDE_INT b ATTRIBUTE_UNUSED,
				   HOST_WIDE_INT c ATTRIBUTE_UNUSED,
				   tree d ATTRIBUTE_UNUSED)
{
  return false;
}

bool
hook_bool_tree_hwi_hwi_tree_true (tree a ATTRIBUTE_UNUSED,
				  HOST_WIDE_INT b ATTRIBUTE_UNUSED,
				  HOST_WIDE_INT c ATTRIBUTE_UNUSED,
				  tree d ATTRIBUTE_UNUSED)
{
  return true;
}

bool
hook_bool_constcharptr_size_t_false (const char *a ATTRIBUTE_UNUSED,
				     size_t b ATTRIBUTE_UNUSED)
{
  return false;
}

bool
hook_bool_size_t_constcharptr_int_true (size_t a ATTRIBUTE_UNUSED,
					const char *b ATTRIBUTE_UNUSED,
					int c ATTRIBUTE_UNUSED)
{
  return true;
}

bool
default_can_output_mi_thunk_no_vcall (tree a ATTRIBUTE_UNUSED,
				      HOST_WIDE_INT b ATTRIBUTE_UNUSED,
				      HOST_WIDE_INT c,
				      tree d ATTRIBUTE_UNUSED)
{
  return c == 0;
}

int
hook_int_tree_0 (tree a ATTRIBUTE_UNUSED)
{
  return 0;
}

/* ??? Used for comp_type_attributes, which ought to return bool.  */
int
hook_int_tree_tree_1 (tree a ATTRIBUTE_UNUSED, tree b ATTRIBUTE_UNUSED)
{
  return 1;
}

int
hook_int_rtx_0 (rtx a ATTRIBUTE_UNUSED)
{
  return 0;
}

int
hook_int_size_t_constcharptr_int_0 (size_t a ATTRIBUTE_UNUSED,
				    const char *b ATTRIBUTE_UNUSED,
				    int c ATTRIBUTE_UNUSED)
{
  return 0;
}

unsigned int
hook_uint_uint_constcharptrptr_0 (unsigned int a ATTRIBUTE_UNUSED,
				  const char **b ATTRIBUTE_UNUSED)
{
  return 0;
}

void
hook_void_tree (tree a ATTRIBUTE_UNUSED)
{
}

void
hook_void_constcharptr (const char *a ATTRIBUTE_UNUSED)
{
}

void
hook_void_tree_treeptr (tree a ATTRIBUTE_UNUSED, tree *b ATTRIBUTE_UNUSED)
{
}

bool
hook_bool_tree_false (tree a ATTRIBUTE_UNUSED)
{
  return false;
}

bool
hook_bool_tree_true (tree a ATTRIBUTE_UNUSED)
{
  return true;
}

bool
hook_bool_tree_tree_false (tree a ATTRIBUTE_UNUSED, tree b ATTRIBUTE_UNUSED)
{
  return false;
}

bool
hook_bool_tree_bool_false (tree a ATTRIBUTE_UNUSED, bool b ATTRIBUTE_UNUSED)
{
  return false;
}

bool
hook_bool_rtx_false (rtx a ATTRIBUTE_UNUSED)
{
  return false;
}

bool
hook_bool_uintp_uintp_false (unsigned int *a ATTRIBUTE_UNUSED,
			     unsigned int *b ATTRIBUTE_UNUSED)
{
  return false;
}

bool
hook_bool_rtx_int_int_intp_false (rtx a ATTRIBUTE_UNUSED,
				  int b ATTRIBUTE_UNUSED,
				  int c ATTRIBUTE_UNUSED,
				  int *d ATTRIBUTE_UNUSED)
{
  return false;
}

/* Generic hook that takes an rtx and returns it.  */
rtx
hook_rtx_rtx_identity (rtx x)
{
  return x;
}

/* Generic hook that takes an rtx and returns NULL_RTX.  */
rtx
hook_rtx_rtx_null (rtx x ATTRIBUTE_UNUSED)
{
  return NULL;
}

/* Generic hook that takes a tree and an int and returns NULL_RTX.  */
rtx
hook_rtx_tree_int_null (tree a ATTRIBUTE_UNUSED, int b ATTRIBUTE_UNUSED)
{
  return NULL;
}

/* Generic hook that takes three trees and returns the last one as is.  */
tree
hook_tree_tree_tree_tree_3rd_identity (tree a ATTRIBUTE_UNUSED,
				       tree b ATTRIBUTE_UNUSED, tree c)
{
  return c;
}

/* Generic hook that takes a tree and returns a NULL string.  */
const char *
hook_constcharptr_tree_null (tree t ATTRIBUTE_UNUSED)
{
  return NULL;
}

tree
hook_tree_tree_tree_bool_null (tree t0 ATTRIBUTE_UNUSED, tree t1 ATTRIBUTE_UNUSED,
			       bool ignore ATTRIBUTE_UNUSED)
{
  return NULL;
}

tree
hook_tree_tree_tree_null (tree t0 ATTRIBUTE_UNUSED, tree t1 ATTRIBUTE_UNUSED)
{
  return NULL;
}

/* Generic hook that takes a rtx and returns a NULL string.  */
const char *
hook_constcharptr_rtx_null (rtx r ATTRIBUTE_UNUSED)
{
  return NULL;
}

const char *
hook_constcharptr_tree_tree_null (tree t0 ATTRIBUTE_UNUSED,
				  tree t1 ATTRIBUTE_UNUSED)
{
  return NULL;
}

const char *
hook_constcharptr_int_tree_null (int i ATTRIBUTE_UNUSED,
				 tree t0 ATTRIBUTE_UNUSED)
{
  return NULL;
}

const char *
hook_constcharptr_int_tree_tree_null (int i ATTRIBUTE_UNUSED,
				      tree t0 ATTRIBUTE_UNUSED,
				      tree t1 ATTRIBUTE_UNUSED)
{
  return NULL;
}
