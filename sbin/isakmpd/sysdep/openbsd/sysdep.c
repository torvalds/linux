/* $OpenBSD: sysdep.c,v 1.38 2018/01/15 09:54:48 mpi Exp $	 */
/* $EOM: sysdep.c,v 1.9 2000/12/04 04:46:35 angelos Exp $	 */

/*
 * Copyright (c) 1998, 1999 Niklas Hallqvist.  All rights reserved.
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * This code was written under funding by Ericsson Radio Systems.
 */

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include "app.h"
#include "log.h"
#include "monitor.h"
#include "util.h"


/* Force communication on socket FD to go in the clear.  */
int
sysdep_cleartext(int fd, int af)
{
	int level, sw;
	struct {
		int             ip_proto;	/* IP protocol */
		int             auth_level;
		int             esp_trans_level;
		int             esp_network_level;
		int             ipcomp_level;
	} optsw[] = {
	    {
		IPPROTO_IP,
		IP_AUTH_LEVEL,
		IP_ESP_TRANS_LEVEL,
		IP_ESP_NETWORK_LEVEL,
#ifdef IP_IPCOMP_LEVEL
		IP_IPCOMP_LEVEL
#else
		0
#endif
	    }, {
		IPPROTO_IPV6,
		IPV6_AUTH_LEVEL,
		IPV6_ESP_TRANS_LEVEL,
		IPV6_ESP_NETWORK_LEVEL,
#ifdef IPV6_IPCOMP_LEVEL
		IPV6_IPCOMP_LEVEL
#else
		0
#endif
	    },
	};

	if (app_none)
		return 0;

	switch (af) {
	case AF_INET:
		sw = 0;
		break;
	case AF_INET6:
		sw = 1;
		break;
	default:
		log_print("sysdep_cleartext: unsupported protocol family %d", af);
		return -1;
	}

	/*
	 * Need to bypass system security policy, so I can send and
	 * receive key management datagrams in the clear.
	 */
	level = IPSEC_LEVEL_BYPASS;
	if (monitor_setsockopt(fd, optsw[sw].ip_proto, optsw[sw].auth_level,
	    (char *) &level, sizeof level) == -1) {
		log_error("sysdep_cleartext: "
		    "setsockopt (%d, %d, IP_AUTH_LEVEL, ...) failed", fd,
		    optsw[sw].ip_proto);
		return -1;
	}
	if (monitor_setsockopt(fd, optsw[sw].ip_proto, optsw[sw].esp_trans_level,
	    (char *) &level, sizeof level) == -1) {
		log_error("sysdep_cleartext: "
		    "setsockopt (%d, %d, IP_ESP_TRANS_LEVEL, ...) failed", fd,
		    optsw[sw].ip_proto);
		return -1;
	}
	if (monitor_setsockopt(fd, optsw[sw].ip_proto, optsw[sw].esp_network_level,
	    (char *) &level, sizeof level) == -1) {
		log_error("sysdep_cleartext: "
		    "setsockopt (%d, %d, IP_ESP_NETWORK_LEVEL, ...) failed", fd,
		    optsw[sw].ip_proto);
		return -1;
	}
	if (optsw[sw].ipcomp_level &&
	    monitor_setsockopt(fd, optsw[sw].ip_proto, optsw[sw].ipcomp_level,
	    (char *) &level, sizeof level) == -1 &&
	    errno != ENOPROTOOPT) {
		log_error("sysdep_cleartext: "
		    "setsockopt (%d, %d, IP_IPCOMP_LEVEL, ...) failed,", fd,
		    optsw[sw].ip_proto);
		return -1;
	}
	return 0;
}
