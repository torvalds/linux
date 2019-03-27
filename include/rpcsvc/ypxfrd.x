/*
 * Copyright (c) 1995, 1996
 *	Bill Paul <wpaul@ctr.columbia.edu>.  All rights reserved.
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
 *	This product includes software developed by Bill Paul.
 * 4. Neither the name of the author nor the names of any co-contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY Bill Paul AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL Bill Paul OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

/*
 * This protocol definition file describes a file transfer
 * system used to very quickly move NIS maps from one host to
 * another. This is similar to what Sun does with their ypxfrd
 * protocol, but it must be stressed that this protocol is _NOT_
 * compatible with Sun's. There are a couple of reasons for this:
 *
 * 1) Sun's protocol is proprietary. The protocol definition is
 *    not freely available in any of the SunRPC source distributions,
 *    even though the NIS v2 protocol is.
 *
 * 2) The idea here is to transfer entire raw files rather than
 *    sending just the records. Sun uses ndbm for its NIS map files,
 *    while FreeBSD uses Berkeley DB. Both are hash databases, but the
 *    formats are incompatible, making it impossible for them to
 *    use each others' files. Even if FreeBSD adopted ndbm for its
 *    database format, FreeBSD/i386 is a little-endian OS and
 *    SunOS/SPARC is big-endian; ndbm is byte-order sensitive and
 *    not very smart about it, which means an attempt to read a
 *    database on a little-endian box that was created on a big-endian
 *    box (or vice-versa) can cause the ndbm code to eat itself.
 *    Luckily, Berkeley DB is able to deal with this situation in
 *    a more graceful manner.
 *
 * While the protocol is incompatible, the idea is the same: we just open
 * up a TCP pipe to the client and transfer the raw map database 
 * from the master server to the slave. This is many times faster than
 * the standard yppush/ypxfr transfer method since it saves us from
 * having to recreate the map databases via the DB library each time.
 * For example: creating a passwd database with 30,000 entries with yp_mkdb
 * can take a couple of minutes, but to just copy the file takes only a few
 * seconds.
 */

#ifndef RPC_HDR
%#include <sys/cdefs.h>
%__FBSDID("$FreeBSD$");
#endif

/* XXX cribbed from yp.x */
const _YPMAXRECORD = 1024;
const _YPMAXDOMAIN = 64;
const _YPMAXMAP = 64;
const _YPMAXPEER = 64;

/* Suggested default -- not necessarily the one used. */
const YPXFRBLOCK = 32767;

/*
 * Possible return codes from the remote server.
 */
enum xfrstat {
	XFR_REQUEST_OK	= 1,	/* Transfer request granted */
	XFR_DENIED	= 2,	/* Transfer request denied */
	XFR_NOFILE	= 3,	/* Requested map file doesn't exist */
	XFR_ACCESS	= 4,	/* File exists, but I couldn't access it */
	XFR_BADDB	= 5,	/* File is not a hash database */
	XFR_READ_OK	= 6,	/* Block read successfully */
	XFR_READ_ERR	= 7,	/* Read error during transfer */
	XFR_DONE	= 8,	/* Transfer completed */
	XFR_DB_ENDIAN_MISMATCH	= 9,	/* Database byte order mismatch */
	XFR_DB_TYPE_MISMATCH	= 10	/* Database type mismatch */
};

/*
 * Database type specifications. The client can use this to ask
 * the server for a particular type of database or just take whatever
 * the server has to offer.
 */
enum xfr_db_type {
	XFR_DB_ASCII		= 1,	/* Flat ASCII text */
	XFR_DB_BSD_HASH		= 2,	/* Berkeley DB, hash method */
	XFR_DB_BSD_BTREE	= 3,	/* Berkeley DB, btree method */
	XFR_DB_BSD_RECNO	= 4,	/* Berkeley DB, recno method */
	XFR_DB_BSD_MPOOL	= 5,	/* Berkeley DB, mpool method */
	XFR_DB_BSD_NDBM		= 6,	/* Berkeley DB, hash, ndbm compat */
	XFR_DB_GNU_GDBM		= 7,	/* GNU GDBM */
	XFR_DB_DBM		= 8,	/* Old, deprecated dbm format */
	XFR_DB_NDBM		= 9,	/* ndbm format (used by Sun's NISv2) */
	XFR_DB_OPAQUE		= 10,	/* Mystery format -- just pass along */
	XFR_DB_ANY		= 11,	/* I'll take any format you've got */
	XFR_DB_UNKNOWN		= 12	/* Unknown format */
};

/*
 * Machine byte order specification. This allows the client to check
 * that it's copying a map database from a machine of similar byte sex.
 * This is necessary for handling database libraries that are fatally
 * byte order sensitive.
 *
 * The XFR_ENDIAN_ANY type is for use with the Berkeley DB database
 * formats; Berkeley DB is smart enough to make up for byte order
 * differences, so byte sex isn't important.
 */
enum xfr_byte_order {
	XFR_ENDIAN_BIG		= 1,	/* We want big endian */
	XFR_ENDIAN_LITTLE	= 2,	/* We want little endian */
	XFR_ENDIAN_ANY		= 3	/* We'll take whatever you got */
};

typedef string xfrdomain<_YPMAXDOMAIN>;
typedef string xfrmap<_YPMAXMAP>;
typedef string xfrmap_filename<_YPMAXMAP>;	/* actual name of map file */

/*
 * Ask the remote ypxfrd for a map using this structure.
 * Note: we supply both a map name and a map file name. These are not
 * the same thing. In the case of ndbm, maps are stored in two files:
 * map.bykey.pag and may.bykey.dir. We may also have to deal with
 * file extensions (on the off chance that the remote server is supporting
 * multiple DB formats). To handle this, we tell the remote server both
 * what map we want and, in the case of ndbm, whether we want the .dir
 * or the .pag part. This name should not be a fully qualified path:
 * it's up to the remote server to decide which directories to look in.
 */
struct ypxfr_mapname {
	xfrmap xfrmap;
	xfrdomain xfrdomain;
	xfrmap_filename xfrmap_filename;
	xfr_db_type xfr_db_type;
	xfr_byte_order xfr_byte_order;
};

/* Read response using this structure. */
union xfr switch (bool ok) {
case TRUE:
	opaque xfrblock_buf<>;
case FALSE:
	xfrstat xfrstat;
};

program YPXFRD_FREEBSD_PROG {
	version YPXFRD_FREEBSD_VERS {
		union xfr
		YPXFRD_GETMAP(ypxfr_mapname) = 1;
	} = 1;
} = 600100069;	/* 100069 + 60000000 -- 100069 is the Sun ypxfrd prog number */
