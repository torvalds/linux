/* $FreeBSD$ */
/*-
 * Copyright (c) 2010 Hans Petter Selasky. All rights reserved.
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
 */

#ifndef _USB_MSC_TEST_H_
#define	_USB_MSC_TEST_H_

enum {
	USB_MSC_IO_MODE_READ_ONLY,
	USB_MSC_IO_MODE_WRITE_ONCE_READ_ONLY,
	USB_MSC_IO_MODE_WRITE_ONLY,
	USB_MSC_IO_MODE_READ_WRITE,
	USB_MSC_IO_MODE_MAX,
};

enum {
	USB_MSC_IO_PATTERN_FIXED,
	USB_MSC_IO_PATTERN_RANDOM,
	USB_MSC_IO_PATTERN_PRESERVE,
	USB_MSC_IO_PATTERN_MAX,
};

enum {
	USB_MSC_IO_SIZE_RANDOM,
	USB_MSC_IO_SIZE_INCREASING,
	USB_MSC_IO_SIZE_FIXED_1BLK,
	USB_MSC_IO_SIZE_FIXED_2BLK,
	USB_MSC_IO_SIZE_FIXED_4BLK,
	USB_MSC_IO_SIZE_FIXED_8BLK,
	USB_MSC_IO_SIZE_FIXED_16BLK,
	USB_MSC_IO_SIZE_FIXED_32BLK,
	USB_MSC_IO_SIZE_FIXED_64BLK,
	USB_MSC_IO_SIZE_FIXED_128BLK,
	USB_MSC_IO_SIZE_FIXED_256BLK,
	USB_MSC_IO_SIZE_FIXED_512BLK,
	USB_MSC_IO_SIZE_FIXED_1024BLK,
	USB_MSC_IO_SIZE_MAX,
};

enum {
	USB_MSC_IO_DELAY_NONE,
	USB_MSC_IO_DELAY_RANDOM_10MS,
	USB_MSC_IO_DELAY_RANDOM_100MS,
	USB_MSC_IO_DELAY_FIXED_10MS,
	USB_MSC_IO_DELAY_FIXED_100MS,
	USB_MSC_IO_DELAY_MAX,
};

enum {
	USB_MSC_IO_OFF_START_OF_DISK,
	USB_MSC_IO_OFF_RANDOM,
	USB_MSC_IO_OFF_MAX,
};

enum {
	USB_MSC_IO_AREA_COMPLETE,
	USB_MSC_IO_AREA_1MB,
	USB_MSC_IO_AREA_16MB,
	USB_MSC_IO_AREA_256MB,
	USB_MSC_IO_AREA_MAX,
};

enum {
	USB_MSC_IO_LUN_0,
	USB_MSC_IO_LUN_1,
	USB_MSC_IO_LUN_2,
	USB_MSC_IO_LUN_3,
	USB_MSC_IO_LUN_MAX,
};

struct usb_msc_params {

	uint32_t duration;
	uint32_t max_errors;

	/* See "USB_MSC_XXX" enums */

	uint8_t	io_mode;
	uint8_t	io_size;
	uint8_t	io_delay;
	uint8_t	io_offset;
	uint8_t	io_area;
	uint8_t	io_pattern;
	uint8_t	io_lun;

	/* booleans */
	uint8_t	try_invalid_scsi_command;
	uint8_t	try_invalid_wrapper_block;
	uint8_t	try_invalid_max_packet_size;
	uint8_t try_shorter_wrapper_block;
	uint8_t	try_last_lba;
	uint8_t	try_abort_data_write;
	uint8_t try_sense_on_error;
	uint8_t try_all_lun;

	uint8_t	done;
};

#endif					/* _USB_MSC_TEST_H_ */
