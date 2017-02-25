/*
 *  SR-IPv6 implementation
 *
 *  Author:
 *  David Lebrun <david.lebrun@uclouvain.be>
 *
 *
 *  This program is free software; you can redistribute it and/or
 *      modify it under the terms of the GNU General Public License
 *      as published by the Free Software Foundation; either version
 *      2 of the License, or (at your option) any later version.
 */

#ifndef _UAPI_LINUX_SEG6_IPTUNNEL_H
#define _UAPI_LINUX_SEG6_IPTUNNEL_H

enum {
	SEG6_IPTUNNEL_UNSPEC,
	SEG6_IPTUNNEL_SRH,
	__SEG6_IPTUNNEL_MAX,
};
#define SEG6_IPTUNNEL_MAX (__SEG6_IPTUNNEL_MAX - 1)

struct seg6_iptunnel_encap {
	int mode;
	struct ipv6_sr_hdr srh[0];
};

#define SEG6_IPTUN_ENCAP_SIZE(x) ((sizeof(*x)) + (((x)->srh->hdrlen + 1) << 3))

enum {
	SEG6_IPTUN_MODE_INLINE,
	SEG6_IPTUN_MODE_ENCAP,
};

static inline size_t seg6_lwt_headroom(struct seg6_iptunnel_encap *tuninfo)
{
	int encap = (tuninfo->mode == SEG6_IPTUN_MODE_ENCAP);

	return ((tuninfo->srh->hdrlen + 1) << 3) +
	       (encap * sizeof(struct ipv6hdr));
}

#endif
