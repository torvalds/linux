****************************************************************************
*                            README                                        *
*                                                                          *
* This file provides all the information regarding new CLI commands that   *
* enable using various hardware features of Intel(R) architecture based    *
* processors from LLDB's CLI.                                              *
****************************************************************************


============
Introduction
============
A shared library has been developed to use various hardware features of
Intel(R) architecture based processors through LLDB's command line. The library
currently comprises of hardware features namely Intel(R) Processor Trace and
Intel(R) Memory Protection Extensions.


============
Details
============
A C++ based cli wrapper (cli-wrapper.cpp) has been developed here that
agglomerates all cli commands for various hardware features. This wrapper is
build to generate a shared library (lldbIntelFeatures) to provide all these
commands.

For each hardware feature, separate cli commands have been developed that are
provided by wrappers (e.g. cli-wrapper-mpxtable.cpp) residing
in feature specific folders.

For details regarding cli commands of each feature, please refer to these
feature specific wrappers.



============
How to Build
============
The shared library (lldbIntelFeatures) has a cmake based build and can be built
while building LLDB with cmake. "cli-wrapper.cpp" file is compiled along with all
the feature specific source files (residing in feature specific folders).

Furthermore, flexibility is provided to the user to include/exclude a particular
feature while building lldbIntelFeatures library. This is done by flags described
below:

  - LLDB_BUILD_INTEL_MPX - Enables building Intel(R) Memory Protection Extensions
    feature (inside intel-mpx folder). This flag defaults to "ON" meaning
    the feature is excluded while building lldbIntelFeatures library.

Please refer to README files in feature specific folders to know about additional
flags that need to be set in order to build that feature successfully.


============
How to Use
============
All CLI commands provided by this shared library can be used through the LLDB's
CLI by executing "plugin load <shared_lib_name>" on LLDB CLI. shared_lib_name here
is lldbIntelFeatures



============
Description
============
Please refer to README_CLI file of each feature to know about details of CLI
commands.
