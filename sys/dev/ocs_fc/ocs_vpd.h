/*-
 * Copyright (c) 2017 Broadcom. All rights reserved.
 * The term "Broadcom" refers to Broadcom Limited and/or its subsidiaries.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 *
 * 3. Neither the name of the copyright holder nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 *
 * $FreeBSD$
 */

/**
 * @file
 * OCS VPD parser
 */

#if !defined(__OCS_VPD_H__)
#define __OCS_VPD_H__

/**
 * @brief VPD buffer structure
 */

typedef struct {
	uint8_t *buffer;
	uint32_t length;
	uint32_t offset;
	uint8_t checksum;
	} vpdbuf_t;

/**
 * @brief return next VPD byte
 *
 * Returns next VPD byte and updates accumulated checksum
 *
 * @param vpd pointer to vpd buffer
 *
 * @return returns next byte for success, or a negative error code value for failure.
 *
 */

static inline int
vpdnext(vpdbuf_t *vpd)
{
	int rc = -1;
	if (vpd->offset < vpd->length) {
		rc = vpd->buffer[vpd->offset++];
		vpd->checksum += rc;
	}
	return rc;
}

/**
 * @brief return true if no more vpd buffer data
 *
 * return true if the vpd buffer data has been completely consumed
 *
 * @param vpd pointer to vpd buffer
 *
 * @return returns true if no more data
 *
 */
static inline int
vpddone(vpdbuf_t *vpd)
{
	return vpd->offset >= vpd->length;
}
/**
 * @brief return pointer to current VPD data location
 *
 * Returns a pointer to the current location in the VPD data
 *
 * @param vpd pointer to vpd buffer
 *
 * @return pointer to current VPD data location
 */

static inline uint8_t *
vpdref(vpdbuf_t *vpd)
{
	return &vpd->buffer[vpd->offset];
}

#define VPD_LARGE_RESOURCE_TYPE_ID_STRING_TAG	0x82
#define VPD_LARGE_RESOURCE_TYPE_R_TAG		0x90
#define VPD_LARGE_RESOURCE_TYPE_W_TAG		0x91
#define VPD_SMALL_RESOURCE_TYPE_END_TAG		0x78

/**
 * @brief find a VPD entry
 *
 * Finds a VPD entry given the two character code
 *
 * @param vpddata pointer to raw vpd data buffer
 * @param vpddata_length length of vpddata buffer in bytes
 * @param key key to look up

 * @return returns a pointer to the key location or NULL if not found or checksum error
 */

static inline uint8_t *
ocs_find_vpd(uint8_t *vpddata, uint32_t vpddata_length, const char *key)
{
	vpdbuf_t vpdbuf;
	uint8_t *pret = NULL;
	uint8_t c0 = key[0];
	uint8_t c1 = key[1];

	vpdbuf.buffer = (uint8_t*) vpddata;
	vpdbuf.length = vpddata_length;
	vpdbuf.offset = 0;
	vpdbuf.checksum = 0;

	while (!vpddone(&vpdbuf)) {
		int type = vpdnext(&vpdbuf);
		int len_lo;
		int len_hi;
		int len;
		int i;

		if (type == VPD_SMALL_RESOURCE_TYPE_END_TAG) {
			break;
		}

		len_lo = vpdnext(&vpdbuf);
		len_hi = vpdnext(&vpdbuf);
		len = len_lo + (len_hi << 8);

		if ((type == VPD_LARGE_RESOURCE_TYPE_R_TAG) || (type == VPD_LARGE_RESOURCE_TYPE_W_TAG)) {
			while (len > 0) {
				int rc0;
				int rc1;
				int sublen;
				uint8_t *pstart;

				rc0 = vpdnext(&vpdbuf);
				rc1 = vpdnext(&vpdbuf);

				/* Mark this location */
				pstart = vpdref(&vpdbuf);

				sublen = vpdnext(&vpdbuf);

				/* Adjust remaining len */
				len -= (sublen + 3);

				/* check for match with request */
				if ((c0 == rc0) && (c1 == rc1)) {
					pret = pstart;
					for (i = 0; i < sublen; i++) {
						vpdnext(&vpdbuf);
					}
				/* check for "RV" end */
				} else if ('R' == rc0 && 'V' == rc1) {

					/* Read the checksum */
					for (i = 0; i < sublen; i++) {
						vpdnext(&vpdbuf);
					}

					/* The accumulated checksum should be zero here */
					if (vpdbuf.checksum != 0) {
						ocs_log_test(NULL, "checksum error\n");
						return NULL;
					}
				}
				else
					for (i = 0; i < sublen; i++) {
						vpdnext(&vpdbuf);
					}
			}
		}

		for (i = 0; i < len; i++) {
			vpdnext(&vpdbuf);
		}
	}

	return pret;
}
#endif 
