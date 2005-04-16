/*
 * Network interface table.
 *
 * Network interfaces (devices) do not have a security field, so we
 * maintain a table associating each interface with a SID.
 *
 * Author: James Morris <jmorris@redhat.com>
 *
 * Copyright (C) 2003 Red Hat, Inc., James Morris <jmorris@redhat.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2,
 * as published by the Free Software Foundation.
 */
#ifndef _SELINUX_NETIF_H_
#define _SELINUX_NETIF_H_

int sel_netif_sids(struct net_device *dev, u32 *if_sid, u32 *msg_sid);

#endif	/* _SELINUX_NETIF_H_ */

