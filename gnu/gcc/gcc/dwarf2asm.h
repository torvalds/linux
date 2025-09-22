/* Dwarf2 assembler output helper routines.
   Copyright (C) 2001, 2003, 2005 Free Software Foundation, Inc.

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


extern void dw2_assemble_integer (int, rtx);

extern void dw2_asm_output_data (int, unsigned HOST_WIDE_INT,
				 const char *, ...)
     ATTRIBUTE_NULL_PRINTF_3;

extern void dw2_asm_output_delta (int, const char *, const char *,
				  const char *, ...)
     ATTRIBUTE_NULL_PRINTF_4;

extern void dw2_asm_output_offset (int, const char *, section *, 
				   const char *, ...)
     ATTRIBUTE_NULL_PRINTF_4;

extern void dw2_asm_output_addr (int, const char *, const char *, ...)
     ATTRIBUTE_NULL_PRINTF_3;

extern void dw2_asm_output_addr_rtx (int, rtx, const char *, ...)
     ATTRIBUTE_NULL_PRINTF_3;

extern void dw2_asm_output_encoded_addr_rtx (int, rtx, bool,
					     const char *, ...)
     ATTRIBUTE_NULL_PRINTF_4;

extern void dw2_asm_output_nstring (const char *, size_t,
				    const char *, ...)
     ATTRIBUTE_NULL_PRINTF_3;

extern void dw2_asm_output_data_uleb128	(unsigned HOST_WIDE_INT,
					 const char *, ...)
     ATTRIBUTE_NULL_PRINTF_2;

extern void dw2_asm_output_data_sleb128	(HOST_WIDE_INT,
					 const char *, ...)
     ATTRIBUTE_NULL_PRINTF_2;

extern void dw2_asm_output_delta_uleb128 (const char *, const char *,
					  const char *, ...)
     ATTRIBUTE_NULL_PRINTF_3;

extern int size_of_uleb128 (unsigned HOST_WIDE_INT);
extern int size_of_sleb128 (HOST_WIDE_INT);
extern int size_of_encoded_value (int);
extern const char *eh_data_format_name (int);

extern void dw2_output_indirect_constants (void);

/* These are currently unused.  */

#if 0
extern void dw2_asm_output_pcrel (int, const char *, const char *, ...)
     ATTRIBUTE_NULL_PRINTF_3;

extern void dw2_asm_output_delta_sleb128 (const char *, const char *,
					  const char *, ...)
     ATTRIBUTE_NULL_PRINTF_3;
#endif
