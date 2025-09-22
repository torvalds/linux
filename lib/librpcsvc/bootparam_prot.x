/*	$OpenBSD: bootparam_prot.x,v 1.10 2015/01/16 16:48:51 deraadt Exp $	*/

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
 * RPC for bootparams service.
 * There are two procedures:
 *   WHOAMI takes a net address and returns a client name and also a
 *	likely net address for routing
 *   GETFILE takes a client name and file identifier and returns the
 *	server name, server net address and pathname for the file.
 *   file identifiers typically include root, swap, pub and dump
 */

#ifdef RPC_HDR
%#include <sys/types.h>
%#include <rpc/types.h>
%#include <sys/time.h>
%#include <sys/ucred.h>
%#include <errno.h>
#endif

const MAX_MACHINE_NAME  = 255;
const MAX_PATH_LEN	= 1024;
const MAX_FILEID	= 32;
const IP_ADDR_TYPE	= 1;

typedef	string	bp_machine_name_t<MAX_MACHINE_NAME>;
typedef	string	bp_path_t<MAX_PATH_LEN>;
typedef	string	bp_fileid_t<MAX_FILEID>;

struct	ip_addr_t {
	char	net;
	char	host;
	char	lh;
	char	impno;
};

union bp_address switch (int address_type) {
	case IP_ADDR_TYPE:
		ip_addr_t	ip_addr;
};

struct bp_whoami_arg {
	bp_address		client_address;
};

struct bp_whoami_res {
	bp_machine_name_t	client_name;
	bp_machine_name_t	domain_name;
	bp_address		router_address;
};

struct bp_getfile_arg {
	bp_machine_name_t	client_name;
	bp_fileid_t		file_id;
};

struct bp_getfile_res {
	bp_machine_name_t	server_name;
	bp_address		server_address;
	bp_path_t		server_path;
};

program BOOTPARAMPROG {
	version BOOTPARAMVERS {
		bp_whoami_res	BOOTPARAMPROC_WHOAMI(bp_whoami_arg) = 1;
		bp_getfile_res	BOOTPARAMPROC_GETFILE(bp_getfile_arg) = 2;
	} = 1;
} = 100026;
