/*	$OpenBSD: pstat.c,v 1.130 2024/07/10 13:29:23 krw Exp $	*/
/*	$NetBSD: pstat.c,v 1.27 1996/10/23 22:50:06 cgd Exp $	*/

/*-
 * Copyright (c) 1980, 1991, 1993
 *	The Regents of the University of California.  All rights reserved.
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
 */

#include <sys/param.h>	/* DEV_BSIZE */
#include <sys/types.h>
#include <sys/signal.h>
#include <sys/proc.h>
#include <sys/time.h>
#include <sys/vnode.h>
#include <sys/ucred.h>
#include <sys/stat.h>
#define _KERNEL
#include <sys/file.h>
#include <ufs/ufs/quota.h>
#include <ufs/ufs/inode.h>
#include <sys/mount.h>
#undef _KERNEL
#include <nfs/nfsproto.h>
#include <nfs/rpcv2.h>
#include <nfs/nfsnode.h>
#include <sys/ioctl.h>
#include <sys/tty.h>
#include <sys/conf.h>
#include <sys/device.h>
#include <sys/swap.h>

#include <sys/sysctl.h>

#include <stdint.h>
#include <endian.h>
#include <err.h>
#include <fcntl.h>
#include <kvm.h>
#include <limits.h>
#include <nlist.h>
#include <paths.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

struct nlist vnodenl[] = {
#define	FNL_NFILE	0		/* sysctl */
	{"_numfiles"},
#define FNL_MAXFILE	1		/* sysctl */
	{"_maxfiles"},
#define TTY_NTTY	2		/* sysctl */
	{"_tty_count"},
#define V_NUMV		3		/* sysctl */
	{ "_numvnodes" },
#define TTY_TTYLIST	4		/* sysctl */
	{"_ttylist"},
#define	V_MOUNTLIST	5		/* no sysctl */
	{ "_mountlist" },
	{ NULL }
};

struct itty *globalitp;
struct kinfo_file *kf;
struct nlist *globalnl;

struct e_vnode {
	struct vnode *vptr;
	struct vnode vnode;
};

int	kflag;
int	totalflag;
int	usenumflag;
int	hideroot;
int	maxfile;
int	need_nlist;
int	nfile;
int	ntty;
int	numvnodes;
char	*nlistf	= NULL;
char	*memf	= NULL;
kvm_t	*kd = NULL;

#define	SVAR(var) __STRING(var)	/* to force expansion */
#define	KGET(idx, var)							\
	KGET1(idx, &var, sizeof(var), SVAR(var))
#define	KGET1(idx, p, s, msg)						\
	KGET2(globalnl[idx].n_value, p, s, msg)
#define	KGET2(addr, p, s, msg)						\
	if (kvm_read(kd, (u_long)(addr), p, s) != s)			\
		warnx("cannot read %s: %s", msg, kvm_geterr(kd))
#define	KGETRET(addr, p, s, msg)					\
	if (kvm_read(kd, (u_long)(addr), p, s) != s) {			\
		warnx("cannot read %s: %s", msg, kvm_geterr(kd));	\
		return (0);						\
	}

void	filemode(void);
void	filemodeprep(void);
struct mount *
	getmnt(struct mount *);
struct e_vnode *
	kinfo_vnodes(void);
void	mount_print(struct mount *);
void	nfs_header(void);
int	nfs_print(struct vnode *);
void	swapmode(void);
void	ttymode(void);
void	ttymodeprep(void);
void	ttyprt(struct itty *);
void	tty2itty(struct tty *tp, struct itty *itp);
void	ufs_header(void);
int	ufs_print(struct vnode *);
void	ext2fs_header(void);
int	ext2fs_print(struct vnode *);
static void __dead	usage(void);
void	vnode_header(void);
void	vnode_print(struct vnode *, struct vnode *);
void	vnodemode(void);
void	vnodemodeprep(void);


int
main(int argc, char *argv[])
{
	int fileflag = 0, swapflag = 0, ttyflag = 0, vnodeflag = 0, ch;
	char buf[_POSIX2_LINE_MAX];
	const char *dformat = NULL;
	int i;

	hideroot = getuid();

	while ((ch = getopt(argc, argv, "d:TM:N:fiknstv")) != -1)
		switch (ch) {
		case 'd':
			dformat = optarg;
			break;
		case 'f':
			fileflag = 1;
			break;
		case 'M':
			memf = optarg;
			break;
		case 'N':
			nlistf = optarg;
			break;
		case 'n':
			usenumflag = 1;
			break;
		case 's':
			swapflag = 1;
			break;
		case 'T':
			totalflag = 1;
			break;
		case 't':
			ttyflag = 1;
			break;
		case 'k':
			kflag = 1;
			break;
		case 'v':
		case 'i':		/* Backward compatibility. */
			vnodeflag = 1;
			break;
		default:
			usage();
		}
	argc -= optind;
	argv += optind;

	if (dformat && getuid())
		errx(1, "Only root can use -d");

	if ((dformat == NULL && argc > 0) || (dformat && argc == 0))
		usage();

	need_nlist = dformat || vnodeflag;

	if (nlistf != NULL || memf != NULL) {
		if (fileflag || totalflag)
			need_nlist = 1;
	}

	if (vnodeflag || fileflag || dformat || need_nlist)
		if ((kd = kvm_openfiles(nlistf, memf, NULL,
		    O_RDONLY | (need_nlist ? 0 : KVM_NO_FILES), buf)) == 0)
			errx(1, "kvm_openfiles: %s", buf);

	if (need_nlist)
		if (kvm_nlist(kd, vnodenl) == -1)
			errx(1, "kvm_nlist: %s", kvm_geterr(kd));

	if (!(fileflag | vnodeflag | ttyflag | swapflag | totalflag || dformat))
		usage();

	if(!dformat) {
		if (fileflag || totalflag)
			filemodeprep();
		if (vnodeflag || totalflag)
			vnodemodeprep();
		if (ttyflag)
			ttymodeprep();
	}

	if (unveil(_PATH_DEVDB, "r") == -1)
		err(1, "unveil %s", _PATH_DEVDB);
	if (pledge("stdio rpath vminfo", NULL) == -1)
		err(1, "pledge");

	if (dformat) {
		struct nlist *nl;
		int longformat = 0, stringformat = 0, error = 0, n;
		uint32_t mask = ~0;
		char format[10], buf[1024];
		
		n = strlen(dformat);
		if (n == 0)
			errx(1, "illegal format");

		/*
		 * Support p, c, s, and {l, ll, h, hh, j, t, z, }[diouxX]
		 */
		if (strcmp(dformat, "p") == 0)
			longformat = sizeof(long) == 8;
		else if (strcmp(dformat, "c") == 0)
			mask = 0xff;
		else if (strcmp(dformat, "s") == 0)
			stringformat = 1;
		else if (strchr("diouxX", dformat[n - 1])) {
			char *ptbl[]= {"l", "ll", "h", "hh", "j", "t", "z", ""};
			int i;

			char *mod;
			for (i = 0; i < sizeof(ptbl)/sizeof(ptbl[0]); i++) {
				mod = ptbl[i];
				if (strlen(mod) == n - 1 &&
				    strncmp(mod, dformat, strlen(mod)) == 0)
					break;
			}
			if (i == sizeof(ptbl)/sizeof(ptbl[0])
			    && dformat[1] != '\0')
				errx(1, "illegal format");
			if (strcmp(mod, "l") == 0)
				longformat = sizeof(long) == sizeof(long long);
			else if (strcmp(mod, "h") == 0)
				mask = 0xffff;
			else if (strcmp(mod, "hh") == 0)
				mask = 0xff;
			else if (strcmp(mod, "") != 0)
				longformat = 1;

		} else
			errx(1, "illegal format");

		if (*dformat == 's') {
			stringformat = 1;
			snprintf(format, sizeof(format), "%%.%zus",
			    sizeof buf);
		} else
			snprintf(format, sizeof(format), "%%%s", dformat);

		nl = calloc(argc + 1, sizeof *nl);
		if (!nl)
			err(1, "calloc nl: ");
		for (i = 0; i < argc; i++) {
			if (asprintf(&nl[i].n_name, "_%s",
			    argv[i]) == -1)
				warn("asprintf");
		}
		kvm_nlist(kd, nl);
		globalnl = nl;
		for (i = 0; i < argc; i++) {
			uint64_t v;

			printf("%s ", argv[i]);
			if (!nl[i].n_value && argv[i][0] == '0') {
				nl[i].n_value = strtoul(argv[i], NULL, 16);
				nl[i].n_type = N_DATA;
			}
			if (!nl[i].n_value) {
				printf("not found\n");
				error++;
				continue;
			}

			printf("at %p: ", (void *)nl[i].n_value);
			if ((nl[i].n_type & N_TYPE) == N_DATA ||
			    (nl[i].n_type & N_TYPE) == N_COMM) {
				if (stringformat) {
					KGET1(i, &buf, sizeof(buf), argv[i]);
					buf[sizeof(buf) - 1] = '\0';
				} else
					KGET1(i, &v, sizeof(v), argv[i]);
				if (stringformat)
					printf(format, &buf);
				else if (longformat)
					printf(format, v);
				else {
#if BYTE_ORDER == BIG_ENDIAN
					switch (mask) {
					case 0xff:
						v >>= 8;
						/* FALLTHROUGH */
					case 0xffff:
						v >>= 16;
						/* FALLTHROUGH */
					case 0xffffffff:
						v >>= 32;
						break;
					}
#endif
					printf(format, ((uint32_t)v) & mask);
				}
			}
			printf("\n");
		}
		for (i = 0; i < argc; i++)
			free(nl[i].n_name);
		free(nl);
		return error;
	}

	if (fileflag || totalflag)
		filemode();
	if (vnodeflag || totalflag)
		vnodemode();
	if (ttyflag)
		ttymode();
	if (swapflag || totalflag)
		swapmode();
	return 0;
}

void
vnodemode(void)
{
	struct e_vnode *e_vnodebase, *endvnode, *evp;
	struct vnode *vp;
	struct mount *maddr, *mp = NULL;

	globalnl = vnodenl;

	e_vnodebase = kinfo_vnodes();
	if (totalflag) {
		(void)printf("%7d vnodes\n", numvnodes);
		return;
	}
	if (!e_vnodebase)
		return;
	endvnode = e_vnodebase + numvnodes;
	(void)printf("%d active vnodes\n", numvnodes);

	maddr = NULL;
	for (evp = e_vnodebase; evp < endvnode; evp++) {
		vp = &evp->vnode;
		if (vp->v_mount != maddr) {
			/*
			 * New filesystem
			 */
			if ((mp = getmnt(vp->v_mount)) == NULL)
				continue;
			maddr = vp->v_mount;
			mount_print(mp);
			vnode_header();
			if (!strncmp(mp->mnt_stat.f_fstypename, MOUNT_FFS, MFSNAMELEN) ||
			    !strncmp(mp->mnt_stat.f_fstypename, MOUNT_MFS, MFSNAMELEN)) {
				ufs_header();
			} else if (!strncmp(mp->mnt_stat.f_fstypename, MOUNT_NFS,
			    MFSNAMELEN)) {
				nfs_header();
			} else if (!strncmp(mp->mnt_stat.f_fstypename, MOUNT_EXT2FS,
			    MFSNAMELEN)) {
				ext2fs_header();
			}
			(void)printf("\n");
		}
		vnode_print(evp->vptr, vp);

		/* Syncer vnodes have no associated fs-specific data */
		if (vp->v_data == NULL) {
			printf(" %6c %5c %7c\n", '-', '-', '-');
			continue;
		}

		if (!strncmp(mp->mnt_stat.f_fstypename, MOUNT_FFS, MFSNAMELEN) ||
		    !strncmp(mp->mnt_stat.f_fstypename, MOUNT_MFS, MFSNAMELEN)) {
			ufs_print(vp);
		} else if (!strncmp(mp->mnt_stat.f_fstypename, MOUNT_NFS, MFSNAMELEN)) {
			nfs_print(vp);
		} else if (!strncmp(mp->mnt_stat.f_fstypename, MOUNT_EXT2FS,
		    MFSNAMELEN)) {
			ext2fs_print(vp);
		}
		(void)printf("\n");
	}
	free(e_vnodebase);
}

void
vnodemodeprep(void)
{
	int mib[2];
	size_t num;

	if (kd == NULL) {
		mib[0] = CTL_KERN;
		mib[1] = KERN_NUMVNODES;
		num = sizeof(numvnodes);
		if (sysctl(mib, 2, &numvnodes, &num, NULL, 0) < 0)
			err(1, "sysctl(KERN_NUMVNODES) failed");
	}
}

void
vnode_header(void)
{
	(void)printf("%*s TYP VFLAG  USE HOLD", 2 * (int)sizeof(long), "ADDR");
}

void
vnode_print(struct vnode *avnode, struct vnode *vp)
{
	char *type, flags[16];
	char *fp;
	int flag;

	/*
	 * set type
	 */
	switch (vp->v_type) {
	case VNON:
		type = "non"; break;
	case VREG:
		type = "reg"; break;
	case VDIR:
		type = "dir"; break;
	case VBLK:
		type = "blk"; break;
	case VCHR:
		type = "chr"; break;
	case VLNK:
		type = "lnk"; break;
	case VSOCK:
		type = "soc"; break;
	case VFIFO:
		type = "fif"; break;
	case VBAD:
		type = "bad"; break;
	default:
		type = "unk"; break;
	}
	/*
	 * gather flags
	 */
	fp = flags;
	flag = vp->v_flag;
	if (flag & VROOT)
		*fp++ = 'R';
	if (flag & VTEXT)
		*fp++ = 'T';
	if (flag & VSYSTEM)
		*fp++ = 'S';
	if (flag & VISTTY)
		*fp++ = 'I';
	if (flag & VXLOCK)
		*fp++ = 'L';
	if (flag & VXWANT)
		*fp++ = 'W';
	if (vp->v_bioflag & VBIOWAIT)
		*fp++ = 'B';
	if (flag & VALIASED)
		*fp++ = 'A';
	if (vp->v_bioflag & VBIOONFREELIST)
		*fp++ = 'F';
	if (flag & VLOCKSWORK)
		*fp++ = 'l';
	if (vp->v_bioflag & VBIOONSYNCLIST)
		*fp++ = 's';
	if (fp == flags)
		*fp++ = '-';
	*fp = '\0';
	(void)printf("%0*lx %s %5s %4d %4u",
	    2 * (int)sizeof(long), hideroot ? 0L : (long)avnode,
	    type, flags, vp->v_usecount, vp->v_holdcnt);
}

void
ufs_header(void)
{
	(void)printf(" FILEID IFLAG RDEV|SZ");
}

int
ufs_print(struct vnode *vp)
{
	int flag;
	struct inode inode, *ip = &inode;
	struct ufs1_dinode di1;
	char flagbuf[16], *flags = flagbuf;
	char *name;
	mode_t type;

	KGETRET(VTOI(vp), &inode, sizeof(struct inode), "vnode's inode");
	KGETRET(inode.i_din1, &di1, sizeof(struct ufs1_dinode),
	    "vnode's dinode");

	inode.i_din1 = &di1;
	flag = ip->i_flag;
#if 0
	if (flag & IN_LOCKED)
		*flags++ = 'L';
	if (flag & IN_WANTED)
		*flags++ = 'W';
	if (flag & IN_LWAIT)
		*flags++ = 'Z';
#endif
	if (flag & IN_ACCESS)
		*flags++ = 'A';
	if (flag & IN_CHANGE)
		*flags++ = 'C';
	if (flag & IN_UPDATE)
		*flags++ = 'U';
	if (flag & IN_MODIFIED)
		*flags++ = 'M';
	if (flag & IN_LAZYMOD)
		*flags++ = 'm';
	if (flag & IN_RENAME)
		*flags++ = 'R';
	if (flag & IN_SHLOCK)
		*flags++ = 'S';
	if (flag & IN_EXLOCK)
		*flags++ = 'E';
	if (flag == 0)
		*flags++ = '-';
	*flags = '\0';

	(void)printf(" %6d %5s", ip->i_number, flagbuf);
	type = ip->i_ffs1_mode & S_IFMT;
	if (S_ISCHR(ip->i_ffs1_mode) || S_ISBLK(ip->i_ffs1_mode))
		if (usenumflag ||
		    ((name = devname(ip->i_ffs1_rdev, type)) == NULL))
			(void)printf("   %2u,%-2u",
			    major(ip->i_ffs1_rdev), minor(ip->i_ffs1_rdev));
		else
			(void)printf(" %7s", name);
	else
		(void)printf(" %7lld", (long long)ip->i_ffs1_size);
	return (0);
}

void
ext2fs_header(void)
{
	(void)printf(" FILEID IFLAG SZ");
}

int
ext2fs_print(struct vnode *vp)
{
	int flag;
	struct inode inode, *ip = &inode;
	struct ext2fs_dinode di;
	char flagbuf[16], *flags = flagbuf;

	KGETRET(VTOI(vp), &inode, sizeof(struct inode), "vnode's inode");
	KGETRET(inode.i_e2din, &di, sizeof(struct ext2fs_dinode),
	    "vnode's dinode");

	inode.i_e2din = &di;
	flag = ip->i_flag;

#if 0
	if (flag & IN_LOCKED)
		*flags++ = 'L';
	if (flag & IN_WANTED)
		*flags++ = 'W';
	if (flag & IN_LWAIT)
		*flags++ = 'Z';
#endif
	if (flag & IN_ACCESS)
		*flags++ = 'A';
	if (flag & IN_CHANGE)
		*flags++ = 'C';
	if (flag & IN_UPDATE)
		*flags++ = 'U';
	if (flag & IN_MODIFIED)
		*flags++ = 'M';
	if (flag & IN_RENAME)
		*flags++ = 'R';
	if (flag & IN_SHLOCK)
		*flags++ = 'S';
	if (flag & IN_EXLOCK)
		*flags++ = 'E';
	if (flag == 0)
		*flags++ = '-';
	*flags = '\0';

	(void)printf(" %6d %5s %2d", ip->i_number, flagbuf, ip->i_e2fs_size);
	return (0);
}

void
nfs_header(void)
{
	(void)printf(" FILEID NFLAG RDEV|SZ");
}

int
nfs_print(struct vnode *vp)
{
	struct nfsnode nfsnode, *np = &nfsnode;
	char flagbuf[16], *flags = flagbuf;
	int flag;
	char *name;
	mode_t type;

	KGETRET(VTONFS(vp), &nfsnode, sizeof(nfsnode), "vnode's nfsnode");
	flag = np->n_flag;
	if (flag & NFLUSHWANT)
		*flags++ = 'W';
	if (flag & NFLUSHINPROG)
		*flags++ = 'P';
	if (flag & NMODIFIED)
		*flags++ = 'M';
	if (flag & NWRITEERR)
		*flags++ = 'E';
	if (flag & NACC)
		*flags++ = 'A';
	if (flag & NUPD)
		*flags++ = 'U';
	if (flag & NCHG)
		*flags++ = 'C';
	if (flag == 0)
		*flags++ = '-';
	*flags = '\0';

	(void)printf(" %6lld %5s", (long long)np->n_vattr.va_fileid, flagbuf);
	type = np->n_vattr.va_mode & S_IFMT;
	if (S_ISCHR(np->n_vattr.va_mode) || S_ISBLK(np->n_vattr.va_mode))
		if (usenumflag ||
		    ((name = devname(np->n_vattr.va_rdev, type)) == NULL))
			(void)printf("   %2u,%-2u", major(np->n_vattr.va_rdev),
			    minor(np->n_vattr.va_rdev));
		else
			(void)printf(" %7s", name);
	else
		(void)printf(" %7lld", (long long)np->n_size);
	return (0);
}

/*
 * Given a pointer to a mount structure in kernel space,
 * read it in and return a usable pointer to it.
 */
struct mount *
getmnt(struct mount *maddr)
{
	static struct mtab {
		struct mtab *next;
		struct mount *maddr;
		struct mount mount;
	} *mhead = NULL;
	struct mtab *mt;

	for (mt = mhead; mt != NULL; mt = mt->next)
		if (maddr == mt->maddr)
			return (&mt->mount);
	if ((mt = malloc(sizeof(struct mtab))) == NULL)
		err(1, "malloc: mount table");
	KGETRET(maddr, &mt->mount, sizeof(struct mount), "mount table");
	mt->maddr = maddr;
	mt->next = mhead;
	mhead = mt;
	return (&mt->mount);
}

void
mount_print(struct mount *mp)
{
	int flags;

	(void)printf("*** MOUNT ");
	(void)printf("%.*s %s on %s", MFSNAMELEN,
	    mp->mnt_stat.f_fstypename, mp->mnt_stat.f_mntfromname,
	    mp->mnt_stat.f_mntonname);
	if ((flags = mp->mnt_flag)) {
		char *comma = "(";

		putchar(' ');
		/* user visible flags */
		if (flags & MNT_RDONLY) {
			(void)printf("%srdonly", comma);
			flags &= ~MNT_RDONLY;
			comma = ",";
		}
		if (flags & MNT_SYNCHRONOUS) {
			(void)printf("%ssynchronous", comma);
			flags &= ~MNT_SYNCHRONOUS;
			comma = ",";
		}
		if (flags & MNT_NOEXEC) {
			(void)printf("%snoexec", comma);
			flags &= ~MNT_NOEXEC;
			comma = ",";
		}
		if (flags & MNT_NOSUID) {
			(void)printf("%snosuid", comma);
			flags &= ~MNT_NOSUID;
			comma = ",";
		}
		if (flags & MNT_NODEV) {
			(void)printf("%snodev", comma);
			flags &= ~MNT_NODEV;
			comma = ",";
		}
		if (flags & MNT_NOPERM) {
			(void)printf("%snoperm", comma);
			flags &= ~MNT_NOPERM;
			comma = ",";
		}
		if (flags & MNT_ASYNC) {
			(void)printf("%sasync", comma);
			flags &= ~MNT_ASYNC;
			comma = ",";
		}
		if (flags & MNT_EXRDONLY) {
			(void)printf("%sexrdonly", comma);
			flags &= ~MNT_EXRDONLY;
			comma = ",";
		}
		if (flags & MNT_EXPORTED) {
			(void)printf("%sexport", comma);
			flags &= ~MNT_EXPORTED;
			comma = ",";
		}
		if (flags & MNT_DEFEXPORTED) {
			(void)printf("%sdefdexported", comma);
			flags &= ~MNT_DEFEXPORTED;
			comma = ",";
		}
		if (flags & MNT_EXPORTANON) {
			(void)printf("%sexportanon", comma);
			flags &= ~MNT_EXPORTANON;
			comma = ",";
		}
		if (flags & MNT_WXALLOWED) {
			(void)printf("%swxallowed", comma);
			flags &= ~MNT_WXALLOWED;
			comma = ",";
		}
		if (flags & MNT_LOCAL) {
			(void)printf("%slocal", comma);
			flags &= ~MNT_LOCAL;
			comma = ",";
		}
		if (flags & MNT_QUOTA) {
			(void)printf("%squota", comma);
			flags &= ~MNT_QUOTA;
			comma = ",";
		}
		if (flags & MNT_ROOTFS) {
			(void)printf("%srootfs", comma);
			flags &= ~MNT_ROOTFS;
			comma = ",";
		}
		if (flags & MNT_NOATIME) {
			(void)printf("%snoatime", comma);
			flags &= ~MNT_NOATIME;
			comma = ",";
		}
		/* filesystem control flags */
		if (flags & MNT_UPDATE) {
			(void)printf("%supdate", comma);
			flags &= ~MNT_UPDATE;
			comma = ",";
		}
		if (flags & MNT_DELEXPORT) {
			(void)printf("%sdelexport", comma);
			flags &= ~MNT_DELEXPORT;
			comma = ",";
		}
		if (flags & MNT_RELOAD) {
			(void)printf("%sreload", comma);
			flags &= ~MNT_RELOAD;
			comma = ",";
		}
		if (flags & MNT_FORCE) {
			(void)printf("%sforce", comma);
			flags &= ~MNT_FORCE;
			comma = ",";
		}
		if (flags & MNT_STALLED) {
			(void)printf("%sstalled", comma);
			flags &= ~MNT_STALLED;
			comma = ",";
		}
		if (flags & MNT_SWAPPABLE) {
			(void)printf("%sswappable", comma);
			flags &= ~MNT_SWAPPABLE;
			comma = ",";
		}
		if (flags & MNT_WANTRDWR) {
			(void)printf("%swantrdwr", comma);
			flags &= ~MNT_WANTRDWR;
			comma = ",";
		}
		if (flags & MNT_SOFTDEP) {
			(void)printf("%ssoftdep", comma);
			flags &= ~MNT_SOFTDEP;
			comma = ",";
		}
		if (flags & MNT_DOOMED) {
			(void)printf("%sdoomed", comma);
			flags &= ~MNT_DOOMED;
			comma = ",";
		}
		if (flags)
			(void)printf("%sunknown_flags:%x", comma, flags);
		(void)printf(")");
	}
	(void)printf("\n");
}

/*
 * simulate what a running kernel does in kinfo_vnode
 */
struct e_vnode *
kinfo_vnodes(void)
{
	struct mntlist kvm_mountlist;
	struct mount *mp, mount;
	struct vnode *vp, vnode;
	char *vbuf, *evbuf, *bp;
	size_t num;

	if (kd != NULL)
		KGET(V_NUMV, numvnodes);
	if (totalflag)
		return NULL;
	if ((vbuf = calloc(numvnodes + 20,
	    sizeof(struct vnode *) + sizeof(struct vnode))) == NULL)
		err(1, "malloc: vnode buffer");
	bp = vbuf;
	evbuf = vbuf + (numvnodes + 20) *
	    (sizeof(struct vnode *) + sizeof(struct vnode));
	KGET(V_MOUNTLIST, kvm_mountlist);
	num = 0;
	for (mp = TAILQ_FIRST(&kvm_mountlist); mp != NULL;
	    mp = TAILQ_NEXT(&mount, mnt_list)) {
		KGETRET(mp, &mount, sizeof(mount), "mount entry");
		for (vp = TAILQ_FIRST(&mount.mnt_vnodelist);
		    vp != NULL; vp = TAILQ_NEXT(&vnode, v_mntvnodes)) {
			KGETRET(vp, &vnode, sizeof(vnode), "vnode");
			if ((bp + sizeof(struct vnode *) +
			    sizeof(struct vnode)) > evbuf)
				/* XXX - should realloc */
				errx(1, "no more room for vnodes");
			memmove(bp, &vp, sizeof(struct vnode *));
			bp += sizeof(struct vnode *);
			memmove(bp, &vnode, sizeof(struct vnode));
			bp += sizeof(struct vnode);
			num++;
		}
	}
	numvnodes = num;
	return ((struct e_vnode *)vbuf);
}

const char hdr[] =
"   LINE RAW  CAN  OUT  HWT LWT    COL STATE      SESS  PGID DISC\n";

void
tty2itty(struct tty *tp, struct itty *itp)
{
	itp->t_dev = tp->t_dev;
	itp->t_rawq_c_cc = tp->t_rawq.c_cc;
	itp->t_canq_c_cc = tp->t_canq.c_cc;
	itp->t_outq_c_cc = tp->t_outq.c_cc;
	itp->t_hiwat = tp->t_hiwat;
	itp->t_lowat = tp->t_lowat;
	itp->t_column = tp->t_column;
	itp->t_state = tp->t_state;
	itp->t_session = tp->t_session;
	if (tp->t_pgrp != NULL)
		KGET2(&tp->t_pgrp->pg_id, &itp->t_pgrp_pg_id, sizeof(pid_t), "pgid");
	itp->t_line = tp->t_line;
}

void
ttymode(void)
{
	struct ttylist_head tty_head;
	struct tty *tp, tty;
	int i;
	struct itty itty;

	if (need_nlist)
		KGET(TTY_NTTY, ntty);
	(void)printf("%d terminal device%s\n", ntty, ntty == 1 ? "" : "s");
	(void)printf("%s", hdr);
	if (!need_nlist) {
		for (i = 0; i < ntty; i++)
			ttyprt(&globalitp[i]);
		free(globalitp);
	} else {
		KGET(TTY_TTYLIST, tty_head);
		for (tp = TAILQ_FIRST(&tty_head); tp;
		    tp = TAILQ_NEXT(&tty, tty_link)) {
			KGET2(tp, &tty, sizeof tty, "tty struct");
			tty2itty(&tty, &itty);
			ttyprt(&itty);
		}
	}
}

void
ttymodeprep(void)
{
	int mib[3];
	size_t nlen;

	if (!need_nlist) {
		mib[0] = CTL_KERN;
		mib[1] = KERN_TTYCOUNT;
		nlen = sizeof(ntty);
		if (sysctl(mib, 2, &ntty, &nlen, NULL, 0) < 0)
			err(1, "sysctl(KERN_TTYCOUNT) failed");

		mib[0] = CTL_KERN;
		mib[1] = KERN_TTY;
		mib[2] = KERN_TTY_INFO;
		if ((globalitp = reallocarray(NULL, ntty, sizeof(struct itty))) == NULL)
			err(1, "malloc");
		nlen = ntty * sizeof(struct itty);
		if (sysctl(mib, 3, globalitp, &nlen, NULL, 0) < 0)
			err(1, "sysctl(KERN_TTY_INFO) failed");
	}
}

struct {
	int flag;
	char val;
} ttystates[] = {
	{ TS_WOPEN,	'W'},
	{ TS_ISOPEN,	'O'},
	{ TS_CARR_ON,	'C'},
	{ TS_TIMEOUT,	'T'},
	{ TS_FLUSH,	'F'},
	{ TS_BUSY,	'B'},
	{ TS_ASLEEP,	'A'},
	{ TS_XCLUDE,	'X'},
	{ TS_TTSTOP,	'S'},
	{ TS_TBLOCK,	'K'},
	{ TS_ASYNC,	'Y'},
	{ TS_BKSL,	'D'},
	{ TS_ERASE,	'E'},
	{ TS_LNCH,	'L'},
	{ TS_TYPEN,	'P'},
	{ TS_CNTTB,	'N'},
	{ 0,		'\0'},
};

void
ttyprt(struct itty *tp)
{
	char *name, state[20];
	int i, j;

	if (usenumflag || (name = devname(tp->t_dev, S_IFCHR)) == NULL)
		(void)printf("%2u,%-3u   ", major(tp->t_dev), minor(tp->t_dev));
	else
		(void)printf("%7s ", name);
	(void)printf("%3d %4d ", tp->t_rawq_c_cc, tp->t_canq_c_cc);
	(void)printf("%4d %4d %3d %6d ", tp->t_outq_c_cc,
		tp->t_hiwat, tp->t_lowat, tp->t_column);
	for (i = j = 0; ttystates[i].flag; i++)
		if (tp->t_state&ttystates[i].flag)
			state[j++] = ttystates[i].val;
	if (j == 0)
		state[j++] = '-';
	state[j] = '\0';
	(void)printf("%-6s %8lx", state,
		hideroot ? 0 : (u_long)tp->t_session & 0xffffffff);
	(void)printf("%6d ", tp->t_pgrp_pg_id);
	switch (tp->t_line) {
	case TTYDISC:
		(void)printf("term\n");
		break;
	case PPPDISC:
		(void)printf("ppp\n");
		break;
	case NMEADISC:
		(void)printf("nmea\n");
		break;
	default:
		(void)printf("%d\n", tp->t_line);
		break;
	}
}

void
filemode(void)
{
	char flagbuf[16], *fbp;
	static char *dtypes[] = { "???", "inode", "socket", "pipe", "kqueue", "???", "???" };

	globalnl = vnodenl;

	if (nlistf != NULL || memf != NULL) {
		KGET(FNL_MAXFILE, maxfile);
		if (totalflag) {
			KGET(FNL_NFILE, nfile);
			(void)printf("%3d/%3d files\n", nfile, maxfile);
			return;
		}
	}

	(void)printf("%d/%d open files\n", nfile, maxfile);
	if (totalflag)
		return;

	(void)printf("%*s TYPE       FLG  CNT  MSG  %*s  OFFSET\n",
	    2 * (int)sizeof(long), "LOC", 2 * (int)sizeof(long), "DATA");
	for (; nfile-- > 0; kf++) {
		(void)printf("%0*llx ", 2 * (int)sizeof(long),
		    hideroot ? 0LL : kf->f_fileaddr);
		(void)printf("%-8.8s", dtypes[
		    (kf->f_type >= (sizeof(dtypes)/sizeof(dtypes[0])))
		    ? 0 : kf->f_type]);
		fbp = flagbuf;
		if (kf->f_flag & FREAD)
			*fbp++ = 'R';
		if (kf->f_flag & FWRITE)
			*fbp++ = 'W';
		if (kf->f_flag & FAPPEND)
			*fbp++ = 'A';
		if (kf->f_flag & FASYNC)
			*fbp++ = 'I';

		if (kf->f_iflags & FIF_HASLOCK)
			*fbp++ = 'L';

		*fbp = '\0';
		(void)printf("%6s  %3ld", flagbuf, (long)kf->f_count);
		(void)printf("  %3ld", (long)kf->f_msgcount);
		(void)printf("  %0*lx", 2 * (int)sizeof(long),
		    hideroot ? 0L : (long)kf->f_data);

		if (kf->f_offset == (uint64_t)-1)
			(void)printf("  *\n");
		else if (kf->f_offset > INT64_MAX) {
			/* would have been negative */
			(void)printf("  %llx\n",
			    hideroot ? 0LL : (long long)kf->f_offset);
		} else
			(void)printf("  %lld\n",
			    hideroot ? 0LL : (long long)kf->f_offset);
	}
}

void
filemodeprep(void)
{
	int mib[2];
	size_t len;

	if (nlistf == NULL && memf == NULL) {
		mib[0] = CTL_KERN;
		mib[1] = KERN_MAXFILES;
		len = sizeof(maxfile);
		if (sysctl(mib, 2, &maxfile, &len, NULL, 0) < 0)
			err(1, "sysctl(KERN_MAXFILES) failed");
		if (totalflag) {
			mib[0] = CTL_KERN;
			mib[1] = KERN_NFILES;
			len = sizeof(nfile);
			if (sysctl(mib, 2, &nfile, &len, NULL, 0) < 0)
				err(1, "sysctl(KERN_NFILES) failed");
		}
	}

	if (!totalflag) {
		kf = kvm_getfiles(kd, KERN_FILE_BYFILE, 0, sizeof *kf, &nfile);
		if (kf == NULL) {
			warnx("kvm_getfiles: %s", kvm_geterr(kd));
			return;
		}
	}
}

/*
 * swapmode is based on a program called swapinfo written
 * by Kevin Lahey <kml@rokkaku.atl.ga.us>.
 */
void
swapmode(void)
{
	char *header;
	int hlen = 10, nswap;
	int bdiv, i, avail, nfree, npfree, used;
	long blocksize;
	struct swapent *swdev;

	if (kflag) {
		header = "1K-blocks";
		blocksize = 1024;
		hlen = strlen(header);
	} else
		header = getbsize(&hlen, &blocksize);

	nswap = swapctl(SWAP_NSWAP, 0, 0);
	if (nswap == 0) {
		if (!totalflag)
			(void)printf("%-11s %*s %8s %8s %8s  %s\n",
			    "Device", hlen, header,
			    "Used", "Avail", "Capacity", "Priority");
		(void)printf("%-11s %*d %8d %8d %5.0f%%\n",
		    "Total", hlen, 0, 0, 0, 0.0);
		return;
	}
	if ((swdev = calloc(nswap, sizeof(*swdev))) == NULL)
		err(1, "malloc");
	if (swapctl(SWAP_STATS, swdev, nswap) == -1)
		err(1, "swapctl");

	if (!totalflag)
		(void)printf("%-11s %*s %8s %8s %8s  %s\n",
		    "Device", hlen, header,
		    "Used", "Avail", "Capacity", "Priority");

	/* Run through swap list, doing the funky monkey. */
	bdiv = blocksize / DEV_BSIZE;
	avail = nfree = npfree = 0;
	for (i = 0; i < nswap; i++) {
		int xsize, xfree;

		if (!(swdev[i].se_flags & SWF_ENABLE))
			continue;

		if (!totalflag) {
			if (usenumflag)
				(void)printf("%2u,%-2u       %*d ",
				    major(swdev[i].se_dev),
				    minor(swdev[i].se_dev),
				    hlen, swdev[i].se_nblks / bdiv);
			else
				(void)printf("%-11s %*d ", swdev[i].se_path,
				    hlen, swdev[i].se_nblks / bdiv);
		}

		xsize = swdev[i].se_nblks;
		used = swdev[i].se_inuse;
		xfree = xsize - used;
		nfree += (xsize - used);
		npfree++;
		avail += xsize;
		if (totalflag)
			continue;
		(void)printf("%8d %8d %5.0f%%    %d\n",
		    used / bdiv, xfree / bdiv,
		    (double)used / (double)xsize * 100.0,
		    swdev[i].se_priority);
	}
	free(swdev);

	/*
	 * If only one partition has been set up via swapon(8), we don't
	 * need to bother with totals.
	 */
	used = avail - nfree;
	if (totalflag) {
		(void)printf("%dM/%dM swap space\n",
		    used / (1048576 / DEV_BSIZE),
		    avail / (1048576 / DEV_BSIZE));
		return;
	}
	if (npfree > 1) {
		(void)printf("%-11s %*d %8d %8d %5.0f%%\n",
		    "Total", hlen, avail / bdiv, used / bdiv, nfree / bdiv,
		    (double)used / (double)avail * 100.0);
	}
}

static void __dead
usage(void)
{
	(void)fprintf(stderr, "usage: "
	    "pstat [-fknsTtv] [-M core] [-N system] [-d format symbol ...]\n");
	exit(1);
}
