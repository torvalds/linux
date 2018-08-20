/*
 * Host communication command constants for ChromeOS EC
 *
 * Copyright (C) 2012 Google, Inc
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * The ChromeOS EC multi function device is used to mux all the requests
 * to the EC device for its multiple features: keyboard controller,
 * battery charging and regulator control, firmware update.
 *
 * NOTE: This file is copied verbatim from the ChromeOS EC Open Source
 * project in an attempt to make future updates easy to make.
 */

#ifndef __CROS_EC_COMMANDS_H
#define __CROS_EC_COMMANDS_H

/*
 * Current version of this protocol
 *
 * TODO(crosbug.com/p/11223): This is effectively useless; protocol is
 * determined in other ways.  Remove this once the kernel code no longer
 * depends on it.
 */
#define EC_PROTO_VERSION          0x00000002

/* Command version mask */
#define EC_VER_MASK(version) (1UL << (version))

/* I/O addresses for ACPI commands */
#define EC_LPC_ADDR_ACPI_DATA  0x62
#define EC_LPC_ADDR_ACPI_CMD   0x66

/* I/O addresses for host command */
#define EC_LPC_ADDR_HOST_DATA  0x200
#define EC_LPC_ADDR_HOST_CMD   0x204

/* I/O addresses for host command args and params */
/* Protocol version 2 */
#define EC_LPC_ADDR_HOST_ARGS    0x800  /* And 0x801, 0x802, 0x803 */
#define EC_LPC_ADDR_HOST_PARAM   0x804  /* For version 2 params; size is
					 * EC_PROTO2_MAX_PARAM_SIZE */
/* Protocol version 3 */
#define EC_LPC_ADDR_HOST_PACKET  0x800  /* Offset of version 3 packet */
#define EC_LPC_HOST_PACKET_SIZE  0x100  /* Max size of version 3 packet */

/* The actual block is 0x800-0x8ff, but some BIOSes think it's 0x880-0x8ff
 * and they tell the kernel that so we have to think of it as two parts. */
#define EC_HOST_CMD_REGION0    0x800
#define EC_HOST_CMD_REGION1    0x880
#define EC_HOST_CMD_REGION_SIZE 0x80

/* EC command register bit functions */
#define EC_LPC_CMDR_DATA	(1 << 0)  /* Data ready for host to read */
#define EC_LPC_CMDR_PENDING	(1 << 1)  /* Write pending to EC */
#define EC_LPC_CMDR_BUSY	(1 << 2)  /* EC is busy processing a command */
#define EC_LPC_CMDR_CMD		(1 << 3)  /* Last host write was a command */
#define EC_LPC_CMDR_ACPI_BRST	(1 << 4)  /* Burst mode (not used) */
#define EC_LPC_CMDR_SCI		(1 << 5)  /* SCI event is pending */
#define EC_LPC_CMDR_SMI		(1 << 6)  /* SMI event is pending */

#define EC_LPC_ADDR_MEMMAP       0x900
#define EC_MEMMAP_SIZE         255 /* ACPI IO buffer max is 255 bytes */
#define EC_MEMMAP_TEXT_MAX     8   /* Size of a string in the memory map */

/* The offset address of each type of data in mapped memory. */
#define EC_MEMMAP_TEMP_SENSOR      0x00 /* Temp sensors 0x00 - 0x0f */
#define EC_MEMMAP_FAN              0x10 /* Fan speeds 0x10 - 0x17 */
#define EC_MEMMAP_TEMP_SENSOR_B    0x18 /* More temp sensors 0x18 - 0x1f */
#define EC_MEMMAP_ID               0x20 /* 0x20 == 'E', 0x21 == 'C' */
#define EC_MEMMAP_ID_VERSION       0x22 /* Version of data in 0x20 - 0x2f */
#define EC_MEMMAP_THERMAL_VERSION  0x23 /* Version of data in 0x00 - 0x1f */
#define EC_MEMMAP_BATTERY_VERSION  0x24 /* Version of data in 0x40 - 0x7f */
#define EC_MEMMAP_SWITCHES_VERSION 0x25 /* Version of data in 0x30 - 0x33 */
#define EC_MEMMAP_EVENTS_VERSION   0x26 /* Version of data in 0x34 - 0x3f */
#define EC_MEMMAP_HOST_CMD_FLAGS   0x27 /* Host cmd interface flags (8 bits) */
/* Unused 0x28 - 0x2f */
#define EC_MEMMAP_SWITCHES         0x30	/* 8 bits */
/* Unused 0x31 - 0x33 */
#define EC_MEMMAP_HOST_EVENTS      0x34 /* 32 bits */
/* Reserve 0x38 - 0x3f for additional host event-related stuff */
/* Battery values are all 32 bits */
#define EC_MEMMAP_BATT_VOLT        0x40 /* Battery Present Voltage */
#define EC_MEMMAP_BATT_RATE        0x44 /* Battery Present Rate */
#define EC_MEMMAP_BATT_CAP         0x48 /* Battery Remaining Capacity */
#define EC_MEMMAP_BATT_FLAG        0x4c /* Battery State, defined below */
#define EC_MEMMAP_BATT_DCAP        0x50 /* Battery Design Capacity */
#define EC_MEMMAP_BATT_DVLT        0x54 /* Battery Design Voltage */
#define EC_MEMMAP_BATT_LFCC        0x58 /* Battery Last Full Charge Capacity */
#define EC_MEMMAP_BATT_CCNT        0x5c /* Battery Cycle Count */
/* Strings are all 8 bytes (EC_MEMMAP_TEXT_MAX) */
#define EC_MEMMAP_BATT_MFGR        0x60 /* Battery Manufacturer String */
#define EC_MEMMAP_BATT_MODEL       0x68 /* Battery Model Number String */
#define EC_MEMMAP_BATT_SERIAL      0x70 /* Battery Serial Number String */
#define EC_MEMMAP_BATT_TYPE        0x78 /* Battery Type String */
#define EC_MEMMAP_ALS              0x80 /* ALS readings in lux (2 X 16 bits) */
/* Unused 0x84 - 0x8f */
#define EC_MEMMAP_ACC_STATUS       0x90 /* Accelerometer status (8 bits )*/
/* Unused 0x91 */
#define EC_MEMMAP_ACC_DATA         0x92 /* Accelerometer data 0x92 - 0x9f */
#define EC_MEMMAP_GYRO_DATA        0xa0 /* Gyroscope data 0xa0 - 0xa5 */
/* Unused 0xa6 - 0xfe (remember, 0xff is NOT part of the memmap region) */


/* Define the format of the accelerometer mapped memory status byte. */
#define EC_MEMMAP_ACC_STATUS_SAMPLE_ID_MASK  0x0f
#define EC_MEMMAP_ACC_STATUS_BUSY_BIT        (1 << 4)
#define EC_MEMMAP_ACC_STATUS_PRESENCE_BIT    (1 << 7)

/* Number of temp sensors at EC_MEMMAP_TEMP_SENSOR */
#define EC_TEMP_SENSOR_ENTRIES     16
/*
 * Number of temp sensors at EC_MEMMAP_TEMP_SENSOR_B.
 *
 * Valid only if EC_MEMMAP_THERMAL_VERSION returns >= 2.
 */
#define EC_TEMP_SENSOR_B_ENTRIES      8

/* Special values for mapped temperature sensors */
#define EC_TEMP_SENSOR_NOT_PRESENT    0xff
#define EC_TEMP_SENSOR_ERROR          0xfe
#define EC_TEMP_SENSOR_NOT_POWERED    0xfd
#define EC_TEMP_SENSOR_NOT_CALIBRATED 0xfc
/*
 * The offset of temperature value stored in mapped memory.  This allows
 * reporting a temperature range of 200K to 454K = -73C to 181C.
 */
#define EC_TEMP_SENSOR_OFFSET      200

/*
 * Number of ALS readings at EC_MEMMAP_ALS
 */
#define EC_ALS_ENTRIES             2

/*
 * The default value a temperature sensor will return when it is present but
 * has not been read this boot.  This is a reasonable number to avoid
 * triggering alarms on the host.
 */
#define EC_TEMP_SENSOR_DEFAULT     (296 - EC_TEMP_SENSOR_OFFSET)

#define EC_FAN_SPEED_ENTRIES       4       /* Number of fans at EC_MEMMAP_FAN */
#define EC_FAN_SPEED_NOT_PRESENT   0xffff  /* Entry not present */
#define EC_FAN_SPEED_STALLED       0xfffe  /* Fan stalled */

/* Battery bit flags at EC_MEMMAP_BATT_FLAG. */
#define EC_BATT_FLAG_AC_PRESENT   0x01
#define EC_BATT_FLAG_BATT_PRESENT 0x02
#define EC_BATT_FLAG_DISCHARGING  0x04
#define EC_BATT_FLAG_CHARGING     0x08
#define EC_BATT_FLAG_LEVEL_CRITICAL 0x10

/* Switch flags at EC_MEMMAP_SWITCHES */
#define EC_SWITCH_LID_OPEN               0x01
#define EC_SWITCH_POWER_BUTTON_PRESSED   0x02
#define EC_SWITCH_WRITE_PROTECT_DISABLED 0x04
/* Was recovery requested via keyboard; now unused. */
#define EC_SWITCH_IGNORE1		 0x08
/* Recovery requested via dedicated signal (from servo board) */
#define EC_SWITCH_DEDICATED_RECOVERY     0x10
/* Was fake developer mode switch; now unused.  Remove in next refactor. */
#define EC_SWITCH_IGNORE0                0x20

/* Host command interface flags */
/* Host command interface supports LPC args (LPC interface only) */
#define EC_HOST_CMD_FLAG_LPC_ARGS_SUPPORTED  0x01
/* Host command interface supports version 3 protocol */
#define EC_HOST_CMD_FLAG_VERSION_3   0x02

/* Wireless switch flags */
#define EC_WIRELESS_SWITCH_ALL       ~0x00  /* All flags */
#define EC_WIRELESS_SWITCH_WLAN       0x01  /* WLAN radio */
#define EC_WIRELESS_SWITCH_BLUETOOTH  0x02  /* Bluetooth radio */
#define EC_WIRELESS_SWITCH_WWAN       0x04  /* WWAN power */
#define EC_WIRELESS_SWITCH_WLAN_POWER 0x08  /* WLAN power */

/*
 * This header file is used in coreboot both in C and ACPI code.  The ACPI code
 * is pre-processed to handle constants but the ASL compiler is unable to
 * handle actual C code so keep it separate.
 */
#ifndef __ACPI__

/*
 * Define __packed if someone hasn't beat us to it.  Linux kernel style
 * checking prefers __packed over __attribute__((packed)).
 */
#ifndef __packed
#define __packed __attribute__((packed))
#endif

/* LPC command status byte masks */
/* EC has written a byte in the data register and host hasn't read it yet */
#define EC_LPC_STATUS_TO_HOST     0x01
/* Host has written a command/data byte and the EC hasn't read it yet */
#define EC_LPC_STATUS_FROM_HOST   0x02
/* EC is processing a command */
#define EC_LPC_STATUS_PROCESSING  0x04
/* Last write to EC was a command, not data */
#define EC_LPC_STATUS_LAST_CMD    0x08
/* EC is in burst mode.  Unsupported by Chrome EC, so this bit is never set */
#define EC_LPC_STATUS_BURST_MODE  0x10
/* SCI event is pending (requesting SCI query) */
#define EC_LPC_STATUS_SCI_PENDING 0x20
/* SMI event is pending (requesting SMI query) */
#define EC_LPC_STATUS_SMI_PENDING 0x40
/* (reserved) */
#define EC_LPC_STATUS_RESERVED    0x80

/*
 * EC is busy.  This covers both the EC processing a command, and the host has
 * written a new command but the EC hasn't picked it up yet.
 */
#define EC_LPC_STATUS_BUSY_MASK \
	(EC_LPC_STATUS_FROM_HOST | EC_LPC_STATUS_PROCESSING)

/* Host command response codes */
enum ec_status {
	EC_RES_SUCCESS = 0,
	EC_RES_INVALID_COMMAND = 1,
	EC_RES_ERROR = 2,
	EC_RES_INVALID_PARAM = 3,
	EC_RES_ACCESS_DENIED = 4,
	EC_RES_INVALID_RESPONSE = 5,
	EC_RES_INVALID_VERSION = 6,
	EC_RES_INVALID_CHECKSUM = 7,
	EC_RES_IN_PROGRESS = 8,		/* Accepted, command in progress */
	EC_RES_UNAVAILABLE = 9,		/* No response available */
	EC_RES_TIMEOUT = 10,		/* We got a timeout */
	EC_RES_OVERFLOW = 11,		/* Table / data overflow */
	EC_RES_INVALID_HEADER = 12,     /* Header contains invalid data */
	EC_RES_REQUEST_TRUNCATED = 13,  /* Didn't get the entire request */
	EC_RES_RESPONSE_TOO_BIG = 14    /* Response was too big to handle */
};

/*
 * Host event codes.  Note these are 1-based, not 0-based, because ACPI query
 * EC command uses code 0 to mean "no event pending".  We explicitly specify
 * each value in the enum listing so they won't change if we delete/insert an
 * item or rearrange the list (it needs to be stable across platforms, not
 * just within a single compiled instance).
 */
enum host_event_code {
	EC_HOST_EVENT_LID_CLOSED = 1,
	EC_HOST_EVENT_LID_OPEN = 2,
	EC_HOST_EVENT_POWER_BUTTON = 3,
	EC_HOST_EVENT_AC_CONNECTED = 4,
	EC_HOST_EVENT_AC_DISCONNECTED = 5,
	EC_HOST_EVENT_BATTERY_LOW = 6,
	EC_HOST_EVENT_BATTERY_CRITICAL = 7,
	EC_HOST_EVENT_BATTERY = 8,
	EC_HOST_EVENT_THERMAL_THRESHOLD = 9,
	EC_HOST_EVENT_THERMAL_OVERLOAD = 10,
	EC_HOST_EVENT_THERMAL = 11,
	EC_HOST_EVENT_USB_CHARGER = 12,
	EC_HOST_EVENT_KEY_PRESSED = 13,
	/*
	 * EC has finished initializing the host interface.  The host can check
	 * for this event following sending a EC_CMD_REBOOT_EC command to
	 * determine when the EC is ready to accept subsequent commands.
	 */
	EC_HOST_EVENT_INTERFACE_READY = 14,
	/* Keyboard recovery combo has been pressed */
	EC_HOST_EVENT_KEYBOARD_RECOVERY = 15,

	/* Shutdown due to thermal overload */
	EC_HOST_EVENT_THERMAL_SHUTDOWN = 16,
	/* Shutdown due to battery level too low */
	EC_HOST_EVENT_BATTERY_SHUTDOWN = 17,

	/* Suggest that the AP throttle itself */
	EC_HOST_EVENT_THROTTLE_START = 18,
	/* Suggest that the AP resume normal speed */
	EC_HOST_EVENT_THROTTLE_STOP = 19,

	/* Hang detect logic detected a hang and host event timeout expired */
	EC_HOST_EVENT_HANG_DETECT = 20,
	/* Hang detect logic detected a hang and warm rebooted the AP */
	EC_HOST_EVENT_HANG_REBOOT = 21,
	/* PD MCU triggering host event */
	EC_HOST_EVENT_PD_MCU = 22,

	/* EC desires to change state of host-controlled USB mux */
	EC_HOST_EVENT_USB_MUX = 28,

	/* EC RTC event occurred */
	EC_HOST_EVENT_RTC = 26,

	/*
	 * The high bit of the event mask is not used as a host event code.  If
	 * it reads back as set, then the entire event mask should be
	 * considered invalid by the host.  This can happen when reading the
	 * raw event status via EC_MEMMAP_HOST_EVENTS but the LPC interface is
	 * not initialized on the EC, or improperly configured on the host.
	 */
	EC_HOST_EVENT_INVALID = 32
};
/* Host event mask */
#define EC_HOST_EVENT_MASK(event_code) (1UL << ((event_code) - 1))

/* Arguments at EC_LPC_ADDR_HOST_ARGS */
struct ec_lpc_host_args {
	uint8_t flags;
	uint8_t command_version;
	uint8_t data_size;
	/*
	 * Checksum; sum of command + flags + command_version + data_size +
	 * all params/response data bytes.
	 */
	uint8_t checksum;
} __packed;

/* Flags for ec_lpc_host_args.flags */
/*
 * Args are from host.  Data area at EC_LPC_ADDR_HOST_PARAM contains command
 * params.
 *
 * If EC gets a command and this flag is not set, this is an old-style command.
 * Command version is 0 and params from host are at EC_LPC_ADDR_OLD_PARAM with
 * unknown length.  EC must respond with an old-style response (that is,
 * withouth setting EC_HOST_ARGS_FLAG_TO_HOST).
 */
#define EC_HOST_ARGS_FLAG_FROM_HOST 0x01
/*
 * Args are from EC.  Data area at EC_LPC_ADDR_HOST_PARAM contains response.
 *
 * If EC responds to a command and this flag is not set, this is an old-style
 * response.  Command version is 0 and response data from EC is at
 * EC_LPC_ADDR_OLD_PARAM with unknown length.
 */
#define EC_HOST_ARGS_FLAG_TO_HOST   0x02

/*****************************************************************************/
/*
 * Byte codes returned by EC over SPI interface.
 *
 * These can be used by the AP to debug the EC interface, and to determine
 * when the EC is not in a state where it will ever get around to responding
 * to the AP.
 *
 * Example of sequence of bytes read from EC for a current good transfer:
 *   1. -                  - AP asserts chip select (CS#)
 *   2. EC_SPI_OLD_READY   - AP sends first byte(s) of request
 *   3. -                  - EC starts handling CS# interrupt
 *   4. EC_SPI_RECEIVING   - AP sends remaining byte(s) of request
 *   5. EC_SPI_PROCESSING  - EC starts processing request; AP is clocking in
 *                           bytes looking for EC_SPI_FRAME_START
 *   6. -                  - EC finishes processing and sets up response
 *   7. EC_SPI_FRAME_START - AP reads frame byte
 *   8. (response packet)  - AP reads response packet
 *   9. EC_SPI_PAST_END    - Any additional bytes read by AP
 *   10 -                  - AP deasserts chip select
 *   11 -                  - EC processes CS# interrupt and sets up DMA for
 *                           next request
 *
 * If the AP is waiting for EC_SPI_FRAME_START and sees any value other than
 * the following byte values:
 *   EC_SPI_OLD_READY
 *   EC_SPI_RX_READY
 *   EC_SPI_RECEIVING
 *   EC_SPI_PROCESSING
 *
 * Then the EC found an error in the request, or was not ready for the request
 * and lost data.  The AP should give up waiting for EC_SPI_FRAME_START,
 * because the EC is unable to tell when the AP is done sending its request.
 */

/*
 * Framing byte which precedes a response packet from the EC.  After sending a
 * request, the AP will clock in bytes until it sees the framing byte, then
 * clock in the response packet.
 */
#define EC_SPI_FRAME_START    0xec

/*
 * Padding bytes which are clocked out after the end of a response packet.
 */
#define EC_SPI_PAST_END       0xed

/*
 * EC is ready to receive, and has ignored the byte sent by the AP.  EC expects
 * that the AP will send a valid packet header (starting with
 * EC_COMMAND_PROTOCOL_3) in the next 32 bytes.
 */
#define EC_SPI_RX_READY       0xf8

/*
 * EC has started receiving the request from the AP, but hasn't started
 * processing it yet.
 */
#define EC_SPI_RECEIVING      0xf9

/* EC has received the entire request from the AP and is processing it. */
#define EC_SPI_PROCESSING     0xfa

/*
 * EC received bad data from the AP, such as a packet header with an invalid
 * length.  EC will ignore all data until chip select deasserts.
 */
#define EC_SPI_RX_BAD_DATA    0xfb

/*
 * EC received data from the AP before it was ready.  That is, the AP asserted
 * chip select and started clocking data before the EC was ready to receive it.
 * EC will ignore all data until chip select deasserts.
 */
#define EC_SPI_NOT_READY      0xfc

/*
 * EC was ready to receive a request from the AP.  EC has treated the byte sent
 * by the AP as part of a request packet, or (for old-style ECs) is processing
 * a fully received packet but is not ready to respond yet.
 */
#define EC_SPI_OLD_READY      0xfd

/*****************************************************************************/

/*
 * Protocol version 2 for I2C and SPI send a request this way:
 *
 *	0	EC_CMD_VERSION0 + (command version)
 *	1	Command number
 *	2	Length of params = N
 *	3..N+2	Params, if any
 *	N+3	8-bit checksum of bytes 0..N+2
 *
 * The corresponding response is:
 *
 *	0	Result code (EC_RES_*)
 *	1	Length of params = M
 *	2..M+1	Params, if any
 *	M+2	8-bit checksum of bytes 0..M+1
 */
#define EC_PROTO2_REQUEST_HEADER_BYTES 3
#define EC_PROTO2_REQUEST_TRAILER_BYTES 1
#define EC_PROTO2_REQUEST_OVERHEAD (EC_PROTO2_REQUEST_HEADER_BYTES +	\
				    EC_PROTO2_REQUEST_TRAILER_BYTES)

#define EC_PROTO2_RESPONSE_HEADER_BYTES 2
#define EC_PROTO2_RESPONSE_TRAILER_BYTES 1
#define EC_PROTO2_RESPONSE_OVERHEAD (EC_PROTO2_RESPONSE_HEADER_BYTES +	\
				     EC_PROTO2_RESPONSE_TRAILER_BYTES)

/* Parameter length was limited by the LPC interface */
#define EC_PROTO2_MAX_PARAM_SIZE 0xfc

/* Maximum request and response packet sizes for protocol version 2 */
#define EC_PROTO2_MAX_REQUEST_SIZE (EC_PROTO2_REQUEST_OVERHEAD +	\
				    EC_PROTO2_MAX_PARAM_SIZE)
#define EC_PROTO2_MAX_RESPONSE_SIZE (EC_PROTO2_RESPONSE_OVERHEAD +	\
				     EC_PROTO2_MAX_PARAM_SIZE)

/*****************************************************************************/

/*
 * Value written to legacy command port / prefix byte to indicate protocol
 * 3+ structs are being used.  Usage is bus-dependent.
 */
#define EC_COMMAND_PROTOCOL_3 0xda

#define EC_HOST_REQUEST_VERSION 3

/* Version 3 request from host */
struct ec_host_request {
	/* Struct version (=3)
	 *
	 * EC will return EC_RES_INVALID_HEADER if it receives a header with a
	 * version it doesn't know how to parse.
	 */
	uint8_t struct_version;

	/*
	 * Checksum of request and data; sum of all bytes including checksum
	 * should total to 0.
	 */
	uint8_t checksum;

	/* Command code */
	uint16_t command;

	/* Command version */
	uint8_t command_version;

	/* Unused byte in current protocol version; set to 0 */
	uint8_t reserved;

	/* Length of data which follows this header */
	uint16_t data_len;
} __packed;

#define EC_HOST_RESPONSE_VERSION 3

/* Version 3 response from EC */
struct ec_host_response {
	/* Struct version (=3) */
	uint8_t struct_version;

	/*
	 * Checksum of response and data; sum of all bytes including checksum
	 * should total to 0.
	 */
	uint8_t checksum;

	/* Result code (EC_RES_*) */
	uint16_t result;

	/* Length of data which follows this header */
	uint16_t data_len;

	/* Unused bytes in current protocol version; set to 0 */
	uint16_t reserved;
} __packed;

/*****************************************************************************/
/*
 * Notes on commands:
 *
 * Each command is an 16-bit command value.  Commands which take params or
 * return response data specify structs for that data.  If no struct is
 * specified, the command does not input or output data, respectively.
 * Parameter/response length is implicit in the structs.  Some underlying
 * communication protocols (I2C, SPI) may add length or checksum headers, but
 * those are implementation-dependent and not defined here.
 */

/*****************************************************************************/
/* General / test commands */

/*
 * Get protocol version, used to deal with non-backward compatible protocol
 * changes.
 */
#define EC_CMD_PROTO_VERSION 0x00

struct ec_response_proto_version {
	uint32_t version;
} __packed;

/*
 * Hello.  This is a simple command to test the EC is responsive to
 * commands.
 */
#define EC_CMD_HELLO 0x01

struct ec_params_hello {
	uint32_t in_data;  /* Pass anything here */
} __packed;

struct ec_response_hello {
	uint32_t out_data;  /* Output will be in_data + 0x01020304 */
} __packed;

/* Get version number */
#define EC_CMD_GET_VERSION 0x02

enum ec_current_image {
	EC_IMAGE_UNKNOWN = 0,
	EC_IMAGE_RO,
	EC_IMAGE_RW
};

struct ec_response_get_version {
	/* Null-terminated version strings for RO, RW */
	char version_string_ro[32];
	char version_string_rw[32];
	char reserved[32];       /* Was previously RW-B string */
	uint32_t current_image;  /* One of ec_current_image */
} __packed;

/* Read test */
#define EC_CMD_READ_TEST 0x03

struct ec_params_read_test {
	uint32_t offset;   /* Starting value for read buffer */
	uint32_t size;     /* Size to read in bytes */
} __packed;

struct ec_response_read_test {
	uint32_t data[32];
} __packed;

/*
 * Get build information
 *
 * Response is null-terminated string.
 */
#define EC_CMD_GET_BUILD_INFO 0x04

/* Get chip info */
#define EC_CMD_GET_CHIP_INFO 0x05

struct ec_response_get_chip_info {
	/* Null-terminated strings */
	char vendor[32];
	char name[32];
	char revision[32];  /* Mask version */
} __packed;

/* Get board HW version */
#define EC_CMD_GET_BOARD_VERSION 0x06

struct ec_response_board_version {
	uint16_t board_version;  /* A monotonously incrementing number. */
} __packed;

/*
 * Read memory-mapped data.
 *
 * This is an alternate interface to memory-mapped data for bus protocols
 * which don't support direct-mapped memory - I2C, SPI, etc.
 *
 * Response is params.size bytes of data.
 */
#define EC_CMD_READ_MEMMAP 0x07

struct ec_params_read_memmap {
	uint8_t offset;   /* Offset in memmap (EC_MEMMAP_*) */
	uint8_t size;     /* Size to read in bytes */
} __packed;

/* Read versions supported for a command */
#define EC_CMD_GET_CMD_VERSIONS 0x08

struct ec_params_get_cmd_versions {
	uint8_t cmd;      /* Command to check */
} __packed;

struct ec_params_get_cmd_versions_v1 {
	uint16_t cmd;     /* Command to check */
} __packed;

struct ec_response_get_cmd_versions {
	/*
	 * Mask of supported versions; use EC_VER_MASK() to compare with a
	 * desired version.
	 */
	uint32_t version_mask;
} __packed;

/*
 * Check EC communcations status (busy). This is needed on i2c/spi but not
 * on lpc since it has its own out-of-band busy indicator.
 *
 * lpc must read the status from the command register. Attempting this on
 * lpc will overwrite the args/parameter space and corrupt its data.
 */
#define EC_CMD_GET_COMMS_STATUS		0x09

/* Avoid using ec_status which is for return values */
enum ec_comms_status {
	EC_COMMS_STATUS_PROCESSING	= 1 << 0,	/* Processing cmd */
};

struct ec_response_get_comms_status {
	uint32_t flags;		/* Mask of enum ec_comms_status */
} __packed;

/* Fake a variety of responses, purely for testing purposes. */
#define EC_CMD_TEST_PROTOCOL		0x0a

/* Tell the EC what to send back to us. */
struct ec_params_test_protocol {
	uint32_t ec_result;
	uint32_t ret_len;
	uint8_t buf[32];
} __packed;

/* Here it comes... */
struct ec_response_test_protocol {
	uint8_t buf[32];
} __packed;

/* Get prococol information */
#define EC_CMD_GET_PROTOCOL_INFO	0x0b

/* Flags for ec_response_get_protocol_info.flags */
/* EC_RES_IN_PROGRESS may be returned if a command is slow */
#define EC_PROTOCOL_INFO_IN_PROGRESS_SUPPORTED (1 << 0)

struct ec_response_get_protocol_info {
	/* Fields which exist if at least protocol version 3 supported */

	/* Bitmask of protocol versions supported (1 << n means version n)*/
	uint32_t protocol_versions;

	/* Maximum request packet size, in bytes */
	uint16_t max_request_packet_size;

	/* Maximum response packet size, in bytes */
	uint16_t max_response_packet_size;

	/* Flags; see EC_PROTOCOL_INFO_* */
	uint32_t flags;
} __packed;


/*****************************************************************************/
/* Get/Set miscellaneous values */

/* The upper byte of .flags tells what to do (nothing means "get") */
#define EC_GSV_SET        0x80000000

/* The lower three bytes of .flags identifies the parameter, if that has
   meaning for an individual command. */
#define EC_GSV_PARAM_MASK 0x00ffffff

struct ec_params_get_set_value {
	uint32_t flags;
	uint32_t value;
} __packed;

struct ec_response_get_set_value {
	uint32_t flags;
	uint32_t value;
} __packed;

/* More than one command can use these structs to get/set paramters. */
#define EC_CMD_GSV_PAUSE_IN_S5	0x0c

/*****************************************************************************/
/* List the features supported by the firmware */
#define EC_CMD_GET_FEATURES  0x0d

/* Supported features */
enum ec_feature_code {
	/*
	 * This image contains a limited set of features. Another image
	 * in RW partition may support more features.
	 */
	EC_FEATURE_LIMITED = 0,
	/*
	 * Commands for probing/reading/writing/erasing the flash in the
	 * EC are present.
	 */
	EC_FEATURE_FLASH = 1,
	/*
	 * Can control the fan speed directly.
	 */
	EC_FEATURE_PWM_FAN = 2,
	/*
	 * Can control the intensity of the keyboard backlight.
	 */
	EC_FEATURE_PWM_KEYB = 3,
	/*
	 * Support Google lightbar, introduced on Pixel.
	 */
	EC_FEATURE_LIGHTBAR = 4,
	/* Control of LEDs  */
	EC_FEATURE_LED = 5,
	/* Exposes an interface to control gyro and sensors.
	 * The host goes through the EC to access these sensors.
	 * In addition, the EC may provide composite sensors, like lid angle.
	 */
	EC_FEATURE_MOTION_SENSE = 6,
	/* The keyboard is controlled by the EC */
	EC_FEATURE_KEYB = 7,
	/* The AP can use part of the EC flash as persistent storage. */
	EC_FEATURE_PSTORE = 8,
	/* The EC monitors BIOS port 80h, and can return POST codes. */
	EC_FEATURE_PORT80 = 9,
	/*
	 * Thermal management: include TMP specific commands.
	 * Higher level than direct fan control.
	 */
	EC_FEATURE_THERMAL = 10,
	/* Can switch the screen backlight on/off */
	EC_FEATURE_BKLIGHT_SWITCH = 11,
	/* Can switch the wifi module on/off */
	EC_FEATURE_WIFI_SWITCH = 12,
	/* Monitor host events, through for example SMI or SCI */
	EC_FEATURE_HOST_EVENTS = 13,
	/* The EC exposes GPIO commands to control/monitor connected devices. */
	EC_FEATURE_GPIO = 14,
	/* The EC can send i2c messages to downstream devices. */
	EC_FEATURE_I2C = 15,
	/* Command to control charger are included */
	EC_FEATURE_CHARGER = 16,
	/* Simple battery support. */
	EC_FEATURE_BATTERY = 17,
	/*
	 * Support Smart battery protocol
	 * (Common Smart Battery System Interface Specification)
	 */
	EC_FEATURE_SMART_BATTERY = 18,
	/* EC can dectect when the host hangs. */
	EC_FEATURE_HANG_DETECT = 19,
	/* Report power information, for pit only */
	EC_FEATURE_PMU = 20,
	/* Another Cros EC device is present downstream of this one */
	EC_FEATURE_SUB_MCU = 21,
	/* Support USB Power delivery (PD) commands */
	EC_FEATURE_USB_PD = 22,
	/* Control USB multiplexer, for audio through USB port for instance. */
	EC_FEATURE_USB_MUX = 23,
	/* Motion Sensor code has an internal software FIFO */
	EC_FEATURE_MOTION_SENSE_FIFO = 24,
	/* EC has RTC feature that can be controlled by host commands */
	EC_FEATURE_RTC = 27,
	/* EC supports CEC commands */
	EC_FEATURE_CEC = 35,
};

#define EC_FEATURE_MASK_0(event_code) (1UL << (event_code % 32))
#define EC_FEATURE_MASK_1(event_code) (1UL << (event_code - 32))
struct ec_response_get_features {
	uint32_t flags[2];
} __packed;

/*****************************************************************************/
/* Flash commands */

/* Get flash info */
#define EC_CMD_FLASH_INFO 0x10

/* Version 0 returns these fields */
struct ec_response_flash_info {
	/* Usable flash size, in bytes */
	uint32_t flash_size;
	/*
	 * Write block size.  Write offset and size must be a multiple
	 * of this.
	 */
	uint32_t write_block_size;
	/*
	 * Erase block size.  Erase offset and size must be a multiple
	 * of this.
	 */
	uint32_t erase_block_size;
	/*
	 * Protection block size.  Protection offset and size must be a
	 * multiple of this.
	 */
	uint32_t protect_block_size;
} __packed;

/* Flags for version 1+ flash info command */
/* EC flash erases bits to 0 instead of 1 */
#define EC_FLASH_INFO_ERASE_TO_0 (1 << 0)

/*
 * Version 1 returns the same initial fields as version 0, with additional
 * fields following.
 *
 * gcc anonymous structs don't seem to get along with the __packed directive;
 * if they did we'd define the version 0 struct as a sub-struct of this one.
 */
struct ec_response_flash_info_1 {
	/* Version 0 fields; see above for description */
	uint32_t flash_size;
	uint32_t write_block_size;
	uint32_t erase_block_size;
	uint32_t protect_block_size;

	/* Version 1 adds these fields: */
	/*
	 * Ideal write size in bytes.  Writes will be fastest if size is
	 * exactly this and offset is a multiple of this.  For example, an EC
	 * may have a write buffer which can do half-page operations if data is
	 * aligned, and a slower word-at-a-time write mode.
	 */
	uint32_t write_ideal_size;

	/* Flags; see EC_FLASH_INFO_* */
	uint32_t flags;
} __packed;

/*
 * Read flash
 *
 * Response is params.size bytes of data.
 */
#define EC_CMD_FLASH_READ 0x11

struct ec_params_flash_read {
	uint32_t offset;   /* Byte offset to read */
	uint32_t size;     /* Size to read in bytes */
} __packed;

/* Write flash */
#define EC_CMD_FLASH_WRITE 0x12
#define EC_VER_FLASH_WRITE 1

/* Version 0 of the flash command supported only 64 bytes of data */
#define EC_FLASH_WRITE_VER0_SIZE 64

struct ec_params_flash_write {
	uint32_t offset;   /* Byte offset to write */
	uint32_t size;     /* Size to write in bytes */
	/* Followed by data to write */
} __packed;

/* Erase flash */
#define EC_CMD_FLASH_ERASE 0x13

struct ec_params_flash_erase {
	uint32_t offset;   /* Byte offset to erase */
	uint32_t size;     /* Size to erase in bytes */
} __packed;

/*
 * Get/set flash protection.
 *
 * If mask!=0, sets/clear the requested bits of flags.  Depending on the
 * firmware write protect GPIO, not all flags will take effect immediately;
 * some flags require a subsequent hard reset to take effect.  Check the
 * returned flags bits to see what actually happened.
 *
 * If mask=0, simply returns the current flags state.
 */
#define EC_CMD_FLASH_PROTECT 0x15
#define EC_VER_FLASH_PROTECT 1  /* Command version 1 */

/* Flags for flash protection */
/* RO flash code protected when the EC boots */
#define EC_FLASH_PROTECT_RO_AT_BOOT         (1 << 0)
/*
 * RO flash code protected now.  If this bit is set, at-boot status cannot
 * be changed.
 */
#define EC_FLASH_PROTECT_RO_NOW             (1 << 1)
/* Entire flash code protected now, until reboot. */
#define EC_FLASH_PROTECT_ALL_NOW            (1 << 2)
/* Flash write protect GPIO is asserted now */
#define EC_FLASH_PROTECT_GPIO_ASSERTED      (1 << 3)
/* Error - at least one bank of flash is stuck locked, and cannot be unlocked */
#define EC_FLASH_PROTECT_ERROR_STUCK        (1 << 4)
/*
 * Error - flash protection is in inconsistent state.  At least one bank of
 * flash which should be protected is not protected.  Usually fixed by
 * re-requesting the desired flags, or by a hard reset if that fails.
 */
#define EC_FLASH_PROTECT_ERROR_INCONSISTENT (1 << 5)
/* Entile flash code protected when the EC boots */
#define EC_FLASH_PROTECT_ALL_AT_BOOT        (1 << 6)

struct ec_params_flash_protect {
	uint32_t mask;   /* Bits in flags to apply */
	uint32_t flags;  /* New flags to apply */
} __packed;

struct ec_response_flash_protect {
	/* Current value of flash protect flags */
	uint32_t flags;
	/*
	 * Flags which are valid on this platform.  This allows the caller
	 * to distinguish between flags which aren't set vs. flags which can't
	 * be set on this platform.
	 */
	uint32_t valid_flags;
	/* Flags which can be changed given the current protection state */
	uint32_t writable_flags;
} __packed;

/*
 * Note: commands 0x14 - 0x19 version 0 were old commands to get/set flash
 * write protect.  These commands may be reused with version > 0.
 */

/* Get the region offset/size */
#define EC_CMD_FLASH_REGION_INFO 0x16
#define EC_VER_FLASH_REGION_INFO 1

enum ec_flash_region {
	/* Region which holds read-only EC image */
	EC_FLASH_REGION_RO = 0,
	/* Region which holds rewritable EC image */
	EC_FLASH_REGION_RW,
	/*
	 * Region which should be write-protected in the factory (a superset of
	 * EC_FLASH_REGION_RO)
	 */
	EC_FLASH_REGION_WP_RO,
	/* Number of regions */
	EC_FLASH_REGION_COUNT,
};

struct ec_params_flash_region_info {
	uint32_t region;  /* enum ec_flash_region */
} __packed;

struct ec_response_flash_region_info {
	uint32_t offset;
	uint32_t size;
} __packed;

/* Read/write VbNvContext */
#define EC_CMD_VBNV_CONTEXT 0x17
#define EC_VER_VBNV_CONTEXT 1
#define EC_VBNV_BLOCK_SIZE 16

enum ec_vbnvcontext_op {
	EC_VBNV_CONTEXT_OP_READ,
	EC_VBNV_CONTEXT_OP_WRITE,
};

struct ec_params_vbnvcontext {
	uint32_t op;
	uint8_t block[EC_VBNV_BLOCK_SIZE];
} __packed;

struct ec_response_vbnvcontext {
	uint8_t block[EC_VBNV_BLOCK_SIZE];
} __packed;

/*****************************************************************************/
/* PWM commands */

/* Get fan target RPM */
#define EC_CMD_PWM_GET_FAN_TARGET_RPM 0x20

struct ec_response_pwm_get_fan_rpm {
	uint32_t rpm;
} __packed;

/* Set target fan RPM */
#define EC_CMD_PWM_SET_FAN_TARGET_RPM 0x21

struct ec_params_pwm_set_fan_target_rpm {
	uint32_t rpm;
} __packed;

/* Get keyboard backlight */
#define EC_CMD_PWM_GET_KEYBOARD_BACKLIGHT 0x22

struct ec_response_pwm_get_keyboard_backlight {
	uint8_t percent;
	uint8_t enabled;
} __packed;

/* Set keyboard backlight */
#define EC_CMD_PWM_SET_KEYBOARD_BACKLIGHT 0x23

struct ec_params_pwm_set_keyboard_backlight {
	uint8_t percent;
} __packed;

/* Set target fan PWM duty cycle */
#define EC_CMD_PWM_SET_FAN_DUTY 0x24

struct ec_params_pwm_set_fan_duty {
	uint32_t percent;
} __packed;

#define EC_CMD_PWM_SET_DUTY 0x25
/* 16 bit duty cycle, 0xffff = 100% */
#define EC_PWM_MAX_DUTY 0xffff

enum ec_pwm_type {
	/* All types, indexed by board-specific enum pwm_channel */
	EC_PWM_TYPE_GENERIC = 0,
	/* Keyboard backlight */
	EC_PWM_TYPE_KB_LIGHT,
	/* Display backlight */
	EC_PWM_TYPE_DISPLAY_LIGHT,
	EC_PWM_TYPE_COUNT,
};

struct ec_params_pwm_set_duty {
	uint16_t duty;     /* Duty cycle, EC_PWM_MAX_DUTY = 100% */
	uint8_t pwm_type;  /* ec_pwm_type */
	uint8_t index;     /* Type-specific index, or 0 if unique */
} __packed;

#define EC_CMD_PWM_GET_DUTY 0x26

struct ec_params_pwm_get_duty {
	uint8_t pwm_type;  /* ec_pwm_type */
	uint8_t index;     /* Type-specific index, or 0 if unique */
} __packed;

struct ec_response_pwm_get_duty {
	uint16_t duty;     /* Duty cycle, EC_PWM_MAX_DUTY = 100% */
} __packed;

/*****************************************************************************/
/*
 * Lightbar commands. This looks worse than it is. Since we only use one HOST
 * command to say "talk to the lightbar", we put the "and tell it to do X" part
 * into a subcommand. We'll make separate structs for subcommands with
 * different input args, so that we know how much to expect.
 */
#define EC_CMD_LIGHTBAR_CMD 0x28

struct rgb_s {
	uint8_t r, g, b;
};

#define LB_BATTERY_LEVELS 4
/* List of tweakable parameters. NOTE: It's __packed so it can be sent in a
 * host command, but the alignment is the same regardless. Keep it that way.
 */
struct lightbar_params_v0 {
	/* Timing */
	int32_t google_ramp_up;
	int32_t google_ramp_down;
	int32_t s3s0_ramp_up;
	int32_t s0_tick_delay[2];		/* AC=0/1 */
	int32_t s0a_tick_delay[2];		/* AC=0/1 */
	int32_t s0s3_ramp_down;
	int32_t s3_sleep_for;
	int32_t s3_ramp_up;
	int32_t s3_ramp_down;

	/* Oscillation */
	uint8_t new_s0;
	uint8_t osc_min[2];			/* AC=0/1 */
	uint8_t osc_max[2];			/* AC=0/1 */
	uint8_t w_ofs[2];			/* AC=0/1 */

	/* Brightness limits based on the backlight and AC. */
	uint8_t bright_bl_off_fixed[2];		/* AC=0/1 */
	uint8_t bright_bl_on_min[2];		/* AC=0/1 */
	uint8_t bright_bl_on_max[2];		/* AC=0/1 */

	/* Battery level thresholds */
	uint8_t battery_threshold[LB_BATTERY_LEVELS - 1];

	/* Map [AC][battery_level] to color index */
	uint8_t s0_idx[2][LB_BATTERY_LEVELS];	/* AP is running */
	uint8_t s3_idx[2][LB_BATTERY_LEVELS];	/* AP is sleeping */

	/* Color palette */
	struct rgb_s color[8];			/* 0-3 are Google colors */
} __packed;

struct lightbar_params_v1 {
	/* Timing */
	int32_t google_ramp_up;
	int32_t google_ramp_down;
	int32_t s3s0_ramp_up;
	int32_t s0_tick_delay[2];		/* AC=0/1 */
	int32_t s0a_tick_delay[2];		/* AC=0/1 */
	int32_t s0s3_ramp_down;
	int32_t s3_sleep_for;
	int32_t s3_ramp_up;
	int32_t s3_ramp_down;
	int32_t tap_tick_delay;
	int32_t tap_display_time;

	/* Tap-for-battery params */
	uint8_t tap_pct_red;
	uint8_t tap_pct_green;
	uint8_t tap_seg_min_on;
	uint8_t tap_seg_max_on;
	uint8_t tap_seg_osc;
	uint8_t tap_idx[3];

	/* Oscillation */
	uint8_t osc_min[2];			/* AC=0/1 */
	uint8_t osc_max[2];			/* AC=0/1 */
	uint8_t w_ofs[2];			/* AC=0/1 */

	/* Brightness limits based on the backlight and AC. */
	uint8_t bright_bl_off_fixed[2];		/* AC=0/1 */
	uint8_t bright_bl_on_min[2];		/* AC=0/1 */
	uint8_t bright_bl_on_max[2];		/* AC=0/1 */

	/* Battery level thresholds */
	uint8_t battery_threshold[LB_BATTERY_LEVELS - 1];

	/* Map [AC][battery_level] to color index */
	uint8_t s0_idx[2][LB_BATTERY_LEVELS];	/* AP is running */
	uint8_t s3_idx[2][LB_BATTERY_LEVELS];	/* AP is sleeping */

	/* Color palette */
	struct rgb_s color[8];			/* 0-3 are Google colors */
} __packed;

/* Lightbar program */
#define EC_LB_PROG_LEN 192
struct lightbar_program {
	uint8_t size;
	uint8_t data[EC_LB_PROG_LEN];
};

struct ec_params_lightbar {
	uint8_t cmd;		      /* Command (see enum lightbar_command) */
	union {
		struct {
			/* no args */
		} dump, off, on, init, get_seq, get_params_v0, get_params_v1,
			version, get_brightness, get_demo, suspend, resume;

		struct {
			uint8_t num;
		} set_brightness, seq, demo;

		struct {
			uint8_t ctrl, reg, value;
		} reg;

		struct {
			uint8_t led, red, green, blue;
		} set_rgb;

		struct {
			uint8_t led;
		} get_rgb;

		struct {
			uint8_t enable;
		} manual_suspend_ctrl;

		struct lightbar_params_v0 set_params_v0;
		struct lightbar_params_v1 set_params_v1;
		struct lightbar_program set_program;
	};
} __packed;

struct ec_response_lightbar {
	union {
		struct {
			struct {
				uint8_t reg;
				uint8_t ic0;
				uint8_t ic1;
			} vals[23];
		} dump;

		struct  {
			uint8_t num;
		} get_seq, get_brightness, get_demo;

		struct lightbar_params_v0 get_params_v0;
		struct lightbar_params_v1 get_params_v1;

		struct {
			uint32_t num;
			uint32_t flags;
		} version;

		struct {
			uint8_t red, green, blue;
		} get_rgb;

		struct {
			/* no return params */
		} off, on, init, set_brightness, seq, reg, set_rgb,
			demo, set_params_v0, set_params_v1,
			set_program, manual_suspend_ctrl, suspend, resume;
	};
} __packed;

/* Lightbar commands */
enum lightbar_command {
	LIGHTBAR_CMD_DUMP = 0,
	LIGHTBAR_CMD_OFF = 1,
	LIGHTBAR_CMD_ON = 2,
	LIGHTBAR_CMD_INIT = 3,
	LIGHTBAR_CMD_SET_BRIGHTNESS = 4,
	LIGHTBAR_CMD_SEQ = 5,
	LIGHTBAR_CMD_REG = 6,
	LIGHTBAR_CMD_SET_RGB = 7,
	LIGHTBAR_CMD_GET_SEQ = 8,
	LIGHTBAR_CMD_DEMO = 9,
	LIGHTBAR_CMD_GET_PARAMS_V0 = 10,
	LIGHTBAR_CMD_SET_PARAMS_V0 = 11,
	LIGHTBAR_CMD_VERSION = 12,
	LIGHTBAR_CMD_GET_BRIGHTNESS = 13,
	LIGHTBAR_CMD_GET_RGB = 14,
	LIGHTBAR_CMD_GET_DEMO = 15,
	LIGHTBAR_CMD_GET_PARAMS_V1 = 16,
	LIGHTBAR_CMD_SET_PARAMS_V1 = 17,
	LIGHTBAR_CMD_SET_PROGRAM = 18,
	LIGHTBAR_CMD_MANUAL_SUSPEND_CTRL = 19,
	LIGHTBAR_CMD_SUSPEND = 20,
	LIGHTBAR_CMD_RESUME = 21,
	LIGHTBAR_NUM_CMDS
};

/*****************************************************************************/
/* LED control commands */

#define EC_CMD_LED_CONTROL 0x29

enum ec_led_id {
	/* LED to indicate battery state of charge */
	EC_LED_ID_BATTERY_LED = 0,
	/*
	 * LED to indicate system power state (on or in suspend).
	 * May be on power button or on C-panel.
	 */
	EC_LED_ID_POWER_LED,
	/* LED on power adapter or its plug */
	EC_LED_ID_ADAPTER_LED,

	EC_LED_ID_COUNT
};

/* LED control flags */
#define EC_LED_FLAGS_QUERY (1 << 0) /* Query LED capability only */
#define EC_LED_FLAGS_AUTO  (1 << 1) /* Switch LED back to automatic control */

enum ec_led_colors {
	EC_LED_COLOR_RED = 0,
	EC_LED_COLOR_GREEN,
	EC_LED_COLOR_BLUE,
	EC_LED_COLOR_YELLOW,
	EC_LED_COLOR_WHITE,

	EC_LED_COLOR_COUNT
};

struct ec_params_led_control {
	uint8_t led_id;     /* Which LED to control */
	uint8_t flags;      /* Control flags */

	uint8_t brightness[EC_LED_COLOR_COUNT];
} __packed;

struct ec_response_led_control {
	/*
	 * Available brightness value range.
	 *
	 * Range 0 means color channel not present.
	 * Range 1 means on/off control.
	 * Other values means the LED is control by PWM.
	 */
	uint8_t brightness_range[EC_LED_COLOR_COUNT];
} __packed;

/*****************************************************************************/
/* Verified boot commands */

/*
 * Note: command code 0x29 version 0 was VBOOT_CMD in Link EVT; it may be
 * reused for other purposes with version > 0.
 */

/* Verified boot hash command */
#define EC_CMD_VBOOT_HASH 0x2A

struct ec_params_vboot_hash {
	uint8_t cmd;             /* enum ec_vboot_hash_cmd */
	uint8_t hash_type;       /* enum ec_vboot_hash_type */
	uint8_t nonce_size;      /* Nonce size; may be 0 */
	uint8_t reserved0;       /* Reserved; set 0 */
	uint32_t offset;         /* Offset in flash to hash */
	uint32_t size;           /* Number of bytes to hash */
	uint8_t nonce_data[64];  /* Nonce data; ignored if nonce_size=0 */
} __packed;

struct ec_response_vboot_hash {
	uint8_t status;          /* enum ec_vboot_hash_status */
	uint8_t hash_type;       /* enum ec_vboot_hash_type */
	uint8_t digest_size;     /* Size of hash digest in bytes */
	uint8_t reserved0;       /* Ignore; will be 0 */
	uint32_t offset;         /* Offset in flash which was hashed */
	uint32_t size;           /* Number of bytes hashed */
	uint8_t hash_digest[64]; /* Hash digest data */
} __packed;

enum ec_vboot_hash_cmd {
	EC_VBOOT_HASH_GET = 0,       /* Get current hash status */
	EC_VBOOT_HASH_ABORT = 1,     /* Abort calculating current hash */
	EC_VBOOT_HASH_START = 2,     /* Start computing a new hash */
	EC_VBOOT_HASH_RECALC = 3,    /* Synchronously compute a new hash */
};

enum ec_vboot_hash_type {
	EC_VBOOT_HASH_TYPE_SHA256 = 0, /* SHA-256 */
};

enum ec_vboot_hash_status {
	EC_VBOOT_HASH_STATUS_NONE = 0, /* No hash (not started, or aborted) */
	EC_VBOOT_HASH_STATUS_DONE = 1, /* Finished computing a hash */
	EC_VBOOT_HASH_STATUS_BUSY = 2, /* Busy computing a hash */
};

/*
 * Special values for offset for EC_VBOOT_HASH_START and EC_VBOOT_HASH_RECALC.
 * If one of these is specified, the EC will automatically update offset and
 * size to the correct values for the specified image (RO or RW).
 */
#define EC_VBOOT_HASH_OFFSET_RO 0xfffffffe
#define EC_VBOOT_HASH_OFFSET_RW 0xfffffffd

/*****************************************************************************/
/*
 * Motion sense commands. We'll make separate structs for sub-commands with
 * different input args, so that we know how much to expect.
 */
#define EC_CMD_MOTION_SENSE_CMD 0x2B

/* Motion sense commands */
enum motionsense_command {
	/*
	 * Dump command returns all motion sensor data including motion sense
	 * module flags and individual sensor flags.
	 */
	MOTIONSENSE_CMD_DUMP = 0,

	/*
	 * Info command returns data describing the details of a given sensor,
	 * including enum motionsensor_type, enum motionsensor_location, and
	 * enum motionsensor_chip.
	 */
	MOTIONSENSE_CMD_INFO = 1,

	/*
	 * EC Rate command is a setter/getter command for the EC sampling rate
	 * of all motion sensors in milliseconds.
	 */
	MOTIONSENSE_CMD_EC_RATE = 2,

	/*
	 * Sensor ODR command is a setter/getter command for the output data
	 * rate of a specific motion sensor in millihertz.
	 */
	MOTIONSENSE_CMD_SENSOR_ODR = 3,

	/*
	 * Sensor range command is a setter/getter command for the range of
	 * a specified motion sensor in +/-G's or +/- deg/s.
	 */
	MOTIONSENSE_CMD_SENSOR_RANGE = 4,

	/*
	 * Setter/getter command for the keyboard wake angle. When the lid
	 * angle is greater than this value, keyboard wake is disabled in S3,
	 * and when the lid angle goes less than this value, keyboard wake is
	 * enabled. Note, the lid angle measurement is an approximate,
	 * un-calibrated value, hence the wake angle isn't exact.
	 */
	MOTIONSENSE_CMD_KB_WAKE_ANGLE = 5,

	/*
	 * Returns a single sensor data.
	 */
	MOTIONSENSE_CMD_DATA = 6,

	/*
	 * Perform low level calibration.. On sensors that support it, ask to
	 * do offset calibration.
	 */
	MOTIONSENSE_CMD_PERFORM_CALIB = 10,

	/*
	 * Sensor Offset command is a setter/getter command for the offset used
	 * for calibration. The offsets can be calculated by the host, or via
	 * PERFORM_CALIB command.
	 */
	MOTIONSENSE_CMD_SENSOR_OFFSET = 11,

	/* Number of motionsense sub-commands. */
	MOTIONSENSE_NUM_CMDS
};

enum motionsensor_id {
	EC_MOTION_SENSOR_ACCEL_BASE = 0,
	EC_MOTION_SENSOR_ACCEL_LID = 1,
	EC_MOTION_SENSOR_GYRO = 2,

	/*
	 * Note, if more sensors are added and this count changes, the padding
	 * in ec_response_motion_sense dump command must be modified.
	 */
	EC_MOTION_SENSOR_COUNT = 3
};

/* List of motion sensor types. */
enum motionsensor_type {
	MOTIONSENSE_TYPE_ACCEL = 0,
	MOTIONSENSE_TYPE_GYRO = 1,
	MOTIONSENSE_TYPE_MAG = 2,
	MOTIONSENSE_TYPE_PROX = 3,
	MOTIONSENSE_TYPE_LIGHT = 4,
	MOTIONSENSE_TYPE_ACTIVITY = 5,
	MOTIONSENSE_TYPE_BARO = 6,
	MOTIONSENSE_TYPE_MAX,
};

/* List of motion sensor locations. */
enum motionsensor_location {
	MOTIONSENSE_LOC_BASE = 0,
	MOTIONSENSE_LOC_LID = 1,
	MOTIONSENSE_LOC_MAX,
};

/* List of motion sensor chips. */
enum motionsensor_chip {
	MOTIONSENSE_CHIP_KXCJ9 = 0,
};

/* Module flag masks used for the dump sub-command. */
#define MOTIONSENSE_MODULE_FLAG_ACTIVE (1<<0)

/* Sensor flag masks used for the dump sub-command. */
#define MOTIONSENSE_SENSOR_FLAG_PRESENT (1<<0)

/*
 * Send this value for the data element to only perform a read. If you
 * send any other value, the EC will interpret it as data to set and will
 * return the actual value set.
 */
#define EC_MOTION_SENSE_NO_VALUE -1

#define EC_MOTION_SENSE_INVALID_CALIB_TEMP 0x8000

/* Set Calibration information */
#define MOTION_SENSE_SET_OFFSET	1

struct ec_response_motion_sensor_data {
	/* Flags for each sensor. */
	uint8_t flags;
	/* Sensor number the data comes from */
	uint8_t sensor_num;
	/* Each sensor is up to 3-axis. */
	union {
		int16_t             data[3];
		struct {
			uint16_t    rsvd;
			uint32_t    timestamp;
		} __packed;
		struct {
			uint8_t     activity; /* motionsensor_activity */
			uint8_t     state;
			int16_t     add_info[2];
		};
	};
} __packed;

struct ec_params_motion_sense {
	uint8_t cmd;
	union {
		/* Used for MOTIONSENSE_CMD_DUMP. */
		struct {
			/* no args */
		} dump;

		/*
		 * Used for MOTIONSENSE_CMD_EC_RATE and
		 * MOTIONSENSE_CMD_KB_WAKE_ANGLE.
		 */
		struct {
			/* Data to set or EC_MOTION_SENSE_NO_VALUE to read. */
			int16_t data;
		} ec_rate, kb_wake_angle;

		/* Used for MOTIONSENSE_CMD_SENSOR_OFFSET */
		struct {
			uint8_t sensor_num;

			/*
			 * bit 0: If set (MOTION_SENSE_SET_OFFSET), set
			 * the calibration information in the EC.
			 * If unset, just retrieve calibration information.
			 */
			uint16_t flags;

			/*
			 * Temperature at calibration, in units of 0.01 C
			 * 0x8000: invalid / unknown.
			 * 0x0: 0C
			 * 0x7fff: +327.67C
			 */
			int16_t temp;

			/*
			 * Offset for calibration.
			 * Unit:
			 * Accelerometer: 1/1024 g
			 * Gyro:          1/1024 deg/s
			 * Compass:       1/16 uT
			 */
			int16_t offset[3];
		} __packed sensor_offset;

		/* Used for MOTIONSENSE_CMD_INFO. */
		struct {
			uint8_t sensor_num;
		} info;

		/*
		 * Used for MOTIONSENSE_CMD_SENSOR_ODR and
		 * MOTIONSENSE_CMD_SENSOR_RANGE.
		 */
		struct {
			/* Should be element of enum motionsensor_id. */
			uint8_t sensor_num;

			/* Rounding flag, true for round-up, false for down. */
			uint8_t roundup;

			uint16_t reserved;

			/* Data to set or EC_MOTION_SENSE_NO_VALUE to read. */
			int32_t data;
		} sensor_odr, sensor_range;
	};
} __packed;

struct ec_response_motion_sense {
	union {
		/* Used for MOTIONSENSE_CMD_DUMP. */
		struct {
			/* Flags representing the motion sensor module. */
			uint8_t module_flags;

			/* Number of sensors managed directly by the EC. */
			uint8_t sensor_count;

			/*
			 * Sensor data is truncated if response_max is too small
			 * for holding all the data.
			 */
			struct ec_response_motion_sensor_data sensor[0];
		} dump;

		/* Used for MOTIONSENSE_CMD_INFO. */
		struct {
			/* Should be element of enum motionsensor_type. */
			uint8_t type;

			/* Should be element of enum motionsensor_location. */
			uint8_t location;

			/* Should be element of enum motionsensor_chip. */
			uint8_t chip;
		} info;

		/* Used for MOTIONSENSE_CMD_DATA */
		struct ec_response_motion_sensor_data data;

		/*
		 * Used for MOTIONSENSE_CMD_EC_RATE, MOTIONSENSE_CMD_SENSOR_ODR,
		 * MOTIONSENSE_CMD_SENSOR_RANGE, and
		 * MOTIONSENSE_CMD_KB_WAKE_ANGLE.
		 */
		struct {
			/* Current value of the parameter queried. */
			int32_t ret;
		} ec_rate, sensor_odr, sensor_range, kb_wake_angle;

		/* Used for MOTIONSENSE_CMD_SENSOR_OFFSET */
		struct {
			int16_t temp;
			int16_t offset[3];
		} sensor_offset, perform_calib;
	};
} __packed;

/*****************************************************************************/
/* USB charging control commands */

/* Set USB port charging mode */
#define EC_CMD_USB_CHARGE_SET_MODE 0x30

struct ec_params_usb_charge_set_mode {
	uint8_t usb_port_id;
	uint8_t mode;
} __packed;

/*****************************************************************************/
/* Persistent storage for host */

/* Maximum bytes that can be read/written in a single command */
#define EC_PSTORE_SIZE_MAX 64

/* Get persistent storage info */
#define EC_CMD_PSTORE_INFO 0x40

struct ec_response_pstore_info {
	/* Persistent storage size, in bytes */
	uint32_t pstore_size;
	/* Access size; read/write offset and size must be a multiple of this */
	uint32_t access_size;
} __packed;

/*
 * Read persistent storage
 *
 * Response is params.size bytes of data.
 */
#define EC_CMD_PSTORE_READ 0x41

struct ec_params_pstore_read {
	uint32_t offset;   /* Byte offset to read */
	uint32_t size;     /* Size to read in bytes */
} __packed;

/* Write persistent storage */
#define EC_CMD_PSTORE_WRITE 0x42

struct ec_params_pstore_write {
	uint32_t offset;   /* Byte offset to write */
	uint32_t size;     /* Size to write in bytes */
	uint8_t data[EC_PSTORE_SIZE_MAX];
} __packed;

/*****************************************************************************/
/* Real-time clock */

/* RTC params and response structures */
struct ec_params_rtc {
	uint32_t time;
} __packed;

struct ec_response_rtc {
	uint32_t time;
} __packed;

/* These use ec_response_rtc */
#define EC_CMD_RTC_GET_VALUE 0x44
#define EC_CMD_RTC_GET_ALARM 0x45

/* These all use ec_params_rtc */
#define EC_CMD_RTC_SET_VALUE 0x46
#define EC_CMD_RTC_SET_ALARM 0x47

/* Pass as param to SET_ALARM to clear the current alarm */
#define EC_RTC_ALARM_CLEAR 0

/*****************************************************************************/
/* Port80 log access */

/* Maximum entries that can be read/written in a single command */
#define EC_PORT80_SIZE_MAX 32

/* Get last port80 code from previous boot */
#define EC_CMD_PORT80_LAST_BOOT 0x48
#define EC_CMD_PORT80_READ 0x48

enum ec_port80_subcmd {
	EC_PORT80_GET_INFO = 0,
	EC_PORT80_READ_BUFFER,
};

struct ec_params_port80_read {
	uint16_t subcmd;
	union {
		struct {
			uint32_t offset;
			uint32_t num_entries;
		} read_buffer;
	};
} __packed;

struct ec_response_port80_read {
	union {
		struct {
			uint32_t writes;
			uint32_t history_size;
			uint32_t last_boot;
		} get_info;
		struct {
			uint16_t codes[EC_PORT80_SIZE_MAX];
		} data;
	};
} __packed;

struct ec_response_port80_last_boot {
	uint16_t code;
} __packed;

/*****************************************************************************/
/* Thermal engine commands. Note that there are two implementations. We'll
 * reuse the command number, but the data and behavior is incompatible.
 * Version 0 is what originally shipped on Link.
 * Version 1 separates the CPU thermal limits from the fan control.
 */

#define EC_CMD_THERMAL_SET_THRESHOLD 0x50
#define EC_CMD_THERMAL_GET_THRESHOLD 0x51

/* The version 0 structs are opaque. You have to know what they are for
 * the get/set commands to make any sense.
 */

/* Version 0 - set */
struct ec_params_thermal_set_threshold {
	uint8_t sensor_type;
	uint8_t threshold_id;
	uint16_t value;
} __packed;

/* Version 0 - get */
struct ec_params_thermal_get_threshold {
	uint8_t sensor_type;
	uint8_t threshold_id;
} __packed;

struct ec_response_thermal_get_threshold {
	uint16_t value;
} __packed;


/* The version 1 structs are visible. */
enum ec_temp_thresholds {
	EC_TEMP_THRESH_WARN = 0,
	EC_TEMP_THRESH_HIGH,
	EC_TEMP_THRESH_HALT,

	EC_TEMP_THRESH_COUNT
};

/* Thermal configuration for one temperature sensor. Temps are in degrees K.
 * Zero values will be silently ignored by the thermal task.
 */
struct ec_thermal_config {
	uint32_t temp_host[EC_TEMP_THRESH_COUNT]; /* levels of hotness */
	uint32_t temp_fan_off;		/* no active cooling needed */
	uint32_t temp_fan_max;		/* max active cooling needed */
} __packed;

/* Version 1 - get config for one sensor. */
struct ec_params_thermal_get_threshold_v1 {
	uint32_t sensor_num;
} __packed;
/* This returns a struct ec_thermal_config */

/* Version 1 - set config for one sensor.
 * Use read-modify-write for best results! */
struct ec_params_thermal_set_threshold_v1 {
	uint32_t sensor_num;
	struct ec_thermal_config cfg;
} __packed;
/* This returns no data */

/****************************************************************************/

/* Toggle automatic fan control */
#define EC_CMD_THERMAL_AUTO_FAN_CTRL 0x52

/* Get TMP006 calibration data */
#define EC_CMD_TMP006_GET_CALIBRATION 0x53

struct ec_params_tmp006_get_calibration {
	uint8_t index;
} __packed;

struct ec_response_tmp006_get_calibration {
	float s0;
	float b0;
	float b1;
	float b2;
} __packed;

/* Set TMP006 calibration data */
#define EC_CMD_TMP006_SET_CALIBRATION 0x54

struct ec_params_tmp006_set_calibration {
	uint8_t index;
	uint8_t reserved[3];  /* Reserved; set 0 */
	float s0;
	float b0;
	float b1;
	float b2;
} __packed;

/* Read raw TMP006 data */
#define EC_CMD_TMP006_GET_RAW 0x55

struct ec_params_tmp006_get_raw {
	uint8_t index;
} __packed;

struct ec_response_tmp006_get_raw {
	int32_t t;  /* In 1/100 K */
	int32_t v;  /* In nV */
};

/*****************************************************************************/
/* MKBP - Matrix KeyBoard Protocol */

/*
 * Read key state
 *
 * Returns raw data for keyboard cols; see ec_response_mkbp_info.cols for
 * expected response size.
 *
 * NOTE: This has been superseded by EC_CMD_MKBP_GET_NEXT_EVENT.  If you wish
 * to obtain the instantaneous state, use EC_CMD_MKBP_INFO with the type
 * EC_MKBP_INFO_CURRENT and event EC_MKBP_EVENT_KEY_MATRIX.
 */
#define EC_CMD_MKBP_STATE 0x60

/*
 * Provide information about various MKBP things.  See enum ec_mkbp_info_type.
 */
#define EC_CMD_MKBP_INFO 0x61

struct ec_response_mkbp_info {
	uint32_t rows;
	uint32_t cols;
	/* Formerly "switches", which was 0. */
	uint8_t reserved;
} __packed;

struct ec_params_mkbp_info {
	uint8_t info_type;
	uint8_t event_type;
} __packed;

enum ec_mkbp_info_type {
	/*
	 * Info about the keyboard matrix: number of rows and columns.
	 *
	 * Returns struct ec_response_mkbp_info.
	 */
	EC_MKBP_INFO_KBD = 0,

	/*
	 * For buttons and switches, info about which specifically are
	 * supported.  event_type must be set to one of the values in enum
	 * ec_mkbp_event.
	 *
	 * For EC_MKBP_EVENT_BUTTON and EC_MKBP_EVENT_SWITCH, returns a 4 byte
	 * bitmask indicating which buttons or switches are present.  See the
	 * bit inidices below.
	 */
	EC_MKBP_INFO_SUPPORTED = 1,

	/*
	 * Instantaneous state of buttons and switches.
	 *
	 * event_type must be set to one of the values in enum ec_mkbp_event.
	 *
	 * For EC_MKBP_EVENT_KEY_MATRIX, returns uint8_t key_matrix[13]
	 * indicating the current state of the keyboard matrix.
	 *
	 * For EC_MKBP_EVENT_HOST_EVENT, return uint32_t host_event, the raw
	 * event state.
	 *
	 * For EC_MKBP_EVENT_BUTTON, returns uint32_t buttons, indicating the
	 * state of supported buttons.
	 *
	 * For EC_MKBP_EVENT_SWITCH, returns uint32_t switches, indicating the
	 * state of supported switches.
	 */
	EC_MKBP_INFO_CURRENT = 2,
};

/* Simulate key press */
#define EC_CMD_MKBP_SIMULATE_KEY 0x62

struct ec_params_mkbp_simulate_key {
	uint8_t col;
	uint8_t row;
	uint8_t pressed;
} __packed;

/* Configure keyboard scanning */
#define EC_CMD_MKBP_SET_CONFIG 0x64
#define EC_CMD_MKBP_GET_CONFIG 0x65

/* flags */
enum mkbp_config_flags {
	EC_MKBP_FLAGS_ENABLE = 1,	/* Enable keyboard scanning */
};

enum mkbp_config_valid {
	EC_MKBP_VALID_SCAN_PERIOD		= 1 << 0,
	EC_MKBP_VALID_POLL_TIMEOUT		= 1 << 1,
	EC_MKBP_VALID_MIN_POST_SCAN_DELAY	= 1 << 3,
	EC_MKBP_VALID_OUTPUT_SETTLE		= 1 << 4,
	EC_MKBP_VALID_DEBOUNCE_DOWN		= 1 << 5,
	EC_MKBP_VALID_DEBOUNCE_UP		= 1 << 6,
	EC_MKBP_VALID_FIFO_MAX_DEPTH		= 1 << 7,
};

/* Configuration for our key scanning algorithm */
struct ec_mkbp_config {
	uint32_t valid_mask;		/* valid fields */
	uint8_t flags;		/* some flags (enum mkbp_config_flags) */
	uint8_t valid_flags;		/* which flags are valid */
	uint16_t scan_period_us;	/* period between start of scans */
	/* revert to interrupt mode after no activity for this long */
	uint32_t poll_timeout_us;
	/*
	 * minimum post-scan relax time. Once we finish a scan we check
	 * the time until we are due to start the next one. If this time is
	 * shorter this field, we use this instead.
	 */
	uint16_t min_post_scan_delay_us;
	/* delay between setting up output and waiting for it to settle */
	uint16_t output_settle_us;
	uint16_t debounce_down_us;	/* time for debounce on key down */
	uint16_t debounce_up_us;	/* time for debounce on key up */
	/* maximum depth to allow for fifo (0 = no keyscan output) */
	uint8_t fifo_max_depth;
} __packed;

struct ec_params_mkbp_set_config {
	struct ec_mkbp_config config;
} __packed;

struct ec_response_mkbp_get_config {
	struct ec_mkbp_config config;
} __packed;

/* Run the key scan emulation */
#define EC_CMD_KEYSCAN_SEQ_CTRL 0x66

enum ec_keyscan_seq_cmd {
	EC_KEYSCAN_SEQ_STATUS = 0,	/* Get status information */
	EC_KEYSCAN_SEQ_CLEAR = 1,	/* Clear sequence */
	EC_KEYSCAN_SEQ_ADD = 2,		/* Add item to sequence */
	EC_KEYSCAN_SEQ_START = 3,	/* Start running sequence */
	EC_KEYSCAN_SEQ_COLLECT = 4,	/* Collect sequence summary data */
};

enum ec_collect_flags {
	/*
	 * Indicates this scan was processed by the EC. Due to timing, some
	 * scans may be skipped.
	 */
	EC_KEYSCAN_SEQ_FLAG_DONE	= 1 << 0,
};

struct ec_collect_item {
	uint8_t flags;		/* some flags (enum ec_collect_flags) */
};

struct ec_params_keyscan_seq_ctrl {
	uint8_t cmd;	/* Command to send (enum ec_keyscan_seq_cmd) */
	union {
		struct {
			uint8_t active;		/* still active */
			uint8_t num_items;	/* number of items */
			/* Current item being presented */
			uint8_t cur_item;
		} status;
		struct {
			/*
			 * Absolute time for this scan, measured from the
			 * start of the sequence.
			 */
			uint32_t time_us;
			uint8_t scan[0];	/* keyscan data */
		} add;
		struct {
			uint8_t start_item;	/* First item to return */
			uint8_t num_items;	/* Number of items to return */
		} collect;
	};
} __packed;

struct ec_result_keyscan_seq_ctrl {
	union {
		struct {
			uint8_t num_items;	/* Number of items */
			/* Data for each item */
			struct ec_collect_item item[0];
		} collect;
	};
} __packed;

/*
 * Command for retrieving the next pending MKBP event from the EC device
 *
 * The device replies with UNAVAILABLE if there aren't any pending events.
 */
#define EC_CMD_GET_NEXT_EVENT 0x67

enum ec_mkbp_event {
	/* Keyboard matrix changed. The event data is the new matrix state. */
	EC_MKBP_EVENT_KEY_MATRIX = 0,

	/* New host event. The event data is 4 bytes of host event flags. */
	EC_MKBP_EVENT_HOST_EVENT = 1,

	/* New Sensor FIFO data. The event data is fifo_info structure. */
	EC_MKBP_EVENT_SENSOR_FIFO = 2,

	/* The state of the non-matrixed buttons have changed. */
	EC_MKBP_EVENT_BUTTON = 3,

	/* The state of the switches have changed. */
	EC_MKBP_EVENT_SWITCH = 4,

	/* EC sent a sysrq command */
	EC_MKBP_EVENT_SYSRQ = 6,

	/* Notify the AP that something happened on CEC */
	EC_MKBP_EVENT_CEC_EVENT = 8,

	/* Send an incoming CEC message to the AP */
	EC_MKBP_EVENT_CEC_MESSAGE = 9,

	/* Number of MKBP events */
	EC_MKBP_EVENT_COUNT,
};

union ec_response_get_next_data {
	uint8_t   key_matrix[13];

	/* Unaligned */
	uint32_t  host_event;

	uint32_t   buttons;
	uint32_t   switches;
	uint32_t   sysrq;
} __packed;

union ec_response_get_next_data_v1 {
	uint8_t key_matrix[16];
	uint32_t host_event;
	uint32_t buttons;
	uint32_t switches;
	uint32_t sysrq;
	uint32_t cec_events;
	uint8_t cec_message[16];
} __packed;

struct ec_response_get_next_event {
	uint8_t event_type;
	/* Followed by event data if any */
	union ec_response_get_next_data data;
} __packed;

struct ec_response_get_next_event_v1 {
	uint8_t event_type;
	/* Followed by event data if any */
	union ec_response_get_next_data_v1 data;
} __packed;

/* Bit indices for buttons and switches.*/
/* Buttons */
#define EC_MKBP_POWER_BUTTON	0
#define EC_MKBP_VOL_UP		1
#define EC_MKBP_VOL_DOWN	2

/* Switches */
#define EC_MKBP_LID_OPEN	0
#define EC_MKBP_TABLET_MODE	1

/*****************************************************************************/
/* Temperature sensor commands */

/* Read temperature sensor info */
#define EC_CMD_TEMP_SENSOR_GET_INFO 0x70

struct ec_params_temp_sensor_get_info {
	uint8_t id;
} __packed;

struct ec_response_temp_sensor_get_info {
	char sensor_name[32];
	uint8_t sensor_type;
} __packed;

/*****************************************************************************/

/*
 * Note: host commands 0x80 - 0x87 are reserved to avoid conflict with ACPI
 * commands accidentally sent to the wrong interface.  See the ACPI section
 * below.
 */

/*****************************************************************************/
/* Host event commands */

/*
 * Host event mask params and response structures, shared by all of the host
 * event commands below.
 */
struct ec_params_host_event_mask {
	uint32_t mask;
} __packed;

struct ec_response_host_event_mask {
	uint32_t mask;
} __packed;

/* These all use ec_response_host_event_mask */
#define EC_CMD_HOST_EVENT_GET_B         0x87
#define EC_CMD_HOST_EVENT_GET_SMI_MASK  0x88
#define EC_CMD_HOST_EVENT_GET_SCI_MASK  0x89
#define EC_CMD_HOST_EVENT_GET_WAKE_MASK 0x8d

/* These all use ec_params_host_event_mask */
#define EC_CMD_HOST_EVENT_SET_SMI_MASK  0x8a
#define EC_CMD_HOST_EVENT_SET_SCI_MASK  0x8b
#define EC_CMD_HOST_EVENT_CLEAR         0x8c
#define EC_CMD_HOST_EVENT_SET_WAKE_MASK 0x8e
#define EC_CMD_HOST_EVENT_CLEAR_B       0x8f

/*****************************************************************************/
/* Switch commands */

/* Enable/disable LCD backlight */
#define EC_CMD_SWITCH_ENABLE_BKLIGHT 0x90

struct ec_params_switch_enable_backlight {
	uint8_t enabled;
} __packed;

/* Enable/disable WLAN/Bluetooth */
#define EC_CMD_SWITCH_ENABLE_WIRELESS 0x91
#define EC_VER_SWITCH_ENABLE_WIRELESS 1

/* Version 0 params; no response */
struct ec_params_switch_enable_wireless_v0 {
	uint8_t enabled;
} __packed;

/* Version 1 params */
struct ec_params_switch_enable_wireless_v1 {
	/* Flags to enable now */
	uint8_t now_flags;

	/* Which flags to copy from now_flags */
	uint8_t now_mask;

	/*
	 * Flags to leave enabled in S3, if they're on at the S0->S3
	 * transition.  (Other flags will be disabled by the S0->S3
	 * transition.)
	 */
	uint8_t suspend_flags;

	/* Which flags to copy from suspend_flags */
	uint8_t suspend_mask;
} __packed;

/* Version 1 response */
struct ec_response_switch_enable_wireless_v1 {
	/* Flags to enable now */
	uint8_t now_flags;

	/* Flags to leave enabled in S3 */
	uint8_t suspend_flags;
} __packed;

/*****************************************************************************/
/* GPIO commands. Only available on EC if write protect has been disabled. */

/* Set GPIO output value */
#define EC_CMD_GPIO_SET 0x92

struct ec_params_gpio_set {
	char name[32];
	uint8_t val;
} __packed;

/* Get GPIO value */
#define EC_CMD_GPIO_GET 0x93

/* Version 0 of input params and response */
struct ec_params_gpio_get {
	char name[32];
} __packed;
struct ec_response_gpio_get {
	uint8_t val;
} __packed;

/* Version 1 of input params and response */
struct ec_params_gpio_get_v1 {
	uint8_t subcmd;
	union {
		struct {
			char name[32];
		} get_value_by_name;
		struct {
			uint8_t index;
		} get_info;
	};
} __packed;

struct ec_response_gpio_get_v1 {
	union {
		struct {
			uint8_t val;
		} get_value_by_name, get_count;
		struct {
			uint8_t val;
			char name[32];
			uint32_t flags;
		} get_info;
	};
} __packed;

enum gpio_get_subcmd {
	EC_GPIO_GET_BY_NAME = 0,
	EC_GPIO_GET_COUNT = 1,
	EC_GPIO_GET_INFO = 2,
};

/*****************************************************************************/
/* I2C commands. Only available when flash write protect is unlocked. */

/*
 * TODO(crosbug.com/p/23570): These commands are deprecated, and will be
 * removed soon.  Use EC_CMD_I2C_XFER instead.
 */

/* Read I2C bus */
#define EC_CMD_I2C_READ 0x94

struct ec_params_i2c_read {
	uint16_t addr; /* 8-bit address (7-bit shifted << 1) */
	uint8_t read_size; /* Either 8 or 16. */
	uint8_t port;
	uint8_t offset;
} __packed;
struct ec_response_i2c_read {
	uint16_t data;
} __packed;

/* Write I2C bus */
#define EC_CMD_I2C_WRITE 0x95

struct ec_params_i2c_write {
	uint16_t data;
	uint16_t addr; /* 8-bit address (7-bit shifted << 1) */
	uint8_t write_size; /* Either 8 or 16. */
	uint8_t port;
	uint8_t offset;
} __packed;

/*****************************************************************************/
/* Charge state commands. Only available when flash write protect unlocked. */

/* Force charge state machine to stop charging the battery or force it to
 * discharge the battery.
 */
#define EC_CMD_CHARGE_CONTROL 0x96
#define EC_VER_CHARGE_CONTROL 1

enum ec_charge_control_mode {
	CHARGE_CONTROL_NORMAL = 0,
	CHARGE_CONTROL_IDLE,
	CHARGE_CONTROL_DISCHARGE,
};

struct ec_params_charge_control {
	uint32_t mode;  /* enum charge_control_mode */
} __packed;

/*****************************************************************************/
/* Console commands. Only available when flash write protect is unlocked. */

/* Snapshot console output buffer for use by EC_CMD_CONSOLE_READ. */
#define EC_CMD_CONSOLE_SNAPSHOT 0x97

/*
 * Read data from the saved snapshot. If the subcmd parameter is
 * CONSOLE_READ_NEXT, this will return data starting from the beginning of
 * the latest snapshot. If it is CONSOLE_READ_RECENT, it will start from the
 * end of the previous snapshot.
 *
 * The params are only looked at in version >= 1 of this command. Prior
 * versions will just default to CONSOLE_READ_NEXT behavior.
 *
 * Response is null-terminated string.  Empty string, if there is no more
 * remaining output.
 */
#define EC_CMD_CONSOLE_READ 0x98

enum ec_console_read_subcmd {
	CONSOLE_READ_NEXT = 0,
	CONSOLE_READ_RECENT
};

struct ec_params_console_read_v1 {
	uint8_t subcmd; /* enum ec_console_read_subcmd */
} __packed;

/*****************************************************************************/

/*
 * Cut off battery power immediately or after the host has shut down.
 *
 * return EC_RES_INVALID_COMMAND if unsupported by a board/battery.
 *	  EC_RES_SUCCESS if the command was successful.
 *	  EC_RES_ERROR if the cut off command failed.
 */

#define EC_CMD_BATTERY_CUT_OFF 0x99

#define EC_BATTERY_CUTOFF_FLAG_AT_SHUTDOWN	(1 << 0)

struct ec_params_battery_cutoff {
	uint8_t flags;
} __packed;

/*****************************************************************************/
/* USB port mux control. */

/*
 * Switch USB mux or return to automatic switching.
 */
#define EC_CMD_USB_MUX 0x9a

struct ec_params_usb_mux {
	uint8_t mux;
} __packed;

/*****************************************************************************/
/* LDOs / FETs control. */

enum ec_ldo_state {
	EC_LDO_STATE_OFF = 0,	/* the LDO / FET is shut down */
	EC_LDO_STATE_ON = 1,	/* the LDO / FET is ON / providing power */
};

/*
 * Switch on/off a LDO.
 */
#define EC_CMD_LDO_SET 0x9b

struct ec_params_ldo_set {
	uint8_t index;
	uint8_t state;
} __packed;

/*
 * Get LDO state.
 */
#define EC_CMD_LDO_GET 0x9c

struct ec_params_ldo_get {
	uint8_t index;
} __packed;

struct ec_response_ldo_get {
	uint8_t state;
} __packed;

/*****************************************************************************/
/* Power info. */

/*
 * Get power info.
 */
#define EC_CMD_POWER_INFO 0x9d

struct ec_response_power_info {
	uint32_t usb_dev_type;
	uint16_t voltage_ac;
	uint16_t voltage_system;
	uint16_t current_system;
	uint16_t usb_current_limit;
} __packed;

/*****************************************************************************/
/* I2C passthru command */

#define EC_CMD_I2C_PASSTHRU 0x9e

/* Read data; if not present, message is a write */
#define EC_I2C_FLAG_READ	(1 << 15)

/* Mask for address */
#define EC_I2C_ADDR_MASK	0x3ff

#define EC_I2C_STATUS_NAK	(1 << 0) /* Transfer was not acknowledged */
#define EC_I2C_STATUS_TIMEOUT	(1 << 1) /* Timeout during transfer */

/* Any error */
#define EC_I2C_STATUS_ERROR	(EC_I2C_STATUS_NAK | EC_I2C_STATUS_TIMEOUT)

struct ec_params_i2c_passthru_msg {
	uint16_t addr_flags;	/* I2C slave address (7 or 10 bits) and flags */
	uint16_t len;		/* Number of bytes to read or write */
} __packed;

struct ec_params_i2c_passthru {
	uint8_t port;		/* I2C port number */
	uint8_t num_msgs;	/* Number of messages */
	struct ec_params_i2c_passthru_msg msg[];
	/* Data to write for all messages is concatenated here */
} __packed;

struct ec_response_i2c_passthru {
	uint8_t i2c_status;	/* Status flags (EC_I2C_STATUS_...) */
	uint8_t num_msgs;	/* Number of messages processed */
	uint8_t data[];		/* Data read by messages concatenated here */
} __packed;

/*****************************************************************************/
/* Power button hang detect */

#define EC_CMD_HANG_DETECT 0x9f

/* Reasons to start hang detection timer */
/* Power button pressed */
#define EC_HANG_START_ON_POWER_PRESS  (1 << 0)

/* Lid closed */
#define EC_HANG_START_ON_LID_CLOSE    (1 << 1)

 /* Lid opened */
#define EC_HANG_START_ON_LID_OPEN     (1 << 2)

/* Start of AP S3->S0 transition (booting or resuming from suspend) */
#define EC_HANG_START_ON_RESUME       (1 << 3)

/* Reasons to cancel hang detection */

/* Power button released */
#define EC_HANG_STOP_ON_POWER_RELEASE (1 << 8)

/* Any host command from AP received */
#define EC_HANG_STOP_ON_HOST_COMMAND  (1 << 9)

/* Stop on end of AP S0->S3 transition (suspending or shutting down) */
#define EC_HANG_STOP_ON_SUSPEND       (1 << 10)

/*
 * If this flag is set, all the other fields are ignored, and the hang detect
 * timer is started.  This provides the AP a way to start the hang timer
 * without reconfiguring any of the other hang detect settings.  Note that
 * you must previously have configured the timeouts.
 */
#define EC_HANG_START_NOW             (1 << 30)

/*
 * If this flag is set, all the other fields are ignored (including
 * EC_HANG_START_NOW).  This provides the AP a way to stop the hang timer
 * without reconfiguring any of the other hang detect settings.
 */
#define EC_HANG_STOP_NOW              (1 << 31)

struct ec_params_hang_detect {
	/* Flags; see EC_HANG_* */
	uint32_t flags;

	/* Timeout in msec before generating host event, if enabled */
	uint16_t host_event_timeout_msec;

	/* Timeout in msec before generating warm reboot, if enabled */
	uint16_t warm_reboot_timeout_msec;
} __packed;

/*****************************************************************************/
/* Commands for battery charging */

/*
 * This is the single catch-all host command to exchange data regarding the
 * charge state machine (v2 and up).
 */
#define EC_CMD_CHARGE_STATE 0xa0

/* Subcommands for this host command */
enum charge_state_command {
	CHARGE_STATE_CMD_GET_STATE,
	CHARGE_STATE_CMD_GET_PARAM,
	CHARGE_STATE_CMD_SET_PARAM,
	CHARGE_STATE_NUM_CMDS
};

/*
 * Known param numbers are defined here. Ranges are reserved for board-specific
 * params, which are handled by the particular implementations.
 */
enum charge_state_params {
	CS_PARAM_CHG_VOLTAGE,	      /* charger voltage limit */
	CS_PARAM_CHG_CURRENT,	      /* charger current limit */
	CS_PARAM_CHG_INPUT_CURRENT,   /* charger input current limit */
	CS_PARAM_CHG_STATUS,	      /* charger-specific status */
	CS_PARAM_CHG_OPTION,	      /* charger-specific options */
	/* How many so far? */
	CS_NUM_BASE_PARAMS,

	/* Range for CONFIG_CHARGER_PROFILE_OVERRIDE params */
	CS_PARAM_CUSTOM_PROFILE_MIN = 0x10000,
	CS_PARAM_CUSTOM_PROFILE_MAX = 0x1ffff,

	/* Other custom param ranges go here... */
};

struct ec_params_charge_state {
	uint8_t cmd;				/* enum charge_state_command */
	union {
		struct {
			/* no args */
		} get_state;

		struct {
			uint32_t param;		/* enum charge_state_param */
		} get_param;

		struct {
			uint32_t param;		/* param to set */
			uint32_t value;		/* value to set */
		} set_param;
	};
} __packed;

struct ec_response_charge_state {
	union {
		struct {
			int ac;
			int chg_voltage;
			int chg_current;
			int chg_input_current;
			int batt_state_of_charge;
		} get_state;

		struct {
			uint32_t value;
		} get_param;
		struct {
			/* no return values */
		} set_param;
	};
} __packed;


/*
 * Set maximum battery charging current.
 */
#define EC_CMD_CHARGE_CURRENT_LIMIT 0xa1

struct ec_params_current_limit {
	uint32_t limit; /* in mA */
} __packed;

/*
 * Set maximum external voltage / current.
 */
#define EC_CMD_EXTERNAL_POWER_LIMIT 0x00A2

/* Command v0 is used only on Spring and is obsolete + unsupported */
struct ec_params_external_power_limit_v1 {
	uint16_t current_lim; /* in mA, or EC_POWER_LIMIT_NONE to clear limit */
	uint16_t voltage_lim; /* in mV, or EC_POWER_LIMIT_NONE to clear limit */
} __packed;

#define EC_POWER_LIMIT_NONE 0xffff

/* Inform the EC when entering a sleep state */
#define EC_CMD_HOST_SLEEP_EVENT 0xa9

enum host_sleep_event {
	HOST_SLEEP_EVENT_S3_SUSPEND   = 1,
	HOST_SLEEP_EVENT_S3_RESUME    = 2,
	HOST_SLEEP_EVENT_S0IX_SUSPEND = 3,
	HOST_SLEEP_EVENT_S0IX_RESUME  = 4
};

struct ec_params_host_sleep_event {
	uint8_t sleep_event;
} __packed;

/*****************************************************************************/
/* Smart battery pass-through */

/* Get / Set 16-bit smart battery registers */
#define EC_CMD_SB_READ_WORD   0xb0
#define EC_CMD_SB_WRITE_WORD  0xb1

/* Get / Set string smart battery parameters
 * formatted as SMBUS "block".
 */
#define EC_CMD_SB_READ_BLOCK  0xb2
#define EC_CMD_SB_WRITE_BLOCK 0xb3

struct ec_params_sb_rd {
	uint8_t reg;
} __packed;

struct ec_response_sb_rd_word {
	uint16_t value;
} __packed;

struct ec_params_sb_wr_word {
	uint8_t reg;
	uint16_t value;
} __packed;

struct ec_response_sb_rd_block {
	uint8_t data[32];
} __packed;

struct ec_params_sb_wr_block {
	uint8_t reg;
	uint16_t data[32];
} __packed;

/*****************************************************************************/
/* Battery vendor parameters
 *
 * Get or set vendor-specific parameters in the battery. Implementations may
 * differ between boards or batteries. On a set operation, the response
 * contains the actual value set, which may be rounded or clipped from the
 * requested value.
 */

#define EC_CMD_BATTERY_VENDOR_PARAM 0xb4

enum ec_battery_vendor_param_mode {
	BATTERY_VENDOR_PARAM_MODE_GET = 0,
	BATTERY_VENDOR_PARAM_MODE_SET,
};

struct ec_params_battery_vendor_param {
	uint32_t param;
	uint32_t value;
	uint8_t mode;
} __packed;

struct ec_response_battery_vendor_param {
	uint32_t value;
} __packed;

/*****************************************************************************/
/* System commands */

/*
 * TODO(crosbug.com/p/23747): This is a confusing name, since it doesn't
 * necessarily reboot the EC.  Rename to "image" or something similar?
 */
#define EC_CMD_REBOOT_EC 0xd2

/* Command */
enum ec_reboot_cmd {
	EC_REBOOT_CANCEL = 0,        /* Cancel a pending reboot */
	EC_REBOOT_JUMP_RO = 1,       /* Jump to RO without rebooting */
	EC_REBOOT_JUMP_RW = 2,       /* Jump to RW without rebooting */
	/* (command 3 was jump to RW-B) */
	EC_REBOOT_COLD = 4,          /* Cold-reboot */
	EC_REBOOT_DISABLE_JUMP = 5,  /* Disable jump until next reboot */
	EC_REBOOT_HIBERNATE = 6      /* Hibernate EC */
};

/* Flags for ec_params_reboot_ec.reboot_flags */
#define EC_REBOOT_FLAG_RESERVED0      (1 << 0)  /* Was recovery request */
#define EC_REBOOT_FLAG_ON_AP_SHUTDOWN (1 << 1)  /* Reboot after AP shutdown */

struct ec_params_reboot_ec {
	uint8_t cmd;           /* enum ec_reboot_cmd */
	uint8_t flags;         /* See EC_REBOOT_FLAG_* */
} __packed;

/*
 * Get information on last EC panic.
 *
 * Returns variable-length platform-dependent panic information.  See panic.h
 * for details.
 */
#define EC_CMD_GET_PANIC_INFO 0xd3

/*****************************************************************************/
/*
 * ACPI commands
 *
 * These are valid ONLY on the ACPI command/data port.
 */

/*
 * ACPI Read Embedded Controller
 *
 * This reads from ACPI memory space on the EC (EC_ACPI_MEM_*).
 *
 * Use the following sequence:
 *
 *    - Write EC_CMD_ACPI_READ to EC_LPC_ADDR_ACPI_CMD
 *    - Wait for EC_LPC_CMDR_PENDING bit to clear
 *    - Write address to EC_LPC_ADDR_ACPI_DATA
 *    - Wait for EC_LPC_CMDR_DATA bit to set
 *    - Read value from EC_LPC_ADDR_ACPI_DATA
 */
#define EC_CMD_ACPI_READ 0x80

/*
 * ACPI Write Embedded Controller
 *
 * This reads from ACPI memory space on the EC (EC_ACPI_MEM_*).
 *
 * Use the following sequence:
 *
 *    - Write EC_CMD_ACPI_WRITE to EC_LPC_ADDR_ACPI_CMD
 *    - Wait for EC_LPC_CMDR_PENDING bit to clear
 *    - Write address to EC_LPC_ADDR_ACPI_DATA
 *    - Wait for EC_LPC_CMDR_PENDING bit to clear
 *    - Write value to EC_LPC_ADDR_ACPI_DATA
 */
#define EC_CMD_ACPI_WRITE 0x81

/*
 * ACPI Query Embedded Controller
 *
 * This clears the lowest-order bit in the currently pending host events, and
 * sets the result code to the 1-based index of the bit (event 0x00000001 = 1,
 * event 0x80000000 = 32), or 0 if no event was pending.
 */
#define EC_CMD_ACPI_QUERY_EVENT 0x84

/* Valid addresses in ACPI memory space, for read/write commands */

/* Memory space version; set to EC_ACPI_MEM_VERSION_CURRENT */
#define EC_ACPI_MEM_VERSION            0x00
/*
 * Test location; writing value here updates test compliment byte to (0xff -
 * value).
 */
#define EC_ACPI_MEM_TEST               0x01
/* Test compliment; writes here are ignored. */
#define EC_ACPI_MEM_TEST_COMPLIMENT    0x02

/* Keyboard backlight brightness percent (0 - 100) */
#define EC_ACPI_MEM_KEYBOARD_BACKLIGHT 0x03
/* DPTF Target Fan Duty (0-100, 0xff for auto/none) */
#define EC_ACPI_MEM_FAN_DUTY           0x04

/*
 * DPTF temp thresholds. Any of the EC's temp sensors can have up to two
 * independent thresholds attached to them. The current value of the ID
 * register determines which sensor is affected by the THRESHOLD and COMMIT
 * registers. The THRESHOLD register uses the same EC_TEMP_SENSOR_OFFSET scheme
 * as the memory-mapped sensors. The COMMIT register applies those settings.
 *
 * The spec does not mandate any way to read back the threshold settings
 * themselves, but when a threshold is crossed the AP needs a way to determine
 * which sensor(s) are responsible. Each reading of the ID register clears and
 * returns one sensor ID that has crossed one of its threshold (in either
 * direction) since the last read. A value of 0xFF means "no new thresholds
 * have tripped". Setting or enabling the thresholds for a sensor will clear
 * the unread event count for that sensor.
 */
#define EC_ACPI_MEM_TEMP_ID            0x05
#define EC_ACPI_MEM_TEMP_THRESHOLD     0x06
#define EC_ACPI_MEM_TEMP_COMMIT        0x07
/*
 * Here are the bits for the COMMIT register:
 *   bit 0 selects the threshold index for the chosen sensor (0/1)
 *   bit 1 enables/disables the selected threshold (0 = off, 1 = on)
 * Each write to the commit register affects one threshold.
 */
#define EC_ACPI_MEM_TEMP_COMMIT_SELECT_MASK (1 << 0)
#define EC_ACPI_MEM_TEMP_COMMIT_ENABLE_MASK (1 << 1)
/*
 * Example:
 *
 * Set the thresholds for sensor 2 to 50 C and 60 C:
 *   write 2 to [0x05]      --  select temp sensor 2
 *   write 0x7b to [0x06]   --  C_TO_K(50) - EC_TEMP_SENSOR_OFFSET
 *   write 0x2 to [0x07]    --  enable threshold 0 with this value
 *   write 0x85 to [0x06]   --  C_TO_K(60) - EC_TEMP_SENSOR_OFFSET
 *   write 0x3 to [0x07]    --  enable threshold 1 with this value
 *
 * Disable the 60 C threshold, leaving the 50 C threshold unchanged:
 *   write 2 to [0x05]      --  select temp sensor 2
 *   write 0x1 to [0x07]    --  disable threshold 1
 */

/* DPTF battery charging current limit */
#define EC_ACPI_MEM_CHARGING_LIMIT     0x08

/* Charging limit is specified in 64 mA steps */
#define EC_ACPI_MEM_CHARGING_LIMIT_STEP_MA   64
/* Value to disable DPTF battery charging limit */
#define EC_ACPI_MEM_CHARGING_LIMIT_DISABLED  0xff

/* Current version of ACPI memory address space */
#define EC_ACPI_MEM_VERSION_CURRENT 1


/*****************************************************************************/
/*
 * HDMI CEC commands
 *
 * These commands are for sending and receiving message via HDMI CEC
 */
#define EC_MAX_CEC_MSG_LEN 16

/* CEC message from the AP to be written on the CEC bus */
#define EC_CMD_CEC_WRITE_MSG 0x00B8

/**
 * struct ec_params_cec_write - Message to write to the CEC bus
 * @msg: message content to write to the CEC bus
 */
struct ec_params_cec_write {
	uint8_t msg[EC_MAX_CEC_MSG_LEN];
} __packed;

/* Set various CEC parameters */
#define EC_CMD_CEC_SET 0x00BA

/**
 * struct ec_params_cec_set - CEC parameters set
 * @cmd: parameter type, can be CEC_CMD_ENABLE or CEC_CMD_LOGICAL_ADDRESS
 * @val: in case cmd is CEC_CMD_ENABLE, this field can be 0 to disable CEC
 *	or 1 to enable CEC functionality, in case cmd is CEC_CMD_LOGICAL_ADDRESS,
 *	this field encodes the requested logical address between 0 and 15
 *	or 0xff to unregister
 */
struct ec_params_cec_set {
	uint8_t cmd; /* enum cec_command */
	uint8_t val;
} __packed;

/* Read various CEC parameters */
#define EC_CMD_CEC_GET 0x00BB

/**
 * struct ec_params_cec_get - CEC parameters get
 * @cmd: parameter type, can be CEC_CMD_ENABLE or CEC_CMD_LOGICAL_ADDRESS
 */
struct ec_params_cec_get {
	uint8_t cmd; /* enum cec_command */
} __packed;

/**
 * struct ec_response_cec_get - CEC parameters get response
 * @val: in case cmd was CEC_CMD_ENABLE, this field will 0 if CEC is
 *	disabled or 1 if CEC functionality is enabled,
 *	in case cmd was CEC_CMD_LOGICAL_ADDRESS, this will encode the
 *	configured logical address between 0 and 15 or 0xff if unregistered
 */
struct ec_response_cec_get {
	uint8_t val;
} __packed;

/* CEC parameters command */
enum ec_cec_command {
	/* CEC reading, writing and events enable */
	CEC_CMD_ENABLE,
	/* CEC logical address  */
	CEC_CMD_LOGICAL_ADDRESS,
};

/* Events from CEC to AP */
enum mkbp_cec_event {
	/* Outgoing message was acknowledged by a follower */
	EC_MKBP_CEC_SEND_OK			= BIT(0),
	/* Outgoing message was not acknowledged */
	EC_MKBP_CEC_SEND_FAILED			= BIT(1),
};

/*****************************************************************************/
/*
 * Special commands
 *
 * These do not follow the normal rules for commands.  See each command for
 * details.
 */

/*
 * Reboot NOW
 *
 * This command will work even when the EC LPC interface is busy, because the
 * reboot command is processed at interrupt level.  Note that when the EC
 * reboots, the host will reboot too, so there is no response to this command.
 *
 * Use EC_CMD_REBOOT_EC to reboot the EC more politely.
 */
#define EC_CMD_REBOOT 0xd1  /* Think "die" */

/*
 * Resend last response (not supported on LPC).
 *
 * Returns EC_RES_UNAVAILABLE if there is no response available - for example,
 * there was no previous command, or the previous command's response was too
 * big to save.
 */
#define EC_CMD_RESEND_RESPONSE 0xdb

/*
 * This header byte on a command indicate version 0. Any header byte less
 * than this means that we are talking to an old EC which doesn't support
 * versioning. In that case, we assume version 0.
 *
 * Header bytes greater than this indicate a later version. For example,
 * EC_CMD_VERSION0 + 1 means we are using version 1.
 *
 * The old EC interface must not use commands 0xdc or higher.
 */
#define EC_CMD_VERSION0 0xdc

#endif  /* !__ACPI__ */

/*****************************************************************************/
/*
 * PD commands
 *
 * These commands are for PD MCU communication.
 */

/* EC to PD MCU exchange status command */
#define EC_CMD_PD_EXCHANGE_STATUS 0x100

/* Status of EC being sent to PD */
struct ec_params_pd_status {
	int8_t batt_soc; /* battery state of charge */
} __packed;

/* Status of PD being sent back to EC */
struct ec_response_pd_status {
	int8_t status;        /* PD MCU status */
	uint32_t curr_lim_ma; /* input current limit */
} __packed;

/* Set USB type-C port role and muxes */
#define EC_CMD_USB_PD_CONTROL 0x101

enum usb_pd_control_role {
	USB_PD_CTRL_ROLE_NO_CHANGE = 0,
	USB_PD_CTRL_ROLE_TOGGLE_ON = 1, /* == AUTO */
	USB_PD_CTRL_ROLE_TOGGLE_OFF = 2,
	USB_PD_CTRL_ROLE_FORCE_SINK = 3,
	USB_PD_CTRL_ROLE_FORCE_SOURCE = 4,
};

enum usb_pd_control_mux {
	USB_PD_CTRL_MUX_NO_CHANGE = 0,
	USB_PD_CTRL_MUX_NONE = 1,
	USB_PD_CTRL_MUX_USB = 2,
	USB_PD_CTRL_MUX_DP = 3,
	USB_PD_CTRL_MUX_DOCK = 4,
	USB_PD_CTRL_MUX_AUTO = 5,
};

enum usb_pd_control_swap {
	USB_PD_CTRL_SWAP_NONE = 0,
	USB_PD_CTRL_SWAP_DATA = 1,
	USB_PD_CTRL_SWAP_POWER = 2,
	USB_PD_CTRL_SWAP_VCONN = 3,
	USB_PD_CTRL_SWAP_COUNT
};

struct ec_params_usb_pd_control {
	uint8_t port;
	uint8_t role;
	uint8_t mux;
	uint8_t swap;
} __packed;

#define PD_CTRL_RESP_ENABLED_COMMS      (1 << 0) /* Communication enabled */
#define PD_CTRL_RESP_ENABLED_CONNECTED  (1 << 1) /* Device connected */
#define PD_CTRL_RESP_ENABLED_PD_CAPABLE (1 << 2) /* Partner is PD capable */

#define PD_CTRL_RESP_ROLE_POWER         BIT(0) /* 0=SNK/1=SRC */
#define PD_CTRL_RESP_ROLE_DATA          BIT(1) /* 0=UFP/1=DFP */
#define PD_CTRL_RESP_ROLE_VCONN         BIT(2) /* Vconn status */
#define PD_CTRL_RESP_ROLE_DR_POWER      BIT(3) /* Partner is dualrole power */
#define PD_CTRL_RESP_ROLE_DR_DATA       BIT(4) /* Partner is dualrole data */
#define PD_CTRL_RESP_ROLE_USB_COMM      BIT(5) /* Partner USB comm capable */
#define PD_CTRL_RESP_ROLE_EXT_POWERED   BIT(6) /* Partner externally powerd */

struct ec_response_usb_pd_control_v1 {
	uint8_t enabled;
	uint8_t role;
	uint8_t polarity;
	char state[32];
} __packed;

#define EC_CMD_USB_PD_PORTS 0x102

/* Maximum number of PD ports on a device, num_ports will be <= this */
#define EC_USB_PD_MAX_PORTS 8

struct ec_response_usb_pd_ports {
	uint8_t num_ports;
} __packed;

#define EC_CMD_USB_PD_POWER_INFO 0x103

#define PD_POWER_CHARGING_PORT 0xff
struct ec_params_usb_pd_power_info {
	uint8_t port;
} __packed;

enum usb_chg_type {
	USB_CHG_TYPE_NONE,
	USB_CHG_TYPE_PD,
	USB_CHG_TYPE_C,
	USB_CHG_TYPE_PROPRIETARY,
	USB_CHG_TYPE_BC12_DCP,
	USB_CHG_TYPE_BC12_CDP,
	USB_CHG_TYPE_BC12_SDP,
	USB_CHG_TYPE_OTHER,
	USB_CHG_TYPE_VBUS,
	USB_CHG_TYPE_UNKNOWN,
};
enum usb_power_roles {
	USB_PD_PORT_POWER_DISCONNECTED,
	USB_PD_PORT_POWER_SOURCE,
	USB_PD_PORT_POWER_SINK,
	USB_PD_PORT_POWER_SINK_NOT_CHARGING,
};

struct usb_chg_measures {
	uint16_t voltage_max;
	uint16_t voltage_now;
	uint16_t current_max;
	uint16_t current_lim;
} __packed;

struct ec_response_usb_pd_power_info {
	uint8_t role;
	uint8_t type;
	uint8_t dualrole;
	uint8_t reserved1;
	struct usb_chg_measures meas;
	uint32_t max_power;
} __packed;

struct ec_params_usb_pd_info_request {
	uint8_t port;
} __packed;

/* Read USB-PD Device discovery info */
#define EC_CMD_USB_PD_DISCOVERY 0x0113
struct ec_params_usb_pd_discovery_entry {
	uint16_t vid;  /* USB-IF VID */
	uint16_t pid;  /* USB-IF PID */
	uint8_t ptype; /* product type (hub,periph,cable,ama) */
} __packed;

/* Override default charge behavior */
#define EC_CMD_PD_CHARGE_PORT_OVERRIDE 0x0114

/* Negative port parameters have special meaning */
enum usb_pd_override_ports {
	OVERRIDE_DONT_CHARGE = -2,
	OVERRIDE_OFF = -1,
	/* [0, CONFIG_USB_PD_PORT_COUNT): Port# */
};

struct ec_params_charge_port_override {
	int16_t override_port; /* Override port# */
} __packed;

/* Read (and delete) one entry of PD event log */
#define EC_CMD_PD_GET_LOG_ENTRY 0x0115

struct ec_response_pd_log {
	uint32_t timestamp; /* relative timestamp in milliseconds */
	uint8_t type;       /* event type : see PD_EVENT_xx below */
	uint8_t size_port;  /* [7:5] port number [4:0] payload size in bytes */
	uint16_t data;      /* type-defined data payload */
	uint8_t payload[0]; /* optional additional data payload: 0..16 bytes */
} __packed;

/* The timestamp is the microsecond counter shifted to get about a ms. */
#define PD_LOG_TIMESTAMP_SHIFT 10 /* 1 LSB = 1024us */

#define PD_LOG_SIZE_MASK  0x1f
#define PD_LOG_PORT_MASK  0xe0
#define PD_LOG_PORT_SHIFT    5
#define PD_LOG_PORT_SIZE(port, size) (((port) << PD_LOG_PORT_SHIFT) | \
				      ((size) & PD_LOG_SIZE_MASK))
#define PD_LOG_PORT(size_port) ((size_port) >> PD_LOG_PORT_SHIFT)
#define PD_LOG_SIZE(size_port) ((size_port) & PD_LOG_SIZE_MASK)

/* PD event log : entry types */
/* PD MCU events */
#define PD_EVENT_MCU_BASE       0x00
#define PD_EVENT_MCU_CHARGE             (PD_EVENT_MCU_BASE+0)
#define PD_EVENT_MCU_CONNECT            (PD_EVENT_MCU_BASE+1)
/* Reserved for custom board event */
#define PD_EVENT_MCU_BOARD_CUSTOM       (PD_EVENT_MCU_BASE+2)
/* PD generic accessory events */
#define PD_EVENT_ACC_BASE       0x20
#define PD_EVENT_ACC_RW_FAIL   (PD_EVENT_ACC_BASE+0)
#define PD_EVENT_ACC_RW_ERASE  (PD_EVENT_ACC_BASE+1)
/* PD power supply events */
#define PD_EVENT_PS_BASE        0x40
#define PD_EVENT_PS_FAULT      (PD_EVENT_PS_BASE+0)
/* PD video dongles events */
#define PD_EVENT_VIDEO_BASE     0x60
#define PD_EVENT_VIDEO_DP_MODE (PD_EVENT_VIDEO_BASE+0)
#define PD_EVENT_VIDEO_CODEC   (PD_EVENT_VIDEO_BASE+1)
/* Returned in the "type" field, when there is no entry available */
#define PD_EVENT_NO_ENTRY       0xff

/*
 * PD_EVENT_MCU_CHARGE event definition :
 * the payload is "struct usb_chg_measures"
 * the data field contains the port state flags as defined below :
 */
/* Port partner is a dual role device */
#define CHARGE_FLAGS_DUAL_ROLE         BIT(15)
/* Port is the pending override port */
#define CHARGE_FLAGS_DELAYED_OVERRIDE  BIT(14)
/* Port is the override port */
#define CHARGE_FLAGS_OVERRIDE          BIT(13)
/* Charger type */
#define CHARGE_FLAGS_TYPE_SHIFT               3
#define CHARGE_FLAGS_TYPE_MASK       (0xf << CHARGE_FLAGS_TYPE_SHIFT)
/* Power delivery role */
#define CHARGE_FLAGS_ROLE_MASK         (7 <<  0)

/*
 * PD_EVENT_PS_FAULT data field flags definition :
 */
#define PS_FAULT_OCP                          1
#define PS_FAULT_FAST_OCP                     2
#define PS_FAULT_OVP                          3
#define PS_FAULT_DISCH                        4

/*
 * PD_EVENT_VIDEO_CODEC payload is "struct mcdp_info".
 */
struct mcdp_version {
	uint8_t major;
	uint8_t minor;
	uint16_t build;
} __packed;

struct mcdp_info {
	uint8_t family[2];
	uint8_t chipid[2];
	struct mcdp_version irom;
	struct mcdp_version fw;
} __packed;

/* struct mcdp_info field decoding */
#define MCDP_CHIPID(chipid) ((chipid[0] << 8) | chipid[1])
#define MCDP_FAMILY(family) ((family[0] << 8) | family[1])

/* Get info about USB-C SS muxes */
#define EC_CMD_USB_PD_MUX_INFO 0x11a

struct ec_params_usb_pd_mux_info {
	uint8_t port; /* USB-C port number */
} __packed;

/* Flags representing mux state */
#define USB_PD_MUX_USB_ENABLED       (1 << 0)
#define USB_PD_MUX_DP_ENABLED        (1 << 1)
#define USB_PD_MUX_POLARITY_INVERTED (1 << 2)
#define USB_PD_MUX_HPD_IRQ           (1 << 3)

struct ec_response_usb_pd_mux_info {
	uint8_t flags; /* USB_PD_MUX_*-encoded USB mux state */
} __packed;

/*****************************************************************************/
/*
 * Passthru commands
 *
 * Some platforms have sub-processors chained to each other.  For example.
 *
 *     AP <--> EC <--> PD MCU
 *
 * The top 2 bits of the command number are used to indicate which device the
 * command is intended for.  Device 0 is always the device receiving the
 * command; other device mapping is board-specific.
 *
 * When a device receives a command to be passed to a sub-processor, it passes
 * it on with the device number set back to 0.  This allows the sub-processor
 * to remain blissfully unaware of whether the command originated on the next
 * device up the chain, or was passed through from the AP.
 *
 * In the above example, if the AP wants to send command 0x0002 to the PD MCU,
 *     AP sends command 0x4002 to the EC
 *     EC sends command 0x0002 to the PD MCU
 *     EC forwards PD MCU response back to the AP
 */

/* Offset and max command number for sub-device n */
#define EC_CMD_PASSTHRU_OFFSET(n) (0x4000 * (n))
#define EC_CMD_PASSTHRU_MAX(n) (EC_CMD_PASSTHRU_OFFSET(n) + 0x3fff)

/*****************************************************************************/
/*
 * Deprecated constants. These constants have been renamed for clarity. The
 * meaning and size has not changed. Programs that use the old names should
 * switch to the new names soon, as the old names may not be carried forward
 * forever.
 */
#define EC_HOST_PARAM_SIZE      EC_PROTO2_MAX_PARAM_SIZE
#define EC_LPC_ADDR_OLD_PARAM   EC_HOST_CMD_REGION1
#define EC_OLD_PARAM_SIZE       EC_HOST_CMD_REGION_SIZE

#endif  /* __CROS_EC_COMMANDS_H */
