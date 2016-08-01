#ifndef _SELFTESTS_POWERPC_PMU_EBB_LMR_H
#define _SELFTESTS_POWERPC_PMU_EBB_LMR_H

#include "reg.h"

#ifndef PPC_FEATURE2_ARCH_3_00
#define PPC_FEATURE2_ARCH_3_00 0x00800000
#endif

#define lmr_is_supported() have_hwcap2(PPC_FEATURE2_ARCH_3_00)

static inline void ebb_lmr_reset(void)
{
	unsigned long bescr = mfspr(SPRN_BESCR);
	bescr &= ~(BESCR_LMEO);
	bescr |= BESCR_LME;
	mtspr(SPRN_BESCR, bescr);
}

#define LDMX(t, a, b)\
	(0x7c00026a |				\
	 (((t) & 0x1f) << 21) |			\
	 (((a) & 0x1f) << 16) |			\
	 (((b) & 0x1f) << 11))

static inline unsigned long ldmx(unsigned long address)
{
	unsigned long ret;

	asm volatile ("mr 9, %1\r\n"
		      ".long " __stringify(LDMX(9, 0, 9)) "\r\n"
		      "mr %0, 9\r\n":"=r"(ret)
		      :"r"(address)
		      :"r9");

	return ret;
}

#endif
