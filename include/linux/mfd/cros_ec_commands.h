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
 * Protocol overview
 *
 * request:  CMD [ P0 P1 P2 ... Pn S ]
 * response: ERR [ P0 P1 P2 ... Pn S ]
 *
 * where the bytes are defined as follow :
 *      - CMD is the command code. (defined by EC_CMD_ constants)
 *      - ERR is the error code. (defined by EC_RES_ constants)
 *      - Px is the optional payload.
 *        it is not sent if the error code is not success.
 *        (defined by ec_params_ and ec_response_ structures)
 *      - S is the checksum which is the sum of all payload bytes.
 *
 * On LPC, CMD and ERR are sent/received at EC_LPC_ADDR_KERNEL|USER_CMD
 * and the payloads are sent/received at EC_LPC_ADDR_KERNEL|USER_PARAM.
 * On I2C, all bytes are sent serially in the same message.
 */

/* Current version of this protocol */
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
#define EC_LPC_ADDR_HOST_ARGS  0x800
#define EC_LPC_ADDR_HOST_PARAM 0x804
#define EC_HOST_PARAM_SIZE     0x0fc  /* Size of param area in bytes */

/* I/O addresses for host command params, old interface */
#define EC_LPC_ADDR_OLD_PARAM  0x880
#define EC_OLD_PARAM_SIZE      0x080  /* Size of param area in bytes */

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
#define EC_MEMMAP_TEMP_SENSOR      0x00 /* Temp sensors */
#define EC_MEMMAP_FAN              0x10 /* Fan speeds */
#define EC_MEMMAP_TEMP_SENSOR_B    0x18 /* Temp sensors (second set) */
#define EC_MEMMAP_ID               0x20 /* 'E' 'C' */
#define EC_MEMMAP_ID_VERSION       0x22 /* Version of data in 0x20 - 0x2f */
#define EC_MEMMAP_THERMAL_VERSION  0x23 /* Version of data in 0x00 - 0x1f */
#define EC_MEMMAP_BATTERY_VERSION  0x24 /* Version of data in 0x40 - 0x7f */
#define EC_MEMMAP_SWITCHES_VERSION 0x25 /* Version of data in 0x30 - 0x33 */
#define EC_MEMMAP_EVENTS_VERSION   0x26 /* Version of data in 0x34 - 0x3f */
#define EC_MEMMAP_HOST_CMD_FLAGS   0x27 /* Host command interface flags */
#define EC_MEMMAP_SWITCHES         0x30
#define EC_MEMMAP_HOST_EVENTS      0x34
#define EC_MEMMAP_BATT_VOLT        0x40 /* Battery Present Voltage */
#define EC_MEMMAP_BATT_RATE        0x44 /* Battery Present Rate */
#define EC_MEMMAP_BATT_CAP         0x48 /* Battery Remaining Capacity */
#define EC_MEMMAP_BATT_FLAG        0x4c /* Battery State, defined below */
#define EC_MEMMAP_BATT_DCAP        0x50 /* Battery Design Capacity */
#define EC_MEMMAP_BATT_DVLT        0x54 /* Battery Design Voltage */
#define EC_MEMMAP_BATT_LFCC        0x58 /* Battery Last Full Charge Capacity */
#define EC_MEMMAP_BATT_CCNT        0x5c /* Battery Cycle Count */
#define EC_MEMMAP_BATT_MFGR        0x60 /* Battery Manufacturer String */
#define EC_MEMMAP_BATT_MODEL       0x68 /* Battery Model Number String */
#define EC_MEMMAP_BATT_SERIAL      0x70 /* Battery Serial Number String */
#define EC_MEMMAP_BATT_TYPE        0x78 /* Battery Type String */

/* Number of temp sensors at EC_MEMMAP_TEMP_SENSOR */
#define EC_TEMP_SENSOR_ENTRIES     16
/*
 * Number of temp sensors at EC_MEMMAP_TEMP_SENSOR_B.
 *
 * Valid only if EC_MEMMAP_THERMAL_VERSION returns >= 2.
 */
#define EC_TEMP_SENSOR_B_ENTRIES      8
#define EC_TEMP_SENSOR_NOT_PRESENT    0xff
#define EC_TEMP_SENSOR_ERROR          0xfe
#define EC_TEMP_SENSOR_NOT_POWERED    0xfd
#define EC_TEMP_SENSOR_NOT_CALIBRATED 0xfc
/*
 * The offset of temperature value stored in mapped memory.  This allows
 * reporting a temperature range of 200K to 454K = -73C to 181C.
 */
#define EC_TEMP_SENSOR_OFFSET      200

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
/* Recovery requested via keyboard */
#define EC_SWITCH_KEYBOARD_RECOVERY      0x08
/* Recovery requested via dedicated signal (from servo board) */
#define EC_SWITCH_DEDICATED_RECOVERY     0x10
/* Was fake developer mode switch; now unused.  Remove in next refactor. */
#define EC_SWITCH_IGNORE0                0x20

/* Host command interface flags */
/* Host command interface supports LPC args (LPC interface only) */
#define EC_HOST_CMD_FLAG_LPC_ARGS_SUPPORTED  0x01

/* Wireless switch flags */
#define EC_WIRELESS_SWITCH_WLAN      0x01
#define EC_WIRELESS_SWITCH_BLUETOOTH 0x02

/*
 * This header file is used in coreboot both in C and ACPI code.  The ACPI code
 * is pre-processed to handle constants but the ASL compiler is unable to
 * handle actual C code so keep it separate.
 */
#ifndef __ACPI__

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

/*
 * Notes on commands:
 *
 * Each command is an 8-byte command value.  Commands which take params or
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


/*****************************************************************************/
/* Flash commands */

/* Get flash info */
#define EC_CMD_FLASH_INFO 0x10

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

struct ec_params_flash_write {
	uint32_t offset;   /* Byte offset to write */
	uint32_t size;     /* Size to write in bytes */
	/*
	 * Data to write.  Could really use EC_PARAM_SIZE - 8, but tidiest to
	 * use a power of 2 so writes stay aligned.
	 */
	uint8_t data[64];
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
	EC_FLASH_REGION_RO,
	/* Region which holds rewritable EC image */
	EC_FLASH_REGION_RW,
	/*
	 * Region which should be write-protected in the factory (a superset of
	 * EC_FLASH_REGION_RO)
	 */
	EC_FLASH_REGION_WP_RO,
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
struct lightbar_params {
	/* Timing */
	int google_ramp_up;
	int google_ramp_down;
	int s3s0_ramp_up;
	int s0_tick_delay[2];			/* AC=0/1 */
	int s0a_tick_delay[2];			/* AC=0/1 */
	int s0s3_ramp_down;
	int s3_sleep_for;
	int s3_ramp_up;
	int s3_ramp_down;

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

struct ec_params_lightbar {
	uint8_t cmd;		      /* Command (see enum lightbar_command) */
	union {
		struct {
			/* no args */
		} dump, off, on, init, get_seq, get_params;

		struct num {
			uint8_t num;
		} brightness, seq, demo;

		struct reg {
			uint8_t ctrl, reg, value;
		} reg;

		struct rgb {
			uint8_t led, red, green, blue;
		} rgb;

		struct lightbar_params set_params;
	};
} __packed;

struct ec_response_lightbar {
	union {
		struct dump {
			struct {
				uint8_t reg;
				uint8_t ic0;
				uint8_t ic1;
			} vals[23];
		} dump;

		struct get_seq {
			uint8_t num;
		} get_seq;

		struct lightbar_params get_params;

		struct {
			/* no return params */
		} off, on, init, brightness, seq, reg, rgb, demo, set_params;
	};
} __packed;

/* Lightbar commands */
enum lightbar_command {
	LIGHTBAR_CMD_DUMP = 0,
	LIGHTBAR_CMD_OFF = 1,
	LIGHTBAR_CMD_ON = 2,
	LIGHTBAR_CMD_INIT = 3,
	LIGHTBAR_CMD_BRIGHTNESS = 4,
	LIGHTBAR_CMD_SEQ = 5,
	LIGHTBAR_CMD_REG = 6,
	LIGHTBAR_CMD_RGB = 7,
	LIGHTBAR_CMD_GET_SEQ = 8,
	LIGHTBAR_CMD_DEMO = 9,
	LIGHTBAR_CMD_GET_PARAMS = 10,
	LIGHTBAR_CMD_SET_PARAMS = 11,
	LIGHTBAR_NUM_CMDS
};

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

/*****************************************************************************/
/* Port80 log access */

/* Get last port80 code from previous boot */
#define EC_CMD_PORT80_LAST_BOOT 0x48

struct ec_response_port80_last_boot {
	uint16_t code;
} __packed;

/*****************************************************************************/
/* Thermal engine commands */

/* Set thershold value */
#define EC_CMD_THERMAL_SET_THRESHOLD 0x50

struct ec_params_thermal_set_threshold {
	uint8_t sensor_type;
	uint8_t threshold_id;
	uint16_t value;
} __packed;

/* Get threshold value */
#define EC_CMD_THERMAL_GET_THRESHOLD 0x51

struct ec_params_thermal_get_threshold {
	uint8_t sensor_type;
	uint8_t threshold_id;
} __packed;

struct ec_response_thermal_get_threshold {
	uint16_t value;
} __packed;

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

/*****************************************************************************/
/* MKBP - Matrix KeyBoard Protocol */

/*
 * Read key state
 *
 * Returns raw data for keyboard cols; see ec_response_mkbp_info.cols for
 * expected response size.
 */
#define EC_CMD_MKBP_STATE 0x60

/* Provide information about the matrix : number of rows and columns */
#define EC_CMD_MKBP_INFO 0x61

struct ec_response_mkbp_info {
	uint32_t rows;
	uint32_t cols;
	uint8_t switches;
} __packed;

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

struct ec_params_switch_enable_wireless {
	uint8_t enabled;
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

struct ec_params_gpio_get {
	char name[32];
} __packed;
struct ec_response_gpio_get {
	uint8_t val;
} __packed;

/*****************************************************************************/
/* I2C commands. Only available when flash write protect is unlocked. */

/* Read I2C bus */
#define EC_CMD_I2C_READ 0x94

struct ec_params_i2c_read {
	uint16_t addr;
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
	uint16_t addr;
	uint8_t write_size; /* Either 8 or 16. */
	uint8_t port;
	uint8_t offset;
} __packed;

/*****************************************************************************/
/* Charge state commands. Only available when flash write protect unlocked. */

/* Force charge state machine to stop in idle mode */
#define EC_CMD_CHARGE_FORCE_IDLE 0x96

struct ec_params_force_idle {
	uint8_t enabled;
} __packed;

/*****************************************************************************/
/* Console commands. Only available when flash write protect is unlocked. */

/* Snapshot console output buffer for use by EC_CMD_CONSOLE_READ. */
#define EC_CMD_CONSOLE_SNAPSHOT 0x97

/*
 * Read next chunk of data from saved snapshot.
 *
 * Response is null-terminated string.  Empty string, if there is no more
 * remaining output.
 */
#define EC_CMD_CONSOLE_READ 0x98

/*****************************************************************************/

/*
 * Cut off battery power output if the battery supports.
 *
 * For unsupported battery, just don't implement this command and lets EC
 * return EC_RES_INVALID_COMMAND.
 */
#define EC_CMD_BATTERY_CUT_OFF 0x99

/*****************************************************************************/
/* Temporary debug commands. TODO: remove this crosbug.com/p/13849 */

/*
 * Dump charge state machine context.
 *
 * Response is a binary dump of charge state machine context.
 */
#define EC_CMD_CHARGE_DUMP 0xa0

/*
 * Set maximum battery charging current.
 */
#define EC_CMD_CHARGE_CURRENT_LIMIT 0xa1

struct ec_params_current_limit {
	uint32_t limit;
} __packed;

/*****************************************************************************/
/* System commands */

/*
 * TODO: this is a confusing name, since it doesn't necessarily reboot the EC.
 * Rename to "set image" or something similar.
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

/* Current version of ACPI memory address space */
#define EC_ACPI_MEM_VERSION_CURRENT 1


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
 * The old EC interface must not use commands 0dc or higher.
 */
#define EC_CMD_VERSION0 0xdc

#endif  /* !__ACPI__ */

#endif  /* __CROS_EC_COMMANDS_H */
