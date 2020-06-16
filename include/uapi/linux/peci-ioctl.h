/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
/* Copyright (c) 2018-2020 Intel Corporation */

#ifndef __PECI_IOCTL_H
#define __PECI_IOCTL_H

#include <linux/ioctl.h>
#include <linux/types.h>

/* The PECI client's default address of 0x30 */
#define PECI_BASE_ADDR					0x30

/* Max number of CPU clients */
#define PECI_OFFSET_MAX					8

/* PECI read/write data buffer size max */
#define PECI_BUFFER_SIZE				255

/* Device Specific Completion Code (CC) Definition */
#define PECI_DEV_CC_SUCCESS				0x40
#define PECI_DEV_CC_NEED_RETRY				0x80
#define PECI_DEV_CC_OUT_OF_RESOURCE			0x81
#define PECI_DEV_CC_UNAVAIL_RESOURCE			0x82
#define PECI_DEV_CC_INVALID_REQ				0x90
#define PECI_DEV_CC_MCA_ERROR				0x91
#define PECI_DEV_CC_CATASTROPHIC_MCA_ERROR		0x93
#define PECI_DEV_CC_FATAL_MCA_DETECTED			0x94
#define PECI_DEV_CC_PARITY_ERROR_ON_GPSB_OR_PMSB	0x98
#define PECI_DEV_CC_PARITY_ERROR_ON_GPSB_OR_PMSB_IERR	0x9B
#define PECI_DEV_CC_PARITY_ERROR_ON_GPSB_OR_PMSB_MCA	0x9C

/* Completion Code mask to check retry needs */
#define PECI_DEV_CC_RETRY_CHECK_MASK			0xf0

#define PECI_DEV_RETRY_TIMEOUT				msecs_to_jiffies(700)
#define PECI_DEV_RETRY_INTERVAL_MIN_USEC		100
#define PECI_DEV_RETRY_INTERVAL_MAX_USEC		(128 * 1000)
#define PECI_DEV_RETRY_BIT				0x01

/**
 * enum peci_cmd - PECI client commands
 * @PECI_CMD_XFER: raw PECI transfer
 * @PECI_CMD_PING: ping, a required message for all PECI devices
 * @PECI_CMD_GET_DIB: get DIB (Device Info Byte)
 * @PECI_CMD_GET_TEMP: get maximum die temperature
 * @PECI_CMD_RD_PKG_CFG: read access to the PCS (Package Configuration Space)
 * @PECI_CMD_WR_PKG_CFG: write access to the PCS (Package Configuration Space)
 * @PECI_CMD_RD_IA_MSR: read access to MSRs (Model Specific Registers)
 * @PECI_CMD_WR_IA_MSR: write access to MSRs (Model Specific Registers)
 * @PECI_CMD_RD_IA_MSREX: read access to MSRs (Model Specific Registers)
 * @PECI_CMD_RD_PCI_CFG: sideband read access to the PCI configuration space
 *	maintained in downstream devices external to the processor
 * @PECI_CMD_WR_PCI_CFG: sideband write access to the PCI configuration space
 *	maintained in downstream devices external to the processor
 * @PECI_CMD_RD_PCI_CFG_LOCAL: sideband read access to the PCI configuration
 *	space that resides within the processor
 * @PECI_CMD_WR_PCI_CFG_LOCAL: sideband write access to the PCI configuration
 *	space that resides within the processor
 *
 * Available commands depend on client's PECI revision.
 */
enum peci_cmd {
	PECI_CMD_XFER = 0,
	PECI_CMD_PING,
	PECI_CMD_GET_DIB,
	PECI_CMD_GET_TEMP,
	PECI_CMD_RD_PKG_CFG,
	PECI_CMD_WR_PKG_CFG,
	PECI_CMD_RD_IA_MSR,
	PECI_CMD_WR_IA_MSR,
	PECI_CMD_RD_IA_MSREX,
	PECI_CMD_RD_PCI_CFG,
	PECI_CMD_WR_PCI_CFG,
	PECI_CMD_RD_PCI_CFG_LOCAL,
	PECI_CMD_WR_PCI_CFG_LOCAL,
	PECI_CMD_RD_END_PT_CFG,
	PECI_CMD_WR_END_PT_CFG,
	PECI_CMD_CRASHDUMP_DISC,
	PECI_CMD_CRASHDUMP_GET_FRAME,
	PECI_CMD_MAX
};

/**
 * struct peci_xfer_msg - raw PECI transfer command
 * @addr; address of the client
 * @tx_len: number of data to be written in bytes
 * @rx_len: number of data to be read in bytes
 * @tx_buf: data to be written, or NULL
 * @rx_buf: data to be read, or NULL
 *
 * raw PECI transfer
 */
struct peci_xfer_msg {
	__u8	addr;
	__u8	tx_len;
	__u8	rx_len;
	__u8	padding;
	__u8	*tx_buf;
	__u8	*rx_buf;
} __attribute__((__packed__));

/**
 * struct peci_ping_msg - ping command
 * @addr: address of the client
 *
 * Ping() is a required message for all PECI devices. This message is used to
 * enumerate devices or determine if a device has been removed, been
 * powered-off, etc.
 */
struct peci_ping_msg {
	__u8	addr;
	__u8	padding[3];
} __attribute__((__packed__));

/**
 * struct peci_get_dib_msg - GetDIB command
 * @addr: address of the client
 * @dib: DIB data to be read
 *
 * The processor PECI client implementation of GetDIB() includes an 8-byte
 * response and provides information regarding client revision number and the
 * number of supported domains. All processor PECI clients support the GetDIB()
 * command.
 */
struct peci_get_dib_msg {
#define PECI_GET_DIB_WR_LEN	1
#define PECI_GET_DIB_RD_LEN	8
#define PECI_GET_DIB_CMD	0xf7

	__u8	addr;
	__u8	padding[3];
	__u64	dib;
} __attribute__((__packed__));

/**
 * struct peci_get_temp_msg - GetTemp command
 * @addr: address of the client
 * @temp_raw: raw temperature data to be read
 *
 * The GetTemp() command is used to retrieve the maximum die temperature from a
 * target PECI address. The temperature is used by the external thermal
 * management system to regulate the temperature on the die. The data is
 * returned as a negative value representing the number of degrees centigrade
 * below the maximum processor junction temperature.
 */
struct peci_get_temp_msg {
#define PECI_GET_TEMP_WR_LEN	1
#define PECI_GET_TEMP_RD_LEN	2
#define PECI_GET_TEMP_CMD	0x01

	__u8	addr;
	__u8	padding;
	__s16	temp_raw;
} __attribute__((__packed__));

/**
 * struct peci_rd_pkg_cfg_msg - RdPkgConfig command
 * @addr: address of the client
 * @index: encoding index for the requested service
 * @param: specific data being requested
 * @rx_len: number of data to be read in bytes
 * @cc: completion code
 * @pkg_config: package config data to be read
 *
 * The RdPkgConfig() command provides read access to the Package Configuration
 * Space (PCS) within the processor, including various power and thermal
 * management functions. Typical PCS read services supported by the processor
 * may include access to temperature data, energy status, run time information,
 * DIMM temperatures and so on.
 */
struct peci_rd_pkg_cfg_msg {
#define PECI_RDPKGCFG_WRITE_LEN			5
#define PECI_RDPKGCFG_READ_LEN_BASE		1
#define PECI_RDPKGCFG_CMD			0xa1

	__u8	addr;
	__u8	index;
#define PECI_MBX_INDEX_CPU_ID			0  /* Package Identifier Read */
#define PECI_MBX_INDEX_VR_DEBUG			1  /* VR Debug */
#define PECI_MBX_INDEX_PKG_TEMP_READ		2  /* Package Temperature Read */
#define PECI_MBX_INDEX_ENERGY_COUNTER		3  /* Energy counter */
#define PECI_MBX_INDEX_ENERGY_STATUS		4  /* DDR Energy Status */
#define PECI_MBX_INDEX_WAKE_MODE_BIT		5  /* "Wake on PECI" Mode bit */
#define PECI_MBX_INDEX_EPI			6  /* Efficient Performance Indication */
#define PECI_MBX_INDEX_PKG_RAPL_PERF		8  /* Pkg RAPL Performance Status Read */
#define PECI_MBX_INDEX_MODULE_TEMP		9  /* Module Temperature Read */
#define PECI_MBX_INDEX_DTS_MARGIN		10 /* DTS thermal margin */
#define PECI_MBX_INDEX_SKT_PWR_THRTL_DUR	11 /* Socket Power Throttled Duration */
#define PECI_MBX_INDEX_CFG_TDP_CONTROL		12 /* TDP Config Control */
#define PECI_MBX_INDEX_CFG_TDP_LEVELS		13 /* TDP Config Levels */
#define PECI_MBX_INDEX_DDR_DIMM_TEMP		14 /* DDR DIMM Temperature */
#define PECI_MBX_INDEX_CFG_ICCMAX		15 /* Configurable ICCMAX */
#define PECI_MBX_INDEX_TEMP_TARGET		16 /* Temperature Target Read */
#define PECI_MBX_INDEX_CURR_CFG_LIMIT		17 /* Current Config Limit */
#define PECI_MBX_INDEX_DIMM_TEMP_READ		20 /* Package Thermal Status Read */
#define PECI_MBX_INDEX_DRAM_IMC_TMP_READ	22 /* DRAM IMC Temperature Read */
#define PECI_MBX_INDEX_DDR_CH_THERM_STAT	23 /* DDR Channel Thermal Status */
#define PECI_MBX_INDEX_PKG_POWER_LIMIT1		26 /* Package Power Limit1 */
#define PECI_MBX_INDEX_PKG_POWER_LIMIT2		27 /* Package Power Limit2 */
#define PECI_MBX_INDEX_TDP			28 /* Thermal design power minimum */
#define PECI_MBX_INDEX_TDP_HIGH			29 /* Thermal design power maximum */
#define PECI_MBX_INDEX_TDP_UNITS		30 /* Units for power/energy registers */
#define PECI_MBX_INDEX_RUN_TIME			31 /* Accumulated Run Time */
#define PECI_MBX_INDEX_CONSTRAINED_TIME		32 /* Thermally Constrained Time Read */
#define PECI_MBX_INDEX_TURBO_RATIO		33 /* Turbo Activation Ratio */
#define PECI_MBX_INDEX_DDR_RAPL_PL1		34 /* DDR RAPL PL1 */
#define PECI_MBX_INDEX_DDR_PWR_INFO_HIGH	35 /* DRAM Power Info Read (high) */
#define PECI_MBX_INDEX_DDR_PWR_INFO_LOW		36 /* DRAM Power Info Read (low) */
#define PECI_MBX_INDEX_DDR_RAPL_PL2		37 /* DDR RAPL PL2 */
#define PECI_MBX_INDEX_DDR_RAPL_STATUS		38 /* DDR RAPL Performance Status */
#define PECI_MBX_INDEX_DDR_HOT_ABSOLUTE		43 /* DDR Hottest Dimm Absolute Temp */
#define PECI_MBX_INDEX_DDR_HOT_RELATIVE		44 /* DDR Hottest Dimm Relative Temp */
#define PECI_MBX_INDEX_DDR_THROTTLE_TIME	45 /* DDR Throttle Time */
#define PECI_MBX_INDEX_DDR_THERM_STATUS		46 /* DDR Thermal Status */
#define PECI_MBX_INDEX_TIME_AVG_TEMP		47 /* Package time-averaged temperature */
#define PECI_MBX_INDEX_TURBO_RATIO_LIMIT	49 /* Turbo Ratio Limit Read */
#define PECI_MBX_INDEX_HWP_AUTO_OOB		53 /* HWP Autonomous Out-of-band */
#define PECI_MBX_INDEX_DDR_WARM_BUDGET		55 /* DDR Warm Power Budget */
#define PECI_MBX_INDEX_DDR_HOT_BUDGET		56 /* DDR Hot Power Budget */
#define PECI_MBX_INDEX_PKG_PSYS_PWR_LIM3	57 /* Package/Psys Power Limit3 */
#define PECI_MBX_INDEX_PKG_PSYS_PWR_LIM1	58 /* Package/Psys Power Limit1 */
#define PECI_MBX_INDEX_PKG_PSYS_PWR_LIM2	59 /* Package/Psys Power Limit2 */
#define PECI_MBX_INDEX_PKG_PSYS_PWR_LIM4	60 /* Package/Psys Power Limit4 */
#define PECI_MBX_INDEX_PERF_LIMIT_REASON	65 /* Performance Limit Reasons */

	__u16	param;
/* When index is PECI_MBX_INDEX_CPU_ID */
#define PECI_PKG_ID_CPU_ID			0x0000  /* CPUID Info */
#define PECI_PKG_POWER_SKU_UNIT		0x0000 /* Time, Energy, Power units */
#define PECI_PKG_ID_PLATFORM_ID			0x0001  /* Platform ID */
#define PECI_PKG_ID_UNCORE_ID			0x0002  /* Uncore Device ID */
#define PECI_PKG_ID_MAX_THREAD_ID		0x0003  /* Max Thread ID */
#define PECI_PKG_ID_MICROCODE_REV		0x0004  /* CPU Microcode Update Revision */
#define PECI_PKG_ID_MACHINE_CHECK_STATUS	0x0005  /* Machine Check Status */
#define PECI_PKG_ID_CPU_PACKAGE		0x00ff  /* CPU package ID*/
#define PECI_PKG_ID_DIMM			0x00ff  /* DIMM ID*/
#define PECI_PKG_ID_PLATFORM			0x00fe  /* Entire platform ID */

	__u8	rx_len;
	__u8	cc;
	__u8	padding[2];
	__u8	pkg_config[4];
} __attribute__((__packed__));

/**
 * struct peci_wr_pkg_cfg_msg - WrPkgConfig command
 * @addr: address of the client
 * @index: encoding index for the requested service
 * @param: specific data being requested
 * @tx_len: number of data to be written in bytes
 * @cc: completion code
 * @value: package config data to be written
 *
 * The WrPkgConfig() command provides write access to the Package Configuration
 * Space (PCS) within the processor, including various power and thermal
 * management functions. Typical PCS write services supported by the processor
 * may include power limiting, thermal averaging constant programming and so
 * on.
 */
struct peci_wr_pkg_cfg_msg {
#define PECI_WRPKGCFG_WRITE_LEN_BASE	6
#define PECI_WRPKGCFG_READ_LEN		1
#define PECI_WRPKGCFG_CMD		0xa5

	__u8	addr;
	__u8	index;
#define PECI_MBX_INDEX_DIMM_AMBIENT	19
#define PECI_MBX_INDEX_DIMM_TEMP	24

	__u16	param;
	__u8	tx_len;
	__u8	cc;
	__u8	padding[2];
	__u32	value;
} __attribute__((__packed__));

/**
 * struct peci_rd_ia_msr_msg - RdIAMSR command
 * @addr: address of the client
 * @thread_id: ID of the specific logical processor
 * @address: address of MSR to read from
 * @cc: completion code
 * @value: data to be read
 *
 * The RdIAMSR() PECI command provides read access to Model Specific Registers
 * (MSRs) defined in the processor's Intel Architecture (IA).
 */
struct peci_rd_ia_msr_msg {
#define PECI_RDIAMSR_WRITE_LEN		5
#define PECI_RDIAMSR_READ_LEN		9
#define PECI_RDIAMSR_CMD		0xb1

	__u8	addr;
	__u8	thread_id;
	__u16	address;
	__u8	cc;
	__u8	padding[3];
	__u64	value;
} __attribute__((__packed__));

/**
 * struct peci_wr_ia_msr_msg - WrIAMSR command
 * @addr: address of the client
 * @thread_id: ID of the specific logical processor
 * @address: address of MSR to write to
 * @tx_len: number of data to be written in bytes
 * @cc: completion code
 * @value: data to be written
 *
 * The WrIAMSR() PECI command provides write access to Model Specific Registers
 * (MSRs) defined in the processor's Intel Architecture (IA).
 */
struct peci_wr_ia_msr_msg {
#define PECI_WRIAMSR_CMD		0xb5

	__u8	addr;
	__u8	thread_id;
	__u16	address;
	__u8	tx_len;
	__u8	cc;
	__u8	padding[2];
	__u64	value;
} __attribute__((__packed__));

/**
 * struct peci_rd_ia_msrex_msg - RdIAMSREX command
 * @addr: address of the client
 * @thread_id: ID of the specific logical processor
 * @address: address of MSR to read from
 * @cc: completion code
 * @value: data to be read
 *
 * The RdIAMSREX() PECI command provides read access to Model Specific
 * Registers (MSRs) defined in the processor's Intel Architecture (IA).
 * The differences between RdIAMSREX() and RdIAMSR() are that:
 * (1)RdIAMSR() can only read MC registers, RdIAMSREX() can read all MSRs
 * (2)thread_id of RdIAMSR() is u8, thread_id of RdIAMSREX() is u16
 */
struct peci_rd_ia_msrex_msg {
#define PECI_RDIAMSREX_WRITE_LEN	6
#define PECI_RDIAMSREX_READ_LEN		9
#define PECI_RDIAMSREX_CMD		0xd1

	__u8	addr;
	__u8	padding0;
	__u16	thread_id;
	__u16	address;
	__u8	cc;
	__u8	padding1;
	__u64	value;
} __attribute__((__packed__));

/**
 * struct peci_rd_pci_cfg_msg - RdPCIConfig command
 * @addr: address of the client
 * @bus: PCI bus number
 * @device: PCI device number
 * @function: specific function to read from
 * @reg: specific register to read from
 * @cc: completion code
 * @pci_config: config data to be read
 *
 * The RdPCIConfig() command provides sideband read access to the PCI
 * configuration space maintained in downstream devices external to the
 * processor.
 */
struct peci_rd_pci_cfg_msg {
#define PECI_RDPCICFG_WRITE_LEN		6
#define PECI_RDPCICFG_READ_LEN		5
#define PECI_RDPCICFG_READ_LEN_MAX	24
#define PECI_RDPCICFG_CMD		0x61

	__u8	addr;
	__u8	bus;
#define PECI_PCI_BUS0_CPU0		0x00
#define PECI_PCI_BUS0_CPU1		0x80
#define PECI_PCI_CPUBUSNO_BUS		0x00
#define PECI_PCI_CPUBUSNO_DEV		0x08
#define PECI_PCI_CPUBUSNO_FUNC		0x02
#define PECI_PCI_CPUBUSNO		0xcc
#define PECI_PCI_CPUBUSNO_1		0xd0
#define PECI_PCI_CPUBUSNO_VALID		0xd4

	__u8	device;
	__u8	function;
	__u16	reg;
	__u8	cc;
	__u8	padding[1];
	__u8	pci_config[4];
} __attribute__((__packed__));

/**
 * struct peci_wr_pci_cfg_msg - WrPCIConfig command
 * @addr: address of the client
 * @bus: PCI bus number
 * @device: PCI device number
 * @function: specific function to write to
 * @reg: specific register to write to
 * @tx_len: number of data to be written in bytes
 * @cc: completion code
 * @pci_config: config data to be written
 *
 * The RdPCIConfig() command provides sideband write access to the PCI
 * configuration space maintained in downstream devices external to the
 * processor.
 */
struct peci_wr_pci_cfg_msg {
#define PECI_WRPCICFG_CMD		0x65

	__u8	addr;
	__u8	bus;
	__u8	device;
	__u8	function;
	__u16	reg;
	__u8	tx_len;
	__u8	cc;
	__u8	pci_config[4];
} __attribute__((__packed__));

/**
 * struct peci_rd_pci_cfg_local_msg - RdPCIConfigLocal command
 * @addr: address of the client
 * @bus: PCI bus number
 * @device: PCI device number
 * @function: specific function to read from
 * @reg: specific register to read from
 * @rx_len: number of data to be read in bytes
 * @cc: completion code
 * @pci_config: config data to be read
 *
 * The RdPCIConfigLocal() command provides sideband read access to the PCI
 * configuration space that resides within the processor. This includes all
 * processor IIO and uncore registers within the PCI configuration space.
 */
struct peci_rd_pci_cfg_local_msg {
#define PECI_RDPCICFGLOCAL_WRITE_LEN		5
#define PECI_RDPCICFGLOCAL_READ_LEN_BASE	1
#define PECI_RDPCICFGLOCAL_CMD			0xe1

	__u8	addr;
	__u8	bus;
	__u8	device;
	__u8	function;
	__u16	reg;
	__u8	rx_len;
	__u8	cc;
	__u8	pci_config[4];
} __attribute__((__packed__));

/**
 * struct peci_wr_pci_cfg_local_msg - WrPCIConfigLocal command
 * @addr: address of the client
 * @bus: PCI bus number
 * @device: PCI device number
 * @function: specific function to read from
 * @reg: specific register to read from
 * @tx_len: number of data to be written in bytes
 * @cc: completion code
 * @value: config data to be written
 *
 * The WrPCIConfigLocal() command provides sideband write access to the PCI
 * configuration space that resides within the processor. PECI originators can
 * access this space even before BIOS enumeration of the system buses.
 */
struct peci_wr_pci_cfg_local_msg {
#define PECI_WRPCICFGLOCAL_WRITE_LEN_BASE	6
#define PECI_WRPCICFGLOCAL_READ_LEN		1
#define PECI_WRPCICFGLOCAL_CMD			0xe5

	__u8	addr;
	__u8	bus;
	__u8	device;
	__u8	function;
	__u16	reg;
	__u8	tx_len;
	__u8	cc;
	__u32	value;
} __attribute__((__packed__));

struct peci_rd_end_pt_cfg_msg {
#define PECI_RDENDPTCFG_PCI_WRITE_LEN		12
#define PECI_RDENDPTCFG_MMIO_D_WRITE_LEN	14
#define PECI_RDENDPTCFG_MMIO_Q_WRITE_LEN	18
#define PECI_RDENDPTCFG_READ_LEN_BASE		1
#define PECI_RDENDPTCFG_CMD			0xc1

	__u8	addr;
	__u8	msg_type;
#define PECI_ENDPTCFG_TYPE_LOCAL_PCI		0x03
#define PECI_ENDPTCFG_TYPE_PCI			0x04
#define PECI_ENDPTCFG_TYPE_MMIO			0x05

	union {
		struct {
			__u8	seg;
			__u8	bus;
			__u8	device;
			__u8	function;
			__u16	reg;
		} pci_cfg;
		struct {
			__u8	seg;
			__u8	bus;
			__u8	device;
			__u8	function;
			__u8	bar;
			__u8	addr_type;
#define PECI_ENDPTCFG_ADDR_TYPE_PCI		0x04
#define PECI_ENDPTCFG_ADDR_TYPE_MMIO_D		0x05
#define PECI_ENDPTCFG_ADDR_TYPE_MMIO_Q		0x06

			__u64	offset;
		} mmio;
	} params;
	__u8	rx_len;
	__u8	cc;
	__u8	padding[2];
	__u8	data[8];
} __attribute__((__packed__));

struct peci_wr_end_pt_cfg_msg {
#define PECI_WRENDPTCFG_PCI_WRITE_LEN_BASE	13
#define PECI_WRENDPTCFG_MMIO_D_WRITE_LEN_BASE	15
#define PECI_WRENDPTCFG_MMIO_Q_WRITE_LEN_BASE	19
#define PECI_WRENDPTCFG_READ_LEN		1
#define PECI_WRENDPTCFG_CMD			0xc5

	__u8	addr;
	__u8	msg_type;
	/* See msg_type in struct peci_rd_end_pt_cfg_msg */

	union {
		struct {
			__u8	seg;
			__u8	bus;
			__u8	device;
			__u8	function;
			__u16	reg;
		} pci_cfg;
		struct {
			__u8	seg;
			__u8	bus;
			__u8	device;
			__u8	function;
			__u8	bar;
			__u8	addr_type;
			/* See addr_type in struct peci_rd_end_pt_cfg_msg */

			__u64	offset;
		} mmio;
	} params;
	__u8	tx_len;
	__u8	cc;
	__u8	padding[2];
	__u64	value;
} __attribute__((__packed__));

/* Crashdump Agent */
#define PECI_CRASHDUMP_CORE		0x00
#define PECI_CRASHDUMP_TOR		0x01

/* Crashdump Agent Param */
#define PECI_CRASHDUMP_PAYLOAD_SIZE	0x00

/* Crashdump Agent Data Param */
#define PECI_CRASHDUMP_AGENT_ID		0x00
#define PECI_CRASHDUMP_AGENT_PARAM	0x01

struct peci_crashdump_disc_msg {
	__u8	addr;
	__u8	subopcode;
#define PECI_CRASHDUMP_ENABLED		0x00
#define PECI_CRASHDUMP_NUM_AGENTS	0x01
#define PECI_CRASHDUMP_AGENT_DATA	0x02

	__u8	cc;
	__u8	param0;
	__u16	param1;
	__u8	param2;
	__u8	rx_len;
	__u8	data[8];
} __attribute__((__packed__));

struct peci_crashdump_get_frame_msg {
#define PECI_CRASHDUMP_DISC_WRITE_LEN		9
#define PECI_CRASHDUMP_DISC_READ_LEN_BASE	1
#define PECI_CRASHDUMP_DISC_VERSION		0
#define PECI_CRASHDUMP_DISC_OPCODE		1
#define PECI_CRASHDUMP_GET_FRAME_WRITE_LEN	10
#define PECI_CRASHDUMP_GET_FRAME_READ_LEN_BASE	1
#define PECI_CRASHDUMP_GET_FRAME_VERSION	0
#define PECI_CRASHDUMP_GET_FRAME_OPCODE		3
#define PECI_CRASHDUMP_CMD			0x71

	__u8	addr;
	__u8	padding0;
	__u16	param0;
	__u16	param1;
	__u16	param2;
	__u8	rx_len;
	__u8	cc;
	__u8	padding1[2];
	__u8	data[16];
} __attribute__((__packed__));

#define PECI_IOC_BASE	0xb8

#define PECI_IOC_XFER \
	_IOWR(PECI_IOC_BASE, PECI_CMD_XFER, struct peci_xfer_msg)

#define PECI_IOC_PING \
	_IOWR(PECI_IOC_BASE, PECI_CMD_PING, struct peci_ping_msg)

#define PECI_IOC_GET_DIB \
	_IOWR(PECI_IOC_BASE, PECI_CMD_GET_DIB, struct peci_get_dib_msg)

#define PECI_IOC_GET_TEMP \
	_IOWR(PECI_IOC_BASE, PECI_CMD_GET_TEMP, struct peci_get_temp_msg)

#define PECI_IOC_RD_PKG_CFG \
	_IOWR(PECI_IOC_BASE, PECI_CMD_RD_PKG_CFG, struct peci_rd_pkg_cfg_msg)

#define PECI_IOC_WR_PKG_CFG \
	_IOWR(PECI_IOC_BASE, PECI_CMD_WR_PKG_CFG, struct peci_wr_pkg_cfg_msg)

#define PECI_IOC_RD_IA_MSR \
	_IOWR(PECI_IOC_BASE, PECI_CMD_RD_IA_MSR, struct peci_rd_ia_msr_msg)

#define PECI_IOC_WR_IA_MSR \
	_IOWR(PECI_IOC_BASE, PECI_CMD_WR_IA_MSR, struct peci_wr_ia_msr_msg)

#define PECI_IOC_RD_IA_MSREX \
	_IOWR(PECI_IOC_BASE, PECI_CMD_RD_IA_MSREX, struct peci_rd_ia_msrex_msg)

#define PECI_IOC_RD_PCI_CFG \
	_IOWR(PECI_IOC_BASE, PECI_CMD_RD_PCI_CFG, struct peci_rd_pci_cfg_msg)

#define PECI_IOC_WR_PCI_CFG \
	_IOWR(PECI_IOC_BASE, PECI_CMD_WR_PCI_CFG, struct peci_wr_pci_cfg_msg)

#define PECI_IOC_RD_PCI_CFG_LOCAL \
	_IOWR(PECI_IOC_BASE, PECI_CMD_RD_PCI_CFG_LOCAL, \
	      struct peci_rd_pci_cfg_local_msg)

#define PECI_IOC_WR_PCI_CFG_LOCAL \
	_IOWR(PECI_IOC_BASE, PECI_CMD_WR_PCI_CFG_LOCAL, \
	      struct peci_wr_pci_cfg_local_msg)

#define PECI_IOC_RD_END_PT_CFG \
	_IOWR(PECI_IOC_BASE, PECI_CMD_RD_END_PT_CFG, \
	      struct peci_rd_end_pt_cfg_msg)

#define PECI_IOC_WR_END_PT_CFG \
	_IOWR(PECI_IOC_BASE, PECI_CMD_WR_END_PT_CFG, \
	      struct peci_wr_end_pt_cfg_msg)

#define PECI_IOC_CRASHDUMP_DISC \
	_IOWR(PECI_IOC_BASE, PECI_CMD_CRASHDUMP_DISC, \
	      struct peci_crashdump_disc_msg)

#define PECI_IOC_CRASHDUMP_GET_FRAME \
	_IOWR(PECI_IOC_BASE, PECI_CMD_CRASHDUMP_GET_FRAME, \
	      struct peci_crashdump_get_frame_msg)

#endif /* __PECI_IOCTL_H */
