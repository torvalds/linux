/*-
 * Copyright (c) 2006, Andrea Bittau <a.bittau@cs.ucl.ac.uk>
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $FreeBSD$
 */
#include <sys/uio.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in_systm.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netinet/ip.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <signal.h>
#include <err.h>

void hexdump(void *b, int len)
{       
        unsigned char *p = (unsigned char*) b;

        while (len--)
                printf("%.2X ", *p++);
        printf("\n");
}

int handle_data(int dude, char *buf, int len)
{
	struct ip *ih;
	unsigned short id;
	char tmp[4];
	struct iovec iov[2];
	struct msghdr mh;

	ih = (struct ip*) buf;

	/* XXX IP FRAGS */

	/* filter */
	if (ih->ip_p != 0)
		return 0;

	if (ih->ip_hl != 5)
		return 0;

	/* get info */
	id = ih->ip_id;
	len -= 20;
	buf += 20;
	printf("Got %d bytes [%d]\n", len, ntohs(id));
#if 0	
	hexdump(buf, len);
#endif

	/* prepare packet */
	memcpy(tmp, &id, 2);
	id = htons(len);
	memcpy(&tmp[2], &id, 2);

	iov[0].iov_base = tmp;
	iov[0].iov_len = 4;
	iov[1].iov_base = buf;
	iov[1].iov_len = len;

	memset(&mh, 0, sizeof(mh));
	mh.msg_iov = iov;
	mh.msg_iovlen = sizeof(iov)/sizeof(struct iovec);

	/* write */
	if (sendmsg(dude, &mh, 0) != (4 + len))
		return -1;
	return 0;
}

void handle_dude(int dude, int raw)
{
	char buf[4096];
	int rd;

	while (1) {
		rd = recv(raw, buf, sizeof(buf), 0);
		if (rd == -1)
			err(1, "recv()");
		
		if (handle_data(dude, buf, rd) == -1)
			return;
	}
}

void hand(int s)
{
	printf("sigpipe\n");
}

int main(int argc, char *argv[])
{
	int s, dude;
	struct sockaddr_in s_in;
	int len;
	int raw;

	memset(&s_in, 0, sizeof(s_in));
	s_in.sin_family = PF_INET;
	s_in.sin_port = htons(666);
	s_in.sin_addr.s_addr = INADDR_ANY;

	if ((raw = socket(PF_INET, SOCK_RAW, 0)) == -1)
		err(1, "socket()");

	if ((s = socket(s_in.sin_family, SOCK_STREAM, IPPROTO_TCP)) == -1)
		err(1, "socket()");

	if (bind(s, (struct sockaddr*)&s_in, sizeof(s_in)) == -1)
		err(1, "bind()");

	if (listen(s, 5) == -1)
		err(1, "listen()");

	if (signal(SIGPIPE, hand) == SIG_ERR)
		err(1, "signal()");

	while (1) {
		len = sizeof(s_in);
		dude = accept(s, (struct sockaddr*)&s_in, &len);
		if (dude == -1)
			err(1, "accept()");

		printf("Got dude %s\n", inet_ntoa(s_in.sin_addr));
		handle_dude(dude, raw);
		printf("Done\n");
	}
}
