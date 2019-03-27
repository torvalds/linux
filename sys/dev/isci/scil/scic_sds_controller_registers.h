/*-
 * SPDX-License-Identifier: BSD-2-Clause OR GPL-2.0
 *
 * This file is provided under a dual BSD/GPLv2 license.  When using or
 * redistributing this file, you may do so under either license.
 *
 * GPL LICENSE SUMMARY
 *
 * Copyright(c) 2008 - 2011 Intel Corporation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St - Fifth Floor, Boston, MA 02110-1301 USA.
 * The full GNU General Public License is included in this distribution
 * in the file called LICENSE.GPL.
 *
 * BSD LICENSE
 *
 * Copyright(c) 2008 - 2011 Intel Corporation. All rights reserved.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 *   * Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 *   * Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in
 *     the documentation and/or other materials provided with the
 *     distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * $FreeBSD$
 */
#ifndef _SCIC_SDS_CONTROLLER_REGISTERS_H_
#define _SCIC_SDS_CONTROLLER_REGISTERS_H_

/**
 * @file
 *
 * @brief This file contains macros used to perform the register reads/writes
 *        to the SCU hardware.
 */

#ifdef __cplusplus
extern "C" {
#endif // __cplusplus

#include <dev/isci/scil/scu_registers.h>
#include <dev/isci/scil/scic_sds_controller.h>

/**
 * @name SMU_REGISTER_ACCESS_MACROS
 */
/*@{*/
#define scic_sds_controller_smu_register_read(controller, reg) \
   smu_register_read( \
      (controller), \
      (controller)->smu_registers->reg \
   )

#define scic_sds_controller_smu_register_write(controller, reg, value) \
   smu_register_write( \
      (controller), \
      (controller)->smu_registers->reg, \
      (value) \
   )
/*@}*/

/**
 * @name AFE_REGISTER_ACCESS_MACROS
 */
/*@{*/
#define scu_afe_register_write(controller, reg, value) \
   scu_register_write( \
      (controller), \
      (controller)->scu_registers->afe.reg, \
      (value) \
   )

#define scu_afe_register_read(controller, reg) \
   scu_register_read( \
      (controller), \
      (controller)->scu_registers->afe.reg \
   )
/*@}*/

/**
 * @name SGPIO_PEG0_REGISTER_ACCESS_MACROS
 */
/*@{*/
#define scu_sgpio_peg0_register_read(controller, reg) \
   scu_register_read( \
      (controller), \
      (controller)->scu_registers->peg0.sgpio.reg \
   )

#define scu_sgpio_peg0_register_write(controller, reg, value) \
   scu_register_write( \
      (controller), \
      (controller)->scu_registers->peg0.sgpio.reg, \
      (value) \
   )
/*@}*/

/**
 * @name VIIT_REGISTER_ACCESS_MACROS
 */
/*@{*/
#define scu_controller_viit_register_write(controller, index, reg, value) \
   scu_register_write( \
      (controller), \
      (controller)->scu_registers->peg0.viit[index].reg, \
      value \
   )
/*@}*/

/**
 * @name SCRATCH_RAM_REGISTER_ACCESS_MACROS
 */
/*@{*/
// Scratch RAM access may be needed before the scu_registers pointer
//  has been initialized.  So instead, explicitly cast BAR1 to a
//  SCU_REGISTERS_T data structure.

// Scratch RAM is stored in the Zoning Permission Table for OROM use.
#define scu_controller_scratch_ram_register_write(controller, index, value) \
   scu_register_write( \
      (controller), \
      ((SCU_REGISTERS_T *)scic_cb_pci_get_bar(controller, PATSBURG_SCU_BAR))->peg0.zpt0.table[index], \
      value \
   )

#define scu_controller_scratch_ram_register_read(controller, index) \
   scu_register_read( \
      (controller), \
      ((SCU_REGISTERS_T *)scic_cb_pci_get_bar(controller, PATSBURG_SCU_BAR))->peg0.zpt0.table[index] \
   )

#define scu_controller_scratch_ram_register_write_ext(controller, index, value) \
   scu_register_write( \
      (controller), \
      ((SCU_REGISTERS_T *)scic_cb_pci_get_bar(controller, PATSBURG_SCU_BAR))->peg0.zpt1.table[index], \
      value \
   )

#define scu_controller_scratch_ram_register_read_ext(controller, index) \
   scu_register_read( \
      (controller), \
      ((SCU_REGISTERS_T *)scic_cb_pci_get_bar(controller, PATSBURG_SCU_BAR))->peg0.zpt1.table[index] \
   )
/*@}*/


//*****************************************************************************
//* SMU REGISTERS
//*****************************************************************************

/**
 * @name SMU_REGISTERS
 */
/*@{*/
#define SMU_PCP_WRITE(controller, value) \
    scic_sds_controller_smu_register_write( \
       controller, post_context_port, value \
    )

#define SMU_TCR_READ(controller, value) \
    scic_sds_controller_smu_register_read( \
       controller, task_context_range \
    )

#define SMU_TCR_WRITE(controller, value) \
    scic_sds_controller_smu_register_write( \
       controller, task_context_range, value \
    )

#define SMU_HTTBAR_WRITE(controller, address) \
{ \
   scic_sds_controller_smu_register_write( \
      controller, \
      host_task_table_lower, \
      sci_cb_physical_address_lower(address) \
   );\
   scic_sds_controller_smu_register_write( \
      controller, \
      host_task_table_upper, \
      sci_cb_physical_address_upper(address) \
   ); \
}

#define SMU_CQBAR_WRITE(controller, address) \
{ \
   scic_sds_controller_smu_register_write( \
      controller, \
      completion_queue_lower, \
      sci_cb_physical_address_lower(address) \
   ); \
   scic_sds_controller_smu_register_write( \
      controller, \
      completion_queue_upper, \
      sci_cb_physical_address_upper(address) \
   ); \
}

#define SMU_CQGR_WRITE(controller, value) \
    scic_sds_controller_smu_register_write( \
       controller, completion_queue_get, value \
    )

#define SMU_CQGR_READ(controller, value) \
    scic_sds_controller_smu_register_read( \
       controller, completion_queue_get \
    )

#define SMU_CQPR_WRITE(controller, value) \
    scic_sds_controller_smu_register_write( \
       controller, completion_queue_put, value \
    )

#define SMU_RNCBAR_WRITE(controller, address) \
{ \
   scic_sds_controller_smu_register_write( \
      controller, \
      remote_node_context_lower, \
      sci_cb_physical_address_lower(address) \
   ); \
   scic_sds_controller_smu_register_write( \
      controller, \
      remote_node_context_upper, \
      sci_cb_physical_address_upper(address) \
   ); \
}

#define SMU_AMR_READ(controller) \
   scic_sds_controller_smu_register_read( \
      controller, address_modifier \
   )

#define SMU_IMR_READ(controller) \
   scic_sds_controller_smu_register_read( \
      controller, interrupt_mask \
   )

#define SMU_IMR_WRITE(controller, mask) \
   scic_sds_controller_smu_register_write( \
      controller, interrupt_mask, mask \
   )

#define SMU_ISR_READ(controller) \
   scic_sds_controller_smu_register_read( \
      controller, interrupt_status \
   )

#define SMU_ISR_WRITE(controller, status) \
   scic_sds_controller_smu_register_write( \
      controller, interrupt_status, status \
   )

#define SMU_ICC_READ(controller) \
   scic_sds_controller_smu_register_read( \
      controller, interrupt_coalesce_control \
   )

#define SMU_ICC_WRITE(controller, value) \
   scic_sds_controller_smu_register_write( \
      controller, interrupt_coalesce_control, value \
   )

#define SMU_CQC_WRITE(controller, value) \
    scic_sds_controller_smu_register_write( \
       controller, completion_queue_control, value \
    )

#define SMU_SMUSRCR_WRITE(controller, value) \
   scic_sds_controller_smu_register_write( \
      controller, soft_reset_control, value \
   )

#define SMU_TCA_WRITE(controller, index, value) \
    scic_sds_controller_smu_register_write( \
       controller, task_context_assignment[index], value \
    )

#define SMU_TCA_READ(controller, index) \
    scic_sds_controller_smu_register_read( \
       controller, task_context_assignment[index] \
    )

#define SMU_DCC_READ(controller) \
   scic_sds_controller_smu_register_read( \
      controller, device_context_capacity \
   )

#define SMU_DFC_READ(controller) \
   scic_sds_controller_smu_register_read( \
      controller, device_function_capacity \
   )

#define SMU_SMUCSR_READ(controller) \
   scic_sds_controller_smu_register_read( \
      controller, control_status \
   )

#define SMU_CGUCR_READ(controller) \
   scic_sds_controller_smu_register_read( \
      controller, clock_gating_control \
   )

#define SMU_CGUCR_WRITE(controller, value) \
   scic_sds_controller_smu_register_write( \
      controller, clock_gating_control, value \
   )

#define SMU_CQPR_READ(controller) \
    scic_sds_controller_smu_register_read( \
       controller, completion_queue_put \
    )

/*@}*/

/**
 * @name SCU_REGISTER_ACCESS_MACROS
 */
/*@{*/
#define scic_sds_controller_scu_register_read(controller, reg) \
   scu_register_read( \
      (controller), \
      (controller)->scu_registers->reg \
   )

#define scic_sds_controller_scu_register_write(controller, reg, value) \
   scu_register_write( \
      (controller), \
      (controller)->scu_registers->reg, \
      (value) \
   )
/*@}*/


//****************************************************************************
//*  SCU SDMA REGISTERS
//****************************************************************************

/**
 * @name SCU_SDMA_REGISTER_ACCESS_MACROS
 */
/*@{*/
#define scu_sdma_register_read(controller, reg) \
   scu_register_read( \
      (controller), \
      (controller)->scu_registers->sdma.reg \
   )

#define scu_sdma_register_write(controller, reg, value) \
   scu_register_write( \
      (controller), \
      (controller)->scu_registers->sdma.reg, \
      (value) \
   )
/*@}*/

/**
 * @name SCU_SDMA_REGISTERS
 */
/*@{*/
#define SCU_PUFATHAR_WRITE(controller, address) \
{ \
   scu_sdma_register_write( \
      controller, \
      uf_address_table_lower, \
      sci_cb_physical_address_lower(address) \
   ); \
   scu_sdma_register_write( \
      controller, \
      uf_address_table_upper, \
      sci_cb_physical_address_upper(address) \
   ); \
}

#define SCU_UFHBAR_WRITE(controller, address) \
{ \
   scu_sdma_register_write( \
      controller, \
      uf_header_base_address_lower, \
      sci_cb_physical_address_lower(address) \
   ); \
   scu_sdma_register_write( \
      controller, \
      uf_header_base_address_upper, \
      sci_cb_physical_address_upper(address) \
   ); \
}

#define SCU_UFQC_READ(controller) \
    scu_sdma_register_read( \
       controller,  \
       unsolicited_frame_queue_control \
    )

#define SCU_UFQC_WRITE(controller, value) \
    scu_sdma_register_write( \
       controller, \
       unsolicited_frame_queue_control, \
       value \
    )

#define SCU_UFQPP_READ(controller) \
    scu_sdma_register_read( \
       controller, \
       unsolicited_frame_put_pointer \
    )

#define SCU_UFQPP_WRITE(controller, value) \
   scu_sdma_register_write( \
      controller, \
      unsolicited_frame_put_pointer, \
      value \
   )

#define SCU_UFQGP_WRITE(controller, value) \
   scu_sdma_register_write( \
      controller, \
      unsolicited_frame_get_pointer, \
      value \
   )

#define SCU_PDMACR_READ(controller) \
   scu_sdma_register_read( \
      controller, \
      pdma_configuration \
   )

#define SCU_PDMACR_WRITE(controller, value) \
   scu_sdma_register_write( \
      controller, \
      pdma_configuration, \
      value \
   )

#define SCU_CDMACR_READ(controller) \
   scu_sdma_register_read( \
     controller, \
     cdma_configuration \
   )

#define SCU_CDMACR_WRITE(controller, value) \
   scu_sdma_register_write( \
      controller, \
      cdma_configuration, \
      value \
   )
/*@}*/

//*****************************************************************************
//* SCU CRAM AND FBRAM Registers
//*****************************************************************************
/**
 * @name SCU_CRAM_REGISTER_ACCESS_MACROS
 */
/*@{*/
#define scu_cram_register_read(controller, reg) \
   scu_register_read( \
      (controller), \
      (controller)->scu_registers->cram.reg \
   )

#define scu_cram_register_write(controller, reg, value) \
   scu_register_write( \
      (controller), \
      (controller)->scu_registers->cram.reg, \
      (value) \
   )
/*@}*/

/**
 * @name SCU_FBRAM_REGISTER_ACCESS_MACROS
 */
/*@{*/
#define scu_fbram_register_read(controller, reg) \
   scu_register_read( \
      (controller), \
      (controller)->scu_registers->fbram.reg \
   )

#define scu_fbram_register_write(controller, reg, value) \
   scu_register_write( \
      (controller), \
      (controller)->scu_registers->fbram.reg, \
      (value) \
   )
/*@}*/


/**
 * @name SCU_CRAM_REGISTERS
 */
/*@{*/

// SRAM ECC CONTROL REGISTER BITS
#define SIGNLE_BIT_ERROR_CORRECTION_ENABLE 0x00000001
#define MULTI_BIT_ERROR_REPORTING_ENABLE   0x00000002
#define SINGLE_BIT_ERROR_REPORTING_ENABLE  0x00000004

//SRAM ECC control register (SECR0)
#define SCU_SECR0_WRITE(controller, value) \
    scu_cram_register_write( \
      controller, \
      sram_ecc_control_0, \
      value \
   )
/*@}*/

/**
 * @name SCU_FBRAM_REGISTERS
 */
/*@{*/

//SRAM ECC control register (SECR1)
#define SCU_SECR1_WRITE(controller, value) \
    scu_fbram_register_write( \
      controller, \
      sram_ecc_control_1, \
      value \
   )
/*@}*/


//*****************************************************************************
//* SCU Port Task Scheduler Group Registers
//*****************************************************************************

/**
 * @name SCU_PTSG_REGISTER_ACCESS_MACROS
 */
/*@{*/
#define scu_ptsg_register_read(controller, reg) \
   scu_register_read( \
      (controller), \
      (controller)->scu_registers->peg0.ptsg.reg \
   )

#define scu_ptsg_register_write(controller, reg, value) \
   scu_register_write( \
      (controller), \
      (controller)->scu_registers->peg0.ptsg.reg, \
      (value) \
   )
/*@}*/

/**
 * @name SCU_PTSG_REGISTERS
 */
/*@{*/
#define SCU_PTSGCR_READ(controller) \
    scu_ptsg_register_read( \
       (controller), \
       control \
    )

#define SCU_PTSGCR_WRITE(controller, value) \
    scu_ptsg_register_write( \
       (controller), \
       control, \
       value \
    )

#define SCU_PTSGRTC_READ(controller) \
    scu_ptsg_register_read( \
       controller, \
       real_time_clock \
    )
/*@}*/

#ifdef __cplusplus
}
#endif // __cplusplus

#endif // _SCIC_SDS_CONTROLLER_REGISTERS_H_
