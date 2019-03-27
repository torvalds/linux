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
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

/**
 * @file
 *
 * @brief This file contains the method implementations utilized in writing
 *        out PCI data for the SCI core.
 */

#include <dev/isci/scil/scic_user_callback.h>

#include <dev/isci/scil/scic_sds_pci.h>
#include <dev/isci/scil/scic_sds_controller.h>

/**
 * @brief This method reads from the driver the BARs that are needed to
 *        determine the virtual memory space for the controller registers
 *
 * @param[in] this_controller The controller for which to read the base
 *            address registers.
 */
void scic_sds_pci_bar_initialization(
   SCIC_SDS_CONTROLLER_T* this_controller
)
{
#ifdef ARLINGTON_BUILD

   #define ARLINGTON_LEX_BAR  0
   #define ARLINGTON_SMU_BAR  1
   #define ARLINGTON_SCU_BAR  2
   #define LEX_REGISTER_OFFSET 0x40000

   this_controller->lex_registers =
      ((char *)scic_cb_pci_get_bar(
                     this_controller, ARLINGTON_LEX_BAR) + LEX_REGISTER_OFFSET);
   this_controller->smu_registers =
      (SMU_REGISTERS_T *)scic_cb_pci_get_bar(this_controller, ARLINGTON_SMU_BAR);
   this_controller->scu_registers =
      (SCU_REGISTERS_T *)scic_cb_pci_get_bar(this_controller, ARLINGTON_SCU_BAR);

#else // !ARLINGTON_BUILD

#if !defined(ENABLE_PCI_IO_SPACE_ACCESS)

   this_controller->smu_registers =
      (SMU_REGISTERS_T *)(
         (char *)scic_cb_pci_get_bar(this_controller, PATSBURG_SMU_BAR)
                +(0x4000 * this_controller->controller_index));
   this_controller->scu_registers =
      (SCU_REGISTERS_T *)(
         (char *)scic_cb_pci_get_bar(this_controller, PATSBURG_SCU_BAR)
                +(0x400000 * this_controller->controller_index));

#else // !defined(ENABLE_PCI_IO_SPACE_ACCESS)

   if (this_controller->controller_index == 0)
   {
      this_controller->smu_registers = (SMU_REGISTERS_T *)
         scic_cb_pci_get_bar(this_controller, PATSBURG_IO_SPACE_BAR0);
   }
   else
   {
      if (this_controller->pci_revision == SCU_PBG_HBA_REV_B0)
      {
         // SCU B0 violates PCI spec for size of IO bar this is corrected
         // in subsequent version of the hardware so we can safely use the
         // else condition below.
         this_controller->smu_registers = (SMU_REGISTERS_T *)
            (scic_cb_pci_get_bar(this_controller, PATSBURG_IO_SPACE_BAR0) + 0x100);
      }
      else
      {
         this_controller->smu_registers = (SMU_REGISTERS_T *)
            scic_cb_pci_get_bar(this_controller, PATSBURG_IO_SPACE_BAR1);
      }
   }

   // No need to get the bar.  We will be using the offset to write to
   // input/output ports via 0xA0 and 0xA4.
   this_controller->scu_registers = (SCU_REGISTERS_T *) 0;

#endif // !defined(ENABLE_PCI_IO_SPACE_ACCESS)

#endif // ARLINGTON_BUILD
}

#if defined(ENABLE_PCI_IO_SPACE_ACCESS) && !defined(ARLINGTON_BUILD)

/**
 * @brief This method will read from PCI memory for the SMU register
 *        space via IO space access.
 *
 * @param[in]  controller The controller for which to read a DWORD.
 * @param[in]  address This parameter depicts the address from
 *             which to read.
 *
 * @return The value being returned from the PCI memory location.
 *
 * @todo This PCI memory access calls likely need to be optimized into macro?
 */
U32 scic_sds_pci_read_smu_dword(
   SCI_CONTROLLER_HANDLE_T   controller,
   void                    * address
)
{
   return scic_cb_pci_read_dword(controller, address);
}

/**
 * @brief This method will write to PCI memory for the SMU register
 *        space via IO space access.
 *
 * @param[in]  controller The controller for which to read a DWORD.
 * @param[in]  address This parameter depicts the address into
 *             which to write.
 * @param[out] write_value This parameter depicts the value being written
 *             into the PCI memory location.
 *
 * @todo This PCI memory access calls likely need to be optimized into macro?
 */
void scic_sds_pci_write_smu_dword(
   SCI_CONTROLLER_HANDLE_T   controller,
   void                    * address,
   U32                       write_value
)
{
   scic_cb_pci_write_dword(controller, address, write_value);
}

/**
 * @brief This method will read from PCI memory for the SCU register
 *        space via IO space access.
 *
 * @param[in]  controller The controller for which to read a DWORD.
 * @param[in]  address This parameter depicts the address from
 *             which to read.
 *
 * @return The value being returned from the PCI memory location.
 *
 * @todo This PCI memory access calls likely need to be optimized into macro?
 */
U32 scic_sds_pci_read_scu_dword(
   SCI_CONTROLLER_HANDLE_T   controller,
   void                    * address
)
{
   SCIC_SDS_CONTROLLER_T * this_controller = (SCIC_SDS_CONTROLLER_T*)controller;

   scic_cb_pci_write_dword(
      controller,
      (void*) ((char *)(this_controller->smu_registers) + SCU_MMR_ADDRESS_WINDOW_OFFSET),
      (U32) address
   );

   return scic_cb_pci_read_dword(
             controller,
             (void*) ((char *)(this_controller->smu_registers) + SCU_MMR_DATA_WINDOW_OFFSET)
          );
}

/**
 * @brief This method will write to PCI memory for the SCU register
 *        space via IO space access.
 *
 * @param[in]  controller The controller for which to read a DWORD.
 * @param[in]  address This parameter depicts the address into
 *             which to write.
 * @param[out] write_value This parameter depicts the value being written
 *             into the PCI memory location.
 *
 * @todo This PCI memory access calls likely need to be optimized into macro?
 */
void scic_sds_pci_write_scu_dword(
   SCI_CONTROLLER_HANDLE_T   controller,
   void                    * address,
   U32                       write_value
)
{
   SCIC_SDS_CONTROLLER_T * this_controller = (SCIC_SDS_CONTROLLER_T*)controller;

   scic_cb_pci_write_dword(
      controller,
      (void*) ((char *)(this_controller->smu_registers) + SCU_MMR_ADDRESS_WINDOW_OFFSET),
      (U32) address
   );

   scic_cb_pci_write_dword(
      controller,
      (void*) ((char *)(this_controller->smu_registers) + SCU_MMR_DATA_WINDOW_OFFSET),
      write_value
   );
}

#endif // defined(ENABLE_PCI_IO_SPACE_ACCESS)
