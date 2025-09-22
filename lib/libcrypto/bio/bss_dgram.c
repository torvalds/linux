/* $OpenBSD: bss_dgram.c,v 1.45 2023/07/05 21:23:37 beck Exp $ */
/* 
 * DTLS implementation written by Nagendra Modadugu
 * (nagendra@cs.stanford.edu) for the OpenSSL project 2005.  
 */
/* ====================================================================
 * Copyright (c) 1999-2005 The OpenSSL Project.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer. 
 *
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 *
 * 3. All advertising materials mentioning features or use of this
 *    software must display the following acknowledgment:
 *    "This product includes software developed by the OpenSSL Project
 *    for use in the OpenSSL Toolkit. (http://www.OpenSSL.org/)"
 *
 * 4. The names "OpenSSL Toolkit" and "OpenSSL Project" must not be used to
 *    endorse or promote products derived from this software without
 *    prior written permission. For written permission, please contact
 *    openssl-core@OpenSSL.org.
 *
 * 5. Products derived from this software may not be called "OpenSSL"
 *    nor may "OpenSSL" appear in their names without prior written
 *    permission of the OpenSSL Project.
 *
 * 6. Redistributions of any form whatsoever must retain the following
 *    acknowledgment:
 *    "This product includes software developed by the OpenSSL Project
 *    for use in the OpenSSL Toolkit (http://www.OpenSSL.org/)"
 *
 * THIS SOFTWARE IS PROVIDED BY THE OpenSSL PROJECT ``AS IS'' AND ANY
 * EXPRESSED OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE OpenSSL PROJECT OR
 * ITS CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED
 * OF THE POSSIBILITY OF SUCH DAMAGE.
 * ====================================================================
 *
 * This product includes cryptographic software written by Eric Young
 * (eay@cryptsoft.com).  This product includes software written by Tim
 * Hudson (tjh@cryptsoft.com).
 *
 */

#include <sys/socket.h>
#include <sys/time.h>

#include <netinet/in.h>

#include <errno.h>
#include <netdb.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include <openssl/opensslconf.h>

#include <openssl/bio.h>

#include "bio_local.h"

#ifndef OPENSSL_NO_DGRAM


static int dgram_write(BIO *h, const char *buf, int num);
static int dgram_read(BIO *h, char *buf, int size);
static int dgram_puts(BIO *h, const char *str);
static long dgram_ctrl(BIO *h, int cmd, long arg1, void *arg2);
static int dgram_new(BIO *h);
static int dgram_free(BIO *data);
static int dgram_clear(BIO *bio);


static int BIO_dgram_should_retry(int s);

static const BIO_METHOD methods_dgramp = {
	.type = BIO_TYPE_DGRAM,
	.name = "datagram socket",
	.bwrite = dgram_write,
	.bread = dgram_read,
	.bputs = dgram_puts,
	.ctrl = dgram_ctrl,
	.create = dgram_new,
	.destroy = dgram_free
};


typedef struct bio_dgram_data_st {
	union {
		struct sockaddr sa;
		struct sockaddr_in sa_in;
		struct sockaddr_in6 sa_in6;
	} peer;
	unsigned int connected;
	unsigned int _errno;
	unsigned int mtu;
	struct timeval next_timeout;
	struct timeval socket_timeout;
} bio_dgram_data;


const BIO_METHOD *
BIO_s_datagram(void)
{
	return (&methods_dgramp);
}
LCRYPTO_ALIAS(BIO_s_datagram);

BIO *
BIO_new_dgram(int fd, int close_flag)
{
	BIO *ret;

	ret = BIO_new(BIO_s_datagram());
	if (ret == NULL)
		return (NULL);
	BIO_set_fd(ret, fd, close_flag);
	return (ret);
}
LCRYPTO_ALIAS(BIO_new_dgram);

static int
dgram_new(BIO *bi)
{
	bio_dgram_data *data = NULL;

	bi->init = 0;
	bi->num = 0;
	data = calloc(1, sizeof(bio_dgram_data));
	if (data == NULL)
		return 0;
	bi->ptr = data;

	bi->flags = 0;
	return (1);
}

static int
dgram_free(BIO *a)
{
	bio_dgram_data *data;

	if (a == NULL)
		return (0);
	if (!dgram_clear(a))
		return 0;

	data = (bio_dgram_data *)a->ptr;
	free(data);

	return (1);
}

static int
dgram_clear(BIO *a)
{
	if (a == NULL)
		return (0);
	if (a->shutdown) {
		if (a->init) {
			shutdown(a->num, SHUT_RDWR);
			close(a->num);
		}
		a->init = 0;
		a->flags = 0;
	}
	return (1);
}

static void
dgram_adjust_rcv_timeout(BIO *b)
{
#if defined(SO_RCVTIMEO)
	bio_dgram_data *data = (bio_dgram_data *)b->ptr;

	/* Is a timer active? */
	if (data->next_timeout.tv_sec > 0 || data->next_timeout.tv_usec > 0) {
		struct timeval timenow, timeleft;

		/* Read current socket timeout */
		socklen_t sz = sizeof(data->socket_timeout);
		if (getsockopt(b->num, SOL_SOCKET, SO_RCVTIMEO,
		    &(data->socket_timeout), &sz) < 0) {
			perror("getsockopt");
		}

		/* Get current time */
		gettimeofday(&timenow, NULL);

		/* Calculate time left until timer expires */
		memcpy(&timeleft, &(data->next_timeout), sizeof(struct timeval));
		timeleft.tv_sec -= timenow.tv_sec;
		timeleft.tv_usec -= timenow.tv_usec;
		if (timeleft.tv_usec < 0) {
			timeleft.tv_sec--;
			timeleft.tv_usec += 1000000;
		}

		if (timeleft.tv_sec < 0) {
			timeleft.tv_sec = 0;
			timeleft.tv_usec = 1;
		}

		/* Adjust socket timeout if next handshake message timer
		 * will expire earlier.
		 */
		if ((data->socket_timeout.tv_sec == 0 &&
		    data->socket_timeout.tv_usec == 0) ||
		    (data->socket_timeout.tv_sec > timeleft.tv_sec) ||
		    (data->socket_timeout.tv_sec == timeleft.tv_sec &&
		    data->socket_timeout.tv_usec >= timeleft.tv_usec)) {
			if (setsockopt(b->num, SOL_SOCKET, SO_RCVTIMEO,
			    &timeleft, sizeof(struct timeval)) < 0) {
				perror("setsockopt");
			}
		}
	}
#endif
}

static void
dgram_reset_rcv_timeout(BIO *b)
{
#if defined(SO_RCVTIMEO)
	bio_dgram_data *data = (bio_dgram_data *)b->ptr;

	/* Is a timer active? */
	if (data->next_timeout.tv_sec > 0 || data->next_timeout.tv_usec > 0) {
		if (setsockopt(b->num, SOL_SOCKET, SO_RCVTIMEO,
		    &(data->socket_timeout), sizeof(struct timeval)) < 0) {
			perror("setsockopt");
		}
	}
#endif
}

static int
dgram_read(BIO *b, char *out, int outl)
{
	int ret = 0;
	bio_dgram_data *data = (bio_dgram_data *)b->ptr;

	struct	{
		socklen_t len;
		union	{
			struct sockaddr sa;
			struct sockaddr_in sa_in;
			struct sockaddr_in6 sa_in6;
		} peer;
	} sa;

	sa.len = sizeof(sa.peer);

	if (out != NULL) {
		errno = 0;
		memset(&sa.peer, 0, sizeof(sa.peer));
		dgram_adjust_rcv_timeout(b);
		ret = recvfrom(b->num, out, outl, 0, &sa.peer.sa, &sa.len);

		if (! data->connected  && ret >= 0)
			BIO_ctrl(b, BIO_CTRL_DGRAM_SET_PEER, 0, &sa.peer);

		BIO_clear_retry_flags(b);
		if (ret < 0) {
			if (BIO_dgram_should_retry(ret)) {
				BIO_set_retry_read(b);
				data->_errno = errno;
			}
		}

		dgram_reset_rcv_timeout(b);
	}
	return (ret);
}

static int
dgram_write(BIO *b, const char *in, int inl)
{
	int ret;
	bio_dgram_data *data = (bio_dgram_data *)b->ptr;
	errno = 0;

	if (data->connected)
		ret = write(b->num, in, inl);
	else {
		int peerlen = sizeof(data->peer);

		if (data->peer.sa.sa_family == AF_INET)
			peerlen = sizeof(data->peer.sa_in);
		else if (data->peer.sa.sa_family == AF_INET6)
			peerlen = sizeof(data->peer.sa_in6);
		ret = sendto(b->num, in, inl, 0, &data->peer.sa, peerlen);
	}

	BIO_clear_retry_flags(b);
	if (ret <= 0) {
		if (BIO_dgram_should_retry(ret)) {
			BIO_set_retry_write(b);

			data->_errno = errno;
			/*
			 * higher layers are responsible for querying MTU,
			 * if necessary
			 */
		}
	}
	return (ret);
}

static long
dgram_ctrl(BIO *b, int cmd, long num, void *ptr)
{
	long ret = 1;
	int *ip;
	struct sockaddr *to = NULL;
	bio_dgram_data *data = NULL;
#if (defined(IP_MTU_DISCOVER) || defined(IP_MTU))
	int sockopt_val = 0;
	socklen_t sockopt_len;	/* assume that system supporting IP_MTU is
				 * modern enough to define socklen_t */
	socklen_t addr_len;
	union	{
		struct sockaddr	sa;
		struct sockaddr_in s4;
		struct sockaddr_in6 s6;
	} addr;
#endif

	data = (bio_dgram_data *)b->ptr;

	switch (cmd) {
	case BIO_CTRL_RESET:
		num = 0;
	case BIO_C_FILE_SEEK:
		ret = 0;
		break;
	case BIO_C_FILE_TELL:
	case BIO_CTRL_INFO:
		ret = 0;
		break;
	case BIO_C_SET_FD:
		dgram_clear(b);
		b->num= *((int *)ptr);
		b->shutdown = (int)num;
		b->init = 1;
		break;
	case BIO_C_GET_FD:
		if (b->init) {
			ip = (int *)ptr;
			if (ip != NULL)
				*ip = b->num;
			ret = b->num;
		} else
			ret = -1;
		break;
	case BIO_CTRL_GET_CLOSE:
		ret = b->shutdown;
		break;
	case BIO_CTRL_SET_CLOSE:
		b->shutdown = (int)num;
		break;
	case BIO_CTRL_PENDING:
	case BIO_CTRL_WPENDING:
		ret = 0;
		break;
	case BIO_CTRL_DUP:
	case BIO_CTRL_FLUSH:
		ret = 1;
		break;
	case BIO_CTRL_DGRAM_CONNECT:
		to = (struct sockaddr *)ptr;
		switch (to->sa_family) {
		case AF_INET:
			memcpy(&data->peer, to, sizeof(data->peer.sa_in));
			break;
		case AF_INET6:
			memcpy(&data->peer, to, sizeof(data->peer.sa_in6));
			break;
		default:
			memcpy(&data->peer, to, sizeof(data->peer.sa));
			break;
		}
		break;
		/* (Linux)kernel sets DF bit on outgoing IP packets */
	case BIO_CTRL_DGRAM_MTU_DISCOVER:
#if defined(IP_MTU_DISCOVER) && defined(IP_PMTUDISC_DO)
		addr_len = (socklen_t)sizeof(addr);
		memset((void *)&addr, 0, sizeof(addr));
		if (getsockname(b->num, &addr.sa, &addr_len) < 0) {
			ret = 0;
			break;
		}
		switch (addr.sa.sa_family) {
		case AF_INET:
			sockopt_val = IP_PMTUDISC_DO;
			ret = setsockopt(b->num, IPPROTO_IP, IP_MTU_DISCOVER,
			    &sockopt_val, sizeof(sockopt_val));
			if (ret < 0)
				perror("setsockopt");
			break;
#if defined(IPV6_MTU_DISCOVER) && defined(IPV6_PMTUDISC_DO)
		case AF_INET6:
			sockopt_val = IPV6_PMTUDISC_DO;
			ret = setsockopt(b->num, IPPROTO_IPV6,
			    IPV6_MTU_DISCOVER, &sockopt_val,
			    sizeof(sockopt_val));
			if (ret < 0)
				perror("setsockopt");
			break;
#endif
		default:
			ret = -1;
			break;
		}
#else
		ret = -1;
#endif
		break;
	case BIO_CTRL_DGRAM_QUERY_MTU:
#if defined(IP_MTU)
		addr_len = (socklen_t)sizeof(addr);
		memset((void *)&addr, 0, sizeof(addr));
		if (getsockname(b->num, &addr.sa, &addr_len) < 0) {
			ret = 0;
			break;
		}
		sockopt_len = sizeof(sockopt_val);
		switch (addr.sa.sa_family) {
		case AF_INET:
			ret = getsockopt(b->num, IPPROTO_IP, IP_MTU,
			    &sockopt_val, &sockopt_len);
			if (ret < 0 || sockopt_val < 0) {
				ret = 0;
			} else {
				/* we assume that the transport protocol is UDP and no
				 * IP options are used.
				 */
				data->mtu = sockopt_val - 8 - 20;
				ret = data->mtu;
			}
			break;
#if defined(IPV6_MTU)
		case AF_INET6:
			ret = getsockopt(b->num, IPPROTO_IPV6, IPV6_MTU,
			    &sockopt_val, &sockopt_len);
			if (ret < 0 || sockopt_val < 0) {
				ret = 0;
			} else {
				/* we assume that the transport protocol is UDP and no
				 * IPV6 options are used.
				 */
				data->mtu = sockopt_val - 8 - 40;
				ret = data->mtu;
			}
			break;
#endif
default:
			ret = 0;
			break;
		}
#else
		ret = 0;
#endif
		break;
	case BIO_CTRL_DGRAM_GET_FALLBACK_MTU:
		switch (data->peer.sa.sa_family) {
		case AF_INET:
			ret = 576 - 20 - 8;
			break;
		case AF_INET6:
#ifdef IN6_IS_ADDR_V4MAPPED
			if (IN6_IS_ADDR_V4MAPPED(&data->peer.sa_in6.sin6_addr))
				ret = 576 - 20 - 8;
			else
#endif
				ret = 1280 - 40 - 8;
			break;
		default:
			ret = 576 - 20 - 8;
			break;
		}
		break;
	case BIO_CTRL_DGRAM_GET_MTU:
		return data->mtu;
		break;
	case BIO_CTRL_DGRAM_SET_MTU:
		data->mtu = num;
		ret = num;
		break;
	case BIO_CTRL_DGRAM_SET_CONNECTED:
		to = (struct sockaddr *)ptr;

		if (to != NULL) {
			data->connected = 1;
			switch (to->sa_family) {
			case AF_INET:
				memcpy(&data->peer, to, sizeof(data->peer.sa_in));
				break;
			case AF_INET6:
				memcpy(&data->peer, to, sizeof(data->peer.sa_in6));
				break;
			default:
				memcpy(&data->peer, to, sizeof(data->peer.sa));
				break;
			}
		} else {
			data->connected = 0;
			memset(&(data->peer), 0, sizeof(data->peer));
		}
		break;
	case BIO_CTRL_DGRAM_GET_PEER:
		switch (data->peer.sa.sa_family) {
		case AF_INET:
			ret = sizeof(data->peer.sa_in);
			break;
		case AF_INET6:
			ret = sizeof(data->peer.sa_in6);
			break;
		default:
			ret = sizeof(data->peer.sa);
			break;
		}
		if (num == 0 || num > ret)
			num = ret;
		memcpy(ptr, &data->peer, (ret = num));
		break;
	case BIO_CTRL_DGRAM_SET_PEER:
		to = (struct sockaddr *) ptr;
		switch (to->sa_family) {
		case AF_INET:
			memcpy(&data->peer, to, sizeof(data->peer.sa_in));
			break;
		case AF_INET6:
			memcpy(&data->peer, to, sizeof(data->peer.sa_in6));
			break;
		default:
			memcpy(&data->peer, to, sizeof(data->peer.sa));
			break;
		}
		break;
	case BIO_CTRL_DGRAM_SET_NEXT_TIMEOUT:
		memcpy(&(data->next_timeout), ptr, sizeof(struct timeval));
		break;
#if defined(SO_RCVTIMEO)
	case BIO_CTRL_DGRAM_SET_RECV_TIMEOUT:
		if (setsockopt(b->num, SOL_SOCKET, SO_RCVTIMEO, ptr,
		    sizeof(struct timeval)) < 0) {
			perror("setsockopt");
			ret = -1;
		}
		break;
	case BIO_CTRL_DGRAM_GET_RECV_TIMEOUT:
		{
			socklen_t sz = sizeof(struct timeval);
			if (getsockopt(b->num, SOL_SOCKET, SO_RCVTIMEO,
			    ptr, &sz) < 0) {
				perror("getsockopt");
				ret = -1;
			} else
				ret = sz;
		}
		break;
#endif
#if defined(SO_SNDTIMEO)
	case BIO_CTRL_DGRAM_SET_SEND_TIMEOUT:
		if (setsockopt(b->num, SOL_SOCKET, SO_SNDTIMEO, ptr,
		    sizeof(struct timeval)) < 0) {
			perror("setsockopt");
			ret = -1;
		}
		break;
	case BIO_CTRL_DGRAM_GET_SEND_TIMEOUT:
		{
			socklen_t sz = sizeof(struct timeval);
			if (getsockopt(b->num, SOL_SOCKET, SO_SNDTIMEO,
			    ptr, &sz) < 0) {
				perror("getsockopt");
				ret = -1;
			} else
				ret = sz;
		}
		break;
#endif
	case BIO_CTRL_DGRAM_GET_SEND_TIMER_EXP:
		/* fall-through */
	case BIO_CTRL_DGRAM_GET_RECV_TIMER_EXP:
		if (data->_errno == EAGAIN) {
			ret = 1;
			data->_errno = 0;
		} else
			ret = 0;
		break;
#ifdef EMSGSIZE
	case BIO_CTRL_DGRAM_MTU_EXCEEDED:
		if (data->_errno == EMSGSIZE) {
			ret = 1;
			data->_errno = 0;
		} else
			ret = 0;
		break;
#endif
	default:
		ret = 0;
		break;
	}
	return (ret);
}

static int
dgram_puts(BIO *bp, const char *str)
{
	int n, ret;

	n = strlen(str);
	ret = dgram_write(bp, str, n);
	return (ret);
}


static int
BIO_dgram_should_retry(int i)
{
	int err;

	if ((i == 0) || (i == -1)) {
		err = errno;
		return (BIO_dgram_non_fatal_error(err));
	}
	return (0);
}

int
BIO_dgram_non_fatal_error(int err)
{
	switch (err) {
	case EINTR:
	case EAGAIN:
	case EINPROGRESS:
	case EALREADY:
		return (1);
	default:
		break;
	}
	return (0);
}
LCRYPTO_ALIAS(BIO_dgram_non_fatal_error);

#endif
