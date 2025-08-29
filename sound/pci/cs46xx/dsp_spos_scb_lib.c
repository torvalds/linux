// SPDX-License-Identifier: GPL-2.0-or-later
/*
 */

/*
 * 2002-07 Benny Sjostrand benny@hostmobility.com
 */


#include <linux/io.h>
#include <linux/delay.h>
#include <linux/pm.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/mutex.h>

#include <sound/core.h>
#include <sound/control.h>
#include <sound/info.h>
#include "cs46xx.h"

#include "cs46xx_lib.h"
#include "dsp_spos.h"

struct proc_scb_info {
	struct dsp_scb_descriptor * scb_desc;
	struct snd_cs46xx *chip;
};

static void remove_symbol (struct snd_cs46xx * chip, struct dsp_symbol_entry * symbol)
{
	struct dsp_spos_instance * ins = chip->dsp_spos_instance;
	int symbol_index = (int)(symbol - ins->symbol_table.symbols);

	if (snd_BUG_ON(ins->symbol_table.nsymbols <= 0))
		return;
	if (snd_BUG_ON(symbol_index < 0 ||
		       symbol_index >= ins->symbol_table.nsymbols))
		return;

	ins->symbol_table.symbols[symbol_index].deleted = 1;

	if (symbol_index < ins->symbol_table.highest_frag_index) {
		ins->symbol_table.highest_frag_index = symbol_index;
	}
  
	if (symbol_index == ins->symbol_table.nsymbols - 1)
		ins->symbol_table.nsymbols --;

	if (ins->symbol_table.highest_frag_index > ins->symbol_table.nsymbols) {
		ins->symbol_table.highest_frag_index = ins->symbol_table.nsymbols;
	}

}

#ifdef CONFIG_SND_PROC_FS
static void cs46xx_dsp_proc_scb_info_read (struct snd_info_entry *entry,
					   struct snd_info_buffer *buffer)
{
	struct proc_scb_info * scb_info  = entry->private_data;
	struct dsp_scb_descriptor * scb = scb_info->scb_desc;
	struct snd_cs46xx *chip = scb_info->chip;
	int j,col;
	void __iomem *dst = chip->region.idx[1].remap_addr + DSP_PARAMETER_BYTE_OFFSET;

	guard(mutex)(&chip->spos_mutex);
	snd_iprintf(buffer,"%04x %s:\n",scb->address,scb->scb_name);

	for (col = 0,j = 0;j < 0x10; j++,col++) {
		if (col == 4) {
			snd_iprintf(buffer,"\n");
			col = 0;
		}
		snd_iprintf(buffer,"%08x ",readl(dst + (scb->address + j) * sizeof(u32)));
	}
  
	snd_iprintf(buffer,"\n");

	if (scb->parent_scb_ptr != NULL) {
		snd_iprintf(buffer,"parent [%s:%04x] ", 
			    scb->parent_scb_ptr->scb_name,
			    scb->parent_scb_ptr->address);
	} else snd_iprintf(buffer,"parent [none] ");
  
	snd_iprintf(buffer,"sub_list_ptr [%s:%04x]\nnext_scb_ptr [%s:%04x]  task_entry [%s:%04x]\n",
		    scb->sub_list_ptr->scb_name,
		    scb->sub_list_ptr->address,
		    scb->next_scb_ptr->scb_name,
		    scb->next_scb_ptr->address,
		    scb->task_entry->symbol_name,
		    scb->task_entry->address);

	snd_iprintf(buffer,"index [%d] ref_count [%d]\n",scb->index,scb->ref_count);  
}
#endif

static void _dsp_unlink_scb (struct snd_cs46xx *chip, struct dsp_scb_descriptor * scb)
{
	struct dsp_spos_instance * ins = chip->dsp_spos_instance;

	if ( scb->parent_scb_ptr ) {
		/* unlink parent SCB */
		if (snd_BUG_ON(scb->parent_scb_ptr->sub_list_ptr != scb &&
			       scb->parent_scb_ptr->next_scb_ptr != scb))
			return;
  
		if (scb->parent_scb_ptr->sub_list_ptr == scb) {

			if (scb->next_scb_ptr == ins->the_null_scb) {
				/* last and only node in parent sublist */
				scb->parent_scb_ptr->sub_list_ptr = scb->sub_list_ptr;

				if (scb->sub_list_ptr != ins->the_null_scb) {
					scb->sub_list_ptr->parent_scb_ptr = scb->parent_scb_ptr;
				}
				scb->sub_list_ptr = ins->the_null_scb;
			} else {
				/* first node in parent sublist */
				scb->parent_scb_ptr->sub_list_ptr = scb->next_scb_ptr;

				if (scb->next_scb_ptr != ins->the_null_scb) {
					/* update next node parent ptr. */
					scb->next_scb_ptr->parent_scb_ptr = scb->parent_scb_ptr;
				}
				scb->next_scb_ptr = ins->the_null_scb;
			}
		} else {
			scb->parent_scb_ptr->next_scb_ptr = scb->next_scb_ptr;

			if (scb->next_scb_ptr != ins->the_null_scb) {
				/* update next node parent ptr. */
				scb->next_scb_ptr->parent_scb_ptr = scb->parent_scb_ptr;
			}
			scb->next_scb_ptr = ins->the_null_scb;
		}

		/* update parent first entry in DSP RAM */
		cs46xx_dsp_spos_update_scb(chip,scb->parent_scb_ptr);

		/* then update entry in DSP RAM */
		cs46xx_dsp_spos_update_scb(chip,scb);

		scb->parent_scb_ptr = NULL;
	}
}

static void _dsp_clear_sample_buffer (struct snd_cs46xx *chip, u32 sample_buffer_addr,
				      int dword_count) 
{
	void __iomem *dst = chip->region.idx[2].remap_addr + sample_buffer_addr;
	int i;
  
	for (i = 0; i < dword_count ; ++i ) {
		writel(0, dst);
		dst += 4;
	}  
}

void cs46xx_dsp_remove_scb (struct snd_cs46xx *chip, struct dsp_scb_descriptor * scb)
{
	struct dsp_spos_instance * ins = chip->dsp_spos_instance;

	/* check integrety */
	if (snd_BUG_ON(scb->index < 0 ||
		       scb->index >= ins->nscb ||
		       (ins->scbs + scb->index) != scb))
		return;

#if 0
	/* can't remove a SCB with childs before 
	   removing childs first  */
	if (snd_BUG_ON(scb->sub_list_ptr != ins->the_null_scb ||
		       scb->next_scb_ptr != ins->the_null_scb))
		goto _end;
#endif

	scoped_guard(spinlock_irqsave, &chip->reg_lock) {
		_dsp_unlink_scb(chip, scb);
	}

	cs46xx_dsp_proc_free_scb_desc(scb);
	if (snd_BUG_ON(!scb->scb_symbol))
		return;
	remove_symbol (chip,scb->scb_symbol);

	ins->scbs[scb->index].deleted = 1;
#ifdef CONFIG_PM_SLEEP
	kfree(ins->scbs[scb->index].data);
	ins->scbs[scb->index].data = NULL;
#endif

	if (scb->index < ins->scb_highest_frag_index)
		ins->scb_highest_frag_index = scb->index;

	if (scb->index == ins->nscb - 1) {
		ins->nscb --;
	}

	if (ins->scb_highest_frag_index > ins->nscb) {
		ins->scb_highest_frag_index = ins->nscb;
	}
}


#ifdef CONFIG_SND_PROC_FS
void cs46xx_dsp_proc_free_scb_desc (struct dsp_scb_descriptor * scb)
{
	if (scb->proc_info) {
		struct proc_scb_info * scb_info = scb->proc_info->private_data;
		struct snd_cs46xx *chip = scb_info->chip;

		dev_dbg(chip->card->dev,
			"cs46xx_dsp_proc_free_scb_desc: freeing %s\n",
			scb->scb_name);

		snd_info_free_entry(scb->proc_info);
		scb->proc_info = NULL;

		kfree (scb_info);
	}
}

void cs46xx_dsp_proc_register_scb_desc (struct snd_cs46xx *chip,
					struct dsp_scb_descriptor * scb)
{
	struct dsp_spos_instance * ins = chip->dsp_spos_instance;
	struct snd_info_entry * entry;
	struct proc_scb_info * scb_info;

	/* register to proc */
	if (ins->snd_card != NULL && ins->proc_dsp_dir != NULL &&
	    scb->proc_info == NULL) {
  
		entry = snd_info_create_card_entry(ins->snd_card, scb->scb_name,
						   ins->proc_dsp_dir);
		if (entry) {
			scb_info = kmalloc(sizeof(struct proc_scb_info), GFP_KERNEL);
			if (!scb_info) {
				snd_info_free_entry(entry);
				entry = NULL;
				goto out;
			}

			scb_info->chip = chip;
			scb_info->scb_desc = scb;
			snd_info_set_text_ops(entry, scb_info,
					      cs46xx_dsp_proc_scb_info_read);
		}
out:
		scb->proc_info = entry;
	}
}
#endif /* CONFIG_SND_PROC_FS */

static struct dsp_scb_descriptor * 
_dsp_create_generic_scb (struct snd_cs46xx *chip, char * name, u32 * scb_data, u32 dest,
                         struct dsp_symbol_entry * task_entry,
                         struct dsp_scb_descriptor * parent_scb,
                         int scb_child_type)
{
	struct dsp_spos_instance * ins = chip->dsp_spos_instance;
	struct dsp_scb_descriptor * scb;
  
	if (snd_BUG_ON(!ins->the_null_scb))
		return NULL;

	/* fill the data that will be wroten to DSP */
	scb_data[SCBsubListPtr] = 
		(ins->the_null_scb->address << 0x10) | ins->the_null_scb->address;

	scb_data[SCBfuncEntryPtr] &= 0xFFFF0000;
	scb_data[SCBfuncEntryPtr] |= task_entry->address;

	dev_dbg(chip->card->dev, "dsp_spos: creating SCB <%s>\n", name);

	scb = cs46xx_dsp_create_scb(chip,name,scb_data,dest);


	scb->sub_list_ptr = ins->the_null_scb;
	scb->next_scb_ptr = ins->the_null_scb;

	scb->parent_scb_ptr = parent_scb;
	scb->task_entry = task_entry;

  
	/* update parent SCB */
	if (scb->parent_scb_ptr) {
#if 0
		dev_dbg(chip->card->dev,
			"scb->parent_scb_ptr = %s\n",
			scb->parent_scb_ptr->scb_name);
		dev_dbg(chip->card->dev,
			"scb->parent_scb_ptr->next_scb_ptr = %s\n",
			scb->parent_scb_ptr->next_scb_ptr->scb_name);
		dev_dbg(chip->card->dev,
			"scb->parent_scb_ptr->sub_list_ptr = %s\n",
			scb->parent_scb_ptr->sub_list_ptr->scb_name);
#endif
		/* link to  parent SCB */
		if (scb_child_type == SCB_ON_PARENT_NEXT_SCB) {
			if (snd_BUG_ON(scb->parent_scb_ptr->next_scb_ptr !=
				       ins->the_null_scb))
				return NULL;

			scb->parent_scb_ptr->next_scb_ptr = scb;

		} else if (scb_child_type == SCB_ON_PARENT_SUBLIST_SCB) {
			if (snd_BUG_ON(scb->parent_scb_ptr->sub_list_ptr !=
				       ins->the_null_scb))
				return NULL;

			scb->parent_scb_ptr->sub_list_ptr = scb;
		} else {
			snd_BUG();
		}

		scoped_guard(spinlock_irqsave, &chip->reg_lock) {
			/* update entry in DSP RAM */
			cs46xx_dsp_spos_update_scb(chip, scb->parent_scb_ptr);
		}
	}


	cs46xx_dsp_proc_register_scb_desc (chip,scb);

	return scb;
}

static struct dsp_scb_descriptor * 
cs46xx_dsp_create_generic_scb (struct snd_cs46xx *chip, char * name, u32 * scb_data,
			       u32 dest, char * task_entry_name,
                               struct dsp_scb_descriptor * parent_scb,
                               int scb_child_type)
{
	struct dsp_symbol_entry * task_entry;

	task_entry = cs46xx_dsp_lookup_symbol (chip,task_entry_name,
					       SYMBOL_CODE);
  
	if (task_entry == NULL) {
		dev_err(chip->card->dev,
			"dsp_spos: symbol %s not found\n", task_entry_name);
		return NULL;
	}
  
	return _dsp_create_generic_scb (chip,name,scb_data,dest,task_entry,
					parent_scb,scb_child_type);
}

struct dsp_scb_descriptor * 
cs46xx_dsp_create_timing_master_scb (struct snd_cs46xx *chip)
{
	struct dsp_scb_descriptor * scb;
  
	struct dsp_timing_master_scb timing_master_scb = {
		{ 0,
		  0,
		  0,
		  0
		},
		{ 0,
		  0,
		  0,
		  0,
		  0
		},
		0,0,
		0,NULL_SCB_ADDR,
		0,0,             /* extraSampleAccum:TMreserved */
		0,0,             /* codecFIFOptr:codecFIFOsyncd */
		0x0001,0x8000,   /* fracSampAccumQm1:TMfrmsLeftInGroup */
		0x0001,0x0000,   /* fracSampCorrectionQm1:TMfrmGroupLength */
		0x00060000       /* nSampPerFrmQ15 */
	};    
  
	scb = cs46xx_dsp_create_generic_scb(chip,"TimingMasterSCBInst",(u32 *)&timing_master_scb,
					    TIMINGMASTER_SCB_ADDR,
					    "TIMINGMASTER",NULL,SCB_NO_PARENT);

	return scb;
}


struct dsp_scb_descriptor * 
cs46xx_dsp_create_codec_out_scb(struct snd_cs46xx * chip, char * codec_name,
                                u16 channel_disp, u16 fifo_addr, u16 child_scb_addr,
                                u32 dest, struct dsp_scb_descriptor * parent_scb,
                                int scb_child_type)
{
	struct dsp_scb_descriptor * scb;
  
	struct dsp_codec_output_scb codec_out_scb = {
		{ 0,
		  0,
		  0,
		  0
		},
		{
			0,
			0,
			0,
			0,
			0
		},
		0,0,
		0,NULL_SCB_ADDR,
		0,                      /* COstrmRsConfig */
		0,                      /* COstrmBufPtr */
		channel_disp,fifo_addr, /* leftChanBaseIOaddr:rightChanIOdisp */
		0x0000,0x0080,          /* (!AC97!) COexpVolChangeRate:COscaleShiftCount */
		0,child_scb_addr        /* COreserved - need child scb to work with rom code */
	};
  
  
	scb = cs46xx_dsp_create_generic_scb(chip,codec_name,(u32 *)&codec_out_scb,
					    dest,"S16_CODECOUTPUTTASK",parent_scb,
					    scb_child_type);
  
	return scb;
}

struct dsp_scb_descriptor * 
cs46xx_dsp_create_codec_in_scb(struct snd_cs46xx * chip, char * codec_name,
			       u16 channel_disp, u16 fifo_addr, u16 sample_buffer_addr,
			       u32 dest, struct dsp_scb_descriptor * parent_scb,
			       int scb_child_type)
{

	struct dsp_scb_descriptor * scb;
	struct dsp_codec_input_scb codec_input_scb = {
		{ 0,
		  0,
		  0,
		  0
		},
		{
			0,
			0,
			0,
			0,
			0
		},
    
#if 0  /* cs4620 */
		SyncIOSCB,NULL_SCB_ADDR
#else
		0 , 0,
#endif
		0,0,

		RSCONFIG_SAMPLE_16STEREO + RSCONFIG_MODULO_64,  /* strmRsConfig */
		sample_buffer_addr << 0x10,       /* strmBufPtr; defined as a dword ptr, used as a byte ptr */
		channel_disp,fifo_addr,           /* (!AC97!) leftChanBaseINaddr=AC97primary 
						     link input slot 3 :rightChanINdisp=""slot 4 */
		0x0000,0x0000,                    /* (!AC97!) ????:scaleShiftCount; no shift needed 
						     because AC97 is already 20 bits */
		0x80008000                        /* ??clw cwcgame.scb has 0 */
	};
  
	scb = cs46xx_dsp_create_generic_scb(chip,codec_name,(u32 *)&codec_input_scb,
					    dest,"S16_CODECINPUTTASK",parent_scb,
					    scb_child_type);
	return scb;
}


static struct dsp_scb_descriptor * 
cs46xx_dsp_create_pcm_reader_scb(struct snd_cs46xx * chip, char * scb_name,
                                 u16 sample_buffer_addr, u32 dest,
                                 int virtual_channel, u32 playback_hw_addr,
                                 struct dsp_scb_descriptor * parent_scb,
                                 int scb_child_type)
{
	struct dsp_spos_instance * ins = chip->dsp_spos_instance;
	struct dsp_scb_descriptor * scb;
  
	struct dsp_generic_scb pcm_reader_scb = {
    
		/*
		  Play DMA Task xfers data from host buffer to SP buffer
		  init/runtime variables:
		  PlayAC: Play Audio Data Conversion - SCB loc: 2nd dword, mask: 0x0000F000L
		  DATA_FMT_16BIT_ST_LTLEND(0x00000000L)   from 16-bit stereo, little-endian
		  DATA_FMT_8_BIT_ST_SIGNED(0x00001000L)   from 8-bit stereo, signed
		  DATA_FMT_16BIT_MN_LTLEND(0x00002000L)   from 16-bit mono, little-endian
		  DATA_FMT_8_BIT_MN_SIGNED(0x00003000L)   from 8-bit mono, signed
		  DATA_FMT_16BIT_ST_BIGEND(0x00004000L)   from 16-bit stereo, big-endian
		  DATA_FMT_16BIT_MN_BIGEND(0x00006000L)   from 16-bit mono, big-endian
		  DATA_FMT_8_BIT_ST_UNSIGNED(0x00009000L) from 8-bit stereo, unsigned
		  DATA_FMT_8_BIT_MN_UNSIGNED(0x0000b000L) from 8-bit mono, unsigned
		  ? Other combinations possible from:
		  DMA_RQ_C2_AUDIO_CONVERT_MASK    0x0000F000L
		  DMA_RQ_C2_AC_NONE               0x00000000L
		  DMA_RQ_C2_AC_8_TO_16_BIT        0x00001000L
		  DMA_RQ_C2_AC_MONO_TO_STEREO     0x00002000L
		  DMA_RQ_C2_AC_ENDIAN_CONVERT     0x00004000L
		  DMA_RQ_C2_AC_SIGNED_CONVERT     0x00008000L
        
		  HostBuffAddr: Host Buffer Physical Byte Address - SCB loc:3rd dword, Mask: 0xFFFFFFFFL
		  aligned to dword boundary
		*/
		/* Basic (non scatter/gather) DMA requestor (4 ints) */
		{ DMA_RQ_C1_SOURCE_ON_HOST +        /* source buffer is on the host */
		  DMA_RQ_C1_SOURCE_MOD1024 +        /* source buffer is 1024 dwords (4096 bytes) */
		  DMA_RQ_C1_DEST_MOD32 +            /* dest buffer(PCMreaderBuf) is 32 dwords*/
		  DMA_RQ_C1_WRITEBACK_SRC_FLAG +    /* ?? */
		  DMA_RQ_C1_WRITEBACK_DEST_FLAG +   /* ?? */
		  15,                             /* DwordCount-1: picked 16 for DwordCount because Jim */
		  /*        Barnette said that is what we should use since */
		  /*        we are not running in optimized mode? */
		  DMA_RQ_C2_AC_NONE +
		  DMA_RQ_C2_SIGNAL_SOURCE_PINGPONG + /* set play interrupt (bit0) in HISR when source */
		  /*   buffer (on host) crosses half-way point */
		  virtual_channel,                   /* Play DMA channel arbitrarily set to 0 */
		  playback_hw_addr,                  /* HostBuffAddr (source) */
		  DMA_RQ_SD_SP_SAMPLE_ADDR +         /* destination buffer is in SP Sample Memory */
		  sample_buffer_addr                 /* SP Buffer Address (destination) */
		},
		/* Scatter/gather DMA requestor extension   (5 ints) */
		{
			0,
			0,
			0,
			0,
			0 
		},
		/* Sublist pointer & next stream control block (SCB) link. */
		NULL_SCB_ADDR,NULL_SCB_ADDR,
		/* Pointer to this tasks parameter block & stream function pointer */
		0,NULL_SCB_ADDR,
		/* rsConfig register for stream buffer (rsDMA reg. is loaded from basicReq.daw */
		/*   for incoming streams, or basicReq.saw, for outgoing streams) */
		RSCONFIG_DMA_ENABLE +                 /* enable DMA */
		(19 << RSCONFIG_MAX_DMA_SIZE_SHIFT) + /* MAX_DMA_SIZE picked to be 19 since SPUD  */
		/*  uses it for some reason */
		((dest >> 4) << RSCONFIG_STREAM_NUM_SHIFT) + /* stream number = SCBaddr/16 */
		RSCONFIG_SAMPLE_16STEREO +
		RSCONFIG_MODULO_32,             /* dest buffer(PCMreaderBuf) is 32 dwords (256 bytes) */
		/* Stream sample pointer & MAC-unit mode for this stream */
		(sample_buffer_addr << 0x10),
		/* Fractional increment per output sample in the input sample buffer */
		0, 
		{
			/* Standard stereo volume control
			   default muted */
			0xffff,0xffff,
			0xffff,0xffff
		}
	};

	if (ins->null_algorithm == NULL) {
		ins->null_algorithm =  cs46xx_dsp_lookup_symbol (chip,"NULLALGORITHM",
								 SYMBOL_CODE);
    
		if (ins->null_algorithm == NULL) {
			dev_err(chip->card->dev,
				"dsp_spos: symbol NULLALGORITHM not found\n");
			return NULL;
		}    
	}

	scb = _dsp_create_generic_scb(chip,scb_name,(u32 *)&pcm_reader_scb,
				      dest,ins->null_algorithm,parent_scb,
				      scb_child_type);
  
	return scb;
}

#define GOF_PER_SEC 200

struct dsp_scb_descriptor * 
cs46xx_dsp_create_src_task_scb(struct snd_cs46xx * chip, char * scb_name,
			       int rate,
                               u16 src_buffer_addr,
                               u16 src_delay_buffer_addr, u32 dest,
                               struct dsp_scb_descriptor * parent_scb,
                               int scb_child_type,
	                       int pass_through)
{

	struct dsp_spos_instance * ins = chip->dsp_spos_instance;
	struct dsp_scb_descriptor * scb;
	unsigned int tmp1, tmp2;
	unsigned int phiIncr;
	unsigned int correctionPerGOF, correctionPerSec;

	dev_dbg(chip->card->dev, "dsp_spos: setting %s rate to %u\n",
		scb_name, rate);

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

	{
		struct dsp_src_task_scb src_task_scb = {
			0x0028,0x00c8,
			0x5555,0x0000,
			0x0000,0x0000,
			src_buffer_addr,1,
			correctionPerGOF,correctionPerSec,
			RSCONFIG_SAMPLE_16STEREO + RSCONFIG_MODULO_32,  
			0x0000,src_delay_buffer_addr,                  
			0x0,                                            
			0x080,(src_delay_buffer_addr + (24 * 4)),
			0,0, /* next_scb, sub_list_ptr */
			0,0, /* entry, this_spb */
			RSCONFIG_SAMPLE_16STEREO + RSCONFIG_MODULO_8,
			src_buffer_addr << 0x10,
			phiIncr,
			{ 
				0xffff - ins->dac_volume_right,0xffff - ins->dac_volume_left,
				0xffff - ins->dac_volume_right,0xffff - ins->dac_volume_left
			}
		};
		
		if (ins->s16_up == NULL) {
			ins->s16_up =  cs46xx_dsp_lookup_symbol (chip,"S16_UPSRC",
								 SYMBOL_CODE);
			
			if (ins->s16_up == NULL) {
				dev_err(chip->card->dev,
					"dsp_spos: symbol S16_UPSRC not found\n");
				return NULL;
			}    
		}
		
		/* clear buffers */
		_dsp_clear_sample_buffer (chip,src_buffer_addr,8);
		_dsp_clear_sample_buffer (chip,src_delay_buffer_addr,32);
				
		if (pass_through) {
			/* wont work with any other rate than
			   the native DSP rate */
			snd_BUG_ON(rate != 48000);

			scb = cs46xx_dsp_create_generic_scb(chip,scb_name,(u32 *)&src_task_scb,
							    dest,"DMAREADER",parent_scb,
							    scb_child_type);
		} else {
			scb = _dsp_create_generic_scb(chip,scb_name,(u32 *)&src_task_scb,
						      dest,ins->s16_up,parent_scb,
						      scb_child_type);
		}


	}

	return scb;
}

#if 0 /* not used */
struct dsp_scb_descriptor * 
cs46xx_dsp_create_filter_scb(struct snd_cs46xx * chip, char * scb_name,
			     u16 buffer_addr, u32 dest,
			     struct dsp_scb_descriptor * parent_scb,
			     int scb_child_type) {
	struct dsp_scb_descriptor * scb;
	
	struct dsp_filter_scb filter_scb = {
		.a0_right            = 0x41a9,
		.a0_left             = 0x41a9,
		.a1_right            = 0xb8e4,
		.a1_left             = 0xb8e4,
		.a2_right            = 0x3e55,
		.a2_left             = 0x3e55,
		
		.filter_unused3      = 0x0000,
		.filter_unused2      = 0x0000,

		.output_buf_ptr      = buffer_addr,
		.init                = 0x000,

		.prev_sample_output1 = 0x00000000,
		.prev_sample_output2 = 0x00000000,

		.prev_sample_input1  = 0x00000000,
		.prev_sample_input2  = 0x00000000,

		.next_scb_ptr        = 0x0000,
		.sub_list_ptr        = 0x0000,

		.entry_point         = 0x0000,
		.spb_ptr             = 0x0000,

		.b0_right            = 0x0e38,
		.b0_left             = 0x0e38,
		.b1_right            = 0x1c71,
		.b1_left             = 0x1c71,
		.b2_right            = 0x0e38,
		.b2_left             = 0x0e38,
	};


	scb = cs46xx_dsp_create_generic_scb(chip,scb_name,(u32 *)&filter_scb,
					    dest,"FILTERTASK",parent_scb,
					    scb_child_type);

 	return scb;
}
#endif /* not used */

struct dsp_scb_descriptor * 
cs46xx_dsp_create_mix_only_scb(struct snd_cs46xx * chip, char * scb_name,
                               u16 mix_buffer_addr, u32 dest,
                               struct dsp_scb_descriptor * parent_scb,
                               int scb_child_type)
{
	struct dsp_scb_descriptor * scb;
  
	struct dsp_mix_only_scb master_mix_scb = {
		/* 0 */ { 0,
			  /* 1 */   0,
			  /* 2 */  mix_buffer_addr,
			  /* 3 */  0
			  /*   */ },
		{
			/* 4 */  0,
			/* 5 */  0,
			/* 6 */  0,
			/* 7 */  0,
			/* 8 */  0x00000080
		},
		/* 9 */ 0,0,
		/* A */ 0,0,
		/* B */ RSCONFIG_SAMPLE_16STEREO + RSCONFIG_MODULO_32,
		/* C */ (mix_buffer_addr  + (16 * 4)) << 0x10, 
		/* D */ 0,
		{
			/* E */ 0x8000,0x8000,
			/* F */ 0x8000,0x8000
		}
	};


	scb = cs46xx_dsp_create_generic_scb(chip,scb_name,(u32 *)&master_mix_scb,
					    dest,"S16_MIX",parent_scb,
					    scb_child_type);
	return scb;
}


struct dsp_scb_descriptor * 
cs46xx_dsp_create_mix_to_ostream_scb(struct snd_cs46xx * chip, char * scb_name,
                                     u16 mix_buffer_addr, u16 writeback_spb, u32 dest,
                                     struct dsp_scb_descriptor * parent_scb,
                                     int scb_child_type)
{
	struct dsp_scb_descriptor * scb;

	struct dsp_mix2_ostream_scb mix2_ostream_scb = {
		/* Basic (non scatter/gather) DMA requestor (4 ints) */
		{ 
			DMA_RQ_C1_SOURCE_MOD64 +
			DMA_RQ_C1_DEST_ON_HOST +
			DMA_RQ_C1_DEST_MOD1024 +
			DMA_RQ_C1_WRITEBACK_SRC_FLAG + 
			DMA_RQ_C1_WRITEBACK_DEST_FLAG +
			15,                            
      
			DMA_RQ_C2_AC_NONE +
			DMA_RQ_C2_SIGNAL_DEST_PINGPONG + 
      
			CS46XX_DSP_CAPTURE_CHANNEL,                                 
			DMA_RQ_SD_SP_SAMPLE_ADDR + 
			mix_buffer_addr, 
			0x0                   
		},
    
		{ 0, 0, 0, 0, 0, },
		0,0,
		0,writeback_spb,
    
		RSCONFIG_DMA_ENABLE + 
		(19 << RSCONFIG_MAX_DMA_SIZE_SHIFT) + 
    
		((dest >> 4) << RSCONFIG_STREAM_NUM_SHIFT) +
		RSCONFIG_DMA_TO_HOST + 
		RSCONFIG_SAMPLE_16STEREO +
		RSCONFIG_MODULO_64,    
		(mix_buffer_addr + (32 * 4)) << 0x10,
		1,0,            
		0x0001,0x0080,
		0xFFFF,0
	};


	scb = cs46xx_dsp_create_generic_scb(chip,scb_name,(u32 *)&mix2_ostream_scb,
				
	    dest,"S16_MIX_TO_OSTREAM",parent_scb,
					    scb_child_type);
  
	return scb;
}


struct dsp_scb_descriptor * 
cs46xx_dsp_create_vari_decimate_scb(struct snd_cs46xx * chip,char * scb_name,
                                    u16 vari_buffer_addr0,
                                    u16 vari_buffer_addr1,
                                    u32 dest,
                                    struct dsp_scb_descriptor * parent_scb,
                                    int scb_child_type)
{

	struct dsp_scb_descriptor * scb;
  
	struct dsp_vari_decimate_scb vari_decimate_scb = {
		0x0028,0x00c8,
		0x5555,0x0000,
		0x0000,0x0000,
		vari_buffer_addr0,vari_buffer_addr1,
    
		0x0028,0x00c8,
		RSCONFIG_SAMPLE_16STEREO + RSCONFIG_MODULO_256, 
    
		0xFF800000,   
		0,
		0x0080,vari_buffer_addr1 + (25 * 4), 
    
		0,0, 
		0,0,

		RSCONFIG_SAMPLE_16STEREO + RSCONFIG_MODULO_8,
		vari_buffer_addr0 << 0x10,   
		0x04000000,                   
		{
			0x8000,0x8000, 
			0xFFFF,0xFFFF
		}
	};

	scb = cs46xx_dsp_create_generic_scb(chip,scb_name,(u32 *)&vari_decimate_scb,
					    dest,"VARIDECIMATE",parent_scb,
					    scb_child_type);
  
	return scb;
}


static struct dsp_scb_descriptor * 
cs46xx_dsp_create_pcm_serial_input_scb(struct snd_cs46xx * chip, char * scb_name, u32 dest,
                                       struct dsp_scb_descriptor * input_scb,
                                       struct dsp_scb_descriptor * parent_scb,
                                       int scb_child_type)
{

	struct dsp_scb_descriptor * scb;


	struct dsp_pcm_serial_input_scb pcm_serial_input_scb = {
		{ 0,
		  0,
		  0,
		  0
		},
		{
			0,
			0,
			0,
			0,
			0
		},

		0,0,
		0,0,

		RSCONFIG_SAMPLE_16STEREO + RSCONFIG_MODULO_16,
		0,
      /* 0xD */ 0,input_scb->address,
		{
      /* 0xE */   0x8000,0x8000,
      /* 0xF */	  0x8000,0x8000
		}
	};

	scb = cs46xx_dsp_create_generic_scb(chip,scb_name,(u32 *)&pcm_serial_input_scb,
					    dest,"PCMSERIALINPUTTASK",parent_scb,
					    scb_child_type);
	return scb;
}


static struct dsp_scb_descriptor * 
cs46xx_dsp_create_asynch_fg_tx_scb(struct snd_cs46xx * chip, char * scb_name, u32 dest,
                                   u16 hfg_scb_address,
                                   u16 asynch_buffer_address,
                                   struct dsp_scb_descriptor * parent_scb,
                                   int scb_child_type)
{

	struct dsp_scb_descriptor * scb;

	struct dsp_asynch_fg_tx_scb asynch_fg_tx_scb = {
		0xfc00,0x03ff,      /*  Prototype sample buffer size of 256 dwords */
		0x0058,0x0028,      /* Min Delta 7 dwords == 28 bytes */
		/* : Max delta 25 dwords == 100 bytes */
		0,hfg_scb_address,  /* Point to HFG task SCB */
		0,0,		    /* Initialize current Delta and Consumer ptr adjustment count */
		0,                  /* Initialize accumulated Phi to 0 */
		0,0x2aab,           /* Const 1/3 */
    
		{
			0,         /* Define the unused elements */
			0,
			0
		},
    
		0,0,
		0,dest + AFGTxAccumPhi,
    
		RSCONFIG_SAMPLE_16STEREO + RSCONFIG_MODULO_256, /* Stereo, 256 dword */
		(asynch_buffer_address) << 0x10,  /* This should be automagically synchronized
                                                     to the producer pointer */
    
		/* There is no correct initial value, it will depend upon the detected
		   rate etc  */
		0x18000000,                     /* Phi increment for approx 32k operation */
		0x8000,0x8000,                  /* Volume controls are unused at this time */
		0x8000,0x8000
	};
  
	scb = cs46xx_dsp_create_generic_scb(chip,scb_name,(u32 *)&asynch_fg_tx_scb,
					    dest,"ASYNCHFGTXCODE",parent_scb,
					    scb_child_type);

	return scb;
}


struct dsp_scb_descriptor * 
cs46xx_dsp_create_asynch_fg_rx_scb(struct snd_cs46xx * chip, char * scb_name, u32 dest,
                                   u16 hfg_scb_address,
                                   u16 asynch_buffer_address,
                                   struct dsp_scb_descriptor * parent_scb,
                                   int scb_child_type)
{
	struct dsp_spos_instance * ins = chip->dsp_spos_instance;
	struct dsp_scb_descriptor * scb;

	struct dsp_asynch_fg_rx_scb asynch_fg_rx_scb = {
		0xfe00,0x01ff,      /*  Prototype sample buffer size of 128 dwords */
		0x0064,0x001c,      /* Min Delta 7 dwords == 28 bytes */
		                    /* : Max delta 25 dwords == 100 bytes */
		0,hfg_scb_address,  /* Point to HFG task SCB */
		0,0,				/* Initialize current Delta and Consumer ptr adjustment count */
		{
			0,                /* Define the unused elements */
			0,
			0,
			0,
			0
		},
      
		0,0,
		0,dest,
    
		RSCONFIG_MODULO_128 |
        RSCONFIG_SAMPLE_16STEREO,                         /* Stereo, 128 dword */
		( (asynch_buffer_address + (16 * 4))  << 0x10),   /* This should be automagically 
							                                  synchrinized to the producer pointer */
    
		/* There is no correct initial value, it will depend upon the detected
		   rate etc  */
		0x18000000,         

		/* Set IEC958 input volume */
		0xffff - ins->spdif_input_volume_right,0xffff - ins->spdif_input_volume_left,
		0xffff - ins->spdif_input_volume_right,0xffff - ins->spdif_input_volume_left,
	};

	scb = cs46xx_dsp_create_generic_scb(chip,scb_name,(u32 *)&asynch_fg_rx_scb,
					    dest,"ASYNCHFGRXCODE",parent_scb,
					    scb_child_type);

	return scb;
}


#if 0 /* not used */
struct dsp_scb_descriptor * 
cs46xx_dsp_create_output_snoop_scb(struct snd_cs46xx * chip, char * scb_name, u32 dest,
                                   u16 snoop_buffer_address,
                                   struct dsp_scb_descriptor * snoop_scb,
                                   struct dsp_scb_descriptor * parent_scb,
                                   int scb_child_type)
{

	struct dsp_scb_descriptor * scb;
  
	struct dsp_output_snoop_scb output_snoop_scb = {
		{ 0,	/*  not used.  Zero */
		  0,
		  0,
		  0,
		},
		{
			0, /* not used.  Zero */
			0,
			0,
			0,
			0
		},
    
		0,0,
		0,0,
    
		RSCONFIG_SAMPLE_16STEREO + RSCONFIG_MODULO_64,
		snoop_buffer_address << 0x10,  
		0,0,
		0,
		0,snoop_scb->address
	};
  
	scb = cs46xx_dsp_create_generic_scb(chip,scb_name,(u32 *)&output_snoop_scb,
					    dest,"OUTPUTSNOOP",parent_scb,
					    scb_child_type);
	return scb;
}
#endif /* not used */


struct dsp_scb_descriptor * 
cs46xx_dsp_create_spio_write_scb(struct snd_cs46xx * chip, char * scb_name, u32 dest,
                                 struct dsp_scb_descriptor * parent_scb,
                                 int scb_child_type)
{
	struct dsp_scb_descriptor * scb;
  
	struct dsp_spio_write_scb spio_write_scb = {
		0,0,         /*   SPIOWAddress2:SPIOWAddress1; */
		0,           /*   SPIOWData1; */
		0,           /*   SPIOWData2; */
		0,0,         /*   SPIOWAddress4:SPIOWAddress3; */
		0,           /*   SPIOWData3; */
		0,           /*   SPIOWData4; */
		0,0,         /*   SPIOWDataPtr:Unused1; */
		{ 0,0 },     /*   Unused2[2]; */
    
		0,0,	     /*   SPIOWChildPtr:SPIOWSiblingPtr; */
		0,0,         /*   SPIOWThisPtr:SPIOWEntryPoint; */
    
		{ 
			0,
			0,
			0,
			0,
			0          /*   Unused3[5];  */
		}
	};

	scb = cs46xx_dsp_create_generic_scb(chip,scb_name,(u32 *)&spio_write_scb,
					    dest,"SPIOWRITE",parent_scb,
					    scb_child_type);

	return scb;
}

struct dsp_scb_descriptor *
cs46xx_dsp_create_magic_snoop_scb(struct snd_cs46xx * chip, char * scb_name, u32 dest,
				  u16 snoop_buffer_address,
				  struct dsp_scb_descriptor * snoop_scb,
				  struct dsp_scb_descriptor * parent_scb,
				  int scb_child_type)
{
	struct dsp_scb_descriptor * scb;
  
	struct dsp_magic_snoop_task magic_snoop_scb = {
		/* 0 */ 0, /* i0 */
		/* 1 */ 0, /* i1 */
		/* 2 */ snoop_buffer_address << 0x10,
		/* 3 */ 0,snoop_scb->address,
		/* 4 */ 0, /* i3 */
		/* 5 */ 0, /* i4 */
		/* 6 */ 0, /* i5 */
		/* 7 */ 0, /* i6 */
		/* 8 */ 0, /* i7 */
		/* 9 */ 0,0, /* next_scb, sub_list_ptr */
		/* A */ 0,0, /* entry_point, this_ptr */
		/* B */ RSCONFIG_SAMPLE_16STEREO + RSCONFIG_MODULO_64,
		/* C */ snoop_buffer_address  << 0x10,
		/* D */ 0,
		/* E */ { 0x8000,0x8000,
	        /* F */   0xffff,0xffff
		}
	};

	scb = cs46xx_dsp_create_generic_scb(chip,scb_name,(u32 *)&magic_snoop_scb,
					    dest,"MAGICSNOOPTASK",parent_scb,
					    scb_child_type);

	return scb;
}

static struct dsp_scb_descriptor *
find_next_free_scb (struct snd_cs46xx * chip, struct dsp_scb_descriptor * from)
{
	struct dsp_spos_instance * ins = chip->dsp_spos_instance;
	struct dsp_scb_descriptor * scb = from;

	while (scb->next_scb_ptr != ins->the_null_scb) {
		if (snd_BUG_ON(!scb->next_scb_ptr))
			return NULL;

		scb = scb->next_scb_ptr;
	}

	return scb;
}

static const u32 pcm_reader_buffer_addr[DSP_MAX_PCM_CHANNELS] = {
	0x0600, /* 1 */
	0x1500, /* 2 */
	0x1580, /* 3 */
	0x1600, /* 4 */
	0x1680, /* 5 */
	0x1700, /* 6 */
	0x1780, /* 7 */
	0x1800, /* 8 */
	0x1880, /* 9 */
	0x1900, /* 10 */
	0x1980, /* 11 */
	0x1A00, /* 12 */
	0x1A80, /* 13 */
	0x1B00, /* 14 */
	0x1B80, /* 15 */
	0x1C00, /* 16 */
	0x1C80, /* 17 */
	0x1D00, /* 18 */
	0x1D80, /* 19 */
	0x1E00, /* 20 */
	0x1E80, /* 21 */
	0x1F00, /* 22 */
	0x1F80, /* 23 */
	0x2000, /* 24 */
	0x2080, /* 25 */
	0x2100, /* 26 */
	0x2180, /* 27 */
	0x2200, /* 28 */
	0x2280, /* 29 */
	0x2300, /* 30 */
	0x2380, /* 31 */
	0x2400, /* 32 */
};

static const u32 src_output_buffer_addr[DSP_MAX_SRC_NR] = {
	0x2B80,
	0x2BA0,
	0x2BC0,
	0x2BE0,
	0x2D00,  
	0x2D20,  
	0x2D40,  
	0x2D60,
	0x2D80,
	0x2DA0,
	0x2DC0,
	0x2DE0,
	0x2E00,
	0x2E20
};

static const u32 src_delay_buffer_addr[DSP_MAX_SRC_NR] = {
	0x2480,
	0x2500,
	0x2580,
	0x2600,
	0x2680,
	0x2700,
	0x2780,
	0x2800,
	0x2880,
	0x2900,
	0x2980,
	0x2A00,
	0x2A80,
	0x2B00
};

struct dsp_pcm_channel_descriptor *
cs46xx_dsp_create_pcm_channel (struct snd_cs46xx * chip,
			       u32 sample_rate, void * private_data, 
			       u32 hw_dma_addr,
			       int pcm_channel_id)
{
	struct dsp_spos_instance * ins = chip->dsp_spos_instance;
	struct dsp_scb_descriptor * src_scb = NULL, * pcm_scb, * mixer_scb = NULL;
	struct dsp_scb_descriptor * src_parent_scb = NULL;

	/* struct dsp_scb_descriptor * pcm_parent_scb; */
	char scb_name[DSP_MAX_SCB_NAME];
	int i, pcm_index = -1, insert_point, src_index = -1, pass_through = 0;

	switch (pcm_channel_id) {
	case DSP_PCM_MAIN_CHANNEL:
		mixer_scb = ins->master_mix_scb;
		break;
	case DSP_PCM_REAR_CHANNEL:
		mixer_scb = ins->rear_mix_scb;
		break;
	case DSP_PCM_CENTER_LFE_CHANNEL:
		mixer_scb = ins->center_lfe_mix_scb;
		break;
	case DSP_PCM_S71_CHANNEL:
		/* TODO */
		snd_BUG();
		break;
	case DSP_IEC958_CHANNEL:
		if (snd_BUG_ON(!ins->asynch_tx_scb))
			return NULL;
		mixer_scb = ins->asynch_tx_scb;

		/* if sample rate is set to 48khz we pass
		   the Sample Rate Converted (which could
		   alter the raw data stream ...) */
		if (sample_rate == 48000) {
			dev_dbg(chip->card->dev, "IEC958 pass through\n");
			/* Hack to bypass creating a new SRC */
			pass_through = 1;
		}
		break;
	default:
		snd_BUG();
		return NULL;
	}
	/* default sample rate is 44100 */
	if (!sample_rate) sample_rate = 44100;

	/* search for a already created SRC SCB with the same sample rate */
	for (i = 0; i < DSP_MAX_PCM_CHANNELS && 
		     (pcm_index == -1 || src_scb == NULL); ++i) {

		/* virtual channel reserved 
		   for capture */
		if (i == CS46XX_DSP_CAPTURE_CHANNEL) continue;

		if (ins->pcm_channels[i].active) {
			if (!src_scb && 
			    ins->pcm_channels[i].sample_rate == sample_rate &&
			    ins->pcm_channels[i].mixer_scb == mixer_scb) {
				src_scb = ins->pcm_channels[i].src_scb;
				ins->pcm_channels[i].src_scb->ref_count ++;
				src_index = ins->pcm_channels[i].src_slot;
			}
		} else if (pcm_index == -1) {
			pcm_index = i;
		}
	}

	if (pcm_index == -1) {
		dev_err(chip->card->dev, "dsp_spos: no free PCM channel\n");
		return NULL;
	}

	if (src_scb == NULL) {
		if (ins->nsrc_scb >= DSP_MAX_SRC_NR) {
			dev_err(chip->card->dev,
				"dsp_spos: too many SRC instances\n!");
			return NULL;
		}

		/* find a free slot */
		for (i = 0; i < DSP_MAX_SRC_NR; ++i) {
			if (ins->src_scb_slots[i] == 0) {
				src_index = i;
				ins->src_scb_slots[i] = 1;
				break;
			}
		}
		if (snd_BUG_ON(src_index == -1))
			return NULL;

		/* we need to create a new SRC SCB */
		if (mixer_scb->sub_list_ptr == ins->the_null_scb) {
			src_parent_scb = mixer_scb;
			insert_point = SCB_ON_PARENT_SUBLIST_SCB;
		} else {
			src_parent_scb = find_next_free_scb(chip,mixer_scb->sub_list_ptr);
			insert_point = SCB_ON_PARENT_NEXT_SCB;
		}

		snprintf (scb_name,DSP_MAX_SCB_NAME,"SrcTask_SCB%d",src_index);
		
		dev_dbg(chip->card->dev,
			"dsp_spos: creating SRC \"%s\"\n", scb_name);
		src_scb = cs46xx_dsp_create_src_task_scb(chip,scb_name,
							 sample_rate,
							 src_output_buffer_addr[src_index],
							 src_delay_buffer_addr[src_index],
							 /* 0x400 - 0x600 source SCBs */
							 0x400 + (src_index * 0x10) ,
							 src_parent_scb,
							 insert_point,
							 pass_through);

		if (!src_scb) {
			dev_err(chip->card->dev,
				"dsp_spos: failed to create SRCtaskSCB\n");
			return NULL;
		}

		/* cs46xx_dsp_set_src_sample_rate(chip,src_scb,sample_rate); */

		ins->nsrc_scb ++;
	} 
  
  
	snprintf (scb_name,DSP_MAX_SCB_NAME,"PCMReader_SCB%d",pcm_index);

	dev_dbg(chip->card->dev, "dsp_spos: creating PCM \"%s\" (%d)\n",
		scb_name, pcm_channel_id);

	pcm_scb = cs46xx_dsp_create_pcm_reader_scb(chip,scb_name,
						   pcm_reader_buffer_addr[pcm_index],
						   /* 0x200 - 400 PCMreader SCBs */
						   (pcm_index * 0x10) + 0x200,
						   pcm_index,    /* virtual channel 0-31 */
						   hw_dma_addr,  /* pcm hw addr */
                           NULL,         /* parent SCB ptr */
                           0             /* insert point */ 
                           );

	if (!pcm_scb) {
		dev_err(chip->card->dev,
			"dsp_spos: failed to create PCMreaderSCB\n");
		return NULL;
	}
	
	guard(spinlock_irqsave)(&chip->reg_lock);
	ins->pcm_channels[pcm_index].sample_rate = sample_rate;
	ins->pcm_channels[pcm_index].pcm_reader_scb = pcm_scb;
	ins->pcm_channels[pcm_index].src_scb = src_scb;
	ins->pcm_channels[pcm_index].unlinked = 1;
	ins->pcm_channels[pcm_index].private_data = private_data;
	ins->pcm_channels[pcm_index].src_slot = src_index;
	ins->pcm_channels[pcm_index].active = 1;
	ins->pcm_channels[pcm_index].pcm_slot = pcm_index;
	ins->pcm_channels[pcm_index].mixer_scb = mixer_scb;
	ins->npcm_channels ++;

	return (ins->pcm_channels + pcm_index);
}

int cs46xx_dsp_pcm_channel_set_period (struct snd_cs46xx * chip,
				       struct dsp_pcm_channel_descriptor * pcm_channel,
				       int period_size)
{
	u32 temp = snd_cs46xx_peek (chip,pcm_channel->pcm_reader_scb->address << 2);
	temp &= ~DMA_RQ_C1_SOURCE_SIZE_MASK;

	switch (period_size) {
	case 2048:
		temp |= DMA_RQ_C1_SOURCE_MOD1024;
		break;
	case 1024:
		temp |= DMA_RQ_C1_SOURCE_MOD512;
		break;
	case 512:
		temp |= DMA_RQ_C1_SOURCE_MOD256;
		break;
	case 256:
		temp |= DMA_RQ_C1_SOURCE_MOD128;
		break;
	case 128:
		temp |= DMA_RQ_C1_SOURCE_MOD64;
		break;
	case 64:
		temp |= DMA_RQ_C1_SOURCE_MOD32;
		break;		      
	case 32:
		temp |= DMA_RQ_C1_SOURCE_MOD16;
		break; 
	default:
		dev_dbg(chip->card->dev,
			"period size (%d) not supported by HW\n", period_size);
		return -EINVAL;
	}

	snd_cs46xx_poke (chip,pcm_channel->pcm_reader_scb->address << 2,temp);

	return 0;
}

int cs46xx_dsp_pcm_ostream_set_period (struct snd_cs46xx * chip,
				       int period_size)
{
	u32 temp = snd_cs46xx_peek (chip,WRITEBACK_SCB_ADDR << 2);
	temp &= ~DMA_RQ_C1_DEST_SIZE_MASK;

	switch (period_size) {
	case 2048:
		temp |= DMA_RQ_C1_DEST_MOD1024;
		break;
	case 1024:
		temp |= DMA_RQ_C1_DEST_MOD512;
		break;
	case 512:
		temp |= DMA_RQ_C1_DEST_MOD256;
		break;
	case 256:
		temp |= DMA_RQ_C1_DEST_MOD128;
		break;
	case 128:
		temp |= DMA_RQ_C1_DEST_MOD64;
		break;
	case 64:
		temp |= DMA_RQ_C1_DEST_MOD32;
		break;		      
	case 32:
		temp |= DMA_RQ_C1_DEST_MOD16;
		break; 
	default:
		dev_dbg(chip->card->dev,
			"period size (%d) not supported by HW\n", period_size);
		return -EINVAL;
	}

	snd_cs46xx_poke (chip,WRITEBACK_SCB_ADDR << 2,temp);

	return 0;
}

void cs46xx_dsp_destroy_pcm_channel (struct snd_cs46xx * chip,
				     struct dsp_pcm_channel_descriptor * pcm_channel)
{
	struct dsp_spos_instance * ins = chip->dsp_spos_instance;

	if (snd_BUG_ON(!pcm_channel->active ||
		       ins->npcm_channels <= 0 ||
		       pcm_channel->src_scb->ref_count <= 0))
		return;

	scoped_guard(spinlock_irqsave, &chip->reg_lock) {
		pcm_channel->unlinked = 1;
		pcm_channel->active = 0;
		pcm_channel->private_data = NULL;
		pcm_channel->src_scb->ref_count--;
		ins->npcm_channels--;
	}

	cs46xx_dsp_remove_scb(chip,pcm_channel->pcm_reader_scb);

	if (!pcm_channel->src_scb->ref_count) {
		cs46xx_dsp_remove_scb(chip,pcm_channel->src_scb);

		if (snd_BUG_ON(pcm_channel->src_slot < 0 ||
			       pcm_channel->src_slot >= DSP_MAX_SRC_NR))
			return;

		ins->src_scb_slots[pcm_channel->src_slot] = 0;
		ins->nsrc_scb --;
	}
}

int cs46xx_dsp_pcm_unlink (struct snd_cs46xx * chip,
			   struct dsp_pcm_channel_descriptor * pcm_channel)
{
	if (snd_BUG_ON(!pcm_channel->active ||
		       chip->dsp_spos_instance->npcm_channels <= 0))
		return -EIO;

	guard(spinlock_irqsave)(&chip->reg_lock);
	if (pcm_channel->unlinked)
		return -EIO;

	pcm_channel->unlinked = 1;

	_dsp_unlink_scb (chip,pcm_channel->pcm_reader_scb);

	return 0;
}

int cs46xx_dsp_pcm_link (struct snd_cs46xx * chip,
			 struct dsp_pcm_channel_descriptor * pcm_channel)
{
	struct dsp_spos_instance * ins = chip->dsp_spos_instance;
	struct dsp_scb_descriptor * parent_scb;
	struct dsp_scb_descriptor * src_scb = pcm_channel->src_scb;

	guard(spinlock_irqsave)(&chip->reg_lock);

	if (pcm_channel->unlinked == 0)
		return -EIO;

	parent_scb = src_scb;

	if (src_scb->sub_list_ptr != ins->the_null_scb) {
		src_scb->sub_list_ptr->parent_scb_ptr = pcm_channel->pcm_reader_scb;
		pcm_channel->pcm_reader_scb->next_scb_ptr = src_scb->sub_list_ptr;
	}

	src_scb->sub_list_ptr = pcm_channel->pcm_reader_scb;

	snd_BUG_ON(pcm_channel->pcm_reader_scb->parent_scb_ptr);
	pcm_channel->pcm_reader_scb->parent_scb_ptr = parent_scb;

	/* update SCB entry in DSP RAM */
	cs46xx_dsp_spos_update_scb(chip,pcm_channel->pcm_reader_scb);

	/* update parent SCB entry */
	cs46xx_dsp_spos_update_scb(chip,parent_scb);

	pcm_channel->unlinked = 0;
	return 0;
}

struct dsp_scb_descriptor *
cs46xx_add_record_source (struct snd_cs46xx *chip, struct dsp_scb_descriptor * source,
			  u16 addr, char * scb_name)
{
  	struct dsp_spos_instance * ins = chip->dsp_spos_instance;
	struct dsp_scb_descriptor * parent;
	struct dsp_scb_descriptor * pcm_input;
	int insert_point;

	if (snd_BUG_ON(!ins->record_mixer_scb))
		return NULL;

	if (ins->record_mixer_scb->sub_list_ptr != ins->the_null_scb) {
		parent = find_next_free_scb (chip,ins->record_mixer_scb->sub_list_ptr);
		insert_point = SCB_ON_PARENT_NEXT_SCB;
	} else {
		parent = ins->record_mixer_scb;
		insert_point = SCB_ON_PARENT_SUBLIST_SCB;
	}

	pcm_input = cs46xx_dsp_create_pcm_serial_input_scb(chip,scb_name,addr,
							   source, parent,
							   insert_point);

	return pcm_input;
}

int cs46xx_src_unlink(struct snd_cs46xx *chip, struct dsp_scb_descriptor * src)
{
	if (snd_BUG_ON(!src->parent_scb_ptr))
		return -EINVAL;

	/* mute SCB */
	cs46xx_dsp_scb_set_volume (chip,src,0,0);

	guard(spinlock_irqsave)(&chip->reg_lock);
	_dsp_unlink_scb (chip,src);

	return 0;
}

int cs46xx_src_link(struct snd_cs46xx *chip, struct dsp_scb_descriptor * src)
{
	struct dsp_spos_instance * ins = chip->dsp_spos_instance;
	struct dsp_scb_descriptor * parent_scb;

	if (snd_BUG_ON(src->parent_scb_ptr))
		return -EINVAL;
	if (snd_BUG_ON(!ins->master_mix_scb))
		return -EINVAL;

	if (ins->master_mix_scb->sub_list_ptr != ins->the_null_scb) {
		parent_scb = find_next_free_scb (chip,ins->master_mix_scb->sub_list_ptr);
		parent_scb->next_scb_ptr = src;
	} else {
		parent_scb = ins->master_mix_scb;
		parent_scb->sub_list_ptr = src;
	}

	src->parent_scb_ptr = parent_scb;

	/* update entry in DSP RAM */
	cs46xx_dsp_spos_update_scb(chip,parent_scb);
  
	return 0;
}

int cs46xx_dsp_enable_spdif_out (struct snd_cs46xx *chip)
{
	struct dsp_spos_instance * ins = chip->dsp_spos_instance;

	if ( ! (ins->spdif_status_out & DSP_SPDIF_STATUS_HW_ENABLED) ) {
		cs46xx_dsp_enable_spdif_hw (chip);
	}

	/* dont touch anything if SPDIF is open */
	if ( ins->spdif_status_out & DSP_SPDIF_STATUS_PLAYBACK_OPEN) {
		/* when cs46xx_iec958_post_close(...) is called it
		   will call this function if necessary depending on
		   this bit */
		ins->spdif_status_out |= DSP_SPDIF_STATUS_OUTPUT_ENABLED;

		return -EBUSY;
	}

	if (snd_BUG_ON(ins->asynch_tx_scb))
		return -EINVAL;
	if (snd_BUG_ON(ins->master_mix_scb->next_scb_ptr !=
		       ins->the_null_scb))
		return -EINVAL;

	/* reset output snooper sample buffer pointer */
	snd_cs46xx_poke (chip, (ins->ref_snoop_scb->address + 2) << 2,
			 (OUTPUT_SNOOP_BUFFER + 0x10) << 0x10 );
	
	/* The asynch. transfer task */
	ins->asynch_tx_scb = cs46xx_dsp_create_asynch_fg_tx_scb(chip,"AsynchFGTxSCB",ASYNCTX_SCB_ADDR,
								SPDIFO_SCB_INST,
								SPDIFO_IP_OUTPUT_BUFFER1,
								ins->master_mix_scb,
								SCB_ON_PARENT_NEXT_SCB);
	if (!ins->asynch_tx_scb) return -ENOMEM;

	ins->spdif_pcm_input_scb = cs46xx_dsp_create_pcm_serial_input_scb(chip,"PCMSerialInput_II",
									  PCMSERIALINII_SCB_ADDR,
									  ins->ref_snoop_scb,
									  ins->asynch_tx_scb,
									  SCB_ON_PARENT_SUBLIST_SCB);
  
	
	if (!ins->spdif_pcm_input_scb) return -ENOMEM;

	/* monitor state */
	ins->spdif_status_out |= DSP_SPDIF_STATUS_OUTPUT_ENABLED;

	return 0;
}

int  cs46xx_dsp_disable_spdif_out (struct snd_cs46xx *chip)
{
	struct dsp_spos_instance * ins = chip->dsp_spos_instance;

	/* dont touch anything if SPDIF is open */
	if ( ins->spdif_status_out & DSP_SPDIF_STATUS_PLAYBACK_OPEN) {
		ins->spdif_status_out &= ~DSP_SPDIF_STATUS_OUTPUT_ENABLED;
		return -EBUSY;
	}

	/* check integrety */
	if (snd_BUG_ON(!ins->asynch_tx_scb))
		return -EINVAL;
	if (snd_BUG_ON(!ins->spdif_pcm_input_scb))
		return -EINVAL;
	if (snd_BUG_ON(ins->master_mix_scb->next_scb_ptr != ins->asynch_tx_scb))
		return -EINVAL;
	if (snd_BUG_ON(ins->asynch_tx_scb->parent_scb_ptr !=
		       ins->master_mix_scb))
		return -EINVAL;

	cs46xx_dsp_remove_scb (chip,ins->spdif_pcm_input_scb);
	cs46xx_dsp_remove_scb (chip,ins->asynch_tx_scb);

	ins->spdif_pcm_input_scb = NULL;
	ins->asynch_tx_scb = NULL;

	/* clear buffer to prevent any undesired noise */
	_dsp_clear_sample_buffer(chip,SPDIFO_IP_OUTPUT_BUFFER1,256);

	/* monitor state */
	ins->spdif_status_out  &= ~DSP_SPDIF_STATUS_OUTPUT_ENABLED;


	return 0;
}

int cs46xx_iec958_pre_open (struct snd_cs46xx *chip)
{
	struct dsp_spos_instance * ins = chip->dsp_spos_instance;

	if ( ins->spdif_status_out & DSP_SPDIF_STATUS_OUTPUT_ENABLED ) {
		/* remove AsynchFGTxSCB and PCMSerialInput_II */
		cs46xx_dsp_disable_spdif_out (chip);

		/* save state */
		ins->spdif_status_out |= DSP_SPDIF_STATUS_OUTPUT_ENABLED;
	}
	
	/* if not enabled already */
	if ( !(ins->spdif_status_out & DSP_SPDIF_STATUS_HW_ENABLED) ) {
		cs46xx_dsp_enable_spdif_hw (chip);
	}

	/* Create the asynch. transfer task  for playback */
	ins->asynch_tx_scb = cs46xx_dsp_create_asynch_fg_tx_scb(chip,"AsynchFGTxSCB",ASYNCTX_SCB_ADDR,
								SPDIFO_SCB_INST,
								SPDIFO_IP_OUTPUT_BUFFER1,
								ins->master_mix_scb,
								SCB_ON_PARENT_NEXT_SCB);


	/* set spdif channel status value for streaming */
	cs46xx_poke_via_dsp (chip,SP_SPDOUT_CSUV, ins->spdif_csuv_stream);

	ins->spdif_status_out  |= DSP_SPDIF_STATUS_PLAYBACK_OPEN;

	return 0;
}

int cs46xx_iec958_post_close (struct snd_cs46xx *chip)
{
	struct dsp_spos_instance * ins = chip->dsp_spos_instance;

	if (snd_BUG_ON(!ins->asynch_tx_scb))
		return -EINVAL;

	ins->spdif_status_out  &= ~DSP_SPDIF_STATUS_PLAYBACK_OPEN;

	/* restore settings */
	cs46xx_poke_via_dsp (chip,SP_SPDOUT_CSUV, ins->spdif_csuv_default);
	
	/* deallocate stuff */
	if (ins->spdif_pcm_input_scb != NULL) {
		cs46xx_dsp_remove_scb (chip,ins->spdif_pcm_input_scb);
		ins->spdif_pcm_input_scb = NULL;
	}

	cs46xx_dsp_remove_scb (chip,ins->asynch_tx_scb);
	ins->asynch_tx_scb = NULL;

	/* clear buffer to prevent any undesired noise */
	_dsp_clear_sample_buffer(chip,SPDIFO_IP_OUTPUT_BUFFER1,256);

	/* restore state */
	if ( ins->spdif_status_out & DSP_SPDIF_STATUS_OUTPUT_ENABLED ) {
		cs46xx_dsp_enable_spdif_out (chip);
	}
	
	return 0;
}
