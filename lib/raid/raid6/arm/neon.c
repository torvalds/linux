// SPDX-License-Identifier: GPL-2.0-only
/*
 * RAID6 syndrome calculation using ARM NEON intrinsics
 *
 * Copyright (C) 2013 Linaro Ltd <ard.biesheuvel@linaro.org>
 */

#include <asm/simd.h>
#include "algos.h"

/*
 * There are 2 reasons these wrappers are kept in a separate compilation unit
 * from the actual implementations in neonN.c (generated from neon.uc by
 * unroll.awk):
 * - the actual implementations use NEON intrinsics, and the GCC support header
 *   (arm_neon.h) is not fully compatible (type wise) with the kernel;
 * - the neonN.c files are compiled with -mfpu=neon and optimization enabled,
 *   and we have to make sure that we never use *any* NEON/VFP instructions
 *   outside a kernel_neon_begin()/kernel_neon_end() pair.
 */

#define RAID6_NEON_WRAPPER(_n)						\
	static void raid6_neon ## _n ## _gen_syndrome(int disks,	\
					size_t bytes, void **ptrs)	\
	{								\
		void raid6_neon ## _n  ## _gen_syndrome_real(int,	\
						unsigned long, void**);	\
		scoped_ksimd()						\
			raid6_neon ## _n ## _gen_syndrome_real(disks,	\
					(unsigned long)bytes, ptrs);	\
	}								\
	static void raid6_neon ## _n ## _xor_syndrome(int disks,	\
					int start, int stop, 		\
					size_t bytes, void **ptrs)	\
	{								\
		void raid6_neon ## _n  ## _xor_syndrome_real(int,	\
				int, int, unsigned long, void**);	\
		scoped_ksimd()						\
			raid6_neon ## _n ## _xor_syndrome_real(disks,	\
				start, stop, (unsigned long)bytes, ptrs);\
	}								\
	struct raid6_calls const raid6_neonx ## _n = {			\
		.gen_syndrome	= raid6_neon ## _n ## _gen_syndrome,	\
		.xor_syndrome	= raid6_neon ## _n ## _xor_syndrome,	\
		.name		= "neonx" #_n,				\
	}

RAID6_NEON_WRAPPER(1);
RAID6_NEON_WRAPPER(2);
RAID6_NEON_WRAPPER(4);
RAID6_NEON_WRAPPER(8);
