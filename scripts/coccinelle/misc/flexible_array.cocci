// SPDX-License-Identifier: GPL-2.0-only
///
/// Zero-length and one-element arrays are deprecated, see
/// Documentation/process/deprecated.rst
/// Flexible-array members should be used instead.
///
//
// Confidence: High
// Copyright: (C) 2020 Denis Efremov ISPRAS.
// Comments:
// Options: --no-includes --include-headers

virtual context
virtual report
virtual org
virtual patch

@initialize:python@
@@
def relevant(positions):
    for p in positions:
        if "uapi" in p.file:
             return False
    return True

@r depends on !patch@
identifier name, array;
type T;
position p : script:python() { relevant(p) };
@@

(
  struct name {
    ...
*   T array@p[\(0\|1\)];
  };
|
  struct {
    ...
*   T array@p[\(0\|1\)];
  };
|
  union name {
    ...
*   T array@p[\(0\|1\)];
  };
|
  union {
    ...
*   T array@p[\(0\|1\)];
  };
)

@only_field depends on patch@
identifier name, array;
type T;
position q;
@@

(
  struct name {@q
    T array[0];
  };
|
  struct {@q
    T array[0];
  };
)

@depends on patch@
identifier name, array;
type T;
position p : script:python() { relevant(p) };
// position @q with rule "only_field" simplifies
// handling of bitfields, arrays, etc.
position q != only_field.q;
@@

(
  struct name {@q
    ...
    T array@p[
-       0
    ];
  };
|
  struct {@q
    ...
    T array@p[
-       0
    ];
  };
)

@script: python depends on report@
p << r.p;
@@

msg = "WARNING use flexible-array member instead (https://www.kernel.org/doc/html/latest/process/deprecated.html#zero-length-and-one-element-arrays)"
coccilib.report.print_report(p[0], msg)

@script: python depends on org@
p << r.p;
@@

msg = "WARNING use flexible-array member instead (https://www.kernel.org/doc/html/latest/process/deprecated.html#zero-length-and-one-element-arrays)"
coccilib.org.print_todo(p[0], msg)
