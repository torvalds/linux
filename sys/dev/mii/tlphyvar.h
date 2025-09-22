/*	$OpenBSD: tlphyvar.h,v 1.2 2008/06/26 05:42:16 ray Exp $	*/
/*	$NetBSD: tlphyvar.h,v 1.1 1998/08/10 23:59:58 thorpej Exp $	*/

/*-
 * Copyright (c) 1998 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Jason R. Thorpe of the Numerical Aerospace Simulation Facility,
 * NASA Ames Research Center.
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
 * THIS SOFTWARE IS PROVIDED BY THE NETBSD FOUNDATION, INC. AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE FOUNDATION OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef _DEV_MII_TLPHYVAR_H_
#define	_DEV_MII_TLPHYVAR_H_

/*
 * Constants used by the ThunderLAN driver to notify the PHY
 * layer which media this particular interface supports on the PHY.
 * There can be only one of BNC or AUI (since the same line driver
 * is used).
 *
 * For cards using the ThunderLAN PHY that also support 100baseTX,
 * they often want to bypass the 10baseT support on the ThunderLAN
 * PHY (because the UTP is wired up to another PHY).  That is what
 * the NO_10_T bit is for.
 */
#define	TLPHY_MEDIA_10_2	0x01	/* 10base2 (BNC) */
#define	TLPHY_MEDIA_10_5	0x02	/* 10base5 (AUI) */
#define	TLPHY_MEDIA_NO_10_T	0x04	/* don't use the 10baseT (UTP) */

#endif /* _DEV_MII_TLPHYVAR_H_ */
