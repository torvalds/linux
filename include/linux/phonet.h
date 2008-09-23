/**
 * file phonet.h
 *
 * Phonet sockets kernel interface
 *
 * Copyright (C) 2008 Nokia Corporation. All rights reserved.
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

#ifndef LINUX_PHONET_H
#define LINUX_PHONET_H

/* Automatic protocol selection */
#define PN_PROTO_TRANSPORT	0
/* Phonet datagram socket */
#define PN_PROTO_PHONET		1
#define PHONET_NPROTO		2

#define PNADDR_ANY		0
#define PNPORT_RESOURCE_ROUTING	0

/* Phonet protocol header */
struct phonethdr {
	__u8	pn_rdev;
	__u8	pn_sdev;
	__u8	pn_res;
	__be16	pn_length;
	__u8	pn_robj;
	__u8	pn_sobj;
} __attribute__((packed));

/* Phonet socket address structure */
struct sockaddr_pn {
	sa_family_t spn_family;
	__u8 spn_obj;
	__u8 spn_dev;
	__u8 spn_resource;
	__u8 spn_zero[sizeof(struct sockaddr) - sizeof(sa_family_t) - 3];
} __attribute__ ((packed));

static inline __u16 pn_object(__u8 addr, __u16 port)
{
	return (addr << 8) | (port & 0x3ff);
}

static inline __u8 pn_obj(__u16 handle)
{
	return handle & 0xff;
}

static inline __u8 pn_dev(__u16 handle)
{
	return handle >> 8;
}

static inline __u16 pn_port(__u16 handle)
{
	return handle & 0x3ff;
}

static inline __u8 pn_addr(__u16 handle)
{
	return (handle >> 8) & 0xfc;
}

static inline void pn_sockaddr_set_addr(struct sockaddr_pn *spn, __u8 addr)
{
	spn->spn_dev &= 0x03;
	spn->spn_dev |= addr & 0xfc;
}

static inline void pn_sockaddr_set_port(struct sockaddr_pn *spn, __u16 port)
{
	spn->spn_dev &= 0xfc;
	spn->spn_dev |= (port >> 8) & 0x03;
	spn->spn_obj = port & 0xff;
}

static inline void pn_sockaddr_set_object(struct sockaddr_pn *spn,
						__u16 handle)
{
	spn->spn_dev = pn_dev(handle);
	spn->spn_obj = pn_obj(handle);
}

static inline void pn_sockaddr_set_resource(struct sockaddr_pn *spn,
						__u8 resource)
{
	spn->spn_resource = resource;
}

static inline __u8 pn_sockaddr_get_addr(const struct sockaddr_pn *spn)
{
	return spn->spn_dev & 0xfc;
}

static inline __u16 pn_sockaddr_get_port(const struct sockaddr_pn *spn)
{
	return ((spn->spn_dev & 0x03) << 8) | spn->spn_obj;
}

static inline __u16 pn_sockaddr_get_object(const struct sockaddr_pn *spn)
{
	return pn_object(spn->spn_dev, spn->spn_obj);
}

static inline __u8 pn_sockaddr_get_resource(const struct sockaddr_pn *spn)
{
	return spn->spn_resource;
}

#endif
