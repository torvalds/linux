==============
MyFirstTypoFix
==============

.. contents::
   :local:

Introduction
============

This tutorial will guide you through the process of making a change to
LLVM, and contributing it back to the LLVM project.

.. note::
   The code changes presented here are only an example and not something you
   should actually submit to the LLVM project. For your first real change to LLVM,
   the code will be different but the rest of the guide will still apply.

We'll be making a change to Clang, but the steps for other parts of LLVM are the same.
Even though the change we'll be making is simple, we're going to cover
steps like building LLVM, running the tests, and code review. This is
good practice, and you'll be prepared for making larger changes.

We'll assume you:

-  know how to use an editor,

-  have basic C++ knowledge,

-  know how to install software on your system,

-  are comfortable with the command line,

-  have basic knowledge of git.


The change we're making
-----------------------

Clang has a warning for infinite recursion:

.. code:: console

   $ echo "void foo() { foo(); }" > ~/test.cc
   $ clang -c -Wall ~/test.cc
   test.cc:1:12: warning: all paths through this function will call itself [-Winfinite-recursion]

This is clear enough, but not exactly catchy. Let's improve the wording
a little:

.. code:: console

   test.cc:1:12: warning: to understand recursion, you must first understand recursion [-Winfinite-recursion]


Dependencies
------------

We're going to need some tools:

-  git: to check out the LLVM source code,

-  a C++ compiler: to compile LLVM source code. You'll want `a recent
   version <host_cpp_toolchain>` of Clang, GCC, or Visual Studio.

-  CMake: used to configure how LLVM should be built on your system,

-  ninja: runs the C++ compiler to (re)build specific parts of LLVM,

-  python: to run the LLVM tests.

As an example, on Ubuntu:

.. code:: console

   $ sudo apt-get install git clang cmake ninja-build python


Building LLVM
=============


Checkout
--------

The source code is stored `on
Github <https://github.com/llvm/llvm-project>`__ in one large repository
("the monorepo").

It may take a while to download!

.. code:: console

   $ git clone https://github.com/llvm/llvm-project.git

This will create a directory "llvm-project" with all of the source
code. (Checking out anonymously is OK - pushing commits uses a different
mechanism, as we'll see later.)

Configure your workspace
------------------------

Before we can build the code, we must configure exactly how to build it
by running CMake. CMake combines information from three sources:

-  explicit choices you make (is this a debug build?)

-  settings detected from your system (where are libraries installed?)

-  project structure (which files are part of 'clang'?)

First, create a directory to build in. Usually, this is ``llvm-project/build``.

.. code:: console

   $ mkdir llvm-project/build
   $ cd llvm-project/build

Now, run CMake:

.. code:: console

   $ cmake -G Ninja ../llvm -DCMAKE_BUILD_TYPE=Release -DLLVM_ENABLE_PROJECTS=clang

If all goes well, you'll see a lot of "performing test" lines, and
finally:

.. code:: console

   Configuring done
   Generating done
   Build files have been written to: /path/llvm-project/build

And you should see a ``build.ninja`` file in the current directory.

Let's break down that last command a little:

-  **-G Ninja**: Tells CMake that we're going to use ninja to build, and to create
   the ``build.ninja`` file.

-  **../llvm**: this is the path to the source of the "main" LLVM
   project

-  The two **-D** flags set CMake variables, which override
   CMake/project defaults:

    -  **CMAKE_BUILD_TYPE=Release**: build in optimized mode, which is
       (surprisingly) the fastest option.

       If you want to run under a debugger, you should use the default Debug
       (which is totally unoptimized, and will lead to >10x slower test
       runs) or RelWithDebInfo which is a halfway point.

       Assertions are not enabled in ``Release`` builds by default.
       You can enable them using ``LLVM_ENABLE_ASSERTIONS=ON``.

    -  **LLVM_ENABLE_PROJECTS=clang**: this lists the LLVM subprojects
       you are interested in building, in addition to LLVM itself. Multiple
       projects can be listed, separated by semicolons, such as ``clang;lldb``.
       In this example, we'll be making a change to Clang, so we only add clang.

Finally, create a symlink (or copy) of ``llvm-project/build/compile-commands.json``
into ``llvm-project/``:

.. code:: console

   $ ln -s build/compile_commands.json ../

(This isn't strictly necessary for building and testing, but allows
tools like clang-tidy, clang-query, and clangd to work in your source
tree).


Build and test
--------------

Finally, we can build the code! It's important to do this first, to
ensure we're in a good state before making changes. But what to build?
In ninja, you specify a **target**. If we just want to build the clang
binary, our target name is "clang" and we run:

.. code:: console

   $ ninja clang

The first time we build will be very slow - Clang + LLVM is a lot of
code. But incremental builds are fast: ninja will only rebuild the parts
that have changed. When it finally finishes you should have a working
clang binary. Try running:

.. code:: console

   $ bin/clang --version

There's also a target for building and running all the clang tests:

.. code:: console

   $ ninja check-clang

This is a common pattern in LLVM: check-llvm is all the checks for the core of
LLVM, other projects have targets like ``check-lldb``, ``check-flang`` and so on.


Making changes
==============


The Change
----------

We need to find the file containing the error message.

.. code:: console

   $ git grep "all paths through this function" ..
   ../clang/include/clang/Basic/DiagnosticSemaKinds.td:  "all paths through this function will call itself">,

The string that appears in ``DiagnosticSemaKinds.td`` is the one that is
printed by Clang. ``*.td`` files define tables - in this case it's a list
of warnings and errors clang can emit and their messages. Let's update
the message in your favorite editor:

.. code:: console

   $ vi ../clang/include/clang/Basic/DiagnosticSemaKinds.td

Find the message (it should be under ``warn_infinite_recursive_function``).
Change the message to "in order to understand recursion, you must first understand recursion".


Test again
----------

To verify our change, we can build clang and manually check that it
works.

.. code:: console

   $ ninja clang
   $ bin/clang -c -Wall ~/test.cc
   test.cc:1:12: warning: in order to understand recursion, you must first understand recursion [-Winfinite-recursion]

We should also run the tests to make sure we didn't break something.

.. code:: console

   $ ninja check-clang

Notice that it is much faster to build this time, but the tests take
just as long to run. Ninja doesn't know which tests might be affected,
so it runs them all.

.. code:: console

   ********************
   Failing Tests (1):
       Clang :: SemaCXX/warn-infinite-recursion.cpp

Well, that makes sense… and the test output suggests it's looking for
the old string "call itself" and finding our new message instead.
Note that more tests may fail in a similar way as new tests are added
over time.

Let's fix it by updating the expectation in the test.

.. code:: console

   $ vi ../clang/test/SemaCXX/warn-infinite-recursion.cpp

Everywhere we see ``// expected-warning{{call itself}}`` (or something similar
from the original warning text), let's replace it with
``// expected-warning{{to understand recursion}}``.

Now we could run **all** the tests again, but this is a slow way to
iterate on a change! Instead, let's find a way to re-run just the
specific test. There are two main types of tests in LLVM:

-  **lit tests** (e.g. ``SemaCXX/warn-infinite-recursion.cpp``).

These are fancy shell scripts that run command-line tools and verify the
output. They live in files like
``clang/**test**/FixIt/dereference-addressof.c``. Re-run like this:

.. code:: console

   $ bin/llvm-lit -v ../clang/test/SemaCXX/warn-infinite-recursion.cpp

-  **unit tests** (e.g. ``ToolingTests/ReplacementTest.CanDeleteAllText``)

These are C++ programs that call LLVM functions and verify the results.
They live in suites like ToolingTests. Re-run like this:

.. code:: console

   $ ninja ToolingTests && tools/clang/unittests/Tooling/ToolingTests --gtest_filter=ReplacementTest.CanDeleteAllText


Commit locally
--------------

We'll save the change to a local git branch. This lets us work on other
things while the change is being reviewed. Changes should have a
title and description, to explain to reviewers and future readers of the code why
the change was made.

For now, we'll only add a title.

.. code:: console

   $ git checkout -b myfirstpatch
   $ git commit -am "[clang][Diagnostic] Clarify -Winfinite-recursion message"

Now we're ready to send this change out into the world!

The ``[clang]`` and ``[Diagnostic]`` prefixes are what we call tags. This loose convention
tells readers of the git log what areas a change is modifying. If you don't know
the tags for the modules you've changed, you can look at the commit history
for those areas of the repository.

.. code:: console

   $ git log --oneline ../clang/

Or using GitHub, for example https://github.com/llvm/llvm-project/commits/main/clang.

Tagging is imprecise, so don't worry if you are not sure what to put. Reviewers
will suggest some if they think they are needed.

Code review
===========

Uploading a change for review
-----------------------------

LLVM code reviews happen through pull-request on GitHub, see the
:ref:`GitHub <github-reviews>` documentation for how to open
a pull-request on GitHub.

Finding a reviewer
------------------

Changes can be reviewed by anyone in the LLVM community. For larger and more
complicated changes, it's important that the
reviewer has experience with the area of LLVM and knows the design goals
well. The author of a change will often assign a specific reviewer. ``git blame``
and ``git log`` can be useful to find previous authors who can review.

Our GitHub bot will also tag and notify various "teams" around LLVM. The
team members contribute and review code for those specific areas regularly,
so one of them will review your change if you don't pick anyone specific.

Review process
--------------

When you open a pull-request, some automation will add a comment and
notify different members of the sub-projects depending on the parts you have
changed.

Within a few days, someone should start the review. They may add
themselves as a reviewer, or simply start leaving comments. You'll get
another email any time the review is updated. For more detail see the
:ref:`Code Review Poilicy <code_review_policy>`.

Comments
~~~~~~~~

The reviewer can leave comments on the change, and you can reply. Some
comments are attached to specific lines, and appear interleaved with the
code. You can reply to these. Perhaps to clarify what was asked or to tell the
reviewer that you have done what was asked.

Updating your change
~~~~~~~~~~~~~~~~~~~~

If you make changes in response to a reviewer's comments, simply update
your branch with more commits and push to your GitHub fork of ``llvm-project``.
It is best if you answer comments from the reviewer directly instead of expecting
them to read through all the changes again.

For example you might comment "I have done this." or "I was able to this part
but have a question about...".

Review expectations
-------------------

In order to make LLVM a long-term sustainable effort, code needs to be
maintainable and well tested. Code reviews help to achieve that goal.
Especially for new contributors, that often means many rounds of reviews
and push-back on design decisions that do not fit well within the
overall architecture of the project.

For your first patches, this means:

-  be kind, and expect reviewers to be kind in return - LLVM has a
   :ref:`Code of Conduct <LLVM Community Code of Conduct>`
   that everyone should be following;

-  be patient - understanding how a new feature fits into the
   architecture of the project is often a time consuming effort, and
   people have to juggle this with other responsibilities in their
   lives; **ping the review once a week** when there is no response;

-  if you can't agree, generally the best way is to do what the reviewer
   asks; we optimize for readability of the code, which the reviewer is
   in a better position to judge; if this feels like it's not the right
   option, you can ask them in a comment or add another reviewer to get a second
   opinion.


Accepting a pull request
------------------------

When the reviewer is happy with the change, they will **Approve** the
pull request. They may leave some more minor comments that you should
address before it is merged, but at this point the review is complete.
It's time to get it merged!


Commit access
=============

Commit by proxy
---------------

As this is your first change, you won't have access to merge it
yourself yet. The reviewer **does not know this**, so you need to tell
them! Leave a comment on the review like:

   Thanks @<username of reviewer>. I don't have commit access, can you merge this
   PR for me?

The pull-request will be closed and you will be notified by GitHub.

Getting commit access
---------------------

Once you've contributed a handful of patches to LLVM, start to think
about getting commit access yourself. It's probably a good idea if:

-  you've landed 3-5 patches of larger scope than "fix a typo"

-  you'd be willing to review changes that are closely related to yours

-  you'd like to keep contributing to LLVM.


The process is described in the :ref:`developer policy document <obtaining_commit_access>`.

With great power
----------------

Actually, this would be a great time to read the rest of the :ref:`developer
policy <developer_policy>` too.


.. _MyFirstTypoFix Issues After Landing Your PR:

Issues After Landing Your PR
============================

Once your change is submitted it will be picked up by automated build
bots that will build and test your patch in a variety of configurations.

The "console" view at http://lab.llvm.org/buildbot/#/console displays results
for specific commits. If you want to follow how your change is affecting the
build bots, this should be the first place to look.

The columns are build configurations and the rows are individual commits. Along
the rows are colored bubbles. The color of the bubble represents the build's
status. Green is passing, red has failed and yellow is a build in progress.

A red build may have already been failing before your change was committed. This
means you didn't break the build but you should check that you did not make it
any worse by adding new problems.

.. note::
   Only recent changes are shown in the console view. If your change is not
   there, rely on PR comments and build bot emails to notify you of any problems.

If there is a problem in a build that includes your changes, you may receive
a report by email or as a comment on your PR. Please check whether the problem
has been caused by your changes specifically. As builds contain changes from
many authors and sometimes fail due to unrelated infrastructure problems.

To see the details of a build, click the bubble in the console view, or the link
provided in the problem report. You will be able to view and download logs for
each stage of that build.

If you need help understanding the problem, or have any other questions, you can
ask them as a comment on your PR, or on `Discord <https://discord.com/invite/xS7Z362>`__.

If you do not receive any reports of problems, no action is required from you.
Your changes are working as expected, well done!

Reverts
-------

If your change has caused a problem, it should be reverted as soon as possible.
This is a normal part of :ref:`LLVM development <revert_policy>`,
that every committer (no matter how experienced) goes through.

If you are in any doubt whether your change can be fixed quickly, revert it.
Then you have plenty of time to investigate and produce a solid fix.

Someone else may revert your change for you, or you can create a revert pull request using
the `GitHub interface <https://docs.github.com/en/pull-requests/collaborating-with-pull-requests/incorporating-changes-from-a-pull-request/reverting-a-pull-request>`__.
Add your original reviewers to this new pull request if possible.

Conclusion
==========

Now you should have an understanding of the life cycle of a contribution to the
LLVM Project.

If some details are still unclear, do not worry. The LLVM Project's process does
differ from what you may be used to elsewhere on GitHub. Within the project
the expectations of different sub-projects may vary too.

So whatever you are contributing to, know that we are not expecting perfection.
Please ask questions whenever you are unsure, and expect that if you have missed
something, someone will politely point it out and help you address it.
