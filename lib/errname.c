// SPDX-License-Identifier: GPL-2.0
#include <linux/build_bug.h>
#include <linux/errno.h>
#include <linux/errname.h>
#include <linux/kernel.h>
#include <linux/math.h>

/*
 * Ensure these tables do not accidentally become gigantic if some
 * huge errno makes it in. On most architectures, the first table will
 * only have about 140 entries, but mips and parisc have more sparsely
 * allocated errnos (with EHWPOISON = 257 on parisc, and EDQUOT = 1133
 * on mips), so this wastes a bit of space on those - though we
 * special case the EDQUOT case.
 */
#define E(err) [err + BUILD_BUG_ON_ZERO(err <= 0 || err > 300)] = "-" #err
static const char *names_0[] = {
	E(E2BIG),
	E(EACCES),
	E(EADDRINUSE),
	E(EADDRNOTAVAIL),
	E(EADV),
	E(EAFNOSUPPORT),
	E(EAGAIN), /* EWOULDBLOCK */
	E(EALREADY),
	E(EBADE),
	E(EBADF),
	E(EBADFD),
	E(EBADMSG),
	E(EBADR),
	E(EBADRQC),
	E(EBADSLT),
	E(EBFONT),
	E(EBUSY),
	E(ECANCELED), /* ECANCELLED */
	E(ECHILD),
	E(ECHRNG),
	E(ECOMM),
	E(ECONNABORTED),
	E(ECONNREFUSED), /* EREFUSED */
	E(ECONNRESET),
	E(EDEADLK), /* EDEADLOCK */
#if EDEADLK != EDEADLOCK /* mips, sparc, powerpc */
	E(EDEADLOCK),
#endif
	E(EDESTADDRREQ),
	E(EDOM),
	E(EDOTDOT),
#ifndef CONFIG_MIPS
	E(EDQUOT),
#endif
	E(EEXIST),
	E(EFAULT),
	E(EFBIG),
	E(EHOSTDOWN),
	E(EHOSTUNREACH),
	E(EHWPOISON),
	E(EIDRM),
	E(EILSEQ),
#ifdef EINIT
	E(EINIT),
#endif
	E(EINPROGRESS),
	E(EINTR),
	E(EINVAL),
	E(EIO),
	E(EISCONN),
	E(EISDIR),
	E(EISNAM),
	E(EKEYEXPIRED),
	E(EKEYREJECTED),
	E(EKEYREVOKED),
	E(EL2HLT),
	E(EL2NSYNC),
	E(EL3HLT),
	E(EL3RST),
	E(ELIBACC),
	E(ELIBBAD),
	E(ELIBEXEC),
	E(ELIBMAX),
	E(ELIBSCN),
	E(ELNRNG),
	E(ELOOP),
	E(EMEDIUMTYPE),
	E(EMFILE),
	E(EMLINK),
	E(EMSGSIZE),
	E(EMULTIHOP),
	E(ENAMETOOLONG),
	E(ENAVAIL),
	E(ENETDOWN),
	E(ENETRESET),
	E(ENETUNREACH),
	E(ENFILE),
	E(ENOANO),
	E(ENOBUFS),
	E(ENOCSI),
	E(ENODATA),
	E(ENODEV),
	E(ENOENT),
	E(ENOEXEC),
	E(ENOKEY),
	E(ENOLCK),
	E(ENOLINK),
	E(ENOMEDIUM),
	E(ENOMEM),
	E(ENOMSG),
	E(ENONET),
	E(ENOPKG),
	E(ENOPROTOOPT),
	E(ENOSPC),
	E(ENOSR),
	E(ENOSTR),
	E(ENOSYS),
	E(ENOTBLK),
	E(ENOTCONN),
	E(ENOTDIR),
	E(ENOTEMPTY),
	E(ENOTNAM),
	E(ENOTRECOVERABLE),
	E(ENOTSOCK),
	E(ENOTTY),
	E(ENOTUNIQ),
	E(ENXIO),
	E(EOPNOTSUPP),
	E(EOVERFLOW),
	E(EOWNERDEAD),
	E(EPERM),
	E(EPFNOSUPPORT),
	E(EPIPE),
#ifdef EPROCLIM
	E(EPROCLIM),
#endif
	E(EPROTO),
	E(EPROTONOSUPPORT),
	E(EPROTOTYPE),
	E(ERANGE),
	E(EREMCHG),
#ifdef EREMDEV
	E(EREMDEV),
#endif
	E(EREMOTE),
	E(EREMOTEIO),
	E(ERESTART),
	E(ERFKILL),
	E(EROFS),
#ifdef ERREMOTE
	E(ERREMOTE),
#endif
	E(ESHUTDOWN),
	E(ESOCKTNOSUPPORT),
	E(ESPIPE),
	E(ESRCH),
	E(ESRMNT),
	E(ESTALE),
	E(ESTRPIPE),
	E(ETIME),
	E(ETIMEDOUT),
	E(ETOOMANYREFS),
	E(ETXTBSY),
	E(EUCLEAN),
	E(EUNATCH),
	E(EUSERS),
	E(EXDEV),
	E(EXFULL),
};
#undef E

#ifdef EREFUSED /* parisc */
static_assert(EREFUSED == ECONNREFUSED);
#endif
#ifdef ECANCELLED /* parisc */
static_assert(ECANCELLED == ECANCELED);
#endif
static_assert(EAGAIN == EWOULDBLOCK); /* everywhere */

#define E(err) [err - 512 + BUILD_BUG_ON_ZERO(err < 512 || err > 550)] = "-" #err
static const char *names_512[] = {
	E(ERESTARTSYS),
	E(ERESTARTNOINTR),
	E(ERESTARTNOHAND),
	E(ENOIOCTLCMD),
	E(ERESTART_RESTARTBLOCK),
	E(EPROBE_DEFER),
	E(EOPENSTALE),
	E(ENOPARAM),

	E(EBADHANDLE),
	E(ENOTSYNC),
	E(EBADCOOKIE),
	E(ENOTSUPP),
	E(ETOOSMALL),
	E(ESERVERFAULT),
	E(EBADTYPE),
	E(EJUKEBOX),
	E(EIOCBQUEUED),
	E(ERECALLCONFLICT),
};
#undef E

static const char *__errname(unsigned err)
{
	if (err < ARRAY_SIZE(names_0))
		return names_0[err];
	if (err >= 512 && err - 512 < ARRAY_SIZE(names_512))
		return names_512[err - 512];
	/* But why? */
	if (IS_ENABLED(CONFIG_MIPS) && err == EDQUOT) /* 1133 */
		return "-EDQUOT";
	return NULL;
}

/*
 * errname(EIO) -> "EIO"
 * errname(-EIO) -> "-EIO"
 */
const char *errname(int err)
{
	const char *name = __errname(abs(err));
	if (!name)
		return NULL;

	return err > 0 ? name + 1 : name;
}
