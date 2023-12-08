/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _NET_BPFILTER_MSGFMT_H
#define _NET_BPFILTER_MSGFMT_H

struct mbox_request {
	__u64 addr;
	__u32 len;
	__u32 is_set;
	__u32 cmd;
	__u32 pid;
};

struct mbox_reply {
	__u32 status;
};

#endif
