========================================================
Architecture & Platform Information for Compiler Writers
========================================================

.. contents::
   :local:

.. note::

  This document is a work-in-progress.  Additions and clarifications are
  welcome.

Hardware
========

AArch64 & ARM
-------------

* `ARMv8-A Architecture Reference Manual <https://developer.arm.com/docs/ddi0487/latest>`_ This document covers both AArch64 and ARM instructions

* `ARMv7-A Architecture Reference Manual <https://developer.arm.com/docs/ddi0406/latest>`_ This has some useful info on what is supported by older architecture versions.

* `ARMv7-M Architecture Reference Manual <https://developer.arm.com/docs/ddi0403/latest>`_ This covers the Thumb2-only microcontrollers

* `ARMv6-M Architecture Reference Manual <https://developer.arm.com/docs/ddi0419/latest>`_ This covers the Thumb1-only microcontrollers

* `ARM C Language Extensions <http://infocenter.arm.com/help/topic/com.arm.doc.ihi0053c/IHI0053C_acle_2_0.pdf>`_

* `ARM NEON Intrinsics Reference <http://infocenter.arm.com/help/topic/com.arm.doc.ihi0073b/IHI0073B_arm_neon_intrinsics_ref.pdf>`_

* AArch32 `ABI Addenda and Errata <http://infocenter.arm.com/help/topic/com.arm.doc.ihi0045d/IHI0045D_ABI_addenda.pdf>`_

* `Cortex-A57 Software Optimization Guide <http://infocenter.arm.com/help/topic/com.arm.doc.uan0015b/Cortex_A57_Software_Optimization_Guide_external.pdf>`_

* `Run-time ABI for the ARM Architecture <http://infocenter.arm.com/help/topic/com.arm.doc.ihi0043d/IHI0043D_rtabi.pdf>`_ This documents the __aeabi_* helper functions.

Itanium (ia64)
--------------

* `Itanium documentation <http://developer.intel.com/design/itanium2/documentation.htm>`_

Lanai
-----

* `Lanai Instruction Set Architecture <http://g.co/lanai/isa>`_


MIPS
----

* `MIPS Processor Architecture <https://www.mips.com/products/>`_

* `MIPS 64-bit ELF Object File Specification <https://www.linux-mips.org/pub/linux/mips/doc/ABI/elf64-2.4.pdf>`_

PowerPC
-------

IBM - Official manuals and docs
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

* `Power Instruction Set Architecture, Version 3.0B <https://openpowerfoundation.org/?resource_lib=power-isa-version-3-0>`_

* `POWER9 Processor User's Manual <https://openpowerfoundation.org/?resource_lib=power9-processor-users-manual>`_

* `Power Instruction Set Architecture, Version 2.07B <https://openpowerfoundation.org/?resource_lib=ibm-power-isa-version-2-07-b>`_

* `POWER8 Processor User's Manual <https://openpowerfoundation.org/?resource_lib=power8-processor-users-manual>`_

* `Power Instruction Set Architecture, Versions 2.03 through 2.06 (Internet Archive) <https://web.archive.org/web/20121124005736/https://www.power.org/technology-introduction/standards-specifications>`_

* `IBM AIX 7.2 POWER Assembly Reference <https://www.ibm.com/support/knowledgecenter/en/ssw_aix_72/assembler/alangref_kickoff.html>`_

* `IBM AIX/5L for POWER Assembly Reference <http://publibn.boulder.ibm.com/doc_link/en_US/a_doc_lib/aixassem/alangref/alangreftfrm.htm>`_

Embedded PowerPC Processors manuals and docs
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

* `Book E: Enhanced PowerPC Architecture <https://www.nxp.com/docs/en/user-guide/BOOK_EUM.pdf>`_

* `EREF: A Programmer's Reference Manual for Freescale Embedded Processors (EREFRM) <https://www.nxp.com/files-static/32bit/doc/ref_manual/EREF_RM.pdf>`_

* `Signal Processing Engine (SPE) Programming Environments Manual: A Supplement to the EREF <https://www.nxp.com/docs/en/reference-manual/SPEPEM.pdf>`_

* `Variable-Length Encoding (VLE) Programming Environments Manual: A Supplement to the EREF <https://www.nxp.com/docs/en/reference-manual/VLEPEM.pdf>`_

Other documents, collections, notes
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

* `PowerPC Compiler Writer's Guide <http://www.ibm.com/chips/techlib/techlib.nsf/techdocs/852569B20050FF7785256996007558C6>`_
* `Intro to PowerPC Architecture <http://www.ibm.com/developerworks/linux/library/l-powarch/>`_
* `Various IBM specifications and white papers <https://www.power.org/documentation/?document_company=105&document_category=all&publish_year=all&grid_order=DESC&grid_sort=title>`_
* `PowerPC ABI documents <http://penguinppc.org/dev/#library>`_
* `PowerPC64 alignment of long doubles (from GCC) <http://gcc.gnu.org/ml/gcc-patches/2003-09/msg00997.html>`_
* `Long branch stubs for powerpc64-linux (from binutils) <http://sources.redhat.com/ml/binutils/2002-04/msg00573.html>`_

AMDGPU
------

Refer to :doc:`AMDGPUUsage` for additional documentation.

RISC-V
------
* `RISC-V User-Level ISA Specification <https://riscv.org/specifications/>`_

C-SKY
------
* `C-SKY Architecture User Guide <https://github.com/c-sky/csky-doc/blob/master/CSKY%20Architecture%20user_guide.pdf>`_
* `C-SKY V2 ABI <https://github.com/c-sky/csky-doc/blob/master/C-SKY_V2_CPU_Applications_Binary_Interface_Standards_Manual.pdf>`_

LoongArch
---------
* `LoongArch Reference Manual - Volume 1: Basic Architecture <https://loongson.github.io/LoongArch-Documentation/LoongArch-Vol1-EN.html>`_
* `LoongArch ELF ABI specification <https://loongson.github.io/LoongArch-Documentation/LoongArch-ELF-ABI-EN.html>`_

SPARC
-----

* `SPARC standards <http://sparc.org/standards>`_
* `SPARC V9 ABI <http://sparc.org/standards/64.psabi.1.35.ps.Z>`_
* `SPARC V8 ABI <http://sparc.org/standards/psABI3rd.pdf>`_

SystemZ
-------

* `z/Architecture Principles of Operation (registration required, free sign-up) <http://www-01.ibm.com/support/docview.wss?uid=isg2b9de5f05a9d57819852571c500428f9a>`_

VE
--

* `NEC SX-Aurora TSUBASA ISA Guide <https://www.hpc.nec/documents/guide/pdfs/Aurora_ISA_guide.pdf>`_
* `NEC SX-Aurora TSUBASA manuals and documentation <https://www.hpc.nec/documentation>`_

X86
---

* `AMD processor manuals <http://developer.amd.com/resources/developer-guides-manuals/>`_
* `Intel 64 and IA-32 manuals <http://www.intel.com/content/www/us/en/processors/architectures-software-developer-manuals.html>`_
* `Intel Itanium documentation <http://www.intel.com/design/itanium/documentation.htm?iid=ipp_srvr_proc_itanium2+techdocs>`_
* `X86 and X86-64 SysV psABI <https://github.com/hjl-tools/x86-psABI/wiki/X86-psABI>`_
* `Calling conventions for different C++ compilers and operating systems  <http://www.agner.org/optimize/calling_conventions.pdf>`_

XCore
-----

* `The XMOS XS1 Architecture (ISA) <https://www.xmos.ai/download/The-XMOS-XS1-Architecture%281.0%29.pdf>`_
* `The XMOS XS2 Architecture (ISA) <https://www.xmos.ai/download/xCORE-200:-The-XMOS-XS2-Architecture-%28ISA%29%281.1%29.pdf>`_
* `Tools Development Guide (includes ABI) <https://www.xmos.ai/download/Tools-Development-Guide%282.1%29.pdf>`_

Hexagon
-------

* `Hexagon Programmer's Reference Manuals and Hexagon ABI Specification (registration required, free sign-up) <https://developer.qualcomm.com/software/hexagon-dsp-sdk/tools>`_

Other relevant lists
--------------------

* `GCC reading list <http://gcc.gnu.org/readings.html>`_

ABI
===

* `System V Application Binary Interface <http://www.sco.com/developers/gabi/latest/contents.html>`_
* `Itanium C++ ABI <http://itanium-cxx-abi.github.io/cxx-abi/>`_ (This is used for all non-Windows targets.)

Linux
-----

* `Linux extensions to gabi <https://github.com/hjl-tools/linux-abi/wiki/Linux-Extensions-to-gABI>`_
* `64-Bit ELF V2 ABI Specification: Power Architecture <https://openpowerfoundation.org/?resource_lib=64-bit-elf-v2-abi-specification-power-architecture>`_

* `OpenPOWER ELFv2 Errata: ELFv2 ABI Version 1.4 <https://openpowerfoundation.org/?resource_lib=openpower-elfv2-errata-elfv2-abi-version-1-4>`_
* `PowerPC 64-bit ELF ABI Supplement <http://www.linuxbase.org/spec/ELF/ppc64/>`_
* `Procedure Call Standard for the AArch64 Architecture <http://infocenter.arm.com/help/topic/com.arm.doc.ihi0055a/IHI0055A_aapcs64.pdf>`_
* `Procedure Call Standard for the ARM Architecture <https://developer.arm.com/docs/ihi0042/latest>`_
* `ELF for the ARM Architecture <http://infocenter.arm.com/help/topic/com.arm.doc.ihi0044e/IHI0044E_aaelf.pdf>`_
* `ELF for the ARM 64-bit Architecture (AArch64) <http://infocenter.arm.com/help/topic/com.arm.doc.ihi0056a/IHI0056A_aaelf64.pdf>`_
* `System z ELF ABI Supplement <http://legacy.redhat.com/pub/redhat/linux/7.1/es/os/s390x/doc/lzsabi0.pdf>`_

macOS
-----

* `Mach-O Runtime Architecture <http://developer.apple.com/documentation/Darwin/RuntimeArchitecture-date.html>`_
* `Notes on Mach-O ABI <http://www.unsanity.org/archives/000044.php>`_
* `ARM64 Function Calling Conventions <https://developer.apple.com/library/archive/documentation/Xcode/Conceptual/iPhoneOSABIReference/Articles/ARM64FunctionCallingConventions.html>`_

Windows
-------

* `Microsoft PE/COFF Specification <http://www.microsoft.com/whdc/system/platform/firmware/pecoff.mspx>`_
* `ARM64 exception handling <https://docs.microsoft.com/en-us/cpp/build/arm64-exception-handling>`_
* `ARM exception handling <https://docs.microsoft.com/en-us/cpp/build/arm-exception-handling>`_
* `Overview of ARM64 ABI conventions <https://docs.microsoft.com/en-us/cpp/build/arm64-windows-abi-conventions>`_
* `Overview of ARM32 ABI Conventions <https://docs.microsoft.com/en-us/cpp/build/overview-of-arm-abi-conventions>`_

NVPTX
=====

* `CUDA Documentation <http://docs.nvidia.com/cuda/index.html>`_ includes the PTX
  ISA and Driver API documentation

SPIR-V
======

* `SPIR-V documentation <https://www.khronos.org/registry/SPIR-V/>`_

Miscellaneous Resources
=======================

* `Executable File Formats <https://wiki.osdev.org/Category:Executable_Formats>`_
  has a list of various executable file formats.

* `GCC prefetch project <http://gcc.gnu.org/projects/prefetch.html>`_ page has a
  good survey of the prefetching capabilities of a variety of modern
  processors.
