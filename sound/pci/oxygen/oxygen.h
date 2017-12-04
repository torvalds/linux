/* SPDX-License-Identifier: GPL-2.0 */
#ifndef OXYGEN_H_INCLUDED
#define OXYGEN_H_INCLUDED

#include <linux/mutex.h>
#include <linux/spinlock.h>
#include <linux/wait.h>
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

#define OXYGEN_MCLKS(f_single, f_double, f_quad) ((MCLK_##f_single << 0) | \
						  (MCLK_##f_double << 2) | \
						  (MCLK_##f_quad   << 4))

#define OXYGEN_IO_SIZE	0x100

#define OXYGEN_EEPROM_ID	0x434d	/* "CM" */

/* model-specific configuration of outputs/inputs */
#define PLAYBACK_0_TO_I2S	0x0001
     /* PLAYBACK_0_TO_AC97_0		not implemented */
#define PLAYBACK_1_TO_SPDIF	0x0004
#define PLAYBACK_2_TO_AC97_1	0x0008
#define CAPTURE_0_FROM_I2S_1	0x0010
#define CAPTURE_0_FROM_I2S_2	0x0020
     /* CAPTURE_0_FROM_AC97_0		not implemented */
#define CAPTURE_1_FROM_SPDIF	0x0080
#define CAPTURE_2_FROM_I2S_2	0x0100
#define CAPTURE_2_FROM_AC97_1	0x0200
#define CAPTURE_3_FROM_I2S_3	0x0400
#define MIDI_OUTPUT		0x0800
#define MIDI_INPUT		0x1000
#define AC97_CD_INPUT		0x2000
#define AC97_FMIC_SWITCH	0x4000

enum {
	CONTROL_SPDIF_PCM,
	CONTROL_SPDIF_INPUT_BITS,
	CONTROL_MIC_CAPTURE_SWITCH,
	CONTROL_LINE_CAPTURE_SWITCH,
	CONTROL_CD_CAPTURE_SWITCH,
	CONTROL_AUX_CAPTURE_SWITCH,
	CONTROL_COUNT
};

#define OXYGEN_PCI_SUBID(sv, sd) \
	.vendor = PCI_VENDOR_ID_CMEDIA, \
	.device = 0x8788, \
	.subvendor = sv, \
	.subdevice = sd

#define BROKEN_EEPROM_DRIVER_DATA ((unsigned long)-1)
#define OXYGEN_PCI_SUBID_BROKEN_EEPROM \
	OXYGEN_PCI_SUBID(PCI_VENDOR_ID_CMEDIA, 0x8788), \
	.driver_data = BROKEN_EEPROM_DRIVER_DATA

struct pci_dev;
struct pci_device_id;
struct snd_card;
struct snd_pcm_substream;
struct snd_pcm_hardware;
struct snd_pcm_hw_params;
struct snd_kcontrol_new;
struct snd_rawmidi;
struct snd_info_buffer;
struct oxygen;

struct oxygen_model {
	const char *shortname;
	const char *longname;
	const char *chip;
	void (*init)(struct oxygen *chip);
	int (*control_filter)(struct snd_kcontrol_new *template);
	int (*mixer_init)(struct oxygen *chip);
	void (*cleanup)(struct oxygen *chip);
	void (*suspend)(struct oxygen *chip);
	void (*resume)(struct oxygen *chip);
	void (*pcm_hardware_filter)(unsigned int channel,
				    struct snd_pcm_hardware *hardware);
	void (*set_dac_params)(struct oxygen *chip,
			       struct snd_pcm_hw_params *params);
	void (*set_adc_params)(struct oxygen *chip,
			       struct snd_pcm_hw_params *params);
	void (*update_dac_volume)(struct oxygen *chip);
	void (*update_dac_mute)(struct oxygen *chip);
	void (*update_center_lfe_mix)(struct oxygen *chip, bool mixed);
	unsigned int (*adjust_dac_routing)(struct oxygen *chip,
					   unsigned int play_routing);
	void (*gpio_changed)(struct oxygen *chip);
	void (*uart_input)(struct oxygen *chip);
	void (*ac97_switch)(struct oxygen *chip,
			    unsigned int reg, unsigned int mute);
	void (*dump_registers)(struct oxygen *chip,
			       struct snd_info_buffer *buffer);
	const unsigned int *dac_tlv;
	size_t model_data_size;
	unsigned int device_config;
	u8 dac_channels_pcm;
	u8 dac_channels_mixer;
	u8 dac_volume_min;
	u8 dac_volume_max;
	u8 misc_flags;
	u8 function_flags;
	u8 dac_mclks;
	u8 adc_mclks;
	u16 dac_i2s_format;
	u16 adc_i2s_format;
};

struct oxygen {
	unsigned long addr;
	spinlock_t reg_lock;
	struct mutex mutex;
	struct snd_card *card;
	struct pci_dev *pci;
	struct snd_rawmidi *midi;
	int irq;
	void *model_data;
	unsigned int interrupt_mask;
	u8 dac_volume[8];
	u8 dac_mute;
	u8 pcm_active;
	u8 pcm_running;
	u8 dac_routing;
	u8 spdif_playback_enable;
	u8 has_ac97_0;
	u8 has_ac97_1;
	u32 spdif_bits;
	u32 spdif_pcm_bits;
	struct snd_pcm_substream *streams[PCM_COUNT];
	struct snd_kcontrol *controls[CONTROL_COUNT];
	struct work_struct spdif_input_bits_work;
	struct work_struct gpio_work;
	wait_queue_head_t ac97_waitqueue;
	union {
		u8 _8[OXYGEN_IO_SIZE];
		__le16 _16[OXYGEN_IO_SIZE / 2];
		__le32 _32[OXYGEN_IO_SIZE / 4];
	} saved_registers;
	u16 saved_ac97_registers[2][0x40];
	unsigned int uart_input_count;
	u8 uart_input[32];
	struct oxygen_model model;
};

/* oxygen_lib.c */

int oxygen_pci_probe(struct pci_dev *pci, int index, char *id,
		     struct module *owner,
		     const struct pci_device_id *ids,
		     int (*get_model)(struct oxygen *chip,
				      const struct pci_device_id *id
				     )
		    );
void oxygen_pci_remove(struct pci_dev *pci);
#ifdef CONFIG_PM_SLEEP
extern const struct dev_pm_ops oxygen_pci_pm;
#endif
void oxygen_pci_shutdown(struct pci_dev *pci);

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

int oxygen_write_spi(struct oxygen *chip, u8 control, unsigned int data);
void oxygen_write_i2c(struct oxygen *chip, u8 device, u8 map, u8 data);

void oxygen_reset_uart(struct oxygen *chip);
void oxygen_write_uart(struct oxygen *chip, u8 data);

u16 oxygen_read_eeprom(struct oxygen *chip, unsigned int index);
void oxygen_write_eeprom(struct oxygen *chip, unsigned int index, u16 value);

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
