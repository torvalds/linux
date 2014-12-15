#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <linux/pm.h>
#include <linux/i2c.h>
#include <linux/slab.h>
#include <linux/platform_device.h>
#include <linux/regulator/consumer.h>
#include <linux/spi/spi.h>
#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>
#include <sound/soc-dapm.h>
#include <sound/initval.h>
#include <sound/tlv.h>

#include <mach/am_regs.h>
#include "aml_audio_hw.h"
#include "aml_syno9629_codec.h"

#define CODEC_DEBUG  printk
#define stimulus_print pr_debug
#define Wr WRITE_MPEG_REG
#define Rd  READ_MPEG_REG
static struct snd_soc_codec *aml_syno9629_codec;

static void latch (void);
static void acodec_delay_us (int us);

/* codec private data */
struct aml_syno9629_codec_priv {
	struct snd_soc_codec codec;
	u16 reg_cache[ADAC_MAXREG];
	unsigned int sysclk;
};

u16 aml_syno9629_reg[ADAC_MAXREG] = {0};
static const unsigned int linein_values[] = {
    1|(1<<(1-1)),
    1|(1<<(2-1)),
    1|(1<<(3-1)),
    1|(1<<(4-1)),
    1|(1<<(5-1)),
    1|(1<<(6-1)),
    1|(1<<(7-1)),
    1|(1<<(8-1))
    };

unsigned long aml_rate_table[] = {
    8000, 11025, 12000, 16000, 22050, 24000, 32000,
    44100, 48000, 88200, 96000, 192000
};
static unsigned int acodec_regbank[ADAC_MAXREG] = {
									0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // Reg   0 -   9
                                    0x00, 0x00, 0x08, 0x08, 0x01, 0x00, 0x00, 0xae, 0x00, 0x00, // Reg  10 -  19
                                    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // Reg  20 -  29
                                    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x14, 0x14, 0x04, 0x04, // Reg  30 -  39
                                    0x12, 0x12, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x00, 0x00, // Reg  40 -  49
                                    0x00, 0x00, 0x54, 0x54, 0xff, 0xff, 0x28, 0x28, 0xff, 0xff, // Reg  50 -  59
                                    0x28, 0x28, 0xff, 0xff, 0xff, 0xff, 0x00, 0x00, 0x00, 0x00, // Reg  60 -  69
                                    0x00, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, // Reg  70 -  79
                                    0x01, 0x00, 0x00, 0x00, 0x00, 0x02, 0x02, 0x01, 0x01, 0x02, // Reg  80 -  89
                                    0x02, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x00, 0x00, 0x00, // Reg  90 -  99
                                    0x00, 0x12, 0x12, 0x3c, 0x3c, 0xff, 0xff, 0xff, 0xff, 0xff, // Reg 100 - 109
                                    0xff, 0x00, 0x00, 0x00, 0x00, 0x00, 0xff, 0xff, 0x2e, 0x2e, // Reg 110 - 119
                                    0xff, 0xff, 0x2e, 0x2e, 0xff, 0xff, 0xff, 0xff, 0x00, 0x00, // Reg 120 - 129
                                    0x00, 0x00, 0x04, 0x04, 0x12, 0x12, 0xff, 0xff, 0xff, 0xff, // Reg 130 - 139
                                    0xff, 0xff, 0x00, 0x00, 0x00, 0x00, 0xff, 0xff, 0x28, 0x28, // Reg 140 - 149
                                    0xff, 0xff, 0x28, 0x28, 0xff, 0xff, 0xff, 0xff, 0x00, 0x00, // Reg 150 - 159
                                    0x00, 0x00, 0x03, 0xf3, 0x00, 0x04, 0x03, 0x08, 0x00, 0x0c, // Reg 160 - 169
                                    0x00, 0x10, 0x00, 0x14, 0x00, 0x18, 0x01, 0x1c, 0x00, 0x20, // Reg 170 - 179
                                    0x00, 0x24, 0x00, 0x28, 0x7f, 0x2c, 0x24, 0x2c, 0x56, 0x30, // Reg 180 - 189
                                    0x00, 0x34, 0x37, 0x38, 0x7f, 0x3c, 0x01, 0x01, 0x00, 0x00, // Reg 190 - 199
                                    0x00, 0x06, 0x00, 0x06, 0x06, 0xff, 0x00, 0x00, 0x00, 0x00, // Reg 200 - 209
                                    0x00, 0x00, 0x00, 0x03, 0x00, 0xff, 0x00, 0x00, 0x00, 0x00, // Reg 210 - 219
                                    0x00, 0x00, 0x00, 0x00, 0x0b, 0x00, 0x40, 0x01, 0x00, 0x00, // Reg 220 - 229
                                    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // Reg 230 - 239
                                    0x02, 0x02, 0x00, 0x0a, 0x00, 0x00, 0x00, 0x00, 0x49, 0xfc, // Reg 240 - 249
                                    0x50, 0x84, 0x00, 0x00, 0x00, 0x00 							// Reg 250 - 255
                                   };

typedef enum  {
    AML_PWR_DOWN,
    AML_PWR_UP,
    AML_PWR_KEEP,
} AML_PATH_SET_TYPE;

static void set_acodec_source (unsigned int src)
{
    unsigned long data32;
    unsigned int i;

    // Disable acodec clock input and its DAC input
    data32  = 0;
    data32 |= 0     << 4;   // [5:4]    acodec_data_sel: 00=disable acodec_sdin; 01=Select pcm data; 10=Select AIU I2S data; 11=Not allowed.
    data32 |= 0     << 0;   // [1:0]    acodec_clk_sel: 00=Disable acodec_sclk; 01=Select pcm clock; 10=Select AIU aoclk; 11=Not allowed.
    Wr(AIU_CODEC_CLK_DATA_CTRL, data32);

    // Enable acodec clock from the selected source
    data32  = 0;
    data32 |= 0      << 4;  // [5:4]    acodec_data_sel: 00=disable acodec_sdin; 01=Select pcm data; 10=Select AIU I2S data; 11=Not allowed.
    data32 |= src   << 0;   // [1:0]    acodec_clk_sel: 00=Disable acodec_sclk; 01=Select pcm clock; 10=Select AIU aoclk; 11=Not allowed.
    Wr(AIU_CODEC_CLK_DATA_CTRL, data32);

    // Wait until clock change is settled
    i = 0;
    while ( (((Rd(AIU_CODEC_CLK_DATA_CTRL)) >> 8) & 0x3) != src ) {
        if (i > 255) {
            stimulus_print("[TEST.C] Error: set_acodec_source timeout!\n");
           // stimulus_finish_fail(10);
        }
        i ++;
    }

    // Enable acodec DAC input from the selected source
    data32  = 0;
    data32 |= src   << 4;   // [5:4]    acodec_data_sel: 00=disable acodec_sdin; 01=Select pcm data; 10=Select AIU I2S data; 11=Not allowed.
    data32 |= src   << 0;   // [1:0]    acodec_clk_sel: 00=Disable acodec_sclk; 01=Select pcm clock; 10=Select AIU aoclk; 11=Not allowed.
    Wr(AIU_CODEC_CLK_DATA_CTRL, data32);

    // Wait until data change is settled
    while ( (((Rd(AIU_CODEC_CLK_DATA_CTRL)) >> 12) & 0x3) != src) {}
} /* set_acodec_source */

static void adac_wr_reg (unsigned addr, unsigned data)
{
    WRITE_APB_REG((APB_BASE+(addr<<2)), data);
    acodec_regbank[addr] = data;
} /* adac_wr_reg */

static unsigned int adac_rd_reg (unsigned long addr)
{
    unsigned int data;
    data = READ_APB_REG((APB_BASE+(addr<<2)));
    return (data);
} /* adac_rd_reg */

static void adac_rd_check_reg (unsigned int addr, unsigned int exp_data, unsigned int mask)
{
    unsigned int rd_data;
    rd_data = adac_rd_reg(addr);
    if ((rd_data | mask) != (exp_data | mask)) {
        stimulus_print("[TEST.C] Error: audio CODEC register read data mismatch!\n");
        stimulus_print("addr=0x%x ,",addr);
        stimulus_print(" rd_data=0x%x ,",rd_data);
        stimulus_print(" exp_data=0x%x\n",exp_data);
    }
} /* adac_rd_check_reg */

static void acodec_startup_sequence (void)
{
    stimulus_print("[TEST.C] audio CODEC Startup Sequence -- Begin\n");
/*
1.	select the master clock mode mclksel[3:0] bit
2.	start the master clock
3.	set pdz bit to high
4.	select the sampling rate
5.	reset the signal path (rstdpz pin to low and back to high after 100ns)
6.	start the individual codec blocks

	7.1.1	Pop free start up recommendations
	To obtain a pop-free start-up for the playback channel, the corresponding
	blocks in the desired playback signal path must also be enable when setting
	the master power up control active, as per the start-up sequence step 3
	(above).

	For example, when setting pdz bit to high, pddacl/rz bit, pdhsdrvl/rz bit
	and/or pdauxdrvl/rz bit should also be set to high at the same time to
	obtain a clean, pop-free start up.
	By using the latch signal properly, it is possible to guarantee that all the
	required power control, signals are loaded to the Audio Codec IP
	simultaneously.*/

	// Init at zero (In power Down and In Reset)
    //data32  = 0;
    //data32 |= 1     << 15;  // [15]     audac_soft_reset_n
    //data32 |= 1     << 14;  // [14]     audac_reset_ctrl: 0=use audac_reset_n pulse from reset module; 1=use audac_soft_reset_n.
    //data32 |= 0     << 9;   // [9]      delay_rd_en
    //data32 |= 0     << 8;   // [8]      audac_reg_clk_inv
    //data32 |= 0x55  << 1;   // [7:1]    audac_i2caddr
    //data32 |= 0     << 0;   // [0]      audac_intfsel: 0=use host bus; 1=use I2C.
    Wr(AIU_AUDAC_CTRL0, Rd(AIU_AUDAC_CTRL0) | (1<<15) | (1<<14));
//    adac_wr_reg(0x15, 0x00);
//    latch();
    acodec_delay_us(3000);

    // Disable system reset (In power Down and Out of Reset)
    Wr(AIU_AUDAC_CTRL0, Rd(AIU_AUDAC_CTRL0) & (~(1<<15)));
    acodec_delay_us(3000);

    stimulus_print("[TEST.C] audio CODEC Startup Sequence -- End\n");
} /* acodec_startup_sequence */

static void acodec_config (unsigned long mclkseladc,   // [3:0]: 0=256*Fs; 1=384*Fs; 2=12M; 3=24M; 4=12.288M; 5=24.576M; 6=11.2896M; 7=22.5792M; 8=18.432M; 9=36.864M; 10=16.9344M; 11=33.8688M; >=12:Reserved.
                    unsigned long mclkseldac,   // [3:0]: 0=256*Fs; 1=384*Fs; 2=12M; 3=24M; 4=12.288M; 5=24.576M; 6=11.2896M; 7=22.5792M; 8=18.432M; 9=36.864M; 10=16.9344M; 11=33.8688M; >=12:Reserved.
                    unsigned long i2sfsadc,     // [3:0]: 0=8k; 1=11.025k; 2=12; 3=16k; 4=22.05k; 5=24k; 6=32k, 7=44.1k, 8=48k; 9=88.2k; 10=96k; 11=192k; >=12:Reserved.
                    unsigned long i2sfsdac,     // [3:0]: 0=8k; 1=11.025k; 2=12; 3=16k; 4=22.05k; 5=24k; 6=32k, 7=44.1k, 8=48k; 9=88.2k; 10=96k; 11=192k; >=12:Reserved.
                    unsigned long i2smsmode,    // 0=slave mode; 1=master mode.
                    unsigned long i2smode,      // [2:0]: 0=Right justify, 1=I2S, 2=Left justify, 3=Burst1, 4=Burst2, 5=Mono burst1, 6=Mono burst2, 7=Rsrv.
                    unsigned long pgamute,      // [1:0]: [0] Input PGA left channel mute; [1] Input PGA right channel mute. 0=un-mute; 1=mute.
                    unsigned long recmute,      // [1:0]: [0] Recording left channel digital mute; [1] Recording right channel digital mute. 0=un-mute; 1=mute.
                    unsigned long hs1mute,      // [1:0]: [0] Headset left channel analog mute; [1] Headset right channel analog mute. 0=un-mute; 1=mute.
                    unsigned long lmmute,       // [1:0]: [0] Playback left channel digital mute; [1] Playback right channel digital mute. 0=un-mute; 1=mute.
                    unsigned long ldr1outmute,  // [1:0]: [0] Playback left channel analog mute; [1] Playback right channel analog mute. 0=un-mute; 1=mute.
                    unsigned long recvol,       // [15:0]: Recording digital master volume control. [7:0] Left; [15:8] Right. 0x14=0dB.
                    unsigned long pgavol,       // [15:0]: Input PGA volume control. [7:0] Left; [15:8] Right. 0x04=0dB.
                    unsigned long lmvol,        // [15:0]: Digital playback master volume control. [7:0] Left; [15:8] Right. 0x54=0dB.
                    unsigned long hs1vol,       // [15:0]: Headset analog volume control. [7:0] Left; [15:8] Right. 0x28=0dB.
                    unsigned long pgasel,       // [15:0]: Left PGA input selection. [7:0] Left; [15:8] Right. 0x01=Input1, 0x03=Input2, 0x05=Input3, 0x09=Input4, 0x11=Input5, 0x21=Input6, 0x41=Input7, 0x81=Input8, others=Rsrv.
                    unsigned long ldr1sel,      // [15:0]: Playback analog mixer input selection. [7:0] Left; [15:8] Right. [0]:1=Enable left DAC output; [1]:1=Enable left PGA; [2]:1=Enable right DAC output.
                    unsigned long ctr,          // [1:0]: test mode sel. 0=Normal, 1=Digital filter loopback, 2=Digital filter bypass, 3=Digital audio I/F loopback.
                    unsigned long recmix,       // [1:0]: Record digital mixer sel.
                    unsigned long enhp,         // Record channel high pass filter enable.
                    unsigned long lmmix)        // [1:0]: Playback digital mixer sel.
{
    stimulus_print("[TEST.C] audio CODEC register config -- Begin\n");
    adac_wr_reg(ADC_MCLK_SEL, mclkseladc);
    adac_wr_reg(DAC_MCLK_SEL, mclkseldac); //256fs
    adac_wr_reg(ADC_I2S_FS_SEL, i2sfsadc);
    adac_wr_reg(DAC_I2S_FS_SEL, i2sfsdac); //48k
    adac_wr_reg(ADAC_I2S_MODE_SEL, (i2smsmode<<3) | i2smode); //slave mode ,i2s
    adac_wr_reg(ADAC_MUTE_CTRL0, (pgamute<<2) | recmute); //un mute
    adac_wr_reg(ADAC_MUTE_CTRL2, (hs1mute<<4) | lmmute);  //un mute
    adac_wr_reg(ADAC_MUTE_CTRL4, (ldr1outmute<<2));
    adac_wr_reg(ADC_REC_MVOL_LSB_CTRL, recvol&0xff);
    adac_wr_reg(ADC_REC_MVOL_MSB_CTRL, recvol>>8);
    adac_wr_reg(ADC_PGA_VOL_LSB_CTRL, pgavol&0xff);
    adac_wr_reg(ADC_PGA_VOL_MSB_CTRL, pgavol>>8);
    adac_wr_reg(DAC_PLYBACK_MVOL_LSB_CTRL, lmvol&0xff);
    adac_wr_reg(DAC_PLYBACK_MVOL_MSB_CTRL, lmvol>>8);
    adac_wr_reg(DAC_HS_VOL_LSB_CTRL, hs1vol&0xff);
    adac_wr_reg(DAC_HS_VOL_MSB_CTRL, hs1vol>>8);
    adac_wr_reg(ADC_REC_INPUT_CH_LSB_SEL, pgasel&0xff);
    adac_wr_reg(ADC_REC_INPUT_CH_MSB_SEL, pgasel>>8);
    adac_wr_reg(DAC_PLYBACK_MIXER_LSB_CTRL, ldr1sel&0xff);
    adac_wr_reg(DAC_PLYBACK_MIXER_MSB_CTRL, ldr1sel>>8);
    adac_wr_reg(ADAC_DIGITAL_TEST_MODE_SEL, ctr);
    adac_wr_reg(ADC_REC_PATH_MIXER_SEL, (enhp<<2) | recmix);
    adac_wr_reg(DAC_PLYBACK_DIG_MIXER_SEL, lmmix);
    latch();
    stimulus_print("[TEST.C] audio CODEC register config -- End\n");
}

static void acodec_prepare_register (void)
{
    stimulus_print("[TEST.C] acodec_prepare_register -- Begin\n");
    adac_wr_reg(ADAC_POWER_CTRL0, 0xfe);
    adac_wr_reg(ADAC_POWER_CTRL1, 0xff);
    //adac_wr_reg(0x17, 0xff);
    adac_wr_reg(ADAC_POWER_CTRL3, 0xff);
  //  adac_wr_reg(0x19, 0xff);
    // Config the not power down
   // adac_wr_reg(0x11, 0x80); // configure the bypass prechage

    // Setup the lssel to 11, enable the line output
    //adac_wr_reg(0x55, 0x01);
    //adac_wr_reg(0x56, 0x01);
    //latch();
    //acodec_delay_us(4000); // wait for 4 ms.

    // Set bias current to 1/40 of the nominal current
    adac_wr_reg(ADAC_POWER_CUM_CTRL, 0x0b);
    latch();
    stimulus_print("[TEST.C] acodec_prepare_register -- End\n");
} /* acodec_prepare_register */

void acodec_powerup_fastcharge (void)
{
    stimulus_print("[TEST.C] acodec_powerup_fastcharge -- Begin\n");

    // Disable the bypass fast charger
    adac_wr_reg(ADAC_DIG_ASS_TEST_REG2, 0x00);
    // test1 register, bit[2:1] to configure the charge time for VCM, when 0 100ms, when 1, 1s
    adac_wr_reg(ADAC_DIG_ASS_TEST_REG1, 0x00);
    // Enable the new FSM for power up, bit[7] for enable the original FSM
    // Configure the vcmok, bit[1:0] for vcm ok configuration, when 11, 1.65v, when 01, lower
    adac_wr_reg(ADAC_VCM_RAMP_CTRL, 0x83);
    latch();
    acodec_delay_us(4000); // wait for 4 ms

    // reset data path
    adac_wr_reg(ADAC_RESET, 0x00);
    latch();
    adac_wr_reg(ADAC_RESET, 0x03);
    latch();
    // Out of power Down and Out of Reset
    adac_wr_reg(ADAC_POWER_CTRL0, 0x05);
    adac_wr_reg(ADAC_POWER_CUM_CTRL, 0x05);
    latch();

    acodec_delay_us(1050000); // wait for 1.05 sec
    stimulus_print("[TEST.C] acodec_powerup_fastcharge -- End\n");
} /* acodec_powerup_fastcharge */
static void acodec_powerup_bypassfastcharge (void)
{
    stimulus_print("[TEST.C] acodec_powerup_bypassfastcharge -- Begin\n");
    adac_wr_reg(ADAC_DIG_ASS_TEST_REG1, 0x00);
    // Bypass power up sequence
    // [0]--bypasspwrseq
    // [1]--cfgprechanaref
    adac_wr_reg(ADAC_DIG_ASS_TEST_REG2, 0x01);
    // Disable soft ramping
    adac_wr_reg(ADAC_DIG_ASS_TEST_REG4, 0x80);

    // [7]--enprechanaref
    adac_wr_reg(ADAC_ANALOG_TEST_REG3, 0x84);
    // Enable the new FSM for power up, bit[7] for enable the original FSM
    // Configure the vcmok, bit[1:0] for vcm ok configuration, when 11, 1.65v, when 01, lower
    adac_wr_reg(ADAC_VCM_RAMP_CTRL, 0x83);

    // Power up block and bias generator
    latch();


    // reset data path
    adac_wr_reg(ADAC_RESET, 0x00);
    latch();
    adac_wr_reg(ADAC_RESET, 0x03);
    latch();
    //acodec_delay_us(4000); // wait for 4 ms.
    adac_wr_reg(ADAC_POWER_CTRL0, 0x0f);
    latch();

    adac_wr_reg(ADAC_POWER_CUM_CTRL, 0x05);
    latch();
    acodec_delay_us(5000);
    stimulus_print("[TEST.C] acodec_powerup_bypassfastcharge -- End\n");
} /* acodec_powerup_bypassfastcharge */

static void latch (void)
{
    int latch;
    latch = 1;
    adac_wr_reg(ADAC_LATCH, latch);
    adac_rd_check_reg(ADAC_LATCH, latch, 0);
    latch = 0;
    adac_wr_reg(ADAC_LATCH, latch);
    adac_rd_check_reg(ADAC_LATCH, latch, 0);
} /* latch */

static void acodec_delay_us (int us)
{
	msleep(us/1000);
} /* acodec_delay_us */

static void aml_reset_path(struct snd_soc_codec* codec, AML_PATH_SET_TYPE type)
{
    CODEC_DEBUG( "enter %s\n", __func__);

    return ;
}

static void aml_syno9629_reset(struct snd_soc_codec* codec, bool first_time)
{
	unsigned long   data32;
	int i;
	CODEC_DEBUG( "enter %s\n", __func__);

	if (first_time)
	{
		CODEC_DEBUG( " first time enter %s\n", __func__);

		audio_set_clk(AUDIO_CLK_FREQ_48,0);
		set_acodec_source(2);
		WRITE_MPEG_REG(AUDIN_SOURCE_SEL, (1<<0)); // select audio codec output as I2S source
		//msleep(100);
		data32  = 0;
		// --------------------------------------------------------
		// Configure audio DAC control interface
		// --------------------------------------------------------
		data32  = 0;
		data32 |= 0     << 15;  // [15]     audac_soft_reset_n
		data32 |= 1     << 14;  // [14]     audac_reset_ctrl: 0=use audac_reset_n pulse from reset module; 1=use audac_soft_reset_n.
		data32 |= 0     << 9;   // [9]      delay_rd_en
		data32 |= 0     << 8;   // [8]      audac_reg_clk_inv
		data32 |= 0x55  << 1;   // [7:1]    audac_i2caddr
		data32 |= 0     << 0;   // [0]      audac_intfsel: 0=use host bus; 1=use I2C.
		Wr(AIU_AUDAC_CTRL0, data32);
		// Enable APB3 fail on error
		data32  = 0;
		data32 |= 1     << 15;  // [15]     err_en
		data32 |= 255   << 0;   // [11:0]   max_err
		Wr(AIU_AUDAC_CTRL1, data32);
		// Check read back data
		data32 = Rd(AIU_AUDAC_CTRL0);
		if (data32 != ((1<<14) | (0x55<<1))) {
			stimulus_print("[TEST.C] Error: AIU_AUDAC_CTRL0 read data mismatch!");
			//stimulus_finish_fail(10);
		}
		data32 = Rd(AIU_AUDAC_CTRL1);
		if (data32 != 0x80ff) {
			stimulus_print("[TEST.C] Error: AIU_AUDAC_CTRL1 read data mismatch!");
			//stimulus_finish_fail(10);
		}

		// Check audio CODEC default register default values
		stimulus_print("[TEST.C] Checking audio CODEC default register default values ...\n");
		for (i = 0; i < 252; i ++) {
			adac_rd_check_reg(i, acodec_regbank[i], 0);
		}
		stimulus_print("[TEST.C] ... Done checking audio CODEC default register values\n");

		// --------------------------------------
		// Setup Audio CODEC
		// --------------------------------------
		stimulus_print("[TEST.C] Setup audio CODEC ...\n");

		acodec_startup_sequence(); //reset rstz  ?

		acodec_config(  0,      // mclkseladc[3:0]: 0=256*Fs; 1=384*Fs; 2=12M; 3=24M; 4=12.288M; 5=24.576M; 6=11.2896M; 7=22.5792M; 8=18.432M; 9=36.864M; 10=16.9344M; 11=33.8688M; >=12:Reserved.
		    0,      // mclkseldac[3:0]: 0=256*Fs; 1=384*Fs; 2=12M; 3=24M; 4=12.288M; 5=24.576M; 6=11.2896M; 7=22.5792M; 8=18.432M; 9=36.864M; 10=16.9344M; 11=33.8688M; >=12:Reserved.
		    8,     // i2sfsadc[3:0]: 0=8k; 1=11.025k; 2=12; 3=16k; 4=22.05k; 5=24k; 6=32k, 7=44.1k, 8=48k; 9=88.2k; 10=96k; 11=192k; >=12:Reserved.
		    8,      // i2sfsdac[3:0]: 0=8k; 1=11.025k; 2=12; 3=16k; 4=22.05k; 5=24k; 6=32k, 7=44.1k, 8=48k; 9=88.2k; 10=96k; 11=192k; >=12:Reserved.
		    0,      // i2smsmode: 0=slave mode; 1=master mode.
		    1,      // i2smode[2:0]: 0=Right justify, 1=I2S, 2=Left justify, 3=Burst1, 4=Burst2, 5=Mono burst1, 6=Mono burst2, 7=Rsrv.
		    0,      // pgamute[1:0]: [0] Input PGA left channel mute; [1] Input PGA right channel mute. 0=un-mute; 1=mute.
		    0,      // recmute[1:0]: [0] Recording left channel digital mute; [1] Recording right channel digital mute. 0=un-mute; 1=mute.
		    0,      // hs1mute[1:0]: [0] Headset left channel analog mute; [1] Headset right channel analog mute. 0=un-mute; 1=mute.
		    0,      // lmmute[1:0]: [0] Playback left channel digital mute; [1] Playback right channel digital mute. 0=un-mute; 1=mute.
		    0,      // ldr1outmute[1:0]: [0] Playback left channel analog mute; [1] Playback right channel analog mute. 0=un-mute; 1=mute.
		    0x1414, // recvol[15:0]: Recording digital master volume control. [7:0] Left; [15:8] Right. 0x14=0dB.
		    0x0404, // pgavol[15:0]: Input PGA volume control. [7:0] Left; [15:8] Right. 0x04=0dB.
		    0x5454, // lmvol[15:0]: Digital playback master volume control. [7:0] Left; [15:8] Right. 0x54=0dB.
		    0x2828, // hs1vol[15:0]: Headset analog volume control. [7:0] Left; [15:8] Right. 0x28=0dB.
		    0x0101, // pgasel[15:0]: Left PGA input selection. [7:0] Left; [15:8] Right. 0x01=Input1, 0x03=Input2, 0x05=Input3, 0x09=Input4, 0x11=Input5, 0x21=Input6, 0x41=Input7, 0x81=Input8, others=Rsrv.
		    0x0101, // ldr1sel[15:0]: Playback analog mixer input selection. [7:0] Left; [15:8] Right. [0]:1=Enable left DAC output; [1]:1=Enable left PGA; [2]:1=Enable right DAC output.
		    0,      // ctr[1:0]: test mode sel. 0=Normal, 1=Digital filter loopback, 2=Digital filter bypass, 3=Digital audio I/F loopback.
		    0,      // recmix[1:0]: Record digital mixer sel.
		    0,      // enhp: Record channel high pass filter enable.
		    0);     // lmmix[1:0]: Playback digital mixer sel.
		acodec_prepare_register();

		acodec_powerup_bypassfastcharge();
		//acodec_powerup_fastcharge();
		// Check audio DAC register read-back values
		stimulus_print("[TEST.C] Checking audio DAC register read-back values ...\n");
		for (i = 0; i < 252; i ++) {
			adac_rd_check_reg(i, acodec_regbank[i], 0);
		}

		stimulus_print("[TEST.C] ... Done audio CODEC power-up bypass fast charge\n");
	}
}

static int aml_switch_get_enum(struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_value *ucontrol)
{
	//CODEC_DEBUG( "enter %s\n", __func__);
#if 0
	struct snd_soc_codec *codec = snd_kcontrol_chip(kcontrol);
	struct soc_enum *e = (struct soc_enum *)kcontrol->private_value;
	struct snd_soc_dapm_widget *w;
	//struct snd_soc_dapm_context * dapm = &codec->dapm;
	char *lname = NULL;
	char *rname = NULL;

    switch (e->reg)
    {
    case ADAC_POWER_CTRL_REG1:
        if (6 == e->shift_l)
        {
            lname = "LINEOUTL";
            rname = "LINEOUTR";
        }
        else if (4 == e->shift_l)
        {
            lname = "HP_L";
            rname = "HP_R";
        }
        else if (2 == e->shift_l)
        {
            lname = "SPEAKER";
        }
    break;
    case ADAC_POWER_CTRL_REG2:
        if (2 == e->shift_l)
        {
            lname = "LINEINL";
            rname = "LINEINR";
        }
    break;
    default:
    break;
    }

	list_for_each_entry(w, &codec->card->widgets, list) {
        if (lname && !strcmp(lname, w->name))
            ucontrol->value.enumerated.item[0] = w->connected;
        if (rname && !strcmp(rname, w->name))
            ucontrol->value.enumerated.item[0] = w->connected;
	}
#endif
	return 0;
}

static int aml_switch_put_enum(struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_value *ucontrol)
{
	//CODEC_DEBUG( "enter %s\n", __func__);
#if 0
	struct snd_soc_codec *codec = snd_kcontrol_chip(kcontrol);
	struct soc_enum *e = (struct soc_enum *)kcontrol->private_value;
    struct snd_soc_dapm_widget *w;
    char *lname = NULL;
    char *rname = NULL;
    unsigned int pwr_reg;

    switch (e->reg)
    {
    case ADAC_POWER_CTRL_REG1:
        if (6 == e->shift_l)
        {
            lname = "LINEOUTL";
            rname = "LINEOUTR";
        }
        else if (4 == e->shift_l)
        {
            lname = "HP_L";
            rname = "HP_R";
        }
        else if (2 == e->shift_l)
        {
            lname = "SPEAKER";
        }
    break;
    case ADAC_POWER_CTRL_REG2:
        if (2 == e->shift_l)
        {
            lname = "LINEINL";
            rname = "LINEINR";
        }
    break;
    default:
    break;
    }

    pwr_reg = snd_soc_read(codec, e->reg);
    if(ucontrol->value.enumerated.item[0] == 0){
    snd_soc_write(codec, e->reg, (pwr_reg&(~(0x1<<(e->shift_l)|0x1<<(e->shift_r)))));
    }
    else{
    snd_soc_write(codec, e->reg, (pwr_reg|(0x1<<(e->shift_l)|0x1<<(e->shift_r))));
    }

	list_for_each_entry(w, &codec->card->widgets, list) {
        if (lname && !strcmp(lname, w->name))
        {
            w->connected = ucontrol->value.enumerated.item[0];
            CODEC_DEBUG("%s:connect=%d\n",w->name,w->connected);
        }
        if (rname && !strcmp(rname, w->name))
        {
            w->connected = ucontrol->value.enumerated.item[0];
            CODEC_DEBUG("%s:connect=%d\n",w->name,w->connected);
        }
	}
#endif
	return 0;
}

static int aml_put_volsw_2r(struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_value *ucontrol)
{
    struct snd_soc_codec *codec = snd_kcontrol_chip(kcontrol);
    int err = snd_soc_put_volsw_2r(kcontrol, ucontrol);
    if (err < 0)
        return err;

    aml_reset_path(codec, AML_PWR_KEEP);
    return 0;
}

static int aml_ai_source_get_enum(struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_value *ucontrol)
{
    //struct snd_soc_codec *codec = snd_kcontrol_chip(kcontrol);
    //struct soc_enum *e = (struct soc_enum *)kcontrol->private_value;
	//CODEC_DEBUG( "enter %s\n", __func__);

    if (READ_MPEG_REG(AUDIN_SOURCE_SEL) == 0)
        WRITE_MPEG_REG(AUDIN_SOURCE_SEL, (1<<0)); // select audio codec output as I2S source

    if (READ_MPEG_REG(AUDIN_SOURCE_SEL) == 1)
        ucontrol->value.enumerated.item[0] = 0;// linein
    else
        ucontrol->value.enumerated.item[0] = 1;//hdmi

    return 0;
}

static int aml_ai_source_put_enum(struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_value *ucontrol)
{
    struct snd_soc_codec *codec = snd_kcontrol_chip(kcontrol);
    //struct soc_enum *e = (struct soc_enum *)kcontrol->private_value;

    if (ucontrol->value.enumerated.item[0] == 0)
        WRITE_MPEG_REG(AUDIN_SOURCE_SEL, (1<<0)); // select audio codec output as I2S source
    else{
  	 WRITE_MPEG_REG(AUDIN_SOURCE_SEL,(0  <<12)   | // [14:12]cntl_hdmirx_chsts_sel: 0=Report chan1 status; 1=Report chan2 status; ...;

                                            (0xf<<8)    | // [11:8] cntl_hdmirx_chsts_en

                                            (1  <<4)    | // [5:4]  spdif_src_sel: 1=Select HDMIRX SPDIF output as AUDIN source

                                            (2 << 0));    // [1:0]  i2sin_src_sel: 2=Select HDMIRX I2S output as AUDIN source

    }
	// reset adc data path
    snd_soc_write(codec, ADAC_RESET, 1);
    snd_soc_write(codec, ADAC_RESET, 3);

    return 0;
}



static const DECLARE_TLV_DB_SCALE(lineout_volume, -12600, 150, 0);
static const DECLARE_TLV_DB_SCALE(hs_volume, -4000, 100, 0);
static const DECLARE_TLV_DB_SCALE(linein_volume, -9600, 150, 0);

static const char *left_linein_texts[] = {
	"Left Line In 1", "Left Line In 2", "Left Line In 3", "Left Line In 4",
	"Left Line In 5", "Left Line In 6", "Left Line In 7", "Left Line In 8"
	};

static const char *right_linein_texts[] = {
	"Right Line In 1", "Right Line In 2", "Right Line In 3", "Right Line In 4",
	"Right Line In 5", "Right Line In 6", "Right Line In 7", "Right Line In 8"
	};



static const char *iis_split_texts[] = {
	"iis_not_split", "iis_split"
	};

static const unsigned int iis_split_values[] = {
	0,
	1
    };


static const SOC_VALUE_ENUM_SINGLE_DECL(left_linein_select, ADAC_REC_CH_SEL_LSB,
		0, 0xff, left_linein_texts, linein_values);
static const SOC_VALUE_ENUM_SINGLE_DECL(right_linein_select, ADAC_REC_CH_SEL_MSB,
		0, 0xff, right_linein_texts, linein_values);
static const SOC_VALUE_ENUM_SINGLE_DECL(iis_split_select, DAC_I2S_FS_SEL,
		3, 0xff, iis_split_texts, iis_split_values);

static const char *switch_op_modes_texts[] = {
	"OFF", "ON"
};
//Left/Right DAC power-down/up
static const struct soc_enum lineout_op_modes_enum =
	SOC_ENUM_DOUBLE(ADAC_POWER_CTRL3, 0, 1,
			ARRAY_SIZE(switch_op_modes_texts),
			switch_op_modes_texts);
//left/Right headset power-down signal
static const struct soc_enum hp_op_modes_enum =
	SOC_ENUM_DOUBLE(ADAC_POWER_CTRL3, 4, 5,
			ARRAY_SIZE(switch_op_modes_texts),
			switch_op_modes_texts);
//Left/Right PGA power-down signal
static const struct soc_enum linein_op_modes_enum =
	SOC_ENUM_DOUBLE(ADAC_POWER_CTRL1, 2,3,
			ARRAY_SIZE(switch_op_modes_texts),
			switch_op_modes_texts);
//
static const struct soc_enum sp_op_modes_enum =
	SOC_ENUM_DOUBLE(ADAC_POWER_CTRL3, 0, 1,
			ARRAY_SIZE(switch_op_modes_texts),
			switch_op_modes_texts);

static const char *audio_in_source_texts[] = {
	"LINEIN", "HDMI"
};
static const struct soc_enum audio_in_source_enum =
	SOC_ENUM_SINGLE(SND_SOC_NOPM, 0,
			ARRAY_SIZE(audio_in_source_texts),
			audio_in_source_texts);


static const struct snd_kcontrol_new amlsyno9629_snd_controls[] = {
	SOC_DOUBLE_R_EXT_TLV("LINEOUT Playback Volume", DAC_PLYBACK_MVOL_LSB_CTRL, DAC_PLYBACK_MVOL_MSB_CTRL,
	       0, 84, 0, snd_soc_get_volsw_2r, aml_put_volsw_2r, lineout_volume),

	 SOC_DOUBLE_R_EXT_TLV("HeadSet Playback Volume", DAC_HS_VOL_LSB_CTRL, DAC_HS_VOL_MSB_CTRL,
	       0, 46, 0, snd_soc_get_volsw_2r, aml_put_volsw_2r, hs_volume),

    SOC_DOUBLE_R_EXT_TLV("LINEIN Capture Volume", ADC_REC_MVOL_LSB_CTRL, ADC_REC_MVOL_MSB_CTRL,
	       0, 84, 1, snd_soc_get_volsw_2r, aml_put_volsw_2r, linein_volume),

	SOC_VALUE_ENUM("Left LINEIN Select",left_linein_select),
	SOC_VALUE_ENUM("Right LINEIN Select",right_linein_select),
	SOC_VALUE_ENUM("IIS Split Select", iis_split_select),

    SOC_ENUM_EXT("LOUT Playback Switch", lineout_op_modes_enum,
		aml_switch_get_enum,aml_switch_put_enum),

    SOC_ENUM_EXT("HP Playback Switch", hp_op_modes_enum,
		aml_switch_get_enum,aml_switch_put_enum),

	SOC_ENUM_EXT("LIN Capture Switch", linein_op_modes_enum,
		aml_switch_get_enum,aml_switch_put_enum),

	SOC_ENUM_EXT("SP Playback Switch", sp_op_modes_enum,
		aml_switch_get_enum,aml_switch_put_enum),

    SOC_ENUM_EXT("Audio In Source", audio_in_source_enum,
        aml_ai_source_get_enum,aml_ai_source_put_enum),

	SOC_DOUBLE_R("Output Mixer DAC Switch", DAC_PLYBACK_MIXER_LSB_CTRL,
					DAC_PLYBACK_MIXER_MSB_CTRL, 0, 1, 0),
	SOC_DOUBLE_R("Output Mixer Bypass Switch", DAC_PLYBACK_MIXER_LSB_CTRL,
					DAC_PLYBACK_MIXER_MSB_CTRL, 1, 1, 0),
	SOC_SINGLE("Left Right Line Out Mute",ADAC_MUTE_CTRL0,0,0xff,0)
};

static int aml_syno9629_write(struct snd_soc_codec *codec, unsigned int reg,
							unsigned int value)
{
       u16 *reg_cache = codec->reg_cache;
	CODEC_DEBUG("***Entered %s:%s:\nWriting reg is %#x; value=%#x\n",__FILE__,__func__, reg, value);
	if (reg >= codec->reg_size/sizeof(u16))
		return -EINVAL;
	WRITE_APB_REG((APB_BASE+(reg<<2)), value);
	reg_cache[reg] = value;
	latch();

      //CODEC_DEBUG("Read back reg is %#x value=%#x\n", reg, (READ_APB_REG(APB_BASE+(reg<<2))));

	return 0;
}

static unsigned int aml_syno9629_read(struct snd_soc_codec *codec,
							unsigned int reg)
{
	//u16 *reg_cache = codec->reg_cache;
	if (reg >= codec->reg_size/sizeof(u16))
		return -EINVAL;

	return READ_APB_REG((APB_BASE+(reg<<2)));
	//return reg_cache[reg];
}

static int aml_syno9629_codec_hw_params(struct snd_pcm_substream *substream,
			    struct snd_pcm_hw_params *params,
			    struct snd_soc_dai *dai)
{
	struct snd_soc_codec *codec = dai->codec;
	unsigned long rate = params_rate(params);
	int rate_idx = 0;

	for (rate_idx = 0; rate_idx < ARRAY_SIZE(aml_rate_table); rate_idx++){
		if (aml_rate_table[rate_idx] == rate)
			break;
	}
	if (ARRAY_SIZE(aml_rate_table) == rate_idx){
		printk(" sample rate not supported by codec \n");
		rate_idx = 0x8; //48k
	}
	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK)
		snd_soc_write(codec, DAC_I2S_FS_SEL, rate_idx);
	else
		snd_soc_write(codec, ADC_I2S_FS_SEL, rate_idx);
//	aml_reset_path(codec, AML_PWR_KEEP);
	return 0;
}


static int aml_syno9629_codec_pcm_prepare(struct snd_pcm_substream *substream,
			      struct snd_soc_dai *dai)
{
	CODEC_DEBUG( "enter %s\n", __func__);

	//struct snd_soc_codec *codec = dai->codec;
	/* set active */

	// TODO

	return 0;
}

static void aml_syno9629_codec_shutdown(struct snd_pcm_substream *substream,
			    struct snd_soc_dai *dai)
{
	struct snd_soc_codec *codec = dai->codec;
	/* deactivate */
	if (!codec->active) {
		udelay(50);

		// TODO
	}
}

static int aml_syno9629_codec_mute(struct snd_soc_dai *dai, int mute)
{
	struct snd_soc_codec *codec = dai->codec;
	u16 reg;
	// TODO

	reg = snd_soc_read(codec, ADAC_MUTE_CTRL4);
	if(mute){
		reg |= 3<<2;
	}
	else{
		reg &= ~(3<<2);
	}
	CODEC_DEBUG("aml_syno9629_codec_mute mute=%d\n",mute);
	snd_soc_write(codec, ADAC_MUTE_CTRL4, reg);
	return 0;
}

static int aml_syno9629_codec_set_dai_sysclk(struct snd_soc_dai *codec_dai,
		int clk_id, unsigned int freq, int dir)
{
	unsigned long data = 0;

	switch (freq) {
	case 32000:
		data = 6;
		break;
	case 44100:
		data = 7;
		break;
	case 48000:
		data = 8;
		break;
	case 96000:
		data = 10;
		break;
	default:
		data = 6;
		break;
	}
	//snd_soc_write(codec,ADAC_CLOCK, 0);
	//snd_soc_write(codec,ADAC_I2S_CONFIG_REG1, data);
	return 0;
}


static int aml_syno9629_codec_set_dai_fmt(struct snd_soc_dai *codec_dai,
		unsigned int fmt)
{
	u16 iface = 0;
	/* set master/slave audio interface */
	switch (fmt & SND_SOC_DAIFMT_MASTER_MASK) {
	case SND_SOC_DAIFMT_CBM_CFM:
		iface |= 0x0040;
		break;
	case SND_SOC_DAIFMT_CBS_CFS:
		break;
	default:
		return -EINVAL;
	}

	/* interface format */
	switch (fmt & SND_SOC_DAIFMT_FORMAT_MASK) {
	case SND_SOC_DAIFMT_I2S:
		iface |= 0x0002;
		break;
	case SND_SOC_DAIFMT_RIGHT_J:
		break;
	case SND_SOC_DAIFMT_LEFT_J:
		iface |= 0x0001;
		break;
	case SND_SOC_DAIFMT_DSP_A:
		iface |= 0x0003;
		break;
	case SND_SOC_DAIFMT_DSP_B:
		iface |= 0x0013;
		break;
	default:
		return -EINVAL;
	}

	/* clock inversion */
	switch (fmt & SND_SOC_DAIFMT_INV_MASK) {
	case SND_SOC_DAIFMT_NB_NF:
		break;
	case SND_SOC_DAIFMT_IB_IF:
		iface |= 0x0090;
		break;
	case SND_SOC_DAIFMT_IB_NF:
		iface |= 0x0080;
		break;
	case SND_SOC_DAIFMT_NB_IF:
		iface |= 0x0010;
		break;
	default:
		return -EINVAL;
	}

	/* set iface */

	// TODO

	return 0;
}

#define AML_RATES SNDRV_PCM_RATE_8000_96000

#define AML_FORMATS (SNDRV_PCM_FMTBIT_S16_LE | \
	SNDRV_PCM_FMTBIT_S24_LE)


static struct snd_soc_dai_ops aml_syno9629_codec_dai_ops = {
	.prepare	= aml_syno9629_codec_pcm_prepare,
	.hw_params	= aml_syno9629_codec_hw_params,
	.shutdown	= aml_syno9629_codec_shutdown,
	.digital_mute	= aml_syno9629_codec_mute,
	.set_sysclk	= aml_syno9629_codec_set_dai_sysclk,
	.set_fmt	= aml_syno9629_codec_set_dai_fmt,
};

struct snd_soc_dai_driver aml_syno9629_codec_dai = {
	.name = "syno9629-hifi",
	.playback = {
		.stream_name = "Playback",
		.channels_min = 1,
		.channels_max = 2,
		.rates = AML_RATES,
		.formats = AML_FORMATS,},
	.capture = {
		.stream_name = "Capture",
		.channels_min = 1,
		.channels_max = 2,
		.rates = AML_RATES,
		.formats = AML_FORMATS,},
	.ops = &aml_syno9629_codec_dai_ops,
	.symmetric_rates = 1,
};
EXPORT_SYMBOL_GPL(aml_syno9629_codec_dai);

static int aml_syno9629_set_bias_level(struct snd_soc_codec *codec,
				 enum snd_soc_bias_level level)
{
	switch (level) {
	case SND_SOC_BIAS_ON:
		break;
	case SND_SOC_BIAS_PREPARE:
		break;
	case SND_SOC_BIAS_STANDBY:
		break;
	case SND_SOC_BIAS_OFF:
	    break;
	default:
	    break;
	}
	codec->dapm.bias_level = level;
	return 0;
}

static int aml_syno9629_soc_probe(struct snd_soc_codec *codec){
	aml_syno9629_reset(codec, true);
	aml_syno9629_set_bias_level(codec, SND_SOC_BIAS_STANDBY);

	snd_soc_add_codec_controls(codec, amlsyno9629_snd_controls,
				ARRAY_SIZE(amlsyno9629_snd_controls));
#if 0
	snd_soc_dapm_new_controls(dapm, aml_syno9629_dapm_widgets,
				  ARRAY_SIZE(aml_syno9629_dapm_widgets));

	snd_soc_dapm_add_routes(dapm, aml_syno9629_audio_map, ARRAY_SIZE(aml_syno9629_audio_map));
#endif
      aml_syno9629_codec = codec;
    return 0;
}
static int aml_syno9629_soc_remove(struct snd_soc_codec *codec){
	return 0;
}
static int aml_syno9629_soc_suspend(struct snd_soc_codec *codec){
	CODEC_DEBUG( "enter %s\n", __func__);
	WRITE_MPEG_REG( HHI_GCLK_MPEG1, READ_MPEG_REG(HHI_GCLK_MPEG1)&~(1 << 2));
	aml_reset_path(codec, AML_PWR_DOWN);
	return 0;
}

static int aml_syno9629_soc_resume(struct snd_soc_codec *codec){
	CODEC_DEBUG( "enter %s\n", __func__);

	WRITE_MPEG_REG( HHI_GCLK_MPEG1, READ_MPEG_REG(HHI_GCLK_MPEG1)|(1 << 2));
	aml_syno9629_reset(codec, true);
	return 0;
}

static struct snd_soc_codec_driver soc_codec_dev_syno9629 = {
	.probe = 	aml_syno9629_soc_probe,
	.remove = 	aml_syno9629_soc_remove,
	.suspend =	aml_syno9629_soc_suspend,
	.resume = 	aml_syno9629_soc_resume,
	.read = aml_syno9629_read,
	.write = aml_syno9629_write,
	.set_bias_level = aml_syno9629_set_bias_level,
	.reg_cache_size = ARRAY_SIZE(aml_syno9629_reg),
	.reg_word_size = sizeof(u16),
	.reg_cache_step = 1,
	.reg_cache_default = aml_syno9629_reg,
	.dapm_widgets = 0,//aml_syno9629_dapm_widgets,
	.num_dapm_widgets = 0,//ARRAY_SIZE(aml_syno9629_dapm_widgets),
	.dapm_routes =  0,//aml_syno9629_audio_map,
	.num_dapm_routes = 0,//ARRAY_SIZE(aml_syno9629_audio_map),
};

static int aml_syno9629_codec_platform_probe(struct platform_device *pdev)
{
	CODEC_DEBUG( "enter %s\n", __func__);
	return snd_soc_register_codec(&pdev->dev,
		&soc_codec_dev_syno9629, &aml_syno9629_codec_dai, 1);
}

static int __exit aml_syno9629_codec_platform_remove(struct platform_device *pdev)
{
	snd_soc_unregister_codec(&pdev->dev);
	return 0;
}

#ifdef CONFIG_USE_OF
static const struct of_device_id amlogic_audio_codec_dt_match[]={
    { .compatible = "amlogic,syno9629", },
    {},
};
#else
#define amlogic_audio_dt_match NULL
#endif

static struct platform_driver aml_syno9629_codec_platform_driver = {
	.driver = {
		.name = "syno9629",
		.owner = THIS_MODULE,
		.of_match_table = amlogic_audio_codec_dt_match,
	},
	.probe = aml_syno9629_codec_platform_probe,
	.remove = __exit_p(aml_syno9629_codec_platform_remove),
};

static int __init aml_syno9629_codec_modinit(void)
{
	CODEC_DEBUG( "enter %s\n", __func__);

	return platform_driver_register(&aml_syno9629_codec_platform_driver);
}

static void __exit aml_syno9629_codec_exit(void)
{
		platform_driver_unregister(&aml_syno9629_codec_platform_driver);
}

module_init(aml_syno9629_codec_modinit);
module_exit(aml_syno9629_codec_exit);


MODULE_DESCRIPTION("ASoC AML synopsys 9629  codec driver");
MODULE_AUTHOR("jian.xu@amlogic.com AMLogic Inc.");
MODULE_LICENSE("GPL");
