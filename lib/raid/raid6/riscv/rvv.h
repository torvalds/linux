/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Copyright 2024 Institute of Software, CAS.
 *
 * Definitions for RISC-V RAID-6 code
 */

#include <asm/vector.h>
#include "algos.h"

#define RAID6_RVV_WRAPPER(_n)						\
	static void raid6_rvv ## _n ## _gen_syndrome(int disks,		\
					size_t bytes, void **ptrs)	\
	{								\
		void raid6_rvv ## _n  ## _gen_syndrome_real(int d,	\
					unsigned long b, void **p);	\
		kernel_vector_begin();					\
		raid6_rvv ## _n ## _gen_syndrome_real(disks,		\
				(unsigned long)bytes, ptrs);		\
		kernel_vector_end();					\
	}								\
	static void raid6_rvv ## _n ## _xor_syndrome(int disks,		\
					int start, int stop,		\
					size_t bytes, void **ptrs)	\
	{								\
		void raid6_rvv ## _n  ## _xor_syndrome_real(int d,	\
					int s1, int s2,			\
					unsigned long b, void **p);	\
		kernel_vector_begin();					\
		raid6_rvv ## _n ## _xor_syndrome_real(disks,		\
			start, stop, (unsigned long)bytes, ptrs);	\
		kernel_vector_end();					\
	}								\
	struct raid6_calls const raid6_rvvx ## _n = {			\
		.gen_syndrome	= raid6_rvv ## _n ## _gen_syndrome,	\
		.xor_syndrome	= raid6_rvv ## _n ## _xor_syndrome,	\
		.name		= "rvvx" #_n,				\
	}
