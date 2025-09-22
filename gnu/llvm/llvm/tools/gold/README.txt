The LLVM Gold LTO Plugin
========================

This directory contains a plugin that is designed to work with binutils
gold linker. At present time, this is not the default linker in
binutils, and the default build of gold does not support plugins.

See docs/GoldPlugin.html for complete build and usage instructions.

NOTE: libLTO and LLVMgold aren't built without PIC because they would fail
to link on x86-64 with a relocation error: PIC and non-PIC can't be combined.
As an alternative to passing --enable-pic, you can use 'make ENABLE_PIC=1' in
your entire LLVM build.
