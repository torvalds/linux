/*	$OpenBSD: if_iwmreg.h,v 1.70 2024/09/01 03:08:59 jsg Exp $	*/

/******************************************************************************
 *
 * This file is provided under a dual BSD/GPLv2 license.  When using or
 * redistributing this file, you may do so under either license.
 *
 * GPL LICENSE SUMMARY
 *
 * Copyright(c) 2005 - 2014 Intel Corporation. All rights reserved.
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
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110,
 * USA
 *
 * The full GNU General Public License is included in this distribution
 * in the file called COPYING.
 *
 * Contact Information:
 *  Intel Linux Wireless <ilw@linux.intel.com>
 * Intel Corporation, 5200 N.E. Elam Young Parkway, Hillsboro, OR 97124-6497
 *
 * BSD LICENSE
 *
 * Copyright(c) 2005 - 2014 Intel Corporation. All rights reserved.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 *  * Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *  * Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 *  * Neither the name Intel Corporation nor the names of its
 *    contributors may be used to endorse or promote products derived
 *    from this software without specific prior written permission.
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
 *****************************************************************************/

/*
 * CSR (control and status registers)
 *
 * CSR registers are mapped directly into PCI bus space, and are accessible
 * whenever platform supplies power to device, even when device is in
 * low power states due to driver-invoked device resets
 * (e.g. IWM_CSR_RESET_REG_FLAG_SW_RESET) or uCode-driven power-saving modes.
 *
 * Use iwl_write32() and iwl_read32() family to access these registers;
 * these provide simple PCI bus access, without waking up the MAC.
 * Do not use iwl_write_direct32() family for these registers;
 * no need to "grab nic access" via IWM_CSR_GP_CNTRL_REG_FLAG_MAC_ACCESS_REQ.
 * The MAC (uCode processor, etc.) does not need to be powered up for accessing
 * the CSR registers.
 *
 * NOTE:  Device does need to be awake in order to read this memory
 *        via IWM_CSR_EEPROM and IWM_CSR_OTP registers
 */
#define IWM_CSR_HW_IF_CONFIG_REG    (0x000) /* hardware interface config */
#define IWM_CSR_INT_COALESCING      (0x004) /* accum ints, 32-usec units */
#define IWM_CSR_INT                 (0x008) /* host interrupt status/ack */
#define IWM_CSR_INT_MASK            (0x00c) /* host interrupt enable */
#define IWM_CSR_FH_INT_STATUS       (0x010) /* busmaster int status/ack*/
#define IWM_CSR_GPIO_IN             (0x018) /* read external chip pins */
#define IWM_CSR_RESET               (0x020) /* busmaster enable, NMI, etc*/
#define IWM_CSR_GP_CNTRL            (0x024)

/* 2nd byte of IWM_CSR_INT_COALESCING, not accessible via iwl_write32()! */
#define IWM_CSR_INT_PERIODIC_REG	(0x005)

/*
 * Hardware revision info
 * Bit fields:
 * 31-16:  Reserved
 *  15-4:  Type of device:  see IWM_CSR_HW_REV_TYPE_xxx definitions
 *  3-2:  Revision step:  0 = A, 1 = B, 2 = C, 3 = D
 *  1-0:  "Dash" (-) value, as in A-1, etc.
 */
#define IWM_CSR_HW_REV              (0x028)

/*
 * EEPROM and OTP (one-time-programmable) memory reads
 *
 * NOTE:  Device must be awake, initialized via apm_ops.init(),
 *        in order to read.
 */
#define IWM_CSR_EEPROM_REG          (0x02c)
#define IWM_CSR_EEPROM_GP           (0x030)
#define IWM_CSR_OTP_GP_REG          (0x034)

#define IWM_CSR_GIO_REG		(0x03C)
#define IWM_CSR_GP_UCODE_REG	(0x048)
#define IWM_CSR_GP_DRIVER_REG	(0x050)

/*
 * UCODE-DRIVER GP (general purpose) mailbox registers.
 * SET/CLR registers set/clear bit(s) if "1" is written.
 */
#define IWM_CSR_UCODE_DRV_GP1       (0x054)
#define IWM_CSR_UCODE_DRV_GP1_SET   (0x058)
#define IWM_CSR_UCODE_DRV_GP1_CLR   (0x05c)
#define IWM_CSR_UCODE_DRV_GP2       (0x060)

#define IWM_CSR_MBOX_SET_REG		(0x088)
#define IWM_CSR_MBOX_SET_REG_OS_ALIVE	0x20

#define IWM_CSR_LED_REG			(0x094)
#define IWM_CSR_DRAM_INT_TBL_REG	(0x0A0)
#define IWM_CSR_MAC_SHADOW_REG_CTRL	(0x0A8) /* 6000 and up */


/* GIO Chicken Bits (PCI Express bus link power management) */
#define IWM_CSR_GIO_CHICKEN_BITS    (0x100)

/* Analog phase-lock-loop configuration  */
#define IWM_CSR_ANA_PLL_CFG         (0x20c)

/*
 * CSR Hardware Revision Workaround Register.  Indicates hardware rev;
 * "step" determines CCK backoff for txpower calculation.  Used for 4965 only.
 * See also IWM_CSR_HW_REV register.
 * Bit fields:
 *  3-2:  0 = A, 1 = B, 2 = C, 3 = D step
 *  1-0:  "Dash" (-) value, as in C-1, etc.
 */
#define IWM_CSR_HW_REV_WA_REG		(0x22C)

#define IWM_CSR_DBG_HPET_MEM_REG	(0x240)
#define IWM_CSR_DBG_LINK_PWR_MGMT_REG	(0x250)

/* Bits for IWM_CSR_HW_IF_CONFIG_REG */
#define IWM_CSR_HW_IF_CONFIG_REG_MSK_MAC_DASH	(0x00000003)
#define IWM_CSR_HW_IF_CONFIG_REG_MSK_MAC_STEP	(0x0000000C)
#define IWM_CSR_HW_IF_CONFIG_REG_MSK_BOARD_VER	(0x000000C0)
#define IWM_CSR_HW_IF_CONFIG_REG_BIT_MAC_SI	(0x00000100)
#define IWM_CSR_HW_IF_CONFIG_REG_BIT_RADIO_SI	(0x00000200)
#define IWM_CSR_HW_IF_CONFIG_REG_MSK_PHY_TYPE	(0x00000C00)
#define IWM_CSR_HW_IF_CONFIG_REG_MSK_PHY_DASH	(0x00003000)
#define IWM_CSR_HW_IF_CONFIG_REG_MSK_PHY_STEP	(0x0000C000)

#define IWM_CSR_HW_IF_CONFIG_REG_POS_MAC_DASH	(0)
#define IWM_CSR_HW_IF_CONFIG_REG_POS_MAC_STEP	(2)
#define IWM_CSR_HW_IF_CONFIG_REG_POS_BOARD_VER	(6)
#define IWM_CSR_HW_IF_CONFIG_REG_POS_PHY_TYPE	(10)
#define IWM_CSR_HW_IF_CONFIG_REG_POS_PHY_DASH	(12)
#define IWM_CSR_HW_IF_CONFIG_REG_POS_PHY_STEP	(14)

#define IWM_CSR_HW_IF_CONFIG_REG_BIT_HAP_WAKE_L1A	(0x00080000)
#define IWM_CSR_HW_IF_CONFIG_REG_BIT_EEPROM_OWN_SEM	(0x00200000)
#define IWM_CSR_HW_IF_CONFIG_REG_BIT_NIC_READY	(0x00400000) /* PCI_OWN_SEM */
#define IWM_CSR_HW_IF_CONFIG_REG_BIT_NIC_PREPARE_DONE (0x02000000) /* ME_OWN */
#define IWM_CSR_HW_IF_CONFIG_REG_PREPARE	(0x08000000) /* WAKE_ME */
#define IWM_CSR_HW_IF_CONFIG_REG_ENABLE_PME	(0x10000000)
#define IWM_CSR_HW_IF_CONFIG_REG_PERSIST_MODE	(0x40000000) /* PERSISTENCE */

#define IWM_CSR_INT_PERIODIC_DIS		(0x00) /* disable periodic int*/
#define IWM_CSR_INT_PERIODIC_ENA		(0xFF) /* 255*32 usec ~ 8 msec*/

/* interrupt flags in INTA, set by uCode or hardware (e.g. dma),
 * acknowledged (reset) by host writing "1" to flagged bits. */
#define IWM_CSR_INT_BIT_FH_RX	(1U << 31) /* Rx DMA, cmd responses, FH_INT[17:16] */
#define IWM_CSR_INT_BIT_HW_ERR	(1 << 29) /* DMA hardware error FH_INT[31] */
#define IWM_CSR_INT_BIT_RX_PERIODIC	(1 << 28) /* Rx periodic */
#define IWM_CSR_INT_BIT_FH_TX	(1 << 27) /* Tx DMA FH_INT[1:0] */
#define IWM_CSR_INT_BIT_SCD	(1 << 26) /* TXQ pointer advanced */
#define IWM_CSR_INT_BIT_SW_ERR	(1 << 25) /* uCode error */
#define IWM_CSR_INT_BIT_RF_KILL	(1 << 7)  /* HW RFKILL switch GP_CNTRL[27] toggled */
#define IWM_CSR_INT_BIT_CT_KILL	(1 << 6)  /* Critical temp (chip too hot) rfkill */
#define IWM_CSR_INT_BIT_SW_RX	(1 << 3)  /* Rx, command responses */
#define IWM_CSR_INT_BIT_WAKEUP	(1 << 1)  /* NIC controller waking up (pwr mgmt) */
#define IWM_CSR_INT_BIT_ALIVE	(1 << 0)  /* uCode interrupts once it initializes */

#define IWM_CSR_INI_SET_MASK	(IWM_CSR_INT_BIT_FH_RX   | \
				 IWM_CSR_INT_BIT_HW_ERR  | \
				 IWM_CSR_INT_BIT_FH_TX   | \
				 IWM_CSR_INT_BIT_SW_ERR  | \
				 IWM_CSR_INT_BIT_RF_KILL | \
				 IWM_CSR_INT_BIT_SW_RX   | \
				 IWM_CSR_INT_BIT_WAKEUP  | \
				 IWM_CSR_INT_BIT_ALIVE   | \
				 IWM_CSR_INT_BIT_RX_PERIODIC)

/* interrupt flags in FH (flow handler) (PCI busmaster DMA) */
#define IWM_CSR_FH_INT_BIT_ERR       (1U << 31) /* Error */
#define IWM_CSR_FH_INT_BIT_HI_PRIOR  (1 << 30) /* High priority Rx, bypass coalescing */
#define IWM_CSR_FH_INT_BIT_RX_CHNL1  (1 << 17) /* Rx channel 1 */
#define IWM_CSR_FH_INT_BIT_RX_CHNL0  (1 << 16) /* Rx channel 0 */
#define IWM_CSR_FH_INT_BIT_TX_CHNL1  (1 << 1)  /* Tx channel 1 */
#define IWM_CSR_FH_INT_BIT_TX_CHNL0  (1 << 0)  /* Tx channel 0 */

#define IWM_CSR_FH_INT_RX_MASK	(IWM_CSR_FH_INT_BIT_HI_PRIOR | \
				IWM_CSR_FH_INT_BIT_RX_CHNL1 | \
				IWM_CSR_FH_INT_BIT_RX_CHNL0)

#define IWM_CSR_FH_INT_TX_MASK	(IWM_CSR_FH_INT_BIT_TX_CHNL1 | \
				IWM_CSR_FH_INT_BIT_TX_CHNL0)

/* GPIO */
#define IWM_CSR_GPIO_IN_BIT_AUX_POWER                   (0x00000200)
#define IWM_CSR_GPIO_IN_VAL_VAUX_PWR_SRC                (0x00000000)
#define IWM_CSR_GPIO_IN_VAL_VMAIN_PWR_SRC               (0x00000200)

/* RESET */
#define IWM_CSR_RESET_REG_FLAG_NEVO_RESET                (0x00000001)
#define IWM_CSR_RESET_REG_FLAG_FORCE_NMI                 (0x00000002)
#define IWM_CSR_RESET_REG_FLAG_SW_RESET                  (0x00000080)
#define IWM_CSR_RESET_REG_FLAG_MASTER_DISABLED           (0x00000100)
#define IWM_CSR_RESET_REG_FLAG_STOP_MASTER               (0x00000200)
#define IWM_CSR_RESET_LINK_PWR_MGMT_DISABLED             (0x80000000)

/*
 * GP (general purpose) CONTROL REGISTER
 * Bit fields:
 *    27:  HW_RF_KILL_SW
 *         Indicates state of (platform's) hardware RF-Kill switch
 * 26-24:  POWER_SAVE_TYPE
 *         Indicates current power-saving mode:
 *         000 -- No power saving
 *         001 -- MAC power-down
 *         010 -- PHY (radio) power-down
 *         011 -- Error
 *   9-6:  SYS_CONFIG
 *         Indicates current system configuration, reflecting pins on chip
 *         as forced high/low by device circuit board.
 *     4:  GOING_TO_SLEEP
 *         Indicates MAC is entering a power-saving sleep power-down.
 *         Not a good time to access device-internal resources.
 *     3:  MAC_ACCESS_REQ
 *         Host sets this to request and maintain MAC wakeup, to allow host
 *         access to device-internal resources.  Host must wait for
 *         MAC_CLOCK_READY (and !GOING_TO_SLEEP) before accessing non-CSR
 *         device registers.
 *     2:  INIT_DONE
 *         Host sets this to put device into fully operational D0 power mode.
 *         Host resets this after SW_RESET to put device into low power mode.
 *     0:  MAC_CLOCK_READY
 *         Indicates MAC (ucode processor, etc.) is powered up and can run.
 *         Internal resources are accessible.
 *         NOTE:  This does not indicate that the processor is actually running.
 *         NOTE:  This does not indicate that device has completed
 *                init or post-power-down restore of internal SRAM memory.
 *                Use IWM_CSR_UCODE_DRV_GP1_BIT_MAC_SLEEP as indication that
 *                SRAM is restored and uCode is in normal operation mode.
 *                Later devices (5xxx/6xxx/1xxx) use non-volatile SRAM, and
 *                do not need to save/restore it.
 *         NOTE:  After device reset, this bit remains "0" until host sets
 *                INIT_DONE
 */
#define IWM_CSR_GP_CNTRL_REG_FLAG_MAC_CLOCK_READY        (0x00000001)
#define IWM_CSR_GP_CNTRL_REG_FLAG_INIT_DONE              (0x00000004)
#define IWM_CSR_GP_CNTRL_REG_FLAG_MAC_ACCESS_REQ         (0x00000008)
#define IWM_CSR_GP_CNTRL_REG_FLAG_GOING_TO_SLEEP         (0x00000010)

#define IWM_CSR_GP_CNTRL_REG_VAL_MAC_ACCESS_EN           (0x00000001)

#define IWM_CSR_GP_CNTRL_REG_MSK_POWER_SAVE_TYPE         (0x07000000)
#define IWM_CSR_GP_CNTRL_REG_FLAG_RFKILL_WAKE_L1A_EN     (0x04000000)
#define IWM_CSR_GP_CNTRL_REG_FLAG_HW_RF_KILL_SW          (0x08000000)


/* HW REV */
#define IWM_CSR_HW_REV_DASH(_val)          (((_val) & 0x0000003) >> 0)
#define IWM_CSR_HW_REV_STEP(_val)          (((_val) & 0x000000C) >> 2)

#define IWM_CSR_HW_REV_TYPE_MSK		(0x000FFF0)
#define IWM_CSR_HW_REV_TYPE_5300	(0x0000020)
#define IWM_CSR_HW_REV_TYPE_5350	(0x0000030)
#define IWM_CSR_HW_REV_TYPE_5100	(0x0000050)
#define IWM_CSR_HW_REV_TYPE_5150	(0x0000040)
#define IWM_CSR_HW_REV_TYPE_1000	(0x0000060)
#define IWM_CSR_HW_REV_TYPE_6x00	(0x0000070)
#define IWM_CSR_HW_REV_TYPE_6x50	(0x0000080)
#define IWM_CSR_HW_REV_TYPE_6150	(0x0000084)
#define IWM_CSR_HW_REV_TYPE_6x05	(0x00000B0)
#define IWM_CSR_HW_REV_TYPE_6x30	IWM_CSR_HW_REV_TYPE_6x05
#define IWM_CSR_HW_REV_TYPE_6x35	IWM_CSR_HW_REV_TYPE_6x05
#define IWM_CSR_HW_REV_TYPE_2x30	(0x00000C0)
#define IWM_CSR_HW_REV_TYPE_2x00	(0x0000100)
#define IWM_CSR_HW_REV_TYPE_105		(0x0000110)
#define IWM_CSR_HW_REV_TYPE_135		(0x0000120)
#define IWM_CSR_HW_REV_TYPE_7265D	(0x0000210)
#define IWM_CSR_HW_REV_TYPE_NONE	(0x00001F0)

/* EEPROM REG */
#define IWM_CSR_EEPROM_REG_READ_VALID_MSK	(0x00000001)
#define IWM_CSR_EEPROM_REG_BIT_CMD		(0x00000002)
#define IWM_CSR_EEPROM_REG_MSK_ADDR		(0x0000FFFC)
#define IWM_CSR_EEPROM_REG_MSK_DATA		(0xFFFF0000)

/* EEPROM GP */
#define IWM_CSR_EEPROM_GP_VALID_MSK		(0x00000007) /* signature */
#define IWM_CSR_EEPROM_GP_IF_OWNER_MSK	(0x00000180)
#define IWM_CSR_EEPROM_GP_BAD_SIGNATURE_BOTH_EEP_AND_OTP	(0x00000000)
#define IWM_CSR_EEPROM_GP_BAD_SIG_EEP_GOOD_SIG_OTP		(0x00000001)
#define IWM_CSR_EEPROM_GP_GOOD_SIG_EEP_LESS_THAN_4K		(0x00000002)
#define IWM_CSR_EEPROM_GP_GOOD_SIG_EEP_MORE_THAN_4K		(0x00000004)

/* One-time-programmable memory general purpose reg */
#define IWM_CSR_OTP_GP_REG_DEVICE_SELECT  (0x00010000) /* 0 - EEPROM, 1 - OTP */
#define IWM_CSR_OTP_GP_REG_OTP_ACCESS_MODE  (0x00020000) /* 0 - absolute, 1 - relative */
#define IWM_CSR_OTP_GP_REG_ECC_CORR_STATUS_MSK    (0x00100000) /* bit 20 */
#define IWM_CSR_OTP_GP_REG_ECC_UNCORR_STATUS_MSK  (0x00200000) /* bit 21 */

/* GP REG */
#define IWM_CSR_GP_REG_POWER_SAVE_STATUS_MSK    (0x03000000) /* bit 24/25 */
#define IWM_CSR_GP_REG_NO_POWER_SAVE            (0x00000000)
#define IWM_CSR_GP_REG_MAC_POWER_SAVE           (0x01000000)
#define IWM_CSR_GP_REG_PHY_POWER_SAVE           (0x02000000)
#define IWM_CSR_GP_REG_POWER_SAVE_ERROR         (0x03000000)


/* CSR GIO */
#define IWM_CSR_GIO_REG_VAL_L0S_ENABLED	(0x00000002)

/*
 * UCODE-DRIVER GP (general purpose) mailbox register 1
 * Host driver and uCode write and/or read this register to communicate with
 * each other.
 * Bit fields:
 *     4:  UCODE_DISABLE
 *         Host sets this to request permanent halt of uCode, same as
 *         sending CARD_STATE command with "halt" bit set.
 *     3:  CT_KILL_EXIT
 *         Host sets this to request exit from CT_KILL state, i.e. host thinks
 *         device temperature is low enough to continue normal operation.
 *     2:  CMD_BLOCKED
 *         Host sets this during RF KILL power-down sequence (HW, SW, CT KILL)
 *         to release uCode to clear all Tx and command queues, enter
 *         unassociated mode, and power down.
 *         NOTE:  Some devices also use HBUS_TARG_MBX_C register for this bit.
 *     1:  SW_BIT_RFKILL
 *         Host sets this when issuing CARD_STATE command to request
 *         device sleep.
 *     0:  MAC_SLEEP
 *         uCode sets this when preparing a power-saving power-down.
 *         uCode resets this when power-up is complete and SRAM is sane.
 *         NOTE:  device saves internal SRAM data to host when powering down,
 *                and must restore this data after powering back up.
 *                MAC_SLEEP is the best indication that restore is complete.
 *                Later devices (5xxx/6xxx/1xxx) use non-volatile SRAM, and
 *                do not need to save/restore it.
 */
#define IWM_CSR_UCODE_DRV_GP1_BIT_MAC_SLEEP             (0x00000001)
#define IWM_CSR_UCODE_SW_BIT_RFKILL                     (0x00000002)
#define IWM_CSR_UCODE_DRV_GP1_BIT_CMD_BLOCKED           (0x00000004)
#define IWM_CSR_UCODE_DRV_GP1_REG_BIT_CT_KILL_EXIT      (0x00000008)
#define IWM_CSR_UCODE_DRV_GP1_BIT_D3_CFG_COMPLETE       (0x00000020)

/* GP Driver */
#define IWM_CSR_GP_DRIVER_REG_BIT_RADIO_SKU_MSK		    (0x00000003)
#define IWM_CSR_GP_DRIVER_REG_BIT_RADIO_SKU_3x3_HYB	    (0x00000000)
#define IWM_CSR_GP_DRIVER_REG_BIT_RADIO_SKU_2x2_HYB	    (0x00000001)
#define IWM_CSR_GP_DRIVER_REG_BIT_RADIO_SKU_2x2_IPA	    (0x00000002)
#define IWM_CSR_GP_DRIVER_REG_BIT_CALIB_VERSION6	    (0x00000004)
#define IWM_CSR_GP_DRIVER_REG_BIT_6050_1x2		    (0x00000008)

#define IWM_CSR_GP_DRIVER_REG_BIT_RADIO_IQ_INVER	    (0x00000080)

/* GIO Chicken Bits (PCI Express bus link power management) */
#define IWM_CSR_GIO_CHICKEN_BITS_REG_BIT_L1A_NO_L0S_RX  (0x00800000)
#define IWM_CSR_GIO_CHICKEN_BITS_REG_BIT_DIS_L0S_EXIT_TIMER  (0x20000000)

/* LED */
#define IWM_CSR_LED_BSM_CTRL_MSK (0xFFFFFFDF)
#define IWM_CSR_LED_REG_TURN_ON (0x60)
#define IWM_CSR_LED_REG_TURN_OFF (0x20)

/* ANA_PLL */
#define IWM_CSR50_ANA_PLL_CFG_VAL        (0x00880300)

/* HPET MEM debug */
#define IWM_CSR_DBG_HPET_MEM_REG_VAL	(0xFFFF0000)

/* DRAM INT TABLE */
#define IWM_CSR_DRAM_INT_TBL_ENABLE		(1U << 31)
#define IWM_CSR_DRAM_INIT_TBL_WRITE_POINTER	(1 << 28)
#define IWM_CSR_DRAM_INIT_TBL_WRAP_CHECK	(1 << 27)

/* SECURE boot registers */
#define IWM_CSR_SECURE_BOOT_CONFIG_ADDR	(0x100)
#define IWM_CSR_SECURE_BOOT_CONFIG_INSPECTOR_BURNED_IN_OTP	0x00000001
#define IWM_CSR_SECURE_BOOT_CONFIG_INSPECTOR_NOT_REQ		0x00000002
#define IWM_CSR_SECURE_BOOT_CPU1_STATUS_ADDR	(0x100)
#define IWM_CSR_SECURE_BOOT_CPU2_STATUS_ADDR	(0x100)
#define IWM_CSR_SECURE_BOOT_CPU_STATUS_VERF_STATUS	0x00000003
#define IWM_CSR_SECURE_BOOT_CPU_STATUS_VERF_COMPLETED	0x00000002
#define IWM_CSR_SECURE_BOOT_CPU_STATUS_VERF_SUCCESS	0x00000004
#define IWM_CSR_SECURE_BOOT_CPU_STATUS_VERF_FAIL	0x00000008
#define IWM_CSR_SECURE_BOOT_CPU_STATUS_SIGN_VERF_FAIL	0x00000010

#define IWM_FH_UCODE_LOAD_STATUS	0x1af0

#define IWM_FH_MEM_TB_MAX_LENGTH	0x20000

/* 9000 rx series registers */

#define IWM_RFH_Q0_FRBDCB_BA_LSB 0xA08000 /* 64 bit address */
#define IWM_RFH_Q_FRBDCB_BA_LSB(q) (IWM_RFH_Q0_FRBDCB_BA_LSB + (q) * 8)
/* Write index table */
#define IWM_RFH_Q0_FRBDCB_WIDX 0xA08080
#define IWM_RFH_Q_FRBDCB_WIDX(q) (IWM_RFH_Q0_FRBDCB_WIDX + (q) * 4)
/* Write index table - shadow registers */
#define IWM_RFH_Q0_FRBDCB_WIDX_TRG 0x1C80
#define IWM_RFH_Q_FRBDCB_WIDX_TRG(q) (IWM_RFH_Q0_FRBDCB_WIDX_TRG + (q) * 4)
/* Read index table */
#define IWM_RFH_Q0_FRBDCB_RIDX 0xA080C0
#define IWM_RFH_Q_FRBDCB_RIDX(q) (IWM_RFH_Q0_FRBDCB_RIDX + (q) * 4)
/* Used list table */
#define IWM_RFH_Q0_URBDCB_BA_LSB 0xA08100 /* 64 bit address */
#define IWM_RFH_Q_URBDCB_BA_LSB(q) (IWM_RFH_Q0_URBDCB_BA_LSB + (q) * 8)
/* Write index table */
#define IWM_RFH_Q0_URBDCB_WIDX 0xA08180
#define IWM_RFH_Q_URBDCB_WIDX(q) (IWM_RFH_Q0_URBDCB_WIDX + (q) * 4)
#define IWM_RFH_Q0_URBDCB_VAID 0xA081C0
#define IWM_RFH_Q_URBDCB_VAID(q) (IWM_RFH_Q0_URBDCB_VAID + (q) * 4)
/* stts */
#define IWM_RFH_Q0_URBD_STTS_WPTR_LSB 0xA08200 /*64 bits address */
#define IWM_RFH_Q_URBD_STTS_WPTR_LSB(q) (IWM_RFH_Q0_URBD_STTS_WPTR_LSB + (q) * 8)

#define IWM_RFH_Q0_ORB_WPTR_LSB 0xA08280
#define IWM_RFH_Q_ORB_WPTR_LSB(q) (IWM_RFH_Q0_ORB_WPTR_LSB + (q) * 8)
#define IWM_RFH_RBDBUF_RBD0_LSB 0xA08300
#define IWM_RFH_RBDBUF_RBD_LSB(q) (IWM_RFH_RBDBUF_RBD0_LSB + (q) * 8)

/**
 * RFH Status Register
 *
 * Bit fields:
 *
 * Bit 29: RBD_FETCH_IDLE
 * This status flag is set by the RFH when there is no active RBD fetch from
 * DRAM.
 * Once the RFH RBD controller starts fetching (or when there is a pending
 * RBD read response from DRAM), this flag is immediately turned off.
 *
 * Bit 30: SRAM_DMA_IDLE
 * This status flag is set by the RFH when there is no active transaction from
 * SRAM to DRAM.
 * Once the SRAM to DRAM DMA is active, this flag is immediately turned off.
 *
 * Bit 31: RXF_DMA_IDLE
 * This status flag is set by the RFH when there is no active transaction from
 * RXF to DRAM.
 * Once the RXF-to-DRAM DMA is active, this flag is immediately turned off.
 */
#define IWM_RFH_GEN_STATUS          0xA09808
#define IWM_RFH_GEN_STATUS_GEN3     0xA07824
#define IWM_RBD_FETCH_IDLE  (1 << 29)
#define IWM_SRAM_DMA_IDLE   (1 << 30)
#define IWM_RXF_DMA_IDLE    (1U << 31)

/* DMA configuration */
#define IWM_RFH_RXF_DMA_CFG         0xA09820
#define IWM_RFH_RXF_DMA_CFG_GEN3    0xA07880
/* RB size */
#define IWM_RFH_RXF_DMA_RB_SIZE_MASK (0x000F0000) /* bits 16-19 */
#define IWM_RFH_RXF_DMA_RB_SIZE_POS 16
#define IWM_RFH_RXF_DMA_RB_SIZE_1K  (0x1 << IWM_RFH_RXF_DMA_RB_SIZE_POS)
#define IWM_RFH_RXF_DMA_RB_SIZE_2K  (0x2 << IWM_RFH_RXF_DMA_RB_SIZE_POS)
#define IWM_RFH_RXF_DMA_RB_SIZE_4K  (0x4 << IWM_RFH_RXF_DMA_RB_SIZE_POS)
#define IWM_RFH_RXF_DMA_RB_SIZE_8K  (0x8 << IWM_RFH_RXF_DMA_RB_SIZE_POS)
#define IWM_RFH_RXF_DMA_RB_SIZE_12K (0x9 << IWM_RFH_RXF_DMA_RB_SIZE_POS)
#define IWM_RFH_RXF_DMA_RB_SIZE_16K (0xA << IWM_RFH_RXF_DMA_RB_SIZE_POS)
#define IWM_RFH_RXF_DMA_RB_SIZE_20K (0xB << IWM_RFH_RXF_DMA_RB_SIZE_POS)
#define IWM_RFH_RXF_DMA_RB_SIZE_24K (0xC << IWM_RFH_RXF_DMA_RB_SIZE_POS)
#define IWM_RFH_RXF_DMA_RB_SIZE_28K (0xD << IWM_RFH_RXF_DMA_RB_SIZE_POS)
#define IWM_RFH_RXF_DMA_RB_SIZE_32K (0xE << IWM_RFH_RXF_DMA_RB_SIZE_POS)
/* RB Circular Buffer size:defines the table sizes in RBD units */
#define IWM_RFH_RXF_DMA_RBDCB_SIZE_MASK (0x00F00000) /* bits 20-23 */
#define IWM_RFH_RXF_DMA_RBDCB_SIZE_POS 20
#define IWM_RFH_RXF_DMA_RBDCB_SIZE_8        (0x3 << IWM_RFH_RXF_DMA_RBDCB_SIZE_POS)
#define IWM_RFH_RXF_DMA_RBDCB_SIZE_16       (0x4 << IWM_RFH_RXF_DMA_RBDCB_SIZE_POS)
#define IWM_RFH_RXF_DMA_RBDCB_SIZE_32       (0x5 << IWM_RFH_RXF_DMA_RBDCB_SIZE_POS)
#define IWM_RFH_RXF_DMA_RBDCB_SIZE_64       (0x7 << IWM_RFH_RXF_DMA_RBDCB_SIZE_POS)
#define IWM_RFH_RXF_DMA_RBDCB_SIZE_128      (0x7 << IWM_RFH_RXF_DMA_RBDCB_SIZE_POS)
#define IWM_RFH_RXF_DMA_RBDCB_SIZE_256      (0x8 << IWM_RFH_RXF_DMA_RBDCB_SIZE_POS)
#define IWM_RFH_RXF_DMA_RBDCB_SIZE_512      (0x9 << IWM_RFH_RXF_DMA_RBDCB_SIZE_POS)
#define IWM_RFH_RXF_DMA_RBDCB_SIZE_1024     (0xA << IWM_RFH_RXF_DMA_RBDCB_SIZE_POS)
#define IWM_RFH_RXF_DMA_RBDCB_SIZE_2048     (0xB << IWM_RFH_RXF_DMA_RBDCB_SIZE_POS)
#define IWM_RFH_RXF_DMA_MIN_RB_SIZE_MASK    (0x03000000) /* bit 24-25 */
#define IWM_RFH_RXF_DMA_MIN_RB_SIZE_POS     24
#define IWM_RFH_RXF_DMA_MIN_RB_4_8          (3 << IWM_RFH_RXF_DMA_MIN_RB_SIZE_POS)
#define IWM_RFH_RXF_DMA_DROP_TOO_LARGE_MASK (0x04000000) /* bit 26 */
#define IWM_RFH_RXF_DMA_SINGLE_FRAME_MASK   (0x20000000) /* bit 29 */
#define IWM_RFH_DMA_EN_MASK                 (0xC0000000) /* bits 30-31*/
#define IWM_RFH_DMA_EN_ENABLE_VAL           (1U << 31)

#define IWM_RFH_RXF_RXQ_ACTIVE 0xA0980C

#define IWM_RFH_GEN_CFG     0xA09800
#define IWM_RFH_GEN_CFG_SERVICE_DMA_SNOOP   (1 << 0)
#define IWM_RFH_GEN_CFG_RFH_DMA_SNOOP       (1 << 1)
#define IWM_RFH_GEN_CFG_RB_CHUNK_SIZE_128   0x00000010
#define IWM_RFH_GEN_CFG_RB_CHUNK_SIZE_64    0x00000000
/* the driver assumes everywhere that the default RXQ is 0 */
#define IWM_RFH_GEN_CFG_DEFAULT_RXQ_NUM     0xF00

/* end of 9000 rx series registers */

#define IWM_LMPM_SECURE_UCODE_LOAD_CPU1_HDR_ADDR	0x1e78
#define IWM_LMPM_SECURE_UCODE_LOAD_CPU2_HDR_ADDR	0x1e7c

#define IWM_LMPM_SECURE_CPU1_HDR_MEM_SPACE		0x420000
#define IWM_LMPM_SECURE_CPU2_HDR_MEM_SPACE		0x420400

#define IWM_CSR_SECURE_TIME_OUT	(100)

/* extended range in FW SRAM */
#define IWM_FW_MEM_EXTENDED_START       0x40000
#define IWM_FW_MEM_EXTENDED_END         0x57FFF

/* FW chicken bits */
#define IWM_LMPM_CHICK				0xa01ff8
#define IWM_LMPM_CHICK_EXTENDED_ADDR_SPACE	0x01

#define IWM_FH_TCSR_0_REG0 (0x1D00)

/*
 * HBUS (Host-side Bus)
 *
 * HBUS registers are mapped directly into PCI bus space, but are used
 * to indirectly access device's internal memory or registers that
 * may be powered-down.
 *
 * Use iwl_write_direct32()/iwl_read_direct32() family for these registers;
 * host must "grab nic access" via CSR_GP_CNTRL_REG_FLAG_MAC_ACCESS_REQ
 * to make sure the MAC (uCode processor, etc.) is powered up for accessing
 * internal resources.
 *
 * Do not use iwl_write32()/iwl_read32() family to access these registers;
 * these provide only simple PCI bus access, without waking up the MAC.
 */
#define IWM_HBUS_BASE	(0x400)

/*
 * Registers for accessing device's internal SRAM memory (e.g. SCD SRAM
 * structures, error log, event log, verifying uCode load).
 * First write to address register, then read from or write to data register
 * to complete the job.  Once the address register is set up, accesses to
 * data registers auto-increment the address by one dword.
 * Bit usage for address registers (read or write):
 *  0-31:  memory address within device
 */
#define IWM_HBUS_TARG_MEM_RADDR     (IWM_HBUS_BASE+0x00c)
#define IWM_HBUS_TARG_MEM_WADDR     (IWM_HBUS_BASE+0x010)
#define IWM_HBUS_TARG_MEM_WDAT      (IWM_HBUS_BASE+0x018)
#define IWM_HBUS_TARG_MEM_RDAT      (IWM_HBUS_BASE+0x01c)

/* Mailbox C, used as workaround alternative to CSR_UCODE_DRV_GP1 mailbox */
#define IWM_HBUS_TARG_MBX_C         (IWM_HBUS_BASE+0x030)
#define IWM_HBUS_TARG_MBX_C_REG_BIT_CMD_BLOCKED         (0x00000004)

/*
 * Registers for accessing device's internal peripheral registers
 * (e.g. SCD, BSM, etc.).  First write to address register,
 * then read from or write to data register to complete the job.
 * Bit usage for address registers (read or write):
 *  0-15:  register address (offset) within device
 * 24-25:  (# bytes - 1) to read or write (e.g. 3 for dword)
 */
#define IWM_HBUS_TARG_PRPH_WADDR    (IWM_HBUS_BASE+0x044)
#define IWM_HBUS_TARG_PRPH_RADDR    (IWM_HBUS_BASE+0x048)
#define IWM_HBUS_TARG_PRPH_WDAT     (IWM_HBUS_BASE+0x04c)
#define IWM_HBUS_TARG_PRPH_RDAT     (IWM_HBUS_BASE+0x050)

/* enable the ID buf for read */
#define IWM_WFPM_PS_CTL_CLR			0xa0300c
#define IWM_WFMP_MAC_ADDR_0			0xa03080
#define IWM_WFMP_MAC_ADDR_1			0xa03084
#define IWM_LMPM_PMG_EN				0xa01cec
#define IWM_RADIO_REG_SYS_MANUAL_DFT_0		0xad4078
#define IWM_RFIC_REG_RD				0xad0470
#define IWM_WFPM_CTRL_REG			0xa03030
#define IWM_WFPM_AUX_CTL_AUX_IF_MAC_OWNER_MSK	0x08000000
#define IWM_ENABLE_WFPM				0x80000000

#define IWM_AUX_MISC_REG			0xa200b0
#define IWM_HW_STEP_LOCATION_BITS		24

#define IWM_AUX_MISC_MASTER1_EN			0xa20818
#define IWM_AUX_MISC_MASTER1_EN_SBE_MSK		0x1
#define IWM_AUX_MISC_MASTER1_SMPHR_STATUS	0xa20800
#define IWM_RSA_ENABLE				0xa24b08
#define IWM_PREG_AUX_BUS_WPROT_0		0xa04cc0
#define IWM_PREG_PRPH_WPROT_9000		0xa04ce0
#define IWM_PREG_PRPH_WPROT_22000		0xa04d00
#define IWM_SB_CFG_OVERRIDE_ADDR		0xa26c78
#define IWM_SB_CFG_OVERRIDE_ENABLE		0x8000
#define IWM_SB_CFG_BASE_OVERRIDE		0xa20000
#define IWM_SB_MODIFY_CFG_FLAG			0xa03088
#define IWM_SB_CPU_1_STATUS			0xa01e30
#define IWM_SB_CPU_2_STATUS			0Xa01e34

#define IWM_UREG_CHICK				0xa05c00
#define IWM_UREG_CHICK_MSI_ENABLE		(1 << 24)
#define IWM_UREG_CHICK_MSIX_ENABLE		(1 << 25)

#define IWM_HPM_DEBUG				0xa03440
#define IWM_HPM_PERSISTENCE_BIT			(1 << 12)
#define IWM_PREG_WFPM_ACCESS			(1 << 12)

/* Used to enable DBGM */
#define IWM_HBUS_TARG_TEST_REG	(IWM_HBUS_BASE+0x05c)

/*
 * Per-Tx-queue write pointer (index, really!)
 * Indicates index to next TFD that driver will fill (1 past latest filled).
 * Bit usage:
 *  0-7:  queue write index
 * 11-8:  queue selector
 */
#define IWM_HBUS_TARG_WRPTR         (IWM_HBUS_BASE+0x060)

/**********************************************************
 * CSR values
 **********************************************************/
 /*
 * host interrupt timeout value
 * used with setting interrupt coalescing timer
 * the CSR_INT_COALESCING is an 8 bit register in 32-usec unit
 *
 * default interrupt coalescing timer is 64 x 32 = 2048 usecs
 */
#define IWM_HOST_INT_TIMEOUT_MAX	(0xFF)
#define IWM_HOST_INT_TIMEOUT_DEF	(0x40)
#define IWM_HOST_INT_TIMEOUT_MIN	(0x0)
#define IWM_HOST_INT_OPER_MODE		(1U << 31)

/*****************************************************************************
 *                        7000/3000 series SHR DTS addresses                 *
 *****************************************************************************/

/* Diode Results Register Structure: */
#define IWM_DTS_DIODE_REG_DIG_VAL		0x000000FF /* bits [7:0] */
#define IWM_DTS_DIODE_REG_VREF_LOW		0x0000FF00 /* bits [15:8] */
#define IWM_DTS_DIODE_REG_VREF_HIGH		0x00FF0000 /* bits [23:16] */
#define IWM_DTS_DIODE_REG_VREF_ID		0x03000000 /* bits [25:24] */
#define IWM_DTS_DIODE_REG_PASS_ONCE		0x80000000 /* bits [31:31] */
#define IWM_DTS_DIODE_REG_FLAGS_MSK		0xFF000000 /* bits [31:24] */
/* Those are the masks INSIDE the flags bit-field: */
#define IWM_DTS_DIODE_REG_FLAGS_VREFS_ID_POS	0
#define IWM_DTS_DIODE_REG_FLAGS_VREFS_ID	0x00000003 /* bits [1:0] */
#define IWM_DTS_DIODE_REG_FLAGS_PASS_ONCE_POS	7
#define IWM_DTS_DIODE_REG_FLAGS_PASS_ONCE	0x00000080 /* bits [7:7] */

/*****************************************************************************
 *                        MSIX related registers                             *
 *****************************************************************************/

#define IWM_CSR_MSIX_BASE			(0x2000)
#define IWM_CSR_MSIX_FH_INT_CAUSES_AD		(IWM_CSR_MSIX_BASE + 0x800)
#define IWM_CSR_MSIX_FH_INT_MASK_AD		(IWM_CSR_MSIX_BASE + 0x804)
#define IWM_CSR_MSIX_HW_INT_CAUSES_AD		(IWM_CSR_MSIX_BASE + 0x808)
#define IWM_CSR_MSIX_HW_INT_MASK_AD		(IWM_CSR_MSIX_BASE + 0x80C)
#define IWM_CSR_MSIX_AUTOMASK_ST_AD		(IWM_CSR_MSIX_BASE + 0x810)
#define IWM_CSR_MSIX_RX_IVAR_AD_REG		(IWM_CSR_MSIX_BASE + 0x880)
#define IWM_CSR_MSIX_IVAR_AD_REG		(IWM_CSR_MSIX_BASE + 0x890)
#define IWM_CSR_MSIX_PENDING_PBA_AD		(IWM_CSR_MSIX_BASE + 0x1000)
#define IWM_CSR_MSIX_RX_IVAR(cause)		(IWM_CSR_MSIX_RX_IVAR_AD_REG + (cause))
#define IWM_CSR_MSIX_IVAR(cause)		(IWM_CSR_MSIX_IVAR_AD_REG + (cause))

/*
 * Causes for the FH register interrupts
 */
enum msix_fh_int_causes {
	IWM_MSIX_FH_INT_CAUSES_Q0		= (1 << 0),
	IWM_MSIX_FH_INT_CAUSES_Q1		= (1 << 1),
	IWM_MSIX_FH_INT_CAUSES_D2S_CH0_NUM	= (1 << 16),
	IWM_MSIX_FH_INT_CAUSES_D2S_CH1_NUM	= (1 << 17),
	IWM_MSIX_FH_INT_CAUSES_S2D		= (1 << 19),
	IWM_MSIX_FH_INT_CAUSES_FH_ERR		= (1 << 21),
};

/*
 * Causes for the HW register interrupts
 */
enum msix_hw_int_causes {
	IWM_MSIX_HW_INT_CAUSES_REG_ALIVE	= (1 << 0),
	IWM_MSIX_HW_INT_CAUSES_REG_WAKEUP	= (1 << 1),
	IWM_MSIX_HW_INT_CAUSES_REG_IPC		= (1 << 1),
	IWM_MSIX_HW_INT_CAUSES_REG_IML		= (1 << 2),
	IWM_MSIX_HW_INT_CAUSES_REG_SW_ERR_V2	= (1 << 5),
	IWM_MSIX_HW_INT_CAUSES_REG_CT_KILL	= (1 << 6),
	IWM_MSIX_HW_INT_CAUSES_REG_RF_KILL	= (1 << 7),
	IWM_MSIX_HW_INT_CAUSES_REG_PERIODIC	= (1 << 8),
	IWM_MSIX_HW_INT_CAUSES_REG_SW_ERR	= (1 << 25),
	IWM_MSIX_HW_INT_CAUSES_REG_SCD		= (1 << 26),
	IWM_MSIX_HW_INT_CAUSES_REG_FH_TX	= (1 << 27),
	IWM_MSIX_HW_INT_CAUSES_REG_HW_ERR	= (1 << 29),
	IWM_MSIX_HW_INT_CAUSES_REG_HAP		= (1 << 30),
};

/*
 * Registers to map causes to vectors
 */
enum msix_ivar_for_cause {
	IWM_MSIX_IVAR_CAUSE_D2S_CH0_NUM		= 0x0,
	IWM_MSIX_IVAR_CAUSE_D2S_CH1_NUM		= 0x1,
	IWM_MSIX_IVAR_CAUSE_S2D			= 0x3,
	IWM_MSIX_IVAR_CAUSE_FH_ERR		= 0x5,
	IWM_MSIX_IVAR_CAUSE_REG_ALIVE		= 0x10,
	IWM_MSIX_IVAR_CAUSE_REG_WAKEUP		= 0x11,
	IWM_MSIX_IVAR_CAUSE_REG_IML		= 0x12,
	IWM_MSIX_IVAR_CAUSE_REG_CT_KILL		= 0x16,
	IWM_MSIX_IVAR_CAUSE_REG_RF_KILL		= 0x17,
	IWM_MSIX_IVAR_CAUSE_REG_PERIODIC	= 0x18,
	IWM_MSIX_IVAR_CAUSE_REG_SW_ERR		= 0x29,
	IWM_MSIX_IVAR_CAUSE_REG_SCD		= 0x2a,
	IWM_MSIX_IVAR_CAUSE_REG_FH_TX		= 0x2b,
	IWM_MSIX_IVAR_CAUSE_REG_HW_ERR		= 0x2d,
	IWM_MSIX_IVAR_CAUSE_REG_HAP		= 0x2e,
};

#define IWM_MSIX_AUTO_CLEAR_CAUSE		(0 << 7)
#define IWM_MSIX_NON_AUTO_CLEAR_CAUSE		(1 << 7)

/**
 * uCode API flags
 * @IWM_UCODE_TLV_FLAGS_PAN: This is PAN capable microcode; this previously
 *	was a separate TLV but moved here to save space.
 * @IWM_UCODE_TLV_FLAGS_NEWSCAN: new uCode scan behaviour on hidden SSID,
 *	treats good CRC threshold as a boolean
 * @IWM_UCODE_TLV_FLAGS_MFP: This uCode image supports MFP (802.11w).
 * @IWM_UCODE_TLV_FLAGS_P2P: This uCode image supports P2P.
 * @IWM_UCODE_TLV_FLAGS_DW_BC_TABLE: The SCD byte count table is in DWORDS
 * @IWM_UCODE_TLV_FLAGS_UAPSD: This uCode image supports uAPSD
 * @IWM_UCODE_TLV_FLAGS_SHORT_BL: 16 entries of black list instead of 64 in scan
 *	offload profile config command.
 * @IWM_UCODE_TLV_FLAGS_D3_6_IPV6_ADDRS: D3 image supports up to six
 *	(rather than two) IPv6 addresses
 * @IWM_UCODE_TLV_FLAGS_NO_BASIC_SSID: not sending a probe with the SSID element
 *	from the probe request template.
 * @IWM_UCODE_TLV_FLAGS_NEW_NSOFFL_SMALL: new NS offload (small version)
 * @IWM_UCODE_TLV_FLAGS_NEW_NSOFFL_LARGE: new NS offload (large version)
 * @IWM_UCODE_TLV_FLAGS_P2P_PS: P2P client power save is supported (only on a
 *	single bound interface).
 * @IWM_UCODE_TLV_FLAGS_UAPSD_SUPPORT: General support for uAPSD
 * @IWM_UCODE_TLV_FLAGS_EBS_SUPPORT: this uCode image supports EBS.
 * @IWM_UCODE_TLV_FLAGS_P2P_PS_UAPSD: P2P client supports uAPSD power save
 * @IWM_UCODE_TLV_FLAGS_BCAST_FILTERING: uCode supports broadcast filtering.
 * @IWM_UCODE_TLV_FLAGS_GO_UAPSD: AP/GO interfaces support uAPSD clients
 *
 */
#define IWM_UCODE_TLV_FLAGS_PAN			(1 << 0)
#define IWM_UCODE_TLV_FLAGS_NEWSCAN		(1 << 1)
#define IWM_UCODE_TLV_FLAGS_MFP			(1 << 2)
#define IWM_UCODE_TLV_FLAGS_P2P			(1 << 3)
#define IWM_UCODE_TLV_FLAGS_DW_BC_TABLE		(1 << 4)
#define IWM_UCODE_TLV_FLAGS_SHORT_BL		(1 << 7)
#define IWM_UCODE_TLV_FLAGS_D3_6_IPV6_ADDRS	(1 << 10)
#define IWM_UCODE_TLV_FLAGS_NO_BASIC_SSID	(1 << 12)
#define IWM_UCODE_TLV_FLAGS_NEW_NSOFFL_SMALL	(1 << 15)
#define IWM_UCODE_TLV_FLAGS_NEW_NSOFFL_LARGE	(1 << 16)
#define IWM_UCODE_TLV_FLAGS_P2P_PS		(1 << 21)
#define IWM_UCODE_TLV_FLAGS_BSS_P2P_PS_DCM	(1 << 22)
#define IWM_UCODE_TLV_FLAGS_BSS_P2P_PS_SCM	(1 << 23)
#define IWM_UCODE_TLV_FLAGS_UAPSD_SUPPORT	(1 << 24)
#define IWM_UCODE_TLV_FLAGS_EBS_SUPPORT		(1 << 25)
#define IWM_UCODE_TLV_FLAGS_P2P_PS_UAPSD	(1 << 26)
#define IWM_UCODE_TLV_FLAGS_BCAST_FILTERING	(1 << 29)
#define IWM_UCODE_TLV_FLAGS_GO_UAPSD		(1 << 30)
#define IWM_UCODE_TLV_FLAGS_LTE_COEX		(1U << 31)

#define IWM_UCODE_TLV_FLAG_BITS \
	"\020\1PAN\2NEWSCAN\3MFP\4P2P\5DW_BC_TABLE\6NEWBT_COEX\7PM_CMD\10SHORT_BL\11RX_ENERGY\12TIME_EVENT_V2\13D3_6_IPV6\14BF_UPDATED\15NO_BASIC_SSID\17D3_CONTINUITY\20NEW_NSOFFL_S\21NEW_NSOFFL_L\22SCHED_SCAN\24STA_KEY_CMD\25DEVICE_PS_CMD\26P2P_PS\27P2P_PS_DCM\30P2P_PS_SCM\31UAPSD_SUPPORT\32EBS\33P2P_PS_UAPSD\36BCAST_FILTERING\37GO_UAPSD\40LTE_COEX"

/**
 * uCode TLV api
 * @IWM_UCODE_TLV_API_FRAGMENTED_SCAN: This ucode supports active dwell time
 *	longer than the passive one, which is essential for fragmented scan.
 * @IWM_UCODE_TLV_API_WIFI_MCC_UPDATE: ucode supports MCC updates with source.
 * @IWM_UCODE_TLV_API_WIDE_CMD_HDR: ucode supports wide command header
 * @IWM_UCODE_TLV_API_LQ_SS_PARAMS: Configure STBC/BFER via LQ CMD ss_params
 * @IWM_UCODE_TLV_API_NEW_VERSION: new versioning format
 * @IWM_UCODE_TLV_API_TX_POWER_CHAIN: TX power API has larger command size
 *	(command version 3) that supports per-chain limits
 * @IWM_UCODE_TLV_API_SCAN_TSF_REPORT: Scan start time reported in scan
 *	iteration complete notification, and the timestamp reported for RX
 *	received during scan, are reported in TSF of the mac specified in the
 *	scan request.
 * @IWM_UCODE_TLV_API_TKIP_MIC_KEYS: This ucode supports version 2 of
 *	ADD_MODIFY_STA_KEY_API_S_VER_2.
 * @IWM_UCODE_TLV_API_STA_TYPE: This ucode supports station type assignment.
 * @IWM_UCODE_TLV_API_EXT_SCAN_PRIORITY: scan APIs use 8-level priority
 *	instead of 3.
 * @IWM_UCODE_TLV_API_NEW_RX_STATS: should new RX STATISTICS API be used
 *
 * @IWM_NUM_UCODE_TLV_API: number of bits used
 */
#define IWM_UCODE_TLV_API_FRAGMENTED_SCAN	8
#define IWM_UCODE_TLV_API_WIFI_MCC_UPDATE	9
#define IWM_UCODE_TLV_API_WIDE_CMD_HDR		14
#define IWM_UCODE_TLV_API_LQ_SS_PARAMS		18
#define IWM_UCODE_TLV_API_NEW_VERSION		20
#define IWM_UCODE_TLV_API_EXT_SCAN_PRIORITY	24
#define IWM_UCODE_TLV_API_TX_POWER_CHAIN	27
#define IWM_UCODE_TLV_API_SCAN_TSF_REPORT	28
#define IWM_UCODE_TLV_API_TKIP_MIC_KEYS         29
#define IWM_UCODE_TLV_API_STA_TYPE		30
#define IWM_UCODE_TLV_API_NAN2_VER2		31
#define IWM_UCODE_TLV_API_ADAPTIVE_DWELL	32
#define IWM_UCODE_TLV_API_NEW_RX_STATS		35
#define IWM_UCODE_TLV_API_QUOTA_LOW_LATENCY	38
#define IWM_UCODE_TLV_API_ADAPTIVE_DWELL_V2	42
#define IWM_UCODE_TLV_API_SCAN_EXT_CHAN_VER	58
#define IWM_NUM_UCODE_TLV_API			128

#define IWM_UCODE_TLV_API_BITS \
	"\020\10FRAGMENTED_SCAN\11WIFI_MCC_UPDATE\16WIDE_CMD_HDR\22LQ_SS_PARAMS\30EXT_SCAN_PRIO\33TX_POWER_CHAIN\35TKIP_MIC_KEYS"

/**
 * uCode capabilities
 * @IWM_UCODE_TLV_CAPA_D0I3_SUPPORT: supports D0i3
 * @IWM_UCODE_TLV_CAPA_LAR_SUPPORT: supports Location Aware Regulatory
 * @IWM_UCODE_TLV_CAPA_UMAC_SCAN: supports UMAC scan.
 * @IWM_UCODE_TLV_CAPA_BEAMFORMER: supports Beamformer
 * @IWM_UCODE_TLV_CAPA_TOF_SUPPORT: supports Time of Flight (802.11mc FTM)
 * @IWM_UCODE_TLV_CAPA_TDLS_SUPPORT: support basic TDLS functionality
 * @IWM_UCODE_TLV_CAPA_TXPOWER_INSERTION_SUPPORT: supports insertion of current
 *	tx power value into TPC Report action frame and Link Measurement Report
 *	action frame
 * @IWM_UCODE_TLV_CAPA_DS_PARAM_SET_IE_SUPPORT: supports updating current
 *	channel in DS parameter set element in probe requests.
 * @IWM_UCODE_TLV_CAPA_WFA_TPC_REP_IE_SUPPORT: supports adding TPC Report IE in
 *	probe requests.
 * @IWM_UCODE_TLV_CAPA_QUIET_PERIOD_SUPPORT: supports Quiet Period requests
 * @IWM_UCODE_TLV_CAPA_DQA_SUPPORT: supports dynamic queue allocation (DQA),
 *	which also implies support for the scheduler configuration command
 * @IWM_UCODE_TLV_CAPA_TDLS_CHANNEL_SWITCH: supports TDLS channel switching
 * @IWM_UCODE_TLV_CAPA_CNSLDTD_D3_D0_IMG: Consolidated D3-D0 image
 * @IWM_UCODE_TLV_CAPA_HOTSPOT_SUPPORT: supports Hot Spot Command
 * @IWM_UCODE_TLV_CAPA_DC2DC_SUPPORT: supports DC2DC Command
 * @IWM_UCODE_TLV_CAPA_2G_COEX_SUPPORT: supports 2G coex Command
 * @IWM_UCODE_TLV_CAPA_CSUM_SUPPORT: supports TCP Checksum Offload
 * @IWM_UCODE_TLV_CAPA_RADIO_BEACON_STATS: support radio and beacon statistics
 * @IWM_UCODE_TLV_CAPA_P2P_STANDALONE_UAPSD: support p2p standalone U-APSD
 * @IWM_UCODE_TLV_CAPA_BT_COEX_PLCR: enabled BT Coex packet level co-running
 * @IWM_UCODE_TLV_CAPA_LAR_MULTI_MCC: ucode supports LAR updates with different
 *	sources for the MCC. This TLV bit is a future replacement to
 *	IWM_UCODE_TLV_API_WIFI_MCC_UPDATE. When either is set, multi-source LAR
 *	is supported.
 * @IWM_UCODE_TLV_CAPA_BT_COEX_RRC: supports BT Coex RRC
 * @IWM_UCODE_TLV_CAPA_GSCAN_SUPPORT: supports gscan
 * @IWM_UCODE_TLV_CAPA_NAN_SUPPORT: supports NAN
 * @IWM_UCODE_TLV_CAPA_UMAC_UPLOAD: supports upload mode in umac (1=supported,
 *	0=no support)
 * @IWM_UCODE_TLV_CAPA_EXTENDED_DTS_MEASURE: extended DTS measurement
 * @IWM_UCODE_TLV_CAPA_SHORT_PM_TIMEOUTS: supports short PM timeouts
 * @IWM_UCODE_TLV_CAPA_BT_MPLUT_SUPPORT: supports bt-coex Multi-priority LUT
 * @IWM_UCODE_TLV_CAPA_BEACON_ANT_SELECTION: firmware will decide on what
 *	antenna the beacon should be transmitted
 * @IWM_UCODE_TLV_CAPA_BEACON_STORING: firmware will store the latest beacon
 *	from AP and will send it upon d0i3 exit.
 * @IWM_UCODE_TLV_CAPA_LAR_SUPPORT_V2: support LAR API V2
 * @IWM_UCODE_TLV_CAPA_CT_KILL_BY_FW: firmware responsible for CT-kill
 * @IWM_UCODE_TLV_CAPA_TEMP_THS_REPORT_SUPPORT: supports temperature
 *	thresholds reporting
 * @IWM_UCODE_TLV_CAPA_CTDP_SUPPORT: supports cTDP command
 * @IWM_UCODE_TLV_CAPA_USNIFFER_UNIFIED: supports usniffer enabled in
 *	regular image.
 * @IWM_UCODE_TLV_CAPA_EXTEND_SHARED_MEM_CFG: support getting more shared
 *	memory addresses from the firmware.
 * @IWM_UCODE_TLV_CAPA_LQM_SUPPORT: supports Link Quality Measurement
 * @IWM_UCODE_TLV_CAPA_LMAC_UPLOAD: supports upload mode in lmac (1=supported,
 *	0=no support)
 *
 * @IWM_NUM_UCODE_TLV_CAPA: number of bits used
 */
#define IWM_UCODE_TLV_CAPA_D0I3_SUPPORT			0
#define IWM_UCODE_TLV_CAPA_LAR_SUPPORT			1
#define IWM_UCODE_TLV_CAPA_UMAC_SCAN			2
#define IWM_UCODE_TLV_CAPA_BEAMFORMER			3
#define IWM_UCODE_TLV_CAPA_TOF_SUPPORT                  5
#define IWM_UCODE_TLV_CAPA_TDLS_SUPPORT			6
#define IWM_UCODE_TLV_CAPA_TXPOWER_INSERTION_SUPPORT	8
#define IWM_UCODE_TLV_CAPA_DS_PARAM_SET_IE_SUPPORT	9
#define IWM_UCODE_TLV_CAPA_WFA_TPC_REP_IE_SUPPORT	10
#define IWM_UCODE_TLV_CAPA_QUIET_PERIOD_SUPPORT		11
#define IWM_UCODE_TLV_CAPA_DQA_SUPPORT			12
#define IWM_UCODE_TLV_CAPA_TDLS_CHANNEL_SWITCH		13
#define IWM_UCODE_TLV_CAPA_CNSLDTD_D3_D0_IMG		17
#define IWM_UCODE_TLV_CAPA_HOTSPOT_SUPPORT		18
#define IWM_UCODE_TLV_CAPA_DC2DC_CONFIG_SUPPORT		19
#define IWM_UCODE_TLV_CAPA_2G_COEX_SUPPORT		20
#define IWM_UCODE_TLV_CAPA_CSUM_SUPPORT			21
#define IWM_UCODE_TLV_CAPA_RADIO_BEACON_STATS		22
#define IWM_UCODE_TLV_CAPA_P2P_STANDALONE_UAPSD		26
#define IWM_UCODE_TLV_CAPA_BT_COEX_PLCR			28
#define IWM_UCODE_TLV_CAPA_LAR_MULTI_MCC		29
#define IWM_UCODE_TLV_CAPA_BT_COEX_RRC			30
#define IWM_UCODE_TLV_CAPA_GSCAN_SUPPORT		31
#define IWM_UCODE_TLV_CAPA_NAN_SUPPORT			34
#define IWM_UCODE_TLV_CAPA_UMAC_UPLOAD			35
#define IWM_UCODE_TLV_CAPA_SOC_LATENCY_SUPPORT		37
#define IWM_UCODE_TLV_CAPA_BINDING_CDB_SUPPORT		39
#define IWM_UCODE_TLV_CAPA_CDB_SUPPORT			40
#define IWM_UCODE_TLV_CAPA_DYNAMIC_QUOTA                44
#define IWM_UCODE_TLV_CAPA_ULTRA_HB_CHANNELS		48
#define IWM_UCODE_TLV_CAPA_EXTENDED_DTS_MEASURE		64
#define IWM_UCODE_TLV_CAPA_SHORT_PM_TIMEOUTS		65
#define IWM_UCODE_TLV_CAPA_BT_MPLUT_SUPPORT		67
#define IWM_UCODE_TLV_CAPA_MULTI_QUEUE_RX_SUPPORT	68
#define IWM_UCODE_TLV_CAPA_BEACON_ANT_SELECTION		71
#define IWM_UCODE_TLV_CAPA_BEACON_STORING		72
#define IWM_UCODE_TLV_CAPA_LAR_SUPPORT_V3		73
#define IWM_UCODE_TLV_CAPA_CT_KILL_BY_FW		74
#define IWM_UCODE_TLV_CAPA_TEMP_THS_REPORT_SUPPORT	75
#define IWM_UCODE_TLV_CAPA_CTDP_SUPPORT			76
#define IWM_UCODE_TLV_CAPA_USNIFFER_UNIFIED		77
#define IWM_UCODE_TLV_CAPA_LMAC_UPLOAD			79
#define IWM_UCODE_TLV_CAPA_EXTEND_SHARED_MEM_CFG	80
#define IWM_UCODE_TLV_CAPA_LQM_SUPPORT			81

#define IWM_NUM_UCODE_TLV_CAPA 128

/* The default calibrate table size if not specified by firmware file */
#define IWM_DEFAULT_STANDARD_PHY_CALIBRATE_TBL_SIZE	18
#define IWM_MAX_STANDARD_PHY_CALIBRATE_TBL_SIZE		19
#define IWM_MAX_PHY_CALIBRATE_TBL_SIZE			253

/* The default max probe length if not specified by the firmware file */
#define IWM_DEFAULT_MAX_PROBE_LENGTH	200

/*
 * For 16.0 uCode and above, there is no differentiation between sections,
 * just an offset to the HW address.
 */
#define IWM_CPU1_CPU2_SEPARATOR_SECTION		0xFFFFCCCC
#define IWM_PAGING_SEPARATOR_SECTION		0xAAAABBBB

/* uCode version contains 4 values: Major/Minor/API/Serial */
#define IWM_UCODE_MAJOR(ver)	(((ver) & 0xFF000000) >> 24)
#define IWM_UCODE_MINOR(ver)	(((ver) & 0x00FF0000) >> 16)
#define IWM_UCODE_API(ver)	(((ver) & 0x0000FF00) >> 8)
#define IWM_UCODE_SERIAL(ver)	((ver) & 0x000000FF)

/*
 * Calibration control struct.
 * Sent as part of the phy configuration command.
 * @flow_trigger: bitmap for which calibrations to perform according to
 *		flow triggers.
 * @event_trigger: bitmap for which calibrations to perform according to
 *		event triggers.
 */
struct iwm_tlv_calib_ctrl {
	uint32_t flow_trigger;
	uint32_t event_trigger;
} __packed;

#define IWM_FW_PHY_CFG_RADIO_TYPE_POS	0
#define IWM_FW_PHY_CFG_RADIO_TYPE	(0x3 << IWM_FW_PHY_CFG_RADIO_TYPE_POS)
#define IWM_FW_PHY_CFG_RADIO_STEP_POS	2
#define IWM_FW_PHY_CFG_RADIO_STEP	(0x3 << IWM_FW_PHY_CFG_RADIO_STEP_POS)
#define IWM_FW_PHY_CFG_RADIO_DASH_POS	4
#define IWM_FW_PHY_CFG_RADIO_DASH	(0x3 << IWM_FW_PHY_CFG_RADIO_DASH_POS)
#define IWM_FW_PHY_CFG_TX_CHAIN_POS	16
#define IWM_FW_PHY_CFG_TX_CHAIN		(0xf << IWM_FW_PHY_CFG_TX_CHAIN_POS)
#define IWM_FW_PHY_CFG_RX_CHAIN_POS	20
#define IWM_FW_PHY_CFG_RX_CHAIN		(0xf << IWM_FW_PHY_CFG_RX_CHAIN_POS)
#define IWM_FW_PHY_CFG_SHARED_CLK	(1U << 31)

#define IWM_UCODE_MAX_CS		1

/**
 * struct iwm_fw_cipher_scheme - a cipher scheme supported by FW.
 * @cipher: a cipher suite selector
 * @flags: cipher scheme flags (currently reserved for a future use)
 * @hdr_len: a size of MPDU security header
 * @pn_len: a size of PN
 * @pn_off: an offset of pn from the beginning of the security header
 * @key_idx_off: an offset of key index byte in the security header
 * @key_idx_mask: a bit mask of key_idx bits
 * @key_idx_shift: bit shift needed to get key_idx
 * @mic_len: mic length in bytes
 * @hw_cipher: a HW cipher index used in host commands
 */
struct iwm_fw_cipher_scheme {
	uint32_t cipher;
	uint8_t flags;
	uint8_t hdr_len;
	uint8_t pn_len;
	uint8_t pn_off;
	uint8_t key_idx_off;
	uint8_t key_idx_mask;
	uint8_t key_idx_shift;
	uint8_t mic_len;
	uint8_t hw_cipher;
} __packed;

/**
 * struct iwm_fw_cscheme_list - a cipher scheme list
 * @size: a number of entries
 * @cs: cipher scheme entries
 */
struct iwm_fw_cscheme_list {
	uint8_t size;
	struct iwm_fw_cipher_scheme cs[];
} __packed;

/* v1/v2 uCode file layout */
struct iwm_ucode_header {
	uint32_t ver;	/* major/minor/API/serial */
	union {
		struct {
			uint32_t inst_size;	/* bytes of runtime code */
			uint32_t data_size;	/* bytes of runtime data */
			uint32_t init_size;	/* bytes of init code */
			uint32_t init_data_size;	/* bytes of init data */
			uint32_t boot_size;	/* bytes of bootstrap code */
			uint8_t data[0];		/* in same order as sizes */
		} v1;
		struct {
			uint32_t build;		/* build number */
			uint32_t inst_size;	/* bytes of runtime code */
			uint32_t data_size;	/* bytes of runtime data */
			uint32_t init_size;	/* bytes of init code */
			uint32_t init_data_size;	/* bytes of init data */
			uint32_t boot_size;	/* bytes of bootstrap code */
			uint8_t data[0];		/* in same order as sizes */
		} v2;
	} u;
};

/*
 * new TLV uCode file layout
 *
 * The new TLV file format contains TLVs, that each specify
 * some piece of data.
 */

#define IWM_UCODE_TLV_INVALID		0 /* unused */
#define IWM_UCODE_TLV_INST		1
#define IWM_UCODE_TLV_DATA		2
#define IWM_UCODE_TLV_INIT		3
#define IWM_UCODE_TLV_INIT_DATA		4
#define IWM_UCODE_TLV_BOOT		5
#define IWM_UCODE_TLV_PROBE_MAX_LEN	6 /* a uint32_t value */
#define IWM_UCODE_TLV_PAN		7
#define IWM_UCODE_TLV_RUNT_EVTLOG_PTR	8
#define IWM_UCODE_TLV_RUNT_EVTLOG_SIZE	9
#define IWM_UCODE_TLV_RUNT_ERRLOG_PTR	10
#define IWM_UCODE_TLV_INIT_EVTLOG_PTR	11
#define IWM_UCODE_TLV_INIT_EVTLOG_SIZE	12
#define IWM_UCODE_TLV_INIT_ERRLOG_PTR	13
#define IWM_UCODE_TLV_ENHANCE_SENS_TBL	14
#define IWM_UCODE_TLV_PHY_CALIBRATION_SIZE 15
#define IWM_UCODE_TLV_WOWLAN_INST	16
#define IWM_UCODE_TLV_WOWLAN_DATA	17
#define IWM_UCODE_TLV_FLAGS		18
#define IWM_UCODE_TLV_SEC_RT		19
#define IWM_UCODE_TLV_SEC_INIT		20
#define IWM_UCODE_TLV_SEC_WOWLAN	21
#define IWM_UCODE_TLV_DEF_CALIB		22
#define IWM_UCODE_TLV_PHY_SKU		23
#define IWM_UCODE_TLV_SECURE_SEC_RT	24
#define IWM_UCODE_TLV_SECURE_SEC_INIT	25
#define IWM_UCODE_TLV_SECURE_SEC_WOWLAN	26
#define IWM_UCODE_TLV_NUM_OF_CPU	27
#define IWM_UCODE_TLV_CSCHEME		28

	/*
	 * Following two are not in our base tag, but allow
	 * handling ucode version 9.
	 */
#define IWM_UCODE_TLV_API_CHANGES_SET	29
#define IWM_UCODE_TLV_ENABLED_CAPABILITIES 30
#define IWM_UCODE_TLV_N_SCAN_CHANNELS	31
#define IWM_UCODE_TLV_PAGING		32
#define IWM_UCODE_TLV_SEC_RT_USNIFFER	34
#define IWM_UCODE_TLV_SDIO_ADMA_ADDR	35
#define IWM_UCODE_TLV_FW_VERSION	36
#define IWM_UCODE_TLV_FW_DBG_DEST	38
#define IWM_UCODE_TLV_FW_DBG_CONF	39
#define IWM_UCODE_TLV_FW_DBG_TRIGGER	40
#define IWM_UCODE_TLV_CMD_VERSIONS	48
#define IWM_UCODE_TLV_FW_GSCAN_CAPA	50
#define IWM_UCODE_TLV_FW_MEM_SEG	51
#define IWM_UCODE_TLV_UMAC_DEBUG_ADDRS	54
#define IWM_UCODE_TLV_LMAC_DEBUG_ADDRS	55
#define IWM_UCODE_TLV_HW_TYPE		58

#define IWM_UCODE_TLV_DEBUG_BASE		0x1000005
#define IWM_UCODE_TLV_TYPE_DEBUG_INFO		(IWM_UCODE_TLV_DEBUG_BASE + 0)
#define IWM_UCODE_TLV_TYPE_BUFFER_ALLOCATION	(IWM_UCODE_TLV_DEBUG_BASE + 1)
#define IWM_UCODE_TLV_TYPE_HCMD			(IWM_UCODE_TLV_DEBUG_BASE + 2)
#define IWM_UCODE_TLV_TYPE_REGIONS		(IWM_UCODE_TLV_DEBUG_BASE + 3)
#define IWM_UCODE_TLV_TYPE_TRIGGERS		(IWM_UCODE_TLV_DEBUG_BASE + 4)
#define IWM_UCODE_TLV_DEBUG_MAX			IWM_UCODE_TLV_TYPE_TRIGGERS

struct iwm_ucode_tlv {
	uint32_t type;		/* see above */
	uint32_t length;		/* not including type/length fields */
	uint8_t data[0];
};

struct iwm_ucode_api {
	uint32_t api_index;
	uint32_t api_flags;
} __packed;

struct iwm_ucode_capa {
	uint32_t api_index;
	uint32_t api_capa;
} __packed;

#define IWM_TLV_UCODE_MAGIC	0x0a4c5749

struct iwm_tlv_ucode_header {
	/*
	 * The TLV style ucode header is distinguished from
	 * the v1/v2 style header by first four bytes being
	 * zero, as such is an invalid combination of
	 * major/minor/API/serial versions.
	 */
	uint32_t zero;
	uint32_t magic;
	uint8_t human_readable[64];
	uint32_t ver;		/* major/minor/API/serial */
	uint32_t build;
	uint64_t ignore;
	/*
	 * The data contained herein has a TLV layout,
	 * see above for the TLV header and types.
	 * Note that each TLV is padded to a length
	 * that is a multiple of 4 for alignment.
	 */
	uint8_t data[0];
};

/*
 * Registers in this file are internal, not PCI bus memory mapped.
 * Driver accesses these via IWM_HBUS_TARG_PRPH_* registers.
 */
#define IWM_PRPH_BASE	(0x00000)
#define IWM_PRPH_END	(0xFFFFF)

/* APMG (power management) constants */
#define IWM_APMG_BASE			(IWM_PRPH_BASE + 0x3000)
#define IWM_APMG_CLK_CTRL_REG		(IWM_APMG_BASE + 0x0000)
#define IWM_APMG_CLK_EN_REG		(IWM_APMG_BASE + 0x0004)
#define IWM_APMG_CLK_DIS_REG		(IWM_APMG_BASE + 0x0008)
#define IWM_APMG_PS_CTRL_REG		(IWM_APMG_BASE + 0x000c)
#define IWM_APMG_PCIDEV_STT_REG		(IWM_APMG_BASE + 0x0010)
#define IWM_APMG_RFKILL_REG		(IWM_APMG_BASE + 0x0014)
#define IWM_APMG_RTC_INT_STT_REG	(IWM_APMG_BASE + 0x001c)
#define IWM_APMG_RTC_INT_MSK_REG	(IWM_APMG_BASE + 0x0020)
#define IWM_APMG_DIGITAL_SVR_REG	(IWM_APMG_BASE + 0x0058)
#define IWM_APMG_ANALOG_SVR_REG		(IWM_APMG_BASE + 0x006C)

#define IWM_APMS_CLK_VAL_MRB_FUNC_MODE	(0x00000001)
#define IWM_APMG_CLK_VAL_DMA_CLK_RQT	(0x00000200)
#define IWM_APMG_CLK_VAL_BSM_CLK_RQT	(0x00000800)

#define IWM_APMG_PS_CTRL_EARLY_PWR_OFF_RESET_DIS	(0x00400000)
#define IWM_APMG_PS_CTRL_VAL_RESET_REQ			(0x04000000)
#define IWM_APMG_PS_CTRL_MSK_PWR_SRC			(0x03000000)
#define IWM_APMG_PS_CTRL_VAL_PWR_SRC_VMAIN		(0x00000000)
#define IWM_APMG_PS_CTRL_VAL_PWR_SRC_VAUX		(0x02000000)
#define IWM_APMG_SVR_VOLTAGE_CONFIG_BIT_MSK		(0x000001E0) /* bit 8:5 */
#define IWM_APMG_SVR_DIGITAL_VOLTAGE_1_32		(0x00000060)

#define IWM_APMG_PCIDEV_STT_VAL_L1_ACT_DIS		(0x00000800)

#define IWM_APMG_RTC_INT_STT_RFKILL			(0x10000000)

/* Device system time */
#define IWM_DEVICE_SYSTEM_TIME_REG 0xA0206C

/* Device NMI register */
#define IWM_DEVICE_SET_NMI_REG		0x00a01c30
#define IWM_DEVICE_SET_NMI_VAL_HW	0x01
#define IWM_DEVICE_SET_NMI_VAL_DRV	0x80
#define IWM_DEVICE_SET_NMI_8000_REG	0x00a01c24
#define IWM_DEVICE_SET_NMI_8000_VAL	0x1000000

/*
 * Device reset for family 8000
 * write to bit 24 in order to reset the CPU
*/
#define IWM_RELEASE_CPU_RESET		0x300c
#define IWM_RELEASE_CPU_RESET_BIT	0x1000000


/*****************************************************************************
 *                        7000/3000 series SHR DTS addresses                 *
 *****************************************************************************/

#define IWM_SHR_MISC_WFM_DTS_EN		(0x00a10024)
#define IWM_DTSC_CFG_MODE		(0x00a10604)
#define IWM_DTSC_VREF_AVG		(0x00a10648)
#define IWM_DTSC_VREF5_AVG		(0x00a1064c)
#define IWM_DTSC_CFG_MODE_PERIODIC	(0x2)
#define IWM_DTSC_PTAT_AVG		(0x00a10650)


/**
 * Tx Scheduler
 *
 * The Tx Scheduler selects the next frame to be transmitted, choosing TFDs
 * (Transmit Frame Descriptors) from up to 16 circular Tx queues resident in
 * host DRAM.  It steers each frame's Tx command (which contains the frame
 * data) into one of up to 7 prioritized Tx DMA FIFO channels within the
 * device.  A queue maps to only one (selectable by driver) Tx DMA channel,
 * but one DMA channel may take input from several queues.
 *
 * Tx DMA FIFOs have dedicated purposes.
 *
 * For 5000 series and up, they are used differently
 * (cf. iwl5000_default_queue_to_tx_fifo in iwl-5000.c):
 *
 * 0 -- EDCA BK (background) frames, lowest priority
 * 1 -- EDCA BE (best effort) frames, normal priority
 * 2 -- EDCA VI (video) frames, higher priority
 * 3 -- EDCA VO (voice) and management frames, highest priority
 * 4 -- unused
 * 5 -- unused
 * 6 -- unused
 * 7 -- Commands
 *
 * Driver should normally map queues 0-6 to Tx DMA/FIFO channels 0-6.
 * In addition, driver can map the remaining queues to Tx DMA/FIFO
 * channels 0-3 to support 11n aggregation via EDCA DMA channels.
 *
 * The driver sets up each queue to work in one of two modes:
 *
 * 1)  Scheduler-Ack, in which the scheduler automatically supports a
 *     block-ack (BA) window of up to 64 TFDs.  In this mode, each queue
 *     contains TFDs for a unique combination of Recipient Address (RA)
 *     and Traffic Identifier (TID), that is, traffic of a given
 *     Quality-Of-Service (QOS) priority, destined for a single station.
 *
 *     In scheduler-ack mode, the scheduler keeps track of the Tx status of
 *     each frame within the BA window, including whether it's been transmitted,
 *     and whether it's been acknowledged by the receiving station.  The device
 *     automatically processes block-acks received from the receiving STA,
 *     and reschedules un-acked frames to be retransmitted (successful
 *     Tx completion may end up being out-of-order).
 *
 *     The driver must maintain the queue's Byte Count table in host DRAM
 *     for this mode.
 *     This mode does not support fragmentation.
 *
 * 2)  FIFO (a.k.a. non-Scheduler-ACK), in which each TFD is processed in order.
 *     The device may automatically retry Tx, but will retry only one frame
 *     at a time, until receiving ACK from receiving station, or reaching
 *     retry limit and giving up.
 *
 *     The command queue (#4/#9) must use this mode!
 *     This mode does not require use of the Byte Count table in host DRAM.
 *
 * Driver controls scheduler operation via 3 means:
 * 1)  Scheduler registers
 * 2)  Shared scheduler data base in internal SRAM
 * 3)  Shared data in host DRAM
 *
 * Initialization:
 *
 * When loading, driver should allocate memory for:
 * 1)  16 TFD circular buffers, each with space for (typically) 256 TFDs.
 * 2)  16 Byte Count circular buffers in 16 KBytes contiguous memory
 *     (1024 bytes for each queue).
 *
 * After receiving "Alive" response from uCode, driver must initialize
 * the scheduler (especially for queue #4/#9, the command queue, otherwise
 * the driver can't issue commands!):
 */
#define IWM_SCD_MEM_LOWER_BOUND		(0x0000)

/**
 * Max Tx window size is the max number of contiguous TFDs that the scheduler
 * can keep track of at one time when creating block-ack chains of frames.
 * Note that "64" matches the number of ack bits in a block-ack packet.
 */
#define IWM_SCD_WIN_SIZE				64
#define IWM_SCD_FRAME_LIMIT				64

#define IWM_SCD_TXFIFO_POS_TID			(0)
#define IWM_SCD_TXFIFO_POS_RA			(4)
#define IWM_SCD_QUEUE_RA_TID_MAP_RATID_MSK	(0x01FF)

/* agn SCD */
#define IWM_SCD_QUEUE_STTS_REG_POS_TXF		(0)
#define IWM_SCD_QUEUE_STTS_REG_POS_ACTIVE	(3)
#define IWM_SCD_QUEUE_STTS_REG_POS_WSL		(4)
#define IWM_SCD_QUEUE_STTS_REG_POS_SCD_ACT_EN	(19)
#define IWM_SCD_QUEUE_STTS_REG_MSK		(0x017F0000)

#define IWM_SCD_QUEUE_CTX_REG1_CREDIT_POS	(8)
#define IWM_SCD_QUEUE_CTX_REG1_CREDIT_MSK	(0x00FFFF00)
#define IWM_SCD_QUEUE_CTX_REG1_SUPER_CREDIT_POS	(24)
#define IWM_SCD_QUEUE_CTX_REG1_SUPER_CREDIT_MSK	(0xFF000000)
#define IWM_SCD_QUEUE_CTX_REG2_WIN_SIZE_POS	(0)
#define IWM_SCD_QUEUE_CTX_REG2_WIN_SIZE_MSK	(0x0000007F)
#define IWM_SCD_QUEUE_CTX_REG2_FRAME_LIMIT_POS	(16)
#define IWM_SCD_QUEUE_CTX_REG2_FRAME_LIMIT_MSK	(0x007F0000)
#define IWM_SCD_GP_CTRL_ENABLE_31_QUEUES	(1 << 0)
#define IWM_SCD_GP_CTRL_AUTO_ACTIVE_MODE	(1 << 18)

/* Context Data */
#define IWM_SCD_CONTEXT_MEM_LOWER_BOUND	(IWM_SCD_MEM_LOWER_BOUND + 0x600)
#define IWM_SCD_CONTEXT_MEM_UPPER_BOUND	(IWM_SCD_MEM_LOWER_BOUND + 0x6A0)

/* Tx status */
#define IWM_SCD_TX_STTS_MEM_LOWER_BOUND	(IWM_SCD_MEM_LOWER_BOUND + 0x6A0)
#define IWM_SCD_TX_STTS_MEM_UPPER_BOUND	(IWM_SCD_MEM_LOWER_BOUND + 0x7E0)

/* Translation Data */
#define IWM_SCD_TRANS_TBL_MEM_LOWER_BOUND (IWM_SCD_MEM_LOWER_BOUND + 0x7E0)
#define IWM_SCD_TRANS_TBL_MEM_UPPER_BOUND (IWM_SCD_MEM_LOWER_BOUND + 0x808)

#define IWM_SCD_CONTEXT_QUEUE_OFFSET(x)\
	(IWM_SCD_CONTEXT_MEM_LOWER_BOUND + ((x) * 8))

#define IWM_SCD_TX_STTS_QUEUE_OFFSET(x)\
	(IWM_SCD_TX_STTS_MEM_LOWER_BOUND + ((x) * 16))

#define IWM_SCD_TRANS_TBL_OFFSET_QUEUE(x) \
	((IWM_SCD_TRANS_TBL_MEM_LOWER_BOUND + ((x) * 2)) & 0xfffc)

#define IWM_SCD_BASE			(IWM_PRPH_BASE + 0xa02c00)

#define IWM_SCD_SRAM_BASE_ADDR	(IWM_SCD_BASE + 0x0)
#define IWM_SCD_DRAM_BASE_ADDR	(IWM_SCD_BASE + 0x8)
#define IWM_SCD_AIT		(IWM_SCD_BASE + 0x0c)
#define IWM_SCD_TXFACT		(IWM_SCD_BASE + 0x10)
#define IWM_SCD_ACTIVE		(IWM_SCD_BASE + 0x14)
#define IWM_SCD_QUEUECHAIN_SEL	(IWM_SCD_BASE + 0xe8)
#define IWM_SCD_CHAINEXT_EN	(IWM_SCD_BASE + 0x244)
#define IWM_SCD_AGGR_SEL	(IWM_SCD_BASE + 0x248)
#define IWM_SCD_INTERRUPT_MASK	(IWM_SCD_BASE + 0x108)
#define IWM_SCD_GP_CTRL		(IWM_SCD_BASE + 0x1a8)
#define IWM_SCD_EN_CTRL		(IWM_SCD_BASE + 0x254)

static inline unsigned int IWM_SCD_QUEUE_WRPTR(unsigned int chnl)
{
	if (chnl < 20)
		return IWM_SCD_BASE + 0x18 + chnl * 4;
	return IWM_SCD_BASE + 0x284 + (chnl - 20) * 4;
}

static inline unsigned int IWM_SCD_QUEUE_RDPTR(unsigned int chnl)
{
	if (chnl < 20)
		return IWM_SCD_BASE + 0x68 + chnl * 4;
	return IWM_SCD_BASE + 0x2B4 + chnl * 4;
}

static inline unsigned int IWM_SCD_QUEUE_STATUS_BITS(unsigned int chnl)
{
	if (chnl < 20)
		return IWM_SCD_BASE + 0x10c + chnl * 4;
	return IWM_SCD_BASE + 0x334 + chnl * 4;
}

/*********************** END TX SCHEDULER *************************************/

/* Oscillator clock */
#define IWM_OSC_CLK				(0xa04068)
#define IWM_OSC_CLK_FORCE_CONTROL		(0x8)

/****************************/
/* Flow Handler Definitions */
/****************************/

/**
 * This I/O area is directly read/writable by driver (e.g. Linux uses writel())
 * Addresses are offsets from device's PCI hardware base address.
 */
#define IWM_FH_MEM_LOWER_BOUND                   (0x1000)
#define IWM_FH_MEM_UPPER_BOUND                   (0x2000)

/**
 * Keep-Warm (KW) buffer base address.
 *
 * Driver must allocate a 4KByte buffer that is for keeping the
 * host DRAM powered on (via dummy accesses to DRAM) to maintain low-latency
 * DRAM access when doing Txing or Rxing.  The dummy accesses prevent host
 * from going into a power-savings mode that would cause higher DRAM latency,
 * and possible data over/under-runs, before all Tx/Rx is complete.
 *
 * Driver loads IWM_FH_KW_MEM_ADDR_REG with the physical address (bits 35:4)
 * of the buffer, which must be 4K aligned.  Once this is set up, the device
 * automatically invokes keep-warm accesses when normal accesses might not
 * be sufficient to maintain fast DRAM response.
 *
 * Bit fields:
 *  31-0:  Keep-warm buffer physical base address [35:4], must be 4K aligned
 */
#define IWM_FH_KW_MEM_ADDR_REG		     (IWM_FH_MEM_LOWER_BOUND + 0x97C)


/**
 * TFD Circular Buffers Base (CBBC) addresses
 *
 * Device has 16 base pointer registers, one for each of 16 host-DRAM-resident
 * circular buffers (CBs/queues) containing Transmit Frame Descriptors (TFDs)
 * (see struct iwm_tfd_frame).  These 16 pointer registers are offset by 0x04
 * bytes from one another.  Each TFD circular buffer in DRAM must be 256-byte
 * aligned (address bits 0-7 must be 0).
 * Later devices have 20 (5000 series) or 30 (higher) queues, but the registers
 * for them are in different places.
 *
 * Bit fields in each pointer register:
 *  27-0: TFD CB physical base address [35:8], must be 256-byte aligned
 */
#define IWM_FH_MEM_CBBC_0_15_LOWER_BOUND	(IWM_FH_MEM_LOWER_BOUND + 0x9D0)
#define IWM_FH_MEM_CBBC_0_15_UPPER_BOUN		(IWM_FH_MEM_LOWER_BOUND + 0xA10)
#define IWM_FH_MEM_CBBC_16_19_LOWER_BOUND	(IWM_FH_MEM_LOWER_BOUND + 0xBF0)
#define IWM_FH_MEM_CBBC_16_19_UPPER_BOUND	(IWM_FH_MEM_LOWER_BOUND + 0xC00)
#define IWM_FH_MEM_CBBC_20_31_LOWER_BOUND	(IWM_FH_MEM_LOWER_BOUND + 0xB20)
#define IWM_FH_MEM_CBBC_20_31_UPPER_BOUND	(IWM_FH_MEM_LOWER_BOUND + 0xB80)

/* Find TFD CB base pointer for given queue */
static inline unsigned int IWM_FH_MEM_CBBC_QUEUE(unsigned int chnl)
{
	if (chnl < 16)
		return IWM_FH_MEM_CBBC_0_15_LOWER_BOUND + 4 * chnl;
	if (chnl < 20)
		return IWM_FH_MEM_CBBC_16_19_LOWER_BOUND + 4 * (chnl - 16);
	return IWM_FH_MEM_CBBC_20_31_LOWER_BOUND + 4 * (chnl - 20);
}


/**
 * Rx SRAM Control and Status Registers (RSCSR)
 *
 * These registers provide handshake between driver and device for the Rx queue
 * (this queue handles *all* command responses, notifications, Rx data, etc.
 * sent from uCode to host driver).  Unlike Tx, there is only one Rx
 * queue, and only one Rx DMA/FIFO channel.  Also unlike Tx, which can
 * concatenate up to 20 DRAM buffers to form a Tx frame, each Receive Buffer
 * Descriptor (RBD) points to only one Rx Buffer (RB); there is a 1:1
 * mapping between RBDs and RBs.
 *
 * Driver must allocate host DRAM memory for the following, and set the
 * physical address of each into device registers:
 *
 * 1)  Receive Buffer Descriptor (RBD) circular buffer (CB), typically with 256
 *     entries (although any power of 2, up to 4096, is selectable by driver).
 *     Each entry (1 dword) points to a receive buffer (RB) of consistent size
 *     (typically 4K, although 8K or 16K are also selectable by driver).
 *     Driver sets up RB size and number of RBDs in the CB via Rx config
 *     register IWM_FH_MEM_RCSR_CHNL0_CONFIG_REG.
 *
 *     Bit fields within one RBD:
 *     27-0:  Receive Buffer physical address bits [35:8], 256-byte aligned
 *
 *     Driver sets physical address [35:8] of base of RBD circular buffer
 *     into IWM_FH_RSCSR_CHNL0_RBDCB_BASE_REG [27:0].
 *
 * 2)  Rx status buffer, 8 bytes, in which uCode indicates which Rx Buffers
 *     (RBs) have been filled, via a "write pointer", actually the index of
 *     the RB's corresponding RBD within the circular buffer.  Driver sets
 *     physical address [35:4] into IWM_FH_RSCSR_CHNL0_STTS_WPTR_REG [31:0].
 *
 *     Bit fields in lower dword of Rx status buffer (upper dword not used
 *     by driver:
 *     31-12:  Not used by driver
 *     11- 0:  Index of last filled Rx buffer descriptor
 *             (device writes, driver reads this value)
 *
 * As the driver prepares Receive Buffers (RBs) for device to fill, driver must
 * enter pointers to these RBs into contiguous RBD circular buffer entries,
 * and update the device's "write" index register,
 * IWM_FH_RSCSR_CHNL0_RBDCB_WPTR_REG.
 *
 * This "write" index corresponds to the *next* RBD that the driver will make
 * available, i.e. one RBD past the tail of the ready-to-fill RBDs within
 * the circular buffer.  This value should initially be 0 (before preparing any
 * RBs), should be 8 after preparing the first 8 RBs (for example), and must
 * wrap back to 0 at the end of the circular buffer (but don't wrap before
 * "read" index has advanced past 1!  See below).
 * NOTE:  DEVICE EXPECTS THE WRITE INDEX TO BE INCREMENTED IN MULTIPLES OF 8.
 *
 * As the device fills RBs (referenced from contiguous RBDs within the circular
 * buffer), it updates the Rx status buffer in host DRAM, 2) described above,
 * to tell the driver the index of the latest filled RBD.  The driver must
 * read this "read" index from DRAM after receiving an Rx interrupt from device
 *
 * The driver must also internally keep track of a third index, which is the
 * next RBD to process.  When receiving an Rx interrupt, driver should process
 * all filled but unprocessed RBs up to, but not including, the RB
 * corresponding to the "read" index.  For example, if "read" index becomes "1",
 * driver may process the RB pointed to by RBD 0.  Depending on volume of
 * traffic, there may be many RBs to process.
 *
 * If read index == write index, device thinks there is no room to put new data.
 * Due to this, the maximum number of filled RBs is 255, instead of 256.  To
 * be safe, make sure that there is a gap of at least 2 RBDs between "write"
 * and "read" indexes; that is, make sure that there are no more than 254
 * buffers waiting to be filled.
 */
#define IWM_FH_MEM_RSCSR_LOWER_BOUND	(IWM_FH_MEM_LOWER_BOUND + 0xBC0)
#define IWM_FH_MEM_RSCSR_UPPER_BOUND	(IWM_FH_MEM_LOWER_BOUND + 0xC00)
#define IWM_FH_MEM_RSCSR_CHNL0		(IWM_FH_MEM_RSCSR_LOWER_BOUND)

/**
 * Physical base address of 8-byte Rx Status buffer.
 * Bit fields:
 *  31-0: Rx status buffer physical base address [35:4], must 16-byte aligned.
 */
#define IWM_FH_RSCSR_CHNL0_STTS_WPTR_REG	(IWM_FH_MEM_RSCSR_CHNL0)

/**
 * Physical base address of Rx Buffer Descriptor Circular Buffer.
 * Bit fields:
 *  27-0:  RBD CD physical base address [35:8], must be 256-byte aligned.
 */
#define IWM_FH_RSCSR_CHNL0_RBDCB_BASE_REG	(IWM_FH_MEM_RSCSR_CHNL0 + 0x004)

/**
 * Rx write pointer (index, really!).
 * Bit fields:
 *  11-0:  Index of driver's most recent prepared-to-be-filled RBD, + 1.
 *         NOTE:  For 256-entry circular buffer, use only bits [7:0].
 */
#define IWM_FH_RSCSR_CHNL0_RBDCB_WPTR_REG	(IWM_FH_MEM_RSCSR_CHNL0 + 0x008)
#define IWM_FH_RSCSR_CHNL0_WPTR		(IWM_FH_RSCSR_CHNL0_RBDCB_WPTR_REG)

#define IWM_FW_RSCSR_CHNL0_RXDCB_RDPTR_REG	(IWM_FH_MEM_RSCSR_CHNL0 + 0x00c)
#define IWM_FH_RSCSR_CHNL0_RDPTR		IWM_FW_RSCSR_CHNL0_RXDCB_RDPTR_REG

/**
 * Rx Config/Status Registers (RCSR)
 * Rx Config Reg for channel 0 (only channel used)
 *
 * Driver must initialize IWM_FH_MEM_RCSR_CHNL0_CONFIG_REG as follows for
 * normal operation (see bit fields).
 *
 * Clearing IWM_FH_MEM_RCSR_CHNL0_CONFIG_REG to 0 turns off Rx DMA.
 * Driver should poll IWM_FH_MEM_RSSR_RX_STATUS_REG	for
 * IWM_FH_RSSR_CHNL0_RX_STATUS_CHNL_IDLE (bit 24) before continuing.
 *
 * Bit fields:
 * 31-30: Rx DMA channel enable: '00' off/pause, '01' pause at end of frame,
 *        '10' operate normally
 * 29-24: reserved
 * 23-20: # RBDs in circular buffer = 2^value; use "8" for 256 RBDs (normal),
 *        min "5" for 32 RBDs, max "12" for 4096 RBDs.
 * 19-18: reserved
 * 17-16: size of each receive buffer; '00' 4K (normal), '01' 8K,
 *        '10' 12K, '11' 16K.
 * 15-14: reserved
 * 13-12: IRQ destination; '00' none, '01' host driver (normal operation)
 * 11- 4: timeout for closing Rx buffer and interrupting host (units 32 usec)
 *        typical value 0x10 (about 1/2 msec)
 *  3- 0: reserved
 */
#define IWM_FH_MEM_RCSR_LOWER_BOUND      (IWM_FH_MEM_LOWER_BOUND + 0xC00)
#define IWM_FH_MEM_RCSR_UPPER_BOUND      (IWM_FH_MEM_LOWER_BOUND + 0xCC0)
#define IWM_FH_MEM_RCSR_CHNL0            (IWM_FH_MEM_RCSR_LOWER_BOUND)

#define IWM_FH_MEM_RCSR_CHNL0_CONFIG_REG	(IWM_FH_MEM_RCSR_CHNL0)
#define IWM_FH_MEM_RCSR_CHNL0_RBDCB_WPTR	(IWM_FH_MEM_RCSR_CHNL0 + 0x8)
#define IWM_FH_MEM_RCSR_CHNL0_FLUSH_RB_REQ	(IWM_FH_MEM_RCSR_CHNL0 + 0x10)

#define IWM_FH_RCSR_CHNL0_RX_CONFIG_RB_TIMEOUT_MSK (0x00000FF0) /* bits 4-11 */
#define IWM_FH_RCSR_CHNL0_RX_CONFIG_IRQ_DEST_MSK   (0x00001000) /* bits 12 */
#define IWM_FH_RCSR_CHNL0_RX_CONFIG_SINGLE_FRAME_MSK (0x00008000) /* bit 15 */
#define IWM_FH_RCSR_CHNL0_RX_CONFIG_RB_SIZE_MSK   (0x00030000) /* bits 16-17 */
#define IWM_FH_RCSR_CHNL0_RX_CONFIG_RBDBC_SIZE_MSK (0x00F00000) /* bits 20-23 */
#define IWM_FH_RCSR_CHNL0_RX_CONFIG_DMA_CHNL_EN_MSK (0xC0000000) /* bits 30-31*/

#define IWM_FH_RCSR_RX_CONFIG_RBDCB_SIZE_POS	(20)
#define IWM_FH_RCSR_RX_CONFIG_REG_IRQ_RBTH_POS	(4)
#define IWM_RX_RB_TIMEOUT	(0x11)

#define IWM_FH_RCSR_RX_CONFIG_CHNL_EN_PAUSE_VAL         (0x00000000)
#define IWM_FH_RCSR_RX_CONFIG_CHNL_EN_PAUSE_EOF_VAL     (0x40000000)
#define IWM_FH_RCSR_RX_CONFIG_CHNL_EN_ENABLE_VAL        (0x80000000)

#define IWM_FH_RCSR_RX_CONFIG_REG_VAL_RB_SIZE_4K    (0x00000000)
#define IWM_FH_RCSR_RX_CONFIG_REG_VAL_RB_SIZE_8K    (0x00010000)
#define IWM_FH_RCSR_RX_CONFIG_REG_VAL_RB_SIZE_12K   (0x00020000)
#define IWM_FH_RCSR_RX_CONFIG_REG_VAL_RB_SIZE_16K   (0x00030000)

#define IWM_FH_RCSR_CHNL0_RX_IGNORE_RXF_EMPTY              (0x00000004)
#define IWM_FH_RCSR_CHNL0_RX_CONFIG_IRQ_DEST_NO_INT_VAL    (0x00000000)
#define IWM_FH_RCSR_CHNL0_RX_CONFIG_IRQ_DEST_INT_HOST_VAL  (0x00001000)

/**
 * Rx Shared Status Registers (RSSR)
 *
 * After stopping Rx DMA channel (writing 0 to
 * IWM_FH_MEM_RCSR_CHNL0_CONFIG_REG), driver must poll
 * IWM_FH_MEM_RSSR_RX_STATUS_REG until Rx channel is idle.
 *
 * Bit fields:
 *  24:  1 = Channel 0 is idle
 *
 * IWM_FH_MEM_RSSR_SHARED_CTRL_REG and IWM_FH_MEM_RSSR_RX_ENABLE_ERR_IRQ2DRV
 * contain default values that should not be altered by the driver.
 */
#define IWM_FH_MEM_RSSR_LOWER_BOUND     (IWM_FH_MEM_LOWER_BOUND + 0xC40)
#define IWM_FH_MEM_RSSR_UPPER_BOUND     (IWM_FH_MEM_LOWER_BOUND + 0xD00)

#define IWM_FH_MEM_RSSR_SHARED_CTRL_REG (IWM_FH_MEM_RSSR_LOWER_BOUND)
#define IWM_FH_MEM_RSSR_RX_STATUS_REG	(IWM_FH_MEM_RSSR_LOWER_BOUND + 0x004)
#define IWM_FH_MEM_RSSR_RX_ENABLE_ERR_IRQ2DRV\
					(IWM_FH_MEM_RSSR_LOWER_BOUND + 0x008)

#define IWM_FH_RSSR_CHNL0_RX_STATUS_CHNL_IDLE	(0x01000000)

#define IWM_FH_MEM_TFDIB_REG1_ADDR_BITSHIFT	28

/* TFDB  Area - TFDs buffer table */
#define IWM_FH_MEM_TFDIB_DRAM_ADDR_LSB_MSK      (0xFFFFFFFF)
#define IWM_FH_TFDIB_LOWER_BOUND       (IWM_FH_MEM_LOWER_BOUND + 0x900)
#define IWM_FH_TFDIB_UPPER_BOUND       (IWM_FH_MEM_LOWER_BOUND + 0x958)
#define IWM_FH_TFDIB_CTRL0_REG(_chnl)  (IWM_FH_TFDIB_LOWER_BOUND + 0x8 * (_chnl))
#define IWM_FH_TFDIB_CTRL1_REG(_chnl)  (IWM_FH_TFDIB_LOWER_BOUND + 0x8 * (_chnl) + 0x4)

/**
 * Transmit DMA Channel Control/Status Registers (TCSR)
 *
 * Device has one configuration register for each of 8 Tx DMA/FIFO channels
 * supported in hardware (don't confuse these with the 16 Tx queues in DRAM,
 * which feed the DMA/FIFO channels); config regs are separated by 0x20 bytes.
 *
 * To use a Tx DMA channel, driver must initialize its
 * IWM_FH_TCSR_CHNL_TX_CONFIG_REG(chnl) with:
 *
 * IWM_FH_TCSR_TX_CONFIG_REG_VAL_DMA_CHNL_ENABLE |
 * IWM_FH_TCSR_TX_CONFIG_REG_VAL_DMA_CREDIT_ENABLE_VAL
 *
 * All other bits should be 0.
 *
 * Bit fields:
 * 31-30: Tx DMA channel enable: '00' off/pause, '01' pause at end of frame,
 *        '10' operate normally
 * 29- 4: Reserved, set to "0"
 *     3: Enable internal DMA requests (1, normal operation), disable (0)
 *  2- 0: Reserved, set to "0"
 */
#define IWM_FH_TCSR_LOWER_BOUND  (IWM_FH_MEM_LOWER_BOUND + 0xD00)
#define IWM_FH_TCSR_UPPER_BOUND  (IWM_FH_MEM_LOWER_BOUND + 0xE60)

/* Find Control/Status reg for given Tx DMA/FIFO channel */
#define IWM_FH_TCSR_CHNL_NUM                            (8)

/* TCSR: tx_config register values */
#define IWM_FH_TCSR_CHNL_TX_CONFIG_REG(_chnl)	\
		(IWM_FH_TCSR_LOWER_BOUND + 0x20 * (_chnl))
#define IWM_FH_TCSR_CHNL_TX_CREDIT_REG(_chnl)	\
		(IWM_FH_TCSR_LOWER_BOUND + 0x20 * (_chnl) + 0x4)
#define IWM_FH_TCSR_CHNL_TX_BUF_STS_REG(_chnl)	\
		(IWM_FH_TCSR_LOWER_BOUND + 0x20 * (_chnl) + 0x8)

#define IWM_FH_TCSR_TX_CONFIG_REG_VAL_MSG_MODE_TXF	(0x00000000)
#define IWM_FH_TCSR_TX_CONFIG_REG_VAL_MSG_MODE_DRV	(0x00000001)

#define IWM_FH_TCSR_TX_CONFIG_REG_VAL_DMA_CREDIT_DISABLE	(0x00000000)
#define IWM_FH_TCSR_TX_CONFIG_REG_VAL_DMA_CREDIT_ENABLE		(0x00000008)

#define IWM_FH_TCSR_TX_CONFIG_REG_VAL_CIRQ_HOST_NOINT	(0x00000000)
#define IWM_FH_TCSR_TX_CONFIG_REG_VAL_CIRQ_HOST_ENDTFD	(0x00100000)
#define IWM_FH_TCSR_TX_CONFIG_REG_VAL_CIRQ_HOST_IFTFD	(0x00200000)

#define IWM_FH_TCSR_TX_CONFIG_REG_VAL_CIRQ_RTC_NOINT	(0x00000000)
#define IWM_FH_TCSR_TX_CONFIG_REG_VAL_CIRQ_RTC_ENDTFD	(0x00400000)
#define IWM_FH_TCSR_TX_CONFIG_REG_VAL_CIRQ_RTC_IFTFD	(0x00800000)

#define IWM_FH_TCSR_TX_CONFIG_REG_VAL_DMA_CHNL_PAUSE		(0x00000000)
#define IWM_FH_TCSR_TX_CONFIG_REG_VAL_DMA_CHNL_PAUSE_EOF	(0x40000000)
#define IWM_FH_TCSR_TX_CONFIG_REG_VAL_DMA_CHNL_ENABLE		(0x80000000)

#define IWM_FH_TCSR_CHNL_TX_BUF_STS_REG_VAL_TFDB_EMPTY	(0x00000000)
#define IWM_FH_TCSR_CHNL_TX_BUF_STS_REG_VAL_TFDB_WAIT	(0x00002000)
#define IWM_FH_TCSR_CHNL_TX_BUF_STS_REG_VAL_TFDB_VALID	(0x00000003)

#define IWM_FH_TCSR_CHNL_TX_BUF_STS_REG_POS_TB_NUM		(20)
#define IWM_FH_TCSR_CHNL_TX_BUF_STS_REG_POS_TB_IDX		(12)

/**
 * Tx Shared Status Registers (TSSR)
 *
 * After stopping Tx DMA channel (writing 0 to
 * IWM_FH_TCSR_CHNL_TX_CONFIG_REG(chnl)), driver must poll
 * IWM_FH_TSSR_TX_STATUS_REG until selected Tx channel is idle
 * (channel's buffers empty | no pending requests).
 *
 * Bit fields:
 * 31-24:  1 = Channel buffers empty (channel 7:0)
 * 23-16:  1 = No pending requests (channel 7:0)
 */
#define IWM_FH_TSSR_LOWER_BOUND		(IWM_FH_MEM_LOWER_BOUND + 0xEA0)
#define IWM_FH_TSSR_UPPER_BOUND		(IWM_FH_MEM_LOWER_BOUND + 0xEC0)

#define IWM_FH_TSSR_TX_STATUS_REG	(IWM_FH_TSSR_LOWER_BOUND + 0x010)

/**
 * Bit fields for TSSR(Tx Shared Status & Control) error status register:
 * 31:  Indicates an address error when accessed to internal memory
 *	uCode/driver must write "1" in order to clear this flag
 * 30:  Indicates that Host did not send the expected number of dwords to FH
 *	uCode/driver must write "1" in order to clear this flag
 * 16-9:Each status bit is for one channel. Indicates that an (Error) ActDMA
 *	command was received from the scheduler while the TRB was already full
 *	with previous command
 *	uCode/driver must write "1" in order to clear this flag
 * 7-0: Each status bit indicates a channel's TxCredit error. When an error
 *	bit is set, it indicates that the FH has received a full indication
 *	from the RTC TxFIFO and the current value of the TxCredit counter was
 *	not equal to zero. This mean that the credit mechanism was not
 *	synchronized to the TxFIFO status
 *	uCode/driver must write "1" in order to clear this flag
 */
#define IWM_FH_TSSR_TX_ERROR_REG	(IWM_FH_TSSR_LOWER_BOUND + 0x018)
#define IWM_FH_TSSR_TX_MSG_CONFIG_REG	(IWM_FH_TSSR_LOWER_BOUND + 0x008)

#define IWM_FH_TSSR_TX_STATUS_REG_MSK_CHNL_IDLE(_chnl) ((1 << (_chnl)) << 16)

/* Tx service channels */
#define IWM_FH_SRVC_CHNL		(9)
#define IWM_FH_SRVC_LOWER_BOUND	(IWM_FH_MEM_LOWER_BOUND + 0x9C8)
#define IWM_FH_SRVC_UPPER_BOUND	(IWM_FH_MEM_LOWER_BOUND + 0x9D0)
#define IWM_FH_SRVC_CHNL_SRAM_ADDR_REG(_chnl) \
		(IWM_FH_SRVC_LOWER_BOUND + ((_chnl) - 9) * 0x4)

#define IWM_FH_TX_CHICKEN_BITS_REG	(IWM_FH_MEM_LOWER_BOUND + 0xE98)
#define IWM_FH_TX_TRB_REG(_chan)	(IWM_FH_MEM_LOWER_BOUND + 0x958 + \
					(_chan) * 4)

/* Instruct FH to increment the retry count of a packet when
 * it is brought from the memory to TX-FIFO
 */
#define IWM_FH_TX_CHICKEN_BITS_SCD_AUTO_RETRY_EN	(0x00000002)

#define IWM_RX_QUEUE_SIZE                         256
#define IWM_RX_QUEUE_MASK                         255
#define IWM_RX_QUEUE_SIZE_LOG                     8

/*
 * RX related structures and functions
 */
#define IWM_RX_FREE_BUFFERS 64
#define IWM_RX_LOW_WATERMARK 8

/**
 * struct iwm_rb_status - reserve buffer status
 * 	host memory mapped FH registers
 * @closed_rb_num [0:11] - Indicates the index of the RB which was closed
 * @closed_fr_num [0:11] - Indicates the index of the RX Frame which was closed
 * @finished_rb_num [0:11] - Indicates the index of the current RB
 * 	in which the last frame was written to
 * @finished_fr_num [0:11] - Indicates the index of the RX Frame
 * 	which was transferred
 */
struct iwm_rb_status {
	uint16_t closed_rb_num;
	uint16_t closed_fr_num;
	uint16_t finished_rb_num;
	uint16_t finished_fr_nam;
	uint32_t unused;
} __packed;


#define IWM_TFD_QUEUE_SIZE_MAX		(256)
#define IWM_TFD_QUEUE_SIZE_BC_DUP	(64)
#define IWM_TFD_QUEUE_BC_SIZE		(IWM_TFD_QUEUE_SIZE_MAX + \
					IWM_TFD_QUEUE_SIZE_BC_DUP)
#define IWM_TX_DMA_MASK        DMA_BIT_MASK(36)
#define IWM_NUM_OF_TBS		20

static inline uint8_t iwm_get_dma_hi_addr(bus_addr_t addr)
{
	return (sizeof(addr) > sizeof(uint32_t) ? (addr >> 16) >> 16 : 0) & 0xF;
}
/**
 * struct iwm_tfd_tb transmit buffer descriptor within transmit frame descriptor
 *
 * This structure contains dma address and length of transmission address
 *
 * @lo: low [31:0] portion of the dma address of TX buffer
 * 	every even is unaligned on 16 bit boundary
 * @hi_n_len 0-3 [35:32] portion of dma
 *	     4-15 length of the tx buffer
 */
struct iwm_tfd_tb {
	uint32_t lo;
	uint16_t hi_n_len;
} __packed;

/**
 * struct iwm_tfd
 *
 * Transmit Frame Descriptor (TFD)
 *
 * @ __reserved1[3] reserved
 * @ num_tbs 0-4 number of active tbs
 *	     5   reserved
 * 	     6-7 padding (not used)
 * @ tbs[20]	transmit frame buffer descriptors
 * @ __pad 	padding
 *
 * Each Tx queue uses a circular buffer of 256 TFDs stored in host DRAM.
 * Both driver and device share these circular buffers, each of which must be
 * contiguous 256 TFDs x 128 bytes-per-TFD = 32 KBytes
 *
 * Driver must indicate the physical address of the base of each
 * circular buffer via the IWM_FH_MEM_CBBC_QUEUE registers.
 *
 * Each TFD contains pointer/size information for up to 20 data buffers
 * in host DRAM.  These buffers collectively contain the (one) frame described
 * by the TFD.  Each buffer must be a single contiguous block of memory within
 * itself, but buffers may be scattered in host DRAM.  Each buffer has max size
 * of (4K - 4).  The concatenates all of a TFD's buffers into a single
 * Tx frame, up to 8 KBytes in size.
 *
 * A maximum of 255 (not 256!) TFDs may be on a queue waiting for Tx.
 */
struct iwm_tfd {
	uint8_t __reserved1[3];
	uint8_t num_tbs;
	struct iwm_tfd_tb tbs[IWM_NUM_OF_TBS];
	uint32_t __pad;
} __packed;

/* Keep Warm Size */
#define IWM_KW_SIZE 0x1000	/* 4k */

/* Fixed (non-configurable) rx data from phy */

/**
 * struct iwm_agn_schedq_bc_tbl scheduler byte count table
 *	base physical address provided by IWM_SCD_DRAM_BASE_ADDR
 * @tfd_offset  0-12 - tx command byte count
 *	       12-16 - station index
 */
struct iwm_agn_scd_bc_tbl {
	uint16_t tfd_offset[IWM_TFD_QUEUE_BC_SIZE];
} __packed;

#define IWM_TX_CRC_SIZE 4
#define IWM_TX_DELIMITER_SIZE 4

/* Maximum number of Tx queues. */
#define IWM_MAX_QUEUES	31

/**
 * DQA - Dynamic Queue Allocation -introduction
 *
 * Dynamic Queue Allocation (AKA "DQA") is a feature implemented in iwlwifi
 * to allow dynamic allocation of queues on-demand, rather than allocate them
 * statically ahead of time. Ideally, we would like to allocate one queue
 * per RA/TID, thus allowing an AP - for example - to send BE traffic to STA2
 * even if it also needs to send traffic to a sleeping STA1, without being
 * blocked by the sleeping station.
 *
 * Although the queues in DQA mode are dynamically allocated, there are still
 * some queues that are statically allocated:
 *	TXQ #0 - command queue
 *	TXQ #1 - aux frames
 *	TXQ #2 - P2P device frames
 *	TXQ #3 - P2P GO/SoftAP GCAST/BCAST frames
 *	TXQ #4 - BSS DATA frames queue
 *	TXQ #5-8 - non-QoS data, QoS no-data, and MGMT frames queue pool
 *	TXQ #9 - P2P GO/SoftAP probe responses
 *	TXQ #10-31 - QoS DATA frames queue pool (for Tx aggregation)
 */

/* static DQA Tx queue numbers */
#define IWM_DQA_CMD_QUEUE		0
#define IWM_DQA_AUX_QUEUE		1
#define IWM_DQA_P2P_DEVICE_QUEUE	2
#define IWM_DQA_INJECT_MONITOR_QUEUE	2
#define IWM_DQA_GCAST_QUEUE		3
#define IWM_DQA_BSS_CLIENT_QUEUE	4
#define IWM_DQA_MIN_MGMT_QUEUE		5
#define IWM_DQA_MAX_MGMT_QUEUE		8
#define IWM_DQA_AP_PROBE_RESP_QUEUE	9
#define IWM_DQA_MIN_DATA_QUEUE		10
#define IWM_DQA_MAX_DATA_QUEUE		31

/* Reserve 8 DQA Tx queues, from 10 up to 17, for A-MPDU aggregation. */
#define IWM_MAX_TID_COUNT	8
#define IWM_FIRST_AGG_TX_QUEUE	IWM_DQA_MIN_DATA_QUEUE
#define IWM_LAST_AGG_TX_QUEUE	(IWM_FIRST_AGG_TX_QUEUE + IWM_MAX_TID_COUNT - 1)

/* legacy non-DQA queues; the legacy command queue uses a different number! */
#define IWM_OFFCHANNEL_QUEUE	8
#define IWM_CMD_QUEUE		9
#define IWM_AUX_QUEUE		15

#define IWM_TX_FIFO_BK	0
#define IWM_TX_FIFO_BE	1
#define IWM_TX_FIFO_VI	2
#define IWM_TX_FIFO_VO	3
#define IWM_TX_FIFO_MCAST	5
#define IWM_TX_FIFO_CMD	7

#define IWM_STATION_COUNT	16

/*
 * Commands
 */
#define IWM_ALIVE		0x1
#define IWM_REPLY_ERROR		0x2
#define IWM_INIT_COMPLETE_NOTIF	0x4

/* PHY context commands */
#define IWM_PHY_CONTEXT_CMD	0x8
#define IWM_DBG_CFG		0x9

/* UMAC scan commands */
#define IWM_SCAN_ITERATION_COMPLETE_UMAC	0xb5
#define IWM_SCAN_CFG_CMD			0xc
#define IWM_SCAN_REQ_UMAC			0xd
#define IWM_SCAN_ABORT_UMAC			0xe
#define IWM_SCAN_COMPLETE_UMAC			0xf

/* station table */
#define IWM_ADD_STA_KEY	0x17
#define IWM_ADD_STA	0x18
#define IWM_REMOVE_STA	0x19

/* TX */
#define IWM_TX_CMD		0x1c
#define IWM_TXPATH_FLUSH	0x1e
#define IWM_MGMT_MCAST_KEY	0x1f

/* scheduler config */
#define IWM_SCD_QUEUE_CFG	0x1d

/* global key */
#define IWM_WEP_KEY	0x20

/* MAC and Binding commands */
#define IWM_MAC_CONTEXT_CMD		0x28
#define IWM_TIME_EVENT_CMD		0x29 /* both CMD and response */
#define IWM_TIME_EVENT_NOTIFICATION	0x2a
#define IWM_BINDING_CONTEXT_CMD		0x2b
#define IWM_TIME_QUOTA_CMD		0x2c
#define IWM_NON_QOS_TX_COUNTER_CMD	0x2d

#define IWM_LQ_CMD	0x4e

/* Calibration */
#define IWM_TEMPERATURE_NOTIFICATION		0x62
#define IWM_CALIBRATION_CFG_CMD			0x65
#define IWM_CALIBRATION_RES_NOTIFICATION	0x66
#define IWM_CALIBRATION_COMPLETE_NOTIFICATION	0x67
#define IWM_RADIO_VERSION_NOTIFICATION		0x68

/* paging block to FW cpu2 */
#define IWM_FW_PAGING_BLOCK_CMD	0x4f

/* Scan offload */
#define IWM_SCAN_OFFLOAD_REQUEST_CMD		0x51
#define IWM_SCAN_OFFLOAD_ABORT_CMD		0x52
#define IWM_HOT_SPOT_CMD			0x53
#define IWM_SCAN_OFFLOAD_COMPLETE		0x6d
#define IWM_SCAN_OFFLOAD_UPDATE_PROFILES_CMD	0x6e
#define IWM_SCAN_OFFLOAD_CONFIG_CMD		0x6f
#define IWM_MATCH_FOUND_NOTIFICATION		0xd9
#define IWM_SCAN_ITERATION_COMPLETE		0xe7

/* Phy */
#define IWM_PHY_CONFIGURATION_CMD		0x6a
#define IWM_CALIB_RES_NOTIF_PHY_DB		0x6b
#define IWM_PHY_DB_CMD				0x6c

/* Power - legacy power table command */
#define IWM_POWER_TABLE_CMD				0x77
#define IWM_PSM_UAPSD_AP_MISBEHAVING_NOTIFICATION	0x78
#define IWM_LTR_CONFIG					0xee

/* Thermal Throttling*/
#define IWM_REPLY_THERMAL_MNG_BACKOFF	0x7e

/* NVM */
#define IWM_NVM_ACCESS_CMD	0x88

#define IWM_SET_CALIB_DEFAULT_CMD	0x8e

#define IWM_BEACON_NOTIFICATION		0x90
#define IWM_BEACON_TEMPLATE_CMD		0x91
#define IWM_TX_ANT_CONFIGURATION_CMD	0x98
#define IWM_BT_CONFIG			0x9b
#define IWM_STATISTICS_NOTIFICATION	0x9d
#define IWM_REDUCE_TX_POWER_CMD		0x9f

/* RF-KILL commands and notifications */
#define IWM_CARD_STATE_CMD		0xa0
#define IWM_CARD_STATE_NOTIFICATION	0xa1

#define IWM_MISSED_BEACONS_NOTIFICATION	0xa2

#define IWM_MFUART_LOAD_NOTIFICATION	0xb1

/* Power - new power table command */
#define IWM_MAC_PM_POWER_TABLE	0xa9

#define IWM_REPLY_RX_PHY_CMD	0xc0
#define IWM_REPLY_RX_MPDU_CMD	0xc1
#define IWM_BA_NOTIF		0xc5

/* Location Aware Regulatory */
#define IWM_MCC_UPDATE_CMD	0xc8
#define IWM_MCC_CHUB_UPDATE_CMD	0xc9

/* BT Coex */
#define IWM_BT_COEX_PRIO_TABLE	0xcc
#define IWM_BT_COEX_PROT_ENV	0xcd
#define IWM_BT_PROFILE_NOTIFICATION	0xce
#define IWM_BT_COEX_CI	0x5d

#define IWM_REPLY_SF_CFG_CMD		0xd1
#define IWM_REPLY_BEACON_FILTERING_CMD	0xd2

/* DTS measurements */
#define IWM_CMD_DTS_MEASUREMENT_TRIGGER		0xdc
#define IWM_DTS_MEASUREMENT_NOTIFICATION	0xdd

#define IWM_REPLY_DEBUG_CMD	0xf0
#define IWM_DEBUG_LOG_MSG	0xf7

#define IWM_MCAST_FILTER_CMD	0xd0

/* D3 commands/notifications */
#define IWM_D3_CONFIG_CMD		0xd3
#define IWM_PROT_OFFLOAD_CONFIG_CMD	0xd4
#define IWM_OFFLOADS_QUERY_CMD		0xd5
#define IWM_REMOTE_WAKE_CONFIG_CMD	0xd6

/* for WoWLAN in particular */
#define IWM_WOWLAN_PATTERNS		0xe0
#define IWM_WOWLAN_CONFIGURATION	0xe1
#define IWM_WOWLAN_TSC_RSC_PARAM	0xe2
#define IWM_WOWLAN_TKIP_PARAM		0xe3
#define IWM_WOWLAN_KEK_KCK_MATERIAL	0xe4
#define IWM_WOWLAN_GET_STATUSES		0xe5
#define IWM_WOWLAN_TX_POWER_PER_DB	0xe6

/* and for NetDetect */
#define IWM_NET_DETECT_CONFIG_CMD		0x54
#define IWM_NET_DETECT_PROFILES_QUERY_CMD	0x56
#define IWM_NET_DETECT_PROFILES_CMD		0x57
#define IWM_NET_DETECT_HOTSPOTS_CMD		0x58
#define IWM_NET_DETECT_HOTSPOTS_QUERY_CMD	0x59

/* system group command IDs */
#define IWM_FSEQ_VER_MISMATCH_NOTIFICATION	0xff

#define IWM_REPLY_MAX	0xff

/* PHY_OPS subcommand IDs */
#define IWM_CMD_DTS_MEASUREMENT_TRIGGER_WIDE	0x0
#define IWM_CTDP_CONFIG_CMD			0x03
#define IWM_TEMP_REPORTING_THRESHOLDS_CMD	0x04
#define IWM_CT_KILL_NOTIFICATION		0xFE
#define IWM_DTS_MEASUREMENT_NOTIF_WIDE		0xFF

/* command groups */
#define IWM_LEGACY_GROUP	0x0
#define IWM_LONG_GROUP		0x1
#define IWM_SYSTEM_GROUP	0x2
#define IWM_MAC_CONF_GROUP	0x3
#define IWM_PHY_OPS_GROUP	0x4
#define IWM_DATA_PATH_GROUP	0x5
#define IWM_PROT_OFFLOAD_GROUP	0xb

/* SYSTEM_GROUP group subcommand IDs */

#define IWM_SHARED_MEM_CFG_CMD		0x00
#define IWM_SOC_CONFIGURATION_CMD	0x01
#define IWM_INIT_EXTENDED_CFG_CMD	0x03
#define IWM_FW_ERROR_RECOVERY_CMD	0x07

/* DATA_PATH group subcommand IDs */
#define IWM_DQA_ENABLE_CMD	0x00

/*
 * struct iwm_dqa_enable_cmd
 * @cmd_queue: the TXQ number of the command queue
 */
struct iwm_dqa_enable_cmd {
	uint32_t cmd_queue;
} __packed; /* DQA_CONTROL_CMD_API_S_VER_1 */

/**
 * struct iwm_cmd_response - generic response struct for most commands
 * @status: status of the command asked, changes for each one
 */
struct iwm_cmd_response {
	uint32_t status;
};

/*
 * struct iwm_tx_ant_cfg_cmd
 * @valid: valid antenna configuration
 */
struct iwm_tx_ant_cfg_cmd {
	uint32_t valid;
} __packed;

/**
 * struct iwm_reduce_tx_power_cmd - TX power reduction command
 * IWM_REDUCE_TX_POWER_CMD = 0x9f
 * @flags: (reserved for future implementation)
 * @mac_context_id: id of the mac ctx for which we are reducing TX power.
 * @pwr_restriction: TX power restriction in dBms.
 */
struct iwm_reduce_tx_power_cmd {
	uint8_t flags;
	uint8_t mac_context_id;
	uint16_t pwr_restriction;
} __packed; /* IWM_TX_REDUCED_POWER_API_S_VER_1 */

/*
 * Calibration control struct.
 * Sent as part of the phy configuration command.
 * @flow_trigger: bitmap for which calibrations to perform according to
 *		flow triggers.
 * @event_trigger: bitmap for which calibrations to perform according to
 *		event triggers.
 */
struct iwm_calib_ctrl {
	uint32_t flow_trigger;
	uint32_t event_trigger;
} __packed;

/* This defines the bitmap of various calibrations to enable in both
 * init ucode and runtime ucode through IWM_CALIBRATION_CFG_CMD.
 */
#define IWM_CALIB_CFG_XTAL_IDX			(1 << 0)
#define IWM_CALIB_CFG_TEMPERATURE_IDX		(1 << 1)
#define IWM_CALIB_CFG_VOLTAGE_READ_IDX		(1 << 2)
#define IWM_CALIB_CFG_PAPD_IDX			(1 << 3)
#define IWM_CALIB_CFG_TX_PWR_IDX		(1 << 4)
#define IWM_CALIB_CFG_DC_IDX			(1 << 5)
#define IWM_CALIB_CFG_BB_FILTER_IDX		(1 << 6)
#define IWM_CALIB_CFG_LO_LEAKAGE_IDX		(1 << 7)
#define IWM_CALIB_CFG_TX_IQ_IDX			(1 << 8)
#define IWM_CALIB_CFG_TX_IQ_SKEW_IDX		(1 << 9)
#define IWM_CALIB_CFG_RX_IQ_IDX			(1 << 10)
#define IWM_CALIB_CFG_RX_IQ_SKEW_IDX		(1 << 11)
#define IWM_CALIB_CFG_SENSITIVITY_IDX		(1 << 12)
#define IWM_CALIB_CFG_CHAIN_NOISE_IDX		(1 << 13)
#define IWM_CALIB_CFG_DISCONNECTED_ANT_IDX	(1 << 14)
#define IWM_CALIB_CFG_ANT_COUPLING_IDX		(1 << 15)
#define IWM_CALIB_CFG_DAC_IDX			(1 << 16)
#define IWM_CALIB_CFG_ABS_IDX			(1 << 17)
#define IWM_CALIB_CFG_AGC_IDX			(1 << 18)

/*
 * Phy configuration command.
 */
struct iwm_phy_cfg_cmd {
	uint32_t	phy_cfg;
	struct iwm_calib_ctrl calib_control;
} __packed;

#define IWM_PHY_CFG_RADIO_TYPE	((1 << 0) | (1 << 1))
#define IWM_PHY_CFG_RADIO_STEP	((1 << 2) | (1 << 3))
#define IWM_PHY_CFG_RADIO_DASH	((1 << 4) | (1 << 5))
#define IWM_PHY_CFG_PRODUCT_NUMBER	((1 << 6) | (1 << 7))
#define IWM_PHY_CFG_TX_CHAIN_A	(1 << 8)
#define IWM_PHY_CFG_TX_CHAIN_B	(1 << 9)
#define IWM_PHY_CFG_TX_CHAIN_C	(1 << 10)
#define IWM_PHY_CFG_RX_CHAIN_A	(1 << 12)
#define IWM_PHY_CFG_RX_CHAIN_B	(1 << 13)
#define IWM_PHY_CFG_RX_CHAIN_C	(1 << 14)

#define IWM_MAX_DTS_TRIPS	8

/**
 * struct iwm_ct_kill_notif - CT-kill entry notification
 *
 * @temperature: the current temperature in celsius
 * @reserved: reserved
 */
struct iwm_ct_kill_notif {
	uint16_t temperature;
	uint16_t reserved;
} __packed; /* GRP_PHY_CT_KILL_NTF */

/**
 * struct iwm_temp_report_ths_cmd - set temperature thresholds
 * (IWM_TEMP_REPORTING_THRESHOLDS_CMD)
 *
 * @num_temps: number of temperature thresholds passed
 * @thresholds: array with the thresholds to be configured
 */
struct iwm_temp_report_ths_cmd {
	uint32_t num_temps;
	uint16_t thresholds[IWM_MAX_DTS_TRIPS];
} __packed; /* GRP_PHY_TEMP_REPORTING_THRESHOLDS_CMD */

/*
 * PHY db
 */

#define IWM_PHY_DB_CFG 			1
#define IWM_PHY_DB_CALIB_NCH		2
#define IWM_PHY_DB_UNUSED		3
#define IWM_PHY_DB_CALIB_CHG_PAPD	4
#define IWM_PHY_DB_CALIB_CHG_TXP	5
#define IWM_PHY_DB_MAX			6

/*
 * phy db - configure operational ucode
 */
struct iwm_phy_db_cmd {
	uint16_t type;
	uint16_t length;
	uint8_t data[];
} __packed;

/* for parsing of tx power channel group data that comes from the firmware*/
struct iwm_phy_db_chg_txp {
	uint32_t space;
	uint16_t max_channel_idx;
} __packed;

/*
 * phy db - Receive phy db chunk after calibrations
 */
struct iwm_calib_res_notif_phy_db {
	uint16_t type;
	uint16_t length;
	uint8_t data[];
} __packed;

/* 7k family NVM HW-Section offset (in words) definitions */
#define IWM_HW_ADDR	0x15
/* 7k family NVM SW-Section offset (in words) definitions */
#define IWM_NVM_SW_SECTION	0x1C0
#define IWM_NVM_VERSION		0
#define IWM_RADIO_CFG		1
#define IWM_SKU			2
#define IWM_N_HW_ADDRS		3
#define IWM_NVM_CHANNELS	0x1E0 - IWM_NVM_SW_SECTION
/* 7k family NVM calibration section offset (in words) definitions */
#define IWM_NVM_CALIB_SECTION	0x2B8
#define IWM_XTAL_CALIB		(0x316 - IWM_NVM_CALIB_SECTION)

/* 8k family NVM HW-Section offset (in words) definitions */
#define IWM_HW_ADDR0_WFPM_8000		0x12
#define IWM_HW_ADDR1_WFPM_8000		0x16
#define IWM_HW_ADDR0_PCIE_8000		0x8A
#define IWM_HW_ADDR1_PCIE_8000		0x8E
#define IWM_MAC_ADDRESS_OVERRIDE_8000	1

/* 8k family NVM SW-Section offset (in words) definitions */
#define IWM_NVM_SW_SECTION_8000	0x1C0
#define IWM_NVM_VERSION_8000	0
#define IWM_RADIO_CFG_8000	0
#define IWM_SKU_8000		2
#define IWM_N_HW_ADDRS_8000	3

/* 8k family NVM REGULATORY -Section offset (in words) definitions */
#define IWM_NVM_CHANNELS_8000		0
#define IWM_NVM_LAR_OFFSET_8000_OLD	0x4C7
#define IWM_NVM_LAR_OFFSET_8000		0x507
#define IWM_NVM_LAR_ENABLED_8000	0x7

/* 8k family NVM calibration section offset (in words) definitions */
#define IWM_NVM_CALIB_SECTION_8000	0x2B8
#define IWM_XTAL_CALIB_8000		(0x316 - IWM_NVM_CALIB_SECTION_8000)

/* SKU Capabilities (actual values from NVM definition) */
#define IWM_NVM_SKU_CAP_BAND_24GHZ	(1 << 0)
#define IWM_NVM_SKU_CAP_BAND_52GHZ	(1 << 1)
#define IWM_NVM_SKU_CAP_11N_ENABLE	(1 << 2)
#define IWM_NVM_SKU_CAP_11AC_ENABLE	(1 << 3)
#define IWM_NVM_SKU_CAP_MIMO_DISABLE	(1 << 5)

/* radio config bits (actual values from NVM definition) */
#define IWM_NVM_RF_CFG_DASH_MSK(x)   (x & 0x3)         /* bits 0-1   */
#define IWM_NVM_RF_CFG_STEP_MSK(x)   ((x >> 2)  & 0x3) /* bits 2-3   */
#define IWM_NVM_RF_CFG_TYPE_MSK(x)   ((x >> 4)  & 0x3) /* bits 4-5   */
#define IWM_NVM_RF_CFG_PNUM_MSK(x)   ((x >> 6)  & 0x3) /* bits 6-7   */
#define IWM_NVM_RF_CFG_TX_ANT_MSK(x) ((x >> 8)  & 0xF) /* bits 8-11  */
#define IWM_NVM_RF_CFG_RX_ANT_MSK(x) ((x >> 12) & 0xF) /* bits 12-15 */

#define IWM_NVM_RF_CFG_PNUM_MSK_8000(x)		(x & 0xF)
#define IWM_NVM_RF_CFG_DASH_MSK_8000(x)		((x >> 4) & 0xF)
#define IWM_NVM_RF_CFG_STEP_MSK_8000(x)		((x >> 8) & 0xF)
#define IWM_NVM_RF_CFG_TYPE_MSK_8000(x)		((x >> 12) & 0xFFF)
#define IWM_NVM_RF_CFG_TX_ANT_MSK_8000(x)	((x >> 24) & 0xF)
#define IWM_NVM_RF_CFG_RX_ANT_MSK_8000(x)	((x >> 28) & 0xF)

/*
 * channel flags in NVM
 * @IWM_NVM_CHANNEL_VALID: channel is usable for this SKU/geo
 * @IWM_NVM_CHANNEL_IBSS: usable as an IBSS channel
 * @IWM_NVM_CHANNEL_ACTIVE: active scanning allowed
 * @IWM_NVM_CHANNEL_RADAR: radar detection required
 * @IWM_NVM_CHANNEL_DFS: dynamic freq selection candidate
 * @IWM_NVM_CHANNEL_WIDE: 20 MHz channel okay (?)
 * @IWM_NVM_CHANNEL_40MHZ: 40 MHz channel okay (?)
 * @IWM_NVM_CHANNEL_80MHZ: 80 MHz channel okay (?)
 * @IWM_NVM_CHANNEL_160MHZ: 160 MHz channel okay (?)
 */
#define IWM_NVM_CHANNEL_VALID	(1 << 0)
#define IWM_NVM_CHANNEL_IBSS	(1 << 1)
#define IWM_NVM_CHANNEL_ACTIVE	(1 << 3)
#define IWM_NVM_CHANNEL_RADAR	(1 << 4)
#define IWM_NVM_CHANNEL_DFS	(1 << 7)
#define IWM_NVM_CHANNEL_WIDE	(1 << 8)
#define IWM_NVM_CHANNEL_40MHZ	(1 << 9)
#define IWM_NVM_CHANNEL_80MHZ	(1 << 10)
#define IWM_NVM_CHANNEL_160MHZ	(1 << 11)

/* Target of the IWM_NVM_ACCESS_CMD */
#define IWM_NVM_ACCESS_TARGET_CACHE	0
#define IWM_NVM_ACCESS_TARGET_OTP	1
#define IWM_NVM_ACCESS_TARGET_EEPROM	2

/* Section types for IWM_NVM_ACCESS_CMD */
#define IWM_NVM_SECTION_TYPE_HW			0
#define IWM_NVM_SECTION_TYPE_SW			1
#define IWM_NVM_SECTION_TYPE_PAPD		2
#define IWM_NVM_SECTION_TYPE_REGULATORY		3
#define IWM_NVM_SECTION_TYPE_CALIBRATION	4
#define IWM_NVM_SECTION_TYPE_PRODUCTION		5
#define IWM_NVM_SECTION_TYPE_POST_FCS_CALIB	6
/* 7 unknown */
#define IWM_NVM_SECTION_TYPE_REGULATORY_SDP	8
/* 9 unknown */
#define IWM_NVM_SECTION_TYPE_HW_8000		10
#define IWM_NVM_SECTION_TYPE_MAC_OVERRIDE	11
#define IWM_NVM_SECTION_TYPE_PHY_SKU		12
#define IWM_NVM_NUM_OF_SECTIONS			13

/**
 * enum iwm_nvm_type - nvm formats
 * @IWM_NVM: the regular format
 * @IWM_NVM_EXT: extended NVM format
 * @IWM_NVM_SDP: NVM format used by 3168 series
 */
enum iwm_nvm_type {
	IWM_NVM,
	IWM_NVM_EXT,
	IWM_NVM_SDP,
};

/**
 * struct iwm_nvm_access_cmd_ver2 - Request the device to send an NVM section
 * @op_code: 0 - read, 1 - write
 * @target: IWM_NVM_ACCESS_TARGET_*
 * @type: IWM_NVM_SECTION_TYPE_*
 * @offset: offset in bytes into the section
 * @length: in bytes, to read/write
 * @data: if write operation, the data to write. On read its empty
 */
struct iwm_nvm_access_cmd {
	uint8_t op_code;
	uint8_t target;
	uint16_t type;
	uint16_t offset;
	uint16_t length;
	uint8_t data[];
} __packed; /* IWM_NVM_ACCESS_CMD_API_S_VER_2 */

/*
 * Block paging calculations
 */
#define IWM_PAGE_2_EXP_SIZE 12 /* 4K == 2^12 */
#define IWM_FW_PAGING_SIZE (1 << IWM_PAGE_2_EXP_SIZE) /* page size is 4KB */
#define IWM_PAGE_PER_GROUP_2_EXP_SIZE 3
/* 8 pages per group */
#define IWM_NUM_OF_PAGE_PER_GROUP (1 << IWM_PAGE_PER_GROUP_2_EXP_SIZE)
/* don't change, support only 32KB size */
#define IWM_PAGING_BLOCK_SIZE (IWM_NUM_OF_PAGE_PER_GROUP * IWM_FW_PAGING_SIZE)
/* 32K == 2^15 */
#define IWM_BLOCK_2_EXP_SIZE (IWM_PAGE_2_EXP_SIZE + IWM_PAGE_PER_GROUP_2_EXP_SIZE)

/*
 * Image paging calculations
 */
#define IWM_BLOCK_PER_IMAGE_2_EXP_SIZE 5
/* 2^5 == 32 blocks per image */
#define IWM_NUM_OF_BLOCK_PER_IMAGE (1 << IWM_BLOCK_PER_IMAGE_2_EXP_SIZE)
/* maximum image size 1024KB */
#define IWM_MAX_PAGING_IMAGE_SIZE (IWM_NUM_OF_BLOCK_PER_IMAGE * IWM_PAGING_BLOCK_SIZE)

/* Virtual address signature */
#define IWM_PAGING_ADDR_SIG 0xAA000000

#define IWM_PAGING_CMD_IS_SECURED (1 << 9)
#define IWM_PAGING_CMD_IS_ENABLED (1 << 8)
#define IWM_PAGING_CMD_NUM_OF_PAGES_IN_LAST_GRP_POS 0
#define IWM_PAGING_TLV_SECURE_MASK 1

#define IWM_NUM_OF_FW_PAGING_BLOCKS	33 /* 32 for data and 1 block for CSS */

/*
 * struct iwm_fw_paging_cmd - paging layout
 *
 * (IWM_FW_PAGING_BLOCK_CMD = 0x4f)
 *
 * Send to FW the paging layout in the driver.
 *
 * @flags: various flags for the command
 * @block_size: the block size in powers of 2
 * @block_num: number of blocks specified in the command.
 * @device_phy_addr: virtual addresses from device side
*/
struct iwm_fw_paging_cmd {
	uint32_t flags;
	uint32_t block_size;
	uint32_t block_num;
	uint32_t device_phy_addr[IWM_NUM_OF_FW_PAGING_BLOCKS];
} __packed; /* IWM_FW_PAGING_BLOCK_CMD_API_S_VER_1 */

/**
 * struct iwm_nvm_access_resp_ver2 - response to IWM_NVM_ACCESS_CMD
 * @offset: offset in bytes into the section
 * @length: in bytes, either how much was written or read
 * @type: IWM_NVM_SECTION_TYPE_*
 * @status: 0 for success, fail otherwise
 * @data: if read operation, the data returned. Empty on write.
 */
struct iwm_nvm_access_resp {
	uint16_t offset;
	uint16_t length;
	uint16_t type;
	uint16_t status;
	uint8_t data[];
} __packed; /* IWM_NVM_ACCESS_CMD_RESP_API_S_VER_2 */

/* IWM_ALIVE 0x1 */

/* alive response is_valid values */
#define IWM_ALIVE_RESP_UCODE_OK	(1 << 0)
#define IWM_ALIVE_RESP_RFKILL	(1 << 1)

/* alive response ver_type values */
#define IWM_FW_TYPE_HW		0
#define IWM_FW_TYPE_PROT	1
#define IWM_FW_TYPE_AP		2
#define IWM_FW_TYPE_WOWLAN	3
#define IWM_FW_TYPE_TIMING	4
#define IWM_FW_TYPE_WIPAN	5

/* alive response ver_subtype values */
#define IWM_FW_SUBTYPE_FULL_FEATURE	0
#define IWM_FW_SUBTYPE_BOOTSRAP		1 /* Not valid */
#define IWM_FW_SUBTYPE_REDUCED		2
#define IWM_FW_SUBTYPE_ALIVE_ONLY	3
#define IWM_FW_SUBTYPE_WOWLAN		4
#define IWM_FW_SUBTYPE_AP_SUBTYPE	5
#define IWM_FW_SUBTYPE_WIPAN		6
#define IWM_FW_SUBTYPE_INITIALIZE	9

#define IWM_ALIVE_STATUS_ERR 0xDEAD
#define IWM_ALIVE_STATUS_OK 0xCAFE

#define IWM_ALIVE_FLG_RFKILL	(1 << 0)

struct iwm_alive_resp_v1 {
	uint16_t status;
	uint16_t flags;
	uint8_t ucode_minor;
	uint8_t ucode_major;
	uint16_t id;
	uint8_t api_minor;
	uint8_t api_major;
	uint8_t ver_subtype;
	uint8_t ver_type;
	uint8_t mac;
	uint8_t opt;
	uint16_t reserved2;
	uint32_t timestamp;
	uint32_t error_event_table_ptr;	/* SRAM address for error log */
	uint32_t log_event_table_ptr;	/* SRAM address for event log */
	uint32_t cpu_register_ptr;
	uint32_t dbgm_config_ptr;
	uint32_t alive_counter_ptr;
	uint32_t scd_base_ptr;		/* SRAM address for SCD */
} __packed; /* IWM_ALIVE_RES_API_S_VER_1 */

struct iwm_alive_resp_v2 {
	uint16_t status;
	uint16_t flags;
	uint8_t ucode_minor;
	uint8_t ucode_major;
	uint16_t id;
	uint8_t api_minor;
	uint8_t api_major;
	uint8_t ver_subtype;
	uint8_t ver_type;
	uint8_t mac;
	uint8_t opt;
	uint16_t reserved2;
	uint32_t timestamp;
	uint32_t error_event_table_ptr;	/* SRAM address for error log */
	uint32_t log_event_table_ptr;	/* SRAM address for LMAC event log */
	uint32_t cpu_register_ptr;
	uint32_t dbgm_config_ptr;
	uint32_t alive_counter_ptr;
	uint32_t scd_base_ptr;		/* SRAM address for SCD */
	uint32_t st_fwrd_addr;		/* pointer to Store and forward */
	uint32_t st_fwrd_size;
	uint8_t umac_minor;			/* UMAC version: minor */
	uint8_t umac_major;			/* UMAC version: major */
	uint16_t umac_id;			/* UMAC version: id */
	uint32_t error_info_addr;		/* SRAM address for UMAC error log */
	uint32_t dbg_print_buff_addr;
} __packed; /* ALIVE_RES_API_S_VER_2 */

struct iwm_alive_resp_v3 {
	uint16_t status;
	uint16_t flags;
	uint32_t ucode_minor;
	uint32_t ucode_major;
	uint8_t ver_subtype;
	uint8_t ver_type;
	uint8_t mac;
	uint8_t opt;
	uint32_t timestamp;
	uint32_t error_event_table_ptr;	/* SRAM address for error log */
	uint32_t log_event_table_ptr;	/* SRAM address for LMAC event log */
	uint32_t cpu_register_ptr;
	uint32_t dbgm_config_ptr;
	uint32_t alive_counter_ptr;
	uint32_t scd_base_ptr;		/* SRAM address for SCD */
	uint32_t st_fwrd_addr;		/* pointer to Store and forward */
	uint32_t st_fwrd_size;
	uint32_t umac_minor;		/* UMAC version: minor */
	uint32_t umac_major;		/* UMAC version: major */
	uint32_t error_info_addr;		/* SRAM address for UMAC error log */
	uint32_t dbg_print_buff_addr;
} __packed; /* ALIVE_RES_API_S_VER_3 */

#define IWM_SOC_CONFIG_CMD_FLAGS_DISCRETE	(1 << 0)
#define IWM_SOC_CONFIG_CMD_FLAGS_LOW_LATENCY	(1 << 1)

#define IWM_SOC_FLAGS_LTR_APPLY_DELAY_MASK		0xc
#define IWM_SOC_FLAGS_LTR_APPLY_DELAY_NONE		0
#define IWM_SOC_FLAGS_LTR_APPLY_DELAY_200		1
#define IWM_SOC_FLAGS_LTR_APPLY_DELAY_2500		2
#define IWM_SOC_FLAGS_LTR_APPLY_DELAY_1820		3

/**
 * struct iwm_soc_configuration_cmd - Set device stabilization latency
 *
 * @flags: soc settings flags.  In VER_1, we can only set the DISCRETE
 *	flag, because the FW treats the whole value as an integer. In
 *	VER_2, we can set the bits independently.
 * @latency: time for SOC to ensure stable power & XTAL
 */
struct iwm_soc_configuration_cmd {
	uint32_t flags;
	uint32_t latency;
} __packed; /*
	     * SOC_CONFIGURATION_CMD_S_VER_1 (see description above)
	     * SOC_CONFIGURATION_CMD_S_VER_2
	     */


/* Error response/notification */
#define IWM_FW_ERR_UNKNOWN_CMD		0x0
#define IWM_FW_ERR_INVALID_CMD_PARAM	0x1
#define IWM_FW_ERR_SERVICE		0x2
#define IWM_FW_ERR_ARC_MEMORY		0x3
#define IWM_FW_ERR_ARC_CODE		0x4
#define IWM_FW_ERR_WATCH_DOG		0x5
#define IWM_FW_ERR_WEP_GRP_KEY_INDX	0x10
#define IWM_FW_ERR_WEP_KEY_SIZE		0x11
#define IWM_FW_ERR_OBSOLETE_FUNC	0x12
#define IWM_FW_ERR_UNEXPECTED		0xFE
#define IWM_FW_ERR_FATAL		0xFF

/**
 * struct iwm_error_resp - FW error indication
 * ( IWM_REPLY_ERROR = 0x2 )
 * @error_type: one of IWM_FW_ERR_*
 * @cmd_id: the command ID for which the error occurred
 * @bad_cmd_seq_num: sequence number of the erroneous command
 * @error_service: which service created the error, applicable only if
 *	error_type = 2, otherwise 0
 * @timestamp: TSF in usecs.
 */
struct iwm_error_resp {
	uint32_t error_type;
	uint8_t cmd_id;
	uint8_t reserved1;
	uint16_t bad_cmd_seq_num;
	uint32_t error_service;
	uint64_t timestamp;
} __packed;

#define IWM_FW_CMD_VER_UNKNOWN 99

/**
 * struct iwm_fw_cmd_version - firmware command version entry
 * @cmd: command ID
 * @group: group ID
 * @cmd_ver: command version
 * @notif_ver: notification version
 */
struct iwm_fw_cmd_version {
	uint8_t cmd;
	uint8_t group;
	uint8_t cmd_ver;
	uint8_t notif_ver;
} __packed;


/* Common PHY, MAC and Bindings definitions */

#define IWM_MAX_MACS_IN_BINDING	(3)
#define IWM_MAX_BINDINGS	(4)
#define IWM_AUX_BINDING_INDEX	(3)
#define IWM_MAX_PHYS		(4)

/* Used to extract ID and color from the context dword */
#define IWM_FW_CTXT_ID_POS	(0)
#define IWM_FW_CTXT_ID_MSK	(0xff << IWM_FW_CTXT_ID_POS)
#define IWM_FW_CTXT_COLOR_POS	(8)
#define IWM_FW_CTXT_COLOR_MSK	(0xff << IWM_FW_CTXT_COLOR_POS)
#define IWM_FW_CTXT_INVALID	(0xffffffff)

#define IWM_FW_CMD_ID_AND_COLOR(_id, _color) ((_id << IWM_FW_CTXT_ID_POS) |\
					  (_color << IWM_FW_CTXT_COLOR_POS))

/* Possible actions on PHYs, MACs and Bindings */
#define IWM_FW_CTXT_ACTION_STUB		0
#define IWM_FW_CTXT_ACTION_ADD		1
#define IWM_FW_CTXT_ACTION_MODIFY	2
#define IWM_FW_CTXT_ACTION_REMOVE	3
#define IWM_FW_CTXT_ACTION_NUM		4
/* COMMON_CONTEXT_ACTION_API_E_VER_1 */

/* Time Events */

/* Time Event types, according to MAC type */

/* BSS Station Events */
#define IWM_TE_BSS_STA_AGGRESSIVE_ASSOC	0
#define IWM_TE_BSS_STA_ASSOC		1
#define IWM_TE_BSS_EAP_DHCP_PROT	2
#define IWM_TE_BSS_QUIET_PERIOD		3

/* P2P Device Events */
#define IWM_TE_P2P_DEVICE_DISCOVERABLE	4
#define IWM_TE_P2P_DEVICE_LISTEN	5
#define IWM_TE_P2P_DEVICE_ACTION_SCAN	6
#define IWM_TE_P2P_DEVICE_FULL_SCAN	7

/* P2P Client Events */
#define IWM_TE_P2P_CLIENT_AGGRESSIVE_ASSOC	8
#define IWM_TE_P2P_CLIENT_ASSOC			9
#define IWM_TE_P2P_CLIENT_QUIET_PERIOD		10

/* P2P GO Events */
#define IWM_TE_P2P_GO_ASSOC_PROT	11
#define IWM_TE_P2P_GO_REPETITIVE_NOA	12
#define IWM_TE_P2P_GO_CT_WINDOW		13

/* WiDi Sync Events */
#define IWM_TE_WIDI_TX_SYNC	14

#define IWM_TE_MAX	15
/* IWM_MAC_EVENT_TYPE_API_E_VER_1 */



/* Time event - defines for command API v1 */

/*
 * @IWM_TE_V1_FRAG_NONE: fragmentation of the time event is NOT allowed.
 * @IWM_TE_V1_FRAG_SINGLE: fragmentation of the time event is allowed, but only
 *	the first fragment is scheduled.
 * @IWM_TE_V1_FRAG_DUAL: fragmentation of the time event is allowed, but only
 *	the first 2 fragments are scheduled.
 * @IWM_TE_V1_FRAG_ENDLESS: fragmentation of the time event is allowed, and any
 *	number of fragments are valid.
 *
 * Other than the constant defined above, specifying a fragmentation value 'x'
 * means that the event can be fragmented but only the first 'x' will be
 * scheduled.
 */
#define IWM_TE_V1_FRAG_NONE	0
#define IWM_TE_V1_FRAG_SINGLE	1
#define IWM_TE_V1_FRAG_DUAL	2
#define IWM_TE_V1_FRAG_ENDLESS	0xffffffff

/* If a Time Event can be fragmented, this is the max number of fragments */
#define IWM_TE_V1_FRAG_MAX_MSK		0x0fffffff
/* Repeat the time event endlessly (until removed) */
#define IWM_TE_V1_REPEAT_ENDLESS	0xffffffff
/* If a Time Event has bounded repetitions, this is the maximal value */
#define IWM_TE_V1_REPEAT_MAX_MSK_V1	0x0fffffff

/* Time Event dependencies: none, on another TE, or in a specific time */
#define IWM_TE_V1_INDEPENDENT		0
#define IWM_TE_V1_DEP_OTHER		(1 << 0)
#define IWM_TE_V1_DEP_TSF		(1 << 1)
#define IWM_TE_V1_EVENT_SOCIOPATHIC	(1 << 2)
/* IWM_MAC_EVENT_DEPENDENCY_POLICY_API_E_VER_2 */

/*
 * @IWM_TE_V1_NOTIF_NONE: no notifications
 * @IWM_TE_V1_NOTIF_HOST_EVENT_START: request/receive notification on event start
 * @IWM_TE_V1_NOTIF_HOST_EVENT_END:request/receive notification on event end
 * @IWM_TE_V1_NOTIF_INTERNAL_EVENT_START: internal FW use
 * @IWM_TE_V1_NOTIF_INTERNAL_EVENT_END: internal FW use.
 * @IWM_TE_V1_NOTIF_HOST_FRAG_START: request/receive notification on frag start
 * @IWM_TE_V1_NOTIF_HOST_FRAG_END:request/receive notification on frag end
 * @IWM_TE_V1_NOTIF_INTERNAL_FRAG_START: internal FW use.
 * @IWM_TE_V1_NOTIF_INTERNAL_FRAG_END: internal FW use.
 *
 * Supported Time event notifications configuration.
 * A notification (both event and fragment) includes a status indicating weather
 * the FW was able to schedule the event or not. For fragment start/end
 * notification the status is always success. There is no start/end fragment
 * notification for monolithic events.
 */
#define IWM_TE_V1_NOTIF_NONE			0
#define IWM_TE_V1_NOTIF_HOST_EVENT_START	(1 << 0)
#define IWM_TE_V1_NOTIF_HOST_EVENT_END		(1 << 1)
#define IWM_TE_V1_NOTIF_INTERNAL_EVENT_START	(1 << 2)
#define IWM_TE_V1_NOTIF_INTERNAL_EVENT_END	(1 << 3)
#define IWM_TE_V1_NOTIF_HOST_FRAG_START		(1 << 4)
#define IWM_TE_V1_NOTIF_HOST_FRAG_END		(1 << 5)
#define IWM_TE_V1_NOTIF_INTERNAL_FRAG_START	(1 << 6)
#define IWM_TE_V1_NOTIF_INTERNAL_FRAG_END	(1 << 7)
/* IWM_MAC_EVENT_ACTION_API_E_VER_2 */


/* Time event - defines for command API */

/**
 * DOC: Time Events - what is it?
 *
 * Time Events are a fw feature that allows the driver to control the presence
 * of the device on the channel. Since the fw supports multiple channels
 * concurrently, the fw may choose to jump to another channel at any time.
 * In order to make sure that the fw is on a specific channel at a certain time
 * and for a certain duration, the driver needs to issue a time event.
 *
 * The simplest example is for BSS association. The driver issues a time event,
 * waits for it to start, and only then tells mac80211 that we can start the
 * association. This way, we make sure that the association will be done
 * smoothly and won't be interrupted by channel switch decided within the fw.
 */

 /**
 * DOC: The flow against the fw
 *
 * When the driver needs to make sure we are in a certain channel, at a certain
 * time and for a certain duration, it sends a Time Event. The flow against the
 * fw goes like this:
 *	1) Driver sends a TIME_EVENT_CMD to the fw
 *	2) Driver gets the response for that command. This response contains the
 *	   Unique ID (UID) of the event.
 *	3) The fw sends notification when the event starts.
 *
 * Of course the API provides various options that allow to cover parameters
 * of the flow.
 *	What is the duration of the event?
 *	What is the start time of the event?
 *	Is there an end-time for the event?
 *	How much can the event be delayed?
 *	Can the event be split?
 *	If yes what is the maximal number of chunks?
 *	etc...
 */

/*
 * @IWM_TE_V2_FRAG_NONE: fragmentation of the time event is NOT allowed.
 * @IWM_TE_V2_FRAG_SINGLE: fragmentation of the time event is allowed, but only
 *  the first fragment is scheduled.
 * @IWM_TE_V2_FRAG_DUAL: fragmentation of the time event is allowed, but only
 *  the first 2 fragments are scheduled.
 * @IWM_TE_V2_FRAG_ENDLESS: fragmentation of the time event is allowed, and any
 *  number of fragments are valid.
 *
 * Other than the constant defined above, specifying a fragmentation value 'x'
 * means that the event can be fragmented but only the first 'x' will be
 * scheduled.
 */
#define IWM_TE_V2_FRAG_NONE		0
#define IWM_TE_V2_FRAG_SINGLE		1
#define IWM_TE_V2_FRAG_DUAL		2
#define IWM_TE_V2_FRAG_MAX		0xfe
#define IWM_TE_V2_FRAG_ENDLESS		0xff

/* Repeat the time event endlessly (until removed) */
#define IWM_TE_V2_REPEAT_ENDLESS	0xff
/* If a Time Event has bounded repetitions, this is the maximal value */
#define IWM_TE_V2_REPEAT_MAX	0xfe

#define IWM_TE_V2_PLACEMENT_POS	12
#define IWM_TE_V2_ABSENCE_POS	15

/* Time event policy values
 * A notification (both event and fragment) includes a status indicating weather
 * the FW was able to schedule the event or not. For fragment start/end
 * notification the status is always success. There is no start/end fragment
 * notification for monolithic events.
 *
 * @IWM_TE_V2_DEFAULT_POLICY: independent, social, present, unoticable
 * @IWM_TE_V2_NOTIF_HOST_EVENT_START: request/receive notification on event start
 * @IWM_TE_V2_NOTIF_HOST_EVENT_END:request/receive notification on event end
 * @IWM_TE_V2_NOTIF_INTERNAL_EVENT_START: internal FW use
 * @IWM_TE_V2_NOTIF_INTERNAL_EVENT_END: internal FW use.
 * @IWM_TE_V2_NOTIF_HOST_FRAG_START: request/receive notification on frag start
 * @IWM_TE_V2_NOTIF_HOST_FRAG_END:request/receive notification on frag end
 * @IWM_TE_V2_NOTIF_INTERNAL_FRAG_START: internal FW use.
 * @IWM_TE_V2_NOTIF_INTERNAL_FRAG_END: internal FW use.
 * @IWM_TE_V2_DEP_OTHER: depends on another time event
 * @IWM_TE_V2_DEP_TSF: depends on a specific time
 * @IWM_TE_V2_EVENT_SOCIOPATHIC: can't co-exist with other events of the same MAC
 * @IWM_TE_V2_ABSENCE: are we present or absent during the Time Event.
 */
#define IWM_TE_V2_DEFAULT_POLICY		0x0

/* notifications (event start/stop, fragment start/stop) */
#define IWM_TE_V2_NOTIF_HOST_EVENT_START	(1 << 0)
#define IWM_TE_V2_NOTIF_HOST_EVENT_END		(1 << 1)
#define IWM_TE_V2_NOTIF_INTERNAL_EVENT_START	(1 << 2)
#define IWM_TE_V2_NOTIF_INTERNAL_EVENT_END	(1 << 3)

#define IWM_TE_V2_NOTIF_HOST_FRAG_START		(1 << 4)
#define IWM_TE_V2_NOTIF_HOST_FRAG_END		(1 << 5)
#define IWM_TE_V2_NOTIF_INTERNAL_FRAG_START	(1 << 6)
#define IWM_TE_V2_NOTIF_INTERNAL_FRAG_END	(1 << 7)
#define IWM_T2_V2_START_IMMEDIATELY		(1 << 11)

#define IWM_TE_V2_NOTIF_MSK	0xff

/* placement characteristics */
#define IWM_TE_V2_DEP_OTHER		(1 << IWM_TE_V2_PLACEMENT_POS)
#define IWM_TE_V2_DEP_TSF		(1 << (IWM_TE_V2_PLACEMENT_POS + 1))
#define IWM_TE_V2_EVENT_SOCIOPATHIC	(1 << (IWM_TE_V2_PLACEMENT_POS + 2))

/* are we present or absent during the Time Event. */
#define IWM_TE_V2_ABSENCE		(1 << IWM_TE_V2_ABSENCE_POS)

/**
 * struct iwm_time_event_cmd_api - configuring Time Events
 * with struct IWM_MAC_TIME_EVENT_DATA_API_S_VER_2 (see also
 * with version 1. determined by IWM_UCODE_TLV_FLAGS)
 * ( IWM_TIME_EVENT_CMD = 0x29 )
 * @id_and_color: ID and color of the relevant MAC
 * @action: action to perform, one of IWM_FW_CTXT_ACTION_*
 * @id: this field has two meanings, depending on the action:
 *	If the action is ADD, then it means the type of event to add.
 *	For all other actions it is the unique event ID assigned when the
 *	event was added by the FW.
 * @apply_time: When to start the Time Event (in GP2)
 * @max_delay: maximum delay to event's start (apply time), in TU
 * @depends_on: the unique ID of the event we depend on (if any)
 * @interval: interval between repetitions, in TU
 * @duration: duration of event in TU
 * @repeat: how many repetitions to do, can be IWM_TE_REPEAT_ENDLESS
 * @max_frags: maximal number of fragments the Time Event can be divided to
 * @policy: defines whether uCode shall notify the host or other uCode modules
 *	on event and/or fragment start and/or end
 *	using one of IWM_TE_INDEPENDENT, IWM_TE_DEP_OTHER, IWM_TE_DEP_TSF
 *	IWM_TE_EVENT_SOCIOPATHIC
 *	using IWM_TE_ABSENCE and using IWM_TE_NOTIF_*
 */
struct iwm_time_event_cmd {
	/* COMMON_INDEX_HDR_API_S_VER_1 */
	uint32_t id_and_color;
	uint32_t action;
	uint32_t id;
	/* IWM_MAC_TIME_EVENT_DATA_API_S_VER_2 */
	uint32_t apply_time;
	uint32_t max_delay;
	uint32_t depends_on;
	uint32_t interval;
	uint32_t duration;
	uint8_t repeat;
	uint8_t max_frags;
	uint16_t policy;
} __packed; /* IWM_MAC_TIME_EVENT_CMD_API_S_VER_2 */

/**
 * struct iwm_time_event_resp - response structure to iwm_time_event_cmd
 * @status: bit 0 indicates success, all others specify errors
 * @id: the Time Event type
 * @unique_id: the unique ID assigned (in ADD) or given (others) to the TE
 * @id_and_color: ID and color of the relevant MAC
 */
struct iwm_time_event_resp {
	uint32_t status;
	uint32_t id;
	uint32_t unique_id;
	uint32_t id_and_color;
} __packed; /* IWM_MAC_TIME_EVENT_RSP_API_S_VER_1 */

/**
 * struct iwm_time_event_notif - notifications of time event start/stop
 * ( IWM_TIME_EVENT_NOTIFICATION = 0x2a )
 * @timestamp: action timestamp in GP2
 * @session_id: session's unique id
 * @unique_id: unique id of the Time Event itself
 * @id_and_color: ID and color of the relevant MAC
 * @action: one of IWM_TE_NOTIF_START or IWM_TE_NOTIF_END
 * @status: true if scheduled, false otherwise (not executed)
 */
struct iwm_time_event_notif {
	uint32_t timestamp;
	uint32_t session_id;
	uint32_t unique_id;
	uint32_t id_and_color;
	uint32_t action;
	uint32_t status;
} __packed; /* IWM_MAC_TIME_EVENT_NTFY_API_S_VER_1 */


/* Bindings and Time Quota */

/**
 * struct iwm_binding_cmd_v1 - configuring bindings
 * ( IWM_BINDING_CONTEXT_CMD = 0x2b )
 * @id_and_color: ID and color of the relevant Binding
 * @action: action to perform, one of IWM_FW_CTXT_ACTION_*
 * @macs: array of MAC id and colors which belong to the binding
 * @phy: PHY id and color which belongs to the binding
 * @lmac_id: the lmac id the binding belongs to
 */
struct iwm_binding_cmd_v1 {
	/* COMMON_INDEX_HDR_API_S_VER_1 */
	uint32_t id_and_color;
	uint32_t action;
	/* IWM_BINDING_DATA_API_S_VER_1 */
	uint32_t macs[IWM_MAX_MACS_IN_BINDING];
	uint32_t phy;
} __packed; /* IWM_BINDING_CMD_API_S_VER_1 */

/**
 * struct iwm_binding_cmd - configuring bindings
 * ( IWM_BINDING_CONTEXT_CMD = 0x2b )
 * @id_and_color: ID and color of the relevant Binding
 * @action: action to perform, one of IWM_FW_CTXT_ACTION_*
 * @macs: array of MAC id and colors which belong to the binding
 * @phy: PHY id and color which belongs to the binding
 * @lmac_id: the lmac id the binding belongs to
 */
struct iwm_binding_cmd {
	/* COMMON_INDEX_HDR_API_S_VER_1 */
	uint32_t id_and_color;
	uint32_t action;
	/* IWM_BINDING_DATA_API_S_VER_1 */
	uint32_t macs[IWM_MAX_MACS_IN_BINDING];
	uint32_t phy;
	uint32_t lmac_id;
} __packed; /* IWM_BINDING_CMD_API_S_VER_2 */

#define IWM_LMAC_24G_INDEX		0
#define IWM_LMAC_5G_INDEX		1

/* The maximal number of fragments in the FW's schedule session */
#define IWM_MAX_QUOTA 128

/**
 * struct iwm_time_quota_data - configuration of time quota per binding
 * @id_and_color: ID and color of the relevant Binding
 * @quota: absolute time quota in TU. The scheduler will try to divide the
 *	remaining quota (after Time Events) according to this quota.
 * @max_duration: max uninterrupted context duration in TU
 */
struct iwm_time_quota_data_v1 {
	uint32_t id_and_color;
	uint32_t quota;
	uint32_t max_duration;
} __packed; /* IWM_TIME_QUOTA_DATA_API_S_VER_1 */

/**
 * struct iwm_time_quota_cmd - configuration of time quota between bindings
 * ( IWM_TIME_QUOTA_CMD = 0x2c )
 * @quotas: allocations per binding
 */
struct iwm_time_quota_cmd_v1 {
	struct iwm_time_quota_data_v1 quotas[IWM_MAX_BINDINGS];
} __packed; /* IWM_TIME_QUOTA_ALLOCATION_CMD_API_S_VER_1 */

#define IWM_QUOTA_LOW_LATENCY_NONE	0
#define IWM_QUOTA_LOW_LATENCY_TX	(1 << 0)
#define IWM_QUOTA_LOW_LATENCY_RX	(1 << 1)

/**
 * struct iwm_time_quota_data - configuration of time quota per binding
 * @id_and_color: ID and color of the relevant Binding.
 * @quota: absolute time quota in TU. The scheduler will try to divide the
 *	remaining quota (after Time Events) according to this quota.
 * @max_duration: max uninterrupted context duration in TU
 * @low_latency: low latency status IWM_QUOTA_LOW_LATENCY_*
 */
struct iwm_time_quota_data {
	uint32_t id_and_color;
	uint32_t quota;
	uint32_t max_duration;
	uint32_t low_latency;
}; /* TIME_QUOTA_DATA_API_S_VER_2 */

/**
 * struct iwm_time_quota_cmd - configuration of time quota between bindings
 * ( TIME_QUOTA_CMD = 0x2c )
 * Note: on non-CDB the fourth one is the auxiliary mac and is essentially zero.
 * On CDB the fourth one is a regular binding.
 *
 * @quotas: allocations per binding
 */
struct iwm_time_quota_cmd {
	struct iwm_time_quota_data quotas[IWM_MAX_BINDINGS];
}; /* TIME_QUOTA_ALLOCATION_CMD_API_S_VER_2 */


/* PHY context */

/* Supported bands */
#define IWM_PHY_BAND_5  (0)
#define IWM_PHY_BAND_24 (1)

/* Supported channel width, vary if there is VHT support */
#define IWM_PHY_VHT_CHANNEL_MODE20	(0x0)
#define IWM_PHY_VHT_CHANNEL_MODE40	(0x1)
#define IWM_PHY_VHT_CHANNEL_MODE80	(0x2)
#define IWM_PHY_VHT_CHANNEL_MODE160	(0x3)

/*
 * Control channel position:
 * For legacy set bit means upper channel, otherwise lower.
 * For VHT - bit-2 marks if the control is lower/upper relative to center-freq
 *   bits-1:0 mark the distance from the center freq. for 20Mhz, offset is 0.
 *                                   center_freq
 *                                        |
 * 40Mhz                          |_______|_______|
 * 80Mhz                  |_______|_______|_______|_______|
 * 160Mhz |_______|_______|_______|_______|_______|_______|_______|_______|
 * code      011     010     001     000  |  100     101     110    111
 */
#define IWM_PHY_VHT_CTRL_POS_1_BELOW  (0x0)
#define IWM_PHY_VHT_CTRL_POS_2_BELOW  (0x1)
#define IWM_PHY_VHT_CTRL_POS_3_BELOW  (0x2)
#define IWM_PHY_VHT_CTRL_POS_4_BELOW  (0x3)
#define IWM_PHY_VHT_CTRL_POS_1_ABOVE  (0x4)
#define IWM_PHY_VHT_CTRL_POS_2_ABOVE  (0x5)
#define IWM_PHY_VHT_CTRL_POS_3_ABOVE  (0x6)
#define IWM_PHY_VHT_CTRL_POS_4_ABOVE  (0x7)

/*
 * @band: IWM_PHY_BAND_*
 * @channel: channel number
 * @width: PHY_[VHT|LEGACY]_CHANNEL_*
 * @ctrl channel: PHY_[VHT|LEGACY]_CTRL_*
 */
struct iwm_fw_channel_info_v1 {
	uint8_t band;
	uint8_t channel;
	uint8_t width;
	uint8_t ctrl_pos;
} __packed; /* CHANNEL_CONFIG_API_S_VER_1 */

/*
 * struct iwm_fw_channel_info - channel information
 *
 * @channel: channel number
 * @band: PHY_BAND_*
 * @width: PHY_[VHT|LEGACY]_CHANNEL_*
 * @ctrl channel: PHY_[VHT|LEGACY]_CTRL_*
 * @reserved: for future use and alignment
 */
struct iwm_fw_channel_info {
	uint32_t channel;
	uint8_t band;
	uint8_t width;
	uint8_t ctrl_pos;
	uint8_t reserved;
} __packed; /* CHANNEL_CONFIG_API_S_VER_2 */

#define IWM_PHY_RX_CHAIN_DRIVER_FORCE_POS	(0)
#define IWM_PHY_RX_CHAIN_DRIVER_FORCE_MSK \
	(0x1 << IWM_PHY_RX_CHAIN_DRIVER_FORCE_POS)
#define IWM_PHY_RX_CHAIN_VALID_POS		(1)
#define IWM_PHY_RX_CHAIN_VALID_MSK \
	(0x7 << IWM_PHY_RX_CHAIN_VALID_POS)
#define IWM_PHY_RX_CHAIN_FORCE_SEL_POS	(4)
#define IWM_PHY_RX_CHAIN_FORCE_SEL_MSK \
	(0x7 << IWM_PHY_RX_CHAIN_FORCE_SEL_POS)
#define IWM_PHY_RX_CHAIN_FORCE_MIMO_SEL_POS	(7)
#define IWM_PHY_RX_CHAIN_FORCE_MIMO_SEL_MSK \
	(0x7 << IWM_PHY_RX_CHAIN_FORCE_MIMO_SEL_POS)
#define IWM_PHY_RX_CHAIN_CNT_POS		(10)
#define IWM_PHY_RX_CHAIN_CNT_MSK \
	(0x3 << IWM_PHY_RX_CHAIN_CNT_POS)
#define IWM_PHY_RX_CHAIN_MIMO_CNT_POS	(12)
#define IWM_PHY_RX_CHAIN_MIMO_CNT_MSK \
	(0x3 << IWM_PHY_RX_CHAIN_MIMO_CNT_POS)
#define IWM_PHY_RX_CHAIN_MIMO_FORCE_POS	(14)
#define IWM_PHY_RX_CHAIN_MIMO_FORCE_MSK \
	(0x1 << IWM_PHY_RX_CHAIN_MIMO_FORCE_POS)

/* TODO: fix the value, make it depend on firmware at runtime? */
#define IWM_NUM_PHY_CTX	3

/* TODO: complete missing documentation */
/**
 * struct iwm_phy_context_cmd - config of the PHY context
 * ( IWM_PHY_CONTEXT_CMD = 0x8 )
 * @id_and_color: ID and color of the relevant Binding
 * @action: action to perform, one of IWM_FW_CTXT_ACTION_*
 * @apply_time: 0 means immediate apply and context switch.
 *	other value means apply new params after X usecs
 * @tx_param_color: ???
 * @channel_info:
 * @txchain_info: ???
 * @rxchain_info: ???
 * @acquisition_data: ???
 * @dsp_cfg_flags: set to 0
 */
/*
 * XXX Intel forgot to bump the PHY_CONTEXT command API when they increased
 * the size of fw_channel_info from v1 to v2.
 * To keep things simple we define two versions of this struct, and both
 * are labeled as CMD_API_VER_1. (The Linux iwlwifi driver performs dark
 * magic with pointers to struct members instead.)
 */
/* This version must be used if IWM_UCODE_TLV_CAPA_ULTRA_HB_CHANNELS is set: */
struct iwm_phy_context_cmd_uhb {
	/* COMMON_INDEX_HDR_API_S_VER_1 */
	uint32_t id_and_color;
	uint32_t action;
	/* IWM_PHY_CONTEXT_DATA_API_S_VER_1 */
	uint32_t apply_time;
	uint32_t tx_param_color;
	struct iwm_fw_channel_info ci;
	uint32_t txchain_info;
	uint32_t rxchain_info;
	uint32_t acquisition_data;
	uint32_t dsp_cfg_flags;
} __packed; /* IWM_PHY_CONTEXT_CMD_API_VER_1 */
/* This version must be used otherwise: */
struct iwm_phy_context_cmd {
	/* COMMON_INDEX_HDR_API_S_VER_1 */
	uint32_t id_and_color;
	uint32_t action;
	/* IWM_PHY_CONTEXT_DATA_API_S_VER_1 */
	uint32_t apply_time;
	uint32_t tx_param_color;
	struct iwm_fw_channel_info_v1 ci;
	uint32_t txchain_info;
	uint32_t rxchain_info;
	uint32_t acquisition_data;
	uint32_t dsp_cfg_flags;
} __packed; /* IWM_PHY_CONTEXT_CMD_API_VER_1 */

#define IWM_RX_INFO_PHY_CNT 8
#define IWM_RX_INFO_ENERGY_ANT_ABC_IDX 1
#define IWM_RX_INFO_ENERGY_ANT_A_MSK 0x000000ff
#define IWM_RX_INFO_ENERGY_ANT_B_MSK 0x0000ff00
#define IWM_RX_INFO_ENERGY_ANT_C_MSK 0x00ff0000
#define IWM_RX_INFO_ENERGY_ANT_A_POS 0
#define IWM_RX_INFO_ENERGY_ANT_B_POS 8
#define IWM_RX_INFO_ENERGY_ANT_C_POS 16

#define IWM_RX_INFO_AGC_IDX 1
#define IWM_RX_INFO_RSSI_AB_IDX 2
#define IWM_OFDM_AGC_A_MSK 0x0000007f
#define IWM_OFDM_AGC_A_POS 0
#define IWM_OFDM_AGC_B_MSK 0x00003f80
#define IWM_OFDM_AGC_B_POS 7
#define IWM_OFDM_AGC_CODE_MSK 0x3fe00000
#define IWM_OFDM_AGC_CODE_POS 20
#define IWM_OFDM_RSSI_INBAND_A_MSK 0x00ff
#define IWM_OFDM_RSSI_A_POS 0
#define IWM_OFDM_RSSI_ALLBAND_A_MSK 0xff00
#define IWM_OFDM_RSSI_ALLBAND_A_POS 8
#define IWM_OFDM_RSSI_INBAND_B_MSK 0xff0000
#define IWM_OFDM_RSSI_B_POS 16
#define IWM_OFDM_RSSI_ALLBAND_B_MSK 0xff000000
#define IWM_OFDM_RSSI_ALLBAND_B_POS 24

/**
 * struct iwm_rx_phy_info - phy info
 * (IWM_REPLY_RX_PHY_CMD = 0xc0)
 * @non_cfg_phy_cnt: non configurable DSP phy data byte count
 * @cfg_phy_cnt: configurable DSP phy data byte count
 * @stat_id: configurable DSP phy data set ID
 * @reserved1:
 * @system_timestamp: GP2  at on air rise
 * @timestamp: TSF at on air rise
 * @beacon_time_stamp: beacon at on-air rise
 * @phy_flags: general phy flags: band, modulation, ...
 * @channel: channel number
 * @non_cfg_phy_buf: for various implementations of non_cfg_phy
 * @rate_n_flags: IWM_RATE_MCS_*
 * @byte_count: frame's byte-count
 * @frame_time: frame's time on the air, based on byte count and frame rate
 *	calculation
 * @mac_active_msk: what MACs were active when the frame was received
 *
 * Before each Rx, the device sends this data. It contains PHY information
 * about the reception of the packet.
 */
struct iwm_rx_phy_info {
	uint8_t non_cfg_phy_cnt;
	uint8_t cfg_phy_cnt;
	uint8_t stat_id;
	uint8_t reserved1;
	uint32_t system_timestamp;
	uint64_t timestamp;
	uint32_t beacon_time_stamp;
	uint16_t phy_flags;
#define IWM_PHY_INFO_FLAG_SHPREAMBLE	(1 << 2)
	uint16_t channel;
	uint32_t non_cfg_phy[IWM_RX_INFO_PHY_CNT];
	uint32_t rate_n_flags;
	uint32_t byte_count;
	uint16_t mac_active_msk;
	uint16_t frame_time;
} __packed;

struct iwm_rx_mpdu_res_start {
	uint16_t byte_count;
	uint16_t reserved;
} __packed;

/**
 * Values to parse %iwm_rx_phy_info phy_flags
 * @IWM_RX_RES_PHY_FLAGS_BAND_24: true if the packet was received on 2.4 band
 * @IWM_RX_RES_PHY_FLAGS_MOD_CCK:
 * @IWM_RX_RES_PHY_FLAGS_SHORT_PREAMBLE: true if packet's preamble was short
 * @IWM_RX_RES_PHY_FLAGS_NARROW_BAND:
 * @IWM_RX_RES_PHY_FLAGS_ANTENNA: antenna on which the packet was received
 * @IWM_RX_RES_PHY_FLAGS_AGG: set if the packet was part of an A-MPDU
 * @IWM_RX_RES_PHY_FLAGS_OFDM_HT: The frame was an HT frame
 * @IWM_RX_RES_PHY_FLAGS_OFDM_GF: The frame used GF preamble
 * @IWM_RX_RES_PHY_FLAGS_OFDM_VHT: The frame was a VHT frame
 */
#define IWM_RX_RES_PHY_FLAGS_BAND_24		(1 << 0)
#define IWM_RX_RES_PHY_FLAGS_MOD_CCK		(1 << 1)
#define IWM_RX_RES_PHY_FLAGS_SHORT_PREAMBLE	(1 << 2)
#define IWM_RX_RES_PHY_FLAGS_NARROW_BAND	(1 << 3)
#define IWM_RX_RES_PHY_FLAGS_ANTENNA		(0x7 << 4)
#define IWM_RX_RES_PHY_FLAGS_ANTENNA_POS	4
#define IWM_RX_RES_PHY_FLAGS_AGG		(1 << 7)
#define IWM_RX_RES_PHY_FLAGS_OFDM_HT		(1 << 8)
#define IWM_RX_RES_PHY_FLAGS_OFDM_GF		(1 << 9)
#define IWM_RX_RES_PHY_FLAGS_OFDM_VHT		(1 << 10)

/**
 * Values written by fw for each Rx packet
 * @IWM_RX_MPDU_RES_STATUS_CRC_OK: CRC is fine
 * @IWM_RX_MPDU_RES_STATUS_OVERRUN_OK: there was no RXE overflow
 * @IWM_RX_MPDU_RES_STATUS_SRC_STA_FOUND:
 * @IWM_RX_MPDU_RES_STATUS_KEY_VALID:
 * @IWM_RX_MPDU_RES_STATUS_KEY_PARAM_OK:
 * @IWM_RX_MPDU_RES_STATUS_ICV_OK: ICV is fine, if not, the packet is destroyed
 * @IWM_RX_MPDU_RES_STATUS_MIC_OK: used for CCM alg only. TKIP MIC is checked
 *	in the driver.
 * @IWM_RX_MPDU_RES_STATUS_TTAK_OK: TTAK is fine
 * @IWM_RX_MPDU_RES_STATUS_MNG_FRAME_REPLAY_ERR:  valid for alg = CCM_CMAC or
 *	alg = CCM only. Checks replay attack for 11w frames. Relevant only if
 *	%IWM_RX_MPDU_RES_STATUS_ROBUST_MNG_FRAME is set.
 * @IWM_RX_MPDU_RES_STATUS_SEC_NO_ENC: this frame is not encrypted
 * @IWM_RX_MPDU_RES_STATUS_SEC_WEP_ENC: this frame is encrypted using WEP
 * @IWM_RX_MPDU_RES_STATUS_SEC_CCM_ENC: this frame is encrypted using CCM
 * @IWM_RX_MPDU_RES_STATUS_SEC_TKIP_ENC: this frame is encrypted using TKIP
 * @IWM_RX_MPDU_RES_STATUS_SEC_CCM_CMAC_ENC: this frame is encrypted using CCM_CMAC
 * @IWM_RX_MPDU_RES_STATUS_SEC_ENC_ERR: this frame couldn't be decrypted
 * @IWM_RX_MPDU_RES_STATUS_SEC_ENC_MSK: bitmask of the encryption algorithm
 * @IWM_RX_MPDU_RES_STATUS_DEC_DONE: this frame has been successfully decrypted
 * @IWM_RX_MPDU_RES_STATUS_PROTECT_FRAME_BIT_CMP:
 * @IWM_RX_MPDU_RES_STATUS_EXT_IV_BIT_CMP:
 * @IWM_RX_MPDU_RES_STATUS_KEY_ID_CMP_BIT:
 * @IWM_RX_MPDU_RES_STATUS_ROBUST_MNG_FRAME: this frame is an 11w management frame
 * @IWM_RX_MPDU_RES_STATUS_HASH_INDEX_MSK:
 * @IWM_RX_MPDU_RES_STATUS_STA_ID_MSK:
 * @IWM_RX_MPDU_RES_STATUS_RRF_KILL:
 * @IWM_RX_MPDU_RES_STATUS_FILTERING_MSK:
 * @IWM_RX_MPDU_RES_STATUS2_FILTERING_MSK:
 */
#define IWM_RX_MPDU_RES_STATUS_CRC_OK			(1 << 0)
#define IWM_RX_MPDU_RES_STATUS_OVERRUN_OK		(1 << 1)
#define IWM_RX_MPDU_RES_STATUS_SRC_STA_FOUND		(1 << 2)
#define IWM_RX_MPDU_RES_STATUS_KEY_VALID		(1 << 3)
#define IWM_RX_MPDU_RES_STATUS_KEY_PARAM_OK		(1 << 4)
#define IWM_RX_MPDU_RES_STATUS_ICV_OK			(1 << 5)
#define IWM_RX_MPDU_RES_STATUS_MIC_OK			(1 << 6)
#define IWM_RX_MPDU_RES_STATUS_TTAK_OK			(1 << 7)
#define IWM_RX_MPDU_RES_STATUS_MNG_FRAME_REPLAY_ERR	(1 << 7)
#define IWM_RX_MPDU_RES_STATUS_SEC_NO_ENC		(0 << 8)
#define IWM_RX_MPDU_RES_STATUS_SEC_WEP_ENC		(1 << 8)
#define IWM_RX_MPDU_RES_STATUS_SEC_CCM_ENC		(2 << 8)
#define IWM_RX_MPDU_RES_STATUS_SEC_TKIP_ENC		(3 << 8)
#define IWM_RX_MPDU_RES_STATUS_SEC_EXT_ENC		(4 << 8)
#define IWM_RX_MPDU_RES_STATUS_SEC_CCM_CMAC_ENC		(6 << 8)
#define IWM_RX_MPDU_RES_STATUS_SEC_ENC_ERR		(7 << 8)
#define IWM_RX_MPDU_RES_STATUS_SEC_ENC_MSK		(7 << 8)
#define IWM_RX_MPDU_RES_STATUS_DEC_DONE			(1 << 11)
#define IWM_RX_MPDU_RES_STATUS_PROTECT_FRAME_BIT_CMP	(1 << 12)
#define IWM_RX_MPDU_RES_STATUS_EXT_IV_BIT_CMP		(1 << 13)
#define IWM_RX_MPDU_RES_STATUS_KEY_ID_CMP_BIT		(1 << 14)
#define IWM_RX_MPDU_RES_STATUS_ROBUST_MNG_FRAME		(1 << 15)
#define IWM_RX_MPDU_RES_STATUS_HASH_INDEX_MSK		(0x3F0000)
#define IWM_RX_MPDU_RES_STATUS_STA_ID_MSK		(0x1f000000)
#define IWM_RX_MPDU_RES_STATUS_RRF_KILL			(1 << 29)
#define IWM_RX_MPDU_RES_STATUS_FILTERING_MSK		(0xc00000)
#define IWM_RX_MPDU_RES_STATUS2_FILTERING_MSK		(0xc0000000)

#define IWM_RX_MPDU_MFLG1_ADDRTYPE_MASK		0x03
#define IWM_RX_MPDU_MFLG1_MIC_CRC_LEN_MASK	0xf0
#define IWM_RX_MPDU_MFLG1_MIC_CRC_LEN_SHIFT	3

#define IWM_RX_MPDU_MFLG2_HDR_LEN_MASK		0x1f
#define	IWM_RX_MPDU_MFLG2_PAD			0x20
#define IWM_RX_MPDU_MFLG2_AMSDU			0x40

#define IWM_RX_MPDU_AMSDU_SUBFRAME_IDX_MASK	0x7f
#define IWM_RX_MPDU_AMSDU_LAST_SUBFRAME		0x80

#define IWM_RX_MPDU_PHY_AMPDU			(1 << 5)
#define IWM_RX_MPDU_PHY_AMPDU_TOGGLE		(1 << 6)
#define IWM_RX_MPDU_PHY_SHORT_PREAMBLE		(1 << 7)
#define IWM_RX_MPDU_PHY_NCCK_ADDTL_NTFY		(1 << 7)
#define IWM_RX_MPDU_PHY_TSF_OVERLOAD		(1 << 8)

struct iwm_rx_mpdu_desc_v1 {
	union {
		uint32_t rss_hash;
		uint32_t phy_data2;
	};
	union {
		uint32_t filter_match;
		uint32_t phy_data3;
	};
	uint32_t rate_n_flags;
	uint8_t energy_a;
	uint8_t energy_b;
	uint8_t channel;
	uint8_t mac_context;
	uint32_t gp2_on_air_rise;
	union {
		uint64_t tsf_on_air_rise;
		struct {
			uint32_t phy_data0;
			uint32_t phy_data1;
		};
	} __packed;
} __packed;

#define IWM_RX_REORDER_DATA_INVALID_BAID	0x7f

#define IWM_RX_MPDU_REORDER_NSSN_MASK		0x00000fff
#define IWM_RX_MPDU_REORDER_SN_MASK		0x00fff000
#define IWM_RX_MPDU_REORDER_SN_SHIFT		12
#define IWM_RX_MPDU_REORDER_BAID_MASK		0x7f000000
#define IWM_RX_MPDU_REORDER_BAID_SHIFT		24
#define IWM_RX_MPDU_REORDER_BA_OLD_SN		0x80000000

struct iwm_rx_mpdu_desc {
	uint16_t mpdu_len;
	uint8_t mac_flags1;
	uint8_t mac_flags2;
	uint8_t amsdu_info;
	uint16_t phy_info;
	uint8_t mac_phy_idx;
	uint16_t raw_csum;
	union {
		uint16_t l3l4_flags;
		uint16_t phy_data4;
	};
	uint16_t status;
	uint8_t hash_filter;
	uint8_t sta_id_flags;
	uint32_t reorder_data;
	struct iwm_rx_mpdu_desc_v1 v1;
} __packed;

/**
 * struct iwm_radio_version_notif - information on the radio version
 * ( IWM_RADIO_VERSION_NOTIFICATION = 0x68 )
 * @radio_flavor:
 * @radio_step:
 * @radio_dash:
 */
struct iwm_radio_version_notif {
	uint32_t radio_flavor;
	uint32_t radio_step;
	uint32_t radio_dash;
} __packed; /* IWM_RADIO_VERSION_NOTOFICATION_S_VER_1 */

#define IWM_CARD_ENABLED		0x00
#define IWM_HW_CARD_DISABLED		0x01
#define IWM_SW_CARD_DISABLED		0x02
#define IWM_CT_KILL_CARD_DISABLED	0x04
#define IWM_HALT_CARD_DISABLED		0x08
#define IWM_CARD_DISABLED_MSK		0x0f
#define IWM_CARD_IS_RX_ON		0x10

/**
 * struct iwm_radio_version_notif - information on the radio version
 * (IWM_CARD_STATE_NOTIFICATION = 0xa1 )
 * @flags: %iwm_card_state_flags
 */
struct iwm_card_state_notif {
	uint32_t flags;
} __packed; /* CARD_STATE_NTFY_API_S_VER_1 */

/**
 * struct iwm_missed_beacons_notif - information on missed beacons
 * ( IWM_MISSED_BEACONS_NOTIFICATION = 0xa2 )
 * @mac_id: interface ID
 * @consec_missed_beacons_since_last_rx: number of consecutive missed
 *	beacons since last RX.
 * @consec_missed_beacons: number of consecutive missed beacons
 * @num_expected_beacons:
 * @num_recvd_beacons:
 */
struct iwm_missed_beacons_notif {
	uint32_t mac_id;
	uint32_t consec_missed_beacons_since_last_rx;
	uint32_t consec_missed_beacons;
	uint32_t num_expected_beacons;
	uint32_t num_recvd_beacons;
} __packed; /* IWM_MISSED_BEACON_NTFY_API_S_VER_3 */

/**
 * struct iwm_mfuart_load_notif - mfuart image version & status
 * ( IWM_MFUART_LOAD_NOTIFICATION = 0xb1 )
 * @installed_ver: installed image version
 * @external_ver: external image version
 * @status: MFUART loading status
 * @duration: MFUART loading time
*/
struct iwm_mfuart_load_notif {
	uint32_t installed_ver;
	uint32_t external_ver;
	uint32_t status;
	uint32_t duration;
} __packed; /*MFU_LOADER_NTFY_API_S_VER_1*/

/**
 * struct iwm_set_calib_default_cmd - set default value for calibration.
 * ( IWM_SET_CALIB_DEFAULT_CMD = 0x8e )
 * @calib_index: the calibration to set value for
 * @length: of data
 * @data: the value to set for the calibration result
 */
struct iwm_set_calib_default_cmd {
	uint16_t calib_index;
	uint16_t length;
	uint8_t data[0];
} __packed; /* IWM_PHY_CALIB_OVERRIDE_VALUES_S */

#define IWM_MAX_PORT_ID_NUM	2
#define IWM_MAX_MCAST_FILTERING_ADDRESSES 256

/**
 * struct iwm_mcast_filter_cmd - configure multicast filter.
 * @filter_own: Set 1 to filter out multicast packets sent by station itself
 * @port_id:	Multicast MAC addresses array specifier. This is a strange way
 *		to identify network interface adopted in host-device IF.
 *		It is used by FW as index in array of addresses. This array has
 *		IWM_MAX_PORT_ID_NUM members.
 * @count:	Number of MAC addresses in the array
 * @pass_all:	Set 1 to pass all multicast packets.
 * @bssid:	current association BSSID.
 * @addr_list:	Place holder for array of MAC addresses.
 *		IMPORTANT: add padding if necessary to ensure DWORD alignment.
 */
struct iwm_mcast_filter_cmd {
	uint8_t filter_own;
	uint8_t port_id;
	uint8_t count;
	uint8_t pass_all;
	uint8_t bssid[6];
	uint8_t reserved[2];
	uint8_t addr_list[0];
} __packed; /* IWM_MCAST_FILTERING_CMD_API_S_VER_1 */

struct iwm_statistics_dbg {
	uint32_t burst_check;
	uint32_t burst_count;
	uint32_t wait_for_silence_timeout_cnt;
	uint32_t reserved[3];
} __packed; /* IWM_STATISTICS_DEBUG_API_S_VER_2 */

struct iwm_statistics_div {
	uint32_t tx_on_a;
	uint32_t tx_on_b;
	uint32_t exec_time;
	uint32_t probe_time;
	uint32_t rssi_ant;
	uint32_t reserved2;
} __packed; /* IWM_STATISTICS_SLOW_DIV_API_S_VER_2 */

struct iwm_statistics_general_common {
	uint32_t temperature;   /* radio temperature */
	uint32_t temperature_m; /* radio voltage */
	struct iwm_statistics_dbg dbg;
	uint32_t sleep_time;
	uint32_t slots_out;
	uint32_t slots_idle;
	uint32_t ttl_timestamp;
	struct iwm_statistics_div div;
	uint32_t rx_enable_counter;
	/*
	 * num_of_sos_states:
	 *  count the number of times we have to re-tune
	 *  in order to get out of bad PHY status
	 */
	uint32_t num_of_sos_states;
} __packed; /* IWM_STATISTICS_GENERAL_API_S_VER_5 */

struct iwm_statistics_rx_non_phy {
	uint32_t bogus_cts;	/* CTS received when not expecting CTS */
	uint32_t bogus_ack;	/* ACK received when not expecting ACK */
	uint32_t non_bssid_frames;	/* number of frames with BSSID that
					 * doesn't belong to the STA BSSID */
	uint32_t filtered_frames;	/* count frames that were dumped in the
				 * filtering process */
	uint32_t non_channel_beacons;	/* beacons with our bss id but not on
					 * our serving channel */
	uint32_t channel_beacons;	/* beacons with our bss id and in our
				 * serving channel */
	uint32_t num_missed_bcon;	/* number of missed beacons */
	uint32_t adc_rx_saturation_time;	/* count in 0.8us units the time the
					 * ADC was in saturation */
	uint32_t ina_detection_search_time;/* total time (in 0.8us) searched
					  * for INA */
	uint32_t beacon_silence_rssi[3];/* RSSI silence after beacon frame */
	uint32_t interference_data_flag;	/* flag for interference data
					 * availability. 1 when data is
					 * available. */
	uint32_t channel_load;		/* counts RX Enable time in uSec */
	uint32_t dsp_false_alarms;	/* DSP false alarm (both OFDM
					 * and CCK) counter */
	uint32_t beacon_rssi_a;
	uint32_t beacon_rssi_b;
	uint32_t beacon_rssi_c;
	uint32_t beacon_energy_a;
	uint32_t beacon_energy_b;
	uint32_t beacon_energy_c;
	uint32_t num_bt_kills;
	uint32_t mac_id;
	uint32_t directed_data_mpdu;
} __packed; /* IWM_STATISTICS_RX_NON_PHY_API_S_VER_3 */

struct iwm_statistics_rx_phy {
	uint32_t ina_cnt;
	uint32_t fina_cnt;
	uint32_t plcp_err;
	uint32_t crc32_err;
	uint32_t overrun_err;
	uint32_t early_overrun_err;
	uint32_t crc32_good;
	uint32_t false_alarm_cnt;
	uint32_t fina_sync_err_cnt;
	uint32_t sfd_timeout;
	uint32_t fina_timeout;
	uint32_t unresponded_rts;
	uint32_t rxe_frame_limit_overrun;
	uint32_t sent_ack_cnt;
	uint32_t sent_cts_cnt;
	uint32_t sent_ba_rsp_cnt;
	uint32_t dsp_self_kill;
	uint32_t mh_format_err;
	uint32_t re_acq_main_rssi_sum;
	uint32_t reserved;
} __packed; /* IWM_STATISTICS_RX_PHY_API_S_VER_2 */

struct iwm_statistics_rx_ht_phy {
	uint32_t plcp_err;
	uint32_t overrun_err;
	uint32_t early_overrun_err;
	uint32_t crc32_good;
	uint32_t crc32_err;
	uint32_t mh_format_err;
	uint32_t agg_crc32_good;
	uint32_t agg_mpdu_cnt;
	uint32_t agg_cnt;
	uint32_t unsupport_mcs;
} __packed;  /* IWM_STATISTICS_HT_RX_PHY_API_S_VER_1 */

#define IWM_MAX_CHAINS 3

struct iwm_statistics_tx_non_phy_agg {
	uint32_t ba_timeout;
	uint32_t ba_reschedule_frames;
	uint32_t scd_query_agg_frame_cnt;
	uint32_t scd_query_no_agg;
	uint32_t scd_query_agg;
	uint32_t scd_query_mismatch;
	uint32_t frame_not_ready;
	uint32_t underrun;
	uint32_t bt_prio_kill;
	uint32_t rx_ba_rsp_cnt;
	int8_t txpower[IWM_MAX_CHAINS];
	int8_t reserved;
	uint32_t reserved2;
} __packed; /* IWM_STATISTICS_TX_NON_PHY_AGG_API_S_VER_1 */

struct iwm_statistics_tx_channel_width {
	uint32_t ext_cca_narrow_ch20[1];
	uint32_t ext_cca_narrow_ch40[2];
	uint32_t ext_cca_narrow_ch80[3];
	uint32_t ext_cca_narrow_ch160[4];
	uint32_t last_tx_ch_width_indx;
	uint32_t rx_detected_per_ch_width[4];
	uint32_t success_per_ch_width[4];
	uint32_t fail_per_ch_width[4];
}; /* IWM_STATISTICS_TX_CHANNEL_WIDTH_API_S_VER_1 */

struct iwm_statistics_tx {
	uint32_t preamble_cnt;
	uint32_t rx_detected_cnt;
	uint32_t bt_prio_defer_cnt;
	uint32_t bt_prio_kill_cnt;
	uint32_t few_bytes_cnt;
	uint32_t cts_timeout;
	uint32_t ack_timeout;
	uint32_t expected_ack_cnt;
	uint32_t actual_ack_cnt;
	uint32_t dump_msdu_cnt;
	uint32_t burst_abort_next_frame_mismatch_cnt;
	uint32_t burst_abort_missing_next_frame_cnt;
	uint32_t cts_timeout_collision;
	uint32_t ack_or_ba_timeout_collision;
	struct iwm_statistics_tx_non_phy_agg agg;
	struct iwm_statistics_tx_channel_width channel_width;
} __packed; /* IWM_STATISTICS_TX_API_S_VER_4 */


struct iwm_statistics_bt_activity {
	uint32_t hi_priority_tx_req_cnt;
	uint32_t hi_priority_tx_denied_cnt;
	uint32_t lo_priority_tx_req_cnt;
	uint32_t lo_priority_tx_denied_cnt;
	uint32_t hi_priority_rx_req_cnt;
	uint32_t hi_priority_rx_denied_cnt;
	uint32_t lo_priority_rx_req_cnt;
	uint32_t lo_priority_rx_denied_cnt;
} __packed;  /* IWM_STATISTICS_BT_ACTIVITY_API_S_VER_1 */

struct iwm_statistics_general {
	struct iwm_statistics_general_common common;
	uint32_t beacon_filtered;
	uint32_t missed_beacons;
	int8_t beacon_filter_average_energy;
	int8_t beacon_filter_reason;
	int8_t beacon_filter_current_energy;
	int8_t beacon_filter_reserved;
	uint32_t beacon_filter_delta_time;
	struct iwm_statistics_bt_activity bt_activity;
} __packed; /* IWM_STATISTICS_GENERAL_API_S_VER_5 */

struct iwm_statistics_rx {
	struct iwm_statistics_rx_phy ofdm;
	struct iwm_statistics_rx_phy cck;
	struct iwm_statistics_rx_non_phy general;
	struct iwm_statistics_rx_ht_phy ofdm_ht;
} __packed; /* IWM_STATISTICS_RX_API_S_VER_3 */

/*
 * IWM_STATISTICS_NOTIFICATION = 0x9d (notification only, not a command)
 *
 * By default, uCode issues this notification after receiving a beacon
 * while associated.  To disable this behavior, set DISABLE_NOTIF flag in the
 * IWM_REPLY_STATISTICS_CMD 0x9c, above.
 *
 * Statistics counters continue to increment beacon after beacon, but are
 * cleared when changing channels or when driver issues IWM_REPLY_STATISTICS_CMD
 * 0x9c with CLEAR_STATS bit set (see above).
 *
 * uCode also issues this notification during scans.  uCode clears statistics
 * appropriately so that each notification contains statistics for only the
 * one channel that has just been scanned.
 */

struct iwm_notif_statistics { /* IWM_STATISTICS_NTFY_API_S_VER_8 */
	uint32_t flag;
	struct iwm_statistics_rx rx;
	struct iwm_statistics_tx tx;
	struct iwm_statistics_general general;
} __packed;

/***********************************
 * Smart Fifo API
 ***********************************/
/* Smart Fifo state */
#define IWM_SF_LONG_DELAY_ON	0 /* should never be called by driver */
#define IWM_SF_FULL_ON		1
#define IWM_SF_UNINIT		2
#define IWM_SF_INIT_OFF		3
#define IWM_SF_HW_NUM_STATES	4

/* Smart Fifo possible scenario */
#define IWM_SF_SCENARIO_SINGLE_UNICAST	0
#define IWM_SF_SCENARIO_AGG_UNICAST	1
#define IWM_SF_SCENARIO_MULTICAST	2
#define IWM_SF_SCENARIO_BA_RESP		3
#define IWM_SF_SCENARIO_TX_RESP		4
#define IWM_SF_NUM_SCENARIO		5

#define IWM_SF_TRANSIENT_STATES_NUMBER 2 /* IWM_SF_LONG_DELAY_ON and IWM_SF_FULL_ON */
#define IWM_SF_NUM_TIMEOUT_TYPES 2	/* Aging timer and Idle timer */

/* smart FIFO default values */
#define IWM_SF_W_MARK_SISO 4096
#define IWM_SF_W_MARK_MIMO2 8192
#define IWM_SF_W_MARK_MIMO3 6144
#define IWM_SF_W_MARK_LEGACY 4096
#define IWM_SF_W_MARK_SCAN 4096

/* SF Scenarios timers for default configuration (aligned to 32 uSec) */
#define IWM_SF_SINGLE_UNICAST_IDLE_TIMER_DEF 160	/* 150 uSec  */
#define IWM_SF_SINGLE_UNICAST_AGING_TIMER_DEF 400	/* 0.4 mSec */
#define IWM_SF_AGG_UNICAST_IDLE_TIMER_DEF 160		/* 150 uSec */
#define IWM_SF_AGG_UNICAST_AGING_TIMER_DEF 400		/* 0.4 mSec */
#define IWM_SF_MCAST_IDLE_TIMER_DEF 160			/* 150 mSec */
#define IWM_SF_MCAST_AGING_TIMER_DEF 400		/* 0.4 mSec */
#define IWM_SF_BA_IDLE_TIMER_DEF 160			/* 150 uSec */
#define IWM_SF_BA_AGING_TIMER_DEF 400			/* 0.4 mSec */
#define IWM_SF_TX_RE_IDLE_TIMER_DEF 160			/* 150 uSec */
#define IWM_SF_TX_RE_AGING_TIMER_DEF 400		/* 0.4 mSec */

/* SF Scenarios timers for FULL_ON state (aligned to 32 uSec) */
#define IWM_SF_SINGLE_UNICAST_IDLE_TIMER 320	/* 300 uSec  */
#define IWM_SF_SINGLE_UNICAST_AGING_TIMER 2016	/* 2 mSec */
#define IWM_SF_AGG_UNICAST_IDLE_TIMER 320	/* 300 uSec */
#define IWM_SF_AGG_UNICAST_AGING_TIMER 2016	/* 2 mSec */
#define IWM_SF_MCAST_IDLE_TIMER 2016		/* 2 mSec */
#define IWM_SF_MCAST_AGING_TIMER 10016		/* 10 mSec */
#define IWM_SF_BA_IDLE_TIMER 320		/* 300 uSec */
#define IWM_SF_BA_AGING_TIMER 2016		/* 2 mSec */
#define IWM_SF_TX_RE_IDLE_TIMER 320		/* 300 uSec */
#define IWM_SF_TX_RE_AGING_TIMER 2016		/* 2 mSec */

#define IWM_SF_LONG_DELAY_AGING_TIMER 1000000	/* 1 Sec */

#define IWM_SF_CFG_DUMMY_NOTIF_OFF	(1 << 16)

/**
 * Smart Fifo configuration command.
 * @state: smart fifo state, types listed in enum %iwm_sf_state.
 * @watermark: Minimum allowed available free space in RXF for transient state.
 * @long_delay_timeouts: aging and idle timer values for each scenario
 * in long delay state.
 * @full_on_timeouts: timer values for each scenario in full on state.
 */
struct iwm_sf_cfg_cmd {
	uint32_t state;
	uint32_t watermark[IWM_SF_TRANSIENT_STATES_NUMBER];
	uint32_t long_delay_timeouts[IWM_SF_NUM_SCENARIO][IWM_SF_NUM_TIMEOUT_TYPES];
	uint32_t full_on_timeouts[IWM_SF_NUM_SCENARIO][IWM_SF_NUM_TIMEOUT_TYPES];
} __packed; /* IWM_SF_CFG_API_S_VER_2 */

/*
 * The first MAC indices (starting from 0)
 * are available to the driver, AUX follows
 */
#define IWM_MAC_INDEX_AUX		4
#define IWM_MAC_INDEX_MIN_DRIVER	0
#define IWM_NUM_MAC_INDEX_DRIVER	IWM_MAC_INDEX_AUX

#define IWM_AC_BK	0
#define IWM_AC_BE	1
#define IWM_AC_VI	2
#define IWM_AC_VO	3
#define IWM_AC_NUM	4

/**
 * MAC context flags
 * @IWM_MAC_PROT_FLG_TGG_PROTECT: 11g protection when transmitting OFDM frames,
 *	this will require CCK RTS/CTS2self.
 *	RTS/CTS will protect full burst time.
 * @IWM_MAC_PROT_FLG_HT_PROT: enable HT protection
 * @IWM_MAC_PROT_FLG_FAT_PROT: protect 40 MHz transmissions
 * @IWM_MAC_PROT_FLG_SELF_CTS_EN: allow CTS2self
 */
#define IWM_MAC_PROT_FLG_TGG_PROTECT	(1 << 3)
#define IWM_MAC_PROT_FLG_HT_PROT	(1 << 23)
#define IWM_MAC_PROT_FLG_FAT_PROT	(1 << 24)
#define IWM_MAC_PROT_FLG_SELF_CTS_EN	(1 << 30)

#define IWM_MAC_FLG_SHORT_SLOT		(1 << 4)
#define IWM_MAC_FLG_SHORT_PREAMBLE	(1 << 5)

/**
 * Supported MAC types
 * @IWM_FW_MAC_TYPE_FIRST: lowest supported MAC type
 * @IWM_FW_MAC_TYPE_AUX: Auxiliary MAC (internal)
 * @IWM_FW_MAC_TYPE_LISTENER: monitor MAC type (?)
 * @IWM_FW_MAC_TYPE_PIBSS: Pseudo-IBSS
 * @IWM_FW_MAC_TYPE_IBSS: IBSS
 * @IWM_FW_MAC_TYPE_BSS_STA: BSS (managed) station
 * @IWM_FW_MAC_TYPE_P2P_DEVICE: P2P Device
 * @IWM_FW_MAC_TYPE_P2P_STA: P2P client
 * @IWM_FW_MAC_TYPE_GO: P2P GO
 * @IWM_FW_MAC_TYPE_TEST: ?
 * @IWM_FW_MAC_TYPE_MAX: highest support MAC type
 */
#define IWM_FW_MAC_TYPE_FIRST		1
#define IWM_FW_MAC_TYPE_AUX		IWM_FW_MAC_TYPE_FIRST
#define IWM_FW_MAC_TYPE_LISTENER	2
#define IWM_FW_MAC_TYPE_PIBSS		3
#define IWM_FW_MAC_TYPE_IBSS		4
#define IWM_FW_MAC_TYPE_BSS_STA		5
#define IWM_FW_MAC_TYPE_P2P_DEVICE	6
#define IWM_FW_MAC_TYPE_P2P_STA		7
#define IWM_FW_MAC_TYPE_GO		8
#define IWM_FW_MAC_TYPE_TEST		9
#define IWM_FW_MAC_TYPE_MAX		IWM_FW_MAC_TYPE_TEST
/* IWM_MAC_CONTEXT_TYPE_API_E_VER_1 */

/**
 * TSF hw timer ID
 * @IWM_TSF_ID_A: use TSF A
 * @IWM_TSF_ID_B: use TSF B
 * @IWM_TSF_ID_C: use TSF C
 * @IWM_TSF_ID_D: use TSF D
 * @IWM_NUM_TSF_IDS: number of TSF timers available
 */
#define IWM_TSF_ID_A	0
#define IWM_TSF_ID_B	1
#define IWM_TSF_ID_C	2
#define IWM_TSF_ID_D	3
#define IWM_NUM_TSF_IDS	4
/* IWM_TSF_ID_API_E_VER_1 */

/**
 * struct iwm_mac_data_ap - configuration data for AP MAC context
 * @beacon_time: beacon transmit time in system time
 * @beacon_tsf: beacon transmit time in TSF
 * @bi: beacon interval in TU
 * @bi_reciprocal: 2^32 / bi
 * @dtim_interval: dtim transmit time in TU
 * @dtim_reciprocal: 2^32 / dtim_interval
 * @mcast_qid: queue ID for multicast traffic
 *	NOTE: obsolete from VER2 and on
 * @beacon_template: beacon template ID
 */
struct iwm_mac_data_ap {
	uint32_t beacon_time;
	uint64_t beacon_tsf;
	uint32_t bi;
	uint32_t bi_reciprocal;
	uint32_t dtim_interval;
	uint32_t dtim_reciprocal;
	uint32_t mcast_qid;
	uint32_t beacon_template;
} __packed; /* AP_MAC_DATA_API_S_VER_2 */

/**
 * struct iwm_mac_data_ibss - configuration data for IBSS MAC context
 * @beacon_time: beacon transmit time in system time
 * @beacon_tsf: beacon transmit time in TSF
 * @bi: beacon interval in TU
 * @bi_reciprocal: 2^32 / bi
 * @beacon_template: beacon template ID
 */
struct iwm_mac_data_ibss {
	uint32_t beacon_time;
	uint64_t beacon_tsf;
	uint32_t bi;
	uint32_t bi_reciprocal;
	uint32_t beacon_template;
} __packed; /* IBSS_MAC_DATA_API_S_VER_1 */

/**
 * struct iwm_mac_data_sta - configuration data for station MAC context
 * @is_assoc: 1 for associated state, 0 otherwise
 * @dtim_time: DTIM arrival time in system time
 * @dtim_tsf: DTIM arrival time in TSF
 * @bi: beacon interval in TU, applicable only when associated
 * @bi_reciprocal: 2^32 / bi , applicable only when associated
 * @dtim_interval: DTIM interval in TU, applicable only when associated
 * @dtim_reciprocal: 2^32 / dtim_interval , applicable only when associated
 * @listen_interval: in beacon intervals, applicable only when associated
 * @assoc_id: unique ID assigned by the AP during association
 */
struct iwm_mac_data_sta {
	uint32_t is_assoc;
	uint32_t dtim_time;
	uint64_t dtim_tsf;
	uint32_t bi;
	uint32_t bi_reciprocal;
	uint32_t dtim_interval;
	uint32_t dtim_reciprocal;
	uint32_t listen_interval;
	uint32_t assoc_id;
	uint32_t assoc_beacon_arrive_time;
} __packed; /* IWM_STA_MAC_DATA_API_S_VER_1 */

/**
 * struct iwm_mac_data_go - configuration data for P2P GO MAC context
 * @ap: iwm_mac_data_ap struct with most config data
 * @ctwin: client traffic window in TU (period after TBTT when GO is present).
 *	0 indicates that there is no CT window.
 * @opp_ps_enabled: indicate that opportunistic PS allowed
 */
struct iwm_mac_data_go {
	struct iwm_mac_data_ap ap;
	uint32_t ctwin;
	uint32_t opp_ps_enabled;
} __packed; /* GO_MAC_DATA_API_S_VER_1 */

/**
 * struct iwm_mac_data_p2p_sta - configuration data for P2P client MAC context
 * @sta: iwm_mac_data_sta struct with most config data
 * @ctwin: client traffic window in TU (period after TBTT when GO is present).
 *	0 indicates that there is no CT window.
 */
struct iwm_mac_data_p2p_sta {
	struct iwm_mac_data_sta sta;
	uint32_t ctwin;
} __packed; /* P2P_STA_MAC_DATA_API_S_VER_1 */

/**
 * struct iwm_mac_data_pibss - Pseudo IBSS config data
 * @stats_interval: interval in TU between statistics notifications to host.
 */
struct iwm_mac_data_pibss {
	uint32_t stats_interval;
} __packed; /* PIBSS_MAC_DATA_API_S_VER_1 */

/*
 * struct iwm_mac_data_p2p_dev - configuration data for the P2P Device MAC
 * context.
 * @is_disc_extended: if set to true, P2P Device discoverability is enabled on
 *	other channels as well. This should be to true only in case that the
 *	device is discoverable and there is an active GO. Note that setting this
 *	field when not needed, will increase the number of interrupts and have
 *	effect on the platform power, as this setting opens the Rx filters on
 *	all macs.
 */
struct iwm_mac_data_p2p_dev {
	uint32_t is_disc_extended;
} __packed; /* _P2P_DEV_MAC_DATA_API_S_VER_1 */

/**
 * MAC context filter flags
 * @IWM_MAC_FILTER_IN_PROMISC: accept all data frames
 * @IWM_MAC_FILTER_IN_CONTROL_AND_MGMT: pass all management and
 *	control frames to the host
 * @IWM_MAC_FILTER_ACCEPT_GRP: accept multicast frames
 * @IWM_MAC_FILTER_DIS_DECRYPT: don't decrypt unicast frames
 * @IWM_MAC_FILTER_DIS_GRP_DECRYPT: don't decrypt multicast frames
 * @IWM_MAC_FILTER_IN_BEACON: transfer foreign BSS's beacons to host
 *	(in station mode when associated)
 * @IWM_MAC_FILTER_OUT_BCAST: filter out all broadcast frames
 * @IWM_MAC_FILTER_IN_CRC32: extract FCS and append it to frames
 * @IWM_MAC_FILTER_IN_PROBE_REQUEST: pass probe requests to host
 */
#define IWM_MAC_FILTER_IN_PROMISC		(1 << 0)
#define IWM_MAC_FILTER_IN_CONTROL_AND_MGMT	(1 << 1)
#define IWM_MAC_FILTER_ACCEPT_GRP		(1 << 2)
#define IWM_MAC_FILTER_DIS_DECRYPT		(1 << 3)
#define IWM_MAC_FILTER_DIS_GRP_DECRYPT		(1 << 4)
#define IWM_MAC_FILTER_IN_BEACON		(1 << 6)
#define IWM_MAC_FILTER_OUT_BCAST		(1 << 8)
#define IWM_MAC_FILTER_IN_CRC32			(1 << 11)
#define IWM_MAC_FILTER_IN_PROBE_REQUEST		(1 << 12)

/**
 * QoS flags
 * @IWM_MAC_QOS_FLG_UPDATE_EDCA: ?
 * @IWM_MAC_QOS_FLG_TGN: HT is enabled
 * @IWM_MAC_QOS_FLG_TXOP_TYPE: ?
 *
 */
#define IWM_MAC_QOS_FLG_UPDATE_EDCA	(1 << 0)
#define IWM_MAC_QOS_FLG_TGN		(1 << 1)
#define IWM_MAC_QOS_FLG_TXOP_TYPE	(1 << 4)

/**
 * struct iwm_ac_qos - QOS timing params for IWM_MAC_CONTEXT_CMD
 * @cw_min: Contention window, start value in numbers of slots.
 *	Should be a power-of-2, minus 1.  Device's default is 0x0f.
 * @cw_max: Contention window, max value in numbers of slots.
 *	Should be a power-of-2, minus 1.  Device's default is 0x3f.
 * @aifsn:  Number of slots in Arbitration Interframe Space (before
 *	performing random backoff timing prior to Tx).  Device default 1.
 * @fifos_mask: FIFOs used by this MAC for this AC
 * @edca_txop:  Length of Tx opportunity, in uSecs.  Device default is 0.
 *
 * One instance of this config struct for each of 4 EDCA access categories
 * in struct iwm_qosparam_cmd.
 *
 * Device will automatically increase contention window by (2*CW) + 1 for each
 * transmission retry.  Device uses cw_max as a bit mask, ANDed with new CW
 * value, to cap the CW value.
 */
struct iwm_ac_qos {
	uint16_t cw_min;
	uint16_t cw_max;
	uint8_t aifsn;
	uint8_t fifos_mask;
	uint16_t edca_txop;
} __packed; /* IWM_AC_QOS_API_S_VER_2 */

/**
 * struct iwm_mac_ctx_cmd - command structure to configure MAC contexts
 * ( IWM_MAC_CONTEXT_CMD = 0x28 )
 * @id_and_color: ID and color of the MAC
 * @action: action to perform, one of IWM_FW_CTXT_ACTION_*
 * @mac_type: one of IWM_FW_MAC_TYPE_*
 * @tsf_id: TSF HW timer, one of IWM_TSF_ID_*
 * @node_addr: MAC address
 * @bssid_addr: BSSID
 * @cck_rates: basic rates available for CCK
 * @ofdm_rates: basic rates available for OFDM
 * @protection_flags: combination of IWM_MAC_PROT_FLG_FLAG_*
 * @cck_short_preamble: 0x20 for enabling short preamble, 0 otherwise
 * @short_slot: 0x10 for enabling short slots, 0 otherwise
 * @filter_flags: combination of IWM_MAC_FILTER_*
 * @qos_flags: from IWM_MAC_QOS_FLG_*
 * @ac: one iwm_mac_qos configuration for each AC
 * @mac_specific: one of struct iwm_mac_data_*, according to mac_type
 */
struct iwm_mac_ctx_cmd {
	/* COMMON_INDEX_HDR_API_S_VER_1 */
	uint32_t id_and_color;
	uint32_t action;
	/* IWM_MAC_CONTEXT_COMMON_DATA_API_S_VER_1 */
	uint32_t mac_type;
	uint32_t tsf_id;
	uint8_t node_addr[6];
	uint16_t reserved_for_node_addr;
	uint8_t bssid_addr[6];
	uint16_t reserved_for_bssid_addr;
	uint32_t cck_rates;
	uint32_t ofdm_rates;
	uint32_t protection_flags;
	uint32_t cck_short_preamble;
	uint32_t short_slot;
	uint32_t filter_flags;
	/* IWM_MAC_QOS_PARAM_API_S_VER_1 */
	uint32_t qos_flags;
	struct iwm_ac_qos ac[IWM_AC_NUM+1];
	/* IWM_MAC_CONTEXT_COMMON_DATA_API_S */
	union {
		struct iwm_mac_data_ap ap;
		struct iwm_mac_data_go go;
		struct iwm_mac_data_sta sta;
		struct iwm_mac_data_p2p_sta p2p_sta;
		struct iwm_mac_data_p2p_dev p2p_dev;
		struct iwm_mac_data_pibss pibss;
		struct iwm_mac_data_ibss ibss;
	};
} __packed; /* IWM_MAC_CONTEXT_CMD_API_S_VER_1 */

static inline uint32_t iwm_reciprocal(uint32_t v)
{
	if (!v)
		return 0;
	return 0xFFFFFFFF / v;
}

#define IWM_NONQOS_SEQ_GET	0x1
#define IWM_NONQOS_SEQ_SET	0x2
struct iwm_nonqos_seq_query_cmd {
	uint32_t get_set_flag;
	uint32_t mac_id_n_color;
	uint16_t value;
	uint16_t reserved;
} __packed; /* IWM_NON_QOS_TX_COUNTER_GET_SET_API_S_VER_1 */

/* Power Management Commands, Responses, Notifications */

/**
 * masks for LTR config command flags
 * @IWM_LTR_CFG_FLAG_FEATURE_ENABLE: Feature operational status
 * @IWM_LTR_CFG_FLAG_HW_DIS_ON_SHADOW_REG_ACCESS: allow LTR change on shadow
 *      memory access
 * @IWM_LTR_CFG_FLAG_HW_EN_SHRT_WR_THROUGH: allow LTR msg send on ANY LTR
 *      reg change
 * @IWM_LTR_CFG_FLAG_HW_DIS_ON_D0_2_D3: allow LTR msg send on transition from
 *      D0 to D3
 * @IWM_LTR_CFG_FLAG_SW_SET_SHORT: fixed static short LTR register
 * @IWM_LTR_CFG_FLAG_SW_SET_LONG: fixed static short LONG register
 * @IWM_LTR_CFG_FLAG_DENIE_C10_ON_PD: allow going into C10 on PD
 */
#define IWM_LTR_CFG_FLAG_FEATURE_ENABLE			0x00000001
#define IWM_LTR_CFG_FLAG_HW_DIS_ON_SHADOW_REG_ACCESS	0x00000002
#define IWM_LTR_CFG_FLAG_HW_EN_SHRT_WR_THROUGH		0x00000004
#define IWM_LTR_CFG_FLAG_HW_DIS_ON_D0_2_D3		0x00000008
#define IWM_LTR_CFG_FLAG_SW_SET_SHORT			0x00000010
#define IWM_LTR_CFG_FLAG_SW_SET_LONG			0x00000020
#define IWM_LTR_CFG_FLAG_DENIE_C10_ON_PD		0x00000040

/**
 * struct iwm_ltr_config_cmd_v1 - configures the LTR
 * @flags: See %enum iwm_ltr_config_flags
 */
struct iwm_ltr_config_cmd_v1 {
	uint32_t flags;
	uint32_t static_long;
	uint32_t static_short;
} __packed; /* LTR_CAPABLE_API_S_VER_1 */

#define IWM_LTR_VALID_STATES_NUM 4

/**
 * struct iwm_ltr_config_cmd - configures the LTR
 * @flags: See %enum iwm_ltr_config_flags
 * @static_long:
 * @static_short:
 * @ltr_cfg_values:
 * @ltr_short_idle_timeout:
 */
struct iwm_ltr_config_cmd {
	uint32_t flags;
	uint32_t static_long;
	uint32_t static_short;
	uint32_t ltr_cfg_values[IWM_LTR_VALID_STATES_NUM];
	uint32_t ltr_short_idle_timeout;
} __packed; /* LTR_CAPABLE_API_S_VER_2 */

/* Radio LP RX Energy Threshold measured in dBm */
#define IWM_POWER_LPRX_RSSI_THRESHOLD	75
#define IWM_POWER_LPRX_RSSI_THRESHOLD_MAX	94
#define IWM_POWER_LPRX_RSSI_THRESHOLD_MIN	30

/**
 * Masks for iwm_mac_power_cmd command flags
 * @IWM_POWER_FLAGS_POWER_SAVE_ENA_MSK: '1' Allow to save power by turning off
 *		receiver and transmitter. '0' - does not allow.
 * @IWM_POWER_FLAGS_POWER_MANAGEMENT_ENA_MSK: '0' Driver disables power management,
 *		'1' Driver enables PM (use rest of parameters)
 * @IWM_POWER_FLAGS_SKIP_OVER_DTIM_MSK: '0' PM have to walk up every DTIM,
 *		'1' PM could sleep over DTIM till listen Interval.
 * @IWM_POWER_FLAGS_SNOOZE_ENA_MSK: Enable snoozing only if uAPSD is enabled and all
 *		access categories are both delivery and trigger enabled.
 * @IWM_POWER_FLAGS_BT_SCO_ENA: Enable BT SCO coex only if uAPSD and
 *		PBW Snoozing enabled
 * @IWM_POWER_FLAGS_ADVANCE_PM_ENA_MSK: Advanced PM (uAPSD) enable mask
 * @IWM_POWER_FLAGS_LPRX_ENA_MSK: Low Power RX enable.
 * @IWM_POWER_FLAGS_AP_UAPSD_MISBEHAVING_ENA_MSK: AP/GO's uAPSD misbehaving
 *		detection enablement
*/
#define IWM_POWER_FLAGS_POWER_SAVE_ENA_MSK		(1 << 0)
#define IWM_POWER_FLAGS_POWER_MANAGEMENT_ENA_MSK	(1 << 1)
#define IWM_POWER_FLAGS_SKIP_OVER_DTIM_MSK		(1 << 2)
#define IWM_POWER_FLAGS_SNOOZE_ENA_MSK			(1 << 5)
#define IWM_POWER_FLAGS_BT_SCO_ENA			(1 << 8)
#define IWM_POWER_FLAGS_ADVANCE_PM_ENA_MSK		(1 << 9)
#define IWM_POWER_FLAGS_LPRX_ENA_MSK			(1 << 11)
#define IWM_POWER_FLAGS_UAPSD_MISBEHAVING_ENA_MSK	(1 << 12)

#define IWM_POWER_VEC_SIZE 5

/**
 * Masks for device power command flags
 * @IWM_DEVICE_POWER_FLAGS_POWER_SAVE_ENA_MSK:
 *   '1' Allow to save power by turning off receiver and transmitter.
 *   '0' Do not allow. This flag should be always set to '1' unless
 *       one needs to disable actual power down for debug purposes.
 * @IWM_DEVICE_POWER_FLAGS_CAM_MSK:
 *   '1' CAM (Continuous Active Mode) is set, power management is disabled.
 *   '0' Power management is enabled, one of the power schemes is applied.
 */
#define IWM_DEVICE_POWER_FLAGS_POWER_SAVE_ENA_MSK	(1 << 0)
#define IWM_DEVICE_POWER_FLAGS_CAM_MSK			(1 << 13)

/**
 * struct iwm_device_power_cmd - device wide power command.
 * IWM_POWER_TABLE_CMD = 0x77 (command, has simple generic response)
 *
 * @flags:	Power table command flags from IWM_DEVICE_POWER_FLAGS_*
 */
struct iwm_device_power_cmd {
	/* PM_POWER_TABLE_CMD_API_S_VER_6 */
	uint16_t flags;
	uint16_t reserved;
} __packed;

/**
 * struct iwm_mac_power_cmd - New power command containing uAPSD support
 * IWM_MAC_PM_POWER_TABLE = 0xA9 (command, has simple generic response)
 * @id_and_color:	MAC context identifier
 * @flags:		Power table command flags from POWER_FLAGS_*
 * @keep_alive_seconds:	Keep alive period in seconds. Default - 25 sec.
 *			Minimum allowed:- 3 * DTIM. Keep alive period must be
 *			set regardless of power scheme or current power state.
 *			FW use this value also when PM is disabled.
 * @rx_data_timeout:    Minimum time (usec) from last Rx packet for AM to
 *			PSM transition - legacy PM
 * @tx_data_timeout:    Minimum time (usec) from last Tx packet for AM to
 *			PSM transition - legacy PM
 * @sleep_interval:	not in use
 * @skip_dtim_periods:	Number of DTIM periods to skip if Skip over DTIM flag
 *			is set. For example, if it is required to skip over
 *			one DTIM, this value need to be set to 2 (DTIM periods).
 * @rx_data_timeout_uapsd: Minimum time (usec) from last Rx packet for AM to
 *			PSM transition - uAPSD
 * @tx_data_timeout_uapsd: Minimum time (usec) from last Tx packet for AM to
 *			PSM transition - uAPSD
 * @lprx_rssi_threshold: Signal strength up to which LP RX can be enabled.
 *			Default: 80dbm
 * @num_skip_dtim:	Number of DTIMs to skip if Skip over DTIM flag is set
 * @snooze_interval:	Maximum time between attempts to retrieve buffered data
 *			from the AP [msec]
 * @snooze_window:	A window of time in which PBW snoozing insures that all
 *			packets received. It is also the minimum time from last
 *			received unicast RX packet, before client stops snoozing
 *			for data. [msec]
 * @snooze_step:	TBD
 * @qndp_tid:		TID client shall use for uAPSD QNDP triggers
 * @uapsd_ac_flags:	Set trigger-enabled and delivery-enabled indication for
 *			each corresponding AC.
 *			Use IEEE80211_WMM_IE_STA_QOSINFO_AC* for correct values.
 * @uapsd_max_sp:	Use IEEE80211_WMM_IE_STA_QOSINFO_SP_* for correct
 *			values.
 * @heavy_tx_thld_packets:	TX threshold measured in number of packets
 * @heavy_rx_thld_packets:	RX threshold measured in number of packets
 * @heavy_tx_thld_percentage:	TX threshold measured in load's percentage
 * @heavy_rx_thld_percentage:	RX threshold measured in load's percentage
 * @limited_ps_threshold:
*/
struct iwm_mac_power_cmd {
	/* CONTEXT_DESC_API_T_VER_1 */
	uint32_t id_and_color;

	/* CLIENT_PM_POWER_TABLE_S_VER_1 */
	uint16_t flags;
	uint16_t keep_alive_seconds;
	uint32_t rx_data_timeout;
	uint32_t tx_data_timeout;
	uint32_t rx_data_timeout_uapsd;
	uint32_t tx_data_timeout_uapsd;
	uint8_t lprx_rssi_threshold;
	uint8_t skip_dtim_periods;
	uint16_t snooze_interval;
	uint16_t snooze_window;
	uint8_t snooze_step;
	uint8_t qndp_tid;
	uint8_t uapsd_ac_flags;
	uint8_t uapsd_max_sp;
	uint8_t heavy_tx_thld_packets;
	uint8_t heavy_rx_thld_packets;
	uint8_t heavy_tx_thld_percentage;
	uint8_t heavy_rx_thld_percentage;
	uint8_t limited_ps_threshold;
	uint8_t reserved;
} __packed;

#define IWM_DEFAULT_PS_TX_DATA_TIMEOUT      (100 * 1000)
#define IWM_DEFAULT_PS_RX_DATA_TIMEOUT      (100 * 1000)

/*
 * struct iwm_uapsd_misbehaving_ap_notif - FW sends this notification when
 * associated AP is identified as improperly implementing uAPSD protocol.
 * IWM_PSM_UAPSD_AP_MISBEHAVING_NOTIFICATION = 0x78
 * @sta_id: index of station in uCode's station table - associated AP ID in
 *	    this context.
 */
struct iwm_uapsd_misbehaving_ap_notif {
	uint32_t sta_id;
	uint8_t mac_id;
	uint8_t reserved[3];
} __packed;

/**
 * struct iwm_beacon_filter_cmd
 * IWM_REPLY_BEACON_FILTERING_CMD = 0xd2 (command)
 * @id_and_color: MAC context identifier
 * @bf_energy_delta: Used for RSSI filtering, if in 'normal' state. Send beacon
 *      to driver if delta in Energy values calculated for this and last
 *      passed beacon is greater than this threshold. Zero value means that
 *      the Energy change is ignored for beacon filtering, and beacon will
 *      not be forced to be sent to driver regardless of this delta. Typical
 *      energy delta 5dB.
 * @bf_roaming_energy_delta: Used for RSSI filtering, if in 'roaming' state.
 *      Send beacon to driver if delta in Energy values calculated for this
 *      and last passed beacon is greater than this threshold. Zero value
 *      means that the Energy change is ignored for beacon filtering while in
 *      Roaming state, typical energy delta 1dB.
 * @bf_roaming_state: Used for RSSI filtering. If absolute Energy values
 *      calculated for current beacon is less than the threshold, use
 *      Roaming Energy Delta Threshold, otherwise use normal Energy Delta
 *      Threshold. Typical energy threshold is -72dBm.
 * @bf_temp_threshold: This threshold determines the type of temperature
 *	filtering (Slow or Fast) that is selected (Units are in Celsius):
 *      If the current temperature is above this threshold - Fast filter
 *	will be used, If the current temperature is below this threshold -
 *	Slow filter will be used.
 * @bf_temp_fast_filter: Send Beacon to driver if delta in temperature values
 *      calculated for this and the last passed beacon is greater than this
 *      threshold. Zero value means that the temperature change is ignored for
 *      beacon filtering; beacons will not be  forced to be sent to driver
 *      regardless of whether its temperature has been changed.
 * @bf_temp_slow_filter: Send Beacon to driver if delta in temperature values
 *      calculated for this and the last passed beacon is greater than this
 *      threshold. Zero value means that the temperature change is ignored for
 *      beacon filtering; beacons will not be forced to be sent to driver
 *      regardless of whether its temperature has been changed.
 * @bf_enable_beacon_filter: 1, beacon filtering is enabled; 0, disabled.
 * @bf_escape_timer: Send beacons to driver if no beacons were passed
 *      for a specific period of time. Units: Beacons.
 * @ba_escape_timer: Fully receive and parse beacon if no beacons were passed
 *      for a longer period of time then this escape-timeout. Units: Beacons.
 * @ba_enable_beacon_abort: 1, beacon abort is enabled; 0, disabled.
 */
struct iwm_beacon_filter_cmd {
	uint32_t bf_energy_delta;
	uint32_t bf_roaming_energy_delta;
	uint32_t bf_roaming_state;
	uint32_t bf_temp_threshold;
	uint32_t bf_temp_fast_filter;
	uint32_t bf_temp_slow_filter;
	uint32_t bf_enable_beacon_filter;
	uint32_t bf_debug_flag;
	uint32_t bf_escape_timer;
	uint32_t ba_escape_timer;
	uint32_t ba_enable_beacon_abort;
} __packed;

/* Beacon filtering and beacon abort */
#define IWM_BF_ENERGY_DELTA_DEFAULT 5
#define IWM_BF_ENERGY_DELTA_MAX 255
#define IWM_BF_ENERGY_DELTA_MIN 0

#define IWM_BF_ROAMING_ENERGY_DELTA_DEFAULT 1
#define IWM_BF_ROAMING_ENERGY_DELTA_MAX 255
#define IWM_BF_ROAMING_ENERGY_DELTA_MIN 0

#define IWM_BF_ROAMING_STATE_DEFAULT 72
#define IWM_BF_ROAMING_STATE_MAX 255
#define IWM_BF_ROAMING_STATE_MIN 0

#define IWM_BF_TEMP_THRESHOLD_DEFAULT 112
#define IWM_BF_TEMP_THRESHOLD_MAX 255
#define IWM_BF_TEMP_THRESHOLD_MIN 0

#define IWM_BF_TEMP_FAST_FILTER_DEFAULT 1
#define IWM_BF_TEMP_FAST_FILTER_MAX 255
#define IWM_BF_TEMP_FAST_FILTER_MIN 0

#define IWM_BF_TEMP_SLOW_FILTER_DEFAULT 5
#define IWM_BF_TEMP_SLOW_FILTER_MAX 255
#define IWM_BF_TEMP_SLOW_FILTER_MIN 0

#define IWM_BF_ENABLE_BEACON_FILTER_DEFAULT 1

#define IWM_BF_DEBUG_FLAG_DEFAULT 0

#define IWM_BF_ESCAPE_TIMER_DEFAULT 50
#define IWM_BF_ESCAPE_TIMER_MAX 1024
#define IWM_BF_ESCAPE_TIMER_MIN 0

#define IWM_BA_ESCAPE_TIMER_DEFAULT 6
#define IWM_BA_ESCAPE_TIMER_D3 9
#define IWM_BA_ESCAPE_TIMER_MAX 1024
#define IWM_BA_ESCAPE_TIMER_MIN 0

#define IWM_BA_ENABLE_BEACON_ABORT_DEFAULT 1

#define IWM_BF_CMD_CONFIG_DEFAULTS					     \
	.bf_energy_delta = htole32(IWM_BF_ENERGY_DELTA_DEFAULT),	     \
	.bf_roaming_energy_delta =					     \
		htole32(IWM_BF_ROAMING_ENERGY_DELTA_DEFAULT),	     \
	.bf_roaming_state = htole32(IWM_BF_ROAMING_STATE_DEFAULT),	     \
	.bf_temp_threshold = htole32(IWM_BF_TEMP_THRESHOLD_DEFAULT),     \
	.bf_temp_fast_filter = htole32(IWM_BF_TEMP_FAST_FILTER_DEFAULT), \
	.bf_temp_slow_filter = htole32(IWM_BF_TEMP_SLOW_FILTER_DEFAULT), \
	.bf_debug_flag = htole32(IWM_BF_DEBUG_FLAG_DEFAULT),	     \
	.bf_escape_timer = htole32(IWM_BF_ESCAPE_TIMER_DEFAULT),	     \
	.ba_escape_timer = htole32(IWM_BA_ESCAPE_TIMER_DEFAULT)

/* uCode API values for HT/VHT bit rates */
#define IWM_RATE_HT_SISO_MCS_0_PLCP	0
#define IWM_RATE_HT_SISO_MCS_1_PLCP	1
#define IWM_RATE_HT_SISO_MCS_2_PLCP	2
#define IWM_RATE_HT_SISO_MCS_3_PLCP	3
#define IWM_RATE_HT_SISO_MCS_4_PLCP	4
#define IWM_RATE_HT_SISO_MCS_5_PLCP	5
#define IWM_RATE_HT_SISO_MCS_6_PLCP	6
#define IWM_RATE_HT_SISO_MCS_7_PLCP	7
#define IWM_RATE_HT_MIMO2_MCS_8_PLCP	0x8
#define IWM_RATE_HT_MIMO2_MCS_9_PLCP	0x9
#define IWM_RATE_HT_MIMO2_MCS_10_PLCP	0xA
#define IWM_RATE_HT_MIMO2_MCS_11_PLCP	0xB
#define IWM_RATE_HT_MIMO2_MCS_12_PLCP	0xC
#define IWM_RATE_HT_MIMO2_MCS_13_PLCP	0xD
#define IWM_RATE_HT_MIMO2_MCS_14_PLCP	0xE
#define IWM_RATE_HT_MIMO2_MCS_15_PLCP	0xF
#define IWM_RATE_VHT_SISO_MCS_0_PLCP	0
#define IWM_RATE_VHT_SISO_MCS_1_PLCP	1
#define IWM_RATE_VHT_SISO_MCS_2_PLCP	2
#define IWM_RATE_VHT_SISO_MCS_3_PLCP	3
#define IWM_RATE_VHT_SISO_MCS_4_PLCP	4
#define IWM_RATE_VHT_SISO_MCS_5_PLCP	5
#define IWM_RATE_VHT_SISO_MCS_6_PLCP	6
#define IWM_RATE_VHT_SISO_MCS_7_PLCP	7
#define IWM_RATE_VHT_SISO_MCS_8_PLCP	8
#define IWM_RATE_VHT_SISO_MCS_9_PLCP	9
#define IWM_RATE_VHT_MIMO2_MCS_0_PLCP	0x10
#define IWM_RATE_VHT_MIMO2_MCS_1_PLCP	0x11
#define IWM_RATE_VHT_MIMO2_MCS_2_PLCP	0x12
#define IWM_RATE_VHT_MIMO2_MCS_3_PLCP	0x13
#define IWM_RATE_VHT_MIMO2_MCS_4_PLCP	0x14
#define IWM_RATE_VHT_MIMO2_MCS_5_PLCP	0x15
#define IWM_RATE_VHT_MIMO2_MCS_6_PLCP	0x16
#define IWM_RATE_VHT_MIMO2_MCS_7_PLCP	0x17
#define IWM_RATE_VHT_MIMO2_MCS_8_PLCP	0x18
#define IWM_RATE_VHT_MIMO2_MCS_9_PLCP	0x19
#define IWM_RATE_HT_SISO_MCS_INV_PLCP	0x20
#define IWM_RATE_HT_MIMO2_MCS_INV_PLCP	IWM_RATE_HT_SISO_MCS_INV_PLCP
#define IWM_RATE_VHT_SISO_MCS_INV_PLCP	IWM_RATE_HT_SISO_MCS_INV_PLCP
#define IWM_RATE_VHT_MIMO2_MCS_INV_PLCP	IWM_RATE_HT_SISO_MCS_INV_PLCP
#define IWM_RATE_HT_SISO_MCS_8_PLCP	IWM_RATE_HT_SISO_MCS_INV_PLCP
#define IWM_RATE_HT_SISO_MCS_9_PLCP	IWM_RATE_HT_SISO_MCS_INV_PLCP

/*
 * These serve as indexes into struct iwm_rate iwm_rates[IWM_RIDX_MAX].
 */
enum {
	IWM_RATE_1M_INDEX = 0,
	IWM_FIRST_CCK_RATE = IWM_RATE_1M_INDEX,
	IWM_RATE_2M_INDEX,
	IWM_RATE_5M_INDEX,
	IWM_RATE_11M_INDEX,
	IWM_LAST_CCK_RATE = IWM_RATE_11M_INDEX,
	IWM_RATE_6M_INDEX,
	IWM_FIRST_OFDM_RATE = IWM_RATE_6M_INDEX,
	IWM_RATE_MCS_0_INDEX = IWM_RATE_6M_INDEX,
	IWM_FIRST_HT_RATE = IWM_RATE_MCS_0_INDEX,
	IWM_RATE_9M_INDEX,
	IWM_RATE_12M_INDEX,
	IWM_RATE_MCS_1_INDEX = IWM_RATE_12M_INDEX,
	IWM_RATE_MCS_8_INDEX,
	IWM_FIRST_HT_MIMO2_RATE = IWM_RATE_MCS_8_INDEX,
	IWM_RATE_18M_INDEX,
	IWM_RATE_MCS_2_INDEX = IWM_RATE_18M_INDEX,
	IWM_RATE_24M_INDEX,
	IWM_RATE_MCS_3_INDEX = IWM_RATE_24M_INDEX,
	IWM_RATE_MCS_9_INDEX,
	IWM_RATE_36M_INDEX,
	IWM_RATE_MCS_4_INDEX = IWM_RATE_36M_INDEX,
	IWM_RATE_MCS_10_INDEX,
	IWM_RATE_48M_INDEX,
	IWM_RATE_MCS_5_INDEX = IWM_RATE_48M_INDEX,
	IWM_RATE_MCS_11_INDEX,
	IWM_RATE_54M_INDEX,
	IWM_RATE_MCS_6_INDEX = IWM_RATE_54M_INDEX,
	IWM_LAST_NON_HT_RATE = IWM_RATE_54M_INDEX,
	IWM_RATE_MCS_7_INDEX,
	IWM_LAST_HT_SISO_RATE = IWM_RATE_MCS_7_INDEX,
	IWM_RATE_MCS_12_INDEX,
	IWM_RATE_MCS_13_INDEX,
	IWM_RATE_MCS_14_INDEX,
	IWM_RATE_MCS_15_INDEX,
	IWM_LAST_HT_RATE = IWM_RATE_MCS_15_INDEX,
	IWM_RATE_COUNT_LEGACY = IWM_LAST_NON_HT_RATE + 1,
	IWM_RATE_COUNT = IWM_LAST_HT_RATE + 1,
};

#define IWM_RATE_BIT_MSK(r) (1 << (IWM_RATE_##r##M_INDEX))

/* fw API values for legacy bit rates, both OFDM and CCK */
#define IWM_RATE_6M_PLCP 	13
#define IWM_RATE_9M_PLCP 	15
#define IWM_RATE_12M_PLCP	5
#define IWM_RATE_18M_PLCP	7
#define IWM_RATE_24M_PLCP	9
#define IWM_RATE_36M_PLCP	11
#define IWM_RATE_48M_PLCP	1
#define IWM_RATE_54M_PLCP	3
#define IWM_RATE_1M_PLCP 	10
#define IWM_RATE_2M_PLCP 	20
#define IWM_RATE_5M_PLCP 	55
#define IWM_RATE_11M_PLCP	110
#define IWM_RATE_INVM_PLCP	0xff

/*
 * rate_n_flags bit fields
 *
 * The 32-bit value has different layouts in the low 8 bites depending on the
 * format. There are three formats, HT, VHT and legacy (11abg, with subformats
 * for CCK and OFDM).
 *
 * High-throughput (HT) rate format
 *	bit 8 is 1, bit 26 is 0, bit 9 is 0 (OFDM)
 * Very High-throughput (VHT) rate format
 *	bit 8 is 0, bit 26 is 1, bit 9 is 0 (OFDM)
 * Legacy OFDM rate format for bits 7:0
 *	bit 8 is 0, bit 26 is 0, bit 9 is 0 (OFDM)
 * Legacy CCK rate format for bits 7:0:
 *	bit 8 is 0, bit 26 is 0, bit 9 is 1 (CCK)
 */

/* Bit 8: (1) HT format, (0) legacy or VHT format */
#define IWM_RATE_MCS_HT_POS 8
#define IWM_RATE_MCS_HT_MSK (1 << IWM_RATE_MCS_HT_POS)

/* Bit 9: (1) CCK, (0) OFDM.  HT (bit 8) must be "0" for this bit to be valid */
#define IWM_RATE_MCS_CCK_POS 9
#define IWM_RATE_MCS_CCK_MSK (1 << IWM_RATE_MCS_CCK_POS)

/* Bit 26: (1) VHT format, (0) legacy format in bits 8:0 */
#define IWM_RATE_MCS_VHT_POS 26
#define IWM_RATE_MCS_VHT_MSK (1 << IWM_RATE_MCS_VHT_POS)

/* Bit 31: (1) RTS (2) CTS */
#define IWM_RATE_MCS_RTS_REQUIRED_POS 30
#define IWM_RATE_MCS_RTS_REQUIRED_MSK (1 << IWM_RATE_MCS_RTS_REQUIRED_POS)

/*
 * High-throughput (HT) rate format for bits 7:0
 *
 *  2-0:  MCS rate base
 *        0)   6 Mbps
 *        1)  12 Mbps
 *        2)  18 Mbps
 *        3)  24 Mbps
 *        4)  36 Mbps
 *        5)  48 Mbps
 *        6)  54 Mbps
 *        7)  60 Mbps
 *  4-3:  0)  Single stream (SISO)
 *        1)  Dual stream (MIMO)
 *        2)  Triple stream (MIMO)
 *    5:  Value of 0x20 in bits 7:0 indicates 6 Mbps HT40 duplicate data
 *  (bits 7-6 are zero)
 *
 * Together the low 5 bits work out to the MCS index because we don't
 * support MCSes above 15/23, and 0-7 have one stream, 8-15 have two
 * streams and 16-23 have three streams. We could also support MCS 32
 * which is the duplicate 20 MHz MCS (bit 5 set, all others zero.)
 */
#define IWM_RATE_HT_MCS_RATE_CODE_MSK	0x7
#define IWM_RATE_HT_MCS_NSS_POS             3
#define IWM_RATE_HT_MCS_NSS_MSK             (3 << IWM_RATE_HT_MCS_NSS_POS)

/* Bit 10: (1) Use Green Field preamble */
#define IWM_RATE_HT_MCS_GF_POS		10
#define IWM_RATE_HT_MCS_GF_MSK		(1 << IWM_RATE_HT_MCS_GF_POS)

#define IWM_RATE_HT_MCS_INDEX_MSK		0x3f

/*
 * Very High-throughput (VHT) rate format for bits 7:0
 *
 *  3-0:  VHT MCS (0-9)
 *  5-4:  number of streams - 1:
 *        0)  Single stream (SISO)
 *        1)  Dual stream (MIMO)
 *        2)  Triple stream (MIMO)
 */

/* Bit 4-5: (0) SISO, (1) MIMO2 (2) MIMO3 */
#define IWM_RATE_VHT_MCS_RATE_CODE_MSK	0xf
#define IWM_RATE_VHT_MCS_NSS_POS		4
#define IWM_RATE_VHT_MCS_NSS_MSK		(3 << IWM_RATE_VHT_MCS_NSS_POS)

/*
 * Legacy OFDM rate format for bits 7:0
 *
 *  3-0:  0xD)   6 Mbps
 *        0xF)   9 Mbps
 *        0x5)  12 Mbps
 *        0x7)  18 Mbps
 *        0x9)  24 Mbps
 *        0xB)  36 Mbps
 *        0x1)  48 Mbps
 *        0x3)  54 Mbps
 * (bits 7-4 are 0)
 *
 * Legacy CCK rate format for bits 7:0:
 * bit 8 is 0, bit 26 is 0, bit 9 is 1 (CCK):
 *
 *  6-0:   10)  1 Mbps
 *         20)  2 Mbps
 *         55)  5.5 Mbps
 *        110)  11 Mbps
 * (bit 7 is 0)
 */
#define IWM_RATE_LEGACY_RATE_MSK 0xff


/*
 * Bit 11-12: (0) 20MHz, (1) 40MHz, (2) 80MHz, (3) 160MHz
 * 0 and 1 are valid for HT and VHT, 2 and 3 only for VHT
 */
#define IWM_RATE_MCS_CHAN_WIDTH_POS		11
#define IWM_RATE_MCS_CHAN_WIDTH_MSK		(3 << IWM_RATE_MCS_CHAN_WIDTH_POS)
#define IWM_RATE_MCS_CHAN_WIDTH_20		(0 << IWM_RATE_MCS_CHAN_WIDTH_POS)
#define IWM_RATE_MCS_CHAN_WIDTH_40		(1 << IWM_RATE_MCS_CHAN_WIDTH_POS)
#define IWM_RATE_MCS_CHAN_WIDTH_80		(2 << IWM_RATE_MCS_CHAN_WIDTH_POS)
#define IWM_RATE_MCS_CHAN_WIDTH_160		(3 << IWM_RATE_MCS_CHAN_WIDTH_POS)

/* Bit 13: (1) Short guard interval (0.4 usec), (0) normal GI (0.8 usec) */
#define IWM_RATE_MCS_SGI_POS		13
#define IWM_RATE_MCS_SGI_MSK		(1 << IWM_RATE_MCS_SGI_POS)

/* Bit 14-16: Antenna selection (1) Ant A, (2) Ant B, (4) Ant C */
#define IWM_RATE_MCS_ANT_POS		14
#define IWM_RATE_MCS_ANT_A_MSK		(1 << IWM_RATE_MCS_ANT_POS)
#define IWM_RATE_MCS_ANT_B_MSK		(2 << IWM_RATE_MCS_ANT_POS)
#define IWM_RATE_MCS_ANT_C_MSK		(4 << IWM_RATE_MCS_ANT_POS)
#define IWM_RATE_MCS_ANT_AB_MSK		(IWM_RATE_MCS_ANT_A_MSK | \
					 IWM_RATE_MCS_ANT_B_MSK)
#define IWM_RATE_MCS_ANT_ABC_MSK		(IWM_RATE_MCS_ANT_AB_MSK | \
					 IWM_RATE_MCS_ANT_C_MSK)
#define IWM_RATE_MCS_ANT_MSK		IWM_RATE_MCS_ANT_ABC_MSK
#define IWM_RATE_MCS_ANT_NUM 3

/* Bit 17-18: (0) SS, (1) SS*2 */
#define IWM_RATE_MCS_STBC_POS		17
#define IWM_RATE_MCS_STBC_MSK		(1 << IWM_RATE_MCS_STBC_POS)

/* Bit 19: (0) Beamforming is off, (1) Beamforming is on */
#define IWM_RATE_MCS_BF_POS			19
#define IWM_RATE_MCS_BF_MSK			(1 << IWM_RATE_MCS_BF_POS)

/* Bit 20: (0) ZLF is off, (1) ZLF is on */
#define IWM_RATE_MCS_ZLF_POS		20
#define IWM_RATE_MCS_ZLF_MSK		(1 << IWM_RATE_MCS_ZLF_POS)

/* Bit 24-25: (0) 20MHz (no dup), (1) 2x20MHz, (2) 4x20MHz, 3 8x20MHz */
#define IWM_RATE_MCS_DUP_POS		24
#define IWM_RATE_MCS_DUP_MSK		(3 << IWM_RATE_MCS_DUP_POS)

/* Bit 27: (1) LDPC enabled, (0) LDPC disabled */
#define IWM_RATE_MCS_LDPC_POS		27
#define IWM_RATE_MCS_LDPC_MSK		(1 << IWM_RATE_MCS_LDPC_POS)


/* Link Quality definitions */

/* # entries in rate scale table to support Tx retries */
#define  IWM_LQ_MAX_RETRY_NUM 16

/* Link quality command flags bit fields */

/* Bit 0: (0) Don't use RTS (1) Use RTS */
#define IWM_LQ_FLAG_USE_RTS_POS             0
#define IWM_LQ_FLAG_USE_RTS_MSK	        (1 << IWM_LQ_FLAG_USE_RTS_POS)

/* Bit 1-3: LQ command color. Used to match responses to LQ commands */
#define IWM_LQ_FLAG_COLOR_POS               1
#define IWM_LQ_FLAG_COLOR_MSK               (7 << IWM_LQ_FLAG_COLOR_POS)

/* Bit 4-5: Tx RTS BW Signalling
 * (0) No RTS BW signalling
 * (1) Static BW signalling
 * (2) Dynamic BW signalling
 */
#define IWM_LQ_FLAG_RTS_BW_SIG_POS          4
#define IWM_LQ_FLAG_RTS_BW_SIG_NONE         (0 << IWM_LQ_FLAG_RTS_BW_SIG_POS)
#define IWM_LQ_FLAG_RTS_BW_SIG_STATIC       (1 << IWM_LQ_FLAG_RTS_BW_SIG_POS)
#define IWM_LQ_FLAG_RTS_BW_SIG_DYNAMIC      (2 << IWM_LQ_FLAG_RTS_BW_SIG_POS)

/* Bit 6: (0) No dynamic BW selection (1) Allow dynamic BW selection
 * Dynamic BW selection allows Tx with narrower BW than requested in rates
 */
#define IWM_LQ_FLAG_DYNAMIC_BW_POS          6
#define IWM_LQ_FLAG_DYNAMIC_BW_MSK          (1 << IWM_LQ_FLAG_DYNAMIC_BW_POS)

/* Antenna flags. */
#define IWM_ANT_A	(1 << 0)
#define IWM_ANT_B	(1 << 1)
#define IWM_ANT_C	(1 << 2)
/* Shortcuts. */
#define IWM_ANT_AB	(IWM_ANT_A | IWM_ANT_B)
#define IWM_ANT_BC	(IWM_ANT_B | IWM_ANT_C)
#define IWM_ANT_ABC	(IWM_ANT_A | IWM_ANT_B | IWM_ANT_C)

/**
 * struct iwm_lq_cmd - link quality command
 * @sta_id: station to update
 * @control: not used
 * @flags: combination of IWM_LQ_FLAG_*
 * @mimo_delim: the first SISO index in rs_table, which separates MIMO
 *	and SISO rates
 * @single_stream_ant_msk: best antenna for SISO (can be dual in CDD).
 *	Should be IWM_ANT_[ABC]
 * @dual_stream_ant_msk: best antennas for MIMO, combination of IWM_ANT_[ABC]
 * @initial_rate_index: first index from rs_table per AC category
 * @agg_time_limit: aggregation max time threshold in usec/100, meaning
 *	value of 100 is one usec. Range is 100 to 8000
 * @agg_disable_start_th: try-count threshold for starting aggregation.
 *	If a frame has higher try-count, it should not be selected for
 *	starting an aggregation sequence.
 * @agg_frame_cnt_limit: max frame count in an aggregation.
 *	0: no limit
 *	1: no aggregation (one frame per aggregation)
 *	2 - 0x3f: maximal number of frames (up to 3f == 63)
 * @rs_table: array of rates for each TX try, each is rate_n_flags,
 *	meaning it is a combination of IWM_RATE_MCS_* and IWM_RATE_*_PLCP
 * @bf_params: beam forming params, currently not used
 */
struct iwm_lq_cmd {
	uint8_t sta_id;
	uint8_t reserved1;
	uint16_t control;
	/* LINK_QUAL_GENERAL_PARAMS_API_S_VER_1 */
	uint8_t flags;
	uint8_t mimo_delim;
	uint8_t single_stream_ant_msk;
	uint8_t dual_stream_ant_msk;
	uint8_t initial_rate_index[IWM_AC_NUM];
	/* LINK_QUAL_AGG_PARAMS_API_S_VER_1 */
	uint16_t agg_time_limit;
	uint8_t agg_disable_start_th;
	uint8_t agg_frame_cnt_limit;
	uint32_t reserved2;
	uint32_t rs_table[IWM_LQ_MAX_RETRY_NUM];
	uint32_t bf_params;
}; /* LINK_QUALITY_CMD_API_S_VER_1 */

/**
 * bitmasks for tx_flags in TX command
 * @IWM_TX_CMD_FLG_PROT_REQUIRE: use RTS or CTS-to-self to protect the frame
 * @IWM_TX_CMD_FLG_ACK: expect ACK from receiving station
 * @IWM_TX_CMD_FLG_STA_RATE: use RS table with initial index from the TX command.
 *	Otherwise, use rate_n_flags from the TX command
 * @IWM_TX_CMD_FLG_BA: this frame is a block ack
 * @IWM_TX_CMD_FLG_BAR: this frame is a BA request, immediate BAR is expected
 *	Must set IWM_TX_CMD_FLG_ACK with this flag.
 * @IWM_TX_CMD_FLG_TXOP_PROT: protect frame with full TXOP protection
 * @IWM_TX_CMD_FLG_VHT_NDPA: mark frame is NDPA for VHT beamformer sequence
 * @IWM_TX_CMD_FLG_HT_NDPA: mark frame is NDPA for HT beamformer sequence
 * @IWM_TX_CMD_FLG_CSI_FDBK2HOST: mark to send feedback to host (only if good CRC)
 * @IWM_TX_CMD_FLG_BT_DIS: disable BT priority for this frame
 * @IWM_TX_CMD_FLG_SEQ_CTL: set if FW should override the sequence control.
 *	Should be set for mgmt, non-QOS data, mcast, bcast and in scan command
 * @IWM_TX_CMD_FLG_MORE_FRAG: this frame is non-last MPDU
 * @IWM_TX_CMD_FLG_NEXT_FRAME: this frame includes information of the next frame
 * @IWM_TX_CMD_FLG_TSF: FW should calculate and insert TSF in the frame
 *	Should be set for beacons and probe responses
 * @IWM_TX_CMD_FLG_CALIB: activate PA TX power calibrations
 * @IWM_TX_CMD_FLG_KEEP_SEQ_CTL: if seq_ctl is set, don't increase inner seq count
 * @IWM_TX_CMD_FLG_AGG_START: allow this frame to start aggregation
 * @IWM_TX_CMD_FLG_MH_PAD: driver inserted 2 byte padding after MAC header.
 *	Should be set for 26/30 length MAC headers
 * @IWM_TX_CMD_FLG_RESP_TO_DRV: zero this if the response should go only to FW
 * @IWM_TX_CMD_FLG_CCMP_AGG: this frame uses CCMP for aggregation acceleration
 * @IWM_TX_CMD_FLG_TKIP_MIC_DONE: FW already performed TKIP MIC calculation
 * @IWM_TX_CMD_FLG_DUR: disable duration overwriting used in PS-Poll Assoc-id
 * @IWM_TX_CMD_FLG_FW_DROP: FW should mark frame to be dropped
 * @IWM_TX_CMD_FLG_EXEC_PAPD: execute PAPD
 * @IWM_TX_CMD_FLG_PAPD_TYPE: 0 for reference power, 1 for nominal power
 * @IWM_TX_CMD_FLG_HCCA_CHUNK: mark start of TSPEC chunk
 */
#define IWM_TX_CMD_FLG_PROT_REQUIRE	(1 << 0)
#define IWM_TX_CMD_FLG_ACK		(1 << 3)
#define IWM_TX_CMD_FLG_STA_RATE		(1 << 4)
#define IWM_TX_CMD_FLG_BA		(1 << 5)
#define IWM_TX_CMD_FLG_BAR		(1 << 6)
#define IWM_TX_CMD_FLG_TXOP_PROT	(1 << 7)
#define IWM_TX_CMD_FLG_VHT_NDPA		(1 << 8)
#define IWM_TX_CMD_FLG_HT_NDPA		(1 << 9)
#define IWM_TX_CMD_FLG_CSI_FDBK2HOST	(1 << 10)
#define IWM_TX_CMD_FLG_BT_DIS		(1 << 12)
#define IWM_TX_CMD_FLG_SEQ_CTL		(1 << 13)
#define IWM_TX_CMD_FLG_MORE_FRAG	(1 << 14)
#define IWM_TX_CMD_FLG_NEXT_FRAME	(1 << 15)
#define IWM_TX_CMD_FLG_TSF		(1 << 16)
#define IWM_TX_CMD_FLG_CALIB		(1 << 17)
#define IWM_TX_CMD_FLG_KEEP_SEQ_CTL	(1 << 18)
#define IWM_TX_CMD_FLG_AGG_START	(1 << 19)
#define IWM_TX_CMD_FLG_MH_PAD		(1 << 20)
#define IWM_TX_CMD_FLG_RESP_TO_DRV	(1 << 21)
#define IWM_TX_CMD_FLG_CCMP_AGG		(1 << 22)
#define IWM_TX_CMD_FLG_TKIP_MIC_DONE	(1 << 23)
#define IWM_TX_CMD_FLG_DUR		(1 << 25)
#define IWM_TX_CMD_FLG_FW_DROP		(1 << 26)
#define IWM_TX_CMD_FLG_EXEC_PAPD	(1 << 27)
#define IWM_TX_CMD_FLG_PAPD_TYPE	(1 << 28)
#define IWM_TX_CMD_FLG_HCCA_CHUNK	(1U << 31)
/* IWM_TX_FLAGS_BITS_API_S_VER_1 */

/*
 * TX command security control
 */
#define IWM_TX_CMD_SEC_WEP		0x01
#define IWM_TX_CMD_SEC_CCM		0x02
#define IWM_TX_CMD_SEC_TKIP		0x03
#define IWM_TX_CMD_SEC_EXT		0x04
#define IWM_TX_CMD_SEC_MSK		0x07
#define IWM_TX_CMD_SEC_WEP_KEY_IDX_POS	6
#define IWM_TX_CMD_SEC_WEP_KEY_IDX_MSK	0xc0
#define IWM_TX_CMD_SEC_KEY128		0x08

/* TODO: how does these values are OK with only 16 bit variable??? */
/*
 * TX command next frame info
 *
 * bits 0:2 - security control (IWM_TX_CMD_SEC_*)
 * bit 3 - immediate ACK required
 * bit 4 - rate is taken from STA table
 * bit 5 - frame belongs to BA stream
 * bit 6 - immediate BA response expected
 * bit 7 - unused
 * bits 8:15 - Station ID
 * bits 16:31 - rate
 */
#define IWM_TX_CMD_NEXT_FRAME_ACK_MSK		(0x8)
#define IWM_TX_CMD_NEXT_FRAME_STA_RATE_MSK	(0x10)
#define IWM_TX_CMD_NEXT_FRAME_BA_MSK		(0x20)
#define IWM_TX_CMD_NEXT_FRAME_IMM_BA_RSP_MSK	(0x40)
#define IWM_TX_CMD_NEXT_FRAME_FLAGS_MSK		(0xf8)
#define IWM_TX_CMD_NEXT_FRAME_STA_ID_MSK	(0xff00)
#define IWM_TX_CMD_NEXT_FRAME_STA_ID_POS	(8)
#define IWM_TX_CMD_NEXT_FRAME_RATE_MSK		(0xffff0000)
#define IWM_TX_CMD_NEXT_FRAME_RATE_POS		(16)

/*
 * TX command Frame life time in us - to be written in pm_frame_timeout
 */
#define IWM_TX_CMD_LIFE_TIME_INFINITE	0xFFFFFFFF
#define IWM_TX_CMD_LIFE_TIME_DEFAULT	2000000 /* 2000 ms*/
#define IWM_TX_CMD_LIFE_TIME_PROBE_RESP	40000 /* 40 ms */
#define IWM_TX_CMD_LIFE_TIME_EXPIRED_FRAME	0

/*
 * TID for non QoS frames - to be written in tid_tspec
 */
#define IWM_MAX_TID_COUNT	8
#define IWM_TID_NON_QOS		0
#define IWM_TID_MGMT		15

/*
 * Limits on the retransmissions - to be written in {data,rts}_retry_limit
 */
#define IWM_DEFAULT_TX_RETRY			15
#define IWM_MGMT_DFAULT_RETRY_LIMIT		3
#define IWM_RTS_DFAULT_RETRY_LIMIT		3
#define IWM_BAR_DFAULT_RETRY_LIMIT		60
#define IWM_LOW_RETRY_LIMIT			7

/**
 * enum iwm_tx_offload_assist_flags_pos -  set %iwm_tx_cmd offload_assist values
 * @TX_CMD_OFFLD_IP_HDR: offset to start of IP header (in words)
 *	from mac header end. For normal case it is 4 words for SNAP.
 *	note: tx_cmd, mac header and pad are not counted in the offset.
 *	This is used to help the offload in case there is tunneling such as
 *	IPv6 in IPv4, in such case the ip header offset should point to the
 *	inner ip header and IPv4 checksum of the external header should be
 *	calculated by driver.
 * @TX_CMD_OFFLD_L4_EN: enable TCP/UDP checksum
 * @TX_CMD_OFFLD_L3_EN: enable IP header checksum
 * @TX_CMD_OFFLD_MH_SIZE: size of the mac header in words. Includes the IV
 *	field. Doesn't include the pad.
 * @TX_CMD_OFFLD_PAD: mark 2-byte pad was inserted after the mac header for
 *	alignment
 * @TX_CMD_OFFLD_AMSDU: mark TX command is A-MSDU
 */
#define IWM_TX_CMD_OFFLD_IP_HDR		(1 << 0)
#define IWM_TX_CMD_OFFLD_L4_EN		(1 << 6)
#define IWM_TX_CMD_OFFLD_L3_EN		(1 << 7)
#define IWM_TX_CMD_OFFLD_MH_SIZE	(1 << 8)
#define IWM_TX_CMD_OFFLD_PAD		(1 << 13)
#define IWM_TX_CMD_OFFLD_AMSDU		(1 << 14)

/* TODO: complete documentation for try_cnt and btkill_cnt */
/**
 * struct iwm_tx_cmd - TX command struct to FW
 * ( IWM_TX_CMD = 0x1c )
 * @len: in bytes of the payload, see below for details
 * @offload_assist: TX offload configuration
 * @tx_flags: combination of IWM_TX_CMD_FLG_*
 * @rate_n_flags: rate for *all* Tx attempts, if IWM_TX_CMD_FLG_STA_RATE_MSK is
 *	cleared. Combination of IWM_RATE_MCS_*
 * @sta_id: index of destination station in FW station table
 * @sec_ctl: security control, IWM_TX_CMD_SEC_*
 * @initial_rate_index: index into the rate table for initial TX attempt.
 *	Applied if IWM_TX_CMD_FLG_STA_RATE_MSK is set, normally 0 for data frames.
 * @key: security key
 * @next_frame_flags: IWM_TX_CMD_SEC_* and IWM_TX_CMD_NEXT_FRAME_*
 * @life_time: frame life time (usecs??)
 * @dram_lsb_ptr: Physical address of scratch area in the command (try_cnt +
 *	btkill_cnd + reserved), first 32 bits. "0" disables usage.
 * @dram_msb_ptr: upper bits of the scratch physical address
 * @rts_retry_limit: max attempts for RTS
 * @data_retry_limit: max attempts to send the data packet
 * @tid_spec: TID/tspec
 * @pm_frame_timeout: PM TX frame timeout
 * @driver_txop: duration od EDCA TXOP, in 32-usec units. Set this if not
 *	specified by HCCA protocol
 *
 * The byte count (both len and next_frame_len) includes MAC header
 * (24/26/30/32 bytes)
 * + 2 bytes pad if 26/30 header size
 * + 8 byte IV for CCM or TKIP (not used for WEP)
 * + Data payload
 * + 8-byte MIC (not used for CCM/WEP)
 * It does not include post-MAC padding, i.e.,
 * MIC (CCM) 8 bytes, ICV (WEP/TKIP/CKIP) 4 bytes, CRC 4 bytes.
 * Range of len: 14-2342 bytes.
 *
 * After the struct fields the MAC header is placed, plus any padding,
 * and then the actual payload.
 */
struct iwm_tx_cmd {
	uint16_t len;
	uint16_t offload_assist;
	uint32_t tx_flags;
	struct {
		uint8_t try_cnt;
		uint8_t btkill_cnt;
		uint16_t reserved;
	} scratch; /* DRAM_SCRATCH_API_U_VER_1 */
	uint32_t rate_n_flags;
	uint8_t sta_id;
	uint8_t sec_ctl;
	uint8_t initial_rate_index;
	uint8_t reserved2;
	uint8_t key[16];
	uint32_t reserved3;
	uint32_t life_time;
	uint32_t dram_lsb_ptr;
	uint8_t dram_msb_ptr;
	uint8_t rts_retry_limit;
	uint8_t data_retry_limit;
	uint8_t tid_tspec;
	uint16_t pm_frame_timeout;
	uint16_t reserved4;
	uint8_t payload[0];
	struct ieee80211_frame hdr[0];
} __packed; /* IWM_TX_CMD_API_S_VER_6 */

/*
 * TX response related data
 */

/*
 * status that is returned by the fw after attempts to Tx
 * @IWM_TX_STATUS_FAIL_STA_COLOR_MISMATCH: mismatch between color of Tx cmd and
 *	STA table
 * Valid only if frame_count =1
 */
#define IWM_TX_STATUS_MSK		0x000000ff
#define IWM_TX_STATUS_SUCCESS		0x01
#define IWM_TX_STATUS_DIRECT_DONE	0x02
/* postpone TX */
#define IWM_TX_STATUS_POSTPONE_DELAY		0x40
#define IWM_TX_STATUS_POSTPONE_FEW_BYTES	0x41
#define IWM_TX_STATUS_POSTPONE_BT_PRIO		0x42
#define IWM_TX_STATUS_POSTPONE_QUIET_PERIOD	0x43
#define IWM_TX_STATUS_POSTPONE_CALC_TTAK	0x44
/* abort TX */
#define IWM_TX_STATUS_FAIL_INTERNAL_CROSSED_RETRY	0x81
#define IWM_TX_STATUS_FAIL_SHORT_LIMIT			0x82
#define IWM_TX_STATUS_FAIL_LONG_LIMIT			0x83
#define IWM_TX_STATUS_FAIL_UNDERRUN			0x84
#define IWM_TX_STATUS_FAIL_DRAIN_FLOW			0x85
#define IWM_TX_STATUS_FAIL_RFKILL_FLUSH			0x86
#define IWM_TX_STATUS_FAIL_LIFE_EXPIRE			0x87
#define IWM_TX_STATUS_FAIL_DEST_PS			0x88
#define IWM_TX_STATUS_FAIL_HOST_ABORTED			0x89
#define IWM_TX_STATUS_FAIL_BT_RETRY			0x8a
#define IWM_TX_STATUS_FAIL_STA_INVALID			0x8b
#define IWM_TX_STATUS_FAIL_FRAG_DROPPED			0x8c
#define IWM_TX_STATUS_FAIL_TID_DISABLE			0x8d
#define IWM_TX_STATUS_FAIL_FIFO_FLUSHED			0x8e
#define IWM_TX_STATUS_FAIL_SMALL_CF_POLL		0x8f
#define IWM_TX_STATUS_FAIL_FW_DROP			0x90
#define IWM_TX_STATUS_FAIL_STA_COLOR_MISMATCH		0x91
#define IWM_TX_STATUS_INTERNAL_ABORT			0x92
#define IWM_TX_MODE_MSK			0x00000f00
#define IWM_TX_MODE_NO_BURST		0x00000000
#define IWM_TX_MODE_IN_BURST_SEQ	0x00000100
#define IWM_TX_MODE_FIRST_IN_BURST	0x00000200
#define IWM_TX_QUEUE_NUM_MSK		0x0001f000
#define IWM_TX_NARROW_BW_MSK		0x00060000
#define IWM_TX_NARROW_BW_1DIV2		0x00020000
#define IWM_TX_NARROW_BW_1DIV4		0x00040000
#define IWM_TX_NARROW_BW_1DIV8		0x00060000

/*
 * TX aggregation status
 * @IWM_AGG_TX_STATE_TRY_CNT_MSK: Retry count for 1st frame in aggregation (retries
 *	occur if tx failed for this frame when it was a member of a previous
 *	aggregation block). If rate scaling is used, retry count indicates the
 *	rate table entry used for all frames in the new agg.
 */
#define IWM_AGG_TX_STATE_STATUS_MSK		0x0fff
#define IWM_AGG_TX_STATE_TRANSMITTED		0x0000
#define IWM_AGG_TX_STATE_UNDERRUN		0x0001
#define IWM_AGG_TX_STATE_BT_PRIO		0x0002
#define IWM_AGG_TX_STATE_FEW_BYTES		0x0004
#define IWM_AGG_TX_STATE_ABORT			0x0008
#define IWM_AGG_TX_STATE_LAST_SENT_TTL		0x0010
#define IWM_AGG_TX_STATE_LAST_SENT_TRY_CNT	0x0020
#define IWM_AGG_TX_STATE_LAST_SENT_BT_KILL	0x0040
#define IWM_AGG_TX_STATE_SCD_QUERY		0x0080
#define IWM_AGG_TX_STATE_TEST_BAD_CRC32		0x0100
#define IWM_AGG_TX_STATE_RESPONSE		0x01ff
#define IWM_AGG_TX_STATE_DUMP_TX		0x0200
#define IWM_AGG_TX_STATE_DELAY_TX		0x0400
#define IWM_AGG_TX_STATE_TRY_CNT_POS	12
#define IWM_AGG_TX_STATE_TRY_CNT_MSK	(0xf << IWM_AGG_TX_STATE_TRY_CNT_POS)

#define IWM_AGG_TX_STATE_LAST_SENT_MSK  (IWM_AGG_TX_STATE_LAST_SENT_TTL| \
				     IWM_AGG_TX_STATE_LAST_SENT_TRY_CNT| \
				     IWM_AGG_TX_STATE_LAST_SENT_BT_KILL)

/*
 * The mask below describes a status where we are absolutely sure that the MPDU
 * wasn't sent. For BA/Underrun we cannot be that sure. All we know that we've
 * written the bytes to the TXE, but we know nothing about what the DSP did.
 */
#define IWM_AGG_TX_STAT_FRAME_NOT_SENT (IWM_AGG_TX_STATE_FEW_BYTES | \
				    IWM_AGG_TX_STATE_ABORT | \
				    IWM_AGG_TX_STATE_SCD_QUERY)

/*
 * IWM_REPLY_TX = 0x1c (response)
 *
 * This response may be in one of two slightly different formats, indicated
 * by the frame_count field:
 *
 * 1)	No aggregation (frame_count == 1).  This reports Tx results for a single
 *	frame. Multiple attempts, at various bit rates, may have been made for
 *	this frame.
 *
 * 2)	Aggregation (frame_count > 1).  This reports Tx results for two or more
 *	frames that used block-acknowledge.  All frames were transmitted at
 *	same rate. Rate scaling may have been used if first frame in this new
 *	agg block failed in previous agg block(s).
 *
 *	Note that, for aggregation, ACK (block-ack) status is not delivered
 *	here; block-ack has not been received by the time the device records
 *	this status.
 *	This status relates to reasons the tx might have been blocked or aborted
 *	within the device, rather than whether it was received successfully by
 *	the destination station.
 */

/**
 * struct iwm_agg_tx_status - per packet TX aggregation status
 * @status: IWM_AGG_TX_STATE_*
 * @idx: Tx queue index of this frame
 * @qid: Tx queue ID of this frame
 */
struct iwm_agg_tx_status {
	uint16_t status;
	uint8_t idx;
	uint8_t qid;
} __packed;

/*
 * definitions for initial rate index field
 * bits [3:0] initial rate index
 * bits [6:4] rate table color, used for the initial rate
 * bit-7 invalid rate indication
 */
#define IWM_TX_RES_INIT_RATE_INDEX_MSK 0x0f
#define IWM_TX_RES_RATE_TABLE_COLOR_MSK 0x70
#define IWM_TX_RES_INV_RATE_INDEX_MSK 0x80

#define IWM_TX_RES_GET_TID(_ra_tid) ((_ra_tid) & 0x0f)
#define IWM_TX_RES_GET_RA(_ra_tid) ((_ra_tid) >> 4)

/**
 * struct iwm_tx_resp - notifies that fw is TXing a packet
 * ( IWM_REPLY_TX = 0x1c )
 * @frame_count: 1 no aggregation, >1 aggregation
 * @bt_kill_count: num of times blocked by bluetooth (unused for agg)
 * @failure_rts: num of failures due to unsuccessful RTS
 * @failure_frame: num failures due to no ACK (unused for agg)
 * @initial_rate: for non-agg: rate of the successful Tx. For agg: rate of the
 *	Tx of all the batch. IWM_RATE_MCS_*
 * @wireless_media_time: for non-agg: RTS + CTS + frame tx attempts time + ACK.
 *	for agg: RTS + CTS + aggregation tx time + block-ack time.
 *	in usec.
 * @pa_status: tx power info
 * @pa_integ_res_a: tx power info
 * @pa_integ_res_b: tx power info
 * @pa_integ_res_c: tx power info
 * @measurement_req_id: tx power info
 * @tfd_info: TFD information set by the FH
 * @seq_ctl: sequence control field from IEEE80211 frame header
 * @byte_cnt: byte count from the Tx cmd
 * @tlc_info: TLC rate info
 * @ra_tid: bits [3:0] = ra, bits [7:4] = tid
 * @frame_ctrl: frame control
 * @status: for non-agg:  frame status IWM_TX_STATUS_*
 *	for agg: status of 1st frame, IWM_AGG_TX_STATE_*; other frame status fields
 *	follow this one, up to frame_count.
 *
 * After the array of statuses comes the SSN of the SCD. Look at
 * %iwm_get_scd_ssn for more details.
 */
struct iwm_tx_resp {
	uint8_t frame_count;
	uint8_t bt_kill_count;
	uint8_t failure_rts;
	uint8_t failure_frame;
	uint32_t initial_rate;
	uint16_t wireless_media_time;

	uint8_t pa_status;
	uint8_t pa_integ_res_a[3];
	uint8_t pa_integ_res_b[3];
	uint8_t pa_integ_res_c[3];
	uint16_t measurement_req_id;
	uint16_t reserved;

	uint32_t tfd_info;
	uint16_t seq_ctl;
	uint16_t byte_cnt;
	uint8_t tlc_info;
	uint8_t ra_tid;
	uint16_t frame_ctrl;

	struct iwm_agg_tx_status status;
} __packed; /* IWM_TX_RSP_API_S_VER_3 */

/**
 * struct iwm_ba_notif - notifies about reception of BA
 * ( IWM_BA_NOTIF = 0xc5 )
 * @sta_addr: MAC address
 * @sta_id: Index of recipient (BA-sending) station in fw's station table
 * @tid: tid of the session
 * @seq_ctl: sequence control field from IEEE80211 frame header (the first
 * bit in @bitmap corresponds to the sequence number stored here)
 * @bitmap: the bitmap of the BA notification as seen in the air
 * @scd_flow: the tx queue this BA relates to
 * @scd_ssn: the index of the last contiguously sent packet
 * @txed: number of Txed frames in this batch
 * @txed_2_done: number of Acked frames in this batch
 * @reduced_txp: power reduced according to TPC. This is the actual value and
 *	not a copy from the LQ command. Thus, if not the first rate was used
 *	for Tx-ing then this value will be set to 0 by FW.
 * @reserved1: reserved
 */
struct iwm_ba_notif {
	uint8_t sta_addr[ETHER_ADDR_LEN];
	uint16_t reserved;

	uint8_t sta_id;
	uint8_t tid;
	uint16_t seq_ctl;
	uint64_t bitmap;
	uint16_t scd_flow;
	uint16_t scd_ssn;
	uint8_t txed;
	uint8_t txed_2_done;
	uint8_t reduced_txp;
	uint8_t reserved1;
} __packed;

/*
 * struct iwm_mac_beacon_cmd - beacon template command
 * @tx: the tx commands associated with the beacon frame
 * @template_id: currently equal to the mac context id of the corresponding
 *  mac.
 * @tim_idx: the offset of the tim IE in the beacon
 * @tim_size: the length of the tim IE
 * @frame: the template of the beacon frame
 */
struct iwm_mac_beacon_cmd {
	struct iwm_tx_cmd tx;
	uint32_t template_id;
	uint32_t tim_idx;
	uint32_t tim_size;
	struct ieee80211_frame frame[0];
} __packed;

struct iwm_beacon_notif {
	struct iwm_tx_resp beacon_notify_hdr;
	uint64_t tsf;
	uint32_t ibss_mgr_status;
} __packed;

/**
 * dump (flush) control flags
 * @IWM_DUMP_TX_FIFO_FLUSH: Dump MSDUs until the FIFO is empty
 *	and the TFD queues are empty.
 */
#define IWM_DUMP_TX_FIFO_FLUSH	(1 << 1)

/**
 * struct iwm_tx_path_flush_cmd -- queue/FIFO flush command
 * @queues_ctl: bitmap of queues to flush
 * @flush_ctl: control flags
 * @reserved: reserved
 */
struct iwm_tx_path_flush_cmd_v1 {
	uint32_t queues_ctl;
	uint16_t flush_ctl;
	uint16_t reserved;
} __packed; /* IWM_TX_PATH_FLUSH_CMD_API_S_VER_1 */

/**
 * struct iwl_tx_path_flush_cmd -- queue/FIFO flush command
 * @sta_id: station ID to flush
 * @tid_mask: TID mask to flush
 * @reserved: reserved
 */
struct iwm_tx_path_flush_cmd {
	uint32_t sta_id;
	uint16_t tid_mask;
	uint16_t reserved;
} __packed; /* TX_PATH_FLUSH_CMD_API_S_VER_2 */

/**
 * iwm_get_scd_ssn - returns the SSN of the SCD
 * @tx_resp: the Tx response from the fw (agg or non-agg)
 *
 * When the fw sends an AMPDU, it fetches the MPDUs one after the other. Since
 * it can't know that everything will go well until the end of the AMPDU, it
 * can't know in advance the number of MPDUs that will be sent in the current
 * batch. This is why it writes the agg Tx response while it fetches the MPDUs.
 * Hence, it can't know in advance what the SSN of the SCD will be at the end
 * of the batch. This is why the SSN of the SCD is written at the end of the
 * whole struct at a variable offset. This function knows how to cope with the
 * variable offset and returns the SSN of the SCD.
 */
static inline uint32_t iwm_get_scd_ssn(struct iwm_tx_resp *tx_resp)
{
	return le32_to_cpup((uint32_t *)&tx_resp->status +
			    tx_resp->frame_count) & 0xfff;
}

/**
 * struct iwm_scd_txq_cfg_cmd - New txq hw scheduler config command
 * @token:
 * @sta_id: station id
 * @tid:
 * @scd_queue: scheduler queue to config
 * @enable: 1 queue enable, 0 queue disable
 * @aggregate: 1 aggregated queue, 0 otherwise
 * @tx_fifo: %enum iwm_tx_fifo
 * @window: BA window size
 * @ssn: SSN for the BA agreement
 */
struct iwm_scd_txq_cfg_cmd {
	uint8_t token;
	uint8_t sta_id;
	uint8_t tid;
	uint8_t scd_queue;
	uint8_t enable;
	uint8_t aggregate;
	uint8_t tx_fifo;
	uint8_t window;
	uint16_t ssn;
	uint16_t reserved;
} __packed; /* SCD_QUEUE_CFG_CMD_API_S_VER_1 */

/**
 * struct iwm_scd_txq_cfg_rsp
 * @token: taken from the command
 * @sta_id: station id from the command
 * @tid: tid from the command
 * @scd_queue: scd_queue from the command
 */
struct iwm_scd_txq_cfg_rsp {
	uint8_t token;
	uint8_t sta_id;
	uint8_t tid;
	uint8_t scd_queue;
} __packed; /* SCD_QUEUE_CFG_RSP_API_S_VER_1 */


/* Scan Commands, Responses, Notifications */

/* Max number of IEs for direct SSID scans in a command */
#define IWM_PROBE_OPTION_MAX		20

/**
 * struct iwm_ssid_ie - directed scan network information element
 *
 * Up to 20 of these may appear in IWM_REPLY_SCAN_CMD,
 * selected by "type" bit field in struct iwm_scan_channel;
 * each channel may select different ssids from among the 20 entries.
 * SSID IEs get transmitted in reverse order of entry.
 */
struct iwm_ssid_ie {
	uint8_t id;
	uint8_t len;
	uint8_t ssid[IEEE80211_NWID_LEN];
} __packed; /* IWM_SCAN_DIRECT_SSID_IE_API_S_VER_1 */

/* scan offload */
#define IWM_SCAN_MAX_BLACKLIST_LEN	64
#define IWM_SCAN_SHORT_BLACKLIST_LEN	16
#define IWM_SCAN_MAX_PROFILES		11
#define IWM_SCAN_OFFLOAD_PROBE_REQ_SIZE	512

/* Default watchdog (in MS) for scheduled scan iteration */
#define IWM_SCHED_SCAN_WATCHDOG cpu_to_le16(15000)

#define IWM_GOOD_CRC_TH_DEFAULT cpu_to_le16(1)
#define IWM_CAN_ABORT_STATUS 1

#define IWM_FULL_SCAN_MULTIPLIER 5
#define IWM_FAST_SCHED_SCAN_ITERATIONS 3
#define IWM_MAX_SCHED_SCAN_PLANS 2

/**
 * iwm_scan_schedule_lmac - schedule of scan offload
 * @delay:		delay between iterations, in seconds.
 * @iterations:		num of scan iterations
 * @full_scan_mul:	number of partial scans before each full scan
 */
struct iwm_scan_schedule_lmac {
	uint16_t delay;
	uint8_t iterations;
	uint8_t full_scan_mul;
} __packed; /* SCAN_SCHEDULE_API_S */

/**
 * iwm_scan_req_tx_cmd - SCAN_REQ_TX_CMD_API_S
 * @tx_flags: combination of TX_CMD_FLG_*
 * @rate_n_flags: rate for *all* Tx attempts, if TX_CMD_FLG_STA_RATE_MSK is
 *	cleared. Combination of RATE_MCS_*
 * @sta_id: index of destination station in FW station table
 * @reserved: for alignment and future use
 */
struct iwm_scan_req_tx_cmd {
	uint32_t tx_flags;
	uint32_t rate_n_flags;
	uint8_t sta_id;
	uint8_t reserved[3];
} __packed;

#define IWM_UNIFIED_SCAN_CHANNEL_FULL		(1 << 27)
#define IWM_UNIFIED_SCAN_CHANNEL_PARTIAL	(1 << 28)

/**
 * iwm_scan_channel_cfg_lmac - SCAN_CHANNEL_CFG_S_VER2
 * @flags:		bits 1-20: directed scan to i'th ssid
 *			other bits &enum iwm_scan_channel_flags_lmac
 * @channel_number:	channel number 1-13 etc
 * @iter_count:		scan iteration on this channel
 * @iter_interval:	interval in seconds between iterations on one channel
 */
struct iwm_scan_channel_cfg_lmac {
	uint32_t flags;
	uint16_t channel_num;
	uint16_t iter_count;
	uint32_t iter_interval;
} __packed;

/*
 * iwm_scan_probe_segment - PROBE_SEGMENT_API_S_VER_1
 * @offset: offset in the data block
 * @len: length of the segment
 */
struct iwm_scan_probe_segment {
	uint16_t offset;
	uint16_t len;
} __packed;

/* iwm_scan_probe_req - PROBE_REQUEST_FRAME_API_S_VER_2
 * @mac_header: first (and common) part of the probe
 * @band_data: band specific data
 * @common_data: last (and common) part of the probe
 * @buf: raw data block
 */
struct iwm_scan_probe_req_v1 {
	struct iwm_scan_probe_segment mac_header;
	struct iwm_scan_probe_segment band_data[2];
	struct iwm_scan_probe_segment common_data;
	uint8_t buf[IWM_SCAN_OFFLOAD_PROBE_REQ_SIZE];
} __packed;

/* iwl_scan_probe_req - PROBE_REQUEST_FRAME_API_S_VER_v2
 * @mac_header: first (and common) part of the probe
 * @band_data: band specific data
 * @common_data: last (and common) part of the probe
 * @buf: raw data block
 */
struct iwm_scan_probe_req {
	struct iwm_scan_probe_segment mac_header;
	struct iwm_scan_probe_segment band_data[3];
	struct iwm_scan_probe_segment common_data;
	uint8_t buf[IWM_SCAN_OFFLOAD_PROBE_REQ_SIZE];
} __packed;


#define IWM_SCAN_CHANNEL_FLAG_EBS		(1 << 0)
#define IWM_SCAN_CHANNEL_FLAG_EBS_ACCURATE	(1 << 1)
#define IWM_SCAN_CHANNEL_FLAG_CACHE_ADD		(1 << 2)

/* iwm_scan_channel_opt - CHANNEL_OPTIMIZATION_API_S
 * @flags: enum iwm_scan_channel_flags
 * @non_ebs_ratio: defines the ratio of number of scan iterations where EBS is
 *	involved.
 *	1 - EBS is disabled.
 *	2 - every second scan will be full scan(and so on).
 */
struct iwm_scan_channel_opt {
	uint16_t flags;
	uint16_t non_ebs_ratio;
} __packed;

/**
 * LMAC scan flags
 * @IWM_LMAC_SCAN_FLAG_PASS_ALL: pass all beacons and probe responses
 *      without filtering.
 * @IWM_LMAC_SCAN_FLAG_PASSIVE: force passive scan on all channels
 * @IWM_LMAC_SCAN_FLAG_PRE_CONNECTION: single channel scan
 * @IWM_LMAC_SCAN_FLAG_ITER_COMPLETE: send iteration complete notification
 * @IWM_LMAC_SCAN_FLAG_MULTIPLE_SSIDS multiple SSID matching
 * @IWM_LMAC_SCAN_FLAG_FRAGMENTED: all passive scans will be fragmented
 * @IWM_LMAC_SCAN_FLAGS_RRM_ENABLED: insert WFA vendor-specific TPC report
 *      and DS parameter set IEs into probe requests.
 * @IWM_LMAC_SCAN_FLAG_EXTENDED_DWELL: use extended dwell time on channels
 *      1, 6 and 11.
 * @IWM_LMAC_SCAN_FLAG_MATCH: Send match found notification on matches
 */
#define IWM_LMAC_SCAN_FLAG_PASS_ALL		(1 << 0)
#define IWM_LMAC_SCAN_FLAG_PASSIVE		(1 << 1)
#define IWM_LMAC_SCAN_FLAG_PRE_CONNECTION	(1 << 2)
#define IWM_LMAC_SCAN_FLAG_ITER_COMPLETE	(1 << 3)
#define IWM_LMAC_SCAN_FLAG_MULTIPLE_SSIDS	(1 << 4)
#define IWM_LMAC_SCAN_FLAG_FRAGMENTED	(1 << 5)
#define IWM_LMAC_SCAN_FLAGS_RRM_ENABLED	(1 << 6)
#define IWM_LMAC_SCAN_FLAG_EXTENDED_DWELL	(1 << 7)
#define IWM_LMAC_SCAN_FLAG_MATCH		(1 << 9)

#define IWM_SCAN_PRIORITY_LOW		0
#define IWM_SCAN_PRIORITY_MEDIUM	1
#define IWM_SCAN_PRIORITY_HIGH		2

/**
 * iwm_scan_req_lmac - SCAN_REQUEST_CMD_API_S_VER_1
 * @reserved1: for alignment and future use
 * @channel_num: num of channels to scan
 * @active-dwell: dwell time for active channels
 * @passive-dwell: dwell time for passive channels
 * @fragmented-dwell: dwell time for fragmented passive scan
 * @extended_dwell: dwell time for channels 1, 6 and 11 (in certain cases)
 * @reserved2: for alignment and future use
 * @rx_chain_selct: PHY_RX_CHAIN_* flags
 * @scan_flags: &enum iwm_lmac_scan_flags
 * @max_out_time: max time (in TU) to be out of associated channel
 * @suspend_time: pause scan this long (TUs) when returning to service channel
 * @flags: RXON flags
 * @filter_flags: RXON filter
 * @tx_cmd: tx command for active scan; for 2GHz and for 5GHz
 * @direct_scan: list of SSIDs for directed active scan
 * @scan_prio: enum iwm_scan_priority
 * @iter_num: number of scan iterations
 * @delay: delay in seconds before first iteration
 * @schedule: two scheduling plans. The first one is finite, the second one can
 *	be infinite.
 * @channel_opt: channel optimization options, for full and partial scan
 * @data: channel configuration and probe request packet.
 */
struct iwm_scan_req_lmac {
	/* SCAN_REQUEST_FIXED_PART_API_S_VER_7 */
	uint32_t reserved1;
	uint8_t n_channels;
	uint8_t active_dwell;
	uint8_t passive_dwell;
	uint8_t fragmented_dwell;
	uint8_t extended_dwell;
	uint8_t reserved2;
	uint16_t rx_chain_select;
	uint32_t scan_flags;
	uint32_t max_out_time;
	uint32_t suspend_time;
	/* RX_ON_FLAGS_API_S_VER_1 */
	uint32_t flags;
	uint32_t filter_flags;
	struct iwm_scan_req_tx_cmd tx_cmd[2];
	struct iwm_ssid_ie direct_scan[IWM_PROBE_OPTION_MAX];
	uint32_t scan_prio;
	/* SCAN_REQ_PERIODIC_PARAMS_API_S */
	uint32_t iter_num;
	uint32_t delay;
	struct iwm_scan_schedule_lmac schedule[IWM_MAX_SCHED_SCAN_PLANS];
	struct iwm_scan_channel_opt channel_opt[2];
	uint8_t data[];
} __packed;

/**
 * iwm_scan_offload_complete - PERIODIC_SCAN_COMPLETE_NTF_API_S_VER_2
 * @last_schedule_line: last schedule line executed (fast or regular)
 * @last_schedule_iteration: last scan iteration executed before scan abort
 * @status: enum iwm_scan_offload_complete_status
 * @ebs_status: EBS success status &enum iwm_scan_ebs_status
 * @time_after_last_iter; time in seconds elapsed after last iteration
 */
struct iwm_periodic_scan_complete {
	uint8_t last_schedule_line;
	uint8_t last_schedule_iteration;
	uint8_t status;
	uint8_t ebs_status;
	uint32_t time_after_last_iter;
	uint32_t reserved;
} __packed;

/**
 * struct iwm_scan_results_notif - scan results for one channel -
 *      SCAN_RESULT_NTF_API_S_VER_3
 * @channel: which channel the results are from
 * @band: 0 for 5.2 GHz, 1 for 2.4 GHz
 * @probe_status: IWM_SCAN_PROBE_STATUS_*, indicates success of probe request
 * @num_probe_not_sent: # of request that weren't sent due to not enough time
 * @duration: duration spent in channel, in usecs
 */
struct iwm_scan_results_notif {
	uint8_t channel;
	uint8_t band;
	uint8_t probe_status;
	uint8_t num_probe_not_sent;
	uint32_t duration;
} __packed;

#define IWM_SCAN_CLIENT_SCHED_SCAN		(1 << 0)
#define IWM_SCAN_CLIENT_NETDETECT		(1 << 1)
#define IWM_SCAN_CLIENT_ASSET_TRACKING		(1 << 2)

/**
 * iwm_scan_offload_blacklist - IWM_SCAN_OFFLOAD_BLACKLIST_S
 * @ssid:		MAC address to filter out
 * @reported_rssi:	AP rssi reported to the host
 * @client_bitmap: clients ignore this entry  - enum scan_framework_client
 */
struct iwm_scan_offload_blacklist {
	uint8_t ssid[ETHER_ADDR_LEN];
	uint8_t reported_rssi;
	uint8_t client_bitmap;
} __packed;

#define IWM_NETWORK_TYPE_BSS	1
#define IWM_NETWORK_TYPE_IBSS	2
#define IWM_NETWORK_TYPE_ANY	3

#define IWM_SCAN_OFFLOAD_SELECT_2_4	0x4
#define IWM_SCAN_OFFLOAD_SELECT_5_2	0x8
#define IWM_SCAN_OFFLOAD_SELECT_ANY	0xc

/**
 * iwm_scan_offload_profile - IWM_SCAN_OFFLOAD_PROFILE_S
 * @ssid_index:		index to ssid list in fixed part
 * @unicast_cipher:	encryption algorithm to match - bitmap
 * @aut_alg:		authentication algorithm to match - bitmap
 * @network_type:	enum iwm_scan_offload_network_type
 * @band_selection:	enum iwm_scan_offload_band_selection
 * @client_bitmap:	clients waiting for match - enum scan_framework_client
 */
struct iwm_scan_offload_profile {
	uint8_t ssid_index;
	uint8_t unicast_cipher;
	uint8_t auth_alg;
	uint8_t network_type;
	uint8_t band_selection;
	uint8_t client_bitmap;
	uint8_t reserved[2];
} __packed;

/**
 * iwm_scan_offload_profile_cfg - IWM_SCAN_OFFLOAD_PROFILES_CFG_API_S_VER_1
 * @blacklist:		AP list to filter off from scan results
 * @profiles:		profiles to search for match
 * @blacklist_len:	length of blacklist
 * @num_profiles:	num of profiles in the list
 * @match_notify:	clients waiting for match found notification
 * @pass_match:		clients waiting for the results
 * @active_clients:	active clients bitmap - enum scan_framework_client
 * @any_beacon_notify:	clients waiting for match notification without match
 */
struct iwm_scan_offload_profile_cfg {
	struct iwm_scan_offload_profile profiles[IWM_SCAN_MAX_PROFILES];
	uint8_t blacklist_len;
	uint8_t num_profiles;
	uint8_t match_notify;
	uint8_t pass_match;
	uint8_t active_clients;
	uint8_t any_beacon_notify;
	uint8_t reserved[2];
} __packed;

#define IWM_SCAN_OFFLOAD_COMPLETED	1
#define IWM_SCAN_OFFLOAD_ABORTED	2

/**
 * struct iwm_lmac_scan_complete_notif - notifies end of scanning (all channels)
 *	SCAN_COMPLETE_NTF_API_S_VER_3
 * @scanned_channels: number of channels scanned (and number of valid results)
 * @status: one of SCAN_COMP_STATUS_*
 * @bt_status: BT on/off status
 * @last_channel: last channel that was scanned
 * @tsf_low: TSF timer (lower half) in usecs
 * @tsf_high: TSF timer (higher half) in usecs
 * @results: an array of scan results, only "scanned_channels" of them are valid
 */
struct iwm_lmac_scan_complete_notif {
	uint8_t scanned_channels;
	uint8_t status;
	uint8_t bt_status;
	uint8_t last_channel;
	uint32_t tsf_low;
	uint32_t tsf_high;
	struct iwm_scan_results_notif results[];
} __packed;


/* UMAC Scan API */

/* The maximum of either of these cannot exceed 8, because we use an
 * 8-bit mask (see IWM_SCAN_MASK).
 */
#define IWM_MAX_UMAC_SCANS 8
#define IWM_MAX_LMAC_SCANS 1

#define IWM_SCAN_CONFIG_FLAG_ACTIVATE			(1 << 0)
#define IWM_SCAN_CONFIG_FLAG_DEACTIVATE			(1 << 1)
#define IWM_SCAN_CONFIG_FLAG_FORBID_CHUB_REQS		(1 << 2)
#define IWM_SCAN_CONFIG_FLAG_ALLOW_CHUB_REQS		(1 << 3)
#define IWM_SCAN_CONFIG_FLAG_SET_TX_CHAINS		(1 << 8)
#define IWM_SCAN_CONFIG_FLAG_SET_RX_CHAINS		(1 << 9)
#define IWM_SCAN_CONFIG_FLAG_SET_AUX_STA_ID		(1 << 10)
#define IWM_SCAN_CONFIG_FLAG_SET_ALL_TIMES		(1 << 11)
#define IWM_SCAN_CONFIG_FLAG_SET_EFFECTIVE_TIMES	(1 << 12)
#define IWM_SCAN_CONFIG_FLAG_SET_CHANNEL_FLAGS		(1 << 13)
#define IWM_SCAN_CONFIG_FLAG_SET_LEGACY_RATES		(1 << 14)
#define IWM_SCAN_CONFIG_FLAG_SET_MAC_ADDR		(1 << 15)
#define IWM_SCAN_CONFIG_FLAG_SET_FRAGMENTED		(1 << 16)
#define IWM_SCAN_CONFIG_FLAG_CLEAR_FRAGMENTED		(1 << 17)
#define IWM_SCAN_CONFIG_FLAG_SET_CAM_MODE		(1 << 18)
#define IWM_SCAN_CONFIG_FLAG_CLEAR_CAM_MODE		(1 << 19)
#define IWM_SCAN_CONFIG_FLAG_SET_PROMISC_MODE		(1 << 20)
#define IWM_SCAN_CONFIG_FLAG_CLEAR_PROMISC_MODE		(1 << 21)

/* Bits 26-31 are for num of channels in channel_array */
#define IWM_SCAN_CONFIG_N_CHANNELS(n) ((n) << 26)

/* OFDM basic rates */
#define IWM_SCAN_CONFIG_RATE_6M		(1 << 0)
#define IWM_SCAN_CONFIG_RATE_9M		(1 << 1)
#define IWM_SCAN_CONFIG_RATE_12M	(1 << 2)
#define IWM_SCAN_CONFIG_RATE_18M	(1 << 3)
#define IWM_SCAN_CONFIG_RATE_24M	(1 << 4)
#define IWM_SCAN_CONFIG_RATE_36M	(1 << 5)
#define IWM_SCAN_CONFIG_RATE_48M	(1 << 6)
#define IWM_SCAN_CONFIG_RATE_54M	(1 << 7)
/* CCK basic rates */
#define IWM_SCAN_CONFIG_RATE_1M		(1 << 8)
#define IWM_SCAN_CONFIG_RATE_2M		(1 << 9)
#define IWM_SCAN_CONFIG_RATE_5M		(1 << 10)
#define IWM_SCAN_CONFIG_RATE_11M	(1 << 11)

/* Bits 16-27 are for supported rates */
#define IWM_SCAN_CONFIG_SUPPORTED_RATE(rate)	((rate) << 16)

#define IWM_CHANNEL_FLAG_EBS				(1 << 0)
#define IWM_CHANNEL_FLAG_ACCURATE_EBS			(1 << 1)
#define IWM_CHANNEL_FLAG_EBS_ADD			(1 << 2)
#define IWM_CHANNEL_FLAG_PRE_SCAN_PASSIVE2ACTIVE	(1 << 3)

/**
 * struct iwm_scan_config
 * @flags:			enum scan_config_flags
 * @tx_chains:			valid_tx antenna - ANT_* definitions
 * @rx_chains:			valid_rx antenna - ANT_* definitions
 * @legacy_rates:		default legacy rates - enum scan_config_rates
 * @out_of_channel_time:	default max out of serving channel time
 * @suspend_time:		default max suspend time
 * @dwell_active:		default dwell time for active scan
 * @dwell_passive:		default dwell time for passive scan
 * @dwell_fragmented:		default dwell time for fragmented scan
 * @dwell_extended:		default dwell time for channels 1, 6 and 11
 * @mac_addr:			default mac address to be used in probes
 * @bcast_sta_id:		the index of the station in the fw
 * @channel_flags:		default channel flags - enum iwm_channel_flags
 *				scan_config_channel_flag
 * @channel_array:		default supported channels
 */
struct iwm_scan_config {
	uint32_t flags;
	uint32_t tx_chains;
	uint32_t rx_chains;
	uint32_t legacy_rates;
	uint32_t out_of_channel_time;
	uint32_t suspend_time;
	uint8_t dwell_active;
	uint8_t dwell_passive;
	uint8_t dwell_fragmented;
	uint8_t dwell_extended;
	uint8_t mac_addr[ETHER_ADDR_LEN];
	uint8_t bcast_sta_id;
	uint8_t channel_flags;
	uint8_t channel_array[];
} __packed; /* SCAN_CONFIG_DB_CMD_API_S */

/**
 * iwm_umac_scan_flags
 *@IWM_UMAC_SCAN_FLAG_PREEMPTIVE: scan process triggered by this scan request
 *	can be preempted by other scan requests with higher priority.
 *	The low priority scan will be resumed when the higher priority scan is
 *	completed.
 *@IWM_UMAC_SCAN_FLAG_START_NOTIF: notification will be sent to the driver
 *	when scan starts.
 */
#define IWM_UMAC_SCAN_FLAG_PREEMPTIVE		(1 << 0)
#define IWM_UMAC_SCAN_FLAG_START_NOTIF		(1 << 1)

#define IWM_UMAC_SCAN_UID_TYPE_OFFSET		0
#define IWM_UMAC_SCAN_UID_SEQ_OFFSET		8

#define IWM_UMAC_SCAN_GEN_FLAGS_PERIODIC	(1 << 0)
#define IWM_UMAC_SCAN_GEN_FLAGS_OVER_BT		(1 << 1)
#define IWM_UMAC_SCAN_GEN_FLAGS_PASS_ALL	(1 << 2)
#define IWM_UMAC_SCAN_GEN_FLAGS_PASSIVE		(1 << 3)
#define IWM_UMAC_SCAN_GEN_FLAGS_PRE_CONNECT	(1 << 4)
#define IWM_UMAC_SCAN_GEN_FLAGS_ITER_COMPLETE	(1 << 5)
#define IWM_UMAC_SCAN_GEN_FLAGS_MULTIPLE_SSID	(1 << 6)
#define IWM_UMAC_SCAN_GEN_FLAGS_FRAGMENTED	(1 << 7)
#define IWM_UMAC_SCAN_GEN_FLAGS_RRM_ENABLED	(1 << 8)
#define IWM_UMAC_SCAN_GEN_FLAGS_MATCH		(1 << 9)
#define IWM_UMAC_SCAN_GEN_FLAGS_EXTENDED_DWELL	(1 << 10)
/* Extended dwell is obsolete when adaptive dwell is used, making this
 * bit reusable. Hence, probe request defer is used only when adaptive
 * dwell is supported. */
#define IWM_UMAC_SCAN_GEN_FLAGS_PROB_REQ_DEFER_SUPP	(1 << 10)
#define IWM_UMAC_SCAN_GEN_FLAGS_LMAC2_FRAGMENTED	(1 << 11)
#define IWM_UMAC_SCAN_GEN_FLAGS_ADAPTIVE_DWELL		(1 << 13)
#define IWM_UMAC_SCAN_GEN_FLAGS_MAX_CHNL_TIME		(1 << 14)
#define IWM_UMAC_SCAN_GEN_FLAGS_PROB_REQ_HIGH_TX_RATE	(1 << 15)

/**
 * UMAC scan general flags #2
 * @IWM_UMAC_SCAN_GEN_FLAGS2_NOTIF_PER_CHNL: Whether to send a complete
 *	notification per channel or not.
 * @IWM_UMAC_SCAN_GEN_FLAGS2_ALLOW_CHNL_REORDER: Whether to allow channel
 *	reorder optimization or not.
 */
#define IWM_UMAC_SCAN_GEN_FLAGS2_NOTIF_PER_CHNL		(1 << 0)
#define IWM_UMAC_SCAN_GEN_FLAGS2_ALLOW_CHNL_REORDER	(1 << 1)

/**
 * struct iwm_scan_channel_cfg_umac
 * @flags:		bitmap - 0-19:	directed scan to i'th ssid.
 * @channel_num:	channel number 1-13 etc.
 * @iter_count:		repetition count for the channel.
 * @iter_interval:	interval between two scan iterations on one channel.
 */
struct iwm_scan_channel_cfg_umac {
	uint32_t flags;
	uint8_t channel_num;
	uint8_t iter_count;
	uint16_t iter_interval;
} __packed; /* SCAN_CHANNEL_CFG_S_VER1 */

/**
 * struct iwm_scan_umac_schedule
 * @interval: interval in seconds between scan iterations
 * @iter_count: num of scan iterations for schedule plan, 0xff for infinite loop
 * @reserved: for alignment and future use
 */
struct iwm_scan_umac_schedule {
	uint16_t interval;
	uint8_t iter_count;
	uint8_t reserved;
} __packed; /* SCAN_SCHED_PARAM_API_S_VER_1 */

/**
 * struct iwm_scan_req_umac_tail - the rest of the UMAC scan request command
 *      parameters following channels configuration array.
 * @schedule: two scheduling plans.
 * @delay: delay in TUs before starting the first scan iteration
 * @reserved: for future use and alignment
 * @preq: probe request with IEs blocks
 * @direct_scan: list of SSIDs for directed active scan
 */
struct iwm_scan_req_umac_tail_v1 {
	/* SCAN_PERIODIC_PARAMS_API_S_VER_1 */
	struct iwm_scan_umac_schedule schedule[IWM_MAX_SCHED_SCAN_PLANS];
	uint16_t delay;
	uint16_t reserved;
	/* SCAN_PROBE_PARAMS_API_S_VER_1 */
	struct iwm_scan_probe_req_v1 preq;
	struct iwm_ssid_ie direct_scan[IWM_PROBE_OPTION_MAX];
} __packed;

/**
 * struct iwm_scan_req_umac_tail - the rest of the UMAC scan request command
 *      parameters following channels configuration array.
 * @schedule: two scheduling plans.
 * @delay: delay in TUs before starting the first scan iteration
 * @reserved: for future use and alignment
 * @preq: probe request with IEs blocks
 * @direct_scan: list of SSIDs for directed active scan
 */
struct iwm_scan_req_umac_tail_v2 {
	/* SCAN_PERIODIC_PARAMS_API_S_VER_1 */
	struct iwm_scan_umac_schedule schedule[IWM_MAX_SCHED_SCAN_PLANS];
	uint16_t delay;
	uint16_t reserved;
	/* SCAN_PROBE_PARAMS_API_S_VER_2 */
	struct iwm_scan_probe_req preq;
	struct iwm_ssid_ie direct_scan[IWM_PROBE_OPTION_MAX];
} __packed;

/**
 * struct iwm_scan_umac_chan_param
 * @flags: channel flags &enum iwl_scan_channel_flags
 * @count: num of channels in scan request
 * @reserved: for future use and alignment
 */
struct iwm_scan_umac_chan_param {
	uint8_t flags;
	uint8_t count;
	uint16_t reserved;
} __packed; /* SCAN_CHANNEL_PARAMS_API_S_VER_1 */

#define IWM_SCAN_LB_LMAC_IDX 0
#define IWM_SCAN_HB_LMAC_IDX 1

/**
 * struct iwm_scan_req_umac
 * @flags: &enum iwl_umac_scan_flags
 * @uid: scan id, &enum iwl_umac_scan_uid_offsets
 * @ooc_priority: out of channel priority - &enum iwl_scan_priority
 * @general_flags: &enum iwl_umac_scan_general_flags
 * @scan_start_mac_id: report the scan start TSF time according to this mac TSF
 * @extended_dwell: dwell time for channels 1, 6 and 11
 * @active_dwell: dwell time for active scan per LMAC
 * @passive_dwell: dwell time for passive scan per LMAC
 * @fragmented_dwell: dwell time for fragmented passive scan
 * @adwell_default_n_aps: for adaptive dwell the default number of APs
 *	per channel
 * @adwell_default_n_aps_social: for adaptive dwell the default
 *	number of APs per social (1,6,11) channel
 * @general_flags2: &enum iwl_umac_scan_general_flags2
 * @adwell_max_budget: for adaptive dwell the maximal budget of TU to be added
 *	to total scan time
 * @max_out_time: max out of serving channel time, per LMAC - for CDB there
 *	are 2 LMACs (high band and low band)
 * @suspend_time: max suspend time, per LMAC - for CDB there are 2 LMACs
 * @scan_priority: scan internal prioritization &enum iwl_scan_priority
 * @num_of_fragments: Number of fragments needed for full coverage per band.
 *	Relevant only for fragmented scan.
 * @channel: &struct iwm_scan_umac_chan_param
 * @reserved: for future use and alignment
 * @reserved3: for future use and alignment
 * @data: &struct iwm_scan_channel_cfg_umac and
 *	&struct iwm_scan_req_umac_tail
 */
struct iwm_scan_req_umac {
	uint32_t flags;
	uint32_t uid;
	uint32_t ooc_priority;
	/* SCAN_GENERAL_PARAMS_API_S_VER_1 */
	uint16_t general_flags;
	uint8_t reserved;
	uint8_t scan_start_mac_id;
	union {
		struct {
			uint8_t extended_dwell;
			uint8_t active_dwell;
			uint8_t passive_dwell;
			uint8_t fragmented_dwell;
			uint32_t max_out_time;
			uint32_t suspend_time;
			uint32_t scan_priority;
			struct iwm_scan_umac_chan_param channel;
			uint8_t data[];
		} v1; /* SCAN_REQUEST_CMD_UMAC_API_S_VER_1 */
		struct {
			uint8_t extended_dwell;
			uint8_t active_dwell;
			uint8_t passive_dwell;
			uint8_t fragmented_dwell;
			uint32_t max_out_time[2];
			uint32_t suspend_time[2];
			uint32_t scan_priority;
			struct iwm_scan_umac_chan_param channel;
			uint8_t data[];
		} v6; /* SCAN_REQUEST_CMD_UMAC_API_S_VER_6 */
		struct {
			uint8_t active_dwell;
			uint8_t passive_dwell;
			uint8_t fragmented_dwell;
			uint8_t adwell_default_n_aps;
			uint8_t adwell_default_n_aps_social;
			uint8_t reserved3;
			uint16_t adwell_max_budget;
			uint32_t max_out_time[2];
			uint32_t suspend_time[2];
			uint32_t scan_priority;
			struct iwm_scan_umac_chan_param channel;
			uint8_t data[];
		} v7; /* SCAN_REQUEST_CMD_UMAC_API_S_VER_7 */
		struct {
			uint8_t active_dwell[2];
			uint8_t reserved2;
			uint8_t adwell_default_n_aps;
			uint8_t adwell_default_n_aps_social;
			uint8_t general_flags2;
			uint16_t adwell_max_budget;
			uint32_t max_out_time[2];
			uint32_t suspend_time[2];
			uint32_t scan_priority;
			uint8_t passive_dwell[2];
			uint8_t num_of_fragments[2];
			struct iwm_scan_umac_chan_param channel;
			uint8_t data[];
		} v8; /* SCAN_REQUEST_CMD_UMAC_API_S_VER_8 */
		struct {
			uint8_t active_dwell[2];
			uint8_t adwell_default_hb_n_aps;
			uint8_t adwell_default_lb_n_aps;
			uint8_t adwell_default_n_aps_social;
			uint8_t general_flags2;
			uint16_t adwell_max_budget;
			uint32_t max_out_time[2];
			uint32_t suspend_time[2];
			uint32_t scan_priority;
			uint8_t passive_dwell[2];
			uint8_t num_of_fragments[2];
			struct iwm_scan_umac_chan_param channel;
			uint8_t data[];
		} v9; /* SCAN_REQUEST_CMD_UMAC_API_S_VER_9 */
	};
} __packed;

#define IWM_SCAN_REQ_UMAC_SIZE_V8 sizeof(struct iwm_scan_req_umac)
#define IWM_SCAN_REQ_UMAC_SIZE_V7 48
#define IWM_SCAN_REQ_UMAC_SIZE_V6 44
#define IWM_SCAN_REQ_UMAC_SIZE_V1 36

/**
 * struct iwm_umac_scan_abort
 * @uid: scan id, &enum iwm_umac_scan_uid_offsets
 * @flags: reserved
 */
struct iwm_umac_scan_abort {
	uint32_t uid;
	uint32_t flags;
} __packed; /* SCAN_ABORT_CMD_UMAC_API_S_VER_1 */

/**
 * struct iwm_umac_scan_complete
 * @uid: scan id, &enum iwm_umac_scan_uid_offsets
 * @last_schedule: last scheduling line
 * @last_iter:	last scan iteration number
 * @scan status: &enum iwm_scan_offload_complete_status
 * @ebs_status: &enum iwm_scan_ebs_status
 * @time_from_last_iter: time elapsed from last iteration
 * @reserved: for future use
 */
struct iwm_umac_scan_complete {
	uint32_t uid;
	uint8_t last_schedule;
	uint8_t last_iter;
	uint8_t status;
	uint8_t ebs_status;
	uint32_t time_from_last_iter;
	uint32_t reserved;
} __packed; /* SCAN_COMPLETE_NTF_UMAC_API_S_VER_1 */

#define IWM_SCAN_OFFLOAD_MATCHING_CHANNELS_LEN 5
/**
 * struct iwm_scan_offload_profile_match - match information
 * @bssid: matched bssid
 * @channel: channel where the match occurred
 * @energy:
 * @matching_feature:
 * @matching_channels: bitmap of channels that matched, referencing
 *	the channels passed in tue scan offload request
 */
struct iwm_scan_offload_profile_match {
	uint8_t bssid[ETHER_ADDR_LEN];
	uint16_t reserved;
	uint8_t channel;
	uint8_t energy;
	uint8_t matching_feature;
	uint8_t matching_channels[IWM_SCAN_OFFLOAD_MATCHING_CHANNELS_LEN];
} __packed; /* SCAN_OFFLOAD_PROFILE_MATCH_RESULTS_S_VER_1 */

/**
 * struct iwm_scan_offload_profiles_query - match results query response
 * @matched_profiles: bitmap of matched profiles, referencing the
 *	matches passed in the scan offload request
 * @last_scan_age: age of the last offloaded scan
 * @n_scans_done: number of offloaded scans done
 * @gp2_d0u: GP2 when D0U occurred
 * @gp2_invoked: GP2 when scan offload was invoked
 * @resume_while_scanning: not used
 * @self_recovery: obsolete
 * @reserved: reserved
 * @matches: array of match information, one for each match
 */
struct iwm_scan_offload_profiles_query {
	uint32_t matched_profiles;
	uint32_t last_scan_age;
	uint32_t n_scans_done;
	uint32_t gp2_d0u;
	uint32_t gp2_invoked;
	uint8_t resume_while_scanning;
	uint8_t self_recovery;
	uint16_t reserved;
	struct iwm_scan_offload_profile_match matches[IWM_SCAN_MAX_PROFILES];
} __packed; /* SCAN_OFFLOAD_PROFILES_QUERY_RSP_S_VER_2 */

/**
 * struct iwm_umac_scan_iter_complete_notif - notifies end of scanning iteration
 * @uid: scan id, &enum iwm_umac_scan_uid_offsets
 * @scanned_channels: number of channels scanned and number of valid elements in
 *	results array
 * @status: one of SCAN_COMP_STATUS_*
 * @bt_status: BT on/off status
 * @last_channel: last channel that was scanned
 * @tsf_low: TSF timer (lower half) in usecs
 * @tsf_high: TSF timer (higher half) in usecs
 * @results: array of scan results, only "scanned_channels" of them are valid
 */
struct iwm_umac_scan_iter_complete_notif {
	uint32_t uid;
	uint8_t scanned_channels;
	uint8_t status;
	uint8_t bt_status;
	uint8_t last_channel;
	uint32_t tsf_low;
	uint32_t tsf_high;
	struct iwm_scan_results_notif results[];
} __packed; /* SCAN_ITER_COMPLETE_NTF_UMAC_API_S_VER_1 */

#define IWM_GSCAN_START_CMD			0x0
#define IWM_GSCAN_STOP_CMD			0x1
#define IWM_GSCAN_SET_HOTLIST_CMD		0x2
#define IWM_GSCAN_RESET_HOTLIST_CMD		0x3
#define IWM_GSCAN_SET_SIGNIFICANT_CHANGE_CMD	0x4
#define IWM_GSCAN_RESET_SIGNIFICANT_CHANGE_CMD	0x5
#define IWM_GSCAN_SIGNIFICANT_CHANGE_EVENT	0xFD
#define IWM_GSCAN_HOTLIST_CHANGE_EVENT		0xFE
#define IWM_GSCAN_RESULTS_AVAILABLE_EVENT	0xFF

/* STA API */

/**
 * flags for the ADD_STA host command
 * @IWM_STA_FLG_REDUCED_TX_PWR_CTRL:
 * @IWM_STA_FLG_REDUCED_TX_PWR_DATA:
 * @IWM_STA_FLG_DISABLE_TX: set if TX should be disabled
 * @IWM_STA_FLG_PS: set if STA is in Power Save
 * @IWM_STA_FLG_INVALID: set if STA is invalid
 * @IWM_STA_FLG_DLP_EN: Direct Link Protocol is enabled
 * @IWM_STA_FLG_SET_ALL_KEYS: the current key applies to all key IDs
 * @IWM_STA_FLG_DRAIN_FLOW: drain flow
 * @IWM_STA_FLG_PAN: STA is for PAN interface
 * @IWM_STA_FLG_CLASS_AUTH:
 * @IWM_STA_FLG_CLASS_ASSOC:
 * @IWM_STA_FLG_CLASS_MIMO_PROT:
 * @IWM_STA_FLG_MAX_AGG_SIZE_MSK: maximal size for A-MPDU
 * @IWM_STA_FLG_AGG_MPDU_DENS_MSK: maximal MPDU density for Tx aggregation
 * @IWM_STA_FLG_FAT_EN_MSK: support for channel width (for Tx). This flag is
 *	initialised by driver and can be updated by fw upon reception of
 *	action frames that can change the channel width. When cleared the fw
 *	will send all the frames in 20MHz even when FAT channel is requested.
 * @IWM_STA_FLG_MIMO_EN_MSK: support for MIMO. This flag is initialised by the
 *	driver and can be updated by fw upon reception of action frames.
 * @IWM_STA_FLG_MFP_EN: Management Frame Protection
 */
#define IWM_STA_FLG_REDUCED_TX_PWR_CTRL	(1 << 3)
#define IWM_STA_FLG_REDUCED_TX_PWR_DATA	(1 << 6)

#define IWM_STA_FLG_DISABLE_TX		(1 << 4)

#define IWM_STA_FLG_PS			(1 << 8)
#define IWM_STA_FLG_DRAIN_FLOW		(1 << 12)
#define IWM_STA_FLG_PAN			(1 << 13)
#define IWM_STA_FLG_CLASS_AUTH		(1 << 14)
#define IWM_STA_FLG_CLASS_ASSOC		(1 << 15)
#define IWM_STA_FLG_RTS_MIMO_PROT	(1 << 17)

#define IWM_STA_FLG_MAX_AGG_SIZE_SHIFT	19
#define IWM_STA_FLG_MAX_AGG_SIZE_8K	(0 << IWM_STA_FLG_MAX_AGG_SIZE_SHIFT)
#define IWM_STA_FLG_MAX_AGG_SIZE_16K	(1 << IWM_STA_FLG_MAX_AGG_SIZE_SHIFT)
#define IWM_STA_FLG_MAX_AGG_SIZE_32K	(2 << IWM_STA_FLG_MAX_AGG_SIZE_SHIFT)
#define IWM_STA_FLG_MAX_AGG_SIZE_64K	(3 << IWM_STA_FLG_MAX_AGG_SIZE_SHIFT)
#define IWM_STA_FLG_MAX_AGG_SIZE_128K	(4 << IWM_STA_FLG_MAX_AGG_SIZE_SHIFT)
#define IWM_STA_FLG_MAX_AGG_SIZE_256K	(5 << IWM_STA_FLG_MAX_AGG_SIZE_SHIFT)
#define IWM_STA_FLG_MAX_AGG_SIZE_512K	(6 << IWM_STA_FLG_MAX_AGG_SIZE_SHIFT)
#define IWM_STA_FLG_MAX_AGG_SIZE_1024K	(7 << IWM_STA_FLG_MAX_AGG_SIZE_SHIFT)
#define IWM_STA_FLG_MAX_AGG_SIZE_MSK	(7 << IWM_STA_FLG_MAX_AGG_SIZE_SHIFT)

#define IWM_STA_FLG_AGG_MPDU_DENS_SHIFT	23
#define IWM_STA_FLG_AGG_MPDU_DENS_2US	(4 << IWM_STA_FLG_AGG_MPDU_DENS_SHIFT)
#define IWM_STA_FLG_AGG_MPDU_DENS_4US	(5 << IWM_STA_FLG_AGG_MPDU_DENS_SHIFT)
#define IWM_STA_FLG_AGG_MPDU_DENS_8US	(6 << IWM_STA_FLG_AGG_MPDU_DENS_SHIFT)
#define IWM_STA_FLG_AGG_MPDU_DENS_16US	(7 << IWM_STA_FLG_AGG_MPDU_DENS_SHIFT)
#define IWM_STA_FLG_AGG_MPDU_DENS_MSK	(7 << IWM_STA_FLG_AGG_MPDU_DENS_SHIFT)

#define IWM_STA_FLG_FAT_EN_20MHZ	(0 << 26)
#define IWM_STA_FLG_FAT_EN_40MHZ	(1 << 26)
#define IWM_STA_FLG_FAT_EN_80MHZ	(2 << 26)
#define IWM_STA_FLG_FAT_EN_160MHZ	(3 << 26)
#define IWM_STA_FLG_FAT_EN_MSK		(3 << 26)

#define IWM_STA_FLG_MIMO_EN_SISO	(0 << 28)
#define IWM_STA_FLG_MIMO_EN_MIMO2	(1 << 28)
#define IWM_STA_FLG_MIMO_EN_MIMO3	(2 << 28)
#define IWM_STA_FLG_MIMO_EN_MSK		(3 << 28)

/**
 * key flags for the ADD_STA host command
 * @IWM_STA_KEY_FLG_NO_ENC: no encryption
 * @IWM_STA_KEY_FLG_WEP: WEP encryption algorithm
 * @IWM_STA_KEY_FLG_CCM: CCMP encryption algorithm
 * @IWM_STA_KEY_FLG_TKIP: TKIP encryption algorithm
 * @IWM_STA_KEY_FLG_EXT: extended cipher algorithm (depends on the FW support)
 * @IWM_STA_KEY_FLG_CMAC: CMAC encryption algorithm
 * @IWM_STA_KEY_FLG_ENC_UNKNOWN: unknown encryption algorithm
 * @IWM_STA_KEY_FLG_EN_MSK: mask for encryption algorithm value
 * @IWM_STA_KEY_FLG_WEP_KEY_MAP: wep is either a group key (0 - legacy WEP) or from
 *	station info array (1 - n 1X mode)
 * @IWM_STA_KEY_FLG_KEYID_MSK: the index of the key
 * @IWM_STA_KEY_NOT_VALID: key is invalid
 * @IWM_STA_KEY_FLG_WEP_13BYTES: set for 13 bytes WEP key
 * @IWM_STA_KEY_MULTICAST: set for multicast key
 * @IWM_STA_KEY_MFP: key is used for Management Frame Protection
 */
#define IWM_STA_KEY_FLG_NO_ENC		(0 << 0)
#define IWM_STA_KEY_FLG_WEP		(1 << 0)
#define IWM_STA_KEY_FLG_CCM		(2 << 0)
#define IWM_STA_KEY_FLG_TKIP		(3 << 0)
#define IWM_STA_KEY_FLG_EXT		(4 << 0)
#define IWM_STA_KEY_FLG_CMAC		(6 << 0)
#define IWM_STA_KEY_FLG_ENC_UNKNOWN	(7 << 0)
#define IWM_STA_KEY_FLG_EN_MSK		(7 << 0)
#define IWM_STA_KEY_FLG_WEP_KEY_MAP	(1 << 3)
#define IWM_STA_KEY_FLG_KEYID_POS	8
#define IWM_STA_KEY_FLG_KEYID_MSK	(3 << IWM_STA_KEY_FLG_KEYID_POS)
#define IWM_STA_KEY_NOT_VALID		(1 << 11)
#define IWM_STA_KEY_FLG_WEP_13BYTES	(1 << 12)
#define IWM_STA_KEY_MULTICAST		(1 << 14)
#define IWM_STA_KEY_MFP			(1 << 15)

/**
 * indicate to the fw what flag are being changed
 * @IWM_STA_MODIFY_QUEUE_REMOVAL: this command removes a queue
 * @IWM_STA_MODIFY_TID_DISABLE_TX: this command modifies %tid_disable_tx
 * @IWM_STA_MODIFY_TX_RATE: unused
 * @IWM_STA_MODIFY_ADD_BA_TID: this command modifies %add_immediate_ba_tid
 * @IWM_STA_MODIFY_REMOVE_BA_TID: this command modifies %remove_immediate_ba_tid
 * @IWM_STA_MODIFY_SLEEPING_STA_TX_COUNT: this command modifies %sleep_tx_count
 * @IWM_STA_MODIFY_PROT_TH:
 * @IWM_STA_MODIFY_QUEUES: modify the queues used by this station
 */
#define IWM_STA_MODIFY_QUEUE_REMOVAL		(1 << 0)
#define IWM_STA_MODIFY_TID_DISABLE_TX		(1 << 1)
#define IWM_STA_MODIFY_TX_RATE			(1 << 2)
#define IWM_STA_MODIFY_ADD_BA_TID		(1 << 3)
#define IWM_STA_MODIFY_REMOVE_BA_TID		(1 << 4)
#define IWM_STA_MODIFY_SLEEPING_STA_TX_COUNT	(1 << 5)
#define IWM_STA_MODIFY_PROT_TH			(1 << 6)
#define IWM_STA_MODIFY_QUEUES			(1 << 7)

#define IWM_STA_MODE_MODIFY	1

/**
 * type of sleep of the station
 * @IWM_STA_SLEEP_STATE_AWAKE:
 * @IWM_STA_SLEEP_STATE_PS_POLL:
 * @IWM_STA_SLEEP_STATE_UAPSD:
 * @IWM_STA_SLEEP_STATE_MOREDATA: set more-data bit on
 *	(last) released frame
 */
#define IWM_STA_SLEEP_STATE_AWAKE	0
#define IWM_STA_SLEEP_STATE_PS_POLL	(1 << 0)
#define IWM_STA_SLEEP_STATE_UAPSD	(1 << 1)
#define IWM_STA_SLEEP_STATE_MOREDATA	(1 << 2)

/* STA ID and color bits definitions */
#define IWM_STA_ID_SEED		(0x0f)
#define IWM_STA_ID_POS		(0)
#define IWM_STA_ID_MSK		(IWM_STA_ID_SEED << IWM_STA_ID_POS)

#define IWM_STA_COLOR_SEED	(0x7)
#define IWM_STA_COLOR_POS	(4)
#define IWM_STA_COLOR_MSK	(IWM_STA_COLOR_SEED << IWM_STA_COLOR_POS)

#define IWM_STA_ID_N_COLOR_GET_COLOR(id_n_color) \
	(((id_n_color) & IWM_STA_COLOR_MSK) >> IWM_STA_COLOR_POS)
#define IWM_STA_ID_N_COLOR_GET_ID(id_n_color)    \
	(((id_n_color) & IWM_STA_ID_MSK) >> IWM_STA_ID_POS)

#define IWM_STA_KEY_MAX_NUM (16)
#define IWM_STA_KEY_IDX_INVALID (0xff)
#define IWM_STA_KEY_MAX_DATA_KEY_NUM (4)
#define IWM_MAX_GLOBAL_KEYS (4)
#define IWM_STA_KEY_LEN_WEP40 (5)
#define IWM_STA_KEY_LEN_WEP104 (13)

/**
 * struct iwm_keyinfo - key information
 * @key_flags: type %iwm_sta_key_flag
 * @tkip_rx_tsc_byte2: TSC[2] for key mix ph1 detection
 * @tkip_rx_ttak: 10-byte unicast TKIP TTAK for Rx
 * @key_offset: key offset in the fw's key table
 * @key: 16-byte unicast decryption key
 * @tx_secur_seq_cnt: initial RSC / PN needed for replay check
 * @hw_tkip_mic_rx_key: byte: MIC Rx Key - used for TKIP only
 * @hw_tkip_mic_tx_key: byte: MIC Tx Key - used for TKIP only
 */
struct iwm_keyinfo {
	uint16_t key_flags;
	uint8_t tkip_rx_tsc_byte2;
	uint8_t reserved1;
	uint16_t tkip_rx_ttak[5];
	uint8_t key_offset;
	uint8_t reserved2;
	uint8_t key[16];
	uint64_t tx_secur_seq_cnt;
	uint64_t hw_tkip_mic_rx_key;
	uint64_t hw_tkip_mic_tx_key;
} __packed;

#define IWM_ADD_STA_STATUS_MASK		0xFF
#define IWM_ADD_STA_BAID_VALID_MASK	0x8000
#define IWM_ADD_STA_BAID_MASK		0x7F00
#define IWM_ADD_STA_BAID_SHIFT		8

/**
 * struct iwm_add_sta_cmd_v7 - Add/modify a station in the fw's sta table.
 * ( REPLY_ADD_STA = 0x18 )
 * @add_modify: 1: modify existing, 0: add new station
 * @awake_acs:
 * @tid_disable_tx: is tid BIT(tid) enabled for Tx. Clear BIT(x) to enable
 *	AMPDU for tid x. Set %IWM_STA_MODIFY_TID_DISABLE_TX to change this field.
 * @mac_id_n_color: the Mac context this station belongs to
 * @addr[ETHER_ADDR_LEN]: station's MAC address
 * @sta_id: index of station in uCode's station table
 * @modify_mask: IWM_STA_MODIFY_*, selects which parameters to modify vs. leave
 *	alone. 1 - modify, 0 - don't change.
 * @station_flags: look at %iwm_sta_flags
 * @station_flags_msk: what of %station_flags have changed
 * @add_immediate_ba_tid: tid for which to add block-ack support (Rx)
 *	Set %IWM_STA_MODIFY_ADD_BA_TID to use this field, and also set
 *	add_immediate_ba_ssn.
 * @remove_immediate_ba_tid: tid for which to remove block-ack support (Rx)
 *	Set %IWM_STA_MODIFY_REMOVE_BA_TID to use this field
 * @add_immediate_ba_ssn: ssn for the Rx block-ack session. Used together with
 *	add_immediate_ba_tid.
 * @sleep_tx_count: number of packets to transmit to station even though it is
 *	asleep. Used to synchronise PS-poll and u-APSD responses while ucode
 *	keeps track of STA sleep state.
 * @sleep_state_flags: Look at %iwm_sta_sleep_flag.
 * @assoc_id: assoc_id to be sent in VHT PLCP (9-bit), for grp use 0, for AP
 *	mac-addr.
 * @beamform_flags: beam forming controls
 * @tfd_queue_msk: tfd queues used by this station
 *
 * The device contains an internal table of per-station information, with info
 * on security keys, aggregation parameters, and Tx rates for initial Tx
 * attempt and any retries (set by IWM_REPLY_TX_LINK_QUALITY_CMD).
 *
 * ADD_STA sets up the table entry for one station, either creating a new
 * entry, or modifying a pre-existing one.
 */
struct iwm_add_sta_cmd_v7 {
	uint8_t add_modify;
	uint8_t awake_acs;
	uint16_t tid_disable_tx;
	uint32_t mac_id_n_color;
	uint8_t addr[ETHER_ADDR_LEN];	/* _STA_ID_MODIFY_INFO_API_S_VER_1 */
	uint16_t reserved2;
	uint8_t sta_id;
	uint8_t modify_mask;
	uint16_t reserved3;
	uint32_t station_flags;
	uint32_t station_flags_msk;
	uint8_t add_immediate_ba_tid;
	uint8_t remove_immediate_ba_tid;
	uint16_t add_immediate_ba_ssn;
	uint16_t sleep_tx_count;
	uint16_t sleep_state_flags;
	uint16_t assoc_id;
	uint16_t beamform_flags;
	uint32_t tfd_queue_msk;
} __packed; /* ADD_STA_CMD_API_S_VER_7 */

/**
 * struct iwm_add_sta_cmd - Add/modify a station in the fw's sta table.
 * ( REPLY_ADD_STA = 0x18 )
 * @add_modify: see &enum iwl_sta_mode
 * @awake_acs: ACs to transmit data on while station is sleeping (for U-APSD)
 * @tid_disable_tx: is tid BIT(tid) enabled for Tx. Clear BIT(x) to enable
 *	AMPDU for tid x. Set %STA_MODIFY_TID_DISABLE_TX to change this field.
 * @mac_id_n_color: the Mac context this station belongs to,
 *	see &enum iwl_ctxt_id_and_color
 * @addr: station's MAC address
 * @reserved2: reserved
 * @sta_id: index of station in uCode's station table
 * @modify_mask: STA_MODIFY_*, selects which parameters to modify vs. leave
 *	alone. 1 - modify, 0 - don't change.
 * @reserved3: reserved
 * @station_flags: look at &enum iwl_sta_flags
 * @station_flags_msk: what of %station_flags have changed,
 *	also &enum iwl_sta_flags
 * @add_immediate_ba_tid: tid for which to add block-ack support (Rx)
 *	Set %STA_MODIFY_ADD_BA_TID to use this field, and also set
 *	add_immediate_ba_ssn.
 * @remove_immediate_ba_tid: tid for which to remove block-ack support (Rx)
 *	Set %STA_MODIFY_REMOVE_BA_TID to use this field
 * @add_immediate_ba_ssn: ssn for the Rx block-ack session. Used together with
 *	add_immediate_ba_tid.
 * @sleep_tx_count: number of packets to transmit to station even though it is
 *	asleep. Used to synchronise PS-poll and u-APSD responses while ucode
 *	keeps track of STA sleep state.
 * @station_type: type of this station. See &enum iwl_sta_type.
 * @sleep_state_flags: Look at &enum iwl_sta_sleep_flag.
 * @assoc_id: assoc_id to be sent in VHT PLCP (9-bit), for grp use 0, for AP
 *	mac-addr.
 * @beamform_flags: beam forming controls
 * @tfd_queue_msk: tfd queues used by this station.
 *	Obsolete for new TX API (9 and above).
 * @rx_ba_window: aggregation window size
 * @sp_length: the size of the SP in actual number of frames
 * @uapsd_acs:  4 LS bits are trigger enabled ACs, 4 MS bits are the deliver
 *	enabled ACs.
 *
 * The device contains an internal table of per-station information, with info
 * on security keys, aggregation parameters, and Tx rates for initial Tx
 * attempt and any retries (set by REPLY_TX_LINK_QUALITY_CMD).
 *
 * ADD_STA sets up the table entry for one station, either creating a new
 * entry, or modifying a pre-existing one.
 */
struct iwm_add_sta_cmd {
	uint8_t add_modify;
	uint8_t awake_acs;
	uint16_t tid_disable_tx;
	uint32_t mac_id_n_color;
	uint8_t addr[ETHER_ADDR_LEN];	/* _STA_ID_MODIFY_INFO_API_S_VER_1 */
	uint16_t reserved2;
	uint8_t sta_id;
	uint8_t modify_mask;
	uint16_t reserved3;
	uint32_t station_flags;
	uint32_t station_flags_msk;
	uint8_t add_immediate_ba_tid;
	uint8_t remove_immediate_ba_tid;
	uint16_t add_immediate_ba_ssn;
	uint16_t sleep_tx_count;
	uint8_t sleep_state_flags;
	uint8_t station_type;
	uint16_t assoc_id;
	uint16_t beamform_flags;
	uint32_t tfd_queue_msk;
	uint16_t rx_ba_window;
	uint8_t sp_length;
	uint8_t uapsd_acs;
} __packed; /* ADD_STA_CMD_API_S_VER_10 */

/**
 * FW station types
 * ( REPLY_ADD_STA = 0x18 )
 * @IWM_STA_LINK: Link station - normal RX and TX traffic.
 * @IWM_STA_GENERAL_PURPOSE: General purpose. In AP mode used for beacons
 *	and probe responses.
 * @IWM_STA_MULTICAST: multicast traffic,
 * @IWM_STA_TDLS_LINK: TDLS link station
 * @IWM_STA_AUX_ACTIVITY: auxiliary station (scan, ROC and so on).
 */
#define IWM_STA_LINK		0
#define IWM_STA_GENERAL_PURPOSE	1
#define IWM_STA_MULTICAST	2
#define IWM_STA_TDLS_LINK	3
#define IWM_STA_AUX_ACTIVITY	4

/**
 * struct iwm_add_sta_key_common - add/modify sta key common part
 * ( REPLY_ADD_STA_KEY = 0x17 )
 * @sta_id: index of station in uCode's station table
 * @key_offset: key offset in key storage
 * @key_flags: IWM_STA_KEY_FLG_* 
 * @key: key material data
 * @rx_secur_seq_cnt: RX security sequence counter for the key
 */
struct iwm_add_sta_key_common {
	uint8_t sta_id;
	uint8_t key_offset;
	uint16_t key_flags;
	uint8_t key[32];
	uint8_t rx_secur_seq_cnt[16];
} __packed;

/**
 * struct iwm_add_sta_key_cmd_v1 - add/modify sta key
 * @common: see &struct iwm_add_sta_key_common
 * @tkip_rx_tsc_byte2: TSC[2] for key mix ph1 detection
 * @reserved: reserved
 * @tkip_rx_ttak: 10-byte unicast TKIP TTAK for Rx
 */
struct iwm_add_sta_key_cmd_v1 {
	struct iwm_add_sta_key_common common;
	uint8_t tkip_rx_tsc_byte2;
	uint8_t reserved;
	uint16_t tkip_rx_ttak[5];
} __packed; /* ADD_MODIFY_STA_KEY_API_S_VER_1 */

/**
 * struct iwm_add_sta_key_cmd - add/modify sta key
 * @common: see &struct iwm_add_sta_key_common
 * @rx_mic_key: TKIP RX unicast or multicast key
 * @tx_mic_key: TKIP TX key
 * @transmit_seq_cnt: TSC, transmit packet number
 */
struct iwm_add_sta_key_cmd {
	struct iwm_add_sta_key_common common;
	uint64_t rx_mic_key;
	uint64_t tx_mic_key;
	uint64_t transmit_seq_cnt;
} __packed; /* ADD_MODIFY_STA_KEY_API_S_VER_2 */

/**
 * status in the response to ADD_STA command
 * @IWM_ADD_STA_SUCCESS: operation was executed successfully
 * @IWM_ADD_STA_STATIONS_OVERLOAD: no room left in the fw's station table
 * @IWM_ADD_STA_IMMEDIATE_BA_FAILURE: can't add Rx block ack session
 * @IWM_ADD_STA_MODIFY_NON_EXISTING_STA: driver requested to modify a station
 *	that doesn't exist.
 */
#define IWM_ADD_STA_SUCCESS			0x1
#define IWM_ADD_STA_STATIONS_OVERLOAD		0x2
#define IWM_ADD_STA_IMMEDIATE_BA_FAILURE	0x4
#define IWM_ADD_STA_MODIFY_NON_EXISTING_STA	0x8

/**
 * struct iwm_rm_sta_cmd - Add / modify a station in the fw's station table
 * ( IWM_REMOVE_STA = 0x19 )
 * @sta_id: the station id of the station to be removed
 */
struct iwm_rm_sta_cmd {
	uint8_t sta_id;
	uint8_t reserved[3];
} __packed; /* IWM_REMOVE_STA_CMD_API_S_VER_2 */

/**
 * struct iwm_mgmt_mcast_key_cmd
 * ( IWM_MGMT_MCAST_KEY = 0x1f )
 * @ctrl_flags: %iwm_sta_key_flag
 * @IGTK:
 * @K1: IGTK master key
 * @K2: IGTK sub key
 * @sta_id: station ID that support IGTK
 * @key_id:
 * @receive_seq_cnt: initial RSC/PN needed for replay check
 */
struct iwm_mgmt_mcast_key_cmd {
	uint32_t ctrl_flags;
	uint8_t IGTK[16];
	uint8_t K1[16];
	uint8_t K2[16];
	uint32_t key_id;
	uint32_t sta_id;
	uint64_t receive_seq_cnt;
} __packed; /* SEC_MGMT_MULTICAST_KEY_CMD_API_S_VER_1 */

struct iwm_wep_key {
	uint8_t key_index;
	uint8_t key_offset;
	uint16_t reserved1;
	uint8_t key_size;
	uint8_t reserved2[3];
	uint8_t key[16];
} __packed;

struct iwm_wep_key_cmd {
	uint32_t mac_id_n_color;
	uint8_t num_keys;
	uint8_t decryption_type;
	uint8_t flags;
	uint8_t reserved;
	struct iwm_wep_key wep_key[0];
} __packed; /* SEC_CURR_WEP_KEY_CMD_API_S_VER_2 */

/* 
 * BT coex
 */

#define IWM_BT_COEX_DISABLE		0x0
#define IWM_BT_COEX_NW			0x1
#define IWM_BT_COEX_BT			0x2
#define IWM_BT_COEX_WIFI		0x3
/* BT_COEX_MODES_E */

#define IWM_BT_COEX_MPLUT_ENABLED	(1 << 0)
#define IWM_BT_COEX_MPLUT_BOOST_ENABLED	(1 << 1)
#define IWM_BT_COEX_SYNC2SCO_ENABLED	(1 << 2)
#define IWM_BT_COEX_CORUN_ENABLED	(1 << 3)
#define IWM_BT_COEX_HIGH_BAND_RET	(1 << 4)
/* BT_COEX_MODULES_ENABLE_E_VER_1 */

/**
 * struct iwm_bt_coex_cmd - bt coex configuration command
 * @mode: enum %iwm_bt_coex_mode
 * @enabled_modules: enum %iwm_bt_coex_enabled_modules
 *
 * The structure is used for the BT_COEX command.
 */
struct iwm_bt_coex_cmd {
	uint32_t mode;
	uint32_t enabled_modules;
} __packed; /* BT_COEX_CMD_API_S_VER_6 */


/*
 * Location Aware Regulatory (LAR) API - MCC updates
 */

/**
 * struct iwm_mcc_update_cmd_v1 - Request the device to update geographic
 * regulatory profile according to the given MCC (Mobile Country Code).
 * The MCC is two letter-code, ascii upper case[A-Z] or '00' for world domain.
 * 'ZZ' MCC will be used to switch to NVM default profile; in this case, the
 * MCC in the cmd response will be the relevant MCC in the NVM.
 * @mcc: given mobile country code
 * @source_id: the source from where we got the MCC, see iwm_mcc_source
 * @reserved: reserved for alignment
 */
struct iwm_mcc_update_cmd_v1 {
	uint16_t mcc;
	uint8_t source_id;
	uint8_t reserved;
} __packed; /* LAR_UPDATE_MCC_CMD_API_S_VER_1 */

/**
 * struct iwm_mcc_update_cmd - Request the device to update geographic
 * regulatory profile according to the given MCC (Mobile Country Code).
 * The MCC is two letter-code, ascii upper case[A-Z] or '00' for world domain.
 * 'ZZ' MCC will be used to switch to NVM default profile; in this case, the
 * MCC in the cmd response will be the relevant MCC in the NVM.
 * @mcc: given mobile country code
 * @source_id: the source from where we got the MCC, see iwm_mcc_source
 * @reserved: reserved for alignment
 * @key: integrity key for MCC API OEM testing
 * @reserved2: reserved
 */
struct iwm_mcc_update_cmd {
	uint16_t mcc;
	uint8_t source_id;
	uint8_t reserved;
	uint32_t key;
	uint32_t reserved2[5];
} __packed; /* LAR_UPDATE_MCC_CMD_API_S_VER_2 */

/**
 * iwm_mcc_update_resp_v1  - response to MCC_UPDATE_CMD.
 * Contains the new channel control profile map, if changed, and the new MCC
 * (mobile country code).
 * The new MCC may be different than what was requested in MCC_UPDATE_CMD.
 * @status: see &enum iwm_mcc_update_status
 * @mcc: the new applied MCC
 * @cap: capabilities for all channels which matches the MCC
 * @source_id: the MCC source, see iwm_mcc_source
 * @n_channels: number of channels in @channels_data (may be 14, 39, 50 or 51
 *		channels, depending on platform)
 * @channels: channel control data map, DWORD for each channel. Only the first
 *	16bits are used.
 */
struct iwm_mcc_update_resp_v1  {
	uint32_t status;
	uint16_t mcc;
	uint8_t cap;
	uint8_t source_id;
	uint32_t n_channels;
	uint32_t channels[0];
} __packed; /* LAR_UPDATE_MCC_CMD_RESP_S_VER_1 */

/**
 * iwm_mcc_update_resp_v2 - response to MCC_UPDATE_CMD.
 * Contains the new channel control profile map, if changed, and the new MCC
 * (mobile country code).
 * The new MCC may be different than what was requested in MCC_UPDATE_CMD.
 * @status: see &enum iwm_mcc_update_status
 * @mcc: the new applied MCC
 * @cap: capabilities for all channels which matches the MCC
 * @source_id: the MCC source, see iwm_mcc_source
 * @time: time elapsed from the MCC test start (in 30 seconds TU)
 * @reserved: reserved.
 * @n_channels: number of channels in @channels_data (may be 14, 39, 50 or 51
 *		channels, depending on platform)
 * @channels: channel control data map, DWORD for each channel. Only the first
 *	16bits are used.
 */
struct iwm_mcc_update_resp_v2 {
	uint32_t status;
	uint16_t mcc;
	uint8_t cap;
	uint8_t source_id;
	uint16_t time;
	uint16_t reserved;
	uint32_t n_channels;
	uint32_t channels[0];
} __packed; /* LAR_UPDATE_MCC_CMD_RESP_S_VER_2 */

#define IWM_GEO_NO_INFO			0
#define IWM_GEO_WMM_ETSI_5GHZ_INFO	(1 << 0)

/**
 * iwm_mcc_update_resp_v3 - response to MCC_UPDATE_CMD.
 * Contains the new channel control profile map, if changed, and the new MCC
 * (mobile country code).
 * The new MCC may be different than what was requested in MCC_UPDATE_CMD.
 * @status: see &enum iwm_mcc_update_status
 * @mcc: the new applied MCC
 * @cap: capabilities for all channels which matches the MCC
 * @source_id: the MCC source, see IWM_MCC_SOURCE_*
 * @time: time elapsed from the MCC test start (in 30 seconds TU)
 * @geo_info: geographic specific profile information
 * @n_channels: number of channels in @channels_data (may be 14, 39, 50 or 51
 *		channels, depending on platform)
 * @channels: channel control data map, DWORD for each channel. Only the first
 *	16bits are used.
 */
struct iwm_mcc_update_resp_v3 {
	uint32_t status;
	uint16_t mcc;
	uint8_t cap;
	uint8_t source_id;
	uint16_t time;
	uint16_t geo_info;
	uint32_t n_channels;
	uint32_t channels[0];
} __packed; /* LAR_UPDATE_MCC_CMD_RESP_S_VER_3 */

/**
 * struct iwm_mcc_chub_notif - chub notifies of mcc change
 * (MCC_CHUB_UPDATE_CMD = 0xc9)
 * The Chub (Communication Hub, CommsHUB) is a HW component that connects to
 * the cellular and connectivity cores that gets updates of the mcc, and
 * notifies the ucode directly of any mcc change.
 * The ucode requests the driver to request the device to update geographic
 * regulatory  profile according to the given MCC (Mobile Country Code).
 * The MCC is two letter-code, ascii upper case[A-Z] or '00' for world domain.
 * 'ZZ' MCC will be used to switch to NVM default profile; in this case, the
 * MCC in the cmd response will be the relevant MCC in the NVM.
 * @mcc: given mobile country code
 * @source_id: identity of the change originator, see iwm_mcc_source
 * @reserved1: reserved for alignment
 */
struct iwm_mcc_chub_notif {
	uint16_t mcc;
	uint8_t source_id;
	uint8_t reserved1;
} __packed; /* LAR_MCC_NOTIFY_S */

#define IWM_MCC_RESP_NEW_CHAN_PROFILE			0
#define IWM_MCC_RESP_SAME_CHAN_PROFILE			1
#define IWM_MCC_RESP_INVALID				2
#define IWM_MCC_RESP_NVM_DISABLED			3
#define IWM_MCC_RESP_ILLEGAL				4
#define IWM_MCC_RESP_LOW_PRIORITY			5
#define IWM_MCC_RESP_TEST_MODE_ACTIVE			6
#define IWM_MCC_RESP_TEST_MODE_NOT_ACTIVE		7
#define IWM_MCC_RESP_TEST_MODE_DENIAL_OF_SERVICE	8

#define IWM_MCC_SOURCE_OLD_FW			0
#define IWM_MCC_SOURCE_ME			1
#define IWM_MCC_SOURCE_BIOS			2
#define IWM_MCC_SOURCE_3G_LTE_HOST		3
#define IWM_MCC_SOURCE_3G_LTE_DEVICE		4
#define IWM_MCC_SOURCE_WIFI			5
#define IWM_MCC_SOURCE_RESERVED			6
#define IWM_MCC_SOURCE_DEFAULT			7
#define IWM_MCC_SOURCE_UNINITIALIZED		8
#define IWM_MCC_SOURCE_MCC_API			9
#define IWM_MCC_SOURCE_GET_CURRENT		0x10
#define IWM_MCC_SOURCE_GETTING_MCC_TEST_MODE	0x11

/*
 * Some cherry-picked definitions
 */

#define IWM_FRAME_LIMIT	64

/*
 * From Linux commit ab02165ccec4c78162501acedeef1a768acdb811:
 *   As the firmware is slowly running out of command IDs and grouping of
 *   commands is desirable anyway, the firmware is extending the command
 *   header from 4 bytes to 8 bytes to introduce a group (in place of the
 *   former flags field, since that's always 0 on commands and thus can
 *   be easily used to distinguish between the two).
 *
 * These functions retrieve specific information from the id field in
 * the iwm_host_cmd struct which contains the command id, the group id,
 * and the version of the command.
*/
static inline uint8_t
iwm_cmd_opcode(uint32_t cmdid)
{
	return cmdid & 0xff;
}

static inline uint8_t
iwm_cmd_groupid(uint32_t cmdid)
{
	return ((cmdid & 0Xff00) >> 8);
}

static inline uint8_t
iwm_cmd_version(uint32_t cmdid)
{
	return ((cmdid & 0xff0000) >> 16);
}

static inline uint32_t
iwm_cmd_id(uint8_t opcode, uint8_t groupid, uint8_t version)
{
	return opcode + (groupid << 8) + (version << 16);
}

/* make uint16_t wide id out of uint8_t group and opcode */
#define IWM_WIDE_ID(grp, opcode) ((grp << 8) | opcode)

struct iwm_cmd_header {
	uint8_t code;
	uint8_t flags;
	uint8_t idx;
	uint8_t qid;
} __packed;

struct iwm_cmd_header_wide {
	uint8_t opcode;
	uint8_t group_id;
	uint8_t idx;
	uint8_t qid;
	uint16_t length;
	uint8_t reserved;
	uint8_t version;
} __packed;

#define IWM_POWER_SCHEME_CAM	1
#define IWM_POWER_SCHEME_BPS	2
#define IWM_POWER_SCHEME_LP	3

#define IWM_DEF_CMD_PAYLOAD_SIZE 320
#define IWM_MAX_CMD_PAYLOAD_SIZE ((4096 - 4) - sizeof(struct iwm_cmd_header))
#define IWM_CMD_FAILED_MSK 0x40

/**
 * struct iwm_device_cmd
 *
 * For allocation of the command and tx queues, this establishes the overall
 * size of the largest command we send to uCode, except for commands that
 * aren't fully copied and use other TFD space.
 */
struct iwm_device_cmd {
	union {
		struct {
			struct iwm_cmd_header hdr;
			uint8_t data[IWM_DEF_CMD_PAYLOAD_SIZE];
		};
		struct {
			struct iwm_cmd_header_wide hdr_wide;
			uint8_t data_wide[IWM_DEF_CMD_PAYLOAD_SIZE -
					sizeof(struct iwm_cmd_header_wide) +
					sizeof(struct iwm_cmd_header)];
		};
	};
} __packed;

struct iwm_rx_packet {
	/*
	 * The first 4 bytes of the RX frame header contain both the RX frame
	 * size and some flags.
	 * Bit fields:
	 * 31:    flag flush RB request
	 * 30:    flag ignore TC (terminal counter) request
	 * 29:    flag fast IRQ request
	 * 28-26: Reserved
	 * 25:    Offload enabled
	 * 24:    RPF enabled
	 * 23:    RSS enabled
	 * 22:    Checksum enabled
	 * 21-16: RX queue
	 * 15-14: Reserved
	 * 13-00: RX frame size
	 */
	uint32_t len_n_flags;
	struct iwm_cmd_header hdr;
	uint8_t data[];
} __packed;

#define	IWM_FH_RSCSR_FRAME_SIZE_MSK	0x00003fff
#define	IWM_FH_RSCSR_FRAME_INVALID	0x55550000
#define	IWM_FH_RSCSR_FRAME_ALIGN	0x40
#define	IWM_FH_RSCSR_RPA_EN		(1 << 25)
#define	IWM_FH_RSCSR_RADA_EN		(1 << 26)
#define	IWM_FH_RSCSR_RXQ_POS		16
#define	IWM_FH_RSCSR_RXQ_MASK		0x3F0000

static uint32_t
iwm_rx_packet_len(const struct iwm_rx_packet *pkt)
{

	return le32toh(pkt->len_n_flags) & IWM_FH_RSCSR_FRAME_SIZE_MSK;
}

static uint32_t
iwm_rx_packet_payload_len(const struct iwm_rx_packet *pkt)
{

	return iwm_rx_packet_len(pkt) - sizeof(pkt->hdr);
}


#define IWM_MIN_DBM	-100
#define IWM_MAX_DBM	-33	/* realistic guess */

#define IWM_READ(sc, reg)						\
	bus_space_read_4((sc)->sc_st, (sc)->sc_sh, (reg))

#define IWM_WRITE(sc, reg, val)						\
	bus_space_write_4((sc)->sc_st, (sc)->sc_sh, (reg), (val))

#define IWM_WRITE_1(sc, reg, val)					\
	bus_space_write_1((sc)->sc_st, (sc)->sc_sh, (reg), (val))

#define IWM_SETBITS(sc, reg, mask)					\
	IWM_WRITE(sc, reg, IWM_READ(sc, reg) | (mask))

#define IWM_CLRBITS(sc, reg, mask)					\
	IWM_WRITE(sc, reg, IWM_READ(sc, reg) & ~(mask))

#define IWM_BARRIER_WRITE(sc)						\
	bus_space_barrier((sc)->sc_st, (sc)->sc_sh, 0, (sc)->sc_sz,	\
	    BUS_SPACE_BARRIER_WRITE)

#define IWM_BARRIER_READ_WRITE(sc)					\
	bus_space_barrier((sc)->sc_st, (sc)->sc_sh, 0, (sc)->sc_sz,	\
	    BUS_SPACE_BARRIER_READ | BUS_SPACE_BARRIER_WRITE)
