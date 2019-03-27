/*-
 * Copyright (c) 2009-2010 Weongyo Jeong <weongyo@freebsd.org>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer,
 *    without modification.
 * 2. Redistributions in binary form must reproduce at minimum a disclaimer
 *    similar to the "NO WARRANTY" disclaimer below ("Disclaimer") and any
 *    redistribution must be conditioned upon including a substantially
 *    similar Disclaimer requirement for further binary redistribution.
 *
 * NO WARRANTY
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF NONINFRINGEMENT, MERCHANTIBILITY
 * AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL
 * THE COPYRIGHT HOLDERS OR CONTRIBUTORS BE LIABLE FOR SPECIAL, EXEMPLARY,
 * OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER
 * IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
 * THE POSSIBILITY OF SUCH DAMAGES.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

/*
 * The Broadcom Wireless LAN controller driver.
 */

#include "opt_bwn.h"
#include "opt_wlan.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/malloc.h>
#include <sys/module.h>
#include <sys/endian.h>
#include <sys/errno.h>
#include <sys/firmware.h>
#include <sys/lock.h>
#include <sys/mutex.h>
#include <machine/bus.h>
#include <machine/resource.h>
#include <sys/bus.h>
#include <sys/rman.h>
#include <sys/socket.h>
#include <sys/sockio.h>

#include <net/ethernet.h>
#include <net/if.h>
#include <net/if_var.h>
#include <net/if_arp.h>
#include <net/if_dl.h>
#include <net/if_llc.h>
#include <net/if_media.h>
#include <net/if_types.h>

#include <dev/pci/pcivar.h>
#include <dev/pci/pcireg.h>

#include <net80211/ieee80211_var.h>
#include <net80211/ieee80211_radiotap.h>
#include <net80211/ieee80211_regdomain.h>
#include <net80211/ieee80211_phy.h>
#include <net80211/ieee80211_ratectl.h>

#include <dev/bwn/if_bwnreg.h>
#include <dev/bwn/if_bwnvar.h>

#include <dev/bwn/if_bwn_debug.h>
#include <dev/bwn/if_bwn_misc.h>
#include <dev/bwn/if_bwn_util.h>

unsigned int
bwn_sqrt(struct bwn_mac *mac, unsigned int x)
{
	/* Table holding (10 * sqrt(x)) for x between 1 and 256. */
	static uint8_t sqrt_table[256] = {
		10, 14, 17, 20, 22, 24, 26, 28,
		30, 31, 33, 34, 36, 37, 38, 40,
		41, 42, 43, 44, 45, 46, 47, 48,
		50, 50, 51, 52, 53, 54, 55, 56,
		57, 58, 59, 60, 60, 61, 62, 63,
		64, 64, 65, 66, 67, 67, 68, 69,
		70, 70, 71, 72, 72, 73, 74, 74,
		75, 76, 76, 77, 78, 78, 79, 80,
		80, 81, 81, 82, 83, 83, 84, 84,
		85, 86, 86, 87, 87, 88, 88, 89,
		90, 90, 91, 91, 92, 92, 93, 93,
		94, 94, 95, 95, 96, 96, 97, 97,
		98, 98, 99, 100, 100, 100, 101, 101,
		102, 102, 103, 103, 104, 104, 105, 105,
		106, 106, 107, 107, 108, 108, 109, 109,
		110, 110, 110, 111, 111, 112, 112, 113,
		113, 114, 114, 114, 115, 115, 116, 116,
		117, 117, 117, 118, 118, 119, 119, 120,
		120, 120, 121, 121, 122, 122, 122, 123,
		123, 124, 124, 124, 125, 125, 126, 126,
		126, 127, 127, 128, 128, 128, 129, 129,
		130, 130, 130, 131, 131, 131, 132, 132,
		133, 133, 133, 134, 134, 134, 135, 135,
		136, 136, 136, 137, 137, 137, 138, 138,
		138, 139, 139, 140, 140, 140, 141, 141,
		141, 142, 142, 142, 143, 143, 143, 144,
		144, 144, 145, 145, 145, 146, 146, 146,
		147, 147, 147, 148, 148, 148, 149, 149,
		150, 150, 150, 150, 151, 151, 151, 152,
		152, 152, 153, 153, 153, 154, 154, 154,
		155, 155, 155, 156, 156, 156, 157, 157,
		157, 158, 158, 158, 159, 159, 159, 160
	};

	if (x == 0)
		return (0);
	if (x >= 256) {
		unsigned int tmp;

		for (tmp = 0; x >= (2 * tmp) + 1; x -= (2 * tmp++) + 1)
			/* do nothing */ ;
		return (tmp);
	}
	return (sqrt_table[x - 1] / 10);
}
