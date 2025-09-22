llvm-tblgen - Target Description to C++ Code for LLVM
=====================================================

.. program:: llvm-tblgen

SYNOPSIS
--------

:program:`llvm-tblgen` [*options*] [*filename*]


DESCRIPTION
-----------

:program:`llvm-tblgen` is a program that translates compiler-related target
description (``.td``) files into C++ code and other output formats. Most
users of LLVM will not need to use this program. It is used only for writing
parts of the compiler.

Please see :doc:`tblgen - Description to C++ Code<./tblgen>`
for a description of the *filename* argument and options, including the
options common to all :program:`*-tblgen` programs.
