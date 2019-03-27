/*-
*******************************************************************************
Copyright (C) 2015 Annapurna Labs Ltd.

This file may be licensed under the terms of the Annapurna Labs Commercial
License Agreement.

Alternatively, this file can be distributed under the terms of the GNU General
Public License V2 as published by the Free Software Foundation and can be
found at http://www.gnu.org/licenses/gpl-2.0.html

Alternatively, redistribution and use in source and binary forms, with or
without modification, are permitted provided that the following conditions are
met:

    *     Redistributions of source code must retain the above copyright notice,
this list of conditions and the following disclaimer.

    *     Redistributions in binary form must reproduce the above copyright
notice, this list of conditions and the following disclaimer in
the documentation and/or other materials provided with the
distribution.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR
ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
(INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON
ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

*******************************************************************************/

/**
 * @defgroup group_udma_debug UDMA Debug
 * @ingroup group_udma_api
 *  UDMA Debug
 *  @{
 * @file   al_hal_udma_debug.h
 *
 * @brief C Header file for the Universal DMA HAL driver for debug APIs
 *
 */

#ifndef __AL_HAL_UDMA_DEBUG_H__
#define __AL_HAL_UDMA_DEBUG_H__

#include <al_hal_udma.h>

/* *INDENT-OFF* */
#ifdef __cplusplus
extern "C" {
#endif
/* *INDENT-ON* */

/* UDMA register print helper macros */
#define AL_UDMA_PRINT_REG(UDMA, PREFIX, POSTFIX, TYPE, GROUP, REG) \
	al_dbg(PREFIX #REG " = 0x%08x" POSTFIX, al_reg_read32( \
		&(UDMA->udma_regs->TYPE.GROUP.REG)))

#define AL_UDMA_PRINT_REG_FIELD( \
		UDMA, PREFIX, POSTFIX, FMT, TYPE, GROUP, REG, LBL, FIELD) \
	al_dbg(PREFIX #LBL " = " FMT POSTFIX, al_reg_read32( \
			&(UDMA->udma_regs->TYPE.GROUP.REG)) \
			& FIELD ## _MASK >> FIELD ## _SHIFT)

#define AL_UDMA_PRINT_REG_BIT( \
		UDMA, PREFIX, POSTFIX, TYPE, GROUP, REG, LBL, FIELD) \
	al_dbg(PREFIX #LBL " = %d" POSTFIX, ((al_reg_read32( \
			&(UDMA->udma_regs->TYPE.GROUP.REG)) \
			& FIELD) != 0))

/* UDMA register print mask definitions */
#define AL_UDMA_DEBUG_QUEUE(n)			AL_BIT(n)
#define AL_UDMA_DEBUG_AXI			AL_BIT(DMA_MAX_Q)
#define AL_UDMA_DEBUG_GENERAL			AL_BIT(DMA_MAX_Q + 1)
#define AL_UDMA_DEBUG_READ			AL_BIT(DMA_MAX_Q + 2)
#define AL_UDMA_DEBUG_WRITE			AL_BIT(DMA_MAX_Q + 3)
#define AL_UDMA_DEBUG_DWRR			AL_BIT(DMA_MAX_Q + 4)
#define AL_UDMA_DEBUG_RATE_LIMITER		AL_BIT(DMA_MAX_Q + 5)
#define AL_UDMA_DEBUG_STREAM_RATE_LIMITER	AL_BIT(DMA_MAX_Q + 6)
#define AL_UDMA_DEBUG_COMP			AL_BIT(DMA_MAX_Q + 7)
#define AL_UDMA_DEBUG_STAT			AL_BIT(DMA_MAX_Q + 8)
#define AL_UDMA_DEBUG_FEATURE			AL_BIT(DMA_MAX_Q + 9)
#define AL_UDMA_DEBUG_ALL			0xFFFFFFFF

/* Debug functions */

/**
 * Print udma registers according to the provided mask
 *
 * @param udma udma data structure
 * @param mask mask that specifies which registers groups to print
 * e.g. AL_UDMA_DEBUG_AXI prints AXI registers, AL_UDMA_DEBUG_ALL prints all
 * registers
 */
void al_udma_regs_print(struct al_udma *udma, unsigned int mask);

/**
 * Print udma queue software structure
 *
 * @param udma udma data structure
 * @param qid queue index
 */
void al_udma_q_struct_print(struct al_udma *udma, uint32_t qid);

/** UDMA ring type */
enum al_udma_ring_type {
	AL_RING_SUBMISSION,
	AL_RING_COMPLETION
};

/**
 * Print the ring entries for the specified queue index and ring type
 * (submission/completion)
 *
 * @param udma udma data structure
 * @param qid queue index
 * @param rtype udma ring type
 */
void al_udma_ring_print(struct al_udma *udma, uint32_t qid,
		enum al_udma_ring_type rtype);


/* *INDENT-OFF* */
#ifdef __cplusplus
}
#endif
/* *INDENT-ON* */
#endif /* __AL_HAL_UDMA_DEBUG_H__ */
/** @} end of UDMA debug group */
