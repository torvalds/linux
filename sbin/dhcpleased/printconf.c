/*	$OpenBSD: printconf.c,v 1.6 2023/11/25 13:00:05 florian Exp $	*/

/*
 * Copyright (c) 2018 Florian Obser <florian@openbsd.org>
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
#include <netinet/if_ether.h>

#include <arpa/inet.h>

#include <event.h>
#include <imsg.h>
#include <stdio.h>
#include <stdlib.h>
#include <vis.h>

#include "dhcpleased.h"
#include "log.h"

void	print_dhcp_options(char *, uint8_t *, int);

void
print_dhcp_options(char *indent, uint8_t *p, int len)
{
	static char	 buf[4 * 1500 + 1];
	int		 rem, i;
	uint8_t		 dho, dho_len;

	rem = len;

	while (rem > 0) {
		dho = *p;
		p += 1;
		rem -= 1;

		if (rem == 0)
			fatal("dhcp option too short");
		dho_len = *p;
		p += 1;
		rem -= 1;
		if (rem < dho_len)
			fatal("dhcp option too short: %d %d", rem, dho_len);

		switch (dho) {
		case DHO_DHCP_CLASS_IDENTIFIER:
			strvisx(buf, p, dho_len, VIS_DQ | VIS_CSTYLE);
			p += dho_len;
			rem -= dho_len;
			printf("%ssend vendor class id \"%s\"\n", indent, buf);
			break;
		case DHO_DHCP_CLIENT_IDENTIFIER:
			if (dho_len < 1)
				fatal("dhcp option too short");
			switch (*p) {
			case HTYPE_ETHER:
				printf("%ssend client id \"", indent);
				for (i = 0; i < dho_len; i++)
					printf("%s%02x", i != 0? ":" : "",
					    *(p + i));
				printf("\"\n");
				break;
			default:
				strvisx(buf, p, dho_len, VIS_DQ | VIS_CSTYLE);
				printf("%ssend client id \"%s\"\n",
				    indent, buf);
				break;
			}
			p += dho_len;
			rem -= dho_len;
			break;
		default:
			fatal("unknown dhcp option: %d [%d]", *p, rem);
			break;
		}
	}
}

void
print_config(struct dhcpleased_conf *conf)
{
	struct iface_conf	*iface;
	int			 i;
	char			 hbuf[INET_ADDRSTRLEN];

	SIMPLEQ_FOREACH(iface, &conf->iface_list, entry) {
		printf("interface %s {\n", iface->name);
		print_dhcp_options("\t", iface->c_id, iface->c_id_len);
		if (iface->h_name != NULL) {
			if (iface->h_name[0] == '\0')
				printf("\tsend no host name\n");
			else {
				printf("\tsend host name \"%s\"\n",
				    iface->h_name);
			}
		}
		print_dhcp_options("\t", iface->vc_id, iface->vc_id_len);
		if (iface->ignore & IGN_DNS)
			printf("\tignore dns\n");
		if (iface->ignore & IGN_ROUTES)
			printf("\tignore routes\n");
		for (i = 0; i < iface->ignore_servers_len; i++) {
			if (inet_ntop(AF_INET, &iface->ignore_servers[i],
			    hbuf, sizeof(hbuf)) == NULL)
				continue;
			printf("\tignore %s\n", hbuf);

		}
		if (iface->prefer_ipv6)
			printf("\tprefer ipv6\n");
		printf("}\n");
	}
}
