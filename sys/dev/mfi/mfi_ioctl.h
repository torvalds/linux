/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2006 IronPort Systems
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
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <dev/mfi/mfireg.h>

struct iovec32 {
	u_int32_t	iov_base;
	int		iov_len;
};

#define MFIQ_FREE	0
#define MFIQ_BIO	1
#define MFIQ_READY	2
#define MFIQ_BUSY	3
#define MFIQ_COUNT	4

struct mfi_qstat {
	uint32_t	q_length;
	uint32_t	q_max;
};

union mfi_statrequest {
	uint32_t		ms_item;
	struct mfi_qstat	ms_qstat;
};

#define MAX_SPACE_FOR_SENSE_PTR		32
union mfi_sense_ptr {
	uint8_t		sense_ptr_data[MAX_SPACE_FOR_SENSE_PTR];
	void 		*user_space;
	struct {
		uint32_t	low;
		uint32_t	high;
	} addr;
} __packed;

#define MAX_IOCTL_SGE	16

struct mfi_ioc_packet {
	uint16_t	mfi_adapter_no;
	uint16_t	mfi_pad1;
	uint32_t	mfi_sgl_off;
	uint32_t	mfi_sge_count;
	uint32_t	mfi_sense_off;
	uint32_t	mfi_sense_len;
	union {
		uint8_t raw[128];
		struct mfi_frame_header hdr;
	} mfi_frame;

	struct iovec mfi_sgl[MAX_IOCTL_SGE];
} __packed;

#ifdef COMPAT_FREEBSD32
struct mfi_ioc_packet32 {
	uint16_t	mfi_adapter_no;
	uint16_t	mfi_pad1;
	uint32_t	mfi_sgl_off;
	uint32_t	mfi_sge_count;
	uint32_t	mfi_sense_off;
	uint32_t	mfi_sense_len;
	union {
		uint8_t raw[128];
		struct mfi_frame_header hdr;
	} mfi_frame;

	struct iovec32 mfi_sgl[MAX_IOCTL_SGE];
} __packed;
#endif

struct mfi_ioc_aen {
	uint16_t	aen_adapter_no;
	uint16_t	aen_pad1;
	uint32_t	aen_seq_num;
	uint32_t	aen_class_locale;
} __packed;

#define MFI_CMD		_IOWR('M', 1, struct mfi_ioc_packet)
#ifdef COMPAT_FREEBSD32
#define MFI_CMD32	_IOWR('M', 1, struct mfi_ioc_packet32)
#endif
#define MFI_SET_AEN	_IOW('M', 3, struct mfi_ioc_aen)

#define MAX_LINUX_IOCTL_SGE	16

struct mfi_linux_ioc_packet {
	uint16_t	lioc_adapter_no;
	uint16_t	lioc_pad1;
	uint32_t	lioc_sgl_off;
	uint32_t	lioc_sge_count;
	uint32_t	lioc_sense_off;
	uint32_t	lioc_sense_len;
	union {
		uint8_t raw[128];
		struct mfi_frame_header hdr;
	} lioc_frame;

#if defined(__amd64__) /* Assume amd64 wants 32 bit Linux */
	struct iovec32 lioc_sgl[MAX_LINUX_IOCTL_SGE];
#else
	struct iovec lioc_sgl[MAX_LINUX_IOCTL_SGE];
#endif
} __packed;

struct mfi_ioc_passthru {
	struct mfi_dcmd_frame	ioc_frame;
	uint32_t		buf_size;
	uint8_t			*buf;
} __packed;

#ifdef COMPAT_FREEBSD32
struct mfi_ioc_passthru32 {
	struct mfi_dcmd_frame	ioc_frame;
	uint32_t		buf_size;
	uint32_t		buf;
} __packed;
#endif

#define MFIIO_STATS	_IOWR('Q', 101, union mfi_statrequest)
#define MFIIO_PASSTHRU	_IOWR('C', 102, struct mfi_ioc_passthru)
#ifdef COMPAT_FREEBSD32
#define MFIIO_PASSTHRU32	_IOWR('C', 102, struct mfi_ioc_passthru32)
#endif

struct mfi_linux_ioc_aen {
	uint16_t	laen_adapter_no;
	uint16_t	laen_pad1;
	uint32_t	laen_seq_num;
	uint32_t	laen_class_locale;
} __packed;

struct mfi_query_disk {
	uint8_t	array_id;
	uint8_t	present;
	uint8_t	open;
	uint8_t reserved;	/* reserved for future use */
	char	devname[SPECNAMELEN + 1];
} __packed;

#define MFIIO_QUERY_DISK	_IOWR('Q', 102, struct mfi_query_disk)

/*
 * Create a second set so the FreeBSD native ioctl doesn't
 * conflict in FreeBSD ioctl handler.  Translate in mfi_linux.c.
 */
#define MFI_LINUX_CMD		0xc1144d01
#define MFI_LINUX_SET_AEN	0x400c4d03
#define MFI_LINUX_CMD_2		0xc1144d02
#define MFI_LINUX_SET_AEN_2	0x400c4d04
