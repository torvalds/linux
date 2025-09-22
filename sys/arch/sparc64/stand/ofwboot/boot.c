/*	$OpenBSD: boot.c,v 1.44 2024/03/26 14:46:48 claudio Exp $	*/
/*	$NetBSD: boot.c,v 1.3 2001/05/31 08:55:19 mrg Exp $	*/
/*
 * Copyright (c) 1997, 1999 Eduardo E. Horvath.  All rights reserved.
 * Copyright (c) 1997 Jason R. Thorpe.  All rights reserved.
 * Copyright (C) 1995, 1996 Wolfgang Solfrank.
 * Copyright (C) 1995, 1996 TooLs GmbH.
 * All rights reserved.
 *
 * ELF support derived from NetBSD/alpha's boot loader, written
 * by Christopher G. Demetriou.
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
 *	This product includes software developed by TooLs GmbH.
 * 4. The name of TooLs GmbH may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY TOOLS GMBH ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL TOOLS GMBH BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
 * OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
 * ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * First try for the boot code
 *
 * Input syntax is:
 *	[promdev[{:|,}partition]]/[filename] [flags]
 */

#define ELFSIZE 64

#include <lib/libsa/stand.h>
#include <lib/libkern/funcs.h>

#include <sys/param.h>
#include <sys/exec.h>
#include <sys/exec_elf.h>
#include <sys/reboot.h>
#include <sys/disklabel.h>

#include <machine/cpu.h>
#include <lib/libsa/arc4.h>

#ifdef SOFTRAID
#include <sys/queue.h>
#include <dev/biovar.h>
#include <dev/softraidvar.h>
#include <lib/libsa/softraid.h>

#include "disk.h"
#include "softraid_sparc64.h"
#endif

#include "ofdev.h"
#include "openfirm.h"

#ifdef BOOT_DEBUG
uint32_t	boot_debug = 0
		    /* | BOOT_D_OFDEV */
		    /* | BOOT_D_OFNET */
		;
#endif

#define	MEG	(1024*1024)

/*
 * Boot device is derived from ROM provided information, or if there is none,
 * this list is used in sequence, to find a kernel.
 */
char *kernels[] = {
	"bsd",
	NULL
};

char bootdev[128];
extern char bootfile[128];
int boothowto;
int debug;

char rnddata[BOOTRANDOM_MAX];
struct rc4_ctx randomctx;

int	elf64_exec(int, Elf64_Ehdr *, u_int64_t *, void **, void **);

/*
 *	parse:
 *		[kernel-name] [-options]
 *	leave kernel-name in passed-in string
 *	put options into *howtop
 *	return -1 iff syntax error (no - before options)
 */

static int
parseargs(char *str, int *howtop)
{
	char *cp;

	*howtop = 0;
	cp = str;
	while (*cp == ' ')
		++cp;
	if (*cp != '-') {
		while (*cp && *cp != ' ')
			*str++ = *cp++;
		while (*cp == ' ')
			++cp;
	}
	/*
	 * Note that, if only options have been passed, without a kernel
	 * name, str == cp and options will be ignored at the boot blocks
	 * level.
	 * This a feature intended to make `boot -a' behave as intended.
	 * If you want the bootblocks to handle arguments explicitly, a
	 * kernel filename needs to be provided (as in `boot bsd -a').
	 */
	*str = 0;
	switch (*cp) {
	default:
		printf("boot options string <%s> must start with -\n", cp);
		return -1;
	case 0:
		return 0;
	case '-':
		break;
	}

	++cp;
	while (*cp) {
		switch (*cp++) {
		case 'a':
			*howtop |= RB_ASKNAME;
			break;
		case 'd':
			if (!debug) debug = 1;
			break;
		case 'D':
			debug = 2;
			break;
		}
	}
	return 0;
}


static void
chain(u_int64_t pentry, char *args, void *ssym, void *esym)
{
	extern char end[];
	void (*entry)();
	int l, machine_tag;
	long newargs[3];

	entry = (void *)(long)pentry;

	/*
	 * When we come in args consists of a pointer to the boot
	 * string.  We need to fix it so it takes into account
	 * other params such as romp.
	 */

	/*
	 * Stash pointer to end of symbol table after the argument
	 * strings.
	 */
	l = strlen(args) + 1;
	bcopy(&esym, args + l, sizeof(esym));
	l += sizeof(esym);

	/*
	 * Tell the kernel we're an OpenFirmware system.
	 */
#define SPARC_MACHINE_OPENFIRMWARE		0x44444230
	machine_tag = SPARC_MACHINE_OPENFIRMWARE;
	bcopy(&machine_tag, args + l, sizeof(machine_tag));
	l += sizeof(machine_tag);

	/*
	 * Since we don't need the boot string (we can get it from /chosen)
	 * we won't pass it in.  Just pass in esym and magic #
	 */
	newargs[0] = SPARC_MACHINE_OPENFIRMWARE;
	newargs[1] = (long)esym;
	newargs[2] = (long)ssym;
	args = (char *)newargs;
	l = sizeof(newargs);

#ifdef DEBUG
	printf("chain: calling OF_chain(%x, %x, %x, %x, %x)\n",
	    (void *)RELOC, end - (char *)RELOC, entry, args, l);
#endif
	/* if -D is set then pause in the PROM. */
	if (debug > 1) OF_enter();
	OF_chain((void *)RELOC, ((end - (char *)RELOC)+PAGE_SIZE)%PAGE_SIZE,
	    entry, args, l);
	panic("chain");
}

int
loadfile(int fd, char *args, int isupgrade)
{
	union {
		Elf64_Ehdr elf64;
	} hdr;
	int rval;
	u_int64_t entry = 0;
	void *ssym;
	void *esym;

	ssym = NULL;
	esym = NULL;

	/* Load the header. */
#ifdef DEBUG
	printf("loadfile: reading header\n");
#endif
	if ((rval = read(fd, &hdr, sizeof(hdr))) != sizeof(hdr)) {
		if (rval == -1)
			printf("read header: %s\n", strerror(errno));
		else
			printf("read header: short read (only %d of %d)\n",
			    rval, sizeof(hdr));
		rval = 1;
		goto err;
	}

	/* Determine file type, load kernel. */
	if (bcmp(hdr.elf64.e_ident, ELFMAG, SELFMAG) == 0 &&
	    hdr.elf64.e_ident[EI_CLASS] == ELFCLASS64) {
		printf("Booting %s\n", opened_name);
		rval = elf64_exec(fd, &hdr.elf64, &entry, &ssym, &esym);
	} else {
		rval = 1;
		printf("unknown executable format\n");
	}

	if (rval)
		goto err;

	printf(" start=0x%lx\n", (unsigned long)entry);

	/* Prevent re-upgrade: chmod a-x bsd.upgrade */
	if (isupgrade) {
		struct stat st;

		if (fstat(fd, &st) == 0) {
			st.st_mode &= ~(S_IXUSR|S_IXGRP|S_IXOTH);
			if (fchmod(fd, st.st_mode) == -1)
				printf("fchmod a-x %s: failed\n", opened_name);
		}
	}
	close(fd);

#ifdef SOFTRAID
	if (bootdev_dip)
		OF_close(bootdev_dip->sr_handle);
	sr_clear_keys();
#endif
	chain(entry, args, ssym, esym);
	/* NOTREACHED */

 err:
	close(fd);
	return (rval);
}

static int
upgrade(void)
{
	struct stat sb;

	if (stat("/bsd.upgrade", &sb) < 0)
		return 0;
	if ((sb.st_mode & S_IXUSR) == 0) {
		printf("/bsd.upgrade is not u+x\n");
		return 0;
        }
	return 1;
}

int
loadrandom(char *path, char *buf, size_t buflen)
{
	struct stat sb;
	int fd, error = 0;

	fd = open(path, O_RDONLY);
	if (fd == -1)
		return -1;
	if (fstat(fd, &sb) == -1) {
		error = -1;
		goto done;
	}
	if (read(fd, buf, buflen) != buflen) {
		error = -1;
		goto done;
	}
	if (sb.st_mode & S_ISTXT) {
		printf("NOTE: random seed is being reused.\n");
		error = -1;
		goto done;
	}
	fchmod(fd, sb.st_mode | S_ISTXT);
done:
	close(fd);
	return (error);
}

#ifdef SOFTRAID
/*
 * Set bootdev_dip to the softraid boot volume, if specified.
 * Otherwise default to the softraid volume on the boot device, if any.
 */
static int
srbootdev(const char *bootline)
{
	struct sr_boot_volume *bv;
	int unit;

	bootdev_dip = NULL;

	/*
	 * Look for softraid disks in bootline.
	 * E.g. 'sr0', 'sr0:bsd', or 'sr0a:/bsd'
	 */
	if (bootline[0] == 's' && bootline[1] == 'r' &&
	    '0' <= bootline[2] && bootline[2] <= '9') {
		unit = bootline[2] - '0';

		/* Create a fake diskinfo for this softraid volume. */
		SLIST_FOREACH(bv, &sr_volumes, sbv_link)
			if (bv->sbv_unit == unit)
				break;
		if (bv == NULL) {
			printf("Unknown device: sr%d\n", unit);
			return ENODEV;
		}
	} else {
		struct sr_boot_chunk *bc;

		/*
		 * Check if the boot device is a member of any of the assembled
		 * softraid volumes.
		 */
		SLIST_FOREACH(bv, &sr_volumes, sbv_link) {
			if ((bv->sbv_flags & BIOC_SCBOOTABLE) == 0)
				continue;

			SLIST_FOREACH(bc, &bv->sbv_chunks, sbc_link) {
				struct diskinfo *dip = bc->sbc_diskinfo;

				if (!strcmp(dip->path, bootdev))
					break;
			}
			if (bc != NULL)
				break;
		}
	}

	if (bv != NULL) {
		if ((bv->sbv_flags & BIOC_SCBOOTABLE) == 0) {
			printf("device sr%d is not bootable\n", unit);
			return ENODEV;
		}

		if ((bv->sbv_level == 'C' || bv->sbv_level == 0x1C) &&
		    bv->sbv_keys == NULL)
			if (sr_crypto_unlock_volume(bv) != 0)
				return EPERM;

		if (bv->sbv_diskinfo == NULL) {
			struct sr_boot_chunk *bc;
			struct diskinfo *dip, *bc_dip;
			int sr_handle;

			/* All reads will come from the boot chunk. */
			bc = sr_vol_boot_chunk(bv);
			if (bc == NULL)
				return ENXIO;
			bc_dip = (struct diskinfo *)bc->sbc_diskinfo;
			sr_handle = OF_open(bc_dip->path);
			if (sr_handle == -1)
				return EIO;

			dip = alloc(sizeof(struct diskinfo));
			bzero(dip, sizeof(*dip));
			dip->sr_vol = bv;
			dip->sr_handle = sr_handle;
			bv->sbv_diskinfo = dip;
		}

		/* strategy() and devopen() will use bootdev_dip */
		bootdev_dip = bv->sbv_diskinfo;

		/* Attempt to read disklabel. */
		bv->sbv_part = 'c';
		if (sr_getdisklabel(bv, &bootdev_dip->disklabel)) {
			OF_close(bootdev_dip->sr_handle);
			free(bv->sbv_diskinfo, sizeof(struct diskinfo));
			bv->sbv_diskinfo = NULL;
			bootdev_dip = NULL;
			return ERDLAB;
		}
	}

	return 0;
}
#endif

int
main(void)
{
	extern char version[];
	int chosen;
	int isupgrade = 0;
	char bootline[512];		/* Should check size? */
	char *cp;
	int fd;
#ifdef SOFTRAID
	int err;
#endif
	char **bootlp;
	char *just_bootline[2];

	printf(">> OpenBSD BOOT %s\n", version);

	/*
	 * Get the boot arguments from Openfirmware
	 */
	if ((chosen = OF_finddevice("/chosen")) == -1 ||
	    OF_getprop(chosen, "bootpath", bootdev, sizeof bootdev) < 0 ||
	    OF_getprop(chosen, "bootargs", bootline, sizeof bootline) < 0) {
		printf("Invalid Openfirmware environment\n");
		exit();
	}

#ifdef SOFTRAID
	diskprobe();
	srprobe();
	err = srbootdev(bootline);
	if (err) {
		printf("Cannot boot from softraid: %s\n", strerror(err));
		_rtt();
	}
#endif

	/*
	 * case 1:	boot net -a
	 *			-> getln loop
	 * case 2:	boot net kernel [options]
	 *			-> boot kernel, getln loop
	 * case 3:	boot net [options]
	 *			-> iterate boot list, getln loop
	 */

	bootlp = kernels;
	if (parseargs(bootline, &boothowto) == -1 ||
	    (boothowto & RB_ASKNAME)) {
		bootlp = 0;
	} else if (*bootline) {
		just_bootline[0] = bootline;
		just_bootline[1] = 0;
		bootlp = just_bootline;
	} else if (upgrade()) {
		just_bootline[0] = "/bsd.upgrade";
		just_bootline[1] = 0;
		isupgrade = 1;
		bootlp = just_bootline;
		printf("upgrade detected: switching to %s\n", *bootlp);
	}
	for (;;) {
		if (bootlp) {
			cp = *bootlp++;
			if (!cp) {
				printf("\n");
				bootlp = 0;
				kernels[0] = 0;	/* no more iteration */
			} else if (cp != bootline) {
				printf("Trying %s...\n", cp);
				if (strlcpy(bootline, cp, sizeof bootline)
				    >= sizeof bootline) {
					printf("bootargs too long: %s\n",
					    bootline);
					_rtt();
				}
			}
		}
		if (!bootlp) {
			printf("Boot: ");
			getln(bootline, sizeof bootline);
			if (parseargs(bootline, &boothowto) == -1)
				continue;
			if (!*bootline) {
				bootlp = kernels;
				continue;
			}
			if (strcmp(bootline, "exit") == 0 ||
			    strcmp(bootline, "halt") == 0) {
				_rtt();
			}
		}
		if (loadrandom(BOOTRANDOM, rnddata, sizeof(rnddata)) == 0)
			boothowto |= RB_GOODRANDOM;

		rc4_keysetup(&randomctx, rnddata, sizeof rnddata);
		rc4_skip(&randomctx, 1536);

		if ((fd = open(bootline, O_RDONLY)) < 0) {
			printf("open %s: %s\n", opened_name, strerror(errno));
			continue;
		}
		/* XXX void, for now */
#ifdef DEBUG
		if (debug)
			printf("main: Calling loadfile(fd, %s)\n", opened_name);
#endif
		(void)loadfile(fd, opened_name, isupgrade);
	}
	return 0;
}
