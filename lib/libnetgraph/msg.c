/*
 * msg.c
 *
 * Copyright (c) 1996-1999 Whistle Communications, Inc.
 * All rights reserved.
 * 
 * Subject to the following obligations and disclaimer of warranty, use and
 * redistribution of this software, in source or object code forms, with or
 * without modifications are expressly permitted by Whistle Communications;
 * provided, however, that:
 * 1. Any and all reproductions of the source or object code must include the
 *    copyright notice above and the following disclaimer of warranties; and
 * 2. No rights are granted, in any manner or form, to use Whistle
 *    Communications, Inc. trademarks, including the mark "WHISTLE
 *    COMMUNICATIONS" on advertising, endorsements, or otherwise except as
 *    such appears in the above copyright notice or in the software.
 * 
 * THIS SOFTWARE IS BEING PROVIDED BY WHISTLE COMMUNICATIONS "AS IS", AND
 * TO THE MAXIMUM EXTENT PERMITTED BY LAW, WHISTLE COMMUNICATIONS MAKES NO
 * REPRESENTATIONS OR WARRANTIES, EXPRESS OR IMPLIED, REGARDING THIS SOFTWARE,
 * INCLUDING WITHOUT LIMITATION, ANY AND ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE, OR NON-INFRINGEMENT.
 * WHISTLE COMMUNICATIONS DOES NOT WARRANT, GUARANTEE, OR MAKE ANY
 * REPRESENTATIONS REGARDING THE USE OF, OR THE RESULTS OF THE USE OF THIS
 * SOFTWARE IN TERMS OF ITS CORRECTNESS, ACCURACY, RELIABILITY OR OTHERWISE.
 * IN NO EVENT SHALL WHISTLE COMMUNICATIONS BE LIABLE FOR ANY DAMAGES
 * RESULTING FROM OR ARISING OUT OF ANY USE OF THIS SOFTWARE, INCLUDING
 * WITHOUT LIMITATION, ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY,
 * PUNITIVE, OR CONSEQUENTIAL DAMAGES, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES, LOSS OF USE, DATA OR PROFITS, HOWEVER CAUSED AND UNDER ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF WHISTLE COMMUNICATIONS IS ADVISED OF THE POSSIBILITY
 * OF SUCH DAMAGE.
 *
 * Author: Archie Cobbs <archie@whistle.com>
 *
 * $Whistle: msg.c,v 1.9 1999/01/20 00:57:23 archie Exp $
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/types.h>
#include <sys/socket.h>
#include <stdarg.h>
#include <netgraph/ng_message.h>
#include <netgraph/ng_socket.h>

#include "netgraph.h"
#include "internal.h"

/* Next message token value */
static int	gMsgId;

/* For delivering both messages and replies */
static int	NgDeliverMsg(int cs, const char *path,
		  const struct ng_mesg *hdr, const void *args, size_t arglen);

/*
 * Send a message to a node using control socket node "cs".
 * Returns -1 if error and sets errno appropriately.
 * If successful, returns the message ID (token) used.
 */
int
NgSendMsg(int cs, const char *path,
	  int cookie, int cmd, const void *args, size_t arglen)
{
	struct ng_mesg msg;

	/* Prepare message header */
	memset(&msg, 0, sizeof(msg));
	msg.header.version = NG_VERSION;
	msg.header.typecookie = cookie;
	if (++gMsgId < 0)
		gMsgId = 1;
	msg.header.token = gMsgId;
	msg.header.flags = NGF_ORIG;
	msg.header.cmd = cmd;
	snprintf((char *)msg.header.cmdstr, NG_CMDSTRSIZ, "cmd%d", cmd);

	/* Deliver message */
	if (NgDeliverMsg(cs, path, &msg, args, arglen) < 0)
		return (-1);
	return (msg.header.token);
}

/*
 * Send a message given in ASCII format. We first ask the node to translate
 * the command into binary, and then we send the binary.
 */
int
NgSendAsciiMsg(int cs, const char *path, const char *fmt, ...)
{
	struct ng_mesg *reply, *binary, *ascii;
	char *buf, *cmd, *args;
	va_list fmtargs;
	int token;

	/* Parse out command and arguments */
	va_start(fmtargs, fmt);
	vasprintf(&buf, fmt, fmtargs);
	va_end(fmtargs);
	if (buf == NULL)
		return (-1);

	/* Parse out command, arguments */
	for (cmd = buf; isspace(*cmd); cmd++)
		;
	for (args = cmd; *args != '\0' && !isspace(*args); args++)
		;
	if (*args != '\0') {
		while (isspace(*args))
			*args++ = '\0';
	}

	/* Get a bigger buffer to hold inner message header plus arg string */
	if ((ascii = malloc(sizeof(struct ng_mesg)
	    + strlen(args) + 1)) == NULL) {
		free(buf);
		return (-1);
	}
	memset(ascii, 0, sizeof(*ascii));

	/* Build inner header (only need cmdstr, arglen, and data fields) */
	strncpy((char *)ascii->header.cmdstr, cmd,
	    sizeof(ascii->header.cmdstr) - 1);
	strcpy(ascii->data, args);
	ascii->header.arglen = strlen(ascii->data) + 1;
	free(buf);

	/* Send node a request to convert ASCII to binary */
	if (NgSendMsg(cs, path, NGM_GENERIC_COOKIE, NGM_ASCII2BINARY,
	    (u_char *)ascii, sizeof(*ascii) + ascii->header.arglen) < 0) {
		free(ascii);
		return (-1);
	}
	free(ascii);

	/* Get reply */
	if (NgAllocRecvMsg(cs, &reply, NULL) < 0)
		return (-1);

	/* Now send binary version */
	binary = (struct ng_mesg *)reply->data;
	if (++gMsgId < 0)
		gMsgId = 1;
	binary->header.token = gMsgId;
	binary->header.version = NG_VERSION;
	if (NgDeliverMsg(cs,
	    path, binary, binary->data, binary->header.arglen) < 0) {
		free(reply);
		return (-1);
	}
	token = binary->header.token;
	free(reply);
	return (token);
}

/*
 * Send a message that is a reply to a previously received message.
 * Returns -1 and sets errno on error, otherwise returns zero.
 */
int
NgSendReplyMsg(int cs, const char *path,
	const struct ng_mesg *msg, const void *args, size_t arglen)
{
	struct ng_mesg rep;

	/* Prepare message header */
	rep = *msg;
	rep.header.flags = NGF_RESP;

	/* Deliver message */
	return (NgDeliverMsg(cs, path, &rep, args, arglen));
}

/*
 * Send a message to a node using control socket node "cs".
 * Returns -1 if error and sets errno appropriately, otherwise zero.
 */
static int
NgDeliverMsg(int cs, const char *path,
	const struct ng_mesg *hdr, const void *args, size_t arglen)
{
	u_char sgbuf[NG_PATHSIZ + NGSA_OVERHEAD];
	struct sockaddr_ng *const sg = (struct sockaddr_ng *) sgbuf;
	u_char *buf = NULL;
	struct ng_mesg *msg;
	int errnosv = 0;
	int rtn = 0;

	/* Sanity check */
	if (args == NULL)
		arglen = 0;

	/* Get buffer */
	if ((buf = malloc(sizeof(*msg) + arglen)) == NULL) {
		errnosv = errno;
		if (_gNgDebugLevel >= 1)
			NGLOG("malloc");
		rtn = -1;
		goto done;
	}
	msg = (struct ng_mesg *) buf;

	/* Finalize message */
	*msg = *hdr;
	msg->header.arglen = arglen;
	memcpy(msg->data, args, arglen);

	/* Prepare socket address */
	sg->sg_family = AF_NETGRAPH;
	/* XXX handle overflow */
	strlcpy(sg->sg_data, path, NG_PATHSIZ);
	sg->sg_len = strlen(sg->sg_data) + 1 + NGSA_OVERHEAD;

	/* Debugging */
	if (_gNgDebugLevel >= 2) {
		NGLOGX("SENDING %s:",
		    (msg->header.flags & NGF_RESP) ? "RESPONSE" : "MESSAGE");
		_NgDebugSockaddr(sg);
		_NgDebugMsg(msg, sg->sg_data);
	}

	/* Send it */
	if (sendto(cs, msg, sizeof(*msg) + arglen,
		   0, (struct sockaddr *) sg, sg->sg_len) < 0) {
		errnosv = errno;
		if (_gNgDebugLevel >= 1)
			NGLOG("sendto(%s)", sg->sg_data);
		rtn = -1;
		goto done;
	}

	/* Wait for reply if there should be one. */
	if (msg->header.cmd & NGM_HASREPLY && !(msg->header.flags & NGF_RESP)) {
		struct pollfd rfds;
		int n;

		rfds.fd = cs;
		rfds.events = POLLIN;
		rfds.revents = 0;
		n = poll(&rfds, 1, INFTIM);
		if (n == -1) {
			errnosv = errno;
			if (_gNgDebugLevel >= 1)
				NGLOG("poll");
			rtn = -1;
		}
	}

done:
	/* Done */
	free(buf);		/* OK if buf is NULL */
	errno = errnosv;
	return (rtn);
}

/*
 * Receive a control message.
 *
 * On error, this returns -1 and sets errno.
 * Otherwise, it returns the length of the received reply.
 */
int
NgRecvMsg(int cs, struct ng_mesg *rep, size_t replen, char *path)
{
	u_char sgbuf[NG_PATHSIZ + NGSA_OVERHEAD];
	struct sockaddr_ng *const sg = (struct sockaddr_ng *) sgbuf;
	socklen_t sglen = sizeof(sgbuf);
	int len, errnosv;

	/* Read reply */
	len = recvfrom(cs, rep, replen, 0, (struct sockaddr *) sg, &sglen);
	if (len < 0) {
		errnosv = errno;
		if (_gNgDebugLevel >= 1)
			NGLOG("recvfrom");
		goto errout;
	}
	if (path != NULL)
		strlcpy(path, sg->sg_data, NG_PATHSIZ);

	/* Debugging */
	if (_gNgDebugLevel >= 2) {
		NGLOGX("RECEIVED %s:",
		    (rep->header.flags & NGF_RESP) ? "RESPONSE" : "MESSAGE");
		_NgDebugSockaddr(sg);
		_NgDebugMsg(rep, sg->sg_data);
	}

	/* Done */
	return (len);

errout:
	errno = errnosv;
	return (-1);
}

/*
 * Identical to NgRecvMsg() except buffer is dynamically allocated.
 */
int
NgAllocRecvMsg(int cs, struct ng_mesg **rep, char *path)
{
	int len;
	socklen_t optlen;

	optlen = sizeof(len);
	if (getsockopt(cs, SOL_SOCKET, SO_RCVBUF, &len, &optlen) == -1 ||
	    (*rep = malloc(len)) == NULL)
		return (-1);
	if ((len = NgRecvMsg(cs, *rep, len, path)) < 0)
		free(*rep);
	return (len);
}

/*
 * Receive a control message and convert the arguments to ASCII
 */
int
NgRecvAsciiMsg(int cs, struct ng_mesg *reply, size_t replen, char *path)
{
	struct ng_mesg *msg, *ascii;
	int bufSize, errnosv;
	u_char *buf;

	/* Allocate buffer */
	bufSize = 2 * sizeof(*reply) + replen;
	if ((buf = malloc(bufSize)) == NULL)
		return (-1);
	msg = (struct ng_mesg *)buf;
	ascii = (struct ng_mesg *)msg->data;

	/* Get binary message */
	if (NgRecvMsg(cs, msg, bufSize, path) < 0)
		goto fail;
	memcpy(reply, msg, sizeof(*msg));

	/* Ask originating node to convert the arguments to ASCII */
	if (NgSendMsg(cs, path, NGM_GENERIC_COOKIE,
	    NGM_BINARY2ASCII, msg, sizeof(*msg) + msg->header.arglen) < 0)
		goto fail;
	if (NgRecvMsg(cs, msg, bufSize, NULL) < 0)
		goto fail;

	/* Copy result to client buffer */
	if (sizeof(*ascii) + ascii->header.arglen > replen) {
		errno = ERANGE;
fail:
		errnosv = errno;
		free(buf);
		errno = errnosv;
		return (-1);
	}
	strncpy(reply->data, ascii->data, ascii->header.arglen);

	/* Done */
	free(buf);
	return (0);
}

/*
 * Identical to NgRecvAsciiMsg() except buffer is dynamically allocated.
 */
int
NgAllocRecvAsciiMsg(int cs, struct ng_mesg **reply, char *path)
{
	int len;
	socklen_t optlen;

	optlen = sizeof(len);
	if (getsockopt(cs, SOL_SOCKET, SO_RCVBUF, &len, &optlen) == -1 ||
	    (*reply = malloc(len)) == NULL)
		return (-1);
	if ((len = NgRecvAsciiMsg(cs, *reply, len, path)) < 0)
		free(*reply);
	return (len);
}
