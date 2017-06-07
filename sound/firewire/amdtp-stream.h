#ifndef SOUND_FIREWIRE_AMDTP_H_INCLUDED
#define SOUND_FIREWIRE_AMDTP_H_INCLUDED

#include <linux/err.h>
#include <linux/interrupt.h>
#include <linux/mutex.h>
#include <linux/sched.h>
#include <sound/asound.h>
#include "packets-buffer.h"

/**
 * enum cip_flags - describes details of the streaming protocol
 * @CIP_NONBLOCKING: In non-blocking mode, each packet contains
 *	sample_rate/8000 samples, with rounding up or down to adjust
 *	for clock skew and left-over fractional samples.  This should
 *	be used if supported by the device.
 * @CIP_BLOCKING: In blocking mode, each packet contains either zero or
 *	SYT_INTERVAL samples, with these two types alternating so that
 *	the overall sample rate comes out right.
 * @CIP_EMPTY_WITH_TAG0: Only for in-stream. Empty in-packets have TAG0.
 * @CIP_DBC_IS_END_EVENT: The value of dbc in an packet corresponds to the end
 * of event in the packet. Out of IEC 61883.
 * @CIP_WRONG_DBS: Only for in-stream. The value of dbs is wrong in in-packets.
 *	The value of data_block_quadlets is used instead of reported value.
 * @CIP_SKIP_DBC_ZERO_CHECK: Only for in-stream.  Packets with zero in dbc is
 *	skipped for detecting discontinuity.
 * @CIP_EMPTY_HAS_WRONG_DBC: Only for in-stream. The value of dbc in empty
 *	packet is wrong but the others are correct.
 * @CIP_JUMBO_PAYLOAD: Only for in-stream. The number of data blocks in an
 *	packet is larger than IEC 61883-6 defines. Current implementation
 *	allows 5 times as large as IEC 61883-6 defines.
 * @CIP_HEADER_WITHOUT_EOH: Only for in-stream. CIP Header doesn't include
 *	valid EOH.
 * @CIP_NO_HEADERS: a lack of headers in packets
 */
enum cip_flags {
	CIP_NONBLOCKING		= 0x00,
	CIP_BLOCKING		= 0x01,
	CIP_EMPTY_WITH_TAG0	= 0x02,
	CIP_DBC_IS_END_EVENT	= 0x04,
	CIP_WRONG_DBS		= 0x08,
	CIP_SKIP_DBC_ZERO_CHECK	= 0x10,
	CIP_EMPTY_HAS_WRONG_DBC	= 0x20,
	CIP_JUMBO_PAYLOAD	= 0x40,
	CIP_HEADER_WITHOUT_EOH	= 0x80,
	CIP_NO_HEADER		= 0x100,
};

/**
 * enum cip_sfc - supported Sampling Frequency Codes (SFCs)
 * @CIP_SFC_32000:   32,000 data blocks
 * @CIP_SFC_44100:   44,100 data blocks
 * @CIP_SFC_48000:   48,000 data blocks
 * @CIP_SFC_88200:   88,200 data blocks
 * @CIP_SFC_96000:   96,000 data blocks
 * @CIP_SFC_176400: 176,400 data blocks
 * @CIP_SFC_192000: 192,000 data blocks
 * @CIP_SFC_COUNT: the number of supported SFCs
 *
 * These values are used to show nominal Sampling Frequency Code in
 * Format Dependent Field (FDF) of AMDTP packet header. In IEC 61883-6:2002,
 * this code means the number of events per second. Actually the code
 * represents the number of data blocks transferred per second in an AMDTP
 * stream.
 *
 * In IEC 61883-6:2005, some extensions were added to support more types of
 * data such as 'One Bit LInear Audio', therefore the meaning of SFC became
 * different depending on the types.
 *
 * Currently our implementation is compatible with IEC 61883-6:2002.
 */
enum cip_sfc {
	CIP_SFC_32000  = 0,
	CIP_SFC_44100  = 1,
	CIP_SFC_48000  = 2,
	CIP_SFC_88200  = 3,
	CIP_SFC_96000  = 4,
	CIP_SFC_176400 = 5,
	CIP_SFC_192000 = 6,
	CIP_SFC_COUNT
};

struct fw_unit;
struct fw_iso_context;
struct snd_pcm_substream;
struct snd_pcm_runtime;

enum amdtp_stream_direction {
	AMDTP_OUT_STREAM = 0,
	AMDTP_IN_STREAM
};

struct amdtp_stream;
typedef unsigned int (*amdtp_stream_process_data_blocks_t)(
						struct amdtp_stream *s,
						__be32 *buffer,
						unsigned int data_blocks,
						unsigned int *syt);
struct amdtp_stream {
	struct fw_unit *unit;
	enum cip_flags flags;
	enum amdtp_stream_direction direction;
	struct mutex mutex;

	/* For packet processing. */
	struct fw_iso_context *context;
	struct iso_packets_buffer buffer;
	int packet_index;
	int tag;
	int (*handle_packet)(struct amdtp_stream *s,
			unsigned int payload_quadlets, unsigned int cycle,
			unsigned int index);
	unsigned int max_payload_length;

	/* For CIP headers. */
	unsigned int source_node_id_field;
	unsigned int data_block_quadlets;
	unsigned int data_block_counter;
	unsigned int sph;
	unsigned int fmt;
	unsigned int fdf;
	/* quirk: fixed interval of dbc between previos/current packets. */
	unsigned int tx_dbc_interval;
	/* quirk: indicate the value of dbc field in a first packet. */
	unsigned int tx_first_dbc;

	/* Internal flags. */
	enum cip_sfc sfc;
	unsigned int syt_interval;
	unsigned int transfer_delay;
	unsigned int data_block_state;
	unsigned int last_syt_offset;
	unsigned int syt_offset_state;

	/* For a PCM substream processing. */
	struct snd_pcm_substream *pcm;
	struct tasklet_struct period_tasklet;
	unsigned int pcm_buffer_pointer;
	unsigned int pcm_period_pointer;

	/* To wait for first packet. */
	bool callbacked;
	wait_queue_head_t callback_wait;
	u32 start_cycle;

	/* For backends to process data blocks. */
	void *protocol;
	amdtp_stream_process_data_blocks_t process_data_blocks;
};

int amdtp_stream_init(struct amdtp_stream *s, struct fw_unit *unit,
		      enum amdtp_stream_direction dir, enum cip_flags flags,
		      unsigned int fmt,
		      amdtp_stream_process_data_blocks_t process_data_blocks,
		      unsigned int protocol_size);
void amdtp_stream_destroy(struct amdtp_stream *s);

int amdtp_stream_set_parameters(struct amdtp_stream *s, unsigned int rate,
				unsigned int data_block_quadlets);
unsigned int amdtp_stream_get_max_payload(struct amdtp_stream *s);

int amdtp_stream_start(struct amdtp_stream *s, int channel, int speed);
void amdtp_stream_update(struct amdtp_stream *s);
void amdtp_stream_stop(struct amdtp_stream *s);

int amdtp_stream_add_pcm_hw_constraints(struct amdtp_stream *s,
					struct snd_pcm_runtime *runtime);

void amdtp_stream_pcm_prepare(struct amdtp_stream *s);
unsigned long amdtp_stream_pcm_pointer(struct amdtp_stream *s);
int amdtp_stream_pcm_ack(struct amdtp_stream *s);
void amdtp_stream_pcm_abort(struct amdtp_stream *s);

extern const unsigned int amdtp_syt_intervals[CIP_SFC_COUNT];
extern const unsigned int amdtp_rate_table[CIP_SFC_COUNT];

/**
 * amdtp_stream_running - check stream is running or not
 * @s: the AMDTP stream
 *
 * If this function returns true, the stream is running.
 */
static inline bool amdtp_stream_running(struct amdtp_stream *s)
{
	return !IS_ERR(s->context);
}

/**
 * amdtp_streaming_error - check for streaming error
 * @s: the AMDTP stream
 *
 * If this function returns true, the stream's packet queue has stopped due to
 * an asynchronous error.
 */
static inline bool amdtp_streaming_error(struct amdtp_stream *s)
{
	return s->packet_index < 0;
}

/**
 * amdtp_stream_pcm_running - check PCM substream is running or not
 * @s: the AMDTP stream
 *
 * If this function returns true, PCM substream in the AMDTP stream is running.
 */
static inline bool amdtp_stream_pcm_running(struct amdtp_stream *s)
{
	return !!s->pcm;
}

/**
 * amdtp_stream_pcm_trigger - start/stop playback from a PCM device
 * @s: the AMDTP stream
 * @pcm: the PCM device to be started, or %NULL to stop the current device
 *
 * Call this function on a running isochronous stream to enable the actual
 * transmission of PCM data.  This function should be called from the PCM
 * device's .trigger callback.
 */
static inline void amdtp_stream_pcm_trigger(struct amdtp_stream *s,
					    struct snd_pcm_substream *pcm)
{
	ACCESS_ONCE(s->pcm) = pcm;
}

static inline bool cip_sfc_is_base_44100(enum cip_sfc sfc)
{
	return sfc & 1;
}

/**
 * amdtp_stream_wait_callback - sleep till callbacked or timeout
 * @s: the AMDTP stream
 * @timeout: msec till timeout
 *
 * If this function return false, the AMDTP stream should be stopped.
 */
static inline bool amdtp_stream_wait_callback(struct amdtp_stream *s,
					      unsigned int timeout)
{
	return wait_event_timeout(s->callback_wait,
				  s->callbacked == true,
				  msecs_to_jiffies(timeout)) > 0;
}

#endif
