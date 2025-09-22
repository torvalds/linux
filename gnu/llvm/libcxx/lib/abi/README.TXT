This directory contains abi lists representing the symbols exported
by the libc++ library. The lists are generated using libcxx/utils/generate_abi_list.py.

Every time a symbol is added or removed from the libc++ library, each of the
lists *MUST* be updated to reflect the changes. This can be done by using the
`generate-cxx-abilist` CMake target.

We do not keep an up-to-date ABI list for all the build configurations of libc++.
Currently, only the default configuration on MacOS and Linux are supported.
