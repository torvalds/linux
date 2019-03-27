/*-
 * Copyright (c) 2018 Conrad Meyer <cem@FreeBSD.org>
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
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#define _WANT_KERNEL_ERRNO	1
#include <errno.h>

#include <lua.h>
#include "lauxlib.h"
#include "lerrno.h"

#ifndef nitems
#define	nitems(x)	(sizeof((x)) / sizeof((x)[0]))
#endif

/*
 * Populated with:
 * $ egrep "^#define.E" sys/sys/errno.h | \
 *   awk '{ print "\tENTRY(" $2 ")," }' >> lerrno.c
 */
#define ENTRY(name)	{ #name, name }
static const struct err_name_number {
	const char *err_name;
	int err_num;
} errnoconstants[] = {
	ENTRY(EPERM),
	ENTRY(ENOENT),
	ENTRY(ESRCH),
	ENTRY(EINTR),
	ENTRY(EIO),
	ENTRY(ENXIO),
	ENTRY(E2BIG),
	ENTRY(ENOEXEC),
	ENTRY(EBADF),
	ENTRY(ECHILD),
	ENTRY(EDEADLK),
	ENTRY(ENOMEM),
	ENTRY(EACCES),
	ENTRY(EFAULT),
	ENTRY(ENOTBLK),
	ENTRY(EBUSY),
	ENTRY(EEXIST),
	ENTRY(EXDEV),
	ENTRY(ENODEV),
	ENTRY(ENOTDIR),
	ENTRY(EISDIR),
	ENTRY(EINVAL),
	ENTRY(ENFILE),
	ENTRY(EMFILE),
	ENTRY(ENOTTY),
	ENTRY(ETXTBSY),
	ENTRY(EFBIG),
	ENTRY(ENOSPC),
	ENTRY(ESPIPE),
	ENTRY(EROFS),
	ENTRY(EMLINK),
	ENTRY(EPIPE),
	ENTRY(EDOM),
	ENTRY(ERANGE),
	ENTRY(EAGAIN),
	ENTRY(EWOULDBLOCK),
	ENTRY(EINPROGRESS),
	ENTRY(EALREADY),
	ENTRY(ENOTSOCK),
	ENTRY(EDESTADDRREQ),
	ENTRY(EMSGSIZE),
	ENTRY(EPROTOTYPE),
	ENTRY(ENOPROTOOPT),
	ENTRY(EPROTONOSUPPORT),
	ENTRY(ESOCKTNOSUPPORT),
	ENTRY(EOPNOTSUPP),
	ENTRY(ENOTSUP),
	ENTRY(EPFNOSUPPORT),
	ENTRY(EAFNOSUPPORT),
	ENTRY(EADDRINUSE),
	ENTRY(EADDRNOTAVAIL),
	ENTRY(ENETDOWN),
	ENTRY(ENETUNREACH),
	ENTRY(ENETRESET),
	ENTRY(ECONNABORTED),
	ENTRY(ECONNRESET),
	ENTRY(ENOBUFS),
	ENTRY(EISCONN),
	ENTRY(ENOTCONN),
	ENTRY(ESHUTDOWN),
	ENTRY(ETOOMANYREFS),
	ENTRY(ETIMEDOUT),
	ENTRY(ECONNREFUSED),
	ENTRY(ELOOP),
	ENTRY(ENAMETOOLONG),
	ENTRY(EHOSTDOWN),
	ENTRY(EHOSTUNREACH),
	ENTRY(ENOTEMPTY),
	ENTRY(EPROCLIM),
	ENTRY(EUSERS),
	ENTRY(EDQUOT),
	ENTRY(ESTALE),
	ENTRY(EREMOTE),
	ENTRY(EBADRPC),
	ENTRY(ERPCMISMATCH),
	ENTRY(EPROGUNAVAIL),
	ENTRY(EPROGMISMATCH),
	ENTRY(EPROCUNAVAIL),
	ENTRY(ENOLCK),
	ENTRY(ENOSYS),
	ENTRY(EFTYPE),
	ENTRY(EAUTH),
	ENTRY(ENEEDAUTH),
	ENTRY(EIDRM),
	ENTRY(ENOMSG),
	ENTRY(EOVERFLOW),
	ENTRY(ECANCELED),
	ENTRY(EILSEQ),
	ENTRY(ENOATTR),
	ENTRY(EDOOFUS),
	ENTRY(EBADMSG),
	ENTRY(EMULTIHOP),
	ENTRY(ENOLINK),
	ENTRY(EPROTO),
	ENTRY(ENOTCAPABLE),
	ENTRY(ECAPMODE),
	ENTRY(ENOTRECOVERABLE),
	ENTRY(EOWNERDEAD),
	ENTRY(EINTEGRITY),
	ENTRY(ELAST),
	ENTRY(ERESTART),
	ENTRY(EJUSTRETURN),
	ENTRY(ENOIOCTL),
	ENTRY(EDIRIOCTL),
	ENTRY(ERELOOKUP),
};
#undef ENTRY

static void
lerrno_register(lua_State *L)
{
	size_t i;

	for (i = 0; i < nitems(errnoconstants); i++) {
		lua_pushinteger(L, errnoconstants[i].err_num);
		lua_setfield(L, -2, errnoconstants[i].err_name);
	}
}

static const struct luaL_Reg errnolib[] = {
	/* Extra bogus entry required by luaL_newlib API */
	{ NULL, NULL },
};

int
luaopen_errno(lua_State *L)
{
	luaL_newlib(L, errnolib);
	lerrno_register(L);
	return 1;
}
