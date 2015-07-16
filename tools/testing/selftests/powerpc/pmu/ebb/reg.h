/*
 * Copyright 2014, Michael Ellerman, IBM Corp.
 * Licensed under GPLv2.
 */

#ifndef _SELFTESTS_POWERPC_REG_H
#define _SELFTESTS_POWERPC_REG_H

#define __stringify_1(x)        #x
#define __stringify(x)          __stringify_1(x)

#define mfspr(rn)       ({unsigned long rval; \
                         asm volatile("mfspr %0," __stringify(rn) \
                                 : "=r" (rval)); rval; })
#define mtspr(rn, v)    asm volatile("mtspr " __stringify(rn) ",%0" : \
                                    : "r" ((unsigned long)(v)) \
                                    : "memory")

#define mb()		asm volatile("sync" : : : "memory");

#define SPRN_MMCR2     769
#define SPRN_MMCRA     770
#define SPRN_MMCR0     779
#define   MMCR0_PMAO   0x00000080
#define   MMCR0_PMAE   0x04000000
#define   MMCR0_FC     0x80000000
#define SPRN_EBBHR     804
#define SPRN_EBBRR     805
#define SPRN_BESCR     806     /* Branch event status & control register */
#define SPRN_BESCRS    800     /* Branch event status & control set (1 bits set to 1) */
#define SPRN_BESCRSU   801     /* Branch event status & control set upper */
#define SPRN_BESCRR    802     /* Branch event status & control REset (1 bits set to 0) */
#define SPRN_BESCRRU   803     /* Branch event status & control REset upper */

#define BESCR_PMEO     0x1     /* PMU Event-based exception Occurred */
#define BESCR_PME      (0x1ul << 32) /* PMU Event-based exception Enable */

#define SPRN_PMC1      771
#define SPRN_PMC2      772
#define SPRN_PMC3      773
#define SPRN_PMC4      774
#define SPRN_PMC5      775
#define SPRN_PMC6      776

#define SPRN_SIAR      780
#define SPRN_SDAR      781
#define SPRN_SIER      768

#endif /* _SELFTESTS_POWERPC_REG_H */
