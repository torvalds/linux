/// Make sure of_device_id tables are NULL terminated
//
// Keywords: of_table
// Confidence: Medium
// Options: --include-headers

virtual patch
virtual context
virtual org
virtual report

@depends on context@
identifier var, arr;
expression E;
@@
struct of_device_id arr[] = {
	...,
	{
	.var = E,
*	}
};

@depends on patch@
identifier var, arr;
expression E;
@@
struct of_device_id arr[] = {
	...,
	{
	.var = E,
-	}
+	},
+	{ }
};

@r depends on org || report@
position p1;
identifier var, arr;
expression E;
@@
struct of_device_id arr[] = {
	...,
	{
	.var = E,
	}
	@p1
};

@script:python depends on org@
p1 << r.p1;
arr << r.arr;
@@

cocci.print_main(arr,p1)

@script:python depends on report@
p1 << r.p1;
arr << r.arr;
@@

msg = "%s is not NULL terminated at line %s" % (arr, p1[0].line)
coccilib.report.print_report(p1[0],msg)
