/*
 *  The driver for the Cirrus Logic's Sound Fusion CS46XX based soundcards
 *  Copyright (c) by Jaroslav Kysela <perex@suse.cz>
 *
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

#ifndef __CS46XX_LIB_H__
#define __CS46XX_LIB_H__

/*
 *  constants
 */

#define CS46XX_BA0_SIZE		  0x1000
#define CS46XX_BA1_DATA0_SIZE 0x3000
#define CS46XX_BA1_DATA1_SIZE 0x3800
#define CS46XX_BA1_PRG_SIZE	  0x7000
#define CS46XX_BA1_REG_SIZE	  0x0100



#ifdef CONFIG_SND_CS46XX_NEW_DSP
#define CS46XX_MIN_PERIOD_SIZE 1
#define CS46XX_MAX_PERIOD_SIZE 1024*1024
#else
#define CS46XX_MIN_PERIOD_SIZE 2048
#define CS46XX_MAX_PERIOD_SIZE 2048
#endif

#define CS46XX_FRAGS 2
/* #define CS46XX_BUFFER_SIZE CS46XX_MAX_PERIOD_SIZE * CS46XX_FRAGS */

#define SCB_NO_PARENT             0
#define SCB_ON_PARENT_NEXT_SCB    1
#define SCB_ON_PARENT_SUBLIST_SCB 2

/* 3*1024 parameter, 3.5*1024 sample, 2*3.5*1024 code */
#define BA1_DWORD_SIZE		(13 * 1024 + 512)
#define BA1_MEMORY_COUNT	3

/*
 *  common I/O routines
 */

static inline void snd_cs46xx_poke(cs46xx_t *chip, unsigned long reg, unsigned int val)
{
	unsigned int bank = reg >> 16;
	unsigned int offset = reg & 0xffff;

	/*if (bank == 0) printk("snd_cs46xx_poke: %04X - %08X\n",reg >> 2,val); */
	writel(val, chip->region.idx[bank+1].remap_addr + offset);
}

static inline unsigned int snd_cs46xx_peek(cs46xx_t *chip, unsigned long reg)
{
	unsigned int bank = reg >> 16;
	unsigned int offset = reg & 0xffff;
	return readl(chip->region.idx[bank+1].remap_addr + offset);
}

static inline void snd_cs46xx_pokeBA0(cs46xx_t *chip, unsigned long offset, unsigned int val)
{
	writel(val, chip->region.name.ba0.remap_addr + offset);
}

static inline unsigned int snd_cs46xx_peekBA0(cs46xx_t *chip, unsigned long offset)
{
	return readl(chip->region.name.ba0.remap_addr + offset);
}

dsp_spos_instance_t *  cs46xx_dsp_spos_create (cs46xx_t * chip);
void                   cs46xx_dsp_spos_destroy (cs46xx_t * chip);
int                    cs46xx_dsp_load_module (cs46xx_t * chip,dsp_module_desc_t * module);
symbol_entry_t *       cs46xx_dsp_lookup_symbol (cs46xx_t * chip,char * symbol_name,int symbol_type);
int                    cs46xx_dsp_proc_init (snd_card_t * card, cs46xx_t *chip);
int                    cs46xx_dsp_proc_done (cs46xx_t *chip);
int                    cs46xx_dsp_scb_and_task_init (cs46xx_t *chip);
int                    snd_cs46xx_download (cs46xx_t *chip,u32 *src,unsigned long offset,
                                            unsigned long len);
int                    snd_cs46xx_clear_BA1(cs46xx_t *chip,unsigned long offset,unsigned long len);
int                    cs46xx_dsp_enable_spdif_out (cs46xx_t *chip);
int                    cs46xx_dsp_enable_spdif_hw (cs46xx_t *chip);
int                    cs46xx_dsp_disable_spdif_out (cs46xx_t *chip);
int                    cs46xx_dsp_enable_spdif_in (cs46xx_t *chip);
int                    cs46xx_dsp_disable_spdif_in (cs46xx_t *chip);
int                    cs46xx_dsp_enable_pcm_capture (cs46xx_t *chip);
int                    cs46xx_dsp_disable_pcm_capture (cs46xx_t *chip);
int                    cs46xx_dsp_enable_adc_capture (cs46xx_t *chip);
int                    cs46xx_dsp_disable_adc_capture (cs46xx_t *chip);
int                    cs46xx_poke_via_dsp (cs46xx_t *chip,u32 address,u32 data);
dsp_scb_descriptor_t * cs46xx_dsp_create_scb (cs46xx_t *chip,char * name, u32 * scb_data,u32 dest);
void                   cs46xx_dsp_proc_free_scb_desc (dsp_scb_descriptor_t * scb);
void                   cs46xx_dsp_proc_register_scb_desc (cs46xx_t *chip,dsp_scb_descriptor_t * scb);
dsp_scb_descriptor_t * cs46xx_dsp_create_timing_master_scb (cs46xx_t *chip);
dsp_scb_descriptor_t * cs46xx_dsp_create_codec_out_scb(cs46xx_t * chip,char * codec_name,
                                                       u16 channel_disp,u16 fifo_addr,
                                                       u16 child_scb_addr,
                                                       u32 dest,
                                                       dsp_scb_descriptor_t * parent_scb,
                                                       int scb_child_type);
dsp_scb_descriptor_t * cs46xx_dsp_create_codec_in_scb(cs46xx_t * chip,char * codec_name,
                                                      u16 channel_disp,u16 fifo_addr,
                                                      u16 sample_buffer_addr,
                                                      u32 dest,
                                                      dsp_scb_descriptor_t * parent_scb,
                                                      int scb_child_type);
void                   cs46xx_dsp_remove_scb (cs46xx_t *chip,dsp_scb_descriptor_t * scb);
dsp_scb_descriptor_t *  cs46xx_dsp_create_codec_in_scb(cs46xx_t * chip,char * codec_name,
                                                       u16 channel_disp,u16 fifo_addr,
                                                       u16 sample_buffer_addr,
                                                       u32 dest,dsp_scb_descriptor_t * parent_scb,
                                                       int scb_child_type);
dsp_scb_descriptor_t *  cs46xx_dsp_create_src_task_scb(cs46xx_t * chip,char * scb_name,
						       int sample_rate,
                                                       u16 src_buffer_addr,
                                                       u16 src_delay_buffer_addr,u32 dest,
                                                       dsp_scb_descriptor_t * parent_scb,
                                                       int scb_child_type,
						       int pass_through);
dsp_scb_descriptor_t *  cs46xx_dsp_create_mix_only_scb(cs46xx_t * chip,char * scb_name,
                                                       u16 mix_buffer_addr,u32 dest,
                                                       dsp_scb_descriptor_t * parent_scb,
                                                       int scb_child_type);

dsp_scb_descriptor_t *  cs46xx_dsp_create_vari_decimate_scb(cs46xx_t * chip,char * scb_name,
                                                            u16 vari_buffer_addr0,
                                                            u16 vari_buffer_addr1,
                                                            u32 dest,
                                                            dsp_scb_descriptor_t * parent_scb,
                                                            int scb_child_type);
dsp_scb_descriptor_t * cs46xx_dsp_create_asynch_fg_rx_scb(cs46xx_t * chip,char * scb_name,u32 dest,
                                                          u16 hfg_scb_address,
                                                          u16 asynch_buffer_address,
                                                          dsp_scb_descriptor_t * parent_scb,
                                                          int scb_child_type);
dsp_scb_descriptor_t *  cs46xx_dsp_create_spio_write_scb(cs46xx_t * chip,char * scb_name,u32 dest,
                                                         dsp_scb_descriptor_t * parent_scb,
                                                         int scb_child_type);
dsp_scb_descriptor_t *  cs46xx_dsp_create_mix_to_ostream_scb(cs46xx_t * chip,char * scb_name,
                                                             u16 mix_buffer_addr,u16 writeback_spb,u32 dest,
                                                             dsp_scb_descriptor_t * parent_scb,
                                                             int scb_child_type);
dsp_scb_descriptor_t *  cs46xx_dsp_create_magic_snoop_scb(cs46xx_t * chip,char * scb_name,u32 dest,
                                                          u16 snoop_buffer_address,
                                                          dsp_scb_descriptor_t * snoop_scb,
                                                          dsp_scb_descriptor_t * parent_scb,
                                                          int scb_child_type);
pcm_channel_descriptor_t * cs46xx_dsp_create_pcm_channel (cs46xx_t * chip,u32 sample_rate, void * private_data, u32 hw_dma_addr,
                                                          int pcm_channel_id);
void                       cs46xx_dsp_destroy_pcm_channel (cs46xx_t * chip,
                                                           pcm_channel_descriptor_t * pcm_channel);
int                        cs46xx_dsp_pcm_unlink (cs46xx_t * chip,pcm_channel_descriptor_t * pcm_channel);
int                        cs46xx_dsp_pcm_link (cs46xx_t * chip,pcm_channel_descriptor_t * pcm_channel);
dsp_scb_descriptor_t *     cs46xx_add_record_source (cs46xx_t *chip,dsp_scb_descriptor_t * source,
                                                     u16 addr,char * scb_name);
int                        cs46xx_src_unlink(cs46xx_t *chip,dsp_scb_descriptor_t * src);
int                        cs46xx_src_link(cs46xx_t *chip,dsp_scb_descriptor_t * src);
int                        cs46xx_iec958_pre_open (cs46xx_t *chip);
int                        cs46xx_iec958_post_close (cs46xx_t *chip);
int                        cs46xx_dsp_pcm_channel_set_period (cs46xx_t * chip,
							       pcm_channel_descriptor_t * pcm_channel,
							       int period_size);
int                        cs46xx_dsp_pcm_ostream_set_period (cs46xx_t * chip,
							      int period_size);
int                        cs46xx_dsp_set_dac_volume (cs46xx_t * chip,u16 left,u16 right);
int                        cs46xx_dsp_set_iec958_volume (cs46xx_t * chip,u16 left,u16 right);
#endif /* __CS46XX_LIB_H__ */
