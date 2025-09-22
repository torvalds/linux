/*	$OpenBSD: acl.c,v 1.17 2022/12/28 21:30:19 jmc Exp $ */

/*
 * Copyright (c) 1994 Mats O Jansson <moj@stacken.kth.se>
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS
 * OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <string.h>
#include <netdb.h>
#include "acl.h"

#define TRUE 1
#define FALSE 0

static	struct aclent *acl_root = NULL;

static int
acl_read_line(FILE *fp, char *buf, int size)
{
	int	 len = 0;
	char *c, *p, l;

	/* Read a line, and remove any comment, trim space */

	do {
		while (fgets(buf, size, fp)) {
			c = buf;
			while (*c != '\0') {
				if (*c == '#' || *c == '\n') {
					*c = '\0';
				} else {
					c++;
				}
			}

			c = p = buf; l = ' ';
			while (*c != '\0') {
				if (isspace((unsigned char)l) &&
				    isspace((unsigned char)*c)) {
					c++;
				} else {
					l = *c++; *p = l; p++;
				}
			}
			*p = '\0';

			if (p != buf) {
				--p;
				if (isspace((unsigned char)*p) != 0) {
					*p = '\0';
				}
			}

			len = strlen(buf);
			return len + 1;
		}
	} while (size > 0 && !feof(fp));
	return len;
}

int
acl_check_host(struct in_addr *addr)
{
	struct aclent *p;

	p = acl_root;
	while (p != NULL) {
		if ((addr->s_addr & p->s_mask) == p->s_addr)
			return(p->allow);
		p = p->next;
	}
	return(TRUE);
}

static void
acl_add_net(int	allow, struct in_addr *addr, struct in_addr *mask)
{
	struct aclent *acl, *p;

	acl = malloc(sizeof(struct aclent));
	acl->next	 = NULL;
	acl->allow	= allow;
	acl->s_addr = addr->s_addr;
	acl->s_mask = mask->s_addr;

	if (acl_root == NULL) {
		acl_root = acl;
	} else {
		p = acl_root;
		while (p->next != NULL)
			p = p->next;
		p->next = acl;
	}
}

static void
acl_add_host(int allow, struct in_addr *addr)
{
	struct in_addr mask;

	mask.s_addr = htonl(0xffffffff);
	acl_add_net(allow, addr, &mask);
}

int
acl_init(char *file)
{
	char data_line[1024], *p, *k;
	int line_no = 0, len, i, state;
	int allow = TRUE, error_cnt = 0;
	struct in_addr addr, mask, *host_addr;
	struct hostent *host;
	FILE *data_file = NULL;

	if (file != NULL)
		data_file = fopen(file, "r");

	while (data_file != NULL &&
	    acl_read_line(data_file, data_line, sizeof(data_line))) {

		line_no++;
		len = strlen(data_line);
		if (len == 0)
			continue;
		p = (char *) &data_line;

		/* State 1: Initial State */

		state = ACLS_INIT;
		addr.s_addr = mask.s_addr = 0;

		k = p;			/* save start of verb */
		i = 0;
		while (*p != '\0' &&
		    !isspace((*p = tolower(*p)))) {
			p++;
			i++;
		}

		if (*p != '\0')
			*p++ = '\0';

		if (strcmp(k, "allow") == 0) {
			allow = TRUE;
			state = ACLS_ALLOW;
		}

		if (strcmp(k, "deny") == 0) {
			allow = FALSE;
			state = ACLS_DENY;
		}

		if (state == ACLS_INIT)
			state = ACLE_UVERB;

		/* State 2: allow row */
		/* State 3: deny row */

		if (*p != '\0' &&
		    (state == ACLS_ALLOW || state == ACLS_DENY)) {
			k = p;			/* save start of verb */
			i = 0;
			while (*p != '\0' &&
			    !isspace((*p = tolower(*p)))) {
				p++;
				i++;
			}

			if (*p != '\0')
				*p++ = '\0';

			if (strcmp(k, "all") == 0)
				state = state + ACLD_ALL;

			if (strcmp(k, "host") == 0)
				state = state + ACLD_HOST;

			if (strcmp(k, "net") == 0)
				state = state + ACLD_NET;

			if (state == ACLS_ALLOW || state == ACLS_DENY)
				state = ACLE_U2VERB;
		}

		if (state == ACLS_ALLOW || state == ACLS_DENY)
			state = ACLE_UEOL;

		/* State 4 & 5: all state, remove any comment */

		if (*p == '\0' &&
		    (state == ACLS_ALLOW_ALL || state == ACLS_DENY_ALL)) {
			acl_add_net(allow, &addr, &mask);
			state = ACLE_OK;
		}

		/* State 6 & 7: host line */
		/* State 8 & 9: net line */

		if (*p != '\0' &&
		    state >= ACLS_ALLOW_HOST && state <= ACLS_DENY_NET) {

			k = p;			/* save start of verb */
			i = 0;
			while (*p != '\0' &&
			    !isspace((*p = tolower(*p)))) {
				p++;
				i++;
			}

			if (*p != '\0')
				*p++ = '\0';

			if (state == ACLS_ALLOW_HOST || state == ACLS_DENY_HOST) {
				if (*k >= '0' && *k <= '9') {
					(void)inet_aton(k, &addr);
					acl_add_host(allow, &addr);
					state = state + ACLD_HOST_DONE;
				} else {
					host = gethostbyname(k);
					if (host == NULL) {
						state = ACLE_NOHOST;
					} else {
						if (host->h_addrtype == AF_INET) {
							while ((host_addr = (struct in_addr *) *host->h_addr_list++) != NULL)
								acl_add_host(allow, host_addr);
						}
						state = state + ACLD_HOST_DONE;
					}
				}
			}

			if (state == ACLS_ALLOW_NET || state == ACLS_DENY_NET) {
				if (*k >= '0' && *k <= '9') {
					(void)inet_aton(k, &addr);
					state = state + ACLD_NET_DONE;
				} else
					state = ACLE_NONET;
			}

		}

		if (state >= ACLS_ALLOW_HOST && state <= ACLS_DENY_NET)
			state = ACLE_UEOL;


		/* State 10 & 11: allow/deny host line */
		if (*p == '\0' &&
		    (state == ACLS_ALLOW_HOST_DONE || state == ACLS_DENY_HOST_DONE))
			state = ACLE_OK;

		/* State 12 & 13: allow/deny net line */
		if (*p == '\0' &&
		    (state == ACLS_ALLOW_NET_DONE || state == ACLS_DENY_NET_DONE)) {
			mask.s_addr = htonl(0xffffff00);
			if (ntohl(addr.s_addr) < 0xc0000000)
				mask.s_addr = htonl(0xffff0000);
			if (ntohl(addr.s_addr) < 0x80000000)
				mask.s_addr = htonl(0xff000000);
			acl_add_net(allow, &addr, &mask);
			state = ACLE_OK;
		}

		if (*p != '\0' &&
		    (state == ACLS_ALLOW_NET_DONE || state == ACLS_DENY_NET_DONE)) {

			k = p;			/* save start of verb */
			i = 0;
			while (*p != '\0' &&
			    !isspace((*p = tolower(*p)))) {
				p++;
				i++;
			}

			if (*p != '\0')
				*p++ = '\0';

			if (strcmp(k, "netmask") == 0)
				state = state + ACLD_NET_MASK;

			if (state == ACLS_ALLOW_NET_DONE ||
			    state == ACLS_DENY_NET_DONE)
				state = ACLE_NONETMASK;
		}

		/* State 14 & 15: allow/deny net netmask line */
		if (*p != '\0' &&
		    (state == ACLS_ALLOW_NET_MASK || state == ACLS_DENY_NET_MASK)) {

			k = p;		/* save start of verb */
			i = 0;
			while (*p != '\0' &&
			    !isspace((*p = tolower(*p)))) {
				p++;
				i++;
			}

			if (*p != '\0')
				*p++ = '\0';

			if (state == ACLS_ALLOW_NET_MASK ||
			    state == ACLS_DENY_NET_MASK) {
				if (*k >= '0' && *k <= '9') {
					(void)inet_aton(k, &mask);
					state = state + ACLD_NET_EOL;
				} else
					state = ACLE_NONET;
			}

		}

		if (state == ACLS_ALLOW_NET_MASK || state == ACLS_DENY_NET_MASK)
			state = ACLE_UEOL;

		/* State 16 & 17: allow/deny host line */
		if (*p == '\0' &&
		    (state == ACLS_ALLOW_NET_EOL || state == ACLS_DENY_NET_EOL)) {
			acl_add_net(allow, &addr, &mask);
			state = ACLE_OK;
		}

		switch (state) {
		case ACLE_NONETMASK:
			fprintf(stderr,
			    "acl: expected \"netmask\" missing at line %d\n",
			    line_no);
			break;
		case ACLE_NONET:
			error_cnt++;
			fprintf(stderr, "acl: unknown network at line %d\n",
			    line_no);
			break;
		case ACLE_NOHOST:
			error_cnt++;
			fprintf(stderr, "acl: unknown host at line %d\n",
			    line_no);
			break;
		case ACLE_UVERB:
			error_cnt++;
			fprintf(stderr, "acl: unknown verb at line %d\n",
			    line_no);
			break;
		case ACLE_U2VERB:
			error_cnt++;
			fprintf(stderr,
			    "acl: unknown secondary verb at line %d\n",
			    line_no);
			break;
		case ACLE_UEOL:
			error_cnt++;
			fprintf(stderr,
			    "acl: unexpected end of line at line %d\n",
			    line_no);
			break;
		case ACLE_OK:
			break;
		default:
			error_cnt++;
			fprintf(stderr, "acl: unexpected state %d %s\n",
			    state, k);
		}

	}

	if (data_file != NULL) {
		(void)fflush(stderr);
		(void)fclose(data_file);
	}

	/* Always add a last allow all if file don't exists or */
	/* the file doesn't cover all cases. */
	addr.s_addr = mask.s_addr = 0;
	allow = TRUE;
	acl_add_net(allow, &addr, &mask);
	return(error_cnt);
}

int
acl_securenet(char *file)
{
	char data_line[1024], *p, *k;
	int line_no = 0, len, i, allow = TRUE, state;
	int error_cnt = 0;
	struct in_addr addr, mask;
	FILE *data_file = NULL;

	if (file != NULL)
		data_file = fopen(file, "r");

	/* Always add a localhost allow first, to be compatible with sun */
	addr.s_addr = htonl(0x7f000001);
	mask.s_addr = htonl(0xffffffff);
	allow = TRUE;
	acl_add_net(allow, &addr, &mask);

	while (data_file != NULL &&
	    acl_read_line(data_file, data_line, sizeof(data_line))) {
		line_no++;
		len = strlen(data_line);
		if (len == 0)
			continue;
		p = (char *) &data_line;

		/* State 1: Initial State */
		state = ACLS_INIT;
		addr.s_addr = mask.s_addr = 0;

		k = p;				/* save start of verb */
		i = 0;
		while (*p != '\0' &&
		    !isspace((*p = tolower(*p)))) {
			p++;
			i++;
		}

		if (*p != '\0') {
			*p++ = '\0';
			state = ACLS_ALLOW_NET_MASK;
		}

		if (state == ACLS_INIT)
			state = ACLE_UEOL;

		if (state == ACLS_ALLOW_NET_MASK) {
			if (*k >= '0' && *k <= '9') {
				(void)inet_aton(k, &mask);
				state = ACLS_ALLOW_NET;
			} else
				state = ACLE_NONET;

			k = p;				/* save start of verb */
			i = 0;
			while (*p != '\0' &&
			    !isspace((*p = tolower(*p)))) {
				p++;
				i++;
			}

			if (*p != '\0')
				*p++ = '\0';
		}

		if (state == ACLS_ALLOW_NET_MASK)
			state = ACLE_UEOL;

		if (state == ACLS_ALLOW_NET) {
			if (*k >= '0' && *k <= '9') {
				(void)inet_aton(k, &addr);
				state = ACLS_ALLOW_NET_EOL;
			} else
				state = ACLE_NONET;
		}

		if (state == ACLS_ALLOW_NET)
			state = ACLE_UEOL;

		if (*p == '\0' && state == ACLS_ALLOW_NET_EOL) {
			acl_add_net(allow, &addr, &mask);
			state = ACLE_OK;
		}

		switch (state) {
		case ACLE_NONET:
			error_cnt++;
			fprintf(stderr,
			    "securenet: unknown network at line %d\n",
			    line_no);
			break;
		case ACLE_UEOL:
			error_cnt++;
			fprintf(stderr,
			    "securenet: unexpected end of line at line %d\n",
			    line_no);
			break;
		case ACLE_OK:
			break;
		default:
			error_cnt++;
			fprintf(stderr, "securenet: unexpected state %d %s\n",
			    state, k);
		}
	}

	if (data_file != NULL) {
		(void)fflush(stderr);
		(void)fclose(data_file);

		/* Always add a last deny all if file exists */
		addr.s_addr = mask.s_addr = 0;
		allow = FALSE;
		acl_add_net(allow, &addr, &mask);
	}

	/* Always add a last allow all if file don't exists */

	addr.s_addr = mask.s_addr = 0;
	allow = TRUE;
	acl_add_net(allow, &addr, &mask);
	return(error_cnt);
}

void
acl_reset(void)
{
	struct aclent *p;

	while (acl_root != NULL) {
		p = acl_root->next;
		free(acl_root);
		acl_root = p;
	}
}
