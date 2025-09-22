/*	$OpenBSD: log.h,v 1.3 2021/12/13 18:28:40 deraadt Exp $	*/

#ifndef _LOG_H
#define	_LOG_H	1

#include <stdarg.h>	/* for va_list */

extern int log_debug_use_syslog;

__BEGIN_DECLS
void		 log_init (int);
void		 logit(int, const char *, ...)
		    __attribute__((__format__ (__syslog__, 2, 3)));
void		 vlog(int, const char *, va_list)
		    __attribute__((__format__ (__syslog__, 2, 0)));
void		 log_warn(const char *, ...)
		    __attribute__((__format__ (printf, 1, 2)));
void		 log_warnx(const char *, ...)
		    __attribute__((__format__ (printf, 1, 2)));
void		 log_info(const char *, ...)
		    __attribute__((__format__ (printf, 1, 2)));
void		 log_debug(const char *, ...)
		    __attribute__((__format__ (printf, 1, 2)));
__dead void	 fatal(const char *);
__dead void	 fatalx(const char *);
__END_DECLS

#endif
