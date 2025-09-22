This directory contains LLVM bindings for the OCaml programming language
(http://ocaml.org).

Prerequisites
-------------

* OCaml 4.00.0+.
* ctypes 0.4+.
* CMake (to build LLVM).

Building the bindings
---------------------

If all dependencies are present, the bindings will be built and installed
as a part of the default CMake configuration, with no further action.
They will only work with the specific OCaml compiler detected during the build.

The bindings can also be built out-of-tree, i.e. targeting a preinstalled
LLVM. To do this, configure the LLVM build tree as follows:

    $ cmake -DLLVM_OCAML_OUT_OF_TREE=TRUE \
            -DCMAKE_INSTALL_PREFIX=[Preinstalled LLVM path] \
            -DLLVM_OCAML_INSTALL_PATH=[OCaml install prefix] \
            [... any other options]

then build and install it as:

    $ make ocaml_all
    $ cmake -P bindings/ocaml/cmake_install.cmake
