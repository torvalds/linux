/*
 *   BSD LICENSE
 *
 *   Copyright(c) 2017 Cavium, Inc.. All rights reserved.
 *   All rights reserved.
 *
 *   Redistribution and use in source and binary forms, with or without
 *   modification, are permitted provided that the following conditions
 *   are met:
 *
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above copyright
 *       notice, this list of conditions and the following disclaimer in
 *       the documentation and/or other materials provided with the
 *       distribution.
 *     * Neither the name of Cavium, Inc. nor the names of its
 *       contributors may be used to endorse or promote products derived
 *       from this software without specific prior written permission.
 *
 *   THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 *   "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 *   LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 *   A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 *   OWNER(S) OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 *   SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 *   LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 *   DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 *   THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 *   (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 *   OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */
/*$FreeBSD$*/

#ifndef _LIO_IMAGE_H_
#define _LIO_IMAGE_H_

#define LIO_MAX_FW_FILENAME_LEN		256
#define LIO_FW_BASE_NAME		"lio_"
#define LIO_FW_NAME_SUFFIX		".bin"
#define LIO_FW_NAME_TYPE_NIC		"nic"
#define LIO_FW_NAME_TYPE_NONE		"none"
#define LIO_MAX_FIRMWARE_VERSION_LEN	16

#define LIO_MAX_BOOTCMD_LEN		1024
#define LIO_MAX_IMAGES			16
#define LIO_NIC_MAGIC			0x434E4943	/* "CNIC" */
struct lio_firmware_desc {
	__be64	addr;
	__be32	len;
	__be32	crc32;	/* crc32 of image */
};

/*
 * Following the header is a list of 64-bit aligned binary images,
 * as described by the desc field.
 * Numeric fields are in network byte order.
 */
struct lio_firmware_file_header {
	__be32				magic;
	char				version[LIO_MAX_FIRMWARE_VERSION_LEN];
	char				bootcmd[LIO_MAX_BOOTCMD_LEN];
	__be32				num_images;
	struct lio_firmware_desc	desc[LIO_MAX_IMAGES];
	__be32				pad;
	__be32				crc32;	/* header checksum */
};

#endif	/* _LIO_IMAGE_H_ */
