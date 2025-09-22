=====================
How To Use Attributes
=====================

.. contents::
  :local:

Introduction
============

Attributes in LLVM have changed in some fundamental ways.  It was necessary to
do this to support expanding the attributes to encompass more than a handful of
attributes --- e.g. command line options.  The old way of handling attributes
consisted of representing them as a bit mask of values.  This bit mask was
stored in a "list" structure that was reference counted.  The advantage of this
was that attributes could be manipulated with 'or's and 'and's.  The
disadvantage of this was that there was limited room for expansion, and
virtually no support for attribute-value pairs other than alignment.

In the new scheme, an ``Attribute`` object represents a single attribute that's
uniqued.  You use the ``Attribute::get`` methods to create a new ``Attribute``
object.  An attribute can be a single "enum" value (the enum being the
``Attribute::AttrKind`` enum), a string representing a target-dependent
attribute, or an attribute-value pair.  Some examples:

* Target-independent: ``noinline``, ``zext``
* Target-dependent: ``"no-sse"``, ``"thumb2"``
* Attribute-value pair: ``"cpu" = "cortex-a8"``, ``align = 4``

Note: for an attribute value pair, we expect a target-dependent attribute to
have a string for the value.

``Attribute``
=============
An ``Attribute`` object is designed to be passed around by value.

Because attributes are no longer represented as a bit mask, you will need to
convert any code which does treat them as a bit mask to use the new query
methods on the Attribute class.

``AttributeList``
=================

The ``AttributeList`` stores a collection of Attribute objects for each kind of
object that may have an attribute associated with it: the function as a whole,
the return type, or the function's parameters.  A function's attributes are at
index ``AttributeList::FunctionIndex``; the return type's attributes are at
index ``AttributeList::ReturnIndex``; and the function's parameters' attributes
are at indices 1, ..., n (where 'n' is the number of parameters).  Most methods
on the ``AttributeList`` class take an index parameter.

An ``AttributeList`` is also a uniqued and immutable object.  You create an
``AttributeList`` through the ``AttributeList::get`` methods.  You can add and
remove attributes, which result in the creation of a new ``AttributeList``.

An ``AttributeList`` object is designed to be passed around by value.

Note: It is advised that you do *not* use the ``AttributeList`` "introspection"
methods (e.g. ``Raw``, ``getRawPointer``, etc.).  These methods break
encapsulation, and may be removed in a future release.

``AttrBuilder``
===============

Lastly, we have a "builder" class to help create the ``AttributeList`` object
without having to create several different intermediate uniqued
``AttributeList`` objects.  The ``AttrBuilder`` class allows you to add and
remove attributes at will.  The attributes won't be uniqued until you call the
appropriate ``AttributeList::get`` method.

An ``AttrBuilder`` object is *not* designed to be passed around by value.  It
should be passed by reference.

Note: It is advised that you do *not* use the ``AttrBuilder::addRawValue()``
method or the ``AttrBuilder(uint64_t Val)`` constructor.  These are for
backwards compatibility and may be removed in a future release.

And that's basically it! A lot of functionality is hidden behind these classes,
but the interfaces are pretty straight forward.

