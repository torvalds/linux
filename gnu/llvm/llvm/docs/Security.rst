===================
LLVM Security Group
===================

The LLVM Security Group has the following goals:

1. Allow LLVM contributors and security researchers to disclose security-related issues affecting the LLVM project to members of the LLVM community.
2. Organize fixes, code reviews, and release management for said issues.
3. Allow distributors time to investigate and deploy fixes before wide dissemination of vulnerabilities or mitigation shortcomings.
4. Ensure timely notification and release to vendors who package and distribute LLVM-based toolchains and projects.
5. Ensure timely notification to users of LLVM-based toolchains whose compiled code is security-sensitive, through the `CVE process`_.
6. Strive to improve security over time, for example by adding additional testing, fuzzing, and hardening after fixing issues.

*Note*: these goals ensure timely action, provide disclosure timing when issues are reported, and respect vendors' / packagers' / users' constraints.

The LLVM Security Group is private. It is composed of trusted LLVM contributors. Its discussions remain within the Security Group (plus issue reporter and key experts) while an issue is being investigated. After an issue becomes public, the entirety of the group’s discussions pertaining to that issue also become public.

.. _report-security-issue:

How to report a security issue?
===============================

To report a security issue in any of the LLVM projects, please use the `report a vulnerability`_ feature in the `llvm/llvm-security-repo`_ repository on github, under the "Security" tab.

We aim to acknowledge your report within two business days since you first reach out. If you do not receive any response by then, you can escalate by posting on the `Discourse forums`_ asking to get in touch with someone from the LLVM Security Group. **The escalation mailing list is public**: avoid discussing or mentioning the specific issue when posting on it.


Group Composition
=================

Security Group Members
----------------------

The members of the group represent a wide cross-section of the community, and
meet the criteria for inclusion below. The list is in the format
`* ${full_name} (${affiliation}) [${github_username}]`. If a github
username for an individual isn't available, the brackets will be empty.

* Ahmed Bougacha (Apple) [@ahmedbougacha]
* Andy Kaylor (Intel) [@andykaylor]
* Artur Pilipenko (Azul Systems Inc) []
* Boovaragavan Dasarathan (Nvidia) [@mrragava]
* Dimitry Andric (individual; FreeBSD) [@DimitryAndric]
* Ed Maste (individual; FreeBSD) [@emaste]
* George Burgess IV (Google) [@gburgessiv]
* Josh Stone (Red Hat; Rust) [@cuviper]
* Kristof Beyls (ARM) [@kbeyls]
* Matthew Riley (Google) [@mmdriley]
* Nikhil Gupta (Nvidia) []
* Oliver Hunt (Apple) [@ojhunt]
* Paul Robinson (Sony) [@pogo59]
* Peter Smith (ARM) [@smithp35]
* Pietro Albini (Ferrous Systems; Rust) [@pietroalbini]
* Serge Guelton (Mozilla) [@serge-sans-paille]
* Shayne Hiet-Block (Microsoft) [@GreatKeeper]
* Tim Penge (Sony) [@tpenge]
* Tulio Magno Quites Machado Filho (Red Hat) [@tuliom]
* Will Huhn (Intel) [@wphuhn-intel]
* Yvan Roux (ST) [@yroux]

Criteria
--------

* Nominees for LLVM Security Group membership should fall in one of these groups:

  - Individual contributors:

    + Specializes in fixing compiler-based security related issues or often participates in their exploration and resolution.
    + Has a track record of finding security vulnerabilities and responsible disclosure of those vulnerabilities.
    + Is a compiler expert who has specific interests in knowing about, resolving, and preventing future security vulnerabilities.
    + Has actively contributed non-trivial code to the LLVM project in the last year.

  - Researchers:

    + Has a track record of finding security vulnerabilities and responsible disclosure of those vulnerabilities.
    + Is a compiler expert who has specific interests in knowing about, resolving, and preventing future security vulnerabilities.

  - Vendor contacts:

    + Represents an organization or company which ships products that include their own copy of LLVM. Due to their position in the organization, the nominee has a reasonable need to know about security issues and disclosure embargoes.

* Additionally, the following are necessary but not sufficient criteria for membership in the LLVM Security Group:

  - If already in the LLVM Security Group, has actively participated in one (if any) security issue in the last year.
  - If already in the LLVM Security Group, has actively participated in most membership discussions in the last year.
  - If already in the LLVM Security Group, has actively participated in writing or reviewing a transparency report in the last year.
  - When employed by a company or other entity, the parent entity has no more than three members already in the LLVM Security Group.
  - When nominated as a vendor contact, their position with that vendor remains the same as when originally nominated.
  - Nominees are trusted by existing Security Group members to keep communications embargoed while still active.

Nomination process
------------------

Anyone who feels they meet these criteria can nominate themselves, or may be nominated by a third party such as an existing LLVM Security Group member. The nomination should state whether the nominee is nominated as an individual, researcher, or as a vendor contact. It should clearly describe the grounds for nomination.

For the moment, nominations are generally proposed, discussed, and voted on using a github pull request. An `example nomination is available here`_. The use of pull requests helps keep membership discussions open, transparent, and easily accessible to LLVM developers in many ways. If, for any reason, a fully-world-readable nomination seems inappropriate, you may reach out to the security group via the `report a vulnerability`_ route, and a discussion can be had about the best way to approach nomination, given the constraints that individuals are under.

Choosing new members
--------------------

If a nomination for LLVM Security Group membership is supported by a majority of existing LLVM Security Group members, then it carries within five business days unless an existing member of the Security Group objects. If an objection is raised, the LLVM Security Group members should discuss the matter and try to come to consensus; failing this, the nomination will succeed only by a two-thirds supermajority vote of the LLVM Security Group.

Accepting membership
--------------------

Before new LLVM Security Group membership is finalized, the successful nominee should accept membership and agree to abide by this security policy, particularly `Privileges and Responsibilities of LLVM Security Group Members`_ below.

Keeping Membership Current
--------------------------

* At least every six months, the LLVM Security Group applies the above criteria. The membership list is pruned accordingly.
* Any Security Group member can ask that the criteria be applied within the next five business days.
* If a member of the LLVM Security Group does not act in accordance with the letter and spirit of this policy, then their LLVM Security Group membership can be revoked by a majority vote of the members, not including the person under consideration for revocation. After a member calls for a revocation vote, voting will be open for five business days.
* Emergency suspension: an LLVM Security Group member who blatantly disregards the LLVM Security Policy may have their membership temporarily suspended on the request of any two members. In such a case, the requesting members should notify the Security Group with a description of the offense. At this point, membership will be temporarily suspended for five business days, pending outcome of the vote for permanent revocation.
* The LLVM Board may remove any member from the LLVM Security Group.

Transparency Report
-------------------

Every year, the LLVM Security Group must publish a transparency report. The intent of this report is to keep the community informed by summarizing the disclosures that have been made public in the last year. It shall contain a list of all public disclosures, as well as statistics on time to fix issues, length of embargo periods, and so on.

The transparency reports are published at :doc:`SecurityTransparencyReports`.


Privileges and Responsibilities of LLVM Security Group Members
==============================================================

Access
------

LLVM Security Group members will be subscribed to a private `Discussion Medium`_. It will be used for technical discussions of security issues, as well as process discussions about matters such as disclosure timelines and group membership. Members have access to all security issues.

Confidentiality
---------------

Members of the LLVM Security Group will be expected to treat LLVM security issue information shared with the group as confidential until publicly disclosed:

* Members should not disclose security issue information to non-members unless both members are employed by the same vendor of a LLVM based product, in which case information can be shared within that organization on a need-to-know basis and handled as confidential information normally is within that organization.
* If the LLVM Security Group agrees, designated members may share issues with vendors of non-LLVM based products if their product suffers from the same issue. The non-LLVM vendor should be asked to respect the issue’s embargo date, and to not share the information beyond the need-to-know people within their organization.
* If the LLVM Security Group agrees, key experts can be brought in to help address particular issues. The key expert should be asked to respect the issue’s embargo date, and to not share the information.

Disclosure
----------

Following the process below, the LLVM Security Group decides on embargo date for public disclosure for each Security issue. An embargo may be lifted before the agreed-upon date if all vendors planning to ship a fix have already done so, and if the reporter does not object.

Collaboration
-------------

Members of the LLVM Security Group are expected to:

* Promptly share any LLVM vulnerabilities they become aware of.
* Volunteer to drive issues forward.
* Help evaluate the severity of incoming issues.
* Help write and review patches to address security issues.
* Participate in the member nomination and removal processes.


Discussion Medium
=================

The medium used to host LLVM Security Group discussions is security-sensitive. It should therefore run on infrastructure which can meet our security expectations.

We use `GitHub's mechanism to privately report security vulnerabilities`_ to have security discussions:

* File security issues.
* Discuss security improvements to LLVM.

We also occasionally need to discuss logistics of the LLVM Security Group itself:

* Nominate new members.
* Propose member removal.
* Suggest policy changes.

We often have these discussions publicly, in our :ref:`monthly public sync-up call <online-sync-ups>` and on the Discourse forums.  For internal or confidential discussions, we also use a private mailing list.

Process
=======

The following process occurs on the discussion medium for each reported issue:

* A security issue reporter (not necessarily an LLVM contributor) reports an issue.
* Within two business days, a member of the Security Group is put in charge of driving the issue to an acceptable resolution. This champion doesn’t need to be the same person for each issue. This person can self-nominate.
* Members of the Security Group discuss in which circumstances (if any) an issue is relevant to security, and determine if it is a security issue.
* Negotiate an embargo date for public disclosure, with a default minimum time limit of ninety days.
* Security Group members can recommend that key experts be pulled in to specific issue discussions. The key expert can be pulled in unless there are objections from other Security Group members.
* Patches are written and reviewed.
* Backporting security patches from recent versions to old versions cannot always work. It is up to the Security Group to decide if such backporting should be done, and how far back.
* The Security Group figures out how the LLVM project’s own releases, as well as individual vendors’ releases, can be timed to patch the issue simultaneously.
* Embargo date can be delayed or pulled forward at the Security Group’s discretion.
* The issue champion obtains a CVE entry from MITRE_.
* Once the embargo expires, the patch is posted publicly according to LLVM’s usual code review process.
* All security issues (as well as nomination / removal discussions) become public within approximately fourteen weeks of the fix landing in the LLVM repository. Precautions should be taken to avoid disclosing particularly sensitive data included in the report (e.g. username and password pairs).


Changes to the Policy
=====================

The LLVM Security Policy may be changed by majority vote of the LLVM Security Group. Such changes also need to be approved by the LLVM Board.


What is considered a security issue?
====================================

The LLVM Project has a significant amount of code, and not all of it is
considered security-sensitive. This is particularly true because LLVM is used in
a wide variety of circumstances: there are different threat models, untrusted
inputs differ, and the environment LLVM runs in is varied. Therefore, what the
LLVM Project considers a security issue is what its members have signed up to
maintain securely.

As this security process matures, members of the LLVM community can propose that
a part of the codebase be designated as security-sensitive (or no longer
security-sensitive). This requires a rationale, and buy-in from the LLVM
community as for any RFC. In some cases, parts of the codebase could be handled
as security-sensitive but need significant work to get to the stage where that's
manageable. The LLVM community will need to decide whether it wants to invest in
making these parts of the code securable, and maintain these security
properties over time. In all cases the LLVM Security Group should be consulted,
since they'll be responding to security issues filed against these parts of the
codebase.

If you're not sure whether an issue is in-scope for this security process or
not, err towards assuming that it is. The Security Group might agree or disagree
and will explain its rationale in the report, as well as update this document
through the above process.

The security-sensitive parts of the LLVM Project currently are the following.
Note that this list can change over time.

* None are currently defined. Please don't let this stop you from reporting
  issues to the security group that you believe are security-sensitive.

The parts of the LLVM Project which are currently treated as non-security
sensitive are the following. Note that this list can change over time.

* Language front-ends, such as clang, for which a malicious input file can cause
  undesirable behavior. For example, a maliciously crafted C or Rust source file
  can cause arbitrary code to execute in LLVM. These parts of LLVM haven't been
  hardened, and compiling untrusted code usually also includes running utilities
  such as `make` which can more readily perform malicious things.


.. _CVE process: https://cve.mitre.org
.. _report a vulnerability: https://github.com/llvm/llvm-security-repo/security/advisories/new
.. _llvm/llvm-security-repo: https://github.com/llvm/llvm-security-repo/security
.. _GitHub's mechanism to privately report security vulnerabilities: https://docs.github.com/en/code-security/security-advisories/guidance-on-reporting-and-writing-information-about-vulnerabilities/privately-reporting-a-security-vulnerability
.. _GitHub security: https://help.github.com/en/articles/about-maintainer-security-advisories
.. _Discourse forums: https://discourse.llvm.org
.. _MITRE: https://cve.mitre.org
.. _example nomination is available here: https://github.com/llvm/llvm-project/pull/92174
