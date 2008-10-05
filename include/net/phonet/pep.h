/*
 * File: pep.h
 *
 * Phonet Pipe End Point sockets definitions
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

#ifndef NET_PHONET_PEP_H
#define NET_PHONET_PEP_H

struct pep_sock {
	struct pn_sock		pn_sk;

	/* Listening socket stuff: */
	struct hlist_head	ackq;

	/* Connected socket stuff: */
	u8			tx_credits;
};

static inline struct pep_sock *pep_sk(struct sock *sk)
{
	return (struct pep_sock *)sk;
}

extern const struct proto_ops phonet_stream_ops;

#endif
