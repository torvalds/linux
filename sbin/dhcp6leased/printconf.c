/*	$OpenBSD: printconf.c,v 1.3 2024/06/03 11:08:31 florian Exp $	*/

/*
 * Copyright (c) 2024 Florian Obser <florian@openbsd.org>
 * Copyright (c) 2004, 2005 Esben Norby <norby@openbsd.org>
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
#include <sys/queue.h>
#include <sys/socket.h>
#include <sys/uio.h>

#include <net/if.h>

#include <netinet/in.h>

#include <arpa/inet.h>

#include <event.h>
#include <imsg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "dhcp6leased.h"
#include "log.h"

void	print_iface_conf(struct iface_conf *, int);
void	print_iface_ia_conf(struct iface_ia_conf *, int);
void	print_iface_pd_conf(char *, struct iface_pd_conf *, int);

void
print_iface_pd_conf(char *indent, struct iface_pd_conf *pd_conf, int verbose)
{
	if (verbose > 1) {
		struct in6_addr	 ia6;
		int		 i;
		char		 ntopbuf[INET6_ADDRSTRLEN];

		memset(&ia6, 0, sizeof(ia6));
		inet_pton(AF_INET6, "2001:db8::", &ia6);

		for (i = 0; i < 16; i++)
			ia6.s6_addr[i] |= pd_conf->prefix_mask.s6_addr[i];

		inet_ntop(AF_INET6, &ia6, ntopbuf, INET6_ADDRSTRLEN);
		printf("%s%s/%d\t# %s/%d\n", indent, pd_conf->name,
		    pd_conf->prefix_len, ntopbuf, pd_conf->prefix_len);
	} else
		printf("%s%s/%d\n", indent, pd_conf->name, pd_conf->prefix_len);
}

void
print_iface_ia_conf(struct iface_ia_conf *ia_conf, int verbose)
{
	struct iface_pd_conf	*pd_conf;

	SIMPLEQ_FOREACH(pd_conf, &ia_conf->iface_pd_list, entry)
	    print_iface_pd_conf("\t", pd_conf,
		ia_conf->prefix_len >= 32 ? verbose : 1);
}

void
print_iface_conf(struct iface_conf *iface, int verbose)
{
	struct iface_ia_conf	*ia_conf;
	int			 first = 1;

	SIMPLEQ_FOREACH(ia_conf, &iface->iface_ia_list, entry) {
		if (!first)
			printf("\n");
		first = 0;

		if (verbose > 1) {
			printf("request prefix delegation on %s for {"
			    "\t# prefix length = %d\n", iface->name,
			    ia_conf->prefix_len);
		} else {
			printf("request prefix delegation on %s for {\n",
			    iface->name);
		}
		print_iface_ia_conf(ia_conf, verbose);
		printf("}\n");
	}
}
void
print_config(struct dhcp6leased_conf *conf, int verbose)
{
	struct iface_conf	*iface;

	if (conf->rapid_commit)
		printf("request rapid commit\n\n");

	SIMPLEQ_FOREACH(iface, &conf->iface_list, entry)
		print_iface_conf(iface, verbose);
}
