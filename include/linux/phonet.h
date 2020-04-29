/* SPDX-License-Identifier: GPL-2.0-only */
/**
 * file phonet.h
 *
 * Phonet sockets kernel interface
 *
 * Copyright (C) 2008 Nokia Corporation. All rights reserved.
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
