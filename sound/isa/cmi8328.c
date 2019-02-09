/*
 * Driver for C-Media CMI8328-based soundcards, such as AudioExcel AV500
 * Copyright (c) 2012 Ondrej Zary
 *
 * AudioExcel AV500 card consists of:
 *  - CMI8328 - main chip (SB Pro emulation, gameport, OPL3, MPU401, CD-ROM)
 *  - CS4231A - WSS codec
 *  - Dream SAM9233+GMS950400+RAM+ROM: Wavetable MIDI, connected to MPU401
 */

#include <linux/init.h>
#include <linux/isa.h>
#include <linux/module.h>
#include <linux/gameport.h>
#include <asm/dma.h>
#include <sound/core.h>
#include <sound/wss.h>
#include <sound/opl3.h>
#include <sound/mpu401.h>
#define SNDRV_LEGACY_FIND_FREE_IOPORT
#define SNDRV_LEGACY_FIND_FREE_IRQ
#define SNDRV_LEGACY_FIND_FREE_DMA
#include <sound/initval.h>

MODULE_AUTHOR("Ondrej Zary <linux@rainbow-software.org>");
MODULE_DESCRIPTION("C-Media CMI8328");
MODULE_LICENSE("GPL");

#if IS_ENABLED(CONFIG_GAMEPORT)
#define SUPPORT_JOYSTICK 1
#endif

/* I/O port is configured by jumpers on the card to one of these */
static int cmi8328_ports[] = { 0x530, 0xe80, 0xf40, 0x604 };
#define CMI8328_MAX	ARRAY_SIZE(cmi8328_ports)

static int index[CMI8328_MAX] =     {[0 ... (CMI8328_MAX-1)] = -1};
static char *id[CMI8328_MAX] =      {[0 ... (CMI8328_MAX-1)] = NULL};
static long port[CMI8328_MAX] =     {[0 ... (CMI8328_MAX-1)] = SNDRV_AUTO_PORT};
static int irq[CMI8328_MAX] =       {[0 ... (CMI8328_MAX-1)] = SNDRV_AUTO_IRQ};
static int dma1[CMI8328_MAX] =      {[0 ... (CMI8328_MAX-1)] = SNDRV_AUTO_DMA};
static int dma2[CMI8328_MAX] =      {[0 ... (CMI8328_MAX-1)] = SNDRV_AUTO_DMA};
static long mpuport[CMI8328_MAX] =  {[0 ... (CMI8328_MAX-1)] = SNDRV_AUTO_PORT};
static int mpuirq[CMI8328_MAX] =    {[0 ... (CMI8328_MAX-1)] = SNDRV_AUTO_IRQ};
#ifdef SUPPORT_JOYSTICK
static bool gameport[CMI8328_MAX] = {[0 ... (CMI8328_MAX-1)] = true};
#endif

module_param_array(index, int, NULL, 0444);
MODULE_PARM_DESC(index, "Index value for CMI8328 soundcard.");
module_param_array(id, charp, NULL, 0444);
MODULE_PARM_DESC(id, "ID string for CMI8328 soundcard.");

module_param_hw_array(port, long, ioport, NULL, 0444);
MODULE_PARM_DESC(port, "Port # for CMI8328 driver.");
module_param_hw_array(irq, int, irq, NULL, 0444);
MODULE_PARM_DESC(irq, "IRQ # for CMI8328 driver.");
module_param_hw_array(dma1, int, dma, NULL, 0444);
MODULE_PARM_DESC(dma1, "DMA1 for CMI8328 driver.");
module_param_hw_array(dma2, int, dma, NULL, 0444);
MODULE_PARM_DESC(dma2, "DMA2 for CMI8328 driver.");

module_param_hw_array(mpuport, long, ioport, NULL, 0444);
MODULE_PARM_DESC(mpuport, "MPU-401 port # for CMI8328 driver.");
module_param_hw_array(mpuirq, int, irq, NULL, 0444);
MODULE_PARM_DESC(mpuirq, "IRQ # for CMI8328 MPU-401 port.");
#ifdef SUPPORT_JOYSTICK
module_param_array(gameport, bool, NULL, 0444);
MODULE_PARM_DESC(gameport, "Enable gameport.");
#endif

struct snd_cmi8328 {
	u16 port;
	u8 cfg[3];
	u8 wss_cfg;
	struct snd_card *card;
	struct snd_wss *wss;
#ifdef SUPPORT_JOYSTICK
	struct gameport *gameport;
#endif
};

/* CMI8328 configuration registers */
#define CFG1 0x61
#define CFG1_SB_DISABLE	(1 << 0)
#define CFG1_GAMEPORT	(1 << 1)
/*
 * bit 0:    SB: 0=enabled, 1=disabled
 * bit 1:    gameport: 0=disabled, 1=enabled
 * bits 2-4: SB IRQ: 001=3, 010=5, 011=7, 100=9, 101=10, 110=11
 * bits 5-6: SB DMA: 00=disabled (when SB disabled), 01=DMA0, 10=DMA1, 11=DMA3
 * bit 7:    SB port: 0=0x220, 1=0x240
 */
#define CFG2 0x62
#define CFG2_MPU_ENABLE (1 << 2)
/*
 * bits 0-1: CD-ROM mode: 00=disabled, 01=Panasonic, 10=Sony/Mitsumi/Wearnes,
			  11=IDE
 * bit 2:    MPU401: 0=disabled, 1=enabled
 * bits 3-4: MPU401 IRQ: 00=3, 01=5, 10=7, 11=9,
 * bits 5-7: MPU401 port: 000=0x300, 001=0x310, 010=0x320, 011=0x330, 100=0x332,
			  101=0x334, 110=0x336
 */
#define CFG3 0x63
/*
 * bits 0-2: CD-ROM IRQ: 000=disabled, 001=3, 010=5, 011=7, 100=9, 101=10,
			 110=11
 * bits 3-4: CD-ROM DMA: 00=disabled, 01=DMA0, 10=DMA1, 11=DMA3
 * bits 5-7: CD-ROM port: 000=0x300, 001=0x310, 010=0x320, 011=0x330, 100=0x340,
			  101=0x350, 110=0x360, 111=0x370
 */

static u8 snd_cmi8328_cfg_read(u16 port, u8 reg)
{
	outb(0x43, port + 3);
	outb(0x21, port + 3);
	outb(reg, port + 3);
	return inb(port);
}

static void snd_cmi8328_cfg_write(u16 port, u8 reg, u8 val)
{
	outb(0x43, port + 3);
	outb(0x21, port + 3);
	outb(reg, port + 3);
	outb(val, port + 3);	/* yes, value goes to the same port as index */
}

#ifdef CONFIG_PM
static void snd_cmi8328_cfg_save(u16 port, u8 cfg[])
{
	cfg[0] = snd_cmi8328_cfg_read(port, CFG1);
	cfg[1] = snd_cmi8328_cfg_read(port, CFG2);
	cfg[2] = snd_cmi8328_cfg_read(port, CFG3);
}

static void snd_cmi8328_cfg_restore(u16 port, u8 cfg[])
{
	snd_cmi8328_cfg_write(port, CFG1, cfg[0]);
	snd_cmi8328_cfg_write(port, CFG2, cfg[1]);
	snd_cmi8328_cfg_write(port, CFG3, cfg[2]);
}
#endif /* CONFIG_PM */

static int snd_cmi8328_mixer(struct snd_wss *chip)
{
	struct snd_card *card;
	struct snd_ctl_elem_id id1, id2;
	int err;

	card = chip->card;

	memset(&id1, 0, sizeof(id1));
	memset(&id2, 0, sizeof(id2));
	id1.iface = id2.iface = SNDRV_CTL_ELEM_IFACE_MIXER;
	/* rename AUX0 switch to CD */
	strcpy(id1.name, "Aux Playback Switch");
	strcpy(id2.name, "CD Playback Switch");
	err = snd_ctl_rename_id(card, &id1, &id2);
	if (err < 0) {
		snd_printk(KERN_ERR "error renaming control\n");
		return err;
	}
	/* rename AUX0 volume to CD */
	strcpy(id1.name, "Aux Playback Volume");
	strcpy(id2.name, "CD Playback Volume");
	err = snd_ctl_rename_id(card, &id1, &id2);
	if (err < 0) {
		snd_printk(KERN_ERR "error renaming control\n");
		return err;
	}
	/* rename AUX1 switch to Synth */
	strcpy(id1.name, "Aux Playback Switch");
	id1.index = 1;
	strcpy(id2.name, "Synth Playback Switch");
	err = snd_ctl_rename_id(card, &id1, &id2);
	if (err < 0) {
		snd_printk(KERN_ERR "error renaming control\n");
		return err;
	}
	/* rename AUX1 volume to Synth */
	strcpy(id1.name, "Aux Playback Volume");
	id1.index = 1;
	strcpy(id2.name, "Synth Playback Volume");
	err = snd_ctl_rename_id(card, &id1, &id2);
	if (err < 0) {
		snd_printk(KERN_ERR "error renaming control\n");
		return err;
	}

	return 0;
}

/* find index of an item in "-1"-ended array */
static int array_find(int array[], int item)
{
	int i;

	for (i = 0; array[i] != -1; i++)
		if (array[i] == item)
			return i;

	return -1;
}
/* the same for long */
static int array_find_l(long array[], long item)
{
	int i;

	for (i = 0; array[i] != -1; i++)
		if (array[i] == item)
			return i;

	return -1;
}

static int snd_cmi8328_probe(struct device *pdev, unsigned int ndev)
{
	struct snd_card *card;
	struct snd_opl3 *opl3;
	struct snd_cmi8328 *cmi;
#ifdef SUPPORT_JOYSTICK
	struct resource *res;
#endif
	int err, pos;
	static long mpu_ports[] = { 0x330, 0x300, 0x310, 0x320, 0x332, 0x334,
				   0x336, -1 };
	static u8 mpu_port_bits[] = { 3, 0, 1, 2, 4, 5, 6 };
	static int mpu_irqs[] = { 9, 7, 5, 3, -1 };
	static u8 mpu_irq_bits[] = { 3, 2, 1, 0 };
	static int irqs[] = { 9, 10, 11, 7, -1 };
	static u8 irq_bits[] = { 2, 3, 4, 1 };
	static int dma1s[] = { 3, 1, 0, -1 };
	static u8 dma_bits[] = { 3, 2, 1 };
	static int dma2s[][2] = { {1, -1}, {0, -1}, {-1, -1}, {0, -1} };
	u16 port = cmi8328_ports[ndev];
	u8 val;

	/* 0xff is invalid configuration (but settable - hope it isn't set) */
	if (snd_cmi8328_cfg_read(port, CFG1) == 0xff)
		return -ENODEV;
	/* the SB disable bit must NEVER EVER be cleared or the WSS dies */
	snd_cmi8328_cfg_write(port, CFG1, CFG1_SB_DISABLE);
	if (snd_cmi8328_cfg_read(port, CFG1) != CFG1_SB_DISABLE)
		return -ENODEV;
	/* disable everything first */
	snd_cmi8328_cfg_write(port, CFG2, 0);	/* disable CDROM and MPU401 */
	snd_cmi8328_cfg_write(port, CFG3, 0);	/* disable CDROM IRQ and DMA */

	if (irq[ndev] == SNDRV_AUTO_IRQ) {
		irq[ndev] = snd_legacy_find_free_irq(irqs);
		if (irq[ndev] < 0) {
			snd_printk(KERN_ERR "unable to find a free IRQ\n");
			return -EBUSY;
		}
	}
	if (dma1[ndev] == SNDRV_AUTO_DMA) {
		dma1[ndev] = snd_legacy_find_free_dma(dma1s);
		if (dma1[ndev] < 0) {
			snd_printk(KERN_ERR "unable to find a free DMA1\n");
			return -EBUSY;
		}
	}
	if (dma2[ndev] == SNDRV_AUTO_DMA) {
		dma2[ndev] = snd_legacy_find_free_dma(dma2s[dma1[ndev] % 4]);
		if (dma2[ndev] < 0) {
			snd_printk(KERN_WARNING "unable to find a free DMA2, full-duplex will not work\n");
			dma2[ndev] = -1;
		}
	}
	/* configure WSS IRQ... */
	pos = array_find(irqs, irq[ndev]);
	if (pos < 0) {
		snd_printk(KERN_ERR "invalid IRQ %d\n", irq[ndev]);
		return -EINVAL;
	}
	val = irq_bits[pos] << 3;
	/* ...and DMA... */
	pos = array_find(dma1s, dma1[ndev]);
	if (pos < 0) {
		snd_printk(KERN_ERR "invalid DMA1 %d\n", dma1[ndev]);
		return -EINVAL;
	}
	val |= dma_bits[pos];
	/* ...and DMA2 */
	if (dma2[ndev] >= 0 && dma1[ndev] != dma2[ndev]) {
		pos = array_find(dma2s[dma1[ndev]], dma2[ndev]);
		if (pos < 0) {
			snd_printk(KERN_ERR "invalid DMA2 %d\n", dma2[ndev]);
			return -EINVAL;
		}
		val |= 0x04; /* enable separate capture DMA */
	}
	outb(val, port);

	err = snd_card_new(pdev, index[ndev], id[ndev], THIS_MODULE,
			   sizeof(struct snd_cmi8328), &card);
	if (err < 0)
		return err;
	cmi = card->private_data;
	cmi->card = card;
	cmi->port = port;
	cmi->wss_cfg = val;

	err = snd_wss_create(card, port + 4, -1, irq[ndev], dma1[ndev],
			dma2[ndev], WSS_HW_DETECT, 0, &cmi->wss);
	if (err < 0)
		goto error;

	err = snd_wss_pcm(cmi->wss, 0);
	if (err < 0)
		goto error;

	err = snd_wss_mixer(cmi->wss);
	if (err < 0)
		goto error;
	err = snd_cmi8328_mixer(cmi->wss);
	if (err < 0)
		goto error;

	if (snd_wss_timer(cmi->wss, 0) < 0)
		snd_printk(KERN_WARNING "error initializing WSS timer\n");

	if (mpuport[ndev] == SNDRV_AUTO_PORT) {
		mpuport[ndev] = snd_legacy_find_free_ioport(mpu_ports, 2);
		if (mpuport[ndev] < 0)
			snd_printk(KERN_ERR "unable to find a free MPU401 port\n");
	}
	if (mpuirq[ndev] == SNDRV_AUTO_IRQ) {
		mpuirq[ndev] = snd_legacy_find_free_irq(mpu_irqs);
		if (mpuirq[ndev] < 0)
			snd_printk(KERN_ERR "unable to find a free MPU401 IRQ\n");
	}
	/* enable and configure MPU401 */
	if (mpuport[ndev] > 0 && mpuirq[ndev] > 0) {
		val = CFG2_MPU_ENABLE;
		pos = array_find_l(mpu_ports, mpuport[ndev]);
		if (pos < 0)
			snd_printk(KERN_WARNING "invalid MPU401 port 0x%lx\n",
								mpuport[ndev]);
		else {
			val |= mpu_port_bits[pos] << 5;
			pos = array_find(mpu_irqs, mpuirq[ndev]);
			if (pos < 0)
				snd_printk(KERN_WARNING "invalid MPU401 IRQ %d\n",
								mpuirq[ndev]);
			else {
				val |= mpu_irq_bits[pos] << 3;
				snd_cmi8328_cfg_write(port, CFG2, val);
				if (snd_mpu401_uart_new(card, 0,
						MPU401_HW_MPU401, mpuport[ndev],
						0, mpuirq[ndev], NULL) < 0)
					snd_printk(KERN_ERR "error initializing MPU401\n");
			}
		}
	}
	/* OPL3 is hardwired to 0x388 and cannot be disabled */
	if (snd_opl3_create(card, 0x388, 0x38a, OPL3_HW_AUTO, 0, &opl3) < 0)
		snd_printk(KERN_ERR "error initializing OPL3\n");
	else
		if (snd_opl3_hwdep_new(opl3, 0, 1, NULL) < 0)
			snd_printk(KERN_WARNING "error initializing OPL3 hwdep\n");

	strcpy(card->driver, "CMI8328");
	strcpy(card->shortname, "C-Media CMI8328");
	sprintf(card->longname, "%s at 0x%lx, irq %d, dma %d,%d",
		card->shortname, cmi->wss->port, irq[ndev], dma1[ndev],
		(dma2[ndev] >= 0) ? dma2[ndev] : dma1[ndev]);

	dev_set_drvdata(pdev, card);
	err = snd_card_register(card);
	if (err < 0)
		goto error;
#ifdef SUPPORT_JOYSTICK
	if (!gameport[ndev])
		return 0;
	/* gameport is hardwired to 0x200 */
	res = request_region(0x200, 8, "CMI8328 gameport");
	if (!res)
		snd_printk(KERN_WARNING "unable to allocate gameport I/O port\n");
	else {
		struct gameport *gp = cmi->gameport = gameport_allocate_port();
		if (!cmi->gameport)
			release_and_free_resource(res);
		else {
			gameport_set_name(gp, "CMI8328 Gameport");
			gameport_set_phys(gp, "%s/gameport0", dev_name(pdev));
			gameport_set_dev_parent(gp, pdev);
			gp->io = 0x200;
			gameport_set_port_data(gp, res);
			/* Enable gameport */
			snd_cmi8328_cfg_write(port, CFG1,
					CFG1_SB_DISABLE | CFG1_GAMEPORT);
			gameport_register_port(gp);
		}
	}
#endif
	return 0;
error:
	snd_card_free(card);

	return err;
}

static int snd_cmi8328_remove(struct device *pdev, unsigned int dev)
{
	struct snd_card *card = dev_get_drvdata(pdev);
	struct snd_cmi8328 *cmi = card->private_data;

#ifdef SUPPORT_JOYSTICK
	if (cmi->gameport) {
		struct resource *res = gameport_get_port_data(cmi->gameport);
		gameport_unregister_port(cmi->gameport);
		release_and_free_resource(res);
	}
#endif
	/* disable everything */
	snd_cmi8328_cfg_write(cmi->port, CFG1, CFG1_SB_DISABLE);
	snd_cmi8328_cfg_write(cmi->port, CFG2, 0);
	snd_cmi8328_cfg_write(cmi->port, CFG3, 0);
	snd_card_free(card);
	return 0;
}

#ifdef CONFIG_PM
static int snd_cmi8328_suspend(struct device *pdev, unsigned int n,
				pm_message_t state)
{
	struct snd_card *card = dev_get_drvdata(pdev);
	struct snd_cmi8328 *cmi;

	if (!card)	/* ignore absent devices */
		return 0;
	cmi = card->private_data;
	snd_cmi8328_cfg_save(cmi->port, cmi->cfg);
	snd_power_change_state(card, SNDRV_CTL_POWER_D3hot);
	snd_pcm_suspend_all(cmi->wss->pcm);
	cmi->wss->suspend(cmi->wss);

	return 0;
}

static int snd_cmi8328_resume(struct device *pdev, unsigned int n)
{
	struct snd_card *card = dev_get_drvdata(pdev);
	struct snd_cmi8328 *cmi;

	if (!card)	/* ignore absent devices */
		return 0;
	cmi = card->private_data;
	snd_cmi8328_cfg_restore(cmi->port, cmi->cfg);
	outb(cmi->wss_cfg, cmi->port);
	cmi->wss->resume(cmi->wss);
	snd_power_change_state(card, SNDRV_CTL_POWER_D0);

	return 0;
}
#endif

static struct isa_driver snd_cmi8328_driver = {
	.probe		= snd_cmi8328_probe,
	.remove		= snd_cmi8328_remove,
#ifdef CONFIG_PM
	.suspend	= snd_cmi8328_suspend,
	.resume		= snd_cmi8328_resume,
#endif
	.driver		= {
		.name	= "cmi8328"
	},
};

module_isa_driver(snd_cmi8328_driver, CMI8328_MAX);
