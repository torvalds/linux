/*	$OpenBSD: uuid.c,v 1.6 2018/04/26 12:42:51 guenther Exp $ */
/*
 * Copyright (c) 2002, Stockholms Universitet
 * (Stockholm University, Stockholm Sweden)
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * 3. Neither the name of the university nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * NCS/DCE/AFS/GUID generator
 *
 *  for more information about DCE UUID, see
 *  <http://www.opengroup.org/onlinepubs/9629399/apdxa.htm>
 *
 *  Note, the Microsoft GUID is a DCE UUID, but it seems like they
 *  folded in the seq num with the node part. That would explain how
 *  the reserved field have a bit pattern 110 when reserved is a 2 bit
 *  field.
 *
 *  XXX should hash the node address for privacy issues
 */

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <netinet/in.h>
#include <net/if.h>
#include <net/if_types.h>
#include <net/if_dl.h>

#include <fcntl.h>
#include <ifaddrs.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "uuid.h"

static uint32_t seq_num;
static struct timeval last_time;
static int32_t counter;
static char nodeaddr[6];

enum { UUID_NODE_MULTICAST = 0x80 };

static int
time_cmp(struct timeval *tv1, struct timeval *tv2)
{
    if (tv1->tv_sec > tv2->tv_sec)
	return -1;
    if (tv1->tv_sec < tv2->tv_sec)
	return 1;
    if (tv1->tv_usec > tv2->tv_usec)
	return -1;
    if (tv1->tv_usec < tv2->tv_usec)
	return 1;
    return 0;
}

static void
get_node_addr(char *addr)
{
    struct ifaddrs *ifa, *ifa0;
    int found_mac = 0;

    if (getifaddrs(&ifa0) != 0)
	ifa0 = NULL;

    for (ifa = ifa0; ifa != NULL && !found_mac; ifa = ifa->ifa_next) {
	if (ifa->ifa_addr == NULL)
	    continue;

#if IFF_LOOPBACK
	if (ifa->ifa_flags & IFF_LOOPBACK)
	    continue;
#endif

	switch (ifa->ifa_addr->sa_family) {
#ifdef AF_LINK
	case AF_LINK: {
	    struct sockaddr_dl *dl = (struct sockaddr_dl *)ifa->ifa_addr;

	    switch (dl->sdl_type) {
	    case IFT_ETHER:
	    case IFT_FDDI:
		if (dl->sdl_alen == 6) {
		    memcpy(addr, LLADDR(dl), 6);
		    found_mac = 1;
		}
	    }

	}
#endif
	default:
	    break;
	}
    }

    if (ifa0 != NULL)
	freeifaddrs(ifa0);

    if (!found_mac) {
	/*
	 * Set the multicast bit to make sure we won't collide with an
	 * allocated (mac) address.
	 */
	arc4random_buf(addr, 6);
	addr[0] |= UUID_NODE_MULTICAST;
    }
    return;
}

/*
 *    Creates a new UUID.
 */

void
uuid_create(afsUUID *uuid)
{
    static int uuid_inited = 0;
    struct timeval tv;
    int ret, got_time;
    uint64_t dce_time;

    if (uuid_inited == 0) {
	gettimeofday(&last_time, NULL);
	seq_num = arc4random();
	get_node_addr(nodeaddr);
	uuid_inited = 1;
    }

    gettimeofday(&tv, NULL);

    got_time = 0;

    do {
	ret = time_cmp(&tv, &last_time);
	if (ret < 0) {
	    /* Time went backward, just inc seq_num and be done.
	     * seq_num is 6 + 8 bit field it the uuid, so let it wrap
	     * around. don't let it be zero.
	     */
	    seq_num = (seq_num + 1) & 0x3fff ;
	    if (seq_num == 0)
		seq_num++;
	    got_time = 1;
	    counter = 0;
	    last_time = tv;
	} else if (ret > 0) {
	    /* time went forward, reset counter and be happy */
	    last_time = tv;
	    counter = 0;
	    got_time = 1;
	} else {
#define UUID_MAX_HZ (1) /* make this bigger fix you have larger tickrate */
#define MULTIPLIER_100_NANO_SEC 10
	    if (++counter < UUID_MAX_HZ * MULTIPLIER_100_NANO_SEC)
		got_time = 1;
	}
    } while(!got_time);

    /*
     * now shift time to dce_time, epoch 00:00:00:00, 15 October 1582
     * dce time ends year ~3400, so start to worry now
     */

    dce_time = tv.tv_usec * MULTIPLIER_100_NANO_SEC + counter;
    dce_time += ((uint64_t)tv.tv_sec) * 10000000;
    dce_time += (((uint64_t)0x01b21dd2) << 32) + 0x13814000;

    uuid->time_low = dce_time & 0xffffffff;
    uuid->time_mid = 0xffff & (dce_time >> 32);
    uuid->time_hi_and_version = 0x0fff & (dce_time >> 48);

    uuid->time_hi_and_version |= (1 << 12);

    uuid->clock_seq_low = seq_num & 0xff;
    uuid->clock_seq_hi_and_reserved = (seq_num >> 8) & 0x3f;
    uuid->clock_seq_hi_and_reserved |= 0x80; /* dce variant */

    memcpy(uuid->node, nodeaddr, 6);
}

/*
 *    Converts a UUID from binary representation to a string representation.
 */

void
uuid_to_string(const afsUUID *uuid, char *str, size_t strsz)
{
    snprintf(str, strsz,
	     "%08lx-%04x-%04x-%02x%02x-%02x%02x%02x%02x%02x%02x",
	     uuid->time_low,
	     uuid->time_mid,
	     uuid->time_hi_and_version,
	     (unsigned char)uuid->clock_seq_hi_and_reserved,
	     (unsigned char)uuid->clock_seq_low,
	     (unsigned char)uuid->node[0],
	     (unsigned char)uuid->node[1],
	     (unsigned char)uuid->node[2],
	     (unsigned char)uuid->node[3],
	     (unsigned char)uuid->node[4],
	     (unsigned char)uuid->node[5]);
}


#ifdef TEST
int
main(int argc, char **argv)
{
    char str[1000];
    afsUUID u1, u2;

    uuid_create(&u1);

    uuid_to_string(&u1, str, sizeof(str));

    printf("u: %s\n", str);

    if (uuid_from_string(str, &u2)) {
	printf("failed to parse\n");
	return 0;
    }

    if (bcmp(&u1, &u2, sizeof(u1)) != 0)
	printf("u1 != u2\n");

    return 0;
}
#endif
