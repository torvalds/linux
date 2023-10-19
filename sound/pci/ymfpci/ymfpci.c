// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *  The driver for the Yamaha's DS1/DS1E cards
 *  Copyright (c) by Jaroslav Kysela <perex@perex.cz>
 */

#include <linux/init.h>
#include <linux/pci.h>
#include <linux/time.h>
#include <linux/module.h>
#include <sound/core.h>
#include "ymfpci.h"
#include <sound/mpu401.h>
#include <sound/opl3.h>
#include <sound/initval.h>

MODULE_AUTHOR("Jaroslav Kysela <perex@perex.cz>");
MODULE_DESCRIPTION("Yamaha DS-1 PCI");
MODULE_LICENSE("GPL");

static int index[SNDRV_CARDS] = SNDRV_DEFAULT_IDX;	/* Index 0-MAX */
static char *id[SNDRV_CARDS] = SNDRV_DEFAULT_STR;	/* ID for this card */
static bool enable[SNDRV_CARDS] = SNDRV_DEFAULT_ENABLE_PNP;	/* Enable this card */
static long fm_port[SNDRV_CARDS];
static long mpu_port[SNDRV_CARDS];
#ifdef SUPPORT_JOYSTICK
static long joystick_port[SNDRV_CARDS];
#endif
static bool rear_switch[SNDRV_CARDS];

module_param_array(index, int, NULL, 0444);
MODULE_PARM_DESC(index, "Index value for the Yamaha DS-1 PCI soundcard.");
module_param_array(id, charp, NULL, 0444);
MODULE_PARM_DESC(id, "ID string for the Yamaha DS-1 PCI soundcard.");
module_param_array(enable, bool, NULL, 0444);
MODULE_PARM_DESC(enable, "Enable Yamaha DS-1 soundcard.");
module_param_hw_array(mpu_port, long, ioport, NULL, 0444);
MODULE_PARM_DESC(mpu_port, "MPU-401 Port.");
module_param_hw_array(fm_port, long, ioport, NULL, 0444);
MODULE_PARM_DESC(fm_port, "FM OPL-3 Port.");
#ifdef SUPPORT_JOYSTICK
module_param_hw_array(joystick_port, long, ioport, NULL, 0444);
MODULE_PARM_DESC(joystick_port, "Joystick port address");
#endif
module_param_array(rear_switch, bool, NULL, 0444);
MODULE_PARM_DESC(rear_switch, "Enable shared rear/line-in switch");

static const struct pci_device_id snd_ymfpci_ids[] = {
	{ PCI_VDEVICE(YAMAHA, 0x0004), 0, },   /* YMF724 */
	{ PCI_VDEVICE(YAMAHA, 0x000d), 0, },   /* YMF724F */
	{ PCI_VDEVICE(YAMAHA, 0x000a), 0, },   /* YMF740 */
	{ PCI_VDEVICE(YAMAHA, 0x000c), 0, },   /* YMF740C */
	{ PCI_VDEVICE(YAMAHA, 0x0010), 0, },   /* YMF744 */
	{ PCI_VDEVICE(YAMAHA, 0x0012), 0, },   /* YMF754 */
	{ 0, }
};

MODULE_DEVICE_TABLE(pci, snd_ymfpci_ids);

#ifdef SUPPORT_JOYSTICK
static int snd_ymfpci_create_gameport(struct snd_ymfpci *chip, int dev,
				      int legacy_ctrl, int legacy_ctrl2)
{
	struct gameport *gp;
	struct resource *r = NULL;
	int io_port = joystick_port[dev];

	if (!io_port)
		return -ENODEV;

	if (chip->pci->device >= 0x0010) { /* YMF 744/754 */

		if (io_port == 1) {
			/* auto-detect */
			io_port = pci_resource_start(chip->pci, 2);
			if (!io_port)
				return -ENODEV;
		}
	} else {
		if (io_port == 1) {
			/* auto-detect */
			for (io_port = 0x201; io_port <= 0x205; io_port++) {
				if (io_port == 0x203)
					continue;
				r = request_region(io_port, 1, "YMFPCI gameport");
				if (r)
					break;
			}
			if (!r) {
				dev_err(chip->card->dev,
					"no gameport ports available\n");
				return -EBUSY;
			}
		}
		switch (io_port) {
		case 0x201: legacy_ctrl2 |= 0 << 6; break;
		case 0x202: legacy_ctrl2 |= 1 << 6; break;
		case 0x204: legacy_ctrl2 |= 2 << 6; break;
		case 0x205: legacy_ctrl2 |= 3 << 6; break;
		default:
			dev_err(chip->card->dev,
				"invalid joystick port %#x", io_port);
			return -EINVAL;
		}
	}

	if (!r) {
		r = devm_request_region(&chip->pci->dev, io_port, 1,
					"YMFPCI gameport");
		if (!r) {
			dev_err(chip->card->dev,
				"joystick port %#x is in use.\n", io_port);
			return -EBUSY;
		}
	}

	chip->gameport = gp = gameport_allocate_port();
	if (!gp) {
		dev_err(chip->card->dev,
			"cannot allocate memory for gameport\n");
		return -ENOMEM;
	}


	gameport_set_name(gp, "Yamaha YMF Gameport");
	gameport_set_phys(gp, "pci%s/gameport0", pci_name(chip->pci));
	gameport_set_dev_parent(gp, &chip->pci->dev);
	gp->io = io_port;

	if (chip->pci->device >= 0x0010) /* YMF 744/754 */
		pci_write_config_word(chip->pci, PCIR_DSXG_JOYBASE, io_port);

	pci_write_config_word(chip->pci, PCIR_DSXG_LEGACY, legacy_ctrl | YMFPCI_LEGACY_JPEN);
	pci_write_config_word(chip->pci, PCIR_DSXG_ELEGACY, legacy_ctrl2);

	gameport_register_port(chip->gameport);

	return 0;
}

void snd_ymfpci_free_gameport(struct snd_ymfpci *chip)
{
	if (chip->gameport) {
		gameport_unregister_port(chip->gameport);
		chip->gameport = NULL;
	}
}
#else
static inline int snd_ymfpci_create_gameport(struct snd_ymfpci *chip, int dev, int l, int l2) { return -ENOSYS; }
void snd_ymfpci_free_gameport(struct snd_ymfpci *chip) { }
#endif /* SUPPORT_JOYSTICK */

static int snd_card_ymfpci_probe(struct pci_dev *pci,
				 const struct pci_device_id *pci_id)
{
	static int dev;
	struct snd_card *card;
	struct resource *fm_res = NULL;
	struct resource *mpu_res = NULL;
	struct snd_ymfpci *chip;
	struct snd_opl3 *opl3;
	const char *str, *model;
	int err;
	u16 legacy_ctrl, legacy_ctrl2, old_legacy_ctrl;

	if (dev >= SNDRV_CARDS)
		return -ENODEV;
	if (!enable[dev]) {
		dev++;
		return -ENOENT;
	}

	err = snd_devm_card_new(&pci->dev, index[dev], id[dev], THIS_MODULE,
			   sizeof(*chip), &card);
	if (err < 0)
		return err;
	chip = card->private_data;

	switch (pci_id->device) {
	case 0x0004: str = "YMF724";  model = "DS-1"; break;
	case 0x000d: str = "YMF724F"; model = "DS-1"; break;
	case 0x000a: str = "YMF740";  model = "DS-1L"; break;
	case 0x000c: str = "YMF740C"; model = "DS-1L"; break;
	case 0x0010: str = "YMF744";  model = "DS-1S"; break;
	case 0x0012: str = "YMF754";  model = "DS-1E"; break;
	default: model = str = "???"; break;
	}

	legacy_ctrl = 0;
	legacy_ctrl2 = 0x0800;	/* SBEN = 0, SMOD = 01, LAD = 0 */

	if (pci_id->device >= 0x0010) { /* YMF 744/754 */
		if (fm_port[dev] == 1) {
			/* auto-detect */
			fm_port[dev] = pci_resource_start(pci, 1);
		}
		if (fm_port[dev] > 0)
			fm_res = devm_request_region(&pci->dev, fm_port[dev],
						     4, "YMFPCI OPL3");
		if (fm_res) {
			legacy_ctrl |= YMFPCI_LEGACY_FMEN;
			pci_write_config_word(pci, PCIR_DSXG_FMBASE, fm_port[dev]);
		}
		if (mpu_port[dev] == 1) {
			/* auto-detect */
			mpu_port[dev] = pci_resource_start(pci, 1) + 0x20;
		}
		if (mpu_port[dev] > 0)
			mpu_res = devm_request_region(&pci->dev, mpu_port[dev],
						      2, "YMFPCI MPU401");
		if (mpu_res) {
			legacy_ctrl |= YMFPCI_LEGACY_MEN;
			pci_write_config_word(pci, PCIR_DSXG_MPU401BASE, mpu_port[dev]);
		}
	} else {
		switch (fm_port[dev]) {
		case 0x388: legacy_ctrl2 |= 0; break;
		case 0x398: legacy_ctrl2 |= 1; break;
		case 0x3a0: legacy_ctrl2 |= 2; break;
		case 0x3a8: legacy_ctrl2 |= 3; break;
		default: fm_port[dev] = 0; break;
		}
		if (fm_port[dev] > 0)
			fm_res = devm_request_region(&pci->dev, fm_port[dev],
						     4, "YMFPCI OPL3");
		if (fm_res) {
			legacy_ctrl |= YMFPCI_LEGACY_FMEN;
		} else {
			legacy_ctrl2 &= ~YMFPCI_LEGACY2_FMIO;
			fm_port[dev] = 0;
		}
		switch (mpu_port[dev]) {
		case 0x330: legacy_ctrl2 |= 0 << 4; break;
		case 0x300: legacy_ctrl2 |= 1 << 4; break;
		case 0x332: legacy_ctrl2 |= 2 << 4; break;
		case 0x334: legacy_ctrl2 |= 3 << 4; break;
		default: mpu_port[dev] = 0; break;
		}
		if (mpu_port[dev] > 0)
			mpu_res = devm_request_region(&pci->dev, mpu_port[dev],
						      2, "YMFPCI MPU401");
		if (mpu_res) {
			legacy_ctrl |= YMFPCI_LEGACY_MEN;
		} else {
			legacy_ctrl2 &= ~YMFPCI_LEGACY2_MPUIO;
			mpu_port[dev] = 0;
		}
	}
	if (mpu_res) {
		legacy_ctrl |= YMFPCI_LEGACY_MIEN;
		legacy_ctrl2 |= YMFPCI_LEGACY2_IMOD;
	}
	pci_read_config_word(pci, PCIR_DSXG_LEGACY, &old_legacy_ctrl);
	pci_write_config_word(pci, PCIR_DSXG_LEGACY, legacy_ctrl);
	pci_write_config_word(pci, PCIR_DSXG_ELEGACY, legacy_ctrl2);
	err = snd_ymfpci_create(card, pci, old_legacy_ctrl);
	if (err  < 0)
		return err;

	strcpy(card->driver, str);
	sprintf(card->shortname, "Yamaha %s (%s)", model, str);
	sprintf(card->longname, "%s at 0x%lx, irq %i",
		card->shortname,
		chip->reg_area_phys,
		chip->irq);
	err = snd_ymfpci_pcm(chip, 0);
	if (err < 0)
		return err;

	err = snd_ymfpci_pcm_spdif(chip, 1);
	if (err < 0)
		return err;

	err = snd_ymfpci_mixer(chip, rear_switch[dev]);
	if (err < 0)
		return err;

	if (chip->ac97->ext_id & AC97_EI_SDAC) {
		err = snd_ymfpci_pcm_4ch(chip, 2);
		if (err < 0)
			return err;

		err = snd_ymfpci_pcm2(chip, 3);
		if (err < 0)
			return err;
	}
	err = snd_ymfpci_timer(chip, 0);
	if (err < 0)
		return err;

	if (mpu_res) {
		err = snd_mpu401_uart_new(card, 0, MPU401_HW_YMFPCI,
					  mpu_port[dev],
					  MPU401_INFO_INTEGRATED |
					  MPU401_INFO_IRQ_HOOK,
					  -1, &chip->rawmidi);
		if (err < 0) {
			dev_warn(card->dev,
				 "cannot initialize MPU401 at 0x%lx, skipping...\n",
				 mpu_port[dev]);
			legacy_ctrl &= ~YMFPCI_LEGACY_MIEN; /* disable MPU401 irq */
			pci_write_config_word(pci, PCIR_DSXG_LEGACY, legacy_ctrl);
		}
	}
	if (fm_res) {
		err = snd_opl3_create(card,
				      fm_port[dev],
				      fm_port[dev] + 2,
				      OPL3_HW_OPL3, 1, &opl3);
		if (err < 0) {
			dev_warn(card->dev,
				 "cannot initialize FM OPL3 at 0x%lx, skipping...\n",
				 fm_port[dev]);
			legacy_ctrl &= ~YMFPCI_LEGACY_FMEN;
			pci_write_config_word(pci, PCIR_DSXG_LEGACY, legacy_ctrl);
		} else {
			err = snd_opl3_hwdep_new(opl3, 0, 1, NULL);
			if (err < 0) {
				dev_err(card->dev, "cannot create opl3 hwdep\n");
				return err;
			}
		}
	}

	snd_ymfpci_create_gameport(chip, dev, legacy_ctrl, legacy_ctrl2);

	err = snd_card_register(card);
	if (err < 0)
		return err;

	pci_set_drvdata(pci, card);
	dev++;
	return 0;
}

static struct pci_driver ymfpci_driver = {
	.name = KBUILD_MODNAME,
	.id_table = snd_ymfpci_ids,
	.probe = snd_card_ymfpci_probe,
#ifdef CONFIG_PM_SLEEP
	.driver = {
		.pm = &snd_ymfpci_pm,
	},
#endif
};

module_pci_driver(ymfpci_driver);
