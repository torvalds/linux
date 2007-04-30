#ifndef __LINUX_IF_ADDR_H
#define __LINUX_IF_ADDR_H

#include <linux/netlink.h>

struct ifaddrmsg
{
	__u8		ifa_family;
	__u8		ifa_prefixlen;	/* The prefix length		*/
	__u8		ifa_flags;	/* Flags			*/
	__u8		ifa_scope;	/* Address scope		*/
	__u32		ifa_index;	/* Link index			*/
};

/*
 * Important comment:
 * IFA_ADDRESS is prefix address, rather than local interface address.
 * It makes no difference for normally configured broadcast interfaces,
 * but for point-to-point IFA_ADDRESS is DESTINATION address,
 * local address is supplied in IFA_LOCAL attribute.
 */
enum
{
	IFA_UNSPEC,
	IFA_ADDRESS,
	IFA_LOCAL,
	IFA_LABEL,
	IFA_BROADCAST,
	IFA_ANYCAST,
	IFA_CACHEINFO,
	IFA_MULTICAST,
	__IFA_MAX,
};

#define IFA_MAX (__IFA_MAX - 1)

/* ifa_flags */
#define IFA_F_SECONDARY		0x01
#define IFA_F_TEMPORARY		IFA_F_SECONDARY

#define	IFA_F_NODAD		0x02
#define IFA_F_OPTIMISTIC	0x04
#define	IFA_F_HOMEADDRESS	0x10
#define IFA_F_DEPRECATED	0x20
#define IFA_F_TENTATIVE		0x40
#define IFA_F_PERMANENT		0x80

struct ifa_cacheinfo
{
	__u32	ifa_prefered;
	__u32	ifa_valid;
	__u32	cstamp; /* created timestamp, hundredths of seconds */
	__u32	tstamp; /* updated timestamp, hundredths of seconds */
};

/* backwards compatibility for userspace */
#ifndef __KERNEL__
#define IFA_RTA(r)  ((struct rtattr*)(((char*)(r)) + NLMSG_ALIGN(sizeof(struct ifaddrmsg))))
#define IFA_PAYLOAD(n) NLMSG_PAYLOAD(n,sizeof(struct ifaddrmsg))
#endif

#endif
