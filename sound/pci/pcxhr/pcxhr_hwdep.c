// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Driver for Digigram pcxhr compatible soundcards
 *
 * hwdep device manager
 *
 * Copyright (c) 2004 by Digigram <alsa@digigram.com>
 */

#include <linux/interrupt.h>
#include <linux/vmalloc.h>
#include <linux/firmware.h>
#include <linux/pci.h>
#include <linux/module.h>
#include <linux/io.h>
#include <sound/core.h>
#include <sound/hwdep.h>
#include "pcxhr.h"
#include "pcxhr_mixer.h"
#include "pcxhr_hwdep.h"
#include "pcxhr_core.h"
#include "pcxhr_mix22.h"


static int pcxhr_sub_init(struct pcxhr_mgr *mgr);
/*
 * get basic information and init pcxhr card
 */
static int pcxhr_init_board(struct pcxhr_mgr *mgr)
{
	int err;
	struct pcxhr_rmh rmh;
	int card_streams;

	/* calc the number of all streams used */
	if (mgr->mono_capture)
		card_streams = mgr->capture_chips * 2;
	else
		card_streams = mgr->capture_chips;
	card_streams += mgr->playback_chips * PCXHR_PLAYBACK_STREAMS;

	/* enable interrupts */
	pcxhr_enable_dsp(mgr);

	pcxhr_init_rmh(&rmh, CMD_SUPPORTED);
	err = pcxhr_send_msg(mgr, &rmh);
	if (err)
		return err;
	/* test 4, 8 or 12 phys out */
	if ((rmh.stat[0] & MASK_FIRST_FIELD) < mgr->playback_chips * 2)
		return -EINVAL;
	/* test 4, 8 or 2 phys in */
	if (((rmh.stat[0] >> (2 * FIELD_SIZE)) & MASK_FIRST_FIELD) <
	    mgr->capture_chips * 2)
		return -EINVAL;
	/* test max nb substream per board */
	if ((rmh.stat[1] & 0x5F) < card_streams)
		return -EINVAL;
	/* test max nb substream per pipe */
	if (((rmh.stat[1] >> 7) & 0x5F) < PCXHR_PLAYBACK_STREAMS)
		return -EINVAL;
	dev_dbg(&mgr->pci->dev,
		"supported formats : playback=%x capture=%x\n",
		    rmh.stat[2], rmh.stat[3]);

	pcxhr_init_rmh(&rmh, CMD_VERSION);
	/* firmware num for DSP */
	rmh.cmd[0] |= mgr->firmware_num;
	/* transfer granularity in samples (should be multiple of 48) */
	rmh.cmd[1] = (1<<23) + mgr->granularity;
	rmh.cmd_len = 2;
	err = pcxhr_send_msg(mgr, &rmh);
	if (err)
		return err;
	dev_dbg(&mgr->pci->dev,
		"PCXHR DSP version is %d.%d.%d\n", (rmh.stat[0]>>16)&0xff,
		    (rmh.stat[0]>>8)&0xff, rmh.stat[0]&0xff);
	mgr->dsp_version = rmh.stat[0];

	if (mgr->is_hr_stereo)
		err = hr222_sub_init(mgr);
	else
		err = pcxhr_sub_init(mgr);
	return err;
}

static int pcxhr_sub_init(struct pcxhr_mgr *mgr)
{
	int err;
	struct pcxhr_rmh rmh;

	/* get options */
	pcxhr_init_rmh(&rmh, CMD_ACCESS_IO_READ);
	rmh.cmd[0] |= IO_NUM_REG_STATUS;
	rmh.cmd[1]  = REG_STATUS_OPTIONS;
	rmh.cmd_len = 2;
	err = pcxhr_send_msg(mgr, &rmh);
	if (err)
		return err;

	if ((rmh.stat[1] & REG_STATUS_OPT_DAUGHTER_MASK) ==
	    REG_STATUS_OPT_ANALOG_BOARD)
		mgr->board_has_analog = 1;	/* analog addon board found */

	/* unmute inputs */
	err = pcxhr_write_io_num_reg_cont(mgr, REG_CONT_UNMUTE_INPUTS,
					  REG_CONT_UNMUTE_INPUTS, NULL);
	if (err)
		return err;
	/* unmute outputs (a write to IO_NUM_REG_MUTE_OUT mutes!) */
	pcxhr_init_rmh(&rmh, CMD_ACCESS_IO_READ);
	rmh.cmd[0] |= IO_NUM_REG_MUTE_OUT;
	if (DSP_EXT_CMD_SET(mgr)) {
		rmh.cmd[1]  = 1;	/* unmute digital plugs */
		rmh.cmd_len = 2;
	}
	err = pcxhr_send_msg(mgr, &rmh);
	return err;
}

void pcxhr_reset_board(struct pcxhr_mgr *mgr)
{
	struct pcxhr_rmh rmh;

	if (mgr->dsp_loaded & (1 << PCXHR_FIRMWARE_DSP_MAIN_INDEX)) {
		/* mute outputs */
	    if (!mgr->is_hr_stereo) {
		/* a read to IO_NUM_REG_MUTE_OUT register unmutes! */
		pcxhr_init_rmh(&rmh, CMD_ACCESS_IO_WRITE);
		rmh.cmd[0] |= IO_NUM_REG_MUTE_OUT;
		pcxhr_send_msg(mgr, &rmh);
		/* mute inputs */
		pcxhr_write_io_num_reg_cont(mgr, REG_CONT_UNMUTE_INPUTS,
					    0, NULL);
	    }
		/* stereo cards mute with reset of dsp */
	}
	/* reset pcxhr dsp */
	if (mgr->dsp_loaded & (1 << PCXHR_FIRMWARE_DSP_EPRM_INDEX))
		pcxhr_reset_dsp(mgr);
	/* reset second xilinx */
	if (mgr->dsp_loaded & (1 << PCXHR_FIRMWARE_XLX_COM_INDEX)) {
		pcxhr_reset_xilinx_com(mgr);
		mgr->dsp_loaded = 1;
	}
	return;
}


/*
 *  allocate a playback/capture pipe (pcmp0/pcmc0)
 */
static int pcxhr_dsp_allocate_pipe(struct pcxhr_mgr *mgr,
				   struct pcxhr_pipe *pipe,
				   int is_capture, int pin)
{
	int stream_count, audio_count;
	int err;
	struct pcxhr_rmh rmh;

	if (is_capture) {
		stream_count = 1;
		if (mgr->mono_capture)
			audio_count = 1;
		else
			audio_count = 2;
	} else {
		stream_count = PCXHR_PLAYBACK_STREAMS;
		audio_count = 2;	/* always stereo */
	}
	dev_dbg(&mgr->pci->dev, "snd_add_ref_pipe pin(%d) pcm%c0\n",
		    pin, is_capture ? 'c' : 'p');
	pipe->is_capture = is_capture;
	pipe->first_audio = pin;
	/* define pipe (P_PCM_ONLY_MASK (0x020000) is not necessary) */
	pcxhr_init_rmh(&rmh, CMD_RES_PIPE);
	pcxhr_set_pipe_cmd_params(&rmh, is_capture, pin,
				  audio_count, stream_count);
	rmh.cmd[1] |= 0x020000; /* add P_PCM_ONLY_MASK */
	if (DSP_EXT_CMD_SET(mgr)) {
		/* add channel mask to command */
	  rmh.cmd[rmh.cmd_len++] = (audio_count == 1) ? 0x01 : 0x03;
	}
	err = pcxhr_send_msg(mgr, &rmh);
	if (err < 0) {
		dev_err(&mgr->pci->dev, "error pipe allocation "
			   "(CMD_RES_PIPE) err=%x!\n", err);
		return err;
	}
	pipe->status = PCXHR_PIPE_DEFINED;

	return 0;
}

/*
 *  free playback/capture pipe (pcmp0/pcmc0)
 */
#if 0
static int pcxhr_dsp_free_pipe( struct pcxhr_mgr *mgr, struct pcxhr_pipe *pipe)
{
	struct pcxhr_rmh rmh;
	int capture_mask = 0;
	int playback_mask = 0;
	int err = 0;

	if (pipe->is_capture)
		capture_mask  = (1 << pipe->first_audio);
	else
		playback_mask = (1 << pipe->first_audio);

	/* stop one pipe */
	err = pcxhr_set_pipe_state(mgr, playback_mask, capture_mask, 0);
	if (err < 0)
		dev_err(&mgr->pci->dev, "error stopping pipe!\n");
	/* release the pipe */
	pcxhr_init_rmh(&rmh, CMD_FREE_PIPE);
	pcxhr_set_pipe_cmd_params(&rmh, pipe->is_capture, pipe->first_audio,
				  0, 0);
	err = pcxhr_send_msg(mgr, &rmh);
	if (err < 0)
		dev_err(&mgr->pci->dev, "error pipe release "
			   "(CMD_FREE_PIPE) err(%x)\n", err);
	pipe->status = PCXHR_PIPE_UNDEFINED;
	return err;
}
#endif


static int pcxhr_config_pipes(struct pcxhr_mgr *mgr)
{
	int err, i, j;
	struct snd_pcxhr *chip;
	struct pcxhr_pipe *pipe;

	/* allocate the pipes on the dsp */
	for (i = 0; i < mgr->num_cards; i++) {
		chip = mgr->chip[i];
		if (chip->nb_streams_play) {
			pipe = &chip->playback_pipe;
			err = pcxhr_dsp_allocate_pipe( mgr, pipe, 0, i*2);
			if (err)
				return err;
			for(j = 0; j < chip->nb_streams_play; j++)
				chip->playback_stream[j].pipe = pipe;
		}
		for (j = 0; j < chip->nb_streams_capt; j++) {
			pipe = &chip->capture_pipe[j];
			err = pcxhr_dsp_allocate_pipe(mgr, pipe, 1, i*2 + j);
			if (err)
				return err;
			chip->capture_stream[j].pipe = pipe;
		}
	}
	return 0;
}

static int pcxhr_start_pipes(struct pcxhr_mgr *mgr)
{
	int i, j;
	struct snd_pcxhr *chip;
	int playback_mask = 0;
	int capture_mask = 0;

	/* start all the pipes on the dsp */
	for (i = 0; i < mgr->num_cards; i++) {
		chip = mgr->chip[i];
		if (chip->nb_streams_play)
			playback_mask |= 1 << chip->playback_pipe.first_audio;
		for (j = 0; j < chip->nb_streams_capt; j++)
			capture_mask |= 1 << chip->capture_pipe[j].first_audio;
	}
	return pcxhr_set_pipe_state(mgr, playback_mask, capture_mask, 1);
}


static int pcxhr_dsp_load(struct pcxhr_mgr *mgr, int index,
			  const struct firmware *dsp)
{
	int err, card_index;

	dev_dbg(&mgr->pci->dev,
		"loading dsp [%d] size = %zd\n", index, dsp->size);

	switch (index) {
	case PCXHR_FIRMWARE_XLX_INT_INDEX:
		pcxhr_reset_xilinx_com(mgr);
		return pcxhr_load_xilinx_binary(mgr, dsp, 0);

	case PCXHR_FIRMWARE_XLX_COM_INDEX:
		pcxhr_reset_xilinx_com(mgr);
		return pcxhr_load_xilinx_binary(mgr, dsp, 1);

	case PCXHR_FIRMWARE_DSP_EPRM_INDEX:
		pcxhr_reset_dsp(mgr);
		return pcxhr_load_eeprom_binary(mgr, dsp);

	case PCXHR_FIRMWARE_DSP_BOOT_INDEX:
		return pcxhr_load_boot_binary(mgr, dsp);

	case PCXHR_FIRMWARE_DSP_MAIN_INDEX:
		err = pcxhr_load_dsp_binary(mgr, dsp);
		if (err)
			return err;
		break;	/* continue with first init */
	default:
		dev_err(&mgr->pci->dev, "wrong file index\n");
		return -EFAULT;
	} /* end of switch file index*/

	/* first communication with embedded */
	err = pcxhr_init_board(mgr);
        if (err < 0) {
		dev_err(&mgr->pci->dev, "pcxhr could not be set up\n");
		return err;
	}
	err = pcxhr_config_pipes(mgr);
        if (err < 0) {
		dev_err(&mgr->pci->dev, "pcxhr pipes could not be set up\n");
		return err;
	}
       	/* create devices and mixer in accordance with HW options*/
        for (card_index = 0; card_index < mgr->num_cards; card_index++) {
		struct snd_pcxhr *chip = mgr->chip[card_index];

		if ((err = pcxhr_create_pcm(chip)) < 0)
			return err;

		if (card_index == 0) {
			if ((err = pcxhr_create_mixer(chip->mgr)) < 0)
				return err;
		}
		if ((err = snd_card_register(chip->card)) < 0)
			return err;
	}
	err = pcxhr_start_pipes(mgr);
        if (err < 0) {
		dev_err(&mgr->pci->dev, "pcxhr pipes could not be started\n");
		return err;
	}
	dev_dbg(&mgr->pci->dev,
		"pcxhr firmware downloaded and successfully set up\n");

	return 0;
}

/*
 * fw loader entry
 */
int pcxhr_setup_firmware(struct pcxhr_mgr *mgr)
{
	static char *fw_files[][5] = {
	[0] = { "xlxint.dat", "xlxc882hr.dat",
		"dspe882.e56", "dspb882hr.b56", "dspd882.d56" },
	[1] = { "xlxint.dat", "xlxc882e.dat",
		"dspe882.e56", "dspb882e.b56", "dspd882.d56" },
	[2] = { "xlxint.dat", "xlxc1222hr.dat",
		"dspe882.e56", "dspb1222hr.b56", "dspd1222.d56" },
	[3] = { "xlxint.dat", "xlxc1222e.dat",
		"dspe882.e56", "dspb1222e.b56", "dspd1222.d56" },
	[4] = { NULL, "xlxc222.dat",
		"dspe924.e56", "dspb924.b56", "dspd222.d56" },
	[5] = { NULL, "xlxc924.dat",
		"dspe924.e56", "dspb924.b56", "dspd222.d56" },
	};
	char path[32];

	const struct firmware *fw_entry;
	int i, err;
	int fw_set = mgr->fw_file_set;

	for (i = 0; i < 5; i++) {
		if (!fw_files[fw_set][i])
			continue;
		sprintf(path, "pcxhr/%s", fw_files[fw_set][i]);
		if (request_firmware(&fw_entry, path, &mgr->pci->dev)) {
			dev_err(&mgr->pci->dev,
				"pcxhr: can't load firmware %s\n",
				   path);
			return -ENOENT;
		}
		/* fake hwdep dsp record */
		err = pcxhr_dsp_load(mgr, i, fw_entry);
		release_firmware(fw_entry);
		if (err < 0)
			return err;
		mgr->dsp_loaded |= 1 << i;
	}
	return 0;
}

MODULE_FIRMWARE("pcxhr/xlxint.dat");
MODULE_FIRMWARE("pcxhr/xlxc882hr.dat");
MODULE_FIRMWARE("pcxhr/xlxc882e.dat");
MODULE_FIRMWARE("pcxhr/dspe882.e56");
MODULE_FIRMWARE("pcxhr/dspb882hr.b56");
MODULE_FIRMWARE("pcxhr/dspb882e.b56");
MODULE_FIRMWARE("pcxhr/dspd882.d56");

MODULE_FIRMWARE("pcxhr/xlxc1222hr.dat");
MODULE_FIRMWARE("pcxhr/xlxc1222e.dat");
MODULE_FIRMWARE("pcxhr/dspb1222hr.b56");
MODULE_FIRMWARE("pcxhr/dspb1222e.b56");
MODULE_FIRMWARE("pcxhr/dspd1222.d56");

MODULE_FIRMWARE("pcxhr/xlxc222.dat");
MODULE_FIRMWARE("pcxhr/xlxc924.dat");
MODULE_FIRMWARE("pcxhr/dspe924.e56");
MODULE_FIRMWARE("pcxhr/dspb924.b56");
MODULE_FIRMWARE("pcxhr/dspd222.d56");
