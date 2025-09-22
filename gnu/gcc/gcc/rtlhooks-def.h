/* Default macros to initialize an rtl_hooks data structure.
   Copyright 2004, 2005 Free Software Foundation, Inc.

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

#ifndef GCC_RTL_HOOKS_DEF_H
#define GCC_RTL_HOOKS_DEF_H

#include "rtl.h"

#define RTL_HOOKS_GEN_LOWPART gen_lowpart_general
#define RTL_HOOKS_GEN_LOWPART_NO_EMIT gen_lowpart_no_emit_general
#define RTL_HOOKS_REG_NONZERO_REG_BITS reg_nonzero_bits_general
#define RTL_HOOKS_REG_NUM_SIGN_BIT_COPIES reg_num_sign_bit_copies_general
#define RTL_HOOKS_REG_TRUNCATED_TO_MODE reg_truncated_to_mode_general

/* The structure is defined in rtl.h.  */
#define RTL_HOOKS_INITIALIZER {			\
  RTL_HOOKS_GEN_LOWPART,			\
  RTL_HOOKS_GEN_LOWPART_NO_EMIT,		\
  RTL_HOOKS_REG_NONZERO_REG_BITS,		\
  RTL_HOOKS_REG_NUM_SIGN_BIT_COPIES,		\
  RTL_HOOKS_REG_TRUNCATED_TO_MODE,		\
}

extern rtx gen_lowpart_general (enum machine_mode, rtx);
extern rtx gen_lowpart_no_emit_general (enum machine_mode, rtx);
extern rtx reg_nonzero_bits_general (rtx, enum machine_mode, rtx,
				     enum machine_mode,
				     unsigned HOST_WIDE_INT,
				     unsigned HOST_WIDE_INT *);
extern rtx reg_num_sign_bit_copies_general (rtx, enum machine_mode, rtx,
					    enum machine_mode,
					    unsigned int, unsigned int *);
extern bool reg_truncated_to_mode_general (enum machine_mode, rtx);

#endif /* GCC_RTL_HOOKS_DEF_H */
