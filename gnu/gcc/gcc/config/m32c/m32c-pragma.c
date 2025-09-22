/* M32C Pragma support
   Copyright (C) 2004 Free Software Foundation, Inc.
   Contributed by Red Hat, Inc.

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

#include <stdio.h>
#include "config.h"
#include "system.h"
#include "coretypes.h"
#include "tm.h"
#include "tree.h"
#include "rtl.h"
#include "toplev.h"
#include "c-pragma.h"
#include "cpplib.h"
#include "hard-reg-set.h"
#include "output.h"
#include "m32c-protos.h"
#include "function.h"
#define MAX_RECOG_OPERANDS 10
#include "reload.h"
#include "target.h"

/* Implements the "GCC memregs" pragma.  This pragma takes only an
   integer, and is semantically identical to the -memregs= command
   line option.  The only catch is, the programmer should only use
   this pragma at the beginning of the file (preferably, in some
   project-wide header) to avoid ABI changes related to changing the
   list of available "registers".  */
static void
m32c_pragma_memregs (cpp_reader * reader ATTRIBUTE_UNUSED)
{
  /* on off */
  tree val;
  enum cpp_ttype type;
  HOST_WIDE_INT i;
  static char new_number[3];

  type = pragma_lex (&val);
  if (type == CPP_NUMBER)
    {
      if (host_integerp (val, 1))
	{
	  i = tree_low_cst (val, 1);

	  type = pragma_lex (&val);
	  if (type != CPP_EOF)
	    warning (0, "junk at end of #pragma GCC memregs [0..16]");

	  if (0 <= i && i <= 16)
	    {
	      if (!ok_to_change_target_memregs)
		{
		  warning (0,
			   "#pragma GCC memregs must precede any function decls");
		  return;
		}
	      new_number[0] = (i / 10) + '0';
	      new_number[1] = (i % 10) + '0';
	      new_number[2] = 0;
	      target_memregs = new_number;
	      m32c_conditional_register_usage ();
	    }
	  else
	    {
	      warning (0, "#pragma GCC memregs takes a number [0..16]");
	    }

	  return;
	}
    }

  error ("#pragma GCC memregs takes a number [0..16]");
}

/* Implements REGISTER_TARGET_PRAGMAS.  */
void
m32c_register_pragmas (void)
{
  c_register_pragma ("GCC", "memregs", m32c_pragma_memregs);
}
