=====================================
CodeView Type Records
=====================================


.. contents::
   :local:

.. _types_intro:

Introduction
============

This document describes the usage and serialization format of the various
CodeView type records that LLVM understands.  This document does not describe
every single CodeView type record that is defined.  In some cases, this is
because the records are clearly deprecated and can only appear in very old
software (e.g. the 16-bit types).  On other cases, it is because the records
have never been observed in practice.  This could be because they are only
generated for non-C++ code (e.g. Visual Basic, C#), or because they have been
made obsolete by newer records, or any number of other reasons.  However, the
records we describe here should cover 99% of type records that one can expect
to encounter when dealing with modern C++ toolchains.

Record Categories
=================

We can think of a sequence of CodeView type records as an array of variable length
`leaf records`.  Each such record describes its own length as part of a fixed-size
header, as well as the kind of record it is.  Leaf records are either padded to 4
bytes (if this type stream appears in a TPI/IPI stream of a PDB) or not padded at
all (if this type stream appears in the ``.debug$T`` section of an object file).
Padding is implemented by inserting a decreasing sequence of `<_padding_records>`
that terminates with ``LF_PAD0``.

The final category of record is a ``member record``.  One particular leaf type --
``LF_FIELDLIST`` -- contains a series of embedded records.  While the outer
``LF_FIELDLIST`` describes its length (like any other leaf record), the embedded
records -- called ``member records`` do not.

.. _leaf_types:

Leaf Records
------------

All leaf records begin with the following 4 byte prefix:

.. code-block:: c++

  struct RecordHeader {
    uint16_t RecordLen;  // Record length, not including this 2 byte field.
    uint16_t RecordKind; // Record kind enum.
  };

LF_POINTER (0x1002)
^^^^^^^^^^^^^^^^^^^

**Usage:** Describes a pointer to another type.

**Layout:**

.. code-block:: none

  .--------------------.-- +0
  |    Referent Type   |
  .--------------------.-- +4
  |     Attributes     |
  .--------------------.-- +8
  |  Member Ptr Info   |       Only present if |Attributes| indicates this is a member pointer.
  .--------------------.-- +E

Attributes is a bitfield with the following layout:

.. code-block:: none

    .-----------------------------------------------------------------------------------------------------.
    |     Unused                   |  Flags  |       Size       |   Modifiers   |  Mode   |      Kind     |
    .-----------------------------------------------------------------------------------------------------.
    |                              |         |                  |               |         |               |
   0x100                         +0x16     +0x13               +0xD            +0x8      +0x5            +0x0

where the various fields are defined by the following enums:

.. code-block:: c++

  enum class PointerKind : uint8_t {
    Near16 = 0x00,                // 16 bit pointer
    Far16 = 0x01,                 // 16:16 far pointer
    Huge16 = 0x02,                // 16:16 huge pointer
    BasedOnSegment = 0x03,        // based on segment
    BasedOnValue = 0x04,          // based on value of base
    BasedOnSegmentValue = 0x05,   // based on segment value of base
    BasedOnAddress = 0x06,        // based on address of base
    BasedOnSegmentAddress = 0x07, // based on segment address of base
    BasedOnType = 0x08,           // based on type
    BasedOnSelf = 0x09,           // based on self
    Near32 = 0x0a,                // 32 bit pointer
    Far32 = 0x0b,                 // 16:32 pointer
    Near64 = 0x0c                 // 64 bit pointer
  };
  enum class PointerMode : uint8_t {
    Pointer = 0x00,                 // "normal" pointer
    LValueReference = 0x01,         // "old" reference
    PointerToDataMember = 0x02,     // pointer to data member
    PointerToMemberFunction = 0x03, // pointer to member function
    RValueReference = 0x04          // r-value reference
  };
  enum class PointerModifiers : uint8_t {
    None = 0x00,                    // "normal" pointer
    Flat32 = 0x01,                  // "flat" pointer
    Volatile = 0x02,                // pointer is marked volatile
    Const = 0x04,                   // pointer is marked const
    Unaligned = 0x08,               // pointer is marked unaligned
    Restrict = 0x10,                // pointer is marked restrict
  };
  enum class PointerFlags : uint8_t {
    WinRTSmartPointer = 0x01,       // pointer is a WinRT smart pointer
    LValueRefThisPointer = 0x02,    // pointer is a 'this' pointer of a member function with ref qualifier (e.g. void X::foo() &)
    RValueRefThisPointer = 0x04     // pointer is a 'this' pointer of a member function with ref qualifier (e.g. void X::foo() &&)
  };

The ``Size`` field of the Attributes bitmask is a 1-byte value indicating the
pointer size.  For example, a `void*` would have a size of either 4 or 8 depending
on the target architecture.  On the other hand, if ``Mode`` indicates that this is
a pointer to member function or pointer to data member, then the size can be any
implementation defined number.

The ``Member Ptr Info`` field of the ``LF_POINTER`` record is only present if the
attributes indicate that this is a pointer to member.

Note that "plain" pointers to primitive types are not represented by ``LF_POINTER``
records, they are indicated by special reserved :ref:`TypeIndex values <type_indices>`.



LF_MODIFIER (0x1001)
^^^^^^^^^^^^^^^^^^^^

LF_PROCEDURE (0x1008)
^^^^^^^^^^^^^^^^^^^^^

LF_MFUNCTION (0x1009)
^^^^^^^^^^^^^^^^^^^^^

LF_LABEL (0x000e)
^^^^^^^^^^^^^^^^^

LF_ARGLIST (0x1201)
^^^^^^^^^^^^^^^^^^^

LF_FIELDLIST (0x1203)
^^^^^^^^^^^^^^^^^^^^^

LF_ARRAY (0x1503)
^^^^^^^^^^^^^^^^^

LF_CLASS (0x1504)
^^^^^^^^^^^^^^^^^

LF_STRUCTURE (0x1505)
^^^^^^^^^^^^^^^^^^^^^

LF_INTERFACE (0x1519)
^^^^^^^^^^^^^^^^^^^^^

LF_UNION (0x1506)
^^^^^^^^^^^^^^^^^

LF_ENUM (0x1507)
^^^^^^^^^^^^^^^^

LF_TYPESERVER2 (0x1515)
^^^^^^^^^^^^^^^^^^^^^^^

LF_VFTABLE (0x151d)
^^^^^^^^^^^^^^^^^^^

LF_VTSHAPE (0x000a)
^^^^^^^^^^^^^^^^^^^

LF_BITFIELD (0x1205)
^^^^^^^^^^^^^^^^^^^^

LF_FUNC_ID (0x1601)
^^^^^^^^^^^^^^^^^^^

LF_MFUNC_ID (0x1602)
^^^^^^^^^^^^^^^^^^^^

LF_BUILDINFO (0x1603)
^^^^^^^^^^^^^^^^^^^^^

LF_SUBSTR_LIST (0x1604)
^^^^^^^^^^^^^^^^^^^^^^^

LF_STRING_ID (0x1605)
^^^^^^^^^^^^^^^^^^^^^

LF_UDT_SRC_LINE (0x1606)
^^^^^^^^^^^^^^^^^^^^^^^^

LF_UDT_MOD_SRC_LINE (0x1607)
^^^^^^^^^^^^^^^^^^^^^^^^^^^^

LF_METHODLIST (0x1206)
^^^^^^^^^^^^^^^^^^^^^^

LF_PRECOMP (0x1509)
^^^^^^^^^^^^^^^^^^^

LF_ENDPRECOMP (0x0014)
^^^^^^^^^^^^^^^^^^^^^^

.. _member_types:

Member Records
--------------

LF_BCLASS (0x1400)
^^^^^^^^^^^^^^^^^^

LF_BINTERFACE (0x151a)
^^^^^^^^^^^^^^^^^^^^^^

LF_VBCLASS (0x1401)
^^^^^^^^^^^^^^^^^^^

LF_IVBCLASS (0x1402)
^^^^^^^^^^^^^^^^^^^^

LF_VFUNCTAB (0x1409)
^^^^^^^^^^^^^^^^^^^^

LF_STMEMBER (0x150e)
^^^^^^^^^^^^^^^^^^^^

LF_METHOD (0x150f)
^^^^^^^^^^^^^^^^^^

LF_MEMBER (0x150d)
^^^^^^^^^^^^^^^^^^

LF_NESTTYPE (0x1510)
^^^^^^^^^^^^^^^^^^^^

LF_ONEMETHOD (0x1511)
^^^^^^^^^^^^^^^^^^^^^

LF_ENUMERATE (0x1502)
^^^^^^^^^^^^^^^^^^^^^

LF_INDEX (0x1404)
^^^^^^^^^^^^^^^^^

.. _padding_records:

Padding Records
---------------

LF_PADn (0xf0 + n)
^^^^^^^^^^^^^^^^^^
