/*	$OpenBSD: pipe.h,v 1.29 2022/07/09 12:48:21 visa Exp $	*/

/*
 * Copyright (c) 1996 John S. Dyson
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice immediately at the beginning of the file, without modification,
 *    this list of conditions, and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Absolutely no warranty of function or purpose is made by the author
 *    John S. Dyson.
 * 4. This work was done expressly for inclusion into FreeBSD.  Other use
 *    is allowed if this notation is included.
 * 5. Modifications may be freely made to this file if the above conditions
 *    are met.
 */

#ifndef _SYS_PIPE_H_
#define _SYS_PIPE_H_

#ifndef _KERNEL
#include <sys/time.h>			/* for struct timespec */
#endif /* _KERNEL */

#include <sys/event.h>			/* for struct klist */
#include <sys/sigio.h>			/* for struct sigio_ref */

/*
 * Pipe buffer size, keep moderate in value, pipes take kva space.
 */
#ifndef PIPE_SIZE
#define PIPE_SIZE	16384
#endif

#ifndef BIG_PIPE_SIZE
#define BIG_PIPE_SIZE	(64*1024)
#endif

/*
 * Pipe buffer information.
 * Separate in, out, cnt are used to simplify calculations.
 * Buffered write is active when the buffer.cnt field is set.
 */
struct pipebuf {
	u_int	cnt;		/* number of chars currently in buffer */
	u_int	in;		/* in pointer */
	u_int	out;		/* out pointer */
	u_int	size;		/* size of buffer */
	caddr_t	buffer;		/* kva of buffer */
};

/*
 * Bits in pipe_state.
 */
#define PIPE_ASYNC	0x004	/* Async? I/O. */
#define PIPE_WANTR	0x008	/* Reader wants some characters. */
#define PIPE_WANTW	0x010	/* Writer wants space to put characters. */
#define PIPE_WANTD	0x020	/* Pipe is wanted to be run-down. */
#define PIPE_EOF	0x080	/* Pipe is in EOF condition. */
#define PIPE_LOCK	0x100	/* Thread has exclusive I/O access. */
#define PIPE_LWANT	0x200	/* Thread wants exclusive I/O access. */

struct pipe_pair;

/*
 * Per-pipe data structure.
 * Two of these are linked together to produce bi-directional pipes.
 *
 * Locking:
 *	I	immutable after creation
 *	S	sigio_lock
 *	p	pipe_lock
 */
struct pipe {
	struct	rwlock *pipe_lock;
	struct	pipebuf pipe_buffer;	/* [p] data storage */
	struct	klist pipe_klist;	/* [p] list of knotes */
	struct	timespec pipe_atime;	/* [p] time of last access */
	struct	timespec pipe_mtime;	/* [p] time of last modify */
	struct	timespec pipe_ctime;	/* [I] time of status change */
	struct	sigio_ref pipe_sigio;	/* [S] async I/O registration */
	struct	pipe *pipe_peer;	/* [p] link with other direction */
	struct	pipe_pair *pipe_pair;	/* [I] pipe storage */
	u_int	pipe_state;		/* [p] pipe status info */
	int	pipe_busy;		/* [p] # readers/writers */
};

#ifdef _KERNEL
void	pipe_init(void);
#endif /* _KERNEL */

#endif /* !_SYS_PIPE_H_ */
