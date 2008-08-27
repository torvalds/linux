/*
 * include/asm-blackfin/dpmc.h -  Miscellaneous IOCTL commands for Dynamic Power
 *   			 	Management Controller Driver.
 * Copyright (C) 2004-2008 Analog Device Inc.
 *
 */
#ifndef _BLACKFIN_DPMC_H_
#define _BLACKFIN_DPMC_H_

#ifdef __KERNEL__
#ifndef __ASSEMBLY__

void sleep_mode(u32 sic_iwr0, u32 sic_iwr1, u32 sic_iwr2);
void hibernate_mode(u32 sic_iwr0, u32 sic_iwr1, u32 sic_iwr2);
void sleep_deeper(u32 sic_iwr0, u32 sic_iwr1, u32 sic_iwr2);
void do_hibernate(int wakeup);
void set_dram_srfs(void);
void unset_dram_srfs(void);

#define VRPAIR(vlev, freq) (((vlev) << 16) | ((freq) >> 16))

struct bfin_dpmc_platform_data {
	const unsigned int *tuple_tab;
	unsigned short tabsize;
	unsigned short vr_settling_time; /* in us */
};

#else

#define PM_PUSH(x) \
	R0 = [P0 + (x - SRAM_BASE_ADDRESS)];\
	[--SP] =  R0;\

#define PM_POP(x) \
	R0 = [SP++];\
	[P0 + (x - SRAM_BASE_ADDRESS)] = R0;\

#define PM_SYS_PUSH(x) \
	R0 = [P0 + (x - PLL_CTL)];\
	[--SP] =  R0;\

#define PM_SYS_POP(x) \
	R0 = [SP++];\
	[P0 + (x - PLL_CTL)] = R0;\

#define PM_SYS_PUSH16(x) \
	R0 = w[P0 + (x - PLL_CTL)];\
	[--SP] =  R0;\

#define PM_SYS_POP16(x) \
	R0 = [SP++];\
	w[P0 + (x - PLL_CTL)] = R0;\

#endif
#endif	/* __KERNEL__ */

#endif	/*_BLACKFIN_DPMC_H_*/
