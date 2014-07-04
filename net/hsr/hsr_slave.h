/* Copyright 2011-2014 Autronica Fire and Security AS
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 *
 * Author(s):
 *	2011-2014 Arvid Brodin, arvid.brodin@alten.se
 */

#ifndef __HSR_SLAVE_H
#define __HSR_SLAVE_H

#include <linux/skbuff.h>
#include <linux/netdevice.h>
#include "hsr_main.h"

int hsr_add_slave(struct hsr_priv *hsr, struct net_device *dev, int idx);
void hsr_del_slave(struct hsr_priv *hsr, int idx);
rx_handler_result_t hsr_handle_frame(struct sk_buff **pskb);

#endif /* __HSR_SLAVE_H */
