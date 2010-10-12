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
	u16 error = 0;

	if ((phr->type != HPI_TYPE_RESPONSE)
		|| (phr->object != phm->object)
		|| (phr->function != phm->function))
		error = HPI_ERROR_INVALID_RESPONSE;

	return error;
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
		{
			retval = HPI_DUPLICATE_ADAPTER_NUMBER;
			goto unlock;
		}
	}
	adapters.adapter[pao->index] = *pao;
	hpios_dsplock_init(&adapters.adapter[pao->index]);
	adapters.gw_num_adapters++;

unlock:
	hpios_alistlock_un_lock(&adapters);
	return retval;
}

void hpi_delete_adapter(struct hpi_adapter_obj *pao)
{
	memset(pao, 0, sizeof(struct hpi_adapter_obj));

	hpios_alistlock_lock(&adapters);
	adapters.gw_num_adapters--;	/* dec the number of adapters */
	hpios_alistlock_un_lock(&adapters);
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
		HPI_DEBUG_LOG(VERBOSE, "find_adapter invalid index %d ",
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
static void wipe_adapter_list(void
	)
{
	memset(&adapters, 0, sizeof(adapters));
}

/**
* SubSysGetAdapters fills awAdapterList in an struct hpi_response structure
* with all adapters in the given HPI_ADAPTERS_LIST.
*
*/
static void subsys_get_adapters(struct hpi_response *phr)
{
	/* fill in the response adapter array with the position */
	/* identified by the adapter number/index of the adapters in */
	/* this HPI */
	/* i.e. if we have an A120 with it's jumper set to */
	/* Adapter Number 2 then put an Adapter type A120 in the */
	/* array in position 1 */
	/* NOTE: AdapterNumber is 1..N, Index is 0..N-1 */

	/* input:  NONE */
	/* output: wNumAdapters */
	/*                 awAdapter[] */
	/* */

	short i;
	struct hpi_adapter_obj *pao = NULL;

	HPI_DEBUG_LOG(VERBOSE, "subsys_get_adapters\n");

	/* for each adapter, place it's type in the position of the array */
	/* corresponding to it's adapter number */
	for (i = 0; i < adapters.gw_num_adapters; i++) {
		pao = &adapters.adapter[i];
		if (phr->u.s.aw_adapter_list[pao->index] != 0) {
			phr->error = HPI_DUPLICATE_ADAPTER_NUMBER;
			phr->specific_error = pao->index;
			return;
		}
		phr->u.s.aw_adapter_list[pao->index] = pao->adapter_type;
	}

	phr->u.s.num_adapters = adapters.gw_num_adapters;
	phr->error = 0;	/* the function completed OK; */
}

static unsigned int control_cache_alloc_check(struct hpi_control_cache *pC)
{
	unsigned int i;
	int cached = 0;
	if (!pC)
		return 0;
	if ((!pC->init) && (pC->p_cache != NULL) && (pC->control_count)
		&& (pC->cache_size_in_bytes)
		) {
		u32 *p_master_cache;
		pC->init = 1;

		p_master_cache = (u32 *)pC->p_cache;
		HPI_DEBUG_LOG(VERBOSE, "check %d controls\n",
			pC->control_count);
		for (i = 0; i < pC->control_count; i++) {
			struct hpi_control_cache_info *info =
				(struct hpi_control_cache_info *)
				p_master_cache;

			if (info->control_type) {
				pC->p_info[i] = info;
				cached++;
			} else
				pC->p_info[i] = NULL;

			if (info->size_in32bit_words)
				p_master_cache += info->size_in32bit_words;
			else
				p_master_cache +=
					sizeof(struct
					hpi_control_cache_single) /
					sizeof(u32);

			HPI_DEBUG_LOG(VERBOSE,
				"cached %d, pinfo %p index %d type %d\n",
				cached, pC->p_info[i], info->control_index,
				info->control_type);
		}
		/*
		   We didn't find anything to cache, so try again later !
		 */
		if (!cached)
			pC->init = 0;
	}
	return pC->init;
}

/** Find a control.
*/
static short find_control(struct hpi_message *phm,
	struct hpi_control_cache *p_cache, struct hpi_control_cache_info **pI,
	u16 *pw_control_index)
{
	*pw_control_index = phm->obj_index;

	if (!control_cache_alloc_check(p_cache)) {
		HPI_DEBUG_LOG(VERBOSE,
			"control_cache_alloc_check() failed. adap%d ci%d\n",
			phm->adapter_index, *pw_control_index);
		return 0;
	}

	*pI = p_cache->p_info[*pw_control_index];
	if (!*pI) {
		HPI_DEBUG_LOG(VERBOSE, "uncached adap %d, control %d\n",
			phm->adapter_index, *pw_control_index);
		return 0;
	} else {
		HPI_DEBUG_LOG(VERBOSE, "find_control() type %d\n",
			(*pI)->control_type);
	}
	return 1;
}

/** Used by the kernel driver to figure out if a buffer needs mapping.
 */
short hpi_check_buffer_mapping(struct hpi_control_cache *p_cache,
	struct hpi_message *phm, void **p, unsigned int *pN)
{
	*pN = 0;
	*p = NULL;
	if ((phm->function == HPI_CONTROL_GET_STATE)
		&& (phm->object == HPI_OBJ_CONTROLEX)
		) {
		u16 control_index;
		struct hpi_control_cache_info *pI;

		if (!find_control(phm, p_cache, &pI, &control_index))
			return 0;
	}
	return 0;
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
	u16 control_index;
	struct hpi_control_cache_info *pI;
	struct hpi_control_cache_single *pC;
	struct hpi_control_cache_pad *p_pad;

	if (!find_control(phm, p_cache, &pI, &control_index))
		return 0;

	phr->error = 0;

	/* pC is the default cached control strucure. May be cast to
	   something else in the following switch statement.
	 */
	pC = (struct hpi_control_cache_single *)pI;
	p_pad = (struct hpi_control_cache_pad *)pI;

	switch (pI->control_type) {

	case HPI_CONTROL_METER:
		if (phm->u.c.attribute == HPI_METER_PEAK) {
			phr->u.c.an_log_value[0] = pC->u.p.an_log_peak[0];
			phr->u.c.an_log_value[1] = pC->u.p.an_log_peak[1];
		} else if (phm->u.c.attribute == HPI_METER_RMS) {
			phr->u.c.an_log_value[0] = pC->u.p.an_logRMS[0];
			phr->u.c.an_log_value[1] = pC->u.p.an_logRMS[1];
		} else
			found = 0;
		break;
	case HPI_CONTROL_VOLUME:
		if (phm->u.c.attribute == HPI_VOLUME_GAIN) {
			phr->u.c.an_log_value[0] = pC->u.v.an_log[0];
			phr->u.c.an_log_value[1] = pC->u.v.an_log[1];
		} else
			found = 0;
		break;
	case HPI_CONTROL_MULTIPLEXER:
		if (phm->u.c.attribute == HPI_MULTIPLEXER_SOURCE) {
			phr->u.c.param1 = pC->u.x.source_node_type;
			phr->u.c.param2 = pC->u.x.source_node_index;
		} else {
			found = 0;
		}
		break;
	case HPI_CONTROL_CHANNEL_MODE:
		if (phm->u.c.attribute == HPI_CHANNEL_MODE_MODE)
			phr->u.c.param1 = pC->u.m.mode;
		else
			found = 0;
		break;
	case HPI_CONTROL_LEVEL:
		if (phm->u.c.attribute == HPI_LEVEL_GAIN) {
			phr->u.c.an_log_value[0] = pC->u.l.an_log[0];
			phr->u.c.an_log_value[1] = pC->u.l.an_log[1];
		} else
			found = 0;
		break;
	case HPI_CONTROL_TUNER:
		if (phm->u.c.attribute == HPI_TUNER_FREQ)
			phr->u.c.param1 = pC->u.t.freq_ink_hz;
		else if (phm->u.c.attribute == HPI_TUNER_BAND)
			phr->u.c.param1 = pC->u.t.band;
		else if ((phm->u.c.attribute == HPI_TUNER_LEVEL)
			&& (phm->u.c.param1 == HPI_TUNER_LEVEL_AVERAGE))
			if (pC->u.t.level == HPI_ERROR_ILLEGAL_CACHE_VALUE) {
				phr->u.c.param1 = 0;
				phr->error =
					HPI_ERROR_INVALID_CONTROL_ATTRIBUTE;
			} else
				phr->u.c.param1 = pC->u.t.level;
		else
			found = 0;
		break;
	case HPI_CONTROL_AESEBU_RECEIVER:
		if (phm->u.c.attribute == HPI_AESEBURX_ERRORSTATUS)
			phr->u.c.param1 = pC->u.aes3rx.error_status;
		else if (phm->u.c.attribute == HPI_AESEBURX_FORMAT)
			phr->u.c.param1 = pC->u.aes3rx.source;
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
			phr->u.c.param2 = pC->u.silence.count;
		} else
			found = 0;
		break;
	case HPI_CONTROL_MICROPHONE:
		if (phm->u.c.attribute == HPI_MICROPHONE_PHANTOM_POWER)
			phr->u.c.param1 = pC->u.phantom_power.state;
		else
			found = 0;
		break;
	case HPI_CONTROL_SAMPLECLOCK:
		if (phm->u.c.attribute == HPI_SAMPLECLOCK_SOURCE)
			phr->u.c.param1 = pC->u.clk.source;
		else if (phm->u.c.attribute == HPI_SAMPLECLOCK_SOURCE_INDEX) {
			if (pC->u.clk.source_index ==
				HPI_ERROR_ILLEGAL_CACHE_VALUE) {
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
	case HPI_CONTROL_PAD:

		if (!(p_pad->field_valid_flags & (1 <<
					HPI_CTL_ATTR_INDEX(phm->u.c.
						attribute)))) {
			phr->error = HPI_ERROR_INVALID_CONTROL_ATTRIBUTE;
			break;
		}

		if (phm->u.c.attribute == HPI_PAD_PROGRAM_ID)
			phr->u.c.param1 = p_pad->pI;
		else if (phm->u.c.attribute == HPI_PAD_PROGRAM_TYPE)
			phr->u.c.param1 = p_pad->pTY;
		else {
			unsigned int index =
				HPI_CTL_ATTR_INDEX(phm->u.c.attribute) - 1;
			unsigned int offset = phm->u.c.param1;
			unsigned int pad_string_len, field_size;
			char *pad_string;
			unsigned int tocopy;

			HPI_DEBUG_LOG(VERBOSE, "PADS HPI_PADS_ %d\n",
				phm->u.c.attribute);

			if (index > ARRAY_SIZE(pad_desc) - 1) {
				phr->error =
					HPI_ERROR_INVALID_CONTROL_ATTRIBUTE;
				break;
			}

			pad_string = ((char *)p_pad) + pad_desc[index].offset;
			field_size = pad_desc[index].field_size;
			/* Ensure null terminator */
			pad_string[field_size - 1] = 0;

			pad_string_len = strlen(pad_string) + 1;

			if (offset > pad_string_len) {
				phr->error = HPI_ERROR_INVALID_CONTROL_VALUE;
				break;
			}

			tocopy = pad_string_len - offset;
			if (tocopy > sizeof(phr->u.cu.chars8.sz_data))
				tocopy = sizeof(phr->u.cu.chars8.sz_data);

			HPI_DEBUG_LOG(VERBOSE,
				"PADS memcpy(%d), offset %d \n", tocopy,
				offset);
			memcpy(phr->u.cu.chars8.sz_data, &pad_string[offset],
				tocopy);

			phr->u.cu.chars8.remaining_chars =
				pad_string_len - offset - tocopy;
		}
		break;
	default:
		found = 0;
		break;
	}

	if (found)
		HPI_DEBUG_LOG(VERBOSE,
			"cached adap %d, ctl %d, type %d, attr %d\n",
			phm->adapter_index, pI->control_index,
			pI->control_type, phm->u.c.attribute);
	else
		HPI_DEBUG_LOG(VERBOSE,
			"uncached adap %d, ctl %d, ctl type %d\n",
			phm->adapter_index, pI->control_index,
			pI->control_type);

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
void hpi_sync_control_cache(struct hpi_control_cache *p_cache,
	struct hpi_message *phm, struct hpi_response *phr)
{
	u16 control_index;
	struct hpi_control_cache_single *pC;
	struct hpi_control_cache_info *pI;

	if (phr->error)
		return;

	if (!find_control(phm, p_cache, &pI, &control_index))
		return;

	/* pC is the default cached control strucure.
	   May be cast to something else in the following switch statement.
	 */
	pC = (struct hpi_control_cache_single *)pI;

	switch (pI->control_type) {
	case HPI_CONTROL_VOLUME:
		if (phm->u.c.attribute == HPI_VOLUME_GAIN) {
			pC->u.v.an_log[0] = phr->u.c.an_log_value[0];
			pC->u.v.an_log[1] = phr->u.c.an_log_value[1];
		}
		break;
	case HPI_CONTROL_MULTIPLEXER:
		/* mux does not return its setting on Set command. */
		if (phm->u.c.attribute == HPI_MULTIPLEXER_SOURCE) {
			pC->u.x.source_node_type = (u16)phm->u.c.param1;
			pC->u.x.source_node_index = (u16)phm->u.c.param2;
		}
		break;
	case HPI_CONTROL_CHANNEL_MODE:
		/* mode does not return its setting on Set command. */
		if (phm->u.c.attribute == HPI_CHANNEL_MODE_MODE)
			pC->u.m.mode = (u16)phm->u.c.param1;
		break;
	case HPI_CONTROL_LEVEL:
		if (phm->u.c.attribute == HPI_LEVEL_GAIN) {
			pC->u.v.an_log[0] = phr->u.c.an_log_value[0];
			pC->u.v.an_log[1] = phr->u.c.an_log_value[1];
		}
		break;
	case HPI_CONTROL_MICROPHONE:
		if (phm->u.c.attribute == HPI_MICROPHONE_PHANTOM_POWER)
			pC->u.phantom_power.state = (u16)phm->u.c.param1;
		break;
	case HPI_CONTROL_AESEBU_TRANSMITTER:
		if (phm->u.c.attribute == HPI_AESEBUTX_FORMAT)
			pC->u.aes3tx.format = phm->u.c.param1;
		break;
	case HPI_CONTROL_AESEBU_RECEIVER:
		if (phm->u.c.attribute == HPI_AESEBURX_FORMAT)
			pC->u.aes3rx.source = phm->u.c.param1;
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

struct hpi_control_cache *hpi_alloc_control_cache(const u32
	number_of_controls, const u32 size_in_bytes,
	struct hpi_control_cache_info *pDSP_control_buffer)
{
	struct hpi_control_cache *p_cache =
		kmalloc(sizeof(*p_cache), GFP_KERNEL);
	p_cache->cache_size_in_bytes = size_in_bytes;
	p_cache->control_count = number_of_controls;
	p_cache->p_cache =
		(struct hpi_control_cache_single *)pDSP_control_buffer;
	p_cache->init = 0;
	p_cache->p_info =
		kmalloc(sizeof(*p_cache->p_info) * p_cache->control_count,
		GFP_KERNEL);
	return p_cache;
}

void hpi_free_control_cache(struct hpi_control_cache *p_cache)
{
	if (p_cache->init) {
		kfree(p_cache->p_info);
		p_cache->p_info = NULL;
		p_cache->init = 0;
		kfree(p_cache);
	}
}

static void subsys_message(struct hpi_message *phm, struct hpi_response *phr)
{

	switch (phm->function) {
	case HPI_SUBSYS_OPEN:
	case HPI_SUBSYS_CLOSE:
	case HPI_SUBSYS_DRIVER_UNLOAD:
		phr->error = 0;
		break;
	case HPI_SUBSYS_DRIVER_LOAD:
		wipe_adapter_list();
		hpios_alistlock_init(&adapters);
		phr->error = 0;
		break;
	case HPI_SUBSYS_GET_INFO:
		subsys_get_adapters(phr);
		break;
	case HPI_SUBSYS_CREATE_ADAPTER:
	case HPI_SUBSYS_DELETE_ADAPTER:
		phr->error = 0;
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
