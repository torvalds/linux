/*-
 * Copyright (c) 2011 Hudson River Trading LLC
 * Written by: John H. Baldwin <jhb@FreeBSD.org>
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
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $FreeBSD$
 */

#ifndef	_EDD_H_
#define	_EDD_H_

/* Supported interfaces for "Check Extensions Present". */
#define	EDD_INTERFACE_FIXED_DISK	0x01
#define	EDD_INTERFACE_EJECT		0x02
#define	EDD_INTERFACE_EDD		0x04

struct edd_packet {
	uint16_t	len;
	uint16_t	count;
	uint16_t	off;
	uint16_t	seg;
	uint64_t	lba;
};

struct edd_packet_v3 {
	uint16_t	len;
	uint16_t	count;
	uint16_t	off;
	uint16_t	seg;
	uint64_t	lba;
	uint64_t	phys_addr;
};

struct edd_params {
	uint16_t	len;
	uint16_t	flags;
	uint32_t	cylinders;
	uint32_t	heads;
	uint32_t	sectors_per_track;
	uint64_t	sectors;
	uint16_t	sector_size;
	uint16_t	edd_params_seg;
	uint16_t	edd_params_off;
} __packed;

struct edd_device_path_v3 {
	uint16_t	key;
	uint8_t		len;
	uint8_t		reserved[3];
	char		host_bus[4];
	char		interface[8];
	uint64_t	interface_path;
	uint64_t	device_path;
	uint8_t		reserved2[1];
	uint8_t		checksum;
} __packed;

struct edd_params_v3 {
	struct edd_params params;
	struct edd_device_path_v3 device_path;
} __packed;

struct edd_device_path_v4 {
	uint16_t	key;
	uint8_t		len;
	uint8_t		reserved[3];
	char		host_bus[4];
	char		interface[8];
	uint64_t	interface_path;
	uint64_t	device_path[2];
	uint8_t		reserved2[1];
	uint8_t		checksum;
} __packed;

struct edd_params_v4 {
	struct edd_params params;
	struct edd_device_path_v4 device_path;
} __packed;

#define	EDD_FLAGS_DMA_BOUNDARY_HANDLING		0x0001
#define	EDD_FLAGS_REMOVABLE_MEDIA		0x0002
#define	EDD_FLAGS_WRITE_VERIFY			0x0004
#define	EDD_FLAGS_MEDIA_CHANGE_NOTIFICATION	0x0008
#define	EDD_FLAGS_LOCKABLE_MEDIA		0x0010
#define	EDD_FLAGS_NO_MEDIA_PRESENT		0x0020

#define	EDD_DEVICE_PATH_KEY	0xbedd

#endif /* !_EDD_H_ */
