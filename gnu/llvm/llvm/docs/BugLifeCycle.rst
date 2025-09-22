===================
LLVM Bug Life Cycle
===================

.. contents::
   :local:



Introduction - Achieving consistency in how we deal with bug reports
====================================================================

We aim to achieve a basic level of consistency in how reported bugs evolve from
being reported, to being worked on, and finally getting closed out. The
consistency helps reporters, developers and others to gain a better
understanding of what a particular bug state actually means and what to expect
might happen next.

At the same time, we aim to not over-specify the life cycle of bugs in
`the LLVM Bug Tracking System <https://github.com/llvm/llvm-project/issues>`_,
as the overall goal is to make it easier to work with and understand the bug
reports.

The main parts of the life cycle documented here are:

#. `Reporting`_
#. `Triaging`_
#. `Actively working on fixing`_
#. `Closing`_

Furthermore, some of the metadata in the bug tracker, such as what labels we
use, needs to be maintained. See the following for details:

#. `Maintenance of metadata`_


.. _Reporting:

Reporting bugs
==============

See :doc:`HowToSubmitABug` on further details on how to submit good bug reports.

You can apply `labels <https://docs.github.com/en/issues/using-labels-and-milestones-to-track-work/managing-labels>`_
to the bug to provide extra information to make the bug easier to discover, such
as a label for the part of the project the bug pertains to.

.. _Triaging:

Triaging bugs
=============

Open bugs that have not been marked with the ``confirmed`` label are bugs that
still need to be triaged. When triage is complete, the ``confirmed`` label
should be added along with any other labels that help to classify the report,
unless the issue is being :ref:`closed<Closing>`.

The goal of triaging a bug is to make sure a newly reported bug ends up in a
good, actionable state. Try to answer the following questions while triaging:

* Is the reported behavior actually wrong?

  * E.g. does a miscompile example depend on undefined behavior?

* Can you reproduce the bug from the details in the report?

  * If not, is there a reasonable explanation why it cannot be reproduced?

* Is it related to an already reported bug?

* Are the following fields filled in correctly?

  * Title
  * Description
  * Labels

* When able to do so, please add the appropriate labels to classify the bug,
  such as the tool (``clang``, ``clang-format``, ``clang-tidy``, etc) or
  component (``backend:<name>``, ``compiler-rt:<name>``, ``clang:<name>``, etc).

* If the issue is with a particular revision of the C or C++ standard, please
  add the appropriate language standard label (``c++20``, ``c99``, etc).

* Please don't use both a general and a specific label. For example, bugs
  labeled ``c++17`` shouldn't also have ``c++``, and bugs labeled
  ``clang:codegen`` shouldn't also have ``clang``.

* Add the ``good first issue`` label if you think this would be a good bug to
  be fixed by someone new to LLVM. This label feeds into `the landing page
  for new contributors <https://github.com/llvm/llvm-project/contribute>`_.

* If you are unsure of what a label is intended to be used for, please see the
  `documentation for our labels <https://github.com/llvm/llvm-project/labels>`_.

.. _Actively working on fixing:

Actively working on fixing bugs
===============================

Please remember to assign the bug to yourself if you're actively working on
fixing it and to unassign it when you're no longer actively working on it.  You
unassign a bug by removing the person from the ``Assignees`` field.

.. _Closing:

Resolving/Closing bugs
======================

Resolving bugs is good! Make sure to properly record the reason for resolving.
Examples of reasons for resolving are:

  * If the issue has been resolved by a particular commit, close the issue with
    a brief comment mentioning which commit(s) fixed it. If you are authoring
    the fix yourself, your git commit message may include the phrase
    ``Fixes #<issue number>`` on a line by itself. GitHub recognizes such commit
    messages and will automatically close the specified issue with a reference
    to your commit.

  * If the reported behavior is not a bug, it is appropriate to close the issue
    with a comment explaining why you believe it is not a bug, and adding the
    ``invalid`` tag.

  * If the bug duplicates another issue, close it as a duplicate by adding the
    ``duplicate`` label with a comment pointing to the issue it duplicates.

  * If there is a sound reason for not fixing the issue (difficulty, ABI, open
    research questions, etc), add the ``wontfix`` label and a comment explaining
    why no changes are expected.

  * If there is a specific and plausible reason to think that a given bug is
    otherwise inapplicable or obsolete. One example is an open bug that doesn't
    contain enough information to clearly understand the problem being reported
    (e.g. not reproducible). It is fine to close such a bug, adding with the
    ``worksforme`` label and leaving a comment to encourage the reporter to
    reopen the bug with more information if it's still reproducible for them.


.. _Maintenance of metadata:

Maintenance of metadata
=======================

Project member with write access to the project can create new labels, but we
discourage adding ad hoc labels because we want to control the proliferation of
labels and avoid single-use labels. If you would like a new label added, please
open an issue asking to create an issue label and add the ``infrastructure``
label to the issue. The request should include a description of what the label
is for. Alternatively, you can ask for the label to be created on the
``#infrastructure`` channel on the LLVM Discord.
