/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2005 Paolo Pisati <piso@FreeBSD.org>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $FreeBSD$
 */

/*
 * Alias_mod.h defines the outside world interfaces for the packet aliasing
 * modular framework
 */

#ifndef _ALIAS_MOD_H_
#define _ALIAS_MOD_H_

#ifdef _KERNEL
MALLOC_DECLARE(M_ALIAS);

/* Use kernel allocator. */
#if defined(_SYS_MALLOC_H_)
#undef malloc
#define	malloc(x)	malloc(x, M_ALIAS, M_NOWAIT|M_ZERO)
#define	calloc(n, x)	mallocarray((n), (x), M_ALIAS, M_NOWAIT|M_ZERO)
#define	free(x)		free(x, M_ALIAS)
#endif
#endif

/* Packet flow direction flags. */
#define IN	0x0001
#define OUT	0x0002
#define	NODIR	0x4000

/* Working protocol flags. */
#define IP	0x01
#define TCP	0x02
#define UDP	0x04

/*
 * Data passed to protocol handler module, it must be filled
 * right before calling find_handler() to determine which
 * module is elegible to be called.
 */
struct alias_data {
	struct alias_link	*lnk;
	struct in_addr		*oaddr;		/* Original address. */
	struct in_addr		*aaddr;		/* Alias address. */
	uint16_t		*aport;		/* Alias port. */
	uint16_t		*sport, *dport;	/* Source & destination port */
	uint16_t		maxpktsize;	/* Max packet size. */
};

/*
 * This structure contains all the information necessary to make
 * a protocol handler correctly work.
 */
struct proto_handler {
	u_int pri;		/* Handler priority. */
	int16_t dir;		/* Flow direction. */
	uint8_t proto;		/* Working protocol. */
	/* Fingerprint * function. */
	int (*fingerprint)(struct libalias *, struct alias_data *);
	/* Aliasing * function. */
	int (*protohandler)(struct libalias *, struct ip *,
	    struct alias_data *);
	TAILQ_ENTRY(proto_handler) link;
};

/* End of handlers. */
#define EOH	.dir = NODIR

/* Functions used with protocol handlers. */
int LibAliasAttachHandlers(struct proto_handler *);
int LibAliasDetachHandlers(struct proto_handler *);
int find_handler(int8_t, int8_t, struct libalias *, struct ip *,
    struct alias_data *);
struct proto_handler *first_handler(void);

#ifndef _KERNEL
/*
 * Used only in userland when libalias needs to keep track of all
 * module loaded. In kernel land (kld mode) we don't need to care
 * care about libalias modules cause it's kld to do it for us.
 */
#define DLL_LEN	 32
struct dll {
	char	name[DLL_LEN];	/* Name of module. */
	void	*handle;	/*
				 * Ptr to shared obj obtained through
				 * dlopen() - use this ptr to get access
				 * to any symbols from a loaded module
				 * via dlsym().
				 */
	SLIST_ENTRY(dll)	next;
};

/* Functions used with dll module. */
void dll_chain_init(void);
void dll_chain_destroy(void);
int attach_dll(struct dll *);
void *detach_dll(char *);
struct dll *walk_dll_chain(void);

/*
 * Some defines borrowed from sys/module.h used to compile a kld
 * in userland as a shared lib.
 */
typedef enum modeventtype {
	MOD_LOAD,
	MOD_UNLOAD,
	MOD_SHUTDOWN,
	MOD_QUIESCE
} modeventtype_t;

typedef struct module *module_t;
typedef int (*modeventhand_t)(module_t, int /* modeventtype_t */, void *);

/*
 * Struct for registering modules statically via SYSINIT.
 */
typedef struct moduledata {
	const char	*name;	/* module name */
	modeventhand_t	evhand;	/* event handler */
	void		*priv;	/* extra data */
} moduledata_t;
#endif /* !_KERNEL */

#endif /* !_ALIAS_MOD_H_ */
