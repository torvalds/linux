/*	$OpenBSD: pri.c,v 1.2 2019/11/28 18:40:42 kn Exp $	*/

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

#include <stdio.h>
#include <string.h>

#include "ds.h"
#include "mdesc.h"
#include "ldom_util.h"

void	pri_start(struct ldc_conn *, uint64_t);
void	pri_rx_data(struct ldc_conn *, uint64_t, void *, size_t);

struct ds_service pri_service = {
	"pri", 1, 0, pri_start, pri_rx_data
};

#define PRI_REQUEST	0x00

struct pri_msg {
	uint32_t	msg_type;
	uint32_t	payload_len;
	uint64_t	svc_handle;
	uint64_t	reqnum;
	uint64_t	type;
} __packed;

#define PRI_DATA	0x01

struct pri_data {
	uint32_t	msg_type;
	uint32_t	payload_len;
	uint64_t	svc_handle;
	uint64_t	reqnum;
	uint64_t	type;
	char		data[1];
} __packed;

#define PRI_UPDATE	0x02

struct pri_update {
	uint32_t	msg_type;
	uint32_t	payload_len;
	uint64_t	svc_handle;
	uint64_t	reqnum;
	uint64_t	type;
} __packed;

void
pri_start(struct ldc_conn *lc, uint64_t svc_handle)
{
	struct pri_msg pm;

	bzero(&pm, sizeof(pm));
	pm.msg_type = DS_DATA;
	pm.payload_len = sizeof(pm) - 8;
	pm.svc_handle = svc_handle;
	pm.reqnum = 0;
	pm.type = PRI_REQUEST;
	ds_send_msg(lc, &pm, sizeof(pm));
}

void *pri_buf;
size_t pri_len;

void
pri_rx_data(struct ldc_conn *lc, uint64_t svc_handle, void *data, size_t len)
{
	struct pri_data *pd = data;

	if (pd->type != PRI_DATA) {
		DPRINTF(("Unexpected PRI message type 0x%02llx\n", pd->type));
		return;
	}

	pri_len = pd->payload_len - 24;
	pri_buf = xmalloc(pri_len);

	len -= sizeof(struct pri_msg);
	bcopy(&pd->data, pri_buf, len);
	ds_receive_msg(lc, pri_buf + len, pri_len - len);
}
