/*-
 * bt3cfw.c
 *
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2001 Maksim Yevmenkin <m_evmenkin@yahoo.com>
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
 * ARE DISCLAIMED. IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $Id: bt3cfw.c,v 1.2 2003/05/21 22:40:29 max Exp $
 * $FreeBSD$
 */

#include <sys/types.h>
#include <errno.h>
#include <netgraph.h>
#include <netgraph/bluetooth/include/ng_bt3c.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>
#include <unistd.h>

#define BT3CFW_IDENT			"bt3cfw"
#define BT3CFW_MAX_FIRMWARE_SIZE	0xffff

/* Convert hex ASCII to int4 */
static int 
hexa2int4(const char *a)
{
	if ('0' <= *a && *a <= '9')
		return (*a - '0');

	if ('A' <= *a && *a <= 'F')
		return (*a - 'A' + 0xa);

	if ('a' <= *a && *a <= 'f')
		return (*a - 'a' + 0xa);

	syslog(LOG_ERR, "Invalid hex character: '%c' (%#x)", *a, *a);
	exit(255);
}

/* Convert hex ASCII to int8 */
static int 
hexa2int8(const char *a)
{
	return ((hexa2int4(a) << 4) | hexa2int4(a + 1));
} 

/* Convert hex ASCII to int16 */
static int 
hexa2int16(const char *a)
{
	return ((hexa2int8(a) << 8) | hexa2int8(a + 2));
} 

/* Convert hex ASCII to int32 */
static int 
hexa2int32(const char *a)
{
	return ((hexa2int16(a) << 16) | hexa2int16(a + 4));
}

/* Display usage() and exit */
static void
usage(void)
{
	syslog(LOG_ERR, "Usage: %s -f FirmwareFile -n NodeName", BT3CFW_IDENT);
	exit(255);
}
 
/* Main */
int
main(int argc, char *argv[])
{
	FILE	*firmware_file = NULL;
	char	 buffer[80], path[NG_PATHSIZ],
		*firmware_filename = NULL;
	uint8_t	*firmware = NULL;
	int	 firmware_size, opt, cs, ds;

	memset(path, 0, sizeof(path));
	openlog(BT3CFW_IDENT, LOG_NDELAY|LOG_PID|LOG_PERROR, LOG_USER);
 
	while ((opt = getopt(argc, argv, "f:hn:")) != -1) {
		switch (opt) {
		case 'f':
			firmware_filename = optarg;
			break;
  
		case 'n':
			snprintf(path, sizeof(path), "%s:", optarg);
			break;

		case 'h':
		default:
			usage();
			/* NOT REACHED */
		}
	}

	if (firmware_filename == NULL || path[0] == 0)
		usage();
		/* NOT REACHED */

	firmware = (uint8_t *) calloc(BT3CFW_MAX_FIRMWARE_SIZE,
					sizeof(uint8_t));
	if (firmware == NULL) {
		syslog(LOG_ERR, "Could not allocate firmware buffer");
		exit(255);
	}

	if ((firmware_file = fopen(firmware_filename, "r")) == NULL) {
		syslog(LOG_ERR, "Could not open BT3C firmware file %s. %s (%d)",
				firmware_filename, strerror(errno), errno);
		exit(255);
	}

	firmware_size = 0;

	while (fgets(buffer, sizeof(buffer), firmware_file)) {
		int     i, size, address, cs, fcs;

		size = hexa2int8(buffer + 2);
		address = hexa2int32(buffer + 4);
		fcs = hexa2int8(buffer + 2 + size * 2);

		if (buffer[1] == '3') {
			ng_bt3c_firmware_block_ep	*block = NULL;
			uint16_t			*data = NULL;

			block = (ng_bt3c_firmware_block_ep *)
						(firmware + firmware_size);

			firmware_size += sizeof(*block);
			if (firmware_size >= BT3CFW_MAX_FIRMWARE_SIZE) {
				syslog(LOG_ERR, "Could not add new firmware " \
						"block. Firmware file %s is " \
						"too big, firmware_size=%d", 
						firmware_filename,
						firmware_size);
				exit(255);
			}

			block->block_address = address;
			block->block_size = (size - 4) / 2;
			block->block_alignment = (block->block_size * 2) % 3;
			if (block->block_alignment != 0)
				block->block_alignment = 3 - block->block_alignment;

			firmware_size += (block->block_size * 2);
			firmware_size += block->block_alignment;
			if (firmware_size >= BT3CFW_MAX_FIRMWARE_SIZE) {
				syslog(LOG_ERR, "Could not add new firmware " \
						"data. Firmware file %s is " \
						"too big, firmware_size=%d", 
						firmware_filename,
						firmware_size);
				exit(255);
			}

			/* First part of the cheksum: size and address */
			cs = 0;
			for (i = 0; i < 5; i++)
				cs += hexa2int8(buffer + 2 + i * 2);

			/* Data + second part of the cheksum: data */
			data = (uint16_t *)(block + 1);
			for (i = 0; i < block->block_size; i++) {
				data[i] = hexa2int16(buffer + (i * 4) + 12);
				cs += (((data[i] & 0xff00) >> 8) & 0xff);
				cs += (data[i] & 0x00ff);
			}
		} else
			for (cs = 0, i = 0; i < size; i++)
				cs += hexa2int8(buffer + 2 + i * 2);

		if (((cs + fcs) & 0xff) != 0xff) {
			syslog(LOG_ERR, "Invalid firmware file %s. Checksum " \
					"error, cs=%#x, fcs=%#x, checksum=%#x",
					firmware_filename, (cs & 0xff), fcs,
					((cs + fcs) & 0xff));
			exit(255);
		}
	}

	/* Send firmware to the card */
	if (NgMkSockNode(NULL, &cs, &ds) < 0) {
		syslog(LOG_ERR, "Could not create Netgraph socket. %s (%d)",
				strerror(errno), errno);
		exit(255);
	}

	if (NgSendMsg(cs, path, NGM_BT3C_COOKIE,
			NGM_BT3C_NODE_DOWNLOAD_FIRMWARE,
			(void const *) firmware, firmware_size) < 0) {
		syslog(LOG_ERR, "Could not send Netgraph message. %s (%d)",
				strerror(errno), errno);
		exit(255);
	}

	free(firmware);
	firmware = NULL;
	fclose(firmware_file);

	return (0);
}

