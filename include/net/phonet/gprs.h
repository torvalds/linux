/*
 * File: pep_gprs.h
 *
 * GPRS over Phonet pipe end point socket
 *
 * Copyright (C) 2008 Nokia Corporation.
 *
 * Author: Rémi Denis-Courmont <remi.denis-courmont@nokia.com>
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

#ifndef NET_PHONET_GPRS_H
#define NET_PHONET_GPRS_H

struct sock;
struct sk_buff;

int pep_writeable(struct sock *sk);
int pep_write(struct sock *sk, struct sk_buff *skb);
struct sk_buff *pep_read(struct sock *sk);

int gprs_attach(struct sock *sk);
void gprs_detach(struct sock *sk);

#endif
