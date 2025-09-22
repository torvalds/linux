/*	$OpenBSD: conf.c,v 1.83 2024/11/27 10:33:31 jsg Exp $	*/

/*
 * Copyright (c) 1994, 1995 Charles M. Hannum.  All rights reserved.
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
 *	This product includes software developed by Charles Hannum.
 * 4. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
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
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/device.h>
#include <sys/disklabel.h>
#include <sys/ioctl.h>
#include <sys/tty.h>
#include <sys/vnode.h>

#include <machine/conf.h>

#include "wd.h"
bdev_decl(wd);
#include "fd.h"
bdev_decl(fd);
#include "sd.h"
#include "st.h"
#include "cd.h"
#include "uk.h"
#include "vnd.h"
#include "rd.h"

struct bdevsw	bdevsw[] =
{
	bdev_disk_init(NWD,wd),		/* 0: ST506/ESDI/IDE disk */
	bdev_swap_init(1,sw),		/* 1: swap pseudo-device */
	bdev_disk_init(NFD,fd),		/* 2: floppy diskette */
	bdev_notdef(),			/* 3 */
	bdev_disk_init(NSD,sd),		/* 4: SCSI disk */
	bdev_notdef(),			/* 5: was: SCSI tape */
	bdev_disk_init(NCD,cd),		/* 6: SCSI CD-ROM */
	bdev_notdef(),			/* 7 */
	bdev_notdef(),			/* 8 */
	bdev_notdef(),			/* 9 */
	bdev_notdef(),			/* 10 */
	bdev_notdef(),			/* 11 */
	bdev_notdef(),			/* 12 */
	bdev_notdef(),			/* 13 */
	bdev_disk_init(NVND,vnd),	/* 14: vnode disk driver */
	bdev_notdef(),			/* 15: was: Sony CD-ROM */
	bdev_notdef(),			/* 16: was: concatenated disk driver */
	bdev_disk_init(NRD,rd),		/* 17: ram disk driver */
	bdev_notdef(),			/* 18 */
	bdev_notdef(),			/* 19 was: RAIDframe disk driver */
};
int	nblkdev = nitems(bdevsw);

/* open, close, ioctl */
#define cdev_ocis_init(c,n) { \
        dev_init(c,n,open), dev_init(c,n,close), (dev_type_read((*))) enodev, \
        (dev_type_write((*))) enodev, dev_init(c,n,ioctl), \
        (dev_type_stop((*))) enodev, 0, \
        (dev_type_mmap((*))) enodev, 0, 0, seltrue_kqfilter }

/* open, close, read */
#define cdev_nvram_init(c,n) { \
	dev_init(c,n,open), dev_init(c,n,close), dev_init(c,n,read), \
	(dev_type_write((*))) enodev, (dev_type_ioctl((*))) enodev, \
	(dev_type_stop((*))) enodev, 0, \
	(dev_type_mmap((*))) enodev, 0, 0, seltrue_kqfilter }

/* open, close, ioctl */
#define cdev_vmm_init(c,n) { \
	dev_init(c,n,open), dev_init(c,n,close), \
	(dev_type_read((*))) enodev, \
	(dev_type_write((*))) enodev, \
	 dev_init(c,n,ioctl), \
	(dev_type_stop((*))) enodev, 0, \
	(dev_type_mmap((*))) enodev, 0, 0, seltrue_kqfilter }

/* open, close, ioctl */
#define cdev_psp_init(c,n) { \
	dev_init(c,n,open), dev_init(c,n,close), \
	(dev_type_read((*))) enodev, \
	(dev_type_write((*))) enodev, \
	 dev_init(c,n,ioctl), \
	(dev_type_stop((*))) enodev, 0, \
	(dev_type_mmap((*))) enodev, 0, 0, seltrue_kqfilter }

#define	mmread	mmrw
#define	mmwrite	mmrw
cdev_decl(mm);
cdev_decl(wd);
#include "bio.h"
#include "pty.h"
#include "com.h"
cdev_decl(com);
cdev_decl(fd);
#include "lpt.h"
cdev_decl(lpt);
#include "ch.h"
#include "bpfilter.h"
#include "spkr.h"
cdev_decl(spkr);
#include "cy.h"
cdev_decl(cy);
#include "tun.h"
#include "audio.h"
#include "video.h"
#include "midi.h"
#include "acpi.h"
#include "pctr.h"
#include "bktr.h"
#include "ksyms.h"
#include "kstat.h"
#include "usb.h"
#include "uhid.h"
#include "fido.h"
#include "ujoy.h"
#include "ugen.h"
#include "ulpt.h"
#include "ucom.h"
#include "cz.h"
cdev_decl(cztty);
#include "radio.h"
#include "nvram.h"
cdev_decl(nvram);
#include "drm.h"
#include "viocon.h"
cdev_decl(viocon);

#include "wsdisplay.h"
#include "wskbd.h"
#include "wsmouse.h"
#include "wsmux.h"
#include "kcov.h"

#ifdef USER_PCICONF
#include "pci.h"
cdev_decl(pci);
#endif

#include "dt.h"
#include "pf.h"
#include "hotplug.h"
#include "gpio.h"
#include "vscsi.h"
#include "pppx.h"
#include "fuse.h"
#include "pvbus.h"
#include "ipmi.h"
#include "efi.h"

struct cdevsw	cdevsw[] =
{
	cdev_cn_init(1,cn),		/* 0: virtual console */
	cdev_ctty_init(1,ctty),		/* 1: controlling terminal */
	cdev_mm_init(1,mm),		/* 2: /dev/{null,mem,kmem,...} */
	cdev_disk_init(NWD,wd),		/* 3: ST506/ESDI/IDE disk */
	cdev_notdef(),			/* 4 was /dev/drum */
	cdev_tty_init(NPTY,pts),	/* 5: pseudo-tty slave */
	cdev_ptc_init(NPTY,ptc),	/* 6: pseudo-tty master */
	cdev_log_init(1,log),		/* 7: /dev/klog */
	cdev_tty_init(NCOM,com),	/* 8: serial port */
	cdev_disk_init(NFD,fd),		/* 9: floppy disk */
	cdev_vmm_init(NVMM,vmm),	/* 10 vmm */
	cdev_notdef(),			/* 11: Sony CD-ROM */
	cdev_wsdisplay_init(NWSDISPLAY,	/* 12: frame buffers, etc. */
	    wsdisplay),
	cdev_disk_init(NSD,sd),		/* 13: SCSI disk */
	cdev_tape_init(NST,st),		/* 14: SCSI tape */
	cdev_disk_init(NCD,cd),		/* 15: SCSI CD-ROM */
	cdev_lpt_init(NLPT,lpt),	/* 16: parallel printer */
	cdev_ch_init(NCH,ch),		/* 17: SCSI autochanger */
	cdev_notdef(),			/* 18: was: concatenated disk driver */
	cdev_kcov_init(NKCOV,kcov),	/* 19: kcov */
	cdev_uk_init(NUK,uk),		/* 20: unknown SCSI */
	cdev_notdef(),			/* 21 */
	cdev_fd_init(1,filedesc),	/* 22: file descriptor pseudo-device */
	cdev_bpf_init(NBPFILTER,bpf),	/* 23: Berkeley packet filter */
	cdev_notdef(),			/* 24 */
	cdev_notdef(),			/* 25 */
	cdev_notdef(),			/* 26 */
	cdev_spkr_init(NSPKR,spkr),	/* 27: PC speaker */
	cdev_notdef(),			/* 28 was LKM */
	cdev_notdef(),			/* 29 */
	cdev_dt_init(NDT,dt),		/* 30: dynamic tracer */
	cdev_notdef(),			/* 31 */
	cdev_notdef(),			/* 32 */
	cdev_notdef(),			/* 33 */
	cdev_notdef(),			/* 34 */
	cdev_notdef(),			/* 35: Microsoft mouse */
	cdev_notdef(),			/* 36: Logitech mouse */
	cdev_notdef(),			/* 37: Extended PS/2 mouse */
	cdev_tty_init(NCY,cy),		/* 38: Cyclom serial port */
	cdev_notdef(),			/* 39: Mitsumi CD-ROM */
	cdev_tun_init(NTUN,tun),	/* 40: network tunnel */
	cdev_disk_init(NVND,vnd),	/* 41: vnode disk driver */
	cdev_audio_init(NAUDIO,audio),	/* 42: generic audio I/O */
	cdev_notdef(),			/* 43 */
	cdev_video_init(NVIDEO,video),	/* 44: generic video I/O */
	cdev_random_init(1,random),	/* 45: random data source */
	cdev_ocis_init(NPCTR,pctr),	/* 46: performance counters */
	cdev_disk_init(NRD,rd),		/* 47: ram disk driver */
	cdev_notdef(),			/* 48 */
	cdev_bktr_init(NBKTR,bktr),     /* 49: Bt848 video capture device */
	cdev_ksyms_init(NKSYMS,ksyms),	/* 50: Kernel symbols device */
	cdev_kstat_init(NKSTAT,kstat),	/* 51: Kernel statistics */
	cdev_midi_init(NMIDI,midi),	/* 52: MIDI I/O */
	cdev_notdef(),			/* 53 was: sequencer I/O */
	cdev_notdef(),			/* 54 was: RAIDframe disk driver */
	cdev_notdef(),			/* 55: */
	/* The following slots are reserved for isdn4bsd. */
	cdev_notdef(),			/* 56: i4b main device */
	cdev_notdef(),			/* 57: i4b control device */
	cdev_notdef(),			/* 58: i4b raw b-channel access */
	cdev_notdef(),			/* 59: i4b trace device */
	cdev_notdef(),			/* 60: i4b phone device */
	/* End of reserved slots for isdn4bsd. */
	cdev_usb_init(NUSB,usb),	/* 61: USB controller */
	cdev_usbdev_init(NUHID,uhid),	/* 62: USB generic HID */
	cdev_usbdev_init(NUGEN,ugen),	/* 63: USB generic driver */
	cdev_ulpt_init(NULPT,ulpt),	/* 64: USB printers */
	cdev_notdef(),			/* 65: urio */
	cdev_tty_init(NUCOM,ucom),	/* 66: USB tty */
	cdev_mouse_init(NWSKBD, wskbd),	/* 67: keyboards */
	cdev_mouse_init(NWSMOUSE,	/* 68: mice */
	    wsmouse),
	cdev_mouse_init(NWSMUX, wsmux),	/* 69: ws multiplexor */
	cdev_notdef(),			/* 70: was: /dev/crypto */
	cdev_tty_init(NCZ,cztty),	/* 71: Cyclades-Z serial port */
#ifdef USER_PCICONF
	cdev_pci_init(NPCI,pci),        /* 72: PCI user */
#else
	cdev_notdef(),
#endif
	cdev_pf_init(NPF,pf),		/* 73: packet filter */
	cdev_notdef(),			/* 74: ALTQ (deprecated) */
	cdev_notdef(),
	cdev_radio_init(NRADIO, radio), /* 76: generic radio I/O */
	cdev_notdef(),			/* 77: was USB scanners */
	cdev_notdef(),			/* 78 */
	cdev_bio_init(NBIO,bio),	/* 79: ioctl tunnel */
	cdev_notdef(),			/* 80 */
	cdev_ptm_init(NPTY,ptm),	/* 81: pseudo-tty ptm device */
	cdev_hotplug_init(NHOTPLUG,hotplug), /* 82: devices hot plugging */
	cdev_acpi_init(NACPI,acpi),	/* 83: ACPI */
	cdev_efi_init(NEFI,efi),	/* 84: EFI */
	cdev_nvram_init(NNVRAM,nvram),	/* 85: NVRAM interface */
	cdev_notdef(),			/* 86 */
	cdev_drm_init(NDRM,drm),	/* 87: drm */
	cdev_gpio_init(NGPIO,gpio),	/* 88: gpio */
	cdev_vscsi_init(NVSCSI,vscsi),	/* 89: vscsi */
	cdev_disk_init(1,diskmap),	/* 90: disk mapper */
	cdev_pppx_init(NPPPX,pppx),     /* 91: pppx */
	cdev_fuse_init(NFUSE,fuse),	/* 92: fuse */
	cdev_tun_init(NTUN,tap),	/* 93: Ethernet network tunnel */
	cdev_tty_init(NVIOCON,viocon),  /* 94: virtio console */
	cdev_pvbus_init(NPVBUS,pvbus),	/* 95: pvbus(4) control interface */
	cdev_ipmi_init(NIPMI,ipmi),	/* 96: ipmi */
	cdev_notdef(),			/* 97: was switch(4) */
	cdev_fido_init(NFIDO,fido),	/* 98: FIDO/U2F security keys */
	cdev_pppx_init(NPPPX,pppac),	/* 99: PPP Access Concentrator */
	cdev_ujoy_init(NUJOY,ujoy),	/* 100: USB joystick/gamecontroller */
	cdev_psp_init(NPSP,psp),	/* 101: PSP */
};
int	nchrdev = nitems(cdevsw);

int	mem_no = 2;	/* major device number of memory special file */

/*
 * Swapdev is a fake device implemented
 * in sw.c used only internally to get to swstrategy.
 * It cannot be provided to the users, because the
 * swstrategy routine munches the b_dev and b_blkno entries
 * before calling the appropriate driver.  This would horribly
 * confuse, e.g. the hashing routines. Instead, /dev/drum is
 * provided as a character (raw) device.
 */
dev_t	swapdev = makedev(1, 0);

/*
 * Returns true if dev is /dev/mem or /dev/kmem.
 */
int
iskmemdev(dev_t dev)
{

	return (major(dev) == mem_no && minor(dev) < 2);
}

/*
 * Returns true if dev is /dev/zero.
 */
int
iszerodev(dev_t dev)
{

	return (major(dev) == mem_no && minor(dev) == 12);
}

dev_t
getnulldev(void)
{
	return makedev(mem_no, 2);
}

const int chrtoblktbl[] = {
	/*VCHR*/	/*VBLK*/
	/*  0 */	NODEV,
	/*  1 */	NODEV,
	/*  2 */	NODEV,
	/*  3 */	0,		/* wd */
	/*  4 */	NODEV,
	/*  5 */	NODEV,
	/*  6 */	NODEV,
	/*  7 */	NODEV,
	/*  8 */	NODEV,
	/*  9 */	2,		/* fd */
	/* 10 */	NODEV,
	/* 11 */	NODEV,
	/* 12 */	NODEV,
	/* 13 */	4,		/* sd */
	/* 14 */	NODEV,
	/* 15 */	6,		/* cd */
	/* 16 */	NODEV,
	/* 17 */	NODEV,
	/* 18 */	NODEV,
	/* 19 */	NODEV,
	/* 20 */	NODEV,
	/* 21 */	NODEV,
	/* 22 */	NODEV,
	/* 23 */	NODEV,
	/* 24 */	NODEV,
	/* 25 */	NODEV,
	/* 26 */	NODEV,
	/* 27 */	NODEV,
	/* 28 */	NODEV,
	/* 29 */	NODEV,
	/* 30 */	NODEV,
	/* 31 */	NODEV,
	/* 32 */	NODEV,
	/* 33 */	NODEV,
	/* 34 */	NODEV,
	/* 35 */	NODEV,
	/* 36 */	NODEV,
	/* 37 */	NODEV,
	/* 38 */	NODEV,
	/* 39 */	NODEV,
	/* 40 */	NODEV,
	/* 41 */	14,		/* vnd */
	/* 42 */	NODEV,
	/* 43 */	NODEV,
	/* 44 */	NODEV,
	/* 45 */	NODEV,
	/* 46 */	NODEV,
	/* 47 */	17,		/* rd */
};

const int nchrtoblktbl = nitems(chrtoblktbl);

/*
 * In order to map BSD bdev numbers of disks to their BIOS equivalents
 * we use several heuristics, one being using checksums of the first
 * few blocks of a disk to get a signature we can match with /boot's
 * computed signatures.  To know where from to read, we must provide a
 * disk driver name -> bdev major number table, which follows.
 * Note: floppies are not included as those are differentiated by the BIOS.
 */
int findblkmajor(struct device *dv);
dev_t dev_rawpart(struct device *);	/* XXX */

dev_t
dev_rawpart(struct device *dv)
{
	int majdev;

	majdev = findblkmajor(dv);

	switch (majdev) {
	/* add here any device you want to be checksummed on boot */
	case 0:		/* wd */
	case 4:		/* sd */
		return (MAKEDISKDEV(majdev, dv->dv_unit, RAW_PART));
		break;
	default:
		;
	}

	return (NODEV);
}

#include <dev/cons.h>

cons_decl(com);
cons_decl(ws);

struct	consdev constab[] = {
#if NWSDISPLAY > 0
	cons_init(ws),
#endif
#if NCOM > 0
	cons_init(com),
#endif
	{ 0 },
};
