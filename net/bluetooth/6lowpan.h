/*
   Copyright (c) 2013 Intel Corp.

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License version 2 and
   only version 2 as published by the Free Software Foundation.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.
*/

#ifndef __6LOWPAN_H
#define __6LOWPAN_H

#include <linux/skbuff.h>
#include <net/bluetooth/l2cap.h>

int bt_6lowpan_recv(struct l2cap_conn *conn, struct sk_buff *skb);
int bt_6lowpan_add_conn(struct l2cap_conn *conn);
int bt_6lowpan_del_conn(struct l2cap_conn *conn);
int bt_6lowpan_init(void);
void bt_6lowpan_cleanup(void);

#endif /* __6LOWPAN_H */
