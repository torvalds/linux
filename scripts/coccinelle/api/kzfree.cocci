// SPDX-License-Identifier: GPL-2.0-only
///
/// Use kzfree, kvfree_sensitive rather than memset or
/// memzero_explicit followed by kfree
///
// Confidence: High
// Copyright: (C) 2020 Denis Efremov ISPRAS
// Options: --no-includes --include-headers
//
// Keywords: kzfree, kvfree_sensitive
//

virtual context
virtual patch
virtual org
virtual report

@initialize:python@
@@
# kmalloc_oob_in_memset uses memset to explicitly trigger out-of-bounds access
filter = frozenset(['kmalloc_oob_in_memset', 'kzfree', 'kvfree_sensitive'])

def relevant(p):
    return not (filter & {el.current_element for el in p})

@cond@
position ok;
@@

if (...)
  \(memset@ok\|memzero_explicit@ok\)(...);

@r depends on !patch forall@
expression E;
position p : script:python() { relevant(p) };
position m != cond.ok;
type T;
@@

(
* memset@m((T)E, 0, ...);
|
* memzero_explicit@m((T)E, ...);
)
  ... when != E
      when strict
* \(kfree\|vfree\|kvfree\)(E)@p;

@rp_memzero depends on patch@
expression E, size;
position p : script:python() { relevant(p) };
position m != cond.ok;
type T;
@@

- memzero_explicit@m((T)E, size);
  ... when != E
      when strict
// TODO: uncomment when kfree_sensitive will be merged.
// Only this case is commented out because developers
// may not like patches like this since kzfree uses memset
// internally (not memzero_explicit).
//(
//- kfree(E)@p;
//+ kfree_sensitive(E);
//|
- \(vfree\|kvfree\)(E)@p;
+ kvfree_sensitive(E, size);
//)

@rp_memset depends on patch@
expression E, size;
position p : script:python() { relevant(p) };
position m != cond.ok;
type T;
@@

- memset@m((T)E, 0, size);
  ... when != E
      when strict
(
- kfree(E)@p;
+ kzfree(E);
|
- \(vfree\|kvfree\)(E)@p;
+ kvfree_sensitive(E, size);
)

@script:python depends on report@
p << r.p;
@@

coccilib.report.print_report(p[0],
  "WARNING: opportunity for kzfree/kvfree_sensitive")

@script:python depends on org@
p << r.p;
@@

coccilib.org.print_todo(p[0],
  "WARNING: opportunity for kzfree/kvfree_sensitive")
