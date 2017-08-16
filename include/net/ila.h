/*
 * ILA kernel interface
 *
 * Copyright (c) 2015 Tom Herbert <tom@herbertland.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of
 * the License, or (at your option) any later version.
 */

#ifndef _NET_ILA_H
#define _NET_ILA_H

int ila_xlat_outgoing(struct sk_buff *skb);
int ila_xlat_incoming(struct sk_buff *skb);

#endif /* _NET_ILA_H */
