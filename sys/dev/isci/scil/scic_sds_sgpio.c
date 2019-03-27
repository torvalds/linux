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
* @brief This file contains the implementation of the SGPIO register inteface
*        methods.
*/

#include <dev/isci/scil/scic_sgpio.h>
#include <dev/isci/scil/scic_sds_controller_registers.h>
#include <dev/isci/scil/scic_user_callback.h>

/**
 * @brief Function writes Value to the
 *        SGPIO Output Data Select Register (SGODSR) for phys specified by
 *        phy_mask paremeter
 *
 * @param[in] SCIC_SDS_CONTROLLER_T controller
 * @param[in] phy_mask - This field is a bit mask that specifies the phys
 *                       to be updated.
 * @param[in] value - Value for write
 *
 * @return none
 */
static
void scic_sgpio_write_SGODSR_register(
   SCIC_SDS_CONTROLLER_T *controller,
   U32 phy_mask,
   U32 value
)
{
   U8 phy_index;

   for (phy_index = 0; phy_index < SCI_MAX_PHYS; phy_index++)
   {
      if (phy_mask >> phy_index & 1)
      {
          scu_sgpio_peg0_register_write(
             controller, output_data_select[phy_index], value
          );
      }
   }
}

void scic_sgpio_set_vendor_code(
   SCI_CONTROLLER_HANDLE_T controller,
   U8 vendor_specific_sequence
)
{
   SCIC_SDS_CONTROLLER_T * core_controller = (SCIC_SDS_CONTROLLER_T *) controller;

   scu_sgpio_peg0_register_write(
      core_controller, vendor_specific_code, vendor_specific_sequence);
}

void scic_sgpio_set_blink_patterns(
   SCI_CONTROLLER_HANDLE_T controller,
   U8 pattern_a_low,
   U8 pattern_a_high,
   U8 pattern_b_low,
   U8 pattern_b_high
)
{
   U32 value;
   SCIC_SDS_CONTROLLER_T * core_controller = (SCIC_SDS_CONTROLLER_T *) controller;

   value = (pattern_b_high << 12) + (pattern_b_low << 8) + (pattern_a_high << 4) + pattern_a_low;

   scu_sgpio_peg0_register_write(
      core_controller, blink_rate, value);
}

void scic_sgpio_set_functionality(
   SCI_CONTROLLER_HANDLE_T controller,
   BOOL sgpio_mode
)
{
   U32 value = DISABLE_SGPIO_FUNCTIONALITY;
   SCIC_SDS_CONTROLLER_T * core_controller = (SCIC_SDS_CONTROLLER_T *) controller;

   if(sgpio_mode)
   {
      value = ENABLE_SGPIO_FUNCTIONALITY;
   }

   scu_sgpio_peg0_register_write(
      core_controller, interface_control, value);
}

void scic_sgpio_apply_led_blink_pattern(
   SCI_CONTROLLER_HANDLE_T controller,
   U32 phy_mask,
   BOOL error,
   BOOL locate,
   BOOL activity,
   U8 pattern_selection
)
{
   U32 output_value = 0;

   SCIC_SDS_CONTROLLER_T * core_controller = (SCIC_SDS_CONTROLLER_T *) controller;

   // Start with all LEDs turned off
   output_value = (SGODSR_INVERT_BIT <<  SGODSR_ERROR_LED_SHIFT)
                     | (SGODSR_INVERT_BIT <<  SGODSR_LOCATE_LED_SHIFT)
                     | (SGODSR_INVERT_BIT << SGODSR_ACTIVITY_LED_SHIFT);

   if(error)
   {  //apply pattern to error LED
      output_value |= pattern_selection << SGODSR_ERROR_LED_SHIFT;
      output_value &= ~(SGODSR_INVERT_BIT <<  SGODSR_ERROR_LED_SHIFT);
   }
   if(locate)
   {  //apply pattern to locate LED
      output_value |= pattern_selection << SGODSR_LOCATE_LED_SHIFT;
      output_value &= ~(SGODSR_INVERT_BIT <<  SGODSR_LOCATE_LED_SHIFT);
   }
   if(activity)
   {  //apply pattern to activity LED
      output_value |= pattern_selection << SGODSR_ACTIVITY_LED_SHIFT;
      output_value &= ~(SGODSR_INVERT_BIT << SGODSR_ACTIVITY_LED_SHIFT);
   }

   scic_sgpio_write_SGODSR_register(core_controller, phy_mask, output_value);
}

void scic_sgpio_set_led_blink_pattern(
   SCI_CONTROLLER_HANDLE_T controller,
   SCI_PORT_HANDLE_T port_handle,
   BOOL error,
   BOOL locate,
   BOOL activity,
   U8 pattern_selection
)
{
   U32 phy_mask;

   SCIC_SDS_PORT_T * port = (SCIC_SDS_PORT_T *) port_handle;

   phy_mask = scic_sds_port_get_phys(port);

   scic_sgpio_apply_led_blink_pattern(
           controller, phy_mask, error, locate, activity, pattern_selection);
}

void scic_sgpio_update_led_state(
   SCI_CONTROLLER_HANDLE_T controller,
   U32 phy_mask,
   BOOL error,
   BOOL locate,
   BOOL activity
)
{
   U32 output_value;

   SCIC_SDS_CONTROLLER_T * core_controller = (SCIC_SDS_CONTROLLER_T *) controller;

   // Start with all LEDs turned on
   output_value = 0x00000000;

   if(!error)
   {  //turn off error LED
      output_value |= SGODSR_INVERT_BIT <<  SGODSR_ERROR_LED_SHIFT;
   }
   if(!locate)
   {  //turn off locate LED
      output_value |= SGODSR_INVERT_BIT <<  SGODSR_LOCATE_LED_SHIFT;
   }
   if(!activity)
   {  //turn off activity LED
      output_value |= SGODSR_INVERT_BIT <<  SGODSR_ACTIVITY_LED_SHIFT;
   }

   scic_sgpio_write_SGODSR_register(core_controller, phy_mask, output_value);
}

void scic_sgpio_set_led_state(
   SCI_CONTROLLER_HANDLE_T controller,
   SCI_PORT_HANDLE_T port_handle,
   BOOL error,
   BOOL locate,
   BOOL activity
)
{
   U32 phy_mask;

   SCIC_SDS_PORT_T * port = (SCIC_SDS_PORT_T *) port_handle;

   phy_mask = scic_sds_port_get_phys(port);

   scic_sgpio_update_led_state(controller, phy_mask, error, locate, activity);
}

void scic_sgpio_set_to_hardware_control(
   SCI_CONTROLLER_HANDLE_T controller,
   BOOL is_hardware_controlled
)
{
   SCIC_SDS_CONTROLLER_T * core_controller = (SCIC_SDS_CONTROLLER_T *) controller;
   U8 i;
   U32 output_value;

   //turn on hardware control for LED's
   if(is_hardware_controlled)
   {
      output_value = SGPIO_HARDWARE_CONTROL;
   }
   else //turn off hardware control
   {
      output_value = SGPIO_SOFTWARE_CONTROL;
   }

   for(i = 0; i < SCI_MAX_PHYS; i++)
   {
      scu_sgpio_peg0_register_write(
         core_controller, output_data_select[i], output_value);
   }
}

U32 scic_sgpio_read(
   SCI_CONTROLLER_HANDLE_T controller
)
{
   //Not supported in the SCU hardware returning 0xFFFFFFFF
   return 0xffffffff;
}

void scic_sgpio_hardware_initialize(
   SCI_CONTROLLER_HANDLE_T controller
)
{
   scic_sgpio_set_functionality(controller, TRUE);
   scic_sgpio_set_to_hardware_control(controller, TRUE);
   scic_sgpio_set_vendor_code(controller, 0x00);
}

void scic_sgpio_initialize(
   SCI_CONTROLLER_HANDLE_T controller
)
{
   scic_sgpio_set_functionality(controller, TRUE);
   scic_sgpio_set_to_hardware_control(controller, FALSE);
   scic_sgpio_set_vendor_code(controller, 0x00);
}
