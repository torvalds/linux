/*	$OpenBSD: var-config.c,v 1.3 2019/11/28 18:40:42 kn Exp $	*/

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

#include <sys/types.h>
#include <sys/poll.h>
#include <sys/queue.h>
#include <assert.h>
#include <err.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "ds.h"
#include "mdesc.h"
#include "ldom_util.h"
#include "ldomd.h"

void	var_config_start(struct ldc_conn *, uint64_t);
void	var_config_rx_data(struct ldc_conn *, uint64_t, void *, size_t);

struct ds_service var_config_service = {
	"var-config", 1, 0, var_config_start, var_config_rx_data
};

#define VAR_CONFIG_SET_REQ	0x00
#define VAR_CONFIG_DELETE_REQ	0x01

struct var_config_set_req {
	uint32_t	msg_type;
	uint32_t	payload_len;
	uint64_t	svc_handle;
	uint32_t	cmd;
	char		name[1];
} __packed;

#define VAR_CONFIG_SET_RESP	0x02
#define VAR_CONFIG_DELETE_RESP	0x03

struct var_config_resp {
	uint32_t	msg_type;
	uint32_t	payload_len;
	uint64_t	svc_handle;
	uint32_t	cmd;
	uint32_t	result;
} __packed;

#define VAR_CONFIG_SUCCESS		0x00
#define VAR_CONFIG_NO_SPACE		0x01
#define VAR_CONFIG_INVALID_VAR		0x02
#define VAR_CONFIG_INVALID_VAL		0x03
#define VAR_CONFIG_VAR_NOT_PRESENT	0x04

uint32_t
set_variable(struct guest *guest, const char *name, const char *value)
{
	struct md *md = guest->md;
	struct md_node *node;
	struct md_prop *prop;

	node = md_find_node(md, "variables");
	if (node == NULL) {
		struct md_node *root = md_find_node(md, "root");

		assert(root);
		node = md_add_node(md, "variables");
		md_link_node(md, root, node);
	}

	prop = md_add_prop_str(md, node, name, value);
	if (prop == NULL)
		return VAR_CONFIG_NO_SPACE;

	hv_update_md(guest);
	return VAR_CONFIG_SUCCESS;
}

uint32_t
delete_variable(struct guest *guest, const char *name)
{
	struct md *md = guest->md;
	struct md_node *node;
	struct md_prop *prop;

	node = md_find_node(md, "variables");
	if (node == NULL)
		return VAR_CONFIG_VAR_NOT_PRESENT;

	prop = md_find_prop(md, node, name);
	if (prop == NULL)
		return VAR_CONFIG_VAR_NOT_PRESENT;

	md_delete_prop(md, node, prop);

	hv_update_md(guest);
	return VAR_CONFIG_SUCCESS;
}

void
var_config_start(struct ldc_conn *lc, uint64_t svc_handle)
{
}

void
var_config_rx_data(struct ldc_conn *lc, uint64_t svc_handle, void *data,
    size_t len)
{
	struct ds_conn *dc = lc->lc_cookie;
	struct var_config_set_req *vr = data;
	struct var_config_resp vx;

	switch (vr->cmd) {
	case VAR_CONFIG_SET_REQ:
		vx.msg_type = DS_DATA;
		vx.payload_len = sizeof(vx) - 8;
		vx.svc_handle = svc_handle;
		vx.cmd = VAR_CONFIG_SET_RESP;
		vx.result = set_variable(dc->cookie, vr->name,
		    vr->name + strlen(vr->name) + 1);
		ds_send_msg(lc, &vx, sizeof(vx));
		break;
	case VAR_CONFIG_DELETE_REQ:
		vx.msg_type = DS_DATA;
		vx.payload_len = sizeof(vx) - 8;
		vx.svc_handle = svc_handle;
		vx.result = delete_variable(dc->cookie, vr->name);
		vx.cmd = VAR_CONFIG_DELETE_RESP;
		ds_send_msg(lc, &vx, sizeof(vx));
		break;
	default:
		printf("Unknown request 0x%02x\n", vr->cmd);
		break;
	}
}
