// SPDX-License-Identifier: GPL-2.0-only
///
/// From Documentation/filesystems/sysfs.rst:
///  show() must not use snprintf() when formatting the value to be
///  returned to user space. If you can guarantee that an overflow
///  will never happen you can use sprintf() otherwise you must use
///  scnprintf().
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
@@

ssize_t show(struct device *dev, struct device_attribute *attr, char *buf)
{
	<...
	return
-		snprintf
+		scnprintf
			(...);
	...>
}

@script: python depends on report@
p << r.p;
@@

coccilib.report.print_report(p[0], "WARNING: use scnprintf or sprintf")

@script: python depends on org@
p << r.p;
@@

coccilib.org.print_todo(p[0], "WARNING: use scnprintf or sprintf")
