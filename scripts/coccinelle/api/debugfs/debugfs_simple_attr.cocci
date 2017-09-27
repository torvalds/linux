/// Use DEFINE_DEBUGFS_ATTRIBUTE rather than DEFINE_SIMPLE_ATTRIBUTE
/// for debugfs files.
///
//# Rationale: DEFINE_SIMPLE_ATTRIBUTE + debugfs_create_file()
//# imposes some significant overhead as compared to
//# DEFINE_DEBUGFS_ATTRIBUTE + debugfs_create_file_unsafe().
//
// Copyright (C): 2016 Nicolai Stange
// Options: --no-includes
//

virtual context
virtual patch
virtual org
virtual report

@dsa@
declarer name DEFINE_SIMPLE_ATTRIBUTE;
identifier dsa_fops;
expression dsa_get, dsa_set, dsa_fmt;
position p;
@@
DEFINE_SIMPLE_ATTRIBUTE@p(dsa_fops, dsa_get, dsa_set, dsa_fmt);

@dcf@
expression name, mode, parent, data;
identifier dsa.dsa_fops;
@@
debugfs_create_file(name, mode, parent, data, &dsa_fops)


@context_dsa depends on context && dcf@
declarer name DEFINE_DEBUGFS_ATTRIBUTE;
identifier dsa.dsa_fops;
expression dsa.dsa_get, dsa.dsa_set, dsa.dsa_fmt;
@@
* DEFINE_SIMPLE_ATTRIBUTE(dsa_fops, dsa_get, dsa_set, dsa_fmt);


@patch_dcf depends on patch expression@
expression name, mode, parent, data;
identifier dsa.dsa_fops;
@@
- debugfs_create_file(name, mode, parent, data, &dsa_fops)
+ debugfs_create_file_unsafe(name, mode, parent, data, &dsa_fops)

@patch_dsa depends on patch_dcf && patch@
identifier dsa.dsa_fops;
expression dsa.dsa_get, dsa.dsa_set, dsa.dsa_fmt;
@@
- DEFINE_SIMPLE_ATTRIBUTE(dsa_fops, dsa_get, dsa_set, dsa_fmt);
+ DEFINE_DEBUGFS_ATTRIBUTE(dsa_fops, dsa_get, dsa_set, dsa_fmt);


@script:python depends on org && dcf@
fops << dsa.dsa_fops;
p << dsa.p;
@@
msg="%s should be defined with DEFINE_DEBUGFS_ATTRIBUTE" % (fops)
coccilib.org.print_todo(p[0], msg)

@script:python depends on report && dcf@
fops << dsa.dsa_fops;
p << dsa.p;
@@
msg="WARNING: %s should be defined with DEFINE_DEBUGFS_ATTRIBUTE" % (fops)
coccilib.report.print_report(p[0], msg)
