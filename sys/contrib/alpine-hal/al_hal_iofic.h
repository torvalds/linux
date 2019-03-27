/*-
********************************************************************************
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
 * @defgroup group_interrupts Common I/O Fabric Interrupt Controller
 * This HAL provides the API for programming the Common I/O Fabric Interrupt
 * Controller (IOFIC) found in most of the units attached to the I/O Fabric of
 * Alpine platform
 *  @{
 * @file   al_hal_iofic.h
 *
 * @brief Header file for the interrupt controller that's embedded in various units
 *
 */

#ifndef __AL_HAL_IOFIC_H__
#define __AL_HAL_IOFIC_H__

#include <al_hal_common.h>

/* *INDENT-OFF* */
#ifdef __cplusplus
extern "C" {
#endif
/* *INDENT-ON* */

#define AL_IOFIC_MAX_GROUPS	4

/*
 * Configurations
 */

/**
 * Configure the interrupt controller registers, actual interrupts are still
 * masked at this stage.
 *
 * @param regs_base regs pointer to interrupt controller registers
 * @param group the interrupt group.
 * @param flags flags of Interrupt Control Register
 *
 * @return 0 on success. -EINVAL otherwise.
 */
int al_iofic_config(void __iomem *regs_base, int group,
		   uint32_t flags);

/**
 * configure the moderation timer resolution for a given group
 * Applies for both msix and legacy mode.
 *
 * @param regs_base pointer to unit registers
 * @param group the interrupt group
 * @param resolution resolution of the timer interval, the resolution determines the rate
 * of decrementing the interval timer, setting value N means that the interval
 * timer will be decremented each (N+1) * (0.68) micro seconds.
 *
 * @return 0 on success. -EINVAL otherwise.
 */
int al_iofic_moder_res_config(void __iomem *regs_base, int group,
			     uint8_t resolution);

/**
 * configure the moderation timer interval for a given legacy interrupt group
 *
 * @param regs_base regs pointer to unit registers
 * @param group the interrupt group
 * @param interval between interrupts in resolution units. 0 disable
 *
 * @return 0 on success. -EINVAL otherwise.
 */
int al_iofic_legacy_moder_interval_config(void __iomem *regs_base, int group,
					 uint8_t interval);

/**
 * configure the moderation timer interval for a given msix vector
 *
 * @param regs_base pointer to unit registers
 * @param group the interrupt group
 * @param vector vector index
 * @param interval interval between interrupts, 0 disable
 *
 * @return 0 on success. -EINVAL otherwise.
 */
int al_iofic_msix_moder_interval_config(void __iomem *regs_base, int group,
				       uint8_t vector, uint8_t interval);

/**
* configure the tgtid attributes for a given msix vector.
*
* @param group the interrupt group
* @param vector index
* @param tgtid the target-id value
* @param tgtid_en take target-id from the intc
*
* @return 0 on success. -EINVAL otherwise.
*/
int al_iofic_msix_tgtid_attributes_config(void __iomem *regs_base, int group,
				       uint8_t vector, uint32_t tgtid, uint8_t tgtid_en);

/**
 * return the offset of the unmask register for a given group.
 * this function can be used when the upper layer wants to directly
 * access the unmask regiter and bypass the al_iofic_unmask() API.
 *
 * @param regs_base regs pointer to unit registers
 * @param group the interrupt group
 * @return the offset of the unmask register.
 */
uint32_t __iomem * al_iofic_unmask_offset_get(void __iomem *regs_base, int group);

/**
 * unmask specific interrupts for a given group
 * this functions guarantees atomic operations, it is performance optimized as
 * it will not require read-modify-write. The unmask done using the interrupt
 * mask clear register, so it's safe to call it while the mask is changed by
 * the HW (auto mask) or another core.
 *
 * @param regs_base pointer to unit registers
 * @param group the interrupt group
 * @param mask bitwise of interrupts to unmask, set bits will be unmasked.
 */
void al_iofic_unmask(void __iomem *regs_base, int group, uint32_t mask);

/**
 * mask specific interrupts for a given group
 * this functions modifies interrupt mask register, the callee must make sure
 * the mask is not changed by another cpu.
 *
 * @param regs_base pointer to unit registers
 * @param group the interrupt group
 * @param mask bitwise of interrupts to mask, set bits will be masked.
 */
void al_iofic_mask(void __iomem *regs_base, int group, uint32_t mask);

/**
 * read the mask register for a given group
 * this functions return the interrupt mask register
 *
 * @param regs_base pointer to unit registers
 * @param group the interrupt group
 */
uint32_t al_iofic_read_mask(void __iomem *regs_base, int group);

/**
 * read interrupt cause register for a given group
 * this will clear the set bits if the Clear on Read mode enabled.
 * @param regs_base pointer to unit registers
 * @param group the interrupt group
 */
uint32_t al_iofic_read_cause(void __iomem *regs_base, int group);

/**
 * clear bits in the interrupt cause register for a given group
 *
 * @param regs_base pointer to unit registers
 * @param group the interrupt group
 * @param mask bitwise of bits to be cleared, set bits will be cleared.
 */
void al_iofic_clear_cause(void __iomem *regs_base, int group, uint32_t mask);

/**
 * set the cause register for a given group
 * this function set the cause register. It will generate an interrupt (if
 * the the interrupt isn't masked )
 *
 * @param regs_base pointer to unit registers
 * @param group the interrupt group
 * @param mask bitwise of bits to be set.
 */
void al_iofic_set_cause(void __iomem *regs_base, int group, uint32_t mask);

/**
 * unmask specific interrupts from aborting the udma a given group
 *
 * @param regs_base pointer to unit registers
 * @param group the interrupt group
 * @param mask bitwise of interrupts to mask
 */
void al_iofic_abort_mask(void __iomem *regs_base, int group, uint32_t mask);

/**
 * trigger all interrupts that are waiting for moderation timers to expire
 *
 * @param regs_base pointer to unit registers
 * @param group the interrupt group
 */
void al_iofic_interrupt_moderation_reset(void __iomem *regs_base, int group);

#endif
/** @} end of interrupt controller group */
