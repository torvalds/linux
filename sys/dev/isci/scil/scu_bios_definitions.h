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
#ifndef _SCU_BIOS_DEFINITIONS_H_
#define _SCU_BIOS_DEFINITIONS_H_

#ifdef __cplusplus
extern "C" {
#endif // __cplusplus

/**
 *  IMPORTANT NOTE:
 *  This file can be used by an SCI Library based driver or
 *  stand-alone where the library is excluded.  By excluding
 *  the SCI Library, inclusion of OS specific header files can
 *  be avoided.  For example, a BIOS utility probably does not
 *  want to be bothered with inclusion of nested OS DDK include
 *  files that are not necessary for its function.
 *
 *  To exclude the SCI Library, either uncomment the EXCLUDE_SCI_LIBRARY
 *  #define statement in environment.h or define the statement as an input
 *  to your compiler.
 */

#include <dev/isci/environment.h>

#ifndef EXCLUDE_SCI_LIBRARY
#include <dev/isci/scil/sci_types.h>
#include <dev/isci/scil/intel_sas.h>
#include <dev/isci/scil/sci_controller_constants.h>
#endif /* EXCLUDE_SCI_LIBRARY */



// For Intel Storage Controller Unit OEM Block
#define SCI_OEM_PARAM_SIGNATURE     "ISCUOEMB"

#define SCI_PREBOOT_SOURCE_INIT     (0x00)
#define SCI_PREBOOT_SOURCE_OROM     (0x80)
#define SCI_PREBOOT_SOURCE_EFI      (0x81)

#define SCI_OEM_PARAM_VER_1_0       (0x10)
#define SCI_OEM_PARAM_VER_1_1       (0x11)
#define SCI_OEM_PARAM_VER_1_2       (0x12)
#define SCI_OEM_PARAM_VER_1_3       (0x13)

// current version
#define SCI_OEM_PARAM_VER_CUR       SCI_OEM_PARAM_VER_1_3

// port configuration mode
#define SCI_BIOS_MODE_MPC   (0x00)
#define SCI_BIOS_MODE_APC   (0x01)


#ifndef SCI_MAX_PHYS
#define SCI_MAX_PHYS (4)
#endif

#ifndef SCI_MAX_PORTS
#define SCI_MAX_PORTS (4)
#endif


/**
 * @struct SCI_BIOS_OEM_PARAM_BLOCK_HDR
 *
 * @brief This structure defines the OEM Parameter block header.
 */
typedef struct SCI_BIOS_OEM_PARAM_BLOCK_HDR
{
    /**
     * This field contains the OEM Parameter Block Signature which is
     * used by BIOS and driver software to identify that the memory location
     * contains valid OEM Parameter data.  The value must be set to
     * SCI_OEM_PARAM_SIGNATURE which is the string "ISCUOEMB" which
     * stands for Intel Storage Controller Unit OEM Block.
     */
    U8 signature[8];
    /**
     * This field contains the size in bytes of the complete OEM
     * Parameter Block, both header and payload hdr_length +
     * (num_elements * element_length).
     */
    U16 total_block_length;
    /**
     * This field contains the size in bytes of the
     * SCI_BIOS_OEM_PARAM_BLOCK_HDR. It also indicates the offset from
     * the beginning of this data structure to where the actual
     * parameter data payload begins.
     */
    U8 hdr_length;
    /**
     * This field contains the version info defining the structure
     * of the OEM Parameter block.
     */
    U8  version;
    /**
     * This field contains a value indicating the preboot initialization
     * method (Option ROM or UEFI driver) so that after OS transition,
     * the OS driver can know the preboot method. OEMs who build a single
     * flash image where the preboot method is unknown at manufacturing
     * time should set this field to SCI_PREBOOT_SOURCE_INIT.  Then
     * after the block is retrieved into host memory and under preboot
     * driver control, the OROM or UEFI driver can set this field
     * appropriately (SCI_PREBOOT_SOURCE_OROM and SCI_PREBOOT_SOURCE_EFI,
     * respectively).
     */
    U8 preboot_source;
    /**
     * This field contains the number of parameter descriptor elements
     * (i.e. controller_elements) following this header.  The number of
     * elements corresponds to the number of SCU controller units contained
     * in the platform:
     *  controller_element[0] = SCU0
     *  controller_element[1] = SCU1
     */
    U8 num_elements;
    /**
     * This field contains the size in bytes of the descriptor element(s)
     * in the block.
     */
    U16 element_length;
    /**
     * Reserve fields for future use.
     */
    U8 reserved[8];

} SCI_BIOS_OEM_PARAM_BLOCK_HDR_T;


/**
 * @struct SCIC_SDS_OEM_PARAMETERS VER 1.0
 *
 * @brief This structure delineates the various OEM parameters that must
 * be set for the Intel SAS Storage Controller Unit (SCU).
 */
typedef struct SCI_BIOS_OEM_PARAM_ELEMENT
{
    /**
     * Per SCU Controller Data
     */
    struct
    {
        /**
         * This field indicates the port configuration mode for
         * this controller:
         *   Automatic Port Configuration(APC) or
         *   Manual Port Configuration (MPC).
         *
         * APC means the Platform OEM expects SCI to configure
         * SAS Ports automatically according to the discovered SAS
         * Address pairs of the endpoints, wide and/or narrow.
         *
         * MPC means the Platform OEM manually defines wide or narrow
         * connectors by apriori assigning PHYs to SAS Ports.
         *
         * By default, the mode type is APC
         * in APC mode, if ANY of the phy mask is non-zero,
         *    SCI_FAILURE_INVALID_PARAMETER_VALUE will be returned
         *      from scic_oem_parameters_set AND the default oem
         *      configuration will be applied
         * in MPC mode, if ALL of the phy masks are zero,
         *    SCI_FAILURE_INVALID_PARAMETER_VALUE will be returned
         *      from scic_oem_parameters_set AND the default oem
         *      configuration will be applied
         */
        U8  mode_type;

        /**
         * This field specifies the maximum number of direct attached
         * devices the OEM will allow to have powered up simultaneously
         * on this controller.  This allows the OEM to avoid exceeding
         * power supply limits for this platform.  A value of zero
         * indicates there are no restrictions.
         */
        U8  max_number_concurrent_device_spin_up;

        /**
         * This field indicates OEM's desired default
         * Spread Spectrum Clocking (SSC) setting for Tx:
         *   enabled     = 1
         *   disabled    = 0
         */
        U8 do_enable_ssc;

        U8 reserved;

    } controller;

    /**
     * Per SAS Port data.
     */
    struct
    {
        /**
         * This field specifies the phys to be contained inside a port.
         * The bit position in the mask specifies the index of the phy
         * to be contained in the port.  Multiple bits (i.e. phys)
        * can be contained in a single port:
        *      Bit 0 = This controller's PHY index 0     (0x01)
        *      Bit 1 = This controller's PHY index 1     (0x02)
        *      Bit 2 = This controller's PHY index 2     (0x04)
        *      Bit 3 = This controller's PHY index 3     (0x08)
        *
        * Refer to the mode_type field for rules regarding APC and MPC mode.
        * General rule: For APC mode phy_mask = 0
        */
        U8 phy_mask;

    } ports[SCI_MAX_PORTS]; // Up to 4 Ports per SCU controller unit

    /**
     * Per PHY Parameter data.
     */
    struct
    {
        /**
         * This field indicates the SAS Address that will be transmitted on
         * this PHY index.  The field is defined as a union, however, the
         * OEM should use the U8 array definition when encoding it to ensure
         * correct byte ordering.
         *
         * NOTE: If using APC MODE, along with phy_mask being set to ZERO, the
         * SAS Addresses for all PHYs within a controller group SHALL be the
         * same.
         */
        union
        {
            /**
             * The array should be stored in little endian order.  For example,
             * if the desired SAS Address is 0x50010B90_0003538D, then it
             * should be stored in the following manner:
             *    array[0] = 0x90
             *    array[1] = 0x0B
             *    array[2] = 0x01
             *    array[3] = 0x50
             *    array[4] = 0x8D
             *    array[5] = 0x53
             *    array[6] = 0x03
             *    array[7] = 0x00
             */
            U8 array[8];
            /**
             * This is the typedef'd version of the SAS Address used in
             * the SCI Library.
             */
            SCI_SAS_ADDRESS_T  sci_format;

        } sas_address;

        /**
         * These are the per PHY equalization settings associated with the
         * AFE XCVR Tx Amplitude and Equalization Control Register Set
         * (0 thru 3).
         *
         * Operational Note: The following Look-Up-Table registers are engaged
         * by the AFE block after the following:
         *  - Software programs the Link Layer AFE Look Up Table Control
         *    Registers (AFE_LUTCR).
         *  - Software sets AFE XCVR Tx Control Register Tx Equalization
         *    Enable bit.
         */
        /**
         * AFE_TX_AMP_CTRL0.  This register is associated with AFE_LUTCR
         * LUTSel=00b. It contains the Tx Equalization settings that will be
         * used if a SATA 1.5Gbs or SATA 3.0Gbs device is direct-attached.
         */
        U32 afe_tx_amp_control0;

        /**
         * AFE_TX_AMP_CTRL1.  This register is associated with AFE_LUTCR
         * LUTSel=01b. It contains the Tx Equalization settings that will
         * be used if a SATA 6.0Gbs device is direct-attached.
         */
        U32 afe_tx_amp_control1;

        /**
         * AFE_TX_AMP_CTRL2.  This register is associated with AFE_LUTCR
         * LUTSel=10b. It contains the Tx Equalization settings that will
         * be used if a SAS 1.5Gbs or SAS 3.0Gbs device is direct-attached.
         */
        U32 afe_tx_amp_control2;

        /**
         * AFE_TX_AMP_CTRL3.  This register is associated with AFE_LUTCR
         * LUTSel=11b. It contains the Tx Equalization settings that will
         * be used if a SAS 6.0Gbs device is direct-attached.
         */
        U32 afe_tx_amp_control3;

    } phys[SCI_MAX_PHYS];   // 4 PHYs per SCU controller unit

} SCI_BIOS_OEM_PARAM_ELEMENT_T;

/**
 * @struct SCIC_SDS_OEM_PARAMETERS VER 1.1
 *
 * @brief This structure delineates the various OEM parameters that must
 * be set for the Intel SAS Storage Controller Unit (SCU).
 */
typedef struct SCI_BIOS_OEM_PARAM_ELEMENT_v_1_1
{
    /**
     * Per SCU Controller Data
     */
    struct
    {
        /**
         * This field indicates the port configuration mode for
         * this controller:
         *   Automatic Port Configuration(APC) or
         *   Manual Port Configuration (MPC).
         *
         * APC means the Platform OEM expects SCI to configure
         * SAS Ports automatically according to the discovered SAS
         * Address pairs of the endpoints, wide and/or narrow.
         *
         * MPC means the Platform OEM manually defines wide or narrow
         * connectors by apriori assigning PHYs to SAS Ports.
         *
         * By default, the mode type is APC
         * in APC mode, if ANY of the phy mask is non-zero,
         *    SCI_FAILURE_INVALID_PARAMETER_VALUE will be returned
         *      from scic_oem_parameters_set AND the default oem
         *      configuration will be applied
         * in MPC mode, if ALL of the phy masks are zero,
         *    SCI_FAILURE_INVALID_PARAMETER_VALUE will be returned
         *      from scic_oem_parameters_set AND the default oem
         *      configuration will be applied
         */
        U8  mode_type;

        /**
         * This field specifies the maximum number of direct attached
         * devices the OEM will allow to have powered up simultaneously
         * on this controller.  This allows the OEM to avoid exceeding
         * power supply limits for this platform.  A value of zero
         * indicates there are no restrictions.
         */
        U8  max_number_concurrent_device_spin_up;

        /**
         * This bitfield indicates the OEM's desired default Tx
         * Spread Spectrum Clocking (SSC) settings for SATA and SAS.
         * NOTE: Default SSC Modulation Frequency is 31.5KHz.
         *--------------------------------------------------------------------*/
        /**
         * NOTE: Max spread for SATA is +0 / -5000 PPM.
         * Down-spreading SSC (only method allowed for SATA):
         *  SATA SSC Tx Disabled                    = 0x0
         *  SATA SSC Tx at +0 / -1419 PPM Spread    = 0x2
         *  SATA SSC Tx at +0 / -2129 PPM Spread    = 0x3
         *  SATA SSC Tx at +0 / -4257 PPM Spread    = 0x6
         *  SATA SSC Tx at +0 / -4967 PPM Spread    = 0x7
        */
        U8 ssc_sata_tx_spread_level : 4;

        /**
         * SAS SSC Tx Disabled                     = 0x0
         *
         * NOTE: Max spread for SAS down-spreading +0 / -2300 PPM
         * Down-spreading SSC:
         *  SAS SSC Tx at +0 / -1419 PPM Spread     = 0x2
         *  SAS SSC Tx at +0 / -2129 PPM Spread     = 0x3
         *
         * NOTE: Max spread for SAS center-spreading +2300 / -2300 PPM
         * Center-spreading SSC:
         *  SAS SSC Tx at +1064 / -1064 PPM Spread  = 0x3
         *  SAS SSC Tx at +2129 / -2129 PPM Spread  = 0x6
         */
        U8 ssc_sas_tx_spread_level : 3;
        /**
         * NOTE: Refer to the SSC section of the SAS 2.x Specification
         * for proper setting of this field. For standard SAS Initiator
         * SAS PHY operation it should be 0 for Down-spreading.
         * SAS SSC Tx spread type:
         *  Down-spreading SSC      = 0
         *  Center-spreading SSC    = 1
         */
        U8 ssc_sas_tx_type : 1;
        /*--------------------------------------------------------------------*/

        U8 reserved;

    } controller;

    /**
     * Per SAS Port data.
     */
    struct
    {
        /**
         * This field specifies the phys to be contained inside a port.
         * The bit position in the mask specifies the index of the phy
         * to be contained in the port.  Multiple bits (i.e. phys)
        * can be contained in a single port:
        *      Bit 0 = This controller's PHY index 0     (0x01)
        *      Bit 1 = This controller's PHY index 1     (0x02)
        *      Bit 2 = This controller's PHY index 2     (0x04)
        *      Bit 3 = This controller's PHY index 3     (0x08)
        *
        * Refer to the mode_type field for rules regarding APC and MPC mode.
        * General rule: For APC mode phy_mask = 0
        */
        U8 phy_mask;

    } ports[SCI_MAX_PORTS]; // Up to 4 Ports per SCU controller unit

    /**
     * Per PHY Parameter data.
     */
    struct
    {
        /**
         * This field indicates the SAS Address that will be transmitted on
         * this PHY index.  The field is defined as a union, however, the
         * OEM should use the U8 array definition when encoding it to ensure
         * correct byte ordering.
         *
         * NOTE: If using APC MODE, along with phy_mask being set to ZERO, the
         * SAS Addresses for all PHYs within a controller group SHALL be the
         * same.
         */
        union
        {
            /**
             * The array should be stored in little endian order.  For example,
             * if the desired SAS Address is 0x50010B90_0003538D, then it
             * should be stored in the following manner:
             *    array[0] = 0x90
             *    array[1] = 0x0B
             *    array[2] = 0x01
             *    array[3] = 0x50
             *    array[4] = 0x8D
             *    array[5] = 0x53
             *    array[6] = 0x03
             *    array[7] = 0x00
             */
            U8 array[8];
            /**
             * This is the typedef'd version of the SAS Address used in
             * the SCI Library.
             */
            SCI_SAS_ADDRESS_T  sci_format;

        } sas_address;

        /**
         * These are the per PHY equalization settings associated with the
         * AFE XCVR Tx Amplitude and Equalization Control Register Set
         * (0 thru 3).
         *
         * Operational Note: The following Look-Up-Table registers are engaged
         * by the AFE block after the following:
         *  - Software programs the Link Layer AFE Look Up Table Control
         *    Registers (AFE_LUTCR).
         *  - Software sets AFE XCVR Tx Control Register Tx Equalization
         *    Enable bit.
         */
        /**
         * AFE_TX_AMP_CTRL0.  This register is associated with AFE_LUTCR
         * LUTSel=00b. It contains the Tx Equalization settings that will be
         * used if a SATA 1.5Gbs or SATA 3.0Gbs device is direct-attached.
         */
        U32 afe_tx_amp_control0;

        /**
         * AFE_TX_AMP_CTRL1.  This register is associated with AFE_LUTCR
         * LUTSel=01b. It contains the Tx Equalization settings that will
         * be used if a SATA 6.0Gbs device is direct-attached.
         */
        U32 afe_tx_amp_control1;

        /**
         * AFE_TX_AMP_CTRL2.  This register is associated with AFE_LUTCR
         * LUTSel=10b. It contains the Tx Equalization settings that will
         * be used if a SAS 1.5Gbs or SAS 3.0Gbs device is direct-attached.
         */
        U32 afe_tx_amp_control2;

        /**
         * AFE_TX_AMP_CTRL3.  This register is associated with AFE_LUTCR
         * LUTSel=11b. It contains the Tx Equalization settings that will
         * be used if a SAS 6.0Gbs device is direct-attached.
         */
        U32 afe_tx_amp_control3;

    } phys[SCI_MAX_PHYS];   // 4 PHYs per SCU controller unit

} SCI_BIOS_OEM_PARAM_ELEMENT_v_1_1_T;

/**
 * @struct SCIC_SDS_OEM_PARAMETERS VER 1.2
 *
 * @brief This structure delineates the various OEM parameters that must
 * be set for the Intel SAS Storage Controller Unit (SCU).
 */
typedef struct SCI_BIOS_OEM_PARAM_ELEMENT_v_1_2
{
    /**
     * Per SCU Controller Data
     */
    struct
    {
        /**
         * This field indicates the port configuration mode for
         * this controller:
         *   Automatic Port Configuration(APC) or
         *   Manual Port Configuration (MPC).
         *
         * APC means the Platform OEM expects SCI to configure
         * SAS Ports automatically according to the discovered SAS
         * Address pairs of the endpoints, wide and/or narrow.
         *
         * MPC means the Platform OEM manually defines wide or narrow
         * connectors by apriori assigning PHYs to SAS Ports.
         *
         * By default, the mode type is APC
         * in APC mode, if ANY of the phy mask is non-zero,
         *    SCI_FAILURE_INVALID_PARAMETER_VALUE will be returned
         *      from scic_oem_parameters_set AND the default oem
         *      configuration will be applied
         * in MPC mode, if ALL of the phy masks are zero,
         *    SCI_FAILURE_INVALID_PARAMETER_VALUE will be returned
         *      from scic_oem_parameters_set AND the default oem
         *      configuration will be applied
         */
        U8  mode_type;

        /**
         * This field specifies the maximum number of direct attached
         * devices the OEM will allow to have powered up simultaneously
         * on this controller.  This allows the OEM to avoid exceeding
         * power supply limits for this platform.  A value of zero
         * indicates there are no restrictions.
         */
        U8  max_number_concurrent_device_spin_up;

        /**
         * This bitfield indicates the OEM's desired default Tx
         * Spread Spectrum Clocking (SSC) settings for SATA and SAS.
         * NOTE: Default SSC Modulation Frequency is 31.5KHz.
         *--------------------------------------------------------------------*/
        /**
         * NOTE: Max spread for SATA is +0 / -5000 PPM.
         * Down-spreading SSC (only method allowed for SATA):
         *  SATA SSC Tx Disabled                    = 0x0
         *  SATA SSC Tx at +0 / -1419 PPM Spread    = 0x2
         *  SATA SSC Tx at +0 / -2129 PPM Spread    = 0x3
         *  SATA SSC Tx at +0 / -4257 PPM Spread    = 0x6
         *  SATA SSC Tx at +0 / -4967 PPM Spread    = 0x7
        */
        U8 ssc_sata_tx_spread_level : 4;

        /**
         * SAS SSC Tx Disabled                     = 0x0
         *
         * NOTE: Max spread for SAS down-spreading +0 / -2300 PPM
         * Down-spreading SSC:
         *  SAS SSC Tx at +0 / -1419 PPM Spread     = 0x2
         *  SAS SSC Tx at +0 / -2129 PPM Spread     = 0x3
         *
         * NOTE: Max spread for SAS center-spreading +2300 / -2300 PPM
         * Center-spreading SSC:
         *  SAS SSC Tx at +1064 / -1064 PPM Spread  = 0x3
         *  SAS SSC Tx at +2129 / -2129 PPM Spread  = 0x6
         */
        U8 ssc_sas_tx_spread_level : 3;
        /**
         * NOTE: Refer to the SSC section of the SAS 2.x Specification
         * for proper setting of this field. For standard SAS Initiator
         * SAS PHY operation it should be 0 for Down-spreading.
         * SAS SSC Tx spread type:
         *  Down-spreading SSC      = 0
         *  Center-spreading SSC    = 1
         */
        U8 ssc_sas_tx_type : 1;

        /**
         * This field indicates length of the SAS/SATA cable between
         * host and device.
         * This field is used make relationship between analog parameters of
         * the phy in the silicon and length of the cable.
         * Supported length: "short"- up to 3m, "long"- more than 3m
         * This is bit mask field:
         *
         * BIT:           7  6  5  4   3     2     1     0 (LSB)
         * ASSIGNMENT:   <-><-><-><-><phy3><phy2><phy1><phy0>
         *
         * For short cable corresponding bit shall be reset,
         * for long cable shall be set.
         */
        U8 long_cable_selection_mask;

    } controller;

    /**
     * Per SAS Port data.
     */
    struct
    {
        /**
         * This field specifies the phys to be contained inside a port.
         * The bit position in the mask specifies the index of the phy
         * to be contained in the port.  Multiple bits (i.e. phys)
        * can be contained in a single port:
        *      Bit 0 = This controller's PHY index 0     (0x01)
        *      Bit 1 = This controller's PHY index 1     (0x02)
        *      Bit 2 = This controller's PHY index 2     (0x04)
        *      Bit 3 = This controller's PHY index 3     (0x08)
        *
        * Refer to the mode_type field for rules regarding APC and MPC mode.
        * General rule: For APC mode phy_mask = 0
        */
        U8 phy_mask;

    } ports[SCI_MAX_PORTS]; // Up to 4 Ports per SCU controller unit

    /**
     * Per PHY Parameter data.
     */
    struct
    {
        /**
         * This field indicates the SAS Address that will be transmitted on
         * this PHY index.  The field is defined as a union, however, the
         * OEM should use the U8 array definition when encoding it to ensure
         * correct byte ordering.
         *
         * NOTE: If using APC MODE, along with phy_mask being set to ZERO, the
         * SAS Addresses for all PHYs within a controller group SHALL be the
         * same.
         */
        union
        {
            /**
             * The array should be stored in little endian order.  For example,
             * if the desired SAS Address is 0x50010B90_0003538D, then it
             * should be stored in the following manner:
             *    array[0] = 0x90
             *    array[1] = 0x0B
             *    array[2] = 0x01
             *    array[3] = 0x50
             *    array[4] = 0x8D
             *    array[5] = 0x53
             *    array[6] = 0x03
             *    array[7] = 0x00
             */
            U8 array[8];
            /**
             * This is the typedef'd version of the SAS Address used in
             * the SCI Library.
             */
            SCI_SAS_ADDRESS_T  sci_format;

        } sas_address;

        /**
         * These are the per PHY equalization settings associated with the
         * AFE XCVR Tx Amplitude and Equalization Control Register Set
         * (0 thru 3).
         *
         * Operational Note: The following Look-Up-Table registers are engaged
         * by the AFE block after the following:
         *  - Software programs the Link Layer AFE Look Up Table Control
         *    Registers (AFE_LUTCR).
         *  - Software sets AFE XCVR Tx Control Register Tx Equalization
         *    Enable bit.
         */
        /**
         * AFE_TX_AMP_CTRL0.  This register is associated with AFE_LUTCR
         * LUTSel=00b. It contains the Tx Equalization settings that will be
         * used if a SATA 1.5Gbs or SATA 3.0Gbs device is direct-attached.
         */
        U32 afe_tx_amp_control0;

        /**
         * AFE_TX_AMP_CTRL1.  This register is associated with AFE_LUTCR
         * LUTSel=01b. It contains the Tx Equalization settings that will
         * be used if a SATA 6.0Gbs device is direct-attached.
         */
        U32 afe_tx_amp_control1;

        /**
         * AFE_TX_AMP_CTRL2.  This register is associated with AFE_LUTCR
         * LUTSel=10b. It contains the Tx Equalization settings that will
         * be used if a SAS 1.5Gbs or SAS 3.0Gbs device is direct-attached.
         */
        U32 afe_tx_amp_control2;

        /**
         * AFE_TX_AMP_CTRL3.  This register is associated with AFE_LUTCR
         * LUTSel=11b. It contains the Tx Equalization settings that will
         * be used if a SAS 6.0Gbs device is direct-attached.
         */
        U32 afe_tx_amp_control3;

    } phys[SCI_MAX_PHYS];   // 4 PHYs per SCU controller unit

} SCI_BIOS_OEM_PARAM_ELEMENT_v_1_2_T;

/**
 * @struct SCIC_SDS_OEM_PARAMETERS VER 1.3
 *
 * @brief This structure delineates the various OEM parameters that must
 * be set for the Intel SAS Storage Controller Unit (SCU).
 */
typedef struct SCI_BIOS_OEM_PARAM_ELEMENT_v_1_3
{
    /**
     * Per SCU Controller Data
     */
    struct
    {
        /**
         * This field indicates the port configuration mode for
         * this controller:
         *   Automatic Port Configuration(APC) or
         *   Manual Port Configuration (MPC).
         *
         * APC means the Platform OEM expects SCI to configure
         * SAS Ports automatically according to the discovered SAS
         * Address pairs of the endpoints, wide and/or narrow.
         *
         * MPC means the Platform OEM manually defines wide or narrow
         * connectors by apriori assigning PHYs to SAS Ports.
         *
         * By default, the mode type is APC
         * in APC mode, if ANY of the phy mask is non-zero,
         *    SCI_FAILURE_INVALID_PARAMETER_VALUE will be returned
         *      from scic_oem_parameters_set AND the default oem
         *      configuration will be applied
         * in MPC mode, if ALL of the phy masks are zero,
         *    SCI_FAILURE_INVALID_PARAMETER_VALUE will be returned
         *      from scic_oem_parameters_set AND the default oem
         *      configuration will be applied
         */
        U8  mode_type;

        /**
         * This field specifies the maximum number of direct attached
         * devices the OEM will allow to have powered up simultaneously
         * on this controller.  This allows the OEM to avoid exceeding
         * power supply limits for this platform.  A value of zero
         * indicates there are no restrictions.
         */
        U8  max_number_concurrent_device_spin_up;

        /**
         * This bitfield indicates the OEM's desired default Tx
         * Spread Spectrum Clocking (SSC) settings for SATA and SAS.
         * NOTE: Default SSC Modulation Frequency is 31.5KHz.
         *--------------------------------------------------------------------*/
        /**
         * NOTE: Max spread for SATA is +0 / -5000 PPM.
         * Down-spreading SSC (only method allowed for SATA):
         *  SATA SSC Tx Disabled                    = 0x0
         *  SATA SSC Tx at +0 / -1419 PPM Spread    = 0x2
         *  SATA SSC Tx at +0 / -2129 PPM Spread    = 0x3
         *  SATA SSC Tx at +0 / -4257 PPM Spread    = 0x6
         *  SATA SSC Tx at +0 / -4967 PPM Spread    = 0x7
        */
        U8 ssc_sata_tx_spread_level : 4;

        /**
         * SAS SSC Tx Disabled                     = 0x0
         *
         * NOTE: Max spread for SAS down-spreading +0 / -2300 PPM
         * Down-spreading SSC:
         *  SAS SSC Tx at +0 / -1419 PPM Spread     = 0x2
         *  SAS SSC Tx at +0 / -2129 PPM Spread     = 0x3
         *
         * NOTE: Max spread for SAS center-spreading +2300 / -2300 PPM
         * Center-spreading SSC:
         *  SAS SSC Tx at +1064 / -1064 PPM Spread  = 0x3
         *  SAS SSC Tx at +2129 / -2129 PPM Spread  = 0x6
         */
        U8 ssc_sas_tx_spread_level : 3;
        /**
         * NOTE: Refer to the SSC section of the SAS 2.x Specification
         * for proper setting of this field. For standard SAS Initiator
         * SAS PHY operation it should be 0 for Down-spreading.
         * SAS SSC Tx spread type:
         *  Down-spreading SSC      = 0
         *  Center-spreading SSC    = 1
         */
        U8 ssc_sas_tx_type : 1;

        /**
         * This field indicates length of the SAS/SATA cable between
         * host and device.
         * This field is used make relationship between analog parameters of
         * the phy in the silicon and length of the cable.
         * Supported cable attenuation levels:
         * "short"- up to 3m, "medium"-3m to 6m, and "long"- more than 6m
         * This is bit mask field:
         *
         * BIT:      (MSB) 7     6     5     4
         * ASSIGNMENT:   <phy3><phy2><phy1><phy0>  - Medium cable length assignment
         * BIT:            3     2     1     0  (LSB)
         * ASSIGNMENT:   <phy3><phy2><phy1><phy0>  - Long cable length assignment
         *
         * BITS 7-4 are set when the cable length is assigned to medium
         * BITS 3-0 are set when the cable length is assigned to long
         * The BIT positions are clear when the cable length is assigned to short
         * Setting the bits for both long and medium cable length is undefined.
         *
         * A value of 0x84 would assign
         *    phy3 - medium
         *    phy2 - long
         *    phy1 - short
         *    phy0 - short
         */
        U8 cable_selection_mask;

    } controller;

    /**
     * Per SAS Port data.
     */
    struct
    {
        /**
         * This field specifies the phys to be contained inside a port.
         * The bit position in the mask specifies the index of the phy
         * to be contained in the port.  Multiple bits (i.e. phys)
        * can be contained in a single port:
        *      Bit 0 = This controller's PHY index 0     (0x01)
        *      Bit 1 = This controller's PHY index 1     (0x02)
        *      Bit 2 = This controller's PHY index 2     (0x04)
        *      Bit 3 = This controller's PHY index 3     (0x08)
        *
        * Refer to the mode_type field for rules regarding APC and MPC mode.
        * General rule: For APC mode phy_mask = 0
        */
        U8 phy_mask;

    } ports[SCI_MAX_PORTS]; // Up to 4 Ports per SCU controller unit

    /**
     * Per PHY Parameter data.
     */
    struct
    {
        /**
         * This field indicates the SAS Address that will be transmitted on
         * this PHY index.  The field is defined as a union, however, the
         * OEM should use the U8 array definition when encoding it to ensure
         * correct byte ordering.
         *
         * NOTE: If using APC MODE, along with phy_mask being set to ZERO, the
         * SAS Addresses for all PHYs within a controller group SHALL be the
         * same.
         */
        union
        {
            /**
             * The array should be stored in little endian order.  For example,
             * if the desired SAS Address is 0x50010B90_0003538D, then it
             * should be stored in the following manner:
             *    array[0] = 0x90
             *    array[1] = 0x0B
             *    array[2] = 0x01
             *    array[3] = 0x50
             *    array[4] = 0x8D
             *    array[5] = 0x53
             *    array[6] = 0x03
             *    array[7] = 0x00
             */
            U8 array[8];
            /**
             * This is the typedef'd version of the SAS Address used in
             * the SCI Library.
             */
            SCI_SAS_ADDRESS_T  sci_format;

        } sas_address;

        /**
         * These are the per PHY equalization settings associated with the
         * AFE XCVR Tx Amplitude and Equalization Control Register Set
         * (0 thru 3).
         *
         * Operational Note: The following Look-Up-Table registers are engaged
         * by the AFE block after the following:
         *  - Software programs the Link Layer AFE Look Up Table Control
         *    Registers (AFE_LUTCR).
         *  - Software sets AFE XCVR Tx Control Register Tx Equalization
         *    Enable bit.
         */
        /**
         * AFE_TX_AMP_CTRL0.  This register is associated with AFE_LUTCR
         * LUTSel=00b. It contains the Tx Equalization settings that will be
         * used if a SATA 1.5Gbs or SATA 3.0Gbs device is direct-attached.
         */
        U32 afe_tx_amp_control0;

        /**
         * AFE_TX_AMP_CTRL1.  This register is associated with AFE_LUTCR
         * LUTSel=01b. It contains the Tx Equalization settings that will
         * be used if a SATA 6.0Gbs device is direct-attached.
         */
        U32 afe_tx_amp_control1;

        /**
         * AFE_TX_AMP_CTRL2.  This register is associated with AFE_LUTCR
         * LUTSel=10b. It contains the Tx Equalization settings that will
         * be used if a SAS 1.5Gbs or SAS 3.0Gbs device is direct-attached.
         */
        U32 afe_tx_amp_control2;

        /**
         * AFE_TX_AMP_CTRL3.  This register is associated with AFE_LUTCR
         * LUTSel=11b. It contains the Tx Equalization settings that will
         * be used if a SAS 6.0Gbs device is direct-attached.
         */
        U32 afe_tx_amp_control3;

    } phys[SCI_MAX_PHYS];   // 4 PHYs per SCU controller unit

} SCI_BIOS_OEM_PARAM_ELEMENT_v_1_3_T;

/**
 *  @struct SCI_BIOS_OEM_PARAM_BLOCK
 *
 * @brief This structure defines the OEM Parameter block as it will be stored
 *  in the last 512 bytes of the PDR region in the SPI flash.  It must be
 *  unpacked or pack(1).
 */
typedef struct SCI_BIOS_OEM_PARAM_BLOCK
{
    /**
     * OEM Parameter Block header.
     */
    SCI_BIOS_OEM_PARAM_BLOCK_HDR_T  header;

    /**
     * Per controller element descriptor containing the controller's
     * parameter data. The prototype defines just one of these descriptors,
     * however, the actual runtime number is determined by the num_elements
     * field in the header.
     */
    SCI_BIOS_OEM_PARAM_ELEMENT_T controller_element[1];

} SCI_BIOS_OEM_PARAM_BLOCK_T;

#ifdef __cplusplus
}
#endif // __cplusplus

#endif // _SCU_BIOS_DEFINITIONS_H_

