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
#ifndef _SCIC_SGPIO_H_
#define _SCIC_SGPIO_H_

/**
* @file
*
* @brief This file contains all of the interface methods that can be called
*        by an SCI user on an Serialized General Purpose IO (SGPIO) object.
*/

#ifdef __cplusplus
extern "C" {
#endif // __cplusplus

#include <dev/isci/scil/sci_types.h>

//Programmable Blink Pattern Durations
#define SGPIO_BLINK_DURATION_125    0x0
#define SGPIO_BLINK_DURATION_250    0x1
#define SGPIO_BLINK_DURATION_375    0x2
#define SGPIO_BLINK_DURATION_500    0x3
#define SGPIO_BLINK_DURATION_625    0x4
#define SGPIO_BLINK_DURATION_750    0x5
#define SGPIO_BLINK_DURATION_875    0x6
#define SGPIO_BLINK_DURATION_1000   0x7
#define SGPIO_BLINK_DURATION_1250   0x8
#define SGPIO_BLINK_DURATION_1375   0x9
#define SGPIO_BLINK_DURATION_1500   0xA
#define SGPIO_BLINK_DURATION_1625   0xB
#define SGPIO_BLINK_DURATION_1750   0xC
#define SGPIO_BLINK_DURATION_1875   0xD
#define SGPIO_BLINK_DURATION_2000   0xF

#define ENABLE_SGPIO_FUNCTIONALITY  1
#define DISABLE_SGPIO_FUNCTIONALITY 0

#define SGPIO_HARDWARE_CONTROL      0x00000443
#define SGPIO_SOFTWARE_CONTROL      0x00000444

#define PHY_0_MASK                  0x01
#define PHY_1_MASK                  0x02
#define PHY_2_MASK                  0x04
#define PHY_3_MASK                  0x08

#define SGODSR_INVERT_BIT 0x4

#define SGODSR_ERROR_LED_SHIFT 8
#define SGODSR_LOCATE_LED_SHIFT 4
#define SGODSR_ACTIVITY_LED_SHIFT 0

#define SGPIO_BLINK_PATTERN_A 0x1
#define SGPIO_BLINK_PATTERN_B 0x2

/**
* @brief This will set the vendor specific code in the SGPIO Vendor Specific Code
*        register that is sent on the sLoad wire at the start of each
*        bit stream.
*
* @param[in] SCI_CONTROLLER_HANDLE_T controller
* @param]in] vendor_specific_sequence - Vendor specific sequence set in the
*         SGVSCR register.
*
*/
void scic_sgpio_set_vendor_code(
   SCI_CONTROLLER_HANDLE_T controller,
   U8 vendor_specific_sequence
);

/**
* @brief Use this to set both programmable blink patterns A & B in the
*        SGPBR(Programmable Blink Register). Will set identical patterns
*        on both SGPIO units.
*
* @param[in] SCI_CONTROLLER_HANDLE_T controller
* @param[in] pattern_a_high - High(LED on) duration time for pattern A
* @param[in] pattern_a_low - Low(LED off) duration time for pattern A
* @param[in] pattern_b_high - High(LED on) duration time for pattern B
* @param[in] pattern_b_low - Low(LED off) duration time for pattern B
*
*/
void scic_sgpio_set_blink_patterns(
   SCI_CONTROLLER_HANDLE_T controller,
   U8 pattern_a_low,
   U8 pattern_a_high,
   U8 pattern_b_low,
   U8 pattern_b_high
);


/**
* @brief This will set the functionality enable bit in the SGPIO interface
*        control register, when set the bus pins will be used for SGPIO
*        signaling, if not the bus pins are used for direct led control.
*
* @param[in] SCI_CONTROLLER_HANDLE_T controller
* @param[in] BOOL sgpio_mode - indication for SGPIO signaling.
*
*/
void scic_sgpio_set_functionality(
   SCI_CONTROLLER_HANDLE_T controller,
   BOOL sgpio_mode
);

/**
 * @brief Communicates with hardware to set the blink pattern
 *        of the error, locate, and activity LED's for phys
 *        specified by the phy_mask parameter.
 *        Function intend to be used to LEDs management
 *        in non-inteligent backplanes.
 *
 * @param[in] SCI_CONTROLLER_HANDLE_T controller
 * @param[in] phy_mask - This field is a bit mask that specifies the phys
 *                       to be updated.
 * @param[in] error - If TRUE, apply pattern for the error LED
 * @param[in] locate - If TRUE, apply pattern for the locate LED
 * @param[in] activity - If TRUE, apply pattern for the activity LED
 * @param[in] pattern_selection - One of two patterns (A or B).
 *            "A" and "B" patterns should be previously defined
 *            by scic_sgpio_set_blink_patterns()
 *
 * @return none
 */
void scic_sgpio_apply_led_blink_pattern(
   SCI_CONTROLLER_HANDLE_T controller,
   U32 phy_mask,
   BOOL error,
   BOOL locate,
   BOOL activity,
   U8 pattern_selection
);

/**
 * @brief Communicates with hardware to set the blink pattern
 *        of the error, locate, and activity LED's for all
 *        phys in the port. Function intend to be used to LEDs
 *        management in non-inteligent backplanes.
 *
 * @param[in] SCI_CONTROLLER_HANDLE_T controller
 * @param[in] port_handle - port handle is used to identify SGPIO bay
 * @param[in] error - If TRUE, apply pattern for the error LED
 * @param[in] locate - If TRUE, apply pattern for the locate LED
 * @param[in] activity - If TRUE, apply pattern for the activity LED
 * @param[in] pattern_selection - One of two patterns (A or B).
 *            "A" and "B" patterns should be previously defined
 *            by scic_sgpio_set_blink_patterns()
 *
 * @return none
 */
void scic_sgpio_set_led_blink_pattern(
   SCI_CONTROLLER_HANDLE_T controller,
   SCI_PORT_HANDLE_T port_handle,
   BOOL error,
   BOOL locate,
   BOOL activity,
   U8 pattern_selection
);

/**
 * @brief Communicates with hardware to set the state of the error, locate,
 *        and activity LED's.
 *
 * @param[in] SCI_CONTROLLER_HANDLE_T controller
 * @param[in] phy_mask - This field is a bit mask that specifies the phys
 *                       to be updated.
 * @param[in] error - State to be set for the error LED
 * @param[in] locate - State to be set for the locate LED
 * @param[in] activity - State to be set for the activity LED
 *
 * @return none
 */
void scic_sgpio_update_led_state(
   SCI_CONTROLLER_HANDLE_T controller,
   U32 phy_mask,
   BOOL error,
   BOOL locate,
   BOOL activity
);

/**
 * @brief Communicates with hardware to set the state of the error, locate,
 *        and activity LED's for all phys in the port.
 *
 * @param[in] SCI_CONTROLLER_HANDLE_T controller
 * @param[in] port_handle - port handle is used to identify SGPIO bay
 * @param[in] error - State to be set for the error LED
 * @param[in] locate - State to be set for the locate LED
 * @param[in] activity - State to be set for the activity LED
 *
 * @return none
 */
void scic_sgpio_set_led_state(
   SCI_CONTROLLER_HANDLE_T controller,
   SCI_PORT_HANDLE_T port_handle,
   BOOL error,
   BOOL locate,
   BOOL activity
);

/**
 * @brief This will set all Activity LED's to hardware controlled
 *
 * @param[in] BOOL is_hardware_controlled - indication for the Activity LED's
 *         to be hardware controlled or driver controlled.
 * @return none
 */
void scic_sgpio_set_to_hardware_control(
   SCI_CONTROLLER_HANDLE_T controller,
   BOOL is_hardware_controlled
);

/**
 * @brief Reads and returns the data-in from the SGPIO port for the specified controller.
 *        Bits 00:02 - Drive 0 input data
 *        Bits 04:06 - Drive 1 input data
 *        Bits 08:10 - Drive 2 input data
 *        Bits 12:14 - Drive 3 input data
 *
 * @param[in] SCI_CONTROLLER_HANDLE_T controller
 * @return U32 - Value read from SGPIO, 0xffffffff indicates hardware not readable
 */
U32 scic_sgpio_read(
   SCI_CONTROLLER_HANDLE_T controller
);

/**
 * @brief Initializes the SCU for Hardware SGPIO LED control.
 *
 * @param[in] SCI_CONTROLLER_HANDLE_T controller
 */
void scic_sgpio_hardware_initialize(
   SCI_CONTROLLER_HANDLE_T controller
);
/**
* @brief Initializes the SCU for Software SGPIO signaling of LED control.
*
* @param[in] SCI_CONTROLLER_HANDLE_T controller
*/
void scic_sgpio_initialize(
   SCI_CONTROLLER_HANDLE_T controller
);

#ifdef __cplusplus
}
#endif // __cplusplus

#endif // _SCIC_SGPIO_H_
