=============================
LLVM Community Support Policy
=============================

As a compilation infrastructure, LLVM has multiple types of users, both
downstream and upstream, of many combinations of its projects, tools and
libraries.

There is a core part of it that encompass the implementation of the compiler
(front/middle/back ends), run-time libraries (RT, C++, OpenMP, etc) and
associated tools (debugger, linker, object file manipulation, etc). These
components are present in the public release on our supported architectures
and operating systems and the whole community must maintain and care about.

There are, however, other components within the main repository that either
cater to a specific sub-community of LLVM (upstream or downstream) or
help parts of the community to integrate LLVM into their own development tools
or external projects. Those parts of the main repository don't always have
rigorous testing like the core parts, nor are they validated and shipped with
our public upstream releases.

Even not being a core part of the project, we have enough sub-communities
needing those changes with enough overlap that having them in the main
repository is beneficial to minimise the repetition of those changes in all
the external repositories that need them.

But the maintenance costs of such diverse ecosystem is non trivial, so we divide
the level of support in two tiers: core and peripheral, with two
different levels of impact and responsibilities. Those tiers refer only to the
main repository (``llvm-project``) and not the other repositories in our git
project, unless explicitly stated.

Regardless of the tier, all code must follow the existing policies on quality,
reviews, style, etc.

Core Tier
=========

The core tier encompasses all of the code in the main repository that is
in production, is actively tested and released in a regular schedule, including
core LLVM APIs and infrastructure, front/middle/back-ends, run-time libraries,
tools, etc.

It is the responsibility of **every** LLVM developer to care for the core tier
regardless of where their work is applied to.

What is covered
---------------

The core tier is composed of:
 * Core code (``llvm-project``) present in official releases and buildbots:
   compiler, debugger, linker, libraries, etc, including infrastructure code
   (table-gen, lit, file-check, unit-tests, etc).
 * Build infrastructure that creates releases and buildbots (CMake, scripts).
 * `Phabricator <https://github.com/llvm/phabricator>`_ and
   `buildbot <https://github.com/llvm/llvm-zorg>`_ infrastructure.
 * The `test-suite <https://github.com/llvm/llvm-test-suite>`_.

Requirements
------------

Code in this tier must:
 * Keep official buildbots green, with warnings on breakages being emailed to
   all affected developers. Those must be fixed as soon as possible or patches
   must be reverted, as per review policy.
 * Bit-rot of a component in the core tier will result in that component being
   downgraded to the peripheral tier or being removed. Sub-communities can
   avoid this by fixing all raised issues in a timely manner.

Peripheral Tier
===============

The peripheral tier encompass the parts of LLVM that cater to a specific
sub-community and which don't usually affect the core components directly.

This includes experimental back-ends, disabled-by-default options and
alternative paths (work-in-progress replacements) in the same repository, as
well as separate efforts to integrate LLVM development with local practices.

It is the responsibility of each sub-community to care about their own parts
and the intersection of that with the core tier and other peripheral parts.

There are three main groups of code that fit in this category:
 * Code that is making its way into LLVM, via the `experimental <https://llvm.org/docs/DeveloperPolicy.html#introducing-new-components-into-llvm>`_
   roadmap or similar efforts.
 * Code that is making its way out of LLVM, via deprecation, replacement or
   bit-rot, and will be removed if the sub-community that cares about it
   cannot maintain it.
 * Code that isn't meant to be in LLVM core and can coexist with the code in
   the core tier (and others in the peripheral tier) long term, without causing
   breakages or disturbances.

What is covered
---------------

The peripheral tier is composed of:
 * Experimental targets and options that haven't been enable by default yet.
 * Main repository projects that don't get released or regularly tested.
 * Legacy tools and scripts that aren't used in upstream validation.
 * Alternative build systems (ex. GN, Bazel) and related infrastructure.
 * Tools support (ex. gdb scripts, editor configuration, helper scripts).

Requirements
------------

Code in this tier must:
 * Have a clear benefit for residing in the main repository, catering to an
   active sub-community (upstream or downstream).
 * Be actively maintained by such sub-community and have its problems addressed
   in a timely manner.

Code in this tier must **not**:
 * Break or invalidate core tier code or infrastructure. If that happens
   accidentally, reverting functionality and working on the issues offline
   is the only acceptable course of action.
 * Negatively affect development of core tier code, with the sub-community
   involved responsible for making changes to address specific concerns.
 * Negatively affect other peripheral tier code, with the sub-communities
   involved tasked to resolve the issues, still making sure the solution doesn't
   break or invalidate the core tier.
 * Impose sub-optimal implementation strategies on core tier components as a
   result of idiosyncrasies in the peripheral component.
 * Have build infrastructure that spams all developers about their breakages.
 * Fall into disrepair. This is a reflection of lack of an active sub-community
   and will result in removal.

Code in this tier should:
 * Have infrastructure to test, whenever meaningful, with either no warnings or
   notification contained within the sub-community.
 * Have support and testing that scales with the complexity and resilience of
   the component, with the bar for simple and gracefully-degrading components
   (such as editor bindings) much lower than for complex components that must
   remain fresh with HEAD (such as experimental back-ends or alternative build
   systems).
 * Have a document making clear the status of implementation, level of support
   available, who the sub-community is and, if applicable, roadmap for inclusion
   into the core tier.
 * Be restricted to a specific directory or have a consistent pattern (ex.
   unique file suffix), making it easy to remove when necessary.

Inclusion Policy
================

To add a new peripheral component, send an RFC to the appropriate dev list
proposing its addition and explaining how it will meet the support requirements
listed above. Different types of components could require different levels of
detail. when in doubt, ask the community what's the best approach.

Inclusion must reach consensus in the RFC by the community and the approval of
the corresponding review (by multiple members of the community) is the official
note of acceptance.

After merge, there often is a period of transition, where teething issues on
existing buildbots are discovered and fixed. If those cannot be fixed straight
away, the sub-community is responsible for tracking and reverting all the
pertinent patches and retrying the inclusion review.

Once the component is stable in tree, it must follow this policy and the
deprecation rules below apply.

Due to the uncertain nature of inclusion, it's advisable that new components
are not added too close to a release branch. The time will depend on the size
and complexity of the component, so adding release and testing managers on the
RFC and review is strongly advisable.

Deprecation Policy
==================

The LLVM code base has a number of files that aren't being actively maintained.
But not all of those files are obstructing the development of the project and
so it remains in the repository with the assumption that it could still be
useful for downstream users.

For code to remain in the repository, its presence must not impose an undue
burden on maintaining other components (core or peripheral).

Warnings
--------

There are multiple types of issues that might trigger a request for deprecation,
including (but not limited to):

 * Changes in a component consistently break other areas of the project.
 * Components go broken for long periods of time (weeks or more).
 * Clearly superior alternatives are in use and maintenance is painful.
 * Builds and tests are harder / take longer, increasing the cost of
   maintenance, overtaking the perceived benefits.

If the maintenance cost is higher than it is acceptable by the majority of
developers, it means that either the sub-community is too small (and the extra
cost should be paid locally), or not active enough (and the problems won't be
fixed any time soon). In either case, removal of such problematic component is
justified.

Steps for removal
-----------------

However clear the needs for removal are, we should take an incremental approach
to deprecating code, especially when there's still a sub-community that cares
about it. In that sense, code will never be removed outright without a series
of steps are taken.

A minimum set of steps should be:
 #. A proposal for removal / deactivation should be made to the Discourse forums 
    (under the appropriate category), with a clear
    statement of the maintenance costs imposed and the alternatives, if
    applicable.
 #. There must be enough consensus on the list that removal is warranted, and no
    pending proposals to fix the situation from a sub-community.
 #. An announcement for removal must be made on the same lists, with ample time
    for downstream users to take action on their local infrastructure. The time
    will depend on what is being removed.

    #. If a script or documents are to be removed, they can always be pulled
       from previous revision, and can be removed within days.
    #. if a whole target is removed, we need to first announce publicly, and
       potentially mark as deprecated in one release, only to remove on the
       next release.
    #. Everything else will fall in between those two extremes.
 #. The removal is made by either the proposer or the sub-community that used to
    maintain it, with replacements and arrangements made atomically on the same
    commit.

If a proposal for removal is delayed by the promise a sub-community will take
care of the code affected, the sub-community will have a time to fix all the
issues (depending on each case, as above), and if those are not fixed in time, a
subsequent request for removal should be made and the community may elect to
eject the component without further attempts to fix.

Reinstatement
-------------

If a component is removed from LLVM, it may, at a later date, request inclusion
of a modified version, with evidence that all of the issues were fixed and that
there is a clear sub-community that will maintain it.

By consequence, the pressure on such sub-community will be higher to keep
overall maintenance costs to a minimum and will need to show steps to mitigate
all of the issues that were listed as reasons for its original removal.

Failing on those again, will lead to become a candidate for removal yet again.

