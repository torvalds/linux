/*
 * fireworks_command.c - a part of driver for Fireworks based devices
 *
 * Copyright (c) 2013-2014 Takashi Sakamoto
 *
 * Licensed under the terms of the GNU General Public License, version 2.
 */

#include "./fireworks.h"

/*
 * This driver uses transaction version 1 or later to use extended hardware
 * information. Then too old devices are not available.
 *
 * Each commands are not required to have continuous sequence numbers. This
 * number is just used to match command and response.
 *
 * This module support a part of commands. Please see FFADO if you want to see
 * whole commands. But there are some commands which FFADO don't implement.
 *
 * Fireworks also supports AV/C general commands and AV/C Stream Format
 * Information commands. But this module don't use them.
 */

#define KERNEL_SEQNUM_MIN	(SND_EFW_TRANSACTION_USER_SEQNUM_MAX + 2)
#define KERNEL_SEQNUM_MAX	((u32)~0)

/* for clock source and sampling rate */
struct efc_clock {
	u32 source;
	u32 sampling_rate;
	u32 index;
};

/* command categories */
enum efc_category {
	EFC_CAT_HWINFO		= 0,
	EFC_CAT_TRANSPORT	= 2,
	EFC_CAT_HWCTL		= 3,
};

/* hardware info category commands */
enum efc_cmd_hwinfo {
	EFC_CMD_HWINFO_GET_CAPS		= 0,
	EFC_CMD_HWINFO_GET_POLLED	= 1,
	EFC_CMD_HWINFO_SET_RESP_ADDR	= 2
};

enum efc_cmd_transport {
	EFC_CMD_TRANSPORT_SET_TX_MODE	= 0
};

/* hardware control category commands */
enum efc_cmd_hwctl {
	EFC_CMD_HWCTL_SET_CLOCK		= 0,
	EFC_CMD_HWCTL_GET_CLOCK		= 1,
	EFC_CMD_HWCTL_IDENTIFY		= 5
};

/* return values in response */
enum efr_status {
	EFR_STATUS_OK			= 0,
	EFR_STATUS_BAD			= 1,
	EFR_STATUS_BAD_COMMAND		= 2,
	EFR_STATUS_COMM_ERR		= 3,
	EFR_STATUS_BAD_QUAD_COUNT	= 4,
	EFR_STATUS_UNSUPPORTED		= 5,
	EFR_STATUS_1394_TIMEOUT		= 6,
	EFR_STATUS_DSP_TIMEOUT		= 7,
	EFR_STATUS_BAD_RATE		= 8,
	EFR_STATUS_BAD_CLOCK		= 9,
	EFR_STATUS_BAD_CHANNEL		= 10,
	EFR_STATUS_BAD_PAN		= 11,
	EFR_STATUS_FLASH_BUSY		= 12,
	EFR_STATUS_BAD_MIRROR		= 13,
	EFR_STATUS_BAD_LED		= 14,
	EFR_STATUS_BAD_PARAMETER	= 15,
	EFR_STATUS_INCOMPLETE		= 0x80000000
};

static const char *const efr_status_names[] = {
	[EFR_STATUS_OK]			= "OK",
	[EFR_STATUS_BAD]		= "bad",
	[EFR_STATUS_BAD_COMMAND]	= "bad command",
	[EFR_STATUS_COMM_ERR]		= "comm err",
	[EFR_STATUS_BAD_QUAD_COUNT]	= "bad quad count",
	[EFR_STATUS_UNSUPPORTED]	= "unsupported",
	[EFR_STATUS_1394_TIMEOUT]	= "1394 timeout",
	[EFR_STATUS_DSP_TIMEOUT]	= "DSP timeout",
	[EFR_STATUS_BAD_RATE]		= "bad rate",
	[EFR_STATUS_BAD_CLOCK]		= "bad clock",
	[EFR_STATUS_BAD_CHANNEL]	= "bad channel",
	[EFR_STATUS_BAD_PAN]		= "bad pan",
	[EFR_STATUS_FLASH_BUSY]		= "flash busy",
	[EFR_STATUS_BAD_MIRROR]		= "bad mirror",
	[EFR_STATUS_BAD_LED]		= "bad LED",
	[EFR_STATUS_BAD_PARAMETER]	= "bad parameter",
	[EFR_STATUS_BAD_PARAMETER + 1]	= "incomplete"
};

static int
efw_transaction(struct snd_efw *efw, unsigned int category,
		unsigned int command,
		const __be32 *params, unsigned int param_bytes,
		const __be32 *resp, unsigned int resp_bytes)
{
	struct snd_efw_transaction *header;
	__be32 *buf;
	u32 seqnum;
	unsigned int buf_bytes, cmd_bytes;
	int err;

	/* calculate buffer size*/
	buf_bytes = sizeof(struct snd_efw_transaction) +
		    max(param_bytes, resp_bytes);

	/* keep buffer */
	buf = kzalloc(buf_bytes, GFP_KERNEL);
	if (buf == NULL)
		return -ENOMEM;

	/* to keep consistency of sequence number */
	spin_lock(&efw->lock);
	if ((efw->seqnum < KERNEL_SEQNUM_MIN) ||
	    (efw->seqnum >= KERNEL_SEQNUM_MAX - 2))
		efw->seqnum = KERNEL_SEQNUM_MIN;
	else
		efw->seqnum += 2;
	seqnum = efw->seqnum;
	spin_unlock(&efw->lock);

	/* fill transaction header fields */
	cmd_bytes = sizeof(struct snd_efw_transaction) + param_bytes;
	header = (struct snd_efw_transaction *)buf;
	header->length	 = cpu_to_be32(cmd_bytes / sizeof(__be32));
	header->version	 = cpu_to_be32(1);
	header->seqnum	 = cpu_to_be32(seqnum);
	header->category = cpu_to_be32(category);
	header->command	 = cpu_to_be32(command);
	header->status	 = 0;

	/* fill transaction command parameters */
	memcpy(header->params, params, param_bytes);

	err = snd_efw_transaction_run(efw->unit, buf, cmd_bytes,
				      buf, buf_bytes);
	if (err < 0)
		goto end;

	/* check transaction header fields */
	if ((be32_to_cpu(header->version) < 1) ||
	    (be32_to_cpu(header->category) != category) ||
	    (be32_to_cpu(header->command) != command) ||
	    (be32_to_cpu(header->status) != EFR_STATUS_OK)) {
		dev_err(&efw->unit->device, "EFW command failed [%u/%u]: %s\n",
			be32_to_cpu(header->category),
			be32_to_cpu(header->command),
			efr_status_names[be32_to_cpu(header->status)]);
		err = -EIO;
		goto end;
	}

	if (resp == NULL)
		goto end;

	/* fill transaction response parameters */
	memset((void *)resp, 0, resp_bytes);
	resp_bytes = min_t(unsigned int, resp_bytes,
			   be32_to_cpu(header->length) * sizeof(__be32) -
				sizeof(struct snd_efw_transaction));
	memcpy((void *)resp, &buf[6], resp_bytes);
end:
	kfree(buf);
	return err;
}

/*
 * The address in host system for transaction response is changable when the
 * device supports. struct hwinfo.flags includes its flag. The default is
 * MEMORY_SPACE_EFW_RESPONSE.
 */
int snd_efw_command_set_resp_addr(struct snd_efw *efw,
				  u16 addr_high, u32 addr_low)
{
	__be32 addr[2];

	addr[0] = cpu_to_be32(addr_high);
	addr[1] = cpu_to_be32(addr_low);

	if (!efw->resp_addr_changable)
		return -ENOSYS;

	return efw_transaction(efw, EFC_CAT_HWCTL,
			       EFC_CMD_HWINFO_SET_RESP_ADDR,
			       addr, sizeof(addr), NULL, 0);
}

/*
 * This is for timestamp processing. In Windows mode, all 32bit fields of second
 * CIP header in AMDTP transmit packet is used for 'presentation timestamp'. In
 * 'no data' packet the value of this field is 0x90ffffff.
 */
int snd_efw_command_set_tx_mode(struct snd_efw *efw,
				enum snd_efw_transport_mode mode)
{
	__be32 param = cpu_to_be32(mode);
	return efw_transaction(efw, EFC_CAT_TRANSPORT,
			       EFC_CMD_TRANSPORT_SET_TX_MODE,
			       &param, sizeof(param), NULL, 0);
}

int snd_efw_command_get_hwinfo(struct snd_efw *efw,
			       struct snd_efw_hwinfo *hwinfo)
{
	int err;

	err  = efw_transaction(efw, EFC_CAT_HWINFO,
			       EFC_CMD_HWINFO_GET_CAPS,
			       NULL, 0, (__be32 *)hwinfo, sizeof(*hwinfo));
	if (err < 0)
		goto end;

	be32_to_cpus(&hwinfo->flags);
	be32_to_cpus(&hwinfo->guid_hi);
	be32_to_cpus(&hwinfo->guid_lo);
	be32_to_cpus(&hwinfo->type);
	be32_to_cpus(&hwinfo->version);
	be32_to_cpus(&hwinfo->supported_clocks);
	be32_to_cpus(&hwinfo->amdtp_rx_pcm_channels);
	be32_to_cpus(&hwinfo->amdtp_tx_pcm_channels);
	be32_to_cpus(&hwinfo->phys_out);
	be32_to_cpus(&hwinfo->phys_in);
	be32_to_cpus(&hwinfo->phys_out_grp_count);
	be32_to_cpus(&hwinfo->phys_in_grp_count);
	be32_to_cpus(&hwinfo->midi_out_ports);
	be32_to_cpus(&hwinfo->midi_in_ports);
	be32_to_cpus(&hwinfo->max_sample_rate);
	be32_to_cpus(&hwinfo->min_sample_rate);
	be32_to_cpus(&hwinfo->dsp_version);
	be32_to_cpus(&hwinfo->arm_version);
	be32_to_cpus(&hwinfo->mixer_playback_channels);
	be32_to_cpus(&hwinfo->mixer_capture_channels);
	be32_to_cpus(&hwinfo->fpga_version);
	be32_to_cpus(&hwinfo->amdtp_rx_pcm_channels_2x);
	be32_to_cpus(&hwinfo->amdtp_tx_pcm_channels_2x);
	be32_to_cpus(&hwinfo->amdtp_rx_pcm_channels_4x);
	be32_to_cpus(&hwinfo->amdtp_tx_pcm_channels_4x);

	/* ensure terminated */
	hwinfo->vendor_name[HWINFO_NAME_SIZE_BYTES - 1] = '\0';
	hwinfo->model_name[HWINFO_NAME_SIZE_BYTES  - 1] = '\0';
end:
	return err;
}

int snd_efw_command_get_phys_meters(struct snd_efw *efw,
				    struct snd_efw_phys_meters *meters,
				    unsigned int len)
{
	u32 *buf = (u32 *)meters;
	unsigned int i;
	int err;

	err = efw_transaction(efw, EFC_CAT_HWINFO,
			      EFC_CMD_HWINFO_GET_POLLED,
			      NULL, 0, (__be32 *)meters, len);
	if (err >= 0)
		for (i = 0; i < len / sizeof(u32); i++)
			be32_to_cpus(&buf[i]);

	return err;
}

static int
command_get_clock(struct snd_efw *efw, struct efc_clock *clock)
{
	int err;

	err = efw_transaction(efw, EFC_CAT_HWCTL,
			      EFC_CMD_HWCTL_GET_CLOCK,
			      NULL, 0,
			      (__be32 *)clock, sizeof(struct efc_clock));
	if (err >= 0) {
		be32_to_cpus(&clock->source);
		be32_to_cpus(&clock->sampling_rate);
		be32_to_cpus(&clock->index);
	}

	return err;
}

/* give UINT_MAX if set nothing */
static int
command_set_clock(struct snd_efw *efw,
		  unsigned int source, unsigned int rate)
{
	struct efc_clock clock = {0};
	int err;

	/* check arguments */
	if ((source == UINT_MAX) && (rate == UINT_MAX)) {
		err = -EINVAL;
		goto end;
	}

	/* get current status */
	err = command_get_clock(efw, &clock);
	if (err < 0)
		goto end;

	/* no need */
	if ((clock.source == source) && (clock.sampling_rate == rate))
		goto end;

	/* set params */
	if ((source != UINT_MAX) && (clock.source != source))
		clock.source = source;
	if ((rate != UINT_MAX) && (clock.sampling_rate != rate))
		clock.sampling_rate = rate;
	clock.index = 0;

	cpu_to_be32s(&clock.source);
	cpu_to_be32s(&clock.sampling_rate);
	cpu_to_be32s(&clock.index);

	err = efw_transaction(efw, EFC_CAT_HWCTL,
			      EFC_CMD_HWCTL_SET_CLOCK,
			      (__be32 *)&clock, sizeof(struct efc_clock),
			      NULL, 0);
	if (err < 0)
		goto end;

	/*
	 * With firmware version 5.8, just after changing clock state, these
	 * parameters are not immediately retrieved by get command. In my
	 * trial, there needs to be 100msec to get changed parameters.
	 */
	msleep(150);
end:
	return err;
}

int snd_efw_command_get_clock_source(struct snd_efw *efw,
				     enum snd_efw_clock_source *source)
{
	int err;
	struct efc_clock clock = {0};

	err = command_get_clock(efw, &clock);
	if (err >= 0)
		*source = clock.source;

	return err;
}

int snd_efw_command_get_sampling_rate(struct snd_efw *efw, unsigned int *rate)
{
	int err;
	struct efc_clock clock = {0};

	err = command_get_clock(efw, &clock);
	if (err >= 0)
		*rate = clock.sampling_rate;

	return err;
}

int snd_efw_command_set_sampling_rate(struct snd_efw *efw, unsigned int rate)
{
	return command_set_clock(efw, UINT_MAX, rate);
}

