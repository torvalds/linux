/*
 *  Copyright (c) by Jaroslav Kysela <perex@perex.cz>
 *                   Abramo Bagnara <abramo@alsa-project.org>
 *                   Cirrus Logic, Inc.
 *  Routines for control of Cirrus Logic CS461x chips
 *
 *  KNOWN BUGS:
 *    - Sometimes the SPDIF input DSP tasks get's unsynchronized
 *      and the SPDIF get somewhat "distorcionated", or/and left right channel
 *      are swapped. To get around this problem when it happens, mute and unmute 
 *      the SPDIF input mixer control.
 *    - On the Hercules Game Theater XP the amplifier are sometimes turned
 *      off on inadecuate moments which causes distorcions on sound.
 *
 *  TODO:
 *    - Secondary CODEC on some soundcards
 *    - SPDIF input support for other sample rates then 48khz
 *    - Posibility to mix the SPDIF output with analog sources.
 *    - PCM channels for Center and LFE on secondary codec
 *
 *  NOTE: with CONFIG_SND_CS46XX_NEW_DSP unset uses old DSP image (which
 *        is default configuration), no SPDIF, no secondary codec, no
 *        multi channel PCM.  But known to work.
 *
 *  FINALLY: A credit to the developers Tom and Jordan 
 *           at Cirrus for have helping me out with the DSP, however we
 *           still don't have sufficient documentation and technical
 *           references to be able to implement all fancy feutures
 *           supported by the cs46xx DSP's. 
 *           Benny <benny@hostmobility.com>
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
 *
 */

#include <linux/delay.h>
#include <linux/pci.h>
#include <linux/pm.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/slab.h>
#include <linux/gameport.h>
#include <linux/mutex.h>
#include <linux/export.h>
#include <linux/module.h>
#include <linux/firmware.h>
#include <linux/vmalloc.h>
#include <linux/io.h>

#include <sound/core.h>
#include <sound/control.h>
#include <sound/info.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include "cs46xx.h"

#include "cs46xx_lib.h"
#include "dsp_spos.h"

static void amp_voyetra(struct snd_cs46xx *chip, int change);

#ifdef CONFIG_SND_CS46XX_NEW_DSP
static const struct snd_pcm_ops snd_cs46xx_playback_rear_ops;
static const struct snd_pcm_ops snd_cs46xx_playback_indirect_rear_ops;
static const struct snd_pcm_ops snd_cs46xx_playback_clfe_ops;
static const struct snd_pcm_ops snd_cs46xx_playback_indirect_clfe_ops;
static const struct snd_pcm_ops snd_cs46xx_playback_iec958_ops;
static const struct snd_pcm_ops snd_cs46xx_playback_indirect_iec958_ops;
#endif

static const struct snd_pcm_ops snd_cs46xx_playback_ops;
static const struct snd_pcm_ops snd_cs46xx_playback_indirect_ops;
static const struct snd_pcm_ops snd_cs46xx_capture_ops;
static const struct snd_pcm_ops snd_cs46xx_capture_indirect_ops;

static unsigned short snd_cs46xx_codec_read(struct snd_cs46xx *chip,
					    unsigned short reg,
					    int codec_index)
{
	int count;
	unsigned short result,tmp;
	u32 offset = 0;

	if (snd_BUG_ON(codec_index != CS46XX_PRIMARY_CODEC_INDEX &&
		       codec_index != CS46XX_SECONDARY_CODEC_INDEX))
		return 0xffff;

	chip->active_ctrl(chip, 1);

	if (codec_index == CS46XX_SECONDARY_CODEC_INDEX)
		offset = CS46XX_SECONDARY_CODEC_OFFSET;

	/*
	 *  1. Write ACCAD = Command Address Register = 46Ch for AC97 register address
	 *  2. Write ACCDA = Command Data Register = 470h    for data to write to AC97 
	 *  3. Write ACCTL = Control Register = 460h for initiating the write7---55
	 *  4. Read ACCTL = 460h, DCV should be reset by now and 460h = 17h
	 *  5. if DCV not cleared, break and return error
	 *  6. Read ACSTS = Status Register = 464h, check VSTS bit
	 */

	snd_cs46xx_peekBA0(chip, BA0_ACSDA + offset);

	tmp = snd_cs46xx_peekBA0(chip, BA0_ACCTL);
	if ((tmp & ACCTL_VFRM) == 0) {
		dev_warn(chip->card->dev, "ACCTL_VFRM not set 0x%x\n", tmp);
		snd_cs46xx_pokeBA0(chip, BA0_ACCTL, (tmp & (~ACCTL_ESYN)) | ACCTL_VFRM );
		msleep(50);
		tmp = snd_cs46xx_peekBA0(chip, BA0_ACCTL + offset);
		snd_cs46xx_pokeBA0(chip, BA0_ACCTL, tmp | ACCTL_ESYN | ACCTL_VFRM );

	}

	/*
	 *  Setup the AC97 control registers on the CS461x to send the
	 *  appropriate command to the AC97 to perform the read.
	 *  ACCAD = Command Address Register = 46Ch
	 *  ACCDA = Command Data Register = 470h
	 *  ACCTL = Control Register = 460h
	 *  set DCV - will clear when process completed
	 *  set CRW - Read command
	 *  set VFRM - valid frame enabled
	 *  set ESYN - ASYNC generation enabled
	 *  set RSTN - ARST# inactive, AC97 codec not reset
	 */

	snd_cs46xx_pokeBA0(chip, BA0_ACCAD, reg);
	snd_cs46xx_pokeBA0(chip, BA0_ACCDA, 0);
	if (codec_index == CS46XX_PRIMARY_CODEC_INDEX) {
		snd_cs46xx_pokeBA0(chip, BA0_ACCTL,/* clear ACCTL_DCV */ ACCTL_CRW | 
				   ACCTL_VFRM | ACCTL_ESYN |
				   ACCTL_RSTN);
		snd_cs46xx_pokeBA0(chip, BA0_ACCTL, ACCTL_DCV | ACCTL_CRW |
				   ACCTL_VFRM | ACCTL_ESYN |
				   ACCTL_RSTN);
	} else {
		snd_cs46xx_pokeBA0(chip, BA0_ACCTL, ACCTL_DCV | ACCTL_TC |
				   ACCTL_CRW | ACCTL_VFRM | ACCTL_ESYN |
				   ACCTL_RSTN);
	}

	/*
	 *  Wait for the read to occur.
	 */
	for (count = 0; count < 1000; count++) {
		/*
		 *  First, we want to wait for a short time.
	 	 */
		udelay(10);
		/*
		 *  Now, check to see if the read has completed.
		 *  ACCTL = 460h, DCV should be reset by now and 460h = 17h
		 */
		if (!(snd_cs46xx_peekBA0(chip, BA0_ACCTL) & ACCTL_DCV))
			goto ok1;
	}

	dev_err(chip->card->dev,
		"AC'97 read problem (ACCTL_DCV), reg = 0x%x\n", reg);
	result = 0xffff;
	goto end;
	
 ok1:
	/*
	 *  Wait for the valid status bit to go active.
	 */
	for (count = 0; count < 100; count++) {
		/*
		 *  Read the AC97 status register.
		 *  ACSTS = Status Register = 464h
		 *  VSTS - Valid Status
		 */
		if (snd_cs46xx_peekBA0(chip, BA0_ACSTS + offset) & ACSTS_VSTS)
			goto ok2;
		udelay(10);
	}
	
	dev_err(chip->card->dev,
		"AC'97 read problem (ACSTS_VSTS), codec_index %d, reg = 0x%x\n",
		codec_index, reg);
	result = 0xffff;
	goto end;

 ok2:
	/*
	 *  Read the data returned from the AC97 register.
	 *  ACSDA = Status Data Register = 474h
	 */
#if 0
	dev_dbg(chip->card->dev,
		"e) reg = 0x%x, val = 0x%x, BA0_ACCAD = 0x%x\n", reg,
			snd_cs46xx_peekBA0(chip, BA0_ACSDA),
			snd_cs46xx_peekBA0(chip, BA0_ACCAD));
#endif

	//snd_cs46xx_peekBA0(chip, BA0_ACCAD);
	result = snd_cs46xx_peekBA0(chip, BA0_ACSDA + offset);
 end:
	chip->active_ctrl(chip, -1);
	return result;
}

static unsigned short snd_cs46xx_ac97_read(struct snd_ac97 * ac97,
					    unsigned short reg)
{
	struct snd_cs46xx *chip = ac97->private_data;
	unsigned short val;
	int codec_index = ac97->num;

	if (snd_BUG_ON(codec_index != CS46XX_PRIMARY_CODEC_INDEX &&
		       codec_index != CS46XX_SECONDARY_CODEC_INDEX))
		return 0xffff;

	val = snd_cs46xx_codec_read(chip, reg, codec_index);

	return val;
}


static void snd_cs46xx_codec_write(struct snd_cs46xx *chip,
				   unsigned short reg,
				   unsigned short val,
				   int codec_index)
{
	int count;

	if (snd_BUG_ON(codec_index != CS46XX_PRIMARY_CODEC_INDEX &&
		       codec_index != CS46XX_SECONDARY_CODEC_INDEX))
		return;

	chip->active_ctrl(chip, 1);

	/*
	 *  1. Write ACCAD = Command Address Register = 46Ch for AC97 register address
	 *  2. Write ACCDA = Command Data Register = 470h    for data to write to AC97
	 *  3. Write ACCTL = Control Register = 460h for initiating the write
	 *  4. Read ACCTL = 460h, DCV should be reset by now and 460h = 07h
	 *  5. if DCV not cleared, break and return error
	 */

	/*
	 *  Setup the AC97 control registers on the CS461x to send the
	 *  appropriate command to the AC97 to perform the read.
	 *  ACCAD = Command Address Register = 46Ch
	 *  ACCDA = Command Data Register = 470h
	 *  ACCTL = Control Register = 460h
	 *  set DCV - will clear when process completed
	 *  reset CRW - Write command
	 *  set VFRM - valid frame enabled
	 *  set ESYN - ASYNC generation enabled
	 *  set RSTN - ARST# inactive, AC97 codec not reset
         */
	snd_cs46xx_pokeBA0(chip, BA0_ACCAD , reg);
	snd_cs46xx_pokeBA0(chip, BA0_ACCDA , val);
	snd_cs46xx_peekBA0(chip, BA0_ACCTL);

	if (codec_index == CS46XX_PRIMARY_CODEC_INDEX) {
		snd_cs46xx_pokeBA0(chip, BA0_ACCTL, /* clear ACCTL_DCV */ ACCTL_VFRM |
				   ACCTL_ESYN | ACCTL_RSTN);
		snd_cs46xx_pokeBA0(chip, BA0_ACCTL, ACCTL_DCV | ACCTL_VFRM |
				   ACCTL_ESYN | ACCTL_RSTN);
	} else {
		snd_cs46xx_pokeBA0(chip, BA0_ACCTL, ACCTL_DCV | ACCTL_TC |
				   ACCTL_VFRM | ACCTL_ESYN | ACCTL_RSTN);
	}

	for (count = 0; count < 4000; count++) {
		/*
		 *  First, we want to wait for a short time.
		 */
		udelay(10);
		/*
		 *  Now, check to see if the write has completed.
		 *  ACCTL = 460h, DCV should be reset by now and 460h = 07h
		 */
		if (!(snd_cs46xx_peekBA0(chip, BA0_ACCTL) & ACCTL_DCV)) {
			goto end;
		}
	}
	dev_err(chip->card->dev,
		"AC'97 write problem, codec_index = %d, reg = 0x%x, val = 0x%x\n",
		codec_index, reg, val);
 end:
	chip->active_ctrl(chip, -1);
}

static void snd_cs46xx_ac97_write(struct snd_ac97 *ac97,
				   unsigned short reg,
				   unsigned short val)
{
	struct snd_cs46xx *chip = ac97->private_data;
	int codec_index = ac97->num;

	if (snd_BUG_ON(codec_index != CS46XX_PRIMARY_CODEC_INDEX &&
		       codec_index != CS46XX_SECONDARY_CODEC_INDEX))
		return;

	snd_cs46xx_codec_write(chip, reg, val, codec_index);
}


/*
 *  Chip initialization
 */

int snd_cs46xx_download(struct snd_cs46xx *chip,
			u32 *src,
                        unsigned long offset,
                        unsigned long len)
{
	void __iomem *dst;
	unsigned int bank = offset >> 16;
	offset = offset & 0xffff;

	if (snd_BUG_ON((offset & 3) || (len & 3)))
		return -EINVAL;
	dst = chip->region.idx[bank+1].remap_addr + offset;
	len /= sizeof(u32);

	/* writel already converts 32-bit value to right endianess */
	while (len-- > 0) {
		writel(*src++, dst);
		dst += sizeof(u32);
	}
	return 0;
}

static inline void memcpy_le32(void *dst, const void *src, unsigned int len)
{
#ifdef __LITTLE_ENDIAN
	memcpy(dst, src, len);
#else
	u32 *_dst = dst;
	const __le32 *_src = src;
	len /= 4;
	while (len-- > 0)
		*_dst++ = le32_to_cpu(*_src++);
#endif
}

#ifdef CONFIG_SND_CS46XX_NEW_DSP

static const char *module_names[CS46XX_DSP_MODULES] = {
	"cwc4630", "cwcasync", "cwcsnoop", "cwcbinhack", "cwcdma"
};

MODULE_FIRMWARE("cs46xx/cwc4630");
MODULE_FIRMWARE("cs46xx/cwcasync");
MODULE_FIRMWARE("cs46xx/cwcsnoop");
MODULE_FIRMWARE("cs46xx/cwcbinhack");
MODULE_FIRMWARE("cs46xx/cwcdma");

static void free_module_desc(struct dsp_module_desc *module)
{
	if (!module)
		return;
	kfree(module->module_name);
	kfree(module->symbol_table.symbols);
	if (module->segments) {
		int i;
		for (i = 0; i < module->nsegments; i++)
			kfree(module->segments[i].data);
		kfree(module->segments);
	}
	kfree(module);
}

/* firmware binary format:
 * le32 nsymbols;
 * struct {
 *	le32 address;
 *	char symbol_name[DSP_MAX_SYMBOL_NAME];
 *	le32 symbol_type;
 * } symbols[nsymbols];
 * le32 nsegments;
 * struct {
 *	le32 segment_type;
 *	le32 offset;
 *	le32 size;
 *	le32 data[size];
 * } segments[nsegments];
 */

static int load_firmware(struct snd_cs46xx *chip,
			 struct dsp_module_desc **module_ret,
			 const char *fw_name)
{
	int i, err;
	unsigned int nums, fwlen, fwsize;
	const __le32 *fwdat;
	struct dsp_module_desc *module = NULL;
	const struct firmware *fw;
	char fw_path[32];

	sprintf(fw_path, "cs46xx/%s", fw_name);
	err = request_firmware(&fw, fw_path, &chip->pci->dev);
	if (err < 0)
		return err;
	fwsize = fw->size / 4;
	if (fwsize < 2) {
		err = -EINVAL;
		goto error;
	}

	err = -ENOMEM;
	module = kzalloc(sizeof(*module), GFP_KERNEL);
	if (!module)
		goto error;
	module->module_name = kstrdup(fw_name, GFP_KERNEL);
	if (!module->module_name)
		goto error;

	fwlen = 0;
	fwdat = (const __le32 *)fw->data;
	nums = module->symbol_table.nsymbols = le32_to_cpu(fwdat[fwlen++]);
	if (nums >= 40)
		goto error_inval;
	module->symbol_table.symbols =
		kcalloc(nums, sizeof(struct dsp_symbol_entry), GFP_KERNEL);
	if (!module->symbol_table.symbols)
		goto error;
	for (i = 0; i < nums; i++) {
		struct dsp_symbol_entry *entry =
			&module->symbol_table.symbols[i];
		if (fwlen + 2 + DSP_MAX_SYMBOL_NAME / 4 > fwsize)
			goto error_inval;
		entry->address = le32_to_cpu(fwdat[fwlen++]);
		memcpy(entry->symbol_name, &fwdat[fwlen], DSP_MAX_SYMBOL_NAME - 1);
		fwlen += DSP_MAX_SYMBOL_NAME / 4;
		entry->symbol_type = le32_to_cpu(fwdat[fwlen++]);
	}

	if (fwlen >= fwsize)
		goto error_inval;
	nums = module->nsegments = le32_to_cpu(fwdat[fwlen++]);
	if (nums > 10)
		goto error_inval;
	module->segments =
		kcalloc(nums, sizeof(struct dsp_segment_desc), GFP_KERNEL);
	if (!module->segments)
		goto error;
	for (i = 0; i < nums; i++) {
		struct dsp_segment_desc *entry = &module->segments[i];
		if (fwlen + 3 > fwsize)
			goto error_inval;
		entry->segment_type = le32_to_cpu(fwdat[fwlen++]);
		entry->offset = le32_to_cpu(fwdat[fwlen++]);
		entry->size = le32_to_cpu(fwdat[fwlen++]);
		if (fwlen + entry->size > fwsize)
			goto error_inval;
		entry->data = kmalloc_array(entry->size, 4, GFP_KERNEL);
		if (!entry->data)
			goto error;
		memcpy_le32(entry->data, &fwdat[fwlen], entry->size * 4);
		fwlen += entry->size;
	}

	*module_ret = module;
	release_firmware(fw);
	return 0;

 error_inval:
	err = -EINVAL;
 error:
	free_module_desc(module);
	release_firmware(fw);
	return err;
}

int snd_cs46xx_clear_BA1(struct snd_cs46xx *chip,
                         unsigned long offset,
                         unsigned long len) 
{
	void __iomem *dst;
	unsigned int bank = offset >> 16;
	offset = offset & 0xffff;

	if (snd_BUG_ON((offset & 3) || (len & 3)))
		return -EINVAL;
	dst = chip->region.idx[bank+1].remap_addr + offset;
	len /= sizeof(u32);

	/* writel already converts 32-bit value to right endianess */
	while (len-- > 0) {
		writel(0, dst);
		dst += sizeof(u32);
	}
	return 0;
}

#else /* old DSP image */

struct ba1_struct {
	struct {
		u32 offset;
		u32 size;
	} memory[BA1_MEMORY_COUNT];
	u32 map[BA1_DWORD_SIZE];
};

MODULE_FIRMWARE("cs46xx/ba1");

static int load_firmware(struct snd_cs46xx *chip)
{
	const struct firmware *fw;
	int i, size, err;

	err = request_firmware(&fw, "cs46xx/ba1", &chip->pci->dev);
	if (err < 0)
		return err;
	if (fw->size != sizeof(*chip->ba1)) {
		err = -EINVAL;
		goto error;
	}

	chip->ba1 = vmalloc(sizeof(*chip->ba1));
	if (!chip->ba1) {
		err = -ENOMEM;
		goto error;
	}

	memcpy_le32(chip->ba1, fw->data, sizeof(*chip->ba1));

	/* sanity check */
	size = 0;
	for (i = 0; i < BA1_MEMORY_COUNT; i++)
		size += chip->ba1->memory[i].size;
	if (size > BA1_DWORD_SIZE * 4)
		err = -EINVAL;

 error:
	release_firmware(fw);
	return err;
}

int snd_cs46xx_download_image(struct snd_cs46xx *chip)
{
	int idx, err;
	unsigned int offset = 0;
	struct ba1_struct *ba1 = chip->ba1;

	for (idx = 0; idx < BA1_MEMORY_COUNT; idx++) {
		err = snd_cs46xx_download(chip,
					  &ba1->map[offset],
					  ba1->memory[idx].offset,
					  ba1->memory[idx].size);
		if (err < 0)
			return err;
		offset += ba1->memory[idx].size >> 2;
	}	
	return 0;
}
#endif /* CONFIG_SND_CS46XX_NEW_DSP */

/*
 *  Chip reset
 */

static void snd_cs46xx_reset(struct snd_cs46xx *chip)
{
	int idx;

	/*
	 *  Write the reset bit of the SP control register.
	 */
	snd_cs46xx_poke(chip, BA1_SPCR, SPCR_RSTSP);

	/*
	 *  Write the control register.
	 */
	snd_cs46xx_poke(chip, BA1_SPCR, SPCR_DRQEN);

	/*
	 *  Clear the trap registers.
	 */
	for (idx = 0; idx < 8; idx++) {
		snd_cs46xx_poke(chip, BA1_DREG, DREG_REGID_TRAP_SELECT + idx);
		snd_cs46xx_poke(chip, BA1_TWPR, 0xFFFF);
	}
	snd_cs46xx_poke(chip, BA1_DREG, 0);

	/*
	 *  Set the frame timer to reflect the number of cycles per frame.
	 */
	snd_cs46xx_poke(chip, BA1_FRMT, 0xadf);
}

static int cs46xx_wait_for_fifo(struct snd_cs46xx * chip,int retry_timeout) 
{
	u32 i, status = 0;
	/*
	 * Make sure the previous FIFO write operation has completed.
	 */
	for(i = 0; i < 50; i++){
		status = snd_cs46xx_peekBA0(chip, BA0_SERBST);
    
		if( !(status & SERBST_WBSY) )
			break;

		mdelay(retry_timeout);
	}
  
	if(status & SERBST_WBSY) {
		dev_err(chip->card->dev,
			"failure waiting for FIFO command to complete\n");
		return -EINVAL;
	}

	return 0;
}

static void snd_cs46xx_clear_serial_FIFOs(struct snd_cs46xx *chip)
{
	int idx, powerdown = 0;
	unsigned int tmp;

	/*
	 *  See if the devices are powered down.  If so, we must power them up first
	 *  or they will not respond.
	 */
	tmp = snd_cs46xx_peekBA0(chip, BA0_CLKCR1);
	if (!(tmp & CLKCR1_SWCE)) {
		snd_cs46xx_pokeBA0(chip, BA0_CLKCR1, tmp | CLKCR1_SWCE);
		powerdown = 1;
	}

	/*
	 *  We want to clear out the serial port FIFOs so we don't end up playing
	 *  whatever random garbage happens to be in them.  We fill the sample FIFOS
	 *  with zero (silence).
	 */
	snd_cs46xx_pokeBA0(chip, BA0_SERBWP, 0);

	/*
	 *  Fill all 256 sample FIFO locations.
	 */
	for (idx = 0; idx < 0xFF; idx++) {
		/*
		 *  Make sure the previous FIFO write operation has completed.
		 */
		if (cs46xx_wait_for_fifo(chip,1)) {
			dev_dbg(chip->card->dev,
				"failed waiting for FIFO at addr (%02X)\n",
				idx);

			if (powerdown)
				snd_cs46xx_pokeBA0(chip, BA0_CLKCR1, tmp);
          
			break;
		}
		/*
		 *  Write the serial port FIFO index.
		 */
		snd_cs46xx_pokeBA0(chip, BA0_SERBAD, idx);
		/*
		 *  Tell the serial port to load the new value into the FIFO location.
		 */
		snd_cs46xx_pokeBA0(chip, BA0_SERBCM, SERBCM_WRC);
	}
	/*
	 *  Now, if we powered up the devices, then power them back down again.
	 *  This is kinda ugly, but should never happen.
	 */
	if (powerdown)
		snd_cs46xx_pokeBA0(chip, BA0_CLKCR1, tmp);
}

static void snd_cs46xx_proc_start(struct snd_cs46xx *chip)
{
	int cnt;

	/*
	 *  Set the frame timer to reflect the number of cycles per frame.
	 */
	snd_cs46xx_poke(chip, BA1_FRMT, 0xadf);
	/*
	 *  Turn on the run, run at frame, and DMA enable bits in the local copy of
	 *  the SP control register.
	 */
	snd_cs46xx_poke(chip, BA1_SPCR, SPCR_RUN | SPCR_RUNFR | SPCR_DRQEN);
	/*
	 *  Wait until the run at frame bit resets itself in the SP control
	 *  register.
	 */
	for (cnt = 0; cnt < 25; cnt++) {
		udelay(50);
		if (!(snd_cs46xx_peek(chip, BA1_SPCR) & SPCR_RUNFR))
			break;
	}

	if (snd_cs46xx_peek(chip, BA1_SPCR) & SPCR_RUNFR)
		dev_err(chip->card->dev, "SPCR_RUNFR never reset\n");
}

static void snd_cs46xx_proc_stop(struct snd_cs46xx *chip)
{
	/*
	 *  Turn off the run, run at frame, and DMA enable bits in the local copy of
	 *  the SP control register.
	 */
	snd_cs46xx_poke(chip, BA1_SPCR, 0);
}

/*
 *  Sample rate routines
 */

#define GOF_PER_SEC 200

static void snd_cs46xx_set_play_sample_rate(struct snd_cs46xx *chip, unsigned int rate)
{
	unsigned long flags;
	unsigned int tmp1, tmp2;
	unsigned int phiIncr;
	unsigned int correctionPerGOF, correctionPerSec;

	/*
	 *  Compute the values used to drive the actual sample rate conversion.
	 *  The following formulas are being computed, using inline assembly
	 *  since we need to use 64 bit arithmetic to compute the values:
	 *
	 *  phiIncr = floor((Fs,in * 2^26) / Fs,out)
	 *  correctionPerGOF = floor((Fs,in * 2^26 - Fs,out * phiIncr) /
         *                                   GOF_PER_SEC)
         *  ulCorrectionPerSec = Fs,in * 2^26 - Fs,out * phiIncr -M
         *                       GOF_PER_SEC * correctionPerGOF
	 *
	 *  i.e.
	 *
	 *  phiIncr:other = dividend:remainder((Fs,in * 2^26) / Fs,out)
	 *  correctionPerGOF:correctionPerSec =
	 *      dividend:remainder(ulOther / GOF_PER_SEC)
	 */
	tmp1 = rate << 16;
	phiIncr = tmp1 / 48000;
	tmp1 -= phiIncr * 48000;
	tmp1 <<= 10;
	phiIncr <<= 10;
	tmp2 = tmp1 / 48000;
	phiIncr += tmp2;
	tmp1 -= tmp2 * 48000;
	correctionPerGOF = tmp1 / GOF_PER_SEC;
	tmp1 -= correctionPerGOF * GOF_PER_SEC;
	correctionPerSec = tmp1;

	/*
	 *  Fill in the SampleRateConverter control block.
	 */
	spin_lock_irqsave(&chip->reg_lock, flags);
	snd_cs46xx_poke(chip, BA1_PSRC,
	  ((correctionPerSec << 16) & 0xFFFF0000) | (correctionPerGOF & 0xFFFF));
	snd_cs46xx_poke(chip, BA1_PPI, phiIncr);
	spin_unlock_irqrestore(&chip->reg_lock, flags);
}

static void snd_cs46xx_set_capture_sample_rate(struct snd_cs46xx *chip, unsigned int rate)
{
	unsigned long flags;
	unsigned int phiIncr, coeffIncr, tmp1, tmp2;
	unsigned int correctionPerGOF, correctionPerSec, initialDelay;
	unsigned int frameGroupLength, cnt;

	/*
	 *  We can only decimate by up to a factor of 1/9th the hardware rate.
	 *  Correct the value if an attempt is made to stray outside that limit.
	 */
	if ((rate * 9) < 48000)
		rate = 48000 / 9;

	/*
	 *  We can not capture at at rate greater than the Input Rate (48000).
	 *  Return an error if an attempt is made to stray outside that limit.
	 */
	if (rate > 48000)
		rate = 48000;

	/*
	 *  Compute the values used to drive the actual sample rate conversion.
	 *  The following formulas are being computed, using inline assembly
	 *  since we need to use 64 bit arithmetic to compute the values:
	 *
	 *     coeffIncr = -floor((Fs,out * 2^23) / Fs,in)
	 *     phiIncr = floor((Fs,in * 2^26) / Fs,out)
	 *     correctionPerGOF = floor((Fs,in * 2^26 - Fs,out * phiIncr) /
	 *                                GOF_PER_SEC)
	 *     correctionPerSec = Fs,in * 2^26 - Fs,out * phiIncr -
	 *                          GOF_PER_SEC * correctionPerGOF
	 *     initialDelay = ceil((24 * Fs,in) / Fs,out)
	 *
	 * i.e.
	 *
	 *     coeffIncr = neg(dividend((Fs,out * 2^23) / Fs,in))
	 *     phiIncr:ulOther = dividend:remainder((Fs,in * 2^26) / Fs,out)
	 *     correctionPerGOF:correctionPerSec =
	 * 	    dividend:remainder(ulOther / GOF_PER_SEC)
	 *     initialDelay = dividend(((24 * Fs,in) + Fs,out - 1) / Fs,out)
	 */

	tmp1 = rate << 16;
	coeffIncr = tmp1 / 48000;
	tmp1 -= coeffIncr * 48000;
	tmp1 <<= 7;
	coeffIncr <<= 7;
	coeffIncr += tmp1 / 48000;
	coeffIncr ^= 0xFFFFFFFF;
	coeffIncr++;
	tmp1 = 48000 << 16;
	phiIncr = tmp1 / rate;
	tmp1 -= phiIncr * rate;
	tmp1 <<= 10;
	phiIncr <<= 10;
	tmp2 = tmp1 / rate;
	phiIncr += tmp2;
	tmp1 -= tmp2 * rate;
	correctionPerGOF = tmp1 / GOF_PER_SEC;
	tmp1 -= correctionPerGOF * GOF_PER_SEC;
	correctionPerSec = tmp1;
	initialDelay = ((48000 * 24) + rate - 1) / rate;

	/*
	 *  Fill in the VariDecimate control block.
	 */
	spin_lock_irqsave(&chip->reg_lock, flags);
	snd_cs46xx_poke(chip, BA1_CSRC,
		((correctionPerSec << 16) & 0xFFFF0000) | (correctionPerGOF & 0xFFFF));
	snd_cs46xx_poke(chip, BA1_CCI, coeffIncr);
	snd_cs46xx_poke(chip, BA1_CD,
		(((BA1_VARIDEC_BUF_1 + (initialDelay << 2)) << 16) & 0xFFFF0000) | 0x80);
	snd_cs46xx_poke(chip, BA1_CPI, phiIncr);
	spin_unlock_irqrestore(&chip->reg_lock, flags);

	/*
	 *  Figure out the frame group length for the write back task.  Basically,
	 *  this is just the factors of 24000 (2^6*3*5^3) that are not present in
	 *  the output sample rate.
	 */
	frameGroupLength = 1;
	for (cnt = 2; cnt <= 64; cnt *= 2) {
		if (((rate / cnt) * cnt) != rate)
			frameGroupLength *= 2;
	}
	if (((rate / 3) * 3) != rate) {
		frameGroupLength *= 3;
	}
	for (cnt = 5; cnt <= 125; cnt *= 5) {
		if (((rate / cnt) * cnt) != rate) 
			frameGroupLength *= 5;
        }

	/*
	 * Fill in the WriteBack control block.
	 */
	spin_lock_irqsave(&chip->reg_lock, flags);
	snd_cs46xx_poke(chip, BA1_CFG1, frameGroupLength);
	snd_cs46xx_poke(chip, BA1_CFG2, (0x00800000 | frameGroupLength));
	snd_cs46xx_poke(chip, BA1_CCST, 0x0000FFFF);
	snd_cs46xx_poke(chip, BA1_CSPB, ((65536 * rate) / 24000));
	snd_cs46xx_poke(chip, (BA1_CSPB + 4), 0x0000FFFF);
	spin_unlock_irqrestore(&chip->reg_lock, flags);
}

/*
 *  PCM part
 */

static void snd_cs46xx_pb_trans_copy(struct snd_pcm_substream *substream,
				     struct snd_pcm_indirect *rec, size_t bytes)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct snd_cs46xx_pcm * cpcm = runtime->private_data;
	memcpy(cpcm->hw_buf.area + rec->hw_data, runtime->dma_area + rec->sw_data, bytes);
}

static int snd_cs46xx_playback_transfer(struct snd_pcm_substream *substream)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct snd_cs46xx_pcm * cpcm = runtime->private_data;
	return snd_pcm_indirect_playback_transfer(substream, &cpcm->pcm_rec,
						  snd_cs46xx_pb_trans_copy);
}

static void snd_cs46xx_cp_trans_copy(struct snd_pcm_substream *substream,
				     struct snd_pcm_indirect *rec, size_t bytes)
{
	struct snd_cs46xx *chip = snd_pcm_substream_chip(substream);
	struct snd_pcm_runtime *runtime = substream->runtime;
	memcpy(runtime->dma_area + rec->sw_data,
	       chip->capt.hw_buf.area + rec->hw_data, bytes);
}

static int snd_cs46xx_capture_transfer(struct snd_pcm_substream *substream)
{
	struct snd_cs46xx *chip = snd_pcm_substream_chip(substream);
	return snd_pcm_indirect_capture_transfer(substream, &chip->capt.pcm_rec,
						 snd_cs46xx_cp_trans_copy);
}

static snd_pcm_uframes_t snd_cs46xx_playback_direct_pointer(struct snd_pcm_substream *substream)
{
	struct snd_cs46xx *chip = snd_pcm_substream_chip(substream);
	size_t ptr;
	struct snd_cs46xx_pcm *cpcm = substream->runtime->private_data;

	if (snd_BUG_ON(!cpcm->pcm_channel))
		return -ENXIO;

#ifdef CONFIG_SND_CS46XX_NEW_DSP
	ptr = snd_cs46xx_peek(chip, (cpcm->pcm_channel->pcm_reader_scb->address + 2) << 2);
#else
	ptr = snd_cs46xx_peek(chip, BA1_PBA);
#endif
	ptr -= cpcm->hw_buf.addr;
	return ptr >> cpcm->shift;
}

static snd_pcm_uframes_t snd_cs46xx_playback_indirect_pointer(struct snd_pcm_substream *substream)
{
	struct snd_cs46xx *chip = snd_pcm_substream_chip(substream);
	size_t ptr;
	struct snd_cs46xx_pcm *cpcm = substream->runtime->private_data;

#ifdef CONFIG_SND_CS46XX_NEW_DSP
	if (snd_BUG_ON(!cpcm->pcm_channel))
		return -ENXIO;
	ptr = snd_cs46xx_peek(chip, (cpcm->pcm_channel->pcm_reader_scb->address + 2) << 2);
#else
	ptr = snd_cs46xx_peek(chip, BA1_PBA);
#endif
	ptr -= cpcm->hw_buf.addr;
	return snd_pcm_indirect_playback_pointer(substream, &cpcm->pcm_rec, ptr);
}

static snd_pcm_uframes_t snd_cs46xx_capture_direct_pointer(struct snd_pcm_substream *substream)
{
	struct snd_cs46xx *chip = snd_pcm_substream_chip(substream);
	size_t ptr = snd_cs46xx_peek(chip, BA1_CBA) - chip->capt.hw_buf.addr;
	return ptr >> chip->capt.shift;
}

static snd_pcm_uframes_t snd_cs46xx_capture_indirect_pointer(struct snd_pcm_substream *substream)
{
	struct snd_cs46xx *chip = snd_pcm_substream_chip(substream);
	size_t ptr = snd_cs46xx_peek(chip, BA1_CBA) - chip->capt.hw_buf.addr;
	return snd_pcm_indirect_capture_pointer(substream, &chip->capt.pcm_rec, ptr);
}

static int snd_cs46xx_playback_trigger(struct snd_pcm_substream *substream,
				       int cmd)
{
	struct snd_cs46xx *chip = snd_pcm_substream_chip(substream);
	/*struct snd_pcm_runtime *runtime = substream->runtime;*/
	int result = 0;

#ifdef CONFIG_SND_CS46XX_NEW_DSP
	struct snd_cs46xx_pcm *cpcm = substream->runtime->private_data;
	if (! cpcm->pcm_channel) {
		return -ENXIO;
	}
#endif
	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
	case SNDRV_PCM_TRIGGER_RESUME:
#ifdef CONFIG_SND_CS46XX_NEW_DSP
		/* magic value to unmute PCM stream  playback volume */
		snd_cs46xx_poke(chip, (cpcm->pcm_channel->pcm_reader_scb->address + 
				       SCBVolumeCtrl) << 2, 0x80008000);

		if (cpcm->pcm_channel->unlinked)
			cs46xx_dsp_pcm_link(chip,cpcm->pcm_channel);

		if (substream->runtime->periods != CS46XX_FRAGS)
			snd_cs46xx_playback_transfer(substream);
#else
		spin_lock(&chip->reg_lock);
		if (substream->runtime->periods != CS46XX_FRAGS)
			snd_cs46xx_playback_transfer(substream);
		{ unsigned int tmp;
		tmp = snd_cs46xx_peek(chip, BA1_PCTL);
		tmp &= 0x0000ffff;
		snd_cs46xx_poke(chip, BA1_PCTL, chip->play_ctl | tmp);
		}
		spin_unlock(&chip->reg_lock);
#endif
		break;
	case SNDRV_PCM_TRIGGER_STOP:
	case SNDRV_PCM_TRIGGER_SUSPEND:
#ifdef CONFIG_SND_CS46XX_NEW_DSP
		/* magic mute channel */
		snd_cs46xx_poke(chip, (cpcm->pcm_channel->pcm_reader_scb->address + 
				       SCBVolumeCtrl) << 2, 0xffffffff);

		if (!cpcm->pcm_channel->unlinked)
			cs46xx_dsp_pcm_unlink(chip,cpcm->pcm_channel);
#else
		spin_lock(&chip->reg_lock);
		{ unsigned int tmp;
		tmp = snd_cs46xx_peek(chip, BA1_PCTL);
		tmp &= 0x0000ffff;
		snd_cs46xx_poke(chip, BA1_PCTL, tmp);
		}
		spin_unlock(&chip->reg_lock);
#endif
		break;
	default:
		result = -EINVAL;
		break;
	}

	return result;
}

static int snd_cs46xx_capture_trigger(struct snd_pcm_substream *substream,
				      int cmd)
{
	struct snd_cs46xx *chip = snd_pcm_substream_chip(substream);
	unsigned int tmp;
	int result = 0;

	spin_lock(&chip->reg_lock);
	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
	case SNDRV_PCM_TRIGGER_RESUME:
		tmp = snd_cs46xx_peek(chip, BA1_CCTL);
		tmp &= 0xffff0000;
		snd_cs46xx_poke(chip, BA1_CCTL, chip->capt.ctl | tmp);
		break;
	case SNDRV_PCM_TRIGGER_STOP:
	case SNDRV_PCM_TRIGGER_SUSPEND:
		tmp = snd_cs46xx_peek(chip, BA1_CCTL);
		tmp &= 0xffff0000;
		snd_cs46xx_poke(chip, BA1_CCTL, tmp);
		break;
	default:
		result = -EINVAL;
		break;
	}
	spin_unlock(&chip->reg_lock);

	return result;
}

#ifdef CONFIG_SND_CS46XX_NEW_DSP
static int _cs46xx_adjust_sample_rate (struct snd_cs46xx *chip, struct snd_cs46xx_pcm *cpcm,
				       int sample_rate) 
{

	/* If PCMReaderSCB and SrcTaskSCB not created yet ... */
	if ( cpcm->pcm_channel == NULL) {
		cpcm->pcm_channel = cs46xx_dsp_create_pcm_channel (chip, sample_rate, 
								   cpcm, cpcm->hw_buf.addr,cpcm->pcm_channel_id);
		if (cpcm->pcm_channel == NULL) {
			dev_err(chip->card->dev,
				"failed to create virtual PCM channel\n");
			return -ENOMEM;
		}
		cpcm->pcm_channel->sample_rate = sample_rate;
	} else
	/* if sample rate is changed */
	if ((int)cpcm->pcm_channel->sample_rate != sample_rate) {
		int unlinked = cpcm->pcm_channel->unlinked;
		cs46xx_dsp_destroy_pcm_channel (chip,cpcm->pcm_channel);

		if ( (cpcm->pcm_channel = cs46xx_dsp_create_pcm_channel (chip, sample_rate, cpcm, 
									 cpcm->hw_buf.addr,
									 cpcm->pcm_channel_id)) == NULL) {
			dev_err(chip->card->dev,
				"failed to re-create virtual PCM channel\n");
			return -ENOMEM;
		}

		if (!unlinked) cs46xx_dsp_pcm_link (chip,cpcm->pcm_channel);
		cpcm->pcm_channel->sample_rate = sample_rate;
	}

	return 0;
}
#endif


static int snd_cs46xx_playback_hw_params(struct snd_pcm_substream *substream,
					 struct snd_pcm_hw_params *hw_params)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct snd_cs46xx_pcm *cpcm;
	int err;
#ifdef CONFIG_SND_CS46XX_NEW_DSP
	struct snd_cs46xx *chip = snd_pcm_substream_chip(substream);
	int sample_rate = params_rate(hw_params);
	int period_size = params_period_bytes(hw_params);
#endif
	cpcm = runtime->private_data;

#ifdef CONFIG_SND_CS46XX_NEW_DSP
	if (snd_BUG_ON(!sample_rate))
		return -ENXIO;

	mutex_lock(&chip->spos_mutex);

	if (_cs46xx_adjust_sample_rate (chip,cpcm,sample_rate)) {
		mutex_unlock(&chip->spos_mutex);
		return -ENXIO;
	}

	snd_BUG_ON(!cpcm->pcm_channel);
	if (!cpcm->pcm_channel) {
		mutex_unlock(&chip->spos_mutex);
		return -ENXIO;
	}


	if (cs46xx_dsp_pcm_channel_set_period (chip,cpcm->pcm_channel,period_size)) {
		 mutex_unlock(&chip->spos_mutex);
		 return -EINVAL;
	 }

	dev_dbg(chip->card->dev,
		"period_size (%d), periods (%d) buffer_size(%d)\n",
		     period_size, params_periods(hw_params),
		     params_buffer_bytes(hw_params));
#endif

	if (params_periods(hw_params) == CS46XX_FRAGS) {
		if (runtime->dma_area != cpcm->hw_buf.area)
			snd_pcm_lib_free_pages(substream);
		runtime->dma_area = cpcm->hw_buf.area;
		runtime->dma_addr = cpcm->hw_buf.addr;
		runtime->dma_bytes = cpcm->hw_buf.bytes;


#ifdef CONFIG_SND_CS46XX_NEW_DSP
		if (cpcm->pcm_channel_id == DSP_PCM_MAIN_CHANNEL) {
			substream->ops = &snd_cs46xx_playback_ops;
		} else if (cpcm->pcm_channel_id == DSP_PCM_REAR_CHANNEL) {
			substream->ops = &snd_cs46xx_playback_rear_ops;
		} else if (cpcm->pcm_channel_id == DSP_PCM_CENTER_LFE_CHANNEL) {
			substream->ops = &snd_cs46xx_playback_clfe_ops;
		} else if (cpcm->pcm_channel_id == DSP_IEC958_CHANNEL) {
			substream->ops = &snd_cs46xx_playback_iec958_ops;
		} else {
			snd_BUG();
		}
#else
		substream->ops = &snd_cs46xx_playback_ops;
#endif

	} else {
		if (runtime->dma_area == cpcm->hw_buf.area) {
			runtime->dma_area = NULL;
			runtime->dma_addr = 0;
			runtime->dma_bytes = 0;
		}
		if ((err = snd_pcm_lib_malloc_pages(substream, params_buffer_bytes(hw_params))) < 0) {
#ifdef CONFIG_SND_CS46XX_NEW_DSP
			mutex_unlock(&chip->spos_mutex);
#endif
			return err;
		}

#ifdef CONFIG_SND_CS46XX_NEW_DSP
		if (cpcm->pcm_channel_id == DSP_PCM_MAIN_CHANNEL) {
			substream->ops = &snd_cs46xx_playback_indirect_ops;
		} else if (cpcm->pcm_channel_id == DSP_PCM_REAR_CHANNEL) {
			substream->ops = &snd_cs46xx_playback_indirect_rear_ops;
		} else if (cpcm->pcm_channel_id == DSP_PCM_CENTER_LFE_CHANNEL) {
			substream->ops = &snd_cs46xx_playback_indirect_clfe_ops;
		} else if (cpcm->pcm_channel_id == DSP_IEC958_CHANNEL) {
			substream->ops = &snd_cs46xx_playback_indirect_iec958_ops;
		} else {
			snd_BUG();
		}
#else
		substream->ops = &snd_cs46xx_playback_indirect_ops;
#endif

	}

#ifdef CONFIG_SND_CS46XX_NEW_DSP
	mutex_unlock(&chip->spos_mutex);
#endif

	return 0;
}

static int snd_cs46xx_playback_hw_free(struct snd_pcm_substream *substream)
{
	/*struct snd_cs46xx *chip = snd_pcm_substream_chip(substream);*/
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct snd_cs46xx_pcm *cpcm;

	cpcm = runtime->private_data;

	/* if play_back open fails, then this function
	   is called and cpcm can actually be NULL here */
	if (!cpcm) return -ENXIO;

	if (runtime->dma_area != cpcm->hw_buf.area)
		snd_pcm_lib_free_pages(substream);
    
	runtime->dma_area = NULL;
	runtime->dma_addr = 0;
	runtime->dma_bytes = 0;

	return 0;
}

static int snd_cs46xx_playback_prepare(struct snd_pcm_substream *substream)
{
	unsigned int tmp;
	unsigned int pfie;
	struct snd_cs46xx *chip = snd_pcm_substream_chip(substream);
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct snd_cs46xx_pcm *cpcm;

	cpcm = runtime->private_data;

#ifdef CONFIG_SND_CS46XX_NEW_DSP
	if (snd_BUG_ON(!cpcm->pcm_channel))
		return -ENXIO;

	pfie = snd_cs46xx_peek(chip, (cpcm->pcm_channel->pcm_reader_scb->address + 1) << 2 );
	pfie &= ~0x0000f03f;
#else
	/* old dsp */
	pfie = snd_cs46xx_peek(chip, BA1_PFIE);
 	pfie &= ~0x0000f03f;
#endif

	cpcm->shift = 2;
	/* if to convert from stereo to mono */
	if (runtime->channels == 1) {
		cpcm->shift--;
		pfie |= 0x00002000;
	}
	/* if to convert from 8 bit to 16 bit */
	if (snd_pcm_format_width(runtime->format) == 8) {
		cpcm->shift--;
		pfie |= 0x00001000;
	}
	/* if to convert to unsigned */
	if (snd_pcm_format_unsigned(runtime->format))
		pfie |= 0x00008000;

	/* Never convert byte order when sample stream is 8 bit */
	if (snd_pcm_format_width(runtime->format) != 8) {
		/* convert from big endian to little endian */
		if (snd_pcm_format_big_endian(runtime->format))
			pfie |= 0x00004000;
	}
	
	memset(&cpcm->pcm_rec, 0, sizeof(cpcm->pcm_rec));
	cpcm->pcm_rec.sw_buffer_size = snd_pcm_lib_buffer_bytes(substream);
	cpcm->pcm_rec.hw_buffer_size = runtime->period_size * CS46XX_FRAGS << cpcm->shift;

#ifdef CONFIG_SND_CS46XX_NEW_DSP

	tmp = snd_cs46xx_peek(chip, (cpcm->pcm_channel->pcm_reader_scb->address) << 2);
	tmp &= ~0x000003ff;
	tmp |= (4 << cpcm->shift) - 1;
	/* playback transaction count register */
	snd_cs46xx_poke(chip, (cpcm->pcm_channel->pcm_reader_scb->address) << 2, tmp);

	/* playback format && interrupt enable */
	snd_cs46xx_poke(chip, (cpcm->pcm_channel->pcm_reader_scb->address + 1) << 2, pfie | cpcm->pcm_channel->pcm_slot);
#else
	snd_cs46xx_poke(chip, BA1_PBA, cpcm->hw_buf.addr);
	tmp = snd_cs46xx_peek(chip, BA1_PDTC);
	tmp &= ~0x000003ff;
	tmp |= (4 << cpcm->shift) - 1;
	snd_cs46xx_poke(chip, BA1_PDTC, tmp);
	snd_cs46xx_poke(chip, BA1_PFIE, pfie);
	snd_cs46xx_set_play_sample_rate(chip, runtime->rate);
#endif

	return 0;
}

static int snd_cs46xx_capture_hw_params(struct snd_pcm_substream *substream,
					struct snd_pcm_hw_params *hw_params)
{
	struct snd_cs46xx *chip = snd_pcm_substream_chip(substream);
	struct snd_pcm_runtime *runtime = substream->runtime;
	int err;

#ifdef CONFIG_SND_CS46XX_NEW_DSP
	cs46xx_dsp_pcm_ostream_set_period (chip, params_period_bytes(hw_params));
#endif
	if (runtime->periods == CS46XX_FRAGS) {
		if (runtime->dma_area != chip->capt.hw_buf.area)
			snd_pcm_lib_free_pages(substream);
		runtime->dma_area = chip->capt.hw_buf.area;
		runtime->dma_addr = chip->capt.hw_buf.addr;
		runtime->dma_bytes = chip->capt.hw_buf.bytes;
		substream->ops = &snd_cs46xx_capture_ops;
	} else {
		if (runtime->dma_area == chip->capt.hw_buf.area) {
			runtime->dma_area = NULL;
			runtime->dma_addr = 0;
			runtime->dma_bytes = 0;
		}
		if ((err = snd_pcm_lib_malloc_pages(substream, params_buffer_bytes(hw_params))) < 0)
			return err;
		substream->ops = &snd_cs46xx_capture_indirect_ops;
	}

	return 0;
}

static int snd_cs46xx_capture_hw_free(struct snd_pcm_substream *substream)
{
	struct snd_cs46xx *chip = snd_pcm_substream_chip(substream);
	struct snd_pcm_runtime *runtime = substream->runtime;

	if (runtime->dma_area != chip->capt.hw_buf.area)
		snd_pcm_lib_free_pages(substream);
	runtime->dma_area = NULL;
	runtime->dma_addr = 0;
	runtime->dma_bytes = 0;

	return 0;
}

static int snd_cs46xx_capture_prepare(struct snd_pcm_substream *substream)
{
	struct snd_cs46xx *chip = snd_pcm_substream_chip(substream);
	struct snd_pcm_runtime *runtime = substream->runtime;

	snd_cs46xx_poke(chip, BA1_CBA, chip->capt.hw_buf.addr);
	chip->capt.shift = 2;
	memset(&chip->capt.pcm_rec, 0, sizeof(chip->capt.pcm_rec));
	chip->capt.pcm_rec.sw_buffer_size = snd_pcm_lib_buffer_bytes(substream);
	chip->capt.pcm_rec.hw_buffer_size = runtime->period_size * CS46XX_FRAGS << 2;
	snd_cs46xx_set_capture_sample_rate(chip, runtime->rate);

	return 0;
}

static irqreturn_t snd_cs46xx_interrupt(int irq, void *dev_id)
{
	struct snd_cs46xx *chip = dev_id;
	u32 status1;
#ifdef CONFIG_SND_CS46XX_NEW_DSP
	struct dsp_spos_instance * ins = chip->dsp_spos_instance;
	u32 status2;
	int i;
	struct snd_cs46xx_pcm *cpcm = NULL;
#endif

	/*
	 *  Read the Interrupt Status Register to clear the interrupt
	 */
	status1 = snd_cs46xx_peekBA0(chip, BA0_HISR);
	if ((status1 & 0x7fffffff) == 0) {
		snd_cs46xx_pokeBA0(chip, BA0_HICR, HICR_CHGM | HICR_IEV);
		return IRQ_NONE;
	}

#ifdef CONFIG_SND_CS46XX_NEW_DSP
	status2 = snd_cs46xx_peekBA0(chip, BA0_HSR0);

	for (i = 0; i < DSP_MAX_PCM_CHANNELS; ++i) {
		if (i <= 15) {
			if ( status1 & (1 << i) ) {
				if (i == CS46XX_DSP_CAPTURE_CHANNEL) {
					if (chip->capt.substream)
						snd_pcm_period_elapsed(chip->capt.substream);
				} else {
					if (ins->pcm_channels[i].active &&
					    ins->pcm_channels[i].private_data &&
					    !ins->pcm_channels[i].unlinked) {
						cpcm = ins->pcm_channels[i].private_data;
						snd_pcm_period_elapsed(cpcm->substream);
					}
				}
			}
		} else {
			if ( status2 & (1 << (i - 16))) {
				if (ins->pcm_channels[i].active && 
				    ins->pcm_channels[i].private_data &&
				    !ins->pcm_channels[i].unlinked) {
					cpcm = ins->pcm_channels[i].private_data;
					snd_pcm_period_elapsed(cpcm->substream);
				}
			}
		}
	}

#else
	/* old dsp */
	if ((status1 & HISR_VC0) && chip->playback_pcm) {
		if (chip->playback_pcm->substream)
			snd_pcm_period_elapsed(chip->playback_pcm->substream);
	}
	if ((status1 & HISR_VC1) && chip->pcm) {
		if (chip->capt.substream)
			snd_pcm_period_elapsed(chip->capt.substream);
	}
#endif

	if ((status1 & HISR_MIDI) && chip->rmidi) {
		unsigned char c;
		
		spin_lock(&chip->reg_lock);
		while ((snd_cs46xx_peekBA0(chip, BA0_MIDSR) & MIDSR_RBE) == 0) {
			c = snd_cs46xx_peekBA0(chip, BA0_MIDRP);
			if ((chip->midcr & MIDCR_RIE) == 0)
				continue;
			snd_rawmidi_receive(chip->midi_input, &c, 1);
		}
		while ((snd_cs46xx_peekBA0(chip, BA0_MIDSR) & MIDSR_TBF) == 0) {
			if ((chip->midcr & MIDCR_TIE) == 0)
				break;
			if (snd_rawmidi_transmit(chip->midi_output, &c, 1) != 1) {
				chip->midcr &= ~MIDCR_TIE;
				snd_cs46xx_pokeBA0(chip, BA0_MIDCR, chip->midcr);
				break;
			}
			snd_cs46xx_pokeBA0(chip, BA0_MIDWP, c);
		}
		spin_unlock(&chip->reg_lock);
	}
	/*
	 *  EOI to the PCI part....reenables interrupts
	 */
	snd_cs46xx_pokeBA0(chip, BA0_HICR, HICR_CHGM | HICR_IEV);

	return IRQ_HANDLED;
}

static const struct snd_pcm_hardware snd_cs46xx_playback =
{
	.info =			(SNDRV_PCM_INFO_MMAP |
				 SNDRV_PCM_INFO_INTERLEAVED | 
				 SNDRV_PCM_INFO_BLOCK_TRANSFER /*|*/
				 /*SNDRV_PCM_INFO_RESUME*/ |
				 SNDRV_PCM_INFO_SYNC_APPLPTR),
	.formats =		(SNDRV_PCM_FMTBIT_S8 | SNDRV_PCM_FMTBIT_U8 |
				 SNDRV_PCM_FMTBIT_S16_LE | SNDRV_PCM_FMTBIT_S16_BE |
				 SNDRV_PCM_FMTBIT_U16_LE | SNDRV_PCM_FMTBIT_U16_BE),
	.rates =		SNDRV_PCM_RATE_CONTINUOUS | SNDRV_PCM_RATE_8000_48000,
	.rate_min =		5500,
	.rate_max =		48000,
	.channels_min =		1,
	.channels_max =		2,
	.buffer_bytes_max =	(256 * 1024),
	.period_bytes_min =	CS46XX_MIN_PERIOD_SIZE,
	.period_bytes_max =	CS46XX_MAX_PERIOD_SIZE,
	.periods_min =		CS46XX_FRAGS,
	.periods_max =		1024,
	.fifo_size =		0,
};

static const struct snd_pcm_hardware snd_cs46xx_capture =
{
	.info =			(SNDRV_PCM_INFO_MMAP |
				 SNDRV_PCM_INFO_INTERLEAVED |
				 SNDRV_PCM_INFO_BLOCK_TRANSFER /*|*/
				 /*SNDRV_PCM_INFO_RESUME*/ |
				 SNDRV_PCM_INFO_SYNC_APPLPTR),
	.formats =		SNDRV_PCM_FMTBIT_S16_LE,
	.rates =		SNDRV_PCM_RATE_CONTINUOUS | SNDRV_PCM_RATE_8000_48000,
	.rate_min =		5500,
	.rate_max =		48000,
	.channels_min =		2,
	.channels_max =		2,
	.buffer_bytes_max =	(256 * 1024),
	.period_bytes_min =	CS46XX_MIN_PERIOD_SIZE,
	.period_bytes_max =	CS46XX_MAX_PERIOD_SIZE,
	.periods_min =		CS46XX_FRAGS,
	.periods_max =		1024,
	.fifo_size =		0,
};

#ifdef CONFIG_SND_CS46XX_NEW_DSP

static const unsigned int period_sizes[] = { 32, 64, 128, 256, 512, 1024, 2048 };

static const struct snd_pcm_hw_constraint_list hw_constraints_period_sizes = {
	.count = ARRAY_SIZE(period_sizes),
	.list = period_sizes,
	.mask = 0
};

#endif

static void snd_cs46xx_pcm_free_substream(struct snd_pcm_runtime *runtime)
{
	kfree(runtime->private_data);
}

static int _cs46xx_playback_open_channel (struct snd_pcm_substream *substream,int pcm_channel_id)
{
	struct snd_cs46xx *chip = snd_pcm_substream_chip(substream);
	struct snd_cs46xx_pcm * cpcm;
	struct snd_pcm_runtime *runtime = substream->runtime;

	cpcm = kzalloc(sizeof(*cpcm), GFP_KERNEL);
	if (cpcm == NULL)
		return -ENOMEM;
	if (snd_dma_alloc_pages(SNDRV_DMA_TYPE_DEV, snd_dma_pci_data(chip->pci),
				PAGE_SIZE, &cpcm->hw_buf) < 0) {
		kfree(cpcm);
		return -ENOMEM;
	}

	runtime->hw = snd_cs46xx_playback;
	runtime->private_data = cpcm;
	runtime->private_free = snd_cs46xx_pcm_free_substream;

	cpcm->substream = substream;
#ifdef CONFIG_SND_CS46XX_NEW_DSP
	mutex_lock(&chip->spos_mutex);
	cpcm->pcm_channel = NULL; 
	cpcm->pcm_channel_id = pcm_channel_id;


	snd_pcm_hw_constraint_list(runtime, 0,
				   SNDRV_PCM_HW_PARAM_PERIOD_BYTES, 
				   &hw_constraints_period_sizes);

	mutex_unlock(&chip->spos_mutex);
#else
	chip->playback_pcm = cpcm; /* HACK */
#endif

	if (chip->accept_valid)
		substream->runtime->hw.info |= SNDRV_PCM_INFO_MMAP_VALID;
	chip->active_ctrl(chip, 1);

	return 0;
}

static int snd_cs46xx_playback_open(struct snd_pcm_substream *substream)
{
	dev_dbg(substream->pcm->card->dev, "open front channel\n");
	return _cs46xx_playback_open_channel(substream,DSP_PCM_MAIN_CHANNEL);
}

#ifdef CONFIG_SND_CS46XX_NEW_DSP
static int snd_cs46xx_playback_open_rear(struct snd_pcm_substream *substream)
{
	dev_dbg(substream->pcm->card->dev, "open rear channel\n");
	return _cs46xx_playback_open_channel(substream,DSP_PCM_REAR_CHANNEL);
}

static int snd_cs46xx_playback_open_clfe(struct snd_pcm_substream *substream)
{
	dev_dbg(substream->pcm->card->dev, "open center - LFE channel\n");
	return _cs46xx_playback_open_channel(substream,DSP_PCM_CENTER_LFE_CHANNEL);
}

static int snd_cs46xx_playback_open_iec958(struct snd_pcm_substream *substream)
{
	struct snd_cs46xx *chip = snd_pcm_substream_chip(substream);

	dev_dbg(chip->card->dev, "open raw iec958 channel\n");

	mutex_lock(&chip->spos_mutex);
	cs46xx_iec958_pre_open (chip);
	mutex_unlock(&chip->spos_mutex);

	return _cs46xx_playback_open_channel(substream,DSP_IEC958_CHANNEL);
}

static int snd_cs46xx_playback_close(struct snd_pcm_substream *substream);

static int snd_cs46xx_playback_close_iec958(struct snd_pcm_substream *substream)
{
	int err;
	struct snd_cs46xx *chip = snd_pcm_substream_chip(substream);
  
	dev_dbg(chip->card->dev, "close raw iec958 channel\n");

	err = snd_cs46xx_playback_close(substream);

	mutex_lock(&chip->spos_mutex);
	cs46xx_iec958_post_close (chip);
	mutex_unlock(&chip->spos_mutex);

	return err;
}
#endif

static int snd_cs46xx_capture_open(struct snd_pcm_substream *substream)
{
	struct snd_cs46xx *chip = snd_pcm_substream_chip(substream);

	if (snd_dma_alloc_pages(SNDRV_DMA_TYPE_DEV, snd_dma_pci_data(chip->pci),
				PAGE_SIZE, &chip->capt.hw_buf) < 0)
		return -ENOMEM;
	chip->capt.substream = substream;
	substream->runtime->hw = snd_cs46xx_capture;

	if (chip->accept_valid)
		substream->runtime->hw.info |= SNDRV_PCM_INFO_MMAP_VALID;

	chip->active_ctrl(chip, 1);

#ifdef CONFIG_SND_CS46XX_NEW_DSP
	snd_pcm_hw_constraint_list(substream->runtime, 0,
				   SNDRV_PCM_HW_PARAM_PERIOD_BYTES, 
				   &hw_constraints_period_sizes);
#endif
	return 0;
}

static int snd_cs46xx_playback_close(struct snd_pcm_substream *substream)
{
	struct snd_cs46xx *chip = snd_pcm_substream_chip(substream);
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct snd_cs46xx_pcm * cpcm;

	cpcm = runtime->private_data;

	/* when playback_open fails, then cpcm can be NULL */
	if (!cpcm) return -ENXIO;

#ifdef CONFIG_SND_CS46XX_NEW_DSP
	mutex_lock(&chip->spos_mutex);
	if (cpcm->pcm_channel) {
		cs46xx_dsp_destroy_pcm_channel(chip,cpcm->pcm_channel);
		cpcm->pcm_channel = NULL;
	}
	mutex_unlock(&chip->spos_mutex);
#else
	chip->playback_pcm = NULL;
#endif

	cpcm->substream = NULL;
	snd_dma_free_pages(&cpcm->hw_buf);
	chip->active_ctrl(chip, -1);

	return 0;
}

static int snd_cs46xx_capture_close(struct snd_pcm_substream *substream)
{
	struct snd_cs46xx *chip = snd_pcm_substream_chip(substream);

	chip->capt.substream = NULL;
	snd_dma_free_pages(&chip->capt.hw_buf);
	chip->active_ctrl(chip, -1);

	return 0;
}

#ifdef CONFIG_SND_CS46XX_NEW_DSP
static const struct snd_pcm_ops snd_cs46xx_playback_rear_ops = {
	.open =			snd_cs46xx_playback_open_rear,
	.close =		snd_cs46xx_playback_close,
	.ioctl =		snd_pcm_lib_ioctl,
	.hw_params =		snd_cs46xx_playback_hw_params,
	.hw_free =		snd_cs46xx_playback_hw_free,
	.prepare =		snd_cs46xx_playback_prepare,
	.trigger =		snd_cs46xx_playback_trigger,
	.pointer =		snd_cs46xx_playback_direct_pointer,
};

static const struct snd_pcm_ops snd_cs46xx_playback_indirect_rear_ops = {
	.open =			snd_cs46xx_playback_open_rear,
	.close =		snd_cs46xx_playback_close,
	.ioctl =		snd_pcm_lib_ioctl,
	.hw_params =		snd_cs46xx_playback_hw_params,
	.hw_free =		snd_cs46xx_playback_hw_free,
	.prepare =		snd_cs46xx_playback_prepare,
	.trigger =		snd_cs46xx_playback_trigger,
	.pointer =		snd_cs46xx_playback_indirect_pointer,
	.ack =			snd_cs46xx_playback_transfer,
};

static const struct snd_pcm_ops snd_cs46xx_playback_clfe_ops = {
	.open =			snd_cs46xx_playback_open_clfe,
	.close =		snd_cs46xx_playback_close,
	.ioctl =		snd_pcm_lib_ioctl,
	.hw_params =		snd_cs46xx_playback_hw_params,
	.hw_free =		snd_cs46xx_playback_hw_free,
	.prepare =		snd_cs46xx_playback_prepare,
	.trigger =		snd_cs46xx_playback_trigger,
	.pointer =		snd_cs46xx_playback_direct_pointer,
};

static const struct snd_pcm_ops snd_cs46xx_playback_indirect_clfe_ops = {
	.open =			snd_cs46xx_playback_open_clfe,
	.close =		snd_cs46xx_playback_close,
	.ioctl =		snd_pcm_lib_ioctl,
	.hw_params =		snd_cs46xx_playback_hw_params,
	.hw_free =		snd_cs46xx_playback_hw_free,
	.prepare =		snd_cs46xx_playback_prepare,
	.trigger =		snd_cs46xx_playback_trigger,
	.pointer =		snd_cs46xx_playback_indirect_pointer,
	.ack =			snd_cs46xx_playback_transfer,
};

static const struct snd_pcm_ops snd_cs46xx_playback_iec958_ops = {
	.open =			snd_cs46xx_playback_open_iec958,
	.close =		snd_cs46xx_playback_close_iec958,
	.ioctl =		snd_pcm_lib_ioctl,
	.hw_params =		snd_cs46xx_playback_hw_params,
	.hw_free =		snd_cs46xx_playback_hw_free,
	.prepare =		snd_cs46xx_playback_prepare,
	.trigger =		snd_cs46xx_playback_trigger,
	.pointer =		snd_cs46xx_playback_direct_pointer,
};

static const struct snd_pcm_ops snd_cs46xx_playback_indirect_iec958_ops = {
	.open =			snd_cs46xx_playback_open_iec958,
	.close =		snd_cs46xx_playback_close_iec958,
	.ioctl =		snd_pcm_lib_ioctl,
	.hw_params =		snd_cs46xx_playback_hw_params,
	.hw_free =		snd_cs46xx_playback_hw_free,
	.prepare =		snd_cs46xx_playback_prepare,
	.trigger =		snd_cs46xx_playback_trigger,
	.pointer =		snd_cs46xx_playback_indirect_pointer,
	.ack =			snd_cs46xx_playback_transfer,
};

#endif

static const struct snd_pcm_ops snd_cs46xx_playback_ops = {
	.open =			snd_cs46xx_playback_open,
	.close =		snd_cs46xx_playback_close,
	.ioctl =		snd_pcm_lib_ioctl,
	.hw_params =		snd_cs46xx_playback_hw_params,
	.hw_free =		snd_cs46xx_playback_hw_free,
	.prepare =		snd_cs46xx_playback_prepare,
	.trigger =		snd_cs46xx_playback_trigger,
	.pointer =		snd_cs46xx_playback_direct_pointer,
};

static const struct snd_pcm_ops snd_cs46xx_playback_indirect_ops = {
	.open =			snd_cs46xx_playback_open,
	.close =		snd_cs46xx_playback_close,
	.ioctl =		snd_pcm_lib_ioctl,
	.hw_params =		snd_cs46xx_playback_hw_params,
	.hw_free =		snd_cs46xx_playback_hw_free,
	.prepare =		snd_cs46xx_playback_prepare,
	.trigger =		snd_cs46xx_playback_trigger,
	.pointer =		snd_cs46xx_playback_indirect_pointer,
	.ack =			snd_cs46xx_playback_transfer,
};

static const struct snd_pcm_ops snd_cs46xx_capture_ops = {
	.open =			snd_cs46xx_capture_open,
	.close =		snd_cs46xx_capture_close,
	.ioctl =		snd_pcm_lib_ioctl,
	.hw_params =		snd_cs46xx_capture_hw_params,
	.hw_free =		snd_cs46xx_capture_hw_free,
	.prepare =		snd_cs46xx_capture_prepare,
	.trigger =		snd_cs46xx_capture_trigger,
	.pointer =		snd_cs46xx_capture_direct_pointer,
};

static const struct snd_pcm_ops snd_cs46xx_capture_indirect_ops = {
	.open =			snd_cs46xx_capture_open,
	.close =		snd_cs46xx_capture_close,
	.ioctl =		snd_pcm_lib_ioctl,
	.hw_params =		snd_cs46xx_capture_hw_params,
	.hw_free =		snd_cs46xx_capture_hw_free,
	.prepare =		snd_cs46xx_capture_prepare,
	.trigger =		snd_cs46xx_capture_trigger,
	.pointer =		snd_cs46xx_capture_indirect_pointer,
	.ack =			snd_cs46xx_capture_transfer,
};

#ifdef CONFIG_SND_CS46XX_NEW_DSP
#define MAX_PLAYBACK_CHANNELS	(DSP_MAX_PCM_CHANNELS - 1)
#else
#define MAX_PLAYBACK_CHANNELS	1
#endif

int snd_cs46xx_pcm(struct snd_cs46xx *chip, int device)
{
	struct snd_pcm *pcm;
	int err;

	if ((err = snd_pcm_new(chip->card, "CS46xx", device, MAX_PLAYBACK_CHANNELS, 1, &pcm)) < 0)
		return err;

	pcm->private_data = chip;

	snd_pcm_set_ops(pcm, SNDRV_PCM_STREAM_PLAYBACK, &snd_cs46xx_playback_ops);
	snd_pcm_set_ops(pcm, SNDRV_PCM_STREAM_CAPTURE, &snd_cs46xx_capture_ops);

	/* global setup */
	pcm->info_flags = 0;
	strcpy(pcm->name, "CS46xx");
	chip->pcm = pcm;

	snd_pcm_lib_preallocate_pages_for_all(pcm, SNDRV_DMA_TYPE_DEV,
					      snd_dma_pci_data(chip->pci), 64*1024, 256*1024);

	return 0;
}


#ifdef CONFIG_SND_CS46XX_NEW_DSP
int snd_cs46xx_pcm_rear(struct snd_cs46xx *chip, int device)
{
	struct snd_pcm *pcm;
	int err;

	if ((err = snd_pcm_new(chip->card, "CS46xx - Rear", device, MAX_PLAYBACK_CHANNELS, 0, &pcm)) < 0)
		return err;

	pcm->private_data = chip;

	snd_pcm_set_ops(pcm, SNDRV_PCM_STREAM_PLAYBACK, &snd_cs46xx_playback_rear_ops);

	/* global setup */
	pcm->info_flags = 0;
	strcpy(pcm->name, "CS46xx - Rear");
	chip->pcm_rear = pcm;

	snd_pcm_lib_preallocate_pages_for_all(pcm, SNDRV_DMA_TYPE_DEV,
					      snd_dma_pci_data(chip->pci), 64*1024, 256*1024);

	return 0;
}

int snd_cs46xx_pcm_center_lfe(struct snd_cs46xx *chip, int device)
{
	struct snd_pcm *pcm;
	int err;

	if ((err = snd_pcm_new(chip->card, "CS46xx - Center LFE", device, MAX_PLAYBACK_CHANNELS, 0, &pcm)) < 0)
		return err;

	pcm->private_data = chip;

	snd_pcm_set_ops(pcm, SNDRV_PCM_STREAM_PLAYBACK, &snd_cs46xx_playback_clfe_ops);

	/* global setup */
	pcm->info_flags = 0;
	strcpy(pcm->name, "CS46xx - Center LFE");
	chip->pcm_center_lfe = pcm;

	snd_pcm_lib_preallocate_pages_for_all(pcm, SNDRV_DMA_TYPE_DEV,
					      snd_dma_pci_data(chip->pci), 64*1024, 256*1024);

	return 0;
}

int snd_cs46xx_pcm_iec958(struct snd_cs46xx *chip, int device)
{
	struct snd_pcm *pcm;
	int err;

	if ((err = snd_pcm_new(chip->card, "CS46xx - IEC958", device, 1, 0, &pcm)) < 0)
		return err;

	pcm->private_data = chip;

	snd_pcm_set_ops(pcm, SNDRV_PCM_STREAM_PLAYBACK, &snd_cs46xx_playback_iec958_ops);

	/* global setup */
	pcm->info_flags = 0;
	strcpy(pcm->name, "CS46xx - IEC958");
	chip->pcm_iec958 = pcm;

	snd_pcm_lib_preallocate_pages_for_all(pcm, SNDRV_DMA_TYPE_DEV,
					      snd_dma_pci_data(chip->pci), 64*1024, 256*1024);

	return 0;
}
#endif

/*
 *  Mixer routines
 */
static void snd_cs46xx_mixer_free_ac97_bus(struct snd_ac97_bus *bus)
{
	struct snd_cs46xx *chip = bus->private_data;

	chip->ac97_bus = NULL;
}

static void snd_cs46xx_mixer_free_ac97(struct snd_ac97 *ac97)
{
	struct snd_cs46xx *chip = ac97->private_data;

	if (snd_BUG_ON(ac97 != chip->ac97[CS46XX_PRIMARY_CODEC_INDEX] &&
		       ac97 != chip->ac97[CS46XX_SECONDARY_CODEC_INDEX]))
		return;

	if (ac97 == chip->ac97[CS46XX_PRIMARY_CODEC_INDEX]) {
		chip->ac97[CS46XX_PRIMARY_CODEC_INDEX] = NULL;
		chip->eapd_switch = NULL;
	}
	else
		chip->ac97[CS46XX_SECONDARY_CODEC_INDEX] = NULL;
}

static int snd_cs46xx_vol_info(struct snd_kcontrol *kcontrol, 
			       struct snd_ctl_elem_info *uinfo)
{
	uinfo->type = SNDRV_CTL_ELEM_TYPE_INTEGER;
	uinfo->count = 2;
	uinfo->value.integer.min = 0;
	uinfo->value.integer.max = 0x7fff;
	return 0;
}

static int snd_cs46xx_vol_get(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_value *ucontrol)
{
	struct snd_cs46xx *chip = snd_kcontrol_chip(kcontrol);
	int reg = kcontrol->private_value;
	unsigned int val = snd_cs46xx_peek(chip, reg);
	ucontrol->value.integer.value[0] = 0xffff - (val >> 16);
	ucontrol->value.integer.value[1] = 0xffff - (val & 0xffff);
	return 0;
}

static int snd_cs46xx_vol_put(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_value *ucontrol)
{
	struct snd_cs46xx *chip = snd_kcontrol_chip(kcontrol);
	int reg = kcontrol->private_value;
	unsigned int val = ((0xffff - ucontrol->value.integer.value[0]) << 16 | 
			    (0xffff - ucontrol->value.integer.value[1]));
	unsigned int old = snd_cs46xx_peek(chip, reg);
	int change = (old != val);

	if (change) {
		snd_cs46xx_poke(chip, reg, val);
	}

	return change;
}

#ifdef CONFIG_SND_CS46XX_NEW_DSP

static int snd_cs46xx_vol_dac_get(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_value *ucontrol)
{
	struct snd_cs46xx *chip = snd_kcontrol_chip(kcontrol);

	ucontrol->value.integer.value[0] = chip->dsp_spos_instance->dac_volume_left;
	ucontrol->value.integer.value[1] = chip->dsp_spos_instance->dac_volume_right;

	return 0;
}

static int snd_cs46xx_vol_dac_put(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_value *ucontrol)
{
	struct snd_cs46xx *chip = snd_kcontrol_chip(kcontrol);
	int change = 0;

	if (chip->dsp_spos_instance->dac_volume_right != ucontrol->value.integer.value[0] ||
	    chip->dsp_spos_instance->dac_volume_left != ucontrol->value.integer.value[1]) {
		cs46xx_dsp_set_dac_volume(chip,
					  ucontrol->value.integer.value[0],
					  ucontrol->value.integer.value[1]);
		change = 1;
	}

	return change;
}

#if 0
static int snd_cs46xx_vol_iec958_get(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_value *ucontrol)
{
	struct snd_cs46xx *chip = snd_kcontrol_chip(kcontrol);

	ucontrol->value.integer.value[0] = chip->dsp_spos_instance->spdif_input_volume_left;
	ucontrol->value.integer.value[1] = chip->dsp_spos_instance->spdif_input_volume_right;
	return 0;
}

static int snd_cs46xx_vol_iec958_put(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_value *ucontrol)
{
	struct snd_cs46xx *chip = snd_kcontrol_chip(kcontrol);
	int change = 0;

	if (chip->dsp_spos_instance->spdif_input_volume_left  != ucontrol->value.integer.value[0] ||
	    chip->dsp_spos_instance->spdif_input_volume_right!= ucontrol->value.integer.value[1]) {
		cs46xx_dsp_set_iec958_volume (chip,
					      ucontrol->value.integer.value[0],
					      ucontrol->value.integer.value[1]);
		change = 1;
	}

	return change;
}
#endif

#define snd_mixer_boolean_info		snd_ctl_boolean_mono_info

static int snd_cs46xx_iec958_get(struct snd_kcontrol *kcontrol, 
                                 struct snd_ctl_elem_value *ucontrol)
{
	struct snd_cs46xx *chip = snd_kcontrol_chip(kcontrol);
	int reg = kcontrol->private_value;

	if (reg == CS46XX_MIXER_SPDIF_OUTPUT_ELEMENT)
		ucontrol->value.integer.value[0] = (chip->dsp_spos_instance->spdif_status_out & DSP_SPDIF_STATUS_OUTPUT_ENABLED);
	else
		ucontrol->value.integer.value[0] = chip->dsp_spos_instance->spdif_status_in;

	return 0;
}

static int snd_cs46xx_iec958_put(struct snd_kcontrol *kcontrol, 
                                  struct snd_ctl_elem_value *ucontrol)
{
	struct snd_cs46xx *chip = snd_kcontrol_chip(kcontrol);
	int change, res;

	switch (kcontrol->private_value) {
	case CS46XX_MIXER_SPDIF_OUTPUT_ELEMENT:
		mutex_lock(&chip->spos_mutex);
		change = (chip->dsp_spos_instance->spdif_status_out & DSP_SPDIF_STATUS_OUTPUT_ENABLED);
		if (ucontrol->value.integer.value[0] && !change) 
			cs46xx_dsp_enable_spdif_out(chip);
		else if (change && !ucontrol->value.integer.value[0])
			cs46xx_dsp_disable_spdif_out(chip);

		res = (change != (chip->dsp_spos_instance->spdif_status_out & DSP_SPDIF_STATUS_OUTPUT_ENABLED));
		mutex_unlock(&chip->spos_mutex);
		break;
	case CS46XX_MIXER_SPDIF_INPUT_ELEMENT:
		change = chip->dsp_spos_instance->spdif_status_in;
		if (ucontrol->value.integer.value[0] && !change) {
			cs46xx_dsp_enable_spdif_in(chip);
			/* restore volume */
		}
		else if (change && !ucontrol->value.integer.value[0])
			cs46xx_dsp_disable_spdif_in(chip);
		
		res = (change != chip->dsp_spos_instance->spdif_status_in);
		break;
	default:
		res = -EINVAL;
		snd_BUG(); /* should never happen ... */
	}

	return res;
}

static int snd_cs46xx_adc_capture_get(struct snd_kcontrol *kcontrol, 
                                      struct snd_ctl_elem_value *ucontrol)
{
	struct snd_cs46xx *chip = snd_kcontrol_chip(kcontrol);
	struct dsp_spos_instance * ins = chip->dsp_spos_instance;

	if (ins->adc_input != NULL) 
		ucontrol->value.integer.value[0] = 1;
	else 
		ucontrol->value.integer.value[0] = 0;
	
	return 0;
}

static int snd_cs46xx_adc_capture_put(struct snd_kcontrol *kcontrol, 
                                      struct snd_ctl_elem_value *ucontrol)
{
	struct snd_cs46xx *chip = snd_kcontrol_chip(kcontrol);
	struct dsp_spos_instance * ins = chip->dsp_spos_instance;
	int change = 0;

	if (ucontrol->value.integer.value[0] && !ins->adc_input) {
		cs46xx_dsp_enable_adc_capture(chip);
		change = 1;
	} else  if (!ucontrol->value.integer.value[0] && ins->adc_input) {
		cs46xx_dsp_disable_adc_capture(chip);
		change = 1;
	}
	return change;
}

static int snd_cs46xx_pcm_capture_get(struct snd_kcontrol *kcontrol, 
                                      struct snd_ctl_elem_value *ucontrol)
{
	struct snd_cs46xx *chip = snd_kcontrol_chip(kcontrol);
	struct dsp_spos_instance * ins = chip->dsp_spos_instance;

	if (ins->pcm_input != NULL) 
		ucontrol->value.integer.value[0] = 1;
	else 
		ucontrol->value.integer.value[0] = 0;

	return 0;
}


static int snd_cs46xx_pcm_capture_put(struct snd_kcontrol *kcontrol, 
                                      struct snd_ctl_elem_value *ucontrol)
{
	struct snd_cs46xx *chip = snd_kcontrol_chip(kcontrol);
	struct dsp_spos_instance * ins = chip->dsp_spos_instance;
	int change = 0;

	if (ucontrol->value.integer.value[0] && !ins->pcm_input) {
		cs46xx_dsp_enable_pcm_capture(chip);
		change = 1;
	} else  if (!ucontrol->value.integer.value[0] && ins->pcm_input) {
		cs46xx_dsp_disable_pcm_capture(chip);
		change = 1;
	}

	return change;
}

static int snd_herc_spdif_select_get(struct snd_kcontrol *kcontrol, 
                                     struct snd_ctl_elem_value *ucontrol)
{
	struct snd_cs46xx *chip = snd_kcontrol_chip(kcontrol);

	int val1 = snd_cs46xx_peekBA0(chip, BA0_EGPIODR);

	if (val1 & EGPIODR_GPOE0)
		ucontrol->value.integer.value[0] = 1;
	else
		ucontrol->value.integer.value[0] = 0;

	return 0;
}

/*
 *	Game Theatre XP card - EGPIO[0] is used to select SPDIF input optical or coaxial.
 */ 
static int snd_herc_spdif_select_put(struct snd_kcontrol *kcontrol, 
                                       struct snd_ctl_elem_value *ucontrol)
{
	struct snd_cs46xx *chip = snd_kcontrol_chip(kcontrol);
	int val1 = snd_cs46xx_peekBA0(chip, BA0_EGPIODR);
	int val2 = snd_cs46xx_peekBA0(chip, BA0_EGPIOPTR);

	if (ucontrol->value.integer.value[0]) {
		/* optical is default */
		snd_cs46xx_pokeBA0(chip, BA0_EGPIODR, 
				   EGPIODR_GPOE0 | val1);  /* enable EGPIO0 output */
		snd_cs46xx_pokeBA0(chip, BA0_EGPIOPTR, 
				   EGPIOPTR_GPPT0 | val2); /* open-drain on output */
	} else {
		/* coaxial */
		snd_cs46xx_pokeBA0(chip, BA0_EGPIODR,  val1 & ~EGPIODR_GPOE0); /* disable */
		snd_cs46xx_pokeBA0(chip, BA0_EGPIOPTR, val2 & ~EGPIOPTR_GPPT0); /* disable */
	}

	/* checking diff from the EGPIO direction register 
	   should be enough */
	return (val1 != (int)snd_cs46xx_peekBA0(chip, BA0_EGPIODR));
}


static int snd_cs46xx_spdif_info(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_info *uinfo)
{
	uinfo->type = SNDRV_CTL_ELEM_TYPE_IEC958;
	uinfo->count = 1;
	return 0;
}

static int snd_cs46xx_spdif_default_get(struct snd_kcontrol *kcontrol,
					struct snd_ctl_elem_value *ucontrol)
{
	struct snd_cs46xx *chip = snd_kcontrol_chip(kcontrol);
	struct dsp_spos_instance * ins = chip->dsp_spos_instance;

	mutex_lock(&chip->spos_mutex);
	ucontrol->value.iec958.status[0] = _wrap_all_bits((ins->spdif_csuv_default >> 24) & 0xff);
	ucontrol->value.iec958.status[1] = _wrap_all_bits((ins->spdif_csuv_default >> 16) & 0xff);
	ucontrol->value.iec958.status[2] = 0;
	ucontrol->value.iec958.status[3] = _wrap_all_bits((ins->spdif_csuv_default) & 0xff);
	mutex_unlock(&chip->spos_mutex);

	return 0;
}

static int snd_cs46xx_spdif_default_put(struct snd_kcontrol *kcontrol,
					struct snd_ctl_elem_value *ucontrol)
{
	struct snd_cs46xx * chip = snd_kcontrol_chip(kcontrol);
	struct dsp_spos_instance * ins = chip->dsp_spos_instance;
	unsigned int val;
	int change;

	mutex_lock(&chip->spos_mutex);
	val = ((unsigned int)_wrap_all_bits(ucontrol->value.iec958.status[0]) << 24) |
		((unsigned int)_wrap_all_bits(ucontrol->value.iec958.status[2]) << 16) |
		((unsigned int)_wrap_all_bits(ucontrol->value.iec958.status[3]))  |
		/* left and right validity bit */
		(1 << 13) | (1 << 12);


	change = (unsigned int)ins->spdif_csuv_default != val;
	ins->spdif_csuv_default = val;

	if ( !(ins->spdif_status_out & DSP_SPDIF_STATUS_PLAYBACK_OPEN) )
		cs46xx_poke_via_dsp (chip,SP_SPDOUT_CSUV,val);

	mutex_unlock(&chip->spos_mutex);

	return change;
}

static int snd_cs46xx_spdif_mask_get(struct snd_kcontrol *kcontrol,
				     struct snd_ctl_elem_value *ucontrol)
{
	ucontrol->value.iec958.status[0] = 0xff;
	ucontrol->value.iec958.status[1] = 0xff;
	ucontrol->value.iec958.status[2] = 0x00;
	ucontrol->value.iec958.status[3] = 0xff;
	return 0;
}

static int snd_cs46xx_spdif_stream_get(struct snd_kcontrol *kcontrol,
                                         struct snd_ctl_elem_value *ucontrol)
{
	struct snd_cs46xx *chip = snd_kcontrol_chip(kcontrol);
	struct dsp_spos_instance * ins = chip->dsp_spos_instance;

	mutex_lock(&chip->spos_mutex);
	ucontrol->value.iec958.status[0] = _wrap_all_bits((ins->spdif_csuv_stream >> 24) & 0xff);
	ucontrol->value.iec958.status[1] = _wrap_all_bits((ins->spdif_csuv_stream >> 16) & 0xff);
	ucontrol->value.iec958.status[2] = 0;
	ucontrol->value.iec958.status[3] = _wrap_all_bits((ins->spdif_csuv_stream) & 0xff);
	mutex_unlock(&chip->spos_mutex);

	return 0;
}

static int snd_cs46xx_spdif_stream_put(struct snd_kcontrol *kcontrol,
                                        struct snd_ctl_elem_value *ucontrol)
{
	struct snd_cs46xx * chip = snd_kcontrol_chip(kcontrol);
	struct dsp_spos_instance * ins = chip->dsp_spos_instance;
	unsigned int val;
	int change;

	mutex_lock(&chip->spos_mutex);
	val = ((unsigned int)_wrap_all_bits(ucontrol->value.iec958.status[0]) << 24) |
		((unsigned int)_wrap_all_bits(ucontrol->value.iec958.status[1]) << 16) |
		((unsigned int)_wrap_all_bits(ucontrol->value.iec958.status[3])) |
		/* left and right validity bit */
		(1 << 13) | (1 << 12);


	change = ins->spdif_csuv_stream != val;
	ins->spdif_csuv_stream = val;

	if ( ins->spdif_status_out & DSP_SPDIF_STATUS_PLAYBACK_OPEN )
		cs46xx_poke_via_dsp (chip,SP_SPDOUT_CSUV,val);

	mutex_unlock(&chip->spos_mutex);

	return change;
}

#endif /* CONFIG_SND_CS46XX_NEW_DSP */


static struct snd_kcontrol_new snd_cs46xx_controls[] = {
{
	.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
	.name = "DAC Volume",
	.info = snd_cs46xx_vol_info,
#ifndef CONFIG_SND_CS46XX_NEW_DSP
	.get = snd_cs46xx_vol_get,
	.put = snd_cs46xx_vol_put,
	.private_value = BA1_PVOL,
#else
	.get = snd_cs46xx_vol_dac_get,
	.put = snd_cs46xx_vol_dac_put,
#endif
},

{
	.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
	.name = "ADC Volume",
	.info = snd_cs46xx_vol_info,
	.get = snd_cs46xx_vol_get,
	.put = snd_cs46xx_vol_put,
#ifndef CONFIG_SND_CS46XX_NEW_DSP
	.private_value = BA1_CVOL,
#else
	.private_value = (VARIDECIMATE_SCB_ADDR + 0xE) << 2,
#endif
},
#ifdef CONFIG_SND_CS46XX_NEW_DSP
{
	.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
	.name = "ADC Capture Switch",
	.info = snd_mixer_boolean_info,
	.get = snd_cs46xx_adc_capture_get,
	.put = snd_cs46xx_adc_capture_put
},
{
	.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
	.name = "DAC Capture Switch",
	.info = snd_mixer_boolean_info,
	.get = snd_cs46xx_pcm_capture_get,
	.put = snd_cs46xx_pcm_capture_put
},
{
	.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
	.name = SNDRV_CTL_NAME_IEC958("Output ",NONE,SWITCH),
	.info = snd_mixer_boolean_info,
	.get = snd_cs46xx_iec958_get,
	.put = snd_cs46xx_iec958_put,
	.private_value = CS46XX_MIXER_SPDIF_OUTPUT_ELEMENT,
},
{
	.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
	.name = SNDRV_CTL_NAME_IEC958("Input ",NONE,SWITCH),
	.info = snd_mixer_boolean_info,
	.get = snd_cs46xx_iec958_get,
	.put = snd_cs46xx_iec958_put,
	.private_value = CS46XX_MIXER_SPDIF_INPUT_ELEMENT,
},
#if 0
/* Input IEC958 volume does not work for the moment. (Benny) */
{
	.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
	.name = SNDRV_CTL_NAME_IEC958("Input ",NONE,VOLUME),
	.info = snd_cs46xx_vol_info,
	.get = snd_cs46xx_vol_iec958_get,
	.put = snd_cs46xx_vol_iec958_put,
	.private_value = (ASYNCRX_SCB_ADDR + 0xE) << 2,
},
#endif
{
	.iface = SNDRV_CTL_ELEM_IFACE_PCM,
	.name =  SNDRV_CTL_NAME_IEC958("",PLAYBACK,DEFAULT),
	.info =	 snd_cs46xx_spdif_info,
	.get =	 snd_cs46xx_spdif_default_get,
	.put =   snd_cs46xx_spdif_default_put,
},
{
	.iface = SNDRV_CTL_ELEM_IFACE_PCM,
	.name =	 SNDRV_CTL_NAME_IEC958("",PLAYBACK,MASK),
	.info =	 snd_cs46xx_spdif_info,
        .get =	 snd_cs46xx_spdif_mask_get,
	.access = SNDRV_CTL_ELEM_ACCESS_READ
},
{
	.iface = SNDRV_CTL_ELEM_IFACE_PCM,
	.name =	 SNDRV_CTL_NAME_IEC958("",PLAYBACK,PCM_STREAM),
	.info =	 snd_cs46xx_spdif_info,
	.get =	 snd_cs46xx_spdif_stream_get,
	.put =	 snd_cs46xx_spdif_stream_put
},

#endif
};

#ifdef CONFIG_SND_CS46XX_NEW_DSP
/* set primary cs4294 codec into Extended Audio Mode */
static int snd_cs46xx_front_dup_get(struct snd_kcontrol *kcontrol, 
				    struct snd_ctl_elem_value *ucontrol)
{
	struct snd_cs46xx *chip = snd_kcontrol_chip(kcontrol);
	unsigned short val;
	val = snd_ac97_read(chip->ac97[CS46XX_PRIMARY_CODEC_INDEX], AC97_CSR_ACMODE);
	ucontrol->value.integer.value[0] = (val & 0x200) ? 0 : 1;
	return 0;
}

static int snd_cs46xx_front_dup_put(struct snd_kcontrol *kcontrol, 
				    struct snd_ctl_elem_value *ucontrol)
{
	struct snd_cs46xx *chip = snd_kcontrol_chip(kcontrol);
	return snd_ac97_update_bits(chip->ac97[CS46XX_PRIMARY_CODEC_INDEX],
				    AC97_CSR_ACMODE, 0x200,
				    ucontrol->value.integer.value[0] ? 0 : 0x200);
}

static const struct snd_kcontrol_new snd_cs46xx_front_dup_ctl = {
	.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
	.name = "Duplicate Front",
	.info = snd_mixer_boolean_info,
	.get = snd_cs46xx_front_dup_get,
	.put = snd_cs46xx_front_dup_put,
};
#endif

#ifdef CONFIG_SND_CS46XX_NEW_DSP
/* Only available on the Hercules Game Theater XP soundcard */
static struct snd_kcontrol_new snd_hercules_controls[] = {
{
	.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
	.name = "Optical/Coaxial SPDIF Input Switch",
	.info = snd_mixer_boolean_info,
	.get = snd_herc_spdif_select_get,
	.put = snd_herc_spdif_select_put,
},
};


static void snd_cs46xx_codec_reset (struct snd_ac97 * ac97)
{
	unsigned long end_time;
	int err;

	/* reset to defaults */
	snd_ac97_write(ac97, AC97_RESET, 0);	

	/* set the desired CODEC mode */
	if (ac97->num == CS46XX_PRIMARY_CODEC_INDEX) {
		dev_dbg(ac97->bus->card->dev, "CODEC1 mode %04x\n", 0x0);
		snd_cs46xx_ac97_write(ac97, AC97_CSR_ACMODE, 0x0);
	} else if (ac97->num == CS46XX_SECONDARY_CODEC_INDEX) {
		dev_dbg(ac97->bus->card->dev, "CODEC2 mode %04x\n", 0x3);
		snd_cs46xx_ac97_write(ac97, AC97_CSR_ACMODE, 0x3);
	} else {
		snd_BUG(); /* should never happen ... */
	}

	udelay(50);

	/* it's necessary to wait awhile until registers are accessible after RESET */
	/* because the PCM or MASTER volume registers can be modified, */
	/* the REC_GAIN register is used for tests */
	end_time = jiffies + HZ;
	do {
		unsigned short ext_mid;
    
		/* use preliminary reads to settle the communication */
		snd_ac97_read(ac97, AC97_RESET);
		snd_ac97_read(ac97, AC97_VENDOR_ID1);
		snd_ac97_read(ac97, AC97_VENDOR_ID2);
		/* modem? */
		ext_mid = snd_ac97_read(ac97, AC97_EXTENDED_MID);
		if (ext_mid != 0xffff && (ext_mid & 1) != 0)
			return;

		/* test if we can write to the record gain volume register */
		snd_ac97_write(ac97, AC97_REC_GAIN, 0x8a05);
		if ((err = snd_ac97_read(ac97, AC97_REC_GAIN)) == 0x8a05)
			return;

		msleep(10);
	} while (time_after_eq(end_time, jiffies));

	dev_err(ac97->bus->card->dev,
		"CS46xx secondary codec doesn't respond!\n");
}
#endif

static int cs46xx_detect_codec(struct snd_cs46xx *chip, int codec)
{
	int idx, err;
	struct snd_ac97_template ac97;

	memset(&ac97, 0, sizeof(ac97));
	ac97.private_data = chip;
	ac97.private_free = snd_cs46xx_mixer_free_ac97;
	ac97.num = codec;
	if (chip->amplifier_ctrl == amp_voyetra)
		ac97.scaps = AC97_SCAP_INV_EAPD;

	if (codec == CS46XX_SECONDARY_CODEC_INDEX) {
		snd_cs46xx_codec_write(chip, AC97_RESET, 0, codec);
		udelay(10);
		if (snd_cs46xx_codec_read(chip, AC97_RESET, codec) & 0x8000) {
			dev_dbg(chip->card->dev,
				"secondary codec not present\n");
			return -ENXIO;
		}
	}

	snd_cs46xx_codec_write(chip, AC97_MASTER, 0x8000, codec);
	for (idx = 0; idx < 100; ++idx) {
		if (snd_cs46xx_codec_read(chip, AC97_MASTER, codec) == 0x8000) {
			err = snd_ac97_mixer(chip->ac97_bus, &ac97, &chip->ac97[codec]);
			return err;
		}
		msleep(10);
	}
	dev_dbg(chip->card->dev, "codec %d detection timeout\n", codec);
	return -ENXIO;
}

int snd_cs46xx_mixer(struct snd_cs46xx *chip, int spdif_device)
{
	struct snd_card *card = chip->card;
	struct snd_ctl_elem_id id;
	int err;
	unsigned int idx;
	static struct snd_ac97_bus_ops ops = {
#ifdef CONFIG_SND_CS46XX_NEW_DSP
		.reset = snd_cs46xx_codec_reset,
#endif
		.write = snd_cs46xx_ac97_write,
		.read = snd_cs46xx_ac97_read,
	};

	/* detect primary codec */
	chip->nr_ac97_codecs = 0;
	dev_dbg(chip->card->dev, "detecting primary codec\n");
	if ((err = snd_ac97_bus(card, 0, &ops, chip, &chip->ac97_bus)) < 0)
		return err;
	chip->ac97_bus->private_free = snd_cs46xx_mixer_free_ac97_bus;

	if (cs46xx_detect_codec(chip, CS46XX_PRIMARY_CODEC_INDEX) < 0)
		return -ENXIO;
	chip->nr_ac97_codecs = 1;

#ifdef CONFIG_SND_CS46XX_NEW_DSP
	dev_dbg(chip->card->dev, "detecting secondary codec\n");
	/* try detect a secondary codec */
	if (! cs46xx_detect_codec(chip, CS46XX_SECONDARY_CODEC_INDEX))
		chip->nr_ac97_codecs = 2;
#endif /* CONFIG_SND_CS46XX_NEW_DSP */

	/* add cs4630 mixer controls */
	for (idx = 0; idx < ARRAY_SIZE(snd_cs46xx_controls); idx++) {
		struct snd_kcontrol *kctl;
		kctl = snd_ctl_new1(&snd_cs46xx_controls[idx], chip);
		if (kctl && kctl->id.iface == SNDRV_CTL_ELEM_IFACE_PCM)
			kctl->id.device = spdif_device;
		if ((err = snd_ctl_add(card, kctl)) < 0)
			return err;
	}

	/* get EAPD mixer switch (for voyetra hack) */
	memset(&id, 0, sizeof(id));
	id.iface = SNDRV_CTL_ELEM_IFACE_MIXER;
	strcpy(id.name, "External Amplifier");
	chip->eapd_switch = snd_ctl_find_id(chip->card, &id);
    
#ifdef CONFIG_SND_CS46XX_NEW_DSP
	if (chip->nr_ac97_codecs == 1) {
		unsigned int id2 = chip->ac97[CS46XX_PRIMARY_CODEC_INDEX]->id & 0xffff;
		if ((id2 & 0xfff0) == 0x5920) {	/* CS4294 and CS4298 */
			err = snd_ctl_add(card, snd_ctl_new1(&snd_cs46xx_front_dup_ctl, chip));
			if (err < 0)
				return err;
			snd_ac97_write_cache(chip->ac97[CS46XX_PRIMARY_CODEC_INDEX],
					     AC97_CSR_ACMODE, 0x200);
		}
	}
	/* do soundcard specific mixer setup */
	if (chip->mixer_init) {
		dev_dbg(chip->card->dev, "calling chip->mixer_init(chip);\n");
		chip->mixer_init(chip);
	}
#endif

 	/* turn on amplifier */
	chip->amplifier_ctrl(chip, 1);
    
	return 0;
}

/*
 *  RawMIDI interface
 */

static void snd_cs46xx_midi_reset(struct snd_cs46xx *chip)
{
	snd_cs46xx_pokeBA0(chip, BA0_MIDCR, MIDCR_MRST);
	udelay(100);
	snd_cs46xx_pokeBA0(chip, BA0_MIDCR, chip->midcr);
}

static int snd_cs46xx_midi_input_open(struct snd_rawmidi_substream *substream)
{
	struct snd_cs46xx *chip = substream->rmidi->private_data;

	chip->active_ctrl(chip, 1);
	spin_lock_irq(&chip->reg_lock);
	chip->uartm |= CS46XX_MODE_INPUT;
	chip->midcr |= MIDCR_RXE;
	chip->midi_input = substream;
	if (!(chip->uartm & CS46XX_MODE_OUTPUT)) {
		snd_cs46xx_midi_reset(chip);
	} else {
		snd_cs46xx_pokeBA0(chip, BA0_MIDCR, chip->midcr);
	}
	spin_unlock_irq(&chip->reg_lock);
	return 0;
}

static int snd_cs46xx_midi_input_close(struct snd_rawmidi_substream *substream)
{
	struct snd_cs46xx *chip = substream->rmidi->private_data;

	spin_lock_irq(&chip->reg_lock);
	chip->midcr &= ~(MIDCR_RXE | MIDCR_RIE);
	chip->midi_input = NULL;
	if (!(chip->uartm & CS46XX_MODE_OUTPUT)) {
		snd_cs46xx_midi_reset(chip);
	} else {
		snd_cs46xx_pokeBA0(chip, BA0_MIDCR, chip->midcr);
	}
	chip->uartm &= ~CS46XX_MODE_INPUT;
	spin_unlock_irq(&chip->reg_lock);
	chip->active_ctrl(chip, -1);
	return 0;
}

static int snd_cs46xx_midi_output_open(struct snd_rawmidi_substream *substream)
{
	struct snd_cs46xx *chip = substream->rmidi->private_data;

	chip->active_ctrl(chip, 1);

	spin_lock_irq(&chip->reg_lock);
	chip->uartm |= CS46XX_MODE_OUTPUT;
	chip->midcr |= MIDCR_TXE;
	chip->midi_output = substream;
	if (!(chip->uartm & CS46XX_MODE_INPUT)) {
		snd_cs46xx_midi_reset(chip);
	} else {
		snd_cs46xx_pokeBA0(chip, BA0_MIDCR, chip->midcr);
	}
	spin_unlock_irq(&chip->reg_lock);
	return 0;
}

static int snd_cs46xx_midi_output_close(struct snd_rawmidi_substream *substream)
{
	struct snd_cs46xx *chip = substream->rmidi->private_data;

	spin_lock_irq(&chip->reg_lock);
	chip->midcr &= ~(MIDCR_TXE | MIDCR_TIE);
	chip->midi_output = NULL;
	if (!(chip->uartm & CS46XX_MODE_INPUT)) {
		snd_cs46xx_midi_reset(chip);
	} else {
		snd_cs46xx_pokeBA0(chip, BA0_MIDCR, chip->midcr);
	}
	chip->uartm &= ~CS46XX_MODE_OUTPUT;
	spin_unlock_irq(&chip->reg_lock);
	chip->active_ctrl(chip, -1);
	return 0;
}

static void snd_cs46xx_midi_input_trigger(struct snd_rawmidi_substream *substream, int up)
{
	unsigned long flags;
	struct snd_cs46xx *chip = substream->rmidi->private_data;

	spin_lock_irqsave(&chip->reg_lock, flags);
	if (up) {
		if ((chip->midcr & MIDCR_RIE) == 0) {
			chip->midcr |= MIDCR_RIE;
			snd_cs46xx_pokeBA0(chip, BA0_MIDCR, chip->midcr);
		}
	} else {
		if (chip->midcr & MIDCR_RIE) {
			chip->midcr &= ~MIDCR_RIE;
			snd_cs46xx_pokeBA0(chip, BA0_MIDCR, chip->midcr);
		}
	}
	spin_unlock_irqrestore(&chip->reg_lock, flags);
}

static void snd_cs46xx_midi_output_trigger(struct snd_rawmidi_substream *substream, int up)
{
	unsigned long flags;
	struct snd_cs46xx *chip = substream->rmidi->private_data;
	unsigned char byte;

	spin_lock_irqsave(&chip->reg_lock, flags);
	if (up) {
		if ((chip->midcr & MIDCR_TIE) == 0) {
			chip->midcr |= MIDCR_TIE;
			/* fill UART FIFO buffer at first, and turn Tx interrupts only if necessary */
			while ((chip->midcr & MIDCR_TIE) &&
			       (snd_cs46xx_peekBA0(chip, BA0_MIDSR) & MIDSR_TBF) == 0) {
				if (snd_rawmidi_transmit(substream, &byte, 1) != 1) {
					chip->midcr &= ~MIDCR_TIE;
				} else {
					snd_cs46xx_pokeBA0(chip, BA0_MIDWP, byte);
				}
			}
			snd_cs46xx_pokeBA0(chip, BA0_MIDCR, chip->midcr);
		}
	} else {
		if (chip->midcr & MIDCR_TIE) {
			chip->midcr &= ~MIDCR_TIE;
			snd_cs46xx_pokeBA0(chip, BA0_MIDCR, chip->midcr);
		}
	}
	spin_unlock_irqrestore(&chip->reg_lock, flags);
}

static const struct snd_rawmidi_ops snd_cs46xx_midi_output =
{
	.open =		snd_cs46xx_midi_output_open,
	.close =	snd_cs46xx_midi_output_close,
	.trigger =	snd_cs46xx_midi_output_trigger,
};

static const struct snd_rawmidi_ops snd_cs46xx_midi_input =
{
	.open =		snd_cs46xx_midi_input_open,
	.close =	snd_cs46xx_midi_input_close,
	.trigger =	snd_cs46xx_midi_input_trigger,
};

int snd_cs46xx_midi(struct snd_cs46xx *chip, int device)
{
	struct snd_rawmidi *rmidi;
	int err;

	if ((err = snd_rawmidi_new(chip->card, "CS46XX", device, 1, 1, &rmidi)) < 0)
		return err;
	strcpy(rmidi->name, "CS46XX");
	snd_rawmidi_set_ops(rmidi, SNDRV_RAWMIDI_STREAM_OUTPUT, &snd_cs46xx_midi_output);
	snd_rawmidi_set_ops(rmidi, SNDRV_RAWMIDI_STREAM_INPUT, &snd_cs46xx_midi_input);
	rmidi->info_flags |= SNDRV_RAWMIDI_INFO_OUTPUT | SNDRV_RAWMIDI_INFO_INPUT | SNDRV_RAWMIDI_INFO_DUPLEX;
	rmidi->private_data = chip;
	chip->rmidi = rmidi;
	return 0;
}


/*
 * gameport interface
 */

#if IS_REACHABLE(CONFIG_GAMEPORT)

static void snd_cs46xx_gameport_trigger(struct gameport *gameport)
{
	struct snd_cs46xx *chip = gameport_get_port_data(gameport);

	if (snd_BUG_ON(!chip))
		return;
	snd_cs46xx_pokeBA0(chip, BA0_JSPT, 0xFF);  //outb(gameport->io, 0xFF);
}

static unsigned char snd_cs46xx_gameport_read(struct gameport *gameport)
{
	struct snd_cs46xx *chip = gameport_get_port_data(gameport);

	if (snd_BUG_ON(!chip))
		return 0;
	return snd_cs46xx_peekBA0(chip, BA0_JSPT); //inb(gameport->io);
}

static int snd_cs46xx_gameport_cooked_read(struct gameport *gameport, int *axes, int *buttons)
{
	struct snd_cs46xx *chip = gameport_get_port_data(gameport);
	unsigned js1, js2, jst;

	if (snd_BUG_ON(!chip))
		return 0;

	js1 = snd_cs46xx_peekBA0(chip, BA0_JSC1);
	js2 = snd_cs46xx_peekBA0(chip, BA0_JSC2);
	jst = snd_cs46xx_peekBA0(chip, BA0_JSPT);
	
	*buttons = (~jst >> 4) & 0x0F; 
	
	axes[0] = ((js1 & JSC1_Y1V_MASK) >> JSC1_Y1V_SHIFT) & 0xFFFF;
	axes[1] = ((js1 & JSC1_X1V_MASK) >> JSC1_X1V_SHIFT) & 0xFFFF;
	axes[2] = ((js2 & JSC2_Y2V_MASK) >> JSC2_Y2V_SHIFT) & 0xFFFF;
	axes[3] = ((js2 & JSC2_X2V_MASK) >> JSC2_X2V_SHIFT) & 0xFFFF;

	for(jst=0;jst<4;++jst)
		if(axes[jst]==0xFFFF) axes[jst] = -1;
	return 0;
}

static int snd_cs46xx_gameport_open(struct gameport *gameport, int mode)
{
	switch (mode) {
	case GAMEPORT_MODE_COOKED:
		return 0;
	case GAMEPORT_MODE_RAW:
		return 0;
	default:
		return -1;
	}
	return 0;
}

int snd_cs46xx_gameport(struct snd_cs46xx *chip)
{
	struct gameport *gp;

	chip->gameport = gp = gameport_allocate_port();
	if (!gp) {
		dev_err(chip->card->dev,
			"cannot allocate memory for gameport\n");
		return -ENOMEM;
	}

	gameport_set_name(gp, "CS46xx Gameport");
	gameport_set_phys(gp, "pci%s/gameport0", pci_name(chip->pci));
	gameport_set_dev_parent(gp, &chip->pci->dev);
	gameport_set_port_data(gp, chip);

	gp->open = snd_cs46xx_gameport_open;
	gp->read = snd_cs46xx_gameport_read;
	gp->trigger = snd_cs46xx_gameport_trigger;
	gp->cooked_read = snd_cs46xx_gameport_cooked_read;

	snd_cs46xx_pokeBA0(chip, BA0_JSIO, 0xFF); // ?
	snd_cs46xx_pokeBA0(chip, BA0_JSCTL, JSCTL_SP_MEDIUM_SLOW);

	gameport_register_port(gp);

	return 0;
}

static inline void snd_cs46xx_remove_gameport(struct snd_cs46xx *chip)
{
	if (chip->gameport) {
		gameport_unregister_port(chip->gameport);
		chip->gameport = NULL;
	}
}
#else
int snd_cs46xx_gameport(struct snd_cs46xx *chip) { return -ENOSYS; }
static inline void snd_cs46xx_remove_gameport(struct snd_cs46xx *chip) { }
#endif /* CONFIG_GAMEPORT */

#ifdef CONFIG_SND_PROC_FS
/*
 *  proc interface
 */

static ssize_t snd_cs46xx_io_read(struct snd_info_entry *entry,
				  void *file_private_data,
				  struct file *file, char __user *buf,
				  size_t count, loff_t pos)
{
	struct snd_cs46xx_region *region = entry->private_data;
	
	if (copy_to_user_fromio(buf, region->remap_addr + pos, count))
		return -EFAULT;
	return count;
}

static struct snd_info_entry_ops snd_cs46xx_proc_io_ops = {
	.read = snd_cs46xx_io_read,
};

static int snd_cs46xx_proc_init(struct snd_card *card, struct snd_cs46xx *chip)
{
	struct snd_info_entry *entry;
	int idx;
	
	for (idx = 0; idx < 5; idx++) {
		struct snd_cs46xx_region *region = &chip->region.idx[idx];
		if (! snd_card_proc_new(card, region->name, &entry)) {
			entry->content = SNDRV_INFO_CONTENT_DATA;
			entry->private_data = chip;
			entry->c.ops = &snd_cs46xx_proc_io_ops;
			entry->size = region->size;
			entry->mode = S_IFREG | 0400;
		}
	}
#ifdef CONFIG_SND_CS46XX_NEW_DSP
	cs46xx_dsp_proc_init(card, chip);
#endif
	return 0;
}

static int snd_cs46xx_proc_done(struct snd_cs46xx *chip)
{
#ifdef CONFIG_SND_CS46XX_NEW_DSP
	cs46xx_dsp_proc_done(chip);
#endif
	return 0;
}
#else /* !CONFIG_SND_PROC_FS */
#define snd_cs46xx_proc_init(card, chip)
#define snd_cs46xx_proc_done(chip)
#endif

/*
 * stop the h/w
 */
static void snd_cs46xx_hw_stop(struct snd_cs46xx *chip)
{
	unsigned int tmp;

	tmp = snd_cs46xx_peek(chip, BA1_PFIE);
	tmp &= ~0x0000f03f;
	tmp |=  0x00000010;
	snd_cs46xx_poke(chip, BA1_PFIE, tmp);	/* playback interrupt disable */

	tmp = snd_cs46xx_peek(chip, BA1_CIE);
	tmp &= ~0x0000003f;
	tmp |=  0x00000011;
	snd_cs46xx_poke(chip, BA1_CIE, tmp);	/* capture interrupt disable */

	/*
         *  Stop playback DMA.
	 */
	tmp = snd_cs46xx_peek(chip, BA1_PCTL);
	snd_cs46xx_poke(chip, BA1_PCTL, tmp & 0x0000ffff);

	/*
         *  Stop capture DMA.
	 */
	tmp = snd_cs46xx_peek(chip, BA1_CCTL);
	snd_cs46xx_poke(chip, BA1_CCTL, tmp & 0xffff0000);

	/*
         *  Reset the processor.
         */
	snd_cs46xx_reset(chip);

	snd_cs46xx_proc_stop(chip);

	/*
	 *  Power down the PLL.
	 */
	snd_cs46xx_pokeBA0(chip, BA0_CLKCR1, 0);

	/*
	 *  Turn off the Processor by turning off the software clock enable flag in 
	 *  the clock control register.
	 */
	tmp = snd_cs46xx_peekBA0(chip, BA0_CLKCR1) & ~CLKCR1_SWCE;
	snd_cs46xx_pokeBA0(chip, BA0_CLKCR1, tmp);
}


static int snd_cs46xx_free(struct snd_cs46xx *chip)
{
	int idx;

	if (snd_BUG_ON(!chip))
		return -EINVAL;

	if (chip->active_ctrl)
		chip->active_ctrl(chip, 1);

	snd_cs46xx_remove_gameport(chip);

	if (chip->amplifier_ctrl)
		chip->amplifier_ctrl(chip, -chip->amplifier); /* force to off */
	
	snd_cs46xx_proc_done(chip);

	if (chip->region.idx[0].resource)
		snd_cs46xx_hw_stop(chip);

	if (chip->irq >= 0)
		free_irq(chip->irq, chip);

	if (chip->active_ctrl)
		chip->active_ctrl(chip, -chip->amplifier);

	for (idx = 0; idx < 5; idx++) {
		struct snd_cs46xx_region *region = &chip->region.idx[idx];

		iounmap(region->remap_addr);
		release_and_free_resource(region->resource);
	}

#ifdef CONFIG_SND_CS46XX_NEW_DSP
	if (chip->dsp_spos_instance) {
		cs46xx_dsp_spos_destroy(chip);
		chip->dsp_spos_instance = NULL;
	}
	for (idx = 0; idx < CS46XX_DSP_MODULES; idx++)
		free_module_desc(chip->modules[idx]);
#else
	vfree(chip->ba1);
#endif
	
#ifdef CONFIG_PM_SLEEP
	kfree(chip->saved_regs);
#endif

	pci_disable_device(chip->pci);
	kfree(chip);
	return 0;
}

static int snd_cs46xx_dev_free(struct snd_device *device)
{
	struct snd_cs46xx *chip = device->device_data;
	return snd_cs46xx_free(chip);
}

/*
 *  initialize chip
 */
static int snd_cs46xx_chip_init(struct snd_cs46xx *chip)
{
	int timeout;

	/* 
	 *  First, blast the clock control register to zero so that the PLL starts
         *  out in a known state, and blast the master serial port control register
         *  to zero so that the serial ports also start out in a known state.
         */
        snd_cs46xx_pokeBA0(chip, BA0_CLKCR1, 0);
        snd_cs46xx_pokeBA0(chip, BA0_SERMC1, 0);

	/*
	 *  If we are in AC97 mode, then we must set the part to a host controlled
         *  AC-link.  Otherwise, we won't be able to bring up the link.
         */        
#ifdef CONFIG_SND_CS46XX_NEW_DSP
	snd_cs46xx_pokeBA0(chip, BA0_SERACC, SERACC_HSP | SERACC_CHIP_TYPE_2_0 | 
			   SERACC_TWO_CODECS);	/* 2.00 dual codecs */
	/* snd_cs46xx_pokeBA0(chip, BA0_SERACC, SERACC_HSP | SERACC_CHIP_TYPE_2_0); */ /* 2.00 codec */
#else
	snd_cs46xx_pokeBA0(chip, BA0_SERACC, SERACC_HSP | SERACC_CHIP_TYPE_1_03); /* 1.03 codec */
#endif

        /*
         *  Drive the ARST# pin low for a minimum of 1uS (as defined in the AC97
         *  spec) and then drive it high.  This is done for non AC97 modes since
         *  there might be logic external to the CS461x that uses the ARST# line
         *  for a reset.
         */
	snd_cs46xx_pokeBA0(chip, BA0_ACCTL, 0);
#ifdef CONFIG_SND_CS46XX_NEW_DSP
	snd_cs46xx_pokeBA0(chip, BA0_ACCTL2, 0);
#endif
	udelay(50);
	snd_cs46xx_pokeBA0(chip, BA0_ACCTL, ACCTL_RSTN);
#ifdef CONFIG_SND_CS46XX_NEW_DSP
	snd_cs46xx_pokeBA0(chip, BA0_ACCTL2, ACCTL_RSTN);
#endif
    
	/*
	 *  The first thing we do here is to enable sync generation.  As soon
	 *  as we start receiving bit clock, we'll start producing the SYNC
	 *  signal.
	 */
	snd_cs46xx_pokeBA0(chip, BA0_ACCTL, ACCTL_ESYN | ACCTL_RSTN);
#ifdef CONFIG_SND_CS46XX_NEW_DSP
	snd_cs46xx_pokeBA0(chip, BA0_ACCTL2, ACCTL_ESYN | ACCTL_RSTN);
#endif

	/*
	 *  Now wait for a short while to allow the AC97 part to start
	 *  generating bit clock (so we don't try to start the PLL without an
	 *  input clock).
	 */
	mdelay(10);

	/*
	 *  Set the serial port timing configuration, so that
	 *  the clock control circuit gets its clock from the correct place.
	 */
	snd_cs46xx_pokeBA0(chip, BA0_SERMC1, SERMC1_PTC_AC97);

	/*
	 *  Write the selected clock control setup to the hardware.  Do not turn on
	 *  SWCE yet (if requested), so that the devices clocked by the output of
	 *  PLL are not clocked until the PLL is stable.
	 */
	snd_cs46xx_pokeBA0(chip, BA0_PLLCC, PLLCC_LPF_1050_2780_KHZ | PLLCC_CDR_73_104_MHZ);
	snd_cs46xx_pokeBA0(chip, BA0_PLLM, 0x3a);
	snd_cs46xx_pokeBA0(chip, BA0_CLKCR2, CLKCR2_PDIVS_8);

	/*
	 *  Power up the PLL.
	 */
	snd_cs46xx_pokeBA0(chip, BA0_CLKCR1, CLKCR1_PLLP);

	/*
         *  Wait until the PLL has stabilized.
	 */
	msleep(100);

	/*
	 *  Turn on clocking of the core so that we can setup the serial ports.
	 */
	snd_cs46xx_pokeBA0(chip, BA0_CLKCR1, CLKCR1_PLLP | CLKCR1_SWCE);

	/*
	 * Enable FIFO  Host Bypass
	 */
	snd_cs46xx_pokeBA0(chip, BA0_SERBCF, SERBCF_HBP);

	/*
	 *  Fill the serial port FIFOs with silence.
	 */
	snd_cs46xx_clear_serial_FIFOs(chip);

	/*
	 *  Set the serial port FIFO pointer to the first sample in the FIFO.
	 */
	/* snd_cs46xx_pokeBA0(chip, BA0_SERBSP, 0); */

	/*
	 *  Write the serial port configuration to the part.  The master
	 *  enable bit is not set until all other values have been written.
	 */
	snd_cs46xx_pokeBA0(chip, BA0_SERC1, SERC1_SO1F_AC97 | SERC1_SO1EN);
	snd_cs46xx_pokeBA0(chip, BA0_SERC2, SERC2_SI1F_AC97 | SERC1_SO1EN);
	snd_cs46xx_pokeBA0(chip, BA0_SERMC1, SERMC1_PTC_AC97 | SERMC1_MSPE);


#ifdef CONFIG_SND_CS46XX_NEW_DSP
	snd_cs46xx_pokeBA0(chip, BA0_SERC7, SERC7_ASDI2EN);
	snd_cs46xx_pokeBA0(chip, BA0_SERC3, 0);
	snd_cs46xx_pokeBA0(chip, BA0_SERC4, 0);
	snd_cs46xx_pokeBA0(chip, BA0_SERC5, 0);
	snd_cs46xx_pokeBA0(chip, BA0_SERC6, 1);
#endif

	mdelay(5);


	/*
	 * Wait for the codec ready signal from the AC97 codec.
	 */
	timeout = 150;
	while (timeout-- > 0) {
		/*
		 *  Read the AC97 status register to see if we've seen a CODEC READY
		 *  signal from the AC97 codec.
		 */
		if (snd_cs46xx_peekBA0(chip, BA0_ACSTS) & ACSTS_CRDY)
			goto ok1;
		msleep(10);
	}


	dev_err(chip->card->dev,
		"create - never read codec ready from AC'97\n");
	dev_err(chip->card->dev,
		"it is not probably bug, try to use CS4236 driver\n");
	return -EIO;
 ok1:
#ifdef CONFIG_SND_CS46XX_NEW_DSP
	{
		int count;
		for (count = 0; count < 150; count++) {
			/* First, we want to wait for a short time. */
			udelay(25);
        
			if (snd_cs46xx_peekBA0(chip, BA0_ACSTS2) & ACSTS_CRDY)
				break;
		}

		/*
		 *  Make sure CODEC is READY.
		 */
		if (!(snd_cs46xx_peekBA0(chip, BA0_ACSTS2) & ACSTS_CRDY))
			dev_dbg(chip->card->dev,
				"never read card ready from secondary AC'97\n");
	}
#endif

	/*
	 *  Assert the vaid frame signal so that we can start sending commands
	 *  to the AC97 codec.
	 */
	snd_cs46xx_pokeBA0(chip, BA0_ACCTL, ACCTL_VFRM | ACCTL_ESYN | ACCTL_RSTN);
#ifdef CONFIG_SND_CS46XX_NEW_DSP
	snd_cs46xx_pokeBA0(chip, BA0_ACCTL2, ACCTL_VFRM | ACCTL_ESYN | ACCTL_RSTN);
#endif


	/*
	 *  Wait until we've sampled input slots 3 and 4 as valid, meaning that
	 *  the codec is pumping ADC data across the AC-link.
	 */
	timeout = 150;
	while (timeout-- > 0) {
		/*
		 *  Read the input slot valid register and see if input slots 3 and
		 *  4 are valid yet.
		 */
		if ((snd_cs46xx_peekBA0(chip, BA0_ACISV) & (ACISV_ISV3 | ACISV_ISV4)) == (ACISV_ISV3 | ACISV_ISV4))
			goto ok2;
		msleep(10);
	}

#ifndef CONFIG_SND_CS46XX_NEW_DSP
	dev_err(chip->card->dev,
		"create - never read ISV3 & ISV4 from AC'97\n");
	return -EIO;
#else
	/* This may happen on a cold boot with a Terratec SiXPack 5.1.
	   Reloading the driver may help, if there's other soundcards 
	   with the same problem I would like to know. (Benny) */

	dev_err(chip->card->dev, "never read ISV3 & ISV4 from AC'97\n");
	dev_err(chip->card->dev,
		"Try reloading the ALSA driver, if you find something\n");
	dev_err(chip->card->dev,
		"broken or not working on your soundcard upon\n");
	dev_err(chip->card->dev,
		"this message please report to alsa-devel@alsa-project.org\n");

	return -EIO;
#endif
 ok2:

	/*
	 *  Now, assert valid frame and the slot 3 and 4 valid bits.  This will
	 *  commense the transfer of digital audio data to the AC97 codec.
	 */

	snd_cs46xx_pokeBA0(chip, BA0_ACOSV, ACOSV_SLV3 | ACOSV_SLV4);


	/*
	 *  Power down the DAC and ADC.  We will power them up (if) when we need
	 *  them.
	 */
	/* snd_cs46xx_pokeBA0(chip, BA0_AC97_POWERDOWN, 0x300); */

	/*
	 *  Turn off the Processor by turning off the software clock enable flag in 
	 *  the clock control register.
	 */
	/* tmp = snd_cs46xx_peekBA0(chip, BA0_CLKCR1) & ~CLKCR1_SWCE; */
	/* snd_cs46xx_pokeBA0(chip, BA0_CLKCR1, tmp); */

	return 0;
}

/*
 *  start and load DSP 
 */

static void cs46xx_enable_stream_irqs(struct snd_cs46xx *chip)
{
	unsigned int tmp;

	snd_cs46xx_pokeBA0(chip, BA0_HICR, HICR_IEV | HICR_CHGM);
        
	tmp = snd_cs46xx_peek(chip, BA1_PFIE);
	tmp &= ~0x0000f03f;
	snd_cs46xx_poke(chip, BA1_PFIE, tmp);	/* playback interrupt enable */

	tmp = snd_cs46xx_peek(chip, BA1_CIE);
	tmp &= ~0x0000003f;
	tmp |=  0x00000001;
	snd_cs46xx_poke(chip, BA1_CIE, tmp);	/* capture interrupt enable */
}

int snd_cs46xx_start_dsp(struct snd_cs46xx *chip)
{	
	unsigned int tmp;
#ifdef CONFIG_SND_CS46XX_NEW_DSP
	int i;
#endif
	int err;

	/*
	 *  Reset the processor.
	 */
	snd_cs46xx_reset(chip);
	/*
	 *  Download the image to the processor.
	 */
#ifdef CONFIG_SND_CS46XX_NEW_DSP
	for (i = 0; i < CS46XX_DSP_MODULES; i++) {
		err = load_firmware(chip, &chip->modules[i], module_names[i]);
		if (err < 0) {
			dev_err(chip->card->dev, "firmware load error [%s]\n",
				   module_names[i]);
			return err;
		}
		err = cs46xx_dsp_load_module(chip, chip->modules[i]);
		if (err < 0) {
			dev_err(chip->card->dev, "image download error [%s]\n",
				   module_names[i]);
			return err;
		}
	}

	if (cs46xx_dsp_scb_and_task_init(chip) < 0)
		return -EIO;
#else
	err = load_firmware(chip);
	if (err < 0)
		return err;

	/* old image */
	err = snd_cs46xx_download_image(chip);
	if (err < 0) {
		dev_err(chip->card->dev, "image download error\n");
		return err;
	}

	/*
         *  Stop playback DMA.
	 */
	tmp = snd_cs46xx_peek(chip, BA1_PCTL);
	chip->play_ctl = tmp & 0xffff0000;
	snd_cs46xx_poke(chip, BA1_PCTL, tmp & 0x0000ffff);
#endif

	/*
         *  Stop capture DMA.
	 */
	tmp = snd_cs46xx_peek(chip, BA1_CCTL);
	chip->capt.ctl = tmp & 0x0000ffff;
	snd_cs46xx_poke(chip, BA1_CCTL, tmp & 0xffff0000);

	mdelay(5);

	snd_cs46xx_set_play_sample_rate(chip, 8000);
	snd_cs46xx_set_capture_sample_rate(chip, 8000);

	snd_cs46xx_proc_start(chip);

	cs46xx_enable_stream_irqs(chip);
	
#ifndef CONFIG_SND_CS46XX_NEW_DSP
	/* set the attenuation to 0dB */ 
	snd_cs46xx_poke(chip, BA1_PVOL, 0x80008000);
	snd_cs46xx_poke(chip, BA1_CVOL, 0x80008000);
#endif

	return 0;
}


/*
 *	AMP control - null AMP
 */
 
static void amp_none(struct snd_cs46xx *chip, int change)
{	
}

#ifdef CONFIG_SND_CS46XX_NEW_DSP
static int voyetra_setup_eapd_slot(struct snd_cs46xx *chip)
{
	
	u32 idx, valid_slots,tmp,powerdown = 0;
	u16 modem_power,pin_config,logic_type;

	dev_dbg(chip->card->dev, "cs46xx_setup_eapd_slot()+\n");

	/*
	 *  See if the devices are powered down.  If so, we must power them up first
	 *  or they will not respond.
	 */
	tmp = snd_cs46xx_peekBA0(chip, BA0_CLKCR1);

	if (!(tmp & CLKCR1_SWCE)) {
		snd_cs46xx_pokeBA0(chip, BA0_CLKCR1, tmp | CLKCR1_SWCE);
		powerdown = 1;
	}

	/*
	 * Clear PRA.  The Bonzo chip will be used for GPIO not for modem
	 * stuff.
	 */
	if(chip->nr_ac97_codecs != 2) {
		dev_err(chip->card->dev,
			"cs46xx_setup_eapd_slot() - no secondary codec configured\n");
		return -EINVAL;
	}

	modem_power = snd_cs46xx_codec_read (chip, 
					     AC97_EXTENDED_MSTATUS,
					     CS46XX_SECONDARY_CODEC_INDEX);
	modem_power &=0xFEFF;

	snd_cs46xx_codec_write(chip, 
			       AC97_EXTENDED_MSTATUS, modem_power,
			       CS46XX_SECONDARY_CODEC_INDEX);

	/*
	 * Set GPIO pin's 7 and 8 so that they are configured for output.
	 */
	pin_config = snd_cs46xx_codec_read (chip, 
					    AC97_GPIO_CFG,
					    CS46XX_SECONDARY_CODEC_INDEX);
	pin_config &=0x27F;

	snd_cs46xx_codec_write(chip, 
			       AC97_GPIO_CFG, pin_config,
			       CS46XX_SECONDARY_CODEC_INDEX);
    
	/*
	 * Set GPIO pin's 7 and 8 so that they are compatible with CMOS logic.
	 */

	logic_type = snd_cs46xx_codec_read(chip, AC97_GPIO_POLARITY,
					   CS46XX_SECONDARY_CODEC_INDEX);
	logic_type &=0x27F; 

	snd_cs46xx_codec_write (chip, AC97_GPIO_POLARITY, logic_type,
				CS46XX_SECONDARY_CODEC_INDEX);

	valid_slots = snd_cs46xx_peekBA0(chip, BA0_ACOSV);
	valid_slots |= 0x200;
	snd_cs46xx_pokeBA0(chip, BA0_ACOSV, valid_slots);

	if ( cs46xx_wait_for_fifo(chip,1) ) {
		dev_dbg(chip->card->dev, "FIFO is busy\n");
	  
	  return -EINVAL;
	}

	/*
	 * Fill slots 12 with the correct value for the GPIO pins. 
	 */
	for(idx = 0x90; idx <= 0x9F; idx++) {
		/*
		 * Initialize the fifo so that bits 7 and 8 are on.
		 *
		 * Remember that the GPIO pins in bonzo are shifted by 4 bits to
		 * the left.  0x1800 corresponds to bits 7 and 8.
		 */
		snd_cs46xx_pokeBA0(chip, BA0_SERBWP, 0x1800);

		/*
		 * Wait for command to complete
		 */
		if ( cs46xx_wait_for_fifo(chip,200) ) {
			dev_dbg(chip->card->dev,
				"failed waiting for FIFO at addr (%02X)\n",
				idx);

			return -EINVAL;
		}
            
		/*
		 * Write the serial port FIFO index.
		 */
		snd_cs46xx_pokeBA0(chip, BA0_SERBAD, idx);
      
		/*
		 * Tell the serial port to load the new value into the FIFO location.
		 */
		snd_cs46xx_pokeBA0(chip, BA0_SERBCM, SERBCM_WRC);
	}

	/* wait for last command to complete */
	cs46xx_wait_for_fifo(chip,200);

	/*
	 *  Now, if we powered up the devices, then power them back down again.
	 *  This is kinda ugly, but should never happen.
	 */
	if (powerdown)
		snd_cs46xx_pokeBA0(chip, BA0_CLKCR1, tmp);

	return 0;
}
#endif

/*
 *	Crystal EAPD mode
 */
 
static void amp_voyetra(struct snd_cs46xx *chip, int change)
{
	/* Manage the EAPD bit on the Crystal 4297 
	   and the Analog AD1885 */
	   
#ifdef CONFIG_SND_CS46XX_NEW_DSP
	int old = chip->amplifier;
#endif
	int oval, val;
	
	chip->amplifier += change;
	oval = snd_cs46xx_codec_read(chip, AC97_POWERDOWN,
				     CS46XX_PRIMARY_CODEC_INDEX);
	val = oval;
	if (chip->amplifier) {
		/* Turn the EAPD amp on */
		val |= 0x8000;
	} else {
		/* Turn the EAPD amp off */
		val &= ~0x8000;
	}
	if (val != oval) {
		snd_cs46xx_codec_write(chip, AC97_POWERDOWN, val,
				       CS46XX_PRIMARY_CODEC_INDEX);
		if (chip->eapd_switch)
			snd_ctl_notify(chip->card, SNDRV_CTL_EVENT_MASK_VALUE,
				       &chip->eapd_switch->id);
	}

#ifdef CONFIG_SND_CS46XX_NEW_DSP
	if (chip->amplifier && !old) {
		voyetra_setup_eapd_slot(chip);
	}
#endif
}

static void hercules_init(struct snd_cs46xx *chip) 
{
	/* default: AMP off, and SPDIF input optical */
	snd_cs46xx_pokeBA0(chip, BA0_EGPIODR, EGPIODR_GPOE0);
	snd_cs46xx_pokeBA0(chip, BA0_EGPIOPTR, EGPIODR_GPOE0);
}


/*
 *	Game Theatre XP card - EGPIO[2] is used to enable the external amp.
 */ 
static void amp_hercules(struct snd_cs46xx *chip, int change)
{
	int old = chip->amplifier;
	int val1 = snd_cs46xx_peekBA0(chip, BA0_EGPIODR);
	int val2 = snd_cs46xx_peekBA0(chip, BA0_EGPIOPTR);

	chip->amplifier += change;
	if (chip->amplifier && !old) {
		dev_dbg(chip->card->dev, "Hercules amplifier ON\n");

		snd_cs46xx_pokeBA0(chip, BA0_EGPIODR, 
				   EGPIODR_GPOE2 | val1);     /* enable EGPIO2 output */
		snd_cs46xx_pokeBA0(chip, BA0_EGPIOPTR, 
				   EGPIOPTR_GPPT2 | val2);   /* open-drain on output */
	} else if (old && !chip->amplifier) {
		dev_dbg(chip->card->dev, "Hercules amplifier OFF\n");
		snd_cs46xx_pokeBA0(chip, BA0_EGPIODR,  val1 & ~EGPIODR_GPOE2); /* disable */
		snd_cs46xx_pokeBA0(chip, BA0_EGPIOPTR, val2 & ~EGPIOPTR_GPPT2); /* disable */
	}
}

static void voyetra_mixer_init (struct snd_cs46xx *chip)
{
	dev_dbg(chip->card->dev, "initializing Voyetra mixer\n");

	/* Enable SPDIF out */
	snd_cs46xx_pokeBA0(chip, BA0_EGPIODR, EGPIODR_GPOE0);
	snd_cs46xx_pokeBA0(chip, BA0_EGPIOPTR, EGPIODR_GPOE0);
}

static void hercules_mixer_init (struct snd_cs46xx *chip)
{
#ifdef CONFIG_SND_CS46XX_NEW_DSP
	unsigned int idx;
	int err;
	struct snd_card *card = chip->card;
#endif

	/* set EGPIO to default */
	hercules_init(chip);

	dev_dbg(chip->card->dev, "initializing Hercules mixer\n");

#ifdef CONFIG_SND_CS46XX_NEW_DSP
	if (chip->in_suspend)
		return;

	for (idx = 0 ; idx < ARRAY_SIZE(snd_hercules_controls); idx++) {
		struct snd_kcontrol *kctl;

		kctl = snd_ctl_new1(&snd_hercules_controls[idx], chip);
		if ((err = snd_ctl_add(card, kctl)) < 0) {
			dev_err(card->dev,
				"failed to initialize Hercules mixer (%d)\n",
				err);
			break;
		}
	}
#endif
}


#if 0
/*
 *	Untested
 */
 
static void amp_voyetra_4294(struct snd_cs46xx *chip, int change)
{
	chip->amplifier += change;

	if (chip->amplifier) {
		/* Switch the GPIO pins 7 and 8 to open drain */
		snd_cs46xx_codec_write(chip, 0x4C,
				       snd_cs46xx_codec_read(chip, 0x4C) & 0xFE7F);
		snd_cs46xx_codec_write(chip, 0x4E,
				       snd_cs46xx_codec_read(chip, 0x4E) | 0x0180);
		/* Now wake the AMP (this might be backwards) */
		snd_cs46xx_codec_write(chip, 0x54,
				       snd_cs46xx_codec_read(chip, 0x54) & ~0x0180);
	} else {
		snd_cs46xx_codec_write(chip, 0x54,
				       snd_cs46xx_codec_read(chip, 0x54) | 0x0180);
	}
}
#endif


/*
 *	Handle the CLKRUN on a thinkpad. We must disable CLKRUN support
 *	whenever we need to beat on the chip.
 *
 *	The original idea and code for this hack comes from David Kaiser at
 *	Linuxcare. Perhaps one day Crystal will document their chips well
 *	enough to make them useful.
 */
 
static void clkrun_hack(struct snd_cs46xx *chip, int change)
{
	u16 control, nval;
	
	if (!chip->acpi_port)
		return;

	chip->amplifier += change;
	
	/* Read ACPI port */	
	nval = control = inw(chip->acpi_port + 0x10);

	/* Flip CLKRUN off while running */
	if (! chip->amplifier)
		nval |= 0x2000;
	else
		nval &= ~0x2000;
	if (nval != control)
		outw(nval, chip->acpi_port + 0x10);
}

	
/*
 * detect intel piix4
 */
static void clkrun_init(struct snd_cs46xx *chip)
{
	struct pci_dev *pdev;
	u8 pp;

	chip->acpi_port = 0;
	
	pdev = pci_get_device(PCI_VENDOR_ID_INTEL,
		PCI_DEVICE_ID_INTEL_82371AB_3, NULL);
	if (pdev == NULL)
		return;		/* Not a thinkpad thats for sure */

	/* Find the control port */		
	pci_read_config_byte(pdev, 0x41, &pp);
	chip->acpi_port = pp << 8;
	pci_dev_put(pdev);
}


/*
 * Card subid table
 */
 
struct cs_card_type
{
	u16 vendor;
	u16 id;
	char *name;
	void (*init)(struct snd_cs46xx *);
	void (*amp)(struct snd_cs46xx *, int);
	void (*active)(struct snd_cs46xx *, int);
	void (*mixer_init)(struct snd_cs46xx *);
};

static struct cs_card_type cards[] = {
	{
		.vendor = 0x1489,
		.id = 0x7001,
		.name = "Genius Soundmaker 128 value",
		/* nothing special */
	},
	{
		.vendor = 0x5053,
		.id = 0x3357,
		.name = "Voyetra",
		.amp = amp_voyetra,
		.mixer_init = voyetra_mixer_init,
	},
	{
		.vendor = 0x1071,
		.id = 0x6003,
		.name = "Mitac MI6020/21",
		.amp = amp_voyetra,
	},
	/* Hercules Game Theatre XP */
	{
		.vendor = 0x14af, /* Guillemot Corporation */
		.id = 0x0050,
		.name = "Hercules Game Theatre XP",
		.amp = amp_hercules,
		.mixer_init = hercules_mixer_init,
	},
	{
		.vendor = 0x1681,
		.id = 0x0050,
		.name = "Hercules Game Theatre XP",
		.amp = amp_hercules,
		.mixer_init = hercules_mixer_init,
	},
	{
		.vendor = 0x1681,
		.id = 0x0051,
		.name = "Hercules Game Theatre XP",
		.amp = amp_hercules,
		.mixer_init = hercules_mixer_init,

	},
	{
		.vendor = 0x1681,
		.id = 0x0052,
		.name = "Hercules Game Theatre XP",
		.amp = amp_hercules,
		.mixer_init = hercules_mixer_init,
	},
	{
		.vendor = 0x1681,
		.id = 0x0053,
		.name = "Hercules Game Theatre XP",
		.amp = amp_hercules,
		.mixer_init = hercules_mixer_init,
	},
	{
		.vendor = 0x1681,
		.id = 0x0054,
		.name = "Hercules Game Theatre XP",
		.amp = amp_hercules,
		.mixer_init = hercules_mixer_init,
	},
	/* Herculess Fortissimo */
	{
		.vendor = 0x1681,
		.id = 0xa010,
		.name = "Hercules Gamesurround Fortissimo II",
	},
	{
		.vendor = 0x1681,
		.id = 0xa011,
		.name = "Hercules Gamesurround Fortissimo III 7.1",
	},
	/* Teratec */
	{
		.vendor = 0x153b,
		.id = 0x112e,
		.name = "Terratec DMX XFire 1024",
	},
	{
		.vendor = 0x153b,
		.id = 0x1136,
		.name = "Terratec SiXPack 5.1",
	},
	/* Not sure if the 570 needs the clkrun hack */
	{
		.vendor = PCI_VENDOR_ID_IBM,
		.id = 0x0132,
		.name = "Thinkpad 570",
		.init = clkrun_init,
		.active = clkrun_hack,
	},
	{
		.vendor = PCI_VENDOR_ID_IBM,
		.id = 0x0153,
		.name = "Thinkpad 600X/A20/T20",
		.init = clkrun_init,
		.active = clkrun_hack,
	},
	{
		.vendor = PCI_VENDOR_ID_IBM,
		.id = 0x1010,
		.name = "Thinkpad 600E (unsupported)",
	},
	{} /* terminator */
};


/*
 * APM support
 */
#ifdef CONFIG_PM_SLEEP
static unsigned int saved_regs[] = {
	BA0_ACOSV,
	/*BA0_ASER_FADDR,*/
	BA0_ASER_MASTER,
	BA1_PVOL,
	BA1_CVOL,
};

static int snd_cs46xx_suspend(struct device *dev)
{
	struct snd_card *card = dev_get_drvdata(dev);
	struct snd_cs46xx *chip = card->private_data;
	int i, amp_saved;

	snd_power_change_state(card, SNDRV_CTL_POWER_D3hot);
	chip->in_suspend = 1;
	// chip->ac97_powerdown = snd_cs46xx_codec_read(chip, AC97_POWER_CONTROL);
	// chip->ac97_general_purpose = snd_cs46xx_codec_read(chip, BA0_AC97_GENERAL_PURPOSE);

	snd_ac97_suspend(chip->ac97[CS46XX_PRIMARY_CODEC_INDEX]);
	snd_ac97_suspend(chip->ac97[CS46XX_SECONDARY_CODEC_INDEX]);

	/* save some registers */
	for (i = 0; i < ARRAY_SIZE(saved_regs); i++)
		chip->saved_regs[i] = snd_cs46xx_peekBA0(chip, saved_regs[i]);

	amp_saved = chip->amplifier;
	/* turn off amp */
	chip->amplifier_ctrl(chip, -chip->amplifier);
	snd_cs46xx_hw_stop(chip);
	/* disable CLKRUN */
	chip->active_ctrl(chip, -chip->amplifier);
	chip->amplifier = amp_saved; /* restore the status */
	return 0;
}

static int snd_cs46xx_resume(struct device *dev)
{
	struct snd_card *card = dev_get_drvdata(dev);
	struct snd_cs46xx *chip = card->private_data;
	int amp_saved;
#ifdef CONFIG_SND_CS46XX_NEW_DSP
	int i;
#endif
	unsigned int tmp;

	amp_saved = chip->amplifier;
	chip->amplifier = 0;
	chip->active_ctrl(chip, 1); /* force to on */

	snd_cs46xx_chip_init(chip);

	snd_cs46xx_reset(chip);
#ifdef CONFIG_SND_CS46XX_NEW_DSP
	cs46xx_dsp_resume(chip);
	/* restore some registers */
	for (i = 0; i < ARRAY_SIZE(saved_regs); i++)
		snd_cs46xx_pokeBA0(chip, saved_regs[i], chip->saved_regs[i]);
#else
	snd_cs46xx_download_image(chip);
#endif

#if 0
	snd_cs46xx_codec_write(chip, BA0_AC97_GENERAL_PURPOSE, 
			       chip->ac97_general_purpose);
	snd_cs46xx_codec_write(chip, AC97_POWER_CONTROL, 
			       chip->ac97_powerdown);
	mdelay(10);
	snd_cs46xx_codec_write(chip, BA0_AC97_POWERDOWN,
			       chip->ac97_powerdown);
	mdelay(5);
#endif

	snd_ac97_resume(chip->ac97[CS46XX_PRIMARY_CODEC_INDEX]);
	snd_ac97_resume(chip->ac97[CS46XX_SECONDARY_CODEC_INDEX]);

	/*
         *  Stop capture DMA.
	 */
	tmp = snd_cs46xx_peek(chip, BA1_CCTL);
	chip->capt.ctl = tmp & 0x0000ffff;
	snd_cs46xx_poke(chip, BA1_CCTL, tmp & 0xffff0000);

	mdelay(5);

	/* reset playback/capture */
	snd_cs46xx_set_play_sample_rate(chip, 8000);
	snd_cs46xx_set_capture_sample_rate(chip, 8000);
	snd_cs46xx_proc_start(chip);

	cs46xx_enable_stream_irqs(chip);

	if (amp_saved)
		chip->amplifier_ctrl(chip, 1); /* turn amp on */
	else
		chip->active_ctrl(chip, -1); /* disable CLKRUN */
	chip->amplifier = amp_saved;
	chip->in_suspend = 0;
	snd_power_change_state(card, SNDRV_CTL_POWER_D0);
	return 0;
}

SIMPLE_DEV_PM_OPS(snd_cs46xx_pm, snd_cs46xx_suspend, snd_cs46xx_resume);
#endif /* CONFIG_PM_SLEEP */


/*
 */

int snd_cs46xx_create(struct snd_card *card,
		      struct pci_dev *pci,
		      int external_amp, int thinkpad,
		      struct snd_cs46xx **rchip)
{
	struct snd_cs46xx *chip;
	int err, idx;
	struct snd_cs46xx_region *region;
	struct cs_card_type *cp;
	u16 ss_card, ss_vendor;
	static struct snd_device_ops ops = {
		.dev_free =	snd_cs46xx_dev_free,
	};
	
	*rchip = NULL;

	/* enable PCI device */
	if ((err = pci_enable_device(pci)) < 0)
		return err;

	chip = kzalloc(sizeof(*chip), GFP_KERNEL);
	if (chip == NULL) {
		pci_disable_device(pci);
		return -ENOMEM;
	}
	spin_lock_init(&chip->reg_lock);
#ifdef CONFIG_SND_CS46XX_NEW_DSP
	mutex_init(&chip->spos_mutex);
#endif
	chip->card = card;
	chip->pci = pci;
	chip->irq = -1;
	chip->ba0_addr = pci_resource_start(pci, 0);
	chip->ba1_addr = pci_resource_start(pci, 1);
	if (chip->ba0_addr == 0 || chip->ba0_addr == (unsigned long)~0 ||
	    chip->ba1_addr == 0 || chip->ba1_addr == (unsigned long)~0) {
		dev_err(chip->card->dev,
			"wrong address(es) - ba0 = 0x%lx, ba1 = 0x%lx\n",
			   chip->ba0_addr, chip->ba1_addr);
	    	snd_cs46xx_free(chip);
	    	return -ENOMEM;
	}

	region = &chip->region.name.ba0;
	strcpy(region->name, "CS46xx_BA0");
	region->base = chip->ba0_addr;
	region->size = CS46XX_BA0_SIZE;

	region = &chip->region.name.data0;
	strcpy(region->name, "CS46xx_BA1_data0");
	region->base = chip->ba1_addr + BA1_SP_DMEM0;
	region->size = CS46XX_BA1_DATA0_SIZE;

	region = &chip->region.name.data1;
	strcpy(region->name, "CS46xx_BA1_data1");
	region->base = chip->ba1_addr + BA1_SP_DMEM1;
	region->size = CS46XX_BA1_DATA1_SIZE;

	region = &chip->region.name.pmem;
	strcpy(region->name, "CS46xx_BA1_pmem");
	region->base = chip->ba1_addr + BA1_SP_PMEM;
	region->size = CS46XX_BA1_PRG_SIZE;

	region = &chip->region.name.reg;
	strcpy(region->name, "CS46xx_BA1_reg");
	region->base = chip->ba1_addr + BA1_SP_REG;
	region->size = CS46XX_BA1_REG_SIZE;

	/* set up amp and clkrun hack */
	pci_read_config_word(pci, PCI_SUBSYSTEM_VENDOR_ID, &ss_vendor);
	pci_read_config_word(pci, PCI_SUBSYSTEM_ID, &ss_card);

	for (cp = &cards[0]; cp->name; cp++) {
		if (cp->vendor == ss_vendor && cp->id == ss_card) {
			dev_dbg(chip->card->dev, "hack for %s enabled\n",
				cp->name);

			chip->amplifier_ctrl = cp->amp;
			chip->active_ctrl = cp->active;
			chip->mixer_init = cp->mixer_init;

			if (cp->init)
				cp->init(chip);
			break;
		}
	}

	if (external_amp) {
		dev_info(chip->card->dev,
			 "Crystal EAPD support forced on.\n");
		chip->amplifier_ctrl = amp_voyetra;
	}

	if (thinkpad) {
		dev_info(chip->card->dev,
			 "Activating CLKRUN hack for Thinkpad.\n");
		chip->active_ctrl = clkrun_hack;
		clkrun_init(chip);
	}
	
	if (chip->amplifier_ctrl == NULL)
		chip->amplifier_ctrl = amp_none;
	if (chip->active_ctrl == NULL)
		chip->active_ctrl = amp_none;

	chip->active_ctrl(chip, 1); /* enable CLKRUN */

	pci_set_master(pci);

	for (idx = 0; idx < 5; idx++) {
		region = &chip->region.idx[idx];
		if ((region->resource = request_mem_region(region->base, region->size,
							   region->name)) == NULL) {
			dev_err(chip->card->dev,
				"unable to request memory region 0x%lx-0x%lx\n",
				   region->base, region->base + region->size - 1);
			snd_cs46xx_free(chip);
			return -EBUSY;
		}
		region->remap_addr = ioremap_nocache(region->base, region->size);
		if (region->remap_addr == NULL) {
			dev_err(chip->card->dev,
				"%s ioremap problem\n", region->name);
			snd_cs46xx_free(chip);
			return -ENOMEM;
		}
	}

	if (request_irq(pci->irq, snd_cs46xx_interrupt, IRQF_SHARED,
			KBUILD_MODNAME, chip)) {
		dev_err(chip->card->dev, "unable to grab IRQ %d\n", pci->irq);
		snd_cs46xx_free(chip);
		return -EBUSY;
	}
	chip->irq = pci->irq;

#ifdef CONFIG_SND_CS46XX_NEW_DSP
	chip->dsp_spos_instance = cs46xx_dsp_spos_create(chip);
	if (chip->dsp_spos_instance == NULL) {
		snd_cs46xx_free(chip);
		return -ENOMEM;
	}
#endif

	err = snd_cs46xx_chip_init(chip);
	if (err < 0) {
		snd_cs46xx_free(chip);
		return err;
	}

	if ((err = snd_device_new(card, SNDRV_DEV_LOWLEVEL, chip, &ops)) < 0) {
		snd_cs46xx_free(chip);
		return err;
	}
	
	snd_cs46xx_proc_init(card, chip);

#ifdef CONFIG_PM_SLEEP
	chip->saved_regs = kmalloc_array(ARRAY_SIZE(saved_regs),
					 sizeof(*chip->saved_regs),
					 GFP_KERNEL);
	if (!chip->saved_regs) {
		snd_cs46xx_free(chip);
		return -ENOMEM;
	}
#endif

	chip->active_ctrl(chip, -1); /* disable CLKRUN */

	*rchip = chip;
	return 0;
}
