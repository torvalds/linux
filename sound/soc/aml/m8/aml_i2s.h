#ifndef __AML_I2S_H__
#define __AML_I2S_H__

//#define debug_printk
#ifdef debug_printk
#define dug_printk(fmt, args...)  printk (fmt, ## args)
#else
#define dug_printk(fmt, args...)
#endif

typedef struct audio_stream {
    int stream_id;
    int active;
    unsigned int last_ptr;
    unsigned int size;
    unsigned int sample_rate;
    unsigned int I2S_addr;
    spinlock_t lock;
    struct snd_pcm_substream *stream;
	unsigned i2s_mode; //0:master, 1:slave,
    unsigned device_type;
    unsigned int xrun_num;
} audio_stream_t;

typedef struct aml_audio {
    struct snd_card *card;
    struct snd_pcm *pcm;
    audio_stream_t s[2];
} aml_audio_t;

typedef struct aml_audio_buffer {
    void *buffer_start;
    unsigned int buffer_size;
} aml_audio_buffer_t;

typedef struct audio_mixer_control {
    int output_devide;
    int input_device;
    int direction;
    int input_volume;
    int output_volume;
} audio_mixer_control_t;

typedef struct audio_tone_control {
    unsigned short * tone_source;
    unsigned short * tone_data;
    int tone_data_len;
    int tone_count;
    int tone_flag;
}audio_tone_control_t;

struct aml_i2s_dma_params{
		char *name;			/* stream identifier */
		struct snd_pcm_substream *substream;
		void (*dma_intr_handler)(u32, struct snd_pcm_substream *);
	
};
typedef struct aml_dai_info {
	unsigned i2s_mode; //0:master, 1:slave,
} aml_dai_info_t;
enum {
	I2S_MASTER_MODE = 0,
	I2S_SLAVE_MODE,	
};
/*--------------------------------------------------------------------------*\
 * Data types
\*--------------------------------------------------------------------------*/
struct aml_runtime_data {
	struct aml_i2s_dma_params *params;
	dma_addr_t dma_buffer;		/* physical address of dma buffer */
	dma_addr_t dma_buffer_end;	/* first address beyond DMA buffer */

	struct snd_pcm *pcm;
	struct snd_pcm_substream *substream;
	audio_stream_t s;	
	struct timer_list timer;	// timeer for playback and capture
	struct hrtimer hrtimer;
	void *buf; //tmp buffer for playback or capture
};

extern struct snd_soc_platform_driver aml_soc_platform;
//extern struct aml_audio_interface aml_i2s_interface;

#endif
