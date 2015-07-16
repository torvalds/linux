/*
 * linux/include/linux/nfsd/debug.h
 *
 * Debugging-related stuff for nfsd
 *
 * Copyright (C) 1995 Olaf Kirch <okir@monad.swb.de>
 */

#ifndef _UAPILINUX_NFSD_DEBUG_H
#define _UAPILINUX_NFSD_DEBUG_H

#include <linux/sunrpc/debug.h>

/*
 * knfsd debug flags
 */
#define NFSDDBG_SOCK		0x0001
#define NFSDDBG_FH		0x0002
#define NFSDDBG_EXPORT		0x0004
#define NFSDDBG_SVC		0x0008
#define NFSDDBG_PROC		0x0010
#define NFSDDBG_FILEOP		0x0020
#define NFSDDBG_AUTH		0x0040
#define NFSDDBG_REPCACHE	0x0080
#define NFSDDBG_XDR		0x0100
#define NFSDDBG_LOCKD		0x0200
#define NFSDDBG_PNFS		0x0400
#define NFSDDBG_ALL		0x7FFF
#define NFSDDBG_NOCHANGE	0xFFFF



#endif /* _UAPILINUX_NFSD_DEBUG_H */
