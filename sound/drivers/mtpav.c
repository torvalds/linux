/*
 *      MOTU Midi Timepiece ALSA Main routines
 *      Copyright by Michael T. Mayers (c) Jan 09, 2000
 *      mail: michael@tweakoz.com
 *      Thanks to John Galbraith
 *
 *      This program is free software; you can redistribute it and/or modify
 *      it under the terms of the GNU General Public License as published by
 *      the Free Software Foundation; either version 2 of the License, or
 *      (at your option) any later version.
 *
 *      This program is distributed in the hope that it will be useful,
 *      but WITHOUT ANY WARRANTY; without even the implied warranty of
 *      MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *      GNU General Public License for more details.
 *
 *      You should have received a copy of the GNU General Public License
 *      along with this program; if not, write to the Free Software
 *      Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307 USA
 *
 *
 *      This driver is for the 'Mark Of The Unicorn' (MOTU)
 *      MidiTimePiece AV multiport MIDI interface 
 *
 *      IOPORTS
 *      -------
 *      8 MIDI Ins and 8 MIDI outs
 *      Video Sync In (BNC), Word Sync Out (BNC), 
 *      ADAT Sync Out (DB9)
 *      SMPTE in/out (1/4")
 *      2 programmable pedal/footswitch inputs and 4 programmable MIDI controller knobs.
 *      Macintosh RS422 serial port
 *      RS422 "network" port for ganging multiple MTP's
 *      PC Parallel Port ( which this driver currently uses )
 *
 *      MISC FEATURES
 *      -------------
 *      Hardware MIDI routing, merging, and filtering   
 *      MIDI Synchronization to Video, ADAT, SMPTE and other Clock sources
 *      128 'scene' memories, recallable from MIDI program change
 *
 *
 * ChangeLog
 * Jun 11 2001	Takashi Iwai <tiwai@suse.de>
 *      - Recoded & debugged
 *      - Added timer interrupt for midi outputs
 *      - hwports is between 1 and 8, which specifies the number of hardware ports.
 *        The three global ports, computer, adat and broadcast ports, are created
 *        always after h/w and remote ports.
 *
 */

#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/err.h>
#include <linux/platform_device.h>
#include <linux/ioport.h>
#include <linux/io.h>
#include <linux/moduleparam.h>
#include <sound/core.h>
#include <sound/initval.h>
#include <sound/rawmidi.h>
#include <linux/delay.h>

/*
 *      globals
 */
MODULE_AUTHOR("Michael T. Mayers");
MODULE_DESCRIPTION("MOTU MidiTimePiece AV multiport MIDI");
MODULE_LICENSE("GPL");
MODULE_SUPPORTED_DEVICE("{{MOTU,MidiTimePiece AV multiport MIDI}}");

// io resources
#define MTPAV_IOBASE		0x378
#define MTPAV_IRQ		7
#define MTPAV_MAX_PORTS		8

static int index = SNDRV_DEFAULT_IDX1;
static char *id = SNDRV_DEFAULT_STR1;
static long port = MTPAV_IOBASE;	/* 0x378, 0x278 */
static int irq = MTPAV_IRQ;		/* 7, 5 */
static int hwports = MTPAV_MAX_PORTS;	/* use hardware ports 1-8 */

module_param(index, int, 0444);
MODULE_PARM_DESC(index, "Index value for MotuMTPAV MIDI.");
module_param(id, charp, 0444);
MODULE_PARM_DESC(id, "ID string for MotuMTPAV MIDI.");
module_param_hw(port, long, ioport, 0444);
MODULE_PARM_DESC(port, "Parallel port # for MotuMTPAV MIDI.");
module_param_hw(irq, int, irq, 0444);
MODULE_PARM_DESC(irq, "Parallel IRQ # for MotuMTPAV MIDI.");
module_param(hwports, int, 0444);
MODULE_PARM_DESC(hwports, "Hardware ports # for MotuMTPAV MIDI.");

static struct platform_device *device;

/*
 *      defines
 */
//#define USE_FAKE_MTP //       don't actually read/write to MTP device (for debugging without an actual unit) (does not work yet)

// parallel port usage masks
#define SIGS_BYTE 0x08
#define SIGS_RFD 0x80
#define SIGS_IRQ 0x40
#define SIGS_IN0 0x10
#define SIGS_IN1 0x20

#define SIGC_WRITE 0x04
#define SIGC_READ 0x08
#define SIGC_INTEN 0x10

#define DREG 0
#define SREG 1
#define CREG 2

//
#define MTPAV_MODE_INPUT_OPENED		0x01
#define MTPAV_MODE_OUTPUT_OPENED	0x02
#define MTPAV_MODE_INPUT_TRIGGERED	0x04
#define MTPAV_MODE_OUTPUT_TRIGGERED	0x08

#define NUMPORTS (0x12+1)


/*
 */

struct mtpav_port {
	u8 number;
	u8 hwport;
	u8 mode;
	u8 running_status;
	struct snd_rawmidi_substream *input;
	struct snd_rawmidi_substream *output;
};

struct mtpav {
	struct snd_card *card;
	unsigned long port;
	struct resource *res_port;
	int irq;			/* interrupt (for inputs) */
	spinlock_t spinlock;
	int share_irq;			/* number of accesses to input interrupts */
	int istimer;			/* number of accesses to timer interrupts */
	struct timer_list timer;	/* timer interrupts for outputs */
	struct snd_rawmidi *rmidi;
	int num_ports;		/* number of hw ports (1-8) */
	struct mtpav_port ports[NUMPORTS];	/* all ports including computer, adat and bc */

	u32 inmidiport;		/* selected input midi port */
	u32 inmidistate;	/* during midi command 0xf5 */

	u32 outmidihwport;	/* selected output midi hw port */
};


/*
 * possible hardware ports (selected by 0xf5 port message)
 *      0x00		all ports
 *      0x01 .. 0x08    this MTP's ports 1..8
 *      0x09 .. 0x10    networked MTP's ports (9..16)
 *      0x11            networked MTP's computer port
 *      0x63            to ADAT
 *
 * mappig:
 *  subdevice 0 - (X-1)    ports
 *            X - (2*X-1)  networked ports
 *            X            computer
 *            X+1          ADAT
 *            X+2          all ports
 *
 *  where X = chip->num_ports
 */

#define MTPAV_PIDX_COMPUTER	0
#define MTPAV_PIDX_ADAT		1
#define MTPAV_PIDX_BROADCAST	2


static int translate_subdevice_to_hwport(struct mtpav *chip, int subdev)
{
	if (subdev < 0)
		return 0x01; /* invalid - use port 0 as default */
	else if (subdev < chip->num_ports)
		return subdev + 1; /* single mtp port */
	else if (subdev < chip->num_ports * 2)
		return subdev - chip->num_ports + 0x09; /* remote port */
	else if (subdev == chip->num_ports * 2 + MTPAV_PIDX_COMPUTER)
		return 0x11; /* computer port */
	else if (subdev == chip->num_ports + MTPAV_PIDX_ADAT)
		return 0x63;		/* ADAT */
	return 0; /* all ports */
}

static int translate_hwport_to_subdevice(struct mtpav *chip, int hwport)
{
	int p;
	if (hwport <= 0x00) /* all ports */
		return chip->num_ports + MTPAV_PIDX_BROADCAST;
	else if (hwport <= 0x08) { /* single port */
		p = hwport - 1;
		if (p >= chip->num_ports)
			p = 0;
		return p;
	} else if (hwport <= 0x10) { /* remote port */
		p = hwport - 0x09 + chip->num_ports;
		if (p >= chip->num_ports * 2)
			p = chip->num_ports;
		return p;
	} else if (hwport == 0x11)  /* computer port */
		return chip->num_ports + MTPAV_PIDX_COMPUTER;
	else  /* ADAT */
		return chip->num_ports + MTPAV_PIDX_ADAT;
}


/*
 */

static u8 snd_mtpav_getreg(struct mtpav *chip, u16 reg)
{
	u8 rval = 0;

	if (reg == SREG) {
		rval = inb(chip->port + SREG);
		rval = (rval & 0xf8);
	} else if (reg == CREG) {
		rval = inb(chip->port + CREG);
		rval = (rval & 0x1c);
	}

	return rval;
}

/*
 */

static inline void snd_mtpav_mputreg(struct mtpav *chip, u16 reg, u8 val)
{
	if (reg == DREG || reg == CREG)
		outb(val, chip->port + reg);
}

/*
 */

static void snd_mtpav_wait_rfdhi(struct mtpav *chip)
{
	int counts = 10000;
	u8 sbyte;

	sbyte = snd_mtpav_getreg(chip, SREG);
	while (!(sbyte & SIGS_RFD) && counts--) {
		sbyte = snd_mtpav_getreg(chip, SREG);
		udelay(10);
	}
}

static void snd_mtpav_send_byte(struct mtpav *chip, u8 byte)
{
	u8 tcbyt;
	u8 clrwrite;
	u8 setwrite;

	snd_mtpav_wait_rfdhi(chip);

	/////////////////

	tcbyt = snd_mtpav_getreg(chip, CREG);
	clrwrite = tcbyt & (SIGC_WRITE ^ 0xff);
	setwrite = tcbyt | SIGC_WRITE;

	snd_mtpav_mputreg(chip, DREG, byte);
	snd_mtpav_mputreg(chip, CREG, clrwrite);	// clear write bit

	snd_mtpav_mputreg(chip, CREG, setwrite);	// set write bit

}


/*
 */

/* call this with spin lock held */
static void snd_mtpav_output_port_write(struct mtpav *mtp_card,
					struct mtpav_port *portp,
					struct snd_rawmidi_substream *substream)
{
	u8 outbyte;

	// Get the outbyte first, so we can emulate running status if
	// necessary
	if (snd_rawmidi_transmit(substream, &outbyte, 1) != 1)
		return;

	// send port change command if necessary

	if (portp->hwport != mtp_card->outmidihwport) {
		mtp_card->outmidihwport = portp->hwport;

		snd_mtpav_send_byte(mtp_card, 0xf5);
		snd_mtpav_send_byte(mtp_card, portp->hwport);
		/*
		snd_printk(KERN_DEBUG "new outport: 0x%x\n",
			   (unsigned int) portp->hwport);
		*/
		if (!(outbyte & 0x80) && portp->running_status)
			snd_mtpav_send_byte(mtp_card, portp->running_status);
	}

	// send data

	do {
		if (outbyte & 0x80)
			portp->running_status = outbyte;
		
		snd_mtpav_send_byte(mtp_card, outbyte);
	} while (snd_rawmidi_transmit(substream, &outbyte, 1) == 1);
}

static void snd_mtpav_output_write(struct snd_rawmidi_substream *substream)
{
	struct mtpav *mtp_card = substream->rmidi->private_data;
	struct mtpav_port *portp = &mtp_card->ports[substream->number];
	unsigned long flags;

	spin_lock_irqsave(&mtp_card->spinlock, flags);
	snd_mtpav_output_port_write(mtp_card, portp, substream);
	spin_unlock_irqrestore(&mtp_card->spinlock, flags);
}


/*
 *      mtpav control
 */

static void snd_mtpav_portscan(struct mtpav *chip)	// put mtp into smart routing mode
{
	u8 p;

	for (p = 0; p < 8; p++) {
		snd_mtpav_send_byte(chip, 0xf5);
		snd_mtpav_send_byte(chip, p);
		snd_mtpav_send_byte(chip, 0xfe);
	}
}

/*
 */

static int snd_mtpav_input_open(struct snd_rawmidi_substream *substream)
{
	struct mtpav *mtp_card = substream->rmidi->private_data;
	struct mtpav_port *portp = &mtp_card->ports[substream->number];
	unsigned long flags;

	spin_lock_irqsave(&mtp_card->spinlock, flags);
	portp->mode |= MTPAV_MODE_INPUT_OPENED;
	portp->input = substream;
	if (mtp_card->share_irq++ == 0)
		snd_mtpav_mputreg(mtp_card, CREG, (SIGC_INTEN | SIGC_WRITE));	// enable pport interrupts
	spin_unlock_irqrestore(&mtp_card->spinlock, flags);
	return 0;
}

/*
 */

static int snd_mtpav_input_close(struct snd_rawmidi_substream *substream)
{
	struct mtpav *mtp_card = substream->rmidi->private_data;
	struct mtpav_port *portp = &mtp_card->ports[substream->number];
	unsigned long flags;

	spin_lock_irqsave(&mtp_card->spinlock, flags);
	portp->mode &= ~MTPAV_MODE_INPUT_OPENED;
	portp->input = NULL;
	if (--mtp_card->share_irq == 0)
		snd_mtpav_mputreg(mtp_card, CREG, 0);	// disable pport interrupts
	spin_unlock_irqrestore(&mtp_card->spinlock, flags);
	return 0;
}

/*
 */

static void snd_mtpav_input_trigger(struct snd_rawmidi_substream *substream, int up)
{
	struct mtpav *mtp_card = substream->rmidi->private_data;
	struct mtpav_port *portp = &mtp_card->ports[substream->number];
	unsigned long flags;

	spin_lock_irqsave(&mtp_card->spinlock, flags);
	if (up)
		portp->mode |= MTPAV_MODE_INPUT_TRIGGERED;
	else
		portp->mode &= ~MTPAV_MODE_INPUT_TRIGGERED;
	spin_unlock_irqrestore(&mtp_card->spinlock, flags);

}


/*
 * timer interrupt for outputs
 */

static void snd_mtpav_output_timer(struct timer_list *t)
{
	unsigned long flags;
	struct mtpav *chip = from_timer(chip, t, timer);
	int p;

	spin_lock_irqsave(&chip->spinlock, flags);
	/* reprogram timer */
	mod_timer(&chip->timer, 1 + jiffies);
	/* process each port */
	for (p = 0; p <= chip->num_ports * 2 + MTPAV_PIDX_BROADCAST; p++) {
		struct mtpav_port *portp = &chip->ports[p];
		if ((portp->mode & MTPAV_MODE_OUTPUT_TRIGGERED) && portp->output)
			snd_mtpav_output_port_write(chip, portp, portp->output);
	}
	spin_unlock_irqrestore(&chip->spinlock, flags);
}

/* spinlock held! */
static void snd_mtpav_add_output_timer(struct mtpav *chip)
{
	mod_timer(&chip->timer, 1 + jiffies);
}

/* spinlock held! */
static void snd_mtpav_remove_output_timer(struct mtpav *chip)
{
	del_timer(&chip->timer);
}

/*
 */

static int snd_mtpav_output_open(struct snd_rawmidi_substream *substream)
{
	struct mtpav *mtp_card = substream->rmidi->private_data;
	struct mtpav_port *portp = &mtp_card->ports[substream->number];
	unsigned long flags;

	spin_lock_irqsave(&mtp_card->spinlock, flags);
	portp->mode |= MTPAV_MODE_OUTPUT_OPENED;
	portp->output = substream;
	spin_unlock_irqrestore(&mtp_card->spinlock, flags);
	return 0;
};

/*
 */

static int snd_mtpav_output_close(struct snd_rawmidi_substream *substream)
{
	struct mtpav *mtp_card = substream->rmidi->private_data;
	struct mtpav_port *portp = &mtp_card->ports[substream->number];
	unsigned long flags;

	spin_lock_irqsave(&mtp_card->spinlock, flags);
	portp->mode &= ~MTPAV_MODE_OUTPUT_OPENED;
	portp->output = NULL;
	spin_unlock_irqrestore(&mtp_card->spinlock, flags);
	return 0;
};

/*
 */

static void snd_mtpav_output_trigger(struct snd_rawmidi_substream *substream, int up)
{
	struct mtpav *mtp_card = substream->rmidi->private_data;
	struct mtpav_port *portp = &mtp_card->ports[substream->number];
	unsigned long flags;

	spin_lock_irqsave(&mtp_card->spinlock, flags);
	if (up) {
		if (! (portp->mode & MTPAV_MODE_OUTPUT_TRIGGERED)) {
			if (mtp_card->istimer++ == 0)
				snd_mtpav_add_output_timer(mtp_card);
			portp->mode |= MTPAV_MODE_OUTPUT_TRIGGERED;
		}
	} else {
		portp->mode &= ~MTPAV_MODE_OUTPUT_TRIGGERED;
		if (--mtp_card->istimer == 0)
			snd_mtpav_remove_output_timer(mtp_card);
	}
	spin_unlock_irqrestore(&mtp_card->spinlock, flags);

	if (up)
		snd_mtpav_output_write(substream);
}

/*
 * midi interrupt for inputs
 */

static void snd_mtpav_inmidi_process(struct mtpav *mcrd, u8 inbyte)
{
	struct mtpav_port *portp;

	if ((int)mcrd->inmidiport > mcrd->num_ports * 2 + MTPAV_PIDX_BROADCAST)
		return;

	portp = &mcrd->ports[mcrd->inmidiport];
	if (portp->mode & MTPAV_MODE_INPUT_TRIGGERED)
		snd_rawmidi_receive(portp->input, &inbyte, 1);
}

static void snd_mtpav_inmidi_h(struct mtpav *mcrd, u8 inbyte)
{
	if (inbyte >= 0xf8) {
		/* real-time midi code */
		snd_mtpav_inmidi_process(mcrd, inbyte);
		return;
	}

	if (mcrd->inmidistate == 0) {	// awaiting command
		if (inbyte == 0xf5)	// MTP port #
			mcrd->inmidistate = 1;
		else
			snd_mtpav_inmidi_process(mcrd, inbyte);
	} else if (mcrd->inmidistate) {
		mcrd->inmidiport = translate_hwport_to_subdevice(mcrd, inbyte);
		mcrd->inmidistate = 0;
	}
}

static void snd_mtpav_read_bytes(struct mtpav *mcrd)
{
	u8 clrread, setread;
	u8 mtp_read_byte;
	u8 sr, cbyt;
	int i;

	u8 sbyt = snd_mtpav_getreg(mcrd, SREG);

	/* printk(KERN_DEBUG "snd_mtpav_read_bytes() sbyt: 0x%x\n", sbyt); */

	if (!(sbyt & SIGS_BYTE))
		return;

	cbyt = snd_mtpav_getreg(mcrd, CREG);
	clrread = cbyt & (SIGC_READ ^ 0xff);
	setread = cbyt | SIGC_READ;

	do {

		mtp_read_byte = 0;
		for (i = 0; i < 4; i++) {
			snd_mtpav_mputreg(mcrd, CREG, setread);
			sr = snd_mtpav_getreg(mcrd, SREG);
			snd_mtpav_mputreg(mcrd, CREG, clrread);

			sr &= SIGS_IN0 | SIGS_IN1;
			sr >>= 4;
			mtp_read_byte |= sr << (i * 2);
		}

		snd_mtpav_inmidi_h(mcrd, mtp_read_byte);

		sbyt = snd_mtpav_getreg(mcrd, SREG);

	} while (sbyt & SIGS_BYTE);
}

static irqreturn_t snd_mtpav_irqh(int irq, void *dev_id)
{
	struct mtpav *mcard = dev_id;

	spin_lock(&mcard->spinlock);
	snd_mtpav_read_bytes(mcard);
	spin_unlock(&mcard->spinlock);
	return IRQ_HANDLED;
}

/*
 * get ISA resources
 */
static int snd_mtpav_get_ISA(struct mtpav *mcard)
{
	if ((mcard->res_port = request_region(port, 3, "MotuMTPAV MIDI")) == NULL) {
		snd_printk(KERN_ERR "MTVAP port 0x%lx is busy\n", port);
		return -EBUSY;
	}
	mcard->port = port;
	if (request_irq(irq, snd_mtpav_irqh, 0, "MOTU MTPAV", mcard)) {
		snd_printk(KERN_ERR "MTVAP IRQ %d busy\n", irq);
		return -EBUSY;
	}
	mcard->irq = irq;
	return 0;
}


/*
 */

static const struct snd_rawmidi_ops snd_mtpav_output = {
	.open =		snd_mtpav_output_open,
	.close =	snd_mtpav_output_close,
	.trigger =	snd_mtpav_output_trigger,
};

static const struct snd_rawmidi_ops snd_mtpav_input = {
	.open =		snd_mtpav_input_open,
	.close =	snd_mtpav_input_close,
	.trigger =	snd_mtpav_input_trigger,
};


/*
 * get RAWMIDI resources
 */

static void snd_mtpav_set_name(struct mtpav *chip,
			       struct snd_rawmidi_substream *substream)
{
	if (substream->number >= 0 && substream->number < chip->num_ports)
		sprintf(substream->name, "MTP direct %d", (substream->number % chip->num_ports) + 1);
	else if (substream->number >= 8 && substream->number < chip->num_ports * 2)
		sprintf(substream->name, "MTP remote %d", (substream->number % chip->num_ports) + 1);
	else if (substream->number == chip->num_ports * 2)
		strcpy(substream->name, "MTP computer");
	else if (substream->number == chip->num_ports * 2 + 1)
		strcpy(substream->name, "MTP ADAT");
	else
		strcpy(substream->name, "MTP broadcast");
}

static int snd_mtpav_get_RAWMIDI(struct mtpav *mcard)
{
	int rval;
	struct snd_rawmidi *rawmidi;
	struct snd_rawmidi_substream *substream;
	struct list_head *list;

	if (hwports < 1)
		hwports = 1;
	else if (hwports > 8)
		hwports = 8;
	mcard->num_ports = hwports;

	if ((rval = snd_rawmidi_new(mcard->card, "MotuMIDI", 0,
				    mcard->num_ports * 2 + MTPAV_PIDX_BROADCAST + 1,
				    mcard->num_ports * 2 + MTPAV_PIDX_BROADCAST + 1,
				    &mcard->rmidi)) < 0)
		return rval;
	rawmidi = mcard->rmidi;
	rawmidi->private_data = mcard;

	list_for_each(list, &rawmidi->streams[SNDRV_RAWMIDI_STREAM_INPUT].substreams) {
		substream = list_entry(list, struct snd_rawmidi_substream, list);
		snd_mtpav_set_name(mcard, substream);
		substream->ops = &snd_mtpav_input;
	}
	list_for_each(list, &rawmidi->streams[SNDRV_RAWMIDI_STREAM_OUTPUT].substreams) {
		substream = list_entry(list, struct snd_rawmidi_substream, list);
		snd_mtpav_set_name(mcard, substream);
		substream->ops = &snd_mtpav_output;
		mcard->ports[substream->number].hwport = translate_subdevice_to_hwport(mcard, substream->number);
	}
	rawmidi->info_flags |= SNDRV_RAWMIDI_INFO_OUTPUT | SNDRV_RAWMIDI_INFO_INPUT |
			       SNDRV_RAWMIDI_INFO_DUPLEX;
	sprintf(rawmidi->name, "MTP AV MIDI");
	return 0;
}

/*
 */

static void snd_mtpav_free(struct snd_card *card)
{
	struct mtpav *crd = card->private_data;
	unsigned long flags;

	spin_lock_irqsave(&crd->spinlock, flags);
	if (crd->istimer > 0)
		snd_mtpav_remove_output_timer(crd);
	spin_unlock_irqrestore(&crd->spinlock, flags);
	if (crd->irq >= 0)
		free_irq(crd->irq, (void *)crd);
	release_and_free_resource(crd->res_port);
}

/*
 */
static int snd_mtpav_probe(struct platform_device *dev)
{
	struct snd_card *card;
	int err;
	struct mtpav *mtp_card;

	err = snd_card_new(&dev->dev, index, id, THIS_MODULE,
			   sizeof(*mtp_card), &card);
	if (err < 0)
		return err;

	mtp_card = card->private_data;
	spin_lock_init(&mtp_card->spinlock);
	mtp_card->card = card;
	mtp_card->irq = -1;
	mtp_card->share_irq = 0;
	mtp_card->inmidistate = 0;
	mtp_card->outmidihwport = 0xffffffff;
	timer_setup(&mtp_card->timer, snd_mtpav_output_timer, 0);

	card->private_free = snd_mtpav_free;

	err = snd_mtpav_get_RAWMIDI(mtp_card);
	if (err < 0)
		goto __error;

	mtp_card->inmidiport = mtp_card->num_ports + MTPAV_PIDX_BROADCAST;

	err = snd_mtpav_get_ISA(mtp_card);
	if (err < 0)
		goto __error;

	strcpy(card->driver, "MTPAV");
	strcpy(card->shortname, "MTPAV on parallel port");
	snprintf(card->longname, sizeof(card->longname),
		 "MTPAV on parallel port at 0x%lx", port);

	snd_mtpav_portscan(mtp_card);

	err = snd_card_register(mtp_card->card);
	if (err < 0)
		goto __error;

	platform_set_drvdata(dev, card);
	printk(KERN_INFO "Motu MidiTimePiece on parallel port irq: %d ioport: 0x%lx\n", irq, port);
	return 0;

 __error:
	snd_card_free(card);
	return err;
}

static int snd_mtpav_remove(struct platform_device *devptr)
{
	snd_card_free(platform_get_drvdata(devptr));
	return 0;
}

#define SND_MTPAV_DRIVER	"snd_mtpav"

static struct platform_driver snd_mtpav_driver = {
	.probe		= snd_mtpav_probe,
	.remove		= snd_mtpav_remove,
	.driver		= {
		.name	= SND_MTPAV_DRIVER,
	},
};

static int __init alsa_card_mtpav_init(void)
{
	int err;

	if ((err = platform_driver_register(&snd_mtpav_driver)) < 0)
		return err;

	device = platform_device_register_simple(SND_MTPAV_DRIVER, -1, NULL, 0);
	if (!IS_ERR(device)) {
		if (platform_get_drvdata(device))
			return 0;
		platform_device_unregister(device);
		err = -ENODEV;
	} else
		err = PTR_ERR(device);
	platform_driver_unregister(&snd_mtpav_driver);
	return err;
}

static void __exit alsa_card_mtpav_exit(void)
{
	platform_device_unregister(device);
	platform_driver_unregister(&snd_mtpav_driver);
}

module_init(alsa_card_mtpav_init)
module_exit(alsa_card_mtpav_exit)
