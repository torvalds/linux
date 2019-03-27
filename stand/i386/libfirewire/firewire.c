/*-
 * Copyright (c) 2004 Hidetoshi Shimokawa <simokawa@FreeBSD.ORG>
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

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

/*
 * FireWire disk device handling.
 * 
 */

#include <stand.h>

#include <machine/bootinfo.h>

#include <stdarg.h>

#include <bootstrap.h>
#include <btxv86.h>
#include <libi386.h>
#include <dev/firewire/firewire.h>
#include "fwohci.h"
#include <dev/dcons/dcons.h>

/* XXX */
#define BIT4x2(x,y)      uint8_t  y:4, x:4
#define BIT16x2(x,y)    uint32_t y:16, x:16
#define _KERNEL
#include <dev/firewire/iec13213.h>

extern uint32_t dcons_paddr;
extern struct console dconsole;

struct crom_src_buf {
	struct crom_src src;
	struct crom_chunk root;
	struct crom_chunk vendor;
	struct crom_chunk hw;
	/* for dcons */
	struct crom_chunk unit;
	struct crom_chunk spec;
	struct crom_chunk ver;
};

static int	fw_init(void);
static int	fw_strategy(void *devdata, int flag, daddr_t dblk,
		    size_t size, char *buf, size_t *rsize);
static int	fw_open(struct open_file *f, ...);
static int	fw_close(struct open_file *f);
static int	fw_print(int verbose);
static void	fw_cleanup(void);

void		fw_enable(void);

struct devsw fwohci = {
    "FW1394", 	/* 7 chars at most */
    DEVT_NET, 
    fw_init,
    fw_strategy, 
    fw_open, 
    fw_close, 
    noioctl,
    fw_print,
    fw_cleanup
};

static struct fwohci_softc fwinfo[MAX_OHCI];
static int fw_initialized = 0;

static void
fw_probe(int index, struct fwohci_softc *sc)
{
	int err;

	sc->state = FWOHCI_STATE_INIT;
	err = biospci_find_devclass(
		0x0c0010	/* Serial:FireWire:OHCI */,
		index		/* index */,
		&sc->locator);

	if (err != 0) {
		sc->state = FWOHCI_STATE_DEAD;
		return;
	}

	biospci_write_config(sc->locator,
	    0x4	/* command */,
	    BIOSPCI_16BITS,
	    0x6	/* enable bus master and memory mapped I/O */);

	biospci_read_config(sc->locator, 0x00 /*devid*/, BIOSPCI_32BITS,
		&sc->devid);
	biospci_read_config(sc->locator, 0x10 /*base_addr*/, BIOSPCI_32BITS,
		&sc->base_addr);

        sc->handle = (uint32_t)PTOV(sc->base_addr);
	sc->bus_id = OREAD(sc, OHCI_BUS_ID);

	return;
}

static int
fw_init(void) 
{
	int i, avail;
	struct fwohci_softc *sc;

	if (fw_initialized)
		return (0);

	avail = 0;
	for (i = 0; i < MAX_OHCI; i ++) {
		sc = &fwinfo[i];
		fw_probe(i, sc);
		if (sc->state == FWOHCI_STATE_DEAD)
			break;
		avail ++;
		break;
	}
	fw_initialized = 1;

	return (0);
}


/*
 * Print information about OHCI chips
 */
static int
fw_print(int verbose)
{
	char line[80];
	int i, ret = 0;
	struct fwohci_softc *sc;

	printf("%s devices:", fwohci.dv_name);
	if ((ret = pager_output("\n")) != 0)
		return (ret);

	for (i = 0; i < MAX_OHCI; i ++) {
		sc = &fwinfo[i];
		if (sc->state == FWOHCI_STATE_DEAD)
			break;
		snprintf(line, sizeof(line), "%d: locator=0x%04x devid=0x%08x"
			" base_addr=0x%08x handle=0x%08x bus_id=0x%08x\n",
			i, sc->locator, sc->devid,
			sc->base_addr, sc->handle, sc->bus_id);
		ret = pager_output(line);
		if (ret != 0)
			break;
	}
	return (ret);
}

static int 
fw_open(struct open_file *f, ...)
{
#if 0
    va_list			ap;
    struct i386_devdesc		*dev;
    struct open_disk		*od;
    int				error;

    va_start(ap, f);
    dev = va_arg(ap, struct i386_devdesc *);
    va_end(ap);
#endif

    return (ENXIO);
}

static int
fw_close(struct open_file *f)
{
    return (0);
}

static void 
fw_cleanup()
{
    struct dcons_buf *db;

    /* invalidate dcons buffer */
    if (dcons_paddr) {
	db = (struct dcons_buf *)PTOV(dcons_paddr);
	db->magic = 0;
    }
}

static int 
fw_strategy(void *devdata, int rw, daddr_t dblk, size_t size,
    char *buf, size_t *rsize)
{
	return (EIO);
}

static void
fw_init_crom(struct fwohci_softc *sc)
{
	struct crom_src *src;

	printf("fw_init_crom\n");
	sc->crom_src_buf = (struct crom_src_buf *)
		malloc(sizeof(struct crom_src_buf));
	if (sc->crom_src_buf == NULL)
		return;

	src = &sc->crom_src_buf->src;
	bzero(src, sizeof(struct crom_src));

	/* BUS info sample */
	src->hdr.info_len = 4;

	src->businfo.bus_name = CSR_BUS_NAME_IEEE1394;

	src->businfo.irmc = 1;
	src->businfo.cmc = 1;
	src->businfo.isc = 1;
	src->businfo.bmc = 1;
	src->businfo.pmc = 0;
	src->businfo.cyc_clk_acc = 100;
	src->businfo.max_rec = sc->maxrec;
	src->businfo.max_rom = MAXROM_4;
#define FW_GENERATION_CHANGEABLE 2
	src->businfo.generation = FW_GENERATION_CHANGEABLE;
	src->businfo.link_spd = sc->speed;

	src->businfo.eui64.hi = sc->eui.hi;
	src->businfo.eui64.lo = sc->eui.lo;

	STAILQ_INIT(&src->chunk_list);

	sc->crom_src = src;
	sc->crom_root = &sc->crom_src_buf->root;
}

static void
fw_reset_crom(struct fwohci_softc *sc)
{
	struct crom_src_buf *buf;
	struct crom_src *src;
	struct crom_chunk *root;

	printf("fw_reset\n");
	if (sc->crom_src_buf == NULL)
		fw_init_crom(sc);

	buf = sc->crom_src_buf;
	src = sc->crom_src;
	root = sc->crom_root;

	STAILQ_INIT(&src->chunk_list);

	bzero(root, sizeof(struct crom_chunk));
	crom_add_chunk(src, NULL, root, 0);
	crom_add_entry(root, CSRKEY_NCAP, 0x0083c0); /* XXX */
	/* private company_id */
	crom_add_entry(root, CSRKEY_VENDOR, CSRVAL_VENDOR_PRIVATE);
#ifdef __DragonFly__
	crom_add_simple_text(src, root, &buf->vendor, "DragonFly Project");
#else
	crom_add_simple_text(src, root, &buf->vendor, "FreeBSD Project");
#endif
}


#define ADDR_HI(x)	(((x) >> 24) & 0xffffff)
#define ADDR_LO(x)	((x) & 0xffffff)

static void
dcons_crom(struct fwohci_softc *sc)
{
	struct crom_src_buf *buf;
	struct crom_src *src;
	struct crom_chunk *root;

	buf = sc->crom_src_buf;
	src = sc->crom_src;
	root = sc->crom_root;

	bzero(&buf->unit, sizeof(struct crom_chunk));

	crom_add_chunk(src, root, &buf->unit, CROM_UDIR);
	crom_add_entry(&buf->unit, CSRKEY_SPEC, CSRVAL_VENDOR_PRIVATE);
	crom_add_simple_text(src, &buf->unit, &buf->spec, "FreeBSD");
	crom_add_entry(&buf->unit, CSRKEY_VER, DCONS_CSR_VAL_VER);
	crom_add_simple_text(src, &buf->unit, &buf->ver, "dcons");
	crom_add_entry(&buf->unit, DCONS_CSR_KEY_HI, ADDR_HI(dcons_paddr));
	crom_add_entry(&buf->unit, DCONS_CSR_KEY_LO, ADDR_LO(dcons_paddr));
}

void
fw_crom(struct fwohci_softc *sc)
{
	struct crom_src *src;
	void *newrom;

	fw_reset_crom(sc);
	dcons_crom(sc);

	newrom = malloc(CROMSIZE);
	src = &sc->crom_src_buf->src;
	crom_load(src, (uint32_t *)newrom, CROMSIZE);
	if (bcmp(newrom, sc->config_rom, CROMSIZE) != 0) {
		/* Bump generation and reload. */
		src->businfo.generation++;

		/* Handle generation count wraps. */
		if (src->businfo.generation < 2)
			src->businfo.generation = 2;

		/* Recalculate CRC to account for generation change. */
		crom_load(src, (uint32_t *)newrom, CROMSIZE);
		bcopy(newrom, (void *)sc->config_rom, CROMSIZE);
	}
	free(newrom);
}

static int
fw_busreset(struct fwohci_softc *sc)
{
	int count;

	if (sc->state < FWOHCI_STATE_ENABLED) {
		printf("fwohci not enabled\n");
		return(CMD_OK);
	}
	fw_crom(sc);
	fwohci_ibr(sc);
	count = 0;
	while (sc->state< FWOHCI_STATE_NORMAL) {
		fwohci_poll(sc);
		count ++;
		if (count > 1000) {
			printf("give up to wait bus initialize\n");
			return (-1);
		}
	}
	printf("poll count = %d\n", count);
	return (0);
}

void
fw_enable(void)
{
	struct fwohci_softc *sc;
	int i;

	if (fw_initialized == 0)
		fw_init();

	for (i = 0; i < MAX_OHCI; i ++) {
		sc = &fwinfo[i];
		if (sc->state != FWOHCI_STATE_INIT)
			break;

		sc->config_rom = (uint32_t *)
			(((uint32_t)sc->config_rom_buf
				+ (CROMSIZE - 1)) & ~(CROMSIZE - 1));
#if 0
		printf("configrom: %08p %08p\n",
			sc->config_rom_buf, sc->config_rom);
#endif
		if (fwohci_init(sc, 0) == 0) {
			sc->state = FWOHCI_STATE_ENABLED;
			fw_busreset(sc);
		} else
			sc->state = FWOHCI_STATE_DEAD;
	}
}

void
fw_poll(void)
{
	struct fwohci_softc *sc;
	int i;

	if (fw_initialized == 0)
		return;

	for (i = 0; i < MAX_OHCI; i ++) {
		sc = &fwinfo[i];
		if (sc->state < FWOHCI_STATE_ENABLED)
			break;
		fwohci_poll(sc);
	}
}

#if 0 /* for debug */
static int
fw_busreset_cmd(int argc, char *argv[])
{
	struct fwohci_softc *sc;
	int i;

	for (i = 0; i < MAX_OHCI; i ++) {
		sc = &fwinfo[i];
		if (sc->state < FWOHCI_STATE_INIT)
			break;
		fw_busreset(sc);
	}
	return(CMD_OK);
}

static int
fw_poll_cmd(int argc, char *argv[])
{
	fw_poll();
	return(CMD_OK);
}

static int
fw_enable_cmd(int argc, char *argv[])
{
	fw_print(0);
	fw_enable();
	return(CMD_OK);
}


static int
dcons_enable(int argc, char *argv[])
{
	dconsole.c_init(0);
	fw_enable();
	dconsole.c_flags |= C_ACTIVEIN | C_ACTIVEOUT;
	return(CMD_OK);
}

static int
dcons_read(int argc, char *argv[])
{
	char c;
	while (dconsole.c_ready()) {
		c = dconsole.c_in();
		printf("%c", c);
	}
	printf("\r\n");
	return(CMD_OK);
}

static int
dcons_write(int argc, char *argv[])
{
	int len, i;
	if (argc < 2)
		return(CMD_OK);

	len = strlen(argv[1]);
	for (i = 0; i < len; i ++)
		dconsole.c_out(argv[1][i]);
	dconsole.c_out('\r');
	dconsole.c_out('\n');
	return(CMD_OK);
}
COMMAND_SET(firewire, "firewire", "enable firewire", fw_enable_cmd);
COMMAND_SET(fwbusreset, "fwbusreset", "firewire busreset", fw_busreset_cmd);
COMMAND_SET(fwpoll, "fwpoll", "firewire poll", fw_poll_cmd);
COMMAND_SET(dcons, "dcons", "enable dcons", dcons_enable);
COMMAND_SET(dread, "dread", "read from dcons", dcons_read);
COMMAND_SET(dwrite, "dwrite", "write to dcons", dcons_write);
#endif
