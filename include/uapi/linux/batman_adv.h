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

#define BATADV_NL_MCAST_GROUP_TPMETER	"tpmeter"

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
 * @BATADV_ATTR_ORIG_ADDRESS: originator mac address
 * @BATADV_ATTR_TPMETER_RESULT: result of run (see batadv_tp_meter_status)
 * @BATADV_ATTR_TPMETER_TEST_TIME: time (msec) the run took
 * @BATADV_ATTR_TPMETER_BYTES: amount of acked bytes during run
 * @BATADV_ATTR_TPMETER_COOKIE: session cookie to match tp_meter session
 * @BATADV_ATTR_PAD: attribute used for padding for 64-bit alignment
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
	BATADV_ATTR_ORIG_ADDRESS,
	BATADV_ATTR_TPMETER_RESULT,
	BATADV_ATTR_TPMETER_TEST_TIME,
	BATADV_ATTR_TPMETER_BYTES,
	BATADV_ATTR_TPMETER_COOKIE,
	BATADV_ATTR_PAD,
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
 * @BATADV_CMD_TP_METER: Start a tp meter session
 * @BATADV_CMD_TP_METER_CANCEL: Cancel a tp meter session
 * @__BATADV_CMD_AFTER_LAST: internal use
 * @BATADV_CMD_MAX: highest used command number
 */
enum batadv_nl_commands {
	BATADV_CMD_UNSPEC,
	BATADV_CMD_GET_MESH_INFO,
	BATADV_CMD_TP_METER,
	BATADV_CMD_TP_METER_CANCEL,
	/* add new commands above here */
	__BATADV_CMD_AFTER_LAST,
	BATADV_CMD_MAX = __BATADV_CMD_AFTER_LAST - 1
};

/**
 * enum batadv_tp_meter_reason - reason of a tp meter test run stop
 * @BATADV_TP_REASON_COMPLETE: sender finished tp run
 * @BATADV_TP_REASON_CANCEL: sender was stopped during run
 * @BATADV_TP_REASON_DST_UNREACHABLE: receiver could not be reached or didn't
 *  answer
 * @BATADV_TP_REASON_RESEND_LIMIT: (unused) sender retry reached limit
 * @BATADV_TP_REASON_ALREADY_ONGOING: test to or from the same node already
 *  ongoing
 * @BATADV_TP_REASON_MEMORY_ERROR: test was stopped due to low memory
 * @BATADV_TP_REASON_CANT_SEND: failed to send via outgoing interface
 * @BATADV_TP_REASON_TOO_MANY: too many ongoing sessions
 */
enum batadv_tp_meter_reason {
	BATADV_TP_REASON_COMPLETE		= 3,
	BATADV_TP_REASON_CANCEL			= 4,
	/* error status >= 128 */
	BATADV_TP_REASON_DST_UNREACHABLE	= 128,
	BATADV_TP_REASON_RESEND_LIMIT		= 129,
	BATADV_TP_REASON_ALREADY_ONGOING	= 130,
	BATADV_TP_REASON_MEMORY_ERROR		= 131,
	BATADV_TP_REASON_CANT_SEND		= 132,
	BATADV_TP_REASON_TOO_MANY		= 133,
};

#endif /* _UAPI_LINUX_BATMAN_ADV_H_ */
