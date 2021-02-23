/* SPDX-License-Identifier: GPL-2.0 */
/*
 * linux/include/linux/sunrpc/msg_prot.h
 *
 * Copyright (C) 1996, Olaf Kirch <okir@monad.swb.de>
 */

#ifndef _LINUX_SUNRPC_MSGPROT_H_
#define _LINUX_SUNRPC_MSGPROT_H_

#define RPC_VERSION 2

/* size of an XDR encoding unit in bytes, i.e. 32bit */
#define XDR_UNIT	(4)

/* spec defines authentication flavor as an unsigned 32 bit integer */
typedef u32	rpc_authflavor_t;

enum rpc_auth_flavors {
	RPC_AUTH_NULL  = 0,
	RPC_AUTH_UNIX  = 1,
	RPC_AUTH_SHORT = 2,
	RPC_AUTH_DES   = 3,
	RPC_AUTH_KRB   = 4,
	RPC_AUTH_GSS   = 6,
	RPC_AUTH_MAXFLAVOR = 8,
	/* pseudoflavors: */
	RPC_AUTH_GSS_KRB5  = 390003,
	RPC_AUTH_GSS_KRB5I = 390004,
	RPC_AUTH_GSS_KRB5P = 390005,
	RPC_AUTH_GSS_LKEY  = 390006,
	RPC_AUTH_GSS_LKEYI = 390007,
	RPC_AUTH_GSS_LKEYP = 390008,
	RPC_AUTH_GSS_SPKM  = 390009,
	RPC_AUTH_GSS_SPKMI = 390010,
	RPC_AUTH_GSS_SPKMP = 390011,
};

/* Maximum size (in bytes) of an rpc credential or verifier */
#define RPC_MAX_AUTH_SIZE (400)

enum rpc_msg_type {
	RPC_CALL = 0,
	RPC_REPLY = 1
};

enum rpc_reply_stat {
	RPC_MSG_ACCEPTED = 0,
	RPC_MSG_DENIED = 1
};

enum rpc_accept_stat {
	RPC_SUCCESS = 0,
	RPC_PROG_UNAVAIL = 1,
	RPC_PROG_MISMATCH = 2,
	RPC_PROC_UNAVAIL = 3,
	RPC_GARBAGE_ARGS = 4,
	RPC_SYSTEM_ERR = 5,
	/* internal use only */
	RPC_DROP_REPLY = 60000,
};

enum rpc_reject_stat {
	RPC_MISMATCH = 0,
	RPC_AUTH_ERROR = 1
};

enum rpc_auth_stat {
	RPC_AUTH_OK = 0,
	RPC_AUTH_BADCRED = 1,
	RPC_AUTH_REJECTEDCRED = 2,
	RPC_AUTH_BADVERF = 3,
	RPC_AUTH_REJECTEDVERF = 4,
	RPC_AUTH_TOOWEAK = 5,
	/* RPCSEC_GSS errors */
	RPCSEC_GSS_CREDPROBLEM = 13,
	RPCSEC_GSS_CTXPROBLEM = 14
};

#define RPC_MAXNETNAMELEN	256

/*
 * From RFC 1831:
 *
 * "A record is composed of one or more record fragments.  A record
 *  fragment is a four-byte header followed by 0 to (2**31) - 1 bytes of
 *  fragment data.  The bytes encode an unsigned binary number; as with
 *  XDR integers, the byte order is from highest to lowest.  The number
 *  encodes two values -- a boolean which indicates whether the fragment
 *  is the last fragment of the record (bit value 1 implies the fragment
 *  is the last fragment) and a 31-bit unsigned binary value which is the
 *  length in bytes of the fragment's data.  The boolean value is the
 *  highest-order bit of the header; the length is the 31 low-order bits.
 *  (Note that this record specification is NOT in XDR standard form!)"
 *
 * The Linux RPC client always sends its requests in a single record
 * fragment, limiting the maximum payload size for stream transports to
 * 2GB.
 */

typedef __be32	rpc_fraghdr;

#define	RPC_LAST_STREAM_FRAGMENT	(1U << 31)
#define	RPC_FRAGMENT_SIZE_MASK		(~RPC_LAST_STREAM_FRAGMENT)
#define	RPC_MAX_FRAGMENT_SIZE		((1U << 31) - 1)

/*
 * RPC call and reply header size as number of 32bit words (verifier
 * size computed separately, see below)
 */
#define RPC_CALLHDRSIZE		(6)
#define RPC_REPHDRSIZE		(4)


/*
 * Maximum RPC header size, including authentication,
 * as number of 32bit words (see RFCs 1831, 1832).
 *
 *	xid			    1 xdr unit = 4 bytes
 *	mtype			    1
 *	rpc_version		    1
 *	program			    1
 *	prog_version		    1
 *	procedure		    1
 *	cred {
 *	    flavor		    1
 *	    length		    1
 *	    body<RPC_MAX_AUTH_SIZE> 100 xdr units = 400 bytes
 *	}
 *	verf {
 *	    flavor		    1
 *	    length		    1
 *	    body<RPC_MAX_AUTH_SIZE> 100 xdr units = 400 bytes
 *	}
 *	TOTAL			    210 xdr units = 840 bytes
 */
#define RPC_MAX_HEADER_WITH_AUTH \
	(RPC_CALLHDRSIZE + 2*(2+RPC_MAX_AUTH_SIZE/4))

#define RPC_MAX_REPHEADER_WITH_AUTH \
	(RPC_REPHDRSIZE + (2 + RPC_MAX_AUTH_SIZE/4))

/*
 * Well-known netids. See:
 *
 *   https://www.iana.org/assignments/rpc-netids/rpc-netids.xhtml
 */
#define RPCBIND_NETID_UDP	"udp"
#define RPCBIND_NETID_TCP	"tcp"
#define RPCBIND_NETID_RDMA	"rdma"
#define RPCBIND_NETID_SCTP	"sctp"
#define RPCBIND_NETID_UDP6	"udp6"
#define RPCBIND_NETID_TCP6	"tcp6"
#define RPCBIND_NETID_RDMA6	"rdma6"
#define RPCBIND_NETID_SCTP6	"sctp6"
#define RPCBIND_NETID_LOCAL	"local"

/*
 * Note that RFC 1833 does not put any size restrictions on the
 * netid string, but all currently defined netid's fit in 5 bytes.
 */
#define RPCBIND_MAXNETIDLEN	(5u)

/*
 * Universal addresses are introduced in RFC 1833 and further spelled
 * out in RFC 3530.  RPCBIND_MAXUADDRLEN defines a maximum byte length
 * of a universal address for use in allocating buffers and character
 * arrays.
 *
 * Quoting RFC 3530, section 2.2:
 *
 * For TCP over IPv4 and for UDP over IPv4, the format of r_addr is the
 * US-ASCII string:
 *
 *	h1.h2.h3.h4.p1.p2
 *
 * The prefix, "h1.h2.h3.h4", is the standard textual form for
 * representing an IPv4 address, which is always four octets long.
 * Assuming big-endian ordering, h1, h2, h3, and h4, are respectively,
 * the first through fourth octets each converted to ASCII-decimal.
 * Assuming big-endian ordering, p1 and p2 are, respectively, the first
 * and second octets each converted to ASCII-decimal.  For example, if a
 * host, in big-endian order, has an address of 0x0A010307 and there is
 * a service listening on, in big endian order, port 0x020F (decimal
 * 527), then the complete universal address is "10.1.3.7.2.15".
 *
 * ...
 *
 * For TCP over IPv6 and for UDP over IPv6, the format of r_addr is the
 * US-ASCII string:
 *
 *	x1:x2:x3:x4:x5:x6:x7:x8.p1.p2
 *
 * The suffix "p1.p2" is the service port, and is computed the same way
 * as with universal addresses for TCP and UDP over IPv4.  The prefix,
 * "x1:x2:x3:x4:x5:x6:x7:x8", is the standard textual form for
 * representing an IPv6 address as defined in Section 2.2 of [RFC2373].
 * Additionally, the two alternative forms specified in Section 2.2 of
 * [RFC2373] are also acceptable.
 */

#include <linux/inet.h>

/* Maximum size of the port number part of a universal address */
#define RPCBIND_MAXUADDRPLEN	sizeof(".255.255")

/* Maximum size of an IPv4 universal address */
#define RPCBIND_MAXUADDR4LEN	\
		(INET_ADDRSTRLEN + RPCBIND_MAXUADDRPLEN)

/* Maximum size of an IPv6 universal address */
#define RPCBIND_MAXUADDR6LEN	\
		(INET6_ADDRSTRLEN + RPCBIND_MAXUADDRPLEN)

/* Assume INET6_ADDRSTRLEN will always be larger than INET_ADDRSTRLEN... */
#define RPCBIND_MAXUADDRLEN	RPCBIND_MAXUADDR6LEN

#endif /* _LINUX_SUNRPC_MSGPROT_H_ */
