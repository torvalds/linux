/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2006 Bernd Walter.  All rights reserved.
 * Copyright (c) 2006 M. Warner Losh.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * Portions of this software may have been developed with reference to
 * the SD Simplified Specification.  The following disclaimer may apply:
 *
 * The following conditions apply to the release of the simplified
 * specification ("Simplified Specification") by the SD Card Association and
 * the SD Group. The Simplified Specification is a subset of the complete SD
 * Specification which is owned by the SD Card Association and the SD
 * Group. This Simplified Specification is provided on a non-confidential
 * basis subject to the disclaimers below. Any implementation of the
 * Simplified Specification may require a license from the SD Card
 * Association, SD Group, SD-3C LLC or other third parties.
 *
 * Disclaimers:
 *
 * The information contained in the Simplified Specification is presented only
 * as a standard specification for SD Cards and SD Host/Ancillary products and
 * is provided "AS-IS" without any representations or warranties of any
 * kind. No responsibility is assumed by the SD Group, SD-3C LLC or the SD
 * Card Association for any damages, any infringements of patents or other
 * right of the SD Group, SD-3C LLC, the SD Card Association or any third
 * parties, which may result from its use. No license is granted by
 * implication, estoppel or otherwise under any patent or other rights of the
 * SD Group, SD-3C LLC, the SD Card Association or any third party. Nothing
 * herein shall be construed as an obligation by the SD Group, the SD-3C LLC
 * or the SD Card Association to disclose or distribute any technical
 * information, know-how or other confidential information to any third party.
 *
 * $FreeBSD$
 */

#ifndef DEV_MMC_MMCVAR_H
#define DEV_MMC_MMCVAR_H

enum mmc_device_ivars {
    MMC_IVAR_SPEC_VERS,
    MMC_IVAR_DSR_IMP,
    MMC_IVAR_MEDIA_SIZE,
    MMC_IVAR_RCA,
    MMC_IVAR_SECTOR_SIZE,
    MMC_IVAR_TRAN_SPEED,
    MMC_IVAR_READ_ONLY,
    MMC_IVAR_HIGH_CAP,
    MMC_IVAR_CARD_TYPE,
    MMC_IVAR_BUS_WIDTH,
    MMC_IVAR_ERASE_SECTOR,
    MMC_IVAR_MAX_DATA,
    MMC_IVAR_CMD6_TIMEOUT,
    MMC_IVAR_QUIRKS,
    MMC_IVAR_CARD_ID_STRING,
    MMC_IVAR_CARD_SN_STRING,
};

/*
 * Simplified accessors for mmc devices
 */
#define MMC_ACCESSOR(var, ivar, type)					\
	__BUS_ACCESSOR(mmc, var, MMC, ivar, type)

MMC_ACCESSOR(spec_vers, SPEC_VERS, uint8_t)
MMC_ACCESSOR(dsr_imp, DSR_IMP, int)
MMC_ACCESSOR(media_size, MEDIA_SIZE, long)
MMC_ACCESSOR(rca, RCA, int)
MMC_ACCESSOR(sector_size, SECTOR_SIZE, int)
MMC_ACCESSOR(tran_speed, TRAN_SPEED, int)
MMC_ACCESSOR(read_only, READ_ONLY, int)
MMC_ACCESSOR(high_cap, HIGH_CAP, int)
MMC_ACCESSOR(card_type, CARD_TYPE, int)
MMC_ACCESSOR(bus_width, BUS_WIDTH, int)
MMC_ACCESSOR(erase_sector, ERASE_SECTOR, int)
MMC_ACCESSOR(max_data, MAX_DATA, int)
MMC_ACCESSOR(cmd6_timeout, CMD6_TIMEOUT, u_int)
MMC_ACCESSOR(quirks, QUIRKS, u_int)
MMC_ACCESSOR(card_id_string, CARD_ID_STRING, const char *)
MMC_ACCESSOR(card_sn_string, CARD_SN_STRING, const char *)

#endif /* DEV_MMC_MMCVAR_H */
