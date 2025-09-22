llvm-debuginfo-analyzer - Print a logical representation of low-level debug information.
========================================================================================

.. program:: llvm-debuginfo-analyzer

.. contents::
   :local:

SYNOPSIS
--------
:program:`llvm-debuginfo-analyzer` [*options*] [*filename ...*]

DESCRIPTION
-----------
:program:`llvm-debuginfo-analyzer` parses debug and text sections in
binary object files and prints their contents in a logical view, which
is a human readable representation that closely matches the structure
of the original user source code. Supported object file formats include
ELF, Mach-O, WebAssembly, PDB and COFF.

The **logical view** abstracts the complexity associated with the
different low-level representations of the debugging information that
is embedded in the object file. :program:`llvm-debuginfo-analyzer`
produces a canonical view of the debug information regardless of how it
is formatted. The same logical view will be seen regardless of object
file format, assuming the debug information correctly represents the
same original source code.

The logical view includes the following **logical elements**: *type*,
*scope*, *symbol* and *line*, which are the basic software elements used
in the C/C++ programming language. Each logical element has a set of
**attributes**, such as *types*, *classes*, *functions*, *variables*,
*parameters*, etc. The :option:`--attribute` can be used to specify which
attributes to include when printing a logical element. A logical element
may have a **kind** that describes specific types of elements. For
instance, a *scope* could have a kind value of *function*, *class*,
*namespace*.

:program:`llvm-debuginfo-analyzer` defaults to print a pre-defined
layout of logical elements and attributes. The command line options can
be used to control the printed elements (:option:`--print`), using a
specific layout (:option:`--report`), matching a given pattern
(:option:`--select`, :option:`--select-offsets`). Also, the output can
be limited to specified logical elements using (:option:`--select-lines`,
:option:`--select-scopes`, :option:`--select-symbols`,
:option:`--select-types`).

:program:`llvm-debuginfo-analyzer` can also compare a set of logical
views (:option:`--compare`), to find differences and identify possible
debug information syntax issues (:option:`--warning`) in any object file.

OPTIONS
-------
:program:`llvm-debuginfo-analyzer` options are separated into several
categories, each tailored to a different purpose:

  * :ref:`general_` - Standard LLVM options to display help, version, etc.
  * :ref:`attributes_` - Describe how to include different details when
    printing an element.
  * :ref:`print_` - Specify which elements will be included when printing
    the view.
  * :ref:`output_` - Describe the supported formats when printing the view.
  * :ref:`report_` - Describe the format layouts for view printing.
  * :ref:`select_` - Allows to use specific criteria or conditions to
    select which elements to print.
  * :ref:`compare_` - Compare logical views and print missing and/or
    added elements.
  * :ref:`warning_` - Print the warnings detected during the creation
    of the view.
  * :ref:`internal_` - Internal analysis of the logical view.

.. _general_:

GENERAL
~~~~~~~
This section describes the standard help options, used to display the
usage, version, response files, etc.

.. option:: -h, --help

 Show help and usage for this command. (--help-hidden for more).

.. option:: --help-list

 Show help and usage for this command without grouping the options
 into categories (--help-list-hidden for more).

.. option:: --help-hidden

 Display all available options.

.. option:: --print-all-options

 Print all option values after command line parsing.

.. option:: --print-options

 Print non-default options after command line parsing

.. option:: --version

 Display the version of the tool.

.. option:: @<FILE>

 Read command-line options from `<FILE>`.

If no input file is specified, :program:`llvm-debuginfo-analyzer`
defaults to read `a.out` and return an error when no input file is found.

If `-` is used as the input file, :program:`llvm-debuginfo-analyzer`
reads the input from its standard input stream.

.. _attributes_:

ATTRIBUTES
~~~~~~~~~~
The following options enable attributes given for the printed elements.
The attributes are divided in categories based on the type of data being
added, such as: internal offsets in the binary file, location descriptors,
register names, user source filenames, additional element transformations,
toolchain name, binary file format, etc.

.. option:: --attribute=<value[,value,...]>

 With **value** being one of the options in the following lists.

 .. code-block:: text

   =all: Include all the below attributes.
   =extended: Add low-level attributes.
   =standard: Add standard high-level attributes.

 The following attributes describe the most common information for a
 logical element. They help to identify the lexical scope level; the
 element visibility across modules (global, local); the toolchain name
 that produced the binary file.

 .. code-block:: text

   =global: Element referenced across Compile Units.
   =format: Object file format name.
   =level: Lexical scope level (File=0, Compile Unit=1).
   =local: Element referenced only in the Compile Unit.
   =producer: Toolchain identification name.

 The following attributes describe files and directory names from the
 user source code, where the elements are declared or defined; functions
 with public visibility across modules. These options allow to map the
 elements to their user code location, for cross references purposes.

 .. code-block:: text

   =directories: Directories referenced in the debug information.
   =filename: Filename where the element is defined.
   =files: Files referenced in the debug information.
   =pathname: Pathname where the object is defined.
   =publics: Function names that are public.

 The following attributes describe additional logical element source
 transformations, in order to display built-in types (int, bool, etc.);
 parameters and arguments used during template instantiation; parent
 name hierarchy; array dimensions information; compiler generated
 elements and the underlying types associated with the types aliases.

 .. code-block:: text

   =argument: Template parameters replaced by its arguments.
   =base: Base types (int, bool, etc.).
   =generated: Compiler generated elements.
   =encoded: Template arguments encoded in the template name.
   =qualified: The element type include parents in its name.
   =reference: Element declaration and definition references.
   =subrange: Subrange encoding information for arrays.
   =typename: Template parameters.
   =underlying: Underlying type for type definitions.

 The following attributes describe the debug location information for
 a symbol or scope. It includes the symbol percentage coverage and any
 gaps within the location layout; ranges determining the code sections
 attached to a function. When descriptors are used, the target processor
 registers are displayed.

 .. code-block:: text

   =coverage: Symbol location coverage.
   =gaps: Missing debug location (gaps).
   =location: Symbol debug location.
   =range: Debug location ranges.
   =register: Processor register names.

 The following attributes are associated with low level details, such
 as: offsets in the binary file; discriminators added to the lines of
 inlined functions in order to distinguish specific instances; debug
 lines state machine registers; elements discarded by the compiler
 (inlining) or by the linker optimizations (dead-stripping); system
 compile units generated by the MS toolchain in PDBs.

 .. code-block:: text

   =discarded: Discarded elements by the linker.
   =discriminator: Discriminators for inlined function instances.
   =inserted: Generated inlined abstract references.
   =linkage: Object file linkage name.
   =offset: Debug information offset.
   =qualifier: Line qualifiers (Newstatement, BasicBlock, etc).
   =zero: Zero line numbers.

 The following attribute described specific information for the **PE/COFF**
 file format. It includes MS runtime types.

 .. code-block:: text

   =system: Display PDB's MS system elements.

 The above attributes are grouped into *standard* and *extended*
 categories that can be enabled.

 The *standard* group, contains those attributes that add sufficient
 information to describe a logical element and that can cover the
 normal situations while dealing with debug information.

 .. code-block:: text

   =base
   =coverage
   =directories
   =discriminator
   =filename
   =files
   =format
   =level
   =producer
   =publics
   =range
   =reference
   =zero

 The *extended* group, contains those attributes that require a more
 extended knowledge about debug information. They are intended when a
 lower level of detail is required.

 .. code-block:: text

   =argument
   =discarded
   =encoded
   =gaps
   =generated
   =global
   =inserted
   =linkage
   =local
   =location
   =offset
   =operation
   =pathname
   =qualified
   =qualifier
   =register
   =subrange
   =system
   =typename

.. _print_:

PRINT
~~~~~
The following options describe the elements to print. The layout used
is determined by the :option:`--report`. In the tree layout, all the
elements have their enclosing lexical scopes printed, even when not
explicitly specified.

.. option:: --print=<value[,value,...]>

 With **value** being one of the options in the following lists.

 .. code-block:: text

   =all: Include all the below attributes.

 The following options print the requested elements; in the case of any
 given select conditions (:option:`--select`), only those elements that
 match them, will be printed. The **elements** value is a convenient
 way to specify instructions, lines, scopes, symbols and types all at
 once.

 .. code-block:: text

   =elements: Instructions, lines, scopes, symbols and types.
   =instructions: Assembler instructions for code sections.
   =lines: Source lines referenced in the debug information.
   =scopes: Lexical blocks (function, class, namespace, etc).
   =symbols: Symbols (variable, member, parameter, etc).
   =types: Types (pointer, reference, type alias, etc).

 The following options print information, collected during the creation
 of the elements, such as: scope contributions to the debug information;
 summary of elements created, printed or matched (:option:`--select`);
 warnings produced during the view creation.

 .. code-block:: text

   =sizes: Debug Information scopes contributions.
   =summary: Summary of elements allocated, selected or printed.
   =warnings: Warnings detected.

 Note: The **--print=sizes** option is ELF specific.

.. _output_:

OUTPUT
~~~~~~
The following options describe how to control the output generated when
printing the logical elements.

.. option:: --output-file=<path>

 Redirect the output to a file specified by <path>, where - is the
 standard output stream.

:program:`llvm-debuginfo-analyzer` has the concept of **split view**.
When redirecting the output from a complex binary format, it is
**divided** into individual files, each one containing the logical view
output for a single compilation unit.

.. option:: --output-folder=<name>

 The folder to write a file per compilation unit when **--output=split**
 is specified.

.. option:: --output-level=<level>

 Only print elements up to the given **lexical level** value. The input
 file is at lexical level zero and a compilation unit is at lexical level
 one.

.. option:: --output=<value[,value,...]>

 With **value** being one of the options in the following lists.

 .. code-block:: text

   =all: Include all the below outputs.

 .. code-block:: text

   =json: Use JSON as the output format (Not implemented).
   =split: Split the output by Compile Units.
   =text: Use a free form text output.

.. option:: --output-sort=<key>

 Primary key when ordering the elements in the output (default: line).
 Sorting by logical element kind, requires be familiarity with the
 element kind selection options (:option:`--select-lines`,
 :option:`--select-scopes`, :option:`--select-symbols`,
 :option:`--select-types`), as those options describe the different
 logical element kinds.

 .. code-block:: text

   =kind: Sort by element kind.
   =line: Sort by element line number.
   =name: Sort by element name.
   =offset: Sort by element offset.

.. _report_:

REPORT
~~~~~~
Depending on the task being executed (print, compare, select), several
layouts are supported to display the elements in a more suitable way,
to make the output easier to understand.

.. option:: --report=<value[,value,...]>

 With **value** being one of the options in the following list.

 .. code-block:: text

   =all: Include all the below reports.

 .. code-block:: text

   =children: Elements and children are displayed in a tree format.
   =list: Elements are displayed in a tabular format.
   =parents: Elements and parents are displayed in a tree format.
   =view: Elements, parents and children are displayed in a tree format.

The **list** layout presents the logical elements in a tabular form
without any parent-child relationship. This may be the preferred way to
display elements that match specific conditions when comparing logical
views, making it easier to find differences.

The **children**, **parents** and **view** layout displays the elements
in a tree format, with the scopes representing their nodes, and types,
symbols, lines and other scopes representing the children. The layout
shows the lexical scoping relationship between elements, with the binary
file being the tree root (level 0) and each compilation unit being a
child (level 1).

The **children** layout includes the elements that match any given
criteria (:option:`--select`) or (:option:`--compare`) and its children.

The **parents** layout includes the elements that match any given
criteria (:option:`--select`) or (:option:`--compare`) and its parents.

The combined **view** layout includes the elements that match any given
criteria (:option:`--select`) or (:option:`--compare`), its parents
and children.

**Notes**:

1. When a selection criteria (:option:`--select`) is specified with no
   report option, the **list** layout is selected.
2. The comparison mode always uses the **view** layout.

.. _select_:

SELECTION
~~~~~~~~~
When printing an element, different data can be included and it varies
(:option:`--attribute`) from data directly associated with the binary
file (offset) to high level details such as coverage, lexical scope
level, location. As the printed output can reach a considerable size,
several selection options, enable printing of specific elements.

The pattern matching can ignore the case (:option:`--select-nocase`)
and be extended to use regular expressions (:option:`--select-regex`).

ELEMENTS
^^^^^^^^
The following options allow printing of elements that match the given
<pattern>, offset <value> or an element <condition>.

.. option:: --select=<pattern>

 Print all elements whose name or line number matches the given <pattern>.

.. option:: --select-offsets=<value[,value,...]>

 Print all elements whose offset matches the given values. See
 :option:`--attribute` option.

.. option:: --select-elements=<condition[,condition,...]>

 Print all elements that satisfy the given <condition>. With **condition**
 being one of the options in the following list.

 .. code-block:: text

   =discarded: Discarded elements by the linker.
   =global: Element referenced across Compile Units.
   =optimized: Optimized inlined abstract references.

.. option:: --select-nocase

 Pattern matching is case-insensitive when using :option:`--select`.

.. option:: --select-regex

 Treat any <pattern> strings as regular expressions when selecting with
 :option:`--select` option. If :option:`--select-nocase` is specified,
 the regular expression becomes case-insensitive.

If the <pattern> criteria is too general, a more selective option can
be specified to target a particular category of elements:
lines (:option:`--select-lines`), scopes (:option:`--select-scopes`),
symbols (:option:`--select-symbols`) and types (:option:`--select-types`).

These options require knowledge of the debug information format (DWARF,
CodeView), as the given **kind** describes a very specific type
of element.

LINES
^^^^^
The following options allow printing of lines that match the given <kind>.
The given criteria describes the debug line state machine registers.

.. option:: --select-lines=<kind[,kind,...]>

 With **kind** being one of the options in the following list.

 .. code-block:: text

   =AlwaysStepInto: marks an always step into.
   =BasicBlock: Marks a new basic block.
   =Discriminator: Line that has a discriminator.
   =EndSequence: Marks the end in the sequence of lines.
   =EpilogueBegin: Marks the start of a function epilogue.
   =LineDebug: Lines that correspond to debug lines.
   =LineAssembler: Lines that correspond to disassembly text.
   =NeverStepInto: marks a never step into.
   =NewStatement: Marks a new statement.
   =PrologueEnd: Marks the end of a function prologue.

SCOPES
^^^^^^
The following options allow printing of scopes that match the given <kind>.

.. option:: --select-scopes=<kind[,kind,...]>

 With **kind** being one of the options in the following list.

 .. code-block:: text

    =Aggregate: A class, structure or union.
    =Array: An array.
    =Block: A generic block (lexical block or exception block).
    =CallSite: A call site.
    =CatchBlock: An exception block.
    =Class: A class.
    =CompileUnit: A compile unit.
    =EntryPoint: A subroutine entry point.
    =Enumeration: An enumeration.
    =Function: A function.
    =FunctionType: A function pointer.
    =InlinedFunction: An inlined function.
    =Label: A label.
    =LexicalBlock: A lexical block.
    =Namespace: A namespace.
    =Root: The element representing the main scope.
    =Structure: A structure.
    =Subprogram: A subprogram.
    =Template: A template definition.
    =TemplateAlias: A template alias.
    =TemplatePack: A template pack.
    =TryBlock: An exception try block.
    =Union: A union.

SYMBOLS
^^^^^^^
The following options allow printing of symbols that match the given <kind>.

.. option:: --select-symbols=<kind[,kind,...]>

 With **kind** being one of the options in the following list.

 .. code-block:: text

    =CallSiteParameter: A call site parameter.
    =Constant: A constant symbol.
    =Inheritance: A base class.
    =Member: A member class.
    =Parameter: A parameter to function.
    =Unspecified: Unspecified parameters to function.
    =Variable: A variable.

TYPES
^^^^^
The following options allow printing of types that match the given <kind>.

.. option:: --select-types=<kind[,kind,...]>

 With **kind** being one of the options in the following list.

 .. code-block:: text

    =Base: Base type (integer, boolean, etc).
    =Const: Constant specifier.
    =Enumerator: Enumerator.
    =Import: Import declaration.
    =ImportDeclaration: Import declaration.
    =ImportModule: Import module.
    =Pointer: Pointer type.
    =PointerMember: Pointer to member function.
    =Reference: Reference type.
    =Restrict: Restrict specifier.
    =RvalueReference: R-value reference.
    =Subrange: Array subrange.
    =TemplateParam: Template parameter.
    =TemplateTemplateParam: Template template parameter.
    =TemplateTypeParam: Template type parameter.
    =TemplateValueParam: Template value parameter.
    =Typedef: Type definition.
    =Unspecified: Unspecified type.
    =Volatile: Volatile specifier.

.. _compare_:

COMPARE
~~~~~~~
When dealing with debug information, there are situations when the
printing of the elements is not the correct approach. That is the case,
when we are interested in the effects caused by different versions of
the same toolchain, or the impact of specific compiler optimizations.

For those cases, we are looking to see which elements have been added
or removed. Due to the complicated debug information format, it is very
difficult to use a regular diff tool to find those elements; even
impossible when dealing with different debug formats.

:program:`llvm-debuginfo-analyzer` supports a logical element comparison,
allowing to find semantic differences between logical views, produced by
different toolchain versions or even debug information formats.

When comparing logical views created from different debug formats, its
accuracy depends on how close the debug information represents the
user code. For instance, a logical view created from a binary file with
DWARF debug information may include more detailed data than a logical
view created from a binary file with CodeView debug information.

The following options describe the elements to compare.

.. option:: --compare=<value[,value,...]>

 With **value** being one of the options in the following list.

 .. code-block:: text

    =all: Include all the below elements.

 .. code-block:: text

    =lines: Include lines.
    =scopes: Include scopes.
    =symbols: Include symbols.
    =types: Include types.

:program:`llvm-debuginfo-analyzer` takes the first binary file on the
command line as the **reference** and the second one as the **target**.
To get a more descriptive report, the comparison is done twice. The
reference and target views are swapped, in order to produce those
**missing** elements from the target view and those **added** elements
to the reference view.

See :option:`--report` options on how to describe the comparison
reports.

.. _warning_:

WARNING
~~~~~~~
When reading the input object files, :program:`llvm-debuginfo-analyzer`
can detect issues in the raw debug information. These may not be
considered fatal to the purpose of printing a logical view but they can
give an indication about the quality and potentially expose issues with
the generated debug information.

The following options describe the warnings to be recorded for later
printing, if they are requested by :option:`--print`.

.. option:: --warning=<value[,value,...]>

 With **value** being one of the options in the following list.

 .. code-block:: text

    =all: Include all the below warnings.

 The following options collect additional information during the creation
 of the logical view, to include invalid coverage values and locations
 for symbols; invalid code ranges; lines that are zero.

 .. code-block:: text

    =coverages: Invalid symbol coverages values.
    =lines: Debug lines that are zero.
    =locations: Invalid symbol locations.
    =ranges: Invalid code ranges.

.. _internal_:

INTERNAL
~~~~~~~~
 For a better understanding of the logical view, access to more detailed
 internal information could be needed. Such data would help to identify
 debug information processed or incorrect logical element management.
 Typically these kind of options are available only in *debug* builds.

 :program:`llvm-debuginfo-analyzer` supports these advanced options in
 both *release* and *debug* builds, with the exception of the unique ID
 that is generated only in *debug* builds.

.. option:: --internal=<value[,value,...]>

 With **value** being one of the options in the following list.

 .. code-block:: text

    =all: Include all the below options.

 The following options allow to check the integrity of the logical view;
 collect the debug tags that are processed or not implemented; ignore the
 logical element line number, to facilitate the logical view comparison
 when using external comparison tools; print the command line options
 used to invoke :program:`llvm-debuginfo-analyzer`.

 .. code-block:: text

    =id: Print unique element ID.
    =cmdline: Print command line.
    =integrity: Check elements integrity.
    =none: Ignore element line number.
    =tag: Debug information tags.

 **Note:** For ELF format, the collected tags represent the debug tags
 that are not processed. For PE/COFF format, they represent the tags
 that are processed.

EXAMPLES
--------
This section includes some real binary files to show how to use
:program:`llvm-debuginfo-analyzer` to print a logical view and to
diagnose possible debug information issues.

TEST CASE 1 - GENERAL OPTIONS
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
The below example is used to show different output generated by
:program:`llvm-debuginfo-analyzer`. We compiled the example for an X86
ELF target with Clang (-O0 -g):

.. code-block:: c++

  1  using INTPTR = const int *;
  2  int foo(INTPTR ParamPtr, unsigned ParamUnsigned, bool ParamBool) {
  3    if (ParamBool) {
  4      typedef int INTEGER;
  5      const INTEGER CONSTANT = 7;
  6      return CONSTANT;
  7    }
  8    return ParamUnsigned;
  9  }

PRINTING MODE
^^^^^^^^^^^^^
In this mode :program:`llvm-debuginfo-analyzer` prints the *logical view*
or portions of it, based on criteria patterns (including regular
expressions) to select the kind of *logical elements* to be included in
the output.

BASIC DETAILS
"""""""""""""
The following command prints basic details for all the logical elements
sorted by the debug information internal offset; it includes its lexical
level and debug info format.

.. code-block:: none

  llvm-debuginfo-analyzer --attribute=level,format
                          --output-sort=offset
                          --print=scopes,symbols,types,lines,instructions
                          test-dwarf-clang.o

or

.. code-block:: none

  llvm-debuginfo-analyzer --attribute=level,format
                          --output-sort=offset
                          --print=elements
                          test-dwarf-clang.o

Each row represents an element that is present within the debug
information. The first column represents the scope level, followed by
the associated line number (if any), and finally the description of
the element.

.. code-block:: none

  Logical View:
  [000]           {File} 'test-dwarf-clang.o' -> elf64-x86-64

  [001]             {CompileUnit} 'test.cpp'
  [002]     2         {Function} extern not_inlined 'foo' -> 'int'
  [003]     2           {Parameter} 'ParamPtr' -> 'INTPTR'
  [003]     2           {Parameter} 'ParamUnsigned' -> 'unsigned int'
  [003]     2           {Parameter} 'ParamBool' -> 'bool'
  [003]                 {Block}
  [004]     5             {Variable} 'CONSTANT' -> 'const INTEGER'
  [004]     5             {Line}
  [004]                   {Code} 'movl	$0x7, -0x1c(%rbp)'
  [004]     6             {Line}
  [004]                   {Code} 'movl	$0x7, -0x4(%rbp)'
  [004]                   {Code} 'jmp	0x6'
  [004]     8             {Line}
  [004]                   {Code} 'movl	-0x14(%rbp), %eax'
  [003]     4           {TypeAlias} 'INTEGER' -> 'int'
  [003]     2           {Line}
  [003]                 {Code} 'pushq	%rbp'
  [003]                 {Code} 'movq	%rsp, %rbp'
  [003]                 {Code} 'movb	%dl, %al'
  [003]                 {Code} 'movq	%rdi, -0x10(%rbp)'
  [003]                 {Code} 'movl	%esi, -0x14(%rbp)'
  [003]                 {Code} 'andb	$0x1, %al'
  [003]                 {Code} 'movb	%al, -0x15(%rbp)'
  [003]     3           {Line}
  [003]                 {Code} 'testb	$0x1, -0x15(%rbp)'
  [003]                 {Code} 'je	0x13'
  [003]     8           {Line}
  [003]                 {Code} 'movl	%eax, -0x4(%rbp)'
  [003]     9           {Line}
  [003]                 {Code} 'movl	-0x4(%rbp), %eax'
  [003]                 {Code} 'popq	%rbp'
  [003]                 {Code} 'retq'
  [003]     9           {Line}
  [002]     1         {TypeAlias} 'INTPTR' -> '* const int'

On closer inspection, we can see what could be a potential debug issue:

.. code-block:: none

  [003]                 {Block}
  [003]     4           {TypeAlias} 'INTEGER' -> 'int'

The **'INTEGER'** definition is at level **[003]**, the same lexical
scope as the anonymous **{Block}** ('true' branch for the 'if' statement)
whereas in the original source code the typedef statement is clearly
inside that block, so the **'INTEGER'** definition should also be at
level **[004]** inside the block.

SELECT LOGICAL ELEMENTS
"""""""""""""""""""""""
The following prints all *instructions*, *symbols* and *types* that
contain **'inte'** or **'movl'** in their names or types, using a tab
layout and given the number of matches.

.. code-block:: none

  llvm-debuginfo-analyzer --attribute=level
                          --select-nocase --select-regex
                          --select=INTe --select=movl
                          --report=list
                          --print=symbols,types,instructions,summary
                          test-dwarf-clang.o

  Logical View:
  [000]           {File} 'test-dwarf-clang.o'

  [001]           {CompileUnit} 'test.cpp'
  [003]           {Code} 'movl	$0x7, -0x1c(%rbp)'
  [003]           {Code} 'movl	$0x7, -0x4(%rbp)'
  [003]           {Code} 'movl	%eax, -0x4(%rbp)'
  [003]           {Code} 'movl	%esi, -0x14(%rbp)'
  [003]           {Code} 'movl	-0x14(%rbp), %eax'
  [003]           {Code} 'movl	-0x4(%rbp), %eax'
  [003]     4     {TypeAlias} 'INTEGER' -> 'int'
  [004]     5     {Variable} 'CONSTANT' -> 'const INTEGER'

  -----------------------------
  Element      Total      Found
  -----------------------------
  Scopes           3          0
  Symbols          4          1
  Types            2          1
  Lines           17          6
  -----------------------------
  Total           26          8

COMPARISON MODE
^^^^^^^^^^^^^^^
In this mode :program:`llvm-debuginfo-analyzer` compares logical views
to produce a report with the logical elements that are missing or added.
This a very powerful aid in finding semantic differences in the debug
information produced by different toolchain versions or even completely
different toolchains altogether (For example a compiler producing DWARF
can be directly compared against a completely different compiler that
produces CodeView).

Given the previous example we found the above debug information issue
(related to the previous invalid scope location for the **'typedef int
INTEGER'**) by comparing against another compiler.

Using GCC to generate test-dwarf-gcc.o, we can apply a selection pattern
with the printing mode to obtain the following logical view output.

.. code-block:: none

  llvm-debuginfo-analyzer --attribute=level
                          --select-regex --select-nocase --select=INTe
                          --report=list
                          --print=symbols,types
                          test-dwarf-clang.o test-dwarf-gcc.o

  Logical View:
  [000]           {File} 'test-dwarf-clang.o'

  [001]           {CompileUnit} 'test.cpp'
  [003]     4     {TypeAlias} 'INTEGER' -> 'int'
  [004]     5     {Variable} 'CONSTANT' -> 'const INTEGER'

  Logical View:
  [000]           {File} 'test-dwarf-gcc.o'

  [001]           {CompileUnit} 'test.cpp'
  [004]     4     {TypeAlias} 'INTEGER' -> 'int'
  [004]     5     {Variable} 'CONSTANT' -> 'const INTEGER'

The output shows that both objects contain the same elements. But the
**'typedef INTEGER'** is located at different scope level. The GCC
generated object, shows **'4'**, which is the correct value.

Note that there is no requirement that GCC must produce identical or
similar DWARF to Clang to allow the comparison. We're only comparing
the semantics. The same case when comparing CodeView debug information
generated by MSVC and Clang.

There are 2 comparison methods: logical view and logical elements.

LOGICAL VIEW
""""""""""""
It compares the logical view as a whole unit; for a match, each compared
logical element must have the same parents and children.

Using the :program:`llvm-debuginfo-analyzer` comparison functionality,
that issue can be seen in a more global context, that can include the
logical view.

The output shows in view form the **missing (-), added (+)** elements,
giving more context by swapping the reference and target object files.

.. code-block:: none

  llvm-debuginfo-analyzer --attribute=level
                          --compare=types
                          --report=view
                          --print=symbols,types
                          test-dwarf-clang.o test-dwarf-gcc.o

  Reference: 'test-dwarf-clang.o'
  Target:    'test-dwarf-gcc.o'

  Logical View:
   [000]           {File} 'test-dwarf-clang.o'

   [001]             {CompileUnit} 'test.cpp'
   [002]     1         {TypeAlias} 'INTPTR' -> '* const int'
   [002]     2         {Function} extern not_inlined 'foo' -> 'int'
   [003]                 {Block}
   [004]     5             {Variable} 'CONSTANT' -> 'const INTEGER'
  +[004]     4             {TypeAlias} 'INTEGER' -> 'int'
   [003]     2           {Parameter} 'ParamBool' -> 'bool'
   [003]     2           {Parameter} 'ParamPtr' -> 'INTPTR'
   [003]     2           {Parameter} 'ParamUnsigned' -> 'unsigned int'
  -[003]     4           {TypeAlias} 'INTEGER' -> 'int'

The output shows the merging view path (reference and target) with the
missing and added elements.

LOGICAL ELEMENTS
""""""""""""""""
It compares individual logical elements without considering if their
parents are the same. For both comparison methods, the equal criteria
includes the name, source code location, type, lexical scope level.

.. code-block:: none

  llvm-debuginfo-analyzer --attribute=level
                          --compare=types
                          --report=list
                          --print=symbols,types,summary
                          test-dwarf-clang.o test-dwarf-gcc.o

  Reference: 'test-dwarf-clang.o'
  Target:    'test-dwarf-gcc.o'

  (1) Missing Types:
  -[003]     4     {TypeAlias} 'INTEGER' -> 'int'

  (1) Added Types:
  +[004]     4     {TypeAlias} 'INTEGER' -> 'int'

  ----------------------------------------
  Element   Expected    Missing      Added
  ----------------------------------------
  Scopes           4          0          0
  Symbols          0          0          0
  Types            2          1          1
  Lines            0          0          0
  ----------------------------------------
  Total            6          1          1

Changing the *Reference* and *Target* order:

.. code-block:: none

  llvm-debuginfo-analyzer --attribute=level
                          --compare=types
                          --report=list
                          --print=symbols,types,summary
                          test-dwarf-gcc.o test-dwarf-clang.o

  Reference: 'test-dwarf-gcc.o'
  Target:    'test-dwarf-clang.o'

  (1) Missing Types:
  -[004]     4     {TypeAlias} 'INTEGER' -> 'int'

  (1) Added Types:
  +[003]     4     {TypeAlias} 'INTEGER' -> 'int'

  ----------------------------------------
  Element   Expected    Missing      Added
  ----------------------------------------
  Scopes           4          0          0
  Symbols          0          0          0
  Types            2          1          1
  Lines            0          0          0
  ----------------------------------------
  Total            6          1          1

As the *Reference* and *Target* are switched, the *Added Types* from
the first case now are listed as *Missing Types*.

TEST CASE 2 - ASSEMBLER INSTRUCTIONS
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
The below example is used to show different output generated by
:program:`llvm-debuginfo-analyzer`. We compiled the example for an X86
Codeview and ELF targets with recent versions of Clang, GCC and MSVC
(-O0 -g) for Windows and Linux.

.. code-block:: c++

   1  extern int printf(const char * format, ... );
   2
   3  int main()
   4  {
   5    printf("Hello, World\n");
   6    return 0;
   7  }

These are the logical views that :program:`llvm-debuginfo-analyzer`
generates for 3 different compilers (MSVC, Clang and GCC), emitting
different debug information formats (CodeView, DWARF) on Windows and
Linux.

.. code-block:: none

  llvm-debuginfo-analyzer --attribute=level,format,producer
                          --print=lines,instructions
                          hello-world-codeview-clang.o
                          hello-world-codeview-msvc.o
                          hello-world-dwarf-clang.o
                          hello-world-dwarf-gcc.o

CodeView - Clang (Windows)
^^^^^^^^^^^^^^^^^^^^^^^^^^

.. code-block:: none

  Logical View:
  [000]           {File} 'hello-world-codeview-clang.o' -> COFF-x86-64

  [001]             {CompileUnit} 'hello-world.cpp'
  [002]               {Producer} 'clang version 14.0.0'
  [002]               {Function} extern not_inlined 'main' -> 'int'
  [003]     4           {Line}
  [003]                 {Code} 'subq	$0x28, %rsp'
  [003]                 {Code} 'movl	$0x0, 0x24(%rsp)'
  [003]     5           {Line}
  [003]                 {Code} 'leaq	(%rip), %rcx'
  [003]                 {Code} 'callq	0x0'
  [003]     6           {Line}
  [003]                 {Code} 'xorl	%eax, %eax'
  [003]                 {Code} 'addq	$0x28, %rsp'
  [003]                 {Code} 'retq'

CodeView - MSVC (Windows)
^^^^^^^^^^^^^^^^^^^^^^^^^

.. code-block:: none

  Logical View:
  [000]           {File} 'hello-world-codeview-msvc.o' -> COFF-i386

  [001]             {CompileUnit} 'hello-world.cpp'
  [002]               {Producer} 'Microsoft (R) Optimizing Compiler'
  [002]               {Function} extern not_inlined 'main' -> 'int'
  [003]     4           {Line}
  [003]                 {Code} 'pushl	%ebp'
  [003]                 {Code} 'movl	%esp, %ebp'
  [003]     5           {Line}
  [003]                 {Code} 'pushl	$0x0'
  [003]                 {Code} 'calll	0x0'
  [003]                 {Code} 'addl	$0x4, %esp'
  [003]     6           {Line}
  [003]                 {Code} 'xorl	%eax, %eax'
  [003]     7           {Line}
  [003]                 {Code} 'popl	%ebp'
  [003]                 {Code} 'retl'

DWARF - Clang (Linux)
^^^^^^^^^^^^^^^^^^^^^

.. code-block:: none

  Logical View:
  [000]           {File} 'hello-world-dwarf-clang.o' -> elf64-x86-64

  [001]             {CompileUnit} 'hello-world.cpp'
  [002]               {Producer} 'clang version 14.0.0'
  [002]     3         {Function} extern not_inlined 'main' -> 'int'
  [003]     4           {Line}
  [003]                 {Code} 'pushq	%rbp'
  [003]                 {Code} 'movq	%rsp, %rbp'
  [003]                 {Code} 'subq	$0x10, %rsp'
  [003]                 {Code} 'movl	$0x0, -0x4(%rbp)'
  [003]     5           {Line}
  [003]                 {Code} 'movabsq	$0x0, %rdi'
  [003]                 {Code} 'movb	$0x0, %al'
  [003]                 {Code} 'callq	0x0'
  [003]     6           {Line}
  [003]                 {Code} 'xorl	%eax, %eax'
  [003]                 {Code} 'addq	$0x10, %rsp'
  [003]                 {Code} 'popq	%rbp'
  [003]                 {Code} 'retq'
  [003]     6           {Line}

DWARF - GCC (Linux)
^^^^^^^^^^^^^^^^^^^

.. code-block:: none

  Logical View:
  [000]           {File} 'hello-world-dwarf-gcc.o' -> elf64-x86-64

  [001]             {CompileUnit} 'hello-world.cpp'
  [002]               {Producer} 'GNU C++14 9.3.0'
  [002]     3         {Function} extern not_inlined 'main' -> 'int'
  [003]     4           {Line}
  [003]                 {Code} 'endbr64'
  [003]                 {Code} 'pushq	%rbp'
  [003]                 {Code} 'movq	%rsp, %rbp'
  [003]     5           {Line}
  [003]                 {Code} 'leaq	(%rip), %rdi'
  [003]                 {Code} 'movl	$0x0, %eax'
  [003]                 {Code} 'callq	0x0'
  [003]     6           {Line}
  [003]                 {Code} 'movl	$0x0, %eax'
  [003]     7           {Line}
  [003]                 {Code} 'popq	%rbp'
  [003]                 {Code} 'retq'
  [003]     7           {Line}

The logical views shows the intermixed lines and assembler instructions,
allowing to compare the code generated by the different toolchains.

TEST CASE 3 - INCORRECT LEXICAL SCOPE FOR TYPEDEF
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
The below example is used to show different output generated by
:program:`llvm-debuginfo-analyzer`. We compiled the example for an X86
Codeview and ELF targets with recent versions of Clang, GCC and MSVC
(-O0 -g).

.. code-block:: c++

   1  int bar(float Input) { return (int)Input; }
   2
   3  unsigned foo(char Param) {
   4    typedef int INT;                // ** Definition for INT **
   5    INT Value = Param;
   6    {
   7      typedef float FLOAT;          // ** Definition for FLOAT **
   8      {
   9        FLOAT Added = Value + Param;
  10        Value = bar(Added);
  11      }
  12    }
  13    return Value + Param;
  14  }

The above test is used to illustrate a scope issue found in the Clang
compiler:
`PR44884 (Bugs LLVM) <https://bugs.llvm.org/show_bug.cgi?id=44884>`_ /
`PR44229 (GitHub LLVM) <https://github.com/llvm/llvm-project/issues/44229>`_

The lines 4 and 7 contains 2 typedefs, defined at different lexical
scopes.

.. code-block:: c++

  4    typedef int INT;
  7      typedef float FLOAT;

These are the logical views that :program:`llvm-debuginfo-analyzer`
generates for 3 different compilers (MSVC, Clang and GCC), emitting
different debug information formats (CodeView, DWARF) on different
platforms.

.. code-block:: none

  llvm-debuginfo-analyzer --attribute=level,format,producer
                          --print=symbols,types,lines
                          --output-sort=kind
                          pr-44884-codeview-clang.o
                          pr-44884-codeview-msvc.o
                          pr-44884-dwarf-clang.o
                          pr-44884-dwarf-gcc.o

CodeView - Clang (Windows)
^^^^^^^^^^^^^^^^^^^^^^^^^^

.. code-block:: none

  Logical View:
  [000]           {File} 'pr-44884-codeview-clang.o' -> COFF-x86-64

  [001]             {CompileUnit} 'pr-44884.cpp'
  [002]               {Producer} 'clang version 14.0.0'
  [002]               {Function} extern not_inlined 'bar' -> 'int'
  [003]                 {Parameter} 'Input' -> 'float'
  [003]     1           {Line}
  [002]               {Function} extern not_inlined 'foo' -> 'unsigned'
  [003]                 {Block}
  [004]                   {Variable} 'Added' -> 'float'
  [004]     9             {Line}
  [004]    10             {Line}
  [003]                 {Parameter} 'Param' -> 'char'
  [003]                 {TypeAlias} 'FLOAT' -> 'float'
  [003]                 {TypeAlias} 'INT' -> 'int'
  [003]                 {Variable} 'Value' -> 'int'
  [003]     3           {Line}
  [003]     5           {Line}
  [003]    13           {Line}

CodeView - MSVC (Windows)
^^^^^^^^^^^^^^^^^^^^^^^^^

.. code-block:: none

  Logical View:
  [000]           {File} 'pr-44884-codeview-msvc.o' -> COFF-i386

  [001]             {CompileUnit} 'pr-44884.cpp'
  [002]               {Producer} 'Microsoft (R) Optimizing Compiler'
  [002]               {Function} extern not_inlined 'bar' -> 'int'
  [003]                 {Variable} 'Input' -> 'float'
  [003]     1           {Line}
  [002]               {Function} extern not_inlined 'foo' -> 'unsigned'
  [003]                 {Block}
  [004]                   {Block}
  [005]                     {Variable} 'Added' -> 'float'
  [004]                   {TypeAlias} 'FLOAT' -> 'float'
  [004]     9             {Line}
  [004]    10             {Line}
  [003]                 {TypeAlias} 'INT' -> 'int'
  [003]                 {Variable} 'Param' -> 'char'
  [003]                 {Variable} 'Value' -> 'int'
  [003]     3           {Line}
  [003]     5           {Line}
  [003]    13           {Line}
  [003]    14           {Line}

DWARF - Clang (Linux)
^^^^^^^^^^^^^^^^^^^^^

.. code-block:: none

  Logical View:
  [000]           {File} 'pr-44884-dwarf-clang.o' -> elf64-x86-64

  [001]             {CompileUnit} 'pr-44884.cpp'
  [002]               {Producer} 'clang version 14.0.0'
  [002]     1         {Function} extern not_inlined 'bar' -> 'int'
  [003]     1           {Parameter} 'Input' -> 'float'
  [003]     1           {Line}
  [003]     1           {Line}
  [003]     1           {Line}
  [002]     3         {Function} extern not_inlined 'foo' -> 'unsigned int'
  [003]                 {Block}
  [004]     9             {Variable} 'Added' -> 'FLOAT'
  [004]     9             {Line}
  [004]     9             {Line}
  [004]     9             {Line}
  [004]     9             {Line}
  [004]     9             {Line}
  [004]    10             {Line}
  [004]    10             {Line}
  [004]    10             {Line}
  [004]    13             {Line}
  [003]     3           {Parameter} 'Param' -> 'char'
  [003]     7           {TypeAlias} 'FLOAT' -> 'float'
  [003]     4           {TypeAlias} 'INT' -> 'int'
  [003]     5           {Variable} 'Value' -> 'INT'
  [003]     3           {Line}
  [003]     5           {Line}
  [003]     5           {Line}
  [003]    13           {Line}
  [003]    13           {Line}
  [003]    13           {Line}
  [003]    13           {Line}

DWARF - GCC (Linux)
^^^^^^^^^^^^^^^^^^^

.. code-block:: none

  Logical View:
  [000]           {File} 'pr-44884-dwarf-gcc.o' -> elf32-littlearm

  [001]             {CompileUnit} 'pr-44884.cpp'
  [002]               {Producer} 'GNU C++14 10.2.1 20201103'
  [002]     1         {Function} extern not_inlined 'bar' -> 'int'
  [003]     1           {Parameter} 'Input' -> 'float'
  [003]     1           {Line}
  [003]     1           {Line}
  [003]     1           {Line}
  [002]     3         {Function} extern not_inlined 'foo' -> 'unsigned int'
  [003]                 {Block}
  [004]                   {Block}
  [005]     9               {Variable} 'Added' -> 'FLOAT'
  [005]     9               {Line}
  [005]     9               {Line}
  [005]     9               {Line}
  [005]    10               {Line}
  [005]    13               {Line}
  [004]     7             {TypeAlias} 'FLOAT' -> 'float'
  [003]     3           {Parameter} 'Param' -> 'char'
  [003]     4           {TypeAlias} 'INT' -> 'int'
  [003]     5           {Variable} 'Value' -> 'INT'
  [003]     3           {Line}
  [003]     5           {Line}
  [003]    13           {Line}
  [003]    14           {Line}
  [003]    14           {Line}

From the previous logical views, we can see that the Clang compiler
emits **both typedefs at the same lexical scope (3)**, which is wrong.
GCC and MSVC emit correct lexical scope for both typedefs.

Using the :program:`llvm-debuginfo-analyzer` selection facilities, we
can produce a simple tabular output showing just the logical types that
are **Typedef**.

.. code-block:: none

  llvm-debuginfo-analyzer --attribute=level,format
                          --output-sort=name
                          --select-types=Typedef
                          --report=list
                          --print=types
                          pr-44884-*.o

  Logical View:
  [000]           {File} 'pr-44884-codeview-clang.o' -> COFF-x86-64

  [001]           {CompileUnit} 'pr_44884.cpp'
  [003]           {TypeAlias} 'FLOAT' -> 'float'
  [003]           {TypeAlias} 'INT' -> 'int'

  Logical View:
  [000]           {File} 'pr-44884-codeview-msvc.o' -> COFF-i386

  [001]           {CompileUnit} 'pr_44884.cpp'
  [004]           {TypeAlias} 'FLOAT' -> 'float'
  [003]           {TypeAlias} 'INT' -> 'int'

  Logical View:
  [000]           {File} 'pr-44884-dwarf-clang.o' -> elf64-x86-64

  [001]           {CompileUnit} 'pr_44884.cpp'
  [003]     7     {TypeAlias} 'FLOAT' -> 'float'
  [003]     4     {TypeAlias} 'INT' -> 'int'

  Logical View:
  [000]           {File} 'pr-44884-dwarf-gcc.o' -> elf32-littlearm

  [001]           {CompileUnit} 'pr_44884.cpp'
  [004]     7     {TypeAlias} 'FLOAT' -> 'float'
  [003]     4     {TypeAlias} 'INT' -> 'int'

It also shows, that the CodeView debug information does not generate
source code line numbers for the those logical types. The logical view
is sorted by the types name.

TEST CASE 4 - MISSING NESTED ENUMERATIONS
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
The below example is used to show different output generated by
:program:`llvm-debuginfo-analyzer`. We compiled the example for an X86
Codeview and ELF targets with recent versions of Clang, GCC and MSVC
(-O0 -g).

.. code-block:: c++

   1  struct Struct {
   2    union Union {
   3      enum NestedEnum { RED, BLUE };
   4    };
   5    Union U;
   6  };
   7
   8  Struct S;
   9  int test() {
  10    return S.U.BLUE;
  11  }

The above test is used to illustrate a scope issue found in the Clang
compiler:
`PR46466 (Bugs LLVM) <https://bugs.llvm.org/show_bug.cgi?id=46466>`_ /
`PR45811 (GitHub LLVM) <https://github.com/llvm/llvm-project/issues/45811>`_

These are the logical views that :program:`llvm-debuginfo-analyzer`
generates for 3 different compilers (MSVC, Clang and GCC), emitting
different debug information formats (CodeView, DWARF) on different
platforms.

.. code-block:: none

  llvm-debuginfo-analyzer --attribute=level,format,producer
                          --output-sort=name
                          --print=symbols,types
                          pr-46466-codeview-clang.o
                          pr-46466-codeview-msvc.o
                          pr-46466-dwarf-clang.o
                          pr-46466-dwarf-gcc.o

CodeView - Clang (Windows)
^^^^^^^^^^^^^^^^^^^^^^^^^^

.. code-block:: none

  Logical View:
  [000]           {File} 'pr-46466-codeview-clang.o' -> COFF-x86-64

  [001]             {CompileUnit} 'pr-46466.cpp'
  [002]               {Producer} 'clang version 14.0.0'
  [002]               {Variable} extern 'S' -> 'Struct'
  [002]     1         {Struct} 'Struct'
  [003]                 {Member} public 'U' -> 'Union'
  [003]     2           {Union} 'Union'
  [004]     3             {Enumeration} 'NestedEnum' -> 'int'
  [005]                     {Enumerator} 'BLUE' = '0x1'
  [005]                     {Enumerator} 'RED' = '0x0'

CodeView - MSVC (Windows)
^^^^^^^^^^^^^^^^^^^^^^^^^

.. code-block:: none

  Logical View:
  [000]           {File} 'pr-46466-codeview-msvc.o' -> COFF-i386

  [001]             {CompileUnit} 'pr-46466.cpp'
  [002]               {Producer} 'Microsoft (R) Optimizing Compiler'
  [002]               {Variable} extern 'S' -> 'Struct'
  [002]     1         {Struct} 'Struct'
  [003]                 {Member} public 'U' -> 'Union'
  [003]     2           {Union} 'Union'
  [004]     3             {Enumeration} 'NestedEnum' -> 'int'
  [005]                     {Enumerator} 'BLUE' = '0x1'
  [005]                     {Enumerator} 'RED' = '0x0'

DWARF - Clang (Linux)
^^^^^^^^^^^^^^^^^^^^^

.. code-block:: none

  Logical View:
  [000]           {File} 'pr-46466-dwarf-clang.o' -> elf64-x86-64

  [001]             {CompileUnit} 'pr-46466.cpp'
  [002]               {Producer} 'clang version 14.0.0'
  [002]     8         {Variable} extern 'S' -> 'Struct'
  [002]     1         {Struct} 'Struct'
  [003]     5           {Member} public 'U' -> 'Union'

DWARF - GCC (Linux)
^^^^^^^^^^^^^^^^^^^

.. code-block:: none

  Logical View:
  [000]           {File} 'pr-46466-dwarf-gcc.o' -> elf64-x86-64

  [001]             {CompileUnit} 'pr-46466.cpp'
  [002]               {Producer} 'GNU C++14 9.3.0'
  [002]     8         {Variable} extern 'S' -> 'Struct'
  [002]     1         {Struct} 'Struct'
  [003]     5           {Member} public 'U' -> 'Union'
  [003]     2           {Union} 'Union'
  [004]     3             {Enumeration} 'NestedEnum' -> 'unsigned int'
  [005]                     {Enumerator} 'BLUE' = '0x1'
  [005]                     {Enumerator} 'RED' = '0x0'

From the previous logical views, we can see that the DWARF debug
information generated by the Clang compiler does not include any
references to the enumerators **RED** and **BLUE**. The DWARF
generated by GCC, CodeView generated by Clang and MSVC, they do
include such references.

Using the :program:`llvm-debuginfo-analyzer` selection facilities, we
can produce a logical view showing just the logical types that are
**Enumerator** and its parents. The logical view is sorted by the types
name.

.. code-block:: none

  llvm-debuginfo-analyzer --attribute=format,level
                          --output-sort=name
                          --select-types=Enumerator
                          --report=parents
                          --print=types
                          pr-46466-*.o

.. code-block:: none

  Logical View:
  [000]           {File} 'pr-46466-codeview-clang.o' -> COFF-x86-64

  [001]             {CompileUnit} 'pr-46466.cpp'
  [002]     1         {Struct} 'Struct'
  [003]     2           {Union} 'Union'
  [004]     3             {Enumeration} 'NestedEnum' -> 'int'
  [005]                     {Enumerator} 'BLUE' = '0x1'
  [005]                     {Enumerator} 'RED' = '0x0'

  Logical View:
  [000]           {File} 'pr-46466-codeview-msvc.o' -> COFF-i386

  [001]             {CompileUnit} 'pr-46466.cpp'
  [002]     1         {Struct} 'Struct'
  [003]     2           {Union} 'Union'
  [004]     3             {Enumeration} 'NestedEnum' -> 'int'
  [005]                     {Enumerator} 'BLUE' = '0x1'
  [005]                     {Enumerator} 'RED' = '0x0'

  Logical View:
  [000]           {File} 'pr-46466-dwarf-clang.o' -> elf64-x86-64

  [001]             {CompileUnit} 'pr-46466.cpp'

  Logical View:
  [000]           {File} 'pr-46466-dwarf-gcc.o' -> elf64-x86-64

  [001]             {CompileUnit} 'pr-46466.cpp'
  [002]     1         {Struct} 'Struct'
  [003]     2           {Union} 'Union'
  [004]     3             {Enumeration} 'NestedEnum' -> 'unsigned int'
  [005]                     {Enumerator} 'BLUE' = '0x1'
  [005]                     {Enumerator} 'RED' = '0x0'

Using the :program:`llvm-debuginfo-analyzer` selection facilities, we
can produce a simple tabular output including a summary for the logical
types that are **Enumerator**. The logical view is sorted by the types
name.

.. code-block:: none

  llvm-debuginfo-analyzer --attribute=format,level
                          --output-sort=name
                          --select-types=Enumerator
                          --print=types,summary
                          pr-46466-*.o

.. code-block:: none

  Logical View:
  [000]           {File} 'pr-46466-codeview-clang.o' -> COFF-x86-64

  [001]           {CompileUnit} 'pr-46466.cpp'
  [005]           {Enumerator} 'BLUE' = '0x1'
  [005]           {Enumerator} 'RED' = '0x0'

  -----------------------------
  Element      Total      Found
  -----------------------------
  Scopes           5          0
  Symbols          2          0
  Types            6          2
  Lines            0          0
  -----------------------------
  Total           13          2

  Logical View:
  [000]           {File} 'pr-46466-codeview-msvc.o' -> COFF-i386

  [001]           {CompileUnit} 'pr-46466.cpp'
  [005]           {Enumerator} 'BLUE' = '0x1'
  [005]           {Enumerator} 'RED' = '0x0'

  -----------------------------
  Element      Total      Found
  -----------------------------
  Scopes           5          0
  Symbols          2          0
  Types            7          2
  Lines            0          0
  -----------------------------
  Total           14          2

  Logical View:
  [000]           {File} 'pr-46466-dwarf-clang.o' -> elf64-x86-64

  [001]           {CompileUnit} 'pr-46466.cpp'

  -----------------------------
  Element      Total      Found
  -----------------------------
  Scopes           4          0
  Symbols          0          0
  Types            0          0
  Lines            0          0
  -----------------------------
  Total            4          0

  Logical View:
  [000]           {File} 'pr-46466-dwarf-gcc.o' -> elf64-x86-64

  [001]           {CompileUnit} 'pr-46466.cpp'
  [005]           {Enumerator} 'BLUE' = '0x1'
  [005]           {Enumerator} 'RED' = '0x0'

  -----------------------------
  Element      Total      Found
  -----------------------------
  Scopes           5          0
  Symbols          0          0
  Types            2          2
  Lines            0          0
  -----------------------------
  Total            7          2

From the values printed under the **Found** column, we can see that no
**Types** were found in the DWARF debug information generated by Clang.

TEST CASE 5 - INCORRECT LEXICAL SCOPE FOR VARIABLE
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
The below example is used to show different output generated by
:program:`llvm-debuginfo-analyzer`. We compiled the example for an X86
Codeview and ELF targets with recent versions of Clang, GCC and MSVC
(-O0 -g).

.. code-block:: c++

  // definitions.h
  #ifdef _MSC_VER
    #define forceinline __forceinline
  #elif defined(__clang__)
    #if __has_attribute(__always_inline__)
      #define forceinline inline __attribute__((__always_inline__))
    #else
      #define forceinline inline
    #endif
  #elif defined(__GNUC__)
    #define forceinline inline __attribute__((__always_inline__))
  #else
    #define forceinline inline
    #error
  #endif

As the test is dependent on inline compiler options, the above header
file defines *forceinline*.

.. code-block:: c++

   #include "definitions.h"

.. code-block:: c++

   1  #include "definitions.h"
   2  forceinline int InlineFunction(int Param) {
   3    int Var_1 = Param;
   4    {
   5      int Var_2 = Param + Var_1;
   6      Var_1 = Var_2;
   7    }
   8    return Var_1;
   9  }
  10
  11  int test(int Param_1, int Param_2) {
  12    int A = Param_1;
  13    A += InlineFunction(Param_2);
  14    return A;
  15  }

The above test is used to illustrate a variable issue found in the Clang
compiler:
`PR43860 (Bugs LLVM) <https://bugs.llvm.org/show_bug.cgi?id=43860>`_ /
`PR43205 (GitHub) <https://github.com/llvm/llvm-project/issues/43205>`_

These are the logical views that :program:`llvm-debuginfo-analyzer`
generates for 3 different compilers (MSVC, Clang and GCC), emitting
different debug information formats (CodeView, DWARF) on different
platforms.

.. code-block:: none

  llvm-debuginfo-analyzer --attribute=level,format,producer
                          --output-sort=name
                          --print=symbols
                          pr-43860-codeview-clang.o
                          pr-43860-codeview-msvc.o
                          pr-43860-dwarf-clang.o
                          pr-43860-dwarf-gcc.o

CODEVIEW - Clang (Windows)
^^^^^^^^^^^^^^^^^^^^^^^^^^

.. code-block:: none

  Logical View:
  [000]           {File} 'pr-43860-codeview-clang.o' -> COFF-x86-64

  [001]             {CompileUnit} 'pr-43860.cpp'
  [002]               {Producer} 'clang version 14.0.0'
  [002]     2         {Function} inlined 'InlineFunction' -> 'int'
  [003]                 {Parameter} '' -> 'int'
  [002]               {Function} extern not_inlined 'test' -> 'int'
  [003]                 {Variable} 'A' -> 'int'
  [003]                 {InlinedFunction} inlined 'InlineFunction' -> 'int'
  [004]                   {Parameter} 'Param' -> 'int'
  [004]                   {Variable} 'Var_1' -> 'int'
  [004]                   {Variable} 'Var_2' -> 'int'
  [003]                 {Parameter} 'Param_1' -> 'int'
  [003]                 {Parameter} 'Param_2' -> 'int'

CODEVIEW - MSVC (Windows)
^^^^^^^^^^^^^^^^^^^^^^^^^

.. code-block:: none

  Logical View:
  [000]           {File} 'pr-43860-codeview-msvc.o' -> COFF-i386

  [001]             {CompileUnit} 'pr-43860.cpp'
  [002]               {Producer} 'Microsoft (R) Optimizing Compiler'
  [002]               {Function} extern not_inlined 'InlineFunction' -> 'int'
  [003]                 {Block}
  [004]                   {Variable} 'Var_2' -> 'int'
  [003]                 {Variable} 'Param' -> 'int'
  [003]                 {Variable} 'Var_1' -> 'int'
  [002]               {Function} extern not_inlined 'test' -> 'int'
  [003]                 {Variable} 'A' -> 'int'
  [003]                 {Variable} 'Param_1' -> 'int'
  [003]                 {Variable} 'Param_2' -> 'int'

DWARF - Clang (Linux)
^^^^^^^^^^^^^^^^^^^^^

.. code-block:: none

  Logical View:
  [000]           {File} 'pr-43860-dwarf-clang.o' -> elf64-x86-64

  [001]             {CompileUnit} 'pr-43860.cpp'
  [002]               {Producer} 'clang version 14.0.0'
  [002]     2         {Function} extern inlined 'InlineFunction' -> 'int'
  [003]                 {Block}
  [004]     5             {Variable} 'Var_2' -> 'int'
  [003]     2           {Parameter} 'Param' -> 'int'
  [003]     3           {Variable} 'Var_1' -> 'int'
  [002]    11         {Function} extern not_inlined 'test' -> 'int'
  [003]    12           {Variable} 'A' -> 'int'
  [003]    14           {InlinedFunction} inlined 'InlineFunction' -> 'int'
  [004]                   {Block}
  [005]                     {Variable} 'Var_2' -> 'int'
  [004]                   {Parameter} 'Param' -> 'int'
  [004]                   {Variable} 'Var_1' -> 'int'
  [003]    11           {Parameter} 'Param_1' -> 'int'
  [003]    11           {Parameter} 'Param_2' -> 'int'

DWARF - GCC (Linux)
^^^^^^^^^^^^^^^^^^^

.. code-block:: none

  Logical View:
  [000]           {File} 'pr-43860-dwarf-gcc.o' -> elf64-x86-64

  [001]             {CompileUnit} 'pr-43860.cpp'
  [002]               {Producer} 'GNU C++14 9.3.0'
  [002]     2         {Function} extern declared_inlined 'InlineFunction' -> 'int'
  [003]                 {Block}
  [004]     5             {Variable} 'Var_2' -> 'int'
  [003]     2           {Parameter} 'Param' -> 'int'
  [003]     3           {Variable} 'Var_1' -> 'int'
  [002]    11         {Function} extern not_inlined 'test' -> 'int'
  [003]    12           {Variable} 'A' -> 'int'
  [003]    13           {InlinedFunction} declared_inlined 'InlineFunction' -> 'int'
  [004]                   {Block}
  [005]                     {Variable} 'Var_2' -> 'int'
  [004]                   {Parameter} 'Param' -> 'int'
  [004]                   {Variable} 'Var_1' -> 'int'
  [003]    11           {Parameter} 'Param_1' -> 'int'
  [003]    11           {Parameter} 'Param_2' -> 'int'

From the previous logical views, we can see that the CodeView debug
information generated by the Clang compiler shows the variables **Var_1**
and **Var_2** are at the same lexical scope (**4**) in the function
**InlineFuction**. The DWARF generated by GCC/Clang and CodeView
generated by MSVC, show those variables at the correct lexical scope:
**3** and **4** respectively.

Using the :program:`llvm-debuginfo-analyzer` selection facilities, we
can produce a simple tabular output showing just the logical elements
that have in their name the *var* pattern. The logical view is sorted
by the variables name.

.. code-block:: none

  llvm-debuginfo-analyzer --attribute=level,format
                          --output-sort=name
                          --select-regex --select-nocase --select=Var
                          --report=list
                          --print=symbols
                          pr-43860-*.o

.. code-block:: none

  Logical View:
  [000]           {File} 'pr-43860-codeview-clang.o' -> COFF-x86-64

  [001]           {CompileUnit} 'pr-43860.cpp'
  [004]           {Variable} 'Var_1' -> 'int'
  [004]           {Variable} 'Var_2' -> 'int'

  Logical View:
  [000]           {File} 'pr-43860-codeview-msvc.o' -> COFF-i386

  [001]           {CompileUnit} 'pr-43860.cpp'
  [003]           {Variable} 'Var_1' -> 'int'
  [004]           {Variable} 'Var_2' -> 'int'

  Logical View:
  [000]           {File} 'pr-43860-dwarf-clang.o' -> elf64-x86-64

  [001]           {CompileUnit} 'pr-43860.cpp'
  [004]           {Variable} 'Var_1' -> 'int'
  [003]     3     {Variable} 'Var_1' -> 'int'
  [005]           {Variable} 'Var_2' -> 'int'
  [004]     5     {Variable} 'Var_2' -> 'int'

  Logical View:
  [000]           {File} 'pr-43860-dwarf-gcc.o' -> elf64-x86-64

  [001]           {CompileUnit} 'pr-43860.cpp'
  [004]           {Variable} 'Var_1' -> 'int'
  [003]     3     {Variable} 'Var_1' -> 'int'
  [005]           {Variable} 'Var_2' -> 'int'
  [004]     5     {Variable} 'Var_2' -> 'int'

It also shows, that the CodeView debug information does not generate
source code line numbers for the those logical symbols. The logical
view is sorted by the types name.

TEST CASE 6 - FULL LOGICAL VIEW
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
For advanced users, :program:`llvm-debuginfo-analyzer` can display low
level information that includes offsets within the debug information
section, debug location operands, linkage names, etc.

.. code-block:: none

  llvm-debuginfo-analyzer --attribute=all
                          --print=all
                          test-dwarf-clang.o

  Logical View:
  [0x0000000000][000]            {File} 'test-dwarf-clang.o' -> elf64-x86-64

  [0x000000000b][001]              {CompileUnit} 'test.cpp'
  [0x000000000b][002]                {Producer} 'clang version 12.0.0'
                                     {Directory} ''
                                     {File} 'test.cpp'
                                     {Public} 'foo' [0x0000000000:0x000000003a]
  [0x000000000b][002]                {Range} Lines 2:9 [0x0000000000:0x000000003a]
  [0x00000000bc][002]                {BaseType} 'bool'
  [0x0000000099][002]                {BaseType} 'int'
  [0x00000000b5][002]                {BaseType} 'unsigned int'

  [0x00000000a0][002]   {Source} '/test.cpp'
  [0x00000000a0][002]      1         {TypeAlias} 'INTPTR' -> [0x00000000ab]'* const int'
  [0x000000002a][002]      2         {Function} extern not_inlined 'foo' -> [0x0000000099]'int'
  [0x000000002a][003]                  {Range} Lines 2:9 [0x0000000000:0x000000003a]
  [0x000000002a][003]                  {Linkage}  0x2 '_Z3fooPKijb'
  [0x0000000071][003]                  {Block}
  [0x0000000071][004]                    {Range} Lines 5:8 [0x000000001c:0x000000002f]
  [0x000000007e][004]      5             {Variable} 'CONSTANT' -> [0x00000000c3]'const INTEGER'
  [0x000000007e][005]                      {Coverage} 100.00%
  [0x000000007f][005]                      {Location}
  [0x000000007f][006]                        {Entry} Stack Offset: -28 (0xffffffffffffffe4) [DW_OP_fbreg]
  [0x000000001c][004]      5             {Line} {NewStatement} '/test.cpp'
  [0x000000001c][004]                    {Code} 'movl	$0x7, -0x1c(%rbp)'
  [0x0000000023][004]      6             {Line} {NewStatement} '/test.cpp'
  [0x0000000023][004]                    {Code} 'movl	$0x7, -0x4(%rbp)'
  [0x000000002a][004]                    {Code} 'jmp	0x6'
  [0x000000002f][004]      8             {Line} {NewStatement} '/test.cpp'
  [0x000000002f][004]                    {Code} 'movl	-0x14(%rbp), %eax'
  [0x0000000063][003]      2           {Parameter} 'ParamBool' -> [0x00000000bc]'bool'
  [0x0000000063][004]                    {Coverage} 100.00%
  [0x0000000064][004]                    {Location}
  [0x0000000064][005]                      {Entry} Stack Offset: -21 (0xffffffffffffffeb) [DW_OP_fbreg]
  [0x0000000047][003]      2           {Parameter} 'ParamPtr' -> [0x00000000a0]'INTPTR'
  [0x0000000047][004]                    {Coverage} 100.00%
  [0x0000000048][004]                    {Location}
  [0x0000000048][005]                      {Entry} Stack Offset: -16 (0xfffffffffffffff0) [DW_OP_fbreg]
  [0x0000000055][003]      2           {Parameter} 'ParamUnsigned' -> [0x00000000b5]'unsigned int'
  [0x0000000055][004]                    {Coverage} 100.00%
  [0x0000000056][004]                    {Location}
  [0x0000000056][005]                      {Entry} Stack Offset: -20 (0xffffffffffffffec) [DW_OP_fbreg]
  [0x000000008d][003]      4           {TypeAlias} 'INTEGER' -> [0x0000000099]'int'
  [0x0000000000][003]      2           {Line} {NewStatement} '/test.cpp'
  [0x0000000000][003]                  {Code} 'pushq	%rbp'
  [0x0000000001][003]                  {Code} 'movq	%rsp, %rbp'
  [0x0000000004][003]                  {Code} 'movb	%dl, %al'
  [0x0000000006][003]                  {Code} 'movq	%rdi, -0x10(%rbp)'
  [0x000000000a][003]                  {Code} 'movl	%esi, -0x14(%rbp)'
  [0x000000000d][003]                  {Code} 'andb	$0x1, %al'
  [0x000000000f][003]                  {Code} 'movb	%al, -0x15(%rbp)'
  [0x0000000012][003]      3           {Line} {NewStatement} {PrologueEnd} '/test.cpp'
  [0x0000000012][003]                  {Code} 'testb	$0x1, -0x15(%rbp)'
  [0x0000000016][003]                  {Code} 'je	0x13'
  [0x0000000032][003]      8           {Line} '/test.cpp'
  [0x0000000032][003]                  {Code} 'movl	%eax, -0x4(%rbp)'
  [0x0000000035][003]      9           {Line} {NewStatement} '/test.cpp'
  [0x0000000035][003]                  {Code} 'movl	-0x4(%rbp), %eax'
  [0x0000000038][003]                  {Code} 'popq	%rbp'
  [0x0000000039][003]                  {Code} 'retq'
  [0x000000003a][003]      9           {Line} {NewStatement} {EndSequence} '/test.cpp'

  -----------------------------
  Element      Total    Printed
  -----------------------------
  Scopes           3          3
  Symbols          4          4
  Types            5          5
  Lines           25         25
  -----------------------------
  Total           37         37

  Scope Sizes:
         189 (100.00%) : [0x000000000b][001]              {CompileUnit} 'test.cpp'
         110 ( 58.20%) : [0x000000002a][002]      2         {Function} extern not_inlined 'foo' -> [0x0000000099]'int'
          27 ( 14.29%) : [0x0000000071][003]                  {Block}

  Totals by lexical level:
  [001]:        189 (100.00%)
  [002]:        110 ( 58.20%)
  [003]:         27 ( 14.29%)

The **Scope Sizes** table shows the contribution in bytes to the debug
information by each scope, which can be used to determine unexpected
size changes in the DWARF sections between different versions of the
same toolchain.

.. code-block:: none

  [0x000000002a][002]      2         {Function} extern not_inlined 'foo' -> [0x0000000099]'int'
  [0x000000002a][003]                  {Range} Lines 2:9 [0x0000000000:0x000000003a]
  [0x000000002a][003]                  {Linkage}  0x2 '_Z3fooPKijb'
  [0x0000000071][003]                  {Block}
  [0x0000000071][004]                    {Range} Lines 5:8 [0x000000001c:0x000000002f]
  [0x000000007e][004]      5             {Variable} 'CONSTANT' -> [0x00000000c3]'const INTEGER'
  [0x000000007e][005]                      {Coverage} 100.00%
  [0x000000007f][005]                      {Location}
  [0x000000007f][006]                        {Entry} Stack Offset: -28 (0xffffffffffffffe4) [DW_OP_fbreg]

The **{Range}** attribute describe the line ranges for a logical scope.
For this case, the function **foo** is within the lines **2** and **9**.

The **{Coverage}** and **{Location}** attributes describe the debug
location and coverage for logical symbols. For optimized code, the
coverage value decreases and it affects the program debuggability.

WEBASSEMBLY SUPPORT
~~~~~~~~~~~~~~~~~~~
The below example is used to show the WebAssembly output generated by
:program:`llvm-debuginfo-analyzer`. We compiled the example for a
WebAssembly 32-bit target with Clang (-O0 -g --target=wasm32):

.. code-block:: c++

  1  using INTPTR = const int *;
  2  int foo(INTPTR ParamPtr, unsigned ParamUnsigned, bool ParamBool) {
  3    if (ParamBool) {
  4      typedef int INTEGER;
  5      const INTEGER CONSTANT = 7;
  6      return CONSTANT;
  7    }
  8    return ParamUnsigned;
  9  }

PRINT BASIC DETAILS
^^^^^^^^^^^^^^^^^^^
The following command prints basic details for all the logical elements
sorted by the debug information internal offset; it includes its lexical
level and debug info format.

.. code-block:: none

  llvm-debuginfo-analyzer --attribute=level,format
                          --output-sort=offset
                          --print=scopes,symbols,types,lines,instructions
                          test-clang.o

or

.. code-block:: none

  llvm-debuginfo-analyzer --attribute=level,format
                          --output-sort=offset
                          --print=elements
                          test-clang.o

Each row represents an element that is present within the debug
information. The first column represents the scope level, followed by
the associated line number (if any), and finally the description of
the element.

.. code-block:: none

  Logical View:
  [000]           {File} 'test-clang.o' -> WASM

  [001]             {CompileUnit} 'test.cpp'
  [002]     2         {Function} extern not_inlined 'foo' -> 'int'
  [003]     2           {Parameter} 'ParamPtr' -> 'INTPTR'
  [003]     2           {Parameter} 'ParamUnsigned' -> 'unsigned int'
  [003]     2           {Parameter} 'ParamBool' -> 'bool'
  [003]                 {Block}
  [004]     5             {Variable} 'CONSTANT' -> 'const INTEGER'
  [004]     5             {Line}
  [004]                   {Code} 'i32.const	7'
  [004]                   {Code} 'local.set	10'
  [004]                   {Code} 'local.get	5'
  [004]                   {Code} 'local.get	10'
  [004]                   {Code} 'i32.store	12'
  [004]     6             {Line}
  [004]                   {Code} 'i32.const	7'
  [004]                   {Code} 'local.set	11'
  [004]                   {Code} 'local.get	5'
  [004]                   {Code} 'local.get	11'
  [004]                   {Code} 'i32.store	28'
  [004]                   {Code} 'br      	1'
  [004]     -             {Line}
  [004]                   {Code} 'end'
  [003]     4           {TypeAlias} 'INTEGER' -> 'int'
  [003]     2           {Line}
  [003]                 {Code} 'nop'
  [003]                 {Code} 'end'
  [003]                 {Code} 'i64.div_s'
  [003]                 {Code} 'global.get	0'
  [003]                 {Code} 'local.set	3'
  [003]                 {Code} 'i32.const	32'
  [003]                 {Code} 'local.set	4'
  [003]                 {Code} 'local.get	3'
  [003]                 {Code} 'local.get	4'
  [003]                 {Code} 'i32.sub'
  [003]                 {Code} 'local.set	5'
  [003]                 {Code} 'local.get	5'
  [003]                 {Code} 'local.get	0'
  [003]                 {Code} 'i32.store	24'
  [003]                 {Code} 'local.get	5'
  [003]                 {Code} 'local.get	1'
  [003]                 {Code} 'i32.store	20'
  [003]                 {Code} 'local.get	2'
  [003]                 {Code} 'local.set	6'
  [003]                 {Code} 'local.get	5'
  [003]                 {Code} 'local.get	6'
  [003]                 {Code} 'i32.store8	19'
  [003]     3           {Line}
  [003]                 {Code} 'local.get	5'
  [003]                 {Code} 'i32.load8_u	19'
  [003]                 {Code} 'local.set	7'
  [003]     3           {Line}
  [003]                 {Code} 'i32.const	1'
  [003]                 {Code} 'local.set	8'
  [003]                 {Code} 'local.get	7'
  [003]                 {Code} 'local.get	8'
  [003]                 {Code} 'i32.and'
  [003]                 {Code} 'local.set	9'
  [003]                 {Code} 'block'
  [003]                 {Code} 'block'
  [003]                 {Code} 'local.get	9'
  [003]                 {Code} 'i32.eqz'
  [003]                 {Code} 'br_if   	0'
  [003]     8           {Line}
  [003]                 {Code} 'local.get	5'
  [003]                 {Code} 'i32.load	20'
  [003]                 {Code} 'local.set	12'
  [003]     8           {Line}
  [003]                 {Code} 'local.get	5'
  [003]                 {Code} 'local.get	12'
  [003]                 {Code} 'i32.store	28'
  [003]     -           {Line}
  [003]                 {Code} 'end'
  [003]     9           {Line}
  [003]                 {Code} 'local.get	5'
  [003]                 {Code} 'i32.load	28'
  [003]                 {Code} 'local.set	13'
  [003]                 {Code} 'local.get	13'
  [003]                 {Code} 'return'
  [003]                 {Code} 'end'
  [003]     9           {Line}
  [003]                 {Code} 'unreachable'
  [002]     1         {TypeAlias} 'INTPTR' -> '* const int'

SELECT LOGICAL ELEMENTS
^^^^^^^^^^^^^^^^^^^^^^^
The following prints all *instructions*, *symbols* and *types* that
contain **'block'** or **'.store'** in their names or types, using a tab
layout and given the number of matches.

.. code-block:: none

  llvm-debuginfo-analyzer --attribute=level
                          --select-nocase --select-regex
                          --select=BLOCK --select=.store
                          --report=list
                          --print=symbols,types,instructions,summary
                          test-clang.o

  Logical View:
  [000]           {File} 'test-clang.o'

  [001]           {CompileUnit} 'test.cpp'
  [003]           {Code} 'block'
  [003]           {Code} 'block'
  [004]           {Code} 'i32.store	12'
  [003]           {Code} 'i32.store	20'
  [003]           {Code} 'i32.store	24'
  [004]           {Code} 'i32.store	28'
  [003]           {Code} 'i32.store	28'
  [003]           {Code} 'i32.store8	19'

  -----------------------------
  Element      Total    Printed
  -----------------------------
  Scopes           3          0
  Symbols          4          0
  Types            2          0
  Lines           62          8
  -----------------------------
  Total           71          8

COMPARISON MODE
^^^^^^^^^^^^^^^
Given the previous example we found the above debug information issue
(related to the previous invalid scope location for the **'typedef int
INTEGER'**) by comparing against another compiler.

Using GCC to generate test-dwarf-gcc.o, we can apply a selection pattern
with the printing mode to obtain the following logical view output.

.. code-block:: none

  llvm-debuginfo-analyzer --attribute=level
                          --select-regex --select-nocase --select=INTe
                          --report=list
                          --print=symbols,types
                          test-clang.o test-dwarf-gcc.o

  Logical View:
  [000]           {File} 'test-clang.o'

  [001]           {CompileUnit} 'test.cpp'
  [003]     4     {TypeAlias} 'INTEGER' -> 'int'
  [004]     5     {Variable} 'CONSTANT' -> 'const INTEGER'

  Logical View:
  [000]           {File} 'test-dwarf-gcc.o'

  [001]           {CompileUnit} 'test.cpp'
  [004]     4     {TypeAlias} 'INTEGER' -> 'int'
  [004]     5     {Variable} 'CONSTANT' -> 'const INTEGER'

The output shows that both objects contain the same elements. But the
**'typedef INTEGER'** is located at different scope level. The GCC
generated object, shows **'4'**, which is the correct value.

There are 2 comparison methods: logical view and logical elements.

LOGICAL VIEW
""""""""""""
It compares the logical view as a whole unit; for a match, each compared
logical element must have the same parents and children.

The output shows in view form the **missing (-), added (+)** elements,
giving more context by swapping the reference and target object files.

.. code-block:: none

  llvm-debuginfo-analyzer --attribute=level
                          --compare=types
                          --report=view
                          --print=symbols,types
                          test-clang.o test-dwarf-gcc.o

  Reference: 'test-clang.o'
  Target:    'test-dwarf-gcc.o'

  Logical View:
   [000]           {File} 'test-clang.o'

   [001]             {CompileUnit} 'test.cpp'
   [002]     1         {TypeAlias} 'INTPTR' -> '* const int'
   [002]     2         {Function} extern not_inlined 'foo' -> 'int'
   [003]                 {Block}
   [004]     5             {Variable} 'CONSTANT' -> 'const INTEGER'
  +[004]     4             {TypeAlias} 'INTEGER' -> 'int'
   [003]     2           {Parameter} 'ParamBool' -> 'bool'
   [003]     2           {Parameter} 'ParamPtr' -> 'INTPTR'
   [003]     2           {Parameter} 'ParamUnsigned' -> 'unsigned int'
  -[003]     4           {TypeAlias} 'INTEGER' -> 'int'

The output shows the merging view path (reference and target) with the
missing and added elements.

LOGICAL ELEMENTS
""""""""""""""""
It compares individual logical elements without considering if their
parents are the same. For both comparison methods, the equal criteria
includes the name, source code location, type, lexical scope level.

.. code-block:: none

  llvm-debuginfo-analyzer --attribute=level
                          --compare=types
                          --report=list
                          --print=symbols,types,summary
                          test-clang.o test-dwarf-gcc.o

  Reference: 'test-clang.o'
  Target:    'test-dwarf-gcc.o'

  (1) Missing Types:
  -[003]     4     {TypeAlias} 'INTEGER' -> 'int'

  (1) Added Types:
  +[004]     4     {TypeAlias} 'INTEGER' -> 'int'

  ----------------------------------------
  Element   Expected    Missing      Added
  ----------------------------------------
  Scopes           4          0          0
  Symbols          0          0          0
  Types            2          1          1
  Lines            0          0          0
  ----------------------------------------
  Total            6          1          1

Changing the *Reference* and *Target* order:

.. code-block:: none

  llvm-debuginfo-analyzer --attribute=level
                          --compare=types
                          --report=list
                          --print=symbols,types,summary
                          test-dwarf-gcc.o test-clang.o

  Reference: 'test-dwarf-gcc.o'
  Target:    'test-clang.o'

  (1) Missing Types:
  -[004]     4     {TypeAlias} 'INTEGER' -> 'int'

  (1) Added Types:
  +[003]     4     {TypeAlias} 'INTEGER' -> 'int'

  ----------------------------------------
  Element   Expected    Missing      Added
  ----------------------------------------
  Scopes           4          0          0
  Symbols          0          0          0
  Types            2          1          1
  Lines            0          0          0
  ----------------------------------------
  Total            6          1          1

As the *Reference* and *Target* are switched, the *Added Types* from
the first case now are listed as *Missing Types*.

EXIT STATUS
-----------
:program:`llvm-debuginfo-analyzer` returns 0 if the input files were
parsed and printed successfully. Otherwise, it returns 1.

LIMITATIONS AND KNOWN ISSUES
----------------------------
See :download:`Limitations <../../tools/llvm-debuginfo-analyzer/README.md>`.

SEE ALSO
--------
:manpage:`llvm-dwarfdump`
