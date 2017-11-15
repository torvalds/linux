#ifndef SOUND_FIREWIRE_LIB_H_INCLUDED
#define SOUND_FIREWIRE_LIB_H_INCLUDED

#include <linux/firewire-constants.h>
#include <linux/types.h>
#include <linux/sched.h>
#include <sound/rawmidi.h>

struct fw_unit;

#define FW_GENERATION_MASK	0x00ff
#define FW_FIXED_GENERATION	0x0100
#define FW_QUIET		0x0200

int snd_fw_transaction(struct fw_unit *unit, int tcode,
		       u64 offset, void *buffer, size_t length,
		       unsigned int flags);

/* returns true if retrying the transaction would not make sense */
static inline bool rcode_is_permanent_error(int rcode)
{
	return rcode == RCODE_TYPE_ERROR || rcode == RCODE_ADDRESS_ERROR;
}

struct snd_fw_async_midi_port;
typedef int (*snd_fw_async_midi_port_fill)(
				struct snd_rawmidi_substream *substream,
				u8 *buf);

struct snd_fw_async_midi_port {
	struct fw_device *parent;
	struct work_struct work;
	bool idling;
	ktime_t next_ktime;
	bool error;

	u64 addr;
	struct fw_transaction transaction;

	u8 *buf;
	unsigned int len;

	struct snd_rawmidi_substream *substream;
	snd_fw_async_midi_port_fill fill;
	int consume_bytes;
};

int snd_fw_async_midi_port_init(struct snd_fw_async_midi_port *port,
		struct fw_unit *unit, u64 addr, unsigned int len,
		snd_fw_async_midi_port_fill fill);
void snd_fw_async_midi_port_destroy(struct snd_fw_async_midi_port *port);

/**
 * snd_fw_async_midi_port_run - run transactions for the async MIDI port
 * @port: the asynchronous MIDI port
 * @substream: the MIDI substream
 */
static inline void
snd_fw_async_midi_port_run(struct snd_fw_async_midi_port *port,
			   struct snd_rawmidi_substream *substream)
{
	if (!port->error) {
		port->substream = substream;
		schedule_work(&port->work);
	}
}

/**
 * snd_fw_async_midi_port_finish - finish the asynchronous MIDI port
 * @port: the asynchronous MIDI port
 */
static inline void
snd_fw_async_midi_port_finish(struct snd_fw_async_midi_port *port)
{
	port->substream = NULL;
	port->error = false;
}

#endif
