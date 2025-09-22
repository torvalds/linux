========================================
LLVM Security Group Transparency Reports
========================================

This page lists the yearly LLVM Security group transparency reports.

2021
----

The :doc:`LLVM security group <Security>` was established on the 10th of July
2020 by the act of the `initial
commit <https://github.com/llvm/llvm-project/commit/7bf73bcf6d93>`_ describing
the purpose of the group and the processes it follows.  Many of the group's
processes were still not well-defined enough for the group to operate well.
Over the course of 2021, the key processes were defined well enough to enable
the group to operate reasonably well:

* We defined details on how to report security issues, see `this commit on
  20th of May 2021 <https://github.com/llvm/llvm-project/commit/c9dbaa4c86d2>`_
* We refined the nomination process for new group members, see `this
  commit on 30th of July 2021 <https://github.com/llvm/llvm-project/commit/4c98e9455aad>`_
* We started writing an annual transparency report (you're reading the 2021
  report here).

Over the course of 2021, we had 2 people leave the LLVM Security group and 4
people join.

In 2021, the security group received 13 issue reports that were made publicly
visible before 31st of December 2021.  The security group judged 2 of these
reports to be security issues:

* https://bugs.chromium.org/p/llvm/issues/detail?id=5
* https://bugs.chromium.org/p/llvm/issues/detail?id=11

Both issues were addressed with source changes: #5 in clangd/vscode-clangd, and
#11 in llvm-project.  No dedicated LLVM release was made for either.

We believe that with the publishing of this first annual transparency report,
the security group now has implemented all necessary processes for the group to
operate as promised. The group's processes can be improved further, and we do
expect further improvements to get implemented in 2022. Many of the potential
improvements end up being discussed on the `monthly public call on LLVM's
security group <https://llvm.org/docs/GettingInvolved.html#online-sync-ups>`_.


2022
----

In this section we report on the issues the group received in 2022, or on issues
that were received earlier, but were disclosed in 2022.

In 2022, the llvm security group received 15 issues that have been disclosed at
the time of writing this transparency report.

5 of these were judged to be security issues:

* https://bugs.chromium.org/p/llvm/issues/detail?id=17 reports a miscompile in
  LLVM that can result in the frame pointer and return address being
  overwritten. This was fixed.

* https://bugs.chromium.org/p/llvm/issues/detail?id=19 reports a vulnerability
  in `std::filesystem::remove_all` in libc++. This was fixed.

* https://bugs.chromium.org/p/llvm/issues/detail?id=23 reports a new Spectre
  gadget variant that Speculative Load Hardening (SLH) does not mitigate. No
  extension to SLH was implemented to also mitigate against this variant.

* https://bugs.chromium.org/p/llvm/issues/detail?id=30 reports missing memory
  safety protection on the (C++) exception handling path. A number of fixes
  were implemented.

* https://bugs.chromium.org/p/llvm/issues/detail?id=33 reports the RETBLEED
  vulnerability. The outcome was clang growing a new security hardening feature
  `-mfunction-return=thunk-extern`, see https://reviews.llvm.org/D129572.


No dedicated LLVM releases were made for any of the above issues.

2023
----

In this section we report on the issues the group received in 2023, or on issues
that were received earlier, but were disclosed in 2023.

9 of these were judged to be security issues:

https://bugs.chromium.org/p/llvm/issues/detail?id=36 reports the presence of
.git folder in https://llvm.org/.git.

https://bugs.chromium.org/p/llvm/issues/detail?id=66 reports the presence of
a GitHub Personal Access token in a DockerHub imaage.

https://bugs.chromium.org/p/llvm/issues/detail?id=42 reports a potential gap
in the Armv8.1-m BTI protection, involving a combination of large switch statements
and __builtin_unreachable() in the default case.

https://bugs.chromium.org/p/llvm/issues/detail?id=43 reports a dependency
on an old version of xml2js with a CVE filed against it.

https://bugs.chromium.org/p/llvm/issues/detail?id=45 reports a number of
dependencies that have had vulnerabilities reported against them.

https://bugs.chromium.org/p/llvm/issues/detail?id=46 is related to issue 43.

https://bugs.chromium.org/p/llvm/issues/detail?id=48 reports a buffer overflow
in std::format from -fexperimental-library.

https://bugs.chromium.org/p/llvm/issues/detail?id=54 reports a memory leak in
basic_string move assignment when built with libc++ versions <=6.0 and run against
newer libc++ shared/dylibs.

https://bugs.chromium.org/p/llvm/issues/detail?id=56 reports an out of bounds buffer
store introduced by LLVM backends, that regressed due to a procedural oversight.

No dedicated LLVM releases were made for any of the above issues.

Over the course of 2023 we had one person join the LLVM Security Group.
