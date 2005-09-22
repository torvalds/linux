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
#else
#define IDENT "CS4236+"
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

struct snd_card_cs4236 {
	struct resource *res_sb_port;
#ifdef CONFIG_PNP
	struct pnp_dev *wss;
	struct pnp_dev *ctrl;
	struct pnp_dev *mpu;
#endif
};

static snd_card_t *snd_cs4236_legacy[SNDRV_CARDS] = SNDRV_DEFAULT_PTR;

#ifdef CONFIG_PNP

#define ISAPNP_CS4232(_va, _vb, _vc, _device, _wss, _ctrl, _mpu401) \
	{ \
		ISAPNP_CARD_ID(_va, _vb, _vc, _device), \
		.devs = { ISAPNP_DEVICE_ID(_va, _vb, _vc, _wss), \
                          ISAPNP_DEVICE_ID(_va, _vb, _vc, _ctrl), \
			  ISAPNP_DEVICE_ID(_va, _vb, _vc, _mpu401) } \
        }
#define ISAPNP_CS4232_1(_va, _vb, _vc, _device, _wss, _ctrl, _mpu401) \
	{ \
		ISAPNP_CARD_ID(_va, _vb, _vc, _device), \
		.devs = { ISAPNP_DEVICE_ID(_va, _vb, _vc, _wss), \
                          ISAPNP_DEVICE_ID(_va, _vb, _vc, _ctrl), \
		 	  ISAPNP_DEVICE_ID('P', 'N', 'P', _mpu401) } \
        }
#define ISAPNP_CS4232_WOMPU(_va, _vb, _vc, _device, _wss, _ctrl) \
	{ \
		ISAPNP_CARD_ID(_va, _vb, _vc, _device), \
		.devs = { ISAPNP_DEVICE_ID(_va, _vb, _vc, _wss), \
                          ISAPNP_DEVICE_ID(_va, _vb, _vc, _ctrl) } \
        }


#ifdef CS4232
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
	/* --- */
	{ .id = "" }	/* end */
};
#else /* CS4236 */
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

static int __devinit snd_card_cs4236_pnp(int dev, struct snd_card_cs4236 *acard,
					 struct pnp_card_link *card,
					 const struct pnp_card_device_id *id)
{
	struct pnp_dev *pdev;
	struct pnp_resource_table * cfg = kmalloc(sizeof(struct pnp_resource_table), GFP_KERNEL);
	int err;

	if (!cfg)
		return -ENOMEM;

	acard->wss = pnp_request_card_device(card, id->devs[0].id, NULL);
	if (acard->wss == NULL) {
		kfree(cfg);
		return -EBUSY;
	}
	acard->ctrl = pnp_request_card_device(card, id->devs[1].id, NULL);
	if (acard->ctrl == NULL) {
		kfree(cfg);
		return -EBUSY;
	}
	if (id->devs[2].id[0]) {
		acard->mpu = pnp_request_card_device(card, id->devs[2].id, NULL);
		if (acard->mpu == NULL) {
			kfree(cfg);
			return -EBUSY;
		}
	}

	/* WSS initialization */
	pdev = acard->wss;
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
		kfree(cfg);
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
	/* CTRL initialization */
	if (acard->ctrl && cport[dev] > 0) {
		pdev = acard->ctrl;
		pnp_init_resource_table(cfg);
		if (cport[dev] != SNDRV_AUTO_PORT)
			pnp_resource_change(&cfg->port_resource[0], cport[dev], 8);
		err = pnp_manual_config_dev(pdev, cfg, 0);
		if (err < 0)
			snd_printk(KERN_ERR IDENT " CTRL PnP manual resources are invalid, using auto config\n");
		err = pnp_activate_dev(pdev);
		if (err < 0) {
			kfree(cfg);
			printk(KERN_ERR IDENT " CTRL PnP configure failed for WSS (out of resources?)\n");
			return -EBUSY;
		}
		cport[dev] = pnp_port_start(pdev, 0);
		snd_printdd("isapnp CTRL: control port=0x%lx\n", cport[dev]);
	}
	/* MPU initialization */
	if (acard->mpu && mpu_port[dev] > 0) {
		pdev = acard->mpu;
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
	}
	kfree(cfg);
	return 0;
}
#endif /* CONFIG_PNP */

static void snd_card_cs4236_free(snd_card_t *card)
{
	struct snd_card_cs4236 *acard = (struct snd_card_cs4236 *)card->private_data;

	if (acard) {
		if (acard->res_sb_port) {
			release_resource(acard->res_sb_port);
			kfree_nocheck(acard->res_sb_port);
		}
	}
}

#ifdef CONFIG_PNP
#define is_isapnp_selected(dev)		isapnp[dev]
#else
#define is_isapnp_selected(dev)		0
#endif

static int __devinit snd_card_cs423x_probe(int dev, struct pnp_card_link *pcard,
					   const struct pnp_card_device_id *pid)
{
	snd_card_t *card;
	struct snd_card_cs4236 *acard;
	snd_pcm_t *pcm = NULL;
	cs4231_t *chip;
	opl3_t *opl3;
	int err;

	if (! is_isapnp_selected(dev)) {
		if (port[dev] == SNDRV_AUTO_PORT) {
			snd_printk(KERN_ERR "specify port\n");
			return -EINVAL;
		}
		if (cport[dev] == SNDRV_AUTO_PORT) {
			snd_printk(KERN_ERR "specify cport\n");
			return -EINVAL;
		}
	}
	card = snd_card_new(index[dev], id[dev], THIS_MODULE,
			    sizeof(struct snd_card_cs4236));
	if (card == NULL)
		return -ENOMEM;
	acard = (struct snd_card_cs4236 *)card->private_data;
	card->private_free = snd_card_cs4236_free;
#ifdef CONFIG_PNP
	if (isapnp[dev]) {
		if ((err = snd_card_cs4236_pnp(dev, acard, pcard, pid))<0) {
			printk(KERN_ERR "isapnp detection failed and probing for " IDENT " is not supported\n");
			goto _err;
		}
		snd_card_set_dev(card, &pcard->card->dev);
	}
#endif
	if (sb_port[dev] > 0 && sb_port[dev] != SNDRV_AUTO_PORT)
		if ((acard->res_sb_port = request_region(sb_port[dev], 16, IDENT " SB")) == NULL) {
			printk(KERN_ERR IDENT ": unable to register SB port at 0x%lx\n", sb_port[dev]);
			err = -EBUSY;
			goto _err;
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
		goto _err;

	if ((err = snd_cs4231_pcm(chip, 0, &pcm)) < 0)
		goto _err;

	if ((err = snd_cs4231_mixer(chip)) < 0)
		goto _err;

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
		goto _err;

	if ((err = snd_cs4236_pcm(chip, 0, &pcm)) < 0)
		goto _err;

	if ((err = snd_cs4236_mixer(chip)) < 0)
		goto _err;
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
		goto _err;

	if (fm_port[dev] > 0 && fm_port[dev] != SNDRV_AUTO_PORT) {
		if (snd_opl3_create(card,
				    fm_port[dev], fm_port[dev] + 2,
				    OPL3_HW_OPL3_CS, 0, &opl3) < 0) {
			printk(KERN_WARNING IDENT ": OPL3 not detected\n");
		} else {
			if ((err = snd_opl3_hwdep_new(opl3, 0, 1, NULL)) < 0)
				goto _err;
		}
	}

	if (mpu_port[dev] > 0 && mpu_port[dev] != SNDRV_AUTO_PORT) {
		if (mpu_irq[dev] == SNDRV_AUTO_IRQ)
			mpu_irq[dev] = -1;
		if (snd_mpu401_uart_new(card, 0, MPU401_HW_CS4232,
					mpu_port[dev], 0,
					mpu_irq[dev],
					mpu_irq[dev] >= 0 ? SA_INTERRUPT : 0, NULL) < 0)
			printk(KERN_WARNING IDENT ": MPU401 not detected\n");
	}

	if ((err = snd_card_set_generic_dev(card)) < 0)
		goto _err;

	if ((err = snd_card_register(card)) < 0)
		goto _err;
	if (pcard)
		pnp_set_card_drvdata(pcard, card);
	else
		snd_cs4236_legacy[dev] = card;
	return 0;

 _err:
	snd_card_free(card);
	return err;
}

#ifdef CONFIG_PNP
static int __devinit snd_cs423x_pnp_detect(struct pnp_card_link *card,
					   const struct pnp_card_device_id *id)
{
	static int dev;
	int res;

	for ( ; dev < SNDRV_CARDS; dev++) {
		if (!enable[dev] || !isapnp[dev])
			continue;
		res = snd_card_cs423x_probe(dev, card, id);
		if (res < 0)
			return res;
		dev++;
		return 0;
	}
	return -ENODEV;
}

static void __devexit snd_cs423x_pnp_remove(struct pnp_card_link * pcard)
{
	snd_card_t *card = (snd_card_t *) pnp_get_card_drvdata(pcard);
        
	snd_card_disconnect(card);
	snd_card_free_in_thread(card);
}
                        
static struct pnp_card_driver cs423x_pnpc_driver = {
	.flags = PNP_DRIVER_RES_DISABLE,
	.name = "cs423x",
	.id_table = snd_cs423x_pnpids,
	.probe = snd_cs423x_pnp_detect,
	.remove = __devexit_p(snd_cs423x_pnp_remove),
};
#endif /* CONFIG_PNP */

static int __init alsa_card_cs423x_init(void)
{
	int dev, cards = 0;

	for (dev = 0; dev < SNDRV_CARDS; dev++) {
		if (!enable[dev])
			continue;
		if (is_isapnp_selected(dev))
			continue;
		if (snd_card_cs423x_probe(dev, NULL, NULL) >= 0)
			cards++;
	}
#ifdef CONFIG_PNP
	cards += pnp_register_card_driver(&cs423x_pnpc_driver);
#endif
	if (!cards) {
#ifdef CONFIG_PNP
		pnp_unregister_card_driver(&cs423x_pnpc_driver);
#endif
#ifdef MODULE
		printk(KERN_ERR IDENT " soundcard not found or device busy\n");
#endif
		return -ENODEV;
	}
	return 0;
}

static void __exit alsa_card_cs423x_exit(void)
{
	int idx;

#ifdef CONFIG_PNP
	/* PnP cards first */
	pnp_unregister_card_driver(&cs423x_pnpc_driver);
#endif
	for (idx = 0; idx < SNDRV_CARDS; idx++)
		snd_card_free(snd_cs4236_legacy[idx]);
}

module_init(alsa_card_cs423x_init)
module_exit(alsa_card_cs423x_exit)
