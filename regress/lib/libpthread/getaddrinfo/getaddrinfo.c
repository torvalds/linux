/*	$OpenBSD: getaddrinfo.c,v 1.3 2003/01/18 01:48:21 marc Exp $	*/
/*
 * Copyright (c) 2002 Todd T. Fries <todd@OpenBSD.org>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES,
 * INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY
 * AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL
 * THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
 * OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
 * ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <pthread.h>
#include <netdb.h>
#include <resolv.h>

#include "test.h"

#define STACK_SIZE	(2 * 1024 * 1024)

void	*func(void *);

int
main(argc, argv)
	int argc;
	char **argv;
{
	pthread_attr_t attr;
	pthread_t threads[2];
	int i;

	CHECKr(pthread_attr_init(&attr));
	CHECKr(pthread_attr_setstacksize(&attr, (size_t) STACK_SIZE));
	for (i = 0; i < 2; i++) {
		CHECKr(pthread_create(&threads[i], &attr, func, NULL));
	}

	pthread_yield();
	for (i = 0; i < 2; i++) {
		CHECKr(pthread_join(threads[i], NULL));
	}

	SUCCEED;
}

void *
func(arg)
	void *arg;
{
	struct addrinfo hints, *res;
	char h[NI_MAXHOST];
	int i;

	memset(&hints, 0, sizeof(hints));
	hints.ai_family = PF_UNSPEC;
	hints.ai_socktype = SOCK_DGRAM;
	hints.ai_flags = AI_CANONNAME;

	printf("Starting thread %p\n", pthread_self());

	for(i = 0; i < 50; i++) {
		if (getaddrinfo("www.openbsd.org", "0", &hints, &res))
			printf("error on thread %p\n", pthread_self());
		else {
			getnameinfo(res->ai_addr, res->ai_addrlen, h, sizeof h,
			    NULL, 0, NI_NUMERICHOST);
			printf("success on thread %p: %s is %s\n",
			    pthread_self(), res->ai_canonname, h);
			freeaddrinfo(res);
		}
	}
	return (NULL);
}

