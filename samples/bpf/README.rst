eBPF sample programs
====================

This directory contains a mini eBPF library, test stubs, verifier
test-suite and examples for using eBPF.

Build dependencies
==================

Compiling requires having installed:
 * clang >= version 3.4.0
 * llvm >= version 3.7.1

Note that LLVM's tool 'llc' must support target 'bpf', list version
and supported targets with command: ``llc --version``

Kernel headers
--------------

There are usually dependencies to header files of the current kernel.
To avoid installing devel kernel headers system wide, as a normal
user, simply call::

 make headers_install

This will creates a local "usr/include" directory in the git/build top
level directory, that the make system automatically pickup first.

Compiling
=========

For building the BPF samples, issue the below command from the kernel
top level directory::

 make samples/bpf/

Do notice the "/" slash after the directory name.

It is also possible to call make from this directory.  This will just
hide the the invocation of make as above with the appended "/".

Manually compiling LLVM with 'bpf' support
------------------------------------------

Since version 3.7.0, LLVM adds a proper LLVM backend target for the
BPF bytecode architecture.

By default llvm will build all non-experimental backends including bpf.
To generate a smaller llc binary one can use::

 -DLLVM_TARGETS_TO_BUILD="BPF"

Quick sniplet for manually compiling LLVM and clang
(build dependencies are cmake and gcc-c++)::

 $ git clone http://llvm.org/git/llvm.git
 $ cd llvm/tools
 $ git clone --depth 1 http://llvm.org/git/clang.git
 $ cd ..; mkdir build; cd build
 $ cmake .. -DLLVM_TARGETS_TO_BUILD="BPF;X86"
 $ make -j $(getconf _NPROCESSORS_ONLN)

It is also possible to point make to the newly compiled 'llc' or
'clang' command via redefining LLC or CLANG on the make command line::

 make samples/bpf/ LLC=~/git/llvm/build/bin/llc CLANG=~/git/llvm/build/bin/clang
