/* 
   You may distribute this file under either of the two licenses that
   follow at your discretion.
*/

/* BLURB lgpl

                           Coda File System
                              Release 5

          Copyright (c) 1987-1999 Carnegie Mellon University
                  Additional copyrights listed below

This code is distributed "AS IS" without warranty of any kind under
the terms of the GNU Library General Public Licence Version 2, as
shown in the file LICENSE, or under the license shown below. The
technical and financial contributors to Coda are listed in the file
CREDITS.

                        Additional copyrights 
*/

/*

            Coda: an Experimental Distributed File System
                             Release 4.0

          Copyright (c) 1987-1999 Carnegie Mellon University
                         All Rights Reserved

Permission  to  use, copy, modify and distribute this software and its
documentation is hereby granted,  provided  that  both  the  copyright
notice  and  this  permission  notice  appear  in  all  copies  of the
software, derivative works or  modified  versions,  and  any  portions
thereof, and that both notices appear in supporting documentation, and
that credit is given to Carnegie Mellon University  in  all  documents
and publicity pertaining to direct or indirect use of this code or its
derivatives.

CODA IS AN EXPERIMENTAL SOFTWARE SYSTEM AND IS  KNOWN  TO  HAVE  BUGS,
SOME  OF  WHICH MAY HAVE SERIOUS CONSEQUENCES.  CARNEGIE MELLON ALLOWS
FREE USE OF THIS SOFTWARE IN ITS "AS IS" CONDITION.   CARNEGIE  MELLON
DISCLAIMS  ANY  LIABILITY  OF  ANY  KIND  FOR  ANY  DAMAGES WHATSOEVER
RESULTING DIRECTLY OR INDIRECTLY FROM THE USE OF THIS SOFTWARE  OR  OF
ANY DERIVATIVE WORK.

Carnegie  Mellon  encourages  users  of  this  software  to return any
improvements or extensions that  they  make,  and  to  grant  Carnegie
Mellon the rights to redistribute these changes without encumbrance.
*/

/*
 *
 * Based on cfs.h from Mach, but revamped for increased simplicity.
 * Linux modifications by 
 * Peter Braam, Aug 1996
 */

#ifndef _CODA_HEADER_
#define _CODA_HEADER_


/* Catch new _KERNEL defn for NetBSD and DJGPP/__CYGWIN32__ */
#if defined(__NetBSD__) || \
  ((defined(DJGPP) || defined(__CYGWIN32__)) && !defined(KERNEL))
#include <sys/types.h>
#endif 

#ifndef CODA_MAXSYMLINKS
#define CODA_MAXSYMLINKS 10
#endif

#if defined(DJGPP) || defined(__CYGWIN32__)
#ifdef KERNEL
typedef unsigned long u_long;
typedef unsigned int u_int;
typedef unsigned short u_short;
typedef u_long ino_t;
typedef u_long dev_t;
typedef void * caddr_t;
#ifdef DOS
typedef unsigned __int64 u_quad_t;
#else 
typedef unsigned long long u_quad_t;
#endif

#define inline

struct timespec {
        long       ts_sec;
        long       ts_nsec;
};
#else  /* DJGPP but not KERNEL */
#include <sys/time.h>
typedef unsigned long long u_quad_t;
#endif /* !KERNEL */
#endif /* !DJGPP */


#if defined(__linux__)
#include <linux/time.h>
#define cdev_t u_quad_t
#ifndef __KERNEL__
#if !defined(_UQUAD_T_) && (!defined(__GLIBC__) || __GLIBC__ < 2)
#define _UQUAD_T_ 1
typedef unsigned long long u_quad_t;
#endif
#else /*__KERNEL__ */
typedef unsigned long long u_quad_t;
#endif /* __KERNEL__ */
#else
#define cdev_t dev_t
#endif

#ifdef __CYGWIN32__
struct timespec {
        time_t  tv_sec;         /* seconds */
        long    tv_nsec;        /* nanoseconds */
};
#endif

#ifndef __BIT_TYPES_DEFINED__
#define __BIT_TYPES_DEFINED__
typedef signed char	      int8_t;
typedef unsigned char	    u_int8_t;
typedef short		     int16_t;
typedef unsigned short	   u_int16_t;
typedef int		     int32_t;
typedef unsigned int	   u_int32_t;
#endif


/*
 * Cfs constants
 */
#define CODA_MAXNAMLEN   255
#define CODA_MAXPATHLEN  1024
#define CODA_MAXSYMLINK  10

/* these are Coda's version of O_RDONLY etc combinations
 * to deal with VFS open modes
 */
#define	C_O_READ	0x001
#define	C_O_WRITE       0x002
#define C_O_TRUNC       0x010
#define C_O_EXCL	0x100
#define C_O_CREAT	0x200

/* these are to find mode bits in Venus */ 
#define C_M_READ  00400
#define C_M_WRITE 00200

/* for access Venus will use */
#define C_A_C_OK    8               /* Test for writing upon create.  */
#define C_A_R_OK    4               /* Test for read permission.  */
#define C_A_W_OK    2               /* Test for write permission.  */
#define C_A_X_OK    1               /* Test for execute permission.  */
#define C_A_F_OK    0               /* Test for existence.  */



#ifndef _VENUS_DIRENT_T_
#define _VENUS_DIRENT_T_ 1
struct venus_dirent {
        u_int32_t d_fileno;		/* file number of entry */
        u_int16_t d_reclen;		/* length of this record */
        u_int8_t  d_type;			/* file type, see below */
        u_int8_t  d_namlen;		/* length of string in d_name */
        char	  d_name[CODA_MAXNAMLEN + 1];/* name must be no longer than this */
};
#undef DIRSIZ
#define DIRSIZ(dp)      ((sizeof (struct venus_dirent) - (CODA_MAXNAMLEN+1)) + \
                         (((dp)->d_namlen+1 + 3) &~ 3))

/*
 * File types
 */
#define	CDT_UNKNOWN	 0
#define	CDT_FIFO	 1
#define	CDT_CHR		 2
#define	CDT_DIR		 4
#define	CDT_BLK		 6
#define	CDT_REG		 8
#define	CDT_LNK		10
#define	CDT_SOCK	12
#define	CDT_WHT		14

/*
 * Convert between stat structure types and directory types.
 */
#define	IFTOCDT(mode)	(((mode) & 0170000) >> 12)
#define	CDTTOIF(dirtype)	((dirtype) << 12)

#endif

#ifndef _VUID_T_
#define _VUID_T_
typedef u_int32_t vuid_t;
typedef u_int32_t vgid_t;
#endif /*_VUID_T_ */

struct CodaFid {
	u_int32_t opaque[4];
};

#define coda_f2i(fid)\
	(fid ? (fid->opaque[3] ^ (fid->opaque[2]<<10) ^ (fid->opaque[1]<<20) ^ fid->opaque[0]) : 0)

#ifndef _VENUS_VATTR_T_
#define _VENUS_VATTR_T_
/*
 * Vnode types.  VNON means no type.
 */
enum coda_vtype	{ C_VNON, C_VREG, C_VDIR, C_VBLK, C_VCHR, C_VLNK, C_VSOCK, C_VFIFO, C_VBAD };

struct coda_vattr {
	long     	va_type;	/* vnode type (for create) */
	u_short		va_mode;	/* files access mode and type */
	short		va_nlink;	/* number of references to file */
	vuid_t		va_uid;		/* owner user id */
	vgid_t		va_gid;		/* owner group id */
	long		va_fileid;	/* file id */
	u_quad_t	va_size;	/* file size in bytes */
	long		va_blocksize;	/* blocksize preferred for i/o */
	struct timespec	va_atime;	/* time of last access */
	struct timespec	va_mtime;	/* time of last modification */
	struct timespec	va_ctime;	/* time file changed */
	u_long		va_gen;		/* generation number of file */
	u_long		va_flags;	/* flags defined for file */
	cdev_t	        va_rdev;	/* device special file represents */
	u_quad_t	va_bytes;	/* bytes of disk space held by file */
	u_quad_t	va_filerev;	/* file modification number */
};

#endif 

/* structure used by CODA_STATFS for getting cache information from venus */
struct coda_statfs {
    int32_t f_blocks;
    int32_t f_bfree;
    int32_t f_bavail;
    int32_t f_files;
    int32_t f_ffree;
};

/*
 * Kernel <--> Venus communications.
 */

#define CODA_ROOT	2
#define CODA_OPEN_BY_FD	3
#define CODA_OPEN	4
#define CODA_CLOSE	5
#define CODA_IOCTL	6
#define CODA_GETATTR	7
#define CODA_SETATTR	8
#define CODA_ACCESS	9
#define CODA_LOOKUP	10
#define CODA_CREATE	11
#define CODA_REMOVE	12
#define CODA_LINK	13
#define CODA_RENAME	14
#define CODA_MKDIR	15
#define CODA_RMDIR	16
#define CODA_SYMLINK	18
#define CODA_READLINK	19
#define CODA_FSYNC	20
#define CODA_VGET	22
#define CODA_SIGNAL	23
#define CODA_REPLACE	 24 /* DOWNCALL */
#define CODA_FLUSH       25 /* DOWNCALL */
#define CODA_PURGEUSER   26 /* DOWNCALL */
#define CODA_ZAPFILE     27 /* DOWNCALL */
#define CODA_ZAPDIR      28 /* DOWNCALL */
#define CODA_PURGEFID    30 /* DOWNCALL */
#define CODA_OPEN_BY_PATH 31
#define CODA_RESOLVE     32
#define CODA_REINTEGRATE 33
#define CODA_STATFS	 34
#define CODA_STORE	 35
#define CODA_RELEASE	 36
#define CODA_NCALLS 37

#define DOWNCALL(opcode) (opcode >= CODA_REPLACE && opcode <= CODA_PURGEFID)

#define VC_MAXDATASIZE	    8192
#define VC_MAXMSGSIZE      sizeof(union inputArgs)+sizeof(union outputArgs) +\
                            VC_MAXDATASIZE  

#define CIOC_KERNEL_VERSION _IOWR('c', 10, size_t)

#define CODA_KERNEL_VERSION 3 /* 128-bit file identifiers */

/*
 *        Venus <-> Coda  RPC arguments
 */
struct coda_in_hdr {
    u_int32_t opcode;
    u_int32_t unique;	    /* Keep multiple outstanding msgs distinct */
    pid_t pid;
    pid_t pgid;
    vuid_t uid;
};

/* Really important that opcode and unique are 1st two fields! */
struct coda_out_hdr {
    u_int32_t opcode;
    u_int32_t unique;	
    u_int32_t result;
};

/* coda_root: NO_IN */
struct coda_root_out {
    struct coda_out_hdr oh;
    struct CodaFid VFid;
};

struct coda_root_in {
    struct coda_in_hdr in;
};

/* coda_open: */
struct coda_open_in {
    struct coda_in_hdr ih;
    struct CodaFid VFid;
    int	flags;
};

struct coda_open_out {
    struct coda_out_hdr oh;
    cdev_t	dev;
    ino_t	inode;
};


/* coda_store: */
struct coda_store_in {
    struct coda_in_hdr ih;
    struct CodaFid VFid;
    int	flags;
};

struct coda_store_out {
    struct coda_out_hdr out;
};

/* coda_release: */
struct coda_release_in {
    struct coda_in_hdr ih;
    struct CodaFid VFid;
    int	flags;
};

struct coda_release_out {
    struct coda_out_hdr out;
};

/* coda_close: */
struct coda_close_in {
    struct coda_in_hdr ih;
    struct CodaFid VFid;
    int	flags;
};

struct coda_close_out {
    struct coda_out_hdr out;
};

/* coda_ioctl: */
struct coda_ioctl_in {
    struct coda_in_hdr ih;
    struct CodaFid VFid;
    int	cmd;
    int	len;
    int	rwflag;
    char *data;			/* Place holder for data. */
};

struct coda_ioctl_out {
    struct coda_out_hdr oh;
    int	len;
    caddr_t	data;		/* Place holder for data. */
};


/* coda_getattr: */
struct coda_getattr_in {
    struct coda_in_hdr ih;
    struct CodaFid VFid;
};

struct coda_getattr_out {
    struct coda_out_hdr oh;
    struct coda_vattr attr;
};


/* coda_setattr: NO_OUT */
struct coda_setattr_in {
    struct coda_in_hdr ih;
    struct CodaFid VFid;
    struct coda_vattr attr;
};

struct coda_setattr_out {
    struct coda_out_hdr out;
};

/* coda_access: NO_OUT */
struct coda_access_in {
    struct coda_in_hdr ih;
    struct CodaFid VFid;
    int	flags;
};

struct coda_access_out {
    struct coda_out_hdr out;
};


/* lookup flags */
#define CLU_CASE_SENSITIVE     0x01
#define CLU_CASE_INSENSITIVE   0x02

/* coda_lookup: */
struct  coda_lookup_in {
    struct coda_in_hdr ih;
    struct CodaFid VFid;
    int         name;		/* Place holder for data. */
    int         flags;	
};

struct coda_lookup_out {
    struct coda_out_hdr oh;
    struct CodaFid VFid;
    int	vtype;
};


/* coda_create: */
struct coda_create_in {
    struct coda_in_hdr ih;
    struct CodaFid VFid;
    struct coda_vattr attr;
    int excl;
    int mode;
    int 	name;		/* Place holder for data. */
};

struct coda_create_out {
    struct coda_out_hdr oh;
    struct CodaFid VFid;
    struct coda_vattr attr;
};


/* coda_remove: NO_OUT */
struct coda_remove_in {
    struct coda_in_hdr ih;
    struct CodaFid VFid;
    int name;		/* Place holder for data. */
};

struct coda_remove_out {
    struct coda_out_hdr out;
};

/* coda_link: NO_OUT */
struct coda_link_in {
    struct coda_in_hdr ih;
    struct CodaFid sourceFid;	/* cnode to link *to* */
    struct CodaFid destFid;	/* Directory in which to place link */
    int tname;		/* Place holder for data. */
};

struct coda_link_out {
    struct coda_out_hdr out;
};


/* coda_rename: NO_OUT */
struct coda_rename_in {
    struct coda_in_hdr ih;
    struct CodaFid sourceFid;
    int 	srcname;
    struct CodaFid destFid;
    int 	destname;
};

struct coda_rename_out {
    struct coda_out_hdr out;
};

/* coda_mkdir: */
struct coda_mkdir_in {
    struct coda_in_hdr ih;
    struct CodaFid VFid;
    struct coda_vattr attr;
    int	   name;		/* Place holder for data. */
};

struct coda_mkdir_out {
    struct coda_out_hdr oh;
    struct CodaFid VFid;
    struct coda_vattr attr;
};


/* coda_rmdir: NO_OUT */
struct coda_rmdir_in {
    struct coda_in_hdr ih;
    struct CodaFid VFid;
    int name;		/* Place holder for data. */
};

struct coda_rmdir_out {
    struct coda_out_hdr out;
};

/* coda_symlink: NO_OUT */
struct coda_symlink_in {
    struct coda_in_hdr ih;
    struct CodaFid VFid;	/* Directory to put symlink in */
    int srcname;
    struct coda_vattr attr;
    int tname;
};

struct coda_symlink_out {
    struct coda_out_hdr out;
};

/* coda_readlink: */
struct coda_readlink_in {
    struct coda_in_hdr ih;
    struct CodaFid VFid;
};

struct coda_readlink_out {
    struct coda_out_hdr oh;
    int	count;
    caddr_t	data;		/* Place holder for data. */
};


/* coda_fsync: NO_OUT */
struct coda_fsync_in {
    struct coda_in_hdr ih;
    struct CodaFid VFid;
};

struct coda_fsync_out {
    struct coda_out_hdr out;
};

/* coda_vget: */
struct coda_vget_in {
    struct coda_in_hdr ih;
    struct CodaFid VFid;
};

struct coda_vget_out {
    struct coda_out_hdr oh;
    struct CodaFid VFid;
    int	vtype;
};


/* CODA_SIGNAL is out-of-band, doesn't need data. */
/* CODA_INVALIDATE is a venus->kernel call */
/* CODA_FLUSH is a venus->kernel call */

/* coda_purgeuser: */
/* CODA_PURGEUSER is a venus->kernel call */
struct coda_purgeuser_out {
    struct coda_out_hdr oh;
    vuid_t uid;
};

/* coda_zapfile: */
/* CODA_ZAPFILE is a venus->kernel call */
struct coda_zapfile_out {  
    struct coda_out_hdr oh;
    struct CodaFid CodaFid;
};

/* coda_zapdir: */
/* CODA_ZAPDIR is a venus->kernel call */	
struct coda_zapdir_out {	  
    struct coda_out_hdr oh;
    struct CodaFid CodaFid;
};

/* coda_purgefid: */
/* CODA_PURGEFID is a venus->kernel call */	
struct coda_purgefid_out { 
    struct coda_out_hdr oh;
    struct CodaFid CodaFid;
};

/* coda_replace: */
/* CODA_REPLACE is a venus->kernel call */	
struct coda_replace_out { /* coda_replace is a venus->kernel call */
    struct coda_out_hdr oh;
    struct CodaFid NewFid;
    struct CodaFid OldFid;
};

/* coda_open_by_fd: */
struct coda_open_by_fd_in {
    struct coda_in_hdr ih;
    struct CodaFid VFid;
    int        flags;
};

struct coda_open_by_fd_out {
    struct coda_out_hdr oh;
    int fd;

#ifdef __KERNEL__
    struct file *fh; /* not passed from userspace but used in-kernel only */
#endif
};

/* coda_open_by_path: */
struct coda_open_by_path_in {
    struct coda_in_hdr ih;
    struct CodaFid VFid;
    int	flags;
};

struct coda_open_by_path_out {
    struct coda_out_hdr oh;
	int path;
};

/* coda_statfs: NO_IN */
struct coda_statfs_in {
    struct coda_in_hdr in;
};

struct coda_statfs_out {
    struct coda_out_hdr oh;
    struct coda_statfs stat;
};

/* 
 * Occasionally, we don't cache the fid returned by CODA_LOOKUP. 
 * For instance, if the fid is inconsistent. 
 * This case is handled by setting the top bit of the type result parameter.
 */
#define CODA_NOCACHE          0x80000000

union inputArgs {
    struct coda_in_hdr ih;		/* NB: every struct below begins with an ih */
    struct coda_open_in coda_open;
    struct coda_store_in coda_store;
    struct coda_release_in coda_release;
    struct coda_close_in coda_close;
    struct coda_ioctl_in coda_ioctl;
    struct coda_getattr_in coda_getattr;
    struct coda_setattr_in coda_setattr;
    struct coda_access_in coda_access;
    struct coda_lookup_in coda_lookup;
    struct coda_create_in coda_create;
    struct coda_remove_in coda_remove;
    struct coda_link_in coda_link;
    struct coda_rename_in coda_rename;
    struct coda_mkdir_in coda_mkdir;
    struct coda_rmdir_in coda_rmdir;
    struct coda_symlink_in coda_symlink;
    struct coda_readlink_in coda_readlink;
    struct coda_fsync_in coda_fsync;
    struct coda_vget_in coda_vget;
    struct coda_open_by_fd_in coda_open_by_fd;
    struct coda_open_by_path_in coda_open_by_path;
    struct coda_statfs_in coda_statfs;
};

union outputArgs {
    struct coda_out_hdr oh;		/* NB: every struct below begins with an oh */
    struct coda_root_out coda_root;
    struct coda_open_out coda_open;
    struct coda_ioctl_out coda_ioctl;
    struct coda_getattr_out coda_getattr;
    struct coda_lookup_out coda_lookup;
    struct coda_create_out coda_create;
    struct coda_mkdir_out coda_mkdir;
    struct coda_readlink_out coda_readlink;
    struct coda_vget_out coda_vget;
    struct coda_purgeuser_out coda_purgeuser;
    struct coda_zapfile_out coda_zapfile;
    struct coda_zapdir_out coda_zapdir;
    struct coda_purgefid_out coda_purgefid;
    struct coda_replace_out coda_replace;
    struct coda_open_by_fd_out coda_open_by_fd;
    struct coda_open_by_path_out coda_open_by_path;
    struct coda_statfs_out coda_statfs;
};    

union coda_downcalls {
    /* CODA_INVALIDATE is a venus->kernel call */
    /* CODA_FLUSH is a venus->kernel call */
    struct coda_purgeuser_out purgeuser;
    struct coda_zapfile_out zapfile;
    struct coda_zapdir_out zapdir;
    struct coda_purgefid_out purgefid;
    struct coda_replace_out replace;
};


/*
 * Used for identifying usage of "Control" and pioctls
 */

#define PIOCPARM_MASK 0x0000ffff
struct ViceIoctl {
        void __user *in;        /* Data to be transferred in */
        void __user *out;       /* Data to be transferred out */
        u_short in_size;        /* Size of input buffer <= 2K */
        u_short out_size;       /* Maximum size of output buffer, <= 2K */
};

struct PioctlData {
        const char __user *path;
        int follow;
        struct ViceIoctl vi;
};

#define CODA_CONTROL		".CONTROL"
#define CODA_CONTROLLEN		8
#define CTL_INO			-1

/* Data passed to mount */

#define CODA_MOUNT_VERSION 1

struct coda_mount_data {
	int		version;
	int		fd;       /* Opened device */
};

#endif 

