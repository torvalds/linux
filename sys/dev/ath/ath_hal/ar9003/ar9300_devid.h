/*-
 * SPDX-License-Identifier: BSD-4-Clause
 *
 * Copyright (c) 2012, Qualcomm Atheros, Inc.  All rights reserved.
 *
 * Redistribution and use in source and binary forms are permitted
 * provided that the following conditions are met:
 * 1. The materials contained herein are unmodified and are used
 *    unmodified.
 * 2. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following NO
 *    ''WARRANTY'' disclaimer below (''Disclaimer''), without
 *    modification.
 * 3. Redistributions in binary form must reproduce at minimum a
 *    disclaimer similar to the Disclaimer below and any redistribution
 *    must be conditioned upon including a substantially similar
 *    Disclaimer requirement for further binary redistribution.
 * 4. Neither the names of the above-listed copyright holders nor the
 *    names of any contributors may be used to endorse or promote
 *    product derived from this software without specific prior written
 *    permission.
 *
 * NO WARRANTY
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * ''AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF NONINFRINGEMENT,
 * MERCHANTIBILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE COPYRIGHT HOLDERS OR CONTRIBUTORS BE LIABLE
 * FOR SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF
 * USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT
 * OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGES.
 *
 * $FreeBSD$
 *
 */
#ifndef	__AR9300_DEVID_H__
#define	__AR9300_DEVID_H__

/*
 * AR9380 HAL device IDs.
 */

/*
 * MAC Version and Revision
 */
#define	AR_SREV_VERSION_AR9380		0x1C0
#define	AR_SREV_VERSION_AR9580		0x1C0
#define	AR_SREV_VERSION_AR9460		0x280
#define	AR_SREV_VERSION_QCA9565		0x2c0

#define	AR_SREV_VERSION_AR9330		0x200
#define	AR_SREV_VERSION_AR9340		0x300
#define	AR_SREV_VERSION_QCA9550		0x400
#define	AR_SREV_VERSION_AR9485		0x240
#define	AR_SREV_VERSION_QCA9530		0x500

#define	AR_SREV_REVISION_AR9380_10	0	/* AR9380 1.0 */
#define	AR_SREV_REVISION_AR9380_20	2	/* AR9380 2.0/2.1 */
#define	AR_SREV_REVISION_AR9380_22	3	/* AR9380 2.2 */
#define	AR_SREV_REVISION_AR9580_10	4	/* AR9580/Peacock 1.0 */

#define	AR_SREV_REVISION_AR9330_10	0	/* AR9330 1.0 */
#define	AR_SREV_REVISION_AR9330_11	1	/* AR9330 1.1 */
#define	AR_SREV_REVISION_AR9330_12	2	/* AR9330 1.2 */
#define	AR_SREV_REVISION_AR9330_11_MASK	0xf	/* AR9330 1.1 revision mask */

#define	AR_SREV_REVISION_AR9485_10	0	/* AR9485 1.0 */
#define	AR_SREV_REVISION_AR9485_11	1	/* AR9485 1.1 */

#define	AR_SREV_REVISION_AR9340_10	0	/* AR9340 1.0 */
#define	AR_SREV_REVISION_AR9340_11	1	/* AR9340 1.1 */
#define	AR_SREV_REVISION_AR9340_12	2	/* AR9340 1.2 */
#define	AR_SREV_REVISION_AR9340_MASK	0xf	/* AR9340 revision mask */

#define	AR_SREV_REVISION_AR9460_10	0	/* AR946x 1.0 */

#endif	/* __AR9300_DEVID_H__ */
