/*	$OpenBSD: conf.c,v 1.90 2024/06/11 09:21:32 jsg Exp $	*/
/*	$NetBSD: conf.c,v 1.17 2001/03/26 12:33:26 lukem Exp $ */

/*
 * Copyright (c) 1992, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * This software was developed by the Computer Systems Engineering group
 * at Lawrence Berkeley Laboratory under DARPA contract BG 91-66 and
 * contributed to Berkeley.
 *
 * All advertising materials mentioning features or use of this software
 * must display the following acknowledgement:
 *	This product includes software developed by the University of
 *	California, Lawrence Berkeley Laboratory.
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
 *	@(#)conf.c	8.3 (Berkeley) 11/14/93
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/buf.h>
#include <sys/ioctl.h>
#include <sys/vnode.h>
#include <sys/tty.h>
#include <sys/conf.h>

#include <machine/conf.h>

#include "bio.h"
#include "pty.h"
#include "bpfilter.h"
#include "tun.h"
#include "midi.h"
#include "audio.h"
#include "video.h"
#include "vnd.h"
#include "ch.h"
#include "sd.h"
#include "st.h"
#include "cd.h"
#include "uk.h"
#include "wd.h"

#include "zstty.h"
#include "sab.h"
#include "pcons.h"
#include "vcons.h"
#include "vcctty.h"
#include "sbbc.h"
#include "com.h"
#include "lpt.h"
#include "bpp.h"
#include "magma.h"		/* has NMTTY and NMBPP */
#include "spif.h"		/* has NSTTY and NSBPP */
#include "uperf.h"
#include "vldcp.h"
#include "vdsp.h"

#include "fdc.h"		/* has NFDC and NFD; see files.sparc64 */

#include "drm.h"

#include "wsdisplay.h"
#include "wskbd.h"
#include "wsmouse.h"
#include "wsmux.h"

#ifdef USER_PCICONF
#include "pci.h"
cdev_decl(pci);
#endif

#include "rd.h"

#include "usb.h"
#include "uhid.h"
#include "fido.h"
#include "ujoy.h"
#include "ugen.h"
#include "ulpt.h"
#include "ucom.h"

#include "dt.h"
#include "pf.h"

#include "ksyms.h"
#include "kstat.h"

#include "hotplug.h"
#include "vscsi.h"
#include "pppx.h"
#include "fuse.h"

struct bdevsw	bdevsw[] =
{
	bdev_notdef(),			/* 0 */
	bdev_notdef(),			/* 1 */
	bdev_notdef(),			/* 2 */
	bdev_notdef(),			/* 3 */
	bdev_swap_init(1,sw),		/* 4 swap pseudo-device */
	bdev_disk_init(NRD,rd),		/* 5: ram disk */
	bdev_notdef(),			/* 6 */
	bdev_disk_init(NSD,sd),		/* 7: SCSI disk */
	bdev_disk_init(NVND,vnd),	/* 8: vnode disk driver */
	bdev_notdef(),			/* 9: was: concatenated disk driver */
	bdev_notdef(),			/* 10 */
	bdev_notdef(),			/* 11: was: SCSI tape */
	bdev_disk_init(NWD,wd),		/* 12: IDE disk */
	bdev_notdef(),			/* 13 */
	bdev_notdef(),			/* 14 */
	bdev_notdef(),			/* 15 */
	bdev_disk_init(NFD,fd),		/* 16: floppy disk */
	bdev_notdef(),			/* 17 */
	bdev_disk_init(NCD,cd),		/* 18: SCSI CD-ROM */
	bdev_notdef(),			/* 19 */
	bdev_notdef(),			/* 20 */
	bdev_notdef(),			/* 21 */
	bdev_notdef(),			/* 22 */
	bdev_notdef(),			/* 23 */
	bdev_notdef(),			/* 24 */
	bdev_notdef(),			/* 25 was: RAIDframe disk driver */
};
int	nblkdev = nitems(bdevsw);

struct cdevsw	cdevsw[] =
{
	cdev_cn_init(1,cn),		/* 0: virtual console */
	cdev_notdef(),			/* 1 */
	cdev_ctty_init(1,ctty),		/* 2: controlling terminal */
	cdev_mm_init(1,mm),		/* 3: /dev/{null,mem,kmem,...} */
	cdev_notdef(),			/* 4 */
	cdev_notdef(),			/* 5 */
	cdev_notdef(),			/* 6 */
	cdev_notdef(),			/* 7 was /dev/drum */
	cdev_notdef(),			/* 8 */
	cdev_notdef(),			/* 9 */
	cdev_notdef(),			/* 10 */
	cdev_notdef(),			/* 11 */
	cdev_tty_init(NZSTTY,zs),	/* 12: Zilog 8530 serial port */
	cdev_notdef(),			/* 13 */
	cdev_notdef(),			/* 14 */
	cdev_notdef(),			/* 15 */
	cdev_log_init(1,log),		/* 16: /dev/klog */
	cdev_disk_init(NSD,sd),		/* 17: SCSI disk */
	cdev_tape_init(NST,st),		/* 18: SCSI tape */
	cdev_ch_init(NCH,ch),		/* 19: SCSI autochanger */
	cdev_tty_init(NPTY,pts),	/* 20: pseudo-tty slave */
	cdev_ptc_init(NPTY,ptc),	/* 21: pseudo-tty master */
	cdev_notdef(),			/* 22 */
	cdev_notdef(),			/* 23: was: concatenated disk driver */
	cdev_fd_init(1,filedesc),	/* 24: file descriptor pseudo-device */
	cdev_uperf_init(NUPERF,uperf),	/* 25: performance counters */
	cdev_disk_init(NWD,wd),		/* 26: IDE disk */
	cdev_notdef(),			/* 27 */
	cdev_notdef(),			/* 28 */
	cdev_notdef(),			/* 29 */
	cdev_dt_init(NDT,dt),		/* 30: dynamic tracer */
	cdev_notdef(),			/* 31 */
	cdev_notdef(),			/* 32 */
	cdev_notdef(),			/* 33 */
	cdev_notdef(),			/* 34 */
	cdev_notdef(),			/* 35 */
	cdev_tty_init(NCOM,com),	/* 36: NS16x50 compatible ports */
	cdev_lpt_init(NLPT,lpt),	/* 37: parallel printer */
	cdev_notdef(),			/* 38 */
	cdev_notdef(),			/* 39 */
	cdev_notdef(),			/* 40 */
	cdev_notdef(),			/* 41 */
	cdev_notdef(),			/* 42 */
	cdev_notdef(),			/* 43 */
	cdev_video_init(NVIDEO,video),	/* 44: generic video I/O */
	cdev_notdef(),			/* 45 */
	cdev_notdef(),			/* 46 */
	cdev_notdef(),			/* 47 */
	cdev_notdef(),			/* 48 */
	cdev_notdef(),			/* 49 */
	cdev_notdef(),			/* 50 */
	cdev_kstat_init(NKSTAT,kstat),	/* 51: kernel statistics */ 
#ifdef USER_PCICONF
	cdev_pci_init(NPCI,pci),	/* 52: PCI user */
#else
	cdev_notdef(),			/* 52 */
#endif
	cdev_notdef(),			/* 53 */
	cdev_disk_init(NFD,fd),		/* 54: floppy disk */
	cdev_notdef(),			/* 55 */
	cdev_notdef(),			/* 56 */
	cdev_notdef(),			/* 57 */
	cdev_disk_init(NCD,cd),		/* 58: SCSI CD-ROM */
	cdev_notdef(),			/* 59 */
	cdev_uk_init(NUK,uk),		/* 60: SCSI unknown */
	cdev_disk_init(NRD,rd),		/* 61: memory disk */
	cdev_notdef(),			/* 62 */
	cdev_notdef(),			/* 63 */
	cdev_notdef(),			/* 64 */
	cdev_notdef(),			/* 65 */
	cdev_notdef(),			/* 66 */
	cdev_notdef(),			/* 67 */
	cdev_midi_init(NMIDI,midi),	/* 68: /dev/rmidi */
	cdev_audio_init(NAUDIO,audio),	/* 69: /dev/audio */
	cdev_openprom_init(1,openprom),	/* 70: /dev/openprom */
	cdev_tty_init(NMTTY,mtty),	/* 71: magma serial ports */
	cdev_gen_init(NMBPP,mbpp),	/* 72: magma parallel ports */
	cdev_pf_init(NPF,pf),		/* 73: packet filter */
	cdev_notdef(),			/* 74: ALTQ (deprecated) */
	cdev_notdef(),			/* 75: was: /dev/crypto */
	cdev_ksyms_init(NKSYMS,ksyms),	/* 76 *: Kernel symbols device */
	cdev_tty_init(NSABTTY,sabtty),	/* 77: sab82532 serial ports */
	cdev_wsdisplay_init(NWSDISPLAY,	/* 78: frame buffers, etc. */
	    wsdisplay),
	cdev_mouse_init(NWSKBD, wskbd),	/* 79: keyboards */
	cdev_mouse_init(NWSMOUSE, wsmouse), /* 80: mice */
	cdev_mouse_init(NWSMUX, wsmux),	/* 81: ws multiplexor */
	cdev_notdef(),			/* 82 */
	cdev_notdef(),			/* 83 */
	cdev_notdef(),			/* 84 */
	cdev_notdef(),			/* 85 */
	cdev_notdef(),			/* 86 */
	cdev_drm_init(NDRM,drm),	/* 87: drm */
	cdev_notdef(),			/* 88 */
	cdev_notdef(),			/* 89 */
	cdev_usb_init(NUSB,usb),	/* 90: USB controller */
	cdev_usbdev_init(NUHID,uhid),	/* 91: USB generic HID */
	cdev_usbdev_init(NUGEN,ugen),	/* 92: USB generic driver */
	cdev_ulpt_init(NULPT,ulpt),	/* 93: USB printers */
	cdev_notdef(),			/* 94 */
	cdev_tty_init(NUCOM,ucom),	/* 95: USB tty */
	cdev_notdef(),			/* 96: was USB scanners */
	cdev_notdef(),			/* 97 */
	cdev_notdef(),			/* 98 */
	cdev_notdef(),			/* 99 */
	cdev_notdef(),			/* 100 */
	cdev_notdef(),			/* 101 */
	cdev_notdef(),			/* 102 */
	cdev_notdef(),			/* 103 */
	cdev_notdef(),			/* 104 */
	cdev_bpf_init(NBPFILTER,bpf),	/* 105: packet filter */
	cdev_notdef(),			/* 106 */
	cdev_bpp_init(NBPP,bpp),	/* 107: on-board parallel port */
	cdev_tty_init(NSTTY,stty),	/* 108: spif serial ports */
	cdev_gen_init(NSBPP,sbpp),	/* 109: spif parallel ports */
	cdev_disk_init(NVND,vnd),	/* 110: vnode disk driver */
	cdev_tun_init(NTUN,tun),	/* 111: network tunnel */
	cdev_notdef(),			/* 112 was LKM */
	cdev_notdef(),			/* 113 */
	cdev_notdef(),			/* 114 */
	cdev_notdef(),			/* 115 */
	cdev_notdef(),			/* 116 */
	cdev_notdef(),			/* 117 */
	cdev_notdef(),			/* 118 */
	cdev_random_init(1,random),	/* 119: random data source */
	cdev_bio_init(NBIO,bio),	/* 120: ioctl tunnel */
	cdev_notdef(),			/* 121 was: RAIDframe disk driver */
	cdev_tty_init(NPCONS,pcons),	/* 122: PROM console */
	cdev_ptm_init(NPTY,ptm),	/* 123: pseudo-tty ptm device */
	cdev_hotplug_init(NHOTPLUG,hotplug), /* 124: devices hot plugging */
	cdev_tty_init(NVCONS,vcons),	/* 125: virtual console */
	cdev_tty_init(NSBBC,sbbc),	/* 126: SBBC console */
	cdev_tty_init(NVCCTTY,vcctty),	/* 127: virtual console concentrator */
	cdev_vscsi_init(NVSCSI,vscsi),	/* 128: vscsi */
	cdev_notdef(),
	cdev_disk_init(1,diskmap),	/* 130: disk mapper */
	cdev_pppx_init(NPPPX,pppx),	/* 131: pppx */
	cdev_gen_init(NVLDCP,vldcp),	/* 132: vldcp */
	cdev_vdsp_init(NVDSP,vdsp),	/* 133: vdsp */
	cdev_fuse_init(NFUSE,fuse),	/* 134: fuse */
	cdev_tun_init(NTUN,tap),	/* 135: Ethernet network tunnel */
	cdev_notdef(),			/* 136: was switch(4) */
	cdev_fido_init(NFIDO,fido),	/* 137: FIDO/U2F security key */
	cdev_pppx_init(NPPPX,pppac),	/* 138: PPP Access Concentrator */
	cdev_ujoy_init(NUJOY,ujoy),	/* 139: USB joystick/gamecontroller */
};
int	nchrdev = nitems(cdevsw);

int	mem_no = 3; 	/* major device number of memory special file */

/*
 * Swapdev is a fake device implemented
 * in sw.c used only internally to get to swstrategy.
 * It cannot be provided to the users, because the
 * swstrategy routine munches the b_dev and b_blkno entries
 * before calling the appropriate driver.  This would horribly
 * confuse, e.g. the hashing routines.
 */
dev_t	swapdev = makedev(4, 0);

/*
 * Routine that identifies /dev/mem and /dev/kmem.
 */
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
	/* 17 */	7,		/* sd */
	/* 18 */	NODEV,
	/* 19 */	NODEV,
	/* 20 */	NODEV,
	/* 21 */	NODEV,
	/* 22 */	NODEV,
	/* 23 */	NODEV,
	/* 24 */	NODEV,
	/* 25 */	NODEV,
	/* 26 */	12,		/* wd */
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
	/* 41 */	NODEV,
	/* 42 */	NODEV,
	/* 43 */	NODEV,
	/* 44 */	NODEV,
	/* 45 */	NODEV,
	/* 46 */	NODEV,
	/* 47 */	NODEV,
	/* 48 */	NODEV,
	/* 49 */	NODEV,
	/* 50 */	NODEV,
	/* 51 */	NODEV,
	/* 52 */	NODEV,
	/* 53 */	NODEV,
	/* 54 */	16,		/* fd */
	/* 55 */	NODEV,
	/* 56 */	NODEV,
	/* 57 */	NODEV,
	/* 58 */	18,		/* cd */
	/* 59 */	NODEV,
	/* 60 */	NODEV,
	/* 61 */	5,		/* rd */
	/* 62 */	NODEV,
	/* 63 */	NODEV,
	/* 64 */	NODEV,
	/* 65 */	NODEV,
	/* 66 */	NODEV,
	/* 67 */	NODEV,
	/* 68 */	NODEV,
	/* 69 */	NODEV,
	/* 70 */	NODEV,
	/* 71 */	NODEV,
	/* 72 */	NODEV,
	/* 73 */	NODEV,
	/* 74 */	NODEV,
	/* 75 */	NODEV,
	/* 76 */	NODEV,
	/* 77 */	NODEV,
	/* 78 */	NODEV,
	/* 79 */	NODEV,
	/* 80 */	NODEV,
	/* 81 */	NODEV,
	/* 82 */	NODEV,
	/* 83 */	NODEV,
	/* 84 */	NODEV,
	/* 85 */	NODEV,
	/* 86 */	NODEV,
	/* 87 */	NODEV,
	/* 88 */	NODEV,
	/* 89 */	NODEV,
	/* 90 */	NODEV,
	/* 91 */	NODEV,
	/* 92 */	NODEV,
	/* 93 */	NODEV,
	/* 94 */	NODEV,
	/* 95 */	NODEV,
	/* 96 */	NODEV,
	/* 97 */	NODEV,
	/* 98 */	NODEV,
	/* 99 */	NODEV,
	/*100 */	NODEV,
	/*101 */	NODEV,
	/*102 */	NODEV,
	/*103 */	NODEV,
	/*104 */	NODEV,
	/*105 */	NODEV,
	/*106 */	NODEV,
	/*107 */	NODEV,
	/*108 */	NODEV,
	/*109 */	NODEV,
	/*110 */	8,		/* vnd */
};
const int nchrtoblktbl = nitems(chrtoblktbl);
