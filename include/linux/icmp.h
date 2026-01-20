/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * INET		An implementation of the TCP/IP protocol suite for the LINUX
 *		operating system.  INET is implemented using the  BSD Socket
 *		interface as the means of communication with the user level.
 *
 *		Definitions for the ICMP protocol.
 *
 * Version:	@(#)icmp.h	1.0.3	04/28/93
 *
 * Author:	Fred N. van Kempen, <waltje@uWalt.NL.Mugnet.ORG>
 */
#ifndef _LINUX_ICMP_H
#define	_LINUX_ICMP_H

#include <linux/skbuff.h>
#include <uapi/linux/icmp.h>
#include <uapi/linux/errqueue.h>

static inline struct icmphdr *icmp_hdr(const struct sk_buff *skb)
{
	return (struct icmphdr *)skb_transport_header(skb);
}

static inline bool icmp_is_err(int type)
{
	switch (type) {
	case ICMP_DEST_UNREACH:
	case ICMP_SOURCE_QUENCH:
	case ICMP_REDIRECT:
	case ICMP_TIME_EXCEEDED:
	case ICMP_PARAMETERPROB:
		return true;
	}

	return false;
}

void ip_icmp_error_rfc4884(const struct sk_buff *skb,
			   struct sock_ee_data_rfc4884 *out,
			   int thlen, int off);

/* RFC 4884 */
#define ICMP_EXT_ORIG_DGRAM_MIN_LEN	128
#define ICMP_EXT_VERSION_2		2

/* ICMP Extension Object Classes */
#define ICMP_EXT_OBJ_CLASS_IIO		2	/* RFC 5837 */

/* Interface Information Object - RFC 5837 */
enum {
	ICMP_EXT_CTYPE_IIO_ROLE_IIF,
};

#define ICMP_EXT_CTYPE_IIO_ROLE(ROLE)	((ROLE) << 6)
#define ICMP_EXT_CTYPE_IIO_MTU		BIT(0)
#define ICMP_EXT_CTYPE_IIO_NAME		BIT(1)
#define ICMP_EXT_CTYPE_IIO_IPADDR	BIT(2)
#define ICMP_EXT_CTYPE_IIO_IFINDEX	BIT(3)

struct icmp_ext_iio_name_subobj {
	u8 len;
	char name[IFNAMSIZ];
};

enum {
	/* RFC 5837 - Incoming IP Interface Role */
	ICMP_ERR_EXT_IIO_IIF,
	/* Add new constants above. Used by "icmp_errors_extension_mask"
	 * sysctl.
	 */
	ICMP_ERR_EXT_COUNT,
};

#endif	/* _LINUX_ICMP_H */
