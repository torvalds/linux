=================
TableGen BackEnds
=================

.. contents::
   :local:

Introduction
============

TableGen backends are at the core of TableGen's functionality. The source
files provide the classes and records that are parsed and end up as a
collection of record instances, but it's up to the backend to interpret and
print the records in a way that is meaningful to the user (normally a C++
include file or a textual list of warnings, options, and error messages).

TableGen is used by both LLVM, Clang, and MLIR with very different goals.
LLVM uses it as a way to automate the generation of massive amounts of
information regarding instructions, schedules, cores, and architecture
features. Some backends generate output that is consumed by more than one
source file, so they need to be created in a way that makes it is easy for
preprocessor tricks to be used. Some backends can also print C++ code
structures, so that they can be directly included as-is.

Clang, on the other hand, uses it mainly for diagnostic messages (errors,
warnings, tips) and attributes, so more on the textual end of the scale.

MLIR uses TableGen to define operations, operation dialects, and operation
traits.

See the :doc:`TableGen Programmer's Reference <./ProgRef>` for an in-depth
description of TableGen, and the :doc:`TableGen Backend Developer's Guide
<./BackGuide>` for a guide to writing a new backend.

LLVM BackEnds
=============

.. warning::
   This portion is incomplete. Each section below needs three subsections:
   description of its purpose with a list of users, output generated from
   generic input, and finally why it needed a new backend (in case there's
   something similar).

Overall, each backend will take the same TableGen file type and transform into
similar output for different targets/uses. There is an implicit contract between
the TableGen files, the back-ends and their users.

For instance, a global contract is that each back-end produces macro-guarded
sections. Based on whether the file is included by a header or a source file,
or even in which context of each file the include is being used, you have
todefine a macro just before including it, to get the right output:

.. code-block:: c++

  #define GET_REGINFO_TARGET_DESC
  #include "ARMGenRegisterInfo.inc"

And just part of the generated file would be included. This is useful if
you need the same information in multiple formats (instantiation, initialization,
getter/setter functions, etc) from the same source TableGen file without having
to re-compile the TableGen file multiple times.

Sometimes, multiple macros might be defined before the same include file to
output multiple blocks:

.. code-block:: c++

  #define GET_REGISTER_MATCHER
  #define GET_SUBTARGET_FEATURE_NAME
  #define GET_MATCHER_IMPLEMENTATION
  #include "ARMGenAsmMatcher.inc"

The macros will be undef'd automatically as they're used, in the include file.

On all LLVM back-ends, the ``llvm-tblgen`` binary will be executed on the root
TableGen file ``<Target>.td``, which should include all others. This guarantees
that all information needed is accessible, and that no duplication is needed
in the TableGen files.

CodeEmitter
-----------

**Purpose**: CodeEmitterGen uses the descriptions of instructions and their fields to
construct an automated code emitter: a function that, given a MachineInstr,
returns the (currently, 32-bit unsigned) value of the instruction.

**Output**: C++ code, implementing the target's CodeEmitter
class by overriding the virtual functions as ``<Target>CodeEmitter::function()``.

**Usage**: Used to include directly at the end of ``<Target>MCCodeEmitter.cpp``.

RegisterInfo
------------

**Purpose**: This tablegen backend is responsible for emitting a description of a target
register file for a code generator.  It uses instances of the Register,
RegisterAliases, and RegisterClass classes to gather this information.

**Output**: C++ code with enums and structures representing the register mappings,
properties, masks, etc.

**Usage**: Both on ``<Target>BaseRegisterInfo`` and ``<Target>MCTargetDesc`` (headers
and source files) with macros defining in which they are for declaration vs.
initialization issues.

InstrInfo
---------

**Purpose**: This tablegen backend is responsible for emitting a description of the target
instruction set for the code generator. (what are the differences from CodeEmitter?)

**Output**: C++ code with enums and structures representing the instruction mappings,
properties, masks, etc.

**Usage**: Both on ``<Target>BaseInstrInfo`` and ``<Target>MCTargetDesc`` (headers
and source files) with macros defining in which they are for declaration vs.
initialization issues.

AsmWriter
---------

**Purpose**: Emits an assembly printer for the current target.

**Output**: Implementation of ``<Target>InstPrinter::printInstruction()``, among
other things.

**Usage**: Included directly into ``InstPrinter/<Target>InstPrinter.cpp``.

AsmMatcher
----------

**Purpose**: Emits a target specifier matcher for
converting parsed assembly operands in the MCInst structures. It also
emits a matcher for custom operand parsing. Extensive documentation is
written on the ``AsmMatcherEmitter.cpp`` file.

**Output**: Assembler parsers' matcher functions, declarations, etc.

**Usage**: Used in back-ends' ``AsmParser/<Target>AsmParser.cpp`` for
building the AsmParser class.

Disassembler
------------

**Purpose**: Contains disassembler table emitters for various
architectures. Extensive documentation is written on the
``DisassemblerEmitter.cpp`` file.

**Output**: Decoding tables, static decoding functions, etc.

**Usage**: Directly included in ``Disassembler/<Target>Disassembler.cpp``
to cater for all default decodings, after all hand-made ones.

PseudoLowering
--------------

**Purpose**: Generate pseudo instruction lowering.

**Output**: Implements ``<Target>AsmPrinter::emitPseudoExpansionLowering()``.

**Usage**: Included directly into ``<Target>AsmPrinter.cpp``.

CallingConv
-----------

**Purpose**: Responsible for emitting descriptions of the calling
conventions supported by this target.

**Output**: Implement static functions to deal with calling conventions
chained by matching styles, returning false on no match.

**Usage**: Used in ISelLowering and FastIsel as function pointers to
implementation returned by a CC selection function.

DAGISel
-------

**Purpose**: Generate a DAG instruction selector.

**Output**: Creates huge functions for automating DAG selection.

**Usage**: Included in ``<Target>ISelDAGToDAG.cpp`` inside the target's
implementation of ``SelectionDAGISel``.

DFAPacketizer
-------------

**Purpose**: This class parses the Schedule.td file and produces an API that
can be used to reason about whether an instruction can be added to a packet
on a VLIW architecture. The class internally generates a deterministic finite
automaton (DFA) that models all possible mappings of machine instructions
to functional units as instructions are added to a packet.

**Output**: Scheduling tables for GPU back-ends (Hexagon, AMD).

**Usage**: Included directly on ``<Target>InstrInfo.cpp``.

FastISel
--------

**Purpose**: This tablegen backend emits code for use by the "fast"
instruction selection algorithm. See the comments at the top of
lib/CodeGen/SelectionDAG/FastISel.cpp for background. This file
scans through the target's tablegen instruction-info files
and extracts instructions with obvious-looking patterns, and it emits
code to look up these instructions by type and operator.

**Output**: Generates ``Predicate`` and ``FastEmit`` methods.

**Usage**: Implements private methods of the targets' implementation
of ``FastISel`` class.

Subtarget
---------

**Purpose**: Generate subtarget enumerations.

**Output**: Enums, globals, local tables for sub-target information.

**Usage**: Populates ``<Target>Subtarget`` and
``MCTargetDesc/<Target>MCTargetDesc`` files (both headers and source).

Intrinsic
---------

**Purpose**: Generate (target) intrinsic information.

OptParserDefs
-------------

**Purpose**: Print enum values for a class.

SearchableTables
----------------

**Purpose**: Generate custom searchable tables.

**Output**: Enums, global tables, and lookup helper functions.

**Usage**: This backend allows generating free-form, target-specific tables
from TableGen records. The ARM and AArch64 targets use this backend to generate
tables of system registers; the AMDGPU target uses it to generate meta-data
about complex image and memory buffer instructions.

See `SearchableTables Reference`_ for a detailed description.

CTags
-----

**Purpose**: This tablegen backend emits an index of definitions in ctags(1)
format. A helper script, utils/TableGen/tdtags, provides an easier-to-use
interface; run 'tdtags -H' for documentation.

X86EVEX2VEX
-----------

**Purpose**: This X86 specific tablegen backend emits tables that map EVEX
encoded instructions to their VEX encoded identical instruction.

Clang BackEnds
==============

ClangAttrClasses
----------------

**Purpose**: Creates Attrs.inc, which contains semantic attribute class
declarations for any attribute in ``Attr.td`` that has not set ``ASTNode = 0``.
This file is included as part of ``Attr.h``.

ClangAttrParserStringSwitches
-----------------------------

**Purpose**: Creates AttrParserStringSwitches.inc, which contains
StringSwitch::Case statements for parser-related string switches. Each switch
is given its own macro (such as ``CLANG_ATTR_ARG_CONTEXT_LIST``, or
``CLANG_ATTR_IDENTIFIER_ARG_LIST``), which is expected to be defined before
including AttrParserStringSwitches.inc, and undefined after.

ClangAttrImpl
-------------

**Purpose**: Creates AttrImpl.inc, which contains semantic attribute class
definitions for any attribute in ``Attr.td`` that has not set ``ASTNode = 0``.
This file is included as part of ``AttrImpl.cpp``.

ClangAttrList
-------------

**Purpose**: Creates AttrList.inc, which is used when a list of semantic
attribute identifiers is required. For instance, ``AttrKinds.h`` includes this
file to generate the list of ``attr::Kind`` enumeration values. This list is
separated out into multiple categories: attributes, inheritable attributes, and
inheritable parameter attributes. This categorization happens automatically
based on information in ``Attr.td`` and is used to implement the ``classof``
functionality required for ``dyn_cast`` and similar APIs.

ClangAttrPCHRead
----------------

**Purpose**: Creates AttrPCHRead.inc, which is used to deserialize attributes
in the ``ASTReader::ReadAttributes`` function.

ClangAttrPCHWrite
-----------------

**Purpose**: Creates AttrPCHWrite.inc, which is used to serialize attributes in
the ``ASTWriter::WriteAttributes`` function.

ClangAttrSpellings
---------------------

**Purpose**: Creates AttrSpellings.inc, which is used to implement the
``__has_attribute`` feature test macro.

ClangAttrSpellingListIndex
--------------------------

**Purpose**: Creates AttrSpellingListIndex.inc, which is used to map parsed
attribute spellings (including which syntax or scope was used) to an attribute
spelling list index. These spelling list index values are internal
implementation details exposed via
``AttributeList::getAttributeSpellingListIndex``.

ClangAttrVisitor
-------------------

**Purpose**: Creates AttrVisitor.inc, which is used when implementing
recursive AST visitors.

ClangAttrTemplateInstantiate
----------------------------

**Purpose**: Creates AttrTemplateInstantiate.inc, which implements the
``instantiateTemplateAttribute`` function, used when instantiating a template
that requires an attribute to be cloned.

ClangAttrParsedAttrList
-----------------------

**Purpose**: Creates AttrParsedAttrList.inc, which is used to generate the
``AttributeList::Kind`` parsed attribute enumeration.

ClangAttrParsedAttrImpl
-----------------------

**Purpose**: Creates AttrParsedAttrImpl.inc, which is used by
``AttributeList.cpp`` to implement several functions on the ``AttributeList``
class. This functionality is implemented via the ``AttrInfoMap ParsedAttrInfo``
array, which contains one element per parsed attribute object.

ClangAttrParsedAttrKinds
------------------------

**Purpose**: Creates AttrParsedAttrKinds.inc, which is used to implement the
``AttributeList::getKind`` function, mapping a string (and syntax) to a parsed
attribute ``AttributeList::Kind`` enumeration.

ClangAttrDump
-------------

**Purpose**: Creates AttrDump.inc, which dumps information about an attribute.
It is used to implement ``ASTDumper::dumpAttr``.

ClangDiagsDefs
--------------

Generate Clang diagnostics definitions.

ClangDiagGroups
---------------

Generate Clang diagnostic groups.

ClangDiagsIndexName
-------------------

Generate Clang diagnostic name index.

ClangCommentNodes
-----------------

Generate Clang AST comment nodes.

ClangDeclNodes
--------------

Generate Clang AST declaration nodes.

ClangStmtNodes
--------------

Generate Clang AST statement nodes.

ClangSACheckers
---------------

Generate Clang Static Analyzer checkers.

ClangCommentHTMLTags
--------------------

Generate efficient matchers for HTML tag names that are used in documentation comments.

ClangCommentHTMLTagsProperties
------------------------------

Generate efficient matchers for HTML tag properties.

ClangCommentHTMLNamedCharacterReferences
----------------------------------------

Generate function to translate named character references to UTF-8 sequences.

ClangCommentCommandInfo
-----------------------

Generate command properties for commands that are used in documentation comments.

ClangCommentCommandList
-----------------------

Generate list of commands that are used in documentation comments.

ArmNeon
-------

Generate arm_neon.h for clang.

ArmNeonSema
-----------

Generate ARM NEON sema support for clang.

ArmNeonTest
-----------

Generate ARM NEON tests for clang.

AttrDocs
--------

**Purpose**: Creates ``AttributeReference.rst`` from ``AttrDocs.td``, and is
used for documenting user-facing attributes.

General BackEnds
================

Print Records
-------------

The TableGen command option ``--print-records`` invokes a simple backend
that prints all the classes and records defined in the source files. This is
the default backend option. See the :doc:`TableGen Backend Developer's Guide
<./BackGuide>` for more information.

Print Detailed Records
----------------------

The TableGen command option ``--print-detailed-records`` invokes a backend
that prints all the global variables, classes, and records defined in the
source files, with more detail than the default record printer. See the
:doc:`TableGen Backend Developer's Guide <./BackGuide>` for more
information.

JSON Reference
--------------

**Purpose**: Output all the values in every ``def``, as a JSON data
structure that can be easily parsed by a variety of languages. Useful
for writing custom backends without having to modify TableGen itself,
or for performing auxiliary analysis on the same TableGen data passed
to a built-in backend.

**Output**:

The root of the output file is a JSON object (i.e. dictionary),
containing the following fixed keys:

* ``!tablegen_json_version``: a numeric version field that will
  increase if an incompatible change is ever made to the structure of
  this data. The format described here corresponds to version 1.

* ``!instanceof``: a dictionary whose keys are the class names defined
  in the TableGen input. For each key, the corresponding value is an
  array of strings giving the names of ``def`` records that derive
  from that class. So ``root["!instanceof"]["Instruction"]``, for
  example, would list the names of all the records deriving from the
  class ``Instruction``.

For each ``def`` record, the root object also has a key for the record
name. The corresponding value is a subsidiary object containing the
following fixed keys:

* ``!superclasses``: an array of strings giving the names of all the
  classes that this record derives from.

* ``!fields``: an array of strings giving the names of all the variables
  in this record that were defined with the ``field`` keyword.

* ``!name``: a string giving the name of the record. This is always
  identical to the key in the JSON root object corresponding to this
  record's dictionary. (If the record is anonymous, the name is
  arbitrary.)

* ``!anonymous``: a boolean indicating whether the record's name was
  specified by the TableGen input (if it is ``false``), or invented by
  TableGen itself (if ``true``).

* ``!locs``: an array of strings giving the source locations associated with
  this record. For records instantiated from a ``multiclass``, this gives the
  location of each ``def`` or ``defm``, starting with the inner-most
  ``multiclass``, and ending with the top-level ``defm``. Each string contains
  the file name and line number, separated by a colon.

For each variable defined in a record, the ``def`` object for that
record also has a key for the variable name. The corresponding value
is a translation into JSON of the variable's value, using the
conventions described below.

Some TableGen data types are translated directly into the
corresponding JSON type:

* A completely undefined value (e.g. for a variable declared without
  initializer in some superclass of this record, and never initialized
  by the record itself or any other superclass) is emitted as the JSON
  ``null`` value.

* ``int`` and ``bit`` values are emitted as numbers. Note that
  TableGen ``int`` values are capable of holding integers too large to
  be exactly representable in IEEE double precision. The integer
  literal in the JSON output will show the full exact integer value.
  So if you need to retrieve large integers with full precision, you
  should use a JSON reader capable of translating such literals back
  into 64-bit integers without losing precision, such as Python's
  standard ``json`` module.

* ``string`` and ``code`` values are emitted as JSON strings.

* ``list<T>`` values, for any element type ``T``, are emitted as JSON
  arrays. Each element of the array is represented in turn using these
  same conventions.

* ``bits`` values are also emitted as arrays. A ``bits`` array is
  ordered from least-significant bit to most-significant. So the
  element with index ``i`` corresponds to the bit described as
  ``x{i}`` in TableGen source. However, note that this means that
  scripting languages are likely to *display* the array in the
  opposite order from the way it appears in the TableGen source or in
  the diagnostic ``-print-records`` output.

All other TableGen value types are emitted as a JSON object,
containing two standard fields: ``kind`` is a discriminator describing
which kind of value the object represents, and ``printable`` is a
string giving the same representation of the value that would appear
in ``-print-records``.

* A reference to a ``def`` object has ``kind=="def"``, and has an
  extra field ``def`` giving the name of the object referred to.

* A reference to another variable in the same record has
  ``kind=="var"``, and has an extra field ``var`` giving the name of
  the variable referred to.

* A reference to a specific bit of a ``bits``-typed variable in the
  same record has ``kind=="varbit"``, and has two extra fields:
  ``var`` gives the name of the variable referred to, and ``index``
  gives the index of the bit.

* A value of type ``dag`` has ``kind=="dag"``, and has two extra
  fields. ``operator`` gives the initial value after the opening
  parenthesis of the dag initializer; ``args`` is an array giving the
  following arguments. The elements of ``args`` are arrays of length
  2, giving the value of each argument followed by its colon-suffixed
  name (if any). For example, in the JSON representation of the dag
  value ``(Op 22, "hello":$foo)`` (assuming that ``Op`` is the name of
  a record defined elsewhere with a ``def`` statement):

  * ``operator`` will be an object in which ``kind=="def"`` and
    ``def=="Op"``

  * ``args`` will be the array ``[[22, null], ["hello", "foo"]]``.

* If any other kind of value or complicated expression appears in the
  output, it will have ``kind=="complex"``, and no additional fields.
  These values are not expected to be needed by backends. The standard
  ``printable`` field can be used to extract a representation of them
  in TableGen source syntax if necessary.

SearchableTables Reference
--------------------------

A TableGen include file, ``SearchableTable.td``, provides classes for
generating C++ searchable tables. These tables are described in the
following sections. To generate the C++ code, run ``llvm-tblgen`` with the
``--gen-searchable-tables`` option, which invokes the backend that generates
the tables from the records you provide.

Each of the data structures generated for searchable tables is guarded by an
``#ifdef``. This allows you to include the generated ``.inc`` file and select only
certain data structures for inclusion. The examples below show the macro
names used in these guards.

Generic Enumerated Types
~~~~~~~~~~~~~~~~~~~~~~~~

The ``GenericEnum`` class makes it easy to define a C++ enumerated type and
the enumerated *elements* of that type. To define the type, define a record
whose parent class is ``GenericEnum`` and whose name is the desired enum
type. This class provides three fields, which you can set in the record
using the ``let`` statement.

* ``string FilterClass``. The enum type will have one element for each record
  that derives from this class. These records are collected to assemble the
  complete set of elements.

* ``string NameField``. The name of a field *in the collected records* that specifies
  the name of the element. If a record has no such field, the record's
  name will be used.

* ``string ValueField``. The name of a field *in the collected records* that
  specifies the numerical value of the element. If a record has no such
  field, it will be assigned an integer value. Values are assigned in
  alphabetical order starting with 0.

Here is an example where the values of the elements are specified
explicitly, as a template argument to the ``BEntry`` class. The resulting
C++ code is shown.

.. code-block:: text

  def BValues : GenericEnum {
    let FilterClass = "BEntry";
    let NameField = "Name";
    let ValueField = "Encoding";
  }

  class BEntry<bits<16> enc> {
    string Name = NAME;
    bits<16> Encoding = enc;
  }

  def BFoo   : BEntry<0xac>;
  def BBar   : BEntry<0x14>;
  def BZoo   : BEntry<0x80>;
  def BSnork : BEntry<0x4c>;

.. code-block:: text

  #ifdef GET_BValues_DECL
  enum BValues {
    BBar = 20,
    BFoo = 172,
    BSnork = 76,
    BZoo = 128,
  };
  #endif

In the following example, the values of the elements are assigned
automatically. Note that values are assigned from 0, in alphabetical order
by element name.

.. code-block:: text

  def CEnum : GenericEnum {
    let FilterClass = "CEnum";
  }

  class CEnum;

  def CFoo : CEnum;
  def CBar : CEnum;
  def CBaz : CEnum;

.. code-block:: text

  #ifdef GET_CEnum_DECL
  enum CEnum {
    CBar = 0,
    CBaz = 1,
    CFoo = 2,
  };
  #endif


Generic Tables
~~~~~~~~~~~~~~

The ``GenericTable`` class is used to define a searchable generic table.
TableGen produces C++ code to define the table entries and also produces
the declaration and definition of a function to search the table based on a
primary key. To define the table, define a record whose parent class is
``GenericTable`` and whose name is the name of the global table of entries.
This class provides six fields.

* ``string FilterClass``. The table will have one entry for each record
  that derives from this class.

* ``string FilterClassField``. This is an optional field of ``FilterClass``
  which should be `bit` type. If specified, only those records with this field
  being true will have corresponding entries in the table. This field won't be
  included in generated C++ fields if it isn't included in ``Fields`` list.

* ``string CppTypeName``. The name of the C++ struct/class type of the
  table that holds the entries. If unspecified, the ``FilterClass`` name is
  used.

* ``list<string> Fields``. A list of the names of the fields *in the
  collected records* that contain the data for the table entries. The order of
  this list determines the order of the values in the C++ initializers. See
  below for information about the types of these fields.

* ``list<string> PrimaryKey``. The list of fields that make up the
  primary key.

* ``string PrimaryKeyName``. The name of the generated C++ function
  that performs a lookup on the primary key.

* ``bit PrimaryKeyEarlyOut``. See the third example below.

* ``bit PrimaryKeyReturnRange``. when set to 1, modifies the lookup function’s
  definition to return a range of results rather than a single pointer to the
  object. This feature proves useful when multiple objects meet the criteria
  specified by the lookup function. Currently, it is supported only for primary
  lookup functions. Refer to the second example below for further details.

TableGen attempts to deduce the type of each of the table fields so that it
can format the C++ initializers in the emitted table. It can deduce ``bit``,
``bits<n>``, ``string``, ``Intrinsic``, and ``Instruction``.  These can be
used in the primary key. Any other field types must be specified
explicitly; this is done as shown in the second example below. Such fields
cannot be used in the primary key.

One special case of the field type has to do with code. Arbitrary code is
represented by a string, but has to be emitted as a C++ initializer without
quotes. If the code field was defined using a code literal (``[{...}]``),
then TableGen will know to emit it without quotes. However, if it was
defined using a string literal or complex string expression, then TableGen
will not know. In this case, you can force TableGen to treat the field as
code by including the following line in the ``GenericTable`` record, where
*xxx* is the code field name.

.. code-block:: text

  string TypeOf_xxx = "code";

Here is an example where TableGen can deduce the field types. Note that the
table entry records are anonymous; the names of entry records are
irrelevant.

.. code-block:: text

  def ATable : GenericTable {
    let FilterClass = "AEntry";
    let FilterClassField = "IsNeeded";
    let Fields = ["Str", "Val1", "Val2"];
    let PrimaryKey = ["Val1", "Val2"];
    let PrimaryKeyName = "lookupATableByValues";
  }

  class AEntry<string str, int val1, int val2, bit isNeeded> {
    string Str = str;
    bits<8> Val1 = val1;
    bits<10> Val2 = val2;
    bit IsNeeded = isNeeded;
  }

  def : AEntry<"Bob",   5, 3, 1>;
  def : AEntry<"Carol", 2, 6, 1>;
  def : AEntry<"Ted",   4, 4, 1>;
  def : AEntry<"Alice", 4, 5, 1>;
  def : AEntry<"Costa", 2, 1, 1>;
  def : AEntry<"Dale",  2, 1, 0>;

Here is the generated C++ code. The declaration of ``lookupATableByValues``
is guarded by ``GET_ATable_DECL``, while the definitions are guarded by
``GET_ATable_IMPL``.

.. code-block:: text

  #ifdef GET_ATable_DECL
  const AEntry *lookupATableByValues(uint8_t Val1, uint16_t Val2);
  #endif

  #ifdef GET_ATable_IMPL
  constexpr AEntry ATable[] = {
    { "Costa", 0x2, 0x1 }, // 0
    { "Carol", 0x2, 0x6 }, // 1
    { "Ted", 0x4, 0x4 }, // 2
    { "Alice", 0x4, 0x5 }, // 3
    { "Bob", 0x5, 0x3 }, // 4
    /* { "Dale", 0x2, 0x1 }, // 5 */ // We don't generate this line as `IsNeeded` is 0.
  };

  const AEntry *lookupATableByValues(uint8_t Val1, uint16_t Val2) {
    struct KeyType {
      uint8_t Val1;
      uint16_t Val2;
    };
    KeyType Key = { Val1, Val2 };
    auto Table = ArrayRef(ATable);
    auto Idx = std::lower_bound(Table.begin(), Table.end(), Key,
      [](const AEntry &LHS, const KeyType &RHS) {
        if (LHS.Val1 < RHS.Val1)
          return true;
        if (LHS.Val1 > RHS.Val1)
          return false;
        if (LHS.Val2 < RHS.Val2)
          return true;
        if (LHS.Val2 > RHS.Val2)
          return false;
        return false;
      });

    if (Idx == Table.end() ||
        Key.Val1 != Idx->Val1 ||
        Key.Val2 != Idx->Val2)
      return nullptr;
    return &*Idx;
  }
  #endif

The table entries in ``ATable`` are sorted in order by ``Val1``, and within
each of those values, by ``Val2``. This allows a binary search of the table,
which is performed in the lookup function by ``std::lower_bound``. The
lookup function returns a reference to the found table entry, or the null
pointer if no entry is found. If the table has a single primary key field
which is integral and densely numbered, a direct lookup is generated rather
than a binary search.

This example includes a field whose type TableGen cannot deduce. The ``Kind``
field uses the enumerated type ``CEnum`` defined above. To inform TableGen
of the type, the record derived from ``GenericTable`` must include a string field
named ``TypeOf_``\ *field*, where *field* is the name of the field whose type
is required.

.. code-block:: text

  def CTable : GenericTable {
    let FilterClass = "CEntry";
    let Fields = ["Name", "Kind", "Encoding"];
    string TypeOf_Kind = "CEnum";
    let PrimaryKey = ["Encoding"];
    let PrimaryKeyName = "lookupCEntryByEncoding";
  }

  class CEntry<string name, CEnum kind, int enc> {
    string Name = name;
    CEnum Kind = kind;
    bits<16> Encoding = enc;
  }

  def : CEntry<"Apple", CFoo, 10>;
  def : CEntry<"Pear",  CBaz, 15>;
  def : CEntry<"Apple", CBar, 13>;

Here is the generated C++ code.

.. code-block:: text

  #ifdef GET_CTable_DECL
  const CEntry *lookupCEntryByEncoding(uint16_t Encoding);
  #endif

  #ifdef GET_CTable_IMPL
  constexpr CEntry CTable[] = {
    { "Apple", CFoo, 0xA }, // 0
    { "Apple", CBar, 0xD }, // 1
    { "Pear", CBaz, 0xF }, // 2
  };

  const CEntry *lookupCEntryByEncoding(uint16_t Encoding) {
    struct KeyType {
      uint16_t Encoding;
    };
    KeyType Key = { Encoding };
    auto Table = ArrayRef(CTable);
    auto Idx = std::lower_bound(Table.begin(), Table.end(), Key,
      [](const CEntry &LHS, const KeyType &RHS) {
        if (LHS.Encoding < RHS.Encoding)
          return true;
        if (LHS.Encoding > RHS.Encoding)
          return false;
        return false;
      });

    if (Idx == Table.end() ||
        Key.Encoding != Idx->Encoding)
      return nullptr;
    return &*Idx;
  }

In the above example, lets add one more record with encoding same as that of
record ``CEntry<"Pear",  CBaz, 15>``.

.. code-block:: text

  def CFoobar : CEnum;
  def : CEntry<"Banana", CFoobar, 15>;

Below is the new generated ``CTable``

.. code-block:: text

  #ifdef GET_Table_IMPL
  constexpr CEntry Table[] = {
    { "Apple", CFoo, 0xA }, // 0
    { "Apple", CBar, 0xD }, // 1
    { "Banana", CFoobar, 0xF }, // 2
    { "Pear", CBaz, 0xF }, // 3
  };

Since ``Banana`` lexicographically appears first, therefore in the ``CEntry``
table, record with name ``Banana`` will come before the record with name
``Pear``. Because of this, the ``lookupCEntryByEncoding`` function will always
return a pointer to the record with name ``Banana`` even though in some cases
the correct result can be the record with name ``Pear``. Such kind of scenario
makes the exisitng lookup function insufficient because they always return a
pointer to a single entry from the table, but instead it should return a range
of results because multiple entries match the criteria sought by the lookup
function. In this case, the definition of the lookup function needs to be
modified to return a range of results which can be done by setting
``PrimaryKeyReturnRange``.

.. code-block:: text

  def CTable : GenericTable {
    let FilterClass = "CEntry";
    let Fields = ["Name", "Kind", "Encoding"];
    string TypeOf_Kind = "CEnum";
    let PrimaryKey = ["Encoding"];
    let PrimaryKeyName = "lookupCEntryByEncoding";
    let PrimaryKeyReturnRange = true;
  }

Here is the modified lookup function.

.. code-block:: text

  llvm::iterator_range<const CEntry *> lookupCEntryByEncoding(uint16_t Encoding) {
    struct KeyType {
      uint16_t Encoding;
    };
    KeyType Key = {Encoding};
    struct Comp {
      bool operator()(const CEntry &LHS, const KeyType &RHS) const {
        if (LHS.Encoding < RHS.Encoding)
          return true;
        if (LHS.Encoding > RHS.Encoding)
          return false;
        return false;
      }
      bool operator()(const KeyType &LHS, const CEntry &RHS) const {
        if (LHS.Encoding < RHS.Encoding)
          return true;
        if (LHS.Encoding > RHS.Encoding)
          return false;
        return false;
      }
    };
    auto Table = ArrayRef(Table);
    auto It = std::equal_range(Table.begin(), Table.end(), Key, Comp());
    return llvm::make_range(It.first, It.second);
  }

The new lookup function will return an iterator range with first pointer to the
first result and the last pointer to the last matching result from the table.
However, please note that the support for emitting modified definition exists
for ``PrimaryKeyName`` only.

The ``PrimaryKeyEarlyOut`` field, when set to 1, modifies the lookup
function so that it tests the first field of the primary key to determine
whether it is within the range of the collected records' primary keys. If
not, the function returns the null pointer without performing the binary
search. This is useful for tables that provide data for only some of the
elements of a larger enum-based space. The first field of the primary key
must be an integral type; it cannot be a string.

Adding ``let PrimaryKeyEarlyOut = 1`` to the ``ATable`` above:

.. code-block:: text

  def ATable : GenericTable {
    let FilterClass = "AEntry";
    let Fields = ["Str", "Val1", "Val2"];
    let PrimaryKey = ["Val1", "Val2"];
    let PrimaryKeyName = "lookupATableByValues";
    let PrimaryKeyEarlyOut = 1;
  }

causes the lookup function to change as follows:

.. code-block:: text

  const AEntry *lookupATableByValues(uint8_t Val1, uint16_t Val2) {
    if ((Val1 < 0x2) ||
        (Val1 > 0x5))
      return nullptr;

    struct KeyType {
    ...

We can construct two GenericTables with the same ``FilterClass``, so that they
select from the same overall set of records, but assign them with different
``FilterClassField`` values so that they include different subsets of the
records of that class.

For example, we can create two tables that contain only even or odd records.
Fields ``IsEven`` and ``IsOdd`` won't be included in generated C++ fields
because they aren't included in ``Fields`` list.

.. code-block:: text

  class EEntry<bits<8> value> {
    bits<8> Value = value;
    bit IsEven = !eq(!and(value, 1), 0);
    bit IsOdd = !not(IsEven);
  }

  foreach i = {1-10} in {
    def : EEntry<i>;
  }

  def EEntryEvenTable : GenericTable {
    let FilterClass = "EEntry";
    let FilterClassField = "IsEven";
    let Fields = ["Value"];
    let PrimaryKey = ["Value"];
    let PrimaryKeyName = "lookupEEntryEvenTableByValue";
  }

  def EEntryOddTable : GenericTable {
    let FilterClass = "EEntry";
    let FilterClassField = "IsOdd";
    let Fields = ["Value"];
    let PrimaryKey = ["Value"];
    let PrimaryKeyName = "lookupEEntryOddTableByValue";
  }

The generated tables are:

.. code-block:: text

  constexpr EEntry EEntryEvenTable[] = {
    { 0x2 }, // 0
    { 0x4 }, // 1
    { 0x6 }, // 2
    { 0x8 }, // 3
    { 0xA }, // 4
  };

  constexpr EEntry EEntryOddTable[] = {
    { 0x1 }, // 0
    { 0x3 }, // 1
    { 0x5 }, // 2
    { 0x7 }, // 3
    { 0x9 }, // 4
  };

Search Indexes
~~~~~~~~~~~~~~

The ``SearchIndex`` class is used to define additional lookup functions for
generic tables. To define an additional function, define a record whose parent
class is ``SearchIndex`` and whose name is the name of the desired lookup
function. This class provides three fields.

* ``GenericTable Table``. The name of the table that is to receive another
  lookup function.

* ``list<string> Key``. The list of fields that make up the secondary key.

* ``bit EarlyOut``. See the third example in `Generic Tables`_.

* ``bit ReturnRange``. See the second example in `Generic Tables`_.

Here is an example of a secondary key added to the ``CTable`` above. The
generated function looks up entries based on the ``Name`` and ``Kind`` fields.

.. code-block:: text

  def lookupCEntry : SearchIndex {
    let Table = CTable;
    let Key = ["Name", "Kind"];
  }

This use of ``SearchIndex`` generates the following additional C++ code.

.. code-block:: text

  const CEntry *lookupCEntry(StringRef Name, unsigned Kind);

  ...

  const CEntry *lookupCEntryByName(StringRef Name, unsigned Kind) {
    struct IndexType {
      const char * Name;
      unsigned Kind;
      unsigned _index;
    };
    static const struct IndexType Index[] = {
      { "APPLE", CBar, 1 },
      { "APPLE", CFoo, 0 },
      { "PEAR", CBaz, 2 },
    };

    struct KeyType {
      std::string Name;
      unsigned Kind;
    };
    KeyType Key = { Name.upper(), Kind };
    auto Table = ArrayRef(Index);
    auto Idx = std::lower_bound(Table.begin(), Table.end(), Key,
      [](const IndexType &LHS, const KeyType &RHS) {
        int CmpName = StringRef(LHS.Name).compare(RHS.Name);
        if (CmpName < 0) return true;
        if (CmpName > 0) return false;
        if ((unsigned)LHS.Kind < (unsigned)RHS.Kind)
          return true;
        if ((unsigned)LHS.Kind > (unsigned)RHS.Kind)
          return false;
        return false;
      });

    if (Idx == Table.end() ||
        Key.Name != Idx->Name ||
        Key.Kind != Idx->Kind)
      return nullptr;
    return &CTable[Idx->_index];
  }
