# Discourse Migration Guide 

## Current Status of Migration: Discourse is back online at a new URL: [https://discourse.llvm.org](https://discourse.llvm.org). The old one still works as well. We are aware of an issue with reply by email to emails from before the merge. We will update once we know more.

This document is intended to help LLVM users to migrate from the mailing lists to
Discourse. Discourse has two basic ways for interaction: Via the [web
UI](https://llvm.discourse.group/) and via emails.

## Setting up your account

The easiest way is to create an account using your GitHub account:

1. Navigate to https://llvm.discourse.group/
1. Click on "Sign Up" in the top right corner.
1. Choose "With GitHub" on the right side and log in with your GitHub account.

## Structure of Discourse

Discourse's structure is similar to a set of mailing lists, however different
terms are used there. To help with the transition, here's a translation table
for the terms:

<table border=1>
<tr><th>Mailing list</th><th>Discourse</th></tr>
<tr><td><i>Mailing list</i>, consists of threads</td><td><i>category</i>, consists of topics</td></tr>
<tr><td><i>thread</i>, consists of emails</td><td><i>topic</i>, consists of posts</td></tr>
<tr><td>email</td><td>post</td></tr>
</table>

## Setting up email interactions

Some folks want to interact with Discourse purely via their email program. Here
are the typical use cases:

* You can [subscribe to a category or topic](https://discourse.mozilla.org/t/how-do-i-subscribe-to-categories-and-topics/16024)
* You can reply to a post, including quoting other peoples texts
  ([tested](https://llvm.discourse.group/t/email-interaction-with-discourse/3306/4) on GMail).
* [Quoting previous topics in an reply](https://meta.discourse.org/t/single-quote-block-dropped-in-email-reply/144802)
* You can filter incoming emails in your email client by category using the
  `List-ID` email header field.
* You can create topics through email using the email address that is specific to the category. Each category description shows the email address to use, or you can use the mapping below.

## Mapping of email addresses to Discourse categories

Use these email addresses to create a topic by email in the specific discourse category. You **must** have a Discourse account associated with the email address you are sending from or the email will be rejected.

<table border=1>
<tr><th>Discourse Category</th><th>Email Address</th></tr>
<tr><td>Beginner</td><td>beginners@discourse.llvm.org</td></tr>
<tr><td>LLVM Project</td><td>llvmproject@discourse.llvm.org</td></tr>
<tr><td>IR & Optimizations</td><td>IR.Optimizations@discourse.llvm.org</td></tr>
<tr><td>IR & Optimizations - Loop Optimizations</td><td>IR.Optimizations-Loops@discourse.llvm.org</td></tr>
<tr><td>Code Generation</td><td>codegen@discourse.llvm.org</td></tr>
<tr><td>Code Generation - AMDGPU</td><td>codegen-amdgpu@discourse.llvm.org</td></tr>
<tr><td>Code Generation - Common Infrastructure</td><td>codegen-common@discourse.llvm.org</td></tr>
<tr><td>Code Generation - AArch64</td><td>codegen-aarch64@discourse.llvm.org</td></tr>
<tr><td>Code Generation - Arm</td><td>codegen-arm@discourse.llvm.org</td></tr>
<tr><td>Code Generation - PowerPC</td><td>codegen-powerpc@discourse.llvm.org</td></tr>
<tr><td>Code Generation - RISCV</td><td>codegen-riscv@discourse.llvm.org</td></tr>
<tr><td>Code Generation - WebAssembly</td><td>codegen-webassembly@discourse.llvm.org</td></tr>
<tr><td>Code Generation - X86</td><td>codegen-x86@discourse.llvm.org</td></tr>
<tr><td>Clang Frontend</td><td>clang@discourse.llvm.org</td></tr>
<tr><td>Clang Frontend - Using Clang</td><td>clang-users@discourse.llvm.org</td></tr>
<tr><td>Clang Frontend - clangd</td><td>clangd@discourse.llvm.org</td></tr>
<tr><td>Clang Frontend - Building Clang</td><td>clang-build@discourse.llvm.org</td></tr>
<tr><td>Clang Frontend - Static Analyzer</td><td>clang-staticanalyzer@discourse.llvm.org</td></tr>
<tr><td>Runtimes</td><td>runtimes@discourse.llvm.org</td></tr>
<tr><td>Runtimes - C++</td><td>runtimes-cxx@discourse.llvm.org</td></tr>
<tr><td>Runtimes - Sanitizers</td><td>runtimes-sanitizers@discourse.llvm.org</td></tr>
<tr><td>Runtimes - C</td><td>runtimes-c@discourse.llvm.org</td></tr>
<tr><td>Runtimes - OpenMP</td><td>runtimes-openmp@discourse.llvm.org</td></tr>
<tr><td>Runtimes - OpenCL</td><td>runtimes-opencl@discourse.llvm.org</td></tr>
<tr><td>MLIR</td><td>mlir@discourse.llvm.org</td></tr>
<tr><td>MLIR - Announce</td><td>mlir-announce@discourse.llvm.org</td></tr>
<tr><td>MLIR - Newsletter</td><td>mlir-news@discourse.llvm.org</td></tr>
<tr><td>MLIR - TCP-WG</td><td>mlir-tcpwg@discourse.llvm.org</td></tr>
<tr><td>Subprojects</td><td>subprojects@discourse.llvm.org</td></tr>
<tr><td>Subprojects - Polly</td><td>polly@discourse.llvm.org</td></tr>
<tr><td>Subprojects - LLDB</td><td>lldb@discourse.llvm.org</td></tr>
<tr><td>Subprojects - LLD</td><td>lld@discourse.llvm.org</td></tr>
<tr><td>Subprojects - Flang</td><td> flang@discourse.llvm.org</td></tr>
<tr><td>Subprojects - Bolt</td><td>bolt@discourse.llvm.org</td></tr>
<tr><td>Project Infrastructure</td><td>infra@discourse.llvm.org</td></tr>
<tr><td>Project Infrastructure - Release Testers</td><td>infra-release-testers@discourse.llvm.org</td></tr>
<tr><td>Project Infrastructure - Website</td><td>infra-website@discourse.llvm.org</td></tr>
<tr><td>Project Infrastructure - Documentation</td><td> infra-docs@discourse.llvm.org</td></tr>
<tr><td>Project Infrastructure - GitHub</td><td>infra-github@discourse.llvm.org</td></tr>
<tr><td>Project Infrastructure - Code Review</td><td>infra-codereview@discourse.llvm.org</td></tr>
<tr><td>Project Infrastructure - Discord</td><td>infra-discord@discourse.llvm.org</td></tr>
<tr><td>Project Infrastructure - Mailing Lists and Forums</td><td>infra-mailinglists@discourse.llvm.org</td></tr>
<tr><td>Project Infrastructure - IRC</td><td> infra-irc@discourse.llvm.org</td></tr>
<tr><td>Project Infrastructure - Infrastructure Working Group</td><td>infra-iwg@discourse.llvm.org</td></tr>
<tr><td>Community</td><td>community@discourse.llvm.org</td></tr>
<tr><td>Community - Women in Compilers and Tools</td><td>wict@discourse.llvm.org</td></tr>
<tr><td>Community - Job Postings</td><td>community-jobs@discourse.llvm.org</td></tr>
<tr><td>Community - US LLVM Developers' Meeting</td><td>devmtg-US@discourse.llvm.org</td></tr>
<tr><td>Community - EuroLLVM</td><td>devmtg-euro@discourse.llvm.org</td></tr>
<tr><td>Community - GSOC</td><td>gsoc@discourse.llvm.org</td></tr>
<tr><td>Community - Community.o</td><td>community-dot-o@discourse.llvm.org</td></tr>
<tr><td>Community - LLVM Foundation</td><td>foundation@discourse.llvm.org</td></tr>
<tr><td>Community - Newsletters</td><td>newsletters@discourse.llvm.org</td></tr>
<tr><td>Incubator</td><td>incubator@discourse.llvm.org</td></tr>
<tr><td>Incubator - CIRCT</td><td>circt@discourse.llvm.org</td></tr>
<tr><td>Incubator - Torch-MLIR</td><td>torch-mlir@discourse.llvm.org</td></tr>
<tr><td>Incubator - Enzyme</td><td>enzyme@discourse.llvm.org</td></tr>
<tr><td>Feedback</td><td>feedback@discourse.llvm.org</td></tr>
</table>

## Mapping of mailing lists to categories

This table explains the mapping from mailing lists to categories in Discourse.
The email addresses of these categories will remain the same, after the
migration.  Obsolete lists will become read-only as part of the Discourse
migration.


<table border=1>
<tr><th>Mailing lists</th><th>Category in Discourse</th></tr>

<tr><td>All-commits</td><td>no migration at the moment</td></tr>
<tr><td>Bugs-admin</td><td>no migration at the moment</td></tr>
<tr><td>cfe-commits</td><td>no migration at the moment</td></tr>
<tr><td>cfe-dev</td><td>Clang Frontend</td></tr>
<tr><td>cfe-users</td><td>Clang Frontend/Using Clang</td></tr>
<tr><td>clangd-dev</td><td>Clang Frontend/clangd</td></tr>
<tr><td>devmtg-organizers</td><td>Obsolete</td></tr>
<tr><td>Docs</td><td>Obsolete</td></tr>
<tr><td>eurollvm-organizers</td><td>Obsolete</td></tr>
<tr><td>flang-commits</td><td>no migration at the moment</td></tr>
<tr><td>flang-dev</td><td>Subprojects/Flang Fortran Frontend</td></tr>
<tr><td>gsoc</td><td>Obsolete</td></tr>
<tr><td>libc-commits</td><td>no migration at the moment</td></tr>
<tr><td>libc-dev</td><td>Runtimes/C</td></tr>
<tr><td>Libclc-dev</td><td>Runtimes/OpenCL</td></tr>
<tr><td>libcxx-bugs</td><td>no migration at the moment</td></tr>
<tr><td>libcxx-commits</td><td>no migration at the moment</td></tr>
<tr><td>libcxx-dev</td><td>Runtimes/C++</td></tr>
<tr><td>lldb-commits</td><td>no migration at the moment</td></tr>
<tr><td>lldb-dev</td><td>Subprojects/lldb</td></tr>
<tr><td>llvm-admin</td><td>no migration at the moment</td></tr>
<tr><td>llvm-announce</td><td>Announce</td></tr>
<tr><td>llvm-branch-commits</td><td>no migration at the moment</td></tr>
<tr><td>llvm-bugs</td><td>no migration at the moment</td></tr>
<tr><td>llvm-commits</td><td>no migration at the moment</td></tr>
<tr><td>llvm-dev</td><td>Project Infrastructure/LLVM Dev List Archives</td></tr>
<tr><td>llvm-devmeeting</td><td>Community/US Developer Meeting</td></tr>
<tr><td>llvm-foundation</td><td>Community/LLVM Foundation</td></tr>
<tr><td>Mlir-commits</td><td>no migration at the moment</td></tr>
<tr><td>Openmp-commits</td><td>no migration at the moment</td></tr>
<tr><td>Openmp-dev</td><td>Runtimes/OpenMP</td></tr>
<tr><td>Parallel_libs-commits</td><td>no migration at the moment</td></tr>
<tr><td>Parallel_libs-dev</td><td>Runtimes/C++</td></tr>
<tr><td>Release-testers</td><td>Project Infrastructure/Release Testers</td></tr>
<tr><td>Test-list</td><td>Obsolete</td></tr>
<tr><td>vmkit-commits</td><td>Obsolete</td></tr>
<tr><td>WiCT</td><td>Community/Women in Compilers and Tools</td></tr>
<tr><td>www-scripts</td><td>Obsolete</td></tr> 
</table>


## FAQ

### I don't want to use a web UI

You can do most of the communication with your email client (see section on
Setting up email interactions above). You only need to set up your account once
and then configure which categories you want to subscribe to.

### How do I send a private message?

On the mailing list you have the opportunity to reply only to the sender of
the email, not to the entire list. That is not supported when replying via
email on Discourse. However you can send someone a private message via the
Web UI: Click on the user's name above a post and then on `Message`.

Also Discourse does not expose users' email addresses , so your private
replies have to go through their platform (unless you happen to know the
email address of the user.)

### How can my script/tool send automatic messages?**

In case you want to [create a new
post/topic](https://docs.discourse.org/#tag/Posts/paths/~1posts.json/post)
automatically from a script or tool, you can use the
[Discourse API](https://docs.discourse.org/).

### Who are the admins for Discourse?

See https://llvm.discourse.group/about

### What is the reason for the migration?

See
[this email](https://lists.llvm.org/pipermail/llvm-dev/2021-June/150823.html)

### How do I set up a private mailing list?

If needed categories can have individual [security
settings](https://meta.discourse.org/t/how-to-use-category-security-settings-to-create-private-categories/87678)
to limit visibility and write permissions. Contact the
[admins](https://llvm.discourse.group/about) if you need such a category.

### What will happen to our email archives?

The Mailman archives will remain on the web server for now.

### What are advantages of Discourse over the current mailing lists?

* Users can post to any category, also without being subscribed.
* Full text search on the Web UI.
* Sending/replying via the Web UI (email is still possible).
* View entire thread on one page.
* Categories are a more light-weight option to structure the discussions than
  creating new mailing lists.
* Single sign on with GitHub.
* User email addresses are kept private.

### I have another question not covered here. What should I do?

Please contact iwg@llvm.org or raise a
[ticket on GitHub](https://github.com/llvm/llvm-iwg/issues).
