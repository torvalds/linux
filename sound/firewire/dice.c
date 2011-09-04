/*
 * TC Applied Technologies Digital Interface Communications Engine driver
 *
 * Copyright (c) Clemens Ladisch <clemens@ladisch.de>
 * Licensed under the terms of the GNU General Public License, version 2.
 */

#include <linux/delay.h>
#include <linux/device.h>
#include <linux/firewire.h>
#include <linux/firewire-constants.h>
#include <linux/module.h>
#include <linux/mod_devicetable.h>
#include <linux/mutex.h>
#include <linux/slab.h>
#include <sound/control.h>
#include <sound/core.h>
#include <sound/hwdep.h>
#include <sound/initval.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include "amdtp.h"
#include "iso-resources.h"
#include "lib.h"

#define DICE_PRIVATE_SPACE		0xffffe0000000uLL

/* offset from DICE_PRIVATE_SPACE; offsets and sizes in quadlets */
#define DICE_GLOBAL_OFFSET		0x00
#define DICE_GLOBAL_SIZE		0x04
#define DICE_TX_OFFSET			0x08
#define DICE_TX_SIZE			0x0c
#define DICE_RX_OFFSET			0x10
#define DICE_RX_SIZE			0x14

/* pointed to by DICE_GLOBAL_OFFSET */
#define GLOBAL_OWNER			0x000
#define  OWNER_NO_OWNER			0xffff000000000000uLL
#define  OWNER_NODE_SHIFT		48
#define GLOBAL_NOTIFICATION		0x008
#define  NOTIFY_RX_CFG_CHG		0x00000001
#define  NOTIFY_TX_CFG_CHG		0x00000002
#define  NOTIFY_DUP_ISOC		0x00000004
#define  NOTIFY_BW_ERR			0x00000008
#define  NOTIFY_LOCK_CHG		0x00000010
#define  NOTIFY_CLOCK_ACCEPTED		0x00000020
#define  NOTIFY_INTERFACE_CHG		0x00000040
#define  NOTIFY_MESSAGE			0x00100000
#define GLOBAL_NICK_NAME		0x00c
#define  NICK_NAME_SIZE			64
#define GLOBAL_CLOCK_SELECT		0x04c
#define  CLOCK_SOURCE_MASK		0x000000ff
#define  CLOCK_SOURCE_AES1		0x00000000
#define  CLOCK_SOURCE_AES2		0x00000001
#define  CLOCK_SOURCE_AES3		0x00000002
#define  CLOCK_SOURCE_AES4		0x00000003
#define  CLOCK_SOURCE_AES_ANY		0x00000004
#define  CLOCK_SOURCE_ADAT		0x00000005
#define  CLOCK_SOURCE_TDIF		0x00000006
#define  CLOCK_SOURCE_WC		0x00000007
#define  CLOCK_SOURCE_ARX1		0x00000008
#define  CLOCK_SOURCE_ARX2		0x00000009
#define  CLOCK_SOURCE_ARX3		0x0000000a
#define  CLOCK_SOURCE_ARX4		0x0000000b
#define  CLOCK_SOURCE_INTERNAL		0x0000000c
#define  CLOCK_RATE_MASK		0x0000ff00
#define  CLOCK_RATE_32000		0x00000000
#define  CLOCK_RATE_44100		0x00000100
#define  CLOCK_RATE_48000		0x00000200
#define  CLOCK_RATE_88200		0x00000300
#define  CLOCK_RATE_96000		0x00000400
#define  CLOCK_RATE_176400		0x00000500
#define  CLOCK_RATE_192000		0x00000600
#define  CLOCK_RATE_ANY_LOW		0x00000700
#define  CLOCK_RATE_ANY_MID		0x00000800
#define  CLOCK_RATE_ANY_HIGH		0x00000900
#define  CLOCK_RATE_NONE		0x00000a00
#define  CLOCK_RATE_SHIFT		8
#define GLOBAL_ENABLE			0x050
#define  ENABLE				0x00000001
#define GLOBAL_STATUS			0x054
#define  STATUS_SOURCE_LOCKED		0x00000001
#define  STATUS_RATE_CONFLICT		0x00000002
#define  STATUS_NOMINAL_RATE_MASK	0x0000ff00
#define GLOBAL_EXTENDED_STATUS		0x058
#define  EXT_STATUS_AES1_LOCKED		0x00000001
#define  EXT_STATUS_AES2_LOCKED		0x00000002
#define  EXT_STATUS_AES3_LOCKED		0x00000004
#define  EXT_STATUS_AES4_LOCKED		0x00000008
#define  EXT_STATUS_ADAT_LOCKED		0x00000010
#define  EXT_STATUS_TDIF_LOCKED		0x00000020
#define  EXT_STATUS_ARX1_LOCKED		0x00000040
#define  EXT_STATUS_ARX2_LOCKED		0x00000080
#define  EXT_STATUS_ARX3_LOCKED		0x00000100
#define  EXT_STATUS_ARX4_LOCKED		0x00000200
#define  EXT_STATUS_WC_LOCKED		0x00000400
#define  EXT_STATUS_AES1_SLIP		0x00010000
#define  EXT_STATUS_AES2_SLIP		0x00020000
#define  EXT_STATUS_AES3_SLIP		0x00040000
#define  EXT_STATUS_AES4_SLIP		0x00080000
#define  EXT_STATUS_ADAT_SLIP		0x00100000
#define  EXT_STATUS_TDIF_SLIP		0x00200000
#define  EXT_STATUS_ARX1_SLIP		0x00400000
#define  EXT_STATUS_ARX2_SLIP		0x00800000
#define  EXT_STATUS_ARX3_SLIP		0x01000000
#define  EXT_STATUS_ARX4_SLIP		0x02000000
#define  EXT_STATUS_WC_SLIP		0x04000000
#define GLOBAL_SAMPLE_RATE		0x05c
#define GLOBAL_VERSION			0x060
#define GLOBAL_CLOCK_CAPABILITIES	0x064
#define  CLOCK_CAP_RATE_32000		0x00000001
#define  CLOCK_CAP_RATE_44100		0x00000002
#define  CLOCK_CAP_RATE_48000		0x00000004
#define  CLOCK_CAP_RATE_88200		0x00000008
#define  CLOCK_CAP_RATE_96000		0x00000010
#define  CLOCK_CAP_RATE_176400		0x00000020
#define  CLOCK_CAP_RATE_192000		0x00000040
#define  CLOCK_CAP_SOURCE_AES1		0x00010000
#define  CLOCK_CAP_SOURCE_AES2		0x00020000
#define  CLOCK_CAP_SOURCE_AES3		0x00040000
#define  CLOCK_CAP_SOURCE_AES4		0x00080000
#define  CLOCK_CAP_SOURCE_AES_ANY	0x00100000
#define  CLOCK_CAP_SOURCE_ADAT		0x00200000
#define  CLOCK_CAP_SOURCE_TDIF		0x00400000
#define  CLOCK_CAP_SOURCE_WC		0x00800000
#define  CLOCK_CAP_SOURCE_ARX1		0x01000000
#define  CLOCK_CAP_SOURCE_ARX2		0x02000000
#define  CLOCK_CAP_SOURCE_ARX3		0x04000000
#define  CLOCK_CAP_SOURCE_ARX4		0x08000000
#define  CLOCK_CAP_SOURCE_INTERNAL	0x10000000
#define GLOBAL_CLOCK_SOURCE_NAMES	0x068
#define  CLOCK_SOURCE_NAMES_SIZE	256

/* pointed to by DICE_TX_OFFSET */
#define TX_NUMBER			0x000
#define TX_SIZE				0x004
/* repeated TX_NUMBER times, offset by TX_SIZE quadlets */
#define TX_ISOCHRONOUS			0x008
#define TX_NUMBER_AUDIO			0x00c
#define TX_NUMBER_MIDI			0x010
#define TX_SPEED			0x014
#define TX_NAMES			0x018
#define  TX_NAMES_SIZE			256
#define TX_AC3_CAPABILITIES		0x118
#define TX_AC3_ENABLE			0x11c

/* pointed to by DICE_RX_OFFSET */
#define RX_NUMBER			0x000
#define RX_SIZE				0x004
/* repeated RX_NUMBER times, offset by RX_SIZE quadlets */
#define RX_ISOCHRONOUS			0x008
#define RX_SEQ_START			0x00c
#define RX_NUMBER_AUDIO			0x010
#define RX_NUMBER_MIDI			0x014
#define RX_NAMES			0x018
#define  RX_NAMES_SIZE			256
#define RX_AC3_CAPABILITIES		0x118
#define RX_AC3_ENABLE			0x11c


#define FIRMWARE_LOAD_SPACE		0xffffe0100000uLL

/* offset from FIRMWARE_LOAD_SPACE */
#define FIRMWARE_VERSION		0x000
#define FIRMWARE_OPCODE			0x004
#define  OPCODE_MASK			0x00000fff
#define  OPCODE_GET_IMAGE_DESC		0x00000000
#define  OPCODE_DELETE_IMAGE		0x00000001
#define  OPCODE_CREATE_IMAGE		0x00000002
#define  OPCODE_UPLOAD			0x00000003
#define  OPCODE_UPLOAD_STAT		0x00000004
#define  OPCODE_RESET_IMAGE		0x00000005
#define  OPCODE_TEST_ACTION		0x00000006
#define  OPCODE_GET_RUNNING_IMAGE_VINFO	0x0000000a
#define  OPCODE_EXECUTE			0x80000000
#define FIRMWARE_RETURN_STATUS		0x008
#define FIRMWARE_PROGRESS		0x00c
#define  PROGRESS_CURR_MASK		0x00000fff
#define  PROGRESS_MAX_MASK		0x00fff000
#define  PROGRESS_TOUT_MASK		0x0f000000
#define  PROGRESS_FLAG			0x80000000
#define FIRMWARE_CAPABILITIES		0x010
#define  FL_CAP_AUTOERASE		0x00000001
#define  FL_CAP_PROGRESS		0x00000002
#define FIRMWARE_DATA			0x02c
#define  TEST_CMD_POKE			0x00000001
#define  TEST_CMD_PEEK			0x00000002
#define  CMD_GET_AVS_CNT		0x00000003
#define  CMD_CLR_AVS_CNT		0x00000004
#define  CMD_SET_MODE			0x00000005
#define  CMD_SET_MIDIBP			0x00000006
#define  CMD_GET_AVSPHASE		0x00000007
#define  CMD_ENABLE_BNC_SYNC		0x00000008
#define  CMD_PULSE_BNC_SYNC		0x00000009
#define  CMD_EMUL_SLOW_CMD		0x0000000a
#define FIRMWARE_TEST_DELAY		0xfd8
#define FIRMWARE_TEST_BUF		0xfdc


/* EAP */
#define EAP_PRIVATE_SPACE		0xffffe0200000uLL

#define EAP_CAPABILITY_OFFSET		0x000
#define EAP_CAPABILITY_SIZE		0x004
/* ... */

#define EAP_ROUTER_CAPS			0x000
#define  ROUTER_EXPOSED			0x00000001
#define  ROUTER_READ_ONLY		0x00000002
#define  ROUTER_FLASH			0x00000004
#define  MAX_ROUTES_MASK		0xffff0000
#define EAP_MIXER_CAPS			0x004
#define  MIXER_EXPOSED			0x00000001
#define  MIXER_READ_ONLY		0x00000002
#define  MIXER_FLASH			0x00000004
#define  MIXER_IN_DEV_MASK		0x000000f0
#define  MIXER_OUT_DEV_MASK		0x00000f00
#define  MIXER_INPUTS_MASK		0x00ff0000
#define  MIXER_OUTPUTS_MASK		0xff000000
#define EAP_GENERAL_CAPS		0x008
#define  GENERAL_STREAM_CONFIG		0x00000001
#define  GENERAL_FLASH			0x00000002
#define  GENERAL_PEAK			0x00000004
#define  GENERAL_MAX_TX_STREAMS_MASK	0x000000f0
#define  GENERAL_MAX_RX_STREAMS_MASK	0x00000f00
#define  GENERAL_STREAM_CONFIG_FLASH	0x00001000
#define  GENERAL_CHIP_MASK		0x00ff0000
#define  GENERAL_CHIP_DICE_II		0x00000000
#define  GENERAL_CHIP_DICE_MINI		0x00010000
#define  GENERAL_CHIP_DICE_JR		0x00020000


struct dice {
	struct snd_card *card;
	struct fw_unit *unit;
	struct mutex mutex;
	unsigned int global_offset;
	unsigned int rx_offset;
	struct fw_address_handler notification_handler;
	int owner_generation;
	bool global_enabled;
	bool stream_running;
	struct snd_pcm_substream *pcm;
	struct fw_iso_resources resources;
	struct amdtp_out_stream stream;
};

MODULE_DESCRIPTION("DICE driver");
MODULE_AUTHOR("Clemens Ladisch <clemens@ladisch.de>");
MODULE_LICENSE("GPL v2");

static const unsigned int dice_rates[] = {
	[0] =  32000,
	[1] =  44100,
	[2] =  48000,
	[3] =  88200,
	[4] =  96000,
	[5] = 176400,
	[6] = 192000,
};

static inline u64 global_address(struct dice *dice, unsigned int offset)
{
	return DICE_PRIVATE_SPACE + dice->global_offset + offset;
}

// TODO: rx index
static inline u64 rx_address(struct dice *dice, unsigned int offset)
{
	return DICE_PRIVATE_SPACE + dice->rx_offset + offset;
}

static int dice_owner_set(struct dice *dice)
{
	struct fw_device *device = fw_parent_device(dice->unit);
	__be64 *buffer;
	int rcode, err, errors = 0;

	buffer = kmalloc(2 * 8, GFP_KERNEL);
	if (!buffer)
		return -ENOMEM;

	for (;;) {
		buffer[0] = cpu_to_be64(OWNER_NO_OWNER);
		buffer[1] = cpu_to_be64(
			((u64)device->card->node_id << OWNER_NODE_SHIFT) |
			dice->notification_handler.offset);

		dice->owner_generation = device->generation;
		smp_rmb(); /* node_id vs. generation */
		rcode = fw_run_transaction(device->card,
					   TCODE_LOCK_COMPARE_SWAP,
					   device->node_id,
					   dice->owner_generation,
					   device->max_speed,
					   global_address(dice, GLOBAL_OWNER),
					   buffer, 2 * 8);

		if (rcode == RCODE_COMPLETE) {
			if (buffer[0] == cpu_to_be64(OWNER_NO_OWNER)) {
				err = 0;
			} else {
				dev_err(&dice->unit->device,
					"device is already in use\n");
				err = -EBUSY;
			}
			break;
		}
		if (rcode_is_permanent_error(rcode) || ++errors >= 3) {
			dev_err(&dice->unit->device,
				"setting device owner failed: %s\n",
				fw_rcode_string(rcode));
			err = -EIO;
			break;
		}
		msleep(20);
	}

	kfree(buffer);

	return err;
}

static int dice_owner_update(struct dice *dice)
{
	struct fw_device *device = fw_parent_device(dice->unit);
	__be64 *buffer;
	int rcode, err, errors = 0;

	if (dice->owner_generation == -1)
		return 0;

	buffer = kmalloc(2 * 8, GFP_KERNEL);
	if (!buffer)
		return -ENOMEM;

	for (;;) {
		buffer[0] = cpu_to_be64(OWNER_NO_OWNER);
		buffer[1] = cpu_to_be64(
			((u64)device->card->node_id << OWNER_NODE_SHIFT) |
			dice->notification_handler.offset);

		dice->owner_generation = device->generation;
		smp_rmb(); /* node_id vs. generation */
		rcode = fw_run_transaction(device->card,
					   TCODE_LOCK_COMPARE_SWAP,
					   device->node_id,
					   dice->owner_generation,
					   device->max_speed,
					   global_address(dice, GLOBAL_OWNER),
					   buffer, 2 * 8);

		if (rcode == RCODE_COMPLETE) {
			if (buffer[0] == cpu_to_be64(OWNER_NO_OWNER)) {
				err = 0;
			} else {
				dev_err(&dice->unit->device,
					"device is already in use\n");
				err = -EBUSY;
			}
			break;
		}
		if (rcode == RCODE_GENERATION) {
			err = 0; /* try again later */
			break;
		}
		if (rcode_is_permanent_error(rcode) || ++errors >= 3) {
			dev_err(&dice->unit->device,
				"setting device owner failed: %s\n",
				fw_rcode_string(rcode));
			err = -EIO;
			break;
		}
		msleep(20);
	}

	kfree(buffer);

	if (err < 0)
		dice->owner_generation = -1;

	return err;
}

static void dice_owner_clear(struct dice *dice)
{
	struct fw_device *device = fw_parent_device(dice->unit);
	__be64 *buffer;
	int rcode, errors = 0;

	buffer = kmalloc(2 * 8, GFP_KERNEL);
	if (!buffer)
		return;

	for (;;) {
		buffer[0] = cpu_to_be64(
			((u64)device->card->node_id << OWNER_NODE_SHIFT) |
			dice->notification_handler.offset);
		buffer[1] = cpu_to_be64(OWNER_NO_OWNER);

		rcode = fw_run_transaction(device->card,
					   TCODE_LOCK_COMPARE_SWAP,
					   device->node_id,
					   dice->owner_generation,
					   device->max_speed,
					   global_address(dice, GLOBAL_OWNER),
					   buffer, 2 * 8);

		if (rcode == RCODE_COMPLETE)
			break;
		if (rcode == RCODE_GENERATION)
			break;
		if (rcode_is_permanent_error(rcode) || ++errors >= 3) {
			dev_err(&dice->unit->device,
				"clearing device owner failed: %s\n",
				fw_rcode_string(rcode));
			break;
		}
		msleep(20);
	}

	kfree(buffer);

	dice->owner_generation = -1;
}

static int dice_enable_set(struct dice *dice)
{
	struct fw_device *device = fw_parent_device(dice->unit);
	__be32 value;
	int rcode, err, errors = 0;

	value = cpu_to_be32(ENABLE);
	for (;;) {
		rcode = fw_run_transaction(device->card,
					   TCODE_WRITE_QUADLET_REQUEST,
					   device->node_id,
					   dice->owner_generation,
					   device->max_speed,
					   global_address(dice, GLOBAL_ENABLE),
					   &value, 4);
		if (rcode == RCODE_COMPLETE) {
			dice->global_enabled = true;
			err = 0;
			break;
		}
		if (rcode == RCODE_GENERATION) {
			err = -EAGAIN;
			break;
		}
		if (rcode_is_permanent_error(rcode) || ++errors >= 3) {
			dev_err(&dice->unit->device,
				"device enabling failed: %s\n",
				fw_rcode_string(rcode));
			err = -EIO;
			break;
		}
		msleep(20);
	}

	return err;
}

static void dice_enable_clear(struct dice *dice)
{
	struct fw_device *device = fw_parent_device(dice->unit);
	__be32 value;
	int rcode, errors = 0;

	value = 0;
	for (;;) {
		rcode = fw_run_transaction(device->card,
					   TCODE_WRITE_QUADLET_REQUEST,
					   device->node_id,
					   dice->owner_generation,
					   device->max_speed,
					   global_address(dice, GLOBAL_ENABLE),
					   &value, 4);
		if (rcode == RCODE_COMPLETE ||
		    rcode == RCODE_GENERATION)
			break;
		if (rcode_is_permanent_error(rcode) || ++errors >= 3) {
			dev_err(&dice->unit->device,
				"device disabling failed: %s\n",
				fw_rcode_string(rcode));
			break;
		}
		msleep(20);
	}
	dice->global_enabled = false;
}

static void dice_notification(struct fw_card *card, struct fw_request *request,
			      int tcode, int destination, int source,
			      int generation, unsigned long long offset,
			      void *data, size_t length, void *callback_data)
{
	struct dice *dice = callback_data;

	if (tcode != TCODE_WRITE_QUADLET_REQUEST) {
		fw_send_response(card, request, RCODE_TYPE_ERROR);
		return;
	}
	if ((offset & 3) != 0) {
		fw_send_response(card, request, RCODE_ADDRESS_ERROR);
		return;
	}
	dev_dbg(&dice->unit->device,
		"notification: %08x\n", be32_to_cpup(data));
	fw_send_response(card, request, RCODE_COMPLETE);
}

static int dice_open(struct snd_pcm_substream *substream)
{
	static const struct snd_pcm_hardware hardware = {
		.info = SNDRV_PCM_INFO_MMAP |
			SNDRV_PCM_INFO_MMAP_VALID |
			SNDRV_PCM_INFO_BATCH |
			SNDRV_PCM_INFO_INTERLEAVED |
			SNDRV_PCM_INFO_BLOCK_TRANSFER,
		.formats = AMDTP_OUT_PCM_FORMAT_BITS,
		.buffer_bytes_max = 16 * 1024 * 1024,
		.period_bytes_min = 1,
		.period_bytes_max = UINT_MAX,
		.periods_min = 1,
		.periods_max = UINT_MAX,
	};
	struct dice *dice = substream->private_data;
	struct snd_pcm_runtime *runtime = substream->runtime;
	__be32 clock_sel, number_audio, number_midi;
	unsigned int rate;
	int err;

	err = snd_fw_transaction(dice->unit, TCODE_READ_QUADLET_REQUEST,
				 global_address(dice, GLOBAL_CLOCK_SELECT),
				 &clock_sel, 4);
	if (err < 0)
		return err;
	rate = (be32_to_cpu(clock_sel) & CLOCK_RATE_MASK) >> CLOCK_RATE_SHIFT;
	if (rate >= ARRAY_SIZE(dice_rates))
		return -ENXIO;
	rate = dice_rates[rate];

	err = snd_fw_transaction(dice->unit, TCODE_READ_QUADLET_REQUEST,
				 rx_address(dice, RX_NUMBER_AUDIO),
				 &number_audio, 4);
	if (err < 0)
		return err;
	err = snd_fw_transaction(dice->unit, TCODE_READ_QUADLET_REQUEST,
				 rx_address(dice, RX_NUMBER_MIDI),
				 &number_midi, 4);
	if (err < 0)
		return err;

	runtime->hw = hardware;

	runtime->hw.rates = snd_pcm_rate_to_rate_bit(rate);
	snd_pcm_limit_hw_rates(runtime);

	runtime->hw.channels_min = be32_to_cpu(number_audio);
	runtime->hw.channels_max = be32_to_cpu(number_audio);

	amdtp_out_stream_set_rate(&dice->stream, rate);
	amdtp_out_stream_set_pcm(&dice->stream, be32_to_cpu(number_audio));
	amdtp_out_stream_set_midi(&dice->stream, be32_to_cpu(number_midi));

	err = snd_pcm_hw_constraint_minmax(runtime,
					   SNDRV_PCM_HW_PARAM_PERIOD_TIME,
					   5000, 8192000);
	if (err < 0)
		return err;

	err = snd_pcm_hw_constraint_msbits(runtime, 0, 32, 24);
	if (err < 0)
		return err;

	return 0;
}

static int dice_close(struct snd_pcm_substream *substream)
{
	return 0;
}

static int dice_stream_start_packets(struct dice *dice)
{
	int err;

	if (dice->stream_running)
		return 0;

	err = amdtp_out_stream_start(&dice->stream, dice->resources.channel,
				     fw_parent_device(dice->unit)->max_speed);
	if (err < 0)
		return err;

	err = dice_enable_set(dice);
	if (err < 0) {
		amdtp_out_stream_stop(&dice->stream);
		return err;
	}

	dice->stream_running = true;

	return 0;
}

static int dice_stream_start(struct dice *dice)
{
	__be32 channel;
	int err;

	if (!dice->resources.allocated) {
		err = fw_iso_resources_allocate(&dice->resources,
				amdtp_out_stream_get_max_payload(&dice->stream),
				fw_parent_device(dice->unit)->max_speed);
		if (err < 0)
			goto error;

		channel = cpu_to_be32(dice->resources.channel);
		err = snd_fw_transaction(dice->unit,
					 TCODE_WRITE_QUADLET_REQUEST,
					 rx_address(dice, RX_ISOCHRONOUS),
					 &channel, 4);
		if (err < 0)
			goto err_resources;
	}

	err = dice_stream_start_packets(dice);
	if (err < 0)
		goto err_rx_channel;

	return 0;

err_rx_channel:
	channel = cpu_to_be32((u32)-1);
	snd_fw_transaction(dice->unit, TCODE_WRITE_QUADLET_REQUEST,
			   rx_address(dice, RX_ISOCHRONOUS), &channel, 4);
err_resources:
	fw_iso_resources_free(&dice->resources);
error:
	return err;
}

static void dice_stream_stop_packets(struct dice *dice)
{
	if (!dice->stream_running)
		return;

	dice_enable_clear(dice);

	amdtp_out_stream_stop(&dice->stream);

	dice->stream_running = false;
}

static void dice_stream_stop(struct dice *dice)
{
	__be32 channel;

	dice_stream_stop_packets(dice);

	if (!dice->resources.allocated)
		return;

	channel = cpu_to_be32((u32)-1);
	snd_fw_transaction(dice->unit, TCODE_WRITE_QUADLET_REQUEST,
			   rx_address(dice, RX_ISOCHRONOUS), &channel, 4);

	fw_iso_resources_free(&dice->resources);
}

static int dice_hw_params(struct snd_pcm_substream *substream,
			  struct snd_pcm_hw_params *hw_params)
{
	struct dice *dice = substream->private_data;
	int err;

	mutex_lock(&dice->mutex);
	dice_stream_stop(dice);
	mutex_unlock(&dice->mutex);

	err = snd_pcm_lib_alloc_vmalloc_buffer(substream,
					       params_buffer_bytes(hw_params));
	if (err < 0)
		goto error;

	amdtp_out_stream_set_pcm_format(&dice->stream,
					params_format(hw_params));

	return 0;

error:
	return err;
}

static int dice_hw_free(struct snd_pcm_substream *substream)
{
	struct dice *dice = substream->private_data;

	mutex_lock(&dice->mutex);
	dice_stream_stop(dice);
	mutex_unlock(&dice->mutex);

	return snd_pcm_lib_free_vmalloc_buffer(substream);
}

static int dice_prepare(struct snd_pcm_substream *substream)
{
	struct dice *dice = substream->private_data;
	int err;

	mutex_lock(&dice->mutex);

	if (amdtp_out_streaming_error(&dice->stream))
		dice_stream_stop_packets(dice);

	err = dice_stream_start(dice);
	if (err < 0) {
		mutex_unlock(&dice->mutex);
		return err;
	}

	mutex_unlock(&dice->mutex);

	amdtp_out_stream_pcm_prepare(&dice->stream);

	return 0;
}

static int dice_trigger(struct snd_pcm_substream *substream, int cmd)
{
	struct dice *dice = substream->private_data;
	struct snd_pcm_substream *pcm;

	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
		pcm = substream;
		break;
	case SNDRV_PCM_TRIGGER_STOP:
		pcm = NULL;
		break;
	default:
		return -EINVAL;
	}
	amdtp_out_stream_pcm_trigger(&dice->stream, pcm);

	return 0;
}

static snd_pcm_uframes_t dice_pointer(struct snd_pcm_substream *substream)
{
	struct dice *dice = substream->private_data;

	return amdtp_out_stream_pcm_pointer(&dice->stream);
}

static int dice_create_pcm(struct dice *dice)
{
	static struct snd_pcm_ops ops = {
		.open      = dice_open,
		.close     = dice_close,
		.ioctl     = snd_pcm_lib_ioctl,
		.hw_params = dice_hw_params,
		.hw_free   = dice_hw_free,
		.prepare   = dice_prepare,
		.trigger   = dice_trigger,
		.pointer   = dice_pointer,
		.page      = snd_pcm_lib_get_vmalloc_page,
		.mmap      = snd_pcm_lib_mmap_vmalloc,
	};
	struct snd_pcm *pcm;
	int err;

	err = snd_pcm_new(dice->card, "DICE", 0, 1, 0, &pcm);
	if (err < 0)
		return err;
	pcm->private_data = dice;
	strcpy(pcm->name, dice->card->shortname);
	dice->pcm = pcm->streams[SNDRV_PCM_STREAM_PLAYBACK].substream;
	dice->pcm->ops = &ops;

	return 0;
}

// TODO: implement these

static long dice_hwdep_read(struct snd_hwdep *hwdep, char __user *buf,
			    long count, loff_t *offset)
{
	return -EIO;
}

static int dice_hwdep_open(struct snd_hwdep *hwdep, struct file *file)
{
	return -EIO;
}

static int dice_hwdep_release(struct snd_hwdep *hwdep, struct file *file)
{
	return 0;
}

static unsigned int dice_hwdep_poll(struct snd_hwdep *hwdep, struct file *file,
				    poll_table *wait)
{
	return POLLERR | POLLHUP;
}

static int dice_hwdep_ioctl(struct snd_hwdep *hwdep, struct file *file,
			    unsigned int cmd, unsigned long arg)
{
	return -EIO;
}

static int dice_create_hwdep(struct dice *dice)
{
	static const struct snd_hwdep_ops ops = {
		.read         = dice_hwdep_read,
		.open         = dice_hwdep_open,
		.release      = dice_hwdep_release,
		.poll         = dice_hwdep_poll,
		.ioctl        = dice_hwdep_ioctl,
		.ioctl_compat = dice_hwdep_ioctl,
	};
	struct snd_hwdep *hwdep;
	int err;

	err = snd_hwdep_new(dice->card, "DICE", 0, &hwdep);
	if (err < 0)
		return err;
	strcpy(hwdep->name, "DICE");
	hwdep->iface = SNDRV_HWDEP_IFACE_FW_DICE;
	hwdep->ops = ops;
	hwdep->private_data = dice;
	hwdep->exclusive = true;

	return 0;
}

static void dice_card_free(struct snd_card *card)
{
	struct dice *dice = card->private_data;

	amdtp_out_stream_destroy(&dice->stream);
	fw_core_remove_address_handler(&dice->notification_handler);
	mutex_destroy(&dice->mutex);
}

static int dice_init_offsets(struct dice *dice)
{
	__be32 pointers[6];
	unsigned int global_size, rx_size;
	int err;

	err = snd_fw_transaction(dice->unit, TCODE_READ_BLOCK_REQUEST,
				 DICE_PRIVATE_SPACE, &pointers, 6 * 4);
	if (err < 0)
		return err;

	dice->global_offset = be32_to_cpu(pointers[0]) * 4;
	global_size = be32_to_cpu(pointers[1]);
	dice->rx_offset = be32_to_cpu(pointers[4]) * 4;
	rx_size = be32_to_cpu(pointers[5]);

	/* some sanity checks to ensure that we actually have a DICE */
	if (dice->global_offset < 10 * 4 || global_size < 0x168 / 4 ||
	    dice->rx_offset < 10 * 4 || rx_size < 0x120 / 4) {
		dev_err(&dice->unit->device, "invalid register pointers\n");
		return -ENXIO;
	}

	return 0;
}

static void dice_card_strings(struct dice *dice)
{
	struct snd_card *card = dice->card;
	struct fw_device *dev = fw_parent_device(dice->unit);
	char vendor[32], model[32];
	unsigned int i;
	int err;

	strcpy(card->driver, "DICE");

	strcpy(card->shortname, "DICE");
	BUILD_BUG_ON(NICK_NAME_SIZE < sizeof(card->shortname));
	err = snd_fw_transaction(dice->unit, TCODE_READ_BLOCK_REQUEST,
				 global_address(dice, GLOBAL_NICK_NAME),
				 card->shortname, sizeof(card->shortname));
	if (err >= 0) {
		/* DICE strings are returned in "always-wrong" endianness */
		BUILD_BUG_ON(sizeof(card->shortname) % 4 != 0);
		for (i = 0; i < sizeof(card->shortname); i += 4)
			swab32s((u32 *)&card->shortname[i]);
		card->shortname[sizeof(card->shortname) - 1] = '\0';
	}

	strcpy(vendor, "?");
	fw_csr_string(dev->config_rom + 5, CSR_VENDOR, vendor, sizeof(vendor));
	strcpy(model, "?");
	fw_csr_string(dice->unit->directory, CSR_MODEL, model, sizeof(model));
	snprintf(card->longname, sizeof(card->longname),
		 "%s %s, GUID %08x%08x at %s, S%d",
		 vendor, model, dev->config_rom[3], dev->config_rom[4],
		 dev_name(&dice->unit->device), 100 << dev->max_speed);

	strcpy(card->mixername, "DICE");
}

static int dice_probe(struct fw_unit *unit, const struct ieee1394_device_id *id)
{
	struct snd_card *card;
	struct dice *dice;
	__be32 clock_sel;
	int err;

	err = snd_card_create(-1, NULL, THIS_MODULE, sizeof(*dice), &card);
	if (err < 0)
		return err;
	snd_card_set_dev(card, &unit->device);

	dice = card->private_data;
	dice->card = card;
	mutex_init(&dice->mutex);
	dice->unit = unit;

	err = dice_init_offsets(dice);
	if (err < 0)
		goto err_mutex;

	dice->notification_handler.length = 4;
	dice->notification_handler.address_callback = dice_notification;
	dice->notification_handler.callback_data = dice;
	err = fw_core_add_address_handler(&dice->notification_handler,
					  &fw_high_memory_region);
	if (err < 0)
		goto err_mutex;

	err = fw_iso_resources_init(&dice->resources, unit);
	if (err < 0)
		goto err_notification_handler;
	dice->resources.channels_mask = 0x00000000ffffffffuLL;

	err = amdtp_out_stream_init(&dice->stream, unit, CIP_NONBLOCKING);
	if (err < 0)
		goto err_resources;

	err = dice_owner_set(dice);
	if (err < 0)
		goto err_stream;

	card->private_free = dice_card_free;

	dice_card_strings(dice);

	err = snd_fw_transaction(unit, TCODE_READ_QUADLET_REQUEST,
				 global_address(dice, GLOBAL_CLOCK_SELECT),
				 &clock_sel, 4);
	if (err < 0)
		goto error;
	clock_sel &= cpu_to_be32(~CLOCK_SOURCE_MASK);
	clock_sel |= cpu_to_be32(CLOCK_SOURCE_ARX1);
	err = snd_fw_transaction(unit, TCODE_WRITE_QUADLET_REQUEST,
				 global_address(dice, GLOBAL_CLOCK_SELECT),
				 &clock_sel, 4);
	if (err < 0)
		goto error;

	err = dice_create_pcm(dice);
	if (err < 0)
		goto error;

	err = dice_create_hwdep(dice);
	if (err < 0)
		goto error;

	err = snd_card_register(card);
	if (err < 0)
		goto error;

	dev_set_drvdata(&unit->device, dice);

	return 0;

err_stream:
	amdtp_out_stream_destroy(&dice->stream);
err_resources:
	fw_iso_resources_destroy(&dice->resources);
err_notification_handler:
	fw_core_remove_address_handler(&dice->notification_handler);
err_mutex:
	mutex_destroy(&dice->mutex);
error:
	snd_card_free(card);
	return err;
}

static void dice_remove(struct fw_unit *unit)
{
	struct dice *dice = dev_get_drvdata(&unit->device);

	snd_card_disconnect(dice->card);

	mutex_lock(&dice->mutex);
	amdtp_out_stream_pcm_abort(&dice->stream);
	dice_stream_stop(dice);
	dice_owner_clear(dice);
	mutex_unlock(&dice->mutex);

	snd_card_free_when_closed(dice->card);
}

static void dice_bus_reset(struct fw_unit *unit)
{
	struct dice *dice = dev_get_drvdata(&unit->device);

	mutex_lock(&dice->mutex);

	/*
	 * On a bus reset, the DICE firmware disables streaming and then goes
	 * off contemplating its own navel for hundreds of milliseconds before
	 * it can react to any of our attempts to reenable streaming.  This
	 * means that we lose synchronization anyway, so we force our streams
	 * to stop so that the application can restart them in an orderly
	 * manner.
	 */
	amdtp_out_stream_pcm_abort(&dice->stream);
	dice_stream_stop_packets(dice);

	dice_owner_update(dice);

	fw_iso_resources_update(&dice->resources);

	mutex_unlock(&dice->mutex);
}

#define TC_OUI		0x000166
#define DICE_INTERFACE	0x000001

static const struct ieee1394_device_id dice_id_table[] = {
	{
		.match_flags  = IEEE1394_MATCH_SPECIFIER_ID |
				IEEE1394_MATCH_VERSION,
		.specifier_id = TC_OUI,
		.version      = DICE_INTERFACE,
	},
	{ }
};
MODULE_DEVICE_TABLE(ieee1394, dice_id_table);

static struct fw_driver dice_driver = {
	.driver   = {
		.owner	= THIS_MODULE,
		.name	= KBUILD_MODNAME,
		.bus	= &fw_bus_type,
	},
	.probe    = dice_probe,
	.update   = dice_bus_reset,
	.remove   = dice_remove,
	.id_table = dice_id_table,
};

static int __init alsa_dice_init(void)
{
	return driver_register(&dice_driver.driver);
}

static void __exit alsa_dice_exit(void)
{
	driver_unregister(&dice_driver.driver);
}

module_init(alsa_dice_init);
module_exit(alsa_dice_exit);
