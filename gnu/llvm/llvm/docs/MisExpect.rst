===================
Misexpect
===================
.. contents::

.. toctree::
   :maxdepth: 1

When developers use ``llvm.expect`` intrinsics, i.e., through use of
``__builtin_expect(...)``, they are trying to communicate how their code is
expected to behave at runtime to the optimizer. These annotations, however, can
be incorrect for a variety of reasons: changes to the code base invalidate them
silently, the developer mis-annotated them (e.g., using ``LIKELY`` instead of
``UNLIKELY``), or perhaps they assumed something incorrectly when they wrote
the annotation. Regardless of why, it is useful to detect these situations so
that the optimizer can make more useful decisions about the code. MisExpect
diagnostics are intended to help developers identify and address these
situations, by comparing the use of the ``llvm.expect`` intrinsic to the ground
truth provided by a profiling input.

The MisExpect checks in the LLVM backend follow a simple procedure: if there is
a mismatch between the branch weights collected during profiling and those
supplied by an ``llvm.expect`` intrinsic, then it will emit a diagnostic
message to the user.

The most natural place to perform the verification is just prior to when
branch weights are assigned to the target instruction in the form of
branch weight metadata.

There are 3 key places in the LLVM backend where branch weights are
created and assigned based on profiling information or the use of the
``llvm.expect`` intrinsic, and our implementation focuses on these
places to perform the verification.

We calculate the threshold for emitting MisExpect related diagnostics
based on the values the compiler assigns to ``llvm.expect`` intrinsics,
which can be set through the ``-likely-branch-weight`` and
``-unlikely-branch-weight`` LLVM options. During verification, if the
profile weights mismatch the calculated threshold, then we will emit a
remark or warning detailing a potential performance regression. The
diagnostic also reports the percentage of the time the annotation was
correct during profiling to help developers reason about how to proceed.

The diagnostics are also available in the form of optimization remarks,
which can be serialized and processed through the ``opt-viewer.py``
scripts in LLVM.

.. option:: -pass-remarks=misexpect

  Enables optimization remarks for misexpect when profiling data conflicts with
  use of ``llvm.expect`` intrinsics.


.. option:: -pgo-warn-misexpect

  Enables misexpect warnings when profiling data conflicts with use of
  ``llvm.expect`` intrinsics.

LLVM supports 4 types of profile formats: Frontend, IR, CS-IR, and
Sampling. MisExpect Diagnostics are compatible with all Profiling formats.

+----------------+--------------------------------------------------------------------------------------+
| Profile Type   | Description                                                                          |
+================+======================================================================================+
| Frontend       | Profiling instrumentation added during compilation by the frontend, i.e. ``clang``   |
+----------------+--------------------------------------------------------------------------------------+
| IR             | Profiling instrumentation added during by the LLVM backend                           |
+----------------+--------------------------------------------------------------------------------------+
| CS-IR          | Context Sensitive IR based profiles                                                  |
+----------------+--------------------------------------------------------------------------------------+
| Sampling       | Profiles collected through sampling with external tools, such as ``perf`` on Linux   |
+----------------+--------------------------------------------------------------------------------------+

