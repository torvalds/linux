/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 1999 Seigo Tanimura
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

#include <dev/sound/chip.h>
#include <dev/sound/pcm/sound.h>
#include <dev/sound/isa/sb.h>

#include <isa/isavar.h>

SND_DECLARE_FILE("$FreeBSD$");

#define IO_MAX	3
#define IRQ_MAX	1
#define DRQ_MAX	2
#define INTR_MAX	2

struct sbc_softc;

struct sbc_ihl {
	driver_intr_t *intr[INTR_MAX];
	void *intr_arg[INTR_MAX];
	struct sbc_softc *parent;
};

/* Here is the parameter structure per a device. */
struct sbc_softc {
	device_t dev; /* device */
	device_t child_pcm, child_midi1, child_midi2;

	int io_rid[IO_MAX]; /* io port rids */
	struct resource *io[IO_MAX]; /* io port resources */
	int io_alloced[IO_MAX]; /* io port alloc flag */

	int irq_rid[IRQ_MAX]; /* irq rids */
	struct resource *irq[IRQ_MAX]; /* irq resources */
	int irq_alloced[IRQ_MAX]; /* irq alloc flag */

	int drq_rid[DRQ_MAX]; /* drq rids */
	struct resource *drq[DRQ_MAX]; /* drq resources */
	int drq_alloced[DRQ_MAX]; /* drq alloc flag */

	struct sbc_ihl ihl[IRQ_MAX];

	void *ih[IRQ_MAX];

	struct mtx *lock;

	u_int32_t bd_ver;
};

static int sbc_probe(device_t dev);
static int sbc_attach(device_t dev);
static void sbc_intr(void *p);

static struct resource *sbc_alloc_resource(device_t bus, device_t child, int type, int *rid,
					   rman_res_t start, rman_res_t end, rman_res_t count, u_int flags);
static int sbc_release_resource(device_t bus, device_t child, int type, int rid,
				struct resource *r);
static int sbc_setup_intr(device_t dev, device_t child, struct resource *irq,
   	       int flags,
	       driver_filter_t *filter,
	       driver_intr_t *intr, 
   	       void *arg, void **cookiep);
static int sbc_teardown_intr(device_t dev, device_t child, struct resource *irq,
  		  void *cookie);

static int alloc_resource(struct sbc_softc *scp);
static int release_resource(struct sbc_softc *scp);

static devclass_t sbc_devclass;

static int io_range[3] = {0x10, 0x2, 0x4};

static int sb_rd(struct resource *io, int reg);
static void sb_wr(struct resource *io, int reg, u_int8_t val);
static int sb_dspready(struct resource *io);
static int sb_cmd(struct resource *io, u_char val);
static u_int sb_get_byte(struct resource *io);
static void sb_setmixer(struct resource *io, u_int port, u_int value);

static void
sbc_lockinit(struct sbc_softc *scp)
{
	scp->lock = snd_mtxcreate(device_get_nameunit(scp->dev),
	    "snd_sbc softc");
}

static void
sbc_lockdestroy(struct sbc_softc *scp)
{
	snd_mtxfree(scp->lock);
}

void
sbc_lock(struct sbc_softc *scp)
{
	snd_mtxlock(scp->lock);
}

void
sbc_lockassert(struct sbc_softc *scp)
{
	snd_mtxassert(scp->lock);
}

void
sbc_unlock(struct sbc_softc *scp)
{
	snd_mtxunlock(scp->lock);
}

static int
sb_rd(struct resource *io, int reg)
{
	return bus_space_read_1(rman_get_bustag(io),
				rman_get_bushandle(io),
				reg);
}

static void
sb_wr(struct resource *io, int reg, u_int8_t val)
{
	bus_space_write_1(rman_get_bustag(io),
			  rman_get_bushandle(io),
			  reg, val);
}

static int
sb_dspready(struct resource *io)
{
	return ((sb_rd(io, SBDSP_STATUS) & 0x80) == 0);
}

static int
sb_dspwr(struct resource *io, u_char val)
{
    	int  i;

    	for (i = 0; i < 1000; i++) {
		if (sb_dspready(io)) {
	    		sb_wr(io, SBDSP_CMD, val);
	    		return 1;
		}
		if (i > 10) DELAY((i > 100)? 1000 : 10);
    	}
    	printf("sb_dspwr(0x%02x) timed out.\n", val);
    	return 0;
}

static int
sb_cmd(struct resource *io, u_char val)
{
    	return sb_dspwr(io, val);
}

static void
sb_setmixer(struct resource *io, u_int port, u_int value)
{
    	u_long   flags;

    	flags = spltty();
    	sb_wr(io, SB_MIX_ADDR, (u_char) (port & 0xff)); /* Select register */
    	DELAY(10);
    	sb_wr(io, SB_MIX_DATA, (u_char) (value & 0xff));
    	DELAY(10);
    	splx(flags);
}

static u_int
sb_get_byte(struct resource *io)
{
    	int i;

    	for (i = 1000; i > 0; i--) {
		if (sb_rd(io, DSP_DATA_AVAIL) & 0x80)
			return sb_rd(io, DSP_READ);
		else
			DELAY(20);
    	}
    	return 0xffff;
}

static int
sb_reset_dsp(struct resource *io)
{
    	sb_wr(io, SBDSP_RST, 3);
    	DELAY(100);
    	sb_wr(io, SBDSP_RST, 0);
    	return (sb_get_byte(io) == 0xAA)? 0 : ENXIO;
}

static int
sb_identify_board(struct resource *io)
{
	int ver, essver, rev;

    	sb_cmd(io, DSP_CMD_GETVER);	/* Get version */
    	ver = (sb_get_byte(io) << 8) | sb_get_byte(io);
	if (ver < 0x100 || ver > 0x4ff) return 0;
    	if (ver == 0x0301) {
	    	/* Try to detect ESS chips. */
	    	sb_cmd(io, DSP_CMD_GETID); /* Return ident. bytes. */
	    	essver = (sb_get_byte(io) << 8) | sb_get_byte(io);
	    	rev = essver & 0x000f;
	    	essver &= 0xfff0;
	    	if (essver == 0x4880) ver |= 0x1000;
	    	else if (essver == 0x6880) ver = 0x0500 | rev;
	}
	return ver;
}

static struct isa_pnp_id sbc_ids[] = {
	{0x01008c0e, "Creative ViBRA16C"},		/* CTL0001 */
	{0x31008c0e, "Creative SB16/SB32"},		/* CTL0031 */
	{0x41008c0e, "Creative SB16/SB32"},		/* CTL0041 */
	{0x42008c0e, "Creative SB AWE64"},		/* CTL0042 */
	{0x43008c0e, "Creative ViBRA16X"},		/* CTL0043 */
	{0x44008c0e, "Creative SB AWE64 Gold"},		/* CTL0044 */
	{0x45008c0e, "Creative SB AWE64"},		/* CTL0045 */
	{0x46008c0e, "Creative SB AWE64"},		/* CTL0046 */

	{0x01000000, "Avance Logic ALS100+"},		/* @@@0001 - ViBRA16X clone */
	{0x01100000, "Avance Asound 110"},		/* @@@1001 */
	{0x01200000, "Avance Logic ALS120"},		/* @@@2001 - ViBRA16X clone */

	{0x81167316, "ESS ES1681"},			/* ESS1681 */
	{0x02017316, "ESS ES1688"},			/* ESS1688 */
	{0x68097316, "ESS ES1688"},			/* ESS1688 */
	{0x68187316, "ESS ES1868"},			/* ESS1868 */
	{0x03007316, "ESS ES1869"},			/* ESS1869 */
	{0x69187316, "ESS ES1869"},			/* ESS1869 */
	{0xabb0110e, "ESS ES1869 (Compaq OEM)"},	/* CPQb0ab */
	{0xacb0110e, "ESS ES1869 (Compaq OEM)"},	/* CPQb0ac */
	{0x78187316, "ESS ES1878"},			/* ESS1878 */
	{0x79187316, "ESS ES1879"},			/* ESS1879 */
	{0x88187316, "ESS ES1888"},			/* ESS1888 */
	{0x07017316, "ESS ES1888 (DEC OEM)"},		/* ESS0107 */
	{0x06017316, "ESS ES1888 (Dell OEM)"},          /* ESS0106 */
	{0}
};

static int
sbc_probe(device_t dev)
{
	char *s = NULL;
	u_int32_t lid, vid;

	lid = isa_get_logicalid(dev);
	vid = isa_get_vendorid(dev);
	if (lid) {
		if (lid == 0x01000000 && vid != 0x01009305) /* ALS0001 */
			return ENXIO;
		/* Check pnp ids */
		return ISA_PNP_PROBE(device_get_parent(dev), dev, sbc_ids);
	} else {
		int rid = 0, ver;
	    	struct resource *io;

		io = bus_alloc_resource_anywhere(dev, SYS_RES_IOPORT, &rid,
						 16, RF_ACTIVE);
		if (!io) goto bad;
    		if (sb_reset_dsp(io)) goto bad2;
		ver = sb_identify_board(io);
		if (ver == 0) goto bad2;
		switch ((ver & 0x00000f00) >> 8) {
		case 1:
			device_set_desc(dev, "SoundBlaster 1.0 (not supported)");
			s = NULL;
			break;

		case 2:
			s = "SoundBlaster 2.0";
			break;

		case 3:
			s = (ver & 0x0000f000)? "ESS 488" : "SoundBlaster Pro";
			break;

		case 4:
			s = "SoundBlaster 16";
			break;

		case 5:
			s = (ver & 0x00000008)? "ESS 688" : "ESS 1688";
			break;
	     	}
		if (s) device_set_desc(dev, s);
bad2:		bus_release_resource(dev, SYS_RES_IOPORT, rid, io);
bad:		return s? 0 : ENXIO;
	}
}

static int
sbc_attach(device_t dev)
{
	char *err = NULL;
	struct sbc_softc *scp;
	struct sndcard_func *func;
	u_int32_t logical_id = isa_get_logicalid(dev);
    	int flags = device_get_flags(dev);
	int f, dh, dl, x, irq, i;

    	if (!logical_id && (flags & DV_F_DUAL_DMA)) {
        	bus_set_resource(dev, SYS_RES_DRQ, 1,
				 flags & DV_F_DRQ_MASK, 1);
    	}

	scp = device_get_softc(dev);
	bzero(scp, sizeof(*scp));
	scp->dev = dev;
	sbc_lockinit(scp);
	err = "alloc_resource";
	if (alloc_resource(scp)) goto bad;

	err = "sb_reset_dsp";
	if (sb_reset_dsp(scp->io[0])) goto bad;
	err = "sb_identify_board";
	scp->bd_ver = sb_identify_board(scp->io[0]) & 0x00000fff;
	if (scp->bd_ver == 0) goto bad;
	f = 0;
	if (logical_id == 0x01200000 && scp->bd_ver < 0x0400) scp->bd_ver = 0x0499;
	switch ((scp->bd_ver & 0x0f00) >> 8) {
    	case 1: /* old sound blaster has nothing... */
		break;

    	case 2:
		f |= BD_F_DUP_MIDI;
		if (scp->bd_ver > 0x200) f |= BD_F_MIX_CT1335;
		break;

	case 5:
		f |= BD_F_ESS;
		scp->bd_ver = 0x0301;
    	case 3:
		f |= BD_F_DUP_MIDI | BD_F_MIX_CT1345;
		break;

    	case 4:
    		f |= BD_F_SB16 | BD_F_MIX_CT1745;
		if (scp->drq[0]) dl = rman_get_start(scp->drq[0]); else dl = -1;
		if (scp->drq[1]) dh = rman_get_start(scp->drq[1]); else dh = dl;
		if (!logical_id && (dh < dl)) {
			struct resource *r;
			r = scp->drq[0];
			scp->drq[0] = scp->drq[1];
			scp->drq[1] = r;
			dl = rman_get_start(scp->drq[0]);
			dh = rman_get_start(scp->drq[1]);
		}
		/* soft irq/dma configuration */
		x = -1;
		irq = rman_get_start(scp->irq[0]);
		if      (irq == 5) x = 2;
		else if (irq == 7) x = 4;
		else if (irq == 9) x = 1;
		else if (irq == 10) x = 8;
		if (x == -1) {
			err = "bad irq (5/7/9/10 valid)";
			goto bad;
		}
		else sb_setmixer(scp->io[0], IRQ_NR, x);
		sb_setmixer(scp->io[0], DMA_NR, (1 << dh) | (1 << dl));
		if (bootverbose) {
			device_printf(dev, "setting card to irq %d, drq %d", irq, dl);
			if (dl != dh) printf(", %d", dh);
			printf("\n");
    		}
		break;
    	}

	switch (logical_id) {
    	case 0x43008c0e:	/* CTL0043 */
	case 0x01200000:
	case 0x01000000:
		f |= BD_F_SB16X;
		break;
	}
	scp->bd_ver |= f << 16;

	err = "setup_intr";
	for (i = 0; i < IRQ_MAX; i++) {
		scp->ihl[i].parent = scp;
		if (snd_setup_intr(dev, scp->irq[i], 0, sbc_intr, &scp->ihl[i], &scp->ih[i]))
			goto bad;
	}

	/* PCM Audio */
	func = malloc(sizeof(struct sndcard_func), M_DEVBUF, M_NOWAIT | M_ZERO);
	if (func == NULL) goto bad;
	func->func = SCF_PCM;
	scp->child_pcm = device_add_child(dev, "pcm", -1);
	device_set_ivars(scp->child_pcm, func);

	/* Midi Interface */
	func = malloc(sizeof(struct sndcard_func), M_DEVBUF, M_NOWAIT | M_ZERO);
	if (func == NULL) goto bad;
	func->func = SCF_MIDI;
	scp->child_midi1 = device_add_child(dev, "midi", -1);
	device_set_ivars(scp->child_midi1, func);

	/* OPL FM Synthesizer */
	func = malloc(sizeof(struct sndcard_func), M_DEVBUF, M_NOWAIT | M_ZERO);
	if (func == NULL) goto bad;
	func->func = SCF_SYNTH;
	scp->child_midi2 = device_add_child(dev, "midi", -1);
	device_set_ivars(scp->child_midi2, func);

	/* probe/attach kids */
	bus_generic_attach(dev);

	return (0);

bad:	if (err) device_printf(dev, "%s\n", err);
	release_resource(scp);
	return (ENXIO);
}

static int
sbc_detach(device_t dev)
{
	struct sbc_softc *scp = device_get_softc(dev);

	sbc_lock(scp);
	device_delete_child(dev, scp->child_midi2);
	device_delete_child(dev, scp->child_midi1);
	device_delete_child(dev, scp->child_pcm);
	release_resource(scp);
	sbc_lockdestroy(scp);
	return bus_generic_detach(dev);
}

static void
sbc_intr(void *p)
{
	struct sbc_ihl *ihl = p;
	int i;

	/* sbc_lock(ihl->parent); */
	i = 0;
	while (i < INTR_MAX) {
		if (ihl->intr[i] != NULL) ihl->intr[i](ihl->intr_arg[i]);
		i++;
	}
	/* sbc_unlock(ihl->parent); */
}

static int
sbc_setup_intr(device_t dev, device_t child, struct resource *irq, int flags,
   	       driver_filter_t *filter,
	       driver_intr_t *intr, 
   	       void *arg, void **cookiep)
{
	struct sbc_softc *scp = device_get_softc(dev);
	struct sbc_ihl *ihl = NULL;
	int i, ret;

	if (filter != NULL) {
		printf("sbc.c: we cannot use a filter here\n");
		return (EINVAL);
	}
	sbc_lock(scp);
	i = 0;
	while (i < IRQ_MAX) {
		if (irq == scp->irq[i]) ihl = &scp->ihl[i];
		i++;
	}
	ret = 0;
	if (ihl == NULL) ret = EINVAL;
	i = 0;
	while ((ret == 0) && (i < INTR_MAX)) {
		if (ihl->intr[i] == NULL) {
			ihl->intr[i] = intr;
			ihl->intr_arg[i] = arg;
			*cookiep = &ihl->intr[i];
			ret = -1;
		} else i++;
	}
	sbc_unlock(scp);
	return (ret > 0)? EINVAL : 0;
}

static int
sbc_teardown_intr(device_t dev, device_t child, struct resource *irq,
  		  void *cookie)
{
	struct sbc_softc *scp = device_get_softc(dev);
	struct sbc_ihl *ihl = NULL;
	int i, ret;

	sbc_lock(scp);
	i = 0;
	while (i < IRQ_MAX) {
		if (irq == scp->irq[i]) ihl = &scp->ihl[i];
		i++;
	}
	ret = 0;
	if (ihl == NULL) ret = EINVAL;
	i = 0;
	while ((ret == 0) && (i < INTR_MAX)) {
		if (cookie == &ihl->intr[i]) {
			ihl->intr[i] = NULL;
			ihl->intr_arg[i] = NULL;
			return 0;
		} else i++;
	}
	sbc_unlock(scp);
	return (ret > 0)? EINVAL : 0;
}

static struct resource *
sbc_alloc_resource(device_t bus, device_t child, int type, int *rid,
		   rman_res_t start, rman_res_t end, rman_res_t count, u_int flags)
{
	struct sbc_softc *scp;
	int *alloced, rid_max, alloced_max;
	struct resource **res;

	scp = device_get_softc(bus);
	switch (type) {
	case SYS_RES_IOPORT:
		alloced = scp->io_alloced;
		res = scp->io;
		rid_max = IO_MAX - 1;
		alloced_max = 1;
		break;
	case SYS_RES_DRQ:
		alloced = scp->drq_alloced;
		res = scp->drq;
		rid_max = DRQ_MAX - 1;
		alloced_max = 1;
		break;
	case SYS_RES_IRQ:
		alloced = scp->irq_alloced;
		res = scp->irq;
		rid_max = IRQ_MAX - 1;
		alloced_max = INTR_MAX; /* pcm and mpu may share the irq. */
		break;
	default:
		return (NULL);
	}

	if (*rid > rid_max || alloced[*rid] == alloced_max)
		return (NULL);

	alloced[*rid]++;
	return (res[*rid]);
}

static int
sbc_release_resource(device_t bus, device_t child, int type, int rid,
			struct resource *r)
{
	struct sbc_softc *scp;
	int *alloced, rid_max;

	scp = device_get_softc(bus);
	switch (type) {
	case SYS_RES_IOPORT:
		alloced = scp->io_alloced;
		rid_max = IO_MAX - 1;
		break;
	case SYS_RES_DRQ:
		alloced = scp->drq_alloced;
		rid_max = DRQ_MAX - 1;
		break;
	case SYS_RES_IRQ:
		alloced = scp->irq_alloced;
		rid_max = IRQ_MAX - 1;
		break;
	default:
		return (1);
	}

	if (rid > rid_max || alloced[rid] == 0)
		return (1);

	alloced[rid]--;
	return (0);
}

static int
sbc_read_ivar(device_t bus, device_t dev, int index, uintptr_t * result)
{
	struct sbc_softc *scp = device_get_softc(bus);
	struct sndcard_func *func = device_get_ivars(dev);

	switch (index) {
	case 0:
		*result = func->func;
		break;

	case 1:
		*result = scp->bd_ver;
	      	break;

	default:
		return ENOENT;
	}

	return 0;
}

static int
sbc_write_ivar(device_t bus, device_t dev,
	       int index, uintptr_t value)
{
	switch (index) {
	case 0:
	case 1:
  		return EINVAL;

	default:
		return (ENOENT);
	}
}

static int
alloc_resource(struct sbc_softc *scp)
{
	int i;

	for (i = 0 ; i < IO_MAX ; i++) {
		if (scp->io[i] == NULL) {
			scp->io_rid[i] = i;
			scp->io[i] = bus_alloc_resource_anywhere(scp->dev,
								 SYS_RES_IOPORT,
								 &scp->io_rid[i],
								io_range[i],
								RF_ACTIVE);
			if (i == 0 && scp->io[i] == NULL)
				return (1);
			scp->io_alloced[i] = 0;
		}
	}
	for (i = 0 ; i < DRQ_MAX ; i++) {
		if (scp->drq[i] == NULL) {
			scp->drq_rid[i] = i;
			scp->drq[i] = bus_alloc_resource_any(scp->dev,
							     SYS_RES_DRQ,
							     &scp->drq_rid[i],
							     RF_ACTIVE);
			if (i == 0 && scp->drq[i] == NULL)
				return (1);
			scp->drq_alloced[i] = 0;
		}
	}
	for (i = 0 ; i < IRQ_MAX ; i++) {
	 	if (scp->irq[i] == NULL) {
			scp->irq_rid[i] = i;
			scp->irq[i] = bus_alloc_resource_any(scp->dev,
							     SYS_RES_IRQ,
							     &scp->irq_rid[i],
							     RF_ACTIVE);
			if (i == 0 && scp->irq[i] == NULL)
				return (1);
			scp->irq_alloced[i] = 0;
		}
	}
	return (0);
}

static int
release_resource(struct sbc_softc *scp)
{
	int i;

	for (i = 0 ; i < IO_MAX ; i++) {
		if (scp->io[i] != NULL) {
			bus_release_resource(scp->dev, SYS_RES_IOPORT, scp->io_rid[i], scp->io[i]);
			scp->io[i] = NULL;
		}
	}
	for (i = 0 ; i < DRQ_MAX ; i++) {
		if (scp->drq[i] != NULL) {
			bus_release_resource(scp->dev, SYS_RES_DRQ, scp->drq_rid[i], scp->drq[i]);
			scp->drq[i] = NULL;
		}
	}
	for (i = 0 ; i < IRQ_MAX ; i++) {
		if (scp->irq[i] != NULL) {
			if (scp->ih[i] != NULL)
				bus_teardown_intr(scp->dev, scp->irq[i], scp->ih[i]);
			scp->ih[i] = NULL;
			bus_release_resource(scp->dev, SYS_RES_IRQ, scp->irq_rid[i], scp->irq[i]);
			scp->irq[i] = NULL;
		}
	}
	return (0);
}

static device_method_t sbc_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe,		sbc_probe),
	DEVMETHOD(device_attach,	sbc_attach),
	DEVMETHOD(device_detach,	sbc_detach),
	DEVMETHOD(device_shutdown,	bus_generic_shutdown),
	DEVMETHOD(device_suspend,	bus_generic_suspend),
	DEVMETHOD(device_resume,	bus_generic_resume),

	/* Bus interface */
	DEVMETHOD(bus_read_ivar,	sbc_read_ivar),
	DEVMETHOD(bus_write_ivar,	sbc_write_ivar),
	DEVMETHOD(bus_alloc_resource,	sbc_alloc_resource),
	DEVMETHOD(bus_release_resource,	sbc_release_resource),
	DEVMETHOD(bus_activate_resource, bus_generic_activate_resource),
	DEVMETHOD(bus_deactivate_resource, bus_generic_deactivate_resource),
	DEVMETHOD(bus_setup_intr,	sbc_setup_intr),
	DEVMETHOD(bus_teardown_intr,	sbc_teardown_intr),

	DEVMETHOD_END
};

static driver_t sbc_driver = {
	"sbc",
	sbc_methods,
	sizeof(struct sbc_softc),
};

/* sbc can be attached to an isa bus. */
DRIVER_MODULE(snd_sbc, isa, sbc_driver, sbc_devclass, 0, 0);
DRIVER_MODULE(snd_sbc, acpi, sbc_driver, sbc_devclass, 0, 0);
MODULE_DEPEND(snd_sbc, sound, SOUND_MINVER, SOUND_PREFVER, SOUND_MAXVER);
MODULE_VERSION(snd_sbc, 1);
ISA_PNP_INFO(sbc_ids);
