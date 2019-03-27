/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 1999 Cameron Grant <cg@freebsd.org>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#ifdef HAVE_KERNEL_OPTION_HEADERS
#include "opt_snd.h"
#endif

#include <dev/sound/pcm/sound.h>
#include <dev/sound/pcm/ac97.h>
#include <dev/sound/pci/aureal.h>

#include <dev/pci/pcireg.h>
#include <dev/pci/pcivar.h>

SND_DECLARE_FILE("$FreeBSD$");

/* PCI IDs of supported chips */
#define AU8820_PCI_ID 0x000112eb

/* channel interface */
static u_int32_t au_playfmt[] = {
	SND_FORMAT(AFMT_U8, 1, 0),
	SND_FORMAT(AFMT_U8, 2, 0),
	SND_FORMAT(AFMT_S16_LE, 1, 0),
	SND_FORMAT(AFMT_S16_LE, 2, 0),
	0
};
static struct pcmchan_caps au_playcaps = {4000, 48000, au_playfmt, 0};

static u_int32_t au_recfmt[] = {
	SND_FORMAT(AFMT_U8, 1, 0),
	SND_FORMAT(AFMT_U8, 2, 0),
	SND_FORMAT(AFMT_S16_LE, 1, 0),
	SND_FORMAT(AFMT_S16_LE, 2, 0),
	0
};
static struct pcmchan_caps au_reccaps = {4000, 48000, au_recfmt, 0};

/* -------------------------------------------------------------------- */

struct au_info;

struct au_chinfo {
	struct au_info *parent;
	struct pcm_channel *channel;
	struct snd_dbuf *buffer;
	int dir;
};

struct au_info {
	int unit;

	bus_space_tag_t st[3];
	bus_space_handle_t sh[3];

	bus_dma_tag_t	parent_dmat;
	struct mtx *lock;

	u_int32_t	x[32], y[128];
	char		z[128];
	u_int32_t	routes[4], interrupts;
	struct au_chinfo pch;
};

static int      au_init(device_t dev, struct au_info *au);
static void     au_intr(void *);

/* -------------------------------------------------------------------- */

static u_int32_t
au_rd(struct au_info *au, int mapno, int regno, int size)
{
	switch(size) {
	case 1:
		return bus_space_read_1(au->st[mapno], au->sh[mapno], regno);
	case 2:
		return bus_space_read_2(au->st[mapno], au->sh[mapno], regno);
	case 4:
		return bus_space_read_4(au->st[mapno], au->sh[mapno], regno);
	default:
		return 0xffffffff;
	}
}

static void
au_wr(struct au_info *au, int mapno, int regno, u_int32_t data, int size)
{
	switch(size) {
	case 1:
		bus_space_write_1(au->st[mapno], au->sh[mapno], regno, data);
		break;
	case 2:
		bus_space_write_2(au->st[mapno], au->sh[mapno], regno, data);
		break;
	case 4:
		bus_space_write_4(au->st[mapno], au->sh[mapno], regno, data);
		break;
	}
}

/* -------------------------------------------------------------------- */

static int
au_rdcd(kobj_t obj, void *arg, int regno)
{
	struct au_info *au = (struct au_info *)arg;
	int i=0, j=0;

	regno<<=16;
	au_wr(au, 0, AU_REG_CODECIO, regno, 4);
	while (j<50) {
		i=au_rd(au, 0, AU_REG_CODECIO, 4);
		if ((i & 0x00ff0000) == (regno | 0x00800000)) break;
		DELAY(j * 200 + 2000);
		j++;
	}
	if (j==50) printf("pcm%d: codec timeout reading register %x (%x)\n",
		au->unit, (regno & AU_CDC_REGMASK)>>16, i);
	return i & AU_CDC_DATAMASK;
}

static int
au_wrcd(kobj_t obj, void *arg, int regno, u_int32_t data)
{
	struct au_info *au = (struct au_info *)arg;
	int i, j, tries;
	i=j=tries=0;
	do {
		while (j<50 && (i & AU_CDC_WROK) == 0) {
			i=au_rd(au, 0, AU_REG_CODECST, 4);
			DELAY(2000);
			j++;
		}
		if (j==50) printf("codec timeout during write of register %x, data %x\n",
				  regno, data);
		au_wr(au, 0, AU_REG_CODECIO, (regno<<16) | AU_CDC_REGSET | data, 4);
/*		DELAY(20000);
		i=au_rdcd(au, regno);
*/		tries++;
	} while (0); /* (i != data && tries < 3); */
	/*
	if (tries == 3) printf("giving up writing 0x%4x to codec reg %2x\n", data, regno);
	*/

	return 0;
}

static kobj_method_t au_ac97_methods[] = {
    	KOBJMETHOD(ac97_read,		au_rdcd),
    	KOBJMETHOD(ac97_write,		au_wrcd),
	KOBJMETHOD_END
};
AC97_DECLARE(au_ac97);

/* -------------------------------------------------------------------- */

static void
au_setbit(u_int32_t *p, char bit, u_int32_t value)
{
	p += bit >> 5;
	bit &= 0x1f;
	*p &= ~ (1 << bit);
	*p |= (value << bit);
}

static void
au_addroute(struct au_info *au, int a, int b, int route)
{
	int j = 0x1099c+(a<<2);
	if (au->x[a] != a+0x67) j = AU_REG_RTBASE+(au->x[a]<<2);

	au_wr(au, 0, AU_REG_RTBASE+(route<<2), 0xffffffff, 4);
 	au_wr(au, 0, j, route | (b<<7), 4);
	au->y[route]=au->x[a];
	au->x[a]=route;
	au->z[route]=a & 0x000000ff;
	au_setbit(au->routes, route, 1);
}

static void
au_delroute(struct au_info *au, int route)
{
	int i;
	int j=au->z[route];

	au_setbit(au->routes, route, 0);
	au->z[route]=0x1f;
	i=au_rd(au, 0, AU_REG_RTBASE+(route<<2), 4);
	au_wr(au, 0, AU_REG_RTBASE+(au->y[route]<<2), i, 4);
	au->y[i & 0x7f]=au->y[route];
	au_wr(au, 0, AU_REG_RTBASE+(route<<2), 0xfffffffe, 4);
	if (au->x[j] == route) au->x[j]=au->y[route];
	au->y[route]=0x7f;
}

static void
au_encodec(struct au_info *au, char channel)
{
	au_wr(au, 0, AU_REG_CODECEN,
	      au_rd(au, 0, AU_REG_CODECEN, 4) | (1 << (channel + 8)), 4);
}

static void
au_clrfifo(struct au_info *au, u_int32_t c)
{
	u_int32_t i;

	for (i=0; i<32; i++) au_wr(au, 0, AU_REG_FIFOBASE+(c<<7)+(i<<2), 0, 4);
}

static void
au_setadb(struct au_info *au, u_int32_t c, u_int32_t enable)
{
	int x;

	x = au_rd(au, 0, AU_REG_ADB, 4);
	x &= ~(1 << c);
	x |= (enable << c);
	au_wr(au, 0, AU_REG_ADB, x, 4);
}

static void
au_prepareoutput(struct au_chinfo *ch, u_int32_t format)
{
	struct au_info *au = ch->parent;
	int i, stereo = (AFMT_CHANNEL(format) > 1)? 1 : 0;
	u_int32_t baseaddr = sndbuf_getbufaddr(ch->buffer);

	au_wr(au, 0, 0x1061c, 0, 4);
	au_wr(au, 0, 0x10620, 0, 4);
	au_wr(au, 0, 0x10624, 0, 4);
	switch(AFMT_ENCODING(format)) {
		case 1:
			i=0xb000;
			break;
		case 2:
			i=0xf000;
			break;
 		case 8:
			i=0x7000;
			break;
		case 16:
			i=0x23000;
			break;
		default:
			i=0x3000;
	}
	au_wr(au, 0, 0x10200, baseaddr, 4);
	au_wr(au, 0, 0x10204, baseaddr+0x1000, 4);
	au_wr(au, 0, 0x10208, baseaddr+0x2000, 4);
	au_wr(au, 0, 0x1020c, baseaddr+0x3000, 4);

	au_wr(au, 0, 0x10400, 0xdeffffff, 4);
	au_wr(au, 0, 0x10404, 0xfcffffff, 4);

	au_wr(au, 0, 0x10580, i, 4);

	au_wr(au, 0, 0x10210, baseaddr, 4);
	au_wr(au, 0, 0x10214, baseaddr+0x1000, 4);
	au_wr(au, 0, 0x10218, baseaddr+0x2000, 4);
	au_wr(au, 0, 0x1021c, baseaddr+0x3000, 4);

	au_wr(au, 0, 0x10408, 0x00fff000 | 0x56000000 | 0x00000fff, 4);
	au_wr(au, 0, 0x1040c, 0x00fff000 | 0x74000000 | 0x00000fff, 4);

	au_wr(au, 0, 0x10584, i, 4);

	au_wr(au, 0, 0x0f800, stereo? 0x00030032 : 0x00030030, 4);
	au_wr(au, 0, 0x0f804, stereo? 0x00030032 : 0x00030030, 4);

	au_addroute(au, 0x11, 0, 0x58);
	au_addroute(au, 0x11, stereo? 0 : 1, 0x59);
}

/* -------------------------------------------------------------------- */
/* channel interface */
static void *
auchan_init(kobj_t obj, void *devinfo, struct snd_dbuf *b, struct pcm_channel *c, int dir)
{
	struct au_info *au = devinfo;
	struct au_chinfo *ch = (dir == PCMDIR_PLAY)? &au->pch : NULL;

	ch->parent = au;
	ch->channel = c;
	ch->buffer = b;
	ch->dir = dir;
	if (sndbuf_alloc(ch->buffer, au->parent_dmat, 0, AU_BUFFSIZE) != 0)
		return NULL;
	return ch;
}

static int
auchan_setformat(kobj_t obj, void *data, u_int32_t format)
{
	struct au_chinfo *ch = data;

	if (ch->dir == PCMDIR_PLAY) au_prepareoutput(ch, format);
	return 0;
}

static int
auchan_setspeed(kobj_t obj, void *data, u_int32_t speed)
{
	struct au_chinfo *ch = data;
	if (ch->dir == PCMDIR_PLAY) {
	} else {
	}
	return speed;
}

static int
auchan_setblocksize(kobj_t obj, void *data, u_int32_t blocksize)
{
	return blocksize;
}

static int
auchan_trigger(kobj_t obj, void *data, int go)
{
	struct au_chinfo *ch = data;
	struct au_info *au = ch->parent;

	if (!PCMTRIG_COMMON(go))
		return 0;

	if (ch->dir == PCMDIR_PLAY) {
		au_setadb(au, 0x11, (go)? 1 : 0);
		if (go != PCMTRIG_START) {
			au_wr(au, 0, 0xf800, 0, 4);
			au_wr(au, 0, 0xf804, 0, 4);
			au_delroute(au, 0x58);
			au_delroute(au, 0x59);
		}
	} else {
	}
	return 0;
}

static int
auchan_getptr(kobj_t obj, void *data)
{
	struct au_chinfo *ch = data;
	struct au_info *au = ch->parent;
	if (ch->dir == PCMDIR_PLAY) {
		return au_rd(au, 0, AU_REG_UNK2, 4) & (AU_BUFFSIZE-1);
	} else {
		return 0;
	}
}

static struct pcmchan_caps *
auchan_getcaps(kobj_t obj, void *data)
{
	struct au_chinfo *ch = data;
	return (ch->dir == PCMDIR_PLAY)? &au_playcaps : &au_reccaps;
}

static kobj_method_t auchan_methods[] = {
    	KOBJMETHOD(channel_init,		auchan_init),
    	KOBJMETHOD(channel_setformat,		auchan_setformat),
    	KOBJMETHOD(channel_setspeed,		auchan_setspeed),
    	KOBJMETHOD(channel_setblocksize,	auchan_setblocksize),
    	KOBJMETHOD(channel_trigger,		auchan_trigger),
    	KOBJMETHOD(channel_getptr,		auchan_getptr),
    	KOBJMETHOD(channel_getcaps,		auchan_getcaps),
	KOBJMETHOD_END
};
CHANNEL_DECLARE(auchan);

/* -------------------------------------------------------------------- */
/* The interrupt handler */
static void
au_intr (void *p)
{
	struct au_info *au = p;
	u_int32_t	intsrc, i;

	au->interrupts++;
	intsrc=au_rd(au, 0, AU_REG_IRQSRC, 4);
	printf("pcm%d: interrupt with src %x\n", au->unit, intsrc);
	if (intsrc & AU_IRQ_FATAL) printf("pcm%d: fatal error irq\n", au->unit);
	if (intsrc & AU_IRQ_PARITY) printf("pcm%d: parity error irq\n", au->unit);
	if (intsrc & AU_IRQ_UNKNOWN) {
		(void)au_rd(au, 0, AU_REG_UNK1, 4);
		au_wr(au, 0, AU_REG_UNK1, 0, 4);
		au_wr(au, 0, AU_REG_UNK1, 0x10000, 4);
	}
	if (intsrc & AU_IRQ_PCMOUT) {
	       	i=au_rd(au, 0, AU_REG_UNK2, 4) & (AU_BUFFSIZE-1);
	       	chn_intr(au->pch.channel);
		(void)au_rd(au, 0, AU_REG_UNK3, 4);
		(void)au_rd(au, 0, AU_REG_UNK4, 4);
		(void)au_rd(au, 0, AU_REG_UNK5, 4);
	}
/* don't support midi
	if (intsrc & AU_IRQ_MIDI) {
		i=au_rd(au, 0, 0x11004, 4);
		j=10;
		while (i & 0xff) {
			if (j-- <= 0) break;
			i=au_rd(au, 0, 0x11000, 4);
			if ((au->midi_stat & 1) && (au->midi_out))
				au->midi_out(au->midi_devno, i);
			i=au_rd(au, 0, 0x11004);
		}
	}
*/
	au_wr(au, 0, AU_REG_IRQSRC, intsrc & 0x7ff, 4);
	au_rd(au, 0, AU_REG_IRQSRC, 4);
}


/* -------------------------------------------------------------------- */

/* Probe and attach the card */

static int
au_init(device_t dev, struct au_info *au)
{
	u_int32_t	i, j;

	au_wr(au, 0, AU_REG_IRQGLOB, 0xffffffff, 4);
	DELAY(100000);

	/* init codec */
	/* cold reset */
	for (i=0; i<32; i++) {
		au_wr(au, 0, AU_REG_CODECCHN+(i<<2), 0, 4);
		DELAY(10000);
	}
	if (1) {
		au_wr(au, 0, AU_REG_CODECST, 0x8068, 4);
		DELAY(10000);
		au_wr(au, 0, AU_REG_CODECST, 0x00e8, 4);
		DELAY(10000);
	} else {
		au_wr(au, 0, AU_REG_CODECST, 0x00a8, 4);
 		DELAY(100000);
		au_wr(au, 0, AU_REG_CODECST, 0x80a8, 4);
		DELAY(100000);
		au_wr(au, 0, AU_REG_CODECST, 0x80e8, 4);
		DELAY(100000);
		au_wr(au, 0, AU_REG_CODECST, 0x80a8, 4);
		DELAY(100000);
		au_wr(au, 0, AU_REG_CODECST, 0x00a8, 4);
		DELAY(100000);
		au_wr(au, 0, AU_REG_CODECST, 0x00e8, 4);
		DELAY(100000);
	}

	/* init */
	for (i=0; i<32; i++) {
		au_wr(au, 0, AU_REG_CODECCHN+(i<<2), 0, 4);
		DELAY(10000);
	}
	au_wr(au, 0, AU_REG_CODECST, 0xe8, 4);
	DELAY(10000);
	au_wr(au, 0, AU_REG_CODECEN, 0, 4);

	/* setup codec */
	i=j=0;
	while (j<100 && (i & AU_CDC_READY)==0) {
		i=au_rd(au, 0, AU_REG_CODECST, 4);
		DELAY(1000);
		j++;
	}
	if (j==100) device_printf(dev, "codec not ready, status 0x%x\n", i);

   	/* init adb */
	/*au->x5c=0;*/
	for (i=0; i<32;  i++) au->x[i]=i+0x67;
	for (i=0; i<128; i++) au->y[i]=0x7f;
	for (i=0; i<128; i++) au->z[i]=0x1f;
	au_wr(au, 0, AU_REG_ADB, 0, 4);
	for (i=0; i<124; i++) au_wr(au, 0, AU_REG_RTBASE+(i<<2), 0xffffffff, 4);

	/* test */
	i=au_rd(au, 0, 0x107c0, 4);
 	if (i!=0xdeadbeef) device_printf(dev, "dma check failed: 0x%x\n", i);

	/* install mixer */
	au_wr(au, 0, AU_REG_IRQGLOB,
	      au_rd(au, 0, AU_REG_IRQGLOB, 4) | AU_IRQ_ENABLE, 4);
	/* braindead but it's what the oss/linux driver does
	 * for (i=0; i<0x80000000; i++) au_wr(au, 0, i<<2, 0, 4);
	 */
	au->routes[0]=au->routes[1]=au->routes[2]=au->routes[3]=0;
	/*au->x1e4=0;*/

	/* attach channel */
	au_addroute(au, 0x11, 0x48, 0x02);
	au_addroute(au, 0x11, 0x49, 0x03);
	au_encodec(au, 0);
	au_encodec(au, 1);

	for (i=0; i<48; i++) au_wr(au, 0, 0xf800+(i<<2), 0x20, 4);
	for (i=2; i<6; i++) au_wr(au, 0, 0xf800+(i<<2), 0, 4);
	au_wr(au, 0, 0xf8c0, 0x0843, 4);
	for (i=0; i<4; i++) au_clrfifo(au, i);

	return (0);
}

static int
au_testirq(struct au_info *au)
{
	au_wr(au, 0, AU_REG_UNK1, 0x80001000, 4);
	au_wr(au, 0, AU_REG_IRQEN, 0x00001030, 4);
	au_wr(au, 0, AU_REG_IRQSRC, 0x000007ff, 4);
	DELAY(1000000);
	if (au->interrupts==0) printf("pcm%d: irq test failed\n", au->unit);
	/* this apparently generates an irq */
	return 0;
}

static int
au_pci_probe(device_t dev)
{
	if (pci_get_devid(dev) == AU8820_PCI_ID) {
		device_set_desc(dev, "Aureal Vortex 8820");
		return BUS_PROBE_DEFAULT;
	}

	return ENXIO;
}

static int
au_pci_attach(device_t dev)
{
	struct au_info *au;
	int		type[10];
	int		regid[10];
	struct resource *reg[10];
	int		i, j, mapped = 0;
	int		irqid;
	struct resource *irq;
	void		*ih;
	struct ac97_info *codec;
	char 		status[SND_STATUSLEN];

	au = malloc(sizeof(*au), M_DEVBUF, M_WAITOK | M_ZERO);
	au->unit = device_get_unit(dev);

	pci_enable_busmaster(dev);

	irq = NULL;
	ih = NULL;
	j=0;
	/* XXX dfr: is this strictly necessary? */
	for (i=0; i<PCI_MAXMAPS_0; i++) {
#if 0
		/* Slapped wrist: config_id and map are private structures */
		if (bootverbose) {
			printf("pcm%d: map %d - allocating ", unit, i+1);
			printf("0x%x bytes of ", 1<<config_id->map[i].ln2size);
			printf("%s space ", (config_id->map[i].type & PCI_MAPPORT)?
					    "io" : "memory");
			printf("at 0x%x...", config_id->map[i].base);
		}
#endif
		regid[j] = PCIR_BAR(i);
		type[j] = SYS_RES_MEMORY;
		reg[j] = bus_alloc_resource_any(dev, type[j], &regid[j],
						RF_ACTIVE);
		if (!reg[j]) {
			type[j] = SYS_RES_IOPORT;
			reg[j] = bus_alloc_resource_any(dev, type[j], 
							&regid[j], RF_ACTIVE);
		}
		if (reg[j]) {
			au->st[i] = rman_get_bustag(reg[j]);
			au->sh[i] = rman_get_bushandle(reg[j]);
			mapped++;
		}
#if 0
		if (bootverbose) printf("%s\n", mapped? "ok" : "failed");
#endif
		if (mapped) j++;
		if (j == 10) {
			/* XXX */
			device_printf(dev, "too many resources");
			goto bad;
		}
	}

#if 0
	if (j < config_id->nummaps) {
		printf("pcm%d: unable to map a required resource\n", unit);
		free(au, M_DEVBUF);
		return;
	}
#endif

	au_wr(au, 0, AU_REG_IRQEN, 0, 4);

	irqid = 0;
	irq = bus_alloc_resource_any(dev, SYS_RES_IRQ, &irqid,
				     RF_ACTIVE | RF_SHAREABLE);
	if (!irq || snd_setup_intr(dev, irq, 0, au_intr, au, &ih)) {
		device_printf(dev, "unable to map interrupt\n");
		goto bad;
	}

	if (au_testirq(au)) device_printf(dev, "irq test failed\n");

	if (au_init(dev, au) == -1) {
		device_printf(dev, "unable to initialize the card\n");
		goto bad;
	}

	codec = AC97_CREATE(dev, au, au_ac97);
	if (codec == NULL) goto bad;
	if (mixer_init(dev, ac97_getmixerclass(), codec) == -1) goto bad;

	if (bus_dma_tag_create(/*parent*/bus_get_dma_tag(dev), /*alignment*/2,
		/*boundary*/0,
		/*lowaddr*/BUS_SPACE_MAXADDR_32BIT,
		/*highaddr*/BUS_SPACE_MAXADDR,
		/*filter*/NULL, /*filterarg*/NULL,
		/*maxsize*/AU_BUFFSIZE, /*nsegments*/1, /*maxsegz*/0x3ffff,
		/*flags*/0, /*lockfunc*/busdma_lock_mutex,
		/*lockarg*/&Giant, &au->parent_dmat) != 0) {
		device_printf(dev, "unable to create dma tag\n");
		goto bad;
	}

	snprintf(status, SND_STATUSLEN, "at %s 0x%jx irq %jd %s",
		 (type[0] == SYS_RES_IOPORT)? "io" : "memory",
		 rman_get_start(reg[0]), rman_get_start(irq),PCM_KLDSTRING(snd_aureal));

	if (pcm_register(dev, au, 1, 1)) goto bad;
	/* pcm_addchan(dev, PCMDIR_REC, &au_chantemplate, au); */
	pcm_addchan(dev, PCMDIR_PLAY, &auchan_class, au);
	pcm_setstatus(dev, status);

	return 0;

 bad:
	if (au) free(au, M_DEVBUF);
	for (i = 0; i < j; i++)
		bus_release_resource(dev, type[i], regid[i], reg[i]);
	if (ih) bus_teardown_intr(dev, irq, ih);
	if (irq) bus_release_resource(dev, SYS_RES_IRQ, irqid, irq);
	return ENXIO;
}

static device_method_t au_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe,		au_pci_probe),
	DEVMETHOD(device_attach,	au_pci_attach),

	{ 0, 0 }
};

static driver_t au_driver = {
	"pcm",
	au_methods,
	PCM_SOFTC_SIZE,
};

DRIVER_MODULE(snd_aureal, pci, au_driver, pcm_devclass, 0, 0);
MODULE_DEPEND(snd_aureal, sound, SOUND_MINVER, SOUND_PREFVER, SOUND_MAXVER);
MODULE_VERSION(snd_aureal, 1);
