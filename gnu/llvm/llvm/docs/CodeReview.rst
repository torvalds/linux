.. _code_review_policy:

=====================================
LLVM Code-Review Policy and Practices
=====================================

LLVM's code-review policy and practices help maintain high code quality across
the project. Specifically, our code review process aims to:

 * Improve readability and maintainability.
 * Improve robustness and prevent the introduction of defects.
 * Best leverage the experience of other contributors for each proposed change.
 * Help grow and develop new contributors, through mentorship by community leaders.

It is important for all contributors to understand our code-review
practices and participate in the code-review process.

General Policies
================

What Code Should Be Reviewed?
-----------------------------

All developers are required to have significant changes reviewed before they
are committed to the repository.

Must Code Be Reviewed Prior to Being Committed?
-----------------------------------------------

Code can be reviewed either before it is committed or after. We expect
significant patches to be reviewed before being committed. Smaller patches
(or patches where the developer owns the component) that meet
likely-community-consensus requirements (as apply to all patch approvals) can
be committed prior to an explicit review. In situations where there is any
uncertainty, a patch should be reviewed prior to being committed.

Please note that the developer responsible for a patch is also
responsible for making all necessary review-related changes, including
those requested during any post-commit review.

.. _post_commit_review:

Can Code Be Reviewed After It Is Committed?
-------------------------------------------

Post-commit review is encouraged, and can be accomplished using any of the
tools detailed below. There is a strong expectation that authors respond
promptly to post-commit feedback and address it. Failure to do so is cause for
the patch to be :ref:`reverted <revert_policy>`.

If a community member expresses a concern about a recent commit, and this
concern would have been significant enough to warrant a conversation during
pre-commit review (including around the need for more design discussions),
they may ask for a revert to the original author who is responsible to revert
the patch promptly. Developers often disagree, and erring on the side of the
developer asking for more review prevents any lingering disagreement over
code in the tree. This does not indicate any fault from the patch author,
this is inherent to our post-commit review practices.
Reverting a patch ensures that design discussions can happen without blocking
other development; it's entirely possible the patch will end up being reapplied
essentially as-is once concerns have been resolved.

Before being recommitted, the patch generally should undergo further review.
The community member who identified the problem is expected to engage
actively in the review. In cases where the problem is identified by a buildbot,
a community member with access to hardware similar to that on the buildbot is
expected to engage in the review.

Please note: The bar for post-commit feedback is not higher than for pre-commit
feedback. Don't delay unnecessarily in providing feedback. However, if you see
something after code has been committed about which you would have commented
pre-commit (had you noticed it earlier), please feel free to provide that
feedback at any time.

That having been said, if a substantial period of time has passed since the
original change was committed, it may be better to create a new patch to
address the issues than comment on the original commit. The original patch
author, for example, might no longer be an active contributor to the project.

What Tools Are Used for Code Review?
------------------------------------

Pre-commit code reviews are conducted on GitHub with Pull Requests. See
:ref:`GitHub <github-reviews>` documentation.

When Is an RFC Required?
------------------------

Some changes are too significant for just a code review. Changes that should
change the LLVM Language Reference (e.g., adding new target-independent
intrinsics), adding language extensions in Clang, and so on, require an RFC
(Request for Comment) email on the project's ``*-dev`` mailing list first. For
changes that promise significant impact on users and/or downstream code bases,
reviewers can request an RFC achieving consensus before proceeding with code
review. That having been said, posting initial patches can help with
discussions on an RFC.

Code-Review Workflow
====================

Code review can be an iterative process, which continues until the patch is
ready to be committed. Specifically, once a patch is sent out for review, it
needs an explicit approval before it is committed. Do not assume silent
approval, or solicit objections to a patch with a deadline.

.. note::
   If you are using a Pull Request for purposes other than review
   (eg: precommit CI results, convenient web-based reverts, etc)
   `skip-precommit-approval <https://github.com/llvm/llvm-project/labels?q=skip-precommit-approval>`_
   label to the PR.

Acknowledge All Reviewer Feedback
---------------------------------

All comments by reviewers should be acknowledged by the patch author. It is
generally expected that suggested changes will be incorporated into a future
revision of the patch unless the author and/or other reviewers can articulate a
good reason to do otherwise (and then the reviewers must agree). If a new patch
does not address all outstanding feedback, the author should explicitly state
that when providing the updated patch. When using the web-based code-review
tool, such notes can be provided in the "Diff" description (which is different
from the description of the "Differential Revision" as a whole used for the
commit message).

If you suggest changes in a code review, but don't wish the suggestion to be
interpreted this strongly, please state so explicitly.

Aim to Make Efficient Use of Everyone's Time
--------------------------------------------

Aim to limit the number of iterations in the review process. For example, when
suggesting a change, if you want the author to make a similar set of changes at
other places in the code, please explain the requested set of changes so that
the author can make all of the changes at once. If a patch will require
multiple steps prior to approval (e.g., splitting, refactoring, posting data
from specific performance tests), please explain as many of these up front as
possible. This allows the patch author and reviewers to make the most efficient
use of their time.

LGTM - How a Patch Is Accepted
------------------------------

A patch is approved to be committed when a reviewer accepts it, and this is
almost always associated with a message containing the text "LGTM" (which
stands for Looks Good To Me). Only approval from a single reviewer is required.

When providing an unqualified LGTM (approval to commit), it is the
responsibility of the reviewer to have reviewed all of the discussion and
feedback from all reviewers ensuring that all feedback has been addressed and
that all other reviewers will almost surely be satisfied with the patch being
approved. If unsure, the reviewer should provide a qualified approval, (e.g.,
"LGTM, but please wait for @someone, @someone_else"). You may also do this if
you are fairly certain that a particular community member will wish to review,
even if that person hasn't done so yet.

Note that, if a reviewer has requested a particular community member to review,
and after a week that community member has yet to respond, feel free to ping
the patch (which literally means submitting a comment on the patch with the
word, "Ping."), or alternatively, ask the original reviewer for further
suggestions.

If it is likely that others will want to review a recently-posted patch,
especially if there might be objections, but no one else has done so yet, it is
also polite to provide a qualified approval (e.g., "LGTM, but please wait for a
couple of days in case others wish to review"). If approval is received very
quickly, a patch author may also elect to wait before committing (and this is
certainly considered polite for non-trivial patches). Especially given the
global nature of our community, this waiting time should be at least 24 hours.
Please also be mindful of weekends and major holidays.

Our goal is to ensure community consensus around design decisions and
significant implementation choices, and one responsibility of a reviewer, when
providing an overall approval for a patch, is to be reasonably sure that such
consensus exists. If you're not familiar enough with the community to know,
then you shouldn't be providing final approval to commit. A reviewer providing
final approval should have commit access to the LLVM project.

Every patch should be reviewed by at least one technical expert in the areas of
the project affected by the change.

Splitting Requests and Conditional Acceptance
---------------------------------------------

Reviewers may request certain aspects of a patch to be broken out into separate
patches for independent review. Reviewers may also accept a patch
conditioned on the author providing a follow-up patch addressing some
particular issue or concern (although no committed patch should leave the
project in a broken state). Moreover, reviewers can accept a patch conditioned on
the author applying some set of minor updates prior to committing, and when
applicable, it is polite for reviewers to do so.

Don't Unintentionally Block a Review
------------------------------------

If you review a patch, but don't intend for the review process to block on your
approval, please state that explicitly. Out of courtesy, we generally wait on
committing a patch until all reviewers are satisfied, and if you don't intend
to look at the patch again in a timely fashion, please communicate that fact in
the review.

Who Can/Should Review Code?
===========================

Non-Experts Should Review Code
------------------------------

You do not need to be an expert in some area of the code base to review patches;
it's fine to ask questions about what some piece of code is doing. If it's not
clear to you what is going on, you're unlikely to be the only one. Please
remember that it is not in the long-term best interest of the community to have
components that are only understood well by a small number of people. Extra
comments and/or test cases can often help (and asking for comments in the test
cases is fine as well).

Moreover, authors are encouraged to interpret questions as a reason to reexamine
the readability of the code in question. Structural changes, or further
comments, may be appropriate.

If you're new to the LLVM community, you might also find this presentation helpful:
.. _How to Contribute to LLVM, A 2019 LLVM Developers' Meeting Presentation: https://youtu.be/C5Y977rLqpw

A good way for new contributors to increase their knowledge of the code base is
to review code. It is perfectly acceptable to review code and explicitly
defer to others for approval decisions.

Experts Should Review Code
--------------------------

If you are an expert in an area of the compiler affected by a proposed patch,
then you are highly encouraged to review the code. If you are a relevant code
owner, and no other experts are reviewing a patch, you must either help arrange
for an expert to review the patch or review it yourself.

Code Reviews, Speed, and Reciprocity
------------------------------------

Sometimes code reviews will take longer than you might hope, especially for
larger features. Common ways to speed up review times for your patches are:

* Review other people's patches. If you help out, everybody will be more
  willing to do the same for you; goodwill is our currency.
* Ping the patch. If it is urgent, provide reasons why it is important to you to
  get this patch landed and ping it every couple of days. If it is
  not urgent, the common courtesy ping rate is one week. Remember that you're
  asking for valuable time from other professional developers.
* Ask for help on IRC. Developers on IRC will be able to either help you
  directly, or tell you who might be a good reviewer.
* Split your patch into multiple smaller patches that build on each other. The
  smaller your patch is, the higher the probability that somebody will take a quick
  look at it. When doing this, it is helpful to add "[N/M]" (for 1 <= N <= M) to
  the title of each patch in the series, so it is clear that there is an order
  and what that order is.

Developers should participate in code reviews as both reviewers and
authors. If someone is kind enough to review your code, you should return the
favor for someone else. Note that anyone is welcome to review and give feedback
on a patch, but approval of patches should be consistent with the policy above.
