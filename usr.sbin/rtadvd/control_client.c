/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (C) 2011 Hiroki Sato <hrs@FreeBSD.org>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE PROJECT AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE PROJECT OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
 * BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
 * ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * $FreeBSD$
 *
 */

#include <sys/queue.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <sys/uio.h>
#include <net/if.h>
#include <net/if_dl.h>
#include <netinet/in.h>
#include <netinet/icmp6.h>
#include <fcntl.h>
#include <errno.h>
#include <netdb.h>
#include <unistd.h>
#include <signal.h>
#include <string.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <syslog.h>

#include "pathnames.h"
#include "rtadvd.h"
#include "if.h"
#include "control.h"
#include "control_client.h"

int
cm_handler_client(int fd, int state, char *buf_orig)
{
	char buf[CM_MSG_MAXLEN];
	struct ctrl_msg_hdr *cm;
	struct ctrl_msg_hdr *cm_orig;
	int error;
	char *msg;
	char *msg_orig;

	syslog(LOG_DEBUG, "<%s> enter", __func__);

	memset(buf, 0, sizeof(buf));
	cm = (struct ctrl_msg_hdr *)buf;
	cm_orig = (struct ctrl_msg_hdr *)buf_orig;
	msg = (char *)buf + sizeof(*cm);
	msg_orig = (char *)buf_orig + sizeof(*cm_orig);

	if (cm_orig->cm_len > CM_MSG_MAXLEN) {
		syslog(LOG_DEBUG, "<%s> msg too long", __func__);
		close(fd);
		return (-1);
	}
	cm->cm_type = cm_orig->cm_type;
	if (cm_orig->cm_len > sizeof(*cm_orig)) {
		memcpy(msg, msg_orig, cm_orig->cm_len - sizeof(*cm));
		cm->cm_len = cm_orig->cm_len;
	}
	while (state != CM_STATE_EOM) {
		syslog(LOG_DEBUG, "<%s> state = %d", __func__, state);

		switch (state) {
		case CM_STATE_INIT:
			state = CM_STATE_EOM;
			break;
		case CM_STATE_MSG_DISPATCH:
			cm->cm_version = CM_VERSION;
			error = cm_send(fd, buf);
			if (error) {
				syslog(LOG_WARNING,
				    "<%s> cm_send()", __func__);
				return (-1);
			}
			state = CM_STATE_ACK_WAIT;
			break;
		case CM_STATE_ACK_WAIT:
			error = cm_recv(fd, buf);
			if (error) {
				syslog(LOG_ERR,
				    "<%s> cm_recv()", __func__);
				close(fd);
				return (-1);
			}
			switch (cm->cm_type) {
			case CM_TYPE_ACK:
				syslog(LOG_DEBUG,
				    "<%s> CM_TYPE_ACK", __func__);
				break;
			case CM_TYPE_ERR:
				syslog(LOG_DEBUG,
				    "<%s> CM_TYPE_ERR", __func__);
				close(fd);
				return (-1);
			default:
				syslog(LOG_DEBUG,
				    "<%s> unknown status", __func__);
				close(fd);
				return (-1);
			}
			memcpy(buf_orig, buf, cm->cm_len);
			state = CM_STATE_EOM;
			break;
		}
	}
	close(fd);
	return (0);
}
