/* SPDX-License-Identifier: GPL-2.0-only WITH Linux-syscall-note */
/*
 * Copyright (c) 2021 Taehee Yoo <ap420073@gmail.com>
 */
#ifndef _UAPI_AMT_H_
#define _UAPI_AMT_H_

enum ifla_amt_mode {
	/* AMT interface works as Gateway mode.
	 * The Gateway mode encapsulates IGMP/MLD traffic and decapsulates
	 * multicast traffic.
	 */
	AMT_MODE_GATEWAY = 0,
	/* AMT interface works as Relay mode.
	 * The Relay mode encapsulates multicast traffic and decapsulates
	 * IGMP/MLD traffic.
	 */
	AMT_MODE_RELAY,
	__AMT_MODE_MAX,
};

#define AMT_MODE_MAX (__AMT_MODE_MAX - 1)

enum {
	IFLA_AMT_UNSPEC,
	/* This attribute specify mode etier Gateway or Relay. */
	IFLA_AMT_MODE,
	/* This attribute specify Relay port.
	 * AMT interface is created as Gateway mode, this attribute is used
	 * to specify relay(remote) port.
	 * AMT interface is created as Relay mode, this attribute is used
	 * as local port.
	 */
	IFLA_AMT_RELAY_PORT,
	/* This attribute specify Gateway port.
	 * AMT interface is created as Gateway mode, this attribute is used
	 * as local port.
	 * AMT interface is created as Relay mode, this attribute is not used.
	 */
	IFLA_AMT_GATEWAY_PORT,
	/* This attribute specify physical device */
	IFLA_AMT_LINK,
	/* This attribute specify local ip address */
	IFLA_AMT_LOCAL_IP,
	/* This attribute specify Relay ip address.
	 * So, this is not used by Relay.
	 */
	IFLA_AMT_REMOTE_IP,
	/* This attribute specify Discovery ip address.
	 * When Gateway get started, it send discovery message to find the
	 * Relay's ip address.
	 * So, this is not used by Relay.
	 */
	IFLA_AMT_DISCOVERY_IP,
	/* This attribute specify number of maximum tunnel. */
	IFLA_AMT_MAX_TUNNELS,
	__IFLA_AMT_MAX,
};

#define IFLA_AMT_MAX (__IFLA_AMT_MAX - 1)

#endif /* _UAPI_AMT_H_ */
