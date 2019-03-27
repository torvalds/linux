/*-
 * Copyright (c) 2004 Robert N. M. Watson
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

#include <sys/types.h>
#include <sys/socket.h>

#include <netinet/in.h>
#include <netinet/in_systm.h>
#include <netinet/ip.h>
#include <arpa/inet.h>

#include <err.h>
#include <errno.h>
#include <getopt.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static int dorandom = 0;
static int nmcastgroups = IP_MAX_MEMBERSHIPS;
static int verbose = 0;

/*
 * The test tool exercises IP-level socket options by interrogating the
 * getsockopt()/setsockopt() APIs.  It does not currently test that the
 * intended semantics of each option are implemented (i.e., that setting IP
 * options on the socket results in packets with the desired IP options in
 * it).
 */

/*
 * get_socket() is a wrapper function that returns a socket of the specified
 * type, and created with or without restored root privilege (if running
 * with a real uid of root and an effective uid of some other user).  This
 * us to test whether the same rights are granted using a socket with a
 * privileged cached credential vs. a socket with a regular credential.
 */
#define	PRIV_ASIS	0
#define	PRIV_GETROOT	1
static int
get_socket_unpriv(int type)
{

	return (socket(PF_INET, type, 0));
}

static int
get_socket_priv(int type)
{
	uid_t olduid;
	int sock;

	if (getuid() != 0)
		errx(-1, "get_sock_priv: running without real uid 0");
	
	olduid = geteuid();
	if (seteuid(0) < 0)
		err(-1, "get_sock_priv: seteuid(0)");

	sock = socket(PF_INET, type, 0);

	if (seteuid(olduid) < 0)
		err(-1, "get_sock_priv: seteuid(%d)", olduid);

	return (sock);
}

static int
get_socket(int type, int priv)
{

	if (priv)
		return (get_socket_priv(type));
	else
		return (get_socket_unpriv(type));
}

/*
 * Exercise the IP_OPTIONS socket option.  Confirm the following properties:
 *
 * - That there is no initial set of options (length returned is 0).
 * - That if we set a specific set of options, we can read it back.
 * - That if we then reset the options, they go away.
 *
 * Use a UDP socket for this.
 */
static void
test_ip_options(int sock, const char *socktypename)
{
	u_int32_t new_options, test_options[2];
	socklen_t len;

	/*
	 * Start off by confirming the default IP options on a socket are to
	 * have no options set.
	 */
	len = sizeof(test_options);
	if (getsockopt(sock, IPPROTO_IP, IP_OPTIONS, test_options, &len) < 0)
		err(-1, "test_ip_options(%s): initial getsockopt()",
		    socktypename);

	if (len != 0)
		errx(-1, "test_ip_options(%s): initial getsockopt() returned "
		    "%d bytes", socktypename, len);

#define	TEST_MAGIC	0xc34e4212
#define	NEW_OPTIONS	htonl(IPOPT_EOL | (IPOPT_NOP << 8) | (IPOPT_NOP << 16) \
			 | (IPOPT_NOP << 24))

	/*
	 * Write some new options into the socket.
	 */
	new_options = NEW_OPTIONS;
	if (setsockopt(sock, IPPROTO_IP, IP_OPTIONS, &new_options,
	    sizeof(new_options)) < 0)
		err(-1, "test_ip_options(%s): setsockopt(NOP|NOP|NOP|EOL)",
		    socktypename);

	/*
	 * Store some random cruft in a local variable and retrieve the
	 * options to make sure they set.  Note that we pass in an array
	 * of u_int32_t's so that if whatever ended up in the option was
	 * larger than what we put in, we find out about it here.
	 */
	test_options[0] = TEST_MAGIC;
	test_options[1] = TEST_MAGIC;
	len = sizeof(test_options);
	if (getsockopt(sock, IPPROTO_IP, IP_OPTIONS, test_options, &len) < 0)
		err(-1, "test_ip_options(%s): getsockopt() after set",
		    socktypename);

	/*
	 * Getting the right amount back is important.
	 */
	if (len != sizeof(new_options))
		errx(-1, "test_ip_options(%s): getsockopt() after set "
		    "returned %d bytes of data", socktypename, len);

	/*
	 * One posible failure mode is that the call succeeds but neglects to
	 * copy out the data.
 	 */
	if (test_options[0] == TEST_MAGIC)
		errx(-1, "test_ip_options(%s): getsockopt() after set didn't "
		    "return data", socktypename);

	/*
	 * Make sure we get back what we wrote on.
	 */
	if (new_options != test_options[0])
		errx(-1, "test_ip_options(%s): getsockopt() after set "
		    "returned wrong options (%08x, %08x)", socktypename,
		    new_options, test_options[0]);

	/*
	 * Now we reset the value to make sure clearing works.
	 */
	if (setsockopt(sock, IPPROTO_IP, IP_OPTIONS, NULL, 0) < 0)
		err(-1, "test_ip_options(%s): setsockopt() to reset",
		    socktypename);

	/*
	 * Make sure it was really cleared.
	 */
	test_options[0] = TEST_MAGIC;
	test_options[1] = TEST_MAGIC;
	len = sizeof(test_options);
	if (getsockopt(sock, IPPROTO_IP, IP_OPTIONS, test_options, &len) < 0)
		err(-1, "test_ip_options(%s): getsockopt() after reset",
		    socktypename);

	if (len != 0)
		errx(-1, "test_ip_options(%s): getsockopt() after reset "
		    "returned %d bytes", socktypename, len);
}

/*
 * This test checks the behavior of the IP_HDRINCL socket option, which
 * allows users with privilege to specify the full header on an IP raw
 * socket.  We test that the option can only be used with raw IP sockets, not
 * with UDP or TCP sockets.  We also confirm that the raw socket is only
 * available to a privileged user (subject to the UID when called).  We
 * confirm that it defaults to off
 *
 * Unlike other tests, doesn't use caller-provided socket.  Probably should
 * be fixed.
 */
static void
test_ip_hdrincl(void)
{
	int flag[2], sock;
	socklen_t len;

	/*
	 * Try to receive or set the IP_HDRINCL flag on a TCP socket.
	 */
	sock = socket(PF_INET, SOCK_STREAM, 0);
	if (sock == -1)
		err(-1, "test_ip_hdrincl(): socket(SOCK_STREAM)");

	flag[0] = -1;
	len = sizeof(flag[0]);
	if (getsockopt(sock, IPPROTO_IP, IP_HDRINCL, flag, &len) == 0)
		err(-1, "test_ip_hdrincl(): initial getsockopt(IP_HDRINCL)");

	if (errno != ENOPROTOOPT)
		errx(-1, "test_ip_hdrincl(): initial getsockopt(IP_HDRINC) "
		    "returned %d (%s) not ENOPROTOOPT", errno,
		    strerror(errno));

	flag[0] = 1;
	if (setsockopt(sock, IPPROTO_IP, IP_HDRINCL, flag, sizeof(flag[0]))
	    == 0)
		err(-1,"test_ip_hdrincl(): setsockopt(IP_HDRINCL) on TCP "
		    "succeeded\n");

	if (errno != ENOPROTOOPT)
		errx(-1, "test_ip_hdrincl(): setsockopt(IP_HDRINCL) on TCP "
		    "returned %d (%s) not ENOPROTOOPT\n", errno,
		    strerror(errno));

	close(sock);

	/*
	 * Try to receive or set the IP_HDRINCL flag on a UDP socket.
	 */
	sock = socket(PF_INET, SOCK_DGRAM, 0);
	if (sock == -1)
		err(-1, "test_ip_hdrincl(): socket(SOCK_DGRAM");

	flag[0] = -1;
	len = sizeof(flag[0]);
	if (getsockopt(sock, IPPROTO_IP, IP_HDRINCL, flag, &len) == 0)
		err(-1, "test_ip_hdrincl(): getsockopt(IP_HDRINCL) on UDP "
		    "succeeded\n");

	if (errno != ENOPROTOOPT)
		errx(-1, "test_ip_hdrincl(): getsockopt(IP_HDRINCL) on UDP "
		    "returned %d (%s) not ENOPROTOOPT\n", errno,
		    strerror(errno));

	if (setsockopt(sock, IPPROTO_IP, IP_HDRINCL, flag, sizeof(flag[0]))
	    == 0)
		err(-1, "test_ip_hdrincl(): setsockopt(IP_HDRINCL) on UDP "
		    "succeeded\n");

	if (errno != ENOPROTOOPT)
		errx(-1, "test_ip_hdrincl(): setsockopt(IP_HDRINCL) on UDP "
		    "returned %d (%s) not ENOPROTOOPT\n", errno,
		    strerror(errno));

	close(sock);

	/*
	 * Now try on a raw socket.  Access ontrol should prevent non-root
	 * users from creating the raw socket, so check that here based on
	 * geteuid().  If we're non-root, we just return assuming the socket
	 * create fails since the remainder of the tests apply only on a raw
	 * socket.
	 */
	sock = socket(PF_INET, SOCK_RAW, 0);
	if (geteuid() != 0) {
		if (sock != -1)
			errx(-1, "test_ip_hdrincl: created raw socket as "
			    "uid %d", geteuid());
		return;
	}
	if (sock == -1)
		err(-1, "test_ip_hdrincl(): socket(PF_INET, SOCK_RAW)");

	/*
	 * Make sure the initial value of the flag is 0 (disabled).
	 */
	flag[0] = -1;
	flag[1] = -1;
	len = sizeof(flag);
	if (getsockopt(sock, IPPROTO_IP, IP_HDRINCL, flag, &len) < 0)
		err(-1, "test_ip_hdrincl(): getsockopt(IP_HDRINCL) on raw "
		    "socket");

	if (len != sizeof(flag[0]))
		errx(-1, "test_ip_hdrincl(): %d bytes returned on "
		    "initial get\n", len);

	if (flag[0] != 0)
		errx(-1, "test_ip_hdrincl(): initial flag value of %d\n",
		    flag[0]);

	/*
	 * Enable the IP_HDRINCL flag.
	 */
	flag[0] = 1;
	if (setsockopt(sock, IPPROTO_IP, IP_HDRINCL, flag, sizeof(flag[0]))
	    < 0)
		err(-1, "test_ip_hdrincl(): setsockopt(IP_HDRINCL, 1)");

	/*
	 * Check that the IP_HDRINCL flag was set.
	 */
	flag[0] = -1;
	flag[1] = -1;
	len = sizeof(flag);
	if (getsockopt(sock, IPPROTO_IP, IP_HDRINCL, flag, &len) < 0)
		err(-1, "test_ip_hdrincl(): getsockopt(IP_HDRINCL) after "
		    "set");

	if (flag[0] == 0)
		errx(-1, "test_ip_hdrincl(): getsockopt(IP_HDRINCL) "
		    "after set had flag of %d\n", flag[0]);

#define	HISTORICAL_INP_HDRINCL	8
	if (flag[0] != HISTORICAL_INP_HDRINCL)
		warnx("test_ip_hdrincl(): WARNING: getsockopt(IP_H"
		    "DRINCL) after set had non-historical value of %d\n",
		    flag[0]);

	/*
	 * Reset the IP_HDRINCL flag to 0.
	 */
	flag[0] = 0;
	if (setsockopt(sock, IPPROTO_IP, IP_HDRINCL, flag, sizeof(flag[0]))
	    < 0)
		err(-1, "test_ip_hdrincl(): setsockopt(IP_HDRINCL, 0)");

	/*
	 * Check that the IP_HDRINCL flag was reset to 0.
	 */
	flag[0] = -1;
	flag[1] = -1;
	len = sizeof(flag);
	if (getsockopt(sock, IPPROTO_IP, IP_HDRINCL, flag, &len) < 0)
		err(-1, "test_ip_hdrincl(): getsockopt(IP_HDRINCL) after "
		    "reset");

	if (flag[0] != 0)
		errx(-1, "test_ip_hdrincl(): getsockopt(IP_HDRINCL) "
		    "after set had flag of %d\n", flag[0]);

	close(sock);
}

/*
 * As with other non-int or larger sized socket options, the IP_TOS and
 * IP_TTL fields in kernel is stored as an 8-bit value, reflecting the IP
 * header fields, but useful I/O to the field occurs using 32-bit integers.
 * The FreeBSD kernel will permit writes from variables at least an int in
 * size (and ignore additional bytes), and will permit a read to buffers 1
 * byte or larger (but depending on endianness, may truncate out useful
 * values if the caller provides less room).
 *
 * Given the limitations of the API, use a UDP socket to confirm that the
 * following are true:
 *
 * - We can read the IP_TOS/IP_TTL options.
 * - The initial value of the TOS option is 0, TTL is 64.
 * - That if we provide more than 32 bits of storage, we get back only 32
 *   bits of data.
 * - When we set it to a non-zero value expressible with a u_char, we can
 *   read that value back.
 * - When we reset it back to zero, we can read it as 0.
 * - When we set it to a value >255, the value is truncated to something less
 *   than 255.
 */
static void
test_ip_uchar(int sock, const char *socktypename, int option,
    const char *optionname, int initial)
{
	int val[2];
	socklen_t len;

	/*
	 * Check that the initial value is 0, and that the size is one
	 * u_char;
	 */
	val[0] = -1;
	val[1] = -1;
	len = sizeof(val);
	if (getsockopt(sock, IPPROTO_IP, option, val, &len) < 0)
		err(-1, "test_ip_uchar(%s, %s): initial getsockopt()",
		    socktypename, optionname);

	if (len != sizeof(val[0]))
		errx(-1, "test_ip_uchar(%s, %s): initial getsockopt() "
		    "returned %d bytes", socktypename, optionname, len);

	if (val[0] == -1)
		errx(-1, "test_ip_uchar(%s, %s): initial getsockopt() didn't "
		    "return data", socktypename, optionname);

	if (val[0] != initial)
		errx(-1, "test_ip_uchar(%s, %s): initial getsockopt() "
		    "returned value of %d, not %d", socktypename, optionname,
		    val[0], initial);

	/*
	 * Set the field to a valid value.
	 */
	val[0] = 128;
	val[1] = -1;
	if (setsockopt(sock, IPPROTO_IP, option, val, sizeof(val[0])) < 0)
		err(-1, "test_ip_uchar(%s, %s): setsockopt(128)",
		    socktypename, optionname);

	/*
	 * Check that when we read back the field, we get the same value.
	 */
	val[0] = -1;
	val[1] = -1;
	len = sizeof(val);
	if (getsockopt(sock, IPPROTO_IP, option, val, &len) < 0)
		err(-1, "test_ip_uchar(%s, %s): getsockopt() after set to "
		    "128", socktypename, optionname);

	if (len != sizeof(val[0]))
		errx(-1, "test_ip_uchar(%s, %s): getsockopt() after set to "
		    "128 returned %d bytes", socktypename, optionname, len);

	if (val[0] == -1)
		errx(-1, "test_ip_uchar(%s, %s): getsockopt() after set to "
		    "128 didn't return data", socktypename, optionname);

	if (val[0] != 128)
		errx(-1, "test_ip_uchar(%s, %s): getsockopt() after set to "
		    "128 returned %d", socktypename, optionname, val[0]);

	/*
	 * Reset the value to 0, check that it was reset.
	 */
	val[0] = 0;
	val[1] = 0;
	if (setsockopt(sock, IPPROTO_IP, option, val, sizeof(val[0])) < 0)
		err(-1, "test_ip_uchar(%s, %s): setsockopt() to reset from "
		    "128", socktypename, optionname);

	if (len != sizeof(val[0]))
		errx(-1, "test_ip_uchar(%s, %s): getsockopt() after reset "
		   "from 128 returned %d bytes", socktypename, optionname,
		    len);

	if (val[0] == -1)
		errx(-1, "test_ip_uchar(%s, %s): getsockopt() after reset "
		    "from 128 didn't return data", socktypename, optionname);

	if (val[0] != 0)
		errx(-1, "test_ip_uchar(%s, %s): getsockopt() after reset "
		    "from 128 returned %d", socktypename, optionname,
		    val[0]);

	/*
	 * Set the value to something out of range and check that it comes
	 * back truncated, or that we get EINVAL back.  Traditional u_char
	 * IP socket options truncate, but newer ones (such as multicast
	 * socket options) will return EINVAL.
	 */
	val[0] = 32000;
	val[1] = -1;
	if (setsockopt(sock, IPPROTO_IP, option, val, sizeof(val[0])) < 0) {
		/*
		 * EINVAL is a fine outcome, no need to run the truncation
		 * tests.
		 */
		if (errno == EINVAL)
			return;
		err(-1, "test_ip_uchar(%s, %s): getsockopt(32000)",
		    socktypename, optionname);
	}

	val[0] = -1;
	val[1] = -1;
	len = sizeof(val);
	if (getsockopt(sock, IPPROTO_IP, option, val, &len) < 0)
		err(-1, "test_ip_uchar(%s, %s): getsockopt() after set to "
		    "32000", socktypename, optionname);

	if (len != sizeof(val[0]))
		errx(-1, "test_ip_uchar(%s, %s): getsockopt() after set to "
		    "32000 returned %d bytes", socktypename, optionname,
		    len);

	if (val[0] == -1)
		errx(-1, "test_ip_uchar(%s, %s): getsockopt() after set to "
		    "32000 didn't return data", socktypename, optionname);

	if (val[0] == 32000)
		errx(-1, "test_ip_uchar(%s, %s): getsockopt() after set to "
		    "32000 returned 32000: failed to truncate", socktypename,
		    optionname);
}

/*
 * Generic test for a boolean socket option.  Caller provides the option
 * number, string name, expected default (initial) value, and whether or not
 * the option is root-only.  For each option, test:
 *
 * - That we can read the option.
 * - That the initial value is as expected.
 * - That we can modify the value.
 * - That on modification, the new value can be read back.
 * - That we can reset the value.
 * - that on reset, the new value can be read back.
 */
#define	BOOLEAN_ANYONE		1
#define	BOOLEAN_ROOTONLY	1
static void
test_ip_boolean(int sock, const char *socktypename, int option,
    char *optionname, int initial, int rootonly)
{
	int newvalue, val[2];
	socklen_t len;

	/*
	 * The default for a boolean might be true or false.  If it's false,
	 * we will try setting it to true (but using a non-1 value of true).
	 * If it's true, we'll set it to false.
	 */
	if (initial == 0)
		newvalue = 0xff;
	else
		newvalue = 0;

	val[0] = -1;
	val[1] = -1;
	len = sizeof(val);
	if (getsockopt(sock, IPPROTO_IP, option, val, &len) < 0)
		err(-1, "test_ip_boolean: initial getsockopt()");

	if (len != sizeof(val[0]))
		errx(-1, "test_ip_boolean(%s, %s): initial getsockopt() "
		    "returned %d bytes", socktypename, optionname, len);

	if (val[0] == -1)
		errx(-1, "test_ip_boolean(%s, %s): initial getsockopt() "
		    "didn't return data", socktypename, optionname);

	if (val[0] != initial)
		errx(-1, "test_ip_boolean(%s, %s): initial getsockopt() "
		    "returned %d (expected %d)", socktypename, optionname,
		    val[0], initial);

	/*
	 * Set the socket option to a new non-default value.
	 */
	if (setsockopt(sock, IPPROTO_IP, option, &newvalue, sizeof(newvalue))
	    < 0)
		err(-1, "test_ip_boolean(%s, %s): setsockopt() to %d",
		    socktypename, optionname, newvalue);

	/*
	 * Read the value back and see if it is not the default (note: will
	 * not be what we set it to, as we set it to 0xff above).
	 */
	val[0] = -1;
	val[1] = -1;
	len = sizeof(val);
	if (getsockopt(sock, IPPROTO_IP, option, val, &len) < 0)
		err(-1, "test_ip_boolean(%s, %s): getsockopt() after set to "
		    "%d", socktypename, optionname, newvalue);

	if (len != sizeof(val[0]))
		errx(-1, "test_ip_boolean(%s, %s): getsockopt() after set "
		    "to %d returned %d bytes", socktypename, optionname,
		    newvalue, len);

	if (val[0] == -1)
		errx(-1, "test_ip_boolean(%s, %s): getsockopt() after set "
		    "to %d didn't return data", socktypename, optionname,
		    newvalue);

	/*
	 * If we set it to true, check for '1', otherwise '0.
	 */
	if (val[0] != (newvalue ? 1 : 0))
		errx(-1, "test_ip_boolean(%s, %s): getsockopt() after set "
		    "to %d returned %d", socktypename, optionname, newvalue,
		    val[0]);

	/*
	 * Reset to initial value.
	 */
	newvalue = initial;
	if (setsockopt(sock, IPPROTO_IP, option, &newvalue, sizeof(newvalue))
	    < 0)
		err(-1, "test_ip_boolean(%s, %s): setsockopt() to reset",
		    socktypename, optionname);

	/*
	 * Check reset version.
	 */
	val[0] = -1;
	val[1] = -1;
	len = sizeof(val);
	if (getsockopt(sock, IPPROTO_IP, option, val, &len) < 0)
		err(-1, "test_ip_boolean(%s, %s): getsockopt() after reset",
		    socktypename, optionname);

	if (len != sizeof(val[0]))
		errx(-1, "test_ip_boolean(%s, %s): getsockopt() after reset "
		    "returned %d bytes", socktypename, optionname, len);

	if (val[0] == -1)
		errx(-1, "test_ip_boolean(%s, %s): getsockopt() after reset "
		    "didn't return data", socktypename, optionname);

	if (val[0] != newvalue)
		errx(-1, "test_ip_boolean(%s, %s): getsockopt() after reset "
		    "returned %d", socktypename, optionname, newvalue);
}

/*
 * Test the IP_ADD_MEMBERSHIP socket option, and the dynamic allocator
 * for the imo_membership vector which now hangs off struct ip_moptions.
 * We then call IP_DROP_MEMBERSHIP for each group so joined.
 */
static void
test_ip_multicast_membership(int sock, const char *socktypename)
{
    char addrbuf[16];
    struct ip_mreq mreq;
    uint32_t basegroup;
    uint16_t i;
    int sotype;
    socklen_t sotypelen;

    sotypelen = sizeof(sotype);
    if (getsockopt(sock, SOL_SOCKET, SO_TYPE, &sotype, &sotypelen) < 0)
	err(-1, "test_ip_multicast_membership(%s): so_type getsockopt()",
	    socktypename);
    /*
     * Do not perform the test for SOCK_STREAM sockets, as this makes
     * no sense.
     */
    if (sotype == SOCK_STREAM)
	return;
    /*
     * The 224/8 range is administratively scoped and has special meaning,
     * therefore it is not used for this test.
     * If we were not told to be non-deterministic:
     * Join multicast groups from 238.1.1.0 up to nmcastgroups.
     * Otherwise, pick a multicast group ID in subnet 238/5 with 11 random
     * bits in the middle, and join groups in linear order up to nmcastgroups.
     */
    if (dorandom) {
	/* be non-deterministic (for interactive operation; a fuller test) */
	srandomdev();
	basegroup = 0xEE000000;	/* 238.0.0.0 */
	basegroup |= ((random() % ((1 << 11) - 1)) << 16);	/* 11 bits */
    } else {
	/* be deterministic (for automated operation) */
	basegroup = 0xEE010100;	/* 238.1.1.0 */
    }
    /*
     * Join the multicast group(s) on the default multicast interface;
     * this usually maps to the interface to which the default
     * route is pointing.
     */
    for (i = 1; i < nmcastgroups+1; i++) {
	mreq.imr_multiaddr.s_addr = htonl((basegroup + i));
	mreq.imr_interface.s_addr = INADDR_ANY;
	inet_ntop(AF_INET, &mreq.imr_multiaddr, addrbuf, sizeof(addrbuf));
	if (verbose)
		fprintf(stderr, "IP_ADD_MEMBERSHIP %s INADDR_ANY\n", addrbuf);
	if (setsockopt(sock, IPPROTO_IP, IP_ADD_MEMBERSHIP, &mreq,
		       sizeof(mreq)) < 0) {
		err(-1,
"test_ip_multicast_membership(%d, %s): failed IP_ADD_MEMBERSHIP (%s, %s)",
		    sock, socktypename, addrbuf, "INADDR_ANY");
	}
    }
    for (i = 1; i < nmcastgroups+1; i++) {
	mreq.imr_multiaddr.s_addr = htonl((basegroup + i));
	mreq.imr_interface.s_addr = INADDR_ANY;
	inet_ntop(AF_INET, &mreq.imr_multiaddr, addrbuf, sizeof(addrbuf));
	if (verbose)
		fprintf(stderr, "IP_DROP_MEMBERSHIP %s INADDR_ANY\n", addrbuf);
	if (setsockopt(sock, IPPROTO_IP, IP_DROP_MEMBERSHIP, &mreq,
		       sizeof(mreq)) < 0) {
		err(-1,
"test_ip_multicast_membership(%d, %s): failed IP_DROP_MEMBERSHIP (%s, %s)",
		    sock, socktypename, addrbuf, "INADDR_ANY");
	}
    }
}

/*
 * XXX: For now, nothing here.
 */
static void
test_ip_multicast_if(int sock, const char *socktypename)
{

	/*
	 * It's probably worth trying INADDR_ANY and INADDR_LOOPBACK here
	 * to see what happens.
	 */
}

/*
 * XXX: For now, nothing here.
 */
static void
test_ip_multicast_vif(int sock, const char *socktypename)
{

	/*
	 * This requires some knowledge of the number of virtual interfaces,
	 * and what is valid.
	 */
}

static void
testsuite(int priv)
{
	const char *socktypenameset[] = {"SOCK_DGRAM", "SOCK_STREAM",
	    "SOCK_RAW"};
	int socktypeset[] = {SOCK_DGRAM, SOCK_STREAM, SOCK_RAW};
	const char *socktypename;
	int i, sock, socktype;

	test_ip_hdrincl();

	for (i = 0; i < sizeof(socktypeset)/sizeof(int); i++) {
		socktype = socktypeset[i];
		socktypename = socktypenameset[i];

		/*
		 * If we can't acquire root privilege, we can't open raw
		 * sockets, so don't actually try.
		 */
		if (getuid() != 0 && socktype == SOCK_RAW)
			continue;
		if (geteuid() != 0 && !priv && socktype == SOCK_RAW)
			continue;

		/*
		 * XXXRW: On 5.3, this seems not to work for SOCK_RAW.
		 */
		sock = get_socket(socktype, priv);
		if (sock == -1)
			err(-1, "get_socket(%s, %d) for test_ip_uchar(IP_TOS)",
			    socktypename, priv);
		test_ip_uchar(sock, socktypename, IP_TOS, "IP_TOS", 0);
		close(sock);

		sock = get_socket(socktype, priv);
		if (sock == -1)
			err(-1, "get_socket(%s %d) for test_ip_uchar(IP_TTL)",
			    socktypename, priv);
		test_ip_uchar(sock, socktypename, IP_TTL, "IP_TTL", 64);
		close(sock);

		sock = get_socket(socktype, priv);
		if (sock == -1)
			err(-1, "get_socket(%s, %d) for test_ip_boolean"
			    "(IP_RECVOPTS)", socktypename, priv);
		test_ip_boolean(sock, socktypename, IP_RECVOPTS,
		    "IP_RECVOPTS", 0, BOOLEAN_ANYONE);
		close(sock);

		sock = get_socket(socktype, priv);
		if (sock == -1)
			err(-1, "get_socket(%s, %d) for test_ip_boolean"
			     "(IP_RECVRETOPTS)", socktypename, priv);
		test_ip_boolean(sock, socktypename, IP_RECVRETOPTS,
		    "IP_RECVRETOPTS", 0, BOOLEAN_ANYONE);
		close(sock);

		sock = get_socket(socktype, priv);
		if (sock == -1)
			err(-1, "get_socket(%s, %d) for test_ip_boolean"
			    "(IP_RECVDSTADDR)", socktypename, priv);
		test_ip_boolean(sock, socktypename, IP_RECVDSTADDR,
		    "IP_RECVDSTADDR", 0, BOOLEAN_ANYONE);
		close(sock);

		sock = get_socket(socktype, priv);
		if (sock == -1)
			err(-1, "get_socket(%s, %d) for test_ip_boolean"
			    "(IP_RECVTTL)", socktypename, priv);
		test_ip_boolean(sock, socktypename, IP_RECVTTL, "IP_RECVTTL",
		    0, BOOLEAN_ANYONE);
		close(sock);

		sock = get_socket(socktype, priv);
		if (sock == -1)
			err(-1, "get_socket(%s, %d) for test_ip_boolean"
			    "(IP_RECVIF)", socktypename, priv);
		test_ip_boolean(sock, socktypename, IP_RECVIF, "IP_RECVIF",
		    0, BOOLEAN_ANYONE);
		close(sock);

		sock = get_socket(socktype, priv);
		if (sock == -1)
			err(-1, "get_socket(%s, %d) for test_ip_boolean"
			    "(IP_FAITH)", socktypename, priv);
		test_ip_boolean(sock, socktypename, IP_FAITH, "IP_FAITH", 0,
		    BOOLEAN_ANYONE);
		close(sock);

		sock = get_socket(socktype, priv);
		if (sock == -1)
			err(-1, "get_socket(%s, %d) for test_ip_boolean"
			    "(IP_ONESBCAST)", socktypename, priv);
		test_ip_boolean(sock, socktypename, IP_ONESBCAST,
		    "IP_ONESBCAST", 0, BOOLEAN_ANYONE);
		close(sock);

		/*
		 * Test the multicast TTL exactly as we would the regular
		 * TTL, only expect a different default.
		 */
		sock = get_socket(socktype, priv);
		if (sock == -1)
			err(-1, "get_socket(%s, %d) for IP_MULTICAST_TTL",
			    socktypename, priv);
		test_ip_uchar(sock, socktypename, IP_MULTICAST_TTL,
		    "IP_MULTICAST_TTL", 1);
		close(sock);

		/*
		 * The multicast loopback flag can be tested using our
		 * boolean tester, but only because the FreeBSD API is a bit
		 * more flexible than earlir APIs and will accept an int as
		 * well as a u_char.  Loopback is enabled by default.
		 */
		sock = get_socket(socktype, priv);
		if (sock == -1)
			err(-1, "get_socket(%s, %d) for IP_MULTICAST_LOOP",
			    socktypename, priv);
		test_ip_boolean(sock, socktypename, IP_MULTICAST_LOOP,
		    "IP_MULTICAST_LOOP", 1, BOOLEAN_ANYONE);
		close(sock);

		sock = get_socket(socktype, priv);
		if (sock == -1)
			err(-1, "get_socket(%s, %d) for test_ip_options",
			    socktypename, priv);
		//test_ip_options(sock, socktypename);
		close(sock);

		sock = get_socket(socktype, priv);
		if (sock == -1)
			err(-1, "get_socket(%s, %d) for test_ip_options",
			    socktypename, priv);
		test_ip_multicast_membership(sock, socktypename);
		close(sock);

		test_ip_multicast_if(0, NULL);
		test_ip_multicast_vif(0, NULL);
		/*
		 * XXX: Still need to test:
		 * IP_PORTRANGE
		 * IP_IPSEC_POLICY?
		 */
	}
}

static void
usage()
{

	fprintf(stderr, "usage: ipsockopt [-M ngroups] [-r] [-v]\n");
	exit(EXIT_FAILURE);
}

/*
 * Very simply exercise that we can get and set each option.  If we're running
 * as root, run it also as nobody.  If not as root, complain about that.
 */
int
main(int argc, char *argv[])
{
	int ch;

	while ((ch = getopt(argc, argv, "M:rv")) != -1) {
		switch (ch) {
		case 'M':
			nmcastgroups = atoi(optarg);
			break;
		case 'r':
			dorandom = 1;	/* introduce non-determinism */
			break;
		case 'v':
			verbose = 1;
			break;
		default:
			usage();
		}
	}

	printf("1..1\n");

	if (geteuid() != 0) {
		warnx("Not running as root, can't run tests as root");
		fprintf(stderr, "\n");
		fprintf(stderr,
		   "Running tests with uid %d sock uid %d\n", geteuid(),
		    geteuid());
		testsuite(PRIV_ASIS);
	} else {
		fprintf(stderr,
		    "Running tests with ruid %d euid %d sock uid 0\n",
		    getuid(), geteuid());
		testsuite(PRIV_ASIS);
		if (seteuid(65534) != 0)
			err(-1, "seteuid(65534)");
		fprintf(stderr,
		    "Running tests with ruid %d euid %d sock uid 65534\n",
		    getuid(), geteuid());
		testsuite(PRIV_ASIS);
		fprintf(stderr,
		    "Running tests with ruid %d euid %d sock uid 0\n",
		    getuid(), geteuid());
		testsuite(PRIV_GETROOT);
	}
	printf("ok 1 - ipsockopt\n");
	exit(0);
}
