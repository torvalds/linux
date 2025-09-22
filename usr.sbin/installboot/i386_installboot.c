/*	$OpenBSD: i386_installboot.c,v 1.51 2025/09/17 16:12:10 deraadt Exp $	*/
/*	$NetBSD: installboot.c,v 1.5 1995/11/17 23:23:50 gwr Exp $ */

/*
 * Copyright (c) 2013 Pedro Martelletto
 * Copyright (c) 2011 Joel Sing <jsing@openbsd.org>
 * Copyright (c) 2003 Tom Cosgrove <tom.cosgrove@arches-consulting.com>
 * Copyright (c) 1997 Michael Shalayeff
 * Copyright (c) 1994 Paul Kranenburg
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
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *      This product includes software developed by Paul Kranenburg.
 * 4. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission
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

#define ELFSIZE 32

#include <sys/param.h>	/* DEV_BSIZE */
#include <sys/disklabel.h>
#include <sys/dkio.h>
#include <sys/ioctl.h>
#include <sys/mount.h>
#include <sys/reboot.h>
#include <sys/stat.h>
#include <sys/sysctl.h>
#include <sys/time.h>

#include <ufs/ufs/dinode.h>
#include <ufs/ufs/dir.h>
#include <ufs/ffs/fs.h>

#include <machine/cpu.h>
#include <machine/biosvar.h>

#include <elf.h>
#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <nlist.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <util.h>
#include <uuid.h>

#include "installboot.h"
#include "i386_installboot.h"

char	*bootldr;

char	*blkstore;
size_t	blksize;

struct sym_data pbr_symbols[] = {
	{"_fs_bsize_p",	2},
	{"_fs_bsize_s",	2},
	{"_fsbtodb",	1},
	{"_p_offset",	4},
	{"_inodeblk",	4},
	{"_inodedbl",	4},
	{"_nblocks",	2},
	{"_blkincr",	1},
	{NULL}
};

static void	devread(int, void *, daddr_t, size_t, char *);
static u_int	findopenbsd(int, struct disklabel *);
static int	getbootparams(char *, int, struct disklabel *);
static char	*loadproto(char *, long *);
static int	gpt_chk_mbr(struct dos_partition *, u_int64_t);
static int	sbchk(struct fs *, daddr_t);
static void	sbread(int, daddr_t, struct fs **, char *);

static const daddr_t sbtry[] = SBLOCKSEARCH;

/*
 * Read information about /boot's inode and filesystem parameters, then
 * put biosboot (partition boot record) on the target drive with these
 * parameters patched in.
 */

void
md_init(void)
{
	stages = 2;
	stage1 = "/usr/mdec/biosboot";
	stage2 = "/usr/mdec/boot";

	bootldr = "/boot";
}

void
md_loadboot(void)
{
	/* Load prototype boot blocks. */
	if ((blkstore = loadproto(stage1, &blksize)) == NULL)
		exit(1);

	/* XXX - Paranoia: Make sure size is aligned! */
	if (blksize & (DEV_BSIZE - 1))
		errx(1, "proto %s bad size=%ld", stage1, blksize);

	if (blksize > SBSIZE - DEV_BSIZE)
		errx(1, "proto bootblocks too big");
}

void
md_prepareboot(int devfd, char *dev)
{
	struct disklabel dl;
	int part;

	/* Get and check disklabel. */
	if (ioctl(devfd, DIOCGDINFO, &dl) == -1)
		err(1, "disklabel: %s", dev);
	if (dl.d_magic != DISKMAGIC)
		errx(1, "bad disklabel magic=0x%08x", dl.d_magic);

	/* Warn on unknown disklabel types. */
	if (dl.d_type == 0)
		warnx("disklabel type unknown");

	part = findgptefisys(devfd, &dl, NULL, NULL);
	if (part != -1) {
		create_filesystem(&dl, (char)part);
		return;
	}
}

void
md_installboot(int devfd, char *dev)
{
	struct disklabel dl;
	struct gpt_partition gp;
	int gpart, part;

	/* Get and check disklabel. */
	if (ioctl(devfd, DIOCGDINFO, &dl) == -1)
		err(1, "disklabel: %s", dev);
	if (dl.d_magic != DISKMAGIC)
		errx(1, "bad disklabel magic=0x%08x", dl.d_magic);

	/* Warn on unknown disklabel types. */
	if (dl.d_type == 0)
		warnx("disklabel type unknown");

	part = findgptefisys(devfd, &dl, &gpart, &gp);
	if (part != -1) {
		write_filesystem(&dl, (char)part, gpart, &gp);
		return;
	}

	bootldr = fileprefix(root, bootldr);
	if (bootldr == NULL)
		exit(1);
	if (verbose)
		fprintf(stderr, "%s %s to %s\n",
		    (nowrite ? "would copy" : "copying"), stage2, bootldr);
	if (!nowrite)
		if (filecopy(stage2, bootldr) == -1)
			exit(1);

	/* Get bootstrap parameters to patch into proto. */
	if (getbootparams(bootldr, devfd, &dl) != 0)
		exit(1);

	/* Write boot blocks to device. */
	write_bootblocks(devfd, dev, &dl);
}

void
write_bootblocks(int devfd, char *dev, struct disklabel *dl)
{
	struct stat	sb;
	u_int8_t	*secbuf;
	u_int		start;

	/* Write patched proto bootblock(s) into the superblock. */
	if (fstat(devfd, &sb) == -1)
		err(1, "fstat: %s", dev);

	if (!S_ISCHR(sb.st_mode))
		errx(1, "%s: not a character device", dev);

	/* Patch the parameters into the proto bootstrap sector. */
	pbr_set_symbols(stage1, blkstore, pbr_symbols);

	if (!nowrite) {
		/* Sync filesystems (to clean in-memory superblock?). */
		sync(); sleep(1);
	}

	/*
	 * Find bootstrap sector.
	 */
	start = findopenbsd(devfd, dl);
	if (verbose) {
		if (start == 0)
			fprintf(stderr, "no MBR, ");
		fprintf(stderr, "%s will be written at sector %u\n",
		    stage1, start);
	}

	if (start + (blksize / dl->d_secsize) > BOOTBIOS_MAXSEC)
		warnx("%s extends beyond sector %u. OpenBSD might not boot.",
		    stage1, BOOTBIOS_MAXSEC);

	if (!nowrite) {
		secbuf = calloc(1, dl->d_secsize);
		if (pread(devfd, secbuf, dl->d_secsize, (off_t)start *
		    dl->d_secsize) != dl->d_secsize)
			err(1, "pread boot sector");
		bcopy(blkstore, secbuf, blksize);
		if (pwrite(devfd, secbuf, dl->d_secsize, (off_t)start *
		    dl->d_secsize) != dl->d_secsize)
			err(1, "pwrite bootstrap");
		free(secbuf);
	}
}

int
create_filesystem(struct disklabel *dl, char part)
{
	static const char *newfsfmt = "/sbin/newfs -t msdos %s >/dev/null";
	struct msdosfs_args args;
	char cmd[60];
	int rslt;

	/* Newfs <duid>.<part> as msdos filesystem. */
	memset(&args, 0, sizeof(args));
	rslt = asprintf(&args.fspec,
	    "%02hhx%02hhx%02hhx%02hhx%02hhx%02hhx%02hhx%02hhx.%c",
            dl->d_uid[0], dl->d_uid[1], dl->d_uid[2], dl->d_uid[3],
            dl->d_uid[4], dl->d_uid[5], dl->d_uid[6], dl->d_uid[7],
	    part);
	if (rslt == -1) {
		warn("bad special device");
		return rslt;
	}

	rslt = snprintf(cmd, sizeof(cmd), newfsfmt, args.fspec);
	if (rslt >= sizeof(cmd)) {
		warnx("can't build newfs command");
		free(args.fspec);
		rslt = -1;
		return rslt;
	}

	if (verbose)
		fprintf(stderr, "%s %s\n",
		    (nowrite ? "would newfs" : "newfsing"), args.fspec);
	if (!nowrite) {
		rslt = system(cmd);
		if (rslt == -1) {
			warn("system('%s') failed", cmd);
			free(args.fspec);
			return rslt;
		}
	}

	free(args.fspec);
	return 0;
}

void
write_filesystem(struct disklabel *dl, char part, int gpart,
    struct gpt_partition *gp)
{
	static const char *fsckfmt = "/sbin/fsck -t msdos %s >/dev/null";
	struct msdosfs_args args;
#ifdef __amd64__
	struct statfs sf;
#endif
	char cmd[60];
	char dst[PATH_MAX];
	char *src;
	size_t mntlen, pathlen, srclen;
	int rslt;

	src = NULL;

	/* Create directory for temporary mount point. */
	strlcpy(dst, "/tmp/installboot.XXXXXXXXXX", sizeof(dst));
	if (mkdtemp(dst) == NULL)
		err(1, "mkdtemp('%s') failed", dst);
	mntlen = strlen(dst);

	/* Mount <duid>.<part> as msdos filesystem. */
	memset(&args, 0, sizeof(args));
	rslt = asprintf(&args.fspec,
	    "%02hhx%02hhx%02hhx%02hhx%02hhx%02hhx%02hhx%02hhx.%c",
            dl->d_uid[0], dl->d_uid[1], dl->d_uid[2], dl->d_uid[3],
            dl->d_uid[4], dl->d_uid[5], dl->d_uid[6], dl->d_uid[7],
	    part);
	if (rslt == -1) {
		warn("bad special device");
		goto rmdir;
	}

	args.export_info.ex_root = -2;	/* unchecked anyway on DOS fs */
	args.export_info.ex_flags = 0;
	args.flags = MSDOSFSMNT_LONGNAME;

	if (mount(MOUNT_MSDOS, dst, 0, &args) == -1) {
		/* Try fsck'ing it. */
		rslt = snprintf(cmd, sizeof(cmd), fsckfmt, args.fspec);
		if (rslt >= sizeof(cmd)) {
			warnx("can't build fsck command");
			rslt = -1;
			goto rmdir;
		}
		rslt = system(cmd);
		if (rslt == -1) {
			warn("system('%s') failed", cmd);
			goto rmdir;
		}
		if (mount(MOUNT_MSDOS, dst, 0, &args) == -1) {
			/* Try newfs'ing it. */
			rslt = create_filesystem(dl, part);
			if (rslt == -1)
				goto rmdir;
			rslt = mount(MOUNT_MSDOS, dst, 0, &args);
			if (rslt == -1) {
				warn("unable to mount EFI System partition");
				goto rmdir;
			}
		}
	}

	/* Create "/efi/BOOT" directory in <duid>.<part>. */
	if (strlcat(dst, "/efi", sizeof(dst)) >= sizeof(dst)) {
		rslt = -1;
		warn("unable to build /efi directory");
		goto umount;
	}
	rslt = mkdir(dst, 0);
	if (rslt == -1 && errno != EEXIST) {
		warn("mkdir('%s') failed", dst);
		goto umount;
	}
	if (strlcat(dst, "/BOOT", sizeof(dst)) >= sizeof(dst)) {
		rslt = -1;
		warn("unable to build /BOOT directory");
		goto umount;
	}
	rslt = mkdir(dst, 0);
	if (rslt == -1 && errno != EEXIST) {
		warn("mkdir('%s') failed", dst);
		goto umount;
	}

	/*
	 * Copy BOOTIA32.EFI and BOOTX64.EFI to /efi/BOOT/.
	 *
	 * N.B.: BOOTIA32.EFI is longer than BOOTX64.EFI, so src can be reused!
	 */
	pathlen = strlen(dst);
	if (strlcat(dst, "/BOOTIA32.EFI", sizeof(dst)) >= sizeof(dst)) {
		rslt = -1;
		warn("unable to build /BOOTIA32.EFI path");
		goto umount;
	}
	src = fileprefix(root, "/usr/mdec/BOOTIA32.EFI");
	if (src == NULL) {
		rslt = -1;
		goto umount;
	}
	srclen = strlen(src);
	if (verbose)
		fprintf(stderr, "%s %s to %s\n",
		    (nowrite ? "would copy" : "copying"), src, dst);
	if (!nowrite) {
		rslt = filecopy(src, dst);
		if (rslt == -1)
			goto umount;
	}
	src[srclen - strlen("/BOOTIA32.EFI")] = '\0';

	dst[pathlen] = '\0';
	if (strlcat(dst, "/BOOTX64.EFI", sizeof(dst)) >= sizeof(dst)) {
		rslt = -1;
		warn("unable to build /BOOTX64.EFI dst path");
		goto umount;
	}
	if (strlcat(src, "/BOOTX64.EFI", srclen+1) >= srclen+1) {
		rslt = -1;
		warn("unable to build /BOOTX64.EFI src path");
		goto umount;
	}
	if (verbose)
		fprintf(stderr, "%s %s to %s\n",
		    (nowrite ? "would copy" : "copying"), src, dst);
	if (!nowrite) {
		rslt = filecopy(src, dst);
		if (rslt == -1)
			goto umount;
	}

#ifdef __amd64__
	/* Skip installing a 2nd copy if we have a small filesystem. */
	if (statfs(dst, &sf) || sf.f_blocks < 2048) {
		rslt = 0;
		goto umount;
	}

	/* Create "/efi/openbsd" directory in <duid>.<part>. */
	dst[mntlen] = '\0';
	if (strlcat(dst, "/efi/openbsd", sizeof(dst)) >= sizeof(dst)) {
		rslt = -1;
		warn("unable to build /efi/openbsd directory");
		goto umount;
	}
	rslt = mkdir(dst, 0755);
	if (rslt == -1 && errno != EEXIST) {
		warn("mkdir('%s') failed", dst);
		goto umount;
	}

	/* Copy BOOTX64.EFI to /efi/openbsd/. */
	if (strlcat(dst, "/BOOTX64.EFI", sizeof(dst)) >= sizeof(dst)) {
		rslt = -1;
		warn("unable to build /BOOTX64.EFI path");
		goto umount;
	}
	src = fileprefix(root, "/usr/mdec/BOOTX64.EFI");
	if (src == NULL) {
		rslt = -1;
		goto umount;
	}
	srclen = strlen(src);
	if (verbose)
		fprintf(stderr, "%s %s to %s\n",
		    (nowrite ? "would copy" : "copying"), src, dst);
	if (!nowrite) {
		rslt = filecopy(src, dst);
		if (rslt == -1)
			goto umount;
	}

#ifdef EFIBOOTMGR
	if (config && gp)
		efi_bootmgr_setup(gpart, gp, "\\EFI\\OPENBSD\\BOOTX64.EFI");
#endif
	
#endif

	rslt = 0;

umount:
	dst[mntlen] = '\0';
	if (unmount(dst, MNT_FORCE) == -1)
		err(1, "unmount('%s') failed", dst);

rmdir:
	free(args.fspec);
	dst[mntlen] = '\0';
	if (rmdir(dst) == -1)
		err(1, "rmdir('%s') failed", dst);

	free(src);

	if (rslt == -1)
		exit(1);
}

/*
 * a) For media w/o an MBR use sector 0.
 * b) For media with an MBR and an OpenBSD (A6) partition use the first
 *    sector of the OpenBSD partition.
 * c) For media with an MBR and no OpenBSD partition error out.
 */
u_int
findopenbsd(int devfd, struct disklabel *dl)
{
	struct		dos_mbr mbr;
	u_int		mbroff = DOSBBSECTOR;
	u_int		mbr_eoff = DOSBBSECTOR; /* Offset of extended part. */
	struct		dos_partition *dp;
	u_int8_t	*secbuf;
	u_int		maxebr = DOS_MAXEBR, nextebr;
	int		i;

again:
	if (!maxebr--) {
		if (verbose)
			fprintf(stderr, "Traversed more than %d Extended Boot "
			    "Records (EBRs)\n", DOS_MAXEBR);
		goto done;
	}

	if (verbose)
		fprintf(stderr, "%s boot record (%cBR) at sector %u\n",
		    (mbroff == DOSBBSECTOR) ? "master" : "extended",
		    (mbroff == DOSBBSECTOR) ? 'M' : 'E', mbroff);

	if ((secbuf = malloc(dl->d_secsize)) == NULL)
		err(1, NULL);
	if (pread(devfd, secbuf, dl->d_secsize, (off_t)mbroff * dl->d_secsize)
	    < (ssize_t)sizeof(mbr))
		err(4, "can't pread boot record");
	bcopy(secbuf, &mbr, sizeof(mbr));
	free(secbuf);

	if (mbr.dmbr_sign != DOSMBR_SIGNATURE) {
		if (mbroff == DOSBBSECTOR)
			return 0;
		errx(1, "invalid boot record signature (0x%04X) @ sector %u",
		    mbr.dmbr_sign, mbroff);
	}

	nextebr = 0;
	for (i = 0; i < NDOSPART; i++) {
		dp = &mbr.dmbr_parts[i];
		if (!dp->dp_size)
			continue;

		if (verbose)
			fprintf(stderr,
			    "\tpartition %d: type 0x%02X offset %u size %u\n",
			    i, dp->dp_typ, dp->dp_start, dp->dp_size);

		if (dp->dp_typ == DOSPTYP_OPENBSD) {
			if (dp->dp_start > (dp->dp_start + mbroff))
				continue;
			return (dp->dp_start + mbroff);
		}

		if (!nextebr && (dp->dp_typ == DOSPTYP_EXTEND ||
		    dp->dp_typ == DOSPTYP_EXTENDL)) {
			nextebr = dp->dp_start + mbr_eoff;
			if (nextebr < dp->dp_start)
				nextebr = (u_int)-1;
			if (mbr_eoff == DOSBBSECTOR)
				mbr_eoff = dp->dp_start;
		}
	}

	if (nextebr && nextebr != (u_int)-1) {
		mbroff = nextebr;
		goto again;
	}

 done:
	errx(1, "no OpenBSD partition");
}

/*
 * Returns 0 if the MBR with the provided partition array is a GPT protective
 * MBR, and returns 1 otherwise. A GPT protective MBR would have one and only
 * one MBR partition, an EFI partition that either covers the whole disk or as
 * much of it as is possible with a 32bit size field.
 *
 * NOTE: MS always uses a size of UINT32_MAX for the EFI partition!**
 */
static int
gpt_chk_mbr(struct dos_partition *dp, u_int64_t dsize)
{
	struct dos_partition *dp2;
	int efi, found, i;
	u_int32_t psize;

	found = efi = 0;
	for (dp2=dp, i=0; i < NDOSPART; i++, dp2++) {
		if (dp2->dp_typ == DOSPTYP_UNUSED)
			continue;
		found++;
		if (dp2->dp_typ != DOSPTYP_EFI)
			continue;
		if (letoh32(dp2->dp_start) != GPTSECTOR)
			continue;
		psize = letoh32(dp2->dp_size);
		if (psize <= (dsize - GPTSECTOR) || psize == UINT32_MAX)
			efi++;
	}
	if (found == 1 && efi == 1)
		return (0);

	return (1);
}

int
findgptefisys(int devfd, struct disklabel *dl, int *gpartp,
    struct gpt_partition *gpp)
{
	struct gpt_partition	 gp[NGPTPARTITIONS];
	struct gpt_header	 gh;
	struct dos_partition	 dp[NDOSPART];
	struct uuid		 efisys_uuid;
	const char		 efisys_uuid_code[] = GPT_UUID_EFI_SYSTEM;
	off_t			 off;
	ssize_t			 len;
	u_int64_t		 start;
	int			 part, i;
	uint32_t		 orig_csum, new_csum;
	uint32_t		 ghsize, ghpartsize, ghpartnum, ghpartspersec;
	u_int8_t		*secbuf;

	/* Prepare EFI System UUID */
	uuid_dec_be(efisys_uuid_code, &efisys_uuid);

	if ((secbuf = malloc(dl->d_secsize)) == NULL)
		err(1, NULL);

	/* Check that there is a protective MBR. */
	len = pread(devfd, secbuf, dl->d_secsize, 0);
	if (len != dl->d_secsize)
		err(4, "can't read mbr");
	memcpy(dp, &secbuf[DOSPARTOFF], sizeof(dp));
	if (gpt_chk_mbr(dp, DL_GETDSIZE(dl))) {
		free(secbuf);
		return (-1);
	}

	/* Check GPT Header. */
	off = dl->d_secsize;	/* Read header from sector 1. */
	len = pread(devfd, secbuf, dl->d_secsize, off);
	if (len != dl->d_secsize)
		err(4, "can't pread gpt header");

	memcpy(&gh, secbuf, sizeof(gh));
	free(secbuf);

	/* Check signature */
	if (letoh64(gh.gh_sig) != GPTSIGNATURE)
		return (-1);

	if (letoh32(gh.gh_rev) != GPTREVISION)
		return (-1);

	ghsize = letoh32(gh.gh_size);
	if (ghsize < GPTMINHDRSIZE || ghsize > sizeof(struct gpt_header))
		return (-1);

	/* Check checksum */
	orig_csum = gh.gh_csum;
	gh.gh_csum = 0;
	new_csum = crc32((unsigned char *)&gh, ghsize);
	gh.gh_csum = orig_csum;
	if (letoh32(orig_csum) != new_csum)
		return (-1);

	off = letoh64(gh.gh_part_lba) * dl->d_secsize;
	ghpartsize = letoh32(gh.gh_part_size);
	ghpartspersec = dl->d_secsize / ghpartsize;
	ghpartnum = letoh32(gh.gh_part_num);
	if ((secbuf = malloc(dl->d_secsize)) == NULL)
		err(1, NULL);
	for (i = 0; i < (ghpartnum + ghpartspersec - 1) / ghpartspersec; i++) {
		len = pread(devfd, secbuf, dl->d_secsize, off);
		if (len != dl->d_secsize) {
			free(secbuf);
			return (-1);
		}
		memcpy(gp + i * ghpartspersec, secbuf,
		    ghpartspersec * sizeof(struct gpt_partition));
		off += dl->d_secsize;
	}
	free(secbuf);
	new_csum = crc32((unsigned char *)&gp, ghpartnum * ghpartsize);
	if (new_csum != letoh32(gh.gh_part_csum))
		return (-1);

	start = 0;
	for (i = 0; i < ghpartnum && start == 0; i++) {
		if (memcmp(&gp[i].gp_type, &efisys_uuid,
		    sizeof(struct uuid)) == 0) {
			start = letoh64(gp[i].gp_lba_start);
			part = i;
		}
	}

	if (start) {
		if (gpartp)
			*gpartp = part;
		if (gpp)
			memcpy(gpp, &gp[part], sizeof(*gpp));
		for (i = 0; i < MAXPARTITIONS; i++) {
			if (DL_GETPSIZE(&dl->d_partitions[i]) > 0 &&
			    DL_GETPOFFSET(&dl->d_partitions[i]) == start)
				return (DL_PARTNUM2NAME(i));
		}
	}

	return (-1);
}

/*
 * Load the prototype boot sector (biosboot) into memory.
 */
static char *
loadproto(char *fname, long *size)
{
	int	fd;
	size_t	tdsize;		/* text+data size */
	char	*bp;
	Elf_Ehdr eh;
	Elf_Word phsize;
	Elf_Phdr *ph;

	if ((fd = open(fname, O_RDONLY)) == -1)
		err(1, "%s", fname);

	if (read(fd, &eh, sizeof(eh)) != sizeof(eh))
		errx(1, "%s: read failed", fname);

	if (!IS_ELF(eh))
		errx(1, "%s: bad magic: 0x%02x%02x%02x%02x", fname,
		    eh.e_ident[EI_MAG0], eh.e_ident[EI_MAG1],
		    eh.e_ident[EI_MAG2], eh.e_ident[EI_MAG3]);

	/*
	 * We have to include the exec header in the beginning of
	 * the buffer, and leave extra space at the end in case
	 * the actual write to disk wants to skip the header.
	 */

	/* Program load header. */
	if (eh.e_phnum != 1)
		errx(1, "%s: %u ELF load sections (only support 1)",
		    fname, eh.e_phnum);

	ph = reallocarray(NULL, eh.e_phnum, sizeof(Elf_Phdr));
	if (ph == NULL)
		err(1, NULL);
	phsize = eh.e_phnum * sizeof(Elf_Phdr);

	if (pread(fd, ph, phsize, eh.e_phoff) != phsize)
		errx(1, "%s: can't pread header", fname);

	tdsize = ph->p_filesz;

	/*
	 * Allocate extra space here because the caller may copy
	 * the boot block starting at the end of the exec header.
	 * This prevents reading beyond the end of the buffer.
	 */
	if ((bp = calloc(tdsize, 1)) == NULL)
		err(1, NULL);

	/* Read the rest of the file. */
	if (pread(fd, bp, tdsize, ph->p_offset) != (ssize_t)tdsize)
		errx(1, "%s: pread failed", fname);

	*size = tdsize;	/* not aligned to DEV_BSIZE */

	close(fd);
	return bp;
}

static void
devread(int fd, void *buf, daddr_t blk, size_t size, char *msg)
{
	if (pread(fd, buf, size, dbtob((off_t)blk)) != (ssize_t)size)
		err(1, "%s: devread: pread", msg);
}

/*
 * Read information about /boot's inode, then put this and filesystem
 * parameters from the superblock into pbr_symbols.
 */
static int
getbootparams(char *boot, int devfd, struct disklabel *dl)
{
	int		fd;
	struct stat	dsb, fsb;
	struct statfs	fssb;
	struct partition *pp;
	struct fs	*fs;
	char		*sblock, *buf;
	u_int		blk, *ap;
	int		ndb;
	int		mib[3];
	size_t		size;
	dev_t		dev;
	int		incr;

	/*
	 * Open 2nd-level boot program and record enough details about
	 * where it is on the filesystem represented by `devfd'
	 * (inode block, offset within that block, and various filesystem
	 * parameters essentially taken from the superblock) for biosboot
	 * to be able to load it later.
	 */

	/* Make sure the (probably new) boot file is on disk. */
	sync(); sleep(1);

	if ((fd = open(boot, O_RDONLY)) == -1)
		err(1, "open: %s", boot);

	if (fstatfs(fd, &fssb) == -1)
		err(1, "statfs: %s", boot);

	if (strncmp(fssb.f_fstypename, "ffs", MFSNAMELEN) &&
	    strncmp(fssb.f_fstypename, "ufs", MFSNAMELEN) )
		errx(1, "%s: not on an FFS filesystem", boot);

#if 0
	if (read(fd, &eh, sizeof(eh)) != sizeof(eh))
		errx(1, "read: %s", boot);

	if (!IS_ELF(eh)) {
		errx(1, "%s: bad magic: 0x%02x%02x%02x%02x",
		    boot,
		    eh.e_ident[EI_MAG0], eh.e_ident[EI_MAG1],
		    eh.e_ident[EI_MAG2], eh.e_ident[EI_MAG3]);
	}
#endif

	if (fsync(fd) != 0)
		err(1, "fsync: %s", boot);

	if (fstat(fd, &fsb) != 0)
		err(1, "fstat: %s", boot);

	if (fstat(devfd, &dsb) != 0)
		err(1, "fstat: %d", devfd);

	/* Check devices. */
	mib[0] = CTL_MACHDEP;
	mib[1] = CPU_CHR2BLK;
	mib[2] = dsb.st_rdev;
	size = sizeof(dev);
	if (sysctl(mib, 3, &dev, &size, NULL, 0) >= 0) {
		if (fsb.st_dev / MAXPARTITIONS != dev / MAXPARTITIONS)
			errx(1, "cross-device install");
	}

	pp = &dl->d_partitions[DISKPART(fsb.st_dev)];
	close(fd);

	if ((sblock = malloc(SBSIZE)) == NULL)
		err(1, NULL);

	sbread(devfd, DL_SECTOBLK(dl, pp->p_offset), &fs, sblock);

	/* Read inode. */
	if ((buf = malloc(fs->fs_bsize)) == NULL)
		err(1, NULL);

	blk = fsbtodb(fs, ino_to_fsba(fs, fsb.st_ino));

	/*
	 * Have the inode.  Figure out how many filesystem blocks (not disk
	 * sectors) there are for biosboot to load.
	 */
	devread(devfd, buf, DL_SECTOBLK(dl, pp->p_offset) + blk,
	    fs->fs_bsize, "inode");
	if (fs->fs_magic == FS_UFS2_MAGIC) {
		struct ufs2_dinode *ip2 = (struct ufs2_dinode *)(buf) +
		    ino_to_fsbo(fs, fsb.st_ino);
		ndb = howmany(ip2->di_size, fs->fs_bsize);
		ap = (u_int *)ip2->di_db;
		incr = sizeof(u_int32_t);
	} else {
		struct ufs1_dinode *ip1 = (struct ufs1_dinode *)(buf) +
		    ino_to_fsbo(fs, fsb.st_ino);
		ndb = howmany(ip1->di_size, fs->fs_bsize);
		ap = (u_int *)ip1->di_db;
		incr = 0;
	}

	if (ndb <= 0)
		errx(1, "No blocks to load");

	/*
	 * Now set the values that will need to go into biosboot
	 * (the partition boot record, a.k.a. the PBR).
	 */
	sym_set_value(pbr_symbols, "_fs_bsize_p", (fs->fs_bsize / 16));
	sym_set_value(pbr_symbols, "_fs_bsize_s", (fs->fs_bsize /
	    dl->d_secsize));

	/*
	 * fs_fsbtodb is the shift to convert fs_fsize to DEV_BSIZE. The
	 * ino_to_fsba() return value is the number of fs_fsize units.
	 * Calculate the shift to convert fs_fsize into physical sectors,
	 * which are added to p_offset to get the sector address BIOS
	 * will use.
	 *
	 * N.B.: ASSUMES fs_fsize is a power of 2 of d_secsize.
	 */
	sym_set_value(pbr_symbols, "_fsbtodb",
	    ffs(fs->fs_fsize / dl->d_secsize) - 1);

	sym_set_value(pbr_symbols, "_p_offset", pp->p_offset);
	sym_set_value(pbr_symbols, "_inodeblk",
	    ino_to_fsba(fs, fsb.st_ino));
	sym_set_value(pbr_symbols, "_inodedbl",
	    ((((char *)ap) - buf) + INODEOFF));
	sym_set_value(pbr_symbols, "_nblocks", ndb);
	sym_set_value(pbr_symbols, "_blkincr", incr);

	if (verbose) {
		fprintf(stderr, "%s is %d blocks x %d bytes\n",
		    boot, ndb, fs->fs_bsize);
		fprintf(stderr, "fs block shift %u; part offset %u; "
		    "inode block %lld, offset %u\n",
		    ffs(fs->fs_fsize / dl->d_secsize) - 1,
		    pp->p_offset,
		    ino_to_fsba(fs, fsb.st_ino),
		    (unsigned int)((((char *)ap) - buf) + INODEOFF));
		fprintf(stderr, "expecting %d-bit fs blocks (incr %d)\n",
		    incr ? 64 : 32, incr);
	}

	free (sblock);
	free (buf);

	return 0;
}

void
sym_set_value(struct sym_data *sym_list, char *sym, u_int32_t value)
{
	struct sym_data *p;

	for (p = sym_list; p->sym_name != NULL; p++) {
		if (strcmp(p->sym_name, sym) == 0)
			break;
	}

	if (p->sym_name == NULL)
		errx(1, "%s: no such symbol", sym);

	p->sym_value = value;
	p->sym_set = 1;
}

/*
 * Write the parameters stored in sym_list into the in-memory copy of
 * the prototype biosboot (proto), ready for it to be written to disk.
 */
void
pbr_set_symbols(char *fname, char *proto, struct sym_data *sym_list)
{
	struct sym_data *sym;
	struct nlist	*nl;
	char		*vp;
	u_int32_t	*lp;
	u_int16_t	*wp;
	u_int8_t	*bp;

	for (sym = sym_list; sym->sym_name != NULL; sym++) {
		if (!sym->sym_set)
			errx(1, "%s not set", sym->sym_name);

		/* Allocate space for 2; second is null-terminator for list. */
		nl = calloc(2, sizeof(struct nlist));
		if (nl == NULL)
			err(1, NULL);

		nl->n_name = sym->sym_name;

		if (nlist_elf32(fname, nl) != 0)
			errx(1, "%s: symbol %s not found",
			    fname, sym->sym_name);

		if (nl->n_type != (N_TEXT))
			errx(1, "%s: %s: wrong type (%x)",
			    fname, sym->sym_name, nl->n_type);

		/* Get a pointer to where the symbol's value needs to go. */
		vp = proto + nl->n_value;

		switch (sym->sym_size) {
		case 4:					/* u_int32_t */
			lp = (u_int32_t *) vp;
			*lp = sym->sym_value;
			break;
		case 2:					/* u_int16_t */
			if (sym->sym_value >= 0x10000)	/* out of range */
				errx(1, "%s: symbol out of range (%u)",
				    sym->sym_name, sym->sym_value);
			wp = (u_int16_t *) vp;
			*wp = (u_int16_t) sym->sym_value;
			break;
		case 1:					/* u_int16_t */
			if (sym->sym_value >= 0x100)	/* out of range */
				errx(1, "%s: symbol out of range (%u)",
				    sym->sym_name, sym->sym_value);
			bp = (u_int8_t *) vp;
			*bp = (u_int8_t) sym->sym_value;
			break;
		default:
			errx(1, "%s: bad symbol size %d",
			    sym->sym_name, sym->sym_size);
			/* NOTREACHED */
		}

		free(nl);
	}
}

static int
sbchk(struct fs *fs, daddr_t sbloc)
{
	if (verbose)
		fprintf(stderr, "looking for superblock at %lld\n", sbloc);

	if (fs->fs_magic != FS_UFS2_MAGIC && fs->fs_magic != FS_UFS1_MAGIC) {
		if (verbose)
			fprintf(stderr, "bad superblock magic 0x%x\n",
			    fs->fs_magic);
		return (0);
	}

	/*
	 * Looking for an FFS1 file system at SBLOCK_UFS2 will find the
	 * wrong superblock for file systems with 64k block size.
	 */
	if (fs->fs_magic == FS_UFS1_MAGIC && sbloc == SBLOCK_UFS2) {
		if (verbose)
			fprintf(stderr, "skipping ffs1 superblock at %lld\n",
			    sbloc);
		return (0);
	}

	if (fs->fs_bsize <= 0 || fs->fs_bsize < sizeof(struct fs) ||
	    fs->fs_bsize > MAXBSIZE) {
		if (verbose)
			fprintf(stderr, "invalid superblock block size %d\n",
			    fs->fs_bsize);
		return (0);
	}

	if (fs->fs_sbsize <= 0 || fs->fs_sbsize > SBSIZE) {
		if (verbose)
			fprintf(stderr, "invalid superblock size %d\n",
			    fs->fs_sbsize);
		return (0);
	}

	if (fs->fs_inopb <= 0) {
		if (verbose)
			fprintf(stderr, "invalid superblock inodes/block %d\n",
			    fs->fs_inopb);
		return (0);
	}

	if (verbose)
		fprintf(stderr, "found valid %s superblock\n",
		    fs->fs_magic == FS_UFS2_MAGIC ? "ffs2" : "ffs1");

	return (1);
}

static void
sbread(int fd, daddr_t poffset, struct fs **fs, char *sblock)
{
	int i;
	daddr_t sboff;

	for (i = 0; sbtry[i] != -1; i++) {
		sboff = sbtry[i] / DEV_BSIZE;
		devread(fd, sblock, poffset + sboff, SBSIZE, "superblock");
		*fs = (struct fs *)sblock;
		if (sbchk(*fs, sbtry[i]))
			break;
	}

	if (sbtry[i] == -1)
		errx(1, "couldn't find ffs superblock");
}
