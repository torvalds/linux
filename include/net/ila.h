/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * ILA kernel interface
 *
 * Copyright (c) 2015 Tom Herbert <tom@herbertland.com>
 */

#ifndef _NET_ILA_H
#define _NET_ILA_H

struct sk_buff;

int ila_xlat_outgoing(struct sk_buff *skb);
int ila_xlat_incoming(struct sk_buff *skb);

#endif /* _NET_ILA_H */
