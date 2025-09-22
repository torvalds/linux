======================
Matching the Clang AST
======================

This document explains how to use Clang's LibASTMatchers to match interesting
nodes of the AST and execute code that uses the matched nodes.  Combined with
:doc:`LibTooling`, LibASTMatchers helps to write code-to-code transformation
tools or query tools.

We assume basic knowledge about the Clang AST.  See the :doc:`Introduction
to the Clang AST <IntroductionToTheClangAST>` if you want to learn more
about how the AST is structured.

..  FIXME: create tutorial and link to the tutorial

Introduction
------------

LibASTMatchers provides a domain specific language to create predicates on
Clang's AST.  This DSL is written in and can be used from C++, allowing users
to write a single program to both match AST nodes and access the node's C++
interface to extract attributes, source locations, or any other information
provided on the AST level.

AST matchers are predicates on nodes in the AST.  Matchers are created by
calling creator functions that allow building up a tree of matchers, where
inner matchers are used to make the match more specific.

For example, to create a matcher that matches all class or union declarations
in the AST of a translation unit, you can call `recordDecl()
<LibASTMatchersReference.html#recordDecl0Anchor>`_.  To narrow the match down,
for example to find all class or union declarations with the name "``Foo``",
insert a `hasName <LibASTMatchersReference.html#hasName0Anchor>`_ matcher: the
call ``recordDecl(hasName("Foo"))`` returns a matcher that matches classes or
unions that are named "``Foo``", in any namespace.  By default, matchers that
accept multiple inner matchers use an implicit `allOf()
<LibASTMatchersReference.html#allOf0Anchor>`_.  This allows further narrowing
down the match, for example to match all classes that are derived from
"``Bar``": ``recordDecl(hasName("Foo"), isDerivedFrom("Bar"))``.

How to create a matcher
-----------------------

With more than a thousand classes in the Clang AST, one can quickly get lost
when trying to figure out how to create a matcher for a specific pattern.  This
section will teach you how to use a rigorous step-by-step pattern to build the
matcher you are interested in.  Note that there will always be matchers missing
for some part of the AST.  See the section about :ref:`how to write your own
AST matchers <astmatchers-writing>` later in this document.

..  FIXME: why is it linking back to the same section?!

The precondition to using the matchers is to understand how the AST for what you
want to match looks like.  The
:doc:`Introduction to the Clang AST <IntroductionToTheClangAST>` teaches you
how to dump a translation unit's AST into a human readable format.

..  FIXME: Introduce link to ASTMatchersTutorial.html
..  FIXME: Introduce link to ASTMatchersCookbook.html

In general, the strategy to create the right matchers is:

#. Find the outermost class in Clang's AST you want to match.
#. Look at the `AST Matcher Reference <LibASTMatchersReference.html>`_ for
   matchers that either match the node you're interested in or narrow down
   attributes on the node.
#. Create your outer match expression.  Verify that it works as expected.
#. Examine the matchers for what the next inner node you want to match is.
#. Repeat until the matcher is finished.

.. _astmatchers-bind:

Binding nodes in match expressions
----------------------------------

Matcher expressions allow you to specify which parts of the AST are interesting
for a certain task.  Often you will want to then do something with the nodes
that were matched, like building source code transformations.

To that end, matchers that match specific AST nodes (so called node matchers)
are bindable; for example, ``recordDecl(hasName("MyClass")).bind("id")`` will
bind the matched ``recordDecl`` node to the string "``id``", to be later
retrieved in the `match callback
<https://clang.llvm.org/doxygen/classclang_1_1ast__matchers_1_1MatchFinder_1_1MatchCallback.html>`_.

..  FIXME: Introduce link to ASTMatchersTutorial.html
..  FIXME: Introduce link to ASTMatchersCookbook.html

Writing your own matchers
-------------------------

There are multiple different ways to define a matcher, depending on its type
and flexibility.

``VariadicDynCastAllOfMatcher<Base, Derived>``
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

Those match all nodes of type *Base* if they can be dynamically casted to
*Derived*.  The names of those matchers are nouns, which closely resemble
*Derived*.  ``VariadicDynCastAllOfMatchers`` are the backbone of the matcher
hierarchy.  Most often, your match expression will start with one of them, and
you can :ref:`bind <astmatchers-bind>` the node they represent to ids for later
processing.

``VariadicDynCastAllOfMatchers`` are callable classes that model variadic
template functions in C++03.  They take an arbitrary number of
``Matcher<Derived>`` and return a ``Matcher<Base>``.

``AST_MATCHER_P(Type, Name, ParamType, Param)``
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

Most matcher definitions use the matcher creation macros.  Those define both
the matcher of type ``Matcher<Type>`` itself, and a matcher-creation function
named *Name* that takes a parameter of type *ParamType* and returns the
corresponding matcher.

There are multiple matcher definition macros that deal with polymorphic return
values and different parameter counts.  See `ASTMatchersMacros.h
<https://clang.llvm.org/doxygen/ASTMatchersMacros_8h.html>`_.

.. _astmatchers-writing:

Matcher creation functions
^^^^^^^^^^^^^^^^^^^^^^^^^^

Matchers are generated by nesting calls to matcher creation functions.  Most of
the time those functions are either created by using
``VariadicDynCastAllOfMatcher`` or the matcher creation macros (see below).
The free-standing functions are an indication that this matcher is just a
combination of other matchers, as is for example the case with `callee
<LibASTMatchersReference.html#callee1Anchor>`_.

..  FIXME: "... macros (see below)" --- there isn't anything below

