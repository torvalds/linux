/* $Id: pathnames.h.in,v 8.7 2012/04/23 08:34:52 zy Exp $ */
/* $FreeBSD$ */

/* Read standard system paths first. */
#include <paths.h>

#ifndef	_PATH_EXRC
#define	_PATH_EXRC	".exrc"
#endif

#ifndef	_PATH_MSGCAT
#define	_PATH_MSGCAT	"/usr/share/vi/catalog/"
#endif

#ifndef	_PATH_NEXRC
#define	_PATH_NEXRC	".nexrc"
#endif

#ifndef	_PATH_PRESERVE
#define	_PATH_PRESERVE	"/var/tmp/vi.recover/"
#endif

#ifndef	_PATH_SYSEXRC
#define	_PATH_SYSEXRC	"/etc/vi.exrc"
#endif

#ifndef	_PATH_TAGS
#define	_PATH_TAGS	"tags"
#endif
