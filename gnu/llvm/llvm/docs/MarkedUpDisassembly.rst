=======================================
LLVM's Optional Rich Disassembly Output
=======================================

.. contents::
   :local:

Introduction
============

LLVM's default disassembly output is raw text. To allow consumers more ability
to introspect the instructions' textual representation or to reformat for a more
user friendly display there is an optional rich disassembly output.

This optional output is sufficient to reference into individual portions of the
instruction text. This is intended for clients like disassemblers, list file
generators, and pretty-printers, which need more than the raw instructions and
the ability to print them.

To provide this functionality the assembly text is marked up with annotations.
The markup is simple enough in syntax to be robust even in the case of version
mismatches between consumers and producers. That is, the syntax generally does
not carry semantics beyond "this text has an annotation," so consumers can
simply ignore annotations they do not understand or do not care about.

After calling ``LLVMCreateDisasm()`` to create a disassembler context the
optional output is enable with this call:

.. code-block:: c

    LLVMSetDisasmOptions(DC, LLVMDisassembler_Option_UseMarkup);

Then subsequent calls to ``LLVMDisasmInstruction()`` will return output strings
with the marked up annotations.

Instruction Annotations
=======================

.. _contextual markups:

Contextual markups
------------------

Annotated assembly display will supply contextual markup to help clients more
efficiently implement things like pretty printers. Most markup will be target
independent, so clients can effectively provide good display without any target
specific knowledge.

Annotated assembly goes through the normal instruction printer, but optionally
includes contextual tags on portions of the instruction string. An annotation
is any '<' '>' delimited section of text(1).

.. code-block:: bat

    annotation: '<' tag-name tag-modifier-list ':' annotated-text '>'
    tag-name: identifier
    tag-modifier-list: comma delimited identifier list

The tag-name is an identifier which gives the type of the annotation. For the
first pass, this will be very simple, with memory references, registers, and
immediates having the tag names "mem", "reg", and "imm", respectively.

The tag-modifier-list is typically additional target-specific context, such as
register class.

Clients should accept and ignore any tag-names or tag-modifiers they do not
understand, allowing the annotations to grow in richness without breaking older
clients.

For example, a possible annotation of an ARM load of a stack-relative location
might be annotated as:

.. code-block:: text

   ldr <reg gpr:r0>, <mem regoffset:[<reg gpr:sp>, <imm:#4>]>


1: For assembly dialects in which '<' and/or '>' are legal tokens, a literal token is escaped by following immediately with a repeat of the character.  For example, a literal '<' character is output as '<<' in an annotated assembly string.

C API Details
-------------

The intended consumers of this information use the C API, therefore the new C
API function for the disassembler will be added to provide an option to produce
disassembled instructions with annotations, ``LLVMSetDisasmOptions()`` and the
``LLVMDisassembler_Option_UseMarkup`` option (see above).
