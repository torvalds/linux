#ifndef __AML_AUDIO_HW_H__
#define __AML_AUDIO_HW_H__
#include <mach/power_gate.h>
#if MESON_CPU_TYPE < MESON_CPU_TYPE_MESON6
#define AUDIO_CLK_GATE_ON(a)
#define AUDIO_CLK_GATE_OFF(a)
#else
#define AUDIO_CLK_GATE_ON(a) CLK_GATE_ON(a)
#define AUDIO_CLK_GATE_OFF(a) CLK_GATE_OFF(a)
#endif

typedef struct {
    unsigned short pll;
    unsigned short mux;
    unsigned short devisor;
} _aiu_clk_setting_t;

typedef struct {
    unsigned short chstat0_l;
    unsigned short chstat1_l;
    unsigned short chstat0_r;
    unsigned short chstat1_r;
} _aiu_958_channel_status_t;

typedef struct {
    /* audio clock */
    unsigned short clock;
    /* analog output */
    unsigned short i2s_mode;
    unsigned short i2s_dac_mode;
    unsigned short i2s_preemphsis;
    /* digital output */
    unsigned short i958_buf_start_addr;
    unsigned short i958_buf_blksize;
    unsigned short i958_int_flag;
    unsigned short i958_mode;
    unsigned short i958_sync_mode;
    unsigned short i958_preemphsis;
    unsigned short i958_copyright;
    unsigned short bpf;
    unsigned short brst;
    unsigned short length;
    unsigned short paddsize;
    _aiu_958_channel_status_t chan_status;
} audio_output_config_t;

typedef struct {
    unsigned short int_flag;
    unsigned short bpf;
    unsigned short brst;
    unsigned short length;
    unsigned short paddsize;
    _aiu_958_channel_status_t *chan_stat;
} _aiu_958_raw_setting_t;

enum {
	I2SIN_MASTER_MODE = 0,
	I2SIN_SLAVE_MODE  =   1<<0,
	SPDIFIN_MODE   = 1<<1,
};
enum {
	AML_AUDIO_NA = 0,	
	AML_AUDIO_SPDIFIN = 1<<0,
	AML_AUDIO_SPDIFOUT = 1<<1,
	AML_AUDIO_I2SIN = 1<<2,
	AML_AUDIO_I2SOUT = 1<<3,
	AML_AUDIO_PCMIN = 1<<4,
	AML_AUDIO_PCMOUT = 1<<5,				
};

#define AUDIO_CLK_256FS             0
#define AUDIO_CLK_384FS             1

#define AUDIO_CLK_FREQ_192  0
#define AUDIO_CLK_FREQ_1764 1
#define AUDIO_CLK_FREQ_96   2
#define AUDIO_CLK_FREQ_882  3
#define AUDIO_CLK_FREQ_48   4
#define AUDIO_CLK_FREQ_441  5
#define AUDIO_CLK_FREQ_32   6

#define AUDIO_CLK_FREQ_8		7
#define AUDIO_CLK_FREQ_11		8
#define AUDIO_CLK_FREQ_12		9
#define AUDIO_CLK_FREQ_16		10
#define AUDIO_CLK_FREQ_22		11
#define AUDIO_CLK_FREQ_24		12


#define AIU_958_MODE_RAW    0
#define AIU_958_MODE_PCM16  1
#define AIU_958_MODE_PCM24  2
#define AIU_958_MODE_PCM32  3
#define AIU_958_MODE_PCM_RAW  4

#define AIU_I2S_MODE_PCM16   0
#define AIU_I2S_MODE_PCM24   2
#define AIU_I2S_MODE_PCM32   3

#define AUDIO_ALGOUT_DAC_FORMAT_DSP             0
#define AUDIO_ALGOUT_DAC_FORMAT_LEFT_JUSTIFY    1

extern unsigned ENABLE_IEC958;
extern unsigned IEC958_MODE;
extern unsigned I2S_MODE;

void audio_set_aiubuf(u32 addr, u32 size, unsigned int channel);
void audio_set_958outbuf(u32 addr, u32 size, int channels, int flag);
void audio_in_i2s_set_buf(u32 addr, u32 size,u32 i2s_mode, u32 i2s_sync);
void audio_in_spdif_set_buf(u32 addr, u32 size);
void audio_in_i2s_enable(int flag);
void audio_in_spdif_enable(int flag);
unsigned int audio_in_i2s_rd_ptr(void);
unsigned int audio_in_i2s_wr_ptr(void);
unsigned int audio_in_spdif_wr_ptr(void);
void audio_set_i2s_mode(u32 mode);
void audio_set_i2s_clk(unsigned freq, unsigned fs_config, unsigned mpll);
void audio_set_958_clk(unsigned freq, unsigned fs_config);
void audio_enable_ouput(int flag);
unsigned int read_i2s_rd_ptr(void);
void audio_i2s_unmute(void);
void audio_i2s_mute(void);
void aml_audio_i2s_unmute(void);
void aml_audio_i2s_mute(void);
void audio_util_set_dac_format(unsigned format);
void audio_util_set_dac_i2s_format(unsigned format);
void audio_util_set_dac_958_format(unsigned format);
void audio_set_958_mode(unsigned mode, _aiu_958_raw_setting_t * set);
unsigned int read_i2s_mute_swap_reg(void);
void audio_i2s_swap_left_right(unsigned int flag);
int if_audio_out_enable(void);
int if_audio_in_i2s_enable(void);
int if_audio_in_spdif_enable(void);
void audio_out_i2s_enable(unsigned flag);
void audio_hw_958_enable(unsigned flag);
void audio_out_enabled(int flag);
void audio_util_set_dac_format(unsigned format);
unsigned int audio_hdmi_init_ready(void);
unsigned int read_iec958_rd_ptr(void);
void audio_in_spdif_enable(int flag);
unsigned audio_spdifout_pg_enable(unsigned char enable);
unsigned audio_aiu_pg_enable(unsigned char enable);

#include "mach/cpu.h"

/*OVERCLOCK == 1,our SOC privide 512fs mclk,OVERCLOCK == 0 ,256fs*/
#if MESON_CPU_TYPE >= MESON_CPU_TYPE_MESON6TV
#define OVERCLOCK 0
#define IEC958_OVERCLOCK 1
#else
#define OVERCLOCK 1
#endif

#if (OVERCLOCK == 1)
#define MCLKFS_RATIO 512
#else
#define MCLKFS_RATIO 256
#endif

#define I2S_PLL_SRC         1   // MPLL0
#define MPLL_I2S_CNTL		HHI_MPLL_CNTL7  

#define I958_PLL_SRC        2   // MPLL1
#define MPLL_958_CNTL		HHI_MPLL_CNTL8


#endif
