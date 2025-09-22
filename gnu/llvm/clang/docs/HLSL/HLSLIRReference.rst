=================
HLSL IR Reference
=================

.. contents::
   :local:

Introduction
============

The goal of this document is to provide a reference for all the special purpose
IR metadata and attributes used by the HLSL code generation path.

Function Attributes
===================

``hlsl.shader``
---------------

The ``hlsl.shader`` function attribute is a string attribute applied to entry
functions. The value is the string representation of the shader stage (i.e.
``compute``, ``pixel``, etc).
