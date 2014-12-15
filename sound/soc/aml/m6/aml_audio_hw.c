#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/errno.h>
#include <linux/string.h>
#include <mach/am_regs.h>
#include <linux/clk.h>
#include <linux/module.h>

#include "aml_audio_hw.h"

#ifndef MREG_AIU_958_chstat0
#define AIU_958_chstat0	AIU_958_CHSTAT_L0
#endif

#ifndef MREG_AIU_958_chstat1
#define AIU_958_chstat1	AIU_958_CHSTAT_L1
#endif


unsigned ENABLE_IEC958 = 1;
unsigned IEC958_MODE   = AIU_958_MODE_PCM16;
unsigned I2S_MODE      = AIU_I2S_MODE_PCM16;


int  audio_in_buf_ready = 0;
int audio_out_buf_ready = 0;

unsigned int IEC958_bpf = 0x7dd;
unsigned int IEC958_brst = 0xc;
unsigned int IEC958_length = 0x7dd*8;
unsigned int IEC958_padsize = 0x8000;
unsigned int IEC958_mode = 1;
unsigned int IEC958_syncword1 = 0x7ffe;
unsigned int IEC958_syncword2 = 0x8001;
unsigned int IEC958_syncword3 = 0;
unsigned int IEC958_syncword1_mask = 0;
unsigned int IEC958_syncword2_mask = 0;
unsigned int IEC958_syncword3_mask = 0xffff;
unsigned int IEC958_chstat0_l = 0x1902 ;
unsigned int IEC958_chstat0_r = 0x1902 ;
unsigned int IEC958_chstat1_l = 0x200;
unsigned int IEC958_chstat1_r = 0x200;
unsigned int IEC958_mode_raw = 0;
/*
 0 --  other formats except(DD,DD+,DTS)
 1 --  DTS
 2 --  DD
 3 -- DTS with 958 PCM RAW package mode
 4 -- DD+
*/
unsigned int IEC958_mode_codec;
/*
bit 0:soc in slave mode for adc;
bit 1:audio in data source from spdif in;
bit 2:adc & spdif in work at the same time;
*/
unsigned audioin_mode = I2SIN_MASTER_MODE;

EXPORT_SYMBOL(IEC958_bpf);
EXPORT_SYMBOL(IEC958_brst);
EXPORT_SYMBOL(IEC958_length);
EXPORT_SYMBOL(IEC958_padsize);
EXPORT_SYMBOL(IEC958_mode);
EXPORT_SYMBOL(IEC958_syncword1);
EXPORT_SYMBOL(IEC958_syncword2);
EXPORT_SYMBOL(IEC958_syncword3);
EXPORT_SYMBOL(IEC958_syncword1_mask);
EXPORT_SYMBOL(IEC958_syncword2_mask);
EXPORT_SYMBOL(IEC958_syncword3_mask);
EXPORT_SYMBOL(IEC958_chstat0_l);
EXPORT_SYMBOL(IEC958_chstat0_r);
EXPORT_SYMBOL(IEC958_chstat1_l);
EXPORT_SYMBOL(IEC958_chstat1_r);
EXPORT_SYMBOL(IEC958_mode_raw);
EXPORT_SYMBOL(IEC958_mode_codec);

// Bit 3:  mute constant
//         0 => 'h0000000
//         1 => 'h800000
unsigned int dac_mute_const = 0x800000;

/*
                                fIn * (M)
            Fout   =  -----------------------------
                      		(N) * (OD+1) * (XD)
*/
#if MESON_CPU_TYPE >= MESON_CPU_TYPE_MESON6
int audio_clock_config_table[][12][2]=
{
	/*{HIU Reg , XD - 1)
	   //7.875k, 8K, 11.025k, 12k, 16k, 22.05k, 24k, 32k, 44.1k, 48k, 96k, 192k
	*/
	{
	//256
#if OVERCLOCK == 0
		{0x0004f880, (50-1)},  // 32
#if MESON_CPU_TYPE >= MESON_CPU_TYPE_MESON8
		{0x0005e965, (40-1)}, //44.1
		{0x0004c9a0,	(50-1)},	//48K
#else
		{0x0004cdf3, (42-1)},  // 44.1
		{0x0007c4e6, (23-1)},  // 48
#endif
		{0x0006d0a4, (13-1)},  // 96
		{0x0004e15a, (9 -1)},   // 192
		{0x0007f400, (125-1)}, // 8k
		{0x0006c6f6, (116-1)}, // 11.025
		{0x0007e47f, (86-1)},  // 12
		{0x0004f880, (100-1)}, // 16
		{0x0004c4a4, (87-1)},  // 22.05
		{0x0007e47f, (43-1)},  // 24
		{0x0007f3f0, (127-1)}, // 7875
#else
	//512FS
		{0x0004f880, (25-1)},  // 32
		{0x0004cdf3, (21-1)},  // 44.1
		{0x0006d0a4, (13-1)},  // 48
		{0x0004e15a, (9 -1)},  // 96
		{0x0006f207, (3 -1)},   // 192
		{0x0004f880, (100-1)}, // 8k
		{0x0004c4a4, (87-1)}, // 11.025
		{0x0007e47f, (43-1)},  // 12
		{0x0004f880, (50-1)}, // 16
		{0x0004cdf3, (42-1)},  // 22.05
		{0x0007c4e6, (23-1)},  // 24
		{0x0006e1b6, (76-1)}, // 7875
#endif
	},
	{
	//384
		{0x0007c4e6, (23-1)},  // 32
		{0x0004c4a4, (29-1)},  // 44.1
		{0x0004cb18, (26-1)},  // 48
		{0x0004cb18, (13-1)},  // 96
		{0x0004e15a, (6 -1)},   // 192
		{0x0007e47f, (86-1)},  // 8k
		{0x0007efa5, (61-1)},  // 11.025
		{0x0006de98, (67-1)},  // 12
		{0x0007e47f, (43-1)},  // 16
		{0x0004c4a4, (58-1)},  // 22.05
		{0x0004c60e, (53-1)},  // 24
		{0x0007fdfa, (83-1)},  // 7875
	}
};
#else
int audio_clock_config_table[][11][2]=
{
  // 128*Fs
  //
	/*{M, N, OD, XD-1*/
	{
	//24M
        {(64<<0) | (3<<9) | (0<<14) , (125-1)}, // 32K, 4.096M
#if OVERCLOCK==0
        {(147<<0) | (5<<9) | (0<<14) , (125-1)}, // 44.1K, 5.6448M
        {(32<<0) | (1<<9) | (0<<14) , (125-1)}, // 48K, 6.144M
#else
        {(143<<0) | (8<<9) | (0<<14) , (19-1)}, // 44.1K, 5.6448M*4=22.5792M
        {(128<<0) | (5<<9) | (0<<14) , (25-1)}, // 48K, 6.144M*4=24.576M
#endif
        {(128<<0) | (5<<9) | (1<<14) , (25-1)}, // 96K, 12.288M
        {(128<<0) | (5<<9) | (0<<14) , (25-1)}, //192K, 24.576M
        {(64<<0) | (3<<9) | (1<<14) , (250-1)}, // 8K, 1.024M
        {(147<<0) | (5<<9) | (1<<14) , (250-1)}, //11.025K,1.4112M
        {(32<<0) | (1<<9) | (1<<14) , (250-1)}, // 12K, 1.536M
        {(64<<0) | (3<<9) | (1<<14) , (125-1)}, // 16K, 2.048M
        {(147<<0) | (5<<9) | (1<<14) , (125-1)}, //22.050K, 2.8224M
        {(32<<0) | (1<<9) | (1<<14) , (125-1)}, // 24K, 3.072M
	},
	{
	//25M
        {(29<<0) | (1<<9) | (0<<14) , (177-1)}, // 32K, 4.096M
#if OVERCLOCK==0
        {(21<<0) | (1<<9) | (0<<14) , (93-1)}, // 44.1K, 5.6448M
        {(29<<0) | (1<<9) | (1<<14) , (59-1)}, // 48K, 6.144M
#else
        {(28<<0) | (1<<9) | (0<<14) , (31-1)}, // 44.1K, 5.6448M*4=22.5792M
        {(173<<0) | (8<<9) | (1<<14) , (11-1)}, // 48K, 6.144M*4=24.576M
#endif
        {(29<<0) | (1<<9) | (0<<14) , (59-1)}, // 96K, 12.288M
        {(173<<0) | (8<<9) | (1<<14) , (11-1)}, //192K, 24.576M
        {(58<<0) | (3<<9) | (1<<14) , (236-1)}, // 8K, 1.024M
        {(162<<0) | (7<<9) | (1<<14) , (205-1)}, //11.025K,1.4112M
        {(29<<0) | (1<<9) | (1<<14) , (236-1)}, // 12K, 1.536M
        {(29<<0) | (1<<9) | (1<<14) , (177-1)}, // 16K, 2.048M
        {(162<<0) | (7<<9) | (0<<14) , (205-1)}, //22.050K, 2.8224M
        {(29<<0) | (1<<9) | (1<<14) , (118-1)}, // 24K, 3.072M
	}
};
#endif


void audio_set_aiubuf(u32 addr, u32 size, unsigned int channel)
{
	printk("====== %s ======\n",__FUNCTION__);
    WRITE_MPEG_REG(AIU_MEM_I2S_START_PTR, addr & 0xffffffc0);
    WRITE_MPEG_REG(AIU_MEM_I2S_RD_PTR, addr & 0xffffffc0);
    if(channel == 8)
		WRITE_MPEG_REG(AIU_MEM_I2S_END_PTR, (addr & 0xffffffc0) + (size & 0xffffffc0) - 256);
	else
    WRITE_MPEG_REG(AIU_MEM_I2S_END_PTR, (addr & 0xffffffc0) + (size & 0xffffffc0) - 64);   //this is for 16bit 2 channel

    WRITE_MPEG_REG(AIU_I2S_MISC,		0x0004);	// Hold I2S
	WRITE_MPEG_REG(AIU_I2S_MUTE_SWAP,	0x0000);	// No mute, no swap
	// As the default amclk is 24.576MHz, set i2s and iec958 divisor appropriately so as not to exceed the maximum sample rate.
	WRITE_MPEG_REG(AIU_I2S_MISC,		0x0010 );	// Release hold and force audio data to left or right

	if(channel == 8){
		printk(" %s channel == 8\n",__FUNCTION__);
	WRITE_MPEG_REG(AIU_MEM_I2S_MASKS,		(24 << 16) |	// [31:16] IRQ block.
								(0xff << 8) |	// [15: 8] chan_mem_mask. Each bit indicates which channels exist in memory
								(0xff << 0));	// [ 7: 0] chan_rd_mask.  Each bit indicates which channels are READ from memory
		}
	else
	WRITE_MPEG_REG(AIU_MEM_I2S_MASKS,		(24 << 16) |	// [31:16] IRQ block.
								(0x3 << 8) |	// [15: 8] chan_mem_mask. Each bit indicates which channels exist in memory
								(0x3 << 0));	// [ 7: 0] chan_rd_mask.  Each bit indicates which channels are READ from memory

    // 16 bit PCM mode
    //  WRITE_MPEG_REG_BITS(AIU_MEM_I2S_CONTROL, 1, 6, 1);
	// Set init high then low to initilize the I2S memory logic
	WRITE_MPEG_REG_BITS(AIU_MEM_I2S_CONTROL, 1, 0, 1 );
	WRITE_MPEG_REG_BITS(AIU_MEM_I2S_CONTROL, 0, 0, 1 );

	WRITE_MPEG_REG(AIU_MEM_I2S_BUF_CNTL, 1 | (0 << 1));
	WRITE_MPEG_REG(AIU_MEM_I2S_BUF_CNTL, 0 | (0 << 1));

    audio_out_buf_ready = 1;
}

void audio_set_958outbuf(u32 addr, u32 size,int flag)
{
    if (ENABLE_IEC958) {
        WRITE_MPEG_REG(AIU_MEM_IEC958_START_PTR, addr & 0xffffffc0);
	  	if(READ_MPEG_REG(AIU_MEM_IEC958_START_PTR) == READ_MPEG_REG(AIU_MEM_I2S_START_PTR)){
			WRITE_MPEG_REG(AIU_MEM_IEC958_RD_PTR, READ_MPEG_REG(AIU_MEM_I2S_RD_PTR));
		}
		else
        WRITE_MPEG_REG(AIU_MEM_IEC958_RD_PTR, addr & 0xffffffc0);
        if(flag == 0){
          WRITE_MPEG_REG(AIU_MEM_IEC958_END_PTR, (addr & 0xffffffc0) + (size & 0xffffffc0) - 64);    // this is for 16bit 2 channel
        }else{
          WRITE_MPEG_REG(AIU_MEM_IEC958_END_PTR, (addr & 0xffffffc0) + (size & 0xffffffc0) - 1);    // this is for RAW mode
        }

        WRITE_MPEG_REG_BITS(AIU_MEM_IEC958_CONTROL, 1, 0, 1);
        WRITE_MPEG_REG_BITS(AIU_MEM_IEC958_CONTROL, 0, 0, 1);

        WRITE_MPEG_REG(AIU_MEM_IEC958_BUF_CNTL, 1 | (0 << 1));
        WRITE_MPEG_REG(AIU_MEM_IEC958_BUF_CNTL, 0 | (0 << 1));
    }
}
/*
i2s mode 0: master 1: slave
*/
static void i2sin_fifo0_set_buf(u32 addr, u32 size,u32 i2s_mode)
{
	unsigned char  mode = 0;
	if(i2s_mode &I2SIN_SLAVE_MODE)
		mode = 1;
	WRITE_MPEG_REG(AUDIN_FIFO0_START, addr & 0xffffffc0);
	WRITE_MPEG_REG(AUDIN_FIFO0_PTR, (addr&0xffffffc0));
	WRITE_MPEG_REG(AUDIN_FIFO0_END, (addr&0xffffffc0) + (size&0xffffffc0)-8);

	WRITE_MPEG_REG(AUDIN_FIFO0_CTRL, (1<<AUDIN_FIFO0_EN)	// FIFO0_EN
    								|(1<<AUDIN_FIFO0_LOAD)	// load start address./* AUDIN_FIFO0_LOAD */
								|(1<<AUDIN_FIFO0_DIN_SEL)	// DIN from i2sin./* AUDIN_FIFO0_DIN_SEL */
	    							//|(1<<6)	// 32 bits data in./*AUDIN_FIFO0_D32b */
									//|(0<<7)	// put the 24bits data to  low 24 bits./* AUDIN_FIFO0_h24b */16bit
								|(4<<AUDIN_FIFO0_ENDIAN)	// /*AUDIN_FIFO0_ENDIAN */
								|(2<<AUDIN_FIFO0_CHAN)//2 channel./* AUDIN_FIFO0_CHAN*/
		    						|(0<<16)	//to DDR
                                                       |(1<<AUDIN_FIFO0_UG)    // Urgent request.  DDR SDRAM urgent request enable.
                                                       |(0<<17)    // Overflow Interrupt mask
                                                       |(0<<18)    // Audio in INT
			                                	//|(1<<19)	//hold 0 enable
								|(0<<AUDIN_FIFO0_UG)	// hold0 to aififo
				  );

    WRITE_MPEG_REG(AUDIN_FIFO0_CTRL1,    0 << 4                       // fifo0_dest_sel
                                       | 2 << 2                       // fifo0_din_byte_num
                                       | 0 << 0);                      // fifo0_din_pos


	WRITE_MPEG_REG(AUDIN_I2SIN_CTRL, //(0<<I2SIN_SIZE)			///*bit8*/  16bit
									 (3<<I2SIN_SIZE)
									|(1<<I2SIN_CHAN_EN)		/*bit10~13*/ //2 channel
#if MESON_CPU_TYPE >= MESON_CPU_TYPE_MESON6TV
									|(0<<I2SIN_POS_SYNC)
#else
									|(1<<I2SIN_POS_SYNC)
#endif
									|(1<<I2SIN_LRCLK_SKEW)
                                    				|(1<<I2SIN_LRCLK_INVT)
									|(!mode<<I2SIN_CLK_SEL)
									|(!mode<<I2SIN_LRCLK_SEL)
				    				|(!mode<<I2SIN_DIR)
				  );

}
static void spdifin_fifo1_set_buf(u32 addr, u32 size)
{
	WRITE_MPEG_REG(AUDIN_SPDIF_MODE, READ_MPEG_REG(AUDIN_SPDIF_MODE)&0x7fffffff);
	WRITE_MPEG_REG(AUDIN_FIFO0_START, addr & 0xffffffc0);
	WRITE_MPEG_REG(AUDIN_FIFO0_PTR, (addr&0xffffffc0));
	WRITE_MPEG_REG(AUDIN_FIFO0_END, (addr&0xffffffc0) + (size&0xffffffc0)-8);
	WRITE_MPEG_REG(AUDIN_FIFO0_CTRL, (1<<AUDIN_FIFO0_EN)	// FIFO0_EN
    								|(1<<AUDIN_FIFO0_LOAD)	// load start address./* AUDIN_FIFO0_LOAD */
								|(0<<AUDIN_FIFO0_DIN_SEL)	// DIN from i2sin./* AUDIN_FIFO0_DIN_SEL */
	    							//|(1<<6)	// 32 bits data in./*AUDIN_FIFO0_D32b */
									//|(0<<7)	// put the 24bits data to  low 24 bits./* AUDIN_FIFO0_h24b */16bit
								|(4<<AUDIN_FIFO0_ENDIAN)	// /*AUDIN_FIFO0_ENDIAN */
								|(2<<AUDIN_FIFO0_CHAN)//2 channel./* AUDIN_FIFO0_CHAN*/
		    						|(0<<16)	//to DDR
                                                       |(1<<AUDIN_FIFO0_UG)    // Urgent request.  DDR SDRAM urgent request enable.
                                                       |(0<<17)    // Overflow Interrupt mask
                                                       |(0<<18)    // Audio in INT
			                                	//|(1<<19)	//hold 0 enable
								|(0<<AUDIN_FIFO0_UG)	// hold0 to aififo
				  );
	WRITE_MPEG_REG(AUDIN_FIFO0_CTRL1,0xc);
}
void audio_in_i2s_set_buf(u32 addr, u32 size,u32 i2s_mode)
{
	if(i2s_mode&SPDIFIN_MODE){ //spdif in ,use fifo1
		printk("spdifin_fifo1_set_buf \n");
		spdifin_fifo1_set_buf(addr,size);
	}
	else{
		printk("i2sin_fifo0_set_buf \n");
		i2sin_fifo0_set_buf(addr,size,i2s_mode);
	}
       audio_in_buf_ready = 1;

}
void audio_in_spdif_set_buf(u32 addr, u32 size)
{
}
//extern void audio_in_enabled(int flag);

void audio_in_i2s_enable(int flag)
{
  	int rd = 0, start=0;
	if(flag){
          /* reset only when start i2s input */
reset_again:
	     WRITE_MPEG_REG_BITS(AUDIN_FIFO0_CTRL, 1, 1, 1); // reset FIFO 0
            WRITE_MPEG_REG(AUDIN_FIFO0_PTR, 0);
            rd = READ_MPEG_REG(AUDIN_FIFO0_PTR);
            start = READ_MPEG_REG(AUDIN_FIFO0_START);
            if(rd != start){
              printk("error %08x, %08x !!!!!!!!!!!!!!!!!!!!!!!!\n", rd, start);
              goto reset_again;
            }
		if(audioin_mode == 	SPDIFIN_MODE)
			WRITE_MPEG_REG(AUDIN_SPDIF_MODE, READ_MPEG_REG(AUDIN_SPDIF_MODE)| (1<<31));
		else
			WRITE_MPEG_REG_BITS(AUDIN_I2SIN_CTRL, 1, I2SIN_EN, 1);

	}else{
		if(audioin_mode == 	SPDIFIN_MODE)
			WRITE_MPEG_REG(AUDIN_SPDIF_MODE, READ_MPEG_REG(AUDIN_SPDIF_MODE)& ~(1<<31));
		else
			WRITE_MPEG_REG_BITS(AUDIN_I2SIN_CTRL, 0, I2SIN_EN, 1);
	}
}

int if_audio_in_i2s_enable()
{
	return READ_MPEG_REG_BITS(AUDIN_I2SIN_CTRL, I2SIN_EN, 1);
}

void audio_in_spdif_enable(int flag)
{
//  int rd = 0, start=0;

	if(flag){
#if 0
reset_again:
	     WRITE_MPEG_REG_BITS(AUDIN_FIFO1_CTRL, 1, 1, 1); // reset FIFO 0
            WRITE_MPEG_REG(AUDIN_FIFO1_PTR, 0);
            rd = READ_MPEG_REG(AUDIN_FIFO1_PTR);
            start = READ_MPEG_REG(AUDIN_FIFO1_START);
            if(rd != start){
              printk("error %08x, %08x !!!!!!!!!!!!!!!!!!!!!!!!\n", rd, start);
              goto reset_again;
            }
#endif
		WRITE_MPEG_REG(AUDIN_SPDIF_MODE, READ_MPEG_REG(AUDIN_SPDIF_MODE)| (1<<31));
	}else{
		WRITE_MPEG_REG(AUDIN_SPDIF_MODE, READ_MPEG_REG(AUDIN_SPDIF_MODE)& ~(1<<31));
	}
}
unsigned int audio_in_i2s_rd_ptr(void)
{
	unsigned int val;
	val = READ_MPEG_REG(AUDIN_FIFO0_RDPTR);
	printk("audio in i2s rd ptr: %x\n", val);
	return val;
}
unsigned int audio_in_i2s_wr_ptr(void)
{
	unsigned int val;
      WRITE_MPEG_REG(AUDIN_FIFO0_PTR, 1);
	val = READ_MPEG_REG(AUDIN_FIFO0_PTR);
	return (val)&(~0x3F);
	//return val&(~0x7);
}
void audio_in_i2s_set_wrptr(unsigned int val)
{
	WRITE_MPEG_REG(AUDIN_FIFO0_RDPTR, val);
}

void audio_set_i2s_mode(u32 mode)
{
    const unsigned short mask[4] = {
        0x303,                  /* 2x16 */
        0x303,                  /* 2x24 */
        0x303,                 /* 8x24 */
        0x303,                  /* 2x32 */
    };

    if (mode < sizeof(mask)/ sizeof(unsigned short)) {
       /* four two channels stream */
        WRITE_MPEG_REG(AIU_I2S_SOURCE_DESC, 1);

        if (mode == AIU_I2S_MODE_PCM16) {
            WRITE_MPEG_REG_BITS(AIU_MEM_I2S_CONTROL, 1, 6, 1);
            WRITE_MPEG_REG_BITS(AIU_I2S_SOURCE_DESC, 0, 5, 1);
        } else if(mode == AIU_I2S_MODE_PCM32){
            WRITE_MPEG_REG_BITS(AIU_MEM_I2S_CONTROL, 0, 6, 1);
            WRITE_MPEG_REG_BITS(AIU_I2S_SOURCE_DESC, 1, 5, 1);
        }else if(mode == AIU_I2S_MODE_PCM24){
            WRITE_MPEG_REG_BITS(AIU_MEM_I2S_CONTROL, 0, 6, 1);
            WRITE_MPEG_REG_BITS(AIU_I2S_SOURCE_DESC, 1, 5, 1);
        }

        WRITE_MPEG_REG_BITS(AIU_MEM_I2S_MASKS, mask[mode], 0, 16);

        //WRITE_MPEG_REG_BITS(AIU_MEM_I2S_CONTROL, 1, 0, 1);
        //WRITE_MPEG_REG_BITS(AIU_MEM_I2S_CONTROL, 0, 0, 1);

        if (ENABLE_IEC958) {
            WRITE_MPEG_REG_BITS(AIU_MEM_IEC958_MASKS, mask[mode], 0,
                                16);
            //WRITE_MPEG_REG_BITS(AIU_MEM_IEC958_CONTROL, 1, 0, 1);
            //WRITE_MPEG_REG_BITS(AIU_MEM_IEC958_CONTROL, 0, 0, 1);
        }
    }
}

/**
 *  if normal clock, i2s clock is twice of 958 clock, so the divisor for i2s is 8, but 4 for 958
 *  if over clock, the devisor for i2s is 8, but for 958 should be 1, because 958 should be 4 times speed according to i2s
 *  This is dolby digital plus's spec
 * */

void audio_util_set_dac_format(unsigned format)
{
  	WRITE_MPEG_REG(AIU_CLK_CTRL,		 (0 << 12) | // 958 divisor more, if true, divided by 2, 4, 6, 8.
							(1 <<  8) | // alrclk skew: 1=alrclk transitions on the cycle before msb is sent
							(1 <<  6) | // invert aoclk
							(1 <<  7) | // invert lrclk
#if OVERCLOCK == 1
							(3 <<  4) | // 958 divisor: 0=no div; 1=div by 2; 2=div by 3; 3=div by 4.
							(3 <<  2) | // i2s divisor: 0=no div; 1=div by 2; 2=div by 4; 3=div by 8.
#else
							(1 <<  4) | // 958 divisor: 0=no div; 1=div by 2; 2=div by 3; 3=div by 4.
							(2 <<  2) | // i2s divisor: 0=no div; 1=div by 2; 2=div by 4; 3=div by 8.
#endif
							(1 <<  1) | // enable 958 clock
							(1 <<  0)); // enable I2S clock
    if (format == AUDIO_ALGOUT_DAC_FORMAT_DSP) {
        WRITE_MPEG_REG_BITS(AIU_CLK_CTRL, 1, 8, 2);
    } else if (format == AUDIO_ALGOUT_DAC_FORMAT_LEFT_JUSTIFY) {
        WRITE_MPEG_REG_BITS(AIU_CLK_CTRL, 0, 8, 2);
    }
 	if(dac_mute_const == 0x800000)
    	WRITE_MPEG_REG(AIU_I2S_DAC_CFG, 	0x000f);	// Payload 24-bit, Msb first, alrclk = aoclk/64.mute const 0x800000
    else
    	WRITE_MPEG_REG(AIU_I2S_DAC_CFG, 	0x0007);	// Payload 24-bit, Msb first, alrclk = aoclk/64
	WRITE_MPEG_REG(AIU_I2S_SOURCE_DESC, 0x0001);	// four 2-channel
}

// iec958 and i2s clock are separated after M6TV.
void audio_util_set_dac_958_format(unsigned format)
{
	WRITE_MPEG_REG_BITS(AIU_CLK_CTRL,0,12,1);// 958 divisor more, if true, divided by 2, 4, 6, 8
#if IEC958_OVERCLOCK == 1
	WRITE_MPEG_REG_BITS(AIU_CLK_CTRL,3,4,2);// 958 divisor: 0=no div; 1=div by 2; 2=div by 3; 3=div by 4.
#else
	WRITE_MPEG_REG_BITS(AIU_CLK_CTRL,1,4,2);// 958 divisor: 0=no div; 1=div by 2; 2=div by 3; 3=div by 4.
#endif
	WRITE_MPEG_REG_BITS(AIU_CLK_CTRL,1,1,1);// enable 958 clock
}

void audio_util_set_dac_i2s_format(unsigned format)
{
	WRITE_MPEG_REG_BITS(AIU_CLK_CTRL,1,6,1);//invert aoclk
	WRITE_MPEG_REG_BITS(AIU_CLK_CTRL,1,7,1);//invert lrclk
	WRITE_MPEG_REG_BITS(AIU_CLK_CTRL,1,8,2);// alrclk skew: 1=alrclk transitions on the cycle before msb is sent
#if OVERCLOCK == 1
	WRITE_MPEG_REG_BITS(AIU_CLK_CTRL,3,2,2);// i2s divisor: 0=no div; 1=div by 2; 2=div by 4; 3=div by 8.
#else
	WRITE_MPEG_REG_BITS(AIU_CLK_CTRL,2,2,2); // i2s divisor: 0=no div; 1=div by 2; 2=div by 4; 3=div by 8.
#endif
	WRITE_MPEG_REG_BITS(AIU_CLK_CTRL,1,0,1);// enable I2S clock

    if (format == AUDIO_ALGOUT_DAC_FORMAT_DSP) {
        WRITE_MPEG_REG_BITS(AIU_CLK_CTRL, 1, 8, 2);
    } else if (format == AUDIO_ALGOUT_DAC_FORMAT_LEFT_JUSTIFY) {
        WRITE_MPEG_REG_BITS(AIU_CLK_CTRL, 0, 8, 2);
    }
 	if(dac_mute_const == 0x800000)
    	WRITE_MPEG_REG(AIU_I2S_DAC_CFG, 	0x000f);	// Payload 24-bit, Msb first, alrclk = aoclk/64.mute const 0x800000
    else
    	WRITE_MPEG_REG(AIU_I2S_DAC_CFG, 	0x0007);	// Payload 24-bit, Msb first, alrclk = aoclk/64
	WRITE_MPEG_REG(AIU_I2S_SOURCE_DESC, 0x0001);	// four 2-channel
}

extern unsigned int get_ddr_pll_clk(void);

void audio_set_clk(unsigned freq, unsigned fs_config)
{
    int i;
    int xtal = 0;

    int (*audio_clock_config)[2];

   // if (fs_config == AUDIO_CLK_256FS) {
   if(1){
		int index=0;
		switch(freq)
		{
			case AUDIO_CLK_FREQ_192:
				index=4;
				break;
			case AUDIO_CLK_FREQ_96:
				index=3;
				break;
			case AUDIO_CLK_FREQ_48:
				index=2;
				break;
			case AUDIO_CLK_FREQ_441:
				index=1;
				break;
			case AUDIO_CLK_FREQ_32:
				index=0;
				break;
			case AUDIO_CLK_FREQ_8:
				index = 5;
				break;
			case AUDIO_CLK_FREQ_11:
				index = 6;
				break;
			case AUDIO_CLK_FREQ_12:
				index = 7;
				break;
			case AUDIO_CLK_FREQ_16:
				index = 8;
				break;
			case AUDIO_CLK_FREQ_22:
				index = 9;
				break;
			case AUDIO_CLK_FREQ_24:
				index = 10;
				break;
			default:
				index=0;
				break;
		};
#if MESON_CPU_TYPE < MESON_CPU_TYPE_MESON6
	// get system crystal freq
		clk=clk_get_sys("clk_xtal", NULL);
		if(!clk)
		{
			printk(KERN_ERR "can't find clk %s for AUDIO PLL SETTING!\n\n","clk_xtal");
			//return -1;
		}
		else
		{
			xtal=clk_get_rate(clk);
			xtal=xtal/1000000;
			if(xtal>=24 && xtal <=25)/*current only support 24,25*/
			{
				xtal-=24;
			}
			else
			{
				printk(KERN_WARNING "UNsupport xtal setting for audio xtal=%d,default to 24M\n",xtal);
				xtal=0;
			}
		}

		audio_clock_config = audio_clock_config_table[xtal];
#endif

#if MESON_CPU_TYPE >= MESON_CPU_TYPE_MESON6
	if (fs_config == AUDIO_CLK_256FS) {
		// divide 256
		xtal = 0;
	}
	else if (fs_config == AUDIO_CLK_384FS) {
	    // divide 384
		xtal = 1;
	}
	audio_clock_config = audio_clock_config_table[xtal];
#endif

#ifdef CONFIG_SND_AML_M3
	if (((clk_get_rate(clk)==24000000)&&(get_ddr_pll_clk()==516000000)&&(index=2))) // 48k
	{
		WRITE_MPEG_REG( HHI_AUD_CLK_CNTL, READ_MPEG_REG(HHI_AUD_CLK_CNTL) & ~(1 << 8)); // audio clock off
		WRITE_MPEG_REG( HHI_AUD_PLL_CNTL, READ_MPEG_REG(HHI_AUD_PLL_CNTL) | (1 << 15)); // audio pll off
		WRITE_MPEG_REG_BITS(HHI_AUD_CLK_CNTL, 4, 9, 3); // select ddr
		WRITE_MPEG_REG_BITS(HHI_AUD_CLK_CNTL, 42-1, 0, 8); // 516/42
		WRITE_MPEG_REG_BITS(AIU_CODEC_ADC_LRCLK_CTRL, 64-1, 0, 12);//set codec adc ratio---lrclk
		WRITE_MPEG_REG_BITS(AIU_CODEC_DAC_LRCLK_CTRL, 64-1, 0, 12);//set codec dac ratio---lrclk
		WRITE_MPEG_REG(HHI_AUD_CLK_CNTL, READ_MPEG_REG(HHI_AUD_CLK_CNTL) | (1<<23));// gate audac_clkpi
		WRITE_MPEG_REG(HHI_AUD_CLK_CNTL, READ_MPEG_REG(HHI_AUD_CLK_CNTL) | (1 << 8));
		printk(KERN_INFO "audio 48k clock from ddr pll %dM\n", 516);
		return;
	}
	else if (((clk_get_rate(clk)==24000000)&&(get_ddr_pll_clk()==508000000)&&(index=1))) // 44.1k
	{
		WRITE_MPEG_REG( HHI_AUD_CLK_CNTL, READ_MPEG_REG(HHI_AUD_CLK_CNTL) & ~(1 << 8)); // audio clock off
		WRITE_MPEG_REG( HHI_AUD_PLL_CNTL, READ_MPEG_REG(HHI_AUD_PLL_CNTL) | (1 << 15)); // audio pll off
		WRITE_MPEG_REG_BITS(HHI_AUD_CLK_CNTL, 4, 9, 3); // select ddr
		WRITE_MPEG_REG_BITS(HHI_AUD_CLK_CNTL, 45-1, 0, 8); // 508/45
		WRITE_MPEG_REG_BITS(AIU_CODEC_ADC_LRCLK_CTRL, 64-1, 0, 12);//set codec adc ratio---lrclk
		WRITE_MPEG_REG_BITS(AIU_CODEC_DAC_LRCLK_CTRL, 64-1, 0, 12);//set codec dac ratio---lrclk
		WRITE_MPEG_REG(HHI_AUD_CLK_CNTL, READ_MPEG_REG(HHI_AUD_CLK_CNTL) | (1<<23));// gate audac_clkpi
		WRITE_MPEG_REG(HHI_AUD_CLK_CNTL, READ_MPEG_REG(HHI_AUD_CLK_CNTL) | (1 << 8));
		printk(KERN_INFO "audio 44.1k clock from ddr pll %dM\n", 508);
		return;
	}
	else if (((clk_get_rate(clk)==24000000)&&(get_ddr_pll_clk()==486000000)&&(index=1))) // 44.1k
	{
		WRITE_MPEG_REG( HHI_AUD_CLK_CNTL, READ_MPEG_REG(HHI_AUD_CLK_CNTL) & ~(1 << 8)); // audio clock off
		WRITE_MPEG_REG( HHI_AUD_PLL_CNTL, READ_MPEG_REG(HHI_AUD_PLL_CNTL) | (1 << 15)); // audio pll off
		WRITE_MPEG_REG_BITS(HHI_AUD_CLK_CNTL, 4, 9, 3); // select ddr
		WRITE_MPEG_REG_BITS(HHI_AUD_CLK_CNTL, 43-1, 0, 8); // 486/42
		WRITE_MPEG_REG_BITS(AIU_CODEC_ADC_LRCLK_CTRL, 64-1, 0, 12);//set codec adc ratio---lrclk
		WRITE_MPEG_REG_BITS(AIU_CODEC_DAC_LRCLK_CTRL, 64-1, 0, 12);//set codec dac ratio---lrclk
		WRITE_MPEG_REG(HHI_AUD_CLK_CNTL, READ_MPEG_REG(HHI_AUD_CLK_CNTL) | (1<<23));// gate audac_clkpi
		WRITE_MPEG_REG(HHI_AUD_CLK_CNTL, READ_MPEG_REG(HHI_AUD_CLK_CNTL) | (1 << 8));
		printk(KERN_INFO "audio 44.1k clock from ddr pll %dM\n", 486);
		return;
	}
	else if (((clk_get_rate(clk)==24000000)&&(get_ddr_pll_clk()==474000000)&&(index=1))) // 44.1k
	{
		WRITE_MPEG_REG( HHI_AUD_CLK_CNTL, READ_MPEG_REG(HHI_AUD_CLK_CNTL) & ~(1 << 8)); // audio clock off
		WRITE_MPEG_REG( HHI_AUD_PLL_CNTL, READ_MPEG_REG(HHI_AUD_PLL_CNTL) | (1 << 15)); // audio pll off
		WRITE_MPEG_REG_BITS(HHI_AUD_CLK_CNTL, 4, 9, 3); // select ddr
		WRITE_MPEG_REG_BITS(HHI_AUD_CLK_CNTL, 42-1, 0, 8); // 474/42
		WRITE_MPEG_REG_BITS(AIU_CODEC_ADC_LRCLK_CTRL, 64-1, 0, 12);//set codec adc ratio---lrclk
		WRITE_MPEG_REG_BITS(AIU_CODEC_DAC_LRCLK_CTRL, 64-1, 0, 12);//set codec dac ratio---lrclk
		WRITE_MPEG_REG(HHI_AUD_CLK_CNTL, READ_MPEG_REG(HHI_AUD_CLK_CNTL) | (1<<23));// gate audac_clkpi
		WRITE_MPEG_REG(HHI_AUD_CLK_CNTL, READ_MPEG_REG(HHI_AUD_CLK_CNTL) | (1 << 8));
		printk(KERN_INFO "audio 44.1k clock from ddr pll %dM\n", 474);
		return;
	}
#endif

    // gate the clock off
    WRITE_MPEG_REG( HHI_AUD_CLK_CNTL, READ_MPEG_REG(HHI_AUD_CLK_CNTL) & ~(1 << 8));

//#ifdef CONFIG_SND_AML_M3
#ifdef CONFIG_ARCH_MESON3
	WRITE_MPEG_REG(HHI_AUD_PLL_CNTL2, 0x065e31ff);
	WRITE_MPEG_REG(HHI_AUD_PLL_CNTL3, 0x9649a941);
	// select Audio PLL as MCLK source
	//WRITE_MPEG_REG( HHI_AUD_CLK_CNTL, READ_MPEG_REG(HHI_AUD_CLK_CNTL) & ~(1 << 9));
	WRITE_MPEG_REG_BITS(HHI_AUD_CLK_CNTL, 0, 9, 3);
	//WRITE_MPEG_REG_BITS(HHI_AUD_CLK_CNTL, 25-1, 0, 8);

	WRITE_MPEG_REG_BITS(HHI_AUD_CLK_CNTL, 13-1, 0, 8);
#endif

#if MESON_CPU_TYPE >= MESON_CPU_TYPE_MESON6
  WRITE_MPEG_REG(HHI_MPLL_CNTL3, 0x26e1250);
#endif

#if MESON_CPU_TYPE < MESON_CPU_TYPE_MESON6
    // Put the PLL to sleep
    WRITE_MPEG_REG( HHI_AUD_PLL_CNTL, READ_MPEG_REG(HHI_AUD_PLL_CNTL) | (1 << 15));//found

//#ifdef CONFIG_SND_AML_M3
#if MESON_CPU_TYPE <= MESON_CPU_TYPE_MESON3
	WRITE_MPEG_REG_BITS(AIU_CODEC_ADC_LRCLK_CTRL, 64-1, 0, 12);//set codec adc ratio---lrclk
	WRITE_MPEG_REG_BITS(AIU_CODEC_DAC_LRCLK_CTRL, 64-1, 0, 12);//set codec dac ratio---lrclk
#endif
    // Bring out of reset but keep bypassed to allow to stablize
    //Wr( HHI_AUD_PLL_CNTL, (1 << 15) | (0 << 14) | (hiu_reg & 0x3FFF) );
    WRITE_MPEG_REG( HHI_AUD_PLL_CNTL, (1 << 15) | (audio_clock_config[index][0] & 0x7FFF) );//found
    // Set the XD value
    WRITE_MPEG_REG( HHI_AUD_CLK_CNTL, (READ_MPEG_REG(HHI_AUD_CLK_CNTL) & ~(0xff << 0)) | audio_clock_config[index][1]);//found
    // delay 5uS
	//udelay(5);
	for (i = 0; i < 500000; i++) ;
    // Bring the PLL out of sleep
    WRITE_MPEG_REG( HHI_AUD_PLL_CNTL, READ_MPEG_REG(HHI_AUD_PLL_CNTL) & ~(1 << 15));//found

    // gate the clock on
    WRITE_MPEG_REG( HHI_AUD_CLK_CNTL, READ_MPEG_REG(HHI_AUD_CLK_CNTL) | (1 << 8));//found
#if MESON_CPU_TYPE <= MESON_CPU_TYPE_MESON3
	WRITE_MPEG_REG(HHI_AUD_CLK_CNTL, READ_MPEG_REG(HHI_AUD_CLK_CNTL) |(1<<23));// gate audac_clkpi
#endif
#else // endif CONFIG_ARCH_MESON6
    WRITE_MPEG_REG(AIU_CLK_CTRL_MORE, 0);
	WRITE_MPEG_REG_BITS(AIU_CODEC_ADC_LRCLK_CTRL, 64-1, 0, 12);//set codec adc ratio---lrclk
	WRITE_MPEG_REG_BITS(AIU_CODEC_DAC_LRCLK_CTRL, 64-1, 0, 12);//set codec dac ratio---lrclk

	// Select Multi-Phase PLL2 as clock source
	WRITE_MPEG_REG_BITS( HHI_AUD_CLK_CNTL, 3, 9, 3);

	// Configure Multi-Phase PLL2
	WRITE_MPEG_REG(HHI_MPLL_CNTL9, audio_clock_config[index][0]);
	// Set the XD value
	WRITE_MPEG_REG_BITS(HHI_AUD_CLK_CNTL, audio_clock_config[index][1], 0, 8);
	// delay 5uS
	//udelay(5);
	for (i = 0; i < 500000; i++) ;
	// gate the clock on
	WRITE_MPEG_REG( HHI_AUD_CLK_CNTL, READ_MPEG_REG(HHI_AUD_CLK_CNTL) | (1 << 8));

#if MESON_CPU_TYPE >= MESON_CPU_TYPE_MESON6TV
	//Audio DAC Clock enable
	WRITE_MPEG_REG(HHI_AUD_CLK_CNTL, READ_MPEG_REG(HHI_AUD_CLK_CNTL) |(1<<23));
/* ADC clock  configuration */
// Disable mclk
    WRITE_MPEG_REG_BITS(HHI_AUD_CLK_CNTL2, 0, 8, 1);

    // Select clk source, 0=ddr_pll; 1=Multi-Phase PLL0; 2=Multi-Phase PLL1; 3=Multi-Phase PLL2.
    WRITE_MPEG_REG_BITS(HHI_AUD_CLK_CNTL2, 3, 9, 2);

    // Set pll over mclk ratio
    //we want 256fs ADC MLCK,so for over clock mode,divide more 2 than I2S  DAC CLOCK
#if OVERCLOCK == 0
    WRITE_MPEG_REG_BITS(HHI_AUD_CLK_CNTL2, audio_clock_config[index][1], 0, 8);
#else
    WRITE_MPEG_REG_BITS(HHI_AUD_CLK_CNTL2, (audio_clock_config[index][1]+1)*2-1, 0, 8);
#endif

    // Set mclk over sclk ratio
    WRITE_MPEG_REG_BITS(AIU_CLK_CTRL_MORE, 4-1, 8, 6);

    // Set sclk over lrclk ratio
    WRITE_MPEG_REG_BITS(AIU_CODEC_ADC_LRCLK_CTRL, 64-1, 0, 12);

    // Enable sclk
    WRITE_MPEG_REG_BITS(AIU_CLK_CTRL_MORE, 1, 14, 1);
    // Enable mclk
    WRITE_MPEG_REG_BITS(HHI_AUD_CLK_CNTL2, 1, 8, 1);
#endif

#endif // endif CONFIG_ARCH_MESON6
    // delay 2uS
	//udelay(2);
	for (i = 0; i < 200000; i++) ;

    } else if (fs_config == AUDIO_CLK_384FS) {
    }
}

// iec958 and i2s clock are separated after M6TV. Use PLL2 for i2s DAC & ADC clock
void audio_set_i2s_clk(unsigned freq, unsigned fs_config)
{
    int i;
    int xtal = 0;

    int (*audio_clock_config)[2];

	int index=0;
	switch(freq)
	{
		case AUDIO_CLK_FREQ_192:
			index=4;
			break;
		case AUDIO_CLK_FREQ_96:
			index=3;
			break;
		case AUDIO_CLK_FREQ_48:
			index=2;
			break;
		case AUDIO_CLK_FREQ_441:
			index=1;
			break;
		case AUDIO_CLK_FREQ_32:
			index=0;
			break;
		case AUDIO_CLK_FREQ_8:
			index = 5;
			break;
		case AUDIO_CLK_FREQ_11:
			index = 6;
			break;
		case AUDIO_CLK_FREQ_12:
			index = 7;
			break;
		case AUDIO_CLK_FREQ_16:
			index = 8;
			break;
		case AUDIO_CLK_FREQ_22:
			index = 9;
			break;
		case AUDIO_CLK_FREQ_24:
			index = 10;
			break;
		default:
			index=0;
			break;
	};

	if (fs_config == AUDIO_CLK_256FS) {
		// divide 256
		xtal = 0;
	}
	else if (fs_config == AUDIO_CLK_384FS) {
	    // divide 384
		xtal = 1;
	}
	audio_clock_config = audio_clock_config_table[xtal];

    // gate the clock off
    WRITE_MPEG_REG( HHI_AUD_CLK_CNTL, READ_MPEG_REG(HHI_AUD_CLK_CNTL) & ~(1 << 8));
	WRITE_MPEG_REG(AIU_CLK_CTRL_MORE, 0);

	//Set filter register
	//WRITE_MPEG_REG(HHI_MPLL_CNTL3, 0x26e1250);

	/*--- DAC clock  configuration--- */
	// Disable mclk
    WRITE_MPEG_REG_BITS(HHI_AUD_CLK_CNTL, 0, 8, 1);
	// Select clk source, 0=ddr_pll; 1=Multi-Phase PLL0; 2=Multi-Phase PLL1; 3=Multi-Phase PLL2.
	WRITE_MPEG_REG_BITS( HHI_AUD_CLK_CNTL, 3, 9, 2);

	// Configure Multi-Phase PLL2
	WRITE_MPEG_REG(HHI_MPLL_CNTL9, audio_clock_config[index][0]);
	// Set the XD value
	WRITE_MPEG_REG_BITS(HHI_AUD_CLK_CNTL, audio_clock_config[index][1], 0, 8);

	WRITE_MPEG_REG_BITS(AIU_CODEC_DAC_LRCLK_CTRL, 64-1, 0, 12);//set codec dac ratio---lrclk--64fs

	// delay 5uS
	//udelay(5);
	for (i = 0; i < 500000; i++) ;
	// gate the clock on
	WRITE_MPEG_REG( HHI_AUD_CLK_CNTL, READ_MPEG_REG(HHI_AUD_CLK_CNTL) | (1 << 8));

	//Audio DAC Clock enable
	WRITE_MPEG_REG(HHI_AUD_CLK_CNTL, READ_MPEG_REG(HHI_AUD_CLK_CNTL) |(1<<23));

	/* ---ADC clock  configuration--- */
	// Disable mclk
	WRITE_MPEG_REG_BITS(HHI_AUD_CLK_CNTL, 0, 8, 1);

    // Select clk source, 0=ddr_pll; 1=Multi-Phase PLL0; 2=Multi-Phase PLL1; 3=Multi-Phase PLL2.
	WRITE_MPEG_REG_BITS(HHI_AUD_CLK_CNTL, 3, 9, 2);

    // Set pll over mclk ratio
    //we want 256fs ADC MLCK,so for over clock mode,divide more 2 than I2S  DAC CLOCK
#if OVERCLOCK == 0
	WRITE_MPEG_REG_BITS(HHI_AUD_CLK_CNTL, audio_clock_config[index][1], 0, 8);
#else
	WRITE_MPEG_REG_BITS(HHI_AUD_CLK_CNTL, (audio_clock_config[index][1]+1)*2-1, 0, 8);
#endif

    // Set mclk over sclk ratio
    WRITE_MPEG_REG_BITS(AIU_CLK_CTRL_MORE, 4-1, 8, 6);

    // Set sclk over lrclk ratio
    WRITE_MPEG_REG_BITS(AIU_CODEC_ADC_LRCLK_CTRL, 64-1, 0, 12); //set codec adc ratio---lrclk--64fs

    // Enable sclk
    WRITE_MPEG_REG_BITS(AIU_CLK_CTRL_MORE, 1, 14, 1);
    // Enable mclk
    WRITE_MPEG_REG_BITS(HHI_AUD_CLK_CNTL, 1, 8, 1);

    // delay 2uS
	//udelay(2);
	for (i = 0; i < 200000; i++) ;
}

// iec958 and i2s clock are separated after M6TV. Use PLL1 for iec958 clock
void audio_set_958_clk(unsigned freq, unsigned fs_config)
{
    int i;
    int xtal = 0;

    int (*audio_clock_config)[2];

	int index=0;
	switch(freq)
	{
		case AUDIO_CLK_FREQ_192:
			index=4;
			break;
		case AUDIO_CLK_FREQ_96:
			index=3;
			break;
		case AUDIO_CLK_FREQ_48:
			index=2;
			break;
		case AUDIO_CLK_FREQ_441:
			index=1;
			break;
		case AUDIO_CLK_FREQ_32:
			index=0;
			break;
		case AUDIO_CLK_FREQ_8:
			index = 5;
			break;
		case AUDIO_CLK_FREQ_11:
			index = 6;
			break;
		case AUDIO_CLK_FREQ_12:
			index = 7;
			break;
		case AUDIO_CLK_FREQ_16:
			index = 8;
			break;
		case AUDIO_CLK_FREQ_22:
			index = 9;
			break;
		case AUDIO_CLK_FREQ_24:
			index = 10;
			break;
		default:
			index=0;
			break;
	};

	if (fs_config == AUDIO_CLK_256FS) {
		// divide 256
		xtal = 0;
	}
	else if (fs_config == AUDIO_CLK_384FS) {
	    // divide 384
		xtal = 1;
	}
	audio_clock_config = audio_clock_config_table[xtal];

    // gate the clock off
    WRITE_MPEG_REG( HHI_AUD_CLK_CNTL, READ_MPEG_REG(HHI_AUD_CLK_CNTL) & ~(1 << 8));
	//WRITE_MPEG_REG(AIU_CLK_CTRL_MORE, 0);

	//Set filter register
	//WRITE_MPEG_REG(HHI_MPLL_CNTL3, 0x26e1250);

	/*--- IEC958 clock  configuration, use MPLL1--- */
	// Disable mclk
    WRITE_MPEG_REG_BITS(HHI_AUD_CLK_CNTL2, 0, 24, 1);
	//IEC958_USE_CNTL
	WRITE_MPEG_REG_BITS( HHI_AUD_CLK_CNTL2, 1, 27, 1);
	// Select clk source, 0=ddr_pll; 1=Multi-Phase PLL0; 2=Multi-Phase PLL1; 3=Multi-Phase PLL2.
	WRITE_MPEG_REG_BITS( HHI_AUD_CLK_CNTL2, 2, 25, 2);

	// Configure Multi-Phase PLL1
	WRITE_MPEG_REG(HHI_MPLL_CNTL8, audio_clock_config[index][0]);
	// Set the XD value
#if IEC958_OVERCLOCK	==1
	WRITE_MPEG_REG_BITS(HHI_AUD_CLK_CNTL2, (audio_clock_config[index][1]+1)/2 -1, 16, 8);
#else
	WRITE_MPEG_REG_BITS(HHI_AUD_CLK_CNTL2, audio_clock_config[index][1], 16, 8);
#endif

	// delay 5uS
	//udelay(5);
	for (i = 0; i < 500000; i++) ;
	// gate the clock on
	WRITE_MPEG_REG( HHI_AUD_CLK_CNTL, READ_MPEG_REG(HHI_AUD_CLK_CNTL) | (1 << 8));
	// Enable mclk
    WRITE_MPEG_REG_BITS(HHI_AUD_CLK_CNTL2, 1, 24, 1);
}

//extern void audio_out_enabled(int flag);
void audio_hw_958_raw(void);

void audio_enable_ouput(int flag)
{
    if (flag) {
        WRITE_MPEG_REG(AIU_RST_SOFT, 0x05);
        READ_MPEG_REG(AIU_I2S_SYNC);
        WRITE_MPEG_REG_BITS(AIU_MEM_I2S_CONTROL, 3, 1, 2);

        if (ENABLE_IEC958) {
            if(IEC958_MODE == AIU_958_MODE_RAW)
            {
              //audio_hw_958_raw();
            }
            //else
            {
              WRITE_MPEG_REG(AIU_958_FORCE_LEFT, 0);
              WRITE_MPEG_REG_BITS(AIU_958_DCU_FF_CTRL, 1, 0, 1);
              //WRITE_MPEG_REG(AIU_958_DCU_FF_CTRL, 1);

              WRITE_MPEG_REG_BITS(AIU_MEM_IEC958_CONTROL, 3, 1, 2);
            }
        }
        // Maybe cause POP noise
        // audio_i2s_unmute();
    } else {
        WRITE_MPEG_REG_BITS(AIU_MEM_I2S_CONTROL, 0, 1, 2);

        if (ENABLE_IEC958) {
            WRITE_MPEG_REG(AIU_958_DCU_FF_CTRL, 0);
            WRITE_MPEG_REG_BITS(AIU_MEM_IEC958_CONTROL, 0, 1, 2);
        }
        // Maybe cause POP noise
        // audio_i2s_mute();
    }
    //audio_out_enabled(flag);
}

int if_audio_out_enable()
{
	return READ_MPEG_REG_BITS(AIU_MEM_I2S_CONTROL, 1, 2);
}
int if_958_audio_out_enable(void)
{
	return READ_MPEG_REG_BITS(AIU_MEM_IEC958_CONTROL,1,2);
}
unsigned int read_i2s_rd_ptr(void)
{
    unsigned int val;
    val = READ_MPEG_REG(AIU_MEM_I2S_RD_PTR);
    return val;
}

void audio_i2s_unmute(void)
{
    WRITE_MPEG_REG_BITS(AIU_I2S_MUTE_SWAP, 0, 8, 8);
    WRITE_MPEG_REG_BITS(AIU_958_CTRL, 0, 3, 2);
}

void audio_i2s_mute(void)
{
    WRITE_MPEG_REG_BITS(AIU_I2S_MUTE_SWAP, 0xff, 8, 8);
    WRITE_MPEG_REG_BITS(AIU_958_CTRL, 3, 3, 2);
}

void audio_hw_958_reset(unsigned slow_domain, unsigned fast_domain)
{
	WRITE_MPEG_REG(AIU_958_DCU_FF_CTRL,0);
    WRITE_MPEG_REG(AIU_RST_SOFT,
                   (slow_domain << 3) | (fast_domain << 2));
}

void audio_hw_958_raw()
{
    if (ENABLE_IEC958) {
         WRITE_MPEG_REG(AIU_958_MISC, 1);
         WRITE_MPEG_REG_BITS(AIU_MEM_IEC958_CONTROL, 1, 8, 1);  // raw
         WRITE_MPEG_REG_BITS(AIU_MEM_IEC958_CONTROL, 0, 7, 1);  // 8bit
         WRITE_MPEG_REG_BITS(AIU_MEM_IEC958_CONTROL, 1, 3, 3); // endian
    }

    WRITE_MPEG_REG(AIU_958_BPF, IEC958_bpf);
    WRITE_MPEG_REG(AIU_958_BRST, IEC958_brst);
    WRITE_MPEG_REG(AIU_958_LENGTH, IEC958_length);
    WRITE_MPEG_REG(AIU_958_PADDSIZE, IEC958_padsize);
    WRITE_MPEG_REG_BITS(AIU_958_DCU_FF_CTRL, 0, 2, 2);// disable int

    if(IEC958_mode == 1){ // search in byte
      WRITE_MPEG_REG_BITS(AIU_958_DCU_FF_CTRL, 7, 4, 3);
    }else if(IEC958_mode == 2) { // search in word
      WRITE_MPEG_REG_BITS(AIU_958_DCU_FF_CTRL, 5, 4, 3);
    }else{
      WRITE_MPEG_REG_BITS(AIU_958_DCU_FF_CTRL, 0, 4, 3);
    }
    WRITE_MPEG_REG(AIU_958_CHSTAT_L0, IEC958_chstat0_l);
    WRITE_MPEG_REG(AIU_958_CHSTAT_L1, IEC958_chstat1_l);
    WRITE_MPEG_REG(AIU_958_CHSTAT_R0, IEC958_chstat0_r);
    WRITE_MPEG_REG(AIU_958_CHSTAT_R1, IEC958_chstat1_r);

    WRITE_MPEG_REG(AIU_958_SYNWORD1, IEC958_syncword1);
    WRITE_MPEG_REG(AIU_958_SYNWORD2, IEC958_syncword2);
    WRITE_MPEG_REG(AIU_958_SYNWORD3, IEC958_syncword3);
    WRITE_MPEG_REG(AIU_958_SYNWORD1_MASK, IEC958_syncword1_mask);
    WRITE_MPEG_REG(AIU_958_SYNWORD2_MASK, IEC958_syncword2_mask);
    WRITE_MPEG_REG(AIU_958_SYNWORD3_MASK, IEC958_syncword3_mask);

    printk("%s: %d\n", __func__, __LINE__);
    printk("\tBPF: %x\n", IEC958_bpf);
    printk("\tBRST: %x\n", IEC958_brst);
    printk("\tLENGTH: %x\n", IEC958_length);
    printk("\tPADDSIZE: %x\n", IEC958_length);
    printk("\tsyncword: %x, %x, %x\n\n", IEC958_syncword1, IEC958_syncword2, IEC958_syncword3);

}

void set_958_channel_status(_aiu_958_channel_status_t * set)
{
    if (set) {
		WRITE_MPEG_REG(AIU_958_CHSTAT_L0, set->chstat0_l);
		WRITE_MPEG_REG(AIU_958_CHSTAT_L1, set->chstat1_l);
		WRITE_MPEG_REG(AIU_958_CHSTAT_R0, set->chstat0_r);
		WRITE_MPEG_REG(AIU_958_CHSTAT_R1, set->chstat1_r);
    }
}

static void audio_hw_set_958_pcm24(_aiu_958_raw_setting_t * set)
{
    WRITE_MPEG_REG(AIU_958_BPF, 0x80); /* in pcm mode, set bpf to 128 */
    set_958_channel_status(set->chan_stat);
}

void audio_set_958_mode(unsigned mode, _aiu_958_raw_setting_t * set)
{
    if(mode == AIU_958_MODE_PCM_RAW){
    	mode = AIU_958_MODE_PCM16; //use 958 raw pcm mode
       WRITE_MPEG_REG(AIU_958_VALID_CTRL,3);//enable 958 invalid bit
    }
    else
	WRITE_MPEG_REG(AIU_958_VALID_CTRL,0);
    if (mode == AIU_958_MODE_RAW) {

        audio_hw_958_raw();
        if (ENABLE_IEC958) {
            WRITE_MPEG_REG(AIU_958_MISC, 1);
            WRITE_MPEG_REG_BITS(AIU_MEM_IEC958_CONTROL, 1, 8, 1);  // raw
            WRITE_MPEG_REG_BITS(AIU_MEM_IEC958_CONTROL, 0, 7, 1);  // 8bit
            WRITE_MPEG_REG_BITS(AIU_MEM_IEC958_CONTROL, 1, 3, 3); // endian
        }

        printk("IEC958 RAW\n");
    }else if(mode == AIU_958_MODE_PCM32){
        audio_hw_set_958_pcm24(set);
        if(ENABLE_IEC958){
            WRITE_MPEG_REG(AIU_958_MISC, 0x2020 | (1 << 7));
            WRITE_MPEG_REG_BITS(AIU_MEM_IEC958_CONTROL, 0, 8, 1);  // pcm
            WRITE_MPEG_REG_BITS(AIU_MEM_IEC958_CONTROL, 0, 7, 1);  // 16bit
            WRITE_MPEG_REG_BITS(AIU_MEM_IEC958_CONTROL, 0, 3, 3); // endian
        }
        printk("IEC958 PCM32 \n");
    }else if (mode == AIU_958_MODE_PCM24) {
        audio_hw_set_958_pcm24(set);
        if (ENABLE_IEC958) {
            WRITE_MPEG_REG(AIU_958_MISC, 0x2020 | (1 << 7));
            WRITE_MPEG_REG_BITS(AIU_MEM_IEC958_CONTROL, 0, 8, 1);  // pcm
            WRITE_MPEG_REG_BITS(AIU_MEM_IEC958_CONTROL, 0, 7, 1);  // 16bit
            WRITE_MPEG_REG_BITS(AIU_MEM_IEC958_CONTROL, 0, 3, 3); // endian

        }
        printk("IEC958 24bit\n");
    } else if (mode == AIU_958_MODE_PCM16) {
        audio_hw_set_958_pcm24(set);
        if (ENABLE_IEC958) {
            WRITE_MPEG_REG(AIU_958_MISC, 0x2042);
            WRITE_MPEG_REG_BITS(AIU_MEM_IEC958_CONTROL, 0, 8, 1);  // pcm
            WRITE_MPEG_REG_BITS(AIU_MEM_IEC958_CONTROL, 1, 7, 1);  // 16bit
            WRITE_MPEG_REG_BITS(AIU_MEM_IEC958_CONTROL, 0, 3, 3); // endian

        }
        printk("IEC958 16bit\n");
    }

    audio_hw_958_reset(0, 1);

    WRITE_MPEG_REG(AIU_958_FORCE_LEFT, 1);
}

void audio_hw_958_enable(unsigned flag)
{
    if (ENABLE_IEC958) {
        WRITE_MPEG_REG_BITS(AIU_MEM_IEC958_CONTROL, flag, 2, 1);
        WRITE_MPEG_REG_BITS(AIU_MEM_IEC958_CONTROL, flag, 1, 1);
        WRITE_MPEG_REG_BITS(AIU_MEM_IEC958_CONTROL, flag, 0, 1);
    }
}

unsigned int read_i2s_mute_swap_reg(void)
{
	unsigned int val;
    	val = READ_MPEG_REG(AIU_I2S_MUTE_SWAP);
    	return val;
}

void audio_i2s_swap_left_right(unsigned int flag)
{
	if (ENABLE_IEC958)
	{
		WRITE_MPEG_REG_BITS(AIU_958_CTRL, flag, 1, 2);
	}
	WRITE_MPEG_REG_BITS(AIU_I2S_MUTE_SWAP, flag, 0, 2);
}
unsigned int audio_hdmi_init_ready()
{
	return 	READ_MPEG_REG_BITS(AIU_HDMI_CLK_DATA_CTRL, 0, 2);
}
