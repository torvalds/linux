.. role:: raw-html(raw)
   :format: html

=================================
LLVM Code Coverage Mapping Format
=================================

.. contents::
   :local:

Introduction
============

LLVM's code coverage mapping format is used to provide code coverage
analysis using LLVM's and Clang's instrumentation based profiling
(Clang's ``-fprofile-instr-generate`` option).

This document is aimed at those who would like to know how LLVM's code coverage
mapping works under the hood. A prior knowledge of how Clang's profile guided
optimization works is useful, but not required. For those interested in using
LLVM to provide code coverage analysis for their own programs, see the `Clang
documentation <https://clang.llvm.org/docs/SourceBasedCodeCoverage.html>`.

We start by briefly describing LLVM's code coverage mapping format and the
way that Clang and LLVM's code coverage tool work with this format. After
the basics are down, more advanced features of the coverage mapping format
are discussed - such as the data structures, LLVM IR representation and
the binary encoding.

High Level Overview
===================

LLVM's code coverage mapping format is designed to be a self contained
data format that can be embedded into the LLVM IR and into object files.
It's described in this document as a **mapping** format because its goal is
to store the data that is required for a code coverage tool to map between
the specific source ranges in a file and the execution counts obtained
after running the instrumented version of the program.

The mapping data is used in two places in the code coverage process:

1. When clang compiles a source file with ``-fcoverage-mapping``, it
   generates the mapping information that describes the mapping between the
   source ranges and the profiling instrumentation counters.
   This information gets embedded into the LLVM IR and conveniently
   ends up in the final executable file when the program is linked.

2. It is also used by *llvm-cov* - the mapping information is extracted from an
   object file and is used to associate the execution counts (the values of the
   profile instrumentation counters), and the source ranges in a file.
   After that, the tool is able to generate various code coverage reports
   for the program.

The coverage mapping format aims to be a "universal format" that would be
suitable for usage by any frontend, and not just by Clang. It also aims to
provide the frontend the possibility of generating the minimal coverage mapping
data in order to reduce the size of the IR and object files - for example,
instead of emitting mapping information for each statement in a function, the
frontend is allowed to group the statements with the same execution count into
regions of code, and emit the mapping information only for those regions.

Advanced Concepts
=================

The remainder of this guide is meant to give you insight into the way the
coverage mapping format works.

The coverage mapping format operates on a per-function level as the
profile instrumentation counters are associated with a specific function.
For each function that requires code coverage, the frontend has to create
coverage mapping data that can map between the source code ranges and
the profile instrumentation counters for that function.

Mapping Region
--------------

The function's coverage mapping data contains an array of mapping regions.
A mapping region stores the `source code range`_ that is covered by this region,
the `file id <coverage file id_>`_, the `coverage mapping counter`_ and
the region's kind.
There are several kinds of mapping regions:

* Code regions associate portions of source code and `coverage mapping
  counters`_. They make up the majority of the mapping regions. They are used
  by the code coverage tool to compute the execution counts for lines,
  highlight the regions of code that were never executed, and to obtain
  the various code coverage statistics for a function.
  For example:

  :raw-html:`<pre class='highlight' style='line-height:initial;'><span>int main(int argc, const char *argv[]) </span><span style='background-color:#4A789C'>{    </span> <span class='c1'>// Code Region from 1:40 to 9:2</span>
  <span style='background-color:#4A789C'>                                            </span>
  <span style='background-color:#4A789C'>  if (argc &gt; 1) </span><span style='background-color:#85C1F5'>{                         </span>   <span class='c1'>// Code Region from 3:17 to 5:4</span>
  <span style='background-color:#85C1F5'>    printf("%s\n", argv[1]);              </span>
  <span style='background-color:#85C1F5'>  }</span><span style='background-color:#4A789C'> else </span><span style='background-color:#F6D55D'>{                                </span>   <span class='c1'>// Code Region from 5:10 to 7:4</span>
  <span style='background-color:#F6D55D'>    printf("\n");                         </span>
  <span style='background-color:#F6D55D'>  }</span><span style='background-color:#4A789C'>                                         </span>
  <span style='background-color:#4A789C'>  return 0;                                 </span>
  <span style='background-color:#4A789C'>}</span>
  </pre>`
* Skipped regions are used to represent source ranges that were skipped
  by Clang's preprocessor. They don't associate with
  `coverage mapping counters`_, as the frontend knows that they are never
  executed. They are used by the code coverage tool to mark the skipped lines
  inside a function as non-code lines that don't have execution counts.
  For example:

  :raw-html:`<pre class='highlight' style='line-height:initial;'><span>int main() </span><span style='background-color:#4A789C'>{               </span> <span class='c1'>// Code Region from 1:12 to 6:2</span>
  <span style='background-color:#85C1F5'>#ifdef DEBUG             </span>   <span class='c1'>// Skipped Region from 2:1 to 4:2</span>
  <span style='background-color:#85C1F5'>  printf("Hello world"); </span>
  <span style='background-color:#85C1F5'>#</span><span style='background-color:#4A789C'>endif                     </span>
  <span style='background-color:#4A789C'>  return 0;                </span>
  <span style='background-color:#4A789C'>}</span>
  </pre>`
* Expansion regions are used to represent Clang's macro expansions. They
  have an additional property - *expanded file id*. This property can be
  used by the code coverage tool to find the mapping regions that are created
  as a result of this macro expansion, by checking if their file id matches the
  expanded file id. They don't associate with `coverage mapping counters`_,
  as the code coverage tool can determine the execution count for this region
  by looking up the execution count of the first region with a corresponding
  file id.
  For example:

  :raw-html:`<pre class='highlight' style='line-height:initial;'><span>int func(int x) </span><span style='background-color:#4A789C'>{                             </span>
  <span style='background-color:#4A789C'>  #define MAX(x,y) </span><span style='background-color:#85C1F5'>((x) &gt; (y)? </span><span style='background-color:#F6D55D'>(x)</span><span style='background-color:#85C1F5'> : </span><span style='background-color:#F4BA70'>(y)</span><span style='background-color:#85C1F5'>)</span><span style='background-color:#4A789C'>     </span>
  <span style='background-color:#4A789C'>  return </span><span style='background-color:#7FCA9F'>MAX</span><span style='background-color:#4A789C'>(x, 42);                          </span> <span class='c1'>// Expansion Region from 3:10 to 3:13</span>
  <span style='background-color:#4A789C'>}</span>
  </pre>`
* Branch regions associate instrumentable branch conditions in the source code
  with a `coverage mapping counter`_ to track how many times an individual
  condition evaluated to 'true' and another `coverage mapping counter`_ to
  track how many times that condition evaluated to false.  Instrumentable
  branch conditions may comprise larger boolean expressions using boolean
  logical operators.  The 'true' and 'false' cases reflect unique branch paths
  that can be traced back to the source code.
  For example:

  :raw-html:`<pre class='highlight' style='line-height:initial;'><span>int func(int x, int y) {
  <span>  if (<span style='background-color:#4A789C'>(x &gt; 1)</span> || <span style='background-color:#4A789C'>(y &gt; 3)</span>) {</span>  <span class='c1'>// Branch Region from 3:6 to 3:12</span>
  <span>                             </span><span class='c1'>// Branch Region from 3:17 to 3:23</span>
  <span>    printf("%d\n", x);              </span>
  <span>  } else {                                </span>
  <span>    printf("\n");                         </span>
  <span>  }</span>
  <span>  return 0;                                 </span>
  <span>}</span>
  </pre>`

* Decision regions associate multiple branch regions with a boolean
  expression in the source code.  This information also includes the number of
  bitmap bits needed to represent the expression's executed test vectors as
  well as the total number of instrumentable branch conditions that comprise
  the expression.  Decision regions are used to visualize Modified
  Condition/Decision Coverage (MC/DC) in *llvm-cov* for each boolean
  expression.  When decision regions are used, control flow IDs are assigned to
  each associated branch region. One ID represents the current branch
  condition, and two additional IDs represent the next branch condition in the
  control flow given a true or false evaluation, respectively.  This allows
  *llvm-cov* to reconstruct the control flow around the conditions in order to
  comprehend the full list of potential executable test vectors.

.. _source code range:

Source Range:
^^^^^^^^^^^^^

The source range record contains the starting and ending location of a certain
mapping region. Both locations include the line and the column numbers.

.. _coverage file id:

File ID:
^^^^^^^^

The file id an integer value that tells us
in which source file or macro expansion is this region located.
It enables Clang to produce mapping information for the code
defined inside macros, like this example demonstrates:

:raw-html:`<pre class='highlight' style='line-height:initial;'><span>void func(const char *str) </span><span style='background-color:#4A789C'>{        </span> <span class='c1'>// Code Region from 1:28 to 6:2 with file id 0</span>
<span style='background-color:#4A789C'>  #define PUT </span><span style='background-color:#85C1F5'>printf("%s\n", str)</span><span style='background-color:#4A789C'>   </span> <span class='c1'>// 2 Code Regions from 2:15 to 2:34 with file ids 1 and 2</span>
<span style='background-color:#4A789C'>  if(*str)                          </span>
<span style='background-color:#4A789C'>    </span><span style='background-color:#F6D55D'>PUT</span><span style='background-color:#4A789C'>;                            </span> <span class='c1'>// Expansion Region from 4:5 to 4:8 with file id 0 that expands a macro with file id 1</span>
<span style='background-color:#4A789C'>  </span><span style='background-color:#F6D55D'>PUT</span><span style='background-color:#4A789C'>;                              </span> <span class='c1'>// Expansion Region from 5:3 to 5:6 with file id 0 that expands a macro with file id 2</span>
<span style='background-color:#4A789C'>}</span>
</pre>`

.. _coverage mapping counter:
.. _coverage mapping counters:

Counter:
^^^^^^^^

A coverage mapping counter can represent a reference to the profile
instrumentation counter. The execution count for a region with such counter
is determined by looking up the value of the corresponding profile
instrumentation counter.

It can also represent a binary arithmetical expression that operates on
coverage mapping counters or other expressions.
The execution count for a region with an expression counter is determined by
evaluating the expression's arguments and then adding them together or
subtracting them from one another.
In the example below, a subtraction expression is used to compute the execution
count for the compound statement that follows the *else* keyword:

:raw-html:`<pre class='highlight' style='line-height:initial;'><span>int main(int argc, const char *argv[]) </span><span style='background-color:#4A789C'>{   </span> <span class='c1'>// Region's counter is a reference to the profile counter #0</span>
<span style='background-color:#4A789C'>                                           </span>
<span style='background-color:#4A789C'>  if (argc &gt; 1) </span><span style='background-color:#85C1F5'>{                        </span>   <span class='c1'>// Region's counter is a reference to the profile counter #1</span>
<span style='background-color:#85C1F5'>    printf("%s\n", argv[1]);             </span><span>   </span>
<span style='background-color:#85C1F5'>  }</span><span style='background-color:#4A789C'> else </span><span style='background-color:#F6D55D'>{                               </span>   <span class='c1'>// Region's counter is an expression (reference to the profile counter #0 - reference to the profile counter #1)</span>
<span style='background-color:#F6D55D'>    printf("\n");                        </span>
<span style='background-color:#F6D55D'>  }</span><span style='background-color:#4A789C'>                                        </span>
<span style='background-color:#4A789C'>  return 0;                                </span>
<span style='background-color:#4A789C'>}</span>
</pre>`

Finally, a coverage mapping counter can also represent an execution count of
of zero. The zero counter is used to provide coverage mapping for
unreachable statements and expressions, like in the example below:

:raw-html:`<pre class='highlight' style='line-height:initial;'><span>int main() </span><span style='background-color:#4A789C'>{                  </span>
<span style='background-color:#4A789C'>  return 0;                   </span>
<span style='background-color:#4A789C'>  </span><span style='background-color:#85C1F5'>printf("Hello world!\n")</span><span style='background-color:#4A789C'>;   </span> <span class='c1'>// Unreachable region's counter is zero</span>
<span style='background-color:#4A789C'>}</span>
</pre>`

The zero counters allow the code coverage tool to display proper line execution
counts for the unreachable lines and highlight the unreachable code.
Without them, the tool would think that those lines and regions were still
executed, as it doesn't possess the frontend's knowledge.

Note that branch regions are created to track branch conditions in the source
code and refer to two coverage mapping counters, one to track the number of
times the branch condition evaluated to "true", and one to track the number of
times the branch condition evaluated to "false".

LLVM IR Representation
======================

The coverage mapping data is stored in the LLVM IR using a global constant
structure variable called *__llvm_coverage_mapping* with the *IPSK_covmap*
section specifier (i.e. ".lcovmap$M" on Windows and "__llvm_covmap" elsewhere).

For example, let’s consider a C file and how it gets compiled to LLVM:

.. _coverage mapping sample:

.. code-block:: c

  int foo() {
    return 42;
  }
  int bar() {
    return 13;
  }

The coverage mapping variable generated by Clang has 2 fields:

* Coverage mapping header.

* An optionally compressed list of filenames present in the translation unit.

The variable has 8-byte alignment because ld64 cannot always pack symbols from
different object files tightly (the word-level alignment assumption is baked in
too deeply).

.. code-block:: llvm

  @__llvm_coverage_mapping = internal constant { { i32, i32, i32, i32 }, [32 x i8] }
  {
    { i32, i32, i32, i32 } ; Coverage map header
    {
      i32 0,  ; Always 0. In prior versions, the number of affixed function records
      i32 32, ; The length of the string that contains the encoded translation unit filenames
      i32 0,  ; Always 0. In prior versions, the length of the affixed string that contains the encoded coverage mapping data
      i32 3,  ; Coverage mapping format version
    },
   [32 x i8] c"..." ; Encoded data (dissected later)
  }, section "__llvm_covmap", align 8

The current version of the format is version 6.

There is one difference between versions 6 and 5:

* The first entry in the filename list is the compilation directory. When the
  filename is relative, the compilation directory is combined with the relative
  path to get an absolute path. This can reduce size by omitting the duplicate
  prefix in filenames.

There is one difference between versions 5 and 4:

* The notion of branch region has been introduced along with a corresponding
  region kind.  Branch regions encode two counters, one to track how many
  times a "true" branch condition is taken, and one to track how many times a
  "false" branch condition is taken.

There are two differences between versions 4 and 3:

* Function records are now named symbols, and are marked *linkonce_odr*. This
  allows linkers to merge duplicate function records. Merging of duplicate
  *dummy* records (emitted for functions included-but-not-used in a translation
  unit) reduces size bloat in the coverage mapping data. As part of this
  change, region mapping information for a function is now included within the
  function record, instead of being affixed to the coverage header.

* The filename list for a translation unit may optionally be zlib-compressed.

The only difference between versions 3 and 2 is that a special encoding for
column end locations was introduced to indicate gap regions.

In version 1, the function record for *foo* was defined as follows:

.. code-block:: llvm

     { i8*, i32, i32, i64 } { i8* getelementptr inbounds ([3 x i8]* @__profn_foo, i32 0, i32 0), ; Function's name
       i32 3, ; Function's name length
       i32 9, ; Function's encoded coverage mapping data string length
       i64 0  ; Function's structural hash
     }

In version 2, the function record for *foo* was defined as follows:

.. code-block:: llvm

     { i64, i32, i64 } {
       i64 0x5cf8c24cdb18bdac, ; Function's name MD5
       i32 9, ; Function's encoded coverage mapping data string length
       i64 0  ; Function's structural hash

Coverage Mapping Header:
------------------------

As shown above, the coverage mapping header has the following fields:

* The number of function records affixed to the coverage header. Always 0, but present for backwards compatibility.

* The length of the string in the third field of *__llvm_coverage_mapping* that contains the encoded translation unit filenames.

* The length of the string in the third field of *__llvm_coverage_mapping* that contains any encoded coverage mapping data affixed to the coverage header. Always 0, but present for backwards compatibility.

* The format version. The current version is 6 (encoded as a 5).

.. _function records:

Function record:
----------------

A function record is a structure of the following type:

.. code-block:: llvm

  { i64, i32, i64, i64, [? x i8] }

It contains the function name's MD5, the length of the encoded mapping data for
that function, the function's structural hash value, the hash of the filenames
in the function's translation unit, and the encoded mapping data.

Dissecting the sample:
^^^^^^^^^^^^^^^^^^^^^^

Here's an overview of the encoded data that was stored in the
IR for the `coverage mapping sample`_ that was shown earlier:

* The IR contains the following string constant that represents the encoded
  coverage mapping data for the sample translation unit:

  .. code-block:: llvm

    c"\01\15\1Dx\DA\13\D1\0F-N-*\D6/+\CE\D6/\C9-\D0O\CB\CF\D7K\06\00N+\07]"

* The string contains values that are encoded in the LEB128 format, which is
  used throughout for storing integers. It also contains a compressed payload.

* The first three LEB128-encoded numbers in the sample specify the number of
  filenames, the length of the uncompressed filenames, and the length of the
  compressed payload (or 0 if compression is disabled). In this sample, there
  is 1 filename that is 21 bytes in length (uncompressed), and stored in 29
  bytes (compressed).

* The coverage mapping from the first function record is encoded in this string:

  .. code-block:: llvm

    c"\01\00\00\01\01\01\0C\02\02"

  This string consists of the following bytes:

  +----------+-------------------------------------------------------------------------------------------------------------------------+
  | ``0x01`` | The number of file ids used by this function. There is only one file id used by the mapping data in this function.      |
  +----------+-------------------------------------------------------------------------------------------------------------------------+
  | ``0x00`` | An index into the filenames array which corresponds to the file "/Users/alex/test.c".                                   |
  +----------+-------------------------------------------------------------------------------------------------------------------------+
  | ``0x00`` | The number of counter expressions used by this function. This function doesn't use any expressions.                     |
  +----------+-------------------------------------------------------------------------------------------------------------------------+
  | ``0x01`` | The number of mapping regions that are stored in an array for the function's file id #0.                                |
  +----------+-------------------------------------------------------------------------------------------------------------------------+
  | ``0x01`` | The coverage mapping counter for the first region in this function. The value of 1 tells us that it's a coverage        |
  |          | mapping counter that is a reference to the profile instrumentation counter with an index of 0.                          |
  +----------+-------------------------------------------------------------------------------------------------------------------------+
  | ``0x01`` | The starting line of the first mapping region in this function.                                                         |
  +----------+-------------------------------------------------------------------------------------------------------------------------+
  | ``0x0C`` | The starting column of the first mapping region in this function.                                                       |
  +----------+-------------------------------------------------------------------------------------------------------------------------+
  | ``0x02`` | The ending line of the first mapping region in this function.                                                           |
  +----------+-------------------------------------------------------------------------------------------------------------------------+
  | ``0x02`` | The ending column of the first mapping region in this function.                                                         |
  +----------+-------------------------------------------------------------------------------------------------------------------------+

* The length of the substring that contains the encoded coverage mapping data
  for the second function record is also 9. It's structured like the mapping data
  for the first function record.

* The two trailing bytes are zeroes and are used to pad the coverage mapping
  data to give it the 8 byte alignment.

Encoding
========

The per-function coverage mapping data is encoded as a stream of bytes,
with a simple structure. The structure consists of the encoding
`types <cvmtypes_>`_ like variable-length unsigned integers, that
are used to encode `File ID Mapping`_, `Counter Expressions`_ and
the `Mapping Regions`_.

The format of the structure follows:

  ``[file id mapping, counter expressions, mapping regions]``

The translation unit filenames are encoded using the same encoding
`types <cvmtypes_>`_ as the per-function coverage mapping data, with the
following structure:

  ``[numFilenames : LEB128, filename0 : string, filename1 : string, ...]``

.. _cvmtypes:

Types
-----

This section describes the basic types that are used by the encoding format
and can appear after ``:`` in the ``[foo : type]`` description.

.. _LEB128:

LEB128
^^^^^^

LEB128 is an unsigned integer value that is encoded using DWARF's LEB128
encoding, optimizing for the case where values are small
(1 byte for values less than 128).

.. _CoverageStrings:

Strings
^^^^^^^

``[length : LEB128, characters...]``

String values are encoded with a `LEB value <LEB128_>`_ for the length
of the string and a sequence of bytes for its characters.

.. _file id mapping:

File ID Mapping
---------------

``[numIndices : LEB128, filenameIndex0 : LEB128, filenameIndex1 : LEB128, ...]``

File id mapping in a function's coverage mapping stream
contains the indices into the translation unit's filenames array.

Counter
-------

``[value : LEB128]``

A `coverage mapping counter`_ is stored in a single `LEB value <LEB128_>`_.
It is composed of two things --- the `tag <counter-tag_>`_
which is stored in the lowest 2 bits, and the `counter data`_ which is stored
in the remaining bits.

.. _counter-tag:

Tag:
^^^^

The counter's tag encodes the counter's kind
and, if the counter is an expression, the expression's kind.
The possible tag values are:

* 0 - The counter is zero.

* 1 - The counter is a reference to the profile instrumentation counter.

* 2 - The counter is a subtraction expression.

* 3 - The counter is an addition expression.

.. _counter data:

Data:
^^^^^

The counter's data is interpreted in the following manner:

* When the counter is a reference to the profile instrumentation counter,
  then the counter's data is the id of the profile counter.
* When the counter is an expression, then the counter's data
  is the index into the array of counter expressions.

.. _Counter Expressions:

Counter Expressions
-------------------

``[numExpressions : LEB128, expr0LHS : LEB128, expr0RHS : LEB128, expr1LHS : LEB128, expr1RHS : LEB128, ...]``

Counter expressions consist of two counters as they
represent binary arithmetic operations.
The expression's kind is determined from the `tag <counter-tag_>`_ of the
counter that references this expression.

.. _Mapping Regions:

Mapping Regions
---------------

``[numRegionArrays : LEB128, regionsForFile0, regionsForFile1, ...]``

The mapping regions are stored in an array of sub-arrays where every
region in a particular sub-array has the same file id.

The file id for a sub-array of regions is the index of that
sub-array in the main array e.g. The first sub-array will have the file id
of 0.

Sub-Array of Regions
^^^^^^^^^^^^^^^^^^^^

``[numRegions : LEB128, region0, region1, ...]``

The mapping regions for a specific file id are stored in an array that is
sorted in an ascending order by the region's starting location.

Mapping Region
^^^^^^^^^^^^^^

``[header, source range]``

The mapping region record contains two sub-records ---
the `header`_, which stores the counter and/or the region's kind,
and the `source range`_ that contains the starting and ending
location of this region.

.. _header:

Header
^^^^^^

``[counter]``

or

``[pseudo-counter]``

The header encodes the region's counter and the region's kind. A branch region
will encode two counters.

The value of the counter's tag distinguishes between the counters and
pseudo-counters --- if the tag is zero, than this header contains a
pseudo-counter, otherwise this header contains an ordinary counter.

Counter:
""""""""

A mapping region whose header has a counter with a non-zero tag is
a code region.

Pseudo-Counter:
"""""""""""""""

``[value : LEB128]``

A pseudo-counter is stored in a single `LEB value <LEB128_>`_, just like
the ordinary counter. It has the following interpretation:

* bits 0-1: tag, which is always 0.

* bit 2: expansionRegionTag. If this bit is set, then this mapping region
  is an expansion region.

* remaining bits: data. If this region is an expansion region, then the data
  contains the expanded file id of that region.

  Otherwise, the data contains the region's kind. The possible region
  kind values are:

  * 0 - This mapping region is a code region with a counter of zero.
  * 2 - This mapping region is a skipped region.
  * 4 - This mapping region is a branch region.

.. _source range:

Source Range
^^^^^^^^^^^^

``[deltaLineStart : LEB128, columnStart : LEB128, numLines : LEB128, columnEnd : LEB128]``

The source range record contains the following fields:

* *deltaLineStart*: The difference between the starting line of the
  current mapping region and the starting line of the previous mapping region.

  If the current mapping region is the first region in the current
  sub-array, then it stores the starting line of that region.

* *columnStart*: The starting column of the mapping region.

* *numLines*: The difference between the ending line and the starting line
  of the current mapping region.

* *columnEnd*: The ending column of the mapping region. If the high bit is set,
  the current mapping region is a gap area. A count for a gap area is only used
  as the line execution count if there are no other regions on a line.

Testing Format
==============

.. warning::
  This section is for the LLVM developers who are working on ``llvm-cov`` only.

``llvm-cov`` uses a special file format (called ``.covmapping`` below) for
testing purposes. This format is private and should have no use for general
users. As a developer, you can get such files by the ``convert-for-testing``
subcommand of ``llvm-cov``.

The structure of the ``.covmapping`` files follows:

``[magicNumber : u64, version : u64, profileNames, coverageMapping, coverageRecords]``

Magic Number and Version
------------------------

The magic is ``0x6d766f636d766c6c``, which is the ASCII string
``llvmcovm`` in little-endian.

There are two versions for now:

- Version1, encoded as ``0x6174616474736574`` (ASCII string ``testdata``).
- Version2, encoded as 1.

The only difference between Version1 and Version2 is in the encoding of the
``coverageMapping`` fields, which is explained later.

Profile Names
-------------

``profileNames``, ``coverageMapping`` and ``coverageRecords`` are 3 sections
extracted from the original binary file.

``profileNames`` encodes the size, address and the raw data of the section:

``[profileNamesSize : LEB128, profileNamesAddr : LEB128, profileNamesData : bytes]``

Coverage Mapping
----------------

This field is padded with zero bytes to make it 8-byte aligned.

``coverageMapping`` contains the records of the source files. In version 1,
only one record is stored:

``[padding : bytes, coverageMappingData : bytes]``

Version 2 relaxes this restriction by encoding the size of
``coverageMappingData`` as a LEB128 number before the data:

``[coverageMappingSize : LEB128, padding : bytes, coverageMappingData : bytes]``

The current version is 2.

Coverage Records
----------------

This field is padded with zero bytes to make it 8-byte aligned.

``coverageRecords`` is encoded as:

``[padding : bytes, coverageRecordsData : bytes]``

The rest data in the file is considered as the ``coverageRecordsData``.
