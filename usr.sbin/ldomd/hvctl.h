/*	$OpenBSD: hvctl.h,v 1.2 2012/12/10 06:42:12 kettenis Exp $	*/

/*
 * Copyright (c) 2012 Mark Kettenis
 *
 * Permission to use, copy, modify, and distribute this software for any
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

struct hv_io {
	uint64_t	hi_cookie;
	void		*hi_addr;
	size_t		hi_len;
};

#define HVIOCREAD	_IOW('h', 0, struct hv_io)
#define HVIOCWRITE	_IOW('h', 1, struct hv_io)

#define SIS_NORMAL		0x1
#define SIS_TRANSITION		0x2
#define SOFT_STATE_SIZE		32

#define GUEST_STATE_STOPPED		0x0
#define GUEST_STATE_RESETTING		0x1
#define GUEST_STATE_NORMAL		0x2
#define GUEST_STATE_SUSPENDED		0x3
#define GUEST_STATE_EXITING		0x4
#define GUEST_STATE_UNCONFIGURED	0xff

#define HVCTL_RES_STATUS_DATA_SIZE	40

struct hvctl_header {
	uint16_t	op;
	uint16_t	seq;
	uint16_t	chksum;
	uint16_t	status;
};

struct hvctl_hello {
	uint16_t	major;
	uint16_t	minor;
};

struct hvctl_challenge {
	uint64_t	code;
};

struct hvctl_hvconfig {
	uint64_t	hv_membase;
	uint64_t	hv_memsize;
	uint64_t	hvmdp;
	uint64_t	del_reconf_hvmdp;
	uint32_t	del_reconf_gid;
};

struct hvctl_reconfig {
	uint64_t	hvmdp;
	uint32_t	guestid;
};

struct hvctl_guest_op {
	uint32_t	guestid;
	uint32_t	code;
};

struct hvctl_res_status {
	uint32_t	res;
	uint32_t	resid;
	uint32_t	infoid;
	uint32_t	code;
	uint8_t         data[HVCTL_RES_STATUS_DATA_SIZE];
};

struct hvctl_rs_guest_state {
	uint64_t	state;
};

struct hvctl_rs_guest_softstate {
	uint8_t		soft_state;
	char		soft_state_str[SOFT_STATE_SIZE];
};

struct hvctl_rs_guest_util {
	uint64_t	lifespan;
	uint64_t	wallclock_delta;
	uint64_t	active_delta;
	uint64_t	stopped_cycles;
	uint64_t	yielded_cycles;
};

struct hvctl_msg {
	struct hvctl_header	hdr;
	union {
		struct hvctl_hello	hello;
		struct hvctl_challenge	clnge;
		struct hvctl_hvconfig	hvcnf;
		struct hvctl_reconfig	reconfig;
		struct hvctl_guest_op	guestop;
		struct hvctl_res_status	resstat;
	} msg;
};

#define HVCTL_OP_HELLO		0
#define HVCTL_OP_CHALLENGE	1
#define HVCTL_OP_RESPONSE	2
#define HVCTL_OP_GET_HVCONFIG	3
#define HVCTL_OP_RECONFIGURE	4
#define HVCTL_OP_GUEST_START	5
#define HVCTL_OP_GUEST_STOP	6
#define HVCTL_OP_GUEST_PANIC	10
#define HVCTL_OP_GET_RES_STAT	11

#define HVCTL_ST_OK		0

#define HVCTL_RES_GUEST		0

#define HVCTL_INFO_GUEST_STATE		0
#define HVCTL_INFO_GUEST_SOFT_STATE	1
#define HVCTL_INFO_GUEST_UTILISATION	3

