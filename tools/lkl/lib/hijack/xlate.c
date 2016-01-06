#include <errno.h>
#define __USE_GNU
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#undef st_atime
#undef st_mtime
#undef st_ctime
#include <lkl.h>

#include "xlate.h"

long lkl_set_errno(long err)
{
	if (err >= 0)
		return err;

	switch (err) {
	case -LKL_EPERM:
		errno = EPERM;
		break;
	case -LKL_ENOENT:
		errno = ENOENT;
		break;
	case -LKL_ESRCH:
		errno = ESRCH;
		break;
	case -LKL_EINTR:
		errno = EINTR;
		break;
	case -LKL_EIO:
		errno = EIO;
		break;
	case -LKL_ENXIO:
		errno = ENXIO;
		break;
	case -LKL_E2BIG:
		errno = E2BIG;
		break;
	case -LKL_ENOEXEC:
		errno = ENOEXEC;
		break;
	case -LKL_EBADF:
		errno = EBADF;
		break;
	case -LKL_ECHILD:
		errno = ECHILD;
		break;
	case -LKL_EAGAIN:
		errno = EAGAIN;
		break;
	case -LKL_ENOMEM:
		errno = ENOMEM;
		break;
	case -LKL_EACCES:
		errno = EACCES;
		break;
	case -LKL_EFAULT:
		errno = EFAULT;
		break;
	case -LKL_ENOTBLK:
		errno = ENOTBLK;
		break;
	case -LKL_EBUSY:
		errno = EBUSY;
		break;
	case -LKL_EEXIST:
		errno = EEXIST;
		break;
	case -LKL_EXDEV:
		errno = EXDEV;
		break;
	case -LKL_ENODEV:
		errno = ENODEV;
		break;
	case -LKL_ENOTDIR:
		errno = ENOTDIR;
		break;
	case -LKL_EISDIR:
		errno = EISDIR;
		break;
	case -LKL_EINVAL:
		errno = EINVAL;
		break;
	case -LKL_ENFILE:
		errno = ENFILE;
		break;
	case -LKL_EMFILE:
		errno = EMFILE;
		break;
	case -LKL_ENOTTY:
		errno = ENOTTY;
		break;
	case -LKL_ETXTBSY:
		errno = ETXTBSY;
		break;
	case -LKL_EFBIG:
		errno = EFBIG;
		break;
	case -LKL_ENOSPC:
		errno = ENOSPC;
		break;
	case -LKL_ESPIPE:
		errno = ESPIPE;
		break;
	case -LKL_EROFS:
		errno = EROFS;
		break;
	case -LKL_EMLINK:
		errno = EMLINK;
		break;
	case -LKL_EPIPE:
		errno = EPIPE;
		break;
	case -LKL_EDOM:
		errno = EDOM;
		break;
	case -LKL_ERANGE:
		errno = ERANGE;
		break;
	case -LKL_EDEADLK:
		errno = EDEADLK;
		break;
	case -LKL_ENAMETOOLONG:
		errno = ENAMETOOLONG;
		break;
	case -LKL_ENOLCK:
		errno = ENOLCK;
		break;
	case -LKL_ENOSYS:
		errno = ENOSYS;
		break;
	case -LKL_ENOTEMPTY:
		errno = ENOTEMPTY;
		break;
	case -LKL_ELOOP:
		errno = ELOOP;
		break;
	case -LKL_ENOMSG:
		errno = ENOMSG;
		break;
	case -LKL_EIDRM:
		errno = EIDRM;
		break;
	case -LKL_ECHRNG:
		errno = ECHRNG;
		break;
	case -LKL_EL2NSYNC:
		errno = EL2NSYNC;
		break;
	case -LKL_EL3HLT:
		errno = EL3HLT;
		break;
	case -LKL_EL3RST:
		errno = EL3RST;
		break;
	case -LKL_ELNRNG:
		errno = ELNRNG;
		break;
	case -LKL_EUNATCH:
		errno = EUNATCH;
		break;
	case -LKL_ENOCSI:
		errno = ENOCSI;
		break;
	case -LKL_EL2HLT:
		errno = EL2HLT;
		break;
	case -LKL_EBADE:
		errno = EBADE;
		break;
	case -LKL_EBADR:
		errno = EBADR;
		break;
	case -LKL_EXFULL:
		errno = EXFULL;
		break;
	case -LKL_ENOANO:
		errno = ENOANO;
		break;
	case -LKL_EBADRQC:
		errno = EBADRQC;
		break;
	case -LKL_EBADSLT:
		errno = EBADSLT;
		break;
	case -LKL_EBFONT:
		errno = EBFONT;
		break;
	case -LKL_ENOSTR:
		errno = ENOSTR;
		break;
	case -LKL_ENODATA:
		errno = ENODATA;
		break;
	case -LKL_ETIME:
		errno = ETIME;
		break;
	case -LKL_ENOSR:
		errno = ENOSR;
		break;
	case -LKL_ENONET:
		errno = ENONET;
		break;
	case -LKL_ENOPKG:
		errno = ENOPKG;
		break;
	case -LKL_EREMOTE:
		errno = EREMOTE;
		break;
	case -LKL_ENOLINK:
		errno = ENOLINK;
		break;
	case -LKL_EADV:
		errno = EADV;
		break;
	case -LKL_ESRMNT:
		errno = ESRMNT;
		break;
	case -LKL_ECOMM:
		errno = ECOMM;
		break;
	case -LKL_EPROTO:
		errno = EPROTO;
		break;
	case -LKL_EMULTIHOP:
		errno = EMULTIHOP;
		break;
	case -LKL_EDOTDOT:
		errno = EDOTDOT;
		break;
	case -LKL_EBADMSG:
		errno = EBADMSG;
		break;
	case -LKL_EOVERFLOW:
		errno = EOVERFLOW;
		break;
	case -LKL_ENOTUNIQ:
		errno = ENOTUNIQ;
		break;
	case -LKL_EBADFD:
		errno = EBADFD;
		break;
	case -LKL_EREMCHG:
		errno = EREMCHG;
		break;
	case -LKL_ELIBACC:
		errno = ELIBACC;
		break;
	case -LKL_ELIBBAD:
		errno = ELIBBAD;
		break;
	case -LKL_ELIBSCN:
		errno = ELIBSCN;
		break;
	case -LKL_ELIBMAX:
		errno = ELIBMAX;
		break;
	case -LKL_ELIBEXEC:
		errno = ELIBEXEC;
		break;
	case -LKL_EILSEQ:
		errno = EILSEQ;
		break;
	case -LKL_ERESTART:
		errno = ERESTART;
		break;
	case -LKL_ESTRPIPE:
		errno = ESTRPIPE;
		break;
	case -LKL_EUSERS:
		errno = EUSERS;
		break;
	case -LKL_ENOTSOCK:
		errno = ENOTSOCK;
		break;
	case -LKL_EDESTADDRREQ:
		errno = EDESTADDRREQ;
		break;
	case -LKL_EMSGSIZE:
		errno = EMSGSIZE;
		break;
	case -LKL_EPROTOTYPE:
		errno = EPROTOTYPE;
		break;
	case -LKL_ENOPROTOOPT:
		errno = ENOPROTOOPT;
		break;
	case -LKL_EPROTONOSUPPORT:
		errno = EPROTONOSUPPORT;
		break;
	case -LKL_ESOCKTNOSUPPORT:
		errno = ESOCKTNOSUPPORT;
		break;
	case -LKL_EOPNOTSUPP:
		errno = EOPNOTSUPP;
		break;
	case -LKL_EPFNOSUPPORT:
		errno = EPFNOSUPPORT;
		break;
	case -LKL_EAFNOSUPPORT:
		errno = EAFNOSUPPORT;
		break;
	case -LKL_EADDRINUSE:
		errno = EADDRINUSE;
		break;
	case -LKL_EADDRNOTAVAIL:
		errno = EADDRNOTAVAIL;
		break;
	case -LKL_ENETDOWN:
		errno = ENETDOWN;
		break;
	case -LKL_ENETUNREACH:
		errno = ENETUNREACH;
		break;
	case -LKL_ENETRESET:
		errno = ENETRESET;
		break;
	case -LKL_ECONNABORTED:
		errno = ECONNABORTED;
		break;
	case -LKL_ECONNRESET:
		errno = ECONNRESET;
		break;
	case -LKL_ENOBUFS:
		errno = ENOBUFS;
		break;
	case -LKL_EISCONN:
		errno = EISCONN;
		break;
	case -LKL_ENOTCONN:
		errno = ENOTCONN;
		break;
	case -LKL_ESHUTDOWN:
		errno = ESHUTDOWN;
		break;
	case -LKL_ETOOMANYREFS:
		errno = ETOOMANYREFS;
		break;
	case -LKL_ETIMEDOUT:
		errno = ETIMEDOUT;
		break;
	case -LKL_ECONNREFUSED:
		errno = ECONNREFUSED;
		break;
	case -LKL_EHOSTDOWN:
		errno = EHOSTDOWN;
		break;
	case -LKL_EHOSTUNREACH:
		errno = EHOSTUNREACH;
		break;
	case -LKL_EALREADY:
		errno = EALREADY;
		break;
	case -LKL_EINPROGRESS:
		errno = EINPROGRESS;
		break;
	case -LKL_ESTALE:
		errno = ESTALE;
		break;
	case -LKL_EUCLEAN:
		errno = EUCLEAN;
		break;
	case -LKL_ENOTNAM:
		errno = ENOTNAM;
		break;
	case -LKL_ENAVAIL:
		errno = ENAVAIL;
		break;
	case -LKL_EISNAM:
		errno = EISNAM;
		break;
	case -LKL_EREMOTEIO:
		errno = EREMOTEIO;
		break;
	case -LKL_EDQUOT:
		errno = EDQUOT;
		break;
	case -LKL_ENOMEDIUM:
		errno = ENOMEDIUM;
		break;
	case -LKL_EMEDIUMTYPE:
		errno = EMEDIUMTYPE;
		break;
	case -LKL_ECANCELED:
		errno = ECANCELED;
		break;
	case -LKL_ENOKEY:
		errno = ENOKEY;
		break;
	case -LKL_EKEYEXPIRED:
		errno = EKEYEXPIRED;
		break;
	case -LKL_EKEYREVOKED:
		errno = EKEYREVOKED;
		break;
	case -LKL_EKEYREJECTED:
		errno = EKEYREJECTED;
		break;
	case -LKL_EOWNERDEAD:
		errno = EOWNERDEAD;
		break;
	case -LKL_ENOTRECOVERABLE:
		errno = ENOTRECOVERABLE;
		break;
	case -LKL_ERFKILL:
		errno = ERFKILL;
		break;
	case -LKL_EHWPOISON:
		errno = EHWPOISON;
		break;
	}

	return -1;
}

int lkl_soname_xlate(int soname)
{
	switch (soname) {
	case SO_DEBUG:
		return LKL_SO_DEBUG;
	case SO_REUSEADDR:
		return LKL_SO_REUSEADDR;
	case SO_TYPE:
		return LKL_SO_TYPE;
	case SO_ERROR:
		return LKL_SO_ERROR;
	case SO_DONTROUTE:
		return LKL_SO_DONTROUTE;
	case SO_BROADCAST:
		return LKL_SO_BROADCAST;
	case SO_SNDBUF:
		return LKL_SO_SNDBUF;
	case SO_RCVBUF:
		return LKL_SO_RCVBUF;
	case SO_SNDBUFFORCE:
		return LKL_SO_SNDBUFFORCE;
	case SO_RCVBUFFORCE:
		return LKL_SO_RCVBUFFORCE;
	case SO_KEEPALIVE:
		return LKL_SO_KEEPALIVE;
	case SO_OOBINLINE:
		return LKL_SO_OOBINLINE;
	case SO_NO_CHECK:
		return LKL_SO_NO_CHECK;
	case SO_PRIORITY:
		return LKL_SO_PRIORITY;
	case SO_LINGER:
		return LKL_SO_LINGER;
	case SO_BSDCOMPAT:
		return LKL_SO_BSDCOMPAT;
#ifdef SO_REUSEPORT
	case SO_REUSEPORT:
		return LKL_SO_REUSEPORT;
#endif
	case SO_PASSCRED:
		return LKL_SO_PASSCRED;
	case SO_PEERCRED:
		return LKL_SO_PEERCRED;
	case SO_RCVLOWAT:
		return LKL_SO_RCVLOWAT;
	case SO_SNDLOWAT:
		return LKL_SO_SNDLOWAT;
	case SO_RCVTIMEO:
		return LKL_SO_RCVTIMEO;
	case SO_SNDTIMEO:
		return LKL_SO_SNDTIMEO;
	case SO_SECURITY_AUTHENTICATION:
		return LKL_SO_SECURITY_AUTHENTICATION;
	case SO_SECURITY_ENCRYPTION_TRANSPORT:
		return LKL_SO_SECURITY_ENCRYPTION_TRANSPORT;
	case SO_SECURITY_ENCRYPTION_NETWORK:
		return LKL_SO_SECURITY_ENCRYPTION_NETWORK;
	case SO_BINDTODEVICE:
		return LKL_SO_BINDTODEVICE;
	case SO_ATTACH_FILTER:
		return LKL_SO_ATTACH_FILTER;
	case SO_DETACH_FILTER:
		return LKL_SO_DETACH_FILTER;
	case SO_PEERNAME:
		return LKL_SO_PEERNAME;
	case SO_TIMESTAMP:
		return LKL_SO_TIMESTAMP;
	case SO_ACCEPTCONN:
		return LKL_SO_ACCEPTCONN;
	case SO_PEERSEC:
		return LKL_SO_PEERSEC;
	case SO_PASSSEC:
		return LKL_SO_PASSSEC;
	case SO_TIMESTAMPNS:
		return LKL_SO_TIMESTAMPNS;
	case SO_MARK:
		return LKL_SO_MARK;
	case SO_TIMESTAMPING :
		return LKL_SO_TIMESTAMPING;
	case SO_PROTOCOL:
		return LKL_SO_PROTOCOL;
	case SO_DOMAIN:
		return LKL_SO_DOMAIN;
	case SO_RXQ_OVFL:
		return LKL_SO_RXQ_OVFL;
#ifdef SO_WIFI_STATUS
	case SO_WIFI_STATUS:
		return LKL_SO_WIFI_STATUS;
#endif
#ifdef SO_PEEK_OFF
	case SO_PEEK_OFF:
		return LKL_SO_PEEK_OFF;
#endif
#ifdef SO_NOFCS
	case SO_NOFCS:
		return LKL_SO_NOFCS;
#endif
#ifdef SO_LOCK_FILTER
	case SO_LOCK_FILTER:
		return LKL_SO_LOCK_FILTER;
#endif
#ifdef SO_SELECT_ERR_QUEUE
	case SO_SELECT_ERR_QUEUE:
		return LKL_SO_SELECT_ERR_QUEUE;
#endif
#ifdef SO_BUSY_POLL
	case SO_BUSY_POLL:
		return LKL_SO_BUSY_POLL;
#endif
#ifdef SO_MAX_PACING_RATE
	case SO_MAX_PACING_RATE:
		return LKL_SO_MAX_PACING_RATE;
#endif
	}

	return soname;
}

int lkl_solevel_xlate(int solevel)
{
	switch (solevel) {
	case SOL_SOCKET:
		return LKL_SOL_SOCKET;
	}

	return solevel;
}

unsigned long lkl_ioctl_req_xlate(unsigned long req)
{
	switch (req) {
	case FIOSETOWN:
		return LKL_FIOSETOWN;
	case SIOCSPGRP:
		return LKL_SIOCSPGRP;
	case FIOGETOWN:
		return LKL_FIOGETOWN;
	case SIOCGPGRP:
		return LKL_SIOCGPGRP;
	case SIOCATMARK:
		return LKL_SIOCATMARK;
	case SIOCGSTAMP:
		return LKL_SIOCGSTAMP;
	case SIOCGSTAMPNS:
		return LKL_SIOCGSTAMPNS;
	}

	/* TODO: asm/termios.h translations */

	return req;
}

int lkl_fcntl_cmd_xlate(int cmd)
{
	switch (cmd) {
	case F_DUPFD:
		return LKL_F_DUPFD;
	case F_GETFD:
		return LKL_F_GETFD;
	case F_SETFD:
		return LKL_F_SETFD;
	case F_GETFL:
		return LKL_F_GETFL;
	case F_SETFL:
		return LKL_F_SETFL;
	case F_GETLK:
		return LKL_F_GETLK;
	case F_SETLK:
		return LKL_F_SETLK;
	case F_SETLKW:
		return LKL_F_SETLKW;
	case F_SETOWN:
		return LKL_F_SETOWN;
	case F_GETOWN:
		return LKL_F_GETOWN;
	case F_SETSIG:
		return LKL_F_SETSIG;
	case F_GETSIG:
		return LKL_F_GETSIG;
#ifndef LKL_CONFIG_64BIT
	case F_GETLK64:
		return LKL_F_GETLK64;
	case F_SETLK64:
		return LKL_F_SETLK64;
	case F_SETLKW64:
		return LKL_F_SETLKW64;
#endif
	case F_SETOWN_EX:
		return LKL_F_SETOWN_EX;
	case F_GETOWN_EX:
		return LKL_F_GETOWN_EX;
	}

	return cmd;
}

