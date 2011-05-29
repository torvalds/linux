#ifndef __XFS_MESSAGE_H
#define __XFS_MESSAGE_H 1

struct xfs_mount;

extern void xfs_printk(const char *level, const struct xfs_mount *mp,
                      const char *fmt, ...)
        __attribute__ ((format (printf, 3, 4)));
extern void xfs_emerg(const struct xfs_mount *mp, const char *fmt, ...)
        __attribute__ ((format (printf, 2, 3)));
extern void xfs_alert(const struct xfs_mount *mp, const char *fmt, ...)
        __attribute__ ((format (printf, 2, 3)));
extern void xfs_alert_tag(const struct xfs_mount *mp, int tag,
			 const char *fmt, ...)
        __attribute__ ((format (printf, 3, 4)));
extern void xfs_crit(const struct xfs_mount *mp, const char *fmt, ...)
        __attribute__ ((format (printf, 2, 3)));
extern void xfs_err(const struct xfs_mount *mp, const char *fmt, ...)
        __attribute__ ((format (printf, 2, 3)));
extern void xfs_warn(const struct xfs_mount *mp, const char *fmt, ...)
        __attribute__ ((format (printf, 2, 3)));
extern void xfs_notice(const struct xfs_mount *mp, const char *fmt, ...)
        __attribute__ ((format (printf, 2, 3)));
extern void xfs_info(const struct xfs_mount *mp, const char *fmt, ...)
        __attribute__ ((format (printf, 2, 3)));

#ifdef DEBUG
extern void xfs_debug(const struct xfs_mount *mp, const char *fmt, ...)
        __attribute__ ((format (printf, 2, 3)));
#else
static inline void xfs_debug(const struct xfs_mount *mp, const char *fmt, ...)
{
}
#endif

extern void assfail(char *expr, char *f, int l);

extern void xfs_hex_dump(void *p, int length);

#endif	/* __XFS_MESSAGE_H */
