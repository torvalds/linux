/*-
 * Copyright (c) 2016 Eric McCorkle
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

#ifndef _INTAKE_H_
#define _INTAKE_H_

#include  <sys/param.h>

/*
 * This file provides an interface for providing keys to the kernel
 * during boot time.
 */

#define MAX_KEY_BITS	4096
#define	MAX_KEY_BYTES	(MAX_KEY_BITS / NBBY)

#define KEYBUF_SENTINEL	0xcee54b5d	/* KEYS4BSD */

enum {
        KEYBUF_TYPE_NONE,
        KEYBUF_TYPE_GELI
};

struct keybuf_ent {
        unsigned int ke_type;
        char ke_data[MAX_KEY_BYTES];
};

struct keybuf {
        unsigned int kb_nents;
        struct keybuf_ent kb_ents[];
};

#ifdef _KERNEL
/* Get the key intake buffer */
extern struct keybuf* get_keybuf(void);
#endif

#endif
