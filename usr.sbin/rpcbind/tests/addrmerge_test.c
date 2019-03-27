/*-
 * Copyright (c) 2014 Spectra Logic Corporation
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions, and the following disclaimer,
 *    without modification.
 * 2. Redistributions in binary form must reproduce at minimum a disclaimer
 *    substantially similar to the "NO WARRANTY" disclaimer below
 *    ("Disclaimer") and any redistribution must be conditioned upon
 *    including a substantially similar Disclaimer requirement for further
 *    binary redistribution.
 *
 * NO WARRANTY
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTIBILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * HOLDERS OR CONTRIBUTORS BE LIABLE FOR SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING
 * IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGES.
 *
 * $FreeBSD$
 */

#include <rpc/rpc.h>
#include <sys/types.h>
#include <sys/socket.h>

#include <net/if.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include <ifaddrs.h>
#include <stdlib.h>

#include <atf-c.h>

#include "rpcbind.h"

#define MAX_IFADDRS 16

int debugging = false;

/* Data for mocking getifaddrs */
struct ifaddr_storage {
	struct ifaddrs ifaddr;
	struct sockaddr_storage addr;
	struct sockaddr_storage mask;
	struct sockaddr_storage bcast;
} mock_ifaddr_storage[MAX_IFADDRS];
struct ifaddrs *mock_ifaddrs = NULL;
int ifaddr_count = 0; 

/* Data for mocking listen_addr */
int bind_address_count = 0;
struct sockaddr* bind_addresses[MAX_IFADDRS];

/* Stub library functions */
void
freeifaddrs(struct ifaddrs *ifp __unused)
{
	return ;
}

int
getifaddrs(struct ifaddrs **ifap)
{
	*ifap = mock_ifaddrs;
	return (0);
}

static void
mock_ifaddr4(const char* name, const char* addr, const char* mask,
    const char* bcast, unsigned int flags, bool bind)
{
	struct ifaddrs *ifaddr = &mock_ifaddr_storage[ifaddr_count].ifaddr;
	struct sockaddr_in *in = (struct sockaddr_in*)
	    			&mock_ifaddr_storage[ifaddr_count].addr;
	struct sockaddr_in *mask_in = (struct sockaddr_in*)
	    			&mock_ifaddr_storage[ifaddr_count].mask;
	struct sockaddr_in *bcast_in = (struct sockaddr_in*)
	    			&mock_ifaddr_storage[ifaddr_count].bcast;

	in->sin_family = AF_INET;
	in->sin_port = 0;
	in->sin_len = sizeof(*in);
	in->sin_addr.s_addr = inet_addr(addr);
	mask_in->sin_family = AF_INET;
	mask_in->sin_port = 0;
	mask_in->sin_len = sizeof(*mask_in);
	mask_in->sin_addr.s_addr = inet_addr(mask);
	bcast_in->sin_family = AF_INET;
	bcast_in->sin_port = 0;
	bcast_in->sin_len = sizeof(*bcast_in);
	bcast_in->sin_addr.s_addr = inet_addr(bcast);
	*ifaddr = (struct ifaddrs) {
		.ifa_next = NULL,
		.ifa_name = (char*) name,
		.ifa_flags = flags,
		.ifa_addr = (struct sockaddr*) in,
		.ifa_netmask = (struct sockaddr*) mask_in,
		.ifa_broadaddr = (struct sockaddr*) bcast_in,
		.ifa_data = NULL,	/* addrmerge doesn't care*/
	};

	if (ifaddr_count > 0)
		mock_ifaddr_storage[ifaddr_count - 1].ifaddr.ifa_next = ifaddr;
	ifaddr_count++;
	mock_ifaddrs = &mock_ifaddr_storage[0].ifaddr;

	/* Optionally simulate binding an ip ala "rpcbind -h foo" */
	if (bind) {
		bind_addresses[bind_address_count] = (struct sockaddr*)in;
		bind_address_count++;
	}
}

#ifdef INET6
static void
mock_ifaddr6(const char* name, const char* addr, const char* mask,
    const char* bcast, unsigned int flags, uint32_t scope_id, bool bind)
{
	struct ifaddrs *ifaddr = &mock_ifaddr_storage[ifaddr_count].ifaddr;
	struct sockaddr_in6 *in6 = (struct sockaddr_in6*)
	    			&mock_ifaddr_storage[ifaddr_count].addr;
	struct sockaddr_in6 *mask_in6 = (struct sockaddr_in6*)
	    			&mock_ifaddr_storage[ifaddr_count].mask;
	struct sockaddr_in6 *bcast_in6 = (struct sockaddr_in6*)
	    			&mock_ifaddr_storage[ifaddr_count].bcast;

	in6->sin6_family = AF_INET6;
	in6->sin6_port = 0;
	in6->sin6_len = sizeof(*in6);
	in6->sin6_scope_id = scope_id;
	ATF_REQUIRE_EQ(1, inet_pton(AF_INET6, addr, (void*)&in6->sin6_addr));
	mask_in6->sin6_family = AF_INET6;
	mask_in6->sin6_port = 0;
	mask_in6->sin6_len = sizeof(*mask_in6);
	mask_in6->sin6_scope_id = scope_id;
	ATF_REQUIRE_EQ(1, inet_pton(AF_INET6, mask,
	    (void*)&mask_in6->sin6_addr));
	bcast_in6->sin6_family = AF_INET6;
	bcast_in6->sin6_port = 0;
	bcast_in6->sin6_len = sizeof(*bcast_in6);
	bcast_in6->sin6_scope_id = scope_id;
	ATF_REQUIRE_EQ(1, inet_pton(AF_INET6, bcast,
	    (void*)&bcast_in6->sin6_addr));
	*ifaddr = (struct ifaddrs) {
		.ifa_next = NULL,
		.ifa_name = (char*) name,
		.ifa_flags = flags,
		.ifa_addr = (struct sockaddr*) in6,
		.ifa_netmask = (struct sockaddr*) mask_in6,
		.ifa_broadaddr = (struct sockaddr*) bcast_in6,
		.ifa_data = NULL,	/* addrmerge doesn't care*/
	};

	if (ifaddr_count > 0)
		mock_ifaddr_storage[ifaddr_count - 1].ifaddr.ifa_next = ifaddr;
	ifaddr_count++;
	mock_ifaddrs = &mock_ifaddr_storage[0].ifaddr;

	/* Optionally simulate binding an ip ala "rpcbind -h foo" */
	if (bind) {
		bind_addresses[bind_address_count] = (struct sockaddr*)in6;
		bind_address_count++;
	}
}
#else
static void
mock_ifaddr6(const char* name __unused, const char* addr __unused,
    const char* mask __unused, const char* bcast __unused,
    unsigned int flags __unused, uint32_t scope_id __unused, bool bind __unused)
{
}
#endif /*INET6 */

static void
mock_lo0(void)
{
	/* 
	 * This broadcast address looks wrong, but it's what getifaddrs(2)
	 * actually returns.  It's invalid because IFF_BROADCAST is not set
	 */
	mock_ifaddr4("lo0", "127.0.0.1", "255.0.0.0", "127.0.0.1",
	    IFF_LOOPBACK | IFF_UP | IFF_RUNNING | IFF_MULTICAST, false);
	mock_ifaddr6("lo0", "::1", "ffff:ffff:ffff:ffff:ffff:ffff:ffff:ffff",
	    "::1",
	    IFF_LOOPBACK | IFF_UP | IFF_RUNNING | IFF_MULTICAST, 0, false);
}

static void
mock_igb0(void)
{
	mock_ifaddr4("igb0", "192.0.2.2", "255.255.255.128", "192.0.2.127",
	    IFF_UP | IFF_BROADCAST | IFF_RUNNING | IFF_SIMPLEX | IFF_MULTICAST,
	    false);
	mock_ifaddr6("igb0", "2001:db8::2", "ffff:ffff:ffff:ffff::",
	    "2001:db8::ffff:ffff:ffff:ffff",
	    IFF_UP | IFF_BROADCAST | IFF_RUNNING | IFF_SIMPLEX | IFF_MULTICAST,
	    0, false);
	/* Link local address */
	mock_ifaddr6("igb0", "fe80::2", "ffff:ffff:ffff:ffff::",
	    "fe80::ffff:ffff:ffff:ffff",
	    IFF_UP | IFF_BROADCAST | IFF_RUNNING | IFF_SIMPLEX | IFF_MULTICAST,
	    2, false);
}

/* On the same subnet as igb0 */
static void
mock_igb1(bool bind)
{
	mock_ifaddr4("igb1", "192.0.2.3", "255.255.255.128", "192.0.2.127",
	    IFF_UP | IFF_BROADCAST | IFF_RUNNING | IFF_SIMPLEX | IFF_MULTICAST,
	    bind);
	mock_ifaddr6("igb1", "2001:db8::3", "ffff:ffff:ffff:ffff::",
	    "2001:db8::ffff:ffff:ffff:ffff",
	    IFF_UP | IFF_BROADCAST | IFF_RUNNING | IFF_SIMPLEX | IFF_MULTICAST,
	    0, bind);
	/* Link local address */
	mock_ifaddr6("igb1", "fe80::3", "ffff:ffff:ffff:ffff::",
	    "fe80::ffff:ffff:ffff:ffff",
	    IFF_UP | IFF_BROADCAST | IFF_RUNNING | IFF_SIMPLEX | IFF_MULTICAST,
	    3, bind);
}

/* igb2 is on a different subnet than igb0 */
static void
mock_igb2(void)
{
	mock_ifaddr4("igb2", "192.0.2.130", "255.255.255.128", "192.0.2.255",
	    IFF_UP | IFF_BROADCAST | IFF_RUNNING | IFF_SIMPLEX | IFF_MULTICAST,
	    false);
	mock_ifaddr6("igb2", "2001:db8:1::2", "ffff:ffff:ffff:ffff::",
	    "2001:db8:1:0:ffff:ffff:ffff:ffff",
	    IFF_UP | IFF_BROADCAST | IFF_RUNNING | IFF_SIMPLEX | IFF_MULTICAST,
	    0, false);
}

/* tun0 is a P2P interface */
static void
mock_tun0(void)
{
	mock_ifaddr4("tun0", "192.0.2.5", "255.255.255.255", "192.0.2.6",
	    IFF_UP | IFF_RUNNING | IFF_POINTOPOINT | IFF_MULTICAST, false);
	mock_ifaddr6("tun0", "2001:db8::5",
	    "ffff:ffff:ffff:ffff:ffff:ffff:ffff:ffff",
	    "2001:db8::6",
	    IFF_UP | IFF_RUNNING | IFF_POINTOPOINT | IFF_MULTICAST, 0, false);
}


/* Stub rpcbind functions */
int
listen_addr(const struct sockaddr *sa)
{
	int i;
	
	if (bind_address_count == 0)
		return (1);
	
	for (i = 0; i < bind_address_count; i++) {
		if (bind_addresses[i]->sa_family != sa->sa_family)
			continue;

		if (0 == memcmp(bind_addresses[i]->sa_data, sa->sa_data,
		    sa->sa_len))
			return (1);
	}
	return (0);
}

struct netconfig*
rpcbind_get_conf(const char* netid __unused)
{
	/* Use static variables so we can return pointers to them */
	static char* lookups = NULL;
	static struct netconfig nconf_udp;
#ifdef INET6
	static struct netconfig nconf_udp6;
#endif /* INET6 */

	nconf_udp.nc_netid = "udp"; //netid_storage;
	nconf_udp.nc_semantics = NC_TPI_CLTS;
	nconf_udp.nc_flag = NC_VISIBLE;
	nconf_udp.nc_protofmly = (char*)"inet";
	nconf_udp.nc_proto = (char*)"udp";
	nconf_udp.nc_device = (char*)"-";
	nconf_udp.nc_nlookups = 0;
	nconf_udp.nc_lookups = &lookups;

#ifdef INET6
	nconf_udp6.nc_netid = "udp6"; //netid_storage;
	nconf_udp6.nc_semantics = NC_TPI_CLTS;
	nconf_udp6.nc_flag = NC_VISIBLE;
	nconf_udp6.nc_protofmly = (char*)"inet6";
	nconf_udp6.nc_proto = (char*)"udp6";
	nconf_udp6.nc_device = (char*)"-";
	nconf_udp6.nc_nlookups = 0;
	nconf_udp6.nc_lookups = &lookups;
#endif /* INET6 */

	if (0 == strncmp("udp", netid, sizeof("udp")))
		return (&nconf_udp);
#ifdef INET6
	else if (0 == strncmp("udp6", netid, sizeof("udp6")))
		return (&nconf_udp6);
#endif /* INET6 */
	else
		return (NULL);
}

/*
 * Helper function used by most test cases
 * param recvdstaddr	If non-null, the uaddr on which the request was received
 */
static char*
do_addrmerge4(const char* recvdstaddr)
{
	struct netbuf caller;
	struct sockaddr_in caller_in;
	const char *serv_uaddr, *clnt_uaddr, *netid;
	
	/* caller contains the client's IP address */
	caller.maxlen = sizeof(struct sockaddr_storage);
	caller.len = sizeof(caller_in);
	caller_in.sin_family = AF_INET;
	caller_in.sin_len = sizeof(caller_in);
	caller_in.sin_port = 1234;
	caller_in.sin_addr.s_addr = inet_addr("192.0.2.1");
	caller.buf = (void*)&caller_in;
	if (recvdstaddr != NULL)
		clnt_uaddr = recvdstaddr;
	else
		clnt_uaddr = "192.0.2.1.3.46";

	/* assume server is bound in INADDR_ANY port 814 */
	serv_uaddr = "0.0.0.0.3.46";

	netid = "udp";
	return (addrmerge(&caller, serv_uaddr, clnt_uaddr, netid));
}

#ifdef INET6
/*
 * Variant of do_addrmerge4 where the caller has an IPv6 address
 * param recvdstaddr	If non-null, the uaddr on which the request was received
 */
static char*
do_addrmerge6(const char* recvdstaddr)
{
	struct netbuf caller;
	struct sockaddr_in6 caller_in6;
	const char *serv_uaddr, *clnt_uaddr, *netid;
	
	/* caller contains the client's IP address */
	caller.maxlen = sizeof(struct sockaddr_storage);
	caller.len = sizeof(caller_in6);
	caller_in6.sin6_family = AF_INET6;
	caller_in6.sin6_len = sizeof(caller_in6);
	caller_in6.sin6_port = 1234;
	ATF_REQUIRE_EQ(1, inet_pton(AF_INET6, "2001:db8::1",
	    (void*)&caller_in6.sin6_addr));
	caller.buf = (void*)&caller_in6;
	if (recvdstaddr != NULL)
		clnt_uaddr = recvdstaddr;
	else
		clnt_uaddr = "2001:db8::1.3.46";

	/* assume server is bound in INADDR_ANY port 814 */
	serv_uaddr = "::1.3.46";

	netid = "udp6";
	return (addrmerge(&caller, serv_uaddr, clnt_uaddr, netid));
}

/* Variant of do_addrmerge6 where the caller uses a link local address */
static char*
do_addrmerge6_ll(void)
{
	struct netbuf caller;
	struct sockaddr_in6 caller_in6;
	const char *serv_uaddr, *clnt_uaddr, *netid;
	
	/* caller contains the client's IP address */
	caller.maxlen = sizeof(struct sockaddr_storage);
	caller.len = sizeof(caller_in6);
	caller_in6.sin6_family = AF_INET6;
	caller_in6.sin6_len = sizeof(caller_in6);
	caller_in6.sin6_port = 1234;
	caller_in6.sin6_scope_id = 2; /* same as igb0 */
	ATF_REQUIRE_EQ(1, inet_pton(AF_INET6, "fe80::beef",
	    (void*)&caller_in6.sin6_addr));
	caller.buf = (void*)&caller_in6;
	clnt_uaddr = "fe80::beef.3.46";

	/* assume server is bound in INADDR_ANY port 814 */
	serv_uaddr = "::1.3.46";

	netid = "udp6";
	return (addrmerge(&caller, serv_uaddr, clnt_uaddr, netid));
}
#endif /* INET6 */

ATF_TC_WITHOUT_HEAD(addrmerge_noifaddrs);
ATF_TC_BODY(addrmerge_noifaddrs, tc)
{
	char* maddr;

	maddr = do_addrmerge4(NULL);

	/* Since getifaddrs returns null, addrmerge must too */
	ATF_CHECK_EQ(NULL, maddr);
}

ATF_TC_WITHOUT_HEAD(addrmerge_localhost_only);
ATF_TC_BODY(addrmerge_localhost_only, tc)
{
	char *maddr;
	
	/* getifaddrs will return localhost only */
	mock_lo0();

	maddr = do_addrmerge4(NULL);

	/* We must return localhost if there is nothing better */
	ATF_REQUIRE(maddr != NULL);
	ATF_CHECK_STREQ("127.0.0.1.3.46", maddr);
	free(maddr);
}

ATF_TC_WITHOUT_HEAD(addrmerge_singlehomed);
ATF_TC_BODY(addrmerge_singlehomed, tc)
{
	char *maddr;
	
	/* getifaddrs will return one public address */
	mock_lo0();
	mock_igb0();

	maddr = do_addrmerge4(NULL);

	ATF_REQUIRE(maddr != NULL);
	ATF_CHECK_STREQ("192.0.2.2.3.46", maddr);
	free(maddr);
}

ATF_TC_WITHOUT_HEAD(addrmerge_one_addr_on_each_subnet);
ATF_TC_BODY(addrmerge_one_addr_on_each_subnet, tc)
{
	char *maddr;
	
	mock_lo0();
	mock_igb0();
	mock_igb2();

	maddr = do_addrmerge4(NULL);

	/* We must return the address on the caller's subnet */
	ATF_REQUIRE(maddr != NULL);
	ATF_CHECK_STREQ("192.0.2.2.3.46", maddr);
	free(maddr);
}


/*
 * Like addrmerge_one_addr_on_each_subnet, but getifaddrs returns a different
 * order
 */
ATF_TC_WITHOUT_HEAD(addrmerge_one_addr_on_each_subnet_rev);
ATF_TC_BODY(addrmerge_one_addr_on_each_subnet_rev, tc)
{
	char *maddr;
	
	/* getifaddrs will return one public address on each of two subnets */
	mock_igb2();
	mock_igb0();
	mock_lo0();

	maddr = do_addrmerge4(NULL);

	/* We must return the address on the caller's subnet */
	ATF_REQUIRE(maddr != NULL);
	ATF_CHECK_STREQ("192.0.2.2.3.46", maddr);
	free(maddr);
}

ATF_TC_WITHOUT_HEAD(addrmerge_point2point);
ATF_TC_BODY(addrmerge_point2point, tc)
{
	char *maddr;
	
	/* getifaddrs will return one normal and one p2p address */
	mock_lo0();
	mock_igb2();
	mock_tun0();

	maddr = do_addrmerge4(NULL);

	/* addrmerge should disprefer P2P interfaces */
	ATF_REQUIRE(maddr != NULL);
	ATF_CHECK_STREQ("192.0.2.130.3.46", maddr);
	free(maddr);
}

/* Like addrerge_point2point, but getifaddrs returns a different order */
ATF_TC_WITHOUT_HEAD(addrmerge_point2point_rev);
ATF_TC_BODY(addrmerge_point2point_rev, tc)
{
	char *maddr;
	
	/* getifaddrs will return one normal and one p2p address */
	mock_tun0();
	mock_igb2();
	mock_lo0();

	maddr = do_addrmerge4(NULL);

	/* addrmerge should disprefer P2P interfaces */
	ATF_REQUIRE(maddr != NULL);
	ATF_CHECK_STREQ("192.0.2.130.3.46", maddr);
	free(maddr);
}

/*
 * Simulate using rpcbind -h to select just one ip when the subnet has
 * multiple
 */
ATF_TC_WITHOUT_HEAD(addrmerge_bindip);
ATF_TC_BODY(addrmerge_bindip, tc)
{
	char *maddr;
	
	/* getifaddrs will return one public address on each of two subnets */
	mock_lo0();
	mock_igb0();
	mock_igb1(true);

	maddr = do_addrmerge4(NULL);

	/* We must return the address to which we are bound */
	ATF_REQUIRE(maddr != NULL);
	ATF_CHECK_STREQ("192.0.2.3.3.46", maddr);
	free(maddr);
}

/* Like addrmerge_bindip, but getifaddrs returns a different order */
ATF_TC_WITHOUT_HEAD(addrmerge_bindip_rev);
ATF_TC_BODY(addrmerge_bindip_rev, tc)
{
	char *maddr;
	
	/* getifaddrs will return one public address on each of two subnets */
	mock_igb1(true);
	mock_igb0();
	mock_lo0();

	maddr = do_addrmerge4(NULL);

	/* We must return the address to which we are bound */
	ATF_REQUIRE(maddr != NULL);
	ATF_CHECK_STREQ("192.0.2.3.3.46", maddr);
	free(maddr);
}

/* 
 * The address on which the request was received is known, and is provided as
 * the hint.
 */
ATF_TC_WITHOUT_HEAD(addrmerge_recvdstaddr);
ATF_TC_BODY(addrmerge_recvdstaddr, tc)
{
	char *maddr;
	
	mock_lo0();
	mock_igb0();
	mock_igb1(false);

	maddr = do_addrmerge4("192.0.2.2.3.46");

	/* We must return the address on which the request was received */
	ATF_REQUIRE(maddr != NULL);
	ATF_CHECK_STREQ("192.0.2.2.3.46", maddr);
	free(maddr);
}

ATF_TC_WITHOUT_HEAD(addrmerge_recvdstaddr_rev);
ATF_TC_BODY(addrmerge_recvdstaddr_rev, tc)
{
	char *maddr;
	
	mock_igb1(false);
	mock_igb0();
	mock_lo0();

	maddr = do_addrmerge4("192.0.2.2.3.46");

	/* We must return the address on which the request was received */
	ATF_REQUIRE(maddr != NULL);
	ATF_CHECK_STREQ("192.0.2.2.3.46", maddr);
	free(maddr);
}

#ifdef INET6
ATF_TC_WITHOUT_HEAD(addrmerge_localhost_only6);
ATF_TC_BODY(addrmerge_localhost_only6, tc)
{
	char *maddr;
	
	/* getifaddrs will return localhost only */
	mock_lo0();

	maddr = do_addrmerge6(NULL);

	/* We must return localhost if there is nothing better */
	ATF_REQUIRE(maddr != NULL);
	ATF_CHECK_STREQ("::1.3.46", maddr);
	free(maddr);
}

ATF_TC_WITHOUT_HEAD(addrmerge_singlehomed6);
ATF_TC_BODY(addrmerge_singlehomed6, tc)
{
	char *maddr;
	
	/* getifaddrs will return one public address */
	mock_lo0();
	mock_igb0();

	maddr = do_addrmerge6(NULL);

	ATF_REQUIRE(maddr != NULL);
	ATF_CHECK_STREQ("2001:db8::2.3.46", maddr);
	free(maddr);
}

ATF_TC_WITHOUT_HEAD(addrmerge_one_addr_on_each_subnet6);
ATF_TC_BODY(addrmerge_one_addr_on_each_subnet6, tc)
{
	char *maddr;
	
	mock_lo0();
	mock_igb0();
	mock_igb2();

	maddr = do_addrmerge6(NULL);

	/* We must return the address on the caller's subnet */
	ATF_REQUIRE(maddr != NULL);
	ATF_CHECK_STREQ("2001:db8::2.3.46", maddr);
	free(maddr);
}


/*
 * Like addrmerge_one_addr_on_each_subnet6, but getifaddrs returns a different
 * order
 */
ATF_TC_WITHOUT_HEAD(addrmerge_one_addr_on_each_subnet6_rev);
ATF_TC_BODY(addrmerge_one_addr_on_each_subnet6_rev, tc)
{
	char *maddr;
	
	/* getifaddrs will return one public address on each of two subnets */
	mock_igb2();
	mock_igb0();
	mock_lo0();

	maddr = do_addrmerge6(NULL);

	/* We must return the address on the caller's subnet */
	ATF_REQUIRE(maddr != NULL);
	ATF_CHECK_STREQ("2001:db8::2.3.46", maddr);
	free(maddr);
}

ATF_TC_WITHOUT_HEAD(addrmerge_point2point6);
ATF_TC_BODY(addrmerge_point2point6, tc)
{
	char *maddr;
	
	/* getifaddrs will return one normal and one p2p address */
	mock_lo0();
	mock_igb2();
	mock_tun0();

	maddr = do_addrmerge6(NULL);

	/* addrmerge should disprefer P2P interfaces */
	ATF_REQUIRE(maddr != NULL);
	ATF_CHECK_STREQ("2001:db8:1::2.3.46", maddr);
	free(maddr);
}

/* Like addrerge_point2point, but getifaddrs returns a different order */
ATF_TC_WITHOUT_HEAD(addrmerge_point2point6_rev);
ATF_TC_BODY(addrmerge_point2point6_rev, tc)
{
	char *maddr;
	
	/* getifaddrs will return one normal and one p2p address */
	mock_tun0();
	mock_igb2();
	mock_lo0();

	maddr = do_addrmerge6(NULL);

	/* addrmerge should disprefer P2P interfaces */
	ATF_REQUIRE(maddr != NULL);
	ATF_CHECK_STREQ("2001:db8:1::2.3.46", maddr);
	free(maddr);
}

ATF_TC_WITHOUT_HEAD(addrmerge_bindip6);
ATF_TC_BODY(addrmerge_bindip6, tc)
{
	char *maddr;
	
	/* getifaddrs will return one public address on each of two subnets */
	mock_lo0();
	mock_igb0();
	mock_igb1(true);

	maddr = do_addrmerge6(NULL);

	/* We must return the address to which we are bound */
	ATF_REQUIRE(maddr != NULL);
	ATF_CHECK_STREQ("2001:db8::3.3.46", maddr);
	free(maddr);
}

/* Like addrerge_bindip, but getifaddrs returns a different order */
ATF_TC_WITHOUT_HEAD(addrmerge_bindip6_rev);
ATF_TC_BODY(addrmerge_bindip6_rev, tc)
{
	char *maddr;
	
	/* getifaddrs will return one public address on each of two subnets */
	mock_igb1(true);
	mock_igb0();
	mock_lo0();

	maddr = do_addrmerge6(NULL);

	/* We must return the address to which we are bound */
	ATF_REQUIRE(maddr != NULL);
	ATF_CHECK_STREQ("2001:db8::3.3.46", maddr);
	free(maddr);
}

/* 
 * IPv6 Link Local addresses with the same scope id as the caller, if the caller
 * is also a link local address, should be preferred
 */
ATF_TC_WITHOUT_HEAD(addrmerge_ipv6_linklocal);
ATF_TC_BODY(addrmerge_ipv6_linklocal, tc)
{
	char *maddr;
	
	/* 
	 * getifaddrs will return two link local addresses with the same netmask
	 * and prefix but different scope IDs
	 */
	mock_igb1(false);
	mock_igb0();
	mock_lo0();

	maddr = do_addrmerge6_ll();

	/* We must return the address to which we are bound */
	ATF_REQUIRE(maddr != NULL);
	ATF_CHECK_STREQ("fe80::2.3.46", maddr);
	free(maddr);
}

ATF_TC_WITHOUT_HEAD(addrmerge_ipv6_linklocal_rev);
ATF_TC_BODY(addrmerge_ipv6_linklocal_rev, tc)
{
	char *maddr;
	
	/* 
	 * getifaddrs will return two link local addresses with the same netmask
	 * and prefix but different scope IDs
	 */
	mock_lo0();
	mock_igb0();
	mock_igb1(false);

	maddr = do_addrmerge6_ll();

	/* We must return the address to which we are bound */
	ATF_REQUIRE(maddr != NULL);
	ATF_CHECK_STREQ("fe80::2.3.46", maddr);
	free(maddr);
}

ATF_TC_WITHOUT_HEAD(addrmerge_recvdstaddr6);
ATF_TC_BODY(addrmerge_recvdstaddr6, tc)
{
	char *maddr;
	
	mock_lo0();
	mock_igb0();
	mock_igb1(false);

	maddr = do_addrmerge6("2001:db8::2.3.46");

	/* We must return the address on which the request was received */
	ATF_REQUIRE(maddr != NULL);
	ATF_CHECK_STREQ("2001:db8::2.3.46", maddr);
	free(maddr);
}

ATF_TC_WITHOUT_HEAD(addrmerge_recvdstaddr6_rev);
ATF_TC_BODY(addrmerge_recvdstaddr6_rev, tc)
{
	char *maddr;
	
	mock_igb1(false);
	mock_igb0();
	mock_lo0();

	maddr = do_addrmerge6("2001:db8::2.3.46");

	/* We must return the address on which the request was received */
	ATF_REQUIRE(maddr != NULL);
	ATF_CHECK_STREQ("2001:db8::2.3.46", maddr);
	free(maddr);
}
#endif /* INET6 */


ATF_TP_ADD_TCS(tp)
{
	ATF_TP_ADD_TC(tp, addrmerge_noifaddrs);
	ATF_TP_ADD_TC(tp, addrmerge_localhost_only);
	ATF_TP_ADD_TC(tp, addrmerge_singlehomed);
	ATF_TP_ADD_TC(tp, addrmerge_one_addr_on_each_subnet);
	ATF_TP_ADD_TC(tp, addrmerge_one_addr_on_each_subnet_rev);
	ATF_TP_ADD_TC(tp, addrmerge_point2point);
	ATF_TP_ADD_TC(tp, addrmerge_point2point_rev);
	ATF_TP_ADD_TC(tp, addrmerge_bindip);
	ATF_TP_ADD_TC(tp, addrmerge_bindip_rev);
	ATF_TP_ADD_TC(tp, addrmerge_recvdstaddr);
	ATF_TP_ADD_TC(tp, addrmerge_recvdstaddr_rev);
#ifdef INET6
	ATF_TP_ADD_TC(tp, addrmerge_localhost_only6);
	ATF_TP_ADD_TC(tp, addrmerge_singlehomed6);
	ATF_TP_ADD_TC(tp, addrmerge_one_addr_on_each_subnet6);
	ATF_TP_ADD_TC(tp, addrmerge_one_addr_on_each_subnet6_rev);
	ATF_TP_ADD_TC(tp, addrmerge_point2point6);
	ATF_TP_ADD_TC(tp, addrmerge_point2point6_rev);
	ATF_TP_ADD_TC(tp, addrmerge_bindip6);
	ATF_TP_ADD_TC(tp, addrmerge_bindip6_rev);
	ATF_TP_ADD_TC(tp, addrmerge_ipv6_linklocal);
	ATF_TP_ADD_TC(tp, addrmerge_ipv6_linklocal_rev);
	ATF_TP_ADD_TC(tp, addrmerge_recvdstaddr6);
	ATF_TP_ADD_TC(tp, addrmerge_recvdstaddr6_rev);
#endif

	return (atf_no_error());
}
