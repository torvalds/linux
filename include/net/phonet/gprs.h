/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * File: pep_gprs.h
 *
 * GPRS over Phonet pipe end point socket
 *
 * Copyright (C) 2008 Nokia Corporation.
 *
 * Author: Rémi Denis-Courmont
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
