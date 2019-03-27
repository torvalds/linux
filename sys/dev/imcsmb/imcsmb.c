/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Authors: Joe Kloss; Ravi Pokala (rpokala@freebsd.org)
 *
 * Copyright (c) 2017-2018 Panasas
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
 *
 * $FreeBSD$
 */

/* A detailed description of this device is present in imcsmb_pci.c */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/module.h>
#include <sys/endian.h>
#include <sys/errno.h>
#include <sys/lock.h>
#include <sys/mutex.h>
#include <sys/syslog.h>
#include <sys/bus.h>

#include <machine/bus.h>
#include <machine/atomic.h>

#include <dev/pci/pcivar.h>
#include <dev/pci/pcireg.h>

#include <dev/smbus/smbconf.h>

#include "imcsmb_reg.h"
#include "imcsmb_var.h"

/* Device methods */
static int imcsmb_attach(device_t dev);
static int imcsmb_detach(device_t dev);
static int imcsmb_probe(device_t dev);

/* SMBus methods */
static int imcsmb_callback(device_t dev, int index, void *data);
static int imcsmb_readb(device_t dev, u_char slave, char cmd, char *byte);
static int imcsmb_readw(device_t dev, u_char slave, char cmd, short *word);
static int imcsmb_writeb(device_t dev, u_char slave, char cmd, char byte);
static int imcsmb_writew(device_t dev, u_char slave, char cmd, short word);

/* All the read/write methods wrap around this. */
static int imcsmb_transfer(device_t dev, u_char slave, char cmd, void *data,
    int word_op, int write_op);

/**
 * device_attach() method. Set up the softc, including getting the set of the
 * parent imcsmb_pci's registers that we will use. Create the smbus(4) device,
 * which any SMBus slave device drivers will connect to.
 *
 * @author rpokala
 *
 * @param[in,out] dev
 *      Device being attached.
 */
static int
imcsmb_attach(device_t dev)
{
	struct imcsmb_softc *sc;
	int rc;

	/* Initialize private state */
	sc = device_get_softc(dev);
	sc->dev = dev;
	sc->imcsmb_pci = device_get_parent(dev);
	sc->regs = device_get_ivars(dev);

	/* Create the smbus child */
	sc->smbus = device_add_child(dev, "smbus", -1);
	if (sc->smbus == NULL) {
		/* Nothing has been allocated, so there's no cleanup. */
		device_printf(dev, "Child smbus not added\n");
		rc = ENXIO;
		goto out;
	}

	/* Attach the smbus child. */
	if ((rc = bus_generic_attach(dev)) != 0) {
		device_printf(dev, "Failed to attach smbus: %d\n", rc);
	}

out:
	return (rc);
}

/**
 * device_detach() method. attach() didn't do any allocations, so all that's
 * needed here is to free up any downstream drivers and children.
 *
 * @author Joe Kloss
 *
 * @param[in] dev
 *      Device being detached.
 */
static int
imcsmb_detach(device_t dev)
{
	int rc;

	/* Detach any attached drivers */
	rc = bus_generic_detach(dev);
	if (rc == 0) {
		/* Remove all children */
		rc = device_delete_children(dev);
	}

	return (rc);
}

/**
 * device_probe() method. All the actual probing was done by the imcsmb_pci
 * parent, so just report success.
 *
 * @author Joe Kloss
 *
 * @param[in,out] dev
 *      Device being probed.
 */
static int
imcsmb_probe(device_t dev)
{

	device_set_desc(dev, "iMC SMBus controller");
	return (BUS_PROBE_DEFAULT);
}

/**
 * smbus_callback() method. Call the parent imcsmb_pci's request or release
 * function to quiesce / restart firmware tasks which might use the SMBus.
 *
 * @author rpokala
 *
 * @param[in] dev
 *      Device being requested or released.
 *
 * @param[in] index
 *      Either SMB_REQUEST_BUS or SMB_RELEASE_BUS.
 *
 * @param[in] data
 *      Tell's the rest of the SMBus subsystem to allow or disallow waiting;
 *      this driver only works with SMB_DONTWAIT.
 */
static int
imcsmb_callback(device_t dev, int index, void *data)
{
	struct imcsmb_softc *sc;
	int *how;
	int rc;

	sc = device_get_softc(dev);
	how = (int *) data;

	switch (index) {
	case SMB_REQUEST_BUS: {
		if (*how != SMB_DONTWAIT) {
			rc = EINVAL;
			goto out;
		}
		rc = imcsmb_pci_request_bus(sc->imcsmb_pci);
		break;
	}
	case SMB_RELEASE_BUS:
		imcsmb_pci_release_bus(sc->imcsmb_pci);
		rc = 0;
		break;
	default:
		rc = EINVAL;
		break;
	}

out:
	return (rc);
}

/**
 * smbus_readb() method. Thin wrapper around imcsmb_transfer().
 *
 * @author Joe Kloss
 *
 * @param[in] dev
 *
 * @param[in] slave
 *      The SMBus address of the target device.
 *
 * @param[in] cmd
 *      The SMBus command for the target device; this is the offset for SPDs,
 *      or the register number for TSODs.
 *
 * @param[out] byte
 *      The byte which was read.
 */
static int
imcsmb_readb(device_t dev, u_char slave, char cmd, char *byte)
{

	return (imcsmb_transfer(dev, slave, cmd, byte, FALSE, FALSE));
}

/**
 * smbus_readw() method. Thin wrapper around imcsmb_transfer().
 *
 * @author Joe Kloss
 *
 * @param[in] dev
 *
 * @param[in] slave
 *      The SMBus address of the target device.
 *
 * @param[in] cmd
 *      The SMBus command for the target device; this is the offset for SPDs,
 *      or the register number for TSODs.
 *
 * @param[out] word
 *      The word which was read.
 */
static int
imcsmb_readw(device_t dev, u_char slave, char cmd, short *word)
{

	return (imcsmb_transfer(dev, slave, cmd, word, TRUE, FALSE));
}

/**
 * smbus_writeb() method. Thin wrapper around imcsmb_transfer().
 *
 * @author Joe Kloss
 *
 * @param[in] dev
 *
 * @param[in] slave
 *      The SMBus address of the target device.
 *
 * @param[in] cmd
 *      The SMBus command for the target device; this is the offset for SPDs,
 *      or the register number for TSODs.
 *
 * @param[in] byte
 *      The byte to write.
 */
static int
imcsmb_writeb(device_t dev, u_char slave, char cmd, char byte)
{

	return (imcsmb_transfer(dev, slave, cmd, &byte, FALSE, TRUE));
}

/**
 * smbus_writew() method. Thin wrapper around imcsmb_transfer().
 *
 * @author Joe Kloss
 *
 * @param[in] dev
 *
 * @param[in] slave
 *      The SMBus address of the target device.
 *
 * @param[in] cmd
 *      The SMBus command for the target device; this is the offset for SPDs,
 *      or the register number for TSODs.
 *
 * @param[in] word
 *      The word to write.
 */
static int
imcsmb_writew(device_t dev, u_char slave, char cmd, short word)
{

	return (imcsmb_transfer(dev, slave, cmd, &word, TRUE, TRUE));
}

/**
 * Manipulate the PCI control registers to read data from or write data to the
 * SMBus controller.
 *
 * @author Joe Kloss, rpokala
 *
 * @param[in] dev
 *
 * @param[in] slave
 *      The SMBus address of the target device.
 *
 * @param[in] cmd
 *      The SMBus command for the target device; this is the offset for SPDs,
 *      or the register number for TSODs.
 *
 * @param[in,out] data
 *      Pointer to either the value to be written, or where to place the value
 *      which was read.
 *
 * @param[in] word_op
 *      Bool: is this a word operation?
 *
 * @param[in] write_op
 *      Bool: is this a write operation?
 */
static int
imcsmb_transfer(device_t dev, u_char slave, char cmd, void *data, int word_op,
    int write_op)
{
	struct imcsmb_softc *sc;
	int i;
	int rc;
	uint32_t cmd_val;
	uint32_t cntl_val;
	uint32_t orig_cntl_val;
	uint32_t stat_val;
	uint16_t *word;
	uint16_t lword;
	uint8_t *byte;
	uint8_t lbyte;

	sc = device_get_softc(dev);
	byte = data;
	word = data;
	lbyte = *byte;
	lword = *word;

	/* We modify the value of the control register; save the original, so
	 * we can restore it later
	 */
	orig_cntl_val = pci_read_config(sc->imcsmb_pci,
	    sc->regs->smb_cntl, 4);
	cntl_val = orig_cntl_val;

	/*
	 * Set up the SMBCNTL register
	 */

	/* [31:28] Clear the existing value of the DTI bits, then set them to
	 * the four high bits of the slave address.
	 */
	cntl_val &= ~IMCSMB_CNTL_DTI_MASK;
	cntl_val |= ((uint32_t) slave & 0xf0) << 24;

	/* [27:27] Set the CLK_OVERRIDE bit, to enable normal operation */
	cntl_val |= IMCSMB_CNTL_CLK_OVERRIDE;

	/* [26:26] Clear the WRITE_DISABLE bit; the datasheet says this isn't
	 * necessary, but empirically, it is.
	 */
	cntl_val &= ~IMCSMB_CNTL_WRITE_DISABLE_BIT;

	/* [9:9] Clear the POLL_EN bit, to stop the hardware TSOD polling. */
	cntl_val &= ~IMCSMB_CNTL_POLL_EN;

	/*
	 * Set up the SMBCMD register
	 */

	/* [31:31] Set the TRIGGER bit; when this gets written, the controller
	 * will issue the command.
	 */
	cmd_val = IMCSMB_CMD_TRIGGER_BIT;

	/* [29:29] For word operations, set the WORD_ACCESS bit. */
	if (word_op) {
		cmd_val |= IMCSMB_CMD_WORD_ACCESS;
	}

	/* [27:27] For write operations, set the WRITE bit. */
	if (write_op) {
		cmd_val |= IMCSMB_CMD_WRITE_BIT;
	}

	/* [26:24] The three non-DTI, non-R/W bits of the slave address. */
	cmd_val |= (uint32_t) ((slave & 0xe) << 23);

	/* [23:16] The command (offset in the case of an EEPROM, or register in
	 * the case of TSOD or NVDIMM controller).
	 */
	cmd_val |= (uint32_t) ((uint8_t) cmd << 16);

	/* [15:0] The data to be written for a write operation. */
	if (write_op) {
		if (word_op) {
			/* The datasheet says the controller uses different
			 * endianness for word operations on I2C vs SMBus!
			 *      I2C: [15:8] = MSB; [7:0] = LSB
			 *      SMB: [15:8] = LSB; [7:0] = MSB
			 * As a practical matter, this controller is very
			 * specifically for use with DIMMs, the SPD (and
			 * NVDIMM controllers) are only accessed as bytes,
			 * the temperature sensor is only accessed as words, and
			 * the temperature sensors are I2C. Thus, byte-swap the
			 * word.
			 */
			lword = htobe16(lword);
		} else {
			/* For byte operations, the data goes in the LSB, and
			 * the MSB is a don't care.
			 */
			lword = (uint16_t) (lbyte & 0xff);
		}
		cmd_val |= lword;
	}

	/* Write the updated value to the control register first, to disable
	 * the hardware TSOD polling.
	 */
	pci_write_config(sc->imcsmb_pci, sc->regs->smb_cntl, cntl_val, 4);

	/* Poll on the BUSY bit in the status register until clear, or timeout.
	 * We just cleared the auto-poll bit, so we need to make sure the device
	 * is idle before issuing a command. We can safely timeout after 35 ms,
	 * as this is the maximum time the SMBus spec allows for a transaction.
	 */
	for (i = 4; i != 0; i--) {
		stat_val = pci_read_config(sc->imcsmb_pci, sc->regs->smb_stat,
		    4);
		if ((stat_val & IMCSMB_STATUS_BUSY_BIT) == 0) {
			break;
		}
		pause("imcsmb", 10 * hz / 1000);
	}

	if (i == 0) {
		device_printf(sc->dev,
		    "transfer: timeout waiting for device to settle\n");
	}

	/* Now that polling has stopped, we can write the command register. This
	 * starts the SMBus command.
	 */
	pci_write_config(sc->imcsmb_pci, sc->regs->smb_cmd, cmd_val, 4);

	/* Wait for WRITE_DATA_DONE/READ_DATA_VALID to be set, or timeout and
	 * fail. We wait up to 35ms.
	 */
	for (i = 35000; i != 0; i -= 10)
	{
		DELAY(10);
		stat_val = pci_read_config(sc->imcsmb_pci, sc->regs->smb_stat,
		    4);
		/* For a write, the bits holding the data contain the data being
		 * written. You'd think that would cause the READ_DATA_VALID bit
		 * to be cleared, because the data bits no longer contain valid
		 * data from the most recent read operation. While that would be
		 * logical, that's not the case here: READ_DATA_VALID is only
		 * cleared when starting a read operation, and WRITE_DATA_DONE
		 * is only cleared when starting a write operation.
		 */
		if (write_op) {
			if ((stat_val & IMCSMB_STATUS_WRITE_DATA_DONE) != 0) {
				break;
			}
		} else {
			if ((stat_val & IMCSMB_STATUS_READ_DATA_VALID) != 0) {
				break;
			}
		}
	}
	if (i == 0) {
		rc = SMB_ETIMEOUT;
		device_printf(dev, "transfer timeout\n");
		goto out;
	}

	/* It is generally the case that this bit indicates non-ACK, but it
	 * could also indicate other bus errors. There's no way to tell the
	 * difference.
	 */
	if ((stat_val & IMCSMB_STATUS_BUS_ERROR_BIT) != 0) {
		/* While it is not documented, empirically, SPD page-change
		 * commands (writes with DTI = 0x60) always complete with the
		 * error bit set. So, ignore it in those cases.
		 */
		if ((slave & 0xf0) != 0x60) {
			rc = SMB_ENOACK;
			goto out;
		}
	}

	/* For a read operation, copy the data out */
	if (write_op == 0) {
		if (word_op) {
			/* The data is returned in bits [15:0]; as discussed
			 * above, byte-swap.
			 */
			lword = (uint16_t) (stat_val & 0xffff);
			lword = htobe16(lword);
			*word = lword;
		} else {
			/* The data is returned in bits [7:0] */
			lbyte = (uint8_t) (stat_val & 0xff);
			*byte = lbyte;
		}
	}

	/* A lack of an error is, de facto, success. */
	rc = SMB_ENOERR;

out:
	/* Restore the original value of the control register. */
	pci_write_config(sc->imcsmb_pci, sc->regs->smb_cntl, orig_cntl_val, 4);
	return (rc);
}

/* Our device class */
static devclass_t imcsmb_devclass;

/* Device methods */
static device_method_t imcsmb_methods[] = {
	/* Device interface */
	DEVMETHOD(device_attach,	imcsmb_attach),
	DEVMETHOD(device_detach,	imcsmb_detach),
	DEVMETHOD(device_probe,		imcsmb_probe),

	/* smbus methods */
	DEVMETHOD(smbus_callback,	imcsmb_callback),
	DEVMETHOD(smbus_readb,		imcsmb_readb),
	DEVMETHOD(smbus_readw,		imcsmb_readw),
	DEVMETHOD(smbus_writeb,		imcsmb_writeb),
	DEVMETHOD(smbus_writew,		imcsmb_writew),

	DEVMETHOD_END
};

static driver_t imcsmb_driver = {
	.name = "imcsmb",
	.methods = imcsmb_methods,
	.size = sizeof(struct imcsmb_softc),
};

DRIVER_MODULE(imcsmb, imcsmb_pci, imcsmb_driver, imcsmb_devclass, 0, 0);
MODULE_DEPEND(imcsmb, smbus, SMBUS_MINVER, SMBUS_PREFVER, SMBUS_MAXVER);
MODULE_VERSION(imcsmb, 1);

DRIVER_MODULE(smbus, imcsmb, smbus_driver, smbus_devclass, 0, 0);

/* vi: set ts=8 sw=4 sts=8 noet: */
