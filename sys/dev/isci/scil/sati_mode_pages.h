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
/**
 * @file
 * @brief This file contains the mode page constants and members that are
 *        supported by this translation implementation.
 */

#include <dev/isci/scil/sati_types.h>

// These values represent the mode page (not including header and block
// descriptor).  The page length fields in the mode sense data are equivalent
// to the constant values below less 2.  The minus 2 is due to not including
// the page code byte (byte 0) and the page length byte (byte 1).
#define SCSI_MODE_PAGE_01_LENGTH   0x0C
#define SCSI_MODE_PAGE_02_LENGTH   0x10
#define SCSI_MODE_PAGE_08_LENGTH   0x14
#define SCSI_MODE_PAGE_0A_LENGTH   0x0C
#define SCSI_MODE_PAGE_19_LENGTH   0x8
#define SCSI_MODE_PAGE_1A_LENGTH   0x0C
#define SCSI_MODE_PAGE_1C_LENGTH   0x0C
#define SCSI_MODE_PAGE_3F_LENGTH   SCSI_MODE_PAGE_08_LENGTH    \
                                   + SCSI_MODE_PAGE_1C_LENGTH  \

#define SATI_MODE_PAGE_UNSUPPORTED_INDEX 0xFFFF

#define SAT_SUPPORTED_MODE_PAGES_LENGTH sizeof(sat_supported_mode_pages)/sizeof(U8)

typedef enum _RETURN_PAGE{
   CHANGEABLE_PAGE,
   DEFAULT_PAGE
}RETURN_PAGE;


/**
 * @struct SCSI_MODE_SELECT_MODE_PARAMETER_HEADER_6
 *
 * @brief This structure contains mode parameter header fields for 6 byte
 *        mode select command.
 */
typedef  struct SCSI_MODE_SELECT_MODE_PARAMETER_HEADER_6
{
   U8 mode_data_length;
   U8 medium_type; //Should be 0
   U8 device_specific_parameter;
   U8 block_descriptor_length;

}SCSI_MODE_SELECT_MODE_PARAMETER_HEADER_6_T;

/**
 * @struct MODE_PARAMETER_HEADER_10
 *
 * @brief This structure contains mode parameter header fields for 10 byte
 *        mode select command.
 */
typedef struct SCSI_MODE_SELECT_MODE_PARAMETER_HEADER_10
{
   U8 mode_data_length[2];
   U8 medium_type; //Should be 0
   U8 device_specific_parameter;
   U8 long_lba;
   U8 reserve;
   U8 block_descriptor_length[2];

}SCSI_MODE_SELECT_MODE_PARAMETER_HEADER_10_T;

/**
 * @struct MODE_PARAMETER_BLOCK_DESCRIPTOR
 *
 * @brief This structure contains mode parameter block descriptor fields.
 */
typedef struct SCSI_MODE_SELECT_MODE_PARAMETER_BLOCK_DESCRIPTOR
{
   U8 density_code;
   U8 number_of_blocks[3];
   U8 reserved;
   U8 block_length[3];

}SCSI_MODE_SELECT_MODE_PARAMETER_BLOCK_DESCRIPTOR_T;

U16 sati_mode_page_get_page_index(
   U8  page_code
);

U8 * sati_mode_page_get_mode_page(
   U8 page_code,
   RETURN_PAGE page
);

extern U8 * sat_changeable_mode_pages[];
extern U8 * sat_default_mode_pages[];
extern U16  sat_mode_page_sizes[];

