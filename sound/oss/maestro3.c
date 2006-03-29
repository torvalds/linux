/*****************************************************************************
 *
 *      ESS Maestro3/Allegro driver for Linux 2.4.x
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
 *      Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 *    (c) Copyright 2000 Zach Brown <zab@zabbo.net>
 *
 * I need to thank many people for helping make this driver happen.  
 * As always, Eric Brombaugh was a hacking machine and killed many bugs
 * that I was too dumb to notice.  Howard Kim at ESS provided reference boards 
 * and as much docs as he could.  Todd and Mick at Dell tested snapshots on 
 * an army of laptops.  msw and deviant at Red Hat also humoured me by hanging
 * their laptops every few hours in the name of science.
 * 
 * Shouts go out to Mike "DJ XPCom" Ang.
 *
 * History
 *  v1.23 - Jun 5 2002 - Michael Olson <olson@cs.odu.edu>
 *   added a module option to allow selection of GPIO pin number 
 *   for external amp 
 *  v1.22 - Feb 28 2001 - Zach Brown <zab@zabbo.net>
 *   allocate mem at insmod/setup, rather than open
 *   limit pci dma addresses to 28bit, thanks guys.
 *  v1.21 - Feb 04 2001 - Zach Brown <zab@zabbo.net>
 *   fix up really dumb notifier -> suspend oops
 *  v1.20 - Jan 30 2001 - Zach Brown <zab@zabbo.net>
 *   get rid of pm callback and use pci_dev suspend/resume instead
 *   m3_probe cleanups, including pm oops think-o
 *  v1.10 - Jan 6 2001 - Zach Brown <zab@zabbo.net>
 *   revert to lame remap_page_range mmap() just to make it work
 *   record mmap fixed.
 *   fix up incredibly broken open/release resource management
 *   duh.  fix record format setting.
 *   add SMP locking and cleanup formatting here and there
 *  v1.00 - Dec 16 2000 - Zach Brown <zab@zabbo.net>
 *   port to sexy 2.4 interfaces
 *   properly align instance allocations so recording works
 *   clean up function namespace a little :/
 *   update PCI IDs based on mail from ESS
 *   arbitrarily bump version number to show its 2.4 now, 
 *      2.2 will stay 0., oss_audio port gets 2.
 *  v0.03 - Nov 05 2000 - Zach Brown <zab@zabbo.net>
 *   disable recording but allow dsp to be opened read 
 *   pull out most silly compat defines
 *  v0.02 - Nov 04 2000 - Zach Brown <zab@zabbo.net>
 *   changed clocking setup for m3, slowdown fixed.
 *   codec reset is hopefully reliable now
 *   rudimentary apm/power management makes suspend/resume work
 *  v0.01 - Oct 31 2000 - Zach Brown <zab@zabbo.net>
 *   first release
 *  v0.00 - Sep 09 2000 - Zach Brown <zab@zabbo.net>
 *   first pass derivation from maestro.c
 *
 * TODO
 *  in/out allocated contiguously so fullduplex mmap will work?
 *  no beep on init (mute)
 *  resetup msrc data memory if freq changes?
 *
 *  --
 *
 *  Allow me to ramble a bit about the m3 architecture.  The core of the
 *  chip is the 'assp', the custom ESS dsp that runs the show.  It has
 *  a small amount of code and data ram.  ESS drops binary dsp code images
 *  on our heads, but we don't get to see specs on the dsp.  
 *
 *  The constant piece of code on the dsp is the 'kernel'.  It also has a 
 *  chunk of the dsp memory that is statically set aside for its control
 *  info.  This is the KDATA defines in maestro3.h.  Part of its core
 *  data is a list of code addresses that point to the pieces of DSP code
 *  that it should walk through in its loop.  These other pieces of code
 *  do the real work.  The kernel presumably jumps into each of them in turn.
 *  These code images tend to have their own data area, and one can have
 *  multiple data areas representing different states for each of the 'client
 *  instance' code portions.  There is generally a list in the kernel data
 *  that points to the data instances for a given piece of code.
 *
 *  We've only been given the binary image for the 'minisrc', mini sample 
 *  rate converter.  This is rather annoying because it limits the work
 *  we can do on the dsp, but it also greatly simplifies the job of managing
 *  dsp data memory for the code and data for our playing streams :).  We
 *  statically allocate the minisrc code into a region we 'know' to be free
 *  based on the map of the binary kernel image we're loading.  We also 
 *  statically allocate the data areas for the maximum number of pcm streams
 *  we can be dealing with.  This max is set by the length of the static list
 *  in the kernel data that records the number of minisrc data regions we
 *  can have.  Thats right, all software dsp mixing with static code list
 *  limits.  Rock.
 *
 *  How sound goes in and out is still a relative mystery.  It appears
 *  that the dsp has the ability to get input and output through various
 *  'connections'.  To do IO from or to a connection, you put the address
 *  of the minisrc client area in the static kernel data lists for that 
 *  input or output.  so for pcm -> dsp -> mixer, we put the minisrc data
 *  instance in the DMA list and also in the list for the mixer.  I guess
 *  it Just Knows which is in/out, and we give some dma control info that
 *  helps.  There are all sorts of cool inputs/outputs that it seems we can't
 *  use without dsp code images that know how to use them.
 *
 *  So at init time we preload all the memory allocation stuff and set some
 *  system wide parameters.  When we really get a sound to play we build
 *  up its minisrc header (stream parameters, buffer addresses, input/output
 *  settings).  Then we throw its header on the various lists.  We also
 *  tickle some KDATA settings that ask the assp to raise clock interrupts
 *  and do some amount of software mixing before handing data to the ac97.
 *
 *  Sorry for the vague details.  Feel free to ask Eric or myself if you
 *  happen to be trying to use this driver elsewhere.  Please accept my
 *  apologies for the quality of the OSS support code, its passed through
 *  too many hands now and desperately wants to be rethought.
 */

/*****************************************************************************/

#include <linux/config.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/ctype.h>
#include <linux/ioport.h>
#include <linux/sched.h>
#include <linux/delay.h>
#include <linux/sound.h>
#include <linux/slab.h>
#include <linux/soundcard.h>
#include <linux/pci.h>
#include <linux/vmalloc.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/poll.h>
#include <linux/reboot.h>
#include <linux/spinlock.h>
#include <linux/ac97_codec.h>
#include <linux/wait.h>
#include <linux/mutex.h>


#include <asm/io.h>
#include <asm/dma.h>
#include <asm/uaccess.h>

#include "maestro3.h"

#define M_DEBUG 1

#define DRIVER_VERSION      "1.23"
#define M3_MODULE_NAME      "maestro3"
#define PFX                 M3_MODULE_NAME ": "

#define M3_STATE_MAGIC      0x734d724d
#define M3_CARD_MAGIC       0x646e6f50

#define ESS_FMT_STEREO      0x01
#define ESS_FMT_16BIT       0x02
#define ESS_FMT_MASK        0x03
#define ESS_DAC_SHIFT       0   
#define ESS_ADC_SHIFT       4

#define DAC_RUNNING         1
#define ADC_RUNNING         2

#define SND_DEV_DSP16       5 
   
#ifdef M_DEBUG
static int debug;
#define DPMOD   1   /* per module load */
#define DPSTR   2   /* per 'stream' */
#define DPSYS   3   /* per syscall */
#define DPCRAP  4   /* stuff the user shouldn't see unless they're really debuggin */
#define DPINT   5   /* per interrupt, LOTS */
#define DPRINTK(DP, args...) {if (debug >= (DP)) printk(KERN_DEBUG PFX args);}
#else
#define DPRINTK(x)
#endif

struct m3_list {
    int curlen;
    u16 mem_addr;
    int max;
};

static int external_amp = 1;
static int gpio_pin = -1;

struct m3_state {
    unsigned int magic;
    struct m3_card *card;
    unsigned char fmt, enable;

    int index;

    /* this locks around the oss state in the driver */
	/* no, this lock is removed - only use card->lock */
	/* otherwise: against what are you protecting on SMP 
		when irqhandler uses s->lock
		and m3_assp_read uses card->lock ?
		*/
    struct mutex open_mutex;
    wait_queue_head_t open_wait;
    mode_t open_mode;

    int dev_audio;

    struct assp_instance {
        u16 code, data;
    } dac_inst, adc_inst;

    /* should be in dmabuf */
    unsigned int rateadc, ratedac;

    struct dmabuf {
        void *rawbuf;
        unsigned buforder;
        unsigned numfrag;
        unsigned fragshift;
        unsigned hwptr, swptr;
        unsigned total_bytes;
        int count;
        unsigned error; /* over/underrun */
        wait_queue_head_t wait;
        /* redundant, but makes calculations easier */
        unsigned fragsize;
        unsigned dmasize;
        unsigned fragsamples;
        /* OSS stuff */
        unsigned mapped:1;
        unsigned ready:1;    
        unsigned endcleared:1;
        unsigned ossfragshift;
        int ossmaxfrags;
        unsigned subdivision;
        /* new in m3 */
        int mixer_index, dma_index, msrc_index, adc1_index;
        int in_lists;
        /* 2.4.. */
        dma_addr_t handle;

    } dma_dac, dma_adc;
};
    
struct m3_card {
    unsigned int magic;

    struct m3_card *next;

    struct ac97_codec *ac97;
    spinlock_t ac97_lock;

    int card_type;

#define NR_DSPS 1
#define MAX_DSPS NR_DSPS
    struct m3_state channels[MAX_DSPS];

    /* this locks around the physical registers on the card */
    spinlock_t lock;

    /* hardware resources */
    struct pci_dev *pcidev;
    u32 iobase;
    u32 irq;

    int dacs_active;

    int timer_users;

    struct m3_list  msrc_list,
                    mixer_list,
                    adc1_list,
                    dma_list;

    /* for storing reset state..*/
    u8 reset_state;

    u16 *suspend_mem;
    int in_suspend;
    wait_queue_head_t suspend_queue;
};

/*
 * an arbitrary volume we set the internal
 * volume settings to so that the ac97 volume
 * range is a little less insane.  0x7fff is 
 * max.
 */
#define ARB_VOLUME ( 0x6800 )

static const unsigned sample_shift[] = { 0, 1, 1, 2 };

enum {
    ESS_ALLEGRO,
    ESS_MAESTRO3,
    /*
     * a maestro3 with 'hardware strapping', only
     * found inside ESS?
     */
    ESS_MAESTRO3HW,
};

static char *card_names[] = {
    [ESS_ALLEGRO] = "Allegro",
    [ESS_MAESTRO3] = "Maestro3(i)",
    [ESS_MAESTRO3HW] = "Maestro3(i)hw"
};

#ifndef PCI_VENDOR_ESS
#define PCI_VENDOR_ESS      0x125D
#endif

#define M3_DEVICE(DEV, TYPE)			\
{						\
.vendor	     = PCI_VENDOR_ESS,			\
.device	     = DEV,				\
.subvendor   = PCI_ANY_ID,			\
.subdevice   = PCI_ANY_ID,			\
.class	     = PCI_CLASS_MULTIMEDIA_AUDIO << 8,	\
.class_mask  = 0xffff << 8,			\
.driver_data = TYPE,				\
}

static struct pci_device_id m3_id_table[] = {
    M3_DEVICE(0x1988, ESS_ALLEGRO),
    M3_DEVICE(0x1998, ESS_MAESTRO3),
    M3_DEVICE(0x199a, ESS_MAESTRO3HW),
    {0,}
};

MODULE_DEVICE_TABLE (pci, m3_id_table);

/*
 * reports seem to indicate that the m3 is limited
 * to 28bit bus addresses.  aaaargggh...
 */
#define M3_PCI_DMA_MASK 0x0fffffff

static unsigned 
ld2(unsigned int x)
{
    unsigned r = 0;
    
    if (x >= 0x10000) {
        x >>= 16;
        r += 16;
    }
    if (x >= 0x100) {
        x >>= 8;
        r += 8;
    }
    if (x >= 0x10) {
        x >>= 4;
        r += 4;
    }
    if (x >= 4) {
        x >>= 2;
        r += 2;
    }
    if (x >= 2)
        r++;
    return r;
}

static struct m3_card *devs;

/*
 * I'm not very good at laying out functions in a file :)
 */
static int m3_notifier(struct notifier_block *nb, unsigned long event, void *buf);
static int m3_suspend(struct pci_dev *pci_dev, pm_message_t state);
static void check_suspend(struct m3_card *card);

static struct notifier_block m3_reboot_nb = {
	.notifier_call = m3_notifier,
};

static void m3_outw(struct m3_card *card,
        u16 value, unsigned long reg)
{
    check_suspend(card);
    outw(value, card->iobase + reg);
}

static u16 m3_inw(struct m3_card *card, unsigned long reg)
{
    check_suspend(card);
    return inw(card->iobase + reg);
}
static void m3_outb(struct m3_card *card, 
        u8 value, unsigned long reg)
{
    check_suspend(card);
    outb(value, card->iobase + reg);
}
static u8 m3_inb(struct m3_card *card, unsigned long reg)
{
    check_suspend(card);
    return inb(card->iobase + reg);
}

/*
 * access 16bit words to the code or data regions of the dsp's memory.
 * index addresses 16bit words.
 */
static u16 __m3_assp_read(struct m3_card *card, u16 region, u16 index)
{
    m3_outw(card, region & MEMTYPE_MASK, DSP_PORT_MEMORY_TYPE);
    m3_outw(card, index, DSP_PORT_MEMORY_INDEX);
    return m3_inw(card, DSP_PORT_MEMORY_DATA);
}
static u16 m3_assp_read(struct m3_card *card, u16 region, u16 index)
{
    unsigned long flags;
    u16 ret;

    spin_lock_irqsave(&(card->lock), flags);
    ret = __m3_assp_read(card, region, index);
    spin_unlock_irqrestore(&(card->lock), flags);

    return ret;
}

static void __m3_assp_write(struct m3_card *card, 
        u16 region, u16 index, u16 data)
{
    m3_outw(card, region & MEMTYPE_MASK, DSP_PORT_MEMORY_TYPE);
    m3_outw(card, index, DSP_PORT_MEMORY_INDEX);
    m3_outw(card, data, DSP_PORT_MEMORY_DATA);
}
static void m3_assp_write(struct m3_card *card, 
        u16 region, u16 index, u16 data)
{
    unsigned long flags;

    spin_lock_irqsave(&(card->lock), flags);
    __m3_assp_write(card, region, index, data);
    spin_unlock_irqrestore(&(card->lock), flags);
}

static void m3_assp_halt(struct m3_card *card)
{
    card->reset_state = m3_inb(card, DSP_PORT_CONTROL_REG_B) & ~REGB_STOP_CLOCK;
    mdelay(10);
    m3_outb(card, card->reset_state & ~REGB_ENABLE_RESET, DSP_PORT_CONTROL_REG_B);
}

static void m3_assp_continue(struct m3_card *card)
{
    m3_outb(card, card->reset_state | REGB_ENABLE_RESET, DSP_PORT_CONTROL_REG_B);
}

/*
 * This makes me sad. the maestro3 has lists
 * internally that must be packed.. 0 terminates,
 * apparently, or maybe all unused entries have
 * to be 0, the lists have static lengths set
 * by the binary code images.
 */

static int m3_add_list(struct m3_card *card,
        struct m3_list *list, u16 val)
{
    DPRINTK(DPSTR, "adding val 0x%x to list 0x%p at pos %d\n",
            val, list, list->curlen);

    m3_assp_write(card, MEMTYPE_INTERNAL_DATA,
            list->mem_addr + list->curlen,
            val);

    return list->curlen++;

}

static void m3_remove_list(struct m3_card *card,
        struct m3_list *list, int index)
{
    u16  val;
    int lastindex = list->curlen - 1;

    DPRINTK(DPSTR, "removing ind %d from list 0x%p\n",
            index, list);

    if(index != lastindex) {
        val = m3_assp_read(card, MEMTYPE_INTERNAL_DATA,
                list->mem_addr + lastindex);
        m3_assp_write(card, MEMTYPE_INTERNAL_DATA,
                list->mem_addr + index,
                val);
    }

    m3_assp_write(card, MEMTYPE_INTERNAL_DATA,
            list->mem_addr + lastindex,
            0);

    list->curlen--;
}

static void set_fmt(struct m3_state *s, unsigned char mask, unsigned char data)
{
    int tmp;

    s->fmt = (s->fmt & mask) | data;

    tmp = (s->fmt >> ESS_DAC_SHIFT) & ESS_FMT_MASK;

    /* write to 'mono' word */
    m3_assp_write(s->card, MEMTYPE_INTERNAL_DATA,
            s->dac_inst.data + SRC3_DIRECTION_OFFSET + 1, 
            (tmp & ESS_FMT_STEREO) ? 0 : 1);
    /* write to '8bit' word */
    m3_assp_write(s->card, MEMTYPE_INTERNAL_DATA,
            s->dac_inst.data + SRC3_DIRECTION_OFFSET + 2, 
            (tmp & ESS_FMT_16BIT) ? 0 : 1);

    tmp = (s->fmt >> ESS_ADC_SHIFT) & ESS_FMT_MASK;

    /* write to 'mono' word */
    m3_assp_write(s->card, MEMTYPE_INTERNAL_DATA,
            s->adc_inst.data + SRC3_DIRECTION_OFFSET + 1, 
            (tmp & ESS_FMT_STEREO) ? 0 : 1);
    /* write to '8bit' word */
    m3_assp_write(s->card, MEMTYPE_INTERNAL_DATA,
            s->adc_inst.data + SRC3_DIRECTION_OFFSET + 2, 
            (tmp & ESS_FMT_16BIT) ? 0 : 1);
}

static void set_dac_rate(struct m3_state *s, unsigned int rate)
{
    u32 freq;

    if (rate > 48000)
        rate = 48000;
    if (rate < 8000)
        rate = 8000;

    s->ratedac = rate;

    freq = ((rate << 15) + 24000 ) / 48000;
    if(freq) 
        freq--;

    m3_assp_write(s->card, MEMTYPE_INTERNAL_DATA,
            s->dac_inst.data + CDATA_FREQUENCY,
            freq);
}

static void set_adc_rate(struct m3_state *s, unsigned int rate)
{
    u32 freq;

    if (rate > 48000)
        rate = 48000;
    if (rate < 8000)
        rate = 8000;

    s->rateadc = rate;

    freq = ((rate << 15) + 24000 ) / 48000;
    if(freq) 
        freq--;

    m3_assp_write(s->card, MEMTYPE_INTERNAL_DATA,
            s->adc_inst.data + CDATA_FREQUENCY,
            freq);
}

static void inc_timer_users(struct m3_card *card)
{
    unsigned long flags;

    spin_lock_irqsave(&card->lock, flags);
    
    card->timer_users++;
    DPRINTK(DPSYS, "inc timer users now %d\n",
            card->timer_users);
    if(card->timer_users != 1) 
        goto out;

    __m3_assp_write(card, MEMTYPE_INTERNAL_DATA,
        KDATA_TIMER_COUNT_RELOAD,
         240 ) ;

    __m3_assp_write(card, MEMTYPE_INTERNAL_DATA,
        KDATA_TIMER_COUNT_CURRENT,
         240 ) ;

    m3_outw(card,  
            m3_inw(card, HOST_INT_CTRL) | CLKRUN_GEN_ENABLE,
            HOST_INT_CTRL);
out:
    spin_unlock_irqrestore(&card->lock, flags);
}

static void dec_timer_users(struct m3_card *card)
{
    unsigned long flags;

    spin_lock_irqsave(&card->lock, flags);

    card->timer_users--;
    DPRINTK(DPSYS, "dec timer users now %d\n",
            card->timer_users);
    if(card->timer_users > 0 ) 
        goto out;

    __m3_assp_write(card, MEMTYPE_INTERNAL_DATA,
        KDATA_TIMER_COUNT_RELOAD,
         0 ) ;

    __m3_assp_write(card, MEMTYPE_INTERNAL_DATA,
        KDATA_TIMER_COUNT_CURRENT,
         0 ) ;

    m3_outw(card,  m3_inw(card, HOST_INT_CTRL) & ~CLKRUN_GEN_ENABLE,
            HOST_INT_CTRL);
out:
    spin_unlock_irqrestore(&card->lock, flags);
}

/*
 * {start,stop}_{adc,dac} should be called
 * while holding the 'state' lock and they
 * will try to grab the 'card' lock..
 */
static void stop_adc(struct m3_state *s)
{
    if (! (s->enable & ADC_RUNNING)) 
        return;

    s->enable &= ~ADC_RUNNING;
    dec_timer_users(s->card);

    m3_assp_write(s->card, MEMTYPE_INTERNAL_DATA,
            s->adc_inst.data + CDATA_INSTANCE_READY, 0);

    m3_assp_write(s->card, MEMTYPE_INTERNAL_DATA,
            KDATA_ADC1_REQUEST, 0);
}    

static void stop_dac(struct m3_state *s)
{
    if (! (s->enable & DAC_RUNNING)) 
        return;

    DPRINTK(DPSYS, "stop_dac()\n");

    m3_assp_write(s->card, MEMTYPE_INTERNAL_DATA,
            s->dac_inst.data + CDATA_INSTANCE_READY, 0);

    s->enable &= ~DAC_RUNNING;
    s->card->dacs_active--;
    dec_timer_users(s->card);

    m3_assp_write(s->card, MEMTYPE_INTERNAL_DATA,
            KDATA_MIXER_TASK_NUMBER, 
            s->card->dacs_active ) ;
}    

static void start_dac(struct m3_state *s)
{
    if( (!s->dma_dac.mapped && s->dma_dac.count < 1) ||
            !s->dma_dac.ready ||
            (s->enable & DAC_RUNNING)) 
        return;

    DPRINTK(DPSYS, "start_dac()\n");

    s->enable |= DAC_RUNNING;
    s->card->dacs_active++;
    inc_timer_users(s->card);

    m3_assp_write(s->card, MEMTYPE_INTERNAL_DATA,
            s->dac_inst.data + CDATA_INSTANCE_READY, 1);

    m3_assp_write(s->card, MEMTYPE_INTERNAL_DATA,
            KDATA_MIXER_TASK_NUMBER, 
            s->card->dacs_active ) ;
}    

static void start_adc(struct m3_state *s)
{
    if ((! s->dma_adc.mapped &&
                s->dma_adc.count >= (signed)(s->dma_adc.dmasize - 2*s->dma_adc.fragsize)) 
        || !s->dma_adc.ready 
        || (s->enable & ADC_RUNNING) ) 
            return;

    DPRINTK(DPSYS, "start_adc()\n");

    s->enable |= ADC_RUNNING;
    inc_timer_users(s->card);

    m3_assp_write(s->card, MEMTYPE_INTERNAL_DATA,
            KDATA_ADC1_REQUEST, 1);

    m3_assp_write(s->card, MEMTYPE_INTERNAL_DATA,
            s->adc_inst.data + CDATA_INSTANCE_READY, 1);
}    

static struct play_vals {
    u16 addr, val;
} pv[] = {
    {CDATA_LEFT_VOLUME, ARB_VOLUME},
    {CDATA_RIGHT_VOLUME, ARB_VOLUME},
    {SRC3_DIRECTION_OFFSET, 0} ,
    /* +1, +2 are stereo/16 bit */
    {SRC3_DIRECTION_OFFSET + 3, 0x0000}, /* fraction? */
    {SRC3_DIRECTION_OFFSET + 4, 0}, /* first l */
    {SRC3_DIRECTION_OFFSET + 5, 0}, /* first r */
    {SRC3_DIRECTION_OFFSET + 6, 0}, /* second l */
    {SRC3_DIRECTION_OFFSET + 7, 0}, /* second r */
    {SRC3_DIRECTION_OFFSET + 8, 0}, /* delta l */
    {SRC3_DIRECTION_OFFSET + 9, 0}, /* delta r */
    {SRC3_DIRECTION_OFFSET + 10, 0x8000}, /* round */
    {SRC3_DIRECTION_OFFSET + 11, 0xFF00}, /* higher bute mark */
    {SRC3_DIRECTION_OFFSET + 13, 0}, /* temp0 */
    {SRC3_DIRECTION_OFFSET + 14, 0}, /* c fraction */
    {SRC3_DIRECTION_OFFSET + 15, 0}, /* counter */
    {SRC3_DIRECTION_OFFSET + 16, 8}, /* numin */
    {SRC3_DIRECTION_OFFSET + 17, 50*2}, /* numout */
    {SRC3_DIRECTION_OFFSET + 18, MINISRC_BIQUAD_STAGE - 1}, /* numstage */
    {SRC3_DIRECTION_OFFSET + 20, 0}, /* filtertap */
    {SRC3_DIRECTION_OFFSET + 21, 0} /* booster */
};


/* the mode passed should be already shifted and masked */
static void m3_play_setup(struct m3_state *s, int mode, u32 rate, void *buffer, int size)
{
    int dsp_in_size = MINISRC_IN_BUFFER_SIZE - (0x20 * 2);
    int dsp_out_size = MINISRC_OUT_BUFFER_SIZE - (0x20 * 2);
    int dsp_in_buffer = s->dac_inst.data + (MINISRC_TMP_BUFFER_SIZE / 2);
    int dsp_out_buffer = dsp_in_buffer + (dsp_in_size / 2) + 1;
    struct dmabuf *db = &s->dma_dac;
    int i;

    DPRINTK(DPSTR, "mode=%d rate=%d buf=%p len=%d.\n",
        mode, rate, buffer, size);

#define LO(x) ((x) & 0xffff)
#define HI(x) LO((x) >> 16)

    /* host dma buffer pointers */

    m3_assp_write(s->card, MEMTYPE_INTERNAL_DATA,
        s->dac_inst.data + CDATA_HOST_SRC_ADDRL,
        LO(virt_to_bus(buffer)));

    m3_assp_write(s->card, MEMTYPE_INTERNAL_DATA,
        s->dac_inst.data + CDATA_HOST_SRC_ADDRH,
        HI(virt_to_bus(buffer)));

    m3_assp_write(s->card, MEMTYPE_INTERNAL_DATA,
        s->dac_inst.data + CDATA_HOST_SRC_END_PLUS_1L,
        LO(virt_to_bus(buffer) + size));

    m3_assp_write(s->card, MEMTYPE_INTERNAL_DATA,
        s->dac_inst.data + CDATA_HOST_SRC_END_PLUS_1H,
        HI(virt_to_bus(buffer) + size));

    m3_assp_write(s->card, MEMTYPE_INTERNAL_DATA,
        s->dac_inst.data + CDATA_HOST_SRC_CURRENTL,
        LO(virt_to_bus(buffer)));

    m3_assp_write(s->card, MEMTYPE_INTERNAL_DATA,
        s->dac_inst.data + CDATA_HOST_SRC_CURRENTH,
        HI(virt_to_bus(buffer)));
#undef LO
#undef HI

    /* dsp buffers */

    m3_assp_write(s->card, MEMTYPE_INTERNAL_DATA,
        s->dac_inst.data + CDATA_IN_BUF_BEGIN,
        dsp_in_buffer);

    m3_assp_write(s->card, MEMTYPE_INTERNAL_DATA,
        s->dac_inst.data + CDATA_IN_BUF_END_PLUS_1,
        dsp_in_buffer + (dsp_in_size / 2));

    m3_assp_write(s->card, MEMTYPE_INTERNAL_DATA,
        s->dac_inst.data + CDATA_IN_BUF_HEAD,
        dsp_in_buffer);
    
    m3_assp_write(s->card, MEMTYPE_INTERNAL_DATA,
        s->dac_inst.data + CDATA_IN_BUF_TAIL,
        dsp_in_buffer);

    m3_assp_write(s->card, MEMTYPE_INTERNAL_DATA,
        s->dac_inst.data + CDATA_OUT_BUF_BEGIN,
        dsp_out_buffer);

    m3_assp_write(s->card, MEMTYPE_INTERNAL_DATA,
        s->dac_inst.data + CDATA_OUT_BUF_END_PLUS_1,
        dsp_out_buffer + (dsp_out_size / 2));

    m3_assp_write(s->card, MEMTYPE_INTERNAL_DATA,
        s->dac_inst.data + CDATA_OUT_BUF_HEAD,
        dsp_out_buffer);

    m3_assp_write(s->card, MEMTYPE_INTERNAL_DATA,
        s->dac_inst.data + CDATA_OUT_BUF_TAIL,
        dsp_out_buffer);

    /*
     * some per client initializers
     */

    m3_assp_write(s->card, MEMTYPE_INTERNAL_DATA,
        s->dac_inst.data + SRC3_DIRECTION_OFFSET + 12,
        s->dac_inst.data + 40 + 8);

    m3_assp_write(s->card, MEMTYPE_INTERNAL_DATA,
        s->dac_inst.data + SRC3_DIRECTION_OFFSET + 19,
        s->dac_inst.code + MINISRC_COEF_LOC);

    /* enable or disable low pass filter? */
    m3_assp_write(s->card, MEMTYPE_INTERNAL_DATA,
        s->dac_inst.data + SRC3_DIRECTION_OFFSET + 22,
        s->ratedac > 45000 ? 0xff : 0 );
    
    /* tell it which way dma is going? */
    m3_assp_write(s->card, MEMTYPE_INTERNAL_DATA,
        s->dac_inst.data + CDATA_DMA_CONTROL,
        DMACONTROL_AUTOREPEAT + DMAC_PAGE3_SELECTOR + DMAC_BLOCKF_SELECTOR);

    /*
     * set an armload of static initializers
     */
    for(i = 0 ; i < (sizeof(pv) / sizeof(pv[0])) ; i++) 
        m3_assp_write(s->card, MEMTYPE_INTERNAL_DATA,
            s->dac_inst.data + pv[i].addr, pv[i].val);

    /* 
     * put us in the lists if we're not already there
     */

    if(db->in_lists == 0) {

        db->msrc_index = m3_add_list(s->card, &s->card->msrc_list, 
                s->dac_inst.data >> DP_SHIFT_COUNT);

        db->dma_index = m3_add_list(s->card, &s->card->dma_list, 
                s->dac_inst.data >> DP_SHIFT_COUNT);

        db->mixer_index = m3_add_list(s->card, &s->card->mixer_list, 
                s->dac_inst.data >> DP_SHIFT_COUNT);

        db->in_lists = 1;
    }

    set_dac_rate(s,rate);
    start_dac(s);
}

/*
 *    Native record driver 
 */
static struct rec_vals {
    u16 addr, val;
} rv[] = {
    {CDATA_LEFT_VOLUME, ARB_VOLUME},
    {CDATA_RIGHT_VOLUME, ARB_VOLUME},
    {SRC3_DIRECTION_OFFSET, 1} ,
    /* +1, +2 are stereo/16 bit */
    {SRC3_DIRECTION_OFFSET + 3, 0x0000}, /* fraction? */
    {SRC3_DIRECTION_OFFSET + 4, 0}, /* first l */
    {SRC3_DIRECTION_OFFSET + 5, 0}, /* first r */
    {SRC3_DIRECTION_OFFSET + 6, 0}, /* second l */
    {SRC3_DIRECTION_OFFSET + 7, 0}, /* second r */
    {SRC3_DIRECTION_OFFSET + 8, 0}, /* delta l */
    {SRC3_DIRECTION_OFFSET + 9, 0}, /* delta r */
    {SRC3_DIRECTION_OFFSET + 10, 0x8000}, /* round */
    {SRC3_DIRECTION_OFFSET + 11, 0xFF00}, /* higher bute mark */
    {SRC3_DIRECTION_OFFSET + 13, 0}, /* temp0 */
    {SRC3_DIRECTION_OFFSET + 14, 0}, /* c fraction */
    {SRC3_DIRECTION_OFFSET + 15, 0}, /* counter */
    {SRC3_DIRECTION_OFFSET + 16, 50},/* numin */
    {SRC3_DIRECTION_OFFSET + 17, 8}, /* numout */
    {SRC3_DIRECTION_OFFSET + 18, 0}, /* numstage */
    {SRC3_DIRECTION_OFFSET + 19, 0}, /* coef */
    {SRC3_DIRECTION_OFFSET + 20, 0}, /* filtertap */
    {SRC3_DIRECTION_OFFSET + 21, 0}, /* booster */
    {SRC3_DIRECTION_OFFSET + 22, 0xff} /* skip lpf */
};

/* again, passed mode is alrady shifted/masked */
static void m3_rec_setup(struct m3_state *s, int mode, u32 rate, void *buffer, int size)
{
    int dsp_in_size = MINISRC_IN_BUFFER_SIZE + (0x10 * 2);
    int dsp_out_size = MINISRC_OUT_BUFFER_SIZE - (0x10 * 2);
    int dsp_in_buffer = s->adc_inst.data + (MINISRC_TMP_BUFFER_SIZE / 2);
    int dsp_out_buffer = dsp_in_buffer + (dsp_in_size / 2) + 1;
    struct dmabuf *db = &s->dma_adc;
    int i;

    DPRINTK(DPSTR, "rec_setup mode=%d rate=%d buf=%p len=%d.\n",
        mode, rate, buffer, size);

#define LO(x) ((x) & 0xffff)
#define HI(x) LO((x) >> 16)

    /* host dma buffer pointers */

    m3_assp_write(s->card, MEMTYPE_INTERNAL_DATA,
        s->adc_inst.data + CDATA_HOST_SRC_ADDRL,
        LO(virt_to_bus(buffer)));

    m3_assp_write(s->card, MEMTYPE_INTERNAL_DATA,
        s->adc_inst.data + CDATA_HOST_SRC_ADDRH,
        HI(virt_to_bus(buffer)));

    m3_assp_write(s->card, MEMTYPE_INTERNAL_DATA,
        s->adc_inst.data + CDATA_HOST_SRC_END_PLUS_1L,
        LO(virt_to_bus(buffer) + size));

    m3_assp_write(s->card, MEMTYPE_INTERNAL_DATA,
        s->adc_inst.data + CDATA_HOST_SRC_END_PLUS_1H,
        HI(virt_to_bus(buffer) + size));

    m3_assp_write(s->card, MEMTYPE_INTERNAL_DATA,
        s->adc_inst.data + CDATA_HOST_SRC_CURRENTL,
        LO(virt_to_bus(buffer)));

    m3_assp_write(s->card, MEMTYPE_INTERNAL_DATA,
        s->adc_inst.data + CDATA_HOST_SRC_CURRENTH,
        HI(virt_to_bus(buffer)));
#undef LO
#undef HI

    /* dsp buffers */

    m3_assp_write(s->card, MEMTYPE_INTERNAL_DATA,
        s->adc_inst.data + CDATA_IN_BUF_BEGIN,
        dsp_in_buffer);

    m3_assp_write(s->card, MEMTYPE_INTERNAL_DATA,
        s->adc_inst.data + CDATA_IN_BUF_END_PLUS_1,
        dsp_in_buffer + (dsp_in_size / 2));

    m3_assp_write(s->card, MEMTYPE_INTERNAL_DATA,
        s->adc_inst.data + CDATA_IN_BUF_HEAD,
        dsp_in_buffer);
    
    m3_assp_write(s->card, MEMTYPE_INTERNAL_DATA,
        s->adc_inst.data + CDATA_IN_BUF_TAIL,
        dsp_in_buffer);

    m3_assp_write(s->card, MEMTYPE_INTERNAL_DATA,
        s->adc_inst.data + CDATA_OUT_BUF_BEGIN,
        dsp_out_buffer);

    m3_assp_write(s->card, MEMTYPE_INTERNAL_DATA,
        s->adc_inst.data + CDATA_OUT_BUF_END_PLUS_1,
        dsp_out_buffer + (dsp_out_size / 2));

    m3_assp_write(s->card, MEMTYPE_INTERNAL_DATA,
        s->adc_inst.data + CDATA_OUT_BUF_HEAD,
        dsp_out_buffer);

    m3_assp_write(s->card, MEMTYPE_INTERNAL_DATA,
        s->adc_inst.data + CDATA_OUT_BUF_TAIL,
        dsp_out_buffer);

    /*
     * some per client initializers
     */

    m3_assp_write(s->card, MEMTYPE_INTERNAL_DATA,
        s->adc_inst.data + SRC3_DIRECTION_OFFSET + 12,
        s->adc_inst.data + 40 + 8);

    /* tell it which way dma is going? */
    m3_assp_write(s->card, MEMTYPE_INTERNAL_DATA,
        s->adc_inst.data + CDATA_DMA_CONTROL,
        DMACONTROL_DIRECTION + DMACONTROL_AUTOREPEAT + 
        DMAC_PAGE3_SELECTOR + DMAC_BLOCKF_SELECTOR);

    /*
     * set an armload of static initializers
     */
    for(i = 0 ; i < (sizeof(rv) / sizeof(rv[0])) ; i++) 
        m3_assp_write(s->card, MEMTYPE_INTERNAL_DATA,
            s->adc_inst.data + rv[i].addr, rv[i].val);

    /* 
     * put us in the lists if we're not already there
     */

    if(db->in_lists == 0) {

        db->adc1_index = m3_add_list(s->card, &s->card->adc1_list, 
                s->adc_inst.data >> DP_SHIFT_COUNT);

        db->dma_index = m3_add_list(s->card, &s->card->dma_list, 
                s->adc_inst.data >> DP_SHIFT_COUNT);

        db->msrc_index = m3_add_list(s->card, &s->card->msrc_list, 
                s->adc_inst.data >> DP_SHIFT_COUNT);

        db->in_lists = 1;
    }

    set_adc_rate(s,rate);
    start_adc(s);
}
/* --------------------------------------------------------------------- */

static void set_dmaa(struct m3_state *s, unsigned int addr, unsigned int count)
{
    DPRINTK(DPINT,"set_dmaa??\n");
}

static void set_dmac(struct m3_state *s, unsigned int addr, unsigned int count)
{
    DPRINTK(DPINT,"set_dmac??\n");
}

static u32 get_dma_pos(struct m3_card *card,
		       int instance_addr)
{
    u16 hi = 0, lo = 0;
    int retry = 10;

    /*
     * try and get a valid answer
     */
    while(retry--) {
        hi =  m3_assp_read(card, MEMTYPE_INTERNAL_DATA,
                instance_addr + CDATA_HOST_SRC_CURRENTH);

        lo = m3_assp_read(card, MEMTYPE_INTERNAL_DATA,
                instance_addr + CDATA_HOST_SRC_CURRENTL);

        if(hi == m3_assp_read(card, MEMTYPE_INTERNAL_DATA,
                instance_addr + CDATA_HOST_SRC_CURRENTH))
            break;
    }
    return lo | (hi<<16);
}

static u32 get_dmaa(struct m3_state *s)
{
    u32 offset;

    offset = get_dma_pos(s->card, s->dac_inst.data) - 
        virt_to_bus(s->dma_dac.rawbuf);

    DPRINTK(DPINT,"get_dmaa: 0x%08x\n",offset);

    return offset;
}

static u32 get_dmac(struct m3_state *s)
{
    u32 offset;

    offset = get_dma_pos(s->card, s->adc_inst.data) -
        virt_to_bus(s->dma_adc.rawbuf);

    DPRINTK(DPINT,"get_dmac: 0x%08x\n",offset);

    return offset;

}

static int 
prog_dmabuf(struct m3_state *s, unsigned rec)
{
    struct dmabuf *db = rec ? &s->dma_adc : &s->dma_dac;
    unsigned rate = rec ? s->rateadc : s->ratedac;
    unsigned bytepersec;
    unsigned bufs;
    unsigned char fmt;
    unsigned long flags;

    spin_lock_irqsave(&s->card->lock, flags);

    fmt = s->fmt;
    if (rec) {
        stop_adc(s);
        fmt >>= ESS_ADC_SHIFT;
    } else {
        stop_dac(s);
        fmt >>= ESS_DAC_SHIFT;
    }
    fmt &= ESS_FMT_MASK;

    db->hwptr = db->swptr = db->total_bytes = db->count = db->error = db->endcleared = 0;

    bytepersec = rate << sample_shift[fmt];
    bufs = PAGE_SIZE << db->buforder;
    if (db->ossfragshift) {
        if ((1000 << db->ossfragshift) < bytepersec)
            db->fragshift = ld2(bytepersec/1000);
        else
            db->fragshift = db->ossfragshift;
    } else {
        db->fragshift = ld2(bytepersec/100/(db->subdivision ? db->subdivision : 1));
        if (db->fragshift < 3)
            db->fragshift = 3; 
    }
    db->numfrag = bufs >> db->fragshift;
    while (db->numfrag < 4 && db->fragshift > 3) {
        db->fragshift--;
        db->numfrag = bufs >> db->fragshift;
    }
    db->fragsize = 1 << db->fragshift;
    if (db->ossmaxfrags >= 4 && db->ossmaxfrags < db->numfrag)
        db->numfrag = db->ossmaxfrags;
    db->fragsamples = db->fragsize >> sample_shift[fmt];
    db->dmasize = db->numfrag << db->fragshift;

    DPRINTK(DPSTR,"prog_dmabuf: numfrag: %d fragsize: %d dmasize: %d\n",db->numfrag,db->fragsize,db->dmasize);

    memset(db->rawbuf, (fmt & ESS_FMT_16BIT) ? 0 : 0x80, db->dmasize);

    if (rec) 
        m3_rec_setup(s, fmt, s->rateadc, db->rawbuf, db->dmasize);
    else 
        m3_play_setup(s, fmt, s->ratedac, db->rawbuf, db->dmasize);

    db->ready = 1;

    spin_unlock_irqrestore(&s->card->lock, flags);

    return 0;
}

static void clear_advance(struct m3_state *s)
{
    unsigned char c = ((s->fmt >> ESS_DAC_SHIFT) & ESS_FMT_16BIT) ? 0 : 0x80;
    
    unsigned char *buf = s->dma_dac.rawbuf;
    unsigned bsize = s->dma_dac.dmasize;
    unsigned bptr = s->dma_dac.swptr;
    unsigned len = s->dma_dac.fragsize;
    
    if (bptr + len > bsize) {
        unsigned x = bsize - bptr;
        memset(buf + bptr, c, x);
        /* account for wrapping? */
        bptr = 0;
        len -= x;
    }
    memset(buf + bptr, c, len);
}

/* call with spinlock held! */
static void m3_update_ptr(struct m3_state *s)
{
    unsigned hwptr;
    int diff;

    /* update ADC pointer */
    if (s->dma_adc.ready) {
        hwptr = get_dmac(s) % s->dma_adc.dmasize;
        diff = (s->dma_adc.dmasize + hwptr - s->dma_adc.hwptr) % s->dma_adc.dmasize;
        s->dma_adc.hwptr = hwptr;
        s->dma_adc.total_bytes += diff;
        s->dma_adc.count += diff;
        if (s->dma_adc.count >= (signed)s->dma_adc.fragsize) 
            wake_up(&s->dma_adc.wait);
        if (!s->dma_adc.mapped) {
            if (s->dma_adc.count > (signed)(s->dma_adc.dmasize - ((3 * s->dma_adc.fragsize) >> 1))) {
                stop_adc(s); 
                /* brute force everyone back in sync, sigh */
                s->dma_adc.count = 0;
                s->dma_adc.swptr = 0;
                s->dma_adc.hwptr = 0;
                s->dma_adc.error++;
            }
        }
    }
    /* update DAC pointer */
    if (s->dma_dac.ready) {
        hwptr = get_dmaa(s) % s->dma_dac.dmasize; 
        diff = (s->dma_dac.dmasize + hwptr - s->dma_dac.hwptr) % s->dma_dac.dmasize;

        DPRINTK(DPINT,"updating dac: hwptr: %6d diff: %6d count: %6d\n",
                hwptr,diff,s->dma_dac.count);

        s->dma_dac.hwptr = hwptr;
        s->dma_dac.total_bytes += diff;

        if (s->dma_dac.mapped) {
            
            s->dma_dac.count += diff;
            if (s->dma_dac.count >= (signed)s->dma_dac.fragsize) {
                wake_up(&s->dma_dac.wait);
            }
        } else {

            s->dma_dac.count -= diff;
            
            if (s->dma_dac.count <= 0) {
                DPRINTK(DPCRAP,"underflow! diff: %d (0x%x) count: %d (0x%x) hw: %d (0x%x) sw: %d (0x%x)\n", 
                        diff, diff, 
                        s->dma_dac.count, 
                        s->dma_dac.count, 
                    hwptr, hwptr,
                    s->dma_dac.swptr,
                    s->dma_dac.swptr);
                stop_dac(s);
                /* brute force everyone back in sync, sigh */
                s->dma_dac.count = 0; 
                s->dma_dac.swptr = hwptr; 
                s->dma_dac.error++;
            } else if (s->dma_dac.count <= (signed)s->dma_dac.fragsize && !s->dma_dac.endcleared) {
                clear_advance(s);
                s->dma_dac.endcleared = 1;
            }
            if (s->dma_dac.count + (signed)s->dma_dac.fragsize <= (signed)s->dma_dac.dmasize) {
                wake_up(&s->dma_dac.wait);
                DPRINTK(DPINT,"waking up DAC count: %d sw: %d hw: %d\n",
                        s->dma_dac.count, s->dma_dac.swptr, hwptr);
            }
        }
    }
}

static irqreturn_t m3_interrupt(int irq, void *dev_id, struct pt_regs *regs)
{
    struct m3_card *c = (struct m3_card *)dev_id;
    struct m3_state *s = &c->channels[0];
    u8 status;

    status = inb(c->iobase+0x1A);

    if(status == 0xff)
	return IRQ_NONE;
   
    /* presumably acking the ints? */
    outw(status, c->iobase+0x1A); 

    if(c->in_suspend)
        return IRQ_HANDLED;

    /*
     * ack an assp int if its running
     * and has an int pending
     */
    if( status & ASSP_INT_PENDING) {
        u8 ctl = inb(c->iobase + ASSP_CONTROL_B);
        if( !(ctl & STOP_ASSP_CLOCK)) {
            ctl = inb(c->iobase + ASSP_HOST_INT_STATUS );
            if(ctl & DSP2HOST_REQ_TIMER) {
                outb( DSP2HOST_REQ_TIMER, c->iobase + ASSP_HOST_INT_STATUS);
                /* update adc/dac info if it was a timer int */
                spin_lock(&c->lock);
                m3_update_ptr(s);
                spin_unlock(&c->lock);
            }
        }
    }

    /* XXX is this needed? */
    if(status & 0x40) 
        outb(0x40, c->iobase+0x1A);
    return IRQ_HANDLED;
}


/* --------------------------------------------------------------------- */

static const char invalid_magic[] = KERN_CRIT PFX "invalid magic value in %s\n";

#define VALIDATE_MAGIC(FOO,MAG)                         \
({                                                \
    if (!(FOO) || (FOO)->magic != MAG) { \
        printk(invalid_magic,__FUNCTION__);            \
        return -ENXIO;                    \
    }                                         \
})

#define VALIDATE_STATE(a) VALIDATE_MAGIC(a,M3_STATE_MAGIC)
#define VALIDATE_CARD(a) VALIDATE_MAGIC(a,M3_CARD_MAGIC)

/* --------------------------------------------------------------------- */

static int drain_dac(struct m3_state *s, int nonblock)
{
    DECLARE_WAITQUEUE(wait,current);
    unsigned long flags;
    int count;
    signed long tmo;

    if (s->dma_dac.mapped || !s->dma_dac.ready)
        return 0;
    set_current_state(TASK_INTERRUPTIBLE);
    add_wait_queue(&s->dma_dac.wait, &wait);
    for (;;) {
        spin_lock_irqsave(&s->card->lock, flags);
        count = s->dma_dac.count;
        spin_unlock_irqrestore(&s->card->lock, flags);
        if (count <= 0)
            break;
        if (signal_pending(current))
            break;
        if (nonblock) {
            remove_wait_queue(&s->dma_dac.wait, &wait);
            set_current_state(TASK_RUNNING);
            return -EBUSY;
        }
        tmo = (count * HZ) / s->ratedac;
        tmo >>= sample_shift[(s->fmt >> ESS_DAC_SHIFT) & ESS_FMT_MASK];
        /* XXX this is just broken.  someone is waking us up alot, or schedule_timeout is broken.
            or something.  who cares. - zach */
        if (!schedule_timeout(tmo ? tmo : 1) && tmo)
            DPRINTK(DPCRAP,"dma timed out?? %ld\n",jiffies);
    }
    remove_wait_queue(&s->dma_dac.wait, &wait);
    set_current_state(TASK_RUNNING);
    if (signal_pending(current))
            return -ERESTARTSYS;
    return 0;
}

static ssize_t m3_read(struct file *file, char __user *buffer, size_t count, loff_t *ppos)
{
    struct m3_state *s = (struct m3_state *)file->private_data;
    ssize_t ret;
    unsigned long flags;
    unsigned swptr;
    int cnt;
    
    VALIDATE_STATE(s);
    if (s->dma_adc.mapped)
        return -ENXIO;
    if (!s->dma_adc.ready && (ret = prog_dmabuf(s, 1)))
        return ret;
    if (!access_ok(VERIFY_WRITE, buffer, count))
        return -EFAULT;
    ret = 0;

    spin_lock_irqsave(&s->card->lock, flags);

    while (count > 0) {
        int timed_out;

        swptr = s->dma_adc.swptr;
        cnt = s->dma_adc.dmasize-swptr;
        if (s->dma_adc.count < cnt)
            cnt = s->dma_adc.count;

        if (cnt > count)
            cnt = count;

        if (cnt <= 0) {
            start_adc(s);
            if (file->f_flags & O_NONBLOCK) 
            {
                ret = ret ? ret : -EAGAIN;
                goto out;
            }

            spin_unlock_irqrestore(&s->card->lock, flags);
            timed_out = interruptible_sleep_on_timeout(&s->dma_adc.wait, HZ) == 0;
            spin_lock_irqsave(&s->card->lock, flags);

            if(timed_out) {
                printk("read: chip lockup? dmasz %u fragsz %u count %u hwptr %u swptr %u\n",
                       s->dma_adc.dmasize, s->dma_adc.fragsize, s->dma_adc.count, 
                       s->dma_adc.hwptr, s->dma_adc.swptr);
                stop_adc(s);
                set_dmac(s, virt_to_bus(s->dma_adc.rawbuf), s->dma_adc.numfrag << s->dma_adc.fragshift);
                s->dma_adc.count = s->dma_adc.hwptr = s->dma_adc.swptr = 0;
            }
            if (signal_pending(current)) 
            {
                ret = ret ? ret : -ERESTARTSYS;
                goto out;
            }
            continue;
        }
    
        spin_unlock_irqrestore(&s->card->lock, flags);
        if (copy_to_user(buffer, s->dma_adc.rawbuf + swptr, cnt)) {
            ret = ret ? ret : -EFAULT;
            return ret;
        }
        spin_lock_irqsave(&s->card->lock, flags);

        swptr = (swptr + cnt) % s->dma_adc.dmasize;
        s->dma_adc.swptr = swptr;
        s->dma_adc.count -= cnt;
        count -= cnt;
        buffer += cnt;
        ret += cnt;
        start_adc(s);
    }

out:
    spin_unlock_irqrestore(&s->card->lock, flags);
    return ret;
}

static ssize_t m3_write(struct file *file, const char __user *buffer, size_t count, loff_t *ppos)
{
    struct m3_state *s = (struct m3_state *)file->private_data;
    ssize_t ret;
    unsigned long flags;
    unsigned swptr;
    int cnt;
    
    VALIDATE_STATE(s);
    if (s->dma_dac.mapped)
        return -ENXIO;
    if (!s->dma_dac.ready && (ret = prog_dmabuf(s, 0)))
        return ret;
    if (!access_ok(VERIFY_READ, buffer, count))
        return -EFAULT;
    ret = 0;

    spin_lock_irqsave(&s->card->lock, flags);

    while (count > 0) {
        int timed_out;

        if (s->dma_dac.count < 0) {
            s->dma_dac.count = 0;
            s->dma_dac.swptr = s->dma_dac.hwptr;
        }
        swptr = s->dma_dac.swptr;

        cnt = s->dma_dac.dmasize-swptr;

        if (s->dma_dac.count + cnt > s->dma_dac.dmasize)
            cnt = s->dma_dac.dmasize - s->dma_dac.count;


        if (cnt > count)
            cnt = count;

        if (cnt <= 0) {
            start_dac(s);
            if (file->f_flags & O_NONBLOCK) {
                if(!ret) ret = -EAGAIN;
                goto out;
            }
            spin_unlock_irqrestore(&s->card->lock, flags);
            timed_out = interruptible_sleep_on_timeout(&s->dma_dac.wait, HZ) == 0;
            spin_lock_irqsave(&s->card->lock, flags);
            if(timed_out) {
                DPRINTK(DPCRAP,"write: chip lockup? dmasz %u fragsz %u count %u hwptr %u swptr %u\n",
                       s->dma_dac.dmasize, s->dma_dac.fragsize, s->dma_dac.count, 
                       s->dma_dac.hwptr, s->dma_dac.swptr);
                stop_dac(s);
                set_dmaa(s, virt_to_bus(s->dma_dac.rawbuf), s->dma_dac.numfrag << s->dma_dac.fragshift);
                s->dma_dac.count = s->dma_dac.hwptr = s->dma_dac.swptr = 0;
            }
            if (signal_pending(current)) {
                if (!ret) ret = -ERESTARTSYS;
                goto out;
            }
            continue;
        }
        spin_unlock_irqrestore(&s->card->lock, flags);
        if (copy_from_user(s->dma_dac.rawbuf + swptr, buffer, cnt)) {
            if (!ret) ret = -EFAULT;
            return ret;
        }
        spin_lock_irqsave(&s->card->lock, flags);

        DPRINTK(DPSYS,"wrote %6d bytes at sw: %6d cnt: %6d while hw: %6d\n",
                cnt, swptr, s->dma_dac.count, s->dma_dac.hwptr);
        
        swptr = (swptr + cnt) % s->dma_dac.dmasize;

        s->dma_dac.swptr = swptr;
        s->dma_dac.count += cnt;
        s->dma_dac.endcleared = 0;
        count -= cnt;
        buffer += cnt;
        ret += cnt;
        start_dac(s);
    }
out:
    spin_unlock_irqrestore(&s->card->lock, flags);
    return ret;
}

static unsigned int m3_poll(struct file *file, struct poll_table_struct *wait)
{
    struct m3_state *s = (struct m3_state *)file->private_data;
    unsigned long flags;
    unsigned int mask = 0;

    VALIDATE_STATE(s);
    if (file->f_mode & FMODE_WRITE)
        poll_wait(file, &s->dma_dac.wait, wait);
    if (file->f_mode & FMODE_READ)
        poll_wait(file, &s->dma_adc.wait, wait);

    spin_lock_irqsave(&s->card->lock, flags);
    m3_update_ptr(s);

    if (file->f_mode & FMODE_READ) {
        if (s->dma_adc.count >= (signed)s->dma_adc.fragsize)
            mask |= POLLIN | POLLRDNORM;
    }
    if (file->f_mode & FMODE_WRITE) {
        if (s->dma_dac.mapped) {
            if (s->dma_dac.count >= (signed)s->dma_dac.fragsize) 
                mask |= POLLOUT | POLLWRNORM;
        } else {
            if ((signed)s->dma_dac.dmasize >= s->dma_dac.count + (signed)s->dma_dac.fragsize)
                mask |= POLLOUT | POLLWRNORM;
        }
    }

    spin_unlock_irqrestore(&s->card->lock, flags);
    return mask;
}

static int m3_mmap(struct file *file, struct vm_area_struct *vma)
{
    struct m3_state *s = (struct m3_state *)file->private_data;
    unsigned long max_size, size, start, offset;
    struct dmabuf *db;
    int ret = -EINVAL;

    VALIDATE_STATE(s);
    if (vma->vm_flags & VM_WRITE) {
        if ((ret = prog_dmabuf(s, 0)) != 0)
            return ret;
        db = &s->dma_dac;
    } else 
    if (vma->vm_flags & VM_READ) {
        if ((ret = prog_dmabuf(s, 1)) != 0)
            return ret;
        db = &s->dma_adc;
    } else  
        return -EINVAL;

    max_size = db->dmasize;

    start = vma->vm_start;
    offset = (vma->vm_pgoff << PAGE_SHIFT);
    size = vma->vm_end - vma->vm_start;

    if(size > max_size)
        goto out;
    if(offset > max_size - size)
        goto out;

    /*
     * this will be ->nopage() once I can 
     * ask Jeff what the hell I'm doing wrong.
     */
    ret = -EAGAIN;
    if (remap_pfn_range(vma, vma->vm_start,
			virt_to_phys(db->rawbuf) >> PAGE_SHIFT,
			size, vma->vm_page_prot))
        goto out;

    db->mapped = 1;
    ret = 0;

out:
    return ret;
}

/*
 * this function is a disaster..
 */
#define get_user_ret(x, ptr,  ret) ({ if(get_user(x, ptr)) return ret; })
static int m3_ioctl(struct inode *inode, struct file *file, unsigned int cmd, unsigned long arg)
{
    struct m3_state *s = (struct m3_state *)file->private_data;
	struct m3_card *card=s->card;
    unsigned long flags;
    audio_buf_info abinfo;
    count_info cinfo;
    int val, mapped, ret;
    unsigned char fmtm, fmtd;
    void __user *argp = (void __user *)arg;
    int __user *p = argp;

    VALIDATE_STATE(s);

    mapped = ((file->f_mode & FMODE_WRITE) && s->dma_dac.mapped) ||
        ((file->f_mode & FMODE_READ) && s->dma_adc.mapped);

    DPRINTK(DPSYS,"m3_ioctl: cmd %d\n", cmd);

    switch (cmd) {
    case OSS_GETVERSION:
        return put_user(SOUND_VERSION, p);

    case SNDCTL_DSP_SYNC:
        if (file->f_mode & FMODE_WRITE)
            return drain_dac(s, file->f_flags & O_NONBLOCK);
        return 0;
        
    case SNDCTL_DSP_SETDUPLEX:
        /* XXX fix */
        return 0;

    case SNDCTL_DSP_GETCAPS:
        return put_user(DSP_CAP_DUPLEX | DSP_CAP_REALTIME | DSP_CAP_TRIGGER | DSP_CAP_MMAP, p);
        
    case SNDCTL_DSP_RESET:
        spin_lock_irqsave(&card->lock, flags);
        if (file->f_mode & FMODE_WRITE) {
            stop_dac(s);
            synchronize_irq(s->card->pcidev->irq);
            s->dma_dac.swptr = s->dma_dac.hwptr = s->dma_dac.count = s->dma_dac.total_bytes = 0;
        }
        if (file->f_mode & FMODE_READ) {
            stop_adc(s);
            synchronize_irq(s->card->pcidev->irq);
            s->dma_adc.swptr = s->dma_adc.hwptr = s->dma_adc.count = s->dma_adc.total_bytes = 0;
        }
        spin_unlock_irqrestore(&card->lock, flags);
        return 0;

    case SNDCTL_DSP_SPEED:
        get_user_ret(val, p, -EFAULT);
        spin_lock_irqsave(&card->lock, flags);
        if (val >= 0) {
            if (file->f_mode & FMODE_READ) {
                stop_adc(s);
                s->dma_adc.ready = 0;
                set_adc_rate(s, val);
            }
            if (file->f_mode & FMODE_WRITE) {
                stop_dac(s);
                s->dma_dac.ready = 0;
                set_dac_rate(s, val);
            }
        }
        spin_unlock_irqrestore(&card->lock, flags);
        return put_user((file->f_mode & FMODE_READ) ? s->rateadc : s->ratedac, p);
        
    case SNDCTL_DSP_STEREO:
        get_user_ret(val, p, -EFAULT);
        spin_lock_irqsave(&card->lock, flags);
        fmtd = 0;
        fmtm = ~0;
        if (file->f_mode & FMODE_READ) {
            stop_adc(s);
            s->dma_adc.ready = 0;
            if (val)
                fmtd |= ESS_FMT_STEREO << ESS_ADC_SHIFT;
            else
                fmtm &= ~(ESS_FMT_STEREO << ESS_ADC_SHIFT);
        }
        if (file->f_mode & FMODE_WRITE) {
            stop_dac(s);
            s->dma_dac.ready = 0;
            if (val)
                fmtd |= ESS_FMT_STEREO << ESS_DAC_SHIFT;
            else
                fmtm &= ~(ESS_FMT_STEREO << ESS_DAC_SHIFT);
        }
        set_fmt(s, fmtm, fmtd);
        spin_unlock_irqrestore(&card->lock, flags);
        return 0;

    case SNDCTL_DSP_CHANNELS:
        get_user_ret(val, p, -EFAULT);
        spin_lock_irqsave(&card->lock, flags);
        if (val != 0) {
            fmtd = 0;
            fmtm = ~0;
            if (file->f_mode & FMODE_READ) {
                stop_adc(s);
                s->dma_adc.ready = 0;
                if (val >= 2)
                    fmtd |= ESS_FMT_STEREO << ESS_ADC_SHIFT;
                else
                    fmtm &= ~(ESS_FMT_STEREO << ESS_ADC_SHIFT);
            }
            if (file->f_mode & FMODE_WRITE) {
                stop_dac(s);
                s->dma_dac.ready = 0;
                if (val >= 2)
                    fmtd |= ESS_FMT_STEREO << ESS_DAC_SHIFT;
                else
                    fmtm &= ~(ESS_FMT_STEREO << ESS_DAC_SHIFT);
            }
            set_fmt(s, fmtm, fmtd);
        }
        spin_unlock_irqrestore(&card->lock, flags);
        return put_user((s->fmt & ((file->f_mode & FMODE_READ) ? (ESS_FMT_STEREO << ESS_ADC_SHIFT) 
                       : (ESS_FMT_STEREO << ESS_DAC_SHIFT))) ? 2 : 1, p);
        
    case SNDCTL_DSP_GETFMTS: /* Returns a mask */
        return put_user(AFMT_U8|AFMT_S16_LE, p);
        
    case SNDCTL_DSP_SETFMT: /* Selects ONE fmt*/
        get_user_ret(val, p, -EFAULT);
        spin_lock_irqsave(&card->lock, flags);
        if (val != AFMT_QUERY) {
            fmtd = 0;
            fmtm = ~0;
            if (file->f_mode & FMODE_READ) {
                stop_adc(s);
                s->dma_adc.ready = 0;
                if (val == AFMT_S16_LE)
                    fmtd |= ESS_FMT_16BIT << ESS_ADC_SHIFT;
                else
                    fmtm &= ~(ESS_FMT_16BIT << ESS_ADC_SHIFT);
            }
            if (file->f_mode & FMODE_WRITE) {
                stop_dac(s);
                s->dma_dac.ready = 0;
                if (val == AFMT_S16_LE)
                    fmtd |= ESS_FMT_16BIT << ESS_DAC_SHIFT;
                else
                    fmtm &= ~(ESS_FMT_16BIT << ESS_DAC_SHIFT);
            }
            set_fmt(s, fmtm, fmtd);
        }
        spin_unlock_irqrestore(&card->lock, flags);
        return put_user((s->fmt & ((file->f_mode & FMODE_READ) ? 
            (ESS_FMT_16BIT << ESS_ADC_SHIFT) 
            : (ESS_FMT_16BIT << ESS_DAC_SHIFT))) ? 
                AFMT_S16_LE : 
                AFMT_U8, 
            p);
        
    case SNDCTL_DSP_POST:
        return 0;

    case SNDCTL_DSP_GETTRIGGER:
        val = 0;
        if ((file->f_mode & FMODE_READ) && (s->enable & ADC_RUNNING))
            val |= PCM_ENABLE_INPUT;
        if ((file->f_mode & FMODE_WRITE) && (s->enable & DAC_RUNNING)) 
            val |= PCM_ENABLE_OUTPUT;
        return put_user(val, p);
        
    case SNDCTL_DSP_SETTRIGGER:
        get_user_ret(val, p, -EFAULT);
        if (file->f_mode & FMODE_READ) {
            if (val & PCM_ENABLE_INPUT) {
                if (!s->dma_adc.ready && (ret =  prog_dmabuf(s, 1)))
                    return ret;
                start_adc(s);
            } else
                stop_adc(s);
        }
        if (file->f_mode & FMODE_WRITE) {
            if (val & PCM_ENABLE_OUTPUT) {
                if (!s->dma_dac.ready && (ret = prog_dmabuf(s, 0)))
                    return ret;
                start_dac(s);
            } else
                stop_dac(s);
        }
        return 0;

    case SNDCTL_DSP_GETOSPACE:
        if (!(file->f_mode & FMODE_WRITE))
            return -EINVAL;
        if (!(s->enable & DAC_RUNNING) && (val = prog_dmabuf(s, 0)) != 0)
            return val;
        spin_lock_irqsave(&card->lock, flags);
        m3_update_ptr(s);
        abinfo.fragsize = s->dma_dac.fragsize;
        abinfo.bytes = s->dma_dac.dmasize - s->dma_dac.count;
        abinfo.fragstotal = s->dma_dac.numfrag;
        abinfo.fragments = abinfo.bytes >> s->dma_dac.fragshift;      
        spin_unlock_irqrestore(&card->lock, flags);
        return copy_to_user(argp, &abinfo, sizeof(abinfo)) ? -EFAULT : 0;

    case SNDCTL_DSP_GETISPACE:
        if (!(file->f_mode & FMODE_READ))
            return -EINVAL;
        if (!(s->enable & ADC_RUNNING) && (val = prog_dmabuf(s, 1)) != 0)
            return val;
        spin_lock_irqsave(&card->lock, flags);
        m3_update_ptr(s);
        abinfo.fragsize = s->dma_adc.fragsize;
        abinfo.bytes = s->dma_adc.count;
        abinfo.fragstotal = s->dma_adc.numfrag;
        abinfo.fragments = abinfo.bytes >> s->dma_adc.fragshift;      
        spin_unlock_irqrestore(&card->lock, flags);
        return copy_to_user(argp, &abinfo, sizeof(abinfo)) ? -EFAULT : 0;
        
    case SNDCTL_DSP_NONBLOCK:
        file->f_flags |= O_NONBLOCK;
        return 0;

    case SNDCTL_DSP_GETODELAY:
        if (!(file->f_mode & FMODE_WRITE))
            return -EINVAL;
        spin_lock_irqsave(&card->lock, flags);
        m3_update_ptr(s);
        val = s->dma_dac.count;
        spin_unlock_irqrestore(&card->lock, flags);
        return put_user(val, p);

    case SNDCTL_DSP_GETIPTR:
        if (!(file->f_mode & FMODE_READ))
            return -EINVAL;
        spin_lock_irqsave(&card->lock, flags);
        m3_update_ptr(s);
        cinfo.bytes = s->dma_adc.total_bytes;
        cinfo.blocks = s->dma_adc.count >> s->dma_adc.fragshift;
        cinfo.ptr = s->dma_adc.hwptr;
        if (s->dma_adc.mapped)
            s->dma_adc.count &= s->dma_adc.fragsize-1;
        spin_unlock_irqrestore(&card->lock, flags);
	if (copy_to_user(argp, &cinfo, sizeof(cinfo)))
		return -EFAULT;
	return 0;

    case SNDCTL_DSP_GETOPTR:
        if (!(file->f_mode & FMODE_WRITE))
            return -EINVAL;
        spin_lock_irqsave(&card->lock, flags);
        m3_update_ptr(s);
        cinfo.bytes = s->dma_dac.total_bytes;
        cinfo.blocks = s->dma_dac.count >> s->dma_dac.fragshift;
        cinfo.ptr = s->dma_dac.hwptr;
        if (s->dma_dac.mapped)
            s->dma_dac.count &= s->dma_dac.fragsize-1;
        spin_unlock_irqrestore(&card->lock, flags);
	if (copy_to_user(argp, &cinfo, sizeof(cinfo)))
		return -EFAULT;
	return 0;

    case SNDCTL_DSP_GETBLKSIZE:
        if (file->f_mode & FMODE_WRITE) {
            if ((val = prog_dmabuf(s, 0)))
                return val;
            return put_user(s->dma_dac.fragsize, p);
        }
        if ((val = prog_dmabuf(s, 1)))
            return val;
        return put_user(s->dma_adc.fragsize, p);

    case SNDCTL_DSP_SETFRAGMENT:
        get_user_ret(val, p, -EFAULT);
        spin_lock_irqsave(&card->lock, flags);
        if (file->f_mode & FMODE_READ) {
            s->dma_adc.ossfragshift = val & 0xffff;
            s->dma_adc.ossmaxfrags = (val >> 16) & 0xffff;
            if (s->dma_adc.ossfragshift < 4)
                s->dma_adc.ossfragshift = 4;
            if (s->dma_adc.ossfragshift > 15)
                s->dma_adc.ossfragshift = 15;
            if (s->dma_adc.ossmaxfrags < 4)
                s->dma_adc.ossmaxfrags = 4;
        }
        if (file->f_mode & FMODE_WRITE) {
            s->dma_dac.ossfragshift = val & 0xffff;
            s->dma_dac.ossmaxfrags = (val >> 16) & 0xffff;
            if (s->dma_dac.ossfragshift < 4)
                s->dma_dac.ossfragshift = 4;
            if (s->dma_dac.ossfragshift > 15)
                s->dma_dac.ossfragshift = 15;
            if (s->dma_dac.ossmaxfrags < 4)
                s->dma_dac.ossmaxfrags = 4;
        }
        spin_unlock_irqrestore(&card->lock, flags);
        return 0;

    case SNDCTL_DSP_SUBDIVIDE:
        if ((file->f_mode & FMODE_READ && s->dma_adc.subdivision) ||
            (file->f_mode & FMODE_WRITE && s->dma_dac.subdivision))
            return -EINVAL;
                get_user_ret(val, p, -EFAULT);
        if (val != 1 && val != 2 && val != 4)
            return -EINVAL;
        if (file->f_mode & FMODE_READ)
            s->dma_adc.subdivision = val;
        if (file->f_mode & FMODE_WRITE)
            s->dma_dac.subdivision = val;
        return 0;

    case SOUND_PCM_READ_RATE:
        return put_user((file->f_mode & FMODE_READ) ? s->rateadc : s->ratedac, p);

    case SOUND_PCM_READ_CHANNELS:
        return put_user((s->fmt & ((file->f_mode & FMODE_READ) ? (ESS_FMT_STEREO << ESS_ADC_SHIFT) 
                       : (ESS_FMT_STEREO << ESS_DAC_SHIFT))) ? 2 : 1, p);

    case SOUND_PCM_READ_BITS:
        return put_user((s->fmt & ((file->f_mode & FMODE_READ) ? (ESS_FMT_16BIT << ESS_ADC_SHIFT) 
                       : (ESS_FMT_16BIT << ESS_DAC_SHIFT))) ? 16 : 8, p);

    case SOUND_PCM_WRITE_FILTER:
    case SNDCTL_DSP_SETSYNCRO:
    case SOUND_PCM_READ_FILTER:
        return -EINVAL;
        
    }
    return -EINVAL;
}

static int
allocate_dmabuf(struct pci_dev *pci_dev, struct dmabuf *db)
{
    int order;

    DPRINTK(DPSTR,"allocating for dmabuf %p\n", db);

    /* 
     * alloc as big a chunk as we can, start with 
     * 64k 'cause we're insane.  based on order cause
     * the amazingly complicated prog_dmabuf wants it.
     *
     * pci_alloc_sonsistent guarantees that it won't cross a natural
     * boundary; the m3 hardware can't have dma cross a 64k bus
     * address boundary.
     */
    for (order = 16-PAGE_SHIFT; order >= 1; order--) {
        db->rawbuf = pci_alloc_consistent(pci_dev, PAGE_SIZE << order,
                        &(db->handle));
        if(db->rawbuf)
            break;
    }

    if (!db->rawbuf)
        return 1;

    DPRINTK(DPSTR,"allocated %ld (%d) bytes at %p\n",
            PAGE_SIZE<<order, order, db->rawbuf);

    {
        struct page *page, *pend;

        pend = virt_to_page(db->rawbuf + (PAGE_SIZE << order) - 1);
        for (page = virt_to_page(db->rawbuf); page <= pend; page++)
            SetPageReserved(page);
    }


    db->buforder = order;
    db->ready = 0;
    db->mapped = 0;

    return 0;
}

static void
nuke_lists(struct m3_card *card, struct dmabuf *db)
{
    m3_remove_list(card, &(card->dma_list), db->dma_index);
    m3_remove_list(card, &(card->msrc_list), db->msrc_index);
    db->in_lists = 0;
}

static void
free_dmabuf(struct pci_dev *pci_dev, struct dmabuf *db)
{
    if(db->rawbuf == NULL)
        return;

    DPRINTK(DPSTR,"freeing %p from dmabuf %p\n",db->rawbuf, db);

    {
        struct page *page, *pend;
        pend = virt_to_page(db->rawbuf + (PAGE_SIZE << db->buforder) - 1);
        for (page = virt_to_page(db->rawbuf); page <= pend; page++)
            ClearPageReserved(page);
    }


    pci_free_consistent(pci_dev, PAGE_SIZE << db->buforder,
            db->rawbuf, db->handle);

    db->rawbuf = NULL;
    db->buforder = 0;
    db->mapped = 0;
    db->ready = 0;
}

static int m3_open(struct inode *inode, struct file *file)
{
    unsigned int minor = iminor(inode);
    struct m3_card *c;
    struct m3_state *s = NULL;
    int i;
    unsigned char fmtm = ~0, fmts = 0;
    unsigned long flags;

    /*
     *    Scan the cards and find the channel. We only
     *    do this at open time so it is ok
     */
    for(c = devs ; c != NULL ; c = c->next) {

        for(i=0;i<NR_DSPS;i++) {

            if(c->channels[i].dev_audio < 0)
                continue;
            if((c->channels[i].dev_audio ^ minor) & ~0xf)
                continue;

            s = &c->channels[i];
            break;
        }
    }
        
    if (!s)
        return -ENODEV;
        
    VALIDATE_STATE(s);

    file->private_data = s;

    /* wait for device to become free */
    mutex_lock(&s->open_mutex);
    while (s->open_mode & file->f_mode) {
        if (file->f_flags & O_NONBLOCK) {
            mutex_unlock(&s->open_mutex);
            return -EWOULDBLOCK;
        }
        mutex_unlock(&s->open_mutex);
        interruptible_sleep_on(&s->open_wait);
        if (signal_pending(current))
            return -ERESTARTSYS;
        mutex_lock(&s->open_mutex);
    }
    
    spin_lock_irqsave(&c->lock, flags);

    if (file->f_mode & FMODE_READ) {
        fmtm &= ~((ESS_FMT_STEREO | ESS_FMT_16BIT) << ESS_ADC_SHIFT);
        if ((minor & 0xf) == SND_DEV_DSP16)
            fmts |= ESS_FMT_16BIT << ESS_ADC_SHIFT; 

        s->dma_adc.ossfragshift = s->dma_adc.ossmaxfrags = s->dma_adc.subdivision = 0;
        set_adc_rate(s, 8000);
    }
    if (file->f_mode & FMODE_WRITE) {
        fmtm &= ~((ESS_FMT_STEREO | ESS_FMT_16BIT) << ESS_DAC_SHIFT);
        if ((minor & 0xf) == SND_DEV_DSP16)
            fmts |= ESS_FMT_16BIT << ESS_DAC_SHIFT;

        s->dma_dac.ossfragshift = s->dma_dac.ossmaxfrags = s->dma_dac.subdivision = 0;
        set_dac_rate(s, 8000);
    }
    set_fmt(s, fmtm, fmts);
    s->open_mode |= file->f_mode & (FMODE_READ | FMODE_WRITE);

    mutex_unlock(&s->open_mutex);
    spin_unlock_irqrestore(&c->lock, flags);
    return nonseekable_open(inode, file);
}

static int m3_release(struct inode *inode, struct file *file)
{
    struct m3_state *s = (struct m3_state *)file->private_data;
	struct m3_card *card=s->card;
    unsigned long flags;

    VALIDATE_STATE(s);
    if (file->f_mode & FMODE_WRITE)
        drain_dac(s, file->f_flags & O_NONBLOCK);

    mutex_lock(&s->open_mutex);
    spin_lock_irqsave(&card->lock, flags);

    if (file->f_mode & FMODE_WRITE) {
        stop_dac(s);
        if(s->dma_dac.in_lists) {
            m3_remove_list(s->card, &(s->card->mixer_list), s->dma_dac.mixer_index);
            nuke_lists(s->card, &(s->dma_dac));
        }
    }
    if (file->f_mode & FMODE_READ) {
        stop_adc(s);
        if(s->dma_adc.in_lists) {
            m3_remove_list(s->card, &(s->card->adc1_list), s->dma_adc.adc1_index);
            nuke_lists(s->card, &(s->dma_adc));
        }
    }
        
    s->open_mode &= (~file->f_mode) & (FMODE_READ|FMODE_WRITE);

    spin_unlock_irqrestore(&card->lock, flags);
    mutex_unlock(&s->open_mutex);
    wake_up(&s->open_wait);

    return 0;
}

/*
 * Wait for the ac97 serial bus to be free.
 * return nonzero if the bus is still busy.
 */
static int m3_ac97_wait(struct m3_card *card)
{
    int i = 10000;

    while( (m3_inb(card, 0x30) & 1) && i--) ;

    return i == 0;
}

static u16 m3_ac97_read(struct ac97_codec *codec, u8 reg)
{
    u16 ret = 0;
    struct m3_card *card = codec->private_data;

    spin_lock(&card->ac97_lock);

    if(m3_ac97_wait(card)) {
        printk(KERN_ERR PFX "serial bus busy reading reg 0x%x\n",reg);
        goto out;
    }

    m3_outb(card, 0x80 | (reg & 0x7f), 0x30);

    if(m3_ac97_wait(card)) {
        printk(KERN_ERR PFX "serial bus busy finishing read reg 0x%x\n",reg);
        goto out;
    }

    ret =  m3_inw(card, 0x32);
    DPRINTK(DPCRAP,"reading 0x%04x from 0x%02x\n",ret, reg);

out:
    spin_unlock(&card->ac97_lock);
    return ret;
}

static void m3_ac97_write(struct ac97_codec *codec, u8 reg, u16 val)
{
    struct m3_card *card = codec->private_data;

    spin_lock(&card->ac97_lock);

    if(m3_ac97_wait(card)) {
        printk(KERN_ERR PFX "serial bus busy writing 0x%x to 0x%x\n",val, reg);
        goto out;
    }
    DPRINTK(DPCRAP,"writing 0x%04x  to  0x%02x\n", val, reg);

    m3_outw(card, val, 0x32);
    m3_outb(card, reg & 0x7f, 0x30);
out:
    spin_unlock(&card->ac97_lock);
}
/* OSS /dev/mixer file operation methods */
static int m3_open_mixdev(struct inode *inode, struct file *file)
{
    unsigned int minor = iminor(inode);
    struct m3_card *card = devs;

    for (card = devs; card != NULL; card = card->next) {
        if((card->ac97 != NULL) && (card->ac97->dev_mixer == minor))
                break;
    }

    if (!card) {
        return -ENODEV;
    }

    file->private_data = card->ac97;

    return nonseekable_open(inode, file);
}

static int m3_release_mixdev(struct inode *inode, struct file *file)
{
    return 0;
}

static int m3_ioctl_mixdev(struct inode *inode, struct file *file, unsigned int cmd,
                                    unsigned long arg)
{
    struct ac97_codec *codec = (struct ac97_codec *)file->private_data;

    return codec->mixer_ioctl(codec, cmd, arg);
}

static struct file_operations m3_mixer_fops = {
	.owner	 = THIS_MODULE,
	.llseek  = no_llseek,
	.ioctl	 = m3_ioctl_mixdev,
	.open	 = m3_open_mixdev,
	.release = m3_release_mixdev,
};

static void remote_codec_config(int io, int isremote)
{
    isremote = isremote ? 1 : 0;

    outw(  (inw(io + RING_BUS_CTRL_B) & ~SECOND_CODEC_ID_MASK) | isremote,
            io + RING_BUS_CTRL_B);
    outw(  (inw(io + SDO_OUT_DEST_CTRL) & ~COMMAND_ADDR_OUT) | isremote,
            io + SDO_OUT_DEST_CTRL);
    outw(  (inw(io + SDO_IN_DEST_CTRL) & ~STATUS_ADDR_IN) | isremote,
            io + SDO_IN_DEST_CTRL);
}

/* 
 * hack, returns non zero on err 
 */
static int try_read_vendor(struct m3_card *card)
{
    u16 ret;

    if(m3_ac97_wait(card)) 
        return 1;

    m3_outb(card, 0x80 | (AC97_VENDOR_ID1 & 0x7f), 0x30);

    if(m3_ac97_wait(card)) 
        return 1;

    ret =  m3_inw(card, 0x32);

    return (ret == 0) || (ret == 0xffff);
}

static void m3_codec_reset(struct m3_card *card, int busywait)
{
    u16 dir;
    int delay1 = 0, delay2 = 0, i;
    int io = card->iobase;

    switch (card->card_type) {
        /*
         * the onboard codec on the allegro seems 
         * to want to wait a very long time before
         * coming back to life 
         */
        case ESS_ALLEGRO:
            delay1 = 50;
            delay2 = 800;
        break;
        case ESS_MAESTRO3:
        case ESS_MAESTRO3HW:
            delay1 = 20;
            delay2 = 500;
        break;
    }

    for(i = 0; i < 5; i ++) {
        dir = inw(io + GPIO_DIRECTION);
        dir |= 0x10; /* assuming pci bus master? */

        remote_codec_config(io, 0);

        outw(IO_SRAM_ENABLE, io + RING_BUS_CTRL_A);
        udelay(20);

        outw(dir & ~GPO_PRIMARY_AC97 , io + GPIO_DIRECTION);
        outw(~GPO_PRIMARY_AC97 , io + GPIO_MASK);
        outw(0, io + GPIO_DATA);
        outw(dir | GPO_PRIMARY_AC97, io + GPIO_DIRECTION);

        if(busywait)  {
            mdelay(delay1);
        } else {
            set_current_state(TASK_UNINTERRUPTIBLE);
            schedule_timeout((delay1 * HZ) / 1000);
        }

        outw(GPO_PRIMARY_AC97, io + GPIO_DATA);
        udelay(5);
        /* ok, bring back the ac-link */
        outw(IO_SRAM_ENABLE | SERIAL_AC_LINK_ENABLE, io + RING_BUS_CTRL_A);
        outw(~0, io + GPIO_MASK);

        if(busywait) {
            mdelay(delay2);
        } else {
            set_current_state(TASK_UNINTERRUPTIBLE);
            schedule_timeout((delay2 * HZ) / 1000);
        }
        if(! try_read_vendor(card))
            break;

        delay1 += 10;
        delay2 += 100;

        DPRINTK(DPMOD, "retrying codec reset with delays of %d and %d ms\n",
                delay1, delay2);
    }

#if 0
    /* more gung-ho reset that doesn't
     * seem to work anywhere :)
     */
    tmp = inw(io + RING_BUS_CTRL_A);
    outw(RAC_SDFS_ENABLE|LAC_SDFS_ENABLE, io + RING_BUS_CTRL_A);
    mdelay(20);
    outw(tmp, io + RING_BUS_CTRL_A);
    mdelay(50);
#endif
}

static int __devinit m3_codec_install(struct m3_card *card)
{
    struct ac97_codec *codec;

    if ((codec = ac97_alloc_codec()) == NULL)
        return -ENOMEM;

    codec->private_data = card;
    codec->codec_read = m3_ac97_read;
    codec->codec_write = m3_ac97_write;
    /* someday we should support secondary codecs.. */
    codec->id = 0;

    if (ac97_probe_codec(codec) == 0) {
        printk(KERN_ERR PFX "codec probe failed\n");
        ac97_release_codec(codec);
        return -1;
    }

    if ((codec->dev_mixer = register_sound_mixer(&m3_mixer_fops, -1)) < 0) {
        printk(KERN_ERR PFX "couldn't register mixer!\n");
        ac97_release_codec(codec);
        return -1;
    }

    card->ac97 = codec;

    return 0;
}


#define MINISRC_LPF_LEN 10
static u16 minisrc_lpf[MINISRC_LPF_LEN] = {
    0X0743, 0X1104, 0X0A4C, 0XF88D, 0X242C,
    0X1023, 0X1AA9, 0X0B60, 0XEFDD, 0X186F
};
static void m3_assp_init(struct m3_card *card)
{
    int i;

    /* zero kernel data */
    for(i = 0 ; i < (REV_B_DATA_MEMORY_UNIT_LENGTH * NUM_UNITS_KERNEL_DATA) / 2; i++)
        m3_assp_write(card, MEMTYPE_INTERNAL_DATA, 
                KDATA_BASE_ADDR + i, 0);

    /* zero mixer data? */
    for(i = 0 ; i < (REV_B_DATA_MEMORY_UNIT_LENGTH * NUM_UNITS_KERNEL_DATA) / 2; i++)
        m3_assp_write(card, MEMTYPE_INTERNAL_DATA, 
                KDATA_BASE_ADDR2 + i, 0);

    /* init dma pointer */
    m3_assp_write(card, MEMTYPE_INTERNAL_DATA, 
            KDATA_CURRENT_DMA, 
            KDATA_DMA_XFER0);

    /* write kernel into code memory.. */
    for(i = 0 ; i < sizeof(assp_kernel_image) / 2; i++) {
        m3_assp_write(card, MEMTYPE_INTERNAL_CODE, 
                REV_B_CODE_MEMORY_BEGIN + i, 
                assp_kernel_image[i]);
    }

    /*
     * We only have this one client and we know that 0x400
     * is free in our kernel's mem map, so lets just
     * drop it there.  It seems that the minisrc doesn't
     * need vectors, so we won't bother with them..
     */
    for(i = 0 ; i < sizeof(assp_minisrc_image) / 2; i++) {
        m3_assp_write(card, MEMTYPE_INTERNAL_CODE, 
                0x400 + i, 
                assp_minisrc_image[i]);
    }

    /*
     * write the coefficients for the low pass filter?
     */
    for(i = 0; i < MINISRC_LPF_LEN ; i++) {
        m3_assp_write(card, MEMTYPE_INTERNAL_CODE,
            0x400 + MINISRC_COEF_LOC + i,
            minisrc_lpf[i]);
    }

    m3_assp_write(card, MEMTYPE_INTERNAL_CODE,
        0x400 + MINISRC_COEF_LOC + MINISRC_LPF_LEN,
        0x8000);

    /*
     * the minisrc is the only thing on
     * our task list..
     */
    m3_assp_write(card, MEMTYPE_INTERNAL_DATA, 
            KDATA_TASK0, 
            0x400);

    /*
     * init the mixer number..
     */

    m3_assp_write(card, MEMTYPE_INTERNAL_DATA,
            KDATA_MIXER_TASK_NUMBER,0);

    /*
     * EXTREME KERNEL MASTER VOLUME
     */
    m3_assp_write(card, MEMTYPE_INTERNAL_DATA,
        KDATA_DAC_LEFT_VOLUME, ARB_VOLUME);
    m3_assp_write(card, MEMTYPE_INTERNAL_DATA,
        KDATA_DAC_RIGHT_VOLUME, ARB_VOLUME);

    card->mixer_list.mem_addr = KDATA_MIXER_XFER0;
    card->mixer_list.max = MAX_VIRTUAL_MIXER_CHANNELS;
    card->adc1_list.mem_addr = KDATA_ADC1_XFER0;
    card->adc1_list.max = MAX_VIRTUAL_ADC1_CHANNELS;
    card->dma_list.mem_addr = KDATA_DMA_XFER0;
    card->dma_list.max = MAX_VIRTUAL_DMA_CHANNELS;
    card->msrc_list.mem_addr = KDATA_INSTANCE0_MINISRC;
    card->msrc_list.max = MAX_INSTANCE_MINISRC;
}

static int setup_msrc(struct m3_card *card,
        struct assp_instance *inst, int index)
{
    int data_bytes = 2 * ( MINISRC_TMP_BUFFER_SIZE / 2 + 
            MINISRC_IN_BUFFER_SIZE / 2 +
            1 + MINISRC_OUT_BUFFER_SIZE / 2 + 1 );
    int address, i;

    /*
     * the revb memory map has 0x1100 through 0x1c00
     * free.  
     */

    /*
     * align instance address to 256 bytes so that it's
     * shifted list address is aligned.  
     * list address = (mem address >> 1) >> 7;
     */
    data_bytes = (data_bytes + 255) & ~255;
    address = 0x1100 + ((data_bytes/2) * index);

    if((address + (data_bytes/2)) >= 0x1c00) {
        printk(KERN_ERR PFX "no memory for %d bytes at ind %d (addr 0x%x)\n",
                data_bytes, index, address);
        return -1;
    }

    for(i = 0; i < data_bytes/2 ; i++) 
        m3_assp_write(card, MEMTYPE_INTERNAL_DATA,
                address + i, 0);

    inst->code = 0x400;
    inst->data = address;

    return 0;
}

static int m3_assp_client_init(struct m3_state *s)
{
    setup_msrc(s->card, &(s->dac_inst), s->index * 2);
    setup_msrc(s->card, &(s->adc_inst), (s->index * 2) + 1);

    return 0;
}

static void m3_amp_enable(struct m3_card *card, int enable)
{
    /* 
     * this works for the reference board, have to find
     * out about others
     *
     * this needs more magic for 4 speaker, but..
     */
    int io = card->iobase;
    u16 gpo, polarity_port, polarity;

    if(!external_amp)
        return;

    if (gpio_pin >= 0  && gpio_pin <= 15) {
        polarity_port = 0x1000 + (0x100 * gpio_pin);
    } else {
        switch (card->card_type) {
            case ESS_ALLEGRO:
                polarity_port = 0x1800;
                break;
            default:
                polarity_port = 0x1100;
                /* Panasonic toughbook CF72 has to be different... */
                if(card->pcidev->subsystem_vendor == 0x10F7 && card->pcidev->subsystem_device == 0x833D)
                	polarity_port = 0x1D00;
                break;
        }
    }

    gpo = (polarity_port >> 8) & 0x0F;
    polarity = polarity_port >> 12;
    if ( enable )
        polarity = !polarity;
    polarity = polarity << gpo;
    gpo = 1 << gpo;

    outw(~gpo , io + GPIO_MASK);

    outw( inw(io + GPIO_DIRECTION) | gpo ,
            io + GPIO_DIRECTION);

    outw( (GPO_SECONDARY_AC97 | GPO_PRIMARY_AC97 | polarity) ,
            io + GPIO_DATA);

    outw(0xffff , io + GPIO_MASK);
}

static int
maestro_config(struct m3_card *card) 
{
    struct pci_dev *pcidev = card->pcidev;
    u32 n;
    u8  t; /* makes as much sense as 'n', no? */

    pci_read_config_dword(pcidev, PCI_ALLEGRO_CONFIG, &n);
    n &= REDUCED_DEBOUNCE;
    n |= PM_CTRL_ENABLE | CLK_DIV_BY_49 | USE_PCI_TIMING;
    pci_write_config_dword(pcidev, PCI_ALLEGRO_CONFIG, n);

    outb(RESET_ASSP, card->iobase + ASSP_CONTROL_B);
    pci_read_config_dword(pcidev, PCI_ALLEGRO_CONFIG, &n);
    n &= ~INT_CLK_SELECT;
    if(card->card_type >= ESS_MAESTRO3)  {
        n &= ~INT_CLK_MULT_ENABLE; 
        n |= INT_CLK_SRC_NOT_PCI;
    }
    n &=  ~( CLK_MULT_MODE_SELECT | CLK_MULT_MODE_SELECT_2 );
    pci_write_config_dword(pcidev, PCI_ALLEGRO_CONFIG, n);

    if(card->card_type <= ESS_ALLEGRO) {
        pci_read_config_dword(pcidev, PCI_USER_CONFIG, &n);
        n |= IN_CLK_12MHZ_SELECT;
        pci_write_config_dword(pcidev, PCI_USER_CONFIG, n);
    }

    t = inb(card->iobase + ASSP_CONTROL_A);
    t &= ~( DSP_CLK_36MHZ_SELECT  | ASSP_CLK_49MHZ_SELECT);
    t |= ASSP_CLK_49MHZ_SELECT;
    t |= ASSP_0_WS_ENABLE; 
    outb(t, card->iobase + ASSP_CONTROL_A);

    outb(RUN_ASSP, card->iobase + ASSP_CONTROL_B); 

    return 0;
} 

static void m3_enable_ints(struct m3_card *card)
{
    unsigned long io = card->iobase;

    outw(ASSP_INT_ENABLE, io + HOST_INT_CTRL);
    outb(inb(io + ASSP_CONTROL_C) | ASSP_HOST_INT_ENABLE,
            io + ASSP_CONTROL_C);
}

static struct file_operations m3_audio_fops = {
	.owner	 = THIS_MODULE,
	.llseek	 = no_llseek,
	.read	 = m3_read,
	.write	 = m3_write,
	.poll	 = m3_poll,
	.ioctl	 = m3_ioctl,
	.mmap	 = m3_mmap,
	.open	 = m3_open,
	.release = m3_release,
};

#ifdef CONFIG_PM
static int alloc_dsp_suspendmem(struct m3_card *card)
{
    int len = sizeof(u16) * (REV_B_CODE_MEMORY_LENGTH + REV_B_DATA_MEMORY_LENGTH);

    if( (card->suspend_mem = vmalloc(len)) == NULL)
        return 1;

    return 0;
}

#else
#define alloc_dsp_suspendmem(args...) 0
#endif

/*
 * great day!  this function is ugly as hell.
 */
static int __devinit m3_probe(struct pci_dev *pci_dev, const struct pci_device_id *pci_id)
{
    u32 n;
    int i;
    struct m3_card *card = NULL;
    int ret = 0;
    int card_type = pci_id->driver_data;

    DPRINTK(DPMOD, "in maestro_install\n");

    if (pci_enable_device(pci_dev))
        return -EIO;

    if (pci_set_dma_mask(pci_dev, M3_PCI_DMA_MASK)) {
        printk(KERN_ERR PFX "architecture does not support limiting to 28bit PCI bus addresses\n");
        return -ENODEV;
    }
        
    pci_set_master(pci_dev);

    if( (card = kmalloc(sizeof(struct m3_card), GFP_KERNEL)) == NULL) {
        printk(KERN_WARNING PFX "out of memory\n");
        return -ENOMEM;
    }
    memset(card, 0, sizeof(struct m3_card));
    card->pcidev = pci_dev;
    init_waitqueue_head(&card->suspend_queue);

    if ( ! request_region(pci_resource_start(pci_dev, 0),
                pci_resource_len (pci_dev, 0), M3_MODULE_NAME)) {

        printk(KERN_WARNING PFX "unable to reserve I/O space.\n");
        ret = -EBUSY;
        goto out;
    }

    card->iobase = pci_resource_start(pci_dev, 0);

    if(alloc_dsp_suspendmem(card)) {
        printk(KERN_WARNING PFX "couldn't alloc %d bytes for saving dsp state on suspend\n",
                REV_B_CODE_MEMORY_LENGTH + REV_B_DATA_MEMORY_LENGTH);
        ret = -ENOMEM;
        goto out;
    }

    card->card_type = card_type;
    card->irq = pci_dev->irq;
    card->next = devs;
    card->magic = M3_CARD_MAGIC;
    spin_lock_init(&card->lock);
    spin_lock_init(&card->ac97_lock);
    devs = card;
    for(i = 0; i<NR_DSPS; i++) {
        struct m3_state *s = &(card->channels[i]);
        s->dev_audio = -1;
    }

    printk(KERN_INFO PFX "Configuring ESS %s found at IO 0x%04X IRQ %d\n", 
        card_names[card->card_type], card->iobase, card->irq);

    pci_read_config_dword(pci_dev, PCI_SUBSYSTEM_VENDOR_ID, &n);
    printk(KERN_INFO PFX " subvendor id: 0x%08x\n",n); 

    maestro_config(card);
    m3_assp_halt(card);

    m3_codec_reset(card, 0);

    if(m3_codec_install(card))  {
        ret = -EIO; 
        goto out;
    }

    m3_assp_init(card);
    m3_amp_enable(card, 1);
    
    for(i=0;i<NR_DSPS;i++) {
        struct m3_state *s=&card->channels[i];

        s->index = i;

        s->card = card;
        init_waitqueue_head(&s->dma_adc.wait);
        init_waitqueue_head(&s->dma_dac.wait);
        init_waitqueue_head(&s->open_wait);
        mutex_init(&(s->open_mutex));
        s->magic = M3_STATE_MAGIC;

        m3_assp_client_init(s);
        
        if(s->dma_adc.ready || s->dma_dac.ready || s->dma_adc.rawbuf)
            printk(KERN_WARNING PFX "initing a dsp device that is already in use?\n");
        /* register devices */
        if ((s->dev_audio = register_sound_dsp(&m3_audio_fops, -1)) < 0) {
            break;
        }

        if( allocate_dmabuf(card->pcidev, &(s->dma_adc)) ||
                allocate_dmabuf(card->pcidev, &(s->dma_dac)))  { 
            ret = -ENOMEM;
            goto out;
        }
    }
    
    if(request_irq(card->irq, m3_interrupt, SA_SHIRQ, card_names[card->card_type], card)) {

        printk(KERN_ERR PFX "unable to allocate irq %d,\n", card->irq);

        ret = -EIO;
        goto out;
    }

    pci_set_drvdata(pci_dev, card);
    
    m3_enable_ints(card);
    m3_assp_continue(card);

out:
    if(ret) {
        if(card->iobase)
            release_region(pci_resource_start(pci_dev, 0), pci_resource_len(pci_dev, 0));
        vfree(card->suspend_mem);
        if(card->ac97) {
            unregister_sound_mixer(card->ac97->dev_mixer);
            kfree(card->ac97);
        }
        for(i=0;i<NR_DSPS;i++)
        {
            struct m3_state *s = &card->channels[i];
            if(s->dev_audio != -1)
                unregister_sound_dsp(s->dev_audio);
        }
        kfree(card);
    }

    return ret; 
}

static void m3_remove(struct pci_dev *pci_dev)
{
    struct m3_card *card;

    unregister_reboot_notifier(&m3_reboot_nb);

    while ((card = devs)) {
        int i;
        devs = devs->next;
    
        free_irq(card->irq, card);
        unregister_sound_mixer(card->ac97->dev_mixer);
        kfree(card->ac97);

        for(i=0;i<NR_DSPS;i++)
        {
            struct m3_state *s = &card->channels[i];
            if(s->dev_audio < 0)
                continue;

            unregister_sound_dsp(s->dev_audio);
            free_dmabuf(card->pcidev, &s->dma_adc);
            free_dmabuf(card->pcidev, &s->dma_dac);
        }

        release_region(card->iobase, 256);
        vfree(card->suspend_mem);
        kfree(card);
    }
    devs = NULL;
}

/*
 * some bioses like the sound chip to be powered down
 * at shutdown.  We're just calling _suspend to
 * achieve that..
 */
static int m3_notifier(struct notifier_block *nb, unsigned long event, void *buf)
{
    struct m3_card *card;

    DPRINTK(DPMOD, "notifier suspending all cards\n");

    for(card = devs; card != NULL; card = card->next) {
        if(!card->in_suspend)
            m3_suspend(card->pcidev, PMSG_SUSPEND); /* XXX legal? */
    }
    return 0;
}

static int m3_suspend(struct pci_dev *pci_dev, pm_message_t state)
{
    unsigned long flags;
    int i;
    struct m3_card *card = pci_get_drvdata(pci_dev);

    /* must be a better way.. */
	spin_lock_irqsave(&card->lock, flags);

    DPRINTK(DPMOD, "pm in dev %p\n",card);

    for(i=0;i<NR_DSPS;i++) {
        struct m3_state *s = &card->channels[i];

        if(s->dev_audio == -1)
            continue;

        DPRINTK(DPMOD, "stop_adc/dac() device %d\n",i);
        stop_dac(s);
        stop_adc(s);
    }

    mdelay(10); /* give the assp a chance to idle.. */

    m3_assp_halt(card);

    if(card->suspend_mem) {
        int index = 0;

        DPRINTK(DPMOD, "saving code\n");
        for(i = REV_B_CODE_MEMORY_BEGIN ; i <= REV_B_CODE_MEMORY_END; i++)
            card->suspend_mem[index++] = 
                m3_assp_read(card, MEMTYPE_INTERNAL_CODE, i);
        DPRINTK(DPMOD, "saving data\n");
        for(i = REV_B_DATA_MEMORY_BEGIN ; i <= REV_B_DATA_MEMORY_END; i++)
            card->suspend_mem[index++] = 
                m3_assp_read(card, MEMTYPE_INTERNAL_DATA, i);
    }

    DPRINTK(DPMOD, "powering down apci regs\n");
    m3_outw(card, 0xffff, 0x54);
    m3_outw(card, 0xffff, 0x56);

    card->in_suspend = 1;

    spin_unlock_irqrestore(&card->lock, flags);

    return 0;
}

static int m3_resume(struct pci_dev *pci_dev)
{
    unsigned long flags;
    int index;
    int i;
    struct m3_card *card = pci_get_drvdata(pci_dev);

	spin_lock_irqsave(&card->lock, flags);
    card->in_suspend = 0;

    DPRINTK(DPMOD, "resuming\n");

    /* first lets just bring everything back. .*/

    DPRINTK(DPMOD, "bringing power back on card 0x%p\n",card);
    m3_outw(card, 0, 0x54);
    m3_outw(card, 0, 0x56);

    DPRINTK(DPMOD, "restoring pci configs and reseting codec\n");
    maestro_config(card);
    m3_assp_halt(card);
    m3_codec_reset(card, 1);

    DPRINTK(DPMOD, "restoring dsp code card\n");
    index = 0;
    for(i = REV_B_CODE_MEMORY_BEGIN ; i <= REV_B_CODE_MEMORY_END; i++)
        m3_assp_write(card, MEMTYPE_INTERNAL_CODE, i, 
            card->suspend_mem[index++]);
    for(i = REV_B_DATA_MEMORY_BEGIN ; i <= REV_B_DATA_MEMORY_END; i++)
        m3_assp_write(card, MEMTYPE_INTERNAL_DATA, i, 
            card->suspend_mem[index++]);

     /* tell the dma engine to restart itself */
    m3_assp_write(card, MEMTYPE_INTERNAL_DATA, 
        KDATA_DMA_ACTIVE, 0);

    DPRINTK(DPMOD, "resuming dsp\n");
    m3_assp_continue(card);

    DPRINTK(DPMOD, "enabling ints\n");
    m3_enable_ints(card);

    /* bring back the old school flavor */
    for(i = 0; i < SOUND_MIXER_NRDEVICES ; i++) {
        int state = card->ac97->mixer_state[i];
        if (!supported_mixer(card->ac97, i)) 
                continue;

        card->ac97->write_mixer(card->ac97, i, 
                state & 0xff, (state >> 8) & 0xff);
    }

    m3_amp_enable(card, 1);

    /* 
     * now we flip on the music 
     */
    for(i=0;i<NR_DSPS;i++) {
        struct m3_state *s = &card->channels[i];
        if(s->dev_audio == -1)
            continue;
        /*
         * db->ready makes it so these guys can be
         * called unconditionally..
         */
        DPRINTK(DPMOD, "turning on dacs ind %d\n",i);
        start_dac(s);    
        start_adc(s);    
    }

    spin_unlock_irqrestore(&card->lock, flags);

    /* 
     * all right, we think things are ready, 
     * wake up people who were using the device 
     * when we suspended
     */
    wake_up(&card->suspend_queue);

    return 0;
}

MODULE_AUTHOR("Zach Brown <zab@zabbo.net>");
MODULE_DESCRIPTION("ESS Maestro3/Allegro Driver");
MODULE_LICENSE("GPL");

#ifdef M_DEBUG
module_param(debug, int, 0);
#endif
module_param(external_amp, int, 0);
module_param(gpio_pin, int, 0);

static struct pci_driver m3_pci_driver = {
	.name	  = "ess_m3_audio",
	.id_table = m3_id_table,
	.probe	  = m3_probe,
	.remove	  = m3_remove,
	.suspend  = m3_suspend,
	.resume	  = m3_resume,
};

static int __init m3_init_module(void)
{
    printk(KERN_INFO PFX "version " DRIVER_VERSION " built at " __TIME__ " " __DATE__ "\n");

    if (register_reboot_notifier(&m3_reboot_nb)) {
        printk(KERN_WARNING PFX "reboot notifier registration failed\n");
        return -ENODEV; /* ? */
    }

    if (pci_register_driver(&m3_pci_driver)) {
        unregister_reboot_notifier(&m3_reboot_nb);
        return -ENODEV;
    }
    return 0;
}

static void __exit m3_cleanup_module(void)
{
    pci_unregister_driver(&m3_pci_driver);
}

module_init(m3_init_module);
module_exit(m3_cleanup_module);

void check_suspend(struct m3_card *card)
{
    DECLARE_WAITQUEUE(wait, current);

    if(!card->in_suspend) 
        return;

    card->in_suspend++;
    add_wait_queue(&card->suspend_queue, &wait);
    set_current_state(TASK_UNINTERRUPTIBLE);
    schedule();
    remove_wait_queue(&card->suspend_queue, &wait);
    set_current_state(TASK_RUNNING);
}
