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
#ifndef __INCmvOsBsdh
#define __INCmvOsBsdh

#ifdef DEBUG
#define MV_DEBUG_LOG
#endif

#define ENABLE_READ_AHEAD
#define ENABLE_WRITE_CACHE

/* Typedefs    */
/*#define HPTLIBAPI __attribute__((regparm(0))) */
#define HPTLIBAPI 
#define FAR
#define SS_SEG
#ifdef FASTCALL
#undef FASTCALL
#endif
#define FASTCALL HPTLIBAPI
#define PASCAL HPTLIBAPI

typedef unsigned short USHORT;
typedef unsigned char  UCHAR;
typedef unsigned char *PUCHAR;
typedef unsigned short *PUSHORT;
typedef unsigned char  BOOLEAN;
typedef unsigned short WORD;
typedef unsigned int   UINT, BOOL;
typedef unsigned char  BYTE;
typedef void *PVOID, *LPVOID;
typedef void *ADDRESS;

typedef int  LONG;
typedef unsigned int ULONG, *PULONG;
typedef unsigned int DWORD, *LPDWORD, *PDWORD;
typedef unsigned long ULONG_PTR, UINT_PTR, BUS_ADDR;
typedef unsigned long long HPT_U64, LBA_T;

typedef enum mvBoolean{MV_FALSE, MV_TRUE} MV_BOOLEAN;

#define FALSE 0
#define TRUE  1

#ifndef NULL
#define NULL  0
#endif

/* System dependent typedefs */
typedef void			MV_VOID;
typedef unsigned int 	MV_U32;
typedef unsigned short	MV_U16;
typedef unsigned char	MV_U8;
typedef void			*MV_VOID_PTR;
typedef MV_U32			*MV_U32_PTR;
typedef MV_U16 			*MV_U16_PTR;
typedef MV_U8			*MV_U8_PTR;
typedef char			*MV_CHAR_PTR;
typedef void			*MV_BUS_ADDR_T;

/* System dependent macro for flushing CPU write cache */
#define MV_CPU_WRITE_BUFFER_FLUSH()

/* System dependent little endian from / to CPU conversions */
#define MV_CPU_TO_LE16(x)	(x)
#define MV_CPU_TO_LE32(x)	(x)

#define MV_LE16_TO_CPU(x)	(x)
#define MV_LE32_TO_CPU(x)	(x)

/* System dependent register read / write in byte/word/dword variants */
extern void HPTLIBAPI MV_REG_WRITE_BYTE(MV_BUS_ADDR_T base, MV_U32 offset, MV_U8 val);
extern void HPTLIBAPI MV_REG_WRITE_WORD(MV_BUS_ADDR_T base, MV_U32 offset, MV_U16 val);
extern void HPTLIBAPI MV_REG_WRITE_DWORD(MV_BUS_ADDR_T base, MV_U32 offset, MV_U32 val);
extern MV_U8  HPTLIBAPI MV_REG_READ_BYTE(MV_BUS_ADDR_T base, MV_U32 offset);
extern MV_U16 HPTLIBAPI MV_REG_READ_WORD(MV_BUS_ADDR_T base, MV_U32 offset);
extern MV_U32 HPTLIBAPI MV_REG_READ_DWORD(MV_BUS_ADDR_T base, MV_U32 offset);

/* System dependent structure */
typedef struct mvOsSemaphore
{
	int notused;
} MV_OS_SEMAPHORE;

/* Functions (User implemented)*/
ULONG_PTR HPTLIBAPI fOsPhysicalAddress(void *addr);

/* Semaphore init, take and release */
#define mvOsSemInit(p)		(MV_TRUE)
#define mvOsSemTake(p)		(MV_TRUE)
#define mvOsSemRelease(p)	(MV_TRUE)

/* Delay function in micro seconds resolution */
void HPTLIBAPI mvMicroSecondsDelay(MV_U32);

/* System logging function */
#ifdef MV_DEBUG_LOG
int mvLogMsg(MV_U8, MV_CHAR_PTR, ...);
#define _mvLogMsg(x) mvLogMsg x
#else 
#define mvLogMsg(x...) 
#define _mvLogMsg(x)
#endif

/*************************************************************************
 * Debug support
 *************************************************************************/
#ifdef DEBUG
#define HPT_ASSERT(x) do { if (!(x)) { \
						printf("ASSERT fail at %s line %d", __FILE__, __LINE__); \
						while (1); \
					  }} while (0)
extern int hpt_dbg_level;
#define KdPrintI(_x_) do{ if (hpt_dbg_level>2) printf _x_; }while(0)
#define KdPrintW(_x_) do{ if (hpt_dbg_level>1) printf _x_; }while(0)
#define KdPrintE(_x_) do{ if (hpt_dbg_level>0) printf _x_; }while(0)
#define KdPrint(x) KdPrintI(x)
#else 
#define HPT_ASSERT(x)
#define KdPrint(x) 
#define KdPrintI(x) 
#define KdPrintW(x) 
#define KdPrintE(x) 
#endif

#endif
