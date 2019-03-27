/*-
 * Copyright (c) 2008 Yahoo!, Inc.
 * All rights reserved.
 * Written by: John Baldwin <jhb@FreeBSD.org>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the author nor the names of any co-contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
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

#ifndef __MPSUTIL_H__
#define	__MPSUTIL_H__

#include <sys/cdefs.h>
#include <sys/linker_set.h>
#include <stdbool.h>

#include <dev/mps/mpi/mpi2_type.h>
#include <dev/mps/mpi/mpi2.h>
#include <dev/mps/mpi/mpi2_cnfg.h>
#include <dev/mps/mpi/mpi2_raid.h>
#include <dev/mps/mpi/mpi2_ioc.h>
#include <dev/mps/mpi/mpi2_init.h>
#include <dev/mps/mpi/mpi2_tool.h>

#define MPSUTIL_VERSION	"1.0.0"

#define	IOC_STATUS_SUCCESS(status)					\
	(((status) & MPI2_IOCSTATUS_MASK) == MPI2_IOCSTATUS_SUCCESS)

struct mpsutil_command {
	const char *name;
	int (*handler)(int ac, char **av);
};
struct mpsutil_usage {
	const char *set;
	const char *name;
	void (*handler)(const char **, const char**);
};

#define	MPS_DATASET(name)	mpsutil_ ## name ## _table

#define	MPS_COMMAND(set, name, function, args, desc)			\
	static struct mpsutil_command function ## _mpsutil_command =	\
	{ #name, function };						\
	DATA_SET(MPS_DATASET(set), function ## _mpsutil_command);	\
	static void							\
	function ## _usage(const char **a3, const char **a4)		\
	{								\
		*a3 = args;						\
		*a4 = desc;						\
		return;							\
	};								\
	static struct mpsutil_usage function ## _mpsutil_usage =	\
	{ #set, #name, function ## _usage };				\
	DATA_SET(MPS_DATASET(usage), function ## _mpsutil_usage);

#define	_MPS_COMMAND(set, name, function)				\
	static struct mpsutil_command function ## _mpsutil_command =	\
	{ #name, function };						\
	DATA_SET(MPS_DATASET(set), function ## _mpsutil_command);

#define	MPS_TABLE(set, name)						\
	SET_DECLARE(MPS_DATASET(name), struct mpsutil_command);		\
									\
	static int							\
	mpsutil_ ## name ## _table_handler(int ac, char **av)		\
	{								\
		return (mps_table_handler(SET_BEGIN(MPS_DATASET(name)), \
		    SET_LIMIT(MPS_DATASET(name)), ac, av));		\
	}								\
	_MPS_COMMAND(set, name, mpsutil_ ## name ## _table_handler)

extern int mps_unit;
extern int is_mps;
#define MPS_MAX_UNIT 10

void	hexdump(const void *ptr, int length, const char *hdr, int flags);
#define	HD_COLUMN_MASK	0xff
#define	HD_DELIM_MASK	0xff00
#define	HD_OMIT_COUNT	(1 << 16)
#define	HD_OMIT_HEX	(1 << 17)
#define	HD_OMIT_CHARS	(1 << 18)
#define HD_REVERSED	(1 << 19)
int	mps_parse_flags(uintmax_t, const char *, char *, int);

int	mps_open(int unit);
int	mps_table_handler(struct mpsutil_command **start,
    struct mpsutil_command **end, int ac, char **av);
int	mps_user_command(int fd, void *req, uint32_t req_len, void *reply,
	uint32_t reply_len, void *buffer, int len, uint32_t flags);
int	mps_pass_command(int fd, void *req, uint32_t req_len, void *reply,
	uint32_t reply_len, void *data_in, uint32_t datain_len, void *data_out,
	uint32_t dataout_len, uint32_t timeout);
int	mps_read_config_page_header(int fd, U8 PageType, U8 PageNumber,
    U32 PageAddress, MPI2_CONFIG_PAGE_HEADER *header, U16 *IOCStatus);
int	mps_read_ext_config_page_header(int fd, U8 ExtPageType, U8 PageNumber,
    U32 PageAddress, MPI2_CONFIG_PAGE_HEADER *header,
    U16 *ExtPageLen, U16 *IOCStatus);
void	*mps_read_config_page(int fd, U8 PageType, U8 PageNumber,
    U32 PageAddress, U16 *IOCStatus);
void	*mps_read_extended_config_page(int fd, U8 ExtPageType, U8 PageVersion,
    U8 PageNumber, U32 PageAddress, U16 *IOCStatus);
int	mps_map_btdh(int fd, uint16_t *devhandle, uint16_t *bus,
    uint16_t *target);
const char *mps_ioc_status(U16 IOCStatus);
int	mps_firmware_send(int fd, unsigned char *buf, uint32_t len, bool bios);
int	mps_firmware_get(int fd, unsigned char **buf, bool bios);

static __inline void *
mps_read_man_page(int fd, U8 PageNumber, U16 *IOCStatus)
{

	return (mps_read_config_page(fd, MPI2_CONFIG_PAGETYPE_MANUFACTURING,
	    PageNumber, 0, IOCStatus));
}

static __inline void *
mps_read_ioc_page(int fd, U8 PageNumber, U16 *IOCStatus)
{

	return (mps_read_config_page(fd, MPI2_CONFIG_PAGETYPE_IOC, PageNumber,
	    0, IOCStatus));
}

static __inline uint64_t
mps_to_u64(U64 *data)
{

	return (((uint64_t)(data->High) << 32 ) | data->Low);
}

MPI2_IOC_FACTS_REPLY * mps_get_iocfacts(int fd);

#endif /* !__MPSUTIL_H__ */
