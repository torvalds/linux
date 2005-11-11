/*
 *  The driver for the Yamaha's DS1/DS1E cards
 *  Copyright (c) by Jaroslav Kysela <perex@suse.cz>
 *
 *
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation; either version 2 of the License, or
 *   (at your option) any later version.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with this program; if not, write to the Free Software
 *   Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307 USA
 *
 */

#include <sound/driver.h>
#include <linux/init.h>
#include <linux/pci.h>
#include <linux/time.h>
#include <linux/moduleparam.h>
#include <sound/core.h>
#include <sound/ymfpci.h>
#include <sound/mpu401.h>
#include <sound/opl3.h>
#include <sound/initval.h>

MODULE_AUTHOR("Jaroslav Kysela <perex@suse.cz>");
MODULE_DESCRIPTION("Yamaha DS-XG PCI");
MODULE_LICENSE("GPL");
MODULE_SUPPORTED_DEVICE("{{Yamaha,YMF724},"
		"{Yamaha,YMF724F},"
		"{Yamaha,YMF740},"
		"{Yamaha,YMF740C},"
		"{Yamaha,YMF744},"
		"{Yamaha,YMF754}}");

static int index[SNDRV_CARDS] = SNDRV_DEFAULT_IDX;	/* Index 0-MAX */
static char *id[SNDRV_CARDS] = SNDRV_DEFAULT_STR;	/* ID for this card */
static int enable[SNDRV_CARDS] = SNDRV_DEFAULT_ENABLE_PNP;	/* Enable this card */
static long fm_port[SNDRV_CARDS];
static long mpu_port[SNDRV_CARDS];
#ifdef SUPPORT_JOYSTICK
static long joystick_port[SNDRV_CARDS];
#endif
static int rear_switch[SNDRV_CARDS];

module_param_array(index, int, NULL, 0444);
MODULE_PARM_DESC(index, "Index value for the Yamaha DS-XG PCI soundcard.");
module_param_array(id, charp, NULL, 0444);
MODULE_PARM_DESC(id, "ID string for the Yamaha DS-XG PCI soundcard.");
module_param_array(enable, bool, NULL, 0444);
MODULE_PARM_DESC(enable, "Enable Yamaha DS-XG soundcard.");
module_param_array(mpu_port, long, NULL, 0444);
MODULE_PARM_DESC(mpu_port, "MPU-401 Port.");
module_param_array(fm_port, long, NULL, 0444);
MODULE_PARM_DESC(fm_port, "FM OPL-3 Port.");
#ifdef SUPPORT_JOYSTICK
module_param_array(joystick_port, long, NULL, 0444);
MODULE_PARM_DESC(joystick_port, "Joystick port address");
#endif
module_param_array(rear_switch, bool, NULL, 0444);
MODULE_PARM_DESC(rear_switch, "Enable shared rear/line-in switch");

static struct pci_device_id snd_ymfpci_ids[] = {
        { 0x1073, 0x0004, PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0, },   /* YMF724 */
        { 0x1073, 0x000d, PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0, },   /* YMF724F */
        { 0x1073, 0x000a, PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0, },   /* YMF740 */
        { 0x1073, 0x000c, PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0, },   /* YMF740C */
        { 0x1073, 0x0010, PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0, },   /* YMF744 */
        { 0x1073, 0x0012, PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0, },   /* YMF754 */
	{ 0, }
};

MODULE_DEVICE_TABLE(pci, snd_ymfpci_ids);

#ifdef SUPPORT_JOYSTICK
static int __devinit snd_ymfpci_create_gameport(ymfpci_t *chip, int dev,
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
			if (!(io_port = pci_resource_start(chip->pci, 2)))
				return -ENODEV;
		}
	} else {
		if (io_port == 1) {
			/* auto-detect */
			for (io_port = 0x201; io_port <= 0x205; io_port++) {
				if (io_port == 0x203)
					continue;
				if ((r = request_region(io_port, 1, "YMFPCI gameport")) != NULL)
					break;
			}
			if (!r) {
				printk(KERN_ERR "ymfpci: no gameport ports available\n");
				return -EBUSY;
			}
		}
		switch (io_port) {
		case 0x201: legacy_ctrl2 |= 0 << 6; break;
		case 0x202: legacy_ctrl2 |= 1 << 6; break;
		case 0x204: legacy_ctrl2 |= 2 << 6; break;
		case 0x205: legacy_ctrl2 |= 3 << 6; break;
		default:
			printk(KERN_ERR "ymfpci: invalid joystick port %#x", io_port);
			return -EINVAL;
		}
	}

	if (!r && !(r = request_region(io_port, 1, "YMFPCI gameport"))) {
		printk(KERN_ERR "ymfpci: joystick port %#x is in use.\n", io_port);
		return -EBUSY;
	}

	chip->gameport = gp = gameport_allocate_port();
	if (!gp) {
		printk(KERN_ERR "ymfpci: cannot allocate memory for gameport\n");
		release_and_free_resource(r);
		return -ENOMEM;
	}


	gameport_set_name(gp, "Yamaha YMF Gameport");
	gameport_set_phys(gp, "pci%s/gameport0", pci_name(chip->pci));
	gameport_set_dev_parent(gp, &chip->pci->dev);
	gp->io = io_port;
	gameport_set_port_data(gp, r);

	if (chip->pci->device >= 0x0010) /* YMF 744/754 */
		pci_write_config_word(chip->pci, PCIR_DSXG_JOYBASE, io_port);

	pci_write_config_word(chip->pci, PCIR_DSXG_LEGACY, legacy_ctrl | YMFPCI_LEGACY_JPEN);
	pci_write_config_word(chip->pci, PCIR_DSXG_ELEGACY, legacy_ctrl2);

	gameport_register_port(chip->gameport);

	return 0;
}

void snd_ymfpci_free_gameport(ymfpci_t *chip)
{
	if (chip->gameport) {
		struct resource *r = gameport_get_port_data(chip->gameport);

		gameport_unregister_port(chip->gameport);
		chip->gameport = NULL;

		release_and_free_resource(r);
	}
}
#else
static inline int snd_ymfpci_create_gameport(ymfpci_t *chip, int dev, int l, int l2) { return -ENOSYS; }
void snd_ymfpci_free_gameport(ymfpci_t *chip) { }
#endif /* SUPPORT_JOYSTICK */

static int __devinit snd_card_ymfpci_probe(struct pci_dev *pci,
					   const struct pci_device_id *pci_id)
{
	static int dev;
	snd_card_t *card;
	struct resource *fm_res = NULL;
	struct resource *mpu_res = NULL;
	ymfpci_t *chip;
	opl3_t *opl3;
	char *str;
	int err;
	u16 legacy_ctrl, legacy_ctrl2, old_legacy_ctrl;

	if (dev >= SNDRV_CARDS)
		return -ENODEV;
	if (!enable[dev]) {
		dev++;
		return -ENOENT;
	}

	card = snd_card_new(index[dev], id[dev], THIS_MODULE, 0);
	if (card == NULL)
		return -ENOMEM;

	switch (pci_id->device) {
	case 0x0004: str = "YMF724"; break;
	case 0x000d: str = "YMF724F"; break;
	case 0x000a: str = "YMF740"; break;
	case 0x000c: str = "YMF740C"; break;
	case 0x0010: str = "YMF744"; break;
	case 0x0012: str = "YMF754"; break;
	default: str = "???"; break;
	}

	legacy_ctrl = 0;
	legacy_ctrl2 = 0x0800;	/* SBEN = 0, SMOD = 01, LAD = 0 */

	if (pci_id->device >= 0x0010) { /* YMF 744/754 */
		if (fm_port[dev] == 1) {
			/* auto-detect */
			fm_port[dev] = pci_resource_start(pci, 1);
		}
		if (fm_port[dev] > 0 &&
		    (fm_res = request_region(fm_port[dev], 4, "YMFPCI OPL3")) != NULL) {
			legacy_ctrl |= YMFPCI_LEGACY_FMEN;
			pci_write_config_word(pci, PCIR_DSXG_FMBASE, fm_port[dev]);
		}
		if (mpu_port[dev] == 1) {
			/* auto-detect */
			mpu_port[dev] = pci_resource_start(pci, 1) + 0x20;
		}
		if (mpu_port[dev] > 0 &&
		    (mpu_res = request_region(mpu_port[dev], 2, "YMFPCI MPU401")) != NULL) {
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
		if (fm_port[dev] > 0 &&
		    (fm_res = request_region(fm_port[dev], 4, "YMFPCI OPL3")) != NULL) {
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
		if (mpu_port[dev] > 0 &&
		    (mpu_res = request_region(mpu_port[dev], 2, "YMFPCI MPU401")) != NULL) {
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
	if ((err = snd_ymfpci_create(card, pci,
				     old_legacy_ctrl,
			 	     &chip)) < 0) {
		snd_card_free(card);
		release_and_free_resource(mpu_res);
		release_and_free_resource(fm_res);
		return err;
	}
	chip->fm_res = fm_res;
	chip->mpu_res = mpu_res;
	strcpy(card->driver, str);
	sprintf(card->shortname, "Yamaha DS-XG (%s)", str);
	sprintf(card->longname, "%s at 0x%lx, irq %i",
		card->shortname,
		chip->reg_area_phys,
		chip->irq);
	if ((err = snd_ymfpci_pcm(chip, 0, NULL)) < 0) {
		snd_card_free(card);
		return err;
	}
	if ((err = snd_ymfpci_pcm_spdif(chip, 1, NULL)) < 0) {
		snd_card_free(card);
		return err;
	}
	if ((err = snd_ymfpci_pcm_4ch(chip, 2, NULL)) < 0) {
		snd_card_free(card);
		return err;
	}
	if ((err = snd_ymfpci_pcm2(chip, 3, NULL)) < 0) {
		snd_card_free(card);
		return err;
	}
	if ((err = snd_ymfpci_mixer(chip, rear_switch[dev])) < 0) {
		snd_card_free(card);
		return err;
	}
	if ((err = snd_ymfpci_timer(chip, 0)) < 0) {
		snd_card_free(card);
		return err;
	}
	if (chip->mpu_res) {
		if ((err = snd_mpu401_uart_new(card, 0, MPU401_HW_YMFPCI,
					       mpu_port[dev], 1,
					       pci->irq, 0, &chip->rawmidi)) < 0) {
			printk(KERN_WARNING "ymfpci: cannot initialize MPU401 at 0x%lx, skipping...\n", mpu_port[dev]);
			legacy_ctrl &= ~YMFPCI_LEGACY_MIEN; /* disable MPU401 irq */
			pci_write_config_word(pci, PCIR_DSXG_LEGACY, legacy_ctrl);
		}
	}
	if (chip->fm_res) {
		if ((err = snd_opl3_create(card,
					   fm_port[dev],
					   fm_port[dev] + 2,
					   OPL3_HW_OPL3, 1, &opl3)) < 0) {
			printk(KERN_WARNING "ymfpci: cannot initialize FM OPL3 at 0x%lx, skipping...\n", fm_port[dev]);
			legacy_ctrl &= ~YMFPCI_LEGACY_FMEN;
			pci_write_config_word(pci, PCIR_DSXG_LEGACY, legacy_ctrl);
		} else if ((err = snd_opl3_hwdep_new(opl3, 0, 1, NULL)) < 0) {
			snd_card_free(card);
			snd_printk(KERN_ERR "cannot create opl3 hwdep\n");
			return err;
		}
	}

	snd_ymfpci_create_gameport(chip, dev, legacy_ctrl, legacy_ctrl2);

	if ((err = snd_card_register(card)) < 0) {
		snd_card_free(card);
		return err;
	}
	pci_set_drvdata(pci, card);
	dev++;
	return 0;
}

static void __devexit snd_card_ymfpci_remove(struct pci_dev *pci)
{
	snd_card_free(pci_get_drvdata(pci));
	pci_set_drvdata(pci, NULL);
}

static struct pci_driver driver = {
	.name = "Yamaha DS-XG PCI",
	.id_table = snd_ymfpci_ids,
	.probe = snd_card_ymfpci_probe,
	.remove = __devexit_p(snd_card_ymfpci_remove),
	SND_PCI_PM_CALLBACKS
};

static int __init alsa_card_ymfpci_init(void)
{
	return pci_register_driver(&driver);
}

static void __exit alsa_card_ymfpci_exit(void)
{
	pci_unregister_driver(&driver);
}

module_init(alsa_card_ymfpci_init)
module_exit(alsa_card_ymfpci_exit)
