/* Subroutines for insn-output.c for NetWare.
   Contributed by Jan Beulich (jbeulich@novell.com)
   Copyright (C) 2004, 2005 Free Software Foundation, Inc.

This file is part of GCC.

GCC is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2, or (at your option)
any later version.

GCC is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with GCC; see the file COPYING.  If not, write to
the Free Software Foundation, 51 Franklin Street, Fifth Floor,
Boston, MA 02110-1301, USA.  */

#include "config.h"
#include "system.h"
#include "coretypes.h"
#include "tm.h"
#include "rtl.h"
#include "regs.h"
#include "hard-reg-set.h"
#include "output.h"
#include "tree.h"
#include "flags.h"
#include "tm_p.h"
#include "toplev.h"
#include "ggc.h"


/* Return string which is the former assembler name modified with an 
   underscore prefix and a suffix consisting of an atsign (@) followed
   by the number of bytes of arguments */

static tree
gen_stdcall_or_fastcall_decoration (tree decl, char prefix)
{
  unsigned total = 0;
  /* ??? This probably should use XSTR (XEXP (DECL_RTL (decl), 0), 0) instead
     of DECL_ASSEMBLER_NAME.  */
  const char *asmname = IDENTIFIER_POINTER (DECL_ASSEMBLER_NAME (decl));
  char *newsym;
  tree formal_type = TYPE_ARG_TYPES (TREE_TYPE (decl));

  if (formal_type != NULL_TREE)
    {
      /* These attributes are ignored for variadic functions in
	 i386.c:ix86_return_pops_args. For compatibility with MS
	 compiler do not add @0 suffix here.  */ 
      if (TREE_VALUE (tree_last (formal_type)) != void_type_node)
	return NULL_TREE;

      /* Quit if we hit an incomplete type.  Error is reported
	 by convert_arguments in c-typeck.c or cp/typeck.c.  */
      while (TREE_VALUE (formal_type) != void_type_node
	     && COMPLETE_TYPE_P (TREE_VALUE (formal_type)))	
	{
	  unsigned parm_size
	    = TREE_INT_CST_LOW (TYPE_SIZE (TREE_VALUE (formal_type)));

	  /* Must round up to include padding.  This is done the same
	     way as in store_one_arg.  */
	  parm_size = ((parm_size + PARM_BOUNDARY - 1)
		       / PARM_BOUNDARY * PARM_BOUNDARY);
	  total += parm_size;
	  formal_type = TREE_CHAIN (formal_type);
	}
    }

  newsym = alloca (1 + strlen (asmname) + 1 + 10 + 1);
  return get_identifier_with_length (newsym,
				     sprintf (newsym,
					      "%c%s@%u",
					      prefix,
					      asmname,
					      total / BITS_PER_UNIT));
}

/* Return string which is the former assembler name modified with an 
   _n@ prefix where n represents the number of arguments passed in
   registers */

static tree
gen_regparm_prefix (tree decl, unsigned nregs)
{
  unsigned total = 0;
  /* ??? This probably should use XSTR (XEXP (DECL_RTL (decl), 0), 0) instead
     of DECL_ASSEMBLER_NAME.  */
  const char *asmname = IDENTIFIER_POINTER (DECL_ASSEMBLER_NAME (decl));
  char *newsym;
  tree formal_type = TYPE_ARG_TYPES (TREE_TYPE (decl));

  if (formal_type != NULL_TREE)
    {
      /* This attribute is ignored for variadic functions.  */ 
      if (TREE_VALUE (tree_last (formal_type)) != void_type_node)
	return NULL_TREE;

      /* Quit if we hit an incomplete type.  Error is reported
	 by convert_arguments in c-typeck.c or cp/typeck.c.  */
      while (TREE_VALUE (formal_type) != void_type_node
	     && COMPLETE_TYPE_P (TREE_VALUE (formal_type)))	
	{
	  unsigned parm_size
	    = TREE_INT_CST_LOW (TYPE_SIZE (TREE_VALUE (formal_type)));

	  /* Must round up to include padding.  This is done the same
	     way as in store_one_arg.  */
	  parm_size = ((parm_size + PARM_BOUNDARY - 1)
		       / PARM_BOUNDARY * PARM_BOUNDARY);
	  total += parm_size;
	  formal_type = TREE_CHAIN (formal_type);
	}
    }

  if (nregs > total / BITS_PER_WORD)
    nregs = total / BITS_PER_WORD;
  gcc_assert (nregs <= 9);
  newsym = alloca (3 + strlen (asmname) + 1);
  return get_identifier_with_length (newsym,
				     sprintf (newsym,
					      "_%u@%s",
					      nregs,
					      asmname));
}

void
i386_nlm_encode_section_info (tree decl, rtx rtl, int first)
{
  default_encode_section_info (decl, rtl, first);

  if (first
      && TREE_CODE (decl) == FUNCTION_DECL
      && *IDENTIFIER_POINTER (DECL_ASSEMBLER_NAME (decl)) != '*'
      && !strchr (IDENTIFIER_POINTER (DECL_ASSEMBLER_NAME (decl)), '@'))
    {
      tree type_attributes = TYPE_ATTRIBUTES (TREE_TYPE (decl));
      tree newid;

      if (lookup_attribute ("stdcall", type_attributes))
	newid = gen_stdcall_or_fastcall_decoration (decl, '_');
      else if (lookup_attribute ("fastcall", type_attributes))
	newid = gen_stdcall_or_fastcall_decoration (decl, FASTCALL_PREFIX);
      else if ((newid = lookup_attribute ("regparm", type_attributes)) != NULL_TREE)
	newid = gen_regparm_prefix (decl,
		      TREE_INT_CST_LOW (TREE_VALUE (TREE_VALUE (newid))));
      if (newid != NULL_TREE) 	
	{
	  rtx rtlname = XEXP (rtl, 0);

	  if (GET_CODE (rtlname) == MEM)
	    rtlname = XEXP (rtlname, 0);
	  XSTR (rtlname, 0) = IDENTIFIER_POINTER (newid);
	  /* These attributes must be present on first declaration,
	     change_decl_assembler_name will warn if they are added
	     later and the decl has been referenced, but duplicate_decls
	     should catch the mismatch before this is called.  */ 
	  change_decl_assembler_name (decl, newid);
	}
    }
}

/* Strip the stdcall/fastcall/regparm pre-/suffix.  */

const char *
i386_nlm_strip_name_encoding (const char *str)
{
  const char *name = default_strip_name_encoding (str);

  if (*str != '*' && (*name == '_' || *name == '@'))
    {
      const char *p = strchr (name + 1, '@');

      if (p)
	{
	  ++name;
	  if (ISDIGIT (p[1]))
	    name = ggc_alloc_string (name, p - name);
	  else
	    {
	      gcc_assert (ISDIGIT (*name));
	      name++;
	      gcc_assert (name == p);
	    }
	}
    }
  return name;
}
