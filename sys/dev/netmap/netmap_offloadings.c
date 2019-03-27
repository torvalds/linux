/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (C) 2014-2015 Vincenzo Maffione
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *   1. Redistributions of source code must retain the above copyright
 *      notice, this list of conditions and the following disclaimer.
 *   2. Redistributions in binary form must reproduce the above copyright
 *      notice, this list of conditions and the following disclaimer in the
 *      documentation and/or other materials provided with the distribution.
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
 */

/* $FreeBSD$ */

#if defined(__FreeBSD__)
#include <sys/cdefs.h> /* prerequisite */

#include <sys/types.h>
#include <sys/errno.h>
#include <sys/param.h>	/* defines used in kernel.h */
#include <sys/kernel.h>	/* types used in module initialization */
#include <sys/sockio.h>
#include <sys/malloc.h>
#include <sys/socketvar.h>	/* struct socket */
#include <sys/socket.h> /* sockaddrs */
#include <net/if.h>
#include <net/if_var.h>
#include <machine/bus.h>	/* bus_dmamap_* */
#include <sys/endian.h>

#elif defined(linux)

#include "bsd_glue.h"

#elif defined(__APPLE__)

#warning OSX support is only partial
#include "osx_glue.h"

#else

#error	Unsupported platform

#endif /* unsupported */

#include <net/netmap.h>
#include <dev/netmap/netmap_kern.h>



/* This routine is called by bdg_mismatch_datapath() when it finishes
 * accumulating bytes for a segment, in order to fix some fields in the
 * segment headers (which still contain the same content as the header
 * of the original GSO packet). 'pkt' points to the beginning of the IP
 * header of the segment, while 'len' is the length of the IP packet.
 */
static void
gso_fix_segment(uint8_t *pkt, size_t len, u_int ipv4, u_int iphlen, u_int tcp,
		u_int idx, u_int segmented_bytes, u_int last_segment)
{
	struct nm_iphdr *iph = (struct nm_iphdr *)(pkt);
	struct nm_ipv6hdr *ip6h = (struct nm_ipv6hdr *)(pkt);
	uint16_t *check = NULL;
	uint8_t *check_data = NULL;

	if (ipv4) {
		/* Set the IPv4 "Total Length" field. */
		iph->tot_len = htobe16(len);
		nm_prdis("ip total length %u", be16toh(ip->tot_len));

		/* Set the IPv4 "Identification" field. */
		iph->id = htobe16(be16toh(iph->id) + idx);
		nm_prdis("ip identification %u", be16toh(iph->id));

		/* Compute and insert the IPv4 header checksum. */
		iph->check = 0;
		iph->check = nm_os_csum_ipv4(iph);
		nm_prdis("IP csum %x", be16toh(iph->check));
	} else {
		/* Set the IPv6 "Payload Len" field. */
		ip6h->payload_len = htobe16(len-iphlen);
	}

	if (tcp) {
		struct nm_tcphdr *tcph = (struct nm_tcphdr *)(pkt + iphlen);

		/* Set the TCP sequence number. */
		tcph->seq = htobe32(be32toh(tcph->seq) + segmented_bytes);
		nm_prdis("tcp seq %u", be32toh(tcph->seq));

		/* Zero the PSH and FIN TCP flags if this is not the last
		   segment. */
		if (!last_segment)
			tcph->flags &= ~(0x8 | 0x1);
		nm_prdis("last_segment %u", last_segment);

		check = &tcph->check;
		check_data = (uint8_t *)tcph;
	} else { /* UDP */
		struct nm_udphdr *udph = (struct nm_udphdr *)(pkt + iphlen);

		/* Set the UDP 'Length' field. */
		udph->len = htobe16(len-iphlen);

		check = &udph->check;
		check_data = (uint8_t *)udph;
	}

	/* Compute and insert TCP/UDP checksum. */
	*check = 0;
	if (ipv4)
		nm_os_csum_tcpudp_ipv4(iph, check_data, len-iphlen, check);
	else
		nm_os_csum_tcpudp_ipv6(ip6h, check_data, len-iphlen, check);

	nm_prdis("TCP/UDP csum %x", be16toh(*check));
}

static inline int
vnet_hdr_is_bad(struct nm_vnet_hdr *vh)
{
	uint8_t gso_type = vh->gso_type & ~VIRTIO_NET_HDR_GSO_ECN;

	return (
		(gso_type != VIRTIO_NET_HDR_GSO_NONE &&
		 gso_type != VIRTIO_NET_HDR_GSO_TCPV4 &&
		 gso_type != VIRTIO_NET_HDR_GSO_UDP &&
		 gso_type != VIRTIO_NET_HDR_GSO_TCPV6)
		||
		 (vh->flags & ~(VIRTIO_NET_HDR_F_NEEDS_CSUM
			       | VIRTIO_NET_HDR_F_DATA_VALID))
	       );
}

/* The VALE mismatch datapath implementation. */
void
bdg_mismatch_datapath(struct netmap_vp_adapter *na,
		      struct netmap_vp_adapter *dst_na,
		      const struct nm_bdg_fwd *ft_p,
		      struct netmap_ring *dst_ring,
		      u_int *j, u_int lim, u_int *howmany)
{
	struct netmap_slot *dst_slot = NULL;
	struct nm_vnet_hdr *vh = NULL;
	const struct nm_bdg_fwd *ft_end = ft_p + ft_p->ft_frags;

	/* Source and destination pointers. */
	uint8_t *dst, *src;
	size_t src_len, dst_len;

	/* Indices and counters for the destination ring. */
	u_int j_start = *j;
	u_int j_cur = j_start;
	u_int dst_slots = 0;

	if (unlikely(ft_p == ft_end)) {
		nm_prlim(1, "No source slots to process");
		return;
	}

	/* Init source and dest pointers. */
	src = ft_p->ft_buf;
	src_len = ft_p->ft_len;
	dst_slot = &dst_ring->slot[j_cur];
	dst = NMB(&dst_na->up, dst_slot);
	dst_len = src_len;

	/* If the source port uses the offloadings, while destination doesn't,
	 * we grab the source virtio-net header and do the offloadings here.
	 */
	if (na->up.virt_hdr_len && !dst_na->up.virt_hdr_len) {
		vh = (struct nm_vnet_hdr *)src;
		/* Initial sanity check on the source virtio-net header. If
		 * something seems wrong, just drop the packet. */
		if (src_len < na->up.virt_hdr_len) {
			nm_prlim(1, "Short src vnet header, dropping");
			return;
		}
		if (unlikely(vnet_hdr_is_bad(vh))) {
			nm_prlim(1, "Bad src vnet header, dropping");
			return;
		}
	}

	/* We are processing the first input slot and there is a mismatch
	 * between source and destination virt_hdr_len (SHL and DHL).
	 * When the a client is using virtio-net headers, the header length
	 * can be:
	 *    - 10: the header corresponds to the struct nm_vnet_hdr
	 *    - 12: the first 10 bytes correspond to the struct
	 *          virtio_net_hdr, and the last 2 bytes store the
	 *          "mergeable buffers" info, which is an optional
	 *	    hint that can be zeroed for compatibility
	 *
	 * The destination header is therefore built according to the
	 * following table:
	 *
	 * SHL | DHL | destination header
	 * -----------------------------
	 *   0 |  10 | zero
	 *   0 |  12 | zero
	 *  10 |   0 | doesn't exist
	 *  10 |  12 | first 10 bytes are copied from source header, last 2 are zero
	 *  12 |   0 | doesn't exist
	 *  12 |  10 | copied from the first 10 bytes of source header
	 */
	bzero(dst, dst_na->up.virt_hdr_len);
	if (na->up.virt_hdr_len && dst_na->up.virt_hdr_len)
		memcpy(dst, src, sizeof(struct nm_vnet_hdr));
	/* Skip the virtio-net headers. */
	src += na->up.virt_hdr_len;
	src_len -= na->up.virt_hdr_len;
	dst += dst_na->up.virt_hdr_len;
	dst_len = dst_na->up.virt_hdr_len + src_len;

	/* Here it could be dst_len == 0 (which implies src_len == 0),
	 * so we avoid passing a zero length fragment.
	 */
	if (dst_len == 0) {
		ft_p++;
		src = ft_p->ft_buf;
		src_len = ft_p->ft_len;
		dst_len = src_len;
	}

	if (vh && vh->gso_type != VIRTIO_NET_HDR_GSO_NONE) {
		u_int gso_bytes = 0;
		/* Length of the GSO packet header. */
		u_int gso_hdr_len = 0;
		/* Pointer to the GSO packet header. Assume it is in a single fragment. */
		uint8_t *gso_hdr = NULL;
		/* Index of the current segment. */
		u_int gso_idx = 0;
		/* Payload data bytes segmented so far (e.g. TCP data bytes). */
		u_int segmented_bytes = 0;
		/* Is this an IPv4 or IPv6 GSO packet? */
		u_int ipv4 = 0;
		/* Length of the IP header (20 if IPv4, 40 if IPv6). */
		u_int iphlen = 0;
		/* Length of the Ethernet header (18 if 802.1q, otherwise 14). */
		u_int ethhlen = 14;
		/* Is this a TCP or an UDP GSO packet? */
		u_int tcp = ((vh->gso_type & ~VIRTIO_NET_HDR_GSO_ECN)
				== VIRTIO_NET_HDR_GSO_UDP) ? 0 : 1;

		/* Segment the GSO packet contained into the input slots (frags). */
		for (;;) {
			size_t copy;

			if (dst_slots >= *howmany) {
				/* We still have work to do, but we've run out of
				 * dst slots, so we have to drop the packet. */
				nm_prdis(1, "Not enough slots, dropping GSO packet");
				return;
			}

			/* Grab the GSO header if we don't have it. */
			if (!gso_hdr) {
				uint16_t ethertype;

				gso_hdr = src;

				/* Look at the 'Ethertype' field to see if this packet
				 * is IPv4 or IPv6, taking into account VLAN
				 * encapsulation. */
				for (;;) {
					if (src_len < ethhlen) {
						nm_prlim(1, "Short GSO fragment [eth], dropping");
						return;
					}
					ethertype = be16toh(*((uint16_t *)
							    (gso_hdr + ethhlen - 2)));
					if (ethertype != 0x8100) /* not 802.1q */
						break;
					ethhlen += 4;
				}
				switch (ethertype) {
					case 0x0800:  /* IPv4 */
					{
						struct nm_iphdr *iph = (struct nm_iphdr *)
									(gso_hdr + ethhlen);

						if (src_len < ethhlen + 20) {
							nm_prlim(1, "Short GSO fragment "
							      "[IPv4], dropping");
							return;
						}
						ipv4 = 1;
						iphlen = 4 * (iph->version_ihl & 0x0F);
						break;
					}
					case 0x86DD:  /* IPv6 */
						ipv4 = 0;
						iphlen = 40;
						break;
					default:
						nm_prlim(1, "Unsupported ethertype, "
						      "dropping GSO packet");
						return;
				}
				nm_prdis(3, "type=%04x", ethertype);

				if (src_len < ethhlen + iphlen) {
					nm_prlim(1, "Short GSO fragment [IP], dropping");
					return;
				}

				/* Compute gso_hdr_len. For TCP we need to read the
				 * content of the 'Data Offset' field.
				 */
				if (tcp) {
					struct nm_tcphdr *tcph = (struct nm_tcphdr *)
								(gso_hdr + ethhlen + iphlen);

					if (src_len < ethhlen + iphlen + 20) {
						nm_prlim(1, "Short GSO fragment "
								"[TCP], dropping");
						return;
					}
					gso_hdr_len = ethhlen + iphlen +
						      4 * (tcph->doff >> 4);
				} else {
					gso_hdr_len = ethhlen + iphlen + 8; /* UDP */
				}

				if (src_len < gso_hdr_len) {
					nm_prlim(1, "Short GSO fragment [TCP/UDP], dropping");
					return;
				}

				nm_prdis(3, "gso_hdr_len %u gso_mtu %d", gso_hdr_len,
								   dst_na->mfs);

				/* Advance source pointers. */
				src += gso_hdr_len;
				src_len -= gso_hdr_len;
				if (src_len == 0) {
					ft_p++;
					if (ft_p == ft_end)
						break;
					src = ft_p->ft_buf;
					src_len = ft_p->ft_len;
				}
			}

			/* Fill in the header of the current segment. */
			if (gso_bytes == 0) {
				memcpy(dst, gso_hdr, gso_hdr_len);
				gso_bytes = gso_hdr_len;
			}

			/* Fill in data and update source and dest pointers. */
			copy = src_len;
			if (gso_bytes + copy > dst_na->mfs)
				copy = dst_na->mfs - gso_bytes;
			memcpy(dst + gso_bytes, src, copy);
			gso_bytes += copy;
			src += copy;
			src_len -= copy;

			/* A segment is complete or we have processed all the
			   the GSO payload bytes. */
			if (gso_bytes >= dst_na->mfs ||
				(src_len == 0 && ft_p + 1 == ft_end)) {
				/* After raw segmentation, we must fix some header
				 * fields and compute checksums, in a protocol dependent
				 * way. */
				gso_fix_segment(dst + ethhlen, gso_bytes - ethhlen,
						ipv4, iphlen, tcp,
						gso_idx, segmented_bytes,
						src_len == 0 && ft_p + 1 == ft_end);

				nm_prdis("frame %u completed with %d bytes", gso_idx, (int)gso_bytes);
				dst_slot->len = gso_bytes;
				dst_slot->flags = 0;
				dst_slots++;
				segmented_bytes += gso_bytes - gso_hdr_len;

				gso_bytes = 0;
				gso_idx++;

				/* Next destination slot. */
				j_cur = nm_next(j_cur, lim);
				dst_slot = &dst_ring->slot[j_cur];
				dst = NMB(&dst_na->up, dst_slot);
			}

			/* Next input slot. */
			if (src_len == 0) {
				ft_p++;
				if (ft_p == ft_end)
					break;
				src = ft_p->ft_buf;
				src_len = ft_p->ft_len;
			}
		}
		nm_prdis(3, "%d bytes segmented", segmented_bytes);

	} else {
		/* Address of a checksum field into a destination slot. */
		uint16_t *check = NULL;
		/* Accumulator for an unfolded checksum. */
		rawsum_t csum = 0;

		/* Process a non-GSO packet. */

		/* Init 'check' if necessary. */
		if (vh && (vh->flags & VIRTIO_NET_HDR_F_NEEDS_CSUM)) {
			if (unlikely(vh->csum_offset + vh->csum_start > src_len))
				nm_prerr("invalid checksum request");
			else
				check = (uint16_t *)(dst + vh->csum_start +
						vh->csum_offset);
		}

		while (ft_p != ft_end) {
			/* Init/update the packet checksum if needed. */
			if (vh && (vh->flags & VIRTIO_NET_HDR_F_NEEDS_CSUM)) {
				if (!dst_slots)
					csum = nm_os_csum_raw(src + vh->csum_start,
								src_len - vh->csum_start, 0);
				else
					csum = nm_os_csum_raw(src, src_len, csum);
			}

			/* Round to a multiple of 64 */
			src_len = (src_len + 63) & ~63;

			if (ft_p->ft_flags & NS_INDIRECT) {
				if (copyin(src, dst, src_len)) {
					/* Invalid user pointer, pretend len is 0. */
					dst_len = 0;
				}
			} else {
				memcpy(dst, src, (int)src_len);
			}
			dst_slot->len = dst_len;
			dst_slots++;

			/* Next destination slot. */
			j_cur = nm_next(j_cur, lim);
			dst_slot = &dst_ring->slot[j_cur];
			dst = NMB(&dst_na->up, dst_slot);

			/* Next source slot. */
			ft_p++;
			src = ft_p->ft_buf;
			dst_len = src_len = ft_p->ft_len;
		}

		/* Finalize (fold) the checksum if needed. */
		if (check && vh && (vh->flags & VIRTIO_NET_HDR_F_NEEDS_CSUM)) {
			*check = nm_os_csum_fold(csum);
		}
		nm_prdis(3, "using %u dst_slots", dst_slots);

		/* A second pass on the destination slots to set the slot flags,
		 * using the right number of destination slots.
		 */
		while (j_start != j_cur) {
			dst_slot = &dst_ring->slot[j_start];
			dst_slot->flags = (dst_slots << 8)| NS_MOREFRAG;
			j_start = nm_next(j_start, lim);
		}
		/* Clear NS_MOREFRAG flag on last entry. */
		dst_slot->flags = (dst_slots << 8);
	}

	/* Update howmany and j. This is to commit the use of
	 * those slots in the destination ring. */
	if (unlikely(dst_slots > *howmany)) {
		nm_prerr("bug: slot allocation error");
	}
	*j = j_cur;
	*howmany -= dst_slots;
}
