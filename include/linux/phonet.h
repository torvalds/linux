/**
 * file phonet.h
 *
 * Phonet sockets kernel interface
 *
 * Copyright (C) 2008 Nokia Corporation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA
 * 02110-1301 USA
 */
#ifndef LINUX_PHONET_H
#define LINUX_PHONET_H

#include <uapi/linux/phonet.h>

#define SIOCPNGAUTOCONF		(SIOCDEVPRIVATE + 0)

struct if_phonet_autoconf {
	uint8_t device;
};

struct if_phonet_req {
	char ifr_phonet_name[16];
	union {
		struct if_phonet_autoconf ifru_phonet_autoconf;
	} ifr_ifru;
};
#define ifr_phonet_autoconf ifr_ifru.ifru_phonet_autoconf
#endif
