#ifndef _SMU_H
#define _SMU_H

/*
 * Definitions for talking to the SMU chip in newer G5 PowerMacs
 */

#include <linux/config.h>
#include <linux/list.h>

/*
 * Known SMU commands
 *
 * Most of what is below comes from looking at the Open Firmware driver,
 * though this is still incomplete and could use better documentation here
 * or there...
 */


/*
 * Partition info commands
 *
 * I do not know what those are for at this point
 */
#define SMU_CMD_PARTITION_COMMAND		0x3e


/*
 * Fan control
 *
 * This is a "mux" for fan control commands, first byte is the
 * "sub" command.
 */
#define SMU_CMD_FAN_COMMAND			0x4a


/*
 * Battery access
 *
 * Same command number as the PMU, could it be same syntax ?
 */
#define SMU_CMD_BATTERY_COMMAND			0x6f
#define   SMU_CMD_GET_BATTERY_INFO		0x00

/*
 * Real time clock control
 *
 * This is a "mux", first data byte contains the "sub" command.
 * The "RTC" part of the SMU controls the date, time, powerup
 * timer, but also a PRAM
 *
 * Dates are in BCD format on 7 bytes:
 * [sec] [min] [hour] [weekday] [month day] [month] [year]
 * with month being 1 based and year minus 100
 */
#define SMU_CMD_RTC_COMMAND			0x8e
#define   SMU_CMD_RTC_SET_PWRUP_TIMER		0x00 /* i: 7 bytes date */
#define   SMU_CMD_RTC_GET_PWRUP_TIMER		0x01 /* o: 7 bytes date */
#define   SMU_CMD_RTC_STOP_PWRUP_TIMER		0x02
#define   SMU_CMD_RTC_SET_PRAM_BYTE_ACC		0x20 /* i: 1 byte (address?) */
#define   SMU_CMD_RTC_SET_PRAM_AUTOINC		0x21 /* i: 1 byte (data?) */
#define   SMU_CMD_RTC_SET_PRAM_LO_BYTES 	0x22 /* i: 10 bytes */
#define   SMU_CMD_RTC_SET_PRAM_HI_BYTES 	0x23 /* i: 10 bytes */
#define   SMU_CMD_RTC_GET_PRAM_BYTE		0x28 /* i: 1 bytes (address?) */
#define   SMU_CMD_RTC_GET_PRAM_LO_BYTES 	0x29 /* o: 10 bytes */
#define   SMU_CMD_RTC_GET_PRAM_HI_BYTES 	0x2a /* o: 10 bytes */
#define	  SMU_CMD_RTC_SET_DATETIME		0x80 /* i: 7 bytes date */
#define   SMU_CMD_RTC_GET_DATETIME		0x81 /* o: 7 bytes date */

 /*
  * i2c commands
  *
  * To issue an i2c command, first is to send a parameter block to the
  * the SMU. This is a command of type 0x9a with 9 bytes of header
  * eventually followed by data for a write:
  *
  * 0: bus number (from device-tree usually, SMU has lots of busses !)
  * 1: transfer type/format (see below)
  * 2: device address. For combined and combined4 type transfers, this
  *    is the "write" version of the address (bit 0x01 cleared)
  * 3: subaddress length (0..3)
  * 4: subaddress byte 0 (or only byte for subaddress length 1)
  * 5: subaddress byte 1
  * 6: subaddress byte 2
  * 7: combined address (device address for combined mode data phase)
  * 8: data length
  *
  * The transfer types are the same good old Apple ones it seems,
  * that is:
  *   - 0x00: Simple transfer
  *   - 0x01: Subaddress transfer (addr write + data tx, no restart)
  *   - 0x02: Combined transfer (addr write + restart + data tx)
  *
  * This is then followed by actual data for a write.
  *
  * At this point, the OF driver seems to have a limitation on transfer
  * sizes of 0xd bytes on reads and 0x5 bytes on writes. I do not know
  * wether this is just an OF limit due to some temporary buffer size
  * or if this is an SMU imposed limit. This driver has the same limitation
  * for now as I use a 0x10 bytes temporary buffer as well
  *
  * Once that is completed, a response is expected from the SMU. This is
  * obtained via a command of type 0x9a with a length of 1 byte containing
  * 0 as the data byte. OF also fills the rest of the data buffer with 0xff's
  * though I can't tell yet if this is actually necessary. Once this command
  * is complete, at this point, all I can tell is what OF does. OF tests
  * byte 0 of the reply:
  *   - on read, 0xfe or 0xfc : bus is busy, wait (see below) or nak ?
  *   - on read, 0x00 or 0x01 : reply is in buffer (after the byte 0)
  *   - on write, < 0 -> failure (immediate exit)
  *   - else, OF just exists (without error, weird)
  *
  * So on read, there is this wait-for-busy thing when getting a 0xfc or
  * 0xfe result. OF does a loop of up to 64 retries, waiting 20ms and
  * doing the above again until either the retries expire or the result
  * is no longer 0xfe or 0xfc
  *
  * The Darwin I2C driver is less subtle though. On any non-success status
  * from the response command, it waits 5ms and tries again up to 20 times,
  * it doesn't differenciate between fatal errors or "busy" status.
  *
  * This driver provides an asynchronous paramblock based i2c command
  * interface to be used either directly by low level code or by a higher
  * level driver interfacing to the linux i2c layer. The current
  * implementation of this relies on working timers & timer interrupts
  * though, so be careful of calling context for now. This may be "fixed"
  * in the future by adding a polling facility.
  */
#define SMU_CMD_I2C_COMMAND			0x9a
          /* transfer types */
#define   SMU_I2C_TRANSFER_SIMPLE	0x00
#define   SMU_I2C_TRANSFER_STDSUB	0x01
#define   SMU_I2C_TRANSFER_COMBINED	0x02

/*
 * Power supply control
 *
 * The "sub" command is an ASCII string in the data, the
 * data lenght is that of the string.
 *
 * The VSLEW command can be used to get or set the voltage slewing.
 *  - lenght 5 (only "VSLEW") : it returns "DONE" and 3 bytes of
 *    reply at data offset 6, 7 and 8.
 *  - lenght 8 ("VSLEWxyz") has 3 additional bytes appended, and is
 *    used to set the voltage slewing point. The SMU replies with "DONE"
 * I yet have to figure out their exact meaning of those 3 bytes in
 * both cases.
 *
 */
#define SMU_CMD_POWER_COMMAND			0xaa
#define   SMU_CMD_POWER_RESTART		       	"RESTART"
#define   SMU_CMD_POWER_SHUTDOWN		"SHUTDOWN"
#define   SMU_CMD_POWER_VOLTAGE_SLEW		"VSLEW"

/* Misc commands
 *
 * This command seem to be a grab bag of various things
 */
#define SMU_CMD_MISC_df_COMMAND			0xdf
#define   SMU_CMD_MISC_df_SET_DISPLAY_LIT	0x02 /* i: 1 byte */
#define   SMU_CMD_MISC_df_NMI_OPTION		0x04

/*
 * Version info commands
 *
 * I haven't quite tried to figure out how these work
 */
#define SMU_CMD_VERSION_COMMAND			0xea


/*
 * Misc commands
 *
 * This command seem to be a grab bag of various things
 */
#define SMU_CMD_MISC_ee_COMMAND			0xee
#define   SMU_CMD_MISC_ee_GET_DATABLOCK_REC	0x02
#define	  SMU_CMD_MISC_ee_LEDS_CTRL		0x04 /* i: 00 (00,01) [00] */
#define   SMU_CMD_MISC_ee_GET_DATA		0x05 /* i: 00 , o: ?? */



/*
 * - Kernel side interface -
 */

#ifdef __KERNEL__

/*
 * Asynchronous SMU commands
 *
 * Fill up this structure and submit it via smu_queue_command(),
 * and get notified by the optional done() callback, or because
 * status becomes != 1
 */

struct smu_cmd;

struct smu_cmd
{
	/* public */
	u8			cmd;		/* command */
	int			data_len;	/* data len */
	int			reply_len;	/* reply len */
	void			*data_buf;	/* data buffer */
	void			*reply_buf;	/* reply buffer */
	int			status;		/* command status */
	void			(*done)(struct smu_cmd *cmd, void *misc);
	void			*misc;

	/* private */
	struct list_head	link;
};

/*
 * Queues an SMU command, all fields have to be initialized
 */
extern int smu_queue_cmd(struct smu_cmd *cmd);

/*
 * Simple command wrapper. This structure embeds a small buffer
 * to ease sending simple SMU commands from the stack
 */
struct smu_simple_cmd
{
	struct smu_cmd	cmd;
	u8	       	buffer[16];
};

/*
 * Queues a simple command. All fields will be initialized by that
 * function
 */
extern int smu_queue_simple(struct smu_simple_cmd *scmd, u8 command,
			    unsigned int data_len,
			    void (*done)(struct smu_cmd *cmd, void *misc),
			    void *misc,
			    ...);

/*
 * Completion helper. Pass it to smu_queue_simple or as 'done'
 * member to smu_queue_cmd, it will call complete() on the struct
 * completion passed in the "misc" argument
 */
extern void smu_done_complete(struct smu_cmd *cmd, void *misc);

/*
 * Synchronous helpers. Will spin-wait for completion of a command
 */
extern void smu_spinwait_cmd(struct smu_cmd *cmd);

static inline void smu_spinwait_simple(struct smu_simple_cmd *scmd)
{
	smu_spinwait_cmd(&scmd->cmd);
}

/*
 * Poll routine to call if blocked with irqs off
 */
extern void smu_poll(void);


/*
 * Init routine, presence check....
 */
extern int smu_init(void);
extern int smu_present(void);
struct of_device;
extern struct of_device *smu_get_ofdev(void);


/*
 * Common command wrappers
 */
extern void smu_shutdown(void);
extern void smu_restart(void);
struct rtc_time;
extern int smu_get_rtc_time(struct rtc_time *time, int spinwait);
extern int smu_set_rtc_time(struct rtc_time *time, int spinwait);

/*
 * SMU command buffer absolute address, exported by pmac_setup,
 * this is allocated very early during boot.
 */
extern unsigned long smu_cmdbuf_abs;


/*
 * Kenrel asynchronous i2c interface
 */

/* SMU i2c header, exactly matches i2c header on wire */
struct smu_i2c_param
{
	u8	bus;		/* SMU bus ID (from device tree) */
	u8	type;		/* i2c transfer type */
	u8	devaddr;	/* device address (includes direction) */
	u8	sublen;		/* subaddress length */
	u8	subaddr[3];	/* subaddress */
	u8	caddr;		/* combined address, filled by SMU driver */
	u8	datalen;	/* length of transfer */
	u8	data[7];	/* data */
};

#define SMU_I2C_READ_MAX	0x0d
#define SMU_I2C_WRITE_MAX	0x05

struct smu_i2c_cmd
{
	/* public */
	struct smu_i2c_param	info;
	void			(*done)(struct smu_i2c_cmd *cmd, void *misc);
	void			*misc;
	int			status; /* 1 = pending, 0 = ok, <0 = fail */

	/* private */
	struct smu_cmd		scmd;
	int			read;
	int			stage;
	int			retries;
	u8			pdata[0x10];
	struct list_head	link;
};

/*
 * Call this to queue an i2c command to the SMU. You must fill info,
 * including info.data for a write, done and misc.
 * For now, no polling interface is provided so you have to use completion
 * callback.
 */
extern int smu_queue_i2c(struct smu_i2c_cmd *cmd);


#endif /* __KERNEL__ */

/*
 * - Userland interface -
 */

/*
 * A given instance of the device can be configured for 2 different
 * things at the moment:
 *
 *  - sending SMU commands (default at open() time)
 *  - receiving SMU events (not yet implemented)
 *
 * Commands are written with write() of a command block. They can be
 * "driver" commands (for example to switch to event reception mode)
 * or real SMU commands. They are made of a header followed by command
 * data if any.
 *
 * For SMU commands (not for driver commands), you can then read() back
 * a reply. The reader will be blocked or not depending on how the device
 * file is opened. poll() isn't implemented yet. The reply will consist
 * of a header as well, followed by the reply data if any. You should
 * always provide a buffer large enough for the maximum reply data, I
 * recommand one page.
 *
 * It is illegal to send SMU commands through a file descriptor configured
 * for events reception
 *
 */
struct smu_user_cmd_hdr
{
	__u32		cmdtype;
#define SMU_CMDTYPE_SMU			0	/* SMU command */
#define SMU_CMDTYPE_WANTS_EVENTS	1	/* switch fd to events mode */

	__u8		cmd;			/* SMU command byte */
	__u32		data_len;		/* Lenght of data following */
};

struct smu_user_reply_hdr
{
	__u32		status;			/* Command status */
	__u32		reply_len;		/* Lenght of data follwing */
};

#endif /*  _SMU_H */
