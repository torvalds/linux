/* SPDX-License-Identifier: GPL-2.0 */

/*
 * Register bits and API for Wolfson WM97xx series of codecs
 */

#ifndef _LINUX_WM97XX_H
#define _LINUX_WM97XX_H

#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/ac97_codec.h>
#include <sound/initval.h>
#include <linux/types.h>
#include <linux/list.h>
#include <linux/input.h>	/* Input device layer */
#include <linux/platform_device.h>

/*
 * WM97xx variants
 */
#define	WM97xx_GENERIC			0x0000
#define	WM97xx_WM1613			0x1613

/*
 * WM97xx AC97 Touchscreen registers
 */
#define AC97_WM97XX_DIGITISER1		0x76
#define AC97_WM97XX_DIGITISER2		0x78
#define AC97_WM97XX_DIGITISER_RD 	0x7a
#define AC97_WM9713_DIG1		0x74
#define AC97_WM9713_DIG2		AC97_WM97XX_DIGITISER1
#define AC97_WM9713_DIG3		AC97_WM97XX_DIGITISER2

/*
 * WM97xx register bits
 */
#define WM97XX_POLL		0x8000	/* initiate a polling measurement */
#define WM97XX_ADCSEL_X		0x1000	/* x coord measurement */
#define WM97XX_ADCSEL_Y		0x2000	/* y coord measurement */
#define WM97XX_ADCSEL_PRES	0x3000	/* pressure measurement */
#define WM97XX_AUX_ID1		0x4000
#define WM97XX_AUX_ID2		0x5000
#define WM97XX_AUX_ID3		0x6000
#define WM97XX_AUX_ID4		0x7000
#define WM97XX_ADCSEL_MASK	0x7000	/* ADC selection mask */
#define WM97XX_COO		0x0800	/* enable coordinate mode */
#define WM97XX_CTC		0x0400	/* enable continuous mode */
#define WM97XX_CM_RATE_93	0x0000	/* 93.75Hz continuous rate */
#define WM97XX_CM_RATE_187	0x0100	/* 187.5Hz continuous rate */
#define WM97XX_CM_RATE_375	0x0200	/* 375Hz continuous rate */
#define WM97XX_CM_RATE_750	0x0300	/* 750Hz continuous rate */
#define WM97XX_CM_RATE_8K	0x00f0	/* 8kHz continuous rate */
#define WM97XX_CM_RATE_12K	0x01f0	/* 12kHz continuous rate */
#define WM97XX_CM_RATE_24K	0x02f0	/* 24kHz continuous rate */
#define WM97XX_CM_RATE_48K	0x03f0	/* 48kHz continuous rate */
#define WM97XX_CM_RATE_MASK	0x03f0
#define WM97XX_RATE(i)		(((i & 3) << 8) | ((i & 4) ? 0xf0 : 0))
#define WM97XX_DELAY(i)		((i << 4) & 0x00f0)	/* sample delay times */
#define WM97XX_DELAY_MASK	0x00f0
#define WM97XX_SLEN		0x0008	/* slot read back enable */
#define WM97XX_SLT(i)		((i - 5) & 0x7)	/* panel slot (5-11) */
#define WM97XX_SLT_MASK		0x0007
#define WM97XX_PRP_DETW		0x4000	/* detect on, digitise off, wake */
#define WM97XX_PRP_DET		0x8000	/* detect on, digitise off, no wake */
#define WM97XX_PRP_DET_DIG	0xc000	/* setect on, digitise on */
#define WM97XX_RPR		0x2000	/* wake up on pen down */
#define WM97XX_PEN_DOWN		0x8000	/* pen is down */

/* WM9712 Bits */
#define WM9712_45W		0x1000	/* set for 5-wire touchscreen */
#define WM9712_PDEN		0x0800	/* measure only when pen down */
#define WM9712_WAIT		0x0200	/* wait until adc is read before next sample */
#define WM9712_PIL		0x0100	/* current used for pressure measurement. set 400uA else 200uA */
#define WM9712_MASK_HI		0x0040	/* hi on mask pin (47) stops conversions */
#define WM9712_MASK_EDGE	0x0080	/* rising/falling edge on pin delays sample */
#define	WM9712_MASK_SYNC	0x00c0	/* rising/falling edge on mask initiates sample */
#define WM9712_RPU(i)		(i&0x3f)	/* internal pull up on pen detect (64k / rpu) */
#define WM9712_PD(i)		(0x1 << i)	/* power management */

/* WM9712 Registers */
#define AC97_WM9712_POWER	0x24
#define AC97_WM9712_REV		0x58

/* WM9705 Bits */
#define WM9705_PDEN		0x1000	/* measure only when pen is down */
#define WM9705_PINV		0x0800	/* inverts sense of pen down output */
#define WM9705_BSEN		0x0400	/* BUSY flag enable, pin47 is 1 when busy */
#define WM9705_BINV		0x0200	/* invert BUSY (pin47) output */
#define WM9705_WAIT		0x0100	/* wait until adc is read before next sample */
#define WM9705_PIL		0x0080	/* current used for pressure measurement. set 400uA else 200uA */
#define WM9705_PHIZ		0x0040	/* set PHONE and PCBEEP inputs to high impedance */
#define WM9705_MASK_HI		0x0010	/* hi on mask stops conversions */
#define WM9705_MASK_EDGE	0x0020	/* rising/falling edge on pin delays sample */
#define	WM9705_MASK_SYNC	0x0030	/* rising/falling edge on mask initiates sample */
#define WM9705_PDD(i)		(i & 0x000f)	/* pen detect comparator threshold */


/* WM9713 Bits */
#define WM9713_PDPOL		0x0400	/* Pen down polarity */
#define WM9713_POLL		0x0200	/* initiate a polling measurement */
#define WM9713_CTC		0x0100	/* enable continuous mode */
#define WM9713_ADCSEL_X		0x0002	/* X measurement */
#define WM9713_ADCSEL_Y		0x0004	/* Y measurement */
#define WM9713_ADCSEL_PRES	0x0008	/* Pressure measurement */
#define WM9713_COO		0x0001	/* enable coordinate mode */
#define WM9713_45W		0x1000  /* set for 5 wire panel */
#define WM9713_PDEN		0x0800	/* measure only when pen down */
#define WM9713_ADCSEL_MASK	0x00fe	/* ADC selection mask */
#define WM9713_WAIT		0x0200	/* coordinate wait */

/* AUX ADC ID's */
#define TS_COMP1		0x0
#define TS_COMP2		0x1
#define TS_BMON			0x2
#define TS_WIPER		0x3

/* ID numbers */
#define WM97XX_ID1		0x574d
#define WM9712_ID2		0x4c12
#define WM9705_ID2		0x4c05
#define WM9713_ID2		0x4c13

/* Codec GPIO's */
#define WM97XX_MAX_GPIO		16
#define WM97XX_GPIO_1		(1 << 1)
#define WM97XX_GPIO_2		(1 << 2)
#define WM97XX_GPIO_3		(1 << 3)
#define WM97XX_GPIO_4		(1 << 4)
#define WM97XX_GPIO_5		(1 << 5)
#define WM97XX_GPIO_6		(1 << 6)
#define WM97XX_GPIO_7		(1 << 7)
#define WM97XX_GPIO_8		(1 << 8)
#define WM97XX_GPIO_9		(1 << 9)
#define WM97XX_GPIO_10		(1 << 10)
#define WM97XX_GPIO_11		(1 << 11)
#define WM97XX_GPIO_12		(1 << 12)
#define WM97XX_GPIO_13		(1 << 13)
#define WM97XX_GPIO_14		(1 << 14)
#define WM97XX_GPIO_15		(1 << 15)


#define AC97_LINK_FRAME		21	/* time in uS for AC97 link frame */


/*---------------- Return codes from sample reading functions ---------------*/

/* More data is available; call the sample gathering function again */
#define RC_AGAIN			0x00000001
/* The returned sample is valid */
#define RC_VALID			0x00000002
/* The pen is up (the first RC_VALID without RC_PENUP means pen is down) */
#define RC_PENUP			0x00000004
/* The pen is down (RC_VALID implies RC_PENDOWN, but sometimes it is helpful
   to tell the handler that the pen is down but we don't know yet his coords,
   so the handler should not sleep or wait for pendown irq) */
#define RC_PENDOWN			0x00000008

/*
 * The wm97xx driver provides a private API for writing platform-specific
 * drivers.
 */

/* The structure used to return arch specific sampled data into */
struct wm97xx_data {
    int x;
    int y;
    int p;
};

/*
 * Codec GPIO status
 */
enum wm97xx_gpio_status {
    WM97XX_GPIO_HIGH,
    WM97XX_GPIO_LOW
};

/*
 * Codec GPIO direction
 */
enum wm97xx_gpio_dir {
    WM97XX_GPIO_IN,
    WM97XX_GPIO_OUT
};

/*
 * Codec GPIO polarity
 */
enum wm97xx_gpio_pol {
    WM97XX_GPIO_POL_HIGH,
    WM97XX_GPIO_POL_LOW
};

/*
 * Codec GPIO sticky
 */
enum wm97xx_gpio_sticky {
    WM97XX_GPIO_STICKY,
    WM97XX_GPIO_NOTSTICKY
};

/*
 * Codec GPIO wake
 */
enum wm97xx_gpio_wake {
    WM97XX_GPIO_WAKE,
    WM97XX_GPIO_NOWAKE
};

/*
 * Digitiser ioctl commands
 */
#define WM97XX_DIG_START	0x1
#define WM97XX_DIG_STOP		0x2
#define WM97XX_PHY_INIT		0x3
#define WM97XX_AUX_PREPARE	0x4
#define WM97XX_DIG_RESTORE	0x5

struct wm97xx;

extern struct wm97xx_codec_drv wm9705_codec;
extern struct wm97xx_codec_drv wm9712_codec;
extern struct wm97xx_codec_drv wm9713_codec;

/*
 * Codec driver interface - allows mapping to WM9705/12/13 and newer codecs
 */
struct wm97xx_codec_drv {
	u16 id;
	char *name;

	/* read 1 sample */
	int (*poll_sample) (struct wm97xx *, int adcsel, int *sample);

	/* read X,Y,[P] in poll */
	int (*poll_touch) (struct wm97xx *, struct wm97xx_data *);

	int (*acc_enable) (struct wm97xx *, int enable);
	void (*phy_init) (struct wm97xx *);
	void (*dig_enable) (struct wm97xx *, int enable);
	void (*dig_restore) (struct wm97xx *);
	void (*aux_prepare) (struct wm97xx *);
};


/* Machine specific and accelerated touch operations */
struct wm97xx_mach_ops {

	/* accelerated touch readback - coords are transmited on AC97 link */
	int acc_enabled;
	void (*acc_pen_up) (struct wm97xx *);
	int (*acc_pen_down) (struct wm97xx *);
	int (*acc_startup) (struct wm97xx *);
	void (*acc_shutdown) (struct wm97xx *);

	/* interrupt mask control - required for accelerated operation */
	void (*irq_enable) (struct wm97xx *, int enable);

	/* GPIO pin used for accelerated operation */
	int irq_gpio;

	/* pre and post sample - can be used to minimise any analog noise */
	void (*pre_sample) (int);  /* function to run before sampling */
	void (*post_sample) (int);  /* function to run after sampling */
};

struct wm97xx {
	u16 dig[3], id, gpio[6], misc;	/* Cached codec registers */
	u16 dig_save[3];		/* saved during aux reading */
	struct wm97xx_codec_drv *codec;	/* attached codec driver*/
	struct input_dev *input_dev;	/* touchscreen input device */
	struct snd_ac97 *ac97;		/* ALSA codec access */
	struct device *dev;		/* ALSA device */
	struct platform_device *battery_dev;
	struct platform_device *touch_dev;
	struct wm97xx_mach_ops *mach_ops;
	struct mutex codec_mutex;
	struct delayed_work ts_reader;  /* Used to poll touchscreen */
	unsigned long ts_reader_interval; /* Current interval for timer */
	unsigned long ts_reader_min_interval; /* Minimum interval */
	unsigned int pen_irq;		/* Pen IRQ number in use */
	struct workqueue_struct *ts_workq;
	struct work_struct pen_event_work;
	u16 acc_slot;			/* AC97 slot used for acc touch data */
	u16 acc_rate;			/* acc touch data rate */
	unsigned pen_is_down:1;		/* Pen is down */
	unsigned aux_waiting:1;		/* aux measurement waiting */
	unsigned pen_probably_down:1;	/* used in polling mode */
	u16 variant;			/* WM97xx chip variant */
	u16 suspend_mode;               /* PRP in suspend mode */
};

struct wm97xx_batt_pdata {
	int	batt_aux;
	int	temp_aux;
	int	charge_gpio;
	int	min_voltage;
	int	max_voltage;
	int	batt_div;
	int	batt_mult;
	int	temp_div;
	int	temp_mult;
	int	batt_tech;
	char	*batt_name;
};

struct wm97xx_pdata {
	struct wm97xx_batt_pdata	*batt_pdata;	/* battery data */
};

/*
 * Codec GPIO access (not supported on WM9705)
 * This can be used to set/get codec GPIO and Virtual GPIO status.
 */
enum wm97xx_gpio_status wm97xx_get_gpio(struct wm97xx *wm, u32 gpio);
void wm97xx_set_gpio(struct wm97xx *wm, u32 gpio,
			  enum wm97xx_gpio_status status);
void wm97xx_config_gpio(struct wm97xx *wm, u32 gpio,
				     enum wm97xx_gpio_dir dir,
				     enum wm97xx_gpio_pol pol,
				     enum wm97xx_gpio_sticky sticky,
				     enum wm97xx_gpio_wake wake);

void wm97xx_set_suspend_mode(struct wm97xx *wm, u16 mode);

/* codec AC97 IO access */
int wm97xx_reg_read(struct wm97xx *wm, u16 reg);
void wm97xx_reg_write(struct wm97xx *wm, u16 reg, u16 val);

/* aux adc readback */
int wm97xx_read_aux_adc(struct wm97xx *wm, u16 adcsel);

/* machine ops */
int wm97xx_register_mach_ops(struct wm97xx *, struct wm97xx_mach_ops *);
void wm97xx_unregister_mach_ops(struct wm97xx *);

#endif
