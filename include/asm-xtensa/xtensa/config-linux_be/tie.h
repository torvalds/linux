/*
 * xtensa/config/tie.h -- HAL definitions that are dependent on CORE and TIE configuration
 *
 *  This header file is sometimes referred to as the "compile-time HAL" or CHAL.
 *  It was generated for a specific Xtensa processor configuration,
 *  and furthermore for a specific set of TIE source files that extend
 *  basic core functionality.
 *
 *  Source for configuration-independent binaries (which link in a
 *  configuration-specific HAL library) must NEVER include this file.
 *  It is perfectly normal, however, for the HAL source itself to include this file.
 */

/*
 * Copyright (c) 2003 Tensilica, Inc.  All Rights Reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of version 2.1 of the GNU Lesser General Public
 * License as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it would be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 *
 * Further, this software is distributed without any warranty that it is
 * free of the rightful claim of any third person regarding infringement
 * or the like.  Any license provided herein, whether implied or
 * otherwise, applies only to this software file.  Patent licenses, if
 * any, provided herein do not apply to combinations of this program with
 * other software, or any other product whatsoever.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this program; if not, write the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston MA 02111-1307,
 * USA.
 */


#ifndef XTENSA_CONFIG_TIE_H
#define XTENSA_CONFIG_TIE_H

#include <xtensa/hal.h>


/*----------------------------------------------------------------------
				GENERAL
  ----------------------------------------------------------------------*/

/*
 *  Separators for macros that expand into arrays.
 *  These can be predefined by files that #include this one,
 *  when different separators are required.
 */
/*  Element separator for macros that expand into 1-dimensional arrays:  */
#ifndef XCHAL_SEP
#define XCHAL_SEP			,
#endif
/*  Array separator for macros that expand into 2-dimensional arrays:  */
#ifndef XCHAL_SEP2
#define XCHAL_SEP2			},{
#endif






/*----------------------------------------------------------------------
			COPROCESSORS and EXTRA STATE
  ----------------------------------------------------------------------*/

#define XCHAL_CP_NUM			0	/* number of coprocessors */
#define XCHAL_CP_MAX			0	/* max coprocessor id plus one (0 if none) */
#define XCHAL_CP_MASK			0x00	/* bitmask of coprocessors by id */

/*  Space for coprocessors' state save areas:  */
#define XCHAL_CP0_SA_SIZE		0
#define XCHAL_CP1_SA_SIZE		0
#define XCHAL_CP2_SA_SIZE		0
#define XCHAL_CP3_SA_SIZE		0
#define XCHAL_CP4_SA_SIZE		0
#define XCHAL_CP5_SA_SIZE		0
#define XCHAL_CP6_SA_SIZE		0
#define XCHAL_CP7_SA_SIZE		0
/*  Minimum required alignments of CP state save areas:  */
#define XCHAL_CP0_SA_ALIGN		1
#define XCHAL_CP1_SA_ALIGN		1
#define XCHAL_CP2_SA_ALIGN		1
#define XCHAL_CP3_SA_ALIGN		1
#define XCHAL_CP4_SA_ALIGN		1
#define XCHAL_CP5_SA_ALIGN		1
#define XCHAL_CP6_SA_ALIGN		1
#define XCHAL_CP7_SA_ALIGN		1

/*  Indexing macros:  */
#define _XCHAL_CP_SA_SIZE(n)		XCHAL_CP ## n ## _SA_SIZE
#define XCHAL_CP_SA_SIZE(n)		_XCHAL_CP_SA_SIZE(n)	/* n = 0 .. 7 */
#define _XCHAL_CP_SA_ALIGN(n)		XCHAL_CP ## n ## _SA_ALIGN
#define XCHAL_CP_SA_ALIGN(n)		_XCHAL_CP_SA_ALIGN(n)	/* n = 0 .. 7 */


/*  Space for "extra" state (user special registers and non-cp TIE) save area:  */
#define XCHAL_EXTRA_SA_SIZE		0
#define XCHAL_EXTRA_SA_ALIGN		1

/*  Total save area size (extra + all coprocessors)  */
/*  (not useful until xthal_{save,restore}_all_extra() is implemented,  */
/*   but included for Tor2 beta; doesn't account for alignment!):  */
#define XCHAL_CPEXTRA_SA_SIZE_TOR2	0	/* Tor2Beta temporary definition -- do not use */

/*  Combined required alignment for all CP and EXTRA state save areas  */
/*  (does not include required alignment for any base config registers):  */
#define XCHAL_CPEXTRA_SA_ALIGN		1

/* ... */


#ifdef _ASMLANGUAGE
/*
 *  Assembly-language specific definitions (assembly macros, etc.).
 */
#include <xtensa/config/specreg.h>

/********************
 *  Macros to save and restore the non-coprocessor TIE portion of EXTRA state.
 */

/* (none) */


/********************
 *  Macros to create functions that save and restore all EXTRA (non-coprocessor) state
 *  (does not include zero-overhead loop registers and non-optional registers).
 */

	/*
	 *  Macro that expands to the body of a function that
	 *  stores the extra (non-coprocessor) optional/custom state.
	 *	Entry:	a2 = ptr to save area in which to save extra state
	 *	Exit:	any register a2-a15 (?) may have been clobbered.
	 */
	.macro	xchal_extra_store_funcbody
	.endm


	/*
	 *  Macro that expands to the body of a function that
	 *  loads the extra (non-coprocessor) optional/custom state.
	 *	Entry:	a2 = ptr to save area from which to restore extra state
	 *	Exit:	any register a2-a15 (?) may have been clobbered.
	 */
	.macro	xchal_extra_load_funcbody
	.endm


/********************
 *  Macros to save and restore the state of each TIE coprocessor.
 */



/********************
 *  Macros to create functions that save and restore the state of *any* TIE coprocessor.
 */

	/*
	 *  Macro that expands to the body of a function
	 *  that stores the selected coprocessor's state (registers etc).
	 *	Entry:	a2 = ptr to save area in which to save cp state
	 *		a3 = coprocessor number
	 *	Exit:	any register a2-a15 (?) may have been clobbered.
	 */
	.macro	xchal_cpi_store_funcbody
	.endm


	/*
	 *  Macro that expands to the body of a function
	 *  that loads the selected coprocessor's state (registers etc).
	 *	Entry:	a2 = ptr to save area from which to restore cp state
	 *		a3 = coprocessor number
	 *	Exit:	any register a2-a15 (?) may have been clobbered.
	 */
	.macro	xchal_cpi_load_funcbody
	.endm

#endif /*_ASMLANGUAGE*/


/*
 *  Contents of save areas in terms of libdb register numbers.
 *  NOTE:  CONTENTS_LIBDB_{UREG,REGF} macros are not defined in this file;
 *  it is up to the user of this header file to define these macros
 *  usefully before each expansion of the CONTENTS_LIBDB macros.
 *  (Fields rsv[123] are reserved for future additions; they are currently
 *   set to zero but may be set to some useful values in the future.)
 *
 *	CONTENTS_LIBDB_SREG(libdbnum, offset, size, align, rsv1, name, sregnum, bitmask, rsv2, rsv3)
 *	CONTENTS_LIBDB_UREG(libdbnum, offset, size, align, rsv1, name, uregnum, bitmask, rsv2, rsv3)
 *	CONTENTS_LIBDB_REGF(libdbnum, offset, size, align, rsv1, name, index, numentries, contentsize, regname_base, regfile_name, rsv2, rsv3)
 */

#define XCHAL_EXTRA_SA_CONTENTS_LIBDB_NUM	0
#define XCHAL_EXTRA_SA_CONTENTS_LIBDB	/* empty */

#define XCHAL_CP0_SA_CONTENTS_LIBDB_NUM	0
#define XCHAL_CP0_SA_CONTENTS_LIBDB	/* empty */

#define XCHAL_CP1_SA_CONTENTS_LIBDB_NUM	0
#define XCHAL_CP1_SA_CONTENTS_LIBDB	/* empty */

#define XCHAL_CP2_SA_CONTENTS_LIBDB_NUM	0
#define XCHAL_CP2_SA_CONTENTS_LIBDB	/* empty */

#define XCHAL_CP3_SA_CONTENTS_LIBDB_NUM	0
#define XCHAL_CP3_SA_CONTENTS_LIBDB	/* empty */

#define XCHAL_CP4_SA_CONTENTS_LIBDB_NUM	0
#define XCHAL_CP4_SA_CONTENTS_LIBDB	/* empty */

#define XCHAL_CP5_SA_CONTENTS_LIBDB_NUM	0
#define XCHAL_CP5_SA_CONTENTS_LIBDB	/* empty */

#define XCHAL_CP6_SA_CONTENTS_LIBDB_NUM	0
#define XCHAL_CP6_SA_CONTENTS_LIBDB	/* empty */

#define XCHAL_CP7_SA_CONTENTS_LIBDB_NUM	0
#define XCHAL_CP7_SA_CONTENTS_LIBDB	/* empty */






/*----------------------------------------------------------------------
				MISC
  ----------------------------------------------------------------------*/

#if 0	/* is there something equivalent for user TIE? */
#define XCHAL_CORE_ID			"linux_be"	/* configuration's alphanumeric core identifier
							   (CoreID) set in the Xtensa Processor Generator */

#define XCHAL_BUILD_UNIQUE_ID		0x00003256	/* software build-unique ID (22-bit) */

/*  These definitions describe the hardware targeted by this software:  */
#define XCHAL_HW_CONFIGID0		0xC103D1FF	/* config ID reg 0 value (upper 32 of 64 bits) */
#define XCHAL_HW_CONFIGID1		0x00803256	/* config ID reg 1 value (lower 32 of 64 bits) */
#define XCHAL_CONFIGID0			XCHAL_HW_CONFIGID0	/* for backward compatibility only -- don't use! */
#define XCHAL_CONFIGID1			XCHAL_HW_CONFIGID1	/* for backward compatibility only -- don't use! */
#define XCHAL_HW_RELEASE_MAJOR		1050	/* major release of targeted hardware */
#define XCHAL_HW_RELEASE_MINOR		1	/* minor release of targeted hardware */
#define XCHAL_HW_RELEASE_NAME		"T1050.1"	/* full release name of targeted hardware */
#define XTHAL_HW_REL_T1050	1
#define XTHAL_HW_REL_T1050_1	1
#define XCHAL_HW_CONFIGID_RELIABLE	1
#endif /*0*/



/*----------------------------------------------------------------------
				ISA
  ----------------------------------------------------------------------*/

#if 0	/* these probably don't belong here, but are related to or implemented using TIE */
#define XCHAL_HAVE_BOOLEANS		0	/* 1 if booleans option configured, 0 otherwise */
/*  Misc instructions:  */
#define XCHAL_HAVE_MUL32		0	/* 1 if 32-bit integer multiply option configured, 0 otherwise */
#define XCHAL_HAVE_MUL32_HIGH		0	/* 1 if MUL32 option includes MULUH and MULSH, 0 otherwise */

#define XCHAL_HAVE_FP			0	/* 1 if floating point option configured, 0 otherwise */
#endif /*0*/


#endif /*XTENSA_CONFIG_TIE_H*/

