/*-
 * Copyright (c) 2005 Hans Petter Selasky
 * All rights reserved.
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
 *	$FreeBSD$
 */

#ifndef _ACPI_SMBUS_H_
#define _ACPI_SMBUS_H_

enum {
        SMBUS_WRITE_QUICK = 2,
        SMBUS_READ_QUICK = 3,
        SMBUS_SEND_BYTE = 4,
        SMBUS_RECEIVE_BYTE = 5,
        SMBUS_WRITE_BYTE = 6,
        SMBUS_READ_BYTE = 7,
        SMBUS_WRITE_WORD  = 8,
        SMBUS_READ_WORD  = 9,
        SMBUS_WRITE_BLOCK = 0xa,
        SMBUS_READ_BLOCK = 0xb,
        SMBUS_PROCESS_CALL = 0xc,
        SMBUS_BLOCK_PROCESS_CALL = 0xd,
};

/*
 * System Management Bus register offsets
 */
#define SMBUS_PRTCL		0
#define SMBUS_STS		1
#define SMBUS_STS_MASK		0x1f
#define SMBUS_ADDR		2
#define SMBUS_CMD		3
#define SMBUS_DATA		4	/* SMBUS_DATA_SIZE bytes */
#define SMBUS_DATA_SIZE		32
#define SMBUS_BCNT		36
#define SMBUS_ALRM_ADDR		37
#define SMBUS_ALRM_DATA		38	/* 2 bytes */

/*
 * Smart-Battery commands and definitions
 */

/* Base address */
#define SMBATT_ADDRESS		0x16


/* access: READ WRITE WORD */
#define SMBATT_CMD_MANUFACTURER_ACCESS		0

/*
 * access: READ WRITE WORD
 * unit  : mAh (CAPACITY_MODE=0) or 10 mWh (CAPACITY_MODE=1)
 * range : 0 .. 65535 inclusively
 */
#define SMBATT_CMD_REMAINING_CAPACITY_ALARM	0x1

/*
 * access: READ WRITE WORD
 * unit  : minutes
 * range : 0 .. 65535 inclusively
 */
#define SMBATT_CMD_REMAINING_TIME_ALARM		0x2

/* access: READ WRITE WORD */
#define SMBATT_CMD_BATTERY_MODE			0x3

#define SMBATT_BM_INTERNAL_CHARGE_CONTROLLER	(1 <<  0) /* READ */
#define SMBATT_BM_PRIMARY_BATTERY_SUPPORT	(1 <<  1) /* READ */
#define SMBATT_BM_CONDITION_FLAG		(1 <<  7) /* READ */
#define SMBATT_BM_CHARGE_CONTROLLER_ENABLED	(1 <<  8) /* READ WRITE */
#define SMBATT_BM_PRIMARY_BATTERY		(1 <<  9) /* READ WRITE */
#define SMBATT_BM_ALARM_MODE			(1 << 13) /* READ WRITE */
#define SMBATT_BM_CHARGER_MODE			(1 << 14) /* READ WRITE */
#define SMBATT_BM_CAPACITY_MODE			(1 << 15) /* READ WRITE */

/*
 * access: READ WRITE WORD
 * unit  : mAh (CAPACITY_MODE=0) or 10 mWh (CAPACITY_MODE=1)
 * range : signed WORD
 */
#define SMBATT_CMD_AT_RATE			0x4

/*
 * access: READ WORD
 * unit  : minutes
 * range : 0 .. 65534, 65535 has special meaning
 */
#define SMBATT_CMD_AT_RATE_TIME_TO_FULL		0x5

/*
 * access: READ WORD
 * unit  : minutes
 * range : 0 .. 65534, 65535 has special meaning
 */
#define SMBATT_CMD_AT_RATE_TIME_TO_EMPTY	0x6

/*
 * access: READ WORD */
#define SMBATT_CMD_AT_RATE_OK			0x7

/*
 * access: READ WORD
 * unit  : 0.1 degrees Kelvin
 * range : 0 .. 6553.5 Kelvin
 */
#define SMBATT_CMD_TEMPERATURE			0x8

/*
 * access: READ WORD
 * unit  : mV
 * range : 0 .. 65535 inclusively
 */
#define SMBATT_CMD_VOLTAGE			0x9

/*
 * access: READ WORD
 * unit  : mA
 * range : signed WORD
 */
#define SMBATT_CMD_CURRENT			0xa

/*
 * access: READ WORD
 * unit  : mA
 * range : signed WORD
 */
#define SMBATT_CMD_AVERAGE_CURRENT		0xb

/*
 * access: READ WORD
 * unit  : percent
 * range : 0..100 inclusively
 */
#define SMBATT_CMD_MAX_ERROR			0xc

/*
 * access: READ WORD
 * unit  : percent
 * range : 0..100 inclusively
 */
#define SMBATT_CMD_RELATIVE_STATE_OF_CHARGE	0xd

/*
 * access: READ WORD
 * unit  : percent
 * range : 0..100 inclusively
 */
#define SMBATT_CMD_ABSOLUTE_STATE_OF_CHARGE	0xe

/*
 * access: READ WORD
 * unit  : mAh (CAPACITY_MODE=0) or 10 mWh (CAPACITY_MODE=1)
 * range : 0..65535 inclusively
 */
#define SMBATT_CMD_REMAINING_CAPACITY		0xf

/*
 * access: READ WORD
 * unit  : mAh (CAPACITY_MODE=0) or 10 mWh (CAPACITY_MODE=1)
 * range : 0..65535 inclusively
 */
#define SMBATT_CMD_FULL_CHARGE_CAPACITY		0x10

/*
 * access: READ WORD
 * unit  : minutes
 * range : 0..65534, 65535 is reserved
 */
#define SMBATT_CMD_RUN_TIME_TO_EMPTY		0x11

/*
 * access: READ WORD
 * unit  : minutes
 * range : 0..65534, 65535 is reserved
 */
#define SMBATT_CMD_AVERAGE_TIME_TO_EMPTY	0x12

/*
 * access: READ WORD
 * unit  : minutes
 * range : 0..65534, 65535 is reserved
 */
#define SMBATT_CMD_AVERAGE_TIME_TO_FULL		0x13

/*
 * access: READ WORD
 * unit  : mA
 */
#define SMBATT_CMD_CHARGING_CURRENT		0x14

/*
 * access: READ WORD
 * unit  : mV
 * range : 0 .. 65534, 65535 reserved
 */
#define SMBATT_CMD_CHARGING_VOLTAGE		0x15

/* access: READ WORD */
#define SMBATT_CMD_BATTERY_STATUS		0x16

/* alarm bits */
#define SMBATT_BS_OVER_CHARGED_ALARM		(1 << 15)
#define SMBATT_BS_TERMINATE_CHARGE_ALARM	(1 << 14)
#define SMBATT_BS_RESERVED_2			(1 << 13)
#define SMBATT_BS_OVER_TEMP_ALARM		(1 << 12)
#define SMBATT_BS_TERMINATE_DISCHARGE_ALARM	(1 << 11)
#define SMBATT_BS_RESERVED_1			(1 << 10)
#define SMBATT_BS_REMAINING_CAPACITY_ALARM     	(1 << 9)
#define SMBATT_BS_REMAINING_TIME_ALARM		(1 << 8)

/* status bits */
#define SMBATT_BS_INITIALIZED			(1 << 7)
#define SMBATT_BS_DISCHARGING			(1 << 6)
#define SMBATT_BS_FULLY_CHARGED			(1 << 5)
#define SMBATT_BS_FULLY_DISCHARGED		(1 << 4)

/* error bits */
#define SMBATT_BS_GET_ERROR(x)			((x) & 0xf)
#define SMBATT_BS_ERROR_OK			0
#define SMBATT_BS_ERROR_BUSY			1
#define SMBATT_BS_ERROR_RESERVED_COMMAND	2
#define SMBATT_BS_ERROR_UNSUPPORTED_COMMAND	3
#define SMBATT_BS_ERROR_ACCESS_DENIED		4
#define SMBATT_BS_ERROR_OVER_UNDER_FLOW		5
#define SMBATT_BS_ERROR_BADSIZE			6
#define SMBATT_BS_ERROR_UNKNOWN			7

/*
 * access: READ WORD
 * unit  : cycle(s)
 * range : 0 .. 65534, 65535 reserved
 */
#define SMBATT_CMD_CYCLE_COUNT			0x17

/*
 * access: READ WORD
 * unit  : mAh (CAPACITY_MODE=0) or 10 mWh (CAPACITY_MODE=1)
 * range : 0..65535 inclusively
 */
#define SMBATT_CMD_DESIGN_CAPACITY		0x18

/*
 * access: READ WORD
 * unit  : mV
 * range : 0..65535 mV
 */
#define SMBATT_CMD_DESIGN_VOLTAGE		0x19

/* access: READ WORD */
#define SMBATT_CMD_SPECIFICATION_INFO		0x1a

#define SMBATT_SI_GET_REVISION(x)	(((x) >>  0) & 0xf)
#define SMBATT_SI_GET_VERSION(x)	(((x) >>  4) & 0xf)
#define SMBATT_SI_GET_VSCALE(x)		(((x) >>  8) & 0xf)
#define SMBATT_SI_GET_IPSCALE(x)	(((x) >> 12) & 0xf)

/* access: READ WORD */
#define SMBATT_CMD_MANUFACTURE_DATE 		0x1b

#define SMBATT_MD_GET_DAY(x)		 (((x) >> 0) & 0x1f)
#define SMBATT_MD_GET_MONTH(x)		 (((x) >> 5) & 0xf)
#define SMBATT_MD_GET_YEAR(x)		((((x) >> 9) & 0x7f) + 1980)

/* access: READ WORD */
#define SMBATT_CMD_SERIAL_NUMBER		0x1c

/* access: READ BLOCK */
#define SMBATT_CMD_MANUFACTURER_NAME		0x20

/* access: READ BLOCK */
#define SMBATT_CMD_DEVICE_NAME			0x21

/* access: READ BLOCK */
#define SMBATT_CMD_DEVICE_CHEMISTRY		0x22

/* access: READ BLOCK */
#define SMBATT_CMD_MANUFACTURER_DATA		0x23

#endif /* !_ACPI_SMBUS_H_ */
