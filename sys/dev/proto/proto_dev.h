/*-
 * Copyright (c) 2014, 2015 Marcel Moolenaar
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
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

#ifndef _DEV_PROTO_DEV_H_
#define _DEV_PROTO_DEV_H_

#include <sys/ioccom.h>

#define	PROTO_IOC_CLASS	'h'

struct proto_ioc_region {
	unsigned long	address;
	unsigned long	size;
};

#define PROTO_IOC_REGION _IOWR(PROTO_IOC_CLASS, 1, struct proto_ioc_region)

struct proto_ioc_busdma {
	unsigned int	request;
#define	PROTO_IOC_BUSDMA_TAG_CREATE	1
#define	PROTO_IOC_BUSDMA_TAG_DERIVE	2
#define	PROTO_IOC_BUSDMA_TAG_DESTROY	3
#define	PROTO_IOC_BUSDMA_MEM_ALLOC	10
#define	PROTO_IOC_BUSDMA_MEM_FREE	11
#define	PROTO_IOC_BUSDMA_MD_CREATE	20
#define	PROTO_IOC_BUSDMA_MD_DESTROY	21
#define	PROTO_IOC_BUSDMA_MD_LOAD	22
#define	PROTO_IOC_BUSDMA_MD_UNLOAD	29
#define	PROTO_IOC_BUSDMA_SYNC		30
	unsigned long	key;
	union {
		struct {
			unsigned long	align;
			unsigned long	bndry;
			unsigned long	maxaddr;
			unsigned long	maxsz;
			unsigned long	maxsegsz;
			unsigned int	nsegs;
			unsigned int	datarate;
			unsigned int	flags;
		} tag;
		struct {
			unsigned long	tag;
			unsigned int	flags;
			unsigned long	virt_addr;
			unsigned long	virt_size;
			unsigned int	phys_nsegs;
			unsigned long	phys_addr;
			unsigned long	bus_addr;
			unsigned int	bus_nsegs;
		} md;
		struct {
			unsigned int	op;
			unsigned long	base;
			unsigned long	size;
		} sync;
	} u;
	unsigned long	result;
};

#define PROTO_IOC_BUSDMA _IOWR(PROTO_IOC_CLASS, 2, struct proto_ioc_busdma)

#endif /* _DEV_PROTO_H_ */
