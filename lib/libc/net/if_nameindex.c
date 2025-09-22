/*	$OpenBSD: if_nameindex.c,v 1.13 2016/12/16 17:44:59 krw Exp $	*/
/*	$KAME: if_nameindex.c,v 1.7 2000/11/24 08:17:20 itojun Exp $	*/

/*-
 * Copyright (c) 2015 Claudio Jeker <claudio@openbsd.org>
 * Copyright (c) 1997, 2000
 *	Berkeley Software Design, Inc.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *
 * THIS SOFTWARE IS PROVIDED BY Berkeley Software Design, Inc. ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL Berkeley Software Design, Inc. BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 *	BSDI Id: if_nameindex.c,v 2.3 2000/04/17 22:38:05 dab Exp
 */

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/sysctl.h>
#include <net/if.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

/*
 * From RFC 2553:
 *
 * 4.3 Return All Interface Names and Indexes
 *
 *    The if_nameindex structure holds the information about a single
 *    interface and is defined as a result of including the <net/if.h>
 *    header.
 *
 *       struct if_nameindex {
 *         unsigned int   if_index;
 *         char          *if_name;
 *       };
 *
 *    The final function returns an array of if_nameindex structures, one
 *    structure per interface.
 *
 *       struct if_nameindex  *if_nameindex(void);
 *
 *    The end of the array of structures is indicated by a structure with
 *    an if_index of 0 and an if_name of NULL.  The function returns a NULL
 *    pointer upon an error, and would set errno to the appropriate value.
 *
 *    The memory used for this array of structures along with the interface
 *    names pointed to by the if_name members is obtained dynamically.
 *    This memory is freed by the next function.
 *
 * 4.4.  Free Memory
 *
 *    The following function frees the dynamic memory that was allocated by
 *    if_nameindex().
 *
 *        #include <net/if.h>
 *
 *        void  if_freenameindex(struct if_nameindex *ptr);
 *
 *    The argument to this function must be a pointer that was returned by
 *    if_nameindex().
 */

struct if_nameindex *
if_nameindex(void)
{
	struct if_nameindex_msg *ifnm = NULL;
	struct if_nameindex *ifni = NULL, *ifni2;
	char *cp;
	size_t needed;
	unsigned int ni, i;
	int mib[6];

	mib[0] = CTL_NET;
	mib[1] = PF_ROUTE;
	mib[2] = 0;		/* protocol */
	mib[3] = 0;		/* not used */
	mib[4] = NET_RT_IFNAMES;
	mib[5] = 0;		/* no flags */
	while (1) {
		struct if_nameindex_msg *buf = NULL;

		if (sysctl(mib, 6, NULL, &needed, NULL, 0) == -1)
			goto out;
		if (needed == 0)
			break;
		if ((buf = realloc(ifnm, needed)) == NULL)
			goto out;
		ifnm = buf;
		if (sysctl(mib, 6, ifnm, &needed, NULL, 0) == -1) {
			if (errno == ENOMEM)
				continue;
			goto out;
		}
		break;
	}

	/*
	 * Allocate a chunk of memory, use the first part for the array of
	 * structures, and the last part for the strings.
	 */
	ni = needed / sizeof(*ifnm);
	ifni = calloc(ni + 1, sizeof(struct if_nameindex) + IF_NAMESIZE);
	if (ifni == NULL)
		goto out;
	cp = (char *)(ifni + (ni + 1));

	ifni2 = ifni;
	for (i = 0; i < ni; i++) {
		ifni2->if_index = ifnm[i].if_index;
		/* don't care about truncation */
		strlcpy(cp, ifnm[i].if_name, IF_NAMESIZE);
		ifni2->if_name = cp;
		ifni2++;
		cp += IF_NAMESIZE;
	}
	/* Finally, terminate the array. */
	ifni2->if_index = 0;
	ifni2->if_name = NULL;
out:
	free(ifnm);
	return ifni;
}

void
if_freenameindex(struct if_nameindex *ptr)
{
	free(ptr);
}
DEF_WEAK(if_nameindex);
DEF_WEAK(if_freenameindex);
