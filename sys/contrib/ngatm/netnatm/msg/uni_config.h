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
 * $Begemot: libunimsg/netnatm/msg/uni_config.h,v 1.3 2003/09/19 11:58:15 hbb Exp $
 */
#ifndef _uni_uni_config_h_
#define _uni_uni_config_h_

enum {
	/* maximum number of reported error IEs */
	UNI_MAX_ERRIE	= 50,

	/* maximum number of Generic Identifier Transport IE's per message */
	UNI_NUM_IE_GIT	= 3,

	/* maximum number of BLLI's in SETUP */
	UNI_NUM_IE_BLLI	= 3,

	/* maximum number of CALLEDSUB's */
	UNI_NUM_IE_CALLEDSUB = 2,

	/* maximum number of CALLINGSUB's */
	UNI_NUM_IE_CALLINGSUB = 2,

	/* maximum number of TNS's */
	UNI_NUM_IE_TNS = 4,

	/* maximum length of TNS name */
	UNI_TNS_MAXLEN = 4,

	/* maximum info size in user-to-user signalling IE */
	UNI_UU_MAXLEN	= 128,

	/* maximum length of address */
	UNI_ADDR_MAXLEN	= 20,

	/* maximum length of subaddress */
	UNI_SUBADDR_MAXLEN = 20,

	/* maximum number of DTLs */
	UNI_NUM_IE_DTL = 10,
	/* maximum number of identifiers in DTL */
	UNI_DTL_MAXNUM = 20,
};
#endif
