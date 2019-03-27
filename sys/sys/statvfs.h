/*-
 * Copyright 2002 Massachusetts Institute of Technology
 *
 * Permission to use, copy, modify, and distribute this software and
 * its documentation for any purpose and without fee is hereby
 * granted, provided that both the above copyright notice and this
 * permission notice appear in all copies, that both the above
 * copyright notice and this permission notice appear in all
 * supporting documentation, and that the name of M.I.T. not be used
 * in advertising or publicity pertaining to distribution of the
 * software without specific, written prior permission.  M.I.T. makes
 * no representations about the suitability of this software for any
 * purpose.  It is provided "as is" without express or implied
 * warranty.
 * 
 * THIS SOFTWARE IS PROVIDED BY M.I.T. ``AS IS''.  M.I.T. DISCLAIMS
 * ALL EXPRESS OR IMPLIED WARRANTIES WITH REGARD TO THIS SOFTWARE,
 * INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE. IN NO EVENT
 * SHALL M.I.T. BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF
 * USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT
 * OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $FreeBSD$
 */

#ifndef _SYS_STATVFS_H_
#define	_SYS_STATVFS_H_

#include <sys/cdefs.h>
#include <sys/_types.h>

/*
 * POSIX says we must define the fsblkcnt_t and fsfilcnt_t types here.
 * Note that these must be unsigned integer types, so we have to be
 * careful in converting the signed statfs members to the unsigned
 * statvfs members.  (Well, actually, we don't -- see below -- but
 * a quality implementation should.)
 */
#ifndef _FSBLKCNT_T_DECLARED		/* always declared together */
typedef	__fsblkcnt_t	fsblkcnt_t;
typedef	__fsfilcnt_t	fsfilcnt_t;
#define _FSBLKCNT_T_DECLARED
#endif

/*
 * The difference between `avail' and `free' is that `avail' represents
 * space available to unprivileged processes, whereas `free' includes all
 * unallocated space, including that reserved for privileged processes.
 * Or at least, that's the most useful interpretation.  (According to
 * the letter of the standard, this entire interface is completely
 * unspecified!)
 */
struct statvfs {
	fsblkcnt_t	f_bavail;	/* Number of blocks */
	fsblkcnt_t	f_bfree;
	fsblkcnt_t	f_blocks;
	fsfilcnt_t	f_favail;	/* Number of files (e.g., inodes) */
	fsfilcnt_t	f_ffree;
	fsfilcnt_t	f_files;
	unsigned long	f_bsize;	/* Size of blocks counted above */
	unsigned long	f_flag;
	unsigned long	f_frsize;	/* Size of fragments */
	unsigned long	f_fsid;		/* Not meaningful */
	unsigned long	f_namemax;	/* Same as pathconf(_PC_NAME_MAX) */
};

/* flag bits for f_flag: */
#define	ST_RDONLY	0x1
#define	ST_NOSUID	0x2

__BEGIN_DECLS
int	fstatvfs(int, struct statvfs *);
int	statvfs(const char *__restrict, struct statvfs *__restrict);
__END_DECLS
#endif /* _SYS_STATVFS_H_ */
