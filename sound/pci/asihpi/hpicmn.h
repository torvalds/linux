/**

    AudioScience HPI driver
    Copyright (C) 1997-2010  AudioScience Inc. <support@audioscience.com>

    This program is free software; you can redistribute it and/or modify
    it under the terms of version 2 of the GNU General Public License as
    published by the Free Software Foundation;

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA

*/

struct hpi_adapter_obj {
	struct hpi_pci pci;	/* PCI info - bus#,dev#,address etc */
	u16 adapter_type;	/* ASI6701 etc */
	u16 index;		/* */
	u16 open;		/* =1 when adapter open */
	u16 mixer_open;

	struct hpios_spinlock dsp_lock;

	u16 dsp_crashed;
	u16 has_control_cache;
	void *priv;
};

struct hpi_control_cache {
	/** indicates whether the structures are initialized */
	u16 init;
	u16 adap_idx;
	u32 control_count;
	u32 cache_size_in_bytes;
	/** pointer to allocated memory of lookup pointers. */
	struct hpi_control_cache_info **p_info;
	/** pointer to DSP's control cache. */
	u8 *p_cache;
};

struct hpi_adapter_obj *hpi_find_adapter(u16 adapter_index);

u16 hpi_add_adapter(struct hpi_adapter_obj *pao);

void hpi_delete_adapter(struct hpi_adapter_obj *pao);

short hpi_check_control_cache(struct hpi_control_cache *pC,
	struct hpi_message *phm, struct hpi_response *phr);
struct hpi_control_cache *hpi_alloc_control_cache(const u32
	number_of_controls, const u32 size_in_bytes, u8 *pDSP_control_buffer);
void hpi_free_control_cache(struct hpi_control_cache *p_cache);

void hpi_cmn_control_cache_sync_to_msg(struct hpi_control_cache *pC,
	struct hpi_message *phm, struct hpi_response *phr);

u16 hpi_validate_response(struct hpi_message *phm, struct hpi_response *phr);
