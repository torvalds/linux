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

/*
 * 2002-07 Benny Sjostrand benny@hostmobility.com
 */

#ifdef  CONFIG_SND_CS46XX_NEW_DSP /* hack ... */
#ifndef __DSP_SPOS_H__
#define __DSP_SPOS_H__

#define DSP_MAX_SYMBOLS 1024
#define DSP_MAX_MODULES 64

#define DSP_CODE_BYTE_SIZE             0x00007000UL
#define DSP_PARAMETER_BYTE_SIZE        0x00003000UL
#define DSP_SAMPLE_BYTE_SIZE           0x00003800UL
#define DSP_PARAMETER_BYTE_OFFSET      0x00000000UL
#define DSP_SAMPLE_BYTE_OFFSET         0x00010000UL
#define DSP_CODE_BYTE_OFFSET           0x00020000UL

#define WIDE_INSTR_MASK       0x0040
#define WIDE_LADD_INSTR_MASK  0x0380

/* this instruction types
   needs to be reallocated when load
   code into DSP */
typedef enum  {
	WIDE_FOR_BEGIN_LOOP = 0x20,
	WIDE_FOR_BEGIN_LOOP2,

	WIDE_COND_GOTO_ADDR = 0x30,
	WIDE_COND_GOTO_CALL,

	WIDE_TBEQ_COND_GOTO_ADDR = 0x70,
	WIDE_TBEQ_COND_CALL_ADDR,
	WIDE_TBEQ_NCOND_GOTO_ADDR,
	WIDE_TBEQ_NCOND_CALL_ADDR,
	WIDE_TBEQ_COND_GOTO1_ADDR,
	WIDE_TBEQ_COND_CALL1_ADDR,
	WIDE_TBEQ_NCOND_GOTOI_ADDR,
	WIDE_TBEQ_NCOND_CALL1_ADDR,
} wide_opcode_t;

/* SAMPLE segment */
#define VARI_DECIMATE_BUF1       0x0000
#define WRITE_BACK_BUF1          0x0400
#define CODEC_INPUT_BUF1         0x0500
#define PCM_READER_BUF1          0x0600
#define SRC_DELAY_BUF1           0x0680
#define VARI_DECIMATE_BUF0       0x0780
#define SRC_OUTPUT_BUF1          0x07A0
#define ASYNC_IP_OUTPUT_BUFFER1  0x0A00
#define OUTPUT_SNOOP_BUFFER      0x0B00
#define SPDIFI_IP_OUTPUT_BUFFER1 0x0E00
#define SPDIFO_IP_OUTPUT_BUFFER1 0x1000
#define MIX_SAMPLE_BUF1          0x1400
#define MIX_SAMPLE_BUF2          0x2E80
#define MIX_SAMPLE_BUF3          0x2F00
#define MIX_SAMPLE_BUF4          0x2F80
#define MIX_SAMPLE_BUF5          0x3000

/* Task stack address */
#define HFG_STACK                0x066A
#define FG_STACK                 0x066E
#define BG_STACK                 0x068E

/* SCB's addresses */
#define SPOSCB_ADDR              0x070
#define BG_TREE_SCB_ADDR         0x635
#define NULL_SCB_ADDR            0x000
#define TIMINGMASTER_SCB_ADDR    0x010
#define CODECOUT_SCB_ADDR        0x020
#define PCMREADER_SCB_ADDR       0x030
#define WRITEBACK_SCB_ADDR       0x040
#define CODECIN_SCB_ADDR         0x080
#define MASTERMIX_SCB_ADDR       0x090
#define SRCTASK_SCB_ADDR         0x0A0
#define VARIDECIMATE_SCB_ADDR    0x0B0
#define PCMSERIALIN_SCB_ADDR     0x0C0
#define FG_TASK_HEADER_ADDR      0x600
#define ASYNCTX_SCB_ADDR         0x0E0
#define ASYNCRX_SCB_ADDR         0x0F0
#define SRCTASKII_SCB_ADDR       0x100
#define OUTPUTSNOOP_SCB_ADDR     0x110
#define PCMSERIALINII_SCB_ADDR   0x120
#define SPIOWRITE_SCB_ADDR       0x130
#define REAR_CODECOUT_SCB_ADDR   0x140
#define OUTPUTSNOOPII_SCB_ADDR   0x150
#define PCMSERIALIN_PCM_SCB_ADDR 0x160
#define RECORD_MIXER_SCB_ADDR    0x170
#define REAR_MIXER_SCB_ADDR      0x180
#define CLFE_MIXER_SCB_ADDR      0x190
#define CLFE_CODEC_SCB_ADDR      0x1A0

/* hyperforground SCB's*/
#define HFG_TREE_SCB             0xBA0
#define SPDIFI_SCB_INST          0xBB0
#define SPDIFO_SCB_INST          0xBC0
#define WRITE_BACK_SPB           0x0D0

/* offsets */
#define AsyncCIOFIFOPointer  0xd
#define SPDIFOFIFOPointer    0xd
#define SPDIFIFIFOPointer    0xd
#define TCBData              0xb
#define HFGFlags             0xa
#define TCBContextBlk        0x10
#define AFGTxAccumPhi        0x4
#define SCBsubListPtr        0x9
#define SCBfuncEntryPtr      0xA
#define SRCCorPerGof         0x2
#define SRCPhiIncr6Int26Frac 0xd
#define SCBVolumeCtrl        0xe

/* conf */
#define UseASER1Input 1



/*
 * The following defines are for the flags in the rsConfig01/23 registers of
 * the SP.
 */

#define RSCONFIG_MODULO_SIZE_MASK               0x0000000FL
#define RSCONFIG_MODULO_16                      0x00000001L
#define RSCONFIG_MODULO_32                      0x00000002L
#define RSCONFIG_MODULO_64                      0x00000003L
#define RSCONFIG_MODULO_128                     0x00000004L
#define RSCONFIG_MODULO_256                     0x00000005L
#define RSCONFIG_MODULO_512                     0x00000006L
#define RSCONFIG_MODULO_1024                    0x00000007L
#define RSCONFIG_MODULO_4                       0x00000008L
#define RSCONFIG_MODULO_8                       0x00000009L
#define RSCONFIG_SAMPLE_SIZE_MASK               0x000000C0L
#define RSCONFIG_SAMPLE_8MONO                   0x00000000L
#define RSCONFIG_SAMPLE_8STEREO                 0x00000040L
#define RSCONFIG_SAMPLE_16MONO                  0x00000080L
#define RSCONFIG_SAMPLE_16STEREO                0x000000C0L
#define RSCONFIG_UNDERRUN_ZERO                  0x00004000L
#define RSCONFIG_DMA_TO_HOST                    0x00008000L
#define RSCONFIG_STREAM_NUM_MASK                0x00FF0000L
#define RSCONFIG_MAX_DMA_SIZE_MASK              0x1F000000L
#define RSCONFIG_DMA_ENABLE                     0x20000000L
#define RSCONFIG_PRIORITY_MASK                  0xC0000000L
#define RSCONFIG_PRIORITY_HIGH                  0x00000000L
#define RSCONFIG_PRIORITY_MEDIUM_HIGH           0x40000000L
#define RSCONFIG_PRIORITY_MEDIUM_LOW            0x80000000L
#define RSCONFIG_PRIORITY_LOW                   0xC0000000L
#define RSCONFIG_STREAM_NUM_SHIFT               16L
#define RSCONFIG_MAX_DMA_SIZE_SHIFT             24L

/* SP constants */
#define FG_INTERVAL_TIMER_PERIOD                0x0051
#define BG_INTERVAL_TIMER_PERIOD                0x0100


/* Only SP accessible registers */
#define SP_ASER_COUNTDOWN 0x8040
#define SP_SPDOUT_FIFO    0x0108
#define SP_SPDIN_MI_FIFO  0x01E0
#define SP_SPDIN_D_FIFO   0x01F0
#define SP_SPDIN_STATUS   0x8048
#define SP_SPDIN_CONTROL  0x8049
#define SP_SPDIN_FIFOPTR  0x804A
#define SP_SPDOUT_STATUS  0x804C
#define SP_SPDOUT_CONTROL 0x804D
#define SP_SPDOUT_CSUV    0x808E

static inline u8 _wrap_all_bits (u8 val) {
	u8 wrapped;
	
	/* wrap all 8 bits */
	wrapped = 
		((val & 0x1 ) << 7) |
		((val & 0x2 ) << 5) |
		((val & 0x4 ) << 3) |
		((val & 0x8 ) << 1) |
		((val & 0x10) >> 1) |
		((val & 0x20) >> 3) |
		((val & 0x40) >> 5) |
		((val & 0x80) >> 7);

	return wrapped;

}


static inline void cs46xx_dsp_spos_update_scb (cs46xx_t * chip,dsp_scb_descriptor_t * scb) 
{
	/* update nextSCB and subListPtr in SCB */
	snd_cs46xx_poke(chip,
			(scb->address + SCBsubListPtr) << 2,
			(scb->sub_list_ptr->address << 0x10) |
			(scb->next_scb_ptr->address));	
}

static inline void cs46xx_dsp_scb_set_volume (cs46xx_t * chip,dsp_scb_descriptor_t * scb,
					      u16 left,u16 right) {
	unsigned int val = ((0xffff - left) << 16 | (0xffff - right));

	snd_cs46xx_poke(chip, (scb->address + SCBVolumeCtrl) << 2, val);
	snd_cs46xx_poke(chip, (scb->address + SCBVolumeCtrl + 1) << 2, val);
}
#endif /* __DSP_SPOS_H__ */
#endif /* CONFIG_SND_CS46XX_NEW_DSP  */
