/*
 * File: af_phonet.h
 *
 * Phonet sockets kernel definitions
 *
 * Copyright (C) 2008 Nokia Corporation.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA
 * 02110-1301 USA
 */

#ifndef AF_PHONET_H
#define AF_PHONET_H

/*
 * The lower layers may not require more space, ever. Make sure it's
 * enough.
 */
#define MAX_PHONET_HEADER	8

static inline struct phonethdr *pn_hdr(struct sk_buff *skb)
{
	return (struct phonethdr *)skb_network_header(skb);
}

/*
 * Get the other party's sockaddr from received skb. The skb begins
 * with a Phonet header.
 */
static inline
void pn_skb_get_src_sockaddr(struct sk_buff *skb, struct sockaddr_pn *sa)
{
	struct phonethdr *ph = pn_hdr(skb);
	u16 obj = pn_object(ph->pn_sdev, ph->pn_sobj);

	sa->spn_family = AF_PHONET;
	pn_sockaddr_set_object(sa, obj);
	pn_sockaddr_set_resource(sa, ph->pn_res);
	memset(sa->spn_zero, 0, sizeof(sa->spn_zero));
}

static inline
void pn_skb_get_dst_sockaddr(struct sk_buff *skb, struct sockaddr_pn *sa)
{
	struct phonethdr *ph = pn_hdr(skb);
	u16 obj = pn_object(ph->pn_rdev, ph->pn_robj);

	sa->spn_family = AF_PHONET;
	pn_sockaddr_set_object(sa, obj);
	pn_sockaddr_set_resource(sa, ph->pn_res);
	memset(sa->spn_zero, 0, sizeof(sa->spn_zero));
}

/* Protocols in Phonet protocol family. */
struct phonet_protocol {
	struct proto		*prot;
	int			sock_type;
};

int phonet_proto_register(int protocol, struct phonet_protocol *pp);
void phonet_proto_unregister(int protocol, struct phonet_protocol *pp);

void phonet_netlink_register(void);
#endif
