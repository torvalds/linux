/*
 *  Driver for AMD InterWave soundcard
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
 *   1999/07/22		Erik Inge Bolso <knan@mo.himolde.no>
 *			* mixer group handlers
 *
 */

#include <sound/driver.h>
#include <asm/dma.h>
#include <linux/delay.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/pnp.h>
#include <linux/moduleparam.h>
#include <sound/core.h>
#include <sound/gus.h>
#include <sound/cs4231.h>
#ifdef SNDRV_STB
#include <sound/tea6330t.h>
#endif
#define SNDRV_LEGACY_AUTO_PROBE
#define SNDRV_LEGACY_FIND_FREE_IRQ
#define SNDRV_LEGACY_FIND_FREE_DMA
#include <sound/initval.h>

MODULE_AUTHOR("Jaroslav Kysela <perex@suse.cz>");
MODULE_LICENSE("GPL");
#ifndef SNDRV_STB
MODULE_DESCRIPTION("AMD InterWave");
MODULE_SUPPORTED_DEVICE("{{Gravis,UltraSound Plug & Play},"
		"{STB,SoundRage32},"
		"{MED,MED3210},"
		"{Dynasonix,Dynasonix Pro},"
		"{Panasonic,PCA761AW}}");
#else
MODULE_DESCRIPTION("AMD InterWave STB with TEA6330T");
MODULE_SUPPORTED_DEVICE("{{AMD,InterWave STB with TEA6330T}}");
#endif

static int index[SNDRV_CARDS] = SNDRV_DEFAULT_IDX;	/* Index 0-MAX */
static char *id[SNDRV_CARDS] = SNDRV_DEFAULT_STR;	/* ID for this card */
static int enable[SNDRV_CARDS] = SNDRV_DEFAULT_ENABLE_ISAPNP; /* Enable this card */
#ifdef CONFIG_PNP
static int isapnp[SNDRV_CARDS] = {[0 ... (SNDRV_CARDS - 1)] = 1};
#endif
static long port[SNDRV_CARDS] = SNDRV_DEFAULT_PORT;	/* 0x210,0x220,0x230,0x240,0x250,0x260 */
#ifdef SNDRV_STB
static long port_tc[SNDRV_CARDS] = SNDRV_DEFAULT_PORT;	/* 0x350,0x360,0x370,0x380 */
#endif
static int irq[SNDRV_CARDS] = SNDRV_DEFAULT_IRQ;	/* 2,3,5,9,11,12,15 */
static int dma1[SNDRV_CARDS] = SNDRV_DEFAULT_DMA;	/* 0,1,3,5,6,7 */
static int dma2[SNDRV_CARDS] = SNDRV_DEFAULT_DMA;	/* 0,1,3,5,6,7 */
static int joystick_dac[SNDRV_CARDS] = {[0 ... (SNDRV_CARDS - 1)] = 29};
				/* 0 to 31, (0.59V-4.52V or 0.389V-2.98V) */
static int midi[SNDRV_CARDS] = {[0 ... (SNDRV_CARDS - 1)] = 0};
static int pcm_channels[SNDRV_CARDS] = {[0 ... (SNDRV_CARDS - 1)] = 2};
static int effect[SNDRV_CARDS] = {[0 ... (SNDRV_CARDS - 1)] = 0};

#ifdef SNDRV_STB
#define PFX "interwave-stb: "
#else
#define PFX "interwave: "
#endif

module_param_array(index, int, NULL, 0444);
MODULE_PARM_DESC(index, "Index value for InterWave soundcard.");
module_param_array(id, charp, NULL, 0444);
MODULE_PARM_DESC(id, "ID string for InterWave soundcard.");
module_param_array(enable, bool, NULL, 0444);
MODULE_PARM_DESC(enable, "Enable InterWave soundcard.");
#ifdef CONFIG_PNP
module_param_array(isapnp, bool, NULL, 0444);
MODULE_PARM_DESC(isapnp, "ISA PnP detection for specified soundcard.");
#endif
module_param_array(port, long, NULL, 0444);
MODULE_PARM_DESC(port, "Port # for InterWave driver.");
#ifdef SNDRV_STB
module_param_array(port_tc, long, NULL, 0444);
MODULE_PARM_DESC(port_tc, "Tone control (TEA6330T - i2c bus) port # for InterWave driver.");
#endif
module_param_array(irq, int, NULL, 0444);
MODULE_PARM_DESC(irq, "IRQ # for InterWave driver.");
module_param_array(dma1, int, NULL, 0444);
MODULE_PARM_DESC(dma1, "DMA1 # for InterWave driver.");
module_param_array(dma2, int, NULL, 0444);
MODULE_PARM_DESC(dma2, "DMA2 # for InterWave driver.");
module_param_array(joystick_dac, int, NULL, 0444);
MODULE_PARM_DESC(joystick_dac, "Joystick DAC level 0.59V-4.52V or 0.389V-2.98V for InterWave driver.");
module_param_array(midi, int, NULL, 0444);
MODULE_PARM_DESC(midi, "MIDI UART enable for InterWave driver.");
module_param_array(pcm_channels, int, NULL, 0444);
MODULE_PARM_DESC(pcm_channels, "Reserved PCM channels for InterWave driver.");
module_param_array(effect, int, NULL, 0444);
MODULE_PARM_DESC(effect, "Effects enable for InterWave driver.");

struct snd_interwave {
	int irq;
	snd_card_t *card;
	snd_gus_card_t *gus;
	cs4231_t *cs4231;
#ifdef SNDRV_STB
	struct resource *i2c_res;
#endif
	unsigned short gus_status_reg;
	unsigned short pcm_status_reg;
#ifdef CONFIG_PNP
	struct pnp_dev *dev;
#ifdef SNDRV_STB
	struct pnp_dev *devtc;
#endif
#endif
};

static snd_card_t *snd_interwave_legacy[SNDRV_CARDS] = SNDRV_DEFAULT_PTR;

#ifdef CONFIG_PNP

static struct pnp_card_device_id snd_interwave_pnpids[] = {
#ifndef SNDRV_STB
	/* Gravis UltraSound Plug & Play */
	{ .id = "GRV0001", .devs = { { .id = "GRV0000" } } },
	/* STB SoundRage32 */
	{ .id = "STB011a", .devs = { { .id = "STB0010" } } },
	/* MED3210 */
	{ .id = "DXP3201", .devs = { { .id = "DXP0010" } } },
	/* Dynasonic Pro */
	/* This device also have CDC1117:DynaSonix Pro Audio Effects Processor */
	{ .id = "CDC1111", .devs = { { .id = "CDC1112" } } },
	/* Panasonic PCA761AW Audio Card */
	{ .id = "ADV55ff", .devs = { { .id = "ADV0010" } } },
	/* InterWave STB without TEA6330T */
	{ .id = "ADV550a", .devs = { { .id = "ADV0010" } } },
#else
	/* InterWave STB with TEA6330T */
	{ .id = "ADV550a", .devs = { { .id = "ADV0010" }, { .id = "ADV0015" } } },
#endif
	{ .id = "" }
};

MODULE_DEVICE_TABLE(pnp_card, snd_interwave_pnpids);

#endif /* CONFIG_PNP */


#ifdef SNDRV_STB
static void snd_interwave_i2c_setlines(snd_i2c_bus_t *bus, int ctrl, int data)
{
	unsigned long port = bus->private_value;

#if 0
	printk("i2c_setlines - 0x%lx <- %i,%i\n", port, ctrl, data);
#endif
	outb((data << 1) | ctrl, port);
	udelay(10);
}

static int snd_interwave_i2c_getclockline(snd_i2c_bus_t *bus)
{
	unsigned long port = bus->private_value;
	unsigned char res;

	res = inb(port) & 1;
#if 0
	printk("i2c_getclockline - 0x%lx -> %i\n", port, res);
#endif
	return res;
}

static int snd_interwave_i2c_getdataline(snd_i2c_bus_t *bus, int ack)
{
	unsigned long port = bus->private_value;
	unsigned char res;

	if (ack)
		udelay(10);
	res = (inb(port) & 2) >> 1;
#if 0
	printk("i2c_getdataline - 0x%lx -> %i\n", port, res);
#endif
	return res;
}

static snd_i2c_bit_ops_t snd_interwave_i2c_bit_ops = {
	.setlines = snd_interwave_i2c_setlines,
	.getclock = snd_interwave_i2c_getclockline,
	.getdata  = snd_interwave_i2c_getdataline,
};

static int __devinit snd_interwave_detect_stb(struct snd_interwave *iwcard,
					      snd_gus_card_t * gus, int dev,
					      snd_i2c_bus_t **rbus)
{
	unsigned long port;
	snd_i2c_bus_t *bus;
	snd_card_t *card = iwcard->card;
	char name[32];
	int err;

	*rbus = NULL;
	port = port_tc[dev];
	if (port == SNDRV_AUTO_PORT) {
		port = 0x350;
		if (gus->gf1.port == 0x250) {
			port = 0x360;
		}
		while (port <= 0x380) {
			if ((iwcard->i2c_res = request_region(port, 1, "InterWave (I2C bus)")) != NULL)
				break;
			port += 0x10;
		}
	} else {
		iwcard->i2c_res = request_region(port, 1, "InterWave (I2C bus)");
	}
	if (iwcard->i2c_res == NULL) {
		snd_printk(KERN_ERR "interwave: can't grab i2c bus port\n");
		return -ENODEV;
	}

	sprintf(name, "InterWave-%i", card->number);
	if ((err = snd_i2c_bus_create(card, name, NULL, &bus)) < 0)
		return err;
	bus->private_value = port;
	bus->hw_ops.bit = &snd_interwave_i2c_bit_ops;
	if ((err = snd_tea6330t_detect(bus, 0)) < 0)
		return err;
	*rbus = bus;
	return 0;
}
#endif

static int __devinit snd_interwave_detect(struct snd_interwave *iwcard,
				          snd_gus_card_t * gus,
				          int dev
#ifdef SNDRV_STB
				          , snd_i2c_bus_t **rbus
#endif
				          )
{
	unsigned long flags;
	unsigned char rev1, rev2;
	int d;

	snd_gf1_i_write8(gus, SNDRV_GF1_GB_RESET, 0);	/* reset GF1 */
	if (((d = snd_gf1_i_look8(gus, SNDRV_GF1_GB_RESET)) & 0x07) != 0) {
		snd_printdd("[0x%lx] check 1 failed - 0x%x\n", gus->gf1.port, d);
		return -ENODEV;
	}
	udelay(160);
	snd_gf1_i_write8(gus, SNDRV_GF1_GB_RESET, 1);	/* release reset */
	udelay(160);
	if (((d = snd_gf1_i_look8(gus, SNDRV_GF1_GB_RESET)) & 0x07) != 1) {
		snd_printdd("[0x%lx] check 2 failed - 0x%x\n", gus->gf1.port, d);
		return -ENODEV;
	}
	spin_lock_irqsave(&gus->reg_lock, flags);
	rev1 = snd_gf1_look8(gus, SNDRV_GF1_GB_VERSION_NUMBER);
	snd_gf1_write8(gus, SNDRV_GF1_GB_VERSION_NUMBER, ~rev1);
	rev2 = snd_gf1_look8(gus, SNDRV_GF1_GB_VERSION_NUMBER);
	snd_gf1_write8(gus, SNDRV_GF1_GB_VERSION_NUMBER, rev1);
	spin_unlock_irqrestore(&gus->reg_lock, flags);
	snd_printdd("[0x%lx] InterWave check - rev1=0x%x, rev2=0x%x\n", gus->gf1.port, rev1, rev2);
	if ((rev1 & 0xf0) == (rev2 & 0xf0) &&
	    (rev1 & 0x0f) != (rev2 & 0x0f)) {
		snd_printdd("[0x%lx] InterWave check - passed\n", gus->gf1.port);
		gus->interwave = 1;
		strcpy(gus->card->shortname, "AMD InterWave");
		gus->revision = rev1 >> 4;
#ifndef SNDRV_STB
		return 0;	/* ok.. We have an InterWave board */
#else
		return snd_interwave_detect_stb(iwcard, gus, dev, rbus);
#endif
	}
	snd_printdd("[0x%lx] InterWave check - failed\n", gus->gf1.port);
	return -ENODEV;
}

static irqreturn_t snd_interwave_interrupt(int irq, void *dev_id, struct pt_regs *regs)
{
	struct snd_interwave *iwcard = (struct snd_interwave *) dev_id;
	int loop, max = 5;
	int handled = 0;

	do {
		loop = 0;
		if (inb(iwcard->gus_status_reg)) {
			handled = 1;
			snd_gus_interrupt(irq, iwcard->gus, regs);
			loop++;
		}
		if (inb(iwcard->pcm_status_reg) & 0x01) {	/* IRQ bit is set? */
			handled = 1;
			snd_cs4231_interrupt(irq, iwcard->cs4231, regs);
			loop++;
		}
	} while (loop && --max > 0);
	return IRQ_RETVAL(handled);
}

static void __devinit snd_interwave_reset(snd_gus_card_t * gus)
{
	snd_gf1_write8(gus, SNDRV_GF1_GB_RESET, 0x00);
	udelay(160);
	snd_gf1_write8(gus, SNDRV_GF1_GB_RESET, 0x01);
	udelay(160);
}

static void __devinit snd_interwave_bank_sizes(snd_gus_card_t * gus, int *sizes)
{
	unsigned int idx;
	unsigned int local;
	unsigned char d;

	for (idx = 0; idx < 4; idx++) {
		sizes[idx] = 0;
		d = 0x55;
		for (local = idx << 22;
		     local < (idx << 22) + 0x400000;
		     local += 0x40000, d++) {
			snd_gf1_poke(gus, local, d);
			snd_gf1_poke(gus, local + 1, d + 1);
#if 0
			printk("d = 0x%x, local = 0x%x, local + 1 = 0x%x, idx << 22 = 0x%x\n",
			       d,
			       snd_gf1_peek(gus, local),
			       snd_gf1_peek(gus, local + 1),
			       snd_gf1_peek(gus, idx << 22));
#endif
			if (snd_gf1_peek(gus, local) != d ||
			    snd_gf1_peek(gus, local + 1) != d + 1 ||
			    snd_gf1_peek(gus, idx << 22) != 0x55)
				break;
			sizes[idx]++;
		}
	}
#if 0
	printk("sizes: %i %i %i %i\n", sizes[0], sizes[1], sizes[2], sizes[3]);
#endif
}

struct rom_hdr {
	/* 000 */ unsigned char iwave[8];
	/* 008 */ unsigned char rom_hdr_revision;
	/* 009 */ unsigned char series_number;
	/* 010 */ unsigned char series_name[16];
	/* 026 */ unsigned char date[10];
	/* 036 */ unsigned short vendor_revision_major;
	/* 038 */ unsigned short vendor_revision_minor;
	/* 040 */ unsigned int rom_size;
	/* 044 */ unsigned char copyright[128];
	/* 172 */ unsigned char vendor_name[64];
	/* 236 */ unsigned char rom_description[128];
	/* 364 */ unsigned char pad[147];
	/* 511 */ unsigned char csum;
};

static void __devinit snd_interwave_detect_memory(snd_gus_card_t * gus)
{
	static unsigned int lmc[13] =
	{
		0x00000001, 0x00000101, 0x01010101, 0x00000401,
		0x04040401, 0x00040101, 0x04040101, 0x00000004,
		0x00000404, 0x04040404, 0x00000010, 0x00001010,
		0x10101010
	};

	int bank_pos, pages;
	unsigned int i, lmct;
	int psizes[4];
	unsigned char iwave[8];
	unsigned char csum;

	snd_interwave_reset(gus);
	snd_gf1_write8(gus, SNDRV_GF1_GB_GLOBAL_MODE, snd_gf1_read8(gus, SNDRV_GF1_GB_GLOBAL_MODE) | 0x01);		/* enhanced mode */
	snd_gf1_write8(gus, SNDRV_GF1_GB_MEMORY_CONTROL, 0x01);	/* DRAM I/O cycles selected */
	snd_gf1_write16(gus, SNDRV_GF1_GW_MEMORY_CONFIG, (snd_gf1_look16(gus, SNDRV_GF1_GW_MEMORY_CONFIG) & 0xff10) | 0x004c);
	/* ok.. simple test of memory size */
	pages = 0;
	snd_gf1_poke(gus, 0, 0x55);
	snd_gf1_poke(gus, 1, 0xaa);
#if 1
	if (snd_gf1_peek(gus, 0) == 0x55 && snd_gf1_peek(gus, 1) == 0xaa)
#else
	if (0)			/* ok.. for testing of 0k RAM */
#endif
	{
		snd_interwave_bank_sizes(gus, psizes);
		lmct = (psizes[3] << 24) | (psizes[2] << 16) |
		    (psizes[1] << 8) | psizes[0];
#if 0
		printk("lmct = 0x%08x\n", lmct);
#endif
		for (i = 0; i < ARRAY_SIZE(lmc); i++)
			if (lmct == lmc[i]) {
#if 0
				printk("found !!! %i\n", i);
#endif
				snd_gf1_write16(gus, SNDRV_GF1_GW_MEMORY_CONFIG, (snd_gf1_look16(gus, SNDRV_GF1_GW_MEMORY_CONFIG) & 0xfff0) | i);
				snd_interwave_bank_sizes(gus, psizes);
				break;
			}
		if (i >= ARRAY_SIZE(lmc) && !gus->gf1.enh_mode)
			 snd_gf1_write16(gus, SNDRV_GF1_GW_MEMORY_CONFIG, (snd_gf1_look16(gus, SNDRV_GF1_GW_MEMORY_CONFIG) & 0xfff0) | 2);
		for (i = 0; i < 4; i++) {
			gus->gf1.mem_alloc.banks_8[i].address =
			    gus->gf1.mem_alloc.banks_16[i].address = i << 22;
			gus->gf1.mem_alloc.banks_8[i].size =
			    gus->gf1.mem_alloc.banks_16[i].size = psizes[i] << 18;
			pages += psizes[i];
		}
	}
	pages <<= 18;
	gus->gf1.memory = pages;

	snd_gf1_write8(gus, SNDRV_GF1_GB_MEMORY_CONTROL, 0x03);	/* select ROM */
	snd_gf1_write16(gus, SNDRV_GF1_GW_MEMORY_CONFIG, (snd_gf1_look16(gus, SNDRV_GF1_GW_MEMORY_CONFIG) & 0xff1f) | (4 << 5));
	gus->gf1.rom_banks = 0;
	gus->gf1.rom_memory = 0;
	for (bank_pos = 0; bank_pos < 16L * 1024L * 1024L; bank_pos += 4L * 1024L * 1024L) {
		for (i = 0; i < 8; ++i)
			iwave[i] = snd_gf1_peek(gus, bank_pos + i);
#ifdef CONFIG_SND_DEBUG_ROM
		printk("ROM at 0x%06x = %02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x\n", bank_pos,
		       iwave[0], iwave[1], iwave[2], iwave[3],
		       iwave[4], iwave[5], iwave[6], iwave[7]);
#endif
		if (strncmp(iwave, "INTRWAVE", 8))
			continue;	/* first check */
		csum = 0;
		for (i = 0; i < sizeof(struct rom_hdr); i++)
			csum += snd_gf1_peek(gus, bank_pos + i);
#ifdef CONFIG_SND_DEBUG_ROM
		printk("ROM checksum = 0x%x (computed)\n", csum);
#endif
		if (csum != 0)
			continue;	/* not valid rom */
		gus->gf1.rom_banks++;
		gus->gf1.rom_present |= 1 << (bank_pos >> 22);
		gus->gf1.rom_memory = snd_gf1_peek(gus, bank_pos + 40) |
				     (snd_gf1_peek(gus, bank_pos + 41) << 8) |
				     (snd_gf1_peek(gus, bank_pos + 42) << 16) |
				     (snd_gf1_peek(gus, bank_pos + 43) << 24);
	}
#if 0
	if (gus->gf1.rom_memory > 0) {
		if (gus->gf1.rom_banks == 1 && gus->gf1.rom_present == 8)
			gus->card->type = SNDRV_CARD_TYPE_IW_DYNASONIC;
	}
#endif
	snd_gf1_write8(gus, SNDRV_GF1_GB_MEMORY_CONTROL, 0x00);	/* select RAM */

	if (!gus->gf1.enh_mode)
		snd_interwave_reset(gus);
}

static void __devinit snd_interwave_init(int dev, snd_gus_card_t * gus)
{
	unsigned long flags;

	/* ok.. some InterWave specific initialization */
	spin_lock_irqsave(&gus->reg_lock, flags);
	snd_gf1_write8(gus, SNDRV_GF1_GB_SOUND_BLASTER_CONTROL, 0x00);
	snd_gf1_write8(gus, SNDRV_GF1_GB_COMPATIBILITY, 0x1f);
	snd_gf1_write8(gus, SNDRV_GF1_GB_DECODE_CONTROL, 0x49);
	snd_gf1_write8(gus, SNDRV_GF1_GB_VERSION_NUMBER, 0x11);
	snd_gf1_write8(gus, SNDRV_GF1_GB_MPU401_CONTROL_A, 0x00);
	snd_gf1_write8(gus, SNDRV_GF1_GB_MPU401_CONTROL_B, 0x30);
	snd_gf1_write8(gus, SNDRV_GF1_GB_EMULATION_IRQ, 0x00);
	spin_unlock_irqrestore(&gus->reg_lock, flags);
	gus->equal_irq = 1;
	gus->codec_flag = 1;
	gus->interwave = 1;
	gus->max_flag = 1;
	gus->joystick_dac = joystick_dac[dev];

}

static snd_kcontrol_new_t snd_interwave_controls[] = {
CS4231_DOUBLE("Master Playback Switch", 0, CS4231_LINE_LEFT_OUTPUT, CS4231_LINE_RIGHT_OUTPUT, 7, 7, 1, 1),
CS4231_DOUBLE("Master Playback Volume", 0, CS4231_LINE_LEFT_OUTPUT, CS4231_LINE_RIGHT_OUTPUT, 0, 0, 31, 1),
CS4231_DOUBLE("Mic Playback Switch", 0, CS4231_LEFT_MIC_INPUT, CS4231_RIGHT_MIC_INPUT, 7, 7, 1, 1),
CS4231_DOUBLE("Mic Playback Volume", 0, CS4231_LEFT_MIC_INPUT, CS4231_RIGHT_MIC_INPUT, 0, 0, 31, 1)
};

static int __devinit snd_interwave_mixer(cs4231_t *chip)
{
	snd_card_t *card = chip->card;
	snd_ctl_elem_id_t id1, id2;
	unsigned int idx;
	int err;

	memset(&id1, 0, sizeof(id1));
	memset(&id2, 0, sizeof(id2));
	id1.iface = id2.iface = SNDRV_CTL_ELEM_IFACE_MIXER;
#if 0
	/* remove mono microphone controls */
	strcpy(id1.name, "Mic Playback Switch");
	if ((err = snd_ctl_remove_id(card, &id1)) < 0)
		return err;
	strcpy(id1.name, "Mic Playback Volume");
	if ((err = snd_ctl_remove_id(card, &id1)) < 0)
		return err;
#endif
	/* add new master and mic controls */
	for (idx = 0; idx < ARRAY_SIZE(snd_interwave_controls); idx++)
		if ((err = snd_ctl_add(card, snd_ctl_new1(&snd_interwave_controls[idx], chip))) < 0)
			return err;
	snd_cs4231_out(chip, CS4231_LINE_LEFT_OUTPUT, 0x9f);
	snd_cs4231_out(chip, CS4231_LINE_RIGHT_OUTPUT, 0x9f);
	snd_cs4231_out(chip, CS4231_LEFT_MIC_INPUT, 0x9f);
	snd_cs4231_out(chip, CS4231_RIGHT_MIC_INPUT, 0x9f);
	/* reassign AUXA to SYNTHESIZER */
	strcpy(id1.name, "Aux Playback Switch");
	strcpy(id2.name, "Synth Playback Switch");
	if ((err = snd_ctl_rename_id(card, &id1, &id2)) < 0)
		return err;
	strcpy(id1.name, "Aux Playback Volume");
	strcpy(id2.name, "Synth Playback Volume");
	if ((err = snd_ctl_rename_id(card, &id1, &id2)) < 0)
		return err;
	/* reassign AUXB to CD */
	strcpy(id1.name, "Aux Playback Switch"); id1.index = 1;
	strcpy(id2.name, "CD Playback Switch");
	if ((err = snd_ctl_rename_id(card, &id1, &id2)) < 0)
		return err;
	strcpy(id1.name, "Aux Playback Volume");
	strcpy(id2.name, "CD Playback Volume");
	if ((err = snd_ctl_rename_id(card, &id1, &id2)) < 0)
		return err;
	return 0;
}

#ifdef CONFIG_PNP

static int __devinit snd_interwave_pnp(int dev, struct snd_interwave *iwcard,
				       struct pnp_card_link *card,
				       const struct pnp_card_device_id *id)
{
	struct pnp_dev *pdev;
	struct pnp_resource_table * cfg = kmalloc(sizeof(struct pnp_resource_table), GFP_KERNEL);
	int err;

	iwcard->dev = pnp_request_card_device(card, id->devs[0].id, NULL);
	if (iwcard->dev == NULL) {
		kfree(cfg);
		return -EBUSY;
	}
#ifdef SNDRV_STB
	iwcard->devtc = pnp_request_card_device(card, id->devs[1].id, NULL);
	if (iwcard->devtc == NULL) {
		kfree(cfg);
		return -EBUSY;
	}
#endif
	/* Synth & Codec initialization */
	pdev = iwcard->dev;
	pnp_init_resource_table(cfg);
	if (port[dev] != SNDRV_AUTO_PORT) {
		pnp_resource_change(&cfg->port_resource[0], port[dev], 16);
		pnp_resource_change(&cfg->port_resource[1], port[dev] + 0x100, 12);
		pnp_resource_change(&cfg->port_resource[2], port[dev] + 0x10c, 4);
	}
	if (dma1[dev] != SNDRV_AUTO_DMA)
		pnp_resource_change(&cfg->dma_resource[0], dma1[dev], 1);
	if (dma2[dev] != SNDRV_AUTO_DMA)
		pnp_resource_change(&cfg->dma_resource[1], dma2[dev], 1);
	if (dma2[dev] < 0)
		pnp_resource_change(&cfg->dma_resource[1], 4, 1);
	if (irq[dev] != SNDRV_AUTO_IRQ)
		pnp_resource_change(&cfg->irq_resource[0], irq[dev], 1);
        if (pnp_manual_config_dev(pdev, cfg, 0) < 0)
		snd_printk(KERN_ERR "InterWave - Synth - the requested resources are invalid, using auto config\n");
	err = pnp_activate_dev(pdev);
	if (err < 0) {
		kfree(cfg);
		snd_printk(KERN_ERR "InterWave PnP configure failure (out of resources?)\n");
		return err;
	}
	if (pnp_port_start(pdev, 0) + 0x100 != pnp_port_start(pdev, 1) ||
	    pnp_port_start(pdev, 0) + 0x10c != pnp_port_start(pdev, 2)) {
		kfree(cfg);
		snd_printk(KERN_ERR "PnP configure failure (wrong ports)\n");
		return -ENOENT;
	}
	port[dev] = pnp_port_start(pdev, 0);
	dma1[dev] = pnp_dma(pdev, 0);
	if (dma2[dev] >= 0)
		dma2[dev] = pnp_dma(pdev, 1);
	irq[dev] = pnp_irq(pdev, 0);
	snd_printdd("isapnp IW: sb port=0x%lx, gf1 port=0x%lx, codec port=0x%lx\n",
				pnp_port_start(pdev, 0),
				pnp_port_start(pdev, 1),
				pnp_port_start(pdev, 2));
	snd_printdd("isapnp IW: dma1=%i, dma2=%i, irq=%i\n", dma1[dev], dma2[dev], irq[dev]);
#ifdef SNDRV_STB
	/* Tone Control initialization */
	pdev = iwcard->devtc;
	pnp_init_resource_table(cfg);
	if (port_tc[dev] != SNDRV_AUTO_PORT)
		pnp_resource_change(&cfg->port_resource[0], port_tc[dev], 1);
        if (pnp_manual_config_dev(pdev, cfg, 0) < 0)
		snd_printk(KERN_ERR "InterWave - ToneControl - the requested resources are invalid, using auto config\n");
	err = pnp_activate_dev(pdev);
	if (err < 0) {
		kfree(cfg);
		snd_printk(KERN_ERR "InterWave ToneControl PnP configure failure (out of resources?)\n");
		return err;
	}
	port_tc[dev] = pnp_port_start(pdev, 0);
	snd_printdd("isapnp IW: tone control port=0x%lx\n", port_tc[dev]);
#endif
	kfree(cfg);
	return 0;
}
#endif /* CONFIG_PNP */

static void snd_interwave_free(snd_card_t *card)
{
	struct snd_interwave *iwcard = (struct snd_interwave *)card->private_data;

	if (iwcard == NULL)
		return;
#ifdef SNDRV_STB
	if (iwcard->i2c_res) {
		release_resource(iwcard->i2c_res);
		kfree_nocheck(iwcard->i2c_res);
	}
#endif
	if (iwcard->irq >= 0)
		free_irq(iwcard->irq, (void *)iwcard);
}

static int __devinit snd_interwave_probe(int dev, struct pnp_card_link *pcard,
				         const struct pnp_card_device_id *pid)
{
	static int possible_irqs[] = {5, 11, 12, 9, 7, 15, 3, -1};
	static int possible_dmas[] = {0, 1, 3, 5, 6, 7, -1};
	int xirq, xdma1, xdma2;
	snd_card_t *card;
	struct snd_interwave *iwcard;
	cs4231_t *cs4231;
	snd_gus_card_t *gus;
#ifdef SNDRV_STB
	snd_i2c_bus_t *i2c_bus;
#endif
	snd_pcm_t *pcm;
	char *str;
	int err;

	card = snd_card_new(index[dev], id[dev], THIS_MODULE,
			    sizeof(struct snd_interwave));
	if (card == NULL)
		return -ENOMEM;
	iwcard = (struct snd_interwave *)card->private_data;
	iwcard->card = card;
	iwcard->irq = -1;
	card->private_free = snd_interwave_free;
#ifdef CONFIG_PNP
	if (isapnp[dev]) {
		if ((err = snd_interwave_pnp(dev, iwcard, pcard, pid)) < 0)
			goto _err;
		snd_card_set_dev(card, &pcard->card->dev);
	}
#endif
	xirq = irq[dev];
	if (xirq == SNDRV_AUTO_IRQ) {
		if ((xirq = snd_legacy_find_free_irq(possible_irqs)) < 0) {
			snd_printk(KERN_ERR PFX "unable to find a free IRQ\n");
			err = -EBUSY;
			goto _err;
		}
	}
	xdma1 = dma1[dev];
	if (xdma1 == SNDRV_AUTO_DMA) {
		if ((xdma1 = snd_legacy_find_free_dma(possible_dmas)) < 0) {
			snd_printk(KERN_ERR PFX "unable to find a free DMA1\n");
			err = -EBUSY;
			goto _err;
		}
	}
	xdma2 = dma2[dev];
	if (xdma2 == SNDRV_AUTO_DMA) {
		if ((xdma2 = snd_legacy_find_free_dma(possible_dmas)) < 0) {
			snd_printk(KERN_ERR PFX "unable to find a free DMA2\n");
			err = -EBUSY;
			goto _err;
		}
	}

	if ((err = snd_gus_create(card,
				  port[dev],
				  -xirq, xdma1, xdma2,
				  0, 32,
				  pcm_channels[dev], effect[dev], &gus)) < 0)
		goto _err;

	if ((err = snd_interwave_detect(iwcard, gus, dev
#ifdef SNDRV_STB
            , &i2c_bus
#endif
	    )) < 0)
		goto _err;

	iwcard->gus_status_reg = gus->gf1.reg_irqstat;
	iwcard->pcm_status_reg = gus->gf1.port + 0x10c + 2;

	snd_interwave_init(dev, gus);
	snd_interwave_detect_memory(gus);
	if ((err = snd_gus_initialize(gus)) < 0)
		goto _err;

	if (request_irq(xirq, snd_interwave_interrupt, SA_INTERRUPT, "InterWave", (void *)iwcard)) {
		snd_printk(KERN_ERR PFX "unable to grab IRQ %d\n", xirq);
		err = -EBUSY;
		goto _err;
	}
	iwcard->irq = xirq;

	if ((err = snd_cs4231_create(card,
				     gus->gf1.port + 0x10c, -1, xirq,
				     xdma2 < 0 ? xdma1 : xdma2, xdma1,
				     CS4231_HW_INTERWAVE,
				     CS4231_HWSHARE_IRQ |
				     CS4231_HWSHARE_DMA1 |
				     CS4231_HWSHARE_DMA2,
				     &cs4231)) < 0)
		goto _err;

	if ((err = snd_cs4231_pcm(cs4231, 0, &pcm)) < 0)
		goto _err;

	sprintf(pcm->name + strlen(pcm->name), " rev %c", gus->revision + 'A');
	strcat(pcm->name, " (codec)");

	if ((err = snd_cs4231_timer(cs4231, 2, NULL)) < 0)
		goto _err;

	if ((err = snd_cs4231_mixer(cs4231)) < 0)
		goto _err;

	if (pcm_channels[dev] > 0) {
		if ((err = snd_gf1_pcm_new(gus, 1, 1, NULL)) < 0)
			goto _err;
	}
	if ((err = snd_interwave_mixer(cs4231)) < 0)
		goto _err;

#ifdef SNDRV_STB
	{
		snd_ctl_elem_id_t id1, id2;
		memset(&id1, 0, sizeof(id1));
		memset(&id2, 0, sizeof(id2));
		id1.iface = id2.iface = SNDRV_CTL_ELEM_IFACE_MIXER;
		strcpy(id1.name, "Master Playback Switch");
		strcpy(id2.name, id1.name);
		id2.index = 1;
		if ((err = snd_ctl_rename_id(card, &id1, &id2)) < 0)
			goto _err;
		strcpy(id1.name, "Master Playback Volume");
		strcpy(id2.name, id1.name);
		if ((err = snd_ctl_rename_id(card, &id1, &id2)) < 0)
			goto _err;
		if ((err = snd_tea6330t_update_mixer(card, i2c_bus, 0, 1)) < 0)
			goto _err;
	}
#endif

	gus->uart_enable = midi[dev];
	if ((err = snd_gf1_rawmidi_new(gus, 0, NULL)) < 0)
		goto _err;

#ifndef SNDRV_STB
	str = "AMD InterWave";
	if (gus->gf1.rom_banks == 1 && gus->gf1.rom_present == 8)
		str = "Dynasonic 3-D";
#else
	str = "InterWave STB";
#endif
	strcpy(card->driver, str);
	strcpy(card->shortname, str);
	sprintf(card->longname, "%s at 0x%lx, irq %i, dma %d",
		str,
		gus->gf1.port,
		xirq,
		xdma1);
	if (xdma2 >= 0)
		sprintf(card->longname + strlen(card->longname), "&%d", xdma2);

	if ((err = snd_card_set_generic_dev(card)) < 0)
		goto _err;

	if ((err = snd_card_register(card)) < 0)
		goto _err;
	
	iwcard->cs4231 = cs4231;
	iwcard->gus = gus;
	if (pcard)
		pnp_set_card_drvdata(pcard, card);
	else
		snd_interwave_legacy[dev++] = card;
	return 0;

 _err:
	snd_card_free(card);
	return err;
}

static int __devinit snd_interwave_probe_legacy_port(unsigned long xport)
{
	static int dev;
	int res;

	for ( ; dev < SNDRV_CARDS; dev++) {
		if (!enable[dev] || port[dev] != SNDRV_AUTO_PORT)
                        continue;
#ifdef CONFIG_PNP
		if (isapnp[dev])
			continue;
#endif
		port[dev] = xport;
		res = snd_interwave_probe(dev, NULL, NULL);
		if (res < 0)
			port[dev] = SNDRV_AUTO_PORT;
		return res;
	}
	return -ENODEV;
}

#ifdef CONFIG_PNP

static int __devinit snd_interwave_pnp_detect(struct pnp_card_link *card,
					   const struct pnp_card_device_id *id)
{
	static int dev;
	int res;

	for ( ; dev < SNDRV_CARDS; dev++) {
		if (!enable[dev] || !isapnp[dev])
			continue;
		res = snd_interwave_probe(dev, card, id);
		if (res < 0)
			return res;
		dev++;
		return 0;
        }

        return -ENODEV;
}

static void __devexit snd_interwave_pnp_remove(struct pnp_card_link * pcard)
{
	snd_card_t *card = (snd_card_t *) pnp_get_card_drvdata(pcard);

	snd_card_disconnect(card);
	snd_card_free_in_thread(card);
}

static struct pnp_card_driver interwave_pnpc_driver = {
	.flags = PNP_DRIVER_RES_DISABLE,
	.name = "interwave",
	.id_table = snd_interwave_pnpids,
	.probe = snd_interwave_pnp_detect,
	.remove = __devexit_p(snd_interwave_pnp_remove),
};

#endif /* CONFIG_PNP */

static int __init alsa_card_interwave_init(void)
{
	int cards = 0, i;
	static long possible_ports[] = {0x210, 0x220, 0x230, 0x240, 0x250, 0x260, -1};
	int dev;

	for (dev = 0; dev < SNDRV_CARDS; dev++) {
		if (!enable[dev] || port[dev] == SNDRV_AUTO_PORT)
			continue;
#ifdef CONFIG_PNP
		if (isapnp[dev])
			continue;
#endif
		if (!snd_interwave_probe(dev, NULL, NULL)) {
			cards++;
			continue;
		}
#ifdef MODULE
		printk(KERN_ERR "InterWave soundcard #%i not found at 0x%lx or device busy\n", dev, port[dev]);
#endif
	}
	/* legacy auto configured cards */
	i = snd_legacy_auto_probe(possible_ports, snd_interwave_probe_legacy_port);
	if (i > 0)
		cards += i;
#ifdef CONFIG_PNP
	/* ISA PnP cards */
	i = pnp_register_card_driver(&interwave_pnpc_driver);
	if (i > 0)
		cards += i;
#endif

	if (!cards) {
#ifdef CONFIG_PNP
		pnp_unregister_card_driver(&interwave_pnpc_driver);
#endif
#ifdef MODULE
		printk(KERN_ERR "InterWave soundcard not found or device busy\n");
#endif
		return -ENODEV;
	}
	return 0;
}

static void __exit alsa_card_interwave_exit(void)
{
	int dev;

#ifdef CONFIG_PNP
	/* PnP cards first */
	pnp_unregister_card_driver(&interwave_pnpc_driver);
#endif
	for (dev = 0; dev < SNDRV_CARDS; dev++)
		snd_card_free(snd_interwave_legacy[dev]);
}

module_init(alsa_card_interwave_init)
module_exit(alsa_card_interwave_exit)
