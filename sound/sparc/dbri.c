/*
 * Driver for DBRI sound chip found on Sparcs.
 * Copyright (C) 2004, 2005 Martin Habets (mhabets@users.sourceforge.net)
 *
 * Based entirely upon drivers/sbus/audio/dbri.c which is:
 * Copyright (C) 1997 Rudolf Koenig (rfkoenig@immd4.informatik.uni-erlangen.de)
 * Copyright (C) 1998, 1999 Brent Baccala (baccala@freesoft.org)
 *
 * This is the lowlevel driver for the DBRI & MMCODEC duo used for ISDN & AUDIO
 * on Sun SPARCstation 10, 20, LX and Voyager models.
 *
 * - DBRI: AT&T T5900FX Dual Basic Rates ISDN Interface. It is a 32 channel
 *   data time multiplexer with ISDN support (aka T7259)
 *   Interfaces: SBus,ISDN NT & TE, CHI, 4 bits parallel.
 *   CHI: (spelled ki) Concentration Highway Interface (AT&T or Intel bus ?).
 *   Documentation:
 *   - "STP 4000SBus Dual Basic Rate ISDN (DBRI) Tranceiver" from
 *     Sparc Technology Business (courtesy of Sun Support)
 *   - Data sheet of the T7903, a newer but very similar ISA bus equivalent
 *     available from the Lucent (formarly AT&T microelectronics) home
 *     page.
 *   - http://www.freesoft.org/Linux/DBRI/
 * - MMCODEC: Crystal Semiconductor CS4215 16 bit Multimedia Audio Codec
 *   Interfaces: CHI, Audio In & Out, 2 bits parallel
 *   Documentation: from the Crystal Semiconductor home page.
 *
 * The DBRI is a 32 pipe machine, each pipe can transfer some bits between
 * memory and a serial device (long pipes, nr 0-15) or between two serial
 * devices (short pipes, nr 16-31), or simply send a fixed data to a serial
 * device (short pipes).
 * A timeslot defines the bit-offset and nr of bits read from a serial device.
 * The timeslots are linked to 6 circular lists, one for each direction for
 * each serial device (NT,TE,CHI). A timeslot is associated to 1 or 2 pipes
 * (the second one is a monitor/tee pipe, valid only for serial input).
 *
 * The mmcodec is connected via the CHI bus and needs the data & some
 * parameters (volume, balance, output selection) timemultiplexed in 8 byte
 * chunks. It also has a control mode, which serves for audio format setting.
 *
 * Looking at the CS4215 data sheet it is easy to set up 2 or 4 codecs on
 * the same CHI bus, so I thought perhaps it is possible to use the onboard
 * & the speakerbox codec simultanously, giving 2 (not very independent :-)
 * audio devices. But the SUN HW group decided against it, at least on my
 * LX the speakerbox connector has at least 1 pin missing and 1 wrongly
 * connected.
 *
 * I've tried to stick to the following function naming conventions:
 * snd_*	ALSA stuff
 * cs4215_*	CS4215 codec specfic stuff
 * dbri_*	DBRI high-level stuff
 * other	DBRI low-level stuff
 */

#include <sound/driver.h>
#include <linux/interrupt.h>
#include <linux/delay.h>

#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/info.h>
#include <sound/control.h>
#include <sound/initval.h>

#include <asm/irq.h>
#include <asm/io.h>
#include <asm/sbus.h>
#include <asm/atomic.h>

MODULE_AUTHOR("Rudolf Koenig, Brent Baccala and Martin Habets");
MODULE_DESCRIPTION("Sun DBRI");
MODULE_LICENSE("GPL");
MODULE_SUPPORTED_DEVICE("{{Sun,DBRI}}");

static int index[SNDRV_CARDS] = SNDRV_DEFAULT_IDX;	/* Index 0-MAX */
static char *id[SNDRV_CARDS] = SNDRV_DEFAULT_STR;	/* ID for this card */
static int enable[SNDRV_CARDS] = SNDRV_DEFAULT_ENABLE_PNP;	/* Enable this card */

module_param_array(index, int, NULL, 0444);
MODULE_PARM_DESC(index, "Index value for Sun DBRI soundcard.");
module_param_array(id, charp, NULL, 0444);
MODULE_PARM_DESC(id, "ID string for Sun DBRI soundcard.");
module_param_array(enable, bool, NULL, 0444);
MODULE_PARM_DESC(enable, "Enable Sun DBRI soundcard.");

#define DBRI_DEBUG

#define D_INT	(1<<0)
#define D_GEN	(1<<1)
#define D_CMD	(1<<2)
#define D_MM	(1<<3)
#define D_USR	(1<<4)
#define D_DESC	(1<<5)

static int dbri_debug = 0;
module_param(dbri_debug, int, 0644);
MODULE_PARM_DESC(dbri_debug, "Debug value for Sun DBRI soundcard.");

#ifdef DBRI_DEBUG
static char *cmds[] = {
	"WAIT", "PAUSE", "JUMP", "IIQ", "REX", "SDP", "CDP", "DTS",
	"SSP", "CHI", "NT", "TE", "CDEC", "TEST", "CDM", "RESRV"
};

#define dprintk(a, x...) if(dbri_debug & a) printk(KERN_DEBUG x)

#define DBRI_CMD(cmd, intr, value) ((cmd << 28) |			\
				    (1 << 27) | \
				    value)
#else
#define dprintk(a, x...)

#define DBRI_CMD(cmd, intr, value) ((cmd << 28) |			\
				    (intr << 27) | \
				    value)
#endif				/* DBRI_DEBUG */

/***************************************************************************
	CS4215 specific definitions and structures
****************************************************************************/

struct cs4215 {
	__u8 data[4];		/* Data mode: Time slots 5-8 */
	__u8 ctrl[4];		/* Ctrl mode: Time slots 1-4 */
	__u8 onboard;
	__u8 offset;		/* Bit offset from frame sync to time slot 1 */
	volatile __u32 status;
	volatile __u32 version;
	__u8 precision;		/* In bits, either 8 or 16 */
	__u8 channels;		/* 1 or 2 */
};

/*
 * Control mode first 
 */

/* Time Slot 1, Status register */
#define CS4215_CLB	(1<<2)	/* Control Latch Bit */
#define CS4215_OLB	(1<<3)	/* 1: line: 2.0V, speaker 4V */
				/* 0: line: 2.8V, speaker 8V */
#define CS4215_MLB	(1<<4)	/* 1: Microphone: 20dB gain disabled */
#define CS4215_RSRVD_1  (1<<5)

/* Time Slot 2, Data Format Register */
#define CS4215_DFR_LINEAR16	0
#define CS4215_DFR_ULAW		1
#define CS4215_DFR_ALAW		2
#define CS4215_DFR_LINEAR8	3
#define CS4215_DFR_STEREO	(1<<2)
static struct {
	unsigned short freq;
	unsigned char xtal;
	unsigned char csval;
} CS4215_FREQ[] = {
	{  8000, (1 << 4), (0 << 3) },
	{ 16000, (1 << 4), (1 << 3) },
	{ 27429, (1 << 4), (2 << 3) },	/* Actually 24428.57 */
	{ 32000, (1 << 4), (3 << 3) },
     /* {    NA, (1 << 4), (4 << 3) }, */
     /* {    NA, (1 << 4), (5 << 3) }, */
	{ 48000, (1 << 4), (6 << 3) },
	{  9600, (1 << 4), (7 << 3) },
	{  5513, (2 << 4), (0 << 3) },	/* Actually 5512.5 */
	{ 11025, (2 << 4), (1 << 3) },
	{ 18900, (2 << 4), (2 << 3) },
	{ 22050, (2 << 4), (3 << 3) },
	{ 37800, (2 << 4), (4 << 3) },
	{ 44100, (2 << 4), (5 << 3) },
	{ 33075, (2 << 4), (6 << 3) },
	{  6615, (2 << 4), (7 << 3) },
	{ 0, 0, 0}
};

#define CS4215_HPF	(1<<7)	/* High Pass Filter, 1: Enabled */

#define CS4215_12_MASK	0xfcbf	/* Mask off reserved bits in slot 1 & 2 */

/* Time Slot 3, Serial Port Control register */
#define CS4215_XEN	(1<<0)	/* 0: Enable serial output */
#define CS4215_XCLK	(1<<1)	/* 1: Master mode: Generate SCLK */
#define CS4215_BSEL_64	(0<<2)	/* Bitrate: 64 bits per frame */
#define CS4215_BSEL_128	(1<<2)
#define CS4215_BSEL_256	(2<<2)
#define CS4215_MCK_MAST (0<<4)	/* Master clock */
#define CS4215_MCK_XTL1 (1<<4)	/* 24.576 MHz clock source */
#define CS4215_MCK_XTL2 (2<<4)	/* 16.9344 MHz clock source */
#define CS4215_MCK_CLK1 (3<<4)	/* Clockin, 256 x Fs */
#define CS4215_MCK_CLK2 (4<<4)	/* Clockin, see DFR */

/* Time Slot 4, Test Register */
#define CS4215_DAD	(1<<0)	/* 0:Digital-Dig loop, 1:Dig-Analog-Dig loop */
#define CS4215_ENL	(1<<1)	/* Enable Loopback Testing */

/* Time Slot 5, Parallel Port Register */
/* Read only here and the same as the in data mode */

/* Time Slot 6, Reserved  */

/* Time Slot 7, Version Register  */
#define CS4215_VERSION_MASK 0xf	/* Known versions 0/C, 1/D, 2/E */

/* Time Slot 8, Reserved  */

/*
 * Data mode
 */
/* Time Slot 1-2: Left Channel Data, 2-3: Right Channel Data  */

/* Time Slot 5, Output Setting  */
#define CS4215_LO(v)	v	/* Left Output Attenuation 0x3f: -94.5 dB */
#define CS4215_LE	(1<<6)	/* Line Out Enable */
#define CS4215_HE	(1<<7)	/* Headphone Enable */

/* Time Slot 6, Output Setting  */
#define CS4215_RO(v)	v	/* Right Output Attenuation 0x3f: -94.5 dB */
#define CS4215_SE	(1<<6)	/* Speaker Enable */
#define CS4215_ADI	(1<<7)	/* A/D Data Invalid: Busy in calibration */

/* Time Slot 7, Input Setting */
#define CS4215_LG(v)	v	/* Left Gain Setting 0xf: 22.5 dB */
#define CS4215_IS	(1<<4)	/* Input Select: 1=Microphone, 0=Line */
#define CS4215_OVR	(1<<5)	/* 1: Overrange condition occurred */
#define CS4215_PIO0	(1<<6)	/* Parallel I/O 0 */
#define CS4215_PIO1	(1<<7)

/* Time Slot 8, Input Setting */
#define CS4215_RG(v)	v	/* Right Gain Setting 0xf: 22.5 dB */
#define CS4215_MA(v)	(v<<4)	/* Monitor Path Attenuation 0xf: mute */

/***************************************************************************
		DBRI specific definitions and structures
****************************************************************************/

/* DBRI main registers */
#define REG0	0x00UL		/* Status and Control */
#define REG1	0x04UL		/* Mode and Interrupt */
#define REG2	0x08UL		/* Parallel IO */
#define REG3	0x0cUL		/* Test */
#define REG8	0x20UL		/* Command Queue Pointer */
#define REG9	0x24UL		/* Interrupt Queue Pointer */

#define DBRI_NO_CMDS	64
#define DBRI_NO_INTS	1	/* Note: the value of this define was
				 * originally 2.  The ringbuffer to store
				 * interrupts in dma is currently broken.
				 * This is a temporary fix until the ringbuffer
				 * is fixed.
				 */
#define DBRI_INT_BLK	64
#define DBRI_NO_DESCS	64
#define DBRI_NO_PIPES	32

#define DBRI_MM_ONB	1
#define DBRI_MM_SB	2

#define DBRI_REC	0
#define DBRI_PLAY	1
#define DBRI_NO_STREAMS	2

/* One transmit/receive descriptor */
struct dbri_mem {
	volatile __u32 word1;
	volatile __u32 ba;	/* Transmit/Receive Buffer Address */
	volatile __u32 nda;	/* Next Descriptor Address */
	volatile __u32 word4;
};

/* This structure is in a DMA region where it can accessed by both
 * the CPU and the DBRI
 */
struct dbri_dma {
	volatile s32 cmd[DBRI_NO_CMDS];	/* Place for commands       */
	volatile s32 intr[DBRI_NO_INTS * DBRI_INT_BLK];	/* Interrupt field  */
	struct dbri_mem desc[DBRI_NO_DESCS];	/* Xmit/receive descriptors */
};

#define dbri_dma_off(member, elem)	\
	((u32)(unsigned long)		\
	 (&(((struct dbri_dma *)0)->member[elem])))

enum in_or_out { PIPEinput, PIPEoutput };

struct dbri_pipe {
	u32 sdp;		/* SDP command word */
	enum in_or_out direction;
	int nextpipe;		/* Next pipe in linked list */
	int prevpipe;
	int cycle;		/* Offset of timeslot (bits) */
	int length;		/* Length of timeslot (bits) */
	int first_desc;		/* Index of first descriptor */
	int desc;		/* Index of active descriptor */
	volatile __u32 *recv_fixed_ptr;	/* Ptr to receive fixed data */
};

struct dbri_desc {
	int inuse;		/* Boolean flag */
	int next;		/* Index of next desc, or -1 */
	unsigned int len;
};

/* Per stream (playback or record) information */
typedef struct dbri_streaminfo {
	snd_pcm_substream_t *substream;
	u32 dvma_buffer;	/* Device view of Alsa DMA buffer */
	int left;		/* # of bytes left in DMA buffer  */
	int size;		/* Size of DMA buffer             */
	size_t offset;		/* offset in user buffer          */
	int pipe;		/* Data pipe used                 */
	int left_gain;		/* mixer elements                 */
	int right_gain;
	int balance;
} dbri_streaminfo_t;

/* This structure holds the information for both chips (DBRI & CS4215) */
typedef struct snd_dbri {
	snd_card_t *card;	/* ALSA card */
	snd_pcm_t *pcm;

	int regs_size, irq;	/* Needed for unload */
	struct sbus_dev *sdev;	/* SBUS device info */
	spinlock_t lock;

	volatile struct dbri_dma *dma;	/* Pointer to our DMA block */
	u32 dma_dvma;		/* DBRI visible DMA address */

	void __iomem *regs;	/* dbri HW regs */
	int dbri_version;	/* 'e' and up is OK */
	int dbri_irqp;		/* intr queue pointer */
	int wait_send;		/* sequence of command buffers send */
	int wait_ackd;		/* sequence of command buffers acknowledged */

	struct dbri_pipe pipes[DBRI_NO_PIPES];	/* DBRI's 32 data pipes */
	struct dbri_desc descs[DBRI_NO_DESCS];

	int chi_in_pipe;
	int chi_out_pipe;
	int chi_bpf;

	struct cs4215 mm;	/* mmcodec special info */
				/* per stream (playback/record) info */
	struct dbri_streaminfo stream_info[DBRI_NO_STREAMS];

	struct snd_dbri *next;
} snd_dbri_t;

#define DBRI_MAX_VOLUME		63	/* Output volume */
#define DBRI_MAX_GAIN		15	/* Input gain */
#define DBRI_RIGHT_BALANCE	255
#define DBRI_MID_BALANCE	(DBRI_RIGHT_BALANCE >> 1)

/* DBRI Reg0 - Status Control Register - defines. (Page 17) */
#define D_P		(1<<15)	/* Program command & queue pointer valid */
#define D_G		(1<<14)	/* Allow 4-Word SBus Burst */
#define D_S		(1<<13)	/* Allow 16-Word SBus Burst */
#define D_E		(1<<12)	/* Allow 8-Word SBus Burst */
#define D_X		(1<<7)	/* Sanity Timer Disable */
#define D_T		(1<<6)	/* Permit activation of the TE interface */
#define D_N		(1<<5)	/* Permit activation of the NT interface */
#define D_C		(1<<4)	/* Permit activation of the CHI interface */
#define D_F		(1<<3)	/* Force Sanity Timer Time-Out */
#define D_D		(1<<2)	/* Disable Master Mode */
#define D_H		(1<<1)	/* Halt for Analysis */
#define D_R		(1<<0)	/* Soft Reset */

/* DBRI Reg1 - Mode and Interrupt Register - defines. (Page 18) */
#define D_LITTLE_END	(1<<8)	/* Byte Order */
#define D_BIG_END	(0<<8)	/* Byte Order */
#define D_MRR		(1<<4)	/* Multiple Error Ack on SBus (readonly) */
#define D_MLE		(1<<3)	/* Multiple Late Error on SBus (readonly) */
#define D_LBG		(1<<2)	/* Lost Bus Grant on SBus (readonly) */
#define D_MBE		(1<<1)	/* Burst Error on SBus (readonly) */
#define D_IR		(1<<0)	/* Interrupt Indicator (readonly) */

/* DBRI Reg2 - Parallel IO Register - defines. (Page 18) */
#define D_ENPIO3	(1<<7)	/* Enable Pin 3 */
#define D_ENPIO2	(1<<6)	/* Enable Pin 2 */
#define D_ENPIO1	(1<<5)	/* Enable Pin 1 */
#define D_ENPIO0	(1<<4)	/* Enable Pin 0 */
#define D_ENPIO		(0xf0)	/* Enable all the pins */
#define D_PIO3		(1<<3)	/* Pin 3: 1: Data mode, 0: Ctrl mode */
#define D_PIO2		(1<<2)	/* Pin 2: 1: Onboard PDN */
#define D_PIO1		(1<<1)	/* Pin 1: 0: Reset */
#define D_PIO0		(1<<0)	/* Pin 0: 1: Speakerbox PDN */

/* DBRI Commands (Page 20) */
#define D_WAIT		0x0	/* Stop execution */
#define D_PAUSE		0x1	/* Flush long pipes */
#define D_JUMP		0x2	/* New command queue */
#define D_IIQ		0x3	/* Initialize Interrupt Queue */
#define D_REX		0x4	/* Report command execution via interrupt */
#define D_SDP		0x5	/* Setup Data Pipe */
#define D_CDP		0x6	/* Continue Data Pipe (reread NULL Pointer) */
#define D_DTS		0x7	/* Define Time Slot */
#define D_SSP		0x8	/* Set short Data Pipe */
#define D_CHI		0x9	/* Set CHI Global Mode */
#define D_NT		0xa	/* NT Command */
#define D_TE		0xb	/* TE Command */
#define D_CDEC		0xc	/* Codec setup */
#define D_TEST		0xd	/* No comment */
#define D_CDM		0xe	/* CHI Data mode command */

/* Special bits for some commands */
#define D_PIPE(v)      ((v)<<0)	/* Pipe Nr: 0-15 long, 16-21 short */

/* Setup Data Pipe */
/* IRM */
#define D_SDP_2SAME	(1<<18)	/* Report 2nd time in a row value rcvd */
#define D_SDP_CHANGE	(2<<18)	/* Report any changes */
#define D_SDP_EVERY	(3<<18)	/* Report any changes */
#define D_SDP_EOL	(1<<17)	/* EOL interrupt enable */
#define D_SDP_IDLE	(1<<16)	/* HDLC idle interrupt enable */

/* Pipe data MODE */
#define D_SDP_MEM	(0<<13)	/* To/from memory */
#define D_SDP_HDLC	(2<<13)
#define D_SDP_HDLC_D	(3<<13)	/* D Channel (prio control) */
#define D_SDP_SER	(4<<13)	/* Serial to serial */
#define D_SDP_FIXED	(6<<13)	/* Short only */
#define D_SDP_MODE(v)	((v)&(7<<13))

#define D_SDP_TO_SER	(1<<12)	/* Direction */
#define D_SDP_FROM_SER	(0<<12)	/* Direction */
#define D_SDP_MSB	(1<<11)	/* Bit order within Byte */
#define D_SDP_LSB	(0<<11)	/* Bit order within Byte */
#define D_SDP_P		(1<<10)	/* Pointer Valid */
#define D_SDP_A		(1<<8)	/* Abort */
#define D_SDP_C		(1<<7)	/* Clear */

/* Define Time Slot */
#define D_DTS_VI	(1<<17)	/* Valid Input Time-Slot Descriptor */
#define D_DTS_VO	(1<<16)	/* Valid Output Time-Slot Descriptor */
#define D_DTS_INS	(1<<15)	/* Insert Time Slot */
#define D_DTS_DEL	(0<<15)	/* Delete Time Slot */
#define D_DTS_PRVIN(v) ((v)<<10)	/* Previous In Pipe */
#define D_DTS_PRVOUT(v)        ((v)<<5)	/* Previous Out Pipe */

/* Time Slot defines */
#define D_TS_LEN(v)	((v)<<24)	/* Number of bits in this time slot */
#define D_TS_CYCLE(v)	((v)<<14)	/* Bit Count at start of TS */
#define D_TS_DI		(1<<13)	/* Data Invert */
#define D_TS_1CHANNEL	(0<<10)	/* Single Channel / Normal mode */
#define D_TS_MONITOR	(2<<10)	/* Monitor pipe */
#define D_TS_NONCONTIG	(3<<10)	/* Non contiguous mode */
#define D_TS_ANCHOR	(7<<10)	/* Starting short pipes */
#define D_TS_MON(v)    ((v)<<5)	/* Monitor Pipe */
#define D_TS_NEXT(v)   ((v)<<0)	/* Pipe Nr: 0-15 long, 16-21 short */

/* Concentration Highway Interface Modes */
#define D_CHI_CHICM(v)	((v)<<16)	/* Clock mode */
#define D_CHI_IR	(1<<15)	/* Immediate Interrupt Report */
#define D_CHI_EN	(1<<14)	/* CHIL Interrupt enabled */
#define D_CHI_OD	(1<<13)	/* Open Drain Enable */
#define D_CHI_FE	(1<<12)	/* Sample CHIFS on Rising Frame Edge */
#define D_CHI_FD	(1<<11)	/* Frame Drive */
#define D_CHI_BPF(v)	((v)<<0)	/* Bits per Frame */

/* NT: These are here for completeness */
#define D_NT_FBIT	(1<<17)	/* Frame Bit */
#define D_NT_NBF	(1<<16)	/* Number of bad frames to loose framing */
#define D_NT_IRM_IMM	(1<<15)	/* Interrupt Report & Mask: Immediate */
#define D_NT_IRM_EN	(1<<14)	/* Interrupt Report & Mask: Enable */
#define D_NT_ISNT	(1<<13)	/* Configfure interface as NT */
#define D_NT_FT		(1<<12)	/* Fixed Timing */
#define D_NT_EZ		(1<<11)	/* Echo Channel is Zeros */
#define D_NT_IFA	(1<<10)	/* Inhibit Final Activation */
#define D_NT_ACT	(1<<9)	/* Activate Interface */
#define D_NT_MFE	(1<<8)	/* Multiframe Enable */
#define D_NT_RLB(v)	((v)<<5)	/* Remote Loopback */
#define D_NT_LLB(v)	((v)<<2)	/* Local Loopback */
#define D_NT_FACT	(1<<1)	/* Force Activation */
#define D_NT_ABV	(1<<0)	/* Activate Bipolar Violation */

/* Codec Setup */
#define D_CDEC_CK(v)	((v)<<24)	/* Clock Select */
#define D_CDEC_FED(v)	((v)<<12)	/* FSCOD Falling Edge Delay */
#define D_CDEC_RED(v)	((v)<<0)	/* FSCOD Rising Edge Delay */

/* Test */
#define D_TEST_RAM(v)	((v)<<16)	/* RAM Pointer */
#define D_TEST_SIZE(v)	((v)<<11)	/* */
#define D_TEST_ROMONOFF	0x5	/* Toggle ROM opcode monitor on/off */
#define D_TEST_PROC	0x6	/* MicroProcessor test */
#define D_TEST_SER	0x7	/* Serial-Controller test */
#define D_TEST_RAMREAD	0x8	/* Copy from Ram to system memory */
#define D_TEST_RAMWRITE	0x9	/* Copy into Ram from system memory */
#define D_TEST_RAMBIST	0xa	/* RAM Built-In Self Test */
#define D_TEST_MCBIST	0xb	/* Microcontroller Built-In Self Test */
#define D_TEST_DUMP	0xe	/* ROM Dump */

/* CHI Data Mode */
#define D_CDM_THI	(1<<8)	/* Transmit Data on CHIDR Pin */
#define D_CDM_RHI	(1<<7)	/* Receive Data on CHIDX Pin */
#define D_CDM_RCE	(1<<6)	/* Receive on Rising Edge of CHICK */
#define D_CDM_XCE	(1<<2)	/* Transmit Data on Rising Edge of CHICK */
#define D_CDM_XEN	(1<<1)	/* Transmit Highway Enable */
#define D_CDM_REN	(1<<0)	/* Receive Highway Enable */

/* The Interrupts */
#define D_INTR_BRDY	1	/* Buffer Ready for processing */
#define D_INTR_MINT	2	/* Marked Interrupt in RD/TD */
#define D_INTR_IBEG	3	/* Flag to idle transition detected (HDLC) */
#define D_INTR_IEND	4	/* Idle to flag transition detected (HDLC) */
#define D_INTR_EOL	5	/* End of List */
#define D_INTR_CMDI	6	/* Command has bean read */
#define D_INTR_XCMP	8	/* Transmission of frame complete */
#define D_INTR_SBRI	9	/* BRI status change info */
#define D_INTR_FXDT	10	/* Fixed data change */
#define D_INTR_CHIL	11	/* CHI lost frame sync (channel 36 only) */
#define D_INTR_COLL	11	/* Unrecoverable D-Channel collision */
#define D_INTR_DBYT	12	/* Dropped by frame slip */
#define D_INTR_RBYT	13	/* Repeated by frame slip */
#define D_INTR_LINT	14	/* Lost Interrupt */
#define D_INTR_UNDR	15	/* DMA underrun */

#define D_INTR_TE	32
#define D_INTR_NT	34
#define D_INTR_CHI	36
#define D_INTR_CMD	38

#define D_INTR_GETCHAN(v)	(((v)>>24) & 0x3f)
#define D_INTR_GETCODE(v)	(((v)>>20) & 0xf)
#define D_INTR_GETCMD(v)	(((v)>>16) & 0xf)
#define D_INTR_GETVAL(v)	((v) & 0xffff)
#define D_INTR_GETRVAL(v)	((v) & 0xfffff)

#define D_P_0		0	/* TE receive anchor */
#define D_P_1		1	/* TE transmit anchor */
#define D_P_2		2	/* NT transmit anchor */
#define D_P_3		3	/* NT receive anchor */
#define D_P_4		4	/* CHI send data */
#define D_P_5		5	/* CHI receive data */
#define D_P_6		6	/* */
#define D_P_7		7	/* */
#define D_P_8		8	/* */
#define D_P_9		9	/* */
#define D_P_10		10	/* */
#define D_P_11		11	/* */
#define D_P_12		12	/* */
#define D_P_13		13	/* */
#define D_P_14		14	/* */
#define D_P_15		15	/* */
#define D_P_16		16	/* CHI anchor pipe */
#define D_P_17		17	/* CHI send */
#define D_P_18		18	/* CHI receive */
#define D_P_19		19	/* CHI receive */
#define D_P_20		20	/* CHI receive */
#define D_P_21		21	/* */
#define D_P_22		22	/* */
#define D_P_23		23	/* */
#define D_P_24		24	/* */
#define D_P_25		25	/* */
#define D_P_26		26	/* */
#define D_P_27		27	/* */
#define D_P_28		28	/* */
#define D_P_29		29	/* */
#define D_P_30		30	/* */
#define D_P_31		31	/* */

/* Transmit descriptor defines */
#define DBRI_TD_F	(1<<31)	/* End of Frame */
#define DBRI_TD_D	(1<<30)	/* Do not append CRC */
#define DBRI_TD_CNT(v)	((v)<<16)	/* Number of valid bytes in the buffer */
#define DBRI_TD_B	(1<<15)	/* Final interrupt */
#define DBRI_TD_M	(1<<14)	/* Marker interrupt */
#define DBRI_TD_I	(1<<13)	/* Transmit Idle Characters */
#define DBRI_TD_FCNT(v)	(v)	/* Flag Count */
#define DBRI_TD_UNR	(1<<3)	/* Underrun: transmitter is out of data */
#define DBRI_TD_ABT	(1<<2)	/* Abort: frame aborted */
#define DBRI_TD_TBC	(1<<0)	/* Transmit buffer Complete */
#define DBRI_TD_STATUS(v)       ((v)&0xff)	/* Transmit status */
			/* Maximum buffer size per TD: almost 8Kb */
#define DBRI_TD_MAXCNT	((1 << 13) - 1)

/* Receive descriptor defines */
#define DBRI_RD_F	(1<<31)	/* End of Frame */
#define DBRI_RD_C	(1<<30)	/* Completed buffer */
#define DBRI_RD_B	(1<<15)	/* Final interrupt */
#define DBRI_RD_M	(1<<14)	/* Marker interrupt */
#define DBRI_RD_BCNT(v)	(v)	/* Buffer size */
#define DBRI_RD_CRC	(1<<7)	/* 0: CRC is correct */
#define DBRI_RD_BBC	(1<<6)	/* 1: Bad Byte received */
#define DBRI_RD_ABT	(1<<5)	/* Abort: frame aborted */
#define DBRI_RD_OVRN	(1<<3)	/* Overrun: data lost */
#define DBRI_RD_STATUS(v)      ((v)&0xff)	/* Receive status */
#define DBRI_RD_CNT(v) (((v)>>16)&0x1fff)	/* Valid bytes in the buffer */

/* stream_info[] access */
/* Translate the ALSA direction into the array index */
#define DBRI_STREAMNO(substream)				\
		(substream->stream == 				\
		 SNDRV_PCM_STREAM_PLAYBACK? DBRI_PLAY: DBRI_REC)

/* Return a pointer to dbri_streaminfo */
#define DBRI_STREAM(dbri, substream)	&dbri->stream_info[DBRI_STREAMNO(substream)]

static snd_dbri_t *dbri_list = NULL;	/* All DBRI devices */

/*
 * Short data pipes transmit LSB first. The CS4215 receives MSB first. Grrr.
 * So we have to reverse the bits. Note: not all bit lengths are supported
 */
static __u32 reverse_bytes(__u32 b, int len)
{
	switch (len) {
	case 32:
		b = ((b & 0xffff0000) >> 16) | ((b & 0x0000ffff) << 16);
	case 16:
		b = ((b & 0xff00ff00) >> 8) | ((b & 0x00ff00ff) << 8);
	case 8:
		b = ((b & 0xf0f0f0f0) >> 4) | ((b & 0x0f0f0f0f) << 4);
	case 4:
		b = ((b & 0xcccccccc) >> 2) | ((b & 0x33333333) << 2);
	case 2:
		b = ((b & 0xaaaaaaaa) >> 1) | ((b & 0x55555555) << 1);
	case 1:
	case 0:
		break;
	default:
		printk(KERN_ERR "DBRI reverse_bytes: unsupported length\n");
	};

	return b;
}

/*
****************************************************************************
************** DBRI initialization and command synchronization *************
****************************************************************************

Commands are sent to the DBRI by building a list of them in memory,
then writing the address of the first list item to DBRI register 8.
The list is terminated with a WAIT command, which generates a
CPU interrupt to signal completion.

Since the DBRI can run in parallel with the CPU, several means of
synchronization present themselves.  The method implemented here is close
to the original scheme (Rudolf's), and uses 2 counters (wait_send and
wait_ackd) to synchronize the command buffer between the CPU and the DBRI.

A more sophisticated scheme might involve a circular command buffer
or an array of command buffers.  A routine could fill one with
commands and link it onto a list.  When a interrupt signaled
completion of the current command buffer, look on the list for
the next one.

Every time a routine wants to write commands to the DBRI, it must
first call dbri_cmdlock() and get an initial pointer into dbri->dma->cmd
in return. dbri_cmdlock() will block if the previous commands have not
been completed yet. After this the commands can be written to the buffer,
and dbri_cmdsend() is called with the final pointer value to send them
to the DBRI.

*/

static void dbri_process_interrupt_buffer(snd_dbri_t * dbri);

enum dbri_lock_t { NoGetLock, GetLock };
#define MAXLOOPS 10

static volatile s32 *dbri_cmdlock(snd_dbri_t * dbri, enum dbri_lock_t get)
{
	int maxloops = MAXLOOPS;

#ifndef SMP
	if ((get == GetLock) && spin_is_locked(&dbri->lock)) {
		printk(KERN_ERR "DBRI: cmdlock called while in spinlock.");
	}
#endif

	/* Delay if previous commands are still being processed */
	while ((--maxloops) > 0 && (dbri->wait_send != dbri->wait_ackd)) {
		msleep_interruptible(1);
		/* If dbri_cmdlock() got called from inside the
		 * interrupt handler, this will do the processing.
		 */
		dbri_process_interrupt_buffer(dbri);
	}
	if (maxloops == 0) {
		printk(KERN_ERR "DBRI: Chip never completed command buffer %d\n",
			dbri->wait_send);
	} else {
		dprintk(D_CMD, "Chip completed command buffer (%d)\n",
			MAXLOOPS - maxloops - 1);
	}

	/*if (get == GetLock) spin_lock(&dbri->lock); */
	return &dbri->dma->cmd[0];
}

static void dbri_cmdsend(snd_dbri_t * dbri, volatile s32 * cmd)
{
	volatile s32 *ptr;
	u32	reg;

	for (ptr = &dbri->dma->cmd[0]; ptr < cmd; ptr++) {
		dprintk(D_CMD, "cmd: %lx:%08x\n", (unsigned long)ptr, *ptr);
	}

	if ((cmd - &dbri->dma->cmd[0]) >= DBRI_NO_CMDS - 1) {
		printk(KERN_ERR "DBRI: Command buffer overflow! (bug in driver)\n");
		/* Ignore the last part. */
		cmd = &dbri->dma->cmd[DBRI_NO_CMDS - 3];
	}

	dbri->wait_send++;
	dbri->wait_send &= 0xffff;	/* restrict it to a 16 bit counter. */
	*(cmd++) = DBRI_CMD(D_PAUSE, 0, 0);
	*(cmd++) = DBRI_CMD(D_WAIT, 1, dbri->wait_send);

	/* Set command pointer and signal it is valid. */
	sbus_writel(dbri->dma_dvma, dbri->regs + REG8);
	reg = sbus_readl(dbri->regs + REG0);
	reg |= D_P;
	sbus_writel(reg, dbri->regs + REG0);

	/*spin_unlock(&dbri->lock); */
}

/* Lock must be held when calling this */
static void dbri_reset(snd_dbri_t * dbri)
{
	int i;

	dprintk(D_GEN, "reset 0:%x 2:%x 8:%x 9:%x\n",
		sbus_readl(dbri->regs + REG0),
		sbus_readl(dbri->regs + REG2),
		sbus_readl(dbri->regs + REG8), sbus_readl(dbri->regs + REG9));

	sbus_writel(D_R, dbri->regs + REG0);	/* Soft Reset */
	for (i = 0; (sbus_readl(dbri->regs + REG0) & D_R) && i < 64; i++)
		udelay(10);
}

/* Lock must not be held before calling this */
static void dbri_initialize(snd_dbri_t * dbri)
{
	volatile s32 *cmd;
	u32 dma_addr, tmp;
	unsigned long flags;
	int n;

	spin_lock_irqsave(&dbri->lock, flags);

	dbri_reset(dbri);

	cmd = dbri_cmdlock(dbri, NoGetLock);
	dprintk(D_GEN, "init: cmd: %p, int: %p\n",
		&dbri->dma->cmd[0], &dbri->dma->intr[0]);

	/*
	 * Initialize the interrupt ringbuffer.
	 */
	for (n = 0; n < DBRI_NO_INTS - 1; n++) {
		dma_addr = dbri->dma_dvma;
		dma_addr += dbri_dma_off(intr, ((n + 1) & DBRI_INT_BLK));
		dbri->dma->intr[n * DBRI_INT_BLK] = dma_addr;
	}
	dma_addr = dbri->dma_dvma + dbri_dma_off(intr, 0);
	dbri->dma->intr[n * DBRI_INT_BLK] = dma_addr;
	dbri->dbri_irqp = 1;

	/* Initialize pipes */
	for (n = 0; n < DBRI_NO_PIPES; n++)
		dbri->pipes[n].desc = dbri->pipes[n].first_desc = -1;

	/* A brute approach - DBRI falls back to working burst size by itself
	 * On SS20 D_S does not work, so do not try so high. */
	tmp = sbus_readl(dbri->regs + REG0);
	tmp |= D_G | D_E;
	tmp &= ~D_S;
	sbus_writel(tmp, dbri->regs + REG0);

	/*
	 * Set up the interrupt queue
	 */
	dma_addr = dbri->dma_dvma + dbri_dma_off(intr, 0);
	*(cmd++) = DBRI_CMD(D_IIQ, 0, 0);
	*(cmd++) = dma_addr;

	dbri_cmdsend(dbri, cmd);
	spin_unlock_irqrestore(&dbri->lock, flags);
}

/*
****************************************************************************
************************** DBRI data pipe management ***********************
****************************************************************************

While DBRI control functions use the command and interrupt buffers, the
main data path takes the form of data pipes, which can be short (command
and interrupt driven), or long (attached to DMA buffers).  These functions
provide a rudimentary means of setting up and managing the DBRI's pipes,
but the calling functions have to make sure they respect the pipes' linked
list ordering, among other things.  The transmit and receive functions
here interface closely with the transmit and receive interrupt code.

*/
static int pipe_active(snd_dbri_t * dbri, int pipe)
{
	return ((pipe >= 0) && (dbri->pipes[pipe].desc != -1));
}

/* reset_pipe(dbri, pipe)
 *
 * Called on an in-use pipe to clear anything being transmitted or received
 * Lock must be held before calling this.
 */
static void reset_pipe(snd_dbri_t * dbri, int pipe)
{
	int sdp;
	int desc;
	volatile int *cmd;

	if (pipe < 0 || pipe > 31) {
		printk(KERN_ERR "DBRI: reset_pipe called with illegal pipe number\n");
		return;
	}

	sdp = dbri->pipes[pipe].sdp;
	if (sdp == 0) {
		printk(KERN_ERR "DBRI: reset_pipe called on uninitialized pipe\n");
		return;
	}

	cmd = dbri_cmdlock(dbri, NoGetLock);
	*(cmd++) = DBRI_CMD(D_SDP, 0, sdp | D_SDP_C | D_SDP_P);
	*(cmd++) = 0;
	dbri_cmdsend(dbri, cmd);

	desc = dbri->pipes[pipe].first_desc;
	while (desc != -1) {
		dbri->descs[desc].inuse = 0;
		desc = dbri->descs[desc].next;
	}

	dbri->pipes[pipe].desc = -1;
	dbri->pipes[pipe].first_desc = -1;
}

/* FIXME: direction as an argument? */
static void setup_pipe(snd_dbri_t * dbri, int pipe, int sdp)
{
	if (pipe < 0 || pipe > 31) {
		printk(KERN_ERR "DBRI: setup_pipe called with illegal pipe number\n");
		return;
	}

	if ((sdp & 0xf800) != sdp) {
		printk(KERN_ERR "DBRI: setup_pipe called with strange SDP value\n");
		/* sdp &= 0xf800; */
	}

	/* If this is a fixed receive pipe, arrange for an interrupt
	 * every time its data changes
	 */
	if (D_SDP_MODE(sdp) == D_SDP_FIXED && !(sdp & D_SDP_TO_SER))
		sdp |= D_SDP_CHANGE;

	sdp |= D_PIPE(pipe);
	dbri->pipes[pipe].sdp = sdp;
	dbri->pipes[pipe].desc = -1;
	dbri->pipes[pipe].first_desc = -1;
	if (sdp & D_SDP_TO_SER)
		dbri->pipes[pipe].direction = PIPEoutput;
	else
		dbri->pipes[pipe].direction = PIPEinput;

	reset_pipe(dbri, pipe);
}

/* FIXME: direction not needed */
static void link_time_slot(snd_dbri_t * dbri, int pipe,
			   enum in_or_out direction, int basepipe,
			   int length, int cycle)
{
	volatile s32 *cmd;
	int val;
	int prevpipe;
	int nextpipe;

	if (pipe < 0 || pipe > 31 || basepipe < 0 || basepipe > 31) {
		printk(KERN_ERR 
		    "DBRI: link_time_slot called with illegal pipe number\n");
		return;
	}

	if (dbri->pipes[pipe].sdp == 0 || dbri->pipes[basepipe].sdp == 0) {
		printk(KERN_ERR "DBRI: link_time_slot called on uninitialized pipe\n");
		return;
	}

	/* Deal with CHI special case:
	 * "If transmission on edges 0 or 1 is desired, then cycle n
	 *  (where n = # of bit times per frame...) must be used."
	 *                  - DBRI data sheet, page 11
	 */
	if (basepipe == 16 && direction == PIPEoutput && cycle == 0)
		cycle = dbri->chi_bpf;

	if (basepipe == pipe) {
		prevpipe = pipe;
		nextpipe = pipe;
	} else {
		/* We're not initializing a new linked list (basepipe != pipe),
		 * so run through the linked list and find where this pipe
		 * should be sloted in, based on its cycle.  CHI confuses
		 * things a bit, since it has a single anchor for both its
		 * transmit and receive lists.
		 */
		if (basepipe == 16) {
			if (direction == PIPEinput) {
				prevpipe = dbri->chi_in_pipe;
			} else {
				prevpipe = dbri->chi_out_pipe;
			}
		} else {
			prevpipe = basepipe;
		}

		nextpipe = dbri->pipes[prevpipe].nextpipe;

		while (dbri->pipes[nextpipe].cycle < cycle
		       && dbri->pipes[nextpipe].nextpipe != basepipe) {
			prevpipe = nextpipe;
			nextpipe = dbri->pipes[nextpipe].nextpipe;
		}
	}

	if (prevpipe == 16) {
		if (direction == PIPEinput) {
			dbri->chi_in_pipe = pipe;
		} else {
			dbri->chi_out_pipe = pipe;
		}
	} else {
		dbri->pipes[prevpipe].nextpipe = pipe;
	}

	dbri->pipes[pipe].nextpipe = nextpipe;
	dbri->pipes[pipe].cycle = cycle;
	dbri->pipes[pipe].length = length;

	cmd = dbri_cmdlock(dbri, NoGetLock);

	if (direction == PIPEinput) {
		val = D_DTS_VI | D_DTS_INS | D_DTS_PRVIN(prevpipe) | pipe;
		*(cmd++) = DBRI_CMD(D_DTS, 0, val);
		*(cmd++) =
		    D_TS_LEN(length) | D_TS_CYCLE(cycle) | D_TS_NEXT(nextpipe);
		*(cmd++) = 0;
	} else {
		val = D_DTS_VO | D_DTS_INS | D_DTS_PRVOUT(prevpipe) | pipe;
		*(cmd++) = DBRI_CMD(D_DTS, 0, val);
		*(cmd++) = 0;
		*(cmd++) =
		    D_TS_LEN(length) | D_TS_CYCLE(cycle) | D_TS_NEXT(nextpipe);
	}

	dbri_cmdsend(dbri, cmd);
}

static void unlink_time_slot(snd_dbri_t * dbri, int pipe,
			     enum in_or_out direction, int prevpipe,
			     int nextpipe)
{
	volatile s32 *cmd;
	int val;

	if (pipe < 0 || pipe > 31 || prevpipe < 0 || prevpipe > 31) {
		printk(KERN_ERR 
		    "DBRI: unlink_time_slot called with illegal pipe number\n");
		return;
	}

	cmd = dbri_cmdlock(dbri, NoGetLock);

	if (direction == PIPEinput) {
		val = D_DTS_VI | D_DTS_DEL | D_DTS_PRVIN(prevpipe) | pipe;
		*(cmd++) = DBRI_CMD(D_DTS, 0, val);
		*(cmd++) = D_TS_NEXT(nextpipe);
		*(cmd++) = 0;
	} else {
		val = D_DTS_VO | D_DTS_DEL | D_DTS_PRVOUT(prevpipe) | pipe;
		*(cmd++) = DBRI_CMD(D_DTS, 0, val);
		*(cmd++) = 0;
		*(cmd++) = D_TS_NEXT(nextpipe);
	}

	dbri_cmdsend(dbri, cmd);
}

/* xmit_fixed() / recv_fixed()
 *
 * Transmit/receive data on a "fixed" pipe - i.e, one whose contents are not
 * expected to change much, and which we don't need to buffer.
 * The DBRI only interrupts us when the data changes (receive pipes),
 * or only changes the data when this function is called (transmit pipes).
 * Only short pipes (numbers 16-31) can be used in fixed data mode.
 *
 * These function operate on a 32-bit field, no matter how large
 * the actual time slot is.  The interrupt handler takes care of bit
 * ordering and alignment.  An 8-bit time slot will always end up
 * in the low-order 8 bits, filled either MSB-first or LSB-first,
 * depending on the settings passed to setup_pipe()
 */
static void xmit_fixed(snd_dbri_t * dbri, int pipe, unsigned int data)
{
	volatile s32 *cmd;

	if (pipe < 16 || pipe > 31) {
		printk(KERN_ERR "DBRI: xmit_fixed: Illegal pipe number\n");
		return;
	}

	if (D_SDP_MODE(dbri->pipes[pipe].sdp) == 0) {
		printk(KERN_ERR "DBRI: xmit_fixed: Uninitialized pipe %d\n", pipe);
		return;
	}

	if (D_SDP_MODE(dbri->pipes[pipe].sdp) != D_SDP_FIXED) {
		printk(KERN_ERR "DBRI: xmit_fixed: Non-fixed pipe %d\n", pipe);
		return;
	}

	if (!(dbri->pipes[pipe].sdp & D_SDP_TO_SER)) {
		printk(KERN_ERR "DBRI: xmit_fixed: Called on receive pipe %d\n", pipe);
		return;
	}

	/* DBRI short pipes always transmit LSB first */

	if (dbri->pipes[pipe].sdp & D_SDP_MSB)
		data = reverse_bytes(data, dbri->pipes[pipe].length);

	cmd = dbri_cmdlock(dbri, GetLock);

	*(cmd++) = DBRI_CMD(D_SSP, 0, pipe);
	*(cmd++) = data;

	dbri_cmdsend(dbri, cmd);
}

static void recv_fixed(snd_dbri_t * dbri, int pipe, volatile __u32 * ptr)
{
	if (pipe < 16 || pipe > 31) {
		printk(KERN_ERR "DBRI: recv_fixed called with illegal pipe number\n");
		return;
	}

	if (D_SDP_MODE(dbri->pipes[pipe].sdp) != D_SDP_FIXED) {
		printk(KERN_ERR "DBRI: recv_fixed called on non-fixed pipe %d\n", pipe);
		return;
	}

	if (dbri->pipes[pipe].sdp & D_SDP_TO_SER) {
		printk(KERN_ERR "DBRI: recv_fixed called on transmit pipe %d\n", pipe);
		return;
	}

	dbri->pipes[pipe].recv_fixed_ptr = ptr;
}

/* setup_descs()
 *
 * Setup transmit/receive data on a "long" pipe - i.e, one associated
 * with a DMA buffer.
 *
 * Only pipe numbers 0-15 can be used in this mode.
 *
 * This function takes a stream number pointing to a data buffer,
 * and work by building chains of descriptors which identify the
 * data buffers.  Buffers too large for a single descriptor will
 * be spread across multiple descriptors.
 */
static int setup_descs(snd_dbri_t * dbri, int streamno, unsigned int period)
{
	dbri_streaminfo_t *info = &dbri->stream_info[streamno];
	__u32 dvma_buffer;
	int desc = 0;
	int len;
	int first_desc = -1;
	int last_desc = -1;

	if (info->pipe < 0 || info->pipe > 15) {
		printk(KERN_ERR "DBRI: setup_descs: Illegal pipe number\n");
		return -2;
	}

	if (dbri->pipes[info->pipe].sdp == 0) {
		printk(KERN_ERR "DBRI: setup_descs: Uninitialized pipe %d\n",
		       info->pipe);
		return -2;
	}

	dvma_buffer = info->dvma_buffer;
	len = info->size;

	if (streamno == DBRI_PLAY) {
		if (!(dbri->pipes[info->pipe].sdp & D_SDP_TO_SER)) {
			printk(KERN_ERR "DBRI: setup_descs: Called on receive pipe %d\n",
			       info->pipe);
			return -2;
		}
	} else {
		if (dbri->pipes[info->pipe].sdp & D_SDP_TO_SER) {
			printk(KERN_ERR 
			    "DBRI: setup_descs: Called on transmit pipe %d\n",
			     info->pipe);
			return -2;
		}
		/* Should be able to queue multiple buffers to receive on a pipe */
		if (pipe_active(dbri, info->pipe)) {
			printk(KERN_ERR "DBRI: recv_on_pipe: Called on active pipe %d\n",
			       info->pipe);
			return -2;
		}

		/* Make sure buffer size is multiple of four */
		len &= ~3;
	}

	while (len > 0) {
		int mylen;

		for (; desc < DBRI_NO_DESCS; desc++) {
			if (!dbri->descs[desc].inuse)
				break;
		}
		if (desc == DBRI_NO_DESCS) {
			printk(KERN_ERR "DBRI: setup_descs: No descriptors\n");
			return -1;
		}

		if (len > DBRI_TD_MAXCNT) {
			mylen = DBRI_TD_MAXCNT;	/* 8KB - 1 */
		} else {
			mylen = len;
		}
		if (mylen > period) {
			mylen = period;
		}

		dbri->descs[desc].inuse = 1;
		dbri->descs[desc].next = -1;
		dbri->dma->desc[desc].ba = dvma_buffer;
		dbri->dma->desc[desc].nda = 0;

		if (streamno == DBRI_PLAY) {
			dbri->descs[desc].len = mylen;
			dbri->dma->desc[desc].word1 = DBRI_TD_CNT(mylen);
			dbri->dma->desc[desc].word4 = 0;
			if (first_desc != -1)
				dbri->dma->desc[desc].word1 |= DBRI_TD_M;
		} else {
			dbri->descs[desc].len = 0;
			dbri->dma->desc[desc].word1 = 0;
			dbri->dma->desc[desc].word4 =
			    DBRI_RD_B | DBRI_RD_BCNT(mylen);
		}

		if (first_desc == -1) {
			first_desc = desc;
		} else {
			dbri->descs[last_desc].next = desc;
			dbri->dma->desc[last_desc].nda =
			    dbri->dma_dvma + dbri_dma_off(desc, desc);
		}

		last_desc = desc;
		dvma_buffer += mylen;
		len -= mylen;
	}

	if (first_desc == -1 || last_desc == -1) {
		printk(KERN_ERR "DBRI: setup_descs: Not enough descriptors available\n");
		return -1;
	}

	dbri->dma->desc[last_desc].word1 &= ~DBRI_TD_M;
	if (streamno == DBRI_PLAY) {
		dbri->dma->desc[last_desc].word1 |=
		    DBRI_TD_I | DBRI_TD_F | DBRI_TD_B;
	}
	dbri->pipes[info->pipe].first_desc = first_desc;
	dbri->pipes[info->pipe].desc = first_desc;

	for (desc = first_desc; desc != -1; desc = dbri->descs[desc].next) {
		dprintk(D_DESC, "DESC %d: %08x %08x %08x %08x\n",
			desc,
			dbri->dma->desc[desc].word1,
			dbri->dma->desc[desc].ba,
			dbri->dma->desc[desc].nda, dbri->dma->desc[desc].word4);
	}
	return 0;
}

/*
****************************************************************************
************************** DBRI - CHI interface ****************************
****************************************************************************

The CHI is a four-wire (clock, frame sync, data in, data out) time-division
multiplexed serial interface which the DBRI can operate in either master
(give clock/frame sync) or slave (take clock/frame sync) mode.

*/

enum master_or_slave { CHImaster, CHIslave };

static void reset_chi(snd_dbri_t * dbri, enum master_or_slave master_or_slave,
		      int bits_per_frame)
{
	volatile s32 *cmd;
	int val;
	static int chi_initialized = 0;	/* FIXME: mutex? */

	if (!chi_initialized) {

		cmd = dbri_cmdlock(dbri, GetLock);

		/* Set CHI Anchor: Pipe 16 */

		val = D_DTS_VI | D_DTS_INS | D_DTS_PRVIN(16) | D_PIPE(16);
		*(cmd++) = DBRI_CMD(D_DTS, 0, val);
		*(cmd++) = D_TS_ANCHOR | D_TS_NEXT(16);
		*(cmd++) = 0;

		val = D_DTS_VO | D_DTS_INS | D_DTS_PRVOUT(16) | D_PIPE(16);
		*(cmd++) = DBRI_CMD(D_DTS, 0, val);
		*(cmd++) = 0;
		*(cmd++) = D_TS_ANCHOR | D_TS_NEXT(16);

		dbri->pipes[16].sdp = 1;
		dbri->pipes[16].nextpipe = 16;
		dbri->chi_in_pipe = 16;
		dbri->chi_out_pipe = 16;

#if 0
		chi_initialized++;
#endif
	} else {
		int pipe;

		for (pipe = dbri->chi_in_pipe;
		     pipe != 16; pipe = dbri->pipes[pipe].nextpipe) {
			unlink_time_slot(dbri, pipe, PIPEinput,
					 16, dbri->pipes[pipe].nextpipe);
		}
		for (pipe = dbri->chi_out_pipe;
		     pipe != 16; pipe = dbri->pipes[pipe].nextpipe) {
			unlink_time_slot(dbri, pipe, PIPEoutput,
					 16, dbri->pipes[pipe].nextpipe);
		}

		dbri->chi_in_pipe = 16;
		dbri->chi_out_pipe = 16;

		cmd = dbri_cmdlock(dbri, GetLock);
	}

	if (master_or_slave == CHIslave) {
		/* Setup DBRI for CHI Slave - receive clock, frame sync (FS)
		 *
		 * CHICM  = 0 (slave mode, 8 kHz frame rate)
		 * IR     = give immediate CHI status interrupt
		 * EN     = give CHI status interrupt upon change
		 */
		*(cmd++) = DBRI_CMD(D_CHI, 0, D_CHI_CHICM(0));
	} else {
		/* Setup DBRI for CHI Master - generate clock, FS
		 *
		 * BPF                          =  bits per 8 kHz frame
		 * 12.288 MHz / CHICM_divisor   = clock rate
		 * FD  =  1 - drive CHIFS on rising edge of CHICK
		 */
		int clockrate = bits_per_frame * 8;
		int divisor = 12288 / clockrate;

		if (divisor > 255 || divisor * clockrate != 12288)
			printk(KERN_ERR "DBRI: illegal bits_per_frame in setup_chi\n");

		*(cmd++) = DBRI_CMD(D_CHI, 0, D_CHI_CHICM(divisor) | D_CHI_FD
				    | D_CHI_BPF(bits_per_frame));
	}

	dbri->chi_bpf = bits_per_frame;

	/* CHI Data Mode
	 *
	 * RCE   =  0 - receive on falling edge of CHICK
	 * XCE   =  1 - transmit on rising edge of CHICK
	 * XEN   =  1 - enable transmitter
	 * REN   =  1 - enable receiver
	 */

	*(cmd++) = DBRI_CMD(D_PAUSE, 0, 0);
	*(cmd++) = DBRI_CMD(D_CDM, 0, D_CDM_XCE | D_CDM_XEN | D_CDM_REN);

	dbri_cmdsend(dbri, cmd);
}

/*
****************************************************************************
*********************** CS4215 audio codec management **********************
****************************************************************************

In the standard SPARC audio configuration, the CS4215 codec is attached
to the DBRI via the CHI interface and few of the DBRI's PIO pins.

*/
static void cs4215_setup_pipes(snd_dbri_t * dbri)
{
	/*
	 * Data mode:
	 * Pipe  4: Send timeslots 1-4 (audio data)
	 * Pipe 20: Send timeslots 5-8 (part of ctrl data)
	 * Pipe  6: Receive timeslots 1-4 (audio data)
	 * Pipe 21: Receive timeslots 6-7. We can only receive 20 bits via
	 *          interrupt, and the rest of the data (slot 5 and 8) is
	 *          not relevant for us (only for doublechecking).
	 *
	 * Control mode:
	 * Pipe 17: Send timeslots 1-4 (slots 5-8 are readonly)
	 * Pipe 18: Receive timeslot 1 (clb).
	 * Pipe 19: Receive timeslot 7 (version). 
	 */

	setup_pipe(dbri, 4, D_SDP_MEM | D_SDP_TO_SER | D_SDP_MSB);
	setup_pipe(dbri, 20, D_SDP_FIXED | D_SDP_TO_SER | D_SDP_MSB);
	setup_pipe(dbri, 6, D_SDP_MEM | D_SDP_FROM_SER | D_SDP_MSB);
	setup_pipe(dbri, 21, D_SDP_FIXED | D_SDP_FROM_SER | D_SDP_MSB);

	setup_pipe(dbri, 17, D_SDP_FIXED | D_SDP_TO_SER | D_SDP_MSB);
	setup_pipe(dbri, 18, D_SDP_FIXED | D_SDP_FROM_SER | D_SDP_MSB);
	setup_pipe(dbri, 19, D_SDP_FIXED | D_SDP_FROM_SER | D_SDP_MSB);
}

static int cs4215_init_data(struct cs4215 *mm)
{
	/*
	 * No action, memory resetting only.
	 *
	 * Data Time Slot 5-8
	 * Speaker,Line and Headphone enable. Gain set to the half.
	 * Input is mike.
	 */
	mm->data[0] = CS4215_LO(0x20) | CS4215_HE | CS4215_LE;
	mm->data[1] = CS4215_RO(0x20) | CS4215_SE;
	mm->data[2] = CS4215_LG(0x8) | CS4215_IS | CS4215_PIO0 | CS4215_PIO1;
	mm->data[3] = CS4215_RG(0x8) | CS4215_MA(0xf);

	/*
	 * Control Time Slot 1-4
	 * 0: Default I/O voltage scale
	 * 1: 8 bit ulaw, 8kHz, mono, high pass filter disabled
	 * 2: Serial enable, CHI master, 128 bits per frame, clock 1
	 * 3: Tests disabled
	 */
	mm->ctrl[0] = CS4215_RSRVD_1 | CS4215_MLB;
	mm->ctrl[1] = CS4215_DFR_ULAW | CS4215_FREQ[0].csval;
	mm->ctrl[2] = CS4215_XCLK | CS4215_BSEL_128 | CS4215_FREQ[0].xtal;
	mm->ctrl[3] = 0;

	mm->status = 0;
	mm->version = 0xff;
	mm->precision = 8;	/* For ULAW */
	mm->channels = 2;

	return 0;
}

static void cs4215_setdata(snd_dbri_t * dbri, int muted)
{
	if (muted) {
		dbri->mm.data[0] |= 63;
		dbri->mm.data[1] |= 63;
		dbri->mm.data[2] &= ~15;
		dbri->mm.data[3] &= ~15;
	} else {
		/* Start by setting the playback attenuation. */
		dbri_streaminfo_t *info = &dbri->stream_info[DBRI_PLAY];
		int left_gain = info->left_gain % 64;
		int right_gain = info->right_gain % 64;

		if (info->balance < DBRI_MID_BALANCE) {
			right_gain *= info->balance;
			right_gain /= DBRI_MID_BALANCE;
		} else {
			left_gain *= DBRI_RIGHT_BALANCE - info->balance;
			left_gain /= DBRI_MID_BALANCE;
		}

		dbri->mm.data[0] &= ~0x3f;	/* Reset the volume bits */
		dbri->mm.data[1] &= ~0x3f;
		dbri->mm.data[0] |= (DBRI_MAX_VOLUME - left_gain);
		dbri->mm.data[1] |= (DBRI_MAX_VOLUME - right_gain);

		/* Now set the recording gain. */
		info = &dbri->stream_info[DBRI_REC];
		left_gain = info->left_gain % 16;
		right_gain = info->right_gain % 16;
		dbri->mm.data[2] |= CS4215_LG(left_gain);
		dbri->mm.data[3] |= CS4215_RG(right_gain);
	}

	xmit_fixed(dbri, 20, *(int *)dbri->mm.data);
}

/*
 * Set the CS4215 to data mode.
 */
static void cs4215_open(snd_dbri_t * dbri)
{
	int data_width;
	u32 tmp;

	dprintk(D_MM, "cs4215_open: %d channels, %d bits\n",
		dbri->mm.channels, dbri->mm.precision);

	/* Temporarily mute outputs, and wait 1/8000 sec (125 us)
	 * to make sure this takes.  This avoids clicking noises.
	 */

	cs4215_setdata(dbri, 1);
	udelay(125);

	/*
	 * Data mode:
	 * Pipe  4: Send timeslots 1-4 (audio data)
	 * Pipe 20: Send timeslots 5-8 (part of ctrl data)
	 * Pipe  6: Receive timeslots 1-4 (audio data)
	 * Pipe 21: Receive timeslots 6-7. We can only receive 20 bits via
	 *          interrupt, and the rest of the data (slot 5 and 8) is
	 *          not relevant for us (only for doublechecking).
	 *
	 * Just like in control mode, the time slots are all offset by eight
	 * bits.  The CS4215, it seems, observes TSIN (the delayed signal)
	 * even if it's the CHI master.  Don't ask me...
	 */
	tmp = sbus_readl(dbri->regs + REG0);
	tmp &= ~(D_C);		/* Disable CHI */
	sbus_writel(tmp, dbri->regs + REG0);

	/* Switch CS4215 to data mode - set PIO3 to 1 */
	sbus_writel(D_ENPIO | D_PIO1 | D_PIO3 |
		    (dbri->mm.onboard ? D_PIO0 : D_PIO2), dbri->regs + REG2);

	reset_chi(dbri, CHIslave, 128);

	/* Note: this next doesn't work for 8-bit stereo, because the two
	 * channels would be on timeslots 1 and 3, with 2 and 4 idle.
	 * (See CS4215 datasheet Fig 15)
	 *
	 * DBRI non-contiguous mode would be required to make this work.
	 */
	data_width = dbri->mm.channels * dbri->mm.precision;

	link_time_slot(dbri, 20, PIPEoutput, 16, 32, dbri->mm.offset + 32);
	link_time_slot(dbri, 4, PIPEoutput, 16, data_width, dbri->mm.offset);
	link_time_slot(dbri, 6, PIPEinput, 16, data_width, dbri->mm.offset);
	link_time_slot(dbri, 21, PIPEinput, 16, 16, dbri->mm.offset + 40);

	/* FIXME: enable CHI after _setdata? */
	tmp = sbus_readl(dbri->regs + REG0);
	tmp |= D_C;		/* Enable CHI */
	sbus_writel(tmp, dbri->regs + REG0);

	cs4215_setdata(dbri, 0);
}

/*
 * Send the control information (i.e. audio format)
 */
static int cs4215_setctrl(snd_dbri_t * dbri)
{
	int i, val;
	u32 tmp;

	/* FIXME - let the CPU do something useful during these delays */

	/* Temporarily mute outputs, and wait 1/8000 sec (125 us)
	 * to make sure this takes.  This avoids clicking noises.
	 */
	cs4215_setdata(dbri, 1);
	udelay(125);

	/*
	 * Enable Control mode: Set DBRI's PIO3 (4215's D/~C) to 0, then wait
	 * 12 cycles <= 12/(5512.5*64) sec = 34.01 usec
	 */
	val = D_ENPIO | D_PIO1 | (dbri->mm.onboard ? D_PIO0 : D_PIO2);
	sbus_writel(val, dbri->regs + REG2);
	dprintk(D_MM, "cs4215_setctrl: reg2=0x%x\n", val);
	udelay(34);

	/* In Control mode, the CS4215 is a slave device, so the DBRI must
	 * operate as CHI master, supplying clocking and frame synchronization.
	 *
	 * In Data mode, however, the CS4215 must be CHI master to insure
	 * that its data stream is synchronous with its codec.
	 *
	 * The upshot of all this?  We start by putting the DBRI into master
	 * mode, program the CS4215 in Control mode, then switch the CS4215
	 * into Data mode and put the DBRI into slave mode.  Various timing
	 * requirements must be observed along the way.
	 *
	 * Oh, and one more thing, on a SPARCStation 20 (and maybe
	 * others?), the addressing of the CS4215's time slots is
	 * offset by eight bits, so we add eight to all the "cycle"
	 * values in the Define Time Slot (DTS) commands.  This is
	 * done in hardware by a TI 248 that delays the DBRI->4215
	 * frame sync signal by eight clock cycles.  Anybody know why?
	 */
	tmp = sbus_readl(dbri->regs + REG0);
	tmp &= ~D_C;		/* Disable CHI */
	sbus_writel(tmp, dbri->regs + REG0);

	reset_chi(dbri, CHImaster, 128);

	/*
	 * Control mode:
	 * Pipe 17: Send timeslots 1-4 (slots 5-8 are readonly)
	 * Pipe 18: Receive timeslot 1 (clb).
	 * Pipe 19: Receive timeslot 7 (version). 
	 */

	link_time_slot(dbri, 17, PIPEoutput, 16, 32, dbri->mm.offset);
	link_time_slot(dbri, 18, PIPEinput, 16, 8, dbri->mm.offset);
	link_time_slot(dbri, 19, PIPEinput, 16, 8, dbri->mm.offset + 48);

	/* Wait for the chip to echo back CLB (Control Latch Bit) as zero */
	dbri->mm.ctrl[0] &= ~CS4215_CLB;
	xmit_fixed(dbri, 17, *(int *)dbri->mm.ctrl);

	tmp = sbus_readl(dbri->regs + REG0);
	tmp |= D_C;		/* Enable CHI */
	sbus_writel(tmp, dbri->regs + REG0);

	for (i = 10; ((dbri->mm.status & 0xe4) != 0x20); --i) {
		msleep_interruptible(1);
	}
	if (i == 0) {
		dprintk(D_MM, "CS4215 didn't respond to CLB (0x%02x)\n",
			dbri->mm.status);
		return -1;
	}

	/* Disable changes to our copy of the version number, as we are about
	 * to leave control mode.
	 */
	recv_fixed(dbri, 19, NULL);

	/* Terminate CS4215 control mode - data sheet says
	 * "Set CLB=1 and send two more frames of valid control info"
	 */
	dbri->mm.ctrl[0] |= CS4215_CLB;
	xmit_fixed(dbri, 17, *(int *)dbri->mm.ctrl);

	/* Two frames of control info @ 8kHz frame rate = 250 us delay */
	udelay(250);

	cs4215_setdata(dbri, 0);

	return 0;
}

/*
 * Setup the codec with the sampling rate, audio format and number of
 * channels.
 * As part of the process we resend the settings for the data
 * timeslots as well.
 */
static int cs4215_prepare(snd_dbri_t * dbri, unsigned int rate,
			  snd_pcm_format_t format, unsigned int channels)
{
	int freq_idx;
	int ret = 0;

	/* Lookup index for this rate */
	for (freq_idx = 0; CS4215_FREQ[freq_idx].freq != 0; freq_idx++) {
		if (CS4215_FREQ[freq_idx].freq == rate)
			break;
	}
	if (CS4215_FREQ[freq_idx].freq != rate) {
		printk(KERN_WARNING "DBRI: Unsupported rate %d Hz\n", rate);
		return -1;
	}

	switch (format) {
	case SNDRV_PCM_FORMAT_MU_LAW:
		dbri->mm.ctrl[1] = CS4215_DFR_ULAW;
		dbri->mm.precision = 8;
		break;
	case SNDRV_PCM_FORMAT_A_LAW:
		dbri->mm.ctrl[1] = CS4215_DFR_ALAW;
		dbri->mm.precision = 8;
		break;
	case SNDRV_PCM_FORMAT_U8:
		dbri->mm.ctrl[1] = CS4215_DFR_LINEAR8;
		dbri->mm.precision = 8;
		break;
	case SNDRV_PCM_FORMAT_S16_BE:
		dbri->mm.ctrl[1] = CS4215_DFR_LINEAR16;
		dbri->mm.precision = 16;
		break;
	default:
		printk(KERN_WARNING "DBRI: Unsupported format %d\n", format);
		return -1;
	}

	/* Add rate parameters */
	dbri->mm.ctrl[1] |= CS4215_FREQ[freq_idx].csval;
	dbri->mm.ctrl[2] = CS4215_XCLK |
	    CS4215_BSEL_128 | CS4215_FREQ[freq_idx].xtal;

	dbri->mm.channels = channels;
	/* Stereo bit: 8 bit stereo not working yet. */
	if ((channels > 1) && (dbri->mm.precision == 16))
		dbri->mm.ctrl[1] |= CS4215_DFR_STEREO;

	ret = cs4215_setctrl(dbri);
	if (ret == 0)
		cs4215_open(dbri);	/* set codec to data mode */

	return ret;
}

/*
 *
 */
static int cs4215_init(snd_dbri_t * dbri)
{
	u32 reg2 = sbus_readl(dbri->regs + REG2);
	dprintk(D_MM, "cs4215_init: reg2=0x%x\n", reg2);

	/* Look for the cs4215 chips */
	if (reg2 & D_PIO2) {
		dprintk(D_MM, "Onboard CS4215 detected\n");
		dbri->mm.onboard = 1;
	}
	if (reg2 & D_PIO0) {
		dprintk(D_MM, "Speakerbox detected\n");
		dbri->mm.onboard = 0;

		if (reg2 & D_PIO2) {
			printk(KERN_INFO "DBRI: Using speakerbox / "
			       "ignoring onboard mmcodec.\n");
			sbus_writel(D_ENPIO2, dbri->regs + REG2);
		}
	}

	if (!(reg2 & (D_PIO0 | D_PIO2))) {
		printk(KERN_ERR "DBRI: no mmcodec found.\n");
		return -EIO;
	}

	cs4215_setup_pipes(dbri);

	cs4215_init_data(&dbri->mm);

	/* Enable capture of the status & version timeslots. */
	recv_fixed(dbri, 18, &dbri->mm.status);
	recv_fixed(dbri, 19, &dbri->mm.version);

	dbri->mm.offset = dbri->mm.onboard ? 0 : 8;
	if (cs4215_setctrl(dbri) == -1 || dbri->mm.version == 0xff) {
		dprintk(D_MM, "CS4215 failed probe at offset %d\n",
			dbri->mm.offset);
		return -EIO;
	}
	dprintk(D_MM, "Found CS4215 at offset %d\n", dbri->mm.offset);

	return 0;
}

/*
****************************************************************************
*************************** DBRI interrupt handler *************************
****************************************************************************

The DBRI communicates with the CPU mainly via a circular interrupt
buffer.  When an interrupt is signaled, the CPU walks through the
buffer and calls dbri_process_one_interrupt() for each interrupt word.
Complicated interrupts are handled by dedicated functions (which
appear first in this file).  Any pending interrupts can be serviced by
calling dbri_process_interrupt_buffer(), which works even if the CPU's
interrupts are disabled.  This function is used by dbri_cmdlock()
to make sure we're synced up with the chip before each command sequence,
even if we're running cli'ed.

*/

/* xmit_descs()
 *
 * Transmit the current TD's for recording/playing, if needed.
 * For playback, ALSA has filled the DMA memory with new data (we hope).
 */
static void xmit_descs(unsigned long data)
{
	snd_dbri_t *dbri = (snd_dbri_t *) data;
	dbri_streaminfo_t *info;
	volatile s32 *cmd;
	unsigned long flags;
	int first_td;

	if (dbri == NULL)
		return;		/* Disabled */

	/* First check the recording stream for buffer overflow */
	info = &dbri->stream_info[DBRI_REC];
	spin_lock_irqsave(&dbri->lock, flags);

	if ((info->left >= info->size) && (info->pipe >= 0)) {
		first_td = dbri->pipes[info->pipe].first_desc;

		dprintk(D_DESC, "xmit_descs rec @ TD %d\n", first_td);

		/* Stream could be closed by the time we run. */
		if (first_td < 0) {
			goto play;
		}

		cmd = dbri_cmdlock(dbri, NoGetLock);
		*(cmd++) = DBRI_CMD(D_SDP, 0,
				    dbri->pipes[info->pipe].sdp
				    | D_SDP_P | D_SDP_EVERY | D_SDP_C);
		*(cmd++) = dbri->dma_dvma + dbri_dma_off(desc, first_td);
		dbri_cmdsend(dbri, cmd);

		/* Reset our admin of the pipe & bytes read. */
		dbri->pipes[info->pipe].desc = first_td;
		info->left = 0;
	}

play:
	spin_unlock_irqrestore(&dbri->lock, flags);

	/* Now check the playback stream for buffer underflow */
	info = &dbri->stream_info[DBRI_PLAY];
	spin_lock_irqsave(&dbri->lock, flags);

	if ((info->left <= 0) && (info->pipe >= 0)) {
		first_td = dbri->pipes[info->pipe].first_desc;

		dprintk(D_DESC, "xmit_descs play @ TD %d\n", first_td);

		/* Stream could be closed by the time we run. */
		if (first_td < 0) {
			spin_unlock_irqrestore(&dbri->lock, flags);
			return;
		}

		cmd = dbri_cmdlock(dbri, NoGetLock);
		*(cmd++) = DBRI_CMD(D_SDP, 0,
				    dbri->pipes[info->pipe].sdp
				    | D_SDP_P | D_SDP_EVERY | D_SDP_C);
		*(cmd++) = dbri->dma_dvma + dbri_dma_off(desc, first_td);
		dbri_cmdsend(dbri, cmd);

		/* Reset our admin of the pipe & bytes written. */
		dbri->pipes[info->pipe].desc = first_td;
		info->left = info->size;
	}
	spin_unlock_irqrestore(&dbri->lock, flags);
}

static DECLARE_TASKLET(xmit_descs_task, xmit_descs, 0);

/* transmission_complete_intr()
 *
 * Called by main interrupt handler when DBRI signals transmission complete
 * on a pipe (interrupt triggered by the B bit in a transmit descriptor).
 *
 * Walks through the pipe's list of transmit buffer descriptors and marks
 * them as available. Stops when the first descriptor is found without
 * TBC (Transmit Buffer Complete) set, or we've run through them all.
 *
 * The DMA buffers are not released, but re-used. Since the transmit buffer
 * descriptors are not clobbered, they can be re-submitted as is. This is
 * done by the xmit_descs() tasklet above since that could take longer.
 */

static void transmission_complete_intr(snd_dbri_t * dbri, int pipe)
{
	dbri_streaminfo_t *info;
	int td;
	int status;

	info = &dbri->stream_info[DBRI_PLAY];

	td = dbri->pipes[pipe].desc;
	while (td >= 0) {
		if (td >= DBRI_NO_DESCS) {
			printk(KERN_ERR "DBRI: invalid td on pipe %d\n", pipe);
			return;
		}

		status = DBRI_TD_STATUS(dbri->dma->desc[td].word4);
		if (!(status & DBRI_TD_TBC)) {
			break;
		}

		dprintk(D_INT, "TD %d, status 0x%02x\n", td, status);

		dbri->dma->desc[td].word4 = 0;	/* Reset it for next time. */
		info->offset += dbri->descs[td].len;
		info->left -= dbri->descs[td].len;

		/* On the last TD, transmit them all again. */
		if (dbri->descs[td].next == -1) {
			if (info->left > 0) {
				printk(KERN_WARNING
				       "%d bytes left after last transfer.\n",
				       info->left);
				info->left = 0;
			}
			tasklet_schedule(&xmit_descs_task);
		}

		td = dbri->descs[td].next;
		dbri->pipes[pipe].desc = td;
	}

	/* Notify ALSA */
	if (spin_is_locked(&dbri->lock)) {
		spin_unlock(&dbri->lock);
		snd_pcm_period_elapsed(info->substream);
		spin_lock(&dbri->lock);
	} else
		snd_pcm_period_elapsed(info->substream);
}

static void reception_complete_intr(snd_dbri_t * dbri, int pipe)
{
	dbri_streaminfo_t *info;
	int rd = dbri->pipes[pipe].desc;
	s32 status;

	if (rd < 0 || rd >= DBRI_NO_DESCS) {
		printk(KERN_ERR "DBRI: invalid rd on pipe %d\n", pipe);
		return;
	}

	dbri->descs[rd].inuse = 0;
	dbri->pipes[pipe].desc = dbri->descs[rd].next;
	status = dbri->dma->desc[rd].word1;
	dbri->dma->desc[rd].word1 = 0;	/* Reset it for next time. */

	info = &dbri->stream_info[DBRI_REC];
	info->offset += DBRI_RD_CNT(status);
	info->left += DBRI_RD_CNT(status);

	/* FIXME: Check status */

	dprintk(D_INT, "Recv RD %d, status 0x%02x, len %d\n",
		rd, DBRI_RD_STATUS(status), DBRI_RD_CNT(status));

	/* On the last TD, transmit them all again. */
	if (dbri->descs[rd].next == -1) {
		if (info->left > info->size) {
			printk(KERN_WARNING
			       "%d bytes recorded in %d size buffer.\n",
			       info->left, info->size);
		}
		tasklet_schedule(&xmit_descs_task);
	}

	/* Notify ALSA */
	if (spin_is_locked(&dbri->lock)) {
		spin_unlock(&dbri->lock);
		snd_pcm_period_elapsed(info->substream);
		spin_lock(&dbri->lock);
	} else
		snd_pcm_period_elapsed(info->substream);
}

static void dbri_process_one_interrupt(snd_dbri_t * dbri, int x)
{
	int val = D_INTR_GETVAL(x);
	int channel = D_INTR_GETCHAN(x);
	int command = D_INTR_GETCMD(x);
	int code = D_INTR_GETCODE(x);
#ifdef DBRI_DEBUG
	int rval = D_INTR_GETRVAL(x);
#endif

	if (channel == D_INTR_CMD) {
		dprintk(D_CMD, "INTR: Command: %-5s  Value:%d\n",
			cmds[command], val);
	} else {
		dprintk(D_INT, "INTR: Chan:%d Code:%d Val:%#x\n",
			channel, code, rval);
	}

	if (channel == D_INTR_CMD && command == D_WAIT) {
		dbri->wait_ackd = val;
		if (dbri->wait_send != val) {
			printk(KERN_ERR "Processing wait command %d when %d was send.\n",
			       val, dbri->wait_send);
		}
		return;
	}

	switch (code) {
	case D_INTR_BRDY:
		reception_complete_intr(dbri, channel);
		break;
	case D_INTR_XCMP:
	case D_INTR_MINT:
		transmission_complete_intr(dbri, channel);
		break;
	case D_INTR_UNDR:
		/* UNDR - Transmission underrun
		 * resend SDP command with clear pipe bit (C) set
		 */
		{
			volatile s32 *cmd;

			int pipe = channel;
			int td = dbri->pipes[pipe].desc;

			dbri->dma->desc[td].word4 = 0;
			cmd = dbri_cmdlock(dbri, NoGetLock);
			*(cmd++) = DBRI_CMD(D_SDP, 0,
					    dbri->pipes[pipe].sdp
					    | D_SDP_P | D_SDP_C | D_SDP_2SAME);
			*(cmd++) = dbri->dma_dvma + dbri_dma_off(desc, td);
			dbri_cmdsend(dbri, cmd);
		}
		break;
	case D_INTR_FXDT:
		/* FXDT - Fixed data change */
		if (dbri->pipes[channel].sdp & D_SDP_MSB)
			val = reverse_bytes(val, dbri->pipes[channel].length);

		if (dbri->pipes[channel].recv_fixed_ptr)
			*(dbri->pipes[channel].recv_fixed_ptr) = val;
		break;
	default:
		if (channel != D_INTR_CMD)
			printk(KERN_WARNING
			       "DBRI: Ignored Interrupt: %d (0x%x)\n", code, x);
	}
}

/* dbri_process_interrupt_buffer advances through the DBRI's interrupt
 * buffer until it finds a zero word (indicating nothing more to do
 * right now).  Non-zero words require processing and are handed off
 * to dbri_process_one_interrupt AFTER advancing the pointer.  This
 * order is important since we might recurse back into this function
 * and need to make sure the pointer has been advanced first.
 */
static void dbri_process_interrupt_buffer(snd_dbri_t * dbri)
{
	s32 x;

	while ((x = dbri->dma->intr[dbri->dbri_irqp]) != 0) {
		dbri->dma->intr[dbri->dbri_irqp] = 0;
		dbri->dbri_irqp++;
		if (dbri->dbri_irqp == (DBRI_NO_INTS * DBRI_INT_BLK))
			dbri->dbri_irqp = 1;
		else if ((dbri->dbri_irqp & (DBRI_INT_BLK - 1)) == 0)
			dbri->dbri_irqp++;

		dbri_process_one_interrupt(dbri, x);
	}
}

static irqreturn_t snd_dbri_interrupt(int irq, void *dev_id,
				      struct pt_regs *regs)
{
	snd_dbri_t *dbri = dev_id;
	static int errcnt = 0;
	int x;

	if (dbri == NULL)
		return IRQ_NONE;
	spin_lock(&dbri->lock);

	/*
	 * Read it, so the interrupt goes away.
	 */
	x = sbus_readl(dbri->regs + REG1);

	if (x & (D_MRR | D_MLE | D_LBG | D_MBE)) {
		u32 tmp;

		if (x & D_MRR)
			printk(KERN_ERR
			       "DBRI: Multiple Error Ack on SBus reg1=0x%x\n",
			       x);
		if (x & D_MLE)
			printk(KERN_ERR
			       "DBRI: Multiple Late Error on SBus reg1=0x%x\n",
			       x);
		if (x & D_LBG)
			printk(KERN_ERR
			       "DBRI: Lost Bus Grant on SBus reg1=0x%x\n", x);
		if (x & D_MBE)
			printk(KERN_ERR
			       "DBRI: Burst Error on SBus reg1=0x%x\n", x);

		/* Some of these SBus errors cause the chip's SBus circuitry
		 * to be disabled, so just re-enable and try to keep going.
		 *
		 * The only one I've seen is MRR, which will be triggered
		 * if you let a transmit pipe underrun, then try to CDP it.
		 *
		 * If these things persist, we reset the chip.
		 */
		if ((++errcnt) % 10 == 0) {
			dprintk(D_INT, "Interrupt errors exceeded.\n");
			dbri_reset(dbri);
		} else {
			tmp = sbus_readl(dbri->regs + REG0);
			tmp &= ~(D_D);
			sbus_writel(tmp, dbri->regs + REG0);
		}
	}

	dbri_process_interrupt_buffer(dbri);

	/* FIXME: Write 0 into regs to ACK interrupt */

	spin_unlock(&dbri->lock);

	return IRQ_HANDLED;
}

/****************************************************************************
		PCM Interface
****************************************************************************/
static snd_pcm_hardware_t snd_dbri_pcm_hw = {
	.info			= (SNDRV_PCM_INFO_MMAP |
				   SNDRV_PCM_INFO_INTERLEAVED |
				   SNDRV_PCM_INFO_BLOCK_TRANSFER |
				   SNDRV_PCM_INFO_MMAP_VALID),
	.formats		= SNDRV_PCM_FMTBIT_MU_LAW |
				  SNDRV_PCM_FMTBIT_A_LAW |
				  SNDRV_PCM_FMTBIT_U8 |
				  SNDRV_PCM_FMTBIT_S16_BE,
	.rates			= SNDRV_PCM_RATE_8000_48000,
	.rate_min		= 8000,
	.rate_max		= 48000,
	.channels_min		= 1,
	.channels_max		= 2,
	.buffer_bytes_max	= (64 * 1024),
	.period_bytes_min	= 1,
	.period_bytes_max	= DBRI_TD_MAXCNT,
	.periods_min		= 1,
	.periods_max		= 1024,
};

static int snd_dbri_open(snd_pcm_substream_t * substream)
{
	snd_dbri_t *dbri = snd_pcm_substream_chip(substream);
	snd_pcm_runtime_t *runtime = substream->runtime;
	dbri_streaminfo_t *info = DBRI_STREAM(dbri, substream);
	unsigned long flags;

	dprintk(D_USR, "open audio output.\n");
	runtime->hw = snd_dbri_pcm_hw;

	spin_lock_irqsave(&dbri->lock, flags);
	info->substream = substream;
	info->left = 0;
	info->offset = 0;
	info->dvma_buffer = 0;
	info->pipe = -1;
	spin_unlock_irqrestore(&dbri->lock, flags);

	cs4215_open(dbri);

	return 0;
}

static int snd_dbri_close(snd_pcm_substream_t * substream)
{
	snd_dbri_t *dbri = snd_pcm_substream_chip(substream);
	dbri_streaminfo_t *info = DBRI_STREAM(dbri, substream);

	dprintk(D_USR, "close audio output.\n");
	info->substream = NULL;
	info->left = 0;
	info->offset = 0;

	return 0;
}

static int snd_dbri_hw_params(snd_pcm_substream_t * substream,
			      snd_pcm_hw_params_t * hw_params)
{
	snd_pcm_runtime_t *runtime = substream->runtime;
	snd_dbri_t *dbri = snd_pcm_substream_chip(substream);
	dbri_streaminfo_t *info = DBRI_STREAM(dbri, substream);
	int direction;
	int ret;

	/* set sampling rate, audio format and number of channels */
	ret = cs4215_prepare(dbri, params_rate(hw_params),
			     params_format(hw_params),
			     params_channels(hw_params));
	if (ret != 0)
		return ret;

	if ((ret = snd_pcm_lib_malloc_pages(substream,
				params_buffer_bytes(hw_params))) < 0) {
		printk(KERN_ERR "malloc_pages failed with %d\n", ret);
		return ret;
	}

	/* hw_params can get called multiple times. Only map the DMA once.
	 */
	if (info->dvma_buffer == 0) {
		if (DBRI_STREAMNO(substream) == DBRI_PLAY)
			direction = SBUS_DMA_TODEVICE;
		else
			direction = SBUS_DMA_FROMDEVICE;

		info->dvma_buffer = sbus_map_single(dbri->sdev,
					runtime->dma_area,
					params_buffer_bytes(hw_params),
					direction);
	}

	direction = params_buffer_bytes(hw_params);
	dprintk(D_USR, "hw_params: %d bytes, dvma=%x\n",
		direction, info->dvma_buffer);
	return 0;
}

static int snd_dbri_hw_free(snd_pcm_substream_t * substream)
{
	snd_dbri_t *dbri = snd_pcm_substream_chip(substream);
	dbri_streaminfo_t *info = DBRI_STREAM(dbri, substream);
	int direction;
	dprintk(D_USR, "hw_free.\n");

	/* hw_free can get called multiple times. Only unmap the DMA once.
	 */
	if (info->dvma_buffer) {
		if (DBRI_STREAMNO(substream) == DBRI_PLAY)
			direction = SBUS_DMA_TODEVICE;
		else
			direction = SBUS_DMA_FROMDEVICE;

		sbus_unmap_single(dbri->sdev, info->dvma_buffer,
				  substream->runtime->buffer_size, direction);
		info->dvma_buffer = 0;
	}
	info->pipe = -1;

	return snd_pcm_lib_free_pages(substream);
}

static int snd_dbri_prepare(snd_pcm_substream_t * substream)
{
	snd_dbri_t *dbri = snd_pcm_substream_chip(substream);
	dbri_streaminfo_t *info = DBRI_STREAM(dbri, substream);
	snd_pcm_runtime_t *runtime = substream->runtime;
	int ret;

	info->size = snd_pcm_lib_buffer_bytes(substream);
	if (DBRI_STREAMNO(substream) == DBRI_PLAY)
		info->pipe = 4;	/* Send pipe */
	else {
		info->pipe = 6;	/* Receive pipe */
		info->left = info->size;	/* To trigger submittal */
	}

	spin_lock_irq(&dbri->lock);

	/* Setup the all the transmit/receive desciptors to cover the
	 * whole DMA buffer.
	 */
	ret = setup_descs(dbri, DBRI_STREAMNO(substream),
			  snd_pcm_lib_period_bytes(substream));

	runtime->stop_threshold = DBRI_TD_MAXCNT / runtime->channels;

	spin_unlock_irq(&dbri->lock);

	dprintk(D_USR, "prepare audio output. %d bytes\n", info->size);
	return ret;
}

static int snd_dbri_trigger(snd_pcm_substream_t * substream, int cmd)
{
	snd_dbri_t *dbri = snd_pcm_substream_chip(substream);
	dbri_streaminfo_t *info = DBRI_STREAM(dbri, substream);
	int ret = 0;

	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
		dprintk(D_USR, "start audio, period is %d bytes\n",
			(int)snd_pcm_lib_period_bytes(substream));
		/* Enable & schedule the tasklet that re-submits the TDs. */
		xmit_descs_task.data = (unsigned long)dbri;
		tasklet_schedule(&xmit_descs_task);
		break;
	case SNDRV_PCM_TRIGGER_STOP:
		dprintk(D_USR, "stop audio.\n");
		/* Make the tasklet bail out immediately. */
		xmit_descs_task.data = 0;
		reset_pipe(dbri, info->pipe);
		break;
	default:
		ret = -EINVAL;
	}

	return ret;
}

static snd_pcm_uframes_t snd_dbri_pointer(snd_pcm_substream_t * substream)
{
	snd_dbri_t *dbri = snd_pcm_substream_chip(substream);
	dbri_streaminfo_t *info = DBRI_STREAM(dbri, substream);
	snd_pcm_uframes_t ret;

	ret = bytes_to_frames(substream->runtime, info->offset)
		% substream->runtime->buffer_size;
	dprintk(D_USR, "I/O pointer: %ld frames, %d bytes left.\n",
		ret, info->left);
	return ret;
}

static snd_pcm_ops_t snd_dbri_ops = {
	.open = snd_dbri_open,
	.close = snd_dbri_close,
	.ioctl = snd_pcm_lib_ioctl,
	.hw_params = snd_dbri_hw_params,
	.hw_free = snd_dbri_hw_free,
	.prepare = snd_dbri_prepare,
	.trigger = snd_dbri_trigger,
	.pointer = snd_dbri_pointer,
};

static int __devinit snd_dbri_pcm(snd_dbri_t * dbri)
{
	snd_pcm_t *pcm;
	int err;

	if ((err = snd_pcm_new(dbri->card,
			       /* ID */		    "sun_dbri",
			       /* device */	    0,
			       /* playback count */ 1,
			       /* capture count */  1, &pcm)) < 0)
		return err;
	snd_assert(pcm != NULL, return -EINVAL);

	snd_pcm_set_ops(pcm, SNDRV_PCM_STREAM_PLAYBACK, &snd_dbri_ops);
	snd_pcm_set_ops(pcm, SNDRV_PCM_STREAM_CAPTURE, &snd_dbri_ops);

	pcm->private_data = dbri;
	pcm->info_flags = 0;
	strcpy(pcm->name, dbri->card->shortname);
	dbri->pcm = pcm;

	if ((err = snd_pcm_lib_preallocate_pages_for_all(pcm,
			SNDRV_DMA_TYPE_CONTINUOUS,
			snd_dma_continuous_data(GFP_KERNEL),
			64 * 1024, 64 * 1024)) < 0) {
		return err;
	}

	return 0;
}

/*****************************************************************************
			Mixer interface
*****************************************************************************/

static int snd_cs4215_info_volume(snd_kcontrol_t * kcontrol,
				  snd_ctl_elem_info_t * uinfo)
{
	uinfo->type = SNDRV_CTL_ELEM_TYPE_INTEGER;
	uinfo->count = 2;
	uinfo->value.integer.min = 0;
	if (kcontrol->private_value == DBRI_PLAY) {
		uinfo->value.integer.max = DBRI_MAX_VOLUME;
	} else {
		uinfo->value.integer.max = DBRI_MAX_GAIN;
	}
	return 0;
}

static int snd_cs4215_get_volume(snd_kcontrol_t * kcontrol,
				 snd_ctl_elem_value_t * ucontrol)
{
	snd_dbri_t *dbri = snd_kcontrol_chip(kcontrol);
	dbri_streaminfo_t *info;
	snd_assert(dbri != NULL, return -EINVAL);
	info = &dbri->stream_info[kcontrol->private_value];
	snd_assert(info != NULL, return -EINVAL);

	ucontrol->value.integer.value[0] = info->left_gain;
	ucontrol->value.integer.value[1] = info->right_gain;
	return 0;
}

static int snd_cs4215_put_volume(snd_kcontrol_t * kcontrol,
				 snd_ctl_elem_value_t * ucontrol)
{
	snd_dbri_t *dbri = snd_kcontrol_chip(kcontrol);
	dbri_streaminfo_t *info = &dbri->stream_info[kcontrol->private_value];
	unsigned long flags;
	int changed = 0;

	if (info->left_gain != ucontrol->value.integer.value[0]) {
		info->left_gain = ucontrol->value.integer.value[0];
		changed = 1;
	}
	if (info->right_gain != ucontrol->value.integer.value[1]) {
		info->right_gain = ucontrol->value.integer.value[1];
		changed = 1;
	}
	if (changed == 1) {
		/* First mute outputs, and wait 1/8000 sec (125 us)
		 * to make sure this takes.  This avoids clicking noises.
		 */
		spin_lock_irqsave(&dbri->lock, flags);

		cs4215_setdata(dbri, 1);
		udelay(125);
		cs4215_setdata(dbri, 0);

		spin_unlock_irqrestore(&dbri->lock, flags);
	}
	return changed;
}

static int snd_cs4215_info_single(snd_kcontrol_t * kcontrol,
				  snd_ctl_elem_info_t * uinfo)
{
	int mask = (kcontrol->private_value >> 16) & 0xff;

	uinfo->type = (mask == 1) ?
	    SNDRV_CTL_ELEM_TYPE_BOOLEAN : SNDRV_CTL_ELEM_TYPE_INTEGER;
	uinfo->count = 1;
	uinfo->value.integer.min = 0;
	uinfo->value.integer.max = mask;
	return 0;
}

static int snd_cs4215_get_single(snd_kcontrol_t * kcontrol,
				 snd_ctl_elem_value_t * ucontrol)
{
	snd_dbri_t *dbri = snd_kcontrol_chip(kcontrol);
	int elem = kcontrol->private_value & 0xff;
	int shift = (kcontrol->private_value >> 8) & 0xff;
	int mask = (kcontrol->private_value >> 16) & 0xff;
	int invert = (kcontrol->private_value >> 24) & 1;
	snd_assert(dbri != NULL, return -EINVAL);

	if (elem < 4) {
		ucontrol->value.integer.value[0] =
		    (dbri->mm.data[elem] >> shift) & mask;
	} else {
		ucontrol->value.integer.value[0] =
		    (dbri->mm.ctrl[elem - 4] >> shift) & mask;
	}

	if (invert == 1) {
		ucontrol->value.integer.value[0] =
		    mask - ucontrol->value.integer.value[0];
	}
	return 0;
}

static int snd_cs4215_put_single(snd_kcontrol_t * kcontrol,
				 snd_ctl_elem_value_t * ucontrol)
{
	snd_dbri_t *dbri = snd_kcontrol_chip(kcontrol);
	unsigned long flags;
	int elem = kcontrol->private_value & 0xff;
	int shift = (kcontrol->private_value >> 8) & 0xff;
	int mask = (kcontrol->private_value >> 16) & 0xff;
	int invert = (kcontrol->private_value >> 24) & 1;
	int changed = 0;
	unsigned short val;
	snd_assert(dbri != NULL, return -EINVAL);

	val = (ucontrol->value.integer.value[0] & mask);
	if (invert == 1)
		val = mask - val;
	val <<= shift;

	if (elem < 4) {
		dbri->mm.data[elem] = (dbri->mm.data[elem] &
				       ~(mask << shift)) | val;
		changed = (val != dbri->mm.data[elem]);
	} else {
		dbri->mm.ctrl[elem - 4] = (dbri->mm.ctrl[elem - 4] &
					   ~(mask << shift)) | val;
		changed = (val != dbri->mm.ctrl[elem - 4]);
	}

	dprintk(D_GEN, "put_single: mask=0x%x, changed=%d, "
		"mixer-value=%ld, mm-value=0x%x\n",
		mask, changed, ucontrol->value.integer.value[0],
		dbri->mm.data[elem & 3]);

	if (changed) {
		/* First mute outputs, and wait 1/8000 sec (125 us)
		 * to make sure this takes.  This avoids clicking noises.
		 */
		spin_lock_irqsave(&dbri->lock, flags);

		cs4215_setdata(dbri, 1);
		udelay(125);
		cs4215_setdata(dbri, 0);

		spin_unlock_irqrestore(&dbri->lock, flags);
	}
	return changed;
}

/* Entries 0-3 map to the 4 data timeslots, entries 4-7 map to the 4 control
   timeslots. Shift is the bit offset in the timeslot, mask defines the
   number of bits. invert is a boolean for use with attenuation.
 */
#define CS4215_SINGLE(xname, entry, shift, mask, invert) \
{ .iface = SNDRV_CTL_ELEM_IFACE_MIXER, .name = xname, \
  .info = snd_cs4215_info_single, \
  .get = snd_cs4215_get_single, .put = snd_cs4215_put_single, \
  .private_value = entry | (shift << 8) | (mask << 16) | (invert << 24) },

static snd_kcontrol_new_t dbri_controls[] __devinitdata = {
	{
	 .iface = SNDRV_CTL_ELEM_IFACE_MIXER,
	 .name  = "Playback Volume",
	 .info  = snd_cs4215_info_volume,
	 .get   = snd_cs4215_get_volume,
	 .put   = snd_cs4215_put_volume,
	 .private_value = DBRI_PLAY,
	 },
	CS4215_SINGLE("Headphone switch", 0, 7, 1, 0)
	CS4215_SINGLE("Line out switch", 0, 6, 1, 0)
	CS4215_SINGLE("Speaker switch", 1, 6, 1, 0)
	{
	 .iface = SNDRV_CTL_ELEM_IFACE_MIXER,
	 .name  = "Capture Volume",
	 .info  = snd_cs4215_info_volume,
	 .get   = snd_cs4215_get_volume,
	 .put   = snd_cs4215_put_volume,
	 .private_value = DBRI_REC,
	 },
	/* FIXME: mic/line switch */
	CS4215_SINGLE("Line in switch", 2, 4, 1, 0)
	CS4215_SINGLE("High Pass Filter switch", 5, 7, 1, 0)
	CS4215_SINGLE("Monitor Volume", 3, 4, 0xf, 1)
	CS4215_SINGLE("Mic boost", 4, 4, 1, 1)
};

#define NUM_CS4215_CONTROLS (sizeof(dbri_controls)/sizeof(snd_kcontrol_new_t))

static int __init snd_dbri_mixer(snd_dbri_t * dbri)
{
	snd_card_t *card;
	int idx, err;

	snd_assert(dbri != NULL && dbri->card != NULL, return -EINVAL);

	card = dbri->card;
	strcpy(card->mixername, card->shortname);

	for (idx = 0; idx < NUM_CS4215_CONTROLS; idx++) {
		if ((err = snd_ctl_add(card,
				snd_ctl_new1(&dbri_controls[idx], dbri))) < 0)
			return err;
	}

	for (idx = DBRI_REC; idx < DBRI_NO_STREAMS; idx++) {
		dbri->stream_info[idx].left_gain = 0;
		dbri->stream_info[idx].right_gain = 0;
		dbri->stream_info[idx].balance = DBRI_MID_BALANCE;
	}

	return 0;
}

/****************************************************************************
			/proc interface
****************************************************************************/
static void dbri_regs_read(snd_info_entry_t * entry, snd_info_buffer_t * buffer)
{
	snd_dbri_t *dbri = entry->private_data;

	snd_iprintf(buffer, "REG0: 0x%x\n", sbus_readl(dbri->regs + REG0));
	snd_iprintf(buffer, "REG2: 0x%x\n", sbus_readl(dbri->regs + REG2));
	snd_iprintf(buffer, "REG8: 0x%x\n", sbus_readl(dbri->regs + REG8));
	snd_iprintf(buffer, "REG9: 0x%x\n", sbus_readl(dbri->regs + REG9));
}

#ifdef DBRI_DEBUG
static void dbri_debug_read(snd_info_entry_t * entry,
			    snd_info_buffer_t * buffer)
{
	snd_dbri_t *dbri = entry->private_data;
	int pipe;
	snd_iprintf(buffer, "debug=%d\n", dbri_debug);

	for (pipe = 0; pipe < 32; pipe++) {
		if (pipe_active(dbri, pipe)) {
			struct dbri_pipe *pptr = &dbri->pipes[pipe];
			snd_iprintf(buffer,
				    "Pipe %d: %s SDP=0x%x desc=%d, "
				    "len=%d @ %d prev: %d next %d\n",
				    pipe,
				    (pptr->direction ==
				     PIPEinput ? "input" : "output"), pptr->sdp,
				    pptr->desc, pptr->length, pptr->cycle,
				    pptr->prevpipe, pptr->nextpipe);
		}
	}
}
#endif

void snd_dbri_proc(snd_dbri_t * dbri)
{
	snd_info_entry_t *entry;
	int err;

	err = snd_card_proc_new(dbri->card, "regs", &entry);
	snd_info_set_text_ops(entry, dbri, 1024, dbri_regs_read);

#ifdef DBRI_DEBUG
	err = snd_card_proc_new(dbri->card, "debug", &entry);
	snd_info_set_text_ops(entry, dbri, 4096, dbri_debug_read);
	entry->mode = S_IFREG | S_IRUGO;	/* Readable only. */
#endif
}

/*
****************************************************************************
**************************** Initialization ********************************
****************************************************************************
*/
static void snd_dbri_free(snd_dbri_t * dbri);

static int __init snd_dbri_create(snd_card_t * card,
				  struct sbus_dev *sdev,
				  struct linux_prom_irqs *irq, int dev)
{
	snd_dbri_t *dbri = card->private_data;
	int err;

	spin_lock_init(&dbri->lock);
	dbri->card = card;
	dbri->sdev = sdev;
	dbri->irq = irq->pri;
	dbri->dbri_version = sdev->prom_name[9];

	dbri->dma = sbus_alloc_consistent(sdev, sizeof(struct dbri_dma),
					  &dbri->dma_dvma);
	memset((void *)dbri->dma, 0, sizeof(struct dbri_dma));

	dprintk(D_GEN, "DMA Cmd Block 0x%p (0x%08x)\n",
		dbri->dma, dbri->dma_dvma);

	/* Map the registers into memory. */
	dbri->regs_size = sdev->reg_addrs[0].reg_size;
	dbri->regs = sbus_ioremap(&sdev->resource[0], 0,
				  dbri->regs_size, "DBRI Registers");
	if (!dbri->regs) {
		printk(KERN_ERR "DBRI: could not allocate registers\n");
		sbus_free_consistent(sdev, sizeof(struct dbri_dma),
				     (void *)dbri->dma, dbri->dma_dvma);
		return -EIO;
	}

	err = request_irq(dbri->irq, snd_dbri_interrupt, SA_SHIRQ,
			  "DBRI audio", dbri);
	if (err) {
		printk(KERN_ERR "DBRI: Can't get irq %d\n", dbri->irq);
		sbus_iounmap(dbri->regs, dbri->regs_size);
		sbus_free_consistent(sdev, sizeof(struct dbri_dma),
				     (void *)dbri->dma, dbri->dma_dvma);
		return err;
	}

	/* Do low level initialization of the DBRI and CS4215 chips */
	dbri_initialize(dbri);
	err = cs4215_init(dbri);
	if (err) {
		snd_dbri_free(dbri);
		return err;
	}

	dbri->next = dbri_list;
	dbri_list = dbri;

	return 0;
}

static void snd_dbri_free(snd_dbri_t * dbri)
{
	dprintk(D_GEN, "snd_dbri_free\n");
	dbri_reset(dbri);

	if (dbri->irq)
		free_irq(dbri->irq, dbri);

	if (dbri->regs)
		sbus_iounmap(dbri->regs, dbri->regs_size);

	if (dbri->dma)
		sbus_free_consistent(dbri->sdev, sizeof(struct dbri_dma),
				     (void *)dbri->dma, dbri->dma_dvma);
}

static int __init dbri_attach(int prom_node, struct sbus_dev *sdev)
{
	snd_dbri_t *dbri;
	struct linux_prom_irqs irq;
	struct resource *rp;
	snd_card_t *card;
	static int dev = 0;
	int err;

	if (sdev->prom_name[9] < 'e') {
		printk(KERN_ERR "DBRI: unsupported chip version %c found.\n",
		       sdev->prom_name[9]);
		return -EIO;
	}

	if (dev >= SNDRV_CARDS)
		return -ENODEV;
	if (!enable[dev]) {
		dev++;
		return -ENOENT;
	}

	err = prom_getproperty(prom_node, "intr", (char *)&irq, sizeof(irq));
	if (err < 0) {
		printk(KERN_ERR "DBRI-%d: Firmware node lacks IRQ property.\n", dev);
		return -ENODEV;
	}

	card = snd_card_new(index[dev], id[dev], THIS_MODULE,
			    sizeof(snd_dbri_t));
	if (card == NULL)
		return -ENOMEM;

	strcpy(card->driver, "DBRI");
	strcpy(card->shortname, "Sun DBRI");
	rp = &sdev->resource[0];
	sprintf(card->longname, "%s at 0x%02lx:0x%08lx, irq %s",
		card->shortname,
		rp->flags & 0xffL, rp->start, __irq_itoa(irq.pri));

	if ((err = snd_dbri_create(card, sdev, &irq, dev)) < 0) {
		snd_card_free(card);
		return err;
	}

	dbri = (snd_dbri_t *) card->private_data;
	if ((err = snd_dbri_pcm(dbri)) < 0)
		goto _err;

	if ((err = snd_dbri_mixer(dbri)) < 0)
		goto _err;

	/* /proc file handling */
	snd_dbri_proc(dbri);

	if ((err = snd_card_set_generic_dev(card)) < 0)
		goto _err;

	if ((err = snd_card_register(card)) < 0)
		goto _err;

	printk(KERN_INFO "audio%d at %p (irq %d) is DBRI(%c)+CS4215(%d)\n",
	       dev, dbri->regs,
	       dbri->irq, dbri->dbri_version, dbri->mm.version);
	dev++;

	return 0;

 _err:
	snd_dbri_free(dbri);
	snd_card_free(card);
	return err;
}

/* Probe for the dbri chip and then attach the driver. */
static int __init dbri_init(void)
{
	struct sbus_bus *sbus;
	struct sbus_dev *sdev;
	int found = 0;

	/* Probe each SBUS for the DBRI chip(s). */
	for_all_sbusdev(sdev, sbus) {
		/*
		 * The version is coded in the last character
		 */
		if (!strncmp(sdev->prom_name, "SUNW,DBRI", 9)) {
			dprintk(D_GEN, "DBRI: Found %s in SBUS slot %d\n",
				sdev->prom_name, sdev->slot);

			if (dbri_attach(sdev->prom_node, sdev) == 0)
				found++;
		}
	}

	return (found > 0) ? 0 : -EIO;
}

static void __exit dbri_exit(void)
{
	snd_dbri_t *this = dbri_list;

	while (this != NULL) {
		snd_dbri_t *next = this->next;
		snd_card_t *card = this->card;

		snd_dbri_free(this);
		snd_card_free(card);
		this = next;
	}
	dbri_list = NULL;
}

module_init(dbri_init);
module_exit(dbri_exit);
