/*-
 * SPDX-License-Identifier: BSD-4-Clause
 *
 * Copyright (c) 2001 Wind River Systems
 * Copyright (c) 1997, 1998, 1999, 2001
 *	Bill Paul <wpaul@windriver.com>.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *	This product includes software developed by Bill Paul.
 * 4. Neither the name of the author nor the names of any co-contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY Bill Paul AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL Bill Paul OR THE VOICES IN HIS HEAD
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
 * THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

/*
 * Broadcom BCM57xx(x)/BCM590x NetXtreme and NetLink family Ethernet driver
 *
 * The Broadcom BCM5700 is based on technology originally developed by
 * Alteon Networks as part of the Tigon I and Tigon II Gigabit Ethernet
 * MAC chips. The BCM5700, sometimes referred to as the Tigon III, has
 * two on-board MIPS R4000 CPUs and can have as much as 16MB of external
 * SSRAM. The BCM5700 supports TCP, UDP and IP checksum offload, jumbo
 * frames, highly configurable RX filtering, and 16 RX and TX queues
 * (which, along with RX filter rules, can be used for QOS applications).
 * Other features, such as TCP segmentation, may be available as part
 * of value-added firmware updates. Unlike the Tigon I and Tigon II,
 * firmware images can be stored in hardware and need not be compiled
 * into the driver.
 *
 * The BCM5700 supports the PCI v2.2 and PCI-X v1.0 standards, and will
 * function in a 32-bit/64-bit 33/66Mhz bus, or a 64-bit/133Mhz bus.
 *
 * The BCM5701 is a single-chip solution incorporating both the BCM5700
 * MAC and a BCM5401 10/100/1000 PHY. Unlike the BCM5700, the BCM5701
 * does not support external SSRAM.
 *
 * Broadcom also produces a variation of the BCM5700 under the "Altima"
 * brand name, which is functionally similar but lacks PCI-X support.
 *
 * Without external SSRAM, you can only have at most 4 TX rings,
 * and the use of the mini RX ring is disabled. This seems to imply
 * that these features are simply not available on the BCM5701. As a
 * result, this driver does not implement any support for the mini RX
 * ring.
 */

#ifdef HAVE_KERNEL_OPTION_HEADERS
#include "opt_device_polling.h"
#endif

#include <sys/param.h>
#include <sys/endian.h>
#include <sys/systm.h>
#include <sys/sockio.h>
#include <sys/mbuf.h>
#include <sys/malloc.h>
#include <sys/kernel.h>
#include <sys/module.h>
#include <sys/socket.h>
#include <sys/sysctl.h>
#include <sys/taskqueue.h>

#include <net/if.h>
#include <net/if_var.h>
#include <net/if_arp.h>
#include <net/ethernet.h>
#include <net/if_dl.h>
#include <net/if_media.h>

#include <net/bpf.h>

#include <net/if_types.h>
#include <net/if_vlan_var.h>

#include <netinet/in_systm.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <netinet/tcp.h>
#include <netinet/netdump/netdump.h>

#include <machine/bus.h>
#include <machine/resource.h>
#include <sys/bus.h>
#include <sys/rman.h>

#include <dev/mii/mii.h>
#include <dev/mii/miivar.h>
#include "miidevs.h"
#include <dev/mii/brgphyreg.h>

#ifdef __sparc64__
#include <dev/ofw/ofw_bus.h>
#include <dev/ofw/openfirm.h>
#include <machine/ofw_machdep.h>
#include <machine/ver.h>
#endif

#include <dev/pci/pcireg.h>
#include <dev/pci/pcivar.h>

#include <dev/bge/if_bgereg.h>

#define	BGE_CSUM_FEATURES	(CSUM_IP | CSUM_TCP)
#define	ETHER_MIN_NOPAD		(ETHER_MIN_LEN - ETHER_CRC_LEN) /* i.e., 60 */

MODULE_DEPEND(bge, pci, 1, 1, 1);
MODULE_DEPEND(bge, ether, 1, 1, 1);
MODULE_DEPEND(bge, miibus, 1, 1, 1);

/* "device miibus" required.  See GENERIC if you get errors here. */
#include "miibus_if.h"

/*
 * Various supported device vendors/types and their names. Note: the
 * spec seems to indicate that the hardware still has Alteon's vendor
 * ID burned into it, though it will always be overriden by the vendor
 * ID in the EEPROM. Just to be safe, we cover all possibilities.
 */
static const struct bge_type {
	uint16_t	bge_vid;
	uint16_t	bge_did;
} bge_devs[] = {
	{ ALTEON_VENDORID,	ALTEON_DEVICEID_BCM5700 },
	{ ALTEON_VENDORID,	ALTEON_DEVICEID_BCM5701 },

	{ ALTIMA_VENDORID,	ALTIMA_DEVICE_AC1000 },
	{ ALTIMA_VENDORID,	ALTIMA_DEVICE_AC1002 },
	{ ALTIMA_VENDORID,	ALTIMA_DEVICE_AC9100 },

	{ APPLE_VENDORID,	APPLE_DEVICE_BCM5701 },

	{ BCOM_VENDORID,	BCOM_DEVICEID_BCM5700 },
	{ BCOM_VENDORID,	BCOM_DEVICEID_BCM5701 },
	{ BCOM_VENDORID,	BCOM_DEVICEID_BCM5702 },
	{ BCOM_VENDORID,	BCOM_DEVICEID_BCM5702_ALT },
	{ BCOM_VENDORID,	BCOM_DEVICEID_BCM5702X },
	{ BCOM_VENDORID,	BCOM_DEVICEID_BCM5703 },
	{ BCOM_VENDORID,	BCOM_DEVICEID_BCM5703_ALT },
	{ BCOM_VENDORID,	BCOM_DEVICEID_BCM5703X },
	{ BCOM_VENDORID,	BCOM_DEVICEID_BCM5704C },
	{ BCOM_VENDORID,	BCOM_DEVICEID_BCM5704S },
	{ BCOM_VENDORID,	BCOM_DEVICEID_BCM5704S_ALT },
	{ BCOM_VENDORID,	BCOM_DEVICEID_BCM5705 },
	{ BCOM_VENDORID,	BCOM_DEVICEID_BCM5705F },
	{ BCOM_VENDORID,	BCOM_DEVICEID_BCM5705K },
	{ BCOM_VENDORID,	BCOM_DEVICEID_BCM5705M },
	{ BCOM_VENDORID,	BCOM_DEVICEID_BCM5705M_ALT },
	{ BCOM_VENDORID,	BCOM_DEVICEID_BCM5714C },
	{ BCOM_VENDORID,	BCOM_DEVICEID_BCM5714S },
	{ BCOM_VENDORID,	BCOM_DEVICEID_BCM5715 },
	{ BCOM_VENDORID,	BCOM_DEVICEID_BCM5715S },
	{ BCOM_VENDORID,	BCOM_DEVICEID_BCM5717 },
	{ BCOM_VENDORID,	BCOM_DEVICEID_BCM5717C },
	{ BCOM_VENDORID,	BCOM_DEVICEID_BCM5718 },
	{ BCOM_VENDORID,	BCOM_DEVICEID_BCM5719 },
	{ BCOM_VENDORID,	BCOM_DEVICEID_BCM5720 },
	{ BCOM_VENDORID,	BCOM_DEVICEID_BCM5721 },
	{ BCOM_VENDORID,	BCOM_DEVICEID_BCM5722 },
	{ BCOM_VENDORID,	BCOM_DEVICEID_BCM5723 },
	{ BCOM_VENDORID,	BCOM_DEVICEID_BCM5725 },
	{ BCOM_VENDORID,	BCOM_DEVICEID_BCM5727 },
	{ BCOM_VENDORID,	BCOM_DEVICEID_BCM5750 },
	{ BCOM_VENDORID,	BCOM_DEVICEID_BCM5750M },
	{ BCOM_VENDORID,	BCOM_DEVICEID_BCM5751 },
	{ BCOM_VENDORID,	BCOM_DEVICEID_BCM5751F },
	{ BCOM_VENDORID,	BCOM_DEVICEID_BCM5751M },
	{ BCOM_VENDORID,	BCOM_DEVICEID_BCM5752 },
	{ BCOM_VENDORID,	BCOM_DEVICEID_BCM5752M },
	{ BCOM_VENDORID,	BCOM_DEVICEID_BCM5753 },
	{ BCOM_VENDORID,	BCOM_DEVICEID_BCM5753F },
	{ BCOM_VENDORID,	BCOM_DEVICEID_BCM5753M },
	{ BCOM_VENDORID,	BCOM_DEVICEID_BCM5754 },
	{ BCOM_VENDORID,	BCOM_DEVICEID_BCM5754M },
	{ BCOM_VENDORID,	BCOM_DEVICEID_BCM5755 },
	{ BCOM_VENDORID,	BCOM_DEVICEID_BCM5755M },
	{ BCOM_VENDORID,	BCOM_DEVICEID_BCM5756 },
	{ BCOM_VENDORID,	BCOM_DEVICEID_BCM5761 },
	{ BCOM_VENDORID,	BCOM_DEVICEID_BCM5761E },
	{ BCOM_VENDORID,	BCOM_DEVICEID_BCM5761S },
	{ BCOM_VENDORID,	BCOM_DEVICEID_BCM5761SE },
	{ BCOM_VENDORID,	BCOM_DEVICEID_BCM5762 },
	{ BCOM_VENDORID,	BCOM_DEVICEID_BCM5764 },
	{ BCOM_VENDORID,	BCOM_DEVICEID_BCM5780 },
	{ BCOM_VENDORID,	BCOM_DEVICEID_BCM5780S },
	{ BCOM_VENDORID,	BCOM_DEVICEID_BCM5781 },
	{ BCOM_VENDORID,	BCOM_DEVICEID_BCM5782 },
	{ BCOM_VENDORID,	BCOM_DEVICEID_BCM5784 },
	{ BCOM_VENDORID,	BCOM_DEVICEID_BCM5785F },
	{ BCOM_VENDORID,	BCOM_DEVICEID_BCM5785G },
	{ BCOM_VENDORID,	BCOM_DEVICEID_BCM5786 },
	{ BCOM_VENDORID,	BCOM_DEVICEID_BCM5787 },
	{ BCOM_VENDORID,	BCOM_DEVICEID_BCM5787F },
	{ BCOM_VENDORID,	BCOM_DEVICEID_BCM5787M },
	{ BCOM_VENDORID,	BCOM_DEVICEID_BCM5788 },
	{ BCOM_VENDORID,	BCOM_DEVICEID_BCM5789 },
	{ BCOM_VENDORID,	BCOM_DEVICEID_BCM5901 },
	{ BCOM_VENDORID,	BCOM_DEVICEID_BCM5901A2 },
	{ BCOM_VENDORID,	BCOM_DEVICEID_BCM5903M },
	{ BCOM_VENDORID,	BCOM_DEVICEID_BCM5906 },
	{ BCOM_VENDORID,	BCOM_DEVICEID_BCM5906M },
	{ BCOM_VENDORID,	BCOM_DEVICEID_BCM57760 },
	{ BCOM_VENDORID,	BCOM_DEVICEID_BCM57761 },
	{ BCOM_VENDORID,	BCOM_DEVICEID_BCM57762 },
	{ BCOM_VENDORID,	BCOM_DEVICEID_BCM57764 },
	{ BCOM_VENDORID,	BCOM_DEVICEID_BCM57765 },
	{ BCOM_VENDORID,	BCOM_DEVICEID_BCM57766 },
	{ BCOM_VENDORID,	BCOM_DEVICEID_BCM57767 },
	{ BCOM_VENDORID,	BCOM_DEVICEID_BCM57780 },
	{ BCOM_VENDORID,	BCOM_DEVICEID_BCM57781 },
	{ BCOM_VENDORID,	BCOM_DEVICEID_BCM57782 },
	{ BCOM_VENDORID,	BCOM_DEVICEID_BCM57785 },
	{ BCOM_VENDORID,	BCOM_DEVICEID_BCM57786 },
	{ BCOM_VENDORID,	BCOM_DEVICEID_BCM57787 },
	{ BCOM_VENDORID,	BCOM_DEVICEID_BCM57788 },
	{ BCOM_VENDORID,	BCOM_DEVICEID_BCM57790 },
	{ BCOM_VENDORID,	BCOM_DEVICEID_BCM57791 },
	{ BCOM_VENDORID,	BCOM_DEVICEID_BCM57795 },

	{ SK_VENDORID,		SK_DEVICEID_ALTIMA },

	{ TC_VENDORID,		TC_DEVICEID_3C996 },

	{ FJTSU_VENDORID,	FJTSU_DEVICEID_PW008GE4 },
	{ FJTSU_VENDORID,	FJTSU_DEVICEID_PW008GE5 },
	{ FJTSU_VENDORID,	FJTSU_DEVICEID_PP250450 },

	{ 0, 0 }
};

static const struct bge_vendor {
	uint16_t	v_id;
	const char	*v_name;
} bge_vendors[] = {
	{ ALTEON_VENDORID,	"Alteon" },
	{ ALTIMA_VENDORID,	"Altima" },
	{ APPLE_VENDORID,	"Apple" },
	{ BCOM_VENDORID,	"Broadcom" },
	{ SK_VENDORID,		"SysKonnect" },
	{ TC_VENDORID,		"3Com" },
	{ FJTSU_VENDORID,	"Fujitsu" },

	{ 0, NULL }
};

static const struct bge_revision {
	uint32_t	br_chipid;
	const char	*br_name;
} bge_revisions[] = {
	{ BGE_CHIPID_BCM5700_A0,	"BCM5700 A0" },
	{ BGE_CHIPID_BCM5700_A1,	"BCM5700 A1" },
	{ BGE_CHIPID_BCM5700_B0,	"BCM5700 B0" },
	{ BGE_CHIPID_BCM5700_B1,	"BCM5700 B1" },
	{ BGE_CHIPID_BCM5700_B2,	"BCM5700 B2" },
	{ BGE_CHIPID_BCM5700_B3,	"BCM5700 B3" },
	{ BGE_CHIPID_BCM5700_ALTIMA,	"BCM5700 Altima" },
	{ BGE_CHIPID_BCM5700_C0,	"BCM5700 C0" },
	{ BGE_CHIPID_BCM5701_A0,	"BCM5701 A0" },
	{ BGE_CHIPID_BCM5701_B0,	"BCM5701 B0" },
	{ BGE_CHIPID_BCM5701_B2,	"BCM5701 B2" },
	{ BGE_CHIPID_BCM5701_B5,	"BCM5701 B5" },
	{ BGE_CHIPID_BCM5703_A0,	"BCM5703 A0" },
	{ BGE_CHIPID_BCM5703_A1,	"BCM5703 A1" },
	{ BGE_CHIPID_BCM5703_A2,	"BCM5703 A2" },
	{ BGE_CHIPID_BCM5703_A3,	"BCM5703 A3" },
	{ BGE_CHIPID_BCM5703_B0,	"BCM5703 B0" },
	{ BGE_CHIPID_BCM5704_A0,	"BCM5704 A0" },
	{ BGE_CHIPID_BCM5704_A1,	"BCM5704 A1" },
	{ BGE_CHIPID_BCM5704_A2,	"BCM5704 A2" },
	{ BGE_CHIPID_BCM5704_A3,	"BCM5704 A3" },
	{ BGE_CHIPID_BCM5704_B0,	"BCM5704 B0" },
	{ BGE_CHIPID_BCM5705_A0,	"BCM5705 A0" },
	{ BGE_CHIPID_BCM5705_A1,	"BCM5705 A1" },
	{ BGE_CHIPID_BCM5705_A2,	"BCM5705 A2" },
	{ BGE_CHIPID_BCM5705_A3,	"BCM5705 A3" },
	{ BGE_CHIPID_BCM5750_A0,	"BCM5750 A0" },
	{ BGE_CHIPID_BCM5750_A1,	"BCM5750 A1" },
	{ BGE_CHIPID_BCM5750_A3,	"BCM5750 A3" },
	{ BGE_CHIPID_BCM5750_B0,	"BCM5750 B0" },
	{ BGE_CHIPID_BCM5750_B1,	"BCM5750 B1" },
	{ BGE_CHIPID_BCM5750_C0,	"BCM5750 C0" },
	{ BGE_CHIPID_BCM5750_C1,	"BCM5750 C1" },
	{ BGE_CHIPID_BCM5750_C2,	"BCM5750 C2" },
	{ BGE_CHIPID_BCM5714_A0,	"BCM5714 A0" },
	{ BGE_CHIPID_BCM5752_A0,	"BCM5752 A0" },
	{ BGE_CHIPID_BCM5752_A1,	"BCM5752 A1" },
	{ BGE_CHIPID_BCM5752_A2,	"BCM5752 A2" },
	{ BGE_CHIPID_BCM5714_B0,	"BCM5714 B0" },
	{ BGE_CHIPID_BCM5714_B3,	"BCM5714 B3" },
	{ BGE_CHIPID_BCM5715_A0,	"BCM5715 A0" },
	{ BGE_CHIPID_BCM5715_A1,	"BCM5715 A1" },
	{ BGE_CHIPID_BCM5715_A3,	"BCM5715 A3" },
	{ BGE_CHIPID_BCM5717_A0,	"BCM5717 A0" },
	{ BGE_CHIPID_BCM5717_B0,	"BCM5717 B0" },
	{ BGE_CHIPID_BCM5717_C0,	"BCM5717 C0" },
	{ BGE_CHIPID_BCM5719_A0,	"BCM5719 A0" },
	{ BGE_CHIPID_BCM5720_A0,	"BCM5720 A0" },
	{ BGE_CHIPID_BCM5755_A0,	"BCM5755 A0" },
	{ BGE_CHIPID_BCM5755_A1,	"BCM5755 A1" },
	{ BGE_CHIPID_BCM5755_A2,	"BCM5755 A2" },
	{ BGE_CHIPID_BCM5722_A0,	"BCM5722 A0" },
	{ BGE_CHIPID_BCM5761_A0,	"BCM5761 A0" },
	{ BGE_CHIPID_BCM5761_A1,	"BCM5761 A1" },
	{ BGE_CHIPID_BCM5762_A0,	"BCM5762 A0" },
	{ BGE_CHIPID_BCM5784_A0,	"BCM5784 A0" },
	{ BGE_CHIPID_BCM5784_A1,	"BCM5784 A1" },
	/* 5754 and 5787 share the same ASIC ID */
	{ BGE_CHIPID_BCM5787_A0,	"BCM5754/5787 A0" },
	{ BGE_CHIPID_BCM5787_A1,	"BCM5754/5787 A1" },
	{ BGE_CHIPID_BCM5787_A2,	"BCM5754/5787 A2" },
	{ BGE_CHIPID_BCM5906_A1,	"BCM5906 A1" },
	{ BGE_CHIPID_BCM5906_A2,	"BCM5906 A2" },
	{ BGE_CHIPID_BCM57765_A0,	"BCM57765 A0" },
	{ BGE_CHIPID_BCM57765_B0,	"BCM57765 B0" },
	{ BGE_CHIPID_BCM57780_A0,	"BCM57780 A0" },
	{ BGE_CHIPID_BCM57780_A1,	"BCM57780 A1" },

	{ 0, NULL }
};

/*
 * Some defaults for major revisions, so that newer steppings
 * that we don't know about have a shot at working.
 */
static const struct bge_revision bge_majorrevs[] = {
	{ BGE_ASICREV_BCM5700,		"unknown BCM5700" },
	{ BGE_ASICREV_BCM5701,		"unknown BCM5701" },
	{ BGE_ASICREV_BCM5703,		"unknown BCM5703" },
	{ BGE_ASICREV_BCM5704,		"unknown BCM5704" },
	{ BGE_ASICREV_BCM5705,		"unknown BCM5705" },
	{ BGE_ASICREV_BCM5750,		"unknown BCM5750" },
	{ BGE_ASICREV_BCM5714_A0,	"unknown BCM5714" },
	{ BGE_ASICREV_BCM5752,		"unknown BCM5752" },
	{ BGE_ASICREV_BCM5780,		"unknown BCM5780" },
	{ BGE_ASICREV_BCM5714,		"unknown BCM5714" },
	{ BGE_ASICREV_BCM5755,		"unknown BCM5755" },
	{ BGE_ASICREV_BCM5761,		"unknown BCM5761" },
	{ BGE_ASICREV_BCM5784,		"unknown BCM5784" },
	{ BGE_ASICREV_BCM5785,		"unknown BCM5785" },
	/* 5754 and 5787 share the same ASIC ID */
	{ BGE_ASICREV_BCM5787,		"unknown BCM5754/5787" },
	{ BGE_ASICREV_BCM5906,		"unknown BCM5906" },
	{ BGE_ASICREV_BCM57765,		"unknown BCM57765" },
	{ BGE_ASICREV_BCM57766,		"unknown BCM57766" },
	{ BGE_ASICREV_BCM57780,		"unknown BCM57780" },
	{ BGE_ASICREV_BCM5717,		"unknown BCM5717" },
	{ BGE_ASICREV_BCM5719,		"unknown BCM5719" },
	{ BGE_ASICREV_BCM5720,		"unknown BCM5720" },
	{ BGE_ASICREV_BCM5762,		"unknown BCM5762" },

	{ 0, NULL }
};

#define	BGE_IS_JUMBO_CAPABLE(sc)	((sc)->bge_flags & BGE_FLAG_JUMBO)
#define	BGE_IS_5700_FAMILY(sc)		((sc)->bge_flags & BGE_FLAG_5700_FAMILY)
#define	BGE_IS_5705_PLUS(sc)		((sc)->bge_flags & BGE_FLAG_5705_PLUS)
#define	BGE_IS_5714_FAMILY(sc)		((sc)->bge_flags & BGE_FLAG_5714_FAMILY)
#define	BGE_IS_575X_PLUS(sc)		((sc)->bge_flags & BGE_FLAG_575X_PLUS)
#define	BGE_IS_5755_PLUS(sc)		((sc)->bge_flags & BGE_FLAG_5755_PLUS)
#define	BGE_IS_5717_PLUS(sc)		((sc)->bge_flags & BGE_FLAG_5717_PLUS)
#define	BGE_IS_57765_PLUS(sc)		((sc)->bge_flags & BGE_FLAG_57765_PLUS)

static uint32_t bge_chipid(device_t);
static const struct bge_vendor * bge_lookup_vendor(uint16_t);
static const struct bge_revision * bge_lookup_rev(uint32_t);

typedef int	(*bge_eaddr_fcn_t)(struct bge_softc *, uint8_t[]);

static int bge_probe(device_t);
static int bge_attach(device_t);
static int bge_detach(device_t);
static int bge_suspend(device_t);
static int bge_resume(device_t);
static void bge_release_resources(struct bge_softc *);
static void bge_dma_map_addr(void *, bus_dma_segment_t *, int, int);
static int bge_dma_alloc(struct bge_softc *);
static void bge_dma_free(struct bge_softc *);
static int bge_dma_ring_alloc(struct bge_softc *, bus_size_t, bus_size_t,
    bus_dma_tag_t *, uint8_t **, bus_dmamap_t *, bus_addr_t *, const char *);

static void bge_devinfo(struct bge_softc *);
static int bge_mbox_reorder(struct bge_softc *);

static int bge_get_eaddr_fw(struct bge_softc *sc, uint8_t ether_addr[]);
static int bge_get_eaddr_mem(struct bge_softc *, uint8_t[]);
static int bge_get_eaddr_nvram(struct bge_softc *, uint8_t[]);
static int bge_get_eaddr_eeprom(struct bge_softc *, uint8_t[]);
static int bge_get_eaddr(struct bge_softc *, uint8_t[]);

static void bge_txeof(struct bge_softc *, uint16_t);
static void bge_rxcsum(struct bge_softc *, struct bge_rx_bd *, struct mbuf *);
static int bge_rxeof(struct bge_softc *, uint16_t, int);

static void bge_asf_driver_up (struct bge_softc *);
static void bge_tick(void *);
static void bge_stats_clear_regs(struct bge_softc *);
static void bge_stats_update(struct bge_softc *);
static void bge_stats_update_regs(struct bge_softc *);
static struct mbuf *bge_check_short_dma(struct mbuf *);
static struct mbuf *bge_setup_tso(struct bge_softc *, struct mbuf *,
    uint16_t *, uint16_t *);
static int bge_encap(struct bge_softc *, struct mbuf **, uint32_t *);

static void bge_intr(void *);
static int bge_msi_intr(void *);
static void bge_intr_task(void *, int);
static void bge_start(if_t);
static void bge_start_locked(if_t);
static void bge_start_tx(struct bge_softc *, uint32_t);
static int bge_ioctl(if_t, u_long, caddr_t);
static void bge_init_locked(struct bge_softc *);
static void bge_init(void *);
static void bge_stop_block(struct bge_softc *, bus_size_t, uint32_t);
static void bge_stop(struct bge_softc *);
static void bge_watchdog(struct bge_softc *);
static int bge_shutdown(device_t);
static int bge_ifmedia_upd_locked(if_t);
static int bge_ifmedia_upd(if_t);
static void bge_ifmedia_sts(if_t, struct ifmediareq *);
static uint64_t bge_get_counter(if_t, ift_counter);

static uint8_t bge_nvram_getbyte(struct bge_softc *, int, uint8_t *);
static int bge_read_nvram(struct bge_softc *, caddr_t, int, int);

static uint8_t bge_eeprom_getbyte(struct bge_softc *, int, uint8_t *);
static int bge_read_eeprom(struct bge_softc *, caddr_t, int, int);

static void bge_setpromisc(struct bge_softc *);
static void bge_setmulti(struct bge_softc *);
static void bge_setvlan(struct bge_softc *);

static __inline void bge_rxreuse_std(struct bge_softc *, int);
static __inline void bge_rxreuse_jumbo(struct bge_softc *, int);
static int bge_newbuf_std(struct bge_softc *, int);
static int bge_newbuf_jumbo(struct bge_softc *, int);
static int bge_init_rx_ring_std(struct bge_softc *);
static void bge_free_rx_ring_std(struct bge_softc *);
static int bge_init_rx_ring_jumbo(struct bge_softc *);
static void bge_free_rx_ring_jumbo(struct bge_softc *);
static void bge_free_tx_ring(struct bge_softc *);
static int bge_init_tx_ring(struct bge_softc *);

static int bge_chipinit(struct bge_softc *);
static int bge_blockinit(struct bge_softc *);
static uint32_t bge_dma_swap_options(struct bge_softc *);

static int bge_has_eaddr(struct bge_softc *);
static uint32_t bge_readmem_ind(struct bge_softc *, int);
static void bge_writemem_ind(struct bge_softc *, int, int);
static void bge_writembx(struct bge_softc *, int, int);
#ifdef notdef
static uint32_t bge_readreg_ind(struct bge_softc *, int);
#endif
static void bge_writemem_direct(struct bge_softc *, int, int);
static void bge_writereg_ind(struct bge_softc *, int, int);

static int bge_miibus_readreg(device_t, int, int);
static int bge_miibus_writereg(device_t, int, int, int);
static void bge_miibus_statchg(device_t);
#ifdef DEVICE_POLLING
static int bge_poll(if_t ifp, enum poll_cmd cmd, int count);
#endif

#define	BGE_RESET_SHUTDOWN	0
#define	BGE_RESET_START		1
#define	BGE_RESET_SUSPEND	2
static void bge_sig_post_reset(struct bge_softc *, int);
static void bge_sig_legacy(struct bge_softc *, int);
static void bge_sig_pre_reset(struct bge_softc *, int);
static void bge_stop_fw(struct bge_softc *);
static int bge_reset(struct bge_softc *);
static void bge_link_upd(struct bge_softc *);

static void bge_ape_lock_init(struct bge_softc *);
static void bge_ape_read_fw_ver(struct bge_softc *);
static int bge_ape_lock(struct bge_softc *, int);
static void bge_ape_unlock(struct bge_softc *, int);
static void bge_ape_send_event(struct bge_softc *, uint32_t);
static void bge_ape_driver_state_change(struct bge_softc *, int);

/*
 * The BGE_REGISTER_DEBUG option is only for low-level debugging.  It may
 * leak information to untrusted users.  It is also known to cause alignment
 * traps on certain architectures.
 */
#ifdef BGE_REGISTER_DEBUG
static int bge_sysctl_debug_info(SYSCTL_HANDLER_ARGS);
static int bge_sysctl_reg_read(SYSCTL_HANDLER_ARGS);
static int bge_sysctl_ape_read(SYSCTL_HANDLER_ARGS);
static int bge_sysctl_mem_read(SYSCTL_HANDLER_ARGS);
#endif
static void bge_add_sysctls(struct bge_softc *);
static void bge_add_sysctl_stats_regs(struct bge_softc *,
    struct sysctl_ctx_list *, struct sysctl_oid_list *);
static void bge_add_sysctl_stats(struct bge_softc *, struct sysctl_ctx_list *,
    struct sysctl_oid_list *);
static int bge_sysctl_stats(SYSCTL_HANDLER_ARGS);

NETDUMP_DEFINE(bge);

static device_method_t bge_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe,		bge_probe),
	DEVMETHOD(device_attach,	bge_attach),
	DEVMETHOD(device_detach,	bge_detach),
	DEVMETHOD(device_shutdown,	bge_shutdown),
	DEVMETHOD(device_suspend,	bge_suspend),
	DEVMETHOD(device_resume,	bge_resume),

	/* MII interface */
	DEVMETHOD(miibus_readreg,	bge_miibus_readreg),
	DEVMETHOD(miibus_writereg,	bge_miibus_writereg),
	DEVMETHOD(miibus_statchg,	bge_miibus_statchg),

	DEVMETHOD_END
};

static driver_t bge_driver = {
	"bge",
	bge_methods,
	sizeof(struct bge_softc)
};

static devclass_t bge_devclass;

DRIVER_MODULE(bge, pci, bge_driver, bge_devclass, 0, 0);
MODULE_PNP_INFO("U16:vendor;U16:device", pci, bge, bge_devs,
    nitems(bge_devs) - 1);
DRIVER_MODULE(miibus, bge, miibus_driver, miibus_devclass, 0, 0);

static int bge_allow_asf = 1;

static SYSCTL_NODE(_hw, OID_AUTO, bge, CTLFLAG_RD, 0, "BGE driver parameters");
SYSCTL_INT(_hw_bge, OID_AUTO, allow_asf, CTLFLAG_RDTUN, &bge_allow_asf, 0,
	"Allow ASF mode if available");

#define	SPARC64_BLADE_1500_MODEL	"SUNW,Sun-Blade-1500"
#define	SPARC64_BLADE_1500_PATH_BGE	"/pci@1f,700000/network@2"
#define	SPARC64_BLADE_2500_MODEL	"SUNW,Sun-Blade-2500"
#define	SPARC64_BLADE_2500_PATH_BGE	"/pci@1c,600000/network@3"
#define	SPARC64_OFW_SUBVENDOR		"subsystem-vendor-id"

static int
bge_has_eaddr(struct bge_softc *sc)
{
#ifdef __sparc64__
	char buf[sizeof(SPARC64_BLADE_1500_PATH_BGE)];
	device_t dev;
	uint32_t subvendor;

	dev = sc->bge_dev;

	/*
	 * The on-board BGEs found in sun4u machines aren't fitted with
	 * an EEPROM which means that we have to obtain the MAC address
	 * via OFW and that some tests will always fail.  We distinguish
	 * such BGEs by the subvendor ID, which also has to be obtained
	 * from OFW instead of the PCI configuration space as the latter
	 * indicates Broadcom as the subvendor of the netboot interface.
	 * For early Blade 1500 and 2500 we even have to check the OFW
	 * device path as the subvendor ID always defaults to Broadcom
	 * there.
	 */
	if (OF_getprop(ofw_bus_get_node(dev), SPARC64_OFW_SUBVENDOR,
	    &subvendor, sizeof(subvendor)) == sizeof(subvendor) &&
	    (subvendor == FJTSU_VENDORID || subvendor == SUN_VENDORID))
		return (0);
	memset(buf, 0, sizeof(buf));
	if (OF_package_to_path(ofw_bus_get_node(dev), buf, sizeof(buf)) > 0) {
		if (strcmp(sparc64_model, SPARC64_BLADE_1500_MODEL) == 0 &&
		    strcmp(buf, SPARC64_BLADE_1500_PATH_BGE) == 0)
			return (0);
		if (strcmp(sparc64_model, SPARC64_BLADE_2500_MODEL) == 0 &&
		    strcmp(buf, SPARC64_BLADE_2500_PATH_BGE) == 0)
			return (0);
	}
#endif
	return (1);
}

static uint32_t
bge_readmem_ind(struct bge_softc *sc, int off)
{
	device_t dev;
	uint32_t val;

	if (sc->bge_asicrev == BGE_ASICREV_BCM5906 &&
	    off >= BGE_STATS_BLOCK && off < BGE_SEND_RING_1_TO_4)
		return (0);

	dev = sc->bge_dev;

	pci_write_config(dev, BGE_PCI_MEMWIN_BASEADDR, off, 4);
	val = pci_read_config(dev, BGE_PCI_MEMWIN_DATA, 4);
	pci_write_config(dev, BGE_PCI_MEMWIN_BASEADDR, 0, 4);
	return (val);
}

static void
bge_writemem_ind(struct bge_softc *sc, int off, int val)
{
	device_t dev;

	if (sc->bge_asicrev == BGE_ASICREV_BCM5906 &&
	    off >= BGE_STATS_BLOCK && off < BGE_SEND_RING_1_TO_4)
		return;

	dev = sc->bge_dev;

	pci_write_config(dev, BGE_PCI_MEMWIN_BASEADDR, off, 4);
	pci_write_config(dev, BGE_PCI_MEMWIN_DATA, val, 4);
	pci_write_config(dev, BGE_PCI_MEMWIN_BASEADDR, 0, 4);
}

#ifdef notdef
static uint32_t
bge_readreg_ind(struct bge_softc *sc, int off)
{
	device_t dev;

	dev = sc->bge_dev;

	pci_write_config(dev, BGE_PCI_REG_BASEADDR, off, 4);
	return (pci_read_config(dev, BGE_PCI_REG_DATA, 4));
}
#endif

static void
bge_writereg_ind(struct bge_softc *sc, int off, int val)
{
	device_t dev;

	dev = sc->bge_dev;

	pci_write_config(dev, BGE_PCI_REG_BASEADDR, off, 4);
	pci_write_config(dev, BGE_PCI_REG_DATA, val, 4);
}

static void
bge_writemem_direct(struct bge_softc *sc, int off, int val)
{
	CSR_WRITE_4(sc, off, val);
}

static void
bge_writembx(struct bge_softc *sc, int off, int val)
{
	if (sc->bge_asicrev == BGE_ASICREV_BCM5906)
		off += BGE_LPMBX_IRQ0_HI - BGE_MBX_IRQ0_HI;

	CSR_WRITE_4(sc, off, val);
	if ((sc->bge_flags & BGE_FLAG_MBOX_REORDER) != 0)
		CSR_READ_4(sc, off);
}

/*
 * Clear all stale locks and select the lock for this driver instance.
 */
static void
bge_ape_lock_init(struct bge_softc *sc)
{
	uint32_t bit, regbase;
	int i;

	if (sc->bge_asicrev == BGE_ASICREV_BCM5761)
		regbase = BGE_APE_LOCK_GRANT;
	else
		regbase = BGE_APE_PER_LOCK_GRANT;

	/* Clear any stale locks. */
	for (i = BGE_APE_LOCK_PHY0; i <= BGE_APE_LOCK_GPIO; i++) {
		switch (i) {
		case BGE_APE_LOCK_PHY0:
		case BGE_APE_LOCK_PHY1:
		case BGE_APE_LOCK_PHY2:
		case BGE_APE_LOCK_PHY3:
			bit = BGE_APE_LOCK_GRANT_DRIVER0;
			break;
		default:
			if (sc->bge_func_addr == 0)
				bit = BGE_APE_LOCK_GRANT_DRIVER0;
			else
				bit = (1 << sc->bge_func_addr);
		}
		APE_WRITE_4(sc, regbase + 4 * i, bit);
	}

	/* Select the PHY lock based on the device's function number. */
	switch (sc->bge_func_addr) {
	case 0:
		sc->bge_phy_ape_lock = BGE_APE_LOCK_PHY0;
		break;
	case 1:
		sc->bge_phy_ape_lock = BGE_APE_LOCK_PHY1;
		break;
	case 2:
		sc->bge_phy_ape_lock = BGE_APE_LOCK_PHY2;
		break;
	case 3:
		sc->bge_phy_ape_lock = BGE_APE_LOCK_PHY3;
		break;
	default:
		device_printf(sc->bge_dev,
		    "PHY lock not supported on this function\n");
	}
}

/*
 * Check for APE firmware, set flags, and print version info.
 */
static void
bge_ape_read_fw_ver(struct bge_softc *sc)
{
	const char *fwtype;
	uint32_t apedata, features;

	/* Check for a valid APE signature in shared memory. */
	apedata = APE_READ_4(sc, BGE_APE_SEG_SIG);
	if (apedata != BGE_APE_SEG_SIG_MAGIC) {
		sc->bge_mfw_flags &= ~ BGE_MFW_ON_APE;
		return;
	}

	/* Check if APE firmware is running. */
	apedata = APE_READ_4(sc, BGE_APE_FW_STATUS);
	if ((apedata & BGE_APE_FW_STATUS_READY) == 0) {
		device_printf(sc->bge_dev, "APE signature found "
		    "but FW status not ready! 0x%08x\n", apedata);
		return;
	}

	sc->bge_mfw_flags |= BGE_MFW_ON_APE;

	/* Fetch the APE firwmare type and version. */
	apedata = APE_READ_4(sc, BGE_APE_FW_VERSION);
	features = APE_READ_4(sc, BGE_APE_FW_FEATURES);
	if ((features & BGE_APE_FW_FEATURE_NCSI) != 0) {
		sc->bge_mfw_flags |= BGE_MFW_TYPE_NCSI;
		fwtype = "NCSI";
	} else if ((features & BGE_APE_FW_FEATURE_DASH) != 0) {
		sc->bge_mfw_flags |= BGE_MFW_TYPE_DASH;
		fwtype = "DASH";
	} else
		fwtype = "UNKN";

	/* Print the APE firmware version. */
	device_printf(sc->bge_dev, "APE FW version: %s v%d.%d.%d.%d\n",
	    fwtype,
	    (apedata & BGE_APE_FW_VERSION_MAJMSK) >> BGE_APE_FW_VERSION_MAJSFT,
	    (apedata & BGE_APE_FW_VERSION_MINMSK) >> BGE_APE_FW_VERSION_MINSFT,
	    (apedata & BGE_APE_FW_VERSION_REVMSK) >> BGE_APE_FW_VERSION_REVSFT,
	    (apedata & BGE_APE_FW_VERSION_BLDMSK));
}

static int
bge_ape_lock(struct bge_softc *sc, int locknum)
{
	uint32_t bit, gnt, req, status;
	int i, off;

	if ((sc->bge_mfw_flags & BGE_MFW_ON_APE) == 0)
		return (0);

	/* Lock request/grant registers have different bases. */
	if (sc->bge_asicrev == BGE_ASICREV_BCM5761) {
		req = BGE_APE_LOCK_REQ;
		gnt = BGE_APE_LOCK_GRANT;
	} else {
		req = BGE_APE_PER_LOCK_REQ;
		gnt = BGE_APE_PER_LOCK_GRANT;
	}

	off = 4 * locknum;

	switch (locknum) {
	case BGE_APE_LOCK_GPIO:
		/* Lock required when using GPIO. */
		if (sc->bge_asicrev == BGE_ASICREV_BCM5761)
			return (0);
		if (sc->bge_func_addr == 0)
			bit = BGE_APE_LOCK_REQ_DRIVER0;
		else
			bit = (1 << sc->bge_func_addr);
		break;
	case BGE_APE_LOCK_GRC:
		/* Lock required to reset the device. */
		if (sc->bge_func_addr == 0)
			bit = BGE_APE_LOCK_REQ_DRIVER0;
		else
			bit = (1 << sc->bge_func_addr);
		break;
	case BGE_APE_LOCK_MEM:
		/* Lock required when accessing certain APE memory. */
		if (sc->bge_func_addr == 0)
			bit = BGE_APE_LOCK_REQ_DRIVER0;
		else
			bit = (1 << sc->bge_func_addr);
		break;
	case BGE_APE_LOCK_PHY0:
	case BGE_APE_LOCK_PHY1:
	case BGE_APE_LOCK_PHY2:
	case BGE_APE_LOCK_PHY3:
		/* Lock required when accessing PHYs. */
		bit = BGE_APE_LOCK_REQ_DRIVER0;
		break;
	default:
		return (EINVAL);
	}

	/* Request a lock. */
	APE_WRITE_4(sc, req + off, bit);

	/* Wait up to 1 second to acquire lock. */
	for (i = 0; i < 20000; i++) {
		status = APE_READ_4(sc, gnt + off);
		if (status == bit)
			break;
		DELAY(50);
	}

	/* Handle any errors. */
	if (status != bit) {
		device_printf(sc->bge_dev, "APE lock %d request failed! "
		    "request = 0x%04x[0x%04x], status = 0x%04x[0x%04x]\n",
		    locknum, req + off, bit & 0xFFFF, gnt + off,
		    status & 0xFFFF);
		/* Revoke the lock request. */
		APE_WRITE_4(sc, gnt + off, bit);
		return (EBUSY);
	}

	return (0);
}

static void
bge_ape_unlock(struct bge_softc *sc, int locknum)
{
	uint32_t bit, gnt;
	int off;

	if ((sc->bge_mfw_flags & BGE_MFW_ON_APE) == 0)
		return;

	if (sc->bge_asicrev == BGE_ASICREV_BCM5761)
		gnt = BGE_APE_LOCK_GRANT;
	else
		gnt = BGE_APE_PER_LOCK_GRANT;

	off = 4 * locknum;

	switch (locknum) {
	case BGE_APE_LOCK_GPIO:
		if (sc->bge_asicrev == BGE_ASICREV_BCM5761)
			return;
		if (sc->bge_func_addr == 0)
			bit = BGE_APE_LOCK_GRANT_DRIVER0;
		else
			bit = (1 << sc->bge_func_addr);
		break;
	case BGE_APE_LOCK_GRC:
		if (sc->bge_func_addr == 0)
			bit = BGE_APE_LOCK_GRANT_DRIVER0;
		else
			bit = (1 << sc->bge_func_addr);
		break;
	case BGE_APE_LOCK_MEM:
		if (sc->bge_func_addr == 0)
			bit = BGE_APE_LOCK_GRANT_DRIVER0;
		else
			bit = (1 << sc->bge_func_addr);
		break;
	case BGE_APE_LOCK_PHY0:
	case BGE_APE_LOCK_PHY1:
	case BGE_APE_LOCK_PHY2:
	case BGE_APE_LOCK_PHY3:
		bit = BGE_APE_LOCK_GRANT_DRIVER0;
		break;
	default:
		return;
	}

	APE_WRITE_4(sc, gnt + off, bit);
}

/*
 * Send an event to the APE firmware.
 */
static void
bge_ape_send_event(struct bge_softc *sc, uint32_t event)
{
	uint32_t apedata;
	int i;

	/* NCSI does not support APE events. */
	if ((sc->bge_mfw_flags & BGE_MFW_ON_APE) == 0)
		return;

	/* Wait up to 1ms for APE to service previous event. */
	for (i = 10; i > 0; i--) {
		if (bge_ape_lock(sc, BGE_APE_LOCK_MEM) != 0)
			break;
		apedata = APE_READ_4(sc, BGE_APE_EVENT_STATUS);
		if ((apedata & BGE_APE_EVENT_STATUS_EVENT_PENDING) == 0) {
			APE_WRITE_4(sc, BGE_APE_EVENT_STATUS, event |
			    BGE_APE_EVENT_STATUS_EVENT_PENDING);
			bge_ape_unlock(sc, BGE_APE_LOCK_MEM);
			APE_WRITE_4(sc, BGE_APE_EVENT, BGE_APE_EVENT_1);
			break;
		}
		bge_ape_unlock(sc, BGE_APE_LOCK_MEM);
		DELAY(100);
	}
	if (i == 0)
		device_printf(sc->bge_dev, "APE event 0x%08x send timed out\n",
		    event);
}

static void
bge_ape_driver_state_change(struct bge_softc *sc, int kind)
{
	uint32_t apedata, event;

	if ((sc->bge_mfw_flags & BGE_MFW_ON_APE) == 0)
		return;

	switch (kind) {
	case BGE_RESET_START:
		/* If this is the first load, clear the load counter. */
		apedata = APE_READ_4(sc, BGE_APE_HOST_SEG_SIG);
		if (apedata != BGE_APE_HOST_SEG_SIG_MAGIC)
			APE_WRITE_4(sc, BGE_APE_HOST_INIT_COUNT, 0);
		else {
			apedata = APE_READ_4(sc, BGE_APE_HOST_INIT_COUNT);
			APE_WRITE_4(sc, BGE_APE_HOST_INIT_COUNT, ++apedata);
		}
		APE_WRITE_4(sc, BGE_APE_HOST_SEG_SIG,
		    BGE_APE_HOST_SEG_SIG_MAGIC);
		APE_WRITE_4(sc, BGE_APE_HOST_SEG_LEN,
		    BGE_APE_HOST_SEG_LEN_MAGIC);

		/* Add some version info if bge(4) supports it. */
		APE_WRITE_4(sc, BGE_APE_HOST_DRIVER_ID,
		    BGE_APE_HOST_DRIVER_ID_MAGIC(1, 0));
		APE_WRITE_4(sc, BGE_APE_HOST_BEHAVIOR,
		    BGE_APE_HOST_BEHAV_NO_PHYLOCK);
		APE_WRITE_4(sc, BGE_APE_HOST_HEARTBEAT_INT_MS,
		    BGE_APE_HOST_HEARTBEAT_INT_DISABLE);
		APE_WRITE_4(sc, BGE_APE_HOST_DRVR_STATE,
		    BGE_APE_HOST_DRVR_STATE_START);
		event = BGE_APE_EVENT_STATUS_STATE_START;
		break;
	case BGE_RESET_SHUTDOWN:
		APE_WRITE_4(sc, BGE_APE_HOST_DRVR_STATE,
		    BGE_APE_HOST_DRVR_STATE_UNLOAD);
		event = BGE_APE_EVENT_STATUS_STATE_UNLOAD;
		break;
	case BGE_RESET_SUSPEND:
		event = BGE_APE_EVENT_STATUS_STATE_SUSPEND;
		break;
	default:
		return;
	}

	bge_ape_send_event(sc, event | BGE_APE_EVENT_STATUS_DRIVER_EVNT |
	    BGE_APE_EVENT_STATUS_STATE_CHNGE);
}

/*
 * Map a single buffer address.
 */

static void
bge_dma_map_addr(void *arg, bus_dma_segment_t *segs, int nseg, int error)
{
	struct bge_dmamap_arg *ctx;

	if (error)
		return;

	KASSERT(nseg == 1, ("%s: %d segments returned!", __func__, nseg));

	ctx = arg;
	ctx->bge_busaddr = segs->ds_addr;
}

static uint8_t
bge_nvram_getbyte(struct bge_softc *sc, int addr, uint8_t *dest)
{
	uint32_t access, byte = 0;
	int i;

	/* Lock. */
	CSR_WRITE_4(sc, BGE_NVRAM_SWARB, BGE_NVRAMSWARB_SET1);
	for (i = 0; i < 8000; i++) {
		if (CSR_READ_4(sc, BGE_NVRAM_SWARB) & BGE_NVRAMSWARB_GNT1)
			break;
		DELAY(20);
	}
	if (i == 8000)
		return (1);

	/* Enable access. */
	access = CSR_READ_4(sc, BGE_NVRAM_ACCESS);
	CSR_WRITE_4(sc, BGE_NVRAM_ACCESS, access | BGE_NVRAMACC_ENABLE);

	CSR_WRITE_4(sc, BGE_NVRAM_ADDR, addr & 0xfffffffc);
	CSR_WRITE_4(sc, BGE_NVRAM_CMD, BGE_NVRAM_READCMD);
	for (i = 0; i < BGE_TIMEOUT * 10; i++) {
		DELAY(10);
		if (CSR_READ_4(sc, BGE_NVRAM_CMD) & BGE_NVRAMCMD_DONE) {
			DELAY(10);
			break;
		}
	}

	if (i == BGE_TIMEOUT * 10) {
		if_printf(sc->bge_ifp, "nvram read timed out\n");
		return (1);
	}

	/* Get result. */
	byte = CSR_READ_4(sc, BGE_NVRAM_RDDATA);

	*dest = (bswap32(byte) >> ((addr % 4) * 8)) & 0xFF;

	/* Disable access. */
	CSR_WRITE_4(sc, BGE_NVRAM_ACCESS, access);

	/* Unlock. */
	CSR_WRITE_4(sc, BGE_NVRAM_SWARB, BGE_NVRAMSWARB_CLR1);
	CSR_READ_4(sc, BGE_NVRAM_SWARB);

	return (0);
}

/*
 * Read a sequence of bytes from NVRAM.
 */
static int
bge_read_nvram(struct bge_softc *sc, caddr_t dest, int off, int cnt)
{
	int err = 0, i;
	uint8_t byte = 0;

	if (sc->bge_asicrev != BGE_ASICREV_BCM5906)
		return (1);

	for (i = 0; i < cnt; i++) {
		err = bge_nvram_getbyte(sc, off + i, &byte);
		if (err)
			break;
		*(dest + i) = byte;
	}

	return (err ? 1 : 0);
}

/*
 * Read a byte of data stored in the EEPROM at address 'addr.' The
 * BCM570x supports both the traditional bitbang interface and an
 * auto access interface for reading the EEPROM. We use the auto
 * access method.
 */
static uint8_t
bge_eeprom_getbyte(struct bge_softc *sc, int addr, uint8_t *dest)
{
	int i;
	uint32_t byte = 0;

	/*
	 * Enable use of auto EEPROM access so we can avoid
	 * having to use the bitbang method.
	 */
	BGE_SETBIT(sc, BGE_MISC_LOCAL_CTL, BGE_MLC_AUTO_EEPROM);

	/* Reset the EEPROM, load the clock period. */
	CSR_WRITE_4(sc, BGE_EE_ADDR,
	    BGE_EEADDR_RESET | BGE_EEHALFCLK(BGE_HALFCLK_384SCL));
	DELAY(20);

	/* Issue the read EEPROM command. */
	CSR_WRITE_4(sc, BGE_EE_ADDR, BGE_EE_READCMD | addr);

	/* Wait for completion */
	for(i = 0; i < BGE_TIMEOUT * 10; i++) {
		DELAY(10);
		if (CSR_READ_4(sc, BGE_EE_ADDR) & BGE_EEADDR_DONE)
			break;
	}

	if (i == BGE_TIMEOUT * 10) {
		device_printf(sc->bge_dev, "EEPROM read timed out\n");
		return (1);
	}

	/* Get result. */
	byte = CSR_READ_4(sc, BGE_EE_DATA);

	*dest = (byte >> ((addr % 4) * 8)) & 0xFF;

	return (0);
}

/*
 * Read a sequence of bytes from the EEPROM.
 */
static int
bge_read_eeprom(struct bge_softc *sc, caddr_t dest, int off, int cnt)
{
	int i, error = 0;
	uint8_t byte = 0;

	for (i = 0; i < cnt; i++) {
		error = bge_eeprom_getbyte(sc, off + i, &byte);
		if (error)
			break;
		*(dest + i) = byte;
	}

	return (error ? 1 : 0);
}

static int
bge_miibus_readreg(device_t dev, int phy, int reg)
{
	struct bge_softc *sc;
	uint32_t val;
	int i;

	sc = device_get_softc(dev);

	if (bge_ape_lock(sc, sc->bge_phy_ape_lock) != 0)
		return (0);

	/* Clear the autopoll bit if set, otherwise may trigger PCI errors. */
	if ((sc->bge_mi_mode & BGE_MIMODE_AUTOPOLL) != 0) {
		CSR_WRITE_4(sc, BGE_MI_MODE,
		    sc->bge_mi_mode & ~BGE_MIMODE_AUTOPOLL);
		DELAY(80);
	}

	CSR_WRITE_4(sc, BGE_MI_COMM, BGE_MICMD_READ | BGE_MICOMM_BUSY |
	    BGE_MIPHY(phy) | BGE_MIREG(reg));

	/* Poll for the PHY register access to complete. */
	for (i = 0; i < BGE_TIMEOUT; i++) {
		DELAY(10);
		val = CSR_READ_4(sc, BGE_MI_COMM);
		if ((val & BGE_MICOMM_BUSY) == 0) {
			DELAY(5);
			val = CSR_READ_4(sc, BGE_MI_COMM);
			break;
		}
	}

	if (i == BGE_TIMEOUT) {
		device_printf(sc->bge_dev,
		    "PHY read timed out (phy %d, reg %d, val 0x%08x)\n",
		    phy, reg, val);
		val = 0;
	}

	/* Restore the autopoll bit if necessary. */
	if ((sc->bge_mi_mode & BGE_MIMODE_AUTOPOLL) != 0) {
		CSR_WRITE_4(sc, BGE_MI_MODE, sc->bge_mi_mode);
		DELAY(80);
	}

	bge_ape_unlock(sc, sc->bge_phy_ape_lock);

	if (val & BGE_MICOMM_READFAIL)
		return (0);

	return (val & 0xFFFF);
}

static int
bge_miibus_writereg(device_t dev, int phy, int reg, int val)
{
	struct bge_softc *sc;
	int i;

	sc = device_get_softc(dev);

	if (sc->bge_asicrev == BGE_ASICREV_BCM5906 &&
	    (reg == BRGPHY_MII_1000CTL || reg == BRGPHY_MII_AUXCTL))
		return (0);

	if (bge_ape_lock(sc, sc->bge_phy_ape_lock) != 0)
		return (0);

	/* Clear the autopoll bit if set, otherwise may trigger PCI errors. */
	if ((sc->bge_mi_mode & BGE_MIMODE_AUTOPOLL) != 0) {
		CSR_WRITE_4(sc, BGE_MI_MODE,
		    sc->bge_mi_mode & ~BGE_MIMODE_AUTOPOLL);
		DELAY(80);
	}

	CSR_WRITE_4(sc, BGE_MI_COMM, BGE_MICMD_WRITE | BGE_MICOMM_BUSY |
	    BGE_MIPHY(phy) | BGE_MIREG(reg) | val);

	for (i = 0; i < BGE_TIMEOUT; i++) {
		DELAY(10);
		if (!(CSR_READ_4(sc, BGE_MI_COMM) & BGE_MICOMM_BUSY)) {
			DELAY(5);
			CSR_READ_4(sc, BGE_MI_COMM); /* dummy read */
			break;
		}
	}

	/* Restore the autopoll bit if necessary. */
	if ((sc->bge_mi_mode & BGE_MIMODE_AUTOPOLL) != 0) {
		CSR_WRITE_4(sc, BGE_MI_MODE, sc->bge_mi_mode);
		DELAY(80);
	}

	bge_ape_unlock(sc, sc->bge_phy_ape_lock);

	if (i == BGE_TIMEOUT)
		device_printf(sc->bge_dev,
		    "PHY write timed out (phy %d, reg %d, val 0x%04x)\n",
		    phy, reg, val);

	return (0);
}

static void
bge_miibus_statchg(device_t dev)
{
	struct bge_softc *sc;
	struct mii_data *mii;
	uint32_t mac_mode, rx_mode, tx_mode;

	sc = device_get_softc(dev);
	if ((if_getdrvflags(sc->bge_ifp) & IFF_DRV_RUNNING) == 0)
		return;
	mii = device_get_softc(sc->bge_miibus);

	if ((mii->mii_media_status & (IFM_ACTIVE | IFM_AVALID)) ==
	    (IFM_ACTIVE | IFM_AVALID)) {
		switch (IFM_SUBTYPE(mii->mii_media_active)) {
		case IFM_10_T:
		case IFM_100_TX:
			sc->bge_link = 1;
			break;
		case IFM_1000_T:
		case IFM_1000_SX:
		case IFM_2500_SX:
			if (sc->bge_asicrev != BGE_ASICREV_BCM5906)
				sc->bge_link = 1;
			else
				sc->bge_link = 0;
			break;
		default:
			sc->bge_link = 0;
			break;
		}
	} else
		sc->bge_link = 0;
	if (sc->bge_link == 0)
		return;

	/*
	 * APE firmware touches these registers to keep the MAC
	 * connected to the outside world.  Try to keep the
	 * accesses atomic.
	 */

	/* Set the port mode (MII/GMII) to match the link speed. */
	mac_mode = CSR_READ_4(sc, BGE_MAC_MODE) &
	    ~(BGE_MACMODE_PORTMODE | BGE_MACMODE_HALF_DUPLEX);
	tx_mode = CSR_READ_4(sc, BGE_TX_MODE);
	rx_mode = CSR_READ_4(sc, BGE_RX_MODE);

	if (IFM_SUBTYPE(mii->mii_media_active) == IFM_1000_T ||
	    IFM_SUBTYPE(mii->mii_media_active) == IFM_1000_SX)
		mac_mode |= BGE_PORTMODE_GMII;
	else
		mac_mode |= BGE_PORTMODE_MII;

	/* Set MAC flow control behavior to match link flow control settings. */
	tx_mode &= ~BGE_TXMODE_FLOWCTL_ENABLE;
	rx_mode &= ~BGE_RXMODE_FLOWCTL_ENABLE;
	if ((IFM_OPTIONS(mii->mii_media_active) & IFM_FDX) != 0) {
		if ((IFM_OPTIONS(mii->mii_media_active) & IFM_ETH_TXPAUSE) != 0)
			tx_mode |= BGE_TXMODE_FLOWCTL_ENABLE;
		if ((IFM_OPTIONS(mii->mii_media_active) & IFM_ETH_RXPAUSE) != 0)
			rx_mode |= BGE_RXMODE_FLOWCTL_ENABLE;
	} else
		mac_mode |= BGE_MACMODE_HALF_DUPLEX;

	CSR_WRITE_4(sc, BGE_MAC_MODE, mac_mode);
	DELAY(40);
	CSR_WRITE_4(sc, BGE_TX_MODE, tx_mode);
	CSR_WRITE_4(sc, BGE_RX_MODE, rx_mode);
}

/*
 * Intialize a standard receive ring descriptor.
 */
static int
bge_newbuf_std(struct bge_softc *sc, int i)
{
	struct mbuf *m;
	struct bge_rx_bd *r;
	bus_dma_segment_t segs[1];
	bus_dmamap_t map;
	int error, nsegs;

	if (sc->bge_flags & BGE_FLAG_JUMBO_STD &&
	    (if_getmtu(sc->bge_ifp) + ETHER_HDR_LEN + ETHER_CRC_LEN +
	    ETHER_VLAN_ENCAP_LEN > (MCLBYTES - ETHER_ALIGN))) {
		m = m_getjcl(M_NOWAIT, MT_DATA, M_PKTHDR, MJUM9BYTES);
		if (m == NULL)
			return (ENOBUFS);
		m->m_len = m->m_pkthdr.len = MJUM9BYTES;
	} else {
		m = m_getcl(M_NOWAIT, MT_DATA, M_PKTHDR);
		if (m == NULL)
			return (ENOBUFS);
		m->m_len = m->m_pkthdr.len = MCLBYTES;
	}
	if ((sc->bge_flags & BGE_FLAG_RX_ALIGNBUG) == 0)
		m_adj(m, ETHER_ALIGN);

	error = bus_dmamap_load_mbuf_sg(sc->bge_cdata.bge_rx_mtag,
	    sc->bge_cdata.bge_rx_std_sparemap, m, segs, &nsegs, 0);
	if (error != 0) {
		m_freem(m);
		return (error);
	}
	if (sc->bge_cdata.bge_rx_std_chain[i] != NULL) {
		bus_dmamap_sync(sc->bge_cdata.bge_rx_mtag,
		    sc->bge_cdata.bge_rx_std_dmamap[i], BUS_DMASYNC_POSTREAD);
		bus_dmamap_unload(sc->bge_cdata.bge_rx_mtag,
		    sc->bge_cdata.bge_rx_std_dmamap[i]);
	}
	map = sc->bge_cdata.bge_rx_std_dmamap[i];
	sc->bge_cdata.bge_rx_std_dmamap[i] = sc->bge_cdata.bge_rx_std_sparemap;
	sc->bge_cdata.bge_rx_std_sparemap = map;
	sc->bge_cdata.bge_rx_std_chain[i] = m;
	sc->bge_cdata.bge_rx_std_seglen[i] = segs[0].ds_len;
	r = &sc->bge_ldata.bge_rx_std_ring[sc->bge_std];
	r->bge_addr.bge_addr_lo = BGE_ADDR_LO(segs[0].ds_addr);
	r->bge_addr.bge_addr_hi = BGE_ADDR_HI(segs[0].ds_addr);
	r->bge_flags = BGE_RXBDFLAG_END;
	r->bge_len = segs[0].ds_len;
	r->bge_idx = i;

	bus_dmamap_sync(sc->bge_cdata.bge_rx_mtag,
	    sc->bge_cdata.bge_rx_std_dmamap[i], BUS_DMASYNC_PREREAD);

	return (0);
}

/*
 * Initialize a jumbo receive ring descriptor. This allocates
 * a jumbo buffer from the pool managed internally by the driver.
 */
static int
bge_newbuf_jumbo(struct bge_softc *sc, int i)
{
	bus_dma_segment_t segs[BGE_NSEG_JUMBO];
	bus_dmamap_t map;
	struct bge_extrx_bd *r;
	struct mbuf *m;
	int error, nsegs;

	MGETHDR(m, M_NOWAIT, MT_DATA);
	if (m == NULL)
		return (ENOBUFS);

	if (m_cljget(m, M_NOWAIT, MJUM9BYTES) == NULL) {
		m_freem(m);
		return (ENOBUFS);
	}
	m->m_len = m->m_pkthdr.len = MJUM9BYTES;
	if ((sc->bge_flags & BGE_FLAG_RX_ALIGNBUG) == 0)
		m_adj(m, ETHER_ALIGN);

	error = bus_dmamap_load_mbuf_sg(sc->bge_cdata.bge_mtag_jumbo,
	    sc->bge_cdata.bge_rx_jumbo_sparemap, m, segs, &nsegs, 0);
	if (error != 0) {
		m_freem(m);
		return (error);
	}

	if (sc->bge_cdata.bge_rx_jumbo_chain[i] != NULL) {
		bus_dmamap_sync(sc->bge_cdata.bge_mtag_jumbo,
		    sc->bge_cdata.bge_rx_jumbo_dmamap[i], BUS_DMASYNC_POSTREAD);
		bus_dmamap_unload(sc->bge_cdata.bge_mtag_jumbo,
		    sc->bge_cdata.bge_rx_jumbo_dmamap[i]);
	}
	map = sc->bge_cdata.bge_rx_jumbo_dmamap[i];
	sc->bge_cdata.bge_rx_jumbo_dmamap[i] =
	    sc->bge_cdata.bge_rx_jumbo_sparemap;
	sc->bge_cdata.bge_rx_jumbo_sparemap = map;
	sc->bge_cdata.bge_rx_jumbo_chain[i] = m;
	sc->bge_cdata.bge_rx_jumbo_seglen[i][0] = 0;
	sc->bge_cdata.bge_rx_jumbo_seglen[i][1] = 0;
	sc->bge_cdata.bge_rx_jumbo_seglen[i][2] = 0;
	sc->bge_cdata.bge_rx_jumbo_seglen[i][3] = 0;

	/*
	 * Fill in the extended RX buffer descriptor.
	 */
	r = &sc->bge_ldata.bge_rx_jumbo_ring[sc->bge_jumbo];
	r->bge_flags = BGE_RXBDFLAG_JUMBO_RING | BGE_RXBDFLAG_END;
	r->bge_idx = i;
	r->bge_len3 = r->bge_len2 = r->bge_len1 = 0;
	switch (nsegs) {
	case 4:
		r->bge_addr3.bge_addr_lo = BGE_ADDR_LO(segs[3].ds_addr);
		r->bge_addr3.bge_addr_hi = BGE_ADDR_HI(segs[3].ds_addr);
		r->bge_len3 = segs[3].ds_len;
		sc->bge_cdata.bge_rx_jumbo_seglen[i][3] = segs[3].ds_len;
	case 3:
		r->bge_addr2.bge_addr_lo = BGE_ADDR_LO(segs[2].ds_addr);
		r->bge_addr2.bge_addr_hi = BGE_ADDR_HI(segs[2].ds_addr);
		r->bge_len2 = segs[2].ds_len;
		sc->bge_cdata.bge_rx_jumbo_seglen[i][2] = segs[2].ds_len;
	case 2:
		r->bge_addr1.bge_addr_lo = BGE_ADDR_LO(segs[1].ds_addr);
		r->bge_addr1.bge_addr_hi = BGE_ADDR_HI(segs[1].ds_addr);
		r->bge_len1 = segs[1].ds_len;
		sc->bge_cdata.bge_rx_jumbo_seglen[i][1] = segs[1].ds_len;
	case 1:
		r->bge_addr0.bge_addr_lo = BGE_ADDR_LO(segs[0].ds_addr);
		r->bge_addr0.bge_addr_hi = BGE_ADDR_HI(segs[0].ds_addr);
		r->bge_len0 = segs[0].ds_len;
		sc->bge_cdata.bge_rx_jumbo_seglen[i][0] = segs[0].ds_len;
		break;
	default:
		panic("%s: %d segments\n", __func__, nsegs);
	}

	bus_dmamap_sync(sc->bge_cdata.bge_mtag_jumbo,
	    sc->bge_cdata.bge_rx_jumbo_dmamap[i], BUS_DMASYNC_PREREAD);

	return (0);
}

static int
bge_init_rx_ring_std(struct bge_softc *sc)
{
	int error, i;

	bzero(sc->bge_ldata.bge_rx_std_ring, BGE_STD_RX_RING_SZ);
	sc->bge_std = 0;
	for (i = 0; i < BGE_STD_RX_RING_CNT; i++) {
		if ((error = bge_newbuf_std(sc, i)) != 0)
			return (error);
		BGE_INC(sc->bge_std, BGE_STD_RX_RING_CNT);
	}

	bus_dmamap_sync(sc->bge_cdata.bge_rx_std_ring_tag,
	    sc->bge_cdata.bge_rx_std_ring_map, BUS_DMASYNC_PREWRITE);

	sc->bge_std = 0;
	bge_writembx(sc, BGE_MBX_RX_STD_PROD_LO, BGE_STD_RX_RING_CNT - 1);

	return (0);
}

static void
bge_free_rx_ring_std(struct bge_softc *sc)
{
	int i;

	for (i = 0; i < BGE_STD_RX_RING_CNT; i++) {
		if (sc->bge_cdata.bge_rx_std_chain[i] != NULL) {
			bus_dmamap_sync(sc->bge_cdata.bge_rx_mtag,
			    sc->bge_cdata.bge_rx_std_dmamap[i],
			    BUS_DMASYNC_POSTREAD);
			bus_dmamap_unload(sc->bge_cdata.bge_rx_mtag,
			    sc->bge_cdata.bge_rx_std_dmamap[i]);
			m_freem(sc->bge_cdata.bge_rx_std_chain[i]);
			sc->bge_cdata.bge_rx_std_chain[i] = NULL;
		}
		bzero((char *)&sc->bge_ldata.bge_rx_std_ring[i],
		    sizeof(struct bge_rx_bd));
	}
}

static int
bge_init_rx_ring_jumbo(struct bge_softc *sc)
{
	struct bge_rcb *rcb;
	int error, i;

	bzero(sc->bge_ldata.bge_rx_jumbo_ring, BGE_JUMBO_RX_RING_SZ);
	sc->bge_jumbo = 0;
	for (i = 0; i < BGE_JUMBO_RX_RING_CNT; i++) {
		if ((error = bge_newbuf_jumbo(sc, i)) != 0)
			return (error);
		BGE_INC(sc->bge_jumbo, BGE_JUMBO_RX_RING_CNT);
	}

	bus_dmamap_sync(sc->bge_cdata.bge_rx_jumbo_ring_tag,
	    sc->bge_cdata.bge_rx_jumbo_ring_map, BUS_DMASYNC_PREWRITE);

	sc->bge_jumbo = 0;

	/* Enable the jumbo receive producer ring. */
	rcb = &sc->bge_ldata.bge_info.bge_jumbo_rx_rcb;
	rcb->bge_maxlen_flags =
	    BGE_RCB_MAXLEN_FLAGS(0, BGE_RCB_FLAG_USE_EXT_RX_BD);
	CSR_WRITE_4(sc, BGE_RX_JUMBO_RCB_MAXLEN_FLAGS, rcb->bge_maxlen_flags);

	bge_writembx(sc, BGE_MBX_RX_JUMBO_PROD_LO, BGE_JUMBO_RX_RING_CNT - 1);

	return (0);
}

static void
bge_free_rx_ring_jumbo(struct bge_softc *sc)
{
	int i;

	for (i = 0; i < BGE_JUMBO_RX_RING_CNT; i++) {
		if (sc->bge_cdata.bge_rx_jumbo_chain[i] != NULL) {
			bus_dmamap_sync(sc->bge_cdata.bge_mtag_jumbo,
			    sc->bge_cdata.bge_rx_jumbo_dmamap[i],
			    BUS_DMASYNC_POSTREAD);
			bus_dmamap_unload(sc->bge_cdata.bge_mtag_jumbo,
			    sc->bge_cdata.bge_rx_jumbo_dmamap[i]);
			m_freem(sc->bge_cdata.bge_rx_jumbo_chain[i]);
			sc->bge_cdata.bge_rx_jumbo_chain[i] = NULL;
		}
		bzero((char *)&sc->bge_ldata.bge_rx_jumbo_ring[i],
		    sizeof(struct bge_extrx_bd));
	}
}

static void
bge_free_tx_ring(struct bge_softc *sc)
{
	int i;

	if (sc->bge_ldata.bge_tx_ring == NULL)
		return;

	for (i = 0; i < BGE_TX_RING_CNT; i++) {
		if (sc->bge_cdata.bge_tx_chain[i] != NULL) {
			bus_dmamap_sync(sc->bge_cdata.bge_tx_mtag,
			    sc->bge_cdata.bge_tx_dmamap[i],
			    BUS_DMASYNC_POSTWRITE);
			bus_dmamap_unload(sc->bge_cdata.bge_tx_mtag,
			    sc->bge_cdata.bge_tx_dmamap[i]);
			m_freem(sc->bge_cdata.bge_tx_chain[i]);
			sc->bge_cdata.bge_tx_chain[i] = NULL;
		}
		bzero((char *)&sc->bge_ldata.bge_tx_ring[i],
		    sizeof(struct bge_tx_bd));
	}
}

static int
bge_init_tx_ring(struct bge_softc *sc)
{
	sc->bge_txcnt = 0;
	sc->bge_tx_saved_considx = 0;

	bzero(sc->bge_ldata.bge_tx_ring, BGE_TX_RING_SZ);
	bus_dmamap_sync(sc->bge_cdata.bge_tx_ring_tag,
	    sc->bge_cdata.bge_tx_ring_map, BUS_DMASYNC_PREWRITE);

	/* Initialize transmit producer index for host-memory send ring. */
	sc->bge_tx_prodidx = 0;
	bge_writembx(sc, BGE_MBX_TX_HOST_PROD0_LO, sc->bge_tx_prodidx);

	/* 5700 b2 errata */
	if (sc->bge_chiprev == BGE_CHIPREV_5700_BX)
		bge_writembx(sc, BGE_MBX_TX_HOST_PROD0_LO, sc->bge_tx_prodidx);

	/* NIC-memory send ring not used; initialize to zero. */
	bge_writembx(sc, BGE_MBX_TX_NIC_PROD0_LO, 0);
	/* 5700 b2 errata */
	if (sc->bge_chiprev == BGE_CHIPREV_5700_BX)
		bge_writembx(sc, BGE_MBX_TX_NIC_PROD0_LO, 0);

	return (0);
}

static void
bge_setpromisc(struct bge_softc *sc)
{
	if_t ifp;

	BGE_LOCK_ASSERT(sc);

	ifp = sc->bge_ifp;

	/* Enable or disable promiscuous mode as needed. */
	if (if_getflags(ifp) & IFF_PROMISC)
		BGE_SETBIT(sc, BGE_RX_MODE, BGE_RXMODE_RX_PROMISC);
	else
		BGE_CLRBIT(sc, BGE_RX_MODE, BGE_RXMODE_RX_PROMISC);
}

static void
bge_setmulti(struct bge_softc *sc)
{
	if_t ifp;
	int mc_count = 0;
	uint32_t hashes[4] = { 0, 0, 0, 0 };
	int h, i, mcnt;
	unsigned char *mta;

	BGE_LOCK_ASSERT(sc);

	ifp = sc->bge_ifp;

	mc_count = if_multiaddr_count(ifp, -1);
	mta = malloc(sizeof(unsigned char) *  ETHER_ADDR_LEN *
	    mc_count, M_DEVBUF, M_NOWAIT);

	if(mta == NULL) {
		device_printf(sc->bge_dev, 
		    "Failed to allocated temp mcast list\n");
		return;
	}

	if (if_getflags(ifp) & IFF_ALLMULTI || if_getflags(ifp) & IFF_PROMISC) {
		for (i = 0; i < 4; i++)
			CSR_WRITE_4(sc, BGE_MAR0 + (i * 4), 0xFFFFFFFF);
		free(mta, M_DEVBUF);
		return;
	}

	/* First, zot all the existing filters. */
	for (i = 0; i < 4; i++)
		CSR_WRITE_4(sc, BGE_MAR0 + (i * 4), 0);

	if_multiaddr_array(ifp, mta, &mcnt, mc_count);
	for(i = 0; i < mcnt; i++) {
		h = ether_crc32_le(mta + (i * ETHER_ADDR_LEN),
		    ETHER_ADDR_LEN) & 0x7F;
		hashes[(h & 0x60) >> 5] |= 1 << (h & 0x1F);
	}

	for (i = 0; i < 4; i++)
		CSR_WRITE_4(sc, BGE_MAR0 + (i * 4), hashes[i]);

	free(mta, M_DEVBUF);
}

static void
bge_setvlan(struct bge_softc *sc)
{
	if_t ifp;

	BGE_LOCK_ASSERT(sc);

	ifp = sc->bge_ifp;

	/* Enable or disable VLAN tag stripping as needed. */
	if (if_getcapenable(ifp) & IFCAP_VLAN_HWTAGGING)
		BGE_CLRBIT(sc, BGE_RX_MODE, BGE_RXMODE_RX_KEEP_VLAN_DIAG);
	else
		BGE_SETBIT(sc, BGE_RX_MODE, BGE_RXMODE_RX_KEEP_VLAN_DIAG);
}

static void
bge_sig_pre_reset(struct bge_softc *sc, int type)
{

	/*
	 * Some chips don't like this so only do this if ASF is enabled
	 */
	if (sc->bge_asf_mode)
		bge_writemem_ind(sc, BGE_SRAM_FW_MB, BGE_SRAM_FW_MB_MAGIC);

	if (sc->bge_asf_mode & ASF_NEW_HANDSHAKE) {
		switch (type) {
		case BGE_RESET_START:
			bge_writemem_ind(sc, BGE_SRAM_FW_DRV_STATE_MB,
			    BGE_FW_DRV_STATE_START);
			break;
		case BGE_RESET_SHUTDOWN:
			bge_writemem_ind(sc, BGE_SRAM_FW_DRV_STATE_MB,
			    BGE_FW_DRV_STATE_UNLOAD);
			break;
		case BGE_RESET_SUSPEND:
			bge_writemem_ind(sc, BGE_SRAM_FW_DRV_STATE_MB,
			    BGE_FW_DRV_STATE_SUSPEND);
			break;
		}
	}

	if (type == BGE_RESET_START || type == BGE_RESET_SUSPEND)
		bge_ape_driver_state_change(sc, type);
}

static void
bge_sig_post_reset(struct bge_softc *sc, int type)
{

	if (sc->bge_asf_mode & ASF_NEW_HANDSHAKE) {
		switch (type) {
		case BGE_RESET_START:
			bge_writemem_ind(sc, BGE_SRAM_FW_DRV_STATE_MB,
			    BGE_FW_DRV_STATE_START_DONE);
			/* START DONE */
			break;
		case BGE_RESET_SHUTDOWN:
			bge_writemem_ind(sc, BGE_SRAM_FW_DRV_STATE_MB,
			    BGE_FW_DRV_STATE_UNLOAD_DONE);
			break;
		}
	}
	if (type == BGE_RESET_SHUTDOWN)
		bge_ape_driver_state_change(sc, type);
}

static void
bge_sig_legacy(struct bge_softc *sc, int type)
{

	if (sc->bge_asf_mode) {
		switch (type) {
		case BGE_RESET_START:
			bge_writemem_ind(sc, BGE_SRAM_FW_DRV_STATE_MB,
			    BGE_FW_DRV_STATE_START);
			break;
		case BGE_RESET_SHUTDOWN:
			bge_writemem_ind(sc, BGE_SRAM_FW_DRV_STATE_MB,
			    BGE_FW_DRV_STATE_UNLOAD);
			break;
		}
	}
}

static void
bge_stop_fw(struct bge_softc *sc)
{
	int i;

	if (sc->bge_asf_mode) {
		bge_writemem_ind(sc, BGE_SRAM_FW_CMD_MB, BGE_FW_CMD_PAUSE);
		CSR_WRITE_4(sc, BGE_RX_CPU_EVENT,
		    CSR_READ_4(sc, BGE_RX_CPU_EVENT) | BGE_RX_CPU_DRV_EVENT);

		for (i = 0; i < 100; i++ ) {
			if (!(CSR_READ_4(sc, BGE_RX_CPU_EVENT) &
			    BGE_RX_CPU_DRV_EVENT))
				break;
			DELAY(10);
		}
	}
}

static uint32_t
bge_dma_swap_options(struct bge_softc *sc)
{
	uint32_t dma_options;

	dma_options = BGE_MODECTL_WORDSWAP_NONFRAME |
	    BGE_MODECTL_BYTESWAP_DATA | BGE_MODECTL_WORDSWAP_DATA;
#if BYTE_ORDER == BIG_ENDIAN
	dma_options |= BGE_MODECTL_BYTESWAP_NONFRAME;
#endif
	return (dma_options);
}

/*
 * Do endian, PCI and DMA initialization.
 */
static int
bge_chipinit(struct bge_softc *sc)
{
	uint32_t dma_rw_ctl, misc_ctl, mode_ctl;
	uint16_t val;
	int i;

	/* Set endianness before we access any non-PCI registers. */
	misc_ctl = BGE_INIT;
	if (sc->bge_flags & BGE_FLAG_TAGGED_STATUS)
		misc_ctl |= BGE_PCIMISCCTL_TAGGED_STATUS;
	pci_write_config(sc->bge_dev, BGE_PCI_MISC_CTL, misc_ctl, 4);

	/*
	 * Clear the MAC statistics block in the NIC's
	 * internal memory.
	 */
	for (i = BGE_STATS_BLOCK;
	    i < BGE_STATS_BLOCK_END + 1; i += sizeof(uint32_t))
		BGE_MEMWIN_WRITE(sc, i, 0);

	for (i = BGE_STATUS_BLOCK;
	    i < BGE_STATUS_BLOCK_END + 1; i += sizeof(uint32_t))
		BGE_MEMWIN_WRITE(sc, i, 0);

	if (sc->bge_chiprev == BGE_CHIPREV_5704_BX) {
		/*
		 *  Fix data corruption caused by non-qword write with WB.
		 *  Fix master abort in PCI mode.
		 *  Fix PCI latency timer.
		 */
		val = pci_read_config(sc->bge_dev, BGE_PCI_MSI_DATA + 2, 2);
		val |= (1 << 10) | (1 << 12) | (1 << 13);
		pci_write_config(sc->bge_dev, BGE_PCI_MSI_DATA + 2, val, 2);
	}

	if (sc->bge_asicrev == BGE_ASICREV_BCM57765 ||
	    sc->bge_asicrev == BGE_ASICREV_BCM57766) {
		/*
		 * For the 57766 and non Ax versions of 57765, bootcode
		 * needs to setup the PCIE Fast Training Sequence (FTS)
		 * value to prevent transmit hangs.
		 */
		if (sc->bge_chiprev != BGE_CHIPREV_57765_AX) {
			CSR_WRITE_4(sc, BGE_CPMU_PADRNG_CTL,
			    CSR_READ_4(sc, BGE_CPMU_PADRNG_CTL) |
			    BGE_CPMU_PADRNG_CTL_RDIV2);
		}
	}

	/*
	 * Set up the PCI DMA control register.
	 */
	dma_rw_ctl = BGE_PCIDMARWCTL_RD_CMD_SHIFT(6) |
	    BGE_PCIDMARWCTL_WR_CMD_SHIFT(7);
	if (sc->bge_flags & BGE_FLAG_PCIE) {
		if (sc->bge_mps >= 256)
			dma_rw_ctl |= BGE_PCIDMARWCTL_WR_WAT_SHIFT(7);
		else
			dma_rw_ctl |= BGE_PCIDMARWCTL_WR_WAT_SHIFT(3);
	} else if (sc->bge_flags & BGE_FLAG_PCIX) {
		if (BGE_IS_5714_FAMILY(sc)) {
			/* 256 bytes for read and write. */
			dma_rw_ctl |= BGE_PCIDMARWCTL_RD_WAT_SHIFT(2) |
			    BGE_PCIDMARWCTL_WR_WAT_SHIFT(2);
			dma_rw_ctl |= (sc->bge_asicrev == BGE_ASICREV_BCM5780) ?
			    BGE_PCIDMARWCTL_ONEDMA_ATONCE_GLOBAL :
			    BGE_PCIDMARWCTL_ONEDMA_ATONCE_LOCAL;
		} else if (sc->bge_asicrev == BGE_ASICREV_BCM5703) {
			/*
			 * In the BCM5703, the DMA read watermark should
			 * be set to less than or equal to the maximum
			 * memory read byte count of the PCI-X command
			 * register.
			 */
			dma_rw_ctl |= BGE_PCIDMARWCTL_RD_WAT_SHIFT(4) |
			    BGE_PCIDMARWCTL_WR_WAT_SHIFT(3);
		} else if (sc->bge_asicrev == BGE_ASICREV_BCM5704) {
			/* 1536 bytes for read, 384 bytes for write. */
			dma_rw_ctl |= BGE_PCIDMARWCTL_RD_WAT_SHIFT(7) |
			    BGE_PCIDMARWCTL_WR_WAT_SHIFT(3);
		} else {
			/* 384 bytes for read and write. */
			dma_rw_ctl |= BGE_PCIDMARWCTL_RD_WAT_SHIFT(3) |
			    BGE_PCIDMARWCTL_WR_WAT_SHIFT(3) |
			    0x0F;
		}
		if (sc->bge_asicrev == BGE_ASICREV_BCM5703 ||
		    sc->bge_asicrev == BGE_ASICREV_BCM5704) {
			uint32_t tmp;

			/* Set ONE_DMA_AT_ONCE for hardware workaround. */
			tmp = CSR_READ_4(sc, BGE_PCI_CLKCTL) & 0x1F;
			if (tmp == 6 || tmp == 7)
				dma_rw_ctl |=
				    BGE_PCIDMARWCTL_ONEDMA_ATONCE_GLOBAL;

			/* Set PCI-X DMA write workaround. */
			dma_rw_ctl |= BGE_PCIDMARWCTL_ASRT_ALL_BE;
		}
	} else {
		/* Conventional PCI bus: 256 bytes for read and write. */
		dma_rw_ctl |= BGE_PCIDMARWCTL_RD_WAT_SHIFT(7) |
		    BGE_PCIDMARWCTL_WR_WAT_SHIFT(7);

		if (sc->bge_asicrev != BGE_ASICREV_BCM5705 &&
		    sc->bge_asicrev != BGE_ASICREV_BCM5750)
			dma_rw_ctl |= 0x0F;
	}
	if (sc->bge_asicrev == BGE_ASICREV_BCM5700 ||
	    sc->bge_asicrev == BGE_ASICREV_BCM5701)
		dma_rw_ctl |= BGE_PCIDMARWCTL_USE_MRM |
		    BGE_PCIDMARWCTL_ASRT_ALL_BE;
	if (sc->bge_asicrev == BGE_ASICREV_BCM5703 ||
	    sc->bge_asicrev == BGE_ASICREV_BCM5704)
		dma_rw_ctl &= ~BGE_PCIDMARWCTL_MINDMA;
	if (BGE_IS_5717_PLUS(sc)) {
		dma_rw_ctl &= ~BGE_PCIDMARWCTL_DIS_CACHE_ALIGNMENT;
		if (sc->bge_chipid == BGE_CHIPID_BCM57765_A0)
			dma_rw_ctl &= ~BGE_PCIDMARWCTL_CRDRDR_RDMA_MRRS_MSK;
		/*
		 * Enable HW workaround for controllers that misinterpret
		 * a status tag update and leave interrupts permanently
		 * disabled.
		 */
		if (!BGE_IS_57765_PLUS(sc) &&
		    sc->bge_asicrev != BGE_ASICREV_BCM5717 &&
		    sc->bge_asicrev != BGE_ASICREV_BCM5762)
			dma_rw_ctl |= BGE_PCIDMARWCTL_TAGGED_STATUS_WA;
	}
	pci_write_config(sc->bge_dev, BGE_PCI_DMA_RW_CTL, dma_rw_ctl, 4);

	/*
	 * Set up general mode register.
	 */
	mode_ctl = bge_dma_swap_options(sc);
	if (sc->bge_asicrev == BGE_ASICREV_BCM5720 ||
	    sc->bge_asicrev == BGE_ASICREV_BCM5762) {
		/* Retain Host-2-BMC settings written by APE firmware. */
		mode_ctl |= CSR_READ_4(sc, BGE_MODE_CTL) &
		    (BGE_MODECTL_BYTESWAP_B2HRX_DATA |
		    BGE_MODECTL_WORDSWAP_B2HRX_DATA |
		    BGE_MODECTL_B2HRX_ENABLE | BGE_MODECTL_HTX2B_ENABLE);
	}
	mode_ctl |= BGE_MODECTL_MAC_ATTN_INTR | BGE_MODECTL_HOST_SEND_BDS |
	    BGE_MODECTL_TX_NO_PHDR_CSUM;

	/*
	 * BCM5701 B5 have a bug causing data corruption when using
	 * 64-bit DMA reads, which can be terminated early and then
	 * completed later as 32-bit accesses, in combination with
	 * certain bridges.
	 */
	if (sc->bge_asicrev == BGE_ASICREV_BCM5701 &&
	    sc->bge_chipid == BGE_CHIPID_BCM5701_B5)
		mode_ctl |= BGE_MODECTL_FORCE_PCI32;

	/*
	 * Tell the firmware the driver is running
	 */
	if (sc->bge_asf_mode & ASF_STACKUP)
		mode_ctl |= BGE_MODECTL_STACKUP;

	CSR_WRITE_4(sc, BGE_MODE_CTL, mode_ctl);

	/*
	 * Disable memory write invalidate.  Apparently it is not supported
	 * properly by these devices.
	 */
	PCI_CLRBIT(sc->bge_dev, BGE_PCI_CMD, PCIM_CMD_MWIEN, 4);

	/* Set the timer prescaler (always 66 MHz). */
	CSR_WRITE_4(sc, BGE_MISC_CFG, BGE_32BITTIME_66MHZ);

	/* XXX: The Linux tg3 driver does this at the start of brgphy_reset. */
	if (sc->bge_asicrev == BGE_ASICREV_BCM5906) {
		DELAY(40);	/* XXX */

		/* Put PHY into ready state */
		BGE_CLRBIT(sc, BGE_MISC_CFG, BGE_MISCCFG_EPHY_IDDQ);
		CSR_READ_4(sc, BGE_MISC_CFG); /* Flush */
		DELAY(40);
	}

	return (0);
}

static int
bge_blockinit(struct bge_softc *sc)
{
	struct bge_rcb *rcb;
	bus_size_t vrcb;
	bge_hostaddr taddr;
	uint32_t dmactl, rdmareg, val;
	int i, limit;

	/*
	 * Initialize the memory window pointer register so that
	 * we can access the first 32K of internal NIC RAM. This will
	 * allow us to set up the TX send ring RCBs and the RX return
	 * ring RCBs, plus other things which live in NIC memory.
	 */
	CSR_WRITE_4(sc, BGE_PCI_MEMWIN_BASEADDR, 0);

	/* Note: the BCM5704 has a smaller mbuf space than other chips. */

	if (!(BGE_IS_5705_PLUS(sc))) {
		/* Configure mbuf memory pool */
		CSR_WRITE_4(sc, BGE_BMAN_MBUFPOOL_BASEADDR, BGE_BUFFPOOL_1);
		if (sc->bge_asicrev == BGE_ASICREV_BCM5704)
			CSR_WRITE_4(sc, BGE_BMAN_MBUFPOOL_LEN, 0x10000);
		else
			CSR_WRITE_4(sc, BGE_BMAN_MBUFPOOL_LEN, 0x18000);

		/* Configure DMA resource pool */
		CSR_WRITE_4(sc, BGE_BMAN_DMA_DESCPOOL_BASEADDR,
		    BGE_DMA_DESCRIPTORS);
		CSR_WRITE_4(sc, BGE_BMAN_DMA_DESCPOOL_LEN, 0x2000);
	}

	/* Configure mbuf pool watermarks */
	if (BGE_IS_5717_PLUS(sc)) {
		CSR_WRITE_4(sc, BGE_BMAN_MBUFPOOL_READDMA_LOWAT, 0x0);
		if (if_getmtu(sc->bge_ifp) > ETHERMTU) {
			CSR_WRITE_4(sc, BGE_BMAN_MBUFPOOL_MACRX_LOWAT, 0x7e);
			CSR_WRITE_4(sc, BGE_BMAN_MBUFPOOL_HIWAT, 0xea);
		} else {
			CSR_WRITE_4(sc, BGE_BMAN_MBUFPOOL_MACRX_LOWAT, 0x2a);
			CSR_WRITE_4(sc, BGE_BMAN_MBUFPOOL_HIWAT, 0xa0);
		}
	} else if (!BGE_IS_5705_PLUS(sc)) {
		CSR_WRITE_4(sc, BGE_BMAN_MBUFPOOL_READDMA_LOWAT, 0x50);
		CSR_WRITE_4(sc, BGE_BMAN_MBUFPOOL_MACRX_LOWAT, 0x20);
		CSR_WRITE_4(sc, BGE_BMAN_MBUFPOOL_HIWAT, 0x60);
	} else if (sc->bge_asicrev == BGE_ASICREV_BCM5906) {
		CSR_WRITE_4(sc, BGE_BMAN_MBUFPOOL_READDMA_LOWAT, 0x0);
		CSR_WRITE_4(sc, BGE_BMAN_MBUFPOOL_MACRX_LOWAT, 0x04);
		CSR_WRITE_4(sc, BGE_BMAN_MBUFPOOL_HIWAT, 0x10);
	} else {
		CSR_WRITE_4(sc, BGE_BMAN_MBUFPOOL_READDMA_LOWAT, 0x0);
		CSR_WRITE_4(sc, BGE_BMAN_MBUFPOOL_MACRX_LOWAT, 0x10);
		CSR_WRITE_4(sc, BGE_BMAN_MBUFPOOL_HIWAT, 0x60);
	}

	/* Configure DMA resource watermarks */
	CSR_WRITE_4(sc, BGE_BMAN_DMA_DESCPOOL_LOWAT, 5);
	CSR_WRITE_4(sc, BGE_BMAN_DMA_DESCPOOL_HIWAT, 10);

	/* Enable buffer manager */
	val = BGE_BMANMODE_ENABLE | BGE_BMANMODE_LOMBUF_ATTN;
	/*
	 * Change the arbitration algorithm of TXMBUF read request to
	 * round-robin instead of priority based for BCM5719.  When
	 * TXFIFO is almost empty, RDMA will hold its request until
	 * TXFIFO is not almost empty.
	 */
	if (sc->bge_asicrev == BGE_ASICREV_BCM5719)
		val |= BGE_BMANMODE_NO_TX_UNDERRUN;
	CSR_WRITE_4(sc, BGE_BMAN_MODE, val);

	/* Poll for buffer manager start indication */
	for (i = 0; i < BGE_TIMEOUT; i++) {
		DELAY(10);
		if (CSR_READ_4(sc, BGE_BMAN_MODE) & BGE_BMANMODE_ENABLE)
			break;
	}

	if (i == BGE_TIMEOUT) {
		device_printf(sc->bge_dev, "buffer manager failed to start\n");
		return (ENXIO);
	}

	/* Enable flow-through queues */
	CSR_WRITE_4(sc, BGE_FTQ_RESET, 0xFFFFFFFF);
	CSR_WRITE_4(sc, BGE_FTQ_RESET, 0);

	/* Wait until queue initialization is complete */
	for (i = 0; i < BGE_TIMEOUT; i++) {
		DELAY(10);
		if (CSR_READ_4(sc, BGE_FTQ_RESET) == 0)
			break;
	}

	if (i == BGE_TIMEOUT) {
		device_printf(sc->bge_dev, "flow-through queue init failed\n");
		return (ENXIO);
	}

	/*
	 * Summary of rings supported by the controller:
	 *
	 * Standard Receive Producer Ring
	 * - This ring is used to feed receive buffers for "standard"
	 *   sized frames (typically 1536 bytes) to the controller.
	 *
	 * Jumbo Receive Producer Ring
	 * - This ring is used to feed receive buffers for jumbo sized
	 *   frames (i.e. anything bigger than the "standard" frames)
	 *   to the controller.
	 *
	 * Mini Receive Producer Ring
	 * - This ring is used to feed receive buffers for "mini"
	 *   sized frames to the controller.
	 * - This feature required external memory for the controller
	 *   but was never used in a production system.  Should always
	 *   be disabled.
	 *
	 * Receive Return Ring
	 * - After the controller has placed an incoming frame into a
	 *   receive buffer that buffer is moved into a receive return
	 *   ring.  The driver is then responsible to passing the
	 *   buffer up to the stack.  Many versions of the controller
	 *   support multiple RR rings.
	 *
	 * Send Ring
	 * - This ring is used for outgoing frames.  Many versions of
	 *   the controller support multiple send rings.
	 */

	/* Initialize the standard receive producer ring control block. */
	rcb = &sc->bge_ldata.bge_info.bge_std_rx_rcb;
	rcb->bge_hostaddr.bge_addr_lo =
	    BGE_ADDR_LO(sc->bge_ldata.bge_rx_std_ring_paddr);
	rcb->bge_hostaddr.bge_addr_hi =
	    BGE_ADDR_HI(sc->bge_ldata.bge_rx_std_ring_paddr);
	bus_dmamap_sync(sc->bge_cdata.bge_rx_std_ring_tag,
	    sc->bge_cdata.bge_rx_std_ring_map, BUS_DMASYNC_PREREAD);
	if (BGE_IS_5717_PLUS(sc)) {
		/*
		 * Bits 31-16: Programmable ring size (2048, 1024, 512, .., 32)
		 * Bits 15-2 : Maximum RX frame size
		 * Bit 1     : 1 = Ring Disabled, 0 = Ring ENabled
		 * Bit 0     : Reserved
		 */
		rcb->bge_maxlen_flags =
		    BGE_RCB_MAXLEN_FLAGS(512, BGE_MAX_FRAMELEN << 2);
	} else if (BGE_IS_5705_PLUS(sc)) {
		/*
		 * Bits 31-16: Programmable ring size (512, 256, 128, 64, 32)
		 * Bits 15-2 : Reserved (should be 0)
		 * Bit 1     : 1 = Ring Disabled, 0 = Ring Enabled
		 * Bit 0     : Reserved
		 */
		rcb->bge_maxlen_flags = BGE_RCB_MAXLEN_FLAGS(512, 0);
	} else {
		/*
		 * Ring size is always XXX entries
		 * Bits 31-16: Maximum RX frame size
		 * Bits 15-2 : Reserved (should be 0)
		 * Bit 1     : 1 = Ring Disabled, 0 = Ring Enabled
		 * Bit 0     : Reserved
		 */
		rcb->bge_maxlen_flags =
		    BGE_RCB_MAXLEN_FLAGS(BGE_MAX_FRAMELEN, 0);
	}
	if (sc->bge_asicrev == BGE_ASICREV_BCM5717 ||
	    sc->bge_asicrev == BGE_ASICREV_BCM5719 ||
	    sc->bge_asicrev == BGE_ASICREV_BCM5720)
		rcb->bge_nicaddr = BGE_STD_RX_RINGS_5717;
	else
		rcb->bge_nicaddr = BGE_STD_RX_RINGS;
	/* Write the standard receive producer ring control block. */
	CSR_WRITE_4(sc, BGE_RX_STD_RCB_HADDR_HI, rcb->bge_hostaddr.bge_addr_hi);
	CSR_WRITE_4(sc, BGE_RX_STD_RCB_HADDR_LO, rcb->bge_hostaddr.bge_addr_lo);
	CSR_WRITE_4(sc, BGE_RX_STD_RCB_MAXLEN_FLAGS, rcb->bge_maxlen_flags);
	CSR_WRITE_4(sc, BGE_RX_STD_RCB_NICADDR, rcb->bge_nicaddr);

	/* Reset the standard receive producer ring producer index. */
	bge_writembx(sc, BGE_MBX_RX_STD_PROD_LO, 0);

	/*
	 * Initialize the jumbo RX producer ring control
	 * block.  We set the 'ring disabled' bit in the
	 * flags field until we're actually ready to start
	 * using this ring (i.e. once we set the MTU
	 * high enough to require it).
	 */
	if (BGE_IS_JUMBO_CAPABLE(sc)) {
		rcb = &sc->bge_ldata.bge_info.bge_jumbo_rx_rcb;
		/* Get the jumbo receive producer ring RCB parameters. */
		rcb->bge_hostaddr.bge_addr_lo =
		    BGE_ADDR_LO(sc->bge_ldata.bge_rx_jumbo_ring_paddr);
		rcb->bge_hostaddr.bge_addr_hi =
		    BGE_ADDR_HI(sc->bge_ldata.bge_rx_jumbo_ring_paddr);
		bus_dmamap_sync(sc->bge_cdata.bge_rx_jumbo_ring_tag,
		    sc->bge_cdata.bge_rx_jumbo_ring_map,
		    BUS_DMASYNC_PREREAD);
		rcb->bge_maxlen_flags = BGE_RCB_MAXLEN_FLAGS(0,
		    BGE_RCB_FLAG_USE_EXT_RX_BD | BGE_RCB_FLAG_RING_DISABLED);
		if (sc->bge_asicrev == BGE_ASICREV_BCM5717 ||
		    sc->bge_asicrev == BGE_ASICREV_BCM5719 ||
		    sc->bge_asicrev == BGE_ASICREV_BCM5720)
			rcb->bge_nicaddr = BGE_JUMBO_RX_RINGS_5717;
		else
			rcb->bge_nicaddr = BGE_JUMBO_RX_RINGS;
		CSR_WRITE_4(sc, BGE_RX_JUMBO_RCB_HADDR_HI,
		    rcb->bge_hostaddr.bge_addr_hi);
		CSR_WRITE_4(sc, BGE_RX_JUMBO_RCB_HADDR_LO,
		    rcb->bge_hostaddr.bge_addr_lo);
		/* Program the jumbo receive producer ring RCB parameters. */
		CSR_WRITE_4(sc, BGE_RX_JUMBO_RCB_MAXLEN_FLAGS,
		    rcb->bge_maxlen_flags);
		CSR_WRITE_4(sc, BGE_RX_JUMBO_RCB_NICADDR, rcb->bge_nicaddr);
		/* Reset the jumbo receive producer ring producer index. */
		bge_writembx(sc, BGE_MBX_RX_JUMBO_PROD_LO, 0);
	}

	/* Disable the mini receive producer ring RCB. */
	if (BGE_IS_5700_FAMILY(sc)) {
		rcb = &sc->bge_ldata.bge_info.bge_mini_rx_rcb;
		rcb->bge_maxlen_flags =
		    BGE_RCB_MAXLEN_FLAGS(0, BGE_RCB_FLAG_RING_DISABLED);
		CSR_WRITE_4(sc, BGE_RX_MINI_RCB_MAXLEN_FLAGS,
		    rcb->bge_maxlen_flags);
		/* Reset the mini receive producer ring producer index. */
		bge_writembx(sc, BGE_MBX_RX_MINI_PROD_LO, 0);
	}

	/* Choose de-pipeline mode for BCM5906 A0, A1 and A2. */
	if (sc->bge_asicrev == BGE_ASICREV_BCM5906) {
		if (sc->bge_chipid == BGE_CHIPID_BCM5906_A0 ||
		    sc->bge_chipid == BGE_CHIPID_BCM5906_A1 ||
		    sc->bge_chipid == BGE_CHIPID_BCM5906_A2)
			CSR_WRITE_4(sc, BGE_ISO_PKT_TX,
			    (CSR_READ_4(sc, BGE_ISO_PKT_TX) & ~3) | 2);
	}
	/*
	 * The BD ring replenish thresholds control how often the
	 * hardware fetches new BD's from the producer rings in host
	 * memory.  Setting the value too low on a busy system can
	 * starve the hardware and recue the throughpout.
	 *
	 * Set the BD ring replentish thresholds. The recommended
	 * values are 1/8th the number of descriptors allocated to
	 * each ring.
	 * XXX The 5754 requires a lower threshold, so it might be a
	 * requirement of all 575x family chips.  The Linux driver sets
	 * the lower threshold for all 5705 family chips as well, but there
	 * are reports that it might not need to be so strict.
	 *
	 * XXX Linux does some extra fiddling here for the 5906 parts as
	 * well.
	 */
	if (BGE_IS_5705_PLUS(sc))
		val = 8;
	else
		val = BGE_STD_RX_RING_CNT / 8;
	CSR_WRITE_4(sc, BGE_RBDI_STD_REPL_THRESH, val);
	if (BGE_IS_JUMBO_CAPABLE(sc))
		CSR_WRITE_4(sc, BGE_RBDI_JUMBO_REPL_THRESH,
		    BGE_JUMBO_RX_RING_CNT/8);
	if (BGE_IS_5717_PLUS(sc)) {
		CSR_WRITE_4(sc, BGE_STD_REPLENISH_LWM, 32);
		CSR_WRITE_4(sc, BGE_JMB_REPLENISH_LWM, 16);
	}

	/*
	 * Disable all send rings by setting the 'ring disabled' bit
	 * in the flags field of all the TX send ring control blocks,
	 * located in NIC memory.
	 */
	if (!BGE_IS_5705_PLUS(sc))
		/* 5700 to 5704 had 16 send rings. */
		limit = BGE_TX_RINGS_EXTSSRAM_MAX;
	else if (BGE_IS_57765_PLUS(sc) ||
	    sc->bge_asicrev == BGE_ASICREV_BCM5762)
		limit = 2;
	else if (BGE_IS_5717_PLUS(sc))
		limit = 4;
	else
		limit = 1;
	vrcb = BGE_MEMWIN_START + BGE_SEND_RING_RCB;
	for (i = 0; i < limit; i++) {
		RCB_WRITE_4(sc, vrcb, bge_maxlen_flags,
		    BGE_RCB_MAXLEN_FLAGS(0, BGE_RCB_FLAG_RING_DISABLED));
		RCB_WRITE_4(sc, vrcb, bge_nicaddr, 0);
		vrcb += sizeof(struct bge_rcb);
	}

	/* Configure send ring RCB 0 (we use only the first ring) */
	vrcb = BGE_MEMWIN_START + BGE_SEND_RING_RCB;
	BGE_HOSTADDR(taddr, sc->bge_ldata.bge_tx_ring_paddr);
	RCB_WRITE_4(sc, vrcb, bge_hostaddr.bge_addr_hi, taddr.bge_addr_hi);
	RCB_WRITE_4(sc, vrcb, bge_hostaddr.bge_addr_lo, taddr.bge_addr_lo);
	if (sc->bge_asicrev == BGE_ASICREV_BCM5717 ||
	    sc->bge_asicrev == BGE_ASICREV_BCM5719 ||
	    sc->bge_asicrev == BGE_ASICREV_BCM5720)
		RCB_WRITE_4(sc, vrcb, bge_nicaddr, BGE_SEND_RING_5717);
	else
		RCB_WRITE_4(sc, vrcb, bge_nicaddr,
		    BGE_NIC_TXRING_ADDR(0, BGE_TX_RING_CNT));
	RCB_WRITE_4(sc, vrcb, bge_maxlen_flags,
	    BGE_RCB_MAXLEN_FLAGS(BGE_TX_RING_CNT, 0));

	/*
	 * Disable all receive return rings by setting the
	 * 'ring diabled' bit in the flags field of all the receive
	 * return ring control blocks, located in NIC memory.
	 */
	if (sc->bge_asicrev == BGE_ASICREV_BCM5717 ||
	    sc->bge_asicrev == BGE_ASICREV_BCM5719 ||
	    sc->bge_asicrev == BGE_ASICREV_BCM5720) {
		/* Should be 17, use 16 until we get an SRAM map. */
		limit = 16;
	} else if (!BGE_IS_5705_PLUS(sc))
		limit = BGE_RX_RINGS_MAX;
	else if (sc->bge_asicrev == BGE_ASICREV_BCM5755 ||
	    sc->bge_asicrev == BGE_ASICREV_BCM5762 ||
	    BGE_IS_57765_PLUS(sc))
		limit = 4;
	else
		limit = 1;
	/* Disable all receive return rings. */
	vrcb = BGE_MEMWIN_START + BGE_RX_RETURN_RING_RCB;
	for (i = 0; i < limit; i++) {
		RCB_WRITE_4(sc, vrcb, bge_hostaddr.bge_addr_hi, 0);
		RCB_WRITE_4(sc, vrcb, bge_hostaddr.bge_addr_lo, 0);
		RCB_WRITE_4(sc, vrcb, bge_maxlen_flags,
		    BGE_RCB_FLAG_RING_DISABLED);
		RCB_WRITE_4(sc, vrcb, bge_nicaddr, 0);
		bge_writembx(sc, BGE_MBX_RX_CONS0_LO +
		    (i * (sizeof(uint64_t))), 0);
		vrcb += sizeof(struct bge_rcb);
	}

	/*
	 * Set up receive return ring 0.  Note that the NIC address
	 * for RX return rings is 0x0.  The return rings live entirely
	 * within the host, so the nicaddr field in the RCB isn't used.
	 */
	vrcb = BGE_MEMWIN_START + BGE_RX_RETURN_RING_RCB;
	BGE_HOSTADDR(taddr, sc->bge_ldata.bge_rx_return_ring_paddr);
	RCB_WRITE_4(sc, vrcb, bge_hostaddr.bge_addr_hi, taddr.bge_addr_hi);
	RCB_WRITE_4(sc, vrcb, bge_hostaddr.bge_addr_lo, taddr.bge_addr_lo);
	RCB_WRITE_4(sc, vrcb, bge_nicaddr, 0);
	RCB_WRITE_4(sc, vrcb, bge_maxlen_flags,
	    BGE_RCB_MAXLEN_FLAGS(sc->bge_return_ring_cnt, 0));

	/* Set random backoff seed for TX */
	CSR_WRITE_4(sc, BGE_TX_RANDOM_BACKOFF,
	    (IF_LLADDR(sc->bge_ifp)[0] + IF_LLADDR(sc->bge_ifp)[1] +
	    IF_LLADDR(sc->bge_ifp)[2] + IF_LLADDR(sc->bge_ifp)[3] +
	    IF_LLADDR(sc->bge_ifp)[4] + IF_LLADDR(sc->bge_ifp)[5]) &
	    BGE_TX_BACKOFF_SEED_MASK);

	/* Set inter-packet gap */
	val = 0x2620;
	if (sc->bge_asicrev == BGE_ASICREV_BCM5720 ||
	    sc->bge_asicrev == BGE_ASICREV_BCM5762)
		val |= CSR_READ_4(sc, BGE_TX_LENGTHS) &
		    (BGE_TXLEN_JMB_FRM_LEN_MSK | BGE_TXLEN_CNT_DN_VAL_MSK);
	CSR_WRITE_4(sc, BGE_TX_LENGTHS, val);

	/*
	 * Specify which ring to use for packets that don't match
	 * any RX rules.
	 */
	CSR_WRITE_4(sc, BGE_RX_RULES_CFG, 0x08);

	/*
	 * Configure number of RX lists. One interrupt distribution
	 * list, sixteen active lists, one bad frames class.
	 */
	CSR_WRITE_4(sc, BGE_RXLP_CFG, 0x181);

	/* Inialize RX list placement stats mask. */
	CSR_WRITE_4(sc, BGE_RXLP_STATS_ENABLE_MASK, 0x007FFFFF);
	CSR_WRITE_4(sc, BGE_RXLP_STATS_CTL, 0x1);

	/* Disable host coalescing until we get it set up */
	CSR_WRITE_4(sc, BGE_HCC_MODE, 0x00000000);

	/* Poll to make sure it's shut down. */
	for (i = 0; i < BGE_TIMEOUT; i++) {
		DELAY(10);
		if (!(CSR_READ_4(sc, BGE_HCC_MODE) & BGE_HCCMODE_ENABLE))
			break;
	}

	if (i == BGE_TIMEOUT) {
		device_printf(sc->bge_dev,
		    "host coalescing engine failed to idle\n");
		return (ENXIO);
	}

	/* Set up host coalescing defaults */
	CSR_WRITE_4(sc, BGE_HCC_RX_COAL_TICKS, sc->bge_rx_coal_ticks);
	CSR_WRITE_4(sc, BGE_HCC_TX_COAL_TICKS, sc->bge_tx_coal_ticks);
	CSR_WRITE_4(sc, BGE_HCC_RX_MAX_COAL_BDS, sc->bge_rx_max_coal_bds);
	CSR_WRITE_4(sc, BGE_HCC_TX_MAX_COAL_BDS, sc->bge_tx_max_coal_bds);
	if (!(BGE_IS_5705_PLUS(sc))) {
		CSR_WRITE_4(sc, BGE_HCC_RX_COAL_TICKS_INT, 0);
		CSR_WRITE_4(sc, BGE_HCC_TX_COAL_TICKS_INT, 0);
	}
	CSR_WRITE_4(sc, BGE_HCC_RX_MAX_COAL_BDS_INT, 1);
	CSR_WRITE_4(sc, BGE_HCC_TX_MAX_COAL_BDS_INT, 1);

	/* Set up address of statistics block */
	if (!(BGE_IS_5705_PLUS(sc))) {
		CSR_WRITE_4(sc, BGE_HCC_STATS_ADDR_HI,
		    BGE_ADDR_HI(sc->bge_ldata.bge_stats_paddr));
		CSR_WRITE_4(sc, BGE_HCC_STATS_ADDR_LO,
		    BGE_ADDR_LO(sc->bge_ldata.bge_stats_paddr));
		CSR_WRITE_4(sc, BGE_HCC_STATS_BASEADDR, BGE_STATS_BLOCK);
		CSR_WRITE_4(sc, BGE_HCC_STATUSBLK_BASEADDR, BGE_STATUS_BLOCK);
		CSR_WRITE_4(sc, BGE_HCC_STATS_TICKS, sc->bge_stat_ticks);
	}

	/* Set up address of status block */
	CSR_WRITE_4(sc, BGE_HCC_STATUSBLK_ADDR_HI,
	    BGE_ADDR_HI(sc->bge_ldata.bge_status_block_paddr));
	CSR_WRITE_4(sc, BGE_HCC_STATUSBLK_ADDR_LO,
	    BGE_ADDR_LO(sc->bge_ldata.bge_status_block_paddr));

	/* Set up status block size. */
	if (sc->bge_asicrev == BGE_ASICREV_BCM5700 &&
	    sc->bge_chipid != BGE_CHIPID_BCM5700_C0) {
		val = BGE_STATBLKSZ_FULL;
		bzero(sc->bge_ldata.bge_status_block, BGE_STATUS_BLK_SZ);
	} else {
		val = BGE_STATBLKSZ_32BYTE;
		bzero(sc->bge_ldata.bge_status_block, 32);
	}
	bus_dmamap_sync(sc->bge_cdata.bge_status_tag,
	    sc->bge_cdata.bge_status_map,
	    BUS_DMASYNC_PREREAD | BUS_DMASYNC_PREWRITE);

	/* Turn on host coalescing state machine */
	CSR_WRITE_4(sc, BGE_HCC_MODE, val | BGE_HCCMODE_ENABLE);

	/* Turn on RX BD completion state machine and enable attentions */
	CSR_WRITE_4(sc, BGE_RBDC_MODE,
	    BGE_RBDCMODE_ENABLE | BGE_RBDCMODE_ATTN);

	/* Turn on RX list placement state machine */
	CSR_WRITE_4(sc, BGE_RXLP_MODE, BGE_RXLPMODE_ENABLE);

	/* Turn on RX list selector state machine. */
	if (!(BGE_IS_5705_PLUS(sc)))
		CSR_WRITE_4(sc, BGE_RXLS_MODE, BGE_RXLSMODE_ENABLE);

	/* Turn on DMA, clear stats. */
	val = BGE_MACMODE_TXDMA_ENB | BGE_MACMODE_RXDMA_ENB |
	    BGE_MACMODE_RX_STATS_CLEAR | BGE_MACMODE_TX_STATS_CLEAR |
	    BGE_MACMODE_RX_STATS_ENB | BGE_MACMODE_TX_STATS_ENB |
	    BGE_MACMODE_FRMHDR_DMA_ENB;

	if (sc->bge_flags & BGE_FLAG_TBI)
		val |= BGE_PORTMODE_TBI;
	else if (sc->bge_flags & BGE_FLAG_MII_SERDES)
		val |= BGE_PORTMODE_GMII;
	else
		val |= BGE_PORTMODE_MII;

	/* Allow APE to send/receive frames. */
	if ((sc->bge_mfw_flags & BGE_MFW_ON_APE) != 0)
		val |= BGE_MACMODE_APE_RX_EN | BGE_MACMODE_APE_TX_EN;

	CSR_WRITE_4(sc, BGE_MAC_MODE, val);
	DELAY(40);

	/* Set misc. local control, enable interrupts on attentions */
	BGE_SETBIT(sc, BGE_MISC_LOCAL_CTL, BGE_MLC_INTR_ONATTN);

#ifdef notdef
	/* Assert GPIO pins for PHY reset */
	BGE_SETBIT(sc, BGE_MISC_LOCAL_CTL, BGE_MLC_MISCIO_OUT0 |
	    BGE_MLC_MISCIO_OUT1 | BGE_MLC_MISCIO_OUT2);
	BGE_SETBIT(sc, BGE_MISC_LOCAL_CTL, BGE_MLC_MISCIO_OUTEN0 |
	    BGE_MLC_MISCIO_OUTEN1 | BGE_MLC_MISCIO_OUTEN2);
#endif

	/* Turn on DMA completion state machine */
	if (!(BGE_IS_5705_PLUS(sc)))
		CSR_WRITE_4(sc, BGE_DMAC_MODE, BGE_DMACMODE_ENABLE);

	val = BGE_WDMAMODE_ENABLE | BGE_WDMAMODE_ALL_ATTNS;

	/* Enable host coalescing bug fix. */
	if (BGE_IS_5755_PLUS(sc))
		val |= BGE_WDMAMODE_STATUS_TAG_FIX;

	/* Request larger DMA burst size to get better performance. */
	if (sc->bge_asicrev == BGE_ASICREV_BCM5785)
		val |= BGE_WDMAMODE_BURST_ALL_DATA;

	/* Turn on write DMA state machine */
	CSR_WRITE_4(sc, BGE_WDMA_MODE, val);
	DELAY(40);

	/* Turn on read DMA state machine */
	val = BGE_RDMAMODE_ENABLE | BGE_RDMAMODE_ALL_ATTNS;

	if (sc->bge_asicrev == BGE_ASICREV_BCM5717)
		val |= BGE_RDMAMODE_MULT_DMA_RD_DIS;

	if (sc->bge_asicrev == BGE_ASICREV_BCM5784 ||
	    sc->bge_asicrev == BGE_ASICREV_BCM5785 ||
	    sc->bge_asicrev == BGE_ASICREV_BCM57780)
		val |= BGE_RDMAMODE_BD_SBD_CRPT_ATTN |
		    BGE_RDMAMODE_MBUF_RBD_CRPT_ATTN |
		    BGE_RDMAMODE_MBUF_SBD_CRPT_ATTN;
	if (sc->bge_flags & BGE_FLAG_PCIE)
		val |= BGE_RDMAMODE_FIFO_LONG_BURST;
	if (sc->bge_flags & (BGE_FLAG_TSO | BGE_FLAG_TSO3)) {
		val |= BGE_RDMAMODE_TSO4_ENABLE;
		if (sc->bge_flags & BGE_FLAG_TSO3 ||
		    sc->bge_asicrev == BGE_ASICREV_BCM5785 ||
		    sc->bge_asicrev == BGE_ASICREV_BCM57780)
			val |= BGE_RDMAMODE_TSO6_ENABLE;
	}

	if (sc->bge_asicrev == BGE_ASICREV_BCM5720 ||
	    sc->bge_asicrev == BGE_ASICREV_BCM5762) {
		val |= CSR_READ_4(sc, BGE_RDMA_MODE) &
			BGE_RDMAMODE_H2BNC_VLAN_DET;
		/*
		 * Allow multiple outstanding read requests from
		 * non-LSO read DMA engine.
		 */
		val &= ~BGE_RDMAMODE_MULT_DMA_RD_DIS;
	}

	if (sc->bge_asicrev == BGE_ASICREV_BCM5761 ||
	    sc->bge_asicrev == BGE_ASICREV_BCM5784 ||
	    sc->bge_asicrev == BGE_ASICREV_BCM5785 ||
	    sc->bge_asicrev == BGE_ASICREV_BCM57780 ||
	    BGE_IS_5717_PLUS(sc) || BGE_IS_57765_PLUS(sc)) {
		if (sc->bge_asicrev == BGE_ASICREV_BCM5762)
			rdmareg = BGE_RDMA_RSRVCTRL_REG2;
		else
			rdmareg = BGE_RDMA_RSRVCTRL;
		dmactl = CSR_READ_4(sc, rdmareg);
		/*
		 * Adjust tx margin to prevent TX data corruption and
		 * fix internal FIFO overflow.
		 */
		if (sc->bge_chipid == BGE_CHIPID_BCM5719_A0 ||
		    sc->bge_asicrev == BGE_ASICREV_BCM5762) {
			dmactl &= ~(BGE_RDMA_RSRVCTRL_FIFO_LWM_MASK |
			    BGE_RDMA_RSRVCTRL_FIFO_HWM_MASK |
			    BGE_RDMA_RSRVCTRL_TXMRGN_MASK);
			dmactl |= BGE_RDMA_RSRVCTRL_FIFO_LWM_1_5K |
			    BGE_RDMA_RSRVCTRL_FIFO_HWM_1_5K |
			    BGE_RDMA_RSRVCTRL_TXMRGN_320B;
		}
		/*
		 * Enable fix for read DMA FIFO overruns.
		 * The fix is to limit the number of RX BDs
		 * the hardware would fetch at a fime.
		 */
		CSR_WRITE_4(sc, rdmareg, dmactl |
		    BGE_RDMA_RSRVCTRL_FIFO_OFLW_FIX);
	}

	if (sc->bge_asicrev == BGE_ASICREV_BCM5719) {
		CSR_WRITE_4(sc, BGE_RDMA_LSO_CRPTEN_CTRL,
		    CSR_READ_4(sc, BGE_RDMA_LSO_CRPTEN_CTRL) |
		    BGE_RDMA_LSO_CRPTEN_CTRL_BLEN_BD_4K |
		    BGE_RDMA_LSO_CRPTEN_CTRL_BLEN_LSO_4K);
	} else if (sc->bge_asicrev == BGE_ASICREV_BCM5720) {
		/*
		 * Allow 4KB burst length reads for non-LSO frames.
		 * Enable 512B burst length reads for buffer descriptors.
		 */
		CSR_WRITE_4(sc, BGE_RDMA_LSO_CRPTEN_CTRL,
		    CSR_READ_4(sc, BGE_RDMA_LSO_CRPTEN_CTRL) |
		    BGE_RDMA_LSO_CRPTEN_CTRL_BLEN_BD_512 |
		    BGE_RDMA_LSO_CRPTEN_CTRL_BLEN_LSO_4K);
	} else if (sc->bge_asicrev == BGE_ASICREV_BCM5762) {
		CSR_WRITE_4(sc, BGE_RDMA_LSO_CRPTEN_CTRL_REG2,
		    CSR_READ_4(sc, BGE_RDMA_LSO_CRPTEN_CTRL_REG2) |
		    BGE_RDMA_LSO_CRPTEN_CTRL_BLEN_BD_4K |
		    BGE_RDMA_LSO_CRPTEN_CTRL_BLEN_LSO_4K);
	}

	CSR_WRITE_4(sc, BGE_RDMA_MODE, val);
	DELAY(40);

	if (sc->bge_flags & BGE_FLAG_RDMA_BUG) {
		for (i = 0; i < BGE_NUM_RDMA_CHANNELS / 2; i++) {
			val = CSR_READ_4(sc, BGE_RDMA_LENGTH + i * 4);
			if ((val & 0xFFFF) > BGE_FRAMELEN)
				break;
			if (((val >> 16) & 0xFFFF) > BGE_FRAMELEN)
				break;
		}
		if (i != BGE_NUM_RDMA_CHANNELS / 2) {
			val = CSR_READ_4(sc, BGE_RDMA_LSO_CRPTEN_CTRL);
			if (sc->bge_asicrev == BGE_ASICREV_BCM5719)
				val |= BGE_RDMA_TX_LENGTH_WA_5719;
			else
				val |= BGE_RDMA_TX_LENGTH_WA_5720;
			CSR_WRITE_4(sc, BGE_RDMA_LSO_CRPTEN_CTRL, val);
		}
	}

	/* Turn on RX data completion state machine */
	CSR_WRITE_4(sc, BGE_RDC_MODE, BGE_RDCMODE_ENABLE);

	/* Turn on RX BD initiator state machine */
	CSR_WRITE_4(sc, BGE_RBDI_MODE, BGE_RBDIMODE_ENABLE);

	/* Turn on RX data and RX BD initiator state machine */
	CSR_WRITE_4(sc, BGE_RDBDI_MODE, BGE_RDBDIMODE_ENABLE);

	/* Turn on Mbuf cluster free state machine */
	if (!(BGE_IS_5705_PLUS(sc)))
		CSR_WRITE_4(sc, BGE_MBCF_MODE, BGE_MBCFMODE_ENABLE);

	/* Turn on send BD completion state machine */
	CSR_WRITE_4(sc, BGE_SBDC_MODE, BGE_SBDCMODE_ENABLE);

	/* Turn on send data completion state machine */
	val = BGE_SDCMODE_ENABLE;
	if (sc->bge_asicrev == BGE_ASICREV_BCM5761)
		val |= BGE_SDCMODE_CDELAY;
	CSR_WRITE_4(sc, BGE_SDC_MODE, val);

	/* Turn on send data initiator state machine */
	if (sc->bge_flags & (BGE_FLAG_TSO | BGE_FLAG_TSO3))
		CSR_WRITE_4(sc, BGE_SDI_MODE, BGE_SDIMODE_ENABLE |
		    BGE_SDIMODE_HW_LSO_PRE_DMA);
	else
		CSR_WRITE_4(sc, BGE_SDI_MODE, BGE_SDIMODE_ENABLE);

	/* Turn on send BD initiator state machine */
	CSR_WRITE_4(sc, BGE_SBDI_MODE, BGE_SBDIMODE_ENABLE);

	/* Turn on send BD selector state machine */
	CSR_WRITE_4(sc, BGE_SRS_MODE, BGE_SRSMODE_ENABLE);

	CSR_WRITE_4(sc, BGE_SDI_STATS_ENABLE_MASK, 0x007FFFFF);
	CSR_WRITE_4(sc, BGE_SDI_STATS_CTL,
	    BGE_SDISTATSCTL_ENABLE | BGE_SDISTATSCTL_FASTER);

	/* ack/clear link change events */
	CSR_WRITE_4(sc, BGE_MAC_STS, BGE_MACSTAT_SYNC_CHANGED |
	    BGE_MACSTAT_CFG_CHANGED | BGE_MACSTAT_MI_COMPLETE |
	    BGE_MACSTAT_LINK_CHANGED);
	CSR_WRITE_4(sc, BGE_MI_STS, 0);

	/*
	 * Enable attention when the link has changed state for
	 * devices that use auto polling.
	 */
	if (sc->bge_flags & BGE_FLAG_TBI) {
		CSR_WRITE_4(sc, BGE_MI_STS, BGE_MISTS_LINK);
	} else {
		if (sc->bge_mi_mode & BGE_MIMODE_AUTOPOLL) {
			CSR_WRITE_4(sc, BGE_MI_MODE, sc->bge_mi_mode);
			DELAY(80);
		}
		if (sc->bge_asicrev == BGE_ASICREV_BCM5700 &&
		    sc->bge_chipid != BGE_CHIPID_BCM5700_B2)
			CSR_WRITE_4(sc, BGE_MAC_EVT_ENB,
			    BGE_EVTENB_MI_INTERRUPT);
	}

	/*
	 * Clear any pending link state attention.
	 * Otherwise some link state change events may be lost until attention
	 * is cleared by bge_intr() -> bge_link_upd() sequence.
	 * It's not necessary on newer BCM chips - perhaps enabling link
	 * state change attentions implies clearing pending attention.
	 */
	CSR_WRITE_4(sc, BGE_MAC_STS, BGE_MACSTAT_SYNC_CHANGED |
	    BGE_MACSTAT_CFG_CHANGED | BGE_MACSTAT_MI_COMPLETE |
	    BGE_MACSTAT_LINK_CHANGED);

	/* Enable link state change attentions. */
	BGE_SETBIT(sc, BGE_MAC_EVT_ENB, BGE_EVTENB_LINK_CHANGED);

	return (0);
}

static const struct bge_revision *
bge_lookup_rev(uint32_t chipid)
{
	const struct bge_revision *br;

	for (br = bge_revisions; br->br_name != NULL; br++) {
		if (br->br_chipid == chipid)
			return (br);
	}

	for (br = bge_majorrevs; br->br_name != NULL; br++) {
		if (br->br_chipid == BGE_ASICREV(chipid))
			return (br);
	}

	return (NULL);
}

static const struct bge_vendor *
bge_lookup_vendor(uint16_t vid)
{
	const struct bge_vendor *v;

	for (v = bge_vendors; v->v_name != NULL; v++)
		if (v->v_id == vid)
			return (v);

	return (NULL);
}

static uint32_t
bge_chipid(device_t dev)
{
	uint32_t id;

	id = pci_read_config(dev, BGE_PCI_MISC_CTL, 4) >>
	    BGE_PCIMISCCTL_ASICREV_SHIFT;
	if (BGE_ASICREV(id) == BGE_ASICREV_USE_PRODID_REG) {
		/*
		 * Find the ASCI revision.  Different chips use different
		 * registers.
		 */
		switch (pci_get_device(dev)) {
		case BCOM_DEVICEID_BCM5717C:
			/* 5717 C0 seems to belong to 5720 line. */
			id = BGE_CHIPID_BCM5720_A0;
			break;
		case BCOM_DEVICEID_BCM5717:
		case BCOM_DEVICEID_BCM5718:
		case BCOM_DEVICEID_BCM5719:
		case BCOM_DEVICEID_BCM5720:
		case BCOM_DEVICEID_BCM5725:
		case BCOM_DEVICEID_BCM5727:
		case BCOM_DEVICEID_BCM5762:
		case BCOM_DEVICEID_BCM57764:
		case BCOM_DEVICEID_BCM57767:
		case BCOM_DEVICEID_BCM57787:
			id = pci_read_config(dev,
			    BGE_PCI_GEN2_PRODID_ASICREV, 4);
			break;
		case BCOM_DEVICEID_BCM57761:
		case BCOM_DEVICEID_BCM57762:
		case BCOM_DEVICEID_BCM57765:
		case BCOM_DEVICEID_BCM57766:
		case BCOM_DEVICEID_BCM57781:
		case BCOM_DEVICEID_BCM57782:
		case BCOM_DEVICEID_BCM57785:
		case BCOM_DEVICEID_BCM57786:
		case BCOM_DEVICEID_BCM57791:
		case BCOM_DEVICEID_BCM57795:
			id = pci_read_config(dev,
			    BGE_PCI_GEN15_PRODID_ASICREV, 4);
			break;
		default:
			id = pci_read_config(dev, BGE_PCI_PRODID_ASICREV, 4);
		}
	}
	return (id);
}

/*
 * Probe for a Broadcom chip. Check the PCI vendor and device IDs
 * against our list and return its name if we find a match.
 *
 * Note that since the Broadcom controller contains VPD support, we
 * try to get the device name string from the controller itself instead
 * of the compiled-in string. It guarantees we'll always announce the
 * right product name. We fall back to the compiled-in string when
 * VPD is unavailable or corrupt.
 */
static int
bge_probe(device_t dev)
{
	char buf[96];
	char model[64];
	const struct bge_revision *br;
	const char *pname;
	struct bge_softc *sc;
	const struct bge_type *t = bge_devs;
	const struct bge_vendor *v;
	uint32_t id;
	uint16_t did, vid;

	sc = device_get_softc(dev);
	sc->bge_dev = dev;
	vid = pci_get_vendor(dev);
	did = pci_get_device(dev);
	while(t->bge_vid != 0) {
		if ((vid == t->bge_vid) && (did == t->bge_did)) {
			id = bge_chipid(dev);
			br = bge_lookup_rev(id);
			if (bge_has_eaddr(sc) &&
			    pci_get_vpd_ident(dev, &pname) == 0)
				snprintf(model, sizeof(model), "%s", pname);
			else {
				v = bge_lookup_vendor(vid);
				snprintf(model, sizeof(model), "%s %s",
				    v != NULL ? v->v_name : "Unknown",
				    br != NULL ? br->br_name :
				    "NetXtreme/NetLink Ethernet Controller");
			}
			snprintf(buf, sizeof(buf), "%s, %sASIC rev. %#08x",
			    model, br != NULL ? "" : "unknown ", id);
			device_set_desc_copy(dev, buf);
			return (BUS_PROBE_DEFAULT);
		}
		t++;
	}

	return (ENXIO);
}

static void
bge_dma_free(struct bge_softc *sc)
{
	int i;

	/* Destroy DMA maps for RX buffers. */
	for (i = 0; i < BGE_STD_RX_RING_CNT; i++) {
		if (sc->bge_cdata.bge_rx_std_dmamap[i])
			bus_dmamap_destroy(sc->bge_cdata.bge_rx_mtag,
			    sc->bge_cdata.bge_rx_std_dmamap[i]);
	}
	if (sc->bge_cdata.bge_rx_std_sparemap)
		bus_dmamap_destroy(sc->bge_cdata.bge_rx_mtag,
		    sc->bge_cdata.bge_rx_std_sparemap);

	/* Destroy DMA maps for jumbo RX buffers. */
	for (i = 0; i < BGE_JUMBO_RX_RING_CNT; i++) {
		if (sc->bge_cdata.bge_rx_jumbo_dmamap[i])
			bus_dmamap_destroy(sc->bge_cdata.bge_mtag_jumbo,
			    sc->bge_cdata.bge_rx_jumbo_dmamap[i]);
	}
	if (sc->bge_cdata.bge_rx_jumbo_sparemap)
		bus_dmamap_destroy(sc->bge_cdata.bge_mtag_jumbo,
		    sc->bge_cdata.bge_rx_jumbo_sparemap);

	/* Destroy DMA maps for TX buffers. */
	for (i = 0; i < BGE_TX_RING_CNT; i++) {
		if (sc->bge_cdata.bge_tx_dmamap[i])
			bus_dmamap_destroy(sc->bge_cdata.bge_tx_mtag,
			    sc->bge_cdata.bge_tx_dmamap[i]);
	}

	if (sc->bge_cdata.bge_rx_mtag)
		bus_dma_tag_destroy(sc->bge_cdata.bge_rx_mtag);
	if (sc->bge_cdata.bge_mtag_jumbo)
		bus_dma_tag_destroy(sc->bge_cdata.bge_mtag_jumbo);
	if (sc->bge_cdata.bge_tx_mtag)
		bus_dma_tag_destroy(sc->bge_cdata.bge_tx_mtag);

	/* Destroy standard RX ring. */
	if (sc->bge_ldata.bge_rx_std_ring_paddr)
		bus_dmamap_unload(sc->bge_cdata.bge_rx_std_ring_tag,
		    sc->bge_cdata.bge_rx_std_ring_map);
	if (sc->bge_ldata.bge_rx_std_ring)
		bus_dmamem_free(sc->bge_cdata.bge_rx_std_ring_tag,
		    sc->bge_ldata.bge_rx_std_ring,
		    sc->bge_cdata.bge_rx_std_ring_map);

	if (sc->bge_cdata.bge_rx_std_ring_tag)
		bus_dma_tag_destroy(sc->bge_cdata.bge_rx_std_ring_tag);

	/* Destroy jumbo RX ring. */
	if (sc->bge_ldata.bge_rx_jumbo_ring_paddr)
		bus_dmamap_unload(sc->bge_cdata.bge_rx_jumbo_ring_tag,
		    sc->bge_cdata.bge_rx_jumbo_ring_map);

	if (sc->bge_ldata.bge_rx_jumbo_ring)
		bus_dmamem_free(sc->bge_cdata.bge_rx_jumbo_ring_tag,
		    sc->bge_ldata.bge_rx_jumbo_ring,
		    sc->bge_cdata.bge_rx_jumbo_ring_map);

	if (sc->bge_cdata.bge_rx_jumbo_ring_tag)
		bus_dma_tag_destroy(sc->bge_cdata.bge_rx_jumbo_ring_tag);

	/* Destroy RX return ring. */
	if (sc->bge_ldata.bge_rx_return_ring_paddr)
		bus_dmamap_unload(sc->bge_cdata.bge_rx_return_ring_tag,
		    sc->bge_cdata.bge_rx_return_ring_map);

	if (sc->bge_ldata.bge_rx_return_ring)
		bus_dmamem_free(sc->bge_cdata.bge_rx_return_ring_tag,
		    sc->bge_ldata.bge_rx_return_ring,
		    sc->bge_cdata.bge_rx_return_ring_map);

	if (sc->bge_cdata.bge_rx_return_ring_tag)
		bus_dma_tag_destroy(sc->bge_cdata.bge_rx_return_ring_tag);

	/* Destroy TX ring. */
	if (sc->bge_ldata.bge_tx_ring_paddr)
		bus_dmamap_unload(sc->bge_cdata.bge_tx_ring_tag,
		    sc->bge_cdata.bge_tx_ring_map);

	if (sc->bge_ldata.bge_tx_ring)
		bus_dmamem_free(sc->bge_cdata.bge_tx_ring_tag,
		    sc->bge_ldata.bge_tx_ring,
		    sc->bge_cdata.bge_tx_ring_map);

	if (sc->bge_cdata.bge_tx_ring_tag)
		bus_dma_tag_destroy(sc->bge_cdata.bge_tx_ring_tag);

	/* Destroy status block. */
	if (sc->bge_ldata.bge_status_block_paddr)
		bus_dmamap_unload(sc->bge_cdata.bge_status_tag,
		    sc->bge_cdata.bge_status_map);

	if (sc->bge_ldata.bge_status_block)
		bus_dmamem_free(sc->bge_cdata.bge_status_tag,
		    sc->bge_ldata.bge_status_block,
		    sc->bge_cdata.bge_status_map);

	if (sc->bge_cdata.bge_status_tag)
		bus_dma_tag_destroy(sc->bge_cdata.bge_status_tag);

	/* Destroy statistics block. */
	if (sc->bge_ldata.bge_stats_paddr)
		bus_dmamap_unload(sc->bge_cdata.bge_stats_tag,
		    sc->bge_cdata.bge_stats_map);

	if (sc->bge_ldata.bge_stats)
		bus_dmamem_free(sc->bge_cdata.bge_stats_tag,
		    sc->bge_ldata.bge_stats,
		    sc->bge_cdata.bge_stats_map);

	if (sc->bge_cdata.bge_stats_tag)
		bus_dma_tag_destroy(sc->bge_cdata.bge_stats_tag);

	if (sc->bge_cdata.bge_buffer_tag)
		bus_dma_tag_destroy(sc->bge_cdata.bge_buffer_tag);

	/* Destroy the parent tag. */
	if (sc->bge_cdata.bge_parent_tag)
		bus_dma_tag_destroy(sc->bge_cdata.bge_parent_tag);
}

static int
bge_dma_ring_alloc(struct bge_softc *sc, bus_size_t alignment,
    bus_size_t maxsize, bus_dma_tag_t *tag, uint8_t **ring, bus_dmamap_t *map,
    bus_addr_t *paddr, const char *msg)
{
	struct bge_dmamap_arg ctx;
	int error;

	error = bus_dma_tag_create(sc->bge_cdata.bge_parent_tag,
	    alignment, 0, BUS_SPACE_MAXADDR, BUS_SPACE_MAXADDR, NULL,
	    NULL, maxsize, 1, maxsize, 0, NULL, NULL, tag);
	if (error != 0) {
		device_printf(sc->bge_dev,
		    "could not create %s dma tag\n", msg);
		return (ENOMEM);
	}
	/* Allocate DMA'able memory for ring. */
	error = bus_dmamem_alloc(*tag, (void **)ring,
	    BUS_DMA_NOWAIT | BUS_DMA_ZERO | BUS_DMA_COHERENT, map);
	if (error != 0) {
		device_printf(sc->bge_dev,
		    "could not allocate DMA'able memory for %s\n", msg);
		return (ENOMEM);
	}
	/* Load the address of the ring. */
	ctx.bge_busaddr = 0;
	error = bus_dmamap_load(*tag, *map, *ring, maxsize, bge_dma_map_addr,
	    &ctx, BUS_DMA_NOWAIT);
	if (error != 0) {
		device_printf(sc->bge_dev,
		    "could not load DMA'able memory for %s\n", msg);
		return (ENOMEM);
	}
	*paddr = ctx.bge_busaddr;
	return (0);
}

static int
bge_dma_alloc(struct bge_softc *sc)
{
	bus_addr_t lowaddr;
	bus_size_t rxmaxsegsz, sbsz, txsegsz, txmaxsegsz;
	int i, error;

	lowaddr = BUS_SPACE_MAXADDR;
	if ((sc->bge_flags & BGE_FLAG_40BIT_BUG) != 0)
		lowaddr = BGE_DMA_MAXADDR;
	/*
	 * Allocate the parent bus DMA tag appropriate for PCI.
	 */
	error = bus_dma_tag_create(bus_get_dma_tag(sc->bge_dev),
	    1, 0, lowaddr, BUS_SPACE_MAXADDR, NULL,
	    NULL, BUS_SPACE_MAXSIZE_32BIT, 0, BUS_SPACE_MAXSIZE_32BIT,
	    0, NULL, NULL, &sc->bge_cdata.bge_parent_tag);
	if (error != 0) {
		device_printf(sc->bge_dev,
		    "could not allocate parent dma tag\n");
		return (ENOMEM);
	}

	/* Create tag for standard RX ring. */
	error = bge_dma_ring_alloc(sc, PAGE_SIZE, BGE_STD_RX_RING_SZ,
	    &sc->bge_cdata.bge_rx_std_ring_tag,
	    (uint8_t **)&sc->bge_ldata.bge_rx_std_ring,
	    &sc->bge_cdata.bge_rx_std_ring_map,
	    &sc->bge_ldata.bge_rx_std_ring_paddr, "RX ring");
	if (error)
		return (error);

	/* Create tag for RX return ring. */
	error = bge_dma_ring_alloc(sc, PAGE_SIZE, BGE_RX_RTN_RING_SZ(sc),
	    &sc->bge_cdata.bge_rx_return_ring_tag,
	    (uint8_t **)&sc->bge_ldata.bge_rx_return_ring,
	    &sc->bge_cdata.bge_rx_return_ring_map,
	    &sc->bge_ldata.bge_rx_return_ring_paddr, "RX return ring");
	if (error)
		return (error);

	/* Create tag for TX ring. */
	error = bge_dma_ring_alloc(sc, PAGE_SIZE, BGE_TX_RING_SZ,
	    &sc->bge_cdata.bge_tx_ring_tag,
	    (uint8_t **)&sc->bge_ldata.bge_tx_ring,
	    &sc->bge_cdata.bge_tx_ring_map,
	    &sc->bge_ldata.bge_tx_ring_paddr, "TX ring");
	if (error)
		return (error);

	/*
	 * Create tag for status block.
	 * Because we only use single Tx/Rx/Rx return ring, use
	 * minimum status block size except BCM5700 AX/BX which
	 * seems to want to see full status block size regardless
	 * of configured number of ring.
	 */
	if (sc->bge_asicrev == BGE_ASICREV_BCM5700 &&
	    sc->bge_chipid != BGE_CHIPID_BCM5700_C0)
		sbsz = BGE_STATUS_BLK_SZ;
	else
		sbsz = 32;
	error = bge_dma_ring_alloc(sc, PAGE_SIZE, sbsz,
	    &sc->bge_cdata.bge_status_tag,
	    (uint8_t **)&sc->bge_ldata.bge_status_block,
	    &sc->bge_cdata.bge_status_map,
	    &sc->bge_ldata.bge_status_block_paddr, "status block");
	if (error)
		return (error);

	/* Create tag for statistics block. */
	error = bge_dma_ring_alloc(sc, PAGE_SIZE, BGE_STATS_SZ,
	    &sc->bge_cdata.bge_stats_tag,
	    (uint8_t **)&sc->bge_ldata.bge_stats,
	    &sc->bge_cdata.bge_stats_map,
	    &sc->bge_ldata.bge_stats_paddr, "statistics block");
	if (error)
		return (error);

	/* Create tag for jumbo RX ring. */
	if (BGE_IS_JUMBO_CAPABLE(sc)) {
		error = bge_dma_ring_alloc(sc, PAGE_SIZE, BGE_JUMBO_RX_RING_SZ,
		    &sc->bge_cdata.bge_rx_jumbo_ring_tag,
		    (uint8_t **)&sc->bge_ldata.bge_rx_jumbo_ring,
		    &sc->bge_cdata.bge_rx_jumbo_ring_map,
		    &sc->bge_ldata.bge_rx_jumbo_ring_paddr, "jumbo RX ring");
		if (error)
			return (error);
	}

	/* Create parent tag for buffers. */
	if ((sc->bge_flags & BGE_FLAG_4G_BNDRY_BUG) != 0) {
		/*
		 * XXX
		 * watchdog timeout issue was observed on BCM5704 which
		 * lives behind PCI-X bridge(e.g AMD 8131 PCI-X bridge).
		 * Both limiting DMA address space to 32bits and flushing
		 * mailbox write seem to address the issue.
		 */
		if (sc->bge_pcixcap != 0)
			lowaddr = BUS_SPACE_MAXADDR_32BIT;
	}
	error = bus_dma_tag_create(bus_get_dma_tag(sc->bge_dev), 1, 0, lowaddr,
	    BUS_SPACE_MAXADDR, NULL, NULL, BUS_SPACE_MAXSIZE_32BIT, 0,
	    BUS_SPACE_MAXSIZE_32BIT, 0, NULL, NULL,
	    &sc->bge_cdata.bge_buffer_tag);
	if (error != 0) {
		device_printf(sc->bge_dev,
		    "could not allocate buffer dma tag\n");
		return (ENOMEM);
	}
	/* Create tag for Tx mbufs. */
	if (sc->bge_flags & (BGE_FLAG_TSO | BGE_FLAG_TSO3)) {
		txsegsz = BGE_TSOSEG_SZ;
		txmaxsegsz = 65535 + sizeof(struct ether_vlan_header);
	} else {
		txsegsz = MCLBYTES;
		txmaxsegsz = MCLBYTES * BGE_NSEG_NEW;
	}
	error = bus_dma_tag_create(sc->bge_cdata.bge_buffer_tag, 1,
	    0, BUS_SPACE_MAXADDR, BUS_SPACE_MAXADDR, NULL, NULL,
	    txmaxsegsz, BGE_NSEG_NEW, txsegsz, 0, NULL, NULL,
	    &sc->bge_cdata.bge_tx_mtag);

	if (error) {
		device_printf(sc->bge_dev, "could not allocate TX dma tag\n");
		return (ENOMEM);
	}

	/* Create tag for Rx mbufs. */
	if (sc->bge_flags & BGE_FLAG_JUMBO_STD)
		rxmaxsegsz = MJUM9BYTES;
	else
		rxmaxsegsz = MCLBYTES;
	error = bus_dma_tag_create(sc->bge_cdata.bge_buffer_tag, 1, 0,
	    BUS_SPACE_MAXADDR, BUS_SPACE_MAXADDR, NULL, NULL, rxmaxsegsz, 1,
	    rxmaxsegsz, 0, NULL, NULL, &sc->bge_cdata.bge_rx_mtag);

	if (error) {
		device_printf(sc->bge_dev, "could not allocate RX dma tag\n");
		return (ENOMEM);
	}

	/* Create DMA maps for RX buffers. */
	error = bus_dmamap_create(sc->bge_cdata.bge_rx_mtag, 0,
	    &sc->bge_cdata.bge_rx_std_sparemap);
	if (error) {
		device_printf(sc->bge_dev,
		    "can't create spare DMA map for RX\n");
		return (ENOMEM);
	}
	for (i = 0; i < BGE_STD_RX_RING_CNT; i++) {
		error = bus_dmamap_create(sc->bge_cdata.bge_rx_mtag, 0,
			    &sc->bge_cdata.bge_rx_std_dmamap[i]);
		if (error) {
			device_printf(sc->bge_dev,
			    "can't create DMA map for RX\n");
			return (ENOMEM);
		}
	}

	/* Create DMA maps for TX buffers. */
	for (i = 0; i < BGE_TX_RING_CNT; i++) {
		error = bus_dmamap_create(sc->bge_cdata.bge_tx_mtag, 0,
			    &sc->bge_cdata.bge_tx_dmamap[i]);
		if (error) {
			device_printf(sc->bge_dev,
			    "can't create DMA map for TX\n");
			return (ENOMEM);
		}
	}

	/* Create tags for jumbo RX buffers. */
	if (BGE_IS_JUMBO_CAPABLE(sc)) {
		error = bus_dma_tag_create(sc->bge_cdata.bge_buffer_tag,
		    1, 0, BUS_SPACE_MAXADDR, BUS_SPACE_MAXADDR, NULL,
		    NULL, MJUM9BYTES, BGE_NSEG_JUMBO, PAGE_SIZE,
		    0, NULL, NULL, &sc->bge_cdata.bge_mtag_jumbo);
		if (error) {
			device_printf(sc->bge_dev,
			    "could not allocate jumbo dma tag\n");
			return (ENOMEM);
		}
		/* Create DMA maps for jumbo RX buffers. */
		error = bus_dmamap_create(sc->bge_cdata.bge_mtag_jumbo,
		    0, &sc->bge_cdata.bge_rx_jumbo_sparemap);
		if (error) {
			device_printf(sc->bge_dev,
			    "can't create spare DMA map for jumbo RX\n");
			return (ENOMEM);
		}
		for (i = 0; i < BGE_JUMBO_RX_RING_CNT; i++) {
			error = bus_dmamap_create(sc->bge_cdata.bge_mtag_jumbo,
				    0, &sc->bge_cdata.bge_rx_jumbo_dmamap[i]);
			if (error) {
				device_printf(sc->bge_dev,
				    "can't create DMA map for jumbo RX\n");
				return (ENOMEM);
			}
		}
	}

	return (0);
}

/*
 * Return true if this device has more than one port.
 */
static int
bge_has_multiple_ports(struct bge_softc *sc)
{
	device_t dev = sc->bge_dev;
	u_int b, d, f, fscan, s;

	d = pci_get_domain(dev);
	b = pci_get_bus(dev);
	s = pci_get_slot(dev);
	f = pci_get_function(dev);
	for (fscan = 0; fscan <= PCI_FUNCMAX; fscan++)
		if (fscan != f && pci_find_dbsf(d, b, s, fscan) != NULL)
			return (1);
	return (0);
}

/*
 * Return true if MSI can be used with this device.
 */
static int
bge_can_use_msi(struct bge_softc *sc)
{
	int can_use_msi = 0;

	if (sc->bge_msi == 0)
		return (0);

	/* Disable MSI for polling(4). */
#ifdef DEVICE_POLLING
	return (0);
#endif
	switch (sc->bge_asicrev) {
	case BGE_ASICREV_BCM5714_A0:
	case BGE_ASICREV_BCM5714:
		/*
		 * Apparently, MSI doesn't work when these chips are
		 * configured in single-port mode.
		 */
		if (bge_has_multiple_ports(sc))
			can_use_msi = 1;
		break;
	case BGE_ASICREV_BCM5750:
		if (sc->bge_chiprev != BGE_CHIPREV_5750_AX &&
		    sc->bge_chiprev != BGE_CHIPREV_5750_BX)
			can_use_msi = 1;
		break;
	case BGE_ASICREV_BCM5784:
		/*
		 * Prevent infinite "watchdog timeout" errors
		 * in some MacBook Pro and make it work out-of-the-box.
		 */
		if (sc->bge_chiprev == BGE_CHIPREV_5784_AX)
			break;
		/* FALLTHROUGH */
	default:
		if (BGE_IS_575X_PLUS(sc))
			can_use_msi = 1;
	}
	return (can_use_msi);
}

static int
bge_mbox_reorder(struct bge_softc *sc)
{
	/* Lists of PCI bridges that are known to reorder mailbox writes. */
	static const struct mbox_reorder {
		const uint16_t vendor;
		const uint16_t device;
		const char *desc;
	} mbox_reorder_lists[] = {
		{ 0x1022, 0x7450, "AMD-8131 PCI-X Bridge" },
	};
	devclass_t pci, pcib;
	device_t bus, dev;
	int i;

	pci = devclass_find("pci");
	pcib = devclass_find("pcib");
	dev = sc->bge_dev;
	bus = device_get_parent(dev);
	for (;;) {
		dev = device_get_parent(bus);
		bus = device_get_parent(dev);
		if (device_get_devclass(dev) != pcib)
			break;
		for (i = 0; i < nitems(mbox_reorder_lists); i++) {
			if (pci_get_vendor(dev) ==
			    mbox_reorder_lists[i].vendor &&
			    pci_get_device(dev) ==
			    mbox_reorder_lists[i].device) {
				device_printf(sc->bge_dev,
				    "enabling MBOX workaround for %s\n",
				    mbox_reorder_lists[i].desc);
				return (1);
			}
		}
		if (device_get_devclass(bus) != pci)
			break;
	}
	return (0);
}

static void
bge_devinfo(struct bge_softc *sc)
{
	uint32_t cfg, clk;

	device_printf(sc->bge_dev,
	    "CHIP ID 0x%08x; ASIC REV 0x%02x; CHIP REV 0x%02x; ",
	    sc->bge_chipid, sc->bge_asicrev, sc->bge_chiprev);
	if (sc->bge_flags & BGE_FLAG_PCIE)
		printf("PCI-E\n");
	else if (sc->bge_flags & BGE_FLAG_PCIX) {
		printf("PCI-X ");
		cfg = CSR_READ_4(sc, BGE_MISC_CFG) & BGE_MISCCFG_BOARD_ID_MASK;
		if (cfg == BGE_MISCCFG_BOARD_ID_5704CIOBE)
			clk = 133;
		else {
			clk = CSR_READ_4(sc, BGE_PCI_CLKCTL) & 0x1F;
			switch (clk) {
			case 0:
				clk = 33;
				break;
			case 2:
				clk = 50;
				break;
			case 4:
				clk = 66;
				break;
			case 6:
				clk = 100;
				break;
			case 7:
				clk = 133;
				break;
			}
		}
		printf("%u MHz\n", clk);
	} else {
		if (sc->bge_pcixcap != 0)
			printf("PCI on PCI-X ");
		else
			printf("PCI ");
		cfg = pci_read_config(sc->bge_dev, BGE_PCI_PCISTATE, 4);
		if (cfg & BGE_PCISTATE_PCI_BUSSPEED)
			clk = 66;
		else
			clk = 33;
		if (cfg & BGE_PCISTATE_32BIT_BUS)
			printf("%u MHz; 32bit\n", clk);
		else
			printf("%u MHz; 64bit\n", clk);
	}
}

static int
bge_attach(device_t dev)
{
	if_t ifp;
	struct bge_softc *sc;
	uint32_t hwcfg = 0, misccfg, pcistate;
	u_char eaddr[ETHER_ADDR_LEN];
	int capmask, error, reg, rid, trys;

	sc = device_get_softc(dev);
	sc->bge_dev = dev;

	BGE_LOCK_INIT(sc, device_get_nameunit(dev));
	TASK_INIT(&sc->bge_intr_task, 0, bge_intr_task, sc);
	callout_init_mtx(&sc->bge_stat_ch, &sc->bge_mtx, 0);

	pci_enable_busmaster(dev);

	/*
	 * Allocate control/status registers.
	 */
	rid = PCIR_BAR(0);
	sc->bge_res = bus_alloc_resource_any(dev, SYS_RES_MEMORY, &rid,
	    RF_ACTIVE);

	if (sc->bge_res == NULL) {
		device_printf (sc->bge_dev, "couldn't map BAR0 memory\n");
		error = ENXIO;
		goto fail;
	}

	/* Save various chip information. */
	sc->bge_func_addr = pci_get_function(dev);
	sc->bge_chipid = bge_chipid(dev);
	sc->bge_asicrev = BGE_ASICREV(sc->bge_chipid);
	sc->bge_chiprev = BGE_CHIPREV(sc->bge_chipid);

	/* Set default PHY address. */
	sc->bge_phy_addr = 1;
	 /*
	  * PHY address mapping for various devices.
	  *
	  *          | F0 Cu | F0 Sr | F1 Cu | F1 Sr |
	  * ---------+-------+-------+-------+-------+
	  * BCM57XX  |   1   |   X   |   X   |   X   |
	  * BCM5704  |   1   |   X   |   1   |   X   |
	  * BCM5717  |   1   |   8   |   2   |   9   |
	  * BCM5719  |   1   |   8   |   2   |   9   |
	  * BCM5720  |   1   |   8   |   2   |   9   |
	  *
	  *          | F2 Cu | F2 Sr | F3 Cu | F3 Sr |
	  * ---------+-------+-------+-------+-------+
	  * BCM57XX  |   X   |   X   |   X   |   X   |
	  * BCM5704  |   X   |   X   |   X   |   X   |
	  * BCM5717  |   X   |   X   |   X   |   X   |
	  * BCM5719  |   3   |   10  |   4   |   11  |
	  * BCM5720  |   X   |   X   |   X   |   X   |
	  *
	  * Other addresses may respond but they are not
	  * IEEE compliant PHYs and should be ignored.
	  */
	if (sc->bge_asicrev == BGE_ASICREV_BCM5717 ||
	    sc->bge_asicrev == BGE_ASICREV_BCM5719 ||
	    sc->bge_asicrev == BGE_ASICREV_BCM5720) {
		if (sc->bge_chipid != BGE_CHIPID_BCM5717_A0) {
			if (CSR_READ_4(sc, BGE_SGDIG_STS) &
			    BGE_SGDIGSTS_IS_SERDES)
				sc->bge_phy_addr = sc->bge_func_addr + 8;
			else
				sc->bge_phy_addr = sc->bge_func_addr + 1;
		} else {
			if (CSR_READ_4(sc, BGE_CPMU_PHY_STRAP) &
			    BGE_CPMU_PHY_STRAP_IS_SERDES)
				sc->bge_phy_addr = sc->bge_func_addr + 8;
			else
				sc->bge_phy_addr = sc->bge_func_addr + 1;
		}
	}

	if (bge_has_eaddr(sc))
		sc->bge_flags |= BGE_FLAG_EADDR;

	/* Save chipset family. */
	switch (sc->bge_asicrev) {
	case BGE_ASICREV_BCM5762:
	case BGE_ASICREV_BCM57765:
	case BGE_ASICREV_BCM57766:
		sc->bge_flags |= BGE_FLAG_57765_PLUS;
		/* FALLTHROUGH */
	case BGE_ASICREV_BCM5717:
	case BGE_ASICREV_BCM5719:
	case BGE_ASICREV_BCM5720:
		sc->bge_flags |= BGE_FLAG_5717_PLUS | BGE_FLAG_5755_PLUS |
		    BGE_FLAG_575X_PLUS | BGE_FLAG_5705_PLUS | BGE_FLAG_JUMBO |
		    BGE_FLAG_JUMBO_FRAME;
		if (sc->bge_asicrev == BGE_ASICREV_BCM5719 ||
		    sc->bge_asicrev == BGE_ASICREV_BCM5720) {
			/*
			 * Enable work around for DMA engine miscalculation
			 * of TXMBUF available space.
			 */
			sc->bge_flags |= BGE_FLAG_RDMA_BUG;
			if (sc->bge_asicrev == BGE_ASICREV_BCM5719 &&
			    sc->bge_chipid == BGE_CHIPID_BCM5719_A0) {
				/* Jumbo frame on BCM5719 A0 does not work. */
				sc->bge_flags &= ~BGE_FLAG_JUMBO;
			}
		}
		break;
	case BGE_ASICREV_BCM5755:
	case BGE_ASICREV_BCM5761:
	case BGE_ASICREV_BCM5784:
	case BGE_ASICREV_BCM5785:
	case BGE_ASICREV_BCM5787:
	case BGE_ASICREV_BCM57780:
		sc->bge_flags |= BGE_FLAG_5755_PLUS | BGE_FLAG_575X_PLUS |
		    BGE_FLAG_5705_PLUS;
		break;
	case BGE_ASICREV_BCM5700:
	case BGE_ASICREV_BCM5701:
	case BGE_ASICREV_BCM5703:
	case BGE_ASICREV_BCM5704:
		sc->bge_flags |= BGE_FLAG_5700_FAMILY | BGE_FLAG_JUMBO;
		break;
	case BGE_ASICREV_BCM5714_A0:
	case BGE_ASICREV_BCM5780:
	case BGE_ASICREV_BCM5714:
		sc->bge_flags |= BGE_FLAG_5714_FAMILY | BGE_FLAG_JUMBO_STD;
		/* FALLTHROUGH */
	case BGE_ASICREV_BCM5750:
	case BGE_ASICREV_BCM5752:
	case BGE_ASICREV_BCM5906:
		sc->bge_flags |= BGE_FLAG_575X_PLUS;
		/* FALLTHROUGH */
	case BGE_ASICREV_BCM5705:
		sc->bge_flags |= BGE_FLAG_5705_PLUS;
		break;
	}

	/* Identify chips with APE processor. */
	switch (sc->bge_asicrev) {
	case BGE_ASICREV_BCM5717:
	case BGE_ASICREV_BCM5719:
	case BGE_ASICREV_BCM5720:
	case BGE_ASICREV_BCM5761:
	case BGE_ASICREV_BCM5762:
		sc->bge_flags |= BGE_FLAG_APE;
		break;
	}

	/* Chips with APE need BAR2 access for APE registers/memory. */
	if ((sc->bge_flags & BGE_FLAG_APE) != 0) {
		rid = PCIR_BAR(2);
		sc->bge_res2 = bus_alloc_resource_any(dev, SYS_RES_MEMORY, &rid,
		    RF_ACTIVE);
		if (sc->bge_res2 == NULL) {
			device_printf (sc->bge_dev,
			    "couldn't map BAR2 memory\n");
			error = ENXIO;
			goto fail;
		}

		/* Enable APE register/memory access by host driver. */
		pcistate = pci_read_config(dev, BGE_PCI_PCISTATE, 4);
		pcistate |= BGE_PCISTATE_ALLOW_APE_CTLSPC_WR |
		    BGE_PCISTATE_ALLOW_APE_SHMEM_WR |
		    BGE_PCISTATE_ALLOW_APE_PSPACE_WR;
		pci_write_config(dev, BGE_PCI_PCISTATE, pcistate, 4);

		bge_ape_lock_init(sc);
		bge_ape_read_fw_ver(sc);
	}

	/* Add SYSCTLs, requires the chipset family to be set. */
	bge_add_sysctls(sc);

	/* Identify the chips that use an CPMU. */
	if (BGE_IS_5717_PLUS(sc) ||
	    sc->bge_asicrev == BGE_ASICREV_BCM5784 ||
	    sc->bge_asicrev == BGE_ASICREV_BCM5761 ||
	    sc->bge_asicrev == BGE_ASICREV_BCM5785 ||
	    sc->bge_asicrev == BGE_ASICREV_BCM57780)
		sc->bge_flags |= BGE_FLAG_CPMU_PRESENT;
	if ((sc->bge_flags & BGE_FLAG_CPMU_PRESENT) != 0)
		sc->bge_mi_mode = BGE_MIMODE_500KHZ_CONST;
	else
		sc->bge_mi_mode = BGE_MIMODE_BASE;
	/* Enable auto polling for BCM570[0-5]. */
	if (BGE_IS_5700_FAMILY(sc) || sc->bge_asicrev == BGE_ASICREV_BCM5705)
		sc->bge_mi_mode |= BGE_MIMODE_AUTOPOLL;

	/*
	 * All Broadcom controllers have 4GB boundary DMA bug.
	 * Whenever an address crosses a multiple of the 4GB boundary
	 * (including 4GB, 8Gb, 12Gb, etc.) and makes the transition
	 * from 0xX_FFFF_FFFF to 0x(X+1)_0000_0000 an internal DMA
	 * state machine will lockup and cause the device to hang.
	 */
	sc->bge_flags |= BGE_FLAG_4G_BNDRY_BUG;

	/* BCM5755 or higher and BCM5906 have short DMA bug. */
	if (BGE_IS_5755_PLUS(sc) || sc->bge_asicrev == BGE_ASICREV_BCM5906)
		sc->bge_flags |= BGE_FLAG_SHORT_DMA_BUG;

	/*
	 * BCM5719 cannot handle DMA requests for DMA segments that
	 * have larger than 4KB in size.  However the maximum DMA
	 * segment size created in DMA tag is 4KB for TSO, so we
	 * wouldn't encounter the issue here.
	 */
	if (sc->bge_asicrev == BGE_ASICREV_BCM5719)
		sc->bge_flags |= BGE_FLAG_4K_RDMA_BUG;

	misccfg = CSR_READ_4(sc, BGE_MISC_CFG) & BGE_MISCCFG_BOARD_ID_MASK;
	if (sc->bge_asicrev == BGE_ASICREV_BCM5705) {
		if (misccfg == BGE_MISCCFG_BOARD_ID_5788 ||
		    misccfg == BGE_MISCCFG_BOARD_ID_5788M)
			sc->bge_flags |= BGE_FLAG_5788;
	}

	capmask = BMSR_DEFCAPMASK;
	if ((sc->bge_asicrev == BGE_ASICREV_BCM5703 &&
	    (misccfg == 0x4000 || misccfg == 0x8000)) ||
	    (sc->bge_asicrev == BGE_ASICREV_BCM5705 &&
	    pci_get_vendor(dev) == BCOM_VENDORID &&
	    (pci_get_device(dev) == BCOM_DEVICEID_BCM5901 ||
	    pci_get_device(dev) == BCOM_DEVICEID_BCM5901A2 ||
	    pci_get_device(dev) == BCOM_DEVICEID_BCM5705F)) ||
	    (pci_get_vendor(dev) == BCOM_VENDORID &&
	    (pci_get_device(dev) == BCOM_DEVICEID_BCM5751F ||
	    pci_get_device(dev) == BCOM_DEVICEID_BCM5753F ||
	    pci_get_device(dev) == BCOM_DEVICEID_BCM5787F)) ||
	    pci_get_device(dev) == BCOM_DEVICEID_BCM57790 ||
	    pci_get_device(dev) == BCOM_DEVICEID_BCM57791 ||
	    pci_get_device(dev) == BCOM_DEVICEID_BCM57795 ||
	    sc->bge_asicrev == BGE_ASICREV_BCM5906) {
		/* These chips are 10/100 only. */
		capmask &= ~BMSR_EXTSTAT;
		sc->bge_phy_flags |= BGE_PHY_NO_WIRESPEED;
	}

	/*
	 * Some controllers seem to require a special firmware to use
	 * TSO. But the firmware is not available to FreeBSD and Linux
	 * claims that the TSO performed by the firmware is slower than
	 * hardware based TSO. Moreover the firmware based TSO has one
	 * known bug which can't handle TSO if Ethernet header + IP/TCP
	 * header is greater than 80 bytes. A workaround for the TSO
	 * bug exist but it seems it's too expensive than not using
	 * TSO at all. Some hardwares also have the TSO bug so limit
	 * the TSO to the controllers that are not affected TSO issues
	 * (e.g. 5755 or higher).
	 */
	if (BGE_IS_5717_PLUS(sc)) {
		/* BCM5717 requires different TSO configuration. */
		sc->bge_flags |= BGE_FLAG_TSO3;
		if (sc->bge_asicrev == BGE_ASICREV_BCM5719 &&
		    sc->bge_chipid == BGE_CHIPID_BCM5719_A0) {
			/* TSO on BCM5719 A0 does not work. */
			sc->bge_flags &= ~BGE_FLAG_TSO3;
		}
	} else if (BGE_IS_5755_PLUS(sc)) {
		/*
		 * BCM5754 and BCM5787 shares the same ASIC id so
		 * explicit device id check is required.
		 * Due to unknown reason TSO does not work on BCM5755M.
		 */
		if (pci_get_device(dev) != BCOM_DEVICEID_BCM5754 &&
		    pci_get_device(dev) != BCOM_DEVICEID_BCM5754M &&
		    pci_get_device(dev) != BCOM_DEVICEID_BCM5755M)
			sc->bge_flags |= BGE_FLAG_TSO;
	}

	/*
	 * Check if this is a PCI-X or PCI Express device.
	 */
	if (pci_find_cap(dev, PCIY_EXPRESS, &reg) == 0) {
		/*
		 * Found a PCI Express capabilities register, this
		 * must be a PCI Express device.
		 */
		sc->bge_flags |= BGE_FLAG_PCIE;
		sc->bge_expcap = reg;
		/* Extract supported maximum payload size. */
		sc->bge_mps = pci_read_config(dev, sc->bge_expcap +
		    PCIER_DEVICE_CAP, 2);
		sc->bge_mps = 128 << (sc->bge_mps & PCIEM_CAP_MAX_PAYLOAD);
		if (sc->bge_asicrev == BGE_ASICREV_BCM5719 ||
		    sc->bge_asicrev == BGE_ASICREV_BCM5720)
			sc->bge_expmrq = 2048;
		else
			sc->bge_expmrq = 4096;
		pci_set_max_read_req(dev, sc->bge_expmrq);
	} else {
		/*
		 * Check if the device is in PCI-X Mode.
		 * (This bit is not valid on PCI Express controllers.)
		 */
		if (pci_find_cap(dev, PCIY_PCIX, &reg) == 0)
			sc->bge_pcixcap = reg;
		if ((pci_read_config(dev, BGE_PCI_PCISTATE, 4) &
		    BGE_PCISTATE_PCI_BUSMODE) == 0)
			sc->bge_flags |= BGE_FLAG_PCIX;
	}

	/*
	 * The 40bit DMA bug applies to the 5714/5715 controllers and is
	 * not actually a MAC controller bug but an issue with the embedded
	 * PCIe to PCI-X bridge in the device. Use 40bit DMA workaround.
	 */
	if (BGE_IS_5714_FAMILY(sc) && (sc->bge_flags & BGE_FLAG_PCIX))
		sc->bge_flags |= BGE_FLAG_40BIT_BUG;
	/*
	 * Some PCI-X bridges are known to trigger write reordering to
	 * the mailbox registers. Typical phenomena is watchdog timeouts
	 * caused by out-of-order TX completions.  Enable workaround for
	 * PCI-X devices that live behind these bridges.
	 * Note, PCI-X controllers can run in PCI mode so we can't use
	 * BGE_FLAG_PCIX flag to detect PCI-X controllers.
	 */
	if (sc->bge_pcixcap != 0 && bge_mbox_reorder(sc) != 0)
		sc->bge_flags |= BGE_FLAG_MBOX_REORDER;
	/*
	 * Allocate the interrupt, using MSI if possible.  These devices
	 * support 8 MSI messages, but only the first one is used in
	 * normal operation.
	 */
	rid = 0;
	if (pci_find_cap(sc->bge_dev, PCIY_MSI, &reg) == 0) {
		sc->bge_msicap = reg;
		reg = 1;
		if (bge_can_use_msi(sc) && pci_alloc_msi(dev, &reg) == 0) {
			rid = 1;
			sc->bge_flags |= BGE_FLAG_MSI;
		}
	}

	/*
	 * All controllers except BCM5700 supports tagged status but
	 * we use tagged status only for MSI case on BCM5717. Otherwise
	 * MSI on BCM5717 does not work.
	 */
#ifndef DEVICE_POLLING
	if (sc->bge_flags & BGE_FLAG_MSI && BGE_IS_5717_PLUS(sc))
		sc->bge_flags |= BGE_FLAG_TAGGED_STATUS;
#endif

	sc->bge_irq = bus_alloc_resource_any(dev, SYS_RES_IRQ, &rid,
	    RF_ACTIVE | (rid != 0 ? 0 : RF_SHAREABLE));

	if (sc->bge_irq == NULL) {
		device_printf(sc->bge_dev, "couldn't map interrupt\n");
		error = ENXIO;
		goto fail;
	}

	bge_devinfo(sc);

	sc->bge_asf_mode = 0;
	/* No ASF if APE present. */
	if ((sc->bge_flags & BGE_FLAG_APE) == 0) {
		if (bge_allow_asf && (bge_readmem_ind(sc, BGE_SRAM_DATA_SIG) ==
		    BGE_SRAM_DATA_SIG_MAGIC)) {
			if (bge_readmem_ind(sc, BGE_SRAM_DATA_CFG) &
			    BGE_HWCFG_ASF) {
				sc->bge_asf_mode |= ASF_ENABLE;
				sc->bge_asf_mode |= ASF_STACKUP;
				if (BGE_IS_575X_PLUS(sc))
					sc->bge_asf_mode |= ASF_NEW_HANDSHAKE;
			}
		}
	}

	bge_stop_fw(sc);
	bge_sig_pre_reset(sc, BGE_RESET_SHUTDOWN);
	if (bge_reset(sc)) {
		device_printf(sc->bge_dev, "chip reset failed\n");
		error = ENXIO;
		goto fail;
	}

	bge_sig_legacy(sc, BGE_RESET_SHUTDOWN);
	bge_sig_post_reset(sc, BGE_RESET_SHUTDOWN);

	if (bge_chipinit(sc)) {
		device_printf(sc->bge_dev, "chip initialization failed\n");
		error = ENXIO;
		goto fail;
	}

	error = bge_get_eaddr(sc, eaddr);
	if (error) {
		device_printf(sc->bge_dev,
		    "failed to read station address\n");
		error = ENXIO;
		goto fail;
	}

	/* 5705 limits RX return ring to 512 entries. */
	if (BGE_IS_5717_PLUS(sc))
		sc->bge_return_ring_cnt = BGE_RETURN_RING_CNT;
	else if (BGE_IS_5705_PLUS(sc))
		sc->bge_return_ring_cnt = BGE_RETURN_RING_CNT_5705;
	else
		sc->bge_return_ring_cnt = BGE_RETURN_RING_CNT;

	if (bge_dma_alloc(sc)) {
		device_printf(sc->bge_dev,
		    "failed to allocate DMA resources\n");
		error = ENXIO;
		goto fail;
	}

	/* Set default tuneable values. */
	sc->bge_stat_ticks = BGE_TICKS_PER_SEC;
	sc->bge_rx_coal_ticks = 150;
	sc->bge_tx_coal_ticks = 150;
	sc->bge_rx_max_coal_bds = 10;
	sc->bge_tx_max_coal_bds = 10;

	/* Initialize checksum features to use. */
	sc->bge_csum_features = BGE_CSUM_FEATURES;
	if (sc->bge_forced_udpcsum != 0)
		sc->bge_csum_features |= CSUM_UDP;

	/* Set up ifnet structure */
	ifp = sc->bge_ifp = if_alloc(IFT_ETHER);
	if (ifp == NULL) {
		device_printf(sc->bge_dev, "failed to if_alloc()\n");
		error = ENXIO;
		goto fail;
	}
	if_setsoftc(ifp, sc);
	if_initname(ifp, device_get_name(dev), device_get_unit(dev));
	if_setflags(ifp, IFF_BROADCAST | IFF_SIMPLEX | IFF_MULTICAST);
	if_setioctlfn(ifp, bge_ioctl);
	if_setstartfn(ifp, bge_start);
	if_setinitfn(ifp, bge_init);
	if_setgetcounterfn(ifp, bge_get_counter);
	if_setsendqlen(ifp, BGE_TX_RING_CNT - 1);
	if_setsendqready(ifp);
	if_sethwassist(ifp, sc->bge_csum_features);
	if_setcapabilities(ifp, IFCAP_HWCSUM | IFCAP_VLAN_HWTAGGING |
	    IFCAP_VLAN_MTU);
	if ((sc->bge_flags & (BGE_FLAG_TSO | BGE_FLAG_TSO3)) != 0) {
		if_sethwassistbits(ifp, CSUM_TSO, 0);
		if_setcapabilitiesbit(ifp, IFCAP_TSO4 | IFCAP_VLAN_HWTSO, 0);
	}
#ifdef IFCAP_VLAN_HWCSUM
	if_setcapabilitiesbit(ifp, IFCAP_VLAN_HWCSUM, 0);
#endif
	if_setcapenable(ifp, if_getcapabilities(ifp));
#ifdef DEVICE_POLLING
	if_setcapabilitiesbit(ifp, IFCAP_POLLING, 0);
#endif

	/*
	 * 5700 B0 chips do not support checksumming correctly due
	 * to hardware bugs.
	 */
	if (sc->bge_chipid == BGE_CHIPID_BCM5700_B0) {
		if_setcapabilitiesbit(ifp, 0, IFCAP_HWCSUM);
		if_setcapenablebit(ifp, 0, IFCAP_HWCSUM);
		if_sethwassist(ifp, 0);
	}

	/*
	 * Figure out what sort of media we have by checking the
	 * hardware config word in the first 32k of NIC internal memory,
	 * or fall back to examining the EEPROM if necessary.
	 * Note: on some BCM5700 cards, this value appears to be unset.
	 * If that's the case, we have to rely on identifying the NIC
	 * by its PCI subsystem ID, as we do below for the SysKonnect
	 * SK-9D41.
	 */
	if (bge_readmem_ind(sc, BGE_SRAM_DATA_SIG) == BGE_SRAM_DATA_SIG_MAGIC)
		hwcfg = bge_readmem_ind(sc, BGE_SRAM_DATA_CFG);
	else if ((sc->bge_flags & BGE_FLAG_EADDR) &&
	    (sc->bge_asicrev != BGE_ASICREV_BCM5906)) {
		if (bge_read_eeprom(sc, (caddr_t)&hwcfg, BGE_EE_HWCFG_OFFSET,
		    sizeof(hwcfg))) {
			device_printf(sc->bge_dev, "failed to read EEPROM\n");
			error = ENXIO;
			goto fail;
		}
		hwcfg = ntohl(hwcfg);
	}

	/* The SysKonnect SK-9D41 is a 1000baseSX card. */
	if ((pci_read_config(dev, BGE_PCI_SUBSYS, 4) >> 16) ==
	    SK_SUBSYSID_9D41 || (hwcfg & BGE_HWCFG_MEDIA) == BGE_MEDIA_FIBER) {
		if (BGE_IS_5705_PLUS(sc)) {
			sc->bge_flags |= BGE_FLAG_MII_SERDES;
			sc->bge_phy_flags |= BGE_PHY_NO_WIRESPEED;
		} else
			sc->bge_flags |= BGE_FLAG_TBI;
	}

	/* Set various PHY bug flags. */
	if (sc->bge_chipid == BGE_CHIPID_BCM5701_A0 ||
	    sc->bge_chipid == BGE_CHIPID_BCM5701_B0)
		sc->bge_phy_flags |= BGE_PHY_CRC_BUG;
	if (sc->bge_chiprev == BGE_CHIPREV_5703_AX ||
	    sc->bge_chiprev == BGE_CHIPREV_5704_AX)
		sc->bge_phy_flags |= BGE_PHY_ADC_BUG;
	if (sc->bge_chipid == BGE_CHIPID_BCM5704_A0)
		sc->bge_phy_flags |= BGE_PHY_5704_A0_BUG;
	if (pci_get_subvendor(dev) == DELL_VENDORID)
		sc->bge_phy_flags |= BGE_PHY_NO_3LED;
	if ((BGE_IS_5705_PLUS(sc)) &&
	    sc->bge_asicrev != BGE_ASICREV_BCM5906 &&
	    sc->bge_asicrev != BGE_ASICREV_BCM5785 &&
	    sc->bge_asicrev != BGE_ASICREV_BCM57780 &&
	    !BGE_IS_5717_PLUS(sc)) {
		if (sc->bge_asicrev == BGE_ASICREV_BCM5755 ||
		    sc->bge_asicrev == BGE_ASICREV_BCM5761 ||
		    sc->bge_asicrev == BGE_ASICREV_BCM5784 ||
		    sc->bge_asicrev == BGE_ASICREV_BCM5787) {
			if (pci_get_device(dev) != BCOM_DEVICEID_BCM5722 &&
			    pci_get_device(dev) != BCOM_DEVICEID_BCM5756)
				sc->bge_phy_flags |= BGE_PHY_JITTER_BUG;
			if (pci_get_device(dev) == BCOM_DEVICEID_BCM5755M)
				sc->bge_phy_flags |= BGE_PHY_ADJUST_TRIM;
		} else
			sc->bge_phy_flags |= BGE_PHY_BER_BUG;
	}

	/*
	 * Don't enable Ethernet@WireSpeed for the 5700 or the
	 * 5705 A0 and A1 chips.
	 */
	if (sc->bge_asicrev == BGE_ASICREV_BCM5700 ||
	    (sc->bge_asicrev == BGE_ASICREV_BCM5705 &&
	    (sc->bge_chipid != BGE_CHIPID_BCM5705_A0 &&
	    sc->bge_chipid != BGE_CHIPID_BCM5705_A1)))
		sc->bge_phy_flags |= BGE_PHY_NO_WIRESPEED;

	if (sc->bge_flags & BGE_FLAG_TBI) {
		ifmedia_init(&sc->bge_ifmedia, IFM_IMASK, bge_ifmedia_upd,
		    bge_ifmedia_sts);
		ifmedia_add(&sc->bge_ifmedia, IFM_ETHER | IFM_1000_SX, 0, NULL);
		ifmedia_add(&sc->bge_ifmedia, IFM_ETHER | IFM_1000_SX | IFM_FDX,
		    0, NULL);
		ifmedia_add(&sc->bge_ifmedia, IFM_ETHER | IFM_AUTO, 0, NULL);
		ifmedia_set(&sc->bge_ifmedia, IFM_ETHER | IFM_AUTO);
		sc->bge_ifmedia.ifm_media = sc->bge_ifmedia.ifm_cur->ifm_media;
	} else {
		/*
		 * Do transceiver setup and tell the firmware the
		 * driver is down so we can try to get access the
		 * probe if ASF is running.  Retry a couple of times
		 * if we get a conflict with the ASF firmware accessing
		 * the PHY.
		 */
		trys = 0;
		BGE_CLRBIT(sc, BGE_MODE_CTL, BGE_MODECTL_STACKUP);
again:
		bge_asf_driver_up(sc);

		error = mii_attach(dev, &sc->bge_miibus, ifp, 
		    (ifm_change_cb_t)bge_ifmedia_upd,
		    (ifm_stat_cb_t)bge_ifmedia_sts, capmask, sc->bge_phy_addr, 
		    MII_OFFSET_ANY, MIIF_DOPAUSE);
		if (error != 0) {
			if (trys++ < 4) {
				device_printf(sc->bge_dev, "Try again\n");
				bge_miibus_writereg(sc->bge_dev,
				    sc->bge_phy_addr, MII_BMCR, BMCR_RESET);
				goto again;
			}
			device_printf(sc->bge_dev, "attaching PHYs failed\n");
			goto fail;
		}

		/*
		 * Now tell the firmware we are going up after probing the PHY
		 */
		if (sc->bge_asf_mode & ASF_STACKUP)
			BGE_SETBIT(sc, BGE_MODE_CTL, BGE_MODECTL_STACKUP);
	}

	/*
	 * When using the BCM5701 in PCI-X mode, data corruption has
	 * been observed in the first few bytes of some received packets.
	 * Aligning the packet buffer in memory eliminates the corruption.
	 * Unfortunately, this misaligns the packet payloads.  On platforms
	 * which do not support unaligned accesses, we will realign the
	 * payloads by copying the received packets.
	 */
	if (sc->bge_asicrev == BGE_ASICREV_BCM5701 &&
	    sc->bge_flags & BGE_FLAG_PCIX)
                sc->bge_flags |= BGE_FLAG_RX_ALIGNBUG;

	/*
	 * Call MI attach routine.
	 */
	ether_ifattach(ifp, eaddr);

	/* Tell upper layer we support long frames. */
	if_setifheaderlen(ifp, sizeof(struct ether_vlan_header));

	/*
	 * Hookup IRQ last.
	 */
	if (BGE_IS_5755_PLUS(sc) && sc->bge_flags & BGE_FLAG_MSI) {
		/* Take advantage of single-shot MSI. */
		CSR_WRITE_4(sc, BGE_MSI_MODE, CSR_READ_4(sc, BGE_MSI_MODE) &
		    ~BGE_MSIMODE_ONE_SHOT_DISABLE);
		sc->bge_tq = taskqueue_create_fast("bge_taskq", M_WAITOK,
		    taskqueue_thread_enqueue, &sc->bge_tq);
		if (sc->bge_tq == NULL) {
			device_printf(dev, "could not create taskqueue.\n");
			ether_ifdetach(ifp);
			error = ENOMEM;
			goto fail;
		}
		error = taskqueue_start_threads(&sc->bge_tq, 1, PI_NET,
		    "%s taskq", device_get_nameunit(sc->bge_dev));
		if (error != 0) {
			device_printf(dev, "could not start threads.\n");
			ether_ifdetach(ifp);
			goto fail;
		}
		error = bus_setup_intr(dev, sc->bge_irq,
		    INTR_TYPE_NET | INTR_MPSAFE, bge_msi_intr, NULL, sc,
		    &sc->bge_intrhand);
	} else
		error = bus_setup_intr(dev, sc->bge_irq,
		    INTR_TYPE_NET | INTR_MPSAFE, NULL, bge_intr, sc,
		    &sc->bge_intrhand);

	if (error) {
		ether_ifdetach(ifp);
		device_printf(sc->bge_dev, "couldn't set up irq\n");
		goto fail;
	}

	/* Attach driver netdump methods. */
	NETDUMP_SET(ifp, bge);

fail:
	if (error)
		bge_detach(dev);
	return (error);
}

static int
bge_detach(device_t dev)
{
	struct bge_softc *sc;
	if_t ifp;

	sc = device_get_softc(dev);
	ifp = sc->bge_ifp;

#ifdef DEVICE_POLLING
	if (if_getcapenable(ifp) & IFCAP_POLLING)
		ether_poll_deregister(ifp);
#endif

	if (device_is_attached(dev)) {
		ether_ifdetach(ifp);
		BGE_LOCK(sc);
		bge_stop(sc);
		BGE_UNLOCK(sc);
		callout_drain(&sc->bge_stat_ch);
	}

	if (sc->bge_tq)
		taskqueue_drain(sc->bge_tq, &sc->bge_intr_task);

	if (sc->bge_flags & BGE_FLAG_TBI)
		ifmedia_removeall(&sc->bge_ifmedia);
	else if (sc->bge_miibus != NULL) {
		bus_generic_detach(dev);
		device_delete_child(dev, sc->bge_miibus);
	}

	bge_release_resources(sc);

	return (0);
}

static void
bge_release_resources(struct bge_softc *sc)
{
	device_t dev;

	dev = sc->bge_dev;

	if (sc->bge_tq != NULL)
		taskqueue_free(sc->bge_tq);

	if (sc->bge_intrhand != NULL)
		bus_teardown_intr(dev, sc->bge_irq, sc->bge_intrhand);

	if (sc->bge_irq != NULL) {
		bus_release_resource(dev, SYS_RES_IRQ,
		    rman_get_rid(sc->bge_irq), sc->bge_irq);
		pci_release_msi(dev);
	}

	if (sc->bge_res != NULL)
		bus_release_resource(dev, SYS_RES_MEMORY,
		    rman_get_rid(sc->bge_res), sc->bge_res);

	if (sc->bge_res2 != NULL)
		bus_release_resource(dev, SYS_RES_MEMORY,
		    rman_get_rid(sc->bge_res2), sc->bge_res2);

	if (sc->bge_ifp != NULL)
		if_free(sc->bge_ifp);

	bge_dma_free(sc);

	if (mtx_initialized(&sc->bge_mtx))	/* XXX */
		BGE_LOCK_DESTROY(sc);
}

static int
bge_reset(struct bge_softc *sc)
{
	device_t dev;
	uint32_t cachesize, command, mac_mode, mac_mode_mask, reset, val;
	void (*write_op)(struct bge_softc *, int, int);
	uint16_t devctl;
	int i;

	dev = sc->bge_dev;

	mac_mode_mask = BGE_MACMODE_HALF_DUPLEX | BGE_MACMODE_PORTMODE;
	if ((sc->bge_mfw_flags & BGE_MFW_ON_APE) != 0)
		mac_mode_mask |= BGE_MACMODE_APE_RX_EN | BGE_MACMODE_APE_TX_EN;
	mac_mode = CSR_READ_4(sc, BGE_MAC_MODE) & mac_mode_mask;

	if (BGE_IS_575X_PLUS(sc) && !BGE_IS_5714_FAMILY(sc) &&
	    (sc->bge_asicrev != BGE_ASICREV_BCM5906)) {
		if (sc->bge_flags & BGE_FLAG_PCIE)
			write_op = bge_writemem_direct;
		else
			write_op = bge_writemem_ind;
	} else
		write_op = bge_writereg_ind;

	if (sc->bge_asicrev != BGE_ASICREV_BCM5700 &&
	    sc->bge_asicrev != BGE_ASICREV_BCM5701) {
		CSR_WRITE_4(sc, BGE_NVRAM_SWARB, BGE_NVRAMSWARB_SET1);
		for (i = 0; i < 8000; i++) {
			if (CSR_READ_4(sc, BGE_NVRAM_SWARB) &
			    BGE_NVRAMSWARB_GNT1)
				break;
			DELAY(20);
		}
		if (i == 8000) {
			if (bootverbose)
				device_printf(dev, "NVRAM lock timedout!\n");
		}
	}
	/* Take APE lock when performing reset. */
	bge_ape_lock(sc, BGE_APE_LOCK_GRC);

	/* Save some important PCI state. */
	cachesize = pci_read_config(dev, BGE_PCI_CACHESZ, 4);
	command = pci_read_config(dev, BGE_PCI_CMD, 4);

	pci_write_config(dev, BGE_PCI_MISC_CTL,
	    BGE_PCIMISCCTL_INDIRECT_ACCESS | BGE_PCIMISCCTL_MASK_PCI_INTR |
	    BGE_HIF_SWAP_OPTIONS | BGE_PCIMISCCTL_PCISTATE_RW, 4);

	/* Disable fastboot on controllers that support it. */
	if (sc->bge_asicrev == BGE_ASICREV_BCM5752 ||
	    BGE_IS_5755_PLUS(sc)) {
		if (bootverbose)
			device_printf(dev, "Disabling fastboot\n");
		CSR_WRITE_4(sc, BGE_FASTBOOT_PC, 0x0);
	}

	/*
	 * Write the magic number to SRAM at offset 0xB50.
	 * When firmware finishes its initialization it will
	 * write ~BGE_SRAM_FW_MB_MAGIC to the same location.
	 */
	bge_writemem_ind(sc, BGE_SRAM_FW_MB, BGE_SRAM_FW_MB_MAGIC);

	reset = BGE_MISCCFG_RESET_CORE_CLOCKS | BGE_32BITTIME_66MHZ;

	/* XXX: Broadcom Linux driver. */
	if (sc->bge_flags & BGE_FLAG_PCIE) {
		if (sc->bge_asicrev != BGE_ASICREV_BCM5785 &&
		    (sc->bge_flags & BGE_FLAG_5717_PLUS) == 0) {
			if (CSR_READ_4(sc, 0x7E2C) == 0x60)	/* PCIE 1.0 */
				CSR_WRITE_4(sc, 0x7E2C, 0x20);
		}
		if (sc->bge_chipid != BGE_CHIPID_BCM5750_A0) {
			/* Prevent PCIE link training during global reset */
			CSR_WRITE_4(sc, BGE_MISC_CFG, 1 << 29);
			reset |= 1 << 29;
		}
	}

	if (sc->bge_asicrev == BGE_ASICREV_BCM5906) {
		val = CSR_READ_4(sc, BGE_VCPU_STATUS);
		CSR_WRITE_4(sc, BGE_VCPU_STATUS,
		    val | BGE_VCPU_STATUS_DRV_RESET);
		val = CSR_READ_4(sc, BGE_VCPU_EXT_CTRL);
		CSR_WRITE_4(sc, BGE_VCPU_EXT_CTRL,
		    val & ~BGE_VCPU_EXT_CTRL_HALT_CPU);
	}

	/*
	 * Set GPHY Power Down Override to leave GPHY
	 * powered up in D0 uninitialized.
	 */
	if (BGE_IS_5705_PLUS(sc) &&
	    (sc->bge_flags & BGE_FLAG_CPMU_PRESENT) == 0)
		reset |= BGE_MISCCFG_GPHY_PD_OVERRIDE;

	/* Issue global reset */
	write_op(sc, BGE_MISC_CFG, reset);

	if (sc->bge_flags & BGE_FLAG_PCIE)
		DELAY(100 * 1000);
	else
		DELAY(1000);

	/* XXX: Broadcom Linux driver. */
	if (sc->bge_flags & BGE_FLAG_PCIE) {
		if (sc->bge_chipid == BGE_CHIPID_BCM5750_A0) {
			DELAY(500000); /* wait for link training to complete */
			val = pci_read_config(dev, 0xC4, 4);
			pci_write_config(dev, 0xC4, val | (1 << 15), 4);
		}
		devctl = pci_read_config(dev,
		    sc->bge_expcap + PCIER_DEVICE_CTL, 2);
		/* Clear enable no snoop and disable relaxed ordering. */
		devctl &= ~(PCIEM_CTL_RELAXED_ORD_ENABLE |
		    PCIEM_CTL_NOSNOOP_ENABLE);
		pci_write_config(dev, sc->bge_expcap + PCIER_DEVICE_CTL,
		    devctl, 2);
		pci_set_max_read_req(dev, sc->bge_expmrq);
		/* Clear error status. */
		pci_write_config(dev, sc->bge_expcap + PCIER_DEVICE_STA,
		    PCIEM_STA_CORRECTABLE_ERROR |
		    PCIEM_STA_NON_FATAL_ERROR | PCIEM_STA_FATAL_ERROR |
		    PCIEM_STA_UNSUPPORTED_REQ, 2);
	}

	/* Reset some of the PCI state that got zapped by reset. */
	pci_write_config(dev, BGE_PCI_MISC_CTL,
	    BGE_PCIMISCCTL_INDIRECT_ACCESS | BGE_PCIMISCCTL_MASK_PCI_INTR |
	    BGE_HIF_SWAP_OPTIONS | BGE_PCIMISCCTL_PCISTATE_RW, 4);
	val = BGE_PCISTATE_ROM_ENABLE | BGE_PCISTATE_ROM_RETRY_ENABLE;
	if (sc->bge_chipid == BGE_CHIPID_BCM5704_A0 &&
	    (sc->bge_flags & BGE_FLAG_PCIX) != 0)
		val |= BGE_PCISTATE_RETRY_SAME_DMA;
	if ((sc->bge_mfw_flags & BGE_MFW_ON_APE) != 0)
		val |= BGE_PCISTATE_ALLOW_APE_CTLSPC_WR |
		    BGE_PCISTATE_ALLOW_APE_SHMEM_WR |
		    BGE_PCISTATE_ALLOW_APE_PSPACE_WR;
	pci_write_config(dev, BGE_PCI_PCISTATE, val, 4);
	pci_write_config(dev, BGE_PCI_CACHESZ, cachesize, 4);
	pci_write_config(dev, BGE_PCI_CMD, command, 4);
	/*
	 * Disable PCI-X relaxed ordering to ensure status block update
	 * comes first then packet buffer DMA. Otherwise driver may
	 * read stale status block.
	 */
	if (sc->bge_flags & BGE_FLAG_PCIX) {
		devctl = pci_read_config(dev,
		    sc->bge_pcixcap + PCIXR_COMMAND, 2);
		devctl &= ~PCIXM_COMMAND_ERO;
		if (sc->bge_asicrev == BGE_ASICREV_BCM5703) {
			devctl &= ~PCIXM_COMMAND_MAX_READ;
			devctl |= PCIXM_COMMAND_MAX_READ_2048;
		} else if (sc->bge_asicrev == BGE_ASICREV_BCM5704) {
			devctl &= ~(PCIXM_COMMAND_MAX_SPLITS |
			    PCIXM_COMMAND_MAX_READ);
			devctl |= PCIXM_COMMAND_MAX_READ_2048;
		}
		pci_write_config(dev, sc->bge_pcixcap + PCIXR_COMMAND,
		    devctl, 2);
	}
	/* Re-enable MSI, if necessary, and enable the memory arbiter. */
	if (BGE_IS_5714_FAMILY(sc)) {
		/* This chip disables MSI on reset. */
		if (sc->bge_flags & BGE_FLAG_MSI) {
			val = pci_read_config(dev,
			    sc->bge_msicap + PCIR_MSI_CTRL, 2);
			pci_write_config(dev,
			    sc->bge_msicap + PCIR_MSI_CTRL,
			    val | PCIM_MSICTRL_MSI_ENABLE, 2);
			val = CSR_READ_4(sc, BGE_MSI_MODE);
			CSR_WRITE_4(sc, BGE_MSI_MODE,
			    val | BGE_MSIMODE_ENABLE);
		}
		val = CSR_READ_4(sc, BGE_MARB_MODE);
		CSR_WRITE_4(sc, BGE_MARB_MODE, BGE_MARBMODE_ENABLE | val);
	} else
		CSR_WRITE_4(sc, BGE_MARB_MODE, BGE_MARBMODE_ENABLE);

	/* Fix up byte swapping. */
	CSR_WRITE_4(sc, BGE_MODE_CTL, bge_dma_swap_options(sc));

	val = CSR_READ_4(sc, BGE_MAC_MODE);
	val = (val & ~mac_mode_mask) | mac_mode;
	CSR_WRITE_4(sc, BGE_MAC_MODE, val);
	DELAY(40);

	bge_ape_unlock(sc, BGE_APE_LOCK_GRC);

	if (sc->bge_asicrev == BGE_ASICREV_BCM5906) {
		for (i = 0; i < BGE_TIMEOUT; i++) {
			val = CSR_READ_4(sc, BGE_VCPU_STATUS);
			if (val & BGE_VCPU_STATUS_INIT_DONE)
				break;
			DELAY(100);
		}
		if (i == BGE_TIMEOUT) {
			device_printf(dev, "reset timed out\n");
			return (1);
		}
	} else {
		/*
		 * Poll until we see the 1's complement of the magic number.
		 * This indicates that the firmware initialization is complete.
		 * We expect this to fail if no chip containing the Ethernet
		 * address is fitted though.
		 */
		for (i = 0; i < BGE_TIMEOUT; i++) {
			DELAY(10);
			val = bge_readmem_ind(sc, BGE_SRAM_FW_MB);
			if (val == ~BGE_SRAM_FW_MB_MAGIC)
				break;
		}

		if ((sc->bge_flags & BGE_FLAG_EADDR) && i == BGE_TIMEOUT)
			device_printf(dev,
			    "firmware handshake timed out, found 0x%08x\n",
			    val);
		/* BCM57765 A0 needs additional time before accessing. */
		if (sc->bge_chipid == BGE_CHIPID_BCM57765_A0)
			DELAY(10 * 1000);	/* XXX */
	}

	/*
	 * The 5704 in TBI mode apparently needs some special
	 * adjustment to insure the SERDES drive level is set
	 * to 1.2V.
	 */
	if (sc->bge_asicrev == BGE_ASICREV_BCM5704 &&
	    sc->bge_flags & BGE_FLAG_TBI) {
		val = CSR_READ_4(sc, BGE_SERDES_CFG);
		val = (val & ~0xFFF) | 0x880;
		CSR_WRITE_4(sc, BGE_SERDES_CFG, val);
	}

	/* XXX: Broadcom Linux driver. */
	if (sc->bge_flags & BGE_FLAG_PCIE &&
	    !BGE_IS_5717_PLUS(sc) &&
	    sc->bge_chipid != BGE_CHIPID_BCM5750_A0 &&
	    sc->bge_asicrev != BGE_ASICREV_BCM5785) {
		/* Enable Data FIFO protection. */
		val = CSR_READ_4(sc, 0x7C00);
		CSR_WRITE_4(sc, 0x7C00, val | (1 << 25));
	}

	if (sc->bge_asicrev == BGE_ASICREV_BCM5720)
		BGE_CLRBIT(sc, BGE_CPMU_CLCK_ORIDE,
		    CPMU_CLCK_ORIDE_MAC_ORIDE_EN);

	return (0);
}

static __inline void
bge_rxreuse_std(struct bge_softc *sc, int i)
{
	struct bge_rx_bd *r;

	r = &sc->bge_ldata.bge_rx_std_ring[sc->bge_std];
	r->bge_flags = BGE_RXBDFLAG_END;
	r->bge_len = sc->bge_cdata.bge_rx_std_seglen[i];
	r->bge_idx = i;
	BGE_INC(sc->bge_std, BGE_STD_RX_RING_CNT);
}

static __inline void
bge_rxreuse_jumbo(struct bge_softc *sc, int i)
{
	struct bge_extrx_bd *r;

	r = &sc->bge_ldata.bge_rx_jumbo_ring[sc->bge_jumbo];
	r->bge_flags = BGE_RXBDFLAG_JUMBO_RING | BGE_RXBDFLAG_END;
	r->bge_len0 = sc->bge_cdata.bge_rx_jumbo_seglen[i][0];
	r->bge_len1 = sc->bge_cdata.bge_rx_jumbo_seglen[i][1];
	r->bge_len2 = sc->bge_cdata.bge_rx_jumbo_seglen[i][2];
	r->bge_len3 = sc->bge_cdata.bge_rx_jumbo_seglen[i][3];
	r->bge_idx = i;
	BGE_INC(sc->bge_jumbo, BGE_JUMBO_RX_RING_CNT);
}

/*
 * Frame reception handling. This is called if there's a frame
 * on the receive return list.
 *
 * Note: we have to be able to handle two possibilities here:
 * 1) the frame is from the jumbo receive ring
 * 2) the frame is from the standard receive ring
 */

static int
bge_rxeof(struct bge_softc *sc, uint16_t rx_prod, int holdlck)
{
	if_t ifp;
	int rx_npkts = 0, stdcnt = 0, jumbocnt = 0;
	uint16_t rx_cons;

	rx_cons = sc->bge_rx_saved_considx;

	/* Nothing to do. */
	if (rx_cons == rx_prod)
		return (rx_npkts);

	ifp = sc->bge_ifp;

	bus_dmamap_sync(sc->bge_cdata.bge_rx_return_ring_tag,
	    sc->bge_cdata.bge_rx_return_ring_map, BUS_DMASYNC_POSTREAD);
	bus_dmamap_sync(sc->bge_cdata.bge_rx_std_ring_tag,
	    sc->bge_cdata.bge_rx_std_ring_map, BUS_DMASYNC_POSTWRITE);
	if (BGE_IS_JUMBO_CAPABLE(sc) &&
	    if_getmtu(ifp) + ETHER_HDR_LEN + ETHER_CRC_LEN + 
	    ETHER_VLAN_ENCAP_LEN > (MCLBYTES - ETHER_ALIGN))
		bus_dmamap_sync(sc->bge_cdata.bge_rx_jumbo_ring_tag,
		    sc->bge_cdata.bge_rx_jumbo_ring_map, BUS_DMASYNC_POSTWRITE);

	while (rx_cons != rx_prod) {
		struct bge_rx_bd	*cur_rx;
		uint32_t		rxidx;
		struct mbuf		*m = NULL;
		uint16_t		vlan_tag = 0;
		int			have_tag = 0;

#ifdef DEVICE_POLLING
		if (if_getcapenable(ifp) & IFCAP_POLLING) {
			if (sc->rxcycles <= 0)
				break;
			sc->rxcycles--;
		}
#endif

		cur_rx = &sc->bge_ldata.bge_rx_return_ring[rx_cons];

		rxidx = cur_rx->bge_idx;
		BGE_INC(rx_cons, sc->bge_return_ring_cnt);

		if (if_getcapenable(ifp) & IFCAP_VLAN_HWTAGGING &&
		    cur_rx->bge_flags & BGE_RXBDFLAG_VLAN_TAG) {
			have_tag = 1;
			vlan_tag = cur_rx->bge_vlan_tag;
		}

		if (cur_rx->bge_flags & BGE_RXBDFLAG_JUMBO_RING) {
			jumbocnt++;
			m = sc->bge_cdata.bge_rx_jumbo_chain[rxidx];
			if (cur_rx->bge_flags & BGE_RXBDFLAG_ERROR) {
				bge_rxreuse_jumbo(sc, rxidx);
				continue;
			}
			if (bge_newbuf_jumbo(sc, rxidx) != 0) {
				bge_rxreuse_jumbo(sc, rxidx);
				if_inc_counter(ifp, IFCOUNTER_IQDROPS, 1);
				continue;
			}
			BGE_INC(sc->bge_jumbo, BGE_JUMBO_RX_RING_CNT);
		} else {
			stdcnt++;
			m = sc->bge_cdata.bge_rx_std_chain[rxidx];
			if (cur_rx->bge_flags & BGE_RXBDFLAG_ERROR) {
				bge_rxreuse_std(sc, rxidx);
				continue;
			}
			if (bge_newbuf_std(sc, rxidx) != 0) {
				bge_rxreuse_std(sc, rxidx);
				if_inc_counter(ifp, IFCOUNTER_IQDROPS, 1);
				continue;
			}
			BGE_INC(sc->bge_std, BGE_STD_RX_RING_CNT);
		}

		if_inc_counter(ifp, IFCOUNTER_IPACKETS, 1);
#ifndef __NO_STRICT_ALIGNMENT
		/*
		 * For architectures with strict alignment we must make sure
		 * the payload is aligned.
		 */
		if (sc->bge_flags & BGE_FLAG_RX_ALIGNBUG) {
			bcopy(m->m_data, m->m_data + ETHER_ALIGN,
			    cur_rx->bge_len);
			m->m_data += ETHER_ALIGN;
		}
#endif
		m->m_pkthdr.len = m->m_len = cur_rx->bge_len - ETHER_CRC_LEN;
		m->m_pkthdr.rcvif = ifp;

		if (if_getcapenable(ifp) & IFCAP_RXCSUM)
			bge_rxcsum(sc, cur_rx, m);

		/*
		 * If we received a packet with a vlan tag,
		 * attach that information to the packet.
		 */
		if (have_tag) {
			m->m_pkthdr.ether_vtag = vlan_tag;
			m->m_flags |= M_VLANTAG;
		}

		if (holdlck != 0) {
			BGE_UNLOCK(sc);
			if_input(ifp, m);
			BGE_LOCK(sc);
		} else
			if_input(ifp, m);
		rx_npkts++;

		if (!(if_getdrvflags(ifp) & IFF_DRV_RUNNING))
			return (rx_npkts);
	}

	bus_dmamap_sync(sc->bge_cdata.bge_rx_return_ring_tag,
	    sc->bge_cdata.bge_rx_return_ring_map, BUS_DMASYNC_PREREAD);
	if (stdcnt > 0)
		bus_dmamap_sync(sc->bge_cdata.bge_rx_std_ring_tag,
		    sc->bge_cdata.bge_rx_std_ring_map, BUS_DMASYNC_PREWRITE);

	if (jumbocnt > 0)
		bus_dmamap_sync(sc->bge_cdata.bge_rx_jumbo_ring_tag,
		    sc->bge_cdata.bge_rx_jumbo_ring_map, BUS_DMASYNC_PREWRITE);

	sc->bge_rx_saved_considx = rx_cons;
	bge_writembx(sc, BGE_MBX_RX_CONS0_LO, sc->bge_rx_saved_considx);
	if (stdcnt)
		bge_writembx(sc, BGE_MBX_RX_STD_PROD_LO, (sc->bge_std +
		    BGE_STD_RX_RING_CNT - 1) % BGE_STD_RX_RING_CNT);
	if (jumbocnt)
		bge_writembx(sc, BGE_MBX_RX_JUMBO_PROD_LO, (sc->bge_jumbo +
		    BGE_JUMBO_RX_RING_CNT - 1) % BGE_JUMBO_RX_RING_CNT);
#ifdef notyet
	/*
	 * This register wraps very quickly under heavy packet drops.
	 * If you need correct statistics, you can enable this check.
	 */
	if (BGE_IS_5705_PLUS(sc))
		if_incierrors(ifp, CSR_READ_4(sc, BGE_RXLP_LOCSTAT_IFIN_DROPS));
#endif
	return (rx_npkts);
}

static void
bge_rxcsum(struct bge_softc *sc, struct bge_rx_bd *cur_rx, struct mbuf *m)
{

	if (BGE_IS_5717_PLUS(sc)) {
		if ((cur_rx->bge_flags & BGE_RXBDFLAG_IPV6) == 0) {
			if (cur_rx->bge_flags & BGE_RXBDFLAG_IP_CSUM) {
				m->m_pkthdr.csum_flags |= CSUM_IP_CHECKED;
				if ((cur_rx->bge_error_flag &
				    BGE_RXERRFLAG_IP_CSUM_NOK) == 0)
					m->m_pkthdr.csum_flags |= CSUM_IP_VALID;
			}
			if (cur_rx->bge_flags & BGE_RXBDFLAG_TCP_UDP_CSUM) {
				m->m_pkthdr.csum_data =
				    cur_rx->bge_tcp_udp_csum;
				m->m_pkthdr.csum_flags |= CSUM_DATA_VALID |
				    CSUM_PSEUDO_HDR;
			}
		}
	} else {
		if (cur_rx->bge_flags & BGE_RXBDFLAG_IP_CSUM) {
			m->m_pkthdr.csum_flags |= CSUM_IP_CHECKED;
			if ((cur_rx->bge_ip_csum ^ 0xFFFF) == 0)
				m->m_pkthdr.csum_flags |= CSUM_IP_VALID;
		}
		if (cur_rx->bge_flags & BGE_RXBDFLAG_TCP_UDP_CSUM &&
		    m->m_pkthdr.len >= ETHER_MIN_NOPAD) {
			m->m_pkthdr.csum_data =
			    cur_rx->bge_tcp_udp_csum;
			m->m_pkthdr.csum_flags |= CSUM_DATA_VALID |
			    CSUM_PSEUDO_HDR;
		}
	}
}

static void
bge_txeof(struct bge_softc *sc, uint16_t tx_cons)
{
	struct bge_tx_bd *cur_tx;
	if_t ifp;

	BGE_LOCK_ASSERT(sc);

	/* Nothing to do. */
	if (sc->bge_tx_saved_considx == tx_cons)
		return;

	ifp = sc->bge_ifp;

	bus_dmamap_sync(sc->bge_cdata.bge_tx_ring_tag,
	    sc->bge_cdata.bge_tx_ring_map, BUS_DMASYNC_POSTWRITE);
	/*
	 * Go through our tx ring and free mbufs for those
	 * frames that have been sent.
	 */
	while (sc->bge_tx_saved_considx != tx_cons) {
		uint32_t		idx;

		idx = sc->bge_tx_saved_considx;
		cur_tx = &sc->bge_ldata.bge_tx_ring[idx];
		if (cur_tx->bge_flags & BGE_TXBDFLAG_END)
			if_inc_counter(ifp, IFCOUNTER_OPACKETS, 1);
		if (sc->bge_cdata.bge_tx_chain[idx] != NULL) {
			bus_dmamap_sync(sc->bge_cdata.bge_tx_mtag,
			    sc->bge_cdata.bge_tx_dmamap[idx],
			    BUS_DMASYNC_POSTWRITE);
			bus_dmamap_unload(sc->bge_cdata.bge_tx_mtag,
			    sc->bge_cdata.bge_tx_dmamap[idx]);
			m_freem(sc->bge_cdata.bge_tx_chain[idx]);
			sc->bge_cdata.bge_tx_chain[idx] = NULL;
		}
		sc->bge_txcnt--;
		BGE_INC(sc->bge_tx_saved_considx, BGE_TX_RING_CNT);
	}

	if_setdrvflagbits(ifp, 0, IFF_DRV_OACTIVE);
	if (sc->bge_txcnt == 0)
		sc->bge_timer = 0;
}

#ifdef DEVICE_POLLING
static int
bge_poll(if_t ifp, enum poll_cmd cmd, int count)
{
	struct bge_softc *sc = if_getsoftc(ifp);
	uint16_t rx_prod, tx_cons;
	uint32_t statusword;
	int rx_npkts = 0;

	BGE_LOCK(sc);
	if (!(if_getdrvflags(ifp) & IFF_DRV_RUNNING)) {
		BGE_UNLOCK(sc);
		return (rx_npkts);
	}

	bus_dmamap_sync(sc->bge_cdata.bge_status_tag,
	    sc->bge_cdata.bge_status_map,
	    BUS_DMASYNC_POSTREAD | BUS_DMASYNC_POSTWRITE);
	/* Fetch updates from the status block. */
	rx_prod = sc->bge_ldata.bge_status_block->bge_idx[0].bge_rx_prod_idx;
	tx_cons = sc->bge_ldata.bge_status_block->bge_idx[0].bge_tx_cons_idx;

	statusword = sc->bge_ldata.bge_status_block->bge_status;
	/* Clear the status so the next pass only sees the changes. */
	sc->bge_ldata.bge_status_block->bge_status = 0;

	bus_dmamap_sync(sc->bge_cdata.bge_status_tag,
	    sc->bge_cdata.bge_status_map,
	    BUS_DMASYNC_PREREAD | BUS_DMASYNC_PREWRITE);

	/* Note link event. It will be processed by POLL_AND_CHECK_STATUS. */
	if (statusword & BGE_STATFLAG_LINKSTATE_CHANGED)
		sc->bge_link_evt++;

	if (cmd == POLL_AND_CHECK_STATUS)
		if ((sc->bge_asicrev == BGE_ASICREV_BCM5700 &&
		    sc->bge_chipid != BGE_CHIPID_BCM5700_B2) ||
		    sc->bge_link_evt || (sc->bge_flags & BGE_FLAG_TBI))
			bge_link_upd(sc);

	sc->rxcycles = count;
	rx_npkts = bge_rxeof(sc, rx_prod, 1);
	if (!(if_getdrvflags(ifp) & IFF_DRV_RUNNING)) {
		BGE_UNLOCK(sc);
		return (rx_npkts);
	}
	bge_txeof(sc, tx_cons);
	if (!if_sendq_empty(ifp))
		bge_start_locked(ifp);

	BGE_UNLOCK(sc);
	return (rx_npkts);
}
#endif /* DEVICE_POLLING */

static int
bge_msi_intr(void *arg)
{
	struct bge_softc *sc;

	sc = (struct bge_softc *)arg;
	/*
	 * This interrupt is not shared and controller already
	 * disabled further interrupt.
	 */
	taskqueue_enqueue(sc->bge_tq, &sc->bge_intr_task);
	return (FILTER_HANDLED);
}

static void
bge_intr_task(void *arg, int pending)
{
	struct bge_softc *sc;
	if_t ifp;
	uint32_t status, status_tag;
	uint16_t rx_prod, tx_cons;

	sc = (struct bge_softc *)arg;
	ifp = sc->bge_ifp;

	BGE_LOCK(sc);
	if ((if_getdrvflags(ifp) & IFF_DRV_RUNNING) == 0) {
		BGE_UNLOCK(sc);
		return;
	}

	/* Get updated status block. */
	bus_dmamap_sync(sc->bge_cdata.bge_status_tag,
	    sc->bge_cdata.bge_status_map,
	    BUS_DMASYNC_POSTREAD | BUS_DMASYNC_POSTWRITE);

	/* Save producer/consumer indices. */
	rx_prod = sc->bge_ldata.bge_status_block->bge_idx[0].bge_rx_prod_idx;
	tx_cons = sc->bge_ldata.bge_status_block->bge_idx[0].bge_tx_cons_idx;
	status = sc->bge_ldata.bge_status_block->bge_status;
	status_tag = sc->bge_ldata.bge_status_block->bge_status_tag << 24;
	/* Dirty the status flag. */
	sc->bge_ldata.bge_status_block->bge_status = 0;
	bus_dmamap_sync(sc->bge_cdata.bge_status_tag,
	    sc->bge_cdata.bge_status_map,
	    BUS_DMASYNC_PREREAD | BUS_DMASYNC_PREWRITE);
	if ((sc->bge_flags & BGE_FLAG_TAGGED_STATUS) == 0)
		status_tag = 0;

	if ((status & BGE_STATFLAG_LINKSTATE_CHANGED) != 0)
		bge_link_upd(sc);

	/* Let controller work. */
	bge_writembx(sc, BGE_MBX_IRQ0_LO, status_tag);

	if (if_getdrvflags(ifp) & IFF_DRV_RUNNING &&
	    sc->bge_rx_saved_considx != rx_prod) {
		/* Check RX return ring producer/consumer. */
		BGE_UNLOCK(sc);
		bge_rxeof(sc, rx_prod, 0);
		BGE_LOCK(sc);
	}
	if (if_getdrvflags(ifp) & IFF_DRV_RUNNING) {
		/* Check TX ring producer/consumer. */
		bge_txeof(sc, tx_cons);
		if (!if_sendq_empty(ifp))
			bge_start_locked(ifp);
	}
	BGE_UNLOCK(sc);
}

static void
bge_intr(void *xsc)
{
	struct bge_softc *sc;
	if_t ifp;
	uint32_t statusword;
	uint16_t rx_prod, tx_cons;

	sc = xsc;

	BGE_LOCK(sc);

	ifp = sc->bge_ifp;

#ifdef DEVICE_POLLING
	if (if_getcapenable(ifp) & IFCAP_POLLING) {
		BGE_UNLOCK(sc);
		return;
	}
#endif

	/*
	 * Ack the interrupt by writing something to BGE_MBX_IRQ0_LO.  Don't
	 * disable interrupts by writing nonzero like we used to, since with
	 * our current organization this just gives complications and
	 * pessimizations for re-enabling interrupts.  We used to have races
	 * instead of the necessary complications.  Disabling interrupts
	 * would just reduce the chance of a status update while we are
	 * running (by switching to the interrupt-mode coalescence
	 * parameters), but this chance is already very low so it is more
	 * efficient to get another interrupt than prevent it.
	 *
	 * We do the ack first to ensure another interrupt if there is a
	 * status update after the ack.  We don't check for the status
	 * changing later because it is more efficient to get another
	 * interrupt than prevent it, not quite as above (not checking is
	 * a smaller optimization than not toggling the interrupt enable,
	 * since checking doesn't involve PCI accesses and toggling require
	 * the status check).  So toggling would probably be a pessimization
	 * even with MSI.  It would only be needed for using a task queue.
	 */
	bge_writembx(sc, BGE_MBX_IRQ0_LO, 0);

	/*
	 * Do the mandatory PCI flush as well as get the link status.
	 */
	statusword = CSR_READ_4(sc, BGE_MAC_STS) & BGE_MACSTAT_LINK_CHANGED;

	/* Make sure the descriptor ring indexes are coherent. */
	bus_dmamap_sync(sc->bge_cdata.bge_status_tag,
	    sc->bge_cdata.bge_status_map,
	    BUS_DMASYNC_POSTREAD | BUS_DMASYNC_POSTWRITE);
	rx_prod = sc->bge_ldata.bge_status_block->bge_idx[0].bge_rx_prod_idx;
	tx_cons = sc->bge_ldata.bge_status_block->bge_idx[0].bge_tx_cons_idx;
	sc->bge_ldata.bge_status_block->bge_status = 0;
	bus_dmamap_sync(sc->bge_cdata.bge_status_tag,
	    sc->bge_cdata.bge_status_map,
	    BUS_DMASYNC_PREREAD | BUS_DMASYNC_PREWRITE);

	if ((sc->bge_asicrev == BGE_ASICREV_BCM5700 &&
	    sc->bge_chipid != BGE_CHIPID_BCM5700_B2) ||
	    statusword || sc->bge_link_evt)
		bge_link_upd(sc);

	if (if_getdrvflags(ifp) & IFF_DRV_RUNNING) {
		/* Check RX return ring producer/consumer. */
		bge_rxeof(sc, rx_prod, 1);
	}

	if (if_getdrvflags(ifp) & IFF_DRV_RUNNING) {
		/* Check TX ring producer/consumer. */
		bge_txeof(sc, tx_cons);
	}

	if (if_getdrvflags(ifp) & IFF_DRV_RUNNING &&
	    !if_sendq_empty(ifp))
		bge_start_locked(ifp);

	BGE_UNLOCK(sc);
}

static void
bge_asf_driver_up(struct bge_softc *sc)
{
	if (sc->bge_asf_mode & ASF_STACKUP) {
		/* Send ASF heartbeat aprox. every 2s */
		if (sc->bge_asf_count)
			sc->bge_asf_count --;
		else {
			sc->bge_asf_count = 2;
			bge_writemem_ind(sc, BGE_SRAM_FW_CMD_MB,
			    BGE_FW_CMD_DRV_ALIVE);
			bge_writemem_ind(sc, BGE_SRAM_FW_CMD_LEN_MB, 4);
			bge_writemem_ind(sc, BGE_SRAM_FW_CMD_DATA_MB,
			    BGE_FW_HB_TIMEOUT_SEC);
			CSR_WRITE_4(sc, BGE_RX_CPU_EVENT,
			    CSR_READ_4(sc, BGE_RX_CPU_EVENT) |
			    BGE_RX_CPU_DRV_EVENT);
		}
	}
}

static void
bge_tick(void *xsc)
{
	struct bge_softc *sc = xsc;
	struct mii_data *mii = NULL;

	BGE_LOCK_ASSERT(sc);

	/* Synchronize with possible callout reset/stop. */
	if (callout_pending(&sc->bge_stat_ch) ||
	    !callout_active(&sc->bge_stat_ch))
		return;

	if (BGE_IS_5705_PLUS(sc))
		bge_stats_update_regs(sc);
	else
		bge_stats_update(sc);

	/* XXX Add APE heartbeat check here? */

	if ((sc->bge_flags & BGE_FLAG_TBI) == 0) {
		mii = device_get_softc(sc->bge_miibus);
		/*
		 * Do not touch PHY if we have link up. This could break
		 * IPMI/ASF mode or produce extra input errors
		 * (extra errors was reported for bcm5701 & bcm5704).
		 */
		if (!sc->bge_link)
			mii_tick(mii);
	} else {
		/*
		 * Since in TBI mode auto-polling can't be used we should poll
		 * link status manually. Here we register pending link event
		 * and trigger interrupt.
		 */
#ifdef DEVICE_POLLING
		/* In polling mode we poll link state in bge_poll(). */
		if (!(if_getcapenable(sc->bge_ifp) & IFCAP_POLLING))
#endif
		{
		sc->bge_link_evt++;
		if (sc->bge_asicrev == BGE_ASICREV_BCM5700 ||
		    sc->bge_flags & BGE_FLAG_5788)
			BGE_SETBIT(sc, BGE_MISC_LOCAL_CTL, BGE_MLC_INTR_SET);
		else
			BGE_SETBIT(sc, BGE_HCC_MODE, BGE_HCCMODE_COAL_NOW);
		}
	}

	bge_asf_driver_up(sc);
	bge_watchdog(sc);

	callout_reset(&sc->bge_stat_ch, hz, bge_tick, sc);
}

static void
bge_stats_update_regs(struct bge_softc *sc)
{
	if_t ifp;
	struct bge_mac_stats *stats;
	uint32_t val;

	ifp = sc->bge_ifp;
	stats = &sc->bge_mac_stats;

	stats->ifHCOutOctets +=
	    CSR_READ_4(sc, BGE_TX_MAC_STATS_OCTETS);
	stats->etherStatsCollisions +=
	    CSR_READ_4(sc, BGE_TX_MAC_STATS_COLLS);
	stats->outXonSent +=
	    CSR_READ_4(sc, BGE_TX_MAC_STATS_XON_SENT);
	stats->outXoffSent +=
	    CSR_READ_4(sc, BGE_TX_MAC_STATS_XOFF_SENT);
	stats->dot3StatsInternalMacTransmitErrors +=
	    CSR_READ_4(sc, BGE_TX_MAC_STATS_ERRORS);
	stats->dot3StatsSingleCollisionFrames +=
	    CSR_READ_4(sc, BGE_TX_MAC_STATS_SINGLE_COLL);
	stats->dot3StatsMultipleCollisionFrames +=
	    CSR_READ_4(sc, BGE_TX_MAC_STATS_MULTI_COLL);
	stats->dot3StatsDeferredTransmissions +=
	    CSR_READ_4(sc, BGE_TX_MAC_STATS_DEFERRED);
	stats->dot3StatsExcessiveCollisions +=
	    CSR_READ_4(sc, BGE_TX_MAC_STATS_EXCESS_COLL);
	stats->dot3StatsLateCollisions +=
	    CSR_READ_4(sc, BGE_TX_MAC_STATS_LATE_COLL);
	stats->ifHCOutUcastPkts +=
	    CSR_READ_4(sc, BGE_TX_MAC_STATS_UCAST);
	stats->ifHCOutMulticastPkts +=
	    CSR_READ_4(sc, BGE_TX_MAC_STATS_MCAST);
	stats->ifHCOutBroadcastPkts +=
	    CSR_READ_4(sc, BGE_TX_MAC_STATS_BCAST);

	stats->ifHCInOctets +=
	    CSR_READ_4(sc, BGE_RX_MAC_STATS_OCTESTS);
	stats->etherStatsFragments +=
	    CSR_READ_4(sc, BGE_RX_MAC_STATS_FRAGMENTS);
	stats->ifHCInUcastPkts +=
	    CSR_READ_4(sc, BGE_RX_MAC_STATS_UCAST);
	stats->ifHCInMulticastPkts +=
	    CSR_READ_4(sc, BGE_RX_MAC_STATS_MCAST);
	stats->ifHCInBroadcastPkts +=
	    CSR_READ_4(sc, BGE_RX_MAC_STATS_BCAST);
	stats->dot3StatsFCSErrors +=
	    CSR_READ_4(sc, BGE_RX_MAC_STATS_FCS_ERRORS);
	stats->dot3StatsAlignmentErrors +=
	    CSR_READ_4(sc, BGE_RX_MAC_STATS_ALGIN_ERRORS);
	stats->xonPauseFramesReceived +=
	    CSR_READ_4(sc, BGE_RX_MAC_STATS_XON_RCVD);
	stats->xoffPauseFramesReceived +=
	    CSR_READ_4(sc, BGE_RX_MAC_STATS_XOFF_RCVD);
	stats->macControlFramesReceived +=
	    CSR_READ_4(sc, BGE_RX_MAC_STATS_CTRL_RCVD);
	stats->xoffStateEntered +=
	    CSR_READ_4(sc, BGE_RX_MAC_STATS_XOFF_ENTERED);
	stats->dot3StatsFramesTooLong +=
	    CSR_READ_4(sc, BGE_RX_MAC_STATS_FRAME_TOO_LONG);
	stats->etherStatsJabbers +=
	    CSR_READ_4(sc, BGE_RX_MAC_STATS_JABBERS);
	stats->etherStatsUndersizePkts +=
	    CSR_READ_4(sc, BGE_RX_MAC_STATS_UNDERSIZE);

	stats->FramesDroppedDueToFilters +=
	    CSR_READ_4(sc, BGE_RXLP_LOCSTAT_FILTDROP);
	stats->DmaWriteQueueFull +=
	    CSR_READ_4(sc, BGE_RXLP_LOCSTAT_DMA_WRQ_FULL);
	stats->DmaWriteHighPriQueueFull +=
	    CSR_READ_4(sc, BGE_RXLP_LOCSTAT_DMA_HPWRQ_FULL);
	stats->NoMoreRxBDs +=
	    CSR_READ_4(sc, BGE_RXLP_LOCSTAT_OUT_OF_BDS);
	/*
	 * XXX
	 * Unlike other controllers, BGE_RXLP_LOCSTAT_IFIN_DROPS
	 * counter of BCM5717, BCM5718, BCM5719 A0 and BCM5720 A0
	 * includes number of unwanted multicast frames.  This comes
	 * from silicon bug and known workaround to get rough(not
	 * exact) counter is to enable interrupt on MBUF low water
	 * attention.  This can be accomplished by setting
	 * BGE_HCCMODE_ATTN bit of BGE_HCC_MODE,
	 * BGE_BMANMODE_LOMBUF_ATTN bit of BGE_BMAN_MODE and
	 * BGE_MODECTL_FLOWCTL_ATTN_INTR bit of BGE_MODE_CTL.
	 * However that change would generate more interrupts and
	 * there are still possibilities of losing multiple frames
	 * during BGE_MODECTL_FLOWCTL_ATTN_INTR interrupt handling.
	 * Given that the workaround still would not get correct
	 * counter I don't think it's worth to implement it.  So
	 * ignore reading the counter on controllers that have the
	 * silicon bug.
	 */
	if (sc->bge_asicrev != BGE_ASICREV_BCM5717 &&
	    sc->bge_chipid != BGE_CHIPID_BCM5719_A0 &&
	    sc->bge_chipid != BGE_CHIPID_BCM5720_A0)
		stats->InputDiscards +=
		    CSR_READ_4(sc, BGE_RXLP_LOCSTAT_IFIN_DROPS);
	stats->InputErrors +=
	    CSR_READ_4(sc, BGE_RXLP_LOCSTAT_IFIN_ERRORS);
	stats->RecvThresholdHit +=
	    CSR_READ_4(sc, BGE_RXLP_LOCSTAT_RXTHRESH_HIT);

	if (sc->bge_flags & BGE_FLAG_RDMA_BUG) {
		/*
		 * If controller transmitted more than BGE_NUM_RDMA_CHANNELS
		 * frames, it's safe to disable workaround for DMA engine's
		 * miscalculation of TXMBUF space.
		 */
		if (stats->ifHCOutUcastPkts + stats->ifHCOutMulticastPkts +
		    stats->ifHCOutBroadcastPkts > BGE_NUM_RDMA_CHANNELS) {
			val = CSR_READ_4(sc, BGE_RDMA_LSO_CRPTEN_CTRL);
			if (sc->bge_asicrev == BGE_ASICREV_BCM5719)
				val &= ~BGE_RDMA_TX_LENGTH_WA_5719;
			else
				val &= ~BGE_RDMA_TX_LENGTH_WA_5720;
			CSR_WRITE_4(sc, BGE_RDMA_LSO_CRPTEN_CTRL, val);
			sc->bge_flags &= ~BGE_FLAG_RDMA_BUG;
		}
	}
}

static void
bge_stats_clear_regs(struct bge_softc *sc)
{

	CSR_READ_4(sc, BGE_TX_MAC_STATS_OCTETS);
	CSR_READ_4(sc, BGE_TX_MAC_STATS_COLLS);
	CSR_READ_4(sc, BGE_TX_MAC_STATS_XON_SENT);
	CSR_READ_4(sc, BGE_TX_MAC_STATS_XOFF_SENT);
	CSR_READ_4(sc, BGE_TX_MAC_STATS_ERRORS);
	CSR_READ_4(sc, BGE_TX_MAC_STATS_SINGLE_COLL);
	CSR_READ_4(sc, BGE_TX_MAC_STATS_MULTI_COLL);
	CSR_READ_4(sc, BGE_TX_MAC_STATS_DEFERRED);
	CSR_READ_4(sc, BGE_TX_MAC_STATS_EXCESS_COLL);
	CSR_READ_4(sc, BGE_TX_MAC_STATS_LATE_COLL);
	CSR_READ_4(sc, BGE_TX_MAC_STATS_UCAST);
	CSR_READ_4(sc, BGE_TX_MAC_STATS_MCAST);
	CSR_READ_4(sc, BGE_TX_MAC_STATS_BCAST);

	CSR_READ_4(sc, BGE_RX_MAC_STATS_OCTESTS);
	CSR_READ_4(sc, BGE_RX_MAC_STATS_FRAGMENTS);
	CSR_READ_4(sc, BGE_RX_MAC_STATS_UCAST);
	CSR_READ_4(sc, BGE_RX_MAC_STATS_MCAST);
	CSR_READ_4(sc, BGE_RX_MAC_STATS_BCAST);
	CSR_READ_4(sc, BGE_RX_MAC_STATS_FCS_ERRORS);
	CSR_READ_4(sc, BGE_RX_MAC_STATS_ALGIN_ERRORS);
	CSR_READ_4(sc, BGE_RX_MAC_STATS_XON_RCVD);
	CSR_READ_4(sc, BGE_RX_MAC_STATS_XOFF_RCVD);
	CSR_READ_4(sc, BGE_RX_MAC_STATS_CTRL_RCVD);
	CSR_READ_4(sc, BGE_RX_MAC_STATS_XOFF_ENTERED);
	CSR_READ_4(sc, BGE_RX_MAC_STATS_FRAME_TOO_LONG);
	CSR_READ_4(sc, BGE_RX_MAC_STATS_JABBERS);
	CSR_READ_4(sc, BGE_RX_MAC_STATS_UNDERSIZE);

	CSR_READ_4(sc, BGE_RXLP_LOCSTAT_FILTDROP);
	CSR_READ_4(sc, BGE_RXLP_LOCSTAT_DMA_WRQ_FULL);
	CSR_READ_4(sc, BGE_RXLP_LOCSTAT_DMA_HPWRQ_FULL);
	CSR_READ_4(sc, BGE_RXLP_LOCSTAT_OUT_OF_BDS);
	CSR_READ_4(sc, BGE_RXLP_LOCSTAT_IFIN_DROPS);
	CSR_READ_4(sc, BGE_RXLP_LOCSTAT_IFIN_ERRORS);
	CSR_READ_4(sc, BGE_RXLP_LOCSTAT_RXTHRESH_HIT);
}

static void
bge_stats_update(struct bge_softc *sc)
{
	if_t ifp;
	bus_size_t stats;
	uint32_t cnt;	/* current register value */

	ifp = sc->bge_ifp;

	stats = BGE_MEMWIN_START + BGE_STATS_BLOCK;

#define	READ_STAT(sc, stats, stat) \
	CSR_READ_4(sc, stats + offsetof(struct bge_stats, stat))

	cnt = READ_STAT(sc, stats, txstats.etherStatsCollisions.bge_addr_lo);
	if_inc_counter(ifp, IFCOUNTER_COLLISIONS, cnt - sc->bge_tx_collisions);
	sc->bge_tx_collisions = cnt;

	cnt = READ_STAT(sc, stats, nicNoMoreRxBDs.bge_addr_lo);
	if_inc_counter(ifp, IFCOUNTER_IERRORS, cnt - sc->bge_rx_nobds);
	sc->bge_rx_nobds = cnt;
	cnt = READ_STAT(sc, stats, ifInErrors.bge_addr_lo);
	if_inc_counter(ifp, IFCOUNTER_IERRORS, cnt - sc->bge_rx_inerrs);
	sc->bge_rx_inerrs = cnt;
	cnt = READ_STAT(sc, stats, ifInDiscards.bge_addr_lo);
	if_inc_counter(ifp, IFCOUNTER_IERRORS, cnt - sc->bge_rx_discards);
	sc->bge_rx_discards = cnt;

	cnt = READ_STAT(sc, stats, txstats.ifOutDiscards.bge_addr_lo);
	if_inc_counter(ifp, IFCOUNTER_OERRORS, cnt - sc->bge_tx_discards);
	sc->bge_tx_discards = cnt;

#undef	READ_STAT
}

/*
 * Pad outbound frame to ETHER_MIN_NOPAD for an unusual reason.
 * The bge hardware will pad out Tx runts to ETHER_MIN_NOPAD,
 * but when such padded frames employ the bge IP/TCP checksum offload,
 * the hardware checksum assist gives incorrect results (possibly
 * from incorporating its own padding into the UDP/TCP checksum; who knows).
 * If we pad such runts with zeros, the onboard checksum comes out correct.
 */
static __inline int
bge_cksum_pad(struct mbuf *m)
{
	int padlen = ETHER_MIN_NOPAD - m->m_pkthdr.len;
	struct mbuf *last;

	/* If there's only the packet-header and we can pad there, use it. */
	if (m->m_pkthdr.len == m->m_len && M_WRITABLE(m) &&
	    M_TRAILINGSPACE(m) >= padlen) {
		last = m;
	} else {
		/*
		 * Walk packet chain to find last mbuf. We will either
		 * pad there, or append a new mbuf and pad it.
		 */
		for (last = m; last->m_next != NULL; last = last->m_next);
		if (!(M_WRITABLE(last) && M_TRAILINGSPACE(last) >= padlen)) {
			/* Allocate new empty mbuf, pad it. Compact later. */
			struct mbuf *n;

			MGET(n, M_NOWAIT, MT_DATA);
			if (n == NULL)
				return (ENOBUFS);
			n->m_len = 0;
			last->m_next = n;
			last = n;
		}
	}

	/* Now zero the pad area, to avoid the bge cksum-assist bug. */
	memset(mtod(last, caddr_t) + last->m_len, 0, padlen);
	last->m_len += padlen;
	m->m_pkthdr.len += padlen;

	return (0);
}

static struct mbuf *
bge_check_short_dma(struct mbuf *m)
{
	struct mbuf *n;
	int found;

	/*
	 * If device receive two back-to-back send BDs with less than
	 * or equal to 8 total bytes then the device may hang.  The two
	 * back-to-back send BDs must in the same frame for this failure
	 * to occur.  Scan mbuf chains and see whether two back-to-back
	 * send BDs are there. If this is the case, allocate new mbuf
	 * and copy the frame to workaround the silicon bug.
	 */
	for (n = m, found = 0; n != NULL; n = n->m_next) {
		if (n->m_len < 8) {
			found++;
			if (found > 1)
				break;
			continue;
		}
		found = 0;
	}

	if (found > 1) {
		n = m_defrag(m, M_NOWAIT);
		if (n == NULL)
			m_freem(m);
	} else
		n = m;
	return (n);
}

static struct mbuf *
bge_setup_tso(struct bge_softc *sc, struct mbuf *m, uint16_t *mss,
    uint16_t *flags)
{
	struct ip *ip;
	struct tcphdr *tcp;
	struct mbuf *n;
	uint16_t hlen;
	uint32_t poff;

	if (M_WRITABLE(m) == 0) {
		/* Get a writable copy. */
		n = m_dup(m, M_NOWAIT);
		m_freem(m);
		if (n == NULL)
			return (NULL);
		m = n;
	}
	m = m_pullup(m, sizeof(struct ether_header) + sizeof(struct ip));
	if (m == NULL)
		return (NULL);
	ip = (struct ip *)(mtod(m, char *) + sizeof(struct ether_header));
	poff = sizeof(struct ether_header) + (ip->ip_hl << 2);
	m = m_pullup(m, poff + sizeof(struct tcphdr));
	if (m == NULL)
		return (NULL);
	tcp = (struct tcphdr *)(mtod(m, char *) + poff);
	m = m_pullup(m, poff + (tcp->th_off << 2));
	if (m == NULL)
		return (NULL);
	/*
	 * It seems controller doesn't modify IP length and TCP pseudo
	 * checksum. These checksum computed by upper stack should be 0.
	 */
	*mss = m->m_pkthdr.tso_segsz;
	ip = (struct ip *)(mtod(m, char *) + sizeof(struct ether_header));
	ip->ip_sum = 0;
	ip->ip_len = htons(*mss + (ip->ip_hl << 2) + (tcp->th_off << 2));
	/* Clear pseudo checksum computed by TCP stack. */
	tcp = (struct tcphdr *)(mtod(m, char *) + poff);
	tcp->th_sum = 0;
	/*
	 * Broadcom controllers uses different descriptor format for
	 * TSO depending on ASIC revision. Due to TSO-capable firmware
	 * license issue and lower performance of firmware based TSO
	 * we only support hardware based TSO.
	 */
	/* Calculate header length, incl. TCP/IP options, in 32 bit units. */
	hlen = ((ip->ip_hl << 2) + (tcp->th_off << 2)) >> 2;
	if (sc->bge_flags & BGE_FLAG_TSO3) {
		/*
		 * For BCM5717 and newer controllers, hardware based TSO
		 * uses the 14 lower bits of the bge_mss field to store the
		 * MSS and the upper 2 bits to store the lowest 2 bits of
		 * the IP/TCP header length.  The upper 6 bits of the header
		 * length are stored in the bge_flags[14:10,4] field.  Jumbo
		 * frames are supported.
		 */
		*mss |= ((hlen & 0x3) << 14);
		*flags |= ((hlen & 0xF8) << 7) | ((hlen & 0x4) << 2);
	} else {
		/*
		 * For BCM5755 and newer controllers, hardware based TSO uses
		 * the lower 11	bits to store the MSS and the upper 5 bits to
		 * store the IP/TCP header length. Jumbo frames are not
		 * supported.
		 */
		*mss |= (hlen << 11);
	}
	return (m);
}

/*
 * Encapsulate an mbuf chain in the tx ring  by coupling the mbuf data
 * pointers to descriptors.
 */
static int
bge_encap(struct bge_softc *sc, struct mbuf **m_head, uint32_t *txidx)
{
	bus_dma_segment_t	segs[BGE_NSEG_NEW];
	bus_dmamap_t		map;
	struct bge_tx_bd	*d;
	struct mbuf		*m = *m_head;
	uint32_t		idx = *txidx;
	uint16_t		csum_flags, mss, vlan_tag;
	int			nsegs, i, error;

	csum_flags = 0;
	mss = 0;
	vlan_tag = 0;
	if ((sc->bge_flags & BGE_FLAG_SHORT_DMA_BUG) != 0 &&
	    m->m_next != NULL) {
		*m_head = bge_check_short_dma(m);
		if (*m_head == NULL)
			return (ENOBUFS);
		m = *m_head;
	}
	if ((m->m_pkthdr.csum_flags & CSUM_TSO) != 0) {
		*m_head = m = bge_setup_tso(sc, m, &mss, &csum_flags);
		if (*m_head == NULL)
			return (ENOBUFS);
		csum_flags |= BGE_TXBDFLAG_CPU_PRE_DMA |
		    BGE_TXBDFLAG_CPU_POST_DMA;
	} else if ((m->m_pkthdr.csum_flags & sc->bge_csum_features) != 0) {
		if (m->m_pkthdr.csum_flags & CSUM_IP)
			csum_flags |= BGE_TXBDFLAG_IP_CSUM;
		if (m->m_pkthdr.csum_flags & (CSUM_TCP | CSUM_UDP)) {
			csum_flags |= BGE_TXBDFLAG_TCP_UDP_CSUM;
			if (m->m_pkthdr.len < ETHER_MIN_NOPAD &&
			    (error = bge_cksum_pad(m)) != 0) {
				m_freem(m);
				*m_head = NULL;
				return (error);
			}
		}
	}

	if ((m->m_pkthdr.csum_flags & CSUM_TSO) == 0) {
		if (sc->bge_flags & BGE_FLAG_JUMBO_FRAME &&
		    m->m_pkthdr.len > ETHER_MAX_LEN)
			csum_flags |= BGE_TXBDFLAG_JUMBO_FRAME;
		if (sc->bge_forced_collapse > 0 &&
		    (sc->bge_flags & BGE_FLAG_PCIE) != 0 && m->m_next != NULL) {
			/*
			 * Forcedly collapse mbuf chains to overcome hardware
			 * limitation which only support a single outstanding
			 * DMA read operation.
			 */
			if (sc->bge_forced_collapse == 1)
				m = m_defrag(m, M_NOWAIT);
			else
				m = m_collapse(m, M_NOWAIT,
				    sc->bge_forced_collapse);
			if (m == NULL)
				m = *m_head;
			*m_head = m;
		}
	}

	map = sc->bge_cdata.bge_tx_dmamap[idx];
	error = bus_dmamap_load_mbuf_sg(sc->bge_cdata.bge_tx_mtag, map, m, segs,
	    &nsegs, BUS_DMA_NOWAIT);
	if (error == EFBIG) {
		m = m_collapse(m, M_NOWAIT, BGE_NSEG_NEW);
		if (m == NULL) {
			m_freem(*m_head);
			*m_head = NULL;
			return (ENOBUFS);
		}
		*m_head = m;
		error = bus_dmamap_load_mbuf_sg(sc->bge_cdata.bge_tx_mtag, map,
		    m, segs, &nsegs, BUS_DMA_NOWAIT);
		if (error) {
			m_freem(m);
			*m_head = NULL;
			return (error);
		}
	} else if (error != 0)
		return (error);

	/* Check if we have enough free send BDs. */
	if (sc->bge_txcnt + nsegs >= BGE_TX_RING_CNT) {
		bus_dmamap_unload(sc->bge_cdata.bge_tx_mtag, map);
		return (ENOBUFS);
	}

	bus_dmamap_sync(sc->bge_cdata.bge_tx_mtag, map, BUS_DMASYNC_PREWRITE);

	if (m->m_flags & M_VLANTAG) {
		csum_flags |= BGE_TXBDFLAG_VLAN_TAG;
		vlan_tag = m->m_pkthdr.ether_vtag;
	}

	if (sc->bge_asicrev == BGE_ASICREV_BCM5762 &&
	    (m->m_pkthdr.csum_flags & CSUM_TSO) != 0) {
		/*
		 * 5725 family of devices corrupts TSO packets when TSO DMA
		 * buffers cross into regions which are within MSS bytes of
		 * a 4GB boundary.  If we encounter the condition, drop the
		 * packet.
		 */
		for (i = 0; ; i++) {
			d = &sc->bge_ldata.bge_tx_ring[idx];
			d->bge_addr.bge_addr_lo = BGE_ADDR_LO(segs[i].ds_addr);
			d->bge_addr.bge_addr_hi = BGE_ADDR_HI(segs[i].ds_addr);
			d->bge_len = segs[i].ds_len;
			if (d->bge_addr.bge_addr_lo + segs[i].ds_len + mss <
			    d->bge_addr.bge_addr_lo)
				break;
			d->bge_flags = csum_flags;
			d->bge_vlan_tag = vlan_tag;
			d->bge_mss = mss;
			if (i == nsegs - 1)
				break;
			BGE_INC(idx, BGE_TX_RING_CNT);
		}
		if (i != nsegs - 1) {
			bus_dmamap_sync(sc->bge_cdata.bge_tx_mtag, map,
			    BUS_DMASYNC_POSTWRITE);
			bus_dmamap_unload(sc->bge_cdata.bge_tx_mtag, map);
			m_freem(*m_head);
			*m_head = NULL;
			return (EIO);
		}
	} else {
		for (i = 0; ; i++) {
			d = &sc->bge_ldata.bge_tx_ring[idx];
			d->bge_addr.bge_addr_lo = BGE_ADDR_LO(segs[i].ds_addr);
			d->bge_addr.bge_addr_hi = BGE_ADDR_HI(segs[i].ds_addr);
			d->bge_len = segs[i].ds_len;
			d->bge_flags = csum_flags;
			d->bge_vlan_tag = vlan_tag;
			d->bge_mss = mss;
			if (i == nsegs - 1)
				break;
			BGE_INC(idx, BGE_TX_RING_CNT);
		}
	}

	/* Mark the last segment as end of packet... */
	d->bge_flags |= BGE_TXBDFLAG_END;

	/*
	 * Insure that the map for this transmission
	 * is placed at the array index of the last descriptor
	 * in this chain.
	 */
	sc->bge_cdata.bge_tx_dmamap[*txidx] = sc->bge_cdata.bge_tx_dmamap[idx];
	sc->bge_cdata.bge_tx_dmamap[idx] = map;
	sc->bge_cdata.bge_tx_chain[idx] = m;
	sc->bge_txcnt += nsegs;

	BGE_INC(idx, BGE_TX_RING_CNT);
	*txidx = idx;

	return (0);
}

/*
 * Main transmit routine. To avoid having to do mbuf copies, we put pointers
 * to the mbuf data regions directly in the transmit descriptors.
 */
static void
bge_start_locked(if_t ifp)
{
	struct bge_softc *sc;
	struct mbuf *m_head;
	uint32_t prodidx;
	int count;

	sc = if_getsoftc(ifp);
	BGE_LOCK_ASSERT(sc);

	if (!sc->bge_link ||
	    (if_getdrvflags(ifp) & (IFF_DRV_RUNNING | IFF_DRV_OACTIVE)) !=
	    IFF_DRV_RUNNING)
		return;

	prodidx = sc->bge_tx_prodidx;

	for (count = 0; !if_sendq_empty(ifp);) {
		if (sc->bge_txcnt > BGE_TX_RING_CNT - 16) {
			if_setdrvflagbits(ifp, IFF_DRV_OACTIVE, 0);
			break;
		}
		m_head = if_dequeue(ifp);
		if (m_head == NULL)
			break;

		/*
		 * Pack the data into the transmit ring. If we
		 * don't have room, set the OACTIVE flag and wait
		 * for the NIC to drain the ring.
		 */
		if (bge_encap(sc, &m_head, &prodidx)) {
			if (m_head == NULL)
				break;
			if_sendq_prepend(ifp, m_head);
			if_setdrvflagbits(ifp, IFF_DRV_OACTIVE, 0);
			break;
		}
		++count;

		/*
		 * If there's a BPF listener, bounce a copy of this frame
		 * to him.
		 */
		if_bpfmtap(ifp, m_head);
	}

	if (count > 0)
		bge_start_tx(sc, prodidx);
}

static void
bge_start_tx(struct bge_softc *sc, uint32_t prodidx)
{

	bus_dmamap_sync(sc->bge_cdata.bge_tx_ring_tag,
	    sc->bge_cdata.bge_tx_ring_map, BUS_DMASYNC_PREWRITE);
	/* Transmit. */
	bge_writembx(sc, BGE_MBX_TX_HOST_PROD0_LO, prodidx);
	/* 5700 b2 errata */
	if (sc->bge_chiprev == BGE_CHIPREV_5700_BX)
		bge_writembx(sc, BGE_MBX_TX_HOST_PROD0_LO, prodidx);

	sc->bge_tx_prodidx = prodidx;

	/* Set a timeout in case the chip goes out to lunch. */
	sc->bge_timer = BGE_TX_TIMEOUT;
}

/*
 * Main transmit routine. To avoid having to do mbuf copies, we put pointers
 * to the mbuf data regions directly in the transmit descriptors.
 */
static void
bge_start(if_t ifp)
{
	struct bge_softc *sc;

	sc = if_getsoftc(ifp);
	BGE_LOCK(sc);
	bge_start_locked(ifp);
	BGE_UNLOCK(sc);
}

static void
bge_init_locked(struct bge_softc *sc)
{
	if_t ifp;
	uint16_t *m;
	uint32_t mode;

	BGE_LOCK_ASSERT(sc);

	ifp = sc->bge_ifp;

	if (if_getdrvflags(ifp) & IFF_DRV_RUNNING)
		return;

	/* Cancel pending I/O and flush buffers. */
	bge_stop(sc);

	bge_stop_fw(sc);
	bge_sig_pre_reset(sc, BGE_RESET_START);
	bge_reset(sc);
	bge_sig_legacy(sc, BGE_RESET_START);
	bge_sig_post_reset(sc, BGE_RESET_START);

	bge_chipinit(sc);

	/*
	 * Init the various state machines, ring
	 * control blocks and firmware.
	 */
	if (bge_blockinit(sc)) {
		device_printf(sc->bge_dev, "initialization failure\n");
		return;
	}

	ifp = sc->bge_ifp;

	/* Specify MTU. */
	CSR_WRITE_4(sc, BGE_RX_MTU, if_getmtu(ifp) +
	    ETHER_HDR_LEN + ETHER_CRC_LEN +
	    (if_getcapenable(ifp) & IFCAP_VLAN_MTU ? ETHER_VLAN_ENCAP_LEN : 0));

	/* Load our MAC address. */
	m = (uint16_t *)IF_LLADDR(sc->bge_ifp);
	CSR_WRITE_4(sc, BGE_MAC_ADDR1_LO, htons(m[0]));
	CSR_WRITE_4(sc, BGE_MAC_ADDR1_HI, (htons(m[1]) << 16) | htons(m[2]));

	/* Program promiscuous mode. */
	bge_setpromisc(sc);

	/* Program multicast filter. */
	bge_setmulti(sc);

	/* Program VLAN tag stripping. */
	bge_setvlan(sc);

	/* Override UDP checksum offloading. */
	if (sc->bge_forced_udpcsum == 0)
		sc->bge_csum_features &= ~CSUM_UDP;
	else
		sc->bge_csum_features |= CSUM_UDP;
	if (if_getcapabilities(ifp) & IFCAP_TXCSUM &&
	    if_getcapenable(ifp) & IFCAP_TXCSUM) {
		if_sethwassistbits(ifp, 0, (BGE_CSUM_FEATURES | CSUM_UDP));
		if_sethwassistbits(ifp, sc->bge_csum_features, 0);
	}

	/* Init RX ring. */
	if (bge_init_rx_ring_std(sc) != 0) {
		device_printf(sc->bge_dev, "no memory for std Rx buffers.\n");
		bge_stop(sc);
		return;
	}

	/*
	 * Workaround for a bug in 5705 ASIC rev A0. Poll the NIC's
	 * memory to insure that the chip has in fact read the first
	 * entry of the ring.
	 */
	if (sc->bge_chipid == BGE_CHIPID_BCM5705_A0) {
		uint32_t		v, i;
		for (i = 0; i < 10; i++) {
			DELAY(20);
			v = bge_readmem_ind(sc, BGE_STD_RX_RINGS + 8);
			if (v == (MCLBYTES - ETHER_ALIGN))
				break;
		}
		if (i == 10)
			device_printf (sc->bge_dev,
			    "5705 A0 chip failed to load RX ring\n");
	}

	/* Init jumbo RX ring. */
	if (BGE_IS_JUMBO_CAPABLE(sc) &&
	    if_getmtu(ifp) + ETHER_HDR_LEN + ETHER_CRC_LEN + 
     	    ETHER_VLAN_ENCAP_LEN > (MCLBYTES - ETHER_ALIGN)) {
		if (bge_init_rx_ring_jumbo(sc) != 0) {
			device_printf(sc->bge_dev,
			    "no memory for jumbo Rx buffers.\n");
			bge_stop(sc);
			return;
		}
	}

	/* Init our RX return ring index. */
	sc->bge_rx_saved_considx = 0;

	/* Init our RX/TX stat counters. */
	sc->bge_rx_discards = sc->bge_tx_discards = sc->bge_tx_collisions = 0;

	/* Init TX ring. */
	bge_init_tx_ring(sc);

	/* Enable TX MAC state machine lockup fix. */
	mode = CSR_READ_4(sc, BGE_TX_MODE);
	if (BGE_IS_5755_PLUS(sc) || sc->bge_asicrev == BGE_ASICREV_BCM5906)
		mode |= BGE_TXMODE_MBUF_LOCKUP_FIX;
	if (sc->bge_asicrev == BGE_ASICREV_BCM5720 ||
	    sc->bge_asicrev == BGE_ASICREV_BCM5762) {
		mode &= ~(BGE_TXMODE_JMB_FRM_LEN | BGE_TXMODE_CNT_DN_MODE);
		mode |= CSR_READ_4(sc, BGE_TX_MODE) &
		    (BGE_TXMODE_JMB_FRM_LEN | BGE_TXMODE_CNT_DN_MODE);
	}
	/* Turn on transmitter. */
	CSR_WRITE_4(sc, BGE_TX_MODE, mode | BGE_TXMODE_ENABLE);
	DELAY(100);

	/* Turn on receiver. */
	mode = CSR_READ_4(sc, BGE_RX_MODE);
	if (BGE_IS_5755_PLUS(sc))
		mode |= BGE_RXMODE_IPV6_ENABLE;
	if (sc->bge_asicrev == BGE_ASICREV_BCM5762)
		mode |= BGE_RXMODE_IPV4_FRAG_FIX;
	CSR_WRITE_4(sc,BGE_RX_MODE, mode | BGE_RXMODE_ENABLE);
	DELAY(10);

	/*
	 * Set the number of good frames to receive after RX MBUF
	 * Low Watermark has been reached. After the RX MAC receives
	 * this number of frames, it will drop subsequent incoming
	 * frames until the MBUF High Watermark is reached.
	 */
	if (BGE_IS_57765_PLUS(sc))
		CSR_WRITE_4(sc, BGE_MAX_RX_FRAME_LOWAT, 1);
	else
		CSR_WRITE_4(sc, BGE_MAX_RX_FRAME_LOWAT, 2);

	/* Clear MAC statistics. */
	if (BGE_IS_5705_PLUS(sc))
		bge_stats_clear_regs(sc);

	/* Tell firmware we're alive. */
	BGE_SETBIT(sc, BGE_MODE_CTL, BGE_MODECTL_STACKUP);

#ifdef DEVICE_POLLING
	/* Disable interrupts if we are polling. */
	if (if_getcapenable(ifp) & IFCAP_POLLING) {
		BGE_SETBIT(sc, BGE_PCI_MISC_CTL,
		    BGE_PCIMISCCTL_MASK_PCI_INTR);
		bge_writembx(sc, BGE_MBX_IRQ0_LO, 1);
	} else
#endif

	/* Enable host interrupts. */
	{
	BGE_SETBIT(sc, BGE_PCI_MISC_CTL, BGE_PCIMISCCTL_CLEAR_INTA);
	BGE_CLRBIT(sc, BGE_PCI_MISC_CTL, BGE_PCIMISCCTL_MASK_PCI_INTR);
	bge_writembx(sc, BGE_MBX_IRQ0_LO, 0);
	}

	if_setdrvflagbits(ifp, IFF_DRV_RUNNING, 0);
	if_setdrvflagbits(ifp, 0, IFF_DRV_OACTIVE);

	bge_ifmedia_upd_locked(ifp);

	callout_reset(&sc->bge_stat_ch, hz, bge_tick, sc);
}

static void
bge_init(void *xsc)
{
	struct bge_softc *sc = xsc;

	BGE_LOCK(sc);
	bge_init_locked(sc);
	BGE_UNLOCK(sc);
}

/*
 * Set media options.
 */
static int
bge_ifmedia_upd(if_t ifp)
{
	struct bge_softc *sc = if_getsoftc(ifp);
	int res;

	BGE_LOCK(sc);
	res = bge_ifmedia_upd_locked(ifp);
	BGE_UNLOCK(sc);

	return (res);
}

static int
bge_ifmedia_upd_locked(if_t ifp)
{
	struct bge_softc *sc = if_getsoftc(ifp);
	struct mii_data *mii;
	struct mii_softc *miisc;
	struct ifmedia *ifm;

	BGE_LOCK_ASSERT(sc);

	ifm = &sc->bge_ifmedia;

	/* If this is a 1000baseX NIC, enable the TBI port. */
	if (sc->bge_flags & BGE_FLAG_TBI) {
		if (IFM_TYPE(ifm->ifm_media) != IFM_ETHER)
			return (EINVAL);
		switch(IFM_SUBTYPE(ifm->ifm_media)) {
		case IFM_AUTO:
			/*
			 * The BCM5704 ASIC appears to have a special
			 * mechanism for programming the autoneg
			 * advertisement registers in TBI mode.
			 */
			if (sc->bge_asicrev == BGE_ASICREV_BCM5704) {
				uint32_t sgdig;
				sgdig = CSR_READ_4(sc, BGE_SGDIG_STS);
				if (sgdig & BGE_SGDIGSTS_DONE) {
					CSR_WRITE_4(sc, BGE_TX_TBI_AUTONEG, 0);
					sgdig = CSR_READ_4(sc, BGE_SGDIG_CFG);
					sgdig |= BGE_SGDIGCFG_AUTO |
					    BGE_SGDIGCFG_PAUSE_CAP |
					    BGE_SGDIGCFG_ASYM_PAUSE;
					CSR_WRITE_4(sc, BGE_SGDIG_CFG,
					    sgdig | BGE_SGDIGCFG_SEND);
					DELAY(5);
					CSR_WRITE_4(sc, BGE_SGDIG_CFG, sgdig);
				}
			}
			break;
		case IFM_1000_SX:
			if ((ifm->ifm_media & IFM_GMASK) == IFM_FDX) {
				BGE_CLRBIT(sc, BGE_MAC_MODE,
				    BGE_MACMODE_HALF_DUPLEX);
			} else {
				BGE_SETBIT(sc, BGE_MAC_MODE,
				    BGE_MACMODE_HALF_DUPLEX);
			}
			DELAY(40);
			break;
		default:
			return (EINVAL);
		}
		return (0);
	}

	sc->bge_link_evt++;
	mii = device_get_softc(sc->bge_miibus);
	LIST_FOREACH(miisc, &mii->mii_phys, mii_list)
		PHY_RESET(miisc);
	mii_mediachg(mii);

	/*
	 * Force an interrupt so that we will call bge_link_upd
	 * if needed and clear any pending link state attention.
	 * Without this we are not getting any further interrupts
	 * for link state changes and thus will not UP the link and
	 * not be able to send in bge_start_locked. The only
	 * way to get things working was to receive a packet and
	 * get an RX intr.
	 * bge_tick should help for fiber cards and we might not
	 * need to do this here if BGE_FLAG_TBI is set but as
	 * we poll for fiber anyway it should not harm.
	 */
	if (sc->bge_asicrev == BGE_ASICREV_BCM5700 ||
	    sc->bge_flags & BGE_FLAG_5788)
		BGE_SETBIT(sc, BGE_MISC_LOCAL_CTL, BGE_MLC_INTR_SET);
	else
		BGE_SETBIT(sc, BGE_HCC_MODE, BGE_HCCMODE_COAL_NOW);

	return (0);
}

/*
 * Report current media status.
 */
static void
bge_ifmedia_sts(if_t ifp, struct ifmediareq *ifmr)
{
	struct bge_softc *sc = if_getsoftc(ifp);
	struct mii_data *mii;

	BGE_LOCK(sc);

	if ((if_getflags(ifp) & IFF_UP) == 0) {
		BGE_UNLOCK(sc);
		return;
	}
	if (sc->bge_flags & BGE_FLAG_TBI) {
		ifmr->ifm_status = IFM_AVALID;
		ifmr->ifm_active = IFM_ETHER;
		if (CSR_READ_4(sc, BGE_MAC_STS) &
		    BGE_MACSTAT_TBI_PCS_SYNCHED)
			ifmr->ifm_status |= IFM_ACTIVE;
		else {
			ifmr->ifm_active |= IFM_NONE;
			BGE_UNLOCK(sc);
			return;
		}
		ifmr->ifm_active |= IFM_1000_SX;
		if (CSR_READ_4(sc, BGE_MAC_MODE) & BGE_MACMODE_HALF_DUPLEX)
			ifmr->ifm_active |= IFM_HDX;
		else
			ifmr->ifm_active |= IFM_FDX;
		BGE_UNLOCK(sc);
		return;
	}

	mii = device_get_softc(sc->bge_miibus);
	mii_pollstat(mii);
	ifmr->ifm_active = mii->mii_media_active;
	ifmr->ifm_status = mii->mii_media_status;

	BGE_UNLOCK(sc);
}

static int
bge_ioctl(if_t ifp, u_long command, caddr_t data)
{
	struct bge_softc *sc = if_getsoftc(ifp);
	struct ifreq *ifr = (struct ifreq *) data;
	struct mii_data *mii;
	int flags, mask, error = 0;

	switch (command) {
	case SIOCSIFMTU:
		if (BGE_IS_JUMBO_CAPABLE(sc) ||
		    (sc->bge_flags & BGE_FLAG_JUMBO_STD)) {
			if (ifr->ifr_mtu < ETHERMIN ||
			    ifr->ifr_mtu > BGE_JUMBO_MTU) {
				error = EINVAL;
				break;
			}
		} else if (ifr->ifr_mtu < ETHERMIN || ifr->ifr_mtu > ETHERMTU) {
			error = EINVAL;
			break;
		}
		BGE_LOCK(sc);
		if (if_getmtu(ifp) != ifr->ifr_mtu) {
			if_setmtu(ifp, ifr->ifr_mtu);
			if (if_getdrvflags(ifp) & IFF_DRV_RUNNING) {
				if_setdrvflagbits(ifp, 0, IFF_DRV_RUNNING);
				bge_init_locked(sc);
			}
		}
		BGE_UNLOCK(sc);
		break;
	case SIOCSIFFLAGS:
		BGE_LOCK(sc);
		if (if_getflags(ifp) & IFF_UP) {
			/*
			 * If only the state of the PROMISC flag changed,
			 * then just use the 'set promisc mode' command
			 * instead of reinitializing the entire NIC. Doing
			 * a full re-init means reloading the firmware and
			 * waiting for it to start up, which may take a
			 * second or two.  Similarly for ALLMULTI.
			 */
			if (if_getdrvflags(ifp) & IFF_DRV_RUNNING) {
				flags = if_getflags(ifp) ^ sc->bge_if_flags;
				if (flags & IFF_PROMISC)
					bge_setpromisc(sc);
				if (flags & IFF_ALLMULTI)
					bge_setmulti(sc);
			} else
				bge_init_locked(sc);
		} else {
			if (if_getdrvflags(ifp) & IFF_DRV_RUNNING) {
				bge_stop(sc);
			}
		}
		sc->bge_if_flags = if_getflags(ifp);
		BGE_UNLOCK(sc);
		error = 0;
		break;
	case SIOCADDMULTI:
	case SIOCDELMULTI:
		if (if_getdrvflags(ifp) & IFF_DRV_RUNNING) {
			BGE_LOCK(sc);
			bge_setmulti(sc);
			BGE_UNLOCK(sc);
			error = 0;
		}
		break;
	case SIOCSIFMEDIA:
	case SIOCGIFMEDIA:
		if (sc->bge_flags & BGE_FLAG_TBI) {
			error = ifmedia_ioctl(ifp, ifr,
			    &sc->bge_ifmedia, command);
		} else {
			mii = device_get_softc(sc->bge_miibus);
			error = ifmedia_ioctl(ifp, ifr,
			    &mii->mii_media, command);
		}
		break;
	case SIOCSIFCAP:
		mask = ifr->ifr_reqcap ^ if_getcapenable(ifp);
#ifdef DEVICE_POLLING
		if (mask & IFCAP_POLLING) {
			if (ifr->ifr_reqcap & IFCAP_POLLING) {
				error = ether_poll_register(bge_poll, ifp);
				if (error)
					return (error);
				BGE_LOCK(sc);
				BGE_SETBIT(sc, BGE_PCI_MISC_CTL,
				    BGE_PCIMISCCTL_MASK_PCI_INTR);
				bge_writembx(sc, BGE_MBX_IRQ0_LO, 1);
				if_setcapenablebit(ifp, IFCAP_POLLING, 0);
				BGE_UNLOCK(sc);
			} else {
				error = ether_poll_deregister(ifp);
				/* Enable interrupt even in error case */
				BGE_LOCK(sc);
				BGE_CLRBIT(sc, BGE_PCI_MISC_CTL,
				    BGE_PCIMISCCTL_MASK_PCI_INTR);
				bge_writembx(sc, BGE_MBX_IRQ0_LO, 0);
				if_setcapenablebit(ifp, 0, IFCAP_POLLING);
				BGE_UNLOCK(sc);
			}
		}
#endif
		if ((mask & IFCAP_TXCSUM) != 0 &&
		    (if_getcapabilities(ifp) & IFCAP_TXCSUM) != 0) {
			if_togglecapenable(ifp, IFCAP_TXCSUM);
			if ((if_getcapenable(ifp) & IFCAP_TXCSUM) != 0)
				if_sethwassistbits(ifp,
				    sc->bge_csum_features, 0);
			else
				if_sethwassistbits(ifp, 0,
				    sc->bge_csum_features);
		}

		if ((mask & IFCAP_RXCSUM) != 0 &&
		    (if_getcapabilities(ifp) & IFCAP_RXCSUM) != 0)
			if_togglecapenable(ifp, IFCAP_RXCSUM);

		if ((mask & IFCAP_TSO4) != 0 &&
		    (if_getcapabilities(ifp) & IFCAP_TSO4) != 0) {
			if_togglecapenable(ifp, IFCAP_TSO4);
			if ((if_getcapenable(ifp) & IFCAP_TSO4) != 0)
				if_sethwassistbits(ifp, CSUM_TSO, 0);
			else
				if_sethwassistbits(ifp, 0, CSUM_TSO);
		}

		if (mask & IFCAP_VLAN_MTU) {
			if_togglecapenable(ifp, IFCAP_VLAN_MTU);
			if_setdrvflagbits(ifp, 0, IFF_DRV_RUNNING);
			bge_init(sc);
		}

		if ((mask & IFCAP_VLAN_HWTSO) != 0 &&
		    (if_getcapabilities(ifp) & IFCAP_VLAN_HWTSO) != 0)
			if_togglecapenable(ifp, IFCAP_VLAN_HWTSO);
		if ((mask & IFCAP_VLAN_HWTAGGING) != 0 &&
		    (if_getcapabilities(ifp) & IFCAP_VLAN_HWTAGGING) != 0) {
			if_togglecapenable(ifp, IFCAP_VLAN_HWTAGGING);
			if ((if_getcapenable(ifp) & IFCAP_VLAN_HWTAGGING) == 0)
				if_setcapenablebit(ifp, 0, IFCAP_VLAN_HWTSO);
			BGE_LOCK(sc);
			bge_setvlan(sc);
			BGE_UNLOCK(sc);
		}
#ifdef VLAN_CAPABILITIES
		if_vlancap(ifp);
#endif
		break;
	default:
		error = ether_ioctl(ifp, command, data);
		break;
	}

	return (error);
}

static void
bge_watchdog(struct bge_softc *sc)
{
	if_t ifp;
	uint32_t status;

	BGE_LOCK_ASSERT(sc);

	if (sc->bge_timer == 0 || --sc->bge_timer)
		return;

	/* If pause frames are active then don't reset the hardware. */
	if ((CSR_READ_4(sc, BGE_RX_MODE) & BGE_RXMODE_FLOWCTL_ENABLE) != 0) {
		status = CSR_READ_4(sc, BGE_RX_STS);
		if ((status & BGE_RXSTAT_REMOTE_XOFFED) != 0) {
			/*
			 * If link partner has us in XOFF state then wait for
			 * the condition to clear.
			 */
			CSR_WRITE_4(sc, BGE_RX_STS, status);
			sc->bge_timer = BGE_TX_TIMEOUT;
			return;
		} else if ((status & BGE_RXSTAT_RCVD_XOFF) != 0 &&
		    (status & BGE_RXSTAT_RCVD_XON) != 0) {
			/*
			 * If link partner has us in XOFF state then wait for
			 * the condition to clear.
			 */
			CSR_WRITE_4(sc, BGE_RX_STS, status);
			sc->bge_timer = BGE_TX_TIMEOUT;
			return;
		}
		/*
		 * Any other condition is unexpected and the controller
		 * should be reset.
		 */
	}

	ifp = sc->bge_ifp;

	if_printf(ifp, "watchdog timeout -- resetting\n");

	if_setdrvflagbits(ifp, 0, IFF_DRV_RUNNING);
	bge_init_locked(sc);

	if_inc_counter(ifp, IFCOUNTER_OERRORS, 1);
}

static void
bge_stop_block(struct bge_softc *sc, bus_size_t reg, uint32_t bit)
{
	int i;

	BGE_CLRBIT(sc, reg, bit);

	for (i = 0; i < BGE_TIMEOUT; i++) {
		if ((CSR_READ_4(sc, reg) & bit) == 0)
			return;
		DELAY(100);
        }
}

/*
 * Stop the adapter and free any mbufs allocated to the
 * RX and TX lists.
 */
static void
bge_stop(struct bge_softc *sc)
{
	if_t ifp;

	BGE_LOCK_ASSERT(sc);

	ifp = sc->bge_ifp;

	callout_stop(&sc->bge_stat_ch);

	/* Disable host interrupts. */
	BGE_SETBIT(sc, BGE_PCI_MISC_CTL, BGE_PCIMISCCTL_MASK_PCI_INTR);
	bge_writembx(sc, BGE_MBX_IRQ0_LO, 1);

	/*
	 * Tell firmware we're shutting down.
	 */
	bge_stop_fw(sc);
	bge_sig_pre_reset(sc, BGE_RESET_SHUTDOWN);

	/*
	 * Disable all of the receiver blocks.
	 */
	bge_stop_block(sc, BGE_RX_MODE, BGE_RXMODE_ENABLE);
	bge_stop_block(sc, BGE_RBDI_MODE, BGE_RBDIMODE_ENABLE);
	bge_stop_block(sc, BGE_RXLP_MODE, BGE_RXLPMODE_ENABLE);
	if (BGE_IS_5700_FAMILY(sc))
		bge_stop_block(sc, BGE_RXLS_MODE, BGE_RXLSMODE_ENABLE);
	bge_stop_block(sc, BGE_RDBDI_MODE, BGE_RBDIMODE_ENABLE);
	bge_stop_block(sc, BGE_RDC_MODE, BGE_RDCMODE_ENABLE);
	bge_stop_block(sc, BGE_RBDC_MODE, BGE_RBDCMODE_ENABLE);

	/*
	 * Disable all of the transmit blocks.
	 */
	bge_stop_block(sc, BGE_SRS_MODE, BGE_SRSMODE_ENABLE);
	bge_stop_block(sc, BGE_SBDI_MODE, BGE_SBDIMODE_ENABLE);
	bge_stop_block(sc, BGE_SDI_MODE, BGE_SDIMODE_ENABLE);
	bge_stop_block(sc, BGE_RDMA_MODE, BGE_RDMAMODE_ENABLE);
	bge_stop_block(sc, BGE_SDC_MODE, BGE_SDCMODE_ENABLE);
	if (BGE_IS_5700_FAMILY(sc))
		bge_stop_block(sc, BGE_DMAC_MODE, BGE_DMACMODE_ENABLE);
	bge_stop_block(sc, BGE_SBDC_MODE, BGE_SBDCMODE_ENABLE);

	/*
	 * Shut down all of the memory managers and related
	 * state machines.
	 */
	bge_stop_block(sc, BGE_HCC_MODE, BGE_HCCMODE_ENABLE);
	bge_stop_block(sc, BGE_WDMA_MODE, BGE_WDMAMODE_ENABLE);
	if (BGE_IS_5700_FAMILY(sc))
		bge_stop_block(sc, BGE_MBCF_MODE, BGE_MBCFMODE_ENABLE);

	CSR_WRITE_4(sc, BGE_FTQ_RESET, 0xFFFFFFFF);
	CSR_WRITE_4(sc, BGE_FTQ_RESET, 0);
	if (!(BGE_IS_5705_PLUS(sc))) {
		BGE_CLRBIT(sc, BGE_BMAN_MODE, BGE_BMANMODE_ENABLE);
		BGE_CLRBIT(sc, BGE_MARB_MODE, BGE_MARBMODE_ENABLE);
	}
	/* Update MAC statistics. */
	if (BGE_IS_5705_PLUS(sc))
		bge_stats_update_regs(sc);

	bge_reset(sc);
	bge_sig_legacy(sc, BGE_RESET_SHUTDOWN);
	bge_sig_post_reset(sc, BGE_RESET_SHUTDOWN);

	/*
	 * Keep the ASF firmware running if up.
	 */
	if (sc->bge_asf_mode & ASF_STACKUP)
		BGE_SETBIT(sc, BGE_MODE_CTL, BGE_MODECTL_STACKUP);
	else
		BGE_CLRBIT(sc, BGE_MODE_CTL, BGE_MODECTL_STACKUP);

	/* Free the RX lists. */
	bge_free_rx_ring_std(sc);

	/* Free jumbo RX list. */
	if (BGE_IS_JUMBO_CAPABLE(sc))
		bge_free_rx_ring_jumbo(sc);

	/* Free TX buffers. */
	bge_free_tx_ring(sc);

	sc->bge_tx_saved_considx = BGE_TXCONS_UNSET;

	/* Clear MAC's link state (PHY may still have link UP). */
	if (bootverbose && sc->bge_link)
		if_printf(sc->bge_ifp, "link DOWN\n");
	sc->bge_link = 0;

	if_setdrvflagbits(ifp, 0, (IFF_DRV_RUNNING | IFF_DRV_OACTIVE));
}

/*
 * Stop all chip I/O so that the kernel's probe routines don't
 * get confused by errant DMAs when rebooting.
 */
static int
bge_shutdown(device_t dev)
{
	struct bge_softc *sc;

	sc = device_get_softc(dev);
	BGE_LOCK(sc);
	bge_stop(sc);
	BGE_UNLOCK(sc);

	return (0);
}

static int
bge_suspend(device_t dev)
{
	struct bge_softc *sc;

	sc = device_get_softc(dev);
	BGE_LOCK(sc);
	bge_stop(sc);
	BGE_UNLOCK(sc);

	return (0);
}

static int
bge_resume(device_t dev)
{
	struct bge_softc *sc;
	if_t ifp;

	sc = device_get_softc(dev);
	BGE_LOCK(sc);
	ifp = sc->bge_ifp;
	if (if_getflags(ifp) & IFF_UP) {
		bge_init_locked(sc);
		if (if_getdrvflags(ifp) & IFF_DRV_RUNNING)
			bge_start_locked(ifp);
	}
	BGE_UNLOCK(sc);

	return (0);
}

static void
bge_link_upd(struct bge_softc *sc)
{
	struct mii_data *mii;
	uint32_t link, status;

	BGE_LOCK_ASSERT(sc);

	/* Clear 'pending link event' flag. */
	sc->bge_link_evt = 0;

	/*
	 * Process link state changes.
	 * Grrr. The link status word in the status block does
	 * not work correctly on the BCM5700 rev AX and BX chips,
	 * according to all available information. Hence, we have
	 * to enable MII interrupts in order to properly obtain
	 * async link changes. Unfortunately, this also means that
	 * we have to read the MAC status register to detect link
	 * changes, thereby adding an additional register access to
	 * the interrupt handler.
	 *
	 * XXX: perhaps link state detection procedure used for
	 * BGE_CHIPID_BCM5700_B2 can be used for others BCM5700 revisions.
	 */

	if (sc->bge_asicrev == BGE_ASICREV_BCM5700 &&
	    sc->bge_chipid != BGE_CHIPID_BCM5700_B2) {
		status = CSR_READ_4(sc, BGE_MAC_STS);
		if (status & BGE_MACSTAT_MI_INTERRUPT) {
			mii = device_get_softc(sc->bge_miibus);
			mii_pollstat(mii);
			if (!sc->bge_link &&
			    mii->mii_media_status & IFM_ACTIVE &&
			    IFM_SUBTYPE(mii->mii_media_active) != IFM_NONE) {
				sc->bge_link++;
				if (bootverbose)
					if_printf(sc->bge_ifp, "link UP\n");
			} else if (sc->bge_link &&
			    (!(mii->mii_media_status & IFM_ACTIVE) ||
			    IFM_SUBTYPE(mii->mii_media_active) == IFM_NONE)) {
				sc->bge_link = 0;
				if (bootverbose)
					if_printf(sc->bge_ifp, "link DOWN\n");
			}

			/* Clear the interrupt. */
			CSR_WRITE_4(sc, BGE_MAC_EVT_ENB,
			    BGE_EVTENB_MI_INTERRUPT);
			bge_miibus_readreg(sc->bge_dev, sc->bge_phy_addr,
			    BRGPHY_MII_ISR);
			bge_miibus_writereg(sc->bge_dev, sc->bge_phy_addr,
			    BRGPHY_MII_IMR, BRGPHY_INTRS);
		}
		return;
	}

	if (sc->bge_flags & BGE_FLAG_TBI) {
		status = CSR_READ_4(sc, BGE_MAC_STS);
		if (status & BGE_MACSTAT_TBI_PCS_SYNCHED) {
			if (!sc->bge_link) {
				sc->bge_link++;
				if (sc->bge_asicrev == BGE_ASICREV_BCM5704) {
					BGE_CLRBIT(sc, BGE_MAC_MODE,
					    BGE_MACMODE_TBI_SEND_CFGS);
					DELAY(40);
				}
				CSR_WRITE_4(sc, BGE_MAC_STS, 0xFFFFFFFF);
				if (bootverbose)
					if_printf(sc->bge_ifp, "link UP\n");
				if_link_state_change(sc->bge_ifp,
				    LINK_STATE_UP);
			}
		} else if (sc->bge_link) {
			sc->bge_link = 0;
			if (bootverbose)
				if_printf(sc->bge_ifp, "link DOWN\n");
			if_link_state_change(sc->bge_ifp, LINK_STATE_DOWN);
		}
	} else if ((sc->bge_mi_mode & BGE_MIMODE_AUTOPOLL) != 0) {
		/*
		 * Some broken BCM chips have BGE_STATFLAG_LINKSTATE_CHANGED bit
		 * in status word always set. Workaround this bug by reading
		 * PHY link status directly.
		 */
		link = (CSR_READ_4(sc, BGE_MI_STS) & BGE_MISTS_LINK) ? 1 : 0;

		if (link != sc->bge_link ||
		    sc->bge_asicrev == BGE_ASICREV_BCM5700) {
			mii = device_get_softc(sc->bge_miibus);
			mii_pollstat(mii);
			if (!sc->bge_link &&
			    mii->mii_media_status & IFM_ACTIVE &&
			    IFM_SUBTYPE(mii->mii_media_active) != IFM_NONE) {
				sc->bge_link++;
				if (bootverbose)
					if_printf(sc->bge_ifp, "link UP\n");
			} else if (sc->bge_link &&
			    (!(mii->mii_media_status & IFM_ACTIVE) ||
			    IFM_SUBTYPE(mii->mii_media_active) == IFM_NONE)) {
				sc->bge_link = 0;
				if (bootverbose)
					if_printf(sc->bge_ifp, "link DOWN\n");
			}
		}
	} else {
		/*
		 * For controllers that call mii_tick, we have to poll
		 * link status.
		 */
		mii = device_get_softc(sc->bge_miibus);
		mii_pollstat(mii);
		bge_miibus_statchg(sc->bge_dev);
	}

	/* Disable MAC attention when link is up. */
	CSR_WRITE_4(sc, BGE_MAC_STS, BGE_MACSTAT_SYNC_CHANGED |
	    BGE_MACSTAT_CFG_CHANGED | BGE_MACSTAT_MI_COMPLETE |
	    BGE_MACSTAT_LINK_CHANGED);
}

static void
bge_add_sysctls(struct bge_softc *sc)
{
	struct sysctl_ctx_list *ctx;
	struct sysctl_oid_list *children;
	int unit;

	ctx = device_get_sysctl_ctx(sc->bge_dev);
	children = SYSCTL_CHILDREN(device_get_sysctl_tree(sc->bge_dev));

#ifdef BGE_REGISTER_DEBUG
	SYSCTL_ADD_PROC(ctx, children, OID_AUTO, "debug_info",
	    CTLTYPE_INT | CTLFLAG_RW, sc, 0, bge_sysctl_debug_info, "I",
	    "Debug Information");

	SYSCTL_ADD_PROC(ctx, children, OID_AUTO, "reg_read",
	    CTLTYPE_INT | CTLFLAG_RW, sc, 0, bge_sysctl_reg_read, "I",
	    "MAC Register Read");

	SYSCTL_ADD_PROC(ctx, children, OID_AUTO, "ape_read",
	    CTLTYPE_INT | CTLFLAG_RW, sc, 0, bge_sysctl_ape_read, "I",
	    "APE Register Read");

	SYSCTL_ADD_PROC(ctx, children, OID_AUTO, "mem_read",
	    CTLTYPE_INT | CTLFLAG_RW, sc, 0, bge_sysctl_mem_read, "I",
	    "Memory Read");

#endif

	unit = device_get_unit(sc->bge_dev);
	/*
	 * A common design characteristic for many Broadcom client controllers
	 * is that they only support a single outstanding DMA read operation
	 * on the PCIe bus. This means that it will take twice as long to fetch
	 * a TX frame that is split into header and payload buffers as it does
	 * to fetch a single, contiguous TX frame (2 reads vs. 1 read). For
	 * these controllers, coalescing buffers to reduce the number of memory
	 * reads is effective way to get maximum performance(about 940Mbps).
	 * Without collapsing TX buffers the maximum TCP bulk transfer
	 * performance is about 850Mbps. However forcing coalescing mbufs
	 * consumes a lot of CPU cycles, so leave it off by default.
	 */
	sc->bge_forced_collapse = 0;
	SYSCTL_ADD_INT(ctx, children, OID_AUTO, "forced_collapse",
	    CTLFLAG_RWTUN, &sc->bge_forced_collapse, 0,
	    "Number of fragmented TX buffers of a frame allowed before "
	    "forced collapsing");

	sc->bge_msi = 1;
	SYSCTL_ADD_INT(ctx, children, OID_AUTO, "msi",
	    CTLFLAG_RDTUN, &sc->bge_msi, 0, "Enable MSI");

	/*
	 * It seems all Broadcom controllers have a bug that can generate UDP
	 * datagrams with checksum value 0 when TX UDP checksum offloading is
	 * enabled.  Generating UDP checksum value 0 is RFC 768 violation.
	 * Even though the probability of generating such UDP datagrams is
	 * low, I don't want to see FreeBSD boxes to inject such datagrams
	 * into network so disable UDP checksum offloading by default.  Users
	 * still override this behavior by setting a sysctl variable,
	 * dev.bge.0.forced_udpcsum.
	 */
	sc->bge_forced_udpcsum = 0;
	SYSCTL_ADD_INT(ctx, children, OID_AUTO, "forced_udpcsum",
	    CTLFLAG_RWTUN, &sc->bge_forced_udpcsum, 0,
	    "Enable UDP checksum offloading even if controller can "
	    "generate UDP checksum value 0");

	if (BGE_IS_5705_PLUS(sc))
		bge_add_sysctl_stats_regs(sc, ctx, children);
	else
		bge_add_sysctl_stats(sc, ctx, children);
}

#define BGE_SYSCTL_STAT(sc, ctx, desc, parent, node, oid) \
	SYSCTL_ADD_PROC(ctx, parent, OID_AUTO, oid, CTLTYPE_UINT|CTLFLAG_RD, \
	    sc, offsetof(struct bge_stats, node), bge_sysctl_stats, "IU", \
	    desc)

static void
bge_add_sysctl_stats(struct bge_softc *sc, struct sysctl_ctx_list *ctx,
    struct sysctl_oid_list *parent)
{
	struct sysctl_oid *tree;
	struct sysctl_oid_list *children, *schildren;

	tree = SYSCTL_ADD_NODE(ctx, parent, OID_AUTO, "stats", CTLFLAG_RD,
	    NULL, "BGE Statistics");
	schildren = children = SYSCTL_CHILDREN(tree);
	BGE_SYSCTL_STAT(sc, ctx, "Frames Dropped Due To Filters",
	    children, COSFramesDroppedDueToFilters,
	    "FramesDroppedDueToFilters");
	BGE_SYSCTL_STAT(sc, ctx, "NIC DMA Write Queue Full",
	    children, nicDmaWriteQueueFull, "DmaWriteQueueFull");
	BGE_SYSCTL_STAT(sc, ctx, "NIC DMA Write High Priority Queue Full",
	    children, nicDmaWriteHighPriQueueFull, "DmaWriteHighPriQueueFull");
	BGE_SYSCTL_STAT(sc, ctx, "NIC No More RX Buffer Descriptors",
	    children, nicNoMoreRxBDs, "NoMoreRxBDs");
	BGE_SYSCTL_STAT(sc, ctx, "Discarded Input Frames",
	    children, ifInDiscards, "InputDiscards");
	BGE_SYSCTL_STAT(sc, ctx, "Input Errors",
	    children, ifInErrors, "InputErrors");
	BGE_SYSCTL_STAT(sc, ctx, "NIC Recv Threshold Hit",
	    children, nicRecvThresholdHit, "RecvThresholdHit");
	BGE_SYSCTL_STAT(sc, ctx, "NIC DMA Read Queue Full",
	    children, nicDmaReadQueueFull, "DmaReadQueueFull");
	BGE_SYSCTL_STAT(sc, ctx, "NIC DMA Read High Priority Queue Full",
	    children, nicDmaReadHighPriQueueFull, "DmaReadHighPriQueueFull");
	BGE_SYSCTL_STAT(sc, ctx, "NIC Send Data Complete Queue Full",
	    children, nicSendDataCompQueueFull, "SendDataCompQueueFull");
	BGE_SYSCTL_STAT(sc, ctx, "NIC Ring Set Send Producer Index",
	    children, nicRingSetSendProdIndex, "RingSetSendProdIndex");
	BGE_SYSCTL_STAT(sc, ctx, "NIC Ring Status Update",
	    children, nicRingStatusUpdate, "RingStatusUpdate");
	BGE_SYSCTL_STAT(sc, ctx, "NIC Interrupts",
	    children, nicInterrupts, "Interrupts");
	BGE_SYSCTL_STAT(sc, ctx, "NIC Avoided Interrupts",
	    children, nicAvoidedInterrupts, "AvoidedInterrupts");
	BGE_SYSCTL_STAT(sc, ctx, "NIC Send Threshold Hit",
	    children, nicSendThresholdHit, "SendThresholdHit");

	tree = SYSCTL_ADD_NODE(ctx, schildren, OID_AUTO, "rx", CTLFLAG_RD,
	    NULL, "BGE RX Statistics");
	children = SYSCTL_CHILDREN(tree);
	BGE_SYSCTL_STAT(sc, ctx, "Inbound Octets",
	    children, rxstats.ifHCInOctets, "ifHCInOctets");
	BGE_SYSCTL_STAT(sc, ctx, "Fragments",
	    children, rxstats.etherStatsFragments, "Fragments");
	BGE_SYSCTL_STAT(sc, ctx, "Inbound Unicast Packets",
	    children, rxstats.ifHCInUcastPkts, "UnicastPkts");
	BGE_SYSCTL_STAT(sc, ctx, "Inbound Multicast Packets",
	    children, rxstats.ifHCInMulticastPkts, "MulticastPkts");
	BGE_SYSCTL_STAT(sc, ctx, "FCS Errors",
	    children, rxstats.dot3StatsFCSErrors, "FCSErrors");
	BGE_SYSCTL_STAT(sc, ctx, "Alignment Errors",
	    children, rxstats.dot3StatsAlignmentErrors, "AlignmentErrors");
	BGE_SYSCTL_STAT(sc, ctx, "XON Pause Frames Received",
	    children, rxstats.xonPauseFramesReceived, "xonPauseFramesReceived");
	BGE_SYSCTL_STAT(sc, ctx, "XOFF Pause Frames Received",
	    children, rxstats.xoffPauseFramesReceived,
	    "xoffPauseFramesReceived");
	BGE_SYSCTL_STAT(sc, ctx, "MAC Control Frames Received",
	    children, rxstats.macControlFramesReceived,
	    "ControlFramesReceived");
	BGE_SYSCTL_STAT(sc, ctx, "XOFF State Entered",
	    children, rxstats.xoffStateEntered, "xoffStateEntered");
	BGE_SYSCTL_STAT(sc, ctx, "Frames Too Long",
	    children, rxstats.dot3StatsFramesTooLong, "FramesTooLong");
	BGE_SYSCTL_STAT(sc, ctx, "Jabbers",
	    children, rxstats.etherStatsJabbers, "Jabbers");
	BGE_SYSCTL_STAT(sc, ctx, "Undersized Packets",
	    children, rxstats.etherStatsUndersizePkts, "UndersizePkts");
	BGE_SYSCTL_STAT(sc, ctx, "Inbound Range Length Errors",
	    children, rxstats.inRangeLengthError, "inRangeLengthError");
	BGE_SYSCTL_STAT(sc, ctx, "Outbound Range Length Errors",
	    children, rxstats.outRangeLengthError, "outRangeLengthError");

	tree = SYSCTL_ADD_NODE(ctx, schildren, OID_AUTO, "tx", CTLFLAG_RD,
	    NULL, "BGE TX Statistics");
	children = SYSCTL_CHILDREN(tree);
	BGE_SYSCTL_STAT(sc, ctx, "Outbound Octets",
	    children, txstats.ifHCOutOctets, "ifHCOutOctets");
	BGE_SYSCTL_STAT(sc, ctx, "TX Collisions",
	    children, txstats.etherStatsCollisions, "Collisions");
	BGE_SYSCTL_STAT(sc, ctx, "XON Sent",
	    children, txstats.outXonSent, "XonSent");
	BGE_SYSCTL_STAT(sc, ctx, "XOFF Sent",
	    children, txstats.outXoffSent, "XoffSent");
	BGE_SYSCTL_STAT(sc, ctx, "Flow Control Done",
	    children, txstats.flowControlDone, "flowControlDone");
	BGE_SYSCTL_STAT(sc, ctx, "Internal MAC TX errors",
	    children, txstats.dot3StatsInternalMacTransmitErrors,
	    "InternalMacTransmitErrors");
	BGE_SYSCTL_STAT(sc, ctx, "Single Collision Frames",
	    children, txstats.dot3StatsSingleCollisionFrames,
	    "SingleCollisionFrames");
	BGE_SYSCTL_STAT(sc, ctx, "Multiple Collision Frames",
	    children, txstats.dot3StatsMultipleCollisionFrames,
	    "MultipleCollisionFrames");
	BGE_SYSCTL_STAT(sc, ctx, "Deferred Transmissions",
	    children, txstats.dot3StatsDeferredTransmissions,
	    "DeferredTransmissions");
	BGE_SYSCTL_STAT(sc, ctx, "Excessive Collisions",
	    children, txstats.dot3StatsExcessiveCollisions,
	    "ExcessiveCollisions");
	BGE_SYSCTL_STAT(sc, ctx, "Late Collisions",
	    children, txstats.dot3StatsLateCollisions,
	    "LateCollisions");
	BGE_SYSCTL_STAT(sc, ctx, "Outbound Unicast Packets",
	    children, txstats.ifHCOutUcastPkts, "UnicastPkts");
	BGE_SYSCTL_STAT(sc, ctx, "Outbound Multicast Packets",
	    children, txstats.ifHCOutMulticastPkts, "MulticastPkts");
	BGE_SYSCTL_STAT(sc, ctx, "Outbound Broadcast Packets",
	    children, txstats.ifHCOutBroadcastPkts, "BroadcastPkts");
	BGE_SYSCTL_STAT(sc, ctx, "Carrier Sense Errors",
	    children, txstats.dot3StatsCarrierSenseErrors,
	    "CarrierSenseErrors");
	BGE_SYSCTL_STAT(sc, ctx, "Outbound Discards",
	    children, txstats.ifOutDiscards, "Discards");
	BGE_SYSCTL_STAT(sc, ctx, "Outbound Errors",
	    children, txstats.ifOutErrors, "Errors");
}

#undef BGE_SYSCTL_STAT

#define	BGE_SYSCTL_STAT_ADD64(c, h, n, p, d)	\
	    SYSCTL_ADD_UQUAD(c, h, OID_AUTO, n, CTLFLAG_RD, p, d)

static void
bge_add_sysctl_stats_regs(struct bge_softc *sc, struct sysctl_ctx_list *ctx,
    struct sysctl_oid_list *parent)
{
	struct sysctl_oid *tree;
	struct sysctl_oid_list *child, *schild;
	struct bge_mac_stats *stats;

	stats = &sc->bge_mac_stats;
	tree = SYSCTL_ADD_NODE(ctx, parent, OID_AUTO, "stats", CTLFLAG_RD,
	    NULL, "BGE Statistics");
	schild = child = SYSCTL_CHILDREN(tree);
	BGE_SYSCTL_STAT_ADD64(ctx, child, "FramesDroppedDueToFilters",
	    &stats->FramesDroppedDueToFilters, "Frames Dropped Due to Filters");
	BGE_SYSCTL_STAT_ADD64(ctx, child, "DmaWriteQueueFull",
	    &stats->DmaWriteQueueFull, "NIC DMA Write Queue Full");
	BGE_SYSCTL_STAT_ADD64(ctx, child, "DmaWriteHighPriQueueFull",
	    &stats->DmaWriteHighPriQueueFull,
	    "NIC DMA Write High Priority Queue Full");
	BGE_SYSCTL_STAT_ADD64(ctx, child, "NoMoreRxBDs",
	    &stats->NoMoreRxBDs, "NIC No More RX Buffer Descriptors");
	BGE_SYSCTL_STAT_ADD64(ctx, child, "InputDiscards",
	    &stats->InputDiscards, "Discarded Input Frames");
	BGE_SYSCTL_STAT_ADD64(ctx, child, "InputErrors",
	    &stats->InputErrors, "Input Errors");
	BGE_SYSCTL_STAT_ADD64(ctx, child, "RecvThresholdHit",
	    &stats->RecvThresholdHit, "NIC Recv Threshold Hit");

	tree = SYSCTL_ADD_NODE(ctx, schild, OID_AUTO, "rx", CTLFLAG_RD,
	    NULL, "BGE RX Statistics");
	child = SYSCTL_CHILDREN(tree);
	BGE_SYSCTL_STAT_ADD64(ctx, child, "ifHCInOctets",
	    &stats->ifHCInOctets, "Inbound Octets");
	BGE_SYSCTL_STAT_ADD64(ctx, child, "Fragments",
	    &stats->etherStatsFragments, "Fragments");
	BGE_SYSCTL_STAT_ADD64(ctx, child, "UnicastPkts",
	    &stats->ifHCInUcastPkts, "Inbound Unicast Packets");
	BGE_SYSCTL_STAT_ADD64(ctx, child, "MulticastPkts",
	    &stats->ifHCInMulticastPkts, "Inbound Multicast Packets");
	BGE_SYSCTL_STAT_ADD64(ctx, child, "BroadcastPkts",
	    &stats->ifHCInBroadcastPkts, "Inbound Broadcast Packets");
	BGE_SYSCTL_STAT_ADD64(ctx, child, "FCSErrors",
	    &stats->dot3StatsFCSErrors, "FCS Errors");
	BGE_SYSCTL_STAT_ADD64(ctx, child, "AlignmentErrors",
	    &stats->dot3StatsAlignmentErrors, "Alignment Errors");
	BGE_SYSCTL_STAT_ADD64(ctx, child, "xonPauseFramesReceived",
	    &stats->xonPauseFramesReceived, "XON Pause Frames Received");
	BGE_SYSCTL_STAT_ADD64(ctx, child, "xoffPauseFramesReceived",
	    &stats->xoffPauseFramesReceived, "XOFF Pause Frames Received");
	BGE_SYSCTL_STAT_ADD64(ctx, child, "ControlFramesReceived",
	    &stats->macControlFramesReceived, "MAC Control Frames Received");
	BGE_SYSCTL_STAT_ADD64(ctx, child, "xoffStateEntered",
	    &stats->xoffStateEntered, "XOFF State Entered");
	BGE_SYSCTL_STAT_ADD64(ctx, child, "FramesTooLong",
	    &stats->dot3StatsFramesTooLong, "Frames Too Long");
	BGE_SYSCTL_STAT_ADD64(ctx, child, "Jabbers",
	    &stats->etherStatsJabbers, "Jabbers");
	BGE_SYSCTL_STAT_ADD64(ctx, child, "UndersizePkts",
	    &stats->etherStatsUndersizePkts, "Undersized Packets");

	tree = SYSCTL_ADD_NODE(ctx, schild, OID_AUTO, "tx", CTLFLAG_RD,
	    NULL, "BGE TX Statistics");
	child = SYSCTL_CHILDREN(tree);
	BGE_SYSCTL_STAT_ADD64(ctx, child, "ifHCOutOctets",
	    &stats->ifHCOutOctets, "Outbound Octets");
	BGE_SYSCTL_STAT_ADD64(ctx, child, "Collisions",
	    &stats->etherStatsCollisions, "TX Collisions");
	BGE_SYSCTL_STAT_ADD64(ctx, child, "XonSent",
	    &stats->outXonSent, "XON Sent");
	BGE_SYSCTL_STAT_ADD64(ctx, child, "XoffSent",
	    &stats->outXoffSent, "XOFF Sent");
	BGE_SYSCTL_STAT_ADD64(ctx, child, "InternalMacTransmitErrors",
	    &stats->dot3StatsInternalMacTransmitErrors,
	    "Internal MAC TX Errors");
	BGE_SYSCTL_STAT_ADD64(ctx, child, "SingleCollisionFrames",
	    &stats->dot3StatsSingleCollisionFrames, "Single Collision Frames");
	BGE_SYSCTL_STAT_ADD64(ctx, child, "MultipleCollisionFrames",
	    &stats->dot3StatsMultipleCollisionFrames,
	    "Multiple Collision Frames");
	BGE_SYSCTL_STAT_ADD64(ctx, child, "DeferredTransmissions",
	    &stats->dot3StatsDeferredTransmissions, "Deferred Transmissions");
	BGE_SYSCTL_STAT_ADD64(ctx, child, "ExcessiveCollisions",
	    &stats->dot3StatsExcessiveCollisions, "Excessive Collisions");
	BGE_SYSCTL_STAT_ADD64(ctx, child, "LateCollisions",
	    &stats->dot3StatsLateCollisions, "Late Collisions");
	BGE_SYSCTL_STAT_ADD64(ctx, child, "UnicastPkts",
	    &stats->ifHCOutUcastPkts, "Outbound Unicast Packets");
	BGE_SYSCTL_STAT_ADD64(ctx, child, "MulticastPkts",
	    &stats->ifHCOutMulticastPkts, "Outbound Multicast Packets");
	BGE_SYSCTL_STAT_ADD64(ctx, child, "BroadcastPkts",
	    &stats->ifHCOutBroadcastPkts, "Outbound Broadcast Packets");
}

#undef	BGE_SYSCTL_STAT_ADD64

static int
bge_sysctl_stats(SYSCTL_HANDLER_ARGS)
{
	struct bge_softc *sc;
	uint32_t result;
	int offset;

	sc = (struct bge_softc *)arg1;
	offset = arg2;
	result = CSR_READ_4(sc, BGE_MEMWIN_START + BGE_STATS_BLOCK + offset +
	    offsetof(bge_hostaddr, bge_addr_lo));
	return (sysctl_handle_int(oidp, &result, 0, req));
}

#ifdef BGE_REGISTER_DEBUG
static int
bge_sysctl_debug_info(SYSCTL_HANDLER_ARGS)
{
	struct bge_softc *sc;
	uint16_t *sbdata;
	int error, result, sbsz;
	int i, j;

	result = -1;
	error = sysctl_handle_int(oidp, &result, 0, req);
	if (error || (req->newptr == NULL))
		return (error);

	if (result == 1) {
		sc = (struct bge_softc *)arg1;

		if (sc->bge_asicrev == BGE_ASICREV_BCM5700 &&
		    sc->bge_chipid != BGE_CHIPID_BCM5700_C0)
			sbsz = BGE_STATUS_BLK_SZ;
		else
			sbsz = 32;
		sbdata = (uint16_t *)sc->bge_ldata.bge_status_block;
		printf("Status Block:\n");
		BGE_LOCK(sc);
		bus_dmamap_sync(sc->bge_cdata.bge_status_tag,
		    sc->bge_cdata.bge_status_map,
		    BUS_DMASYNC_POSTREAD | BUS_DMASYNC_POSTWRITE);
		for (i = 0x0; i < sbsz / sizeof(uint16_t); ) {
			printf("%06x:", i);
			for (j = 0; j < 8; j++)
				printf(" %04x", sbdata[i++]);
			printf("\n");
		}

		printf("Registers:\n");
		for (i = 0x800; i < 0xA00; ) {
			printf("%06x:", i);
			for (j = 0; j < 8; j++) {
				printf(" %08x", CSR_READ_4(sc, i));
				i += 4;
			}
			printf("\n");
		}
		BGE_UNLOCK(sc);

		printf("Hardware Flags:\n");
		if (BGE_IS_5717_PLUS(sc))
			printf(" - 5717 Plus\n");
		if (BGE_IS_5755_PLUS(sc))
			printf(" - 5755 Plus\n");
		if (BGE_IS_575X_PLUS(sc))
			printf(" - 575X Plus\n");
		if (BGE_IS_5705_PLUS(sc))
			printf(" - 5705 Plus\n");
		if (BGE_IS_5714_FAMILY(sc))
			printf(" - 5714 Family\n");
		if (BGE_IS_5700_FAMILY(sc))
			printf(" - 5700 Family\n");
		if (sc->bge_flags & BGE_FLAG_JUMBO)
			printf(" - Supports Jumbo Frames\n");
		if (sc->bge_flags & BGE_FLAG_PCIX)
			printf(" - PCI-X Bus\n");
		if (sc->bge_flags & BGE_FLAG_PCIE)
			printf(" - PCI Express Bus\n");
		if (sc->bge_phy_flags & BGE_PHY_NO_3LED)
			printf(" - No 3 LEDs\n");
		if (sc->bge_flags & BGE_FLAG_RX_ALIGNBUG)
			printf(" - RX Alignment Bug\n");
	}

	return (error);
}

static int
bge_sysctl_reg_read(SYSCTL_HANDLER_ARGS)
{
	struct bge_softc *sc;
	int error;
	uint16_t result;
	uint32_t val;

	result = -1;
	error = sysctl_handle_int(oidp, &result, 0, req);
	if (error || (req->newptr == NULL))
		return (error);

	if (result < 0x8000) {
		sc = (struct bge_softc *)arg1;
		val = CSR_READ_4(sc, result);
		printf("reg 0x%06X = 0x%08X\n", result, val);
	}

	return (error);
}

static int
bge_sysctl_ape_read(SYSCTL_HANDLER_ARGS)
{
	struct bge_softc *sc;
	int error;
	uint16_t result;
	uint32_t val;

	result = -1;
	error = sysctl_handle_int(oidp, &result, 0, req);
	if (error || (req->newptr == NULL))
		return (error);

	if (result < 0x8000) {
		sc = (struct bge_softc *)arg1;
		val = APE_READ_4(sc, result);
		printf("reg 0x%06X = 0x%08X\n", result, val);
	}

	return (error);
}

static int
bge_sysctl_mem_read(SYSCTL_HANDLER_ARGS)
{
	struct bge_softc *sc;
	int error;
	uint16_t result;
	uint32_t val;

	result = -1;
	error = sysctl_handle_int(oidp, &result, 0, req);
	if (error || (req->newptr == NULL))
		return (error);

	if (result < 0x8000) {
		sc = (struct bge_softc *)arg1;
		val = bge_readmem_ind(sc, result);
		printf("mem 0x%06X = 0x%08X\n", result, val);
	}

	return (error);
}
#endif

static int
bge_get_eaddr_fw(struct bge_softc *sc, uint8_t ether_addr[])
{
#ifdef __sparc64__
	if (sc->bge_flags & BGE_FLAG_EADDR)
		return (1);

	OF_getetheraddr(sc->bge_dev, ether_addr);
	return (0);
#else
	return (1);
#endif
}

static int
bge_get_eaddr_mem(struct bge_softc *sc, uint8_t ether_addr[])
{
	uint32_t mac_addr;

	mac_addr = bge_readmem_ind(sc, BGE_SRAM_MAC_ADDR_HIGH_MB);
	if ((mac_addr >> 16) == 0x484b) {
		ether_addr[0] = (uint8_t)(mac_addr >> 8);
		ether_addr[1] = (uint8_t)mac_addr;
		mac_addr = bge_readmem_ind(sc, BGE_SRAM_MAC_ADDR_LOW_MB);
		ether_addr[2] = (uint8_t)(mac_addr >> 24);
		ether_addr[3] = (uint8_t)(mac_addr >> 16);
		ether_addr[4] = (uint8_t)(mac_addr >> 8);
		ether_addr[5] = (uint8_t)mac_addr;
		return (0);
	}
	return (1);
}

static int
bge_get_eaddr_nvram(struct bge_softc *sc, uint8_t ether_addr[])
{
	int mac_offset = BGE_EE_MAC_OFFSET;

	if (sc->bge_asicrev == BGE_ASICREV_BCM5906)
		mac_offset = BGE_EE_MAC_OFFSET_5906;

	return (bge_read_nvram(sc, ether_addr, mac_offset + 2,
	    ETHER_ADDR_LEN));
}

static int
bge_get_eaddr_eeprom(struct bge_softc *sc, uint8_t ether_addr[])
{

	if (sc->bge_asicrev == BGE_ASICREV_BCM5906)
		return (1);

	return (bge_read_eeprom(sc, ether_addr, BGE_EE_MAC_OFFSET + 2,
	   ETHER_ADDR_LEN));
}

static int
bge_get_eaddr(struct bge_softc *sc, uint8_t eaddr[])
{
	static const bge_eaddr_fcn_t bge_eaddr_funcs[] = {
		/* NOTE: Order is critical */
		bge_get_eaddr_fw,
		bge_get_eaddr_mem,
		bge_get_eaddr_nvram,
		bge_get_eaddr_eeprom,
		NULL
	};
	const bge_eaddr_fcn_t *func;

	for (func = bge_eaddr_funcs; *func != NULL; ++func) {
		if ((*func)(sc, eaddr) == 0)
			break;
	}
	return (*func == NULL ? ENXIO : 0);
}

static uint64_t
bge_get_counter(if_t ifp, ift_counter cnt)
{
	struct bge_softc *sc;
	struct bge_mac_stats *stats;

	sc = if_getsoftc(ifp);
	if (!BGE_IS_5705_PLUS(sc))
		return (if_get_counter_default(ifp, cnt));
	stats = &sc->bge_mac_stats;

	switch (cnt) {
	case IFCOUNTER_IERRORS:
		return (stats->NoMoreRxBDs + stats->InputDiscards +
		    stats->InputErrors);
	case IFCOUNTER_COLLISIONS:
		return (stats->etherStatsCollisions);
	default:
		return (if_get_counter_default(ifp, cnt));
	}
}

#ifdef NETDUMP
static void
bge_netdump_init(if_t ifp, int *nrxr, int *ncl, int *clsize)
{
	struct bge_softc *sc;

	sc = if_getsoftc(ifp);
	BGE_LOCK(sc);
	*nrxr = sc->bge_return_ring_cnt;
	*ncl = NETDUMP_MAX_IN_FLIGHT;
	if ((sc->bge_flags & BGE_FLAG_JUMBO_STD) != 0 &&
	    (if_getmtu(sc->bge_ifp) + ETHER_HDR_LEN + ETHER_CRC_LEN +
	    ETHER_VLAN_ENCAP_LEN > (MCLBYTES - ETHER_ALIGN)))
		*clsize = MJUM9BYTES;
	else
		*clsize = MCLBYTES;
	BGE_UNLOCK(sc);
}

static void
bge_netdump_event(if_t ifp __unused, enum netdump_ev event __unused)
{
}

static int
bge_netdump_transmit(if_t ifp, struct mbuf *m)
{
	struct bge_softc *sc;
	uint32_t prodidx;
	int error;

	sc = if_getsoftc(ifp);
	if ((if_getdrvflags(ifp) & (IFF_DRV_RUNNING | IFF_DRV_OACTIVE)) !=
	    IFF_DRV_RUNNING)
		return (1);

	prodidx = sc->bge_tx_prodidx;
	error = bge_encap(sc, &m, &prodidx);
	if (error == 0)
		bge_start_tx(sc, prodidx);
	return (error);
}

static int
bge_netdump_poll(if_t ifp, int count)
{
	struct bge_softc *sc;
	uint32_t rx_prod, tx_cons;

	sc = if_getsoftc(ifp);
	if ((if_getdrvflags(ifp) & (IFF_DRV_RUNNING | IFF_DRV_OACTIVE)) !=
	    IFF_DRV_RUNNING)
		return (1);

	bus_dmamap_sync(sc->bge_cdata.bge_status_tag,
	    sc->bge_cdata.bge_status_map,
	    BUS_DMASYNC_POSTREAD | BUS_DMASYNC_POSTWRITE);

	rx_prod = sc->bge_ldata.bge_status_block->bge_idx[0].bge_rx_prod_idx;
	tx_cons = sc->bge_ldata.bge_status_block->bge_idx[0].bge_tx_cons_idx;

	bus_dmamap_sync(sc->bge_cdata.bge_status_tag,
	    sc->bge_cdata.bge_status_map,
	    BUS_DMASYNC_PREREAD | BUS_DMASYNC_PREWRITE);

	(void)bge_rxeof(sc, rx_prod, 0);
	bge_txeof(sc, tx_cons);
	return (0);
}
#endif /* NETDUMP */
