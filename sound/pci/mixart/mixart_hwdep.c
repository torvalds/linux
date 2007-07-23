/*
 * Driver for Digigram miXart soundcards
 *
 * DSP firmware management
 *
 * Copyright (c) 2003 by Digigram <alsa@digigram.com>
 *
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation; either version 2 of the License, or
 *   (at your option) any later version.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with this program; if not, write to the Free Software
 *   Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307 USA
 */

#include <sound/driver.h>
#include <linux/interrupt.h>
#include <linux/pci.h>
#include <linux/firmware.h>
#include <linux/vmalloc.h>
#include <asm/io.h>
#include <sound/core.h>
#include "mixart.h"
#include "mixart_mixer.h"
#include "mixart_core.h"
#include "mixart_hwdep.h"


/**
 * wait for a value on a peudo register, exit with a timeout
 *
 * @param mgr pointer to miXart manager structure
 * @param offset unsigned pseudo_register base + offset of value
 * @param value value
 * @param timeout timeout in centisenconds
 */
static int mixart_wait_nice_for_register_value(struct mixart_mgr *mgr,
					       u32 offset, int is_egal,
					       u32 value, unsigned long timeout)
{
	unsigned long end_time = jiffies + (timeout * HZ / 100);
	u32 read;

	do {	/* we may take too long time in this loop.
		 * so give controls back to kernel if needed.
		 */
		cond_resched();

		read = readl_be( MIXART_MEM( mgr, offset ));
		if(is_egal) {
			if(read == value) return 0;
		}
		else { /* wait for different value */
			if(read != value) return 0;
		}
	} while ( time_after_eq(end_time, jiffies) );

	return -EBUSY;
}


/*
  structures needed to upload elf code packets 
 */
struct snd_mixart_elf32_ehdr {
	u8      e_ident[16];
	u16     e_type;
	u16     e_machine;
	u32     e_version;
	u32     e_entry;
	u32     e_phoff;
	u32     e_shoff;
	u32     e_flags;
	u16     e_ehsize;
	u16     e_phentsize;
	u16     e_phnum;
	u16     e_shentsize;
	u16     e_shnum;
	u16     e_shstrndx;
};

struct snd_mixart_elf32_phdr {
	u32     p_type;
	u32     p_offset;
	u32     p_vaddr;
	u32     p_paddr;
	u32     p_filesz;
	u32     p_memsz;
	u32     p_flags;
	u32     p_align;
};

static int mixart_load_elf(struct mixart_mgr *mgr, const struct firmware *dsp )
{
	char                    elf32_magic_number[4] = {0x7f,'E','L','F'};
	struct snd_mixart_elf32_ehdr *elf_header;
	int                     i;

	elf_header = (struct snd_mixart_elf32_ehdr *)dsp->data;
	for( i=0; i<4; i++ )
		if ( elf32_magic_number[i] != elf_header->e_ident[i] )
			return -EINVAL;

	if( elf_header->e_phoff != 0 ) {
		struct snd_mixart_elf32_phdr     elf_programheader;

		for( i=0; i < be16_to_cpu(elf_header->e_phnum); i++ ) {
			u32 pos = be32_to_cpu(elf_header->e_phoff) + (u32)(i * be16_to_cpu(elf_header->e_phentsize));

			memcpy( &elf_programheader, dsp->data + pos, sizeof(elf_programheader) );

			if(elf_programheader.p_type != 0) {
				if( elf_programheader.p_filesz != 0 ) {
					memcpy_toio( MIXART_MEM( mgr, be32_to_cpu(elf_programheader.p_vaddr)),
						     dsp->data + be32_to_cpu( elf_programheader.p_offset ),
						     be32_to_cpu( elf_programheader.p_filesz ));
				}
			}
		}
	}
	return 0;
}

/*
 * get basic information and init miXart
 */

/* audio IDs for request to the board */
#define MIXART_FIRST_ANA_AUDIO_ID       0
#define MIXART_FIRST_DIG_AUDIO_ID       8

static int mixart_enum_connectors(struct mixart_mgr *mgr)
{
	u32 k;
	int err;
	struct mixart_msg request;
	struct mixart_enum_connector_resp *connector;
	struct mixart_audio_info_req  *audio_info_req;
	struct mixart_audio_info_resp *audio_info;

	connector = kmalloc(sizeof(*connector), GFP_KERNEL);
	audio_info_req = kmalloc(sizeof(*audio_info_req), GFP_KERNEL);
	audio_info = kmalloc(sizeof(*audio_info), GFP_KERNEL);
	if (! connector || ! audio_info_req || ! audio_info) {
		err = -ENOMEM;
		goto __error;
	}

	audio_info_req->line_max_level = MIXART_FLOAT_P_22_0_TO_HEX;
	audio_info_req->micro_max_level = MIXART_FLOAT_M_20_0_TO_HEX;
	audio_info_req->cd_max_level = MIXART_FLOAT____0_0_TO_HEX;

	request.message_id = MSG_SYSTEM_ENUM_PLAY_CONNECTOR;
	request.uid = (struct mixart_uid){0,0};  /* board num = 0 */
	request.data = NULL;
	request.size = 0;

	err = snd_mixart_send_msg(mgr, &request, sizeof(*connector), connector);
	if((err < 0) || (connector->error_code) || (connector->uid_count > MIXART_MAX_PHYS_CONNECTORS)) {
		snd_printk(KERN_ERR "error MSG_SYSTEM_ENUM_PLAY_CONNECTOR\n");
		err = -EINVAL;
		goto __error;
	}

	for(k=0; k < connector->uid_count; k++) {
		struct mixart_pipe *pipe;

		if(k < MIXART_FIRST_DIG_AUDIO_ID) {
			pipe = &mgr->chip[k/2]->pipe_out_ana;
		} else {
			pipe = &mgr->chip[(k-MIXART_FIRST_DIG_AUDIO_ID)/2]->pipe_out_dig;
		}
		if(k & 1) {
			pipe->uid_right_connector = connector->uid[k];   /* odd */
		} else {
			pipe->uid_left_connector = connector->uid[k];    /* even */
		}

		/* snd_printk(KERN_DEBUG "playback connector[%d].object_id = %x\n", k, connector->uid[k].object_id); */

		/* TODO: really need send_msg MSG_CONNECTOR_GET_AUDIO_INFO for each connector ? perhaps for analog level caps ? */
		request.message_id = MSG_CONNECTOR_GET_AUDIO_INFO;
		request.uid = connector->uid[k];
		request.data = audio_info_req;
		request.size = sizeof(*audio_info_req);

		err = snd_mixart_send_msg(mgr, &request, sizeof(*audio_info), audio_info);
		if( err < 0 ) {
			snd_printk(KERN_ERR "error MSG_CONNECTOR_GET_AUDIO_INFO\n");
			goto __error;
		}
		/*snd_printk(KERN_DEBUG "play  analog_info.analog_level_present = %x\n", audio_info->info.analog_info.analog_level_present);*/
	}

	request.message_id = MSG_SYSTEM_ENUM_RECORD_CONNECTOR;
	request.uid = (struct mixart_uid){0,0};  /* board num = 0 */
	request.data = NULL;
	request.size = 0;

	err = snd_mixart_send_msg(mgr, &request, sizeof(*connector), connector);
	if((err < 0) || (connector->error_code) || (connector->uid_count > MIXART_MAX_PHYS_CONNECTORS)) {
		snd_printk(KERN_ERR "error MSG_SYSTEM_ENUM_RECORD_CONNECTOR\n");
		err = -EINVAL;
		goto __error;
	}

	for(k=0; k < connector->uid_count; k++) {
		struct mixart_pipe *pipe;

		if(k < MIXART_FIRST_DIG_AUDIO_ID) {
			pipe = &mgr->chip[k/2]->pipe_in_ana;
		} else {
			pipe = &mgr->chip[(k-MIXART_FIRST_DIG_AUDIO_ID)/2]->pipe_in_dig;
		}
		if(k & 1) {
			pipe->uid_right_connector = connector->uid[k];   /* odd */
		} else {
			pipe->uid_left_connector = connector->uid[k];    /* even */
		}

		/* snd_printk(KERN_DEBUG "capture connector[%d].object_id = %x\n", k, connector->uid[k].object_id); */

		/* TODO: really need send_msg MSG_CONNECTOR_GET_AUDIO_INFO for each connector ? perhaps for analog level caps ? */
		request.message_id = MSG_CONNECTOR_GET_AUDIO_INFO;
		request.uid = connector->uid[k];
		request.data = audio_info_req;
		request.size = sizeof(*audio_info_req);

		err = snd_mixart_send_msg(mgr, &request, sizeof(*audio_info), audio_info);
		if( err < 0 ) {
			snd_printk(KERN_ERR "error MSG_CONNECTOR_GET_AUDIO_INFO\n");
			goto __error;
		}
		/*snd_printk(KERN_DEBUG "rec  analog_info.analog_level_present = %x\n", audio_info->info.analog_info.analog_level_present);*/
	}
	err = 0;

 __error:
	kfree(connector);
	kfree(audio_info_req);
	kfree(audio_info);

	return err;
}

static int mixart_enum_physio(struct mixart_mgr *mgr)
{
	u32 k;
	int err;
	struct mixart_msg request;
	struct mixart_uid get_console_mgr;
	struct mixart_return_uid console_mgr;
	struct mixart_uid_enumeration phys_io;

	/* get the uid for the console manager */
	get_console_mgr.object_id = 0;
	get_console_mgr.desc = MSG_CONSOLE_MANAGER | 0; /* cardindex = 0 */

	request.message_id = MSG_CONSOLE_GET_CLOCK_UID;
	request.uid = get_console_mgr;
	request.data = &get_console_mgr;
	request.size = sizeof(get_console_mgr);

	err = snd_mixart_send_msg(mgr, &request, sizeof(console_mgr), &console_mgr);

	if( (err < 0) || (console_mgr.error_code != 0) ) {
		snd_printk(KERN_DEBUG "error MSG_CONSOLE_GET_CLOCK_UID : err=%x\n", console_mgr.error_code);
		return -EINVAL;
	}

	/* used later for clock issues ! */
	mgr->uid_console_manager = console_mgr.uid;

	request.message_id = MSG_SYSTEM_ENUM_PHYSICAL_IO;
	request.uid = (struct mixart_uid){0,0};
	request.data = &console_mgr.uid;
	request.size = sizeof(console_mgr.uid);

	err = snd_mixart_send_msg(mgr, &request, sizeof(phys_io), &phys_io);
	if( (err < 0) || ( phys_io.error_code != 0 ) ) {
		snd_printk(KERN_ERR "error MSG_SYSTEM_ENUM_PHYSICAL_IO err(%x) error_code(%x)\n", err, phys_io.error_code );
		return -EINVAL;
	}

	snd_assert(phys_io.nb_uid >= (MIXART_MAX_CARDS * 2),  return -EINVAL); /* min 2 phys io per card (analog in + analog out) */

	for(k=0; k<mgr->num_cards; k++) {
		mgr->chip[k]->uid_in_analog_physio = phys_io.uid[k];
		mgr->chip[k]->uid_out_analog_physio = phys_io.uid[phys_io.nb_uid/2 + k]; 
	}

	return 0;
}


static int mixart_first_init(struct mixart_mgr *mgr)
{
	u32 k;
	int err;
	struct mixart_msg request;

	if((err = mixart_enum_connectors(mgr)) < 0) return err;

	if((err = mixart_enum_physio(mgr)) < 0) return err;

	/* send a synchro command to card (necessary to do this before first MSG_STREAM_START_STREAM_GRP_PACKET) */
	/* though why not here */
	request.message_id = MSG_SYSTEM_SEND_SYNCHRO_CMD;
	request.uid = (struct mixart_uid){0,0};
	request.data = NULL;
	request.size = 0;
	/* this command has no data. response is a 32 bit status */
	err = snd_mixart_send_msg(mgr, &request, sizeof(k), &k);
	if( (err < 0) || (k != 0) ) {
		snd_printk(KERN_ERR "error MSG_SYSTEM_SEND_SYNCHRO_CMD\n");
		return err == 0 ? -EINVAL : err;
	}

	return 0;
}


/* firmware base addresses (when hard coded) */
#define MIXART_MOTHERBOARD_XLX_BASE_ADDRESS   0x00600000

static int mixart_dsp_load(struct mixart_mgr* mgr, int index, const struct firmware *dsp)
{
	int           err, card_index;
	u32           status_xilinx, status_elf, status_daught;
	u32           val;

	/* read motherboard xilinx status */
	status_xilinx = readl_be( MIXART_MEM( mgr,MIXART_PSEUDOREG_MXLX_STATUS_OFFSET ));
	/* read elf status */
	status_elf = readl_be( MIXART_MEM( mgr,MIXART_PSEUDOREG_ELF_STATUS_OFFSET ));
	/* read daughterboard xilinx status */
	status_daught = readl_be( MIXART_MEM( mgr,MIXART_PSEUDOREG_DXLX_STATUS_OFFSET ));

	/* motherboard xilinx status 5 will say that the board is performing a reset */
	if( status_xilinx == 5 ) {
		snd_printk( KERN_ERR "miXart is resetting !\n");
		return -EAGAIN; /* try again later */
	}

	switch (index)   {
	case MIXART_MOTHERBOARD_XLX_INDEX:

		/* xilinx already loaded ? */ 
		if( status_xilinx == 4 ) {
			snd_printk( KERN_DEBUG "xilinx is already loaded !\n");
			return 0;
		}
		/* the status should be 0 == "idle" */
		if( status_xilinx != 0 ) {
			snd_printk( KERN_ERR "xilinx load error ! status = %d\n", status_xilinx);
			return -EIO; /* modprob -r may help ? */
		}

		/* check xilinx validity */
		snd_assert(((u32*)(dsp->data))[0]==0xFFFFFFFF, return -EINVAL);
		snd_assert(dsp->size % 4 == 0, return -EINVAL);

		/* set xilinx status to copying */
		writel_be( 1, MIXART_MEM( mgr, MIXART_PSEUDOREG_MXLX_STATUS_OFFSET ));

		/* setup xilinx base address */
		writel_be( MIXART_MOTHERBOARD_XLX_BASE_ADDRESS, MIXART_MEM( mgr,MIXART_PSEUDOREG_MXLX_BASE_ADDR_OFFSET ));
		/* setup code size for xilinx file */
		writel_be( dsp->size, MIXART_MEM( mgr, MIXART_PSEUDOREG_MXLX_SIZE_OFFSET ));

		/* copy xilinx code */
		memcpy_toio(  MIXART_MEM( mgr, MIXART_MOTHERBOARD_XLX_BASE_ADDRESS),  dsp->data,  dsp->size);
    
		/* set xilinx status to copy finished */
		writel_be( 2, MIXART_MEM( mgr, MIXART_PSEUDOREG_MXLX_STATUS_OFFSET ));

		/* return, because no further processing needed */
		return 0;

	case MIXART_MOTHERBOARD_ELF_INDEX:

		if( status_elf == 4 ) {
			snd_printk( KERN_DEBUG "elf file already loaded !\n");
			return 0;
		}

		/* the status should be 0 == "idle" */
		if( status_elf != 0 ) {
			snd_printk( KERN_ERR "elf load error ! status = %d\n", status_elf);
			return -EIO; /* modprob -r may help ? */
		}

		/* wait for xilinx status == 4 */
		err = mixart_wait_nice_for_register_value( mgr, MIXART_PSEUDOREG_MXLX_STATUS_OFFSET, 1, 4, 500); /* 5sec */
		if (err < 0) {
			snd_printk( KERN_ERR "xilinx was not loaded or could not be started\n");
			return err;
		}

		/* init some data on the card */
		writel_be( 0, MIXART_MEM( mgr, MIXART_PSEUDOREG_BOARDNUMBER ) ); /* set miXart boardnumber to 0 */
		writel_be( 0, MIXART_MEM( mgr, MIXART_FLOWTABLE_PTR ) );         /* reset pointer to flow table on miXart */

		/* set elf status to copying */
		writel_be( 1, MIXART_MEM( mgr, MIXART_PSEUDOREG_ELF_STATUS_OFFSET ));

		/* process the copying of the elf packets */
		err = mixart_load_elf( mgr, dsp );
		if (err < 0) return err;

		/* set elf status to copy finished */
		writel_be( 2, MIXART_MEM( mgr, MIXART_PSEUDOREG_ELF_STATUS_OFFSET ));

		/* wait for elf status == 4 */
		err = mixart_wait_nice_for_register_value( mgr, MIXART_PSEUDOREG_ELF_STATUS_OFFSET, 1, 4, 300); /* 3sec */
		if (err < 0) {
			snd_printk( KERN_ERR "elf could not be started\n");
			return err;
		}

		/* miXart waits at this point on the pointer to the flow table */
		writel_be( (u32)mgr->flowinfo.addr, MIXART_MEM( mgr, MIXART_FLOWTABLE_PTR ) ); /* give pointer of flow table to miXart */

		return 0;  /* return, another xilinx file has to be loaded before */

	case MIXART_AESEBUBOARD_XLX_INDEX:
	default:

		/* elf and xilinx should be loaded */
		if( (status_elf != 4) || (status_xilinx != 4) ) {
			printk( KERN_ERR "xilinx or elf not successfully loaded\n");
			return -EIO; /* modprob -r may help ? */
		}

		/* wait for daughter detection != 0 */
		err = mixart_wait_nice_for_register_value( mgr, MIXART_PSEUDOREG_DBRD_PRESENCE_OFFSET, 0, 0, 30); /* 300msec */
		if (err < 0) {
			snd_printk( KERN_ERR "error starting elf file\n");
			return err;
		}

		/* the board type can now be retrieved */
		mgr->board_type = (DAUGHTER_TYPE_MASK & readl_be( MIXART_MEM( mgr, MIXART_PSEUDOREG_DBRD_TYPE_OFFSET)));

		if (mgr->board_type == MIXART_DAUGHTER_TYPE_NONE)
			break;  /* no daughter board; the file does not have to be loaded, continue after the switch */

		/* only if aesebu daughter board presence (elf code must run)  */ 
		if (mgr->board_type != MIXART_DAUGHTER_TYPE_AES )
			return -EINVAL;

		/* daughter should be idle */
		if( status_daught != 0 ) {
			printk( KERN_ERR "daughter load error ! status = %d\n", status_daught);
			return -EIO; /* modprob -r may help ? */
		}
 
		/* check daughterboard xilinx validity */
		snd_assert(((u32*)(dsp->data))[0]==0xFFFFFFFF, return -EINVAL);
		snd_assert(dsp->size % 4 == 0, return -EINVAL);

		/* inform mixart about the size of the file */
		writel_be( dsp->size, MIXART_MEM( mgr, MIXART_PSEUDOREG_DXLX_SIZE_OFFSET ));

		/* set daughterboard status to 1 */
		writel_be( 1, MIXART_MEM( mgr, MIXART_PSEUDOREG_DXLX_STATUS_OFFSET ));

		/* wait for status == 2 */
		err = mixart_wait_nice_for_register_value( mgr, MIXART_PSEUDOREG_DXLX_STATUS_OFFSET, 1, 2, 30); /* 300msec */
		if (err < 0) {
			snd_printk( KERN_ERR "daughter board load error\n");
			return err;
		}

		/* get the address where to write the file */
		val = readl_be( MIXART_MEM( mgr, MIXART_PSEUDOREG_DXLX_BASE_ADDR_OFFSET ));
		snd_assert(val != 0, return -EINVAL);

		/* copy daughterboard xilinx code */
		memcpy_toio(  MIXART_MEM( mgr, val),  dsp->data,  dsp->size);

		/* set daughterboard status to 4 */
		writel_be( 4, MIXART_MEM( mgr, MIXART_PSEUDOREG_DXLX_STATUS_OFFSET ));

		/* continue with init */
		break;
	} /* end of switch file index*/

        /* wait for daughter status == 3 */
        err = mixart_wait_nice_for_register_value( mgr, MIXART_PSEUDOREG_DXLX_STATUS_OFFSET, 1, 3, 300); /* 3sec */
        if (err < 0) {
		snd_printk( KERN_ERR "daughter board could not be initialised\n");
		return err;
	}

	/* init mailbox (communication with embedded) */
	snd_mixart_init_mailbox(mgr);

	/* first communication with embedded */
	err = mixart_first_init(mgr);
        if (err < 0) {
		snd_printk( KERN_ERR "miXart could not be set up\n");
		return err;
	}

       	/* create devices and mixer in accordance with HW options*/
        for (card_index = 0; card_index < mgr->num_cards; card_index++) {
		struct snd_mixart *chip = mgr->chip[card_index];

		if ((err = snd_mixart_create_pcm(chip)) < 0)
			return err;

		if (card_index == 0) {
			if ((err = snd_mixart_create_mixer(chip->mgr)) < 0)
	        		return err;
		}

		if ((err = snd_card_register(chip->card)) < 0)
			return err;
	};

	snd_printdd("miXart firmware downloaded and successfully set up\n");

	return 0;
}


#if defined(CONFIG_FW_LOADER) || defined(CONFIG_FW_LOADER_MODULE)
#if !defined(CONFIG_USE_MIXARTLOADER) && !defined(CONFIG_SND_MIXART) /* built-in kernel */
#define SND_MIXART_FW_LOADER	/* use the standard firmware loader */
#endif
#endif

#ifdef SND_MIXART_FW_LOADER

int snd_mixart_setup_firmware(struct mixart_mgr *mgr)
{
	static char *fw_files[3] = {
		"miXart8.xlx", "miXart8.elf", "miXart8AES.xlx"
	};
	char path[32];

	const struct firmware *fw_entry;
	int i, err;

	for (i = 0; i < 3; i++) {
		sprintf(path, "mixart/%s", fw_files[i]);
		if (request_firmware(&fw_entry, path, &mgr->pci->dev)) {
			snd_printk(KERN_ERR "miXart: can't load firmware %s\n", path);
			return -ENOENT;
		}
		/* fake hwdep dsp record */
		err = mixart_dsp_load(mgr, i, fw_entry);
		release_firmware(fw_entry);
		if (err < 0)
			return err;
		mgr->dsp_loaded |= 1 << i;
	}
	return 0;
}

MODULE_FIRMWARE("mixart/miXart8.xlx");
MODULE_FIRMWARE("mixart/miXart8.elf");
MODULE_FIRMWARE("mixart/miXart8AES.xlx");

#else /* old style firmware loading */

/* miXart hwdep interface id string */
#define SND_MIXART_HWDEP_ID       "miXart Loader"

static int mixart_hwdep_open(struct snd_hwdep *hw, struct file *file)
{
	return 0;
}

static int mixart_hwdep_release(struct snd_hwdep *hw, struct file *file)
{
	return 0;
}

static int mixart_hwdep_dsp_status(struct snd_hwdep *hw,
				   struct snd_hwdep_dsp_status *info)
{
	struct mixart_mgr *mgr = hw->private_data;

	strcpy(info->id, "miXart");
        info->num_dsps = MIXART_HARDW_FILES_MAX_INDEX;

	if (mgr->dsp_loaded & (1 <<  MIXART_MOTHERBOARD_ELF_INDEX))
		info->chip_ready = 1;

	info->version = MIXART_DRIVER_VERSION;
	return 0;
}

static int mixart_hwdep_dsp_load(struct snd_hwdep *hw,
				 struct snd_hwdep_dsp_image *dsp)
{
	struct mixart_mgr* mgr = hw->private_data;
	struct firmware fw;
	int err;

	fw.size = dsp->length;
	fw.data = vmalloc(dsp->length);
	if (! fw.data) {
		snd_printk(KERN_ERR "miXart: cannot allocate image size %d\n",
			   (int)dsp->length);
		return -ENOMEM;
	}
	if (copy_from_user(fw.data, dsp->image, dsp->length)) {
		vfree(fw.data);
		return -EFAULT;
	}
	err = mixart_dsp_load(mgr, dsp->index, &fw);
	vfree(fw.data);
	if (err < 0)
		return err;
	mgr->dsp_loaded |= 1 << dsp->index;
	return err;
}

int snd_mixart_setup_firmware(struct mixart_mgr *mgr)
{
	int err;
	struct snd_hwdep *hw;

	/* only create hwdep interface for first cardX (see "index" module parameter)*/
	if ((err = snd_hwdep_new(mgr->chip[0]->card, SND_MIXART_HWDEP_ID, 0, &hw)) < 0)
		return err;

	hw->iface = SNDRV_HWDEP_IFACE_MIXART;
	hw->private_data = mgr;
	hw->ops.open = mixart_hwdep_open;
	hw->ops.release = mixart_hwdep_release;
	hw->ops.dsp_status = mixart_hwdep_dsp_status;
	hw->ops.dsp_load = mixart_hwdep_dsp_load;
	hw->exclusive = 1;
	sprintf(hw->name,  SND_MIXART_HWDEP_ID);
	mgr->dsp_loaded = 0;

	return snd_card_register(mgr->chip[0]->card);
}

#endif /* SND_MIXART_FW_LOADER */
