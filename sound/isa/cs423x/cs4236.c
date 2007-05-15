/*
 *  Driver for generic CS4232/CS4235/CS4236/CS4236B/CS4237B/CS4238B/CS4239 chips
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
#include <linux/err.h>
#include <linux/isa.h>
#include <linux/slab.h>
#include <linux/pnp.h>
#include <linux/moduleparam.h>
#include <sound/core.h>
#include <sound/cs4231.h>
#include <sound/mpu401.h>
#include <sound/opl3.h>
#include <sound/initval.h>

MODULE_AUTHOR("Jaroslav Kysela <perex@suse.cz>");
MODULE_LICENSE("GPL");
#ifdef CS4232
MODULE_DESCRIPTION("Cirrus Logic CS4232");
MODULE_SUPPORTED_DEVICE("{{Turtle Beach,TBS-2000},"
		"{Turtle Beach,Tropez Plus},"
		"{SIC CrystalWave 32},"
		"{Hewlett Packard,Omnibook 5500},"
		"{TerraTec,Maestro 32/96},"
		"{Philips,PCA70PS}}");
#else
MODULE_DESCRIPTION("Cirrus Logic CS4235-9");
MODULE_SUPPORTED_DEVICE("{{Crystal Semiconductors,CS4235},"
		"{Crystal Semiconductors,CS4236},"
		"{Crystal Semiconductors,CS4237},"
		"{Crystal Semiconductors,CS4238},"
		"{Crystal Semiconductors,CS4239},"
		"{Acer,AW37},"
		"{Acer,AW35/Pro},"
		"{Crystal,3D},"
		"{Crystal Computer,TidalWave128},"
		"{Dell,Optiplex GX1},"
		"{Dell,Workstation 400 sound},"
		"{EliteGroup,P5TX-LA sound},"
		"{Gallant,SC-70P},"
		"{Gateway,E1000 Onboard CS4236B},"
		"{Genius,Sound Maker 3DJ},"
		"{Hewlett Packard,HP6330 sound},"
		"{IBM,PC 300PL sound},"
		"{IBM,Aptiva 2137 E24},"
		"{IBM,IntelliStation M Pro},"
		"{Intel,Marlin Spike Mobo CS4235},"
		"{Intel PR440FX Onboard},"
		"{Guillemot,MaxiSound 16 PnP},"
		"{NewClear,3D},"
		"{TerraTec,AudioSystem EWS64L/XL},"
		"{Typhoon Soundsystem,CS4236B},"
		"{Turtle Beach,Malibu},"
		"{Unknown,Digital PC 5000 Onboard}}");
#endif

#ifdef CS4232
#define IDENT "CS4232"
#define DEV_NAME "cs4232"
#else
#define IDENT "CS4236+"
#define DEV_NAME "cs4236"
#endif

static int index[SNDRV_CARDS] = SNDRV_DEFAULT_IDX;	/* Index 0-MAX */
static char *id[SNDRV_CARDS] = SNDRV_DEFAULT_STR;	/* ID for this card */
static int enable[SNDRV_CARDS] = SNDRV_DEFAULT_ENABLE_ISAPNP; /* Enable this card */
#ifdef CONFIG_PNP
static int isapnp[SNDRV_CARDS] = {[0 ... (SNDRV_CARDS - 1)] = 1};
#endif
static long port[SNDRV_CARDS] = SNDRV_DEFAULT_PORT;	/* PnP setup */
static long cport[SNDRV_CARDS] = SNDRV_DEFAULT_PORT;	/* PnP setup */
static long mpu_port[SNDRV_CARDS] = SNDRV_DEFAULT_PORT;/* PnP setup */
static long fm_port[SNDRV_CARDS] = SNDRV_DEFAULT_PORT;	/* PnP setup */
static long sb_port[SNDRV_CARDS] = SNDRV_DEFAULT_PORT;	/* PnP setup */
static int irq[SNDRV_CARDS] = SNDRV_DEFAULT_IRQ;	/* 5,7,9,11,12,15 */
static int mpu_irq[SNDRV_CARDS] = SNDRV_DEFAULT_IRQ;	/* 9,11,12,15 */
static int dma1[SNDRV_CARDS] = SNDRV_DEFAULT_DMA;	/* 0,1,3,5,6,7 */
static int dma2[SNDRV_CARDS] = SNDRV_DEFAULT_DMA;	/* 0,1,3,5,6,7 */

module_param_array(index, int, NULL, 0444);
MODULE_PARM_DESC(index, "Index value for " IDENT " soundcard.");
module_param_array(id, charp, NULL, 0444);
MODULE_PARM_DESC(id, "ID string for " IDENT " soundcard.");
module_param_array(enable, bool, NULL, 0444);
MODULE_PARM_DESC(enable, "Enable " IDENT " soundcard.");
#ifdef CONFIG_PNP
module_param_array(isapnp, bool, NULL, 0444);
MODULE_PARM_DESC(isapnp, "ISA PnP detection for specified soundcard.");
#endif
module_param_array(port, long, NULL, 0444);
MODULE_PARM_DESC(port, "Port # for " IDENT " driver.");
module_param_array(cport, long, NULL, 0444);
MODULE_PARM_DESC(cport, "Control port # for " IDENT " driver.");
module_param_array(mpu_port, long, NULL, 0444);
MODULE_PARM_DESC(mpu_port, "MPU-401 port # for " IDENT " driver.");
module_param_array(fm_port, long, NULL, 0444);
MODULE_PARM_DESC(fm_port, "FM port # for " IDENT " driver.");
module_param_array(sb_port, long, NULL, 0444);
MODULE_PARM_DESC(sb_port, "SB port # for " IDENT " driver (optional).");
module_param_array(irq, int, NULL, 0444);
MODULE_PARM_DESC(irq, "IRQ # for " IDENT " driver.");
module_param_array(mpu_irq, int, NULL, 0444);
MODULE_PARM_DESC(mpu_irq, "MPU-401 IRQ # for " IDENT " driver.");
module_param_array(dma1, int, NULL, 0444);
MODULE_PARM_DESC(dma1, "DMA1 # for " IDENT " driver.");
module_param_array(dma2, int, NULL, 0444);
MODULE_PARM_DESC(dma2, "DMA2 # for " IDENT " driver.");

#ifdef CONFIG_PNP
static int isa_registered;
static int pnpc_registered;
#ifdef CS4232
static int pnp_registered;
#endif
#endif /* CONFIG_PNP */

struct snd_card_cs4236 {
	struct snd_cs4231 *chip;
	struct resource *res_sb_port;
#ifdef CONFIG_PNP
	struct pnp_dev *wss;
	struct pnp_dev *ctrl;
	struct pnp_dev *mpu;
#endif
};

#ifdef CONFIG_PNP

#ifdef CS4232
/*
 * PNP BIOS
 */
static const struct pnp_device_id snd_cs4232_pnpbiosids[] = {
	{ .id = "CSC0100" },
	{ .id = "CSC0000" },
	/* Guillemot Turtlebeach something appears to be cs4232 compatible
	 * (untested) */
	{ .id = "GIM0100" },
	{ .id = "" }
};
MODULE_DEVICE_TABLE(pnp, snd_cs4232_pnpbiosids);
#endif /* CS4232 */

#ifdef CS4232
#define CS423X_ISAPNP_DRIVER	"cs4232_isapnp"
static struct pnp_card_device_id snd_cs423x_pnpids[] = {
	/* Philips PCA70PS */
	{ .id = "CSC0d32", .devs = { { "CSC0000" }, { "CSC0010" }, { "PNPb006" } } },
	/* TerraTec Maestro 32/96 (CS4232) */
	{ .id = "CSC1a32", .devs = { { "CSC0000" }, { "CSC0010" }, { "CSC0003" } } },
	/* HP Omnibook 5500 onboard */
	{ .id = "CSC4232", .devs = { { "CSC0000" }, { "CSC0002" }, { "CSC0003" } } },
	/* Unnamed CS4236 card (Made in Taiwan) */
	{ .id = "CSC4236", .devs = { { "CSC0000" }, { "CSC0010" }, { "CSC0003" } } },
	/* Turtle Beach TBS-2000 (CS4232) */
	{ .id = "CSC7532", .devs = { { "CSC0000" }, { "CSC0010" }, { "CSCb006" } } },
	/* Turtle Beach Tropez Plus (CS4232) */
	{ .id = "CSC7632", .devs = { { "CSC0000" }, { "CSC0010" }, { "PNPb006" } } },
	/* SIC CrystalWave 32 (CS4232) */
	{ .id = "CSCf032", .devs = { { "CSC0000" }, { "CSC0010" }, { "CSC0003" } } },
	/* Netfinity 3000 on-board soundcard */
	{ .id = "CSCe825", .devs = { { "CSC0100" }, { "CSC0110" }, { "CSC010f" } } },
	/* --- */
	{ .id = "" }	/* end */
};
#else /* CS4236 */
#define CS423X_ISAPNP_DRIVER	"cs4236_isapnp"
static struct pnp_card_device_id snd_cs423x_pnpids[] = {
	/* Intel Marlin Spike Motherboard - CS4235 */
	{ .id = "CSC0225", .devs = { { "CSC0000" }, { "CSC0010" }, { "CSC0003" } } },
	/* Intel Marlin Spike Motherboard (#2) - CS4235 */
	{ .id = "CSC0225", .devs = { { "CSC0100" }, { "CSC0110" }, { "CSC0103" } } },
	/* Unknown Intel mainboard - CS4235 */
	{ .id = "CSC0225", .devs = { { "CSC0100" }, { "CSC0110" } } },
	/* Genius Sound Maker 3DJ - CS4237B */
	{ .id = "CSC0437", .devs = { { "CSC0000" }, { "CSC0010" }, { "CSC0003" } } },
	/* Digital PC 5000 Onboard - CS4236B */
	{ .id = "CSC0735", .devs = { { "CSC0000" }, { "CSC0010" } } },
	/* some uknown CS4236B */
	{ .id = "CSC0b35", .devs = { { "CSC0000" }, { "CSC0010" }, { "CSC0003" } } },
	/* Intel PR440FX Onboard sound */
	{ .id = "CSC0b36", .devs = { { "CSC0000" }, { "CSC0010" }, { "CSC0003" } } },
	/* CS4235 on mainboard without MPU */
	{ .id = "CSC1425", .devs = { { "CSC0100" }, { "CSC0110" } } },
	/* Gateway E1000 Onboard CS4236B */
	{ .id = "CSC1335", .devs = { { "CSC0000" }, { "CSC0010" }, { "CSC0003" } } },
	/* HP 6330 Onboard sound */
	{ .id = "CSC1525", .devs = { { "CSC0100" }, { "CSC0110" }, { "CSC0103" } } },
	/* Crystal Computer TidalWave128 */
	{ .id = "CSC1e37", .devs = { { "CSC0000" }, { "CSC0010" }, { "CSC0003" } } },
	/* ACER AW37 - CS4235 */
	{ .id = "CSC4236", .devs = { { "CSC0000" }, { "CSC0010" }, { "CSC0003" } } },
	/* build-in soundcard in EliteGroup P5TX-LA motherboard - CS4237B */
	{ .id = "CSC4237", .devs = { { "CSC0000" }, { "CSC0010" }, { "CSC0003" } } },
	/* Crystal 3D - CS4237B */
	{ .id = "CSC4336", .devs = { { "CSC0000" }, { "CSC0010" }, { "CSC0003" } } },
	/* Typhoon Soundsystem PnP - CS4236B */
	{ .id = "CSC4536", .devs = { { "CSC0000" }, { "CSC0010" }, { "CSC0003" } } },
	/* Crystal CX4235-XQ3 EP - CS4235 */
	{ .id = "CSC4625", .devs = { { "CSC0100" }, { "CSC0110" }, { "CSC0103" } } },
	/* Crystal Semiconductors CS4237B */
	{ .id = "CSC4637", .devs = { { "CSC0000" }, { "CSC0010" }, { "CSC0003" } } },
	/* NewClear 3D - CX4237B-XQ3 */
	{ .id = "CSC4837", .devs = { { "CSC0000" }, { "CSC0010" }, { "CSC0003" } } },
	/* Dell Optiplex GX1 - CS4236B */
	{ .id = "CSC6835", .devs = { { "CSC0000" }, { "CSC0010" }, { "CSC0003" } } },
	/* Dell P410 motherboard - CS4236B */
	{ .id = "CSC6835", .devs = { { "CSC0000" }, { "CSC0010" } } },
	/* Dell Workstation 400 Onboard - CS4236B */
	{ .id = "CSC6836", .devs = { { "CSC0000" }, { "CSC0010" }, { "CSC0003" } } },
	/* Turtle Beach Malibu - CS4237B */
	{ .id = "CSC7537", .devs = { { "CSC0000" }, { "CSC0010" }, { "CSC0003" } } },
	/* CS4235 - onboard */
	{ .id = "CSC8025", .devs = { { "CSC0100" }, { "CSC0110" }, { "CSC0103" } } },
	/* IBM Aptiva 2137 E24 Onboard - CS4237B */
	{ .id = "CSC8037", .devs = { { "CSC0000" }, { "CSC0010" }, { "CSC0003" } } },
	/* IBM IntelliStation M Pro motherboard */
	{ .id = "CSCc835", .devs = { { "CSC0000" }, { "CSC0010" } } },
	/* Guillemot MaxiSound 16 PnP - CS4236B */
	{ .id = "CSC9836", .devs = { { "CSC0000" }, { "CSC0010" }, { "CSC0003" } } },
	/* Gallant SC-70P */
	{ .id = "CSC9837", .devs = { { "CSC0000" }, { "CSC0010" }, { "CSC0003" } } },
	/* TerraTec AudioSystem EWS64XL - CS4236B */
	{ .id = "CSCa836", .devs = { { "CSCa800" }, { "CSCa810" }, { "CSCa803" } } },
	/* TerraTec AudioSystem EWS64XL - CS4236B */
	{ .id = "CSCa836", .devs = { { "CSCa800" }, { "CSCa810" } } },
	/* ACER AW37/Pro - CS4235 */
	{ .id = "CSCd925", .devs = { { "CSC0000" }, { "CSC0010" }, { "CSC0003" } } },
	/* ACER AW35/Pro - CS4237B */
	{ .id = "CSCd937", .devs = { { "CSC0000" }, { "CSC0010" }, { "CSC0003" } } },
	/* CS4235 without MPU401 */
	{ .id = "CSCe825", .devs = { { "CSC0100" }, { "CSC0110" } } },
	/* Unknown SiS530 - CS4235 */
	{ .id = "CSC4825", .devs = { { "CSC0100" }, { "CSC0110" } } },
	/* IBM IntelliStation M Pro 6898 11U - CS4236B */
	{ .id = "CSCe835", .devs = { { "CSC0000" }, { "CSC0010" } } },
	/* IBM PC 300PL Onboard - CS4236B */
	{ .id = "CSCe836", .devs = { { "CSC0000" }, { "CSC0010" } } },
	/* Some noname CS4236 based card */
	{ .id = "CSCe936", .devs = { { "CSC0000" }, { "CSC0010" }, { "CSC0003" } } },
	/* CS4236B */
	{ .id = "CSCf235", .devs = { { "CSC0000" }, { "CSC0010" }, { "CSC0003" } } },
	/* CS4236B */
	{ .id = "CSCf238", .devs = { { "CSC0000" }, { "CSC0010" }, { "CSC0003" } } },
	/* --- */
	{ .id = "" }	/* end */
};
#endif

MODULE_DEVICE_TABLE(pnp_card, snd_cs423x_pnpids);

/* WSS initialization */
static int __devinit snd_cs423x_pnp_init_wss(int dev, struct pnp_dev *pdev,
					     struct pnp_resource_table *cfg)
{
	int err;

	pnp_init_resource_table(cfg);
	if (port[dev] != SNDRV_AUTO_PORT)
		pnp_resource_change(&cfg->port_resource[0], port[dev], 4);
	if (fm_port[dev] != SNDRV_AUTO_PORT && fm_port[dev] > 0)
		pnp_resource_change(&cfg->port_resource[1], fm_port[dev], 4);
	if (sb_port[dev] != SNDRV_AUTO_PORT)
		pnp_resource_change(&cfg->port_resource[2], sb_port[dev], 16);
	if (irq[dev] != SNDRV_AUTO_IRQ)
		pnp_resource_change(&cfg->irq_resource[0], irq[dev], 1);
	if (dma1[dev] != SNDRV_AUTO_DMA)
		pnp_resource_change(&cfg->dma_resource[0], dma1[dev], 1);
	if (dma2[dev] != SNDRV_AUTO_DMA)
		pnp_resource_change(&cfg->dma_resource[1], dma2[dev] < 0 ? 4 : dma2[dev], 1);
	err = pnp_manual_config_dev(pdev, cfg, 0);
	if (err < 0)
		snd_printk(KERN_ERR IDENT " WSS PnP manual resources are invalid, using auto config\n");
	err = pnp_activate_dev(pdev);
	if (err < 0) {
		printk(KERN_ERR IDENT " WSS PnP configure failed for WSS (out of resources?)\n");
		return -EBUSY;
	}
	port[dev] = pnp_port_start(pdev, 0);
	if (fm_port[dev] > 0)
		fm_port[dev] = pnp_port_start(pdev, 1);
	sb_port[dev] = pnp_port_start(pdev, 2);
	irq[dev] = pnp_irq(pdev, 0);
	dma1[dev] = pnp_dma(pdev, 0);
	dma2[dev] = pnp_dma(pdev, 1) == 4 ? -1 : (int)pnp_dma(pdev, 1);
	snd_printdd("isapnp WSS: wss port=0x%lx, fm port=0x%lx, sb port=0x%lx\n",
			port[dev], fm_port[dev], sb_port[dev]);
	snd_printdd("isapnp WSS: irq=%i, dma1=%i, dma2=%i\n",
			irq[dev], dma1[dev], dma2[dev]);
	return 0;
}

/* CTRL initialization */
static int __devinit snd_cs423x_pnp_init_ctrl(int dev, struct pnp_dev *pdev,
					      struct pnp_resource_table *cfg)
{
	int err;

	pnp_init_resource_table(cfg);
	if (cport[dev] != SNDRV_AUTO_PORT)
		pnp_resource_change(&cfg->port_resource[0], cport[dev], 8);
	err = pnp_manual_config_dev(pdev, cfg, 0);
	if (err < 0)
		snd_printk(KERN_ERR IDENT " CTRL PnP manual resources are invalid, using auto config\n");
	err = pnp_activate_dev(pdev);
	if (err < 0) {
		printk(KERN_ERR IDENT " CTRL PnP configure failed for WSS (out of resources?)\n");
		return -EBUSY;
	}
	cport[dev] = pnp_port_start(pdev, 0);
	snd_printdd("isapnp CTRL: control port=0x%lx\n", cport[dev]);
	return 0;
}

/* MPU initialization */
static int __devinit snd_cs423x_pnp_init_mpu(int dev, struct pnp_dev *pdev,
					     struct pnp_resource_table *cfg)
{
	int err;

	pnp_init_resource_table(cfg);
	if (mpu_port[dev] != SNDRV_AUTO_PORT)
		pnp_resource_change(&cfg->port_resource[0], mpu_port[dev], 2);
	if (mpu_irq[dev] != SNDRV_AUTO_IRQ && mpu_irq[dev] >= 0)
		pnp_resource_change(&cfg->irq_resource[0], mpu_irq[dev], 1);
	err = pnp_manual_config_dev(pdev, cfg, 0);
	if (err < 0)
		snd_printk(KERN_ERR IDENT " MPU401 PnP manual resources are invalid, using auto config\n");
	err = pnp_activate_dev(pdev);
	if (err < 0) {
		printk(KERN_ERR IDENT " MPU401 PnP configure failed for WSS (out of resources?)\n");
		mpu_port[dev] = SNDRV_AUTO_PORT;
		mpu_irq[dev] = SNDRV_AUTO_IRQ;
	} else {
		mpu_port[dev] = pnp_port_start(pdev, 0);
		if (mpu_irq[dev] >= 0 &&
		    pnp_irq_valid(pdev, 0) && pnp_irq(pdev, 0) >= 0) {
			mpu_irq[dev] = pnp_irq(pdev, 0);
		} else {
			mpu_irq[dev] = -1;	/* disable interrupt */
		}
	}
	snd_printdd("isapnp MPU: port=0x%lx, irq=%i\n", mpu_port[dev], mpu_irq[dev]);
	return 0;
}

#ifdef CS4232
static int __devinit snd_card_cs4232_pnp(int dev, struct snd_card_cs4236 *acard,
					 struct pnp_dev *pdev)
{
	struct pnp_resource_table *cfg = kmalloc(sizeof(*cfg), GFP_KERNEL);

	if (!cfg)
		return -ENOMEM;
	if (snd_cs423x_pnp_init_wss(dev, acard->wss, cfg) < 0) {
		kfree(cfg);
		return -EBUSY;
	}
	kfree(cfg);
	cport[dev] = -1;
	return 0;
}
#endif

static int __devinit snd_card_cs423x_pnpc(int dev, struct snd_card_cs4236 *acard,
					  struct pnp_card_link *card,
					  const struct pnp_card_device_id *id)
{
	struct pnp_resource_table *cfg = kmalloc(sizeof(*cfg), GFP_KERNEL);

	if (!cfg)
		return -ENOMEM;

	acard->wss = pnp_request_card_device(card, id->devs[0].id, NULL);
	if (acard->wss == NULL)
		goto error;
	acard->ctrl = pnp_request_card_device(card, id->devs[1].id, NULL);
	if (acard->ctrl == NULL)
		goto error;
	if (id->devs[2].id[0]) {
		acard->mpu = pnp_request_card_device(card, id->devs[2].id, NULL);
		if (acard->mpu == NULL)
			goto error;
	}

	/* WSS initialization */
	if (snd_cs423x_pnp_init_wss(dev, acard->wss, cfg) < 0)
		goto error;

	/* CTRL initialization */
	if (acard->ctrl && cport[dev] > 0) {
		if (snd_cs423x_pnp_init_ctrl(dev, acard->ctrl, cfg) < 0)
			goto error;
	}
	/* MPU initialization */
	if (acard->mpu && mpu_port[dev] > 0) {
		if (snd_cs423x_pnp_init_mpu(dev, acard->mpu, cfg) < 0)
			goto error;
	}
	kfree(cfg);
	return 0;

 error:
	kfree(cfg);
	return -EBUSY;
}
#endif /* CONFIG_PNP */

#ifdef CONFIG_PNP
#define is_isapnp_selected(dev)		isapnp[dev]
#else
#define is_isapnp_selected(dev)		0
#endif

static void snd_card_cs4236_free(struct snd_card *card)
{
	struct snd_card_cs4236 *acard = card->private_data;

	release_and_free_resource(acard->res_sb_port);
}

static struct snd_card *snd_cs423x_card_new(int dev)
{
	struct snd_card *card;

	card = snd_card_new(index[dev], id[dev], THIS_MODULE,
			    sizeof(struct snd_card_cs4236));
	if (card == NULL)
		return NULL;
	card->private_free = snd_card_cs4236_free;
	return card;
}

static int __devinit snd_cs423x_probe(struct snd_card *card, int dev)
{
	struct snd_card_cs4236 *acard;
	struct snd_pcm *pcm;
	struct snd_cs4231 *chip;
	struct snd_opl3 *opl3;
	int err;

	acard = card->private_data;
	if (sb_port[dev] > 0 && sb_port[dev] != SNDRV_AUTO_PORT)
		if ((acard->res_sb_port = request_region(sb_port[dev], 16, IDENT " SB")) == NULL) {
			printk(KERN_ERR IDENT ": unable to register SB port at 0x%lx\n", sb_port[dev]);
			return -EBUSY;
		}

#ifdef CS4232
	if ((err = snd_cs4231_create(card,
				     port[dev],
				     cport[dev],
				     irq[dev],
				     dma1[dev],
				     dma2[dev],
				     CS4231_HW_DETECT,
				     0,
				     &chip)) < 0)
		return err;
	acard->chip = chip;

	if ((err = snd_cs4231_pcm(chip, 0, &pcm)) < 0)
		return err;

	if ((err = snd_cs4231_mixer(chip)) < 0)
		return err;

#else /* CS4236 */
	if ((err = snd_cs4236_create(card,
				     port[dev],
				     cport[dev],
				     irq[dev],
				     dma1[dev],
				     dma2[dev],
				     CS4231_HW_DETECT,
				     0,
				     &chip)) < 0)
		return err;
	acard->chip = chip;

	if ((err = snd_cs4236_pcm(chip, 0, &pcm)) < 0)
		return err;

	if ((err = snd_cs4236_mixer(chip)) < 0)
		return err;
#endif
	strcpy(card->driver, pcm->name);
	strcpy(card->shortname, pcm->name);
	sprintf(card->longname, "%s at 0x%lx, irq %i, dma %i",
		pcm->name,
		chip->port,
		irq[dev],
		dma1[dev]);
	if (dma2[dev] >= 0)
		sprintf(card->longname + strlen(card->longname), "&%d", dma2[dev]);

	if ((err = snd_cs4231_timer(chip, 0, NULL)) < 0)
		return err;

	if (fm_port[dev] > 0 && fm_port[dev] != SNDRV_AUTO_PORT) {
		if (snd_opl3_create(card,
				    fm_port[dev], fm_port[dev] + 2,
				    OPL3_HW_OPL3_CS, 0, &opl3) < 0) {
			printk(KERN_WARNING IDENT ": OPL3 not detected\n");
		} else {
			if ((err = snd_opl3_hwdep_new(opl3, 0, 1, NULL)) < 0)
				return err;
		}
	}

	if (mpu_port[dev] > 0 && mpu_port[dev] != SNDRV_AUTO_PORT) {
		if (mpu_irq[dev] == SNDRV_AUTO_IRQ)
			mpu_irq[dev] = -1;
		if (snd_mpu401_uart_new(card, 0, MPU401_HW_CS4232,
					mpu_port[dev], 0,
					mpu_irq[dev],
					mpu_irq[dev] >= 0 ? IRQF_DISABLED : 0, NULL) < 0)
			printk(KERN_WARNING IDENT ": MPU401 not detected\n");
	}

	return snd_card_register(card);
}

static int __devinit snd_cs423x_isa_match(struct device *pdev,
					  unsigned int dev)
{
	if (!enable[dev] || is_isapnp_selected(dev))
		return 0;

	if (port[dev] == SNDRV_AUTO_PORT) {
		snd_printk(KERN_ERR "%s: please specify port\n", pdev->bus_id);
		return 0;
	}
	if (cport[dev] == SNDRV_AUTO_PORT) {
		snd_printk(KERN_ERR "%s: please specify cport\n", pdev->bus_id);
		return 0;
	}
	if (irq[dev] == SNDRV_AUTO_IRQ) {
		snd_printk(KERN_ERR "%s: please specify irq\n", pdev->bus_id);
		return 0;
	}
	if (dma1[dev] == SNDRV_AUTO_DMA) {
		snd_printk(KERN_ERR "%s: please specify dma1\n", pdev->bus_id);
		return 0;
	}
	return 1;
}

static int __devinit snd_cs423x_isa_probe(struct device *pdev,
					  unsigned int dev)
{
	struct snd_card *card;
	int err;

	card = snd_cs423x_card_new(dev);
	if (! card)
		return -ENOMEM;
	snd_card_set_dev(card, pdev);
	if ((err = snd_cs423x_probe(card, dev)) < 0) {
		snd_card_free(card);
		return err;
	}

	dev_set_drvdata(pdev, card);
	return 0;
}

static int __devexit snd_cs423x_isa_remove(struct device *pdev,
					   unsigned int dev)
{
	snd_card_free(dev_get_drvdata(pdev));
	dev_set_drvdata(pdev, NULL);
	return 0;
}

#ifdef CONFIG_PM
static int snd_cs423x_suspend(struct snd_card *card)
{
	struct snd_card_cs4236 *acard = card->private_data;
	snd_power_change_state(card, SNDRV_CTL_POWER_D3hot);
	acard->chip->suspend(acard->chip);
	return 0;
}

static int snd_cs423x_resume(struct snd_card *card)
{
	struct snd_card_cs4236 *acard = card->private_data;
	acard->chip->resume(acard->chip);
	snd_power_change_state(card, SNDRV_CTL_POWER_D0);
	return 0;
}

static int snd_cs423x_isa_suspend(struct device *dev, unsigned int n,
				  pm_message_t state)
{
	return snd_cs423x_suspend(dev_get_drvdata(dev));
}

static int snd_cs423x_isa_resume(struct device *dev, unsigned int n)
{
	return snd_cs423x_resume(dev_get_drvdata(dev));
}
#endif

static struct isa_driver cs423x_isa_driver = {
	.match		= snd_cs423x_isa_match,
	.probe		= snd_cs423x_isa_probe,
	.remove		= __devexit_p(snd_cs423x_isa_remove),
#ifdef CONFIG_PM
	.suspend	= snd_cs423x_isa_suspend,
	.resume		= snd_cs423x_isa_resume,
#endif
	.driver		= {
		.name	= DEV_NAME
	},
};


#ifdef CONFIG_PNP
#ifdef CS4232
static int __devinit snd_cs4232_pnpbios_detect(struct pnp_dev *pdev,
					       const struct pnp_device_id *id)
{
	static int dev;
	int err;
	struct snd_card *card;

	if (pnp_device_is_isapnp(pdev))
		return -ENOENT;	/* we have another procedure - card */
	for (; dev < SNDRV_CARDS; dev++) {
		if (enable[dev] && isapnp[dev])
			break;
	}
	if (dev >= SNDRV_CARDS)
		return -ENODEV;

	card = snd_cs423x_card_new(dev);
	if (! card)
		return -ENOMEM;
	if ((err = snd_card_cs4232_pnp(dev, card->private_data, pdev)) < 0) {
		printk(KERN_ERR "PnP BIOS detection failed for " IDENT "\n");
		snd_card_free(card);
		return err;
	}
	snd_card_set_dev(card, &pdev->dev);
	if ((err = snd_cs423x_probe(card, dev)) < 0) {
		snd_card_free(card);
		return err;
	}
	pnp_set_drvdata(pdev, card);
	dev++;
	return 0;
}

static void __devexit snd_cs4232_pnp_remove(struct pnp_dev * pdev)
{
	snd_card_free(pnp_get_drvdata(pdev));
	pnp_set_drvdata(pdev, NULL);
}

#ifdef CONFIG_PM
static int snd_cs4232_pnp_suspend(struct pnp_dev *pdev, pm_message_t state)
{
	return snd_cs423x_suspend(pnp_get_drvdata(pdev));
}

static int snd_cs4232_pnp_resume(struct pnp_dev *pdev)
{
	return snd_cs423x_resume(pnp_get_drvdata(pdev));
}
#endif

static struct pnp_driver cs4232_pnp_driver = {
	.name = "cs4232-pnpbios",
	.id_table = snd_cs4232_pnpbiosids,
	.probe = snd_cs4232_pnpbios_detect,
	.remove = __devexit_p(snd_cs4232_pnp_remove),
#ifdef CONFIG_PM
	.suspend	= snd_cs4232_pnp_suspend,
	.resume		= snd_cs4232_pnp_resume,
#endif
};
#endif /* CS4232 */

static int __devinit snd_cs423x_pnpc_detect(struct pnp_card_link *pcard,
					    const struct pnp_card_device_id *pid)
{
	static int dev;
	struct snd_card *card;
	int res;

	for ( ; dev < SNDRV_CARDS; dev++) {
		if (enable[dev] && isapnp[dev])
			break;
	}
	if (dev >= SNDRV_CARDS)
		return -ENODEV;

	card = snd_cs423x_card_new(dev);
	if (! card)
		return -ENOMEM;
	if ((res = snd_card_cs423x_pnpc(dev, card->private_data, pcard, pid)) < 0) {
		printk(KERN_ERR "isapnp detection failed and probing for " IDENT
		       " is not supported\n");
		snd_card_free(card);
		return res;
	}
	snd_card_set_dev(card, &pcard->card->dev);
	if ((res = snd_cs423x_probe(card, dev)) < 0) {
		snd_card_free(card);
		return res;
	}
	pnp_set_card_drvdata(pcard, card);
	dev++;
	return 0;
}

static void __devexit snd_cs423x_pnpc_remove(struct pnp_card_link * pcard)
{
	snd_card_free(pnp_get_card_drvdata(pcard));
	pnp_set_card_drvdata(pcard, NULL);
}

#ifdef CONFIG_PM
static int snd_cs423x_pnpc_suspend(struct pnp_card_link *pcard, pm_message_t state)
{
	return snd_cs423x_suspend(pnp_get_card_drvdata(pcard));
}

static int snd_cs423x_pnpc_resume(struct pnp_card_link *pcard)
{
	return snd_cs423x_resume(pnp_get_card_drvdata(pcard));
}
#endif

static struct pnp_card_driver cs423x_pnpc_driver = {
	.flags = PNP_DRIVER_RES_DISABLE,
	.name = CS423X_ISAPNP_DRIVER,
	.id_table = snd_cs423x_pnpids,
	.probe = snd_cs423x_pnpc_detect,
	.remove = __devexit_p(snd_cs423x_pnpc_remove),
#ifdef CONFIG_PM
	.suspend	= snd_cs423x_pnpc_suspend,
	.resume		= snd_cs423x_pnpc_resume,
#endif
};
#endif /* CONFIG_PNP */

static int __init alsa_card_cs423x_init(void)
{
	int err;

	err = isa_register_driver(&cs423x_isa_driver, SNDRV_CARDS);
#ifdef CONFIG_PNP
	if (!err)
		isa_registered = 1;
#ifdef CS4232
	err = pnp_register_driver(&cs4232_pnp_driver);
	if (!err)
		pnp_registered = 1;
#endif
	err = pnp_register_card_driver(&cs423x_pnpc_driver);
	if (!err)
		pnpc_registered = 1;
#ifdef CS4232
	if (pnp_registered)
		err = 0;
#endif
	if (isa_registered)
		err = 0;
#endif
	return err;
}

static void __exit alsa_card_cs423x_exit(void)
{
#ifdef CONFIG_PNP
	if (pnpc_registered)
		pnp_unregister_card_driver(&cs423x_pnpc_driver);
#ifdef CS4232
	if (pnp_registered)
		pnp_unregister_driver(&cs4232_pnp_driver);
#endif
	if (isa_registered)
#endif
		isa_unregister_driver(&cs423x_isa_driver);
}

module_init(alsa_card_cs423x_init)
module_exit(alsa_card_cs423x_exit)
