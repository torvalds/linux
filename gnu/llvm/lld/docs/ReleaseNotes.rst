===========================
lld |release| Release Notes
===========================

.. contents::
    :local:

.. only:: PreRelease

  .. warning::
     These are in-progress notes for the upcoming LLVM |release| release.
     Release notes for previous releases can be found on
     `the Download Page <https://releases.llvm.org/download.html>`_.

Introduction
============

This document contains the release notes for the lld linker, release |release|.
Here we describe the status of lld, including major improvements
from the previous release. All lld releases may be downloaded
from the `LLVM releases web site <https://llvm.org/releases/>`_.

Non-comprehensive list of changes in this release
=================================================

ELF Improvements
----------------

* Experimental CREL relocations with explicit addends are now supported using the
  temporary section type code 0x40000020 (``clang -c -Wa,--crel,--allow-experimental-crel``).
  LLVM will change the code and break compatibility (Clang and lld of different
  versions are not guaranteed to cooperate, unlike other features). CREL with
  implicit addends are not supported.
  (`#98115 <https://github.com/llvm/llvm-project/pull/98115>`_)
* ``EI_OSABI`` in the output is now inferred from input object files.
  (`#97144 <https://github.com/llvm/llvm-project/pull/97144>`_)
* ``--compress-sections <section-glib>={none,zlib,zstd}[:level]`` is added to compress
  matched output sections without the ``SHF_ALLOC`` flag.
  (`#84855 <https://github.com/llvm/llvm-project/pull/84855>`_)
  (`#90567 <https://github.com/llvm/llvm-project/pull/90567>`_)
* The default compression level for zlib is now independent of linker
  optimization level (``Z_BEST_SPEED``).
* zstd compression parallelism no longer requires ``ZSTD_MULITHREAD`` build.
* ``GNU_PROPERTY_AARCH64_FEATURE_PAUTH`` notes, ``R_AARCH64_AUTH_ABS64`` and
  ``R_AARCH64_AUTH_RELATIVE`` relocations are now supported.
  (`#72714 <https://github.com/llvm/llvm-project/pull/72714>`_)
* ``--no-allow-shlib-undefined`` now rejects non-exported definitions in the
  ``def-hidden.so ref.so`` case.
  (`#86777 <https://github.com/llvm/llvm-project/issues/86777>`_)
* ``--debug-names`` is added to create a merged ``.debug_names`` index
  from input ``.debug_names`` sections. Type units are not handled yet.
  (`#86508 <https://github.com/llvm/llvm-project/pull/86508>`_)
* ``--enable-non-contiguous-regions`` option allows automatically packing input
  sections into memory regions by automatically spilling to later matches if a
  region would overflow. This reduces the toil of manually packing regions
  (typical for embedded). It also makes full LTO feasible in such cases, since
  IR merging currently prevents the linker script from referring to input
  files. (`#90007 <https://github.com/llvm/llvm-project/pull/90007>`_)
* ``--default-script`/``-dT`` is implemented to specify a default script that is processed
  if ``--script``/``-T`` is not specified.
  (`#89327 <https://github.com/llvm/llvm-project/pull/89327>`_)
* ``--force-group-allocation`` is implemented to discard ``SHT_GROUP`` sections
  and combine relocation sections if their relocated section group members are
  placed to the same output section.
  (`#94704 <https://github.com/llvm/llvm-project/pull/94704>`_)
* ``--build-id`` now defaults to generating a 20-byte digest ("sha1") instead
  of 8-byte ("fast"). This improves compatibility with RPM packaging tools.
  (`#93943 <https://github.com/llvm/llvm-project/pull/93943>`_)
* ``-z lrodata-after-bss`` is implemented to place ``.lrodata`` after ``.bss``.
  (`#81224 <https://github.com/llvm/llvm-project/pull/81224>`_)
* ``--export-dynamic`` no longer creates dynamic sections for ``-no-pie`` static linking.
* ``--lto-emit-asm`` is now added as the canonical spelling of ``--plugin-opt=emit-llvm``.
* ``--lto-emit-llvm`` now uses the pre-codegen module.
  (`#97480 <https://github.com/llvm/llvm-project/pull/97480>`_)
* When AArch64 PAuth is enabled, ``-z pack-relative-relocs`` now encodes ``R_AARCH64_AUTH_RELATIVE`` relocations in ``.rela.auth.dyn``.
  (`#96496 <https://github.com/llvm/llvm-project/pull/96496>`_)
* ``-z gcs`` and ``-z gcs-report`` are now supported for AArch64 Guarded Control Stack extension.
* ``-r`` now forces ``-Bstatic``.
* Thumb2 PLT is now supported for Cortex-M processors.
  (`#93644 <https://github.com/llvm/llvm-project/pull/93644>`_)
* ``DW_EH_sdata4`` of addresses larger than 0x80000000 is now supported for MIPS32.
  (`#92438 <https://github.com/llvm/llvm-project/pull/92438>`_)
* Certain unknown section types are rejected.
  (`#85173 <https://github.com/llvm/llvm-project/pull/85173>`_)
* ``PROVIDE(lhs = rhs) PROVIDE(rhs = ...)``, ``lhs`` is now defined only if ``rhs`` is needed.
  (`#74771 <https://github.com/llvm/llvm-project/issues/74771>`_)
  (`#87530 <https://github.com/llvm/llvm-project/pull/87530>`_)
* ``OUTPUT_FORMAT(binary)`` is now supported.
  (`#98837 <https://github.com/llvm/llvm-project/pull/98837>`_)
* ``NOCROSSREFS`` and ``NOCRFOSSREFS_TO`` commands now supported to prohibit
  cross references between certain output sections.
  (`#98773 <https://github.com/llvm/llvm-project/pull/98773>`_)
* Orphan placement is refined to prefer the last similar section when its rank <= orphan's rank.
  (`#94099 <https://github.com/llvm/llvm-project/pull/94099>`_)
  Non-alloc orphan sections are now placed at the end.
  (`#94519 <https://github.com/llvm/llvm-project/pull/94519>`_)
* ``R_X86_64_REX_GOTPCRELX`` of the addq form is no longer incorrectly optimized when the address is larger than 0x80000000.

Breaking changes
----------------

COFF Improvements
-----------------

MinGW Improvements
------------------

MachO Improvements
------------------

* Chained fixups are now enabled by default when targeting macOS 13.0,
  iOS 13.4, tvOS 14.0, watchOS 7.0, and visionOS 1.0 or later.
  They can be disabled with the `-no_fixup_chains` flag.
  (`#99255 <https://github.com/llvm/llvm-project/pull/99255>`_)

WebAssembly Improvements
------------------------

Fixes
#####
