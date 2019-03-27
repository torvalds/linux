/*
 * Mach Operating System
 * Copyright (c) 1992 Carnegie Mellon University
 * All Rights Reserved.
 *
 * Permission to use, copy, modify and distribute this software and its
 * documentation is hereby granted, provided that both the copyright
 * notice and this permission notice appear in all copies of the
 * software, derivative works or modified versions, and any portions
 * thereof, and that both notices appear in supporting documentation.
 *
 * CARNEGIE MELLON ALLOWS FREE USE OF THIS SOFTWARE IN ITS "AS IS"
 * CONDITION.  CARNEGIE MELLON DISCLAIMS ANY LIABILITY OF ANY KIND FOR
 * ANY DAMAGES WHATSOEVER RESULTING FROM THE USE OF THIS SOFTWARE.
 *
 * Carnegie Mellon requests users of this software to return to
 *
 *  Software Distribution Coordinator  or  Software.Distribution@CS.CMU.EDU
 *  School of Computer Science
 *  Carnegie Mellon University
 *  Pittsburgh PA 15213-3890
 *
 * any improvements or extensions that they make and grant Carnegie Mellon
 * the rights to redistribute these changes.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/disk.h>
#include <sys/disklabel.h>
#include <sys/diskmbr.h>
#include <sys/endian.h>
#include <sys/param.h>
#include <sys/stat.h>
#include <sys/mount.h>
#include <ctype.h>
#include <fcntl.h>
#include <err.h>
#include <errno.h>
#include <libgeom.h>
#include <paths.h>
#include <regex.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static int iotest;

#define NO_DISK_SECTORS ((u_int32_t)-1)
#define NO_TRACK_CYLINDERS 1023
#define NO_TRACK_HEADS 255
#define NO_TRACK_SECTORS 63
#define LBUF 100
static char lbuf[LBUF];

/*
 *
 * Ported to 386bsd by Julian Elischer  Thu Oct 15 20:26:46 PDT 1992
 *
 * 14-Dec-89  Robert Baron (rvb) at Carnegie-Mellon University
 *	Copyright (c) 1989	Robert. V. Baron
 *	Created.
 */

#define Decimal(str, ans, tmp, maxval) if (decimal(str, &tmp, ans, maxval)) ans = tmp

#define MAX_SEC_SIZE 65536	/* maximum sector size that is supported */
#define MIN_SEC_SIZE 512	/* the sector size to start sensing at */
static int secsize = 0;		/* the sensed sector size */

static char *disk;

static int cyls, sectors, heads, cylsecs;
static u_int32_t disksecs;

struct mboot {
	unsigned char *bootinst;  /* boot code */
	off_t bootinst_size;
	struct	dos_partition parts[NDOSPART];
};

static struct mboot mboot;
static int fd;

#define ACTIVE 0x80

static uint dos_cyls;
static uint dos_heads;
static uint dos_sectors;
static uint dos_cylsecs;

#define DOSSECT(s,c) ((s & 0x3f) | ((c >> 2) & 0xc0))
#define DOSCYL(c)	(c & 0xff)

#define MAX_ARGS	10

static int	current_line_number;

static int	geom_processed = 0;
static int	part_processed = 0;
static int	active_processed = 0;

typedef struct cmd {
    char		cmd;
    int			n_args;
    struct arg {
	char		argtype;
	unsigned long	arg_val;
	char *		arg_str;
    }			args[MAX_ARGS];
} CMD;

static int B_flag  = 0;		/* replace boot code */
static int I_flag  = 0;		/* use entire disk for FreeBSD */
static int a_flag  = 0;		/* set active partition */
static char *b_flag = NULL;	/* path to boot code */
static int i_flag  = 0;		/* replace partition data */
static int q_flag  = 0;		/* Be quiet */
static int u_flag  = 0;		/* update partition data */
static int s_flag  = 0;		/* Print a summary and exit */
static int t_flag  = 0;		/* test only */
static char *f_flag = NULL;	/* Read config info from file */
static int v_flag  = 0;		/* Be verbose */
static int print_config_flag = 0;

/*
 * A list of partition types, probably outdated.
 */
static const char *const part_types[256] = {
	[0x00] = "unused",
	[0x01] = "Primary DOS with 12 bit FAT",
	[0x02] = "XENIX / file system",
	[0x03] = "XENIX /usr file system",
	[0x04] = "Primary DOS with 16 bit FAT (< 32MB)",
	[0x05] = "Extended DOS",
	[0x06] = "Primary DOS, 16 bit FAT (>= 32MB)",
	[0x07] = "NTFS, OS/2 HPFS, QNX-2 (16 bit) or Advanced UNIX",
	[0x08] = "AIX file system or SplitDrive",
	[0x09] = "AIX boot partition or Coherent",
	[0x0A] = "OS/2 Boot Manager, OPUS or Coherent swap",
	[0x0B] = "DOS or Windows 95 with 32 bit FAT",
	[0x0C] = "DOS or Windows 95 with 32 bit FAT (LBA)",
	[0x0E] = "Primary 'big' DOS (>= 32MB, LBA)",
	[0x0F] = "Extended DOS (LBA)",
	[0x10] = "OPUS",
	[0x11] = "OS/2 BM: hidden DOS with 12-bit FAT",
	[0x12] = "Compaq diagnostics",
	[0x14] = "OS/2 BM: hidden DOS with 16-bit FAT (< 32MB)",
	[0x16] = "OS/2 BM: hidden DOS with 16-bit FAT (>= 32MB)",
	[0x17] = "OS/2 BM: hidden IFS (e.g. HPFS)",
	[0x18] = "AST Windows swapfile",
	[0x1b] = "ASUS Recovery partition (NTFS)",
	[0x24] = "NEC DOS",
	[0x3C] = "PartitionMagic recovery",
	[0x39] = "plan9",
	[0x40] = "VENIX 286",
	[0x41] = "Linux/MINIX (sharing disk with DRDOS)",
	[0x42] = "SFS or Linux swap (sharing disk with DRDOS)",
	[0x43] = "Linux native (sharing disk with DRDOS)",
	[0x4D] = "QNX 4.2 Primary",
	[0x4E] = "QNX 4.2 Secondary",
	[0x4F] = "QNX 4.2 Tertiary",
	[0x50] = "DM (disk manager)",
	[0x51] = "DM6 Aux1 (or Novell)",
	[0x52] = "CP/M or Microport SysV/AT",
	[0x53] = "DM6 Aux3",
	[0x54] = "DM6",
	[0x55] = "EZ-Drive (disk manager)",
	[0x56] = "Golden Bow (disk manager)",
	[0x5c] = "Priam Edisk (disk manager)", /* according to S. Widlake */
	[0x61] = "SpeedStor",
	[0x63] = "System V/386 (such as ISC UNIX), GNU HURD or Mach",
	[0x64] = "Novell Netware/286 2.xx",
	[0x65] = "Novell Netware/386 3.xx",
	[0x70] = "DiskSecure Multi-Boot",
	[0x75] = "PCIX",
	[0x77] = "QNX4.x",
	[0x78] = "QNX4.x 2nd part",
	[0x79] = "QNX4.x 3rd part",
	[0x80] = "Minix until 1.4a",
	[0x81] = "Minix since 1.4b, early Linux partition or Mitac disk manager",
	[0x82] = "Linux swap or Solaris x86",
	[0x83] = "Linux native",
	[0x84] = "OS/2 hidden C: drive",
	[0x85] = "Linux extended",
	[0x86] = "NTFS volume set??",
	[0x87] = "NTFS volume set??",
	[0x93] = "Amoeba file system",
	[0x94] = "Amoeba bad block table",
	[0x9F] = "BSD/OS",
	[0xA0] = "Suspend to Disk",
	[0xA5] = "FreeBSD/NetBSD/386BSD",
	[0xA6] = "OpenBSD",
	[0xA7] = "NeXTSTEP",
	[0xA9] = "NetBSD",
	[0xAC] = "IBM JFS",
	[0xAF] = "HFS+",
	[0xB7] = "BSDI BSD/386 file system",
	[0xB8] = "BSDI BSD/386 swap",
	[0xBE] = "Solaris x86 boot",
	[0xBF] = "Solaris x86 (new)",
	[0xC1] = "DRDOS/sec with 12-bit FAT",
	[0xC4] = "DRDOS/sec with 16-bit FAT (< 32MB)",
	[0xC6] = "DRDOS/sec with 16-bit FAT (>= 32MB)",
	[0xC7] = "Syrinx",
	[0xDB] = "CP/M, Concurrent CP/M, Concurrent DOS or CTOS",
	[0xDE] = "DELL Utilities - FAT filesystem",
	[0xE1] = "DOS access or SpeedStor with 12-bit FAT extended partition",
	[0xE3] = "DOS R/O or SpeedStor",
	[0xE4] = "SpeedStor with 16-bit FAT extended partition < 1024 cyl.",
	[0xEB] = "BeOS file system",
	[0xEE] = "EFI GPT",
	[0xEF] = "EFI System Partition",
	[0xF1] = "SpeedStor",
	[0xF2] = "DOS 3.3+ Secondary",
	[0xF4] = "SpeedStor large partition",
	[0xFB] = "VMware VMFS",
	[0xFE] = "SpeedStor >1024 cyl. or LANstep",
	[0xFF] = "Xenix bad blocks table",
};

static const char *
get_type(int t)
{
	const char *ret;

	ret = (t >= 0 && t <= 255) ? part_types[t] : NULL;
	return ret ? ret : "unknown";
}


static int geom_class_available(const char *);
static void print_s0(void);
static void print_part(const struct dos_partition *);
static void init_sector0(unsigned long start);
static void init_boot(void);
static void change_part(int i);
static void print_params(void);
static void change_active(int which);
static void change_code(void);
static void get_params_to_use(void);
static char *get_rootdisk(void);
static void dos(struct dos_partition *partp);
static int open_disk(int flag);
static ssize_t read_disk(off_t sector, void *buf);
static int write_disk(off_t sector, void *buf);
static int get_params(void);
static int read_s0(void);
static int write_s0(void);
static int ok(const char *str);
static int decimal(const char *str, int *num, int deflt, uint32_t maxval);
static int read_config(char *config_file);
static void reset_boot(void);
static int sanitize_partition(struct dos_partition *);
static void usage(void);

int
main(int argc, char *argv[])
{
	int	c, i;
	int	partition = -1;
	struct	dos_partition *partp;

	while ((c = getopt(argc, argv, "BIab:f:ipqstuv1234")) != -1)
		switch (c) {
		case 'B':
			B_flag = 1;
			break;
		case 'I':
			I_flag = 1;
			break;
		case 'a':
			a_flag = 1;
			break;
		case 'b':
			b_flag = optarg;
			break;
		case 'f':
			f_flag = optarg;
			break;
		case 'i':
			i_flag = 1;
			break;
		case 'p':
			print_config_flag = 1;
			break;
		case 'q':
			q_flag = 1;
			break;
		case 's':
			s_flag = 1;
			break;
		case 't':
			t_flag = 1;
			break;
		case 'u':
			u_flag = 1;
			break;
		case 'v':
			v_flag = 1;
			break;
		case '1':
		case '2':
		case '3':
		case '4':
			partition = c - '0';
			break;
		default:
			usage();
		}
	if (f_flag || i_flag)
		u_flag = 1;
	if (t_flag)
		v_flag = 1;
	argc -= optind;
	argv += optind;

	if (argc == 0) {
		disk = get_rootdisk();
	} else {
		disk = g_device_path(argv[0]);
		if (disk == NULL)
			err(1, "unable to get correct path for %s", argv[0]);
	}
	if (open_disk(u_flag) < 0)
		err(1, "cannot open disk %s", disk);

	/* (abu)use mboot.bootinst to probe for the sector size */
	if ((mboot.bootinst = malloc(MAX_SEC_SIZE)) == NULL)
		err(1, "cannot allocate buffer to determine disk sector size");
	if (read_disk(0, mboot.bootinst) == -1)
		errx(1, "could not detect sector size");
	free(mboot.bootinst);
	mboot.bootinst = NULL;

	if (print_config_flag) {
		if (read_s0())
			err(1, "read_s0");

		printf("# %s\n", disk);
		printf("g c%d h%d s%d\n", dos_cyls, dos_heads, dos_sectors);

		for (i = 0; i < NDOSPART; i++) {
			partp = &mboot.parts[i];

			if (partp->dp_start == 0 && partp->dp_size == 0)
				continue;

			printf("p %d 0x%02x %lu %lu\n", i + 1, partp->dp_typ,
			    (u_long)partp->dp_start, (u_long)partp->dp_size);

			/* Fill flags for the partition. */
			if (partp->dp_flag & 0x80)
				printf("a %d\n", i + 1);
		}
		exit(0);
	}
	if (s_flag) {
		if (read_s0())
			err(1, "read_s0");
		printf("%s: %d cyl %d hd %d sec\n", disk, dos_cyls, dos_heads,
		    dos_sectors);
		printf("Part  %11s %11s Type Flags\n", "Start", "Size");
		for (i = 0; i < NDOSPART; i++) {
			partp = &mboot.parts[i];
			if (partp->dp_start == 0 && partp->dp_size == 0)
				continue;
			printf("%4d: %11lu %11lu 0x%02x 0x%02x\n", i + 1,
			    (u_long) partp->dp_start,
			    (u_long) partp->dp_size, partp->dp_typ,
			    partp->dp_flag);
		}
		exit(0);
	}

	printf("******* Working on device %s *******\n",disk);

	if (I_flag) {
		read_s0();
		reset_boot();
		partp = &mboot.parts[0];
		partp->dp_typ = DOSPTYP_386BSD;
		partp->dp_flag = ACTIVE;
		partp->dp_start = dos_sectors;
		partp->dp_size = rounddown(disksecs, dos_cylsecs) -
		    dos_sectors;
		dos(partp);
		if (v_flag)
			print_s0();
		if (!t_flag)
			write_s0();
		exit(0);
	}
	if (f_flag) {
	    if (read_s0() || i_flag)
		reset_boot();
	    if (!read_config(f_flag))
		exit(1);
	    if (v_flag)
		print_s0();
	    if (!t_flag)
		write_s0();
	} else {
	    if(u_flag)
		get_params_to_use();
	    else
		print_params();

	    if (read_s0())
		init_sector0(dos_sectors);

	    printf("Media sector size is %d\n", secsize);
	    printf("Warning: BIOS sector numbering starts with sector 1\n");
	    printf("Information from DOS bootblock is:\n");
	    if (partition == -1)
		for (i = 1; i <= NDOSPART; i++)
		    change_part(i);
	    else
		change_part(partition);

	    if (u_flag || a_flag)
		change_active(partition);

	    if (B_flag)
		change_code();

	    if (u_flag || a_flag || B_flag) {
		if (!t_flag) {
		    printf("\nWe haven't changed the partition table yet.  ");
		    printf("This is your last chance.\n");
		}
		print_s0();
		if (!t_flag) {
		    if (ok("Should we write new partition table?"))
			write_s0();
		} else {
		    printf("\n-t flag specified -- partition table not written.\n");
		}
	    }
	}

	exit(0);
}

static void
usage()
{
	fprintf(stderr, "%s%s",
		"usage: fdisk [-BIaipqstu] [-b bootcode] [-1234] [disk]\n",
 		"       fdisk -f configfile [-itv] [disk]\n");
        exit(1);
}

static void
print_s0(void)
{
	int	i;

	print_params();
	printf("Information from DOS bootblock is:\n");
	for (i = 1; i <= NDOSPART; i++) {
		printf("%d: ", i);
		print_part(&mboot.parts[i - 1]);
	}
}

static struct dos_partition mtpart;

static void
print_part(const struct dos_partition *partp)
{
	u_int64_t part_mb;

	if (!bcmp(partp, &mtpart, sizeof (struct dos_partition))) {
		printf("<UNUSED>\n");
		return;
	}
	/*
	 * Be careful not to overflow.
	 */
	part_mb = partp->dp_size;
	part_mb *= secsize;
	part_mb /= (1024 * 1024);
	printf("sysid %d (%#04x),(%s)\n", partp->dp_typ, partp->dp_typ,
	    get_type(partp->dp_typ));
	printf("    start %lu, size %lu (%ju Meg), flag %x%s\n",
		(u_long)partp->dp_start,
		(u_long)partp->dp_size,
		(uintmax_t)part_mb,
		partp->dp_flag,
		partp->dp_flag == ACTIVE ? " (active)" : "");
	printf("\tbeg: cyl %d/ head %d/ sector %d;\n\tend: cyl %d/ head %d/ sector %d\n"
		,DPCYL(partp->dp_scyl, partp->dp_ssect)
		,partp->dp_shd
		,DPSECT(partp->dp_ssect)
		,DPCYL(partp->dp_ecyl, partp->dp_esect)
		,partp->dp_ehd
		,DPSECT(partp->dp_esect));
}


static void
init_boot(void)
{
	const char *fname;
	int fdesc, n;
	struct stat sb;

	fname = b_flag ? b_flag : "/boot/mbr";
	if ((fdesc = open(fname, O_RDONLY)) == -1 ||
	    fstat(fdesc, &sb) == -1)
		err(1, "%s", fname);
	if (sb.st_size == 0)
		errx(1, "%s is empty, must not be.", fname);
	if ((mboot.bootinst_size = sb.st_size) % secsize != 0)
		errx(1, "%s: length must be a multiple of sector size", fname);
	if (mboot.bootinst != NULL)
		free(mboot.bootinst);
	if ((mboot.bootinst = malloc(mboot.bootinst_size = sb.st_size)) == NULL)
		errx(1, "%s: unable to allocate read buffer", fname);
	if ((n = read(fdesc, mboot.bootinst, mboot.bootinst_size)) == -1 ||
	    close(fdesc))
		err(1, "%s", fname);
	if (n != mboot.bootinst_size)
		errx(1, "%s: short read", fname);
}


static void
init_sector0(unsigned long start)
{
	struct dos_partition *partp = &mboot.parts[0];

	init_boot();

	partp->dp_typ = DOSPTYP_386BSD;
	partp->dp_flag = ACTIVE;
	start = roundup(start, dos_sectors);
	if(start == 0)
		start = dos_sectors;
	partp->dp_start = start;
	partp->dp_size = rounddown(disksecs, dos_cylsecs) - start;

	dos(partp);
}

static void
change_part(int i)
{
    struct dos_partition *partp = &mboot.parts[i - 1];

    printf("The data for partition %d is:\n", i);
    print_part(partp);

    if (u_flag && ok("Do you want to change it?")) {
	int tmp;

	if (i_flag) {
	    bzero(partp, sizeof (*partp));
	    if (i == 1) {
		init_sector0(1);
		printf("\nThe static data for the slice 1 has been reinitialized to:\n");
		print_part(partp);
	    }
	}

	do {
		Decimal("sysid (165=FreeBSD)", partp->dp_typ, tmp, 255);
		Decimal("start", partp->dp_start, tmp, NO_DISK_SECTORS);
		Decimal("size", partp->dp_size, tmp, NO_DISK_SECTORS);
		if (!sanitize_partition(partp)) {
			warnx("ERROR: failed to adjust; setting sysid to 0");
			partp->dp_typ = 0;
		}

		if (ok("Explicitly specify beg/end address ?"))
		{
			int	tsec,tcyl,thd;
			tcyl = DPCYL(partp->dp_scyl,partp->dp_ssect);
			thd = partp->dp_shd;
			tsec = DPSECT(partp->dp_ssect);
			Decimal("beginning cylinder", tcyl, tmp, NO_TRACK_CYLINDERS);
			Decimal("beginning head", thd, tmp, NO_TRACK_HEADS);
			Decimal("beginning sector", tsec, tmp, NO_TRACK_SECTORS);
			partp->dp_scyl = DOSCYL(tcyl);
			partp->dp_ssect = DOSSECT(tsec,tcyl);
			partp->dp_shd = thd;

			tcyl = DPCYL(partp->dp_ecyl,partp->dp_esect);
			thd = partp->dp_ehd;
			tsec = DPSECT(partp->dp_esect);
			Decimal("ending cylinder", tcyl, tmp, NO_TRACK_CYLINDERS);
			Decimal("ending head", thd, tmp, NO_TRACK_HEADS);
			Decimal("ending sector", tsec, tmp, NO_TRACK_SECTORS);
			partp->dp_ecyl = DOSCYL(tcyl);
			partp->dp_esect = DOSSECT(tsec,tcyl);
			partp->dp_ehd = thd;
		} else
			dos(partp);

		print_part(partp);
	} while (!ok("Are we happy with this entry?"));
    }
}

static void
print_params()
{
	printf("parameters extracted from in-core disklabel are:\n");
	printf("cylinders=%d heads=%d sectors/track=%d (%d blks/cyl)\n\n"
			,cyls,heads,sectors,cylsecs);
	if (dos_cyls > 1023 || dos_heads > 255 || dos_sectors > 63)
		printf("Figures below won't work with BIOS for partitions not in cyl 1\n");
	printf("parameters to be used for BIOS calculations are:\n");
	printf("cylinders=%d heads=%d sectors/track=%d (%d blks/cyl)\n\n"
		,dos_cyls,dos_heads,dos_sectors,dos_cylsecs);
}

static void
change_active(int which)
{
	struct dos_partition *partp = &mboot.parts[0];
	int active, i, new, tmp;

	active = -1;
	for (i = 0; i < NDOSPART; i++) {
		if ((partp[i].dp_flag & ACTIVE) == 0)
			continue;
		printf("Partition %d is marked active\n", i + 1);
		if (active == -1)
			active = i + 1;
	}
	if (a_flag && which != -1)
		active = which;
	else if (active == -1)
		active = 1;

	if (!ok("Do you want to change the active partition?"))
		return;
setactive:
	do {
		new = active;
		Decimal("active partition", new, tmp, 0);
		if (new < 1 || new > 4) {
			printf("Active partition number must be in range 1-4."
					"  Try again.\n");
			goto setactive;
		}
		active = new;
	} while (!ok("Are you happy with this choice"));
	for (i = 0; i < NDOSPART; i++)
		partp[i].dp_flag = 0;
	if (active > 0 && active <= NDOSPART)
		partp[active-1].dp_flag = ACTIVE;
}

static void
change_code()
{
	if (ok("Do you want to change the boot code?"))
		init_boot();
}

void
get_params_to_use()
{
	int	tmp;
	print_params();
	if (ok("Do you want to change our idea of what BIOS thinks ?"))
	{
		do
		{
			Decimal("BIOS's idea of #cylinders", dos_cyls, tmp, 0);
			Decimal("BIOS's idea of #heads", dos_heads, tmp, 0);
			Decimal("BIOS's idea of #sectors", dos_sectors, tmp, 0);
			dos_cylsecs = dos_heads * dos_sectors;
			print_params();
		}
		while(!ok("Are you happy with this choice"));
	}
}


/***********************************************\
* Change real numbers into strange dos numbers	*
\***********************************************/
static void
dos(struct dos_partition *partp)
{
	int cy, sec;
	u_int32_t end;

	if (partp->dp_typ == 0 && partp->dp_start == 0 && partp->dp_size == 0) {
		memcpy(partp, &mtpart, sizeof(*partp));
		return;
	}

	/* Start c/h/s. */
	partp->dp_shd = partp->dp_start % dos_cylsecs / dos_sectors;
	cy = partp->dp_start / dos_cylsecs;
	sec = partp->dp_start % dos_sectors + 1;
	partp->dp_scyl = DOSCYL(cy);
	partp->dp_ssect = DOSSECT(sec, cy);

	/* End c/h/s. */
	end = partp->dp_start + partp->dp_size - 1;
	partp->dp_ehd = end % dos_cylsecs / dos_sectors;
	cy = end / dos_cylsecs;
	sec = end % dos_sectors + 1;
	partp->dp_ecyl = DOSCYL(cy);
	partp->dp_esect = DOSSECT(sec, cy);
}

static int
open_disk(int flag)
{
	int rwmode;

	/* Write mode if one of these flags are set. */
	rwmode = (a_flag || I_flag || B_flag || flag);
	fd = g_open(disk, rwmode);
	/* If the mode fails, try read-only if we didn't. */
	if (fd == -1 && errno == EPERM && rwmode)
		fd = g_open(disk, 0);
	if (fd == -1 && errno == ENXIO)
		return -2;
	if (fd == -1) {
		warnx("can't open device %s", disk);
		return -1;
	}
	if (get_params() == -1) {
		warnx("can't get disk parameters on %s", disk);
		return -1;
	}
	return fd;
}

static ssize_t
read_disk(off_t sector, void *buf)
{

	lseek(fd, (sector * 512), 0);
	if (secsize == 0)
		for (secsize = MIN_SEC_SIZE; secsize <= MAX_SEC_SIZE;
		     secsize *= 2) {
			/* try the read */
			int size = read(fd, buf, secsize);
			if (size == secsize)
				/* it worked so return */
				return secsize;
		}
	else
		return read(fd, buf, secsize);

	/* we failed to read at any of the sizes */
	return -1;
}

static int
geom_class_available(const char *name)
{
	struct gclass *class;
	struct gmesh mesh;
	int error;

	error = geom_gettree(&mesh);
	if (error != 0)
		errc(1, error, "Cannot get GEOM tree");

	LIST_FOREACH(class, &mesh.lg_class, lg_class) {
		if (strcmp(class->lg_name, name) == 0) {
			geom_deletetree(&mesh);
			return (1);
		}
	}

	geom_deletetree(&mesh);

	return (0);
}

static int
write_disk(off_t sector, void *buf)
{
	struct gctl_req *grq;
	const char *errmsg;
	char *pname;
	int error;

	/* Check that GEOM_MBR is available */
	if (geom_class_available("MBR") != 0) {
		grq = gctl_get_handle();
		gctl_ro_param(grq, "verb", -1, "write MBR");
		gctl_ro_param(grq, "class", -1, "MBR");
		pname = g_providername(fd);
		if (pname == NULL) {
			warn("Error getting providername for %s", disk);
			return (-1);
		}
		gctl_ro_param(grq, "geom", -1, pname);
		gctl_ro_param(grq, "data", secsize, buf);
		errmsg = gctl_issue(grq);
		free(pname);
		if (errmsg == NULL) {
			gctl_free(grq);
			return(0);
		}
		if (!q_flag)
			warnx("GEOM_MBR: %s", errmsg);
		gctl_free(grq);
	} else {
		/*
		 * Try to write MBR directly. This may help when disk
		 * is not in use.
		 * XXX: hardcoded sectorsize
		 */
		error = pwrite(fd, buf, secsize, (sector * 512));
		if (error == secsize)
			return (0);
	}

	/*
	 * GEOM_MBR is not available or failed to write MBR.
	 * Now check that we have GEOM_PART and recommend to use gpart (8).
	 */
	if (geom_class_available("PART") != 0)
		warnx("Failed to write MBR. Try to use gpart(8).");
	else
		warnx("Failed to write sector zero");
	return(EINVAL);
}

static int
get_params()
{
	int error;
	u_int u;
	off_t o;

	error = ioctl(fd, DIOCGFWSECTORS, &u);
	if (error == 0)
		sectors = dos_sectors = u;
	else
		sectors = dos_sectors = 63;

	error = ioctl(fd, DIOCGFWHEADS, &u);
	if (error == 0)
		heads = dos_heads = u;
	else
		heads = dos_heads = 255;

	dos_cylsecs = cylsecs = heads * sectors;
	disksecs = cyls * heads * sectors;

	u = g_sectorsize(fd);
	if (u <= 0)
		return (-1);

	o = g_mediasize(fd);
	if (o < 0)
		return (-1);
	if (o / u <= NO_DISK_SECTORS)
		disksecs = o / u;
	else
		disksecs = NO_DISK_SECTORS;
	cyls = dos_cyls = o / (u * dos_heads * dos_sectors);

	return (0);
}

static int
read_s0()
{
	int i;

	mboot.bootinst_size = secsize;
	if (mboot.bootinst != NULL)
		free(mboot.bootinst);
	if ((mboot.bootinst = malloc(mboot.bootinst_size)) == NULL) {
		warnx("unable to allocate buffer to read fdisk "
		      "partition table");
		return -1;
	}
	if (read_disk(0, mboot.bootinst) == -1) {
		warnx("can't read fdisk partition table");
		return -1;
	}
	if (le16dec(&mboot.bootinst[DOSMAGICOFFSET]) != DOSMAGIC) {
		warnx("invalid fdisk partition table found");
		/* So should we initialize things */
		return -1;
	}
	for (i = 0; i < NDOSPART; i++)
		dos_partition_dec(
		    &mboot.bootinst[DOSPARTOFF + i * DOSPARTSIZE],
		    &mboot.parts[i]);
	return 0;
}

static int
write_s0()
{
	int	sector, i;

	if (iotest) {
		print_s0();
		return 0;
	}
	for(i = 0; i < NDOSPART; i++)
		dos_partition_enc(&mboot.bootinst[DOSPARTOFF + i * DOSPARTSIZE],
		    &mboot.parts[i]);
	le16enc(&mboot.bootinst[DOSMAGICOFFSET], DOSMAGIC);
	for(sector = 0; sector < mboot.bootinst_size / secsize; sector++)
		if (write_disk(sector,
			       &mboot.bootinst[sector * secsize]) == -1) {
			warn("can't write fdisk partition table");
			return -1;
		}
	return(0);
}


static int
ok(const char *str)
{
	printf("%s [n] ", str);
	fflush(stdout);
	if (fgets(lbuf, LBUF, stdin) == NULL)
		exit(1);
	lbuf[strlen(lbuf)-1] = 0;

	if (*lbuf &&
		(!strcmp(lbuf, "yes") || !strcmp(lbuf, "YES") ||
		 !strcmp(lbuf, "y") || !strcmp(lbuf, "Y")))
		return 1;
	else
		return 0;
}

static int
decimal(const char *str, int *num, int deflt, uint32_t maxval)
{
	long long acc;
	int c;
	char *cp;

	while (1) {
		acc = 0;
		printf("Supply a decimal value for \"%s\" [%d] ", str, deflt);
		fflush(stdout);
		if (fgets(lbuf, LBUF, stdin) == NULL)
			exit(1);
		lbuf[strlen(lbuf)-1] = 0;

		if (!*lbuf)
			return 0;

		cp = lbuf;
		while ((c = *cp) && (c == ' ' || c == '\t')) cp++;
		if (!c)
			return 0;
		while ((c = *cp++)) {
			if (c <= '9' && c >= '0') {
				if (acc <= maxval || maxval == 0)
					acc = acc * 10 + c - '0';
			} else
				break;
		}
		if (c == ' ' || c == '\t')
			while ((c = *cp) && (c == ' ' || c == '\t')) cp++;
		if (!c) {
			if (maxval > 0 && acc > maxval) {
				acc = maxval;
				printf("%s exceeds maximum value allowed for "
				  "this field. The value has been reduced "
				  "to %lld\n", lbuf, acc);
			}
			*num = acc;
			return 1;
		} else
			printf("%s is an invalid decimal number.  Try again.\n",
				lbuf);
	}
}


static void
parse_config_line(char *line, CMD *command)
{
    char	*cp, *end;

    cp = line;
    while (1) {
	memset(command, 0, sizeof(*command));

	while (isspace(*cp)) ++cp;
	if (*cp == '\0' || *cp == '#')
	    break;
	command->cmd = *cp++;

	/*
	 * Parse args
	 */
	    while (1) {
	    while (isspace(*cp)) ++cp;
	    if (*cp == '\0')
		break;		/* eol */
	    if (*cp == '#')
		break;		/* found comment */
	    if (isalpha(*cp))
		command->args[command->n_args].argtype = *cp++;
	    end = NULL;
	    command->args[command->n_args].arg_val = strtoul(cp, &end, 0);
 	    if (cp == end || (!isspace(*end) && *end != '\0')) {
 		char ch;
 		end = cp;
 		while (!isspace(*end) && *end != '\0') ++end;
 		ch = *end; *end = '\0';
 		command->args[command->n_args].arg_str = strdup(cp);
 		*end = ch;
 	    } else
 		command->args[command->n_args].arg_str = NULL;
	    cp = end;
	    command->n_args++;
	}
	break;
    }
}


static int
process_geometry(CMD *command)
{
    int		status = 1, i;

    while (1) {
	geom_processed = 1;
	    if (part_processed) {
	    warnx(
	"ERROR line %d: the geometry specification line must occur before\n\
    all partition specifications",
		    current_line_number);
	    status = 0;
	    break;
	}
	    if (command->n_args != 3) {
	    warnx("ERROR line %d: incorrect number of geometry args",
		    current_line_number);
	    status = 0;
	    break;
	}
	    dos_cyls = 0;
	    dos_heads = 0;
	    dos_sectors = 0;
	    for (i = 0; i < 3; ++i) {
		    switch (command->args[i].argtype) {
	    case 'c':
		dos_cyls = command->args[i].arg_val;
		break;
	    case 'h':
		dos_heads = command->args[i].arg_val;
		break;
	    case 's':
		dos_sectors = command->args[i].arg_val;
		break;
	    default:
		warnx(
		"ERROR line %d: unknown geometry arg type: '%c' (0x%02x)",
			current_line_number, command->args[i].argtype,
			command->args[i].argtype);
		status = 0;
		break;
	    }
	}
	if (status == 0)
	    break;

	dos_cylsecs = dos_heads * dos_sectors;

	/*
	 * Do sanity checks on parameter values
	 */
	    if (dos_cyls == 0) {
	    warnx("ERROR line %d: number of cylinders not specified",
		    current_line_number);
	    status = 0;
	}
	    if (dos_cyls > 1024) {
	    warnx(
	"WARNING line %d: number of cylinders (%d) may be out-of-range\n\
    (must be within 1-1024 for normal BIOS operation, unless the entire disk\n\
    is dedicated to FreeBSD)",
		    current_line_number, dos_cyls);
	}

	    if (dos_heads == 0) {
	    warnx("ERROR line %d: number of heads not specified",
		    current_line_number);
	    status = 0;
	    } else if (dos_heads > 256) {
	    warnx("ERROR line %d: number of heads must be within (1-256)",
		    current_line_number);
	    status = 0;
	}

	    if (dos_sectors == 0) {
	    warnx("ERROR line %d: number of sectors not specified",
		    current_line_number);
	    status = 0;
	    } else if (dos_sectors > 63) {
	    warnx("ERROR line %d: number of sectors must be within (1-63)",
		    current_line_number);
	    status = 0;
	}

	break;
    }
    return (status);
}

static u_int32_t
str2sectors(const char *str)
{
	char *end;
	unsigned long val;

	val = strtoul(str, &end, 0);
	if (str == end || *end == '\0') {
		warnx("ERROR line %d: unexpected size: \'%s\'",
		    current_line_number, str);
		return NO_DISK_SECTORS;
	}

	if (*end == 'K')
		val *= 1024UL / secsize;
	else if (*end == 'M')
		val *= 1024UL * 1024UL / secsize;
	else if (*end == 'G')
		val *= 1024UL * 1024UL * 1024UL / secsize;
	else {
		warnx("ERROR line %d: unexpected modifier: %c "
		    "(not K/M/G)", current_line_number, *end);
		return NO_DISK_SECTORS;
	}

	return val;
}

static int
process_partition(CMD *command)
{
    int				status = 0, partition;
    u_int32_t			prev_head_boundary, prev_cyl_boundary;
    u_int32_t			adj_size, max_end;
    struct dos_partition	*partp;

	while (1) {
	part_processed = 1;
		if (command->n_args != 4) {
	    warnx("ERROR line %d: incorrect number of partition args",
		    current_line_number);
	    break;
	}
	partition = command->args[0].arg_val;
		if (partition < 1 || partition > 4) {
	    warnx("ERROR line %d: invalid partition number %d",
		    current_line_number, partition);
	    break;
	}
	partp = &mboot.parts[partition - 1];
	bzero(partp, sizeof (*partp));
	partp->dp_typ = command->args[1].arg_val;
	if (command->args[2].arg_str != NULL) {
		if (strcmp(command->args[2].arg_str, "*") == 0) {
			int i;
			partp->dp_start = dos_sectors;
			for (i = 1; i < partition; i++) {
    				struct dos_partition *prev_partp;
				prev_partp = ((struct dos_partition *)
				    &mboot.parts) + i - 1;
				if (prev_partp->dp_typ != 0)
					partp->dp_start = prev_partp->dp_start +
					    prev_partp->dp_size;
			}
			if (partp->dp_start % dos_sectors != 0) {
				prev_head_boundary = rounddown(partp->dp_start,
				    dos_sectors);
		    		partp->dp_start = prev_head_boundary +
				    dos_sectors;
			}
		} else {
			partp->dp_start = str2sectors(command->args[2].arg_str);
			if (partp->dp_start == NO_DISK_SECTORS)
				break;
		}
	} else
		partp->dp_start = command->args[2].arg_val;

	if (command->args[3].arg_str != NULL) {
		if (strcmp(command->args[3].arg_str, "*") == 0)
			partp->dp_size = rounddown(disksecs, dos_cylsecs) -
			    partp->dp_start;
		else {
			partp->dp_size = str2sectors(command->args[3].arg_str);
			if (partp->dp_size == NO_DISK_SECTORS)
				break;
		}
		prev_cyl_boundary = rounddown(partp->dp_start + partp->dp_size,
		    dos_cylsecs);
		if (prev_cyl_boundary > partp->dp_start)
			partp->dp_size = prev_cyl_boundary - partp->dp_start;
	} else
		partp->dp_size = command->args[3].arg_val;

	max_end = partp->dp_start + partp->dp_size;

	if (partp->dp_typ == 0) {
	    /*
	     * Get out, the partition is marked as unused.
	     */
	    /*
	     * Insure that it's unused.
	     */
	    bzero(partp, sizeof(*partp));
	    status = 1;
	    break;
	}

	/*
	 * Adjust start upwards, if necessary, to fall on a head boundary.
	 */
		if (partp->dp_start % dos_sectors != 0) {
	    prev_head_boundary = rounddown(partp->dp_start, dos_sectors);
	    if (max_end < dos_sectors ||
			    prev_head_boundary > max_end - dos_sectors) {
		/*
		 * Can't go past end of partition
		 */
		warnx(
	"ERROR line %d: unable to adjust start of partition %d to fall on\n\
    a head boundary",
			current_line_number, partition);
		break;
	    }
	    warnx(
	"WARNING: adjusting start offset of partition %d\n\
    from %u to %u, to fall on a head boundary",
		    partition, (u_int)partp->dp_start,
		    (u_int)(prev_head_boundary + dos_sectors));
	    partp->dp_start = prev_head_boundary + dos_sectors;
	}

	/*
	 * Adjust size downwards, if necessary, to fall on a cylinder
	 * boundary.
	 */
	prev_cyl_boundary = rounddown(partp->dp_start + partp->dp_size,
	    dos_cylsecs);
	if (prev_cyl_boundary > partp->dp_start)
	    adj_size = prev_cyl_boundary - partp->dp_start;
		else {
	    warnx(
	"ERROR: could not adjust partition to start on a head boundary\n\
    and end on a cylinder boundary.");
	    return (0);
	}
		if (adj_size != partp->dp_size) {
	    warnx(
	"WARNING: adjusting size of partition %d from %u to %u\n\
    to end on a cylinder boundary",
		    partition, (u_int)partp->dp_size, (u_int)adj_size);
	    partp->dp_size = adj_size;
	}
		if (partp->dp_size == 0) {
	    warnx("ERROR line %d: size of partition %d is zero",
		    current_line_number, partition);
	    break;
	}

	dos(partp);
	status = 1;
	break;
    }
    return (status);
}


static int
process_active(CMD *command)
{
    int				status = 0, partition, i;
    struct dos_partition	*partp;

	while (1) {
	active_processed = 1;
		if (command->n_args != 1) {
	    warnx("ERROR line %d: incorrect number of active args",
		    current_line_number);
	    status = 0;
	    break;
	}
	partition = command->args[0].arg_val;
		if (partition < 1 || partition > 4) {
	    warnx("ERROR line %d: invalid partition number %d",
		    current_line_number, partition);
	    break;
	}
	/*
	 * Reset active partition
	 */
	partp = mboot.parts;
	for (i = 0; i < NDOSPART; i++)
	    partp[i].dp_flag = 0;
	partp[partition-1].dp_flag = ACTIVE;

	status = 1;
	break;
    }
    return (status);
}


static int
process_line(char *line)
{
    CMD		command;
    int		status = 1;

	while (1) {
	parse_config_line(line, &command);
		switch (command.cmd) {
	case 0:
	    /*
	     * Comment or blank line
	     */
	    break;
	case 'g':
	    /*
	     * Set geometry
	     */
	    status = process_geometry(&command);
	    break;
	case 'p':
	    status = process_partition(&command);
	    break;
	case 'a':
	    status = process_active(&command);
	    break;
	default:
	    status = 0;
	    break;
	}
	break;
    }
    return (status);
}


static int
read_config(char *config_file)
{
    FILE	*fp = NULL;
    int		status = 1;
    char	buf[1010];

	while (1) {
		if (strcmp(config_file, "-") != 0) {
	    /*
	     * We're not reading from stdin
	     */
			if ((fp = fopen(config_file, "r")) == NULL) {
		status = 0;
		break;
	    }
		} else {
	    fp = stdin;
	}
	current_line_number = 0;
		while (!feof(fp)) {
	    if (fgets(buf, sizeof(buf), fp) == NULL)
		break;
	    ++current_line_number;
	    status = process_line(buf);
	    if (status == 0)
		break;
	    }
	break;
    }
	if (fp) {
	/*
	 * It doesn't matter if we're reading from stdin, as we've reached EOF
	 */
	fclose(fp);
    }
    return (status);
}


static void
reset_boot(void)
{
    int				i;
    struct dos_partition	*partp;

    init_boot();
    for (i = 0; i < 4; ++i) {
	partp = &mboot.parts[i];
	bzero(partp, sizeof(*partp));
    }
}

static int
sanitize_partition(struct dos_partition *partp)
{
    u_int32_t			prev_head_boundary, prev_cyl_boundary;
    u_int32_t			max_end, size, start;

    start = partp->dp_start;
    size = partp->dp_size;
    max_end = start + size;
    /* Only allow a zero size if the partition is being marked unused. */
    if (size == 0) {
	if (start == 0 && partp->dp_typ == 0)
	    return (1);
	warnx("ERROR: size of partition is zero");
	return (0);
    }
    /* Return if no adjustment is necessary. */
    if (start % dos_sectors == 0 && (start + size) % dos_sectors == 0)
	return (1);

    if (start == 0) {
	    warnx("WARNING: partition overlaps with partition table");
	    if (ok("Correct this automatically?"))
		    start = dos_sectors;
    }
    if (start % dos_sectors != 0)
	warnx("WARNING: partition does not start on a head boundary");
    if ((start  +size) % dos_sectors != 0)
	warnx("WARNING: partition does not end on a cylinder boundary");
    warnx("WARNING: this may confuse the BIOS or some operating systems");
    if (!ok("Correct this automatically?"))
	return (1);

    /*
     * Adjust start upwards, if necessary, to fall on a head boundary.
     */
    if (start % dos_sectors != 0) {
	prev_head_boundary = rounddown(start, dos_sectors);
	if (max_end < dos_sectors ||
	    prev_head_boundary >= max_end - dos_sectors) {
	    /*
	     * Can't go past end of partition
	     */
	    warnx(
    "ERROR: unable to adjust start of partition to fall on a head boundary");
	    return (0);
        }
	start = prev_head_boundary + dos_sectors;
    }

    /*
     * Adjust size downwards, if necessary, to fall on a cylinder
     * boundary.
     */
    prev_cyl_boundary = rounddown(start + size, dos_cylsecs);
    if (prev_cyl_boundary > start)
	size = prev_cyl_boundary - start;
    else {
	warnx("ERROR: could not adjust partition to start on a head boundary\n\
    and end on a cylinder boundary.");
	return (0);
    }

    /* Finally, commit any changes to partp and return. */
    if (start != partp->dp_start) {
	warnx("WARNING: adjusting start offset of partition to %u",
	    (u_int)start);
	partp->dp_start = start;
    }
    if (size != partp->dp_size) {
	warnx("WARNING: adjusting size of partition to %u", (u_int)size);
	partp->dp_size = size;
    }

    return (1);
}

/*
 * Try figuring out the root device's canonical disk name.
 * The following choices are considered:
 *   /dev/ad0s1a     => /dev/ad0
 *   /dev/da0a       => /dev/da0
 *   /dev/vinum/root => /dev/vinum/root
 * A ".eli" part is removed if it exists (see geli(8)).
 * A ".journal" ending is removed if it exists (see gjournal(8)).
 */
static char *
get_rootdisk(void)
{
	struct statfs rootfs;
	regex_t re;
#define NMATCHES 2
	regmatch_t rm[NMATCHES];
	char dev[PATH_MAX], *s;
	int rv;

	if (statfs("/", &rootfs) == -1)
		err(1, "statfs(\"/\")");

	if ((rv = regcomp(&re, "^(/dev/[a-z/]+[0-9]*)([sp][0-9]+)?[a-h]?(\\.journal)?$",
		    REG_EXTENDED)) != 0)
		errx(1, "regcomp() failed (%d)", rv);
	strlcpy(dev, rootfs.f_mntfromname, sizeof (dev));
	if ((s = strstr(dev, ".eli")) != NULL)
	    memmove(s, s+4, strlen(s + 4) + 1);

	if ((rv = regexec(&re, dev, NMATCHES, rm, 0)) != 0)
		errx(1,
"mounted root fs resource doesn't match expectations (regexec returned %d)",
		    rv);
	if ((s = malloc(rm[1].rm_eo - rm[1].rm_so + 1)) == NULL)
		errx(1, "out of memory");
	memcpy(s, rootfs.f_mntfromname + rm[1].rm_so,
	    rm[1].rm_eo - rm[1].rm_so);
	s[rm[1].rm_eo - rm[1].rm_so] = 0;

	return s;
}
