/* SPDX-License-Identifier: (GPL-2.0-only OR BSD-3-Clause) */
/*
 * Copyright 2021 Marvell. All rights reserved.
 */

#ifndef _QED_IP_SERVICES_IF_H
#define _QED_IP_SERVICES_IF_H

#include <linux/types.h>
#include <net/route.h>
#include <net/ip6_route.h>
#include <linux/inetdevice.h>

int qed_route_ipv4(struct sockaddr_storage *local_addr,
		   struct sockaddr_storage *remote_addr,
		   struct sockaddr *hardware_address,
		   struct net_device **ndev);
int qed_route_ipv6(struct sockaddr_storage *local_addr,
		   struct sockaddr_storage *remote_addr,
		   struct sockaddr *hardware_address,
		   struct net_device **ndev);
void qed_vlan_get_ndev(struct net_device **ndev, u16 *vlan_id);
struct pci_dev *qed_validate_ndev(struct net_device *ndev);
void qed_return_tcp_port(struct socket *sock);
int qed_fetch_tcp_port(struct sockaddr_storage local_ip_addr,
		       struct socket **sock, u16 *port);
__be16 qed_get_in_port(struct sockaddr_storage *sa);

#endif /* _QED_IP_SERVICES_IF_H */
