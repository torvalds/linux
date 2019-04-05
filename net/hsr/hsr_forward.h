/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright 2011-2014 Autronica Fire and Security AS
 *
 * Author(s):
 *	2011-2014 Arvid Brodin, arvid.brodin@alten.se
 */

#ifndef __HSR_FORWARD_H
#define __HSR_FORWARD_H

#include <linux/netdevice.h>
#include "hsr_main.h"

void hsr_forward_skb(struct sk_buff *skb, struct hsr_port *port);

#endif /* __HSR_FORWARD_H */
