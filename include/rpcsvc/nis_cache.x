%/*
% * Sun RPC is a product of Sun Microsystems, Inc. and is provided for
% * unrestricted use provided that this legend is included on all tape
% * media and as a part of the software program in whole or part.  Users
% * may copy or modify Sun RPC without charge, but are not authorized
% * to license or distribute it to anyone else except as part of a product or
% * program developed by the user or with the express written consent of
% * Sun Microsystems, Inc.
% *
% * SUN RPC IS PROVIDED AS IS WITH NO WARRANTIES OF ANY KIND INCLUDING THE
% * WARRANTIES OF DESIGN, MERCHANTIBILITY AND FITNESS FOR A PARTICULAR
% * PURPOSE, OR ARISING FROM A COURSE OF DEALING, USAGE OR TRADE PRACTICE.
% *
% * Sun RPC is provided with no support and without any obligation on the
% * part of Sun Microsystems, Inc. to assist in its use, correction,
% * modification or enhancement.
% *
% * SUN MICROSYSTEMS, INC. SHALL HAVE NO LIABILITY WITH RESPECT TO THE
% * INFRINGEMENT OF COPYRIGHTS, TRADE SECRETS OR ANY PATENTS BY SUN RPC
% * OR ANY PART THEREOF.
% *
% * In no event will Sun Microsystems, Inc. be liable for any lost revenue
% * or profits or other special, indirect and consequential damages, even if
% * Sun has been advised of the possibility of such damages.
% *
% * Sun Microsystems, Inc.
% * 2550 Garcia Avenue
% * Mountain View, California  94043
% */

/*
 *	nis_cache.x
 *
 *	Copyright (c) 1988-1992 Sun Microsystems Inc
 *	All Rights Reserved.
 */

/* From: %#pragma ident	"@(#)nis_cache.x	1.11	94/05/03 SMI" */

#ifndef RPC_HDR
%#include <sys/cdefs.h>
%__FBSDID("$FreeBSD$");
#endif

#ifdef RPC_HDR
%#include <rpc/types.h>
%#include <rpcsvc/nis.h>
%
%/* default cache file */
%#define CACHEFILE "/var/nis/NIS_SHARED_DIRCACHE" 
%
%/* clients have to read-lock the cache file, and SVR4 locking requires that */
%/*   the file be writable, but we don't want a world-writable cache file.   */
%/*   So... everyone agrees to use a different, world-writable file for the  */
%/*   locking operations, but the data is in CACHEFILE.                      */
%#define CACHELOCK "/usr/tmp/.NIS_DIR_CACHELOCK"
%
%/* the file containing one trusted XDR'ed directory object.
% * This has to be present for the system to work.
% */
%#define COLD_START_FILE "/var/nis/NIS_COLD_START"
%
%enum pc_status {HIT, MISS, NEAR_MISS};
%
%extern int __nis_debuglevel;
%
%
#endif

#ifdef RPC_CLNT
#ifdef SOLARIS
%#include "../gen/nis_clnt.h"
#else
%#include "nis.h"
#endif
#endif

program CACHEPROG {
	version CACHE_VER_1 {
		void NIS_CACHE_ADD_ENTRY(fd_result) = 1;
		void NIS_CACHE_REMOVE_ENTRY(directory_obj) = 2;
		void NIS_CACHE_READ_COLDSTART(void) = 3;
		void NIS_CACHE_REFRESH_ENTRY(string<>) = 4;
	} = 1;
} = 100301;
