=============================
User Guide for RISC-V Target
=============================

.. contents::
   :local:

Introduction
============

The RISC-V target provides code generation for processors implementing
supported variations of the RISC-V specification.  It lives in the
``llvm/lib/Target/RISCV`` directory.

Specification Documents
=======================

There have been a number of revisions to the RISC-V specifications. LLVM aims
to implement the most recent ratified version of the standard RISC-V base ISAs
and ISA extensions with pragmatic variances. The most recent specification can
be found at: https://github.com/riscv/riscv-isa-manual/releases/.

`The official RISC-V International specification page
<https://riscv.org/technical/specifications/>`__. is also worth checking, but
tends to significantly lag the specifications linked above. Make sure to check
the `wiki for not yet integrated extensions
<https://wiki.riscv.org/display/HOME/Recently+Ratified+Extensions>`__ and note
that in addition, we sometimes carry support for extensions that have not yet
been ratified (these will be marked as experimental - see below) and support
various vendor-specific extensions (see below).

The current known variances from the specification are:

* Unconditionally allowing instructions from zifencei, zicsr, zicntr, and
  zihpm without gating them on the extensions being enabled.  Previous
  revisions of the specification included these instructions in the base
  ISA, and we preserve this behavior to avoid breaking existing code.  If
  a future revision of the specification reuses these opcodes for other
  extensions, we may need to reevaluate this choice, and thus recommend
  users migrate build systems so as not to rely on this.
* Allowing CSRs to be named without gating on specific extensions.  This
  applies to all CSR names, not just those in zicsr, zicntr, and zihpm.
* The ordering of ``z*``, ``s*``, and ``x*`` prefixed extension names is not
  enforced in user-specified ISA naming strings (e.g. ``-march``).

We are actively deciding not to support multiple specification revisions
at this time. We acknowledge a likely future need, but actively defer the
decisions making around handling this until we have a concrete example of
real hardware having shipped and an incompatible change to the
specification made afterwards.

Base ISAs
=========

The specification defines five base instruction sets: RV32I, RV32E, RV64I,
RV64E, and RV128I. Currently, LLVM fully supports RV32I, and RV64I.  RV32E and
RV64E are supported by the assembly-based tools only.  RV128I is not supported.

To specify the target triple:

  .. table:: RISC-V Architectures

     ============ ==============================================================
     Architecture Description
     ============ ==============================================================
     ``riscv32``   RISC-V with XLEN=32 (i.e. RV32I or RV32E)
     ``riscv64``   RISC-V with XLEN=64 (i.e. RV64I or RV64E)
     ============ ==============================================================

To select an E variant ISA (e.g. RV32E instead of RV32I), use the base
architecture string (e.g. ``riscv32``) with the extension ``e``.

Profiles
========

Supported profile names can be passed using ``-march`` instead of a standard
ISA naming string. Currently supported profiles:

* ``rvi20u32``
* ``rvi20u64``
* ``rva20u64``
* ``rva20s64``
* ``rva22u64``
* ``rva22s64``

Note that you can also append additional extension names to be enabled, e.g.
``rva20u64_zicond`` will enable the ``zicond`` extension in addition to those
in the ``rva20u64`` profile.

Profiles that are not yet ratified cannot be used unless
``-menable-experimental-extensions`` (or equivalent for other tools) is
specified. This applies to the following profiles:

* ``rva23u64``
* ``rva23s64``
* ``rvb23u64``
* ``rvb23s64``
* ``rvm23u32``

.. _riscv-extensions:

Extensions
==========

The following table provides a status summary for extensions which have been
ratified and thus have finalized specifications.  When relevant, detailed notes
on support follow.

  .. table:: Ratified Extensions by Status

     ================  =================================================================
     Extension         Status
     ================  =================================================================
     ``A``             Supported
     ``B``             Supported
     ``C``             Supported
     ``D``             Supported
     ``F``             Supported
     ``E``             Supported (`See note <#riscv-rve-note>`__)
     ``H``             Assembly Support
     ``M``             Supported
     ``Shcounterenw``  Assembly Support (`See note <#riscv-profiles-extensions-note>`__)
     ``Shgatpa``       Assembly Support (`See note <#riscv-profiles-extensions-note>`__)
     ``Shtvala``       Assembly Support (`See note <#riscv-profiles-extensions-note>`__)
     ``Shvsatpa``      Assembly Support (`See note <#riscv-profiles-extensions-note>`__)
     ``Shvstvala``     Assembly Support (`See note <#riscv-profiles-extensions-note>`__)
     ``Shvstvecd``     Assembly Support (`See note <#riscv-profiles-extensions-note>`__)
     ``Smaia``         Supported
     ``Smcdeleg``      Supported
     ``Smcsrind``      Supported
     ``Smepmp``        Supported
     ``Smstateen``     Assembly Support
     ``Ssaia``         Supported
     ``Ssccfg``        Supported
     ``Ssccptr``       Assembly Support (`See note <#riscv-profiles-extensions-note>`__)
     ``Sscofpmf``      Assembly Support
     ``Sscounterenw``  Assembly Support (`See note <#riscv-profiles-extensions-note>`__)
     ``Sscsrind``      Supported
     ``Ssstateen``     Assembly Support (`See note <#riscv-profiles-extensions-note>`__)
     ``Ssstrict``      Assembly Support (`See note <#riscv-profiles-extensions-note>`__)
     ``Sstc``          Assembly Support
     ``Sstvala``       Assembly Support (`See note <#riscv-profiles-extensions-note>`__)
     ``Sstvecd``       Assembly Support (`See note <#riscv-profiles-extensions-note>`__)
     ``Ssu64xl``       Assembly Support (`See note <#riscv-profiles-extensions-note>`__)
     ``Svade``         Assembly Support (`See note <#riscv-profiles-extensions-note>`__)
     ``Svadu``         Assembly Support
     ``Svbare``        Assembly Support (`See note <#riscv-profiles-extensions-note>`__)
     ``Svinval``       Assembly Support
     ``Svnapot``       Assembly Support
     ``Svpbmt``        Supported
     ``V``             Supported
     ``Za128rs``       Supported (`See note <#riscv-profiles-extensions-note>`__)
     ``Za64rs``        Supported (`See note <#riscv-profiles-extensions-note>`__)
     ``Zaamo``         Assembly Support
     ``Zabha``         Supported
     ``Zalrsc``        Assembly Support
     ``Zama16b``       Supported (`See note <#riscv-profiles-extensions-note>`__)
     ``Zawrs``         Assembly Support
     ``Zba``           Supported
     ``Zbb``           Supported
     ``Zbc``           Supported
     ``Zbkb``          Supported (`See note <#riscv-scalar-crypto-note1>`__)
     ``Zbkc``          Supported
     ``Zbkx``          Supported (`See note <#riscv-scalar-crypto-note1>`__)
     ``Zbs``           Supported
     ``Zca``           Supported
     ``Zcb``           Supported
     ``Zcd``           Supported
     ``Zcf``           Supported
     ``Zcmop``         Supported
     ``Zcmp``          Supported
     ``Zcmt``          Assembly Support
     ``Zdinx``         Supported
     ``Zfa``           Supported
     ``Zfbfmin``       Supported
     ``Zfh``           Supported
     ``Zfhmin``        Supported
     ``Zfinx``         Supported
     ``Zhinx``         Supported
     ``Zhinxmin``      Supported
     ``Zic64b``        Supported (`See note <#riscv-profiles-extensions-note>`__)
     ``Zicbom``        Assembly Support
     ``Zicbop``        Supported
     ``Zicboz``        Assembly Support
     ``Ziccamoa``      Supported (`See note <#riscv-profiles-extensions-note>`__)
     ``Ziccif``        Supported (`See note <#riscv-profiles-extensions-note>`__)
     ``Zicclsm``       Supported (`See note <#riscv-profiles-extensions-note>`__)
     ``Ziccrse``       Supported (`See note <#riscv-profiles-extensions-note>`__)
     ``Zicntr``        (`See Note <#riscv-i2p1-note>`__)
     ``Zicond``        Supported
     ``Zicsr``         (`See Note <#riscv-i2p1-note>`__)
     ``Zifencei``      (`See Note <#riscv-i2p1-note>`__)
     ``Zihintntl``     Supported
     ``Zihintpause``   Assembly Support
     ``Zihpm``         (`See Note <#riscv-i2p1-note>`__)
     ``Zimop``         Supported
     ``Zkn``           Supported
     ``Zknd``          Supported (`See note <#riscv-scalar-crypto-note2>`__)
     ``Zkne``          Supported (`See note <#riscv-scalar-crypto-note2>`__)
     ``Zknh``          Supported (`See note <#riscv-scalar-crypto-note2>`__)
     ``Zksed``         Supported (`See note <#riscv-scalar-crypto-note2>`__)
     ``Zksh``          Supported (`See note <#riscv-scalar-crypto-note2>`__)
     ``Zk``            Supported
     ``Zkr``           Supported
     ``Zks``           Supported
     ``Zkt``           Supported
     ``Zmmul``         Supported
     ``Ztso``          Supported
     ``Zvbb``          Assembly Support
     ``Zvbc``          Assembly Support
     ``Zve32x``        (`Partially <#riscv-vlen-32-note>`__) Supported
     ``Zve32f``        (`Partially <#riscv-vlen-32-note>`__) Supported
     ``Zve64x``        Supported
     ``Zve64f``        Supported
     ``Zve64d``        Supported
     ``Zvfbfmin``      Supported
     ``Zvfbfwma``      Supported
     ``Zvfh``          Supported
     ``Zvkb``          Assembly Support
     ``Zvkg``          Assembly Support
     ``Zvkn``          Assembly Support
     ``Zvknc``         Assembly Support
     ``Zvkned``        Assembly Support
     ``Zvkng``         Assembly Support
     ``Zvknha``        Assembly Support
     ``Zvknhb``        Assembly Support
     ``Zvks``          Assembly Support
     ``Zvksc``         Assembly Support
     ``Zvksed``        Assembly Support
     ``Zvksg``         Assembly Support
     ``Zvksh``         Assembly Support
     ``Zvkt``          Assembly Support
     ``Zvl32b``        (`Partially <#riscv-vlen-32-note>`__) Supported
     ``Zvl64b``        Supported
     ``Zvl128b``       Supported
     ``Zvl256b``       Supported
     ``Zvl512b``       Supported
     ``Zvl1024b``      Supported
     ``Zvl2048b``      Supported
     ``Zvl4096b``      Supported
     ``Zvl8192b``      Supported
     ``Zvl16384b``     Supported
     ``Zvl32768b``     Supported
     ``Zvl65536b``     Supported
     ================  =================================================================

Assembly Support
  LLVM supports the associated instructions in assembly.  All assembly related tools (e.g. assembler, disassembler, llvm-objdump, etc..) are supported.  Compiler and linker will accept extension names, and linked binaries will contain appropriate ELF flags and attributes to reflect use of named extension.

Supported
  Fully supported by the compiler.  This includes everything in Assembly Support, along with - if relevant - C language intrinsics for the instructions and pattern matching by the compiler to recognize idiomatic patterns which can be lowered to the associated instructions.

.. _riscv-rve-note:

``E``
  Support of RV32E/RV64E and ilp32e/lp64e ABIs are experimental. To be compatible with the implementation of ilp32e in GCC, we don't use aligned registers to pass variadic arguments. Furthermore, we set the stack alignment to 4 bytes for types with length of 2*XLEN.

.. _riscv-scalar-crypto-note1:

``Zbkb``, ``Zbkx``
  Pattern matching support for these instructions is incomplete.

.. _riscv-scalar-crypto-note2:

``Zknd``, ``Zkne``, ``Zknh``, ``Zksed``, ``Zksh``
  No pattern matching exists.  As a result, these instructions can only be used from assembler or via intrinsic calls.

.. _riscv-vlen-32-note:

``Zve32x``, ``Zve32f``, ``Zvl32b``
  LLVM currently assumes a minimum VLEN (vector register width) of 64 bits during compilation, and as a result ``Zve32x`` and ``Zve32f`` are supported only for VLEN>=64.  Assembly support doesn't have this restriction.

.. _riscv-i2p1-note:

``Zicntr``, ``Zicsr``, ``Zifencei``, ``Zihpm``
  Between versions 2.0 and 2.1 of the base I specification, a backwards incompatible change was made to remove selected instructions and CSRs from the base ISA.  These instructions were grouped into a set of new extensions, but were no longer required by the base ISA.  This change is partially described in "Preface to Document Version 20190608-Base-Ratified" from the specification document (the ``zicntr`` and ``zihpm`` bits are not mentioned).  LLVM currently implements version 2.1 of the base specification. To maintain compatibility, instructions from these extensions are accepted without being in the ``-march`` string.  LLVM also allows the explicit specification of the extensions in an ``-march`` string.

.. _riscv-profiles-extensions-note:

``Za128rs``, ``Za64rs``, ``Zama16b``, ``Zic64b``, ``Ziccamoa``, ``Ziccif``, ``Zicclsm``, ``Ziccrse``, ``Shcounterenvw``, ``Shgatpa``, ``Shtvala``, ``Shvsatpa``, ``Shvstvala``, ``Shvstvecd``, ``Ssccptr``, ``Sscounterenw``, ``Ssstateen``, ``Ssstrict``, ``Sstvala``, ``Sstvecd``, ``Ssu64xl``, ``Svade``, ``Svbare``
  These extensions are defined as part of the `RISC-V Profiles specification <https://github.com/riscv/riscv-profiles/releases/tag/v1.0>`__.  They do not introduce any new features themselves, but instead describe existing hardware features.

Experimental Extensions
=======================

LLVM supports (to various degrees) a number of experimental extensions.  All experimental extensions have ``experimental-`` as a prefix.  There is explicitly no compatibility promised between versions of the toolchain, and regular users are strongly advised *not* to make use of experimental extensions before they reach ratification.

The primary goal of experimental support is to assist in the process of ratification by providing an existence proof of an implementation, and simplifying efforts to validate the value of a proposed extension against large code bases.  Experimental extensions are expected to either transition to ratified status, or be eventually removed.  The decision on whether to accept an experimental extension is currently done on an entirely case by case basis; if you want to propose one, attending the bi-weekly RISC-V sync-up call is strongly advised.

``experimental-ssnpm``, ``experimental-smnpm``, ``experimental-smmpm``, ``experimental-sspm``, ``experimental-supm``
  LLVM implements the `v1.0.0-rc2 specification <https://github.com/riscv/riscv-j-extension/releases/tag/pointer-masking-v1.0.0-rc2>`__.

``experimental-ssqosid``
  LLVM implements assembler support for the `v1.0-rc1 draft specification <https://github.com/riscv/riscv-ssqosid/releases/tag/v1.0-rc1>`_.

``experimental-zacas``
  LLVM implements the `1.0 release specification <https://github.com/riscvarchive/riscv-zacas/releases/tag/v1.0>`__. amocas.w will be used for i32 cmpxchg. amocas.d will be used i64 cmpxchg on RV64. The compiler will not generate amocas.d on RV32 or amocas.q on RV64 due to ABI compatibilty. These can only be used in the assembler. The extension will be left as experimental until `an ABI issue <https://github.com/riscv-non-isa/riscv-elf-psabi-doc/issues/444>`__ is resolved.

``experimental-zalasr``
  LLVM implements the `0.0.5 draft specification <https://github.com/mehnadnerd/riscv-zalasr>`__.

``experimental-zicfilp``, ``experimental-zicfiss``
  LLVM implements the `1.0 release specification <https://github.com/riscv/riscv-cfi/releases/tag/v1.0>`__.

To use an experimental extension from `clang`, you must add `-menable-experimental-extensions` to the command line, and specify the exact version of the experimental extension you are using.  To use an experimental extension with LLVM's internal developer tools (e.g. `llc`, `llvm-objdump`, `llvm-mc`), you must prefix the extension name with `experimental-`.  Note that you don't need to specify the version with internal tools, and shouldn't include the `experimental-` prefix with `clang`.

Vendor Extensions
=================

Vendor extensions are extensions which are not standardized by RISC-V International, and are instead defined by a hardware vendor.  The term vendor extension roughly parallels the definition of a `non-standard` extension from Section 1.3 of the Volume I: RISC-V Unprivileged ISA specification.  In particular, we expect to eventually accept both `custom` extensions and `non-conforming` extensions.

Inclusion of a vendor extension will be considered on a case by case basis.  All proposals should be brought to the bi-weekly RISCV sync calls for discussion.  For a general idea of the factors likely to be considered, please see the `Clang documentation <https://clang.llvm.org/get_involved.html>`__.

It is our intention to follow the naming conventions described in `riscv-non-isa/riscv-toolchain-conventions <https://github.com/riscv-non-isa/riscv-toolchain-conventions#conventions-for-vendor-extensions>`__.  Exceptions to this naming will need to be strongly motivated.

The current vendor extensions supported are:

``XTHeadBa``
  LLVM implements `the THeadBa (address-generation) vendor-defined instructions specified in <https://github.com/T-head-Semi/thead-extension-spec/releases/download/2.2.2/xthead-2023-01-30-2.2.2.pdf>`__ by T-HEAD of Alibaba.  Instructions are prefixed with `th.` as described in the specification.

``XTHeadBb``
  LLVM implements `the THeadBb (basic bit-manipulation) vendor-defined instructions specified in <https://github.com/T-head-Semi/thead-extension-spec/releases/download/2.2.2/xthead-2023-01-30-2.2.2.pdf>`__ by T-HEAD of Alibaba.  Instructions are prefixed with `th.` as described in the specification.

``XTHeadBs``
  LLVM implements `the THeadBs (single-bit operations) vendor-defined instructions specified in <https://github.com/T-head-Semi/thead-extension-spec/releases/download/2.2.2/xthead-2023-01-30-2.2.2.pdf>`__ by T-HEAD of Alibaba.  Instructions are prefixed with `th.` as described in the specification.

``XTHeadCondMov``
  LLVM implements `the THeadCondMov (conditional move) vendor-defined instructions specified in <https://github.com/T-head-Semi/thead-extension-spec/releases/download/2.2.2/xthead-2023-01-30-2.2.2.pdf>`__ by T-HEAD of Alibaba.  Instructions are prefixed with `th.` as described in the specification.

``XTHeadCmo``
  LLVM implements `the THeadCmo (cache management operations) vendor-defined instructions specified in <https://github.com/T-head-Semi/thead-extension-spec/releases/download/2.2.2/xthead-2023-01-30-2.2.2.pdf>`__  by T-HEAD of Alibaba.  Instructions are prefixed with `th.` as described in the specification.

``XTHeadFMemIdx``
  LLVM implements `the THeadFMemIdx (indexed memory operations for floating point) vendor-defined instructions specified in <https://github.com/T-head-Semi/thead-extension-spec/releases/download/2.2.2/xthead-2023-01-30-2.2.2.pdf>`__ by T-HEAD of Alibaba.  Instructions are prefixed with `th.` as described in the specification.

``XTheadMac``
  LLVM implements `the XTheadMac (multiply-accumulate instructions) vendor-defined instructions specified in <https://github.com/T-head-Semi/thead-extension-spec/releases/download/2.2.2/xthead-2023-01-30-2.2.2.pdf>`__ by T-HEAD of Alibaba.  Instructions are prefixed with `th.` as described in the specification.

``XTHeadMemIdx``
  LLVM implements `the THeadMemIdx (indexed memory operations) vendor-defined instructions specified in <https://github.com/T-head-Semi/thead-extension-spec/releases/download/2.2.2/xthead-2023-01-30-2.2.2.pdf>`__ by T-HEAD of Alibaba.  Instructions are prefixed with `th.` as described in the specification.

``XTHeadMemPair``
  LLVM implements `the THeadMemPair (two-GPR memory operations) vendor-defined instructions specified in <https://github.com/T-head-Semi/thead-extension-spec/releases/download/2.2.2/xthead-2023-01-30-2.2.2.pdf>`__ by T-HEAD of Alibaba.  Instructions are prefixed with `th.` as described in the specification.

``XTHeadSync``
  LLVM implements `the THeadSync (multi-core synchronization instructions) vendor-defined instructions specified in <https://github.com/T-head-Semi/thead-extension-spec/releases/download/2.2.2/xthead-2023-01-30-2.2.2.pdf>`__ by T-HEAD of Alibaba.  Instructions are prefixed with `th.` as described in the specification.

``XTHeadVdot``
  LLVM implements `version 1.0.0 of the THeadV-family custom instructions specification <https://github.com/T-head-Semi/thead-extension-spec/releases/download/2.2.0/xthead-2022-12-04-2.2.0.pdf>`__ by T-HEAD of Alibaba.  All instructions are prefixed with `th.` as described in the specification, and the riscv-toolchain-convention document linked above.

``XVentanaCondOps``
  LLVM implements `version 1.0.0 of the VTx-family custom instructions specification <https://github.com/ventanamicro/ventana-custom-extensions/releases/download/v1.0.0/ventana-custom-extensions-v1.0.0.pdf>`__ by Ventana Micro Systems.  All instructions are prefixed with `vt.` as described in the specification, and the riscv-toolchain-convention document linked above.  These instructions are only available for riscv64 at this time.

``XSfvcp``
  LLVM implements `version 1.1.0 of the SiFive Vector Coprocessor Interface (VCIX) Software Specification <https://sifive.cdn.prismic.io/sifive/Zn3m1R5LeNNTwnLS_vcix-spec-software-v1p1.pdf>`__ by SiFive.  All instructions are prefixed with `sf.vc.` as described in the specification, and the riscv-toolchain-convention document linked above.

``XSfvqmaccdod``, ``XSfvqmaccqoq``
  LLVM implements `version 1.1.0 of the SiFive Int8 Matrix Multiplication Extensions Specification <https://sifive.cdn.prismic.io/sifive/1a2ad85b-d818-49f7-ba83-f51f1731edbe_int8-matmul-spec.pdf>`__ by SiFive.  All instructions are prefixed with `sf.` as described in the specification linked above.

``Xsfvfnrclipxfqf``
  LLVM implements `version 1.0.0 of the FP32-to-int8 Ranged Clip Instructions Extension Specification <https://sifive.cdn.prismic.io/sifive/0aacff47-f530-43dc-8446-5caa2260ece0_xsfvfnrclipxfqf-spec.pdf>`__ by SiFive.  All instructions are prefixed with `sf.` as described in the specification linked above.

``Xsfvfwmaccqqq``
  LLVM implements `version 1.0.0 of the Matrix Multiply Accumulate Instruction Extension Specification <https://sifive.cdn.prismic.io/sifive/c391d53e-ffcf-4091-82f6-c37bf3e883ed_xsfvfwmaccqqq-spec.pdf>`__ by SiFive.  All instructions are prefixed with `sf.` as described in the specification linked above.

``XCVbitmanip``
  LLVM implements `version 1.0.0 of the CORE-V Bit Manipulation custom instructions specification <https://github.com/openhwgroup/cv32e40p/blob/62bec66b36182215e18c9cf10f723567e23878e9/docs/source/instruction_set_extensions.rst>`__ by OpenHW Group.  All instructions are prefixed with `cv.` as described in the specification.

``XCVelw``
  LLVM implements `version 1.0.0 of the CORE-V Event load custom instructions specification <https://github.com/openhwgroup/cv32e40p/blob/master/docs/source/instruction_set_extensions.rst>`__ by OpenHW Group.  All instructions are prefixed with `cv.` as described in the specification. These instructions are only available for riscv32 at this time.

``XCVmac``
  LLVM implements `version 1.0.0 of the CORE-V Multiply-Accumulate (MAC) custom instructions specification <https://github.com/openhwgroup/cv32e40p/blob/4f024fe4b15a68b76615b0630c07a6745c620da7/docs/source/instruction_set_extensions.rst>`__ by OpenHW Group.  All instructions are prefixed with `cv.mac` as described in the specification. These instructions are only available for riscv32 at this time.

``XCVmem``
  LLVM implements `version 1.0.0 of the CORE-V Post-Increment load and stores custom instructions specification <https://github.com/openhwgroup/cv32e40p/blob/master/docs/source/instruction_set_extensions.rst>`__ by OpenHW Group.  All instructions are prefixed with `cv.` as described in the specification. These instructions are only available for riscv32 at this time.

``XCValu``
  LLVM implements `version 1.0.0 of the Core-V ALU custom instructions specification <https://github.com/openhwgroup/cv32e40p/blob/4f024fe4b15a68b76615b0630c07a6745c620da7/docs/source/instruction_set_extensions.rst>`__ by Core-V.  All instructions are prefixed with `cv.` as described in the specification. These instructions are only available for riscv32 at this time.

``XCVsimd``
  LLVM implements `version 1.0.0 of the CORE-V SIMD custom instructions specification <https://github.com/openhwgroup/cv32e40p/blob/cv32e40p_v1.3.2/docs/source/instruction_set_extensions.rst>`__ by OpenHW Group.  All instructions are prefixed with `cv.` as described in the specification.

``XCVbi``
  LLVM implements `version 1.0.0 of the CORE-V immediate branching custom instructions specification <https://github.com/openhwgroup/cv32e40p/blob/cv32e40p_v1.3.2/docs/source/instruction_set_extensions.rst>`__ by OpenHW Group.  All instructions are prefixed with `cv.` as described in the specification. These instructions are only available for riscv32 at this time.

``XSiFivecdiscarddlone``
  LLVM implements `the SiFive sf.cdiscard.d.l1 instruction specified in <https://sifive.cdn.prismic.io/sifive/767804da-53b2-4893-97d5-b7c030ae0a94_s76mc_core_complex_manual_21G3.pdf>`_ by SiFive.

``XSiFivecflushdlone``
  LLVM implements `the SiFive sf.cflush.d.l1 instruction specified in <https://sifive.cdn.prismic.io/sifive/767804da-53b2-4893-97d5-b7c030ae0a94_s76mc_core_complex_manual_21G3.pdf>`_ by SiFive.

``XSfcease``
  LLVM implements `the SiFive sf.cease instruction specified in <https://sifive.cdn.prismic.io/sifive/767804da-53b2-4893-97d5-b7c030ae0a94_s76mc_core_complex_manual_21G3.pdf>`_ by SiFive.

``Xwchc``
  LLVM implements `the custom compressed opcodes present in some QingKe cores` by WCH / Nanjing Qinheng Microelectronics. The vendor refers to these opcodes by the name "XW".

Experimental C Intrinsics
=========================

In some cases an extension is non-experimental but the C intrinsics for that
extension are still experimental.  To use C intrinsics for such an extension
from `clang`, you must add `-menable-experimental-extensions` to the command
line.  This currently applies to the following extensions:

* ``Zvbb``
* ``Zvbc``
* ``Zvkb``
* ``Zvkg``
* ``Zvkn``
* ``Zvknc``
* ``Zvkned``
* ``Zvkng``
* ``Zvknha``
* ``Zvknhb``
* ``Zvks``
* ``Zvksc``
* ``Zvksed``
* ``Zvksg``
* ``Zvksh``
* ``Zvkt``
