/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (C) 2012 Juniper Networks, Inc.
 * Copyright (C) 2009-2012 Semihalf
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
/*
 * TODO :
 *
 *  -- test support for small pages
 *  -- support for reading ONFI parameters
 *  -- support for cached and interleaving commands
 *  -- proper setting of AL bits in FMR
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/proc.h>
#include <sys/bus.h>
#include <sys/conf.h>
#include <sys/kernel.h>
#include <sys/module.h>
#include <sys/malloc.h>
#include <sys/rman.h>
#include <sys/sysctl.h>
#include <sys/time.h>
#include <sys/kdb.h>

#include <machine/bus.h>

#include <dev/ofw/ofw_bus.h>
#include <dev/ofw/ofw_bus_subr.h>

#include <powerpc/mpc85xx/lbc.h>

#include <dev/nand/nand.h>
#include <dev/nand/nandbus.h>

#include "nfc_fsl.h"

#include "nfc_if.h"

#define LBC_READ(regname)	lbc_read_reg(dev, (LBC85XX_ ## regname))
#define LBC_WRITE(regname, val)	lbc_write_reg(dev, (LBC85XX_ ## regname), val)

enum addr_type {
	ADDR_NONE,
	ADDR_ID,
	ADDR_ROW,
	ADDR_ROWCOL
};

struct fsl_nfc_fcm {
	/* Read-only after initialization */
	uint32_t	reg_fmr;

	/* To be preserved across "start_command" */
	u_int		buf_ofs;
	u_int		read_ptr;
	u_int		status:1;

	/* Command state -- cleared by "start_command" */
	uint32_t	fcm_startzero;
	uint32_t	reg_fcr;
	uint32_t	reg_fir;
	uint32_t	reg_mdr;
	uint32_t	reg_fbcr;
	uint32_t	reg_fbar;
	uint32_t	reg_fpar;
	u_int		cmdnr;
	u_int		opnr;
	u_int		pg_ofs;
	enum addr_type	addr_type;
	u_int		addr_bytes;
	u_int		row_addr;
	u_int		column_addr;
	u_int		data_fir:8;
	uint32_t	fcm_endzero;
};

struct fsl_nand_softc {
	struct nand_softc		nand_dev;
	device_t			dev;
	struct resource			*res;
	int				rid;		/* Resourceid */
	struct lbc_devinfo		*dinfo;
	struct fsl_nfc_fcm		fcm;
	uint8_t				col_cycles;
	uint8_t				row_cycles;
	uint16_t			pgsz;		/* Page size */
};

static int	fsl_nand_attach(device_t dev);
static int	fsl_nand_probe(device_t dev);
static int	fsl_nand_detach(device_t dev);

static int	fsl_nfc_select_cs(device_t dev, uint8_t cs);
static int	fsl_nfc_read_rnb(device_t dev);
static int	fsl_nfc_send_command(device_t dev, uint8_t command);
static int	fsl_nfc_send_address(device_t dev, uint8_t address);
static uint8_t	fsl_nfc_read_byte(device_t dev);
static int	fsl_nfc_start_command(device_t dev);
static void	fsl_nfc_read_buf(device_t dev, void *buf, uint32_t len);
static void	fsl_nfc_write_buf(device_t dev, void *buf, uint32_t len);

static device_method_t fsl_nand_methods[] = {
	DEVMETHOD(device_probe,		fsl_nand_probe),
	DEVMETHOD(device_attach,	fsl_nand_attach),
	DEVMETHOD(device_detach,	fsl_nand_detach),

	DEVMETHOD(nfc_select_cs,	fsl_nfc_select_cs),
	DEVMETHOD(nfc_read_rnb,		fsl_nfc_read_rnb),
	DEVMETHOD(nfc_start_command,	fsl_nfc_start_command),
	DEVMETHOD(nfc_send_command,	fsl_nfc_send_command),
	DEVMETHOD(nfc_send_address,	fsl_nfc_send_address),
	DEVMETHOD(nfc_read_byte,	fsl_nfc_read_byte),
	DEVMETHOD(nfc_read_buf,		fsl_nfc_read_buf),
	DEVMETHOD(nfc_write_buf,	fsl_nfc_write_buf),
	{ 0, 0 },
};

static driver_t fsl_nand_driver = {
	"nand",
	fsl_nand_methods,
	sizeof(struct fsl_nand_softc),
};

static devclass_t fsl_nand_devclass;

DRIVER_MODULE(fsl_nand, lbc, fsl_nand_driver, fsl_nand_devclass,
    0, 0);

static int fsl_nand_build_address(device_t dev, uint32_t page, uint32_t column);
static int fsl_nand_chip_preprobe(device_t dev, struct nand_id *id);

#ifdef NAND_DEBUG_TIMING
static device_t fcm_devs[8];
#endif

#define CMD_SHIFT(cmd_num)	(24 - ((cmd_num) * 8))
#define OP_SHIFT(op_num)	(28 - ((op_num) * 4))

#define FSL_LARGE_PAGE_SIZE	(2112)
#define FSL_SMALL_PAGE_SIZE	(528)

static void
fsl_nand_init_regs(struct fsl_nand_softc *sc)
{
	uint32_t or_v, br_v;
	device_t dev;

	dev = sc->dev;

	sc->fcm.reg_fmr = (15 << FMR_CWTO_SHIFT);

	/*
	 * Setup 4 row cycles and hope that chip ignores superfluous address
	 * bytes.
	 */
	sc->fcm.reg_fmr |= (2 << FMR_AL_SHIFT);

	/* Reprogram BR(x) */
	br_v = lbc_read_reg(dev, LBC85XX_BR(sc->dinfo->di_bank));
	br_v &= 0xffff8000;
	br_v |= 1 << 11;	/* 8-bit port size */
	br_v |= 0 << 9;		/* No ECC checking and generation */
	br_v |= 1 << 5;		/* FCM machine */
	br_v |= 1;		/* Valid */
	lbc_write_reg(dev, LBC85XX_BR(sc->dinfo->di_bank), br_v);

	/* Reprogram OR(x) */
	or_v = lbc_read_reg(dev, LBC85XX_OR(sc->dinfo->di_bank));
	or_v &= 0xfffffc00;
	or_v |= 0x03AE;		/* Default POR timing */
	lbc_write_reg(dev, LBC85XX_OR(sc->dinfo->di_bank), or_v);

	if (or_v & OR_FCM_PAGESIZE) {
		sc->pgsz = FSL_LARGE_PAGE_SIZE;
		sc->col_cycles = 2;
		nand_debug(NDBG_DRV, "%s: large page NAND device at #%d",
		    device_get_nameunit(dev), sc->dinfo->di_bank);
	} else {
		sc->pgsz = FSL_SMALL_PAGE_SIZE;
		sc->col_cycles = 1;
		nand_debug(NDBG_DRV, "%s: small page NAND device at #%d",
		    device_get_nameunit(dev), sc->dinfo->di_bank);
	}
}

static int
fsl_nand_probe(device_t dev)
{

	if (!ofw_bus_is_compatible(dev, "fsl,elbc-fcm-nand"))
		return (ENXIO);

	device_set_desc(dev, "Freescale localbus FCM Controller");
	return (BUS_PROBE_DEFAULT);
}

static int
fsl_nand_attach(device_t dev)
{
	struct fsl_nand_softc *sc;
	struct nand_id id;
	struct nand_params *param;
	uint32_t num_pages;

	sc = device_get_softc(dev);
	sc->dev = dev;
	sc->dinfo = device_get_ivars(dev);

	sc->res = bus_alloc_resource_any(dev, SYS_RES_MEMORY, &sc->rid,
	    RF_ACTIVE);
	if (sc->res == NULL) {
		device_printf(dev, "could not allocate resources!\n");
		return (ENXIO);
	}

	bzero(&sc->fcm, sizeof(sc->fcm));

	/* Init register and check if HW ECC turned on */
	fsl_nand_init_regs(sc);

	/* Chip is probed, so determine number of row address cycles */
	fsl_nand_chip_preprobe(dev, &id);
	param = nand_get_params(&id);
	if (param != NULL) {
		num_pages = (param->chip_size << 20) / param->page_size;
		while(num_pages) {
			sc->row_cycles++;
			num_pages >>= 8;
		}

		sc->fcm.reg_fmr &= ~(FMR_AL);
		sc->fcm.reg_fmr |= (sc->row_cycles - 2) << FMR_AL_SHIFT;
	}

	nand_init(&sc->nand_dev, dev, NAND_ECC_SOFT, 0, 0, NULL, NULL);

#ifdef NAND_DEBUG_TIMING
	fcm_devs[sc->dinfo->di_bank] = dev;
#endif

	return (nandbus_create(dev));
}

static int
fsl_nand_detach(device_t dev)
{
	struct fsl_nand_softc *sc;

	sc = device_get_softc(dev);

	if (sc->res != NULL)
		bus_release_resource(dev, SYS_RES_MEMORY, sc->rid, sc->res);

	return (0);
}

static int
fsl_nfc_select_cs(device_t dev, uint8_t cs)
{

	// device_printf(dev, "%s(cs=%u)\n", __func__, cs);
	return ((cs > 0) ? EINVAL : 0);
}

static int
fsl_nfc_read_rnb(device_t dev)
{

	// device_printf(dev, "%s()\n", __func__);
	return (0);
}

static int
fsl_nfc_send_command(device_t dev, uint8_t command)
{
	struct fsl_nand_softc *sc;
	struct fsl_nfc_fcm *fcm;
	uint8_t	fir_op;

	// device_printf(dev, "%s(command=%u)\n", __func__, command);

	sc = device_get_softc(dev);
	fcm = &sc->fcm;

	if (command == NAND_CMD_PROG_END) {
		fcm->reg_fir |= (FIR_OP_WB << OP_SHIFT(fcm->opnr));
		fcm->opnr++;
	}
	fcm->reg_fcr |= command << CMD_SHIFT(fcm->cmdnr);
	fir_op = (fcm->cmdnr == 0) ? FIR_OP_CW0 : FIR_OP_CM(fcm->cmdnr);
	fcm->cmdnr++;

	fcm->reg_fir |= (fir_op << OP_SHIFT(fcm->opnr));
	fcm->opnr++;

	switch (command) {
	case NAND_CMD_READ_ID:
		fcm->data_fir = FIR_OP_RBW;
		fcm->addr_type = ADDR_ID;
		break;
	case NAND_CMD_SMALLOOB:
		fcm->pg_ofs += 256;
		/*FALLTHROUGH*/
	case NAND_CMD_SMALLB:
		fcm->pg_ofs += 256;
		/*FALLTHROUGH*/
	case NAND_CMD_READ: /* NAND_CMD_SMALLA */
		fcm->data_fir = FIR_OP_RBW;
		fcm->addr_type = ADDR_ROWCOL;
		break;
	case NAND_CMD_STATUS:
		fcm->data_fir = FIR_OP_RS;
		fcm->status = 1;
		break;
	case NAND_CMD_ERASE:
		fcm->addr_type = ADDR_ROW;
		break;
	case NAND_CMD_PROG:
		fcm->addr_type = ADDR_ROWCOL;
		break;
	}
	return (0);
}

static int
fsl_nfc_send_address(device_t dev, uint8_t addr)
{
	struct fsl_nand_softc *sc;
	struct fsl_nfc_fcm *fcm;
	uint32_t addr_bits;

	// device_printf(dev, "%s(address=%u)\n", __func__, addr);

	sc = device_get_softc(dev);
	fcm = &sc->fcm;

	KASSERT(fcm->addr_type != ADDR_NONE,
	    ("controller doesn't expect address cycle"));

	addr_bits = addr;

	if (fcm->addr_type == ADDR_ID) {
		fcm->reg_fir |= (FIR_OP_UA << OP_SHIFT(fcm->opnr));
		fcm->opnr++;

		fcm->reg_fbcr = 5;
		fcm->reg_fbar = 0;
		fcm->reg_fpar = 0;
		fcm->reg_mdr = addr_bits;
		fcm->buf_ofs = 0;
		fcm->read_ptr = 0;
		return (0);
	}

	if (fcm->addr_type == ADDR_ROW) {
		addr_bits <<= fcm->addr_bytes * 8;
		fcm->row_addr |= addr_bits;
		fcm->addr_bytes++;
		if (fcm->addr_bytes < sc->row_cycles)
			return (0);
	} else {
		if (fcm->addr_bytes < sc->col_cycles) {
			addr_bits <<= fcm->addr_bytes * 8;
			fcm->column_addr |= addr_bits;
		} else {
			addr_bits <<= (fcm->addr_bytes - sc->col_cycles) * 8;
			fcm->row_addr |= addr_bits;
		}
		fcm->addr_bytes++;
		if (fcm->addr_bytes < (sc->row_cycles + sc->col_cycles))
			return (0);
	}

	return (fsl_nand_build_address(dev, fcm->row_addr, fcm->column_addr));
}

static int
fsl_nand_build_address(device_t dev, uint32_t row, uint32_t column)
{
	struct fsl_nand_softc *sc;
	struct fsl_nfc_fcm *fcm;
	uint32_t byte_count = 0;
	uint32_t block_address = 0;
	uint32_t page_address = 0;

	sc = device_get_softc(dev);
	fcm = &sc->fcm;

	fcm->read_ptr = 0;
	fcm->buf_ofs = 0;

	if (fcm->addr_type == ADDR_ROWCOL) {
		fcm->reg_fir |= (FIR_OP_CA << OP_SHIFT(fcm->opnr));
		fcm->opnr++;

		column += fcm->pg_ofs;
		fcm->pg_ofs = 0;

		page_address |= column;

		if (column != 0) {
			byte_count = sc->pgsz - column;
			fcm->read_ptr = column;
		}
	}

	fcm->reg_fir |= (FIR_OP_PA << OP_SHIFT(fcm->opnr));
	fcm->opnr++;

	if (sc->pgsz == FSL_LARGE_PAGE_SIZE) {
		block_address = row >> 6;
		page_address |= ((row << FPAR_LP_PI_SHIFT) & FPAR_LP_PI);
		fcm->buf_ofs = (row & 1) * 4096;
	} else {
		block_address = row >> 5;
		page_address |= ((row << FPAR_SP_PI_SHIFT) & FPAR_SP_PI);
		fcm->buf_ofs = (row & 7) * 1024;
	}

	fcm->reg_fbcr = byte_count;
	fcm->reg_fbar = block_address;
	fcm->reg_fpar = page_address;
	return (0);
}

static int
fsl_nfc_start_command(device_t dev)
{
	struct fsl_nand_softc *sc;
	struct fsl_nfc_fcm *fcm;
	uint32_t fmr, ltesr_v;
	int error, timeout;

	// device_printf(dev, "%s()\n", __func__);

	sc = device_get_softc(dev);
	fcm = &sc->fcm;

	fmr = fcm->reg_fmr | FMR_OP;

	if (fcm->data_fir)
		fcm->reg_fir |= (fcm->data_fir << OP_SHIFT(fcm->opnr));

	LBC_WRITE(FIR, fcm->reg_fir);
	LBC_WRITE(FCR, fcm->reg_fcr);

	LBC_WRITE(FMR, fmr);

	LBC_WRITE(FBCR, fcm->reg_fbcr);
	LBC_WRITE(FBAR, fcm->reg_fbar);
	LBC_WRITE(FPAR, fcm->reg_fpar);

	if (fcm->addr_type == ADDR_ID)
		LBC_WRITE(MDR, fcm->reg_mdr);

	nand_debug(NDBG_DRV, "BEFORE:\nFMR=%#x, FIR=%#x, FCR=%#x", fmr,
	    fcm->reg_fir, fcm->reg_fcr);
	nand_debug(NDBG_DRV, "MDR=%#x, FBAR=%#x, FPAR=%#x, FBCR=%#x",
	    LBC_READ(MDR), fcm->reg_fbar, fcm->reg_fpar, fcm->reg_fbcr);

	LBC_WRITE(LSOR, sc->dinfo->di_bank);

	timeout = (cold) ? FSL_FCM_WAIT_TIMEOUT : ~0;
	error = 0;
	ltesr_v = LBC_READ(LTESR);
	while (!error && (ltesr_v & LTESR_CC) == 0) {
		if (cold) {
			DELAY(1000);
			timeout--;
			if (timeout < 0)
				error = EWOULDBLOCK;
		} else
			error = tsleep(device_get_parent(sc->dev), PRIBIO,
			    "nfcfsl", hz);
		ltesr_v = LBC_READ(LTESR);
	}
	if (error)
		nand_debug(NDBG_DRV, "Command complete wait timeout\n");

	nand_debug(NDBG_DRV, "AFTER:\nLTESR=%#x, LTEDR=%#x, LTEIR=%#x,"
	    " LTEATR=%#x, LTEAR=%#x, LTECCR=%#x", ltesr_v,
	    LBC_READ(LTEDR), LBC_READ(LTEIR), LBC_READ(LTEATR),
	    LBC_READ(LTEAR), LBC_READ(LTECCR));

	bzero(&fcm->fcm_startzero,
	    __rangeof(struct fsl_nfc_fcm, fcm_startzero, fcm_endzero));

	if (fcm->status)
		sc->fcm.reg_mdr = LBC_READ(MDR);

	/* Even if timeout occurred, we should perform steps below */
	LBC_WRITE(LTESR, ltesr_v);
	LBC_WRITE(LTEATR, 0);

	return (error);
}

static uint8_t
fsl_nfc_read_byte(device_t dev)
{
	struct fsl_nand_softc *sc = device_get_softc(dev);
	uint32_t offset;

	// device_printf(dev, "%s()\n", __func__);

	/*
	 * LBC controller allows us to read status into a MDR instead of FCM
	 * buffer. If last operation requested before read_byte() was STATUS,
	 * then return MDR instead of reading a single byte from a buffer.
	 */
	if (sc->fcm.status) {
		sc->fcm.status = 0;
		return (sc->fcm.reg_mdr);
	}

	KASSERT(sc->fcm.read_ptr < sc->pgsz,
	    ("Attempt to read beyond buffer %x %x", sc->fcm.read_ptr,
	    sc->pgsz));

	offset = sc->fcm.buf_ofs + sc->fcm.read_ptr;
	sc->fcm.read_ptr++;
	return (bus_read_1(sc->res, offset));
}

static void
fsl_nfc_read_buf(device_t dev, void *buf, uint32_t len)
{
	struct fsl_nand_softc *sc = device_get_softc(dev);
	uint32_t offset;
	int bytesleft = 0;

	// device_printf(dev, "%s(buf=%p, len=%u)\n", __func__, buf, len);

	nand_debug(NDBG_DRV, "REQUEST OF 0x%0x B (BIB=0x%0x, NTR=0x%0x)",
	    len, sc->pgsz, sc->fcm.read_ptr);

	bytesleft = MIN((unsigned int)len, sc->pgsz - sc->fcm.read_ptr);

	offset = sc->fcm.buf_ofs + sc->fcm.read_ptr;
	bus_read_region_1(sc->res, offset, buf, bytesleft);
	sc->fcm.read_ptr += bytesleft;
}

static void
fsl_nfc_write_buf(device_t dev, void *buf, uint32_t len)
{
	struct fsl_nand_softc *sc = device_get_softc(dev);
	uint32_t offset;
	int bytesleft = 0;

	// device_printf(dev, "%s(buf=%p, len=%u)\n", __func__, buf, len);

	KASSERT(len <= sc->pgsz - sc->fcm.read_ptr,
	    ("Attempt to write beyond buffer"));

	bytesleft = MIN((unsigned int)len, sc->pgsz - sc->fcm.read_ptr);

	nand_debug(NDBG_DRV, "REQUEST TO WRITE 0x%0x (BIB=0x%0x, NTR=0x%0x)",
	    bytesleft, sc->pgsz, sc->fcm.read_ptr);

	offset = sc->fcm.buf_ofs + sc->fcm.read_ptr;
	bus_write_region_1(sc->res, offset, buf, bytesleft);
	sc->fcm.read_ptr += bytesleft;
}

static int
fsl_nand_chip_preprobe(device_t dev, struct nand_id *id)
{

	if (fsl_nfc_send_command(dev, NAND_CMD_RESET) != 0)
		return (ENXIO);

	if (fsl_nfc_start_command(dev) != 0)
		return (ENXIO);

	DELAY(1000);

	if (fsl_nfc_send_command(dev, NAND_CMD_READ_ID))
		return (ENXIO);

	if (fsl_nfc_send_address(dev, 0))
		return (ENXIO);

	if (fsl_nfc_start_command(dev) != 0)
		return (ENXIO);

	DELAY(25);

	id->man_id = fsl_nfc_read_byte(dev);
	id->dev_id = fsl_nfc_read_byte(dev);

	nand_debug(NDBG_DRV, "manufacturer id: %x chip id: %x",
	    id->man_id, id->dev_id);

	return (0);
}

#ifdef NAND_DEBUG_TIMING

static SYSCTL_NODE(_debug, OID_AUTO, fcm, CTLFLAG_RD, 0, "FCM timing");

static u_int csct = 1;	/* 22:    Chip select to command time (trlx). */
SYSCTL_UINT(_debug_fcm, OID_AUTO, csct, CTLFLAG_RW, &csct, 1,
    "Chip select to command time: determines how far in advance -LCSn is "
    "asserted prior to any bus activity during a NAND Flash access handled "
    "by the FCM. This helps meet chip-select setup times for slow memories.");

static u_int cst = 1;	/* 23:    Command setup time (trlx). */
SYSCTL_UINT(_debug_fcm, OID_AUTO, cst, CTLFLAG_RW, &cst, 1,
    "Command setup time: determines the delay of -LFWE assertion relative to "
    "the command, address, or data change when the external memory access "
    "is handled by the FCM.");

static u_int cht = 1;	/* 24:    Command hold time (trlx). */
SYSCTL_UINT(_debug_fcm, OID_AUTO, cht, CTLFLAG_RW, &cht, 1,
    "Command hold time: determines the -LFWE negation prior to the command, "
    "address, or data change when the external memory access is handled by "
    "the FCM.");

static u_int scy = 2;	/* 25-27: Cycle length in bus clocks */
SYSCTL_UINT(_debug_fcm, OID_AUTO, scy, CTLFLAG_RW, &scy, 2,
    "Cycle length in bus clocks: see RM");

static u_int rst = 1;	/* 28:    Read setup time (trlx). */
SYSCTL_UINT(_debug_fcm, OID_AUTO, rst, CTLFLAG_RW, &rst, 1,
    "Read setup time: determines the delay of -LFRE assertion relative to "
    "sampling of read data when the external memory access is handled by "
    "the FCM.");

static u_int trlx = 1;	/* 29:    Timing relaxed. */
SYSCTL_UINT(_debug_fcm, OID_AUTO, trlx, CTLFLAG_RW, &trlx, 1,
    "Timing relaxed: modifies the settings of timing parameters for slow "
    "memories. See RM");

static u_int ehtr = 1;	/* 30:    Extended hold time on read accesses. */
SYSCTL_UINT(_debug_fcm, OID_AUTO, ehtr, CTLFLAG_RW, &ehtr, 1,
    "Extended hold time on read accesses: indicates with TRLX how many "
    "cycles are inserted between a read access from the current bank and "
    "the next access.");

static u_int
fsl_nand_get_timing(void)
{
	u_int timing;

	timing = ((csct & 1) << 9) | ((cst & 1) << 8) | ((cht & 1) << 7) |
	    ((scy & 7) << 4) | ((rst & 1) << 3) | ((trlx & 1) << 2) |
	    ((ehtr & 1) << 1);

	printf("nfc_fsl: timing = %u\n", timing);
	return (timing);
}

static int
fsl_sysctl_program(SYSCTL_HANDLER_ARGS)
{
	struct fsl_nand_softc *sc;
	int error, i;
	device_t dev;
	uint32_t or_v;

	error = sysctl_wire_old_buffer(req, sizeof(int));
	if (error == 0) {
		i = 0;
		error = sysctl_handle_int(oidp, &i, 0, req);
	}
	if (error != 0 || req->newptr == NULL)
		return (error);

	for (i = 0; i < 8; i++) {
		dev = fcm_devs[i];
		if (dev == NULL)
			continue;
		sc = device_get_softc(dev);

		/* Reprogram OR(x) */
		or_v = lbc_read_reg(dev, LBC85XX_OR(sc->dinfo->di_bank));
		or_v &= 0xfffffc00;
		or_v |= fsl_nand_get_timing();
		lbc_write_reg(dev, LBC85XX_OR(sc->dinfo->di_bank), or_v);
	}
	return (0);
}

SYSCTL_PROC(_debug_fcm, OID_AUTO, program, CTLTYPE_INT | CTLFLAG_RW, NULL, 0,
    fsl_sysctl_program, "I", "write to program FCM with current values");

#endif /* NAND_DEBUG_TIMING */
