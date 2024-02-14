// SPDX-License-Identifier: GPL-2.0-only
/// Remove .owner field if calls are used which set it automatically
///
// Confidence: High
// Copyright: (C) 2014 Wolfram Sang.

virtual patch
virtual context
virtual org
virtual report

@match1@
declarer name module_i2c_driver;
declarer name module_platform_driver;
declarer name module_platform_driver_probe;
identifier __driver;
@@
(
	module_i2c_driver(__driver);
|
	module_platform_driver(__driver);
|
	module_platform_driver_probe(__driver, ...);
)

@fix1 depends on match1 && patch && !context && !org && !report@
identifier match1.__driver;
@@
	static struct platform_driver __driver = {
		.driver = {
-			.owner = THIS_MODULE,
		}
	};

@fix1_i2c depends on match1 && patch && !context && !org && !report@
identifier match1.__driver;
@@
	static struct i2c_driver __driver = {
		.driver = {
-			.owner = THIS_MODULE,
		}
	};

@match2@
identifier __driver;
@@
(
	platform_driver_register(&__driver)
|
	platform_driver_probe(&__driver, ...)
|
	platform_create_bundle(&__driver, ...)
|
	i2c_add_driver(&__driver)
)

@fix2 depends on match2 && patch && !context && !org && !report@
identifier match2.__driver;
@@
	static struct platform_driver __driver = {
		.driver = {
-			.owner = THIS_MODULE,
		}
	};

@fix2_i2c depends on match2 && patch && !context && !org && !report@
identifier match2.__driver;
@@
	static struct i2c_driver __driver = {
		.driver = {
-			.owner = THIS_MODULE,
		}
	};

// ----------------------------------------------------------------------------

@fix1_context depends on match1 && !patch && (context || org || report)@
identifier match1.__driver;
position j0;
@@

 	static struct platform_driver __driver = {
		.driver = {
*			.owner@j0 = THIS_MODULE,
		}
	};

@fix1_i2c_context depends on match1 && !patch && (context || org || report)@
identifier match1.__driver;
position j0;
@@

	static struct i2c_driver __driver = {
		.driver = {
*			.owner@j0 = THIS_MODULE,
		}
	};

@fix2_context depends on match2 && !patch && (context || org || report)@
identifier match2.__driver;
position j0;
@@

 	static struct platform_driver __driver = {
		.driver = {
*			.owner@j0 = THIS_MODULE,
		}
	};

@fix2_i2c_context depends on match2 && !patch && (context || org || report)@
identifier match2.__driver;
position j0;
@@

	static struct i2c_driver __driver = {
		.driver = {
*			.owner@j0 = THIS_MODULE,
		}
	};

// ----------------------------------------------------------------------------

@script:python fix1_org depends on org@
j0 << fix1_context.j0;
@@

msg = "No need to set .owner here. The core will do it."
coccilib.org.print_todo(j0[0], msg)

@script:python fix1_i2c_org depends on org@
j0 << fix1_i2c_context.j0;
@@

msg = "No need to set .owner here. The core will do it."
coccilib.org.print_todo(j0[0], msg)

@script:python fix2_org depends on org@
j0 << fix2_context.j0;
@@

msg = "No need to set .owner here. The core will do it."
coccilib.org.print_todo(j0[0], msg)

@script:python fix2_i2c_org depends on org@
j0 << fix2_i2c_context.j0;
@@

msg = "No need to set .owner here. The core will do it."
coccilib.org.print_todo(j0[0], msg)

// ----------------------------------------------------------------------------

@script:python fix1_report depends on report@
j0 << fix1_context.j0;
@@

msg = "No need to set .owner here. The core will do it."
coccilib.report.print_report(j0[0], msg)

@script:python fix1_i2c_report depends on report@
j0 << fix1_i2c_context.j0;
@@

msg = "No need to set .owner here. The core will do it."
coccilib.report.print_report(j0[0], msg)

@script:python fix2_report depends on report@
j0 << fix2_context.j0;
@@

msg = "No need to set .owner here. The core will do it."
coccilib.report.print_report(j0[0], msg)

@script:python fix2_i2c_report depends on report@
j0 << fix2_i2c_context.j0;
@@

msg = "No need to set .owner here. The core will do it."
coccilib.report.print_report(j0[0], msg)

