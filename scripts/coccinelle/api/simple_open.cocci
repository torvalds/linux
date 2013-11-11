/// This removes an open coded simple_open() function
/// and replaces file operations references to the function
/// with simple_open() instead.
///
// Confidence: High
// Comments:
// Options: -no_includes -include_headers

virtual patch
virtual report

@ open depends on patch @
identifier open_f != simple_open;
identifier i, f;
@@
-int open_f(struct inode *i, struct file *f)
-{
(
-if (i->i_private)
-f->private_data = i->i_private;
|
-f->private_data = i->i_private;
)
-return 0;
-}

@ has_open depends on open @
identifier fops;
identifier open.open_f;
@@
struct file_operations fops = {
...,
-.open = open_f,
+.open = simple_open,
...
};

@ openr depends on report @
identifier open_f != simple_open;
identifier i, f;
position p;
@@
int open_f@p(struct inode *i, struct file *f)
{
(
if (i->i_private)
f->private_data = i->i_private;
|
f->private_data = i->i_private;
)
return 0;
}

@ has_openr depends on openr @
identifier fops;
identifier openr.open_f;
position p;
@@
struct file_operations fops = {
...,
.open = open_f@p,
...
};

@script:python@
pf << openr.p;
ps << has_openr.p;
@@

coccilib.report.print_report(pf[0],"WARNING opportunity for simple_open, see also structure on line %s"%(ps[0].line))
