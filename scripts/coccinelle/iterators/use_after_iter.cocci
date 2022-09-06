// SPDX-License-Identifier: GPL-2.0-only
/// If list_for_each_entry, etc complete a traversal of the list, the iterator
/// variable ends up pointing to an address at an offset from the list head,
/// and not a meaningful structure.  Thus this value should not be used after
/// the end of the iterator.
//#False positives arise when there is a goto in the iterator and the
//#reported reference is at the label of this goto.  Some flag tests
//#may also cause a report to be a false positive.
///
// Confidence: Moderate
// Copyright: (C) 2012 Julia Lawall, INRIA/LIP6.
// Copyright: (C) 2012 Gilles Muller, INRIA/LIP6.
// URL: https://coccinelle.gitlabpages.inria.fr/website
// Comments:
// Options: --no-includes --include-headers

virtual context
virtual org
virtual report

@r exists@
identifier c,member;
expression E,x;
iterator name list_for_each_entry;
iterator name list_for_each_entry_reverse;
iterator name list_for_each_entry_continue;
iterator name list_for_each_entry_continue_reverse;
iterator name list_for_each_entry_from;
iterator name list_for_each_entry_safe;
iterator name list_for_each_entry_safe_continue;
iterator name list_for_each_entry_safe_from;
iterator name list_for_each_entry_safe_reverse;
iterator name hlist_for_each_entry;
iterator name hlist_for_each_entry_continue;
iterator name hlist_for_each_entry_from;
iterator name hlist_for_each_entry_safe;
statement S;
position p1,p2;
type T;
@@

(
list_for_each_entry@p1(c,...,member) { ... when != break;
                                 when forall
                                 when strict
}
|
list_for_each_entry_reverse@p1(c,...,member) { ... when != break;
                                 when forall
                                 when strict
}
|
list_for_each_entry_continue@p1(c,...,member) { ... when != break;
                                 when forall
                                 when strict
}
|
list_for_each_entry_continue_reverse@p1(c,...,member) { ... when != break;
                                 when forall
                                 when strict
}
|
list_for_each_entry_from@p1(c,...,member) { ... when != break;
                                 when forall
                                 when strict
}
|
list_for_each_entry_safe@p1(c,...,member) { ... when != break;
                                 when forall
                                 when strict
}
|
list_for_each_entry_safe_continue@p1(c,...,member) { ... when != break;
                                 when forall
                                 when strict
}
|
list_for_each_entry_safe_from@p1(c,...,member) { ... when != break;
                                 when forall
                                 when strict
}
|
list_for_each_entry_safe_reverse@p1(c,...,member) { ... when != break;
                                 when forall
                                 when strict
}
)
...
(
list_for_each_entry(c,...) S
|
list_for_each_entry_reverse(c,...) S
|
list_for_each_entry_continue(c,...) S
|
list_for_each_entry_continue_reverse(c,...) S
|
list_for_each_entry_from(c,...) S
|
list_for_each_entry_safe(c,...) S
|
list_for_each_entry_safe(x,c,...) S
|
list_for_each_entry_safe_continue(c,...) S
|
list_for_each_entry_safe_continue(x,c,...) S
|
list_for_each_entry_safe_from(c,...) S
|
list_for_each_entry_safe_from(x,c,...) S
|
list_for_each_entry_safe_reverse(c,...) S
|
list_for_each_entry_safe_reverse(x,c,...) S
|
hlist_for_each_entry(c,...) S
|
hlist_for_each_entry_continue(c,...) S
|
hlist_for_each_entry_from(c,...) S
|
hlist_for_each_entry_safe(c,...) S
|
list_remove_head(x,c,...)
|
list_entry_is_head(c,...)
|
sizeof(<+...c...+>)
|
 &c->member
|
T c;
|
c = E
|
*c@p2
)

@script:python depends on org@
p1 << r.p1;
p2 << r.p2;
@@

cocci.print_main("invalid iterator index reference",p2)
cocci.print_secs("iterator",p1)

@script:python depends on report@
p1 << r.p1;
p2 << r.p2;
@@

msg = "ERROR: invalid reference to the index variable of the iterator on line %s" % (p1[0].line)
coccilib.report.print_report(p2[0], msg)
