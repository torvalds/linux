/* Machine mode definitions for GCC; included by rtl.h and tree.h.
   Copyright (C) 1991, 1993, 1994, 1996, 1998, 1999, 2000, 2001, 2003
   Free Software Foundation, Inc.

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

#ifndef HAVE_MACHINE_MODES
#define HAVE_MACHINE_MODES

/* Make an enum class that gives all the machine modes.  */
#include "insn-modes.h"

/* Get the name of mode MODE as a string.  */

extern const char * const mode_name[NUM_MACHINE_MODES];
#define GET_MODE_NAME(MODE)  mode_name[MODE]

/* Mode classes.  */

#include "mode-classes.def"
#define DEF_MODE_CLASS(M) M
enum mode_class { MODE_CLASSES, MAX_MODE_CLASS };
#undef DEF_MODE_CLASS
#undef MODE_CLASSES

/* Get the general kind of object that mode MODE represents
   (integer, floating, complex, etc.)  */

extern const unsigned char mode_class[NUM_MACHINE_MODES];
#define GET_MODE_CLASS(MODE)  mode_class[MODE]

/* Nonzero if MODE is an integral mode.  */
#define INTEGRAL_MODE_P(MODE)			\
  (GET_MODE_CLASS (MODE) == MODE_INT		\
   || GET_MODE_CLASS (MODE) == MODE_PARTIAL_INT \
   || GET_MODE_CLASS (MODE) == MODE_COMPLEX_INT \
   || GET_MODE_CLASS (MODE) == MODE_VECTOR_INT)

/* Nonzero if MODE is a floating-point mode.  */
#define FLOAT_MODE_P(MODE)		\
  (GET_MODE_CLASS (MODE) == MODE_FLOAT	\
   || GET_MODE_CLASS (MODE) == MODE_DECIMAL_FLOAT \
   || GET_MODE_CLASS (MODE) == MODE_COMPLEX_FLOAT \
   || GET_MODE_CLASS (MODE) == MODE_VECTOR_FLOAT)

/* Nonzero if MODE is a complex mode.  */
#define COMPLEX_MODE_P(MODE)			\
  (GET_MODE_CLASS (MODE) == MODE_COMPLEX_INT	\
   || GET_MODE_CLASS (MODE) == MODE_COMPLEX_FLOAT)

/* Nonzero if MODE is a vector mode.  */
#define VECTOR_MODE_P(MODE)			\
  (GET_MODE_CLASS (MODE) == MODE_VECTOR_INT	\
   || GET_MODE_CLASS (MODE) == MODE_VECTOR_FLOAT)

/* Nonzero if MODE is a scalar integral mode.  */
#define SCALAR_INT_MODE_P(MODE)			\
  (GET_MODE_CLASS (MODE) == MODE_INT		\
   || GET_MODE_CLASS (MODE) == MODE_PARTIAL_INT)

/* Nonzero if MODE is a scalar floating point mode.  */
#define SCALAR_FLOAT_MODE_P(MODE)		\
  (GET_MODE_CLASS (MODE) == MODE_FLOAT		\
   || GET_MODE_CLASS (MODE) == MODE_DECIMAL_FLOAT)

/* Nonzero if MODE is a decimal floating point mode.  */
#define DECIMAL_FLOAT_MODE_P(MODE)		\
  (GET_MODE_CLASS (MODE) == MODE_DECIMAL_FLOAT)

/* Nonzero if CLASS modes can be widened.  */
#define CLASS_HAS_WIDER_MODES_P(CLASS)         \
  (CLASS == MODE_INT                           \
   || CLASS == MODE_FLOAT                      \
   || CLASS == MODE_DECIMAL_FLOAT              \
   || CLASS == MODE_COMPLEX_FLOAT)

/* Get the size in bytes and bits of an object of mode MODE.  */

extern CONST_MODE_SIZE unsigned char mode_size[NUM_MACHINE_MODES];
#define GET_MODE_SIZE(MODE)    ((unsigned short) mode_size[MODE])
#define GET_MODE_BITSIZE(MODE) ((unsigned short) (GET_MODE_SIZE (MODE) * BITS_PER_UNIT))

/* Get the number of value bits of an object of mode MODE.  */
extern const unsigned short mode_precision[NUM_MACHINE_MODES];
#define GET_MODE_PRECISION(MODE)  mode_precision[MODE]

/* Get a bitmask containing 1 for all bits in a word
   that fit within mode MODE.  */

extern const unsigned HOST_WIDE_INT mode_mask_array[NUM_MACHINE_MODES];

#define GET_MODE_MASK(MODE) mode_mask_array[MODE]

/* Return the mode of the inner elements in a vector.  */

extern const unsigned char mode_inner[NUM_MACHINE_MODES];
#define GET_MODE_INNER(MODE) mode_inner[MODE]

/* Get the size in bytes of the basic parts of an object of mode MODE.  */

#define GET_MODE_UNIT_SIZE(MODE)		\
  (GET_MODE_INNER (MODE) == VOIDmode		\
   ? GET_MODE_SIZE (MODE)			\
   : GET_MODE_SIZE (GET_MODE_INNER (MODE)))

/* Get the number of units in the object.  */

extern const unsigned char mode_nunits[NUM_MACHINE_MODES];
#define GET_MODE_NUNITS(MODE)  mode_nunits[MODE]

/* Get the next wider natural mode (eg, QI -> HI -> SI -> DI -> TI).  */

extern const unsigned char mode_wider[NUM_MACHINE_MODES];
#define GET_MODE_WIDER_MODE(MODE) mode_wider[MODE]

extern const unsigned char mode_2xwider[NUM_MACHINE_MODES];
#define GET_MODE_2XWIDER_MODE(MODE) mode_2xwider[MODE]

/* Return the mode for data of a given size SIZE and mode class CLASS.
   If LIMIT is nonzero, then don't use modes bigger than MAX_FIXED_MODE_SIZE.
   The value is BLKmode if no other mode is found.  */

extern enum machine_mode mode_for_size (unsigned int, enum mode_class, int);

/* Similar, but find the smallest mode for a given width.  */

extern enum machine_mode smallest_mode_for_size (unsigned int,
						 enum mode_class);


/* Return an integer mode of the exact same size as the input mode,
   or BLKmode on failure.  */

extern enum machine_mode int_mode_for_mode (enum machine_mode);

/* Find the best mode to use to access a bit field.  */

extern enum machine_mode get_best_mode (int, int, unsigned int,
					enum machine_mode, int);

/* Determine alignment, 1<=result<=BIGGEST_ALIGNMENT.  */

extern CONST_MODE_BASE_ALIGN unsigned char mode_base_align[NUM_MACHINE_MODES];

extern unsigned get_mode_alignment (enum machine_mode);

#define GET_MODE_ALIGNMENT(MODE) get_mode_alignment (MODE)

/* For each class, get the narrowest mode in that class.  */

extern const unsigned char class_narrowest_mode[MAX_MODE_CLASS];
#define GET_CLASS_NARROWEST_MODE(CLASS) class_narrowest_mode[CLASS]

/* Define the integer modes whose sizes are BITS_PER_UNIT and BITS_PER_WORD
   and the mode whose class is Pmode and whose size is POINTER_SIZE.  */

extern enum machine_mode byte_mode;
extern enum machine_mode word_mode;
extern enum machine_mode ptr_mode;

/* Target-dependent machine mode initialization - in insn-modes.c.  */
extern void init_adjust_machine_modes (void);

#endif /* not HAVE_MACHINE_MODES */
