/* $FreeBSD$ */
/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2008 Hans Petter Selasky. All rights reserved.
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
 */

#ifdef LIBUSB_GLOBAL_INCLUDE_FILE
#include LIBUSB_GLOBAL_INCLUDE_FILE
#else
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/queue.h>
#endif

#include "libusb20.h"
#include "libusb20_desc.h"
#include "libusb20_int.h"

static const uint32_t libusb20_me_encode_empty[2];	/* dummy */

LIBUSB20_MAKE_STRUCT_FORMAT(LIBUSB20_DEVICE_DESC);
LIBUSB20_MAKE_STRUCT_FORMAT(LIBUSB20_ENDPOINT_DESC);
LIBUSB20_MAKE_STRUCT_FORMAT(LIBUSB20_INTERFACE_DESC);
LIBUSB20_MAKE_STRUCT_FORMAT(LIBUSB20_CONFIG_DESC);
LIBUSB20_MAKE_STRUCT_FORMAT(LIBUSB20_CONTROL_SETUP);
LIBUSB20_MAKE_STRUCT_FORMAT(LIBUSB20_SS_ENDPT_COMP_DESC);
LIBUSB20_MAKE_STRUCT_FORMAT(LIBUSB20_USB_20_DEVCAP_DESC);
LIBUSB20_MAKE_STRUCT_FORMAT(LIBUSB20_SS_USB_DEVCAP_DESC);
LIBUSB20_MAKE_STRUCT_FORMAT(LIBUSB20_BOS_DESCRIPTOR);

/*------------------------------------------------------------------------*
 *	libusb20_parse_config_desc
 *
 * Return values:
 * NULL: Out of memory.
 * Else: A valid config structure pointer which must be passed to "free()"
 *------------------------------------------------------------------------*/
struct libusb20_config *
libusb20_parse_config_desc(const void *config_desc)
{
	struct libusb20_config *lub_config;
	struct libusb20_interface *lub_interface;
	struct libusb20_interface *lub_alt_interface;
	struct libusb20_interface *last_if;
	struct libusb20_endpoint *lub_endpoint;
	struct libusb20_endpoint *last_ep;

	struct libusb20_me_struct pcdesc;
	const uint8_t *ptr;
	uint32_t size;
	uint16_t niface_no_alt;
	uint16_t niface;
	uint16_t nendpoint;
	uint16_t iface_no;

	ptr = config_desc;
	if (ptr[1] != LIBUSB20_DT_CONFIG) {
		return (NULL);		/* not config descriptor */
	}
	/*
	 * The first "bInterfaceNumber" should never have the value 0xff.
	 * Then it is corrupt.
	 */
	niface_no_alt = 0;
	nendpoint = 0;
	niface = 0;
	iface_no = 0xFFFF;
	ptr = NULL;

	/* get "wTotalLength" and setup "pcdesc" */
	pcdesc.ptr = LIBUSB20_ADD_BYTES(config_desc, 0);
	pcdesc.len =
	    ((const uint8_t *)config_desc)[2] |
	    (((const uint8_t *)config_desc)[3] << 8);
	pcdesc.type = LIBUSB20_ME_IS_RAW;

	/* descriptor pre-scan */
	while ((ptr = libusb20_desc_foreach(&pcdesc, ptr))) {
		if (ptr[1] == LIBUSB20_DT_ENDPOINT) {
			nendpoint++;
		} else if ((ptr[1] == LIBUSB20_DT_INTERFACE) && (ptr[0] >= 4)) {
			niface++;
			/* check "bInterfaceNumber" */
			if (ptr[2] != iface_no) {
				iface_no = ptr[2];
				niface_no_alt++;
			}
		}
	}

	/* sanity checking */
	if (niface >= 256) {
		return (NULL);		/* corrupt */
	}
	if (nendpoint >= 256) {
		return (NULL);		/* corrupt */
	}
	size = sizeof(*lub_config) +
	    (niface * sizeof(*lub_interface)) +
	    (nendpoint * sizeof(*lub_endpoint)) +
	    pcdesc.len;

	lub_config = malloc(size);
	if (lub_config == NULL) {
		return (NULL);		/* out of memory */
	}
	/* make sure memory is initialised */
	memset(lub_config, 0, size);

	lub_interface = (void *)(lub_config + 1);
	lub_alt_interface = (void *)(lub_interface + niface_no_alt);
	lub_endpoint = (void *)(lub_interface + niface);

	/*
	 * Make a copy of the config descriptor, so that the caller can free
	 * the initial config descriptor pointer!
	 */
	memcpy((void *)(lub_endpoint + nendpoint), config_desc, pcdesc.len);

	ptr = (const void *)(lub_endpoint + nendpoint);
	pcdesc.ptr = LIBUSB20_ADD_BYTES(ptr, 0);

	/* init config structure */

	LIBUSB20_INIT(LIBUSB20_CONFIG_DESC, &lub_config->desc);

	if (libusb20_me_decode(ptr, ptr[0], &lub_config->desc)) {
		/* ignore */
	}
	lub_config->num_interface = 0;
	lub_config->interface = lub_interface;
	lub_config->extra.ptr = LIBUSB20_ADD_BYTES(ptr, ptr[0]);
	lub_config->extra.len = -ptr[0];
	lub_config->extra.type = LIBUSB20_ME_IS_RAW;

	/* reset states */
	niface = 0;
	iface_no = 0xFFFF;
	ptr = NULL;
	lub_interface--;
	lub_endpoint--;
	last_if = NULL;
	last_ep = NULL;

	/* descriptor pre-scan */
	while ((ptr = libusb20_desc_foreach(&pcdesc, ptr))) {
		if (ptr[1] == LIBUSB20_DT_ENDPOINT) {
			if (last_if) {
				lub_endpoint++;
				last_ep = lub_endpoint;
				last_if->num_endpoints++;

				LIBUSB20_INIT(LIBUSB20_ENDPOINT_DESC, &last_ep->desc);

				if (libusb20_me_decode(ptr, ptr[0], &last_ep->desc)) {
					/* ignore */
				}
				last_ep->extra.ptr = LIBUSB20_ADD_BYTES(ptr, ptr[0]);
				last_ep->extra.len = 0;
				last_ep->extra.type = LIBUSB20_ME_IS_RAW;
			} else {
				lub_config->extra.len += ptr[0];
			}

		} else if ((ptr[1] == LIBUSB20_DT_INTERFACE) && (ptr[0] >= 4)) {
			if (ptr[2] != iface_no) {
				/* new interface */
				iface_no = ptr[2];
				lub_interface++;
				lub_config->num_interface++;
				last_if = lub_interface;
				niface++;
			} else {
				/* one more alternate setting */
				lub_interface->num_altsetting++;
				last_if = lub_alt_interface;
				lub_alt_interface++;
			}

			LIBUSB20_INIT(LIBUSB20_INTERFACE_DESC, &last_if->desc);

			if (libusb20_me_decode(ptr, ptr[0], &last_if->desc)) {
				/* ignore */
			}
			/*
			 * Sometimes USB devices have corrupt interface
			 * descriptors and we need to overwrite the provided
			 * interface number!
			 */
			last_if->desc.bInterfaceNumber = niface - 1;
			last_if->extra.ptr = LIBUSB20_ADD_BYTES(ptr, ptr[0]);
			last_if->extra.len = 0;
			last_if->extra.type = LIBUSB20_ME_IS_RAW;
			last_if->endpoints = lub_endpoint + 1;
			last_if->altsetting = lub_alt_interface;
			last_if->num_altsetting = 0;
			last_if->num_endpoints = 0;
			last_ep = NULL;
		} else {
			/* unknown descriptor */
			if (last_if) {
				if (last_ep) {
					last_ep->extra.len += ptr[0];
				} else {
					last_if->extra.len += ptr[0];
				}
			} else {
				lub_config->extra.len += ptr[0];
			}
		}
	}
	return (lub_config);
}

/*------------------------------------------------------------------------*
 *	libusb20_desc_foreach
 *
 * Safe traversal of USB descriptors.
 *
 * Return values:
 * NULL: End of descriptors
 * Else: Pointer to next descriptor
 *------------------------------------------------------------------------*/
const uint8_t *
libusb20_desc_foreach(const struct libusb20_me_struct *pdesc,
    const uint8_t *psubdesc)
{
	const uint8_t *start;
	const uint8_t *end;
	const uint8_t *desc_next;

	/* be NULL safe */
	if (pdesc == NULL)
		return (NULL);

	start = (const uint8_t *)pdesc->ptr;
	end = LIBUSB20_ADD_BYTES(start, pdesc->len);

	/* get start of next descriptor */
	if (psubdesc == NULL)
		psubdesc = start;
	else
		psubdesc = psubdesc + psubdesc[0];

	/* check that the next USB descriptor is within the range */
	if ((psubdesc < start) || (psubdesc >= end))
		return (NULL);		/* out of range, or EOD */

	/* check start of the second next USB descriptor, if any */
	desc_next = psubdesc + psubdesc[0];
	if ((desc_next < start) || (desc_next > end))
		return (NULL);		/* out of range */

	/* check minimum descriptor length */
	if (psubdesc[0] < 3)
		return (NULL);		/* too short descriptor */

	return (psubdesc);		/* return start of next descriptor */
}

/*------------------------------------------------------------------------*
 *	libusb20_me_get_1 - safety wrapper to read out one byte
 *------------------------------------------------------------------------*/
uint8_t
libusb20_me_get_1(const struct libusb20_me_struct *ie, uint16_t offset)
{
	if (offset < ie->len) {
		return (*((uint8_t *)LIBUSB20_ADD_BYTES(ie->ptr, offset)));
	}
	return (0);
}

/*------------------------------------------------------------------------*
 *	libusb20_me_get_2 - safety wrapper to read out one word
 *------------------------------------------------------------------------*/
uint16_t
libusb20_me_get_2(const struct libusb20_me_struct *ie, uint16_t offset)
{
	return (libusb20_me_get_1(ie, offset) |
	    (libusb20_me_get_1(ie, offset + 1) << 8));
}

/*------------------------------------------------------------------------*
 *	libusb20_me_encode - encode a message structure
 *
 * Description of parameters:
 * "len" - maximum length of output buffer
 * "ptr" - pointer to output buffer. If NULL, no data will be written
 * "pd" - source structure
 *
 * Return values:
 * 0..65535 - Number of bytes used, limited by the "len" input parameter.
 *------------------------------------------------------------------------*/
uint16_t
libusb20_me_encode(void *ptr, uint16_t len, const void *pd)
{
	const uint8_t *pf;		/* pointer to format data */
	uint8_t *buf;			/* pointer to output buffer */

	uint32_t pd_offset;		/* decoded structure offset */
	uint16_t len_old;		/* old length */
	uint16_t pd_count;		/* decoded element count */
	uint8_t me;			/* message element */

	/* initialise */

	len_old = len;
	buf = ptr;
	pd_offset = sizeof(void *);
	pf = (*((struct libusb20_me_format *const *)pd))->format;

	/* scan */

	while (1) {

		/* get information element */

		me = (pf[0]) & LIBUSB20_ME_MASK;
		pd_count = pf[1] | (pf[2] << 8);
		pf += 3;

		/* encode the message element */

		switch (me) {
		case LIBUSB20_ME_INT8:
			while (pd_count--) {
				uint8_t temp;

				if (len < 1)	/* overflow */
					goto done;
				if (buf) {
					temp = *((const uint8_t *)
					    LIBUSB20_ADD_BYTES(pd, pd_offset));
					buf[0] = temp;
					buf += 1;
				}
				pd_offset += 1;
				len -= 1;
			}
			break;

		case LIBUSB20_ME_INT16:
			pd_offset = -((-pd_offset) & ~1);	/* align */
			while (pd_count--) {
				uint16_t temp;

				if (len < 2)	/* overflow */
					goto done;

				if (buf) {
					temp = *((const uint16_t *)
					    LIBUSB20_ADD_BYTES(pd, pd_offset));
					buf[1] = (temp >> 8) & 0xFF;
					buf[0] = temp & 0xFF;
					buf += 2;
				}
				pd_offset += 2;
				len -= 2;
			}
			break;

		case LIBUSB20_ME_INT32:
			pd_offset = -((-pd_offset) & ~3);	/* align */
			while (pd_count--) {
				uint32_t temp;

				if (len < 4)	/* overflow */
					goto done;
				if (buf) {
					temp = *((const uint32_t *)
					    LIBUSB20_ADD_BYTES(pd, pd_offset));
					buf[3] = (temp >> 24) & 0xFF;
					buf[2] = (temp >> 16) & 0xFF;
					buf[1] = (temp >> 8) & 0xFF;
					buf[0] = temp & 0xFF;
					buf += 4;
				}
				pd_offset += 4;
				len -= 4;
			}
			break;

		case LIBUSB20_ME_INT64:
			pd_offset = -((-pd_offset) & ~7);	/* align */
			while (pd_count--) {
				uint64_t temp;

				if (len < 8)	/* overflow */
					goto done;
				if (buf) {

					temp = *((const uint64_t *)
					    LIBUSB20_ADD_BYTES(pd, pd_offset));
					buf[7] = (temp >> 56) & 0xFF;
					buf[6] = (temp >> 48) & 0xFF;
					buf[5] = (temp >> 40) & 0xFF;
					buf[4] = (temp >> 32) & 0xFF;
					buf[3] = (temp >> 24) & 0xFF;
					buf[2] = (temp >> 16) & 0xFF;
					buf[1] = (temp >> 8) & 0xFF;
					buf[0] = temp & 0xFF;
					buf += 8;
				}
				pd_offset += 8;
				len -= 8;
			}
			break;

		case LIBUSB20_ME_STRUCT:
			pd_offset = -((-pd_offset) &
			    ~(LIBUSB20_ME_STRUCT_ALIGN - 1));	/* align */
			while (pd_count--) {
				void *src_ptr;
				uint16_t src_len;
				struct libusb20_me_struct *ps;

				ps = LIBUSB20_ADD_BYTES(pd, pd_offset);

				switch (ps->type) {
				case LIBUSB20_ME_IS_RAW:
					src_len = ps->len;
					src_ptr = ps->ptr;
					break;

				case LIBUSB20_ME_IS_ENCODED:
					if (ps->len == 0) {
						/*
						 * Length is encoded
						 * in the data itself
						 * and should be
						 * correct:
						 */
						ps->len = 0xFFFF;
					}
					src_len = libusb20_me_get_1(pd, 0);
					src_ptr = LIBUSB20_ADD_BYTES(ps->ptr, 1);
					if (src_len == 0xFF) {
						/* length is escaped */
						src_len = libusb20_me_get_2(pd, 1);
						src_ptr =
						    LIBUSB20_ADD_BYTES(ps->ptr, 3);
					}
					break;

				case LIBUSB20_ME_IS_DECODED:
					/* reserve 3 length bytes */
					src_len = libusb20_me_encode(NULL,
					    0xFFFF - 3, ps->ptr);
					src_ptr = NULL;
					break;

				default:	/* empty structure */
					src_len = 0;
					src_ptr = NULL;
					break;
				}

				if (src_len > 0xFE) {
					if (src_len > (0xFFFF - 3))
						/* overflow */
						goto done;

					if (len < (src_len + 3))
						/* overflow */
						goto done;

					if (buf) {
						buf[0] = 0xFF;
						buf[1] = (src_len & 0xFF);
						buf[2] = (src_len >> 8) & 0xFF;
						buf += 3;
					}
					len -= (src_len + 3);
				} else {
					if (len < (src_len + 1))
						/* overflow */
						goto done;

					if (buf) {
						buf[0] = (src_len & 0xFF);
						buf += 1;
					}
					len -= (src_len + 1);
				}

				/* check for buffer and non-zero length */

				if (buf && src_len) {
					if (ps->type == LIBUSB20_ME_IS_DECODED) {
						/*
						 * Repeat encode
						 * procedure - we have
						 * room for the
						 * complete structure:
						 */
						(void) libusb20_me_encode(buf,
						    0xFFFF - 3, ps->ptr);
					} else {
						bcopy(src_ptr, buf, src_len);
					}
					buf += src_len;
				}
				pd_offset += sizeof(struct libusb20_me_struct);
			}
			break;

		default:
			goto done;
		}
	}
done:
	return (len_old - len);
}

/*------------------------------------------------------------------------*
 *	libusb20_me_decode - decode a message into a decoded structure
 *
 * Description of parameters:
 * "ptr" - message pointer
 * "len" - message length
 * "pd" - pointer to decoded structure
 *
 * Returns:
 * "0..65535" - number of bytes decoded, limited by "len"
 *------------------------------------------------------------------------*/
uint16_t
libusb20_me_decode(const void *ptr, uint16_t len, void *pd)
{
	const uint8_t *pf;		/* pointer to format data */
	const uint8_t *buf;		/* pointer to input buffer */

	uint32_t pd_offset;		/* decoded structure offset */
	uint16_t len_old;		/* old length */
	uint16_t pd_count;		/* decoded element count */
	uint8_t me;			/* message element */

	/* initialise */

	len_old = len;
	buf = ptr;
	pd_offset = sizeof(void *);
	pf = (*((struct libusb20_me_format **)pd))->format;

	/* scan */

	while (1) {

		/* get information element */

		me = (pf[0]) & LIBUSB20_ME_MASK;
		pd_count = pf[1] | (pf[2] << 8);
		pf += 3;

		/* decode the message element by type */

		switch (me) {
		case LIBUSB20_ME_INT8:
			while (pd_count--) {
				uint8_t temp;

				if (len < 1) {
					len = 0;
					temp = 0;
				} else {
					len -= 1;
					temp = buf[0];
					buf++;
				}
				*((uint8_t *)LIBUSB20_ADD_BYTES(pd,
				    pd_offset)) = temp;
				pd_offset += 1;
			}
			break;

		case LIBUSB20_ME_INT16:
			pd_offset = -((-pd_offset) & ~1);	/* align */
			while (pd_count--) {
				uint16_t temp;

				if (len < 2) {
					len = 0;
					temp = 0;
				} else {
					len -= 2;
					temp = buf[1] << 8;
					temp |= buf[0];
					buf += 2;
				}
				*((uint16_t *)LIBUSB20_ADD_BYTES(pd,
				    pd_offset)) = temp;
				pd_offset += 2;
			}
			break;

		case LIBUSB20_ME_INT32:
			pd_offset = -((-pd_offset) & ~3);	/* align */
			while (pd_count--) {
				uint32_t temp;

				if (len < 4) {
					len = 0;
					temp = 0;
				} else {
					len -= 4;
					temp = buf[3] << 24;
					temp |= buf[2] << 16;
					temp |= buf[1] << 8;
					temp |= buf[0];
					buf += 4;
				}

				*((uint32_t *)LIBUSB20_ADD_BYTES(pd,
				    pd_offset)) = temp;
				pd_offset += 4;
			}
			break;

		case LIBUSB20_ME_INT64:
			pd_offset = -((-pd_offset) & ~7);	/* align */
			while (pd_count--) {
				uint64_t temp;

				if (len < 8) {
					len = 0;
					temp = 0;
				} else {
					len -= 8;
					temp = ((uint64_t)buf[7]) << 56;
					temp |= ((uint64_t)buf[6]) << 48;
					temp |= ((uint64_t)buf[5]) << 40;
					temp |= ((uint64_t)buf[4]) << 32;
					temp |= buf[3] << 24;
					temp |= buf[2] << 16;
					temp |= buf[1] << 8;
					temp |= buf[0];
					buf += 8;
				}

				*((uint64_t *)LIBUSB20_ADD_BYTES(pd,
				    pd_offset)) = temp;
				pd_offset += 8;
			}
			break;

		case LIBUSB20_ME_STRUCT:
			pd_offset = -((-pd_offset) &
			    ~(LIBUSB20_ME_STRUCT_ALIGN - 1));	/* align */
			while (pd_count--) {
				uint16_t temp;
				struct libusb20_me_struct *ps;

				ps = LIBUSB20_ADD_BYTES(pd, pd_offset);

				if (ps->type == LIBUSB20_ME_IS_ENCODED) {
					/*
					 * Pre-store a de-constified
					 * pointer to the raw
					 * structure:
					 */
					ps->ptr = LIBUSB20_ADD_BYTES(buf, 0);

					/*
					 * Get the correct number of
					 * length bytes:
					 */
					if (len != 0) {
						if (buf[0] == 0xFF) {
							ps->len = 3;
						} else {
							ps->len = 1;
						}
					} else {
						ps->len = 0;
					}
				}
				/* get the structure length */

				if (len != 0) {
					if (buf[0] == 0xFF) {
						if (len < 3) {
							len = 0;
							temp = 0;
						} else {
							len -= 3;
							temp = buf[1] |
							    (buf[2] << 8);
							buf += 3;
						}
					} else {
						len -= 1;
						temp = buf[0];
						buf += 1;
					}
				} else {
					len = 0;
					temp = 0;
				}
				/* check for invalid length */

				if (temp > len) {
					len = 0;
					temp = 0;
				}
				/* check wanted structure type */

				switch (ps->type) {
				case LIBUSB20_ME_IS_ENCODED:
					/* check for zero length */
					if (temp == 0) {
						/*
						 * The pointer must
						 * be valid:
						 */
						ps->ptr = LIBUSB20_ADD_BYTES(
						    libusb20_me_encode_empty, 0);
						ps->len = 1;
					} else {
						ps->len += temp;
					}
					break;

				case LIBUSB20_ME_IS_RAW:
					/* update length and pointer */
					ps->len = temp;
					ps->ptr = LIBUSB20_ADD_BYTES(buf, 0);
					break;

				case LIBUSB20_ME_IS_EMPTY:
				case LIBUSB20_ME_IS_DECODED:
					/* check for non-zero length */
					if (temp != 0) {
						/* update type */
						ps->type = LIBUSB20_ME_IS_DECODED;
						ps->len = 0;
						/*
						 * Recursivly decode
						 * the next structure
						 */
						(void) libusb20_me_decode(buf,
						    temp, ps->ptr);
					} else {
						/* update type */
						ps->type = LIBUSB20_ME_IS_EMPTY;
						ps->len = 0;
					}
					break;

				default:
					/*
					 * nothing to do - should
					 * not happen
					 */
					ps->ptr = NULL;
					ps->len = 0;
					break;
				}
				buf += temp;
				len -= temp;
				pd_offset += sizeof(struct libusb20_me_struct);
			}
			break;

		default:
			goto done;
		}
	}
done:
	return (len_old - len);
}
