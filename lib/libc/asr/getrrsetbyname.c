/*	$OpenBSD: getrrsetbyname.c,v 1.6 2015/09/14 07:38:37 guenther Exp $	*/
/*
 * Copyright (c) 2012 Eric Faurot <eric@openbsd.org>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>

#include <asr.h>
#include <errno.h>
#include <resolv.h>
#include <stdlib.h>

int
getrrsetbyname(const char *name, unsigned int class, unsigned int type,
    unsigned int flags, struct rrsetinfo **res)
{
	struct asr_query *as;
	struct asr_result ar;
	int r, saved_errno = errno;

	res_init();

	as = getrrsetbyname_async(name, class, type, flags, NULL);
	if (as == NULL) {
		r = (errno == ENOMEM) ? ERRSET_NOMEMORY : ERRSET_FAIL;
		errno = saved_errno;
		return (r);
	}

	asr_run_sync(as, &ar);

	*res = ar.ar_rrsetinfo;

	return (ar.ar_rrset_errno);
}

/* from net/getrrsetbyname.c */
void
freerrset(struct rrsetinfo *rrset)
{
	u_int16_t i;

	if (rrset == NULL)
		return;

	if (rrset->rri_rdatas) {
		for (i = 0; i < rrset->rri_nrdatas; i++) {
			if (rrset->rri_rdatas[i].rdi_data == NULL)
				break;
			free(rrset->rri_rdatas[i].rdi_data);
		}
		free(rrset->rri_rdatas);
	}

	if (rrset->rri_sigs) {
		for (i = 0; i < rrset->rri_nsigs; i++) {
			if (rrset->rri_sigs[i].rdi_data == NULL)
				break;
			free(rrset->rri_sigs[i].rdi_data);
		}
		free(rrset->rri_sigs);
	}

	if (rrset->rri_name)
		free(rrset->rri_name);
	free(rrset);
}
DEF_WEAK(freerrset);
