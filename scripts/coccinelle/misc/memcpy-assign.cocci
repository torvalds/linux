//
// Replace memcpy with struct assignment.
//
// Confidence: High
// Copyright: (C) 2012 Peter Senna Tschudin, INRIA/LIP6.  GPLv2.
// URL: http://coccinelle.lip6.fr/
// Comments:
// Options: --no-includes --include-headers

virtual patch
virtual report
virtual context
virtual org

@r1 depends on !patch@
identifier struct_name;
struct struct_name to;
struct struct_name from;
struct struct_name *top;
struct struct_name *fromp;
position p;
@@
memcpy@p(\(&(to)\|top\), \(&(from)\|fromp\), \(sizeof(to)\|sizeof(from)\|sizeof(struct struct_name)\|sizeof(*top)\|sizeof(*fromp)\))

@script:python depends on report@
p << r1.p;
@@
coccilib.report.print_report(p[0],"Replace memcpy with struct assignment")

@depends on context@
position r1.p;
@@
*memcpy@p(...);

@script:python depends on org@
p << r1.p;
@@
cocci.print_main("Replace memcpy with struct assignment",p)

@depends on patch@
identifier struct_name;
struct struct_name to;
struct struct_name from;
@@
(
-memcpy(&(to), &(from), sizeof(to));
+to = from;
|
-memcpy(&(to), &(from), sizeof(from));
+to = from;
|
-memcpy(&(to), &(from), sizeof(struct struct_name));
+to = from;
)

@depends on patch@
identifier struct_name;
struct struct_name to;
struct struct_name *from;
@@
(
-memcpy(&(to), from, sizeof(to));
+to = *from;
|
-memcpy(&(to), from, sizeof(*from));
+to = *from;
|
-memcpy(&(to), from, sizeof(struct struct_name));
+to = *from;
)

@depends on patch@
identifier struct_name;
struct struct_name *to;
struct struct_name from;
@@
(
-memcpy(to, &(from), sizeof(*to));
+ *to = from;
|
-memcpy(to, &(from), sizeof(from));
+ *to = from;
|
-memcpy(to, &(from), sizeof(struct struct_name));
+ *to = from;
)

@depends on patch@
identifier struct_name;
struct struct_name *to;
struct struct_name *from;
@@
(
-memcpy(to, from, sizeof(*to));
+ *to = *from;
|
-memcpy(to, from, sizeof(*from));
+ *to = *from;
|
-memcpy(to, from, sizeof(struct struct_name));
+ *to = *from;
)

