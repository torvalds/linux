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
that the optimizer can make more useful decisions about the code.

MisExpect diagnostics are intended to help developers identify and address
these situations, by comparing the branch weights added by the ``llvm.expect``
intrinsic to those collected through profiling. Whenever these values are
mismatched, a diagnostic is surfaced to the user. Details on how the checks
operate in the LLVM backed can be found in LLVM's documentation.

By default MisExpect checking is quite strict, because the use of the
``llvm.expect`` intrinsic is designed for specialized cases, where the outcome
of a condition is severely skewed. As a result, the optimizer can be extremely
aggressive, which can result in performance degradation if the outcome is less
predictable than the annotation suggests. Even when the annotation is correct
90% of the time, it may be beneficial to either remove the annotation or to use
a different intrinsic that can communicate the probability more directly.

Because this may be too strict, MisExpect diagnostics are not enabled by
default, and support an additional flag to tolerate some deviation from the
exact thresholds. The ``-fdiagnostic-misexpect-tolerance=N`` accepts
deviations when comparing branch weights within ``N%`` of the expected values.
So passing ``-fdiagnostic-misexpect-tolerance=5`` will not report diagnostic messages
if the branch weight from the profile is within 5% of the weight added by
the ``llvm.expect`` intrinsic.

MisExpect diagnostics are also available in the form of optimization remarks,
which can be serialized and processed through the ``opt-viewer.py``
scripts in LLVM.

.. option:: -Rpass=misexpect

  Enables optimization remarks for misexpect when profiling data conflicts with
  use of ``llvm.expect`` intrinsics.


.. option:: -Wmisexpect

  Enables misexpect warnings when profiling data conflicts with use of
  ``llvm.expect`` intrinsics.

.. option:: -fdiagnostic-misexpect-tolerance=N

   Relaxes misexpect checking to tolerate profiling values within N% of the
   expected branch weight. e.g., a value of ``N=5`` allows misexpect to check against
   ``0.95 * Threshold``

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
