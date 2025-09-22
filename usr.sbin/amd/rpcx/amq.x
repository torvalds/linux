/*
 * Copyright (c) 1990 Jan-Simon Pendry
 * Copyright (c) 1990 Imperial College of Science, Technology & Medicine
 * Copyright (c) 1990, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * Jan-Simon Pendry at Imperial College, London.
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
 *	from: @(#)amq.x	8.1 (Berkeley) 6/6/93
 *	$Id: amq.x,v 1.7 2022/12/28 21:30:15 jmc Exp $
 *
 */

/*
 * Protocol description used by the amq program
 */

const AMQ_STRLEN = 1024;	/* Maximum length of a pathname */

/*
 * The type dirpath is the pathname of a directory
 */
typedef string amq_string<AMQ_STRLEN>;

/*
 * The type time_type should correspond to the system time_t
 */
typedef int64_t time_type;

/*
 * A tree of what is mounted
 */
struct amq_mount_tree {
	amq_string	mt_mountinfo;	/* Mounted filesystem */
	amq_string	mt_directory;	/* Virtual mount */
	amq_string	mt_mountpoint;	/* Mount point */
	amq_string	mt_type;	/* Filesystem type */
	time_type	mt_mounttime;	/* Mount time */
	u_short		mt_mountuid;	/* Mounter */
	int		mt_getattr;	/* Count of getattrs */
	int		mt_lookup;	/* Count of lookups */
	int		mt_readdir;	/* Count of readdirs */
	int		mt_readlink;	/* Count of readlinks */
	int		mt_statfs;	/* Count of statfss */
	amq_mount_tree	*mt_next;	/* Sibling mount tree */
	amq_mount_tree	*mt_child;	/* Child mount tree */
};
typedef amq_mount_tree *amq_mount_tree_p;

/*
 * List of mounted filesystems
 */
struct amq_mount_info {
	amq_string	mi_type;	/* Type of mount */
	amq_string	mi_mountpt;	/* Mount point */
	amq_string	mi_mountinfo;	/* Mount info */
	amq_string	mi_fserver;	/* Fileserver */
	int		mi_error;	/* Error code */
	int		mi_refc;	/* References */
	int		mi_up;		/* Filesystem available */
};
typedef amq_mount_info amq_mount_info_list<>;

/*
 * A list of mount trees
 */
typedef amq_mount_tree_p amq_mount_tree_list<>;

/*
 * System wide stats
 */
struct amq_mount_stats {
	int	as_drops;	/* Dropped requests */
	int	as_stale;	/* Stale NFS handles */
	int	as_mok;		/* Successful mounts */
	int	as_merr;	/* Failed mounts */
	int	as_uerr;	/* Failed unmounts */
};

enum amq_opt {
	AMOPT_DEBUG=0,
	AMOPT_LOGFILE=1,
	AMOPT_XLOG=2,
	AMOPT_FLUSHMAPC=3
};

struct amq_setopt {
	amq_opt	as_opt;		/* Option */
	amq_string as_str;	/* String */
};

program AMQ_PROGRAM {
	version AMQ_VERSION {
		/*
		 * Does no work. It is made available in all RPC services
		 * to allow server response testing and timing
		 */
		void
		AMQPROC_NULL(void) = 0;

		/*
		 * Returned the mount tree descending from
		 * the given directory.  The directory must
		 * be a top-level mount point of the automounter.
		 */
		amq_mount_tree_p
		AMQPROC_MNTTREE(amq_string) = 1;

		/*
		 * Force a timeout unmount on the specified directory.
		 */
		void
		AMQPROC_UMNT(amq_string) = 2;

		/*
		 * Obtain system wide statistics from the automounter
		 */
		amq_mount_stats
		AMQPROC_STATS(void) = 3;

		/*
		 * Obtain full tree
		 */
		amq_mount_tree_list
		AMQPROC_EXPORT(void) = 4;

		/*
		 * Control debug options.
		 * Return status:
		 *	-1: debug not available
		 *	 0: everything wonderful
		 *	>0: number of options not recognised
		 */
		int
		AMQPROC_SETOPT(amq_setopt) = 5;

		/*
		 * List of mounted filesystems
		 */
		amq_mount_info_list
		AMQPROC_GETMNTFS(void) = 6;

		/*
		 * Get version info
		 */
		amq_string
		AMQPROC_GETVERS(void) = 7;
	} = 57;
} = 300019;	/* Allocated by Sun, 89/8/29 */
