/*-
 * Copyright (c) 2018, Juniper Networks, Inc.
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
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <libsecureboot.h>

#include "decode.h"

char *
octets2hex(unsigned char *ptr, size_t n)
{
	char *hex;
	char *cp;
	size_t i;

	hex = malloc(2 * n + 1);
	if (hex != NULL) {
		for (i = 0, cp = hex; i < n; i++) {
			snprintf(&cp[i*2], 3, "%02X", ptr[i]);
		}
	}
	return (hex);
}

unsigned char *
i2octets(int n, size_t i)
{
	static unsigned char o[16];
	int x, j;

	if (n > 15)
		return (NULL);
	for (j = 0, x = n - 1; x >= 0; x--, j++) {
		o[j] = (unsigned char)((i & (0xff << x * 8)) >> x * 8);
	}
	return (o);
}

int
octets2i(unsigned char *ptr, size_t n)
{
	size_t i;
	int val;

	for (val = i = 0; i < n; i++) {
		val |= (*ptr++ << ((n - i - 1) * 8));
	}
	return (val);
}

/**
 * @brief decode packet tag
 *
 * Also indicate if new/old and in the later case
 * the length type
 *
 * @sa rfc4880:4.2
 */
int
decode_tag(unsigned char *ptr, int *isnew, int *ltype)
{
	int tag;

	if (!ptr || !isnew || !ltype)
		return (-1);
	tag = *ptr;

	if (!(tag & OPENPGP_TAG_ISTAG))
		return (-1);		/* we are lost! */
	*isnew = tag & OPENPGP_TAG_ISNEW;
	if (*isnew) {
		*ltype = -1;		/* irrelevant */
		tag &= OPENPGP_TAG_NEW_MASK;
	} else {
		*ltype = tag & OPENPGP_TAG_OLD_TYPE;
		tag = (tag & OPENPGP_TAG_OLD_MASK) >> 2;
	}
	return (tag);
}

/**
 * @brief return packet length
 *
 * @sa rfc4880:4.2.2
 */
static int
decode_new_len(unsigned char **pptr)
{
	unsigned char *ptr;
	int len = -1;

	if (pptr == NULL)
		return (-1);
	ptr = *pptr;

	if (!(*ptr < 224 || *ptr == 255))
		return (-1);		/* not supported */

	if (*ptr < 192)
		len = *ptr++;
	else if (*ptr < 224) {
		len = ((*ptr - 192) << 8) + *(ptr+1) + 192;
		ptr++;
	} else if (*ptr == 255) {
		len = (*ptr++ << 24);
		len |= (*ptr++ << 16);
		len |= (*ptr++ < 8);
		len |= *ptr++;
	}

	*pptr = ptr;
	return (len);
}

/**
 * @brief return packet length
 *
 * @sa rfc4880:4.2.1
 */
static int
decode_len(unsigned char **pptr, int ltype)
{
	unsigned char *ptr;
	int len;

	if (ltype < 0)
		return (decode_new_len(pptr));

	if (pptr == NULL)
		return (-1);

	ptr = *pptr;

	switch (ltype) {
	case 0:
		len = *ptr++;
		break;
	case 1:
		len = (*ptr++ << 8);
		len |= *ptr++;
		break;
	case 2:
		len =  *ptr++ << 24;
		len |= *ptr++ << 16;
		len |= *ptr++ << 8;
		len |= *ptr++;
		break;
	case 3:
	default:
		/* Not supported */
		len = -1;
	}

	*pptr = ptr;
	return (len);
}

/**
 * @brief return pointer and length of an mpi
 *
 * @sa rfc4880:3.2
 */
unsigned char *
decode_mpi(unsigned char **pptr, size_t *sz)
{
	unsigned char *data;
	unsigned char *ptr;
	size_t mlen;

	if (pptr == NULL || sz == NULL)
		return (NULL);

	ptr = *pptr;

	mlen = (size_t)(*ptr++ << 8);
	mlen |= (size_t)*ptr++;		/* number of bits */
	mlen = (mlen + 7) / 8;		/* number of bytes */
	*sz = mlen;
	data = ptr;
	ptr += mlen;
	*pptr = ptr;
	return (data);
}

/**
 * @brief return an OpenSSL BIGNUM from mpi
 *
 * @sa rfc4880:3.2
 */
#ifdef USE_BEARSSL
unsigned char *
mpi2bn(unsigned char **pptr, size_t *sz)
{
	return (decode_mpi(pptr, sz));
}
#else
BIGNUM *
mpi2bn(unsigned char **pptr)
{
	BIGNUM *bn = NULL;
	unsigned char *ptr;
	int mlen;

	if (pptr == NULL)
		return (NULL);

	ptr = *pptr;

	mlen = (*ptr++ << 8);
	mlen |= *ptr++;			/* number of bits */
	mlen = (mlen + 7) / 8;		/* number of bytes */
	bn = BN_bin2bn(ptr, mlen, NULL);
	ptr += mlen;
	*pptr = ptr;

	return (bn);
}
#endif

/**
 * @brief decode a packet
 *
 * If want is set, check that the packet tag matches
 * if all good, call the provided decoder with its arg
 *
 * @return count of unconsumed data
 *
 * @sa rfc4880:4.2
 */
int
decode_packet(int want, unsigned char **pptr, size_t nbytes,
    decoder_t decoder, void *decoder_arg)
{
	int tag;
	unsigned char *ptr;
	unsigned char *nptr;
	int isnew, ltype;
	int len;
	int hlen;
	int rc = 0;

	nptr = ptr = *pptr;

	tag = decode_tag(ptr, &isnew, &ltype);

	if (want > 0 && tag != want)
		return (-1);
	ptr++;

	len = rc = decode_len(&ptr, ltype);
	hlen = (int)(ptr - nptr);
	nptr = ptr + len;		/* consume it */

	if (decoder)
		rc = decoder(tag, &ptr, len, decoder_arg);
	*pptr = nptr;
	nbytes -= (size_t)(hlen + len);
	if (rc < 0)
		return (rc);		/* error */
	return ((int)nbytes);		/* unconsumed data */
}

/**
 * @brief decode a sub packet
 *
 * @sa rfc4880:5.2.3.1
 */
unsigned char *
decode_subpacket(unsigned char **pptr, int *stag, int *sz)
{
	unsigned char *ptr;
	int len;

	ptr = *pptr;
	len = decode_len(&ptr, -1);
	*sz = (int)(len + ptr - *pptr);
	*pptr = ptr + len;
	*stag = *ptr++;
	return (ptr);
}
