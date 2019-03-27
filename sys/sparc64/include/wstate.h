/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 1997 Berkeley Software Design, Inc. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Berkeley Software Design Inc's name may not be used to endorse or
 *    promote products derived from this software without specific prior
 *    written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY BERKELEY SOFTWARE DESIGN INC ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL BERKELEY SOFTWARE DESIGN INC BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 *	from: BSDI: wstate.h,v 1.4 1997/09/18 13:05:51 torek Exp
 * $FreeBSD$
 */

#ifndef	_MACHINE_WSTATE_H_
#define	_MACHINE_WSTATE_H_

/*
 * Window state register bits
 *
 * There really are no bits per se, just the two fields WSTATE.NORMAL
 * and WSTATE.OTHER.  The rest is up to software.
 *
 * We use WSTATE_NORMAL to represent user mode or kernel mode saves
 * (whichever is currently in effect) and WSTATE_OTHER to represent
 * user mode saves (only).
 *
 * Note that locore.s assumes this same bit layout (since the translation
 * from "bits" to "{spill,fill}_N_{normal,other}" is done in hardware).
 */

#define	WSTATE_NORMAL_MASK	1	/* wstate normal minus transition */
#define	WSTATE_OTHER_SHIFT	3	/* for wstate other / user */
#define	WSTATE_OTHER_MASK		/* wstate other minus nested */ \
	(WSTATE_NORMAL_MASK << WSTATE_OTHER_SHIFT)

#define	WSTATE_KERNEL		0	/* normal kernel wstate */
#define	WSTATE_USER_64		0	/* normal 64bit user wstate */
#define	WSTATE_USER_32		1	/* normal 32bit user wstate */

#define	WSTATE_TRANSITION	2	/* if set, force user window */
#define	WSTATE_NESTED			/* if set, spill must not fault */ \
	(WSTATE_TRANSITION << WSTATE_OTHER_SHIFT)

/* Values used by the PROM and (Open)Solaris */
#define	WSTATE_PROM_KMIX	7
#define	WSTATE_PROM_MASK	7

#endif /* !_MACHINE_WSTATE_H_ */
