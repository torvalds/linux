/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2002 Networks Associates Technology, Inc.
 * All rights reserved.
 *
 * This software was developed for the FreeBSD Project by ThinkSec AS and
 * NAI Labs, the Security Research Division of Network Associates, Inc.
 * under DARPA/SPAWAR contract N66001-01-C-8035 ("CBOSS"), as part of the
 * DARPA CHATS research program.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. The name of the author may not be used to endorse or promote
 *    products derived from this software without specific prior written
 *    permission.
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
 * $FreeBSD$
 */

#include <sys/param.h>

#include <err.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <rpcsvc/ypclnt.h>

#include "ypclnt.h"

int
ypclnt_connect(ypclnt_t *ypclnt)
{
	int r;

	/* get default domain name unless specified */
	if (ypclnt->domain == NULL) {
		if ((ypclnt->domain = malloc(MAXHOSTNAMELEN)) == NULL) {
			ypclnt_error(ypclnt, __func__,
			    "%s", strerror(errno));
			return (-1);
		}
		if (getdomainname(ypclnt->domain, MAXHOSTNAMELEN) != 0) {
			ypclnt_error(ypclnt, __func__,
			    "can't get NIS domain name");
			return (-1);
		}
	}

	/* map must be specified */
	if (ypclnt->map == NULL) {
		ypclnt_error(ypclnt, __func__,
		    "caller must specify map name");
		return (-1);
	}

	/* get master server for requested map unless specified */
	if (ypclnt->server == NULL) {
		r = yp_master(ypclnt->domain, ypclnt->map, &ypclnt->server);
		if (r != 0) {
			ypclnt_error(ypclnt, __func__,
			    "can't get NIS server name: %s", yperr_string(r));
			return (-1);
		}
	}

	ypclnt_error(ypclnt, NULL, NULL);
	return (0);
}
