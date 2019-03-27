/*
 * Copyright (c) 1996
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

#ifndef RPC_HDR
%#include <sys/cdefs.h>
%__FBSDID("$FreeBSD$");
#endif

/*
 * This protocol definition exists because of the U.S. government and
 * its stupid export laws. We can't export DES code from the United
 * States to other countries (even though the code already exists
 * outside the U.S. -- go figure that one out) but we need to make
 * Secure RPC work. The normal way around this is to break the DES
 * code out into a shared library; we can then provide a dummy lib
 * in the base OS and provide the real lib in the secure dist, which
 * the user can install later. But we need Secure RPC for NIS+, and
 * there are several system programs that use NIS+ which are statically
 * linked. We would have to provide replacements for these programs
 * in the secure dist, but there are a lot, and this is a pain. The
 * shared lib trick won't work for these programs, and we can't change
 * them once they're compiled.
 *
 * One solution for this problem is to do the DES encryption as a system
 * call; no programs need to be changed and we can even supply the DES
 * support as an LKM. But this bloats the kernel. Maybe if we have
 * Secure NFS one day this will be worth it, but for now we should keep
 * this mess in user space.
 *
 * So we have this second solution: we provide a server that does the
 * DES encryption for us. In this case, the server is keyserv (we need
 * it to make Secure RPC work anyway) and we use this protocol to ship
 * the data back and forth between keyserv and the application.
 */

enum des_dir { ENCRYPT_DES, DECRYPT_DES };
enum des_mode { CBC_DES, ECB_DES };

struct desargs {
	u_char des_key[8];	/* key (with low bit parity) */
	des_dir des_dir;	/* direction */
	des_mode des_mode;	/* mode */
	u_char des_ivec[8];	/* input vector */
	opaque desbuf<>;
};

struct desresp {
	opaque desbuf<>;
	u_char des_ivec[8];
	int stat;
};

program CRYPT_PROG {
	version CRYPT_VERS {
		desresp
		DES_CRYPT(desargs) = 1;
	} = 1;
} = 600100029;
