/* Generic hooks for the RTL middle-end.
   Copyright (C) 2004, 2005 Free Software Foundation, Inc.

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
#include "rtlhooks-def.h"
#include "expr.h"
#include "recog.h"


/* For speed, we will copy the RTX hooks struct member-by-member
   instead of doing indirect calls.  For these reason, we initialize
   *two* struct rtl_hooks globals: rtl_hooks is the one that is used
   to actually call the hooks, while general_rtl_hooks is used
   to restore the hooks by passes that modify them.  */

const struct rtl_hooks general_rtl_hooks = RTL_HOOKS_INITIALIZER;
struct rtl_hooks rtl_hooks = RTL_HOOKS_INITIALIZER;

rtx
gen_lowpart_general (enum machine_mode mode, rtx x)
{
  rtx result = gen_lowpart_common (mode, x);

  if (result)
    return result;
  /* If it's a REG, it must be a hard reg that's not valid in MODE.  */
  else if (REG_P (x)
	   /* Or we could have a subreg of a floating point value.  */
	   || (GET_CODE (x) == SUBREG
	       && FLOAT_MODE_P (GET_MODE (SUBREG_REG (x)))))
    {
      result = gen_lowpart_common (mode, copy_to_reg (x));
      gcc_assert (result != 0);
      return result;
    }
  else
    {
      int offset = 0;

      /* The only additional case we can do is MEM.  */
      gcc_assert (MEM_P (x));

      /* The following exposes the use of "x" to CSE.  */
      if (GET_MODE_SIZE (GET_MODE (x)) <= UNITS_PER_WORD
	  && SCALAR_INT_MODE_P (GET_MODE (x))
	  && TRULY_NOOP_TRUNCATION (GET_MODE_BITSIZE (mode),
				    GET_MODE_BITSIZE (GET_MODE (x)))
	  && ! no_new_pseudos)
	return gen_lowpart_general (mode, force_reg (GET_MODE (x), x));

      if (WORDS_BIG_ENDIAN)
	offset = (MAX (GET_MODE_SIZE (GET_MODE (x)), UNITS_PER_WORD)
		  - MAX (GET_MODE_SIZE (mode), UNITS_PER_WORD));

      if (BYTES_BIG_ENDIAN)
	/* Adjust the address so that the address-after-the-data
	   is unchanged.  */
	offset -= (MIN (UNITS_PER_WORD, GET_MODE_SIZE (mode))
		   - MIN (UNITS_PER_WORD, GET_MODE_SIZE (GET_MODE (x))));

      return adjust_address (x, mode, offset);
    }
}

/* Similar to gen_lowpart, but cannot emit any instruction via
   copy_to_reg or force_reg.  Mainly used in simplify-rtx.c.  */
rtx
gen_lowpart_no_emit_general (enum machine_mode mode, rtx x)
{
  rtx result = gen_lowpart_if_possible (mode, x);
  if (result)
    return result;
  else
    return x;
}

rtx
reg_num_sign_bit_copies_general (rtx x ATTRIBUTE_UNUSED,
				 enum machine_mode mode ATTRIBUTE_UNUSED,
                                 rtx known_x ATTRIBUTE_UNUSED,
				 enum machine_mode known_mode ATTRIBUTE_UNUSED,
                                 unsigned int known_ret ATTRIBUTE_UNUSED,
                                 unsigned int *result ATTRIBUTE_UNUSED)
{
  return NULL;
}

rtx
reg_nonzero_bits_general (rtx x ATTRIBUTE_UNUSED,
			  enum machine_mode mode ATTRIBUTE_UNUSED,
			  rtx known_x ATTRIBUTE_UNUSED,
                          enum machine_mode known_mode ATTRIBUTE_UNUSED,
                          unsigned HOST_WIDE_INT known_ret ATTRIBUTE_UNUSED,
                          unsigned HOST_WIDE_INT *nonzero ATTRIBUTE_UNUSED)
{
  return NULL;
}

bool
reg_truncated_to_mode_general (enum machine_mode mode ATTRIBUTE_UNUSED,
			       rtx x ATTRIBUTE_UNUSED)
{
  return false;
}

/* Assuming that X is an rtx (e.g., MEM, REG or SUBREG) for a fixed-point
   number, return an rtx (MEM, SUBREG, or CONST_INT) that refers to the
   least-significant part of X.
   MODE specifies how big a part of X to return.

   If the requested operation cannot be done, 0 is returned.

   This is similar to gen_lowpart_general.  */

rtx
gen_lowpart_if_possible (enum machine_mode mode, rtx x)
{
  rtx result = gen_lowpart_common (mode, x);

  if (result)
    return result;
  else if (MEM_P (x))
    {
      /* This is the only other case we handle.  */
      int offset = 0;
      rtx new;

      if (WORDS_BIG_ENDIAN)
	offset = (MAX (GET_MODE_SIZE (GET_MODE (x)), UNITS_PER_WORD)
		  - MAX (GET_MODE_SIZE (mode), UNITS_PER_WORD));
      if (BYTES_BIG_ENDIAN)
	/* Adjust the address so that the address-after-the-data is
	   unchanged.  */
	offset -= (MIN (UNITS_PER_WORD, GET_MODE_SIZE (mode))
		   - MIN (UNITS_PER_WORD, GET_MODE_SIZE (GET_MODE (x))));

      new = adjust_address_nv (x, mode, offset);
      if (! memory_address_p (mode, XEXP (new, 0)))
	return 0;

      return new;
    }
  else if (mode != GET_MODE (x) && GET_MODE (x) != VOIDmode)
    return gen_lowpart_SUBREG (mode, x);
  else
    return 0;
}

