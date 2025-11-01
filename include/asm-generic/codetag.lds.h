/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef __ASM_GENERIC_CODETAG_LDS_H
#define __ASM_GENERIC_CODETAG_LDS_H

#ifdef CONFIG_MEM_ALLOC_PROFILING
#define IF_MEM_ALLOC_PROFILING(...) __VA_ARGS__
#else
#define IF_MEM_ALLOC_PROFILING(...)
#endif

#define SECTION_WITH_BOUNDARIES(_name)	\
	. = ALIGN(8);			\
	__start_##_name = .;		\
	KEEP(*(_name))			\
	__stop_##_name = .;

#define CODETAG_SECTIONS()		\
	IF_MEM_ALLOC_PROFILING(SECTION_WITH_BOUNDARIES(alloc_tags))

#define MOD_SEPARATE_CODETAG_SECTION(_name)	\
	.codetag.##_name : {			\
		SECTION_WITH_BOUNDARIES(_name)	\
	}

/*
 * For codetags which might be used after module unload, therefore might stay
 * longer in memory. Each such codetag type has its own section so that we can
 * unload them individually once unused.
 */
#define MOD_SEPARATE_CODETAG_SECTIONS()		\
	IF_MEM_ALLOC_PROFILING(MOD_SEPARATE_CODETAG_SECTION(alloc_tags))

#endif /* __ASM_GENERIC_CODETAG_LDS_H */
