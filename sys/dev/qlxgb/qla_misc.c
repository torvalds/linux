/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2011-2013 Qlogic Corporation
 * All rights reserved.
 *
 *  Redistribution and use in source and binary forms, with or without
 *  modification, are permitted provided that the following conditions
 *  are met:
 *
 *  1. Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 *  2. Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in the
 *     documentation and/or other materials provided with the distribution.
 *
 *  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 *  AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 *  IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 *  ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 *  LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 *  CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 *  SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 *  INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 *  CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 *  ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 *  POSSIBILITY OF SUCH DAMAGE.
 */
/*
 * File : qla_misc.c
 * Author : David C Somayajulu, Qlogic Corporation, Aliso Viejo, CA 92656.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include "qla_os.h"
#include "qla_reg.h"
#include "qla_hw.h"
#include "qla_def.h"
#include "qla_reg.h"
#include "qla_inline.h"
#include "qla_glbl.h"
#include "qla_dbg.h"

/*
 * structure encapsulating the value to read/write to offchip memory
 */
typedef struct _offchip_mem_val {
        uint32_t data_lo;
        uint32_t data_hi;
        uint32_t data_ulo;
        uint32_t data_uhi;
} offchip_mem_val_t;

#define Q8_ADDR_UNDEFINED		0xFFFFFFFF

/*
 * The index to this table is Bits 20-27 of the indirect register address
 */ 
static uint32_t indirect_to_base_map[] =
	{
		Q8_ADDR_UNDEFINED,	/* 0x00 */
		0x77300000,		/* 0x01 */
		0x29500000,		/* 0x02 */
		0x2A500000,		/* 0x03 */
		Q8_ADDR_UNDEFINED,	/* 0x04 */
		0x0D000000,		/* 0x05 */
		0x1B100000,		/* 0x06 */
		0x0E600000,		/* 0x07 */
		0x0E000000,		/* 0x08 */
		0x0E100000,		/* 0x09 */
		0x0E200000,		/* 0x0A */
		0x0E300000,		/* 0x0B */
		0x42000000,		/* 0x0C */
		0x41700000,		/* 0x0D */
		0x42100000,		/* 0x0E */
		0x34B00000,		/* 0x0F */
		0x40500000,		/* 0x10 */
		0x34000000,		/* 0x11 */
		0x34100000,		/* 0x12 */
		0x34200000,		/* 0x13 */
		0x34300000,		/* 0x14 */
		0x34500000,		/* 0x15 */
		0x34400000,		/* 0x16 */
		0x3C000000,		/* 0x17 */
		0x3C100000,		/* 0x18 */
		0x3C200000,		/* 0x19 */
		0x3C300000,		/* 0x1A */
		Q8_ADDR_UNDEFINED,	/* 0x1B */
		0x3C400000,		/* 0x1C */
		0x41000000,		/* 0x1D */
		Q8_ADDR_UNDEFINED,	/* 0x1E */
		0x0D100000,		/* 0x1F */
		Q8_ADDR_UNDEFINED,	/* 0x20 */
		0x77300000,		/* 0x21 */
		0x41600000,		/* 0x22 */
		Q8_ADDR_UNDEFINED,	/* 0x23 */
		Q8_ADDR_UNDEFINED,	/* 0x24 */
		Q8_ADDR_UNDEFINED,	/* 0x25 */
		Q8_ADDR_UNDEFINED,	/* 0x26 */
		Q8_ADDR_UNDEFINED,	/* 0x27 */
		0x41700000,		/* 0x28 */
		Q8_ADDR_UNDEFINED,	/* 0x29 */
		0x08900000,		/* 0x2A */
		0x70A00000,		/* 0x2B */
		0x70B00000,		/* 0x2C */
		0x70C00000,		/* 0x2D */
		0x08D00000,		/* 0x2E */
		0x08E00000,		/* 0x2F */
		0x70F00000,		/* 0x30 */
		0x40500000,		/* 0x31 */
		0x42000000,		/* 0x32 */
		0x42100000,		/* 0x33 */
		Q8_ADDR_UNDEFINED,	/* 0x34 */
		0x08800000,		/* 0x35 */
		0x09100000,		/* 0x36 */
		0x71200000,		/* 0x37 */
		0x40600000,		/* 0x38 */
		Q8_ADDR_UNDEFINED,	/* 0x39 */
		0x71800000,		/* 0x3A */
		0x19900000,		/* 0x3B */
		0x1A900000,		/* 0x3C */
		Q8_ADDR_UNDEFINED,	/* 0x3D */
		0x34600000,		/* 0x3E */
		Q8_ADDR_UNDEFINED,	/* 0x3F */
	};

/*
 * Address Translation Table for CRB to offsets from PCI BAR0
 */
typedef struct _crb_to_pci {
	uint32_t crb_addr;
	uint32_t pci_addr;
} crb_to_pci_t;

static crb_to_pci_t crbinit_to_pciaddr[] = {
	{(0x088 << 20), (0x035 << 20)},
	{(0x089 << 20), (0x02A << 20)},
	{(0x08D << 20), (0x02E << 20)},
	{(0x08E << 20), (0x02F << 20)},
	{(0x0C6 << 20), (0x023 << 20)},
	{(0x0C7 << 20), (0x024 << 20)},
	{(0x0C8 << 20), (0x025 << 20)},
	{(0x0D0 << 20), (0x005 << 20)},
	{(0x0D1 << 20), (0x01F << 20)},
	{(0x0E0 << 20), (0x008 << 20)},
	{(0x0E1 << 20), (0x009 << 20)},
	{(0x0E2 << 20), (0x00A << 20)},
	{(0x0E3 << 20), (0x00B << 20)},
	{(0x0E6 << 20), (0x007 << 20)},
	{(0x199 << 20), (0x03B << 20)},
	{(0x1B1 << 20), (0x006 << 20)},
	{(0x295 << 20), (0x002 << 20)},
	{(0x29A << 20), (0x000 << 20)},
	{(0x2A5 << 20), (0x003 << 20)},
	{(0x340 << 20), (0x011 << 20)},
	{(0x341 << 20), (0x012 << 20)},
	{(0x342 << 20), (0x013 << 20)},
	{(0x343 << 20), (0x014 << 20)},
	{(0x344 << 20), (0x016 << 20)},
	{(0x345 << 20), (0x015 << 20)},
	{(0x3C0 << 20), (0x017 << 20)},
	{(0x3C1 << 20), (0x018 << 20)},
	{(0x3C2 << 20), (0x019 << 20)},
	{(0x3C3 << 20), (0x01A << 20)},
	{(0x3C4 << 20), (0x01C << 20)},
	{(0x3C5 << 20), (0x01B << 20)},
	{(0x405 << 20), (0x031 << 20)},
	{(0x406 << 20), (0x038 << 20)},
	{(0x410 << 20), (0x01D << 20)},
	{(0x416 << 20), (0x022 << 20)},
	{(0x417 << 20), (0x028 << 20)},
	{(0x420 << 20), (0x032 << 20)},
	{(0x421 << 20), (0x033 << 20)},
	{(0x700 << 20), (0x00C << 20)},
	{(0x701 << 20), (0x00D << 20)},
	{(0x702 << 20), (0x00E << 20)},
	{(0x703 << 20), (0x00F << 20)},
	{(0x704 << 20), (0x010 << 20)},
	{(0x70A << 20), (0x02B << 20)},
	{(0x70B << 20), (0x02C << 20)},
	{(0x70C << 20), (0x02D << 20)},
	{(0x70F << 20), (0x030 << 20)},
	{(0x718 << 20), (0x03A << 20)},
	{(0x758 << 20), (0x026 << 20)},
	{(0x759 << 20), (0x027 << 20)},
	{(0x773 << 20), (0x001 << 20)}
};
 
#define Q8_INVALID_ADDRESS	(-1)
#define Q8_ADDR_MASK		(0xFFF << 20)

typedef struct _addr_val {
	uint32_t addr;
	uint32_t value;
	uint32_t pci_addr;
	uint32_t ind_addr;
} addr_val_t;

/*
 * Name: qla_rdwr_indreg32
 * Function: Read/Write an Indirect Register
 */
int
qla_rdwr_indreg32(qla_host_t *ha, uint32_t addr, uint32_t *val, uint32_t rd)
{
	uint32_t offset;
	int count = 100;

	offset = (addr & 0xFFF00000) >> 20;

	if (offset > 0x3F) {
		device_printf(ha->pci_dev, "%s: invalid addr 0x%08x\n",
			__func__, addr);
		return -1;
	}

	offset = indirect_to_base_map[offset];
	if (offset == Q8_ADDR_UNDEFINED) {
		device_printf(ha->pci_dev, "%s: undefined map 0x%08x\n",
			__func__, addr);
		return -1;
	}

	offset = offset | (addr & 0x000F0000);

	if (qla_sem_lock(ha, Q8_SEM7_LOCK, 0, 0)) {
		device_printf(ha->pci_dev, "%s: SEM7_LOCK failed\n", __func__);
		return (-1);
	}

	WRITE_OFFSET32(ha, Q8_CRB_WINDOW_2M, offset);

	while (offset != (READ_OFFSET32(ha, Q8_CRB_WINDOW_2M))) {
		count--;
		if (!count) {
			qla_sem_unlock(ha, Q8_SEM7_UNLOCK);
			return -1;
		}

		qla_mdelay(__func__, 1);
	}

	if (rd) {
		*val = READ_OFFSET32(ha, ((addr & 0xFFFF) | 0x1E0000));
	} else {
		WRITE_OFFSET32(ha, ((addr & 0xFFFF) | 0x1E0000), *val);
	} 

	qla_sem_unlock(ha, Q8_SEM7_UNLOCK);
	return 0;
}

/*
 * Name: qla_rdwr_offchip_mem
 * Function: Read/Write OffChip Memory
 */
static int
qla_rdwr_offchip_mem(qla_host_t *ha, uint64_t addr, offchip_mem_val_t *val,
	uint32_t rd)
{
	uint32_t count = 100;
	uint32_t data;

	WRITE_OFFSET32(ha, Q8_MIU_TEST_AGT_ADDR_LO, (uint32_t)addr);
	WRITE_OFFSET32(ha, Q8_MIU_TEST_AGT_ADDR_HI, (uint32_t)(addr >> 32));

	if (!rd) {
		WRITE_OFFSET32(ha, Q8_MIU_TEST_AGT_WRDATA_LO, val->data_lo);
		WRITE_OFFSET32(ha, Q8_MIU_TEST_AGT_WRDATA_HI, val->data_hi);
		WRITE_OFFSET32(ha, Q8_MIU_TEST_AGT_WRDATA_ULO, val->data_ulo);
		WRITE_OFFSET32(ha, Q8_MIU_TEST_AGT_WRDATA_UHI, val->data_uhi);
		WRITE_OFFSET32(ha, Q8_MIU_TEST_AGT_CTRL, 0x07); /* Write */
	} else {
		WRITE_OFFSET32(ha, Q8_MIU_TEST_AGT_CTRL, 0x03); /* Read */
	}

	while (count--) {
		data = READ_OFFSET32(ha, Q8_MIU_TEST_AGT_CTRL);
		if (!(data & BIT_3)) {
			if (rd) {
				val->data_lo = READ_OFFSET32(ha, \
						Q8_MIU_TEST_AGT_RDDATA_LO);
				val->data_hi = READ_OFFSET32(ha, \
						Q8_MIU_TEST_AGT_RDDATA_HI);
				val->data_ulo = READ_OFFSET32(ha, \
						Q8_MIU_TEST_AGT_RDDATA_ULO);
				val->data_uhi = READ_OFFSET32(ha, \
						Q8_MIU_TEST_AGT_RDDATA_UHI);
			}
			return 0;
		} else 
			qla_mdelay(__func__, 1);
	}
	
	device_printf(ha->pci_dev, "%s: failed[0x%08x]\n", __func__, data);
	return (-1);
}

/*
 * Name: qla_rd_flash32
 * Function: Read Flash Memory
 */
int
qla_rd_flash32(qla_host_t *ha, uint32_t addr, uint32_t *data)
{
	uint32_t val;
	uint32_t count = 100;

	if (qla_sem_lock(ha, Q8_SEM2_LOCK, 0, 0)) {
		device_printf(ha->pci_dev, "%s: SEM2_LOCK failed\n", __func__);
		return (-1);
	}
	WRITE_OFFSET32(ha, Q8_ROM_LOCKID, 0xa5a5a5a5);

	val = addr;
	qla_rdwr_indreg32(ha, Q8_ROM_ADDRESS, &val, 0);
	val = 0;
	qla_rdwr_indreg32(ha, Q8_ROM_DUMMY_BYTE_COUNT, &val, 0);
	val = 3;
	qla_rdwr_indreg32(ha, Q8_ROM_ADDR_BYTE_COUNT, &val, 0);

	QLA_USEC_DELAY(100);

	val = ROM_OPCODE_FAST_RD;
	qla_rdwr_indreg32(ha, Q8_ROM_INSTR_OPCODE, &val, 0);

	while (!((val = READ_OFFSET32(ha, Q8_ROM_STATUS)) & BIT_1)) {
		count--;
		if (!count) {
			qla_sem_unlock(ha, Q8_SEM7_UNLOCK);
			return -1;
		}
	}

	val = 0;
	qla_rdwr_indreg32(ha, Q8_ROM_DUMMY_BYTE_COUNT, &val, 0);
	qla_rdwr_indreg32(ha, Q8_ROM_ADDR_BYTE_COUNT, &val, 0);

	QLA_USEC_DELAY(100);

	qla_rdwr_indreg32(ha, Q8_ROM_RD_DATA, data, 1);

	qla_sem_unlock(ha, Q8_SEM2_UNLOCK);
	return 0;
}

static int
qla_p3p_sem_lock2(qla_host_t *ha)
{
        if (qla_sem_lock(ha, Q8_SEM2_LOCK, 0, 0)) {
                device_printf(ha->pci_dev, "%s: SEM2_LOCK failed\n", __func__);
                return (-1);
        }
        WRITE_OFFSET32(ha, Q8_ROM_LOCKID, 0xa5a5a5a5);
        return (0);
}

/*
 * Name: qla_int_to_pci_addr_map
 * Function: Convert's Internal(CRB) Address to Indirect Address
 */
static uint32_t
qla_int_to_pci_addr_map(qla_host_t *ha, uint32_t int_addr)
{
	uint32_t crb_to_pci_table_size, i;
	uint32_t addr;

	crb_to_pci_table_size = sizeof(crbinit_to_pciaddr)/sizeof(crb_to_pci_t);
	addr = int_addr & Q8_ADDR_MASK;

	for (i = 0; i < crb_to_pci_table_size; i++) {
		if (crbinit_to_pciaddr[i].crb_addr == addr) {
			addr = (int_addr & ~Q8_ADDR_MASK) |
					crbinit_to_pciaddr[i].pci_addr;
			return (addr);
		}
	}
	return (Q8_INVALID_ADDRESS);
}

/*
 * Name: qla_filter_pci_addr
 * Function: Filter's out Indirect Addresses which are not writeable
 */
static uint32_t
qla_filter_pci_addr(qla_host_t *ha, uint32_t addr)
{
	if ((addr == Q8_INVALID_ADDRESS) ||
		(addr == 0x00112040) ||
		(addr == 0x00112048) ||
		((addr & 0xFFFF0FFF) == 0x001100C4) ||
		((addr & 0xFFFF0FFF) == 0x001100C8) ||
		((addr & 0x0FF00000) == 0x00200000) ||
		(addr == 0x022021FC) ||
		(addr == 0x0330001C) ||
		(addr == 0x03300024) ||
		(addr == 0x033000A8) ||
		(addr == 0x033000C8) ||
		(addr == 0x033000BC) ||
		((addr & 0x0FF00000) == 0x03A00000) ||
		(addr == 0x03B0001C))
		return (Q8_INVALID_ADDRESS);
	else
		return (addr); 
}

/*
 * Name: qla_crb_init
 * Function: CRB Initialization - first step in the initialization after reset
 *	Essentially reads the address/value pairs from address = 0x00 and
 *	writes the value into address in the addr/value pair.
 */
static int
qla_crb_init(qla_host_t *ha)
{
	uint32_t val = 0, sig = 0;
	uint32_t offset, count, i;
	addr_val_t *addr_val_map, *avmap;

	qla_rd_flash32(ha, 0, &sig);
	QL_DPRINT2((ha->pci_dev, "%s: val[0] = 0x%08x\n", __func__, sig));

	qla_rd_flash32(ha, 4, &val);
	QL_DPRINT2((ha->pci_dev, "%s: val[4] = 0x%08x\n", __func__, val));

	count = val >> 16;
	offset = val & 0xFFFF;
	offset = offset << 2;

	QL_DPRINT2((ha->pci_dev, "%s: [sig,val]=[0x%08x, 0x%08x] %d pairs\n",
		__func__, sig, val, count));

	addr_val_map = avmap = malloc((sizeof(addr_val_t) * count),
					M_QLA8XXXBUF, M_NOWAIT);

	if (addr_val_map == NULL) {
		device_printf(ha->pci_dev, "%s: malloc failed\n", __func__);
		return (-1);
	}
	memset(avmap, 0, (sizeof(addr_val_t) * count));

	count = count << 1;
	for (i = 0; i < count; ) {
		qla_rd_flash32(ha, (offset + (i * 4)), &avmap->value);
		i++;
		qla_rd_flash32(ha, (offset + (i * 4)), &avmap->addr);
		i++;

		avmap->pci_addr = qla_int_to_pci_addr_map(ha, avmap->addr);
		avmap->ind_addr = qla_filter_pci_addr(ha, avmap->pci_addr);

		QL_DPRINT2((ha->pci_dev,
			"%s: [0x%02x][0x%08x:0x%08x:0x%08x] 0x%08x\n",
			__func__, (i >> 1), avmap->addr, avmap->pci_addr,
			avmap->ind_addr, avmap->value));

		if (avmap->ind_addr != Q8_INVALID_ADDRESS) {
			qla_rdwr_indreg32(ha, avmap->ind_addr, &avmap->value,0);
			qla_mdelay(__func__, 1);
		}
		avmap++;
	}

	free (addr_val_map, M_QLA8XXXBUF);
	return (0);
}

/*
 * Name: qla_init_peg_regs
 * Function: Protocol Engine Register Initialization
 */
static void
qla_init_peg_regs(qla_host_t *ha)
{
	WRITE_OFFSET32(ha, Q8_PEG_D_RESET1, 0x001E);
	WRITE_OFFSET32(ha, Q8_PEG_D_RESET2, 0x0008);
	WRITE_OFFSET32(ha, Q8_PEG_I_RESET, 0x0008);
	WRITE_OFFSET32(ha, Q8_PEG_0_CLR1, 0x0000);
	WRITE_OFFSET32(ha, Q8_PEG_0_CLR2, 0x0000);
	WRITE_OFFSET32(ha, Q8_PEG_1_CLR1, 0x0000);
	WRITE_OFFSET32(ha, Q8_PEG_1_CLR2, 0x0000);
	WRITE_OFFSET32(ha, Q8_PEG_2_CLR1, 0x0000);
	WRITE_OFFSET32(ha, Q8_PEG_2_CLR2, 0x0000);
	WRITE_OFFSET32(ha, Q8_PEG_3_CLR1, 0x0000);
	WRITE_OFFSET32(ha, Q8_PEG_3_CLR2, 0x0000);
	WRITE_OFFSET32(ha, Q8_PEG_4_CLR1, 0x0000);
	WRITE_OFFSET32(ha, Q8_PEG_4_CLR2, 0x0000);
}

/*
 * Name: qla_load_fw_from_flash
 * Function: Reads the Bootloader from Flash and Loads into Offchip Memory
 */
static void
qla_load_fw_from_flash(qla_host_t *ha)
{
	uint64_t mem_off	= 0x10000;
	uint32_t flash_off	= 0x10000;
	uint32_t count;
	offchip_mem_val_t val;


	/* only bootloader needs to be loaded into memory */
	for (count = 0; count < 0x20000 ; ) {
		qla_rd_flash32(ha, flash_off, &val.data_lo);
		count = count + 4;
		flash_off = flash_off + 4;

		qla_rd_flash32(ha, flash_off, &val.data_hi);
		count = count + 4;
		flash_off = flash_off + 4;

		qla_rd_flash32(ha, flash_off, &val.data_ulo);
		count = count + 4;
		flash_off = flash_off + 4;

		qla_rd_flash32(ha, flash_off, &val.data_uhi);
		count = count + 4;
		flash_off = flash_off + 4;

		qla_rdwr_offchip_mem(ha, mem_off, &val, 0);

		mem_off = mem_off + 16;
	}
	return;
}

/*
 * Name: qla_init_from_flash
 * Function: Performs Initialization which consists of the following sequence
 *	- reset
 *	- CRB Init
 *	- Peg Init
 *	- Read the Bootloader from Flash and Load into Offchip Memory
 *	- Kick start the bootloader which loads the rest of the firmware
 *		and performs the remaining steps in the initialization process.
 */
static int
qla_init_from_flash(qla_host_t *ha)
{
	uint32_t delay = 300;
	uint32_t data;

	qla_hw_reset(ha);
	qla_mdelay(__func__, 100);

	qla_crb_init(ha);
	qla_mdelay(__func__, 10);

	qla_init_peg_regs(ha);
	qla_mdelay(__func__, 10);

	qla_load_fw_from_flash(ha);
	
	WRITE_OFFSET32(ha, Q8_CMDPEG_STATE, 0x00000000);
	WRITE_OFFSET32(ha, Q8_PEG_0_RESET, 0x00001020);
	WRITE_OFFSET32(ha, Q8_ASIC_RESET, 0x0080001E);
	qla_mdelay(__func__, 100);

	do {
		data = READ_OFFSET32(ha, Q8_CMDPEG_STATE);

		QL_DPRINT2((ha->pci_dev, "%s: func[%d] cmdpegstate 0x%08x\n",
				__func__, ha->pci_func, data));
		if (data == CMDPEG_PHAN_INIT_COMPLETE) {
			QL_DPRINT2((ha->pci_dev,
				"%s: func[%d] init complete\n",
				__func__, ha->pci_func));
			return(0);
		}
		qla_mdelay(__func__, 100);
	} while (delay--);

	device_printf(ha->pci_dev,
		"%s: func[%d] Q8_PEG_HALT_STATUS1[0x%08x] STATUS2[0x%08x]"
		" HEARTBEAT[0x%08x] RCVPEG_STATE[0x%08x]"
		" CMDPEG_STATE[0x%08x]\n",
		__func__, ha->pci_func,
		(READ_OFFSET32(ha, Q8_PEG_HALT_STATUS1)),
		(READ_OFFSET32(ha, Q8_PEG_HALT_STATUS2)),
		(READ_OFFSET32(ha, Q8_FIRMWARE_HEARTBEAT)),
		(READ_OFFSET32(ha, Q8_RCVPEG_STATE)), data);
	
	return (-1);
}

/*
 * Name: qla_init_hw
 * Function: Initializes P3+ hardware.
 */
int
qla_init_hw(qla_host_t *ha)
{
        device_t dev;
        int ret = 0;
        uint32_t val, delay = 300;

        dev = ha->pci_dev;

        QL_DPRINT1((dev, "%s: enter\n", __func__));

	qla_mdelay(__func__, 100);

	if (ha->pci_func & 0x1) {
        	while ((ha->pci_func & 0x1) && delay--) {
			val = READ_OFFSET32(ha, Q8_CMDPEG_STATE);

			if (val == CMDPEG_PHAN_INIT_COMPLETE) {
				QL_DPRINT2((dev,
					"%s: func = %d init complete\n",
					__func__, ha->pci_func));
				qla_mdelay(__func__, 100);
				goto qla_init_exit;
			}
			qla_mdelay(__func__, 100);
		}
		return (-1);
	}

	val = READ_OFFSET32(ha, Q8_CMDPEG_STATE);

	if (val != CMDPEG_PHAN_INIT_COMPLETE) {
        	ret = qla_init_from_flash(ha);
		qla_mdelay(__func__, 100);
	} else {
        	ha->fw_ver_major = READ_OFFSET32(ha, Q8_FW_VER_MAJOR);
        	ha->fw_ver_minor = READ_OFFSET32(ha, Q8_FW_VER_MINOR);
		ha->fw_ver_sub = READ_OFFSET32(ha, Q8_FW_VER_SUB);

		if (qla_rd_flash32(ha, 0x100004, &val) == 0) {

			if (((val & 0xFF) != ha->fw_ver_major) ||
				(((val >> 8) & 0xFF) != ha->fw_ver_minor) ||
				(((val >> 16) & 0xFF) != ha->fw_ver_sub)) {

        			ret = qla_init_from_flash(ha);
				qla_mdelay(__func__, 100);
			}
		}
	}

qla_init_exit:
        ha->fw_ver_major = READ_OFFSET32(ha, Q8_FW_VER_MAJOR);
        ha->fw_ver_minor = READ_OFFSET32(ha, Q8_FW_VER_MINOR);
        ha->fw_ver_sub = READ_OFFSET32(ha, Q8_FW_VER_SUB);
        ha->fw_ver_build = READ_OFFSET32(ha, Q8_FW_VER_BUILD);

        return (ret);
}

static int
qla_wait_for_flash_busy(qla_host_t *ha)
{
	uint32_t count = 100;
	uint32_t val;

	QLA_USEC_DELAY(100);

	while (count--) {
		val = READ_OFFSET32(ha, Q8_ROM_STATUS);

		if (val & BIT_1)
			return 0;
		qla_mdelay(__func__, 1);
	}
	return -1;
}

static int
qla_flash_write_enable(qla_host_t *ha)
{
	uint32_t val, rval;

	val = 0;
	qla_rdwr_indreg32(ha, Q8_ROM_ADDR_BYTE_COUNT, &val, 0);

	val = ROM_OPCODE_WR_ENABLE;
	qla_rdwr_indreg32(ha, Q8_ROM_INSTR_OPCODE, &val, 0);

	rval = qla_wait_for_flash_busy(ha);

	if (rval)
		device_printf(ha->pci_dev, "%s: failed \n", __func__);

	return (rval);
}

static int
qla_flash_unprotect(qla_host_t *ha)
{
	uint32_t val, rval;

	if (qla_flash_write_enable(ha) != 0) 
		return(-1);

	val = 0;
	qla_rdwr_indreg32(ha, Q8_ROM_WR_DATA, &val, 0);

	val = ROM_OPCODE_WR_STATUS_REG;
	qla_rdwr_indreg32(ha, Q8_ROM_INSTR_OPCODE, &val, 0);
	
	rval = qla_wait_for_flash_busy(ha);

	if (rval) {
		device_printf(ha->pci_dev, "%s: failed \n", __func__);
		return rval;
	}

	if (qla_flash_write_enable(ha) != 0) 
		return(-1);

	val = 0;
	qla_rdwr_indreg32(ha, Q8_ROM_WR_DATA, &val, 0);

	val = ROM_OPCODE_WR_STATUS_REG;
	qla_rdwr_indreg32(ha, Q8_ROM_INSTR_OPCODE, &val, 0);
	
	rval = qla_wait_for_flash_busy(ha);

	if (rval)
		device_printf(ha->pci_dev, "%s: failed \n", __func__);

	return rval;
}

static int
qla_flash_protect(qla_host_t *ha)
{
	uint32_t val, rval;

	if (qla_flash_write_enable(ha) != 0) 
		return(-1);

	val = 0x9C;
	qla_rdwr_indreg32(ha, Q8_ROM_WR_DATA, &val, 0);

	val = ROM_OPCODE_WR_STATUS_REG;
	qla_rdwr_indreg32(ha, Q8_ROM_INSTR_OPCODE, &val, 0);
	
	rval = qla_wait_for_flash_busy(ha);

	if (rval)
		device_printf(ha->pci_dev, "%s: failed \n", __func__);

	return rval;
}

static uint32_t
qla_flash_get_status(qla_host_t *ha)
{
	uint32_t count = 1000;
	uint32_t val, rval;

	while (count--) {
		val = 0;
		qla_rdwr_indreg32(ha, Q8_ROM_ADDR_BYTE_COUNT, &val, 0);
			
		val = ROM_OPCODE_RD_STATUS_REG;
		qla_rdwr_indreg32(ha, Q8_ROM_INSTR_OPCODE, &val, 0);
	
		rval = qla_wait_for_flash_busy(ha);

		if (rval == 0) {
			qla_rdwr_indreg32(ha, Q8_ROM_RD_DATA, &val, 1);

			if ((val & BIT_0) == 0)
				return (val);
		}
		qla_mdelay(__func__, 1);
	}
	return -1;
}

static int
qla_wait_for_flash_unprotect(qla_host_t *ha)
{
	uint32_t delay = 1000;

	while (delay--) {

		if (qla_flash_get_status(ha) == 0)
			return 0;

		qla_mdelay(__func__, 1);
	}

	return -1;
}

static int
qla_wait_for_flash_protect(qla_host_t *ha)
{
	uint32_t delay = 1000;

	while (delay--) {

		if (qla_flash_get_status(ha) == 0x9C)
			return 0;

		qla_mdelay(__func__, 1);
	}

	return -1;
}

static int
qla_erase_flash_sector(qla_host_t *ha, uint32_t start)
{
	uint32_t val;
	int rval;

	if (qla_flash_write_enable(ha) != 0) 
		return(-1);

        val = start;
        qla_rdwr_indreg32(ha, Q8_ROM_ADDRESS, &val, 0);

        val = 3;
        qla_rdwr_indreg32(ha, Q8_ROM_ADDR_BYTE_COUNT, &val, 0);

        val = ROM_OPCODE_SECTOR_ERASE;
        qla_rdwr_indreg32(ha, Q8_ROM_INSTR_OPCODE, &val, 0);

	rval = qla_wait_for_flash_busy(ha);

	if (rval)
		device_printf(ha->pci_dev, "%s: failed \n", __func__);
	return rval;
}

#define Q8_FLASH_SECTOR_SIZE 0x10000
int
qla_erase_flash(qla_host_t *ha, uint32_t off, uint32_t size)
{
	int rval = 0;
	uint32_t start;

	if (off & (Q8_FLASH_SECTOR_SIZE -1))
		return -1;

	if ((rval = qla_p3p_sem_lock2(ha)))
		goto qla_erase_flash_exit;

	if ((rval = qla_flash_unprotect(ha)))
		goto qla_erase_flash_unlock_exit;

	if ((rval = qla_wait_for_flash_unprotect(ha)))
		goto qla_erase_flash_unlock_exit;

	for (start = off; start < (off + size); start = start + 0x10000) {
		if (qla_erase_flash_sector(ha, start)) {
			rval = -1;
			break;
		}
	}

	rval = qla_flash_protect(ha);

qla_erase_flash_unlock_exit:
	qla_sem_unlock(ha, Q8_SEM2_UNLOCK);

qla_erase_flash_exit:
	return (rval);
}

static int
qla_flash_write32(qla_host_t *ha, uint32_t off, uint32_t data)
{
	uint32_t val;
	int rval = 0;

        val = data;
        qla_rdwr_indreg32(ha, Q8_ROM_WR_DATA, &val, 0);

        val = off;
        qla_rdwr_indreg32(ha, Q8_ROM_ADDRESS, &val, 0);

        val = 3;
        qla_rdwr_indreg32(ha, Q8_ROM_ADDR_BYTE_COUNT, &val, 0);

        val = ROM_OPCODE_PROG_PAGE;
        qla_rdwr_indreg32(ha, Q8_ROM_INSTR_OPCODE, &val, 0);

	rval = qla_wait_for_flash_busy(ha);

	if (rval)
		device_printf(ha->pci_dev, "%s: failed \n", __func__);

	return rval;
}

static int
qla_flash_wait_for_write_complete(qla_host_t *ha)
{
	uint32_t val, count = 1000;
	int rval = 0;

	while (count--) {

		val = 0;
		qla_rdwr_indreg32(ha, Q8_ROM_ADDR_BYTE_COUNT, &val, 0);

		val = ROM_OPCODE_RD_STATUS_REG;
		qla_rdwr_indreg32(ha, Q8_ROM_INSTR_OPCODE, &val, 0);

		
		rval = qla_wait_for_flash_busy(ha);

		if (rval == 0) {
			qla_rdwr_indreg32(ha, Q8_ROM_RD_DATA, &val, 1);

			if ((val & BIT_0) == 0)
				return (0);
		}
		qla_mdelay(__func__, 1);
	}
	return -1;
}

static int
qla_flash_write(qla_host_t *ha, uint32_t off, uint32_t data)
{
	if (qla_flash_write_enable(ha) != 0) 
		return(-1);

	if (qla_flash_write32(ha, off, data) != 0)
		return -1;

	if (qla_flash_wait_for_write_complete(ha))
		return -1;

	return 0;
}


static int
qla_flash_write_pattern(qla_host_t *ha, uint32_t off, uint32_t size,
	uint32_t pattern)
{
	int rval = 0;
	uint32_t start;


	if ((rval = qla_p3p_sem_lock2(ha)))
		goto qla_wr_pattern_exit;

	if ((rval = qla_flash_unprotect(ha)))
		goto qla_wr_pattern_unlock_exit;

	if ((rval = qla_wait_for_flash_unprotect(ha)))
		goto qla_wr_pattern_unlock_exit;

	for (start = off; start < (off + size); start = start + 4) {
		if (qla_flash_write(ha, start, pattern)) {
			rval = -1;
			break;
		}
	}

	rval = qla_flash_protect(ha);

	if (rval == 0)
		rval = qla_wait_for_flash_protect(ha);

qla_wr_pattern_unlock_exit:
	qla_sem_unlock(ha, Q8_SEM2_UNLOCK);

qla_wr_pattern_exit:
	return (rval);
}

static int
qla_flash_write_data(qla_host_t *ha, uint32_t off, uint32_t size,
	void *data)
{
	int rval = 0;
	uint32_t start;
	uint32_t *data32 = data;


	if ((rval = qla_p3p_sem_lock2(ha)))
		goto qla_wr_pattern_exit;

	if ((rval = qla_flash_unprotect(ha)))
		goto qla_wr_pattern_unlock_exit;

	if ((rval = qla_wait_for_flash_unprotect(ha)))
		goto qla_wr_pattern_unlock_exit;

	for (start = off; start < (off + size); start = start + 4) {
		
		if (*data32 != 0xFFFFFFFF) {
			if (qla_flash_write(ha, start, *data32)) {
				rval = -1;
				break;
			}
		}
		data32++;
	}

	rval = qla_flash_protect(ha);

	if (rval == 0)
		rval = qla_wait_for_flash_protect(ha);

qla_wr_pattern_unlock_exit:
	qla_sem_unlock(ha, Q8_SEM2_UNLOCK);

qla_wr_pattern_exit:
	return (rval);
}
 
int
qla_wr_flash_buffer(qla_host_t *ha, uint32_t off, uint32_t size, void *buf,
	uint32_t pattern)
{
	int rval = 0;
	void *data;


	if (size == 0)
		return 0;

	size = size << 2;

	if (buf == NULL) {
		rval = qla_flash_write_pattern(ha, off, size, pattern);
		return (rval);
	}

	if ((data = malloc(size, M_QLA8XXXBUF, M_NOWAIT)) == NULL) {
		device_printf(ha->pci_dev, "%s: malloc failed \n", __func__);
		rval = -1;
		goto qla_wr_flash_buffer_exit;
	}

	if ((rval = copyin(buf, data, size))) {
		device_printf(ha->pci_dev, "%s copyin failed\n", __func__);
		goto qla_wr_flash_buffer_free_exit;
	}

	rval = qla_flash_write_data(ha, off, size, data);

qla_wr_flash_buffer_free_exit:
	free(data, M_QLA8XXXBUF);

qla_wr_flash_buffer_exit:
	return (rval);
}

