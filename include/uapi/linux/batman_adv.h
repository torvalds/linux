/* Copyright (C) 2016 B.A.T.M.A.N. contributors:
 *
 * Matthias Schiffer
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#ifndef _UAPI_LINUX_BATMAN_ADV_H_
#define _UAPI_LINUX_BATMAN_ADV_H_

#define BATADV_NL_NAME "batadv"

/**
 * enum batadv_nl_attrs - batman-adv netlink attributes
 *
 * @BATADV_ATTR_UNSPEC: unspecified attribute to catch errors
 * @BATADV_ATTR_VERSION: batman-adv version string
 * @BATADV_ATTR_ALGO_NAME: name of routing algorithm
 * @BATADV_ATTR_MESH_IFINDEX: index of the batman-adv interface
 * @BATADV_ATTR_MESH_IFNAME: name of the batman-adv interface
 * @BATADV_ATTR_MESH_ADDRESS: mac address of the batman-adv interface
 * @BATADV_ATTR_HARD_IFINDEX: index of the non-batman-adv interface
 * @BATADV_ATTR_HARD_IFNAME: name of the non-batman-adv interface
 * @BATADV_ATTR_HARD_ADDRESS: mac address of the non-batman-adv interface
 * @__BATADV_ATTR_AFTER_LAST: internal use
 * @NUM_BATADV_ATTR: total number of batadv_nl_attrs available
 * @BATADV_ATTR_MAX: highest attribute number currently defined
 */
enum batadv_nl_attrs {
	BATADV_ATTR_UNSPEC,
	BATADV_ATTR_VERSION,
	BATADV_ATTR_ALGO_NAME,
	BATADV_ATTR_MESH_IFINDEX,
	BATADV_ATTR_MESH_IFNAME,
	BATADV_ATTR_MESH_ADDRESS,
	BATADV_ATTR_HARD_IFINDEX,
	BATADV_ATTR_HARD_IFNAME,
	BATADV_ATTR_HARD_ADDRESS,
	/* add attributes above here, update the policy in netlink.c */
	__BATADV_ATTR_AFTER_LAST,
	NUM_BATADV_ATTR = __BATADV_ATTR_AFTER_LAST,
	BATADV_ATTR_MAX = __BATADV_ATTR_AFTER_LAST - 1
};

/**
 * enum batadv_nl_commands - supported batman-adv netlink commands
 *
 * @BATADV_CMD_UNSPEC: unspecified command to catch errors
 * @BATADV_CMD_GET_MESH_INFO: Query basic information about batman-adv device
 * @__BATADV_CMD_AFTER_LAST: internal use
 * @BATADV_CMD_MAX: highest used command number
 */
enum batadv_nl_commands {
	BATADV_CMD_UNSPEC,
	BATADV_CMD_GET_MESH_INFO,
	/* add new commands above here */
	__BATADV_CMD_AFTER_LAST,
	BATADV_CMD_MAX = __BATADV_CMD_AFTER_LAST - 1
};

#endif /* _UAPI_LINUX_BATMAN_ADV_H_ */
