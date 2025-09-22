tblgen - Description to C++ Code
================================

.. program:: tblgen

SYNOPSIS
--------

:program:`clang-tblgen` [*options*] [*filename*]

:program:`lldb-tblgen` [*options*] [*filename*]

:program:`llvm-tblgen` [*options*] [*filename*]

:program:`mlir-tblgen` [*options*] [*filename*]

DESCRIPTION
-----------

:program:`*-tblgen` is a family of programs that translates target
description (``.td``) files into C++ code and other output formats. Most
users of LLVM will not need to use this program. It is used only for
writing parts of the compiler, debugger, and LLVM target backends.

The details of the input and output of the :program:`*-tblgen` programs is
beyond the scope of this short introduction; please see the :doc:`TableGen
Overview <../TableGen/index>` for an introduction and for references to
additional TableGen documents.

The *filename* argument specifies the name of the Target Description (``.td``)
file that TableGen processes.

OPTIONS
-------

General Options
~~~~~~~~~~~~~~~

.. option:: -help

 Print a description of the command line options.

.. option:: -help-list

  Print a description of the command line options in a simple list format.

.. option:: -D=macroname

  Specify the name of a macro to be defined. The name is defined, but it
  has no particular value.

.. option:: -d=filename

  Specify the name of the dependency filename.

.. option:: -debug

  Enable debug output.

.. option:: -dump-json

 Print a JSON representation of all records, suitable for further
 automated processing.

.. option:: -I directory

 Specify where to find other target description files for inclusion.  The
 ``directory`` value should be a full or partial path to a directory that
 contains target description files.

.. option:: -null-backend

  Parse the source files and build the records, but do not run any
  backend. This is useful for timing the frontend.

.. option:: -o filename

 Specify the output file name.  If ``filename`` is ``-``, then
 :program:`*-tblgen` sends its output to standard output.

.. option:: -print-records

 Print all classes and records to standard output (default backend option).

.. option:: -print-detailed-records

  Print a detailed report of all global variables, classes, and records
  to standard output.

.. option:: -stats

  Print a report with any statistics collected by the backend.

.. option:: -time-phases

  Time the parser and backend phases and print a report.

.. option:: -version

 Show the version number of the program.

.. option:: -write-if-changed

  Write the output file only if it is new or has changed.


clang-tblgen Options
~~~~~~~~~~~~~~~~~~~~

.. option:: -gen-clang-attr-classes

  Generate Clang attribute classes.

.. option:: -gen-clang-attr-parser-string-switches

  Generate all parser-related attribute string switches.

.. option:: -gen-clang-attr-subject-match-rules-parser-string-switches

  Generate all parser-related attribute subject match rule string switches.

.. option:: -gen-clang-attr-impl

  Generate Clang attribute implementations.

.. option:: -gen-clang-attr-list"

  Generate a Clang attribute list.

.. option:: -gen-clang-attr-subject-match-rule-list

  Generate a Clang attribute subject match rule list.

.. option:: -gen-clang-attr-pch-read

  Generate Clang PCH attribute reader.

.. option:: -gen-clang-attr-pch-write

  Generate Clang PCH attribute writer.

.. option:: -gen-clang-attr-has-attribute-impl

  Generate a Clang attribute spelling list.

.. option:: -gen-clang-attr-spelling-index

  Generate a Clang attribute spelling index.

.. option:: -gen-clang-attr-ast-visitor

  Generate a recursive AST visitor for Clang attributes.

.. option:: -gen-clang-attr-template-instantiate

  Generate a Clang template instantiate code.

.. option:: -gen-clang-attr-parsed-attr-list

  Generate a Clang parsed attribute list.

.. option:: -gen-clang-attr-parsed-attr-impl

  Generate the Clang parsed attribute helpers.

.. option:: -gen-clang-attr-parsed-attr-kinds

  Generate a Clang parsed attribute kinds.

.. option:: -gen-clang-attr-text-node-dump

  Generate Clang attribute text node dumper.

.. option:: -gen-clang-attr-node-traverse

  Generate Clang attribute traverser.

.. option:: -gen-clang-diags-defs

  Generate Clang diagnostics definitions.

.. option:: -clang-component component

  Only use warnings from specified component.

.. option:: -gen-clang-diag-groups

  Generate Clang diagnostic groups.

.. option:: -gen-clang-diags-index-name

  Generate Clang diagnostic name index.

.. option:: -gen-clang-basic-reader

  Generate Clang BasicReader classes.

.. option:: -gen-clang-basic-writer

  Generate Clang BasicWriter classes.

.. option:: -gen-clang-comment-nodes

  Generate Clang AST comment nodes.

.. option:: -gen-clang-decl-nodes

  Generate Clang AST declaration nodes.

.. option:: -gen-clang-stmt-nodes

  Generate Clang AST statement nodes.

.. option:: -gen-clang-type-nodes

  Generate Clang AST type nodes.

.. option:: -gen-clang-type-reader

  Generate Clang AbstractTypeReader class.

.. option:: -gen-clang-type-writer

  Generate Clang AbstractTypeWriter class.

.. option:: -gen-clang-opcodes

  Generate Clang constexpr interpreter opcodes.

.. option:: -gen-clang-sa-checkers

  Generate Clang static analyzer checkers.

.. option:: -gen-clang-comment-html-tags

  Generate efficient matchers for HTML tag names that are used in
  documentation comments.

.. option:: -gen-clang-comment-html-tags-properties

  Generate efficient matchers for HTML tag properties.

.. option:: -gen-clang-comment-html-named-character-references

  Generate function to translate named character references to UTF-8 sequences.

.. option:: -gen-clang-comment-command-info

  Generate command properties for commands that are used in documentation comments.

.. option:: -gen-clang-comment-command-list

  Generate list of commands that are used in documentation comments.

.. option:: -gen-clang-opencl-builtins

  Generate OpenCL builtin declaration handlers.

.. option:: -gen-arm-neon

  Generate ``arm_neon.h`` for Clang.

.. option:: -gen-arm-fp16

  Generate ``arm_fp16.h`` for Clang.

.. option:: -gen-arm-bf16

  Generate ``arm_bf16.h`` for Clang.

.. option:: -gen-arm-neon-sema

  Generate ARM NEON sema support for Clang.

.. option:: -gen-arm-neon-test

  Generate ARM NEON tests for Clang.

.. option:: -gen-arm-sve-header

  Generate ``arm_sve.h`` for Clang.

.. option:: -gen-arm-sve-builtins

  Generate ``arm_sve_builtins.inc`` for Clang.

.. option:: -gen-arm-sve-builtin-codegen

  Generate ``arm_sve_builtin_cg_map.inc`` for Clang.

.. option:: -gen-arm-sve-typeflags

  Generate ``arm_sve_typeflags.inc`` for Clang.

.. option:: -gen-arm-sve-sema-rangechecks

  Generate ``arm_sve_sema_rangechecks.inc`` for Clang.

.. option:: -gen-arm-mve-header

  Generate ``arm_mve.h`` for Clang.

.. option:: -gen-arm-mve-builtin-def

  Generate ARM MVE builtin definitions for Clang.

.. option:: -gen-arm-mve-builtin-sema

  Generate ARM MVE builtin sema checks for Clang.

.. option:: -gen-arm-mve-builtin-codegen

  Generate ARM MVE builtin code-generator for Clang.

.. option:: -gen-arm-mve-builtin-aliases

  Generate list of valid ARM MVE builtin aliases for Clang.

.. option:: -gen-arm-cde-header

  Generate ``arm_cde.h`` for Clang.

.. option:: -gen-arm-cde-builtin-def

  Generate ARM CDE builtin definitions for Clang.

.. option:: -gen-arm-cde-builtin-sema

  Generate ARM CDE builtin sema checks for Clang.

.. option:: -gen-arm-cde-builtin-codegen

  Generate ARM CDE builtin code-generator for Clang.

.. option:: -gen-arm-cde-builtin-aliases

  Generate list of valid ARM CDE builtin aliases for Clang.

.. option:: -gen-riscv-vector-header

  Generate ``riscv_vector.h`` for Clang.

.. option:: -gen-riscv-vector-builtins

  Generate ``riscv_vector_builtins.inc`` for Clang.

.. option:: -gen-riscv-vector-builtin-codegen

  Generate ``riscv_vector_builtin_cg.inc`` for Clang.

.. option:: -gen-riscv-sifive-vector-builtins

  Generate ``riscv_sifive_vector_builtins.inc`` for Clang.

.. option:: -gen-riscv-sifive-vector-builtin-codegen

  Generate ``riscv_sifive_vector_builtin_cg.inc`` for Clang.

.. option:: -gen-attr-docs

  Generate attribute documentation.

.. option:: -gen-diag-docs

  Generate diagnostic documentation.

.. option:: -gen-opt-docs

  Generate option documentation.

.. option:: -gen-clang-data-collectors

  Generate data collectors for AST nodes.

.. option:: -gen-clang-test-pragma-attribute-supported-attributes

  Generate a list of attributes supported by ``#pragma`` Clang attribute for
  testing purposes.


lldb-tblgen Options
~~~~~~~~~~~~~~~~~~~

.. option:: gen-lldb-option-defs

  Generate lldb OptionDefinition values.

.. option:: gen-lldb-property-defs

  Generate lldb PropertyDefinition values.

.. option:: gen-lldb-property-enum-defs

  Generate lldb PropertyDefinition enum values.


llvm-tblgen Options
~~~~~~~~~~~~~~~~~~~

.. option:: -gen-asm-matcher

 Generate assembly instruction matcher.

.. option:: -match-prefix=prefix

  Make -gen-asm-matcher match only instructions with the given *prefix*.

.. option:: -gen-asm-parser

 Generate assembly instruction parser.

.. option:: -asmparsernum=n

 Make -gen-asm-parser emit assembly parser number *n*.

.. option:: -gen-asm-writer

 Generate assembly writer.

.. option:: -asmwriternum=n

 Make -gen-asm-writer emit assembly writer number *n*.

.. option:: -gen-attrs

  Generate attributes.

.. option:: -gen-automata

  Generate generic automata.

.. option:: -gen-callingconv

  Generate calling convention descriptions.

.. option:: -gen-compress-inst-emitter

  Generate RISC-V compressed instructions.

.. option:: -gen-ctags

  Generate ctags-compatible index.

.. option:: -gen-dag-isel

 Generate a DAG (directed acyclic graph) instruction selector.

.. option:: -instrument-coverage

  Make -gen-dag-isel generate tables to help identify the patterns matched.

.. option:: -omit-comments

  Make -gen-dag-isel omit comments. The default is false.

.. option:: -gen-dfa-packetizer

 Generate DFA Packetizer for VLIW targets.

.. option:: -gen-directive-decl

  Generate directive related declaration code (header file).

.. option:: -gen-directive-gen

  Generate directive related implementation code part.

.. option:: -gen-directive-impl

  Generate directive related implementation code.

.. option:: -gen-disassembler

  Generate disassembler.

.. option:: -gen-emitter

 Generate machine code emitter.

.. option:: -gen-exegesis

  Generate llvm-exegesis tables.

.. option:: -gen-fast-isel

  Generate a "fast" instruction selector.

.. option:: -gen-global-isel

  Generate GlobalISel selector.

.. option:: -gisel-coverage-file=filename

  Specify the file from which to retrieve coverage information.

.. option:: -instrument-gisel-coverage

  Make -gen-global-isel generate coverage instrumentation.

.. option:: -optimize-match-table

  Make -gen-global-isel generate an optimized version of the match table.

.. option:: -warn-on-skipped-patterns

  Make -gen-global-isel explain why a pattern was skipped for inclusion.

.. option:: -gen-global-isel-combiner

  Generate GlobalISel combiner.

.. option:: -combiners=list

  Make -gen-global-isel-combiner emit the specified combiners.

.. option:: -gicombiner-debug-cxxpreds

  Add debug comments to all C++ predicates emitted by -gen-global-isel-combiner

.. option:: -gicombiner-stop-after-parse

  Make -gen-global-isel-combiner stop processing after parsing rules and dump state.

.. option:: -gen-instr-info

 Generate instruction descriptions.

.. option:: -gen-instr-docs

 Generate instruction documentation.

.. option:: -gen-intrinsic-enums

 Generate intrinsic enums.

.. option:: -intrinsic-prefix=prefix

  Make -gen-intrinsic-enums generate intrinsics with this target *prefix*.

.. option:: -gen-intrinsic-impl

 Generate intrinsic information.

.. option:: -gen-opt-parser-defs

  Generate options definitions.

.. option:: -gen-opt-rst

  Generate option RST.

.. option:: -gen-pseudo-lowering

 Generate pseudo instruction lowering.

.. option:: -gen-register-bank

  Generate register bank descriptions.

.. option:: -gen-register-info

  Generate registers and register classes info.

.. option:: -register-info-debug

  Make -gen-register-info dump register information for debugging.

.. option:: -gen-searchable-tables

  Generate generic searchable tables. See :doc:`TableGen BackEnds <../TableGen/BackEnds>`
  for a detailed description.

.. option:: -gen-subtarget

 Generate subtarget enumerations.

.. option:: -gen-x86-EVEX2VEX-tables

  Generate X86 EVEX to VEX compress tables.

.. option:: -gen-x86-fold-tables

  Generate X86 fold tables.

.. option:: -long-string-literals

  When emitting large string tables, prefer string literals over
  comma-separated char literals. This can be a readability and
  compile-time performance win, but upsets some compilers.

.. option:: -print-enums

 Print enumeration values for a class.

.. option:: -class=classname

 Make -print-enums print the enumeration list for the specified class.

.. option:: -print-sets

 Print expanded sets for testing DAG exprs.


mlir-tblgen Options
~~~~~~~~~~~~~~~~~~~

.. option:: -gen-avail-interface-decls

  Generate availability interface declarations.

.. option:: -gen-avail-interface-defs

  Generate op interface definitions.

.. option:: -gen-dialect-doc

  Generate dialect documentation.

.. option:: -dialect

  The dialect to generate.

.. option:: -gen-directive-decl

  Generate declarations for directives (OpenMP, etc.).

.. option:: -gen-enum-decls

  Generate enum utility declarations.

.. option:: -gen-enum-defs

  Generate enum utility definitions.

.. option:: -gen-enum-from-llvmir-conversions

  Generate conversions of EnumAttrs from LLVM IR.

.. option:: -gen-enum-to-llvmir-conversions

  Generate conversions of EnumAttrs to LLVM IR.

.. option:: -gen-llvmir-conversions

  Generate LLVM IR conversions.

.. option:: -gen-llvmir-intrinsics

  Generate LLVM IR intrinsics.

.. option:: -llvmir-intrinsics-filter

  Only keep the intrinsics with the specified substring in their record name.

.. option:: -dialect-opclass-base

  The base class for the ops in the dialect we are to emit.

.. option:: -gen-op-decls

  Generate operation declarations.

.. option:: -gen-op-defs

  Generate operation definitions.

.. option:: -asmformat-error-is-fatal

  Emit a fatal error if format parsing fails.

.. option:: -op-exclude-regex

  Regular expression of name of ops to exclude (no filter if empty).

.. option:: -op-include-regex

  Regular expression of name of ops to include (no filter if empty).

.. option:: -gen-op-doc

  Generate operation documentation.

.. option:: -gen-pass-decls

  Generate operation documentation.

.. option:: -name namestring

  The name of this group of passes.

.. option:: -gen-pass-doc

  Generate pass documentation.

.. option:: -gen-rewriters

  Generate pattern rewriters.

.. option:: -gen-spirv-avail-impls

  Generate SPIR-V operation utility definitions.

.. option:: -gen-spirv-capability-implication

  Generate utility function to return implied capabilities for a given capability.

.. option:: -gen-spirv-enum-avail-decls

  Generate SPIR-V enum availability declarations.

.. option:: -gen-spirv-enum-avail-defs

  Generate SPIR-V enum availability definitions.

.. option:: -gen-spirv-op-utils

  Generate SPIR-V operation utility definitions.

.. option:: -gen-spirv-serialization

  Generate SPIR-V (de)serialization utilities and functions.

.. option:: -gen-struct-attr-decls

  Generate struct utility declarations.

.. option:: -gen-struct-attr-defs

  Generate struct utility definitions.

.. option:: -gen-typedef-decls

  Generate TypeDef declarations.

.. option:: -gen-typedef-defs

  Generate TypeDef definitions.

.. option:: -typedefs-dialect name

  Generate types for this dialect.

EXIT STATUS
-----------

If :program:`*-tblgen` succeeds, it will exit with 0.  Otherwise, if an error
occurs, it will exit with a non-zero value.
