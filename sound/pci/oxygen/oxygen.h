#ifndef OXYGEN_H_INCLUDED
#define OXYGEN_H_INCLUDED

#include <linux/mutex.h>
#include <linux/spinlock.h>
#include <linux/workqueue.h>
#include "oxygen_regs.h"

/* 1 << PCM_x == OXYGEN_CHANNEL_x */
#define PCM_A		0
#define PCM_B		1
#define PCM_C		2
#define PCM_SPDIF	3
#define PCM_MULTICH	4
#define PCM_AC97	5
#define PCM_COUNT	6

#define OXYGEN_PCI_SUBID(sv, sd) \
	.vendor = PCI_VENDOR_ID_CMEDIA, \
	.device = 0x8788, \
	.subvendor = sv, \
	.subdevice = sd

struct pci_dev;
struct snd_card;
struct snd_pcm_substream;
struct snd_pcm_hw_params;
struct snd_rawmidi;
struct oxygen_model;

struct oxygen {
	unsigned long addr;
	spinlock_t reg_lock;
	struct mutex mutex;
	struct snd_card *card;
	struct pci_dev *pci;
	struct snd_rawmidi *midi;
	int irq;
	const struct oxygen_model *model;
	unsigned int interrupt_mask;
	u8 dac_volume[8];
	u8 dac_mute;
	u8 pcm_active;
	u8 pcm_running;
	u8 dac_routing;
	u8 spdif_playback_enable;
	u8 ak4396_reg1;
	u8 revision;
	u8 has_2nd_ac97_codec;
	u32 spdif_bits;
	u32 spdif_pcm_bits;
	struct snd_pcm_substream *streams[PCM_COUNT];
	struct snd_kcontrol *spdif_pcm_ctl;
	struct snd_kcontrol *spdif_input_bits_ctl;
	struct work_struct spdif_input_bits_work;
};

struct oxygen_model {
	const char *shortname;
	const char *longname;
	const char *chip;
	struct module *owner;
	void (*init)(struct oxygen *chip);
	int (*mixer_init)(struct oxygen *chip);
	void (*cleanup)(struct oxygen *chip);
	void (*set_dac_params)(struct oxygen *chip,
			       struct snd_pcm_hw_params *params);
	void (*set_adc_params)(struct oxygen *chip,
			       struct snd_pcm_hw_params *params);
	void (*update_dac_volume)(struct oxygen *chip);
	void (*update_dac_mute)(struct oxygen *chip);
	const unsigned int *dac_tlv;
	u8 record_from_dma_b;
	u8 cd_in_from_video_in;
	u8 dac_minimum_volume;
};

/* oxygen_lib.c */

int oxygen_pci_probe(struct pci_dev *pci, int index, char *id,
		     const struct oxygen_model *model);
void oxygen_pci_remove(struct pci_dev *pci);

/* oxygen_mixer.c */

int oxygen_mixer_init(struct oxygen *chip);
void oxygen_update_dac_routing(struct oxygen *chip);
void oxygen_update_spdif_source(struct oxygen *chip);

/* oxygen_pcm.c */

int oxygen_pcm_init(struct oxygen *chip);

/* oxygen_io.c */

u8 oxygen_read8(struct oxygen *chip, unsigned int reg);
u16 oxygen_read16(struct oxygen *chip, unsigned int reg);
u32 oxygen_read32(struct oxygen *chip, unsigned int reg);
void oxygen_write8(struct oxygen *chip, unsigned int reg, u8 value);
void oxygen_write16(struct oxygen *chip, unsigned int reg, u16 value);
void oxygen_write32(struct oxygen *chip, unsigned int reg, u32 value);
void oxygen_write8_masked(struct oxygen *chip, unsigned int reg,
			  u8 value, u8 mask);
void oxygen_write16_masked(struct oxygen *chip, unsigned int reg,
			   u16 value, u16 mask);
void oxygen_write32_masked(struct oxygen *chip, unsigned int reg,
			   u32 value, u32 mask);

u16 oxygen_read_ac97(struct oxygen *chip, unsigned int codec,
		     unsigned int index);
void oxygen_write_ac97(struct oxygen *chip, unsigned int codec,
		       unsigned int index, u16 data);
void oxygen_write_ac97_masked(struct oxygen *chip, unsigned int codec,
			      unsigned int index, u16 data, u16 mask);

void oxygen_write_spi(struct oxygen *chip, u8 control, unsigned int data);

static inline void oxygen_set_bits8(struct oxygen *chip,
				    unsigned int reg, u8 value)
{
	oxygen_write8_masked(chip, reg, value, value);
}

static inline void oxygen_set_bits16(struct oxygen *chip,
				     unsigned int reg, u16 value)
{
	oxygen_write16_masked(chip, reg, value, value);
}

static inline void oxygen_set_bits32(struct oxygen *chip,
				     unsigned int reg, u32 value)
{
	oxygen_write32_masked(chip, reg, value, value);
}

static inline void oxygen_clear_bits8(struct oxygen *chip,
				      unsigned int reg, u8 value)
{
	oxygen_write8_masked(chip, reg, 0, value);
}

static inline void oxygen_clear_bits16(struct oxygen *chip,
				       unsigned int reg, u16 value)
{
	oxygen_write16_masked(chip, reg, 0, value);
}

static inline void oxygen_clear_bits32(struct oxygen *chip,
				       unsigned int reg, u32 value)
{
	oxygen_write32_masked(chip, reg, 0, value);
}

static inline void oxygen_ac97_set_bits(struct oxygen *chip, unsigned int codec,
					unsigned int index, u16 value)
{
	oxygen_write_ac97_masked(chip, codec, index, value, value);
}

static inline void oxygen_ac97_clear_bits(struct oxygen *chip,
					  unsigned int codec,
					  unsigned int index, u16 value)
{
	oxygen_write_ac97_masked(chip, codec, index, 0, value);
}

#endif
