/*-
 * SPDX-License-Identifier: BSD-4-Clause
 *
 * Copyright (c) 1997, 1998, 1999
 *	Bill Paul <wpaul@ctr.columbia.edu>.  All rights reserved.
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

/*
 * Alteon Networks Tigon PCI gigabit ethernet driver for FreeBSD.
 * Manuals, sample driver and firmware source kits are available
 * from http://www.alteon.com/support/openkits.
 *
 * Written by Bill Paul <wpaul@ctr.columbia.edu>
 * Electrical Engineering Department
 * Columbia University, New York City
 */

/*
 * The Alteon Networks Tigon chip contains an embedded R4000 CPU,
 * gigabit MAC, dual DMA channels and a PCI interface unit. NICs
 * using the Tigon may have anywhere from 512K to 2MB of SRAM. The
 * Tigon supports hardware IP, TCP and UCP checksumming, multicast
 * filtering and jumbo (9014 byte) frames. The hardware is largely
 * controlled by firmware, which must be loaded into the NIC during
 * initialization.
 *
 * The Tigon 2 contains 2 R4000 CPUs and requires a newer firmware
 * revision, which supports new features such as extended commands,
 * extended jumbo receive ring desciptors and a mini receive ring.
 *
 * Alteon Networks is to be commended for releasing such a vast amount
 * of development material for the Tigon NIC without requiring an NDA
 * (although they really should have done it a long time ago). With
 * any luck, the other vendors will finally wise up and follow Alteon's
 * stellar example.
 *
 * The firmware for the Tigon 1 and 2 NICs is compiled directly into
 * this driver by #including it as a C header file. This bloats the
 * driver somewhat, but it's the easiest method considering that the
 * driver code and firmware code need to be kept in sync. The source
 * for the firmware is not provided with the FreeBSD distribution since
 * compiling it requires a GNU toolchain targeted for mips-sgi-irix5.3.
 *
 * The following people deserve special thanks:
 * - Terry Murphy of 3Com, for providing a 3c985 Tigon 1 board
 *   for testing
 * - Raymond Lee of Netgear, for providing a pair of Netgear
 *   GA620 Tigon 2 boards for testing
 * - Ulf Zimmermann, for bringing the GA260 to my attention and
 *   convincing me to write this driver.
 * - Andrew Gallatin for providing FreeBSD/Alpha support.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include "opt_ti.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/sockio.h>
#include <sys/mbuf.h>
#include <sys/malloc.h>
#include <sys/kernel.h>
#include <sys/module.h>
#include <sys/socket.h>
#include <sys/queue.h>
#include <sys/conf.h>
#include <sys/sf_buf.h>

#include <net/if.h>
#include <net/if_var.h>
#include <net/if_arp.h>
#include <net/ethernet.h>
#include <net/if_dl.h>
#include <net/if_media.h>
#include <net/if_types.h>
#include <net/if_vlan_var.h>

#include <net/bpf.h>

#include <netinet/in_systm.h>
#include <netinet/in.h>
#include <netinet/ip.h>

#include <machine/bus.h>
#include <machine/resource.h>
#include <sys/bus.h>
#include <sys/rman.h>

#ifdef TI_SF_BUF_JUMBO
#include <vm/vm.h>
#include <vm/vm_page.h>
#endif

#include <dev/pci/pcireg.h>
#include <dev/pci/pcivar.h>

#include <sys/tiio.h>
#include <dev/ti/if_tireg.h>
#include <dev/ti/ti_fw.h>
#include <dev/ti/ti_fw2.h>

#include <sys/sysctl.h>

#define TI_CSUM_FEATURES	(CSUM_IP | CSUM_TCP | CSUM_UDP)
/*
 * We can only turn on header splitting if we're using extended receive
 * BDs.
 */
#if defined(TI_JUMBO_HDRSPLIT) && !defined(TI_SF_BUF_JUMBO)
#error "options TI_JUMBO_HDRSPLIT requires TI_SF_BUF_JUMBO"
#endif /* TI_JUMBO_HDRSPLIT && !TI_SF_BUF_JUMBO */

typedef enum {
	TI_SWAP_HTON,
	TI_SWAP_NTOH
} ti_swap_type;

/*
 * Various supported device vendors/types and their names.
 */

static const struct ti_type ti_devs[] = {
	{ ALT_VENDORID,	ALT_DEVICEID_ACENIC,
		"Alteon AceNIC 1000baseSX Gigabit Ethernet" },
	{ ALT_VENDORID,	ALT_DEVICEID_ACENIC_COPPER,
		"Alteon AceNIC 1000baseT Gigabit Ethernet" },
	{ TC_VENDORID,	TC_DEVICEID_3C985,
		"3Com 3c985-SX Gigabit Ethernet" },
	{ NG_VENDORID, NG_DEVICEID_GA620,
		"Netgear GA620 1000baseSX Gigabit Ethernet" },
	{ NG_VENDORID, NG_DEVICEID_GA620T,
		"Netgear GA620 1000baseT Gigabit Ethernet" },
	{ SGI_VENDORID, SGI_DEVICEID_TIGON,
		"Silicon Graphics Gigabit Ethernet" },
	{ DEC_VENDORID, DEC_DEVICEID_FARALLON_PN9000SX,
		"Farallon PN9000SX Gigabit Ethernet" },
	{ 0, 0, NULL }
};


static	d_open_t	ti_open;
static	d_close_t	ti_close;
static	d_ioctl_t	ti_ioctl2;

static struct cdevsw ti_cdevsw = {
	.d_version =	D_VERSION,
	.d_flags =	0,
	.d_open =	ti_open,
	.d_close =	ti_close,
	.d_ioctl =	ti_ioctl2,
	.d_name =	"ti",
};

static int ti_probe(device_t);
static int ti_attach(device_t);
static int ti_detach(device_t);
static void ti_txeof(struct ti_softc *);
static void ti_rxeof(struct ti_softc *);

static int ti_encap(struct ti_softc *, struct mbuf **);

static void ti_intr(void *);
static void ti_start(struct ifnet *);
static void ti_start_locked(struct ifnet *);
static int ti_ioctl(struct ifnet *, u_long, caddr_t);
static uint64_t ti_get_counter(struct ifnet *, ift_counter);
static void ti_init(void *);
static void ti_init_locked(void *);
static void ti_init2(struct ti_softc *);
static void ti_stop(struct ti_softc *);
static void ti_watchdog(void *);
static int ti_shutdown(device_t);
static int ti_ifmedia_upd(struct ifnet *);
static int ti_ifmedia_upd_locked(struct ti_softc *);
static void ti_ifmedia_sts(struct ifnet *, struct ifmediareq *);

static uint32_t ti_eeprom_putbyte(struct ti_softc *, int);
static uint8_t	ti_eeprom_getbyte(struct ti_softc *, int, uint8_t *);
static int ti_read_eeprom(struct ti_softc *, caddr_t, int, int);

static void ti_add_mcast(struct ti_softc *, struct ether_addr *);
static void ti_del_mcast(struct ti_softc *, struct ether_addr *);
static void ti_setmulti(struct ti_softc *);

static void ti_mem_read(struct ti_softc *, uint32_t, uint32_t, void *);
static void ti_mem_write(struct ti_softc *, uint32_t, uint32_t, void *);
static void ti_mem_zero(struct ti_softc *, uint32_t, uint32_t);
static int ti_copy_mem(struct ti_softc *, uint32_t, uint32_t, caddr_t, int,
    int);
static int ti_copy_scratch(struct ti_softc *, uint32_t, uint32_t, caddr_t,
    int, int, int);
static int ti_bcopy_swap(const void *, void *, size_t, ti_swap_type);
static void ti_loadfw(struct ti_softc *);
static void ti_cmd(struct ti_softc *, struct ti_cmd_desc *);
static void ti_cmd_ext(struct ti_softc *, struct ti_cmd_desc *, caddr_t, int);
static void ti_handle_events(struct ti_softc *);
static void ti_dma_map_addr(void *, bus_dma_segment_t *, int, int);
static int ti_dma_alloc(struct ti_softc *);
static void ti_dma_free(struct ti_softc *);
static int ti_dma_ring_alloc(struct ti_softc *, bus_size_t, bus_size_t,
    bus_dma_tag_t *, uint8_t **, bus_dmamap_t *, bus_addr_t *, const char *);
static void ti_dma_ring_free(struct ti_softc *, bus_dma_tag_t *, uint8_t **,
    bus_dmamap_t, bus_addr_t *);
static int ti_newbuf_std(struct ti_softc *, int);
static int ti_newbuf_mini(struct ti_softc *, int);
static int ti_newbuf_jumbo(struct ti_softc *, int, struct mbuf *);
static int ti_init_rx_ring_std(struct ti_softc *);
static void ti_free_rx_ring_std(struct ti_softc *);
static int ti_init_rx_ring_jumbo(struct ti_softc *);
static void ti_free_rx_ring_jumbo(struct ti_softc *);
static int ti_init_rx_ring_mini(struct ti_softc *);
static void ti_free_rx_ring_mini(struct ti_softc *);
static void ti_free_tx_ring(struct ti_softc *);
static int ti_init_tx_ring(struct ti_softc *);
static void ti_discard_std(struct ti_softc *, int);
#ifndef TI_SF_BUF_JUMBO
static void ti_discard_jumbo(struct ti_softc *, int);
#endif
static void ti_discard_mini(struct ti_softc *, int);

static int ti_64bitslot_war(struct ti_softc *);
static int ti_chipinit(struct ti_softc *);
static int ti_gibinit(struct ti_softc *);

#ifdef TI_JUMBO_HDRSPLIT
static __inline void ti_hdr_split(struct mbuf *top, int hdr_len, int pkt_len,
    int idx);
#endif /* TI_JUMBO_HDRSPLIT */

static void ti_sysctl_node(struct ti_softc *);

static device_method_t ti_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe,		ti_probe),
	DEVMETHOD(device_attach,	ti_attach),
	DEVMETHOD(device_detach,	ti_detach),
	DEVMETHOD(device_shutdown,	ti_shutdown),
	{ 0, 0 }
};

static driver_t ti_driver = {
	"ti",
	ti_methods,
	sizeof(struct ti_softc)
};

static devclass_t ti_devclass;

DRIVER_MODULE(ti, pci, ti_driver, ti_devclass, 0, 0);
MODULE_DEPEND(ti, pci, 1, 1, 1);
MODULE_DEPEND(ti, ether, 1, 1, 1);

/*
 * Send an instruction or address to the EEPROM, check for ACK.
 */
static uint32_t
ti_eeprom_putbyte(struct ti_softc *sc, int byte)
{
	int i, ack = 0;

	/*
	 * Make sure we're in TX mode.
	 */
	TI_SETBIT(sc, TI_MISC_LOCAL_CTL, TI_MLC_EE_TXEN);

	/*
	 * Feed in each bit and stobe the clock.
	 */
	for (i = 0x80; i; i >>= 1) {
		if (byte & i) {
			TI_SETBIT(sc, TI_MISC_LOCAL_CTL, TI_MLC_EE_DOUT);
		} else {
			TI_CLRBIT(sc, TI_MISC_LOCAL_CTL, TI_MLC_EE_DOUT);
		}
		DELAY(1);
		TI_SETBIT(sc, TI_MISC_LOCAL_CTL, TI_MLC_EE_CLK);
		DELAY(1);
		TI_CLRBIT(sc, TI_MISC_LOCAL_CTL, TI_MLC_EE_CLK);
	}

	/*
	 * Turn off TX mode.
	 */
	TI_CLRBIT(sc, TI_MISC_LOCAL_CTL, TI_MLC_EE_TXEN);

	/*
	 * Check for ack.
	 */
	TI_SETBIT(sc, TI_MISC_LOCAL_CTL, TI_MLC_EE_CLK);
	ack = CSR_READ_4(sc, TI_MISC_LOCAL_CTL) & TI_MLC_EE_DIN;
	TI_CLRBIT(sc, TI_MISC_LOCAL_CTL, TI_MLC_EE_CLK);

	return (ack);
}

/*
 * Read a byte of data stored in the EEPROM at address 'addr.'
 * We have to send two address bytes since the EEPROM can hold
 * more than 256 bytes of data.
 */
static uint8_t
ti_eeprom_getbyte(struct ti_softc *sc, int addr, uint8_t *dest)
{
	int i;
	uint8_t byte = 0;

	EEPROM_START;

	/*
	 * Send write control code to EEPROM.
	 */
	if (ti_eeprom_putbyte(sc, EEPROM_CTL_WRITE)) {
		device_printf(sc->ti_dev,
		    "failed to send write command, status: %x\n",
		    CSR_READ_4(sc, TI_MISC_LOCAL_CTL));
		return (1);
	}

	/*
	 * Send first byte of address of byte we want to read.
	 */
	if (ti_eeprom_putbyte(sc, (addr >> 8) & 0xFF)) {
		device_printf(sc->ti_dev, "failed to send address, status: %x\n",
		    CSR_READ_4(sc, TI_MISC_LOCAL_CTL));
		return (1);
	}
	/*
	 * Send second byte address of byte we want to read.
	 */
	if (ti_eeprom_putbyte(sc, addr & 0xFF)) {
		device_printf(sc->ti_dev, "failed to send address, status: %x\n",
		    CSR_READ_4(sc, TI_MISC_LOCAL_CTL));
		return (1);
	}

	EEPROM_STOP;
	EEPROM_START;
	/*
	 * Send read control code to EEPROM.
	 */
	if (ti_eeprom_putbyte(sc, EEPROM_CTL_READ)) {
		device_printf(sc->ti_dev,
		    "failed to send read command, status: %x\n",
		    CSR_READ_4(sc, TI_MISC_LOCAL_CTL));
		return (1);
	}

	/*
	 * Start reading bits from EEPROM.
	 */
	TI_CLRBIT(sc, TI_MISC_LOCAL_CTL, TI_MLC_EE_TXEN);
	for (i = 0x80; i; i >>= 1) {
		TI_SETBIT(sc, TI_MISC_LOCAL_CTL, TI_MLC_EE_CLK);
		DELAY(1);
		if (CSR_READ_4(sc, TI_MISC_LOCAL_CTL) & TI_MLC_EE_DIN)
			byte |= i;
		TI_CLRBIT(sc, TI_MISC_LOCAL_CTL, TI_MLC_EE_CLK);
		DELAY(1);
	}

	EEPROM_STOP;

	/*
	 * No ACK generated for read, so just return byte.
	 */

	*dest = byte;

	return (0);
}

/*
 * Read a sequence of bytes from the EEPROM.
 */
static int
ti_read_eeprom(struct ti_softc *sc, caddr_t dest, int off, int cnt)
{
	int err = 0, i;
	uint8_t byte = 0;

	for (i = 0; i < cnt; i++) {
		err = ti_eeprom_getbyte(sc, off + i, &byte);
		if (err)
			break;
		*(dest + i) = byte;
	}

	return (err ? 1 : 0);
}

/*
 * NIC memory read function.
 * Can be used to copy data from NIC local memory.
 */
static void
ti_mem_read(struct ti_softc *sc, uint32_t addr, uint32_t len, void *buf)
{
	int segptr, segsize, cnt;
	char *ptr;

	segptr = addr;
	cnt = len;
	ptr = buf;

	while (cnt) {
		if (cnt < TI_WINLEN)
			segsize = cnt;
		else
			segsize = TI_WINLEN - (segptr % TI_WINLEN);
		CSR_WRITE_4(sc, TI_WINBASE, rounddown2(segptr, TI_WINLEN));
		bus_space_read_region_4(sc->ti_btag, sc->ti_bhandle,
		    TI_WINDOW + (segptr & (TI_WINLEN - 1)), (uint32_t *)ptr,
		    segsize / 4);
		ptr += segsize;
		segptr += segsize;
		cnt -= segsize;
	}
}


/*
 * NIC memory write function.
 * Can be used to copy data into NIC local memory.
 */
static void
ti_mem_write(struct ti_softc *sc, uint32_t addr, uint32_t len, void *buf)
{
	int segptr, segsize, cnt;
	char *ptr;

	segptr = addr;
	cnt = len;
	ptr = buf;

	while (cnt) {
		if (cnt < TI_WINLEN)
			segsize = cnt;
		else
			segsize = TI_WINLEN - (segptr % TI_WINLEN);
		CSR_WRITE_4(sc, TI_WINBASE, rounddown2(segptr, TI_WINLEN));
		bus_space_write_region_4(sc->ti_btag, sc->ti_bhandle,
		    TI_WINDOW + (segptr & (TI_WINLEN - 1)), (uint32_t *)ptr,
		    segsize / 4);
		ptr += segsize;
		segptr += segsize;
		cnt -= segsize;
	}
}

/*
 * NIC memory read function.
 * Can be used to clear a section of NIC local memory.
 */
static void
ti_mem_zero(struct ti_softc *sc, uint32_t addr, uint32_t len)
{
	int segptr, segsize, cnt;

	segptr = addr;
	cnt = len;

	while (cnt) {
		if (cnt < TI_WINLEN)
			segsize = cnt;
		else
			segsize = TI_WINLEN - (segptr % TI_WINLEN);
		CSR_WRITE_4(sc, TI_WINBASE, rounddown2(segptr, TI_WINLEN));
		bus_space_set_region_4(sc->ti_btag, sc->ti_bhandle,
		    TI_WINDOW + (segptr & (TI_WINLEN - 1)), 0, segsize / 4);
		segptr += segsize;
		cnt -= segsize;
	}
}

static int
ti_copy_mem(struct ti_softc *sc, uint32_t tigon_addr, uint32_t len,
    caddr_t buf, int useraddr, int readdata)
{
	int segptr, segsize, cnt;
	caddr_t ptr;
	uint32_t origwin;
	int resid, segresid;
	int first_pass;

	TI_LOCK_ASSERT(sc);

	/*
	 * At the moment, we don't handle non-aligned cases, we just bail.
	 * If this proves to be a problem, it will be fixed.
	 */
	if (readdata == 0 && (tigon_addr & 0x3) != 0) {
		device_printf(sc->ti_dev, "%s: tigon address %#x isn't "
		    "word-aligned\n", __func__, tigon_addr);
		device_printf(sc->ti_dev, "%s: unaligned writes aren't "
		    "yet supported\n", __func__);
		return (EINVAL);
	}

	segptr = tigon_addr & ~0x3;
	segresid = tigon_addr - segptr;

	/*
	 * This is the non-aligned amount left over that we'll need to
	 * copy.
	 */
	resid = len & 0x3;

	/* Add in the left over amount at the front of the buffer */
	resid += segresid;

	cnt = len & ~0x3;
	/*
	 * If resid + segresid is >= 4, add multiples of 4 to the count and
	 * decrease the residual by that much.
	 */
	cnt += resid & ~0x3;
	resid -= resid & ~0x3;

	ptr = buf;

	first_pass = 1;

	/*
	 * Save the old window base value.
	 */
	origwin = CSR_READ_4(sc, TI_WINBASE);

	while (cnt) {
		bus_size_t ti_offset;

		if (cnt < TI_WINLEN)
			segsize = cnt;
		else
			segsize = TI_WINLEN - (segptr % TI_WINLEN);
		CSR_WRITE_4(sc, TI_WINBASE, rounddown2(segptr, TI_WINLEN));

		ti_offset = TI_WINDOW + (segptr & (TI_WINLEN -1));

		if (readdata) {
			bus_space_read_region_4(sc->ti_btag, sc->ti_bhandle,
			    ti_offset, (uint32_t *)sc->ti_membuf, segsize >> 2);
			if (useraddr) {
				/*
				 * Yeah, this is a little on the kludgy
				 * side, but at least this code is only
				 * used for debugging.
				 */
				ti_bcopy_swap(sc->ti_membuf, sc->ti_membuf2,
				    segsize, TI_SWAP_NTOH);

				TI_UNLOCK(sc);
				if (first_pass) {
					copyout(&sc->ti_membuf2[segresid], ptr,
					    segsize - segresid);
					first_pass = 0;
				} else
					copyout(sc->ti_membuf2, ptr, segsize);
				TI_LOCK(sc);
			} else {
				if (first_pass) {

					ti_bcopy_swap(sc->ti_membuf,
					    sc->ti_membuf2, segsize,
					    TI_SWAP_NTOH);
					TI_UNLOCK(sc);
					bcopy(&sc->ti_membuf2[segresid], ptr,
					    segsize - segresid);
					TI_LOCK(sc);
					first_pass = 0;
				} else
					ti_bcopy_swap(sc->ti_membuf, ptr,
					    segsize, TI_SWAP_NTOH);
			}

		} else {
			if (useraddr) {
				TI_UNLOCK(sc);
				copyin(ptr, sc->ti_membuf2, segsize);
				TI_LOCK(sc);
				ti_bcopy_swap(sc->ti_membuf2, sc->ti_membuf,
				    segsize, TI_SWAP_HTON);
			} else
				ti_bcopy_swap(ptr, sc->ti_membuf, segsize,
				    TI_SWAP_HTON);

			bus_space_write_region_4(sc->ti_btag, sc->ti_bhandle,
			    ti_offset, (uint32_t *)sc->ti_membuf, segsize >> 2);
		}
		segptr += segsize;
		ptr += segsize;
		cnt -= segsize;
	}

	/*
	 * Handle leftover, non-word-aligned bytes.
	 */
	if (resid != 0) {
		uint32_t tmpval, tmpval2;
		bus_size_t ti_offset;

		/*
		 * Set the segment pointer.
		 */
		CSR_WRITE_4(sc, TI_WINBASE, rounddown2(segptr, TI_WINLEN));

		ti_offset = TI_WINDOW + (segptr & (TI_WINLEN - 1));

		/*
		 * First, grab whatever is in our source/destination.
		 * We'll obviously need this for reads, but also for
		 * writes, since we'll be doing read/modify/write.
		 */
		bus_space_read_region_4(sc->ti_btag, sc->ti_bhandle,
		    ti_offset, &tmpval, 1);

		/*
		 * Next, translate this from little-endian to big-endian
		 * (at least on i386 boxes).
		 */
		tmpval2 = ntohl(tmpval);

		if (readdata) {
			/*
			 * If we're reading, just copy the leftover number
			 * of bytes from the host byte order buffer to
			 * the user's buffer.
			 */
			if (useraddr) {
				TI_UNLOCK(sc);
				copyout(&tmpval2, ptr, resid);
				TI_LOCK(sc);
			} else
				bcopy(&tmpval2, ptr, resid);
		} else {
			/*
			 * If we're writing, first copy the bytes to be
			 * written into the network byte order buffer,
			 * leaving the rest of the buffer with whatever was
			 * originally in there.  Then, swap the bytes
			 * around into host order and write them out.
			 *
			 * XXX KDM the read side of this has been verified
			 * to work, but the write side of it has not been
			 * verified.  So user beware.
			 */
			if (useraddr) {
				TI_UNLOCK(sc);
				copyin(ptr, &tmpval2, resid);
				TI_LOCK(sc);
			} else
				bcopy(ptr, &tmpval2, resid);

			tmpval = htonl(tmpval2);

			bus_space_write_region_4(sc->ti_btag, sc->ti_bhandle,
			    ti_offset, &tmpval, 1);
		}
	}

	CSR_WRITE_4(sc, TI_WINBASE, origwin);

	return (0);
}

static int
ti_copy_scratch(struct ti_softc *sc, uint32_t tigon_addr, uint32_t len,
    caddr_t buf, int useraddr, int readdata, int cpu)
{
	uint32_t segptr;
	int cnt;
	uint32_t tmpval, tmpval2;
	caddr_t ptr;

	TI_LOCK_ASSERT(sc);

	/*
	 * At the moment, we don't handle non-aligned cases, we just bail.
	 * If this proves to be a problem, it will be fixed.
	 */
	if (tigon_addr & 0x3) {
		device_printf(sc->ti_dev, "%s: tigon address %#x "
		    "isn't word-aligned\n", __func__, tigon_addr);
		return (EINVAL);
	}

	if (len & 0x3) {
		device_printf(sc->ti_dev, "%s: transfer length %d "
		    "isn't word-aligned\n", __func__, len);
		return (EINVAL);
	}

	segptr = tigon_addr;
	cnt = len;
	ptr = buf;

	while (cnt) {
		CSR_WRITE_4(sc, CPU_REG(TI_SRAM_ADDR, cpu), segptr);

		if (readdata) {
			tmpval2 = CSR_READ_4(sc, CPU_REG(TI_SRAM_DATA, cpu));

			tmpval = ntohl(tmpval2);

			/*
			 * Note:  I've used this debugging interface
			 * extensively with Alteon's 12.3.15 firmware,
			 * compiled with GCC 2.7.2.1 and binutils 2.9.1.
			 *
			 * When you compile the firmware without
			 * optimization, which is necessary sometimes in
			 * order to properly step through it, you sometimes
			 * read out a bogus value of 0xc0017c instead of
			 * whatever was supposed to be in that scratchpad
			 * location.  That value is on the stack somewhere,
			 * but I've never been able to figure out what was
			 * causing the problem.
			 *
			 * The address seems to pop up in random places,
			 * often not in the same place on two subsequent
			 * reads.
			 *
			 * In any case, the underlying data doesn't seem
			 * to be affected, just the value read out.
			 *
			 * KDM, 3/7/2000
			 */

			if (tmpval2 == 0xc0017c)
				device_printf(sc->ti_dev, "found 0xc0017c at "
				    "%#x (tmpval2)\n", segptr);

			if (tmpval == 0xc0017c)
				device_printf(sc->ti_dev, "found 0xc0017c at "
				    "%#x (tmpval)\n", segptr);

			if (useraddr)
				copyout(&tmpval, ptr, 4);
			else
				bcopy(&tmpval, ptr, 4);
		} else {
			if (useraddr)
				copyin(ptr, &tmpval2, 4);
			else
				bcopy(ptr, &tmpval2, 4);

			tmpval = htonl(tmpval2);

			CSR_WRITE_4(sc, CPU_REG(TI_SRAM_DATA, cpu), tmpval);
		}

		cnt -= 4;
		segptr += 4;
		ptr += 4;
	}

	return (0);
}

static int
ti_bcopy_swap(const void *src, void *dst, size_t len, ti_swap_type swap_type)
{
	const uint8_t *tmpsrc;
	uint8_t *tmpdst;
	size_t tmplen;

	if (len & 0x3) {
		printf("ti_bcopy_swap: length %zd isn't 32-bit aligned\n", len);
		return (-1);
	}

	tmpsrc = src;
	tmpdst = dst;
	tmplen = len;

	while (tmplen) {
		if (swap_type == TI_SWAP_NTOH)
			*(uint32_t *)tmpdst = ntohl(*(const uint32_t *)tmpsrc);
		else
			*(uint32_t *)tmpdst = htonl(*(const uint32_t *)tmpsrc);
		tmpsrc += 4;
		tmpdst += 4;
		tmplen -= 4;
	}

	return (0);
}

/*
 * Load firmware image into the NIC. Check that the firmware revision
 * is acceptable and see if we want the firmware for the Tigon 1 or
 * Tigon 2.
 */
static void
ti_loadfw(struct ti_softc *sc)
{

	TI_LOCK_ASSERT(sc);

	switch (sc->ti_hwrev) {
	case TI_HWREV_TIGON:
		if (tigonFwReleaseMajor != TI_FIRMWARE_MAJOR ||
		    tigonFwReleaseMinor != TI_FIRMWARE_MINOR ||
		    tigonFwReleaseFix != TI_FIRMWARE_FIX) {
			device_printf(sc->ti_dev, "firmware revision mismatch; "
			    "want %d.%d.%d, got %d.%d.%d\n",
			    TI_FIRMWARE_MAJOR, TI_FIRMWARE_MINOR,
			    TI_FIRMWARE_FIX, tigonFwReleaseMajor,
			    tigonFwReleaseMinor, tigonFwReleaseFix);
			return;
		}
		ti_mem_write(sc, tigonFwTextAddr, tigonFwTextLen, tigonFwText);
		ti_mem_write(sc, tigonFwDataAddr, tigonFwDataLen, tigonFwData);
		ti_mem_write(sc, tigonFwRodataAddr, tigonFwRodataLen,
		    tigonFwRodata);
		ti_mem_zero(sc, tigonFwBssAddr, tigonFwBssLen);
		ti_mem_zero(sc, tigonFwSbssAddr, tigonFwSbssLen);
		CSR_WRITE_4(sc, TI_CPU_PROGRAM_COUNTER, tigonFwStartAddr);
		break;
	case TI_HWREV_TIGON_II:
		if (tigon2FwReleaseMajor != TI_FIRMWARE_MAJOR ||
		    tigon2FwReleaseMinor != TI_FIRMWARE_MINOR ||
		    tigon2FwReleaseFix != TI_FIRMWARE_FIX) {
			device_printf(sc->ti_dev, "firmware revision mismatch; "
			    "want %d.%d.%d, got %d.%d.%d\n",
			    TI_FIRMWARE_MAJOR, TI_FIRMWARE_MINOR,
			    TI_FIRMWARE_FIX, tigon2FwReleaseMajor,
			    tigon2FwReleaseMinor, tigon2FwReleaseFix);
			return;
		}
		ti_mem_write(sc, tigon2FwTextAddr, tigon2FwTextLen,
		    tigon2FwText);
		ti_mem_write(sc, tigon2FwDataAddr, tigon2FwDataLen,
		    tigon2FwData);
		ti_mem_write(sc, tigon2FwRodataAddr, tigon2FwRodataLen,
		    tigon2FwRodata);
		ti_mem_zero(sc, tigon2FwBssAddr, tigon2FwBssLen);
		ti_mem_zero(sc, tigon2FwSbssAddr, tigon2FwSbssLen);
		CSR_WRITE_4(sc, TI_CPU_PROGRAM_COUNTER, tigon2FwStartAddr);
		break;
	default:
		device_printf(sc->ti_dev,
		    "can't load firmware: unknown hardware rev\n");
		break;
	}
}

/*
 * Send the NIC a command via the command ring.
 */
static void
ti_cmd(struct ti_softc *sc, struct ti_cmd_desc *cmd)
{
	int index;

	index = sc->ti_cmd_saved_prodidx;
	CSR_WRITE_4(sc, TI_GCR_CMDRING + (index * 4), *(uint32_t *)(cmd));
	TI_INC(index, TI_CMD_RING_CNT);
	CSR_WRITE_4(sc, TI_MB_CMDPROD_IDX, index);
	sc->ti_cmd_saved_prodidx = index;
}

/*
 * Send the NIC an extended command. The 'len' parameter specifies the
 * number of command slots to include after the initial command.
 */
static void
ti_cmd_ext(struct ti_softc *sc, struct ti_cmd_desc *cmd, caddr_t arg, int len)
{
	int index;
	int i;

	index = sc->ti_cmd_saved_prodidx;
	CSR_WRITE_4(sc, TI_GCR_CMDRING + (index * 4), *(uint32_t *)(cmd));
	TI_INC(index, TI_CMD_RING_CNT);
	for (i = 0; i < len; i++) {
		CSR_WRITE_4(sc, TI_GCR_CMDRING + (index * 4),
		    *(uint32_t *)(&arg[i * 4]));
		TI_INC(index, TI_CMD_RING_CNT);
	}
	CSR_WRITE_4(sc, TI_MB_CMDPROD_IDX, index);
	sc->ti_cmd_saved_prodidx = index;
}

/*
 * Handle events that have triggered interrupts.
 */
static void
ti_handle_events(struct ti_softc *sc)
{
	struct ti_event_desc *e;

	if (sc->ti_rdata.ti_event_ring == NULL)
		return;

	bus_dmamap_sync(sc->ti_cdata.ti_event_ring_tag,
	    sc->ti_cdata.ti_event_ring_map, BUS_DMASYNC_POSTREAD);
	while (sc->ti_ev_saved_considx != sc->ti_ev_prodidx.ti_idx) {
		e = &sc->ti_rdata.ti_event_ring[sc->ti_ev_saved_considx];
		switch (TI_EVENT_EVENT(e)) {
		case TI_EV_LINKSTAT_CHANGED:
			sc->ti_linkstat = TI_EVENT_CODE(e);
			if (sc->ti_linkstat == TI_EV_CODE_LINK_UP) {
				if_link_state_change(sc->ti_ifp, LINK_STATE_UP);
				sc->ti_ifp->if_baudrate = IF_Mbps(100);
				if (bootverbose)
					device_printf(sc->ti_dev,
					    "10/100 link up\n");
			} else if (sc->ti_linkstat == TI_EV_CODE_GIG_LINK_UP) {
				if_link_state_change(sc->ti_ifp, LINK_STATE_UP);
				sc->ti_ifp->if_baudrate = IF_Gbps(1UL);
				if (bootverbose)
					device_printf(sc->ti_dev,
					    "gigabit link up\n");
			} else if (sc->ti_linkstat == TI_EV_CODE_LINK_DOWN) {
				if_link_state_change(sc->ti_ifp,
				    LINK_STATE_DOWN);
				sc->ti_ifp->if_baudrate = 0;
				if (bootverbose)
					device_printf(sc->ti_dev,
					    "link down\n");
			}
			break;
		case TI_EV_ERROR:
			if (TI_EVENT_CODE(e) == TI_EV_CODE_ERR_INVAL_CMD)
				device_printf(sc->ti_dev, "invalid command\n");
			else if (TI_EVENT_CODE(e) == TI_EV_CODE_ERR_UNIMP_CMD)
				device_printf(sc->ti_dev, "unknown command\n");
			else if (TI_EVENT_CODE(e) == TI_EV_CODE_ERR_BADCFG)
				device_printf(sc->ti_dev, "bad config data\n");
			break;
		case TI_EV_FIRMWARE_UP:
			ti_init2(sc);
			break;
		case TI_EV_STATS_UPDATED:
		case TI_EV_RESET_JUMBO_RING:
		case TI_EV_MCAST_UPDATED:
			/* Who cares. */
			break;
		default:
			device_printf(sc->ti_dev, "unknown event: %d\n",
			    TI_EVENT_EVENT(e));
			break;
		}
		/* Advance the consumer index. */
		TI_INC(sc->ti_ev_saved_considx, TI_EVENT_RING_CNT);
		CSR_WRITE_4(sc, TI_GCR_EVENTCONS_IDX, sc->ti_ev_saved_considx);
	}
	bus_dmamap_sync(sc->ti_cdata.ti_event_ring_tag,
	    sc->ti_cdata.ti_event_ring_map, BUS_DMASYNC_PREREAD);
}

struct ti_dmamap_arg {
	bus_addr_t	ti_busaddr;
};

static void
ti_dma_map_addr(void *arg, bus_dma_segment_t *segs, int nseg, int error)
{
	struct ti_dmamap_arg *ctx;

	if (error)
		return;

	KASSERT(nseg == 1, ("%s: %d segments returned!", __func__, nseg));

	ctx = arg;
	ctx->ti_busaddr = segs->ds_addr;
}

static int
ti_dma_ring_alloc(struct ti_softc *sc, bus_size_t alignment, bus_size_t maxsize,
    bus_dma_tag_t *tag, uint8_t **ring, bus_dmamap_t *map, bus_addr_t *paddr,
    const char *msg)
{
	struct ti_dmamap_arg ctx;
	int error;

	error = bus_dma_tag_create(sc->ti_cdata.ti_parent_tag,
	    alignment, 0, BUS_SPACE_MAXADDR, BUS_SPACE_MAXADDR, NULL,
	    NULL, maxsize, 1, maxsize, 0, NULL, NULL, tag);
	if (error != 0) {
		device_printf(sc->ti_dev,
		    "could not create %s dma tag\n", msg);
		return (error);
	}
	/* Allocate DMA'able memory for ring. */
	error = bus_dmamem_alloc(*tag, (void **)ring,
	    BUS_DMA_NOWAIT | BUS_DMA_ZERO | BUS_DMA_COHERENT, map);
	if (error != 0) {
		device_printf(sc->ti_dev,
		    "could not allocate DMA'able memory for %s\n", msg);
		return (error);
	}
	/* Load the address of the ring. */
	ctx.ti_busaddr = 0;
	error = bus_dmamap_load(*tag, *map, *ring, maxsize, ti_dma_map_addr,
	    &ctx, BUS_DMA_NOWAIT);
	if (error != 0) {
		device_printf(sc->ti_dev,
		    "could not load DMA'able memory for %s\n", msg);
		return (error);
	}
	*paddr = ctx.ti_busaddr;
	return (0);
}

static void
ti_dma_ring_free(struct ti_softc *sc, bus_dma_tag_t *tag, uint8_t **ring,
    bus_dmamap_t map, bus_addr_t *paddr)
{

	if (*paddr != 0) {
		bus_dmamap_unload(*tag, map);
		*paddr = 0;
	}
	if (*ring != NULL) {
		bus_dmamem_free(*tag, *ring, map);
		*ring = NULL;
	}
	if (*tag) {
		bus_dma_tag_destroy(*tag);
		*tag = NULL;
	}
}

static int
ti_dma_alloc(struct ti_softc *sc)
{
	bus_addr_t lowaddr;
	int i, error;

	lowaddr = BUS_SPACE_MAXADDR;
	if (sc->ti_dac == 0)
		lowaddr = BUS_SPACE_MAXADDR_32BIT;

	error = bus_dma_tag_create(bus_get_dma_tag(sc->ti_dev), 1, 0, lowaddr,
	    BUS_SPACE_MAXADDR, NULL, NULL, BUS_SPACE_MAXSIZE_32BIT, 0,
	    BUS_SPACE_MAXSIZE_32BIT, 0, NULL, NULL,
	    &sc->ti_cdata.ti_parent_tag);
	if (error != 0) {
		device_printf(sc->ti_dev,
		    "could not allocate parent dma tag\n");
		return (ENOMEM);
	}

	error = ti_dma_ring_alloc(sc, TI_RING_ALIGN, sizeof(struct ti_gib),
	    &sc->ti_cdata.ti_gib_tag, (uint8_t **)&sc->ti_rdata.ti_info,
	    &sc->ti_cdata.ti_gib_map, &sc->ti_rdata.ti_info_paddr, "GIB");
	if (error)
		return (error);

	/* Producer/consumer status */
	error = ti_dma_ring_alloc(sc, TI_RING_ALIGN, sizeof(struct ti_status),
	    &sc->ti_cdata.ti_status_tag, (uint8_t **)&sc->ti_rdata.ti_status,
	    &sc->ti_cdata.ti_status_map, &sc->ti_rdata.ti_status_paddr,
	    "event ring");
	if (error)
		return (error);

	/* Event ring */
	error = ti_dma_ring_alloc(sc, TI_RING_ALIGN, TI_EVENT_RING_SZ,
	    &sc->ti_cdata.ti_event_ring_tag,
	    (uint8_t **)&sc->ti_rdata.ti_event_ring,
	    &sc->ti_cdata.ti_event_ring_map, &sc->ti_rdata.ti_event_ring_paddr,
	    "event ring");
	if (error)
		return (error);

	/* Command ring lives in shared memory so no need to create DMA area. */

	/* Standard RX ring */
	error = ti_dma_ring_alloc(sc, TI_RING_ALIGN, TI_STD_RX_RING_SZ,
	    &sc->ti_cdata.ti_rx_std_ring_tag,
	    (uint8_t **)&sc->ti_rdata.ti_rx_std_ring,
	    &sc->ti_cdata.ti_rx_std_ring_map,
	    &sc->ti_rdata.ti_rx_std_ring_paddr, "RX ring");
	if (error)
		return (error);

	/* Jumbo RX ring */
	error = ti_dma_ring_alloc(sc, TI_JUMBO_RING_ALIGN, TI_JUMBO_RX_RING_SZ,
	    &sc->ti_cdata.ti_rx_jumbo_ring_tag,
	    (uint8_t **)&sc->ti_rdata.ti_rx_jumbo_ring,
	    &sc->ti_cdata.ti_rx_jumbo_ring_map,
	    &sc->ti_rdata.ti_rx_jumbo_ring_paddr, "jumbo RX ring");
	if (error)
		return (error);

	/* RX return ring */
	error = ti_dma_ring_alloc(sc, TI_RING_ALIGN, TI_RX_RETURN_RING_SZ,
	    &sc->ti_cdata.ti_rx_return_ring_tag,
	    (uint8_t **)&sc->ti_rdata.ti_rx_return_ring,
	    &sc->ti_cdata.ti_rx_return_ring_map,
	    &sc->ti_rdata.ti_rx_return_ring_paddr, "RX return ring");
	if (error)
		return (error);

	/* Create DMA tag for standard RX mbufs. */
	error = bus_dma_tag_create(sc->ti_cdata.ti_parent_tag, 1, 0,
	    BUS_SPACE_MAXADDR, BUS_SPACE_MAXADDR, NULL, NULL, MCLBYTES, 1,
	    MCLBYTES, 0, NULL, NULL, &sc->ti_cdata.ti_rx_std_tag);
	if (error) {
		device_printf(sc->ti_dev, "could not allocate RX dma tag\n");
		return (error);
	}

	/* Create DMA tag for jumbo RX mbufs. */
#ifdef TI_SF_BUF_JUMBO
	/*
	 * The VM system will take care of providing aligned pages.  Alignment
	 * is set to 1 here so that busdma resources won't be wasted.
	 */
	error = bus_dma_tag_create(sc->ti_cdata.ti_parent_tag, 1, 0,
	    BUS_SPACE_MAXADDR, BUS_SPACE_MAXADDR, NULL, NULL, PAGE_SIZE * 4, 4,
	    PAGE_SIZE, 0, NULL, NULL, &sc->ti_cdata.ti_rx_jumbo_tag);
#else
	error = bus_dma_tag_create(sc->ti_cdata.ti_parent_tag, 1, 0,
	    BUS_SPACE_MAXADDR, BUS_SPACE_MAXADDR, NULL, NULL, MJUM9BYTES, 1,
	    MJUM9BYTES, 0, NULL, NULL, &sc->ti_cdata.ti_rx_jumbo_tag);
#endif
	if (error) {
		device_printf(sc->ti_dev,
		    "could not allocate jumbo RX dma tag\n");
		return (error);
	}

	/* Create DMA tag for TX mbufs. */
	error = bus_dma_tag_create(sc->ti_cdata.ti_parent_tag, 1,
	    0, BUS_SPACE_MAXADDR, BUS_SPACE_MAXADDR, NULL, NULL,
	    MCLBYTES * TI_MAXTXSEGS, TI_MAXTXSEGS, MCLBYTES, 0, NULL, NULL,
	    &sc->ti_cdata.ti_tx_tag);
	if (error) {
		device_printf(sc->ti_dev, "could not allocate TX dma tag\n");
		return (ENOMEM);
	}

	/* Create DMA maps for RX buffers. */
	for (i = 0; i < TI_STD_RX_RING_CNT; i++) {
		error = bus_dmamap_create(sc->ti_cdata.ti_rx_std_tag, 0,
		    &sc->ti_cdata.ti_rx_std_maps[i]);
		if (error) {
			device_printf(sc->ti_dev,
			    "could not create DMA map for RX\n");
			return (error);
		}
	}
	error = bus_dmamap_create(sc->ti_cdata.ti_rx_std_tag, 0,
	    &sc->ti_cdata.ti_rx_std_sparemap);
	if (error) {
		device_printf(sc->ti_dev,
		    "could not create spare DMA map for RX\n");
		return (error);
	}

	/* Create DMA maps for jumbo RX buffers. */
	for (i = 0; i < TI_JUMBO_RX_RING_CNT; i++) {
		error = bus_dmamap_create(sc->ti_cdata.ti_rx_jumbo_tag, 0,
		    &sc->ti_cdata.ti_rx_jumbo_maps[i]);
		if (error) {
			device_printf(sc->ti_dev,
			    "could not create DMA map for jumbo RX\n");
			return (error);
		}
	}
	error = bus_dmamap_create(sc->ti_cdata.ti_rx_jumbo_tag, 0,
	    &sc->ti_cdata.ti_rx_jumbo_sparemap);
	if (error) {
		device_printf(sc->ti_dev,
		    "could not create spare DMA map for jumbo RX\n");
		return (error);
	}

	/* Create DMA maps for TX buffers. */
	for (i = 0; i < TI_TX_RING_CNT; i++) {
		error = bus_dmamap_create(sc->ti_cdata.ti_tx_tag, 0,
		    &sc->ti_cdata.ti_txdesc[i].tx_dmamap);
		if (error) {
			device_printf(sc->ti_dev,
			    "could not create DMA map for TX\n");
			return (ENOMEM);
		}
	}

	/* Mini ring and TX ring is not available on Tigon 1. */
	if (sc->ti_hwrev == TI_HWREV_TIGON)
		return (0);

	/* TX ring */
	error = ti_dma_ring_alloc(sc, TI_RING_ALIGN, TI_TX_RING_SZ,
	    &sc->ti_cdata.ti_tx_ring_tag, (uint8_t **)&sc->ti_rdata.ti_tx_ring,
	    &sc->ti_cdata.ti_tx_ring_map, &sc->ti_rdata.ti_tx_ring_paddr,
	    "TX ring");
	if (error)
		return (error);

	/* Mini RX ring */
	error = ti_dma_ring_alloc(sc, TI_RING_ALIGN, TI_MINI_RX_RING_SZ,
	    &sc->ti_cdata.ti_rx_mini_ring_tag,
	    (uint8_t **)&sc->ti_rdata.ti_rx_mini_ring,
	    &sc->ti_cdata.ti_rx_mini_ring_map,
	    &sc->ti_rdata.ti_rx_mini_ring_paddr, "mini RX ring");
	if (error)
		return (error);

	/* Create DMA tag for mini RX mbufs. */
	error = bus_dma_tag_create(sc->ti_cdata.ti_parent_tag, 1, 0,
	    BUS_SPACE_MAXADDR, BUS_SPACE_MAXADDR, NULL, NULL, MHLEN, 1,
	    MHLEN, 0, NULL, NULL, &sc->ti_cdata.ti_rx_mini_tag);
	if (error) {
		device_printf(sc->ti_dev,
		    "could not allocate mini RX dma tag\n");
		return (error);
	}

	/* Create DMA maps for mini RX buffers. */
	for (i = 0; i < TI_MINI_RX_RING_CNT; i++) {
		error = bus_dmamap_create(sc->ti_cdata.ti_rx_mini_tag, 0,
		    &sc->ti_cdata.ti_rx_mini_maps[i]);
		if (error) {
			device_printf(sc->ti_dev,
			    "could not create DMA map for mini RX\n");
			return (error);
		}
	}
	error = bus_dmamap_create(sc->ti_cdata.ti_rx_mini_tag, 0,
	    &sc->ti_cdata.ti_rx_mini_sparemap);
	if (error) {
		device_printf(sc->ti_dev,
		    "could not create spare DMA map for mini RX\n");
		return (error);
	}

	return (0);
}

static void
ti_dma_free(struct ti_softc *sc)
{
	int i;

	/* Destroy DMA maps for RX buffers. */
	for (i = 0; i < TI_STD_RX_RING_CNT; i++) {
		if (sc->ti_cdata.ti_rx_std_maps[i]) {
			bus_dmamap_destroy(sc->ti_cdata.ti_rx_std_tag,
			    sc->ti_cdata.ti_rx_std_maps[i]);
			sc->ti_cdata.ti_rx_std_maps[i] = NULL;
		}
	}
	if (sc->ti_cdata.ti_rx_std_sparemap) {
		bus_dmamap_destroy(sc->ti_cdata.ti_rx_std_tag,
		    sc->ti_cdata.ti_rx_std_sparemap);
		sc->ti_cdata.ti_rx_std_sparemap = NULL;
	}
	if (sc->ti_cdata.ti_rx_std_tag) {
		bus_dma_tag_destroy(sc->ti_cdata.ti_rx_std_tag);
		sc->ti_cdata.ti_rx_std_tag = NULL;
	}

	/* Destroy DMA maps for jumbo RX buffers. */
	for (i = 0; i < TI_JUMBO_RX_RING_CNT; i++) {
		if (sc->ti_cdata.ti_rx_jumbo_maps[i]) {
			bus_dmamap_destroy(sc->ti_cdata.ti_rx_jumbo_tag,
			    sc->ti_cdata.ti_rx_jumbo_maps[i]);
			sc->ti_cdata.ti_rx_jumbo_maps[i] = NULL;
		}
	}
	if (sc->ti_cdata.ti_rx_jumbo_sparemap) {
		bus_dmamap_destroy(sc->ti_cdata.ti_rx_jumbo_tag,
		    sc->ti_cdata.ti_rx_jumbo_sparemap);
		sc->ti_cdata.ti_rx_jumbo_sparemap = NULL;
	}
	if (sc->ti_cdata.ti_rx_jumbo_tag) {
		bus_dma_tag_destroy(sc->ti_cdata.ti_rx_jumbo_tag);
		sc->ti_cdata.ti_rx_jumbo_tag = NULL;
	}

	/* Destroy DMA maps for mini RX buffers. */
	for (i = 0; i < TI_MINI_RX_RING_CNT; i++) {
		if (sc->ti_cdata.ti_rx_mini_maps[i]) {
			bus_dmamap_destroy(sc->ti_cdata.ti_rx_mini_tag,
			    sc->ti_cdata.ti_rx_mini_maps[i]);
			sc->ti_cdata.ti_rx_mini_maps[i] = NULL;
		}
	}
	if (sc->ti_cdata.ti_rx_mini_sparemap) {
		bus_dmamap_destroy(sc->ti_cdata.ti_rx_mini_tag,
		    sc->ti_cdata.ti_rx_mini_sparemap);
		sc->ti_cdata.ti_rx_mini_sparemap = NULL;
	}
	if (sc->ti_cdata.ti_rx_mini_tag) {
		bus_dma_tag_destroy(sc->ti_cdata.ti_rx_mini_tag);
		sc->ti_cdata.ti_rx_mini_tag = NULL;
	}

	/* Destroy DMA maps for TX buffers. */
	for (i = 0; i < TI_TX_RING_CNT; i++) {
		if (sc->ti_cdata.ti_txdesc[i].tx_dmamap) {
			bus_dmamap_destroy(sc->ti_cdata.ti_tx_tag,
			    sc->ti_cdata.ti_txdesc[i].tx_dmamap);
			sc->ti_cdata.ti_txdesc[i].tx_dmamap = NULL;
		}
	}
	if (sc->ti_cdata.ti_tx_tag) {
		bus_dma_tag_destroy(sc->ti_cdata.ti_tx_tag);
		sc->ti_cdata.ti_tx_tag = NULL;
	}

	/* Destroy standard RX ring. */
	ti_dma_ring_free(sc, &sc->ti_cdata.ti_rx_std_ring_tag,
	    (void *)&sc->ti_rdata.ti_rx_std_ring,
	    sc->ti_cdata.ti_rx_std_ring_map,
	    &sc->ti_rdata.ti_rx_std_ring_paddr);
	/* Destroy jumbo RX ring. */
	ti_dma_ring_free(sc, &sc->ti_cdata.ti_rx_jumbo_ring_tag,
	    (void *)&sc->ti_rdata.ti_rx_jumbo_ring,
	    sc->ti_cdata.ti_rx_jumbo_ring_map,
	    &sc->ti_rdata.ti_rx_jumbo_ring_paddr);
	/* Destroy mini RX ring. */
	ti_dma_ring_free(sc, &sc->ti_cdata.ti_rx_mini_ring_tag,
	    (void *)&sc->ti_rdata.ti_rx_mini_ring,
	    sc->ti_cdata.ti_rx_mini_ring_map,
	    &sc->ti_rdata.ti_rx_mini_ring_paddr);
	/* Destroy RX return ring. */
	ti_dma_ring_free(sc, &sc->ti_cdata.ti_rx_return_ring_tag,
	    (void *)&sc->ti_rdata.ti_rx_return_ring,
	    sc->ti_cdata.ti_rx_return_ring_map,
	    &sc->ti_rdata.ti_rx_return_ring_paddr);
	/* Destroy TX ring. */
	ti_dma_ring_free(sc, &sc->ti_cdata.ti_tx_ring_tag,
	    (void *)&sc->ti_rdata.ti_tx_ring, sc->ti_cdata.ti_tx_ring_map,
	    &sc->ti_rdata.ti_tx_ring_paddr);
	/* Destroy status block. */
	ti_dma_ring_free(sc, &sc->ti_cdata.ti_status_tag,
	    (void *)&sc->ti_rdata.ti_status, sc->ti_cdata.ti_status_map,
	    &sc->ti_rdata.ti_status_paddr);
	/* Destroy event ring. */
	ti_dma_ring_free(sc, &sc->ti_cdata.ti_event_ring_tag,
	    (void *)&sc->ti_rdata.ti_event_ring,
	    sc->ti_cdata.ti_event_ring_map, &sc->ti_rdata.ti_event_ring_paddr);
	/* Destroy GIB */
	ti_dma_ring_free(sc, &sc->ti_cdata.ti_gib_tag,
	    (void *)&sc->ti_rdata.ti_info, sc->ti_cdata.ti_gib_map,
	    &sc->ti_rdata.ti_info_paddr);

	/* Destroy the parent tag. */
	if (sc->ti_cdata.ti_parent_tag) {
		bus_dma_tag_destroy(sc->ti_cdata.ti_parent_tag);
		sc->ti_cdata.ti_parent_tag = NULL;
	}
}

/*
 * Intialize a standard receive ring descriptor.
 */
static int
ti_newbuf_std(struct ti_softc *sc, int i)
{
	bus_dmamap_t map;
	bus_dma_segment_t segs[1];
	struct mbuf *m;
	struct ti_rx_desc *r;
	int error, nsegs;

	m = m_getcl(M_NOWAIT, MT_DATA, M_PKTHDR);
	if (m == NULL)
		return (ENOBUFS);
	m->m_len = m->m_pkthdr.len = MCLBYTES;
	m_adj(m, ETHER_ALIGN);

	error = bus_dmamap_load_mbuf_sg(sc->ti_cdata.ti_rx_std_tag,
	    sc->ti_cdata.ti_rx_std_sparemap, m, segs, &nsegs, 0);
	if (error != 0) {
		m_freem(m);
		return (error);
        }
	KASSERT(nsegs == 1, ("%s: %d segments returned!", __func__, nsegs));

	if (sc->ti_cdata.ti_rx_std_chain[i] != NULL) {
		bus_dmamap_sync(sc->ti_cdata.ti_rx_std_tag,
		    sc->ti_cdata.ti_rx_std_maps[i], BUS_DMASYNC_POSTREAD);
		bus_dmamap_unload(sc->ti_cdata.ti_rx_std_tag,
		    sc->ti_cdata.ti_rx_std_maps[i]);
	}

	map = sc->ti_cdata.ti_rx_std_maps[i];
	sc->ti_cdata.ti_rx_std_maps[i] = sc->ti_cdata.ti_rx_std_sparemap;
	sc->ti_cdata.ti_rx_std_sparemap = map;
	sc->ti_cdata.ti_rx_std_chain[i] = m;

	r = &sc->ti_rdata.ti_rx_std_ring[i];
	ti_hostaddr64(&r->ti_addr, segs[0].ds_addr);
	r->ti_len = segs[0].ds_len;
	r->ti_type = TI_BDTYPE_RECV_BD;
	r->ti_flags = 0;
	r->ti_vlan_tag = 0;
	r->ti_tcp_udp_cksum = 0;
	if (sc->ti_ifp->if_capenable & IFCAP_RXCSUM)
		r->ti_flags |= TI_BDFLAG_TCP_UDP_CKSUM | TI_BDFLAG_IP_CKSUM;
	r->ti_idx = i;

	bus_dmamap_sync(sc->ti_cdata.ti_rx_std_tag,
	    sc->ti_cdata.ti_rx_std_maps[i], BUS_DMASYNC_PREREAD);
	return (0);
}

/*
 * Intialize a mini receive ring descriptor. This only applies to
 * the Tigon 2.
 */
static int
ti_newbuf_mini(struct ti_softc *sc, int i)
{
	bus_dmamap_t map;
	bus_dma_segment_t segs[1];
	struct mbuf *m;
	struct ti_rx_desc *r;
	int error, nsegs;

	MGETHDR(m, M_NOWAIT, MT_DATA);
	if (m == NULL)
		return (ENOBUFS);
	m->m_len = m->m_pkthdr.len = MHLEN;
	m_adj(m, ETHER_ALIGN);

	error = bus_dmamap_load_mbuf_sg(sc->ti_cdata.ti_rx_mini_tag,
	    sc->ti_cdata.ti_rx_mini_sparemap, m, segs, &nsegs, 0);
	if (error != 0) {
		m_freem(m);
		return (error);
        }
	KASSERT(nsegs == 1, ("%s: %d segments returned!", __func__, nsegs));

	if (sc->ti_cdata.ti_rx_mini_chain[i] != NULL) {
		bus_dmamap_sync(sc->ti_cdata.ti_rx_mini_tag,
		    sc->ti_cdata.ti_rx_mini_maps[i], BUS_DMASYNC_POSTREAD);
		bus_dmamap_unload(sc->ti_cdata.ti_rx_mini_tag,
		    sc->ti_cdata.ti_rx_mini_maps[i]);
	}

	map = sc->ti_cdata.ti_rx_mini_maps[i];
	sc->ti_cdata.ti_rx_mini_maps[i] = sc->ti_cdata.ti_rx_mini_sparemap;
	sc->ti_cdata.ti_rx_mini_sparemap = map;
	sc->ti_cdata.ti_rx_mini_chain[i] = m;

	r = &sc->ti_rdata.ti_rx_mini_ring[i];
	ti_hostaddr64(&r->ti_addr, segs[0].ds_addr);
	r->ti_len = segs[0].ds_len;
	r->ti_type = TI_BDTYPE_RECV_BD;
	r->ti_flags = TI_BDFLAG_MINI_RING;
	r->ti_vlan_tag = 0;
	r->ti_tcp_udp_cksum = 0;
	if (sc->ti_ifp->if_capenable & IFCAP_RXCSUM)
		r->ti_flags |= TI_BDFLAG_TCP_UDP_CKSUM | TI_BDFLAG_IP_CKSUM;
	r->ti_idx = i;

	bus_dmamap_sync(sc->ti_cdata.ti_rx_mini_tag,
	    sc->ti_cdata.ti_rx_mini_maps[i], BUS_DMASYNC_PREREAD);
	return (0);
}

#ifndef TI_SF_BUF_JUMBO

/*
 * Initialize a jumbo receive ring descriptor. This allocates
 * a jumbo buffer from the pool managed internally by the driver.
 */
static int
ti_newbuf_jumbo(struct ti_softc *sc, int i, struct mbuf *dummy)
{
	bus_dmamap_t map;
	bus_dma_segment_t segs[1];
	struct mbuf *m;
	struct ti_rx_desc *r;
	int error, nsegs;

	(void)dummy;

	m = m_getjcl(M_NOWAIT, MT_DATA, M_PKTHDR, MJUM9BYTES);
	if (m == NULL)
		return (ENOBUFS);
	m->m_len = m->m_pkthdr.len = MJUM9BYTES;
	m_adj(m, ETHER_ALIGN);

	error = bus_dmamap_load_mbuf_sg(sc->ti_cdata.ti_rx_jumbo_tag,
	    sc->ti_cdata.ti_rx_jumbo_sparemap, m, segs, &nsegs, 0);
	if (error != 0) {
		m_freem(m);
		return (error);
        }
	KASSERT(nsegs == 1, ("%s: %d segments returned!", __func__, nsegs));

	if (sc->ti_cdata.ti_rx_jumbo_chain[i] != NULL) {
		bus_dmamap_sync(sc->ti_cdata.ti_rx_jumbo_tag,
		    sc->ti_cdata.ti_rx_jumbo_maps[i], BUS_DMASYNC_POSTREAD);
		bus_dmamap_unload(sc->ti_cdata.ti_rx_jumbo_tag,
		    sc->ti_cdata.ti_rx_jumbo_maps[i]);
	}

	map = sc->ti_cdata.ti_rx_jumbo_maps[i];
	sc->ti_cdata.ti_rx_jumbo_maps[i] = sc->ti_cdata.ti_rx_jumbo_sparemap;
	sc->ti_cdata.ti_rx_jumbo_sparemap = map;
	sc->ti_cdata.ti_rx_jumbo_chain[i] = m;

	r = &sc->ti_rdata.ti_rx_jumbo_ring[i];
	ti_hostaddr64(&r->ti_addr, segs[0].ds_addr);
	r->ti_len = segs[0].ds_len;
	r->ti_type = TI_BDTYPE_RECV_JUMBO_BD;
	r->ti_flags = TI_BDFLAG_JUMBO_RING;
	r->ti_vlan_tag = 0;
	r->ti_tcp_udp_cksum = 0;
	if (sc->ti_ifp->if_capenable & IFCAP_RXCSUM)
		r->ti_flags |= TI_BDFLAG_TCP_UDP_CKSUM | TI_BDFLAG_IP_CKSUM;
	r->ti_idx = i;

	bus_dmamap_sync(sc->ti_cdata.ti_rx_jumbo_tag,
	    sc->ti_cdata.ti_rx_jumbo_maps[i], BUS_DMASYNC_PREREAD);
	return (0);
}

#else

#if (PAGE_SIZE == 4096)
#define NPAYLOAD 2
#else
#define NPAYLOAD 1
#endif

#define TCP_HDR_LEN (52 + sizeof(struct ether_header))
#define UDP_HDR_LEN (28 + sizeof(struct ether_header))
#define NFS_HDR_LEN (UDP_HDR_LEN)
static int HDR_LEN = TCP_HDR_LEN;

/*
 * Initialize a jumbo receive ring descriptor. This allocates
 * a jumbo buffer from the pool managed internally by the driver.
 */
static int
ti_newbuf_jumbo(struct ti_softc *sc, int idx, struct mbuf *m_old)
{
	bus_dmamap_t map;
	struct mbuf *cur, *m_new = NULL;
	struct mbuf *m[3] = {NULL, NULL, NULL};
	struct ti_rx_desc_ext *r;
	vm_page_t frame;
	/* 1 extra buf to make nobufs easy*/
	struct sf_buf *sf[3] = {NULL, NULL, NULL};
	int i;
	bus_dma_segment_t segs[4];
	int nsegs;

	if (m_old != NULL) {
		m_new = m_old;
		cur = m_old->m_next;
		for (i = 0; i <= NPAYLOAD; i++){
			m[i] = cur;
			cur = cur->m_next;
		}
	} else {
		/* Allocate the mbufs. */
		MGETHDR(m_new, M_NOWAIT, MT_DATA);
		if (m_new == NULL) {
			device_printf(sc->ti_dev, "mbuf allocation failed "
			    "-- packet dropped!\n");
			goto nobufs;
		}
		MGET(m[NPAYLOAD], M_NOWAIT, MT_DATA);
		if (m[NPAYLOAD] == NULL) {
			device_printf(sc->ti_dev, "cluster mbuf allocation "
			    "failed -- packet dropped!\n");
			goto nobufs;
		}
		if (!(MCLGET(m[NPAYLOAD], M_NOWAIT))) {
			device_printf(sc->ti_dev, "mbuf allocation failed "
			    "-- packet dropped!\n");
			goto nobufs;
		}
		m[NPAYLOAD]->m_len = MCLBYTES;

		for (i = 0; i < NPAYLOAD; i++){
			MGET(m[i], M_NOWAIT, MT_DATA);
			if (m[i] == NULL) {
				device_printf(sc->ti_dev, "mbuf allocation "
				    "failed -- packet dropped!\n");
				goto nobufs;
			}
			frame = vm_page_alloc(NULL, 0,
			    VM_ALLOC_INTERRUPT | VM_ALLOC_NOOBJ |
			    VM_ALLOC_WIRED);
			if (frame == NULL) {
				device_printf(sc->ti_dev, "buffer allocation "
				    "failed -- packet dropped!\n");
				printf("      index %d page %d\n", idx, i);
				goto nobufs;
			}
			sf[i] = sf_buf_alloc(frame, SFB_NOWAIT);
			if (sf[i] == NULL) {
				vm_page_unwire(frame, PQ_NONE);
				vm_page_free(frame);
				device_printf(sc->ti_dev, "buffer allocation "
				    "failed -- packet dropped!\n");
				printf("      index %d page %d\n", idx, i);
				goto nobufs;
			}
		}
		for (i = 0; i < NPAYLOAD; i++){
		/* Attach the buffer to the mbuf. */
			m[i]->m_data = (void *)sf_buf_kva(sf[i]);
			m[i]->m_len = PAGE_SIZE;
			MEXTADD(m[i], sf_buf_kva(sf[i]), PAGE_SIZE,
			    sf_mext_free, (void*)sf_buf_kva(sf[i]), sf[i],
			    0, EXT_DISPOSABLE);
			m[i]->m_next = m[i+1];
		}
		/* link the buffers to the header */
		m_new->m_next = m[0];
		m_new->m_data += ETHER_ALIGN;
		if (sc->ti_hdrsplit)
			m_new->m_len = MHLEN - ETHER_ALIGN;
		else
			m_new->m_len = HDR_LEN;
		m_new->m_pkthdr.len = NPAYLOAD * PAGE_SIZE + m_new->m_len;
	}

	/* Set up the descriptor. */
	r = &sc->ti_rdata.ti_rx_jumbo_ring[idx];
	sc->ti_cdata.ti_rx_jumbo_chain[idx] = m_new;
	map = sc->ti_cdata.ti_rx_jumbo_maps[i];
	if (bus_dmamap_load_mbuf_sg(sc->ti_cdata.ti_rx_jumbo_tag, map, m_new,
	    segs, &nsegs, 0))
		return (ENOBUFS);
	if ((nsegs < 1) || (nsegs > 4))
		return (ENOBUFS);
	ti_hostaddr64(&r->ti_addr0, segs[0].ds_addr);
	r->ti_len0 = m_new->m_len;

	ti_hostaddr64(&r->ti_addr1, segs[1].ds_addr);
	r->ti_len1 = PAGE_SIZE;

	ti_hostaddr64(&r->ti_addr2, segs[2].ds_addr);
	r->ti_len2 = m[1]->m_ext.ext_size; /* could be PAGE_SIZE or MCLBYTES */

	if (PAGE_SIZE == 4096) {
		ti_hostaddr64(&r->ti_addr3, segs[3].ds_addr);
		r->ti_len3 = MCLBYTES;
	} else {
		r->ti_len3 = 0;
	}
	r->ti_type = TI_BDTYPE_RECV_JUMBO_BD;

	r->ti_flags = TI_BDFLAG_JUMBO_RING|TI_RCB_FLAG_USE_EXT_RX_BD;

	if (sc->ti_ifp->if_capenable & IFCAP_RXCSUM)
		r->ti_flags |= TI_BDFLAG_TCP_UDP_CKSUM|TI_BDFLAG_IP_CKSUM;

	r->ti_idx = idx;

	bus_dmamap_sync(sc->ti_cdata.ti_rx_jumbo_tag, map, BUS_DMASYNC_PREREAD);
	return (0);

nobufs:

	/*
	 * Warning! :
	 * This can only be called before the mbufs are strung together.
	 * If the mbufs are strung together, m_freem() will free the chain,
	 * so that the later mbufs will be freed multiple times.
	 */
	if (m_new)
		m_freem(m_new);

	for (i = 0; i < 3; i++) {
		if (m[i])
			m_freem(m[i]);
		if (sf[i])
			sf_mext_free((void *)sf_buf_kva(sf[i]), sf[i]);
	}
	return (ENOBUFS);
}
#endif

/*
 * The standard receive ring has 512 entries in it. At 2K per mbuf cluster,
 * that's 1MB or memory, which is a lot. For now, we fill only the first
 * 256 ring entries and hope that our CPU is fast enough to keep up with
 * the NIC.
 */
static int
ti_init_rx_ring_std(struct ti_softc *sc)
{
	int i;
	struct ti_cmd_desc cmd;

	for (i = 0; i < TI_STD_RX_RING_CNT; i++) {
		if (ti_newbuf_std(sc, i) != 0)
			return (ENOBUFS);
	}

	sc->ti_std = TI_STD_RX_RING_CNT - 1;
	TI_UPDATE_STDPROD(sc, TI_STD_RX_RING_CNT - 1);

	return (0);
}

static void
ti_free_rx_ring_std(struct ti_softc *sc)
{
	bus_dmamap_t map;
	int i;

	for (i = 0; i < TI_STD_RX_RING_CNT; i++) {
		if (sc->ti_cdata.ti_rx_std_chain[i] != NULL) {
			map = sc->ti_cdata.ti_rx_std_maps[i];
			bus_dmamap_sync(sc->ti_cdata.ti_rx_std_tag, map,
			    BUS_DMASYNC_POSTREAD);
			bus_dmamap_unload(sc->ti_cdata.ti_rx_std_tag, map);
			m_freem(sc->ti_cdata.ti_rx_std_chain[i]);
			sc->ti_cdata.ti_rx_std_chain[i] = NULL;
		}
	}
	bzero(sc->ti_rdata.ti_rx_std_ring, TI_STD_RX_RING_SZ);
	bus_dmamap_sync(sc->ti_cdata.ti_rx_std_ring_tag,
	    sc->ti_cdata.ti_rx_std_ring_map, BUS_DMASYNC_PREWRITE);
}

static int
ti_init_rx_ring_jumbo(struct ti_softc *sc)
{
	struct ti_cmd_desc cmd;
	int i;

	for (i = 0; i < TI_JUMBO_RX_RING_CNT; i++) {
		if (ti_newbuf_jumbo(sc, i, NULL) != 0)
			return (ENOBUFS);
	}

	sc->ti_jumbo = TI_JUMBO_RX_RING_CNT - 1;
	TI_UPDATE_JUMBOPROD(sc, TI_JUMBO_RX_RING_CNT - 1);

	return (0);
}

static void
ti_free_rx_ring_jumbo(struct ti_softc *sc)
{
	bus_dmamap_t map;
	int i;

	for (i = 0; i < TI_JUMBO_RX_RING_CNT; i++) {
		if (sc->ti_cdata.ti_rx_jumbo_chain[i] != NULL) {
			map = sc->ti_cdata.ti_rx_jumbo_maps[i];
			bus_dmamap_sync(sc->ti_cdata.ti_rx_jumbo_tag, map,
			    BUS_DMASYNC_POSTREAD);
			bus_dmamap_unload(sc->ti_cdata.ti_rx_jumbo_tag, map);
			m_freem(sc->ti_cdata.ti_rx_jumbo_chain[i]);
			sc->ti_cdata.ti_rx_jumbo_chain[i] = NULL;
		}
	}
	bzero(sc->ti_rdata.ti_rx_jumbo_ring, TI_JUMBO_RX_RING_SZ);
	bus_dmamap_sync(sc->ti_cdata.ti_rx_jumbo_ring_tag,
	    sc->ti_cdata.ti_rx_jumbo_ring_map, BUS_DMASYNC_PREWRITE);
}

static int
ti_init_rx_ring_mini(struct ti_softc *sc)
{
	int i;

	for (i = 0; i < TI_MINI_RX_RING_CNT; i++) {
		if (ti_newbuf_mini(sc, i) != 0)
			return (ENOBUFS);
	}

	sc->ti_mini = TI_MINI_RX_RING_CNT - 1;
	TI_UPDATE_MINIPROD(sc, TI_MINI_RX_RING_CNT - 1);

	return (0);
}

static void
ti_free_rx_ring_mini(struct ti_softc *sc)
{
	bus_dmamap_t map;
	int i;

	if (sc->ti_rdata.ti_rx_mini_ring == NULL)
		return;

	for (i = 0; i < TI_MINI_RX_RING_CNT; i++) {
		if (sc->ti_cdata.ti_rx_mini_chain[i] != NULL) {
			map = sc->ti_cdata.ti_rx_mini_maps[i];
			bus_dmamap_sync(sc->ti_cdata.ti_rx_mini_tag, map,
			    BUS_DMASYNC_POSTREAD);
			bus_dmamap_unload(sc->ti_cdata.ti_rx_mini_tag, map);
			m_freem(sc->ti_cdata.ti_rx_mini_chain[i]);
			sc->ti_cdata.ti_rx_mini_chain[i] = NULL;
		}
	}
	bzero(sc->ti_rdata.ti_rx_mini_ring, TI_MINI_RX_RING_SZ);
	bus_dmamap_sync(sc->ti_cdata.ti_rx_mini_ring_tag,
	    sc->ti_cdata.ti_rx_mini_ring_map, BUS_DMASYNC_PREWRITE);
}

static void
ti_free_tx_ring(struct ti_softc *sc)
{
	struct ti_txdesc *txd;
	int i;

	if (sc->ti_rdata.ti_tx_ring == NULL)
		return;

	for (i = 0; i < TI_TX_RING_CNT; i++) {
		txd = &sc->ti_cdata.ti_txdesc[i];
		if (txd->tx_m != NULL) {
			bus_dmamap_sync(sc->ti_cdata.ti_tx_tag, txd->tx_dmamap,
			    BUS_DMASYNC_POSTWRITE);
			bus_dmamap_unload(sc->ti_cdata.ti_tx_tag,
			    txd->tx_dmamap);
			m_freem(txd->tx_m);
			txd->tx_m = NULL;
		}
	}
	bzero(sc->ti_rdata.ti_tx_ring, TI_TX_RING_SZ);
	bus_dmamap_sync(sc->ti_cdata.ti_tx_ring_tag,
	    sc->ti_cdata.ti_tx_ring_map, BUS_DMASYNC_PREWRITE);
}

static int
ti_init_tx_ring(struct ti_softc *sc)
{
	struct ti_txdesc *txd;
	int i;

	STAILQ_INIT(&sc->ti_cdata.ti_txfreeq);
	STAILQ_INIT(&sc->ti_cdata.ti_txbusyq);
	for (i = 0; i < TI_TX_RING_CNT; i++) {
		txd = &sc->ti_cdata.ti_txdesc[i];
		STAILQ_INSERT_TAIL(&sc->ti_cdata.ti_txfreeq, txd, tx_q);
	}
	sc->ti_txcnt = 0;
	sc->ti_tx_saved_considx = 0;
	sc->ti_tx_saved_prodidx = 0;
	CSR_WRITE_4(sc, TI_MB_SENDPROD_IDX, 0);
	return (0);
}

/*
 * The Tigon 2 firmware has a new way to add/delete multicast addresses,
 * but we have to support the old way too so that Tigon 1 cards will
 * work.
 */
static void
ti_add_mcast(struct ti_softc *sc, struct ether_addr *addr)
{
	struct ti_cmd_desc cmd;
	uint16_t *m;
	uint32_t ext[2] = {0, 0};

	m = (uint16_t *)&addr->octet[0];

	switch (sc->ti_hwrev) {
	case TI_HWREV_TIGON:
		CSR_WRITE_4(sc, TI_GCR_MAR0, htons(m[0]));
		CSR_WRITE_4(sc, TI_GCR_MAR1, (htons(m[1]) << 16) | htons(m[2]));
		TI_DO_CMD(TI_CMD_ADD_MCAST_ADDR, 0, 0);
		break;
	case TI_HWREV_TIGON_II:
		ext[0] = htons(m[0]);
		ext[1] = (htons(m[1]) << 16) | htons(m[2]);
		TI_DO_CMD_EXT(TI_CMD_EXT_ADD_MCAST, 0, 0, (caddr_t)&ext, 2);
		break;
	default:
		device_printf(sc->ti_dev, "unknown hwrev\n");
		break;
	}
}

static void
ti_del_mcast(struct ti_softc *sc, struct ether_addr *addr)
{
	struct ti_cmd_desc cmd;
	uint16_t *m;
	uint32_t ext[2] = {0, 0};

	m = (uint16_t *)&addr->octet[0];

	switch (sc->ti_hwrev) {
	case TI_HWREV_TIGON:
		CSR_WRITE_4(sc, TI_GCR_MAR0, htons(m[0]));
		CSR_WRITE_4(sc, TI_GCR_MAR1, (htons(m[1]) << 16) | htons(m[2]));
		TI_DO_CMD(TI_CMD_DEL_MCAST_ADDR, 0, 0);
		break;
	case TI_HWREV_TIGON_II:
		ext[0] = htons(m[0]);
		ext[1] = (htons(m[1]) << 16) | htons(m[2]);
		TI_DO_CMD_EXT(TI_CMD_EXT_DEL_MCAST, 0, 0, (caddr_t)&ext, 2);
		break;
	default:
		device_printf(sc->ti_dev, "unknown hwrev\n");
		break;
	}
}

/*
 * Configure the Tigon's multicast address filter.
 *
 * The actual multicast table management is a bit of a pain, thanks to
 * slight brain damage on the part of both Alteon and us. With our
 * multicast code, we are only alerted when the multicast address table
 * changes and at that point we only have the current list of addresses:
 * we only know the current state, not the previous state, so we don't
 * actually know what addresses were removed or added. The firmware has
 * state, but we can't get our grubby mits on it, and there is no 'delete
 * all multicast addresses' command. Hence, we have to maintain our own
 * state so we know what addresses have been programmed into the NIC at
 * any given time.
 */
static void
ti_setmulti(struct ti_softc *sc)
{
	struct ifnet *ifp;
	struct ifmultiaddr *ifma;
	struct ti_cmd_desc cmd;
	struct ti_mc_entry *mc;
	uint32_t intrs;

	TI_LOCK_ASSERT(sc);

	ifp = sc->ti_ifp;

	if (ifp->if_flags & IFF_ALLMULTI) {
		TI_DO_CMD(TI_CMD_SET_ALLMULTI, TI_CMD_CODE_ALLMULTI_ENB, 0);
		return;
	} else {
		TI_DO_CMD(TI_CMD_SET_ALLMULTI, TI_CMD_CODE_ALLMULTI_DIS, 0);
	}

	/* Disable interrupts. */
	intrs = CSR_READ_4(sc, TI_MB_HOSTINTR);
	CSR_WRITE_4(sc, TI_MB_HOSTINTR, 1);

	/* First, zot all the existing filters. */
	while (SLIST_FIRST(&sc->ti_mc_listhead) != NULL) {
		mc = SLIST_FIRST(&sc->ti_mc_listhead);
		ti_del_mcast(sc, &mc->mc_addr);
		SLIST_REMOVE_HEAD(&sc->ti_mc_listhead, mc_entries);
		free(mc, M_DEVBUF);
	}

	/* Now program new ones. */
	if_maddr_rlock(ifp);
	CK_STAILQ_FOREACH(ifma, &ifp->if_multiaddrs, ifma_link) {
		if (ifma->ifma_addr->sa_family != AF_LINK)
			continue;
		mc = malloc(sizeof(struct ti_mc_entry), M_DEVBUF, M_NOWAIT);
		if (mc == NULL) {
			device_printf(sc->ti_dev,
			    "no memory for mcast filter entry\n");
			continue;
		}
		bcopy(LLADDR((struct sockaddr_dl *)ifma->ifma_addr),
		    (char *)&mc->mc_addr, ETHER_ADDR_LEN);
		SLIST_INSERT_HEAD(&sc->ti_mc_listhead, mc, mc_entries);
		ti_add_mcast(sc, &mc->mc_addr);
	}
	if_maddr_runlock(ifp);

	/* Re-enable interrupts. */
	CSR_WRITE_4(sc, TI_MB_HOSTINTR, intrs);
}

/*
 * Check to see if the BIOS has configured us for a 64 bit slot when
 * we aren't actually in one. If we detect this condition, we can work
 * around it on the Tigon 2 by setting a bit in the PCI state register,
 * but for the Tigon 1 we must give up and abort the interface attach.
 */
static int
ti_64bitslot_war(struct ti_softc *sc)
{

	if (!(CSR_READ_4(sc, TI_PCI_STATE) & TI_PCISTATE_32BIT_BUS)) {
		CSR_WRITE_4(sc, 0x600, 0);
		CSR_WRITE_4(sc, 0x604, 0);
		CSR_WRITE_4(sc, 0x600, 0x5555AAAA);
		if (CSR_READ_4(sc, 0x604) == 0x5555AAAA) {
			if (sc->ti_hwrev == TI_HWREV_TIGON)
				return (EINVAL);
			else {
				TI_SETBIT(sc, TI_PCI_STATE,
				    TI_PCISTATE_32BIT_BUS);
				return (0);
			}
		}
	}

	return (0);
}

/*
 * Do endian, PCI and DMA initialization. Also check the on-board ROM
 * self-test results.
 */
static int
ti_chipinit(struct ti_softc *sc)
{
	uint32_t cacheline;
	uint32_t pci_writemax = 0;
	uint32_t hdrsplit;

	/* Initialize link to down state. */
	sc->ti_linkstat = TI_EV_CODE_LINK_DOWN;

	/* Set endianness before we access any non-PCI registers. */
#if 0 && BYTE_ORDER == BIG_ENDIAN
	CSR_WRITE_4(sc, TI_MISC_HOST_CTL,
	    TI_MHC_BIGENDIAN_INIT | (TI_MHC_BIGENDIAN_INIT << 24));
#else
	CSR_WRITE_4(sc, TI_MISC_HOST_CTL,
	    TI_MHC_LITTLEENDIAN_INIT | (TI_MHC_LITTLEENDIAN_INIT << 24));
#endif

	/* Check the ROM failed bit to see if self-tests passed. */
	if (CSR_READ_4(sc, TI_CPU_STATE) & TI_CPUSTATE_ROMFAIL) {
		device_printf(sc->ti_dev, "board self-diagnostics failed!\n");
		return (ENODEV);
	}

	/* Halt the CPU. */
	TI_SETBIT(sc, TI_CPU_STATE, TI_CPUSTATE_HALT);

	/* Figure out the hardware revision. */
	switch (CSR_READ_4(sc, TI_MISC_HOST_CTL) & TI_MHC_CHIP_REV_MASK) {
	case TI_REV_TIGON_I:
		sc->ti_hwrev = TI_HWREV_TIGON;
		break;
	case TI_REV_TIGON_II:
		sc->ti_hwrev = TI_HWREV_TIGON_II;
		break;
	default:
		device_printf(sc->ti_dev, "unsupported chip revision\n");
		return (ENODEV);
	}

	/* Do special setup for Tigon 2. */
	if (sc->ti_hwrev == TI_HWREV_TIGON_II) {
		TI_SETBIT(sc, TI_CPU_CTL_B, TI_CPUSTATE_HALT);
		TI_SETBIT(sc, TI_MISC_LOCAL_CTL, TI_MLC_SRAM_BANK_512K);
		TI_SETBIT(sc, TI_MISC_CONF, TI_MCR_SRAM_SYNCHRONOUS);
	}

	/*
	 * We don't have firmware source for the Tigon 1, so Tigon 1 boards
	 * can't do header splitting.
	 */
#ifdef TI_JUMBO_HDRSPLIT
	if (sc->ti_hwrev != TI_HWREV_TIGON)
		sc->ti_hdrsplit = 1;
	else
		device_printf(sc->ti_dev,
		    "can't do header splitting on a Tigon I board\n");
#endif /* TI_JUMBO_HDRSPLIT */

	/* Set up the PCI state register. */
	CSR_WRITE_4(sc, TI_PCI_STATE, TI_PCI_READ_CMD|TI_PCI_WRITE_CMD);
	if (sc->ti_hwrev == TI_HWREV_TIGON_II) {
		TI_SETBIT(sc, TI_PCI_STATE, TI_PCISTATE_USE_MEM_RD_MULT);
	}

	/* Clear the read/write max DMA parameters. */
	TI_CLRBIT(sc, TI_PCI_STATE, (TI_PCISTATE_WRITE_MAXDMA|
	    TI_PCISTATE_READ_MAXDMA));

	/* Get cache line size. */
	cacheline = CSR_READ_4(sc, TI_PCI_BIST) & 0xFF;

	/*
	 * If the system has set enabled the PCI memory write
	 * and invalidate command in the command register, set
	 * the write max parameter accordingly. This is necessary
	 * to use MWI with the Tigon 2.
	 */
	if (CSR_READ_4(sc, TI_PCI_CMDSTAT) & PCIM_CMD_MWIEN) {
		switch (cacheline) {
		case 1:
		case 4:
		case 8:
		case 16:
		case 32:
		case 64:
			break;
		default:
		/* Disable PCI memory write and invalidate. */
			if (bootverbose)
				device_printf(sc->ti_dev, "cache line size %d"
				    " not supported; disabling PCI MWI\n",
				    cacheline);
			CSR_WRITE_4(sc, TI_PCI_CMDSTAT, CSR_READ_4(sc,
			    TI_PCI_CMDSTAT) & ~PCIM_CMD_MWIEN);
			break;
		}
	}

	TI_SETBIT(sc, TI_PCI_STATE, pci_writemax);

	/* This sets the min dma param all the way up (0xff). */
	TI_SETBIT(sc, TI_PCI_STATE, TI_PCISTATE_MINDMA);

	if (sc->ti_hdrsplit)
		hdrsplit = TI_OPMODE_JUMBO_HDRSPLIT;
	else
		hdrsplit = 0;

	/* Configure DMA variables. */
#if BYTE_ORDER == BIG_ENDIAN
	CSR_WRITE_4(sc, TI_GCR_OPMODE, TI_OPMODE_BYTESWAP_BD |
	    TI_OPMODE_BYTESWAP_DATA | TI_OPMODE_WORDSWAP_BD |
	    TI_OPMODE_WARN_ENB | TI_OPMODE_FATAL_ENB |
	    TI_OPMODE_DONT_FRAG_JUMBO | hdrsplit);
#else /* BYTE_ORDER */
	CSR_WRITE_4(sc, TI_GCR_OPMODE, TI_OPMODE_BYTESWAP_DATA|
	    TI_OPMODE_WORDSWAP_BD|TI_OPMODE_DONT_FRAG_JUMBO|
	    TI_OPMODE_WARN_ENB|TI_OPMODE_FATAL_ENB | hdrsplit);
#endif /* BYTE_ORDER */

	/*
	 * Only allow 1 DMA channel to be active at a time.
	 * I don't think this is a good idea, but without it
	 * the firmware racks up lots of nicDmaReadRingFull
	 * errors.  This is not compatible with hardware checksums.
	 */
	if ((sc->ti_ifp->if_capenable & (IFCAP_TXCSUM | IFCAP_RXCSUM)) == 0)
		TI_SETBIT(sc, TI_GCR_OPMODE, TI_OPMODE_1_DMA_ACTIVE);

	/* Recommended settings from Tigon manual. */
	CSR_WRITE_4(sc, TI_GCR_DMA_WRITECFG, TI_DMA_STATE_THRESH_8W);
	CSR_WRITE_4(sc, TI_GCR_DMA_READCFG, TI_DMA_STATE_THRESH_8W);

	if (ti_64bitslot_war(sc)) {
		device_printf(sc->ti_dev, "bios thinks we're in a 64 bit slot, "
		    "but we aren't");
		return (EINVAL);
	}

	return (0);
}

/*
 * Initialize the general information block and firmware, and
 * start the CPU(s) running.
 */
static int
ti_gibinit(struct ti_softc *sc)
{
	struct ifnet *ifp;
	struct ti_rcb *rcb;
	int i;

	TI_LOCK_ASSERT(sc);

	ifp = sc->ti_ifp;

	/* Disable interrupts for now. */
	CSR_WRITE_4(sc, TI_MB_HOSTINTR, 1);

	/* Tell the chip where to find the general information block. */
	CSR_WRITE_4(sc, TI_GCR_GENINFO_HI,
	    (uint64_t)sc->ti_rdata.ti_info_paddr >> 32);
	CSR_WRITE_4(sc, TI_GCR_GENINFO_LO,
	    sc->ti_rdata.ti_info_paddr & 0xFFFFFFFF);

	/* Load the firmware into SRAM. */
	ti_loadfw(sc);

	/* Set up the contents of the general info and ring control blocks. */

	/* Set up the event ring and producer pointer. */
	bzero(sc->ti_rdata.ti_event_ring, TI_EVENT_RING_SZ);
	rcb = &sc->ti_rdata.ti_info->ti_ev_rcb;
	ti_hostaddr64(&rcb->ti_hostaddr, sc->ti_rdata.ti_event_ring_paddr);
	rcb->ti_flags = 0;
	ti_hostaddr64(&sc->ti_rdata.ti_info->ti_ev_prodidx_ptr,
	    sc->ti_rdata.ti_status_paddr +
	    offsetof(struct ti_status, ti_ev_prodidx_r));
	sc->ti_ev_prodidx.ti_idx = 0;
	CSR_WRITE_4(sc, TI_GCR_EVENTCONS_IDX, 0);
	sc->ti_ev_saved_considx = 0;

	/* Set up the command ring and producer mailbox. */
	rcb = &sc->ti_rdata.ti_info->ti_cmd_rcb;
	ti_hostaddr64(&rcb->ti_hostaddr, TI_GCR_NIC_ADDR(TI_GCR_CMDRING));
	rcb->ti_flags = 0;
	rcb->ti_max_len = 0;
	for (i = 0; i < TI_CMD_RING_CNT; i++) {
		CSR_WRITE_4(sc, TI_GCR_CMDRING + (i * 4), 0);
	}
	CSR_WRITE_4(sc, TI_GCR_CMDCONS_IDX, 0);
	CSR_WRITE_4(sc, TI_MB_CMDPROD_IDX, 0);
	sc->ti_cmd_saved_prodidx = 0;

	/*
	 * Assign the address of the stats refresh buffer.
	 * We re-use the current stats buffer for this to
	 * conserve memory.
	 */
	bzero(&sc->ti_rdata.ti_info->ti_stats, sizeof(struct ti_stats));
	ti_hostaddr64(&sc->ti_rdata.ti_info->ti_refresh_stats_ptr,
	    sc->ti_rdata.ti_info_paddr + offsetof(struct ti_gib, ti_stats));

	/* Set up the standard receive ring. */
	rcb = &sc->ti_rdata.ti_info->ti_std_rx_rcb;
	ti_hostaddr64(&rcb->ti_hostaddr, sc->ti_rdata.ti_rx_std_ring_paddr);
	rcb->ti_max_len = TI_FRAMELEN;
	rcb->ti_flags = 0;
	if (sc->ti_ifp->if_capenable & IFCAP_RXCSUM)
		rcb->ti_flags |= TI_RCB_FLAG_TCP_UDP_CKSUM |
		     TI_RCB_FLAG_IP_CKSUM | TI_RCB_FLAG_NO_PHDR_CKSUM;
	if (sc->ti_ifp->if_capenable & IFCAP_VLAN_HWTAGGING)
		rcb->ti_flags |= TI_RCB_FLAG_VLAN_ASSIST;

	/* Set up the jumbo receive ring. */
	rcb = &sc->ti_rdata.ti_info->ti_jumbo_rx_rcb;
	ti_hostaddr64(&rcb->ti_hostaddr, sc->ti_rdata.ti_rx_jumbo_ring_paddr);

#ifndef TI_SF_BUF_JUMBO
	rcb->ti_max_len = MJUM9BYTES - ETHER_ALIGN;
	rcb->ti_flags = 0;
#else
	rcb->ti_max_len = PAGE_SIZE;
	rcb->ti_flags = TI_RCB_FLAG_USE_EXT_RX_BD;
#endif
	if (sc->ti_ifp->if_capenable & IFCAP_RXCSUM)
		rcb->ti_flags |= TI_RCB_FLAG_TCP_UDP_CKSUM |
		     TI_RCB_FLAG_IP_CKSUM | TI_RCB_FLAG_NO_PHDR_CKSUM;
	if (sc->ti_ifp->if_capenable & IFCAP_VLAN_HWTAGGING)
		rcb->ti_flags |= TI_RCB_FLAG_VLAN_ASSIST;

	/*
	 * Set up the mini ring. Only activated on the
	 * Tigon 2 but the slot in the config block is
	 * still there on the Tigon 1.
	 */
	rcb = &sc->ti_rdata.ti_info->ti_mini_rx_rcb;
	ti_hostaddr64(&rcb->ti_hostaddr, sc->ti_rdata.ti_rx_mini_ring_paddr);
	rcb->ti_max_len = MHLEN - ETHER_ALIGN;
	if (sc->ti_hwrev == TI_HWREV_TIGON)
		rcb->ti_flags = TI_RCB_FLAG_RING_DISABLED;
	else
		rcb->ti_flags = 0;
	if (sc->ti_ifp->if_capenable & IFCAP_RXCSUM)
		rcb->ti_flags |= TI_RCB_FLAG_TCP_UDP_CKSUM |
		     TI_RCB_FLAG_IP_CKSUM | TI_RCB_FLAG_NO_PHDR_CKSUM;
	if (sc->ti_ifp->if_capenable & IFCAP_VLAN_HWTAGGING)
		rcb->ti_flags |= TI_RCB_FLAG_VLAN_ASSIST;

	/*
	 * Set up the receive return ring.
	 */
	rcb = &sc->ti_rdata.ti_info->ti_return_rcb;
	ti_hostaddr64(&rcb->ti_hostaddr, sc->ti_rdata.ti_rx_return_ring_paddr);
	rcb->ti_flags = 0;
	rcb->ti_max_len = TI_RETURN_RING_CNT;
	ti_hostaddr64(&sc->ti_rdata.ti_info->ti_return_prodidx_ptr,
	    sc->ti_rdata.ti_status_paddr +
	    offsetof(struct ti_status, ti_return_prodidx_r));

	/*
	 * Set up the tx ring. Note: for the Tigon 2, we have the option
	 * of putting the transmit ring in the host's address space and
	 * letting the chip DMA it instead of leaving the ring in the NIC's
	 * memory and accessing it through the shared memory region. We
	 * do this for the Tigon 2, but it doesn't work on the Tigon 1,
	 * so we have to revert to the shared memory scheme if we detect
	 * a Tigon 1 chip.
	 */
	CSR_WRITE_4(sc, TI_WINBASE, TI_TX_RING_BASE);
	if (sc->ti_rdata.ti_tx_ring != NULL)
		bzero(sc->ti_rdata.ti_tx_ring, TI_TX_RING_SZ);
	rcb = &sc->ti_rdata.ti_info->ti_tx_rcb;
	if (sc->ti_hwrev == TI_HWREV_TIGON)
		rcb->ti_flags = 0;
	else
		rcb->ti_flags = TI_RCB_FLAG_HOST_RING;
	if (sc->ti_ifp->if_capenable & IFCAP_VLAN_HWTAGGING)
		rcb->ti_flags |= TI_RCB_FLAG_VLAN_ASSIST;
	if (sc->ti_ifp->if_capenable & IFCAP_TXCSUM)
		rcb->ti_flags |= TI_RCB_FLAG_TCP_UDP_CKSUM |
		     TI_RCB_FLAG_IP_CKSUM | TI_RCB_FLAG_NO_PHDR_CKSUM;
	rcb->ti_max_len = TI_TX_RING_CNT;
	if (sc->ti_hwrev == TI_HWREV_TIGON)
		ti_hostaddr64(&rcb->ti_hostaddr, TI_TX_RING_BASE);
	else
		ti_hostaddr64(&rcb->ti_hostaddr,
		    sc->ti_rdata.ti_tx_ring_paddr);
	ti_hostaddr64(&sc->ti_rdata.ti_info->ti_tx_considx_ptr,
	    sc->ti_rdata.ti_status_paddr +
	    offsetof(struct ti_status, ti_tx_considx_r));

	bus_dmamap_sync(sc->ti_cdata.ti_gib_tag, sc->ti_cdata.ti_gib_map,
	    BUS_DMASYNC_PREREAD | BUS_DMASYNC_PREWRITE);
	bus_dmamap_sync(sc->ti_cdata.ti_status_tag, sc->ti_cdata.ti_status_map,
	    BUS_DMASYNC_PREREAD | BUS_DMASYNC_PREWRITE);
	bus_dmamap_sync(sc->ti_cdata.ti_event_ring_tag,
	    sc->ti_cdata.ti_event_ring_map,
	    BUS_DMASYNC_PREREAD | BUS_DMASYNC_PREWRITE);
	if (sc->ti_rdata.ti_tx_ring != NULL)
		bus_dmamap_sync(sc->ti_cdata.ti_tx_ring_tag,
		    sc->ti_cdata.ti_tx_ring_map, BUS_DMASYNC_PREWRITE);

	/* Set up tunables */
#if 0
	if (ifp->if_mtu > ETHERMTU + ETHER_HDR_LEN + ETHER_VLAN_ENCAP_LEN)
		CSR_WRITE_4(sc, TI_GCR_RX_COAL_TICKS,
		    (sc->ti_rx_coal_ticks / 10));
	else
#endif
		CSR_WRITE_4(sc, TI_GCR_RX_COAL_TICKS, sc->ti_rx_coal_ticks);
	CSR_WRITE_4(sc, TI_GCR_TX_COAL_TICKS, sc->ti_tx_coal_ticks);
	CSR_WRITE_4(sc, TI_GCR_STAT_TICKS, sc->ti_stat_ticks);
	CSR_WRITE_4(sc, TI_GCR_RX_MAX_COAL_BD, sc->ti_rx_max_coal_bds);
	CSR_WRITE_4(sc, TI_GCR_TX_MAX_COAL_BD, sc->ti_tx_max_coal_bds);
	CSR_WRITE_4(sc, TI_GCR_TX_BUFFER_RATIO, sc->ti_tx_buf_ratio);

	/* Turn interrupts on. */
	CSR_WRITE_4(sc, TI_GCR_MASK_INTRS, 0);
	CSR_WRITE_4(sc, TI_MB_HOSTINTR, 0);

	/* Start CPU. */
	TI_CLRBIT(sc, TI_CPU_STATE, (TI_CPUSTATE_HALT|TI_CPUSTATE_STEP));

	return (0);
}

/*
 * Probe for a Tigon chip. Check the PCI vendor and device IDs
 * against our list and return its name if we find a match.
 */
static int
ti_probe(device_t dev)
{
	const struct ti_type *t;

	t = ti_devs;

	while (t->ti_name != NULL) {
		if ((pci_get_vendor(dev) == t->ti_vid) &&
		    (pci_get_device(dev) == t->ti_did)) {
			device_set_desc(dev, t->ti_name);
			return (BUS_PROBE_DEFAULT);
		}
		t++;
	}

	return (ENXIO);
}

static int
ti_attach(device_t dev)
{
	struct ifnet *ifp;
	struct ti_softc *sc;
	int error = 0, rid;
	u_char eaddr[6];

	sc = device_get_softc(dev);
	sc->ti_dev = dev;

	mtx_init(&sc->ti_mtx, device_get_nameunit(dev), MTX_NETWORK_LOCK,
	    MTX_DEF);
	callout_init_mtx(&sc->ti_watchdog, &sc->ti_mtx, 0);
	ifmedia_init(&sc->ifmedia, IFM_IMASK, ti_ifmedia_upd, ti_ifmedia_sts);
	ifp = sc->ti_ifp = if_alloc(IFT_ETHER);
	if (ifp == NULL) {
		device_printf(dev, "can not if_alloc()\n");
		error = ENOSPC;
		goto fail;
	}
	sc->ti_ifp->if_hwassist = TI_CSUM_FEATURES;
	sc->ti_ifp->if_capabilities = IFCAP_TXCSUM | IFCAP_RXCSUM;
	sc->ti_ifp->if_capenable = sc->ti_ifp->if_capabilities;

	/*
	 * Map control/status registers.
	 */
	pci_enable_busmaster(dev);

	rid = PCIR_BAR(0);
	sc->ti_res = bus_alloc_resource_any(dev, SYS_RES_MEMORY, &rid,
	    RF_ACTIVE);

	if (sc->ti_res == NULL) {
		device_printf(dev, "couldn't map memory\n");
		error = ENXIO;
		goto fail;
	}

	sc->ti_btag = rman_get_bustag(sc->ti_res);
	sc->ti_bhandle = rman_get_bushandle(sc->ti_res);

	/* Allocate interrupt */
	rid = 0;

	sc->ti_irq = bus_alloc_resource_any(dev, SYS_RES_IRQ, &rid,
	    RF_SHAREABLE | RF_ACTIVE);

	if (sc->ti_irq == NULL) {
		device_printf(dev, "couldn't map interrupt\n");
		error = ENXIO;
		goto fail;
	}

	if (ti_chipinit(sc)) {
		device_printf(dev, "chip initialization failed\n");
		error = ENXIO;
		goto fail;
	}

	/* Zero out the NIC's on-board SRAM. */
	ti_mem_zero(sc, 0x2000, 0x100000 - 0x2000);

	/* Init again -- zeroing memory may have clobbered some registers. */
	if (ti_chipinit(sc)) {
		device_printf(dev, "chip initialization failed\n");
		error = ENXIO;
		goto fail;
	}

	/*
	 * Get station address from the EEPROM. Note: the manual states
	 * that the MAC address is at offset 0x8c, however the data is
	 * stored as two longwords (since that's how it's loaded into
	 * the NIC). This means the MAC address is actually preceded
	 * by two zero bytes. We need to skip over those.
	 */
	if (ti_read_eeprom(sc, eaddr, TI_EE_MAC_OFFSET + 2, ETHER_ADDR_LEN)) {
		device_printf(dev, "failed to read station address\n");
		error = ENXIO;
		goto fail;
	}

	/* Allocate working area for memory dump. */
	sc->ti_membuf = malloc(sizeof(uint8_t) * TI_WINLEN, M_DEVBUF, M_NOWAIT);
	sc->ti_membuf2 = malloc(sizeof(uint8_t) * TI_WINLEN, M_DEVBUF,
	    M_NOWAIT);
	if (sc->ti_membuf == NULL || sc->ti_membuf2 == NULL) {
		device_printf(dev, "cannot allocate memory buffer\n");
		error = ENOMEM;
		goto fail;
	}
	if ((error = ti_dma_alloc(sc)) != 0)
		goto fail;

	/*
	 * We really need a better way to tell a 1000baseTX card
	 * from a 1000baseSX one, since in theory there could be
	 * OEMed 1000baseTX cards from lame vendors who aren't
	 * clever enough to change the PCI ID. For the moment
	 * though, the AceNIC is the only copper card available.
	 */
	if (pci_get_vendor(dev) == ALT_VENDORID &&
	    pci_get_device(dev) == ALT_DEVICEID_ACENIC_COPPER)
		sc->ti_copper = 1;
	/* Ok, it's not the only copper card available. */
	if (pci_get_vendor(dev) == NG_VENDORID &&
	    pci_get_device(dev) == NG_DEVICEID_GA620T)
		sc->ti_copper = 1;

	/* Set default tunable values. */
	ti_sysctl_node(sc);

	/* Set up ifnet structure */
	ifp->if_softc = sc;
	if_initname(ifp, device_get_name(dev), device_get_unit(dev));
	ifp->if_flags = IFF_BROADCAST | IFF_SIMPLEX | IFF_MULTICAST;
	ifp->if_ioctl = ti_ioctl;
	ifp->if_start = ti_start;
	ifp->if_init = ti_init;
	ifp->if_get_counter = ti_get_counter;
	ifp->if_baudrate = IF_Gbps(1UL);
	ifp->if_snd.ifq_drv_maxlen = TI_TX_RING_CNT - 1;
	IFQ_SET_MAXLEN(&ifp->if_snd, ifp->if_snd.ifq_drv_maxlen);
	IFQ_SET_READY(&ifp->if_snd);

	/* Set up ifmedia support. */
	if (sc->ti_copper) {
		/*
		 * Copper cards allow manual 10/100 mode selection,
		 * but not manual 1000baseTX mode selection. Why?
		 * Because currently there's no way to specify the
		 * master/slave setting through the firmware interface,
		 * so Alteon decided to just bag it and handle it
		 * via autonegotiation.
		 */
		ifmedia_add(&sc->ifmedia, IFM_ETHER|IFM_10_T, 0, NULL);
		ifmedia_add(&sc->ifmedia,
		    IFM_ETHER|IFM_10_T|IFM_FDX, 0, NULL);
		ifmedia_add(&sc->ifmedia, IFM_ETHER|IFM_100_TX, 0, NULL);
		ifmedia_add(&sc->ifmedia,
		    IFM_ETHER|IFM_100_TX|IFM_FDX, 0, NULL);
		ifmedia_add(&sc->ifmedia, IFM_ETHER|IFM_1000_T, 0, NULL);
		ifmedia_add(&sc->ifmedia,
		    IFM_ETHER|IFM_1000_T|IFM_FDX, 0, NULL);
	} else {
		/* Fiber cards don't support 10/100 modes. */
		ifmedia_add(&sc->ifmedia, IFM_ETHER|IFM_1000_SX, 0, NULL);
		ifmedia_add(&sc->ifmedia,
		    IFM_ETHER|IFM_1000_SX|IFM_FDX, 0, NULL);
	}
	ifmedia_add(&sc->ifmedia, IFM_ETHER|IFM_AUTO, 0, NULL);
	ifmedia_set(&sc->ifmedia, IFM_ETHER|IFM_AUTO);

	/*
	 * We're assuming here that card initialization is a sequential
	 * thing.  If it isn't, multiple cards probing at the same time
	 * could stomp on the list of softcs here.
	 */

	/* Register the device */
	sc->dev = make_dev(&ti_cdevsw, device_get_unit(dev), UID_ROOT,
	    GID_OPERATOR, 0600, "ti%d", device_get_unit(dev));
	sc->dev->si_drv1 = sc;

	/*
	 * Call MI attach routine.
	 */
	ether_ifattach(ifp, eaddr);

	/* VLAN capability setup. */
	ifp->if_capabilities |= IFCAP_VLAN_MTU | IFCAP_VLAN_HWCSUM |
	    IFCAP_VLAN_HWTAGGING;
	ifp->if_capenable = ifp->if_capabilities;
	/* Tell the upper layer we support VLAN over-sized frames. */
	ifp->if_hdrlen = sizeof(struct ether_vlan_header);

	/* Driver supports link state tracking. */
	ifp->if_capabilities |= IFCAP_LINKSTATE;
	ifp->if_capenable |= IFCAP_LINKSTATE;

	/* Hook interrupt last to avoid having to lock softc */
	error = bus_setup_intr(dev, sc->ti_irq, INTR_TYPE_NET|INTR_MPSAFE,
	   NULL, ti_intr, sc, &sc->ti_intrhand);

	if (error) {
		device_printf(dev, "couldn't set up irq\n");
		goto fail;
	}

fail:
	if (error)
		ti_detach(dev);

	return (error);
}

/*
 * Shutdown hardware and free up resources. This can be called any
 * time after the mutex has been initialized. It is called in both
 * the error case in attach and the normal detach case so it needs
 * to be careful about only freeing resources that have actually been
 * allocated.
 */
static int
ti_detach(device_t dev)
{
	struct ti_softc *sc;
	struct ifnet *ifp;

	sc = device_get_softc(dev);
	if (sc->dev)
		destroy_dev(sc->dev);
	KASSERT(mtx_initialized(&sc->ti_mtx), ("ti mutex not initialized"));
	ifp = sc->ti_ifp;
	if (device_is_attached(dev)) {
		ether_ifdetach(ifp);
		TI_LOCK(sc);
		ti_stop(sc);
		TI_UNLOCK(sc);
	}

	/* These should only be active if attach succeeded */
	callout_drain(&sc->ti_watchdog);
	bus_generic_detach(dev);
	ti_dma_free(sc);
	ifmedia_removeall(&sc->ifmedia);

	if (sc->ti_intrhand)
		bus_teardown_intr(dev, sc->ti_irq, sc->ti_intrhand);
	if (sc->ti_irq)
		bus_release_resource(dev, SYS_RES_IRQ, 0, sc->ti_irq);
	if (sc->ti_res) {
		bus_release_resource(dev, SYS_RES_MEMORY, PCIR_BAR(0),
		    sc->ti_res);
	}
	if (ifp)
		if_free(ifp);
	if (sc->ti_membuf)
		free(sc->ti_membuf, M_DEVBUF);
	if (sc->ti_membuf2)
		free(sc->ti_membuf2, M_DEVBUF);

	mtx_destroy(&sc->ti_mtx);

	return (0);
}

#ifdef TI_JUMBO_HDRSPLIT
/*
 * If hdr_len is 0, that means that header splitting wasn't done on
 * this packet for some reason.  The two most likely reasons are that
 * the protocol isn't a supported protocol for splitting, or this
 * packet had a fragment offset that wasn't 0.
 *
 * The header length, if it is non-zero, will always be the length of
 * the headers on the packet, but that length could be longer than the
 * first mbuf.  So we take the minimum of the two as the actual
 * length.
 */
static __inline void
ti_hdr_split(struct mbuf *top, int hdr_len, int pkt_len, int idx)
{
	int i = 0;
	int lengths[4] = {0, 0, 0, 0};
	struct mbuf *m, *mp;

	if (hdr_len != 0)
		top->m_len = min(hdr_len, top->m_len);
	pkt_len -= top->m_len;
	lengths[i++] = top->m_len;

	mp = top;
	for (m = top->m_next; m && pkt_len; m = m->m_next) {
		m->m_len = m->m_ext.ext_size = min(m->m_len, pkt_len);
		pkt_len -= m->m_len;
		lengths[i++] = m->m_len;
		mp = m;
	}

#if 0
	if (hdr_len != 0)
		printf("got split packet: ");
	else
		printf("got non-split packet: ");

	printf("%d,%d,%d,%d = %d\n", lengths[0],
	    lengths[1], lengths[2], lengths[3],
	    lengths[0] + lengths[1] + lengths[2] +
	    lengths[3]);
#endif

	if (pkt_len)
		panic("header splitting didn't");

	if (m) {
		m_freem(m);
		mp->m_next = NULL;

	}
	if (mp->m_next != NULL)
		panic("ti_hdr_split: last mbuf in chain should be null");
}
#endif /* TI_JUMBO_HDRSPLIT */

static void
ti_discard_std(struct ti_softc *sc, int i)
{

	struct ti_rx_desc *r;

	r = &sc->ti_rdata.ti_rx_std_ring[i];
	r->ti_len = MCLBYTES - ETHER_ALIGN;
	r->ti_type = TI_BDTYPE_RECV_BD;
	r->ti_flags = 0;
	r->ti_vlan_tag = 0;
	r->ti_tcp_udp_cksum = 0;
	if (sc->ti_ifp->if_capenable & IFCAP_RXCSUM)
		r->ti_flags |= TI_BDFLAG_TCP_UDP_CKSUM | TI_BDFLAG_IP_CKSUM;
	r->ti_idx = i;
}

static void
ti_discard_mini(struct ti_softc *sc, int i)
{

	struct ti_rx_desc *r;

	r = &sc->ti_rdata.ti_rx_mini_ring[i];
	r->ti_len = MHLEN - ETHER_ALIGN;
	r->ti_type = TI_BDTYPE_RECV_BD;
	r->ti_flags = TI_BDFLAG_MINI_RING;
	r->ti_vlan_tag = 0;
	r->ti_tcp_udp_cksum = 0;
	if (sc->ti_ifp->if_capenable & IFCAP_RXCSUM)
		r->ti_flags |= TI_BDFLAG_TCP_UDP_CKSUM | TI_BDFLAG_IP_CKSUM;
	r->ti_idx = i;
}

#ifndef TI_SF_BUF_JUMBO
static void
ti_discard_jumbo(struct ti_softc *sc, int i)
{

	struct ti_rx_desc *r;

	r = &sc->ti_rdata.ti_rx_jumbo_ring[i];
	r->ti_len = MJUM9BYTES - ETHER_ALIGN;
	r->ti_type = TI_BDTYPE_RECV_JUMBO_BD;
	r->ti_flags = TI_BDFLAG_JUMBO_RING;
	r->ti_vlan_tag = 0;
	r->ti_tcp_udp_cksum = 0;
	if (sc->ti_ifp->if_capenable & IFCAP_RXCSUM)
		r->ti_flags |= TI_BDFLAG_TCP_UDP_CKSUM | TI_BDFLAG_IP_CKSUM;
	r->ti_idx = i;
}
#endif

/*
 * Frame reception handling. This is called if there's a frame
 * on the receive return list.
 *
 * Note: we have to be able to handle three possibilities here:
 * 1) the frame is from the mini receive ring (can only happen)
 *    on Tigon 2 boards)
 * 2) the frame is from the jumbo receive ring
 * 3) the frame is from the standard receive ring
 */

static void
ti_rxeof(struct ti_softc *sc)
{
	struct ifnet *ifp;
#ifdef TI_SF_BUF_JUMBO
	bus_dmamap_t map;
#endif
	struct ti_cmd_desc cmd;
	int jumbocnt, minicnt, stdcnt, ti_len;

	TI_LOCK_ASSERT(sc);

	ifp = sc->ti_ifp;

	bus_dmamap_sync(sc->ti_cdata.ti_rx_std_ring_tag,
	    sc->ti_cdata.ti_rx_std_ring_map, BUS_DMASYNC_POSTWRITE);
	if (ifp->if_mtu > ETHERMTU + ETHER_HDR_LEN + ETHER_VLAN_ENCAP_LEN)
		bus_dmamap_sync(sc->ti_cdata.ti_rx_jumbo_ring_tag,
		    sc->ti_cdata.ti_rx_jumbo_ring_map, BUS_DMASYNC_POSTWRITE);
	if (sc->ti_rdata.ti_rx_mini_ring != NULL)
		bus_dmamap_sync(sc->ti_cdata.ti_rx_mini_ring_tag,
		    sc->ti_cdata.ti_rx_mini_ring_map, BUS_DMASYNC_POSTWRITE);
	bus_dmamap_sync(sc->ti_cdata.ti_rx_return_ring_tag,
	    sc->ti_cdata.ti_rx_return_ring_map, BUS_DMASYNC_POSTREAD);

	jumbocnt = minicnt = stdcnt = 0;
	while (sc->ti_rx_saved_considx != sc->ti_return_prodidx.ti_idx) {
		struct ti_rx_desc *cur_rx;
		uint32_t rxidx;
		struct mbuf *m = NULL;
		uint16_t vlan_tag = 0;
		int have_tag = 0;

		cur_rx =
		    &sc->ti_rdata.ti_rx_return_ring[sc->ti_rx_saved_considx];
		rxidx = cur_rx->ti_idx;
		ti_len = cur_rx->ti_len;
		TI_INC(sc->ti_rx_saved_considx, TI_RETURN_RING_CNT);

		if (cur_rx->ti_flags & TI_BDFLAG_VLAN_TAG) {
			have_tag = 1;
			vlan_tag = cur_rx->ti_vlan_tag;
		}

		if (cur_rx->ti_flags & TI_BDFLAG_JUMBO_RING) {
			jumbocnt++;
			TI_INC(sc->ti_jumbo, TI_JUMBO_RX_RING_CNT);
			m = sc->ti_cdata.ti_rx_jumbo_chain[rxidx];
#ifndef TI_SF_BUF_JUMBO
			if (cur_rx->ti_flags & TI_BDFLAG_ERROR) {
				if_inc_counter(ifp, IFCOUNTER_IERRORS, 1);
				ti_discard_jumbo(sc, rxidx);
				continue;
			}
			if (ti_newbuf_jumbo(sc, rxidx, NULL) != 0) {
				if_inc_counter(ifp, IFCOUNTER_IQDROPS, 1);
				ti_discard_jumbo(sc, rxidx);
				continue;
			}
			m->m_len = ti_len;
#else /* !TI_SF_BUF_JUMBO */
			sc->ti_cdata.ti_rx_jumbo_chain[rxidx] = NULL;
			map = sc->ti_cdata.ti_rx_jumbo_maps[rxidx];
			bus_dmamap_sync(sc->ti_cdata.ti_rx_jumbo_tag, map,
			    BUS_DMASYNC_POSTREAD);
			bus_dmamap_unload(sc->ti_cdata.ti_rx_jumbo_tag, map);
			if (cur_rx->ti_flags & TI_BDFLAG_ERROR) {
				if_inc_counter(ifp, IFCOUNTER_IERRORS, 1);
				ti_newbuf_jumbo(sc, sc->ti_jumbo, m);
				continue;
			}
			if (ti_newbuf_jumbo(sc, sc->ti_jumbo, NULL) == ENOBUFS) {
				if_inc_counter(ifp, IFCOUNTER_IQDROPS, 1);
				ti_newbuf_jumbo(sc, sc->ti_jumbo, m);
				continue;
			}
#ifdef TI_JUMBO_HDRSPLIT
			if (sc->ti_hdrsplit)
				ti_hdr_split(m, TI_HOSTADDR(cur_rx->ti_addr),
					     ti_len, rxidx);
			else
#endif /* TI_JUMBO_HDRSPLIT */
			m_adj(m, ti_len - m->m_pkthdr.len);
#endif /* TI_SF_BUF_JUMBO */
		} else if (cur_rx->ti_flags & TI_BDFLAG_MINI_RING) {
			minicnt++;
			TI_INC(sc->ti_mini, TI_MINI_RX_RING_CNT);
			m = sc->ti_cdata.ti_rx_mini_chain[rxidx];
			if (cur_rx->ti_flags & TI_BDFLAG_ERROR) {
				if_inc_counter(ifp, IFCOUNTER_IERRORS, 1);
				ti_discard_mini(sc, rxidx);
				continue;
			}
			if (ti_newbuf_mini(sc, rxidx) != 0) {
				if_inc_counter(ifp, IFCOUNTER_IQDROPS, 1);
				ti_discard_mini(sc, rxidx);
				continue;
			}
			m->m_len = ti_len;
		} else {
			stdcnt++;
			TI_INC(sc->ti_std, TI_STD_RX_RING_CNT);
			m = sc->ti_cdata.ti_rx_std_chain[rxidx];
			if (cur_rx->ti_flags & TI_BDFLAG_ERROR) {
				if_inc_counter(ifp, IFCOUNTER_IERRORS, 1);
				ti_discard_std(sc, rxidx);
				continue;
			}
			if (ti_newbuf_std(sc, rxidx) != 0) {
				if_inc_counter(ifp, IFCOUNTER_IQDROPS, 1);
				ti_discard_std(sc, rxidx);
				continue;
			}
			m->m_len = ti_len;
		}

		m->m_pkthdr.len = ti_len;
		if_inc_counter(ifp, IFCOUNTER_IPACKETS, 1);
		m->m_pkthdr.rcvif = ifp;

		if (ifp->if_capenable & IFCAP_RXCSUM) {
			if (cur_rx->ti_flags & TI_BDFLAG_IP_CKSUM) {
				m->m_pkthdr.csum_flags |= CSUM_IP_CHECKED;
				if ((cur_rx->ti_ip_cksum ^ 0xffff) == 0)
					m->m_pkthdr.csum_flags |= CSUM_IP_VALID;
			}
			if (cur_rx->ti_flags & TI_BDFLAG_TCP_UDP_CKSUM) {
				m->m_pkthdr.csum_data =
				    cur_rx->ti_tcp_udp_cksum;
				m->m_pkthdr.csum_flags |= CSUM_DATA_VALID;
			}
		}

		/*
		 * If we received a packet with a vlan tag,
		 * tag it before passing the packet upward.
		 */
		if (have_tag) {
			m->m_pkthdr.ether_vtag = vlan_tag;
			m->m_flags |= M_VLANTAG;
		}
		TI_UNLOCK(sc);
		(*ifp->if_input)(ifp, m);
		TI_LOCK(sc);
	}

	bus_dmamap_sync(sc->ti_cdata.ti_rx_return_ring_tag,
	    sc->ti_cdata.ti_rx_return_ring_map, BUS_DMASYNC_PREREAD);
	/* Only necessary on the Tigon 1. */
	if (sc->ti_hwrev == TI_HWREV_TIGON)
		CSR_WRITE_4(sc, TI_GCR_RXRETURNCONS_IDX,
		    sc->ti_rx_saved_considx);

	if (stdcnt > 0) {
		bus_dmamap_sync(sc->ti_cdata.ti_rx_std_ring_tag,
		    sc->ti_cdata.ti_rx_std_ring_map, BUS_DMASYNC_PREWRITE);
		TI_UPDATE_STDPROD(sc, sc->ti_std);
	}
	if (minicnt > 0) {
		bus_dmamap_sync(sc->ti_cdata.ti_rx_mini_ring_tag,
		    sc->ti_cdata.ti_rx_mini_ring_map, BUS_DMASYNC_PREWRITE);
		TI_UPDATE_MINIPROD(sc, sc->ti_mini);
	}
	if (jumbocnt > 0) {
		bus_dmamap_sync(sc->ti_cdata.ti_rx_jumbo_ring_tag,
		    sc->ti_cdata.ti_rx_jumbo_ring_map, BUS_DMASYNC_PREWRITE);
		TI_UPDATE_JUMBOPROD(sc, sc->ti_jumbo);
	}
}

static void
ti_txeof(struct ti_softc *sc)
{
	struct ti_txdesc *txd;
	struct ti_tx_desc txdesc;
	struct ti_tx_desc *cur_tx = NULL;
	struct ifnet *ifp;
	int idx;

	ifp = sc->ti_ifp;

	txd = STAILQ_FIRST(&sc->ti_cdata.ti_txbusyq);
	if (txd == NULL)
		return;

	if (sc->ti_rdata.ti_tx_ring != NULL)
		bus_dmamap_sync(sc->ti_cdata.ti_tx_ring_tag,
		    sc->ti_cdata.ti_tx_ring_map, BUS_DMASYNC_POSTWRITE);
	/*
	 * Go through our tx ring and free mbufs for those
	 * frames that have been sent.
	 */
	for (idx = sc->ti_tx_saved_considx; idx != sc->ti_tx_considx.ti_idx;
	    TI_INC(idx, TI_TX_RING_CNT)) {
		if (sc->ti_hwrev == TI_HWREV_TIGON) {
			ti_mem_read(sc, TI_TX_RING_BASE + idx * sizeof(txdesc),
			    sizeof(txdesc), &txdesc);
			cur_tx = &txdesc;
		} else
			cur_tx = &sc->ti_rdata.ti_tx_ring[idx];
		sc->ti_txcnt--;
		ifp->if_drv_flags &= ~IFF_DRV_OACTIVE;
		if ((cur_tx->ti_flags & TI_BDFLAG_END) == 0)
			continue;
		bus_dmamap_sync(sc->ti_cdata.ti_tx_tag, txd->tx_dmamap,
		    BUS_DMASYNC_POSTWRITE);
		bus_dmamap_unload(sc->ti_cdata.ti_tx_tag, txd->tx_dmamap);

		if_inc_counter(ifp, IFCOUNTER_OPACKETS, 1);
		m_freem(txd->tx_m);
		txd->tx_m = NULL;
		STAILQ_REMOVE_HEAD(&sc->ti_cdata.ti_txbusyq, tx_q);
		STAILQ_INSERT_TAIL(&sc->ti_cdata.ti_txfreeq, txd, tx_q);
		txd = STAILQ_FIRST(&sc->ti_cdata.ti_txbusyq);
	}
	sc->ti_tx_saved_considx = idx;
	if (sc->ti_txcnt == 0)
		sc->ti_timer = 0;
}

static void
ti_intr(void *xsc)
{
	struct ti_softc *sc;
	struct ifnet *ifp;

	sc = xsc;
	TI_LOCK(sc);
	ifp = sc->ti_ifp;

	/* Make sure this is really our interrupt. */
	if (!(CSR_READ_4(sc, TI_MISC_HOST_CTL) & TI_MHC_INTSTATE)) {
		TI_UNLOCK(sc);
		return;
	}

	/* Ack interrupt and stop others from occurring. */
	CSR_WRITE_4(sc, TI_MB_HOSTINTR, 1);

	if (ifp->if_drv_flags & IFF_DRV_RUNNING) {
		bus_dmamap_sync(sc->ti_cdata.ti_status_tag,
		    sc->ti_cdata.ti_status_map, BUS_DMASYNC_POSTREAD);
		/* Check RX return ring producer/consumer */
		ti_rxeof(sc);

		/* Check TX ring producer/consumer */
		ti_txeof(sc);
		bus_dmamap_sync(sc->ti_cdata.ti_status_tag,
		    sc->ti_cdata.ti_status_map, BUS_DMASYNC_PREREAD);
	}

	ti_handle_events(sc);

	if (ifp->if_drv_flags & IFF_DRV_RUNNING) {
		/* Re-enable interrupts. */
		CSR_WRITE_4(sc, TI_MB_HOSTINTR, 0);
		if (!IFQ_DRV_IS_EMPTY(&ifp->if_snd))
			ti_start_locked(ifp);
	}

	TI_UNLOCK(sc);
}

static uint64_t
ti_get_counter(struct ifnet *ifp, ift_counter cnt)
{

	switch (cnt) {
	case IFCOUNTER_COLLISIONS:
	    {
		struct ti_softc *sc;
		struct ti_stats *s;
		uint64_t rv;

		sc = if_getsoftc(ifp);
		s = &sc->ti_rdata.ti_info->ti_stats;

		TI_LOCK(sc);
		bus_dmamap_sync(sc->ti_cdata.ti_gib_tag,
		    sc->ti_cdata.ti_gib_map, BUS_DMASYNC_POSTREAD);
		rv = s->dot3StatsSingleCollisionFrames +
		    s->dot3StatsMultipleCollisionFrames +
		    s->dot3StatsExcessiveCollisions +
		    s->dot3StatsLateCollisions;
		bus_dmamap_sync(sc->ti_cdata.ti_gib_tag,
		    sc->ti_cdata.ti_gib_map, BUS_DMASYNC_PREREAD);
		TI_UNLOCK(sc);
		return (rv);
	    }
	default:
		return (if_get_counter_default(ifp, cnt));
	}
}

/*
 * Encapsulate an mbuf chain in the tx ring  by coupling the mbuf data
 * pointers to descriptors.
 */
static int
ti_encap(struct ti_softc *sc, struct mbuf **m_head)
{
	struct ti_txdesc *txd;
	struct ti_tx_desc *f;
	struct ti_tx_desc txdesc;
	struct mbuf *m;
	bus_dma_segment_t txsegs[TI_MAXTXSEGS];
	uint16_t csum_flags;
	int error, frag, i, nseg;

	if ((txd = STAILQ_FIRST(&sc->ti_cdata.ti_txfreeq)) == NULL)
		return (ENOBUFS);

	error = bus_dmamap_load_mbuf_sg(sc->ti_cdata.ti_tx_tag, txd->tx_dmamap,
	    *m_head, txsegs, &nseg, 0);
	if (error == EFBIG) {
		m = m_defrag(*m_head, M_NOWAIT);
		if (m == NULL) {
			m_freem(*m_head);
			*m_head = NULL;
			return (ENOMEM);
		}
		*m_head = m;
		error = bus_dmamap_load_mbuf_sg(sc->ti_cdata.ti_tx_tag,
		    txd->tx_dmamap, *m_head, txsegs, &nseg, 0);
		if (error) {
			m_freem(*m_head);
			*m_head = NULL;
			return (error);
		}
	} else if (error != 0)
		return (error);
	if (nseg == 0) {
		m_freem(*m_head);
		*m_head = NULL;
		return (EIO);
	}

	if (sc->ti_txcnt + nseg >= TI_TX_RING_CNT) {
		bus_dmamap_unload(sc->ti_cdata.ti_tx_tag, txd->tx_dmamap);
		return (ENOBUFS);
	}
	bus_dmamap_sync(sc->ti_cdata.ti_tx_tag, txd->tx_dmamap,
	    BUS_DMASYNC_PREWRITE);

	m = *m_head;
	csum_flags = 0;
	if (m->m_pkthdr.csum_flags & CSUM_IP)
		csum_flags |= TI_BDFLAG_IP_CKSUM;
	if (m->m_pkthdr.csum_flags & (CSUM_TCP | CSUM_UDP))
		csum_flags |= TI_BDFLAG_TCP_UDP_CKSUM;

	frag = sc->ti_tx_saved_prodidx;
	for (i = 0; i < nseg; i++) {
		if (sc->ti_hwrev == TI_HWREV_TIGON) {
			bzero(&txdesc, sizeof(txdesc));
			f = &txdesc;
		} else
			f = &sc->ti_rdata.ti_tx_ring[frag];
		ti_hostaddr64(&f->ti_addr, txsegs[i].ds_addr);
		f->ti_len = txsegs[i].ds_len;
		f->ti_flags = csum_flags;
		if (m->m_flags & M_VLANTAG) {
			f->ti_flags |= TI_BDFLAG_VLAN_TAG;
			f->ti_vlan_tag = m->m_pkthdr.ether_vtag;
		} else {
			f->ti_vlan_tag = 0;
		}

		if (sc->ti_hwrev == TI_HWREV_TIGON)
			ti_mem_write(sc, TI_TX_RING_BASE + frag *
			    sizeof(txdesc), sizeof(txdesc), &txdesc);
		TI_INC(frag, TI_TX_RING_CNT);
	}

	sc->ti_tx_saved_prodidx = frag;
	/* set TI_BDFLAG_END on the last descriptor */
	frag = (frag + TI_TX_RING_CNT - 1) % TI_TX_RING_CNT;
	if (sc->ti_hwrev == TI_HWREV_TIGON) {
		txdesc.ti_flags |= TI_BDFLAG_END;
		ti_mem_write(sc, TI_TX_RING_BASE + frag * sizeof(txdesc),
		    sizeof(txdesc), &txdesc);
	} else
		sc->ti_rdata.ti_tx_ring[frag].ti_flags |= TI_BDFLAG_END;

	STAILQ_REMOVE_HEAD(&sc->ti_cdata.ti_txfreeq, tx_q);
	STAILQ_INSERT_TAIL(&sc->ti_cdata.ti_txbusyq, txd, tx_q);
	txd->tx_m = m;
	sc->ti_txcnt += nseg;

	return (0);
}

static void
ti_start(struct ifnet *ifp)
{
	struct ti_softc *sc;

	sc = ifp->if_softc;
	TI_LOCK(sc);
	ti_start_locked(ifp);
	TI_UNLOCK(sc);
}

/*
 * Main transmit routine. To avoid having to do mbuf copies, we put pointers
 * to the mbuf data regions directly in the transmit descriptors.
 */
static void
ti_start_locked(struct ifnet *ifp)
{
	struct ti_softc *sc;
	struct mbuf *m_head = NULL;
	int enq = 0;

	sc = ifp->if_softc;

	for (; !IFQ_DRV_IS_EMPTY(&ifp->if_snd) &&
	    sc->ti_txcnt < (TI_TX_RING_CNT - 16);) {
		IFQ_DRV_DEQUEUE(&ifp->if_snd, m_head);
		if (m_head == NULL)
			break;

		/*
		 * Pack the data into the transmit ring. If we
		 * don't have room, set the OACTIVE flag and wait
		 * for the NIC to drain the ring.
		 */
		if (ti_encap(sc, &m_head)) {
			if (m_head == NULL)
				break;
			IFQ_DRV_PREPEND(&ifp->if_snd, m_head);
			ifp->if_drv_flags |= IFF_DRV_OACTIVE;
			break;
		}

		enq++;
		/*
		 * If there's a BPF listener, bounce a copy of this frame
		 * to him.
		 */
		ETHER_BPF_MTAP(ifp, m_head);
	}

	if (enq > 0) {
		if (sc->ti_rdata.ti_tx_ring != NULL)
			bus_dmamap_sync(sc->ti_cdata.ti_tx_ring_tag,
			    sc->ti_cdata.ti_tx_ring_map, BUS_DMASYNC_PREWRITE);
		/* Transmit */
		CSR_WRITE_4(sc, TI_MB_SENDPROD_IDX, sc->ti_tx_saved_prodidx);

		/*
		 * Set a timeout in case the chip goes out to lunch.
		 */
		sc->ti_timer = 5;
	}
}

static void
ti_init(void *xsc)
{
	struct ti_softc *sc;

	sc = xsc;
	TI_LOCK(sc);
	ti_init_locked(sc);
	TI_UNLOCK(sc);
}

static void
ti_init_locked(void *xsc)
{
	struct ti_softc *sc = xsc;

	if (sc->ti_ifp->if_drv_flags & IFF_DRV_RUNNING)
		return;

	/* Cancel pending I/O and flush buffers. */
	ti_stop(sc);

	/* Init the gen info block, ring control blocks and firmware. */
	if (ti_gibinit(sc)) {
		device_printf(sc->ti_dev, "initialization failure\n");
		return;
	}
}

static void ti_init2(struct ti_softc *sc)
{
	struct ti_cmd_desc cmd;
	struct ifnet *ifp;
	uint8_t *ea;
	struct ifmedia *ifm;
	int tmp;

	TI_LOCK_ASSERT(sc);

	ifp = sc->ti_ifp;

	/* Specify MTU and interface index. */
	CSR_WRITE_4(sc, TI_GCR_IFINDEX, device_get_unit(sc->ti_dev));
	CSR_WRITE_4(sc, TI_GCR_IFMTU, ifp->if_mtu +
	    ETHER_HDR_LEN + ETHER_CRC_LEN + ETHER_VLAN_ENCAP_LEN);
	TI_DO_CMD(TI_CMD_UPDATE_GENCOM, 0, 0);

	/* Load our MAC address. */
	ea = IF_LLADDR(sc->ti_ifp);
	CSR_WRITE_4(sc, TI_GCR_PAR0, (ea[0] << 8) | ea[1]);
	CSR_WRITE_4(sc, TI_GCR_PAR1,
	    (ea[2] << 24) | (ea[3] << 16) | (ea[4] << 8) | ea[5]);
	TI_DO_CMD(TI_CMD_SET_MAC_ADDR, 0, 0);

	/* Enable or disable promiscuous mode as needed. */
	if (ifp->if_flags & IFF_PROMISC) {
		TI_DO_CMD(TI_CMD_SET_PROMISC_MODE, TI_CMD_CODE_PROMISC_ENB, 0);
	} else {
		TI_DO_CMD(TI_CMD_SET_PROMISC_MODE, TI_CMD_CODE_PROMISC_DIS, 0);
	}

	/* Program multicast filter. */
	ti_setmulti(sc);

	/*
	 * If this is a Tigon 1, we should tell the
	 * firmware to use software packet filtering.
	 */
	if (sc->ti_hwrev == TI_HWREV_TIGON) {
		TI_DO_CMD(TI_CMD_FDR_FILTERING, TI_CMD_CODE_FILT_ENB, 0);
	}

	/* Init RX ring. */
	if (ti_init_rx_ring_std(sc) != 0) {
		/* XXX */
		device_printf(sc->ti_dev, "no memory for std Rx buffers.\n");
		return;
	}

	/* Init jumbo RX ring. */
	if (ifp->if_mtu > ETHERMTU + ETHER_HDR_LEN + ETHER_VLAN_ENCAP_LEN) {
		if (ti_init_rx_ring_jumbo(sc) != 0) {
			/* XXX */
			device_printf(sc->ti_dev,
			    "no memory for jumbo Rx buffers.\n");
			return;
		}
	}

	/*
	 * If this is a Tigon 2, we can also configure the
	 * mini ring.
	 */
	if (sc->ti_hwrev == TI_HWREV_TIGON_II) {
		if (ti_init_rx_ring_mini(sc) != 0) {
			/* XXX */
			device_printf(sc->ti_dev,
			    "no memory for mini Rx buffers.\n");
			return;
		}
	}

	CSR_WRITE_4(sc, TI_GCR_RXRETURNCONS_IDX, 0);
	sc->ti_rx_saved_considx = 0;

	/* Init TX ring. */
	ti_init_tx_ring(sc);

	/* Tell firmware we're alive. */
	TI_DO_CMD(TI_CMD_HOST_STATE, TI_CMD_CODE_STACK_UP, 0);

	/* Enable host interrupts. */
	CSR_WRITE_4(sc, TI_MB_HOSTINTR, 0);

	ifp->if_drv_flags |= IFF_DRV_RUNNING;
	ifp->if_drv_flags &= ~IFF_DRV_OACTIVE;
	callout_reset(&sc->ti_watchdog, hz, ti_watchdog, sc);

	/*
	 * Make sure to set media properly. We have to do this
	 * here since we have to issue commands in order to set
	 * the link negotiation and we can't issue commands until
	 * the firmware is running.
	 */
	ifm = &sc->ifmedia;
	tmp = ifm->ifm_media;
	ifm->ifm_media = ifm->ifm_cur->ifm_media;
	ti_ifmedia_upd_locked(sc);
	ifm->ifm_media = tmp;
}

/*
 * Set media options.
 */
static int
ti_ifmedia_upd(struct ifnet *ifp)
{
	struct ti_softc *sc;
	int error;

	sc = ifp->if_softc;
	TI_LOCK(sc);
	error = ti_ifmedia_upd_locked(sc);
	TI_UNLOCK(sc);

	return (error);
}

static int
ti_ifmedia_upd_locked(struct ti_softc *sc)
{
	struct ifmedia *ifm;
	struct ti_cmd_desc cmd;
	uint32_t flowctl;

	ifm = &sc->ifmedia;

	if (IFM_TYPE(ifm->ifm_media) != IFM_ETHER)
		return (EINVAL);

	flowctl = 0;

	switch (IFM_SUBTYPE(ifm->ifm_media)) {
	case IFM_AUTO:
		/*
		 * Transmit flow control doesn't work on the Tigon 1.
		 */
		flowctl = TI_GLNK_RX_FLOWCTL_Y;

		/*
		 * Transmit flow control can also cause problems on the
		 * Tigon 2, apparently with both the copper and fiber
		 * boards.  The symptom is that the interface will just
		 * hang.  This was reproduced with Alteon 180 switches.
		 */
#if 0
		if (sc->ti_hwrev != TI_HWREV_TIGON)
			flowctl |= TI_GLNK_TX_FLOWCTL_Y;
#endif

		CSR_WRITE_4(sc, TI_GCR_GLINK, TI_GLNK_PREF|TI_GLNK_1000MB|
		    TI_GLNK_FULL_DUPLEX| flowctl |
		    TI_GLNK_AUTONEGENB|TI_GLNK_ENB);

		flowctl = TI_LNK_RX_FLOWCTL_Y;
#if 0
		if (sc->ti_hwrev != TI_HWREV_TIGON)
			flowctl |= TI_LNK_TX_FLOWCTL_Y;
#endif

		CSR_WRITE_4(sc, TI_GCR_LINK, TI_LNK_100MB|TI_LNK_10MB|
		    TI_LNK_FULL_DUPLEX|TI_LNK_HALF_DUPLEX| flowctl |
		    TI_LNK_AUTONEGENB|TI_LNK_ENB);
		TI_DO_CMD(TI_CMD_LINK_NEGOTIATION,
		    TI_CMD_CODE_NEGOTIATE_BOTH, 0);
		break;
	case IFM_1000_SX:
	case IFM_1000_T:
		flowctl = TI_GLNK_RX_FLOWCTL_Y;
#if 0
		if (sc->ti_hwrev != TI_HWREV_TIGON)
			flowctl |= TI_GLNK_TX_FLOWCTL_Y;
#endif

		CSR_WRITE_4(sc, TI_GCR_GLINK, TI_GLNK_PREF|TI_GLNK_1000MB|
		    flowctl |TI_GLNK_ENB);
		CSR_WRITE_4(sc, TI_GCR_LINK, 0);
		if ((ifm->ifm_media & IFM_GMASK) == IFM_FDX) {
			TI_SETBIT(sc, TI_GCR_GLINK, TI_GLNK_FULL_DUPLEX);
		}
		TI_DO_CMD(TI_CMD_LINK_NEGOTIATION,
		    TI_CMD_CODE_NEGOTIATE_GIGABIT, 0);
		break;
	case IFM_100_FX:
	case IFM_10_FL:
	case IFM_100_TX:
	case IFM_10_T:
		flowctl = TI_LNK_RX_FLOWCTL_Y;
#if 0
		if (sc->ti_hwrev != TI_HWREV_TIGON)
			flowctl |= TI_LNK_TX_FLOWCTL_Y;
#endif

		CSR_WRITE_4(sc, TI_GCR_GLINK, 0);
		CSR_WRITE_4(sc, TI_GCR_LINK, TI_LNK_ENB|TI_LNK_PREF|flowctl);
		if (IFM_SUBTYPE(ifm->ifm_media) == IFM_100_FX ||
		    IFM_SUBTYPE(ifm->ifm_media) == IFM_100_TX) {
			TI_SETBIT(sc, TI_GCR_LINK, TI_LNK_100MB);
		} else {
			TI_SETBIT(sc, TI_GCR_LINK, TI_LNK_10MB);
		}
		if ((ifm->ifm_media & IFM_GMASK) == IFM_FDX) {
			TI_SETBIT(sc, TI_GCR_LINK, TI_LNK_FULL_DUPLEX);
		} else {
			TI_SETBIT(sc, TI_GCR_LINK, TI_LNK_HALF_DUPLEX);
		}
		TI_DO_CMD(TI_CMD_LINK_NEGOTIATION,
		    TI_CMD_CODE_NEGOTIATE_10_100, 0);
		break;
	}

	return (0);
}

/*
 * Report current media status.
 */
static void
ti_ifmedia_sts(struct ifnet *ifp, struct ifmediareq *ifmr)
{
	struct ti_softc *sc;
	uint32_t media = 0;

	sc = ifp->if_softc;

	TI_LOCK(sc);

	ifmr->ifm_status = IFM_AVALID;
	ifmr->ifm_active = IFM_ETHER;

	if (sc->ti_linkstat == TI_EV_CODE_LINK_DOWN) {
		TI_UNLOCK(sc);
		return;
	}

	ifmr->ifm_status |= IFM_ACTIVE;

	if (sc->ti_linkstat == TI_EV_CODE_GIG_LINK_UP) {
		media = CSR_READ_4(sc, TI_GCR_GLINK_STAT);
		if (sc->ti_copper)
			ifmr->ifm_active |= IFM_1000_T;
		else
			ifmr->ifm_active |= IFM_1000_SX;
		if (media & TI_GLNK_FULL_DUPLEX)
			ifmr->ifm_active |= IFM_FDX;
		else
			ifmr->ifm_active |= IFM_HDX;
	} else if (sc->ti_linkstat == TI_EV_CODE_LINK_UP) {
		media = CSR_READ_4(sc, TI_GCR_LINK_STAT);
		if (sc->ti_copper) {
			if (media & TI_LNK_100MB)
				ifmr->ifm_active |= IFM_100_TX;
			if (media & TI_LNK_10MB)
				ifmr->ifm_active |= IFM_10_T;
		} else {
			if (media & TI_LNK_100MB)
				ifmr->ifm_active |= IFM_100_FX;
			if (media & TI_LNK_10MB)
				ifmr->ifm_active |= IFM_10_FL;
		}
		if (media & TI_LNK_FULL_DUPLEX)
			ifmr->ifm_active |= IFM_FDX;
		if (media & TI_LNK_HALF_DUPLEX)
			ifmr->ifm_active |= IFM_HDX;
	}
	TI_UNLOCK(sc);
}

static int
ti_ioctl(struct ifnet *ifp, u_long command, caddr_t data)
{
	struct ti_softc *sc = ifp->if_softc;
	struct ifreq *ifr = (struct ifreq *) data;
	struct ti_cmd_desc cmd;
	int mask, error = 0;

	switch (command) {
	case SIOCSIFMTU:
		TI_LOCK(sc);
		if (ifr->ifr_mtu < ETHERMIN || ifr->ifr_mtu > TI_JUMBO_MTU)
			error = EINVAL;
		else {
			ifp->if_mtu = ifr->ifr_mtu;
			if (ifp->if_drv_flags & IFF_DRV_RUNNING) {
				ifp->if_drv_flags &= ~IFF_DRV_RUNNING;
				ti_init_locked(sc);
			}
		}
		TI_UNLOCK(sc);
		break;
	case SIOCSIFFLAGS:
		TI_LOCK(sc);
		if (ifp->if_flags & IFF_UP) {
			/*
			 * If only the state of the PROMISC flag changed,
			 * then just use the 'set promisc mode' command
			 * instead of reinitializing the entire NIC. Doing
			 * a full re-init means reloading the firmware and
			 * waiting for it to start up, which may take a
			 * second or two.
			 */
			if (ifp->if_drv_flags & IFF_DRV_RUNNING &&
			    ifp->if_flags & IFF_PROMISC &&
			    !(sc->ti_if_flags & IFF_PROMISC)) {
				TI_DO_CMD(TI_CMD_SET_PROMISC_MODE,
				    TI_CMD_CODE_PROMISC_ENB, 0);
			} else if (ifp->if_drv_flags & IFF_DRV_RUNNING &&
			    !(ifp->if_flags & IFF_PROMISC) &&
			    sc->ti_if_flags & IFF_PROMISC) {
				TI_DO_CMD(TI_CMD_SET_PROMISC_MODE,
				    TI_CMD_CODE_PROMISC_DIS, 0);
			} else
				ti_init_locked(sc);
		} else {
			if (ifp->if_drv_flags & IFF_DRV_RUNNING) {
				ti_stop(sc);
			}
		}
		sc->ti_if_flags = ifp->if_flags;
		TI_UNLOCK(sc);
		break;
	case SIOCADDMULTI:
	case SIOCDELMULTI:
		TI_LOCK(sc);
		if (ifp->if_drv_flags & IFF_DRV_RUNNING)
			ti_setmulti(sc);
		TI_UNLOCK(sc);
		break;
	case SIOCSIFMEDIA:
	case SIOCGIFMEDIA:
		error = ifmedia_ioctl(ifp, ifr, &sc->ifmedia, command);
		break;
	case SIOCSIFCAP:
		TI_LOCK(sc);
		mask = ifr->ifr_reqcap ^ ifp->if_capenable;
		if ((mask & IFCAP_TXCSUM) != 0 &&
		    (ifp->if_capabilities & IFCAP_TXCSUM) != 0) {
			ifp->if_capenable ^= IFCAP_TXCSUM;
			if ((ifp->if_capenable & IFCAP_TXCSUM) != 0)
				ifp->if_hwassist |= TI_CSUM_FEATURES;
                        else
				ifp->if_hwassist &= ~TI_CSUM_FEATURES;
                }
		if ((mask & IFCAP_RXCSUM) != 0 &&
		    (ifp->if_capabilities & IFCAP_RXCSUM) != 0)
			ifp->if_capenable ^= IFCAP_RXCSUM;
		if ((mask & IFCAP_VLAN_HWTAGGING) != 0 &&
		    (ifp->if_capabilities & IFCAP_VLAN_HWTAGGING) != 0)
                        ifp->if_capenable ^= IFCAP_VLAN_HWTAGGING;
		if ((mask & IFCAP_VLAN_HWCSUM) != 0 &&
		    (ifp->if_capabilities & IFCAP_VLAN_HWCSUM) != 0)
			ifp->if_capenable ^= IFCAP_VLAN_HWCSUM;
		if ((mask & (IFCAP_TXCSUM | IFCAP_RXCSUM |
		    IFCAP_VLAN_HWTAGGING)) != 0) {
			if (ifp->if_drv_flags & IFF_DRV_RUNNING) {
				ifp->if_drv_flags &= ~IFF_DRV_RUNNING;
				ti_init_locked(sc);
			}
		}
		TI_UNLOCK(sc);
		VLAN_CAPABILITIES(ifp);
		break;
	default:
		error = ether_ioctl(ifp, command, data);
		break;
	}

	return (error);
}

static int
ti_open(struct cdev *dev, int flags, int fmt, struct thread *td)
{
	struct ti_softc *sc;

	sc = dev->si_drv1;
	if (sc == NULL)
		return (ENODEV);

	TI_LOCK(sc);
	sc->ti_flags |= TI_FLAG_DEBUGING;
	TI_UNLOCK(sc);

	return (0);
}

static int
ti_close(struct cdev *dev, int flag, int fmt, struct thread *td)
{
	struct ti_softc *sc;

	sc = dev->si_drv1;
	if (sc == NULL)
		return (ENODEV);

	TI_LOCK(sc);
	sc->ti_flags &= ~TI_FLAG_DEBUGING;
	TI_UNLOCK(sc);

	return (0);
}

/*
 * This ioctl routine goes along with the Tigon character device.
 */
static int
ti_ioctl2(struct cdev *dev, u_long cmd, caddr_t addr, int flag,
    struct thread *td)
{
	struct ti_softc *sc;
	int error;

	sc = dev->si_drv1;
	if (sc == NULL)
		return (ENODEV);

	error = 0;

	switch (cmd) {
	case TIIOCGETSTATS:
	{
		struct ti_stats *outstats;

		outstats = (struct ti_stats *)addr;

		TI_LOCK(sc);
		bus_dmamap_sync(sc->ti_cdata.ti_gib_tag,
		    sc->ti_cdata.ti_gib_map, BUS_DMASYNC_POSTREAD);
		bcopy(&sc->ti_rdata.ti_info->ti_stats, outstats,
		    sizeof(struct ti_stats));
		bus_dmamap_sync(sc->ti_cdata.ti_gib_tag,
		    sc->ti_cdata.ti_gib_map, BUS_DMASYNC_PREREAD);
		TI_UNLOCK(sc);
		break;
	}
	case TIIOCGETPARAMS:
	{
		struct ti_params *params;

		params = (struct ti_params *)addr;

		TI_LOCK(sc);
		params->ti_stat_ticks = sc->ti_stat_ticks;
		params->ti_rx_coal_ticks = sc->ti_rx_coal_ticks;
		params->ti_tx_coal_ticks = sc->ti_tx_coal_ticks;
		params->ti_rx_max_coal_bds = sc->ti_rx_max_coal_bds;
		params->ti_tx_max_coal_bds = sc->ti_tx_max_coal_bds;
		params->ti_tx_buf_ratio = sc->ti_tx_buf_ratio;
		params->param_mask = TI_PARAM_ALL;
		TI_UNLOCK(sc);
		break;
	}
	case TIIOCSETPARAMS:
	{
		struct ti_params *params;

		params = (struct ti_params *)addr;

		TI_LOCK(sc);
		if (params->param_mask & TI_PARAM_STAT_TICKS) {
			sc->ti_stat_ticks = params->ti_stat_ticks;
			CSR_WRITE_4(sc, TI_GCR_STAT_TICKS, sc->ti_stat_ticks);
		}

		if (params->param_mask & TI_PARAM_RX_COAL_TICKS) {
			sc->ti_rx_coal_ticks = params->ti_rx_coal_ticks;
			CSR_WRITE_4(sc, TI_GCR_RX_COAL_TICKS,
				    sc->ti_rx_coal_ticks);
		}

		if (params->param_mask & TI_PARAM_TX_COAL_TICKS) {
			sc->ti_tx_coal_ticks = params->ti_tx_coal_ticks;
			CSR_WRITE_4(sc, TI_GCR_TX_COAL_TICKS,
				    sc->ti_tx_coal_ticks);
		}

		if (params->param_mask & TI_PARAM_RX_COAL_BDS) {
			sc->ti_rx_max_coal_bds = params->ti_rx_max_coal_bds;
			CSR_WRITE_4(sc, TI_GCR_RX_MAX_COAL_BD,
				    sc->ti_rx_max_coal_bds);
		}

		if (params->param_mask & TI_PARAM_TX_COAL_BDS) {
			sc->ti_tx_max_coal_bds = params->ti_tx_max_coal_bds;
			CSR_WRITE_4(sc, TI_GCR_TX_MAX_COAL_BD,
				    sc->ti_tx_max_coal_bds);
		}

		if (params->param_mask & TI_PARAM_TX_BUF_RATIO) {
			sc->ti_tx_buf_ratio = params->ti_tx_buf_ratio;
			CSR_WRITE_4(sc, TI_GCR_TX_BUFFER_RATIO,
				    sc->ti_tx_buf_ratio);
		}
		TI_UNLOCK(sc);
		break;
	}
	case TIIOCSETTRACE: {
		ti_trace_type trace_type;

		trace_type = *(ti_trace_type *)addr;

		/*
		 * Set tracing to whatever the user asked for.  Setting
		 * this register to 0 should have the effect of disabling
		 * tracing.
		 */
		TI_LOCK(sc);
		CSR_WRITE_4(sc, TI_GCR_NIC_TRACING, trace_type);
		TI_UNLOCK(sc);
		break;
	}
	case TIIOCGETTRACE: {
		struct ti_trace_buf *trace_buf;
		uint32_t trace_start, cur_trace_ptr, trace_len;

		trace_buf = (struct ti_trace_buf *)addr;

		TI_LOCK(sc);
		trace_start = CSR_READ_4(sc, TI_GCR_NICTRACE_START);
		cur_trace_ptr = CSR_READ_4(sc, TI_GCR_NICTRACE_PTR);
		trace_len = CSR_READ_4(sc, TI_GCR_NICTRACE_LEN);
#if 0
		if_printf(sc->ti_ifp, "trace_start = %#x, cur_trace_ptr = %#x, "
		       "trace_len = %d\n", trace_start,
		       cur_trace_ptr, trace_len);
		if_printf(sc->ti_ifp, "trace_buf->buf_len = %d\n",
		       trace_buf->buf_len);
#endif
		error = ti_copy_mem(sc, trace_start, min(trace_len,
		    trace_buf->buf_len), (caddr_t)trace_buf->buf, 1, 1);
		if (error == 0) {
			trace_buf->fill_len = min(trace_len,
			    trace_buf->buf_len);
			if (cur_trace_ptr < trace_start)
				trace_buf->cur_trace_ptr =
				    trace_start - cur_trace_ptr;
			else
				trace_buf->cur_trace_ptr =
				    cur_trace_ptr - trace_start;
		} else
			trace_buf->fill_len = 0;
		TI_UNLOCK(sc);
		break;
	}

	/*
	 * For debugging, five ioctls are needed:
	 * ALT_ATTACH
	 * ALT_READ_TG_REG
	 * ALT_WRITE_TG_REG
	 * ALT_READ_TG_MEM
	 * ALT_WRITE_TG_MEM
	 */
	case ALT_ATTACH:
		/*
		 * From what I can tell, Alteon's Solaris Tigon driver
		 * only has one character device, so you have to attach
		 * to the Tigon board you're interested in.  This seems
		 * like a not-so-good way to do things, since unless you
		 * subsequently specify the unit number of the device
		 * you're interested in every ioctl, you'll only be
		 * able to debug one board at a time.
		 */
		break;
	case ALT_READ_TG_MEM:
	case ALT_WRITE_TG_MEM:
	{
		struct tg_mem *mem_param;
		uint32_t sram_end, scratch_end;

		mem_param = (struct tg_mem *)addr;

		if (sc->ti_hwrev == TI_HWREV_TIGON) {
			sram_end = TI_END_SRAM_I;
			scratch_end = TI_END_SCRATCH_I;
		} else {
			sram_end = TI_END_SRAM_II;
			scratch_end = TI_END_SCRATCH_II;
		}

		/*
		 * For now, we'll only handle accessing regular SRAM,
		 * nothing else.
		 */
		TI_LOCK(sc);
		if (mem_param->tgAddr >= TI_BEG_SRAM &&
		    mem_param->tgAddr + mem_param->len <= sram_end) {
			/*
			 * In this instance, we always copy to/from user
			 * space, so the user space argument is set to 1.
			 */
			error = ti_copy_mem(sc, mem_param->tgAddr,
			    mem_param->len, mem_param->userAddr, 1,
			    cmd == ALT_READ_TG_MEM ? 1 : 0);
		} else if (mem_param->tgAddr >= TI_BEG_SCRATCH &&
		    mem_param->tgAddr <= scratch_end) {
			error = ti_copy_scratch(sc, mem_param->tgAddr,
			    mem_param->len, mem_param->userAddr, 1,
			    cmd == ALT_READ_TG_MEM ?  1 : 0, TI_PROCESSOR_A);
		} else if (mem_param->tgAddr >= TI_BEG_SCRATCH_B_DEBUG &&
		    mem_param->tgAddr <= TI_BEG_SCRATCH_B_DEBUG) {
			if (sc->ti_hwrev == TI_HWREV_TIGON) {
				if_printf(sc->ti_ifp,
				    "invalid memory range for Tigon I\n");
				error = EINVAL;
				break;
			}
			error = ti_copy_scratch(sc, mem_param->tgAddr -
			    TI_SCRATCH_DEBUG_OFF, mem_param->len,
			    mem_param->userAddr, 1,
			    cmd == ALT_READ_TG_MEM ? 1 : 0, TI_PROCESSOR_B);
		} else {
			if_printf(sc->ti_ifp, "memory address %#x len %d is "
			        "out of supported range\n",
			        mem_param->tgAddr, mem_param->len);
			error = EINVAL;
		}
		TI_UNLOCK(sc);
		break;
	}
	case ALT_READ_TG_REG:
	case ALT_WRITE_TG_REG:
	{
		struct tg_reg *regs;
		uint32_t tmpval;

		regs = (struct tg_reg *)addr;

		/*
		 * Make sure the address in question isn't out of range.
		 */
		if (regs->addr > TI_REG_MAX) {
			error = EINVAL;
			break;
		}
		TI_LOCK(sc);
		if (cmd == ALT_READ_TG_REG) {
			bus_space_read_region_4(sc->ti_btag, sc->ti_bhandle,
			    regs->addr, &tmpval, 1);
			regs->data = ntohl(tmpval);
#if 0
			if ((regs->addr == TI_CPU_STATE)
			 || (regs->addr == TI_CPU_CTL_B)) {
				if_printf(sc->ti_ifp, "register %#x = %#x\n",
				       regs->addr, tmpval);
			}
#endif
		} else {
			tmpval = htonl(regs->data);
			bus_space_write_region_4(sc->ti_btag, sc->ti_bhandle,
			    regs->addr, &tmpval, 1);
		}
		TI_UNLOCK(sc);
		break;
	}
	default:
		error = ENOTTY;
		break;
	}
	return (error);
}

static void
ti_watchdog(void *arg)
{
	struct ti_softc *sc;
	struct ifnet *ifp;

	sc = arg;
	TI_LOCK_ASSERT(sc);
	callout_reset(&sc->ti_watchdog, hz, ti_watchdog, sc);
	if (sc->ti_timer == 0 || --sc->ti_timer > 0)
		return;

	/*
	 * When we're debugging, the chip is often stopped for long periods
	 * of time, and that would normally cause the watchdog timer to fire.
	 * Since that impedes debugging, we don't want to do that.
	 */
	if (sc->ti_flags & TI_FLAG_DEBUGING)
		return;

	ifp = sc->ti_ifp;
	if_printf(ifp, "watchdog timeout -- resetting\n");
	ifp->if_drv_flags &= ~IFF_DRV_RUNNING;
	ti_init_locked(sc);

	if_inc_counter(ifp, IFCOUNTER_OERRORS, 1);
}

/*
 * Stop the adapter and free any mbufs allocated to the
 * RX and TX lists.
 */
static void
ti_stop(struct ti_softc *sc)
{
	struct ifnet *ifp;
	struct ti_cmd_desc cmd;

	TI_LOCK_ASSERT(sc);

	ifp = sc->ti_ifp;

	/* Disable host interrupts. */
	CSR_WRITE_4(sc, TI_MB_HOSTINTR, 1);
	/*
	 * Tell firmware we're shutting down.
	 */
	TI_DO_CMD(TI_CMD_HOST_STATE, TI_CMD_CODE_STACK_DOWN, 0);

	/* Halt and reinitialize. */
	if (ti_chipinit(sc) == 0) {
		ti_mem_zero(sc, 0x2000, 0x100000 - 0x2000);
		/* XXX ignore init errors. */
		ti_chipinit(sc);
	}

	/* Free the RX lists. */
	ti_free_rx_ring_std(sc);

	/* Free jumbo RX list. */
	ti_free_rx_ring_jumbo(sc);

	/* Free mini RX list. */
	ti_free_rx_ring_mini(sc);

	/* Free TX buffers. */
	ti_free_tx_ring(sc);

	sc->ti_ev_prodidx.ti_idx = 0;
	sc->ti_return_prodidx.ti_idx = 0;
	sc->ti_tx_considx.ti_idx = 0;
	sc->ti_tx_saved_considx = TI_TXCONS_UNSET;

	ifp->if_drv_flags &= ~(IFF_DRV_RUNNING | IFF_DRV_OACTIVE);
	callout_stop(&sc->ti_watchdog);
}

/*
 * Stop all chip I/O so that the kernel's probe routines don't
 * get confused by errant DMAs when rebooting.
 */
static int
ti_shutdown(device_t dev)
{
	struct ti_softc *sc;

	sc = device_get_softc(dev);
	TI_LOCK(sc);
	ti_chipinit(sc);
	TI_UNLOCK(sc);

	return (0);
}

static void
ti_sysctl_node(struct ti_softc *sc)
{
	struct sysctl_ctx_list *ctx;
	struct sysctl_oid_list *child;
	char tname[32];

	ctx = device_get_sysctl_ctx(sc->ti_dev);
	child = SYSCTL_CHILDREN(device_get_sysctl_tree(sc->ti_dev));

	/* Use DAC */
	sc->ti_dac = 1;
	snprintf(tname, sizeof(tname), "dev.ti.%d.dac",
	    device_get_unit(sc->ti_dev));
	TUNABLE_INT_FETCH(tname, &sc->ti_dac);

	SYSCTL_ADD_UINT(ctx, child, OID_AUTO, "rx_coal_ticks", CTLFLAG_RW,
	    &sc->ti_rx_coal_ticks, 0, "Receive coalcesced ticks");
	SYSCTL_ADD_UINT(ctx, child, OID_AUTO, "rx_max_coal_bds", CTLFLAG_RW,
	    &sc->ti_rx_max_coal_bds, 0, "Receive max coalcesced BDs");

	SYSCTL_ADD_UINT(ctx, child, OID_AUTO, "tx_coal_ticks", CTLFLAG_RW,
	    &sc->ti_tx_coal_ticks, 0, "Send coalcesced ticks");
	SYSCTL_ADD_UINT(ctx, child, OID_AUTO, "tx_max_coal_bds", CTLFLAG_RW,
	    &sc->ti_tx_max_coal_bds, 0, "Send max coalcesced BDs");
	SYSCTL_ADD_UINT(ctx, child, OID_AUTO, "tx_buf_ratio", CTLFLAG_RW,
	    &sc->ti_tx_buf_ratio, 0,
	    "Ratio of NIC memory devoted to TX buffer");

	SYSCTL_ADD_UINT(ctx, child, OID_AUTO, "stat_ticks", CTLFLAG_RW,
	    &sc->ti_stat_ticks, 0,
	    "Number of clock ticks for statistics update interval");

	/* Pull in device tunables. */
	sc->ti_rx_coal_ticks = 170;
	resource_int_value(device_get_name(sc->ti_dev),
	    device_get_unit(sc->ti_dev), "rx_coal_ticks",
	    &sc->ti_rx_coal_ticks);
	sc->ti_rx_max_coal_bds = 64;
	resource_int_value(device_get_name(sc->ti_dev),
	    device_get_unit(sc->ti_dev), "rx_max_coal_bds",
	    &sc->ti_rx_max_coal_bds);

	sc->ti_tx_coal_ticks = TI_TICKS_PER_SEC / 500;
	resource_int_value(device_get_name(sc->ti_dev),
	    device_get_unit(sc->ti_dev), "tx_coal_ticks",
	    &sc->ti_tx_coal_ticks);
	sc->ti_tx_max_coal_bds = 32;
	resource_int_value(device_get_name(sc->ti_dev),
	    device_get_unit(sc->ti_dev), "tx_max_coal_bds",
	    &sc->ti_tx_max_coal_bds);
	sc->ti_tx_buf_ratio = 21;
	resource_int_value(device_get_name(sc->ti_dev),
	    device_get_unit(sc->ti_dev), "tx_buf_ratio",
	    &sc->ti_tx_buf_ratio);

	sc->ti_stat_ticks = 2 * TI_TICKS_PER_SEC;
	resource_int_value(device_get_name(sc->ti_dev),
	    device_get_unit(sc->ti_dev), "stat_ticks",
	    &sc->ti_stat_ticks);
}
