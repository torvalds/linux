/*
 * dice.h - a part of driver for Dice based devices
 *
 * Copyright (c) Clemens Ladisch
 * Copyright (c) 2014 Takashi Sakamoto
 *
 * Licensed under the terms of the GNU General Public License, version 2.
 */

#ifndef SOUND_DICE_H_INCLUDED
#define SOUND_DICE_H_INCLUDED

#include <linux/compat.h>
#include <linux/completion.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/firewire.h>
#include <linux/firewire-constants.h>
#include <linux/jiffies.h>
#include <linux/module.h>
#include <linux/mod_devicetable.h>
#include <linux/mutex.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/wait.h>

#include <sound/control.h>
#include <sound/core.h>
#include <sound/firewire.h>
#include <sound/hwdep.h>
#include <sound/info.h>
#include <sound/initval.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/rawmidi.h>

#include "../amdtp.h"
#include "../iso-resources.h"
#include "../lib.h"
#include "dice-interface.h"

struct snd_dice {
	struct snd_card *card;
	struct fw_unit *unit;
	spinlock_t lock;
	struct mutex mutex;

	/* Offsets for sub-addresses */
	unsigned int global_offset;
	unsigned int rx_offset;
	unsigned int tx_offset;
	unsigned int sync_offset;
	unsigned int rsrv_offset;

	unsigned int clock_caps;
	unsigned int tx_channels[3];
	unsigned int rx_channels[3];
	unsigned int tx_midi_ports[3];
	unsigned int rx_midi_ports[3];

	struct fw_address_handler notification_handler;
	int owner_generation;
	u32 notification_bits;

	/* For uapi */
	int dev_lock_count; /* > 0 driver, < 0 userspace */
	bool dev_lock_changed;
	wait_queue_head_t hwdep_wait;

	/* For streaming */
	struct fw_iso_resources tx_resources;
	struct fw_iso_resources rx_resources;
	struct amdtp_stream tx_stream;
	struct amdtp_stream rx_stream;
	bool global_enabled;
	struct completion clock_accepted;
	unsigned int substreams_counter;
};

enum snd_dice_addr_type {
	SND_DICE_ADDR_TYPE_PRIVATE,
	SND_DICE_ADDR_TYPE_GLOBAL,
	SND_DICE_ADDR_TYPE_TX,
	SND_DICE_ADDR_TYPE_RX,
	SND_DICE_ADDR_TYPE_SYNC,
	SND_DICE_ADDR_TYPE_RSRV,
};

int snd_dice_transaction_write(struct snd_dice *dice,
			       enum snd_dice_addr_type type,
			       unsigned int offset,
			       void *buf, unsigned int len);
int snd_dice_transaction_read(struct snd_dice *dice,
			      enum snd_dice_addr_type type, unsigned int offset,
			      void *buf, unsigned int len);

static inline int snd_dice_transaction_write_global(struct snd_dice *dice,
						    unsigned int offset,
						    void *buf, unsigned int len)
{
	return snd_dice_transaction_write(dice,
					  SND_DICE_ADDR_TYPE_GLOBAL, offset,
					  buf, len);
}
static inline int snd_dice_transaction_read_global(struct snd_dice *dice,
						   unsigned int offset,
						   void *buf, unsigned int len)
{
	return snd_dice_transaction_read(dice,
					 SND_DICE_ADDR_TYPE_GLOBAL, offset,
					 buf, len);
}
static inline int snd_dice_transaction_write_tx(struct snd_dice *dice,
						unsigned int offset,
						void *buf, unsigned int len)
{
	return snd_dice_transaction_write(dice, SND_DICE_ADDR_TYPE_TX, offset,
					  buf, len);
}
static inline int snd_dice_transaction_read_tx(struct snd_dice *dice,
					       unsigned int offset,
					       void *buf, unsigned int len)
{
	return snd_dice_transaction_read(dice, SND_DICE_ADDR_TYPE_TX, offset,
					 buf, len);
}
static inline int snd_dice_transaction_write_rx(struct snd_dice *dice,
						unsigned int offset,
						void *buf, unsigned int len)
{
	return snd_dice_transaction_write(dice, SND_DICE_ADDR_TYPE_RX, offset,
					  buf, len);
}
static inline int snd_dice_transaction_read_rx(struct snd_dice *dice,
					       unsigned int offset,
					       void *buf, unsigned int len)
{
	return snd_dice_transaction_read(dice, SND_DICE_ADDR_TYPE_RX, offset,
					 buf, len);
}
static inline int snd_dice_transaction_write_sync(struct snd_dice *dice,
						  unsigned int offset,
						  void *buf, unsigned int len)
{
	return snd_dice_transaction_write(dice, SND_DICE_ADDR_TYPE_SYNC, offset,
					  buf, len);
}
static inline int snd_dice_transaction_read_sync(struct snd_dice *dice,
						 unsigned int offset,
						 void *buf, unsigned int len)
{
	return snd_dice_transaction_read(dice, SND_DICE_ADDR_TYPE_SYNC, offset,
					 buf, len);
}

int snd_dice_transaction_get_clock_source(struct snd_dice *dice,
					  unsigned int *source);
int snd_dice_transaction_set_rate(struct snd_dice *dice, unsigned int rate);
int snd_dice_transaction_get_rate(struct snd_dice *dice, unsigned int *rate);
int snd_dice_transaction_set_enable(struct snd_dice *dice);
void snd_dice_transaction_clear_enable(struct snd_dice *dice);
int snd_dice_transaction_init(struct snd_dice *dice);
int snd_dice_transaction_reinit(struct snd_dice *dice);
void snd_dice_transaction_destroy(struct snd_dice *dice);

#define SND_DICE_RATES_COUNT	7
extern const unsigned int snd_dice_rates[SND_DICE_RATES_COUNT];

int snd_dice_stream_get_rate_mode(struct snd_dice *dice,
				  unsigned int rate, unsigned int *mode);

int snd_dice_stream_start_duplex(struct snd_dice *dice, unsigned int rate);
void snd_dice_stream_stop_duplex(struct snd_dice *dice);
int snd_dice_stream_init_duplex(struct snd_dice *dice);
void snd_dice_stream_destroy_duplex(struct snd_dice *dice);
void snd_dice_stream_update_duplex(struct snd_dice *dice);

int snd_dice_stream_lock_try(struct snd_dice *dice);
void snd_dice_stream_lock_release(struct snd_dice *dice);

int snd_dice_create_pcm(struct snd_dice *dice);

int snd_dice_create_hwdep(struct snd_dice *dice);

void snd_dice_create_proc(struct snd_dice *dice);

int snd_dice_create_midi(struct snd_dice *dice);

#endif
