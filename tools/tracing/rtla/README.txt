RTLA: Real-Time Linux Analysis tools

The rtla meta-tool includes a set of commands that aims to analyze
the real-time properties of Linux. Instead of testing Linux as a black box,
rtla leverages kernel tracing capabilities to provide precise information
about the properties and root causes of unexpected results.

Installing RTLA

RTLA depends on the following libraries and tools:

 - libtracefs
 - libtraceevent
 - libcpupower (optional, for --deepest-idle-state)
 - libcheck (optional, for unit tests)

For BPF sample collection support, the following extra dependencies are
required:

 - libbpf 1.0.0 or later
 - bpftool with skeleton support
 - clang with BPF CO-RE support

It also depends on python3-docutils to compile man pages.

For development, we suggest the following steps for compiling rtla:

  $ git clone git://git.kernel.org/pub/scm/libs/libtrace/libtraceevent.git
  $ cd libtraceevent/
  $ make
  $ sudo make install
  $ cd ..
  $ git clone git://git.kernel.org/pub/scm/libs/libtrace/libtracefs.git
  $ cd libtracefs/
  $ make
  $ sudo make install
  $ cd ..
  $ cd $libcpupower_src
  $ make
  $ sudo make install
  $ cd $rtla_src
  $ make
  $ sudo make install

Running tests

RTLA has two test suites: a runtime test suite and a unit test suite.

The runtime test suite is available as "make check" (root required) and has
the following dependencies, in addition to RTLA build dependencies:

- Perl
- Test::Harness (libtest-harness-perl on Debian/Ubuntu, perl-Test-Harness on Fedora/RHEL)
- bash
- coreutils
- ldd
- util-linux
- procps(-ng)
- bpftool (if rtla is built against libbpf)

as well as the following required system configuration:

- CONFIG_OSNOISE_TRACER=y
- CONFIG_TIMERLAT_TRACER=y
- tracefs mounted and readable at /sys/kernel/tracing

The unit test suite is available as "make unit-tests" and has the following
dependencies:

- libcheck

Unlike the runtime test suite, root is not required to run unit tests, nor is
a tracefs/osnoise/timerlat-capable kernel required.

For further information, please refer to the rtla man page.
