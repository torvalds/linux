/*-
 * Copyright (c) 2013-2014 Robert N. M. Watson
 * All rights reserved.
 *
 * This software was developed by SRI International and the University of
 * Cambridge Computer Laboratory under DARPA/AFRL contract (FA8750-10-C-0237)
 * ("CTSRD"), as part of the DARPA CRASH research programme.
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
 * Copyright (c) 1998 Robert Nordier
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms are freely
 * permitted provided that the above copyright notice and this
 * paragraph and the following disclaimer are duplicated in all
 * such forms.
 *
 * This software is provided "AS IS" and without any express or
 * implied warranties, including, without limitation, the implied
 * warranties of merchantability and fitness for a particular
 * purpose.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/disklabel.h>
#include <sys/diskmbr.h>
#include <sys/dirent.h>
#include <sys/endian.h>
#include <sys/reboot.h>

#include <machine/bootinfo.h>
#include <machine/elf.h>

#include <stand.h>
#include <stdarg.h>
#include <string.h>

#include <beri.h>
#include <cfi.h>
#include <cons.h>
#include <mips.h>
#include <sdcard.h>

#include "paths.h"
#include "rbx.h"

static int		 beri_argc;
static const char	**beri_argv, **beri_envv;
static uint64_t		 beri_memsize;

#define IO_KEYBOARD	1
#define IO_SERIAL	2

#define SECOND		1	/* Circa that many ticks in a second. */

#define ARGS		0x900
#define NOPT		14
#define MEM_BASE	0x12
#define MEM_EXT 	0x15

/*
 * XXXRW: I think this has to do with whether boot2 expects a partition
 * table?
 */
#define DRV_HARD	0x80
#define DRV_MASK	0x7f

/* Default to using CFI flash. */
#define	TYPE_DEFAULT	BOOTINFO_DEV_TYPE_SDCARD

/* Hard-coded assumption about location of JTAG-loaded kernel. */
#define	DRAM_KERNEL_ADDR	((void *)mips_phys_to_cached(0x20000))

extern uint32_t _end;

static const char optstr[NOPT] = "DhaCcdgmnpqrsv"; /* Also 'P', 'S' */
static const unsigned char flags[NOPT] = {
    RBX_DUAL,
    RBX_SERIAL,
    RBX_ASKNAME,
    RBX_CDROM,
    RBX_CONFIG,
    RBX_KDB,
    RBX_GDB,
    RBX_MUTE,
    RBX_NOINTR,
    RBX_PAUSE,
    RBX_QUIET,
    RBX_DFLTROOT,
    RBX_SINGLE,
    RBX_VERBOSE
};

/* These must match BOOTINFO_DEV_TYPE constants. */
static const char *const dev_nm[] = {"dram", "cfi", "sdcard"};
static const u_int dev_nm_count = nitems(dev_nm);

static struct dsk {
    unsigned type;		/* BOOTINFO_DEV_TYPE_x object type. */
    uintptr_t unitptr;		/* Unit number or pointer to object. */
    uint8_t slice;
    uint8_t part;
#if 0
    unsigned start;
    int init;
#endif
} dsk;
static char cmd[512], cmddup[512], knamebuf[1024];
static const char *kname;
uint32_t opts;
#if 0
static int comspeed = SIOSPD;
#endif
struct bootinfo bootinfo;
static uint8_t ioctrl = IO_KEYBOARD;

void putchar(int);
static void boot_fromdram(void);
static void boot_fromfs(void);
static void load(void);
static int parse(void);
static int dskread(void *, unsigned, unsigned);
static int xputc(int);
static int xgetc(int);

#define	UFS_SMALL_CGBASE
#include "ufsread.c"

static struct dmadat __dmadat;

static inline int
xfsread(ufs_ino_t inode, void *buf, size_t nbyte)
{
    if ((size_t)fsread(inode, buf, nbyte) != nbyte) {
	printf("Invalid %s\n", "format");
	return -1;
    }
    return 0;
}

static inline void
getstr(void)
{
    char *s;
    int c;

    s = cmd;
    for (;;) {
	switch (c = xgetc(0)) {
	case 0:
	    break;
	case '\177':
	case '\b':
	    if (s > cmd) {
		s--;
		printf("\b \b");
	    }
	    break;
	case '\n':
	case '\r':
		putchar('\n');
	    *s = 0;
	    return;
	default:
	    if (s - cmd < sizeof(cmd) - 1)
		*s++ = c;
	    putchar(c);
	}
    }
}

int
main(u_int argc, const char *argv[], const char *envv[], uint64_t memsize)
{
    uint8_t autoboot;
    ufs_ino_t ino;
    size_t nbyte;

    /* Arguments from Miniboot. */
    beri_argc = argc;
    beri_argv = argv;
    beri_envv = envv;
    beri_memsize = memsize;

    dmadat = &__dmadat;
#if 0
    /* XXXRW: more here. */
    v86.ctl = V86_FLAGS;
    v86.efl = PSL_RESERVED_DEFAULT | PSL_I;
    dsk.drive = *(uint8_t *)PTOV(ARGS);
#endif
    dsk.type = TYPE_DEFAULT;
#if 0
    dsk.unit = dsk.drive & DRV_MASK;
    dsk.slice = *(uint8_t *)PTOV(ARGS + 1) + 1;
#endif
    bootinfo.bi_version = BOOTINFO_VERSION;
    bootinfo.bi_size = sizeof(bootinfo);

    /* Process configuration file */

    autoboot = 1;

    if ((ino = lookup(PATH_CONFIG)) ||
        (ino = lookup(PATH_DOTCONFIG))) {
	nbyte = fsread(ino, cmd, sizeof(cmd) - 1);
	cmd[nbyte] = '\0';
    }

    if (*cmd) {
	memcpy(cmddup, cmd, sizeof(cmd));
	if (parse())
	    autoboot = 0;
	if (!OPT_CHECK(RBX_QUIET))
	    printf("%s: %s", PATH_CONFIG, cmddup);
	/* Do not process this command twice */
	*cmd = 0;
    }

    /*
     * Try to exec stage 3 boot loader. If interrupted by a keypress,
     * or in case of failure, try to load a kernel directly instead.
     */

    if (!kname) {
	kname = PATH_LOADER;
	if (autoboot && !keyhit(3*SECOND)) {
	    boot_fromfs();
	    kname = PATH_KERNEL;
	}
    }

    /* Present the user with the boot2 prompt. */

    for (;;) {
	if (!autoboot || !OPT_CHECK(RBX_QUIET))
	    printf("\nFreeBSD/mips boot\n"
		   "Default: %s%ju:%s\n"
		   "boot: ",
		   dev_nm[dsk.type], dsk.unitptr, kname);
#if 0
	if (ioctrl & IO_SERIAL)
	    sio_flush();
#endif
	if (!autoboot || keyhit(3*SECOND))
	    getstr();
	else if (!autoboot || !OPT_CHECK(RBX_QUIET))
	    putchar('\n');
	autoboot = 0;
	if (parse())
	    putchar('\a');
	else
	    load();
    }
}

static void
boot(void *entryp, int argc, const char *argv[], const char *envv[])
{

    bootinfo.bi_kernelname = (bi_ptr_t)kname;
    bootinfo.bi_boot2opts = opts & RBX_MASK;
    bootinfo.bi_boot_dev_type = dsk.type;
    bootinfo.bi_boot_dev_unitptr = dsk.unitptr;
    bootinfo.bi_memsize = beri_memsize;
#if 0
    /*
     * XXXRW: A possible future way to distinguish Miniboot passing a memory
     * size vs DTB..?
     */
    if (beri_memsize <= BERI_MEMVSDTB)
	bootinfo.bi_memsize = beri_memsize;
    else
	bootinfo.bi_dtb = beri_memsize;
#endif
    ((void(*)(int, const char **, const char **, void *))entryp)(argc, argv,
      envv, &bootinfo);
}

/*
 * Boot a kernel that has mysteriously (i.e., by JTAG) appeared in DRAM;
 * assume that it is already properly relocated, etc, and invoke its entry
 * address without question or concern.
 */
static void
boot_fromdram(void)
{
    void *kaddr = DRAM_KERNEL_ADDR;	/* XXXRW: Something better here. */
    Elf64_Ehdr *ehp = kaddr;

    if (!IS_ELF(*ehp)) {
	printf("Invalid %s\n", "format");
	return;
    }
    boot((void *)ehp->e_entry, beri_argc, beri_argv, beri_envv);
}

static void
boot_fromfs(void)
{
    union {
	Elf64_Ehdr eh;
    } hdr;
    static Elf64_Phdr ep[2];
#if 0
    static Elf64_Shdr es[2];
#endif
    caddr_t p;
    ufs_ino_t ino;
    uint64_t addr;
    int i, j;

    if (!(ino = lookup(kname))) {
	if (!ls)
	    printf("No %s\n", kname);
	return;
    }
    if (xfsread(ino, &hdr, sizeof(hdr)))
	return;

    if (IS_ELF(hdr.eh)) {
	fs_off = hdr.eh.e_phoff;
	for (j = i = 0; i < hdr.eh.e_phnum && j < 2; i++) {
	    if (xfsread(ino, ep + j, sizeof(ep[0])))
		return;
	    if (ep[j].p_type == PT_LOAD)
		j++;
	}
	for (i = 0; i < 2; i++) {
	    p = (caddr_t)ep[i].p_paddr;
	    fs_off = ep[i].p_offset;
	    if (xfsread(ino, p, ep[i].p_filesz))
		return;
	}
	p += roundup2(ep[1].p_memsz, PAGE_SIZE);
#if 0
	bootinfo.bi_symtab = VTOP(p);
	if (hdr.eh.e_shnum == hdr.eh.e_shstrndx + 3) {
	    fs_off = hdr.eh.e_shoff + sizeof(es[0]) *
		(hdr.eh.e_shstrndx + 1);
	    if (xfsread(ino, &es, sizeof(es)))
		return;
	    for (i = 0; i < 2; i++) {
		*(Elf32_Word *)p = es[i].sh_size;
		p += sizeof(es[i].sh_size);
		fs_off = es[i].sh_offset;
		if (xfsread(ino, p, es[i].sh_size))
		    return;
		p += es[i].sh_size;
	    }
	}
#endif
	addr = hdr.eh.e_entry;
#if 0
	bootinfo.bi_esymtab = VTOP(p);
#endif
    } else {
	printf("Invalid %s\n", "format");
	return;
    }
    boot((void *)addr, beri_argc, beri_argv, beri_envv);
}

static void
load(void)
{

	switch (dsk.type) {
	case BOOTINFO_DEV_TYPE_DRAM:
		boot_fromdram();
		break;

	default:
		boot_fromfs();
		break;
	}
}

static int
parse()
{
    char *arg = cmd;
    char *ep, *p, *q;
    char unit;
    size_t len;
    const char *cp;
#if 0
    int c, i, j;
#else
    int c, i;
#endif

    while ((c = *arg++)) {
	if (c == ' ' || c == '\t' || c == '\n')
	    continue;
	for (p = arg; *p && *p != '\n' && *p != ' ' && *p != '\t'; p++);
	ep = p;
	if (*p)
	    *p++ = 0;
	if (c == '-') {
	    while ((c = *arg++)) {
		if (c == 'P') {
			cp = "yes";
#if 0
		    } else {
			opts |= OPT_SET(RBX_DUAL) | OPT_SET(RBX_SERIAL);
			cp = "no";
		    }
#endif
		    printf("Keyboard: %s\n", cp);
		    continue;
#if 0
		} else if (c == 'S') {
		    j = 0;
		    while ((unsigned int)(i = *arg++ - '0') <= 9)
			j = j * 10 + i;
		    if (j > 0 && i == -'0') {
			comspeed = j;
			break;
		    }
		    /* Fall through to error below ('S' not in optstr[]). */
#endif
		}
		for (i = 0; c != optstr[i]; i++)
		    if (i == NOPT - 1)
			return -1;
		opts ^= OPT_SET(flags[i]);
	    }
	    ioctrl = OPT_CHECK(RBX_DUAL) ? (IO_SERIAL|IO_KEYBOARD) :
		     OPT_CHECK(RBX_SERIAL) ? IO_SERIAL : IO_KEYBOARD;
#if 0
	    if (ioctrl & IO_SERIAL) {
	        if (sio_init(115200 / comspeed) != 0)
		    ioctrl &= ~IO_SERIAL;
	    }
#endif
	} else {
	    /*-
	     * Parse a device/kernel name.  Format(s):
	     *
	     *   path
	     *   deviceX:path
	     *
	     * NB: Utterly incomprehensible but space-efficient ARM/i386
	     * parsing removed in favour of larger but easier-to-read C.  This
	     * is still not great, however -- e.g., relating to unit handling.
	     *
	     * TODO: it would be nice if a DRAM pointer could be specified
	     * here.
	     *
	     * XXXRW: Pick up pieces here.
	     */

	    /*
	     * Search for a parens; if none, then it's just a path.
	     * Otherwise, it's a devicename.
	     */
	    arg--;
	    q = strsep(&arg, ":");
	    if (arg != NULL) {
		len = strlen(q);
		if (len < 2) {
		    printf("Invalid device: name too short\n");
		    return (-1);
		}

		/*
		 * First, handle one-digit unit.
		 */
		unit = q[len-1];
		if (unit < '0' || unit > '9') {
		    printf("Invalid device: invalid unit %c\n",
		      unit);
		    return (-1);
		}
		unit -= '0';
		q[len-1] = '\0';

		/*
		 * Next, find matching device.
		 */
		for (i = 0; i < dev_nm_count; i++) {
		    if (strcmp(q, dev_nm[i]) == 0)
			break;
		}
		if (i == dev_nm_count) {
		    printf("Invalid device: no driver match\n");
		    return (-1);
		}
		dsk.type = i;
		dsk.unitptr = unit;	/* Someday: also a DRAM pointer? */
	    } else
		arg = q;
	    if ((i = ep - arg)) {
		if ((size_t)i >= sizeof(knamebuf))
		    return -1;
		memcpy(knamebuf, arg, i + 1);
		kname = knamebuf;
	    }
	}
	arg = p;
    }
    return 0;
}

static int
drvread(void *buf, unsigned lba, unsigned nblk)
{

	/* XXXRW: eventually, we may want to pass 'drive' and 'unit' here. */
	switch (dsk.type) {
	case BOOTINFO_DEV_TYPE_CFI:
		return (cfi_read(buf, lba, nblk));

	case BOOTINFO_DEV_TYPE_SDCARD:
		return (altera_sdcard_read(buf, lba, nblk));

	default:
		return (-1);
	}
}

static int
dskread(void *buf, unsigned lba, unsigned nblk)
{
#if 0
    /*
     * XXXRW: For now, assume no partition table around the file system; it's
     * just in raw flash.
     */
    struct dos_partition *dp;
    struct disklabel *d;
    char *sec;
    unsigned i;
    uint8_t sl;

    if (!dsk_meta) {
	sec = dmadat->secbuf;
	dsk.start = 0;
	if (drvread(sec, DOSBBSECTOR, 1))
	    return -1;
	dp = (void *)(sec + DOSPARTOFF);
	sl = dsk.slice;
	if (sl < BASE_SLICE) {
	    for (i = 0; i < NDOSPART; i++)
		if (dp[i].dp_typ == DOSPTYP_386BSD &&
		    (dp[i].dp_flag & 0x80 || sl < BASE_SLICE)) {
		    sl = BASE_SLICE + i;
		    if (dp[i].dp_flag & 0x80 ||
			dsk.slice == COMPATIBILITY_SLICE)
			break;
		}
	    if (dsk.slice == WHOLE_DISK_SLICE)
		dsk.slice = sl;
	}
	if (sl != WHOLE_DISK_SLICE) {
	    if (sl != COMPATIBILITY_SLICE)
		dp += sl - BASE_SLICE;
	    if (dp->dp_typ != DOSPTYP_386BSD) {
		printf("Invalid %s\n", "slice");
		return -1;
	    }
	    dsk.start = le32toh(dp->dp_start);
	}
	if (drvread(sec, dsk.start + LABELSECTOR, 1))
		return -1;
	d = (void *)(sec + LABELOFFSET);
	if (le32toh(d->d_magic) != DISKMAGIC ||
	    le32toh(d->d_magic2) != DISKMAGIC) {
	    if (dsk.part != RAW_PART) {
		printf("Invalid %s\n", "label");
		return -1;
	    }
	} else {
	    if (!dsk.init) {
		if (le16toh(d->d_type) == DTYPE_SCSI)
		    dsk.type = TYPE_DA;
		dsk.init++;
	    }
	    if (dsk.part >= le16toh(d->d_npartitions) ||
		!(le32toh(d->d_partitions[dsk.part].p_size))) {
		printf("Invalid %s\n", "partition");
		return -1;
	    }
	    dsk.start += le32toh(d->d_partitions[dsk.part].p_offset);
	    dsk.start -= le32toh(d->d_partitions[RAW_PART].p_offset);
	}
    }
    return drvread(buf, dsk.start + lba, nblk);
#else
    return drvread(buf, lba, nblk);
#endif
}

void
putchar(int c)
{
    if (c == '\n')
	xputc('\r');
    xputc(c);
}

static int
xputc(int c)
{
    if (ioctrl & IO_KEYBOARD)
	beri_putc(c);
#if 0
    if (ioctrl & IO_SERIAL)
	sio_putc(c);
#endif
    return c;
}

static int
xgetc(int fn)
{
    if (OPT_CHECK(RBX_NOINTR))
	return 0;
    for (;;) {
	if (ioctrl & IO_KEYBOARD && keyhit(0))
	    return fn ? 1 : beri_getc();
#if 0
	if (ioctrl & IO_SERIAL && sio_ischar())
	    return fn ? 1 : sio_getc();
#endif
	if (fn)
	    return 0;
    }
}

int
getchar(void)
{

	return xgetc(0);
}

void
exit(int code)
{

        printf("error: loader exit\n");
        while (1);
        __unreachable();
}
