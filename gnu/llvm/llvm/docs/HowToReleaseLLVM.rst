=================================
How To Release LLVM To The Public
=================================

Introduction
============

This document contains information about successfully releasing LLVM ---
including sub-projects: e.g., ``clang`` and ``compiler-rt`` --- to the public.
It is the Release Manager's responsibility to ensure that a high quality build
of LLVM is released.

If you're looking for the document on how to test the release candidates and
create the binary packages, please refer to the :doc:`ReleaseProcess` instead.

.. _timeline:

Release Timeline
================

LLVM is released on a time based schedule --- with major releases roughly
every 6 months.  In between major releases there may be dot releases.
The release manager will determine if and when to make a dot release based
on feedback from the community.  Typically, dot releases should be made if
there are large number of bug-fixes in the stable branch or a critical bug
has been discovered that affects a large number of users.

Unless otherwise stated, dot releases will follow the same procedure as
major releases.

Annual Release Schedule
-----------------------

Here is the annual release schedule for LLVM.  This is meant to be a
guide, and release managers are not required to follow this exactly.
Releases should be tagged on Tuesdays.

=============================== =========================
Release                         Approx. Date
=============================== =========================
*release branch: even releases* *4th Tue in January*
*release branch: odd releases*  *4th Tue in July*
X.1.0-rc1                       3 days after branch.
X.1.0-rc2                       2 weeks after branch.
X.1.0-rc3                       4 weeks after branch
**X.1.0-final**                 **6 weeks after branch**
**X.1.1**                       **8 weeks after branch**
**X.1.2**                       **10 weeks after branch**
**X.1.3**                       **12 weeks after branch**
**X.1.4**                       **14 weeks after branch**
**X.1.5**                       **16 weeks after branch**
**X.1.6 (if necessary)**        **18 weeks after branch**
=============================== =========================

Release Process Summary
-----------------------

* Announce release schedule to the LLVM community and update the website.  Do
  this at least 3 weeks before the -rc1 release.

* Create release branch and begin release process.

* Send out release candidate sources for first round of testing.  Testing lasts
  6 weeks.  During the first round of testing, any regressions found should be
  fixed.  Patches are merged from mainline into the release branch.  Also, all
  features need to be completed during this time.  Any features not completed at
  the end of the first round of testing will be removed or disabled for the
  release.

* Generate and send out the second release candidate sources.  Only *critical*
  bugs found during this testing phase will be fixed.  Any bugs introduced by
  merged patches will be fixed.  If so a third round of testing is needed.

* The release notes are updated.

* Finally, release!

* Announce bug fix release schedule to the LLVM community and update the website.

* Do bug-fix releases every two weeks until X.1.5 or X.1.6 (if necessary).

Release Process
===============

.. contents::
   :local:

Release Administrative Tasks
----------------------------

This section describes a few administrative tasks that need to be done for the
release process to begin.  Specifically, it involves:

* Updating version numbers,

* Creating the release branch, and

* Tagging release candidates for the release team to begin testing.

Create Release Branch
^^^^^^^^^^^^^^^^^^^^^

Branch the Git trunk using the following procedure:

#. Remind developers that the release branching is imminent and to refrain from
   committing patches that might break the build.  E.g., new features, large
   patches for works in progress, an overhaul of the type system, an exciting
   new TableGen feature, etc.

#. Verify that the current git trunk is in decent shape by
   examining nightly tester and buildbot results.

#. Bump the version in trunk to N.0.0git and tag the commit with llvmorg-N-init.
   If ``X`` is the version to be released, then ``N`` is ``X + 1``.

::

  $ git tag -sa llvmorg-N-init

#. Clear the release notes in trunk.

#. Create the release branch from the last known good revision from before the
   version bump.  The branch's name is release/X.x where ``X`` is the major version
   number and ``x`` is just the letter ``x``.

#. On the newly-created release branch, immediately bump the version
   to X.1.0git (where ``X`` is the major version of the branch.)

#. All tags and branches need to be created in both the llvm/llvm-project and
   llvm/llvm-test-suite repos.

Update LLVM Version
^^^^^^^^^^^^^^^^^^^

After creating the LLVM release branch, update the release branches'
version with the script in ``llvm/utils/release/bump-version.py``.

Tagging the LLVM Release Candidates
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

Tag release candidates:

::

  $ git tag -sa llvmorg-X.Y.Z-rcN

The pre-packaged source tarballs will be automatically generated via the
"Release Sources" workflow on GitHub.  This workflow will create an artifact
containing all the release tarballs and the artifact attestation.  The
Release Manager should download the artifact, verify the tarballs, sign them,
and then upload them to the release page.

::

  $ unzip artifact.zip
  $ gh auth login
  $ for f in *.xz; do gh attestation verify --owner llvm $f && gpg -b $f; done

Tarballs, release binaries,  or any other release artifacts must be uploaded to
GitHub.  This can be done using the github-upload-release.py script in utils/release.

::

  $ github-upload-release.py upload --token <github-token> --release X.Y.Z-rcN --files <release_files>


Build The Binary Distribution
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

Creating the binary distribution requires following the instructions
:doc:`here <ReleaseProcess>`.

That process will perform both Release+Asserts and Release builds but only
pack the Release build for upload. You should use the Release+Asserts sysroot,
normally under ``final/Phase3/Release+Asserts/llvmCore-3.8.1-RCn.install/``,
for test-suite and run-time benchmarks, to make sure nothing serious has
passed through the net. For compile-time benchmarks, use the Release version.

The minimum required version of the tools you'll need are :doc:`here <GettingStarted>`

Release Qualification Criteria
------------------------------

There are no official release qualification criteria.  It is up to the
the release manager to determine when a release is ready.  The release manager
should pay attention to the results of community testing, the number of outstanding
bugs, and then number of regressions when determining whether or not to make a
release.

The community values time based releases, so releases should not be delayed for
too long unless there are critical issues remaining.  In most cases, the only
kind of bugs that are critical enough to block a release would be a major regression
from a previous release.

Official Testing
----------------

A few developers in the community have dedicated time to validate the release
candidates and volunteered to be the official release testers for each
architecture.

These will be the ones testing, generating and uploading the official binaries
to the server, and will be the minimum tests *necessary* for the release to
proceed.

This will obviously not cover all OSs and distributions, so additional community
validation is important. However, if community input is not reached before the
release is out, all bugs reported will have to go on the next stable release.

The official release managers are:

* Even releases: Tom Stellard (tstellar@redhat.com)
* Odd releases: Tobias Hieta (tobias@hieta.se)

The official release testers are volunteered from the community and have
consistently validated and released binaries for their targets/OSs. To contact
them, you should post on the `Discourse forums (Project
Infrastructure - Release Testers). <https://discourse.llvm.org/c/infrastructure/release-testers/66>`_

The official testers list is in the file ``RELEASE_TESTERS.TXT``, in the ``LLVM``
repository.

Community Testing
-----------------

Once all testing has been completed and appropriate bugs filed, the release
candidate tarballs are put on the website and the LLVM community is notified.

We ask that all LLVM developers test the release in any the following ways:

#. Download ``llvm-X.Y``, ``llvm-test-X.Y``, and the appropriate ``clang``
   binary.  Build LLVM.  Run ``make check`` and the full LLVM test suite (``make
   TEST=nightly report``).

#. Download ``llvm-X.Y``, ``llvm-test-X.Y``, and the ``clang`` sources.  Compile
   everything.  Run ``make check`` and the full LLVM test suite (``make
   TEST=nightly report``).

#. Download ``llvm-X.Y``, ``llvm-test-X.Y``, and the appropriate ``clang``
   binary. Build whole programs with it (ex. Chromium, Firefox, Apache) for
   your platform.

#. Download ``llvm-X.Y``, ``llvm-test-X.Y``, and the appropriate ``clang``
   binary. Build *your* programs with it and check for conformance and
   performance regressions.

#. Run the :doc:`release process <ReleaseProcess>`, if your platform is
   *different* than that which is officially supported, and report back errors
   only if they were not reported by the official release tester for that
   architecture.

We also ask that the OS distribution release managers test their packages with
the first candidate of every release, and report any *new* errors in GitHub.
If the bug can be reproduced with an unpatched upstream version of the release
candidate (as opposed to the distribution's own build), the priority should be
release blocker.

During the first round of testing, all regressions must be fixed before the
second release candidate is tagged.

In the subsequent stages, the testing is only to ensure that bug
fixes previously merged in have not created new major problems. *This is not
the time to solve additional and unrelated bugs!* If no patches are merged in,
the release is determined to be ready and the release manager may move onto the
next stage.

Reporting Regressions
---------------------

Every regression that is found during the tests (as per the criteria above),
should be filled in a bug in GitHub and added to the release milestone.

If a bug can't be reproduced, or stops being a blocker, it should be removed
from the Milestone. Debugging can continue, but on trunk.

Backport Requests
-----------------

Instructions for requesting a backport to a stable branch can be found :doc:`here <GitHub>`.

Triaging Bug Reports for Releases
---------------------------------

This section describes how to triage bug reports:

#. Search for bugs with a Release Milestone that have not been added to the
   "Release Status" github project:

   https://github.com/llvm/llvm-project/issues?q=is%3Aissue+milestone%3A%22LLVM+14.0.5+Release%22+no%3Aproject+

   Replace 14.0.5 in this query with the version from the Release Milestone being
   targeted.

   Add these bugs to the "Release Status" project.

#. Navigate to the `Release Status project <https://github.com/orgs/llvm/projects/3>`_
   to see the list of bugs that are being considered for the release.

#. Review each bug and first check if it has been fixed in main.  If it has, update
   its status to "Needs Pull Request", and create a pull request for the fix
   using the /cherry-pick or /branch comments if this has not been done already.

#. If a bug has been fixed and has a pull request created for backporting it,
   then update its status to "Needs Review" and notify a knowledgeable reviewer.
   Usually you will want to notify the person who approved the patch in Phabricator,
   but you may use your best judgement on who a good reviewer would be.  Once
   you have identified the reviewer(s), assign the issue to them and mention
   them (i.e @username) in a comment and ask them if the patch is safe to backport.
   You should also review the bug yourself to ensure that it meets the requirements
   for committing to the release branch.

#. Once a bug has been reviewed, add the release:reviewed label and update the
   issue's status to "Needs Merge".  Check the pull request associated with the
   issue.  If all the tests pass, then the pull request can be merged.  If not,
   then add a comment on the issue asking someone to take a look at the failures.

#. Once the pull request has been merged push it to the official release branch
   with the script ``llvm/utils/git/sync-release-repo.sh``.

   Then add a comment to the issue stating that the fix has been merged along with
   the git hashes from the release branch.  Add the release:merged label to the issue
   and close it.


Release Patch Rules
-------------------

Below are the rules regarding patching the release branch:

#. Patches applied to the release branch may only be applied by the release
   manager, the official release testers or the code owners with approval from
   the release manager.

#. Release managers are encouraged, but not required, to get approval from code
   owners before approving patches.  If there is no code owner or the code owner
   is unreachable then release managers can ask approval from patch reviewers or
   other developers active in that area.

#. *Before RC1* Patches should be limited to bug fixes, important optimization
   improvements, or completion of features that were started before the branch
   was created.  As with all phases, release managers and code owners can reject
   patches that are deemed too invasive.

#. *Before RC2* Patches should be limited to bug fixes or backend specific
   improvements that are determined to be very safe.

#. *Before RC3/Final Major Release* Patches should be limited to critical
   bugs or regressions.

#. *Bug fix releases* Patches should be limited to bug fixes or very safe
   and critical performance improvements.  Patches must maintain both API and
   ABI compatibility with the previous major release.


Release Final Tasks
-------------------

The final stages of the release process involves tagging the "final" release
branch, updating documentation that refers to the release, and updating the
demo page.

Update Documentation
^^^^^^^^^^^^^^^^^^^^

Review the documentation in the release branch and ensure that it is up
to date.  The "Release Notes" must be updated to reflect new features, bug
fixes, new known issues, and changes in the list of supported platforms.
The "Getting Started Guide" should be updated to reflect the new release
version number tag available from Subversion and changes in basic system
requirements.

.. _tag:

Tag the LLVM Final Release
^^^^^^^^^^^^^^^^^^^^^^^^^^

Tag the final release sources:

::

  $ git tag -sa llvmorg-X.Y.Z
  $ git push https://github.com/llvm/llvm-project.git llvmorg-X.Y.Z

Update the LLVM Website
^^^^^^^^^^^^^^^^^^^^^^^

The website must be updated before the release announcement is sent out.  Here
is what to do:

#. Check out the ``www-releases`` module from GitHub.

#. Create a new sub-directory ``X.Y.Z`` in the releases directory.

#. Copy and commit the ``llvm/docs`` and ``LICENSE.txt`` files into this new
   directory.

#. Update the ``releases/download.html`` file with links to the release
   binaries on GitHub.

#. Update the ``releases/index.html`` with the new release and link to release
   documentation.

#. After you push the changes to the www-releases repo, someone with admin
   access must login to prereleases-origin.llvm.org and manually pull the new
   changes into /data/www-releases/.  This is where the website is served from.

#. Finally checkout the llvm-www repo and update the main page
   (``index.html`` and sidebar) to point to the new release and release
   announcement.

Announce the Release
^^^^^^^^^^^^^^^^^^^^

Create a new post in the `Announce Category <https://discourse.llvm.org/c/announce>`_
once all the release tasks are complete.  For X.1.0 releases, make sure to include a
link to the release notes in the post.  For X.1.1+ releases, generate a changelog
using this command and add it to the post.

::

  $ git log --format="- %aN: [%s (%h)](https://github.com/llvm/llvm-project/commit/%H)" llvmorg-X.1.N-1..llvmorg-X.1.N

Once the release has been announced add a link to the announcement on the llvm
homepage (from the llvm-www repo) in the "Release Emails" section.
