/*	$OpenBSD: imsg_subr.h,v 1.2 2021/12/13 18:28:40 deraadt Exp $	*/

#ifndef _IMSG_SUBR_H
#define _IMSG_SUBR_H

struct imsgbuf;

__BEGIN_DECLS

int	 imsg_sync_read(struct imsgbuf *, int);
int	 imsg_sync_flush(struct imsgbuf *, int);

__END_DECLS

#endif
