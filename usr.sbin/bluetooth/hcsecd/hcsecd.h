/*-
 * hcsecd.h
 *
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2001-2002 Maksim Yevmenkin <m_evmenkin@yahoo.com>
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
 * ARE DISCLAIMED. IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $Id: hcsecd.h,v 1.3 2003/09/08 18:54:21 max Exp $
 * $FreeBSD$
 */

#ifndef _HCSECD_H_
#define _HCSECD_H_ 1

#define HCSECD_BUFFER_SIZE	512
#define HCSECD_IDENT		"hcsecd"
#define HCSECD_PIDFILE		"/var/run/" HCSECD_IDENT ".pid"
#define HCSECD_KEYSFILE		"/var/db/"  HCSECD_IDENT ".keys"

struct link_key
{
	bdaddr_t		 bdaddr; /* remote device BDADDR */
	char			*name;   /* remote device name */
	uint8_t			*key;    /* link key (or NULL if no key) */
	char			*pin;    /* pin (or NULL if no pin) */
	LIST_ENTRY(link_key)	 next;   /* link to the next */
};
typedef struct link_key		link_key_t;
typedef struct link_key *	link_key_p;

extern char	*config_file;

#if __config_debug__
void		dump_config	(void);
#endif

void		read_config_file(void);
void		clean_config	(void);
link_key_p	get_key		(bdaddr_p bdaddr, int exact_match);

int		read_keys_file  (void);
int		dump_keys_file  (void);

#endif /* ndef _HCSECD_H_ */

