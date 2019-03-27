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
 *
 * $FreeBSD$
 */
/*
 * File: ql_ioctl.h
 * Author : David C Somayajulu, Qlogic Corporation, Aliso Viejo, CA 92656.
 */

#ifndef _QL_IOCTL_H_
#define _QL_IOCTL_H_

#include <sys/ioccom.h>

struct qla_reg_val {
        uint16_t rd;
        uint16_t direct;
        uint32_t reg;
        uint32_t val;
};
typedef struct qla_reg_val qla_reg_val_t;

struct qla_rd_flash {
        uint32_t off;
        uint32_t data;
};
typedef struct qla_rd_flash qla_rd_flash_t;

struct qla_wr_flash {
	uint32_t off;
	uint32_t size;
	void *buffer;
	uint32_t pattern;
};
typedef struct qla_wr_flash qla_wr_flash_t;

struct qla_erase_flash {
	uint32_t off;
	uint32_t size;
};
typedef struct qla_erase_flash qla_erase_flash_t;

struct qla_rd_pci_ids {
	uint16_t ven_id;
	uint16_t dev_id;
	uint16_t subsys_ven_id;
	uint16_t subsys_dev_id;
	uint8_t rev_id;
};
typedef struct qla_rd_pci_ids qla_rd_pci_ids_t;

#define NUM_LOG_ENTRY_PARAMS	5
#define NUM_LOG_ENTRIES		512

struct qla_sp_log_entry {
	uint32_t fmtstr_idx;
	uint32_t num_params;
	uint64_t usec_ts;
	uint32_t params[NUM_LOG_ENTRY_PARAMS];
};
typedef struct qla_sp_log_entry qla_sp_log_entry_t;

/*
 * structure encapsulating the value to read/write from/to offchip (MS) memory
 */
struct qla_offchip_mem_val {
	uint16_t rd;
	uint64_t off;
	uint32_t data_lo;
	uint32_t data_hi;
	uint32_t data_ulo;
	uint32_t data_uhi;
};
typedef struct qla_offchip_mem_val qla_offchip_mem_val_t;

struct qla_rd_fw_dump {
	uint16_t pci_func;
	uint16_t saved;
	uint64_t usec_ts;
	uint32_t minidump_size;
	void *minidump;
};
typedef struct qla_rd_fw_dump qla_rd_fw_dump_t;

struct qla_drvr_state_tx {
	uint64_t	base_p_addr;
	uint64_t	cons_p_addr;
	uint32_t	tx_prod_reg;
	uint32_t	tx_cntxt_id;
	uint32_t	txr_free;
	uint32_t	txr_next;
	uint32_t	txr_comp;
};
typedef struct qla_drvr_state_tx qla_drvr_state_tx_t;

struct qla_drvr_state_sds {
	uint32_t        sdsr_next; /* next entry in SDS ring to process */
	uint32_t        sds_consumer;
};
typedef struct qla_drvr_state_sds qla_drvr_state_sds_t;

struct qla_drvr_state_rx {
	uint32_t	prod_std;
	uint32_t	rx_next; /* next standard rcv ring to arm fw */;
};
typedef struct qla_drvr_state_rx qla_drvr_state_rx_t;

struct qla_drvr_state_hdr {
	uint32_t	drvr_version_major;
	uint32_t	drvr_version_minor;
	uint32_t	drvr_version_build;

	uint8_t         mac_addr[ETHER_ADDR_LEN];
	uint16_t	saved;
	uint64_t	usec_ts;
        uint16_t        link_speed;
        uint16_t        cable_length;
        uint32_t        cable_oui;
        uint8_t         link_up;
        uint8_t         module_type;
        uint8_t         link_faults;
        uint32_t        rcv_intr_coalesce;
        uint32_t        xmt_intr_coalesce;

	uint32_t	tx_state_offset;/* size = sizeof (qla_drvr_state_tx_t) * num_tx_rings */
	uint32_t	rx_state_offset;/* size = sizeof (qla_drvr_state_rx_t) * num_rx_rings */
	uint32_t	sds_state_offset;/* size = sizeof (qla_drvr_state_sds_t) * num_sds_rings */

	uint32_t	num_tx_rings; /* number of tx rings */
	uint32_t	txr_size; /* size of each tx ring in bytes */
	uint32_t	txr_entries; /* number of descriptors in each tx ring */
	uint32_t	txr_offset; /* start of tx ring [0 - #rings] content */

	uint32_t	num_rx_rings; /* number of rx rings */
	uint32_t	rxr_size; /* size of each rx ring in bytes */
	uint32_t	rxr_entries; /* number of descriptors in each rx ring */
	uint32_t	rxr_offset; /* start of rx ring [0 - #rings] content */

	uint32_t	num_sds_rings; /* number of sds rings */
	uint32_t	sds_ring_size; /* size of each sds ring in bytes */
	uint32_t	sds_entries; /* number of descriptors in each sds ring */
	uint32_t	sds_offset; /* start of sds ring [0 - #rings] content */
};

typedef struct qla_drvr_state_hdr qla_drvr_state_hdr_t;

struct qla_driver_state {
	uint32_t	size;
	void		*buffer;
};
typedef struct qla_driver_state qla_driver_state_t;

struct qla_sp_log {
	uint32_t	next_idx; /* index of next entry in slowpath trace log */
	uint32_t	num_entries; /* number of entries in slowpath trace log */
	void		*buffer;
};
typedef struct qla_sp_log qla_sp_log_t;

/*
 * Read/Write Register
 */
#define QLA_RDWR_REG		_IOWR('q', 1, qla_reg_val_t)

/*
 * Read Flash
 */
#define QLA_RD_FLASH		_IOWR('q', 2, qla_rd_flash_t)

/*
 * Write Flash
 */
#define QLA_WR_FLASH		_IOWR('q', 3, qla_wr_flash_t)

/*
 * Read Offchip (MS) Memory
 */
#define QLA_RDWR_MS_MEM		_IOWR('q', 4, qla_offchip_mem_val_t)

/*
 * Erase Flash
 */
#define QLA_ERASE_FLASH		_IOWR('q', 5, qla_erase_flash_t)

/*
 * Read PCI IDs 
 */
#define QLA_RD_PCI_IDS		_IOWR('q', 6, qla_rd_pci_ids_t)			

/*
 * Read Minidump Template Size
 */
#define QLA_RD_FW_DUMP_SIZE	_IOWR('q', 7, qla_rd_fw_dump_t)

/*
 * Read Minidump Template
 */
#define QLA_RD_FW_DUMP		_IOWR('q', 8, qla_rd_fw_dump_t)

/*
 * Read Driver State
 */
#define QLA_RD_DRVR_STATE	_IOWR('q', 9, qla_driver_state_t)

/*
 * Read Slowpath Log
 */
#define QLA_RD_SLOWPATH_LOG	_IOWR('q', 10, qla_sp_log_t)

/*
 * Format Strings For Slowpath Trace Logs
 */
#define SP_TLOG_FMT_STR_0	\
	"qla_mbx_cmd [%ld]: enter no_pause = %d [0x%08x 0x%08x 0x%08x 0x%08x]\n"

#define SP_TLOG_FMT_STR_1	\
	"qla_mbx_cmd [%ld]: offline = 0x%08x qla_initiate_recovery = 0x%08x exit1\n"

#define SP_TLOG_FMT_STR_2	\
	"qla_mbx_cmd [%ld]: qla_initiate_recovery = 0x%08x exit2\n"

#define SP_TLOG_FMT_STR_3	\
	"qla_mbx_cmd [%ld]: timeout exit3 [host_mbx_cntrl = 0x%08x]\n"

#define SP_TLOG_FMT_STR_4	\
	"qla_mbx_cmd [%ld]: qla_initiate_recovery = 0x%08x exit4\n"

#define SP_TLOG_FMT_STR_5	\
	"qla_mbx_cmd [%ld]: timeout exit5 [fw_mbx_cntrl = 0x%08x]\n"

#define SP_TLOG_FMT_STR_6	\
	"qla_mbx_cmd [%ld]: qla_initiate_recovery = 0x%08x exit6\n"

#define SP_TLOG_FMT_STR_7	\
	"qla_mbx_cmd [%ld]: exit [0x%08x 0x%08x 0x%08x 0x%08x 0x%08x]\n"

#define SP_TLOG_FMT_STR_8	\
	"qla_ioctl [%ld]: SIOCSIFADDR if_drv_flags = 0x%08x [0x%08x] ipv4 = 0x%08x\n"

#define SP_TLOG_FMT_STR_9	\
	"qla_ioctl [%ld]: SIOCSIFMTU if_drv_flags = 0x%08x [0x%08x] max_frame_size = 0x%08x if_mtu = 0x%08x\n"

#define SP_TLOG_FMT_STR_10	\
	"qla_ioctl [%ld]: SIOCSIFFLAGS if_drv_flags = 0x%08x [0x%08x] ha->if_flags = 0x%08x ifp->if_flags = 0x%08x\n"

#define SP_TLOG_FMT_STR_11	\
	"qla_ioctl [%ld]: SIOCSIFCAP if_drv_flags = 0x%08x [0x%08x] mask = 0x%08x ifp->if_capenable = 0x%08x\n"

#define SP_TLOG_FMT_STR_12	\
	"qla_set_multi [%ld]: if_drv_flags = 0x%08x [0x%08x] add_multi = 0x%08x mcnt = 0x%08x\n"

#define SP_TLOG_FMT_STR_13	\
	"qla_stop [%ld]: \n"

#define SP_TLOG_FMT_STR_14	\
	"qla_init_locked [%ld]: \n"


#endif /* #ifndef _QL_IOCTL_H_ */
