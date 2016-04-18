/*
 * Copyright (c) 2014 Red Hat, Inc.
 * All Rights Reserved.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it would be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write the Free Software Foundation,
 * Inc.,  51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 */

#include "xfs.h"
#include "xfs_sysfs.h"
#include "xfs_format.h"
#include "xfs_log_format.h"
#include "xfs_trans_resv.h"
#include "xfs_log.h"
#include "xfs_log_priv.h"
#include "xfs_stats.h"
#include "xfs_mount.h"

struct xfs_sysfs_attr {
	struct attribute attr;
	ssize_t (*show)(struct kobject *kobject, char *buf);
	ssize_t (*store)(struct kobject *kobject, const char *buf,
			 size_t count);
};

static inline struct xfs_sysfs_attr *
to_attr(struct attribute *attr)
{
	return container_of(attr, struct xfs_sysfs_attr, attr);
}

#define XFS_SYSFS_ATTR_RW(name) \
	static struct xfs_sysfs_attr xfs_sysfs_attr_##name = __ATTR_RW(name)
#define XFS_SYSFS_ATTR_RO(name) \
	static struct xfs_sysfs_attr xfs_sysfs_attr_##name = __ATTR_RO(name)
#define XFS_SYSFS_ATTR_WO(name) \
	static struct xfs_sysfs_attr xfs_sysfs_attr_##name = __ATTR_WO(name)

#define ATTR_LIST(name) &xfs_sysfs_attr_##name.attr

STATIC ssize_t
xfs_sysfs_object_show(
	struct kobject		*kobject,
	struct attribute	*attr,
	char			*buf)
{
	struct xfs_sysfs_attr *xfs_attr = to_attr(attr);

	return xfs_attr->show ? xfs_attr->show(kobject, buf) : 0;
}

STATIC ssize_t
xfs_sysfs_object_store(
	struct kobject		*kobject,
	struct attribute	*attr,
	const char		*buf,
	size_t			count)
{
	struct xfs_sysfs_attr *xfs_attr = to_attr(attr);

	return xfs_attr->store ? xfs_attr->store(kobject, buf, count) : 0;
}

static const struct sysfs_ops xfs_sysfs_ops = {
	.show = xfs_sysfs_object_show,
	.store = xfs_sysfs_object_store,
};

/*
 * xfs_mount kobject. The mp kobject also serves as the per-mount parent object
 * that is identified by the fsname under sysfs.
 */

static inline struct xfs_mount *
to_mp(struct kobject *kobject)
{
	struct xfs_kobj *kobj = to_kobj(kobject);

	return container_of(kobj, struct xfs_mount, m_kobj);
}

#ifdef DEBUG

STATIC ssize_t
fail_writes_store(
	struct kobject		*kobject,
	const char		*buf,
	size_t			count)
{
	struct xfs_mount	*mp = to_mp(kobject);
	int			ret;
	int			val;

	ret = kstrtoint(buf, 0, &val);
	if (ret)
		return ret;

	if (val == 1)
		mp->m_fail_writes = true;
	else if (val == 0)
		mp->m_fail_writes = false;
	else
		return -EINVAL;

	return count;
}

STATIC ssize_t
fail_writes_show(
	struct kobject		*kobject,
	char			*buf)
{
	struct xfs_mount	*mp = to_mp(kobject);

	return snprintf(buf, PAGE_SIZE, "%d\n", mp->m_fail_writes ? 1 : 0);
}
XFS_SYSFS_ATTR_RW(fail_writes);

#endif /* DEBUG */

static struct attribute *xfs_mp_attrs[] = {
#ifdef DEBUG
	ATTR_LIST(fail_writes),
#endif
	NULL,
};

struct kobj_type xfs_mp_ktype = {
	.release = xfs_sysfs_release,
	.sysfs_ops = &xfs_sysfs_ops,
	.default_attrs = xfs_mp_attrs,
};

#ifdef DEBUG
/* debug */

STATIC ssize_t
log_recovery_delay_store(
	struct kobject	*kobject,
	const char	*buf,
	size_t		count)
{
	int		ret;
	int		val;

	ret = kstrtoint(buf, 0, &val);
	if (ret)
		return ret;

	if (val < 0 || val > 60)
		return -EINVAL;

	xfs_globals.log_recovery_delay = val;

	return count;
}

STATIC ssize_t
log_recovery_delay_show(
	struct kobject	*kobject,
	char		*buf)
{
	return snprintf(buf, PAGE_SIZE, "%d\n", xfs_globals.log_recovery_delay);
}
XFS_SYSFS_ATTR_RW(log_recovery_delay);

static struct attribute *xfs_dbg_attrs[] = {
	ATTR_LIST(log_recovery_delay),
	NULL,
};

struct kobj_type xfs_dbg_ktype = {
	.release = xfs_sysfs_release,
	.sysfs_ops = &xfs_sysfs_ops,
	.default_attrs = xfs_dbg_attrs,
};

#endif /* DEBUG */

/* stats */

static inline struct xstats *
to_xstats(struct kobject *kobject)
{
	struct xfs_kobj *kobj = to_kobj(kobject);

	return container_of(kobj, struct xstats, xs_kobj);
}

STATIC ssize_t
stats_show(
	struct kobject	*kobject,
	char		*buf)
{
	struct xstats	*stats = to_xstats(kobject);

	return xfs_stats_format(stats->xs_stats, buf);
}
XFS_SYSFS_ATTR_RO(stats);

STATIC ssize_t
stats_clear_store(
	struct kobject	*kobject,
	const char	*buf,
	size_t		count)
{
	int		ret;
	int		val;
	struct xstats	*stats = to_xstats(kobject);

	ret = kstrtoint(buf, 0, &val);
	if (ret)
		return ret;

	if (val != 1)
		return -EINVAL;

	xfs_stats_clearall(stats->xs_stats);
	return count;
}
XFS_SYSFS_ATTR_WO(stats_clear);

static struct attribute *xfs_stats_attrs[] = {
	ATTR_LIST(stats),
	ATTR_LIST(stats_clear),
	NULL,
};

struct kobj_type xfs_stats_ktype = {
	.release = xfs_sysfs_release,
	.sysfs_ops = &xfs_sysfs_ops,
	.default_attrs = xfs_stats_attrs,
};

/* xlog */

static inline struct xlog *
to_xlog(struct kobject *kobject)
{
	struct xfs_kobj *kobj = to_kobj(kobject);

	return container_of(kobj, struct xlog, l_kobj);
}

STATIC ssize_t
log_head_lsn_show(
	struct kobject	*kobject,
	char		*buf)
{
	int cycle;
	int block;
	struct xlog *log = to_xlog(kobject);

	spin_lock(&log->l_icloglock);
	cycle = log->l_curr_cycle;
	block = log->l_curr_block;
	spin_unlock(&log->l_icloglock);

	return snprintf(buf, PAGE_SIZE, "%d:%d\n", cycle, block);
}
XFS_SYSFS_ATTR_RO(log_head_lsn);

STATIC ssize_t
log_tail_lsn_show(
	struct kobject	*kobject,
	char		*buf)
{
	int cycle;
	int block;
	struct xlog *log = to_xlog(kobject);

	xlog_crack_atomic_lsn(&log->l_tail_lsn, &cycle, &block);
	return snprintf(buf, PAGE_SIZE, "%d:%d\n", cycle, block);
}
XFS_SYSFS_ATTR_RO(log_tail_lsn);

STATIC ssize_t
reserve_grant_head_show(
	struct kobject	*kobject,
	char		*buf)

{
	int cycle;
	int bytes;
	struct xlog *log = to_xlog(kobject);

	xlog_crack_grant_head(&log->l_reserve_head.grant, &cycle, &bytes);
	return snprintf(buf, PAGE_SIZE, "%d:%d\n", cycle, bytes);
}
XFS_SYSFS_ATTR_RO(reserve_grant_head);

STATIC ssize_t
write_grant_head_show(
	struct kobject	*kobject,
	char		*buf)
{
	int cycle;
	int bytes;
	struct xlog *log = to_xlog(kobject);

	xlog_crack_grant_head(&log->l_write_head.grant, &cycle, &bytes);
	return snprintf(buf, PAGE_SIZE, "%d:%d\n", cycle, bytes);
}
XFS_SYSFS_ATTR_RO(write_grant_head);

#ifdef DEBUG
STATIC ssize_t
log_badcrc_factor_store(
	struct kobject	*kobject,
	const char	*buf,
	size_t		count)
{
	struct xlog	*log = to_xlog(kobject);
	int		ret;
	uint32_t	val;

	ret = kstrtouint(buf, 0, &val);
	if (ret)
		return ret;

	log->l_badcrc_factor = val;

	return count;
}

STATIC ssize_t
log_badcrc_factor_show(
	struct kobject	*kobject,
	char		*buf)
{
	struct xlog	*log = to_xlog(kobject);

	return snprintf(buf, PAGE_SIZE, "%d\n", log->l_badcrc_factor);
}

XFS_SYSFS_ATTR_RW(log_badcrc_factor);
#endif	/* DEBUG */

static struct attribute *xfs_log_attrs[] = {
	ATTR_LIST(log_head_lsn),
	ATTR_LIST(log_tail_lsn),
	ATTR_LIST(reserve_grant_head),
	ATTR_LIST(write_grant_head),
#ifdef DEBUG
	ATTR_LIST(log_badcrc_factor),
#endif
	NULL,
};

struct kobj_type xfs_log_ktype = {
	.release = xfs_sysfs_release,
	.sysfs_ops = &xfs_sysfs_ops,
	.default_attrs = xfs_log_attrs,
};
