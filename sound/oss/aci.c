/*
 * Audio Command Interface (ACI) driver (sound/aci.c)
 *
 * ACI is a protocol used to communicate with the microcontroller on
 * some sound cards produced by miro, e.g. the miroSOUND PCM12 and
 * PCM20. The ACI has been developed for miro by Norberto Pellicci
 * <pellicci@home.com>. Special thanks to both him and miro for
 * providing the ACI specification.
 *
 * The main function of the ACI is to control the mixer and to get a
 * product identification. On the PCM20, ACI also controls the radio
 * tuner on this card, this is supported in the Video for Linux 
 * miropcm20 driver.
 * -
 * This is a fullfeatured implementation. Unsupported features
 * are bugs... (:
 *
 * It is not longer necessary to load the mad16 module first. The
 * user is currently responsible to set the mad16 mixer correctly.
 *
 * To toggle the solo mode for full duplex operation just use the OSS
 * record switch for the pcm ('wave') controller.           Robert
 * -
 *
 * Revision history:
 *
 *   1995-11-10  Markus Kuhn <mskuhn@cip.informatik.uni-erlangen.de>
 *        First version written.
 *   1995-12-31  Markus Kuhn
 *        Second revision, general code cleanup.
 *   1996-05-16	 Hannu Savolainen
 *	  Integrated with other parts of the driver.
 *   1996-05-28  Markus Kuhn
 *        Initialize CS4231A mixer, make ACI first mixer,
 *        use new private mixer API for solo mode.
 *   1998-08-18  Ruurd Reitsma <R.A.Reitsma@wbmt.tudelft.nl>
 *	  Small modification to export ACI functions and 
 *	  complete modularisation.
 *   2000-06-20  Robert Siemer <Robert.Siemer@gmx.de>
 *        Don't initialize the CS4231A mixer anymore, so the code is
 *        working again, and other small changes to fit in todays
 *        kernels.
 *   2000-08-26  Robert Siemer
 *        Clean up and rewrite for 2.4.x. Maybe it's SMP safe now... (:
 *        ioctl bugfix, and integration of solo-mode into OSS-API,
 *        added (OSS-limited) equalizer support, return value bugfix,
 *        changed param aci_reset to reset, new params: ide, wss.
 *   2001-04-20  Robert Siemer
 *        even more cleanups...
 *   2001-10-08  Arnaldo Carvalho de Melo <acme@conectiva.com.br>
 *   	  Get rid of check_region, .bss optimizations, use set_current_state
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/module.h> 
#include <linux/proc_fs.h>
#include <linux/slab.h>
#include <asm/semaphore.h>
#include <asm/io.h>
#include <asm/uaccess.h>
#include "sound_config.h"

int aci_port;	/* as determined by bit 4 in the OPTi 929 MC4 register */
static int aci_idcode[2];	/* manufacturer and product ID */
int aci_version;	/* ACI firmware version	*/

EXPORT_SYMBOL(aci_port);
EXPORT_SYMBOL(aci_version);

#include "aci.h"


static int aci_solo;	/* status bit of the card that can't be		*
			 * checked with ACI versions prior to 0xb0	*/
static int aci_amp;   /* status bit for power-amp/line-out level
			   but I have no docs about what is what... */
static int aci_micpreamp=3; /* microphone preamp-level that can't be    *
			 * checked with ACI versions prior to 0xb0	*/

static int mixer_device;
static struct semaphore aci_sem;

#ifdef MODULE
static int reset;
module_param(reset, bool, 0);
MODULE_PARM_DESC(reset,"When set to 1, reset aci mixer.");
#else
static int reset = 1;
#endif

static int ide=-1;
module_param(ide, int, 0);
MODULE_PARM_DESC(ide,"1 enable, 0 disable ide-port - untested"
		 " default: do nothing");
static int wss=-1;
module_param(wss, int, 0);
MODULE_PARM_DESC(wss,"change between ACI/WSS-mixer; use 0 and 1 - untested"
		 " default: do nothing; for PCM1-pro only");

#ifdef DEBUG
static void print_bits(unsigned char c)
{
	int j;
	printk(KERN_DEBUG "aci: ");

	for (j=7; j>=0; j--) {
		printk("%d", (c >> j) & 0x1);
	}

	printk("\n");
}
#endif

/*
 * This busy wait code normally requires less than 15 loops and
 * practically always less than 100 loops on my i486/DX2 66 MHz.
 *
 * Warning: Waiting on the general status flag after reseting the MUTE
 * function can take a VERY long time, because the PCM12 does some kind
 * of fade-in effect. For this reason, access to the MUTE function has
 * not been implemented at all.
 *
 * - The OSS interface has no mute option. It takes about 3 seconds to
 * fade-in on my PCM20. busy_wait() handles it great now...     Robert
 */

static int busy_wait(void)
{
	#define MINTIME 500
	long timeout;
	unsigned char byte;

	for (timeout = 1; timeout <= MINTIME+30; timeout++) {
		if (((byte=inb(BUSY_REGISTER)) & 1) == 0) {
			if (timeout >= MINTIME)
				printk(KERN_DEBUG "aci: Got READYFLAG in round %ld.\n", timeout-MINTIME);
			return byte;
		}
		if (timeout >= MINTIME) {
			long out=10*HZ;
			switch (timeout-MINTIME) {
			case 0 ... 9:
				out /= 10;
			case 10 ... 19:
				out /= 10;
			case 20 ... 30:
				out /= 10;
			default:
				set_current_state(TASK_UNINTERRUPTIBLE);
				schedule_timeout(out);
				break;
			}
		}
	}
	printk(KERN_WARNING "aci: busy_wait() time out.\n");
	return -EBUSY;
}

/* The four ACI command types are fucked up. [-:
 * implied is: 1w      - special case for INIT
 * write   is: 2w1r
 * read    is: x(1w1r) where x is 1 or 2 (1 CHECK_SIG, 1 CHECK_STER,
 *                                        1 VERSION, 2 IDCODE)
 *  the command is only in the first write, rest is protocol overhead
 *
 * indexed is technically a write and used for STATUS
 * and the special case for TUNE is: 3w1r
 * 
 * Here the new general sheme: TUNE --> aci_rw_cmd(x,  y,  z)
 *                indexed and write --> aci_rw_cmd(x,  y, -1)
 *           implied and read (x=1) --> aci_rw_cmd(x, -1, -1)
 *
 * Read (x>=2) is not implemented (only used during initialization).
 * Use aci_idcode[2] and aci_version...                    Robert
 */

/* Some notes for error detection: theoretically it is possible.
 * But it doubles the I/O-traffic from ww(r) to wwwrw(r) in the normal 
 * case and doesn't seem to be designed for that...        Robert
 */

static inline int aci_rawwrite(unsigned char byte)
{
	if (busy_wait() >= 0) {
#ifdef DEBUG
		printk(KERN_DEBUG "aci_rawwrite(%d)\n", byte);
#endif
		outb(byte, COMMAND_REGISTER);
		return 0;
	} else
		return -EBUSY;
}

static inline int aci_rawread(void)
{
	unsigned char byte;

	if (busy_wait() >= 0) {
		byte=inb(STATUS_REGISTER);
#ifdef DEBUG
		printk(KERN_DEBUG "%d = aci_rawread()\n", byte);
#endif
		return byte;
	} else
		return -EBUSY;
}


int aci_rw_cmd(int write1, int write2, int write3)
{
	int write[] = {write1, write2, write3};
	int read = -EINTR, i;

	if (down_interruptible(&aci_sem))
		goto out;

	for (i=0; i<3; i++) {
		if (write[i]< 0 || write[i] > 255)
			break;
		else {
			read = aci_rawwrite(write[i]);
			if (read < 0)
				goto out_up;
		}
		
	}
	
	read = aci_rawread();
out_up:	up(&aci_sem);
out:	return read;
}

EXPORT_SYMBOL(aci_rw_cmd);

static int setvolume(int __user *arg, 
		     unsigned char left_index, unsigned char right_index)
{
	int vol, ret, uservol, buf;

	__get_user(uservol, arg);

	/* left channel */
	vol = uservol & 0xff;
	if (vol > 100)
		vol = 100;
	vol = SCALE(100, 0x20, vol);
	if ((buf=aci_write_cmd(left_index, 0x20 - vol))<0)
		return buf;
	ret = SCALE(0x20, 100, vol);


	/* right channel */
	vol = (uservol >> 8) & 0xff;
	if (vol > 100)
		vol = 100;
	vol = SCALE(100, 0x20, vol);
	if ((buf=aci_write_cmd(right_index, 0x20 - vol))<0)
		return buf;
	ret |= SCALE(0x20, 100, vol) << 8;
 
	__put_user(ret, arg);

	return 0;
}

static int getvolume(int __user *arg,
		     unsigned char left_index, unsigned char right_index)
{
	int vol;
	int buf;

	/* left channel */
	if ((buf=aci_indexed_cmd(ACI_STATUS, left_index))<0)
		return buf;
	vol = SCALE(0x20, 100, buf < 0x20 ? 0x20-buf : 0);
	
	/* right channel */
	if ((buf=aci_indexed_cmd(ACI_STATUS, right_index))<0)
		return buf;
	vol |= SCALE(0x20, 100, buf < 0x20 ? 0x20-buf : 0) << 8;

	__put_user(vol, arg);

	return 0;
}


/* The equalizer is somewhat strange on the ACI. From -12dB to +12dB
 * write:  0xff..down.to..0x80==0x00..up.to..0x7f
 */

static inline unsigned int eq_oss2aci(unsigned int vol)
{
	int boost=0;
	unsigned int ret;

	if (vol > 100)
		vol = 100;
	if (vol > 50) {
		vol -= 51;
		boost=1;
	}
	if (boost)
		ret=SCALE(49, 0x7e, vol)+1;
	else
		ret=0xff - SCALE(50, 0x7f, vol);
	return ret;
}

static inline unsigned int eq_aci2oss(unsigned int vol)
{
	if (vol < 0x80)
		return SCALE(0x7f, 50, vol) + 50;
	else
		return SCALE(0x7f, 50, 0xff-vol);
}


static int setequalizer(int __user *arg, 
			unsigned char left_index, unsigned char right_index)
{
	int buf;
	unsigned int vol;

	__get_user(vol, arg);

	/* left channel */
	if ((buf=aci_write_cmd(left_index, eq_oss2aci(vol & 0xff)))<0)
		return buf;

	/* right channel */
	if ((buf=aci_write_cmd(right_index, eq_oss2aci((vol>>8) & 0xff)))<0)
		return buf;

	/* the ACI equalizer is more precise */
	return 0;
}

static int getequalizer(int __user *arg,
			unsigned char left_index, unsigned char right_index)
{
	int buf;
	unsigned int vol;

	/* left channel */
	if ((buf=aci_indexed_cmd(ACI_STATUS, left_index))<0)
		return buf;
	vol = eq_aci2oss(buf);
	
	/* right channel */
	if ((buf=aci_indexed_cmd(ACI_STATUS, right_index))<0)
		return buf;
	vol |= eq_aci2oss(buf) << 8;

	__put_user(vol, arg);

	return 0;
}

static int aci_mixer_ioctl (int dev, unsigned int cmd, void __user * arg)
{
	int vol, buf;
	int __user *p = arg;

	switch (cmd) {
	case SOUND_MIXER_WRITE_VOLUME:
		return setvolume(p, 0x01, 0x00);
	case SOUND_MIXER_WRITE_CD:
		return setvolume(p, 0x3c, 0x34);
	case SOUND_MIXER_WRITE_MIC:
		return setvolume(p, 0x38, 0x30);
	case SOUND_MIXER_WRITE_LINE:
		return setvolume(p, 0x39, 0x31);
	case SOUND_MIXER_WRITE_SYNTH:
		return setvolume(p, 0x3b, 0x33);
	case SOUND_MIXER_WRITE_PCM:
		return setvolume(p, 0x3a, 0x32);
	case MIXER_WRITE(SOUND_MIXER_RADIO): /* fall through */
	case SOUND_MIXER_WRITE_LINE1:  /* AUX1 or radio */
		return setvolume(p, 0x3d, 0x35);
	case SOUND_MIXER_WRITE_LINE2:  /* AUX2 */
		return setvolume(p, 0x3e, 0x36);
	case SOUND_MIXER_WRITE_BASS:   /* set band one and two */
		if (aci_idcode[1]=='C') {
			if ((buf=setequalizer(p, 0x48, 0x40)) || 
			    (buf=setequalizer(p, 0x49, 0x41)));
			return buf;
		}
		break;
	case SOUND_MIXER_WRITE_TREBLE: /* set band six and seven */
		if (aci_idcode[1]=='C') {
			if ((buf=setequalizer(p, 0x4d, 0x45)) || 
			    (buf=setequalizer(p, 0x4e, 0x46)));
			return buf;
		}
		break;
	case SOUND_MIXER_WRITE_IGAIN:  /* MIC pre-amp */
		if (aci_idcode[1]=='B' || aci_idcode[1]=='C') {
			__get_user(vol, p);
			vol = vol & 0xff;
			if (vol > 100)
				vol = 100;
			vol = SCALE(100, 3, vol);
			if ((buf=aci_write_cmd(ACI_WRITE_IGAIN, vol))<0)
				return buf;
			aci_micpreamp = vol;
			vol = SCALE(3, 100, vol);
			vol |= (vol << 8);
			__put_user(vol, p);
			return 0;
		}
		break;
	case SOUND_MIXER_WRITE_OGAIN:  /* Power-amp/line-out level */
		if (aci_idcode[1]=='A' || aci_idcode[1]=='B') {
			__get_user(buf, p);
			buf = buf & 0xff;
			if (buf > 50)
				vol = 1;
			else
				vol = 0;
			if ((buf=aci_write_cmd(ACI_SET_POWERAMP, vol))<0)
				return buf;
			aci_amp = vol;
			if (aci_amp)
				buf = (100 || 100<<8);
			else
				buf = 0;
			__put_user(buf, p);
			return 0;
		}
		break;
	case SOUND_MIXER_WRITE_RECSRC:
		/* handle solo mode control */
		__get_user(buf, p);
		/* unset solo when RECSRC for PCM is requested */
		if (aci_idcode[1]=='B' || aci_idcode[1]=='C') {
			vol = !(buf & SOUND_MASK_PCM);
			if ((buf=aci_write_cmd(ACI_SET_SOLOMODE, vol))<0)
				return buf;
			aci_solo = vol;
		}
		buf = (SOUND_MASK_CD| SOUND_MASK_MIC| SOUND_MASK_LINE|
		       SOUND_MASK_SYNTH| SOUND_MASK_LINE2);
		if (aci_idcode[1] == 'C') /* PCM20 radio */
			buf |= SOUND_MASK_RADIO;
		else
			buf |= SOUND_MASK_LINE1;
		if (!aci_solo)
			buf |= SOUND_MASK_PCM;
		__put_user(buf, p);
		return 0;
	case SOUND_MIXER_READ_DEVMASK:
		buf = (SOUND_MASK_VOLUME | SOUND_MASK_CD    |
		       SOUND_MASK_MIC    | SOUND_MASK_LINE  |
		       SOUND_MASK_SYNTH  | SOUND_MASK_PCM   |
		       SOUND_MASK_LINE2);
		switch (aci_idcode[1]) {
		case 'C': /* PCM20 radio */
			buf |= (SOUND_MASK_RADIO | SOUND_MASK_IGAIN |
				SOUND_MASK_BASS  | SOUND_MASK_TREBLE);
			break;
		case 'B': /* PCM12 */
			buf |= (SOUND_MASK_LINE1 | SOUND_MASK_IGAIN |
				SOUND_MASK_OGAIN);
			break;
		case 'A': /* PCM1-pro */
			buf |= (SOUND_MASK_LINE1 | SOUND_MASK_OGAIN);
			break;
		default:
			buf |= SOUND_MASK_LINE1;
		}
		__put_user(buf, p);
		return 0;
	case SOUND_MIXER_READ_STEREODEVS:
		buf = (SOUND_MASK_VOLUME | SOUND_MASK_CD    |
		       SOUND_MASK_MIC    | SOUND_MASK_LINE  |
		       SOUND_MASK_SYNTH  | SOUND_MASK_PCM   |
		       SOUND_MASK_LINE2);
		switch (aci_idcode[1]) {
		case 'C': /* PCM20 radio */
			buf |= (SOUND_MASK_RADIO |
				SOUND_MASK_BASS  | SOUND_MASK_TREBLE);
			break;
		default:
			buf |= SOUND_MASK_LINE1;
		}
		__put_user(buf, p);
		return 0;
	case SOUND_MIXER_READ_RECMASK:
		buf = (SOUND_MASK_CD| SOUND_MASK_MIC| SOUND_MASK_LINE|
		       SOUND_MASK_SYNTH| SOUND_MASK_LINE2| SOUND_MASK_PCM);
		if (aci_idcode[1] == 'C') /* PCM20 radio */
			buf |= SOUND_MASK_RADIO;
		else
			buf |= SOUND_MASK_LINE1;

		__put_user(buf, p);
		return 0;
	case SOUND_MIXER_READ_RECSRC:
		buf = (SOUND_MASK_CD    | SOUND_MASK_MIC   | SOUND_MASK_LINE  |
		       SOUND_MASK_SYNTH | SOUND_MASK_LINE2);
		/* do we need aci_solo or can I get it from the ACI? */
		switch (aci_idcode[1]) {
		case 'B': /* PCM12 */
		case 'C': /* PCM20 radio */
			if (aci_version >= 0xb0) {
				if ((vol=aci_rw_cmd(ACI_STATUS,
						    ACI_S_GENERAL, -1))<0)
					return vol;
				if (vol & 0x20)
					buf |= SOUND_MASK_PCM;
			}
			else
				if (!aci_solo)
					buf |= SOUND_MASK_PCM;
			break;
		default:
			buf |= SOUND_MASK_PCM;
		}
		if (aci_idcode[1] == 'C') /* PCM20 radio */
			buf |= SOUND_MASK_RADIO;
		else
			buf |= SOUND_MASK_LINE1;

		__put_user(buf, p);
		return 0;
	case SOUND_MIXER_READ_CAPS:
		__put_user(0, p);
		return 0;
	case SOUND_MIXER_READ_VOLUME:
		return getvolume(p, 0x04, 0x03);
	case SOUND_MIXER_READ_CD:
		return getvolume(p, 0x0a, 0x09);
	case SOUND_MIXER_READ_MIC:
		return getvolume(p, 0x06, 0x05);
	case SOUND_MIXER_READ_LINE:
		return getvolume(p, 0x08, 0x07);
	case SOUND_MIXER_READ_SYNTH:
		return getvolume(p, 0x0c, 0x0b);
	case SOUND_MIXER_READ_PCM:
		return getvolume(p, 0x0e, 0x0d);
	case MIXER_READ(SOUND_MIXER_RADIO): /* fall through */
	case SOUND_MIXER_READ_LINE1:  /* AUX1 */
		return getvolume(p, 0x11, 0x10);
	case SOUND_MIXER_READ_LINE2:  /* AUX2 */
		return getvolume(p, 0x13, 0x12);
	case SOUND_MIXER_READ_BASS:   /* get band one */
		if (aci_idcode[1]=='C') {
			return getequalizer(p, 0x23, 0x22);
		}
		break;
	case SOUND_MIXER_READ_TREBLE: /* get band seven */
		if (aci_idcode[1]=='C') {
			return getequalizer(p, 0x2f, 0x2e);
		}
		break;
	case SOUND_MIXER_READ_IGAIN:  /* MIC pre-amp */
		if (aci_idcode[1]=='B' || aci_idcode[1]=='C') {
			/* aci_micpreamp or ACI? */
			if (aci_version >= 0xb0) {
				if ((buf=aci_indexed_cmd(ACI_STATUS,
							 ACI_S_READ_IGAIN))<0)
					return buf;
			}
			else
				buf=aci_micpreamp;
			vol = SCALE(3, 100, buf <= 3 ? buf : 3);
			vol |= vol << 8;
			__put_user(vol, p);
			return 0;
		}
		break;
	case SOUND_MIXER_READ_OGAIN:
		if (aci_amp)
			buf = (100 || 100<<8);
		else
			buf = 0;
		__put_user(buf, p);
		return 0;
	}
	return -EINVAL;
}

static struct mixer_operations aci_mixer_operations =
{
	.owner = THIS_MODULE,
	.id    = "ACI",
	.ioctl = aci_mixer_ioctl
};

/*
 * There is also an internal mixer in the codec (CS4231A or AD1845),
 * that deserves no purpose in an ACI based system which uses an
 * external ACI controlled stereo mixer. Make sure that this codec
 * mixer has the AUX1 input selected as the recording source, that the
 * input gain is set near maximum and that the other channels going
 * from the inputs to the codec output are muted.
 */

static int __init attach_aci(void)
{
	char *boardname;
	int i, rc = -EBUSY;

	init_MUTEX(&aci_sem);

	outb(0xE3, 0xf8f); /* Write MAD16 password */
	aci_port = (inb(0xf90) & 0x10) ?
		0x344: 0x354; /* Get aci_port from MC4_PORT */

	if (!request_region(aci_port, 3, "sound mixer (ACI)")) {
		printk(KERN_NOTICE
		       "aci: I/O area 0x%03x-0x%03x already used.\n",
		       aci_port, aci_port+2);
		goto out;
	}

	/* force ACI into a known state */
	rc = -EFAULT;
	for (i=0; i<3; i++)
		if (aci_rw_cmd(ACI_ERROR_OP, -1, -1)<0)
			goto out_release_region;

	/* official this is one aci read call: */
	rc = -EFAULT;
	if ((aci_idcode[0]=aci_rw_cmd(ACI_READ_IDCODE, -1, -1))<0 ||
	    (aci_idcode[1]=aci_rw_cmd(ACI_READ_IDCODE, -1, -1))<0) {
		printk(KERN_ERR "aci: Failed to read idcode on 0x%03x.\n",
		       aci_port);
		goto out_release_region;
	}

	if ((aci_version=aci_rw_cmd(ACI_READ_VERSION, -1, -1))<0) {
		printk(KERN_ERR "aci: Failed to read version on 0x%03x.\n",
		       aci_port);
		goto out_release_region;
	}

	if (aci_idcode[0] == 'm') {
		/* It looks like a miro sound card. */
		switch (aci_idcode[1]) {
		case 'A':
			boardname = "PCM1 pro / early PCM12";
			break;
		case 'B':
			boardname = "PCM12";
			break;
		case 'C':
			boardname = "PCM20 radio";
			break;
		default:
			boardname = "unknown miro";
		}
	} else {
		printk(KERN_WARNING "aci: Warning: unsupported card! - "
		       "no hardware, no specs...\n");
		boardname = "unknown Cardinal Technologies";
	}

	printk(KERN_INFO "<ACI 0x%02x, id %02x/%02x \"%c/%c\", (%s)> at 0x%03x\n",
	       aci_version,
	       aci_idcode[0], aci_idcode[1],
	       aci_idcode[0], aci_idcode[1],
	       boardname, aci_port);

	rc = -EBUSY;
	if (reset) {
		/* first write()s after reset fail with my PCM20 */
		if (aci_rw_cmd(ACI_INIT, -1, -1)<0 ||
		    aci_rw_cmd(ACI_ERROR_OP, ACI_ERROR_OP, ACI_ERROR_OP)<0 ||
		    aci_rw_cmd(ACI_ERROR_OP, ACI_ERROR_OP, ACI_ERROR_OP)<0)
			goto out_release_region;
	}

	/* the PCM20 is muted after reset (and reboot) */
	if (aci_rw_cmd(ACI_SET_MUTE, 0x00, -1)<0)
		goto out_release_region;

	if (ide>=0)
		if (aci_rw_cmd(ACI_SET_IDE, !ide, -1)<0)
			goto out_release_region;
	
	if (wss>=0 && aci_idcode[1]=='A')
		if (aci_rw_cmd(ACI_SET_WSS, !!wss, -1)<0)
			goto out_release_region;

	mixer_device = sound_install_mixer(MIXER_DRIVER_VERSION, boardname,
					   &aci_mixer_operations,
					   sizeof(aci_mixer_operations), NULL);
	rc = 0;
	if (mixer_device < 0) {
		printk(KERN_ERR "aci: Failed to install mixer.\n");
		rc = mixer_device;
		goto out_release_region;
	} /* else Maybe initialize the CS4231A mixer here... */
out:	return rc;
out_release_region:
	release_region(aci_port, 3);
	goto out;
}

static void __exit unload_aci(void)
{
	sound_unload_mixerdev(mixer_device);
	release_region(aci_port, 3);
}

module_init(attach_aci);
module_exit(unload_aci);
MODULE_LICENSE("GPL");
