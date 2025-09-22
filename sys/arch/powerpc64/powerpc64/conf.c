/*	$OpenBSD: conf.c,v 1.14 2022/09/02 20:06:56 miod Exp $	*/

/*-
 * Copyright (c) 1991 The Regents of the University of California.
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
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 *     @(#)conf.c	7.9 (Berkeley) 5/28/91
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/device.h>
#include <sys/tty.h>
#include <sys/vnode.h>

#include <machine/conf.h>

#include "wd.h"
bdev_decl(wd);
#include "cd.h"
#include "rd.h"
#include "sd.h"
#include "vnd.h"

struct bdevsw bdevsw[] =
{
	bdev_swap_init(1,sw),		/* 0: swap pseudo-device */
	bdev_disk_init(NVND,vnd),	/* 1: vnode disk driver */
	bdev_disk_init(NRD,rd),		/* 2: ram disk driver */
	bdev_disk_init(NSD,sd),		/* 3: SCSI disk */
	bdev_disk_init(NCD,cd),		/* 4: SCSI CD-ROM */
	bdev_disk_init(NWD,wd),		/* 5: ST506/ESDI/IDE disk */
	bdev_notdef(),
};
int	nblkdev = nitems(bdevsw);

#include "audio.h"
cdev_decl(wd);
#include "bio.h"
#include "bpfilter.h"
#include "ch.h"
#include "com.h"
cdev_decl(com);
#include "drm.h"
#include "dt.h"
#include "fido.h"
#include "ujoy.h"
#include "fuse.h"
#include "hotplug.h"
#include "ipmi.h"
#include "kcov.h"
#include "kexec.h"
#include "kstat.h"
#include "ksyms.h"
#include "lpt.h"
cdev_decl(lpt);
#include "midi.h"
#include "opalcons.h"
#include "openprom.h"
#include "pf.h"
#include "pppx.h"
#include "pty.h"
#include "radio.h"
#include "st.h"
#include "tun.h"
#include "ucom.h"
#include "ugen.h"
#include "uhid.h"
#include "uk.h"
#include "ulpt.h"
#include "usb.h"
#include "video.h"
#include "vscsi.h"
#include "wsdisplay.h"
#include "wskbd.h"
#include "wsmouse.h"
#include "wsmux.h"

#ifdef USER_PCICONF
#include "pci.h"
cdev_decl(pci);
#endif

struct cdevsw cdevsw[] =
{
	cdev_cn_init(1,cn),		/* 0: virtual console */
	cdev_ctty_init(1,ctty),		/* 1: controlling terminal */
	cdev_mm_init(1,mm),		/* 2: /dev/{null,mem,kmem,...} */
	cdev_ksyms_init(NKSYMS,ksyms),	/* 3: kernel symbols device */
	cdev_log_init(1,log),		/* 4: /dev/klog */
	cdev_ptm_init(NPTY,ptm),	/* 5: pseudo-tty ptm device */
	cdev_ptc_init(NPTY,ptc),	/* 6: pseudo-tty master */
	cdev_tty_init(NPTY,pts),	/* 7: pseudo-tty slave */
	cdev_fd_init(1,filedesc),	/* 8: file descriptor pseudo-device */
	cdev_bpf_init(NBPFILTER,bpf),	/* 9: packet filter */
	cdev_disk_init(1,diskmap),	/* 10: disk mapper */
	cdev_pf_init(NPF,pf),		/* 11: packet filter */
	cdev_random_init(1,random),	/* 12: random data source */
	cdev_dt_init(NDT,dt),		/* 13: dynamic tracer */
	cdev_kcov_init(NKCOV,kcov),	/* 14: kcov */
	cdev_kstat_init(NKSTAT,kstat),	/* 15: kernel statistics */
	cdev_kexec_init(NKEXEC,kexec),	/* 16: kexec */
	cdev_disk_init(NWD,wd),         /* 17: ST506/ESDI/IDE disk */
	cdev_notdef(),			/* 18 */
	cdev_notdef(),			/* 19 */
	cdev_notdef(),			/* 20 */
	cdev_notdef(),			/* 21 */
	cdev_notdef(),			/* 22 */
	cdev_notdef(),			/* 23 */
	cdev_disk_init(NVND,vnd),	/* 24: vnode disk driver */
	cdev_disk_init(NRD,rd),		/* 25: ram disk driver */
	cdev_disk_init(NSD,sd),		/* 26: SCSI disk */
	cdev_disk_init(NCD,cd),		/* 27: SCSI CD-ROM */
	cdev_notdef(),			/* 28 */
	cdev_notdef(),			/* 29 */
	cdev_notdef(),			/* 30 */
	cdev_notdef(),			/* 31 */
	cdev_audio_init(NAUDIO,audio),	/* 32: generic audio I/O */
	cdev_midi_init(NMIDI,midi),	/* 33: MIDI I/O */
	cdev_radio_init(NRADIO, radio), /* 34: generic radio I/O */
	cdev_video_init(NVIDEO,video),	/* 35: generic video I/O */
	cdev_notdef(),			/* 36 */
	cdev_notdef(),			/* 37 */
	cdev_notdef(),			/* 38 */
	cdev_notdef(),			/* 39 */
	cdev_wsdisplay_init(NWSDISPLAY,	/* 40: frame buffers, etc. */
	    wsdisplay),
	cdev_mouse_init(NWSKBD, wskbd),	/* 41: keyboards */
	cdev_mouse_init(NWSMOUSE,	/* 42: mice */
	    wsmouse),
	cdev_mouse_init(NWSMUX, wsmux),	/* 43: ws multiplexor */
	cdev_notdef(),			/* 44 */
	cdev_notdef(),			/* 45 */
	cdev_notdef(),			/* 46 */
	cdev_notdef(),			/* 47 */
	cdev_usb_init(NUSB,usb),	/* 48: USB controller */
	cdev_usbdev_init(NUGEN,ugen),	/* 49: USB generic driver */
	cdev_usbdev_init(NUHID,uhid),	/* 50: USB generic HID */
	cdev_fido_init(NFIDO,fido),	/* 51: FIDO/U2F security key */
	cdev_notdef(),			/* 52 */
	cdev_notdef(),			/* 53 */
	cdev_notdef(),			/* 54 */
	cdev_notdef(),			/* 55 */
	cdev_tty_init(NOPALCONS,opalcons), /* 56: OPAL console */
	cdev_tty_init(NCOM,com),	/* 57: serial port */
	cdev_tty_init(NUCOM,ucom),	/* 58: USB tty */
	cdev_notdef(),			/* 59 */
	cdev_notdef(),			/* 60 */
	cdev_notdef(),			/* 61 */
	cdev_notdef(),			/* 62 */
	cdev_notdef(),			/* 63 */
	cdev_lpt_init(NLPT,lpt),	/* 64: parallel printer */
	cdev_ulpt_init(NULPT,ulpt),	/* 65: USB printers */
	cdev_notdef(),			/* 66 */
	cdev_notdef(),			/* 67 */
	cdev_ch_init(NCH,ch),		/* 68: SCSI autochanger */
	cdev_tape_init(NST,st),		/* 69: SCSI tape */
	cdev_uk_init(NUK,uk),		/* 70: unknown SCSI */
	cdev_notdef(),			/* 71 */
	cdev_pppx_init(NPPPX,pppx),     /* 72: pppx */
	cdev_pppx_init(NPPPX,pppac),	/* 73: PPP Access Concentrator */
	cdev_notdef(),			/* 74: was switch(4) */
	cdev_tun_init(NTUN,tap),	/* 75: Ethernet network tunnel */
	cdev_tun_init(NTUN,tun),	/* 76: network tunnel */
	cdev_notdef(),			/* 77 */
	cdev_notdef(),			/* 78 */
	cdev_notdef(),			/* 79 */
	cdev_bio_init(NBIO,bio),	/* 80: ioctl tunnel */
	cdev_fuse_init(NFUSE,fuse),	/* 81: fuse */
	cdev_hotplug_init(NHOTPLUG,hotplug), /* 82: devices hot plugging */
	cdev_vscsi_init(NVSCSI,vscsi),	/* 83: vscsi */
	cdev_notdef(),			/* 84 */
	cdev_notdef(),			/* 85 */
	cdev_notdef(),			/* 86 */
	cdev_drm_init(NDRM,drm),	/* 87: drm */
	cdev_ipmi_init(NIPMI,ipmi),	/* 88: ipmi */
	cdev_notdef(),			/* 89 */
	cdev_notdef(),			/* 90 */
	cdev_notdef(),			/* 91 */
	cdev_openprom_init(NOPENPROM,openprom),	/* 92: /dev/openprom */
#ifdef USER_PCICONF
	cdev_pci_init(NPCI,pci),        /* 93: PCI user */
#else
	cdev_notdef(),			/* 93 */
#endif
	cdev_ujoy_init(NUJOY,ujoy),	/* 94: USB joystick/gamecontroller */
};
int	nchrdev = nitems(cdevsw);

int	mem_no = 2; 	/* major device number of memory special file */

dev_t	swapdev = makedev(0, 0);

int
iskmemdev(dev_t dev)
{
	return (major(dev) == mem_no && minor(dev) < 2);
}

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
	/*  3 */	NODEV,
	/*  4 */	NODEV,
	/*  5 */	NODEV,
	/*  6 */	NODEV,
	/*  7 */	NODEV,
	/*  8 */	NODEV,
	/*  9 */	NODEV,
	/* 10 */	NODEV,
	/* 11 */	NODEV,
	/* 12 */	NODEV,
	/* 13 */	NODEV,
	/* 14 */	NODEV,
	/* 15 */	NODEV,
	/* 16 */	NODEV,
	/* 17 */	5,		/* wd */
	/* 18 */	NODEV,
	/* 19 */	NODEV,
	/* 20 */	NODEV,
	/* 21 */	NODEV,
	/* 22 */	NODEV,
	/* 23 */	NODEV,
	/* 24 */	1,		/* vnd */
	/* 25 */	2,		/* rd */
	/* 26 */	3,		/* sd */
	/* 27 */	4,		/* cd */
};
const int nchrtoblktbl = nitems(chrtoblktbl);
