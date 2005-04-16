/*
 * ac97.h 
 * 
 * definitions for the AC97, Intel's Audio Codec 97 Spec
 * also includes support for a generic AC97 interface
 */

#ifndef _AC97_H_
#define _AC97_H_
#include "sound_config.h"
#include "sound_calls.h"

#define  AC97_RESET              0x0000      //
#define  AC97_MASTER_VOL_STEREO  0x0002      // Line Out
#define  AC97_HEADPHONE_VOL      0x0004      // 
#define  AC97_MASTER_VOL_MONO    0x0006      // TAD Output
#define  AC97_MASTER_TONE        0x0008      //
#define  AC97_PCBEEP_VOL         0x000a      // none
#define  AC97_PHONE_VOL          0x000c      // TAD Input (mono)
#define  AC97_MIC_VOL            0x000e      // MIC Input (mono)
#define  AC97_LINEIN_VOL         0x0010      // Line Input (stereo)
#define  AC97_CD_VOL             0x0012      // CD Input (stereo)
#define  AC97_VIDEO_VOL          0x0014      // none
#define  AC97_AUX_VOL            0x0016      // Aux Input (stereo)
#define  AC97_PCMOUT_VOL         0x0018      // Wave Output (stereo)
#define  AC97_RECORD_SELECT      0x001a      //
#define  AC97_RECORD_GAIN        0x001c
#define  AC97_RECORD_GAIN_MIC    0x001e
#define  AC97_GENERAL_PURPOSE    0x0020
#define  AC97_3D_CONTROL         0x0022
#define  AC97_MODEM_RATE         0x0024
#define  AC97_POWER_CONTROL      0x0026

/* registers 0x0028 - 0x0058 are reserved */

/* AC'97 2.0 */
#define AC97_EXTENDED_ID	0x0028	/* Extended Audio ID */
#define AC97_EXTENDED_STATUS	0x002A	/* Extended Audio Status */
#define AC97_PCM_FRONT_DAC_RATE 0x002C  /* PCM Front DAC Rate */
#define AC97_PCM_SURR_DAC_RATE  0x002E  /* PCM Surround DAC Rate */
#define AC97_PCM_LFE_DAC_RATE   0x0030  /* PCM LFE DAC Rate */
#define AC97_PCM_LR_ADC_RATE	0x0032	/* PCM LR DAC Rate */
#define AC97_PCM_MIC_ADC_RATE   0x0034  /* PCM MIC ADC Rate */
#define AC97_CENTER_LFE_MASTER  0x0036  /* Center + LFE Master Volume */
#define AC97_SURROUND_MASTER    0x0038  /* Surround (Rear) Master Volume */
#define AC97_RESERVED_3A	0x003A	/* Reserved */
/* range 0x3c-0x58 - MODEM */

/* registers 0x005a - 0x007a are vendor reserved */

#define  AC97_VENDOR_ID1         0x007c
#define  AC97_VENDOR_ID2         0x007e

/* volume control bit defines */

#define AC97_MUTE                0x8000
#define AC97_MICBOOST            0x0040
#define AC97_LEFTVOL             0x3f00
#define AC97_RIGHTVOL            0x003f

/* record mux defines */

#define AC97_RECMUX_MIC          0x0000
#define AC97_RECMUX_CD           0x0101
#define AC97_RECMUX_VIDEO        0x0202      /* not used */
#define AC97_RECMUX_AUX          0x0303      
#define AC97_RECMUX_LINE         0x0404      
#define AC97_RECMUX_STEREO_MIX   0x0505
#define AC97_RECMUX_MONO_MIX     0x0606
#define AC97_RECMUX_PHONE        0x0707


/* general purpose register bit defines */

#define AC97_GP_LPBK             0x0080      /* Loopback mode */
#define AC97_GP_MS               0x0100      /* Mic Select 0=Mic1, 1=Mic2 */
#define AC97_GP_MIX              0x0200      /* Mono output select 0=Mix, 1=Mic */
#define AC97_GP_RLBK             0x0400      /* Remote Loopback - Modem line codec */
#define AC97_GP_LLBK             0x0800      /* Local Loopback - Modem Line codec */
#define AC97_GP_LD               0x1000      /* Loudness 1=on */
#define AC97_GP_3D               0x2000      /* 3D Enhancement 1=on */
#define AC97_GP_ST               0x4000      /* Stereo Enhancement 1=on */
#define AC97_GP_POP              0x8000      /* Pcm Out Path, 0=pre 3D, 1=post 3D */


/* powerdown control and status bit defines */

/* status */
#define AC97_PWR_MDM             0x0010      /* Modem section ready */
#define AC97_PWR_REF             0x0008      /* Vref nominal */
#define AC97_PWR_ANL             0x0004      /* Analog section ready */
#define AC97_PWR_DAC             0x0002      /* DAC section ready */
#define AC97_PWR_ADC             0x0001      /* ADC section ready */

/* control */
#define AC97_PWR_PR0             0x0100      /* ADC and Mux powerdown */
#define AC97_PWR_PR1             0x0200      /* DAC powerdown */
#define AC97_PWR_PR2             0x0400      /* Output mixer powerdown (Vref on) */
#define AC97_PWR_PR3             0x0800      /* Output mixer powerdown (Vref off) */
#define AC97_PWR_PR4             0x1000      /* AC-link powerdown */
#define AC97_PWR_PR5             0x2000      /* Internal Clk disable */
#define AC97_PWR_PR6             0x4000      /* HP amp powerdown */
#define AC97_PWR_PR7             0x8000      /* Modem off - if supported */

/* useful power states */
#define AC97_PWR_D0              0x0000      /* everything on */
#define AC97_PWR_D1              AC97_PWR_PR0|AC97_PWR_PR1|AC97_PWR_PR4
#define AC97_PWR_D2              AC97_PWR_PR0|AC97_PWR_PR1|AC97_PWR_PR2|AC97_PWR_PR3|AC97_PWR_PR4
#define AC97_PWR_D3              AC97_PWR_PR0|AC97_PWR_PR1|AC97_PWR_PR2|AC97_PWR_PR3|AC97_PWR_PR4
#define AC97_PWR_ANLOFF          AC97_PWR_PR2|AC97_PWR_PR3  /* analog section off */

/* Total number of defined registers.  */
#define AC97_REG_CNT 64

/* Generic AC97 mixer interface. */

/* Structure describing access to the hardware. */
struct ac97_hwint
{
    /* Perform any hardware-specific reset and initialization.  Returns
     0 on success, or a negative error code.  */
    int (*reset_device) (struct ac97_hwint *dev);

    /* Returns the contents of the specified register REG.  The caller
       should check to see if the desired contents are available in
       the cache first, if applicable. Returns a positive unsigned value
       representing the contents of the register, or a negative error
       code.  */
    int (*read_reg) (struct ac97_hwint *dev, u8 reg);

    /* Writes VALUE to register REG.  Returns 0 on success, or a
       negative error code.  */
    int (*write_reg) (struct ac97_hwint *dev, u8 reg, u16 value);

    /* Hardware-specific information. */
    void *driver_private;

    /* Three OSS masks. */
    int mixer_devmask;
    int mixer_stereomask;
    int mixer_recmask;

    /* The mixer cache. The indices correspond to the AC97 hardware register
       number / 2, since the register numbers are always an even number.

       Unknown values are set to -1; unsupported registers contain a
       -2.  */
    int last_written_mixer_values[AC97_REG_CNT];

    /* A cache of values written via OSS; we need these so we can return
       the values originally written by the user.

       Why the original user values?  Because the real-world hardware
       has less precision, and some existing applications assume that
       they will get back the exact value that they wrote (aumix).

       A -1 value indicates that no value has been written to this mixer
       channel via OSS.  */
    int last_written_OSS_values[SOUND_MIXER_NRDEVICES];
};

/* Values stored in the register cache.  */
#define AC97_REGVAL_UNKNOWN -1
#define AC97_REG_UNSUPPORTED -2

struct ac97_mixer_value_list
{
    /* Mixer channel to set.  List is terminated by a value of -1.  */
    int oss_channel;
    /* The scaled value to set it to; values generally range from 0-100. */
    union {
	struct {
	    u8 left, right;
	} stereo;
	u8 mono;
    } value;
};

/* Initialize the ac97 mixer by resetting it.  */
extern int ac97_init (struct ac97_hwint *dev);

/* Sets the mixer DEV to the values in VALUE_LIST.  Returns 0 on success,
   or a negative error code.  */
extern int ac97_set_values (struct ac97_hwint *dev,
			    struct ac97_mixer_value_list *value_list);

/* Writes the specified VALUE to the AC97 register REG in the mixer.
   Takes care of setting the last-written cache as well.  */
extern int ac97_put_register (struct ac97_hwint *dev, u8 reg, u16 value);

/* Default ioctl. */
extern int ac97_mixer_ioctl (struct ac97_hwint *dev, unsigned int cmd,
			     void __user * arg);

/* Do a complete reset on the AC97 mixer, restoring all mixer registers to
   the current values.  Normally used after an APM resume event.  */
extern int ac97_reset (struct ac97_hwint *dev);
#endif

/*
 * Local variables:
 *  c-basic-offset: 4
 * End:
 */
