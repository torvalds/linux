Linux Kernel Selftests

The kernel contains a set of "self tests" under the tools/testing/selftests/
directory. These are intended to be small unit tests to exercise individual
code paths in the kernel.

Running the selftests
=====================

To build the tests:

  $ make -C tools/testing/selftests


To run the tests:

  $ make -C tools/testing/selftests run_tests

- note that some tests will require root privileges.


To run only tests targetted for a single subsystem:

  $  make -C tools/testing/selftests TARGETS=cpu-hotplug run_tests

See the top-level tools/testing/selftests/Makefile for the list of all possible
targets.


Contributing new tests
======================

In general, the rules for for selftests are

 * Do as much as you can if you're not root;

 * Don't take too long;

 * Don't break the build on any architecture, and

 * Don't cause the top-level "make run_tests" to fail if your feature is
   unconfigured.
