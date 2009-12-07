#ifndef __SOUND_AZT3328_H
#define __SOUND_AZT3328_H

/* "PU" == "power-up value", as tested on PCI168 PCI rev. 10
 * "WRITE_ONLY"  == register does not indicate actual bit values */

/*** main I/O area port indices ***/
/* (only 0x70 of 0x80 bytes saved/restored by Windows driver) */
#define AZF_IO_SIZE_CTRL	0x80
#define AZF_IO_SIZE_CTRL_PM	0x70

/* the driver initialisation suggests a layout of 4 areas
 * within the main card control I/O:
 * from 0x00 (playback codec), from 0x20 (recording codec)
 * and from 0x40 (most certainly I2S out codec).
 * And another area from 0x60 to 0x6f (DirectX timer, IRQ management,
 * power management etc.???). */

#define AZF_IO_OFFS_CODEC_PLAYBACK	0x00
#define AZF_IO_OFFS_CODEC_CAPTURE	0x20
#define AZF_IO_OFFS_CODEC_I2S_OUT	0x40

#define IDX_IO_CODEC_DMA_FLAGS       0x00 /* PU:0x0000 */
     /* able to reactivate output after output muting due to 8/16bit
      * output change, just like 0x0002.
      * 0x0001 is the only bit that's able to start the DMA counter */
  #define DMA_RESUME			0x0001 /* paused if cleared? */
     /* 0x0002 *temporarily* set during DMA stopping. hmm
      * both 0x0002 and 0x0004 set in playback setup. */
     /* able to reactivate output after output muting due to 8/16bit
      * output change, just like 0x0001. */
  #define DMA_RUN_SOMETHING1		0x0002 /* \ alternated (toggled) */
     /* 0x0004: NOT able to reactivate output */
  #define DMA_RUN_SOMETHING2		0x0004 /* / bits */
  #define SOMETHING_ALMOST_ALWAYS_SET	0x0008 /* ???; can be modified */
  #define DMA_EPILOGUE_SOMETHING	0x0010
  #define DMA_SOMETHING_ELSE		0x0020 /* ??? */
  #define SOMETHING_UNMODIFIABLE	0xffc0 /* unused? not modifiable */
#define IDX_IO_CODEC_IRQTYPE     0x02 /* PU:0x0001 */
  /* write back to flags in case flags are set, in order to ACK IRQ in handler
   * (bit 1 of port 0x64 indicates interrupt for one of these three types)
   * sometimes in this case it just writes 0xffff to globally ACK all IRQs
   * settings written are not reflected when reading back, though.
   * seems to be IRQ, too (frequently used: port |= 0x07 !), but who knows? */
  #define IRQ_SOMETHING			0x0001 /* something & ACK */
  #define IRQ_FINISHED_DMABUF_1		0x0002 /* 1st dmabuf finished & ACK */
  #define IRQ_FINISHED_DMABUF_2		0x0004 /* 2nd dmabuf finished & ACK */
  #define IRQMASK_SOME_STATUS_1		0x0008 /* \ related bits */
  #define IRQMASK_SOME_STATUS_2		0x0010 /* / (checked together in loop) */
  #define IRQMASK_UNMODIFIABLE		0xffe0 /* unused? not modifiable */
  /* start address of 1st DMA transfer area, PU:0x00000000 */
#define IDX_IO_CODEC_DMA_START_1 0x04
  /* start address of 2nd DMA transfer area, PU:0x00000000 */
#define IDX_IO_CODEC_DMA_START_2 0x08
  /* both lengths of DMA transfer areas, PU:0x00000000
     length1: offset 0x0c, length2: offset 0x0e */
#define IDX_IO_CODEC_DMA_LENGTHS 0x0c
#define IDX_IO_CODEC_DMA_CURRPOS 0x10 /* current DMA position, PU:0x00000000 */
  /* offset within current DMA transfer area, PU:0x0000 */
#define IDX_IO_CODEC_DMA_CURROFS 0x14
#define IDX_IO_CODEC_SOUNDFORMAT 0x16 /* PU:0x0010 */
  /* all unspecified bits can't be modified */
  #define SOUNDFORMAT_FREQUENCY_MASK	0x000f
  #define SOUNDFORMAT_XTAL1		0x00
  #define SOUNDFORMAT_XTAL2		0x01
    /* all _SUSPECTED_ values are not used by Windows drivers, so we don't
     * have any hard facts, only rough measurements.
     * All we know is that the crystal used on the board has 24.576MHz,
     * like many soundcards (which results in the frequencies below when
     * using certain divider values selected by the values below) */
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


/* define frequency helpers, for maximum value safety */
enum azf_freq_t {
#define AZF_FREQ(rate) AZF_FREQ_##rate = rate
  AZF_FREQ(4000),
  AZF_FREQ(4800),
  AZF_FREQ(5512),
  AZF_FREQ(6620),
  AZF_FREQ(8000),
  AZF_FREQ(9600),
  AZF_FREQ(11025),
  AZF_FREQ(13240),
  AZF_FREQ(16000),
  AZF_FREQ(22050),
  AZF_FREQ(32000),
  AZF_FREQ(44100),
  AZF_FREQ(48000),
  AZF_FREQ(66200),
#undef AZF_FREQ
};

/** DirectX timer, main interrupt area (FIXME: and something else?) **/ 
#define IDX_IO_TIMER_VALUE	0x60 /* found this timer area by pure luck :-) */
  /* timer countdown value; triggers IRQ when timer is finished */
  #define TIMER_VALUE_MASK		0x000fffffUL
  /* activate timer countdown */
  #define TIMER_COUNTDOWN_ENABLE	0x01000000UL
  /* trigger timer IRQ on zero transition */
  #define TIMER_IRQ_ENABLE		0x02000000UL
  /* being set in IRQ handler in case port 0x00 (hmm, not port 0x64!?!?)
   * had 0x0020 set upon IRQ handler */
  #define TIMER_IRQ_ACK			0x04000000UL
#define IDX_IO_IRQSTATUS        0x64
  /* some IRQ bit in here might also be used to signal a power-management timer
   * timeout, to request shutdown of the chip (e.g. AD1815JS has such a thing).
   * OPL3 hardware contains several timers which confusingly in most cases
   * are NOT routed to an IRQ, but some designs (e.g. LM4560) DO support that,
   * so I wouldn't be surprised at all to discover that AZF3328
   * supports that thing as well... */

  #define IRQ_PLAYBACK	0x0001
  #define IRQ_RECORDING	0x0002
  #define IRQ_I2S_OUT	0x0004 /* this IS I2S, right!? (untested) */
  #define IRQ_GAMEPORT	0x0008 /* Interrupt of Digital(ly) Enhanced Game Port */
  #define IRQ_MPU401	0x0010
  #define IRQ_TIMER	0x0020 /* DirectX timer */
  #define IRQ_UNKNOWN2	0x0040 /* probably unused, or possibly OPL3 timer? */
  #define IRQ_UNKNOWN3	0x0080 /* probably unused, or possibly OPL3 timer? */
#define IDX_IO_66H		0x66    /* writing 0xffff returns 0x0000 */
  /* this is set to e.g. 0x3ff or 0x300, and writable;
   * maybe some buffer limit, but I couldn't find out more, PU:0x00ff: */
#define IDX_IO_SOME_VALUE	0x68
  #define IO_68_RANDOM_TOGGLE1	0x0100	/* toggles randomly */
  #define IO_68_RANDOM_TOGGLE2	0x0200	/* toggles randomly */
  /* umm, nope, behaviour of these bits changes depending on what we wrote
   * to 0x6b!!
   * And they change upon playback/stop, too:
   * Writing a value to 0x68 will display this exact value during playback,
   * too but when stopped it can fall back to a rather different
   * seemingly random value). Hmm, possibly this is a register which
   * has a remote shadow which needs proper device supply which only exists
   * in case playback is active? Or is this driver-induced?
   */

/* this WORD can be set to have bits 0x0028 activated (FIXME: correct??);
 * actually inhibits PCM playback!!! maybe power management??: */
#define IDX_IO_6AH		0x6A /* WRITE_ONLY! */
  /* bit 5: enabling this will activate permanent counting of bytes 2/3
   * at gameport I/O (0xb402/3) (equal values each) and cause
   * gameport legacy I/O at 0x0200 to be _DISABLED_!
   * Is this Digital Enhanced Game Port Enable??? Or maybe it's Testmode
   * for Enhanced Digital Gameport (see 4D Wave DX card): */
  #define IO_6A_SOMETHING1_GAMEPORT	0x0020
  /* bit 8; sure, this _pauses_ playback (later resumes at same spot!),
   * but what the heck is this really about??: */
  #define IO_6A_PAUSE_PLAYBACK_BIT8	0x0100
  /* bit 9; sure, this _pauses_ playback (later resumes at same spot!),
   * but what the heck is this really about??: */
  #define IO_6A_PAUSE_PLAYBACK_BIT9	0x0200
	/* BIT8 and BIT9 are _NOT_ able to affect OPL3 MIDI playback,
	 * thus it suggests influence on PCM only!!
	 * However OTOH there seems to be no bit anywhere around here
	 * which is able to disable OPL3... */
  /* bit 10: enabling this actually changes values at legacy gameport
   * I/O address (0x200); is this enabling of the Digital Enhanced Game Port???
   * Or maybe this simply switches off the NE558 circuit, since enabling this
   * still lets us evaluate button states, but not axis states */
  #define IO_6A_SOMETHING2_GAMEPORT      0x0400
	/* writing 0x0300: causes quite some crackling during
	 * PC activity such as switching windows (PCI traffic??
	 * --> FIFO/timing settings???) */
	/* writing 0x0100 plus/or 0x0200 inhibits playback */
	/* since the Windows .INF file has Flag_Enable_JoyStick and
	 * Flag_Enable_SB_DOS_Emulation directly together, it stands to reason
	 * that some other bit in this same register might be responsible
	 * for SB DOS Emulation activation (note that the file did NOT define
	 * a switch for OPL3!) */
#define IDX_IO_6CH		0x6C	/* unknown; fully read-writable */
#define IDX_IO_6EH		0x6E
	/* writing 0xffff returns 0x83fe (or 0x03fe only).
	 * writing 0x83 (and only 0x83!!) to 0x6f will cause 0x6c to switch
	 * from 0000 to ffff. */

/* further I/O indices not saved/restored and not readable after writing,
 * so probably not used */


/*** Gameport area port indices ***/
/* (only 0x06 of 0x08 bytes saved/restored by Windows driver) */ 
#define AZF_IO_SIZE_GAME		0x08
#define AZF_IO_SIZE_GAME_PM		0x06

enum {
	AZF_GAME_LEGACY_IO_PORT = 0x200
};

#define IDX_GAME_LEGACY_COMPATIBLE	0x00
	/* in some operation mode, writing anything to this port
	 * triggers an interrupt:
	 * yup, that's in case IDX_GAME_01H has one of the
	 * axis measurement bits enabled
	 * (and of course one needs to have GAME_HWCFG_IRQ_ENABLE, too) */

#define IDX_GAME_AXES_CONFIG            0x01
	/* NOTE: layout of this register awfully similar (read: "identical??")
	 * to AD1815JS.pdf (p.29) */

  /* enables axis 1 (X axis) measurement: */
  #define GAME_AXES_ENABLE_1		0x01
  /* enables axis 2 (Y axis) measurement: */
  #define GAME_AXES_ENABLE_2		0x02
  /* enables axis 3 (X axis) measurement: */
  #define GAME_AXES_ENABLE_3		0x04
  /* enables axis 4 (Y axis) measurement: */
  #define GAME_AXES_ENABLE_4		0x08
  /* selects the current axis to read the measured value of
   * (at IDX_GAME_AXIS_VALUE):
   * 00 = axis 1, 01 = axis 2, 10 = axis 3, 11 = axis 4: */
  #define GAME_AXES_READ_MASK		0x30
  /* enable to have the latch continuously accept ADC values
   * (and continuously cause interrupts in case interrupts are enabled);
   * AD1815JS.pdf says it's ~16ms interval there: */
  #define GAME_AXES_LATCH_ENABLE	0x40
  /* joystick data (measured axes) ready for reading: */
  #define GAME_AXES_SAMPLING_READY	0x80

  /* NOTE: other card specs (SiS960 and others!) state that the
   * game position latches should be frozen when reading and be freed
   * (== reset?) after reading!!!
   * Freezing most likely means disabling 0x40 (GAME_AXES_LATCH_ENABLE),
   *  but how to free the value? */
  /* An internet search for "gameport latch ADC" should provide some insight
   * into how to program such a gameport system. */

  /* writing 0xf0 to 01H once reset both counters to 0, in some special mode!?
   * yup, in case 6AH 0x20 is not enabled
   * (and 0x40 is sufficient, 0xf0 is not needed) */

#define IDX_GAME_AXIS_VALUE	0x02
	/* R: value of currently configured axis (word value!);
	 * W: trigger axis measurement */

#define IDX_GAME_HWCONFIG	0x04
	/* note: bits 4 to 7 are never set (== 0) when reading!
	 * --> reserved bits? */
  /* enables IRQ notification upon axes measurement ready: */
  #define GAME_HWCFG_IRQ_ENABLE			0x01
  /* these bits choose a different frequency for the
   *  internal ADC counter increment.
   * hmm, seems to be a combo of bits:
   * 00 --> standard frequency
   * 10 --> 1/2
   * 01 --> 1/20
   * 11 --> 1/200: */
  #define GAME_HWCFG_ADC_COUNTER_FREQ_MASK	0x06

  /* FIXME: these values might be reversed... */
  #define GAME_HWCFG_ADC_COUNTER_FREQ_STD	0
  #define GAME_HWCFG_ADC_COUNTER_FREQ_1_2	1
  #define GAME_HWCFG_ADC_COUNTER_FREQ_1_20	2
  #define GAME_HWCFG_ADC_COUNTER_FREQ_1_200	3

  /* enable gameport legacy I/O address (0x200)
   * I was unable to locate any configurability for a different address: */
  #define GAME_HWCFG_LEGACY_ADDRESS_ENABLE	0x08

/*** MPU401 ***/
#define AZF_IO_SIZE_MPU		0x04
#define AZF_IO_SIZE_MPU_PM	0x04

/*** OPL3 synth ***/
/* (only 0x06 of 0x08 bytes saved/restored by Windows driver) */
#define AZF_IO_SIZE_OPL3	0x08
#define AZF_IO_SIZE_OPL3_PM	0x06
/* hmm, given that a standard OPL3 has 4 registers only,
 * there might be some enhanced functionality lurking at the end
 * (especially since register 0x04 has a "non-empty" value 0xfe) */

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

/* helper macro to align I/O port ranges to 32bit I/O width */
#define AZF_ALIGN(x) (((x) + 3) & (~3))

#endif /* __SOUND_AZT3328_H  */
