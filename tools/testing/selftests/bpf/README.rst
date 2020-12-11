==================
BPF Selftest Notes
==================
General instructions on running selftests can be found in
`Documentation/bpf/bpf_devel_QA.rst`_.

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
Hence
    https://reviews.llvm.org/D85570
addresses it on the compiler side. It was committed on llvm 12.

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

This is due to a llvm BPF backend bug. The fix 
  https://reviews.llvm.org/D78466
has been pushed to llvm 10.x release branch and will be
available in 10.0.1. The fix is available in llvm 11.0.0 trunk.

BPF CO-RE-based tests and Clang version
=======================================

A set of selftests use BPF target-specific built-ins, which might require
bleeding-edge Clang versions (Clang 12 nightly at this time).

Few sub-tests of core_reloc test suit (part of test_progs test runner) require
the following built-ins, listed with corresponding Clang diffs introducing
them to Clang/LLVM. These sub-tests are going to be skipped if Clang is too
old to support them, they shouldn't cause build failures or runtime test
failures:

  - __builtin_btf_type_id() ([0], [1], [2]);
  - __builtin_preserve_type_info(), __builtin_preserve_enum_value() ([3], [4]).

  [0] https://reviews.llvm.org/D74572
  [1] https://reviews.llvm.org/D74668
  [2] https://reviews.llvm.org/D85174
  [3] https://reviews.llvm.org/D83878
  [4] https://reviews.llvm.org/D83242
