/*	$OpenBSD: nviic.c,v 1.18 2022/03/11 18:00:51 mpi Exp $ */

/*
 * Copyright (c) 2005 David Gwynne <dlg@openbsd.org>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/device.h>
#include <sys/rwlock.h>

#include <machine/bus.h>

#include <dev/pci/pcidevs.h>
#include <dev/pci/pcireg.h>
#include <dev/pci/pcivar.h>

#include <dev/i2c/i2cvar.h>

/* PCI Configuration space registers */
#define NVI_PCI_SMBASE1		0x20
#define NVI_PCI_SMBASE2		0x24

#define NVI_OLD_PCI_SMBASE1	0x50
#define NVI_OLD_PCI_SMBASE2	0x54

#define NVI_SMBASE(x)		((x) & 0xfffc)
#define NVI_SMBASE_SIZE		8

/* SMBus 2.0 registers */   
#define NVI_SMB_PRTCL		0x00	/* protocol, PEC */
#define NVI_SMB_STS		0x01	/* status */
#define NVI_SMB_ADDR		0x02	/* address */
#define NVI_SMB_CMD		0x03	/* command */
#define NVI_SMB_DATA(o)		(0x04 + (o))	/* 32 data registers */
#define NVI_SMB_BCNT		0x24	/* number of data bytes */
#define NVI_SMB_ALRM_A		0x25	/* alarm address */
#define NVI_SMB_ALRM_D		0x26	/* 2 bytes alarm data */

#define NVI_SMB_STS_DONE	0x80
#define NVI_SMB_STS_ALRM	0x40
#define NVI_SMB_STS_RES		0x20
#define NVI_SMB_STS_STATUS	0x1f

#define NVI_SMB_PRTCL_WRITE	0x00
#define NVI_SMB_PRTCL_READ	0x01
#define NVI_SMB_PRTCL_QUICK	0x02
#define NVI_SMB_PRTCL_BYTE	0x04
#define NVI_SMB_PRTCL_BYTE_DATA	0x06
#define NVI_SMB_PRTCL_WORD_DATA	0x08
#define NVI_SMB_PRTCL_BLOCK_DATA 0x0a
#define NVI_SMB_PRTCL_PROC_CALL	0x0c
#define NVI_SMB_PRTCL_BLOCK_PROC_CALL 0x0d
#define NVI_SMB_PRTCL_PEC	0x80

#ifdef NVIIC_DEBUG
#define DPRINTF(x...)		do { if (nviic_debug) printf(x); } while (0)
int nviic_debug = 1;
#else
#define DPRINTF(x...)		/* x */
#endif

/* there are two iic busses on this pci device */
#define NVIIC_NBUS		2

int		nviic_match(struct device *, void *, void *);
void		nviic_attach(struct device *, struct device *, void *);

struct nviic_softc;

struct nviic_controller {
	struct nviic_softc	*nc_sc;
	bus_space_handle_t	nc_ioh;
	struct rwlock		nc_lock;
	struct i2c_controller	nc_i2c;
};

struct nviic_softc {
	struct device		sc_dev;
	bus_space_tag_t		sc_iot;
	struct nviic_controller	sc_nc[NVIIC_NBUS];
};

const struct cfattach nviic_ca = {
	sizeof(struct nviic_softc), nviic_match, nviic_attach
};

struct cfdriver nviic_cd = {
	NULL, "nviic", DV_DULL
};

int		nviic_i2c_acquire_bus(void *, int);
void		nviic_i2c_release_bus(void *, int);
int		nviic_i2c_exec(void *, i2c_op_t, i2c_addr_t, const void *,
		    size_t, void *, size_t, int);

u_int8_t	nviic_read(struct nviic_controller *, bus_size_t);
void		nviic_write(struct nviic_controller *, bus_size_t, u_int8_t);

#define DEVNAME(s)		((sc)->sc_dev.dv_xname)

const struct pci_matchid nviic_ids[] = {
	{ PCI_VENDOR_NVIDIA, PCI_PRODUCT_NVIDIA_NFORCE2_SMB },
	{ PCI_VENDOR_NVIDIA, PCI_PRODUCT_NVIDIA_NFORCE2_400_SMB },
	{ PCI_VENDOR_NVIDIA, PCI_PRODUCT_NVIDIA_NFORCE3_SMB },
	{ PCI_VENDOR_NVIDIA, PCI_PRODUCT_NVIDIA_NFORCE3_250_SMB },
	{ PCI_VENDOR_NVIDIA, PCI_PRODUCT_NVIDIA_NFORCE4_SMB },
	{ PCI_VENDOR_NVIDIA, PCI_PRODUCT_NVIDIA_MCP51_SMB },
	{ PCI_VENDOR_NVIDIA, PCI_PRODUCT_NVIDIA_MCP55_SMB },
	{ PCI_VENDOR_NVIDIA, PCI_PRODUCT_NVIDIA_MCP61_SMB },
	{ PCI_VENDOR_NVIDIA, PCI_PRODUCT_NVIDIA_MCP65_SMB },
	{ PCI_VENDOR_NVIDIA, PCI_PRODUCT_NVIDIA_MCP67_SMB },
	{ PCI_VENDOR_NVIDIA, PCI_PRODUCT_NVIDIA_MCP73_SMB },
	{ PCI_VENDOR_NVIDIA, PCI_PRODUCT_NVIDIA_MCP77_SMB },
	{ PCI_VENDOR_NVIDIA, PCI_PRODUCT_NVIDIA_MCP79_SMB },
	{ PCI_VENDOR_NVIDIA, PCI_PRODUCT_NVIDIA_MCP89_SMB }
};

int
nviic_match(struct device *parent, void *match, void *aux)
{
	return (pci_matchbyid(aux, nviic_ids,
	    sizeof(nviic_ids) / sizeof(nviic_ids[0])));
}

void
nviic_attach(struct device *parent, struct device *self, void *aux)
{
	struct nviic_softc		*sc = (struct nviic_softc *)self;
	struct pci_attach_args		*pa = aux;
	struct nviic_controller		*nc;
	struct i2cbus_attach_args	iba;
	int				baseregs[NVIIC_NBUS];
	pcireg_t			reg;
	int				i;

	sc->sc_iot = pa->pa_iot;

	printf("\n");

	/* Older chipsets used non-standard BARs */
	switch (PCI_PRODUCT(pa->pa_id)) {
	case PCI_PRODUCT_NVIDIA_NFORCE2_SMB:
	case PCI_PRODUCT_NVIDIA_NFORCE2_400_SMB:
	case PCI_PRODUCT_NVIDIA_NFORCE3_SMB:
	case PCI_PRODUCT_NVIDIA_NFORCE3_250_SMB:
	case PCI_PRODUCT_NVIDIA_NFORCE4_SMB:
		baseregs[0] = NVI_OLD_PCI_SMBASE1;
		baseregs[1] = NVI_OLD_PCI_SMBASE2;
		break;
	default:
		baseregs[0] = NVI_PCI_SMBASE1;
		baseregs[1] = NVI_PCI_SMBASE2;
	}

	for (i = 0; i < NVIIC_NBUS; i++) {
		nc = &sc->sc_nc[i];

		reg = pci_conf_read(pa->pa_pc, pa->pa_tag, baseregs[i]);
		if (NVI_SMBASE(reg) == 0 ||
		    bus_space_map(sc->sc_iot, NVI_SMBASE(reg), NVI_SMBASE_SIZE,
		    0, &nc->nc_ioh)) {
			printf("%s: unable to map space for bus %d\n",
			    DEVNAME(sc), i);
			continue;
		}

		nc->nc_sc = sc;
		rw_init(&nc->nc_lock, "nviic");
		nc->nc_i2c.ic_cookie = nc;
		nc->nc_i2c.ic_acquire_bus = nviic_i2c_acquire_bus;
		nc->nc_i2c.ic_release_bus = nviic_i2c_release_bus;
		nc->nc_i2c.ic_exec = nviic_i2c_exec;

		bzero(&iba, sizeof(iba));
		iba.iba_name = "iic";
		iba.iba_tag = &nc->nc_i2c;
		config_found(self, &iba, iicbus_print);
	}
}

int
nviic_i2c_acquire_bus(void *arg, int flags)
{
	struct nviic_controller		*nc = arg;

	if (cold || (flags & I2C_F_POLL))
		return (0);

	return (rw_enter(&nc->nc_lock, RW_WRITE | RW_INTR));
}

void
nviic_i2c_release_bus(void *arg, int flags)
{
	struct nviic_controller		*nc = arg;

	if (cold || (flags & I2C_F_POLL))
		return;

	rw_exit(&nc->nc_lock);
}

int
nviic_i2c_exec(void *arg, i2c_op_t op, i2c_addr_t addr,
    const void *cmdbuf, size_t cmdlen, void *buf, size_t len, int flags)
{
	struct nviic_controller		*nc = arg;
#ifdef NVIIC_DEBUG
	struct nviic_softc		*sc = nc->nc_sc;
#endif
	u_int8_t			protocol;
	u_int8_t			*b;
	u_int8_t			sts;
	int				i;

	DPRINTF("%s: exec op: %d addr: 0x%x cmdlen: %d len: %d flags 0x%x\n",
	    DEVNAME(sc), op, addr, cmdlen, len, flags);

	if (cold)
		flags |= I2C_F_POLL;

	if (I2C_OP_STOP_P(op) == 0 || cmdlen > 1 || len > 2)
		return (1);

	/* set slave address */
	nviic_write(nc, NVI_SMB_ADDR, addr << 1);

	/* set command byte */
	if (cmdlen > 0) {
		b = (u_int8_t *)cmdbuf;
		nviic_write(nc, NVI_SMB_CMD, b[0]);
	}

	b = (u_int8_t *)buf;

	/* write data */
	if (I2C_OP_WRITE_P(op)) {
		for (i = 0; i < len; i++)
			nviic_write(nc, NVI_SMB_DATA(i), b[i]);
	}

	switch (len) {
	case 0:
		protocol = NVI_SMB_PRTCL_BYTE;
		break;
	case 1:
		protocol = NVI_SMB_PRTCL_BYTE_DATA;
		break;
	case 2:
		protocol = NVI_SMB_PRTCL_WORD_DATA;
		break;
	}

	/* set direction */
	if (I2C_OP_READ_P(op))
		protocol |= NVI_SMB_PRTCL_READ;

	/* start transaction */
	nviic_write(nc, NVI_SMB_PRTCL, protocol);

	for (i = 1000; i > 0; i--) {
		delay(100);
		if (nviic_read(nc, NVI_SMB_PRTCL) == 0)
			break;
	}
	if (i == 0) {
		DPRINTF("%s: timeout\n", DEVNAME(sc));
		return (1);
	}

	sts = nviic_read(nc, NVI_SMB_STS);
	if (sts & NVI_SMB_STS_STATUS)
		return (1);

	/* read data */
	if (I2C_OP_READ_P(op)) {
		for (i = 0; i < len; i++)
			b[i] = nviic_read(nc, NVI_SMB_DATA(i));
	}

	return (0);
}

u_int8_t
nviic_read(struct nviic_controller *nc, bus_size_t r)
{
	struct nviic_softc		*sc = nc->nc_sc;

	bus_space_barrier(sc->sc_iot, nc->nc_ioh, r, 1,
	    BUS_SPACE_BARRIER_READ);
	return (bus_space_read_1(sc->sc_iot, nc->nc_ioh, r));
}

void
nviic_write(struct nviic_controller *nc, bus_size_t r, u_int8_t v)
{
	struct nviic_softc		*sc = nc->nc_sc;

	bus_space_write_1(sc->sc_iot, nc->nc_ioh, r, v);
	bus_space_barrier(sc->sc_iot, nc->nc_ioh, r, 1,
	    BUS_SPACE_BARRIER_WRITE);
}
