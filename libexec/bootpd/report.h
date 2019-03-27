/* report.h */
/* $FreeBSD$ */

extern void report_init(int nolog);
extern void report(int, const char *, ...) __printflike(2, 3);
extern const char *get_errmsg(void);
