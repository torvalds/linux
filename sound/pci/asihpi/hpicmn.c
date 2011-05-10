/******************************************************************************

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

\file hpicmn.c

 Common functions used by hpixxxx.c modules

(C) Copyright AudioScience Inc. 1998-2003
*******************************************************************************/
#define SOURCEFILE_NAME "hpicmn.c"

#include "hpi_internal.h"
#include "hpidebug.h"
#include "hpimsginit.h"

#include "hpicmn.h"

struct hpi_adapters_list {
	struct hpios_spinlock list_lock;
	struct hpi_adapter_obj adapter[HPI_MAX_ADAPTERS];
	u16 gw_num_adapters;
};

static struct hpi_adapters_list adapters;

/**
* Given an HPI Message that was sent out and a response that was received,
* validate that the response has the correct fields filled in,
* i.e ObjectType, Function etc
**/
u16 hpi_validate_response(struct hpi_message *phm, struct hpi_response *phr)
{
	if (phr->type != HPI_TYPE_RESPONSE) {
		HPI_DEBUG_LOG(ERROR, "header type %d invalid\n", phr->type);
		return HPI_ERROR_INVALID_RESPONSE;
	}

	if (phr->object != phm->object) {
		HPI_DEBUG_LOG(ERROR, "header object %d invalid\n",
			phr->object);
		return HPI_ERROR_INVALID_RESPONSE;
	}

	if (phr->function != phm->function) {
		HPI_DEBUG_LOG(ERROR, "header type %d invalid\n",
			phr->function);
		return HPI_ERROR_INVALID_RESPONSE;
	}

	return 0;
}

u16 hpi_add_adapter(struct hpi_adapter_obj *pao)
{
	u16 retval = 0;
	/*HPI_ASSERT(pao->wAdapterType); */

	hpios_alistlock_lock(&adapters);

	if (pao->index >= HPI_MAX_ADAPTERS) {
		retval = HPI_ERROR_BAD_ADAPTER_NUMBER;
		goto unlock;
	}

	if (adapters.adapter[pao->index].adapter_type) {
		int a;
		for (a = HPI_MAX_ADAPTERS - 1; a >= 0; a--) {
			if (!adapters.adapter[a].adapter_type) {
				HPI_DEBUG_LOG(WARNING,
					"ASI%X duplicate index %d moved to %d\n",
					pao->adapter_type, pao->index, a);
				pao->index = a;
				break;
			}
		}
		if (a < 0) {
			retval = HPI_ERROR_DUPLICATE_ADAPTER_NUMBER;
			goto unlock;
		}
	}
	adapters.adapter[pao->index] = *pao;
	hpios_dsplock_init(&adapters.adapter[pao->index]);
	adapters.gw_num_adapters++;

unlock:
	hpios_alistlock_unlock(&adapters);
	return retval;
}

void hpi_delete_adapter(struct hpi_adapter_obj *pao)
{
	if (!pao->adapter_type) {
		HPI_DEBUG_LOG(ERROR, "removing null adapter?\n");
		return;
	}

	hpios_alistlock_lock(&adapters);
	if (adapters.adapter[pao->index].adapter_type)
		adapters.gw_num_adapters--;
	memset(&adapters.adapter[pao->index], 0, sizeof(adapters.adapter[0]));
	hpios_alistlock_unlock(&adapters);
}

/**
* FindAdapter returns a pointer to the struct hpi_adapter_obj with
* index wAdapterIndex in an HPI_ADAPTERS_LIST structure.
*
*/
struct hpi_adapter_obj *hpi_find_adapter(u16 adapter_index)
{
	struct hpi_adapter_obj *pao = NULL;

	if (adapter_index >= HPI_MAX_ADAPTERS) {
		HPI_DEBUG_LOG(VERBOSE, "find_adapter invalid index %d\n",
			adapter_index);
		return NULL;
	}

	pao = &adapters.adapter[adapter_index];
	if (pao->adapter_type != 0) {
		/*
		   HPI_DEBUG_LOG(VERBOSE, "Found adapter index %d\n",
		   wAdapterIndex);
		 */
		return pao;
	} else {
		/*
		   HPI_DEBUG_LOG(VERBOSE, "No adapter index %d\n",
		   wAdapterIndex);
		 */
		return NULL;
	}
}

/**
*
* wipe an HPI_ADAPTERS_LIST structure.
*
**/
static void wipe_adapter_list(void)
{
	memset(&adapters, 0, sizeof(adapters));
}

static void subsys_get_adapter(struct hpi_message *phm,
	struct hpi_response *phr)
{
	int count = phm->obj_index;
	u16 index = 0;

	/* find the nCount'th nonzero adapter in array */
	for (index = 0; index < HPI_MAX_ADAPTERS; index++) {
		if (adapters.adapter[index].adapter_type) {
			if (!count)
				break;
			count--;
		}
	}

	if (index < HPI_MAX_ADAPTERS) {
		phr->u.s.adapter_index = adapters.adapter[index].index;
		phr->u.s.adapter_type = adapters.adapter[index].adapter_type;
	} else {
		phr->u.s.adapter_index = 0;
		phr->u.s.adapter_type = 0;
		phr->error = HPI_ERROR_BAD_ADAPTER_NUMBER;
	}
}

static unsigned int control_cache_alloc_check(struct hpi_control_cache *pC)
{
	unsigned int i;
	int cached = 0;
	if (!pC)
		return 0;

	if (pC->init)
		return pC->init;

	if (!pC->p_cache)
		return 0;

	if (pC->control_count && pC->cache_size_in_bytes) {
		char *p_master_cache;
		unsigned int byte_count = 0;

		p_master_cache = (char *)pC->p_cache;
		HPI_DEBUG_LOG(DEBUG, "check %d controls\n",
			pC->control_count);
		for (i = 0; i < pC->control_count; i++) {
			struct hpi_control_cache_info *info =
				(struct hpi_control_cache_info *)
				&p_master_cache[byte_count];

			if (!info->size_in32bit_words) {
				if (!i) {
					HPI_DEBUG_LOG(INFO,
						"adap %d cache not ready?\n",
						pC->adap_idx);
					return 0;
				}
				/* The cache is invalid.
				 * Minimum valid entry size is
				 * sizeof(struct hpi_control_cache_info)
				 */
				HPI_DEBUG_LOG(ERROR,
					"adap %d zero size cache entry %d\n",
					pC->adap_idx, i);
				break;
			}

			if (info->control_type) {
				pC->p_info[info->control_index] = info;
				cached++;
			} else	/* dummy cache entry */
				pC->p_info[info->control_index] = NULL;

			byte_count += info->size_in32bit_words * 4;

			HPI_DEBUG_LOG(VERBOSE,
				"cached %d, pinfo %p index %d type %d size %d\n",
				cached, pC->p_info[info->control_index],
				info->control_index, info->control_type,
				info->size_in32bit_words);

			/* quit loop early if whole cache has been scanned.
			 * dwControlCount is the maximum possible entries
			 * but some may be absent from the cache
			 */
			if (byte_count >= pC->cache_size_in_bytes)
				break;
			/* have seen last control index */
			if (info->control_index == pC->control_count - 1)
				break;
		}

		if (byte_count != pC->cache_size_in_bytes)
			HPI_DEBUG_LOG(WARNING,
				"adap %d bytecount %d != cache size %d\n",
				pC->adap_idx, byte_count,
				pC->cache_size_in_bytes);
		else
			HPI_DEBUG_LOG(DEBUG,
				"adap %d cache good, bytecount == cache size = %d\n",
				pC->adap_idx, byte_count);

		pC->init = (u16)cached;
	}
	return pC->init;
}

/** Find a control.
*/
static short find_control(u16 control_index,
	struct hpi_control_cache *p_cache, struct hpi_control_cache_info **pI)
{
	if (!control_cache_alloc_check(p_cache)) {
		HPI_DEBUG_LOG(VERBOSE,
			"control_cache_alloc_check() failed %d\n",
			control_index);
		return 0;
	}

	*pI = p_cache->p_info[control_index];
	if (!*pI) {
		HPI_DEBUG_LOG(VERBOSE, "Uncached Control %d\n",
			control_index);
		return 0;
	} else {
		HPI_DEBUG_LOG(VERBOSE, "find_control() type %d\n",
			(*pI)->control_type);
	}
	return 1;
}

/* allow unified treatment of several string fields within struct */
#define HPICMN_PAD_OFS_AND_SIZE(m)  {\
	offsetof(struct hpi_control_cache_pad, m), \
	sizeof(((struct hpi_control_cache_pad *)(NULL))->m) }

struct pad_ofs_size {
	unsigned int offset;
	unsigned int field_size;
};

static struct pad_ofs_size pad_desc[] = {
	HPICMN_PAD_OFS_AND_SIZE(c_channel),	/* HPI_PAD_CHANNEL_NAME */
	HPICMN_PAD_OFS_AND_SIZE(c_artist),	/* HPI_PAD_ARTIST */
	HPICMN_PAD_OFS_AND_SIZE(c_title),	/* HPI_PAD_TITLE */
	HPICMN_PAD_OFS_AND_SIZE(c_comment),	/* HPI_PAD_COMMENT */
};

/** CheckControlCache checks the cache and fills the struct hpi_response
 * accordingly. It returns one if a cache hit occurred, zero otherwise.
 */
short hpi_check_control_cache(struct hpi_control_cache *p_cache,
	struct hpi_message *phm, struct hpi_response *phr)
{
	short found = 1;
	struct hpi_control_cache_info *pI;
	struct hpi_control_cache_single *pC;
	struct hpi_control_cache_pad *p_pad;

	if (!find_control(phm->obj_index, p_cache, &pI)) {
		HPI_DEBUG_LOG(VERBOSE,
			"HPICMN find_control() failed for adap %d\n",
			phm->adapter_index);
		return 0;
	}

	phr->error = 0;

	/* pC is the default cached control strucure. May be cast to
	   something else in the following switch statement.
	 */
	pC = (struct hpi_control_cache_single *)pI;
	p_pad = (struct hpi_control_cache_pad *)pI;

	switch (pI->control_type) {

	case HPI_CONTROL_METER:
		if (phm->u.c.attribute == HPI_METER_PEAK) {
			phr->u.c.an_log_value[0] = pC->u.meter.an_log_peak[0];
			phr->u.c.an_log_value[1] = pC->u.meter.an_log_peak[1];
		} else if (phm->u.c.attribute == HPI_METER_RMS) {
			if (pC->u.meter.an_logRMS[0] ==
				HPI_CACHE_INVALID_SHORT) {
				phr->error =
					HPI_ERROR_INVALID_CONTROL_ATTRIBUTE;
				phr->u.c.an_log_value[0] = HPI_METER_MINIMUM;
				phr->u.c.an_log_value[1] = HPI_METER_MINIMUM;
			} else {
				phr->u.c.an_log_value[0] =
					pC->u.meter.an_logRMS[0];
				phr->u.c.an_log_value[1] =
					pC->u.meter.an_logRMS[1];
			}
		} else
			found = 0;
		break;
	case HPI_CONTROL_VOLUME:
		if (phm->u.c.attribute == HPI_VOLUME_GAIN) {
			phr->u.c.an_log_value[0] = pC->u.vol.an_log[0];
			phr->u.c.an_log_value[1] = pC->u.vol.an_log[1];
		} else if (phm->u.c.attribute == HPI_VOLUME_MUTE) {
			if (pC->u.vol.flags & HPI_VOLUME_FLAG_HAS_MUTE) {
				if (pC->u.vol.flags & HPI_VOLUME_FLAG_MUTED)
					phr->u.c.param1 =
						HPI_BITMASK_ALL_CHANNELS;
				else
					phr->u.c.param1 = 0;
			} else {
				phr->error =
					HPI_ERROR_INVALID_CONTROL_ATTRIBUTE;
				phr->u.c.param1 = 0;
			}
		} else {
			found = 0;
		}
		break;
	case HPI_CONTROL_MULTIPLEXER:
		if (phm->u.c.attribute == HPI_MULTIPLEXER_SOURCE) {
			phr->u.c.param1 = pC->u.mux.source_node_type;
			phr->u.c.param2 = pC->u.mux.source_node_index;
		} else {
			found = 0;
		}
		break;
	case HPI_CONTROL_CHANNEL_MODE:
		if (phm->u.c.attribute == HPI_CHANNEL_MODE_MODE)
			phr->u.c.param1 = pC->u.mode.mode;
		else
			found = 0;
		break;
	case HPI_CONTROL_LEVEL:
		if (phm->u.c.attribute == HPI_LEVEL_GAIN) {
			phr->u.c.an_log_value[0] = pC->u.level.an_log[0];
			phr->u.c.an_log_value[1] = pC->u.level.an_log[1];
		} else
			found = 0;
		break;
	case HPI_CONTROL_TUNER:
		if (phm->u.c.attribute == HPI_TUNER_FREQ)
			phr->u.c.param1 = pC->u.tuner.freq_ink_hz;
		else if (phm->u.c.attribute == HPI_TUNER_BAND)
			phr->u.c.param1 = pC->u.tuner.band;
		else if (phm->u.c.attribute == HPI_TUNER_LEVEL_AVG)
			if (pC->u.tuner.s_level_avg ==
				HPI_CACHE_INVALID_SHORT) {
				phr->u.cu.tuner.s_level = 0;
				phr->error =
					HPI_ERROR_INVALID_CONTROL_ATTRIBUTE;
			} else
				phr->u.cu.tuner.s_level =
					pC->u.tuner.s_level_avg;
		else
			found = 0;
		break;
	case HPI_CONTROL_AESEBU_RECEIVER:
		if (phm->u.c.attribute == HPI_AESEBURX_ERRORSTATUS)
			phr->u.c.param1 = pC->u.aes3rx.error_status;
		else if (phm->u.c.attribute == HPI_AESEBURX_FORMAT)
			phr->u.c.param1 = pC->u.aes3rx.format;
		else
			found = 0;
		break;
	case HPI_CONTROL_AESEBU_TRANSMITTER:
		if (phm->u.c.attribute == HPI_AESEBUTX_FORMAT)
			phr->u.c.param1 = pC->u.aes3tx.format;
		else
			found = 0;
		break;
	case HPI_CONTROL_TONEDETECTOR:
		if (phm->u.c.attribute == HPI_TONEDETECTOR_STATE)
			phr->u.c.param1 = pC->u.tone.state;
		else
			found = 0;
		break;
	case HPI_CONTROL_SILENCEDETECTOR:
		if (phm->u.c.attribute == HPI_SILENCEDETECTOR_STATE) {
			phr->u.c.param1 = pC->u.silence.state;
		} else
			found = 0;
		break;
	case HPI_CONTROL_MICROPHONE:
		if (phm->u.c.attribute == HPI_MICROPHONE_PHANTOM_POWER)
			phr->u.c.param1 = pC->u.microphone.phantom_state;
		else
			found = 0;
		break;
	case HPI_CONTROL_SAMPLECLOCK:
		if (phm->u.c.attribute == HPI_SAMPLECLOCK_SOURCE)
			phr->u.c.param1 = pC->u.clk.source;
		else if (phm->u.c.attribute == HPI_SAMPLECLOCK_SOURCE_INDEX) {
			if (pC->u.clk.source_index ==
				HPI_CACHE_INVALID_UINT16) {
				phr->u.c.param1 = 0;
				phr->error =
					HPI_ERROR_INVALID_CONTROL_ATTRIBUTE;
			} else
				phr->u.c.param1 = pC->u.clk.source_index;
		} else if (phm->u.c.attribute == HPI_SAMPLECLOCK_SAMPLERATE)
			phr->u.c.param1 = pC->u.clk.sample_rate;
		else
			found = 0;
		break;
	case HPI_CONTROL_PAD:{
			struct hpi_control_cache_pad *p_pad;
			p_pad = (struct hpi_control_cache_pad *)pI;

			if (!(p_pad->field_valid_flags & (1 <<
						HPI_CTL_ATTR_INDEX(phm->u.c.
							attribute)))) {
				phr->error =
					HPI_ERROR_INVALID_CONTROL_ATTRIBUTE;
				break;
			}

			if (phm->u.c.attribute == HPI_PAD_PROGRAM_ID)
				phr->u.c.param1 = p_pad->pI;
			else if (phm->u.c.attribute == HPI_PAD_PROGRAM_TYPE)
				phr->u.c.param1 = p_pad->pTY;
			else {
				unsigned int index =
					HPI_CTL_ATTR_INDEX(phm->u.c.
					attribute) - 1;
				unsigned int offset = phm->u.c.param1;
				unsigned int pad_string_len, field_size;
				char *pad_string;
				unsigned int tocopy;

				if (index > ARRAY_SIZE(pad_desc) - 1) {
					phr->error =
						HPI_ERROR_INVALID_CONTROL_ATTRIBUTE;
					break;
				}

				pad_string =
					((char *)p_pad) +
					pad_desc[index].offset;
				field_size = pad_desc[index].field_size;
				/* Ensure null terminator */
				pad_string[field_size - 1] = 0;

				pad_string_len = strlen(pad_string) + 1;

				if (offset > pad_string_len) {
					phr->error =
						HPI_ERROR_INVALID_CONTROL_VALUE;
					break;
				}

				tocopy = pad_string_len - offset;
				if (tocopy > sizeof(phr->u.cu.chars8.sz_data))
					tocopy = sizeof(phr->u.cu.chars8.
						sz_data);

				memcpy(phr->u.cu.chars8.sz_data,
					&pad_string[offset], tocopy);

				phr->u.cu.chars8.remaining_chars =
					pad_string_len - offset - tocopy;
			}
		}
		break;
	default:
		found = 0;
		break;
	}

	HPI_DEBUG_LOG(VERBOSE, "%s Adap %d, Ctl %d, Type %d, Attr %d\n",
		found ? "Cached" : "Uncached", phm->adapter_index,
		pI->control_index, pI->control_type, phm->u.c.attribute);

	if (found)
		phr->size =
			sizeof(struct hpi_response_header) +
			sizeof(struct hpi_control_res);

	return found;
}

/** Updates the cache with Set values.

Only update if no error.
Volume and Level return the limited values in the response, so use these
Multiplexer does so use sent values
*/
void hpi_cmn_control_cache_sync_to_msg(struct hpi_control_cache *p_cache,
	struct hpi_message *phm, struct hpi_response *phr)
{
	struct hpi_control_cache_single *pC;
	struct hpi_control_cache_info *pI;

	if (phr->error)
		return;

	if (!find_control(phm->obj_index, p_cache, &pI)) {
		HPI_DEBUG_LOG(VERBOSE,
			"HPICMN find_control() failed for adap %d\n",
			phm->adapter_index);
		return;
	}

	/* pC is the default cached control strucure.
	   May be cast to something else in the following switch statement.
	 */
	pC = (struct hpi_control_cache_single *)pI;

	switch (pI->control_type) {
	case HPI_CONTROL_VOLUME:
		if (phm->u.c.attribute == HPI_VOLUME_GAIN) {
			pC->u.vol.an_log[0] = phr->u.c.an_log_value[0];
			pC->u.vol.an_log[1] = phr->u.c.an_log_value[1];
		} else if (phm->u.c.attribute == HPI_VOLUME_MUTE) {
			if (phm->u.c.param1)
				pC->u.vol.flags |= HPI_VOLUME_FLAG_MUTED;
			else
				pC->u.vol.flags &= ~HPI_VOLUME_FLAG_MUTED;
		}
		break;
	case HPI_CONTROL_MULTIPLEXER:
		/* mux does not return its setting on Set command. */
		if (phm->u.c.attribute == HPI_MULTIPLEXER_SOURCE) {
			pC->u.mux.source_node_type = (u16)phm->u.c.param1;
			pC->u.mux.source_node_index = (u16)phm->u.c.param2;
		}
		break;
	case HPI_CONTROL_CHANNEL_MODE:
		/* mode does not return its setting on Set command. */
		if (phm->u.c.attribute == HPI_CHANNEL_MODE_MODE)
			pC->u.mode.mode = (u16)phm->u.c.param1;
		break;
	case HPI_CONTROL_LEVEL:
		if (phm->u.c.attribute == HPI_LEVEL_GAIN) {
			pC->u.vol.an_log[0] = phr->u.c.an_log_value[0];
			pC->u.vol.an_log[1] = phr->u.c.an_log_value[1];
		}
		break;
	case HPI_CONTROL_MICROPHONE:
		if (phm->u.c.attribute == HPI_MICROPHONE_PHANTOM_POWER)
			pC->u.microphone.phantom_state = (u16)phm->u.c.param1;
		break;
	case HPI_CONTROL_AESEBU_TRANSMITTER:
		if (phm->u.c.attribute == HPI_AESEBUTX_FORMAT)
			pC->u.aes3tx.format = phm->u.c.param1;
		break;
	case HPI_CONTROL_AESEBU_RECEIVER:
		if (phm->u.c.attribute == HPI_AESEBURX_FORMAT)
			pC->u.aes3rx.format = phm->u.c.param1;
		break;
	case HPI_CONTROL_SAMPLECLOCK:
		if (phm->u.c.attribute == HPI_SAMPLECLOCK_SOURCE)
			pC->u.clk.source = (u16)phm->u.c.param1;
		else if (phm->u.c.attribute == HPI_SAMPLECLOCK_SOURCE_INDEX)
			pC->u.clk.source_index = (u16)phm->u.c.param1;
		else if (phm->u.c.attribute == HPI_SAMPLECLOCK_SAMPLERATE)
			pC->u.clk.sample_rate = phm->u.c.param1;
		break;
	default:
		break;
	}
}

struct hpi_control_cache *hpi_alloc_control_cache(const u32 control_count,
	const u32 size_in_bytes, u8 *p_dsp_control_buffer)
{
	struct hpi_control_cache *p_cache =
		kmalloc(sizeof(*p_cache), GFP_KERNEL);
	if (!p_cache)
		return NULL;

	p_cache->p_info =
		kmalloc(sizeof(*p_cache->p_info) * control_count, GFP_KERNEL);
	if (!p_cache->p_info) {
		kfree(p_cache);
		return NULL;
	}
	memset(p_cache->p_info, 0, sizeof(*p_cache->p_info) * control_count);
	p_cache->cache_size_in_bytes = size_in_bytes;
	p_cache->control_count = control_count;
	p_cache->p_cache = p_dsp_control_buffer;
	p_cache->init = 0;
	return p_cache;
}

void hpi_free_control_cache(struct hpi_control_cache *p_cache)
{
	if (p_cache) {
		kfree(p_cache->p_info);
		kfree(p_cache);
	}
}

static void subsys_message(struct hpi_message *phm, struct hpi_response *phr)
{
	hpi_init_response(phr, HPI_OBJ_SUBSYSTEM, phm->function, 0);

	switch (phm->function) {
	case HPI_SUBSYS_OPEN:
	case HPI_SUBSYS_CLOSE:
	case HPI_SUBSYS_DRIVER_UNLOAD:
		break;
	case HPI_SUBSYS_DRIVER_LOAD:
		wipe_adapter_list();
		hpios_alistlock_init(&adapters);
		break;
	case HPI_SUBSYS_GET_ADAPTER:
		subsys_get_adapter(phm, phr);
		break;
	case HPI_SUBSYS_GET_NUM_ADAPTERS:
		phr->u.s.num_adapters = adapters.gw_num_adapters;
		break;
	case HPI_SUBSYS_CREATE_ADAPTER:
	case HPI_SUBSYS_DELETE_ADAPTER:
		break;
	default:
		phr->error = HPI_ERROR_INVALID_FUNC;
		break;
	}
}

void HPI_COMMON(struct hpi_message *phm, struct hpi_response *phr)
{
	switch (phm->type) {
	case HPI_TYPE_MESSAGE:
		switch (phm->object) {
		case HPI_OBJ_SUBSYSTEM:
			subsys_message(phm, phr);
			break;
		}
		break;

	default:
		phr->error = HPI_ERROR_INVALID_TYPE;
		break;
	}
}
