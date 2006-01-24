/*
 	drivers/sound/harmony.c 

	This is a sound driver for ASP's and Lasi's Harmony sound chip
	and is unlikely to be used for anything other than on a HP PA-RISC.

	Harmony is found in HP 712s, 715/new and many other GSC based machines.
	On older 715 machines you'll find the technically identical chip 
	called 'Vivace'. Both Harmony and Vicace are supported by this driver.

	Copyright 2000 (c) Linuxcare Canada, Alex deVries <alex@onefishtwo.ca>
	Copyright 2000-2003 (c) Helge Deller <deller@gmx.de>
	Copyright 2001 (c) Matthieu Delahaye <delahaym@esiee.fr>
	Copyright 2001 (c) Jean-Christophe Vaugeois <vaugeoij@esiee.fr>
	Copyright 2004 (c) Stuart Brady <sdbrady@ntlworld.com>

				
TODO:
	- fix SNDCTL_DSP_GETOSPACE and SNDCTL_DSP_GETISPACE ioctls to
		return the real values
	- add private ioctl for selecting line- or microphone input
		(only one of them is available at the same time)
	- add module parameters
	- implement mmap functionality
	- implement gain meter ?
	- ...
*/

#include <linux/delay.h>
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/ioport.h>
#include <linux/types.h>
#include <linux/mm.h>
#include <linux/pci.h>

#include <asm/parisc-device.h>
#include <asm/io.h>

#include "sound_config.h"


#define PFX "harmony: "
#define HARMONY_VERSION "V0.9a"

#undef DEBUG
#ifdef DEBUG
# define DPRINTK printk 
#else
# define DPRINTK(x,...)
#endif


#define MAX_BUFS 10		/* maximum number of rotating buffers */
#define HARMONY_BUF_SIZE 4096	/* needs to be a multiple of PAGE_SIZE (4096)! */

#define CNTL_C		0x80000000
#define	CNTL_ST		0x00000020
#define CNTL_44100	0x00000015	/* HARMONY_SR_44KHZ */
#define CNTL_8000	0x00000008	/* HARMONY_SR_8KHZ */

#define GAINCTL_HE	0x08000000
#define GAINCTL_LE	0x04000000
#define GAINCTL_SE	0x02000000

#define DSTATUS_PN	0x00000200
#define DSTATUS_RN	0x00000002

#define DSTATUS_IE	0x80000000

#define HARMONY_DF_16BIT_LINEAR	0
#define HARMONY_DF_8BIT_ULAW	1
#define HARMONY_DF_8BIT_ALAW	2

#define HARMONY_SS_MONO		0
#define HARMONY_SS_STEREO	1

#define HARMONY_SR_8KHZ		0x08
#define HARMONY_SR_16KHZ	0x09
#define HARMONY_SR_27KHZ	0x0A
#define HARMONY_SR_32KHZ	0x0B
#define HARMONY_SR_48KHZ	0x0E
#define HARMONY_SR_9KHZ		0x0F
#define HARMONY_SR_5KHZ		0x10
#define HARMONY_SR_11KHZ	0x11
#define HARMONY_SR_18KHZ	0x12
#define HARMONY_SR_22KHZ	0x13
#define HARMONY_SR_37KHZ	0x14
#define HARMONY_SR_44KHZ	0x15
#define HARMONY_SR_33KHZ	0x16
#define HARMONY_SR_6KHZ		0x17

/*
 * Some magics numbers used to auto-detect file formats
 */

#define HARMONY_MAGIC_8B_ULAW	1
#define HARMONY_MAGIC_8B_ALAW	27
#define HARMONY_MAGIC_16B_LINEAR 3
#define HARMONY_MAGIC_MONO	1
#define HARMONY_MAGIC_STEREO	2

/*
 * Channels Positions in mixer register
 */

#define GAIN_HE_SHIFT   27
#define GAIN_HE_MASK    ( 1 << GAIN_HE_SHIFT) 
#define GAIN_LE_SHIFT   26
#define GAIN_LE_MASK    ( 1 << GAIN_LE_SHIFT) 
#define GAIN_SE_SHIFT   25
#define GAIN_SE_MASK    ( 1 << GAIN_SE_SHIFT) 
#define GAIN_IS_SHIFT   24
#define GAIN_IS_MASK    ( 1 << GAIN_IS_SHIFT) 
#define GAIN_MA_SHIFT   20
#define GAIN_MA_MASK    ( 0x0f << GAIN_MA_SHIFT) 
#define GAIN_LI_SHIFT   16
#define GAIN_LI_MASK    ( 0x0f << GAIN_LI_SHIFT) 
#define GAIN_RI_SHIFT   12
#define GAIN_RI_MASK    ( 0x0f << GAIN_RI_SHIFT) 
#define GAIN_LO_SHIFT   6
#define GAIN_LO_MASK    ( 0x3f << GAIN_LO_SHIFT) 
#define GAIN_RO_SHIFT   0
#define GAIN_RO_MASK    ( 0x3f << GAIN_RO_SHIFT) 


#define MAX_OUTPUT_LEVEL  (GAIN_RO_MASK >> GAIN_RO_SHIFT)
#define MAX_INPUT_LEVEL   (GAIN_RI_MASK >> GAIN_RI_SHIFT)
#define MAX_MONITOR_LEVEL (GAIN_MA_MASK >> GAIN_MA_SHIFT)

#define MIXER_INTERNAL   SOUND_MIXER_LINE1
#define MIXER_LINEOUT    SOUND_MIXER_LINE2
#define MIXER_HEADPHONES SOUND_MIXER_LINE3

#define MASK_INTERNAL   SOUND_MASK_LINE1
#define MASK_LINEOUT    SOUND_MASK_LINE2
#define MASK_HEADPHONES SOUND_MASK_LINE3

/*
 * Channels Mask in mixer register
 */

#define GAIN_TOTAL_SILENCE 0x00F00FFF
#define GAIN_DEFAULT       0x0FF00000


struct harmony_hpa {
	u8	unused000;
	u8	id;
	u8	teleshare_id;
	u8	unused003;
	u32	reset;
	u32	cntl;
	u32	gainctl;
	u32	pnxtadd;
	u32	pcuradd;
	u32	rnxtadd;
	u32	rcuradd;
	u32	dstatus;
	u32	ov;
	u32	pio;
	u32	unused02c;
	u32	unused030[3];
	u32	diag;
};

struct harmony_dev {
	struct harmony_hpa *hpa;
	struct parisc_device *dev;
	u32 current_gain;
	u32 dac_rate;		/* 8000 ... 48000 (Hz) */
	u8 data_format;		/* HARMONY_DF_xx_BIT_xxx */
	u8 sample_rate;		/* HARMONY_SR_xx_KHZ */
	u8 stereo_select;	/* HARMONY_SS_MONO or HARMONY_SS_STEREO */
	int format_initialized  :1;
	int suspended_playing   :1;
	int suspended_recording :1;
	
	int blocked_playing     :1;
	int blocked_recording   :1;
	int audio_open		:1;
	int mixer_open		:1;
	
	wait_queue_head_t wq_play, wq_record;
	int first_filled_play;	/* first buffer containing data (next to play) */
	int nb_filled_play; 
	int play_offset;
	int first_filled_record;
	int nb_filled_record;
		
	int dsp_unit, mixer_unit;
};


static struct harmony_dev harmony;


/*
 * Dynamic sound buffer allocation and DMA memory
 */

struct harmony_buffer {
	unsigned char *addr;
	dma_addr_t dma_handle;
	int dma_coherent;	/* Zero if dma_alloc_coherent() fails */
	unsigned int len;
};

/*
 * Harmony memory buffers
 */

static struct harmony_buffer played_buf, recorded_buf, silent, graveyard;


#define CHECK_WBACK_INV_OFFSET(b,offset,len) \
        do { if (!b.dma_coherent) \
		dma_cache_wback_inv((unsigned long)b.addr+offset,len); \
	} while (0) 

	
static int __init harmony_alloc_buffer(struct harmony_buffer *b, 
		unsigned int buffer_count)
{
	b->len = buffer_count * HARMONY_BUF_SIZE;
	b->addr = dma_alloc_coherent(&harmony.dev->dev, 
			  b->len, &b->dma_handle, GFP_KERNEL|GFP_DMA);
	if (b->addr && b->dma_handle) {
		b->dma_coherent = 1;
		DPRINTK(KERN_INFO PFX "coherent memory: 0x%lx, played_buf: 0x%lx\n",
				(unsigned long)b->dma_handle, (unsigned long)b->addr);
	} else {
		b->dma_coherent = 0;
		/* kmalloc()ed memory will HPMC on ccio machines ! */
		b->addr = kmalloc(b->len, GFP_KERNEL);
		if (!b->addr) {
			printk(KERN_ERR PFX "couldn't allocate memory\n");
			return -EBUSY;
		}
		b->dma_handle = __pa(b->addr);
	}
	return 0;
}

static void __exit harmony_free_buffer(struct harmony_buffer *b)
{
	if (!b->addr)
		return;

	if (b->dma_coherent)
		dma_free_coherent(&harmony.dev->dev,
				b->len, b->addr, b->dma_handle);
	else
		kfree(b->addr);

	memset(b, 0, sizeof(*b));
}



/*
 * Low-Level sound-chip programming
 */

static void __inline__ harmony_wait_CNTL(void)
{
	/* Wait until we're out of control mode */
	while (gsc_readl(&harmony.hpa->cntl) & CNTL_C)
		/* wait */ ;
}


static void harmony_update_control(void) 
{
	u32 default_cntl;
	
	/* Set CNTL */
	default_cntl = (CNTL_C |  		/* The C bit */
		(harmony.data_format << 6) |	/* Set the data format */
		(harmony.stereo_select << 5) |	/* Stereo select */
		(harmony.sample_rate));		/* Set sample rate */
	harmony.format_initialized = 1;
	
	/* initialize CNTL */
	gsc_writel(default_cntl, &harmony.hpa->cntl);
}

static void harmony_set_control(u8 data_format, u8 sample_rate, u8 stereo_select) 
{
	harmony.sample_rate = sample_rate;
	harmony.data_format = data_format;
	harmony.stereo_select = stereo_select;
	harmony_update_control();
}

static void harmony_set_rate(u8 data_rate) 
{
	harmony.sample_rate = data_rate;
	harmony_update_control();
}

static int harmony_detect_rate(int *freq)
{
	int newrate;
	switch (*freq) {
	case 8000:	newrate = HARMONY_SR_8KHZ;	break;
	case 16000:	newrate = HARMONY_SR_16KHZ;	break; 
	case 27428:	newrate = HARMONY_SR_27KHZ;	break; 
	case 32000:	newrate = HARMONY_SR_32KHZ;	break; 
	case 48000:	newrate = HARMONY_SR_48KHZ;	break; 
	case 9600:	newrate = HARMONY_SR_9KHZ;	break; 
	case 5512:	newrate = HARMONY_SR_5KHZ;	break; 
	case 11025:	newrate = HARMONY_SR_11KHZ;	break; 
	case 18900:	newrate = HARMONY_SR_18KHZ;	break; 
	case 22050:	newrate = HARMONY_SR_22KHZ;	break; 
	case 37800:	newrate = HARMONY_SR_37KHZ;	break; 
	case 44100:	newrate = HARMONY_SR_44KHZ;	break; 
	case 33075:	newrate = HARMONY_SR_33KHZ;	break; 
	case 6615:	newrate = HARMONY_SR_6KHZ;	break; 
	default:	newrate = HARMONY_SR_8KHZ; 
			*freq = 8000;			break;
	}
	return newrate;
}

static void harmony_set_format(u8 data_format) 
{
	harmony.data_format = data_format;
	harmony_update_control();
}

static void harmony_set_stereo(u8 stereo_select) 
{
	harmony.stereo_select = stereo_select;
	harmony_update_control();
}

static void harmony_disable_interrupts(void) 
{
	harmony_wait_CNTL();
	gsc_writel(0, &harmony.hpa->dstatus); 
}

static void harmony_enable_interrupts(void) 
{
	harmony_wait_CNTL();
	gsc_writel(DSTATUS_IE, &harmony.hpa->dstatus); 
}

/*
 * harmony_silence()
 *
 * This subroutine fills in a buffer starting at location start and
 * silences for length bytes.  This references the current
 * configuration of the audio format.
 *
 */

static void harmony_silence(struct harmony_buffer *buffer, int start, int length) 
{
	u8 silence_char;

	/* Despite what you hear, silence is different in
	   different audio formats.  */
	switch (harmony.data_format) {
		case HARMONY_DF_8BIT_ULAW:	silence_char = 0x55; break;
		case HARMONY_DF_8BIT_ALAW:	silence_char = 0xff; break;
		case HARMONY_DF_16BIT_LINEAR:	/* fall through */
		default:			silence_char = 0;
	}

	memset(buffer->addr+start, silence_char, length);
}


static int harmony_audio_open(struct inode *inode, struct file *file)
{
	if (harmony.audio_open) 
		return -EBUSY;
	
	harmony.audio_open = 1;
	harmony.suspended_playing = harmony.suspended_recording = 1;
	harmony.blocked_playing   = harmony.blocked_recording   = 0;
	harmony.first_filled_play = harmony.first_filled_record = 0;
	harmony.nb_filled_play    = harmony.nb_filled_record    = 0;
	harmony.play_offset = 0;
	init_waitqueue_head(&harmony.wq_play);
	init_waitqueue_head(&harmony.wq_record);
	
	/* Start off in a balanced mode. */
	harmony_set_control(HARMONY_DF_8BIT_ULAW, HARMONY_SR_8KHZ, HARMONY_SS_MONO);
	harmony_update_control();
	harmony.format_initialized = 0;

	/* Clear out all the buffers and flush to cache */
	harmony_silence(&played_buf, 0, HARMONY_BUF_SIZE*MAX_BUFS);
	CHECK_WBACK_INV_OFFSET(played_buf, 0, HARMONY_BUF_SIZE*MAX_BUFS);
	
	return 0;
}

/*
 * Release (close) the audio device.
 */

static int harmony_audio_release(struct inode *inode, struct file *file)
{
	if (!harmony.audio_open) 
		return -EBUSY;
	
	harmony.audio_open = 0;

	return 0;
}

/*
 * Read recorded data off the audio device.
 */

static ssize_t harmony_audio_read(struct file *file,
                                char *buffer,
                                size_t size_count,
                                loff_t *ppos)
{
	int total_count = (int) size_count;
	int count = 0;
	int buf_to_read;

	while (count<total_count) {
		/* Wait until we're out of control mode */
		harmony_wait_CNTL();
		
		/* Figure out which buffer to fill in */
		if (harmony.nb_filled_record <= 2) {
			harmony.blocked_recording = 1;
		        if (harmony.suspended_recording) {
				harmony.suspended_recording = 0;
				harmony_enable_interrupts();
			}
							
			interruptible_sleep_on(&harmony.wq_record);
			harmony.blocked_recording = 0;
		}
		
		if (harmony.nb_filled_record < 2)
			return -EBUSY;
		
		buf_to_read = harmony.first_filled_record;

		/* Copy the page to an aligned buffer */
		if (copy_to_user(buffer+count, recorded_buf.addr +
				 (HARMONY_BUF_SIZE*buf_to_read),
				 HARMONY_BUF_SIZE)) {
			count = -EFAULT;
			break;
		}
		
		harmony.nb_filled_record--;
		harmony.first_filled_record++;
		harmony.first_filled_record %= MAX_BUFS;
				
		count += HARMONY_BUF_SIZE;
	}
	return count;
}




/*
 * Here is the place where we try to recognize file format.
 * Sun/NeXT .au files begin with the string .snd
 * At offset 12 is specified the encoding.
 * At offset 16 is specified speed rate
 * At Offset 20 is specified the numbers of voices
 */

#define four_bytes_to_u32(start) (file_header[start] << 24)|\
                                  (file_header[start+1] << 16)|\
                                  (file_header[start+2] << 8)|\
                                  (file_header[start+3]);

#define test_rate(tested,real_value,harmony_value) if ((tested)<=(real_value))\
                                                    

static int harmony_format_auto_detect(const char *buffer, int block_size)
{
	u8 file_header[24];
	u32 start_string;
	int ret = 0;
	
	if (block_size>24) {
		if (copy_from_user(file_header, buffer, sizeof(file_header)))
			ret = -EFAULT;
			
		start_string = four_bytes_to_u32(0);
		
		if ((file_header[4]==0) && (start_string==0x2E736E64)) {
			u32 format;
			u32 nb_voices;
			u32 speed;
			
			format = four_bytes_to_u32(12);
			nb_voices = four_bytes_to_u32(20);
			speed = four_bytes_to_u32(16);
			
			switch (format) {
			case HARMONY_MAGIC_8B_ULAW:
				harmony.data_format = HARMONY_DF_8BIT_ULAW;
				break;
			case HARMONY_MAGIC_8B_ALAW:
				harmony.data_format = HARMONY_DF_8BIT_ALAW;
				break;
			case HARMONY_MAGIC_16B_LINEAR:
				harmony.data_format = HARMONY_DF_16BIT_LINEAR;
				break;
			default:
				harmony_set_control(HARMONY_DF_16BIT_LINEAR,
						HARMONY_SR_44KHZ, HARMONY_SS_STEREO);
				goto out;
			}
			switch (nb_voices) {
			case HARMONY_MAGIC_MONO:
				harmony.stereo_select = HARMONY_SS_MONO;
				break;
			case HARMONY_MAGIC_STEREO:
				harmony.stereo_select = HARMONY_SS_STEREO;
				break;
			default:
				harmony.stereo_select = HARMONY_SS_MONO;
				break;
			}
			harmony_set_rate(harmony_detect_rate(&speed));
			harmony.dac_rate = speed;
			goto out;
		}
	}
	harmony_set_control(HARMONY_DF_8BIT_ULAW, HARMONY_SR_8KHZ, HARMONY_SS_MONO);
out:
	return ret;
}
#undef four_bytes_to_u32


static ssize_t harmony_audio_write(struct file *file,
                                 const char *buffer,
                                 size_t size_count,
                                 loff_t *ppos)
{
	int total_count = (int) size_count;
	int count = 0;
	int frame_size;
	int buf_to_fill;
	int fresh_buffer;

	if (!harmony.format_initialized) {
		if (harmony_format_auto_detect(buffer, total_count))
			return -EFAULT;
	}
	
	while (count<total_count) {
		/* Wait until we're out of control mode */
		harmony_wait_CNTL();

		/* Figure out which buffer to fill in */
		if (harmony.nb_filled_play+2 >= MAX_BUFS && !harmony.play_offset) {
			harmony.blocked_playing = 1;
			interruptible_sleep_on(&harmony.wq_play);
			harmony.blocked_playing = 0;
		}
		if (harmony.nb_filled_play+2 >= MAX_BUFS && !harmony.play_offset)
			return -EBUSY;
		
		
		buf_to_fill = (harmony.first_filled_play+harmony.nb_filled_play); 
		if (harmony.play_offset) {
			buf_to_fill--;
			buf_to_fill += MAX_BUFS;
		}
		buf_to_fill %= MAX_BUFS;
		
		fresh_buffer = (harmony.play_offset == 0);
		
		/* Figure out the size of the frame */
		if ((total_count-count) >= HARMONY_BUF_SIZE - harmony.play_offset) {
			frame_size = HARMONY_BUF_SIZE - harmony.play_offset;
		} else {
			frame_size = total_count - count;
			/* Clear out the buffer, since there we'll only be 
			   overlaying part of the old buffer with the new one */
			harmony_silence(&played_buf, 
				HARMONY_BUF_SIZE*buf_to_fill+frame_size+harmony.play_offset,
				HARMONY_BUF_SIZE-frame_size-harmony.play_offset);
		}

		/* Copy the page to an aligned buffer */
		if (copy_from_user(played_buf.addr +(HARMONY_BUF_SIZE*buf_to_fill) + harmony.play_offset, 
				   buffer+count, frame_size))
			return -EFAULT;
		CHECK_WBACK_INV_OFFSET(played_buf, (HARMONY_BUF_SIZE*buf_to_fill + harmony.play_offset), 
				frame_size);
	
		if (fresh_buffer)
			harmony.nb_filled_play++;
		
		count += frame_size;
		harmony.play_offset += frame_size;
		harmony.play_offset %= HARMONY_BUF_SIZE;
		if (harmony.suspended_playing && (harmony.nb_filled_play>=4))
			harmony_enable_interrupts();
	}
	
	return count;
}

static unsigned int harmony_audio_poll(struct file *file,
                                     struct poll_table_struct *wait)
{
	unsigned int mask = 0;
	
	if (file->f_mode & FMODE_READ) {
		if (!harmony.suspended_recording)
			poll_wait(file, &harmony.wq_record, wait);
		if (harmony.nb_filled_record)
			mask |= POLLIN | POLLRDNORM;
	}

	if (file->f_mode & FMODE_WRITE) {
		if (!harmony.suspended_playing)
			poll_wait(file, &harmony.wq_play, wait);
		if (harmony.nb_filled_play)
			mask |= POLLOUT | POLLWRNORM;
	}

	return mask;
}

static int harmony_audio_ioctl(struct inode *inode,
                                struct file *file,
				unsigned int cmd,
                                unsigned long arg)
{
	int ival, new_format;
	int frag_size, frag_buf;
	struct audio_buf_info info;
	
	switch (cmd) {
	case OSS_GETVERSION:
		return put_user(SOUND_VERSION, (int *) arg);

	case SNDCTL_DSP_GETCAPS:
		ival = DSP_CAP_DUPLEX;
		return put_user(ival, (int *) arg);

	case SNDCTL_DSP_GETFMTS:
		ival = (AFMT_S16_BE | AFMT_MU_LAW | AFMT_A_LAW ); 
		return put_user(ival, (int *) arg);
	
	case SNDCTL_DSP_SETFMT:
		if (get_user(ival, (int *) arg)) 
			return -EFAULT;
		if (ival != AFMT_QUERY) {
			switch (ival) {
			case AFMT_MU_LAW:	new_format = HARMONY_DF_8BIT_ULAW; break;
			case AFMT_A_LAW:	new_format = HARMONY_DF_8BIT_ALAW; break;
			case AFMT_S16_BE:	new_format = HARMONY_DF_16BIT_LINEAR; break;
			default: {
				DPRINTK(KERN_WARNING PFX 
					"unsupported sound format 0x%04x requested.\n",
					ival);
				ival = AFMT_S16_BE;
				return put_user(ival, (int *) arg);
			}
			}
			harmony_set_format(new_format);
			return 0;
		} else {
			switch (harmony.data_format) {
			case HARMONY_DF_8BIT_ULAW:	ival = AFMT_MU_LAW; break;
			case HARMONY_DF_8BIT_ALAW:	ival = AFMT_A_LAW;  break;
			case HARMONY_DF_16BIT_LINEAR:	ival = AFMT_U16_BE; break;
			default: ival = 0;
			}
			return put_user(ival, (int *) arg);
		}

	case SOUND_PCM_READ_RATE:
		ival = harmony.dac_rate;
		return put_user(ival, (int *) arg);

	case SNDCTL_DSP_SPEED:
		if (get_user(ival, (int *) arg))
			return -EFAULT;
		harmony_set_rate(harmony_detect_rate(&ival));
		harmony.dac_rate = ival;
		return put_user(ival, (int*) arg);

	case SNDCTL_DSP_STEREO:
		if (get_user(ival, (int *) arg))
			return -EFAULT;
		if (ival != 0 && ival != 1)
			return -EINVAL;
		harmony_set_stereo(ival);
 		return 0;
 
 	case SNDCTL_DSP_CHANNELS:
 		if (get_user(ival, (int *) arg))
 			return -EFAULT;
 		if (ival != 1 && ival != 2) {
 			ival = harmony.stereo_select == HARMONY_SS_MONO ? 1 : 2;
 			return put_user(ival, (int *) arg);
 		}
 		harmony_set_stereo(ival-1);
 		return 0;

	case SNDCTL_DSP_GETBLKSIZE:
		ival = HARMONY_BUF_SIZE;
		return put_user(ival, (int *) arg);
		
        case SNDCTL_DSP_NONBLOCK:
                file->f_flags |= O_NONBLOCK;
                return 0;

        case SNDCTL_DSP_RESET:
		if (!harmony.suspended_recording) {
			/* TODO: stop_recording() */
		}
		return 0;

	case SNDCTL_DSP_SETFRAGMENT:
		if (get_user(ival, (int *)arg))
			return -EFAULT;
		frag_size = ival & 0xffff;
		frag_buf = (ival>>16) & 0xffff;
		/* TODO: We use hardcoded fragment sizes and numbers for now */
		frag_size = 12;  /* 4096 == 2^12 */
		frag_buf  = MAX_BUFS;
		ival = (frag_buf << 16) + frag_size;
		return put_user(ival, (int *) arg);
		
	case SNDCTL_DSP_GETOSPACE:
		if (!(file->f_mode & FMODE_WRITE))
			return -EINVAL;
		info.fragstotal = MAX_BUFS;
                info.fragments = MAX_BUFS - harmony.nb_filled_play;
		info.fragsize = HARMONY_BUF_SIZE;
                info.bytes = info.fragments * info.fragsize;
		return copy_to_user((void *)arg, &info, sizeof(info)) ? -EFAULT : 0;

	case SNDCTL_DSP_GETISPACE:
		if (!(file->f_mode & FMODE_READ))
			return -EINVAL;
		info.fragstotal = MAX_BUFS;
                info.fragments = /*MAX_BUFS-*/ harmony.nb_filled_record;
		info.fragsize = HARMONY_BUF_SIZE;
                info.bytes = info.fragments * info.fragsize;
		return copy_to_user((void *)arg, &info, sizeof(info)) ? -EFAULT : 0;
	
	case SNDCTL_DSP_SYNC:
		return 0;
	}
	
	return -EINVAL;
}


/*
 * harmony_interrupt()
 *
 * harmony interruption service routine
 * 
 */

static irqreturn_t harmony_interrupt(int irq, void *dev, struct pt_regs *regs)
{
	u32 dstatus;
	struct harmony_hpa *hpa;

	/* Setup the hpa */
	hpa = ((struct harmony_dev *)dev)->hpa;
	harmony_wait_CNTL();

	/* Read dstatus and pcuradd (the current address) */
	dstatus = gsc_readl(&hpa->dstatus);
	
	/* Turn off interrupts */
	harmony_disable_interrupts();
	
	/* Check if this is a request to get the next play buffer */
	if (dstatus & DSTATUS_PN) {
		if (!harmony.nb_filled_play) {
			harmony.suspended_playing = 1;
			gsc_writel((unsigned long)silent.dma_handle, &hpa->pnxtadd);
						
			if (!harmony.suspended_recording)
				harmony_enable_interrupts();
		} else {
			harmony.suspended_playing = 0;
			gsc_writel((unsigned long)played_buf.dma_handle + 
					(HARMONY_BUF_SIZE*harmony.first_filled_play),
					&hpa->pnxtadd);
			harmony.first_filled_play++;
			harmony.first_filled_play %= MAX_BUFS;
			harmony.nb_filled_play--;
			
		       	harmony_enable_interrupts();
		}
		
		if (harmony.blocked_playing)
			wake_up_interruptible(&harmony.wq_play);
	}
	
	/* Check if we're being asked to fill in a recording buffer */
	if (dstatus & DSTATUS_RN) {
		if((harmony.nb_filled_record+2>=MAX_BUFS) || harmony.suspended_recording)
		{
			harmony.nb_filled_record = 0;
			harmony.first_filled_record = 0;
			harmony.suspended_recording = 1;
			gsc_writel((unsigned long)graveyard.dma_handle, &hpa->rnxtadd);
			if (!harmony.suspended_playing)
				harmony_enable_interrupts();
		} else {
			int buf_to_fill;
			buf_to_fill = (harmony.first_filled_record+harmony.nb_filled_record) % MAX_BUFS;
			CHECK_WBACK_INV_OFFSET(recorded_buf, HARMONY_BUF_SIZE*buf_to_fill, HARMONY_BUF_SIZE);
			gsc_writel((unsigned long)recorded_buf.dma_handle +
					HARMONY_BUF_SIZE*buf_to_fill,
					&hpa->rnxtadd);
			harmony.nb_filled_record++;
			harmony_enable_interrupts();
		}

		if (harmony.blocked_recording && harmony.nb_filled_record>3)
			wake_up_interruptible(&harmony.wq_record);
	}
	return IRQ_HANDLED;
}

/*
 * Sound playing functions
 */

static struct file_operations harmony_audio_fops = {
	.owner		= THIS_MODULE,
	.llseek		= no_llseek,
	.read		= harmony_audio_read,
	.write		= harmony_audio_write,
	.poll		= harmony_audio_poll,
	.ioctl		= harmony_audio_ioctl,
	.open		= harmony_audio_open,
	.release	= harmony_audio_release,
};

static int harmony_audio_init(void)
{
	/* Request that IRQ */
	if (request_irq(harmony.dev->irq, harmony_interrupt, 0 ,"harmony", &harmony)) {
		printk(KERN_ERR PFX "Error requesting irq %d.\n", harmony.dev->irq);
		return -EFAULT;
	}

   	harmony.dsp_unit = register_sound_dsp(&harmony_audio_fops, -1);
	if (harmony.dsp_unit < 0) {
		printk(KERN_ERR PFX "Error registering dsp\n");
		free_irq(harmony.dev->irq, &harmony);
		return -EFAULT;
	}
	
	/* Clear the buffers so you don't end up with crap in the buffers. */ 
	harmony_silence(&played_buf, 0, HARMONY_BUF_SIZE*MAX_BUFS);

	/* Make sure this makes it to cache */
	CHECK_WBACK_INV_OFFSET(played_buf, 0, HARMONY_BUF_SIZE*MAX_BUFS);

	/* Clear out the silent buffer and flush to cache */
	harmony_silence(&silent, 0, HARMONY_BUF_SIZE);
	CHECK_WBACK_INV_OFFSET(silent, 0, HARMONY_BUF_SIZE);
	
	harmony.audio_open = 0;
	
	return 0;
}


/*
 * mixer functions 
 */

static void harmony_mixer_set_gain(void)
{
	harmony_wait_CNTL();
	gsc_writel(harmony.current_gain, &harmony.hpa->gainctl);
}

/* 
 *  Read gain of selected channel.
 *  The OSS rate is from 0 (silent) to 100 -> need some conversions
 *
 *  The harmony gain are attenuation for output and monitor gain.
 *                   is amplifaction for input gain
 */
#define to_harmony_level(level,max) ((level)*max/100)
#define to_oss_level(level,max) ((level)*100/max)

static int harmony_mixer_get_level(int channel)
{
	int left_level;
	int right_level;

	switch (channel) {
		case SOUND_MIXER_VOLUME:
			left_level  = (harmony.current_gain & GAIN_LO_MASK) >> GAIN_LO_SHIFT;
			right_level = (harmony.current_gain & GAIN_RO_MASK) >> GAIN_RO_SHIFT;
			left_level  = to_oss_level(MAX_OUTPUT_LEVEL - left_level, MAX_OUTPUT_LEVEL);
			right_level = to_oss_level(MAX_OUTPUT_LEVEL - right_level, MAX_OUTPUT_LEVEL);
			return (right_level << 8)+left_level;
			
		case SOUND_MIXER_IGAIN:
			left_level = (harmony.current_gain & GAIN_LI_MASK) >> GAIN_LI_SHIFT;
			right_level= (harmony.current_gain & GAIN_RI_MASK) >> GAIN_RI_SHIFT;
			left_level = to_oss_level(left_level, MAX_INPUT_LEVEL);
			right_level= to_oss_level(right_level, MAX_INPUT_LEVEL);
			return (right_level << 8)+left_level;
			
		case SOUND_MIXER_MONITOR:
			left_level = (harmony.current_gain & GAIN_MA_MASK) >> GAIN_MA_SHIFT;
			left_level = to_oss_level(MAX_MONITOR_LEVEL-left_level, MAX_MONITOR_LEVEL);
			return (left_level << 8)+left_level;
	}
	return -EINVAL;
}



/*
 * Some conversions for the same reasons.
 * We give back the new real value(s) due to
 * the rescale.
 */

static int harmony_mixer_set_level(int channel, int value)
{
	int left_level;
	int right_level;
	int new_left_level;
	int new_right_level;

	right_level = (value & 0x0000ff00) >> 8;
	left_level = value & 0x000000ff;
	if (right_level > 100) right_level = 100;
	if (left_level > 100) left_level = 100;
  
	switch (channel) {
		case SOUND_MIXER_VOLUME:
			right_level = to_harmony_level(100-right_level, MAX_OUTPUT_LEVEL);
			left_level  = to_harmony_level(100-left_level, MAX_OUTPUT_LEVEL);
			new_right_level = to_oss_level(MAX_OUTPUT_LEVEL - right_level, MAX_OUTPUT_LEVEL);
			new_left_level  = to_oss_level(MAX_OUTPUT_LEVEL - left_level, MAX_OUTPUT_LEVEL);
			harmony.current_gain = (harmony.current_gain & ~(GAIN_LO_MASK | GAIN_RO_MASK)) 
					| (left_level << GAIN_LO_SHIFT) | (right_level << GAIN_RO_SHIFT);
			harmony_mixer_set_gain();
			return (new_right_level << 8) + new_left_level;
			
		case SOUND_MIXER_IGAIN:
			right_level = to_harmony_level(right_level, MAX_INPUT_LEVEL);
			left_level  = to_harmony_level(left_level, MAX_INPUT_LEVEL);
			new_right_level = to_oss_level(right_level, MAX_INPUT_LEVEL);
			new_left_level  = to_oss_level(left_level, MAX_INPUT_LEVEL);
			harmony.current_gain = (harmony.current_gain & ~(GAIN_LI_MASK | GAIN_RI_MASK))
					| (left_level << GAIN_LI_SHIFT) | (right_level << GAIN_RI_SHIFT);
			harmony_mixer_set_gain();
			return (new_right_level << 8) + new_left_level;
	
		case SOUND_MIXER_MONITOR:
			left_level = to_harmony_level(100-left_level, MAX_MONITOR_LEVEL);
			new_left_level = to_oss_level(MAX_MONITOR_LEVEL-left_level, MAX_MONITOR_LEVEL);
			harmony.current_gain = (harmony.current_gain & ~GAIN_MA_MASK) | (left_level << GAIN_MA_SHIFT);
			harmony_mixer_set_gain();
			return (new_left_level << 8) + new_left_level;
	}

	return -EINVAL;
}

#undef to_harmony_level
#undef to_oss_level

/* 
 * Return the selected input device (mic or line)
 */

static int harmony_mixer_get_recmask(void) 
{
	int current_input_line;
	
	current_input_line = (harmony.current_gain & GAIN_IS_MASK) 
				    >> GAIN_IS_SHIFT;
	if (current_input_line) 
		return SOUND_MASK_MIC;

	return SOUND_MASK_LINE;
}

/*
 * Set the input (only one at time, arbitrary priority to line in)
 */

static int harmony_mixer_set_recmask(int recmask)
{
	int new_input_line;
	int new_input_mask;
	int current_input_line;
	
	current_input_line = (harmony.current_gain & GAIN_IS_MASK)
				    >> GAIN_IS_SHIFT;
	if ((current_input_line && ((recmask & SOUND_MASK_LINE) || !(recmask & SOUND_MASK_MIC))) ||
		(!current_input_line && ((recmask & SOUND_MASK_LINE) && !(recmask & SOUND_MASK_MIC)))) {
		new_input_line = 0;
		new_input_mask = SOUND_MASK_LINE;
	} else {
		new_input_line = 1;
		new_input_mask = SOUND_MASK_MIC;
	}
	harmony.current_gain = ((harmony.current_gain & ~GAIN_IS_MASK) | 
				(new_input_line << GAIN_IS_SHIFT ));
	harmony_mixer_set_gain();
	return new_input_mask;
}


/* 
 * give the active outlines
 */

static int harmony_mixer_get_outmask(void)
{
	int outmask = 0;
	
	if (harmony.current_gain & GAIN_SE_MASK) outmask |= MASK_INTERNAL;
	if (harmony.current_gain & GAIN_LE_MASK) outmask |= MASK_LINEOUT;
	if (harmony.current_gain & GAIN_HE_MASK) outmask |= MASK_HEADPHONES;
	
	return outmask;
}


static int harmony_mixer_set_outmask(int outmask)
{
	if (outmask & MASK_INTERNAL) 
		harmony.current_gain |= GAIN_SE_MASK;
	else 
		harmony.current_gain &= ~GAIN_SE_MASK;
	
	if (outmask & MASK_LINEOUT) 
		harmony.current_gain |= GAIN_LE_MASK;
	else 
		harmony.current_gain &= ~GAIN_LE_MASK;
	
	if (outmask & MASK_HEADPHONES) 
		harmony.current_gain |= GAIN_HE_MASK; 
	else 
		harmony.current_gain &= ~GAIN_HE_MASK;
	
	harmony_mixer_set_gain();

	return (outmask & (MASK_INTERNAL | MASK_LINEOUT | MASK_HEADPHONES));
}

/*
 * This code is inspired from sb_mixer.c
 */

static int harmony_mixer_ioctl(struct inode * inode, struct file * file,
		unsigned int cmd, unsigned long arg)
{
	int val;
	int ret;

	if (cmd == SOUND_MIXER_INFO) {
		mixer_info info;
		memset(&info, 0, sizeof(info));
                strncpy(info.id, "harmony", sizeof(info.id)-1);
                strncpy(info.name, "Harmony audio", sizeof(info.name)-1);
                info.modify_counter = 1; /* ? */
                if (copy_to_user((void *)arg, &info, sizeof(info)))
                        return -EFAULT;
		return 0;
	}
	
	if (cmd == OSS_GETVERSION)
		return put_user(SOUND_VERSION, (int *)arg);

	/* read */
	val = 0;
	if (_SIOC_DIR(cmd) & _SIOC_WRITE)
		if (get_user(val, (int *)arg))
			return -EFAULT;

	switch (cmd) {
	case MIXER_READ(SOUND_MIXER_CAPS):
		ret = SOUND_CAP_EXCL_INPUT;
		break;
	case MIXER_READ(SOUND_MIXER_STEREODEVS):
		ret = SOUND_MASK_VOLUME | SOUND_MASK_IGAIN;
		break;
		
	case MIXER_READ(SOUND_MIXER_RECMASK):
		ret = SOUND_MASK_MIC | SOUND_MASK_LINE;
		break;
	case MIXER_READ(SOUND_MIXER_DEVMASK):
		ret = SOUND_MASK_VOLUME | SOUND_MASK_IGAIN |
			SOUND_MASK_MONITOR;
		break;
	case MIXER_READ(SOUND_MIXER_OUTMASK):
		ret = MASK_INTERNAL | MASK_LINEOUT |
			MASK_HEADPHONES;
		break;
		
	case MIXER_WRITE(SOUND_MIXER_RECSRC):
		ret = harmony_mixer_set_recmask(val);
		break;
	case MIXER_READ(SOUND_MIXER_RECSRC):
		ret = harmony_mixer_get_recmask();
		break;
	      
	case MIXER_WRITE(SOUND_MIXER_OUTSRC):
		ret = harmony_mixer_set_outmask(val);
		break;
	case MIXER_READ(SOUND_MIXER_OUTSRC):
		ret = harmony_mixer_get_outmask();
		break;
	
	case MIXER_WRITE(SOUND_MIXER_VOLUME):
	case MIXER_WRITE(SOUND_MIXER_IGAIN):
	case MIXER_WRITE(SOUND_MIXER_MONITOR):
		ret = harmony_mixer_set_level(cmd & 0xff, val);
		break;

	case MIXER_READ(SOUND_MIXER_VOLUME):
	case MIXER_READ(SOUND_MIXER_IGAIN):
	case MIXER_READ(SOUND_MIXER_MONITOR):
		ret = harmony_mixer_get_level(cmd & 0xff);
		break;

	default:
		return -EINVAL;
	}

	if (put_user(ret, (int *)arg))
		return -EFAULT;
	return 0;
}


static int harmony_mixer_open(struct inode *inode, struct file *file)
{
	if (harmony.mixer_open) 
		return -EBUSY;
	harmony.mixer_open = 1;
	return 0;
}

static int harmony_mixer_release(struct inode *inode, struct file *file)
{
	if (!harmony.mixer_open) 
		return -EBUSY;
	harmony.mixer_open = 0;
	return 0;
}

static struct file_operations harmony_mixer_fops = {
	.owner		= THIS_MODULE,
	.llseek		= no_llseek,
	.open		= harmony_mixer_open,
	.release	= harmony_mixer_release,
	.ioctl		= harmony_mixer_ioctl,
};


/*
 * Mute all the output and reset Harmony.
 */

static void __init harmony_mixer_reset(void)
{
	harmony.current_gain = GAIN_TOTAL_SILENCE;
	harmony_mixer_set_gain();
	harmony_wait_CNTL();
	gsc_writel(1, &harmony.hpa->reset);
	mdelay(50);		/* wait 50 ms */
	gsc_writel(0, &harmony.hpa->reset);
	harmony.current_gain = GAIN_DEFAULT;
	harmony_mixer_set_gain();
}

static int __init harmony_mixer_init(void)
{
	/* Register the device file operations */
	harmony.mixer_unit = register_sound_mixer(&harmony_mixer_fops, -1);
	if (harmony.mixer_unit < 0) {
		printk(KERN_WARNING PFX "Error Registering Mixer Driver\n");
		return -EFAULT;
	}
  
	harmony_mixer_reset();
	harmony.mixer_open = 0;
	
	return 0;
}



/* 
 * This is the callback that's called by the inventory hardware code 
 * if it finds a match to the registered driver. 
 */
static int __devinit
harmony_driver_probe(struct parisc_device *dev)
{
	u8	id;
	u8	rev;
	u32	cntl;
	int	ret;

	if (harmony.hpa) {
		/* We only support one Harmony at this time */
		printk(KERN_ERR PFX "driver already registered\n");
		return -EBUSY;
	}

	if (!dev->irq) {
		printk(KERN_ERR PFX "no irq found\n");
		return -ENODEV;
	}

	/* Set the HPA of harmony */
	harmony.hpa = (struct harmony_hpa *)dev->hpa.start;
	harmony.dev = dev;

	/* Grab the ID and revision from the device */
	id = gsc_readb(&harmony.hpa->id);
	if ((id | 1) != 0x15) {
		printk(KERN_WARNING PFX "wrong harmony id 0x%02x\n", id);
		return -EBUSY;
	}
	cntl = gsc_readl(&harmony.hpa->cntl);
	rev = (cntl>>20) & 0xff;

	printk(KERN_INFO "Lasi Harmony Audio driver " HARMONY_VERSION ", "
			"h/w id %i, rev. %i at 0x%lx, IRQ %i\n",
			id, rev, dev->hpa.start, harmony.dev->irq);
	
	/* Make sure the control bit isn't set, although I don't think it 
	   ever is. */
	if (cntl & CNTL_C) {
		printk(KERN_WARNING PFX "CNTL busy\n");
		harmony.hpa = 0;
		return -EBUSY;
	}

	/* Initialize the memory buffers */
	if (harmony_alloc_buffer(&played_buf, MAX_BUFS) || 
	    harmony_alloc_buffer(&recorded_buf, MAX_BUFS) ||
	    harmony_alloc_buffer(&graveyard, 1) ||
	    harmony_alloc_buffer(&silent, 1)) {
		ret = -EBUSY;
		goto out_err;
	}

	/* Initialize /dev/mixer and /dev/audio  */
	if ((ret=harmony_mixer_init())) 
		goto out_err;
	if ((ret=harmony_audio_init())) 
		goto out_err;

	return 0;

out_err:
	harmony.hpa = 0;
	harmony_free_buffer(&played_buf);
	harmony_free_buffer(&recorded_buf);
	harmony_free_buffer(&graveyard);
	harmony_free_buffer(&silent);
	return ret;
}


static struct parisc_device_id harmony_tbl[] = {
 /* { HPHW_FIO, HVERSION_REV_ANY_ID, HVERSION_ANY_ID, 0x0007A }, Bushmaster/Flounder */
 { HPHW_FIO, HVERSION_REV_ANY_ID, HVERSION_ANY_ID, 0x0007B }, /* 712/715 Audio */
 { HPHW_FIO, HVERSION_REV_ANY_ID, HVERSION_ANY_ID, 0x0007E }, /* Pace Audio */
 { HPHW_FIO, HVERSION_REV_ANY_ID, HVERSION_ANY_ID, 0x0007F }, /* Outfield / Coral II */
 { 0, }
};

MODULE_DEVICE_TABLE(parisc, harmony_tbl);

static struct parisc_driver harmony_driver = {
	.name		= "Lasi Harmony",
	.id_table	= harmony_tbl,
	.probe		= harmony_driver_probe,
};

static int __init init_harmony(void)
{
	return register_parisc_driver(&harmony_driver);
}

static void __exit cleanup_harmony(void)
{
	free_irq(harmony.dev->irq, &harmony);
	unregister_sound_mixer(harmony.mixer_unit);
	unregister_sound_dsp(harmony.dsp_unit);
	harmony_free_buffer(&played_buf);
	harmony_free_buffer(&recorded_buf);
	harmony_free_buffer(&graveyard);
	harmony_free_buffer(&silent);
	unregister_parisc_driver(&harmony_driver);
}


MODULE_AUTHOR("Alex DeVries <alex@onefishtwo.ca>");
MODULE_DESCRIPTION("Harmony sound driver");
MODULE_LICENSE("GPL");

module_init(init_harmony);
module_exit(cleanup_harmony);

