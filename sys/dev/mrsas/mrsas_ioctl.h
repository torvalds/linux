/*
 * Copyright (c) 2015, AVAGO Tech. All rights reserved. Author: Marian Choy
 * Copyright (c) 2014, LSI Corp. All rights reserved. Author: Marian Choy
 * Support: freebsdraid@avagotech.com
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 * 1. Redistributions of source code must retain the above copyright notice,
 * this list of conditions and the following disclaimer. 2. Redistributions
 * in binary form must reproduce the above copyright notice, this list of
 * conditions and the following disclaimer in the documentation and/or other
 * materials provided with the distribution. 3. Neither the name of the
 * <ORGANIZATION> nor the names of its contributors may be used to endorse or
 * promote products derived from this software without specific prior written
 * permission.
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
 * The views and conclusions contained in the software and documentation are
 * those of the authors and should not be interpreted as representing
 * official policies,either expressed or implied, of the FreeBSD Project.
 *
 * Send feedback to: <megaraidfbsd@avagotech.com> Mail to: AVAGO TECHNOLOGIES, 1621
 * Barber Lane, Milpitas, CA 95035 ATTN: MegaRaid FreeBSD
 *
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#ifndef MRSAS_IOCTL_H
#define	MRSAS_IOCTL_H

#ifndef _IOWR
#include <sys/ioccom.h>
#endif					/* !_IOWR */

#ifdef COMPAT_FREEBSD32
/* Compilation error FIX */
#if (__FreeBSD_version <= 900000)
#include <sys/socket.h>
#endif
#include <sys/mount.h>
#include <compat/freebsd32/freebsd32.h>
#endif

/*
 * We need to use the same values as the mfi driver until MegaCli adds
 * support for this (mrsas) driver: M is for MegaRAID. (This is typically the
 * vendor or product initial) 1 arbitrary. (This may be used to segment kinds
 * of commands.  (1-9 status, 10-20 policy, etc.) struct mrsas_iocpacket
 * (sizeof() this parameter will be used.) These three values are encoded
 * into a somewhat unique, 32-bit value.
 */

#define	MRSAS_IOC_GET_PCI_INFO				_IOR('M', 7, MRSAS_DRV_PCI_INFORMATION)
#define	MRSAS_IOC_FIRMWARE_PASS_THROUGH64	_IOWR('M', 1, struct mrsas_iocpacket)
#ifdef COMPAT_FREEBSD32
#define	MRSAS_IOC_FIRMWARE_PASS_THROUGH32	_IOWR('M', 1, struct mrsas_iocpacket32)
#endif

#define	MRSAS_IOC_SCAN_BUS		_IO('M',  10)

#define	MRSAS_LINUX_CMD32		0xc1144d01

#define	MAX_IOCTL_SGE			16
#define	MFI_FRAME_DIR_READ		0x0010
#define	MFI_CMD_LD_SCSI_IO		0x03

#define	INQUIRY_CMD				0x12
#define	INQUIRY_CMDLEN			6
#define	INQUIRY_REPLY_LEN		96
#define	INQUIRY_VENDOR			8	/* Offset in reply data to
						 * vendor name */
#define	SCSI_SENSE_BUFFERSIZE	96

#define	MEGAMFI_RAW_FRAME_SIZE	128


#pragma pack(1)
struct mrsas_iocpacket {
	u_int16_t host_no;
	u_int16_t __pad1;
	u_int32_t sgl_off;
	u_int32_t sge_count;
	u_int32_t sense_off;
	u_int32_t sense_len;
	union {
		u_int8_t raw[MEGAMFI_RAW_FRAME_SIZE];
		struct mrsas_header hdr;
	}	frame;
	struct iovec sgl[MAX_IOCTL_SGE];
};

#pragma pack()

#ifdef COMPAT_FREEBSD32
#pragma pack(1)
struct mrsas_iocpacket32 {
	u_int16_t host_no;
	u_int16_t __pad1;
	u_int32_t sgl_off;
	u_int32_t sge_count;
	u_int32_t sense_off;
	u_int32_t sense_len;
	union {
		u_int8_t raw[MEGAMFI_RAW_FRAME_SIZE];
		struct mrsas_header hdr;
	}	frame;
	struct iovec32 sgl[MAX_IOCTL_SGE];
};

#pragma pack()
#endif					/* COMPAT_FREEBSD32 */

#endif					/* MRSAS_IOCTL_H */
