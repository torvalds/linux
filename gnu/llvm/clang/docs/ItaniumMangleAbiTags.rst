========
ABI tags
========

Introduction
============

This text tries to describe gcc semantic for mangling "abi_tag" attributes
described in https://gcc.gnu.org/onlinedocs/gcc/C_002b_002b-Attributes.html

There is no guarantee the following rules are correct, complete or make sense
in any way as they were determined empirically by experiments with gcc5.

Declaration
===========

ABI tags are declared in an abi_tag attribute and can be applied to a
function, variable, class or inline namespace declaration. The attribute takes
one or more strings (called tags); the order does not matter.

See https://gcc.gnu.org/onlinedocs/gcc/C_002b_002b-Attributes.html for
details.

Tags on an inline namespace are called "implicit tags", all other tags are
"explicit tags".

Mangling
========

All tags that are "active" on an <unqualified-name> are emitted after the
<unqualified-name>, before <template-args> or <discriminator>, and are part of
the same <substitution> the <unqualified-name> is.

They are mangled as:

.. code-block:: none

    <abi-tags> ::= <abi-tag>*   # sort by name
    <abi-tag> ::= B <tag source-name>

Example:

.. code-block:: c++

    __attribute__((abi_tag("test")))
    void Func();
    // gets mangled as: _Z4FuncB4testv (prettified as `Func[abi:test]()`)

Active tags
===========

A namespace does not have any active tags. For types (class / struct / union /
enum), the explicit tags are the active tags.

For variables and functions, the active tags are the explicit tags plus any
"required tags" which are not in the "available tags" set:

.. code-block:: none

    derived-tags := (required-tags - available-tags)
    active-tags := explicit-tags + derived-tags

Required tags for a function
============================

If a function is used as a local scope for another name, and is part of
another function as local scope, it doesn't have any required tags.

If a function is used as a local scope for a guard variable name, it doesn't
have any required tags.

Otherwise the function requires any implicit or explicit tag used in the name
for the return type.

Example:

.. code-block:: c++

    namespace A {
      inline namespace B __attribute__((abi_tag)) {
        struct C { int x; };
      }
    }

    A::C foo(); // gets mangled as: _Z3fooB1Bv (prettified as `foo[abi:B]()`)

Required tags for a variable
============================

A variable requires any implicit or explicit tag used in its type.

Available tags
==============

All tags used in the prefix and in the template arguments for a name are
available. Also, for functions, all tags from the <bare-function-type>
(which might include the return type for template functions) are available.

For <local-name>s all active tags used in the local part (<function-
encoding>) are available, but not implicit tags which were not active.

Implicit and explicit tags used in the <unqualified-name> for a function (as
in the type of a cast operator) are NOT available.

Example: a cast operator to std::string (which is
std::__cxx11::basic_string<...>) will use 'cxx11' as an active tag, as it is
required from the return type `std::string` but not available.
