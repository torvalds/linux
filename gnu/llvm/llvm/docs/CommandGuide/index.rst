LLVM Command Guide
------------------

The following documents are command descriptions for all of the LLVM tools.
These pages describe how to use the LLVM commands and what their options are.
Note that these pages do not describe all of the options available for all
tools. To get a complete listing, pass the ``--help`` (general options) or
``--help-hidden`` (general and debugging options) arguments to the tool you are
interested in.

Basic Commands
~~~~~~~~~~~~~~

.. toctree::
   :maxdepth: 1

   dsymutil
   llc
   lli
   llvm-as
   llvm-config
   llvm-cov
   llvm-cxxmap
   llvm-debuginfo-analyzer
   llvm-diff
   llvm-dis
   llvm-dwarfdump
   llvm-dwarfutil
   llvm-lib
   llvm-libtool-darwin
   llvm-link
   llvm-lipo
   llvm-mc
   llvm-mca
   llvm-opt-report
   llvm-otool
   llvm-profdata
   llvm-readobj
   llvm-reduce
   llvm-stress
   llvm-symbolizer
   opt

GNU binutils replacements
~~~~~~~~~~~~~~~~~~~~~~~~~

.. toctree::
   :maxdepth: 1

   llvm-addr2line
   llvm-ar
   llvm-cxxfilt
   llvm-install-name-tool
   llvm-nm
   llvm-objcopy
   llvm-objdump
   llvm-ranlib
   llvm-readelf
   llvm-size
   llvm-strings
   llvm-strip

Debugging Tools
~~~~~~~~~~~~~~~

.. toctree::
   :maxdepth: 1

   bugpoint
   llvm-extract
   llvm-bcanalyzer

Developer Tools
~~~~~~~~~~~~~~~

.. toctree::
   :maxdepth: 1

   FileCheck
   tblgen
   clang-tblgen
   lldb-tblgen
   llvm-tblgen
   mlir-tblgen
   lit
   llvm-exegesis
   llvm-ifs
   llvm-locstats
   llvm-pdbutil
   llvm-profgen
   llvm-tli-checker

Remarks Tools
~~~~~~~~~~~~~~

.. toctree::
   :maxdepth: 1

   llvm-remarkutil
