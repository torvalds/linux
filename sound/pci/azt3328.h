#ifndef __SOUND_AZT3328_H
#define __SOUND_AZT3328_H

/* "PU" == "power-up value", as tested on PCI168 PCI rev. 10 */

/*** main I/O area port indices ***/
/* (only 0x70 of 0x80 bytes saved/restored by Windows driver) */
#define AZF_IO_SIZE_CODEC	0x80
#define AZF_IO_SIZE_CODEC_PM	0x70

/* the driver initialisation suggests a layout of 4 main areas:
 * from 0x00 (playback), from 0x20 (recording) and from 0x40 (maybe MPU401??).
 * And another area from 0x60 to 0x6f (DirectX timer, IRQ management,
 * power management etc.???). */

/** playback area **/
#define IDX_IO_PLAY_FLAGS       0x00 /* PU:0x0000 */
     /* able to reactivate output after output muting due to 8/16bit
      * output change, just like 0x0002.
      * 0x0001 is the only bit that's able to start the DMA counter */
  #define DMA_RESUME			0x0001 /* paused if cleared ? */
     /* 0x0002 *temporarily* set during DMA stopping. hmm
      * both 0x0002 and 0x0004 set in playback setup. */
     /* able to reactivate output after output muting due to 8/16bit
      * output change, just like 0x0001. */
  #define DMA_PLAY_SOMETHING1		0x0002 /* \ alternated (toggled) */
     /* 0x0004: NOT able to reactivate output */
  #define DMA_PLAY_SOMETHING2		0x0004 /* / bits */
  #define SOMETHING_ALMOST_ALWAYS_SET	0x0008 /* ???; can be modified */
  #define DMA_EPILOGUE_SOMETHING	0x0010
  #define DMA_SOMETHING_ELSE		0x0020 /* ??? */
  #define SOMETHING_UNMODIFIABLE	0xffc0 /* unused ? not modifiable */
#define IDX_IO_PLAY_IRQTYPE     0x02 /* PU:0x0001 */
  /* write back to flags in case flags are set, in order to ACK IRQ in handler
   * (bit 1 of port 0x64 indicates interrupt for one of these three types)
   * sometimes in this case it just writes 0xffff to globally ACK all IRQs
   * settings written are not reflected when reading back, though.
   * seems to be IRQ, too (frequently used: port |= 0x07 !), but who knows ? */
  #define IRQ_PLAY_SOMETHING		0x0001 /* something & ACK */
  #define IRQ_FINISHED_PLAYBUF_1	0x0002 /* 1st dmabuf finished & ACK */
  #define IRQ_FINISHED_PLAYBUF_2	0x0004 /* 2nd dmabuf finished & ACK */
  #define IRQMASK_SOME_STATUS_1		0x0008 /* \ related bits */
  #define IRQMASK_SOME_STATUS_2		0x0010 /* / (checked together in loop) */
  #define IRQMASK_UNMODIFIABLE		0xffe0 /* unused ? not modifiable */
#define IDX_IO_PLAY_DMA_START_1 0x04 /* start address of 1st DMA play area, PU:0x00000000 */
#define IDX_IO_PLAY_DMA_START_2 0x08 /* start address of 2nd DMA play area, PU:0x00000000 */
#define IDX_IO_PLAY_DMA_LEN_1   0x0c /* length of 1st DMA play area, PU:0x0000 */
#define IDX_IO_PLAY_DMA_LEN_2   0x0e /* length of 2nd DMA play area, PU:0x0000 */
#define IDX_IO_PLAY_DMA_CURRPOS 0x10 /* current DMA position, PU:0x00000000 */
#define IDX_IO_PLAY_DMA_CURROFS	0x14 /* offset within current DMA play area, PU:0x0000 */
#define IDX_IO_PLAY_SOUNDFORMAT 0x16 /* PU:0x0010 */
  /* all unspecified bits can't be modified */
  #define SOUNDFORMAT_FREQUENCY_MASK	0x000f
  #define SOUNDFORMAT_XTAL1		0x00
  #define SOUNDFORMAT_XTAL2		0x01
    /* all _SUSPECTED_ values are not used by Windows drivers, so we don't
     * have any hard facts, only rough measurements */
    #define SOUNDFORMAT_FREQ_SUSPECTED_4000	0x0c | SOUNDFORMAT_XTAL1
    #define SOUNDFORMAT_FREQ_SUSPECTED_4800	0x0a | SOUNDFORMAT_XTAL1
    #define SOUNDFORMAT_FREQ_5510		0x0c | SOUNDFORMAT_XTAL2
    #define SOUNDFORMAT_FREQ_6620		0x0a | SOUNDFORMAT_XTAL2
    #define SOUNDFORMAT_FREQ_8000		0x00 | SOUNDFORMAT_XTAL1 /* also 0x0e | SOUNDFORMAT_XTAL1? */
    #define SOUNDFORMAT_FREQ_9600		0x08 | SOUNDFORMAT_XTAL1
    #define SOUNDFORMAT_FREQ_11025		0x00 | SOUNDFORMAT_XTAL2 /* also 0x0e | SOUNDFORMAT_XTAL2? */
    #define SOUNDFORMAT_FREQ_SUSPECTED_13240	0x08 | SOUNDFORMAT_XTAL2 /* seems to be 6620 *2 */
    #define SOUNDFORMAT_FREQ_16000		0x02 | SOUNDFORMAT_XTAL1
    #define SOUNDFORMAT_FREQ_22050		0x02 | SOUNDFORMAT_XTAL2
    #define SOUNDFORMAT_FREQ_32000		0x04 | SOUNDFORMAT_XTAL1
    #define SOUNDFORMAT_FREQ_44100		0x04 | SOUNDFORMAT_XTAL2
    #define SOUNDFORMAT_FREQ_48000		0x06 | SOUNDFORMAT_XTAL1
    #define SOUNDFORMAT_FREQ_SUSPECTED_66200	0x06 | SOUNDFORMAT_XTAL2 /* 66200 (13240 * 5); 64000 may have been nicer :-\ */
  #define SOUNDFORMAT_FLAG_16BIT	0x0010
  #define SOUNDFORMAT_FLAG_2CHANNELS	0x0020

/** recording area (see also: playback bit flag definitions) **/
#define IDX_IO_REC_FLAGS	0x20 /* ??, PU:0x0000 */
#define IDX_IO_REC_IRQTYPE	0x22 /* ??, PU:0x0000 */
  #define IRQ_REC_SOMETHING		0x0001 /* something & ACK */
  #define IRQ_FINISHED_RECBUF_1		0x0002 /* 1st dmabuf finished & ACK */
  #define IRQ_FINISHED_RECBUF_2		0x0004 /* 2nd dmabuf finished & ACK */
  /* hmm, maybe these are just the corresponding *recording* flags ?
   * but OTOH they are most likely at port 0x22 instead */
  #define IRQMASK_SOME_STATUS_1		0x0008 /* \ related bits */
  #define IRQMASK_SOME_STATUS_2		0x0010 /* / (checked together in loop) */
#define IDX_IO_REC_DMA_START_1  0x24 /* PU:0x00000000 */
#define IDX_IO_REC_DMA_START_2  0x28 /* PU:0x00000000 */
#define IDX_IO_REC_DMA_LEN_1    0x2c /* PU:0x0000 */
#define IDX_IO_REC_DMA_LEN_2    0x2e /* PU:0x0000 */
#define IDX_IO_REC_DMA_CURRPOS  0x30 /* PU:0x00000000 */
#define IDX_IO_REC_DMA_CURROFS  0x34 /* PU:0x00000000 */
#define IDX_IO_REC_SOUNDFORMAT  0x36 /* PU:0x0000 */

/** hmm, what is this I/O area for? MPU401?? or external DAC via I2S?? (after playback, recording, ???, timer) **/
#define IDX_IO_SOMETHING_FLAGS	0x40 /* gets set to 0x34 just like port 0x0 and 0x20 on card init, PU:0x0000 */
/* general */
#define IDX_IO_42H		0x42 /* PU:0x0001 */

/** DirectX timer, main interrupt area (FIXME: and something else?) **/ 
#define IDX_IO_TIMER_VALUE	0x60 /* found this timer area by pure luck :-) */
  #define TIMER_VALUE_MASK		0x000fffffUL /* timer countdown value; triggers IRQ when timer is finished */
  #define TIMER_ENABLE_COUNTDOWN	0x01000000UL /* activate the timer countdown */
  #define TIMER_ENABLE_IRQ		0x02000000UL /* trigger timer IRQ on zero transition */
  #define TIMER_ACK_IRQ			0x04000000UL /* being set in IRQ handler in case port 0x00 (hmm, not port 0x64!?!?) had 0x0020 set upon IRQ handler */
#define IDX_IO_IRQSTATUS        0x64
  #define IRQ_PLAYBACK			0x0001
  #define IRQ_RECORDING			0x0002
  #define IRQ_MPU401			0x0010
  #define IRQ_TIMER			0x0020 /* DirectX timer */
  #define IRQ_UNKNOWN1			0x0040 /* probably unused, or possibly I2S port? or gameport IRQ? */
  #define IRQ_UNKNOWN2			0x0080 /* probably unused, or possibly I2S port? or gameport IRQ? */
#define IDX_IO_66H		0x66    /* writing 0xffff returns 0x0000 */
#define IDX_IO_SOME_VALUE	0x68	/* this is set to e.g. 0x3ff or 0x300, and writable; maybe some buffer limit, but I couldn't find out more, PU:0x00ff */
#define IDX_IO_6AH		0x6A	/* this WORD can be set to have bits 0x0028 activated (FIXME: correct??); actually inhibits PCM playback!!! maybe power management?? */
  #define IO_6A_PAUSE_PLAYBACK		0x0200 /* bit 9; sure, this pauses playback, but what the heck is this really about?? */
#define IDX_IO_6CH		0x6C
#define IDX_IO_6EH		0x6E	/* writing 0xffff returns 0x83fe */
/* further I/O indices not saved/restored, so probably not used */


/*** I/O 2 area port indices ***/
/* (only 0x06 of 0x08 bytes saved/restored by Windows driver) */ 
#define AZF_IO_SIZE_IO2		0x08
#define AZF_IO_SIZE_IO2_PM	0x06

#define IDX_IO2_LEGACY_ADDR	0x04
  #define LEGACY_SOMETHING		0x01 /* OPL3?? */
  #define LEGACY_JOY			0x08

#define AZF_IO_SIZE_MPU		0x04
#define AZF_IO_SIZE_MPU_PM	0x04

#define AZF_IO_SIZE_SYNTH	0x08
#define AZF_IO_SIZE_SYNTH_PM	0x06

/*** mixer I/O area port indices ***/
/* (only 0x22 of 0x40 bytes saved/restored by Windows driver)
 * UNFORTUNATELY azf3328 is NOT truly AC97 compliant: see main file intro */
#define AZF_IO_SIZE_MIXER	0x40
#define AZF_IO_SIZE_MIXER_PM	0x22

  #define MIXER_VOLUME_RIGHT_MASK	0x001f
  #define MIXER_VOLUME_LEFT_MASK	0x1f00
  #define MIXER_MUTE_MASK		0x8000
#define IDX_MIXER_RESET		0x00 /* does NOT seem to have AC97 ID bits */
#define IDX_MIXER_PLAY_MASTER   0x02
#define IDX_MIXER_MODEMOUT      0x04
#define IDX_MIXER_BASSTREBLE    0x06
  #define MIXER_BASSTREBLE_TREBLE_VOLUME_MASK	0x000e
  #define MIXER_BASSTREBLE_BASS_VOLUME_MASK	0x0e00
#define IDX_MIXER_PCBEEP        0x08
#define IDX_MIXER_MODEMIN       0x0a
#define IDX_MIXER_MIC           0x0c
  #define MIXER_MIC_MICGAIN_20DB_ENHANCEMENT_MASK	0x0040
#define IDX_MIXER_LINEIN        0x0e
#define IDX_MIXER_CDAUDIO       0x10
#define IDX_MIXER_VIDEO         0x12
#define IDX_MIXER_AUX           0x14
#define IDX_MIXER_WAVEOUT       0x16
#define IDX_MIXER_FMSYNTH       0x18
#define IDX_MIXER_REC_SELECT    0x1a
  #define MIXER_REC_SELECT_MIC		0x00
  #define MIXER_REC_SELECT_CD		0x01
  #define MIXER_REC_SELECT_VIDEO	0x02
  #define MIXER_REC_SELECT_AUX		0x03
  #define MIXER_REC_SELECT_LINEIN	0x04
  #define MIXER_REC_SELECT_MIXSTEREO	0x05
  #define MIXER_REC_SELECT_MIXMONO	0x06
  #define MIXER_REC_SELECT_MONOIN	0x07
#define IDX_MIXER_REC_VOLUME    0x1c
#define IDX_MIXER_ADVCTL1       0x1e
  /* unlisted bits are unmodifiable */
  #define MIXER_ADVCTL1_3DWIDTH_MASK	0x000e
  #define MIXER_ADVCTL1_HIFI3D_MASK	0x0300 /* yup, this is missing the high bit that official AC97 contains, plus it doesn't have linear bit value range behaviour but instead acts weirdly (possibly we're dealing with two *different* 3D settings here??) */
#define IDX_MIXER_ADVCTL2       0x20 /* subset of AC97_GENERAL_PURPOSE reg! */
  /* unlisted bits are unmodifiable */
  #define MIXER_ADVCTL2_LPBK		0x0080 /* Loopback mode -- Win driver: "WaveOut3DBypass"? mutes WaveOut at LineOut */
  #define MIXER_ADVCTL2_MS		0x0100 /* Mic Select 0=Mic1, 1=Mic2 -- Win driver: "ModemOutSelect"?? */
  #define MIXER_ADVCTL2_MIX		0x0200 /* Mono output select 0=Mix, 1=Mic; Win driver: "MonoSelectSource"?? */
  #define MIXER_ADVCTL2_3D		0x2000 /* 3D Enhancement 1=on */
  #define MIXER_ADVCTL2_POP		0x8000 /* Pcm Out Path, 0=pre 3D, 1=post 3D */
  
#define IDX_MIXER_SOMETHING30H	0x30 /* used, but unknown??? */

/* driver internal flags */
#define SET_CHAN_LEFT	1
#define SET_CHAN_RIGHT	2

#endif /* __SOUND_AZT3328_H  */
