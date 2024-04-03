// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *	X.25 Packet Layer release 002
 *
 *	This is ALPHA test software. This code may break your machine,
 *	randomly fail to work with new releases, misbehave and/or generally
 *	screw up. It might even work.
 *
 *	This code REQUIRES 2.1.15 or higher
 *
 *	History
 *	X.25 001	Split from x25_subr.c
 *	mar/20/00	Daniela Squassoni Disabling/enabling of facilities
 *					  negotiation.
 *	apr/14/05	Shaun Pereira - Allow fast select with no restriction
 *					on response.
 */

#define pr_fmt(fmt) "X25: " fmt

#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/skbuff.h>
#include <net/sock.h>
#include <net/x25.h>

/**
 * x25_parse_facilities - Parse facilities from skb into the facilities structs
 *
 * @skb: sk_buff to parse
 * @facilities: Regular facilities, updated as facilities are found
 * @dte_facs: ITU DTE facilities, updated as DTE facilities are found
 * @vc_fac_mask: mask is updated with all facilities found
 *
 * Return codes:
 *  -1 - Parsing error, caller should drop call and clean up
 *   0 - Parse OK, this skb has no facilities
 *  >0 - Parse OK, returns the length of the facilities header
 *
 */
int x25_parse_facilities(struct sk_buff *skb, struct x25_facilities *facilities,
		struct x25_dte_facilities *dte_facs, unsigned long *vc_fac_mask)
{
	unsigned char *p;
	unsigned int len;

	*vc_fac_mask = 0;

	/*
	 * The kernel knows which facilities were set on an incoming call but
	 * currently this information is not available to userspace.  Here we
	 * give userspace who read incoming call facilities 0 length to indicate
	 * it wasn't set.
	 */
	dte_facs->calling_len = 0;
	dte_facs->called_len = 0;
	memset(dte_facs->called_ae, '\0', sizeof(dte_facs->called_ae));
	memset(dte_facs->calling_ae, '\0', sizeof(dte_facs->calling_ae));

	if (!pskb_may_pull(skb, 1))
		return 0;

	len = skb->data[0];

	if (!pskb_may_pull(skb, 1 + len))
		return -1;

	p = skb->data + 1;

	while (len > 0) {
		switch (*p & X25_FAC_CLASS_MASK) {
		case X25_FAC_CLASS_A:
			if (len < 2)
				return -1;
			switch (*p) {
			case X25_FAC_REVERSE:
				if((p[1] & 0x81) == 0x81) {
					facilities->reverse = p[1] & 0x81;
					*vc_fac_mask |= X25_MASK_REVERSE;
					break;
				}

				if((p[1] & 0x01) == 0x01) {
					facilities->reverse = p[1] & 0x01;
					*vc_fac_mask |= X25_MASK_REVERSE;
					break;
				}

				if((p[1] & 0x80) == 0x80) {
					facilities->reverse = p[1] & 0x80;
					*vc_fac_mask |= X25_MASK_REVERSE;
					break;
				}

				if(p[1] == 0x00) {
					facilities->reverse
						= X25_DEFAULT_REVERSE;
					*vc_fac_mask |= X25_MASK_REVERSE;
					break;
				}
				fallthrough;
			case X25_FAC_THROUGHPUT:
				facilities->throughput = p[1];
				*vc_fac_mask |= X25_MASK_THROUGHPUT;
				break;
			case X25_MARKER:
				break;
			default:
				pr_debug("unknown facility "
				       "%02X, value %02X\n",
				       p[0], p[1]);
				break;
			}
			p   += 2;
			len -= 2;
			break;
		case X25_FAC_CLASS_B:
			if (len < 3)
				return -1;
			switch (*p) {
			case X25_FAC_PACKET_SIZE:
				facilities->pacsize_in  = p[1];
				facilities->pacsize_out = p[2];
				*vc_fac_mask |= X25_MASK_PACKET_SIZE;
				break;
			case X25_FAC_WINDOW_SIZE:
				facilities->winsize_in  = p[1];
				facilities->winsize_out = p[2];
				*vc_fac_mask |= X25_MASK_WINDOW_SIZE;
				break;
			default:
				pr_debug("unknown facility "
				       "%02X, values %02X, %02X\n",
				       p[0], p[1], p[2]);
				break;
			}
			p   += 3;
			len -= 3;
			break;
		case X25_FAC_CLASS_C:
			if (len < 4)
				return -1;
			pr_debug("unknown facility %02X, "
			       "values %02X, %02X, %02X\n",
			       p[0], p[1], p[2], p[3]);
			p   += 4;
			len -= 4;
			break;
		case X25_FAC_CLASS_D:
			if (len < p[1] + 2)
				return -1;
			switch (*p) {
			case X25_FAC_CALLING_AE:
				if (p[1] > X25_MAX_DTE_FACIL_LEN || p[1] <= 1)
					return -1;
				if (p[2] > X25_MAX_AE_LEN)
					return -1;
				dte_facs->calling_len = p[2];
				memcpy(dte_facs->calling_ae, &p[3], p[1] - 1);
				*vc_fac_mask |= X25_MASK_CALLING_AE;
				break;
			case X25_FAC_CALLED_AE:
				if (p[1] > X25_MAX_DTE_FACIL_LEN || p[1] <= 1)
					return -1;
				if (p[2] > X25_MAX_AE_LEN)
					return -1;
				dte_facs->called_len = p[2];
				memcpy(dte_facs->called_ae, &p[3], p[1] - 1);
				*vc_fac_mask |= X25_MASK_CALLED_AE;
				break;
			default:
				pr_debug("unknown facility %02X,"
					"length %d\n", p[0], p[1]);
				break;
			}
			len -= p[1] + 2;
			p += p[1] + 2;
			break;
		}
	}

	return p - skb->data;
}

/*
 *	Create a set of facilities.
 */
int x25_create_facilities(unsigned char *buffer,
		struct x25_facilities *facilities,
		struct x25_dte_facilities *dte_facs, unsigned long facil_mask)
{
	unsigned char *p = buffer + 1;
	int len;

	if (!facil_mask) {
		/*
		 * Length of the facilities field in call_req or
		 * call_accept packets
		 */
		buffer[0] = 0;
		len = 1; /* 1 byte for the length field */
		return len;
	}

	if (facilities->reverse && (facil_mask & X25_MASK_REVERSE)) {
		*p++ = X25_FAC_REVERSE;
		*p++ = facilities->reverse;
	}

	if (facilities->throughput && (facil_mask & X25_MASK_THROUGHPUT)) {
		*p++ = X25_FAC_THROUGHPUT;
		*p++ = facilities->throughput;
	}

	if ((facilities->pacsize_in || facilities->pacsize_out) &&
	    (facil_mask & X25_MASK_PACKET_SIZE)) {
		*p++ = X25_FAC_PACKET_SIZE;
		*p++ = facilities->pacsize_in ? : facilities->pacsize_out;
		*p++ = facilities->pacsize_out ? : facilities->pacsize_in;
	}

	if ((facilities->winsize_in || facilities->winsize_out) &&
	    (facil_mask & X25_MASK_WINDOW_SIZE)) {
		*p++ = X25_FAC_WINDOW_SIZE;
		*p++ = facilities->winsize_in ? : facilities->winsize_out;
		*p++ = facilities->winsize_out ? : facilities->winsize_in;
	}

	if (facil_mask & (X25_MASK_CALLING_AE|X25_MASK_CALLED_AE)) {
		*p++ = X25_MARKER;
		*p++ = X25_DTE_SERVICES;
	}

	if (dte_facs->calling_len && (facil_mask & X25_MASK_CALLING_AE)) {
		unsigned int bytecount = (dte_facs->calling_len + 1) >> 1;
		*p++ = X25_FAC_CALLING_AE;
		*p++ = 1 + bytecount;
		*p++ = dte_facs->calling_len;
		memcpy(p, dte_facs->calling_ae, bytecount);
		p += bytecount;
	}

	if (dte_facs->called_len && (facil_mask & X25_MASK_CALLED_AE)) {
		unsigned int bytecount = (dte_facs->called_len % 2) ?
		dte_facs->called_len / 2 + 1 :
		dte_facs->called_len / 2;
		*p++ = X25_FAC_CALLED_AE;
		*p++ = 1 + bytecount;
		*p++ = dte_facs->called_len;
		memcpy(p, dte_facs->called_ae, bytecount);
		p+=bytecount;
	}

	len       = p - buffer;
	buffer[0] = len - 1;

	return len;
}

/*
 *	Try to reach a compromise on a set of facilities.
 *
 *	The only real problem is with reverse charging.
 */
int x25_negotiate_facilities(struct sk_buff *skb, struct sock *sk,
		struct x25_facilities *new, struct x25_dte_facilities *dte)
{
	struct x25_sock *x25 = x25_sk(sk);
	struct x25_facilities *ours = &x25->facilities;
	struct x25_facilities theirs;
	int len;

	memset(&theirs, 0, sizeof(theirs));
	memcpy(new, ours, sizeof(*new));
	memset(dte, 0, sizeof(*dte));

	len = x25_parse_facilities(skb, &theirs, dte, &x25->vc_facil_mask);
	if (len < 0)
		return len;

	/*
	 *	They want reverse charging, we won't accept it.
	 */
	if ((theirs.reverse & 0x01 ) && (ours->reverse & 0x01)) {
		net_dbg_ratelimited("X.25: rejecting reverse charging request\n");
		return -1;
	}

	new->reverse = theirs.reverse;

	if (theirs.throughput) {
		int theirs_in =  theirs.throughput & 0x0f;
		int theirs_out = theirs.throughput & 0xf0;
		int ours_in  = ours->throughput & 0x0f;
		int ours_out = ours->throughput & 0xf0;
		if (!ours_in || theirs_in < ours_in) {
			net_dbg_ratelimited("X.25: inbound throughput negotiated\n");
			new->throughput = (new->throughput & 0xf0) | theirs_in;
		}
		if (!ours_out || theirs_out < ours_out) {
			net_dbg_ratelimited(
				"X.25: outbound throughput negotiated\n");
			new->throughput = (new->throughput & 0x0f) | theirs_out;
		}
	}

	if (theirs.pacsize_in && theirs.pacsize_out) {
		if (theirs.pacsize_in < ours->pacsize_in) {
			net_dbg_ratelimited("X.25: packet size inwards negotiated down\n");
			new->pacsize_in = theirs.pacsize_in;
		}
		if (theirs.pacsize_out < ours->pacsize_out) {
			net_dbg_ratelimited("X.25: packet size outwards negotiated down\n");
			new->pacsize_out = theirs.pacsize_out;
		}
	}

	if (theirs.winsize_in && theirs.winsize_out) {
		if (theirs.winsize_in < ours->winsize_in) {
			net_dbg_ratelimited("X.25: window size inwards negotiated down\n");
			new->winsize_in = theirs.winsize_in;
		}
		if (theirs.winsize_out < ours->winsize_out) {
			net_dbg_ratelimited("X.25: window size outwards negotiated down\n");
			new->winsize_out = theirs.winsize_out;
		}
	}

	return len;
}

/*
 *	Limit values of certain facilities according to the capability of the
 *      currently attached x25 link.
 */
void x25_limit_facilities(struct x25_facilities *facilities,
			  struct x25_neigh *nb)
{

	if (!nb->extended) {
		if (facilities->winsize_in  > 7) {
			pr_debug("incoming winsize limited to 7\n");
			facilities->winsize_in = 7;
		}
		if (facilities->winsize_out > 7) {
			facilities->winsize_out = 7;
			pr_debug("outgoing winsize limited to 7\n");
		}
	}
}
