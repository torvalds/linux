/*-
 * Copyright (c) 2015 M. Warner Losh <imp@freebsd.org>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice unmodified, this list of conditions, and the following
 *    disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * $FreeBSD$
 */

#ifndef DEV_OW_OWN_H
#define DEV_OW_OWN_H 1

#include "own_if.h"


#define	READ_ROM	0x33
#define	MATCH_ROM	0x55
#define	SKIP_ROM	0xcc
#define	ALARM_SEARCH	0xec
#define	SEARCH_ROM	0xf0

static inline int
own_send_command(device_t pdev, struct ow_cmd *cmd)
{
	device_t ndev = device_get_parent(pdev);

	return OWN_SEND_COMMAND(ndev, pdev, cmd);
}

/*
 * How args for own_acquire_bus
 */
#define	OWN_WAIT	1
#define	OWN_DONTWAIT	2

static inline int
own_acquire_bus(device_t pdev, int how)
{
	device_t ndev = device_get_parent(pdev);

	return OWN_ACQUIRE_BUS(ndev, pdev, how);
}

static inline void
own_release_bus(device_t pdev)
{
	device_t ndev = device_get_parent(pdev);

	OWN_RELEASE_BUS(ndev, pdev);
}

static inline uint8_t
own_crc(device_t pdev, uint8_t *buffer, size_t len)
{
	device_t ndev = device_get_parent(pdev);

	return OWN_CRC(ndev, pdev, buffer, len);
}

static inline void
own_self_command(device_t pdev, struct ow_cmd *cmd, uint8_t xpt_cmd)
{
	uint8_t *mep;

	memset(cmd, 0, sizeof(*cmd));
	mep = ow_get_romid(pdev);
	cmd->rom_cmd[0] = MATCH_ROM;
	memcpy(&cmd->rom_cmd[1], mep, sizeof(romid_t));
	cmd->rom_len = 1 + sizeof(romid_t);
	cmd->xpt_cmd[0] = xpt_cmd;
	cmd->xpt_len = 1;
}

static inline int
own_command_wait(device_t pdev, struct ow_cmd *cmd)
{
	int rv;

	rv = own_acquire_bus(pdev, OWN_WAIT);
	if (rv != 0)
		return rv;
	rv = own_send_command(pdev, cmd);
	own_release_bus(pdev);
	return rv;
}

#endif /* DEV_OW_OWLL_H */
