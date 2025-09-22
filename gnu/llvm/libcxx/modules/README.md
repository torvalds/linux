# The "module partitions" for the std module

The files in this directory contain the exported named declarations per header.
These files are used for the following purposes:

 - During testing exported named declarations are tested against the named
   declarations in the associated header. This excludes reserved names; they
   are not exported.
 - Generate the module std.

These use cases require including the required headers for these "partitions"
at different locations. This means the user of these "partitions" are
responsible for including the proper header and validating whether the header can
be loaded in the current libc++ configuration. For example "include <locale>"
fails when locales are not available. The "partitions" use the libc++ feature
macros to export the declarations available in the current configuration. This
configuration is available if the user includes the `__config' header.

We use `.inc` files that we include from the top-level module instead of
using real C++ module partitions. This is a lot faster than module partitions,
see [this](https://discourse.llvm.org/t/alternatives-to-the-implementation-of-std-modules/71958) for details.
