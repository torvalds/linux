// SPDX-License-Identifier: GPL-2.0-only
/*
 * Handle the TLS Alert protocol
 *
 * Author: Chuck Lever <chuck.lever@oracle.com>
 *
 * Copyright (c) 2023, Oracle and/or its affiliates.
 */

#include <linux/types.h>
#include <linux/socket.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/skbuff.h>
#include <linux/inet.h>

#include <net/sock.h>
#include <net/handshake.h>
#include <net/tls.h>
#include <net/tls_prot.h>

#include "handshake.h"

/**
 * tls_alert_send - send a TLS Alert on a kTLS socket
 * @sock: open kTLS socket to send on
 * @level: TLS Alert level
 * @description: TLS Alert description
 *
 * Returns zero on success or a negative errno.
 */
int tls_alert_send(struct socket *sock, u8 level, u8 description)
{
	u8 record_type = TLS_RECORD_TYPE_ALERT;
	u8 buf[CMSG_SPACE(sizeof(record_type))];
	struct msghdr msg = { 0 };
	struct cmsghdr *cmsg;
	struct kvec iov;
	u8 alert[2];
	int ret;

	alert[0] = level;
	alert[1] = description;
	iov.iov_base = alert;
	iov.iov_len = sizeof(alert);

	memset(buf, 0, sizeof(buf));
	msg.msg_control = buf;
	msg.msg_controllen = sizeof(buf);
	msg.msg_flags = MSG_DONTWAIT;

	cmsg = CMSG_FIRSTHDR(&msg);
	cmsg->cmsg_level = SOL_TLS;
	cmsg->cmsg_type = TLS_SET_RECORD_TYPE;
	cmsg->cmsg_len = CMSG_LEN(sizeof(record_type));
	memcpy(CMSG_DATA(cmsg), &record_type, sizeof(record_type));

	iov_iter_kvec(&msg.msg_iter, ITER_SOURCE, &iov, 1, iov.iov_len);
	ret = sock_sendmsg(sock, &msg);
	return ret < 0 ? ret : 0;
}
