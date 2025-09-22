# `llvm-debuginfo-analyzer`

These are the notes collected during the development, review and test.
They describe limitations, known issues and future work.

### Remove the use of macros in ``LVReader.h`` that describe the ``bumpallocators``.
**[D137933](https://reviews.llvm.org/D137933#inline-1389904)**

Use a standard (or LLVM) ``map`` with ``typeinfo`` (would need a specialization
to expose equality and hasher) for the allocators and the creation
functions could be a function template.

### Use a **lit test** instead of a **unit test** for the **logical readers**.
**[D125783](https://reviews.llvm.org/D125783#inline-1324376)**

As the ``DebugInfoLogicalView`` library is sufficiently exposed via the
``llvm-debuginfo-analyzer`` tool, follow the LLVM general approach and
use ``lit`` tests to validate the **logical readers**.

Convert the ``unitests``:
```
llvm-project/llvm/unittests/DebugInfo/LogicalView/CodeViewReaderTest.cpp
llvm-project/llvm/unittests/DebugInfo/LogicalView/DWARFReaderTest.cpp
```
into ``lit`` tests:
```
llvm-project/llvm/test/DebugInfo/LogicalView/CodeViewReader.test
llvm-project/llvm/test/DebugInfo/LogicalView/DWARFReader.test
```

### Eliminate calls to ``getInputFileDirectory()`` in the ``unittests``.
**[D125783](https://reviews.llvm.org/D125783#inline-1324359)**

Rewrite the unittests ``ReaderTest`` and ``CodeViewReaderTest`` to eliminate
the call:
```
  getInputFileDirectory()
```
as use of that call is discouraged.

### Fix mismatch between ``%d/%x`` format strings and ``uint64_t`` type.
**[D137400](https://reviews.llvm.org/D137400) / [58758](https://github.com/llvm/llvm-project/issues/58758)**

Incorrect printing of ``uint64_t`` on ``32-bit`` platforms.
Add the ``PRIx64`` specifier to the printing code (``format()``).

### Remove ``LVScope::Children`` container.
**[D137933](https://reviews.llvm.org/D137933#inline-1373902)**

Use a **chaining iterator** over the other containers rather than keep a
separate container ``Children`` that mirrors their contents.

### Use ``TableGen`` for command line options.
**[D125777](https://reviews.llvm.org/D125777#inline-1291801)**

The current trend is to use ``TableGen`` for command-line options in tools.
Change command line options to use ``tablegen`` as many other LLVM tools.

### ``LVDoubleMap`` to return ``optional<ValueType>`` instead of ``null pointer``.
**[D125783](https://reviews.llvm.org/D125783#inline-1294164)**

The more idiomatic LLVM way to handle this would be to have ``find``
return ``Optional<ValueType>``.

### Pass references instead of pointers (**Comparison functions**).
**[D125782](https://reviews.llvm.org/D125782#inline-1293920)**

In the **comparison functions**, pass references instead of pointers (when
pointers cannot be null).

### Use ``StringMap`` where possible.
**[D125783](https://reviews.llvm.org/D125783#inline-1294211)**

LLVM has a ``StringMap`` class that is advertised as more efficient than
``std::map<std::string, ValueType>``. Mainly it does fewer allocations
because the key is not a ``std::string``.

Replace the use of ``std::map<std::string, ValueType>`` with ``StringMap``.
One specific case is the ``LVSymbolNames`` definitions.

### Calculate unique offset for CodeView elements.
In order to have the same logical functionality as the DWARF reader, such
as:

* find scopes contribution to debug info
* sort by its physical location

The logical elements must have an unique offset (similar like the DWARF
``DIE`` offset).

### Move ``initializeFileAndStringTables`` to the CodeView Library.
There is some code in the CodeView reader that was extracted/adapted
from ``tools/llvm-readobj/COFFDumper.cpp`` that can be moved to the CodeView
library.

We had a similar case with code shared with ``llvm-pdbutil`` that was moved
to the PDB library: **[D122226](https://reviews.llvm.org/D122226)**

### Move ``getSymbolKindName`` and ``formatRegisterId`` to the CodeView Library.
There is some code in the CodeView reader that was extracted/adapted
from ``lib/DebugInfo/CodeView/SymbolDumper.cpp`` that can be used.

### Use of ``std::unordered_set`` instead of ``std::set``.
**[D125784](https://reviews.llvm.org/D125784#inline-1221421)**

Replace the ``std::set`` usage for ``DeducedScopes``, ``UnresolvedScopes`` and
``IdentifiedNamespaces`` with ``std::unordered_set`` and get the benefit
of the O(1) while inserting/searching, as the order is not important.

### Optimize ``LVNamespaceDeduction::find`` funtion.
**[D125784](https://reviews.llvm.org/D125784#inline-1296195)**

Optimize the ``find`` method to use the proposed code:

```
  LVStringRefs::iterator Iter = std::find_if(Components.begin(), Components.end(),
    [](StringRef Name) {
        return IdentifiedNamespaces.find(Name) == IdentifiedNamespaces.end();
    });
  LVStringRefs::size_type FirstNonNamespace = std::distance(Components.begin(), Iter);
```

### Move all the printing support to a common module.
Factor out printing functionality from the logical elements into a
common module.

### Refactor ``LVBinaryReader::processLines``.
**[D125783](https://reviews.llvm.org/D125783#inline-1246155) /
[D137156](https://reviews.llvm.org/D137156)**

During the traversal of the debug information sections, we created the
logical lines representing the **disassembled instructions** from the **text
section** and the logical lines representing the **line records** from the
**debug line** section. Using the ranges associated with the logical scopes,
we will allocate those logical lines to their logical scopes.

Consider the case when any of those lines become orphans, causing
incorrect scope parent for disassembly or line records.

### Add support for ``-ffunction-sections``.
**[D125783](https://reviews.llvm.org/D125783#inline-1295012)**

Only linked executables are handled. It does not support relocatable
files compiled with ``-ffunction-sections``.

### Add support for DWARF v5 `.debug_names` section / CodeView public symbols stream.
**[D125783](https://reviews.llvm.org/D125783#inline-1294142)**

The DWARF and CodeView readers use the public names information to create
the instructions (``LVLineAssembler``). Instead of relying on DWARF section
names (``.debug_pubnames``, ``.debug_names``) and CodeView public symbol stream
(``S_PUB32``), the readers should collect the needed information while processing
the debug information.

If the object file supports the above section names and stream, use them
to create the public names.

### Add support for some extra DWARF locations.
The following DWARF debug location operands are not supported:

* `DW_OP_const_type`
* `DW_OP_entry_value`
* `DW_OP_implicit_value`

### Add support for additional binary formats.
* Extended COFF (`XCOFF`)

### Add support for ``JSON`` or ``YAML``
The logical view uses its own and non-standard free form text when
displaying information on logical elements.
