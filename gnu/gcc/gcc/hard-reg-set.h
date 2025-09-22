/* Sets (bit vectors) of hard registers, and operations on them.
   Copyright (C) 1987, 1992, 1994, 2000, 2003, 2004, 2005
   Free Software Foundation, Inc.

This file is part of GCC

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

#ifndef GCC_HARD_REG_SET_H
#define GCC_HARD_REG_SET_H 

/* Define the type of a set of hard registers.  */

/* HARD_REG_ELT_TYPE is a typedef of the unsigned integral type which
   will be used for hard reg sets, either alone or in an array.

   If HARD_REG_SET is a macro, its definition is HARD_REG_ELT_TYPE,
   and it has enough bits to represent all the target machine's hard
   registers.  Otherwise, it is a typedef for a suitably sized array
   of HARD_REG_ELT_TYPEs.  HARD_REG_SET_LONGS is defined as how many.

   Note that lots of code assumes that the first part of a regset is
   the same format as a HARD_REG_SET.  To help make sure this is true,
   we only try the widest fast integer mode (HOST_WIDEST_FAST_INT)
   instead of all the smaller types.  This approach loses only if
   there are very few registers and then only in the few cases where
   we have an array of HARD_REG_SETs, so it needn't be as complex as
   it used to be.  */

typedef unsigned HOST_WIDEST_FAST_INT HARD_REG_ELT_TYPE;

#if FIRST_PSEUDO_REGISTER <= HOST_BITS_PER_WIDEST_FAST_INT

#define HARD_REG_SET HARD_REG_ELT_TYPE

#else

#define HARD_REG_SET_LONGS \
 ((FIRST_PSEUDO_REGISTER + HOST_BITS_PER_WIDEST_FAST_INT - 1)	\
  / HOST_BITS_PER_WIDEST_FAST_INT)
typedef HARD_REG_ELT_TYPE HARD_REG_SET[HARD_REG_SET_LONGS];

#endif

/* HARD_CONST is used to cast a constant to the appropriate type
   for use with a HARD_REG_SET.  */

#define HARD_CONST(X) ((HARD_REG_ELT_TYPE) (X))

/* Define macros SET_HARD_REG_BIT, CLEAR_HARD_REG_BIT and TEST_HARD_REG_BIT
   to set, clear or test one bit in a hard reg set of type HARD_REG_SET.
   All three take two arguments: the set and the register number.

   In the case where sets are arrays of longs, the first argument
   is actually a pointer to a long.

   Define two macros for initializing a set:
   CLEAR_HARD_REG_SET and SET_HARD_REG_SET.
   These take just one argument.

   Also define macros for copying hard reg sets:
   COPY_HARD_REG_SET and COMPL_HARD_REG_SET.
   These take two arguments TO and FROM; they read from FROM
   and store into TO.  COMPL_HARD_REG_SET complements each bit.

   Also define macros for combining hard reg sets:
   IOR_HARD_REG_SET and AND_HARD_REG_SET.
   These take two arguments TO and FROM; they read from FROM
   and combine bitwise into TO.  Define also two variants
   IOR_COMPL_HARD_REG_SET and AND_COMPL_HARD_REG_SET
   which use the complement of the set FROM.

   Also define GO_IF_HARD_REG_SUBSET (X, Y, TO):
   if X is a subset of Y, go to TO.
*/

#ifdef HARD_REG_SET

#define SET_HARD_REG_BIT(SET, BIT)  \
 ((SET) |= HARD_CONST (1) << (BIT))
#define CLEAR_HARD_REG_BIT(SET, BIT)  \
 ((SET) &= ~(HARD_CONST (1) << (BIT)))
#define TEST_HARD_REG_BIT(SET, BIT)  \
 (!!((SET) & (HARD_CONST (1) << (BIT))))

#define CLEAR_HARD_REG_SET(TO) ((TO) = HARD_CONST (0))
#define SET_HARD_REG_SET(TO) ((TO) = ~ HARD_CONST (0))

#define COPY_HARD_REG_SET(TO, FROM) ((TO) = (FROM))
#define COMPL_HARD_REG_SET(TO, FROM) ((TO) = ~(FROM))

#define IOR_HARD_REG_SET(TO, FROM) ((TO) |= (FROM))
#define IOR_COMPL_HARD_REG_SET(TO, FROM) ((TO) |= ~ (FROM))
#define AND_HARD_REG_SET(TO, FROM) ((TO) &= (FROM))
#define AND_COMPL_HARD_REG_SET(TO, FROM) ((TO) &= ~ (FROM))

#define GO_IF_HARD_REG_SUBSET(X,Y,TO) if (HARD_CONST (0) == ((X) & ~(Y))) goto TO

#define GO_IF_HARD_REG_EQUAL(X,Y,TO) if ((X) == (Y)) goto TO

#else

#define UHOST_BITS_PER_WIDE_INT ((unsigned) HOST_BITS_PER_WIDEST_FAST_INT)

#define SET_HARD_REG_BIT(SET, BIT)		\
  ((SET)[(BIT) / UHOST_BITS_PER_WIDE_INT]	\
   |= HARD_CONST (1) << ((BIT) % UHOST_BITS_PER_WIDE_INT))

#define CLEAR_HARD_REG_BIT(SET, BIT)		\
  ((SET)[(BIT) / UHOST_BITS_PER_WIDE_INT]	\
   &= ~(HARD_CONST (1) << ((BIT) % UHOST_BITS_PER_WIDE_INT)))

#define TEST_HARD_REG_BIT(SET, BIT)		\
  (!!((SET)[(BIT) / UHOST_BITS_PER_WIDE_INT]	\
      & (HARD_CONST (1) << ((BIT) % UHOST_BITS_PER_WIDE_INT))))

#if FIRST_PSEUDO_REGISTER <= 2*HOST_BITS_PER_WIDEST_FAST_INT
#define CLEAR_HARD_REG_SET(TO)  \
do { HARD_REG_ELT_TYPE *scan_tp_ = (TO);			\
     scan_tp_[0] = 0;						\
     scan_tp_[1] = 0; } while (0)

#define SET_HARD_REG_SET(TO)  \
do { HARD_REG_ELT_TYPE *scan_tp_ = (TO);			\
     scan_tp_[0] = -1;						\
     scan_tp_[1] = -1; } while (0)

#define COPY_HARD_REG_SET(TO, FROM)  \
do { HARD_REG_ELT_TYPE *scan_tp_ = (TO), *scan_fp_ = (FROM);	\
     scan_tp_[0] = scan_fp_[0];					\
     scan_tp_[1] = scan_fp_[1]; } while (0)

#define COMPL_HARD_REG_SET(TO, FROM)  \
do { HARD_REG_ELT_TYPE *scan_tp_ = (TO), *scan_fp_ = (FROM); 	\
     scan_tp_[0] = ~ scan_fp_[0];				\
     scan_tp_[1] = ~ scan_fp_[1]; } while (0)

#define AND_HARD_REG_SET(TO, FROM)  \
do { HARD_REG_ELT_TYPE *scan_tp_ = (TO), *scan_fp_ = (FROM); 	\
     scan_tp_[0] &= scan_fp_[0];				\
     scan_tp_[1] &= scan_fp_[1]; } while (0)

#define AND_COMPL_HARD_REG_SET(TO, FROM)  \
do { HARD_REG_ELT_TYPE *scan_tp_ = (TO), *scan_fp_ = (FROM); 	\
     scan_tp_[0] &= ~ scan_fp_[0];				\
     scan_tp_[1] &= ~ scan_fp_[1]; } while (0)

#define IOR_HARD_REG_SET(TO, FROM)  \
do { HARD_REG_ELT_TYPE *scan_tp_ = (TO), *scan_fp_ = (FROM); 	\
     scan_tp_[0] |= scan_fp_[0];				\
     scan_tp_[1] |= scan_fp_[1]; } while (0)

#define IOR_COMPL_HARD_REG_SET(TO, FROM)  \
do { HARD_REG_ELT_TYPE *scan_tp_ = (TO), *scan_fp_ = (FROM); 	\
     scan_tp_[0] |= ~ scan_fp_[0];				\
     scan_tp_[1] |= ~ scan_fp_[1]; } while (0)

#define GO_IF_HARD_REG_SUBSET(X,Y,TO)  \
do { HARD_REG_ELT_TYPE *scan_xp_ = (X), *scan_yp_ = (Y); 	\
     if ((0 == (scan_xp_[0] & ~ scan_yp_[0]))			\
	 && (0 == (scan_xp_[1] & ~ scan_yp_[1])))		\
	goto TO; } while (0)

#define GO_IF_HARD_REG_EQUAL(X,Y,TO)  \
do { HARD_REG_ELT_TYPE *scan_xp_ = (X), *scan_yp_ = (Y); 	\
     if ((scan_xp_[0] == scan_yp_[0])				\
	 && (scan_xp_[1] == scan_yp_[1]))			\
	goto TO; } while (0)

#else
#if FIRST_PSEUDO_REGISTER <= 3*HOST_BITS_PER_WIDEST_FAST_INT
#define CLEAR_HARD_REG_SET(TO)  \
do { HARD_REG_ELT_TYPE *scan_tp_ = (TO);			\
     scan_tp_[0] = 0;						\
     scan_tp_[1] = 0;						\
     scan_tp_[2] = 0; } while (0)

#define SET_HARD_REG_SET(TO)  \
do { HARD_REG_ELT_TYPE *scan_tp_ = (TO);			\
     scan_tp_[0] = -1;						\
     scan_tp_[1] = -1;						\
     scan_tp_[2] = -1; } while (0)

#define COPY_HARD_REG_SET(TO, FROM)  \
do { HARD_REG_ELT_TYPE *scan_tp_ = (TO), *scan_fp_ = (FROM);	\
     scan_tp_[0] = scan_fp_[0];					\
     scan_tp_[1] = scan_fp_[1];					\
     scan_tp_[2] = scan_fp_[2]; } while (0)

#define COMPL_HARD_REG_SET(TO, FROM)  \
do { HARD_REG_ELT_TYPE *scan_tp_ = (TO), *scan_fp_ = (FROM); 	\
     scan_tp_[0] = ~ scan_fp_[0];				\
     scan_tp_[1] = ~ scan_fp_[1];				\
     scan_tp_[2] = ~ scan_fp_[2]; } while (0)

#define AND_HARD_REG_SET(TO, FROM)  \
do { HARD_REG_ELT_TYPE *scan_tp_ = (TO), *scan_fp_ = (FROM); 	\
     scan_tp_[0] &= scan_fp_[0];				\
     scan_tp_[1] &= scan_fp_[1];				\
     scan_tp_[2] &= scan_fp_[2]; } while (0)

#define AND_COMPL_HARD_REG_SET(TO, FROM)  \
do { HARD_REG_ELT_TYPE *scan_tp_ = (TO), *scan_fp_ = (FROM); 	\
     scan_tp_[0] &= ~ scan_fp_[0];				\
     scan_tp_[1] &= ~ scan_fp_[1];				\
     scan_tp_[2] &= ~ scan_fp_[2]; } while (0)

#define IOR_HARD_REG_SET(TO, FROM)  \
do { HARD_REG_ELT_TYPE *scan_tp_ = (TO), *scan_fp_ = (FROM); 	\
     scan_tp_[0] |= scan_fp_[0];				\
     scan_tp_[1] |= scan_fp_[1];				\
     scan_tp_[2] |= scan_fp_[2]; } while (0)

#define IOR_COMPL_HARD_REG_SET(TO, FROM)  \
do { HARD_REG_ELT_TYPE *scan_tp_ = (TO), *scan_fp_ = (FROM); 	\
     scan_tp_[0] |= ~ scan_fp_[0];				\
     scan_tp_[1] |= ~ scan_fp_[1];				\
     scan_tp_[2] |= ~ scan_fp_[2]; } while (0)

#define GO_IF_HARD_REG_SUBSET(X,Y,TO)  \
do { HARD_REG_ELT_TYPE *scan_xp_ = (X), *scan_yp_ = (Y); 	\
     if ((0 == (scan_xp_[0] & ~ scan_yp_[0]))			\
	 && (0 == (scan_xp_[1] & ~ scan_yp_[1]))		\
	 && (0 == (scan_xp_[2] & ~ scan_yp_[2])))		\
	goto TO; } while (0)

#define GO_IF_HARD_REG_EQUAL(X,Y,TO)  \
do { HARD_REG_ELT_TYPE *scan_xp_ = (X), *scan_yp_ = (Y); 	\
     if ((scan_xp_[0] == scan_yp_[0])				\
	 && (scan_xp_[1] == scan_yp_[1])			\
	 && (scan_xp_[2] == scan_yp_[2]))			\
	goto TO; } while (0)

#else
#if FIRST_PSEUDO_REGISTER <= 4*HOST_BITS_PER_WIDEST_FAST_INT
#define CLEAR_HARD_REG_SET(TO)  \
do { HARD_REG_ELT_TYPE *scan_tp_ = (TO);			\
     scan_tp_[0] = 0;						\
     scan_tp_[1] = 0;						\
     scan_tp_[2] = 0;						\
     scan_tp_[3] = 0; } while (0)

#define SET_HARD_REG_SET(TO)  \
do { HARD_REG_ELT_TYPE *scan_tp_ = (TO);			\
     scan_tp_[0] = -1;						\
     scan_tp_[1] = -1;						\
     scan_tp_[2] = -1;						\
     scan_tp_[3] = -1; } while (0)

#define COPY_HARD_REG_SET(TO, FROM)  \
do { HARD_REG_ELT_TYPE *scan_tp_ = (TO), *scan_fp_ = (FROM);	\
     scan_tp_[0] = scan_fp_[0];					\
     scan_tp_[1] = scan_fp_[1];					\
     scan_tp_[2] = scan_fp_[2];					\
     scan_tp_[3] = scan_fp_[3]; } while (0)

#define COMPL_HARD_REG_SET(TO, FROM)  \
do { HARD_REG_ELT_TYPE *scan_tp_ = (TO), *scan_fp_ = (FROM); 	\
     scan_tp_[0] = ~ scan_fp_[0];				\
     scan_tp_[1] = ~ scan_fp_[1];				\
     scan_tp_[2] = ~ scan_fp_[2];				\
     scan_tp_[3] = ~ scan_fp_[3]; } while (0)

#define AND_HARD_REG_SET(TO, FROM)  \
do { HARD_REG_ELT_TYPE *scan_tp_ = (TO), *scan_fp_ = (FROM); 	\
     scan_tp_[0] &= scan_fp_[0];				\
     scan_tp_[1] &= scan_fp_[1];				\
     scan_tp_[2] &= scan_fp_[2];				\
     scan_tp_[3] &= scan_fp_[3]; } while (0)

#define AND_COMPL_HARD_REG_SET(TO, FROM)  \
do { HARD_REG_ELT_TYPE *scan_tp_ = (TO), *scan_fp_ = (FROM); 	\
     scan_tp_[0] &= ~ scan_fp_[0];				\
     scan_tp_[1] &= ~ scan_fp_[1];				\
     scan_tp_[2] &= ~ scan_fp_[2];				\
     scan_tp_[3] &= ~ scan_fp_[3]; } while (0)

#define IOR_HARD_REG_SET(TO, FROM)  \
do { HARD_REG_ELT_TYPE *scan_tp_ = (TO), *scan_fp_ = (FROM); 	\
     scan_tp_[0] |= scan_fp_[0];				\
     scan_tp_[1] |= scan_fp_[1];				\
     scan_tp_[2] |= scan_fp_[2];				\
     scan_tp_[3] |= scan_fp_[3]; } while (0)

#define IOR_COMPL_HARD_REG_SET(TO, FROM)  \
do { HARD_REG_ELT_TYPE *scan_tp_ = (TO), *scan_fp_ = (FROM); 	\
     scan_tp_[0] |= ~ scan_fp_[0];				\
     scan_tp_[1] |= ~ scan_fp_[1];				\
     scan_tp_[2] |= ~ scan_fp_[2];				\
     scan_tp_[3] |= ~ scan_fp_[3]; } while (0)

#define GO_IF_HARD_REG_SUBSET(X,Y,TO)  \
do { HARD_REG_ELT_TYPE *scan_xp_ = (X), *scan_yp_ = (Y); 	\
     if ((0 == (scan_xp_[0] & ~ scan_yp_[0]))			\
	 && (0 == (scan_xp_[1] & ~ scan_yp_[1]))		\
	 && (0 == (scan_xp_[2] & ~ scan_yp_[2]))		\
	 && (0 == (scan_xp_[3] & ~ scan_yp_[3])))		\
	goto TO; } while (0)

#define GO_IF_HARD_REG_EQUAL(X,Y,TO)  \
do { HARD_REG_ELT_TYPE *scan_xp_ = (X), *scan_yp_ = (Y); 	\
     if ((scan_xp_[0] == scan_yp_[0])				\
	 && (scan_xp_[1] == scan_yp_[1])			\
	 && (scan_xp_[2] == scan_yp_[2])			\
	 && (scan_xp_[3] == scan_yp_[3]))			\
	goto TO; } while (0)

#else /* FIRST_PSEUDO_REGISTER > 3*HOST_BITS_PER_WIDEST_FAST_INT */

#define CLEAR_HARD_REG_SET(TO)  \
do { HARD_REG_ELT_TYPE *scan_tp_ = (TO);			\
     int i;							\
     for (i = 0; i < HARD_REG_SET_LONGS; i++)			\
       *scan_tp_++ = 0; } while (0)

#define SET_HARD_REG_SET(TO)  \
do { HARD_REG_ELT_TYPE *scan_tp_ = (TO);			\
     int i;							\
     for (i = 0; i < HARD_REG_SET_LONGS; i++)			\
       *scan_tp_++ = -1; } while (0)

#define COPY_HARD_REG_SET(TO, FROM)  \
do { HARD_REG_ELT_TYPE *scan_tp_ = (TO), *scan_fp_ = (FROM); 	\
     int i;							\
     for (i = 0; i < HARD_REG_SET_LONGS; i++)			\
       *scan_tp_++ = *scan_fp_++; } while (0)

#define COMPL_HARD_REG_SET(TO, FROM)  \
do { HARD_REG_ELT_TYPE *scan_tp_ = (TO), *scan_fp_ = (FROM); 	\
     int i;							\
     for (i = 0; i < HARD_REG_SET_LONGS; i++)			\
       *scan_tp_++ = ~ *scan_fp_++; } while (0)

#define AND_HARD_REG_SET(TO, FROM)  \
do { HARD_REG_ELT_TYPE *scan_tp_ = (TO), *scan_fp_ = (FROM); 	\
     int i;							\
     for (i = 0; i < HARD_REG_SET_LONGS; i++)			\
       *scan_tp_++ &= *scan_fp_++; } while (0)

#define AND_COMPL_HARD_REG_SET(TO, FROM)  \
do { HARD_REG_ELT_TYPE *scan_tp_ = (TO), *scan_fp_ = (FROM); 	\
     int i;							\
     for (i = 0; i < HARD_REG_SET_LONGS; i++)			\
       *scan_tp_++ &= ~ *scan_fp_++; } while (0)

#define IOR_HARD_REG_SET(TO, FROM)  \
do { HARD_REG_ELT_TYPE *scan_tp_ = (TO), *scan_fp_ = (FROM); 	\
     int i;							\
     for (i = 0; i < HARD_REG_SET_LONGS; i++)			\
       *scan_tp_++ |= *scan_fp_++; } while (0)

#define IOR_COMPL_HARD_REG_SET(TO, FROM)  \
do { HARD_REG_ELT_TYPE *scan_tp_ = (TO), *scan_fp_ = (FROM); 	\
     int i;							\
     for (i = 0; i < HARD_REG_SET_LONGS; i++)			\
       *scan_tp_++ |= ~ *scan_fp_++; } while (0)

#define GO_IF_HARD_REG_SUBSET(X,Y,TO)  \
do { HARD_REG_ELT_TYPE *scan_xp_ = (X), *scan_yp_ = (Y); 	\
     int i;							\
     for (i = 0; i < HARD_REG_SET_LONGS; i++)			\
       if (0 != (*scan_xp_++ & ~ *scan_yp_++)) break;		\
     if (i == HARD_REG_SET_LONGS) goto TO; } while (0)

#define GO_IF_HARD_REG_EQUAL(X,Y,TO)  \
do { HARD_REG_ELT_TYPE *scan_xp_ = (X), *scan_yp_ = (Y); 	\
     int i;							\
     for (i = 0; i < HARD_REG_SET_LONGS; i++)			\
       if (*scan_xp_++ != *scan_yp_++) break;			\
     if (i == HARD_REG_SET_LONGS) goto TO; } while (0)

#endif
#endif
#endif
#endif

/* Define some standard sets of registers.  */

/* Indexed by hard register number, contains 1 for registers
   that are fixed use (stack pointer, pc, frame pointer, etc.).
   These are the registers that cannot be used to allocate
   a pseudo reg whose life does not cross calls.  */

extern char fixed_regs[FIRST_PSEUDO_REGISTER];

/* The same info as a HARD_REG_SET.  */

extern HARD_REG_SET fixed_reg_set;

/* Indexed by hard register number, contains 1 for registers
   that are fixed use or are clobbered by function calls.
   These are the registers that cannot be used to allocate
   a pseudo reg whose life crosses calls.  */

extern char call_used_regs[FIRST_PSEUDO_REGISTER];

#ifdef CALL_REALLY_USED_REGISTERS
extern char call_really_used_regs[];
#endif

/* The same info as a HARD_REG_SET.  */

extern HARD_REG_SET call_used_reg_set;
  
/* Registers that we don't want to caller save.  */
extern HARD_REG_SET losing_caller_save_reg_set;

/* Indexed by hard register number, contains 1 for registers that are
   fixed use -- i.e. in fixed_regs -- or a function value return register
   or TARGET_STRUCT_VALUE_RTX or STATIC_CHAIN_REGNUM.  These are the
   registers that cannot hold quantities across calls even if we are
   willing to save and restore them.  */

extern char call_fixed_regs[FIRST_PSEUDO_REGISTER];

/* The same info as a HARD_REG_SET.  */

extern HARD_REG_SET call_fixed_reg_set;

/* Indexed by hard register number, contains 1 for registers
   that are being used for global register decls.
   These must be exempt from ordinary flow analysis
   and are also considered fixed.  */

extern char global_regs[FIRST_PSEUDO_REGISTER];

/* Contains 1 for registers that are set or clobbered by calls.  */
/* ??? Ideally, this would be just call_used_regs plus global_regs, but
   for someone's bright idea to have call_used_regs strictly include
   fixed_regs.  Which leaves us guessing as to the set of fixed_regs
   that are actually preserved.  We know for sure that those associated
   with the local stack frame are safe, but scant others.  */

extern HARD_REG_SET regs_invalidated_by_call;

#ifdef REG_ALLOC_ORDER
/* Table of register numbers in the order in which to try to use them.  */

extern int reg_alloc_order[FIRST_PSEUDO_REGISTER];

/* The inverse of reg_alloc_order.  */

extern int inv_reg_alloc_order[FIRST_PSEUDO_REGISTER];
#endif

/* For each reg class, a HARD_REG_SET saying which registers are in it.  */

extern HARD_REG_SET reg_class_contents[N_REG_CLASSES];

/* For each reg class, number of regs it contains.  */

extern unsigned int reg_class_size[N_REG_CLASSES];

/* For each pair of reg classes,
   a largest reg class contained in their union.  */

extern enum reg_class reg_class_subunion[N_REG_CLASSES][N_REG_CLASSES];

/* For each pair of reg classes,
   the smallest reg class that contains their union.  */

extern enum reg_class reg_class_superunion[N_REG_CLASSES][N_REG_CLASSES];

/* Vector indexed by hardware reg giving its name.  */

extern const char * reg_names[FIRST_PSEUDO_REGISTER];

/* Vector indexed by reg class giving its name.  */

extern const char * reg_class_names[];

/* Given a hard REGN a FROM mode and a TO mode, return nonzero if
   REGN cannot change modes between the specified modes.  */
#define REG_CANNOT_CHANGE_MODE_P(REGN, FROM, TO)                          \
         CANNOT_CHANGE_MODE_CLASS (FROM, TO, REGNO_REG_CLASS (REGN))

#endif /* ! GCC_HARD_REG_SET_H */
