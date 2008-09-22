/*
 * C-Media CMI8788 driver - main driver module
 *
 * Copyright (c) Clemens Ladisch <clemens@ladisch.de>
 *
 *
 *  This driver is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License, version 2.
 *
 *  This driver is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this driver; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307 USA
 */

#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/mutex.h>
#include <linux/pci.h>
#include <sound/ac97_codec.h>
#include <sound/asoundef.h>
#include <sound/core.h>
#include <sound/info.h>
#include <sound/mpu401.h>
#include <sound/pcm.h>
#include "oxygen.h"
#include "cm9780.h"

MODULE_AUTHOR("Clemens Ladisch <clemens@ladisch.de>");
MODULE_DESCRIPTION("C-Media CMI8788 helper library");
MODULE_LICENSE("GPL v2");


static irqreturn_t oxygen_interrupt(int dummy, void *dev_id)
{
	struct oxygen *chip = dev_id;
	unsigned int status, clear, elapsed_streams, i;

	status = oxygen_read16(chip, OXYGEN_INTERRUPT_STATUS);
	if (!status)
		return IRQ_NONE;

	spin_lock(&chip->reg_lock);

	clear = status & (OXYGEN_CHANNEL_A |
			  OXYGEN_CHANNEL_B |
			  OXYGEN_CHANNEL_C |
			  OXYGEN_CHANNEL_SPDIF |
			  OXYGEN_CHANNEL_MULTICH |
			  OXYGEN_CHANNEL_AC97 |
			  OXYGEN_INT_SPDIF_IN_DETECT |
			  OXYGEN_INT_GPIO |
			  OXYGEN_INT_AC97);
	if (clear) {
		if (clear & OXYGEN_INT_SPDIF_IN_DETECT)
			chip->interrupt_mask &= ~OXYGEN_INT_SPDIF_IN_DETECT;
		oxygen_write16(chip, OXYGEN_INTERRUPT_MASK,
			       chip->interrupt_mask & ~clear);
		oxygen_write16(chip, OXYGEN_INTERRUPT_MASK,
			       chip->interrupt_mask);
	}

	elapsed_streams = status & chip->pcm_running;

	spin_unlock(&chip->reg_lock);

	for (i = 0; i < PCM_COUNT; ++i)
		if ((elapsed_streams & (1 << i)) && chip->streams[i])
			snd_pcm_period_elapsed(chip->streams[i]);

	if (status & OXYGEN_INT_SPDIF_IN_DETECT) {
		spin_lock(&chip->reg_lock);
		i = oxygen_read32(chip, OXYGEN_SPDIF_CONTROL);
		if (i & (OXYGEN_SPDIF_SENSE_INT | OXYGEN_SPDIF_LOCK_INT |
			 OXYGEN_SPDIF_RATE_INT)) {
			/* write the interrupt bit(s) to clear */
			oxygen_write32(chip, OXYGEN_SPDIF_CONTROL, i);
			schedule_work(&chip->spdif_input_bits_work);
		}
		spin_unlock(&chip->reg_lock);
	}

	if (status & OXYGEN_INT_GPIO)
		schedule_work(&chip->gpio_work);

	if ((status & OXYGEN_INT_MIDI) && chip->midi)
		snd_mpu401_uart_interrupt(0, chip->midi->private_data);

	if (status & OXYGEN_INT_AC97)
		wake_up(&chip->ac97_waitqueue);

	return IRQ_HANDLED;
}

static void oxygen_spdif_input_bits_changed(struct work_struct *work)
{
	struct oxygen *chip = container_of(work, struct oxygen,
					   spdif_input_bits_work);
	u32 reg;

	/*
	 * This function gets called when there is new activity on the SPDIF
	 * input, or when we lose lock on the input signal, or when the rate
	 * changes.
	 */
	msleep(1);
	spin_lock_irq(&chip->reg_lock);
	reg = oxygen_read32(chip, OXYGEN_SPDIF_CONTROL);
	if ((reg & (OXYGEN_SPDIF_SENSE_STATUS |
		    OXYGEN_SPDIF_LOCK_STATUS))
	    == OXYGEN_SPDIF_SENSE_STATUS) {
		/*
		 * If we detect activity on the SPDIF input but cannot lock to
		 * a signal, the clock bit is likely to be wrong.
		 */
		reg ^= OXYGEN_SPDIF_IN_CLOCK_MASK;
		oxygen_write32(chip, OXYGEN_SPDIF_CONTROL, reg);
		spin_unlock_irq(&chip->reg_lock);
		msleep(1);
		spin_lock_irq(&chip->reg_lock);
		reg = oxygen_read32(chip, OXYGEN_SPDIF_CONTROL);
		if ((reg & (OXYGEN_SPDIF_SENSE_STATUS |
			    OXYGEN_SPDIF_LOCK_STATUS))
		    == OXYGEN_SPDIF_SENSE_STATUS) {
			/* nothing detected with either clock; give up */
			if ((reg & OXYGEN_SPDIF_IN_CLOCK_MASK)
			    == OXYGEN_SPDIF_IN_CLOCK_192) {
				/*
				 * Reset clock to <= 96 kHz because this is
				 * more likely to be received next time.
				 */
				reg &= ~OXYGEN_SPDIF_IN_CLOCK_MASK;
				reg |= OXYGEN_SPDIF_IN_CLOCK_96;
				oxygen_write32(chip, OXYGEN_SPDIF_CONTROL, reg);
			}
		}
	}
	spin_unlock_irq(&chip->reg_lock);

	if (chip->controls[CONTROL_SPDIF_INPUT_BITS]) {
		spin_lock_irq(&chip->reg_lock);
		chip->interrupt_mask |= OXYGEN_INT_SPDIF_IN_DETECT;
		oxygen_write16(chip, OXYGEN_INTERRUPT_MASK,
			       chip->interrupt_mask);
		spin_unlock_irq(&chip->reg_lock);

		/*
		 * We don't actually know that any channel status bits have
		 * changed, but let's send a notification just to be sure.
		 */
		snd_ctl_notify(chip->card, SNDRV_CTL_EVENT_MASK_VALUE,
			       &chip->controls[CONTROL_SPDIF_INPUT_BITS]->id);
	}
}

static void oxygen_gpio_changed(struct work_struct *work)
{
	struct oxygen *chip = container_of(work, struct oxygen, gpio_work);

	if (chip->model.gpio_changed)
		chip->model.gpio_changed(chip);
}

#ifdef CONFIG_PROC_FS
static void oxygen_proc_read(struct snd_info_entry *entry,
			     struct snd_info_buffer *buffer)
{
	struct oxygen *chip = entry->private_data;
	int i, j;

	snd_iprintf(buffer, "CMI8788\n\n");
	for (i = 0; i < OXYGEN_IO_SIZE; i += 0x10) {
		snd_iprintf(buffer, "%02x:", i);
		for (j = 0; j < 0x10; ++j)
			snd_iprintf(buffer, " %02x", oxygen_read8(chip, i + j));
		snd_iprintf(buffer, "\n");
	}
	if (mutex_lock_interruptible(&chip->mutex) < 0)
		return;
	if (chip->has_ac97_0) {
		snd_iprintf(buffer, "\nAC97\n");
		for (i = 0; i < 0x80; i += 0x10) {
			snd_iprintf(buffer, "%02x:", i);
			for (j = 0; j < 0x10; j += 2)
				snd_iprintf(buffer, " %04x",
					    oxygen_read_ac97(chip, 0, i + j));
			snd_iprintf(buffer, "\n");
		}
	}
	if (chip->has_ac97_1) {
		snd_iprintf(buffer, "\nAC97 2\n");
		for (i = 0; i < 0x80; i += 0x10) {
			snd_iprintf(buffer, "%02x:", i);
			for (j = 0; j < 0x10; j += 2)
				snd_iprintf(buffer, " %04x",
					    oxygen_read_ac97(chip, 1, i + j));
			snd_iprintf(buffer, "\n");
		}
	}
	mutex_unlock(&chip->mutex);
}

static void oxygen_proc_init(struct oxygen *chip)
{
	struct snd_info_entry *entry;

	if (!snd_card_proc_new(chip->card, "cmi8788", &entry))
		snd_info_set_text_ops(entry, chip, oxygen_proc_read);
}
#else
#define oxygen_proc_init(chip)
#endif

static void oxygen_init(struct oxygen *chip)
{
	unsigned int i;

	chip->dac_routing = 1;
	for (i = 0; i < 8; ++i)
		chip->dac_volume[i] = chip->model.dac_volume_min;
	chip->dac_mute = 1;
	chip->spdif_playback_enable = 1;
	chip->spdif_bits = OXYGEN_SPDIF_C | OXYGEN_SPDIF_ORIGINAL |
		(IEC958_AES1_CON_PCM_CODER << OXYGEN_SPDIF_CATEGORY_SHIFT);
	chip->spdif_pcm_bits = chip->spdif_bits;

	if (oxygen_read8(chip, OXYGEN_REVISION) & OXYGEN_REVISION_2)
		chip->revision = 2;
	else
		chip->revision = 1;

	if (chip->revision == 1)
		oxygen_set_bits8(chip, OXYGEN_MISC,
				 OXYGEN_MISC_PCI_MEM_W_1_CLOCK);

	i = oxygen_read16(chip, OXYGEN_AC97_CONTROL);
	chip->has_ac97_0 = (i & OXYGEN_AC97_CODEC_0) != 0;
	chip->has_ac97_1 = (i & OXYGEN_AC97_CODEC_1) != 0;

	oxygen_write8_masked(chip, OXYGEN_FUNCTION,
			     OXYGEN_FUNCTION_RESET_CODEC |
			     chip->model.function_flags,
			     OXYGEN_FUNCTION_RESET_CODEC |
			     OXYGEN_FUNCTION_2WIRE_SPI_MASK |
			     OXYGEN_FUNCTION_ENABLE_SPI_4_5);
	oxygen_write8(chip, OXYGEN_DMA_STATUS, 0);
	oxygen_write8(chip, OXYGEN_DMA_PAUSE, 0);
	oxygen_write8(chip, OXYGEN_PLAY_CHANNELS,
		      OXYGEN_PLAY_CHANNELS_2 |
		      OXYGEN_DMA_A_BURST_8 |
		      OXYGEN_DMA_MULTICH_BURST_8);
	oxygen_write16(chip, OXYGEN_INTERRUPT_MASK, 0);
	oxygen_write8_masked(chip, OXYGEN_MISC,
			     chip->model.misc_flags,
			     OXYGEN_MISC_WRITE_PCI_SUBID |
			     OXYGEN_MISC_REC_C_FROM_SPDIF |
			     OXYGEN_MISC_REC_B_FROM_AC97 |
			     OXYGEN_MISC_REC_A_FROM_MULTICH |
			     OXYGEN_MISC_MIDI);
	oxygen_write8(chip, OXYGEN_REC_FORMAT,
		      (OXYGEN_FORMAT_16 << OXYGEN_REC_FORMAT_A_SHIFT) |
		      (OXYGEN_FORMAT_16 << OXYGEN_REC_FORMAT_B_SHIFT) |
		      (OXYGEN_FORMAT_16 << OXYGEN_REC_FORMAT_C_SHIFT));
	oxygen_write8(chip, OXYGEN_PLAY_FORMAT,
		      (OXYGEN_FORMAT_16 << OXYGEN_SPDIF_FORMAT_SHIFT) |
		      (OXYGEN_FORMAT_16 << OXYGEN_MULTICH_FORMAT_SHIFT));
	oxygen_write8(chip, OXYGEN_REC_CHANNELS, OXYGEN_REC_CHANNELS_2_2_2);
	oxygen_write16(chip, OXYGEN_I2S_MULTICH_FORMAT,
		       OXYGEN_RATE_48000 | chip->model.dac_i2s_format |
		       OXYGEN_I2S_MCLK_256 | OXYGEN_I2S_BITS_16 |
		       OXYGEN_I2S_MASTER | OXYGEN_I2S_BCLK_64);
	if (chip->model.device_config & CAPTURE_0_FROM_I2S_1)
		oxygen_write16(chip, OXYGEN_I2S_A_FORMAT,
			       OXYGEN_RATE_48000 | chip->model.adc_i2s_format |
			       OXYGEN_I2S_MCLK_256 | OXYGEN_I2S_BITS_16 |
			       OXYGEN_I2S_MASTER | OXYGEN_I2S_BCLK_64);
	else
		oxygen_write16(chip, OXYGEN_I2S_A_FORMAT,
			       OXYGEN_I2S_MASTER | OXYGEN_I2S_MUTE_MCLK);
	if (chip->model.device_config & (CAPTURE_0_FROM_I2S_2 |
					 CAPTURE_2_FROM_I2S_2))
		oxygen_write16(chip, OXYGEN_I2S_B_FORMAT,
			       OXYGEN_RATE_48000 | chip->model.adc_i2s_format |
			       OXYGEN_I2S_MCLK_256 | OXYGEN_I2S_BITS_16 |
			       OXYGEN_I2S_MASTER | OXYGEN_I2S_BCLK_64);
	else
		oxygen_write16(chip, OXYGEN_I2S_B_FORMAT,
			       OXYGEN_I2S_MASTER | OXYGEN_I2S_MUTE_MCLK);
	oxygen_write16(chip, OXYGEN_I2S_C_FORMAT,
		       OXYGEN_I2S_MASTER | OXYGEN_I2S_MUTE_MCLK);
	oxygen_clear_bits32(chip, OXYGEN_SPDIF_CONTROL,
			    OXYGEN_SPDIF_OUT_ENABLE |
			    OXYGEN_SPDIF_LOOPBACK);
	if (chip->model.device_config & CAPTURE_1_FROM_SPDIF)
		oxygen_write32_masked(chip, OXYGEN_SPDIF_CONTROL,
				      OXYGEN_SPDIF_SENSE_MASK |
				      OXYGEN_SPDIF_LOCK_MASK |
				      OXYGEN_SPDIF_RATE_MASK |
				      OXYGEN_SPDIF_LOCK_PAR |
				      OXYGEN_SPDIF_IN_CLOCK_96,
				      OXYGEN_SPDIF_SENSE_MASK |
				      OXYGEN_SPDIF_LOCK_MASK |
				      OXYGEN_SPDIF_RATE_MASK |
				      OXYGEN_SPDIF_SENSE_PAR |
				      OXYGEN_SPDIF_LOCK_PAR |
				      OXYGEN_SPDIF_IN_CLOCK_MASK);
	else
		oxygen_clear_bits32(chip, OXYGEN_SPDIF_CONTROL,
				    OXYGEN_SPDIF_SENSE_MASK |
				    OXYGEN_SPDIF_LOCK_MASK |
				    OXYGEN_SPDIF_RATE_MASK);
	oxygen_write32(chip, OXYGEN_SPDIF_OUTPUT_BITS, chip->spdif_bits);
	oxygen_write16(chip, OXYGEN_2WIRE_BUS_STATUS,
		       OXYGEN_2WIRE_LENGTH_8 |
		       OXYGEN_2WIRE_INTERRUPT_MASK |
		       OXYGEN_2WIRE_SPEED_STANDARD);
	oxygen_clear_bits8(chip, OXYGEN_MPU401_CONTROL, OXYGEN_MPU401_LOOPBACK);
	oxygen_write8(chip, OXYGEN_GPI_INTERRUPT_MASK, 0);
	oxygen_write16(chip, OXYGEN_GPIO_INTERRUPT_MASK, 0);
	oxygen_write16(chip, OXYGEN_PLAY_ROUTING,
		       OXYGEN_PLAY_MULTICH_I2S_DAC |
		       OXYGEN_PLAY_SPDIF_SPDIF |
		       (0 << OXYGEN_PLAY_DAC0_SOURCE_SHIFT) |
		       (1 << OXYGEN_PLAY_DAC1_SOURCE_SHIFT) |
		       (2 << OXYGEN_PLAY_DAC2_SOURCE_SHIFT) |
		       (3 << OXYGEN_PLAY_DAC3_SOURCE_SHIFT));
	oxygen_write8(chip, OXYGEN_REC_ROUTING,
		      OXYGEN_REC_A_ROUTE_I2S_ADC_1 |
		      OXYGEN_REC_B_ROUTE_I2S_ADC_2 |
		      OXYGEN_REC_C_ROUTE_SPDIF);
	oxygen_write8(chip, OXYGEN_ADC_MONITOR, 0);
	oxygen_write8(chip, OXYGEN_A_MONITOR_ROUTING,
		      (0 << OXYGEN_A_MONITOR_ROUTE_0_SHIFT) |
		      (1 << OXYGEN_A_MONITOR_ROUTE_1_SHIFT) |
		      (2 << OXYGEN_A_MONITOR_ROUTE_2_SHIFT) |
		      (3 << OXYGEN_A_MONITOR_ROUTE_3_SHIFT));

	if (chip->has_ac97_0 | chip->has_ac97_1)
		oxygen_write8(chip, OXYGEN_AC97_INTERRUPT_MASK,
			      OXYGEN_AC97_INT_READ_DONE |
			      OXYGEN_AC97_INT_WRITE_DONE);
	else
		oxygen_write8(chip, OXYGEN_AC97_INTERRUPT_MASK, 0);
	oxygen_write32(chip, OXYGEN_AC97_OUT_CONFIG, 0);
	oxygen_write32(chip, OXYGEN_AC97_IN_CONFIG, 0);
	if (!(chip->has_ac97_0 | chip->has_ac97_1))
		oxygen_set_bits16(chip, OXYGEN_AC97_CONTROL,
				  OXYGEN_AC97_CLOCK_DISABLE);
	if (!chip->has_ac97_0) {
		oxygen_set_bits16(chip, OXYGEN_AC97_CONTROL,
				  OXYGEN_AC97_NO_CODEC_0);
	} else {
		oxygen_write_ac97(chip, 0, AC97_RESET, 0);
		msleep(1);
		oxygen_ac97_set_bits(chip, 0, CM9780_GPIO_SETUP,
				     CM9780_GPIO0IO | CM9780_GPIO1IO);
		oxygen_ac97_set_bits(chip, 0, CM9780_MIXER,
				     CM9780_BSTSEL | CM9780_STRO_MIC |
				     CM9780_MIX2FR | CM9780_PCBSW);
		oxygen_ac97_set_bits(chip, 0, CM9780_JACK,
				     CM9780_RSOE | CM9780_CBOE |
				     CM9780_SSOE | CM9780_FROE |
				     CM9780_MIC2MIC | CM9780_LI2LI);
		oxygen_write_ac97(chip, 0, AC97_MASTER, 0x0000);
		oxygen_write_ac97(chip, 0, AC97_PC_BEEP, 0x8000);
		oxygen_write_ac97(chip, 0, AC97_MIC, 0x8808);
		oxygen_write_ac97(chip, 0, AC97_LINE, 0x0808);
		oxygen_write_ac97(chip, 0, AC97_CD, 0x8808);
		oxygen_write_ac97(chip, 0, AC97_VIDEO, 0x8808);
		oxygen_write_ac97(chip, 0, AC97_AUX, 0x8808);
		oxygen_write_ac97(chip, 0, AC97_REC_GAIN, 0x8000);
		oxygen_write_ac97(chip, 0, AC97_CENTER_LFE_MASTER, 0x8080);
		oxygen_write_ac97(chip, 0, AC97_SURROUND_MASTER, 0x8080);
		oxygen_ac97_clear_bits(chip, 0, CM9780_GPIO_STATUS,
				       CM9780_GPO0);
		/* power down unused ADCs and DACs */
		oxygen_ac97_set_bits(chip, 0, AC97_POWERDOWN,
				     AC97_PD_PR0 | AC97_PD_PR1);
		oxygen_ac97_set_bits(chip, 0, AC97_EXTENDED_STATUS,
				     AC97_EA_PRI | AC97_EA_PRJ | AC97_EA_PRK);
	}
	if (chip->has_ac97_1) {
		oxygen_set_bits32(chip, OXYGEN_AC97_OUT_CONFIG,
				  OXYGEN_AC97_CODEC1_SLOT3 |
				  OXYGEN_AC97_CODEC1_SLOT4);
		oxygen_write_ac97(chip, 1, AC97_RESET, 0);
		msleep(1);
		oxygen_write_ac97(chip, 1, AC97_MASTER, 0x0000);
		oxygen_write_ac97(chip, 1, AC97_HEADPHONE, 0x8000);
		oxygen_write_ac97(chip, 1, AC97_PC_BEEP, 0x8000);
		oxygen_write_ac97(chip, 1, AC97_MIC, 0x8808);
		oxygen_write_ac97(chip, 1, AC97_LINE, 0x8808);
		oxygen_write_ac97(chip, 1, AC97_CD, 0x8808);
		oxygen_write_ac97(chip, 1, AC97_VIDEO, 0x8808);
		oxygen_write_ac97(chip, 1, AC97_AUX, 0x8808);
		oxygen_write_ac97(chip, 1, AC97_PCM, 0x0808);
		oxygen_write_ac97(chip, 1, AC97_REC_SEL, 0x0000);
		oxygen_write_ac97(chip, 1, AC97_REC_GAIN, 0x0000);
		oxygen_ac97_set_bits(chip, 1, 0x6a, 0x0040);
	}
}

static void oxygen_card_free(struct snd_card *card)
{
	struct oxygen *chip = card->private_data;

	spin_lock_irq(&chip->reg_lock);
	chip->interrupt_mask = 0;
	chip->pcm_running = 0;
	oxygen_write16(chip, OXYGEN_DMA_STATUS, 0);
	oxygen_write16(chip, OXYGEN_INTERRUPT_MASK, 0);
	spin_unlock_irq(&chip->reg_lock);
	if (chip->irq >= 0)
		free_irq(chip->irq, chip);
	flush_scheduled_work();
	chip->model.cleanup(chip);
	mutex_destroy(&chip->mutex);
	pci_release_regions(chip->pci);
	pci_disable_device(chip->pci);
}

int oxygen_pci_probe(struct pci_dev *pci, int index, char *id,
		     const struct oxygen_model *model,
		     unsigned long driver_data)
{
	struct snd_card *card;
	struct oxygen *chip;
	int err;

	card = snd_card_new(index, id, model->owner,
			    sizeof *chip + model->model_data_size);
	if (!card)
		return -ENOMEM;

	chip = card->private_data;
	chip->card = card;
	chip->pci = pci;
	chip->irq = -1;
	chip->model = *model;
	chip->model_data = chip + 1;
	spin_lock_init(&chip->reg_lock);
	mutex_init(&chip->mutex);
	INIT_WORK(&chip->spdif_input_bits_work,
		  oxygen_spdif_input_bits_changed);
	INIT_WORK(&chip->gpio_work, oxygen_gpio_changed);
	init_waitqueue_head(&chip->ac97_waitqueue);

	err = pci_enable_device(pci);
	if (err < 0)
		goto err_card;

	err = pci_request_regions(pci, model->chip);
	if (err < 0) {
		snd_printk(KERN_ERR "cannot reserve PCI resources\n");
		goto err_pci_enable;
	}

	if (!(pci_resource_flags(pci, 0) & IORESOURCE_IO) ||
	    pci_resource_len(pci, 0) < OXYGEN_IO_SIZE) {
		snd_printk(KERN_ERR "invalid PCI I/O range\n");
		err = -ENXIO;
		goto err_pci_regions;
	}
	chip->addr = pci_resource_start(pci, 0);

	pci_set_master(pci);
	snd_card_set_dev(card, &pci->dev);
	card->private_free = oxygen_card_free;

	if (chip->model.probe) {
		err = chip->model.probe(chip, driver_data);
		if (err < 0)
			goto err_card;
	}
	oxygen_init(chip);
	chip->model.init(chip);

	err = request_irq(pci->irq, oxygen_interrupt, IRQF_SHARED,
			  chip->model.chip, chip);
	if (err < 0) {
		snd_printk(KERN_ERR "cannot grab interrupt %d\n", pci->irq);
		goto err_card;
	}
	chip->irq = pci->irq;

	strcpy(card->driver, chip->model.chip);
	strcpy(card->shortname, chip->model.shortname);
	sprintf(card->longname, "%s (rev %u) at %#lx, irq %i",
		chip->model.longname, chip->revision, chip->addr, chip->irq);
	strcpy(card->mixername, chip->model.chip);
	snd_component_add(card, chip->model.chip);

	err = oxygen_pcm_init(chip);
	if (err < 0)
		goto err_card;

	err = oxygen_mixer_init(chip);
	if (err < 0)
		goto err_card;

	if (chip->model.device_config & (MIDI_OUTPUT | MIDI_INPUT)) {
		unsigned int info_flags = MPU401_INFO_INTEGRATED;
		if (chip->model.device_config & MIDI_OUTPUT)
			info_flags |= MPU401_INFO_OUTPUT;
		if (chip->model.device_config & MIDI_INPUT)
			info_flags |= MPU401_INFO_INPUT;
		err = snd_mpu401_uart_new(card, 0, MPU401_HW_CMIPCI,
					  chip->addr + OXYGEN_MPU401,
					  info_flags, 0, 0,
					  &chip->midi);
		if (err < 0)
			goto err_card;
	}

	oxygen_proc_init(chip);

	spin_lock_irq(&chip->reg_lock);
	if (chip->model.device_config & CAPTURE_1_FROM_SPDIF)
		chip->interrupt_mask |= OXYGEN_INT_SPDIF_IN_DETECT;
	if (chip->has_ac97_0 | chip->has_ac97_1)
		chip->interrupt_mask |= OXYGEN_INT_AC97;
	oxygen_write16(chip, OXYGEN_INTERRUPT_MASK, chip->interrupt_mask);
	spin_unlock_irq(&chip->reg_lock);

	err = snd_card_register(card);
	if (err < 0)
		goto err_card;

	pci_set_drvdata(pci, card);
	return 0;

err_pci_regions:
	pci_release_regions(pci);
err_pci_enable:
	pci_disable_device(pci);
err_card:
	snd_card_free(card);
	return err;
}
EXPORT_SYMBOL(oxygen_pci_probe);

void oxygen_pci_remove(struct pci_dev *pci)
{
	snd_card_free(pci_get_drvdata(pci));
	pci_set_drvdata(pci, NULL);
}
EXPORT_SYMBOL(oxygen_pci_remove);

#ifdef CONFIG_PM
int oxygen_pci_suspend(struct pci_dev *pci, pm_message_t state)
{
	struct snd_card *card = pci_get_drvdata(pci);
	struct oxygen *chip = card->private_data;
	unsigned int i, saved_interrupt_mask;

	snd_power_change_state(card, SNDRV_CTL_POWER_D3hot);

	for (i = 0; i < PCM_COUNT; ++i)
		if (chip->streams[i])
			snd_pcm_suspend(chip->streams[i]);

	if (chip->model.suspend)
		chip->model.suspend(chip);

	spin_lock_irq(&chip->reg_lock);
	saved_interrupt_mask = chip->interrupt_mask;
	chip->interrupt_mask = 0;
	oxygen_write16(chip, OXYGEN_DMA_STATUS, 0);
	oxygen_write16(chip, OXYGEN_INTERRUPT_MASK, 0);
	spin_unlock_irq(&chip->reg_lock);

	synchronize_irq(chip->irq);
	flush_scheduled_work();
	chip->interrupt_mask = saved_interrupt_mask;

	pci_disable_device(pci);
	pci_save_state(pci);
	pci_set_power_state(pci, pci_choose_state(pci, state));
	return 0;
}
EXPORT_SYMBOL(oxygen_pci_suspend);

static const u32 registers_to_restore[OXYGEN_IO_SIZE / 32] = {
	0xffffffff, 0x00ff077f, 0x00011d08, 0x007f00ff,
	0x00300000, 0x00000fe4, 0x0ff7001f, 0x00000000
};
static const u32 ac97_registers_to_restore[2][0x40 / 32] = {
	{ 0x18284fa2, 0x03060000 },
	{ 0x00007fa6, 0x00200000 }
};

static inline int is_bit_set(const u32 *bitmap, unsigned int bit)
{
	return bitmap[bit / 32] & (1 << (bit & 31));
}

static void oxygen_restore_ac97(struct oxygen *chip, unsigned int codec)
{
	unsigned int i;

	oxygen_write_ac97(chip, codec, AC97_RESET, 0);
	msleep(1);
	for (i = 1; i < 0x40; ++i)
		if (is_bit_set(ac97_registers_to_restore[codec], i))
			oxygen_write_ac97(chip, codec, i * 2,
					  chip->saved_ac97_registers[codec][i]);
}

int oxygen_pci_resume(struct pci_dev *pci)
{
	struct snd_card *card = pci_get_drvdata(pci);
	struct oxygen *chip = card->private_data;
	unsigned int i;

	pci_set_power_state(pci, PCI_D0);
	pci_restore_state(pci);
	if (pci_enable_device(pci) < 0) {
		snd_printk(KERN_ERR "cannot reenable device");
		snd_card_disconnect(card);
		return -EIO;
	}
	pci_set_master(pci);

	oxygen_write16(chip, OXYGEN_DMA_STATUS, 0);
	oxygen_write16(chip, OXYGEN_INTERRUPT_MASK, 0);
	for (i = 0; i < OXYGEN_IO_SIZE; ++i)
		if (is_bit_set(registers_to_restore, i))
			oxygen_write8(chip, i, chip->saved_registers._8[i]);
	if (chip->has_ac97_0)
		oxygen_restore_ac97(chip, 0);
	if (chip->has_ac97_1)
		oxygen_restore_ac97(chip, 1);

	if (chip->model.resume)
		chip->model.resume(chip);

	oxygen_write16(chip, OXYGEN_INTERRUPT_MASK, chip->interrupt_mask);

	snd_power_change_state(card, SNDRV_CTL_POWER_D0);
	return 0;
}
EXPORT_SYMBOL(oxygen_pci_resume);
#endif /* CONFIG_PM */
