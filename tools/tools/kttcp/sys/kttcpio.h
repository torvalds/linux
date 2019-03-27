/*	$FreeBSD$	*/
/*	$NetBSD$	*/

/*
 * Copyright (c) 2002 Wasabi Systems, Inc.
 * All rights reserved.
 *
 * Written by Frank van der Linden and Jason R. Thorpe for
 * Wasabi Systems, Inc.
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
 *	This product includes software developed for the NetBSD Project by
 *	Wasabi Systems, Inc.
 * 4. The name of Wasabi Systems, Inc. may not be used to endorse
 *    or promote products derived from this software without specific prior
 *    written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY WASABI SYSTEMS, INC. ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL WASABI SYSTEMS, INC
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef _DEV_KTTCPIO_H_
#define	_DEV_KTTCPIO_H_

#include <sys/ioccom.h>
#include <sys/time.h>

struct kttcp_io_args {
	unsigned long long kio_totalsize;/* i/o total size (IN) */
	unsigned long long kio_bytesdone;/* i/o actually completed (OUT) */
	struct timeval kio_elapsed;	 /* elapsed time (OUT) */
	int	kio_socket;		 /* socket to use for i/o (IN) */
	int	kio_protovers;		 /* KTTCP protocol version */
};

#define	KTTCP_IO_SEND		_IOWR('K', 0, struct kttcp_io_args)
#define	KTTCP_IO_RECV		_IOWR('K', 1, struct kttcp_io_args)

#define	KTTCP_MAX_XMIT		0x7fffffffLL /* XXX can't handle > 31 bits */

#endif /* _DEV_KTTCPIO_H_ */
