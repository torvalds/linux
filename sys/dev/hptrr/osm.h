/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) HighPoint Technologies, Inc.
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
#include <dev/hptrr/hptrr_config.h>
/*
 * $Id: osm.h,v 1.7 2007/12/10 02:13:52 xxj Exp $
 * Copyright (C) 2005 HighPoint Technologies, Inc. All rights reserved.
 */
#ifndef _HPT_OSM_H_
#define _HPT_OSM_H_

#define VERMAGIC_OSM 6

#define os_max_queue_comm 32
#define os_max_sg_descriptors 18


extern int os_max_cache_size;


#define DMAPOOL_PAGE_SIZE 0x1000 /* PAGE_SIZE (i386/x86_64) */
#define os_max_cache_pages (os_max_cache_size/DMAPOOL_PAGE_SIZE)

/* data types */
typedef unsigned int HPT_UINT, HPT_U32;
typedef unsigned long HPT_UPTR;
typedef unsigned short HPT_U16;
typedef unsigned char HPT_U8;
typedef unsigned long HPT_TIME;
typedef unsigned long long HPT_U64;

#define CPU_TO_LE64(x) (x)
#define CPU_TO_LE32(x) (x)
#define CPU_TO_LE16(x) (x)
#define LE32_TO_CPU(x) (x)
#define LE16_TO_CPU(x) (x)
#define LE64_TO_CPU(x) (x)

#define FAR
#define EXTERN_C

typedef void * HPT_PTR;

typedef HPT_U64 HPT_LBA;
typedef HPT_U64 HPT_RAW_LBA;
#define MAX_LBA_VALUE 0xffffffffffffffffull
#define MAX_RAW_LBA_VALUE MAX_LBA_VALUE
#define RAW_LBA(x) (x)
#define LO_LBA(x) ((HPT_U32)(x))
#define HI_LBA(x) (sizeof(HPT_LBA)>4? (HPT_U32)((x)>>32) : 0)
#define LBA_FORMAT_STR "0x%llX"

typedef HPT_U64 BUS_ADDRESS;
#define LO_BUSADDR(x) ((HPT_U32)(x))
#define HI_BUSADDR(x) (sizeof(BUS_ADDRESS)>4? (x)>>32 : 0)

typedef unsigned char HPT_BOOL;
#define HPT_TRUE  1
#define HPT_FALSE 0

typedef struct _TIME_RECORD {
   HPT_U32        seconds:6;      /* 0 - 59 */
   HPT_U32        minutes:6;      /* 0 - 59 */
   HPT_U32        month:4;        /* 1 - 12 */
   HPT_U32        hours:6;        /* 0 - 59 */
   HPT_U32        day:5;          /* 1 - 31 */
   HPT_U32        year:5;         /* 0=2000, 31=2031 */
} TIME_RECORD;

/* hardware access */
HPT_U8   os_inb  (void *port);
HPT_U16  os_inw  (void *port);
HPT_U32  os_inl  (void *port);
void     os_outb (void *port, HPT_U8 value);
void     os_outw (void *port, HPT_U16 value);
void     os_outl (void *port, HPT_U32 value);
void     os_insw (void *port, HPT_U16 *buffer, HPT_U32 count);
void     os_outsw(void *port, HPT_U16 *buffer, HPT_U32 count);

extern HPT_U32 __dummy_reg; /* to avoid the compiler warning */

#define os_readb(addr) (*(HPT_U8 *)&__dummy_reg = *(volatile HPT_U8 *)(addr))
#define os_readw(addr) (*(HPT_U16 *)&__dummy_reg = *(volatile HPT_U16 *)(addr))
#define os_readl(addr) (*(HPT_U32 *)&__dummy_reg = *(volatile HPT_U32 *)(addr))

#define os_writeb(addr, val) *(volatile HPT_U8 *)(addr) = (HPT_U8)(val)
#define os_writew(addr, val) *(volatile HPT_U16 *)(addr) = (HPT_U16)(val)
#define os_writel(addr, val) *(volatile HPT_U32 *)(addr) = (HPT_U32)(val)

/* PCI configuration space for specified device*/
HPT_U8   os_pci_readb (void *osext, HPT_U8 offset);
HPT_U16  os_pci_readw (void *osext, HPT_U8 offset);
HPT_U32  os_pci_readl (void *osext, HPT_U8 offset);
void     os_pci_writeb(void *osext, HPT_U8 offset, HPT_U8 value);
void     os_pci_writew(void *osext, HPT_U8 offset, HPT_U16 value);
void     os_pci_writel(void *osext, HPT_U8 offset, HPT_U32 value);

/* obsolute interface */
#define MAX_PCI_BUS_NUMBER 0xff
#define MAX_PCI_DEVICE_NUMBER 32
#define MAX_PCI_FUNC_NUMBER 8
#define pcicfg_read_dword(bus, dev, fn, reg) 0xffff


void *os_map_pci_bar(
	void *osext, 
	int index,   
	HPT_U32 offset,
	HPT_U32 length
);


void os_unmap_pci_bar(void *osext, void *base);

#define os_kmap_sgptr(psg) (psg->addr._logical)
#define os_kunmap_sgptr(ptr)
#define os_set_sgptr(psg, ptr) (psg)->addr._logical = (ptr)

/* timer */
void *os_add_timer(void *osext, HPT_U32 microseconds, void (*proc)(void *), void *arg);
void  os_del_timer(void *handle);
void  os_request_timer(void * osext, HPT_U32 interval);
HPT_TIME os_query_time(void);

/* task */
#define OS_SUPPORT_TASK

typedef struct _OSM_TASK {
	struct _OSM_TASK *next;
	void (*func)(void *vbus, void *data);
	void *data;
}
OSM_TASK;

void os_schedule_task(void *osext, OSM_TASK *task);

/* misc */
HPT_U32 os_get_stamp(void);
void os_stallexec(HPT_U32 microseconds);

#ifndef _SYS_LIBKERN_H_
#define memcpy(dst, src, size) __builtin_memcpy((dst), (src), (size))
#define memcmp(dst, src, size) __builtin_memcmp((dst), (src), (size))
#define strcpy(dst, src) __builtin_strcpy((dst), (src))
static __inline void * memset(void *dst, int c, unsigned long size)
{
	char *p;
	for (p=(char*)dst; size; size--,p++) *p = c;
	return dst;
}
#endif

#define farMemoryCopy(a,b,c) memcpy((char *)(a), (char *)(b), (HPT_U32)c)


#define os_register_device(osext, target_id)
#define os_unregister_device(osext, target_id)
int os_query_remove_device(void *osext, int target_id);
int os_revalidate_device(void *osext, int target_id);

HPT_U8 os_get_vbus_seq(void *osext);

/* debug support */
int  os_printk(char *fmt, ...);

#if DBG
extern int hptrr_dbg_level;
#define KdPrint(x)  do { if (hptrr_dbg_level) os_printk x; } while (0)
void __os_dbgbreak(const char *file, int line);
#define os_dbgbreak() __os_dbgbreak(__FILE__, __LINE__)
#define HPT_ASSERT(x) do { if (!(x)) os_dbgbreak(); } while (0)
void os_check_stack(const char *location, int size);
#define HPT_CHECK_STACK(size) os_check_stack(__FUNCTION__, (size))
#else 
#define KdPrint(x)
#define HPT_ASSERT(x)
#define HPT_CHECK_STACK(size)
#endif

#define OsPrint(x) do { os_printk x; } while (0)

#endif
