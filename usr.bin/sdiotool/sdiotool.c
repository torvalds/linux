/*-
 * Copyright (c) 2016-2017 Ilya Bakulin
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
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * Copyright (c) 2010 Broadcom Corporation
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY
 * SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION
 * OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN
 * CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 *
 * $FreeBSD$
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/ioctl.h>
#include <sys/stdint.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/endian.h>
#include <sys/sbuf.h>
#include <sys/mman.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <inttypes.h>
#include <limits.h>
#include <fcntl.h>
#include <ctype.h>
#include <err.h>
#include <libutil.h>
#include <unistd.h>

#include <cam/cam.h>
#include <cam/cam_debug.h>
#include <cam/cam_ccb.h>
#include <cam/mmc/mmc_all.h>
#include <camlib.h>

#include "linux_compat.h"
#include "linux_sdio_compat.h"
#include "cam_sdio.h"
#include "brcmfmac_sdio.h"
#include "brcmfmac_bus.h"

static void probe_bcrm(struct cam_device *dev);

/*
 * How Linux driver works
 *
 * The probing begins by calling brcmf_ops_sdio_probe() which is defined as probe function in struct sdio_driver. http://lxr.free-electrons.com/source/drivers/net/wireless/broadcom/brcm80211/brcmfmac/bcmsdh.c#L1126
 *
 * The driver does black magic by copying func struct for F2 and setting func number to zero there, to create an F0 func structure :)
 * Driver state changes to BRCMF_SDIOD_DOWN.
 * ops_sdio_probe() then calls brcmf_sdio_probe() -- at this point it has filled in sdiodev struct with the pointers to all three functions (F0, F1, F2).
 *
 * brcmf_sdiod_probe() sets block sizes for F1 and F2. It sets F1 block size to 64 and F2 to 512, not consulting the values stored in SDIO CCCR  / FBR registers!
 * Then it increases timeout for F2 (what is this?!)
 * Then it enables F1
 * Then it attaches "freezer" (without PM this is NOP)
 * Finally it calls brcmf_sdio_probe() http://lxr.free-electrons.com/source/drivers/net/wireless/broadcom/brcm80211/brcmfmac/sdio.c#L4082
 *
 * Here high-level workqueues and sg tables are allocated.
 * It then calls brcmf_sdio_probe_attach()
 *
 * Here at the beginning there is a pr_debug() call with brcmf_sdiod_regrl() inside to addr #define SI_ENUM_BASE            0x18000000.
 * Return value is 0x16044330.
 * Then turns off PLL:  byte-write BRCMF_INIT_CLKCTL1 (0x28) ->  SBSDIO_FUNC1_CHIPCLKCSR (0x1000E)
 * Then it reads value back, should be 0xe8.
 * Then calls brcmf_chip_attach()
 *
 * http://lxr.free-electrons.com/source/drivers/net/wireless/broadcom/brcm80211/brcmfmac/chip.c#L1054
 * This func enumerates and resets all the cores on the dongle.
 *  - brcmf_sdio_buscoreprep(): force clock to ALPAvail req only:
 *    SBSDIO_FORCE_HW_CLKREQ_OFF | SBSDIO_ALP_AVAIL_REQ -> SBSDIO_FUNC1_CHIPCLKCSR
 * Wait up to 15ms to !SBSDIO_ALPAV(clkval) of the value from CLKCSR.
 * Force ALP:
 *    SBSDIO_FORCE_HW_CLKREQ_OFF | SBSDIO_FORCE_ALP (0x21)-> SBSDIO_FUNC1_CHIPCLKCSR
 * Disaable SDIO pullups:
 * byte 0 -> SBSDIO_FUNC1_SDIOPULLUP (0x0001000f)
 *
 *  Calls brcmf_chip_recognition()
 * http://lxr.free-electrons.com/source/drivers/net/wireless/broadcom/brcm80211/brcmfmac/chip.c#L908
 * Read 0x18000000. Get 0x16044330: chip 4330 rev 4
 * AXI chip, call  brcmf_chip_dmp_erom_scan() to get info about all cores.
 * Then  brcmf_chip_cores_check() to check that CPU and RAM are found,
 *
 * Setting cores to passive: not clear which of CR4/CA7/CM3 our chip has.
 *  Quite a few r/w calls to different parts of the chip to reset cores....
 * Finally get_raminfo() called to fill in RAM info:
 * brcmf_chip_get_raminfo: RAM: base=0x0 size=294912 (0x48000) sr=0 (0x0)
 * http://lxr.free-electrons.com/source/drivers/net/wireless/broadcom/brcm80211/brcmfmac/chip.c#L700
 *
 * Then brcmf_chip_setup() is called, this prints and fills in chipcommon rev and PMU caps:
 *   brcmf_chip_setup: ccrev=39, pmurev=12, pmucaps=0x19583c0c
 * http://lxr.free-electrons.com/source/drivers/net/wireless/broadcom/brcm80211/brcmfmac/chip.c#L1015
 *  Bus-specific setup code is NOP for SDIO.
 *
 * brcmf_sdio_kso_init() is called.
 * Here it first reads 0x1 from SBSDIO_FUNC1_SLEEPCSR 0x18000650 and then writes it back... WTF?
 *
 * brcmf_sdio_drivestrengthinit() is called
 * http://lxr.free-electrons.com/source/drivers/net/wireless/broadcom/brcm80211/brcmfmac/sdio.c#L3630
 *
 * Set card control so an SDIO card reset does a WLAN backplane reset
 * set PMUControl so a backplane reset does PMU state reload
 * === end of brcmf_sdio_probe_attach ===

 **** Finished reading at http://lxr.free-electrons.com/source/drivers/net/wireless/broadcom/brcm80211/brcmfmac/sdio.c#L4152, line 2025 in the dump

 * === How register reading works ===
 * http://lxr.free-electrons.com/source/drivers/net/wireless/broadcom/brcm80211/brcmfmac/bcmsdh.c#L357
 * The address to read from is written to three byte-sized registers of F1:
 *  - SBSDIO_FUNC1_SBADDRLOW  0x1000A
 *  - SBSDIO_FUNC1_SBADDRMID  0x1000B
 *  - SBSDIO_FUNC1_SBADDRHIGH 0x1000C
 * If this is 32-bit read , a flag is set. The address is ANDed with SBSDIO_SB_OFT_ADDR_MASK which is 0x07FFF.
 * Then brcmf_sdiod_regrw_helper() is called to read the reply.
 * http://lxr.free-electrons.com/source/drivers/net/wireless/broadcom/brcm80211/brcmfmac/bcmsdh.c#L306
 * Based on the address it figures out where to read it from (CCCR / FBR in F0, or somewhere in F1).
 * Reads are retried three times.
 * 1-byte IO is done with CMD52, more is read with CMD53 with address increment (not FIFO mode).
 * http://lxr.free-electrons.com/source/drivers/mmc/core/sdio_io.c#L458
 * ==================================
 *
 *
 */

/* BRCM-specific functions */
#define SDIOH_API_ACCESS_RETRY_LIMIT	2
#define SI_ENUM_BASE            0x18000000
#define REPLY_MAGIC             0x16044330
#define brcmf_err(fmt, ...) brcmf_dbg(0, fmt, ##__VA_ARGS__)
#define brcmf_dbg(level, fmt, ...) printf(fmt, ##__VA_ARGS__)

struct brcmf_sdio_dev {
	struct cam_device *cam_dev;
	u32 sbwad;			/* Save backplane window address */
	struct brcmf_bus *bus_if;
	enum brcmf_sdiod_state state;
	struct sdio_func *func[8];
};

void brcmf_bus_change_state(struct brcmf_bus *bus, enum brcmf_bus_state state);
void brcmf_sdiod_change_state(struct brcmf_sdio_dev *sdiodev,
			      enum brcmf_sdiod_state state);
static int brcmf_sdiod_request_data(struct brcmf_sdio_dev *sdiodev, u8 fn, u32 addr,
				    u8 regsz, void *data, bool write);
static int brcmf_sdiod_set_sbaddr_window(struct brcmf_sdio_dev *sdiodev, u32 address);
static int brcmf_sdiod_addrprep(struct brcmf_sdio_dev *sdiodev, uint width, u32 *addr);
u32 brcmf_sdiod_regrl(struct brcmf_sdio_dev *sdiodev, u32 addr, int *ret);

static void bailout(int ret);

static void
bailout(int ret) {
	if (ret == 0)
		return;
	errx(1, "Operation returned error %d", ret);
}

void
brcmf_bus_change_state(struct brcmf_bus *bus, enum brcmf_bus_state state)
{
	bus->state = state;
}

void brcmf_sdiod_change_state(struct brcmf_sdio_dev *sdiodev,
			      enum brcmf_sdiod_state state)
{
	if (sdiodev->state == BRCMF_SDIOD_NOMEDIUM ||
	    state == sdiodev->state)
		return;

	//brcmf_dbg(TRACE, "%d -> %d\n", sdiodev->state, state);
	switch (sdiodev->state) {
	case BRCMF_SDIOD_DATA:
		/* any other state means bus interface is down */
		brcmf_bus_change_state(sdiodev->bus_if, BRCMF_BUS_DOWN);
		break;
	case BRCMF_SDIOD_DOWN:
		/* transition from DOWN to DATA means bus interface is up */
		if (state == BRCMF_SDIOD_DATA)
			brcmf_bus_change_state(sdiodev->bus_if, BRCMF_BUS_UP);
		break;
	default:
		break;
	}
	sdiodev->state = state;
}

static inline int brcmf_sdiod_f0_writeb(struct sdio_func *func,
					uint regaddr, u8 byte) {
	int err_ret;

	/*
	 * Can only directly write to some F0 registers.
	 * Handle CCCR_IENx and CCCR_ABORT command
	 * as a special case.
	 */
	if ((regaddr == SDIO_CCCR_ABORT) ||
	    (regaddr == SDIO_CCCR_IENx))
		sdio_writeb(func, byte, regaddr, &err_ret);
	else
		sdio_f0_writeb(func, byte, regaddr, &err_ret);

	return err_ret;
}

static int brcmf_sdiod_request_data(struct brcmf_sdio_dev *sdiodev, u8 fn, u32 addr, u8 regsz, void *data, bool write)
{
	struct sdio_func *func;
	int ret = -EINVAL;

	brcmf_dbg(SDIO, "rw=%d, func=%d, addr=0x%05x, nbytes=%d\n",
		  write, fn, addr, regsz);

	/* only allow byte access on F0 */
	if (WARN_ON(regsz > 1 && !fn))
		return -EINVAL;
	func = sdiodev->func[fn];

	switch (regsz) {
	case sizeof(u8):
		if (write) {
			if (fn)
				sdio_writeb(func, *(u8 *)data, addr, &ret);
			else
				ret = brcmf_sdiod_f0_writeb(func, addr,
							    *(u8 *)data);
		} else {
			if (fn)
				*(u8 *)data = sdio_readb(func, addr, &ret);
			else
				*(u8 *)data = sdio_f0_readb(func, addr, &ret);
		}
		break;
	case sizeof(u16):
		if (write)
			sdio_writew(func, *(u16 *)data, addr, &ret);
		else
			*(u16 *)data = sdio_readw(func, addr, &ret);
		break;
	case sizeof(u32):
		if (write)
			sdio_writel(func, *(u32 *)data, addr, &ret);
		else
			*(u32 *)data = sdio_readl(func, addr, &ret);
		break;
	default:
		brcmf_err("invalid size: %d\n", regsz);
		break;
	}

	if (ret)
		brcmf_dbg(SDIO, "failed to %s data F%d@0x%05x, err: %d\n",
			  write ? "write" : "read", fn, addr, ret);

	return ret;
}

static int
brcmf_sdiod_addrprep(struct brcmf_sdio_dev *sdiodev, uint width, u32 *addr)
{
	uint bar0 = *addr & ~SBSDIO_SB_OFT_ADDR_MASK;
	int err = 0;

	if (bar0 != sdiodev->sbwad) {
		err = brcmf_sdiod_set_sbaddr_window(sdiodev, bar0);
		if (err)
			return err;

		sdiodev->sbwad = bar0;
	}

	*addr &= SBSDIO_SB_OFT_ADDR_MASK;

	if (width == 4)
		*addr |= SBSDIO_SB_ACCESS_2_4B_FLAG;

	return 0;
}

static int brcmf_sdiod_regrw_helper(struct brcmf_sdio_dev *sdiodev, u32 addr, u8 regsz, void *data, bool write) {
	u8 func;
	s32 retry = 0;
	int ret;

	if (sdiodev->state == BRCMF_SDIOD_NOMEDIUM)
		return -ENOMEDIUM;

	/*
	 * figure out how to read the register based on address range
	 * 0x00 ~ 0x7FF: function 0 CCCR and FBR
	 * 0x10000 ~ 0x1FFFF: function 1 miscellaneous registers
	 * The rest: function 1 silicon backplane core registers
	 */
	if ((addr & ~REG_F0_REG_MASK) == 0)
		func = SDIO_FUNC_0;
	else
		func = SDIO_FUNC_1;

	do {
		if (!write)
			memset(data, 0, regsz);
		/* for retry wait for 1 ms till bus get settled down */
		if (retry)
			usleep_range(1000, 2000);
		ret = brcmf_sdiod_request_data(sdiodev, func, addr, regsz,
					       data, write);
	} while (ret != 0 && ret != -ENOMEDIUM &&
		 retry++ < SDIOH_API_ACCESS_RETRY_LIMIT);

	if (ret == -ENOMEDIUM)
		brcmf_sdiod_change_state(sdiodev, BRCMF_SDIOD_NOMEDIUM);
	else if (ret != 0) {
		/*
		 * SleepCSR register access can fail when
		 * waking up the device so reduce this noise
		 * in the logs.
		 */
		if (addr != SBSDIO_FUNC1_SLEEPCSR)
			brcmf_err("failed to %s data F%d@0x%05x, err: %d\n",
				  write ? "write" : "read", func, addr, ret);
		else
			brcmf_dbg(SDIO, "failed to %s data F%d@0x%05x, err: %d\n",
				  write ? "write" : "read", func, addr, ret);
	}
	return ret;
}

static int
brcmf_sdiod_set_sbaddr_window(struct brcmf_sdio_dev *sdiodev, u32 address)
{
	int err = 0, i;
	u8 addr[3];

	if (sdiodev->state == BRCMF_SDIOD_NOMEDIUM)
		return -ENOMEDIUM;

	addr[0] = (address >> 8) & SBSDIO_SBADDRLOW_MASK;
	addr[1] = (address >> 16) & SBSDIO_SBADDRMID_MASK;
	addr[2] = (address >> 24) & SBSDIO_SBADDRHIGH_MASK;

	for (i = 0; i < 3; i++) {
		err = brcmf_sdiod_regrw_helper(sdiodev,
					       SBSDIO_FUNC1_SBADDRLOW + i,
					       sizeof(u8), &addr[i], true);
		if (err) {
			brcmf_err("failed at addr: 0x%0x\n",
				  SBSDIO_FUNC1_SBADDRLOW + i);
			break;
		}
	}

	return err;
}

u32 brcmf_sdiod_regrl(struct brcmf_sdio_dev *sdiodev, u32 addr, int *ret)
{
	u32 data = 0;
	int retval;

	brcmf_dbg(SDIO, "addr:0x%08x\n", addr);
	retval = brcmf_sdiod_addrprep(sdiodev, sizeof(data), &addr);
	if (retval)
		goto done;
	retval = brcmf_sdiod_regrw_helper(sdiodev, addr, sizeof(data), &data,
					  false);
	brcmf_dbg(SDIO, "data:0x%08x\n", data);

done:
	if (ret)
		*ret = retval;

	return data;
}

/********************************************************/
__unused
static void
probe_bcrm(struct cam_device *dev) {
	uint32_t cis_addr;
	struct cis_info info;

	sdio_card_set_bus_width(dev, bus_width_4);
	cis_addr = sdio_get_common_cis_addr(dev);
	printf("CIS address: %04X\n", cis_addr);

	memset(&info, 0, sizeof(info));
	sdio_func_read_cis(dev, 0, cis_addr, &info);
	printf("Vendor 0x%04X product 0x%04X\n", info.man_id, info.prod_id);
}

__unused static uint8_t*
mmap_fw() {
	const char fw_path[] = "/home/kibab/repos/fbsd-bbb/brcm-firmware/brcmfmac4330-sdio.bin";
	struct stat sb;
	uint8_t *fw_ptr;

	int fd = open(fw_path, O_RDONLY);
	if (fd < 0)
		errx(1, "Cannot open firmware file");
	if (fstat(fd, &sb) < 0)
		errx(1, "Cannot get file stat");
	fw_ptr = mmap(NULL, sb.st_size, PROT_READ, 0, fd, 0);
	if (fw_ptr == MAP_FAILED)
		errx(1, "Cannot map the file");

	return fw_ptr;
}

static void
usage() {
	printf("sdiotool -u <pass_dev_unit>\n");
	exit(0);
}

struct card_info {
	uint8_t num_funcs;
	struct cis_info f[8];
};

/*
 * TODO: We should add SDIO card info about at least number of
 * available functions to struct cam_device and use it instead
 * of checking for man_id = 0x00 for detecting number of functions
 */
static void
get_sdio_card_info(struct cam_device *dev, struct card_info *ci) {
	uint32_t cis_addr;
	uint32_t fbr_addr;
	int ret;

	cis_addr = sdio_get_common_cis_addr(dev);

	memset(ci, 0, sizeof(struct card_info));
	sdio_func_read_cis(dev, 0, cis_addr, &ci->f[0]);
	printf("F0: Vendor 0x%04X product 0x%04X max block size %d bytes\n",
	       ci->f[0].man_id, ci->f[0].prod_id, ci->f[0].max_block_size);
	for (int i = 1; i <= 7; i++) {
		fbr_addr = SD_IO_FBR_START * i + 0x9;
		cis_addr =  sdio_read_1(dev, 0, fbr_addr++, &ret);bailout(ret);
		cis_addr |= sdio_read_1(dev, 0, fbr_addr++, &ret) << 8;
		cis_addr |= sdio_read_1(dev, 0, fbr_addr++, &ret) << 16;
		sdio_func_read_cis(dev, i, cis_addr, &ci->f[i]);
		printf("F%d: Vendor 0x%04X product 0x%04X max block size %d bytes\n",
		       i, ci->f[i].man_id, ci->f[i].prod_id, ci->f[i].max_block_size);
		if (ci->f[i].man_id == 0) {
			printf("F%d doesn't exist\n", i);
			break;
		}
		ci->num_funcs++;
	}
}

int
main(int argc, char **argv) {
	char device[] = "pass";
	int unit = 0;
	int func = 0;
	__unused uint8_t *fw_ptr;
	int ch;
	struct cam_device *cam_dev;
	int ret;
	struct card_info ci;

	//fw_ptr = mmap_fw();

	while ((ch = getopt(argc, argv, "fu:")) != -1) {
		switch (ch) {
		case 'u':
			unit = (int) strtol(optarg, NULL, 10);
			break;
		case 'f':
			func = (int) strtol(optarg, NULL, 10);
			break;
		case '?':
		default:
			usage();
		}
	}
	argc -= optind;
	argv += optind;

	if ((cam_dev = cam_open_spec_device(device, unit, O_RDWR, NULL)) == NULL)
		errx(1, "Cannot open device");

	get_sdio_card_info(cam_dev, &ci);

	/* For now, everything non-broadcom is out of the question */
	if (ci.f[0].man_id != 0x02D0) {
		printf("The card is not a Broadcom device\n");
		exit(1);
	}
	/* Init structures */
	struct brcmf_sdio_dev brcmf_dev;
	struct brcmf_bus bus_if;
	struct sdio_func f0, f1, f2;
	bus_if.state = BRCMF_BUS_DOWN;
	brcmf_dev.cam_dev = cam_dev;
	brcmf_dev.bus_if = &bus_if;
	brcmf_dev.state = BRCMF_SDIOD_DOWN;

	/* Fill in functions */
	brcmf_dev.func[0] = &f0;
	brcmf_dev.func[1] = &f1;
	brcmf_dev.func[2] = &f2;

	brcmf_dev.func[0]->dev = brcmf_dev.func[1]->dev
		= brcmf_dev.func[2]->dev = cam_dev;
	brcmf_dev.func[0]->num = 0;
	brcmf_dev.func[1]->num = 1;
	brcmf_dev.func[2]->num = 2;

	ret = sdio_func_enable(cam_dev, 1, 1);bailout(ret);
	uint32_t magic = brcmf_sdiod_regrl(&brcmf_dev, 0x18000000, &ret);
	printf("Magic = %08x\n", magic);
	if (magic != REPLY_MAGIC) {
		errx(1, "Reply magic is incorrect: expected %08x, got %08x",
		     REPLY_MAGIC, magic);
	}
	cam_close_spec_device(cam_dev);
}
