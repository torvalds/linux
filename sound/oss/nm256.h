#ifndef _NM256_H_
#define _NM256_H_

#include <linux/spinlock.h>
#include <linux/interrupt.h>

#include "ac97.h"

/* The revisions that we currently handle.  */
enum nm256rev {
    REV_NM256AV, REV_NM256ZX
};

/* Per-card structure. */
struct nm256_info 
{
    /* Magic number used to verify that this struct is valid. */
#define NM_MAGIC_SIG 0x55aa00ff
    int magsig;

    /* Revision number */
    enum nm256rev rev;

    struct ac97_hwint mdev;

    /* Our audio device numbers. */
    int dev[2];

    /* The # of times each device has been opened. (Should only be 
       0 or 1). */
    int opencnt[2];

    /* We use two devices, because we can do simultaneous play and record.
       This keeps track of which device is being used for what purpose;
       these are the actual device numbers. */
    int dev_for_play;
    int dev_for_record;

	spinlock_t lock;
	
    /* The mixer device. */
    int mixer_oss_dev;

    /* 
     * Can only be opened once for each operation.  These aren't set
     * until an actual I/O operation is performed; this allows one
     * device to be open for read/write without inhibiting I/O to
     * the other device.
     */
    int is_open_play;
    int is_open_record;

    /* Non-zero if we're currently playing a sample. */
    int playing;
    /* Ditto for recording a sample. */
    int recording;

    /* The two memory ports.  */
    struct nm256_ports {
	/* Physical address of the port. */
	u32 physaddr;
	/* Our mapped-in pointer. */
	char __iomem *ptr;
	/* PTR's offset within the physical port.  */
	u32 start_offset;
	/* And the offset of the end of the buffer.  */
	u32 end_offset;
    } port[2];

    /* The following are offsets within memory port 1. */
    u32 coeffBuf;
    u32 allCoeffBuf;

    /* Record and playback buffers. */
    u32 abuf1, abuf2;

    /* Offset of the AC97 mixer in memory port 2. */
    u32 mixer;

    /* Offset of the mixer status register in memory port 2.  */
    u32 mixer_status_offset;

    /* Non-zero if we have written initial values to the mixer. */
    u8 mixer_values_init;

    /* 
     * Status mask bit; (*mixer_status_loc & mixer_status_mask) == 0 means
     * it's ready.  
     */
    u16 mixer_status_mask;

    /* The sizes of the playback and record ring buffers. */
    u32 playbackBufferSize;
    u32 recordBufferSize;

    /* Are the coefficient values in the memory cache current? */
    u8 coeffsCurrent;

    /* For writes, the amount we last wrote. */
    u32 requested_amt;
    /* The start of the block currently playing. */
    u32 curPlayPos;

    /* The amount of data we were requested to record. */
    u32 requestedRecAmt;
    /* The offset of the currently-recording block. */
    u32 curRecPos;
    /* The destination buffer. */
    char *recBuf;

    /* Our IRQ number. */
    int irq;

    /* A flag indicating how many times we've grabbed the IRQ. */
    int has_irq;

    /* The card interrupt service routine. */
    irqreturn_t (*introutine) (int, void *, struct pt_regs *);

    /* Current audio config, cached. */
    struct sinfo {
	u32 samplerate;
	u8 bits;
	u8 stereo;
    } sinfo[2]; /* goes with each device */

    /* The cards are stored in a chain;  this is the next card. */
    struct nm256_info *next_card;
};

/* The BIOS signature. */
#define NM_SIGNATURE 0x4e4d0000
/* Signature mask. */
#define NM_SIG_MASK 0xffff0000

/* Size of the second memory area. */
#define NM_PORT2_SIZE 4096

/* The base offset of the mixer in the second memory area. */
#define NM_MIXER_OFFSET 0x600

/* The maximum size of a coefficient entry. */
#define NM_MAX_COEFFICIENT 0x5000

/* The interrupt register. */
#define NM_INT_REG 0xa04
/* And its bits. */
#define NM_PLAYBACK_INT 0x40
#define NM_RECORD_INT 0x100
#define NM_MISC_INT_1 0x4000
#define NM_MISC_INT_2 0x1
#define NM_ACK_INT(CARD, X) nm256_writePort16((CARD), 2, NM_INT_REG, (X) << 1)

/* The AV's "mixer ready" status bit and location. */
#define NM_MIXER_STATUS_OFFSET 0xa04
#define NM_MIXER_READY_MASK 0x0800
#define NM_MIXER_PRESENCE 0xa06
#define NM_PRESENCE_MASK 0x0050
#define NM_PRESENCE_VALUE 0x0040

/*
 * For the ZX.  It uses the same interrupt register, but it holds 32
 * bits instead of 16.
 */
#define NM2_PLAYBACK_INT 0x10000
#define NM2_RECORD_INT 0x80000
#define NM2_MISC_INT_1 0x8
#define NM2_MISC_INT_2 0x2
#define NM2_ACK_INT(CARD, X) nm256_writePort32((CARD), 2, NM_INT_REG, (X))

/* The ZX's "mixer ready" status bit and location. */
#define NM2_MIXER_STATUS_OFFSET 0xa06
#define NM2_MIXER_READY_MASK 0x0800

/* The playback registers start from here. */
#define NM_PLAYBACK_REG_OFFSET 0x0
/* The record registers start from here. */
#define NM_RECORD_REG_OFFSET 0x200

/* The rate register is located 2 bytes from the start of the register area. */
#define NM_RATE_REG_OFFSET 2

/* Mono/stereo flag, number of bits on playback, and rate mask. */
#define NM_RATE_STEREO 1
#define NM_RATE_BITS_16 2
#define NM_RATE_MASK 0xf0

/* Playback enable register. */
#define NM_PLAYBACK_ENABLE_REG (NM_PLAYBACK_REG_OFFSET + 0x1)
#define NM_PLAYBACK_ENABLE_FLAG 1
#define NM_PLAYBACK_ONESHOT 2
#define NM_PLAYBACK_FREERUN 4

/* Mutes the audio output. */
#define NM_AUDIO_MUTE_REG (NM_PLAYBACK_REG_OFFSET + 0x18)
#define NM_AUDIO_MUTE_LEFT 0x8000
#define NM_AUDIO_MUTE_RIGHT 0x0080

/* Recording enable register. */
#define NM_RECORD_ENABLE_REG (NM_RECORD_REG_OFFSET + 0)
#define NM_RECORD_ENABLE_FLAG 1
#define NM_RECORD_FREERUN 2

#define NM_RBUFFER_START (NM_RECORD_REG_OFFSET + 0x4)
#define NM_RBUFFER_END   (NM_RECORD_REG_OFFSET + 0x10)
#define NM_RBUFFER_WMARK (NM_RECORD_REG_OFFSET + 0xc)
#define NM_RBUFFER_CURRP (NM_RECORD_REG_OFFSET + 0x8)

#define NM_PBUFFER_START (NM_PLAYBACK_REG_OFFSET + 0x4)
#define NM_PBUFFER_END   (NM_PLAYBACK_REG_OFFSET + 0x14)
#define NM_PBUFFER_WMARK (NM_PLAYBACK_REG_OFFSET + 0xc)
#define NM_PBUFFER_CURRP (NM_PLAYBACK_REG_OFFSET + 0x8)

/* A few trivial routines to make it easier to work with the registers
   on the chip. */

/* This is a common code portion used to fix up the port offsets. */
#define NM_FIX_PORT \
  if (port < 1 || port > 2 || card == NULL) \
      return -1; \
\
    if (offset < card->port[port - 1].start_offset \
	|| offset >= card->port[port - 1].end_offset) { \
	printk (KERN_ERR "Bad access: port %d, offset 0x%x\n", port, offset); \
	return -1; \
    } \
    offset -= card->port[port - 1].start_offset;

#define DEFwritePortX(X, func) \
static inline int nm256_writePort##X (struct nm256_info *card,\
				      int port, int offset, int value)\
{\
    u##X __iomem *addr;\
\
    if (nm256_debug > 1)\
        printk (KERN_DEBUG "Writing 0x%x to %d:0x%x\n", value, port, offset);\
\
    NM_FIX_PORT;\
\
    addr = (u##X __iomem *)(card->port[port - 1].ptr + offset);\
    func (value, addr);\
    return 0;\
}

DEFwritePortX (8, writeb)
DEFwritePortX (16, writew)
DEFwritePortX (32, writel)

#define DEFreadPortX(X, func) \
static inline u##X nm256_readPort##X (struct nm256_info *card,\
					int port, int offset)\
{\
    u##X __iomem *addr;\
\
    NM_FIX_PORT\
\
    addr = (u##X __iomem *)(card->port[port - 1].ptr + offset);\
    return func(addr);\
}

DEFreadPortX (8, readb)
DEFreadPortX (16, readw)
DEFreadPortX (32, readl)

static inline int
nm256_writeBuffer8 (struct nm256_info *card, u8 *src, int port, int offset,
		      int amt)
{
    NM_FIX_PORT;
    memcpy_toio (card->port[port - 1].ptr + offset, src, amt);
    return 0;
}

static inline int
nm256_readBuffer8 (struct nm256_info *card, u8 *dst, int port, int offset,
		     int amt)
{
    NM_FIX_PORT;
    memcpy_fromio (dst, card->port[port - 1].ptr + offset, amt);
    return 0;
}

/* Returns a non-zero value if we should use the coefficient cache. */
static int nm256_cachedCoefficients (struct nm256_info *card);

#endif

/*
 * Local variables:
 * c-basic-offset: 4
 * End:
 */
