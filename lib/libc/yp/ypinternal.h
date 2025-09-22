/*	$OpenBSD: ypinternal.h,v 1.14 2022/08/02 16:59:30 deraadt Exp $	 */

/*
 * Copyright (c) 1992, 1993, 1996 Theo de Raadt <deraadt@theos.com>
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS
 * OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

/*
 * XXX  We have to define these here due to clashes between
 * yp_prot.h and yp.h.
 */
struct dom_binding {
	struct sockaddr_in dom_server_addr;
	int dom_socket;
	CLIENT *dom_client;
};

#define BINDINGDIR	"/var/yp/binding"

__BEGIN_HIDDEN_DECLS
extern struct dom_binding *_ypbindlist;
extern char _yp_domain[HOST_NAME_MAX+1];
extern int _yplib_timeout;

void	_yp_unbind(struct dom_binding *);
int	ypconnect(int type);
__END_HIDDEN_DECLS

int	_yp_check(char **);
PROTO_NORMAL(_yp_check);
PROTO_WRAP(ypconnect);
