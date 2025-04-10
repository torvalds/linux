/* SPDX-License-Identifier: GPL-2.0 */

#ifndef XSK_XDP_COMMON_H_
#define XSK_XDP_COMMON_H_

#define MAX_SOCKETS 2
#define PKT_HDR_ALIGN (sizeof(struct ethhdr) + 2) /* Just to align the data in the packet */

struct xdp_info {
	__u64 count;
} __attribute__((aligned(32)));

#endif /* XSK_XDP_COMMON_H_ */
