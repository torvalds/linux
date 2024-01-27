==================
BPF Selftest Notes
==================
General instructions on running selftests can be found in
`Documentation/bpf/bpf_devel_QA.rst`__.

__ /Documentation/bpf/bpf_devel_QA.rst#q-how-to-run-bpf-selftests

=============
BPF CI System
=============

BPF employs a continuous integration (CI) system to check patch submission in an
automated fashion. The system runs selftests for each patch in a series. Results
are propagated to patchwork, where failures are highlighted similar to
violations of other checks (such as additional warnings being emitted or a
``scripts/checkpatch.pl`` reported deficiency):

  https://patchwork.kernel.org/project/netdevbpf/list/?delegate=121173

The CI system executes tests on multiple architectures. It uses a kernel
configuration derived from both the generic and architecture specific config
file fragments below ``tools/testing/selftests/bpf/`` (e.g., ``config`` and
``config.x86_64``).

Denylisting Tests
=================

It is possible for some architectures to not have support for all BPF features.
In such a case tests in CI may fail. An example of such a shortcoming is BPF
trampoline support on IBM's s390x architecture. For cases like this, an in-tree
deny list file, located at ``tools/testing/selftests/bpf/DENYLIST.<arch>``, can
be used to prevent the test from running on such an architecture.

In addition to that, the generic ``tools/testing/selftests/bpf/DENYLIST`` is
honored on every architecture running tests.

These files are organized in three columns. The first column lists the test in
question. This can be the name of a test suite or of an individual test. The
remaining two columns provide additional meta data that helps identify and
classify the entry: column two is a copy and paste of the error being reported
when running the test in the setting in question. The third column, if
available, summarizes the underlying problem. A value of ``trampoline``, for
example, indicates that lack of trampoline support is causing the test to fail.
This last entry helps identify tests that can be re-enabled once such support is
added.

=========================
Running Selftests in a VM
=========================

It's now possible to run the selftests using ``tools/testing/selftests/bpf/vmtest.sh``.
The script tries to ensure that the tests are run with the same environment as they
would be run post-submit in the CI used by the Maintainers, with the exception
that deny lists are not automatically honored.

This script uses the in-tree kernel configuration and downloads a VM userspace
image from the system used by the CI. It builds the kernel (without overwriting
your existing Kconfig), recompiles the bpf selftests, runs them (by default
``tools/testing/selftests/bpf/test_progs``) and saves the resulting output (by
default in ``~/.bpf_selftests``).

Script dependencies:
- clang (preferably built from sources, https://github.com/llvm/llvm-project);
- pahole (preferably built from sources, https://git.kernel.org/pub/scm/devel/pahole/pahole.git/);
- qemu;
- docutils (for ``rst2man``);
- libcap-devel.

For more information about using the script, run:

.. code-block:: console

  $ tools/testing/selftests/bpf/vmtest.sh -h

In case of linker errors when running selftests, try using static linking:

.. code-block:: console

  $ LDLIBS=-static PKG_CONFIG='pkg-config --static' vmtest.sh

.. note:: Some distros may not support static linking.

.. note:: The script uses pahole and clang based on host environment setting.
          If you want to change pahole and llvm, you can change `PATH` environment
          variable in the beginning of script.

.. note:: The script currently only supports x86_64 and s390x architectures.

Additional information about selftest failures are
documented here.

profiler[23] test failures with clang/llvm <12.0.0
==================================================

With clang/llvm <12.0.0, the profiler[23] test may fail.
The symptom looks like

.. code-block:: c

  // r9 is a pointer to map_value
  // r7 is a scalar
  17:       bf 96 00 00 00 00 00 00 r6 = r9
  18:       0f 76 00 00 00 00 00 00 r6 += r7
  math between map_value pointer and register with unbounded min value is not allowed

  // the instructions below will not be seen in the verifier log
  19:       a5 07 01 00 01 01 00 00 if r7 < 257 goto +1
  20:       bf 96 00 00 00 00 00 00 r6 = r9
  // r6 is used here

The verifier will reject such code with above error.
At insn 18 the r7 is indeed unbounded. The later insn 19 checks the bounds and
the insn 20 undoes map_value addition. It is currently impossible for the
verifier to understand such speculative pointer arithmetic.
Hence `this patch`__ addresses it on the compiler side. It was committed on llvm 12.

__ https://github.com/llvm/llvm-project/commit/ddf1864ace484035e3cde5e83b3a31ac81e059c6

The corresponding C code

.. code-block:: c

  for (int i = 0; i < MAX_CGROUPS_PATH_DEPTH; i++) {
          filepart_length = bpf_probe_read_str(payload, ...);
          if (filepart_length <= MAX_PATH) {
                  barrier_var(filepart_length); // workaround
                  payload += filepart_length;
          }
  }

bpf_iter test failures with clang/llvm 10.0.0
=============================================

With clang/llvm 10.0.0, the following two bpf_iter tests failed:
  * ``bpf_iter/ipv6_route``
  * ``bpf_iter/netlink``

The symptom for ``bpf_iter/ipv6_route`` looks like

.. code-block:: c

  2: (79) r8 = *(u64 *)(r1 +8)
  ...
  14: (bf) r2 = r8
  15: (0f) r2 += r1
  ; BPF_SEQ_PRINTF(seq, "%pi6 %02x ", &rt->fib6_dst.addr, rt->fib6_dst.plen);
  16: (7b) *(u64 *)(r8 +64) = r2
  only read is supported

The symptom for ``bpf_iter/netlink`` looks like

.. code-block:: c

  ; struct netlink_sock *nlk = ctx->sk;
  2: (79) r7 = *(u64 *)(r1 +8)
  ...
  15: (bf) r2 = r7
  16: (0f) r2 += r1
  ; BPF_SEQ_PRINTF(seq, "%pK %-3d ", s, s->sk_protocol);
  17: (7b) *(u64 *)(r7 +0) = r2
  only read is supported

This is due to a llvm BPF backend bug. `The fix`__
has been pushed to llvm 10.x release branch and will be
available in 10.0.1. The patch is available in llvm 11.0.0 trunk.

__  https://github.com/llvm/llvm-project/commit/3cb7e7bf959dcd3b8080986c62e10a75c7af43f0

bpf_verif_scale/loop6.bpf.o test failure with Clang 12
======================================================

With Clang 12, the following bpf_verif_scale test failed:
  * ``bpf_verif_scale/loop6.bpf.o``

The verifier output looks like

.. code-block:: c

  R1 type=ctx expected=fp
  The sequence of 8193 jumps is too complex.

The reason is compiler generating the following code

.. code-block:: c

  ;       for (i = 0; (i < VIRTIO_MAX_SGS) && (i < num); i++) {
      14:       16 05 40 00 00 00 00 00 if w5 == 0 goto +64 <LBB0_6>
      15:       bc 51 00 00 00 00 00 00 w1 = w5
      16:       04 01 00 00 ff ff ff ff w1 += -1
      17:       67 05 00 00 20 00 00 00 r5 <<= 32
      18:       77 05 00 00 20 00 00 00 r5 >>= 32
      19:       a6 01 01 00 05 00 00 00 if w1 < 5 goto +1 <LBB0_4>
      20:       b7 05 00 00 06 00 00 00 r5 = 6
  00000000000000a8 <LBB0_4>:
      21:       b7 02 00 00 00 00 00 00 r2 = 0
      22:       b7 01 00 00 00 00 00 00 r1 = 0
  ;       for (i = 0; (i < VIRTIO_MAX_SGS) && (i < num); i++) {
      23:       7b 1a e0 ff 00 00 00 00 *(u64 *)(r10 - 32) = r1
      24:       7b 5a c0 ff 00 00 00 00 *(u64 *)(r10 - 64) = r5

Note that insn #15 has w1 = w5 and w1 is refined later but
r5(w5) is eventually saved on stack at insn #24 for later use.
This cause later verifier failure. The bug has been `fixed`__ in
Clang 13.

__  https://github.com/llvm/llvm-project/commit/1959ead525b8830cc8a345f45e1c3ef9902d3229

BPF CO-RE-based tests and Clang version
=======================================

A set of selftests use BPF target-specific built-ins, which might require
bleeding-edge Clang versions (Clang 12 nightly at this time).

Few sub-tests of core_reloc test suit (part of test_progs test runner) require
the following built-ins, listed with corresponding Clang diffs introducing
them to Clang/LLVM. These sub-tests are going to be skipped if Clang is too
old to support them, they shouldn't cause build failures or runtime test
failures:

- __builtin_btf_type_id() [0_, 1_, 2_];
- __builtin_preserve_type_info(), __builtin_preserve_enum_value() [3_, 4_].

.. _0: https://github.com/llvm/llvm-project/commit/6b01b465388b204d543da3cf49efd6080db094a9
.. _1: https://github.com/llvm/llvm-project/commit/072cde03aaa13a2c57acf62d79876bf79aa1919f
.. _2: https://github.com/llvm/llvm-project/commit/00602ee7ef0bf6c68d690a2bd729c12b95c95c99
.. _3: https://github.com/llvm/llvm-project/commit/6d218b4adb093ff2e9764febbbc89f429412006c
.. _4: https://github.com/llvm/llvm-project/commit/6d6750696400e7ce988d66a1a00e1d0cb32815f8

Floating-point tests and Clang version
======================================

Certain selftests, e.g. core_reloc, require support for the floating-point
types, which was introduced in `Clang 13`__. The older Clang versions will
either crash when compiling these tests, or generate an incorrect BTF.

__  https://github.com/llvm/llvm-project/commit/a7137b238a07d9399d3ae96c0b461571bd5aa8b2

Kernel function call test and Clang version
===========================================

Some selftests (e.g. kfunc_call and bpf_tcp_ca) require a LLVM support
to generate extern function in BTF.  It was introduced in `Clang 13`__.

Without it, the error from compiling bpf selftests looks like:

.. code-block:: console

  libbpf: failed to find BTF for extern 'tcp_slow_start' [25] section: -2

__ https://github.com/llvm/llvm-project/commit/886f9ff53155075bd5f1e994f17b85d1e1b7470c

btf_tag test and Clang version
==============================

The btf_tag selftest requires LLVM support to recognize the btf_decl_tag and
btf_type_tag attributes. They are introduced in `Clang 14` [0_, 1_].
The subtests ``btf_type_tag_user_{mod1, mod2, vmlinux}`` also requires
pahole version ``1.23``.

Without them, the btf_tag selftest will be skipped and you will observe:

.. code-block:: console

  #<test_num> btf_tag:SKIP

.. _0: https://github.com/llvm/llvm-project/commit/a162b67c98066218d0d00aa13b99afb95d9bb5e6
.. _1: https://github.com/llvm/llvm-project/commit/3466e00716e12e32fdb100e3fcfca5c2b3e8d784

Clang dependencies for static linking tests
===========================================

linked_vars, linked_maps, and linked_funcs tests depend on `Clang fix`__ to
generate valid BTF information for weak variables. Please make sure you use
Clang that contains the fix.

__ https://github.com/llvm/llvm-project/commit/968292cb93198442138128d850fd54dc7edc0035

Clang relocation changes
========================

Clang 13 patch `clang reloc patch`_  made some changes on relocations such
that existing relocation types are broken into more types and
each new type corresponds to only one way to resolve relocation.
See `kernel llvm reloc`_ for more explanation and some examples.
Using clang 13 to compile old libbpf which has static linker support,
there will be a compilation failure::

  libbpf: ELF relo #0 in section #6 has unexpected type 2 in .../bpf_tcp_nogpl.bpf.o

Here, ``type 2`` refers to new relocation type ``R_BPF_64_ABS64``.
To fix this issue, user newer libbpf.

.. Links
.. _clang reloc patch: https://github.com/llvm/llvm-project/commit/6a2ea84600ba4bd3b2733bd8f08f5115eb32164b
.. _kernel llvm reloc: /Documentation/bpf/llvm_reloc.rst

Clang dependencies for the u32 spill test (xdpwall)
===================================================
The xdpwall selftest requires a change in `Clang 14`__.

Without it, the xdpwall selftest will fail and the error message
from running test_progs will look like:

.. code-block:: console

  test_xdpwall:FAIL:Does LLVM have https://github.com/llvm/llvm-project/commit/ea72b0319d7b0f0c2fcf41d121afa5d031b319d5? unexpected error: -4007

__ https://github.com/llvm/llvm-project/commit/ea72b0319d7b0f0c2fcf41d121afa5d031b319d5
