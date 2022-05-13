/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2018 Cadence Design Systems Inc.
 *
 * Author: Boris Brezillon <boris.brezillon@bootlin.com>
 */

#ifndef I3C_CCC_H
#define I3C_CCC_H

#include <linux/bitops.h>
#include <linux/i3c/device.h>

/* I3C CCC (Common Command Codes) related definitions */
#define I3C_CCC_DIRECT			BIT(7)

#define I3C_CCC_ID(id, broadcast)	\
	((id) | ((broadcast) ? 0 : I3C_CCC_DIRECT))

/* Commands valid in both broadcast and unicast modes */
#define I3C_CCC_ENEC(broadcast)		I3C_CCC_ID(0x0, broadcast)
#define I3C_CCC_DISEC(broadcast)	I3C_CCC_ID(0x1, broadcast)
#define I3C_CCC_ENTAS(as, broadcast)	I3C_CCC_ID(0x2 + (as), broadcast)
#define I3C_CCC_RSTDAA(broadcast)	I3C_CCC_ID(0x6, broadcast)
#define I3C_CCC_SETMWL(broadcast)	I3C_CCC_ID(0x9, broadcast)
#define I3C_CCC_SETMRL(broadcast)	I3C_CCC_ID(0xa, broadcast)
#define I3C_CCC_SETXTIME(broadcast)	((broadcast) ? 0x28 : 0x98)
#define I3C_CCC_VENDOR(id, broadcast)	((id) + ((broadcast) ? 0x61 : 0xe0))

/* Broadcast-only commands */
#define I3C_CCC_ENTDAA			I3C_CCC_ID(0x7, true)
#define I3C_CCC_DEFSLVS			I3C_CCC_ID(0x8, true)
#define I3C_CCC_ENTTM			I3C_CCC_ID(0xb, true)
#define I3C_CCC_ENTHDR(x)		I3C_CCC_ID(0x20 + (x), true)

/* Unicast-only commands */
#define I3C_CCC_SETDASA			I3C_CCC_ID(0x7, false)
#define I3C_CCC_SETNEWDA		I3C_CCC_ID(0x8, false)
#define I3C_CCC_GETMWL			I3C_CCC_ID(0xb, false)
#define I3C_CCC_GETMRL			I3C_CCC_ID(0xc, false)
#define I3C_CCC_GETPID			I3C_CCC_ID(0xd, false)
#define I3C_CCC_GETBCR			I3C_CCC_ID(0xe, false)
#define I3C_CCC_GETDCR			I3C_CCC_ID(0xf, false)
#define I3C_CCC_GETSTATUS		I3C_CCC_ID(0x10, false)
#define I3C_CCC_GETACCMST		I3C_CCC_ID(0x11, false)
#define I3C_CCC_SETBRGTGT		I3C_CCC_ID(0x13, false)
#define I3C_CCC_GETMXDS			I3C_CCC_ID(0x14, false)
#define I3C_CCC_GETHDRCAP		I3C_CCC_ID(0x15, false)
#define I3C_CCC_GETXTIME		I3C_CCC_ID(0x19, false)

#define I3C_CCC_EVENT_SIR		BIT(0)
#define I3C_CCC_EVENT_MR		BIT(1)
#define I3C_CCC_EVENT_HJ		BIT(3)

/**
 * struct i3c_ccc_events - payload passed to ENEC/DISEC CCC
 *
 * @events: bitmask of I3C_CCC_EVENT_xxx events.
 *
 * Depending on the CCC command, the specific events coming from all devices
 * (broadcast version) or a specific device (unicast version) will be
 * enabled (ENEC) or disabled (DISEC).
 */
struct i3c_ccc_events {
	u8 events;
};

/**
 * struct i3c_ccc_mwl - payload passed to SETMWL/GETMWL CCC
 *
 * @len: maximum write length in bytes
 *
 * The maximum write length is only applicable to SDR private messages or
 * extended Write CCCs (like SETXTIME).
 */
struct i3c_ccc_mwl {
	__be16 len;
};

/**
 * struct i3c_ccc_mrl - payload passed to SETMRL/GETMRL CCC
 *
 * @len: maximum read length in bytes
 * @ibi_len: maximum IBI payload length
 *
 * The maximum read length is only applicable to SDR private messages or
 * extended Read CCCs (like GETXTIME).
 * The IBI length is only valid if the I3C slave is IBI capable
 * (%I3C_BCR_IBI_REQ_CAP is set).
 */
struct i3c_ccc_mrl {
	__be16 read_len;
	u8 ibi_len;
} __packed;

/**
 * struct i3c_ccc_dev_desc - I3C/I2C device descriptor used for DEFSLVS
 *
 * @dyn_addr: dynamic address assigned to the I3C slave or 0 if the entry is
 *	      describing an I2C slave.
 * @dcr: DCR value (not applicable to entries describing I2C devices)
 * @lvr: LVR value (not applicable to entries describing I3C devices)
 * @bcr: BCR value or 0 if this entry is describing an I2C slave
 * @static_addr: static address or 0 if the device does not have a static
 *		 address
 *
 * The DEFSLVS command should be passed an array of i3c_ccc_dev_desc
 * descriptors (one entry per I3C/I2C dev controlled by the master).
 */
struct i3c_ccc_dev_desc {
	u8 dyn_addr;
	union {
		u8 dcr;
		u8 lvr;
	};
	u8 bcr;
	u8 static_addr;
};

/**
 * struct i3c_ccc_defslvs - payload passed to DEFSLVS CCC
 *
 * @count: number of dev descriptors
 * @master: descriptor describing the current master
 * @slaves: array of descriptors describing slaves controlled by the
 *	    current master
 *
 * Information passed to the broadcast DEFSLVS to propagate device
 * information to all masters currently acting as slaves on the bus.
 * This is only meaningful if you have more than one master.
 */
struct i3c_ccc_defslvs {
	u8 count;
	struct i3c_ccc_dev_desc master;
	struct i3c_ccc_dev_desc slaves[];
} __packed;

/**
 * enum i3c_ccc_test_mode - enum listing all available test modes
 *
 * @I3C_CCC_EXIT_TEST_MODE: exit test mode
 * @I3C_CCC_VENDOR_TEST_MODE: enter vendor test mode
 */
enum i3c_ccc_test_mode {
	I3C_CCC_EXIT_TEST_MODE,
	I3C_CCC_VENDOR_TEST_MODE,
};

/**
 * struct i3c_ccc_enttm - payload passed to ENTTM CCC
 *
 * @mode: one of the &enum i3c_ccc_test_mode modes
 *
 * Information passed to the ENTTM CCC to instruct an I3C device to enter a
 * specific test mode.
 */
struct i3c_ccc_enttm {
	u8 mode;
};

/**
 * struct i3c_ccc_setda - payload passed to SETNEWDA and SETDASA CCCs
 *
 * @addr: dynamic address to assign to an I3C device
 *
 * Information passed to the SETNEWDA and SETDASA CCCs to assign/change the
 * dynamic address of an I3C device.
 */
struct i3c_ccc_setda {
	u8 addr;
};

/**
 * struct i3c_ccc_getpid - payload passed to GETPID CCC
 *
 * @pid: 48 bits PID in big endian
 */
struct i3c_ccc_getpid {
	u8 pid[6];
};

/**
 * struct i3c_ccc_getbcr - payload passed to GETBCR CCC
 *
 * @bcr: BCR (Bus Characteristic Register) value
 */
struct i3c_ccc_getbcr {
	u8 bcr;
};

/**
 * struct i3c_ccc_getdcr - payload passed to GETDCR CCC
 *
 * @dcr: DCR (Device Characteristic Register) value
 */
struct i3c_ccc_getdcr {
	u8 dcr;
};

#define I3C_CCC_STATUS_PENDING_INT(status)	((status) & GENMASK(3, 0))
#define I3C_CCC_STATUS_PROTOCOL_ERROR		BIT(5)
#define I3C_CCC_STATUS_ACTIVITY_MODE(status)	\
	(((status) & GENMASK(7, 6)) >> 6)

/**
 * struct i3c_ccc_getstatus - payload passed to GETSTATUS CCC
 *
 * @status: status of the I3C slave (see I3C_CCC_STATUS_xxx macros for more
 *	    information).
 */
struct i3c_ccc_getstatus {
	__be16 status;
};

/**
 * struct i3c_ccc_getaccmst - payload passed to GETACCMST CCC
 *
 * @newmaster: address of the master taking bus ownership
 */
struct i3c_ccc_getaccmst {
	u8 newmaster;
};

/**
 * struct i3c_ccc_bridged_slave_desc - bridged slave descriptor
 *
 * @addr: dynamic address of the bridged device
 * @id: ID of the slave device behind the bridge
 */
struct i3c_ccc_bridged_slave_desc {
	u8 addr;
	__be16 id;
} __packed;

/**
 * struct i3c_ccc_setbrgtgt - payload passed to SETBRGTGT CCC
 *
 * @count: number of bridged slaves
 * @bslaves: bridged slave descriptors
 */
struct i3c_ccc_setbrgtgt {
	u8 count;
	struct i3c_ccc_bridged_slave_desc bslaves[];
} __packed;

/**
 * enum i3c_sdr_max_data_rate - max data rate values for private SDR transfers
 */
enum i3c_sdr_max_data_rate {
	I3C_SDR0_FSCL_MAX,
	I3C_SDR1_FSCL_8MHZ,
	I3C_SDR2_FSCL_6MHZ,
	I3C_SDR3_FSCL_4MHZ,
	I3C_SDR4_FSCL_2MHZ,
};

/**
 * enum i3c_tsco - clock to data turn-around
 */
enum i3c_tsco {
	I3C_TSCO_8NS,
	I3C_TSCO_9NS,
	I3C_TSCO_10NS,
	I3C_TSCO_11NS,
	I3C_TSCO_12NS,
};

#define I3C_CCC_MAX_SDR_FSCL_MASK	GENMASK(2, 0)
#define I3C_CCC_MAX_SDR_FSCL(x)		((x) & I3C_CCC_MAX_SDR_FSCL_MASK)

/**
 * struct i3c_ccc_getmxds - payload passed to GETMXDS CCC
 *
 * @maxwr: write limitations
 * @maxrd: read limitations
 * @maxrdturn: maximum read turn-around expressed micro-seconds and
 *	       little-endian formatted
 */
struct i3c_ccc_getmxds {
	u8 maxwr;
	u8 maxrd;
	u8 maxrdturn[3];
} __packed;

#define I3C_CCC_HDR_MODE(mode)		BIT(mode)

/**
 * struct i3c_ccc_gethdrcap - payload passed to GETHDRCAP CCC
 *
 * @modes: bitmap of supported HDR modes
 */
struct i3c_ccc_gethdrcap {
	u8 modes;
} __packed;

/**
 * enum i3c_ccc_setxtime_subcmd - SETXTIME sub-commands
 */
enum i3c_ccc_setxtime_subcmd {
	I3C_CCC_SETXTIME_ST = 0x7f,
	I3C_CCC_SETXTIME_DT = 0xbf,
	I3C_CCC_SETXTIME_ENTER_ASYNC_MODE0 = 0xdf,
	I3C_CCC_SETXTIME_ENTER_ASYNC_MODE1 = 0xef,
	I3C_CCC_SETXTIME_ENTER_ASYNC_MODE2 = 0xf7,
	I3C_CCC_SETXTIME_ENTER_ASYNC_MODE3 = 0xfb,
	I3C_CCC_SETXTIME_ASYNC_TRIGGER = 0xfd,
	I3C_CCC_SETXTIME_TPH = 0x3f,
	I3C_CCC_SETXTIME_TU = 0x9f,
	I3C_CCC_SETXTIME_ODR = 0x8f,
};

/**
 * struct i3c_ccc_setxtime - payload passed to SETXTIME CCC
 *
 * @subcmd: one of the sub-commands ddefined in &enum i3c_ccc_setxtime_subcmd
 * @data: sub-command payload. Amount of data is determined by
 *	  &i3c_ccc_setxtime->subcmd
 */
struct i3c_ccc_setxtime {
	u8 subcmd;
	u8 data[];
} __packed;

#define I3C_CCC_GETXTIME_SYNC_MODE	BIT(0)
#define I3C_CCC_GETXTIME_ASYNC_MODE(x)	BIT((x) + 1)
#define I3C_CCC_GETXTIME_OVERFLOW	BIT(7)

/**
 * struct i3c_ccc_getxtime - payload retrieved from GETXTIME CCC
 *
 * @supported_modes: bitmap describing supported XTIME modes
 * @state: current status (enabled mode and overflow status)
 * @frequency: slave's internal oscillator frequency in 500KHz steps
 * @inaccuracy: slave's internal oscillator inaccuracy in 0.1% steps
 */
struct i3c_ccc_getxtime {
	u8 supported_modes;
	u8 state;
	u8 frequency;
	u8 inaccuracy;
} __packed;

/**
 * struct i3c_ccc_cmd_payload - CCC payload
 *
 * @len: payload length
 * @data: payload data. This buffer must be DMA-able
 */
struct i3c_ccc_cmd_payload {
	u16 len;
	void *data;
};

/**
 * struct i3c_ccc_cmd_dest - CCC command destination
 *
 * @addr: can be an I3C device address or the broadcast address if this is a
 *	  broadcast CCC
 * @payload: payload to be sent to this device or broadcasted
 */
struct i3c_ccc_cmd_dest {
	u8 addr;
	struct i3c_ccc_cmd_payload payload;
};

/**
 * struct i3c_ccc_cmd - CCC command
 *
 * @rnw: true if the CCC should retrieve data from the device. Only valid for
 *	 unicast commands
 * @id: CCC command id
 * @ndests: number of destinations. Should always be one for broadcast commands
 * @dests: array of destinations and associated payload for this CCC. Most of
 *	   the time, only one destination is provided
 * @err: I3C error code
 */
struct i3c_ccc_cmd {
	u8 rnw;
	u8 id;
	unsigned int ndests;
	struct i3c_ccc_cmd_dest *dests;
	enum i3c_error_code err;
};

#endif /* I3C_CCC_H */
