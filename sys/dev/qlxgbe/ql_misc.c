/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2013-2016 Qlogic Corporation
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
 * File : ql_misc.c
 * Author : David C Somayajulu, Qlogic Corporation, Aliso Viejo, CA 92656.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include "ql_os.h"
#include "ql_hw.h"
#include "ql_def.h"
#include "ql_inline.h"
#include "ql_glbl.h"
#include "ql_dbg.h"
#include "ql_tmplt.h"

#define	QL_FDT_OFFSET		0x3F0000
#define Q8_FLASH_SECTOR_SIZE	0x10000

static int qla_ld_fw_init(qla_host_t *ha);

/*
 * structure encapsulating the value to read/write to offchip memory
 */
typedef struct _offchip_mem_val {
        uint32_t data_lo;
        uint32_t data_hi;
        uint32_t data_ulo;
        uint32_t data_uhi;
} offchip_mem_val_t;

/*
 * Name: ql_rdwr_indreg32
 * Function: Read/Write an Indirect Register
 */
int
ql_rdwr_indreg32(qla_host_t *ha, uint32_t addr, uint32_t *val, uint32_t rd)
{
	uint32_t wnd_reg;
	uint32_t count = 100;

	wnd_reg = (Q8_CRB_WINDOW_PF0 | (ha->pci_func << 2));

	WRITE_REG32(ha, wnd_reg, addr);

	while (count--) {
		if (READ_REG32(ha, wnd_reg) == addr)
			break;
		qla_mdelay(__func__, 1);
	}
	if (!count || QL_ERR_INJECT(ha, INJCT_RDWR_INDREG_FAILURE)) {
		device_printf(ha->pci_dev, "%s: [0x%08x, 0x%08x, %d] failed\n",
			__func__, addr, *val, rd);
		QL_INITIATE_RECOVERY(ha);
		return -1;
	}

	if (rd) {
		*val = READ_REG32(ha, Q8_WILD_CARD);
	} else {
		WRITE_REG32(ha, Q8_WILD_CARD, *val);
	}

	return 0;
}

/*
 * Name: ql_rdwr_offchip_mem
 * Function: Read/Write OffChip Memory
 */
int
ql_rdwr_offchip_mem(qla_host_t *ha, uint64_t addr, q80_offchip_mem_val_t *val,
	uint32_t rd)
{
	uint32_t count = 100;
	uint32_t data, step = 0;


	if (QL_ERR_INJECT(ha, INJCT_RDWR_OFFCHIPMEM_FAILURE))
		goto exit_ql_rdwr_offchip_mem;

	data = (uint32_t)addr;
	if (ql_rdwr_indreg32(ha, Q8_MS_ADDR_LO, &data, 0)) {
		step = 1;
		goto exit_ql_rdwr_offchip_mem;
	}

	data = (uint32_t)(addr >> 32);
	if (ql_rdwr_indreg32(ha, Q8_MS_ADDR_HI, &data, 0)) {
		step = 2;
		goto exit_ql_rdwr_offchip_mem;
	}

	data = BIT_1;
	if (ql_rdwr_indreg32(ha, Q8_MS_CNTRL, &data, 0)) {
		step = 3;
		goto exit_ql_rdwr_offchip_mem;
	}

	if (!rd) {
		data = val->data_lo;
		if (ql_rdwr_indreg32(ha, Q8_MS_WR_DATA_0_31, &data, 0)) {
			step = 4;
			goto exit_ql_rdwr_offchip_mem;
		}

		data = val->data_hi;
		if (ql_rdwr_indreg32(ha, Q8_MS_WR_DATA_32_63, &data, 0)) {
			step = 5;
			goto exit_ql_rdwr_offchip_mem;
		}

		data = val->data_ulo;
		if (ql_rdwr_indreg32(ha, Q8_MS_WR_DATA_64_95, &data, 0)) {
			step = 6;
			goto exit_ql_rdwr_offchip_mem;
		}

		data = val->data_uhi;
		if (ql_rdwr_indreg32(ha, Q8_MS_WR_DATA_96_127, &data, 0)) {
			step = 7;
			goto exit_ql_rdwr_offchip_mem;
		}

		data = (BIT_2|BIT_1|BIT_0);
		if (ql_rdwr_indreg32(ha, Q8_MS_CNTRL, &data, 0)) {
			step = 7;
			goto exit_ql_rdwr_offchip_mem;
		}
	} else {
		data = (BIT_1|BIT_0);
		if (ql_rdwr_indreg32(ha, Q8_MS_CNTRL, &data, 0)) {
			step = 8;
			goto exit_ql_rdwr_offchip_mem;
		}
	}

	while (count--) {
		if (ql_rdwr_indreg32(ha, Q8_MS_CNTRL, &data, 1)) {
			step = 9;
			goto exit_ql_rdwr_offchip_mem;
		}

		if (!(data & BIT_3)) {
			if (rd) {
				if (ql_rdwr_indreg32(ha, Q8_MS_RD_DATA_0_31,
					&data, 1)) {
					step = 10;
					goto exit_ql_rdwr_offchip_mem;
				}
				val->data_lo = data;

				if (ql_rdwr_indreg32(ha, Q8_MS_RD_DATA_32_63,
					&data, 1)) {
					step = 11;
					goto exit_ql_rdwr_offchip_mem;
				}
				val->data_hi = data;

				if (ql_rdwr_indreg32(ha, Q8_MS_RD_DATA_64_95,
					&data, 1)) {
					step = 12;
					goto exit_ql_rdwr_offchip_mem;
				}
				val->data_ulo = data;

				if (ql_rdwr_indreg32(ha, Q8_MS_RD_DATA_96_127,
					&data, 1)) {
					step = 13;
					goto exit_ql_rdwr_offchip_mem;
				}
				val->data_uhi = data;
			}
			return 0;
		} else 
			qla_mdelay(__func__, 1);
	}
	
exit_ql_rdwr_offchip_mem:

	device_printf(ha->pci_dev,
		"%s: [0x%08x 0x%08x : 0x%08x 0x%08x 0x%08x 0x%08x]"
		" [%d] [%d] failed\n", __func__, (uint32_t)(addr >> 32),
		(uint32_t)(addr), val->data_lo, val->data_hi, val->data_ulo,
		val->data_uhi, rd, step);

	QL_INITIATE_RECOVERY(ha);

	return (-1);
}

/*
 * Name: ql_rd_flash32
 * Function: Read Flash Memory
 */
int
ql_rd_flash32(qla_host_t *ha, uint32_t addr, uint32_t *data)
{
	uint32_t data32;

	if (qla_sem_lock(ha, Q8_FLASH_LOCK, Q8_FLASH_LOCK_ID, 0xABCDABCD)) {
		device_printf(ha->pci_dev, "%s: Q8_FLASH_LOCK failed\n",
			__func__);
		return (-1);
	}

	data32 = addr;
	if (ql_rdwr_indreg32(ha, Q8_FLASH_DIRECT_WINDOW, &data32, 0)) {
		qla_sem_unlock(ha, Q8_FLASH_UNLOCK);
		device_printf(ha->pci_dev,
			"%s: Q8_FLASH_DIRECT_WINDOW[0x%08x] failed\n",
			__func__, data32);
		return (-1);
	}

	data32 = Q8_FLASH_DIRECT_DATA | (addr & 0xFFFF);
	if (ql_rdwr_indreg32(ha, data32, data, 1)) {
		qla_sem_unlock(ha, Q8_FLASH_UNLOCK);
		device_printf(ha->pci_dev,
			"%s: data32:data [0x%08x] failed\n",
			__func__, data32);
		return (-1);
	}

	qla_sem_unlock(ha, Q8_FLASH_UNLOCK);
	return 0;
}

static int 
qla_get_fdt(qla_host_t *ha)
{
	uint32_t data32;
	int count;
	qla_hw_t *hw;

	hw = &ha->hw;

	for (count = 0; count < sizeof(qla_flash_desc_table_t); count+=4) {
		if (ql_rd_flash32(ha, QL_FDT_OFFSET + count, 
			(uint32_t *)&hw->fdt + (count >> 2))) {
				device_printf(ha->pci_dev,
					"%s: Read QL_FDT_OFFSET + %d failed\n",
					__func__, count);
				return (-1);
		}
	}

	if (qla_sem_lock(ha, Q8_FLASH_LOCK, Q8_FLASH_LOCK_ID, 
		Q8_FDT_LOCK_MAGIC_ID)) {
		device_printf(ha->pci_dev, "%s: Q8_FLASH_LOCK failed\n",
			__func__);
		return (-1);
	}

	data32 = Q8_FDT_FLASH_ADDR_VAL;
	if (ql_rdwr_indreg32(ha, Q8_FLASH_ADDRESS, &data32, 0)) {
		qla_sem_unlock(ha, Q8_FLASH_UNLOCK);
		device_printf(ha->pci_dev,
			"%s: Write to Q8_FLASH_ADDRESS failed\n",
			__func__);
		return (-1);
	}

	data32 = Q8_FDT_FLASH_CTRL_VAL;
	if (ql_rdwr_indreg32(ha, Q8_FLASH_CONTROL, &data32, 0)) {
		qla_sem_unlock(ha, Q8_FLASH_UNLOCK);
		device_printf(ha->pci_dev,
			"%s: Write to Q8_FLASH_CONTROL failed\n",
			__func__);
		return (-1);
	}

	count = 0;

	do {
		if (count < 1000) {
			QLA_USEC_DELAY(10);
			count += 10;
		} else {
			qla_mdelay(__func__, 1);
			count += 1000;
		}

		data32 = 0;

		if (ql_rdwr_indreg32(ha, Q8_FLASH_STATUS, &data32, 1)) {
			qla_sem_unlock(ha, Q8_FLASH_UNLOCK);
			device_printf(ha->pci_dev,
				"%s: Read Q8_FLASH_STATUS failed\n",
				__func__);
			return (-1);
		}

		data32 &= 0x6;

	} while ((count < 10000) && (data32 != 0x6));

	if (data32 != 0x6) {
		qla_sem_unlock(ha, Q8_FLASH_UNLOCK);
		device_printf(ha->pci_dev,
			"%s: Poll Q8_FLASH_STATUS failed\n",
			__func__);
		return (-1);
	}

	if (ql_rdwr_indreg32(ha, Q8_FLASH_RD_DATA, &data32, 1)) {
		qla_sem_unlock(ha, Q8_FLASH_UNLOCK);
		device_printf(ha->pci_dev,
			"%s: Read Q8_FLASH_RD_DATA failed\n",
			__func__);
		return (-1);
	}

	qla_sem_unlock(ha, Q8_FLASH_UNLOCK);

	data32 &= Q8_FDT_MASK_VAL;
	if (hw->fdt.flash_manuf == data32)
		return (0);
	else
		return (-1);
}

static int
qla_flash_write_enable(qla_host_t *ha, int enable)
{
	uint32_t data32;
	int count = 0;

	data32 = Q8_WR_ENABLE_FL_ADDR | ha->hw.fdt.write_statusreg_cmd;	
	if (ql_rdwr_indreg32(ha, Q8_FLASH_ADDRESS, &data32, 0)) {
		device_printf(ha->pci_dev,
			"%s: Write to Q8_FLASH_ADDRESS failed\n",
			__func__);
		return (-1);
	}

	if (enable)
		data32 = ha->hw.fdt.write_enable_bits;
	else
		data32 = ha->hw.fdt.write_disable_bits;

	if (ql_rdwr_indreg32(ha, Q8_FLASH_WR_DATA, &data32, 0)) {
		device_printf(ha->pci_dev,
			"%s: Write to Q8_FLASH_WR_DATA failed\n",
			__func__);
		return (-1);
	}

	data32 = Q8_WR_ENABLE_FL_CTRL;
	if (ql_rdwr_indreg32(ha, Q8_FLASH_CONTROL, &data32, 0)) {
		device_printf(ha->pci_dev,
			"%s: Write to Q8_FLASH_CONTROL failed\n",
			__func__);
		return (-1);
	}

	do {
		if (count < 1000) {
			QLA_USEC_DELAY(10);
			count += 10;
		} else {
			qla_mdelay(__func__, 1);
			count += 1000;
		}

		data32 = 0;
		if (ql_rdwr_indreg32(ha, Q8_FLASH_STATUS, &data32, 1)) {
			device_printf(ha->pci_dev,
				"%s: Read Q8_FLASH_STATUS failed\n",
				__func__);
			return (-1);
		}

		data32 &= 0x6;

	} while ((count < 10000) && (data32 != 0x6));
			
	if (data32 != 0x6) {
		device_printf(ha->pci_dev,
			"%s: Poll Q8_FLASH_STATUS failed\n",
			__func__);
		return (-1);
	}

	return 0;
}

static int
qla_erase_flash_sector(qla_host_t *ha, uint32_t start)
{
	uint32_t data32;
	int count = 0;

	do {
		qla_mdelay(__func__, 1);

		data32 = 0;
		if (ql_rdwr_indreg32(ha, Q8_FLASH_STATUS, &data32, 1)) { 
			device_printf(ha->pci_dev,
				"%s: Read Q8_FLASH_STATUS failed\n",
				__func__);
			return (-1);
		}

		data32 &= 0x6;

	} while (((count++) < 1000) && (data32 != 0x6));

	if (data32 != 0x6) {
		device_printf(ha->pci_dev,
			"%s: Poll Q8_FLASH_STATUS failed\n",
			__func__);
		return (-1);
	}

	data32 = (start >> 16) & 0xFF;
	if (ql_rdwr_indreg32(ha, Q8_FLASH_WR_DATA, &data32, 0)) { 
		device_printf(ha->pci_dev,
			"%s: Write to Q8_FLASH_WR_DATA failed\n",
			__func__);
		return (-1);
	}

	data32 = Q8_ERASE_FL_ADDR_MASK | ha->hw.fdt.erase_cmd;
	if (ql_rdwr_indreg32(ha, Q8_FLASH_ADDRESS, &data32, 0)) {
		device_printf(ha->pci_dev,
			"%s: Write to Q8_FLASH_ADDRESS failed\n",
			__func__);
		return (-1);
	}

	data32 = Q8_ERASE_FL_CTRL_MASK;
	if (ql_rdwr_indreg32(ha, Q8_FLASH_CONTROL, &data32, 0)) {
		device_printf(ha->pci_dev,
			"%s: Write to Q8_FLASH_CONTROL failed\n",
			__func__);
		return (-1);
	}

	count = 0;
	do {
		qla_mdelay(__func__, 1);

		data32 = 0;
		if (ql_rdwr_indreg32(ha, Q8_FLASH_STATUS, &data32, 1)) {
			device_printf(ha->pci_dev,
				"%s: Read Q8_FLASH_STATUS failed\n",
				__func__);
			return (-1);
		}

		data32 &= 0x6;

	} while (((count++) < 1000) && (data32 != 0x6));

	if (data32 != 0x6) {
		device_printf(ha->pci_dev,
			"%s: Poll Q8_FLASH_STATUS failed\n",
			__func__);
		return (-1);
	}

	return 0;
}

int
ql_erase_flash(qla_host_t *ha, uint32_t off, uint32_t size)
{
	int rval = 0;
	uint32_t start;

	if (off & (Q8_FLASH_SECTOR_SIZE -1))
		return (-1);
		
	if (qla_sem_lock(ha, Q8_FLASH_LOCK, Q8_FLASH_LOCK_ID, 
		Q8_ERASE_LOCK_MAGIC_ID)) {
		device_printf(ha->pci_dev, "%s: Q8_FLASH_LOCK failed\n",
			__func__);
		return (-1);
	}

	if (qla_flash_write_enable(ha, 1) != 0) {
		rval = -1;
		goto ql_erase_flash_exit;
	}

	for (start = off; start < (off + size); start = start + 
		Q8_FLASH_SECTOR_SIZE) {
			if (qla_erase_flash_sector(ha, start)) {
				rval = -1;
				break;
			}
	}

	rval = qla_flash_write_enable(ha, 0);

ql_erase_flash_exit:
	qla_sem_unlock(ha, Q8_FLASH_UNLOCK);
	return (rval);
}

static int
qla_wr_flash32(qla_host_t *ha, uint32_t off, uint32_t *data)
{
	uint32_t data32;
	int count = 0;

	data32 = Q8_WR_FL_ADDR_MASK | (off >> 2);
	if (ql_rdwr_indreg32(ha, Q8_FLASH_ADDRESS, &data32, 0)) {
		device_printf(ha->pci_dev,
			"%s: Write to Q8_FLASH_ADDRESS failed\n",
			__func__);
		return (-1);
	}

	if (ql_rdwr_indreg32(ha, Q8_FLASH_WR_DATA, data, 0)) {
		device_printf(ha->pci_dev,
			"%s: Write to Q8_FLASH_WR_DATA failed\n",
			__func__);
		return (-1);
	}

	data32 = Q8_WR_FL_CTRL_MASK;
	if (ql_rdwr_indreg32(ha, Q8_FLASH_CONTROL, &data32, 0)) {
		device_printf(ha->pci_dev,
			"%s: Write to Q8_FLASH_CONTROL failed\n",
			__func__);
		return (-1);
	}

	do {
		if (count < 1000) {
			QLA_USEC_DELAY(10);
			count += 10;
		} else {
			qla_mdelay(__func__, 1);
			count += 1000;
		}

		data32 = 0;
		if (ql_rdwr_indreg32(ha, Q8_FLASH_STATUS, &data32, 1)) {
			device_printf(ha->pci_dev,
				"%s: Read Q8_FLASH_STATUS failed\n",
				__func__);
			return (-1);
		}

		data32 &= 0x6;

	} while ((count < 10000) && (data32 != 0x6)); 

	if (data32 != 0x6) {
		device_printf(ha->pci_dev,
			"%s: Poll Q8_FLASH_STATUS failed\n",
			__func__);
		return (-1);
	}

	return 0;
}

static int
qla_flash_write_data(qla_host_t *ha, uint32_t off, uint32_t size,
        void *data)
{
	int rval = 0;
	uint32_t start;
	uint32_t *data32 = data;

	if (qla_sem_lock(ha, Q8_FLASH_LOCK, Q8_FLASH_LOCK_ID, 
		Q8_WR_FL_LOCK_MAGIC_ID)) {
			device_printf(ha->pci_dev, "%s: Q8_FLASH_LOCK failed\n",
				__func__);
			rval = -1;
			goto qla_flash_write_data_exit;
	}

	if ((qla_flash_write_enable(ha, 1) != 0)) {
		device_printf(ha->pci_dev, "%s: failed\n",
			__func__);
		rval = -1;
		goto qla_flash_write_data_unlock_exit;
	}

	for (start = off; start < (off + size); start = start + 4) {
		if (*data32 != 0xFFFFFFFF) {
			if (qla_wr_flash32(ha, start, data32)) {
				rval = -1;
				break;
			}
		}
		data32++;
	}

	rval = qla_flash_write_enable(ha, 0);

qla_flash_write_data_unlock_exit:
	qla_sem_unlock(ha, Q8_FLASH_UNLOCK);

qla_flash_write_data_exit:
	return (rval);
}

int
ql_wr_flash_buffer(qla_host_t *ha, uint32_t off, uint32_t size, void *buf) 
{
	int rval = 0;
	void *data;

	if (size == 0)
		return 0;

	size = size << 2;

	if (buf == NULL) 
		return -1;
	
	if ((data = malloc(size, M_QLA83XXBUF, M_NOWAIT)) == NULL) {
		device_printf(ha->pci_dev, "%s: malloc failed \n", __func__);
		rval = -1;
		goto ql_wr_flash_buffer_exit;
	}

	if ((rval = copyin(buf, data, size))) {
		device_printf(ha->pci_dev, "%s copyin failed\n", __func__);
		goto ql_wr_flash_buffer_free_exit;
	}

	rval = qla_flash_write_data(ha, off, size, data);

ql_wr_flash_buffer_free_exit:
	free(data, M_QLA83XXBUF);

ql_wr_flash_buffer_exit:
	return (rval);
}

#ifdef QL_LDFLASH_FW
/*
 * Name: qla_load_fw_from_flash
 * Function: Reads the Bootloader from Flash and Loads into Offchip Memory
 */
static void
qla_load_fw_from_flash(qla_host_t *ha)
{
	uint32_t flash_off	= 0x10000;
	uint64_t mem_off;
	uint32_t count, mem_size;
	q80_offchip_mem_val_t val;

	mem_off = (uint64_t)(READ_REG32(ha, Q8_BOOTLD_ADDR));
	mem_size = READ_REG32(ha, Q8_BOOTLD_SIZE);

	device_printf(ha->pci_dev, "%s: [0x%08x][0x%08x]\n",
		__func__, (uint32_t)mem_off, mem_size);

	/* only bootloader needs to be loaded into memory */
	for (count = 0; count < mem_size ; ) {
		ql_rd_flash32(ha, flash_off, &val.data_lo);
		count = count + 4;
		flash_off = flash_off + 4;

		ql_rd_flash32(ha, flash_off, &val.data_hi);
		count = count + 4;
		flash_off = flash_off + 4;

		ql_rd_flash32(ha, flash_off, &val.data_ulo);
		count = count + 4;
		flash_off = flash_off + 4;

		ql_rd_flash32(ha, flash_off, &val.data_uhi);
		count = count + 4;
		flash_off = flash_off + 4;

		ql_rdwr_offchip_mem(ha, mem_off, &val, 0);

		mem_off = mem_off + 16;
	}

	return;
}
#endif /* #ifdef QL_LDFLASH_FW */

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

	qla_ld_fw_init(ha);
	
	do {
		data = READ_REG32(ha, Q8_CMDPEG_STATE);

		QL_DPRINT2(ha,
			(ha->pci_dev, "%s: func[%d] cmdpegstate 0x%08x\n",
				__func__, ha->pci_func, data));
		if (data == 0xFF01) {
			QL_DPRINT2(ha, (ha->pci_dev,
				"%s: func[%d] init complete\n",
				__func__, ha->pci_func));
			return(0);
		}
		qla_mdelay(__func__, 100);
	} while (delay--);

	return (-1);
}

/*
 * Name: ql_init_hw
 * Function: Initializes P3+ hardware.
 */
int
ql_init_hw(qla_host_t *ha)
{
        device_t dev;
        int ret = 0;
        uint32_t val, delay = 300;

        dev = ha->pci_dev;

        QL_DPRINT1(ha, (dev, "%s: enter\n", __func__));

	if (ha->pci_func & 0x1) {

        	while ((ha->pci_func & 0x1) && delay--) {

			val = READ_REG32(ha, Q8_CMDPEG_STATE);

			if (val == 0xFF01) {
				QL_DPRINT2(ha, (dev,
					"%s: func = %d init complete\n",
					__func__, ha->pci_func));
				qla_mdelay(__func__, 100);
				goto qla_init_exit;
			}
			qla_mdelay(__func__, 100);
		}
		ret = -1;
		goto ql_init_hw_exit;
	}

	
	val = READ_REG32(ha, Q8_CMDPEG_STATE);
	if (!cold || (val != 0xFF01) || ha->qla_initiate_recovery) {
        	ret = qla_init_from_flash(ha);
		qla_mdelay(__func__, 100);
	}

qla_init_exit:
        ha->fw_ver_major = READ_REG32(ha, Q8_FW_VER_MAJOR);
        ha->fw_ver_minor = READ_REG32(ha, Q8_FW_VER_MINOR);
        ha->fw_ver_sub = READ_REG32(ha, Q8_FW_VER_SUB);

	if (qla_get_fdt(ha) != 0) {
		device_printf(dev, "%s: qla_get_fdt failed\n", __func__);
	} else {
		ha->hw.flags.fdt_valid = 1;
	}

ql_init_hw_exit:

	if (ret) {
		if (ha->hw.sp_log_stop_events & Q8_SP_LOG_STOP_HW_INIT_FAILURE)
			ha->hw.sp_log_stop = -1;
	}

        return (ret);
}

void
ql_read_mac_addr(qla_host_t *ha)
{
	uint8_t *macp;
	uint32_t mac_lo;
	uint32_t mac_hi;
	uint32_t flash_off;

	flash_off = Q8_BOARD_CONFIG_OFFSET + Q8_BOARD_CONFIG_MAC0_LO +
			(ha->pci_func << 3);
	ql_rd_flash32(ha, flash_off, &mac_lo);

	flash_off += 4;
	ql_rd_flash32(ha, flash_off, &mac_hi);

	macp = (uint8_t *)&mac_lo;
	ha->hw.mac_addr[5] = macp[0];
	ha->hw.mac_addr[4] = macp[1];
	ha->hw.mac_addr[3] = macp[2];
	ha->hw.mac_addr[2] = macp[3];
 
	macp = (uint8_t *)&mac_hi;
	ha->hw.mac_addr[1] = macp[0];
	ha->hw.mac_addr[0] = macp[1];

	//device_printf(ha->pci_dev, "%s: %02x:%02x:%02x:%02x:%02x:%02x\n",
	//	__func__, ha->hw.mac_addr[0], ha->hw.mac_addr[1],
	//	ha->hw.mac_addr[2], ha->hw.mac_addr[3],
	//	ha->hw.mac_addr[4], ha->hw.mac_addr[5]);

        return;
}

/*
 * Stop/Start/Initialization Handling
 */

static uint16_t
qla_tmplt_16bit_checksum(qla_host_t *ha, uint16_t *buf, uint32_t size)
{
	uint32_t sum = 0;
	uint32_t count = size >> 1; /* size in 16 bit words */

	while (count-- > 0) 
		sum += *buf++;

	while (sum >> 16) 
		sum = (sum & 0xFFFF) + (sum >> 16);

	return (~sum);
}

static int
qla_wr_list(qla_host_t *ha, q8_ce_hdr_t *ce_hdr)
{
	q8_wrl_e_t *wr_l;
	int i;

	wr_l = (q8_wrl_e_t *)((uint8_t *)ce_hdr + sizeof (q8_ce_hdr_t));

	for (i = 0; i < ce_hdr->opcount; i++, wr_l++) {

		if (ql_rdwr_indreg32(ha, wr_l->addr, &wr_l->value, 0)) {
			device_printf(ha->pci_dev,
				"%s: [0x%08x 0x%08x] error\n", __func__,
				wr_l->addr, wr_l->value);
			return -1;
		}
		if (ce_hdr->delay_to) {
			DELAY(ce_hdr->delay_to);
		}
	}
	return 0;
}

static int
qla_rd_wr_list(qla_host_t *ha, q8_ce_hdr_t *ce_hdr)
{
	q8_rdwrl_e_t *rd_wr_l;
	uint32_t data;
	int i;

	rd_wr_l = (q8_rdwrl_e_t *)((uint8_t *)ce_hdr + sizeof (q8_ce_hdr_t));

	for (i = 0; i < ce_hdr->opcount; i++, rd_wr_l++) {

		if (ql_rdwr_indreg32(ha, rd_wr_l->rd_addr, &data, 1)) {
			device_printf(ha->pci_dev, "%s: [0x%08x] error\n",
				__func__, rd_wr_l->rd_addr);

			return -1;
		}

		if (ql_rdwr_indreg32(ha, rd_wr_l->wr_addr, &data, 0)) {
			device_printf(ha->pci_dev,
				"%s: [0x%08x 0x%08x] error\n", __func__,
				rd_wr_l->wr_addr, data);
			return -1;
		}
		if (ce_hdr->delay_to) {
			DELAY(ce_hdr->delay_to);
		}
	}
	return 0;
}

static int
qla_poll_reg(qla_host_t *ha, uint32_t addr, uint32_t ms_to, uint32_t tmask,
	uint32_t tvalue)
{
	uint32_t data;

	while (ms_to) {

		if (ql_rdwr_indreg32(ha, addr, &data, 1)) {
			device_printf(ha->pci_dev, "%s: [0x%08x] error\n",
				__func__, addr);
			return -1;
		}

		if ((data & tmask) != tvalue) {
			ms_to--;
		} else 
			break;

		qla_mdelay(__func__, 1);
	}
	return ((ms_to ? 0: -1));
}

static int
qla_poll_list(qla_host_t *ha, q8_ce_hdr_t *ce_hdr)
{
	int		i;
	q8_poll_hdr_t	*phdr;
	q8_poll_e_t	*pe;
	uint32_t	data;

	phdr = (q8_poll_hdr_t *)((uint8_t *)ce_hdr + sizeof (q8_ce_hdr_t));
	pe = (q8_poll_e_t *)((uint8_t *)phdr + sizeof(q8_poll_hdr_t));

	for (i = 0; i < ce_hdr->opcount; i++, pe++) {
		if (ql_rdwr_indreg32(ha, pe->addr, &data, 1)) {
			device_printf(ha->pci_dev, "%s: [0x%08x] error\n",
				__func__, pe->addr);
			return -1;
		}

		if (ce_hdr->delay_to)  {
			if ((data & phdr->tmask) == phdr->tvalue)
				break;
			if (qla_poll_reg(ha, pe->addr, ce_hdr->delay_to,
				phdr->tmask, phdr->tvalue)) {

				if (ql_rdwr_indreg32(ha, pe->to_addr, &data,
					1)) {
					device_printf(ha->pci_dev,
						"%s: [0x%08x] error\n",
						__func__, pe->to_addr);
					return -1;
				}

				if (ql_rdwr_indreg32(ha, pe->addr, &data, 1)) {
					device_printf(ha->pci_dev,
						"%s: [0x%08x] error\n",
						__func__, pe->addr);
					return -1;
				}
			}
		}
	}
	return 0;
}

static int
qla_poll_write_list(qla_host_t *ha, q8_ce_hdr_t *ce_hdr)
{
	int		i;
	q8_poll_hdr_t	*phdr;
	q8_poll_wr_e_t	*wr_e;

	phdr = (q8_poll_hdr_t *)((uint8_t *)ce_hdr + sizeof (q8_ce_hdr_t));
	wr_e = (q8_poll_wr_e_t *)((uint8_t *)phdr + sizeof(q8_poll_hdr_t));

	for (i = 0; i < ce_hdr->opcount; i++, wr_e++) {

		if (ql_rdwr_indreg32(ha, wr_e->dr_addr, &wr_e->dr_value, 0)) {
			device_printf(ha->pci_dev,
				"%s: [0x%08x 0x%08x] error\n", __func__,
				wr_e->dr_addr, wr_e->dr_value);
			return -1;
		}
		if (ql_rdwr_indreg32(ha, wr_e->ar_addr, &wr_e->ar_value, 0)) {
			device_printf(ha->pci_dev,
				"%s: [0x%08x 0x%08x] error\n", __func__,
				wr_e->ar_addr, wr_e->ar_value);
			return -1;
		}
		if (ce_hdr->delay_to)  {
			if (qla_poll_reg(ha, wr_e->ar_addr, ce_hdr->delay_to,
				phdr->tmask, phdr->tvalue))
				device_printf(ha->pci_dev, "%s: "
					"[ar_addr, ar_value, delay, tmask,"
					"tvalue] [0x%08x 0x%08x 0x%08x 0x%08x"
					" 0x%08x]\n",
					__func__, wr_e->ar_addr, wr_e->ar_value,
					ce_hdr->delay_to, phdr->tmask,
					phdr->tvalue);
		}
	}
	return 0;
}

static int
qla_poll_read_list(qla_host_t *ha, q8_ce_hdr_t *ce_hdr)
{
	int		i;
	q8_poll_hdr_t	*phdr;
	q8_poll_rd_e_t	*rd_e;
	uint32_t	value;

	phdr = (q8_poll_hdr_t *)((uint8_t *)ce_hdr + sizeof (q8_ce_hdr_t));
	rd_e = (q8_poll_rd_e_t *)((uint8_t *)phdr + sizeof(q8_poll_hdr_t));

	for (i = 0; i < ce_hdr->opcount; i++, rd_e++) {
		if (ql_rdwr_indreg32(ha, rd_e->ar_addr, &rd_e->ar_value, 0)) {
			device_printf(ha->pci_dev,
				"%s: [0x%08x 0x%08x] error\n", __func__,
				rd_e->ar_addr, rd_e->ar_value);
			return -1;
		}

		if (ce_hdr->delay_to)  {
			if (qla_poll_reg(ha, rd_e->ar_addr, ce_hdr->delay_to,
				phdr->tmask, phdr->tvalue)) {
				return (-1);
			} else {
				if (ql_rdwr_indreg32(ha, rd_e->dr_addr,
					&value, 1)) {
					device_printf(ha->pci_dev,
						"%s: [0x%08x] error\n",
						__func__, rd_e->ar_addr);
					return -1;
				}

				ha->hw.rst_seq[ha->hw.rst_seq_idx++] = value;
				if (ha->hw.rst_seq_idx == Q8_MAX_RESET_SEQ_IDX)
					ha->hw.rst_seq_idx = 1;
			}
		}
	}
	return 0;
}

static int
qla_rdmwr(qla_host_t *ha, uint32_t raddr, uint32_t waddr, q8_rdmwr_hdr_t *hdr)
{
	uint32_t value;

	if (hdr->index_a >= Q8_MAX_RESET_SEQ_IDX) {
		device_printf(ha->pci_dev, "%s: [0x%08x] error\n", __func__,
			hdr->index_a);
		return -1;
	}

	if (hdr->index_a) {
		value = ha->hw.rst_seq[hdr->index_a];
	} else {
		if (ql_rdwr_indreg32(ha, raddr, &value, 1)) {
			device_printf(ha->pci_dev, "%s: [0x%08x] error\n",
						__func__, raddr);
			return -1;
		}
	}

	value &= hdr->and_value;
	value <<= hdr->shl;
	value >>= hdr->shr;
	value |= hdr->or_value;
	value ^= hdr->xor_value;

	if (ql_rdwr_indreg32(ha, waddr, &value, 0)) {
		device_printf(ha->pci_dev, "%s: [0x%08x] error\n", __func__,
			raddr);
		return -1;
	}
	return 0;
}

static int
qla_read_modify_write_list(qla_host_t *ha, q8_ce_hdr_t *ce_hdr)
{
	int		i;
	q8_rdmwr_hdr_t	*rdmwr_hdr;
	q8_rdmwr_e_t	*rdmwr_e;

	rdmwr_hdr = (q8_rdmwr_hdr_t *)((uint8_t *)ce_hdr +
						sizeof (q8_ce_hdr_t));
	rdmwr_e = (q8_rdmwr_e_t *)((uint8_t *)rdmwr_hdr +
					sizeof(q8_rdmwr_hdr_t));

	for (i = 0; i < ce_hdr->opcount; i++, rdmwr_e++) {

		if (qla_rdmwr(ha, rdmwr_e->rd_addr, rdmwr_e->wr_addr,
			rdmwr_hdr)) {
			return -1;
		}
		if (ce_hdr->delay_to) {
			DELAY(ce_hdr->delay_to);
		}
	}
	return 0;
}

static int
qla_tmplt_execute(qla_host_t *ha, uint8_t *buf, int start_idx, int *end_idx,
	uint32_t nentries)
{
	int i, ret = 0, proc_end = 0;
	q8_ce_hdr_t	*ce_hdr;

	for (i = start_idx; ((i < nentries) && (!proc_end)); i++) {
		ce_hdr = (q8_ce_hdr_t *)buf;
		ret = 0;

		switch (ce_hdr->opcode) {
		case Q8_CE_OPCODE_NOP:
			break;

		case Q8_CE_OPCODE_WRITE_LIST:
			ret = qla_wr_list(ha, ce_hdr);
			//printf("qla_wr_list %d\n", ret);
			break;

		case Q8_CE_OPCODE_READ_WRITE_LIST:
			ret = qla_rd_wr_list(ha, ce_hdr);
			//printf("qla_rd_wr_list %d\n", ret);
			break;

		case Q8_CE_OPCODE_POLL_LIST:
			ret = qla_poll_list(ha, ce_hdr);
			//printf("qla_poll_list %d\n", ret);
			break;

		case Q8_CE_OPCODE_POLL_WRITE_LIST:
			ret = qla_poll_write_list(ha, ce_hdr);
			//printf("qla_poll_write_list %d\n", ret);
			break;

		case Q8_CE_OPCODE_POLL_RD_LIST:
			ret = qla_poll_read_list(ha, ce_hdr);
			//printf("qla_poll_read_list %d\n", ret);
			break;

		case Q8_CE_OPCODE_READ_MODIFY_WRITE:
			ret = qla_read_modify_write_list(ha, ce_hdr);
			//printf("qla_read_modify_write_list %d\n", ret);
			break;

		case Q8_CE_OPCODE_SEQ_PAUSE:
			if (ce_hdr->delay_to) {
				qla_mdelay(__func__, ce_hdr->delay_to);
			}
			break;

		case Q8_CE_OPCODE_SEQ_END:
			proc_end = 1;
			break;

		case Q8_CE_OPCODE_TMPLT_END:
			*end_idx = i;
			return 0;
		}

		if (ret)
			break;

		buf += ce_hdr->size;
	}
	*end_idx = i;

	return (ret);
}

#ifndef QL_LDFLASH_FW
static int
qla_load_offchip_mem(qla_host_t *ha, uint64_t addr, uint32_t *data32,
        uint32_t len32)
{
        q80_offchip_mem_val_t val;
        int             ret = 0;

        while (len32) {
                if (len32 > 4) {
                        val.data_lo = *data32++;
                        val.data_hi = *data32++;
                        val.data_ulo = *data32++;
                        val.data_uhi = *data32++;
                        len32 -= 4;
                        if (ql_rdwr_offchip_mem(ha, addr, &val, 0))
                                return -1;

                        addr += (uint64_t)16;
                } else {
                        break;
                }
        }

        bzero(&val, sizeof(q80_offchip_mem_val_t));

        switch (len32) {
        case 3:
                val.data_lo = *data32++;
                val.data_hi = *data32++;
                val.data_ulo = *data32++;
                 ret = ql_rdwr_offchip_mem(ha, addr, &val, 0);
                break;

        case 2:
                val.data_lo = *data32++;
                val.data_hi = *data32++;
                 ret = ql_rdwr_offchip_mem(ha, addr, &val, 0);
                break;

        case 1:
                val.data_lo = *data32++;
                ret = ql_rdwr_offchip_mem(ha, addr, &val, 0);
                break;

        default:
                break;

        }
        return ret;
}


static int
qla_load_bootldr(qla_host_t *ha)
{
        uint64_t        addr;
        uint32_t        *data32;
        uint32_t        len32;
        int             ret;

        addr = (uint64_t)(READ_REG32(ha, Q8_BOOTLD_ADDR));
        data32 = (uint32_t *)ql83xx_bootloader;
        len32 = ql83xx_bootloader_len >> 2;

        ret = qla_load_offchip_mem(ha, addr, data32, len32);

        return (ret);
}

static int
qla_load_fwimage(qla_host_t *ha)
{
        uint64_t        addr;
        uint32_t        *data32;
        uint32_t        len32;
        int             ret;

        addr = (uint64_t)(READ_REG32(ha, Q8_FW_IMAGE_ADDR));
        data32 = (uint32_t *)ql83xx_firmware;
        len32 = ql83xx_firmware_len >> 2;

        ret = qla_load_offchip_mem(ha, addr, data32, len32);

        return (ret);
}
#endif /* #ifndef QL_LDFLASH_FW */

static int
qla_ld_fw_init(qla_host_t *ha)
{
	uint8_t *buf;
	uint32_t index = 0, end_idx;
	q8_tmplt_hdr_t *hdr;

	bzero(ha->hw.rst_seq, sizeof (ha->hw.rst_seq));

	hdr = (q8_tmplt_hdr_t *)ql83xx_resetseq;

	device_printf(ha->pci_dev, "%s: reset sequence\n", __func__);
	if (qla_tmplt_16bit_checksum(ha, (uint16_t *)ql83xx_resetseq,
		(uint32_t)hdr->size)) {
		device_printf(ha->pci_dev, "%s: reset seq checksum failed\n",
			__func__);
		return -1;
	}
	

	buf = ql83xx_resetseq + hdr->stop_seq_off;

	device_printf(ha->pci_dev, "%s: stop sequence\n", __func__);
	if (qla_tmplt_execute(ha, buf, index , &end_idx, hdr->nentries)) {
		device_printf(ha->pci_dev, "%s: stop seq failed\n", __func__);
		return -1;
	}

	index = end_idx;

	buf = ql83xx_resetseq + hdr->init_seq_off;

	device_printf(ha->pci_dev, "%s: init sequence\n", __func__);
	if (qla_tmplt_execute(ha, buf, index , &end_idx, hdr->nentries)) {
		device_printf(ha->pci_dev, "%s: init seq failed\n", __func__);
		return -1;
	}

#ifdef QL_LDFLASH_FW
	qla_load_fw_from_flash(ha);
	WRITE_REG32(ha, Q8_FW_IMAGE_VALID, 0);
#else
        if (qla_load_bootldr(ha))
                return -1;

        if (qla_load_fwimage(ha))
                return -1;

        WRITE_REG32(ha, Q8_FW_IMAGE_VALID, 0x12345678);
#endif /* #ifdef QL_LDFLASH_FW */

	index = end_idx;
	buf = ql83xx_resetseq + hdr->start_seq_off;

	device_printf(ha->pci_dev, "%s: start sequence\n", __func__);
	if (qla_tmplt_execute(ha, buf, index , &end_idx, hdr->nentries)) {
		device_printf(ha->pci_dev, "%s: init seq failed\n", __func__);
		return -1;
	}

	return 0;
}

int
ql_stop_sequence(qla_host_t *ha)
{
	uint8_t *buf;
	uint32_t index = 0, end_idx;
	q8_tmplt_hdr_t *hdr;

	bzero(ha->hw.rst_seq, sizeof (ha->hw.rst_seq));

	hdr = (q8_tmplt_hdr_t *)ql83xx_resetseq;

	if (qla_tmplt_16bit_checksum(ha, (uint16_t *)ql83xx_resetseq,
		(uint32_t)hdr->size)) {
		device_printf(ha->pci_dev, "%s: reset seq checksum failed\n",
		__func__);
		return (-1);
	}

	buf = ql83xx_resetseq + hdr->stop_seq_off;

	device_printf(ha->pci_dev, "%s: stop sequence\n", __func__);
	if (qla_tmplt_execute(ha, buf, index , &end_idx, hdr->nentries)) {
		device_printf(ha->pci_dev, "%s: stop seq failed\n", __func__);
		return (-1);
	}

	return end_idx;
}

int
ql_start_sequence(qla_host_t *ha, uint16_t index)
{
	uint8_t *buf;
	uint32_t end_idx;
	q8_tmplt_hdr_t *hdr;

	bzero(ha->hw.rst_seq, sizeof (ha->hw.rst_seq));

	hdr = (q8_tmplt_hdr_t *)ql83xx_resetseq;

	if (qla_tmplt_16bit_checksum(ha, (uint16_t *)ql83xx_resetseq,
		(uint32_t)hdr->size)) {
		device_printf(ha->pci_dev, "%s: reset seq checksum failed\n",
		__func__);
		return (-1);
	}

	buf = ql83xx_resetseq + hdr->init_seq_off;

	device_printf(ha->pci_dev, "%s: init sequence\n", __func__);
	if (qla_tmplt_execute(ha, buf, index , &end_idx, hdr->nentries)) {
		device_printf(ha->pci_dev, "%s: init seq failed\n", __func__);
		return (-1);
	}

#ifdef QL_LDFLASH_FW
	qla_load_fw_from_flash(ha);
	WRITE_REG32(ha, Q8_FW_IMAGE_VALID, 0);
#else
        if (qla_load_bootldr(ha))
                return -1;

        if (qla_load_fwimage(ha))
                return -1;

        WRITE_REG32(ha, Q8_FW_IMAGE_VALID, 0x12345678);
#endif /* #ifdef QL_LDFLASH_FW */


	index = end_idx;
	buf = ql83xx_resetseq + hdr->start_seq_off;

	device_printf(ha->pci_dev, "%s: start sequence\n", __func__);
	if (qla_tmplt_execute(ha, buf, index , &end_idx, hdr->nentries)) {
		device_printf(ha->pci_dev, "%s: init seq failed\n", __func__);
		return -1;
	}

	return (0);
}

