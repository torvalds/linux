/// These iterators only exit normally when the loop cursor is NULL, so there
/// is no point to call of_node_put on the final value.
///
// Confidence: High
// Copyright: (C) 2010 Nicolas Palix, DIKU.  GPLv2.
// Copyright: (C) 2010 Julia Lawall, DIKU.  GPLv2.
// Copyright: (C) 2010 Gilles Muller, INRIA/LiP6.  GPLv2.
// URL: http://coccinelle.lip6.fr/
// Comments:
// Options: -no_includes -include_headers

virtual patch

@@
iterator name for_each_node_by_name;
expression np,E;
identifier l;
@@

for_each_node_by_name(np,...) {
  ... when != break;
      when != goto l;
}
... when != np = E
- of_node_put(np);

@@
iterator name for_each_node_by_type;
expression np,E;
identifier l;
@@

for_each_node_by_type(np,...) {
  ... when != break;
      when != goto l;
}
... when != np = E
- of_node_put(np);

@@
iterator name for_each_compatible_node;
expression np,E;
identifier l;
@@

for_each_compatible_node(np,...) {
  ... when != break;
      when != goto l;
}
... when != np = E
- of_node_put(np);

@@
iterator name for_each_matching_node;
expression np,E;
identifier l;
@@

for_each_matching_node(np,...) {
  ... when != break;
      when != goto l;
}
... when != np = E
- of_node_put(np);
