llvm-stress - generate random .ll files
=======================================

.. program:: llvm-stress

SYNOPSIS
--------

:program:`llvm-stress` [-size=filesize] [-seed=initialseed] [-o=outfile]

DESCRIPTION
-----------

The :program:`llvm-stress` tool is used to generate random ``.ll`` files that
can be used to test different components of LLVM.

OPTIONS
-------

.. option:: -o filename

 Specify the output filename.

.. option:: -size size

 Specify the size of the generated ``.ll`` file.

.. option:: -seed seed

 Specify the seed to be used for the randomly generated instructions.

EXIT STATUS
-----------

:program:`llvm-stress` returns 0.
