/*-
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
#include <sys/gpt.h>
#include <sys/dirent.h>
#include <sys/reboot.h>

#include <machine/bootinfo.h>
#include <machine/elf.h>
#include <machine/pc/bios.h>
#include <machine/psl.h>

#include <stdarg.h>

#include <a.out.h>

#include <btxv86.h>

#include "stand.h"

#include "bootargs.h"
#include "lib.h"
#include "rbx.h"
#include "drv.h"
#include "cons.h"
#include "gpt.h"
#include "paths.h"

#define ARGS		0x900
#define NOPT		14
#define NDEV		3
#define MEM_BASE	0x12
#define MEM_EXT 	0x15

#define DRV_HARD	0x80
#define DRV_MASK	0x7f

#define TYPE_AD		0
#define TYPE_DA		1
#define TYPE_MAXHARD	TYPE_DA
#define TYPE_FD		2

extern uint32_t _end;

static const uuid_t freebsd_ufs_uuid = GPT_ENT_TYPE_FREEBSD_UFS;
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
uint32_t opts;

static const char *const dev_nm[NDEV] = {"ad", "da", "fd"};
static const unsigned char dev_maj[NDEV] = {30, 4, 2};

static char kname[1024];
static int comspeed = SIOSPD;
static struct bootinfo bootinfo;
#ifdef LOADER_GELI_SUPPORT
static struct geli_boot_args geliargs;
#endif

static vm_offset_t	high_heap_base;
static uint32_t		bios_basemem, bios_extmem, high_heap_size;

static struct bios_smap smap;

/*
 * The minimum amount of memory to reserve in bios_extmem for the heap.
 */
#define	HEAP_MIN	(3 * 1024 * 1024)

static char *heap_next;
static char *heap_end;

static void load(void);
static int parse_cmds(char *, int *);
static int dskread(void *, daddr_t, unsigned);
#ifdef LOADER_GELI_SUPPORT
static int vdev_read(void *vdev __unused, void *priv, off_t off, void *buf,
	size_t bytes);
#endif

#include "ufsread.c"
#include "gpt.c"
#ifdef LOADER_GELI_SUPPORT
#include "geliboot.h"
static char gelipw[GELI_PW_MAXLEN];
#endif

struct gptdsk {
	struct dsk       dsk;
#ifdef LOADER_GELI_SUPPORT
	struct geli_dev *gdev;
#endif
};

static struct gptdsk gdsk;

static inline int
xfsread(ufs_ino_t inode, void *buf, size_t nbyte)
{

	if ((size_t)fsread(inode, buf, nbyte) != nbyte) {
		printf("Invalid %s\n", "format");
		return (-1);
	}
	return (0);
}

static void
bios_getmem(void)
{
	uint64_t size;

	/* Parse system memory map */
	v86.ebx = 0;
	do {
		v86.ctl = V86_FLAGS;
		v86.addr = MEM_EXT;		/* int 0x15 function 0xe820*/
		v86.eax = 0xe820;
		v86.ecx = sizeof(struct bios_smap);
		v86.edx = SMAP_SIG;
		v86.es = VTOPSEG(&smap);
		v86.edi = VTOPOFF(&smap);
		v86int();
		if ((v86.efl & 1) || (v86.eax != SMAP_SIG))
			break;
		/* look for a low-memory segment that's large enough */
		if ((smap.type == SMAP_TYPE_MEMORY) && (smap.base == 0) &&
		    (smap.length >= (512 * 1024)))
			bios_basemem = smap.length;
		/* look for the first segment in 'extended' memory */
		if ((smap.type == SMAP_TYPE_MEMORY) &&
		    (smap.base == 0x100000)) {
			bios_extmem = smap.length;
		}

		/*
		 * Look for the largest segment in 'extended' memory beyond
		 * 1MB but below 4GB.
		 */
		if ((smap.type == SMAP_TYPE_MEMORY) &&
		    (smap.base > 0x100000) && (smap.base < 0x100000000ull)) {
			size = smap.length;

			/*
			 * If this segment crosses the 4GB boundary,
			 * truncate it.
			 */
			if (smap.base + size > 0x100000000ull)
				size = 0x100000000ull - smap.base;

			if (size > high_heap_size) {
				high_heap_size = size;
				high_heap_base = smap.base;
			}
		}
	} while (v86.ebx != 0);

	/* Fall back to the old compatibility function for base memory */
	if (bios_basemem == 0) {
		v86.ctl = 0;
		v86.addr = 0x12;		/* int 0x12 */
		v86int();

		bios_basemem = (v86.eax & 0xffff) * 1024;
	}

	/*
	 * Fall back through several compatibility functions for extended
	 * memory
	 */
	if (bios_extmem == 0) {
		v86.ctl = V86_FLAGS;
		v86.addr = 0x15;		/* int 0x15 function 0xe801*/
		v86.eax = 0xe801;
		v86int();
		if (!(v86.efl & 1)) {
			bios_extmem = ((v86.ecx & 0xffff) +
			    ((v86.edx & 0xffff) * 64)) * 1024;
		}
	}
	if (bios_extmem == 0) {
		v86.ctl = 0;
		v86.addr = 0x15;		/* int 0x15 function 0x88*/
		v86.eax = 0x8800;
		v86int();
		bios_extmem = (v86.eax & 0xffff) * 1024;
	}

	/*
	 * If we have extended memory and did not find a suitable heap
	 * region in the SMAP, use the last 3MB of 'extended' memory as a
	 * high heap candidate.
	 */
	if (bios_extmem >= HEAP_MIN && high_heap_size < HEAP_MIN) {
		high_heap_size = HEAP_MIN;
		high_heap_base = bios_extmem + 0x100000 - HEAP_MIN;
	}
}

static int
gptinit(void)
{

	if (gptread(&freebsd_ufs_uuid, &gdsk.dsk, dmadat->secbuf) == -1) {
		printf("%s: unable to load GPT\n", BOOTPROG);
		return (-1);
	}
	if (gptfind(&freebsd_ufs_uuid, &gdsk.dsk, gdsk.dsk.part) == -1) {
		printf("%s: no UFS partition was found\n", BOOTPROG);
		return (-1);
	}
#ifdef LOADER_GELI_SUPPORT
	gdsk.gdev = geli_taste(vdev_read, &gdsk.dsk, 
	    (gpttable[curent].ent_lba_end - gpttable[curent].ent_lba_start),
	    "disk%up%u:", gdsk.dsk.unit, curent + 1);
	if (gdsk.gdev != NULL) {
		if (geli_havekey(gdsk.gdev) != 0 &&
		    geli_passphrase(gdsk.gdev, gelipw) != 0) {
			printf("%s: unable to decrypt GELI key\n", BOOTPROG);
			return (-1);
		}
	}
#endif

	dsk_meta = 0;
	return (0);
}

int main(void);

int
main(void)
{
	char cmd[512], cmdtmp[512];
	ssize_t sz;
	int autoboot, dskupdated;
	ufs_ino_t ino;

	dmadat = (void *)(roundup2(__base + (int32_t)&_end, 0x10000) - __base);

	bios_getmem();

	if (high_heap_size > 0) {
		heap_end = PTOV(high_heap_base + high_heap_size);
		heap_next = PTOV(high_heap_base);
	} else {
		heap_next = (char *)dmadat + sizeof(*dmadat);
		heap_end = (char *)PTOV(bios_basemem);
	}
	setheap(heap_next, heap_end);

	v86.ctl = V86_FLAGS;
	v86.efl = PSL_RESERVED_DEFAULT | PSL_I;
	gdsk.dsk.drive = *(uint8_t *)PTOV(ARGS);
	gdsk.dsk.type = gdsk.dsk.drive & DRV_HARD ? TYPE_AD : TYPE_FD;
	gdsk.dsk.unit = gdsk.dsk.drive & DRV_MASK;
	gdsk.dsk.part = -1;
	gdsk.dsk.start = 0;
	bootinfo.bi_version = BOOTINFO_VERSION;
	bootinfo.bi_size = sizeof(bootinfo);
	bootinfo.bi_basemem = bios_basemem / 1024;
	bootinfo.bi_extmem = bios_extmem / 1024;
	bootinfo.bi_memsizes_valid++;
	bootinfo.bi_bios_dev = gdsk.dsk.drive;

	/* Process configuration file */

	if (gptinit() != 0)
		return (-1);

	autoboot = 1;
	*cmd = '\0';

	for (;;) {
		*kname = '\0';
		if ((ino = lookup(PATH_CONFIG)) ||
		    (ino = lookup(PATH_DOTCONFIG))) {
			sz = fsread(ino, cmd, sizeof(cmd) - 1);
			cmd[(sz < 0) ? 0 : sz] = '\0';
		}
		if (*cmd != '\0') {
			memcpy(cmdtmp, cmd, sizeof(cmdtmp));
			if (parse_cmds(cmdtmp, &dskupdated))
				break;
			if (dskupdated && gptinit() != 0)
				break;
			if (!OPT_CHECK(RBX_QUIET))
				printf("%s: %s", PATH_CONFIG, cmd);
			*cmd = '\0';
		}

		if (autoboot && keyhit(3)) {
			if (*kname == '\0')
				memcpy(kname, PATH_LOADER, sizeof(PATH_LOADER));
			break;
		}
		autoboot = 0;

		/*
		 * Try to exec stage 3 boot loader. If interrupted by a
		 * keypress, or in case of failure, try to load a kernel
		 * directly instead.
		 */
		if (*kname != '\0')
			load();
		memcpy(kname, PATH_LOADER, sizeof(PATH_LOADER));
		load();
		memcpy(kname, PATH_KERNEL, sizeof(PATH_KERNEL));
		load();
		gptbootfailed(&gdsk.dsk);
		if (gptfind(&freebsd_ufs_uuid, &gdsk.dsk, -1) == -1)
			break;
		dsk_meta = 0;
	}

	/* Present the user with the boot2 prompt. */

	for (;;) {
		if (!OPT_CHECK(RBX_QUIET)) {
			printf("\nFreeBSD/x86 boot\n"
			    "Default: %u:%s(%up%u)%s\n"
			    "boot: ",
			    gdsk.dsk.drive & DRV_MASK, dev_nm[gdsk.dsk.type],
			    gdsk.dsk.unit, gdsk.dsk.part, kname);
		}
		if (ioctrl & IO_SERIAL)
			sio_flush();
		*cmd = '\0';
		if (keyhit(0))
			getstr(cmd, sizeof(cmd));
		else if (!OPT_CHECK(RBX_QUIET))
			putchar('\n');
		if (parse_cmds(cmd, &dskupdated)) {
			putchar('\a');
			continue;
		}
		if (dskupdated && gptinit() != 0)
			continue;
		load();
	}
	/* NOTREACHED */
}

/* XXX - Needed for btxld to link the boot2 binary; do not remove. */
void
exit(int x)
{

	while (1);
	__unreachable();
}

static void
load(void)
{
	union {
		struct exec ex;
		Elf32_Ehdr eh;
	} hdr;
	static Elf32_Phdr ep[2];
	static Elf32_Shdr es[2];
	caddr_t p;
	ufs_ino_t ino;
	uint32_t addr, x;
	int fmt, i, j;

	if (!(ino = lookup(kname))) {
		if (!ls) {
			printf("%s: No %s on %u:%s(%up%u)\n", BOOTPROG,
			    kname, gdsk.dsk.drive & DRV_MASK,
			    dev_nm[gdsk.dsk.type], gdsk.dsk.unit,
			    gdsk.dsk.part);
		}
		return;
	}
	if (xfsread(ino, &hdr, sizeof(hdr)))
		return;
	if (N_GETMAGIC(hdr.ex) == ZMAGIC)
		fmt = 0;
	else if (IS_ELF(hdr.eh))
		fmt = 1;
	else {
		printf("Invalid %s\n", "format");
		return;
	}
	if (fmt == 0) {
		addr = hdr.ex.a_entry & 0xffffff;
		p = PTOV(addr);
		fs_off = PAGE_SIZE;
		if (xfsread(ino, p, hdr.ex.a_text))
			return;
		p += roundup2(hdr.ex.a_text, PAGE_SIZE);
		if (xfsread(ino, p, hdr.ex.a_data))
			return;
		p += hdr.ex.a_data + roundup2(hdr.ex.a_bss, PAGE_SIZE);
		bootinfo.bi_symtab = VTOP(p);
		memcpy(p, &hdr.ex.a_syms, sizeof(hdr.ex.a_syms));
		p += sizeof(hdr.ex.a_syms);
		if (hdr.ex.a_syms) {
			if (xfsread(ino, p, hdr.ex.a_syms))
				return;
			p += hdr.ex.a_syms;
			if (xfsread(ino, p, sizeof(int)))
				return;
			x = *(uint32_t *)p;
			p += sizeof(int);
			x -= sizeof(int);
			if (xfsread(ino, p, x))
				return;
			p += x;
		}
	} else {
		fs_off = hdr.eh.e_phoff;
		for (j = i = 0; i < hdr.eh.e_phnum && j < 2; i++) {
			if (xfsread(ino, ep + j, sizeof(ep[0])))
				return;
			if (ep[j].p_type == PT_LOAD)
				j++;
		}
		for (i = 0; i < 2; i++) {
			p = PTOV(ep[i].p_paddr & 0xffffff);
			fs_off = ep[i].p_offset;
			if (xfsread(ino, p, ep[i].p_filesz))
				return;
		}
		p += roundup2(ep[1].p_memsz, PAGE_SIZE);
		bootinfo.bi_symtab = VTOP(p);
		if (hdr.eh.e_shnum == hdr.eh.e_shstrndx + 3) {
			fs_off = hdr.eh.e_shoff + sizeof(es[0]) *
			    (hdr.eh.e_shstrndx + 1);
			if (xfsread(ino, &es, sizeof(es)))
				return;
			for (i = 0; i < 2; i++) {
				memcpy(p, &es[i].sh_size,
				    sizeof(es[i].sh_size));
				p += sizeof(es[i].sh_size);
				fs_off = es[i].sh_offset;
				if (xfsread(ino, p, es[i].sh_size))
					return;
				p += es[i].sh_size;
			}
		}
		addr = hdr.eh.e_entry & 0xffffff;
	}
	bootinfo.bi_esymtab = VTOP(p);
	bootinfo.bi_kernelname = VTOP(kname);
	bootinfo.bi_bios_dev = gdsk.dsk.drive;
#ifdef LOADER_GELI_SUPPORT
	geliargs.size = sizeof(geliargs);
	explicit_bzero(gelipw, sizeof(gelipw));
	export_geli_boot_data(&geliargs.gelidata);
#endif
	/*
	 * Note that the geliargs struct is passed by value, not by pointer.
	 * Code in btxldr.S copies the values from the entry stack to a fixed
	 * location within loader(8) at startup due to the presence of the
	 * KARGS_FLAGS_EXTARG flag.
	 */
	__exec((caddr_t)addr, RB_BOOTINFO | (opts & RBX_MASK),
	    MAKEBOOTDEV(dev_maj[gdsk.dsk.type], gdsk.dsk.part + 1, gdsk.dsk.unit, 0xff),
#ifdef LOADER_GELI_SUPPORT
	    KARGS_FLAGS_GELI | KARGS_FLAGS_EXTARG, 0, 0, VTOP(&bootinfo), geliargs
#else
	    0, 0, 0, VTOP(&bootinfo)
#endif
	    );
}

static int
parse_cmds(char *cmdstr, int *dskupdated)
{
	char *arg;
	char *ep, *p, *q;
	const char *cp;
	unsigned int drv;
	int c, i, j;

	arg = cmdstr;
	*dskupdated = 0;
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
					if (*(uint8_t *)PTOV(0x496) & 0x10) {
						cp = "yes";
					} else {
						opts |= OPT_SET(RBX_DUAL) |
						    OPT_SET(RBX_SERIAL);
						cp = "no";
					}
					printf("Keyboard: %s\n", cp);
					continue;
				} else if (c == 'S') {
					j = 0;
					while ((unsigned int)(i = *arg++ - '0')
					    <= 9)
						j = j * 10 + i;
					if (j > 0 && i == -'0') {
						comspeed = j;
						break;
					}
					/*
					 * Fall through to error below
					 * ('S' not in optstr[]).
					 */
				}
				for (i = 0; c != optstr[i]; i++)
					if (i == NOPT - 1)
						return (-1);
				opts ^= OPT_SET(flags[i]);
			}
			ioctrl = OPT_CHECK(RBX_DUAL) ? (IO_SERIAL|IO_KEYBOARD) :
			    OPT_CHECK(RBX_SERIAL) ? IO_SERIAL : IO_KEYBOARD;
			if (ioctrl & IO_SERIAL) {
				if (sio_init(115200 / comspeed) != 0)
					ioctrl &= ~IO_SERIAL;
			}
		} else {
			for (q = arg--; *q && *q != '('; q++);
			if (*q) {
				drv = -1;
				if (arg[1] == ':') {
					drv = *arg - '0';
					if (drv > 9)
						return (-1);
					arg += 2;
				}
				if (q - arg != 2)
					return (-1);
				for (i = 0; arg[0] != dev_nm[i][0] ||
				    arg[1] != dev_nm[i][1]; i++)
					if (i == NDEV - 1)
						return (-1);
				gdsk.dsk.type = i;
				arg += 3;
				gdsk.dsk.unit = *arg - '0';
				if (arg[1] != 'p' || gdsk.dsk.unit > 9)
					return (-1);
				arg += 2;
				gdsk.dsk.part = *arg - '0';
				if (gdsk.dsk.part < 1 || gdsk.dsk.part > 9)
					return (-1);
				arg++;
				if (arg[0] != ')')
					return (-1);
				arg++;
				if (drv == -1)
					drv = gdsk.dsk.unit;
				gdsk.dsk.drive = (gdsk.dsk.type <= TYPE_MAXHARD
				    ? DRV_HARD : 0) + drv;
				*dskupdated = 1;
			}
			if ((i = ep - arg)) {
				if ((size_t)i >= sizeof(kname))
					return (-1);
				memcpy(kname, arg, i + 1);
			}
		}
		arg = p;
	}
	return (0);
}

static int
dskread(void *buf, daddr_t lba, unsigned nblk)
{
	int err;

	err = drvread(&gdsk.dsk, buf, lba + gdsk.dsk.start, nblk);

#ifdef LOADER_GELI_SUPPORT
	if (err == 0 && gdsk.gdev != NULL) {
		/* Decrypt */
		if (geli_read(gdsk.gdev, lba * DEV_BSIZE, buf,
		    nblk * DEV_BSIZE))
			return (err);
	}
#endif

	return (err);
}

#ifdef LOADER_GELI_SUPPORT
/*
 * Read function compatible with the ZFS callback, required to keep the GELI
 * implementation the same for both UFS and ZFS.
 */
static int
vdev_read(void *vdev __unused, void *priv, off_t off, void *buf, size_t bytes)
{
	char *p;
	daddr_t lba;
	unsigned int nb;
	struct gptdsk *dskp;

	dskp = (struct gptdsk *)priv;

	if ((off & (DEV_BSIZE - 1)) || (bytes & (DEV_BSIZE - 1)))
		return (-1);

	p = buf;
	lba = off / DEV_BSIZE;
	lba += dskp->dsk.start;

	while (bytes > 0) {
		nb = bytes / DEV_BSIZE;
		if (nb > VBLKSIZE / DEV_BSIZE)
			nb = VBLKSIZE / DEV_BSIZE;
		if (drvread(&dskp->dsk, dmadat->blkbuf, lba, nb))
			return (-1);
		memcpy(p, dmadat->blkbuf, nb * DEV_BSIZE);
		p += nb * DEV_BSIZE;
		lba += nb;
		bytes -= nb * DEV_BSIZE;
	}

	return (0);
}
#endif /* LOADER_GELI_SUPPORT */
