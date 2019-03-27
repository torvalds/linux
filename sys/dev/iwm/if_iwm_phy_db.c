/*	$OpenBSD: if_iwm.c,v 1.39 2015/03/23 00:35:19 jsg Exp $	*/

/*
 * Copyright (c) 2014 genua mbh <info@genua.de>
 * Copyright (c) 2014 Fixup Software Ltd.
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

/*-
 * Based on BSD-licensed source modules in the Linux iwlwifi driver,
 * which were used as the reference documentation for this implementation.
 *
 * Driver version we are currently based off of is
 * Linux 3.14.3 (tag id a2df521e42b1d9a23f620ac79dbfe8655a8391dd)
 *
 ***********************************************************************
 *
 * This file is provided under a dual BSD/GPLv2 license.  When using or
 * redistributing this file, you may do so under either license.
 *
 * GPL LICENSE SUMMARY
 *
 * Copyright(c) 2007 - 2013 Intel Corporation. All rights reserved.
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
 *
 * BSD LICENSE
 *
 * Copyright(c) 2005 - 2013 Intel Corporation. All rights reserved.
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
 */

/*-
 * Copyright (c) 2007-2010 Damien Bergamini <damien.bergamini@free.fr>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */
#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include "opt_wlan.h"
#include "opt_iwm.h"

#include <sys/param.h>
#include <sys/bus.h>
#include <sys/conf.h>
#include <sys/endian.h>
#include <sys/firmware.h>
#include <sys/kernel.h>
#include <sys/malloc.h>
#include <sys/mbuf.h>
#include <sys/mutex.h>
#include <sys/module.h>
#include <sys/proc.h>
#include <sys/rman.h>
#include <sys/socket.h>
#include <sys/sockio.h>
#include <sys/sysctl.h>
#include <sys/linker.h>

#include <machine/bus.h>
#include <machine/endian.h>
#include <machine/resource.h>

#include <dev/pci/pcivar.h>
#include <dev/pci/pcireg.h>

#include <net/bpf.h>

#include <net/if.h>
#include <net/if_var.h>
#include <net/if_arp.h>
#include <net/if_dl.h>
#include <net/if_media.h>
#include <net/if_types.h>

#include <netinet/in.h>
#include <netinet/in_systm.h>
#include <netinet/if_ether.h>
#include <netinet/ip.h>

#include <net80211/ieee80211_var.h>
#include <net80211/ieee80211_regdomain.h>
#include <net80211/ieee80211_ratectl.h>
#include <net80211/ieee80211_radiotap.h>

#include "if_iwmreg.h"
#include "if_iwmvar.h"
#include "if_iwm_debug.h"
#include "if_iwm_util.h"
#include "if_iwm_phy_db.h"

#define CHANNEL_NUM_SIZE	4	/* num of channels in calib_ch size */

struct iwm_phy_db_entry {
	uint16_t	size;
	uint8_t		*data;
};

/**
 * struct iwm_phy_db - stores phy configuration and calibration data.
 *
 * @cfg: phy configuration.
 * @calib_nch: non channel specific calibration data.
 * @calib_ch: channel specific calibration data.
 * @n_group_papd: number of entries in papd channel group.
 * @calib_ch_group_papd: calibration data related to papd channel group.
 * @n_group_txp: number of entries in tx power channel group.
 * @calib_ch_group_txp: calibration data related to tx power chanel group.
 */
struct iwm_phy_db {
	struct iwm_phy_db_entry cfg;
	struct iwm_phy_db_entry calib_nch;
	int n_group_papd;
	struct iwm_phy_db_entry *calib_ch_group_papd;
	int n_group_txp;
	struct iwm_phy_db_entry *calib_ch_group_txp;

	struct iwm_softc *sc;
};

enum iwm_phy_db_section_type {
	IWM_PHY_DB_CFG = 1,
	IWM_PHY_DB_CALIB_NCH,
	IWM_PHY_DB_UNUSED,
	IWM_PHY_DB_CALIB_CHG_PAPD,
	IWM_PHY_DB_CALIB_CHG_TXP,
	IWM_PHY_DB_MAX
};

#define PHY_DB_CMD 0x6c

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

struct iwm_phy_db *
iwm_phy_db_init(struct iwm_softc *sc)
{
	struct iwm_phy_db *phy_db = malloc(sizeof(struct iwm_phy_db),
	    M_DEVBUF, M_NOWAIT | M_ZERO);

	if (!phy_db)
		return phy_db;

	phy_db->sc = sc;

	phy_db->n_group_txp = -1;
	phy_db->n_group_papd = -1;

	/* TODO: add default values of the phy db. */
	return phy_db;
}

/*
 * get phy db section: returns a pointer to a phy db section specified by
 * type and channel group id.
 */
static struct iwm_phy_db_entry *
iwm_phy_db_get_section(struct iwm_phy_db *phy_db,
		       enum iwm_phy_db_section_type type,
		       uint16_t chg_id)
{
	if (!phy_db || type >= IWM_PHY_DB_MAX)
		return NULL;

	switch (type) {
	case IWM_PHY_DB_CFG:
		return &phy_db->cfg;
	case IWM_PHY_DB_CALIB_NCH:
		return &phy_db->calib_nch;
	case IWM_PHY_DB_CALIB_CHG_PAPD:
		if (chg_id >= phy_db->n_group_papd)
			return NULL;
		return &phy_db->calib_ch_group_papd[chg_id];
	case IWM_PHY_DB_CALIB_CHG_TXP:
		if (chg_id >= phy_db->n_group_txp)
			return NULL;
		return &phy_db->calib_ch_group_txp[chg_id];
	default:
		return NULL;
	}
	return NULL;
}

static void
iwm_phy_db_free_section(struct iwm_phy_db *phy_db,
			enum iwm_phy_db_section_type type, uint16_t chg_id)
{
	struct iwm_phy_db_entry *entry =
				iwm_phy_db_get_section(phy_db, type, chg_id);
	if (!entry)
		return;

	if (entry->data != NULL)
		free(entry->data, M_DEVBUF);
	entry->data = NULL;
	entry->size = 0;
}

void
iwm_phy_db_free(struct iwm_phy_db *phy_db)
{
	int i;

	if (!phy_db)
		return;

	iwm_phy_db_free_section(phy_db, IWM_PHY_DB_CFG, 0);
	iwm_phy_db_free_section(phy_db, IWM_PHY_DB_CALIB_NCH, 0);

	for (i = 0; i < phy_db->n_group_papd; i++)
		iwm_phy_db_free_section(phy_db, IWM_PHY_DB_CALIB_CHG_PAPD, i);
	if (phy_db->calib_ch_group_papd != NULL)
		free(phy_db->calib_ch_group_papd, M_DEVBUF);

	for (i = 0; i < phy_db->n_group_txp; i++)
		iwm_phy_db_free_section(phy_db, IWM_PHY_DB_CALIB_CHG_TXP, i);
	if (phy_db->calib_ch_group_txp != NULL)
		free(phy_db->calib_ch_group_txp, M_DEVBUF);

	free(phy_db, M_DEVBUF);
}

int
iwm_phy_db_set_section(struct iwm_phy_db *phy_db,
		       struct iwm_rx_packet *pkt)
{
	struct iwm_calib_res_notif_phy_db *phy_db_notif =
			(struct iwm_calib_res_notif_phy_db *)pkt->data;
	enum iwm_phy_db_section_type type = le16toh(phy_db_notif->type);
        uint16_t size  = le16toh(phy_db_notif->length);
        struct iwm_phy_db_entry *entry;
        uint16_t chg_id = 0;

	if (!phy_db)
		return EINVAL;

	if (type == IWM_PHY_DB_CALIB_CHG_PAPD) {
		chg_id = le16toh(*(uint16_t *)phy_db_notif->data);
		if (phy_db && !phy_db->calib_ch_group_papd) {
			/*
			 * Firmware sends the largest index first, so we can use
			 * it to know how much we should allocate.
			 */
			phy_db->calib_ch_group_papd = malloc(
			    (chg_id + 1) * sizeof(struct iwm_phy_db_entry),
			    M_DEVBUF, M_NOWAIT | M_ZERO);
			if (!phy_db->calib_ch_group_papd)
				return ENOMEM;
			phy_db->n_group_papd = chg_id + 1;
		}
	} else if (type == IWM_PHY_DB_CALIB_CHG_TXP) {
		chg_id = le16toh(*(uint16_t *)phy_db_notif->data);
		if (phy_db && !phy_db->calib_ch_group_txp) {
			/*
			 * Firmware sends the largest index first, so we can use
			 * it to know how much we should allocate.
			 */
			phy_db->calib_ch_group_txp = malloc(
			    (chg_id + 1) * sizeof(struct iwm_phy_db_entry),
			    M_DEVBUF, M_NOWAIT | M_ZERO);
			if (!phy_db->calib_ch_group_txp)
				return ENOMEM;
			phy_db->n_group_txp = chg_id + 1;
		}
	}

	entry = iwm_phy_db_get_section(phy_db, type, chg_id);
	if (!entry)
		return EINVAL;

	if (entry->data != NULL)
		free(entry->data, M_DEVBUF);
	entry->data = malloc(size, M_DEVBUF, M_NOWAIT);
	if (!entry->data) {
		entry->size = 0;
		return ENOMEM;
	}
	memcpy(entry->data, phy_db_notif->data, size);

	entry->size = size;

	IWM_DPRINTF(phy_db->sc, IWM_DEBUG_RESET,
		    "%s(%d): [PHYDB]SET: Type %d , Size: %d\n",
		    __func__, __LINE__, type, size);

	return 0;
}

static int
is_valid_channel(uint16_t ch_id)
{
	if (ch_id <= 14 ||
	    (36 <= ch_id && ch_id <= 64 && ch_id % 4 == 0) ||
	    (100 <= ch_id && ch_id <= 140 && ch_id % 4 == 0) ||
	    (145 <= ch_id && ch_id <= 165 && ch_id % 4 == 1))
		return 1;
	return 0;
}

static uint8_t
ch_id_to_ch_index(uint16_t ch_id)
{
	if (!is_valid_channel(ch_id))
		return 0xff;

	if (ch_id <= 14)
		return ch_id - 1;
	if (ch_id <= 64)
		return (ch_id + 20) / 4;
	if (ch_id <= 140)
		return (ch_id - 12) / 4;
	return (ch_id - 13) / 4;
}


static uint16_t
channel_id_to_papd(uint16_t ch_id)
{
	if (!is_valid_channel(ch_id))
		return 0xff;

	if (1 <= ch_id && ch_id <= 14)
		return 0;
	if (36 <= ch_id && ch_id <= 64)
		return 1;
	if (100 <= ch_id && ch_id <= 140)
		return 2;
	return 3;
}

static uint16_t
channel_id_to_txp(struct iwm_phy_db *phy_db, uint16_t ch_id)
{
	struct iwm_phy_db_chg_txp *txp_chg;
	int i;
	uint8_t ch_index = ch_id_to_ch_index(ch_id);
	if (ch_index == 0xff)
		return 0xff;

	for (i = 0; i < phy_db->n_group_txp; i++) {
		txp_chg = (void *)phy_db->calib_ch_group_txp[i].data;
		if (!txp_chg)
			return 0xff;
		/*
		 * Looking for the first channel group that its max channel is
		 * higher then wanted channel.
		 */
		if (le16toh(txp_chg->max_channel_idx) >= ch_index)
			return i;
	}
	return 0xff;
}

static int
iwm_phy_db_get_section_data(struct iwm_phy_db *phy_db,
			   uint32_t type, uint8_t **data, uint16_t *size,
			   uint16_t ch_id)
{
	struct iwm_phy_db_entry *entry;
	uint16_t ch_group_id = 0;

	if (!phy_db)
		return EINVAL;

	/* find wanted channel group */
	if (type == IWM_PHY_DB_CALIB_CHG_PAPD)
		ch_group_id = channel_id_to_papd(ch_id);
	else if (type == IWM_PHY_DB_CALIB_CHG_TXP)
		ch_group_id = channel_id_to_txp(phy_db, ch_id);

	entry = iwm_phy_db_get_section(phy_db, type, ch_group_id);
	if (!entry)
		return EINVAL;

	*data = entry->data;
	*size = entry->size;

	IWM_DPRINTF(phy_db->sc, IWM_DEBUG_RESET,
		   "%s(%d): [PHYDB] GET: Type %d , Size: %d\n",
		   __func__, __LINE__, type, *size);

	return 0;
}

static int
iwm_send_phy_db_cmd(struct iwm_phy_db *phy_db, uint16_t type,
		    uint16_t length, void *data)
{
	struct iwm_phy_db_cmd phy_db_cmd;
	struct iwm_host_cmd cmd = {
		.id = PHY_DB_CMD,
	};

	IWM_DPRINTF(phy_db->sc, IWM_DEBUG_RESET,
		   "Sending PHY-DB hcmd of type %d, of length %d\n",
		   type, length);

	/* Set phy db cmd variables */
	phy_db_cmd.type = htole16(type);
	phy_db_cmd.length = htole16(length);

	/* Set hcmd variables */
	cmd.data[0] = &phy_db_cmd;
	cmd.len[0] = sizeof(struct iwm_phy_db_cmd);
	cmd.data[1] = data;
	cmd.len[1] = length;
#ifdef notyet
	cmd.dataflags[1] = IWM_HCMD_DFL_NOCOPY;
#endif

	return iwm_send_cmd(phy_db->sc, &cmd);
}

static int
iwm_phy_db_send_all_channel_groups(struct iwm_phy_db *phy_db,
				   enum iwm_phy_db_section_type type,
				   uint8_t max_ch_groups)
{
	uint16_t i;
	int err;
	struct iwm_phy_db_entry *entry;

	/* Send all the channel specific groups to operational fw */
	for (i = 0; i < max_ch_groups; i++) {
		entry = iwm_phy_db_get_section(phy_db,
                                               type,
                                               i);
		if (!entry)
			return EINVAL;

		if (!entry->size)
			continue;

		/* Send the requested PHY DB section */
		err = iwm_send_phy_db_cmd(phy_db,
					  type,
					  entry->size,
					  entry->data);
		if (err) {
			device_printf(phy_db->sc->sc_dev,
				"Can't SEND phy_db section %d (%d), err %d\n",
				type, i, err);
			return err;
		}

		IWM_DPRINTF(phy_db->sc, IWM_DEBUG_CMD,
		    "Sent PHY_DB HCMD, type = %d num = %d\n", type, i);
	}

	return 0;
}

int
iwm_send_phy_db_data(struct iwm_phy_db *phy_db)
{
	uint8_t *data = NULL;
	uint16_t size = 0;
	int err;

	IWM_DPRINTF(phy_db->sc, IWM_DEBUG_CMD | IWM_DEBUG_RESET,
	    "%s: Sending phy db data and configuration to runtime image\n",
	    __func__);

	/* Send PHY DB CFG section */
	err = iwm_phy_db_get_section_data(phy_db, IWM_PHY_DB_CFG,
					  &data, &size, 0);
	if (err) {
		device_printf(phy_db->sc->sc_dev,
		    "%s: Cannot get Phy DB cfg section, %d\n",
		    __func__, err);
		return err;
	}

	err = iwm_send_phy_db_cmd(phy_db, IWM_PHY_DB_CFG, size, data);
	if (err) {
		device_printf(phy_db->sc->sc_dev,
		    "%s: Cannot send HCMD of Phy DB cfg section, %d\n",
		    __func__, err);
		return err;
	}

	err = iwm_phy_db_get_section_data(phy_db, IWM_PHY_DB_CALIB_NCH,
	    &data, &size, 0);
	if (err) {
		device_printf(phy_db->sc->sc_dev,
		    "%s: Cannot get Phy DB non specific channel section, "
		    "%d\n", __func__, err);
		return err;
	}

	err = iwm_send_phy_db_cmd(phy_db, IWM_PHY_DB_CALIB_NCH, size, data);
	if (err) {
		device_printf(phy_db->sc->sc_dev,
		    "%s: Cannot send HCMD of Phy DB non specific channel "
		    "sect, %d\n", __func__, err);
		return err;
	}

	/* Send all the TXP channel specific data */
	err = iwm_phy_db_send_all_channel_groups(phy_db,
	    IWM_PHY_DB_CALIB_CHG_PAPD, phy_db->n_group_papd);
	if (err) {
		device_printf(phy_db->sc->sc_dev,
		    "%s: Cannot send channel specific PAPD groups, %d\n",
		    __func__, err);
		return err;
	}

	/* Send all the TXP channel specific data */
	err = iwm_phy_db_send_all_channel_groups(phy_db,
	    IWM_PHY_DB_CALIB_CHG_TXP, phy_db->n_group_txp);
	if (err) {
		device_printf(phy_db->sc->sc_dev,
		    "%s: Cannot send channel specific TX power groups, "
		    "%d\n", __func__, err);
		return err;
	}

	IWM_DPRINTF(phy_db->sc, IWM_DEBUG_CMD | IWM_DEBUG_RESET,
	    "%s: Finished sending phy db non channel data\n",
	    __func__);
	return 0;
}
