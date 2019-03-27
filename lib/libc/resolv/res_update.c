#if !defined(lint) && !defined(SABER)
static const char rcsid[] = "$Id: res_update.c,v 1.13 2005/04/27 04:56:43 sra Exp $";
#endif /* not lint */

/*-
 * SPDX-License-Identifier: ISC
 *
 * Copyright (c) 2004 by Internet Systems Consortium, Inc. ("ISC")
 * Copyright (c) 1996-1999 by Internet Software Consortium.
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND ISC DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS.  IN NO EVENT SHALL ISC BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT
 * OF OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

/*! \file
 * \brief
 * Based on the Dynamic DNS reference implementation by Viraj Bais
 * &lt;viraj_bais@ccm.fm.intel.com>
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include "port_before.h"

#include <sys/param.h>
#include <sys/socket.h>
#include <sys/time.h>

#include <netinet/in.h>
#include <arpa/inet.h>
#include <arpa/nameser.h>

#include <errno.h>
#include <limits.h>
#include <netdb.h>
#include <res_update.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <isc/list.h>
#include <resolv.h>

#include "port_after.h"
#include "res_private.h"

/*%
 * Separate a linked list of records into groups so that all records
 * in a group will belong to a single zone on the nameserver.
 * Create a dynamic update packet for each zone and send it to the
 * nameservers for that zone, and await answer.
 * Abort if error occurs in updating any zone.
 * Return the number of zones updated on success, < 0 on error.
 *
 * On error, caller must deal with the unsynchronized zones
 * eg. an A record might have been successfully added to the forward
 * zone but the corresponding PTR record would be missing if error
 * was encountered while updating the reverse zone.
 */

struct zonegrp {
	char			z_origin[MAXDNAME];
	ns_class		z_class;
	union res_sockaddr_union z_nsaddrs[MAXNS];
	int			z_nscount;
	int			z_flags;
	LIST(ns_updrec)		z_rrlist;
	LINK(struct zonegrp)	z_link;
};

#define ZG_F_ZONESECTADDED	0x0001

/* Forward. */

static void	res_dprintf(const char *, ...) ISC_FORMAT_PRINTF(1, 2);

/* Macros. */

#define DPRINTF(x) do {\
		int save_errno = errno; \
		if ((statp->options & RES_DEBUG) != 0U) res_dprintf x; \
		errno = save_errno; \
	} while (0)

/* Public. */

int
res_nupdate(res_state statp, ns_updrec *rrecp_in, ns_tsig_key *key) {
	ns_updrec *rrecp;
	u_char answer[PACKETSZ];
	u_char *packet;
	struct zonegrp *zptr, tgrp;
	LIST(struct zonegrp) zgrps;
	int nzones = 0, nscount = 0, n;
	union res_sockaddr_union nsaddrs[MAXNS];

	packet = malloc(NS_MAXMSG);
	if (packet == NULL) {
		DPRINTF(("malloc failed"));
		return (0);
	}
	/* Thread all of the updates onto a list of groups. */
	INIT_LIST(zgrps);
	memset(&tgrp, 0, sizeof (tgrp));
	for (rrecp = rrecp_in; rrecp;
	     rrecp = LINKED(rrecp, r_link) ? NEXT(rrecp, r_link) : NULL) {
		int nscnt;
		/* Find the origin for it if there is one. */
		tgrp.z_class = rrecp->r_class;
		nscnt = res_findzonecut2(statp, rrecp->r_dname, tgrp.z_class,
					 RES_EXHAUSTIVE, tgrp.z_origin,
					 sizeof tgrp.z_origin, 
					 tgrp.z_nsaddrs, MAXNS);
		if (nscnt <= 0) {
			DPRINTF(("res_findzonecut failed (%d)", nscnt));
			goto done;
		}
		tgrp.z_nscount = nscnt;
		/* Find the group for it if there is one. */
		for (zptr = HEAD(zgrps); zptr != NULL; zptr = NEXT(zptr, z_link))
			if (ns_samename(tgrp.z_origin, zptr->z_origin) == 1 &&
			    tgrp.z_class == zptr->z_class)
				break;
		/* Make a group for it if there isn't one. */
		if (zptr == NULL) {
			zptr = malloc(sizeof *zptr);
			if (zptr == NULL) {
				DPRINTF(("malloc failed"));
				goto done;
			}
			*zptr = tgrp;
			zptr->z_flags = 0;
			INIT_LINK(zptr, z_link);
			INIT_LIST(zptr->z_rrlist);
			APPEND(zgrps, zptr, z_link);
		}
		/* Thread this rrecp onto the right group. */
		APPEND(zptr->z_rrlist, rrecp, r_glink);
	}

	for (zptr = HEAD(zgrps); zptr != NULL; zptr = NEXT(zptr, z_link)) {
		/* Construct zone section and prepend it. */
		rrecp = res_mkupdrec(ns_s_zn, zptr->z_origin,
				     zptr->z_class, ns_t_soa, 0);
		if (rrecp == NULL) {
			DPRINTF(("res_mkupdrec failed"));
			goto done;
		}
		PREPEND(zptr->z_rrlist, rrecp, r_glink);
		zptr->z_flags |= ZG_F_ZONESECTADDED;

		/* Marshall the update message. */
		n = res_nmkupdate(statp, HEAD(zptr->z_rrlist),
				  packet, NS_MAXMSG);
		DPRINTF(("res_mkupdate -> %d", n));
		if (n < 0)
			goto done;

		/* Temporarily replace the resolver's nameserver set. */
		nscount = res_getservers(statp, nsaddrs, MAXNS);
		res_setservers(statp, zptr->z_nsaddrs, zptr->z_nscount);

		/* Send the update and remember the result. */
		if (key != NULL) {
#ifdef _LIBC
			DPRINTF(("TSIG is not supported\n"));
			RES_SET_H_ERRNO(statp, NO_RECOVERY);
			goto done;
#else
			n = res_nsendsigned(statp, packet, n, key,
					    answer, sizeof answer);
#endif
		} else
			n = res_nsend(statp, packet, n, answer, sizeof answer);
		if (n < 0) {
			DPRINTF(("res_nsend: send error, n=%d (%s)\n",
				 n, strerror(errno)));
			goto done;
		}
		if (((HEADER *)answer)->rcode == NOERROR)
			nzones++;

		/* Restore resolver's nameserver set. */
		res_setservers(statp, nsaddrs, nscount);
		nscount = 0;
	}
 done:
	while (!EMPTY(zgrps)) {
		zptr = HEAD(zgrps);
		if ((zptr->z_flags & ZG_F_ZONESECTADDED) != 0)
			res_freeupdrec(HEAD(zptr->z_rrlist));
		UNLINK(zgrps, zptr, z_link);
		free(zptr);
	}
	if (nscount != 0)
		res_setservers(statp, nsaddrs, nscount);

	free(packet);
	return (nzones);
}

/* Private. */

static void
res_dprintf(const char *fmt, ...) {
	va_list ap;

	va_start(ap, fmt);
	fputs(";; res_nupdate: ", stderr);
	vfprintf(stderr, fmt, ap);
	fputc('\n', stderr);
	va_end(ap);
}
