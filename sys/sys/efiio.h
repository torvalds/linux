/*-
 * Copyright (c) 2016 Netflix, Inc.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer
 *    in this position and unchanged.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * $FreeBSD$
 */

#ifndef	_SYS_EFIIO_H_
#define	_SYS_EFIIO_H_

#include <sys/ioccom.h>
#include <sys/uuid.h>
#include <sys/efi.h>

struct efi_get_table_ioc
{
	struct uuid uuid;	/* UUID to look up */
	void *ptr;		/* Pointer to table in KVA space */
};

struct efi_var_ioc
{
	efi_char *name;		/* User pointer to name, in wide chars */
	size_t namesize;	/* Number of wide characters in name */
	struct uuid vendor;	/* Vendor's UUID for variable */
	uint32_t attrib;	/* Attributes */
	void *data;		/* User pointer to the data */
	size_t datasize;	/* Number of *bytes* in the data */
};

#define EFIIOC_GET_TABLE	_IOWR('E',  1, struct efi_get_table_ioc)
#define EFIIOC_GET_TIME		_IOR('E',   2, struct efi_tm)
#define EFIIOC_SET_TIME		_IOW('E',   3, struct efi_tm)
#define EFIIOC_VAR_GET		_IOWR('E',  4, struct efi_var_ioc)
#define EFIIOC_VAR_NEXT		_IOWR('E',  5, struct efi_var_ioc)
#define EFIIOC_VAR_SET		_IOWR('E',  6, struct efi_var_ioc)

#endif /* _SYS_EFIIO_H_ */
