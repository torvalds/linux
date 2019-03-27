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
 * Copyright(c) 2008 - 2010 Intel Corporation. All rights reserved.
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
#ifndef _SATA_H_
#define _SATA_H_

#include <dev/isci/types.h>

/**
 * @file
 *
 * @brief This file defines all of the SATA releated constants, enumerations,
 *        and types. Please note that this file does not necessarily contain
 *        an exhaustive list of all contants and commands.
 */

/**
 * @name SATA FIS Types
 *
 * These constants depict the various SATA FIS types devined in the serial ATA
 * specification.
 */
/*@{*/
#define SATA_FIS_TYPE_REGH2D          0x27
#define SATA_FIS_TYPE_REGD2H          0x34
#define SATA_FIS_TYPE_SETDEVBITS      0xA1
#define SATA_FIS_TYPE_DMA_ACTIVATE    0x39
#define SATA_FIS_TYPE_DMA_SETUP       0x41
#define SATA_FIS_TYPE_BIST_ACTIVATE   0x58
#define SATA_FIS_TYPE_PIO_SETUP       0x5F
#define SATA_FIS_TYPE_DATA            0x46
/*@}*/

#define SATA_REGISTER_FIS_SIZE 0x20

/**
 * @struct  SATA_FIS_HEADER
 *
 * @brief This is the common definition for a SATA FIS Header word.  A
 *        different header word is defined for any FIS type that does not use
 *        the standard header.
 */
typedef struct SATA_FIS_HEADER
{
   U32 fis_type         :8;   // word 0
   U32 pm_port          :4;
   U32 reserved         :1;
   U32 direction_flag   :1;   // direction
   U32 interrupt_flag   :1;
   U32 command_flag     :1;   // command, auto_activate, or notification
   U32 status           :8;
   U32 error            :8;
} SATA_FIS_HEADER_T;


/**
 * @struct SATA_FIS_REG_H2D
 *
 * @brief This is the definition for a SATA Host to Device Register FIS.
 */
typedef struct SATA_FIS_REG_H2D
{
   U32 fis_type         :8;     // word 0
   U32 pm_port          :4;
   U32 reserved0        :3;
   U32 command_flag     :1;
   U32 command          :8;
   U32 features         :8;
   U32 lba_low          :8;     // word 1
   U32 lba_mid          :8;
   U32 lba_high         :8;
   U32 device           :8;
   U32 lba_low_exp      :8;     // word 2
   U32 lba_mid_exp      :8;
   U32 lba_high_exp     :8;
   U32 features_exp     :8;
   U32 sector_count     :8;     // word 3
   U32 sector_count_exp :8;
   U32 reserved1        :8;
   U32 control          :8;
   U32 reserved2;               // word 4
} SATA_FIS_REG_H2D_T;

/**
 * @struct SATA_FIS_REG_D2H
 *
 * @brief SATA Device To Host FIS
 */
typedef struct SATA_FIS_REG_D2H
{
   U32 fis_type   :8;         // word 0
   U32 pm_port    :4;
   U32 reserved0  :2;
   U32 irq        :1;
   U32 reserved1  :1;
   U32 status     :8;
   U32 error      :8;
   U8 lba_low;               // word 1
   U8 lba_mid;
   U8 lba_high;
   U8 device;
   U8 lba_low_exp;           // word 2
   U8 lba_mid_exp;
   U8 lba_high_exp;
   U8 reserved;
   U8 sector_count;          // word 3
   U8 sector_count_exp;
   U16 reserved2;
   U32 reserved3;
} SATA_FIS_REG_D2H_T;

/**
 *  Status field bit definitions
 */
#define SATA_FIS_STATUS_DEVBITS_MASK  (0x77)

/**
 * @struct SATA_FIS_SET_DEV_BITS
 *
 * @brief SATA Set Device Bits FIS
 */
typedef struct SATA_FIS_SET_DEV_BITS
{
   U32 fis_type      :8;   // word 0
   U32 pm_port       :4;
   U32 reserved0     :2;
   U32 irq           :1;
   U32 notification  :1;
   U32 status_low    :4;
   U32 status_high   :4;
   U32 error         :8;
   U32 s_active;           // word 1
} SATA_FIS_SET_DEV_BITS_T;

/**
 * @struct SATA_FIS_DMA_ACTIVATE
 *
 * @brief SATA DMA Activate FIS
 */
typedef struct SATA_FIS_DMA_ACTIVATE
{
   U32 fis_type      :8;   // word 0
   U32 pm_port       :4;
   U32 reserved0     :24;
} SATA_FIS_DMA_ACTIVATE_T;

/**
 * The lower 5 bits in the DMA Buffer ID Low field of the DMA Setup
 * are used to communicate the command tag.
 */
#define SATA_DMA_SETUP_TAG_ENABLE      0x1F

#define SATA_DMA_SETUP_AUTO_ACT_ENABLE 0x80

/**
 * @struct SATA_FIS_DMA_SETUP
 *
 * @brief SATA DMA Setup FIS
 */
typedef struct SATA_FIS_DMA_SETUP
{
   U32 fis_type            :8;   // word 0
   U32 pm_port             :4;
   U32 reserved_00         :1;
   U32 direction           :1;
   U32 irq                 :1;
   U32 auto_activate       :1;
   U32 reserved_01         :16;
   U32 dma_buffer_id_low;        // word 1
   U32 dma_buffer_id_high;       // word 2
   U32 reserved0;                // word 3
   U32 dma_buffer_offset;        // word 4
   U32 dma_transfer_count;       // word 5
   U32 reserved1;                // word 6
} SATA_FIS_DMA_SETUP_T;

/**
 *  @struct SATA_FIS_BIST_ACTIVATE
 *
 *  @brief SATA BIST Activate FIS
 */
typedef struct SATA_FIS_BIST_ACTIVATE
{
   U32 fis_type               :8;   // word 0
   U32 reserved0              :8;
   U32 pattern_definition     :8;
   U32 reserved1              :8;
   U32 data1;                       // word 1
   U32 data2;                       // word 1
} SATA_FIS_BIST_ACTIVATE_T;

/*
 *  SATA PIO Setup FIS
 */
typedef struct SATA_FIS_PIO_SETUP
{
   U32 fis_type         :8;   // word 0
   U32 pm_port          :4;
   U32 reserved_00      :1;
   U32 direction        :1;
   U32 irq              :1;
   U32 reserved_01      :1;
   U32 status           :8;
   U32 error            :8;
   U32 lba_low          :8;   // word 1
   U32 lba_mid          :8;
   U32 lba_high         :8;
   U32 device           :8;
   U32 lba_low_exp      :8;   // word 2
   U32 lba_mid_exp      :8;
   U32 lba_high_exp     :8;
   U32 reserved         :8;
   U32 sector_count     :8;   // word 3
   U32 sector_count_exp :8;
   U32 reserved1        :8;
   U32 ending_status    :8;
   U32 transfter_count  :16;  // word 4
   U32 reserved3        :16;
} SATA_FIS_PIO_SETUP_T;

/**
 * @struct SATA_FIS_DATA
 *
 * @brief SATA Data FIS
 */
typedef struct SATA_FIS_DATA
{
   U32 fis_type      :8;   // word 0
   U32 pm_port       :4;
   U32 reserved0     :24;
   U8  data[4];            // word 1
} SATA_FIS_DATA_T;

#endif // _SATA_H_
