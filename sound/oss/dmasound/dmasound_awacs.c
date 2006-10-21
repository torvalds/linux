/*
 *  linux/sound/oss/dmasound/dmasound_awacs.c
 *
 *  PowerMac `AWACS' and `Burgundy' DMA Sound Driver
 *  with some limited support for DACA & Tumbler
 *
 *  See linux/sound/oss/dmasound/dmasound_core.c for copyright and
 *  history prior to 2001/01/26.
 *
 *	26/01/2001 ed 0.1 Iain Sandoe
 *		- added version info.
 *		- moved dbdma command buffer allocation to PMacXXXSqSetup()
 *		- fixed up beep dbdma cmd buffers
 *
 *	08/02/2001 [0.2]
 *		- make SNDCTL_DSP_GETFMTS return the correct info for the h/w
 *		- move soft format translations to a separate file
 *		- [0.3] make SNDCTL_DSP_GETCAPS return correct info.
 *		- [0.4] more informative machine name strings.
 *		- [0.5]
 *		- record changes.
 *		- made the default_hard/soft entries.
 *	04/04/2001 [0.6]
 *		- minor correction to bit assignments in awacs_defs.h
 *		- incorporate mixer changes from 2.2.x back-port.
 *		- take out passthru as a rec input (it isn't).
 *              - make Input Gain slider work the 'right way up'.
 *              - try to make the mixer sliders more logical - so now the
 *                input selectors are just two-state (>50% == ON) and the
 *                Input Gain slider handles the rest of the gain issues.
 *              - try to pick slider representations that most closely match
 *                the actual use - e.g. IGain for input gain... 
 *              - first stab at over/under-run detection.
 *		- minor cosmetic changes to IRQ identification.
 *		- fix bug where rates > max would be reported as supported.
 *              - first stab at over/under-run detection.
 *              - make use of i2c for mixer settings conditional on perch
 *                rather than cuda (some machines without perch have cuda).
 *              - fix bug where TX stops when dbdma status comes up "DEAD"
 *		  so far only reported on PowerComputing clones ... but.
 *		- put in AWACS/Screamer register write timeouts.
 *		- part way to partitioning the init() stuff
 *		- first pass at 'tumbler' stuff (not support - just an attempt
 *		  to allow the driver to load on new G4s).
 *      01/02/2002 [0.7] - BenH
 *	        - all sort of minor bits went in since the latest update, I
 *	          bumped the version number for that reason
 *
 *      07/26/2002 [0.8] - BenH
 *	        - More minor bits since last changelog (I should be more careful
 *	          with those)
 *	        - Support for snapper & better tumbler integration by Toby Sargeant
 *	        - Headphone detect for scremer by Julien Blache
 *	        - More tumbler fixed by Andreas Schwab
 *	11/29/2003 [0.8.1] - Renzo Davoli (King Enzo)
 *		- Support for Snapper line in
 *		- snapper input resampling (for rates < 44100)
 *		- software line gain control
 */

/* GENERAL FIXME/TODO: check that the assumptions about what is written to
   mac-io is valid for DACA & Tumbler.

   This driver is in bad need of a rewrite. The dbdma code has to be split,
   some proper device-tree parsing code has to be written, etc...
*/

#include <linux/types.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <linux/soundcard.h>
#include <linux/adb.h>
#include <linux/nvram.h>
#include <linux/tty.h>
#include <linux/vt_kern.h>
#include <linux/spinlock.h>
#include <linux/kmod.h>
#include <linux/interrupt.h>
#include <linux/input.h>
#include <linux/mutex.h>
#ifdef CONFIG_ADB_CUDA
#include <linux/cuda.h>
#endif
#ifdef CONFIG_ADB_PMU
#include <linux/pmu.h>
#endif

#include <asm/uaccess.h>
#include <asm/prom.h>
#include <asm/machdep.h>
#include <asm/io.h>
#include <asm/dbdma.h>
#include <asm/pmac_feature.h>
#include <asm/irq.h>
#include <asm/nvram.h>

#include "awacs_defs.h"
#include "dmasound.h"
#include "tas3001c.h"
#include "tas3004.h"
#include "tas_common.h"

#define DMASOUND_AWACS_REVISION	0
#define DMASOUND_AWACS_EDITION	7

#define AWACS_SNAPPER   110	/* fake revision # for snapper */
#define AWACS_BURGUNDY	100	/* fake revision # for burgundy */
#define AWACS_TUMBLER    90	/* fake revision # for tumbler */
#define AWACS_DACA	 80	/* fake revision # for daca (ibook) */
#define AWACS_AWACS       2     /* holding revision for AWACS */
#define AWACS_SCREAMER    3     /* holding revision for Screamer */
/*
 * Interrupt numbers and addresses, & info obtained from the device tree.
 */
static int awacs_irq, awacs_tx_irq, awacs_rx_irq;
static volatile struct awacs_regs __iomem *awacs;
static volatile u32 __iomem *i2s;
static volatile struct dbdma_regs __iomem *awacs_txdma, *awacs_rxdma;
static int awacs_rate_index;
static int awacs_subframe;
static struct device_node* awacs_node;
static struct device_node* i2s_node;
static struct resource awacs_rsrc[3];

static char awacs_name[64];
static int awacs_revision;
static int awacs_sleeping;
static DEFINE_MUTEX(dmasound_mutex);

static int sound_device_id;		/* exists after iMac revA */
static int hw_can_byteswap = 1 ;	/* most pmac sound h/w can */

/* model info */
/* To be replaced with better interaction with pmac_feature.c */
static int is_pbook_3X00;
static int is_pbook_g3;

/* expansion info */
static int has_perch;
static int has_ziva;

/* for earlier powerbooks which need fiddling with mac-io to enable
 * cd etc.
*/
static unsigned char __iomem *latch_base;
static unsigned char __iomem *macio_base;

/*
 * Space for the DBDMA command blocks.
 */
static void *awacs_tx_cmd_space;
static volatile struct dbdma_cmd *awacs_tx_cmds;
static int number_of_tx_cmd_buffers;

static void *awacs_rx_cmd_space;
static volatile struct dbdma_cmd *awacs_rx_cmds;
static int number_of_rx_cmd_buffers;

/*
 * Cached values of AWACS registers (we can't read them).
 * Except on the burgundy (and screamer). XXX
 */

int awacs_reg[8];
int awacs_reg1_save;

/* tracking values for the mixer contents
*/

static int spk_vol;
static int line_vol;
static int passthru_vol;

static int ip_gain;           /* mic preamp settings */
static int rec_lev = 0x4545 ; /* default CD gain 69 % */
static int mic_lev;
static int cd_lev = 0x6363 ; /* 99 % */
static int line_lev;

static int hdp_connected;

/*
 * Stuff for outputting a beep.  The values range from -327 to +327
 * so we can multiply by an amplitude in the range 0..100 to get a
 * signed short value to put in the output buffer.
 */
static short beep_wform[256] = {
	0,	40,	79,	117,	153,	187,	218,	245,
	269,	288,	304,	316,	323,	327,	327,	324,
	318,	310,	299,	288,	275,	262,	249,	236,
	224,	213,	204,	196,	190,	186,	183,	182,
	182,	183,	186,	189,	192,	196,	200,	203,
	206,	208,	209,	209,	209,	207,	204,	201,
	197,	193,	188,	183,	179,	174,	170,	166,
	163,	161,	160,	159,	159,	160,	161,	162,
	164,	166,	168,	169,	171,	171,	171,	170,
	169,	167,	163,	159,	155,	150,	144,	139,
	133,	128,	122,	117,	113,	110,	107,	105,
	103,	103,	103,	103,	104,	104,	105,	105,
	105,	103,	101,	97,	92,	86,	78,	68,
	58,	45,	32,	18,	3,	-11,	-26,	-41,
	-55,	-68,	-79,	-88,	-95,	-100,	-102,	-102,
	-99,	-93,	-85,	-75,	-62,	-48,	-33,	-16,
	0,	16,	33,	48,	62,	75,	85,	93,
	99,	102,	102,	100,	95,	88,	79,	68,
	55,	41,	26,	11,	-3,	-18,	-32,	-45,
	-58,	-68,	-78,	-86,	-92,	-97,	-101,	-103,
	-105,	-105,	-105,	-104,	-104,	-103,	-103,	-103,
	-103,	-105,	-107,	-110,	-113,	-117,	-122,	-128,
	-133,	-139,	-144,	-150,	-155,	-159,	-163,	-167,
	-169,	-170,	-171,	-171,	-171,	-169,	-168,	-166,
	-164,	-162,	-161,	-160,	-159,	-159,	-160,	-161,
	-163,	-166,	-170,	-174,	-179,	-183,	-188,	-193,
	-197,	-201,	-204,	-207,	-209,	-209,	-209,	-208,
	-206,	-203,	-200,	-196,	-192,	-189,	-186,	-183,
	-182,	-182,	-183,	-186,	-190,	-196,	-204,	-213,
	-224,	-236,	-249,	-262,	-275,	-288,	-299,	-310,
	-318,	-324,	-327,	-327,	-323,	-316,	-304,	-288,
	-269,	-245,	-218,	-187,	-153,	-117,	-79,	-40,
};

/* beep support */
#define BEEP_SRATE	22050	/* 22050 Hz sample rate */
#define BEEP_BUFLEN	512
#define BEEP_VOLUME	15	/* 0 - 100 */

static int beep_vol = BEEP_VOLUME;
static int beep_playing;
static int awacs_beep_state;
static short *beep_buf;
static void *beep_dbdma_cmd_space;
static volatile struct dbdma_cmd *beep_dbdma_cmd;

/* Burgundy functions */
static void awacs_burgundy_wcw(unsigned addr,unsigned newval);
static unsigned awacs_burgundy_rcw(unsigned addr);
static void awacs_burgundy_write_volume(unsigned address, int volume);
static int awacs_burgundy_read_volume(unsigned address);
static void awacs_burgundy_write_mvolume(unsigned address, int volume);
static int awacs_burgundy_read_mvolume(unsigned address);

/* we will allocate a single 'emergency' dbdma cmd block to use if the
   tx status comes up "DEAD".  This happens on some PowerComputing Pmac
   clones, either owing to a bug in dbdma or some interaction between
   IDE and sound.  However, this measure would deal with DEAD status if
   if appeared elsewhere.

   for the sake of memory efficiency we'll allocate this cmd as part of
   the beep cmd stuff.
*/

static volatile struct dbdma_cmd *emergency_dbdma_cmd;

#ifdef CONFIG_PM
/*
 * Stuff for restoring after a sleep.
 */
static int awacs_sleep_notify(struct pmu_sleep_notifier *self, int when);
struct pmu_sleep_notifier awacs_sleep_notifier = {
	awacs_sleep_notify, SLEEP_LEVEL_SOUND,
};
#endif /* CONFIG_PM */

/* for (soft) sample rate translations */
int expand_bal;		/* Balance factor for expanding (not volume!) */
int expand_read_bal;	/* Balance factor for expanding reads (not volume!) */

/*** Low level stuff *********************************************************/

static void *PMacAlloc(unsigned int size, gfp_t flags);
static void PMacFree(void *ptr, unsigned int size);
static int PMacIrqInit(void);
#ifdef MODULE
static void PMacIrqCleanup(void);
#endif
static void PMacSilence(void);
static void PMacInit(void);
static int PMacSetFormat(int format);
static int PMacSetVolume(int volume);
static void PMacPlay(void);
static void PMacRecord(void);
static irqreturn_t pmac_awacs_tx_intr(int irq, void *devid);
static irqreturn_t pmac_awacs_rx_intr(int irq, void *devid);
static irqreturn_t pmac_awacs_intr(int irq, void *devid);
static void awacs_write(int val);
static int awacs_get_volume(int reg, int lshift);
static int awacs_volume_setter(int volume, int n, int mute, int lshift);


/*** Mid level stuff **********************************************************/

static int PMacMixerIoctl(u_int cmd, u_long arg);
static int PMacWriteSqSetup(void);
static int PMacReadSqSetup(void);
static void PMacAbortRead(void);

extern TRANS transAwacsNormal ;
extern TRANS transAwacsExpand ;
extern TRANS transAwacsNormalRead ;
extern TRANS transAwacsExpandRead ;

extern int daca_init(void);
extern void daca_cleanup(void);
extern int daca_set_volume(uint left_vol, uint right_vol);
extern void daca_get_volume(uint * left_vol, uint  *right_vol);
extern int daca_enter_sleep(void);
extern int daca_leave_sleep(void);

#define TRY_LOCK()	\
	if ((rc = mutex_lock_interruptible(&dmasound_mutex)) != 0)	\
		return rc;
#define LOCK()		mutex_lock(&dmasound_mutex);

#define UNLOCK()	mutex_unlock(&dmasound_mutex);

/* We use different versions that the ones provided in dmasound.h
 * 
 * FIXME: Use different names ;)
 */
#undef IOCTL_IN
#undef IOCTL_OUT

#define IOCTL_IN(arg, ret)	\
	rc = get_user(ret, (int __user *)(arg)); \
	if (rc) break;
#define IOCTL_OUT(arg, ret)	\
	ioctl_return2((int __user *)(arg), ret)

static inline int ioctl_return2(int __user *addr, int value)
{
	return value < 0 ? value : put_user(value, addr);
}


/*** AE - TUMBLER / SNAPPER START ************************************************/


int gpio_audio_reset, gpio_audio_reset_pol;
int gpio_amp_mute, gpio_amp_mute_pol;
int gpio_headphone_mute, gpio_headphone_mute_pol;
int gpio_headphone_detect, gpio_headphone_detect_pol;
int gpio_headphone_irq;

int
setup_audio_gpio(const char *name, const char* compatible, int *gpio_addr, int* gpio_pol)
{
	struct device_node *np;
	const u32* pp;

	np = find_devices("gpio");
	if (!np)
		return -ENODEV;

	np = np->child;
	while(np != 0) {
		if (name) {
			const char *property =
				get_property(np,"audio-gpio",NULL);
			if (property != 0 && strcmp(property,name) == 0)
				break;
		} else if (compatible && device_is_compatible(np, compatible))
			break;
		np = np->sibling;
	}
	if (!np)
		return -ENODEV;
	pp = get_property(np, "AAPL,address", NULL);
	if (!pp)
		return -ENODEV;
	*gpio_addr = (*pp) & 0x0000ffff;
	pp = get_property(np, "audio-gpio-active-state", NULL);
	if (pp)
		*gpio_pol = *pp;
	else
		*gpio_pol = 1;
	return irq_of_parse_and_map(np, 0);
}

static inline void
write_audio_gpio(int gpio_addr, int data)
{
	if (!gpio_addr)
		return;
	pmac_call_feature(PMAC_FTR_WRITE_GPIO, NULL, gpio_addr, data ? 0x05 : 0x04);
}

static inline int
read_audio_gpio(int gpio_addr)
{
	if (!gpio_addr)
		return 0;
	return ((pmac_call_feature(PMAC_FTR_READ_GPIO, NULL, gpio_addr, 0) & 0x02) !=0);
}

/*
 * Headphone interrupt via GPIO (Tumbler, Snapper, DACA)
 */
static irqreturn_t
headphone_intr(int irq, void *devid)
{
	unsigned long flags;

	spin_lock_irqsave(&dmasound.lock, flags);
	if (read_audio_gpio(gpio_headphone_detect) == gpio_headphone_detect_pol) {
		printk(KERN_INFO "Audio jack plugged, muting speakers.\n");
		write_audio_gpio(gpio_headphone_mute, !gpio_headphone_mute_pol);
		write_audio_gpio(gpio_amp_mute, gpio_amp_mute_pol);
		tas_output_device_change(sound_device_id,TAS_OUTPUT_HEADPHONES,0);
	} else {
		printk(KERN_INFO "Audio jack unplugged, enabling speakers.\n");
		write_audio_gpio(gpio_amp_mute, !gpio_amp_mute_pol);
		write_audio_gpio(gpio_headphone_mute, gpio_headphone_mute_pol);
		tas_output_device_change(sound_device_id,TAS_OUTPUT_INTERNAL_SPKR,0);
	}
	spin_unlock_irqrestore(&dmasound.lock, flags);
	return IRQ_HANDLED;
}


/* Initialize tumbler */

static int
tas_dmasound_init(void)
{
	setup_audio_gpio(
		"audio-hw-reset",
		NULL,
		&gpio_audio_reset,
		&gpio_audio_reset_pol);
	setup_audio_gpio(
		"amp-mute",
		NULL,
		&gpio_amp_mute,
		&gpio_amp_mute_pol);
	setup_audio_gpio("headphone-mute",
		NULL,
		&gpio_headphone_mute,
		&gpio_headphone_mute_pol);
	gpio_headphone_irq = setup_audio_gpio(
		"headphone-detect",
		NULL,
		&gpio_headphone_detect,
		&gpio_headphone_detect_pol);
	/* Fix some broken OF entries in desktop machines */
	if (!gpio_headphone_irq)
		gpio_headphone_irq = setup_audio_gpio(
			NULL,
			"keywest-gpio15",
			&gpio_headphone_detect,
			&gpio_headphone_detect_pol);

	write_audio_gpio(gpio_audio_reset, gpio_audio_reset_pol);
	msleep(100);
	write_audio_gpio(gpio_audio_reset, !gpio_audio_reset_pol);
	msleep(100);
  	if (gpio_headphone_irq) {
		if (request_irq(gpio_headphone_irq,headphone_intr,0,"Headphone detect",NULL) < 0) {
    			printk(KERN_ERR "tumbler: Can't request headphone interrupt\n");
    			gpio_headphone_irq = 0;
    		} else {
			u8 val;
			/* Activate headphone status interrupts */
			val = pmac_call_feature(PMAC_FTR_READ_GPIO, NULL, gpio_headphone_detect, 0);
			pmac_call_feature(PMAC_FTR_WRITE_GPIO, NULL, gpio_headphone_detect, val | 0x80);
			/* Trigger it */
  			headphone_intr(0, NULL);
  		}
  	}
  	if (!gpio_headphone_irq) {
  		/* Some machine enter this case ? */
  		printk(KERN_WARNING "tumbler: Headphone detect IRQ not found, enabling all outputs !\n");
  		write_audio_gpio(gpio_amp_mute, !gpio_amp_mute_pol);
  		write_audio_gpio(gpio_headphone_mute, !gpio_headphone_mute_pol);
  	}
	return 0;
}


static int
tas_dmasound_cleanup(void)
{
	if (gpio_headphone_irq)
		free_irq(gpio_headphone_irq, NULL);
	return 0;
}

/* We don't support 48k yet */
static int tas_freqs[1] = { 44100 } ;
static int tas_freqs_ok[1] = { 1 } ;

/* don't know what to do really - just have to leave it where
 * OF left things
*/

static int
tas_set_frame_rate(void)
{
	if (i2s) {
		out_le32(i2s + (I2S_REG_SERIAL_FORMAT >> 2), 0x41190000);
		out_le32(i2s + (I2S_REG_DATAWORD_SIZES >> 2), 0x02000200);
	}
	dmasound.hard.speed = 44100 ;
	awacs_rate_index = 0 ;
	return 44100 ;
}

static int
tas_mixer_ioctl(u_int cmd, u_long arg)
{
	int __user *argp = (int __user *)arg;
	int data;
	int rc;

        rc=tas_device_ioctl(cmd, arg);
        if (rc != -EINVAL) {
        	return rc;
        }

        if ((cmd & ~0xff) == MIXER_WRITE(0) &&
            tas_supported_mixers() & (1<<(cmd & 0xff))) {
		rc = get_user(data, argp);
                if (rc<0) return rc;
		tas_set_mixer_level(cmd & 0xff, data);
		tas_get_mixer_level(cmd & 0xff, &data);
		return ioctl_return2(argp, data);
        }
        if ((cmd & ~0xff) == MIXER_READ(0) &&
            tas_supported_mixers() & (1<<(cmd & 0xff))) {
		tas_get_mixer_level(cmd & 0xff, &data);
		return ioctl_return2(argp, data);
        }

	switch(cmd) {
	case SOUND_MIXER_READ_DEVMASK:
		data = tas_supported_mixers() | SOUND_MASK_SPEAKER;
		rc = IOCTL_OUT(arg, data);
		break;
	case SOUND_MIXER_READ_STEREODEVS:
		data = tas_stereo_mixers();
		rc = IOCTL_OUT(arg, data);
		break;
	case SOUND_MIXER_READ_CAPS:
		rc = IOCTL_OUT(arg, 0);
		break;
	case SOUND_MIXER_READ_RECMASK:
		// XXX FIXME: find a way to check what is really available */
		data = SOUND_MASK_LINE | SOUND_MASK_MIC;
		rc = IOCTL_OUT(arg, data);
		break;
	case SOUND_MIXER_READ_RECSRC:
		if (awacs_reg[0] & MASK_MUX_AUDIN)
			data |= SOUND_MASK_LINE;
		if (awacs_reg[0] & MASK_MUX_MIC)
			data |= SOUND_MASK_MIC;
		rc = IOCTL_OUT(arg, data);
		break;
	case SOUND_MIXER_WRITE_RECSRC:
 		IOCTL_IN(arg, data);
		data =0;
 		rc = IOCTL_OUT(arg, data);
 		break;
	case SOUND_MIXER_WRITE_SPEAKER:	/* really bell volume */
 		IOCTL_IN(arg, data);
 		beep_vol = data & 0xff;
 		/* fall through */
	case SOUND_MIXER_READ_SPEAKER:
		rc = IOCTL_OUT(arg, (beep_vol<<8) | beep_vol);
 		break;
	case SOUND_MIXER_OUTMASK:
	case SOUND_MIXER_OUTSRC:
	default:
		rc = -EINVAL;
	}

	return rc;
}

static void __init
tas_init_frame_rates(unsigned int *prop, unsigned int l)
{
	int i ;
	if (prop) {
		for (i=0; i<1; i++)
			tas_freqs_ok[i] = 0;
		for (l /= sizeof(int); l > 0; --l) {
			unsigned int r = *prop++;
			/* Apple 'Fixed' format */
			if (r >= 0x10000)
				r >>= 16;
			for (i = 0; i < 1; ++i) {
				if (r == tas_freqs[i]) {
					tas_freqs_ok[i] = 1;
					break;
				}
			}
		}
	}
	/* else we assume that all the rates are available */
}


/*** AE - TUMBLER / SNAPPER END ************************************************/



/*** Low level stuff *********************************************************/

/*
 * PCI PowerMac, with AWACS, Screamer, Burgundy, DACA or Tumbler and DBDMA.
 */
static void *PMacAlloc(unsigned int size, gfp_t flags)
{
	return kmalloc(size, flags);
}

static void PMacFree(void *ptr, unsigned int size)
{
	kfree(ptr);
}

static int __init PMacIrqInit(void)
{
	if (awacs)
		if (request_irq(awacs_irq, pmac_awacs_intr, 0, "Built-in Sound misc", NULL))
			return 0;
	if (request_irq(awacs_tx_irq, pmac_awacs_tx_intr, 0, "Built-in Sound out", NULL)
	    || request_irq(awacs_rx_irq, pmac_awacs_rx_intr, 0, "Built-in Sound in", NULL))
		return 0;
	return 1;
}

#ifdef MODULE
static void PMacIrqCleanup(void)
{
	/* turn off input & output dma */
	DBDMA_DO_STOP(awacs_txdma);
	DBDMA_DO_STOP(awacs_rxdma);

	if (awacs)
		/* disable interrupts from awacs interface */
		out_le32(&awacs->control, in_le32(&awacs->control) & 0xfff);
	
	/* Switch off the sound clock */
	pmac_call_feature(PMAC_FTR_SOUND_CHIP_ENABLE, awacs_node, 0, 0);
	/* Make sure proper bits are set on pismo & tipb */
	if ((machine_is_compatible("PowerBook3,1") ||
	    machine_is_compatible("PowerBook3,2")) && awacs) {
		awacs_reg[1] |= MASK_PAROUT0 | MASK_PAROUT1;
		awacs_write(MASK_ADDR1 | awacs_reg[1]);
		msleep(200);
	}
	if (awacs)
		free_irq(awacs_irq, NULL);
	free_irq(awacs_tx_irq, NULL);
	free_irq(awacs_rx_irq, NULL);
	
	if (awacs)
		iounmap(awacs);
	if (i2s)
		iounmap(i2s);
	iounmap(awacs_txdma);
	iounmap(awacs_rxdma);

	release_mem_region(awacs_rsrc[0].start,
			   awacs_rsrc[0].end - awacs_rsrc[0].start + 1);
	release_mem_region(awacs_rsrc[1].start,
			   awacs_rsrc[1].end - awacs_rsrc[1].start + 1);
	release_mem_region(awacs_rsrc[2].start,
			   awacs_rsrc[2].end - awacs_rsrc[2].start + 1);

	kfree(awacs_tx_cmd_space);
	kfree(awacs_rx_cmd_space);
	kfree(beep_dbdma_cmd_space);
	kfree(beep_buf);
#ifdef CONFIG_PM
	pmu_unregister_sleep_notifier(&awacs_sleep_notifier);
#endif
}
#endif /* MODULE */

static void PMacSilence(void)
{
	/* turn off output dma */
	DBDMA_DO_STOP(awacs_txdma);
}

/* don't know what to do really - just have to leave it where
 * OF left things
*/

static int daca_set_frame_rate(void)
{
	if (i2s) {
		out_le32(i2s + (I2S_REG_SERIAL_FORMAT >> 2), 0x41190000);
		out_le32(i2s + (I2S_REG_DATAWORD_SIZES >> 2), 0x02000200);
	}
	dmasound.hard.speed = 44100 ;
	awacs_rate_index = 0 ;
	return 44100 ;
}

static int awacs_freqs[8] = {
	44100, 29400, 22050, 17640, 14700, 11025, 8820, 7350
};
static int awacs_freqs_ok[8] = { 1, 1, 1, 1, 1, 1, 1, 1 };

static int
awacs_set_frame_rate(int desired, int catch_r)
{
	int tolerance, i = 8 ;
	/*
	 * If we have a sample rate which is within catchRadius percent
	 * of the requested value, we don't have to expand the samples.
	 * Otherwise choose the next higher rate.
	 * N.B.: burgundy awacs only works at 44100 Hz.
	 */
	do {
		tolerance = catch_r * awacs_freqs[--i] / 100;
		if (awacs_freqs_ok[i]
		    && dmasound.soft.speed <= awacs_freqs[i] + tolerance)
			break;
	} while (i > 0);
	dmasound.hard.speed = awacs_freqs[i];
	awacs_rate_index = i;

	out_le32(&awacs->control, MASK_IEPC | (i << 8) | 0x11 );
	awacs_reg[1] = (awacs_reg[1] & ~MASK_SAMPLERATE) | (i << 3);
	awacs_write(awacs_reg[1] | MASK_ADDR1);
	return dmasound.hard.speed;
}

static int
burgundy_set_frame_rate(void)
{
	awacs_rate_index = 0 ;
	awacs_reg[1] = (awacs_reg[1] & ~MASK_SAMPLERATE) ;
	/* XXX disable error interrupt on burgundy for now */
	out_le32(&awacs->control, MASK_IEPC | 0 | 0x11 | MASK_IEE);
	return 44100 ;
}

static int
set_frame_rate(int desired, int catch_r)
{
	switch (awacs_revision) {
		case AWACS_BURGUNDY:
			dmasound.hard.speed = burgundy_set_frame_rate();
			break ;
		case AWACS_TUMBLER:
		case AWACS_SNAPPER:
			dmasound.hard.speed = tas_set_frame_rate();
			break ;
		case AWACS_DACA:
			dmasound.hard.speed =
			  daca_set_frame_rate();
			break ;
		default:
			dmasound.hard.speed = awacs_set_frame_rate(desired,
						catch_r);
			break ;
	}
	return dmasound.hard.speed ;
}

static void
awacs_recalibrate(void)
{
	/* Sorry for the horrible delays... I hope to get that improved
	 * by making the whole PM process asynchronous in a future version
	 */
	msleep(750);
	awacs_reg[1] |= MASK_CMUTE | MASK_AMUTE;
	awacs_write(awacs_reg[1] | MASK_RECALIBRATE | MASK_ADDR1);
	msleep(1000);
	awacs_write(awacs_reg[1] | MASK_ADDR1);
}

static void PMacInit(void)
{
	int tolerance;

	switch (dmasound.soft.format) {
	    case AFMT_S16_LE:
	    case AFMT_U16_LE:
		if (hw_can_byteswap)
			dmasound.hard.format = AFMT_S16_LE;
		else
			dmasound.hard.format = AFMT_S16_BE;
		break;
	default:
		dmasound.hard.format = AFMT_S16_BE;
		break;
	}
	dmasound.hard.stereo = 1;
	dmasound.hard.size = 16;

	/* set dmasound.hard.speed - on the basis of what we want (soft)
	 * and the tolerance we'll allow.
	*/
	set_frame_rate(dmasound.soft.speed, catchRadius) ;

	tolerance = (catchRadius * dmasound.hard.speed) / 100;
	if (dmasound.soft.speed >= dmasound.hard.speed - tolerance) {
		dmasound.trans_write = &transAwacsNormal;
		dmasound.trans_read = &transAwacsNormalRead;
	} else {
		dmasound.trans_write = &transAwacsExpand;
		dmasound.trans_read = &transAwacsExpandRead;
	}

	if (awacs) {
		if (hw_can_byteswap && (dmasound.hard.format == AFMT_S16_LE))
			out_le32(&awacs->byteswap, BS_VAL);
		else
			out_le32(&awacs->byteswap, 0);
	}
	
	expand_bal = -dmasound.soft.speed;
	expand_read_bal = -dmasound.soft.speed;
}

static int PMacSetFormat(int format)
{
	int size;
	int req_format = format;
		
	switch (format) {
	case AFMT_QUERY:
		return dmasound.soft.format;
	case AFMT_MU_LAW:
	case AFMT_A_LAW:
	case AFMT_U8:
	case AFMT_S8:
		size = 8;
		break;
	case AFMT_S16_LE:
		if(!hw_can_byteswap)
			format = AFMT_S16_BE;
	case AFMT_S16_BE:
		size = 16;
		break;
	case AFMT_U16_LE:
		if(!hw_can_byteswap)
			format = AFMT_U16_BE;
	case AFMT_U16_BE:
		size = 16;
		break;
	default: /* :-) */
		printk(KERN_ERR "dmasound: unknown format 0x%x, using AFMT_U8\n",
		       format);
		size = 8;
		format = AFMT_U8;
	}
	
	if (req_format == format) {
		dmasound.soft.format = format;
		dmasound.soft.size = size;
		if (dmasound.minDev == SND_DEV_DSP) {
			dmasound.dsp.format = format;
			dmasound.dsp.size = size;
		}
	}

	return format;
}

#define AWACS_VOLUME_TO_MASK(x)	(15 - ((((x) - 1) * 15) / 99))
#define AWACS_MASK_TO_VOLUME(y)	(100 - ((y) * 99 / 15))

static int awacs_get_volume(int reg, int lshift)
{
	int volume;

	volume = AWACS_MASK_TO_VOLUME((reg >> lshift) & 0xf);
	volume |= AWACS_MASK_TO_VOLUME(reg & 0xf) << 8;
	return volume;
}

static int awacs_volume_setter(int volume, int n, int mute, int lshift)
{
	int r1, rn;

	if (mute && volume == 0) {
		r1 = awacs_reg[1] | mute;
	} else {
		r1 = awacs_reg[1] & ~mute;
		rn = awacs_reg[n] & ~(0xf | (0xf << lshift));
		rn |= ((AWACS_VOLUME_TO_MASK(volume & 0xff) & 0xf) << lshift);
		rn |= AWACS_VOLUME_TO_MASK((volume >> 8) & 0xff) & 0xf;
		awacs_reg[n] = rn;
		awacs_write((n << 12) | rn);
		volume = awacs_get_volume(rn, lshift);
	}
	if (r1 != awacs_reg[1]) {
		awacs_reg[1] = r1;
		awacs_write(r1 | MASK_ADDR1);
	}
	return volume;
}

static int PMacSetVolume(int volume)
{
	printk(KERN_WARNING "Bogus call to PMacSetVolume !\n");
	return 0;
}

static void awacs_setup_for_beep(int speed)
{
	out_le32(&awacs->control,
		 (in_le32(&awacs->control) & ~0x1f00)
		 | ((speed > 0 ? speed : awacs_rate_index) << 8));

	if (hw_can_byteswap && (dmasound.hard.format == AFMT_S16_LE) && speed == -1)
		out_le32(&awacs->byteswap, BS_VAL);
	else
		out_le32(&awacs->byteswap, 0);
}

/* CHECK: how much of this *really* needs IRQs masked? */
static void __PMacPlay(void)
{
	volatile struct dbdma_cmd *cp;
	int next_frg, count;

	count = 300 ; /* > two cycles at the lowest sample rate */

	/* what we want to send next */
	next_frg = (write_sq.front + write_sq.active) % write_sq.max_count;

	if (awacs_beep_state) {
		/* sound takes precedence over beeps */
		/* stop the dma channel */
		out_le32(&awacs_txdma->control, (RUN|PAUSE|FLUSH|WAKE) << 16);
		while ( (in_le32(&awacs_txdma->status) & RUN) && count--)
			udelay(1);
		if (awacs)
			awacs_setup_for_beep(-1);
		out_le32(&awacs_txdma->cmdptr,
			 virt_to_bus(&(awacs_tx_cmds[next_frg])));

		beep_playing = 0;
		awacs_beep_state = 0;
	}
	/* this won't allow more than two frags to be in the output queue at
	   once. (or one, if the max frags is 2 - because count can't exceed
	   2 in that case)
	*/
	while (write_sq.active < 2 && write_sq.active < write_sq.count) {
		count = (write_sq.count == write_sq.active + 1) ?
				write_sq.rear_size:write_sq.block_size ;
		if (count < write_sq.block_size) {
			if (!write_sq.syncing) /* last block not yet filled,*/
				break; 	/* and we're not syncing or POST-ed */
			else {
				/* pretend the block is full to force a new
				   block to be started on the next write */
				write_sq.rear_size = write_sq.block_size ;
				write_sq.syncing &= ~2 ; /* clear POST */
			}
		}
		cp = &awacs_tx_cmds[next_frg];
		st_le16(&cp->req_count, count);
		st_le16(&cp->xfer_status, 0);
		st_le16(&cp->command, OUTPUT_MORE + INTR_ALWAYS);
		/* put a STOP at the end of the queue - but only if we have
		   space for it.  This means that, if we under-run and we only
		   have two fragments, we might re-play sound from an existing
		   queued frag.  I guess the solution to that is not to set two
		   frags if you are likely to under-run...
		*/
		if (write_sq.count < write_sq.max_count) {
			if (++next_frg >= write_sq.max_count)
				next_frg = 0 ; /* wrap */
			/* if we get here then we've underrun so we will stop*/
			st_le16(&awacs_tx_cmds[next_frg].command, DBDMA_STOP);
		}
		/* set the dbdma controller going, if it is not already */
		if (write_sq.active == 0)
			out_le32(&awacs_txdma->cmdptr, virt_to_bus(cp));
		(void)in_le32(&awacs_txdma->status);
		out_le32(&awacs_txdma->control, ((RUN|WAKE) << 16) + (RUN|WAKE));
		++write_sq.active;
	}
}

static void PMacPlay(void)
{
	LOCK();
	if (!awacs_sleeping) {
		unsigned long flags;

		spin_lock_irqsave(&dmasound.lock, flags);
		__PMacPlay();
		spin_unlock_irqrestore(&dmasound.lock, flags);
	}
	UNLOCK();
}

static void PMacRecord(void)
{
	unsigned long flags;

	if (read_sq.active)
		return;

	spin_lock_irqsave(&dmasound.lock, flags);

	/* This is all we have to do......Just start it up.
	*/
	out_le32(&awacs_rxdma->control, ((RUN|WAKE) << 16) + (RUN|WAKE));
	read_sq.active = 1;

	spin_unlock_irqrestore(&dmasound.lock, flags);
}

/* if the TX status comes up "DEAD" - reported on some Power Computing machines
   we need to re-start the dbdma - but from a different physical start address
   and with a different transfer length.  It would get very messy to do this
   with the normal dbdma_cmd blocks - we would have to re-write the buffer start
   addresses each time.  So, we will keep a single dbdma_cmd block which can be
   fiddled with.
   When DEAD status is first reported the content of the faulted dbdma block is
   copied into the emergency buffer and we note that the buffer is in use.
   we then bump the start physical address by the amount that was successfully
   output before it died.
   On any subsequent DEAD result we just do the bump-ups (we know that we are
   already using the emergency dbdma_cmd).
   CHECK: this just tries to "do it".  It is possible that we should abandon
   xfers when the number of residual bytes gets below a certain value - I can
   see that this might cause a loop-forever if too small a transfer causes
   DEAD status.  However this is a TODO for now - we'll see what gets reported.
   When we get a successful transfer result with the emergency buffer we just
   pretend that it completed using the original dmdma_cmd and carry on.  The
   'next_cmd' field will already point back to the original loop of blocks.
*/

static irqreturn_t
pmac_awacs_tx_intr(int irq, void *devid)
{
	int i = write_sq.front;
	int stat;
	int i_nowrap = write_sq.front;
	volatile struct dbdma_cmd *cp;
	/* != 0 when we are dealing with a DEAD xfer */
	static int emergency_in_use;

	spin_lock(&dmasound.lock);
	while (write_sq.active > 0) { /* we expect to have done something*/
		if (emergency_in_use) /* we are dealing with DEAD xfer */
			cp = emergency_dbdma_cmd ;
		else
			cp = &awacs_tx_cmds[i];
		stat = ld_le16(&cp->xfer_status);
		if (stat & DEAD) {
			unsigned short req, res ;
			unsigned int phy ;
#ifdef DEBUG_DMASOUND
printk("dmasound_pmac: tx-irq: xfer died - patching it up...\n") ;
#endif
			/* to clear DEAD status we must first clear RUN
			   set it to quiescent to be on the safe side */
			(void)in_le32(&awacs_txdma->status);
			out_le32(&awacs_txdma->control,
				(RUN|PAUSE|FLUSH|WAKE) << 16);
			write_sq.died++ ;
			if (!emergency_in_use) { /* new problem */
				memcpy((void *)emergency_dbdma_cmd, (void *)cp,
					sizeof(struct dbdma_cmd));
				emergency_in_use = 1;
				cp = emergency_dbdma_cmd;
			}
			/* now bump the values to reflect the amount
			   we haven't yet shifted */
			req = ld_le16(&cp->req_count);
			res = ld_le16(&cp->res_count);
			phy = ld_le32(&cp->phy_addr);
			phy += (req - res);
			st_le16(&cp->req_count, res);
			st_le16(&cp->res_count, 0);
			st_le16(&cp->xfer_status, 0);
			st_le32(&cp->phy_addr, phy);
			st_le32(&cp->cmd_dep, virt_to_bus(&awacs_tx_cmds[(i+1)%write_sq.max_count]));
			st_le16(&cp->command, OUTPUT_MORE | BR_ALWAYS | INTR_ALWAYS);
			
			/* point at our patched up command block */
			out_le32(&awacs_txdma->cmdptr, virt_to_bus(cp));
			/* we must re-start the controller */
			(void)in_le32(&awacs_txdma->status);
			/* should complete clearing the DEAD status */
			out_le32(&awacs_txdma->control,
				((RUN|WAKE) << 16) + (RUN|WAKE));
			break; /* this block is still going */
		}
		if ((stat & ACTIVE) == 0)
			break;	/* this frame is still going */
		if (emergency_in_use)
			emergency_in_use = 0 ; /* done that */
		--write_sq.count;
		--write_sq.active;
		i_nowrap++;
		if (++i >= write_sq.max_count)
			i = 0;
	}

	/* if we stopped and we were not sync-ing - then we under-ran */
	if( write_sq.syncing == 0 ){
		stat = in_le32(&awacs_txdma->status) ;
		/* we hit the dbdma_stop */
		if( (stat & ACTIVE) == 0 ) write_sq.xruns++ ;
	}

	/* if we used some data up then wake the writer to supply some more*/
	if (i_nowrap != write_sq.front)
		WAKE_UP(write_sq.action_queue);
	write_sq.front = i;

	/* but make sure we funnel what we've already got */\
	 if (!awacs_sleeping)
		__PMacPlay();

	/* make the wake-on-empty conditional on syncing */
	if (!write_sq.active && (write_sq.syncing & 1))
		WAKE_UP(write_sq.sync_queue); /* any time we're empty */
	spin_unlock(&dmasound.lock);
	return IRQ_HANDLED;
}


static irqreturn_t
pmac_awacs_rx_intr(int irq, void *devid)
{
	int stat ;
	/* For some reason on my PowerBook G3, I get one interrupt
	 * when the interrupt vector is installed (like something is
	 * pending).  This happens before the dbdma is initialized by
	 * us, so I just check the command pointer and if it is zero,
	 * just blow it off.
	 */
	if (in_le32(&awacs_rxdma->cmdptr) == 0)
		return IRQ_HANDLED;

	/* We also want to blow 'em off when shutting down.
	*/
	if (read_sq.active == 0)
		return IRQ_HANDLED;

	spin_lock(&dmasound.lock);
	/* Check multiple buffers in case we were held off from
	 * interrupt processing for a long time.  Geeze, I really hope
	 * this doesn't happen.
	 */
	while ((stat=awacs_rx_cmds[read_sq.rear].xfer_status)) {

		/* if we got a "DEAD" status then just log it for now.
		   and try to restart dma.
		   TODO: figure out how best to fix it up
		*/
		if (stat & DEAD){
#ifdef DEBUG_DMASOUND
printk("dmasound_pmac: rx-irq: DIED - attempting resurection\n");
#endif
			/* to clear DEAD status we must first clear RUN
			   set it to quiescent to be on the safe side */
			(void)in_le32(&awacs_txdma->status);
			out_le32(&awacs_txdma->control,
				(RUN|PAUSE|FLUSH|WAKE) << 16);
			awacs_rx_cmds[read_sq.rear].xfer_status = 0;
			awacs_rx_cmds[read_sq.rear].res_count = 0;
			read_sq.died++ ;
			(void)in_le32(&awacs_txdma->status);
			/* re-start the same block */
			out_le32(&awacs_rxdma->cmdptr,
				virt_to_bus(&awacs_rx_cmds[read_sq.rear]));
			/* we must re-start the controller */
			(void)in_le32(&awacs_rxdma->status);
			/* should complete clearing the DEAD status */
			out_le32(&awacs_rxdma->control,
				((RUN|WAKE) << 16) + (RUN|WAKE));
			spin_unlock(&dmasound.lock);
			return IRQ_HANDLED; /* try this block again */
		}
		/* Clear status and move on to next buffer.
		*/
		awacs_rx_cmds[read_sq.rear].xfer_status = 0;
		read_sq.rear++;

		/* Wrap the buffer ring.
		*/
		if (read_sq.rear >= read_sq.max_active)
			read_sq.rear = 0;

		/* If we have caught up to the front buffer, bump it.
		 * This will cause weird (but not fatal) results if the
		 * read loop is currently using this buffer.  The user is
		 * behind in this case anyway, so weird things are going
		 * to happen.
		 */
		if (read_sq.rear == read_sq.front) {
			read_sq.front++;
			read_sq.xruns++ ; /* we overan */
			if (read_sq.front >= read_sq.max_active)
				read_sq.front = 0;
		}
	}

	WAKE_UP(read_sq.action_queue);
	spin_unlock(&dmasound.lock);
	return IRQ_HANDLED;
}


static irqreturn_t
pmac_awacs_intr(int irq, void *devid)
{
	int ctrl;
	int status;
	int r1;

	spin_lock(&dmasound.lock);
	ctrl = in_le32(&awacs->control);
	status = in_le32(&awacs->codec_stat);

	if (ctrl & MASK_PORTCHG) {
		/* tested on Screamer, should work on others too */
		if (awacs_revision == AWACS_SCREAMER) {
			if (((status & MASK_HDPCONN) >> 3) && (hdp_connected == 0)) {
				hdp_connected = 1;
				
				r1 = awacs_reg[1] | MASK_SPKMUTE;
				awacs_reg[1] = r1;
				awacs_write(r1 | MASK_ADDR_MUTE);
			} else if (((status & MASK_HDPCONN) >> 3 == 0) && (hdp_connected == 1)) {
				hdp_connected = 0;
				
				r1 = awacs_reg[1] & ~MASK_SPKMUTE;
				awacs_reg[1] = r1;
				awacs_write(r1 | MASK_ADDR_MUTE);
			}
		}
	}
	if (ctrl & MASK_CNTLERR) {
		int err = (in_le32(&awacs->codec_stat) & MASK_ERRCODE) >> 16;
		/* CHECK: we just swallow burgundy errors at the moment..*/
		if (err != 0 && awacs_revision != AWACS_BURGUNDY)
			printk(KERN_ERR "dmasound_pmac: error %x\n", err);
	}
	/* Writing 1s to the CNTLERR and PORTCHG bits clears them... */
	out_le32(&awacs->control, ctrl);
	spin_unlock(&dmasound.lock);
	return IRQ_HANDLED;
}

static void
awacs_write(int val)
{
	int count = 300 ;
	if (awacs_revision >= AWACS_DACA || !awacs)
		return ;

	while ((in_le32(&awacs->codec_ctrl) & MASK_NEWECMD) && count--)
		udelay(1) ;	/* timeout is > 2 samples at lowest rate */
	out_le32(&awacs->codec_ctrl, val | (awacs_subframe << 22));
	(void)in_le32(&awacs->byteswap);
}

/* this is called when the beep timer expires... it will be called even
   if the beep has been overidden by other sound output.
*/
static void awacs_nosound(unsigned long xx)
{
	unsigned long flags;
	int count = 600 ; /* > four samples at lowest rate */

	spin_lock_irqsave(&dmasound.lock, flags);
	if (beep_playing) {
		st_le16(&beep_dbdma_cmd->command, DBDMA_STOP);
		out_le32(&awacs_txdma->control, (RUN|PAUSE|FLUSH|WAKE) << 16);
		while ((in_le32(&awacs_txdma->status) & RUN) && count--)
			udelay(1);
		if (awacs)
			awacs_setup_for_beep(-1);
		beep_playing = 0;
	}
	spin_unlock_irqrestore(&dmasound.lock, flags);
}

/*
 * We generate the beep with a single dbdma command that loops a buffer
 * forever - without generating interrupts.
 *
 * So, to stop it you have to stop dma output as per awacs_nosound.
 */
static int awacs_beep_event(struct input_dev *dev, unsigned int type,
		unsigned int code, int hz)
{
	unsigned long flags;
	int beep_speed = 0;
	int srate;
	int period, ncycles, nsamples;
	int i, j, f;
	short *p;
	static int beep_hz_cache;
	static int beep_nsamples_cache;
	static int beep_volume_cache;

	if (type != EV_SND)
		return -1;
	switch (code) {
	case SND_BELL:
		if (hz)
			hz = 1000;
		break;
	case SND_TONE:
		break;
	default:
		return -1;
	}

	if (beep_buf == NULL)
		return -1;

	/* quick-hack fix for DACA, Burgundy & Tumbler */

	if (awacs_revision >= AWACS_DACA){
		srate = 44100 ;
	} else {
		for (i = 0; i < 8 && awacs_freqs[i] >= BEEP_SRATE; ++i)
			if (awacs_freqs_ok[i])
				beep_speed = i;
		srate = awacs_freqs[beep_speed];
	}

	if (hz <= srate / BEEP_BUFLEN || hz > srate / 2) {
		/* cancel beep currently playing */
		awacs_nosound(0);
		return 0;
	}

	spin_lock_irqsave(&dmasound.lock, flags);
	if (beep_playing || write_sq.active || beep_buf == NULL) {
		spin_unlock_irqrestore(&dmasound.lock, flags);
		return -1;		/* too hard, sorry :-( */
	}
	beep_playing = 1;
	st_le16(&beep_dbdma_cmd->command, OUTPUT_MORE + BR_ALWAYS);
	spin_unlock_irqrestore(&dmasound.lock, flags);

	if (hz == beep_hz_cache && beep_vol == beep_volume_cache) {
		nsamples = beep_nsamples_cache;
	} else {
		period = srate * 256 / hz;	/* fixed point */
		ncycles = BEEP_BUFLEN * 256 / period;
		nsamples = (period * ncycles) >> 8;
		f = ncycles * 65536 / nsamples;
		j = 0;
		p = beep_buf;
		for (i = 0; i < nsamples; ++i, p += 2) {
			p[0] = p[1] = beep_wform[j >> 8] * beep_vol;
			j = (j + f) & 0xffff;
		}
		beep_hz_cache = hz;
		beep_volume_cache = beep_vol;
		beep_nsamples_cache = nsamples;
	}

	st_le16(&beep_dbdma_cmd->req_count, nsamples*4);
	st_le16(&beep_dbdma_cmd->xfer_status, 0);
	st_le32(&beep_dbdma_cmd->cmd_dep, virt_to_bus(beep_dbdma_cmd));
	st_le32(&beep_dbdma_cmd->phy_addr, virt_to_bus(beep_buf));
	awacs_beep_state = 1;

	spin_lock_irqsave(&dmasound.lock, flags);
	if (beep_playing) {	/* i.e. haven't been terminated already */
		int count = 300 ;
		out_le32(&awacs_txdma->control, (RUN|WAKE|FLUSH|PAUSE) << 16);
		while ((in_le32(&awacs_txdma->status) & RUN) && count--)
			udelay(1); /* timeout > 2 samples at lowest rate*/
		if (awacs)
			awacs_setup_for_beep(beep_speed);
		out_le32(&awacs_txdma->cmdptr, virt_to_bus(beep_dbdma_cmd));
		(void)in_le32(&awacs_txdma->status);
		out_le32(&awacs_txdma->control, RUN | (RUN << 16));
	}
	spin_unlock_irqrestore(&dmasound.lock, flags);

	return 0;
}

/* used in init and for wake-up */

static void
load_awacs(void)
{
	awacs_write(awacs_reg[0] + MASK_ADDR0);
	awacs_write(awacs_reg[1] + MASK_ADDR1);
	awacs_write(awacs_reg[2] + MASK_ADDR2);
	awacs_write(awacs_reg[4] + MASK_ADDR4);

	if (awacs_revision == AWACS_SCREAMER) {
		awacs_write(awacs_reg[5] + MASK_ADDR5);
		msleep(100);
		awacs_write(awacs_reg[6] + MASK_ADDR6);
		msleep(2);
		awacs_write(awacs_reg[1] + MASK_ADDR1);
		awacs_write(awacs_reg[7] + MASK_ADDR7);
	}
	if (awacs) {
		if (hw_can_byteswap && (dmasound.hard.format == AFMT_S16_LE))
			out_le32(&awacs->byteswap, BS_VAL);
		else
			out_le32(&awacs->byteswap, 0);
	}
}

#ifdef CONFIG_PM
/*
 * Save state when going to sleep, restore it afterwards.
 */
/* FIXME: sort out disabling/re-enabling of read stuff as well */
static int awacs_sleep_notify(struct pmu_sleep_notifier *self, int when)
{
	unsigned long flags;

	switch (when) {
	case PBOOK_SLEEP_NOW:		
		LOCK();
		awacs_sleeping = 1;
		/* Tell the rest of the driver we are now going to sleep */
		mb();
		if (awacs_revision == AWACS_SCREAMER ||
		    awacs_revision == AWACS_AWACS) {
			awacs_reg1_save = awacs_reg[1];
			awacs_reg[1] |= MASK_AMUTE | MASK_CMUTE;
			awacs_write(MASK_ADDR1 | awacs_reg[1]);
		}

		PMacSilence();
		/* stop rx - if going - a bit of a daft user... but */
		out_le32(&awacs_rxdma->control, (RUN|WAKE|FLUSH << 16));
		/* deny interrupts */
		if (awacs)
			disable_irq(awacs_irq);
		disable_irq(awacs_tx_irq);
		disable_irq(awacs_rx_irq);
		/* Chip specific sleep code */
		switch (awacs_revision) {
			case AWACS_TUMBLER:
			case AWACS_SNAPPER:
				write_audio_gpio(gpio_headphone_mute, gpio_headphone_mute_pol);
				write_audio_gpio(gpio_amp_mute, gpio_amp_mute_pol);
				tas_enter_sleep();
				write_audio_gpio(gpio_audio_reset, gpio_audio_reset_pol);
				break ;
			case AWACS_DACA:
				daca_enter_sleep();
				break ;
			case AWACS_BURGUNDY:
				break ;
			case AWACS_SCREAMER:
			case AWACS_AWACS:
			default:
				out_le32(&awacs->control, 0x11) ;
				break ;
		}
		/* Disable sound clock */
		pmac_call_feature(PMAC_FTR_SOUND_CHIP_ENABLE, awacs_node, 0, 0);
		/* According to Darwin, we do that after turning off the sound
		 * chip clock. All this will have to be cleaned up once we properly
		 * parse the OF sound-objects
		 */
		if ((machine_is_compatible("PowerBook3,1") ||
		    machine_is_compatible("PowerBook3,2")) && awacs) {
			awacs_reg[1] |= MASK_PAROUT0 | MASK_PAROUT1;
			awacs_write(MASK_ADDR1 | awacs_reg[1]);
			msleep(200);
		}
		break;
	case PBOOK_WAKE:
		/* Enable sound clock */
		pmac_call_feature(PMAC_FTR_SOUND_CHIP_ENABLE, awacs_node, 0, 1);
		if ((machine_is_compatible("PowerBook3,1") ||
		    machine_is_compatible("PowerBook3,2")) && awacs) {
			msleep(100);
			awacs_reg[1] &= ~(MASK_PAROUT0 | MASK_PAROUT1);
			awacs_write(MASK_ADDR1 | awacs_reg[1]);
			msleep(300);
		} else
			msleep(1000);
 		/* restore settings */
		switch (awacs_revision) {
			case AWACS_TUMBLER:
			case AWACS_SNAPPER:
				write_audio_gpio(gpio_headphone_mute, gpio_headphone_mute_pol);
				write_audio_gpio(gpio_amp_mute, gpio_amp_mute_pol);
				write_audio_gpio(gpio_audio_reset, gpio_audio_reset_pol);
				msleep(100);
				write_audio_gpio(gpio_audio_reset, !gpio_audio_reset_pol);
				msleep(150);
				tas_leave_sleep(); /* Stub for now */
				headphone_intr(0, NULL);
				break;
			case AWACS_DACA:
				msleep(10); /* Check this !!! */
				daca_leave_sleep();
				break ;		/* dont know how yet */
			case AWACS_BURGUNDY:
				break ;
			case AWACS_SCREAMER:
			case AWACS_AWACS:
			default:
		 		load_awacs() ;
				break ;
		}
		/* Recalibrate chip */
		if (awacs_revision == AWACS_SCREAMER && awacs)
			awacs_recalibrate();
		/* Make sure dma is stopped */
		PMacSilence();
		if (awacs)
			enable_irq(awacs_irq);
		enable_irq(awacs_tx_irq);
 		enable_irq(awacs_rx_irq);
 		if (awacs) {
 			/* OK, allow ints back again */
	 		out_le32(&awacs->control, MASK_IEPC
 			 	| (awacs_rate_index << 8) | 0x11
 				 | (awacs_revision < AWACS_DACA ? MASK_IEE: 0));
 		}
 		if (macio_base && is_pbook_g3) {
			/* FIXME: should restore the setup we had...*/
			out_8(macio_base + 0x37, 3);
 		} else if (is_pbook_3X00) {
			in_8(latch_base + 0x190);
		}
		/* Remove mute */
		if (awacs_revision == AWACS_SCREAMER ||
		    awacs_revision == AWACS_AWACS) {
			awacs_reg[1] = awacs_reg1_save;
			awacs_write(MASK_ADDR1 | awacs_reg[1]);
		}
 		awacs_sleeping = 0;
		/* Resume pending sounds. */
		/* we don't try to restart input... */
		spin_lock_irqsave(&dmasound.lock, flags);
		__PMacPlay();
		spin_unlock_irqrestore(&dmasound.lock, flags);
		UNLOCK();
	}
	return PBOOK_SLEEP_OK;
}
#endif /* CONFIG_PM */


/* All the burgundy functions: */

/* Waits for busy flag to clear */
static inline void
awacs_burgundy_busy_wait(void)
{
	int count = 50; /* > 2 samples at 44k1 */
	while ((in_le32(&awacs->codec_ctrl) & MASK_NEWECMD) && count--)
		udelay(1) ;
}

static inline void
awacs_burgundy_extend_wait(void)
{
	int count = 50 ; /* > 2 samples at 44k1 */
	while ((!(in_le32(&awacs->codec_stat) & MASK_EXTEND)) && count--)
		udelay(1) ;
	count = 50;
	while ((in_le32(&awacs->codec_stat) & MASK_EXTEND) && count--)
		udelay(1);
}

static void
awacs_burgundy_wcw(unsigned addr, unsigned val)
{
	out_le32(&awacs->codec_ctrl, addr + 0x200c00 + (val & 0xff));
	awacs_burgundy_busy_wait();
	out_le32(&awacs->codec_ctrl, addr + 0x200d00 +((val>>8) & 0xff));
	awacs_burgundy_busy_wait();
	out_le32(&awacs->codec_ctrl, addr + 0x200e00 +((val>>16) & 0xff));
	awacs_burgundy_busy_wait();
	out_le32(&awacs->codec_ctrl, addr + 0x200f00 +((val>>24) & 0xff));
	awacs_burgundy_busy_wait();
}

static unsigned
awacs_burgundy_rcw(unsigned addr)
{
	unsigned val = 0;
	unsigned long flags;

	/* should have timeouts here */
	spin_lock_irqsave(&dmasound.lock, flags);

	out_le32(&awacs->codec_ctrl, addr + 0x100000);
	awacs_burgundy_busy_wait();
	awacs_burgundy_extend_wait();
	val += (in_le32(&awacs->codec_stat) >> 4) & 0xff;

	out_le32(&awacs->codec_ctrl, addr + 0x100100);
	awacs_burgundy_busy_wait();
	awacs_burgundy_extend_wait();
	val += ((in_le32(&awacs->codec_stat)>>4) & 0xff) <<8;

	out_le32(&awacs->codec_ctrl, addr + 0x100200);
	awacs_burgundy_busy_wait();
	awacs_burgundy_extend_wait();
	val += ((in_le32(&awacs->codec_stat)>>4) & 0xff) <<16;

	out_le32(&awacs->codec_ctrl, addr + 0x100300);
	awacs_burgundy_busy_wait();
	awacs_burgundy_extend_wait();
	val += ((in_le32(&awacs->codec_stat)>>4) & 0xff) <<24;

	spin_unlock_irqrestore(&dmasound.lock, flags);

	return val;
}


static void
awacs_burgundy_wcb(unsigned addr, unsigned val)
{
	out_le32(&awacs->codec_ctrl, addr + 0x300000 + (val & 0xff));
	awacs_burgundy_busy_wait();
}

static unsigned
awacs_burgundy_rcb(unsigned addr)
{
	unsigned val = 0;
	unsigned long flags;

	/* should have timeouts here */
	spin_lock_irqsave(&dmasound.lock, flags);

	out_le32(&awacs->codec_ctrl, addr + 0x100000);
	awacs_burgundy_busy_wait();
	awacs_burgundy_extend_wait();
	val += (in_le32(&awacs->codec_stat) >> 4) & 0xff;

	spin_unlock_irqrestore(&dmasound.lock, flags);

	return val;
}

static int
awacs_burgundy_check(void)
{
	/* Checks to see the chip is alive and kicking */
	int error = in_le32(&awacs->codec_ctrl) & MASK_ERRCODE;

	return error == 0xf0000;
}

static int
awacs_burgundy_init(void)
{
	if (awacs_burgundy_check()) {
		printk(KERN_WARNING "dmasound_pmac: burgundy not working :-(\n");
		return 1;
	}

	awacs_burgundy_wcb(MASK_ADDR_BURGUNDY_OUTPUTENABLES,
			   DEF_BURGUNDY_OUTPUTENABLES);
	awacs_burgundy_wcb(MASK_ADDR_BURGUNDY_MORE_OUTPUTENABLES,
			   DEF_BURGUNDY_MORE_OUTPUTENABLES);
	awacs_burgundy_wcw(MASK_ADDR_BURGUNDY_OUTPUTSELECTS,
			   DEF_BURGUNDY_OUTPUTSELECTS);

	awacs_burgundy_wcb(MASK_ADDR_BURGUNDY_INPSEL21,
			   DEF_BURGUNDY_INPSEL21);
	awacs_burgundy_wcb(MASK_ADDR_BURGUNDY_INPSEL3,
			   DEF_BURGUNDY_INPSEL3);
	awacs_burgundy_wcb(MASK_ADDR_BURGUNDY_GAINCD,
			   DEF_BURGUNDY_GAINCD);
	awacs_burgundy_wcb(MASK_ADDR_BURGUNDY_GAINLINE,
			   DEF_BURGUNDY_GAINLINE);
	awacs_burgundy_wcb(MASK_ADDR_BURGUNDY_GAINMIC,
			   DEF_BURGUNDY_GAINMIC);
	awacs_burgundy_wcb(MASK_ADDR_BURGUNDY_GAINMODEM,
			   DEF_BURGUNDY_GAINMODEM);

	awacs_burgundy_wcb(MASK_ADDR_BURGUNDY_ATTENSPEAKER,
			   DEF_BURGUNDY_ATTENSPEAKER);
	awacs_burgundy_wcb(MASK_ADDR_BURGUNDY_ATTENLINEOUT,
			   DEF_BURGUNDY_ATTENLINEOUT);
	awacs_burgundy_wcb(MASK_ADDR_BURGUNDY_ATTENHP,
			   DEF_BURGUNDY_ATTENHP);

	awacs_burgundy_wcw(MASK_ADDR_BURGUNDY_MASTER_VOLUME,
			   DEF_BURGUNDY_MASTER_VOLUME);
	awacs_burgundy_wcw(MASK_ADDR_BURGUNDY_VOLCD,
			   DEF_BURGUNDY_VOLCD);
	awacs_burgundy_wcw(MASK_ADDR_BURGUNDY_VOLLINE,
			   DEF_BURGUNDY_VOLLINE);
	awacs_burgundy_wcw(MASK_ADDR_BURGUNDY_VOLMIC,
			   DEF_BURGUNDY_VOLMIC);
	return 0;
}

static void
awacs_burgundy_write_volume(unsigned address, int volume)
{
	int hardvolume,lvolume,rvolume;

	lvolume = (volume & 0xff) ? (volume & 0xff) + 155 : 0;
	rvolume = ((volume >>8)&0xff) ? ((volume >> 8)&0xff ) + 155 : 0;

	hardvolume = lvolume + (rvolume << 16);

	awacs_burgundy_wcw(address, hardvolume);
}

static int
awacs_burgundy_read_volume(unsigned address)
{
	int softvolume,wvolume;

	wvolume = awacs_burgundy_rcw(address);

	softvolume = (wvolume & 0xff) - 155;
	softvolume += (((wvolume >> 16) & 0xff) - 155)<<8;

	return softvolume > 0 ? softvolume : 0;
}

static int
awacs_burgundy_read_mvolume(unsigned address)
{
	int lvolume,rvolume,wvolume;

	wvolume = awacs_burgundy_rcw(address);

	wvolume &= 0xffff;

	rvolume = (wvolume & 0xff) - 155;
	lvolume = ((wvolume & 0xff00)>>8) - 155;

	return lvolume + (rvolume << 8);
}

static void
awacs_burgundy_write_mvolume(unsigned address, int volume)
{
	int lvolume,rvolume,hardvolume;

	lvolume = (volume &0xff) ? (volume & 0xff) + 155 :0;
	rvolume = ((volume >>8) & 0xff) ? (volume >> 8) + 155 :0;

	hardvolume = lvolume + (rvolume << 8);
	hardvolume += (hardvolume << 16);

	awacs_burgundy_wcw(address, hardvolume);
}

/* End burgundy functions */

/* Set up output volumes on machines with the 'perch/whisper' extension card.
 * this has an SGS i2c chip (7433) which is accessed using the cuda.
 *
 * TODO: split this out and make use of the other parts of the SGS chip to
 * do Bass, Treble etc.
 */

static void
awacs_enable_amp(int spkr_vol)
{
#ifdef CONFIG_ADB_CUDA
	struct adb_request req;

	if (sys_ctrler != SYS_CTRLER_CUDA)
		return;

	/* turn on headphones */
	cuda_request(&req, NULL, 5, CUDA_PACKET, CUDA_GET_SET_IIC,
		     0x8a, 4, 0);
	while (!req.complete) cuda_poll();
	cuda_request(&req, NULL, 5, CUDA_PACKET, CUDA_GET_SET_IIC,
		     0x8a, 6, 0);
	while (!req.complete) cuda_poll();

	/* turn on speaker */
	cuda_request(&req, NULL, 5, CUDA_PACKET, CUDA_GET_SET_IIC,
		     0x8a, 3, (100 - (spkr_vol & 0xff)) * 32 / 100);
	while (!req.complete) cuda_poll();
	cuda_request(&req, NULL, 5, CUDA_PACKET, CUDA_GET_SET_IIC,
		     0x8a, 5, (100 - ((spkr_vol >> 8) & 0xff)) * 32 / 100);
	while (!req.complete) cuda_poll();

	cuda_request(&req, NULL, 5, CUDA_PACKET,
		     CUDA_GET_SET_IIC, 0x8a, 1, 0x29);
	while (!req.complete) cuda_poll();
#endif /* CONFIG_ADB_CUDA */
}


/*** Mid level stuff *********************************************************/


/*
 * /dev/mixer abstraction
 */

static void do_line_lev(int data)
{
		line_lev = data ;
		awacs_reg[0] &= ~MASK_MUX_AUDIN;
		if ((data & 0xff) >= 50)
			awacs_reg[0] |= MASK_MUX_AUDIN;
		awacs_write(MASK_ADDR0 | awacs_reg[0]);
}

static void do_ip_gain(int data)
{
	ip_gain = data ;
	data &= 0xff;
	awacs_reg[0] &= ~MASK_GAINLINE;
	if (awacs_revision == AWACS_SCREAMER) {
		awacs_reg[6] &= ~MASK_MIC_BOOST ;
		if (data >= 33) {
			awacs_reg[0] |= MASK_GAINLINE;
			if( data >= 66)
				awacs_reg[6] |= MASK_MIC_BOOST ;
		}
		awacs_write(MASK_ADDR6 | awacs_reg[6]) ;
	} else {
		if (data >= 50)
			awacs_reg[0] |= MASK_GAINLINE;
	}
	awacs_write(MASK_ADDR0 | awacs_reg[0]);
}

static void do_mic_lev(int data)
{
	mic_lev = data ;
	data &= 0xff;
	awacs_reg[0] &= ~MASK_MUX_MIC;
	if (data >= 50)
		awacs_reg[0] |= MASK_MUX_MIC;
	awacs_write(MASK_ADDR0 | awacs_reg[0]);
}

static void do_cd_lev(int data)
{
	cd_lev = data ;
	awacs_reg[0] &= ~MASK_MUX_CD;
	if ((data & 0xff) >= 50)
		awacs_reg[0] |= MASK_MUX_CD;
	awacs_write(MASK_ADDR0 | awacs_reg[0]);
}

static void do_rec_lev(int data)
{
	int left, right ;
	rec_lev = data ;
	/* need to fudge this to use the volume setter routine */
	left = 100 - (data & 0xff) ; if( left < 0 ) left = 0 ;
	right = 100 - ((data >> 8) & 0xff) ; if( right < 0 ) right = 0 ;
	left |= (right << 8 );
	left = awacs_volume_setter(left, 0, 0, 4);
}

static void do_passthru_vol(int data)
{
	passthru_vol = data ;
	awacs_reg[1] &= ~MASK_LOOPTHRU;
	if (awacs_revision == AWACS_SCREAMER) {
		if( data ) { /* switch it on for non-zero */
			awacs_reg[1] |= MASK_LOOPTHRU;
			awacs_write(MASK_ADDR1 | awacs_reg[1]);
		}
		data = awacs_volume_setter(data, 5, 0, 6) ;
	} else {
		if ((data & 0xff) >= 50)
			awacs_reg[1] |= MASK_LOOPTHRU;
		awacs_write(MASK_ADDR1 | awacs_reg[1]);
		data = (awacs_reg[1] & MASK_LOOPTHRU)? 100: 0;
	}
}

static int awacs_mixer_ioctl(u_int cmd, u_long arg)
{
	int data;
	int rc;

	switch (cmd) {
	case SOUND_MIXER_READ_CAPS:
		/* say we will allow multiple inputs?  prob. wrong
			so I'm switching it to single */
		return IOCTL_OUT(arg, 1);
	case SOUND_MIXER_READ_DEVMASK:
		data  = SOUND_MASK_VOLUME | SOUND_MASK_SPEAKER
			| SOUND_MASK_LINE | SOUND_MASK_MIC | SOUND_MASK_CD
			| SOUND_MASK_IGAIN | SOUND_MASK_RECLEV
			| SOUND_MASK_ALTPCM
			| SOUND_MASK_MONITOR;
		rc = IOCTL_OUT(arg, data);
		break;
	case SOUND_MIXER_READ_RECMASK:
		data = SOUND_MASK_LINE | SOUND_MASK_MIC | SOUND_MASK_CD;
		rc = IOCTL_OUT(arg, data);
		break;
	case SOUND_MIXER_READ_RECSRC:
		data = 0;
		if (awacs_reg[0] & MASK_MUX_AUDIN)
			data |= SOUND_MASK_LINE;
		if (awacs_reg[0] & MASK_MUX_MIC)
			data |= SOUND_MASK_MIC;
		if (awacs_reg[0] & MASK_MUX_CD)
			data |= SOUND_MASK_CD;
		rc = IOCTL_OUT(arg, data);
		break;
	case SOUND_MIXER_WRITE_RECSRC:
		IOCTL_IN(arg, data);
		data &= (SOUND_MASK_LINE | SOUND_MASK_MIC | SOUND_MASK_CD);
		awacs_reg[0] &= ~(MASK_MUX_CD | MASK_MUX_MIC
				  | MASK_MUX_AUDIN);
		if (data & SOUND_MASK_LINE)
			awacs_reg[0] |= MASK_MUX_AUDIN;
		if (data & SOUND_MASK_MIC)
			awacs_reg[0] |= MASK_MUX_MIC;
		if (data & SOUND_MASK_CD)
			awacs_reg[0] |= MASK_MUX_CD;
		awacs_write(awacs_reg[0] | MASK_ADDR0);
		rc = IOCTL_OUT(arg, data);
		break;
	case SOUND_MIXER_READ_STEREODEVS:
		data = SOUND_MASK_VOLUME | SOUND_MASK_SPEAKER| SOUND_MASK_RECLEV  ;
		if (awacs_revision == AWACS_SCREAMER)
			data |= SOUND_MASK_MONITOR ;
		rc = IOCTL_OUT(arg, data);
		break;
	case SOUND_MIXER_WRITE_VOLUME:
		IOCTL_IN(arg, data);
		line_vol = data ;
		awacs_volume_setter(data, 2, 0, 6);
		/* fall through */
	case SOUND_MIXER_READ_VOLUME:
		rc = IOCTL_OUT(arg, line_vol);
		break;
	case SOUND_MIXER_WRITE_SPEAKER:
		IOCTL_IN(arg, data);
		spk_vol = data ;
		if (has_perch)
			awacs_enable_amp(data);
		else
			(void)awacs_volume_setter(data, 4, MASK_CMUTE, 6);
		/* fall though */
	case SOUND_MIXER_READ_SPEAKER:
		rc = IOCTL_OUT(arg, spk_vol);
		break;
	case SOUND_MIXER_WRITE_ALTPCM:	/* really bell volume */
		IOCTL_IN(arg, data);
		beep_vol = data & 0xff;
		/* fall through */
	case SOUND_MIXER_READ_ALTPCM:
		rc = IOCTL_OUT(arg, beep_vol);
		break;
	case SOUND_MIXER_WRITE_LINE:
		IOCTL_IN(arg, data);
		do_line_lev(data) ;
		/* fall through */
	case SOUND_MIXER_READ_LINE:
		rc = IOCTL_OUT(arg, line_lev);
		break;
	case SOUND_MIXER_WRITE_IGAIN:
		IOCTL_IN(arg, data);
		do_ip_gain(data) ;
		/* fall through */
	case SOUND_MIXER_READ_IGAIN:
		rc = IOCTL_OUT(arg, ip_gain);
		break;
	case SOUND_MIXER_WRITE_MIC:
		IOCTL_IN(arg, data);
		do_mic_lev(data);
		/* fall through */
	case SOUND_MIXER_READ_MIC:
		rc = IOCTL_OUT(arg, mic_lev);
		break;
	case SOUND_MIXER_WRITE_CD:
		IOCTL_IN(arg, data);
		do_cd_lev(data);
		/* fall through */
	case SOUND_MIXER_READ_CD:
		rc = IOCTL_OUT(arg, cd_lev);
		break;
	case SOUND_MIXER_WRITE_RECLEV:
		IOCTL_IN(arg, data);
		do_rec_lev(data) ;
		/* fall through */
	case SOUND_MIXER_READ_RECLEV:
		rc = IOCTL_OUT(arg, rec_lev);
		break;
	case MIXER_WRITE(SOUND_MIXER_MONITOR):
		IOCTL_IN(arg, data);
		do_passthru_vol(data) ;
		/* fall through */
	case MIXER_READ(SOUND_MIXER_MONITOR):
		rc = IOCTL_OUT(arg, passthru_vol);
		break;
	default:
		rc = -EINVAL;
	}
	
	return rc;
}

static void awacs_mixer_init(void)
{
	awacs_volume_setter(line_vol, 2, 0, 6);
	if (has_perch)
		awacs_enable_amp(spk_vol);
	else
		(void)awacs_volume_setter(spk_vol, 4, MASK_CMUTE, 6);
	do_line_lev(line_lev) ;
	do_ip_gain(ip_gain) ;
	do_mic_lev(mic_lev) ;
	do_cd_lev(cd_lev) ;
	do_rec_lev(rec_lev) ;
	do_passthru_vol(passthru_vol) ;
}

static int burgundy_mixer_ioctl(u_int cmd, u_long arg)
{
	int data;
	int rc;

	/* We are, we are, we are... Burgundy or better */
	switch(cmd) {
	case SOUND_MIXER_READ_DEVMASK:
		data = SOUND_MASK_VOLUME | SOUND_MASK_CD |
			SOUND_MASK_LINE | SOUND_MASK_MIC |
			SOUND_MASK_SPEAKER | SOUND_MASK_ALTPCM;
		rc = IOCTL_OUT(arg, data);
		break;
	case SOUND_MIXER_READ_RECMASK:
		data = SOUND_MASK_LINE | SOUND_MASK_MIC
			| SOUND_MASK_CD;
		rc = IOCTL_OUT(arg, data);
		break;
	case SOUND_MIXER_READ_RECSRC:
		data = 0;
		if (awacs_reg[0] & MASK_MUX_AUDIN)
			data |= SOUND_MASK_LINE;
		if (awacs_reg[0] & MASK_MUX_MIC)
			data |= SOUND_MASK_MIC;
		if (awacs_reg[0] & MASK_MUX_CD)
			data |= SOUND_MASK_CD;
		rc = IOCTL_OUT(arg, data);
		break;
	case SOUND_MIXER_WRITE_RECSRC:
		IOCTL_IN(arg, data);
		data &= (SOUND_MASK_LINE
			 | SOUND_MASK_MIC | SOUND_MASK_CD);
		awacs_reg[0] &= ~(MASK_MUX_CD | MASK_MUX_MIC
				  | MASK_MUX_AUDIN);
		if (data & SOUND_MASK_LINE)
			awacs_reg[0] |= MASK_MUX_AUDIN;
		if (data & SOUND_MASK_MIC)
			awacs_reg[0] |= MASK_MUX_MIC;
		if (data & SOUND_MASK_CD)
			awacs_reg[0] |= MASK_MUX_CD;
		awacs_write(awacs_reg[0] | MASK_ADDR0);
		rc = IOCTL_OUT(arg, data);
		break;
	case SOUND_MIXER_READ_STEREODEVS:
		data = SOUND_MASK_VOLUME | SOUND_MASK_SPEAKER
			| SOUND_MASK_RECLEV | SOUND_MASK_CD
			| SOUND_MASK_LINE;
		rc = IOCTL_OUT(arg, data);
		break;
	case SOUND_MIXER_READ_CAPS:
		rc = IOCTL_OUT(arg, 0);
		break;
	case SOUND_MIXER_WRITE_VOLUME:
		IOCTL_IN(arg, data);
		awacs_burgundy_write_mvolume(MASK_ADDR_BURGUNDY_MASTER_VOLUME, data);
				/* Fall through */
	case SOUND_MIXER_READ_VOLUME:
		rc = IOCTL_OUT(arg, awacs_burgundy_read_mvolume(MASK_ADDR_BURGUNDY_MASTER_VOLUME));
		break;
	case SOUND_MIXER_WRITE_SPEAKER:
		IOCTL_IN(arg, data);
		if (!(data & 0xff)) {
			/* Mute the left speaker */
			awacs_burgundy_wcb(MASK_ADDR_BURGUNDY_MORE_OUTPUTENABLES,
					   awacs_burgundy_rcb(MASK_ADDR_BURGUNDY_MORE_OUTPUTENABLES) & ~0x2);
		} else {
			/* Unmute the left speaker */
			awacs_burgundy_wcb(MASK_ADDR_BURGUNDY_MORE_OUTPUTENABLES,
					   awacs_burgundy_rcb(MASK_ADDR_BURGUNDY_MORE_OUTPUTENABLES) | 0x2);
		}
		if (!(data & 0xff00)) {
			/* Mute the right speaker */
			awacs_burgundy_wcb(MASK_ADDR_BURGUNDY_MORE_OUTPUTENABLES,
					   awacs_burgundy_rcb(MASK_ADDR_BURGUNDY_MORE_OUTPUTENABLES) & ~0x4);
		} else {
			/* Unmute the right speaker */
			awacs_burgundy_wcb(MASK_ADDR_BURGUNDY_MORE_OUTPUTENABLES,
					   awacs_burgundy_rcb(MASK_ADDR_BURGUNDY_MORE_OUTPUTENABLES) | 0x4);
		}

		data = (((data&0xff)*16)/100 > 0xf ? 0xf :
			(((data&0xff)*16)/100)) +
			((((data>>8)*16)/100 > 0xf ? 0xf :
			  ((((data>>8)*16)/100)))<<4);

		awacs_burgundy_wcb(MASK_ADDR_BURGUNDY_ATTENSPEAKER, ~data);
				/* Fall through */
	case SOUND_MIXER_READ_SPEAKER:
		data = awacs_burgundy_rcb(MASK_ADDR_BURGUNDY_ATTENSPEAKER);
		data = (((data & 0xf)*100)/16) + ((((data>>4)*100)/16)<<8);
		rc = IOCTL_OUT(arg, (~data) & 0x0000ffff);
		break;
	case SOUND_MIXER_WRITE_ALTPCM:	/* really bell volume */
		IOCTL_IN(arg, data);
		beep_vol = data & 0xff;
				/* fall through */
	case SOUND_MIXER_READ_ALTPCM:
		rc = IOCTL_OUT(arg, beep_vol);
		break;
	case SOUND_MIXER_WRITE_LINE:
		IOCTL_IN(arg, data);
		awacs_burgundy_write_volume(MASK_ADDR_BURGUNDY_VOLLINE, data);

				/* fall through */
	case SOUND_MIXER_READ_LINE:
		data = awacs_burgundy_read_volume(MASK_ADDR_BURGUNDY_VOLLINE);
		rc = IOCTL_OUT(arg, data);
		break;
	case SOUND_MIXER_WRITE_MIC:
		IOCTL_IN(arg, data);
				/* Mic is mono device */
		data = (data << 8) + (data << 24);
		awacs_burgundy_write_volume(MASK_ADDR_BURGUNDY_VOLMIC, data);
				/* fall through */
	case SOUND_MIXER_READ_MIC:
		data = awacs_burgundy_read_volume(MASK_ADDR_BURGUNDY_VOLMIC);
		data <<= 24;
		rc = IOCTL_OUT(arg, data);
		break;
	case SOUND_MIXER_WRITE_CD:
		IOCTL_IN(arg, data);
		awacs_burgundy_write_volume(MASK_ADDR_BURGUNDY_VOLCD, data);
				/* fall through */
	case SOUND_MIXER_READ_CD:
		data = awacs_burgundy_read_volume(MASK_ADDR_BURGUNDY_VOLCD);
		rc = IOCTL_OUT(arg, data);
		break;
	case SOUND_MIXER_WRITE_RECLEV:
		IOCTL_IN(arg, data);
		data = awacs_volume_setter(data, 0, 0, 4);
		rc = IOCTL_OUT(arg, data);
		break;
	case SOUND_MIXER_READ_RECLEV:
		data = awacs_get_volume(awacs_reg[0], 4);
		rc = IOCTL_OUT(arg, data);
		break;
	case SOUND_MIXER_OUTMASK:
	case SOUND_MIXER_OUTSRC:
	default:
		rc = -EINVAL;
	}
	
	return rc;
}

static int daca_mixer_ioctl(u_int cmd, u_long arg)
{
	int data;
	int rc;

	/* And the DACA's no genius either! */

	switch(cmd) {
	case SOUND_MIXER_READ_DEVMASK:
		data = SOUND_MASK_VOLUME;
		rc = IOCTL_OUT(arg, data);
		break;
	case SOUND_MIXER_READ_RECMASK:
		data = 0;
		rc = IOCTL_OUT(arg, data);
		break;
	case SOUND_MIXER_READ_RECSRC:
		data = 0;
		rc = IOCTL_OUT(arg, data);
		break;
	case SOUND_MIXER_WRITE_RECSRC:
		IOCTL_IN(arg, data);
		data =0;
		rc = IOCTL_OUT(arg, data);
		break;
	case SOUND_MIXER_READ_STEREODEVS:
		data = SOUND_MASK_VOLUME;
		rc = IOCTL_OUT(arg, data);
		break;
	case SOUND_MIXER_READ_CAPS:
		rc = IOCTL_OUT(arg, 0);
		break;
	case SOUND_MIXER_WRITE_VOLUME:
		IOCTL_IN(arg, data);
		daca_set_volume(data, data);
		/* Fall through */
	case SOUND_MIXER_READ_VOLUME:
		daca_get_volume(& data, &data);
		rc = IOCTL_OUT(arg, data);
		break;
	case SOUND_MIXER_OUTMASK:
	case SOUND_MIXER_OUTSRC:
	default:
		rc = -EINVAL;
	}
	return rc;
}

static int PMacMixerIoctl(u_int cmd, u_long arg)
{
	int rc;
	
	/* Different IOCTLS for burgundy and, eventually, DACA & Tumbler */

	TRY_LOCK();
	
	switch (awacs_revision){
		case AWACS_BURGUNDY:
			rc = burgundy_mixer_ioctl(cmd, arg);
			break ;
		case AWACS_DACA:
			rc = daca_mixer_ioctl(cmd, arg);
			break;
		case AWACS_TUMBLER:
		case AWACS_SNAPPER:
			rc = tas_mixer_ioctl(cmd, arg);
			break ;
		default: /* ;-)) */
			rc = awacs_mixer_ioctl(cmd, arg);
	}

	UNLOCK();
	
	return rc;
}

static void PMacMixerInit(void)
{
	switch (awacs_revision) {
		case AWACS_TUMBLER:
		  printk("AE-Init tumbler mixer\n");
		  break ;
		case AWACS_SNAPPER:
		  printk("AE-Init snapper mixer\n");
		  break ;
		case AWACS_DACA:
		case AWACS_BURGUNDY:
			break ;	/* don't know yet */
		case AWACS_AWACS:
		case AWACS_SCREAMER:
		default:
			awacs_mixer_init() ;
			break ;
	}
}

/* Write/Read sq setup functions:
   Check to see if we have enough (or any) dbdma cmd buffers for the
   user's fragment settings.  If not, allocate some. If this fails we will
   point at the beep buffer - as an emergency provision - to stop dma tromping
   on some random bit of memory (if someone lets it go anyway).
   The command buffers are then set up to point to the fragment buffers
   (allocated elsewhere).  We need n+1 commands the last of which holds
   a NOP + loop to start.
*/

static int PMacWriteSqSetup(void)
{
	int i, count = 600 ;
	volatile struct dbdma_cmd *cp;

	LOCK();
	
	/* stop the controller from doing any output - if it isn't already.
	   it _should_ be before this is called anyway */

	out_le32(&awacs_txdma->control, (RUN|PAUSE|FLUSH|WAKE) << 16);
	while ((in_le32(&awacs_txdma->status) & RUN) && count--)
		udelay(1);
#ifdef DEBUG_DMASOUND
if (count <= 0)
	printk("dmasound_pmac: write sq setup: timeout waiting for dma to stop\n");
#endif

	if ((write_sq.max_count + 1) > number_of_tx_cmd_buffers) {
		kfree(awacs_tx_cmd_space);
		number_of_tx_cmd_buffers = 0;

		/* we need nbufs + 1 (for the loop) and we should request + 1
		   again because the DBDMA_ALIGN might pull the start up by up
		   to sizeof(struct dbdma_cmd) - 4.
		*/

		awacs_tx_cmd_space = kmalloc
			((write_sq.max_count + 1 + 1) * sizeof(struct dbdma_cmd),
			 GFP_KERNEL);
		if (awacs_tx_cmd_space == NULL) {
			/* don't leave it dangling - nasty but better than a
			   random address */
			out_le32(&awacs_txdma->cmdptr, virt_to_bus(beep_dbdma_cmd));
			printk(KERN_ERR
			   "dmasound_pmac: can't allocate dbdma cmd buffers"
			   ", driver disabled\n");
			UNLOCK();
			return -ENOMEM;
		}
		awacs_tx_cmds = (volatile struct dbdma_cmd *)
			DBDMA_ALIGN(awacs_tx_cmd_space);
		number_of_tx_cmd_buffers = write_sq.max_count + 1;
	}

	cp = awacs_tx_cmds;
	memset((void *)cp, 0, (write_sq.max_count+1) * sizeof(struct dbdma_cmd));
	for (i = 0; i < write_sq.max_count; ++i, ++cp) {
		st_le32(&cp->phy_addr, virt_to_bus(write_sq.buffers[i]));
	}
	st_le16(&cp->command, DBDMA_NOP + BR_ALWAYS);
	st_le32(&cp->cmd_dep, virt_to_bus(awacs_tx_cmds));
	/* point the controller at the command stack - ready to go */
	out_le32(&awacs_txdma->cmdptr, virt_to_bus(awacs_tx_cmds));
	UNLOCK();
	return 0;
}

static int PMacReadSqSetup(void)
{
	int i, count = 600;
	volatile struct dbdma_cmd *cp;

	LOCK();
	
	/* stop the controller from doing any input - if it isn't already.
	   it _should_ be before this is called anyway */
	
	out_le32(&awacs_rxdma->control, (RUN|PAUSE|FLUSH|WAKE) << 16);
	while ((in_le32(&awacs_rxdma->status) & RUN) && count--)
		udelay(1);
#ifdef DEBUG_DMASOUND
if (count <= 0)
	printk("dmasound_pmac: read sq setup: timeout waiting for dma to stop\n");
#endif

	if ((read_sq.max_count+1) > number_of_rx_cmd_buffers ) {
		kfree(awacs_rx_cmd_space);
		number_of_rx_cmd_buffers = 0;

		/* we need nbufs + 1 (for the loop) and we should request + 1 again
		   because the DBDMA_ALIGN might pull the start up by up to
		   sizeof(struct dbdma_cmd) - 4 (assuming kmalloc aligns 32 bits).
		*/

		awacs_rx_cmd_space = kmalloc
			((read_sq.max_count + 1 + 1) * sizeof(struct dbdma_cmd),
			 GFP_KERNEL);
		if (awacs_rx_cmd_space == NULL) {
			/* don't leave it dangling - nasty but better than a
			   random address */
			out_le32(&awacs_rxdma->cmdptr, virt_to_bus(beep_dbdma_cmd));
			printk(KERN_ERR
			   "dmasound_pmac: can't allocate dbdma cmd buffers"
			   ", driver disabled\n");
			UNLOCK();
			return -ENOMEM;
		}
		awacs_rx_cmds = (volatile struct dbdma_cmd *)
			DBDMA_ALIGN(awacs_rx_cmd_space);
		number_of_rx_cmd_buffers = read_sq.max_count + 1 ;
	}
	cp = awacs_rx_cmds;
	memset((void *)cp, 0, (read_sq.max_count+1) * sizeof(struct dbdma_cmd));

	/* Set dma buffers up in a loop */
	for (i = 0; i < read_sq.max_count; i++,cp++) {
		st_le32(&cp->phy_addr, virt_to_bus(read_sq.buffers[i]));
		st_le16(&cp->command, INPUT_MORE + INTR_ALWAYS);
		st_le16(&cp->req_count, read_sq.block_size);
		st_le16(&cp->xfer_status, 0);
	}

	/* The next two lines make the thing loop around.
	*/
	st_le16(&cp->command, DBDMA_NOP + BR_ALWAYS);
	st_le32(&cp->cmd_dep, virt_to_bus(awacs_rx_cmds));
	/* point the controller at the command stack - ready to go */
	out_le32(&awacs_rxdma->cmdptr, virt_to_bus(awacs_rx_cmds));

	UNLOCK();
	return 0;
}

/* TODO: this needs work to guarantee that when it returns DMA has stopped
   but in a more elegant way than is done here....
*/

static void PMacAbortRead(void)
{
	int i;
	volatile struct dbdma_cmd *cp;

	LOCK();
	/* give it a chance to update the output and provide the IRQ
	   that is expected.
	*/

	out_le32(&awacs_rxdma->control, ((FLUSH) << 16) + FLUSH );

	cp = awacs_rx_cmds;
	for (i = 0; i < read_sq.max_count; i++,cp++)
		st_le16(&cp->command, DBDMA_STOP);
	/*
	 * We should probably wait for the thing to stop before we
	 * release the memory.
	 */

	msleep(100) ; /* give it a (small) chance to act */

	/* apply the sledgehammer approach - just stop it now */

	out_le32(&awacs_rxdma->control, (RUN|PAUSE|FLUSH|WAKE) << 16);
	UNLOCK();
}

extern char *get_afmt_string(int);
static int PMacStateInfo(char *b, size_t sp)
{
	int i, len = 0;
	len = sprintf(b,"HW rates: ");
	switch (awacs_revision){
		case AWACS_DACA:
		case AWACS_BURGUNDY:
			len += sprintf(b,"44100 ") ;
			break ;
		case AWACS_TUMBLER:
		case AWACS_SNAPPER:
			for (i=0; i<1; i++){
				if (tas_freqs_ok[i])
					len += sprintf(b+len,"%d ", tas_freqs[i]) ;
			}
			break ;
		case AWACS_AWACS:
		case AWACS_SCREAMER:
		default:
			for (i=0; i<8; i++){
				if (awacs_freqs_ok[i])
					len += sprintf(b+len,"%d ", awacs_freqs[i]) ;
			}
			break ;
	}
	len += sprintf(b+len,"s/sec\n") ;
	if (len < sp) {
		len += sprintf(b+len,"HW AFMTS: ");
		i = AFMT_U16_BE ;
		while (i) {
			if (i & dmasound.mach.hardware_afmts)
				len += sprintf(b+len,"%s ",
					get_afmt_string(i & dmasound.mach.hardware_afmts));
			i >>= 1 ;
		}
		len += sprintf(b+len,"\n") ;
	}
	return len ;
}

/*** Machine definitions *****************************************************/

static SETTINGS def_hard = {
	.format	= AFMT_S16_BE,
	.stereo	= 1,
	.size	= 16,
	.speed	= 44100
} ;

static SETTINGS def_soft = {
	.format	= AFMT_S16_BE,
	.stereo	= 1,
	.size	= 16,
	.speed	= 44100
} ;

static MACHINE machPMac = {
	.name		= awacs_name,
	.name2		= "PowerMac Built-in Sound",
	.owner		= THIS_MODULE,
	.dma_alloc	= PMacAlloc,
	.dma_free	= PMacFree,
	.irqinit	= PMacIrqInit,
#ifdef MODULE
	.irqcleanup	= PMacIrqCleanup,
#endif /* MODULE */
	.init		= PMacInit,
	.silence	= PMacSilence,
	.setFormat	= PMacSetFormat,
	.setVolume	= PMacSetVolume,
	.play		= PMacPlay,
	.record		= NULL,		/* default to no record */
	.mixer_init	= PMacMixerInit,
	.mixer_ioctl	= PMacMixerIoctl,
	.write_sq_setup	= PMacWriteSqSetup,
	.read_sq_setup	= PMacReadSqSetup,
	.state_info	= PMacStateInfo,
	.abort_read	= PMacAbortRead,
	.min_dsp_speed	= 7350,
	.max_dsp_speed	= 44100,
	.version	= ((DMASOUND_AWACS_REVISION<<8) + DMASOUND_AWACS_EDITION)
};


/*** Config & Setup **********************************************************/

/* Check for pmac models that we care about in terms of special actions.
*/

void __init
set_model(void)
{
	/* portables/lap-tops */

	if (machine_is_compatible("AAPL,3400/2400") ||
	    machine_is_compatible("AAPL,3500"))	{
		is_pbook_3X00 = 1 ;
	}
	if (machine_is_compatible("PowerBook1,1")  || /* lombard */
	    machine_is_compatible("AAPL,PowerBook1998")){ /* wallstreet */
		is_pbook_g3 = 1 ;
		return ;
	}
}

/* Get the OF node that tells us about the registers, interrupts etc. to use
   for sound IO.

   On most machines the sound IO OF node is the 'davbus' node.  On newer pmacs
   with DACA (& Tumbler) the node to use is i2s-a.  On much older machines i.e.
   before 9500 there is no davbus node and we have to use the 'awacs' property.

  In the latter case we signal this by setting the codec value - so that the
  code that looks for chip properties knows how to go about it.
*/

static struct device_node* __init
get_snd_io_node(void)
{
	struct device_node *np = NULL;

	/* set up awacs_node for early OF which doesn't have a full set of
	 * properties on davbus
	*/

	awacs_node = find_devices("awacs");
	if (awacs_node)
		awacs_revision = AWACS_AWACS;

	/* powermac models after 9500 (other than those which use DACA or
	 * Tumbler) have a node called "davbus".
	 */
	np = find_devices("davbus");
	/*
	 * if we didn't find a davbus device, try 'i2s-a' since
	 * this seems to be what iBooks (& Tumbler) have.
	 */
	if (np == NULL)
		np = i2s_node = find_devices("i2s-a");

	/* if we didn't find this - perhaps we are on an early model
	 * which _only_ has an 'awacs' node
	*/
	if (np == NULL && awacs_node)
		np = awacs_node ;

	/* if we failed all these return null - this will cause the
	 * driver to give up...
	*/
	return np ;
}

/* Get the OF node that contains the info about the sound chip, inputs s-rates
   etc.
   This node does not exist (or contains much reduced info) on earlier machines
   we have to deduce the info other ways for these.
*/

static struct device_node* __init
get_snd_info_node(struct device_node *io)
{
	struct device_node *info;

	info = find_devices("sound");
	while (info && info->parent != io)
		info = info->next;
	return info;
}

/* Find out what type of codec we have.
*/

static int __init
get_codec_type(struct device_node *info)
{
	/* already set if pre-davbus model and info will be NULL */
	int codec = awacs_revision ;

	if (info) {
		/* must do awacs first to allow screamer to overide it */
		if (device_is_compatible(info, "awacs"))
			codec = AWACS_AWACS ;
		if (device_is_compatible(info, "screamer"))
			codec = AWACS_SCREAMER;
		if (device_is_compatible(info, "burgundy"))
			codec = AWACS_BURGUNDY ;
		if (device_is_compatible(info, "daca"))
			codec = AWACS_DACA;
		if (device_is_compatible(info, "tumbler"))
			codec = AWACS_TUMBLER;
		if (device_is_compatible(info, "snapper"))
			codec = AWACS_SNAPPER;
	}
	return codec ;
}

/* find out what type, if any, of expansion card we have
*/
static void __init
get_expansion_type(void)
{
	if (find_devices("perch") != NULL)
		has_perch = 1;

	if (find_devices("pb-ziva-pc") != NULL)
		has_ziva = 1;
	/* need to work out how we deal with iMac SRS module */
}

/* set up frame rates.
 * I suspect that these routines don't quite go about it the right way:
 * - where there is more than one rate - I think that the first property
 * value is the number of rates.
 * TODO: check some more device trees and modify accordingly
 *       Set dmasound.mach.max_dsp_rate on the basis of these routines.
*/

static void __init
awacs_init_frame_rates(unsigned int *prop, unsigned int l)
{
	int i ;
	if (prop) {
		for (i=0; i<8; i++)
			awacs_freqs_ok[i] = 0 ;
		for (l /= sizeof(int); l > 0; --l) {
			unsigned int r = *prop++;
			/* Apple 'Fixed' format */
			if (r >= 0x10000)
				r >>= 16;
			for (i = 0; i < 8; ++i) {
				if (r == awacs_freqs[i]) {
					awacs_freqs_ok[i] = 1;
					break;
				}
			}
		}
	}
	/* else we assume that all the rates are available */
}

static void __init
burgundy_init_frame_rates(unsigned int *prop, unsigned int l)
{
	int temp[9] ;
	int i = 0 ;
	if (prop) {
		for (l /= sizeof(int); l > 0; --l) {
			unsigned int r = *prop++;
			/* Apple 'Fixed' format */
			if (r >= 0x10000)
				r >>= 16;
			temp[i] = r ;
			i++ ; if(i>=9) i=8;
		}
	}
#ifdef DEBUG_DMASOUND
if (i > 1){
	int j;
	printk("dmasound_pmac: burgundy with multiple frame rates\n");
	for(j=0; j<i; j++)
		printk("%d ", temp[j]) ;
	printk("\n") ;
}
#endif
}

static void __init
daca_init_frame_rates(unsigned int *prop, unsigned int l)
{
	int temp[9] ;
	int i = 0 ;
	if (prop) {
		for (l /= sizeof(int); l > 0; --l) {
			unsigned int r = *prop++;
			/* Apple 'Fixed' format */
			if (r >= 0x10000)
				r >>= 16;
			temp[i] = r ;
			i++ ; if(i>=9) i=8;

		}
	}
#ifdef DEBUG_DMASOUND
if (i > 1){
	int j;
	printk("dmasound_pmac: DACA with multiple frame rates\n");
	for(j=0; j<i; j++)
		printk("%d ", temp[j]) ;
	printk("\n") ;
}
#endif
}

static void __init
init_frame_rates(unsigned int *prop, unsigned int l)
{
	switch (awacs_revision) {
		case AWACS_TUMBLER:
		case AWACS_SNAPPER:
			tas_init_frame_rates(prop, l);
			break ;
		case AWACS_DACA:
			daca_init_frame_rates(prop, l);
			break ;
		case AWACS_BURGUNDY:
			burgundy_init_frame_rates(prop, l);
			break ;
		default:
			awacs_init_frame_rates(prop, l);
			break ;
	}
}

/* find things/machines that can't do mac-io byteswap
*/

static void __init
set_hw_byteswap(struct device_node *io)
{
	struct device_node *mio ;
	unsigned int kl = 0 ;

	/* if seems that Keylargo can't byte-swap  */

	for (mio = io->parent; mio ; mio = mio->parent) {
		if (strcmp(mio->name, "mac-io") == 0) {
			if (device_is_compatible(mio, "Keylargo"))
				kl = 1;
			break;
		}
	}
	hw_can_byteswap = !kl;
}

/* Allocate the resources necessary for beep generation.  This cannot be (quite)
   done statically (yet) because we cannot do virt_to_bus() on static vars when
   the code is loaded as a module.

   for the sake of saving the possibility that two allocations will incur the
   overhead of two pull-ups in DBDMA_ALIGN() we allocate the 'emergency' dmdma
   command here as well... even tho' it is not part of the beep process.
*/

int32_t
__init setup_beep(void)
{
	/* Initialize beep stuff */
	/* want one cmd buffer for beeps, and a second one for emergencies
	   - i.e. dbdma error conditions.
	   ask for three to allow for pull up in DBDMA_ALIGN().
	*/
	beep_dbdma_cmd_space =
		kmalloc((2 + 1) * sizeof(struct dbdma_cmd), GFP_KERNEL);
	if(beep_dbdma_cmd_space == NULL) {
		printk(KERN_ERR "dmasound_pmac: no beep dbdma cmd space\n") ;
		return -ENOMEM ;
	}
	beep_dbdma_cmd = (volatile struct dbdma_cmd *)
			DBDMA_ALIGN(beep_dbdma_cmd_space);
	/* set up emergency dbdma cmd */
	emergency_dbdma_cmd = beep_dbdma_cmd+1 ;
	beep_buf = kmalloc(BEEP_BUFLEN * 4, GFP_KERNEL);
	if (beep_buf == NULL) {
		printk(KERN_ERR "dmasound_pmac: no memory for beep buffer\n");
		kfree(beep_dbdma_cmd_space) ;
		return -ENOMEM ;
	}
	return 0 ;
}

static struct input_dev *awacs_beep_dev;

int __init dmasound_awacs_init(void)
{
	struct device_node *io = NULL, *info = NULL;
	int vol, res;

	if (!machine_is(powermac))
		return -ENODEV;

	awacs_subframe = 0;
	awacs_revision = 0;
	hw_can_byteswap = 1 ; /* most can */

	/* look for models we need to handle specially */
	set_model() ;

	/* find the OF node that tells us about the dbdma stuff
	*/
	io = get_snd_io_node();
	if (io == NULL) {
#ifdef DEBUG_DMASOUND
printk("dmasound_pmac: couldn't find sound io OF node\n");
#endif
		return -ENODEV ;
	}

	/* find the OF node that tells us about the sound sub-system
	 * this doesn't exist on pre-davbus machines (earlier than 9500)
	*/
	if (awacs_revision != AWACS_AWACS) { /* set for pre-davbus */
		info = get_snd_info_node(io) ;
		if (info == NULL){
#ifdef DEBUG_DMASOUND
printk("dmasound_pmac: couldn't find 'sound' OF node\n");
#endif
			return -ENODEV ;
		}
	}

	awacs_revision = get_codec_type(info) ;
	if (awacs_revision == 0) {
#ifdef DEBUG_DMASOUND
printk("dmasound_pmac: couldn't find a Codec we can handle\n");
#endif
		return -ENODEV ; /* we don't know this type of h/w */
	}

	/* set up perch, ziva, SRS or whatever else we have as sound
	 *  expansion.
	*/
	get_expansion_type();

	/* we've now got enough information to make up the audio topology.
	 * we will map the sound part of mac-io now so that we can probe for
	 * other info if necessary (early AWACS we want to read chip ids)
	 */

	if (of_get_address(io, 2, NULL, NULL) == NULL) {
		/* OK - maybe we need to use the 'awacs' node (on earlier
		 * machines).
		 */
		if (awacs_node) {
			io = awacs_node ;
			if (of_get_address(io, 2, NULL, NULL) == NULL) {
				printk("dmasound_pmac: can't use %s\n",
				       io->full_name);
				return -ENODEV;
			}
		} else
			printk("dmasound_pmac: can't use %s\n", io->full_name);
	}

	if (of_address_to_resource(io, 0, &awacs_rsrc[0]) ||
	    request_mem_region(awacs_rsrc[0].start,
			       awacs_rsrc[0].end - awacs_rsrc[0].start + 1,
			       " (IO)") == NULL) {
		printk(KERN_ERR "dmasound: can't request IO resource !\n");
		return -ENODEV;
	}
	if (of_address_to_resource(io, 1, &awacs_rsrc[1]) ||
	    request_mem_region(awacs_rsrc[1].start,
			       awacs_rsrc[1].end - awacs_rsrc[1].start + 1,
			       " (tx dma)") == NULL) {
		release_mem_region(awacs_rsrc[0].start,
				   awacs_rsrc[0].end - awacs_rsrc[0].start + 1);
		printk(KERN_ERR "dmasound: can't request Tx DMA resource !\n");
		return -ENODEV;
	}
	if (of_address_to_resource(io, 2, &awacs_rsrc[2]) ||
	    request_mem_region(awacs_rsrc[2].start,
			       awacs_rsrc[2].end - awacs_rsrc[2].start + 1,
			       " (rx dma)") == NULL) {
		release_mem_region(awacs_rsrc[0].start,
				   awacs_rsrc[0].end - awacs_rsrc[0].start + 1);
		release_mem_region(awacs_rsrc[1].start,
				   awacs_rsrc[1].end - awacs_rsrc[1].start + 1);
		printk(KERN_ERR "dmasound: can't request Rx DMA resource !\n");
		return -ENODEV;
	}

	awacs_beep_dev = input_allocate_device();
	if (!awacs_beep_dev) {
		release_mem_region(awacs_rsrc[0].start,
				   awacs_rsrc[0].end - awacs_rsrc[0].start + 1);
		release_mem_region(awacs_rsrc[1].start,
				   awacs_rsrc[1].end - awacs_rsrc[1].start + 1);
		release_mem_region(awacs_rsrc[2].start,
				   awacs_rsrc[2].end - awacs_rsrc[2].start + 1);
		printk(KERN_ERR "dmasound: can't allocate input device !\n");
		return -ENOMEM;
	}

	awacs_beep_dev->name = "dmasound beeper";
	awacs_beep_dev->phys = "macio/input0";
	awacs_beep_dev->id.bustype = BUS_HOST;
	awacs_beep_dev->event = awacs_beep_event;
	awacs_beep_dev->sndbit[0] = BIT(SND_BELL) | BIT(SND_TONE);
	awacs_beep_dev->evbit[0] = BIT(EV_SND);

	/* all OF versions I've seen use this value */
	if (i2s_node)
		i2s = ioremap(awacs_rsrc[0].start, 0x1000);
	else
		awacs = ioremap(awacs_rsrc[0].start, 0x1000);
	awacs_txdma = ioremap(awacs_rsrc[1].start, 0x100);
	awacs_rxdma = ioremap(awacs_rsrc[2].start, 0x100);

	/* first of all make sure that the chip is powered up....*/
	pmac_call_feature(PMAC_FTR_SOUND_CHIP_ENABLE, io, 0, 1);
	if (awacs_revision == AWACS_SCREAMER && awacs)
		awacs_recalibrate();

	awacs_irq = irq_of_parse_and_map(io, 0);
	awacs_tx_irq = irq_of_parse_and_map(io, 1);
	awacs_rx_irq = irq_of_parse_and_map(io, 2);

	/* Hack for legacy crap that will be killed someday */
	awacs_node = io;

	/* if we have an awacs or screamer - probe the chip to make
	 * sure we have the right revision.
	*/

	if (awacs_revision <= AWACS_SCREAMER){
		uint32_t temp, rev, mfg ;
		/* find out the awacs revision from the chip */
		temp = in_le32(&awacs->codec_stat);
		rev = (temp >> 12) & 0xf;
		mfg = (temp >>  8) & 0xf;
#ifdef DEBUG_DMASOUND
printk("dmasound_pmac: Awacs/Screamer Codec Mfct: %d Rev %d\n", mfg, rev);
#endif
		if (rev >= AWACS_SCREAMER)
			awacs_revision = AWACS_SCREAMER ;
		else
			awacs_revision = rev ;
	}

	dmasound.mach = machPMac;

	/* find out other bits & pieces from OF, these may be present
	   only on some models ... so be careful.
	*/

	/* in the absence of a frame rates property we will use the defaults
	*/

	if (info) {
		unsigned int *prop, l;

		sound_device_id = 0;
		/* device ID appears post g3 b&w */
		prop = (unsigned int *)get_property(info, "device-id", NULL);
		if (prop != 0)
			sound_device_id = *prop;

		/* look for a property saying what sample rates
		   are available */

		prop = (unsigned int *)get_property(info, "sample-rates", &l);
		if (prop == 0)
			prop = (unsigned int *) get_property
				(info, "output-frame-rates", &l);

		/* if it's there use it to set up frame rates */
		init_frame_rates(prop, l) ;
	}

	if (awacs)
		out_le32(&awacs->control, 0x11); /* set everything quiesent */

	set_hw_byteswap(io) ; /* figure out if the h/w can do it */

#ifdef CONFIG_NVRAM
	/* get default volume from nvram */
	vol = ((pmac_xpram_read( 8 ) & 7 ) << 1 );
#else
	vol = 0;
#endif

	/* set up tracking values */
	spk_vol = vol * 100 ;
	spk_vol /= 7 ; /* get set value to a percentage */
	spk_vol |= (spk_vol << 8) ; /* equal left & right */
 	line_vol = passthru_vol = spk_vol ;

	/* fill regs that are shared between AWACS & Burgundy */

	awacs_reg[2] = vol + (vol << 6);
	awacs_reg[4] = vol + (vol << 6);
	awacs_reg[5] = vol + (vol << 6); /* screamer has loopthru vol control */
	awacs_reg[6] = 0; /* maybe should be vol << 3 for PCMCIA speaker */
	awacs_reg[7] = 0;

	awacs_reg[0] = MASK_MUX_CD;
	awacs_reg[1] = MASK_LOOPTHRU;

	/* FIXME: Only machines with external SRS module need MASK_PAROUT */
	if (has_perch || sound_device_id == 0x5
	    || /*sound_device_id == 0x8 ||*/ sound_device_id == 0xb)
		awacs_reg[1] |= MASK_PAROUT0 | MASK_PAROUT1;

	switch (awacs_revision) {
		case AWACS_TUMBLER:
                        tas_register_driver(&tas3001c_hooks);
			tas_init(I2C_DRIVERID_TAS3001C, I2C_DRIVERNAME_TAS3001C);
			tas_dmasound_init();
			tas_post_init();
			break ;
		case AWACS_SNAPPER:
                        tas_register_driver(&tas3004_hooks);
			tas_init(I2C_DRIVERID_TAS3004,I2C_DRIVERNAME_TAS3004);
			tas_dmasound_init();
			tas_post_init();
			break;
		case AWACS_DACA:
			daca_init();
			break;	
		case AWACS_BURGUNDY:
			awacs_burgundy_init();
			break ;
		case AWACS_SCREAMER:
		case AWACS_AWACS:
		default:
			load_awacs();
			break ;
	}

	/* enable/set-up external modules - when we know how */

	if (has_perch)
		awacs_enable_amp(100 * 0x101);

	/* Reset dbdma channels */
	out_le32(&awacs_txdma->control, (RUN|PAUSE|FLUSH|WAKE|DEAD) << 16);
	while (in_le32(&awacs_txdma->status) & RUN)
		udelay(1);
	out_le32(&awacs_rxdma->control, (RUN|PAUSE|FLUSH|WAKE|DEAD) << 16);
	while (in_le32(&awacs_rxdma->status) & RUN)
		udelay(1);

	/* Initialize beep stuff */
	if ((res=setup_beep()))
		return res ;

#ifdef CONFIG_PM
	pmu_register_sleep_notifier(&awacs_sleep_notifier);
#endif /* CONFIG_PM */

	/* Powerbooks have odd ways of enabling inputs such as
	   an expansion-bay CD or sound from an internal modem
	   or a PC-card modem. */
	if (is_pbook_3X00) {
		/*
		 * Enable CD and PC-card sound inputs.
		 * This is done by reading from address
		 * f301a000, + 0x10 to enable the expansion-bay
		 * CD sound input, + 0x80 to enable the PC-card
		 * sound input.  The 0x100 enables the SCSI bus
		 * terminator power.
		 */
		latch_base = ioremap (0xf301a000, 0x1000);
		in_8(latch_base + 0x190);

	} else if (is_pbook_g3) {
		struct device_node* mio;
		macio_base = NULL;
		for (mio = io->parent; mio; mio = mio->parent) {
			if (strcmp(mio->name, "mac-io") == 0) {
				struct resource r;
				if (of_address_to_resource(mio, 0, &r) == 0)
					macio_base = ioremap(r.start, 0x40);
				break;
			}
		}
		/*
		 * Enable CD sound input.
		 * The relevant bits for writing to this byte are 0x8f.
		 * I haven't found out what the 0x80 bit does.
		 * For the 0xf bits, writing 3 or 7 enables the CD
		 * input, any other value disables it.  Values
		 * 1, 3, 5, 7 enable the microphone.  Values 0, 2,
		 * 4, 6, 8 - f enable the input from the modem.
		 *  -- paulus.
		 */
		if (macio_base)
			out_8(macio_base + 0x37, 3);
	}

	if (hw_can_byteswap)
 		dmasound.mach.hardware_afmts = (AFMT_S16_BE | AFMT_S16_LE) ;
 	else
		dmasound.mach.hardware_afmts = AFMT_S16_BE ;

	/* shut out chips that do output only.
	 * may need to extend this to machines which have no inputs - even tho'
	 * they use screamer - IIRC one of the powerbooks is like this.
	 */

	if (awacs_revision != AWACS_DACA) {
		dmasound.mach.capabilities = DSP_CAP_DUPLEX ;
		dmasound.mach.record = PMacRecord ;
	}

	dmasound.mach.default_hard = def_hard ;
	dmasound.mach.default_soft = def_soft ;

	switch (awacs_revision) {
		case AWACS_BURGUNDY:
			sprintf(awacs_name, "PowerMac Burgundy ") ;
			break ;
		case AWACS_DACA:
			sprintf(awacs_name, "PowerMac DACA ") ;
			break ;
		case AWACS_TUMBLER:
			sprintf(awacs_name, "PowerMac Tumbler ") ;
			break ;
		case AWACS_SNAPPER:
			sprintf(awacs_name, "PowerMac Snapper ") ;
			break ;
		case AWACS_SCREAMER:
			sprintf(awacs_name, "PowerMac Screamer ") ;
			break ;
		case AWACS_AWACS:
		default:
			sprintf(awacs_name, "PowerMac AWACS rev %d ", awacs_revision) ;
			break ;
	}

	/*
	 * XXX: we should handle errors here, but that would mean
	 * rewriting the whole init code.  later..
	 */
	input_register_device(awacs_beep_dev);

	return dmasound_init();
}

static void __exit dmasound_awacs_cleanup(void)
{
	input_unregister_device(awacs_beep_dev);

	switch (awacs_revision) {
		case AWACS_TUMBLER:
		case AWACS_SNAPPER:
			tas_dmasound_cleanup();
			tas_cleanup();
			break ;
		case AWACS_DACA:
			daca_cleanup();
			break;
	}
	dmasound_deinit();

}

MODULE_DESCRIPTION("PowerMac built-in audio driver.");
MODULE_LICENSE("GPL");

module_init(dmasound_awacs_init);
module_exit(dmasound_awacs_cleanup);
