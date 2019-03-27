/*
 * Copyright (c) 1996-2003
 *	Fraunhofer Institute for Open Communication Systems (FhG Fokus).
 * 	All rights reserved.
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * Author: Hartmut Brandt <harti@freebsd.org>
 *
 * $Begemot: libunimsg/netnatm/saal/sscfudef.h,v 1.4 2004/07/08 08:22:16 brandt Exp $
 *
 * Definitions of UNI SSCF constants.
 */
#ifndef _NETNATM_SAAL_SSCFUDEF_H_
#define _NETNATM_SAAL_SSCFUDEF_H_

/*
 * Signals at the upper boundary of the SSCF.
 */
enum saal_sig {
	SAAL_ESTABLISH_request,		/* U -> SAAL: (UU) */
	SAAL_ESTABLISH_indication,	/* SAAL -> U: (UU) */
	SAAL_ESTABLISH_confirm,		/* SAAL -> U: (UU) */
	SAAL_RELEASE_request,		/* U -> SAAL: (UU) */
	SAAL_RELEASE_confirm,		/* SAAL -> U: */
	SAAL_RELEASE_indication,	/* SAAL -> U: (UU) */
	SAAL_DATA_request,		/* U -> SAAL: (DATA) */
	SAAL_DATA_indication,		/* SAAL -> U: (DATA) */
	SAAL_UDATA_request,		/* U -> SAAL: (UDATA) */
	SAAL_UDATA_indication,		/* SAAL -> U: (UDATA) */
};

/*
 * States of the SSCF
 */
enum sscfu_state {
	SSCFU_RELEASED,			/* 1/1 */
	SSCFU_AWAITING_ESTABLISH,	/* 2/2 */
	SSCFU_AWAITING_RELEASE,		/* 4/10 */
	SSCFU_ESTABLISHED,		/* 3/4 */
	SSCFU_RESYNC,			/* 2/5 */
};

/*
 * Debugging flags
 */
enum {
	SSCFU_DBG_LSIG		= 0x01,
	SSCFU_DBG_ERR		= 0x02,
	SSCFU_DBG_STATE		= 0x04,
	SSCFU_DBG_EXEC		= 0x08,
};

#endif
