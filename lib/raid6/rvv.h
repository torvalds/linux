/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Copyright 2024 Institute of Software, CAS.
 *
 * raid6/rvv.h
 *
 * Definitions for RISC-V RAID-6 code
 */

#ifdef __KERNEL__
#include <asm/vector.h>
#else
#define kernel_vector_begin()
#define kernel_vector_end()
#include <sys/auxv.h>
#include <asm/hwcap.h>
#define has_vector() (getauxval(AT_HWCAP) & COMPAT_HWCAP_ISA_V)
#endif

#include <linux/raid/pq.h>

static int rvv_has_vector(void)
{
	return has_vector();
}

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
		raid6_rvv ## _n ## _gen_syndrome,			\
		raid6_rvv ## _n ## _xor_syndrome,			\
		rvv_has_vector,						\
		"rvvx" #_n,						\
		0							\
	}
