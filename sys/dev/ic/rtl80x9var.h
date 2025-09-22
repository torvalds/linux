/*	$OpenBSD: rtl80x9var.h,v 1.6 2014/11/24 02:03:37 brad Exp $	*/
/*	$NetBSD: rtl80x9var.h,v 1.1 1998/10/31 00:44:33 thorpej Exp $	*/

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

/*
 * Definitions on Realtek 8019 and 8029 NE2000-compatible network interfaces.
 *
 * Data sheets for these chips can be found at:
 *
 *	http://www.realtek.com.tw
 */

#ifndef _DEV_IC_RTL80x9_VAR_H_
#define	_DEV_IC_RTL80x9_VAR_H_

#ifdef _KERNEL
int	rtl80x9_mediachange(struct dp8390_softc *);
void	rtl80x9_mediastatus(struct dp8390_softc *, struct ifmediareq *);
void	rtl80x9_init_card(struct dp8390_softc *);
void	rtl80x9_media_init(struct dp8390_softc *);
#endif /* _KERNEL */

#endif /* _DEV_IC_RTL80x9_VAR_H_ */
