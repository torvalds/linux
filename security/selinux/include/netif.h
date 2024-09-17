/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Network interface table.
 *
 * Network interfaces (devices) do not have a security field, so we
 * maintain a table associating each interface with a SID.
 *
 * Author: James Morris <jmorris@redhat.com>
 *
 * Copyright (C) 2003 Red Hat, Inc., James Morris <jmorris@redhat.com>
 * Copyright (C) 2007 Hewlett-Packard Development Company, L.P.
 *                    Paul Moore <paul@paul-moore.com>
 */
#ifndef _SELINUX_NETIF_H_
#define _SELINUX_NETIF_H_

#include <net/net_namespace.h>

void sel_netif_flush(void);

int sel_netif_sid(struct net *ns, int ifindex, u32 *sid);

#endif	/* _SELINUX_NETIF_H_ */

