/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2004-2005 HighPoint Technologies, Inc.
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
#ifdef _RAID5N_

/* OS provided function, call only at initialization time */
extern void * HPTLIBAPI os_alloc_page(_VBUS_ARG0);      /* may be cached memory */
extern void * HPTLIBAPI os_alloc_dma_page(_VBUS_ARG0);  /* must be non-cached memory */
/* implement if the driver can be unloaded */
void HPTLIBAPI os_free_page(_VBUS_ARG void *p);
void HPTLIBAPI os_free_dma_page(_VBUS_ARG void *p);

typedef void (* HPTLIBAPI xfer_done_fn)(_VBUS_ARG void *tag, int result);


#define DATAXFER_STACK_VAR
#define DATAXFER_INIT_ARG 0

#define dataxfer_init(arg) 0
#define dataxfer_add_item(handle, host, cache, bytes, tocache) \
		if (tocache) memcpy((PUCHAR)(cache), (PUCHAR)(host), bytes); \
		else memcpy((PUCHAR)(host), (PUCHAR)(cache), bytes)
#define dataxfer_exec(handle, done, tag) done(_VBUS_P tag, 0)
#define dataxfer_poll()


typedef void (* HPTLIBAPI xor_done_fn)(_VBUS_ARG void *tag, int result);


#define XOR_STACK_VAR
#define XOR_INIT_ARG 0

/* DoXor1, DoXor2 provided by platform dependent code */
void HPTLIBAPI DoXor1(ULONG *p0, ULONG *p1, ULONG *p2, UINT nBytes);
void HPTLIBAPI DoXor2(ULONG *p0, ULONG *p2, UINT nBytes);
#define max_xor_way 2
#define xor_init(arg) 0
#define xor_add_item(handle, dest, src, nsrc, bytes) \
	do {\
		if (((void**)(src))[0]==dest)\
			DoXor2((PULONG)(dest), ((PULONG *)(src))[1], bytes);\
		else\
			DoXor1((PULONG)(dest), ((PULONG *)(src))[0], ((PULONG *)(src))[1], bytes);\
	} while(0)
#define xor_exec(handle, done, tag) done(_VBUS_P tag, 0)
#define xor_poll()


/* set before calling init_raid5_memory */
extern UINT num_raid5_pages;

/* called by init.c */
extern void HPTLIBAPI init_raid5_memory(_VBUS_ARG0);
extern void HPTLIBAPI free_raid5_memory(_VBUS_ARG0);

/* asynchronous flush, may be called periodly */
extern void HPTLIBAPI flush_stripe_cache(_VBUS_ARG0);
extern void HPTLIBAPI flush_raid5_async(PVDevice pArray, DPC_PROC done, void *arg);

/* synchronous function called at shutdown */
extern int HPTLIBAPI flush_raid5(PVDevice pArray);

extern void HPTLIBAPI raid5_free(_VBUS_ARG PVDevice pArray);

struct free_heap_block {
	struct free_heap_block *next;
};

#ifndef LIST_H_INCLUDED
struct list_head {
	struct list_head *next, *prev;
};
#endif

struct free_page {
	struct free_page *link;
};

struct r5_global_data {
	int enable_write_back;
	struct list_head inactive_list;
	struct list_head dirty_list;
	struct list_head active_list;
#ifdef R5_CONTIG_CACHE
	BUS_ADDR page_base_phys;
	PUCHAR page_base_virt;
	PUCHAR page_current;
#endif
	struct free_heap_block *free_heap_slots[10];
	struct free_page *free_pages;
	UINT num_free_pages;
	UINT active_stripes;
	UINT num_flushing;
	PCommand cache_wait_list;
	
	LBA_T __start[MAX_MEMBERS];
	USHORT __sectors[MAX_MEMBERS];
};


#endif
