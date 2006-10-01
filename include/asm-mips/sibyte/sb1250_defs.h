/*  *********************************************************************
    *  SB1250 Board Support Package
    *
    *  Global constants and macros		File: sb1250_defs.h
    *
    *  This file contains macros and definitions used by the other
    *  include files.
    *
    *  SB1250 specification level:  User's manual 1/02/02
    *
    *********************************************************************
    *
    *  Copyright 2000,2001,2002,2003
    *  Broadcom Corporation. All rights reserved.
    *
    *  This program is free software; you can redistribute it and/or
    *  modify it under the terms of the GNU General Public License as
    *  published by the Free Software Foundation; either version 2 of
    *  the License, or (at your option) any later version.
    *
    *  This program is distributed in the hope that it will be useful,
    *  but WITHOUT ANY WARRANTY; without even the implied warranty of
    *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    *  GNU General Public License for more details.
    *
    *  You should have received a copy of the GNU General Public License
    *  along with this program; if not, write to the Free Software
    *  Foundation, Inc., 59 Temple Place, Suite 330, Boston,
    *  MA 02111-1307 USA
    ********************************************************************* */

#ifndef _SB1250_DEFS_H
#define _SB1250_DEFS_H

/*
 * These headers require ANSI C89 string concatenation, and GCC or other
 * 'long long' (64-bit integer) support.
 */
#if !defined(__STDC__) && !defined(_MSC_VER)
#error SiByte headers require ANSI C89 support
#endif


/*  *********************************************************************
    *  Macros for feature tests, used to enable include file features
    *  for chip features only present in certain chip revisions.
    *
    *  SIBYTE_HDR_FEATURES may be defined to be the mask value chip/revision
    *  which is to be exposed by the headers.  If undefined, it defaults to
    *  "all features."
    *
    *  Use like:
    *
    *    #define SIBYTE_HDR_FEATURES	SIBYTE_HDR_FMASK_112x_PASS1
    *
    *		Generate defines only for that revision of chip.
    *
    *    #if SIBYTE_HDR_FEATURE(chip,pass)
    *
    *		True if header features for that revision or later of
    *	        that particular chip type are enabled in SIBYTE_HDR_FEATURES.
    *	        (Use this to bracket #defines for features present in a given
    *		revision and later.)
    *
    *		Note that there is no implied ordering between chip types.
    *
    *		Note also that 'chip' and 'pass' must textually exactly
    *		match the defines below.  So, for example,
    *		SIBYTE_HDR_FEATURE(112x, PASS1) is OK, but
    *		SIBYTE_HDR_FEATURE(1120, pass1) is not (for two reasons).
    *
    *    #if SIBYTE_HDR_FEATURE_UP_TO(chip,pass)
    *
    *		Same as SIBYTE_HDR_FEATURE, but true for the named revision
    *		and earlier revisions of the named chip type.
    *
    *    #if SIBYTE_HDR_FEATURE_EXACT(chip,pass)
    *
    *		Same as SIBYTE_HDR_FEATURE, but only true for the named
    *		revision of the named chip type.  (Note that this CANNOT
    *		be used to verify that you're compiling only for that
    *		particular chip/revision.  It will be true any time this
    *		chip/revision is included in SIBYTE_HDR_FEATURES.)
    *
    *    #if SIBYTE_HDR_FEATURE_CHIP(chip)
    *
    *		True if header features for (any revision of) that chip type
    *		are enabled in SIBYTE_HDR_FEATURES.  (Use this to bracket
    *		#defines for features specific to a given chip type.)
    *
    *  Mask values currently include room for additional revisions of each
    *  chip type, but can be renumbered at will.  Note that they MUST fit
    *  into 31 bits and may not include C type constructs, for safe use in
    *  CPP conditionals.  Bit positions within chip types DO indicate
    *  ordering, so be careful when adding support for new minor revs.
    ********************************************************************* */

#define	SIBYTE_HDR_FMASK_1250_ALL		0x000000ff
#define	SIBYTE_HDR_FMASK_1250_PASS1		0x00000001
#define	SIBYTE_HDR_FMASK_1250_PASS2		0x00000002
#define	SIBYTE_HDR_FMASK_1250_PASS3		0x00000004

#define	SIBYTE_HDR_FMASK_112x_ALL		0x00000f00
#define	SIBYTE_HDR_FMASK_112x_PASS1		0x00000100

#define SIBYTE_HDR_FMASK_1480_ALL		0x0000f000
#define SIBYTE_HDR_FMASK_1480_PASS1		0x00001000
#define SIBYTE_HDR_FMASK_1480_PASS2		0x00002000

/* Bit mask for chip/revision.  (use _ALL for all revisions of a chip).  */
#define	SIBYTE_HDR_FMASK(chip, pass)					\
    (SIBYTE_HDR_FMASK_ ## chip ## _ ## pass)
#define	SIBYTE_HDR_FMASK_ALLREVS(chip)					\
    (SIBYTE_HDR_FMASK_ ## chip ## _ALL)

/* Default constant value for all chips, all revisions */
#define	SIBYTE_HDR_FMASK_ALL						\
    (SIBYTE_HDR_FMASK_1250_ALL | SIBYTE_HDR_FMASK_112x_ALL		\
     | SIBYTE_HDR_FMASK_1480_ALL)

/* This one is used for the "original" BCM1250/BCM112x chips.  We use this
   to weed out constants and macros that do not exist on later chips like
   the BCM1480  */
#define SIBYTE_HDR_FMASK_1250_112x_ALL					\
    (SIBYTE_HDR_FMASK_1250_ALL | SIBYTE_HDR_FMASK_112x_ALL)
#define SIBYTE_HDR_FMASK_1250_112x SIBYTE_HDR_FMASK_1250_112x_ALL

#ifndef SIBYTE_HDR_FEATURES
#define	SIBYTE_HDR_FEATURES			SIBYTE_HDR_FMASK_ALL
#endif


/* Bit mask for revisions of chip exclusively before the named revision.  */
#define	SIBYTE_HDR_FMASK_BEFORE(chip, pass)				\
    ((SIBYTE_HDR_FMASK(chip, pass) - 1) & SIBYTE_HDR_FMASK_ALLREVS(chip))

/* Bit mask for revisions of chip exclusively after the named revision.  */
#define	SIBYTE_HDR_FMASK_AFTER(chip, pass)				\
    (~(SIBYTE_HDR_FMASK(chip, pass)					\
     | (SIBYTE_HDR_FMASK(chip, pass) - 1)) & SIBYTE_HDR_FMASK_ALLREVS(chip))


/* True if header features enabled for (any revision of) that chip type.  */
#define SIBYTE_HDR_FEATURE_CHIP(chip)					\
    (!! (SIBYTE_HDR_FMASK_ALLREVS(chip) & SIBYTE_HDR_FEATURES))

/* True for all versions of the BCM1250 and BCM1125, but not true for
   anything else */
#define SIBYTE_HDR_FEATURE_1250_112x \
      (SIBYTE_HDR_FEATURE_CHIP(1250) || SIBYTE_HDR_FEATURE_CHIP(112x))
/*    (!!  (SIBYTE_HDR_FEATURES & SIBYHTE_HDR_FMASK_1250_112x)) */

/* True if header features enabled for that rev or later, inclusive.  */
#define SIBYTE_HDR_FEATURE(chip, pass)					\
    (!! ((SIBYTE_HDR_FMASK(chip, pass)					\
	  | SIBYTE_HDR_FMASK_AFTER(chip, pass)) & SIBYTE_HDR_FEATURES))

/* True if header features enabled for exactly that rev.  */
#define SIBYTE_HDR_FEATURE_EXACT(chip, pass)				\
    (!! (SIBYTE_HDR_FMASK(chip, pass) & SIBYTE_HDR_FEATURES))

/* True if header features enabled for that rev or before, inclusive.  */
#define SIBYTE_HDR_FEATURE_UP_TO(chip, pass)				\
    (!! ((SIBYTE_HDR_FMASK(chip, pass)					\
	 | SIBYTE_HDR_FMASK_BEFORE(chip, pass)) & SIBYTE_HDR_FEATURES))


/*  *********************************************************************
    *  Naming schemes for constants in these files:
    *
    *  M_xxx           MASK constant (identifies bits in a register).
    *                  For multi-bit fields, all bits in the field will
    *                  be set.
    *
    *  K_xxx           "Code" constant (value for data in a multi-bit
    *                  field).  The value is right justified.
    *
    *  V_xxx           "Value" constant.  This is the same as the
    *                  corresponding "K_xxx" constant, except it is
    *                  shifted to the correct position in the register.
    *
    *  S_xxx           SHIFT constant.  This is the number of bits that
    *                  a field value (code) needs to be shifted
    *                  (towards the left) to put the value in the right
    *                  position for the register.
    *
    *  A_xxx           ADDRESS constant.  This will be a physical
    *                  address.  Use the PHYS_TO_K1 macro to generate
    *                  a K1SEG address.
    *
    *  R_xxx           RELATIVE offset constant.  This is an offset from
    *                  an A_xxx constant (usually the first register in
    *                  a group).
    *
    *  G_xxx(X)        GET value.  This macro obtains a multi-bit field
    *                  from a register, masks it, and shifts it to
    *                  the bottom of the register (retrieving a K_xxx
    *                  value, for example).
    *
    *  V_xxx(X)        VALUE.  This macro computes the value of a
    *                  K_xxx constant shifted to the correct position
    *                  in the register.
    ********************************************************************* */




/*
 * Cast to 64-bit number.  Presumably the syntax is different in
 * assembly language.
 *
 * Note: you'll need to define uint32_t and uint64_t in your headers.
 */

#if !defined(__ASSEMBLY__)
#define _SB_MAKE64(x) ((uint64_t)(x))
#define _SB_MAKE32(x) ((uint32_t)(x))
#else
#define _SB_MAKE64(x) (x)
#define _SB_MAKE32(x) (x)
#endif


/*
 * Make a mask for 1 bit at position 'n'
 */

#define _SB_MAKEMASK1(n) (_SB_MAKE64(1) << _SB_MAKE64(n))
#define _SB_MAKEMASK1_32(n) (_SB_MAKE32(1) << _SB_MAKE32(n))

/*
 * Make a mask for 'v' bits at position 'n'
 */

#define _SB_MAKEMASK(v,n) (_SB_MAKE64((_SB_MAKE64(1)<<(v))-1) << _SB_MAKE64(n))
#define _SB_MAKEMASK_32(v,n) (_SB_MAKE32((_SB_MAKE32(1)<<(v))-1) << _SB_MAKE32(n))

/*
 * Make a value at 'v' at bit position 'n'
 */

#define _SB_MAKEVALUE(v,n) (_SB_MAKE64(v) << _SB_MAKE64(n))
#define _SB_MAKEVALUE_32(v,n) (_SB_MAKE32(v) << _SB_MAKE32(n))

#define _SB_GETVALUE(v,n,m) ((_SB_MAKE64(v) & _SB_MAKE64(m)) >> _SB_MAKE64(n))
#define _SB_GETVALUE_32(v,n,m) ((_SB_MAKE32(v) & _SB_MAKE32(m)) >> _SB_MAKE32(n))

/*
 * Macros to read/write on-chip registers
 * XXX should we do the PHYS_TO_K1 here?
 */


#if defined(__mips64) && !defined(__ASSEMBLY__)
#define SBWRITECSR(csr,val) *((volatile uint64_t *) PHYS_TO_K1(csr)) = (val)
#define SBREADCSR(csr) (*((volatile uint64_t *) PHYS_TO_K1(csr)))
#endif /* __ASSEMBLY__ */

#endif
