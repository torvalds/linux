===================================
TableGen Backend Developer's Guide
===================================

.. sectnum::

.. contents::
   :local:

Introduction
============

The purpose of TableGen is to generate complex output files based on
information from source files that are significantly easier to code than the
output files would be, and also easier to maintain and modify over time. The
information is coded in a declarative style involving classes and records,
which are then processed by TableGen. The internalized records are passed on
to various backends, which extract information from a subset of the records
and generate an output file. These output files are typically ``.inc`` files
for C++, but may be any type of file that the backend developer needs.

This document is a guide to writing a backend for TableGen. It is not a
complete reference manual, but rather a guide to using the facilities
provided by TableGen for the backends. For a complete reference to the
various data structures and functions involved, see the primary TableGen
header file (``record.h``) and/or the Doxygen documentation.

This document assumes that you have read the :doc:`TableGen Programmer's
Reference <./ProgRef>`, which provides a detailed reference for coding
TableGen source files. For a description of the existing backends, see
:doc:`TableGen BackEnds <./BackEnds>`.

Data Structures
===============

The following sections describe the data structures that contain the classes
and records that are collected from the TableGen source files by the
TableGen parser. Note that the term *class* refers to an abstract record
class, while the term *record* refers to a concrete record.

Unless otherwise noted, functions associated with classes are instance
functions.

``RecordKeeper``
----------------

An instance of the ``RecordKeeper`` class acts as the container for all the
classes and records parsed and collected by TableGen. The ``RecordKeeper``
instance is passed to the backend when it is invoked by TableGen. This class
is usually abbreviated ``RK``.

There are two maps in the recordkeeper, one for classes and one for records
(the latter often referred to as *defs*). Each map maps the class or record
name to an instance of the ``Record`` class (see `Record`_), which contains
all the information about that class or record.

In addition to the two maps, the ``RecordKeeper`` instance contains:

* A map that maps the names of global variables to their values.
  Global variables are defined in TableGen files with outer
  ``defvar`` statements.

* A counter for naming anonymous records.

The ``RecordKeeper`` class provides a few useful functions.

* Functions to get the complete class and record maps.

* Functions to get a subset of the records based on their parent classes.

* Functions to get individual classes, records, and globals, by name.

A ``RecordKeeper`` instance can be printed to an output stream with the ``<<``
operator.

``Record``
----------

Each class or record built by TableGen is represented by an instance of
the ``Record`` class. The ``RecordKeeper`` instance contains one map for the
classes and one for the records. The primary data members of a record are
the record name, the vector of field names and their values, and the vector of
superclasses of the record.

The record name is stored as a pointer to an ``Init`` (see `Init`_), which
is a class whose instances hold TableGen values (sometimes referred to as
*initializers*). The field names and values are stored in a vector of
``RecordVal`` instances (see `RecordVal`_), each of which contains both the
field name and its value. The superclass vector contains a sequence of
pairs, with each pair including the superclass record and its source
file location.

In addition to those members, a ``Record`` instance contains:

* A vector of source file locations that includes the record definition
  itself, plus the locations of any multiclasses involved in its definition.

* For a class record, a vector of the class's template arguments.

* An instance of ``DefInit`` (see `DefInit`_) corresponding to this record.

* A unique record ID.

* A boolean that specifies whether this is a class definition.

* A boolean that specifies whether this is an anonymous record.

The ``Record`` class provides many useful functions.

* Functions to get the record name, fields, source file locations,
  template arguments, and unique ID.

* Functions to get all the record's superclasses or just its direct
  superclasses.

* Functions to get a particular field value by specifying its name in various
  forms, and returning its value in various forms
  (see `Getting Record Names and Fields`_).

* Boolean functions to check the various attributes of the record.

A ``Record`` instance can be printed to an output stream with the ``<<``
operator.


``RecordVal``
-------------

Each field of a record is stored in an instance of the ``RecordVal`` class.
The ``Record`` instance includes a vector of these value instances. A
``RecordVal`` instance contains the name of the field, stored in an ``Init``
instance. It also contains the value of the field, likewise stored in an
``Init``. (A better name for this class might be ``RecordField``.)

In addition to those primary members, the ``RecordVal`` has other data members.

* The source file location of the field definition.

* The type of the field, stored as an instance
  of the ``RecTy`` class (see `RecTy`_).

The ``RecordVal`` class provides some useful functions.

* Functions to get the name of the field in various forms.

* A function to get the type of the field.

* A function to get the value of the field.

* A function to get the source file location.

Note that field values are more easily obtained directly from the ``Record``
instance (see `Record`_).

A ``RecordVal`` instance can be printed to an output stream with the ``<<``
operator.

``RecTy``
---------

The ``RecTy`` class is used to represent the types of field values. It is
the base class for a series of subclasses, one for each of the
available field types. The ``RecTy`` class has one data member that is an
enumerated type specifying the specific type of field value. (A better
name for this class might be ``FieldTy``.)

The ``RecTy`` class provides a few useful functions.

* A virtual function to get the type name as a string.

* A virtual function to check whether all the values of this type can
  be converted to another given type.

* A virtual function to check whether this type is a subtype of
  another given type.

* A function to get the corresponding ``list``
  type for lists with elements of this type. For example, the function
  returns the ``list<int>`` type when called with the ``int`` type.

The subclasses that inherit from ``RecTy`` are
``BitRecTy``,
``BitsRecTy``,
``CodeRecTy``,
``DagRecTy``,
``IntRecTy``,
``ListRecTy``,
``RecordRecTy``, and
``StringRecTy``.
Some of these classes have additional members that
are described in the following subsections.

*All* of the classes derived from ``RecTy`` provide the ``get()`` function.
It returns an instance of ``Recty`` corresponding to the derived class.
Some of the ``get()`` functions require an argument to
specify which particular variant of the type is desired. These arguments are
described in the following subsections.

A ``RecTy`` instance can be printed to an output stream with the ``<<``
operator.

.. warning::
  It is not specified whether there is a single ``RecTy`` instance of a
  particular type or multiple instances.


``BitsRecTy``
~~~~~~~~~~~~~

This class includes a data member with the size of the ``bits`` value and a
function to get that size.

The ``get()`` function takes the length of the sequence, *n*, and returns the
``BitsRecTy`` type corresponding to ``bits<``\ *n*\ ``>``.

``ListRecTy``
~~~~~~~~~~~~~

This class includes a data member that specifies the type of the list's
elements and a function to get that type.

The ``get()`` function takes the ``RecTy`` *type* of the list members and
returns the ``ListRecTy`` type corresponding to ``list<``\ *type*\ ``>``.


``RecordRecTy``
~~~~~~~~~~~~~~~

This class includes data members that contain the list of parent classes of
this record. It also provides a function to obtain the array of classes and
two functions to get the iterator ``begin()`` and ``end()`` values. The
class defines a type for the return values of the latter two functions.

.. code-block:: text

  using const_record_iterator = Record * const *;

The ``get()`` function takes an ``ArrayRef`` of pointers to the ``Record``
instances of the *direct* superclasses of the record and returns the ``RecordRecTy``
corresponding to the record inheriting from those superclasses.

``Init``
--------

The ``Init`` class is used to represent TableGen values.  The name derives
from *initialization value*. This class should not be confused with the
``RecordVal`` class, which represents record fields, both their names and
values. The ``Init`` class is the base class for a series of subclasses, one
for each of the available value types. The primary data member of ``Init``
is an enumerated type that represents the specific type of the value.

The ``Init`` class provides a few useful functions.

* A function to get the type enumerator.

* A boolean virtual function to determine whether a value is completely
  specified; that is, has no uninitialized subvalues.

* Virtual functions to get the value as a string.

* Virtual functions to cast the value to other types, implement the bit
  range feature of TableGen, and implement the list slice feature.

* A virtual function to get a particular bit of the value.

The subclasses that inherit directly from ``Init`` are
``UnsetInit`` and ``TypedInit``.

An ``Init`` instance can be printed to an output stream with the ``<<``
operator.

.. warning::
  It is not specified whether two separate initialization values with
  the same underlying type and value (e.g., two strings with the value
  "Hello") are represented by two ``Init``\ s or share the same ``Init``.

``UnsetInit``
~~~~~~~~~~~~~

This class, a subclass of ``Init``, represents the unset (uninitialized)
value. The static function ``get()`` can be used to obtain the singleton
``Init`` of this type.


``TypedInit``
~~~~~~~~~~~~~

This class, a subclass of ``Init``, acts as the parent class of the classes
that represent specific value types (except for the unset value). These
classes include ``BitInit``, ``BitsInit``, ``DagInit``, ``DefInit``,
``IntInit``, ``ListInit``, and ``StringInit``. (There are additional derived
types used by the TableGen parser.)

This class includes a data member that specifies the ``RecTy`` type of the
value. It provides a function to get that ``RecTy`` type.

``BitInit``
~~~~~~~~~~~

The ``BitInit`` class is a subclass of ``TypedInit``. Its instances
represent the possible values of a bit: 0 or 1. It includes a data member
that contains the bit.

*All* of the classes derived from ``TypedInit`` provide the following functions.

* A static function named ``get()`` that returns an ``Init`` representing
  the specified value(s). In the case of ``BitInit``, ``get(true)`` returns
  an instance of ``BitInit`` representing true, while ``get(false)`` returns
  an instance
  representing false. As noted above, it is not specified whether there
  is exactly one or more than one ``BitInit`` representing true (or false).

* A function named ``GetValue()`` that returns the value of the instance
  in a more direct form, in this case as a ``bool``.

``BitsInit``
~~~~~~~~~~~~

The ``BitsInit`` class is a subclass of ``TypedInit``. Its instances
represent sequences of bits, from high-order to low-order. It includes a
data member with the length of the sequence and a vector of pointers to
``Init`` instances, one per bit.

The class provides the usual ``get()`` function. It does not provide the
``getValue()`` function.

The class provides the following additional functions.

* A function to get the number of bits in the sequence.

* A function that gets a bit specified by an integer index.

``DagInit``
~~~~~~~~~~~

The ``DagInit`` class is a subclass of ``TypedInit``. Its instances
represent the possible direct acyclic graphs (``dag``).

The class includes a pointer to an ``Init`` for the DAG operator and a
pointer to a ``StringInit`` for the operator name. It includes the count of
DAG operands and the count of operand names. Finally, it includes a vector of
pointers to ``Init`` instances for the operands and another to
``StringInit`` instances for the operand names.
(The DAG operands are also referred to as *arguments*.)

The class provides two forms of the usual ``get()`` function. It does not
provide the usual ``getValue()`` function.

The class provides many additional functions:

* Functions to get the operator in various forms and to get the
  operator name in various forms.

* Functions to determine whether there are any operands and to get the
  number of operands.

* Functions to the get the operands, both individually and together.

* Functions to determine whether there are any names and to
  get the number of names

* Functions to the get the names, both individually and together.

* Functions to get the operand iterator ``begin()`` and ``end()`` values.

* Functions to get the name iterator ``begin()`` and ``end()`` values.

The class defines two types for the return values of the operand and name
iterators.

.. code-block:: text

  using const_arg_iterator = SmallVectorImpl<Init*>::const_iterator;
  using const_name_iterator = SmallVectorImpl<StringInit*>::const_iterator;


``DefInit``
~~~~~~~~~~~

The ``DefInit`` class is a subclass of ``TypedInit``. Its instances
represent the records that were collected by TableGen. It includes a data
member that is a pointer to the record's ``Record`` instance.

The class provides the usual ``get()`` function. It does not provide
``getValue()``. Instead, it provides ``getDef()``, which returns the
``Record`` instance.

``IntInit``
~~~~~~~~~~~

The ``IntInit`` class is a subclass of ``TypedInit``. Its instances
represent the possible values of a 64-bit integer. It includes a data member
that contains the integer.

The class provides the usual ``get()`` and ``getValue()`` functions. The
latter function returns the integer as an ``int64_t``.

The class also provides a function, ``getBit()``, to obtain a specified bit
of the integer value.

``ListInit``
~~~~~~~~~~~~

The ``ListInit`` class is a subclass of ``TypedInit``. Its instances
represent lists of elements of some type. It includes a data member with the
length of the list and a vector of pointers to ``Init`` instances, one per
element.

The class provides the usual ``get()`` and ``getValues()`` functions. The
latter function returns an ``ArrayRef`` of the vector of pointers to ``Init``
instances.

The class provides these additional functions.

* A function to get the element type.

* Functions to get the length of the vector and to determine whether
  it is empty.

* Functions to get an element specified by an integer index and return
  it in various forms.

* Functions to get the iterator ``begin()`` and ``end()`` values. The
  class defines a type for the return type of these two functions.

.. code-block:: text

  using const_iterator = Init *const *;


``StringInit``
~~~~~~~~~~~~~~

The ``StringInit`` class is a subclass of ``TypedInit``. Its instances
represent arbitrary-length strings. It includes a data member
that contains a ``StringRef`` of the value.

The class provides the usual ``get()`` and ``getValue()`` functions. The
latter function returns the ``StringRef``.

Creating a New Backend
======================

The following steps are required to create a new backend for TableGen.

#. Invent a name for your backend C++ file, say ``GenAddressModes``.

#. Write the new backend, using the file ``TableGenBackendSkeleton.cpp``
   as a starting point.

#. Determine which instance of TableGen requires the new backend. There is
   one instance for Clang and another for LLVM. Or you may be building
   your own instance.

#. Add your backend C++ file to the appropriate ``CMakeLists.txt`` file so
   that it will be built.

#. Add your C++ file to the system.

The Backend Skeleton
====================

The file ``TableGenBackendSkeleton.cpp`` provides a skeleton C++ translation
unit for writing a new TableGen backend. Here are a few notes on the file.

* The list of includes is the minimal list required by most backends.

* As with all LLVM C++ files, it has a ``using namespace llvm;`` statement.
  It also has an anonymous namespace that contains all the file-specific
  data structure definitions, along with the class embodying the emitter
  data members and functions. Continuing with the ``GenAddressModes`` example,
  this class is named ``AddressModesEmitter``.

* The constructor for the emitter class accepts a ``RecordKeeper`` reference,
  typically named ``RK``. The ``RecordKeeper`` reference is saved in a data
  member so that records can be obtained from it. This data member is usually
  named ``Records``.

* One function is named ``run``. It is invoked by the backend's "main
  function" to collect records and emit the output file. It accepts an instance
  of the ``raw_ostream`` class, typically named ``OS``. The output file is
  emitted by writing to this stream.

* The ``run`` function should use the ``emitSourceFileHeader`` helper function
  to include a standard header in the emitted file.

* Register the class or the function as the command line option
  with ``llvm/TableGen/TableGenBackend.h``.

  * Use ``llvm::TableGen::Emitter::OptClass<AddressModesEmitter>``
    if the class has the constructor ``(RK)`` and
    the method ``run(OS)``.

  * Otherwise, use ``llvm::TableGen::Emitter::Opt``.

All the examples in the remainder of this document will assume the naming
conventions used in the skeleton file.

Getting Classes
===============

The ``RecordKeeper`` class provides two functions for getting the
``Record`` instances for classes defined in the TableGen files.

* ``getClasses()`` returns a ``RecordMap`` reference for all the classes.

* ``getClass(``\ *name*\ ``)`` returns a ``Record`` reference for the named
  class.

If you need to iterate over all the class records:

.. code-block:: text

  for (auto ClassPair : Records.getClasses()) {
    Record *ClassRec = ClassPair.second.get();
    ...
  }

``ClassPair.second`` gets the class's ``unique_ptr``, then ``.get()`` gets the
class ``Record`` itself.


Getting Records
===============

The ``RecordKeeper`` class provides four functions for getting the
``Record`` instances for concrete records defined in the TableGen files.

* ``getDefs()`` returns a ``RecordMap`` reference for all the concrete
  records.

* ``getDef(``\ *name*\ ``)`` returns a ``Record`` reference for the named
  concrete record.

* ``getAllDerivedDefinitions(``\ *classname*\ ``)`` returns a vector of
  ``Record`` references for the concrete records that derive from the
  given class.

* ``getAllDerivedDefinitions(``\ *classnames*\ ``)`` returns
  a vector of ``Record`` references for the concrete records that derive from
  *all* of the given classes.

This statement obtains all the records that derive from the ``Attribute``
class and iterates over them.

.. code-block:: text

  auto AttrRecords = Records.getAllDerivedDefinitions("Attribute");
  for (Record *AttrRec : AttrRecords) {
    ...
  }

Getting Record Names and Fields
===============================

As described above (see `Record`_), there are multiple functions that
return the name of a record. One particularly useful one is
``getNameInitAsString()``, which returns the name as a ``std::string``.

There are also multiple functions that return the fields of a record. To
obtain and iterate over all the fields:

.. code-block:: text

  for (const RecordVal &Field : SomeRec->getValues()) {
    ...
  }

You will recall that ``RecordVal`` is the class whose instances contain
information about the fields in records.

The ``getValue()`` function returns the ``RecordVal`` instance for a field
specified by name. There are multiple overloaded functions, some taking a
``StringRef`` and others taking a ``const Init *``. Some functions return a
``RecordVal *`` and others return a ``const RecordVal *``. If the field does
not exist, a fatal error message is printed.

More often than not, you are interested in the value of the field, not all
the information in the ``RecordVal``. There is a large set of functions that
take a field name in some form and return its value. One function,
``getValueInit``, returns the value as an ``Init *``. Another function,
``isValueUnset``, returns a boolean specifying whether the value is unset
(uninitialized).

Most of the functions return the value in some more useful form. For
example:

.. code-block:: text

  std::vector<int64_t> RegCosts =
      SomeRec->getValueAsListOfInts("RegCosts");

The field ``RegCosts`` is assumed to be a list of integers. That list is
returned as a ``std::vector`` of 64-bit integers. If the field is not a list
of integers, a fatal error message is printed.

Here is a function that returns a field value as a ``Record``, but returns
null if the field does not exist.

.. code-block:: text

  if (Record *BaseRec = SomeRec->getValueAsOptionalDef(BaseFieldName)) {
    ...
  }

The field is assumed to have another record as its value. That record is returned
as a pointer to a ``Record``. If the field does not exist or is unset, the
functions returns null.

Getting Record Superclasses
===========================

The ``Record`` class provides a function to obtain the superclasses of a
record. It is named ``getSuperClasses`` and returns an ``ArrayRef`` of an
array of ``std::pair`` pairs. The superclasses are in post-order: the order
in which the superclasses were visited while copying their fields into the
record. Each pair consists of a pointer to the ``Record`` instance for a
superclass record and an instance of the ``SMRange`` class. The range
indicates the source file locations of the beginning and end of the class
definition.

This example obtains the superclasses of the ``Prototype`` record and then
iterates over the pairs in the returned array.

.. code-block:: text

  ArrayRef<std::pair<Record *, SMRange>>
      Superclasses = Prototype->getSuperClasses();
  for (const auto &SuperPair : Superclasses) {
    ...
  }

The ``Record`` class also provides a function, ``getDirectSuperClasses``, to
append the *direct* superclasses of a record to a given vector of type
``SmallVectorImpl<Record *>``.

Emitting Text to the Output Stream
==================================

The ``run`` function is passed a ``raw_ostream`` to which it prints the
output file. By convention, this stream is saved in the emitter class member
named ``OS``, although some ``run`` functions are simple and just use the
stream without saving it. The output can be produced by writing values
directly to the output stream, or by using the ``std::format()`` or
``llvm::formatv()`` functions.

.. code-block:: text

  OS << "#ifndef " << NodeName << "\n";

  OS << format("0x%0*x, ", Digits, Value);

Instances of the following classes can be printed using the ``<<`` operator:
``RecordKeeper``,
``Record``,
``RecTy``,
``RecordVal``, and
``Init``.

The helper function ``emitSourceFileHeader()`` prints the header comment
that should be included at the top of every output file. A call to it is
included in the skeleton backend file ``TableGenBackendSkeleton.cpp``.

Printing Error Messages
=======================

TableGen records are often derived from multiple classes and also often
defined through a sequence of multiclasses. Because of this, it can be
difficult for backends to report clear error messages with accurate source
file locations.  To make error reporting easier, five error reporting
functions are provided, each with four overloads.

* ``PrintWarning`` prints a message tagged as a warning.

* ``PrintError`` prints a message tagged as an error.

* ``PrintFatalError`` prints a message tagged as an error and then terminates.

* ``PrintNote`` prints a note. It is often used after one of the previous
  functions to provide more information.

* ``PrintFatalNote`` prints a note and then terminates.

Each of these five functions is overloaded four times.

* ``PrintError(const Twine &Msg)``:
  Prints the message with no source file location.

* ``PrintError(ArrayRef<SMLoc> ErrorLoc, const Twine &Msg)``:
  Prints the message followed by the specified source line,
  along with a pointer to the item in error. The array of
  source file locations is typically taken from a ``Record`` instance.

* ``PrintError(const Record *Rec, const Twine &Msg)``:
  Prints the message followed by the source line associated with the
  specified record (see `Record`_).

* ``PrintError(const RecordVal *RecVal, const Twine &Msg)``:
  Prints the message followed by the source line associated with the
  specified record field (see `RecordVal`_).

Using these functions, the goal is to produce the most specific error report
possible.

Debugging Tools
===============

TableGen provides some tools to aid in debugging backends.

The ``PrintRecords`` Backend
----------------------------

The TableGen command option ``--print-records`` invokes a simple backend
that prints all the classes and records defined in the source files. This is
the default backend option. The format of the output is guaranteed to be
constant over time, so that the output can be compared in tests. The output
looks like this:

.. code-block:: text

  ------------- Classes -----------------
  ...
  class XEntry<string XEntry:str = ?, int XEntry:val1 = ?> { // XBase
    string Str = XEntry:str;
    bits<8> Val1 = { !cast<bits<8>>(XEntry:val1){7}, ... };
    bit Val3 = 1;
  }
  ...
  ------------- Defs -----------------
  def ATable {	// GenericTable
    string FilterClass = "AEntry";
    string CppTypeName = "AEntry";
    list<string> Fields = ["Str", "Val1", "Val2"];
    list<string> PrimaryKey = ["Val1", "Val2"];
    string PrimaryKeyName = "lookupATableByValues";
    bit PrimaryKeyEarlyOut = 0;
  }
  ...
  def anonymous_0 {	// AEntry
    string Str = "Bob";
    bits<8> Val1 = { 0, 0, 0, 0, 0, 1, 0, 1 };
    bits<10> Val2 = { 0, 0, 0, 0, 0, 0, 0, 0, 1, 1 };
  }

Classes are shown with their template arguments, parent classes (following
``//``), and fields. Records are shown with their parent classes and
fields. Note that anonymous records are named ``anonymous_0``,
``anonymous_1``, etc.

The ``PrintDetailedRecords`` Backend
------------------------------------

The TableGen command option ``--print-detailed-records`` invokes a backend
that prints all the global variables, classes, and records defined in the
source files. The format of the output is *not* guaranteed to be constant
over time. The output looks like this.

.. code-block:: text

  DETAILED RECORDS for file llvm-project\llvm\lib\target\arc\arc.td

  -------------------- Global Variables (5) --------------------

  AMDGPUBufferIntrinsics = [int_amdgcn_s_buffer_load, ...
  AMDGPUImageDimAtomicIntrinsics = [int_amdgcn_image_atomic_swap_1d, ...
  ...
  -------------------- Classes (758) --------------------

  AMDGPUBufferLoad  |IntrinsicsAMDGPU.td:879|
    Template args:
      LLVMType AMDGPUBufferLoad:data_ty = llvm_any_ty  |IntrinsicsAMDGPU.td:879|
    Superclasses: (SDPatternOperator) Intrinsic AMDGPURsrcIntrinsic
    Fields:
      list<SDNodeProperty> Properties = [SDNPMemOperand]  |Intrinsics.td:348|
      string LLVMName = ""  |Intrinsics.td:343|
  ...
  -------------------- Records (12303) --------------------

  AMDGPUSample_lz_o  |IntrinsicsAMDGPU.td:560|
    Defm sequence: |IntrinsicsAMDGPU.td:584| |IntrinsicsAMDGPU.td:566|
    Superclasses: AMDGPUSampleVariant
    Fields:
      string UpperCaseMod = "_LZ_O"  |IntrinsicsAMDGPU.td:542|
      string LowerCaseMod = "_lz_o"  |IntrinsicsAMDGPU.td:543|
  ...

* Global variables defined with outer ``defvar`` statements are shown with
  their values.

* The classes are shown with their source location, template arguments,
  superclasses, and fields.

* The records are shown with their source location, ``defm`` sequence,
  superclasses, and fields.

Superclasses are shown in the order processed, with indirect superclasses in
parentheses. Each field is shown with its value and the source location at
which it was set.
The ``defm`` sequence gives the locations of the ``defm`` statements that
were involved in generating the record, in the order they were invoked.

Timing TableGen Phases
----------------------

TableGen provides a phase timing feature that produces a report of the time
used by the various phases of parsing the source files and running the
selected backend. This feature is enabled with the ``--time-phases`` option
of the TableGen command.

If the backend is *not* instrumented for timing, then a report such as the
following is produced. This is the timing for the
``--print-detailed-records`` backend run on the AMDGPU target.

.. code-block:: text

  ===-------------------------------------------------------------------------===
                               TableGen Phase Timing
  ===-------------------------------------------------------------------------===
    Total Execution Time: 101.0106 seconds (102.4819 wall clock)

     ---User Time---   --System Time--   --User+System--   ---Wall Time---  --- Name ---
    85.5197 ( 84.9%)   0.1560 ( 50.0%)  85.6757 ( 84.8%)  85.7009 ( 83.6%)  Backend overall
    15.1789 ( 15.1%)   0.0000 (  0.0%)  15.1789 ( 15.0%)  15.1829 ( 14.8%)  Parse, build records
     0.0000 (  0.0%)   0.1560 ( 50.0%)   0.1560 (  0.2%)   1.5981 (  1.6%)  Write output
    100.6986 (100.0%)   0.3120 (100.0%)  101.0106 (100.0%)  102.4819 (100.0%)  Total

Note that all the time for the backend is lumped under "Backend overall".

If the backend is instrumented for timing, then its processing is
divided into phases and each one timed separately. This is the timing for
the ``--emit-dag-isel`` backend run on the AMDGPU target.

.. code-block:: text

  ===-------------------------------------------------------------------------===
                               TableGen Phase Timing
  ===-------------------------------------------------------------------------===
    Total Execution Time: 746.3868 seconds (747.1447 wall clock)

     ---User Time---   --System Time--   --User+System--   ---Wall Time---  --- Name ---
    657.7938 ( 88.1%)   0.1404 ( 90.0%)  657.9342 ( 88.1%)  658.6497 ( 88.2%)  Emit matcher table
    70.2317 (  9.4%)   0.0000 (  0.0%)  70.2317 (  9.4%)  70.2700 (  9.4%)  Convert to matchers
    14.8825 (  2.0%)   0.0156 ( 10.0%)  14.8981 (  2.0%)  14.9009 (  2.0%)  Parse, build records
     2.1840 (  0.3%)   0.0000 (  0.0%)   2.1840 (  0.3%)   2.1791 (  0.3%)  Sort patterns
     1.1388 (  0.2%)   0.0000 (  0.0%)   1.1388 (  0.2%)   1.1401 (  0.2%)  Optimize matchers
     0.0000 (  0.0%)   0.0000 (  0.0%)   0.0000 (  0.0%)   0.0050 (  0.0%)  Write output
    746.2308 (100.0%)   0.1560 (100.0%)  746.3868 (100.0%)  747.1447 (100.0%)  Total

The backend has been divided into four phases and timed separately.

If you want to instrument a backend, refer to the backend ``DAGISelEmitter.cpp``
and search for ``Records.startTimer``.
