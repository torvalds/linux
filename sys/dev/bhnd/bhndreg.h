/*-
 * SPDX-License-Identifier: ISC
 *
 * Copyright (c) 2015-2016 Landon Fuller <landon@landonf.org>
 * Copyright (c) 2010 Broadcom Corporation
 * All rights reserved.
 *
 * Portions of this file were derived from the sbchipc.h header contributed by
 * Broadcom to to the Linux staging repository, as well as later revisions of
 * sbchipc.h distributed with the Asus RT-N16 firmware source code release.
 * 
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY
 * SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION
 * OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN
 * CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 *
 * $FreeBSD$
 */

#ifndef _BHND_BHNDREG_H_
#define _BHND_BHNDREG_H_

/**
 * The default address at which the ChipCommon core is mapped on all siba(4)
 * devices, and most (all?) bcma(4) devices.
 */
#define	BHND_DEFAULT_CHIPC_ADDR	0x18000000

/**
 * The standard size of a primary BHND_PORT_DEVICE or BHND_PORT_AGENT
 * register block.
 */
#define	BHND_DEFAULT_CORE_SIZE	0x1000

/**
 * The standard size of the siba(4) and bcma(4) enumeration space.
 */
#define	BHND_DEFAULT_ENUM_SIZE	0x00100000

/*
 * Common per-core clock control/status register available on PMU-equipped
 * devices.
 * 
 * Clock Mode		Name	Description
 * High Throughput	(HT)	Full bandwidth, low latency. Generally supplied
 * 				from PLL.
 * Active Low Power	(ALP)	Register access, low speed DMA.
 * Idle Low Power	(ILP)	No interconnect activity, or if long latency
 * 				is permitted.
 */
#define BHND_CLK_CTL_ST			0x1e0		/**< clock control and status */
#define	BHND_CCS_FORCEALP		0x00000001	/**< force ALP request */
#define	BHND_CCS_FORCEHT		0x00000002	/**< force HT request */
#define	BHND_CCS_FORCEILP		0x00000004	/**< force ILP request */
#define	BHND_CCS_FORCE_MASK		0x0000000F

#define	BHND_CCS_ALPAREQ		0x00000008	/**< ALP Avail Request */
#define	BHND_CCS_HTAREQ			0x00000010	/**< HT Avail Request */
#define	BHND_CCS_AREQ_MASK		0x00000018

#define	BHND_CCS_FORCEHWREQOFF		0x00000020	/**< Force HW Clock Request Off */

#define	BHND_CCS_ERSRC_REQ_MASK		0x00000700	/**< external resource requests */
#define	BHND_CCS_ERSRC_REQ_SHIFT	8
#define	BHND_CCS_ERSRC_MAX		2		/**< maximum ERSRC value (corresponding to bits 0-2) */

#define	BHND_CCS_ALPAVAIL		0x00010000	/**< ALP is available */
#define	BHND_CCS_HTAVAIL		0x00020000	/**< HT is available */
#define	BHND_CCS_AVAIL_MASK		0x00030000

#define	BHND_CCS_BP_ON_APL		0x00040000	/**< RO: Backplane is running on ALP clock */
#define	BHND_CCS_BP_ON_HT		0x00080000	/**< RO: Backplane is running on HT clock */
#define	BHND_CCS_ERSRC_STS_MASK		0x07000000	/**< external resource status */
#define	BHND_CCS_ERSRC_STS_SHIFT	24

#define	BHND_CCS0_HTAVAIL		0x00010000	/**< HT avail in chipc and pcmcia on 4328a0 */
#define	BHND_CCS0_ALPAVAIL		0x00020000	/**< ALP avail in chipc and pcmcia on 4328a0 */

#define BHND_CCS_GET_FLAG(_value, _flag)	\
	(((_value) & _flag) != 0)
#define	BHND_CCS_GET_BITS(_value, _field)	\
	(((_value) & _field ## _MASK) >> _field ## _SHIFT)
#define	BHND_CCS_SET_BITS(_value, _field)	\
	(((_value) << _field ## _SHIFT) & _field ## _MASK)

#endif /* _BHND_BHNDREG_H_ */
