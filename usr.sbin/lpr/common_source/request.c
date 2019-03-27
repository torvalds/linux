/*
 * Copyright 1997 Massachusetts Institute of Technology
 *
 * Permission to use, copy, modify, and distribute this software and
 * its documentation for any purpose and without fee is hereby
 * granted, provided that both the above copyright notice and this
 * permission notice appear in all copies, that both the above
 * copyright notice and this permission notice appear in all
 * supporting documentation, and that the name of M.I.T. not be used
 * in advertising or publicity pertaining to distribution of the
 * software without specific, written prior permission.  M.I.T. makes
 * no representations about the suitability of this software for any
 * purpose.  It is provided "as is" without express or implied
 * warranty.
 * 
 * THIS SOFTWARE IS PROVIDED BY M.I.T. ``AS IS''.  M.I.T. DISCLAIMS
 * ALL EXPRESS OR IMPLIED WARRANTIES WITH REGARD TO THIS SOFTWARE,
 * INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE. IN NO EVENT
 * SHALL M.I.T. BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF
 * USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT
 * OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

static const char copyright[] =
	"Copyright (C) 1997, Massachusetts Institute of Technology\r\n";

#include "lp.cdefs.h"		/* A cross-platform version of <sys/cdefs.h> */
__FBSDID("$FreeBSD$");

#include <sys/types.h>
#include <sys/stat.h>

#include <stdlib.h>
#include <unistd.h>

#include <sys/param.h>		/* needed for lp.h but not used here */
#include <dirent.h>		/* ditto */
#include <stdio.h>		/* ditto */
#include "lp.h"
#include "lp.local.h"

void
init_request(struct request *rp)
{
	static struct request zero;

	*rp = zero;
	TAILQ_INIT(&rp->users);
	TAILQ_INIT(&rp->jobids);
}

void
free_request(struct request *rp)
{
	struct req_user *ru;
	struct req_jobid *rj;

	if (rp->logname)
		free(rp->logname);
	if (rp->authname)
		free(rp->authname);
	if (rp->prettyname)
		free(rp->prettyname);
	if (rp->authinfo)
		free(rp->authinfo);
	while ((ru = TAILQ_FIRST(&rp->users)) != NULL) {
		TAILQ_REMOVE(&rp->users, ru, ru_link);
		free(ru);
	}
	while ((rj = TAILQ_FIRST(&rp->jobids)) != NULL) {
		TAILQ_REMOVE(&rp->jobids, rj, rj_link);
		free(rj);
	}
	init_request(rp);
}
