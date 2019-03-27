/*
 * Copyright (c) 2017-2018 Cavium, Inc. 
 * All rights reserved.
 *
 *  Redistribution and use in source and binary forms, with or without
 *  modification, are permitted provided that the following conditions
 *  are met:
 *
 *  1. Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 *  2. Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in the
 *     documentation and/or other materials provided with the distribution.
 *
 *  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 *  AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 *  IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 *  ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 *  LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 *  CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 *  SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 *  INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 *  CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 *  ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 *  POSSIBILITY OF SUCH DAMAGE.
 *
 * $FreeBSD$
 *
 */

#ifndef __ECORE_UTILS_H__
#define __ECORE_UTILS_H__

/* dma_addr_t manip */
/* Suppress "right shift count >= width of type" warning when that quantity is
 * 32-bits rquires the >> 16) >> 16)
 */
#define PTR_LO(x)		((u32)(((osal_uintptr_t)(x)) & 0xffffffff))
#define PTR_HI(x)		((u32)((((osal_uintptr_t)(x)) >> 16) >> 16))

#define DMA_LO(x)		((u32)(((dma_addr_t)(x)) & 0xffffffff))
#define DMA_HI(x)		((u32)(((dma_addr_t)(x)) >> 32))

#define DMA_LO_LE(x)		OSAL_CPU_TO_LE32(DMA_LO(x))
#define DMA_HI_LE(x)		OSAL_CPU_TO_LE32(DMA_HI(x))

/* It's assumed that whoever includes this has previously included an hsi
 * file defining the regpair.
 */
#define DMA_REGPAIR_LE(x, val)	(x).hi = DMA_HI_LE((val)); \
				(x).lo = DMA_LO_LE((val))

#define HILO_GEN(hi, lo, type)	((((type)(hi)) << 32) + (lo))
#define HILO_DMA(hi, lo)	HILO_GEN(hi, lo, dma_addr_t)
#define HILO_64(hi, lo)		HILO_GEN(hi, lo, u64)
#define HILO_DMA_REGPAIR(regpair)	(HILO_DMA(regpair.hi, regpair.lo))
#define HILO_64_REGPAIR(regpair)	(HILO_64(regpair.hi, regpair.lo))

#ifndef USHRT_MAX
#define USHRT_MAX       ((u16)(~0U))
#endif

#endif
