/// NULL check before some freeing functions is not needed.
///
/// Based on checkpatch warning
/// "kfree(NULL) is safe this check is probably not required"
/// and kfreeaddr.cocci by Julia Lawall.
///
// Copyright: (C) 2014 Fabian Frederick.  GPLv2.
// Comments: -
// Options: --no-includes --include-headers

virtual patch
virtual org
virtual report
virtual context

@r2 depends on patch@
expression E;
@@
- if (E)
(
-	kfree(E);
+ kfree(E);
|
-	debugfs_remove(E);
+ debugfs_remove(E);
|
-	debugfs_remove_recursive(E);
+ debugfs_remove_recursive(E);
|
-	usb_free_urb(E);
+ usb_free_urb(E);
)

@r depends on context || report || org @
expression E;
position p;
@@

* if (E)
*	\(kfree@p\|debugfs_remove@p\|debugfs_remove_recursive@p\|usb_free_urb\)(E);

@script:python depends on org@
p << r.p;
@@

cocci.print_main("NULL check before that freeing function is not needed", p)

@script:python depends on report@
p << r.p;
@@

msg = "WARNING: NULL check before freeing functions like kfree, debugfs_remove, debugfs_remove_recursive or usb_free_urb is not needed. Maybe consider reorganizing relevant code to avoid passing NULL values."
coccilib.report.print_report(p[0], msg)
