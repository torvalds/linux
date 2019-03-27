/*-
 * Copyright (c) 2013-2014 Devin Teske <dteske@FreeBSD.org>
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

#ifndef _DPROMPT_H_
#define _DPROMPT_H_

#include <sys/cdefs.h>

#include "dpv.h"

/* Display characteristics */
#define ENV_MSG_DONE	"msg_done"
#define ENV_MSG_FAIL	"msg_fail"
#define ENV_MSG_PENDING	"msg_pending"
extern int display_limit;
extern int label_size;
extern int pbar_size;

__BEGIN_DECLS
void	dprompt_clear(void);
void	dprompt_dprint(int _fd, const char *_prefix, const char *_append,
	    int _overall);
void	dprompt_free(void);
void	dprompt_init(struct dpv_file_node *_file_list);
void	dprompt_libprint(const char *_prefix, const char *_append,
	    int _overall);
void	dprompt_recreate(struct dpv_file_node *_file_list,
	    struct dpv_file_node *_curfile, int _pct);
int	dprompt_add(const char *_format, ...);
int	dprompt_sprint(char * restrict _str, const char *_prefix,
	    const char *_append);
__END_DECLS

#endif /* !_DPROMPT_H_ */
