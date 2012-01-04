#ifndef __XFS_MESSAGE_H
#define __XFS_MESSAGE_H 1

struct xfs_mount;

extern __printf(2, 3)
void xfs_emerg(const struct xfs_mount *mp, const char *fmt, ...);
extern __printf(2, 3)
void xfs_alert(const struct xfs_mount *mp, const char *fmt, ...);
extern __printf(3, 4)
void xfs_alert_tag(const struct xfs_mount *mp, int tag, const char *fmt, ...);
extern __printf(2, 3)
void xfs_crit(const struct xfs_mount *mp, const char *fmt, ...);
extern __printf(2, 3)
void xfs_err(const struct xfs_mount *mp, const char *fmt, ...);
extern __printf(2, 3)
void xfs_warn(const struct xfs_mount *mp, const char *fmt, ...);
extern __printf(2, 3)
void xfs_notice(const struct xfs_mount *mp, const char *fmt, ...);
extern __printf(2, 3)
void xfs_info(const struct xfs_mount *mp, const char *fmt, ...);

#ifdef DEBUG
extern __printf(2, 3)
void xfs_debug(const struct xfs_mount *mp, const char *fmt, ...);
#else
static inline __printf(2, 3)
void xfs_debug(const struct xfs_mount *mp, const char *fmt, ...)
{
}
#endif

extern void assfail(char *expr, char *f, int l);

extern void xfs_hex_dump(void *p, int length);

#endif	/* __XFS_MESSAGE_H */
