/*
 * ff-transaction.c - a part of driver for RME Fireface series
 *
 * Copyright (c) 2015-2017 Takashi Sakamoto
 *
 * Licensed under the terms of the GNU General Public License, version 2.
 */

#include "ff.h"

#define SND_FF_REG_MIDI_RX_PORT_0	0x000080180000ull
#define SND_FF_REG_MIDI_RX_PORT_1	0x000080190000ull

int snd_ff_transaction_get_clock(struct snd_ff *ff, unsigned int *rate,
				 enum snd_ff_clock_src *src)
{
	__le32 reg;
	u32 data;
	int err;

	err = snd_fw_transaction(ff->unit, TCODE_READ_QUADLET_REQUEST,
				 SND_FF_REG_CLOCK_CONFIG, &reg, sizeof(reg), 0);
	if (err < 0)
		return err;
	data = le32_to_cpu(reg);

	/* Calculate sampling rate. */
	switch ((data >> 1) & 0x03) {
	case 0x01:
		*rate = 32000;
		break;
	case 0x00:
		*rate = 44100;
		break;
	case 0x03:
		*rate = 48000;
		break;
	case 0x02:
	default:
		return -EIO;
	}

	if (data & 0x08)
		*rate *= 2;
	else if (data & 0x10)
		*rate *= 4;

	/* Calculate source of clock. */
	if (data & 0x01) {
		*src = SND_FF_CLOCK_SRC_INTERNAL;
	} else {
		/* TODO: 0x02, 0x06, 0x07? */
		switch ((data >> 10) & 0x07) {
		case 0x00:
			*src = SND_FF_CLOCK_SRC_ADAT1;
			break;
		case 0x01:
			*src = SND_FF_CLOCK_SRC_ADAT2;
			break;
		case 0x03:
			*src = SND_FF_CLOCK_SRC_SPDIF;
			break;
		case 0x04:
			*src = SND_FF_CLOCK_SRC_WORD;
			break;
		case 0x05:
			*src = SND_FF_CLOCK_SRC_LTC;
			break;
		default:
			return -EIO;
		}
	}

	return 0;
}

static void finish_transmit_midi_msg(struct snd_ff *ff, unsigned int port,
				     int rcode)
{
	struct snd_rawmidi_substream *substream =
				READ_ONCE(ff->rx_midi_substreams[port]);

	if (rcode_is_permanent_error(rcode)) {
		ff->rx_midi_error[port] = true;
		return;
	}

	if (rcode != RCODE_COMPLETE) {
		/* Transfer the message again, immediately. */
		ff->next_ktime[port] = 0;
		schedule_work(&ff->rx_midi_work[port]);
		return;
	}

	snd_rawmidi_transmit_ack(substream, ff->rx_bytes[port]);
	ff->rx_bytes[port] = 0;

	if (!snd_rawmidi_transmit_empty(substream))
		schedule_work(&ff->rx_midi_work[port]);
}

static void finish_transmit_midi0_msg(struct fw_card *card, int rcode,
				      void *data, size_t length,
				      void *callback_data)
{
	struct snd_ff *ff =
		container_of(callback_data, struct snd_ff, transactions[0]);
	finish_transmit_midi_msg(ff, 0, rcode);
}

static void finish_transmit_midi1_msg(struct fw_card *card, int rcode,
				      void *data, size_t length,
				      void *callback_data)
{
	struct snd_ff *ff =
		container_of(callback_data, struct snd_ff, transactions[1]);
	finish_transmit_midi_msg(ff, 1, rcode);
}

static inline void fill_midi_buf(struct snd_ff *ff, unsigned int port,
				 unsigned int index, u8 byte)
{
	ff->msg_buf[port][index] = cpu_to_le32(byte);
}

static void transmit_midi_msg(struct snd_ff *ff, unsigned int port)
{
	struct snd_rawmidi_substream *substream =
			READ_ONCE(ff->rx_midi_substreams[port]);
	u8 *buf = (u8 *)ff->msg_buf[port];
	int i, len;

	struct fw_device *fw_dev = fw_parent_device(ff->unit);
	unsigned long long addr;
	int generation;
	fw_transaction_callback_t callback;

	if (substream == NULL || snd_rawmidi_transmit_empty(substream))
		return;

	if (ff->rx_bytes[port] > 0 || ff->rx_midi_error[port])
		return;

	/* Do it in next chance. */
	if (ktime_after(ff->next_ktime[port], ktime_get())) {
		schedule_work(&ff->rx_midi_work[port]);
		return;
	}

	len = snd_rawmidi_transmit_peek(substream, buf,
					SND_FF_MAXIMIM_MIDI_QUADS);
	if (len <= 0)
		return;

	for (i = len - 1; i >= 0; i--)
		fill_midi_buf(ff, port, i, buf[i]);

	if (port == 0) {
		addr = SND_FF_REG_MIDI_RX_PORT_0;
		callback = finish_transmit_midi0_msg;
	} else {
		addr = SND_FF_REG_MIDI_RX_PORT_1;
		callback = finish_transmit_midi1_msg;
	}

	/* Set interval to next transaction. */
	ff->next_ktime[port] = ktime_add_ns(ktime_get(),
					    len * 8 * NSEC_PER_SEC / 31250);
	ff->rx_bytes[port] = len;

	/*
	 * In Linux FireWire core, when generation is updated with memory
	 * barrier, node id has already been updated. In this module, After
	 * this smp_rmb(), load/store instructions to memory are completed.
	 * Thus, both of generation and node id are available with recent
	 * values. This is a light-serialization solution to handle bus reset
	 * events on IEEE 1394 bus.
	 */
	generation = fw_dev->generation;
	smp_rmb();
	fw_send_request(fw_dev->card, &ff->transactions[port],
			TCODE_WRITE_BLOCK_REQUEST,
			fw_dev->node_id, generation, fw_dev->max_speed,
			addr, &ff->msg_buf[port], len * 4,
			callback, &ff->transactions[port]);
}

static void transmit_midi0_msg(struct work_struct *work)
{
	struct snd_ff *ff = container_of(work, struct snd_ff, rx_midi_work[0]);

	transmit_midi_msg(ff, 0);
}

static void transmit_midi1_msg(struct work_struct *work)
{
	struct snd_ff *ff = container_of(work, struct snd_ff, rx_midi_work[1]);

	transmit_midi_msg(ff, 1);
}

static void handle_midi_msg(struct fw_card *card, struct fw_request *request,
			    int tcode, int destination, int source,
			    int generation, unsigned long long offset,
			    void *data, size_t length, void *callback_data)
{
	struct snd_ff *ff = callback_data;
	__le32 *buf = data;

	fw_send_response(card, request, RCODE_COMPLETE);

	ff->spec->protocol->handle_midi_msg(ff, buf, length);
}

static int allocate_own_address(struct snd_ff *ff, int i)
{
	struct fw_address_region midi_msg_region;
	int err;

	ff->async_handler.length = SND_FF_MAXIMIM_MIDI_QUADS * 4;
	ff->async_handler.address_callback = handle_midi_msg;
	ff->async_handler.callback_data = ff;

	midi_msg_region.start = 0x000100000000ull * i;
	midi_msg_region.end = midi_msg_region.start + ff->async_handler.length;

	err = fw_core_add_address_handler(&ff->async_handler, &midi_msg_region);
	if (err >= 0) {
		/* Controllers are allowed to register this region. */
		if (ff->async_handler.offset & 0x0000ffffffff) {
			fw_core_remove_address_handler(&ff->async_handler);
			err = -EAGAIN;
		}
	}

	return err;
}

/*
 * Controllers are allowed to register higher 4 bytes of address to receive
 * the transactions. Different models have different registers for this purpose;
 * e.g. 0x'0000'8010'03f4 for Fireface 400.
 * The controllers are not allowed to register lower 4 bytes of the address.
 * They are forced to select one of 4 options for the part of address by writing
 * corresponding bits to 0x'0000'8010'051f.
 *
 * The 3rd-6th bits of this register are flags to indicate lower 4 bytes of
 * address to which the device transferrs the transactions. In short:
 *  - 0x20: 0x'....'....'0000'0180
 *  - 0x10: 0x'....'....'0000'0100
 *  - 0x08: 0x'....'....'0000'0080
 *  - 0x04: 0x'....'....'0000'0000
 *
 * This driver configure 0x'....'....'0000'0000 to receive MIDI messages from
 * units. The 3rd bit of the register should be configured, however this driver
 * deligates this task to userspace applications due to a restriction that this
 * register is write-only and the other bits have own effects.
 *
 * Unlike Fireface 800, Fireface 400 cancels transferring asynchronous
 * transactions when the 1st and 2nd of the register stand. These two bits have
 * the same effect.
 *  - 0x02, 0x01: cancel transferring
 *
 * On the other hand, the bits have no effect on Fireface 800. This model
 * cancels asynchronous transactions when the higher 4 bytes of address is
 * overwritten with zero.
 */
int snd_ff_transaction_reregister(struct snd_ff *ff)
{
	struct fw_card *fw_card = fw_parent_device(ff->unit)->card;
	u32 addr;
	__le32 reg;

	/*
	 * Controllers are allowed to register its node ID and upper 2 byte of
	 * local address to listen asynchronous transactions.
	 */
	addr = (fw_card->node_id << 16) | (ff->async_handler.offset >> 32);
	reg = cpu_to_le32(addr);
	return snd_fw_transaction(ff->unit, TCODE_WRITE_QUADLET_REQUEST,
				  ff->spec->midi_high_addr,
				  &reg, sizeof(reg), 0);
}

int snd_ff_transaction_register(struct snd_ff *ff)
{
	int i, err;

	/*
	 * Allocate in Memory Space of IEC 13213, but lower 4 byte in LSB should
	 * be zero due to device specification.
	 */
	for (i = 0; i < 0xffff; i++) {
		err = allocate_own_address(ff, i);
		if (err != -EBUSY && err != -EAGAIN)
			break;
	}
	if (err < 0)
		return err;

	err = snd_ff_transaction_reregister(ff);
	if (err < 0)
		return err;

	INIT_WORK(&ff->rx_midi_work[0], transmit_midi0_msg);
	INIT_WORK(&ff->rx_midi_work[1], transmit_midi1_msg);

	return 0;
}

void snd_ff_transaction_unregister(struct snd_ff *ff)
{
	__le32 reg;

	if (ff->async_handler.callback_data == NULL)
		return;
	ff->async_handler.callback_data = NULL;

	/* Release higher 4 bytes of address. */
	reg = cpu_to_le32(0x00000000);
	snd_fw_transaction(ff->unit, TCODE_WRITE_QUADLET_REQUEST,
			   ff->spec->midi_high_addr,
			   &reg, sizeof(reg), 0);

	fw_core_remove_address_handler(&ff->async_handler);
}
