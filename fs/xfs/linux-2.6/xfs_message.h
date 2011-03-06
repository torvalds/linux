#ifndef __XFS_MESSAGE_H
#define __XFS_MESSAGE_H 1

struct xfs_mount;

extern int xfs_printk(const char *level, const struct xfs_mount *mp,
                      const char *fmt, ...)
        __attribute__ ((format (printf, 3, 4)));
extern int xfs_emerg(const struct xfs_mount *mp, const char *fmt, ...)
        __attribute__ ((format (printf, 2, 3)));
extern int xfs_alert(const struct xfs_mount *mp, const char *fmt, ...)
        __attribute__ ((format (printf, 2, 3)));
extern int xfs_alert_tag(const struct xfs_mount *mp, int tag,
			 const char *fmt, ...)
        __attribute__ ((format (printf, 3, 4)));
extern int xfs_crit(const struct xfs_mount *mp, const char *fmt, ...)
        __attribute__ ((format (printf, 2, 3)));
extern int xfs_err(const struct xfs_mount *mp, const char *fmt, ...)
        __attribute__ ((format (printf, 2, 3)));
extern int xfs_warn(const struct xfs_mount *mp, const char *fmt, ...)
        __attribute__ ((format (printf, 2, 3)));
extern int xfs_notice(const struct xfs_mount *mp, const char *fmt, ...)
        __attribute__ ((format (printf, 2, 3)));
extern int xfs_info(const struct xfs_mount *mp, const char *fmt, ...)
        __attribute__ ((format (printf, 2, 3)));

#ifdef DEBUG
extern int xfs_debug(const struct xfs_mount *mp, const char *fmt, ...)
        __attribute__ ((format (printf, 2, 3)));
#else
#define xfs_debug(mp, fmt, ...)	(0)
#endif

extern void assfail(char *expr, char *f, int l);

extern void xfs_hex_dump(void *p, int length);

#endif	/* __XFS_MESSAGE_H */
