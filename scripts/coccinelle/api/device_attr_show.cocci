// SPDX-License-Identifier: GPL-2.0-only
///
/// From Documentation/filesystems/sysfs.rst:
///  show() should only use sysfs_emit() or sysfs_emit_at() when formatting
///  the value to be returned to user space.
///
// Confidence: High
// Copyright: (C) 2020 Denis Efremov ISPRAS
// Options: --no-includes --include-headers
//

virtual report
virtual org
virtual context
virtual patch

@r depends on !patch@
identifier show, dev, attr, buf;
position p;
@@

ssize_t show(struct device *dev, struct device_attribute *attr, char *buf)
{
	<...
*	return snprintf@p(...);
	...>
}

@rp depends on patch@
identifier show, dev, attr, buf;
expression BUF, SZ, FORMAT, STR;
@@

ssize_t show(struct device *dev, struct device_attribute *attr, char *buf)
{
	<...
	return
-		snprintf(BUF, SZ, FORMAT
+		sysfs_emit(BUF, FORMAT
				,...);
	...>
}

@script: python depends on report@
p << r.p;
@@

coccilib.report.print_report(p[0], "WARNING: please use sysfs_emit or sysfs_emit_at")

@script: python depends on org@
p << r.p;
@@

coccilib.org.print_todo(p[0], "WARNING: please use sysfs_emit or sysfs_emit_at")
