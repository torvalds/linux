virtual patch
virtual report

@depends on patch@
expression base, dev, res;
@@

-base = devm_request_and_ioremap(dev, res);
+base = devm_ioremap_resource(dev, res);
 ...
 if (
-base == NULL
+IS_ERR(base)
 || ...) {
<...
-	return ...;
+	return PTR_ERR(base);
...>
 }

@depends on patch@
expression e, E, ret;
identifier l;
@@

 e = devm_ioremap_resource(...);
 ...
 if (IS_ERR(e) || ...) {
 	... when any
-	ret = E;
+	ret = PTR_ERR(e);
 	...
(
 	return ret;
|
 	goto l;
)
 }

@depends on patch@
expression e;
@@

 e = devm_ioremap_resource(...);
 ...
 if (IS_ERR(e) || ...) {
 	...
-	\(dev_dbg\|dev_err\|pr_debug\|pr_err\|DRM_ERROR\)(...);
 	...
 }

@depends on patch@
expression e;
identifier l;
@@

 e = devm_ioremap_resource(...);
 ...
 if (IS_ERR(e) || ...)
-{
(
 	return ...;
|
 	goto l;
)
-}

@r depends on report@
expression e;
identifier l;
position p1;
@@

*e = devm_request_and_ioremap@p1(...);
 ...
 if (e == NULL || ...) {
 	...
(
 	return ...;
|
 	goto l;
)
 }

@script:python depends on r@
p1 << r.p1;
@@

msg = "ERROR: deprecated devm_request_and_ioremap() API used on line %s" % (p1[0].line)
coccilib.report.print_report(p1[0], msg)
