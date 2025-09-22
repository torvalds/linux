# Policies on git repositories

This document explains our current policies around git repositories. Everything
not covered in this document is most likely a case-by-case decision. In these
cases please create an issue with the
[Infrastructure Working Group](https://github.com/llvm/llvm-iwg/issues).

## New GitHub repositories

Requirements for *new* repositories as part of the
[LLVM organisation on GitHub](https://github.com/llvm):

* The repo will be used for something related to the LLVM ecosystem or community.
* The repo contains a `README.md` explaining the contents.
* The repo contains a `CONTRIBUTING.md`, ideally copy this from
  [llvm-project](https://github.com/llvm/llvm-project/blob/main/CONTRIBUTING.md).
* The repo contains a `LICENSE.TXT`, preferably copy this from
  [llvm-project](https://github.com/llvm/llvm-project/blob/main/LICENSE.TXT).
  Other licences need to be discussed case-by-case.

If you want to integrate your project as part of the Monorepo, please take a
look at the
[Developer Policy](project:DeveloperPolicy.rst#Adding an Established Project To the LLVM Monorepo).

To request a new repository, please create an issue with the
[Infrastructure Working Group](https://github.com/llvm/llvm-iwg/issues).

## Repo access on GitHub

Some 3rd party applications require write access to our GitHub organisation in
order to work properly. Typical examples are continuous integration services
reporting build results back to GitHub. We consider granting access to such
application if they provide benefits to the LLVM community and do not raise
privacy or security concerns.

To request access please run an RFC on the mailing list and get community
feedback.
