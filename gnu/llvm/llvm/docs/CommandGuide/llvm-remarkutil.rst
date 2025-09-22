llvm-remarkutil - Remark utility
================================

.. program:: llvm-remarkutil

Synopsis
--------

:program:`llvm-remarkutil` [*subcommmand*] [*options*]

Description
-----------

Utility for displaying information from, and converting between different
`remark <https://llvm.org/docs/Remarks.html>`_ formats.

Subcommands
-----------

  * :ref:`bitstream2yaml_subcommand` - Reserialize bitstream remarks to YAML.
  * :ref:`yaml2bitstream_subcommand` - Reserialize YAML remarks to bitstream.
  * :ref:`instruction-count_subcommand` - Output function instruction counts.
  * :ref:`annotation-count_subcommand` - Output remark type count from annotation remarks.
  * :ref:`size-diff_subcommand` - Compute diff in size remarks.

.. _bitstream2yaml_subcommand:

bitstream2yaml
~~~~~~~~~~~~~~

.. program:: llvm-remarkutil bitstream2yaml

USAGE: :program:`llvm-remarkutil` bitstream2yaml <input file> -o <output file>

Summary
^^^^^^^

Takes a bitstream remark file as input, and reserializes that file as YAML.

.. _yaml2bitstream_subcommand:

yaml2bitstream
~~~~~~~~~~~~~~

.. program:: llvm-remarkutil yaml2bitstream

USAGE: :program:`llvm-remarkutil` yaml2bitstream <input file> -o <output file>

Summary
^^^^^^^

Takes a YAML remark file as input, and reserializes that file in the bitstream
format.

.. _instruction-count_subcommand:

instruction-count
~~~~~~~~~~~~~~~~~

.. program:: llvm-remarkutil instruction-count

USAGE: :program:`llvm-remarkutil` instruction-count <input file> --parser=<bitstream|yaml> [--use-debug-loc] -o <output file>

Summary
^^^^^^^

Outputs instruction count remarks for every function. Instruction count remarks
encode the number of instructions in a function at assembly printing time.

Instruction count remarks require asm-printer remarks.

CSV format is as follows:

::

  Function,InstructionCount
  foo,123

if `--use-debug-loc` is passed then the CSV will include the source path, line number and column.

::

  Source,Function,InstructionCount
  path:line:column,foo,3

.. _annotation-count_subcommand:

annotation-count
~~~~~~~~~~~~~~~~~

.. program:: llvm-remarkutil annotation-count

USAGE: :program:`llvm-remarkutil` annotation-count <input file> --parser=<bitstream|yaml> --annotation-type=<type>  [--use-debug-loc] -o <output file>

Summary
^^^^^^^

Outputs a count for annotation-type `<type>` remark for every function. The count expresses
the number of remark checks inserted at the function.

Annotation count remarks require AnnotationRemarksPass remarks.

CSV format is as follows:

::

  Function,Count
  foo,123

if `--use-debug-loc` is passed then the CSV will include the source path, line number and column.

::
  
  Source,Function,Count
  path:line:column,foo,3

.. _count_subcommand:

count
~~~~~

.. program:: llvm-remarkutil count

USAGE: :program:`llvm-remarkutil` count [*options*] <input file>

Summary
^^^^^^^

:program:`llvm-remarkutil count` counts `remarks <https://llvm.org/docs/Remarks.html>`_ based on specified properties.
By default the tool counts remarks based on how many occur in a source file or function or total for the generated remark file.
The tool also supports collecting count based on specific remark arguments. The specified arguments should have an integer value to be able to report a count.

The tool contains utilities to filter the remark count based on remark name, pass name, argument value and remark type.

Options
^^^^^^^

.. option:: --parser=<yaml|bitstream>

  Select the type of input remark parser. Required.

  * ``yaml`` : The tool will parse YAML remarks.
  * ``bitstream`` : The tool will parse bitstream remarks.

.. option:: --count-by=<value>

  Select option to collect remarks by.

  * ``remark-name`` : count how many individual remarks exist.
  * ``arg`` : count remarks based on specified arguments passed by --(r)args. The argument value must be a number.

.. option:: --group-by=<value>

  group count of remarks by property.

  * ``source`` : Count will be collected per source path. Remarks with no debug location will not be counted.
  * ``function`` : Count is collected per function.
  * ``function-with-loc`` : Count is collected per function per source. Remarks with no debug location will not be counted.
  * ``Total`` : Report a count for the provided remark file.

.. option:: --args[=arguments]

  If `count-by` is set to `arg` this flag can be used to collect from specified remark arguments represented as a comma separated string.
  The arguments must have a numeral value to be able to count remarks by

.. option:: --rargs[=arguments]

  If `count-by` is set to `arg` this flag can be used to collect from specified remark arguments using regular expression.
  The arguments must have a numeral value to be able to count remarks by

.. option:: --pass-name[=<string>]

  Filter count by pass name.

.. option:: --rpass-name[=<string>]

  Filter count by pass name using regular expressions.

.. option:: --remark-name[=<string>]

  Filter count by remark name.

.. option:: --rremark-name[=<string>]

  Filter count by remark name using regular expressions.

.. option:: --filter-arg-by[=<string>]

  Filter count by argument value.

.. option:: --rfilter-arg-by[=<string>]

  Filter count by argument value using regular expressions.

.. option:: --remark-type=<value>

  Filter remarks by type with the following options.

  * ``unknown``
  * ``passed``
  * ``missed``
  * ``analysis``
  * ``analysis-fp-commute``
  * ``analysis-aliasing``
  * ``failure``

.. _size-diff_subcommand:

size-diff
~~~~~~~~~
.. program:: llvm-remarkutil size-diff

USAGE: :program:`llvm-remarkutil` size-diff [*options*] *file_a* *file_b* **--parser** *parser*

Summary
^^^^^^^

:program:`llvm-remarkutil size-diff` diffs size `remarks <https://llvm.org/docs/Remarks.html>`_ in two remark files: ``file_a``
and ``file_b``.

:program:`llvm-remarkutil size-diff` can be used to gain insight into which
functions were impacted the most by code generation changes.

In most common use-cases ``file_a`` and ``file_b`` will be remarks output by
compiling a **fixed source** with **differing compilers** or
**differing optimization settings**.

:program:`llvm-remarkutil size-diff` handles both
`YAML <https://llvm.org/docs/Remarks.html#yaml-remarks>`_ and
`bitstream <https://llvm.org/docs/Remarks.html#llvm-bitstream-remarks>`_
remarks.

Options
^^^^^^^

.. option:: --parser=<yaml|bitstream>

Select the type of input remark parser. Required.

* ``yaml`` : The tool will parse YAML remarks.
* ``bitstream`` : The tool will parse bitstream remarks.

.. option:: --report-style=<human|json>

  Output style.

  * ``human`` : Human-readable textual report. Default option.
  * ``json`` : JSON report.

.. option:: --pretty

  Pretty-print JSON output. Optional.

  If output is not set to JSON, this does nothing.

.. option:: -o=<file>

  Output file for the report. Outputs to stdout by default.

Human-Readable Output
^^^^^^^^^^^^^^^^^^^^^

The human-readable format for :program:`llvm-remarkutil size-diff` is composed of
two sections:

* Per-function changes.
* A high-level summary of all changes.

Changed Function Section
^^^^^^^^^^^^^^^^^^^^^^^^

Suppose you are comparing two remark files OLD and NEW.

For each function with a **changed instruction count** in OLD and NEW,
:program:`llvm-remarkutil size-diff` will emit a line like below:

::

  (++|--|==) (>|<) function_name, N instrs, M stack B

A breakdown of the format is below:

``(++|--|==)``
  Which of OLD and NEW the ``function_name`` is present in.

  * ``++``: Only in NEW. ("Added")
  * ``--``: Only in OLD. ("Removed")
  * ``==``: In both.

``(>|<)``
  Denotes if ``function_name`` has more instructions or fewer instructions in
  the second file.

  *  ``>``: More instructions in second file than first file.
  *  ``<``: Fewer instructions in second file than in first file.

``function_name``
  The name of the changed function.

``N instrs``
  Second file instruction count - first file instruction count.

``M stack B``
  Second file stack byte count - first file stack byte count.

Summary Section
^^^^^^^^^^^^^^^

:program:`llvm-remarkutil size-diff` will output a high-level summary after
printing all changed functions.

::

  instruction count: N (inst_pct_change%)
  stack byte usage: M (sb_pct_change%)

``N``
  Sum of all instruction count changes between the second and first file.

``inst_pct_change%``
  Percent increase or decrease in instruction count between the second and first
  file.

``M``
  Sum of all stack byte count changes between the second and first file.

``sb_pct_change%``
  Percent increase or decrease in stack byte usage between the second and first
  file.

JSON OUTPUT
^^^^^^^^^^^^

High-Level view
^^^^^^^^^^^^^^^

Suppose we are comparing two files, OLD and NEW.

:program:`llvm-remarkutil size-diff` will output JSON as follows.

::

  "Files": [
    "A": "path/to/OLD",
    "B": "path/to/NEW"
  ]

  "InBoth": [
    ...
  ],

  "OnlyInA": [
    ...
  ],

  "OnlyInB": [
    ...
  ]


``Files``
  Original paths to remark files.

  * ``A``: Path to the first file.
  * ``B``: Path to the second file.

``InBoth``
  Functions present in both files.

``OnlyInA``
  Functions only present in the first file.

``OnlyInB``
  Functions only present in the second file.

Function JSON
^^^^^^^^^^^^^

The ``InBoth``, ``OnlyInA``, and ``OnlyInB`` sections contain size information
for each function in the input remark files.

::

  {
    "FunctionName" : "function_name"
    "InstCount": [
        INST_COUNT_A,
        INST_COUNT_B
      ],
    "StackSize": [
        STACK_BYTES_A,
        STACK_BYTES_B
      ],
  }

``FunctionName``
  Name of the function.

``InstCount``
  Instruction counts for the function.

  * ``INST_COUNT_A``: Instruction count in OLD.
  * ``INST_COUNT_B``: Instruction count in NEW.

``StackSize``
  Stack byte counts for the function.

  * ``STACK_BYTES_A``: Stack bytes in OLD.
  *  ``STACK_BYTES_B``: Stack bytes in NEW.

Computing Diffs From Function JSON
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

Function JSON does not contain the diffs. Tools consuming JSON output from
:program:`llvm-remarkutil size-diff` are responsible for computing the diffs
separately.

**To compute the diffs:**

* Instruction count diff: ``INST_COUNT_B - INST_COUNT_A``
* Stack byte count diff: ``STACK_BYTES_B - STACK_BYTES_A``

EXIT STATUS
^^^^^^^^^^^

:program:`llvm-remarkutil size-diff` returns 0 on success, and a non-zero value
otherwise.
