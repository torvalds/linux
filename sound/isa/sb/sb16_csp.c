// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *  Copyright (c) 1999 by Uros Bizjak <uros@kss-loka.si>
 *                        Takashi Iwai <tiwai@suse.de>
 *
 *  SB16ASP/AWE32 CSP control
 *
 *  CSP microcode loader:
 *   alsa-tools/sb16_csp/ 
 */

#include <linux/delay.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <sound/core.h>
#include <sound/control.h>
#include <sound/info.h>
#include <sound/sb16_csp.h>
#include <sound/initval.h>

MODULE_AUTHOR("Uros Bizjak <uros@kss-loka.si>");
MODULE_DESCRIPTION("ALSA driver for SB16 Creative Signal Processor");
MODULE_LICENSE("GPL");
MODULE_FIRMWARE("sb16/mulaw_main.csp");
MODULE_FIRMWARE("sb16/alaw_main.csp");
MODULE_FIRMWARE("sb16/ima_adpcm_init.csp");
MODULE_FIRMWARE("sb16/ima_adpcm_playback.csp");
MODULE_FIRMWARE("sb16/ima_adpcm_capture.csp");

#ifdef SNDRV_LITTLE_ENDIAN
#define CSP_HDR_VALUE(a,b,c,d)	((a) | ((b)<<8) | ((c)<<16) | ((d)<<24))
#else
#define CSP_HDR_VALUE(a,b,c,d)	((d) | ((c)<<8) | ((b)<<16) | ((a)<<24))
#endif

#define RIFF_HEADER	CSP_HDR_VALUE('R', 'I', 'F', 'F')
#define CSP__HEADER	CSP_HDR_VALUE('C', 'S', 'P', ' ')
#define LIST_HEADER	CSP_HDR_VALUE('L', 'I', 'S', 'T')
#define FUNC_HEADER	CSP_HDR_VALUE('f', 'u', 'n', 'c')
#define CODE_HEADER	CSP_HDR_VALUE('c', 'o', 'd', 'e')
#define INIT_HEADER	CSP_HDR_VALUE('i', 'n', 'i', 't')
#define MAIN_HEADER	CSP_HDR_VALUE('m', 'a', 'i', 'n')

/*
 * RIFF data format
 */
struct riff_header {
	__le32 name;
	__le32 len;
};

struct desc_header {
	struct riff_header info;
	__le16 func_nr;
	__le16 VOC_type;
	__le16 flags_play_rec;
	__le16 flags_16bit_8bit;
	__le16 flags_stereo_mono;
	__le16 flags_rates;
};

/*
 * prototypes
 */
static void snd_sb_csp_free(struct snd_hwdep *hw);
static int snd_sb_csp_open(struct snd_hwdep * hw, struct file *file);
static int snd_sb_csp_ioctl(struct snd_hwdep * hw, struct file *file, unsigned int cmd, unsigned long arg);
static int snd_sb_csp_release(struct snd_hwdep * hw, struct file *file);

static int csp_detect(struct snd_sb *chip, int *version);
static int set_codec_parameter(struct snd_sb *chip, unsigned char par, unsigned char val);
static int set_register(struct snd_sb *chip, unsigned char reg, unsigned char val);
static int read_register(struct snd_sb *chip, unsigned char reg);
static int set_mode_register(struct snd_sb *chip, unsigned char mode);
static int get_version(struct snd_sb *chip);

static int snd_sb_csp_riff_load(struct snd_sb_csp * p,
				struct snd_sb_csp_microcode __user * code);
static int snd_sb_csp_unload(struct snd_sb_csp * p);
static int snd_sb_csp_load_user(struct snd_sb_csp * p, const unsigned char __user *buf, int size, int load_flags);
static int snd_sb_csp_autoload(struct snd_sb_csp * p, snd_pcm_format_t pcm_sfmt, int play_rec_mode);
static int snd_sb_csp_check_version(struct snd_sb_csp * p);

static int snd_sb_csp_use(struct snd_sb_csp * p);
static int snd_sb_csp_unuse(struct snd_sb_csp * p);
static int snd_sb_csp_start(struct snd_sb_csp * p, int sample_width, int channels);
static int snd_sb_csp_stop(struct snd_sb_csp * p);
static int snd_sb_csp_pause(struct snd_sb_csp * p);
static int snd_sb_csp_restart(struct snd_sb_csp * p);

static int snd_sb_qsound_build(struct snd_sb_csp * p);
static void snd_sb_qsound_destroy(struct snd_sb_csp * p);
static int snd_sb_csp_qsound_transfer(struct snd_sb_csp * p);

static int init_proc_entry(struct snd_sb_csp * p, int device);
static void info_read(struct snd_info_entry *entry, struct snd_info_buffer *buffer);

/*
 * Detect CSP chip and create a new instance
 */
int snd_sb_csp_new(struct snd_sb *chip, int device, struct snd_hwdep ** rhwdep)
{
	struct snd_sb_csp *p;
	int version;
	int err;
	struct snd_hwdep *hw;

	if (rhwdep)
		*rhwdep = NULL;

	if (csp_detect(chip, &version))
		return -ENODEV;

	err = snd_hwdep_new(chip->card, "SB16-CSP", device, &hw);
	if (err < 0)
		return err;

	p = kzalloc(sizeof(*p), GFP_KERNEL);
	if (!p) {
		snd_device_free(chip->card, hw);
		return -ENOMEM;
	}
	p->chip = chip;
	p->version = version;

	/* CSP operators */
	p->ops.csp_use = snd_sb_csp_use;
	p->ops.csp_unuse = snd_sb_csp_unuse;
	p->ops.csp_autoload = snd_sb_csp_autoload;
	p->ops.csp_start = snd_sb_csp_start;
	p->ops.csp_stop = snd_sb_csp_stop;
	p->ops.csp_qsound_transfer = snd_sb_csp_qsound_transfer;

	mutex_init(&p->access_mutex);
	sprintf(hw->name, "CSP v%d.%d", (version >> 4), (version & 0x0f));
	hw->iface = SNDRV_HWDEP_IFACE_SB16CSP;
	hw->private_data = p;
	hw->private_free = snd_sb_csp_free;

	/* operators - only write/ioctl */
	hw->ops.open = snd_sb_csp_open;
	hw->ops.ioctl = snd_sb_csp_ioctl;
	hw->ops.release = snd_sb_csp_release;

	/* create a proc entry */
	init_proc_entry(p, device);
	if (rhwdep)
		*rhwdep = hw;
	return 0;
}

/*
 * free_private for hwdep instance
 */
static void snd_sb_csp_free(struct snd_hwdep *hwdep)
{
	int i;
	struct snd_sb_csp *p = hwdep->private_data;
	if (p) {
		if (p->running & SNDRV_SB_CSP_ST_RUNNING)
			snd_sb_csp_stop(p);
		for (i = 0; i < ARRAY_SIZE(p->csp_programs); ++i)
			release_firmware(p->csp_programs[i]);
		kfree(p);
	}
}

/* ------------------------------ */

/*
 * open the device exclusively
 */
static int snd_sb_csp_open(struct snd_hwdep * hw, struct file *file)
{
	struct snd_sb_csp *p = hw->private_data;
	return (snd_sb_csp_use(p));
}

/*
 * ioctl for hwdep device:
 */
static int snd_sb_csp_ioctl(struct snd_hwdep * hw, struct file *file, unsigned int cmd, unsigned long arg)
{
	struct snd_sb_csp *p = hw->private_data;
	struct snd_sb_csp_info info;
	struct snd_sb_csp_start start_info;
	int err;

	if (snd_BUG_ON(!p))
		return -EINVAL;

	if (snd_sb_csp_check_version(p))
		return -ENODEV;

	switch (cmd) {
		/* get information */
	case SNDRV_SB_CSP_IOCTL_INFO:
		memset(&info, 0, sizeof(info));
		*info.codec_name = *p->codec_name;
		info.func_nr = p->func_nr;
		info.acc_format = p->acc_format;
		info.acc_channels = p->acc_channels;
		info.acc_width = p->acc_width;
		info.acc_rates = p->acc_rates;
		info.csp_mode = p->mode;
		info.run_channels = p->run_channels;
		info.run_width = p->run_width;
		info.version = p->version;
		info.state = p->running;
		if (copy_to_user((void __user *)arg, &info, sizeof(info)))
			err = -EFAULT;
		else
			err = 0;
		break;

		/* load CSP microcode */
	case SNDRV_SB_CSP_IOCTL_LOAD_CODE:
		err = (p->running & SNDRV_SB_CSP_ST_RUNNING ?
		       -EBUSY : snd_sb_csp_riff_load(p, (struct snd_sb_csp_microcode __user *) arg));
		break;
	case SNDRV_SB_CSP_IOCTL_UNLOAD_CODE:
		err = (p->running & SNDRV_SB_CSP_ST_RUNNING ?
		       -EBUSY : snd_sb_csp_unload(p));
		break;

		/* change CSP running state */
	case SNDRV_SB_CSP_IOCTL_START:
		if (copy_from_user(&start_info, (void __user *) arg, sizeof(start_info)))
			err = -EFAULT;
		else
			err = snd_sb_csp_start(p, start_info.sample_width, start_info.channels);
		break;
	case SNDRV_SB_CSP_IOCTL_STOP:
		err = snd_sb_csp_stop(p);
		break;
	case SNDRV_SB_CSP_IOCTL_PAUSE:
		err = snd_sb_csp_pause(p);
		break;
	case SNDRV_SB_CSP_IOCTL_RESTART:
		err = snd_sb_csp_restart(p);
		break;
	default:
		err = -ENOTTY;
		break;
	}

	return err;
}

/*
 * close the device
 */
static int snd_sb_csp_release(struct snd_hwdep * hw, struct file *file)
{
	struct snd_sb_csp *p = hw->private_data;
	return (snd_sb_csp_unuse(p));
}

/* ------------------------------ */

/*
 * acquire device
 */
static int snd_sb_csp_use(struct snd_sb_csp * p)
{
	mutex_lock(&p->access_mutex);
	if (p->used) {
		mutex_unlock(&p->access_mutex);
		return -EAGAIN;
	}
	p->used++;
	mutex_unlock(&p->access_mutex);

	return 0;

}

/*
 * release device
 */
static int snd_sb_csp_unuse(struct snd_sb_csp * p)
{
	mutex_lock(&p->access_mutex);
	p->used--;
	mutex_unlock(&p->access_mutex);

	return 0;
}

/*
 * load microcode via ioctl: 
 * code is user-space pointer
 */
static int snd_sb_csp_riff_load(struct snd_sb_csp * p,
				struct snd_sb_csp_microcode __user * mcode)
{
	struct snd_sb_csp_mc_header info;

	unsigned char __user *data_ptr;
	unsigned char __user *data_end;
	unsigned short func_nr = 0;

	struct riff_header file_h, item_h, code_h;
	__le32 item_type;
	struct desc_header funcdesc_h;

	unsigned long flags;
	int err;

	if (copy_from_user(&info, mcode, sizeof(info)))
		return -EFAULT;
	data_ptr = mcode->data;

	if (copy_from_user(&file_h, data_ptr, sizeof(file_h)))
		return -EFAULT;
	if ((le32_to_cpu(file_h.name) != RIFF_HEADER) ||
	    (le32_to_cpu(file_h.len) >= SNDRV_SB_CSP_MAX_MICROCODE_FILE_SIZE - sizeof(file_h))) {
		snd_printd("%s: Invalid RIFF header\n", __func__);
		return -EINVAL;
	}
	data_ptr += sizeof(file_h);
	data_end = data_ptr + le32_to_cpu(file_h.len);

	if (copy_from_user(&item_type, data_ptr, sizeof(item_type)))
		return -EFAULT;
	if (le32_to_cpu(item_type) != CSP__HEADER) {
		snd_printd("%s: Invalid RIFF file type\n", __func__);
		return -EINVAL;
	}
	data_ptr += sizeof (item_type);

	for (; data_ptr < data_end; data_ptr += le32_to_cpu(item_h.len)) {
		if (copy_from_user(&item_h, data_ptr, sizeof(item_h)))
			return -EFAULT;
		data_ptr += sizeof(item_h);
		if (le32_to_cpu(item_h.name) != LIST_HEADER)
			continue;

		if (copy_from_user(&item_type, data_ptr, sizeof(item_type)))
			 return -EFAULT;
		switch (le32_to_cpu(item_type)) {
		case FUNC_HEADER:
			if (copy_from_user(&funcdesc_h, data_ptr + sizeof(item_type), sizeof(funcdesc_h)))
				return -EFAULT;
			func_nr = le16_to_cpu(funcdesc_h.func_nr);
			break;
		case CODE_HEADER:
			if (func_nr != info.func_req)
				break;	/* not required function, try next */
			data_ptr += sizeof(item_type);

			/* destroy QSound mixer element */
			if (p->mode == SNDRV_SB_CSP_MODE_QSOUND) {
				snd_sb_qsound_destroy(p);
			}
			/* Clear all flags */
			p->running = 0;
			p->mode = 0;

			/* load microcode blocks */
			for (;;) {
				if (data_ptr >= data_end)
					return -EINVAL;
				if (copy_from_user(&code_h, data_ptr, sizeof(code_h)))
					return -EFAULT;

				/* init microcode blocks */
				if (le32_to_cpu(code_h.name) != INIT_HEADER)
					break;
				data_ptr += sizeof(code_h);
				err = snd_sb_csp_load_user(p, data_ptr, le32_to_cpu(code_h.len),
						      SNDRV_SB_CSP_LOAD_INITBLOCK);
				if (err)
					return err;
				data_ptr += le32_to_cpu(code_h.len);
			}
			/* main microcode block */
			if (copy_from_user(&code_h, data_ptr, sizeof(code_h)))
				return -EFAULT;

			if (le32_to_cpu(code_h.name) != MAIN_HEADER) {
				snd_printd("%s: Missing 'main' microcode\n", __func__);
				return -EINVAL;
			}
			data_ptr += sizeof(code_h);
			err = snd_sb_csp_load_user(p, data_ptr,
						   le32_to_cpu(code_h.len), 0);
			if (err)
				return err;

			/* fill in codec header */
			strscpy(p->codec_name, info.codec_name, sizeof(p->codec_name));
			p->func_nr = func_nr;
			p->mode = le16_to_cpu(funcdesc_h.flags_play_rec);
			switch (le16_to_cpu(funcdesc_h.VOC_type)) {
			case 0x0001:	/* QSound decoder */
				if (le16_to_cpu(funcdesc_h.flags_play_rec) == SNDRV_SB_CSP_MODE_DSP_WRITE) {
					if (snd_sb_qsound_build(p) == 0)
						/* set QSound flag and clear all other mode flags */
						p->mode = SNDRV_SB_CSP_MODE_QSOUND;
				}
				p->acc_format = 0;
				break;
			case 0x0006:	/* A Law codec */
				p->acc_format = SNDRV_PCM_FMTBIT_A_LAW;
				break;
			case 0x0007:	/* Mu Law codec */
				p->acc_format = SNDRV_PCM_FMTBIT_MU_LAW;
				break;
			case 0x0011:	/* what Creative thinks is IMA ADPCM codec */
			case 0x0200:	/* Creative ADPCM codec */
				p->acc_format = SNDRV_PCM_FMTBIT_IMA_ADPCM;
				break;
			case    201:	/* Text 2 Speech decoder */
				/* TODO: Text2Speech handling routines */
				p->acc_format = 0;
				break;
			case 0x0202:	/* Fast Speech 8 codec */
			case 0x0203:	/* Fast Speech 10 codec */
				p->acc_format = SNDRV_PCM_FMTBIT_SPECIAL;
				break;
			default:	/* other codecs are unsupported */
				p->acc_format = p->acc_width = p->acc_rates = 0;
				p->mode = 0;
				snd_printd("%s: Unsupported CSP codec type: 0x%04x\n",
					   __func__,
					   le16_to_cpu(funcdesc_h.VOC_type));
				return -EINVAL;
			}
			p->acc_channels = le16_to_cpu(funcdesc_h.flags_stereo_mono);
			p->acc_width = le16_to_cpu(funcdesc_h.flags_16bit_8bit);
			p->acc_rates = le16_to_cpu(funcdesc_h.flags_rates);

			/* Decouple CSP from IRQ and DMAREQ lines */
			spin_lock_irqsave(&p->chip->reg_lock, flags);
			set_mode_register(p->chip, 0xfc);
			set_mode_register(p->chip, 0x00);
			spin_unlock_irqrestore(&p->chip->reg_lock, flags);

			/* finished loading successfully */
			p->running = SNDRV_SB_CSP_ST_LOADED;	/* set LOADED flag */
			return 0;
		}
	}
	snd_printd("%s: Function #%d not found\n", __func__, info.func_req);
	return -EINVAL;
}

/*
 * unload CSP microcode
 */
static int snd_sb_csp_unload(struct snd_sb_csp * p)
{
	if (p->running & SNDRV_SB_CSP_ST_RUNNING)
		return -EBUSY;
	if (!(p->running & SNDRV_SB_CSP_ST_LOADED))
		return -ENXIO;

	/* clear supported formats */
	p->acc_format = 0;
	p->acc_channels = p->acc_width = p->acc_rates = 0;
	/* destroy QSound mixer element */
	if (p->mode == SNDRV_SB_CSP_MODE_QSOUND) {
		snd_sb_qsound_destroy(p);
	}
	/* clear all flags */
	p->running = 0;
	p->mode = 0;
	return 0;
}

/*
 * send command sequence to DSP
 */
static inline int command_seq(struct snd_sb *chip, const unsigned char *seq, int size)
{
	int i;
	for (i = 0; i < size; i++) {
		if (!snd_sbdsp_command(chip, seq[i]))
			return -EIO;
	}
	return 0;
}

/*
 * set CSP codec parameter
 */
static int set_codec_parameter(struct snd_sb *chip, unsigned char par, unsigned char val)
{
	unsigned char dsp_cmd[3];

	dsp_cmd[0] = 0x05;	/* CSP set codec parameter */
	dsp_cmd[1] = val;	/* Parameter value */
	dsp_cmd[2] = par;	/* Parameter */
	command_seq(chip, dsp_cmd, 3);
	snd_sbdsp_command(chip, 0x03);	/* DSP read? */
	if (snd_sbdsp_get_byte(chip) != par)
		return -EIO;
	return 0;
}

/*
 * set CSP register
 */
static int set_register(struct snd_sb *chip, unsigned char reg, unsigned char val)
{
	unsigned char dsp_cmd[3];

	dsp_cmd[0] = 0x0e;	/* CSP set register */
	dsp_cmd[1] = reg;	/* CSP Register */
	dsp_cmd[2] = val;	/* value */
	return command_seq(chip, dsp_cmd, 3);
}

/*
 * read CSP register
 * return < 0 -> error
 */
static int read_register(struct snd_sb *chip, unsigned char reg)
{
	unsigned char dsp_cmd[2];

	dsp_cmd[0] = 0x0f;	/* CSP read register */
	dsp_cmd[1] = reg;	/* CSP Register */
	command_seq(chip, dsp_cmd, 2);
	return snd_sbdsp_get_byte(chip);	/* Read DSP value */
}

/*
 * set CSP mode register
 */
static int set_mode_register(struct snd_sb *chip, unsigned char mode)
{
	unsigned char dsp_cmd[2];

	dsp_cmd[0] = 0x04;	/* CSP set mode register */
	dsp_cmd[1] = mode;	/* mode */
	return command_seq(chip, dsp_cmd, 2);
}

/*
 * Detect CSP
 * return 0 if CSP exists.
 */
static int csp_detect(struct snd_sb *chip, int *version)
{
	unsigned char csp_test1, csp_test2;
	unsigned long flags;
	int result = -ENODEV;

	spin_lock_irqsave(&chip->reg_lock, flags);

	set_codec_parameter(chip, 0x00, 0x00);
	set_mode_register(chip, 0xfc);		/* 0xfc = ?? */

	csp_test1 = read_register(chip, 0x83);
	set_register(chip, 0x83, ~csp_test1);
	csp_test2 = read_register(chip, 0x83);
	if (csp_test2 != (csp_test1 ^ 0xff))
		goto __fail;

	set_register(chip, 0x83, csp_test1);
	csp_test2 = read_register(chip, 0x83);
	if (csp_test2 != csp_test1)
		goto __fail;

	set_mode_register(chip, 0x00);		/* 0x00 = ? */

	*version = get_version(chip);
	snd_sbdsp_reset(chip);	/* reset DSP after getversion! */
	if (*version >= 0x10 && *version <= 0x1f)
		result = 0;		/* valid version id */

      __fail:
	spin_unlock_irqrestore(&chip->reg_lock, flags);
	return result;
}

/*
 * get CSP version number
 */
static int get_version(struct snd_sb *chip)
{
	unsigned char dsp_cmd[2];

	dsp_cmd[0] = 0x08;	/* SB_DSP_!something! */
	dsp_cmd[1] = 0x03;	/* get chip version id? */
	command_seq(chip, dsp_cmd, 2);

	return (snd_sbdsp_get_byte(chip));
}

/*
 * check if the CSP version is valid
 */
static int snd_sb_csp_check_version(struct snd_sb_csp * p)
{
	if (p->version < 0x10 || p->version > 0x1f) {
		snd_printd("%s: Invalid CSP version: 0x%x\n", __func__, p->version);
		return 1;
	}
	return 0;
}

/*
 * download microcode to CSP (microcode should have one "main" block).
 */
static int snd_sb_csp_load(struct snd_sb_csp * p, const unsigned char *buf, int size, int load_flags)
{
	int status, i;
	int err;
	int result = -EIO;
	unsigned long flags;

	spin_lock_irqsave(&p->chip->reg_lock, flags);
	snd_sbdsp_command(p->chip, 0x01);	/* CSP download command */
	if (snd_sbdsp_get_byte(p->chip)) {
		snd_printd("%s: Download command failed\n", __func__);
		goto __fail;
	}
	/* Send CSP low byte (size - 1) */
	snd_sbdsp_command(p->chip, (unsigned char)(size - 1));
	/* Send high byte */
	snd_sbdsp_command(p->chip, (unsigned char)((size - 1) >> 8));
	/* send microcode sequence */
	/* load from kernel space */
	while (size--) {
		if (!snd_sbdsp_command(p->chip, *buf++))
			goto __fail;
	}
	if (snd_sbdsp_get_byte(p->chip))
		goto __fail;

	if (load_flags & SNDRV_SB_CSP_LOAD_INITBLOCK) {
		i = 0;
		/* some codecs (FastSpeech) take some time to initialize */
		while (1) {
			snd_sbdsp_command(p->chip, 0x03);
			status = snd_sbdsp_get_byte(p->chip);
			if (status == 0x55 || ++i >= 10)
				break;
			udelay (10);
		}
		if (status != 0x55) {
			snd_printd("%s: Microcode initialization failed\n", __func__);
			goto __fail;
		}
	} else {
		/*
		 * Read mixer register SB_DSP4_DMASETUP after loading 'main' code.
		 * Start CSP chip if no 16bit DMA channel is set - some kind
		 * of autorun or perhaps a bugfix?
		 */
		spin_lock(&p->chip->mixer_lock);
		status = snd_sbmixer_read(p->chip, SB_DSP4_DMASETUP);
		spin_unlock(&p->chip->mixer_lock);
		if (!(status & (SB_DMASETUP_DMA7 | SB_DMASETUP_DMA6 | SB_DMASETUP_DMA5))) {
			err = (set_codec_parameter(p->chip, 0xaa, 0x00) ||
			       set_codec_parameter(p->chip, 0xff, 0x00));
			snd_sbdsp_reset(p->chip);		/* really! */
			if (err)
				goto __fail;
			set_mode_register(p->chip, 0xc0);	/* c0 = STOP */
			set_mode_register(p->chip, 0x70);	/* 70 = RUN */
		}
	}
	result = 0;

      __fail:
	spin_unlock_irqrestore(&p->chip->reg_lock, flags);
	return result;
}
 
static int snd_sb_csp_load_user(struct snd_sb_csp * p, const unsigned char __user *buf, int size, int load_flags)
{
	int err;
	unsigned char *kbuf;

	kbuf = memdup_user(buf, size);
	if (IS_ERR(kbuf))
		return PTR_ERR(kbuf);

	err = snd_sb_csp_load(p, kbuf, size, load_flags);

	kfree(kbuf);
	return err;
}

static int snd_sb_csp_firmware_load(struct snd_sb_csp *p, int index, int flags)
{
	static const char *const names[] = {
		"sb16/mulaw_main.csp",
		"sb16/alaw_main.csp",
		"sb16/ima_adpcm_init.csp",
		"sb16/ima_adpcm_playback.csp",
		"sb16/ima_adpcm_capture.csp",
	};
	const struct firmware *program;

	BUILD_BUG_ON(ARRAY_SIZE(names) != CSP_PROGRAM_COUNT);
	program = p->csp_programs[index];
	if (!program) {
		int err = request_firmware(&program, names[index],
				       p->chip->card->dev);
		if (err < 0)
			return err;
		p->csp_programs[index] = program;
	}
	return snd_sb_csp_load(p, program->data, program->size, flags);
}

/*
 * autoload hardware codec if necessary
 * return 0 if CSP is loaded and ready to run (p->running != 0)
 */
static int snd_sb_csp_autoload(struct snd_sb_csp * p, snd_pcm_format_t pcm_sfmt, int play_rec_mode)
{
	unsigned long flags;
	int err = 0;

	/* if CSP is running or manually loaded then exit */
	if (p->running & (SNDRV_SB_CSP_ST_RUNNING | SNDRV_SB_CSP_ST_LOADED)) 
		return -EBUSY;

	/* autoload microcode only if requested hardware codec is not already loaded */
	if (((1U << (__force int)pcm_sfmt) & p->acc_format) && (play_rec_mode & p->mode)) {
		p->running = SNDRV_SB_CSP_ST_AUTO;
	} else {
		switch (pcm_sfmt) {
		case SNDRV_PCM_FORMAT_MU_LAW:
			err = snd_sb_csp_firmware_load(p, CSP_PROGRAM_MULAW, 0);
			p->acc_format = SNDRV_PCM_FMTBIT_MU_LAW;
			p->mode = SNDRV_SB_CSP_MODE_DSP_READ | SNDRV_SB_CSP_MODE_DSP_WRITE;
			break;
		case SNDRV_PCM_FORMAT_A_LAW:
			err = snd_sb_csp_firmware_load(p, CSP_PROGRAM_ALAW, 0);
			p->acc_format = SNDRV_PCM_FMTBIT_A_LAW;
			p->mode = SNDRV_SB_CSP_MODE_DSP_READ | SNDRV_SB_CSP_MODE_DSP_WRITE;
			break;
		case SNDRV_PCM_FORMAT_IMA_ADPCM:
			err = snd_sb_csp_firmware_load(p, CSP_PROGRAM_ADPCM_INIT,
						       SNDRV_SB_CSP_LOAD_INITBLOCK);
			if (err)
				break;
			if (play_rec_mode == SNDRV_SB_CSP_MODE_DSP_WRITE) {
				err = snd_sb_csp_firmware_load
					(p, CSP_PROGRAM_ADPCM_PLAYBACK, 0);
				p->mode = SNDRV_SB_CSP_MODE_DSP_WRITE;
			} else {
				err = snd_sb_csp_firmware_load
					(p, CSP_PROGRAM_ADPCM_CAPTURE, 0);
				p->mode = SNDRV_SB_CSP_MODE_DSP_READ;
			}
			p->acc_format = SNDRV_PCM_FMTBIT_IMA_ADPCM;
			break;				  
		default:
			/* Decouple CSP from IRQ and DMAREQ lines */
			if (p->running & SNDRV_SB_CSP_ST_AUTO) {
				spin_lock_irqsave(&p->chip->reg_lock, flags);
				set_mode_register(p->chip, 0xfc);
				set_mode_register(p->chip, 0x00);
				spin_unlock_irqrestore(&p->chip->reg_lock, flags);
				p->running = 0;			/* clear autoloaded flag */
			}
			return -EINVAL;
		}
		if (err) {
			p->acc_format = 0;
			p->acc_channels = p->acc_width = p->acc_rates = 0;

			p->running = 0;				/* clear autoloaded flag */
			p->mode = 0;
			return (err);
		} else {
			p->running = SNDRV_SB_CSP_ST_AUTO;	/* set autoloaded flag */
			p->acc_width = SNDRV_SB_CSP_SAMPLE_16BIT;	/* only 16 bit data */
			p->acc_channels = SNDRV_SB_CSP_MONO | SNDRV_SB_CSP_STEREO;
			p->acc_rates = SNDRV_SB_CSP_RATE_ALL;	/* HW codecs accept all rates */
		}   

	}
	return (p->running & SNDRV_SB_CSP_ST_AUTO) ? 0 : -ENXIO;
}

/*
 * start CSP
 */
static int snd_sb_csp_start(struct snd_sb_csp * p, int sample_width, int channels)
{
	unsigned char s_type;	/* sample type */
	unsigned char mixL, mixR;
	int result = -EIO;
	unsigned long flags;

	if (!(p->running & (SNDRV_SB_CSP_ST_LOADED | SNDRV_SB_CSP_ST_AUTO))) {
		snd_printd("%s: Microcode not loaded\n", __func__);
		return -ENXIO;
	}
	if (p->running & SNDRV_SB_CSP_ST_RUNNING) {
		snd_printd("%s: CSP already running\n", __func__);
		return -EBUSY;
	}
	if (!(sample_width & p->acc_width)) {
		snd_printd("%s: Unsupported PCM sample width\n", __func__);
		return -EINVAL;
	}
	if (!(channels & p->acc_channels)) {
		snd_printd("%s: Invalid number of channels\n", __func__);
		return -EINVAL;
	}

	/* Mute PCM volume */
	spin_lock_irqsave(&p->chip->mixer_lock, flags);
	mixL = snd_sbmixer_read(p->chip, SB_DSP4_PCM_DEV);
	mixR = snd_sbmixer_read(p->chip, SB_DSP4_PCM_DEV + 1);
	snd_sbmixer_write(p->chip, SB_DSP4_PCM_DEV, mixL & 0x7);
	snd_sbmixer_write(p->chip, SB_DSP4_PCM_DEV + 1, mixR & 0x7);
	spin_unlock_irqrestore(&p->chip->mixer_lock, flags);

	spin_lock(&p->chip->reg_lock);
	set_mode_register(p->chip, 0xc0);	/* c0 = STOP */
	set_mode_register(p->chip, 0x70);	/* 70 = RUN */

	s_type = 0x00;
	if (channels == SNDRV_SB_CSP_MONO)
		s_type = 0x11;	/* 000n 000n    (n = 1 if mono) */
	if (sample_width == SNDRV_SB_CSP_SAMPLE_8BIT)
		s_type |= 0x22;	/* 00dX 00dX    (d = 1 if 8 bit samples) */

	if (set_codec_parameter(p->chip, 0x81, s_type)) {
		snd_printd("%s: Set sample type command failed\n", __func__);
		goto __fail;
	}
	if (set_codec_parameter(p->chip, 0x80, 0x00)) {
		snd_printd("%s: Codec start command failed\n", __func__);
		goto __fail;
	}
	p->run_width = sample_width;
	p->run_channels = channels;

	p->running |= SNDRV_SB_CSP_ST_RUNNING;

	if (p->mode & SNDRV_SB_CSP_MODE_QSOUND) {
		set_codec_parameter(p->chip, 0xe0, 0x01);
		/* enable QSound decoder */
		set_codec_parameter(p->chip, 0x00, 0xff);
		set_codec_parameter(p->chip, 0x01, 0xff);
		p->running |= SNDRV_SB_CSP_ST_QSOUND;
		/* set QSound startup value */
		snd_sb_csp_qsound_transfer(p);
	}
	result = 0;

      __fail:
	spin_unlock(&p->chip->reg_lock);

	/* restore PCM volume */
	spin_lock_irqsave(&p->chip->mixer_lock, flags);
	snd_sbmixer_write(p->chip, SB_DSP4_PCM_DEV, mixL);
	snd_sbmixer_write(p->chip, SB_DSP4_PCM_DEV + 1, mixR);
	spin_unlock_irqrestore(&p->chip->mixer_lock, flags);

	return result;
}

/*
 * stop CSP
 */
static int snd_sb_csp_stop(struct snd_sb_csp * p)
{
	int result;
	unsigned char mixL, mixR;
	unsigned long flags;

	if (!(p->running & SNDRV_SB_CSP_ST_RUNNING))
		return 0;

	/* Mute PCM volume */
	spin_lock_irqsave(&p->chip->mixer_lock, flags);
	mixL = snd_sbmixer_read(p->chip, SB_DSP4_PCM_DEV);
	mixR = snd_sbmixer_read(p->chip, SB_DSP4_PCM_DEV + 1);
	snd_sbmixer_write(p->chip, SB_DSP4_PCM_DEV, mixL & 0x7);
	snd_sbmixer_write(p->chip, SB_DSP4_PCM_DEV + 1, mixR & 0x7);
	spin_unlock_irqrestore(&p->chip->mixer_lock, flags);

	spin_lock(&p->chip->reg_lock);
	if (p->running & SNDRV_SB_CSP_ST_QSOUND) {
		set_codec_parameter(p->chip, 0xe0, 0x01);
		/* disable QSound decoder */
		set_codec_parameter(p->chip, 0x00, 0x00);
		set_codec_parameter(p->chip, 0x01, 0x00);

		p->running &= ~SNDRV_SB_CSP_ST_QSOUND;
	}
	result = set_mode_register(p->chip, 0xc0);	/* c0 = STOP */
	spin_unlock(&p->chip->reg_lock);

	/* restore PCM volume */
	spin_lock_irqsave(&p->chip->mixer_lock, flags);
	snd_sbmixer_write(p->chip, SB_DSP4_PCM_DEV, mixL);
	snd_sbmixer_write(p->chip, SB_DSP4_PCM_DEV + 1, mixR);
	spin_unlock_irqrestore(&p->chip->mixer_lock, flags);

	if (!(result))
		p->running &= ~(SNDRV_SB_CSP_ST_PAUSED | SNDRV_SB_CSP_ST_RUNNING);
	return result;
}

/*
 * pause CSP codec and hold DMA transfer
 */
static int snd_sb_csp_pause(struct snd_sb_csp * p)
{
	int result;
	unsigned long flags;

	if (!(p->running & SNDRV_SB_CSP_ST_RUNNING))
		return -EBUSY;

	spin_lock_irqsave(&p->chip->reg_lock, flags);
	result = set_codec_parameter(p->chip, 0x80, 0xff);
	spin_unlock_irqrestore(&p->chip->reg_lock, flags);
	if (!(result))
		p->running |= SNDRV_SB_CSP_ST_PAUSED;

	return result;
}

/*
 * restart CSP codec and resume DMA transfer
 */
static int snd_sb_csp_restart(struct snd_sb_csp * p)
{
	int result;
	unsigned long flags;

	if (!(p->running & SNDRV_SB_CSP_ST_PAUSED))
		return -EBUSY;

	spin_lock_irqsave(&p->chip->reg_lock, flags);
	result = set_codec_parameter(p->chip, 0x80, 0x00);
	spin_unlock_irqrestore(&p->chip->reg_lock, flags);
	if (!(result))
		p->running &= ~SNDRV_SB_CSP_ST_PAUSED;

	return result;
}

/* ------------------------------ */

/*
 * QSound mixer control for PCM
 */

#define snd_sb_qsound_switch_info	snd_ctl_boolean_mono_info

static int snd_sb_qsound_switch_get(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_value *ucontrol)
{
	struct snd_sb_csp *p = snd_kcontrol_chip(kcontrol);
	
	ucontrol->value.integer.value[0] = p->q_enabled ? 1 : 0;
	return 0;
}

static int snd_sb_qsound_switch_put(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_value *ucontrol)
{
	struct snd_sb_csp *p = snd_kcontrol_chip(kcontrol);
	unsigned long flags;
	int change;
	unsigned char nval;
	
	nval = ucontrol->value.integer.value[0] & 0x01;
	spin_lock_irqsave(&p->q_lock, flags);
	change = p->q_enabled != nval;
	p->q_enabled = nval;
	spin_unlock_irqrestore(&p->q_lock, flags);
	return change;
}

static int snd_sb_qsound_space_info(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_info *uinfo)
{
	uinfo->type = SNDRV_CTL_ELEM_TYPE_INTEGER;
	uinfo->count = 2;
	uinfo->value.integer.min = 0;
	uinfo->value.integer.max = SNDRV_SB_CSP_QSOUND_MAX_RIGHT;
	return 0;
}

static int snd_sb_qsound_space_get(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_value *ucontrol)
{
	struct snd_sb_csp *p = snd_kcontrol_chip(kcontrol);
	unsigned long flags;
	
	spin_lock_irqsave(&p->q_lock, flags);
	ucontrol->value.integer.value[0] = p->qpos_left;
	ucontrol->value.integer.value[1] = p->qpos_right;
	spin_unlock_irqrestore(&p->q_lock, flags);
	return 0;
}

static int snd_sb_qsound_space_put(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_value *ucontrol)
{
	struct snd_sb_csp *p = snd_kcontrol_chip(kcontrol);
	unsigned long flags;
	int change;
	unsigned char nval1, nval2;
	
	nval1 = ucontrol->value.integer.value[0];
	if (nval1 > SNDRV_SB_CSP_QSOUND_MAX_RIGHT)
		nval1 = SNDRV_SB_CSP_QSOUND_MAX_RIGHT;
	nval2 = ucontrol->value.integer.value[1];
	if (nval2 > SNDRV_SB_CSP_QSOUND_MAX_RIGHT)
		nval2 = SNDRV_SB_CSP_QSOUND_MAX_RIGHT;
	spin_lock_irqsave(&p->q_lock, flags);
	change = p->qpos_left != nval1 || p->qpos_right != nval2;
	p->qpos_left = nval1;
	p->qpos_right = nval2;
	p->qpos_changed = change;
	spin_unlock_irqrestore(&p->q_lock, flags);
	return change;
}

static const struct snd_kcontrol_new snd_sb_qsound_switch = {
	.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
	.name = "3D Control - Switch",
	.info = snd_sb_qsound_switch_info,
	.get = snd_sb_qsound_switch_get,
	.put = snd_sb_qsound_switch_put
};

static const struct snd_kcontrol_new snd_sb_qsound_space = {
	.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
	.name = "3D Control - Space",
	.info = snd_sb_qsound_space_info,
	.get = snd_sb_qsound_space_get,
	.put = snd_sb_qsound_space_put
};

static int snd_sb_qsound_build(struct snd_sb_csp * p)
{
	struct snd_card *card;
	struct snd_kcontrol *kctl;
	int err;

	if (snd_BUG_ON(!p))
		return -EINVAL;

	card = p->chip->card;
	p->qpos_left = p->qpos_right = SNDRV_SB_CSP_QSOUND_MAX_RIGHT / 2;
	p->qpos_changed = 0;

	spin_lock_init(&p->q_lock);

	kctl = snd_ctl_new1(&snd_sb_qsound_switch, p);
	err = snd_ctl_add(card, kctl);
	if (err < 0)
		goto __error;
	p->qsound_switch = kctl;
	kctl = snd_ctl_new1(&snd_sb_qsound_space, p);
	err = snd_ctl_add(card, kctl);
	if (err < 0)
		goto __error;
	p->qsound_space = kctl;

	return 0;

     __error:
	snd_sb_qsound_destroy(p);
	return err;
}

static void snd_sb_qsound_destroy(struct snd_sb_csp * p)
{
	struct snd_card *card;
	unsigned long flags;

	if (snd_BUG_ON(!p))
		return;

	card = p->chip->card;	
	
	snd_ctl_remove(card, p->qsound_switch);
	p->qsound_switch = NULL;
	snd_ctl_remove(card, p->qsound_space);
	p->qsound_space = NULL;

	/* cancel pending transfer of QSound parameters */
	spin_lock_irqsave (&p->q_lock, flags);
	p->qpos_changed = 0;
	spin_unlock_irqrestore (&p->q_lock, flags);
}

/*
 * Transfer qsound parameters to CSP,
 * function should be called from interrupt routine
 */
static int snd_sb_csp_qsound_transfer(struct snd_sb_csp * p)
{
	int err = -ENXIO;

	spin_lock(&p->q_lock);
	if (p->running & SNDRV_SB_CSP_ST_QSOUND) {
		set_codec_parameter(p->chip, 0xe0, 0x01);
		/* left channel */
		set_codec_parameter(p->chip, 0x00, p->qpos_left);
		set_codec_parameter(p->chip, 0x02, 0x00);
		/* right channel */
		set_codec_parameter(p->chip, 0x00, p->qpos_right);
		set_codec_parameter(p->chip, 0x03, 0x00);
		err = 0;
	}
	p->qpos_changed = 0;
	spin_unlock(&p->q_lock);
	return err;
}

/* ------------------------------ */

/*
 * proc interface
 */
static int init_proc_entry(struct snd_sb_csp * p, int device)
{
	char name[16];

	sprintf(name, "cspD%d", device);
	snd_card_ro_proc_new(p->chip->card, name, p, info_read);
	return 0;
}

static void info_read(struct snd_info_entry *entry, struct snd_info_buffer *buffer)
{
	struct snd_sb_csp *p = entry->private_data;

	snd_iprintf(buffer, "Creative Signal Processor [v%d.%d]\n", (p->version >> 4), (p->version & 0x0f));
	snd_iprintf(buffer, "State: %cx%c%c%c\n", ((p->running & SNDRV_SB_CSP_ST_QSOUND) ? 'Q' : '-'),
		    ((p->running & SNDRV_SB_CSP_ST_PAUSED) ? 'P' : '-'),
		    ((p->running & SNDRV_SB_CSP_ST_RUNNING) ? 'R' : '-'),
		    ((p->running & SNDRV_SB_CSP_ST_LOADED) ? 'L' : '-'));
	if (p->running & SNDRV_SB_CSP_ST_LOADED) {
		snd_iprintf(buffer, "Codec: %s [func #%d]\n", p->codec_name, p->func_nr);
		snd_iprintf(buffer, "Sample rates: ");
		if (p->acc_rates == SNDRV_SB_CSP_RATE_ALL) {
			snd_iprintf(buffer, "All\n");
		} else {
			snd_iprintf(buffer, "%s%s%s%s\n",
				    ((p->acc_rates & SNDRV_SB_CSP_RATE_8000) ? "8000Hz " : ""),
				    ((p->acc_rates & SNDRV_SB_CSP_RATE_11025) ? "11025Hz " : ""),
				    ((p->acc_rates & SNDRV_SB_CSP_RATE_22050) ? "22050Hz " : ""),
				    ((p->acc_rates & SNDRV_SB_CSP_RATE_44100) ? "44100Hz" : ""));
		}
		if (p->mode == SNDRV_SB_CSP_MODE_QSOUND) {
			snd_iprintf(buffer, "QSound decoder %sabled\n",
				    p->q_enabled ? "en" : "dis");
		} else {
			snd_iprintf(buffer, "PCM format ID: 0x%x (%s/%s) [%s/%s] [%s/%s]\n",
				    p->acc_format,
				    ((p->acc_width & SNDRV_SB_CSP_SAMPLE_16BIT) ? "16bit" : "-"),
				    ((p->acc_width & SNDRV_SB_CSP_SAMPLE_8BIT) ? "8bit" : "-"),
				    ((p->acc_channels & SNDRV_SB_CSP_MONO) ? "mono" : "-"),
				    ((p->acc_channels & SNDRV_SB_CSP_STEREO) ? "stereo" : "-"),
				    ((p->mode & SNDRV_SB_CSP_MODE_DSP_WRITE) ? "playback" : "-"),
				    ((p->mode & SNDRV_SB_CSP_MODE_DSP_READ) ? "capture" : "-"));
		}
	}
	if (p->running & SNDRV_SB_CSP_ST_AUTO) {
		snd_iprintf(buffer, "Autoloaded Mu-Law, A-Law or Ima-ADPCM hardware codec\n");
	}
	if (p->running & SNDRV_SB_CSP_ST_RUNNING) {
		snd_iprintf(buffer, "Processing %dbit %s PCM samples\n",
			    ((p->run_width & SNDRV_SB_CSP_SAMPLE_16BIT) ? 16 : 8),
			    ((p->run_channels & SNDRV_SB_CSP_MONO) ? "mono" : "stereo"));
	}
	if (p->running & SNDRV_SB_CSP_ST_QSOUND) {
		snd_iprintf(buffer, "Qsound position: left = 0x%x, right = 0x%x\n",
			    p->qpos_left, p->qpos_right);
	}
}

/* */

EXPORT_SYMBOL(snd_sb_csp_new);
