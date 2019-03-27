/*
 * Copyright (c) 2017-2018 Cavium, Inc. 
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
 *
 */

#ifndef _QLNX_IOCTL_H_
#define _QLNX_IOCTL_H_

#include <sys/ioccom.h>

#define QLNX_MAX_HW_FUNCS	2

/*
 * Read grcdump and grcdump size
 */

struct qlnx_grcdump {
	uint16_t	pci_func;
	uint32_t	grcdump_size[QLNX_MAX_HW_FUNCS];
	void		*grcdump[QLNX_MAX_HW_FUNCS];
	uint32_t	grcdump_dwords[QLNX_MAX_HW_FUNCS];
};
typedef struct qlnx_grcdump qlnx_grcdump_t;

/*
 * Read idle_chk and idle_chk size
 */
struct qlnx_idle_chk {
	uint16_t	pci_func;
	uint32_t	idle_chk_size[QLNX_MAX_HW_FUNCS];
	void		*idle_chk[QLNX_MAX_HW_FUNCS];
	uint32_t	idle_chk_dwords[QLNX_MAX_HW_FUNCS];
};
typedef struct qlnx_idle_chk qlnx_idle_chk_t;

/*
 * Retrive traces
 */
struct qlnx_trace {
	uint16_t	pci_func;

	uint16_t	cmd;
#define QLNX_MCP_TRACE			0x01
#define QLNX_REG_FIFO			0x02
#define QLNX_IGU_FIFO			0x03
#define QLNX_PROTECTION_OVERRIDE	0x04
#define QLNX_FW_ASSERTS			0x05

	uint32_t	size[QLNX_MAX_HW_FUNCS];
	void		*buffer[QLNX_MAX_HW_FUNCS];
	uint32_t	dwords[QLNX_MAX_HW_FUNCS];
};
typedef struct qlnx_trace qlnx_trace_t;


/*
 * Read driver info
 */
#define QLNX_DRV_INFO_NAME_LENGTH		32
#define QLNX_DRV_INFO_VERSION_LENGTH		32
#define QLNX_DRV_INFO_MFW_VERSION_LENGTH	32
#define QLNX_DRV_INFO_STORMFW_VERSION_LENGTH	32
#define QLNX_DRV_INFO_BUS_INFO_LENGTH		32

struct qlnx_drvinfo {
	char		drv_name[QLNX_DRV_INFO_NAME_LENGTH];
	char		drv_version[QLNX_DRV_INFO_VERSION_LENGTH];
	char		mfw_version[QLNX_DRV_INFO_MFW_VERSION_LENGTH];
	char		stormfw_version[QLNX_DRV_INFO_STORMFW_VERSION_LENGTH];
	uint32_t	eeprom_dump_len; /* in bytes */
	uint32_t	reg_dump_len; /* in bytes */
	char		bus_info[QLNX_DRV_INFO_BUS_INFO_LENGTH];
};
typedef struct qlnx_drvinfo qlnx_drvinfo_t;

/*
 * Read Device Setting
 */
struct qlnx_dev_setting {
	uint32_t	supported; /* Features this interface supports */
	uint32_t	advertising; /* Features this interface advertises */
	uint32_t	speed; /* The forced speed, 10Mb, 100Mb, gigabit */
	uint32_t	duplex; /* Duplex, half or full */
	uint32_t	port; /* Which connector port */
	uint32_t	phy_address; /* port number*/
	uint32_t	autoneg; /* Enable or disable autonegotiation */
};
typedef struct qlnx_dev_setting qlnx_dev_setting_t;

/*
 * Get Registers
 */
struct qlnx_get_regs {
	void		*reg_buf;
	uint32_t	reg_buf_len;
};
typedef struct qlnx_get_regs qlnx_get_regs_t;

/*
 * Get/Set NVRAM
 */
struct qlnx_nvram {
	uint32_t	cmd;
#define QLNX_NVRAM_CMD_WRITE_NVRAM	0x01
#define QLNX_NVRAM_CMD_READ_NVRAM	0x02
#define QLNX_NVRAM_CMD_SET_SECURE_MODE	0x03
#define QLNX_NVRAM_CMD_DEL_FILE		0x04
#define QLNX_NVRAM_CMD_PUT_FILE_BEGIN	0x05
#define QLNX_NVRAM_CMD_GET_NVRAM_RESP	0x06
#define QLNX_NVRAM_CMD_PUT_FILE_DATA	0x07

	void		*data;
	uint32_t	offset;
	uint32_t	data_len;
	uint32_t	magic;
};
typedef struct qlnx_nvram qlnx_nvram_t;

/*
 * Get/Set Device registers
 */
struct qlnx_reg_rd_wr {
	uint32_t	cmd;
#define QLNX_REG_READ_CMD	0x01
#define QLNX_REG_WRITE_CMD	0x02

	uint32_t	addr;
	uint32_t	val;

	uint32_t	access_type;
#define QLNX_REG_ACCESS_DIRECT		0x01
#define QLNX_REG_ACCESS_INDIRECT	0x02

	uint32_t	hwfn_index;
};
typedef struct qlnx_reg_rd_wr qlnx_reg_rd_wr_t;

/*
 * Read/Write PCI Configuration
 */
struct qlnx_pcicfg_rd_wr {
	uint32_t	cmd;
#define QLNX_PCICFG_READ		0x01
#define QLNX_PCICFG_WRITE		0x02
	uint32_t	reg;
	uint32_t	val;
	uint32_t	width;
};
typedef struct qlnx_pcicfg_rd_wr qlnx_pcicfg_rd_wr_t;

/*
 * Read MAC address
 */
struct qlnx_perm_mac_addr {
	char	addr[32];
};
typedef struct qlnx_perm_mac_addr qlnx_perm_mac_addr_t;


/*
 * Read STORM statistics registers
 */
struct qlnx_storm_stats {

	/* xstorm */
	uint32_t xstorm_active_cycles;
	uint32_t xstorm_stall_cycles;
	uint32_t xstorm_sleeping_cycles;
	uint32_t xstorm_inactive_cycles;

	/* ystorm */
	uint32_t ystorm_active_cycles;
	uint32_t ystorm_stall_cycles;
	uint32_t ystorm_sleeping_cycles;
	uint32_t ystorm_inactive_cycles;

	/* pstorm */
	uint32_t pstorm_active_cycles;
	uint32_t pstorm_stall_cycles;
	uint32_t pstorm_sleeping_cycles;
	uint32_t pstorm_inactive_cycles;

	/* tstorm */
	uint32_t tstorm_active_cycles;
	uint32_t tstorm_stall_cycles;
	uint32_t tstorm_sleeping_cycles;
	uint32_t tstorm_inactive_cycles;

	/* mstorm */
	uint32_t mstorm_active_cycles;
	uint32_t mstorm_stall_cycles;
	uint32_t mstorm_sleeping_cycles;
	uint32_t mstorm_inactive_cycles;

	/* ustorm */
	uint32_t ustorm_active_cycles;
	uint32_t ustorm_stall_cycles;
	uint32_t ustorm_sleeping_cycles;
	uint32_t ustorm_inactive_cycles;
}; 

typedef struct qlnx_storm_stats qlnx_storm_stats_t;

#define QLNX_STORM_STATS_SAMPLES_PER_HWFN	(10000)

#define QLNX_STORM_STATS_BYTES_PER_HWFN (sizeof(qlnx_storm_stats_t) * \
		QLNX_STORM_STATS_SAMPLES_PER_HWFN)

struct qlnx_storm_stats_dump {
	int num_hwfns;
	int num_samples;
	void *buffer[QLNX_MAX_HW_FUNCS];
};

typedef struct qlnx_storm_stats_dump qlnx_storm_stats_dump_t;

#define QLNX_LLDP_TYPE_END_OF_LLDPDU		0
#define QLNX_LLDP_TYPE_CHASSIS_ID		1	
#define QLNX_LLDP_TYPE_PORT_ID			2	
#define QLNX_LLDP_TYPE_TTL			3
#define QLNX_LLDP_TYPE_PORT_DESC		4
#define QLNX_LLDP_TYPE_SYS_NAME			5
#define QLNX_LLDP_TYPE_SYS_DESC			6
#define QLNX_LLDP_TYPE_SYS_CAPS			7
#define QLNX_LLDP_TYPE_MGMT_ADDR		8
#define QLNX_LLDP_TYPE_ORG_SPECIFIC		127

#define QLNX_LLDP_CHASSIS_ID_SUBTYPE_OCTETS	1 //Subtype is 1 byte
#define QLNX_LLDP_CHASSIS_ID_SUBTYPE_MAC	0x04 //Mac Address
#define QLNX_LLDP_CHASSIS_ID_MAC_ADDR_LEN	6 // Mac address is 6 bytes
#define QLNX_LLDP_CHASSIS_ID_SUBTYPE_IF_NAME	0x06 //Interface Name

#define QLNX_LLDP_PORT_ID_SUBTYPE_OCTETS	1 //Subtype is 1 byte
#define QLNX_LLDP_PORT_ID_SUBTYPE_MAC		0x03 //Mac Address
#define QLNX_LLDP_PORT_ID_MAC_ADDR_LEN		6 // Mac address is 6 bytes
#define QLNX_LLDP_PORT_ID_SUBTYPE_IF_NAME	0x05 //Interface Name

#define QLNX_LLDP_SYS_TLV_SIZE 256
struct qlnx_lldp_sys_tlvs {
	int		discard_mandatory_tlv;
	uint8_t		buf[QLNX_LLDP_SYS_TLV_SIZE];
	uint16_t	buf_size;
};
typedef struct qlnx_lldp_sys_tlvs qlnx_lldp_sys_tlvs_t;


/*
 * Read grcdump size
 */
#define QLNX_GRC_DUMP_SIZE	_IOWR('q', 1, qlnx_grcdump_t)

/*
 * Read grcdump
 */
#define QLNX_GRC_DUMP		_IOWR('q', 2, qlnx_grcdump_t)

/*
 * Read idle_chk size
 */
#define QLNX_IDLE_CHK_SIZE	_IOWR('q', 3, qlnx_idle_chk_t)

/*
 * Read idle_chk
 */
#define QLNX_IDLE_CHK		_IOWR('q', 4, qlnx_idle_chk_t)

/*
 * Read driver info
 */
#define QLNX_DRV_INFO		_IOWR('q', 5, qlnx_drvinfo_t)

/*
 * Read Device Setting
 */
#define QLNX_DEV_SETTING	_IOR('q', 6, qlnx_dev_setting_t)

/*
 * Get Registers
 */
#define QLNX_GET_REGS		_IOR('q', 7, qlnx_get_regs_t)

/*
 * Get/Set NVRAM
 */
#define QLNX_NVRAM		_IOWR('q', 8, qlnx_nvram_t)

/*
 * Get/Set Device registers
 */
#define QLNX_RD_WR_REG		_IOWR('q', 9, qlnx_reg_rd_wr_t)

/*
 * Read/Write PCI Configuration
 */
#define QLNX_RD_WR_PCICFG	_IOWR('q', 10, qlnx_pcicfg_rd_wr_t)

/*
 * Read MAC address
 */
#define QLNX_MAC_ADDR		_IOWR('q', 11, qlnx_perm_mac_addr_t)

/*
 * Read STORM statistics
 */
#define QLNX_STORM_STATS	_IOWR('q', 12, qlnx_storm_stats_dump_t)

/*
 * Read trace size
 */
#define QLNX_TRACE_SIZE		_IOWR('q', 13, qlnx_trace_t)

/*
 * Read trace
 */
#define QLNX_TRACE		_IOWR('q', 14, qlnx_trace_t)

/*
 * Set LLDP TLVS
 */
#define QLNX_SET_LLDP_TLVS	_IOWR('q', 15, qlnx_lldp_sys_tlvs_t)

#endif /* #ifndef _QLNX_IOCTL_H_ */
