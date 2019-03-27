/***********************license start***************
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2003-2010  Cavium Networks (support@cavium.com). All rights
 * reserved.
 *
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 *   * Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 *
 *   * Redistributions in binary form must reproduce the above
 *     copyright notice, this list of conditions and the following
 *     disclaimer in the documentation and/or other materials provided
 *     with the distribution.

 *   * Neither the name of Cavium Networks nor the names of
 *     its contributors may be used to endorse or promote products
 *     derived from this software without specific prior written
 *     permission.

 * This Software, including technical data, may be subject to U.S. export  control
 * laws, including the U.S. Export Administration Act and its  associated
 * regulations, and may be subject to export or import  regulations in other
 * countries.

 * TO THE MAXIMUM EXTENT PERMITTED BY LAW, THE SOFTWARE IS PROVIDED "AS IS"
 * AND WITH ALL FAULTS AND CAVIUM  NETWORKS MAKES NO PROMISES, REPRESENTATIONS OR
 * WARRANTIES, EITHER EXPRESS, IMPLIED, STATUTORY, OR OTHERWISE, WITH RESPECT TO
 * THE SOFTWARE, INCLUDING ITS CONDITION, ITS CONFORMITY TO ANY REPRESENTATION OR
 * DESCRIPTION, OR THE EXISTENCE OF ANY LATENT OR PATENT DEFECTS, AND CAVIUM
 * SPECIFICALLY DISCLAIMS ALL IMPLIED (IF ANY) WARRANTIES OF TITLE,
 * MERCHANTABILITY, NONINFRINGEMENT, FITNESS FOR A PARTICULAR PURPOSE, LACK OF
 * VIRUSES, ACCURACY OR COMPLETENESS, QUIET ENJOYMENT, QUIET POSSESSION OR
 * CORRESPONDENCE TO DESCRIPTION. THE ENTIRE  RISK ARISING OUT OF USE OR
 * PERFORMANCE OF THE SOFTWARE LIES WITH YOU.
 ***********************license end**************************************/

#ifndef __OCTEON_IRQ_H__
#define __OCTEON_IRQ_H__

/*
 * $FreeBSD$
 */

/**
 * Enumeration of Interrupt numbers
 */
typedef enum
{
    /* 0 - 7 represent the 8 MIPS standard interrupt sources */
    OCTEON_IRQ_SW0        = 0,
    OCTEON_IRQ_SW1        = 1,
    OCTEON_IRQ_CIU0       = 2,
    OCTEON_IRQ_CIU1       = 3,
    OCTEON_IRQ_4          = 4,
    OCTEON_IRQ_5          = 5,
    OCTEON_IRQ_6          = 6,
    OCTEON_IRQ_7          = 7,

    /* 8 - 71 represent the sources in CIU_INTX_EN0 */
    OCTEON_IRQ_WORKQ0     = 8,
    OCTEON_IRQ_WORKQ1     = 9,
    OCTEON_IRQ_WORKQ2     = 10,
    OCTEON_IRQ_WORKQ3     = 11,
    OCTEON_IRQ_WORKQ4     = 12,
    OCTEON_IRQ_WORKQ5     = 13,
    OCTEON_IRQ_WORKQ6     = 14,
    OCTEON_IRQ_WORKQ7     = 15,
    OCTEON_IRQ_WORKQ8     = 16,
    OCTEON_IRQ_WORKQ9     = 17,
    OCTEON_IRQ_WORKQ10    = 18,
    OCTEON_IRQ_WORKQ11    = 19,
    OCTEON_IRQ_WORKQ12    = 20,
    OCTEON_IRQ_WORKQ13    = 21,
    OCTEON_IRQ_WORKQ14    = 22,
    OCTEON_IRQ_WORKQ15    = 23,
    OCTEON_IRQ_GPIO0      = 24,
    OCTEON_IRQ_GPIO1      = 25,
    OCTEON_IRQ_GPIO2      = 26,
    OCTEON_IRQ_GPIO3      = 27,
    OCTEON_IRQ_GPIO4      = 28,
    OCTEON_IRQ_GPIO5      = 29,
    OCTEON_IRQ_GPIO6      = 30,
    OCTEON_IRQ_GPIO7      = 31,
    OCTEON_IRQ_GPIO8      = 32,
    OCTEON_IRQ_GPIO9      = 33,
    OCTEON_IRQ_GPIO10     = 34,
    OCTEON_IRQ_GPIO11     = 35,
    OCTEON_IRQ_GPIO12     = 36,
    OCTEON_IRQ_GPIO13     = 37,
    OCTEON_IRQ_GPIO14     = 38,
    OCTEON_IRQ_GPIO15     = 39,
    OCTEON_IRQ_MBOX0      = 40,
    OCTEON_IRQ_MBOX1      = 41,
    OCTEON_IRQ_UART0      = 42,
    OCTEON_IRQ_UART1      = 43,
    OCTEON_IRQ_PCI_INT0   = 44,
    OCTEON_IRQ_PCI_INT1   = 45,
    OCTEON_IRQ_PCI_INT2   = 46,
    OCTEON_IRQ_PCI_INT3   = 47,
    OCTEON_IRQ_PCI_MSI0   = 48,
    OCTEON_IRQ_PCI_MSI1   = 49,
    OCTEON_IRQ_PCI_MSI2   = 50,
    OCTEON_IRQ_PCI_MSI3   = 51,
    OCTEON_IRQ_RESERVED44 = 52,
    OCTEON_IRQ_TWSI       = 53,
    OCTEON_IRQ_RML        = 54,
    OCTEON_IRQ_TRACE      = 55,
    OCTEON_IRQ_GMX_DRP0   = 56,
    OCTEON_IRQ_GMX_DRP1   = 57,   /* Doesn't apply on CN52XX or CN63XX */
    OCTEON_IRQ_IPD_DRP    = 58,
    OCTEON_IRQ_KEY_ZERO   = 59,   /* Doesn't apply on CN52XX or CN63XX */
    OCTEON_IRQ_TIMER0     = 60,
    OCTEON_IRQ_TIMER1     = 61,
    OCTEON_IRQ_TIMER2     = 62,
    OCTEON_IRQ_TIMER3     = 63,
    OCTEON_IRQ_USB0       = 64,   /* Doesn't apply on CN38XX or CN58XX */
    OCTEON_IRQ_PCM        = 65,   /* Doesn't apply on CN52XX or CN63XX */
    OCTEON_IRQ_MPI        = 66,   /* Doesn't apply on CN52XX or CN63XX */
    OCTEON_IRQ_TWSI2      = 67,   /* Added in CN56XX */
    OCTEON_IRQ_POWIQ      = 68,   /* Added in CN56XX */
    OCTEON_IRQ_IPDPPTHR   = 69,   /* Added in CN56XX */
    OCTEON_IRQ_MII        = 70,   /* Added in CN56XX */
    OCTEON_IRQ_BOOTDMA    = 71,   /* Added in CN56XX */

    /* 72 - 135 represent the sources in CIU_INTX_EN1 */
    OCTEON_IRQ_WDOG0 = 72,
    OCTEON_IRQ_WDOG1 = 73,
    OCTEON_IRQ_WDOG2 = 74,
    OCTEON_IRQ_WDOG3 = 75,
    OCTEON_IRQ_WDOG4 = 76,
    OCTEON_IRQ_WDOG5 = 77,
    OCTEON_IRQ_WDOG6 = 78,
    OCTEON_IRQ_WDOG7 = 79,
    OCTEON_IRQ_WDOG8 = 80,
    OCTEON_IRQ_WDOG9 = 81,
    OCTEON_IRQ_WDOG10= 82,
    OCTEON_IRQ_WDOG11= 83,
    OCTEON_IRQ_WDOG12= 84,
    OCTEON_IRQ_WDOG13= 85,
    OCTEON_IRQ_WDOG14= 86,
    OCTEON_IRQ_WDOG15= 87,
    OCTEON_IRQ_UART2 = 88,           /* Added in CN52XX */
    OCTEON_IRQ_USB1  = 89,           /* Added in CN52XX */
    OCTEON_IRQ_MII1  = 90,           /* Added in CN52XX */
    OCTEON_IRQ_NAND  = 91,           /* Added in CN52XX */
    OCTEON_IRQ_MIO   = 92,           /* Added in CN63XX */
    OCTEON_IRQ_IOB   = 93,           /* Added in CN63XX */
    OCTEON_IRQ_FPA   = 94,           /* Added in CN63XX */
    OCTEON_IRQ_POW   = 95,           /* Added in CN63XX */
    OCTEON_IRQ_L2C   = 96,           /* Added in CN63XX */
    OCTEON_IRQ_IPD   = 97,           /* Added in CN63XX */
    OCTEON_IRQ_PIP   = 98,           /* Added in CN63XX */
    OCTEON_IRQ_PKO   = 99,           /* Added in CN63XX */
    OCTEON_IRQ_ZIP   = 100,          /* Added in CN63XX */
    OCTEON_IRQ_TIM   = 101,          /* Added in CN63XX */
    OCTEON_IRQ_RAD   = 102,          /* Added in CN63XX */
    OCTEON_IRQ_KEY   = 103,          /* Added in CN63XX */
    OCTEON_IRQ_DFA   = 104,          /* Added in CN63XX */
    OCTEON_IRQ_USB   = 105,          /* Added in CN63XX */
    OCTEON_IRQ_SLI   = 106,          /* Added in CN63XX */
    OCTEON_IRQ_DPI   = 107,          /* Added in CN63XX */
    OCTEON_IRQ_AGX0  = 108,          /* Added in CN63XX */
    /* 109 - 117 are reserved */
    OCTEON_IRQ_AGL   = 118,          /* Added in CN63XX */
    OCTEON_IRQ_PTP   = 119,          /* Added in CN63XX */
    OCTEON_IRQ_PEM0  = 120,          /* Added in CN63XX */
    OCTEON_IRQ_PEM1  = 121,          /* Added in CN63XX */
    OCTEON_IRQ_SRIO0 = 122,          /* Added in CN63XX */
    OCTEON_IRQ_SRIO1 = 123,          /* Added in CN63XX */
    OCTEON_IRQ_LMC0  = 124,          /* Added in CN63XX */
    /* Interrupts 125 - 127 are reserved */
    OCTEON_IRQ_DFM   = 128,          /* Added in CN63XX */
    /* Interrupts 129 - 135 are reserved */
} octeon_irq_t;

#define	OCTEON_PMC_IRQ	OCTEON_IRQ_4

#endif
