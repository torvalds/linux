# Contributing to `pin-init`

Thanks for showing interest in contributing to `pin-init`! This document outlines the guidelines for
contributing to `pin-init`.

All contributions are double-licensed under Apache 2.0 and MIT. You can find the respective licenses
in the `LICENSE-APACHE` and `LICENSE-MIT` files.

## Non-Code Contributions

### Bug Reports

For any type of bug report, please submit an issue using the bug report issue template.

If the issue is a soundness issue, please privately report it as a security vulnerability via the
GitHub web interface.

### Feature Requests

If you have any feature requests, please submit an issue using the feature request issue template.

### Questions and Getting Help

You can ask questions in the Discussions page of the GitHub repository. If you're encountering
problems or just have questions related to `pin-init` in the Linux kernel, you can also ask your
questions in the [Rust-for-Linux Zulip](https://rust-for-linux.zulipchat.com/) or see
<https://rust-for-linux.com/contact>.

## Contributing Code

### Linux Kernel

`pin-init` is used by the Linux kernel and all commits are synchronized to it. For this reason, the
same requirements for commits apply to `pin-init`. See [the kernel's documentation] for details. The
rest of this document will also cover some of the rules listed there and additional ones.

[the kernel's documentation]: https://docs.kernel.org/process/submitting-patches.html

Contributions to `pin-init` ideally go through the [GitHub repository], because that repository runs
a CI with lots of tests not present in the kernel. However, patches are also accepted (though not
preferred). Do note that there are some files that are only present in the GitHub repository such as
tests, licenses and cargo related files. Making changes to them can only happen via GitHub.

[GitHub repository]: https://github.com/Rust-for-Linux/pin-init

### Commit Style

Everything must compile without errors or warnings and all tests must pass after **every commit**.
This is important for bisection and also required by the kernel.

Each commit should be a single, logically cohesive change. Of course it's best to keep the changes
small and digestible, but logically linked changes should be made in the same commit. For example,
when fixing typos, create a single commit that fixes all of them instead of one commit per typo.

Commits must have a meaningful commit title. Commits with changes to files in the `internal`
directory should have a title prefixed with `internal:`. The commit message should explain the
change and its rationale. You also have to add your `Signed-off-by` tag, see [Developer's
Certificate of Origin]. This has to be done for both mailing list submissions as well as GitHub
submissions.

[Developer's Certificate of Origin]: https://docs.kernel.org/process/submitting-patches.html#sign-your-work-the-developer-s-certificate-of-origin

Any changes made to public APIs must be documented not only in the commit message, but also in the
`CHANGELOG.md` file. This is especially important for breaking changes, as those warrant a major
version bump.

If you make changes to the top-level crate documentation, you also need to update the `README.md`
via `cargo rdme`.

Some of these rules can be ignored if the change is done solely to files that are not present in the
kernel version of this library. Those files are documented in the `sync-kernel.sh` script at the
very bottom in the `--exclude` flag given to the `git am` command.
