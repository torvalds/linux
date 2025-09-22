WebAssembly lld port
====================

The WebAssembly version of lld takes WebAssembly binaries as inputs and produces
a WebAssembly binary as its output.  For the most part it tries to mimic the
behaviour of traditional ELF linkers and specifically the ELF lld port.  Where
possible the command line flags and the semantics should be the same.


Object file format
------------------

The WebAssembly object file format used by LLVM and LLD is specified as part of
the WebAssembly tool conventions on linking_.

This is the object format that the llvm will produce when run with the
``wasm32-unknown-unknown`` target.

Usage
-----

The WebAssembly version of lld is installed as **wasm-ld**.  It shared many
common linker flags with **ld.lld** but also includes several
WebAssembly-specific options:

.. option:: --no-entry

  Don't search for the entry point symbol (by default ``_start``).

.. option:: --export-table

  Export the function table to the environment.

.. option:: --import-table

  Import the function table from the environment.

.. option:: --export-all

  Export all symbols (normally combined with --no-gc-sections)

  Note that this will not export linker-generated mutable globals unless
  the resulting binaryen already includes the 'mutable-globals' features
  since that would otherwise create and invalid binaryen.

.. option:: --export-dynamic

  When building an executable, export any non-hidden symbols.  By default only
  the entry point and any symbols marked as exports (either via the command line
  or via the `export-name` source attribute) are exported.

.. option:: --global-base=<value>

  Address at which to place global data.

.. option:: --no-merge-data-segments

  Disable merging of data segments.

.. option:: --stack-first

  Place stack at start of linear memory rather than after data.

.. option:: --compress-relocations

  Relocation targets in the code section are 5-bytes wide in order to
  potentially accommodate the largest LEB128 value.  This option will cause the
  linker to shrink the code section to remove any padding from the final
  output.  However because it affects code offset, this option is not
  compatible with outputting debug information.

.. option:: --allow-undefined

  Allow undefined symbols in linked binary.  This is the legacy
  flag which corresponds to ``--unresolve-symbols=ignore`` +
  ``--import-undefined``.

.. option:: --allow-undefined-file=<filename>

  Like ``--allow-undefined``, but the filename specified a flat list of
  symbols, one per line, which are allowed to be undefined.

.. option:: --unresolved-symbols=<method>

  This is a more full featured version of ``--allow-undefined``.
  The semanatics of the different methods are as follows:

  report-all:

     Report all unresolved symbols.  This is the default.  Normally the linker
     will generate an error message for each reported unresolved symbol but the
     option ``--warn-unresolved-symbols`` can change this to a warning.

  ignore-all:

     Resolve all undefined symbols to zero.  For data and function addresses
     this is trivial.  For direct function calls, the linker will generate a
     trapping stub function in place of the undefined function.

  import-dynamic:

     Undefined symbols generate WebAssembly imports, including undefined data
     symbols.  This is somewhat similar to the --import-undefined option but
     works all symbol types.  This options puts limitations on the type of
     relocations that are allowed for imported data symbols.  Relocations that
     require absolute data addresses (i.e. All R_WASM_MEMORY_ADDR_I32) will
     generate an error if they cannot be resolved statically.  For clang/llvm
     this means inputs should be compiled with `-fPIC` (i.e. `pic` or
     `dynamic-no-pic` relocation models).  This options is useful for linking
     binaries that are themselves static (non-relocatable) but whose undefined
     symbols are resolved by a dynamic linker.  Since the dynamic linking API is
     experimental, this option currently requires `--experimental-pic` to also
     be specified.

.. option:: --import-memory

  Import memory from the environment.

.. option:: --import-undefined

   Generate WebAssembly imports for undefined symbols, where possible.  For
   example, for function symbols this is always possible, but in general this
   is not possible for undefined data symbols.  Undefined data symbols will
   still be reported as normal (in accordance with ``--unresolved-symbols``).

.. option:: --initial-heap=<value>

  Initial size of the heap. Default: zero.

.. option:: --initial-memory=<value>

  Initial size of the linear memory. Default: the sum of stack, static data and heap sizes.

.. option:: --max-memory=<value>

  Maximum size of the linear memory. Default: unlimited.

.. option:: --no-growable-memory

  Set maximum size of the linear memory to its initial size, disallowing memory growth.

By default the function table is neither imported nor exported, but defined
for internal use only.

Behaviour
---------

In general, where possible, the WebAssembly linker attempts to emulate the
behaviour of a traditional ELF linker, and in particular the ELF port of lld.
For more specific details on how this is achieved see the tool conventions on
linking_.

Function Signatures
~~~~~~~~~~~~~~~~~~~

One way in which the WebAssembly linker differs from traditional native linkers
is that function signature checking is strict in WebAssembly.  It is a
validation error for a module to contain a call site that doesn't agree with
the target signature.  Even though this is undefined behaviour in C/C++, it is not
uncommon to find this in real-world C/C++ programs.  For example, a call site in
one compilation unit which calls a function defined in another compilation
unit but with too many arguments.

In order not to generate such invalid modules, lld has two modes of handling such
mismatches: it can simply error-out or it can create stub functions that will
trap at runtime (functions that contain only an ``unreachable`` instruction)
and use these stub functions at the otherwise invalid call sites.

The default behaviour is to generate these stub function and to produce
a warning.  The ``--fatal-warnings`` flag can be used to disable this behaviour
and error out if mismatched are found.

Exports
~~~~~~~

When building a shared library any symbols marked as ``visibility=default`` will
be exported.

When building an executable, only the entry point (``_start``) and symbols with
the ``WASM_SYMBOL_EXPORTED`` flag are exported by default.  In LLVM the
``WASM_SYMBOL_EXPORTED`` flag is set by the ``wasm-export-name`` attribute which
in turn can be set using ``__attribute__((export_name))`` clang attribute.

In addition, symbols can be exported via the linker command line using
``--export`` (which will error if the symbol is not found) or
``--export-if-defined`` (which will not).

Finally, just like with native ELF linker the ``--export-dynamic`` flag can be
used to export symbols in the executable which are marked as
``visibility=default``.

Imports
~~~~~~~

By default no undefined symbols are allowed in the final binary.  The flag
``--allow-undefined`` results in a WebAssembly import being defined for each
undefined symbol.  It is then up to the runtime to provide such symbols.
``--allow-undefined-file`` is the same but allows a list of symbols to be
specified.

Alternatively symbols can be marked in the source code as with the
``import_name`` and/or ``import_module`` clang attributes which signals that
they are expected to be undefined at static link time.

Stub Libraries
~~~~~~~~~~~~~~

Another way to specify imports and exports is via a "stub library".  This
feature is inspired by the ELF stub objects which are supported by the Solaris
linker.  Stub libraries are text files that can be passed as normal linker
inputs, similar to how linker scripts can be passed to the ELF linker.  The stub
library is a stand-in for a set of symbols that will be available at runtime,
but doesn't contain any actual code or data.  Instead it contains just a list of
symbols, one per line.  Each symbol can specify zero or more dependencies.
These dependencies are symbols that must be defined, and exported, by the output
module if the symbol is question is imported/required by the output module.

For example, imagine the runtime provides an external symbol ``foo`` that
depends on the ``malloc`` and ``free``.  This can be expressed simply as::

  #STUB
  foo: malloc,free

Here we are saying that ``foo`` is allowed to be imported (undefined) but that
if it is imported, then the output module must also export ``malloc`` and
``free`` to the runtime.  If ``foo`` is imported (undefined), but the output
module does not define ``malloc`` and ``free`` then the link will fail.

Stub libraries must begin with ``#STUB`` on a line by itself.

Garbage Collection
~~~~~~~~~~~~~~~~~~

Since WebAssembly is designed with size in mind the linker defaults to
``--gc-sections`` which means that all unused functions and data segments will
be stripped from the binary.

The symbols which are preserved by default are:

- The entry point (by default ``_start``).
- Any symbol which is to be exported.
- Any symbol transitively referenced by the above.

Weak Undefined Functions
~~~~~~~~~~~~~~~~~~~~~~~~

On native platforms, calls to weak undefined functions end up as calls to the
null function pointer.  With WebAssembly, direct calls must reference a defined
function (with the correct signature).  In order to handle this case the linker
will generate function a stub containing only the ``unreachable`` instruction
and use this for any direct references to an undefined weak function.

For example a runtime call to a weak undefined function ``foo`` will up trapping
on ``unreachable`` inside and linker-generated function called
``undefined:foo``.

Missing features
----------------

- Merging of data section similar to ``SHF_MERGE`` in the ELF world is not
  supported.
- No support for creating shared libraries.  The spec for shared libraries in
  WebAssembly is still in flux:
  https://github.com/WebAssembly/tool-conventions/blob/main/DynamicLinking.md

.. _linking: https://github.com/WebAssembly/tool-conventions/blob/main/Linking.md
