/*-
 * search.c
 *
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2001-2003 Maksim Yevmenkin <m_evmenkin@yahoo.com>
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
 * ARE DISCLAIMED. IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $Id: search.c,v 1.2 2003/09/04 22:12:13 max Exp $
 * $FreeBSD$
 */

#include <sys/uio.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#define L2CAP_SOCKET_CHECKED
#include <bluetooth.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <sdp-int.h>
#include <sdp.h>

int32_t
sdp_search(void *xss,
		uint32_t plen, uint16_t const *pp,
		uint32_t alen, uint32_t const *ap,
		uint32_t vlen, sdp_attr_t *vp)
{
	struct sdp_xpdu {
		sdp_pdu_t		 pdu;
		uint16_t		 len;
	} __attribute__ ((packed))	 xpdu;

	sdp_session_p			 ss = (sdp_session_p) xss;
	uint8_t				*req = NULL, *rsp = NULL, *rsp_tmp = NULL;
	int32_t				 t, len;
	uint16_t			 lo, hi;

	if (ss == NULL)
		return (-1);

	if (ss->req == NULL || ss->rsp == NULL ||
	    plen == 0 || pp == NULL || alen == 0 || ap == NULL) {
		ss->error = EINVAL;
		return (-1);
	}

	req = ss->req;

	/* Calculate ServiceSearchPattern length */
	plen = plen * (sizeof(pp[0]) + 1);

	/* Calculate AttributeIDList length */
	for (len = 0, t = 0; t < alen; t ++) {
		lo = (uint16_t) (ap[t] >> 16);
		hi = (uint16_t) (ap[t]);

		if (lo > hi) {
			ss->error = EINVAL;
			return (-1);
		}

		if (lo != hi)
			len += (sizeof(ap[t]) + 1);
		else
			len += (sizeof(lo) + 1);
	}
	alen = len;

	/* Calculate length of the request */
	len =	plen + sizeof(uint8_t) + sizeof(uint16_t) +
			/* ServiceSearchPattern */
		sizeof(uint16_t) +
			/* MaximumAttributeByteCount */
		alen + sizeof(uint8_t) + sizeof(uint16_t);
			/* AttributeIDList */

	if (ss->req_e - req < len) {
		ss->error = ENOBUFS;
		return (-1);
	}
		
	/* Put ServiceSearchPattern */
	SDP_PUT8(SDP_DATA_SEQ16, req);
	SDP_PUT16(plen, req);
	for (; plen > 0; pp ++, plen -= (sizeof(pp[0]) + 1)) {
		SDP_PUT8(SDP_DATA_UUID16, req);
		SDP_PUT16(*pp, req);
	}

	/* Put MaximumAttributeByteCount */
	SDP_PUT16(0xffff, req);

	/* Put AttributeIDList */
	SDP_PUT8(SDP_DATA_SEQ16, req);
	SDP_PUT16(alen, req);
	for (; alen > 0; ap ++) {
		lo = (uint16_t) (*ap >> 16);
		hi = (uint16_t) (*ap);

		if (lo != hi) {
			/* Put attribute range */
			SDP_PUT8(SDP_DATA_UINT32, req);
			SDP_PUT32(*ap, req);
			alen -= (sizeof(ap[0]) + 1);
		} else {
			/* Put attribute */
			SDP_PUT8(SDP_DATA_UINT16, req);
			SDP_PUT16(lo, req);
			alen -= (sizeof(lo) + 1);
		}
	}

	/* Submit ServiceSearchAttributeRequest and wait for response */
	ss->cslen = 0;
	rsp = ss->rsp;

	do {
		struct iovec	 iov[2];
		uint8_t		*req_cs = req;

		/* Add continuation state (if any) */
		if (ss->req_e - req_cs < ss->cslen + 1) {
			ss->error = ENOBUFS;
			return (-1);
		}

		SDP_PUT8(ss->cslen, req_cs);
		if (ss->cslen > 0) {
			memcpy(req_cs, ss->cs, ss->cslen);
			req_cs += ss->cslen;
		}

		/* Prepare SDP PDU header */
		xpdu.pdu.pid = SDP_PDU_SERVICE_SEARCH_ATTRIBUTE_REQUEST;
		xpdu.pdu.tid = htons(ss->tid);
		xpdu.pdu.len = htons(req_cs - ss->req);

		/* Submit request */
		iov[0].iov_base = (void *) &xpdu;
		iov[0].iov_len = sizeof(xpdu.pdu);
		iov[1].iov_base = (void *) ss->req;
		iov[1].iov_len = req_cs - ss->req;

		do {
			len = writev(ss->s, iov, sizeof(iov)/sizeof(iov[0]));
		} while (len < 0 && errno == EINTR);

		if (len < 0) {
			ss->error = errno;
			return (-1);
		}

		/* Read response */
		iov[0].iov_base = (void *) &xpdu;
		iov[0].iov_len = sizeof(xpdu);
		iov[1].iov_base = (void *) rsp;
		iov[1].iov_len = ss->imtu;

		do {
			len = readv(ss->s, iov, sizeof(iov)/sizeof(iov[0]));
		} while (len < 0 && errno == EINTR);

		if (len < 0) {
			ss->error = errno;
			return (-1);
		}
		if (len < sizeof(xpdu)) {
			ss->error = ENOMSG;
			return (-1);
		}

		xpdu.pdu.tid = ntohs(xpdu.pdu.tid);
		xpdu.pdu.len = ntohs(xpdu.pdu.len);
		xpdu.len = ntohs(xpdu.len);

		if (xpdu.pdu.pid == SDP_PDU_ERROR_RESPONSE ||
		    xpdu.pdu.tid != ss->tid ||
		    xpdu.pdu.len > len ||
		    xpdu.len > xpdu.pdu.len) {
			ss->error = EIO;
			return (-1);
		}

		rsp += xpdu.len;
		ss->tid ++;

		/* Save continuation state (if any) */
		ss->cslen = rsp[0];
		if (ss->cslen > 0) {
			if (ss->cslen > sizeof(ss->cs)) {
				ss->error = ENOBUFS;
				return (-1);
			}

			memcpy(ss->cs, rsp + 1, ss->cslen);

			/*
			 * Ensure that we always have ss->imtu bytes
			 * available in the ss->rsp buffer
			 */

			if (ss->rsp_e - rsp <= ss->imtu) {
				uint32_t	 size, offset;

				size = ss->rsp_e - ss->rsp + ss->imtu;
				offset = rsp - ss->rsp;
		
				rsp_tmp = realloc(ss->rsp, size);
				if (rsp_tmp == NULL) {
					ss->error = ENOMEM;
					return (-1);
				}

				ss->rsp = rsp_tmp;
				ss->rsp_e = ss->rsp + size;
				rsp = ss->rsp + offset;
			}
		}
	} while (ss->cslen > 0);

	/*
	 * If we got here then we have completed SDP transaction and now
	 * we must populate attribute values into vp array. At this point
	 * ss->rsp points to the beginning of the response and rsp points
	 * to the end of the response.
	 * 
	 * From Bluetooth v1.1 spec page 364
	 * 
	 * The AttributeLists is a data element sequence where each element
	 * in turn is a data element sequence representing an attribute list.
	 * Each attribute list contains attribute IDs and attribute values 
	 * from one service record. The first element in each attribute list
	 * contains the attribute ID of the first attribute to be returned for
	 * that service record. The second element in each attribute list
	 * contains the corresponding attribute value. Successive pairs of
	 * elements in each attribute list contain additional attribute ID
	 * and value pairs. Only attributes that have non-null values within
	 * the service record and whose attribute IDs were specified in the
	 * SDP_ServiceSearchAttributeRequest are contained in the AttributeLists
	 * Neither an attribute ID nor attribute value is placed in 
	 * AttributeLists for attributes in the service record that have no 
	 * value. Within each attribute list, the attributes are listed in 
	 * ascending order of attribute ID value.
	 */

	if (vp == NULL)
		goto done;

	rsp_tmp = ss->rsp;

	/* Skip the first SEQ */
	SDP_GET8(t, rsp_tmp);
	switch (t) {
	case SDP_DATA_SEQ8:
		SDP_GET8(len, rsp_tmp);
		break;

	case SDP_DATA_SEQ16:
		SDP_GET16(len, rsp_tmp);
		break;

	case SDP_DATA_SEQ32:
		SDP_GET32(len, rsp_tmp);
		break;

	default:
		ss->error = ENOATTR;
		return (-1);
		/* NOT REACHED */
	}

	for (; rsp_tmp < rsp && vlen > 0; ) {
		/* Get set of attributes for the next record */
		SDP_GET8(t, rsp_tmp);
		switch (t) {
		case SDP_DATA_SEQ8:
			SDP_GET8(len, rsp_tmp);
			break;

		case SDP_DATA_SEQ16:
			SDP_GET16(len, rsp_tmp);
			break;

		case SDP_DATA_SEQ32:
			SDP_GET32(len, rsp_tmp);
			break;

		default:
			ss->error = ENOATTR;
			return (-1);
			/* NOT REACHED */
		}

		/* Now rsp_tmp points to list of (attr,value) pairs */
		for (; len > 0 && vlen > 0; vp ++, vlen --) {
			/* Attribute */
			SDP_GET8(t, rsp_tmp);
			if (t != SDP_DATA_UINT16) {
				ss->error = ENOATTR;
				return (-1);
			}
			SDP_GET16(vp->attr, rsp_tmp);

			/* Attribute value */
			switch (rsp_tmp[0]) {
			case SDP_DATA_NIL:
				alen = 0;
				break;

			case SDP_DATA_UINT8:
			case SDP_DATA_INT8:
			case SDP_DATA_BOOL:
				alen = sizeof(uint8_t);
				break;

			case SDP_DATA_UINT16:
			case SDP_DATA_INT16:
			case SDP_DATA_UUID16:
				alen = sizeof(uint16_t);
				break;

			case SDP_DATA_UINT32:
			case SDP_DATA_INT32:
			case SDP_DATA_UUID32:
				alen = sizeof(uint32_t);
				break;

			case SDP_DATA_UINT64:
			case SDP_DATA_INT64:
				alen = sizeof(uint64_t);
				break;

			case SDP_DATA_UINT128:
			case SDP_DATA_INT128:
			case SDP_DATA_UUID128:
				alen = sizeof(uint128_t);
				break;

			case SDP_DATA_STR8:
			case SDP_DATA_URL8:
			case SDP_DATA_SEQ8:
			case SDP_DATA_ALT8:
				alen = rsp_tmp[1] + sizeof(uint8_t);
				break;

			case SDP_DATA_STR16:
			case SDP_DATA_URL16:
			case SDP_DATA_SEQ16:
			case SDP_DATA_ALT16:
				alen =	  ((uint16_t)rsp_tmp[1] << 8)
					| ((uint16_t)rsp_tmp[2]);
				alen += sizeof(uint16_t);
				break;

			case SDP_DATA_STR32:
			case SDP_DATA_URL32:
			case SDP_DATA_SEQ32:
			case SDP_DATA_ALT32:
				alen =    ((uint32_t)rsp_tmp[1] << 24)
					| ((uint32_t)rsp_tmp[2] << 16)
					| ((uint32_t)rsp_tmp[3] <<  8)
					| ((uint32_t)rsp_tmp[4]);
				alen += sizeof(uint32_t);
				break;

			default:
				ss->error = ENOATTR;
				return (-1);
				/* NOT REACHED */
			}

			alen += sizeof(uint8_t);

			if (vp->value != NULL) {
				if (alen <= vp->vlen) {
					vp->flags = SDP_ATTR_OK;
					vp->vlen = alen;
				} else
					vp->flags = SDP_ATTR_TRUNCATED;

				memcpy(vp->value, rsp_tmp, vp->vlen);
			} else
				vp->flags = SDP_ATTR_INVALID;

			len -=	(
				sizeof(uint8_t) + sizeof(uint16_t) +
				alen
				);

			rsp_tmp += alen;
		}
	}
done:
	ss->error = 0;

	return (0);
}

