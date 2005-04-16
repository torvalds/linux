#ifndef _QIC117_H
#define _QIC117_H

/*
 *      Copyright (C) 1993-1996 Bas Laarhoven,
 *                (C) 1997      Claus-Justus Heine.

 This program is free software; you can redistribute it and/or modify
 it under the terms of the GNU General Public License as published by
 the Free Software Foundation; either version 2, or (at your option)
 any later version.

 This program is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 GNU General Public License for more details.

 You should have received a copy of the GNU General Public License
 along with this program; see the file COPYING.  If not, write to
 the Free Software Foundation, 675 Mass Ave, Cambridge, MA 02139, USA.

 *
 * $Source: /homes/cvs/ftape-stacked/include/linux/qic117.h,v $
 * $Revision: 1.2 $
 * $Date: 1997/10/05 19:19:32 $
 *
 *      This file contains QIC-117 spec. related definitions for the
 *      QIC-40/80/3010/3020 floppy-tape driver "ftape" for Linux.
 *
 *      These data were taken from the Quarter-Inch Cartridge
 *      Drive Standards, Inc. document titled:
 *      `Common Command Set Interface Specification for Flexible
 *       Disk Controller Based Minicartridge Tape Drives'
 *       document QIC-117 Revision J, 28 Aug 96.
 *      For more information, contact:
 *       Quarter-Inch Cartridge Drive Standards, Inc.
 *       311 East Carrillo Street
 *       Santa Barbara, California 93101
 *       Telephone (805) 963-3853
 *       Fax       (805) 962-1541
 *       WWW       http://www.qic.org
 *
 *      Current QIC standard revisions (of interest) are:
 *       QIC-40-MC,   Rev. M,  2 Sep 92.
 *       QIC-80-MC,   Rev. N, 20 Mar 96.
 *       QIC-80-MC,   Rev. K, 15 Dec 94.
 *       QIC-113,     Rev. G, 15 Jun 95.
 *       QIC-117,     Rev. J, 28 Aug 96.
 *       QIC-122,     Rev. B,  6 Mar 91.
 *       QIC-130,     Rev. C,  2 Sep 92.
 *       QIC-3010-MC, Rev. F, 14 Jun 95.
 *       QIC-3020-MC, Rev. G, 31 Aug 95.
 *       QIC-CRF3,    Rev. B, 15 Jun 95.
 * */

/*
 *      QIC-117 common command set rev. J.
 *      These commands are sent to the tape unit
 *      as number of pulses over the step line.
 */

typedef enum {
	QIC_NO_COMMAND                  = 0,
	QIC_RESET 			= 1,
	QIC_REPORT_NEXT_BIT		= 2,
	QIC_PAUSE 			= 3,
	QIC_MICRO_STEP_PAUSE		= 4,
	QIC_ALTERNATE_TIMEOUT		= 5,
	QIC_REPORT_DRIVE_STATUS		= 6,
	QIC_REPORT_ERROR_CODE		= 7,
	QIC_REPORT_DRIVE_CONFIGURATION	= 8,
	QIC_REPORT_ROM_VERSION		= 9,
	QIC_LOGICAL_FORWARD		= 10,
	QIC_PHYSICAL_REVERSE		= 11,
	QIC_PHYSICAL_FORWARD		= 12,
	QIC_SEEK_HEAD_TO_TRACK		= 13,
	QIC_SEEK_LOAD_POINT		= 14,
	QIC_ENTER_FORMAT_MODE		= 15,
	QIC_WRITE_REFERENCE_BURST	= 16,
	QIC_ENTER_VERIFY_MODE		= 17,
	QIC_STOP_TAPE			= 18,
/* commands 19-20: reserved */
	QIC_MICRO_STEP_HEAD_UP		= 21,
	QIC_MICRO_STEP_HEAD_DOWN	= 22,
	QIC_SOFT_SELECT			= 23,
	QIC_SOFT_DESELECT		= 24,
	QIC_SKIP_REVERSE		= 25,
	QIC_SKIP_FORWARD		= 26,
	QIC_SELECT_RATE			= 27,
/* command 27, in ccs2: Select Rate or Format */
	QIC_ENTER_DIAGNOSTIC_1		= 28,
	QIC_ENTER_DIAGNOSTIC_2		= 29,
	QIC_ENTER_PRIMARY_MODE		= 30,
/* command 31: vendor unique */
	QIC_REPORT_VENDOR_ID		= 32,
	QIC_REPORT_TAPE_STATUS		= 33,
	QIC_SKIP_EXTENDED_REVERSE	= 34,
	QIC_SKIP_EXTENDED_FORWARD	= 35,
	QIC_CALIBRATE_TAPE_LENGTH	= 36,
	QIC_REPORT_FORMAT_SEGMENTS	= 37,
	QIC_SET_FORMAT_SEGMENTS		= 38,
/* commands 39-45: reserved */
	QIC_PHANTOM_SELECT		= 46,
	QIC_PHANTOM_DESELECT		= 47
} qic117_cmd_t;

typedef enum {
	discretional = 0, required, ccs1, ccs2
} qic_compatibility;

typedef enum {
	unused, mode, motion, report
} command_types;

struct qic117_command_table {
	char *name;
	__u8 mask;
	__u8 state;
	__u8 cmd_type;
	__u8 non_intr;
	__u8 level;
};

#define QIC117_COMMANDS {\
/* command                           mask  state cmd_type           */\
/* |    name                         |     |     |       non_intr   */\
/* |    |                            |     |     |       |  level   */\
/* 0*/ {NULL,                        0x00, 0x00, mode,   0, discretional},\
/* 1*/ {"soft reset",                0x00, 0x00, motion, 1, required},\
/* 2*/ {"report next bit",           0x00, 0x00, report, 0, required},\
/* 3*/ {"pause",                     0x36, 0x24, motion, 1, required},\
/* 4*/ {"micro step pause",          0x36, 0x24, motion, 1, required},\
/* 5*/ {"alternate command timeout", 0x00, 0x00, mode,   0, required},\
/* 6*/ {"report drive status",       0x00, 0x00, report, 0, required},\
/* 7*/ {"report error code",         0x01, 0x01, report, 0, required},\
/* 8*/ {"report drive configuration",0x00, 0x00, report, 0, required},\
/* 9*/ {"report rom version",        0x00, 0x00, report, 0, required},\
/*10*/ {"logical forward",           0x37, 0x25, motion, 0, required},\
/*11*/ {"physical reverse",          0x17, 0x05, motion, 0, required},\
/*12*/ {"physical forward",          0x17, 0x05, motion, 0, required},\
/*13*/ {"seek head to track",        0x37, 0x25, motion, 0, required},\
/*14*/ {"seek load point",           0x17, 0x05, motion, 1, required},\
/*15*/ {"enter format mode",         0x1f, 0x05, mode,   0, required},\
/*16*/ {"write reference burst",     0x1f, 0x05, motion, 1, required},\
/*17*/ {"enter verify mode",         0x37, 0x25, mode,   0, required},\
/*18*/ {"stop tape",                 0x00, 0x00, motion, 1, required},\
/*19*/ {"reserved (19)",             0x00, 0x00, unused, 0, discretional},\
/*20*/ {"reserved (20)",             0x00, 0x00, unused, 0, discretional},\
/*21*/ {"micro step head up",        0x02, 0x00, motion, 0, required},\
/*22*/ {"micro step head down",      0x02, 0x00, motion, 0, required},\
/*23*/ {"soft select",               0x00, 0x00, mode,   0, discretional},\
/*24*/ {"soft deselect",             0x00, 0x00, mode,   0, discretional},\
/*25*/ {"skip segments reverse",     0x36, 0x24, motion, 1, required},\
/*26*/ {"skip segments forward",     0x36, 0x24, motion, 1, required},\
/*27*/ {"select rate or format",     0x03, 0x01, mode,   0, required /* [ccs2] */},\
/*28*/ {"enter diag mode 1",         0x00, 0x00, mode,   0, discretional},\
/*29*/ {"enter diag mode 2",         0x00, 0x00, mode,   0, discretional},\
/*30*/ {"enter primary mode",        0x00, 0x00, mode,   0, required},\
/*31*/ {"vendor unique (31)",        0x00, 0x00, unused, 0, discretional},\
/*32*/ {"report vendor id",          0x00, 0x00, report, 0, required},\
/*33*/ {"report tape status",        0x04, 0x04, report, 0, ccs1},\
/*34*/ {"skip extended reverse",     0x36, 0x24, motion, 1, ccs1},\
/*35*/ {"skip extended forward",     0x36, 0x24, motion, 1, ccs1},\
/*36*/ {"calibrate tape length",     0x17, 0x05, motion, 1, ccs2},\
/*37*/ {"report format segments",    0x17, 0x05, report, 0, ccs2},\
/*38*/ {"set format segments",       0x17, 0x05, mode,   0, ccs2},\
/*39*/ {"reserved (39)",             0x00, 0x00, unused, 0, discretional},\
/*40*/ {"vendor unique (40)",        0x00, 0x00, unused, 0, discretional},\
/*41*/ {"vendor unique (41)",        0x00, 0x00, unused, 0, discretional},\
/*42*/ {"vendor unique (42)",        0x00, 0x00, unused, 0, discretional},\
/*43*/ {"vendor unique (43)",        0x00, 0x00, unused, 0, discretional},\
/*44*/ {"vendor unique (44)",        0x00, 0x00, unused, 0, discretional},\
/*45*/ {"vendor unique (45)",        0x00, 0x00, unused, 0, discretional},\
/*46*/ {"phantom select",            0x00, 0x00, mode,   0, discretional},\
/*47*/ {"phantom deselect",          0x00, 0x00, mode,   0, discretional},\
}

/*
 *      Status bits returned by QIC_REPORT_DRIVE_STATUS
 */

#define QIC_STATUS_READY	0x01	/* Drive is ready or idle. */
#define QIC_STATUS_ERROR	0x02	/* Error detected, must read
					   error code to clear this */
#define QIC_STATUS_CARTRIDGE_PRESENT 0x04	/* Tape is present */
#define QIC_STATUS_WRITE_PROTECT 0x08	/* Tape is write protected */
#define QIC_STATUS_NEW_CARTRIDGE 0x10	/* New cartridge inserted, must
					   read error status to clear. */
#define QIC_STATUS_REFERENCED	0x20	/* Cartridge appears to have been
					   formatted. */
#define QIC_STATUS_AT_BOT	0x40	/* Cartridge is at physical
					   beginning of tape. */
#define QIC_STATUS_AT_EOT	0x80	/* Cartridge is at physical end
					   of tape. */
/*
 *      Status bits returned by QIC_REPORT_DRIVE_CONFIGURATION
 */

#define QIC_CONFIG_RATE_MASK	0x18
#define QIC_CONFIG_RATE_SHIFT	3
#define QIC_CONFIG_RATE_250	0
#define QIC_CONFIG_RATE_500	2
#define QIC_CONFIG_RATE_1000	3
#define QIC_CONFIG_RATE_2000	1
#define QIC_CONFIG_RATE_4000    0       /* since QIC-117 Rev. J */

#define QIC_CONFIG_LONG		0x40	/* Extra Length Tape Detected */
#define QIC_CONFIG_80		0x80	/* QIC-80 detected. */

/*
 *      Status bits returned by QIC_REPORT_TAPE_STATUS
 */

#define QIC_TAPE_STD_MASK       0x0f
#define QIC_TAPE_QIC40  	0x01
#define QIC_TAPE_QIC80  	0x02
#define QIC_TAPE_QIC3020  	0x03
#define QIC_TAPE_QIC3010  	0x04

#define QIC_TAPE_LEN_MASK	0x70
#define QIC_TAPE_205FT		0x10
#define QIC_TAPE_307FT		0x20
#define QIC_TAPE_VARIABLE	0x30
#define QIC_TAPE_1100FT		0x40
#define QIC_TAPE_FLEX		0x60

#define QIC_TAPE_WIDE		0x80

/* Define a value (in feet) slightly higher than 
 * the possible maximum tape length.
 */
#define QIC_TOP_TAPE_LEN	1500

/*
 *      Errors: List of error codes, and their severity.
 */

typedef struct {
	char *message;		/* Text describing the error. */
	unsigned int fatal:1;	/* Non-zero if the error is fatal. */
} ftape_error;

#define QIC117_ERRORS {\
  /* 0*/ { "No error", 0, },\
  /* 1*/ { "Command Received while Drive Not Ready", 0, },\
  /* 2*/ { "Cartridge Not Present or Removed", 1, },\
  /* 3*/ { "Motor Speed Error (not within 1%)", 1, },\
  /* 4*/ { "Motor Speed Fault (jammed, or gross speed error", 1, },\
  /* 5*/ { "Cartridge Write Protected", 1, },\
  /* 6*/ { "Undefined or Reserved Command Code", 1, },\
  /* 7*/ { "Illegal Track Address Specified for Seek", 1, },\
  /* 8*/ { "Illegal Command in Report Subcontext", 0, },\
  /* 9*/ { "Illegal Entry into a Diagnostic Mode", 1, },\
  /*10*/ { "Broken Tape Detected (based on hole sensor)", 1, },\
  /*11*/ { "Warning--Read Gain Setting Error", 1, },\
  /*12*/ { "Command Received While Error Status Pending (obs)", 1, },\
  /*13*/ { "Command Received While New Cartridge Pending", 1, },\
  /*14*/ { "Command Illegal or Undefined in Primary Mode", 1, },\
  /*15*/ { "Command Illegal or Undefined in Format Mode", 1, },\
  /*16*/ { "Command Illegal or Undefined in Verify Mode", 1, },\
  /*17*/ { "Logical Forward Not at Logical BOT or no Format Segments in Format Mode", 1, },\
  /*18*/ { "Logical EOT Before All Segments generated", 1, },\
  /*19*/ { "Command Illegal When Cartridge Not Referenced", 1, },\
  /*20*/ { "Self-Diagnostic Failed (cannot be cleared)", 1, },\
  /*21*/ { "Warning EEPROM Not Initialized, Defaults Set", 1, },\
  /*22*/ { "EEPROM Corrupted or Hardware Failure", 1, },\
  /*23*/ { "Motion Time-out Error", 1, },\
  /*24*/ { "Data Segment Too Long -- Logical Forward or Pause", 1, },\
  /*25*/ { "Transmit Overrun (obs)", 1, },\
  /*26*/ { "Power On Reset Occurred", 0, },\
  /*27*/ { "Software Reset Occurred", 0, },\
  /*28*/ { "Diagnostic Mode 1 Error", 1, },\
  /*29*/ { "Diagnostic Mode 2 Error", 1, },\
  /*30*/ { "Command Received During Non-Interruptible Process", 1, },\
  /*31*/ { "Rate or Format Selection Error", 1, },\
  /*32*/ { "Illegal Command While in High Speed Mode", 1, },\
  /*33*/ { "Illegal Seek Segment Value", 1, },\
  /*34*/ { "Invalid Media", 1, },\
  /*35*/ { "Head Positioning Failure", 1, },\
  /*36*/ { "Write Reference Burst Failure", 1, },\
  /*37*/ { "Prom Code Missing", 1, },\
  /*38*/ { "Invalid Format", 1, },\
  /*39*/ { "EOT/BOT System Failure", 1, },\
  /*40*/ { "Prom A Checksum Error", 1, },\
  /*41*/ { "Drive Wakeup Reset Occurred", 1, },\
  /*42*/ { "Prom B Checksum Error", 1, },\
  /*43*/ { "Illegal Entry into Format Mode", 1, },\
}

#endif				/* _QIC117_H */
