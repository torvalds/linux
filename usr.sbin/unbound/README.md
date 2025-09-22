# Unbound

[![Github Build Status](https://github.com/NLnetLabs/unbound/actions/workflows/ci.yml/badge.svg?branch=master)](https://github.com/NLnetLabs/unbound/actions)
[![Packaging status](https://repology.org/badge/tiny-repos/unbound.svg)](https://repology.org/project/unbound/versions)
[![Fuzzing Status](https://oss-fuzz-build-logs.storage.googleapis.com/badges/unbound.svg)](https://bugs.chromium.org/p/oss-fuzz/issues/list?sort=-opened&can=1&q=proj:unbound)
[![Documentation Status](https://readthedocs.org/projects/unbound/badge/?version=latest)](https://unbound.readthedocs.io/en/latest/?badge=latest)
[![Mastodon Follow](https://img.shields.io/mastodon/follow/109262826617293067?domain=https%3A%2F%2Ffosstodon.org&style=social)](https://fosstodon.org/@nlnetlabs)

Unbound is a validating, recursive, caching DNS resolver. It is designed to be
fast and lean and incorporates modern features based on open standards. If you
have any feedback, we would love to hear from you. Don’t hesitate to
[create an issue on Github](https://github.com/NLnetLabs/unbound/issues/new)
or post a message on the [Unbound mailing list](https://lists.nlnetlabs.nl/mailman/listinfo/unbound-users).
You can learn more about Unbound by reading our
[documentation](https://unbound.docs.nlnetlabs.nl/).

## Compiling

Make sure you have the C toolchain, OpenSSL and its include files, and libexpat
installed.
If building from the repository source you also need flex and bison installed.
Unbound can be compiled and installed using:

```
./configure && make && make install
```

You can use libevent if you want. libevent is useful when using many (10000)
outgoing ports. By default max 256 ports are opened at the same time and the
builtin alternative is equally capable and a little faster.

Use the `--with-libevent` configure option to compile Unbound with libevent
support.

## Unbound configuration

All of Unbound's configuration options are described in the man pages, which
will be installed and are available on the Unbound
[documentation page](https://unbound.docs.nlnetlabs.nl/).

An example configuration file is located in
[doc/example.conf](https://github.com/NLnetLabs/unbound/blob/master/doc/example.conf.in).
