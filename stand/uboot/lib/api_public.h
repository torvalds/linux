/*
 * (C) Copyright 2007-2008 Semihalf
 *
 * Written by: Rafal Jaworowski <raj@semihalf.com>
 *
 * This file is dual licensed; you can use it under the terms of
 * either the GPL, or the BSD license, at your option.
 *
 * I. GPL:
 *
 * This file is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of
 * the License, or (at your option) any later version.
 *
 * This file is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston,
 * MA 02111-1307 USA
 *
 * Alternatively,
 *
 * II. BSD license:
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
 *
 * This file needs to be kept in sync with U-Boot reference:
 * http://www.denx.de/cgi-bin/gitweb.cgi?p=u-boot.git;a=blob;f=include/api_public.h
 */

#ifndef _API_PUBLIC_H_
#define	_API_PUBLIC_H_

#define	API_EINVAL		1	/* invalid argument(s)	*/
#define	API_ENODEV		2	/* no device		*/
#define	API_ENOMEM		3	/* no memory		*/
#define	API_EBUSY		4	/* busy, occupied etc.	*/
#define	API_EIO			5	/* I/O error		*/
#define	API_ESYSC		6	/* syscall error	*/

typedef int (*scp_t)(int, int *, ...);

#define	API_SIG_VERSION	1
#define	API_SIG_MAGIC	"UBootAPI"
#define	API_SIG_MAGLEN	8

struct api_signature {
	char		magic[API_SIG_MAGLEN];	/* magic string */
	uint16_t	version;	/* API version */
	uint32_t	checksum;	/* checksum of this sig struct */
	scp_t		syscall;	/* entry point to the API */
};

enum {
	API_RSVD = 0,
	API_GETC,
	API_PUTC,
	API_TSTC,
	API_PUTS,
	API_RESET,
	API_GET_SYS_INFO,
	API_UDELAY,
	API_GET_TIMER,
	API_DEV_ENUM,
	API_DEV_OPEN,
	API_DEV_CLOSE,
	API_DEV_READ,
	API_DEV_WRITE,
	API_ENV_ENUM,
	API_ENV_GET,
	API_ENV_SET,
	API_MAXCALL
};

#define	MR_ATTR_FLASH	0x0001
#define	MR_ATTR_DRAM	0x0002
#define	MR_ATTR_SRAM	0x0003

struct mem_region {
	unsigned long	start;
	unsigned long	size;
	int		flags;
};

struct sys_info {
	unsigned long		clk_bus;
	unsigned long		clk_cpu;
	unsigned long		bar;
	struct mem_region	*mr;
	int			mr_no;	/* number of memory regions */
};

#undef CFG_64BIT_LBA
#ifdef CFG_64BIT_LBA
typedef	uint64_t lbasize_t;
#else
typedef unsigned long lbasize_t;
#endif
typedef unsigned long lbastart_t;

#define	DEV_TYP_NONE	0x0000
#define	DEV_TYP_NET	0x0001

#define	DEV_TYP_STOR	0x0002
#define	DT_STOR_IDE	0x0010
#define	DT_STOR_SCSI	0x0020
#define	DT_STOR_USB	0x0040
#define	DT_STOR_MMC	0x0080
#define	DT_STOR_SATA	0x0100

#define	DEV_STA_CLOSED	0x0000		/* invalid, closed */
#define	DEV_STA_OPEN	0x0001		/* open i.e. active */

struct device_info {
	int	type;
	void	*cookie;

	union {
		struct {
			lbasize_t	block_count;	/* no of blocks */
			unsigned long	block_size;	/* size of one block */
		} storage;

		struct {
			unsigned char	hwaddr[6];
		} net;
	} info;
#define	di_stor info.storage
#define	di_net info.net

	int	state;
};

#endif /* _API_PUBLIC_H_ */
