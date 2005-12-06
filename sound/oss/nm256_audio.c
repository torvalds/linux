/* 
 * Audio driver for the NeoMagic 256AV and 256ZX chipsets in native
 * mode, with AC97 mixer support.
 *
 * Overall design and parts of this code stolen from vidc_*.c and
 * skeleton.c.
 *
 * Yeah, there are a lot of magic constants in here.  You tell ME what
 * they are.  I just get this stuff psychically, remember? 
 *
 * This driver was written by someone who wishes to remain anonymous. 
 * It is in the public domain, so share and enjoy.  Try to make a profit
 * off of it; go on, I dare you.  
 *
 * Changes:
 * 11-10-2000	Bartlomiej Zolnierkiewicz <bkz@linux-ide.org>
 *		Added some __init
 * 19-04-2001	Marcus Meissner <mm@caldera.de>
 *		Ported to 2.4 PCI API.
 */

#include <linux/pci.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/delay.h>
#include <linux/spinlock.h>
#include "sound_config.h"

static int nm256_debug;
static int force_load;

#include "nm256.h"
#include "nm256_coeff.h"

/* 
 * The size of the playback reserve.  When the playback buffer has less
 * than NM256_PLAY_WMARK_SIZE bytes to output, we request a new
 * buffer.
 */
#define NM256_PLAY_WMARK_SIZE 512

static struct audio_driver nm256_audio_driver;

static int nm256_grabInterrupt (struct nm256_info *card);
static int nm256_releaseInterrupt (struct nm256_info *card);
static irqreturn_t nm256_interrupt (int irq, void *dev_id, struct pt_regs *dummy);
static irqreturn_t nm256_interrupt_zx (int irq, void *dev_id, struct pt_regs *dummy);

/* These belong in linux/pci.h. */
#define PCI_DEVICE_ID_NEOMAGIC_NM256AV_AUDIO 0x8005
#define PCI_DEVICE_ID_NEOMAGIC_NM256ZX_AUDIO 0x8006
#define PCI_DEVICE_ID_NEOMAGIC_NM256XL_PLUS_AUDIO 0x8016

/* List of cards.  */
static struct nm256_info *nmcard_list;

/* Release the mapped-in memory for CARD.  */
static void
nm256_release_ports (struct nm256_info *card)
{
    int x;

    for (x = 0; x < 2; x++) {
	if (card->port[x].ptr != NULL) {
	    iounmap (card->port[x].ptr);
	    card->port[x].ptr = NULL;
	}
    }
}

/* 
 * Map in the memory ports for CARD, if they aren't already mapped in
 * and have been configured.  If successful, a zero value is returned;
 * otherwise any previously mapped-in areas are released and a non-zero
 * value is returned.
 *
 * This is invoked twice, once for each port.  Ideally it would only be
 * called once, but we now need to map in the second port in order to
 * check how much memory the card has on the 256ZX.
 */
static int
nm256_remap_ports (struct nm256_info *card)
{
    int x;

    for (x = 0; x < 2; x++) {
	if (card->port[x].ptr == NULL && card->port[x].end_offset > 0) {
	    u32 physaddr 
		= card->port[x].physaddr + card->port[x].start_offset;
	    u32 size 
		= card->port[x].end_offset - card->port[x].start_offset;

	    card->port[x].ptr = ioremap_nocache (physaddr, size);
						  
	    if (card->port[x].ptr == NULL) {
		printk (KERN_ERR "NM256: Unable to remap port %d\n", x + 1);
		nm256_release_ports (card);
		return -1;
	    }
	}
    }
    return 0;
}

/* Locate the card in our list. */
static struct nm256_info *
nm256_find_card (int dev)
{
    struct nm256_info *card;

    for (card = nmcard_list; card != NULL; card = card->next_card)
	if (card->dev[0] == dev || card->dev[1] == dev)
	    return card;

    return NULL;
}

/*
 * Ditto, but find the card struct corresponding to the mixer device DEV 
 * instead. 
 */
static struct nm256_info *
nm256_find_card_for_mixer (int dev)
{
    struct nm256_info *card;

    for (card = nmcard_list; card != NULL; card = card->next_card)
	if (card->mixer_oss_dev == dev)
	    return card;

    return NULL;
}

static int usecache;
static int buffertop;

/* Check to see if we're using the bank of cached coefficients. */
static int
nm256_cachedCoefficients (struct nm256_info *card)
{
    return usecache;
}

/* The actual rates supported by the card. */
static int samplerates[9] = {
    8000, 11025, 16000, 22050, 24000, 32000, 44100, 48000, 99999999
};

/*
 * Set the card samplerate, word size and stereo mode to correspond to
 * the settings in the CARD struct for the specified device in DEV.
 * We keep two separate sets of information, one for each device; the
 * hardware is not actually configured until a read or write is
 * attempted.
 */

static int
nm256_setInfo (int dev, struct nm256_info *card)
{
    int x;
    int w;
    int targetrate;

    if (card->dev[0] == dev)
	w = 0;
    else if (card->dev[1] == dev)
	w = 1;
    else
	return -ENODEV;

    targetrate = card->sinfo[w].samplerate;

    if ((card->sinfo[w].bits != 8 && card->sinfo[w].bits != 16)
	|| targetrate < samplerates[0]
	|| targetrate > samplerates[7])
	return -EINVAL;

    for (x = 0; x < 8; x++)
	if (targetrate < ((samplerates[x] + samplerates[x + 1]) / 2))
	    break;

    if (x < 8) {
	u8 ratebits = ((x << 4) & NM_RATE_MASK);
	if (card->sinfo[w].bits == 16)
	    ratebits |= NM_RATE_BITS_16;
	if (card->sinfo[w].stereo)
	    ratebits |= NM_RATE_STEREO;

	card->sinfo[w].samplerate = samplerates[x];


	if (card->dev_for_play == dev && card->playing) {
	    if (nm256_debug)
		printk (KERN_DEBUG "Setting play ratebits to 0x%x\n",
			ratebits);
	    nm256_loadCoefficient (card, 0, x);
	    nm256_writePort8 (card, 2,
			      NM_PLAYBACK_REG_OFFSET + NM_RATE_REG_OFFSET,
			      ratebits);
	}

	if (card->dev_for_record == dev && card->recording) {
	    if (nm256_debug)
		printk (KERN_DEBUG "Setting record ratebits to 0x%x\n",
			ratebits);
	    nm256_loadCoefficient (card, 1, x);
	    nm256_writePort8 (card, 2,
			      NM_RECORD_REG_OFFSET + NM_RATE_REG_OFFSET,
			      ratebits);
	}
	return 0;
    }
    else
	return -EINVAL;
}

/* Start the play process going. */
static void
startPlay (struct nm256_info *card)
{
    if (! card->playing) {
	card->playing = 1;
	if (nm256_grabInterrupt (card) == 0) {
	    nm256_setInfo (card->dev_for_play, card);

	    /* Enable playback engine and interrupts. */
	    nm256_writePort8 (card, 2, NM_PLAYBACK_ENABLE_REG,
			      NM_PLAYBACK_ENABLE_FLAG | NM_PLAYBACK_FREERUN);

	    /* Enable both channels. */
	    nm256_writePort16 (card, 2, NM_AUDIO_MUTE_REG, 0x0);
	}
    }
}

/* 
 * Request one chunk of AMT bytes from the recording device.  When the
 * operation is complete, the data will be copied into BUFFER and the
 * function DMAbuf_inputintr will be invoked.
 */

static void
nm256_startRecording (struct nm256_info *card, char *buffer, u32 amt)
{
    u32 endpos;
    int enableEngine = 0;
    u32 ringsize = card->recordBufferSize;
    unsigned long flags;

    if (amt > (ringsize / 2)) {
	/*
	 * Of course this won't actually work right, because the
	 * caller is going to assume we will give what we got asked
	 * for.
	 */
	printk (KERN_ERR "NM256: Read request too large: %d\n", amt);
	amt = ringsize / 2;
    }

    if (amt < 8) {
	printk (KERN_ERR "NM256: Read request too small; %d\n", amt);
	return;
    }

    spin_lock_irqsave(&card->lock,flags);
    /*
     * If we're not currently recording, set up the start and end registers
     * for the recording engine.
     */
    if (! card->recording) {
	card->recording = 1;
	if (nm256_grabInterrupt (card) == 0) {
	    card->curRecPos = 0;
	    nm256_setInfo (card->dev_for_record, card);
	    nm256_writePort32 (card, 2, NM_RBUFFER_START, card->abuf2);
	    nm256_writePort32 (card, 2, NM_RBUFFER_END,
				 card->abuf2 + ringsize);

	    nm256_writePort32 (card, 2, NM_RBUFFER_CURRP,
				 card->abuf2 + card->curRecPos);
	    enableEngine = 1;
	}
	else {
	    /* Not sure what else to do here.  */
	    spin_unlock_irqrestore(&card->lock,flags);
	    return;
	}
    }

    /* 
     * If we happen to go past the end of the buffer a bit (due to a
     * delayed interrupt) it's OK.  So might as well set the watermark
     * right at the end of the data we want.
     */
    endpos = card->abuf2 + ((card->curRecPos + amt) % ringsize);

    card->recBuf = buffer;
    card->requestedRecAmt = amt;
    nm256_writePort32 (card, 2, NM_RBUFFER_WMARK, endpos);
    /* Enable recording engine and interrupts. */
    if (enableEngine)
	nm256_writePort8 (card, 2, NM_RECORD_ENABLE_REG,
			    NM_RECORD_ENABLE_FLAG | NM_RECORD_FREERUN);

    spin_unlock_irqrestore(&card->lock,flags);
}

/* Stop the play engine. */
static void
stopPlay (struct nm256_info *card)
{
    /* Shut off sound from both channels. */
    nm256_writePort16 (card, 2, NM_AUDIO_MUTE_REG,
		       NM_AUDIO_MUTE_LEFT | NM_AUDIO_MUTE_RIGHT);
    /* Disable play engine. */
    nm256_writePort8 (card, 2, NM_PLAYBACK_ENABLE_REG, 0);
    if (card->playing) {
	nm256_releaseInterrupt (card);

	/* Reset the relevant state bits. */
	card->playing = 0;
	card->curPlayPos = 0;
    }
}

/* Stop recording. */
static void
stopRecord (struct nm256_info *card)
{
    /* Disable recording engine. */
    nm256_writePort8 (card, 2, NM_RECORD_ENABLE_REG, 0);

    if (card->recording) {
	nm256_releaseInterrupt (card);

	card->recording = 0;
	card->curRecPos = 0;
    }
}

/*
 * Ring buffers, man.  That's where the hip-hop, wild-n-wooly action's at.
 * 1972?  (Well, I suppose it was cheep-n-easy to implement.)
 *
 * Write AMT bytes of BUFFER to the playback ring buffer, and start the
 * playback engine running.  It will only accept up to 1/2 of the total
 * size of the ring buffer.  No check is made that we're about to overwrite
 * the currently-playing sample.
 */

static void
nm256_write_block (struct nm256_info *card, char *buffer, u32 amt)
{
    u32 ringsize = card->playbackBufferSize;
    u32 endstop;
    unsigned long flags;

    if (amt > (ringsize / 2)) {
	printk (KERN_ERR "NM256: Write request too large: %d\n", amt);
	amt = (ringsize / 2);
    }

    if (amt < NM256_PLAY_WMARK_SIZE) {
	printk (KERN_ERR "NM256: Write request too small: %d\n", amt);
	return;
    }

    card->curPlayPos %= ringsize;

    card->requested_amt = amt;

    spin_lock_irqsave(&card->lock,flags);

    if ((card->curPlayPos + amt) >= ringsize) {
	u32 rem = ringsize - card->curPlayPos;

	nm256_writeBuffer8 (card, buffer, 1,
			      card->abuf1 + card->curPlayPos,
			      rem);
	if (amt > rem)
	    nm256_writeBuffer8 (card, buffer + rem, 1, card->abuf1,
				  amt - rem);
    } 
    else
	nm256_writeBuffer8 (card, buffer, 1,
			      card->abuf1 + card->curPlayPos,
			      amt);

    /*
     * Setup the start-n-stop-n-limit registers, and start that engine
     * goin'. 
     *
     * Normally we just let it wrap around to avoid the click-click
     * action scene.
     */
    if (! card->playing) {
	/* The PBUFFER_END register in this case points to one sample
	   before the end of the buffer. */
	int w = (card->dev_for_play == card->dev[0] ? 0 : 1);
	int sampsize = (card->sinfo[w].bits == 16 ? 2 : 1);

	if (card->sinfo[w].stereo)
	    sampsize *= 2;

	/* Need to set the not-normally-changing-registers up. */
	nm256_writePort32 (card, 2, NM_PBUFFER_START,
			     card->abuf1 + card->curPlayPos);
	nm256_writePort32 (card, 2, NM_PBUFFER_END,
			     card->abuf1 + ringsize - sampsize);
	nm256_writePort32 (card, 2, NM_PBUFFER_CURRP,
			     card->abuf1 + card->curPlayPos);
    }
    endstop = (card->curPlayPos + amt - NM256_PLAY_WMARK_SIZE) % ringsize;
    nm256_writePort32 (card, 2, NM_PBUFFER_WMARK, card->abuf1 + endstop);

    if (! card->playing)
	startPlay (card);

    spin_unlock_irqrestore(&card->lock,flags);
}

/*  We just got a card playback interrupt; process it.  */
static void
nm256_get_new_block (struct nm256_info *card)
{
    /* Check to see how much got played so far. */
    u32 amt = nm256_readPort32 (card, 2, NM_PBUFFER_CURRP) - card->abuf1;

    if (amt >= card->playbackBufferSize) {
	printk (KERN_ERR "NM256: Sound playback pointer invalid!\n");
	amt = 0;
    }

    if (amt < card->curPlayPos)
	amt = (card->playbackBufferSize - card->curPlayPos) + amt;
    else
	amt -= card->curPlayPos;

    if (card->requested_amt > (amt + NM256_PLAY_WMARK_SIZE)) {
	u32 endstop =
	    card->curPlayPos + card->requested_amt - NM256_PLAY_WMARK_SIZE;
	nm256_writePort32 (card, 2, NM_PBUFFER_WMARK, card->abuf1 + endstop);
    } 
    else {
	card->curPlayPos += card->requested_amt;
	/* Get a new block to write.  This will eventually invoke
	   nm256_write_block () or stopPlay ().  */
	DMAbuf_outputintr (card->dev_for_play, 1);
    }
}

/* 
 * Read the last-recorded block from the ring buffer, copy it into the
 * saved buffer pointer, and invoke DMAuf_inputintr() with the recording
 * device. 
 */

static void
nm256_read_block (struct nm256_info *card)
{
    /* Grab the current position of the recording pointer. */
    u32 currptr = nm256_readPort32 (card, 2, NM_RBUFFER_CURRP) - card->abuf2;
    u32 amtToRead = card->requestedRecAmt;
    u32 ringsize = card->recordBufferSize;

    if (currptr >= card->recordBufferSize) {
	printk (KERN_ERR "NM256: Sound buffer record pointer invalid!\n");
        currptr = 0;
    }

    /*
     * This test is probably redundant; we shouldn't be here unless
     * it's true.
     */
    if (card->recording) {
	/* If we wrapped around, copy everything from the start of our
	   recording buffer to the end of the buffer. */
	if (currptr < card->curRecPos) {
	    u32 amt = min (ringsize - card->curRecPos, amtToRead);

	    nm256_readBuffer8 (card, card->recBuf, 1,
				 card->abuf2 + card->curRecPos,
				 amt);
	    amtToRead -= amt;
	    card->curRecPos += amt;
	    card->recBuf += amt;
	    if (card->curRecPos == ringsize)
		card->curRecPos = 0;
	}

	if ((card->curRecPos < currptr) && (amtToRead > 0)) {
	    u32 amt = min (currptr - card->curRecPos, amtToRead);
	    nm256_readBuffer8 (card, card->recBuf, 1,
				 card->abuf2 + card->curRecPos, amt);
	    card->curRecPos = ((card->curRecPos + amt) % ringsize);
	}
	card->recBuf = NULL;
	card->requestedRecAmt = 0;
	DMAbuf_inputintr (card->dev_for_record);
    }
}

/*
 * Initialize the hardware. 
 */
static void
nm256_initHw (struct nm256_info *card)
{
    /* Reset everything. */
    nm256_writePort8 (card, 2, 0x0, 0x11);
    nm256_writePort16 (card, 2, 0x214, 0);

    stopRecord (card);
    stopPlay (card);
}

/* 
 * Handle a potential interrupt for the device referred to by DEV_ID. 
 *
 * I don't like the cut-n-paste job here either between the two routines,
 * but there are sufficient differences between the two interrupt handlers
 * that parameterizing it isn't all that great either.  (Could use a macro,
 * I suppose...yucky bleah.)
 */

static irqreturn_t
nm256_interrupt (int irq, void *dev_id, struct pt_regs *dummy)
{
    struct nm256_info *card = (struct nm256_info *)dev_id;
    u16 status;
    static int badintrcount;
    int handled = 0;

    if ((card == NULL) || (card->magsig != NM_MAGIC_SIG)) {
	printk (KERN_ERR "NM256: Bad card pointer\n");
	return IRQ_NONE;
    }

    status = nm256_readPort16 (card, 2, NM_INT_REG);

    /* Not ours. */
    if (status == 0) {
	if (badintrcount++ > 1000) {
	    /*
	     * I'm not sure if the best thing is to stop the card from
	     * playing or just release the interrupt (after all, we're in
	     * a bad situation, so doing fancy stuff may not be such a good
	     * idea).
	     *
	     * I worry about the card engine continuing to play noise
	     * over and over, however--that could become a very
	     * obnoxious problem.  And we know that when this usually
	     * happens things are fairly safe, it just means the user's
	     * inserted a PCMCIA card and someone's spamming us with IRQ 9s.
	     */

	    handled = 1;
	    if (card->playing)
		stopPlay (card);
	    if (card->recording)
		stopRecord (card);
	    badintrcount = 0;
	}
	return IRQ_RETVAL(handled);
    }

    badintrcount = 0;

    /* Rather boring; check for individual interrupts and process them. */

    if (status & NM_PLAYBACK_INT) {
	handled = 1;
	status &= ~NM_PLAYBACK_INT;
	NM_ACK_INT (card, NM_PLAYBACK_INT);

	if (card->playing)
	    nm256_get_new_block (card);
    }

    if (status & NM_RECORD_INT) {
	handled = 1;
	status &= ~NM_RECORD_INT;
	NM_ACK_INT (card, NM_RECORD_INT);

	if (card->recording)
	    nm256_read_block (card);
    }

    if (status & NM_MISC_INT_1) {
	u8 cbyte;

	handled = 1;
	status &= ~NM_MISC_INT_1;
	printk (KERN_ERR "NM256: Got misc interrupt #1\n");
	NM_ACK_INT (card, NM_MISC_INT_1);
	nm256_writePort16 (card, 2, NM_INT_REG, 0x8000);
	cbyte = nm256_readPort8 (card, 2, 0x400);
	nm256_writePort8 (card, 2, 0x400, cbyte | 2);
    }

    if (status & NM_MISC_INT_2) {
	u8 cbyte;

	handled = 1;
	status &= ~NM_MISC_INT_2;
	printk (KERN_ERR "NM256: Got misc interrupt #2\n");
	NM_ACK_INT (card, NM_MISC_INT_2);
	cbyte = nm256_readPort8 (card, 2, 0x400);
	nm256_writePort8 (card, 2, 0x400, cbyte & ~2);
    }

    /* Unknown interrupt. */
    if (status) {
	handled = 1;
	printk (KERN_ERR "NM256: Fire in the hole! Unknown status 0x%x\n",
		status);
	/* Pray. */
	NM_ACK_INT (card, status);
    }
    return IRQ_RETVAL(handled);
}

/*
 * Handle a potential interrupt for the device referred to by DEV_ID.
 * This handler is for the 256ZX, and is very similar to the non-ZX
 * routine.
 */

static irqreturn_t
nm256_interrupt_zx (int irq, void *dev_id, struct pt_regs *dummy)
{
    struct nm256_info *card = (struct nm256_info *)dev_id;
    u32 status;
    static int badintrcount;
    int handled = 0;

    if ((card == NULL) || (card->magsig != NM_MAGIC_SIG)) {
	printk (KERN_ERR "NM256: Bad card pointer\n");
	return IRQ_NONE;
    }

    status = nm256_readPort32 (card, 2, NM_INT_REG);

    /* Not ours. */
    if (status == 0) {
	if (badintrcount++ > 1000) {
	    printk (KERN_ERR "NM256: Releasing interrupt, over 1000 invalid interrupts\n");
	    /*
	     * I'm not sure if the best thing is to stop the card from
	     * playing or just release the interrupt (after all, we're in
	     * a bad situation, so doing fancy stuff may not be such a good
	     * idea).
	     *
	     * I worry about the card engine continuing to play noise
	     * over and over, however--that could become a very
	     * obnoxious problem.  And we know that when this usually
	     * happens things are fairly safe, it just means the user's
	     * inserted a PCMCIA card and someone's spamming us with 
	     * IRQ 9s.
	     */

	    handled = 1;
	    if (card->playing)
		stopPlay (card);
	    if (card->recording)
		stopRecord (card);
	    badintrcount = 0;
	}
	return IRQ_RETVAL(handled);
    }

    badintrcount = 0;

    /* Rather boring; check for individual interrupts and process them. */

    if (status & NM2_PLAYBACK_INT) {
	handled = 1;
	status &= ~NM2_PLAYBACK_INT;
	NM2_ACK_INT (card, NM2_PLAYBACK_INT);

	if (card->playing)
	    nm256_get_new_block (card);
    }

    if (status & NM2_RECORD_INT) {
	handled = 1;
	status &= ~NM2_RECORD_INT;
	NM2_ACK_INT (card, NM2_RECORD_INT);

	if (card->recording)
	    nm256_read_block (card);
    }

    if (status & NM2_MISC_INT_1) {
	u8 cbyte;

	handled = 1;
	status &= ~NM2_MISC_INT_1;
	printk (KERN_ERR "NM256: Got misc interrupt #1\n");
	NM2_ACK_INT (card, NM2_MISC_INT_1);
	cbyte = nm256_readPort8 (card, 2, 0x400);
	nm256_writePort8 (card, 2, 0x400, cbyte | 2);
    }

    if (status & NM2_MISC_INT_2) {
	u8 cbyte;

	handled = 1;
	status &= ~NM2_MISC_INT_2;
	printk (KERN_ERR "NM256: Got misc interrupt #2\n");
	NM2_ACK_INT (card, NM2_MISC_INT_2);
	cbyte = nm256_readPort8 (card, 2, 0x400);
	nm256_writePort8 (card, 2, 0x400, cbyte & ~2);
    }

    /* Unknown interrupt. */
    if (status) {
	handled = 1;
	printk (KERN_ERR "NM256: Fire in the hole! Unknown status 0x%x\n",
		status);
	/* Pray. */
	NM2_ACK_INT (card, status);
    }
    return IRQ_RETVAL(handled);
}

/* 
 * Request our interrupt.
 */
static int
nm256_grabInterrupt (struct nm256_info *card)
{
    if (card->has_irq++ == 0) {
	if (request_irq (card->irq, card->introutine, SA_SHIRQ,
			 "NM256_audio", card) < 0) {
	    printk (KERN_ERR "NM256: can't obtain IRQ %d\n", card->irq);
	    return -1;
	}
    }
    return 0;
}

/* 
 * Release our interrupt. 
 */
static int
nm256_releaseInterrupt (struct nm256_info *card)
{
    if (card->has_irq <= 0) {
	printk (KERN_ERR "nm256: too many calls to releaseInterrupt\n");
	return -1;
    }
    card->has_irq--;
    if (card->has_irq == 0) {
	free_irq (card->irq, card);
    }
    return 0;
}

/*
 * Waits for the mixer to become ready to be written; returns a zero value
 * if it timed out.
 */

static int
nm256_isReady (struct ac97_hwint *dev)
{
    struct nm256_info *card = (struct nm256_info *)dev->driver_private;
    int t2 = 10;
    u32 testaddr;
    u16 testb;
    int done = 0;

    if (card->magsig != NM_MAGIC_SIG) {
	printk (KERN_ERR "NM256: Bad magic signature in isReady!\n");
	return 0;
    }

    testaddr = card->mixer_status_offset;
    testb = card->mixer_status_mask;

    /* 
     * Loop around waiting for the mixer to become ready. 
     */
    while (! done && t2-- > 0) {
	if ((nm256_readPort16 (card, 2, testaddr) & testb) == 0)
	    done = 1;
	else
	    udelay (100);
    }
    return done;
}

/*
 * Return the contents of the AC97 mixer register REG.  Returns a positive
 * value if successful, or a negative error code.
 */
static int
nm256_readAC97Reg (struct ac97_hwint *dev, u8 reg)
{
    struct nm256_info *card = (struct nm256_info *)dev->driver_private;

    if (card->magsig != NM_MAGIC_SIG) {
	printk (KERN_ERR "NM256: Bad magic signature in readAC97Reg!\n");
	return -EINVAL;
    }

    if (reg < 128) {
	int res;

	nm256_isReady (dev);
	res = nm256_readPort16 (card, 2, card->mixer + reg);
	/* Magic delay.  Bleah yucky.  */
        udelay (1000);
	return res;
    }
    else
	return -EINVAL;
}

/* 
 * Writes VALUE to AC97 mixer register REG.  Returns 0 if successful, or
 * a negative error code. 
 */
static int
nm256_writeAC97Reg (struct ac97_hwint *dev, u8 reg, u16 value)
{
    unsigned long flags;
    int tries = 2;
    int done = 0;
    u32 base;

    struct nm256_info *card = (struct nm256_info *)dev->driver_private;

    if (card->magsig != NM_MAGIC_SIG) {
	printk (KERN_ERR "NM256: Bad magic signature in writeAC97Reg!\n");
	return -EINVAL;
    }

    base = card->mixer;

    spin_lock_irqsave(&card->lock,flags);

    nm256_isReady (dev);

    /* Wait for the write to take, too. */
    while ((tries-- > 0) && !done) {
	nm256_writePort16 (card, 2, base + reg, value);
	if (nm256_isReady (dev)) {
	    done = 1;
	    break;
	}

    }

    spin_unlock_irqrestore(&card->lock,flags);
    udelay (1000);

    return ! done;
}

/* 
 * Initial register values to be written to the AC97 mixer.
 * While most of these are identical to the reset values, we do this
 * so that we have most of the register contents cached--this avoids
 * reading from the mixer directly (which seems to be problematic,
 * probably due to ignorance).
 */
struct initialValues 
{
    unsigned short port;
    unsigned short value;
};

static struct initialValues nm256_ac97_initial_values[] = 
{
    { AC97_MASTER_VOL_STEREO, 0x8000 },
    { AC97_HEADPHONE_VOL,     0x8000 },
    { AC97_MASTER_VOL_MONO,   0x0000 },
    { AC97_PCBEEP_VOL,        0x0000 },
    { AC97_PHONE_VOL,         0x0008 },
    { AC97_MIC_VOL,           0x8000 },
    { AC97_LINEIN_VOL,        0x8808 },
    { AC97_CD_VOL,            0x8808 },
    { AC97_VIDEO_VOL,         0x8808 },
    { AC97_AUX_VOL,           0x8808 },
    { AC97_PCMOUT_VOL,        0x0808 },
    { AC97_RECORD_SELECT,     0x0000 },
    { AC97_RECORD_GAIN,       0x0B0B },
    { AC97_GENERAL_PURPOSE,   0x0000 },
    { 0xffff, 0xffff }
};

/* Initialize the AC97 into a known state.  */
static int
nm256_resetAC97 (struct ac97_hwint *dev)
{
    struct nm256_info *card = (struct nm256_info *)dev->driver_private;
    int x;

    if (card->magsig != NM_MAGIC_SIG) {
	printk (KERN_ERR "NM256: Bad magic signature in resetAC97!\n");
	return -EINVAL;
    }

    /* Reset the mixer.  'Tis magic!  */
    nm256_writePort8 (card, 2, 0x6c0, 1);
//  nm256_writePort8 (card, 2, 0x6cc, 0x87);	/* This crashes Dell latitudes */
    nm256_writePort8 (card, 2, 0x6cc, 0x80);
    nm256_writePort8 (card, 2, 0x6cc, 0x0);

    if (! card->mixer_values_init) {
	for (x = 0; nm256_ac97_initial_values[x].port != 0xffff; x++) {
	    ac97_put_register (dev,
			       nm256_ac97_initial_values[x].port,
			       nm256_ac97_initial_values[x].value);
	    card->mixer_values_init = 1;
	}
    }

    return 0;
}

/*
 * We don't do anything particularly special here; it just passes the
 * mixer ioctl to the AC97 driver.
 */
static int
nm256_default_mixer_ioctl (int dev, unsigned int cmd, void __user *arg)
{
    struct nm256_info *card = nm256_find_card_for_mixer (dev);
    if (card != NULL)
	return ac97_mixer_ioctl (&(card->mdev), cmd, arg);
    else
	return -ENODEV;
}

static struct mixer_operations nm256_mixer_operations = {
	.owner	= THIS_MODULE,
	.id	= "NeoMagic",
	.name	= "NM256AC97Mixer",
	.ioctl	= nm256_default_mixer_ioctl
};

/*
 * Default settings for the OSS mixer.  These are set last, after the
 * mixer is initialized.
 *
 * I "love" C sometimes.  Got braces?
 */
static struct ac97_mixer_value_list mixer_defaults[] = {
    { SOUND_MIXER_VOLUME,  { { 85, 85 } } },
    { SOUND_MIXER_SPEAKER, { { 100 } } },
    { SOUND_MIXER_PCM,     { { 65, 65 } } },
    { SOUND_MIXER_CD,      { { 65, 65 } } },
    { -1,                  {  { 0,  0 } } }
};


/* Installs the AC97 mixer into CARD.  */
static int __init
nm256_install_mixer (struct nm256_info *card)
{
    int mixer;

    card->mdev.reset_device = nm256_resetAC97;
    card->mdev.read_reg = nm256_readAC97Reg;
    card->mdev.write_reg = nm256_writeAC97Reg;
    card->mdev.driver_private = (void *)card;

    if (ac97_init (&(card->mdev)))
	return -1;

    mixer = sound_alloc_mixerdev();
    if (num_mixers >= MAX_MIXER_DEV) {
	printk ("NM256 mixer: Unable to alloc mixerdev\n");
	return -1;
    }

    mixer_devs[mixer] = &nm256_mixer_operations;
    card->mixer_oss_dev = mixer;

    /* Some reasonable default values.  */
    ac97_set_values (&(card->mdev), mixer_defaults);

    printk(KERN_INFO "Initialized AC97 mixer\n");
    return 0;
}

/* 
 * See if the signature left by the NM256 BIOS is intact; if so, we use
 * the associated address as the end of our audio buffer in the video
 * RAM.
 */

static void __init
nm256_peek_for_sig (struct nm256_info *card)
{
    u32 port1offset 
	= card->port[0].physaddr + card->port[0].end_offset - 0x0400;
    /* The signature is located 1K below the end of video RAM.  */
    char __iomem *temp = ioremap_nocache (port1offset, 16);
    /* Default buffer end is 5120 bytes below the top of RAM.  */
    u32 default_value = card->port[0].end_offset - 0x1400;
    u32 sig;

    /* Install the default value first, so we don't have to repeatedly
       do it if there is a problem.  */
    card->port[0].end_offset = default_value;

    if (temp == NULL) {
	printk (KERN_ERR "NM256: Unable to scan for card signature in video RAM\n");
	return;
    }
    sig = readl (temp);
    if ((sig & NM_SIG_MASK) == NM_SIGNATURE) {
	u32 pointer = readl (temp + 4);

	/*
	 * If it's obviously invalid, don't use it (the port already has a
	 * suitable default value set).
	 */
	if (pointer != 0xffffffff)
	    card->port[0].end_offset = pointer;

	printk (KERN_INFO "NM256: Found card signature in video RAM: 0x%x\n",
		pointer);
    }

    iounmap (temp);
}

/* 
 * Install a driver for the PCI device referenced by PCIDEV.
 * VERSTR is a human-readable version string.
 */

static int __devinit
nm256_install(struct pci_dev *pcidev, enum nm256rev rev, char *verstr)
{
    struct nm256_info *card;
    int x;

    if (pci_enable_device(pcidev))
	    return 0;

    card = kmalloc (sizeof (struct nm256_info), GFP_KERNEL);
    if (card == NULL) {
	printk (KERN_ERR "NM256: out of memory!\n");
	return 0;
    }

    card->magsig = NM_MAGIC_SIG;
    card->playing  = 0;
    card->recording = 0;
    card->rev = rev;
	spin_lock_init(&card->lock);

    /* Init the memory port info.  */
    for (x = 0; x < 2; x++) {
	card->port[x].physaddr = pci_resource_start (pcidev, x);
	card->port[x].ptr = NULL;
	card->port[x].start_offset = 0;
	card->port[x].end_offset = 0;
    }

    /* Port 2 is easy.  */
    card->port[1].start_offset = 0;
    card->port[1].end_offset = NM_PORT2_SIZE;

    /* Yuck.  But we have to map in port 2 so we can check how much RAM the
       card has.  */
    if (nm256_remap_ports (card)) {
	kfree (card);
	return 0;
    }

    /* 
     * The NM256 has two memory ports.  The first port is nothing
     * more than a chunk of video RAM, which is used as the I/O ring
     * buffer.  The second port has the actual juicy stuff (like the
     * mixer and the playback engine control registers).
     */

    if (card->rev == REV_NM256AV) {
	/* Ok, try to see if this is a non-AC97 version of the hardware. */
	int pval = nm256_readPort16 (card, 2, NM_MIXER_PRESENCE);
	if ((pval & NM_PRESENCE_MASK) != NM_PRESENCE_VALUE) {
	    if (! force_load) {
		printk (KERN_ERR "NM256: This doesn't look to me like the AC97-compatible version.\n");
		printk (KERN_ERR "       You can force the driver to load by passing in the module\n");
		printk (KERN_ERR "       parameter:\n");
		printk (KERN_ERR "              force_load = 1\n");
		printk (KERN_ERR "\n");
		printk (KERN_ERR "       More likely, you should be using the appropriate SB-16 or\n");
		printk (KERN_ERR "       CS4232 driver instead.  (If your BIOS has settings for\n");
		printk (KERN_ERR "       IRQ and/or DMA for the sound card, this is *not* the correct\n");
		printk (KERN_ERR "       driver to use.)\n");
		nm256_release_ports (card);
		kfree (card);
		return 0;
	    }
	    else {
		printk (KERN_INFO "NM256: Forcing driver load as per user request.\n");
	    }
	}
	else {
	 /*   printk (KERN_INFO "NM256: Congratulations. You're not running Eunice.\n")*/;
	}
	card->port[0].end_offset = 2560 * 1024;
	card->introutine = nm256_interrupt;
	card->mixer_status_offset = NM_MIXER_STATUS_OFFSET;
	card->mixer_status_mask = NM_MIXER_READY_MASK;
    } 
    else {
	/* Not sure if there is any relevant detect for the ZX or not.  */
	if (nm256_readPort8 (card, 2, 0xa0b) != 0)
	    card->port[0].end_offset = 6144 * 1024;
	else
	    card->port[0].end_offset = 4096 * 1024;

	card->introutine = nm256_interrupt_zx;
	card->mixer_status_offset = NM2_MIXER_STATUS_OFFSET;
	card->mixer_status_mask = NM2_MIXER_READY_MASK;
    }

    if (buffertop >= 98304 && buffertop < card->port[0].end_offset)
	card->port[0].end_offset = buffertop;
    else
	nm256_peek_for_sig (card);

    card->port[0].start_offset = card->port[0].end_offset - 98304;

    printk (KERN_INFO "NM256: Mapping port 1 from 0x%x - 0x%x\n",
	    card->port[0].start_offset, card->port[0].end_offset);

    if (nm256_remap_ports (card)) {
	kfree (card);
	return 0;
    }

    /* See if we can get the interrupt. */

    card->irq = pcidev->irq;
    card->has_irq = 0;

    if (nm256_grabInterrupt (card) != 0) {
	nm256_release_ports (card);
	kfree (card);
	return 0;
    }

    nm256_releaseInterrupt (card);

    /*
     *	Init the board.
     */

    card->playbackBufferSize = 16384;
    card->recordBufferSize = 16384;

    card->coeffBuf = card->port[0].end_offset - NM_MAX_COEFFICIENT;
    card->abuf2 = card->coeffBuf - card->recordBufferSize;
    card->abuf1 = card->abuf2 - card->playbackBufferSize;
    card->allCoeffBuf = card->abuf2 - (NM_TOTAL_COEFF_COUNT * 4);

    /* Fixed setting. */
    card->mixer = NM_MIXER_OFFSET;
    card->mixer_values_init = 0;

    card->is_open_play = 0;
    card->is_open_record = 0;

    card->coeffsCurrent = 0;

    card->opencnt[0] = 0; card->opencnt[1] = 0;

    /* Reasonable default settings, but largely unnecessary. */
    for (x = 0; x < 2; x++) {
	card->sinfo[x].bits = 8;
	card->sinfo[x].stereo = 0;
	card->sinfo[x].samplerate = 8000;
    }

    nm256_initHw (card);

    for (x = 0; x < 2; x++) {
	if ((card->dev[x] =
	     sound_install_audiodrv(AUDIO_DRIVER_VERSION,
				    "NM256", &nm256_audio_driver,
				    sizeof(struct audio_driver),
				    DMA_NODMA, AFMT_U8 | AFMT_S16_LE,
				    NULL, -1, -1)) >= 0) {
	    /* 1K minimum buffer size. */
	    audio_devs[card->dev[x]]->min_fragment = 10;
	    /* Maximum of 8K buffer size. */
	    audio_devs[card->dev[x]]->max_fragment = 13;
	}
	else {
	    printk(KERN_ERR "NM256: Too many PCM devices available\n");
	    nm256_release_ports (card);
	    kfree (card);
	    return 0;
	}
    }

    pci_set_drvdata(pcidev,card);

    /* Insert the card in the list.  */
    card->next_card = nmcard_list;
    nmcard_list = card;

    printk(KERN_INFO "Initialized NeoMagic %s audio in PCI native mode\n",
	   verstr);

    /* 
     * And our mixer.  (We should allow support for other mixers, maybe.)
     */

    nm256_install_mixer (card);

    return 1;
}


static int __devinit
nm256_probe(struct pci_dev *pcidev,const struct pci_device_id *pciid)
{
    if (pcidev->device == PCI_DEVICE_ID_NEOMAGIC_NM256AV_AUDIO)
	return nm256_install(pcidev, REV_NM256AV, "256AV");
    if (pcidev->device == PCI_DEVICE_ID_NEOMAGIC_NM256ZX_AUDIO)
	return nm256_install(pcidev, REV_NM256ZX, "256ZX");
    if (pcidev->device == PCI_DEVICE_ID_NEOMAGIC_NM256XL_PLUS_AUDIO)
	return nm256_install(pcidev, REV_NM256ZX, "256XL+");
    return -1; /* should not come here ... */
}

static void __devinit
nm256_remove(struct pci_dev *pcidev) {
    struct nm256_info *xcard = pci_get_drvdata(pcidev);
    struct nm256_info *card,*next_card = NULL;

    for (card = nmcard_list; card != NULL; card = next_card) {
	next_card = card->next_card;
	if (card == xcard) {
	    stopPlay (card);
	    stopRecord (card);
	    if (card->has_irq)
		free_irq (card->irq, card);
	    nm256_release_ports (card);
	    sound_unload_mixerdev (card->mixer_oss_dev);
	    sound_unload_audiodev (card->dev[0]);
	    sound_unload_audiodev (card->dev[1]);
	    kfree (card);
	    break;
	}
    }
    if (nmcard_list == card)
    	nmcard_list = next_card;
}

/*
 * Open the device
 *
 * DEV  - device
 * MODE - mode to open device (logical OR of OPEN_READ and OPEN_WRITE)
 *
 * Called when opening the DMAbuf               (dmabuf.c:259)
 */
static int
nm256_audio_open(int dev, int mode)
{
    struct nm256_info *card = nm256_find_card (dev);
    int w;
	
    if (card == NULL)
	return -ENODEV;

    if (card->dev[0] == dev)
	w = 0;
    else if (card->dev[1] == dev)
	w = 1;
    else
	return -ENODEV;

    if (card->opencnt[w] > 0)
	return -EBUSY;

    /* No bits set? Huh? */
    if (! ((mode & OPEN_READ) || (mode & OPEN_WRITE)))
	return -EIO;

    /*
     * If it's open for both read and write, and the card's currently
     * being read or written to, then do the opposite of what has
     * already been done.  Otherwise, don't specify any mode until the
     * user actually tries to do I/O.  (Some programs open the device
     * for both read and write, but only actually do reading or writing.)
     */

    if ((mode & OPEN_WRITE) && (mode & OPEN_READ)) {
	if (card->is_open_play)
	    mode = OPEN_WRITE;
	else if (card->is_open_record)
	    mode = OPEN_READ;
	else mode = 0;
    }
	
    if (mode & OPEN_WRITE) {
	if (card->is_open_play == 0) {
	    card->dev_for_play = dev;
	    card->is_open_play = 1;
	}
	else
	    return -EBUSY;
    }

    if (mode & OPEN_READ) {
	if (card->is_open_record == 0) {
	    card->dev_for_record = dev;
	    card->is_open_record = 1;
	}
	else
	    return -EBUSY;
    }

    card->opencnt[w]++;
    return 0;
}

/*
 * Close the device
 *
 * DEV  - device
 *
 * Called when closing the DMAbuf               (dmabuf.c:477)
 *      after halt_xfer
 */
static void
nm256_audio_close(int dev)
{
    struct nm256_info *card = nm256_find_card (dev);
	
    if (card != NULL) {
	int w;

	if (card->dev[0] == dev)
	    w = 0;
	else if (card->dev[1] == dev)
	    w = 1;
	else
	    return;

	card->opencnt[w]--;
	if (card->opencnt[w] <= 0) {
	    card->opencnt[w] = 0;

	    if (card->dev_for_play == dev) {
		stopPlay (card);
		card->is_open_play = 0;
		card->dev_for_play = -1;
	    }

	    if (card->dev_for_record == dev) {
		stopRecord (card);
		card->is_open_record = 0;
		card->dev_for_record = -1;
	    }
	}
    }
}

/* Standard ioctl handler. */
static int
nm256_audio_ioctl(int dev, unsigned int cmd, void __user *arg)
{
    int ret;
    u32 oldinfo;
    int w;

    struct nm256_info *card = nm256_find_card (dev);

    if (card == NULL)
	return -ENODEV;

    if (dev == card->dev[0])
	w = 0;
    else
	w = 1;

    /* 
     * The code here is messy.  There are probably better ways to do
     * it.  (It should be possible to handle it the same way the AC97 mixer 
     * is done.)
     */
    switch (cmd)
	{
	case SOUND_PCM_WRITE_RATE:
	    if (get_user(ret, (int __user *) arg))
		return -EFAULT;

	    if (ret != 0) {
		oldinfo = card->sinfo[w].samplerate;
		card->sinfo[w].samplerate = ret;
		ret = nm256_setInfo(dev, card);
		if (ret != 0)
		    card->sinfo[w].samplerate = oldinfo;
	    }
	    if (ret == 0)
		ret = card->sinfo[w].samplerate;
	    break;

	case SOUND_PCM_READ_RATE:
	    ret = card->sinfo[w].samplerate;
	    break;

	case SNDCTL_DSP_STEREO:
	    if (get_user(ret, (int __user *) arg))
		return -EFAULT;

	    card->sinfo[w].stereo = ret ? 1 : 0;
	    ret = nm256_setInfo (dev, card);
	    if (ret == 0)
		ret = card->sinfo[w].stereo;

	    break;

	case SOUND_PCM_WRITE_CHANNELS:
	    if (get_user(ret, (int __user *) arg))
		return -EFAULT;

	    if (ret < 1 || ret > 3)
		ret = card->sinfo[w].stereo + 1;
	    else {
		card->sinfo[w].stereo = ret - 1;
		ret = nm256_setInfo (dev, card);
		if (ret == 0)
		    ret = card->sinfo[w].stereo + 1;
	    }
	    break;

	case SOUND_PCM_READ_CHANNELS:
	    ret = card->sinfo[w].stereo + 1;
	    break;

	case SNDCTL_DSP_SETFMT:
	    if (get_user(ret, (int __user *) arg))
		return -EFAULT;

	    if (ret != 0) {
		oldinfo = card->sinfo[w].bits;
		card->sinfo[w].bits = ret;
		ret = nm256_setInfo (dev, card);
		if (ret != 0)
		    card->sinfo[w].bits = oldinfo;
	    }
	    if (ret == 0)
		ret = card->sinfo[w].bits;
	    break;

	case SOUND_PCM_READ_BITS:
	    ret = card->sinfo[w].bits;
	    break;

	default:
	    return -EINVAL;
	}
    return put_user(ret, (int __user *) arg);
}

/*
 * Given the sound device DEV and an associated physical buffer PHYSBUF, 
 * return a pointer to the actual buffer in kernel space. 
 *
 * This routine should exist as part of the soundcore routines.
 */

static char *
nm256_getDMAbuffer (int dev, unsigned long physbuf)
{
    struct audio_operations *adev = audio_devs[dev];
    struct dma_buffparms *dmap = adev->dmap_out;
    char *dma_start =
	(char *)(physbuf - (unsigned long)dmap->raw_buf_phys 
		 + (unsigned long)dmap->raw_buf);

    return dma_start;
}


/*
 * Output a block to sound device
 *
 * dev          - device number
 * buf          - physical address of buffer
 * total_count  - total byte count in buffer
 * intrflag     - set if this has been called from an interrupt 
 *				  (via DMAbuf_outputintr)
 * restart_dma  - set if engine needs to be re-initialised
 *
 * Called when:
 *  1. Starting output                                  (dmabuf.c:1327)
 *  2.                                                  (dmabuf.c:1504)
 *  3. A new buffer needs to be sent to the device      (dmabuf.c:1579)
 */
static void
nm256_audio_output_block(int dev, unsigned long physbuf,
				       int total_count, int intrflag)
{
    struct nm256_info *card = nm256_find_card (dev);

    if (card != NULL) {
	char *dma_buf = nm256_getDMAbuffer (dev, physbuf);
	card->is_open_play = 1;
	card->dev_for_play = dev;
	nm256_write_block (card, dma_buf, total_count);
    }
}

/* Ditto, but do recording instead.  */
static void
nm256_audio_start_input(int dev, unsigned long physbuf, int count,
			int intrflag)
{
    struct nm256_info *card = nm256_find_card (dev);

    if (card != NULL) {
	char *dma_buf = nm256_getDMAbuffer (dev, physbuf);
	card->is_open_record = 1;
	card->dev_for_record = dev;
	nm256_startRecording (card, dma_buf, count);
    }
}

/* 
 * Prepare for inputting samples to DEV. 
 * Each requested buffer will be BSIZE byes long, with a total of
 * BCOUNT buffers. 
 */

static int
nm256_audio_prepare_for_input(int dev, int bsize, int bcount)
{
    struct nm256_info *card = nm256_find_card (dev);

    if (card == NULL) 
	return -ENODEV;

    if (card->is_open_record && card->dev_for_record != dev)
	return -EBUSY;

    audio_devs[dev]->dmap_in->flags |= DMA_NODMA;
    return 0;
}

/*
 * Prepare for outputting samples to `dev'
 *
 * Each buffer that will be passed will be `bsize' bytes long,
 * with a total of `bcount' buffers.
 *
 * Called when:
 *  1. A trigger enables audio output                   (dmabuf.c:978)
 *  2. We get a write buffer without dma_mode setup     (dmabuf.c:1152)
 *  3. We restart a transfer                            (dmabuf.c:1324)
 */

static int
nm256_audio_prepare_for_output(int dev, int bsize, int bcount)
{
    struct nm256_info *card = nm256_find_card (dev);

    if (card == NULL)
	return -ENODEV;

    if (card->is_open_play && card->dev_for_play != dev)
	return -EBUSY;

    audio_devs[dev]->dmap_out->flags |= DMA_NODMA;
    return 0;
}

/* Stop the current operations associated with DEV.  */
static void
nm256_audio_reset(int dev)
{
    struct nm256_info *card = nm256_find_card (dev);

    if (card != NULL) {
	if (card->dev_for_play == dev)
	    stopPlay (card);
	if (card->dev_for_record == dev)
	    stopRecord (card);
    }
}

static int
nm256_audio_local_qlen(int dev)
{
    return 0;
}

static struct audio_driver nm256_audio_driver =
{
	.owner			= THIS_MODULE,
	.open			= nm256_audio_open,
	.close			= nm256_audio_close,
	.output_block		= nm256_audio_output_block,
	.start_input		= nm256_audio_start_input,
	.ioctl			= nm256_audio_ioctl,
	.prepare_for_input	= nm256_audio_prepare_for_input,
	.prepare_for_output	= nm256_audio_prepare_for_output,
	.halt_io		= nm256_audio_reset,
	.local_qlen		= nm256_audio_local_qlen,
};

static struct pci_device_id nm256_pci_tbl[] = {
	{PCI_VENDOR_ID_NEOMAGIC, PCI_DEVICE_ID_NEOMAGIC_NM256AV_AUDIO,
	PCI_ANY_ID, PCI_ANY_ID, 0, 0},
	{PCI_VENDOR_ID_NEOMAGIC, PCI_DEVICE_ID_NEOMAGIC_NM256ZX_AUDIO,
	PCI_ANY_ID, PCI_ANY_ID, 0, 0},
	{PCI_VENDOR_ID_NEOMAGIC, PCI_DEVICE_ID_NEOMAGIC_NM256XL_PLUS_AUDIO,
	PCI_ANY_ID, PCI_ANY_ID, 0, 0},
	{0,}
};
MODULE_DEVICE_TABLE(pci, nm256_pci_tbl);
MODULE_LICENSE("GPL");


static struct pci_driver nm256_pci_driver = {
	.name		= "nm256_audio",
	.id_table	= nm256_pci_tbl,
	.probe		= nm256_probe,
	.remove		= nm256_remove,
};

module_param(usecache, bool, 0);
module_param(buffertop, int, 0);
module_param(nm256_debug, bool, 0644);
module_param(force_load, bool, 0);

static int __init do_init_nm256(void)
{
    printk (KERN_INFO "NeoMagic 256AV/256ZX audio driver, version 1.1p\n");
    return pci_register_driver(&nm256_pci_driver);
}

static void __exit cleanup_nm256 (void)
{
    pci_unregister_driver(&nm256_pci_driver);
}

module_init(do_init_nm256);
module_exit(cleanup_nm256);

/*
 * Local variables:
 *  c-basic-offset: 4
 * End:
 */
