/*-
 * ichsmb.c
 *
 * Author: Archie Cobbs <archie@freebsd.org>
 * Copyright (c) 2000 Whistle Communications, Inc.
 * All rights reserved.
 * 
 * Subject to the following obligations and disclaimer of warranty, use and
 * redistribution of this software, in source or object code forms, with or
 * without modifications are expressly permitted by Whistle Communications;
 * provided, however, that:
 * 1. Any and all reproductions of the source or object code must include the
 *    copyright notice above and the following disclaimer of warranties; and
 * 2. No rights are granted, in any manner or form, to use Whistle
 *    Communications, Inc. trademarks, including the mark "WHISTLE
 *    COMMUNICATIONS" on advertising, endorsements, or otherwise except as
 *    such appears in the above copyright notice or in the software.
 * 
 * THIS SOFTWARE IS BEING PROVIDED BY WHISTLE COMMUNICATIONS "AS IS", AND
 * TO THE MAXIMUM EXTENT PERMITTED BY LAW, WHISTLE COMMUNICATIONS MAKES NO
 * REPRESENTATIONS OR WARRANTIES, EXPRESS OR IMPLIED, REGARDING THIS SOFTWARE,
 * INCLUDING WITHOUT LIMITATION, ANY AND ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE, OR NON-INFRINGEMENT.
 * WHISTLE COMMUNICATIONS DOES NOT WARRANT, GUARANTEE, OR MAKE ANY
 * REPRESENTATIONS REGARDING THE USE OF, OR THE RESULTS OF THE USE OF THIS
 * SOFTWARE IN TERMS OF ITS CORRECTNESS, ACCURACY, RELIABILITY OR OTHERWISE.
 * IN NO EVENT SHALL WHISTLE COMMUNICATIONS BE LIABLE FOR ANY DAMAGES
 * RESULTING FROM OR ARISING OUT OF ANY USE OF THIS SOFTWARE, INCLUDING
 * WITHOUT LIMITATION, ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY,
 * PUNITIVE, OR CONSEQUENTIAL DAMAGES, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES, LOSS OF USE, DATA OR PROFITS, HOWEVER CAUSED AND UNDER ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF WHISTLE COMMUNICATIONS IS ADVISED OF THE POSSIBILITY
 * OF SUCH DAMAGE.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

/*
 * Support for the SMBus controller logical device which is part of the
 * Intel 81801AA (ICH) and 81801AB (ICH0) I/O controller hub chips.
 *
 * This driver assumes that the generic SMBus code will ensure that
 * at most one process at a time calls into the SMBus methods below.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/errno.h>
#include <sys/lock.h>
#include <sys/module.h>
#include <sys/mutex.h>
#include <sys/syslog.h>
#include <sys/bus.h>

#include <machine/bus.h>
#include <sys/rman.h>
#include <machine/resource.h>

#include <dev/smbus/smbconf.h>

#include <dev/ichsmb/ichsmb_var.h>
#include <dev/ichsmb/ichsmb_reg.h>

/*
 * Enable debugging by defining ICHSMB_DEBUG to a non-zero value.
 */
#define ICHSMB_DEBUG	0
#if ICHSMB_DEBUG != 0 && defined(__CC_SUPPORTS___FUNC__)
#define DBG(fmt, args...)	\
	do { printf("%s: " fmt, __func__ , ## args); } while (0)
#else
#define DBG(fmt, args...)	do { } while (0)
#endif

/*
 * Our child device driver name
 */
#define DRIVER_SMBUS	"smbus"

/*
 * Internal functions
 */
static int ichsmb_wait(sc_p sc);

/********************************************************************
		BUS-INDEPENDENT BUS METHODS
********************************************************************/

/*
 * Handle probe-time duties that are independent of the bus
 * our device lives on.
 */
int
ichsmb_probe(device_t dev)
{
	return (BUS_PROBE_DEFAULT);
}

/*
 * Handle attach-time duties that are independent of the bus
 * our device lives on.
 */
int
ichsmb_attach(device_t dev)
{
	const sc_p sc = device_get_softc(dev);
	int error;

	/* Create mutex */
	mtx_init(&sc->mutex, device_get_nameunit(dev), "ichsmb", MTX_DEF);

	/* Add child: an instance of the "smbus" device */
	if ((sc->smb = device_add_child(dev, DRIVER_SMBUS, -1)) == NULL) {
		device_printf(dev, "no \"%s\" child found\n", DRIVER_SMBUS);
		error = ENXIO;
		goto fail;
	}

	/* Clear interrupt conditions */
	bus_write_1(sc->io_res, ICH_HST_STA, 0xff);

	/* Set up interrupt handler */
	error = bus_setup_intr(dev, sc->irq_res, INTR_TYPE_MISC | INTR_MPSAFE,
	    NULL, ichsmb_device_intr, sc, &sc->irq_handle);
	if (error != 0) {
		device_printf(dev, "can't setup irq\n");
		goto fail;
	}

	/* Attach "smbus" child */
	if ((error = bus_generic_attach(dev)) != 0) {
		device_printf(dev, "failed to attach child: %d\n", error);
		goto fail;
	}

	return (0);

fail:
	mtx_destroy(&sc->mutex);
	return (error);
}

/********************************************************************
			SMBUS METHODS
********************************************************************/

int 
ichsmb_callback(device_t dev, int index, void *data)
{
	int smb_error = 0;

	DBG("index=%d how=%d\n", index, data ? *(int *)data : -1);
	switch (index) {
	case SMB_REQUEST_BUS:
		break;
	case SMB_RELEASE_BUS:
		break;
	default:
		smb_error = SMB_EABORT;	/* XXX */
		break;
	}
	DBG("smb_error=%d\n", smb_error);
	return (smb_error);
}

int
ichsmb_quick(device_t dev, u_char slave, int how)
{
	const sc_p sc = device_get_softc(dev);
	int smb_error;

	DBG("slave=0x%02x how=%d\n", slave, how);
	KASSERT(sc->ich_cmd == -1,
	    ("%s: ich_cmd=%d\n", __func__ , sc->ich_cmd));
	switch (how) {
	case SMB_QREAD:
	case SMB_QWRITE:
		mtx_lock(&sc->mutex);
		sc->ich_cmd = ICH_HST_CNT_SMB_CMD_QUICK;
		bus_write_1(sc->io_res, ICH_XMIT_SLVA,
		    slave | (how == SMB_QREAD ?
	    		ICH_XMIT_SLVA_READ : ICH_XMIT_SLVA_WRITE));
		bus_write_1(sc->io_res, ICH_HST_CNT,
		    ICH_HST_CNT_START | ICH_HST_CNT_INTREN | sc->ich_cmd);
		smb_error = ichsmb_wait(sc);
		mtx_unlock(&sc->mutex);
		break;
	default:
		smb_error = SMB_ENOTSUPP;
	}
	DBG("smb_error=%d\n", smb_error);
	return (smb_error);
}

int
ichsmb_sendb(device_t dev, u_char slave, char byte)
{
	const sc_p sc = device_get_softc(dev);
	int smb_error;

	DBG("slave=0x%02x byte=0x%02x\n", slave, (u_char)byte);
	KASSERT(sc->ich_cmd == -1,
	    ("%s: ich_cmd=%d\n", __func__ , sc->ich_cmd));
	mtx_lock(&sc->mutex);
	sc->ich_cmd = ICH_HST_CNT_SMB_CMD_BYTE;
	bus_write_1(sc->io_res, ICH_XMIT_SLVA,
	    slave | ICH_XMIT_SLVA_WRITE);
	bus_write_1(sc->io_res, ICH_HST_CMD, byte);
	bus_write_1(sc->io_res, ICH_HST_CNT,
	    ICH_HST_CNT_START | ICH_HST_CNT_INTREN | sc->ich_cmd);
	smb_error = ichsmb_wait(sc);
	mtx_unlock(&sc->mutex);
	DBG("smb_error=%d\n", smb_error);
	return (smb_error);
}

int
ichsmb_recvb(device_t dev, u_char slave, char *byte)
{
	const sc_p sc = device_get_softc(dev);
	int smb_error;

	DBG("slave=0x%02x\n", slave);
	KASSERT(sc->ich_cmd == -1,
	    ("%s: ich_cmd=%d\n", __func__ , sc->ich_cmd));
	mtx_lock(&sc->mutex);
	sc->ich_cmd = ICH_HST_CNT_SMB_CMD_BYTE;
	bus_write_1(sc->io_res, ICH_XMIT_SLVA,
	    slave | ICH_XMIT_SLVA_READ);
	bus_write_1(sc->io_res, ICH_HST_CNT,
	    ICH_HST_CNT_START | ICH_HST_CNT_INTREN | sc->ich_cmd);
	if ((smb_error = ichsmb_wait(sc)) == SMB_ENOERR)
		*byte = bus_read_1(sc->io_res, ICH_D0);
	mtx_unlock(&sc->mutex);
	DBG("smb_error=%d byte=0x%02x\n", smb_error, (u_char)*byte);
	return (smb_error);
}

int
ichsmb_writeb(device_t dev, u_char slave, char cmd, char byte)
{
	const sc_p sc = device_get_softc(dev);
	int smb_error;

	DBG("slave=0x%02x cmd=0x%02x byte=0x%02x\n",
	    slave, (u_char)cmd, (u_char)byte);
	KASSERT(sc->ich_cmd == -1,
	    ("%s: ich_cmd=%d\n", __func__ , sc->ich_cmd));
	mtx_lock(&sc->mutex);
	sc->ich_cmd = ICH_HST_CNT_SMB_CMD_BYTE_DATA;
	bus_write_1(sc->io_res, ICH_XMIT_SLVA,
	    slave | ICH_XMIT_SLVA_WRITE);
	bus_write_1(sc->io_res, ICH_HST_CMD, cmd);
	bus_write_1(sc->io_res, ICH_D0, byte);
	bus_write_1(sc->io_res, ICH_HST_CNT,
	    ICH_HST_CNT_START | ICH_HST_CNT_INTREN | sc->ich_cmd);
	smb_error = ichsmb_wait(sc);
	mtx_unlock(&sc->mutex);
	DBG("smb_error=%d\n", smb_error);
	return (smb_error);
}

int
ichsmb_writew(device_t dev, u_char slave, char cmd, short word)
{
	const sc_p sc = device_get_softc(dev);
	int smb_error;

	DBG("slave=0x%02x cmd=0x%02x word=0x%04x\n",
	    slave, (u_char)cmd, (u_int16_t)word);
	KASSERT(sc->ich_cmd == -1,
	    ("%s: ich_cmd=%d\n", __func__ , sc->ich_cmd));
	mtx_lock(&sc->mutex);
	sc->ich_cmd = ICH_HST_CNT_SMB_CMD_WORD_DATA;
	bus_write_1(sc->io_res, ICH_XMIT_SLVA,
	    slave | ICH_XMIT_SLVA_WRITE);
	bus_write_1(sc->io_res, ICH_HST_CMD, cmd);
	bus_write_1(sc->io_res, ICH_D0, word & 0xff);
	bus_write_1(sc->io_res, ICH_D1, word >> 8);
	bus_write_1(sc->io_res, ICH_HST_CNT,
	    ICH_HST_CNT_START | ICH_HST_CNT_INTREN | sc->ich_cmd);
	smb_error = ichsmb_wait(sc);
	mtx_unlock(&sc->mutex);
	DBG("smb_error=%d\n", smb_error);
	return (smb_error);
}

int
ichsmb_readb(device_t dev, u_char slave, char cmd, char *byte)
{
	const sc_p sc = device_get_softc(dev);
	int smb_error;

	DBG("slave=0x%02x cmd=0x%02x\n", slave, (u_char)cmd);
	KASSERT(sc->ich_cmd == -1,
	    ("%s: ich_cmd=%d\n", __func__ , sc->ich_cmd));
	mtx_lock(&sc->mutex);
	sc->ich_cmd = ICH_HST_CNT_SMB_CMD_BYTE_DATA;
	bus_write_1(sc->io_res, ICH_XMIT_SLVA,
	    slave | ICH_XMIT_SLVA_READ);
	bus_write_1(sc->io_res, ICH_HST_CMD, cmd);
	bus_write_1(sc->io_res, ICH_HST_CNT,
	    ICH_HST_CNT_START | ICH_HST_CNT_INTREN | sc->ich_cmd);
	if ((smb_error = ichsmb_wait(sc)) == SMB_ENOERR)
		*byte = bus_read_1(sc->io_res, ICH_D0);
	mtx_unlock(&sc->mutex);
	DBG("smb_error=%d byte=0x%02x\n", smb_error, (u_char)*byte);
	return (smb_error);
}

int
ichsmb_readw(device_t dev, u_char slave, char cmd, short *word)
{
	const sc_p sc = device_get_softc(dev);
	int smb_error;

	DBG("slave=0x%02x cmd=0x%02x\n", slave, (u_char)cmd);
	KASSERT(sc->ich_cmd == -1,
	    ("%s: ich_cmd=%d\n", __func__ , sc->ich_cmd));
	mtx_lock(&sc->mutex);
	sc->ich_cmd = ICH_HST_CNT_SMB_CMD_WORD_DATA;
	bus_write_1(sc->io_res, ICH_XMIT_SLVA,
	    slave | ICH_XMIT_SLVA_READ);
	bus_write_1(sc->io_res, ICH_HST_CMD, cmd);
	bus_write_1(sc->io_res, ICH_HST_CNT,
	    ICH_HST_CNT_START | ICH_HST_CNT_INTREN | sc->ich_cmd);
	if ((smb_error = ichsmb_wait(sc)) == SMB_ENOERR) {
		*word = (bus_read_1(sc->io_res,
			ICH_D0) & 0xff)
		  | (bus_read_1(sc->io_res,
			ICH_D1) << 8);
	}
	mtx_unlock(&sc->mutex);
	DBG("smb_error=%d word=0x%04x\n", smb_error, (u_int16_t)*word);
	return (smb_error);
}

int
ichsmb_pcall(device_t dev, u_char slave, char cmd, short sdata, short *rdata)
{
	const sc_p sc = device_get_softc(dev);
	int smb_error;

	DBG("slave=0x%02x cmd=0x%02x sdata=0x%04x\n",
	    slave, (u_char)cmd, (u_int16_t)sdata);
	KASSERT(sc->ich_cmd == -1,
	    ("%s: ich_cmd=%d\n", __func__ , sc->ich_cmd));
	mtx_lock(&sc->mutex);
	sc->ich_cmd = ICH_HST_CNT_SMB_CMD_PROC_CALL;
	bus_write_1(sc->io_res, ICH_XMIT_SLVA,
	    slave | ICH_XMIT_SLVA_WRITE);
	bus_write_1(sc->io_res, ICH_HST_CMD, cmd);
	bus_write_1(sc->io_res, ICH_D0, sdata & 0xff);
	bus_write_1(sc->io_res, ICH_D1, sdata >> 8);
	bus_write_1(sc->io_res, ICH_HST_CNT,
	    ICH_HST_CNT_START | ICH_HST_CNT_INTREN | sc->ich_cmd);
	if ((smb_error = ichsmb_wait(sc)) == SMB_ENOERR) {
		*rdata = (bus_read_1(sc->io_res,
			ICH_D0) & 0xff)
		  | (bus_read_1(sc->io_res,
			ICH_D1) << 8);
	}
	mtx_unlock(&sc->mutex);
	DBG("smb_error=%d rdata=0x%04x\n", smb_error, (u_int16_t)*rdata);
	return (smb_error);
}

int
ichsmb_bwrite(device_t dev, u_char slave, char cmd, u_char count, char *buf)
{
	const sc_p sc = device_get_softc(dev);
	int smb_error;

	DBG("slave=0x%02x cmd=0x%02x count=%d\n", slave, (u_char)cmd, count);
#if ICHSMB_DEBUG
#define DISP(ch)	(((ch) < 0x20 || (ch) >= 0x7e) ? '.' : (ch))
	{
	    u_char *p;

	    for (p = (u_char *)buf; p - (u_char *)buf < 32; p += 8) {
		DBG("%02x: %02x %02x %02x %02x %02x %02x %02x %02x"
		    "  %c%c%c%c%c%c%c%c", (p - (u_char *)buf),
		    p[0], p[1], p[2], p[3], p[4], p[5], p[6], p[7],
		    DISP(p[0]), DISP(p[1]), DISP(p[2]), DISP(p[3]), 
		    DISP(p[4]), DISP(p[5]), DISP(p[6]), DISP(p[7]));
	    }
	}
#undef DISP
#endif
	KASSERT(sc->ich_cmd == -1,
	    ("%s: ich_cmd=%d\n", __func__ , sc->ich_cmd));
	if (count < 1 || count > 32)
		return (SMB_EINVAL);
	bcopy(buf, sc->block_data, count);
	sc->block_count = count;
	sc->block_index = 1;
	sc->block_write = 1;

	mtx_lock(&sc->mutex);
	sc->ich_cmd = ICH_HST_CNT_SMB_CMD_BLOCK;
	bus_write_1(sc->io_res, ICH_XMIT_SLVA,
	    slave | ICH_XMIT_SLVA_WRITE);
	bus_write_1(sc->io_res, ICH_HST_CMD, cmd);
	bus_write_1(sc->io_res, ICH_D0, count);
	bus_write_1(sc->io_res, ICH_BLOCK_DB, buf[0]);
	bus_write_1(sc->io_res, ICH_HST_CNT,
	    ICH_HST_CNT_START | ICH_HST_CNT_INTREN | sc->ich_cmd);
	smb_error = ichsmb_wait(sc);
	mtx_unlock(&sc->mutex);
	DBG("smb_error=%d\n", smb_error);
	return (smb_error);
}

int
ichsmb_bread(device_t dev, u_char slave, char cmd, u_char *count, char *buf)
{
	const sc_p sc = device_get_softc(dev);
	int smb_error;

	DBG("slave=0x%02x cmd=0x%02x count=%d\n", slave, (u_char)cmd, count);
	KASSERT(sc->ich_cmd == -1,
	    ("%s: ich_cmd=%d\n", __func__ , sc->ich_cmd));
	if (*count < 1 || *count > 32)
		return (SMB_EINVAL);
	bzero(sc->block_data, sizeof(sc->block_data));
	sc->block_count = 0;
	sc->block_index = 0;
	sc->block_write = 0;

	mtx_lock(&sc->mutex);
	sc->ich_cmd = ICH_HST_CNT_SMB_CMD_BLOCK;
	bus_write_1(sc->io_res, ICH_XMIT_SLVA,
	    slave | ICH_XMIT_SLVA_READ);
	bus_write_1(sc->io_res, ICH_HST_CMD, cmd);
	bus_write_1(sc->io_res, ICH_D0, *count); /* XXX? */
	bus_write_1(sc->io_res, ICH_HST_CNT,
	    ICH_HST_CNT_START | ICH_HST_CNT_INTREN | sc->ich_cmd);
	if ((smb_error = ichsmb_wait(sc)) == SMB_ENOERR) {
		bcopy(sc->block_data, buf, min(sc->block_count, *count));
		*count = sc->block_count;
	}
	mtx_unlock(&sc->mutex);
	DBG("smb_error=%d\n", smb_error);
#if ICHSMB_DEBUG
#define DISP(ch)	(((ch) < 0x20 || (ch) >= 0x7e) ? '.' : (ch))
	{
	    u_char *p;

	    for (p = (u_char *)buf; p - (u_char *)buf < 32; p += 8) {
		DBG("%02x: %02x %02x %02x %02x %02x %02x %02x %02x"
		    "  %c%c%c%c%c%c%c%c", (p - (u_char *)buf),
		    p[0], p[1], p[2], p[3], p[4], p[5], p[6], p[7],
		    DISP(p[0]), DISP(p[1]), DISP(p[2]), DISP(p[3]), 
		    DISP(p[4]), DISP(p[5]), DISP(p[6]), DISP(p[7]));
	    }
	}
#undef DISP
#endif
	return (smb_error);
}

/********************************************************************
			OTHER FUNCTIONS
********************************************************************/

/*
 * This table describes what interrupts we should ever expect to
 * see after each ICH command, not including the SMBALERT interrupt.
 */
static const u_int8_t ichsmb_state_irqs[] = {
	/* quick */
	(ICH_HST_STA_BUS_ERR | ICH_HST_STA_DEV_ERR | ICH_HST_STA_INTR),
	/* byte */
	(ICH_HST_STA_BUS_ERR | ICH_HST_STA_DEV_ERR | ICH_HST_STA_INTR),
	/* byte data */
	(ICH_HST_STA_BUS_ERR | ICH_HST_STA_DEV_ERR | ICH_HST_STA_INTR),
	/* word data */
	(ICH_HST_STA_BUS_ERR | ICH_HST_STA_DEV_ERR | ICH_HST_STA_INTR),
	/* process call */
	(ICH_HST_STA_BUS_ERR | ICH_HST_STA_DEV_ERR | ICH_HST_STA_INTR),
	/* block */
	(ICH_HST_STA_BUS_ERR | ICH_HST_STA_DEV_ERR | ICH_HST_STA_INTR
	    | ICH_HST_STA_BYTE_DONE_STS),
	/* i2c read (not used) */
	(ICH_HST_STA_BUS_ERR | ICH_HST_STA_DEV_ERR | ICH_HST_STA_INTR
	    | ICH_HST_STA_BYTE_DONE_STS)
};

/*
 * Interrupt handler. This handler is bus-independent. Note that our
 * interrupt may be shared, so we must handle "false" interrupts.
 */
void
ichsmb_device_intr(void *cookie)
{
	const sc_p sc = cookie;
	const device_t dev = sc->dev;
	const int maxloops = 16;
	u_int8_t status;
	u_int8_t ok_bits;
	int cmd_index;
        int count;

	mtx_lock(&sc->mutex);
	for (count = 0; count < maxloops; count++) {

		/* Get and reset status bits */
		status = bus_read_1(sc->io_res, ICH_HST_STA);
#if ICHSMB_DEBUG
		if ((status & ~(ICH_HST_STA_INUSE_STS | ICH_HST_STA_HOST_BUSY))
		    || count > 0) {
			DBG("%d stat=0x%02x\n", count, status);
		}
#endif
		status &= ~(ICH_HST_STA_INUSE_STS | ICH_HST_STA_HOST_BUSY);
		if (status == 0)
			break;

		/* Check for unexpected interrupt */
		ok_bits = ICH_HST_STA_SMBALERT_STS;
		cmd_index = sc->ich_cmd >> 2;
		if (sc->ich_cmd != -1) {
			KASSERT(cmd_index < sizeof(ichsmb_state_irqs),
			    ("%s: ich_cmd=%d", device_get_nameunit(dev),
			    sc->ich_cmd));
			ok_bits |= ichsmb_state_irqs[cmd_index];
		}
		if ((status & ~ok_bits) != 0) {
			device_printf(dev, "irq 0x%02x during %d\n", status,
			    cmd_index);
			bus_write_1(sc->io_res,
			    ICH_HST_STA, (status & ~ok_bits));
			continue;
		}

		/* Handle SMBALERT interrupt */
		if (status & ICH_HST_STA_SMBALERT_STS) {
			static int smbalert_count = 16;
			if (smbalert_count > 0) {
				device_printf(dev, "SMBALERT# rec'd\n");
				if (--smbalert_count == 0) {
					device_printf(dev,
					    "not logging anymore\n");
				}
			}
		}

		/* Check for bus error */
		if (status & ICH_HST_STA_BUS_ERR) {
			sc->smb_error = SMB_ECOLLI;	/* XXX SMB_EBUSERR? */
			goto finished;
		}

		/* Check for device error */
		if (status & ICH_HST_STA_DEV_ERR) {
			sc->smb_error = SMB_ENOACK;	/* or SMB_ETIMEOUT? */
			goto finished;
		}

		/* Check for byte completion in block transfer */
		if (status & ICH_HST_STA_BYTE_DONE_STS) {
			if (sc->block_write) {
				if (sc->block_index < sc->block_count) {

					/* Write next byte */
					bus_write_1(sc->io_res,
					    ICH_BLOCK_DB,
					    sc->block_data[sc->block_index++]);
				}
			} else {

				/* First interrupt, get the count also */
				if (sc->block_index == 0) {
					sc->block_count = bus_read_1(
					    sc->io_res, ICH_D0);
				}

				/* Get next byte, if any */
				if (sc->block_index < sc->block_count) {

					/* Read next byte */
					sc->block_data[sc->block_index++] =
					    bus_read_1(sc->io_res,
					      ICH_BLOCK_DB);

					/* Set "LAST_BYTE" bit before reading
					   the last byte of block data */
					if (sc->block_index
					    >= sc->block_count - 1) {
						bus_write_1(sc->io_res,
						    ICH_HST_CNT,
						    ICH_HST_CNT_LAST_BYTE
							| ICH_HST_CNT_INTREN
							| sc->ich_cmd);
					}
				}
			}
		}

		/* Check command completion */
		if (status & ICH_HST_STA_INTR) {
			sc->smb_error = SMB_ENOERR;
finished:
			sc->ich_cmd = -1;
			bus_write_1(sc->io_res,
			    ICH_HST_STA, status);
			wakeup(sc);
			break;
		}

		/* Clear status bits and try again */
		bus_write_1(sc->io_res, ICH_HST_STA, status);
	}
	mtx_unlock(&sc->mutex);

	/* Too many loops? */
	if (count == maxloops) {
		device_printf(dev, "interrupt loop, status=0x%02x\n",
		    bus_read_1(sc->io_res, ICH_HST_STA));
	}
}

/*
 * Wait for command completion. Assumes mutex is held.
 * Returns an SMB_* error code.
 */
static int
ichsmb_wait(sc_p sc)
{
	const device_t dev = sc->dev;
	int error, smb_error;

	KASSERT(sc->ich_cmd != -1,
	    ("%s: ich_cmd=%d\n", __func__ , sc->ich_cmd));
	mtx_assert(&sc->mutex, MA_OWNED);
	error = msleep(sc, &sc->mutex, PZERO, "ichsmb", hz / 4);
	DBG("msleep -> %d\n", error);
	switch (error) {
	case 0:
		smb_error = sc->smb_error;
		break;
	case EWOULDBLOCK:
		device_printf(dev, "device timeout, status=0x%02x\n",
		    bus_read_1(sc->io_res, ICH_HST_STA));
		sc->ich_cmd = -1;
		smb_error = SMB_ETIMEOUT;
		break;
	default:
		smb_error = SMB_EABORT;
		break;
	}
	return (smb_error);
}

/*
 * Release resources associated with device.
 */
void
ichsmb_release_resources(sc_p sc)
{
	const device_t dev = sc->dev;

	if (sc->irq_handle != NULL) {
		bus_teardown_intr(dev, sc->irq_res, sc->irq_handle);
		sc->irq_handle = NULL;
	}
	if (sc->irq_res != NULL) {
		bus_release_resource(dev,
		    SYS_RES_IRQ, sc->irq_rid, sc->irq_res);
		sc->irq_res = NULL;
	}
	if (sc->io_res != NULL) {
		bus_release_resource(dev,
		    SYS_RES_IOPORT, sc->io_rid, sc->io_res);
		sc->io_res = NULL;
	}
}

int
ichsmb_detach(device_t dev)
{
	const sc_p sc = device_get_softc(dev);
	int error;

	error = bus_generic_detach(dev);
	if (error)
		return (error);
	device_delete_child(dev, sc->smb);
	ichsmb_release_resources(sc);
	mtx_destroy(&sc->mutex);
	
	return 0;
}

DRIVER_MODULE(smbus, ichsmb, smbus_driver, smbus_devclass, 0, 0);
