/*	$OpenBSD: if_indextoname.c,v 1.12 2018/05/10 13:44:43 tb Exp $	*/
/*	$KAME: if_indextoname.c,v 1.6 2000/11/07 22:33:25 jinmei Exp $	*/

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
 *	BSDI Id: if_indextoname.c,v 2.3 2000/04/17 22:38:05 dab Exp
 */

#include <sys/types.h>
#include <sys/socket.h>
#include <net/if.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

/*
 * From RFC 2553:
 *
 * The second function maps an interface index into its corresponding
 * name.
 *
 *    #include <net/if.h>
 *
 *    char  *if_indextoname(unsigned int ifindex, char *ifname);
 *
 * The ifname argument must point to a buffer of at least IF_NAMESIZE
 * bytes into which the interface name corresponding to the specified
 * index is returned.  (IF_NAMESIZE is also defined in <net/if.h> and
 * its value includes a terminating null byte at the end of the
 * interface name.) This pointer is also the return value of the
 * function.  If there is no interface corresponding to the specified
 * index, NULL is returned, and errno is set to ENXIO, if there was a
 * system error (such as running out of memory), if_indextoname returns
 * NULL and errno would be set to the proper value (e.g., ENOMEM).
 */

char *
if_indextoname(unsigned int ifindex, char *ifname)
{
	struct if_nameindex *ifni, *ifni2;

	if ((ifni = if_nameindex()) == NULL)
		return NULL;

	for (ifni2 = ifni; ifni2->if_index != 0; ifni2++) {
		if (ifni2->if_index == ifindex) {
			strlcpy(ifname, ifni2->if_name, IFNAMSIZ);
			if_freenameindex(ifni);
			return ifname;
		}
	}

	if_freenameindex(ifni);
	errno = ENXIO;
	return NULL;
}
DEF_WEAK(if_indextoname);
