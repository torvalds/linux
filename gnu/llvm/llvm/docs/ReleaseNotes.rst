============================
LLVM |release| Release Notes
============================

.. contents::
    :local:

.. only:: PreRelease

  .. warning::
     These are in-progress notes for the upcoming LLVM |version| release.
     Release notes for previous releases can be found on
     `the Download Page <https://releases.llvm.org/download.html>`_.


Introduction
============

This document contains the release notes for the LLVM Compiler Infrastructure,
release |release|.  Here we describe the status of LLVM, including major improvements
from the previous release, improvements in various subprojects of LLVM, and
some of the current users of the code.  All LLVM releases may be downloaded
from the `LLVM releases web site <https://llvm.org/releases/>`_.

For more information about LLVM, including information about the latest
release, please check out the `main LLVM web site <https://llvm.org/>`_.  If you
have questions or comments, the `Discourse forums
<https://discourse.llvm.org>`_ is a good place to ask
them.

Note that if you are reading this file from a Git checkout or the main
LLVM web page, this document applies to the *next* release, not the current
one.  To see the release notes for a specific release, please see the `releases
page <https://llvm.org/releases/>`_.

Non-comprehensive list of changes in this release
=================================================
.. NOTE
   For small 1-3 sentence descriptions, just add an entry at the end of
   this list. If your description won't fit comfortably in one bullet
   point (e.g. maybe you would like to give an example of the
   functionality, or simply have a lot to talk about), see the `NOTE` below
   for adding a new subsection.

* Starting with LLVM 19, the Windows installers only include support for the
  X86, ARM, and AArch64 targets in order to keep the build size within the
  limits of the NSIS installer framework.

* ...

Update on required toolchains to build LLVM
-------------------------------------------

* The minimum Python version has been raised from 3.6 to 3.8 across all of LLVM.
  This enables the use of many new Python features, aligning more closely with
  modern Python best practices, and improves CI maintainability
  See `#78828 <https://github.com/llvm/llvm-project/pull/78828>`_ for more info.

Changes to the LLVM IR
----------------------

* Added Memory Model Relaxation Annotations (MMRAs).
* Added ``nusw`` and ``nuw`` flags to ``getelementptr`` instruction.
* Renamed ``llvm.experimental.vector.reverse`` intrinsic to ``llvm.vector.reverse``.
* Renamed ``llvm.experimental.vector.splice`` intrinsic to ``llvm.vector.splice``.
* Renamed ``llvm.experimental.vector.interleave2`` intrinsic to ``llvm.vector.interleave2``.
* Renamed ``llvm.experimental.vector.deinterleave2`` intrinsic to ``llvm.vector.deinterleave2``.
* The constant expression variants of the following instructions have been
  removed:

  * ``icmp``
  * ``fcmp``
  * ``shl``
* LLVM has switched from using debug intrinsics in textual IR to using debug
  records by default. Details of the change and instructions on how to update
  any downstream tools and tests can be found in the `migration docs
  <https://llvm.org/docs/RemoveDIsDebugInfo.html>`_.
* Semantics of MC/DC intrinsics have been changed.

  * ``llvm.instprof.mcdc.parameters``: 3rd argument has been changed
    from bytes to bits.
  * ``llvm.instprof.mcdc.condbitmap.update``: Removed.
  * ``llvm.instprof.mcdc.tvbitmap.update``: 3rd argument has been
    removed. The next argument has been changed from byte index to bit
    index.
* Added ``llvm.experimental.vector.compress`` intrinsic.
* Added special kind of `constant expressions
  <https://llvm.org/docs/LangRef.html#pointer-authentication-constants>`_ to
  represent pointers with signature embedded into it.
* Added `pointer authentication operand bundles
  <https://llvm.org/docs/LangRef.html#pointer-authentication-operand-bundles>`_. 

Changes to LLVM infrastructure
------------------------------

Changes to building LLVM
------------------------

* LLVM now has rpmalloc version 1.4.5 in-tree, as a replacement C allocator for
  hosted toolchains. This supports several host platforms such as Mac or Unix,
  however currently only the Windows 64-bit LLVM release uses it.
  This has a great benefit in terms of build times on Windows when using ThinLTO
  linking, especially on machines with lots of cores, to an order of magnitude
  or more. Clang compilation is also improved. Please see some build timings in
  (`#91862 <https://github.com/llvm/llvm-project/pull/91862#issue-2291033962>`_)
  For more information, refer to the **LLVM_ENABLE_RPMALLOC** option in `CMake variables <https://llvm.org/docs/CMake.html#llvm-related-variables>`_.

* The ``LLVM_ENABLE_TERMINFO`` flag has been removed. LLVM no longer depends on
  terminfo and now always uses the ``TERM`` environment variable for color
  support autodetection.

Changes to TableGen
-------------------

- We can define type aliases via new keyword ``deftype``.

Changes to Interprocedural Optimizations
----------------------------------------

* Hot cold region splitting analysis improvements for overlapping cold regions.

Changes to the AArch64 Backend
------------------------------

* Added support for Cortex-R82AE, Cortex-A78AE, Cortex-A520AE, Cortex-A720AE,
  Cortex-A725, Cortex-X925, Neoverse-N3, Neoverse-V3 and Neoverse-V3AE CPUs.

* ``-mbranch-protection=standard`` now enables FEAT_PAuth_LR by
  default when the feature is enabled. The new behaviour results 
  in ``standard`` being equal to ``bti+pac-ret+pc`` when ``+pauth-lr``
  is passed as part of ``-mcpu=`` options.

* SVE and SVE2 have been moved to the default extensions list for ARMv9.0,
  making them optional per the Arm ARM.  Existing v9.0+ CPUs in the backend that
  support these extensions continue to have these features enabled by default
  when specified via ``-march=`` or an ``-mcpu=`` that supports them.  The
  attribute ``"target-features"="+v9a"`` no longer implies ``"+sve"`` and
  ``"+sve2"`` respectively.
* Added support for ELF pointer authentication relocations as specified in
  `PAuth ABI Extension to ELF
  <https://github.com/ARM-software/abi-aa/blob/main/pauthabielf64/pauthabielf64.rst>`_.
* Added codegeneration, ELF object file and linker support for authenticated
  call lowering, signed constants and emission of signing scheme details in
  ``GNU_PROPERTY_AARCH64_FEATURE_PAUTH`` property of ``.note.gnu.property``
  section.
* Added codegeneration support for ``llvm.ptrauth.auth`` and
  ``llvm.ptrauth.resign`` intrinsics.

Changes to the AMDGPU Backend
-----------------------------

* Implemented the ``llvm.get.fpenv`` and ``llvm.set.fpenv`` intrinsics.
* Added ``!amdgpu.no.fine.grained.memory`` and
  ``!amdgpu.no.remote.memory`` metadata to control atomic behavior.

* Implemented :ref:`llvm.get.rounding <int_get_rounding>` and :ref:`llvm.set.rounding <int_set_rounding>`

* Removed ``llvm.amdgcn.ds.fadd``, ``llvm.amdgcn.ds.fmin`` and
  ``llvm.amdgcn.ds.fmax`` intrinsics. Users should use the
  :ref:`atomicrmw <i_atomicrmw>` instruction with `fadd`, `fmin` and
  `fmax` with addrspace(3) instead.

* AMDGPUAttributor is no longer run as part of the codegen pass
  pipeline. It is expected to run as part of the middle end
  optimizations.

Changes to the ARM Backend
--------------------------

* Added support for Cortex-R52+ CPU.
* FEAT_F32MM is no longer activated by default when using `+sve` on v8.6-A or greater. The feature is still available and can be used by adding `+f32mm` to the command line options.
* armv8-r now implies only fp-armv8d16sp, rather than neon and full fp-armv8. These features are still included by default for cortex-r52. The default cpu for armv8-r is now "generic", for compatibility with variants that do not include neon, fp64, and d32.

Changes to the AVR Backend
--------------------------

Changes to the DirectX Backend
------------------------------

Changes to the Hexagon Backend
------------------------------

Changes to the LoongArch Backend
--------------------------------

* i32 is now a native type in the datalayout string. This enables
  LoopStrengthReduce for loops with i32 induction variables, among other
  optimizations.
* Codegen support is added for TLS Desciptor.
* Interleaved vectorization and vector shuffle are supported on LoongArch and
  the experimental feature ``auto-vec`` is removed.
* Allow ``f16`` codegen with expansion to libcalls.
* Clarify that emulated TLS is not supported.
* A codegen issue for ``bstrins.w`` is fixed on loongarch32.
* Assorted codegen improvements.

Changes to the MIPS Backend
---------------------------

Changes to the PowerPC Backend
------------------------------

* PPC big-endian Linux now supports ``-fpatchable-function-entry``.
* PPC AIX now supports local-dynamic TLS mode.
* PPC AIX saves the Git revision in binaries when built with LLVM_APPEND_VC_REV=ON.
* PPC AIX now supports toc-data attribute for large code model.
* PPC AIX now supports passing arguments by value having greater alignment than
  the pointer size. Currently only compatible with the IBM XL C compiler.
* Add support for the per global code model attribute on AIX.
* Support spilling non-volatile registers for traceback table accuracy on AIX.
* Codegen improvements and bug fixes.

Changes to the RISC-V Backend
-----------------------------

* Added full support for the experimental Zabha (Byte and
  Halfword Atomic Memory Operations) extension.
* Added assembler/disassembler support for the experimenatl Zalasr
  (Load-Acquire and Store-Release) extension.
* The names of the majority of the S-prefixed (supervisor-level) extension
  names in the RISC-V profiles specification are now recognised.
* Codegen support was added for the Zimop (May-Be-Operations) extension.
* The experimental Ssnpm, Smnpm, Smmpm, Sspm, and Supm 1.0.0 Pointer Masking extensions are supported.
* The experimental Ssqosid extension is supported.
* Added the CSR names from the Resumable Non-Maskable Interrupts (Smrnmi) extension.
* llvm-objdump now prints disassembled opcode bytes in groups of 2 or 4 bytes to
  match GNU objdump. The bytes within the groups are in big endian order.
* Added smstateen extension to -march. CSR names for smstateen were already supported.
* Zaamo and Zalrsc are no longer experimental.
* Processors that enable post reg-alloc scheduling (PostMachineScheduler) by default should use the `UsePostRAScheduler` subtarget feature. Setting `PostRAScheduler = 1` in the scheduler model will have no effect on the enabling of the PostMachineScheduler.
* Zabha is no longer experimental.
* B (the collection of the Zba, Zbb, Zbs extensions) is supported.
* Added smcdeleg, ssccfg, smcsrind, and sscsrind extensions to -march.
* ``-mcpu=syntacore-scr3-rv32`` and ``-mcpu=syntacore-scr3-rv64`` were added.
* The default atomics mapping was changed to emit an additional trailing fence
  for sequentially consistent stores, offering compatibility with a future
  mapping using load-acquire and store-release instructions while remaining
  fully compatible with objects produced prior to this change. The mapping
  (ABI) used is recorded as an ELF attribute.
* Ztso is no longer experimental.
* The WCH / Nanjing Qinheng Microelectronics QingKe "XW" compressed opcodes are
  supported under the name "Xwchc".
* ``-mcpu=native`` now detects available features with hwprobe (RISC-V Hardware Probing Interface) on Linux 6.4 or later.
* The version of Zicfilp/Zicfiss is updated to 1.0.

Changes to the WebAssembly Backend
----------------------------------

Changes to the Windows Target
-----------------------------

Changes to the X86 Backend
--------------------------

- Removed knl/knm specific ISA intrinsics: AVX512PF, AVX512ER, PREFETCHWT1,
  while assembly encoding/decoding supports are kept.

- Removed ``3DNow!``-specific ISA intrinsics and codegen support. The ``3dnow`` and ``3dnowa`` target features are no longer supported. The intrinsics ``llvm.x86.3dnow.*``, ``llvm.x86.3dnowa.*``, and ``llvm.x86.mmx.femms`` have been removed. Assembly encoding/decoding for the corresponding instructions remains supported.


Changes to the OCaml bindings
-----------------------------

Changes to the Python bindings
------------------------------

Changes to the C API
--------------------

* Added ``LLVMGetBlockAddressFunction`` and ``LLVMGetBlockAddressBasicBlock``
  functions for accessing the values in a blockaddress constant.

* Added ``LLVMConstStringInContext2`` function, which better matches the C++
  API by using ``size_t`` for string length. Deprecated ``LLVMConstStringInContext``.

* Added the following functions for accessing a function's prefix data:

  * ``LLVMHasPrefixData``
  * ``LLVMGetPrefixData``
  * ``LLVMSetPrefixData``

* Added the following functions for accessing a function's prologue data:

  * ``LLVMHasPrologueData``
  * ``LLVMGetPrologueData``
  * ``LLVMSetPrologueData``

* Deprecated ``LLVMConstNUWNeg`` and ``LLVMBuildNUWNeg``.

* Added ``LLVMAtomicRMWBinOpUIncWrap`` and ``LLVMAtomicRMWBinOpUDecWrap`` to
  ``LLVMAtomicRMWBinOp`` enum for AtomicRMW instructions.

* Added ``LLVMCreateConstantRangeAttribute`` function for creating ConstantRange Attributes.

* Added the following functions for creating and accessing data for CallBr instructions:

  * ``LLVMBuildCallBr``
  * ``LLVMGetCallBrDefaultDest``
  * ``LLVMGetCallBrNumIndirectDests``
  * ``LLVMGetCallBrIndirectDest``

* The following functions for creating constant expressions have been removed,
  because the underlying constant expressions are no longer supported. Instead,
  an instruction should be created using the ``LLVMBuildXYZ`` APIs, which will
  constant fold the operands if possible and create an instruction otherwise:

  * ``LLVMConstICmp``
  * ``LLVMConstFCmp``
  * ``LLVMConstShl``

**Note:** The following changes are due to the removal of the debug info
intrinsics from LLVM and to the introduction of debug records into LLVM.
They are described in detail in the `debug info migration guide <https://llvm.org/docs/RemoveDIsDebugInfo.html>`_.

* Added the following functions to insert before the indicated instruction but
  after any attached debug records.

  * ``LLVMPositionBuilderBeforeDbgRecords``
  * ``LLVMPositionBuilderBeforeInstrAndDbgRecords``

  Same as ``LLVMPositionBuilder`` and ``LLVMPositionBuilderBefore`` except the
  insertion position is set to before the debug records that precede the target
  instruction. ``LLVMPositionBuilder`` and ``LLVMPositionBuilderBefore`` are
  unchanged.

* Added the following functions to get/set the new non-instruction debug info format.
  They will be deprecated in the future and they are just a transition aid.

  * ``LLVMIsNewDbgInfoFormat``
  * ``LLVMSetIsNewDbgInfoFormat``

* Added the following functions to insert a debug record (new debug info format).

  * ``LLVMDIBuilderInsertDeclareRecordBefore``
  * ``LLVMDIBuilderInsertDeclareRecordAtEnd``
  * ``LLVMDIBuilderInsertDbgValueRecordBefore``
  * ``LLVMDIBuilderInsertDbgValueRecordAtEnd``

* Deleted the following functions that inserted a debug intrinsic (old debug info format).

  * ``LLVMDIBuilderInsertDeclareBefore``
  * ``LLVMDIBuilderInsertDeclareAtEnd``
  * ``LLVMDIBuilderInsertDbgValueBefore``
  * ``LLVMDIBuilderInsertDbgValueAtEnd``

* Added the following functions for accessing a Target Extension Type's data:

  * ``LLVMGetTargetExtTypeName``
  * ``LLVMGetTargetExtTypeNumTypeParams``/``LLVMGetTargetExtTypeTypeParam``
  * ``LLVMGetTargetExtTypeNumIntParams``/``LLVMGetTargetExtTypeIntParam``

* Added the following functions for accessing/setting the no-wrap flags for a
  GetElementPtr instruction:

  * ``LLVMBuildGEPWithNoWrapFlags``
  * ``LLVMConstGEPWithNoWrapFlags``
  * ``LLVMGEPGetNoWrapFlags``
  * ``LLVMGEPSetNoWrapFlags``

* Added the following functions for creating and accessing data for ConstantPtrAuth constants:

  * ``LLVMConstantPtrAuth``
  * ``LLVMGetConstantPtrAuthPointer``
  * ``LLVMGetConstantPtrAuthKey``
  * ``LLVMGetConstantPtrAuthDiscriminator``
  * ``LLVMGetConstantPtrAuthAddrDiscriminator``

Changes to the CodeGen infrastructure
-------------------------------------

Changes to the Metadata Info
---------------------------------

Changes to the Debug Info
---------------------------------

* LLVM has switched from using debug intrinsics internally to using debug
  records by default. This should happen transparently when using the DIBuilder
  to construct debug variable information, but will require changes for any code
  that interacts with debug intrinsics directly. Debug intrinsics will only be
  supported on a best-effort basis from here onwards; for more information, see
  the `migration docs <https://llvm.org/docs/RemoveDIsDebugInfo.html>`_.

* When emitting DWARF v2 and not in strict DWARF mode, LLVM will now add
  a ``DW_AT_type`` to instances of ``DW_TAG_enumeration_type``. This is actually
  a DWARF v3 feature which tells tools what the enum's underlying type is.
  Emitting this for v2 as well will help users who have to build binaries with
  DWARF v2 but are using tools that understand newer DWARF standards. Older
  tools will ignore it. (`#98335 <https://github.com/llvm/llvm-project/pull/98335>`_)

Changes to the LLVM tools
---------------------------------
* llvm-nm and llvm-objdump can now print symbol information from linked
  WebAssembly binaries, using information from exports or the "name"
  section for functions, globals and data segments. Symbol addresses and sizes
  are printed as offsets in the file, allowing for binary size analysis. Wasm
  files using reference types and GC are also supported (but also only for
  functions, globals, and data, and only for listing symbols and names).

* llvm-ar now utilizes LLVM_DEFAULT_TARGET_TRIPLE to determine the archive format
  if it's not specified with the ``--format`` argument and cannot be inferred from
  input files.

* llvm-ar now allows specifying COFF archive format with ``--format`` argument
  and uses it by default for COFF targets.

* llvm-ranlib now supports ``-V`` as an alias for ``--version``.
  ``-v`` (``--verbose`` in llvm-ar) has been removed.
  (`#87661 <https://github.com/llvm/llvm-project/pull/87661>`_)

* llvm-objcopy now supports ``--set-symbol-visibility`` and
  ``--set-symbols-visibility`` options for ELF input to change the
  visibility of symbols.

* llvm-objcopy now supports ``--skip-symbol`` and ``--skip-symbols`` options
  for ELF input to skip the specified symbols when executing other options
  that can change a symbol's name, binding or visibility.

* llvm-objcopy now supports ``--compress-sections`` to compress or decompress
  arbitrary sections not within a segment.
  (`#85036 <https://github.com/llvm/llvm-project/pull/85036>`_.)

* llvm-profgen now supports COFF+DWARF binaries. This enables Sample-based PGO
  on Windows using Intel VTune's SEP. For details on usage, see the `end-user
  documentation for SPGO
  <https://clang.llvm.org/docs/UsersManual.html#using-sampling-profilers>`_.

* llvm-readelf's ``-r`` output for RELR has been improved.
  (`#89162 <https://github.com/llvm/llvm-project/pull/89162>`_)
  ``--raw-relr`` has been removed.

* llvm-mca now aborts by default if it is given bad input where previously it
  would continue. Additionally, it can now continue when it encounters
  instructions which lack scheduling information. The behaviour can be
  controlled by the newly introduced
  ``--skip-unsupported-instructions=<none|lack-sched|parse-failure|any>``, as
  documented in ``--help`` output and the command guide. (`#90474
  <https://github.com/llvm/llvm-project/pull/90474>`_)

* llvm-readobj's LLVM output format for ELF core files has been changed.
  Similarly, the JSON format has been fixed for this case. The NT_FILE note
  now has a map for the mapped files. (`#92835
  <https://github.com/llvm/llvm-project/pull/92835>`_).

* llvm-cov now generates HTML report with JavaScript code to allow simple
  jumping between uncovered parts (lines/regions/branches) of code 
  using buttons on top-right corner of the page or using keys (L/R/B or 
  jumping in reverse direction with shift+L/R/B). (`#95662
  <https://github.com/llvm/llvm-project/pull/95662>`_).

* llvm-objcopy now verifies format of ``.note`` sections for ELF input. This can
  be disabled by ``--no-verify-note-sections``. (`#90458
  <https://github.com/llvm/llvm-project/pull/90458>`).

* llvm-objdump now supports the ``--file-headers`` option for XCOFF object files.

Changes to LLDB
---------------------------------

* Register field information is now provided on AArch64 FreeBSD for live
  processes and core files (previously only provided on AArch64 Linux).

* Register field information can now include enums to represent field
  values. Enums have been added for ``fpcr.RMode`` and ``mte_ctrl.TCF``
  for AArch64 targets::

    (lldb) register read fpcr
        fpcr = 0x00000000
             = (AHP = 0, DN = 0, FZ = 0, RMode = RN, <...>)

  If you need to know the values of the enum, these can be found in
  the output of ``register info`` for the same register.

Changes to BOLT
---------------------------------
* Now supports ``--match-profile-with-function-hash`` to match profiled and
  binary functions with exact hash, allowing for the matching of renamed but
  identical functions.

Changes to Sanitizers
---------------------

Other Changes
-------------

External Open Source Projects Using LLVM 19
===========================================

* A project...

Additional Information
======================

A wide variety of additional information is available on the `LLVM web page
<https://llvm.org/>`_, in particular in the `documentation
<https://llvm.org/docs/>`_ section.  The web page also contains versions of the
API documentation which is up-to-date with the Git version of the source
code.  You can access versions of these documents specific to this release by
going into the ``llvm/docs/`` directory in the LLVM tree.

If you have any questions or comments about LLVM, please feel free to contact
us via the `Discourse forums <https://discourse.llvm.org>`_.
