// SPDX-License-Identifier: GPL-2.0-only
/// Make sure pm_runtime_* calls do not unnecessarily use IS_ERR_VALUE
///
// Keywords: pm_runtime
// Confidence: Medium
// Copyright (C) 2013 Texas Instruments Incorporated -
// URL: https://coccinelle.gitlabpages.inria.fr/website
// Options: --include-headers

virtual patch
virtual context
virtual org
virtual report

//----------------------------------------------------------
//  Detection
//----------------------------------------------------------

@runtime_bad_err_handle exists@
expression ret;
position p;
@@
(
ret@p = \(pm_runtime_idle\|
	pm_runtime_suspend\|
	pm_runtime_autosuspend\|
	pm_runtime_resume\|
	pm_request_idle\|
	pm_request_resume\|
	pm_request_autosuspend\|
	pm_runtime_get\|
	pm_runtime_get_sync\|
	pm_runtime_put\|
	pm_runtime_put_autosuspend\|
	pm_runtime_put_sync\|
	pm_runtime_put_sync_suspend\|
	pm_runtime_put_sync_autosuspend\|
	pm_runtime_set_active\|
	pm_schedule_suspend\|
	pm_runtime_barrier\|
	pm_generic_runtime_suspend\|
	pm_generic_runtime_resume\)(...);
...
IS_ERR_VALUE(ret)
...
)

//----------------------------------------------------------
//  For context mode
//----------------------------------------------------------

@depends on context@
identifier pm_runtime_api;
expression ret;
position runtime_bad_err_handle.p;
@@
(
ret@p = pm_runtime_api(...);
...
* IS_ERR_VALUE(ret)
...
)

//----------------------------------------------------------
//  For patch mode
//----------------------------------------------------------

@depends on patch@
identifier pm_runtime_api;
expression ret;
position runtime_bad_err_handle.p;
@@
(
ret@p = pm_runtime_api(...);
...
- IS_ERR_VALUE(ret)
+ ret < 0
...
)

//----------------------------------------------------------
//  For org and report mode
//----------------------------------------------------------

@r depends on (org || report) exists@
position p1, p2;
identifier pm_runtime_api;
expression ret;
position runtime_bad_err_handle.p;
@@
(
ret@p = pm_runtime_api@p1(...);
...
IS_ERR_VALUE@p2(ret)
...
)

@script:python depends on org@
p1 << r.p1;
p2 << r.p2;
pm_runtime_api << r.pm_runtime_api;
@@

cocci.print_main(pm_runtime_api,p1)
cocci.print_secs("IS_ERR_VALUE",p2)

@script:python depends on report@
p1 << r.p1;
p2 << r.p2;
pm_runtime_api << r.pm_runtime_api;
@@

msg = "%s returns < 0 as error. Unecessary IS_ERR_VALUE at line %s" % (pm_runtime_api, p2[0].line)
coccilib.report.print_report(p1[0],msg)
