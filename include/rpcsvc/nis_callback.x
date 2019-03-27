%/*-
% * Copyright (c) 2010, Oracle America, Inc.
% *
% * Redistribution and use in source and binary forms, with or without
% * modification, are permitted provided that the following conditions are
% * met:
% *
% *     * Redistributions of source code must retain the above copyright
% *       notice, this list of conditions and the following disclaimer.
% *     * Redistributions in binary form must reproduce the above
% *       copyright notice, this list of conditions and the following
% *       disclaimer in the documentation and/or other materials
% *       provided with the distribution.
% *     * Neither the name of the "Oracle America, Inc." nor the names of its
% *       contributors may be used to endorse or promote products derived
% *       from this software without specific prior written permission.
% *
% *   THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
% *   "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
% *   LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
% *   FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
% *   COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT,
% *   INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
% *   DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE
% *   GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
% *   INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
% *   WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
% *   NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
% *   OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
% */

/*
 *	nis_callback.x
 *
 *	Copyright (c) 1988-1992 Sun Microsystems Inc
 *	All Rights Reserved.
 */

/* From: %#pragma ident	"@(#)nis_callback.x	1.7	94/05/03 SMI" */

#ifndef RPC_HDR
%#include <sys/cdefs.h>
%__FBSDID("$FreeBSD$");
#endif

/*
 * "@(#)zns_cback.x 1.2 90/09/10 Copyr 1990 Sun Micro" 
 *
 * RPCL description of the Callback Service.
 */

#ifdef RPC_HDR
%#include <rpcsvc/nis.h>
#endif
#ifdef RPC_XDR
#ifdef SOLARIS
%#include "nis_clnt.h"
#else
%#include "nis.h"
#endif
#endif

typedef nis_object	*obj_p;

struct cback_data {
	obj_p		entries<>;	/* List of objects */
};

program CB_PROG {
	version CB_VERS {
		bool	CBPROC_RECEIVE(cback_data) = 1;
		void	CBPROC_FINISH(void) = 2;
		void	CBPROC_ERROR(nis_error) = 3;
	} = 1;
} = 100302;
