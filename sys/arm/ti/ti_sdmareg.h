/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2011
 *	Ben Gray <ben.r.gray@gmail.com>.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $FreeBSD$
 */
#ifndef	__TI_SDMAREG_H__
#define	__TI_SDMAREG_H__

/**
 * The number of DMA channels possible on the controller.
 */
#define NUM_DMA_CHANNELS	32
#define NUM_DMA_IRQS		4

/**
 * Register offsets
 */
#define DMA4_REVISION                            0x0000
#define DMA4_IRQSTATUS_L(j)                     (0x0008 + ((j) * 0x4))
#define DMA4_IRQENABLE_L(j)                     (0x0018 + ((j) * 0x4))
#define DMA4_SYSSTATUS                           0x0028
#define DMA4_OCP_SYSCONFIG                       0x002C
#define DMA4_CAPS_0                              0x0064
#define DMA4_CAPS_2                              0x006C
#define DMA4_CAPS_3                              0x0070
#define DMA4_CAPS_4                              0x0074
#define DMA4_GCR                                 0x0078
#define DMA4_CCR(i)                             (0x0080 + ((i) * 0x60))
#define DMA4_CLNK_CTRL(i)                       (0x0084 + ((i) * 0x60))
#define DMA4_CICR(i)                            (0x0088 + ((i) * 0x60))
#define DMA4_CSR(i)                             (0x008C + ((i) * 0x60))
#define DMA4_CSDP(i)                            (0x0090 + ((i) * 0x60))
#define DMA4_CEN(i)                             (0x0094 + ((i) * 0x60))
#define DMA4_CFN(i)                             (0x0098 + ((i) * 0x60))
#define DMA4_CSSA(i)                            (0x009C + ((i) * 0x60))
#define DMA4_CDSA(i)                            (0x00A0 + ((i) * 0x60))
#define DMA4_CSE(i)                             (0x00A4 + ((i) * 0x60))
#define DMA4_CSF(i)                             (0x00A8 + ((i) * 0x60))
#define DMA4_CDE(i)                             (0x00AC + ((i) * 0x60))
#define DMA4_CDF(i)                             (0x00B0 + ((i) * 0x60))
#define DMA4_CSAC(i)                            (0x00B4 + ((i) * 0x60))
#define DMA4_CDAC(i)                            (0x00B8 + ((i) * 0x60))
#define DMA4_CCEN(i)                            (0x00BC + ((i) * 0x60))
#define DMA4_CCFN(i)                            (0x00C0 + ((i) * 0x60))
#define DMA4_COLOR(i)                           (0x00C4 + ((i) * 0x60))

/* The following register are only defined on OMAP44xx (and newer?) */
#define DMA4_CDP(i)                             (0x00D0 + ((i) * 0x60))
#define DMA4_CNDP(i)                            (0x00D4 + ((i) * 0x60))
#define DMA4_CCDN(i)                            (0x00D8 + ((i) * 0x60))

/**
 * Various register field settings
 */
#define DMA4_CSDP_DATA_TYPE(x)                  (((x) & 0x3) << 0)
#define DMA4_CSDP_SRC_BURST_MODE(x)             (((x) & 0x3) << 7)
#define DMA4_CSDP_DST_BURST_MODE(x)             (((x) & 0x3) << 14)
#define DMA4_CSDP_SRC_ENDIANISM(x)              (((x) & 0x1) << 21)
#define DMA4_CSDP_DST_ENDIANISM(x)              (((x) & 0x1) << 19)
#define DMA4_CSDP_WRITE_MODE(x)                 (((x) & 0x3) << 16)
#define DMA4_CSDP_SRC_PACKED(x)                 (((x) & 0x1) << 6)
#define DMA4_CSDP_DST_PACKED(x)                 (((x) & 0x1) << 13)

#define DMA4_CCR_DST_ADDRESS_MODE(x)            (((x) & 0x3) << 14)
#define DMA4_CCR_SRC_ADDRESS_MODE(x)            (((x) & 0x3) << 12)
#define DMA4_CCR_READ_PRIORITY(x)               (((x) & 0x1) << 6)
#define DMA4_CCR_WRITE_PRIORITY(x)              (((x) & 0x1) << 26)
#define DMA4_CCR_SYNC_TRIGGER(x)                ((((x) & 0x60) << 14) \
                                                 | ((x) & 0x1f))
#define	DMA4_CCR_FRAME_SYNC(x)                  (((x) & 0x1) << 5)
#define	DMA4_CCR_BLOCK_SYNC(x)                  (((x) & 0x1) << 18)
#define DMA4_CCR_SEL_SRC_DST_SYNC(x)            (((x) & 0x1) << 24)

#define DMA4_CCR_PACKET_TRANS                   (DMA4_CCR_FRAME_SYNC(1) | \
                                                 DMA4_CCR_BLOCK_SYNC(1) )

#define DMA4_CSR_DROP                           (1UL << 1)
#define DMA4_CSR_HALF                           (1UL << 2)
#define DMA4_CSR_FRAME                          (1UL << 3)
#define DMA4_CSR_LAST                           (1UL << 4)
#define DMA4_CSR_BLOCK                          (1UL << 5)
#define DMA4_CSR_SYNC                           (1UL << 6)
#define DMA4_CSR_PKT                            (1UL << 7)
#define DMA4_CSR_TRANS_ERR                      (1UL << 8)
#define DMA4_CSR_SECURE_ERR                     (1UL << 9)
#define DMA4_CSR_SUPERVISOR_ERR                 (1UL << 10)
#define DMA4_CSR_MISALIGNED_ADRS_ERR            (1UL << 11)
#define DMA4_CSR_DRAIN_END                      (1UL << 12)
#define DMA4_CSR_CLEAR_MASK                     (0xffe)

#define DMA4_CICR_DROP_IE                       (1UL << 1)
#define DMA4_CICR_HALF_IE                       (1UL << 2)
#define DMA4_CICR_FRAME_IE                      (1UL << 3)
#define DMA4_CICR_LAST_IE                       (1UL << 4)
#define DMA4_CICR_BLOCK_IE                      (1UL << 5)
#define DMA4_CICR_PKT_IE                        (1UL << 7)
#define DMA4_CICR_TRANS_ERR_IE                  (1UL << 8)
#define DMA4_CICR_SECURE_ERR_IE                 (1UL << 9)
#define DMA4_CICR_SUPERVISOR_ERR_IE             (1UL << 10)
#define DMA4_CICR_MISALIGNED_ADRS_ERR_IE        (1UL << 11)
#define DMA4_CICR_DRAIN_IE                      (1UL << 12)

/**
 *	The following H/W revision values were found be experimentation, TI don't
 *	publish the revision numbers.  The TRM says "TI internal Data".
 */
#define DMA4_OMAP3_REV                          0x00000040
#define DMA4_OMAP4_REV                          0x00010900

#endif	/* __TI_SDMAREG_H__ */
