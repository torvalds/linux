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
#include "xfs_log_format.h"
#include "xfs_log.h"
#include "xfs_log_priv.h"

struct xfs_sysfs_attr {
	struct attribute attr;
	ssize_t (*show)(char *buf, void *data);
	ssize_t (*store)(const char *buf, size_t count, void *data);
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

#define ATTR_LIST(name) &xfs_sysfs_attr_##name.attr

/*
 * xfs_mount kobject. This currently has no attributes and thus no need for show
 * and store helpers. The mp kobject serves as the per-mount parent object that
 * is identified by the fsname under sysfs.
 */

struct kobj_type xfs_mp_ktype = {
	.release = xfs_sysfs_release,
};

#ifdef DEBUG
/* debug */

STATIC ssize_t
log_recovery_delay_store(
	const char	*buf,
	size_t		count,
	void		*data)
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
	char	*buf,
	void	*data)
{
	return snprintf(buf, PAGE_SIZE, "%d\n", xfs_globals.log_recovery_delay);
}
XFS_SYSFS_ATTR_RW(log_recovery_delay);

static struct attribute *xfs_dbg_attrs[] = {
	ATTR_LIST(log_recovery_delay),
	NULL,
};

STATIC ssize_t
xfs_dbg_show(
	struct kobject		*kobject,
	struct attribute	*attr,
	char			*buf)
{
	struct xfs_sysfs_attr *xfs_attr = to_attr(attr);

	return xfs_attr->show ? xfs_attr->show(buf, NULL) : 0;
}

STATIC ssize_t
xfs_dbg_store(
	struct kobject		*kobject,
	struct attribute	*attr,
	const char		*buf,
	size_t			count)
{
	struct xfs_sysfs_attr *xfs_attr = to_attr(attr);

	return xfs_attr->store ? xfs_attr->store(buf, count, NULL) : 0;
}

static struct sysfs_ops xfs_dbg_ops = {
	.show = xfs_dbg_show,
	.store = xfs_dbg_store,
};

struct kobj_type xfs_dbg_ktype = {
	.release = xfs_sysfs_release,
	.sysfs_ops = &xfs_dbg_ops,
	.default_attrs = xfs_dbg_attrs,
};

#endif /* DEBUG */

/* xlog */

STATIC ssize_t
log_head_lsn_show(
	char	*buf,
	void	*data)
{
	struct xlog *log = data;
	int cycle;
	int block;

	spin_lock(&log->l_icloglock);
	cycle = log->l_curr_cycle;
	block = log->l_curr_block;
	spin_unlock(&log->l_icloglock);

	return snprintf(buf, PAGE_SIZE, "%d:%d\n", cycle, block);
}
XFS_SYSFS_ATTR_RO(log_head_lsn);

STATIC ssize_t
log_tail_lsn_show(
	char	*buf,
	void	*data)
{
	struct xlog *log = data;
	int cycle;
	int block;

	xlog_crack_atomic_lsn(&log->l_tail_lsn, &cycle, &block);
	return snprintf(buf, PAGE_SIZE, "%d:%d\n", cycle, block);
}
XFS_SYSFS_ATTR_RO(log_tail_lsn);

STATIC ssize_t
reserve_grant_head_show(
	char	*buf,
	void	*data)
{
	struct xlog *log = data;
	int cycle;
	int bytes;

	xlog_crack_grant_head(&log->l_reserve_head.grant, &cycle, &bytes);
	return snprintf(buf, PAGE_SIZE, "%d:%d\n", cycle, bytes);
}
XFS_SYSFS_ATTR_RO(reserve_grant_head);

STATIC ssize_t
write_grant_head_show(
	char	*buf,
	void	*data)
{
	struct xlog *log = data;
	int cycle;
	int bytes;

	xlog_crack_grant_head(&log->l_write_head.grant, &cycle, &bytes);
	return snprintf(buf, PAGE_SIZE, "%d:%d\n", cycle, bytes);
}
XFS_SYSFS_ATTR_RO(write_grant_head);

static struct attribute *xfs_log_attrs[] = {
	ATTR_LIST(log_head_lsn),
	ATTR_LIST(log_tail_lsn),
	ATTR_LIST(reserve_grant_head),
	ATTR_LIST(write_grant_head),
	NULL,
};

static inline struct xlog *
to_xlog(struct kobject *kobject)
{
	struct xfs_kobj *kobj = to_kobj(kobject);
	return container_of(kobj, struct xlog, l_kobj);
}

STATIC ssize_t
xfs_log_show(
	struct kobject		*kobject,
	struct attribute	*attr,
	char			*buf)
{
	struct xlog *log = to_xlog(kobject);
	struct xfs_sysfs_attr *xfs_attr = to_attr(attr);

	return xfs_attr->show ? xfs_attr->show(buf, log) : 0;
}

STATIC ssize_t
xfs_log_store(
	struct kobject		*kobject,
	struct attribute	*attr,
	const char		*buf,
	size_t			count)
{
	struct xlog *log = to_xlog(kobject);
	struct xfs_sysfs_attr *xfs_attr = to_attr(attr);

	return xfs_attr->store ? xfs_attr->store(buf, count, log) : 0;
}

static struct sysfs_ops xfs_log_ops = {
	.show = xfs_log_show,
	.store = xfs_log_store,
};

struct kobj_type xfs_log_ktype = {
	.release = xfs_sysfs_release,
	.sysfs_ops = &xfs_log_ops,
	.default_attrs = xfs_log_attrs,
};
