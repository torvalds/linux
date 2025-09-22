===================
Bisecting LLVM code
===================

Introduction
============

``git bisect`` is a useful tool for finding which revision caused a bug.

This document describes how to use ``git bisect``. In particular, while LLVM
has a mostly linear history, it has a few merge commits that added projects --
and these merged the linear history of those projects. As a consequence, the
LLVM repository has multiple roots: One "normal" root, and then one for each
toplevel project that was developed out-of-tree and then merged later.
As of early 2020, the only such merged project is MLIR, but flang will likely
be merged in a similar way soon.

Basic operation
===============

See https://git-scm.com/docs/git-bisect for a good overview. In summary:

  .. code-block:: bash

     git bisect start
     git bisect bad main
     git bisect good f00ba

git will check out a revision in between. Try to reproduce your problem at
that revision, and run ``git bisect good`` or ``git bisect bad``.

If you can't repro at the current commit (maybe the build is broken), run
``git bisect skip`` and git will pick a nearby alternate commit.

(To abort a bisect, run ``git bisect reset``, and if git complains about not
being able to reset, do the usual ``git checkout -f main; git reset --hard
origin/main`` dance and try again).

``git bisect run``
==================

A single bisect step often requires first building clang, and then compiling
a large code base with just-built clang. This can take a long time, so it's
good if it can happen completely automatically. ``git bisect run`` can do
this for you if you write a run script that reproduces the problem
automatically. Writing the script can take 10-20 minutes, but it's almost
always worth it -- you can do something else while the bisect runs (such
as writing this document).

Here's an example run script. It assumes that you're in ``llvm-project`` and
that you have a sibling ``llvm-build-project`` build directory where you
configured CMake to use Ninja. You have a file ``repro.c`` in the current
directory that makes clang crash at trunk, but it worked fine at revision
``f00ba``.

  .. code-block:: bash

     # Build clang. If the build fails, `exit 125` causes this
     # revision to be skipped
     ninja -C ../llvm-build-project clang || exit 125

     ../llvm-build-project/bin/clang repro.c

To make sure your run script works, it's a good idea to run ``./run.sh`` by
hand and tweak the script until it works, then run ``git bisect good`` or
``git bisect bad`` manually once based on the result of the script
(check ``echo $?`` after your script ran), and only then run ``git bisect run
./run.sh``. Don't forget to mark your run script as executable -- ``git bisect
run`` doesn't check for that, it just assumes the run script failed each time.

Once your run script works, run ``git bisect run ./run.sh`` and a few hours
later you'll know which commit caused the regression.

(This is a very simple run script. Often, you want to use just-built clang
to build a different project and then run a built executable of that project
in the run script.)

Bisecting across multiple roots
===============================

Here's how LLVM's history currently looks:

  .. code-block:: none

     A-o-o-......-o-D-o-o-HEAD
                   /
       B-o-...-o-C-

``A`` is the first commit in LLVM ever, ``97724f18c79c``.

``B`` is the first commit in MLIR, ``aed0d21a62db``.

``D`` is the merge commit that merged MLIR into the main LLVM repository,
``0f0d0ed1c78f``.

``C`` is the last commit in MLIR before it got merged, ``0f0d0ed1c78f^2``. (The
``^n`` modifier selects the n'th parent of a merge commit.)

``git bisect`` goes through all parent revisions. Due to the way MLIR was
merged, at every revision at ``C`` or earlier, *only* the ``mlir/`` directory
exists, and nothing else does.

As of early 2020, there is no flag to ``git bisect`` to tell it to not
descend into all reachable commits. Ideally, we'd want to tell it to only
follow the first parent of ``D``.

The best workaround is to pass a list of directories to ``git bisect``:
If you know the bug is due to a change in llvm, clang, or compiler-rt, use

  .. code-block:: bash

     git bisect start -- clang llvm compiler-rt

That way, the commits in ``mlir`` are never evaluated.

Alternatively, ``git bisect skip aed0d21a6 aed0d21a6..0f0d0ed1c78f`` explicitly
skips all commits on that branch. It takes 1.5 minutes to run on a fast
machine, and makes ``git bisect log`` output unreadable. (``aed0d21a6`` is
listed twice because git ranges exclude the revision listed on the left,
so it needs to be ignored explicitly.)

More Resources
==============

https://git-scm.com/book/en/v2/Git-Tools-Revision-Selection
