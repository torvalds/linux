/***********************license start***************
 * SPDX-License-Identifier: BSD-3-Clause
 *
 *  Copyright (c) 2003-2008 Cavium Networks (support@cavium.com). All rights
 *  reserved.
 *
 *
 *  Redistribution and use in source and binary forms, with or without
 *  modification, are permitted provided that the following conditions are
 *  met:
 *
 *      * Redistributions of source code must retain the above copyright
 *        notice, this list of conditions and the following disclaimer.
 *
 *      * Redistributions in binary form must reproduce the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer in the documentation and/or other materials provided
 *        with the distribution.
 *
 *      * Neither the name of Cavium Networks nor the names of
 *        its contributors may be used to endorse or promote products
 *        derived from this software without specific prior written
 *        permission.
 *
 *  TO THE MAXIMUM EXTENT PERMITTED BY LAW, THE SOFTWARE IS PROVIDED "AS IS"
 *  AND WITH ALL FAULTS AND CAVIUM NETWORKS MAKES NO PROMISES, REPRESENTATIONS
 *  OR WARRANTIES, EITHER EXPRESS, IMPLIED, STATUTORY, OR OTHERWISE, WITH
 *  RESPECT TO THE SOFTWARE, INCLUDING ITS CONDITION, ITS CONFORMITY TO ANY
 *  REPRESENTATION OR DESCRIPTION, OR THE EXISTENCE OF ANY LATENT OR PATENT
 *  DEFECTS, AND CAVIUM SPECIFICALLY DISCLAIMS ALL IMPLIED (IF ANY) WARRANTIES
 *  OF TITLE, MERCHANTABILITY, NONINFRINGEMENT, FITNESS FOR A PARTICULAR
 *  PURPOSE, LACK OF VIRUSES, ACCURACY OR COMPLETENESS, QUIET ENJOYMENT, QUIET
 *  POSSESSION OR CORRESPONDENCE TO DESCRIPTION.  THE ENTIRE RISK ARISING OUT
 *  OF USE OR PERFORMANCE OF THE SOFTWARE LIES WITH YOU.
 *
 *
 *  For any questions regarding licensing please contact marketing@caviumnetworks.com
 *
 ***********************license end**************************************/

/*
 *  octeon_ebt3000_cf.c
 *
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/bio.h>
#include <sys/systm.h>
#include <sys/sysctl.h>
#include <sys/ata.h>
#include <sys/bus.h>
#include <sys/kernel.h>
#include <sys/module.h>
#include <sys/rman.h>
#include <sys/power.h>
#include <sys/smp.h>
#include <sys/time.h>
#include <sys/timetc.h>
#include <sys/malloc.h>

#include <geom/geom.h>

#include <machine/clock.h>
#include <machine/locore.h>
#include <machine/md_var.h>
#include <machine/cpuregs.h>

#include <mips/cavium/octeon_pcmap_regs.h>

#include <contrib/octeon-sdk/cvmx.h>

/* ATA Commands */
#define CMD_READ_SECTOR		0x20
#define CMD_WRITE_SECTOR	0x30
#define CMD_IDENTIFY		0xEC

/* The ATA Task File */
#define TF_DATA			0x00
#define TF_ERROR		0x01
#define TF_PRECOMP		0x01
#define TF_SECTOR_COUNT		0x02
#define TF_SECTOR_NUMBER	0x03
#define TF_CYL_LSB		0x04
#define TF_CYL_MSB		0x05
#define TF_DRV_HEAD		0x06
#define TF_STATUS		0x07
#define TF_COMMAND		0x07

/* Status Register */
#define STATUS_BSY		0x80	/* Drive is busy */
#define STATUS_RDY		0x40	/* Drive is ready */
#define STATUS_DF		0x20	/* Device fault */
#define STATUS_DRQ		0x08	/* Data can be transferred */

/* Miscelaneous */
#define SECTOR_SIZE		512
#define WAIT_DELAY		1000
#define NR_TRIES		1000
#define SWAP_SHORT(x)		((x << 8) | (x >> 8))
#define MODEL_STR_SIZE		40

/* Globals */
/*
 * There's three bus types supported by this driver.
 *
 * CF_8 -- Traditional PC Card IDE interface on an 8-bit wide bus.  We assume
 * the bool loader has configure attribute memory properly.  We then access
 * the device like old-school 8-bit IDE card (which is all a traditional PC Card
 * interface really is).
 * CF_16 -- Traditional PC Card IDE interface on a 16-bit wide bus.  Registers on
 * this bus are 16-bits wide too.  When accessing registers in the task file, you
 * have to do it in 16-bit chunks, and worry about masking out what you don't want
 * or ORing together the traditional 8-bit values.  We assume the bootloader does
 * the right attribute memory initialization dance.
 * CF_TRUE_IDE_8 - CF Card wired to True IDE mode.  There's no Attribute memory
 * space at all.  Instead all the traditional 8-bit registers are there, but
 * on a 16-bit bus where addr0 isn't wired.  This means we need to read/write them
 * 16-bit chunks, but only the lower 8 bits are valid.  We do not (and can not)
 * access this like CF_16 with the comingled registers.  Yet we can't access
 * this like CF_8 because of the register offset.  Except the TF_DATA register
 * appears to be full width?
 */
void	*base_addr;
int	bus_type;
#define CF_8		1	/* 8-bit bus, no offsets - PC Card */
#define CF_16		2	/* 16-bit bus, registers shared - PC Card */
#define CF_TRUE_IDE_8	3	/* 16-bit bus, only lower 8-bits, TrueIDE */
const char *const cf_type[] = {
	"impossible type",
	"CF 8-bit",
	"CF 16-bit",
	"True IDE"
};

/* Device parameters */
struct drive_param{
	union {
		char buf[SECTOR_SIZE];
		struct ata_params driveid;
	} u;

	char model[MODEL_STR_SIZE];
	uint32_t nr_sectors;
	uint16_t sector_size;
	uint16_t heads;
	uint16_t tracks;
	uint16_t sec_track;
};

/* Device softc */
struct cf_priv {
	device_t dev;
	struct drive_param drive_param;

	struct bio_queue_head cf_bq;
	struct g_geom *cf_geom;
	struct g_provider *cf_provider;

};

/* GEOM class implementation */
static g_access_t       cf_access;
static g_start_t        cf_start;
static g_ioctl_t        cf_ioctl;

struct g_class g_cf_class = {
        .name =         "CF",
        .version =      G_VERSION,
        .start =        cf_start,
        .access =       cf_access,
        .ioctl =        cf_ioctl,
};

DECLARE_GEOM_CLASS(g_cf_class, g_cf);

/* Device methods */
static int	cf_probe(device_t);
static void	cf_identify(driver_t *, device_t);
static int	cf_attach(device_t);
static void	cf_attach_geom(void *, int);

/* ATA methods */
static int	cf_cmd_identify(struct cf_priv *);
static int	cf_cmd_write(uint32_t, uint32_t, void *);
static int	cf_cmd_read(uint32_t, uint32_t, void *);
static int	cf_wait_busy(void);
static int	cf_send_cmd(uint32_t, uint8_t);

/* Miscelenous */
static void	cf_swap_ascii(unsigned char[], char[]);


/* ------------------------------------------------------------------- *
 *                      cf_access()                                    *
 * ------------------------------------------------------------------- */
static int cf_access (struct g_provider *pp, int r, int w, int e)
{
	return (0);
}


/* ------------------------------------------------------------------- *
 *                      cf_start()                                     *
 * ------------------------------------------------------------------- */
static void cf_start (struct bio *bp)
{
	struct cf_priv *cf_priv;
	int error;

	cf_priv = bp->bio_to->geom->softc;

	/*
	* Handle actual I/O requests. The request is passed down through
	* the bio struct.
	*/

	switch (bp->bio_cmd) {
	case BIO_GETATTR:
		if (g_handleattr_int(bp, "GEOM::fwsectors", cf_priv->drive_param.sec_track))
                        return;
                if (g_handleattr_int(bp, "GEOM::fwheads", cf_priv->drive_param.heads))
                        return;
                g_io_deliver(bp, ENOIOCTL);
                return;

	case BIO_READ:
		error = cf_cmd_read(bp->bio_length / cf_priv->drive_param.sector_size,
		    bp->bio_offset / cf_priv->drive_param.sector_size, bp->bio_data);
		break;
	case BIO_WRITE:
		error = cf_cmd_write(bp->bio_length / cf_priv->drive_param.sector_size,
		    bp->bio_offset/cf_priv->drive_param.sector_size, bp->bio_data);
		break;

	default:
		printf("%s: unrecognized bio_cmd %x.\n", __func__, bp->bio_cmd);
		error = ENOTSUP;
		break;
	}

	if (error != 0) {
		g_io_deliver(bp, error);
		return;
	}

	bp->bio_resid = 0;
	bp->bio_completed = bp->bio_length;
	g_io_deliver(bp, 0);
}


static int cf_ioctl (struct g_provider *pp, u_long cmd, void *data, int fflag, struct thread *td)
{
	return (0);
}


static uint8_t cf_inb_8(int port)
{
	/*
	 * Traditional 8-bit PC Card/CF bus access.
	 */
	if (bus_type == CF_8) {
		volatile uint8_t *task_file = (volatile uint8_t *)base_addr;
		return task_file[port];
	}

	/*
	 * True IDE access.  lower 8 bits on a 16-bit bus (see above).
	 */
	volatile uint16_t *task_file = (volatile uint16_t *)base_addr;
	return task_file[port] & 0xff;
}

static void cf_outb_8(int port, uint8_t val)
{
	/*
	 * Traditional 8-bit PC Card/CF bus access.
	 */
	if (bus_type == CF_8) {
		volatile uint8_t *task_file = (volatile uint8_t *)base_addr;
		task_file[port] = val;
		return;
	}

	/*
	 * True IDE access.  lower 8 bits on a 16-bit bus (see above).
	 */
	volatile uint16_t *task_file = (volatile uint16_t *)base_addr;
	task_file[port] = val & 0xff;
}

static uint8_t cf_inb_16(int port)
{
	volatile uint16_t *task_file = (volatile uint16_t *)base_addr;
	uint16_t val = task_file[port / 2];
	if (port & 1)
		return (val >> 8) & 0xff;
	return val & 0xff;
}

static uint16_t cf_inw_16(int port)
{
	volatile uint16_t *task_file = (volatile uint16_t *)base_addr;
	uint16_t val = task_file[port / 2];
	return val;
}

static void cf_outw_16(int port, uint16_t val)
{
	volatile uint16_t *task_file = (volatile uint16_t *)base_addr;
	task_file[port / 2] = val;
}

/* ------------------------------------------------------------------- *
 *                      cf_cmd_read()                                  *
 * ------------------------------------------------------------------- *
 *
 *  Read nr_sectors from the device starting from start_sector.
 */
static int cf_cmd_read (uint32_t nr_sectors, uint32_t start_sector, void *buf)
{
	unsigned long lba;
	uint32_t count;
	uint16_t *ptr_16;
	uint8_t  *ptr_8;
	int error;

	ptr_8  = (uint8_t*)buf;
	ptr_16 = (uint16_t*)buf;
	lba = start_sector; 

	while (nr_sectors--) {
		error = cf_send_cmd(lba, CMD_READ_SECTOR);
		if (error != 0) {
			printf("%s: cf_send_cmd(CMD_READ_SECTOR) failed: %d\n", __func__, error);
			return (error);
		}

		switch (bus_type)
		{
		case CF_8:
			for (count = 0; count < SECTOR_SIZE; count++) {
				*ptr_8++ = cf_inb_8(TF_DATA);
				if ((count & 0xf) == 0)
					(void)cf_inb_8(TF_STATUS);
			}
			break;
		case CF_TRUE_IDE_8:
		case CF_16:
		default:
			for (count = 0; count < SECTOR_SIZE; count+=2) {
				uint16_t temp;
				temp = cf_inw_16(TF_DATA);
				*ptr_16++ = SWAP_SHORT(temp);
				if ((count & 0xf) == 0)
					(void)cf_inb_16(TF_STATUS);
			}
			break;
		}  

		lba++;
	}
	return (0);
}


/* ------------------------------------------------------------------- *
 *                      cf_cmd_write()                                 *
 * ------------------------------------------------------------------- *
 *
 * Write nr_sectors to the device starting from start_sector.
 */
static int cf_cmd_write (uint32_t nr_sectors, uint32_t start_sector, void *buf)
{
	uint32_t lba;
	uint32_t count;
	uint16_t *ptr_16;
	uint8_t  *ptr_8;
	int error;
	
	lba = start_sector;
	ptr_8  = (uint8_t*)buf;
	ptr_16 = (uint16_t*)buf;

	while (nr_sectors--) {
		error = cf_send_cmd(lba, CMD_WRITE_SECTOR);
		if (error != 0) {
			printf("%s: cf_send_cmd(CMD_WRITE_SECTOR) failed: %d\n", __func__, error);
			return (error);
		}

		switch (bus_type)
		{
		case CF_8:
			for (count = 0; count < SECTOR_SIZE; count++) {
				cf_outb_8(TF_DATA, *ptr_8++);
				if ((count & 0xf) == 0)
					(void)cf_inb_8(TF_STATUS);
			}
			break;
		case CF_TRUE_IDE_8:
		case CF_16:
		default:
			for (count = 0; count < SECTOR_SIZE; count+=2) {
				uint16_t temp = *ptr_16++;
				cf_outw_16(TF_DATA, SWAP_SHORT(temp));
				if ((count & 0xf) == 0)
					(void)cf_inb_16(TF_STATUS);
			}
			break;
		} 

		lba++;
	}
	return (0);
}


/* ------------------------------------------------------------------- *
 *                      cf_cmd_identify()                              *
 * ------------------------------------------------------------------- *
 *
 * Read parameters and other information from the drive and store 
 * it in the drive_param structure
 *
 */
static int cf_cmd_identify(struct cf_priv *cf_priv)
{
	int count;
	int error;

	error = cf_send_cmd(0, CMD_IDENTIFY);
	if (error != 0) {
		printf("%s: identify failed: %d\n", __func__, error);
		return (error);
	}
	switch (bus_type)
	{
	case CF_8:
		for (count = 0; count < SECTOR_SIZE; count++) 
			cf_priv->drive_param.u.buf[count] = cf_inb_8(TF_DATA);
		break;
	case CF_TRUE_IDE_8:
	case CF_16:
	default:
		for (count = 0; count < SECTOR_SIZE; count += 2) {
			uint16_t temp;
			temp = cf_inw_16(TF_DATA);
				
			/* endianess will be swapped below */
			cf_priv->drive_param.u.buf[count]   = (temp & 0xff);
			cf_priv->drive_param.u.buf[count + 1] = (temp & 0xff00) >> 8;
		}
		break;
	}

	cf_swap_ascii(cf_priv->drive_param.u.driveid.model, cf_priv->drive_param.model);

	cf_priv->drive_param.sector_size =  512;   //=  SWAP_SHORT (cf_priv->drive_param.u.driveid.sector_bytes);
	cf_priv->drive_param.heads 	=  SWAP_SHORT (cf_priv->drive_param.u.driveid.current_heads);
	cf_priv->drive_param.tracks	=  SWAP_SHORT (cf_priv->drive_param.u.driveid.current_cylinders); 
	cf_priv->drive_param.sec_track   =  SWAP_SHORT (cf_priv->drive_param.u.driveid.current_sectors);
	cf_priv->drive_param.nr_sectors  = (uint32_t)SWAP_SHORT (cf_priv->drive_param.u.driveid.lba_size_1) |
	    ((uint32_t)SWAP_SHORT (cf_priv->drive_param.u.driveid.lba_size_2));
	if (bootverbose) {
		printf("    model %s\n", cf_priv->drive_param.model);
		printf("    heads %d tracks %d sec_tracks %d sectors %d\n",
			cf_priv->drive_param.heads, cf_priv->drive_param.tracks,
			cf_priv->drive_param.sec_track, cf_priv->drive_param.nr_sectors);
	}

	return (0);
}


/* ------------------------------------------------------------------- *
 *                      cf_send_cmd()                                  *
 * ------------------------------------------------------------------- *
 *
 * Send command to read/write one sector specified by lba.
 *
 */
static int cf_send_cmd (uint32_t lba, uint8_t cmd)
{
	switch (bus_type)
	{
	case CF_8:
	case CF_TRUE_IDE_8:
		while (cf_inb_8(TF_STATUS) & STATUS_BSY)
			DELAY(WAIT_DELAY);
		cf_outb_8(TF_SECTOR_COUNT, 1);
		cf_outb_8(TF_SECTOR_NUMBER, lba & 0xff);
		cf_outb_8(TF_CYL_LSB, (lba >> 8) & 0xff);
		cf_outb_8(TF_CYL_MSB, (lba >> 16) & 0xff);
		cf_outb_8(TF_DRV_HEAD, ((lba >> 24) & 0xff) | 0xe0);
		cf_outb_8(TF_COMMAND, cmd);
		break;
	case CF_16:
	default:
		while (cf_inb_16(TF_STATUS) & STATUS_BSY)
			DELAY(WAIT_DELAY);
		cf_outw_16(TF_SECTOR_COUNT, 1 | ((lba & 0xff) << 8));
		cf_outw_16(TF_CYL_LSB, ((lba >> 8) & 0xff) | (((lba >> 16) & 0xff) << 8));
		cf_outw_16(TF_DRV_HEAD, (((lba >> 24) & 0xff) | 0xe0) | (cmd << 8));
		break;
	}

	return (cf_wait_busy());
}

/* ------------------------------------------------------------------- *
 *                      cf_wait_busy()                                 *
 * ------------------------------------------------------------------- *
 *
 * Wait until the drive finishes a given command and data is
 * ready to be transferred. This is done by repeatedly checking 
 * the BSY bit of the status register. When the controller is ready for
 * data transfer, it clears the BSY bit and sets the DRQ bit.
 *
 * If the DF bit is ever set, we return error.
 *
 * This code originally spun on DRQ.  If that behavior turns out to be
 * necessary, a flag can be added or this function can be called
 * repeatedly as long as it is returning ENXIO.
 */
static int cf_wait_busy (void)
{
	uint8_t status;

	switch (bus_type)
	{
	case CF_8:
	case CF_TRUE_IDE_8:
		status = cf_inb_8(TF_STATUS);
		while ((status & STATUS_BSY) == STATUS_BSY) {
			if ((status & STATUS_DF) != 0) {
				printf("%s: device fault (status=%x)\n", __func__, status);
				return (EIO);
			}
			DELAY(WAIT_DELAY);
			status = cf_inb_8(TF_STATUS);
		}
		break;
	case CF_16:
	default:
		status = cf_inb_16(TF_STATUS);
		while ((status & STATUS_BSY) == STATUS_BSY) {
			if ((status & STATUS_DF) != 0) {
				printf("%s: device fault (status=%x)\n", __func__, status);
				return (EIO);
			}
			DELAY(WAIT_DELAY);
			status = cf_inb_16(TF_STATUS);
		}
		break;
	}

	/* DRQ is only for when read data is actually available; check BSY */
	/* Some vendors do assert DRQ, but not all. Check BSY instead. */
	if (status & STATUS_BSY) {
		printf("%s: device not ready (status=%x)\n", __func__, status);
		return (ENXIO);
	}

	return (0);
}

/* ------------------------------------------------------------------- *
 *                      cf_swap_ascii()                                *
 * ------------------------------------------------------------------- *
 *
 * The ascii string returned by the controller specifying 
 * the model of the drive is byte-swaped. This routine 
 * corrects the byte ordering.
 *
 */
static void cf_swap_ascii (unsigned char str1[], char str2[])
{
	int i;

	for(i = 0; i < MODEL_STR_SIZE; i++)
		str2[i] = str1[i ^ 1];
}


/* ------------------------------------------------------------------- *
 *                      cf_probe()                                     *
 * ------------------------------------------------------------------- */

static int cf_probe (device_t dev)
{
    	if (cvmx_sysinfo_get()->board_type == CVMX_BOARD_TYPE_SIM)
		return (ENXIO);

	if (device_get_unit(dev) != 0) {
                panic("can't attach more devices\n");
        }

        device_set_desc(dev, "Octeon Compact Flash Driver");

	return (BUS_PROBE_NOWILDCARD);
}

/* ------------------------------------------------------------------- *
 *                      cf_identify()                                  *
 * ------------------------------------------------------------------- *
 *
 * Find the bootbus region for the CF to determine 
 * 16 or 8 bit and check to see if device is 
 * inserted.
 *
 */
static void cf_identify (driver_t *drv, device_t parent)
{
	int bus_region;
	int count = 0;
	cvmx_mio_boot_reg_cfgx_t cfg;
	uint64_t phys_base;
	
    	if (cvmx_sysinfo_get()->board_type == CVMX_BOARD_TYPE_SIM)
		return;

	phys_base = cvmx_sysinfo_get()->compact_flash_common_base_addr;
	if (phys_base == 0)
		return;
	base_addr = cvmx_phys_to_ptr(phys_base);

        for (bus_region = 0; bus_region < 8; bus_region++)
        {
                cfg.u64 = cvmx_read_csr(CVMX_MIO_BOOT_REG_CFGX(bus_region));
                if (cfg.s.base == phys_base >> 16)
                {
			if (cvmx_sysinfo_get()->compact_flash_attribute_base_addr == 0)
				bus_type = CF_TRUE_IDE_8;
			else
				bus_type = (cfg.s.width) ? CF_16 : CF_8;
                        printf("Compact flash found in bootbus region %d (%s).\n", bus_region, cf_type[bus_type]);
                        break;
                }
        }

	switch (bus_type)
	{
	case CF_8:
	case CF_TRUE_IDE_8:
		/* Check if CF is inserted */
		while (cf_inb_8(TF_STATUS) & STATUS_BSY) {
			if ((count++) == NR_TRIES ) {
				printf("Compact Flash not present\n");
				return;
                	}
			DELAY(WAIT_DELAY);
        	}
		break;
	case CF_16:
	default:
		/* Check if CF is inserted */
		while (cf_inb_16(TF_STATUS) & STATUS_BSY) {
			if ((count++) == NR_TRIES ) {
				printf("Compact Flash not present\n");
				return;
                	}
			DELAY(WAIT_DELAY);
        	}
		break;
	}

	BUS_ADD_CHILD(parent, 0, "cf", 0);
}


/* ------------------------------------------------------------------- *
 *                      cf_attach_geom()                               *
 * ------------------------------------------------------------------- */

static void cf_attach_geom (void *arg, int flag)
{
	struct cf_priv *cf_priv;

	cf_priv = (struct cf_priv *) arg;
	cf_priv->cf_geom = g_new_geomf(&g_cf_class, "cf%d", device_get_unit(cf_priv->dev));
	cf_priv->cf_geom->softc = cf_priv;
	cf_priv->cf_provider = g_new_providerf(cf_priv->cf_geom, "%s",
	    cf_priv->cf_geom->name);
	cf_priv->cf_provider->sectorsize = cf_priv->drive_param.sector_size;
	cf_priv->cf_provider->mediasize = cf_priv->drive_param.nr_sectors * cf_priv->cf_provider->sectorsize;
        g_error_provider(cf_priv->cf_provider, 0);
}

/* ------------------------------------------------------------------- *
 *                      cf_attach()                                    *
 * ------------------------------------------------------------------- */

static int cf_attach (device_t dev)
{
	struct cf_priv *cf_priv;
	int error;

    	if (cvmx_sysinfo_get()->board_type == CVMX_BOARD_TYPE_SIM)
		return (ENXIO);

	cf_priv = device_get_softc(dev);
	cf_priv->dev = dev;

	error = cf_cmd_identify(cf_priv);
	if (error != 0) {
		device_printf(dev, "cf_cmd_identify failed: %d\n", error);
		return (error);
	}

	g_post_event(cf_attach_geom, cf_priv, M_WAITOK, NULL);
	bioq_init(&cf_priv->cf_bq);

        return 0;
}


static device_method_t cf_methods[] = {
        /* Device interface */
        DEVMETHOD(device_probe,         cf_probe),
        DEVMETHOD(device_identify,      cf_identify),
        DEVMETHOD(device_attach,        cf_attach),
        DEVMETHOD(device_detach,        bus_generic_detach),
        DEVMETHOD(device_shutdown,      bus_generic_shutdown),

        { 0, 0 }
};

static driver_t cf_driver = {
        "cf", 
	cf_methods, 
	sizeof(struct cf_priv)
};

static devclass_t cf_devclass;

DRIVER_MODULE(cf, nexus, cf_driver, cf_devclass, 0, 0);
