/*
    btaudio - bt878 audio dma driver for linux 2.4.x

    (c) 2000-2002 Gerd Knorr <kraxel@bytesex.org>

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.

*/

#include <linux/module.h>
#include <linux/errno.h>
#include <linux/pci.h>
#include <linux/sched.h>
#include <linux/signal.h>
#include <linux/types.h>
#include <linux/interrupt.h>
#include <linux/init.h>
#include <linux/poll.h>
#include <linux/sound.h>
#include <linux/soundcard.h>
#include <linux/slab.h>
#include <linux/kdev_t.h>
#include <linux/mutex.h>

#include <asm/uaccess.h>
#include <asm/io.h>


/* mmio access */
#define btwrite(dat,adr)    writel((dat), (bta->mmio+(adr)))
#define btread(adr)         readl(bta->mmio+(adr))

#define btand(dat,adr)      btwrite((dat) & btread(adr), adr)
#define btor(dat,adr)       btwrite((dat) | btread(adr), adr)
#define btaor(dat,mask,adr) btwrite((dat) | ((mask) & btread(adr)), adr)

/* registers (shifted because bta->mmio is long) */
#define REG_INT_STAT      (0x100 >> 2)
#define REG_INT_MASK      (0x104 >> 2)
#define REG_GPIO_DMA_CTL  (0x10c >> 2)
#define REG_PACKET_LEN    (0x110 >> 2)
#define REG_RISC_STRT_ADD (0x114 >> 2)
#define REG_RISC_COUNT    (0x120 >> 2)

/* IRQ bits - REG_INT_(STAT|MASK) */
#define IRQ_SCERR         (1 << 19)
#define IRQ_OCERR         (1 << 18)
#define IRQ_PABORT        (1 << 17)
#define IRQ_RIPERR        (1 << 16)
#define IRQ_PPERR         (1 << 15)
#define IRQ_FDSR          (1 << 14)
#define IRQ_FTRGT         (1 << 13)
#define IRQ_FBUS          (1 << 12)
#define IRQ_RISCI         (1 << 11)
#define IRQ_OFLOW         (1 <<  3)

#define IRQ_BTAUDIO       (IRQ_SCERR | IRQ_OCERR | IRQ_PABORT | IRQ_RIPERR |\
			   IRQ_PPERR | IRQ_FDSR  | IRQ_FTRGT  | IRQ_FBUS   |\
			   IRQ_RISCI)

/* REG_GPIO_DMA_CTL bits */
#define DMA_CTL_A_PWRDN   (1 << 26)
#define DMA_CTL_DA_SBR    (1 << 14)
#define DMA_CTL_DA_ES2    (1 << 13)
#define DMA_CTL_ACAP_EN   (1 <<  4)
#define DMA_CTL_RISC_EN   (1 <<  1)
#define DMA_CTL_FIFO_EN   (1 <<  0)

/* RISC instructions */
#define RISC_WRITE        (0x01 << 28)
#define RISC_JUMP         (0x07 << 28)
#define RISC_SYNC         (0x08 << 28)

/* RISC bits */
#define RISC_WR_SOL       (1 << 27)
#define RISC_WR_EOL       (1 << 26)
#define RISC_IRQ          (1 << 24)
#define RISC_SYNC_RESYNC  (1 << 15)
#define RISC_SYNC_FM1     0x06
#define RISC_SYNC_VRO     0x0c

#define HWBASE_AD (448000)

/* -------------------------------------------------------------- */

struct btaudio {
	/* linked list */
	struct btaudio *next;

	/* device info */
	int            dsp_digital;
	int            dsp_analog;
	int            mixer_dev;
	struct pci_dev *pci;
	unsigned int   irq;
	unsigned long  mem;
	unsigned long  __iomem *mmio;

	/* locking */
	int            users;
	struct mutex lock;

	/* risc instructions */
	unsigned int   risc_size;
	unsigned long  *risc_cpu;
	dma_addr_t     risc_dma;

	/* audio data */
	unsigned int   buf_size;
	unsigned char  *buf_cpu;
	dma_addr_t     buf_dma;

	/* buffer setup */
	int line_bytes;
	int line_count;
	int block_bytes;
	int block_count;

	/* read fifo management */
	int recording;
	int dma_block;
	int read_offset;
	int read_count;
	wait_queue_head_t readq;

	/* settings */
	int gain[3];
	int source;
	int bits;
	int decimation;
	int mixcount;
	int sampleshift;
	int channels;
	int analog;
	int rate;
};

struct cardinfo {
	char *name;
	int rate;
};

static struct btaudio *btaudios;
static unsigned int debug;
static unsigned int irq_debug;

/* -------------------------------------------------------------- */

#define BUF_DEFAULT 128*1024
#define BUF_MIN         8192

static int alloc_buffer(struct btaudio *bta)
{
	if (NULL == bta->buf_cpu) {
		for (bta->buf_size = BUF_DEFAULT; bta->buf_size >= BUF_MIN;
		     bta->buf_size = bta->buf_size >> 1) {
			bta->buf_cpu = pci_alloc_consistent
				(bta->pci, bta->buf_size, &bta->buf_dma);
			if (NULL != bta->buf_cpu)
				break;
		}
		if (NULL == bta->buf_cpu)
			return -ENOMEM;
		memset(bta->buf_cpu,0,bta->buf_size);
	}
	if (NULL == bta->risc_cpu) {
		bta->risc_size = PAGE_SIZE;
		bta->risc_cpu = pci_alloc_consistent
			(bta->pci, bta->risc_size, &bta->risc_dma);
		if (NULL == bta->risc_cpu) {
			pci_free_consistent(bta->pci, bta->buf_size, bta->buf_cpu, bta->buf_dma);
			bta->buf_cpu = NULL;
			return -ENOMEM;
		}
	}
	return 0;
}

static void free_buffer(struct btaudio *bta)
{
	if (NULL != bta->buf_cpu) {
		pci_free_consistent(bta->pci, bta->buf_size,
				    bta->buf_cpu, bta->buf_dma);
		bta->buf_cpu = NULL;
	}
	if (NULL != bta->risc_cpu) {
		pci_free_consistent(bta->pci, bta->risc_size,
				    bta->risc_cpu, bta->risc_dma);
		bta->risc_cpu = NULL;
	}
}

static int make_risc(struct btaudio *bta)
{
	int rp, bp, line, block;
	unsigned long risc;

	bta->block_bytes = bta->buf_size >> 4;
	bta->block_count = 1 << 4;
	bta->line_bytes  = bta->block_bytes;
	bta->line_count  = bta->block_count;
	while (bta->line_bytes > 4095) {
		bta->line_bytes >>= 1;
		bta->line_count <<= 1;
	}
	if (bta->line_count > 255)
		return -EINVAL;
	if (debug)
		printk(KERN_DEBUG
		       "btaudio: bufsize=%d - bs=%d bc=%d - ls=%d, lc=%d\n",
		       bta->buf_size,bta->block_bytes,bta->block_count,
		       bta->line_bytes,bta->line_count);
        rp = 0; bp = 0;
	block = 0;
	bta->risc_cpu[rp++] = cpu_to_le32(RISC_SYNC|RISC_SYNC_FM1);
	bta->risc_cpu[rp++] = cpu_to_le32(0);
	for (line = 0; line < bta->line_count; line++) {
		risc  = RISC_WRITE | RISC_WR_SOL | RISC_WR_EOL;
		risc |= bta->line_bytes;
		if (0 == (bp & (bta->block_bytes-1))) {
			risc |= RISC_IRQ;
			risc |= (block  & 0x0f) << 16;
			risc |= (~block & 0x0f) << 20;
			block++;
		}
		bta->risc_cpu[rp++] = cpu_to_le32(risc);
		bta->risc_cpu[rp++] = cpu_to_le32(bta->buf_dma + bp);
		bp += bta->line_bytes;
	}
	bta->risc_cpu[rp++] = cpu_to_le32(RISC_SYNC|RISC_SYNC_VRO);
	bta->risc_cpu[rp++] = cpu_to_le32(0);
	bta->risc_cpu[rp++] = cpu_to_le32(RISC_JUMP); 
	bta->risc_cpu[rp++] = cpu_to_le32(bta->risc_dma);
	return 0;
}

static int start_recording(struct btaudio *bta)
{
	int ret;

	if (0 != (ret = alloc_buffer(bta)))
		return ret;
	if (0 != (ret = make_risc(bta)))
		return ret;

	btwrite(bta->risc_dma, REG_RISC_STRT_ADD);
	btwrite((bta->line_count << 16) | bta->line_bytes,
		REG_PACKET_LEN);
	btwrite(IRQ_BTAUDIO, REG_INT_MASK);
	if (bta->analog) {
		btwrite(DMA_CTL_ACAP_EN |
			DMA_CTL_RISC_EN |
			DMA_CTL_FIFO_EN |
			DMA_CTL_DA_ES2  |
			((bta->bits == 8) ? DMA_CTL_DA_SBR : 0) |
			(bta->gain[bta->source] << 28) |
			(bta->source            << 24) |
			(bta->decimation        <<  8),
			REG_GPIO_DMA_CTL);
	} else {
		btwrite(DMA_CTL_ACAP_EN |
			DMA_CTL_RISC_EN |
			DMA_CTL_FIFO_EN |
			DMA_CTL_DA_ES2  |
			DMA_CTL_A_PWRDN |
			(1 << 6)   |
			((bta->bits == 8) ? DMA_CTL_DA_SBR : 0) |
			(bta->gain[bta->source] << 28) |
			(bta->source            << 24) |
			(bta->decimation        <<  8),
			REG_GPIO_DMA_CTL);
	}
	bta->dma_block = 0;
	bta->read_offset = 0;
	bta->read_count = 0;
	bta->recording = 1;
	if (debug)
		printk(KERN_DEBUG "btaudio: recording started\n");
	return 0;
}

static void stop_recording(struct btaudio *bta)
{
        btand(~15, REG_GPIO_DMA_CTL);
	bta->recording = 0;
	if (debug)
		printk(KERN_DEBUG "btaudio: recording stopped\n");
}


/* -------------------------------------------------------------- */

static int btaudio_mixer_open(struct inode *inode, struct file *file)
{
	int minor = iminor(inode);
	struct btaudio *bta;

	for (bta = btaudios; bta != NULL; bta = bta->next)
		if (bta->mixer_dev == minor)
			break;
	if (NULL == bta)
		return -ENODEV;

	if (debug)
		printk("btaudio: open mixer [%d]\n",minor);
	file->private_data = bta;
	return 0;
}

static int btaudio_mixer_release(struct inode *inode, struct file *file)
{
	return 0;
}

static int btaudio_mixer_ioctl(struct inode *inode, struct file *file,
			       unsigned int cmd, unsigned long arg)
{
	struct btaudio *bta = file->private_data;
	int ret,val=0,i=0;
	void __user *argp = (void __user *)arg;

	if (cmd == SOUND_MIXER_INFO) {
		mixer_info info;
		memset(&info,0,sizeof(info));
                strlcpy(info.id,"bt878",sizeof(info.id));
                strlcpy(info.name,"Brooktree Bt878 audio",sizeof(info.name));
                info.modify_counter = bta->mixcount;
                if (copy_to_user(argp, &info, sizeof(info)))
                        return -EFAULT;
		return 0;
	}
	if (cmd == SOUND_OLD_MIXER_INFO) {
		_old_mixer_info info;
		memset(&info,0,sizeof(info));
                strlcpy(info.id,"bt878",sizeof(info.id)-1);
                strlcpy(info.name,"Brooktree Bt878 audio",sizeof(info.name));
                if (copy_to_user(argp, &info, sizeof(info)))
                        return -EFAULT;
		return 0;
	}
	if (cmd == OSS_GETVERSION)
		return put_user(SOUND_VERSION, (int __user *)argp);

	/* read */
	if (_SIOC_DIR(cmd) & _SIOC_WRITE)
		if (get_user(val, (int __user *)argp))
			return -EFAULT;

	switch (cmd) {
	case MIXER_READ(SOUND_MIXER_CAPS):
		ret = SOUND_CAP_EXCL_INPUT;
		break;
	case MIXER_READ(SOUND_MIXER_STEREODEVS):
		ret = 0;
		break;
	case MIXER_READ(SOUND_MIXER_RECMASK):
	case MIXER_READ(SOUND_MIXER_DEVMASK):
		ret = SOUND_MASK_LINE1|SOUND_MASK_LINE2|SOUND_MASK_LINE3;
		break;

	case MIXER_WRITE(SOUND_MIXER_RECSRC):
		if (val & SOUND_MASK_LINE1 && bta->source != 0)
			bta->source = 0;
		else if (val & SOUND_MASK_LINE2 && bta->source != 1)
			bta->source = 1;
		else if (val & SOUND_MASK_LINE3 && bta->source != 2)
			bta->source = 2;
		btaor((bta->gain[bta->source] << 28) |
		      (bta->source            << 24),
		      0x0cffffff, REG_GPIO_DMA_CTL);
	case MIXER_READ(SOUND_MIXER_RECSRC):
		switch (bta->source) {
		case 0:  ret = SOUND_MASK_LINE1; break;
		case 1:  ret = SOUND_MASK_LINE2; break;
		case 2:  ret = SOUND_MASK_LINE3; break;
		default: ret = 0;
		}
		break;

	case MIXER_WRITE(SOUND_MIXER_LINE1):
	case MIXER_WRITE(SOUND_MIXER_LINE2):
	case MIXER_WRITE(SOUND_MIXER_LINE3):
		if (MIXER_WRITE(SOUND_MIXER_LINE1) == cmd)
			i = 0;
		if (MIXER_WRITE(SOUND_MIXER_LINE2) == cmd)
			i = 1;
		if (MIXER_WRITE(SOUND_MIXER_LINE3) == cmd)
			i = 2;
		bta->gain[i] = (val & 0xff) * 15 / 100;
		if (bta->gain[i] > 15) bta->gain[i] = 15;
		if (bta->gain[i] <  0) bta->gain[i] =  0;
		if (i == bta->source)
			btaor((bta->gain[bta->source]<<28),
			      0x0fffffff, REG_GPIO_DMA_CTL);
		ret  = bta->gain[i] * 100 / 15;
		ret |= ret << 8;
		break;

	case MIXER_READ(SOUND_MIXER_LINE1):
	case MIXER_READ(SOUND_MIXER_LINE2):
	case MIXER_READ(SOUND_MIXER_LINE3):
		if (MIXER_READ(SOUND_MIXER_LINE1) == cmd)
			i = 0;
		if (MIXER_READ(SOUND_MIXER_LINE2) == cmd)
			i = 1;
		if (MIXER_READ(SOUND_MIXER_LINE3) == cmd)
			i = 2;
		ret  = bta->gain[i] * 100 / 15;
		ret |= ret << 8;
		break;

	default:
		return -EINVAL;
	}
	if (put_user(ret, (int __user *)argp))
		return -EFAULT;
	return 0;
}

static struct file_operations btaudio_mixer_fops = {
	.owner		= THIS_MODULE,
	.llseek		= no_llseek,
	.open		= btaudio_mixer_open,
	.release	= btaudio_mixer_release,
	.ioctl		= btaudio_mixer_ioctl,
};

/* -------------------------------------------------------------- */

static int btaudio_dsp_open(struct inode *inode, struct file *file,
			    struct btaudio *bta, int analog)
{
	mutex_lock(&bta->lock);
	if (bta->users)
		goto busy;
	bta->users++;
	file->private_data = bta;

	bta->analog = analog;
	bta->dma_block = 0;
	bta->read_offset = 0;
	bta->read_count = 0;
	bta->sampleshift = 0;

	mutex_unlock(&bta->lock);
	return 0;

 busy:
	mutex_unlock(&bta->lock);
	return -EBUSY;
}

static int btaudio_dsp_open_digital(struct inode *inode, struct file *file)
{
	int minor = iminor(inode);
	struct btaudio *bta;

	for (bta = btaudios; bta != NULL; bta = bta->next)
		if (bta->dsp_digital == minor)
			break;
	if (NULL == bta)
		return -ENODEV;
	
	if (debug)
		printk("btaudio: open digital dsp [%d]\n",minor);
	return btaudio_dsp_open(inode,file,bta,0);
}

static int btaudio_dsp_open_analog(struct inode *inode, struct file *file)
{
	int minor = iminor(inode);
	struct btaudio *bta;

	for (bta = btaudios; bta != NULL; bta = bta->next)
		if (bta->dsp_analog == minor)
			break;
	if (NULL == bta)
		return -ENODEV;

	if (debug)
		printk("btaudio: open analog dsp [%d]\n",minor);
	return btaudio_dsp_open(inode,file,bta,1);
}

static int btaudio_dsp_release(struct inode *inode, struct file *file)
{
	struct btaudio *bta = file->private_data;

	mutex_lock(&bta->lock);
	if (bta->recording)
		stop_recording(bta);
	bta->users--;
	mutex_unlock(&bta->lock);
	return 0;
}

static ssize_t btaudio_dsp_read(struct file *file, char __user *buffer,
				size_t swcount, loff_t *ppos)
{
	struct btaudio *bta = file->private_data;
	int hwcount = swcount << bta->sampleshift;
	int nsrc, ndst, err, ret = 0;
	DECLARE_WAITQUEUE(wait, current);

	add_wait_queue(&bta->readq, &wait);
	mutex_lock(&bta->lock);
	while (swcount > 0) {
		if (0 == bta->read_count) {
			if (!bta->recording) {
				if (0 != (err = start_recording(bta))) {
					if (0 == ret)
						ret = err;
					break;
				}
			}
			if (file->f_flags & O_NONBLOCK) {
				if (0 == ret)
					ret = -EAGAIN;
				break;
			}
			mutex_unlock(&bta->lock);
			current->state = TASK_INTERRUPTIBLE;
			schedule();
			mutex_lock(&bta->lock);
			if(signal_pending(current)) {
				if (0 == ret)
					ret = -EINTR;
				break;
			}
		}
		nsrc = (bta->read_count < hwcount) ? bta->read_count : hwcount;
		if (nsrc > bta->buf_size - bta->read_offset)
			nsrc = bta->buf_size - bta->read_offset;
		ndst = nsrc >> bta->sampleshift;
		
		if ((bta->analog  && 0 == bta->sampleshift) ||
		    (!bta->analog && 2 == bta->channels)) {
			/* just copy */
			if (copy_to_user(buffer + ret, bta->buf_cpu + bta->read_offset, nsrc)) {
				if (0 == ret)
					ret = -EFAULT;
				break;
			}

		} else if (!bta->analog) {
			/* stereo => mono (digital audio) */
			__s16 *src = (__s16*)(bta->buf_cpu + bta->read_offset);
			__s16 __user *dst = (__s16 __user *)(buffer + ret);
			__s16 avg;
			int n = ndst>>1;
			if (!access_ok(VERIFY_WRITE, dst, ndst)) {
				if (0 == ret)
					ret = -EFAULT;
				break;
			}
			for (; n; n--, dst++) {
				avg  = (__s16)le16_to_cpu(*src) / 2; src++;
				avg += (__s16)le16_to_cpu(*src) / 2; src++;
				__put_user(cpu_to_le16(avg),dst);
			}

		} else if (8 == bta->bits) {
			/* copy + byte downsampling (audio A/D) */
			__u8 *src = bta->buf_cpu + bta->read_offset;
			__u8 __user *dst = buffer + ret;
			int n = ndst;
			if (!access_ok(VERIFY_WRITE, dst, ndst)) {
				if (0 == ret)
					ret = -EFAULT;
				break;
			}
			for (; n; n--, src += (1 << bta->sampleshift), dst++)
				__put_user(*src, dst);

		} else {
			/* copy + word downsampling (audio A/D) */
			__u16 *src = (__u16*)(bta->buf_cpu + bta->read_offset);
			__u16 __user *dst = (__u16 __user *)(buffer + ret);
			int n = ndst>>1;
			if (!access_ok(VERIFY_WRITE,dst,ndst)) {
				if (0 == ret)
					ret = -EFAULT;
				break;
			}
			for (; n; n--, src += (1 << bta->sampleshift), dst++)
				__put_user(*src, dst);
		}

		ret     += ndst;
		swcount -= ndst;
		hwcount -= nsrc;
		bta->read_count  -= nsrc;
		bta->read_offset += nsrc;
		if (bta->read_offset == bta->buf_size)
			bta->read_offset = 0;
	}
	mutex_unlock(&bta->lock);
	remove_wait_queue(&bta->readq, &wait);
	current->state = TASK_RUNNING;
	return ret;
}

static ssize_t btaudio_dsp_write(struct file *file, const char __user *buffer,
				 size_t count, loff_t *ppos)
{
	return -EINVAL;
}

static int btaudio_dsp_ioctl(struct inode *inode, struct file *file,
			     unsigned int cmd, unsigned long arg)
{
	struct btaudio *bta = file->private_data;
	int s, i, ret, val = 0;
	void __user *argp = (void __user *)arg;
	int __user *p = argp;
	
        switch (cmd) {
        case OSS_GETVERSION:
                return put_user(SOUND_VERSION, p);
        case SNDCTL_DSP_GETCAPS:
		return 0;

        case SNDCTL_DSP_SPEED:
		if (get_user(val, p))
			return -EFAULT;
		if (bta->analog) {
			for (s = 0; s < 16; s++)
				if (val << s >= HWBASE_AD*4/15)
					break;
			for (i = 15; i >= 5; i--)
				if (val << s <= HWBASE_AD*4/i)
					break;
			bta->sampleshift = s;
			bta->decimation  = i;
			if (debug)
				printk(KERN_DEBUG "btaudio: rate: req=%d  "
				       "dec=%d shift=%d hwrate=%d swrate=%d\n",
				       val,i,s,(HWBASE_AD*4/i),(HWBASE_AD*4/i)>>s);
		} else {
			bta->sampleshift = (bta->channels == 2) ? 0 : 1;
			bta->decimation  = 0;
		}
		if (bta->recording) {
			mutex_lock(&bta->lock);
			stop_recording(bta);
			start_recording(bta);
			mutex_unlock(&bta->lock);
		}
		/* fall through */
        case SOUND_PCM_READ_RATE:
		if (bta->analog) {
			return put_user(HWBASE_AD*4/bta->decimation>>bta->sampleshift, p);
		} else {
			return put_user(bta->rate, p);
		}

        case SNDCTL_DSP_STEREO:
		if (!bta->analog) {
			if (get_user(val, p))
				return -EFAULT;
			bta->channels    = (val > 0) ? 2 : 1;
			bta->sampleshift = (bta->channels == 2) ? 0 : 1;
			if (debug)
				printk(KERN_INFO
				       "btaudio: stereo=%d channels=%d\n",
				       val,bta->channels);
		} else {
			if (val == 1)
				return -EFAULT;
			else {
				bta->channels = 1;
				if (debug)
					printk(KERN_INFO
					       "btaudio: stereo=0 channels=1\n");
			}
		}
		return put_user((bta->channels)-1, p);

        case SNDCTL_DSP_CHANNELS:
		if (!bta->analog) {
			if (get_user(val, p))
				return -EFAULT;
			bta->channels    = (val > 1) ? 2 : 1;
			bta->sampleshift = (bta->channels == 2) ? 0 : 1;
			if (debug)
				printk(KERN_DEBUG
				       "btaudio: val=%d channels=%d\n",
				       val,bta->channels);
		}
		/* fall through */
        case SOUND_PCM_READ_CHANNELS:
		return put_user(bta->channels, p);
		
        case SNDCTL_DSP_GETFMTS: /* Returns a mask */
		if (bta->analog)
			return put_user(AFMT_S16_LE|AFMT_S8, p);
		else
			return put_user(AFMT_S16_LE, p);

        case SNDCTL_DSP_SETFMT: /* Selects ONE fmt*/
		if (get_user(val, p))
			return -EFAULT;
                if (val != AFMT_QUERY) {
			if (bta->analog)
				bta->bits = (val == AFMT_S8) ? 8 : 16;
			else
				bta->bits = 16;
			if (bta->recording) {
				mutex_lock(&bta->lock);
				stop_recording(bta);
				start_recording(bta);
				mutex_unlock(&bta->lock);
			}
		}
		if (debug)
			printk(KERN_DEBUG "btaudio: fmt: bits=%d\n",bta->bits);
                return put_user((bta->bits==16) ? AFMT_S16_LE : AFMT_S8,
				p);
		break;
        case SOUND_PCM_READ_BITS:
		return put_user(bta->bits, p);

        case SNDCTL_DSP_NONBLOCK:
                file->f_flags |= O_NONBLOCK;
                return 0;

        case SNDCTL_DSP_RESET:
		if (bta->recording) {
			mutex_lock(&bta->lock);
			stop_recording(bta);
			mutex_unlock(&bta->lock);
		}
		return 0;
        case SNDCTL_DSP_GETBLKSIZE:
		if (!bta->recording) {
			if (0 != (ret = alloc_buffer(bta)))
				return ret;
			if (0 != (ret = make_risc(bta)))
				return ret;
		}
		return put_user(bta->block_bytes>>bta->sampleshift,p);

        case SNDCTL_DSP_SYNC:
		/* NOP */
		return 0;
	case SNDCTL_DSP_GETISPACE:
	{
		audio_buf_info info;
		if (!bta->recording)
			return -EINVAL;
		info.fragsize = bta->block_bytes>>bta->sampleshift;
		info.fragstotal = bta->block_count;
		info.bytes = bta->read_count;
		info.fragments = info.bytes / info.fragsize;
		if (debug)
			printk(KERN_DEBUG "btaudio: SNDCTL_DSP_GETISPACE "
			       "returns %d/%d/%d/%d\n",
			       info.fragsize, info.fragstotal,
			       info.bytes, info.fragments);
		if (copy_to_user(argp, &info, sizeof(info)))
			return -EFAULT;
		return 0;
	}
#if 0 /* TODO */
        case SNDCTL_DSP_GETTRIGGER:
        case SNDCTL_DSP_SETTRIGGER:
        case SNDCTL_DSP_SETFRAGMENT:
#endif
	default:
		return -EINVAL;
	}
}

static unsigned int btaudio_dsp_poll(struct file *file, struct poll_table_struct *wait)
{
	struct btaudio *bta = file->private_data;
	unsigned int mask = 0;

	poll_wait(file, &bta->readq, wait);

	if (0 != bta->read_count)
		mask |= (POLLIN | POLLRDNORM);

	return mask;
}

static struct file_operations btaudio_digital_dsp_fops = {
	.owner		= THIS_MODULE,
	.llseek		= no_llseek,
	.open		= btaudio_dsp_open_digital,
	.release	= btaudio_dsp_release,
	.read		= btaudio_dsp_read,
	.write		= btaudio_dsp_write,
	.ioctl		= btaudio_dsp_ioctl,
	.poll		= btaudio_dsp_poll,
};

static struct file_operations btaudio_analog_dsp_fops = {
	.owner		= THIS_MODULE,
	.llseek		= no_llseek,
	.open		= btaudio_dsp_open_analog,
	.release	= btaudio_dsp_release,
	.read		= btaudio_dsp_read,
	.write		= btaudio_dsp_write,
	.ioctl		= btaudio_dsp_ioctl,
	.poll		= btaudio_dsp_poll,
};

/* -------------------------------------------------------------- */

static char *irq_name[] = { "", "", "", "OFLOW", "", "", "", "", "", "", "",
			    "RISCI", "FBUS", "FTRGT", "FDSR", "PPERR",
			    "RIPERR", "PABORT", "OCERR", "SCERR" };

static irqreturn_t btaudio_irq(int irq, void *dev_id)
{
	int count = 0;
	u32 stat,astat;
	struct btaudio *bta = dev_id;
	int handled = 0;

	for (;;) {
		count++;
		stat  = btread(REG_INT_STAT);
		astat = stat & btread(REG_INT_MASK);
		if (!astat)
			return IRQ_RETVAL(handled);
		handled = 1;
		btwrite(astat,REG_INT_STAT);

		if (irq_debug) {
			int i;
			printk(KERN_DEBUG "btaudio: irq loop=%d risc=%x, bits:",
			       count, stat>>28);
			for (i = 0; i < (sizeof(irq_name)/sizeof(char*)); i++) {
				if (stat & (1 << i))
					printk(" %s",irq_name[i]);
				if (astat & (1 << i))
					printk("*");
			}
			printk("\n");
		}
		if (stat & IRQ_RISCI) {
			int blocks;
			blocks = (stat >> 28) - bta->dma_block;
			if (blocks < 0)
				blocks += bta->block_count;
			bta->dma_block = stat >> 28;
			if (bta->read_count + 2*bta->block_bytes > bta->buf_size) {
				stop_recording(bta);
				printk(KERN_INFO "btaudio: buffer overrun\n");
			}
			if (blocks > 0) {
				bta->read_count += blocks * bta->block_bytes;
				wake_up_interruptible(&bta->readq);
			}
		}
		if (count > 10) {
			printk(KERN_WARNING
			       "btaudio: Oops - irq mask cleared\n");
			btwrite(0, REG_INT_MASK);
		}
	}
	return IRQ_NONE;
}

/* -------------------------------------------------------------- */

static unsigned int dsp1 = -1;
static unsigned int dsp2 = -1;
static unsigned int mixer = -1;
static int latency = -1;
static int digital = 1;
static int analog = 1;
static int rate;

#define BTA_OSPREY200 1

static struct cardinfo cards[] = {
	[0] = {
		.name	= "default",
		.rate	= 32000,
	},
	[BTA_OSPREY200] = {
		.name	= "Osprey 200",
		.rate	= 44100,
	},
};

static int __devinit btaudio_probe(struct pci_dev *pci_dev,
				   const struct pci_device_id *pci_id)
{
	struct btaudio *bta;
	struct cardinfo *card = &cards[pci_id->driver_data];
	unsigned char revision,lat;
	int rc = -EBUSY;

	if (pci_enable_device(pci_dev))
		return -EIO;
	if (!request_mem_region(pci_resource_start(pci_dev,0),
				pci_resource_len(pci_dev,0),
				"btaudio")) {
		return -EBUSY;
	}

	bta = kmalloc(sizeof(*bta),GFP_ATOMIC);
	if (!bta) {
		rc = -ENOMEM;
		goto fail0;
	}
	memset(bta,0,sizeof(*bta));

	bta->pci  = pci_dev;
	bta->irq  = pci_dev->irq;
	bta->mem  = pci_resource_start(pci_dev,0);
	bta->mmio = ioremap(pci_resource_start(pci_dev,0),
			    pci_resource_len(pci_dev,0));

	bta->source     = 1;
	bta->bits       = 8;
	bta->channels   = 1;
	if (bta->analog) {
		bta->decimation  = 15;
	} else {
		bta->decimation  = 0;
		bta->sampleshift = 1;
	}

	/* sample rate */
	bta->rate = card->rate;
	if (rate)
		bta->rate = rate;
	
	mutex_init(&bta->lock);
        init_waitqueue_head(&bta->readq);

	if (-1 != latency) {
		printk(KERN_INFO "btaudio: setting pci latency timer to %d\n",
		       latency);
		pci_write_config_byte(pci_dev, PCI_LATENCY_TIMER, latency);
	}
        pci_read_config_byte(pci_dev, PCI_CLASS_REVISION, &revision);
        pci_read_config_byte(pci_dev, PCI_LATENCY_TIMER, &lat);
        printk(KERN_INFO "btaudio: Bt%x (rev %d) at %02x:%02x.%x, ",
	       pci_dev->device,revision,pci_dev->bus->number,
	       PCI_SLOT(pci_dev->devfn),PCI_FUNC(pci_dev->devfn));
        printk("irq: %d, latency: %d, mmio: 0x%lx\n",
	       bta->irq, lat, bta->mem);
	printk("btaudio: using card config \"%s\"\n", card->name);

	/* init hw */
        btwrite(0, REG_GPIO_DMA_CTL);
        btwrite(0, REG_INT_MASK);
        btwrite(~0U, REG_INT_STAT);
	pci_set_master(pci_dev);

	if ((rc = request_irq(bta->irq, btaudio_irq, IRQF_SHARED|IRQF_DISABLED,
			      "btaudio",(void *)bta)) < 0) {
		printk(KERN_WARNING
		       "btaudio: can't request irq (rc=%d)\n",rc);
		goto fail1;
	}

	/* register devices */
	if (digital) {
		rc = bta->dsp_digital =
			register_sound_dsp(&btaudio_digital_dsp_fops,dsp1);
		if (rc < 0) {
			printk(KERN_WARNING
			       "btaudio: can't register digital dsp (rc=%d)\n",rc);
			goto fail2;
		}
		printk(KERN_INFO "btaudio: registered device dsp%d [digital]\n",
		       bta->dsp_digital >> 4);
	}
	if (analog) {
		rc = bta->dsp_analog =
			register_sound_dsp(&btaudio_analog_dsp_fops,dsp2);
		if (rc < 0) {
			printk(KERN_WARNING
			       "btaudio: can't register analog dsp (rc=%d)\n",rc);
			goto fail3;
		}
		printk(KERN_INFO "btaudio: registered device dsp%d [analog]\n",
		       bta->dsp_analog >> 4);
		rc = bta->mixer_dev = register_sound_mixer(&btaudio_mixer_fops,mixer);
		if (rc < 0) {
			printk(KERN_WARNING
			       "btaudio: can't register mixer (rc=%d)\n",rc);
			goto fail4;
		}
		printk(KERN_INFO "btaudio: registered device mixer%d\n",
		       bta->mixer_dev >> 4);
	}

	/* hook into linked list */
	bta->next = btaudios;
	btaudios = bta;

	pci_set_drvdata(pci_dev,bta);
        return 0;

 fail4:
	unregister_sound_dsp(bta->dsp_analog);
 fail3:
	if (digital)
		unregister_sound_dsp(bta->dsp_digital);
 fail2:
        free_irq(bta->irq,bta);	
 fail1:
	kfree(bta);
 fail0:
	release_mem_region(pci_resource_start(pci_dev,0),
			   pci_resource_len(pci_dev,0));
	return rc;
}

static void __devexit btaudio_remove(struct pci_dev *pci_dev)
{
	struct btaudio *bta = pci_get_drvdata(pci_dev);
	struct btaudio *walk;

	/* turn off all DMA / IRQs */
        btand(~15, REG_GPIO_DMA_CTL);
        btwrite(0, REG_INT_MASK);
        btwrite(~0U, REG_INT_STAT);

	/* unregister devices */
	if (digital) {
		unregister_sound_dsp(bta->dsp_digital);
	}
	if (analog) {
		unregister_sound_dsp(bta->dsp_analog);
		unregister_sound_mixer(bta->mixer_dev);
	}

	/* free resources */
	free_buffer(bta);
        free_irq(bta->irq,bta);
	release_mem_region(pci_resource_start(pci_dev,0),
			   pci_resource_len(pci_dev,0));

	/* remove from linked list */
	if (bta == btaudios) {
		btaudios = NULL;
	} else {
		for (walk = btaudios; walk->next != bta; walk = walk->next)
			; /* if (NULL == walk->next) BUG(); */
		walk->next = bta->next;
	}

	pci_set_drvdata(pci_dev, NULL);
	kfree(bta);
	return;
}

/* -------------------------------------------------------------- */

static struct pci_device_id btaudio_pci_tbl[] = {
        {
		.vendor		= PCI_VENDOR_ID_BROOKTREE,
		.device		= 0x0878,
		.subvendor	= 0x0070,
		.subdevice	= 0xff01,
		.driver_data	= BTA_OSPREY200,
	},{
		.vendor		= PCI_VENDOR_ID_BROOKTREE,
		.device		= 0x0878,
		.subvendor	= PCI_ANY_ID,
		.subdevice	= PCI_ANY_ID,
	},{
		.vendor		= PCI_VENDOR_ID_BROOKTREE,
		.device		= 0x0878,
		.subvendor	= PCI_ANY_ID,
		.subdevice	= PCI_ANY_ID,
        },{
		/* --- end of list --- */
	}
};

static struct pci_driver btaudio_pci_driver = {
        .name		= "btaudio",
        .id_table	= btaudio_pci_tbl,
        .probe		= btaudio_probe,
        .remove		=  __devexit_p(btaudio_remove),
};

static int btaudio_init_module(void)
{
	printk(KERN_INFO "btaudio: driver version 0.7 loaded [%s%s%s]\n",
	       digital ? "digital" : "",
	       analog && digital ? "+" : "",
	       analog ? "analog" : "");
	return pci_register_driver(&btaudio_pci_driver);
}

static void btaudio_cleanup_module(void)
{
	pci_unregister_driver(&btaudio_pci_driver);
	return;
}

module_init(btaudio_init_module);
module_exit(btaudio_cleanup_module);

module_param(dsp1, int, S_IRUGO);
module_param(dsp2, int, S_IRUGO);
module_param(mixer, int, S_IRUGO);
module_param(debug, int, S_IRUGO | S_IWUSR);
module_param(irq_debug, int, S_IRUGO | S_IWUSR);
module_param(digital, int, S_IRUGO);
module_param(analog, int, S_IRUGO);
module_param(rate, int, S_IRUGO);
module_param(latency, int, S_IRUGO);
MODULE_PARM_DESC(latency,"pci latency timer");

MODULE_DEVICE_TABLE(pci, btaudio_pci_tbl);
MODULE_DESCRIPTION("bt878 audio dma driver");
MODULE_AUTHOR("Gerd Knorr");
MODULE_LICENSE("GPL");

/*
 * Local variables:
 * c-basic-offset: 8
 * End:
 */
