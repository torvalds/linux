#ifndef __ASM_POWERPC_FEATURE_FIXUPS_H
#define __ASM_POWERPC_FEATURE_FIXUPS_H

/*
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

#ifdef __ASSEMBLY__

/*
 * Feature section common macros
 *
 * Note that the entries now contain offsets between the table entry
 * and the code rather than absolute code pointers in order to be
 * useable with the vdso shared library. There is also an assumption
 * that values will be negative, that is, the fixup table has to be
 * located after the code it fixes up.
 */
#if defined(CONFIG_PPC64) && !defined(__powerpc64__)
/* 64 bits kernel, 32 bits code (ie. vdso32) */
#define FTR_ENTRY_LONG		.llong
#define FTR_ENTRY_OFFSET	.long 0xffffffff; .long
#else
/* 64 bit kernel 64 bit code, or 32 bit kernel 32 bit code */
#define FTR_ENTRY_LONG		PPC_LONG
#define FTR_ENTRY_OFFSET	PPC_LONG
#endif

#define MAKE_FTR_SECTION_ENTRY(msk, val, label, sect)	\
99:							\
	.section sect,"a";				\
	.align 3;					\
98:						       	\
	FTR_ENTRY_LONG msk;				\
	FTR_ENTRY_LONG val;				\
	FTR_ENTRY_OFFSET label##b-98b;			\
	FTR_ENTRY_OFFSET 99b-98b;		 	\
	.previous



/* CPU feature dependent sections */
#define BEGIN_FTR_SECTION_NESTED(label)	label:
#define BEGIN_FTR_SECTION		BEGIN_FTR_SECTION_NESTED(97)

#define END_FTR_SECTION_NESTED(msk, val, label) \
	MAKE_FTR_SECTION_ENTRY(msk, val, label, __ftr_fixup)

#define END_FTR_SECTION(msk, val)		\
	END_FTR_SECTION_NESTED(msk, val, 97)

#define END_FTR_SECTION_IFSET(msk)	END_FTR_SECTION((msk), (msk))
#define END_FTR_SECTION_IFCLR(msk)	END_FTR_SECTION((msk), 0)


/* Firmware feature dependent sections */
#define BEGIN_FW_FTR_SECTION_NESTED(label)	label:
#define BEGIN_FW_FTR_SECTION			BEGIN_FW_FTR_SECTION_NESTED(97)

#define END_FW_FTR_SECTION_NESTED(msk, val, label) \
	MAKE_FTR_SECTION_ENTRY(msk, val, label, __fw_ftr_fixup)

#define END_FW_FTR_SECTION(msk, val)		\
	END_FW_FTR_SECTION_NESTED(msk, val, 97)

#define END_FW_FTR_SECTION_IFSET(msk)	END_FW_FTR_SECTION((msk), (msk))
#define END_FW_FTR_SECTION_IFCLR(msk)	END_FW_FTR_SECTION((msk), 0)

#endif /* __ASSEMBLY__ */

#endif /* __ASM_POWERPC_FEATURE_FIXUPS_H */
