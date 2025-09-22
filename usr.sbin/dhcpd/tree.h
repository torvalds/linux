/*	$OpenBSD: tree.h,v 1.5 2004/09/16 18:35:43 deraadt Exp $	*/

/* Definitions for address trees... */

/*
 * Copyright (c) 1995 The Internet Software Consortium.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of The Internet Software Consortium nor the names
 *    of its contributors may be used to endorse or promote products derived
 *    from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE INTERNET SOFTWARE CONSORTIUM AND
 * CONTRIBUTORS ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES,
 * INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED.  IN NO EVENT SHALL THE INTERNET SOFTWARE CONSORTIUM OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF
 * USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT
 * OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * This software has been written for the Internet Software Consortium
 * by Ted Lemon <mellon@fugue.com> in cooperation with Vixie
 * Enterprises.  To learn more about the Internet Software Consortium,
 * see ``http://www.vix.com/isc''.  To learn more about Vixie
 * Enterprises, see ``http://www.vix.com''.
 */

/* A pair of pointers, suitable for making a linked list. */
typedef struct _pair {
	caddr_t		 car;
	struct _pair	*cdr;
} *pair;

/* Tree node types... */
#define TREE_CONCAT		1
#define TREE_HOST_LOOKUP	2
#define TREE_CONST		3
#define TREE_LIMIT		4

/* Tree structure for deferred evaluation of changing values. */
struct tree {
	int	op;
	union {
		struct concat {
			struct tree	*left;
			struct tree	*right;
		} concat;
		struct host_lookup {
			struct dns_host_entry *host;
		} host_lookup;
		struct const_val {
			unsigned char	*data;
			int		 len;
		} const_val;
		struct limit {
			struct tree	*tree;
			int		 limit;
		} limit;
	} data;
};

/* DNS host entry structure... */
struct dns_host_entry {
	char		*hostname;
	unsigned char	*data;
	int		 data_len;
	int		 buf_len;
	time_t		 timeout;
};

struct tree_cache {
	unsigned char	*value;
	int		 len;
	unsigned int	 buf_size;
	time_t		 timeout;
	struct tree	*tree;
	int		 flags;
#define	TC_AWAITING_RESOLUTION	1
#define TC_TEMPORARY		2
};

struct universe {
	char		*name;
	struct hash_table *hash;
	struct option	*options[256];
};

struct option {
	char		*name;
	char		*format;
	struct universe	*universe;
	unsigned char	code;
};
