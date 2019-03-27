/*
 * $FreeBSD$
 */

#include <db.h>

#define	YPOP_CHANGE 1			/* change, do not add */
#define	YPOP_INSERT 2			/* add, do not change */
#define	YPOP_DELETE 3			/* delete this entry */
#define	YPOP_STORE  4			/* add, or change */

#define	ERR_ACCESS	1
#define	ERR_MALLOC	2
#define	ERR_READ	3
#define	ERR_WRITE	4
#define	ERR_DBASE	5
#define	ERR_KEY		6

#ifndef YPLIBDIR
#define YPLIBDIR "/usr/libexec/"
#endif

#ifndef MAP_UPPATE
#define MAP_UPDATE "ypupdate"
#endif

#define MAP_UPDATE_PATH YPLIBDIR MAP_UPDATE

extern int children;
extern void ypu_prog_1(struct svc_req *, register SVCXPRT *);
extern int localupdate(char *, char *, u_int, u_int, char *, u_int, char *);
extern int ypmap_update(char *, char *, u_int, u_int, char *, u_int, char *);
extern int yp_del_record(DB *, DBT *);
