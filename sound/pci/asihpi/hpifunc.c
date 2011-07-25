
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

static u16 hpi_handle_indexes(const u32 h, u16 *p1, u16 *p2)
{
	union handle_word uhandle;
	if (!h)
		return HPI_ERROR_INVALID_HANDLE;

	uhandle.w = h;

	*p1 = (u16)uhandle.h.adapter_index;
	if (p2)
		*p2 = (u16)uhandle.h.obj_index;

	return 0;
}

void hpi_handle_to_indexes(const u32 handle, u16 *pw_adapter_index,
	u16 *pw_object_index)
{
	hpi_handle_indexes(handle, pw_adapter_index, pw_object_index);
}

char hpi_handle_object(const u32 handle)
{
	union handle_word uhandle;
	uhandle.w = handle;
	return (char)uhandle.h.obj_type;
}

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

static inline void hpi_send_recvV1(struct hpi_message_header *m,
	struct hpi_response_header *r)
{
	hpi_send_recv((struct hpi_message *)m, (struct hpi_response *)r);
}

u16 hpi_subsys_get_version_ex(u32 *pversion_ex)
{
	struct hpi_message hm;
	struct hpi_response hr;

	hpi_init_message_response(&hm, &hr, HPI_OBJ_SUBSYSTEM,
		HPI_SUBSYS_GET_VERSION);
	hpi_send_recv(&hm, &hr);
	*pversion_ex = hr.u.s.data;
	return hr.error;
}

u16 hpi_subsys_get_num_adapters(int *pn_num_adapters)
{
	struct hpi_message hm;
	struct hpi_response hr;
	hpi_init_message_response(&hm, &hr, HPI_OBJ_SUBSYSTEM,
		HPI_SUBSYS_GET_NUM_ADAPTERS);
	hpi_send_recv(&hm, &hr);
	*pn_num_adapters = (int)hr.u.s.num_adapters;
	return hr.error;
}

u16 hpi_subsys_get_adapter(int iterator, u32 *padapter_index,
	u16 *pw_adapter_type)
{
	struct hpi_message hm;
	struct hpi_response hr;
	hpi_init_message_response(&hm, &hr, HPI_OBJ_SUBSYSTEM,
		HPI_SUBSYS_GET_ADAPTER);
	hm.obj_index = (u16)iterator;
	hpi_send_recv(&hm, &hr);
	*padapter_index = (int)hr.u.s.adapter_index;
	*pw_adapter_type = hr.u.s.adapter_type;

	return hr.error;
}

u16 hpi_adapter_open(u16 adapter_index)
{
	struct hpi_message hm;
	struct hpi_response hr;
	hpi_init_message_response(&hm, &hr, HPI_OBJ_ADAPTER,
		HPI_ADAPTER_OPEN);
	hm.adapter_index = adapter_index;

	hpi_send_recv(&hm, &hr);

	return hr.error;

}

u16 hpi_adapter_close(u16 adapter_index)
{
	struct hpi_message hm;
	struct hpi_response hr;
	hpi_init_message_response(&hm, &hr, HPI_OBJ_ADAPTER,
		HPI_ADAPTER_CLOSE);
	hm.adapter_index = adapter_index;

	hpi_send_recv(&hm, &hr);

	return hr.error;
}

u16 hpi_adapter_set_mode(u16 adapter_index, u32 adapter_mode)
{
	return hpi_adapter_set_mode_ex(adapter_index, adapter_mode,
		HPI_ADAPTER_MODE_SET);
}

u16 hpi_adapter_set_mode_ex(u16 adapter_index, u32 adapter_mode,
	u16 query_or_set)
{
	struct hpi_message hm;
	struct hpi_response hr;

	hpi_init_message_response(&hm, &hr, HPI_OBJ_ADAPTER,
		HPI_ADAPTER_SET_MODE);
	hm.adapter_index = adapter_index;
	hm.u.ax.mode.adapter_mode = adapter_mode;
	hm.u.ax.mode.query_or_set = query_or_set;
	hpi_send_recv(&hm, &hr);
	return hr.error;
}

u16 hpi_adapter_get_mode(u16 adapter_index, u32 *padapter_mode)
{
	struct hpi_message hm;
	struct hpi_response hr;
	hpi_init_message_response(&hm, &hr, HPI_OBJ_ADAPTER,
		HPI_ADAPTER_GET_MODE);
	hm.adapter_index = adapter_index;
	hpi_send_recv(&hm, &hr);
	if (padapter_mode)
		*padapter_mode = hr.u.ax.mode.adapter_mode;
	return hr.error;
}

u16 hpi_adapter_get_info(u16 adapter_index, u16 *pw_num_outstreams,
	u16 *pw_num_instreams, u16 *pw_version, u32 *pserial_number,
	u16 *pw_adapter_type)
{
	struct hpi_message hm;
	struct hpi_response hr;
	hpi_init_message_response(&hm, &hr, HPI_OBJ_ADAPTER,
		HPI_ADAPTER_GET_INFO);
	hm.adapter_index = adapter_index;

	hpi_send_recv(&hm, &hr);

	*pw_adapter_type = hr.u.ax.info.adapter_type;
	*pw_num_outstreams = hr.u.ax.info.num_outstreams;
	*pw_num_instreams = hr.u.ax.info.num_instreams;
	*pw_version = hr.u.ax.info.version;
	*pserial_number = hr.u.ax.info.serial_number;
	return hr.error;
}

u16 hpi_adapter_get_module_by_index(u16 adapter_index, u16 module_index,
	u16 *pw_num_outputs, u16 *pw_num_inputs, u16 *pw_version,
	u32 *pserial_number, u16 *pw_module_type, u32 *ph_module)
{
	struct hpi_message hm;
	struct hpi_response hr;

	hpi_init_message_response(&hm, &hr, HPI_OBJ_ADAPTER,
		HPI_ADAPTER_MODULE_INFO);
	hm.adapter_index = adapter_index;
	hm.u.ax.module_info.index = module_index;

	hpi_send_recv(&hm, &hr);

	*pw_module_type = hr.u.ax.info.adapter_type;
	*pw_num_outputs = hr.u.ax.info.num_outstreams;
	*pw_num_inputs = hr.u.ax.info.num_instreams;
	*pw_version = hr.u.ax.info.version;
	*pserial_number = hr.u.ax.info.serial_number;
	*ph_module = 0;

	return hr.error;
}

u16 hpi_adapter_set_property(u16 adapter_index, u16 property, u16 parameter1,
	u16 parameter2)
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

u16 hpi_adapter_get_property(u16 adapter_index, u16 property,
	u16 *pw_parameter1, u16 *pw_parameter2)
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

u16 hpi_adapter_enumerate_property(u16 adapter_index, u16 index,
	u16 what_to_enumerate, u16 property_index, u32 *psetting)
{
	return 0;
}

u16 hpi_format_create(struct hpi_format *p_format, u16 channels, u16 format,
	u32 sample_rate, u32 bit_rate, u32 attributes)
{
	u16 err = 0;
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
		err = HPI_ERROR_INVALID_CHANNELS;
		return err;
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
		err = HPI_ERROR_INVALID_FORMAT;
		return err;
	}
	fmt.format = format;

	if (sample_rate < 8000L) {
		err = HPI_ERROR_INCOMPATIBLE_SAMPLERATE;
		sample_rate = 8000L;
	}
	if (sample_rate > 200000L) {
		err = HPI_ERROR_INCOMPATIBLE_SAMPLERATE;
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
			err = HPI_ERROR_INVALID_FORMAT;
		} else if (attributes > HPI_MPEG_MODE_DUALCHANNEL) {
			attributes = HPI_MPEG_MODE_DEFAULT;
			err = HPI_ERROR_INVALID_FORMAT;
		}
		fmt.attributes = attributes;
		break;
	default:
		fmt.attributes = attributes;
	}

	hpi_msg_to_format(p_format, &fmt);
	return err;
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

u16 hpi_outstream_open(u16 adapter_index, u16 outstream_index,
	u32 *ph_outstream)
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

u16 hpi_outstream_close(u32 h_outstream)
{
	struct hpi_message hm;
	struct hpi_response hr;

	hpi_init_message_response(&hm, &hr, HPI_OBJ_OSTREAM,
		HPI_OSTREAM_HOSTBUFFER_FREE);
	if (hpi_handle_indexes(h_outstream, &hm.adapter_index, &hm.obj_index))
		return HPI_ERROR_INVALID_HANDLE;

	hpi_send_recv(&hm, &hr);

	hpi_init_message_response(&hm, &hr, HPI_OBJ_OSTREAM,
		HPI_OSTREAM_GROUP_RESET);
	hpi_handle_indexes(h_outstream, &hm.adapter_index, &hm.obj_index);
	hpi_send_recv(&hm, &hr);

	hpi_init_message_response(&hm, &hr, HPI_OBJ_OSTREAM,
		HPI_OSTREAM_CLOSE);
	hpi_handle_indexes(h_outstream, &hm.adapter_index, &hm.obj_index);
	hpi_send_recv(&hm, &hr);

	return hr.error;
}

u16 hpi_outstream_get_info_ex(u32 h_outstream, u16 *pw_state,
	u32 *pbuffer_size, u32 *pdata_to_play, u32 *psamples_played,
	u32 *pauxiliary_data_to_play)
{
	struct hpi_message hm;
	struct hpi_response hr;
	hpi_init_message_response(&hm, &hr, HPI_OBJ_OSTREAM,
		HPI_OSTREAM_GET_INFO);
	if (hpi_handle_indexes(h_outstream, &hm.adapter_index, &hm.obj_index))
		return HPI_ERROR_INVALID_HANDLE;

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

u16 hpi_outstream_write_buf(u32 h_outstream, const u8 *pb_data,
	u32 bytes_to_write, const struct hpi_format *p_format)
{
	struct hpi_message hm;
	struct hpi_response hr;
	hpi_init_message_response(&hm, &hr, HPI_OBJ_OSTREAM,
		HPI_OSTREAM_WRITE);
	if (hpi_handle_indexes(h_outstream, &hm.adapter_index, &hm.obj_index))
		return HPI_ERROR_INVALID_HANDLE;
	hm.u.d.u.data.pb_data = (u8 *)pb_data;
	hm.u.d.u.data.data_size = bytes_to_write;

	hpi_format_to_msg(&hm.u.d.u.data.format, p_format);

	hpi_send_recv(&hm, &hr);

	return hr.error;
}

u16 hpi_outstream_start(u32 h_outstream)
{
	struct hpi_message hm;
	struct hpi_response hr;
	hpi_init_message_response(&hm, &hr, HPI_OBJ_OSTREAM,
		HPI_OSTREAM_START);
	if (hpi_handle_indexes(h_outstream, &hm.adapter_index, &hm.obj_index))
		return HPI_ERROR_INVALID_HANDLE;

	hpi_send_recv(&hm, &hr);

	return hr.error;
}

u16 hpi_outstream_wait_start(u32 h_outstream)
{
	struct hpi_message hm;
	struct hpi_response hr;
	hpi_init_message_response(&hm, &hr, HPI_OBJ_OSTREAM,
		HPI_OSTREAM_WAIT_START);
	if (hpi_handle_indexes(h_outstream, &hm.adapter_index, &hm.obj_index))
		return HPI_ERROR_INVALID_HANDLE;

	hpi_send_recv(&hm, &hr);

	return hr.error;
}

u16 hpi_outstream_stop(u32 h_outstream)
{
	struct hpi_message hm;
	struct hpi_response hr;
	hpi_init_message_response(&hm, &hr, HPI_OBJ_OSTREAM,
		HPI_OSTREAM_STOP);
	if (hpi_handle_indexes(h_outstream, &hm.adapter_index, &hm.obj_index))
		return HPI_ERROR_INVALID_HANDLE;

	hpi_send_recv(&hm, &hr);

	return hr.error;
}

u16 hpi_outstream_sinegen(u32 h_outstream)
{
	struct hpi_message hm;
	struct hpi_response hr;
	hpi_init_message_response(&hm, &hr, HPI_OBJ_OSTREAM,
		HPI_OSTREAM_SINEGEN);
	if (hpi_handle_indexes(h_outstream, &hm.adapter_index, &hm.obj_index))
		return HPI_ERROR_INVALID_HANDLE;

	hpi_send_recv(&hm, &hr);

	return hr.error;
}

u16 hpi_outstream_reset(u32 h_outstream)
{
	struct hpi_message hm;
	struct hpi_response hr;
	hpi_init_message_response(&hm, &hr, HPI_OBJ_OSTREAM,
		HPI_OSTREAM_RESET);
	if (hpi_handle_indexes(h_outstream, &hm.adapter_index, &hm.obj_index))
		return HPI_ERROR_INVALID_HANDLE;

	hpi_send_recv(&hm, &hr);

	return hr.error;
}

u16 hpi_outstream_query_format(u32 h_outstream, struct hpi_format *p_format)
{
	struct hpi_message hm;
	struct hpi_response hr;

	hpi_init_message_response(&hm, &hr, HPI_OBJ_OSTREAM,
		HPI_OSTREAM_QUERY_FORMAT);
	if (hpi_handle_indexes(h_outstream, &hm.adapter_index, &hm.obj_index))
		return HPI_ERROR_INVALID_HANDLE;

	hpi_format_to_msg(&hm.u.d.u.data.format, p_format);

	hpi_send_recv(&hm, &hr);

	return hr.error;
}

u16 hpi_outstream_set_format(u32 h_outstream, struct hpi_format *p_format)
{
	struct hpi_message hm;
	struct hpi_response hr;

	hpi_init_message_response(&hm, &hr, HPI_OBJ_OSTREAM,
		HPI_OSTREAM_SET_FORMAT);
	if (hpi_handle_indexes(h_outstream, &hm.adapter_index, &hm.obj_index))
		return HPI_ERROR_INVALID_HANDLE;

	hpi_format_to_msg(&hm.u.d.u.data.format, p_format);

	hpi_send_recv(&hm, &hr);

	return hr.error;
}

u16 hpi_outstream_set_velocity(u32 h_outstream, short velocity)
{
	struct hpi_message hm;
	struct hpi_response hr;

	hpi_init_message_response(&hm, &hr, HPI_OBJ_OSTREAM,
		HPI_OSTREAM_SET_VELOCITY);
	if (hpi_handle_indexes(h_outstream, &hm.adapter_index, &hm.obj_index))
		return HPI_ERROR_INVALID_HANDLE;
	hm.u.d.u.velocity = velocity;

	hpi_send_recv(&hm, &hr);

	return hr.error;
}

u16 hpi_outstream_set_punch_in_out(u32 h_outstream, u32 punch_in_sample,
	u32 punch_out_sample)
{
	struct hpi_message hm;
	struct hpi_response hr;

	hpi_init_message_response(&hm, &hr, HPI_OBJ_OSTREAM,
		HPI_OSTREAM_SET_PUNCHINOUT);
	if (hpi_handle_indexes(h_outstream, &hm.adapter_index, &hm.obj_index))
		return HPI_ERROR_INVALID_HANDLE;

	hm.u.d.u.pio.punch_in_sample = punch_in_sample;
	hm.u.d.u.pio.punch_out_sample = punch_out_sample;

	hpi_send_recv(&hm, &hr);

	return hr.error;
}

u16 hpi_outstream_ancillary_reset(u32 h_outstream, u16 mode)
{
	struct hpi_message hm;
	struct hpi_response hr;

	hpi_init_message_response(&hm, &hr, HPI_OBJ_OSTREAM,
		HPI_OSTREAM_ANC_RESET);
	if (hpi_handle_indexes(h_outstream, &hm.adapter_index, &hm.obj_index))
		return HPI_ERROR_INVALID_HANDLE;
	hm.u.d.u.data.format.channels = mode;
	hpi_send_recv(&hm, &hr);
	return hr.error;
}

u16 hpi_outstream_ancillary_get_info(u32 h_outstream, u32 *pframes_available)
{
	struct hpi_message hm;
	struct hpi_response hr;

	hpi_init_message_response(&hm, &hr, HPI_OBJ_OSTREAM,
		HPI_OSTREAM_ANC_GET_INFO);
	if (hpi_handle_indexes(h_outstream, &hm.adapter_index, &hm.obj_index))
		return HPI_ERROR_INVALID_HANDLE;
	hpi_send_recv(&hm, &hr);
	if (hr.error == 0) {
		if (pframes_available)
			*pframes_available =
				hr.u.d.u.stream_info.data_available /
				sizeof(struct hpi_anc_frame);
	}
	return hr.error;
}

u16 hpi_outstream_ancillary_read(u32 h_outstream,
	struct hpi_anc_frame *p_anc_frame_buffer,
	u32 anc_frame_buffer_size_in_bytes,
	u32 number_of_ancillary_frames_to_read)
{
	struct hpi_message hm;
	struct hpi_response hr;

	hpi_init_message_response(&hm, &hr, HPI_OBJ_OSTREAM,
		HPI_OSTREAM_ANC_READ);
	if (hpi_handle_indexes(h_outstream, &hm.adapter_index, &hm.obj_index))
		return HPI_ERROR_INVALID_HANDLE;
	hm.u.d.u.data.pb_data = (u8 *)p_anc_frame_buffer;
	hm.u.d.u.data.data_size =
		number_of_ancillary_frames_to_read *
		sizeof(struct hpi_anc_frame);
	if (hm.u.d.u.data.data_size <= anc_frame_buffer_size_in_bytes)
		hpi_send_recv(&hm, &hr);
	else
		hr.error = HPI_ERROR_INVALID_DATASIZE;
	return hr.error;
}

u16 hpi_outstream_set_time_scale(u32 h_outstream, u32 time_scale)
{
	struct hpi_message hm;
	struct hpi_response hr;

	hpi_init_message_response(&hm, &hr, HPI_OBJ_OSTREAM,
		HPI_OSTREAM_SET_TIMESCALE);
	if (hpi_handle_indexes(h_outstream, &hm.adapter_index, &hm.obj_index))
		return HPI_ERROR_INVALID_HANDLE;

	hm.u.d.u.time_scale = time_scale;

	hpi_send_recv(&hm, &hr);

	return hr.error;
}

u16 hpi_outstream_host_buffer_allocate(u32 h_outstream, u32 size_in_bytes)
{
	struct hpi_message hm;
	struct hpi_response hr;

	hpi_init_message_response(&hm, &hr, HPI_OBJ_OSTREAM,
		HPI_OSTREAM_HOSTBUFFER_ALLOC);
	if (hpi_handle_indexes(h_outstream, &hm.adapter_index, &hm.obj_index))
		return HPI_ERROR_INVALID_HANDLE;
	hm.u.d.u.data.data_size = size_in_bytes;
	hpi_send_recv(&hm, &hr);
	return hr.error;
}

u16 hpi_outstream_host_buffer_get_info(u32 h_outstream, u8 **pp_buffer,
	struct hpi_hostbuffer_status **pp_status)
{
	struct hpi_message hm;
	struct hpi_response hr;

	hpi_init_message_response(&hm, &hr, HPI_OBJ_OSTREAM,
		HPI_OSTREAM_HOSTBUFFER_GET_INFO);
	if (hpi_handle_indexes(h_outstream, &hm.adapter_index, &hm.obj_index))
		return HPI_ERROR_INVALID_HANDLE;
	hpi_send_recv(&hm, &hr);

	if (hr.error == 0) {
		if (pp_buffer)
			*pp_buffer = hr.u.d.u.hostbuffer_info.p_buffer;
		if (pp_status)
			*pp_status = hr.u.d.u.hostbuffer_info.p_status;
	}
	return hr.error;
}

u16 hpi_outstream_host_buffer_free(u32 h_outstream)
{
	struct hpi_message hm;
	struct hpi_response hr;

	hpi_init_message_response(&hm, &hr, HPI_OBJ_OSTREAM,
		HPI_OSTREAM_HOSTBUFFER_FREE);
	if (hpi_handle_indexes(h_outstream, &hm.adapter_index, &hm.obj_index))
		return HPI_ERROR_INVALID_HANDLE;
	hpi_send_recv(&hm, &hr);
	return hr.error;
}

u16 hpi_outstream_group_add(u32 h_outstream, u32 h_stream)
{
	struct hpi_message hm;
	struct hpi_response hr;
	u16 adapter;
	char c_obj_type;

	hpi_init_message_response(&hm, &hr, HPI_OBJ_OSTREAM,
		HPI_OSTREAM_GROUP_ADD);

	if (hpi_handle_indexes(h_outstream, &hm.adapter_index, &hm.obj_index))
		return HPI_ERROR_INVALID_HANDLE;

	if (hpi_handle_indexes(h_stream, &adapter,
			&hm.u.d.u.stream.stream_index))
		return HPI_ERROR_INVALID_HANDLE;

	c_obj_type = hpi_handle_object(h_stream);
	switch (c_obj_type) {
	case HPI_OBJ_OSTREAM:
	case HPI_OBJ_ISTREAM:
		hm.u.d.u.stream.object_type = c_obj_type;
		break;
	default:
		return HPI_ERROR_INVALID_OBJ;
	}
	if (adapter != hm.adapter_index)
		return HPI_ERROR_NO_INTERADAPTER_GROUPS;

	hpi_send_recv(&hm, &hr);
	return hr.error;
}

u16 hpi_outstream_group_get_map(u32 h_outstream, u32 *poutstream_map,
	u32 *pinstream_map)
{
	struct hpi_message hm;
	struct hpi_response hr;

	hpi_init_message_response(&hm, &hr, HPI_OBJ_OSTREAM,
		HPI_OSTREAM_GROUP_GETMAP);
	if (hpi_handle_indexes(h_outstream, &hm.adapter_index, &hm.obj_index))
		return HPI_ERROR_INVALID_HANDLE;
	hpi_send_recv(&hm, &hr);

	if (poutstream_map)
		*poutstream_map = hr.u.d.u.group_info.outstream_group_map;
	if (pinstream_map)
		*pinstream_map = hr.u.d.u.group_info.instream_group_map;

	return hr.error;
}

u16 hpi_outstream_group_reset(u32 h_outstream)
{
	struct hpi_message hm;
	struct hpi_response hr;

	hpi_init_message_response(&hm, &hr, HPI_OBJ_OSTREAM,
		HPI_OSTREAM_GROUP_RESET);
	if (hpi_handle_indexes(h_outstream, &hm.adapter_index, &hm.obj_index))
		return HPI_ERROR_INVALID_HANDLE;
	hpi_send_recv(&hm, &hr);
	return hr.error;
}

u16 hpi_instream_open(u16 adapter_index, u16 instream_index, u32 *ph_instream)
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

u16 hpi_instream_close(u32 h_instream)
{
	struct hpi_message hm;
	struct hpi_response hr;

	hpi_init_message_response(&hm, &hr, HPI_OBJ_ISTREAM,
		HPI_ISTREAM_HOSTBUFFER_FREE);
	if (hpi_handle_indexes(h_instream, &hm.adapter_index, &hm.obj_index))
		return HPI_ERROR_INVALID_HANDLE;
	hpi_send_recv(&hm, &hr);

	hpi_init_message_response(&hm, &hr, HPI_OBJ_ISTREAM,
		HPI_ISTREAM_GROUP_RESET);
	hpi_handle_indexes(h_instream, &hm.adapter_index, &hm.obj_index);
	hpi_send_recv(&hm, &hr);

	hpi_init_message_response(&hm, &hr, HPI_OBJ_ISTREAM,
		HPI_ISTREAM_CLOSE);
	hpi_handle_indexes(h_instream, &hm.adapter_index, &hm.obj_index);
	hpi_send_recv(&hm, &hr);

	return hr.error;
}

u16 hpi_instream_query_format(u32 h_instream,
	const struct hpi_format *p_format)
{
	struct hpi_message hm;
	struct hpi_response hr;

	hpi_init_message_response(&hm, &hr, HPI_OBJ_ISTREAM,
		HPI_ISTREAM_QUERY_FORMAT);
	if (hpi_handle_indexes(h_instream, &hm.adapter_index, &hm.obj_index))
		return HPI_ERROR_INVALID_HANDLE;
	hpi_format_to_msg(&hm.u.d.u.data.format, p_format);

	hpi_send_recv(&hm, &hr);

	return hr.error;
}

u16 hpi_instream_set_format(u32 h_instream, const struct hpi_format *p_format)
{
	struct hpi_message hm;
	struct hpi_response hr;

	hpi_init_message_response(&hm, &hr, HPI_OBJ_ISTREAM,
		HPI_ISTREAM_SET_FORMAT);
	if (hpi_handle_indexes(h_instream, &hm.adapter_index, &hm.obj_index))
		return HPI_ERROR_INVALID_HANDLE;
	hpi_format_to_msg(&hm.u.d.u.data.format, p_format);

	hpi_send_recv(&hm, &hr);

	return hr.error;
}

u16 hpi_instream_read_buf(u32 h_instream, u8 *pb_data, u32 bytes_to_read)
{
	struct hpi_message hm;
	struct hpi_response hr;

	hpi_init_message_response(&hm, &hr, HPI_OBJ_ISTREAM,
		HPI_ISTREAM_READ);
	if (hpi_handle_indexes(h_instream, &hm.adapter_index, &hm.obj_index))
		return HPI_ERROR_INVALID_HANDLE;
	hm.u.d.u.data.data_size = bytes_to_read;
	hm.u.d.u.data.pb_data = pb_data;

	hpi_send_recv(&hm, &hr);

	return hr.error;
}

u16 hpi_instream_start(u32 h_instream)
{
	struct hpi_message hm;
	struct hpi_response hr;

	hpi_init_message_response(&hm, &hr, HPI_OBJ_ISTREAM,
		HPI_ISTREAM_START);
	if (hpi_handle_indexes(h_instream, &hm.adapter_index, &hm.obj_index))
		return HPI_ERROR_INVALID_HANDLE;

	hpi_send_recv(&hm, &hr);

	return hr.error;
}

u16 hpi_instream_wait_start(u32 h_instream)
{
	struct hpi_message hm;
	struct hpi_response hr;

	hpi_init_message_response(&hm, &hr, HPI_OBJ_ISTREAM,
		HPI_ISTREAM_WAIT_START);
	if (hpi_handle_indexes(h_instream, &hm.adapter_index, &hm.obj_index))
		return HPI_ERROR_INVALID_HANDLE;

	hpi_send_recv(&hm, &hr);

	return hr.error;
}

u16 hpi_instream_stop(u32 h_instream)
{
	struct hpi_message hm;
	struct hpi_response hr;

	hpi_init_message_response(&hm, &hr, HPI_OBJ_ISTREAM,
		HPI_ISTREAM_STOP);
	if (hpi_handle_indexes(h_instream, &hm.adapter_index, &hm.obj_index))
		return HPI_ERROR_INVALID_HANDLE;

	hpi_send_recv(&hm, &hr);

	return hr.error;
}

u16 hpi_instream_reset(u32 h_instream)
{
	struct hpi_message hm;
	struct hpi_response hr;

	hpi_init_message_response(&hm, &hr, HPI_OBJ_ISTREAM,
		HPI_ISTREAM_RESET);
	if (hpi_handle_indexes(h_instream, &hm.adapter_index, &hm.obj_index))
		return HPI_ERROR_INVALID_HANDLE;

	hpi_send_recv(&hm, &hr);

	return hr.error;
}

u16 hpi_instream_get_info_ex(u32 h_instream, u16 *pw_state, u32 *pbuffer_size,
	u32 *pdata_recorded, u32 *psamples_recorded,
	u32 *pauxiliary_data_recorded)
{
	struct hpi_message hm;
	struct hpi_response hr;
	hpi_init_message_response(&hm, &hr, HPI_OBJ_ISTREAM,
		HPI_ISTREAM_GET_INFO);
	if (hpi_handle_indexes(h_instream, &hm.adapter_index, &hm.obj_index))
		return HPI_ERROR_INVALID_HANDLE;

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

u16 hpi_instream_ancillary_reset(u32 h_instream, u16 bytes_per_frame,
	u16 mode, u16 alignment, u16 idle_bit)
{
	struct hpi_message hm;
	struct hpi_response hr;
	hpi_init_message_response(&hm, &hr, HPI_OBJ_ISTREAM,
		HPI_ISTREAM_ANC_RESET);
	if (hpi_handle_indexes(h_instream, &hm.adapter_index, &hm.obj_index))
		return HPI_ERROR_INVALID_HANDLE;
	hm.u.d.u.data.format.attributes = bytes_per_frame;
	hm.u.d.u.data.format.format = (mode << 8) | (alignment & 0xff);
	hm.u.d.u.data.format.channels = idle_bit;
	hpi_send_recv(&hm, &hr);
	return hr.error;
}

u16 hpi_instream_ancillary_get_info(u32 h_instream, u32 *pframe_space)
{
	struct hpi_message hm;
	struct hpi_response hr;
	hpi_init_message_response(&hm, &hr, HPI_OBJ_ISTREAM,
		HPI_ISTREAM_ANC_GET_INFO);
	if (hpi_handle_indexes(h_instream, &hm.adapter_index, &hm.obj_index))
		return HPI_ERROR_INVALID_HANDLE;
	hpi_send_recv(&hm, &hr);
	if (pframe_space)
		*pframe_space =
			(hr.u.d.u.stream_info.buffer_size -
			hr.u.d.u.stream_info.data_available) /
			sizeof(struct hpi_anc_frame);
	return hr.error;
}

u16 hpi_instream_ancillary_write(u32 h_instream,
	const struct hpi_anc_frame *p_anc_frame_buffer,
	u32 anc_frame_buffer_size_in_bytes,
	u32 number_of_ancillary_frames_to_write)
{
	struct hpi_message hm;
	struct hpi_response hr;

	hpi_init_message_response(&hm, &hr, HPI_OBJ_ISTREAM,
		HPI_ISTREAM_ANC_WRITE);
	if (hpi_handle_indexes(h_instream, &hm.adapter_index, &hm.obj_index))
		return HPI_ERROR_INVALID_HANDLE;
	hm.u.d.u.data.pb_data = (u8 *)p_anc_frame_buffer;
	hm.u.d.u.data.data_size =
		number_of_ancillary_frames_to_write *
		sizeof(struct hpi_anc_frame);
	if (hm.u.d.u.data.data_size <= anc_frame_buffer_size_in_bytes)
		hpi_send_recv(&hm, &hr);
	else
		hr.error = HPI_ERROR_INVALID_DATASIZE;
	return hr.error;
}

u16 hpi_instream_host_buffer_allocate(u32 h_instream, u32 size_in_bytes)
{

	struct hpi_message hm;
	struct hpi_response hr;

	hpi_init_message_response(&hm, &hr, HPI_OBJ_ISTREAM,
		HPI_ISTREAM_HOSTBUFFER_ALLOC);
	if (hpi_handle_indexes(h_instream, &hm.adapter_index, &hm.obj_index))
		return HPI_ERROR_INVALID_HANDLE;
	hm.u.d.u.data.data_size = size_in_bytes;
	hpi_send_recv(&hm, &hr);
	return hr.error;
}

u16 hpi_instream_host_buffer_get_info(u32 h_instream, u8 **pp_buffer,
	struct hpi_hostbuffer_status **pp_status)
{
	struct hpi_message hm;
	struct hpi_response hr;

	hpi_init_message_response(&hm, &hr, HPI_OBJ_ISTREAM,
		HPI_ISTREAM_HOSTBUFFER_GET_INFO);
	if (hpi_handle_indexes(h_instream, &hm.adapter_index, &hm.obj_index))
		return HPI_ERROR_INVALID_HANDLE;
	hpi_send_recv(&hm, &hr);

	if (hr.error == 0) {
		if (pp_buffer)
			*pp_buffer = hr.u.d.u.hostbuffer_info.p_buffer;
		if (pp_status)
			*pp_status = hr.u.d.u.hostbuffer_info.p_status;
	}
	return hr.error;
}

u16 hpi_instream_host_buffer_free(u32 h_instream)
{

	struct hpi_message hm;
	struct hpi_response hr;

	hpi_init_message_response(&hm, &hr, HPI_OBJ_ISTREAM,
		HPI_ISTREAM_HOSTBUFFER_FREE);
	if (hpi_handle_indexes(h_instream, &hm.adapter_index, &hm.obj_index))
		return HPI_ERROR_INVALID_HANDLE;
	hpi_send_recv(&hm, &hr);
	return hr.error;
}

u16 hpi_instream_group_add(u32 h_instream, u32 h_stream)
{
	struct hpi_message hm;
	struct hpi_response hr;
	u16 adapter;
	char c_obj_type;

	hpi_init_message_response(&hm, &hr, HPI_OBJ_ISTREAM,
		HPI_ISTREAM_GROUP_ADD);
	hr.error = 0;

	if (hpi_handle_indexes(h_instream, &hm.adapter_index, &hm.obj_index))
		return HPI_ERROR_INVALID_HANDLE;

	if (hpi_handle_indexes(h_stream, &adapter,
			&hm.u.d.u.stream.stream_index))
		return HPI_ERROR_INVALID_HANDLE;

	c_obj_type = hpi_handle_object(h_stream);

	switch (c_obj_type) {
	case HPI_OBJ_OSTREAM:
	case HPI_OBJ_ISTREAM:
		hm.u.d.u.stream.object_type = c_obj_type;
		break;
	default:
		return HPI_ERROR_INVALID_OBJ;
	}

	if (adapter != hm.adapter_index)
		return HPI_ERROR_NO_INTERADAPTER_GROUPS;

	hpi_send_recv(&hm, &hr);
	return hr.error;
}

u16 hpi_instream_group_get_map(u32 h_instream, u32 *poutstream_map,
	u32 *pinstream_map)
{
	struct hpi_message hm;
	struct hpi_response hr;

	hpi_init_message_response(&hm, &hr, HPI_OBJ_ISTREAM,
		HPI_ISTREAM_HOSTBUFFER_FREE);
	if (hpi_handle_indexes(h_instream, &hm.adapter_index, &hm.obj_index))
		return HPI_ERROR_INVALID_HANDLE;
	hpi_send_recv(&hm, &hr);

	if (poutstream_map)
		*poutstream_map = hr.u.d.u.group_info.outstream_group_map;
	if (pinstream_map)
		*pinstream_map = hr.u.d.u.group_info.instream_group_map;

	return hr.error;
}

u16 hpi_instream_group_reset(u32 h_instream)
{
	struct hpi_message hm;
	struct hpi_response hr;

	hpi_init_message_response(&hm, &hr, HPI_OBJ_ISTREAM,
		HPI_ISTREAM_GROUP_RESET);
	if (hpi_handle_indexes(h_instream, &hm.adapter_index, &hm.obj_index))
		return HPI_ERROR_INVALID_HANDLE;
	hpi_send_recv(&hm, &hr);
	return hr.error;
}

u16 hpi_mixer_open(u16 adapter_index, u32 *ph_mixer)
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

u16 hpi_mixer_close(u32 h_mixer)
{
	struct hpi_message hm;
	struct hpi_response hr;

	hpi_init_message_response(&hm, &hr, HPI_OBJ_MIXER, HPI_MIXER_CLOSE);
	if (hpi_handle_indexes(h_mixer, &hm.adapter_index, NULL))
		return HPI_ERROR_INVALID_HANDLE;

	hpi_send_recv(&hm, &hr);
	return hr.error;
}

u16 hpi_mixer_get_control(u32 h_mixer, u16 src_node_type,
	u16 src_node_type_index, u16 dst_node_type, u16 dst_node_type_index,
	u16 control_type, u32 *ph_control)
{
	struct hpi_message hm;
	struct hpi_response hr;
	hpi_init_message_response(&hm, &hr, HPI_OBJ_MIXER,
		HPI_MIXER_GET_CONTROL);
	if (hpi_handle_indexes(h_mixer, &hm.adapter_index, NULL))
		return HPI_ERROR_INVALID_HANDLE;
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

u16 hpi_mixer_get_control_by_index(u32 h_mixer, u16 control_index,
	u16 *pw_src_node_type, u16 *pw_src_node_index, u16 *pw_dst_node_type,
	u16 *pw_dst_node_index, u16 *pw_control_type, u32 *ph_control)
{
	struct hpi_message hm;
	struct hpi_response hr;
	hpi_init_message_response(&hm, &hr, HPI_OBJ_MIXER,
		HPI_MIXER_GET_CONTROL_BY_INDEX);
	if (hpi_handle_indexes(h_mixer, &hm.adapter_index, NULL))
		return HPI_ERROR_INVALID_HANDLE;
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

u16 hpi_mixer_store(u32 h_mixer, enum HPI_MIXER_STORE_COMMAND command,
	u16 index)
{
	struct hpi_message hm;
	struct hpi_response hr;
	hpi_init_message_response(&hm, &hr, HPI_OBJ_MIXER, HPI_MIXER_STORE);
	if (hpi_handle_indexes(h_mixer, &hm.adapter_index, NULL))
		return HPI_ERROR_INVALID_HANDLE;
	hm.u.mx.store.command = command;
	hm.u.mx.store.index = index;
	hpi_send_recv(&hm, &hr);
	return hr.error;
}

static
u16 hpi_control_param_set(const u32 h_control, const u16 attrib,
	const u32 param1, const u32 param2)
{
	struct hpi_message hm;
	struct hpi_response hr;

	hpi_init_message_response(&hm, &hr, HPI_OBJ_CONTROL,
		HPI_CONTROL_SET_STATE);
	if (hpi_handle_indexes(h_control, &hm.adapter_index, &hm.obj_index))
		return HPI_ERROR_INVALID_HANDLE;
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
	if (hpi_handle_indexes(h_control, &hm.adapter_index, &hm.obj_index))
		return HPI_ERROR_INVALID_HANDLE;
	hm.u.c.attribute = attrib;
	hm.u.c.an_log_value[0] = sv0;
	hm.u.c.an_log_value[1] = sv1;
	hpi_send_recv(&hm, &hr);
	return hr.error;
}

static
u16 hpi_control_param_get(const u32 h_control, const u16 attrib, u32 param1,
	u32 param2, u32 *pparam1, u32 *pparam2)
{
	struct hpi_message hm;
	struct hpi_response hr;

	hpi_init_message_response(&hm, &hr, HPI_OBJ_CONTROL,
		HPI_CONTROL_GET_STATE);
	if (hpi_handle_indexes(h_control, &hm.adapter_index, &hm.obj_index))
		return HPI_ERROR_INVALID_HANDLE;
	hm.u.c.attribute = attrib;
	hm.u.c.param1 = param1;
	hm.u.c.param2 = param2;
	hpi_send_recv(&hm, &hr);

	*pparam1 = hr.u.c.param1;
	if (pparam2)
		*pparam2 = hr.u.c.param2;

	return hr.error;
}

#define hpi_control_param1_get(h, a, p1) \
		hpi_control_param_get(h, a, 0, 0, p1, NULL)
#define hpi_control_param2_get(h, a, p1, p2) \
		hpi_control_param_get(h, a, 0, 0, p1, p2)

static u16 hpi_control_log_get2(u32 h_control, u16 attrib, short *sv0,
	short *sv1)
{
	struct hpi_message hm;
	struct hpi_response hr;
	hpi_init_message_response(&hm, &hr, HPI_OBJ_CONTROL,
		HPI_CONTROL_GET_STATE);
	if (hpi_handle_indexes(h_control, &hm.adapter_index, &hm.obj_index))
		return HPI_ERROR_INVALID_HANDLE;
	hm.u.c.attribute = attrib;

	hpi_send_recv(&hm, &hr);
	*sv0 = hr.u.c.an_log_value[0];
	if (sv1)
		*sv1 = hr.u.c.an_log_value[1];
	return hr.error;
}

static
u16 hpi_control_query(const u32 h_control, const u16 attrib, const u32 index,
	const u32 param, u32 *psetting)
{
	struct hpi_message hm;
	struct hpi_response hr;

	hpi_init_message_response(&hm, &hr, HPI_OBJ_CONTROL,
		HPI_CONTROL_GET_INFO);
	if (hpi_handle_indexes(h_control, &hm.adapter_index, &hm.obj_index))
		return HPI_ERROR_INVALID_HANDLE;

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
	u16 err = 0;

	if ((string_length < 1) || (string_length > 256))
		return HPI_ERROR_INVALID_CONTROL_VALUE;
	for (sub_string_index = 0; sub_string_index < string_length;
		sub_string_index += 8) {
		struct hpi_message hm;
		struct hpi_response hr;

		hpi_init_message_response(&hm, &hr, HPI_OBJ_CONTROL,
			HPI_CONTROL_GET_STATE);
		if (hpi_handle_indexes(h_control, &hm.adapter_index,
				&hm.obj_index))
			return HPI_ERROR_INVALID_HANDLE;
		hm.u.c.attribute = attribute;
		hm.u.c.param1 = sub_string_index;
		hm.u.c.param2 = 0;
		hpi_send_recv(&hm, &hr);

		if (sub_string_index == 0
			&& (hr.u.cu.chars8.remaining_chars + 8) >
			string_length)
			return HPI_ERROR_INVALID_CONTROL_VALUE;

		if (hr.error) {
			err = hr.error;
			break;
		}
		for (j = 0; j < 8; j++) {
			c = hr.u.cu.chars8.sz_data[j];
			psz_string[sub_string_index + j] = c;
			n++;
			if (n >= string_length) {
				psz_string[string_length - 1] = 0;
				err = HPI_ERROR_INVALID_CONTROL_VALUE;
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
	return err;
}

u16 hpi_aesebu_receiver_query_format(const u32 h_aes_rx, const u32 index,
	u16 *pw_format)
{
	u32 qr;
	u16 err;

	err = hpi_control_query(h_aes_rx, HPI_AESEBURX_FORMAT, index, 0, &qr);
	*pw_format = (u16)qr;
	return err;
}

u16 hpi_aesebu_receiver_set_format(u32 h_control, u16 format)
{
	return hpi_control_param_set(h_control, HPI_AESEBURX_FORMAT, format,
		0);
}

u16 hpi_aesebu_receiver_get_format(u32 h_control, u16 *pw_format)
{
	u16 err;
	u32 param;

	err = hpi_control_param1_get(h_control, HPI_AESEBURX_FORMAT, &param);
	if (!err && pw_format)
		*pw_format = (u16)param;

	return err;
}

u16 hpi_aesebu_receiver_get_sample_rate(u32 h_control, u32 *psample_rate)
{
	return hpi_control_param1_get(h_control, HPI_AESEBURX_SAMPLERATE,
		psample_rate);
}

u16 hpi_aesebu_receiver_get_user_data(u32 h_control, u16 index, u16 *pw_data)
{
	struct hpi_message hm;
	struct hpi_response hr;
	hpi_init_message_response(&hm, &hr, HPI_OBJ_CONTROL,
		HPI_CONTROL_GET_STATE);
	if (hpi_handle_indexes(h_control, &hm.adapter_index, &hm.obj_index))
		return HPI_ERROR_INVALID_HANDLE;
	hm.u.c.attribute = HPI_AESEBURX_USERDATA;
	hm.u.c.param1 = index;

	hpi_send_recv(&hm, &hr);

	if (pw_data)
		*pw_data = (u16)hr.u.c.param2;
	return hr.error;
}

u16 hpi_aesebu_receiver_get_channel_status(u32 h_control, u16 index,
	u16 *pw_data)
{
	struct hpi_message hm;
	struct hpi_response hr;
	hpi_init_message_response(&hm, &hr, HPI_OBJ_CONTROL,
		HPI_CONTROL_GET_STATE);
	if (hpi_handle_indexes(h_control, &hm.adapter_index, &hm.obj_index))
		return HPI_ERROR_INVALID_HANDLE;
	hm.u.c.attribute = HPI_AESEBURX_CHANNELSTATUS;
	hm.u.c.param1 = index;

	hpi_send_recv(&hm, &hr);

	if (pw_data)
		*pw_data = (u16)hr.u.c.param2;
	return hr.error;
}

u16 hpi_aesebu_receiver_get_error_status(u32 h_control, u16 *pw_error_data)
{
	u32 error_data = 0;
	u16 err = 0;

	err = hpi_control_param1_get(h_control, HPI_AESEBURX_ERRORSTATUS,
		&error_data);
	if (pw_error_data)
		*pw_error_data = (u16)error_data;
	return err;
}

u16 hpi_aesebu_transmitter_set_sample_rate(u32 h_control, u32 sample_rate)
{
	return hpi_control_param_set(h_control, HPI_AESEBUTX_SAMPLERATE,
		sample_rate, 0);
}

u16 hpi_aesebu_transmitter_set_user_data(u32 h_control, u16 index, u16 data)
{
	return hpi_control_param_set(h_control, HPI_AESEBUTX_USERDATA, index,
		data);
}

u16 hpi_aesebu_transmitter_set_channel_status(u32 h_control, u16 index,
	u16 data)
{
	return hpi_control_param_set(h_control, HPI_AESEBUTX_CHANNELSTATUS,
		index, data);
}

u16 hpi_aesebu_transmitter_get_channel_status(u32 h_control, u16 index,
	u16 *pw_data)
{
	return HPI_ERROR_INVALID_OPERATION;
}

u16 hpi_aesebu_transmitter_query_format(const u32 h_aes_tx, const u32 index,
	u16 *pw_format)
{
	u32 qr;
	u16 err;

	err = hpi_control_query(h_aes_tx, HPI_AESEBUTX_FORMAT, index, 0, &qr);
	*pw_format = (u16)qr;
	return err;
}

u16 hpi_aesebu_transmitter_set_format(u32 h_control, u16 output_format)
{
	return hpi_control_param_set(h_control, HPI_AESEBUTX_FORMAT,
		output_format, 0);
}

u16 hpi_aesebu_transmitter_get_format(u32 h_control, u16 *pw_output_format)
{
	u16 err;
	u32 param;

	err = hpi_control_param1_get(h_control, HPI_AESEBUTX_FORMAT, &param);
	if (!err && pw_output_format)
		*pw_output_format = (u16)param;

	return err;
}

u16 hpi_bitstream_set_clock_edge(u32 h_control, u16 edge_type)
{
	return hpi_control_param_set(h_control, HPI_BITSTREAM_CLOCK_EDGE,
		edge_type, 0);
}

u16 hpi_bitstream_set_data_polarity(u32 h_control, u16 polarity)
{
	return hpi_control_param_set(h_control, HPI_BITSTREAM_DATA_POLARITY,
		polarity, 0);
}

u16 hpi_bitstream_get_activity(u32 h_control, u16 *pw_clk_activity,
	u16 *pw_data_activity)
{
	struct hpi_message hm;
	struct hpi_response hr;
	hpi_init_message_response(&hm, &hr, HPI_OBJ_CONTROL,
		HPI_CONTROL_GET_STATE);
	if (hpi_handle_indexes(h_control, &hm.adapter_index, &hm.obj_index))
		return HPI_ERROR_INVALID_HANDLE;
	hm.u.c.attribute = HPI_BITSTREAM_ACTIVITY;
	hpi_send_recv(&hm, &hr);
	if (pw_clk_activity)
		*pw_clk_activity = (u16)hr.u.c.param1;
	if (pw_data_activity)
		*pw_data_activity = (u16)hr.u.c.param2;
	return hr.error;
}

u16 hpi_channel_mode_query_mode(const u32 h_mode, const u32 index,
	u16 *pw_mode)
{
	u32 qr;
	u16 err;

	err = hpi_control_query(h_mode, HPI_CHANNEL_MODE_MODE, index, 0, &qr);
	*pw_mode = (u16)qr;
	return err;
}

u16 hpi_channel_mode_set(u32 h_control, u16 mode)
{
	return hpi_control_param_set(h_control, HPI_CHANNEL_MODE_MODE, mode,
		0);
}

u16 hpi_channel_mode_get(u32 h_control, u16 *mode)
{
	u32 mode32 = 0;
	u16 err = hpi_control_param1_get(h_control,
		HPI_CHANNEL_MODE_MODE, &mode32);
	if (mode)
		*mode = (u16)mode32;
	return err;
}

u16 hpi_cobranet_hmi_write(u32 h_control, u32 hmi_address, u32 byte_count,
	u8 *pb_data)
{
	struct hpi_msg_cobranet_hmiwrite hm;
	struct hpi_response_header hr;

	hpi_init_message_responseV1(&hm.h, sizeof(hm), &hr, sizeof(hr),
		HPI_OBJ_CONTROL, HPI_CONTROL_SET_STATE);

	if (hpi_handle_indexes(h_control, &hm.h.adapter_index,
			&hm.h.obj_index))
		return HPI_ERROR_INVALID_HANDLE;

	if (byte_count > sizeof(hm.bytes))
		return HPI_ERROR_MESSAGE_BUFFER_TOO_SMALL;

	hm.p.attribute = HPI_COBRANET_SET;
	hm.p.byte_count = byte_count;
	hm.p.hmi_address = hmi_address;
	memcpy(hm.bytes, pb_data, byte_count);
	hm.h.size = (u16)(sizeof(hm.h) + sizeof(hm.p) + byte_count);

	hpi_send_recvV1(&hm.h, &hr);
	return hr.error;
}

u16 hpi_cobranet_hmi_read(u32 h_control, u32 hmi_address, u32 max_byte_count,
	u32 *pbyte_count, u8 *pb_data)
{
	struct hpi_msg_cobranet_hmiread hm;
	struct hpi_res_cobranet_hmiread hr;

	hpi_init_message_responseV1(&hm.h, sizeof(hm), &hr.h, sizeof(hr),
		HPI_OBJ_CONTROL, HPI_CONTROL_GET_STATE);

	if (hpi_handle_indexes(h_control, &hm.h.adapter_index,
			&hm.h.obj_index))
		return HPI_ERROR_INVALID_HANDLE;

	if (max_byte_count > sizeof(hr.bytes))
		return HPI_ERROR_RESPONSE_BUFFER_TOO_SMALL;

	hm.p.attribute = HPI_COBRANET_GET;
	hm.p.byte_count = max_byte_count;
	hm.p.hmi_address = hmi_address;

	hpi_send_recvV1(&hm.h, &hr.h);

	if (!hr.h.error && pb_data) {
		if (hr.byte_count > sizeof(hr.bytes))

			return HPI_ERROR_RESPONSE_BUFFER_TOO_SMALL;

		*pbyte_count = hr.byte_count;

		if (hr.byte_count < max_byte_count)
			max_byte_count = *pbyte_count;

		memcpy(pb_data, hr.bytes, max_byte_count);
	}
	return hr.h.error;
}

u16 hpi_cobranet_hmi_get_status(u32 h_control, u32 *pstatus,
	u32 *preadable_size, u32 *pwriteable_size)
{
	struct hpi_message hm;
	struct hpi_response hr;

	hpi_init_message_response(&hm, &hr, HPI_OBJ_CONTROL,
		HPI_CONTROL_GET_STATE);
	if (hpi_handle_indexes(h_control, &hm.adapter_index, &hm.obj_index))
		return HPI_ERROR_INVALID_HANDLE;

	hm.u.c.attribute = HPI_COBRANET_GET_STATUS;

	hpi_send_recv(&hm, &hr);
	if (!hr.error) {
		if (pstatus)
			*pstatus = hr.u.cu.cobranet.status.status;
		if (preadable_size)
			*preadable_size =
				hr.u.cu.cobranet.status.readable_size;
		if (pwriteable_size)
			*pwriteable_size =
				hr.u.cu.cobranet.status.writeable_size;
	}
	return hr.error;
}

u16 hpi_cobranet_get_ip_address(u32 h_control, u32 *pdw_ip_address)
{
	u32 byte_count;
	u32 iP;
	u16 err;

	err = hpi_cobranet_hmi_read(h_control,
		HPI_COBRANET_HMI_cobra_ip_mon_currentIP, 4, &byte_count,
		(u8 *)&iP);

	*pdw_ip_address =
		((iP & 0xff000000) >> 8) | ((iP & 0x00ff0000) << 8) | ((iP &
			0x0000ff00) >> 8) | ((iP & 0x000000ff) << 8);

	if (err)
		*pdw_ip_address = 0;

	return err;

}

u16 hpi_cobranet_set_ip_address(u32 h_control, u32 dw_ip_address)
{
	u32 iP;
	u16 err;

	iP = ((dw_ip_address & 0xff000000) >> 8) | ((dw_ip_address &
			0x00ff0000) << 8) | ((dw_ip_address & 0x0000ff00) >>
		8) | ((dw_ip_address & 0x000000ff) << 8);

	err = hpi_cobranet_hmi_write(h_control,
		HPI_COBRANET_HMI_cobra_ip_mon_currentIP, 4, (u8 *)&iP);

	return err;

}

u16 hpi_cobranet_get_static_ip_address(u32 h_control, u32 *pdw_ip_address)
{
	u32 byte_count;
	u32 iP;
	u16 err;
	err = hpi_cobranet_hmi_read(h_control,
		HPI_COBRANET_HMI_cobra_ip_mon_staticIP, 4, &byte_count,
		(u8 *)&iP);

	*pdw_ip_address =
		((iP & 0xff000000) >> 8) | ((iP & 0x00ff0000) << 8) | ((iP &
			0x0000ff00) >> 8) | ((iP & 0x000000ff) << 8);

	if (err)
		*pdw_ip_address = 0;

	return err;

}

u16 hpi_cobranet_set_static_ip_address(u32 h_control, u32 dw_ip_address)
{
	u32 iP;
	u16 err;

	iP = ((dw_ip_address & 0xff000000) >> 8) | ((dw_ip_address &
			0x00ff0000) << 8) | ((dw_ip_address & 0x0000ff00) >>
		8) | ((dw_ip_address & 0x000000ff) << 8);

	err = hpi_cobranet_hmi_write(h_control,
		HPI_COBRANET_HMI_cobra_ip_mon_staticIP, 4, (u8 *)&iP);

	return err;

}

u16 hpi_cobranet_get_macaddress(u32 h_control, u32 *p_mac_msbs,
	u32 *p_mac_lsbs)
{
	u32 byte_count;
	u16 err;
	u32 mac;

	err = hpi_cobranet_hmi_read(h_control,
		HPI_COBRANET_HMI_cobra_if_phy_address, 4, &byte_count,
		(u8 *)&mac);

	if (!err) {
		*p_mac_msbs =
			((mac & 0xff000000) >> 8) | ((mac & 0x00ff0000) << 8)
			| ((mac & 0x0000ff00) >> 8) | ((mac & 0x000000ff) <<
			8);

		err = hpi_cobranet_hmi_read(h_control,
			HPI_COBRANET_HMI_cobra_if_phy_address + 1, 4,
			&byte_count, (u8 *)&mac);
	}

	if (!err) {
		*p_mac_lsbs =
			((mac & 0xff000000) >> 8) | ((mac & 0x00ff0000) << 8)
			| ((mac & 0x0000ff00) >> 8) | ((mac & 0x000000ff) <<
			8);
	} else {
		*p_mac_msbs = 0;
		*p_mac_lsbs = 0;
	}

	return err;
}

u16 hpi_compander_set_enable(u32 h_control, u32 enable)
{
	return hpi_control_param_set(h_control, HPI_GENERIC_ENABLE, enable,
		0);
}

u16 hpi_compander_get_enable(u32 h_control, u32 *enable)
{
	return hpi_control_param1_get(h_control, HPI_GENERIC_ENABLE, enable);
}

u16 hpi_compander_set_makeup_gain(u32 h_control, short makeup_gain0_01dB)
{
	return hpi_control_log_set2(h_control, HPI_COMPANDER_MAKEUPGAIN,
		makeup_gain0_01dB, 0);
}

u16 hpi_compander_get_makeup_gain(u32 h_control, short *makeup_gain0_01dB)
{
	return hpi_control_log_get2(h_control, HPI_COMPANDER_MAKEUPGAIN,
		makeup_gain0_01dB, NULL);
}

u16 hpi_compander_set_attack_time_constant(u32 h_control, unsigned int index,
	u32 attack)
{
	return hpi_control_param_set(h_control, HPI_COMPANDER_ATTACK, attack,
		index);
}

u16 hpi_compander_get_attack_time_constant(u32 h_control, unsigned int index,
	u32 *attack)
{
	return hpi_control_param_get(h_control, HPI_COMPANDER_ATTACK, 0,
		index, attack, NULL);
}

u16 hpi_compander_set_decay_time_constant(u32 h_control, unsigned int index,
	u32 decay)
{
	return hpi_control_param_set(h_control, HPI_COMPANDER_DECAY, decay,
		index);
}

u16 hpi_compander_get_decay_time_constant(u32 h_control, unsigned int index,
	u32 *decay)
{
	return hpi_control_param_get(h_control, HPI_COMPANDER_DECAY, 0, index,
		decay, NULL);

}

u16 hpi_compander_set_threshold(u32 h_control, unsigned int index,
	short threshold0_01dB)
{
	struct hpi_message hm;
	struct hpi_response hr;

	hpi_init_message_response(&hm, &hr, HPI_OBJ_CONTROL,
		HPI_CONTROL_SET_STATE);
	if (hpi_handle_indexes(h_control, &hm.adapter_index, &hm.obj_index))
		return HPI_ERROR_INVALID_HANDLE;
	hm.u.c.attribute = HPI_COMPANDER_THRESHOLD;
	hm.u.c.param2 = index;
	hm.u.c.an_log_value[0] = threshold0_01dB;

	hpi_send_recv(&hm, &hr);

	return hr.error;
}

u16 hpi_compander_get_threshold(u32 h_control, unsigned int index,
	short *threshold0_01dB)
{
	struct hpi_message hm;
	struct hpi_response hr;

	hpi_init_message_response(&hm, &hr, HPI_OBJ_CONTROL,
		HPI_CONTROL_GET_STATE);
	if (hpi_handle_indexes(h_control, &hm.adapter_index, &hm.obj_index))
		return HPI_ERROR_INVALID_HANDLE;
	hm.u.c.attribute = HPI_COMPANDER_THRESHOLD;
	hm.u.c.param2 = index;

	hpi_send_recv(&hm, &hr);
	*threshold0_01dB = hr.u.c.an_log_value[0];

	return hr.error;
}

u16 hpi_compander_set_ratio(u32 h_control, u32 index, u32 ratio100)
{
	return hpi_control_param_set(h_control, HPI_COMPANDER_RATIO, ratio100,
		index);
}

u16 hpi_compander_get_ratio(u32 h_control, u32 index, u32 *ratio100)
{
	return hpi_control_param_get(h_control, HPI_COMPANDER_RATIO, 0, index,
		ratio100, NULL);
}

u16 hpi_level_query_range(u32 h_control, short *min_gain_01dB,
	short *max_gain_01dB, short *step_gain_01dB)
{
	struct hpi_message hm;
	struct hpi_response hr;

	hpi_init_message_response(&hm, &hr, HPI_OBJ_CONTROL,
		HPI_CONTROL_GET_STATE);
	if (hpi_handle_indexes(h_control, &hm.adapter_index, &hm.obj_index))
		return HPI_ERROR_INVALID_HANDLE;
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

u16 hpi_level_set_gain(u32 h_control, short an_gain0_01dB[HPI_MAX_CHANNELS]
	)
{
	return hpi_control_log_set2(h_control, HPI_LEVEL_GAIN,
		an_gain0_01dB[0], an_gain0_01dB[1]);
}

u16 hpi_level_get_gain(u32 h_control, short an_gain0_01dB[HPI_MAX_CHANNELS]
	)
{
	return hpi_control_log_get2(h_control, HPI_LEVEL_GAIN,
		&an_gain0_01dB[0], &an_gain0_01dB[1]);
}

u16 hpi_meter_query_channels(const u32 h_meter, u32 *p_channels)
{
	return hpi_control_query(h_meter, HPI_METER_NUM_CHANNELS, 0, 0,
		p_channels);
}

u16 hpi_meter_get_peak(u32 h_control, short an_peakdB[HPI_MAX_CHANNELS]
	)
{
	short i = 0;

	struct hpi_message hm;
	struct hpi_response hr;

	hpi_init_message_response(&hm, &hr, HPI_OBJ_CONTROL,
		HPI_CONTROL_GET_STATE);
	if (hpi_handle_indexes(h_control, &hm.adapter_index, &hm.obj_index))
		return HPI_ERROR_INVALID_HANDLE;
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

u16 hpi_meter_get_rms(u32 h_control, short an_rmsdB[HPI_MAX_CHANNELS]
	)
{
	short i = 0;

	struct hpi_message hm;
	struct hpi_response hr;

	hpi_init_message_response(&hm, &hr, HPI_OBJ_CONTROL,
		HPI_CONTROL_GET_STATE);
	if (hpi_handle_indexes(h_control, &hm.adapter_index, &hm.obj_index))
		return HPI_ERROR_INVALID_HANDLE;
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

u16 hpi_meter_set_rms_ballistics(u32 h_control, u16 attack, u16 decay)
{
	return hpi_control_param_set(h_control, HPI_METER_RMS_BALLISTICS,
		attack, decay);
}

u16 hpi_meter_get_rms_ballistics(u32 h_control, u16 *pn_attack, u16 *pn_decay)
{
	u32 attack;
	u32 decay;
	u16 error;

	error = hpi_control_param2_get(h_control, HPI_METER_RMS_BALLISTICS,
		&attack, &decay);

	if (pn_attack)
		*pn_attack = (unsigned short)attack;
	if (pn_decay)
		*pn_decay = (unsigned short)decay;

	return error;
}

u16 hpi_meter_set_peak_ballistics(u32 h_control, u16 attack, u16 decay)
{
	return hpi_control_param_set(h_control, HPI_METER_PEAK_BALLISTICS,
		attack, decay);
}

u16 hpi_meter_get_peak_ballistics(u32 h_control, u16 *pn_attack,
	u16 *pn_decay)
{
	u32 attack;
	u32 decay;
	u16 error;

	error = hpi_control_param2_get(h_control, HPI_METER_PEAK_BALLISTICS,
		&attack, &decay);

	if (pn_attack)
		*pn_attack = (short)attack;
	if (pn_decay)
		*pn_decay = (short)decay;

	return error;
}

u16 hpi_microphone_set_phantom_power(u32 h_control, u16 on_off)
{
	return hpi_control_param_set(h_control, HPI_MICROPHONE_PHANTOM_POWER,
		(u32)on_off, 0);
}

u16 hpi_microphone_get_phantom_power(u32 h_control, u16 *pw_on_off)
{
	u16 error = 0;
	u32 on_off = 0;
	error = hpi_control_param1_get(h_control,
		HPI_MICROPHONE_PHANTOM_POWER, &on_off);
	if (pw_on_off)
		*pw_on_off = (u16)on_off;
	return error;
}

u16 hpi_multiplexer_set_source(u32 h_control, u16 source_node_type,
	u16 source_node_index)
{
	return hpi_control_param_set(h_control, HPI_MULTIPLEXER_SOURCE,
		source_node_type, source_node_index);
}

u16 hpi_multiplexer_get_source(u32 h_control, u16 *source_node_type,
	u16 *source_node_index)
{
	u32 node, index;
	u16 err = hpi_control_param2_get(h_control,
		HPI_MULTIPLEXER_SOURCE, &node,
		&index);
	if (source_node_type)
		*source_node_type = (u16)node;
	if (source_node_index)
		*source_node_index = (u16)index;
	return err;
}

u16 hpi_multiplexer_query_source(u32 h_control, u16 index,
	u16 *source_node_type, u16 *source_node_index)
{
	struct hpi_message hm;
	struct hpi_response hr;
	hpi_init_message_response(&hm, &hr, HPI_OBJ_CONTROL,
		HPI_CONTROL_GET_STATE);
	if (hpi_handle_indexes(h_control, &hm.adapter_index, &hm.obj_index))
		return HPI_ERROR_INVALID_HANDLE;
	hm.u.c.attribute = HPI_MULTIPLEXER_QUERYSOURCE;
	hm.u.c.param1 = index;

	hpi_send_recv(&hm, &hr);

	if (source_node_type)
		*source_node_type = (u16)hr.u.c.param1;
	if (source_node_index)
		*source_node_index = (u16)hr.u.c.param2;
	return hr.error;
}

u16 hpi_parametric_eq_get_info(u32 h_control, u16 *pw_number_of_bands,
	u16 *pw_on_off)
{
	u32 oB = 0;
	u32 oO = 0;
	u16 error = 0;

	error = hpi_control_param2_get(h_control, HPI_EQUALIZER_NUM_FILTERS,
		&oO, &oB);
	if (pw_number_of_bands)
		*pw_number_of_bands = (u16)oB;
	if (pw_on_off)
		*pw_on_off = (u16)oO;
	return error;
}

u16 hpi_parametric_eq_set_state(u32 h_control, u16 on_off)
{
	return hpi_control_param_set(h_control, HPI_EQUALIZER_NUM_FILTERS,
		on_off, 0);
}

u16 hpi_parametric_eq_get_band(u32 h_control, u16 index, u16 *pn_type,
	u32 *pfrequency_hz, short *pnQ100, short *pn_gain0_01dB)
{
	struct hpi_message hm;
	struct hpi_response hr;

	hpi_init_message_response(&hm, &hr, HPI_OBJ_CONTROL,
		HPI_CONTROL_GET_STATE);
	if (hpi_handle_indexes(h_control, &hm.adapter_index, &hm.obj_index))
		return HPI_ERROR_INVALID_HANDLE;
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

u16 hpi_parametric_eq_set_band(u32 h_control, u16 index, u16 type,
	u32 frequency_hz, short q100, short gain0_01dB)
{
	struct hpi_message hm;
	struct hpi_response hr;

	hpi_init_message_response(&hm, &hr, HPI_OBJ_CONTROL,
		HPI_CONTROL_SET_STATE);
	if (hpi_handle_indexes(h_control, &hm.adapter_index, &hm.obj_index))
		return HPI_ERROR_INVALID_HANDLE;

	hm.u.c.param1 = frequency_hz;
	hm.u.c.param2 = (index & 0xFFFFL) + ((u32)type << 16);
	hm.u.c.an_log_value[0] = gain0_01dB;
	hm.u.c.an_log_value[1] = q100;
	hm.u.c.attribute = HPI_EQUALIZER_FILTER;

	hpi_send_recv(&hm, &hr);

	return hr.error;
}

u16 hpi_parametric_eq_get_coeffs(u32 h_control, u16 index, short coeffs[5]
	)
{
	struct hpi_message hm;
	struct hpi_response hr;

	hpi_init_message_response(&hm, &hr, HPI_OBJ_CONTROL,
		HPI_CONTROL_GET_STATE);
	if (hpi_handle_indexes(h_control, &hm.adapter_index, &hm.obj_index))
		return HPI_ERROR_INVALID_HANDLE;
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

u16 hpi_sample_clock_query_source(const u32 h_clock, const u32 index,
	u16 *pw_source)
{
	u32 qr;
	u16 err;

	err = hpi_control_query(h_clock, HPI_SAMPLECLOCK_SOURCE, index, 0,
		&qr);
	*pw_source = (u16)qr;
	return err;
}

u16 hpi_sample_clock_set_source(u32 h_control, u16 source)
{
	return hpi_control_param_set(h_control, HPI_SAMPLECLOCK_SOURCE,
		source, 0);
}

u16 hpi_sample_clock_get_source(u32 h_control, u16 *pw_source)
{
	u16 err = 0;
	u32 source = 0;
	err = hpi_control_param1_get(h_control, HPI_SAMPLECLOCK_SOURCE,
		&source);
	if (!err)
		if (pw_source)
			*pw_source = (u16)source;
	return err;
}

u16 hpi_sample_clock_query_source_index(const u32 h_clock, const u32 index,
	const u32 source, u16 *pw_source_index)
{
	u32 qr;
	u16 err;

	err = hpi_control_query(h_clock, HPI_SAMPLECLOCK_SOURCE_INDEX, index,
		source, &qr);
	*pw_source_index = (u16)qr;
	return err;
}

u16 hpi_sample_clock_set_source_index(u32 h_control, u16 source_index)
{
	return hpi_control_param_set(h_control, HPI_SAMPLECLOCK_SOURCE_INDEX,
		source_index, 0);
}

u16 hpi_sample_clock_get_source_index(u32 h_control, u16 *pw_source_index)
{
	u16 err = 0;
	u32 source_index = 0;
	err = hpi_control_param1_get(h_control, HPI_SAMPLECLOCK_SOURCE_INDEX,
		&source_index);
	if (!err)
		if (pw_source_index)
			*pw_source_index = (u16)source_index;
	return err;
}

u16 hpi_sample_clock_query_local_rate(const u32 h_clock, const u32 index,
	u32 *prate)
{
	u16 err;
	err = hpi_control_query(h_clock, HPI_SAMPLECLOCK_LOCAL_SAMPLERATE,
		index, 0, prate);

	return err;
}

u16 hpi_sample_clock_set_local_rate(u32 h_control, u32 sample_rate)
{
	return hpi_control_param_set(h_control,
		HPI_SAMPLECLOCK_LOCAL_SAMPLERATE, sample_rate, 0);
}

u16 hpi_sample_clock_get_local_rate(u32 h_control, u32 *psample_rate)
{
	u16 err = 0;
	u32 sample_rate = 0;
	err = hpi_control_param1_get(h_control,
		HPI_SAMPLECLOCK_LOCAL_SAMPLERATE, &sample_rate);
	if (!err)
		if (psample_rate)
			*psample_rate = sample_rate;
	return err;
}

u16 hpi_sample_clock_get_sample_rate(u32 h_control, u32 *psample_rate)
{
	u16 err = 0;
	u32 sample_rate = 0;
	err = hpi_control_param1_get(h_control, HPI_SAMPLECLOCK_SAMPLERATE,
		&sample_rate);
	if (!err)
		if (psample_rate)
			*psample_rate = sample_rate;
	return err;
}

u16 hpi_sample_clock_set_auto(u32 h_control, u32 enable)
{
	return hpi_control_param_set(h_control, HPI_SAMPLECLOCK_AUTO, enable,
		0);
}

u16 hpi_sample_clock_get_auto(u32 h_control, u32 *penable)
{
	return hpi_control_param1_get(h_control, HPI_SAMPLECLOCK_AUTO,
		penable);
}

u16 hpi_sample_clock_set_local_rate_lock(u32 h_control, u32 lock)
{
	return hpi_control_param_set(h_control, HPI_SAMPLECLOCK_LOCAL_LOCK,
		lock, 0);
}

u16 hpi_sample_clock_get_local_rate_lock(u32 h_control, u32 *plock)
{
	return hpi_control_param1_get(h_control, HPI_SAMPLECLOCK_LOCAL_LOCK,
		plock);
}

u16 hpi_tone_detector_get_frequency(u32 h_control, u32 index, u32 *frequency)
{
	return hpi_control_param_get(h_control, HPI_TONEDETECTOR_FREQUENCY,
		index, 0, frequency, NULL);
}

u16 hpi_tone_detector_get_state(u32 h_control, u32 *state)
{
	return hpi_control_param1_get(h_control, HPI_TONEDETECTOR_STATE,
		state);
}

u16 hpi_tone_detector_set_enable(u32 h_control, u32 enable)
{
	return hpi_control_param_set(h_control, HPI_GENERIC_ENABLE, enable,
		0);
}

u16 hpi_tone_detector_get_enable(u32 h_control, u32 *enable)
{
	return hpi_control_param1_get(h_control, HPI_GENERIC_ENABLE, enable);
}

u16 hpi_tone_detector_set_event_enable(u32 h_control, u32 event_enable)
{
	return hpi_control_param_set(h_control, HPI_GENERIC_EVENT_ENABLE,
		(u32)event_enable, 0);
}

u16 hpi_tone_detector_get_event_enable(u32 h_control, u32 *event_enable)
{
	return hpi_control_param1_get(h_control, HPI_GENERIC_EVENT_ENABLE,
		event_enable);
}

u16 hpi_tone_detector_set_threshold(u32 h_control, int threshold)
{
	return hpi_control_param_set(h_control, HPI_TONEDETECTOR_THRESHOLD,
		(u32)threshold, 0);
}

u16 hpi_tone_detector_get_threshold(u32 h_control, int *threshold)
{
	return hpi_control_param1_get(h_control, HPI_TONEDETECTOR_THRESHOLD,
		(u32 *)threshold);
}

u16 hpi_silence_detector_get_state(u32 h_control, u32 *state)
{
	return hpi_control_param1_get(h_control, HPI_SILENCEDETECTOR_STATE,
		state);
}

u16 hpi_silence_detector_set_enable(u32 h_control, u32 enable)
{
	return hpi_control_param_set(h_control, HPI_GENERIC_ENABLE, enable,
		0);
}

u16 hpi_silence_detector_get_enable(u32 h_control, u32 *enable)
{
	return hpi_control_param1_get(h_control, HPI_GENERIC_ENABLE, enable);
}

u16 hpi_silence_detector_set_event_enable(u32 h_control, u32 event_enable)
{
	return hpi_control_param_set(h_control, HPI_GENERIC_EVENT_ENABLE,
		event_enable, 0);
}

u16 hpi_silence_detector_get_event_enable(u32 h_control, u32 *event_enable)
{
	return hpi_control_param1_get(h_control, HPI_GENERIC_EVENT_ENABLE,
		event_enable);
}

u16 hpi_silence_detector_set_delay(u32 h_control, u32 delay)
{
	return hpi_control_param_set(h_control, HPI_SILENCEDETECTOR_DELAY,
		delay, 0);
}

u16 hpi_silence_detector_get_delay(u32 h_control, u32 *delay)
{
	return hpi_control_param1_get(h_control, HPI_SILENCEDETECTOR_DELAY,
		delay);
}

u16 hpi_silence_detector_set_threshold(u32 h_control, int threshold)
{
	return hpi_control_param_set(h_control, HPI_SILENCEDETECTOR_THRESHOLD,
		threshold, 0);
}

u16 hpi_silence_detector_get_threshold(u32 h_control, int *threshold)
{
	return hpi_control_param1_get(h_control,
		HPI_SILENCEDETECTOR_THRESHOLD, (u32 *)threshold);
}

u16 hpi_tuner_query_band(const u32 h_tuner, const u32 index, u16 *pw_band)
{
	u32 qr;
	u16 err;

	err = hpi_control_query(h_tuner, HPI_TUNER_BAND, index, 0, &qr);
	*pw_band = (u16)qr;
	return err;
}

u16 hpi_tuner_set_band(u32 h_control, u16 band)
{
	return hpi_control_param_set(h_control, HPI_TUNER_BAND, band, 0);
}

u16 hpi_tuner_get_band(u32 h_control, u16 *pw_band)
{
	u32 band = 0;
	u16 error = 0;

	error = hpi_control_param1_get(h_control, HPI_TUNER_BAND, &band);
	if (pw_band)
		*pw_band = (u16)band;
	return error;
}

u16 hpi_tuner_query_frequency(const u32 h_tuner, const u32 index,
	const u16 band, u32 *pfreq)
{
	return hpi_control_query(h_tuner, HPI_TUNER_FREQ, index, band, pfreq);
}

u16 hpi_tuner_set_frequency(u32 h_control, u32 freq_ink_hz)
{
	return hpi_control_param_set(h_control, HPI_TUNER_FREQ, freq_ink_hz,
		0);
}

u16 hpi_tuner_get_frequency(u32 h_control, u32 *pw_freq_ink_hz)
{
	return hpi_control_param1_get(h_control, HPI_TUNER_FREQ,
		pw_freq_ink_hz);
}

u16 hpi_tuner_query_gain(const u32 h_tuner, const u32 index, u16 *pw_gain)
{
	u32 qr;
	u16 err;

	err = hpi_control_query(h_tuner, HPI_TUNER_BAND, index, 0, &qr);
	*pw_gain = (u16)qr;
	return err;
}

u16 hpi_tuner_set_gain(u32 h_control, short gain)
{
	return hpi_control_param_set(h_control, HPI_TUNER_GAIN, gain, 0);
}

u16 hpi_tuner_get_gain(u32 h_control, short *pn_gain)
{
	u32 gain = 0;
	u16 error = 0;

	error = hpi_control_param1_get(h_control, HPI_TUNER_GAIN, &gain);
	if (pn_gain)
		*pn_gain = (u16)gain;
	return error;
}

u16 hpi_tuner_get_rf_level(u32 h_control, short *pw_level)
{
	struct hpi_message hm;
	struct hpi_response hr;

	hpi_init_message_response(&hm, &hr, HPI_OBJ_CONTROL,
		HPI_CONTROL_GET_STATE);
	if (hpi_handle_indexes(h_control, &hm.adapter_index, &hm.obj_index))
		return HPI_ERROR_INVALID_HANDLE;
	hm.u.cu.attribute = HPI_TUNER_LEVEL_AVG;
	hpi_send_recv(&hm, &hr);
	if (pw_level)
		*pw_level = hr.u.cu.tuner.s_level;
	return hr.error;
}

u16 hpi_tuner_get_raw_rf_level(u32 h_control, short *pw_level)
{
	struct hpi_message hm;
	struct hpi_response hr;

	hpi_init_message_response(&hm, &hr, HPI_OBJ_CONTROL,
		HPI_CONTROL_GET_STATE);
	if (hpi_handle_indexes(h_control, &hm.adapter_index, &hm.obj_index))
		return HPI_ERROR_INVALID_HANDLE;
	hm.u.cu.attribute = HPI_TUNER_LEVEL_RAW;
	hpi_send_recv(&hm, &hr);
	if (pw_level)
		*pw_level = hr.u.cu.tuner.s_level;
	return hr.error;
}

u16 hpi_tuner_query_deemphasis(const u32 h_tuner, const u32 index,
	const u16 band, u32 *pdeemphasis)
{
	return hpi_control_query(h_tuner, HPI_TUNER_DEEMPHASIS, index, band,
		pdeemphasis);
}

u16 hpi_tuner_set_deemphasis(u32 h_control, u32 deemphasis)
{
	return hpi_control_param_set(h_control, HPI_TUNER_DEEMPHASIS,
		deemphasis, 0);
}

u16 hpi_tuner_get_deemphasis(u32 h_control, u32 *pdeemphasis)
{
	return hpi_control_param1_get(h_control, HPI_TUNER_DEEMPHASIS,
		pdeemphasis);
}

u16 hpi_tuner_query_program(const u32 h_tuner, u32 *pbitmap_program)
{
	return hpi_control_query(h_tuner, HPI_TUNER_PROGRAM, 0, 0,
		pbitmap_program);
}

u16 hpi_tuner_set_program(u32 h_control, u32 program)
{
	return hpi_control_param_set(h_control, HPI_TUNER_PROGRAM, program,
		0);
}

u16 hpi_tuner_get_program(u32 h_control, u32 *pprogram)
{
	return hpi_control_param1_get(h_control, HPI_TUNER_PROGRAM, pprogram);
}

u16 hpi_tuner_get_hd_radio_dsp_version(u32 h_control, char *psz_dsp_version,
	const u32 string_size)
{
	return hpi_control_get_string(h_control,
		HPI_TUNER_HDRADIO_DSP_VERSION, psz_dsp_version, string_size);
}

u16 hpi_tuner_get_hd_radio_sdk_version(u32 h_control, char *psz_sdk_version,
	const u32 string_size)
{
	return hpi_control_get_string(h_control,
		HPI_TUNER_HDRADIO_SDK_VERSION, psz_sdk_version, string_size);
}

u16 hpi_tuner_get_status(u32 h_control, u16 *pw_status_mask, u16 *pw_status)
{
	u32 status = 0;
	u16 error = 0;

	error = hpi_control_param1_get(h_control, HPI_TUNER_STATUS, &status);
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

u16 hpi_tuner_set_mode(u32 h_control, u32 mode, u32 value)
{
	return hpi_control_param_set(h_control, HPI_TUNER_MODE, mode, value);
}

u16 hpi_tuner_get_mode(u32 h_control, u32 mode, u32 *pn_value)
{
	return hpi_control_param_get(h_control, HPI_TUNER_MODE, mode, 0,
		pn_value, NULL);
}

u16 hpi_tuner_get_hd_radio_signal_quality(u32 h_control, u32 *pquality)
{
	return hpi_control_param1_get(h_control,
		HPI_TUNER_HDRADIO_SIGNAL_QUALITY, pquality);
}

u16 hpi_tuner_get_hd_radio_signal_blend(u32 h_control, u32 *pblend)
{
	return hpi_control_param1_get(h_control, HPI_TUNER_HDRADIO_BLEND,
		pblend);
}

u16 hpi_tuner_set_hd_radio_signal_blend(u32 h_control, const u32 blend)
{
	return hpi_control_param_set(h_control, HPI_TUNER_HDRADIO_BLEND,
		blend, 0);
}

u16 hpi_tuner_get_rds(u32 h_control, char *p_data)
{
	struct hpi_message hm;
	struct hpi_response hr;

	hpi_init_message_response(&hm, &hr, HPI_OBJ_CONTROL,
		HPI_CONTROL_GET_STATE);
	if (hpi_handle_indexes(h_control, &hm.adapter_index, &hm.obj_index))
		return HPI_ERROR_INVALID_HANDLE;
	hm.u.c.attribute = HPI_TUNER_RDS;
	hpi_send_recv(&hm, &hr);
	if (p_data) {
		*(u32 *)&p_data[0] = hr.u.cu.tuner.rds.data[0];
		*(u32 *)&p_data[4] = hr.u.cu.tuner.rds.data[1];
		*(u32 *)&p_data[8] = hr.u.cu.tuner.rds.bLER;
	}
	return hr.error;
}

u16 hpi_pad_get_channel_name(u32 h_control, char *psz_string,
	const u32 data_length)
{
	return hpi_control_get_string(h_control, HPI_PAD_CHANNEL_NAME,
		psz_string, data_length);
}

u16 hpi_pad_get_artist(u32 h_control, char *psz_string, const u32 data_length)
{
	return hpi_control_get_string(h_control, HPI_PAD_ARTIST, psz_string,
		data_length);
}

u16 hpi_pad_get_title(u32 h_control, char *psz_string, const u32 data_length)
{
	return hpi_control_get_string(h_control, HPI_PAD_TITLE, psz_string,
		data_length);
}

u16 hpi_pad_get_comment(u32 h_control, char *psz_string,
	const u32 data_length)
{
	return hpi_control_get_string(h_control, HPI_PAD_COMMENT, psz_string,
		data_length);
}

u16 hpi_pad_get_program_type(u32 h_control, u32 *ppTY)
{
	return hpi_control_param1_get(h_control, HPI_PAD_PROGRAM_TYPE, ppTY);
}

u16 hpi_pad_get_rdsPI(u32 h_control, u32 *ppI)
{
	return hpi_control_param1_get(h_control, HPI_PAD_PROGRAM_ID, ppI);
}

u16 hpi_volume_query_channels(const u32 h_volume, u32 *p_channels)
{
	return hpi_control_query(h_volume, HPI_VOLUME_NUM_CHANNELS, 0, 0,
		p_channels);
}

u16 hpi_volume_set_gain(u32 h_control, short an_log_gain[HPI_MAX_CHANNELS]
	)
{
	return hpi_control_log_set2(h_control, HPI_VOLUME_GAIN,
		an_log_gain[0], an_log_gain[1]);
}

u16 hpi_volume_get_gain(u32 h_control, short an_log_gain[HPI_MAX_CHANNELS]
	)
{
	return hpi_control_log_get2(h_control, HPI_VOLUME_GAIN,
		&an_log_gain[0], &an_log_gain[1]);
}

u16 hpi_volume_set_mute(u32 h_control, u32 mute)
{
	return hpi_control_param_set(h_control, HPI_VOLUME_MUTE, mute, 0);
}

u16 hpi_volume_get_mute(u32 h_control, u32 *mute)
{
	return hpi_control_param1_get(h_control, HPI_VOLUME_MUTE, mute);
}

u16 hpi_volume_query_range(u32 h_control, short *min_gain_01dB,
	short *max_gain_01dB, short *step_gain_01dB)
{
	struct hpi_message hm;
	struct hpi_response hr;

	hpi_init_message_response(&hm, &hr, HPI_OBJ_CONTROL,
		HPI_CONTROL_GET_STATE);
	if (hpi_handle_indexes(h_control, &hm.adapter_index, &hm.obj_index))
		return HPI_ERROR_INVALID_HANDLE;
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

u16 hpi_volume_auto_fade_profile(u32 h_control,
	short an_stop_gain0_01dB[HPI_MAX_CHANNELS], u32 duration_ms,
	u16 profile)
{
	struct hpi_message hm;
	struct hpi_response hr;

	hpi_init_message_response(&hm, &hr, HPI_OBJ_CONTROL,
		HPI_CONTROL_SET_STATE);
	if (hpi_handle_indexes(h_control, &hm.adapter_index, &hm.obj_index))
		return HPI_ERROR_INVALID_HANDLE;

	memcpy(hm.u.c.an_log_value, an_stop_gain0_01dB,
		sizeof(short) * HPI_MAX_CHANNELS);

	hm.u.c.attribute = HPI_VOLUME_AUTOFADE;
	hm.u.c.param1 = duration_ms;
	hm.u.c.param2 = profile;

	hpi_send_recv(&hm, &hr);

	return hr.error;
}

u16 hpi_volume_auto_fade(u32 h_control,
	short an_stop_gain0_01dB[HPI_MAX_CHANNELS], u32 duration_ms)
{
	return hpi_volume_auto_fade_profile(h_control, an_stop_gain0_01dB,
		duration_ms, HPI_VOLUME_AUTOFADE_LOG);
}

u16 hpi_vox_set_threshold(u32 h_control, short an_gain0_01dB)
{
	struct hpi_message hm;
	struct hpi_response hr;
	hpi_init_message_response(&hm, &hr, HPI_OBJ_CONTROL,
		HPI_CONTROL_SET_STATE);
	if (hpi_handle_indexes(h_control, &hm.adapter_index, &hm.obj_index))
		return HPI_ERROR_INVALID_HANDLE;
	hm.u.c.attribute = HPI_VOX_THRESHOLD;

	hm.u.c.an_log_value[0] = an_gain0_01dB;

	hpi_send_recv(&hm, &hr);

	return hr.error;
}

u16 hpi_vox_get_threshold(u32 h_control, short *an_gain0_01dB)
{
	struct hpi_message hm;
	struct hpi_response hr;
	hpi_init_message_response(&hm, &hr, HPI_OBJ_CONTROL,
		HPI_CONTROL_GET_STATE);
	if (hpi_handle_indexes(h_control, &hm.adapter_index, &hm.obj_index))
		return HPI_ERROR_INVALID_HANDLE;
	hm.u.c.attribute = HPI_VOX_THRESHOLD;

	hpi_send_recv(&hm, &hr);

	*an_gain0_01dB = hr.u.c.an_log_value[0];

	return hr.error;
}
