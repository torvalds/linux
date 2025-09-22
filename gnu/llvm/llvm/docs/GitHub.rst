.. _github-reviews:

======================
LLVM GitHub User Guide
======================

Introduction
============
The LLVM Project uses `GitHub <https://github.com/>`_ for
`Source Code <https://github.com/llvm/llvm-project>`_,
`Releases <https://github.com/llvm/llvm-project/releases>`_,
`Issue Tracking <https://github.com/llvm/llvm-project/issues>`_., and
`Code Reviews <https://github.com/llvm/llvm-project/pulls>`_.

This page describes how the LLVM Project users and developers can
participate in the project using GitHub.

Branches
========

It is possible to create branches that starts with `users/<username>/`, however this is
intended to be able to support "stacked" pull-request. Do not create any branches in the
llvm/llvm-project repository otherwise, please use a fork (see below). User branches that
aren't associated with a pull-request **will be deleted**.

Pull Requests
=============
The LLVM project is using GitHub Pull Requests for Code Reviews. This document
describes the typical workflow of creating a Pull Request and getting it reviewed
and accepted. This is meant as an overview of the GitHub workflow, for complete
documentation refer to `GitHub's documentation <https://docs.github.com/pull-requests>`_.

.. note::
   If you are using a Pull Request for purposes other than review
   (eg: precommit CI results, convenient web-based reverts, etc)
   `skip-precommit-approval <https://github.com/llvm/llvm-project/labels?q=skip-precommit-approval>`_
   label to the PR.

GitHub Tools
------------
You can interact with GitHub in several ways: via git command line tools,
the web browser, `GitHub Desktop <https://desktop.github.com/>`_, or the
`GitHub CLI <https://cli.github.com>`_. This guide will cover the git command line
tools and the GitHub CLI. The GitHub CLI (`gh`) will be most like the `arc` workflow and
recommended.

Creating Pull Requests
----------------------
Keep in mind that when creating a pull request, it should generally only contain one
self-contained commit initially.
This makes it easier for reviewers to understand the introduced changes and
provide feedback. It also helps maintain a clear and organized commit history
for the project. If you have multiple changes you want to introduce, it's
recommended to create separate pull requests for each change.

Create a local branch per commit you want to submit and then push that branch
to your `fork <https://docs.github.com/en/pull-requests/collaborating-with-pull-requests/working-with-forks>`_
of the llvm-project and
`create a pull request from the fork <https://docs.github.com/en/pull-requests/collaborating-with-pull-requests/proposing-changes-to-your-work-with-pull-requests/creating-a-pull-request-from-a-fork>`_.
As GitHub uses the first line of the commit message truncated to 72 characters
as the pull request title, you may have to edit to reword or to undo this
truncation.

Creating Pull Requests with GitHub CLI
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
With the CLI it's enough to create the branch locally and then run:

::

  gh pr create

When prompted select to create and use your own fork and follow
the instructions to add more information needed.

.. note::

  When you let the GitHub CLI create a fork of llvm-project to
  your user, it will change the git "remotes" so that "origin" points
  to your fork and "upstream" points to the main llvm-project repository.

Updating Pull Requests
----------------------
In order to update your pull request, the only thing you need to do is to push
your new commits to the branch in your fork. That will automatically update
the pull request.

When updating a pull request, you should push additional "fix up" commits to
your branch instead of force pushing. This makes it easier for GitHub to
track the context of previous review comments. Consider using the
`built-in support for fixups <https://git-scm.com/docs/git-commit#Documentation/git-commit.txt---fixupamendrewordltcommitgt>`_
in git.

If you do this, you must squash and merge before landing the PR and
you must use the pull request title and description as the commit message.
You can do this manually with an interactive git rebase or with GitHub's
built-in tool. See the section about landing your fix below.

When pushing to your branch, make sure you push to the correct fork. Check your
remotes with:

::

  git remote -v

And make sure you push to the remote that's pointing to your fork.

Rebasing Pull Requests and Force Pushes
---------------------------------------
In general, you should avoid rebasing a Pull Request and force pushing to the
branch that's the root of the Pull Request during the review. This action will
make the context of the old changes and comments harder to find and read.

Sometimes, a rebase might be needed to update your branch with a fix for a test
or in some dependent code.

After your PR is reviewed and accepted, you want to rebase your branch to ensure
you won't encounter merge conflicts when landing the PR.

Landing your change
-------------------
When your PR has been accepted you can use the web interface to land your change.
If you have created multiple commits to address feedback at this point you need
to consolidate those commits into one commit. There are two different ways to
do this:

`Interactive rebase <https://git-scm.com/docs/git-rebase#_interactive_mode>`_
with fixup's. This is the recommended method since you can control the final
commit message and inspect that the final commit looks as you expect. When
your local state is correct, remember to force-push to your branch and press
the merge button afterwards.

Use the button `Squash and merge` in GitHub's web interface, if you do this
remember to review the commit message when prompted.

Afterwards you can select the option `Delete branch` to delete the branch
from your fork.

You can also merge via the CLI by switching to your branch locally and run:

::

  gh pr merge --squash --delete-branch

If you observe an error message from the above informing you that your pull
request is not mergeable, then that is likely because upstream has been
modified since your pull request was authored in a way that now results in a
merge conflict. You must first resolve this merge conflict in order to merge
your pull request. In order to do that:

::

  git fetch upstream
  git rebase upstream/main

Then fix the source files causing merge conflicts and make sure to rebuild and
retest the result. Then:

::

  git add <files with resolved merge conflicts>
  git rebase --continue

Finally, you'll need to force push to your branch one more time before you can
merge:

::

  git push -f
  gh pr merge --squash --delete-branch

This force push may ask if you intend to push hundreds, or potentially
thousands of patches (depending on how long it's been since your pull request
was initially authored vs. when you intended to merge it). Since you're pushing
to a branch in your fork, this is ok and expected. Github's UI for the pull
request will understand that you're rebasing just your patches, and display
this result correctly with a note that a force push did occur.


Problems After Landing Your Change
==================================

Even though your PR passed the pre-commit checks and is approved by reviewers, it
may cause problems for some configurations after it lands. You will be notified
if this happens and the community is ready to help you fix the problems.

This process is described in detail
:ref:`here <MyFirstTypoFix Issues After Landing Your PR>`.


Checking out another PR locally
-------------------------------
Sometimes you want to review another person's PR on your local machine to run
tests or inspect code in your preferred editor. This is easily done with the
CLI:

::

  gh pr checkout <PR Number>

This is also possible with the web interface and the normal git command line
tools, but the process is a bit more complicated. See GitHub's
`documentation <https://docs.github.com/en/pull-requests/collaborating-with-pull-requests/reviewing-changes-in-pull-requests/checking-out-pull-requests-locally?platform=linux&tool=webui#modifying-an-inactive-pull-request-locally>`_
on the topic.

Example Pull Request with GitHub CLI
====================================
Here is an example for creating a Pull Request with the GitHub CLI:

::

  # Clone the repo
  gh repo clone llvm/llvm-project

  # Switch to the repo and create a new branch
  cd llvm-project
  git switch -c my_change

  # Create your changes
  $EDITOR file.cpp

  # Don't forget clang-format
  git clang-format

  # and don't forget running your tests
  ninja check-llvm

  # Commit, use a good commit message
  git commit file.cpp

  # Create the PR, select to use your own fork when prompted.
  # If you don't have a fork, gh will create one for you.
  gh pr create

  # If you get any review comments, come back to the branch and
  # adjust them.
  git switch my_change
  $EDITOR file.cpp

  # Commit your changes
  git commit file.cpp -m "Code Review adjustments"

  # Format changes
  git clang-format HEAD~

  # Recommit if any formatting changes
  git commit -a --amend

  # Push your changes to your fork branch, be mindful of
  # your remotes here, if you don't remember what points to your
  # fork, use git remote -v to see. Usually origin points to your
  # fork and upstream to llvm/llvm-project
  git push origin my_change

Before merging the PR, it is recommended that you rebase locally and re-run test
checks:

::

  # Add upstream as a remote (if you don't have it already)
  git remote add upstream https://github.com/llvm/llvm-project.git

  # Make sure you have all the latest changes
  git fetch upstream && git rebase -i upstream/main

  # Make sure tests pass with latest changes and your change
  ninja check

  # Push the rebased changes to your fork.
  git push origin my_change -f

  # Now merge it
  gh pr merge --squash --delete-branch


See more in-depth information about how to contribute in the following documentation:

* :doc:`Contributing`
* :doc:`MyFirstTypoFix`

Example Pull Request with git
====================================

Instead of using the GitHub CLI to create a PR, you can push your code to a
remote branch on your fork and create the PR to upstream using the GitHub web
interface.

Here is an example of making a PR using git and the GitHub web interface:

First follow the instructions to [fork the repository](https://docs.github.com/en/get-started/quickstart/fork-a-repo?tool=webui#forking-a-repository).

Next follow the instructions to [clone your forked repository](https://docs.github.com/en/get-started/quickstart/fork-a-repo?tool=webui#cloning-your-forked-repository).

Once you've cloned your forked repository,

::

  # Switch to the forked repo
  cd llvm-project

  # Create a new branch
  git switch -c my_change

  # Create your changes
  $EDITOR file.cpp

  # Don't forget clang-format
  git clang-format

  # and don't forget running your tests
  ninja check-llvm

  # Commit, use a good commit message
  git commit file.cpp

  # Push your changes to your fork branch, be mindful of
  # your remotes here, if you don't remember what points to your
  # fork, use git remote -v to see. Usually origin points to your
  # fork and upstream to llvm/llvm-project
  git push origin my_change

Navigate to the URL printed to the console from the git push command in the last step.
Create a pull request from your branch to llvm::main.

::

  # If you get any review comments, come back to the branch and
  # adjust them.
  git switch my_change
  $EDITOR file.cpp

  # Commit your changes
  git commit file.cpp -m "Code Review adjustments"

  # Format changes
  git clang-format HEAD~

  # Recommit if any formatting changes
  git commit -a --amend

  # Re-run tests and make sure nothing broke.
  ninja check

  # Push your changes to your fork branch, be mindful of
  # your remotes here, if you don't remember what points to your
  # fork, use git remote -v to see. Usually origin points to your
  # fork and upstream to llvm/llvm-project
  git push origin my_change

Before merging the PR, it is recommended that you rebase locally and re-run test
checks:

::

  # Add upstream as a remote (if you don't have it already)
  git remote add upstream https://github.com/llvm/llvm-project.git

  # Make sure you have all the latest changes
  git fetch upstream && git rebase -i upstream/main

  # Make sure tests pass with latest changes and your change
  ninja check

  # Push the rebased changes to your fork.
  git push origin my_change -f

Once your PR is approved, rebased, and tests are passing, click `Squash and
Merge` on your PR in the GitHub web interface.

See more in-depth information about how to contribute in the following documentation:

* :doc:`Contributing`
* :doc:`MyFirstTypoFix`

Releases
========

Backporting Fixes to the Release Branches
-----------------------------------------
You can use special comments on issues to make backport requests for the
release branches.  This is done by making a comment containing the following
command on any issue that has been added to one of the "X.Y.Z Release"
milestones.

::

  /cherry-pick <commit> <commit> <...>

This command takes one or more git commit hashes as arguments and will attempt
to cherry-pick the commit(s) to the release branch.  If the commit(s) fail to
apply cleanly, then a comment with a link to the failing job will be added to
the issue.  If the commit(s) do apply cleanly, then a pull request will
be created with the specified commits.

If a commit you want to backport does not apply cleanly, you may resolve
the conflicts locally and then create a pull request against the release
branch.  Just make sure to add the release milestone to the pull request.
