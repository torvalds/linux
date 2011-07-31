
#include "hpi_internal.h"
#include "hpimsginit.h"

#include "hpidebug.h"

struct hpi_handle {
	unsigned int obj_index:12;
	unsigned int obj_type:4;
	unsigned int adapter_index:14;
	unsigned int spare:1;
	unsigned int read_only:1;
};

union handle_word {
	struct hpi_handle h;
	u32 w;
};

u32 hpi_indexes_to_handle(const char c_object, const u16 adapter_index,
	const u16 object_index)
{
	union handle_word handle;

	handle.h.adapter_index = adapter_index;
	handle.h.spare = 0;
	handle.h.read_only = 0;
	handle.h.obj_type = c_object;
	handle.h.obj_index = object_index;
	return handle.w;
}

void hpi_handle_to_indexes(const u32 handle, u16 *pw_adapter_index,
	u16 *pw_object_index)
{
	union handle_word uhandle;
	uhandle.w = handle;

	if (pw_adapter_index)
		*pw_adapter_index = (u16)uhandle.h.adapter_index;
	if (pw_object_index)
		*pw_object_index = (u16)uhandle.h.obj_index;
}

char hpi_handle_object(const u32 handle)
{
	union handle_word uhandle;
	uhandle.w = handle;
	return (char)uhandle.h.obj_type;
}

#define u32TOINDEX(h, i1) \
do {\
	if (h == 0) \
		return HPI_ERROR_INVALID_OBJ; \
	else \
		hpi_handle_to_indexes(h, i1, NULL); \
} while (0)

#define u32TOINDEXES(h, i1, i2) \
do {\
	if (h == 0) \
		return HPI_ERROR_INVALID_OBJ; \
	else \
		hpi_handle_to_indexes(h, i1, i2);\
} while (0)

void hpi_format_to_msg(struct hpi_msg_format *pMF,
	const struct hpi_format *pF)
{
	pMF->sample_rate = pF->sample_rate;
	pMF->bit_rate = pF->bit_rate;
	pMF->attributes = pF->attributes;
	pMF->channels = pF->channels;
	pMF->format = pF->format;
}

static void hpi_msg_to_format(struct hpi_format *pF,
	struct hpi_msg_format *pMF)
{
	pF->sample_rate = pMF->sample_rate;
	pF->bit_rate = pMF->bit_rate;
	pF->attributes = pMF->attributes;
	pF->channels = pMF->channels;
	pF->format = pMF->format;
	pF->mode_legacy = 0;
	pF->unused = 0;
}

void hpi_stream_response_to_legacy(struct hpi_stream_res *pSR)
{
	pSR->u.legacy_stream_info.auxiliary_data_available =
		pSR->u.stream_info.auxiliary_data_available;
	pSR->u.legacy_stream_info.state = pSR->u.stream_info.state;
}

static struct hpi_hsubsys gh_subsys;

struct hpi_hsubsys *hpi_subsys_create(void)
{
	struct hpi_message hm;
	struct hpi_response hr;

	memset(&gh_subsys, 0, sizeof(struct hpi_hsubsys));

	{
		hpi_init_message_response(&hm, &hr, HPI_OBJ_SUBSYSTEM,
			HPI_SUBSYS_OPEN);
		hpi_send_recv(&hm, &hr);

		if (hr.error == 0)
			return &gh_subsys;

	}
	return NULL;
}

void hpi_subsys_free(const struct hpi_hsubsys *ph_subsys)
{
	struct hpi_message hm;
	struct hpi_response hr;

	hpi_init_message_response(&hm, &hr, HPI_OBJ_SUBSYSTEM,
		HPI_SUBSYS_CLOSE);
	hpi_send_recv(&hm, &hr);

}

u16 hpi_subsys_get_version(const struct hpi_hsubsys *ph_subsys, u32 *pversion)
{
	struct hpi_message hm;
	struct hpi_response hr;

	hpi_init_message_response(&hm, &hr, HPI_OBJ_SUBSYSTEM,
		HPI_SUBSYS_GET_VERSION);
	hpi_send_recv(&hm, &hr);
	*pversion = hr.u.s.version;
	return hr.error;
}

u16 hpi_subsys_get_version_ex(const struct hpi_hsubsys *ph_subsys,
	u32 *pversion_ex)
{
	struct hpi_message hm;
	struct hpi_response hr;

	hpi_init_message_response(&hm, &hr, HPI_OBJ_SUBSYSTEM,
		HPI_SUBSYS_GET_VERSION);
	hpi_send_recv(&hm, &hr);
	*pversion_ex = hr.u.s.data;
	return hr.error;
}

u16 hpi_subsys_get_info(const struct hpi_hsubsys *ph_subsys, u32 *pversion,
	u16 *pw_num_adapters, u16 aw_adapter_list[], u16 list_length)
{
	struct hpi_message hm;
	struct hpi_response hr;
	hpi_init_message_response(&hm, &hr, HPI_OBJ_SUBSYSTEM,
		HPI_SUBSYS_GET_INFO);

	hpi_send_recv(&hm, &hr);

	*pversion = hr.u.s.version;
	if (list_length > HPI_MAX_ADAPTERS)
		memcpy(aw_adapter_list, &hr.u.s.aw_adapter_list,
			HPI_MAX_ADAPTERS);
	else
		memcpy(aw_adapter_list, &hr.u.s.aw_adapter_list, list_length);
	*pw_num_adapters = hr.u.s.num_adapters;
	return hr.error;
}

u16 hpi_subsys_find_adapters(const struct hpi_hsubsys *ph_subsys,
	u16 *pw_num_adapters, u16 aw_adapter_list[], u16 list_length)
{
	struct hpi_message hm;
	struct hpi_response hr;
	hpi_init_message_response(&hm, &hr, HPI_OBJ_SUBSYSTEM,
		HPI_SUBSYS_FIND_ADAPTERS);

	hpi_send_recv(&hm, &hr);

	if (list_length > HPI_MAX_ADAPTERS) {
		memcpy(aw_adapter_list, &hr.u.s.aw_adapter_list,
			HPI_MAX_ADAPTERS * sizeof(u16));
		memset(&aw_adapter_list[HPI_MAX_ADAPTERS], 0,
			(list_length - HPI_MAX_ADAPTERS) * sizeof(u16));
	} else
		memcpy(aw_adapter_list, &hr.u.s.aw_adapter_list,
			list_length * sizeof(u16));
	*pw_num_adapters = hr.u.s.num_adapters;

	return hr.error;
}

u16 hpi_subsys_create_adapter(const struct hpi_hsubsys *ph_subsys,
	const struct hpi_resource *p_resource, u16 *pw_adapter_index)
{
	struct hpi_message hm;
	struct hpi_response hr;

	hpi_init_message_response(&hm, &hr, HPI_OBJ_SUBSYSTEM,
		HPI_SUBSYS_CREATE_ADAPTER);
	hm.u.s.resource = *p_resource;

	hpi_send_recv(&hm, &hr);

	*pw_adapter_index = hr.u.s.adapter_index;
	return hr.error;
}

u16 hpi_subsys_delete_adapter(const struct hpi_hsubsys *ph_subsys,
	u16 adapter_index)
{
	struct hpi_message hm;
	struct hpi_response hr;
	hpi_init_message_response(&hm, &hr, HPI_OBJ_SUBSYSTEM,
		HPI_SUBSYS_DELETE_ADAPTER);
	hm.adapter_index = adapter_index;
	hpi_send_recv(&hm, &hr);
	return hr.error;
}

u16 hpi_subsys_get_num_adapters(const struct hpi_hsubsys *ph_subsys,
	int *pn_num_adapters)
{
	struct hpi_message hm;
	struct hpi_response hr;
	hpi_init_message_response(&hm, &hr, HPI_OBJ_SUBSYSTEM,
		HPI_SUBSYS_GET_NUM_ADAPTERS);
	hpi_send_recv(&hm, &hr);
	*pn_num_adapters = (int)hr.u.s.num_adapters;
	return hr.error;
}

u16 hpi_subsys_get_adapter(const struct hpi_hsubsys *ph_subsys, int iterator,
	u32 *padapter_index, u16 *pw_adapter_type)
{
	struct hpi_message hm;
	struct hpi_response hr;
	hpi_init_message_response(&hm, &hr, HPI_OBJ_SUBSYSTEM,
		HPI_SUBSYS_GET_ADAPTER);
	hm.adapter_index = (u16)iterator;
	hpi_send_recv(&hm, &hr);
	*padapter_index = (int)hr.u.s.adapter_index;
	*pw_adapter_type = hr.u.s.aw_adapter_list[0];
	return hr.error;
}

u16 hpi_subsys_set_host_network_interface(const struct hpi_hsubsys *ph_subsys,
	const char *sz_interface)
{
	struct hpi_message hm;
	struct hpi_response hr;
	hpi_init_message_response(&hm, &hr, HPI_OBJ_SUBSYSTEM,
		HPI_SUBSYS_SET_NETWORK_INTERFACE);
	if (sz_interface == NULL)
		return HPI_ERROR_INVALID_RESOURCE;
	hm.u.s.resource.r.net_if = sz_interface;
	hpi_send_recv(&hm, &hr);
	return hr.error;
}

u16 hpi_adapter_open(const struct hpi_hsubsys *ph_subsys, u16 adapter_index)
{
	struct hpi_message hm;
	struct hpi_response hr;
	hpi_init_message_response(&hm, &hr, HPI_OBJ_ADAPTER,
		HPI_ADAPTER_OPEN);
	hm.adapter_index = adapter_index;

	hpi_send_recv(&hm, &hr);

	return hr.error;

}

u16 hpi_adapter_close(const struct hpi_hsubsys *ph_subsys, u16 adapter_index)
{
	struct hpi_message hm;
	struct hpi_response hr;
	hpi_init_message_response(&hm, &hr, HPI_OBJ_ADAPTER,
		HPI_ADAPTER_CLOSE);
	hm.adapter_index = adapter_index;

	hpi_send_recv(&hm, &hr);

	return hr.error;
}

u16 hpi_adapter_set_mode(const struct hpi_hsubsys *ph_subsys,
	u16 adapter_index, u32 adapter_mode)
{
	return hpi_adapter_set_mode_ex(ph_subsys, adapter_index, adapter_mode,
		HPI_ADAPTER_MODE_SET);
}

u16 hpi_adapter_set_mode_ex(const struct hpi_hsubsys *ph_subsys,
	u16 adapter_index, u32 adapter_mode, u16 query_or_set)
{
	struct hpi_message hm;
	struct hpi_response hr;

	hpi_init_message_response(&hm, &hr, HPI_OBJ_ADAPTER,
		HPI_ADAPTER_SET_MODE);
	hm.adapter_index = adapter_index;
	hm.u.a.adapter_mode = adapter_mode;
	hm.u.a.assert_id = query_or_set;
	hpi_send_recv(&hm, &hr);
	return hr.error;
}

u16 hpi_adapter_get_mode(const struct hpi_hsubsys *ph_subsys,
	u16 adapter_index, u32 *padapter_mode)
{
	struct hpi_message hm;
	struct hpi_response hr;
	hpi_init_message_response(&hm, &hr, HPI_OBJ_ADAPTER,
		HPI_ADAPTER_GET_MODE);
	hm.adapter_index = adapter_index;
	hpi_send_recv(&hm, &hr);
	if (padapter_mode)
		*padapter_mode = hr.u.a.serial_number;
	return hr.error;
}

u16 hpi_adapter_get_info(const struct hpi_hsubsys *ph_subsys,
	u16 adapter_index, u16 *pw_num_outstreams, u16 *pw_num_instreams,
	u16 *pw_version, u32 *pserial_number, u16 *pw_adapter_type)
{
	struct hpi_message hm;
	struct hpi_response hr;
	hpi_init_message_response(&hm, &hr, HPI_OBJ_ADAPTER,
		HPI_ADAPTER_GET_INFO);
	hm.adapter_index = adapter_index;

	hpi_send_recv(&hm, &hr);

	*pw_adapter_type = hr.u.a.adapter_type;
	*pw_num_outstreams = hr.u.a.num_outstreams;
	*pw_num_instreams = hr.u.a.num_instreams;
	*pw_version = hr.u.a.version;
	*pserial_number = hr.u.a.serial_number;
	return hr.error;
}

u16 hpi_adapter_get_module_by_index(const struct hpi_hsubsys *ph_subsys,
	u16 adapter_index, u16 module_index, u16 *pw_num_outputs,
	u16 *pw_num_inputs, u16 *pw_version, u32 *pserial_number,
	u16 *pw_module_type, u32 *ph_module)
{
	struct hpi_message hm;
	struct hpi_response hr;

	hpi_init_message_response(&hm, &hr, HPI_OBJ_ADAPTER,
		HPI_ADAPTER_MODULE_INFO);
	hm.adapter_index = adapter_index;
	hm.u.ax.module_info.index = module_index;

	hpi_send_recv(&hm, &hr);

	*pw_module_type = hr.u.a.adapter_type;
	*pw_num_outputs = hr.u.a.num_outstreams;
	*pw_num_inputs = hr.u.a.num_instreams;
	*pw_version = hr.u.a.version;
	*pserial_number = hr.u.a.serial_number;
	*ph_module = 0;

	return hr.error;
}

u16 hpi_adapter_get_assert(const struct hpi_hsubsys *ph_subsys,
	u16 adapter_index, u16 *assert_present, char *psz_assert,
	u16 *pw_line_number)
{
	struct hpi_message hm;
	struct hpi_response hr;
	hpi_init_message_response(&hm, &hr, HPI_OBJ_ADAPTER,
		HPI_ADAPTER_GET_ASSERT);
	hm.adapter_index = adapter_index;
	hpi_send_recv(&hm, &hr);

	*assert_present = 0;

	if (!hr.error) {

		*pw_line_number = (u16)hr.u.a.serial_number;
		if (*pw_line_number) {

			int i;
			char *src = (char *)hr.u.a.sz_adapter_assert;
			char *dst = psz_assert;

			*assert_present = 1;

			for (i = 0; i < HPI_STRING_LEN; i++) {
				char c;
				c = *src++;
				*dst++ = c;
				if (c == 0)
					break;
			}

		}
	}
	return hr.error;
}

u16 hpi_adapter_get_assert_ex(const struct hpi_hsubsys *ph_subsys,
	u16 adapter_index, u16 *assert_present, char *psz_assert,
	u32 *pline_number, u16 *pw_assert_on_dsp)
{
	struct hpi_message hm;
	struct hpi_response hr;
	hpi_init_message_response(&hm, &hr, HPI_OBJ_ADAPTER,
		HPI_ADAPTER_GET_ASSERT);
	hm.adapter_index = adapter_index;

	hpi_send_recv(&hm, &hr);

	*assert_present = 0;

	if (!hr.error) {

		*pline_number = hr.u.a.serial_number;

		*assert_present = hr.u.a.adapter_type;

		*pw_assert_on_dsp = hr.u.a.adapter_index;

		if (!*assert_present && *pline_number)

			*assert_present = 1;

		if (*assert_present) {

			int i;
			char *src = (char *)hr.u.a.sz_adapter_assert;
			char *dst = psz_assert;

			for (i = 0; i < HPI_STRING_LEN; i++) {
				char c;
				c = *src++;
				*dst++ = c;
				if (c == 0)
					break;
			}

		} else {
			*psz_assert = 0;
		}
	}
	return hr.error;
}

u16 hpi_adapter_test_assert(const struct hpi_hsubsys *ph_subsys,
	u16 adapter_index, u16 assert_id)
{
	struct hpi_message hm;
	struct hpi_response hr;
	hpi_init_message_response(&hm, &hr, HPI_OBJ_ADAPTER,
		HPI_ADAPTER_TEST_ASSERT);
	hm.adapter_index = adapter_index;
	hm.u.a.assert_id = assert_id;

	hpi_send_recv(&hm, &hr);

	return hr.error;
}

u16 hpi_adapter_enable_capability(const struct hpi_hsubsys *ph_subsys,
	u16 adapter_index, u16 capability, u32 key)
{
	struct hpi_message hm;
	struct hpi_response hr;
	hpi_init_message_response(&hm, &hr, HPI_OBJ_ADAPTER,
		HPI_ADAPTER_ENABLE_CAPABILITY);
	hm.adapter_index = adapter_index;
	hm.u.a.assert_id = capability;
	hm.u.a.adapter_mode = key;

	hpi_send_recv(&hm, &hr);

	return hr.error;
}

u16 hpi_adapter_self_test(const struct hpi_hsubsys *ph_subsys,
	u16 adapter_index)
{
	struct hpi_message hm;
	struct hpi_response hr;
	hpi_init_message_response(&hm, &hr, HPI_OBJ_ADAPTER,
		HPI_ADAPTER_SELFTEST);
	hm.adapter_index = adapter_index;
	hpi_send_recv(&hm, &hr);
	return hr.error;
}

u16 hpi_adapter_debug_read(const struct hpi_hsubsys *ph_subsys,
	u16 adapter_index, u32 dsp_address, char *p_buffer, int *count_bytes)
{
	struct hpi_message hm;
	struct hpi_response hr;
	hpi_init_message_response(&hm, &hr, HPI_OBJ_ADAPTER,
		HPI_ADAPTER_DEBUG_READ);

	hr.size = sizeof(hr);

	hm.adapter_index = adapter_index;
	hm.u.ax.debug_read.dsp_address = dsp_address;

	if (*count_bytes > (int)sizeof(hr.u.bytes))
		*count_bytes = sizeof(hr.u.bytes);

	hm.u.ax.debug_read.count_bytes = *count_bytes;

	hpi_send_recv(&hm, &hr);

	if (!hr.error) {
		*count_bytes = hr.size - 12;
		memcpy(p_buffer, &hr.u.bytes, *count_bytes);
	} else
		*count_bytes = 0;
	return hr.error;
}

u16 hpi_adapter_set_property(const struct hpi_hsubsys *ph_subsys,
	u16 adapter_index, u16 property, u16 parameter1, u16 parameter2)
{
	struct hpi_message hm;
	struct hpi_response hr;
	hpi_init_message_response(&hm, &hr, HPI_OBJ_ADAPTER,
		HPI_ADAPTER_SET_PROPERTY);
	hm.adapter_index = adapter_index;
	hm.u.ax.property_set.property = property;
	hm.u.ax.property_set.parameter1 = parameter1;
	hm.u.ax.property_set.parameter2 = parameter2;

	hpi_send_recv(&hm, &hr);

	return hr.error;
}

u16 hpi_adapter_get_property(const struct hpi_hsubsys *ph_subsys,
	u16 adapter_index, u16 property, u16 *pw_parameter1,
	u16 *pw_parameter2)
{
	struct hpi_message hm;
	struct hpi_response hr;
	hpi_init_message_response(&hm, &hr, HPI_OBJ_ADAPTER,
		HPI_ADAPTER_GET_PROPERTY);
	hm.adapter_index = adapter_index;
	hm.u.ax.property_set.property = property;

	hpi_send_recv(&hm, &hr);
	if (!hr.error) {
		if (pw_parameter1)
			*pw_parameter1 = hr.u.ax.property_get.parameter1;
		if (pw_parameter2)
			*pw_parameter2 = hr.u.ax.property_get.parameter2;
	}

	return hr.error;
}

u16 hpi_adapter_enumerate_property(const struct hpi_hsubsys *ph_subsys,
	u16 adapter_index, u16 index, u16 what_to_enumerate,
	u16 property_index, u32 *psetting)
{
	return 0;
}

u16 hpi_format_create(struct hpi_format *p_format, u16 channels, u16 format,
	u32 sample_rate, u32 bit_rate, u32 attributes)
{
	u16 error = 0;
	struct hpi_msg_format fmt;

	switch (channels) {
	case 1:
	case 2:
	case 4:
	case 6:
	case 8:
	case 16:
		break;
	default:
		error = HPI_ERROR_INVALID_CHANNELS;
		return error;
	}
	fmt.channels = channels;

	switch (format) {
	case HPI_FORMAT_PCM16_SIGNED:
	case HPI_FORMAT_PCM24_SIGNED:
	case HPI_FORMAT_PCM32_SIGNED:
	case HPI_FORMAT_PCM32_FLOAT:
	case HPI_FORMAT_PCM16_BIGENDIAN:
	case HPI_FORMAT_PCM8_UNSIGNED:
	case HPI_FORMAT_MPEG_L1:
	case HPI_FORMAT_MPEG_L2:
	case HPI_FORMAT_MPEG_L3:
	case HPI_FORMAT_DOLBY_AC2:
	case HPI_FORMAT_AA_TAGIT1_HITS:
	case HPI_FORMAT_AA_TAGIT1_INSERTS:
	case HPI_FORMAT_RAW_BITSTREAM:
	case HPI_FORMAT_AA_TAGIT1_HITS_EX1:
	case HPI_FORMAT_OEM1:
	case HPI_FORMAT_OEM2:
		break;
	default:
		error = HPI_ERROR_INVALID_FORMAT;
		return error;
	}
	fmt.format = format;

	if (sample_rate < 8000L) {
		error = HPI_ERROR_INCOMPATIBLE_SAMPLERATE;
		sample_rate = 8000L;
	}
	if (sample_rate > 200000L) {
		error = HPI_ERROR_INCOMPATIBLE_SAMPLERATE;
		sample_rate = 200000L;
	}
	fmt.sample_rate = sample_rate;

	switch (format) {
	case HPI_FORMAT_MPEG_L1:
	case HPI_FORMAT_MPEG_L2:
	case HPI_FORMAT_MPEG_L3:
		fmt.bit_rate = bit_rate;
		break;
	case HPI_FORMAT_PCM16_SIGNED:
	case HPI_FORMAT_PCM16_BIGENDIAN:
		fmt.bit_rate = channels * sample_rate * 2;
		break;
	case HPI_FORMAT_PCM32_SIGNED:
	case HPI_FORMAT_PCM32_FLOAT:
		fmt.bit_rate = channels * sample_rate * 4;
		break;
	case HPI_FORMAT_PCM8_UNSIGNED:
		fmt.bit_rate = channels * sample_rate;
		break;
	default:
		fmt.bit_rate = 0;
	}

	switch (format) {
	case HPI_FORMAT_MPEG_L2:
		if ((channels == 1)
			&& (attributes != HPI_MPEG_MODE_DEFAULT)) {
			attributes = HPI_MPEG_MODE_DEFAULT;
			error = HPI_ERROR_INVALID_FORMAT;
		} else if (attributes > HPI_MPEG_MODE_DUALCHANNEL) {
			attributes = HPI_MPEG_MODE_DEFAULT;
			error = HPI_ERROR_INVALID_FORMAT;
		}
		fmt.attributes = attributes;
		break;
	default:
		fmt.attributes = attributes;
	}

	hpi_msg_to_format(p_format, &fmt);
	return error;
}

u16 hpi_stream_estimate_buffer_size(struct hpi_format *p_format,
	u32 host_polling_rate_in_milli_seconds, u32 *recommended_buffer_size)
{

	u32 bytes_per_second;
	u32 size;
	u16 channels;
	struct hpi_format *pF = p_format;

	channels = pF->channels;

	switch (pF->format) {
	case HPI_FORMAT_PCM16_BIGENDIAN:
	case HPI_FORMAT_PCM16_SIGNED:
		bytes_per_second = pF->sample_rate * 2L * channels;
		break;
	case HPI_FORMAT_PCM24_SIGNED:
		bytes_per_second = pF->sample_rate * 3L * channels;
		break;
	case HPI_FORMAT_PCM32_SIGNED:
	case HPI_FORMAT_PCM32_FLOAT:
		bytes_per_second = pF->sample_rate * 4L * channels;
		break;
	case HPI_FORMAT_PCM8_UNSIGNED:
		bytes_per_second = pF->sample_rate * 1L * channels;
		break;
	case HPI_FORMAT_MPEG_L1:
	case HPI_FORMAT_MPEG_L2:
	case HPI_FORMAT_MPEG_L3:
		bytes_per_second = pF->bit_rate / 8L;
		break;
	case HPI_FORMAT_DOLBY_AC2:

		bytes_per_second = 256000L / 8L;
		break;
	default:
		return HPI_ERROR_INVALID_FORMAT;
	}
	size = (bytes_per_second * host_polling_rate_in_milli_seconds * 2) /
		1000L;

	*recommended_buffer_size =
		roundup_pow_of_two(((size + 4095L) & ~4095L));
	return 0;
}

u16 hpi_outstream_open(const struct hpi_hsubsys *ph_subsys, u16 adapter_index,
	u16 outstream_index, u32 *ph_outstream)
{
	struct hpi_message hm;
	struct hpi_response hr;
	hpi_init_message_response(&hm, &hr, HPI_OBJ_OSTREAM,
		HPI_OSTREAM_OPEN);
	hm.adapter_index = adapter_index;
	hm.obj_index = outstream_index;

	hpi_send_recv(&hm, &hr);

	if (hr.error == 0)
		*ph_outstream =
			hpi_indexes_to_handle(HPI_OBJ_OSTREAM, adapter_index,
			outstream_index);
	else
		*ph_outstream = 0;
	return hr.error;
}

u16 hpi_outstream_close(const struct hpi_hsubsys *ph_subsys, u32 h_outstream)
{
	struct hpi_message hm;
	struct hpi_response hr;

	hpi_init_message_response(&hm, &hr, HPI_OBJ_OSTREAM,
		HPI_OSTREAM_HOSTBUFFER_FREE);
	u32TOINDEXES(h_outstream, &hm.adapter_index, &hm.obj_index);
	hpi_send_recv(&hm, &hr);

	hpi_init_message_response(&hm, &hr, HPI_OBJ_OSTREAM,
		HPI_OSTREAM_GROUP_RESET);
	u32TOINDEXES(h_outstream, &hm.adapter_index, &hm.obj_index);
	hpi_send_recv(&hm, &hr);

	hpi_init_message_response(&hm, &hr, HPI_OBJ_OSTREAM,
		HPI_OSTREAM_CLOSE);
	u32TOINDEXES(h_outstream, &hm.adapter_index, &hm.obj_index);
	hpi_send_recv(&hm, &hr);

	return hr.error;
}

u16 hpi_outstream_get_info_ex(const struct hpi_hsubsys *ph_subsys,
	u32 h_outstream, u16 *pw_state, u32 *pbuffer_size, u32 *pdata_to_play,
	u32 *psamples_played, u32 *pauxiliary_data_to_play)
{
	struct hpi_message hm;
	struct hpi_response hr;
	hpi_init_message_response(&hm, &hr, HPI_OBJ_OSTREAM,
		HPI_OSTREAM_GET_INFO);
	u32TOINDEXES(h_outstream, &hm.adapter_index, &hm.obj_index);

	hpi_send_recv(&hm, &hr);

	if (pw_state)
		*pw_state = hr.u.d.u.stream_info.state;
	if (pbuffer_size)
		*pbuffer_size = hr.u.d.u.stream_info.buffer_size;
	if (pdata_to_play)
		*pdata_to_play = hr.u.d.u.stream_info.data_available;
	if (psamples_played)
		*psamples_played = hr.u.d.u.stream_info.samples_transferred;
	if (pauxiliary_data_to_play)
		*pauxiliary_data_to_play =
			hr.u.d.u.stream_info.auxiliary_data_available;
	return hr.error;
}

u16 hpi_outstream_write_buf(const struct hpi_hsubsys *ph_subsys,
	u32 h_outstream, const u8 *pb_data, u32 bytes_to_write,
	const struct hpi_format *p_format)
{
	struct hpi_message hm;
	struct hpi_response hr;
	hpi_init_message_response(&hm, &hr, HPI_OBJ_OSTREAM,
		HPI_OSTREAM_WRITE);
	u32TOINDEXES(h_outstream, &hm.adapter_index, &hm.obj_index);
	hm.u.d.u.data.pb_data = (u8 *)pb_data;
	hm.u.d.u.data.data_size = bytes_to_write;

	hpi_format_to_msg(&hm.u.d.u.data.format, p_format);

	hpi_send_recv(&hm, &hr);

	return hr.error;
}

u16 hpi_outstream_start(const struct hpi_hsubsys *ph_subsys, u32 h_outstream)
{
	struct hpi_message hm;
	struct hpi_response hr;
	hpi_init_message_response(&hm, &hr, HPI_OBJ_OSTREAM,
		HPI_OSTREAM_START);
	u32TOINDEXES(h_outstream, &hm.adapter_index, &hm.obj_index);

	hpi_send_recv(&hm, &hr);

	return hr.error;
}

u16 hpi_outstream_wait_start(const struct hpi_hsubsys *ph_subsys,
	u32 h_outstream)
{
	struct hpi_message hm;
	struct hpi_response hr;
	hpi_init_message_response(&hm, &hr, HPI_OBJ_OSTREAM,
		HPI_OSTREAM_WAIT_START);
	u32TOINDEXES(h_outstream, &hm.adapter_index, &hm.obj_index);

	hpi_send_recv(&hm, &hr);

	return hr.error;
}

u16 hpi_outstream_stop(const struct hpi_hsubsys *ph_subsys, u32 h_outstream)
{
	struct hpi_message hm;
	struct hpi_response hr;
	hpi_init_message_response(&hm, &hr, HPI_OBJ_OSTREAM,
		HPI_OSTREAM_STOP);
	u32TOINDEXES(h_outstream, &hm.adapter_index, &hm.obj_index);

	hpi_send_recv(&hm, &hr);

	return hr.error;
}

u16 hpi_outstream_sinegen(const struct hpi_hsubsys *ph_subsys,
	u32 h_outstream)
{
	struct hpi_message hm;
	struct hpi_response hr;
	hpi_init_message_response(&hm, &hr, HPI_OBJ_OSTREAM,
		HPI_OSTREAM_SINEGEN);
	u32TOINDEXES(h_outstream, &hm.adapter_index, &hm.obj_index);

	hpi_send_recv(&hm, &hr);

	return hr.error;
}

u16 hpi_outstream_reset(const struct hpi_hsubsys *ph_subsys, u32 h_outstream)
{
	struct hpi_message hm;
	struct hpi_response hr;
	hpi_init_message_response(&hm, &hr, HPI_OBJ_OSTREAM,
		HPI_OSTREAM_RESET);
	u32TOINDEXES(h_outstream, &hm.adapter_index, &hm.obj_index);

	hpi_send_recv(&hm, &hr);

	return hr.error;
}

u16 hpi_outstream_query_format(const struct hpi_hsubsys *ph_subsys,
	u32 h_outstream, struct hpi_format *p_format)
{
	struct hpi_message hm;
	struct hpi_response hr;

	hpi_init_message_response(&hm, &hr, HPI_OBJ_OSTREAM,
		HPI_OSTREAM_QUERY_FORMAT);
	u32TOINDEXES(h_outstream, &hm.adapter_index, &hm.obj_index);

	hpi_format_to_msg(&hm.u.d.u.data.format, p_format);

	hpi_send_recv(&hm, &hr);

	return hr.error;
}

u16 hpi_outstream_set_format(const struct hpi_hsubsys *ph_subsys,
	u32 h_outstream, struct hpi_format *p_format)
{
	struct hpi_message hm;
	struct hpi_response hr;

	hpi_init_message_response(&hm, &hr, HPI_OBJ_OSTREAM,
		HPI_OSTREAM_SET_FORMAT);
	u32TOINDEXES(h_outstream, &hm.adapter_index, &hm.obj_index);

	hpi_format_to_msg(&hm.u.d.u.data.format, p_format);

	hpi_send_recv(&hm, &hr);

	return hr.error;
}

u16 hpi_outstream_set_velocity(const struct hpi_hsubsys *ph_subsys,
	u32 h_outstream, short velocity)
{
	struct hpi_message hm;
	struct hpi_response hr;

	hpi_init_message_response(&hm, &hr, HPI_OBJ_OSTREAM,
		HPI_OSTREAM_SET_VELOCITY);
	u32TOINDEXES(h_outstream, &hm.adapter_index, &hm.obj_index);
	hm.u.d.u.velocity = velocity;

	hpi_send_recv(&hm, &hr);

	return hr.error;
}

u16 hpi_outstream_set_punch_in_out(const struct hpi_hsubsys *ph_subsys,
	u32 h_outstream, u32 punch_in_sample, u32 punch_out_sample)
{
	struct hpi_message hm;
	struct hpi_response hr;

	hpi_init_message_response(&hm, &hr, HPI_OBJ_OSTREAM,
		HPI_OSTREAM_SET_PUNCHINOUT);
	u32TOINDEXES(h_outstream, &hm.adapter_index, &hm.obj_index);

	hm.u.d.u.pio.punch_in_sample = punch_in_sample;
	hm.u.d.u.pio.punch_out_sample = punch_out_sample;

	hpi_send_recv(&hm, &hr);

	return hr.error;
}

u16 hpi_outstream_ancillary_reset(const struct hpi_hsubsys *ph_subsys,
	u32 h_outstream, u16 mode)
{
	struct hpi_message hm;
	struct hpi_response hr;

	hpi_init_message_response(&hm, &hr, HPI_OBJ_OSTREAM,
		HPI_OSTREAM_ANC_RESET);
	u32TOINDEXES(h_outstream, &hm.adapter_index, &hm.obj_index);
	hm.u.d.u.data.format.channels = mode;
	hpi_send_recv(&hm, &hr);
	return hr.error;
}

u16 hpi_outstream_ancillary_get_info(const struct hpi_hsubsys *ph_subsys,
	u32 h_outstream, u32 *pframes_available)
{
	struct hpi_message hm;
	struct hpi_response hr;

	hpi_init_message_response(&hm, &hr, HPI_OBJ_OSTREAM,
		HPI_OSTREAM_ANC_GET_INFO);
	u32TOINDEXES(h_outstream, &hm.adapter_index, &hm.obj_index);
	hpi_send_recv(&hm, &hr);
	if (hr.error == 0) {
		if (pframes_available)
			*pframes_available =
				hr.u.d.u.stream_info.data_available /
				sizeof(struct hpi_anc_frame);
	}
	return hr.error;
}

u16 hpi_outstream_ancillary_read(const struct hpi_hsubsys *ph_subsys,
	u32 h_outstream, struct hpi_anc_frame *p_anc_frame_buffer,
	u32 anc_frame_buffer_size_in_bytes,
	u32 number_of_ancillary_frames_to_read)
{
	struct hpi_message hm;
	struct hpi_response hr;

	hpi_init_message_response(&hm, &hr, HPI_OBJ_OSTREAM,
		HPI_OSTREAM_ANC_READ);
	u32TOINDEXES(h_outstream, &hm.adapter_index, &hm.obj_index);
	hm.u.d.u.data.pb_data = (u8 *)p_anc_frame_buffer;
	hm.u.d.u.data.data_size =
		number_of_ancillary_frames_to_read *
		sizeof(struct hpi_anc_frame);
	if (hm.u.d.u.data.data_size <= anc_frame_buffer_size_in_bytes)
		hpi_send_recv(&hm, &hr);
	else
		hr.error = HPI_ERROR_INVALID_DATA_TRANSFER;
	return hr.error;
}

u16 hpi_outstream_set_time_scale(const struct hpi_hsubsys *ph_subsys,
	u32 h_outstream, u32 time_scale)
{
	struct hpi_message hm;
	struct hpi_response hr;

	hpi_init_message_response(&hm, &hr, HPI_OBJ_OSTREAM,
		HPI_OSTREAM_SET_TIMESCALE);
	u32TOINDEXES(h_outstream, &hm.adapter_index, &hm.obj_index);

	hm.u.d.u.time_scale = time_scale;

	hpi_send_recv(&hm, &hr);

	return hr.error;
}

u16 hpi_outstream_host_buffer_allocate(const struct hpi_hsubsys *ph_subsys,
	u32 h_outstream, u32 size_in_bytes)
{
	struct hpi_message hm;
	struct hpi_response hr;

	hpi_init_message_response(&hm, &hr, HPI_OBJ_OSTREAM,
		HPI_OSTREAM_HOSTBUFFER_ALLOC);
	u32TOINDEXES(h_outstream, &hm.adapter_index, &hm.obj_index);
	hm.u.d.u.data.data_size = size_in_bytes;
	hpi_send_recv(&hm, &hr);
	return hr.error;
}

u16 hpi_outstream_host_buffer_get_info(const struct hpi_hsubsys *ph_subsys,
	u32 h_outstream, u8 **pp_buffer,
	struct hpi_hostbuffer_status **pp_status)
{
	struct hpi_message hm;
	struct hpi_response hr;

	hpi_init_message_response(&hm, &hr, HPI_OBJ_OSTREAM,
		HPI_OSTREAM_HOSTBUFFER_GET_INFO);
	u32TOINDEXES(h_outstream, &hm.adapter_index, &hm.obj_index);
	hpi_send_recv(&hm, &hr);

	if (hr.error == 0) {
		if (pp_buffer)
			*pp_buffer = hr.u.d.u.hostbuffer_info.p_buffer;
		if (pp_status)
			*pp_status = hr.u.d.u.hostbuffer_info.p_status;
	}
	return hr.error;
}

u16 hpi_outstream_host_buffer_free(const struct hpi_hsubsys *ph_subsys,
	u32 h_outstream)
{
	struct hpi_message hm;
	struct hpi_response hr;

	hpi_init_message_response(&hm, &hr, HPI_OBJ_OSTREAM,
		HPI_OSTREAM_HOSTBUFFER_FREE);
	u32TOINDEXES(h_outstream, &hm.adapter_index, &hm.obj_index);
	hpi_send_recv(&hm, &hr);
	return hr.error;
}

u16 hpi_outstream_group_add(const struct hpi_hsubsys *ph_subsys,
	u32 h_outstream, u32 h_stream)
{
	struct hpi_message hm;
	struct hpi_response hr;
	u16 adapter;
	char c_obj_type;

	hpi_init_message_response(&hm, &hr, HPI_OBJ_OSTREAM,
		HPI_OSTREAM_GROUP_ADD);
	hr.error = 0;
	u32TOINDEXES(h_outstream, &hm.adapter_index, &hm.obj_index);
	c_obj_type = hpi_handle_object(h_stream);
	switch (c_obj_type) {
	case HPI_OBJ_OSTREAM:
		hm.u.d.u.stream.object_type = HPI_OBJ_OSTREAM;
		u32TOINDEXES(h_stream, &adapter,
			&hm.u.d.u.stream.stream_index);
		break;
	case HPI_OBJ_ISTREAM:
		hm.u.d.u.stream.object_type = HPI_OBJ_ISTREAM;
		u32TOINDEXES(h_stream, &adapter,
			&hm.u.d.u.stream.stream_index);
		break;
	default:
		return HPI_ERROR_INVALID_STREAM;
	}
	if (adapter != hm.adapter_index)
		return HPI_ERROR_NO_INTERADAPTER_GROUPS;

	hpi_send_recv(&hm, &hr);
	return hr.error;
}

u16 hpi_outstream_group_get_map(const struct hpi_hsubsys *ph_subsys,
	u32 h_outstream, u32 *poutstream_map, u32 *pinstream_map)
{
	struct hpi_message hm;
	struct hpi_response hr;

	hpi_init_message_response(&hm, &hr, HPI_OBJ_OSTREAM,
		HPI_OSTREAM_GROUP_GETMAP);
	u32TOINDEXES(h_outstream, &hm.adapter_index, &hm.obj_index);
	hpi_send_recv(&hm, &hr);

	if (poutstream_map)
		*poutstream_map = hr.u.d.u.group_info.outstream_group_map;
	if (pinstream_map)
		*pinstream_map = hr.u.d.u.group_info.instream_group_map;

	return hr.error;
}

u16 hpi_outstream_group_reset(const struct hpi_hsubsys *ph_subsys,
	u32 h_outstream)
{
	struct hpi_message hm;
	struct hpi_response hr;

	hpi_init_message_response(&hm, &hr, HPI_OBJ_OSTREAM,
		HPI_OSTREAM_GROUP_RESET);
	u32TOINDEXES(h_outstream, &hm.adapter_index, &hm.obj_index);
	hpi_send_recv(&hm, &hr);
	return hr.error;
}

u16 hpi_instream_open(const struct hpi_hsubsys *ph_subsys, u16 adapter_index,
	u16 instream_index, u32 *ph_instream)
{
	struct hpi_message hm;
	struct hpi_response hr;

	hpi_init_message_response(&hm, &hr, HPI_OBJ_ISTREAM,
		HPI_ISTREAM_OPEN);
	hm.adapter_index = adapter_index;
	hm.obj_index = instream_index;

	hpi_send_recv(&hm, &hr);

	if (hr.error == 0)
		*ph_instream =
			hpi_indexes_to_handle(HPI_OBJ_ISTREAM, adapter_index,
			instream_index);
	else
		*ph_instream = 0;

	return hr.error;
}

u16 hpi_instream_close(const struct hpi_hsubsys *ph_subsys, u32 h_instream)
{
	struct hpi_message hm;
	struct hpi_response hr;

	hpi_init_message_response(&hm, &hr, HPI_OBJ_ISTREAM,
		HPI_ISTREAM_HOSTBUFFER_FREE);
	u32TOINDEXES(h_instream, &hm.adapter_index, &hm.obj_index);
	hpi_send_recv(&hm, &hr);

	hpi_init_message_response(&hm, &hr, HPI_OBJ_ISTREAM,
		HPI_ISTREAM_GROUP_RESET);
	u32TOINDEXES(h_instream, &hm.adapter_index, &hm.obj_index);
	hpi_send_recv(&hm, &hr);

	hpi_init_message_response(&hm, &hr, HPI_OBJ_ISTREAM,
		HPI_ISTREAM_CLOSE);
	u32TOINDEXES(h_instream, &hm.adapter_index, &hm.obj_index);
	hpi_send_recv(&hm, &hr);

	return hr.error;
}

u16 hpi_instream_query_format(const struct hpi_hsubsys *ph_subsys,
	u32 h_instream, const struct hpi_format *p_format)
{
	struct hpi_message hm;
	struct hpi_response hr;

	hpi_init_message_response(&hm, &hr, HPI_OBJ_ISTREAM,
		HPI_ISTREAM_QUERY_FORMAT);
	u32TOINDEXES(h_instream, &hm.adapter_index, &hm.obj_index);
	hpi_format_to_msg(&hm.u.d.u.data.format, p_format);

	hpi_send_recv(&hm, &hr);

	return hr.error;
}

u16 hpi_instream_set_format(const struct hpi_hsubsys *ph_subsys,
	u32 h_instream, const struct hpi_format *p_format)
{
	struct hpi_message hm;
	struct hpi_response hr;

	hpi_init_message_response(&hm, &hr, HPI_OBJ_ISTREAM,
		HPI_ISTREAM_SET_FORMAT);
	u32TOINDEXES(h_instream, &hm.adapter_index, &hm.obj_index);
	hpi_format_to_msg(&hm.u.d.u.data.format, p_format);

	hpi_send_recv(&hm, &hr);

	return hr.error;
}

u16 hpi_instream_read_buf(const struct hpi_hsubsys *ph_subsys, u32 h_instream,
	u8 *pb_data, u32 bytes_to_read)
{
	struct hpi_message hm;
	struct hpi_response hr;

	hpi_init_message_response(&hm, &hr, HPI_OBJ_ISTREAM,
		HPI_ISTREAM_READ);
	u32TOINDEXES(h_instream, &hm.adapter_index, &hm.obj_index);
	hm.u.d.u.data.data_size = bytes_to_read;
	hm.u.d.u.data.pb_data = pb_data;

	hpi_send_recv(&hm, &hr);

	return hr.error;
}

u16 hpi_instream_start(const struct hpi_hsubsys *ph_subsys, u32 h_instream)
{
	struct hpi_message hm;
	struct hpi_response hr;

	hpi_init_message_response(&hm, &hr, HPI_OBJ_ISTREAM,
		HPI_ISTREAM_START);
	u32TOINDEXES(h_instream, &hm.adapter_index, &hm.obj_index);

	hpi_send_recv(&hm, &hr);

	return hr.error;
}

u16 hpi_instream_wait_start(const struct hpi_hsubsys *ph_subsys,
	u32 h_instream)
{
	struct hpi_message hm;
	struct hpi_response hr;

	hpi_init_message_response(&hm, &hr, HPI_OBJ_ISTREAM,
		HPI_ISTREAM_WAIT_START);
	u32TOINDEXES(h_instream, &hm.adapter_index, &hm.obj_index);

	hpi_send_recv(&hm, &hr);

	return hr.error;
}

u16 hpi_instream_stop(const struct hpi_hsubsys *ph_subsys, u32 h_instream)
{
	struct hpi_message hm;
	struct hpi_response hr;

	hpi_init_message_response(&hm, &hr, HPI_OBJ_ISTREAM,
		HPI_ISTREAM_STOP);
	u32TOINDEXES(h_instream, &hm.adapter_index, &hm.obj_index);

	hpi_send_recv(&hm, &hr);

	return hr.error;
}

u16 hpi_instream_reset(const struct hpi_hsubsys *ph_subsys, u32 h_instream)
{
	struct hpi_message hm;
	struct hpi_response hr;

	hpi_init_message_response(&hm, &hr, HPI_OBJ_ISTREAM,
		HPI_ISTREAM_RESET);
	u32TOINDEXES(h_instream, &hm.adapter_index, &hm.obj_index);

	hpi_send_recv(&hm, &hr);

	return hr.error;
}

u16 hpi_instream_get_info_ex(const struct hpi_hsubsys *ph_subsys,
	u32 h_instream, u16 *pw_state, u32 *pbuffer_size, u32 *pdata_recorded,
	u32 *psamples_recorded, u32 *pauxiliary_data_recorded)
{
	struct hpi_message hm;
	struct hpi_response hr;
	hpi_init_message_response(&hm, &hr, HPI_OBJ_ISTREAM,
		HPI_ISTREAM_GET_INFO);
	u32TOINDEXES(h_instream, &hm.adapter_index, &hm.obj_index);

	hpi_send_recv(&hm, &hr);

	if (pw_state)
		*pw_state = hr.u.d.u.stream_info.state;
	if (pbuffer_size)
		*pbuffer_size = hr.u.d.u.stream_info.buffer_size;
	if (pdata_recorded)
		*pdata_recorded = hr.u.d.u.stream_info.data_available;
	if (psamples_recorded)
		*psamples_recorded = hr.u.d.u.stream_info.samples_transferred;
	if (pauxiliary_data_recorded)
		*pauxiliary_data_recorded =
			hr.u.d.u.stream_info.auxiliary_data_available;
	return hr.error;
}

u16 hpi_instream_ancillary_reset(const struct hpi_hsubsys *ph_subsys,
	u32 h_instream, u16 bytes_per_frame, u16 mode, u16 alignment,
	u16 idle_bit)
{
	struct hpi_message hm;
	struct hpi_response hr;
	hpi_init_message_response(&hm, &hr, HPI_OBJ_ISTREAM,
		HPI_ISTREAM_ANC_RESET);
	u32TOINDEXES(h_instream, &hm.adapter_index, &hm.obj_index);
	hm.u.d.u.data.format.attributes = bytes_per_frame;
	hm.u.d.u.data.format.format = (mode << 8) | (alignment & 0xff);
	hm.u.d.u.data.format.channels = idle_bit;
	hpi_send_recv(&hm, &hr);
	return hr.error;
}

u16 hpi_instream_ancillary_get_info(const struct hpi_hsubsys *ph_subsys,
	u32 h_instream, u32 *pframe_space)
{
	struct hpi_message hm;
	struct hpi_response hr;
	hpi_init_message_response(&hm, &hr, HPI_OBJ_ISTREAM,
		HPI_ISTREAM_ANC_GET_INFO);
	u32TOINDEXES(h_instream, &hm.adapter_index, &hm.obj_index);
	hpi_send_recv(&hm, &hr);
	if (pframe_space)
		*pframe_space =
			(hr.u.d.u.stream_info.buffer_size -
			hr.u.d.u.stream_info.data_available) /
			sizeof(struct hpi_anc_frame);
	return hr.error;
}

u16 hpi_instream_ancillary_write(const struct hpi_hsubsys *ph_subsys,
	u32 h_instream, const struct hpi_anc_frame *p_anc_frame_buffer,
	u32 anc_frame_buffer_size_in_bytes,
	u32 number_of_ancillary_frames_to_write)
{
	struct hpi_message hm;
	struct hpi_response hr;

	hpi_init_message_response(&hm, &hr, HPI_OBJ_ISTREAM,
		HPI_ISTREAM_ANC_WRITE);
	u32TOINDEXES(h_instream, &hm.adapter_index, &hm.obj_index);
	hm.u.d.u.data.pb_data = (u8 *)p_anc_frame_buffer;
	hm.u.d.u.data.data_size =
		number_of_ancillary_frames_to_write *
		sizeof(struct hpi_anc_frame);
	if (hm.u.d.u.data.data_size <= anc_frame_buffer_size_in_bytes)
		hpi_send_recv(&hm, &hr);
	else
		hr.error = HPI_ERROR_INVALID_DATA_TRANSFER;
	return hr.error;
}

u16 hpi_instream_host_buffer_allocate(const struct hpi_hsubsys *ph_subsys,
	u32 h_instream, u32 size_in_bytes)
{

	struct hpi_message hm;
	struct hpi_response hr;

	hpi_init_message_response(&hm, &hr, HPI_OBJ_ISTREAM,
		HPI_ISTREAM_HOSTBUFFER_ALLOC);
	u32TOINDEXES(h_instream, &hm.adapter_index, &hm.obj_index);
	hm.u.d.u.data.data_size = size_in_bytes;
	hpi_send_recv(&hm, &hr);
	return hr.error;
}

u16 hpi_instream_host_buffer_get_info(const struct hpi_hsubsys *ph_subsys,
	u32 h_instream, u8 **pp_buffer,
	struct hpi_hostbuffer_status **pp_status)
{
	struct hpi_message hm;
	struct hpi_response hr;

	hpi_init_message_response(&hm, &hr, HPI_OBJ_ISTREAM,
		HPI_ISTREAM_HOSTBUFFER_GET_INFO);
	u32TOINDEXES(h_instream, &hm.adapter_index, &hm.obj_index);
	hpi_send_recv(&hm, &hr);

	if (hr.error == 0) {
		if (pp_buffer)
			*pp_buffer = hr.u.d.u.hostbuffer_info.p_buffer;
		if (pp_status)
			*pp_status = hr.u.d.u.hostbuffer_info.p_status;
	}
	return hr.error;
}

u16 hpi_instream_host_buffer_free(const struct hpi_hsubsys *ph_subsys,
	u32 h_instream)
{

	struct hpi_message hm;
	struct hpi_response hr;

	hpi_init_message_response(&hm, &hr, HPI_OBJ_ISTREAM,
		HPI_ISTREAM_HOSTBUFFER_FREE);
	u32TOINDEXES(h_instream, &hm.adapter_index, &hm.obj_index);
	hpi_send_recv(&hm, &hr);
	return hr.error;
}

u16 hpi_instream_group_add(const struct hpi_hsubsys *ph_subsys,
	u32 h_instream, u32 h_stream)
{
	struct hpi_message hm;
	struct hpi_response hr;
	u16 adapter;
	char c_obj_type;

	hpi_init_message_response(&hm, &hr, HPI_OBJ_ISTREAM,
		HPI_ISTREAM_GROUP_ADD);
	hr.error = 0;
	u32TOINDEXES(h_instream, &hm.adapter_index, &hm.obj_index);
	c_obj_type = hpi_handle_object(h_stream);

	switch (c_obj_type) {
	case HPI_OBJ_OSTREAM:
		hm.u.d.u.stream.object_type = HPI_OBJ_OSTREAM;
		u32TOINDEXES(h_stream, &adapter,
			&hm.u.d.u.stream.stream_index);
		break;
	case HPI_OBJ_ISTREAM:
		hm.u.d.u.stream.object_type = HPI_OBJ_ISTREAM;
		u32TOINDEXES(h_stream, &adapter,
			&hm.u.d.u.stream.stream_index);
		break;
	default:
		return HPI_ERROR_INVALID_STREAM;
	}

	if (adapter != hm.adapter_index)
		return HPI_ERROR_NO_INTERADAPTER_GROUPS;

	hpi_send_recv(&hm, &hr);
	return hr.error;
}

u16 hpi_instream_group_get_map(const struct hpi_hsubsys *ph_subsys,
	u32 h_instream, u32 *poutstream_map, u32 *pinstream_map)
{
	struct hpi_message hm;
	struct hpi_response hr;

	hpi_init_message_response(&hm, &hr, HPI_OBJ_ISTREAM,
		HPI_ISTREAM_HOSTBUFFER_FREE);
	u32TOINDEXES(h_instream, &hm.adapter_index, &hm.obj_index);
	hpi_send_recv(&hm, &hr);

	if (poutstream_map)
		*poutstream_map = hr.u.d.u.group_info.outstream_group_map;
	if (pinstream_map)
		*pinstream_map = hr.u.d.u.group_info.instream_group_map;

	return hr.error;
}

u16 hpi_instream_group_reset(const struct hpi_hsubsys *ph_subsys,
	u32 h_instream)
{
	struct hpi_message hm;
	struct hpi_response hr;

	hpi_init_message_response(&hm, &hr, HPI_OBJ_ISTREAM,
		HPI_ISTREAM_GROUP_RESET);
	u32TOINDEXES(h_instream, &hm.adapter_index, &hm.obj_index);
	hpi_send_recv(&hm, &hr);
	return hr.error;
}

u16 hpi_mixer_open(const struct hpi_hsubsys *ph_subsys, u16 adapter_index,
	u32 *ph_mixer)
{
	struct hpi_message hm;
	struct hpi_response hr;
	hpi_init_message_response(&hm, &hr, HPI_OBJ_MIXER, HPI_MIXER_OPEN);
	hm.adapter_index = adapter_index;

	hpi_send_recv(&hm, &hr);

	if (hr.error == 0)
		*ph_mixer =
			hpi_indexes_to_handle(HPI_OBJ_MIXER, adapter_index,
			0);
	else
		*ph_mixer = 0;
	return hr.error;
}

u16 hpi_mixer_close(const struct hpi_hsubsys *ph_subsys, u32 h_mixer)
{
	struct hpi_message hm;
	struct hpi_response hr;
	hpi_init_message_response(&hm, &hr, HPI_OBJ_MIXER, HPI_MIXER_CLOSE);
	u32TOINDEX(h_mixer, &hm.adapter_index);
	hpi_send_recv(&hm, &hr);
	return hr.error;
}

u16 hpi_mixer_get_control(const struct hpi_hsubsys *ph_subsys, u32 h_mixer,
	u16 src_node_type, u16 src_node_type_index, u16 dst_node_type,
	u16 dst_node_type_index, u16 control_type, u32 *ph_control)
{
	struct hpi_message hm;
	struct hpi_response hr;
	hpi_init_message_response(&hm, &hr, HPI_OBJ_MIXER,
		HPI_MIXER_GET_CONTROL);
	u32TOINDEX(h_mixer, &hm.adapter_index);
	hm.u.m.node_type1 = src_node_type;
	hm.u.m.node_index1 = src_node_type_index;
	hm.u.m.node_type2 = dst_node_type;
	hm.u.m.node_index2 = dst_node_type_index;
	hm.u.m.control_type = control_type;

	hpi_send_recv(&hm, &hr);

	if (hr.error == 0)
		*ph_control =
			hpi_indexes_to_handle(HPI_OBJ_CONTROL,
			hm.adapter_index, hr.u.m.control_index);
	else
		*ph_control = 0;
	return hr.error;
}

u16 hpi_mixer_get_control_by_index(const struct hpi_hsubsys *ph_subsys,
	u32 h_mixer, u16 control_index, u16 *pw_src_node_type,
	u16 *pw_src_node_index, u16 *pw_dst_node_type, u16 *pw_dst_node_index,
	u16 *pw_control_type, u32 *ph_control)
{
	struct hpi_message hm;
	struct hpi_response hr;
	hpi_init_message_response(&hm, &hr, HPI_OBJ_MIXER,
		HPI_MIXER_GET_CONTROL_BY_INDEX);
	u32TOINDEX(h_mixer, &hm.adapter_index);
	hm.u.m.control_index = control_index;
	hpi_send_recv(&hm, &hr);

	if (pw_src_node_type) {
		*pw_src_node_type =
			hr.u.m.src_node_type + HPI_SOURCENODE_NONE;
		*pw_src_node_index = hr.u.m.src_node_index;
		*pw_dst_node_type = hr.u.m.dst_node_type + HPI_DESTNODE_NONE;
		*pw_dst_node_index = hr.u.m.dst_node_index;
	}
	if (pw_control_type)
		*pw_control_type = hr.u.m.control_index;

	if (ph_control) {
		if (hr.error == 0)
			*ph_control =
				hpi_indexes_to_handle(HPI_OBJ_CONTROL,
				hm.adapter_index, control_index);
		else
			*ph_control = 0;
	}
	return hr.error;
}

u16 hpi_mixer_store(const struct hpi_hsubsys *ph_subsys, u32 h_mixer,
	enum HPI_MIXER_STORE_COMMAND command, u16 index)
{
	struct hpi_message hm;
	struct hpi_response hr;
	hpi_init_message_response(&hm, &hr, HPI_OBJ_MIXER, HPI_MIXER_STORE);
	u32TOINDEX(h_mixer, &hm.adapter_index);
	hm.u.mx.store.command = command;
	hm.u.mx.store.index = index;
	hpi_send_recv(&hm, &hr);
	return hr.error;
}

static
u16 hpi_control_param_set(const struct hpi_hsubsys *ph_subsys,
	const u32 h_control, const u16 attrib, const u32 param1,
	const u32 param2)
{
	struct hpi_message hm;
	struct hpi_response hr;

	hpi_init_message_response(&hm, &hr, HPI_OBJ_CONTROL,
		HPI_CONTROL_SET_STATE);
	u32TOINDEXES(h_control, &hm.adapter_index, &hm.obj_index);
	hm.u.c.attribute = attrib;
	hm.u.c.param1 = param1;
	hm.u.c.param2 = param2;
	hpi_send_recv(&hm, &hr);
	return hr.error;
}

static u16 hpi_control_log_set2(u32 h_control, u16 attrib, short sv0,
	short sv1)
{
	struct hpi_message hm;
	struct hpi_response hr;

	hpi_init_message_response(&hm, &hr, HPI_OBJ_CONTROL,
		HPI_CONTROL_SET_STATE);
	u32TOINDEXES(h_control, &hm.adapter_index, &hm.obj_index);
	hm.u.c.attribute = attrib;
	hm.u.c.an_log_value[0] = sv0;
	hm.u.c.an_log_value[1] = sv1;
	hpi_send_recv(&hm, &hr);
	return hr.error;
}

static
u16 hpi_control_param_get(const struct hpi_hsubsys *ph_subsys,
	const u32 h_control, const u16 attrib, u32 param1, u32 param2,
	u32 *pparam1, u32 *pparam2)
{
	struct hpi_message hm;
	struct hpi_response hr;

	hpi_init_message_response(&hm, &hr, HPI_OBJ_CONTROL,
		HPI_CONTROL_GET_STATE);
	u32TOINDEXES(h_control, &hm.adapter_index, &hm.obj_index);
	hm.u.c.attribute = attrib;
	hm.u.c.param1 = param1;
	hm.u.c.param2 = param2;
	hpi_send_recv(&hm, &hr);

	*pparam1 = hr.u.c.param1;
	if (pparam2)
		*pparam2 = hr.u.c.param2;

	return hr.error;
}

#define hpi_control_param1_get(s, h, a, p1) \
		hpi_control_param_get(s, h, a, 0, 0, p1, NULL)
#define hpi_control_param2_get(s, h, a, p1, p2) \
		hpi_control_param_get(s, h, a, 0, 0, p1, p2)

static u16 hpi_control_log_get2(const struct hpi_hsubsys *ph_subsys,
	u32 h_control, u16 attrib, short *sv0, short *sv1)
{
	struct hpi_message hm;
	struct hpi_response hr;
	hpi_init_message_response(&hm, &hr, HPI_OBJ_CONTROL,
		HPI_CONTROL_GET_STATE);
	u32TOINDEXES(h_control, &hm.adapter_index, &hm.obj_index);
	hm.u.c.attribute = attrib;

	hpi_send_recv(&hm, &hr);
	*sv0 = hr.u.c.an_log_value[0];
	if (sv1)
		*sv1 = hr.u.c.an_log_value[1];
	return hr.error;
}

static
u16 hpi_control_query(const struct hpi_hsubsys *ph_subsys,
	const u32 h_control, const u16 attrib, const u32 index,
	const u32 param, u32 *psetting)
{
	struct hpi_message hm;
	struct hpi_response hr;

	hpi_init_message_response(&hm, &hr, HPI_OBJ_CONTROL,
		HPI_CONTROL_GET_INFO);
	u32TOINDEXES(h_control, &hm.adapter_index, &hm.obj_index);

	hm.u.c.attribute = attrib;
	hm.u.c.param1 = index;
	hm.u.c.param2 = param;

	hpi_send_recv(&hm, &hr);
	*psetting = hr.u.c.param1;

	return hr.error;
}

static u16 hpi_control_get_string(const u32 h_control, const u16 attribute,
	char *psz_string, const u32 string_length)
{
	unsigned int sub_string_index = 0, j = 0;
	char c = 0;
	unsigned int n = 0;
	u16 hE = 0;

	if ((string_length < 1) || (string_length > 256))
		return HPI_ERROR_INVALID_CONTROL_VALUE;
	for (sub_string_index = 0; sub_string_index < string_length;
		sub_string_index += 8) {
		struct hpi_message hm;
		struct hpi_response hr;

		hpi_init_message_response(&hm, &hr, HPI_OBJ_CONTROL,
			HPI_CONTROL_GET_STATE);
		u32TOINDEXES(h_control, &hm.adapter_index, &hm.obj_index);
		hm.u.c.attribute = attribute;
		hm.u.c.param1 = sub_string_index;
		hm.u.c.param2 = 0;
		hpi_send_recv(&hm, &hr);

		if (sub_string_index == 0
			&& (hr.u.cu.chars8.remaining_chars + 8) >
			string_length)
			return HPI_ERROR_INVALID_CONTROL_VALUE;

		if (hr.error) {
			hE = hr.error;
			break;
		}
		for (j = 0; j < 8; j++) {
			c = hr.u.cu.chars8.sz_data[j];
			psz_string[sub_string_index + j] = c;
			n++;
			if (n >= string_length) {
				psz_string[string_length - 1] = 0;
				hE = HPI_ERROR_INVALID_CONTROL_VALUE;
				break;
			}
			if (c == 0)
				break;
		}

		if ((hr.u.cu.chars8.remaining_chars == 0)
			&& ((sub_string_index + j) < string_length)
			&& (c != 0)) {
			c = 0;
			psz_string[sub_string_index + j] = c;
		}
		if (c == 0)
			break;
	}
	return hE;
}

u16 HPI_AESEBU__receiver_query_format(const struct hpi_hsubsys *ph_subsys,
	const u32 h_aes_rx, const u32 index, u16 *pw_format)
{
	u32 qr;
	u16 err;

	err = hpi_control_query(ph_subsys, h_aes_rx, HPI_AESEBURX_FORMAT,
		index, 0, &qr);
	*pw_format = (u16)qr;
	return err;
}

u16 HPI_AESEBU__receiver_set_format(const struct hpi_hsubsys *ph_subsys,
	u32 h_control, u16 format)
{
	return hpi_control_param_set(ph_subsys, h_control,
		HPI_AESEBURX_FORMAT, format, 0);
}

u16 HPI_AESEBU__receiver_get_format(const struct hpi_hsubsys *ph_subsys,
	u32 h_control, u16 *pw_format)
{
	u16 err;
	u32 param;

	err = hpi_control_param1_get(ph_subsys, h_control,
		HPI_AESEBURX_FORMAT, &param);
	if (!err && pw_format)
		*pw_format = (u16)param;

	return err;
}

u16 HPI_AESEBU__receiver_get_sample_rate(const struct hpi_hsubsys *ph_subsys,
	u32 h_control, u32 *psample_rate)
{
	return hpi_control_param1_get(ph_subsys, h_control,
		HPI_AESEBURX_SAMPLERATE, psample_rate);
}

u16 HPI_AESEBU__receiver_get_user_data(const struct hpi_hsubsys *ph_subsys,
	u32 h_control, u16 index, u16 *pw_data)
{
	struct hpi_message hm;
	struct hpi_response hr;
	hpi_init_message_response(&hm, &hr, HPI_OBJ_CONTROL,
		HPI_CONTROL_GET_STATE);
	u32TOINDEXES(h_control, &hm.adapter_index, &hm.obj_index);
	hm.u.c.attribute = HPI_AESEBURX_USERDATA;
	hm.u.c.param1 = index;

	hpi_send_recv(&hm, &hr);

	if (pw_data)
		*pw_data = (u16)hr.u.c.param2;
	return hr.error;
}

u16 HPI_AESEBU__receiver_get_channel_status(const struct hpi_hsubsys
	*ph_subsys, u32 h_control, u16 index, u16 *pw_data)
{
	struct hpi_message hm;
	struct hpi_response hr;
	hpi_init_message_response(&hm, &hr, HPI_OBJ_CONTROL,
		HPI_CONTROL_GET_STATE);
	u32TOINDEXES(h_control, &hm.adapter_index, &hm.obj_index);
	hm.u.c.attribute = HPI_AESEBURX_CHANNELSTATUS;
	hm.u.c.param1 = index;

	hpi_send_recv(&hm, &hr);

	if (pw_data)
		*pw_data = (u16)hr.u.c.param2;
	return hr.error;
}

u16 HPI_AESEBU__receiver_get_error_status(const struct hpi_hsubsys *ph_subsys,
	u32 h_control, u16 *pw_error_data)
{
	u32 error_data = 0;
	u16 error = 0;

	error = hpi_control_param1_get(ph_subsys, h_control,
		HPI_AESEBURX_ERRORSTATUS, &error_data);
	if (pw_error_data)
		*pw_error_data = (u16)error_data;
	return error;
}

u16 HPI_AESEBU__transmitter_set_sample_rate(const struct hpi_hsubsys
	*ph_subsys, u32 h_control, u32 sample_rate)
{
	return hpi_control_param_set(ph_subsys, h_control,
		HPI_AESEBUTX_SAMPLERATE, sample_rate, 0);
}

u16 HPI_AESEBU__transmitter_set_user_data(const struct hpi_hsubsys *ph_subsys,
	u32 h_control, u16 index, u16 data)
{
	return hpi_control_param_set(ph_subsys, h_control,
		HPI_AESEBUTX_USERDATA, index, data);
}

u16 HPI_AESEBU__transmitter_set_channel_status(const struct hpi_hsubsys
	*ph_subsys, u32 h_control, u16 index, u16 data)
{
	return hpi_control_param_set(ph_subsys, h_control,
		HPI_AESEBUTX_CHANNELSTATUS, index, data);
}

u16 HPI_AESEBU__transmitter_get_channel_status(const struct hpi_hsubsys
	*ph_subsys, u32 h_control, u16 index, u16 *pw_data)
{
	return HPI_ERROR_INVALID_OPERATION;
}

u16 HPI_AESEBU__transmitter_query_format(const struct hpi_hsubsys *ph_subsys,
	const u32 h_aes_tx, const u32 index, u16 *pw_format)
{
	u32 qr;
	u16 err;

	err = hpi_control_query(ph_subsys, h_aes_tx, HPI_AESEBUTX_FORMAT,
		index, 0, &qr);
	*pw_format = (u16)qr;
	return err;
}

u16 HPI_AESEBU__transmitter_set_format(const struct hpi_hsubsys *ph_subsys,
	u32 h_control, u16 output_format)
{
	return hpi_control_param_set(ph_subsys, h_control,
		HPI_AESEBUTX_FORMAT, output_format, 0);
}

u16 HPI_AESEBU__transmitter_get_format(const struct hpi_hsubsys *ph_subsys,
	u32 h_control, u16 *pw_output_format)
{
	u16 err;
	u32 param;

	err = hpi_control_param1_get(ph_subsys, h_control,
		HPI_AESEBUTX_FORMAT, &param);
	if (!err && pw_output_format)
		*pw_output_format = (u16)param;

	return err;
}

u16 hpi_bitstream_set_clock_edge(const struct hpi_hsubsys *ph_subsys,
	u32 h_control, u16 edge_type)
{
	return hpi_control_param_set(ph_subsys, h_control,
		HPI_BITSTREAM_CLOCK_EDGE, edge_type, 0);
}

u16 hpi_bitstream_set_data_polarity(const struct hpi_hsubsys *ph_subsys,
	u32 h_control, u16 polarity)
{
	return hpi_control_param_set(ph_subsys, h_control,
		HPI_BITSTREAM_DATA_POLARITY, polarity, 0);
}

u16 hpi_bitstream_get_activity(const struct hpi_hsubsys *ph_subsys,
	u32 h_control, u16 *pw_clk_activity, u16 *pw_data_activity)
{
	struct hpi_message hm;
	struct hpi_response hr;
	hpi_init_message_response(&hm, &hr, HPI_OBJ_CONTROL,
		HPI_CONTROL_GET_STATE);
	u32TOINDEXES(h_control, &hm.adapter_index, &hm.obj_index);
	hm.u.c.attribute = HPI_BITSTREAM_ACTIVITY;
	hpi_send_recv(&hm, &hr);
	if (pw_clk_activity)
		*pw_clk_activity = (u16)hr.u.c.param1;
	if (pw_data_activity)
		*pw_data_activity = (u16)hr.u.c.param2;
	return hr.error;
}

u16 hpi_channel_mode_query_mode(const struct hpi_hsubsys *ph_subsys,
	const u32 h_mode, const u32 index, u16 *pw_mode)
{
	u32 qr;
	u16 err;

	err = hpi_control_query(ph_subsys, h_mode, HPI_CHANNEL_MODE_MODE,
		index, 0, &qr);
	*pw_mode = (u16)qr;
	return err;
}

u16 hpi_channel_mode_set(const struct hpi_hsubsys *ph_subsys, u32 h_control,
	u16 mode)
{
	return hpi_control_param_set(ph_subsys, h_control,
		HPI_CHANNEL_MODE_MODE, mode, 0);
}

u16 hpi_channel_mode_get(const struct hpi_hsubsys *ph_subsys, u32 h_control,
	u16 *mode)
{
	u32 mode32 = 0;
	u16 error = hpi_control_param1_get(ph_subsys, h_control,
		HPI_CHANNEL_MODE_MODE, &mode32);
	if (mode)
		*mode = (u16)mode32;
	return error;
}

u16 hpi_cobranet_hmi_write(const struct hpi_hsubsys *ph_subsys, u32 h_control,
	u32 hmi_address, u32 byte_count, u8 *pb_data)
{
	struct hpi_message hm;
	struct hpi_response hr;

	hpi_init_message_response(&hm, &hr, HPI_OBJ_CONTROLEX,
		HPI_CONTROL_SET_STATE);
	u32TOINDEXES(h_control, &hm.adapter_index, &hm.obj_index);

	hm.u.cx.u.cobranet_data.byte_count = byte_count;
	hm.u.cx.u.cobranet_data.hmi_address = hmi_address;

	if (byte_count <= 8) {
		memcpy(hm.u.cx.u.cobranet_data.data, pb_data, byte_count);
		hm.u.cx.attribute = HPI_COBRANET_SET;
	} else {
		hm.u.cx.u.cobranet_bigdata.pb_data = pb_data;
		hm.u.cx.attribute = HPI_COBRANET_SET_DATA;
	}

	hpi_send_recv(&hm, &hr);

	return hr.error;
}

u16 hpi_cobranet_hmi_read(const struct hpi_hsubsys *ph_subsys, u32 h_control,
	u32 hmi_address, u32 max_byte_count, u32 *pbyte_count, u8 *pb_data)
{
	struct hpi_message hm;
	struct hpi_response hr;

	hpi_init_message_response(&hm, &hr, HPI_OBJ_CONTROLEX,
		HPI_CONTROL_GET_STATE);
	u32TOINDEXES(h_control, &hm.adapter_index, &hm.obj_index);

	hm.u.cx.u.cobranet_data.byte_count = max_byte_count;
	hm.u.cx.u.cobranet_data.hmi_address = hmi_address;

	if (max_byte_count <= 8) {
		hm.u.cx.attribute = HPI_COBRANET_GET;
	} else {
		hm.u.cx.u.cobranet_bigdata.pb_data = pb_data;
		hm.u.cx.attribute = HPI_COBRANET_GET_DATA;
	}

	hpi_send_recv(&hm, &hr);
	if (!hr.error && pb_data) {

		*pbyte_count = hr.u.cx.u.cobranet_data.byte_count;

		if (*pbyte_count < max_byte_count)
			max_byte_count = *pbyte_count;

		if (hm.u.cx.attribute == HPI_COBRANET_GET) {
			memcpy(pb_data, hr.u.cx.u.cobranet_data.data,
				max_byte_count);
		} else {

		}

	}
	return hr.error;
}

u16 hpi_cobranet_hmi_get_status(const struct hpi_hsubsys *ph_subsys,
	u32 h_control, u32 *pstatus, u32 *preadable_size,
	u32 *pwriteable_size)
{
	struct hpi_message hm;
	struct hpi_response hr;

	hpi_init_message_response(&hm, &hr, HPI_OBJ_CONTROLEX,
		HPI_CONTROL_GET_STATE);
	u32TOINDEXES(h_control, &hm.adapter_index, &hm.obj_index);

	hm.u.cx.attribute = HPI_COBRANET_GET_STATUS;

	hpi_send_recv(&hm, &hr);
	if (!hr.error) {
		if (pstatus)
			*pstatus = hr.u.cx.u.cobranet_status.status;
		if (preadable_size)
			*preadable_size =
				hr.u.cx.u.cobranet_status.readable_size;
		if (pwriteable_size)
			*pwriteable_size =
				hr.u.cx.u.cobranet_status.writeable_size;
	}
	return hr.error;
}

u16 hpi_cobranet_getI_paddress(const struct hpi_hsubsys *ph_subsys,
	u32 h_control, u32 *pi_paddress)
{
	u32 byte_count;
	u32 iP;
	u16 error;

	error = hpi_cobranet_hmi_read(ph_subsys, h_control,
		HPI_COBRANET_HMI_cobra_ip_mon_currentIP, 4, &byte_count,
		(u8 *)&iP);

	*pi_paddress =
		((iP & 0xff000000) >> 8) | ((iP & 0x00ff0000) << 8) | ((iP &
			0x0000ff00) >> 8) | ((iP & 0x000000ff) << 8);

	if (error)
		*pi_paddress = 0;

	return error;

}

u16 hpi_cobranet_setI_paddress(const struct hpi_hsubsys *ph_subsys,
	u32 h_control, u32 i_paddress)
{
	u32 iP;
	u16 error;

	iP = ((i_paddress & 0xff000000) >> 8) | ((i_paddress & 0x00ff0000) <<
		8) | ((i_paddress & 0x0000ff00) >> 8) | ((i_paddress &
			0x000000ff) << 8);

	error = hpi_cobranet_hmi_write(ph_subsys, h_control,
		HPI_COBRANET_HMI_cobra_ip_mon_currentIP, 4, (u8 *)&iP);

	return error;

}

u16 hpi_cobranet_get_staticI_paddress(const struct hpi_hsubsys *ph_subsys,
	u32 h_control, u32 *pi_paddress)
{
	u32 byte_count;
	u32 iP;
	u16 error;
	error = hpi_cobranet_hmi_read(ph_subsys, h_control,
		HPI_COBRANET_HMI_cobra_ip_mon_staticIP, 4, &byte_count,
		(u8 *)&iP);

	*pi_paddress =
		((iP & 0xff000000) >> 8) | ((iP & 0x00ff0000) << 8) | ((iP &
			0x0000ff00) >> 8) | ((iP & 0x000000ff) << 8);

	if (error)
		*pi_paddress = 0;

	return error;

}

u16 hpi_cobranet_set_staticI_paddress(const struct hpi_hsubsys *ph_subsys,
	u32 h_control, u32 i_paddress)
{
	u32 iP;
	u16 error;

	iP = ((i_paddress & 0xff000000) >> 8) | ((i_paddress & 0x00ff0000) <<
		8) | ((i_paddress & 0x0000ff00) >> 8) | ((i_paddress &
			0x000000ff) << 8);

	error = hpi_cobranet_hmi_write(ph_subsys, h_control,
		HPI_COBRANET_HMI_cobra_ip_mon_staticIP, 4, (u8 *)&iP);

	return error;

}

u16 hpi_cobranet_getMA_caddress(const struct hpi_hsubsys *ph_subsys,
	u32 h_control, u32 *pmAC_MS_bs, u32 *pmAC_LS_bs)
{
	u32 byte_count;
	u16 error;
	u32 mAC;

	error = hpi_cobranet_hmi_read(ph_subsys, h_control,
		HPI_COBRANET_HMI_cobra_if_phy_address, 4, &byte_count,
		(u8 *)&mAC);
	*pmAC_MS_bs =
		((mAC & 0xff000000) >> 8) | ((mAC & 0x00ff0000) << 8) | ((mAC
			& 0x0000ff00) >> 8) | ((mAC & 0x000000ff) << 8);
	error += hpi_cobranet_hmi_read(ph_subsys, h_control,
		HPI_COBRANET_HMI_cobra_if_phy_address + 1, 4, &byte_count,
		(u8 *)&mAC);
	*pmAC_LS_bs =
		((mAC & 0xff000000) >> 8) | ((mAC & 0x00ff0000) << 8) | ((mAC
			& 0x0000ff00) >> 8) | ((mAC & 0x000000ff) << 8);

	if (error) {
		*pmAC_MS_bs = 0;
		*pmAC_LS_bs = 0;
	}

	return error;
}

u16 hpi_compander_set_enable(const struct hpi_hsubsys *ph_subsys,
	u32 h_control, u32 enable)
{
	return hpi_control_param_set(ph_subsys, h_control, HPI_GENERIC_ENABLE,
		enable, 0);
}

u16 hpi_compander_get_enable(const struct hpi_hsubsys *ph_subsys,
	u32 h_control, u32 *enable)
{
	return hpi_control_param1_get(ph_subsys, h_control,
		HPI_GENERIC_ENABLE, enable);
}

u16 hpi_compander_set_makeup_gain(const struct hpi_hsubsys *ph_subsys,
	u32 h_control, short makeup_gain0_01dB)
{
	return hpi_control_log_set2(h_control, HPI_COMPANDER_MAKEUPGAIN,
		makeup_gain0_01dB, 0);
}

u16 hpi_compander_get_makeup_gain(const struct hpi_hsubsys *ph_subsys,
	u32 h_control, short *makeup_gain0_01dB)
{
	return hpi_control_log_get2(ph_subsys, h_control,
		HPI_COMPANDER_MAKEUPGAIN, makeup_gain0_01dB, NULL);
}

u16 hpi_compander_set_attack_time_constant(const struct hpi_hsubsys
	*ph_subsys, u32 h_control, unsigned int index, u32 attack)
{
	return hpi_control_param_set(ph_subsys, h_control,
		HPI_COMPANDER_ATTACK, attack, index);
}

u16 hpi_compander_get_attack_time_constant(const struct hpi_hsubsys
	*ph_subsys, u32 h_control, unsigned int index, u32 *attack)
{
	return hpi_control_param_get(ph_subsys, h_control,
		HPI_COMPANDER_ATTACK, 0, index, attack, NULL);
}

u16 hpi_compander_set_decay_time_constant(const struct hpi_hsubsys *ph_subsys,
	u32 h_control, unsigned int index, u32 decay)
{
	return hpi_control_param_set(ph_subsys, h_control,
		HPI_COMPANDER_DECAY, decay, index);
}

u16 hpi_compander_get_decay_time_constant(const struct hpi_hsubsys *ph_subsys,
	u32 h_control, unsigned int index, u32 *decay)
{
	return hpi_control_param_get(ph_subsys, h_control,
		HPI_COMPANDER_DECAY, 0, index, decay, NULL);

}

u16 hpi_compander_set_threshold(const struct hpi_hsubsys *ph_subsys,
	u32 h_control, unsigned int index, short threshold0_01dB)
{
	struct hpi_message hm;
	struct hpi_response hr;

	hpi_init_message_response(&hm, &hr, HPI_OBJ_CONTROL,
		HPI_CONTROL_SET_STATE);
	u32TOINDEXES(h_control, &hm.adapter_index, &hm.obj_index);
	hm.u.c.attribute = HPI_COMPANDER_THRESHOLD;
	hm.u.c.param2 = index;
	hm.u.c.an_log_value[0] = threshold0_01dB;

	hpi_send_recv(&hm, &hr);

	return hr.error;
}

u16 hpi_compander_get_threshold(const struct hpi_hsubsys *ph_subsys,
	u32 h_control, unsigned int index, short *threshold0_01dB)
{
	struct hpi_message hm;
	struct hpi_response hr;

	hpi_init_message_response(&hm, &hr, HPI_OBJ_CONTROL,
		HPI_CONTROL_GET_STATE);
	u32TOINDEXES(h_control, &hm.adapter_index, &hm.obj_index);
	hm.u.c.attribute = HPI_COMPANDER_THRESHOLD;
	hm.u.c.param2 = index;

	hpi_send_recv(&hm, &hr);
	*threshold0_01dB = hr.u.c.an_log_value[0];

	return hr.error;
}

u16 hpi_compander_set_ratio(const struct hpi_hsubsys *ph_subsys,
	u32 h_control, u32 index, u32 ratio100)
{
	return hpi_control_param_set(ph_subsys, h_control,
		HPI_COMPANDER_RATIO, ratio100, index);
}

u16 hpi_compander_get_ratio(const struct hpi_hsubsys *ph_subsys,
	u32 h_control, u32 index, u32 *ratio100)
{
	return hpi_control_param_get(ph_subsys, h_control,
		HPI_COMPANDER_RATIO, 0, index, ratio100, NULL);
}

u16 hpi_level_query_range(const struct hpi_hsubsys *ph_subsys, u32 h_control,
	short *min_gain_01dB, short *max_gain_01dB, short *step_gain_01dB)
{
	struct hpi_message hm;
	struct hpi_response hr;

	hpi_init_message_response(&hm, &hr, HPI_OBJ_CONTROL,
		HPI_CONTROL_GET_STATE);
	u32TOINDEXES(h_control, &hm.adapter_index, &hm.obj_index);
	hm.u.c.attribute = HPI_LEVEL_RANGE;

	hpi_send_recv(&hm, &hr);
	if (hr.error) {
		hr.u.c.an_log_value[0] = 0;
		hr.u.c.an_log_value[1] = 0;
		hr.u.c.param1 = 0;
	}
	if (min_gain_01dB)
		*min_gain_01dB = hr.u.c.an_log_value[0];
	if (max_gain_01dB)
		*max_gain_01dB = hr.u.c.an_log_value[1];
	if (step_gain_01dB)
		*step_gain_01dB = (short)hr.u.c.param1;
	return hr.error;
}

u16 hpi_level_set_gain(const struct hpi_hsubsys *ph_subsys, u32 h_control,
	short an_gain0_01dB[HPI_MAX_CHANNELS]
	)
{
	return hpi_control_log_set2(h_control, HPI_LEVEL_GAIN,
		an_gain0_01dB[0], an_gain0_01dB[1]);
}

u16 hpi_level_get_gain(const struct hpi_hsubsys *ph_subsys, u32 h_control,
	short an_gain0_01dB[HPI_MAX_CHANNELS]
	)
{
	return hpi_control_log_get2(ph_subsys, h_control, HPI_LEVEL_GAIN,
		&an_gain0_01dB[0], &an_gain0_01dB[1]);
}

u16 hpi_meter_query_channels(const struct hpi_hsubsys *ph_subsys,
	const u32 h_meter, u32 *p_channels)
{
	return hpi_control_query(ph_subsys, h_meter, HPI_METER_NUM_CHANNELS,
		0, 0, p_channels);
}

u16 hpi_meter_get_peak(const struct hpi_hsubsys *ph_subsys, u32 h_control,
	short an_peakdB[HPI_MAX_CHANNELS]
	)
{
	short i = 0;

	struct hpi_message hm;
	struct hpi_response hr;

	hpi_init_message_response(&hm, &hr, HPI_OBJ_CONTROL,
		HPI_CONTROL_GET_STATE);
	u32TOINDEXES(h_control, &hm.adapter_index, &hm.obj_index);
	hm.obj_index = hm.obj_index;
	hm.u.c.attribute = HPI_METER_PEAK;

	hpi_send_recv(&hm, &hr);

	if (!hr.error)
		memcpy(an_peakdB, hr.u.c.an_log_value,
			sizeof(short) * HPI_MAX_CHANNELS);
	else
		for (i = 0; i < HPI_MAX_CHANNELS; i++)
			an_peakdB[i] = HPI_METER_MINIMUM;
	return hr.error;
}

u16 hpi_meter_get_rms(const struct hpi_hsubsys *ph_subsys, u32 h_control,
	short an_rmsdB[HPI_MAX_CHANNELS]
	)
{
	short i = 0;

	struct hpi_message hm;
	struct hpi_response hr;

	hpi_init_message_response(&hm, &hr, HPI_OBJ_CONTROL,
		HPI_CONTROL_GET_STATE);
	u32TOINDEXES(h_control, &hm.adapter_index, &hm.obj_index);
	hm.u.c.attribute = HPI_METER_RMS;

	hpi_send_recv(&hm, &hr);

	if (!hr.error)
		memcpy(an_rmsdB, hr.u.c.an_log_value,
			sizeof(short) * HPI_MAX_CHANNELS);
	else
		for (i = 0; i < HPI_MAX_CHANNELS; i++)
			an_rmsdB[i] = HPI_METER_MINIMUM;

	return hr.error;
}

u16 hpi_meter_set_rms_ballistics(const struct hpi_hsubsys *ph_subsys,
	u32 h_control, u16 attack, u16 decay)
{
	return hpi_control_param_set(ph_subsys, h_control,
		HPI_METER_RMS_BALLISTICS, attack, decay);
}

u16 hpi_meter_get_rms_ballistics(const struct hpi_hsubsys *ph_subsys,
	u32 h_control, u16 *pn_attack, u16 *pn_decay)
{
	u32 attack;
	u32 decay;
	u16 error;

	error = hpi_control_param2_get(ph_subsys, h_control,
		HPI_METER_RMS_BALLISTICS, &attack, &decay);

	if (pn_attack)
		*pn_attack = (unsigned short)attack;
	if (pn_decay)
		*pn_decay = (unsigned short)decay;

	return error;
}

u16 hpi_meter_set_peak_ballistics(const struct hpi_hsubsys *ph_subsys,
	u32 h_control, u16 attack, u16 decay)
{
	return hpi_control_param_set(ph_subsys, h_control,
		HPI_METER_PEAK_BALLISTICS, attack, decay);
}

u16 hpi_meter_get_peak_ballistics(const struct hpi_hsubsys *ph_subsys,
	u32 h_control, u16 *pn_attack, u16 *pn_decay)
{
	u32 attack;
	u32 decay;
	u16 error;

	error = hpi_control_param2_get(ph_subsys, h_control,
		HPI_METER_PEAK_BALLISTICS, &attack, &decay);

	if (pn_attack)
		*pn_attack = (short)attack;
	if (pn_decay)
		*pn_decay = (short)decay;

	return error;
}

u16 hpi_microphone_set_phantom_power(const struct hpi_hsubsys *ph_subsys,
	u32 h_control, u16 on_off)
{
	return hpi_control_param_set(ph_subsys, h_control,
		HPI_MICROPHONE_PHANTOM_POWER, (u32)on_off, 0);
}

u16 hpi_microphone_get_phantom_power(const struct hpi_hsubsys *ph_subsys,
	u32 h_control, u16 *pw_on_off)
{
	u16 error = 0;
	u32 on_off = 0;
	error = hpi_control_param1_get(ph_subsys, h_control,
		HPI_MICROPHONE_PHANTOM_POWER, &on_off);
	if (pw_on_off)
		*pw_on_off = (u16)on_off;
	return error;
}

u16 hpi_multiplexer_set_source(const struct hpi_hsubsys *ph_subsys,
	u32 h_control, u16 source_node_type, u16 source_node_index)
{
	return hpi_control_param_set(ph_subsys, h_control,
		HPI_MULTIPLEXER_SOURCE, source_node_type, source_node_index);
}

u16 hpi_multiplexer_get_source(const struct hpi_hsubsys *ph_subsys,
	u32 h_control, u16 *source_node_type, u16 *source_node_index)
{
	u32 node, index;
	u16 error = hpi_control_param2_get(ph_subsys, h_control,
		HPI_MULTIPLEXER_SOURCE, &node,
		&index);
	if (source_node_type)
		*source_node_type = (u16)node;
	if (source_node_index)
		*source_node_index = (u16)index;
	return error;
}

u16 hpi_multiplexer_query_source(const struct hpi_hsubsys *ph_subsys,
	u32 h_control, u16 index, u16 *source_node_type,
	u16 *source_node_index)
{
	struct hpi_message hm;
	struct hpi_response hr;
	hpi_init_message_response(&hm, &hr, HPI_OBJ_CONTROL,
		HPI_CONTROL_GET_STATE);
	u32TOINDEXES(h_control, &hm.adapter_index, &hm.obj_index);
	hm.u.c.attribute = HPI_MULTIPLEXER_QUERYSOURCE;
	hm.u.c.param1 = index;

	hpi_send_recv(&hm, &hr);

	if (source_node_type)
		*source_node_type = (u16)hr.u.c.param1;
	if (source_node_index)
		*source_node_index = (u16)hr.u.c.param2;
	return hr.error;
}

u16 hpi_parametricEQ__get_info(const struct hpi_hsubsys *ph_subsys,
	u32 h_control, u16 *pw_number_of_bands, u16 *pw_on_off)
{
	u32 oB = 0;
	u32 oO = 0;
	u16 error = 0;

	error = hpi_control_param2_get(ph_subsys, h_control,
		HPI_EQUALIZER_NUM_FILTERS, &oO, &oB);
	if (pw_number_of_bands)
		*pw_number_of_bands = (u16)oB;
	if (pw_on_off)
		*pw_on_off = (u16)oO;
	return error;
}

u16 hpi_parametricEQ__set_state(const struct hpi_hsubsys *ph_subsys,
	u32 h_control, u16 on_off)
{
	return hpi_control_param_set(ph_subsys, h_control,
		HPI_EQUALIZER_NUM_FILTERS, on_off, 0);
}

u16 hpi_parametricEQ__get_band(const struct hpi_hsubsys *ph_subsys,
	u32 h_control, u16 index, u16 *pn_type, u32 *pfrequency_hz,
	short *pnQ100, short *pn_gain0_01dB)
{
	struct hpi_message hm;
	struct hpi_response hr;

	hpi_init_message_response(&hm, &hr, HPI_OBJ_CONTROL,
		HPI_CONTROL_GET_STATE);
	u32TOINDEXES(h_control, &hm.adapter_index, &hm.obj_index);
	hm.u.c.attribute = HPI_EQUALIZER_FILTER;
	hm.u.c.param2 = index;

	hpi_send_recv(&hm, &hr);

	if (pfrequency_hz)
		*pfrequency_hz = hr.u.c.param1;
	if (pn_type)
		*pn_type = (u16)(hr.u.c.param2 >> 16);
	if (pnQ100)
		*pnQ100 = hr.u.c.an_log_value[1];
	if (pn_gain0_01dB)
		*pn_gain0_01dB = hr.u.c.an_log_value[0];

	return hr.error;
}

u16 hpi_parametricEQ__set_band(const struct hpi_hsubsys *ph_subsys,
	u32 h_control, u16 index, u16 type, u32 frequency_hz, short q100,
	short gain0_01dB)
{
	struct hpi_message hm;
	struct hpi_response hr;

	hpi_init_message_response(&hm, &hr, HPI_OBJ_CONTROL,
		HPI_CONTROL_SET_STATE);
	u32TOINDEXES(h_control, &hm.adapter_index, &hm.obj_index);

	hm.u.c.param1 = frequency_hz;
	hm.u.c.param2 = (index & 0xFFFFL) + ((u32)type << 16);
	hm.u.c.an_log_value[0] = gain0_01dB;
	hm.u.c.an_log_value[1] = q100;
	hm.u.c.attribute = HPI_EQUALIZER_FILTER;

	hpi_send_recv(&hm, &hr);

	return hr.error;
}

u16 hpi_parametricEQ__get_coeffs(const struct hpi_hsubsys *ph_subsys,
	u32 h_control, u16 index, short coeffs[5]
	)
{
	struct hpi_message hm;
	struct hpi_response hr;

	hpi_init_message_response(&hm, &hr, HPI_OBJ_CONTROL,
		HPI_CONTROL_GET_STATE);
	u32TOINDEXES(h_control, &hm.adapter_index, &hm.obj_index);
	hm.u.c.attribute = HPI_EQUALIZER_COEFFICIENTS;
	hm.u.c.param2 = index;

	hpi_send_recv(&hm, &hr);

	coeffs[0] = (short)hr.u.c.an_log_value[0];
	coeffs[1] = (short)hr.u.c.an_log_value[1];
	coeffs[2] = (short)hr.u.c.param1;
	coeffs[3] = (short)(hr.u.c.param1 >> 16);
	coeffs[4] = (short)hr.u.c.param2;

	return hr.error;
}

u16 hpi_sample_clock_query_source(const struct hpi_hsubsys *ph_subsys,
	const u32 h_clock, const u32 index, u16 *pw_source)
{
	u32 qr;
	u16 err;

	err = hpi_control_query(ph_subsys, h_clock, HPI_SAMPLECLOCK_SOURCE,
		index, 0, &qr);
	*pw_source = (u16)qr;
	return err;
}

u16 hpi_sample_clock_set_source(const struct hpi_hsubsys *ph_subsys,
	u32 h_control, u16 source)
{
	return hpi_control_param_set(ph_subsys, h_control,
		HPI_SAMPLECLOCK_SOURCE, source, 0);
}

u16 hpi_sample_clock_get_source(const struct hpi_hsubsys *ph_subsys,
	u32 h_control, u16 *pw_source)
{
	u16 error = 0;
	u32 source = 0;
	error = hpi_control_param1_get(ph_subsys, h_control,
		HPI_SAMPLECLOCK_SOURCE, &source);
	if (!error)
		if (pw_source)
			*pw_source = (u16)source;
	return error;
}

u16 hpi_sample_clock_query_source_index(const struct hpi_hsubsys *ph_subsys,
	const u32 h_clock, const u32 index, const u32 source,
	u16 *pw_source_index)
{
	u32 qr;
	u16 err;

	err = hpi_control_query(ph_subsys, h_clock,
		HPI_SAMPLECLOCK_SOURCE_INDEX, index, source, &qr);
	*pw_source_index = (u16)qr;
	return err;
}

u16 hpi_sample_clock_set_source_index(const struct hpi_hsubsys *ph_subsys,
	u32 h_control, u16 source_index)
{
	return hpi_control_param_set(ph_subsys, h_control,
		HPI_SAMPLECLOCK_SOURCE_INDEX, source_index, 0);
}

u16 hpi_sample_clock_get_source_index(const struct hpi_hsubsys *ph_subsys,
	u32 h_control, u16 *pw_source_index)
{
	u16 error = 0;
	u32 source_index = 0;
	error = hpi_control_param1_get(ph_subsys, h_control,
		HPI_SAMPLECLOCK_SOURCE_INDEX, &source_index);
	if (!error)
		if (pw_source_index)
			*pw_source_index = (u16)source_index;
	return error;
}

u16 hpi_sample_clock_query_local_rate(const struct hpi_hsubsys *ph_subsys,
	const u32 h_clock, const u32 index, u32 *prate)
{
	u16 err;
	err = hpi_control_query(ph_subsys, h_clock,
		HPI_SAMPLECLOCK_LOCAL_SAMPLERATE, index, 0, prate);

	return err;
}

u16 hpi_sample_clock_set_local_rate(const struct hpi_hsubsys *ph_subsys,
	u32 h_control, u32 sample_rate)
{
	return hpi_control_param_set(ph_subsys, h_control,
		HPI_SAMPLECLOCK_LOCAL_SAMPLERATE, sample_rate, 0);
}

u16 hpi_sample_clock_get_local_rate(const struct hpi_hsubsys *ph_subsys,
	u32 h_control, u32 *psample_rate)
{
	u16 error = 0;
	u32 sample_rate = 0;
	error = hpi_control_param1_get(ph_subsys, h_control,
		HPI_SAMPLECLOCK_LOCAL_SAMPLERATE, &sample_rate);
	if (!error)
		if (psample_rate)
			*psample_rate = sample_rate;
	return error;
}

u16 hpi_sample_clock_get_sample_rate(const struct hpi_hsubsys *ph_subsys,
	u32 h_control, u32 *psample_rate)
{
	u16 error = 0;
	u32 sample_rate = 0;
	error = hpi_control_param1_get(ph_subsys, h_control,
		HPI_SAMPLECLOCK_SAMPLERATE, &sample_rate);
	if (!error)
		if (psample_rate)
			*psample_rate = sample_rate;
	return error;
}

u16 hpi_sample_clock_set_auto(const struct hpi_hsubsys *ph_subsys,
	u32 h_control, u32 enable)
{
	return hpi_control_param_set(ph_subsys, h_control,
		HPI_SAMPLECLOCK_AUTO, enable, 0);
}

u16 hpi_sample_clock_get_auto(const struct hpi_hsubsys *ph_subsys,
	u32 h_control, u32 *penable)
{
	return hpi_control_param1_get(ph_subsys, h_control,
		HPI_SAMPLECLOCK_AUTO, penable);
}

u16 hpi_sample_clock_set_local_rate_lock(const struct hpi_hsubsys *ph_subsys,
	u32 h_control, u32 lock)
{
	return hpi_control_param_set(ph_subsys, h_control,
		HPI_SAMPLECLOCK_LOCAL_LOCK, lock, 0);
}

u16 hpi_sample_clock_get_local_rate_lock(const struct hpi_hsubsys *ph_subsys,
	u32 h_control, u32 *plock)
{
	return hpi_control_param1_get(ph_subsys, h_control,
		HPI_SAMPLECLOCK_LOCAL_LOCK, plock);
}

u16 hpi_tone_detector_get_frequency(const struct hpi_hsubsys *ph_subsys,
	u32 h_control, u32 index, u32 *frequency)
{
	return hpi_control_param_get(ph_subsys, h_control,
		HPI_TONEDETECTOR_FREQUENCY, index, 0, frequency, NULL);
}

u16 hpi_tone_detector_get_state(const struct hpi_hsubsys *ph_subsys,
	u32 h_control, u32 *state)
{
	return hpi_control_param1_get(ph_subsys, h_control,
		HPI_TONEDETECTOR_STATE, state);
}

u16 hpi_tone_detector_set_enable(const struct hpi_hsubsys *ph_subsys,
	u32 h_control, u32 enable)
{
	return hpi_control_param_set(ph_subsys, h_control, HPI_GENERIC_ENABLE,
		(u32)enable, 0);
}

u16 hpi_tone_detector_get_enable(const struct hpi_hsubsys *ph_subsys,
	u32 h_control, u32 *enable)
{
	return hpi_control_param1_get(ph_subsys, h_control,
		HPI_GENERIC_ENABLE, enable);
}

u16 hpi_tone_detector_set_event_enable(const struct hpi_hsubsys *ph_subsys,
	u32 h_control, u32 event_enable)
{
	return hpi_control_param_set(ph_subsys, h_control,
		HPI_GENERIC_EVENT_ENABLE, (u32)event_enable, 0);
}

u16 hpi_tone_detector_get_event_enable(const struct hpi_hsubsys *ph_subsys,
	u32 h_control, u32 *event_enable)
{
	return hpi_control_param1_get(ph_subsys, h_control,
		HPI_GENERIC_EVENT_ENABLE, event_enable);
}

u16 hpi_tone_detector_set_threshold(const struct hpi_hsubsys *ph_subsys,
	u32 h_control, int threshold)
{
	return hpi_control_param_set(ph_subsys, h_control,
		HPI_TONEDETECTOR_THRESHOLD, (u32)threshold, 0);
}

u16 hpi_tone_detector_get_threshold(const struct hpi_hsubsys *ph_subsys,
	u32 h_control, int *threshold)
{
	return hpi_control_param1_get(ph_subsys, h_control,
		HPI_TONEDETECTOR_THRESHOLD, (u32 *)threshold);
}

u16 hpi_silence_detector_get_state(const struct hpi_hsubsys *ph_subsys,
	u32 h_control, u32 *state)
{
	return hpi_control_param1_get(ph_subsys, h_control,
		HPI_SILENCEDETECTOR_STATE, state);
}

u16 hpi_silence_detector_set_enable(const struct hpi_hsubsys *ph_subsys,
	u32 h_control, u32 enable)
{
	return hpi_control_param_set(ph_subsys, h_control, HPI_GENERIC_ENABLE,
		(u32)enable, 0);
}

u16 hpi_silence_detector_get_enable(const struct hpi_hsubsys *ph_subsys,
	u32 h_control, u32 *enable)
{
	return hpi_control_param1_get(ph_subsys, h_control,
		HPI_GENERIC_ENABLE, enable);
}

u16 hpi_silence_detector_set_event_enable(const struct hpi_hsubsys *ph_subsys,
	u32 h_control, u32 event_enable)
{
	return hpi_control_param_set(ph_subsys, h_control,
		HPI_GENERIC_EVENT_ENABLE, event_enable, 0);
}

u16 hpi_silence_detector_get_event_enable(const struct hpi_hsubsys *ph_subsys,
	u32 h_control, u32 *event_enable)
{
	return hpi_control_param1_get(ph_subsys, h_control,
		HPI_GENERIC_EVENT_ENABLE, event_enable);
}

u16 hpi_silence_detector_set_delay(const struct hpi_hsubsys *ph_subsys,
	u32 h_control, u32 delay)
{
	return hpi_control_param_set(ph_subsys, h_control,
		HPI_SILENCEDETECTOR_DELAY, delay, 0);
}

u16 hpi_silence_detector_get_delay(const struct hpi_hsubsys *ph_subsys,
	u32 h_control, u32 *delay)
{
	return hpi_control_param1_get(ph_subsys, h_control,
		HPI_SILENCEDETECTOR_DELAY, delay);
}

u16 hpi_silence_detector_set_threshold(const struct hpi_hsubsys *ph_subsys,
	u32 h_control, int threshold)
{
	return hpi_control_param_set(ph_subsys, h_control,
		HPI_SILENCEDETECTOR_THRESHOLD, threshold, 0);
}

u16 hpi_silence_detector_get_threshold(const struct hpi_hsubsys *ph_subsys,
	u32 h_control, int *threshold)
{
	return hpi_control_param1_get(ph_subsys, h_control,
		HPI_SILENCEDETECTOR_THRESHOLD, (u32 *)threshold);
}

u16 hpi_tuner_query_band(const struct hpi_hsubsys *ph_subsys,
	const u32 h_tuner, const u32 index, u16 *pw_band)
{
	u32 qr;
	u16 err;

	err = hpi_control_query(ph_subsys, h_tuner, HPI_TUNER_BAND, index, 0,
		&qr);
	*pw_band = (u16)qr;
	return err;
}

u16 hpi_tuner_set_band(const struct hpi_hsubsys *ph_subsys, u32 h_control,
	u16 band)
{
	return hpi_control_param_set(ph_subsys, h_control, HPI_TUNER_BAND,
		band, 0);
}

u16 hpi_tuner_get_band(const struct hpi_hsubsys *ph_subsys, u32 h_control,
	u16 *pw_band)
{
	u32 band = 0;
	u16 error = 0;

	error = hpi_control_param1_get(ph_subsys, h_control, HPI_TUNER_BAND,
		&band);
	if (pw_band)
		*pw_band = (u16)band;
	return error;
}

u16 hpi_tuner_query_frequency(const struct hpi_hsubsys *ph_subsys,
	const u32 h_tuner, const u32 index, const u16 band, u32 *pfreq)
{
	return hpi_control_query(ph_subsys, h_tuner, HPI_TUNER_FREQ, index,
		band, pfreq);
}

u16 hpi_tuner_set_frequency(const struct hpi_hsubsys *ph_subsys,
	u32 h_control, u32 freq_ink_hz)
{
	return hpi_control_param_set(ph_subsys, h_control, HPI_TUNER_FREQ,
		freq_ink_hz, 0);
}

u16 hpi_tuner_get_frequency(const struct hpi_hsubsys *ph_subsys,
	u32 h_control, u32 *pw_freq_ink_hz)
{
	return hpi_control_param1_get(ph_subsys, h_control, HPI_TUNER_FREQ,
		pw_freq_ink_hz);
}

u16 hpi_tuner_query_gain(const struct hpi_hsubsys *ph_subsys,
	const u32 h_tuner, const u32 index, u16 *pw_gain)
{
	u32 qr;
	u16 err;

	err = hpi_control_query(ph_subsys, h_tuner, HPI_TUNER_BAND, index, 0,
		&qr);
	*pw_gain = (u16)qr;
	return err;
}

u16 hpi_tuner_set_gain(const struct hpi_hsubsys *ph_subsys, u32 h_control,
	short gain)
{
	return hpi_control_param_set(ph_subsys, h_control, HPI_TUNER_GAIN,
		gain, 0);
}

u16 hpi_tuner_get_gain(const struct hpi_hsubsys *ph_subsys, u32 h_control,
	short *pn_gain)
{
	u32 gain = 0;
	u16 error = 0;

	error = hpi_control_param1_get(ph_subsys, h_control, HPI_TUNER_GAIN,
		&gain);
	if (pn_gain)
		*pn_gain = (u16)gain;
	return error;
}

u16 hpi_tuner_getRF_level(const struct hpi_hsubsys *ph_subsys, u32 h_control,
	short *pw_level)
{
	struct hpi_message hm;
	struct hpi_response hr;

	hpi_init_message_response(&hm, &hr, HPI_OBJ_CONTROL,
		HPI_CONTROL_GET_STATE);
	u32TOINDEXES(h_control, &hm.adapter_index, &hm.obj_index);
	hm.u.c.attribute = HPI_TUNER_LEVEL;
	hm.u.c.param1 = HPI_TUNER_LEVEL_AVERAGE;
	hpi_send_recv(&hm, &hr);
	if (pw_level)
		*pw_level = (short)hr.u.c.param1;
	return hr.error;
}

u16 hpi_tuner_get_rawRF_level(const struct hpi_hsubsys *ph_subsys,
	u32 h_control, short *pw_level)
{
	struct hpi_message hm;
	struct hpi_response hr;

	hpi_init_message_response(&hm, &hr, HPI_OBJ_CONTROL,
		HPI_CONTROL_GET_STATE);
	u32TOINDEXES(h_control, &hm.adapter_index, &hm.obj_index);
	hm.u.c.attribute = HPI_TUNER_LEVEL;
	hm.u.c.param1 = HPI_TUNER_LEVEL_RAW;
	hpi_send_recv(&hm, &hr);
	if (pw_level)
		*pw_level = (short)hr.u.c.param1;
	return hr.error;
}

u16 hpi_tuner_query_deemphasis(const struct hpi_hsubsys *ph_subsys,
	const u32 h_tuner, const u32 index, const u16 band, u32 *pdeemphasis)
{
	return hpi_control_query(ph_subsys, h_tuner, HPI_TUNER_DEEMPHASIS,
		index, band, pdeemphasis);
}

u16 hpi_tuner_set_deemphasis(const struct hpi_hsubsys *ph_subsys,
	u32 h_control, u32 deemphasis)
{
	return hpi_control_param_set(ph_subsys, h_control,
		HPI_TUNER_DEEMPHASIS, deemphasis, 0);
}

u16 hpi_tuner_get_deemphasis(const struct hpi_hsubsys *ph_subsys,
	u32 h_control, u32 *pdeemphasis)
{
	return hpi_control_param1_get(ph_subsys, h_control,
		HPI_TUNER_DEEMPHASIS, pdeemphasis);
}

u16 hpi_tuner_query_program(const struct hpi_hsubsys *ph_subsys,
	const u32 h_tuner, u32 *pbitmap_program)
{
	return hpi_control_query(ph_subsys, h_tuner, HPI_TUNER_PROGRAM, 0, 0,
		pbitmap_program);
}

u16 hpi_tuner_set_program(const struct hpi_hsubsys *ph_subsys, u32 h_control,
	u32 program)
{
	return hpi_control_param_set(ph_subsys, h_control, HPI_TUNER_PROGRAM,
		program, 0);
}

u16 hpi_tuner_get_program(const struct hpi_hsubsys *ph_subsys, u32 h_control,
	u32 *pprogram)
{
	return hpi_control_param1_get(ph_subsys, h_control, HPI_TUNER_PROGRAM,
		pprogram);
}

u16 hpi_tuner_get_hd_radio_dsp_version(const struct hpi_hsubsys *ph_subsys,
	u32 h_control, char *psz_dsp_version, const u32 string_size)
{
	return hpi_control_get_string(h_control,
		HPI_TUNER_HDRADIO_DSP_VERSION, psz_dsp_version, string_size);
}

u16 hpi_tuner_get_hd_radio_sdk_version(const struct hpi_hsubsys *ph_subsys,
	u32 h_control, char *psz_sdk_version, const u32 string_size)
{
	return hpi_control_get_string(h_control,
		HPI_TUNER_HDRADIO_SDK_VERSION, psz_sdk_version, string_size);
}

u16 hpi_tuner_get_status(const struct hpi_hsubsys *ph_subsys, u32 h_control,
	u16 *pw_status_mask, u16 *pw_status)
{
	u32 status = 0;
	u16 error = 0;

	error = hpi_control_param1_get(ph_subsys, h_control, HPI_TUNER_STATUS,
		&status);
	if (pw_status) {
		if (!error) {
			*pw_status_mask = (u16)(status >> 16);
			*pw_status = (u16)(status & 0xFFFF);
		} else {
			*pw_status_mask = 0;
			*pw_status = 0;
		}
	}
	return error;
}

u16 hpi_tuner_set_mode(const struct hpi_hsubsys *ph_subsys, u32 h_control,
	u32 mode, u32 value)
{
	return hpi_control_param_set(ph_subsys, h_control, HPI_TUNER_MODE,
		mode, value);
}

u16 hpi_tuner_get_mode(const struct hpi_hsubsys *ph_subsys, u32 h_control,
	u32 mode, u32 *pn_value)
{
	return hpi_control_param_get(ph_subsys, h_control, HPI_TUNER_MODE,
		mode, 0, pn_value, NULL);
}

u16 hpi_tuner_get_hd_radio_signal_quality(const struct hpi_hsubsys *ph_subsys,
	u32 h_control, u32 *pquality)
{
	return hpi_control_param1_get(ph_subsys, h_control,
		HPI_TUNER_HDRADIO_SIGNAL_QUALITY, pquality);
}

u16 hpi_tuner_get_hd_radio_signal_blend(const struct hpi_hsubsys *ph_subsys,
	u32 h_control, u32 *pblend)
{
	return hpi_control_param1_get(ph_subsys, h_control,
		HPI_TUNER_HDRADIO_BLEND, pblend);
}

u16 hpi_tuner_set_hd_radio_signal_blend(const struct hpi_hsubsys *ph_subsys,
	u32 h_control, const u32 blend)
{
	return hpi_control_param_set(ph_subsys, h_control,
		HPI_TUNER_HDRADIO_BLEND, blend, 0);
}

u16 hpi_tuner_getRDS(const struct hpi_hsubsys *ph_subsys, u32 h_control,
	char *p_data)
{
	struct hpi_message hm;
	struct hpi_response hr;

	hpi_init_message_response(&hm, &hr, HPI_OBJ_CONTROL,
		HPI_CONTROL_GET_STATE);
	u32TOINDEXES(h_control, &hm.adapter_index, &hm.obj_index);
	hm.u.c.attribute = HPI_TUNER_RDS;
	hpi_send_recv(&hm, &hr);
	if (p_data) {
		*(u32 *)&p_data[0] = hr.u.cu.tuner.rds.data[0];
		*(u32 *)&p_data[4] = hr.u.cu.tuner.rds.data[1];
		*(u32 *)&p_data[8] = hr.u.cu.tuner.rds.bLER;
	}
	return hr.error;
}

u16 HPI_PAD__get_channel_name(const struct hpi_hsubsys *ph_subsys,
	u32 h_control, char *psz_string, const u32 data_length)
{
	return hpi_control_get_string(h_control, HPI_PAD_CHANNEL_NAME,
		psz_string, data_length);
}

u16 HPI_PAD__get_artist(const struct hpi_hsubsys *ph_subsys, u32 h_control,
	char *psz_string, const u32 data_length)
{
	return hpi_control_get_string(h_control, HPI_PAD_ARTIST, psz_string,
		data_length);
}

u16 HPI_PAD__get_title(const struct hpi_hsubsys *ph_subsys, u32 h_control,
	char *psz_string, const u32 data_length)
{
	return hpi_control_get_string(h_control, HPI_PAD_TITLE, psz_string,
		data_length);
}

u16 HPI_PAD__get_comment(const struct hpi_hsubsys *ph_subsys, u32 h_control,
	char *psz_string, const u32 data_length)
{
	return hpi_control_get_string(h_control, HPI_PAD_COMMENT, psz_string,
		data_length);
}

u16 HPI_PAD__get_program_type(const struct hpi_hsubsys *ph_subsys,
	u32 h_control, u32 *ppTY)
{
	return hpi_control_param1_get(ph_subsys, h_control,
		HPI_PAD_PROGRAM_TYPE, ppTY);
}

u16 HPI_PAD__get_rdsPI(const struct hpi_hsubsys *ph_subsys, u32 h_control,
	u32 *ppI)
{
	return hpi_control_param1_get(ph_subsys, h_control,
		HPI_PAD_PROGRAM_ID, ppI);
}

u16 hpi_volume_query_channels(const struct hpi_hsubsys *ph_subsys,
	const u32 h_volume, u32 *p_channels)
{
	return hpi_control_query(ph_subsys, h_volume, HPI_VOLUME_NUM_CHANNELS,
		0, 0, p_channels);
}

u16 hpi_volume_set_gain(const struct hpi_hsubsys *ph_subsys, u32 h_control,
	short an_log_gain[HPI_MAX_CHANNELS]
	)
{
	return hpi_control_log_set2(h_control, HPI_VOLUME_GAIN,
		an_log_gain[0], an_log_gain[1]);
}

u16 hpi_volume_get_gain(const struct hpi_hsubsys *ph_subsys, u32 h_control,
	short an_log_gain[HPI_MAX_CHANNELS]
	)
{
	return hpi_control_log_get2(ph_subsys, h_control, HPI_VOLUME_GAIN,
		&an_log_gain[0], &an_log_gain[1]);
}

u16 hpi_volume_query_range(const struct hpi_hsubsys *ph_subsys, u32 h_control,
	short *min_gain_01dB, short *max_gain_01dB, short *step_gain_01dB)
{
	struct hpi_message hm;
	struct hpi_response hr;

	hpi_init_message_response(&hm, &hr, HPI_OBJ_CONTROL,
		HPI_CONTROL_GET_STATE);
	u32TOINDEXES(h_control, &hm.adapter_index, &hm.obj_index);
	hm.u.c.attribute = HPI_VOLUME_RANGE;

	hpi_send_recv(&hm, &hr);
	if (hr.error) {
		hr.u.c.an_log_value[0] = 0;
		hr.u.c.an_log_value[1] = 0;
		hr.u.c.param1 = 0;
	}
	if (min_gain_01dB)
		*min_gain_01dB = hr.u.c.an_log_value[0];
	if (max_gain_01dB)
		*max_gain_01dB = hr.u.c.an_log_value[1];
	if (step_gain_01dB)
		*step_gain_01dB = (short)hr.u.c.param1;
	return hr.error;
}

u16 hpi_volume_auto_fade_profile(const struct hpi_hsubsys *ph_subsys,
	u32 h_control, short an_stop_gain0_01dB[HPI_MAX_CHANNELS],
	u32 duration_ms, u16 profile)
{
	struct hpi_message hm;
	struct hpi_response hr;

	hpi_init_message_response(&hm, &hr, HPI_OBJ_CONTROL,
		HPI_CONTROL_SET_STATE);
	u32TOINDEXES(h_control, &hm.adapter_index, &hm.obj_index);

	memcpy(hm.u.c.an_log_value, an_stop_gain0_01dB,
		sizeof(short) * HPI_MAX_CHANNELS);

	hm.u.c.attribute = HPI_VOLUME_AUTOFADE;
	hm.u.c.param1 = duration_ms;
	hm.u.c.param2 = profile;

	hpi_send_recv(&hm, &hr);

	return hr.error;
}

u16 hpi_volume_auto_fade(const struct hpi_hsubsys *ph_subsys, u32 h_control,
	short an_stop_gain0_01dB[HPI_MAX_CHANNELS], u32 duration_ms)
{
	return hpi_volume_auto_fade_profile(ph_subsys, h_control,
		an_stop_gain0_01dB, duration_ms, HPI_VOLUME_AUTOFADE_LOG);
}

u16 hpi_vox_set_threshold(const struct hpi_hsubsys *ph_subsys, u32 h_control,
	short an_gain0_01dB)
{
	struct hpi_message hm;
	struct hpi_response hr;
	hpi_init_message_response(&hm, &hr, HPI_OBJ_CONTROL,
		HPI_CONTROL_SET_STATE);
	u32TOINDEXES(h_control, &hm.adapter_index, &hm.obj_index);
	hm.u.c.attribute = HPI_VOX_THRESHOLD;

	hm.u.c.an_log_value[0] = an_gain0_01dB;

	hpi_send_recv(&hm, &hr);

	return hr.error;
}

u16 hpi_vox_get_threshold(const struct hpi_hsubsys *ph_subsys, u32 h_control,
	short *an_gain0_01dB)
{
	struct hpi_message hm;
	struct hpi_response hr;
	hpi_init_message_response(&hm, &hr, HPI_OBJ_CONTROL,
		HPI_CONTROL_GET_STATE);
	u32TOINDEXES(h_control, &hm.adapter_index, &hm.obj_index);
	hm.u.c.attribute = HPI_VOX_THRESHOLD;

	hpi_send_recv(&hm, &hr);

	*an_gain0_01dB = hr.u.c.an_log_value[0];

	return hr.error;
}

static size_t strv_packet_size = MIN_STRV_PACKET_SIZE;

static size_t entity_type_to_size[LAST_ENTITY_TYPE] = {
	0,
	sizeof(struct hpi_entity),
	sizeof(void *),

	sizeof(int),
	sizeof(float),
	sizeof(double),

	sizeof(char),
	sizeof(char),

	4 * sizeof(char),
	16 * sizeof(char),
	6 * sizeof(char),
};

static inline size_t hpi_entity_size(struct hpi_entity *entity_ptr)
{
	return entity_ptr->header.size;
}

static inline size_t hpi_entity_header_size(struct hpi_entity *entity_ptr)
{
	return sizeof(entity_ptr->header);
}

static inline size_t hpi_entity_value_size(struct hpi_entity *entity_ptr)
{
	return hpi_entity_size(entity_ptr) -
		hpi_entity_header_size(entity_ptr);
}

static inline size_t hpi_entity_item_count(struct hpi_entity *entity_ptr)
{
	return hpi_entity_value_size(entity_ptr) /
		entity_type_to_size[entity_ptr->header.type];
}

static inline struct hpi_entity *hpi_entity_ptr_to_next(struct hpi_entity
	*entity_ptr)
{
	return (void *)(((u8 *)entity_ptr) + hpi_entity_size(entity_ptr));
}

static inline u16 hpi_entity_check_type(const enum e_entity_type t)
{
	if (t >= 0 && t < STR_TYPE_FIELD_MAX)
		return 0;
	return HPI_ERROR_ENTITY_TYPE_INVALID;
}

static inline u16 hpi_entity_check_role(const enum e_entity_role r)
{
	if (r >= 0 && r < STR_ROLE_FIELD_MAX)
		return 0;
	return HPI_ERROR_ENTITY_ROLE_INVALID;
}

static u16 hpi_entity_get_next(struct hpi_entity *entity, int recursive_flag,
	void *guard_p, struct hpi_entity **next)
{
	HPI_DEBUG_ASSERT(entity != NULL);
	HPI_DEBUG_ASSERT(next != NULL);
	HPI_DEBUG_ASSERT(hpi_entity_size(entity) != 0);

	if (guard_p <= (void *)entity) {
		*next = NULL;
		return 0;
	}

	if (recursive_flag && entity->header.type == entity_type_sequence)
		*next = (struct hpi_entity *)entity->value;
	else
		*next = (struct hpi_entity *)hpi_entity_ptr_to_next(entity);

	if (guard_p <= (void *)*next) {
		*next = NULL;
		return 0;
	}

	HPI_DEBUG_ASSERT(guard_p >= (void *)hpi_entity_ptr_to_next(*next));
	return 0;
}

u16 hpi_entity_find_next(struct hpi_entity *container_entity,
	enum e_entity_type type, enum e_entity_role role, int recursive_flag,
	struct hpi_entity **current_match)
{
	struct hpi_entity *tmp = NULL;
	void *guard_p = NULL;

	HPI_DEBUG_ASSERT(container_entity != NULL);
	guard_p = hpi_entity_ptr_to_next(container_entity);

	if (*current_match != NULL)
		hpi_entity_get_next(*current_match, recursive_flag, guard_p,
			&tmp);
	else
		hpi_entity_get_next(container_entity, 1, guard_p, &tmp);

	while (tmp) {
		u16 err;

		HPI_DEBUG_ASSERT((void *)tmp >= (void *)container_entity);

		if ((!type || tmp->header.type == type) && (!role
				|| tmp->header.role == role)) {
			*current_match = tmp;
			return 0;
		}

		err = hpi_entity_get_next(tmp, recursive_flag, guard_p,
			current_match);
		if (err)
			return err;

		tmp = *current_match;
	}

	*current_match = NULL;
	return 0;
}

void hpi_entity_free(struct hpi_entity *entity)
{
	kfree(entity);
}

static u16 hpi_entity_alloc_and_copy(struct hpi_entity *src,
	struct hpi_entity **dst)
{
	size_t buf_size;
	HPI_DEBUG_ASSERT(dst != NULL);
	HPI_DEBUG_ASSERT(src != NULL);

	buf_size = hpi_entity_size(src);
	*dst = kmalloc(buf_size, GFP_KERNEL);
	if (*dst == NULL)
		return HPI_ERROR_MEMORY_ALLOC;
	memcpy(*dst, src, buf_size);
	return 0;
}

u16 hpi_universal_info(const struct hpi_hsubsys *ph_subsys, u32 hC,
	struct hpi_entity **info)
{
	struct hpi_msg_strv hm;
	struct hpi_res_strv *phr;
	u16 hpi_err;
	int remaining_attempts = 2;
	size_t resp_packet_size = 1024;

	*info = NULL;

	while (remaining_attempts--) {
		phr = kmalloc(resp_packet_size, GFP_KERNEL);
		HPI_DEBUG_ASSERT(phr != NULL);

		hpi_init_message_responseV1(&hm.h, (u16)sizeof(hm), &phr->h,
			(u16)resp_packet_size, HPI_OBJ_CONTROL,
			HPI_CONTROL_GET_INFO);
		u32TOINDEXES(hC, &hm.h.adapter_index, &hm.h.obj_index);

		hm.strv.header.size = sizeof(hm.strv);
		phr->strv.header.size = resp_packet_size - sizeof(phr->h);

		hpi_send_recv((struct hpi_message *)&hm.h,
			(struct hpi_response *)&phr->h);
		if (phr->h.error == HPI_ERROR_RESPONSE_BUFFER_TOO_SMALL) {

			HPI_DEBUG_ASSERT(phr->h.specific_error >
				MIN_STRV_PACKET_SIZE
				&& phr->h.specific_error < 1500);
			resp_packet_size = phr->h.specific_error;
		} else {
			remaining_attempts = 0;
			if (!phr->h.error)
				hpi_entity_alloc_and_copy(&phr->strv, info);
		}

		hpi_err = phr->h.error;
		kfree(phr);
	}

	return hpi_err;
}

u16 hpi_universal_get(const struct hpi_hsubsys *ph_subsys, u32 hC,
	struct hpi_entity **value)
{
	struct hpi_msg_strv hm;
	struct hpi_res_strv *phr;
	u16 hpi_err;
	int remaining_attempts = 2;

	*value = NULL;

	while (remaining_attempts--) {
		phr = kmalloc(strv_packet_size, GFP_KERNEL);
		if (!phr)
			return HPI_ERROR_MEMORY_ALLOC;

		hpi_init_message_responseV1(&hm.h, (u16)sizeof(hm), &phr->h,
			(u16)strv_packet_size, HPI_OBJ_CONTROL,
			HPI_CONTROL_GET_STATE);
		u32TOINDEXES(hC, &hm.h.adapter_index, &hm.h.obj_index);

		hm.strv.header.size = sizeof(hm.strv);
		phr->strv.header.size = strv_packet_size - sizeof(phr->h);

		hpi_send_recv((struct hpi_message *)&hm.h,
			(struct hpi_response *)&phr->h);
		if (phr->h.error == HPI_ERROR_RESPONSE_BUFFER_TOO_SMALL) {

			HPI_DEBUG_ASSERT(phr->h.specific_error >
				MIN_STRV_PACKET_SIZE
				&& phr->h.specific_error < 1000);
			strv_packet_size = phr->h.specific_error;
		} else {
			remaining_attempts = 0;
			if (!phr->h.error)
				hpi_entity_alloc_and_copy(&phr->strv, value);
		}

		hpi_err = phr->h.error;
		kfree(phr);
	}

	return hpi_err;
}

u16 hpi_universal_set(const struct hpi_hsubsys *ph_subsys, u32 hC,
	struct hpi_entity *value)
{
	struct hpi_msg_strv *phm;
	struct hpi_res_strv hr;

	phm = kmalloc(sizeof(phm->h) + value->header.size, GFP_KERNEL);
	HPI_DEBUG_ASSERT(phm != NULL);

	hpi_init_message_responseV1(&phm->h,
		sizeof(phm->h) + value->header.size, &hr.h, sizeof(hr),
		HPI_OBJ_CONTROL, HPI_CONTROL_SET_STATE);
	u32TOINDEXES(hC, &phm->h.adapter_index, &phm->h.obj_index);
	hr.strv.header.size = sizeof(hr.strv);

	memcpy(&phm->strv, value, value->header.size);
	hpi_send_recv((struct hpi_message *)&phm->h,
		(struct hpi_response *)&hr.h);

	return hr.h.error;
}

u16 hpi_entity_alloc_and_pack(const enum e_entity_type type,
	const size_t item_count, const enum e_entity_role role, void *value,
	struct hpi_entity **entity)
{
	size_t bytes_to_copy, total_size;
	u16 hE = 0;
	*entity = NULL;

	hE = hpi_entity_check_type(type);
	if (hE)
		return hE;

	HPI_DEBUG_ASSERT(role > entity_role_null && type < LAST_ENTITY_TYPE);

	bytes_to_copy = entity_type_to_size[type] * item_count;
	total_size = hpi_entity_header_size(*entity) + bytes_to_copy;

	HPI_DEBUG_ASSERT(total_size >= hpi_entity_header_size(*entity)
		&& total_size < STR_SIZE_FIELD_MAX);

	*entity = kmalloc(total_size, GFP_KERNEL);
	if (*entity == NULL)
		return HPI_ERROR_MEMORY_ALLOC;
	memcpy((*entity)->value, value, bytes_to_copy);
	(*entity)->header.size =
		hpi_entity_header_size(*entity) + bytes_to_copy;
	(*entity)->header.type = type;
	(*entity)->header.role = role;
	return 0;
}

u16 hpi_entity_copy_value_from(struct hpi_entity *entity,
	enum e_entity_type type, size_t item_count, void *value_dst_p)
{
	size_t bytes_to_copy;

	if (entity->header.type != type)
		return HPI_ERROR_ENTITY_TYPE_MISMATCH;

	if (hpi_entity_item_count(entity) != item_count)
		return HPI_ERROR_ENTITY_ITEM_COUNT;

	bytes_to_copy = entity_type_to_size[type] * item_count;
	memcpy(value_dst_p, entity->value, bytes_to_copy);
	return 0;
}

u16 hpi_entity_unpack(struct hpi_entity *entity, enum e_entity_type *type,
	size_t *item_count, enum e_entity_role *role, void **value)
{
	u16 err = 0;
	HPI_DEBUG_ASSERT(entity != NULL);

	if (type)
		*type = entity->header.type;

	if (role)
		*role = entity->header.role;

	if (value)
		*value = entity->value;

	if (item_count != NULL) {
		if (entity->header.type == entity_type_sequence) {
			void *guard_p = hpi_entity_ptr_to_next(entity);
			struct hpi_entity *next = NULL;
			void *contents = entity->value;

			*item_count = 0;
			while (contents < guard_p) {
				(*item_count)++;
				err = hpi_entity_get_next(contents, 0,
					guard_p, &next);
				if (next == NULL || err)
					break;
				contents = next;
			}
		} else {
			*item_count = hpi_entity_item_count(entity);
		}
	}
	return err;
}

u16 hpi_gpio_open(const struct hpi_hsubsys *ph_subsys, u16 adapter_index,
	u32 *ph_gpio, u16 *pw_number_input_bits, u16 *pw_number_output_bits)
{
	struct hpi_message hm;
	struct hpi_response hr;
	hpi_init_message_response(&hm, &hr, HPI_OBJ_GPIO, HPI_GPIO_OPEN);
	hm.adapter_index = adapter_index;

	hpi_send_recv(&hm, &hr);

	if (hr.error == 0) {
		*ph_gpio =
			hpi_indexes_to_handle(HPI_OBJ_GPIO, adapter_index, 0);
		if (pw_number_input_bits)
			*pw_number_input_bits = hr.u.l.number_input_bits;
		if (pw_number_output_bits)
			*pw_number_output_bits = hr.u.l.number_output_bits;
	} else
		*ph_gpio = 0;
	return hr.error;
}

u16 hpi_gpio_read_bit(const struct hpi_hsubsys *ph_subsys, u32 h_gpio,
	u16 bit_index, u16 *pw_bit_data)
{
	struct hpi_message hm;
	struct hpi_response hr;
	hpi_init_message_response(&hm, &hr, HPI_OBJ_GPIO, HPI_GPIO_READ_BIT);
	u32TOINDEX(h_gpio, &hm.adapter_index);
	hm.u.l.bit_index = bit_index;

	hpi_send_recv(&hm, &hr);

	*pw_bit_data = hr.u.l.bit_data[0];
	return hr.error;
}

u16 hpi_gpio_read_all_bits(const struct hpi_hsubsys *ph_subsys, u32 h_gpio,
	u16 aw_all_bit_data[4]
	)
{
	struct hpi_message hm;
	struct hpi_response hr;
	hpi_init_message_response(&hm, &hr, HPI_OBJ_GPIO, HPI_GPIO_READ_ALL);
	u32TOINDEX(h_gpio, &hm.adapter_index);

	hpi_send_recv(&hm, &hr);

	if (aw_all_bit_data) {
		aw_all_bit_data[0] = hr.u.l.bit_data[0];
		aw_all_bit_data[1] = hr.u.l.bit_data[1];
		aw_all_bit_data[2] = hr.u.l.bit_data[2];
		aw_all_bit_data[3] = hr.u.l.bit_data[3];
	}
	return hr.error;
}

u16 hpi_gpio_write_bit(const struct hpi_hsubsys *ph_subsys, u32 h_gpio,
	u16 bit_index, u16 bit_data)
{
	struct hpi_message hm;
	struct hpi_response hr;
	hpi_init_message_response(&hm, &hr, HPI_OBJ_GPIO, HPI_GPIO_WRITE_BIT);
	u32TOINDEX(h_gpio, &hm.adapter_index);
	hm.u.l.bit_index = bit_index;
	hm.u.l.bit_data = bit_data;

	hpi_send_recv(&hm, &hr);

	return hr.error;
}

u16 hpi_gpio_write_status(const struct hpi_hsubsys *ph_subsys, u32 h_gpio,
	u16 aw_all_bit_data[4]
	)
{
	struct hpi_message hm;
	struct hpi_response hr;
	hpi_init_message_response(&hm, &hr, HPI_OBJ_GPIO,
		HPI_GPIO_WRITE_STATUS);
	u32TOINDEX(h_gpio, &hm.adapter_index);

	hpi_send_recv(&hm, &hr);

	if (aw_all_bit_data) {
		aw_all_bit_data[0] = hr.u.l.bit_data[0];
		aw_all_bit_data[1] = hr.u.l.bit_data[1];
		aw_all_bit_data[2] = hr.u.l.bit_data[2];
		aw_all_bit_data[3] = hr.u.l.bit_data[3];
	}
	return hr.error;
}

u16 hpi_async_event_open(const struct hpi_hsubsys *ph_subsys,
	u16 adapter_index, u32 *ph_async)
{
	struct hpi_message hm;
	struct hpi_response hr;
	hpi_init_message_response(&hm, &hr, HPI_OBJ_ASYNCEVENT,
		HPI_ASYNCEVENT_OPEN);
	hm.adapter_index = adapter_index;

	hpi_send_recv(&hm, &hr);

	if (hr.error == 0)

		*ph_async =
			hpi_indexes_to_handle(HPI_OBJ_ASYNCEVENT,
			adapter_index, 0);
	else
		*ph_async = 0;
	return hr.error;

}

u16 hpi_async_event_close(const struct hpi_hsubsys *ph_subsys, u32 h_async)
{
	struct hpi_message hm;
	struct hpi_response hr;
	hpi_init_message_response(&hm, &hr, HPI_OBJ_ASYNCEVENT,
		HPI_ASYNCEVENT_OPEN);
	u32TOINDEX(h_async, &hm.adapter_index);

	hpi_send_recv(&hm, &hr);

	return hr.error;
}

u16 hpi_async_event_wait(const struct hpi_hsubsys *ph_subsys, u32 h_async,
	u16 maximum_events, struct hpi_async_event *p_events,
	u16 *pw_number_returned)
{

	return 0;
}

u16 hpi_async_event_get_count(const struct hpi_hsubsys *ph_subsys,
	u32 h_async, u16 *pw_count)
{
	struct hpi_message hm;
	struct hpi_response hr;
	hpi_init_message_response(&hm, &hr, HPI_OBJ_ASYNCEVENT,
		HPI_ASYNCEVENT_GETCOUNT);
	u32TOINDEX(h_async, &hm.adapter_index);

	hpi_send_recv(&hm, &hr);

	if (hr.error == 0)
		if (pw_count)
			*pw_count = hr.u.as.u.count.count;

	return hr.error;
}

u16 hpi_async_event_get(const struct hpi_hsubsys *ph_subsys, u32 h_async,
	u16 maximum_events, struct hpi_async_event *p_events,
	u16 *pw_number_returned)
{
	struct hpi_message hm;
	struct hpi_response hr;
	hpi_init_message_response(&hm, &hr, HPI_OBJ_ASYNCEVENT,
		HPI_ASYNCEVENT_GET);
	u32TOINDEX(h_async, &hm.adapter_index);

	hpi_send_recv(&hm, &hr);
	if (!hr.error) {
		memcpy(p_events, &hr.u.as.u.event,
			sizeof(struct hpi_async_event));
		*pw_number_returned = 1;
	}

	return hr.error;
}

u16 hpi_nv_memory_open(const struct hpi_hsubsys *ph_subsys, u16 adapter_index,
	u32 *ph_nv_memory, u16 *pw_size_in_bytes)
{
	struct hpi_message hm;
	struct hpi_response hr;
	hpi_init_message_response(&hm, &hr, HPI_OBJ_NVMEMORY,
		HPI_NVMEMORY_OPEN);
	hm.adapter_index = adapter_index;

	hpi_send_recv(&hm, &hr);

	if (hr.error == 0) {
		*ph_nv_memory =
			hpi_indexes_to_handle(HPI_OBJ_NVMEMORY, adapter_index,
			0);
		if (pw_size_in_bytes)
			*pw_size_in_bytes = hr.u.n.size_in_bytes;
	} else
		*ph_nv_memory = 0;
	return hr.error;
}

u16 hpi_nv_memory_read_byte(const struct hpi_hsubsys *ph_subsys,
	u32 h_nv_memory, u16 index, u16 *pw_data)
{
	struct hpi_message hm;
	struct hpi_response hr;
	hpi_init_message_response(&hm, &hr, HPI_OBJ_NVMEMORY,
		HPI_NVMEMORY_READ_BYTE);
	u32TOINDEX(h_nv_memory, &hm.adapter_index);
	hm.u.n.address = index;

	hpi_send_recv(&hm, &hr);

	*pw_data = hr.u.n.data;
	return hr.error;
}

u16 hpi_nv_memory_write_byte(const struct hpi_hsubsys *ph_subsys,
	u32 h_nv_memory, u16 index, u16 data)
{
	struct hpi_message hm;
	struct hpi_response hr;
	hpi_init_message_response(&hm, &hr, HPI_OBJ_NVMEMORY,
		HPI_NVMEMORY_WRITE_BYTE);
	u32TOINDEX(h_nv_memory, &hm.adapter_index);
	hm.u.n.address = index;
	hm.u.n.data = data;

	hpi_send_recv(&hm, &hr);

	return hr.error;
}

u16 hpi_profile_open_all(const struct hpi_hsubsys *ph_subsys,
	u16 adapter_index, u16 profile_index, u32 *ph_profile,
	u16 *pw_max_profiles)
{
	struct hpi_message hm;
	struct hpi_response hr;
	hpi_init_message_response(&hm, &hr, HPI_OBJ_PROFILE,
		HPI_PROFILE_OPEN_ALL);
	hm.adapter_index = adapter_index;
	hm.obj_index = profile_index;
	hpi_send_recv(&hm, &hr);

	*pw_max_profiles = hr.u.p.u.o.max_profiles;
	if (hr.error == 0)
		*ph_profile =
			hpi_indexes_to_handle(HPI_OBJ_PROFILE, adapter_index,
			profile_index);
	else
		*ph_profile = 0;
	return hr.error;
}

u16 hpi_profile_get(const struct hpi_hsubsys *ph_subsys, u32 h_profile,
	u16 bin_index, u16 *pw_seconds, u32 *pmicro_seconds, u32 *pcall_count,
	u32 *pmax_micro_seconds, u32 *pmin_micro_seconds)
{
	struct hpi_message hm;
	struct hpi_response hr;
	hpi_init_message_response(&hm, &hr, HPI_OBJ_PROFILE, HPI_PROFILE_GET);
	u32TOINDEXES(h_profile, &hm.adapter_index, &hm.obj_index);
	hm.u.p.bin_index = bin_index;
	hpi_send_recv(&hm, &hr);
	if (pw_seconds)
		*pw_seconds = hr.u.p.u.t.seconds;
	if (pmicro_seconds)
		*pmicro_seconds = hr.u.p.u.t.micro_seconds;
	if (pcall_count)
		*pcall_count = hr.u.p.u.t.call_count;
	if (pmax_micro_seconds)
		*pmax_micro_seconds = hr.u.p.u.t.max_micro_seconds;
	if (pmin_micro_seconds)
		*pmin_micro_seconds = hr.u.p.u.t.min_micro_seconds;
	return hr.error;
}

u16 hpi_profile_get_utilization(const struct hpi_hsubsys *ph_subsys,
	u32 h_profile, u32 *putilization)
{
	struct hpi_message hm;
	struct hpi_response hr;
	hpi_init_message_response(&hm, &hr, HPI_OBJ_PROFILE,
		HPI_PROFILE_GET_UTILIZATION);
	u32TOINDEXES(h_profile, &hm.adapter_index, &hm.obj_index);
	hpi_send_recv(&hm, &hr);
	if (hr.error) {
		if (putilization)
			*putilization = 0;
	} else {
		if (putilization)
			*putilization = hr.u.p.u.t.call_count;
	}
	return hr.error;
}

u16 hpi_profile_get_name(const struct hpi_hsubsys *ph_subsys, u32 h_profile,
	u16 bin_index, char *sz_name, u16 name_length)
{
	struct hpi_message hm;
	struct hpi_response hr;
	hpi_init_message_response(&hm, &hr, HPI_OBJ_PROFILE,
		HPI_PROFILE_GET_NAME);
	u32TOINDEXES(h_profile, &hm.adapter_index, &hm.obj_index);
	hm.u.p.bin_index = bin_index;
	hpi_send_recv(&hm, &hr);
	if (hr.error) {
		if (sz_name)
			strcpy(sz_name, "??");
	} else {
		if (sz_name)
			memcpy(sz_name, (char *)hr.u.p.u.n.sz_name,
				name_length);
	}
	return hr.error;
}

u16 hpi_profile_start_all(const struct hpi_hsubsys *ph_subsys, u32 h_profile)
{
	struct hpi_message hm;
	struct hpi_response hr;
	hpi_init_message_response(&hm, &hr, HPI_OBJ_PROFILE,
		HPI_PROFILE_START_ALL);
	u32TOINDEXES(h_profile, &hm.adapter_index, &hm.obj_index);
	hpi_send_recv(&hm, &hr);

	return hr.error;
}

u16 hpi_profile_stop_all(const struct hpi_hsubsys *ph_subsys, u32 h_profile)
{
	struct hpi_message hm;
	struct hpi_response hr;
	hpi_init_message_response(&hm, &hr, HPI_OBJ_PROFILE,
		HPI_PROFILE_STOP_ALL);
	u32TOINDEXES(h_profile, &hm.adapter_index, &hm.obj_index);
	hpi_send_recv(&hm, &hr);

	return hr.error;
}

u16 hpi_watchdog_open(const struct hpi_hsubsys *ph_subsys, u16 adapter_index,
	u32 *ph_watchdog)
{
	struct hpi_message hm;
	struct hpi_response hr;
	hpi_init_message_response(&hm, &hr, HPI_OBJ_WATCHDOG,
		HPI_WATCHDOG_OPEN);
	hm.adapter_index = adapter_index;

	hpi_send_recv(&hm, &hr);

	if (hr.error == 0)
		*ph_watchdog =
			hpi_indexes_to_handle(HPI_OBJ_WATCHDOG, adapter_index,
			0);
	else
		*ph_watchdog = 0;
	return hr.error;
}

u16 hpi_watchdog_set_time(const struct hpi_hsubsys *ph_subsys, u32 h_watchdog,
	u32 time_millisec)
{
	struct hpi_message hm;
	struct hpi_response hr;
	hpi_init_message_response(&hm, &hr, HPI_OBJ_WATCHDOG,
		HPI_WATCHDOG_SET_TIME);
	u32TOINDEX(h_watchdog, &hm.adapter_index);
	hm.u.w.time_ms = time_millisec;

	hpi_send_recv(&hm, &hr);

	return hr.error;
}

u16 hpi_watchdog_ping(const struct hpi_hsubsys *ph_subsys, u32 h_watchdog)
{
	struct hpi_message hm;
	struct hpi_response hr;
	hpi_init_message_response(&hm, &hr, HPI_OBJ_WATCHDOG,
		HPI_WATCHDOG_PING);
	u32TOINDEX(h_watchdog, &hm.adapter_index);

	hpi_send_recv(&hm, &hr);

	return hr.error;
}
