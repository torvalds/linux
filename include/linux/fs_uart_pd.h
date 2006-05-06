/*
 * Platform information definitions for the CPM Uart driver.
 *
 * 2006 (c) MontaVista Software, Inc.
 * Vitaly Bordug <vbordug@ru.mvista.com>
 *
 * This file is licensed under the terms of the GNU General Public License
 * version 2. This program is licensed "as is" without any warranty of any
 * kind, whether express or implied.
 */

#ifndef FS_UART_PD_H
#define FS_UART_PD_H

#include <linux/version.h>
#include <asm/types.h>

enum fs_uart_id {
	fsid_smc1_uart,
	fsid_smc2_uart,
	fsid_scc1_uart,
	fsid_scc2_uart,
	fsid_scc3_uart,
	fsid_scc4_uart,
	fs_uart_nr,
};

static inline int fs_uart_id_scc2fsid(int id)
{
    return fsid_scc1_uart + id - 1;
}

static inline int fs_uart_id_fsid2scc(int id)
{
    return id - fsid_scc1_uart + 1;
}

static inline int fs_uart_id_smc2fsid(int id)
{
    return fsid_smc1_uart + id - 1;
}

static inline int fs_uart_id_fsid2smc(int id)
{
    return id - fsid_smc1_uart + 1;
}

struct fs_uart_platform_info {
        void(*init_ioports)(void);
	/* device specific information */
	int fs_no;		/* controller index */
	u32 uart_clk;
	u8 tx_num_fifo;
	u8 tx_buf_size;
	u8 rx_num_fifo;
	u8 rx_buf_size;
	u8 brg;
};

#endif
