/*	$OpenBSD: mount.x,v 1.6 2022/12/27 17:10:07 jmc Exp $	*/

/*
 * Copyright (c) 2010, Oracle America, Inc.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above
 *       copyright notice, this list of conditions and the following
 *       disclaimer in the documentation and/or other materials
 *       provided with the distribution.
 *     * Neither the name of the "Oracle America, Inc." nor the names of its
 *       contributors may be used to endorse or promote products derived
 *       from this software without specific prior written permission.
 *
 *   THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 *   "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 *   LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 *   FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
 *   COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT,
 *   INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 *   DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE
 *   GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 *   INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 *   WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
 *   NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 *   OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * Protocol description for the mount program
 */

#ifndef RPC_HDR
#endif

const MNTPATHLEN = 1024;	/* maximum bytes in a pathname argument */
const MNTNAMLEN = 255;		/* maximum bytes in a name argument */
const FHSIZE = 32;		/* size in bytes of a file handle */

/*
 * The fhandle is the file handle that the server passes to the client.
 * All file operations are done using the file handles to refer to a file
 * or a directory. The file handle can contain whatever information the
 * server needs to distinguish an individual file.
 */
typedef opaque fhandle[FHSIZE];

/*
 * If a status of zero is returned, the call completed successfully, and
 * a file handle for the directory follows. A non-zero status indicates
 * some sort of error. The status corresponds with UNIX error numbers.
 */
union fhstatus switch (unsigned fhs_status) {
case 0:
	fhandle fhs_fhandle;
default:
	void;
};

/*
 * The type dirpath is the pathname of a directory
 */
typedef string dirpath<MNTPATHLEN>;

/*
 * The type name is used for arbitrary names (hostnames, groupnames)
 */
typedef string name<MNTNAMLEN>;

/*
 * A list of who has what mounted
 */
typedef struct mountbody *mountlist;
struct mountbody {
	name ml_hostname;
	dirpath ml_directory;
	mountlist ml_next;
};

/*
 * A list of netgroups
 */
typedef struct groupnode *groups;
struct groupnode {
	name gr_name;
	groups gr_next;
};

/*
 * A list of what is exported and to whom
 */
typedef struct exportnode *exports;
struct exportnode {
	dirpath ex_dir;
	groups ex_groups;
	exports ex_next;
};

program MOUNTPROG {
	/*
	 * Version one of the mount protocol communicates with version two
	 * of the NFS protocol. The only connecting point is the fhandle
	 * structure, which is the same for both protocols.
	 */
	version MOUNTVERS {
		/*
		 * Does no work. It is made available in all RPC services
		 * to allow server response testing and timing
		 */
		void
		MOUNTPROC_NULL(void) = 0;

		/*
		 * If fhs_status is 0, then fhs_fhandle contains the
		 * file handle for the directory. This file handle may
		 * be used in the NFS protocol. This procedure also adds
		 * a new entry to the mount list for this client mounting
		 * the directory.
		 * Unix authentication required.
		 */
		fhstatus
		MOUNTPROC_MNT(dirpath) = 1;

		/*
		 * Returns the list of remotely mounted filesystems. The
		 * mountlist contains one entry for each hostname and
		 * directory pair.
		 */
		mountlist
		MOUNTPROC_DUMP(void) = 2;

		/*
		 * Removes the mount list entry for the directory
		 * Unix authentication required.
		 */
		void
		MOUNTPROC_UMNT(dirpath) = 3;

		/*
		 * Removes all of the mount list entries for this client
		 * Unix authentication required.
		 */
		void
		MOUNTPROC_UMNTALL(void) = 4;

		/*
		 * Returns a list of all the exported filesystems, and which
		 * machines are allowed to import it.
		 */
		exports
		MOUNTPROC_EXPORT(void)  = 5;

		/*
		 * Identical to MOUNTPROC_EXPORT above
		 */
		exports
		MOUNTPROC_EXPORTALL(void) = 6;
	} = 1;
} = 100005;
