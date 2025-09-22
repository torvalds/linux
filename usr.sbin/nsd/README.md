# NSD

[![GitHub Build Status](https://github.com/NLnetLabs/nsd/actions/workflows/build-test.yml/badge.svg?branch=master)](https://github.com/NLnetLabs/nsd/actions)
[![Coverity Scan Status](https://scan.coverity.com/projects/18867/badge.svg)](https://scan.coverity.com/projects/nlnetlabs-nsd)
[![Packaging status](https://repology.org/badge/tiny-repos/nsd.svg)](https://repology.org/project/nsd/versions)
[![CII Best Practices](https://bestpractices.coreinfrastructure.org/projects/1462/badge)](https://bestpractices.coreinfrastructure.org/projects/1462)
[![Mastodon Follow](https://img.shields.io/mastodon/follow/109262826617293067?domain=https%3A%2F%2Ffosstodon.org&style=social)](https://fosstodon.org/@nlnetlabs)

The NLnet Labs Name Server Daemon (NSD) is an authoritative DNS name server.
It has been developed for operations in environments where speed,
reliability, stability and security are of high importance.  If you
have any feedback, we would love to hear from you. Don’t hesitate to
[create an issue on Github](https://github.com/NLnetLabs/nsd/issues/new)
or post a message on the
[NSD mailing list](https://lists.nlnetlabs.nl/mailman/listinfo/nsd-users).
You can learn more about NSD by reading our
[documentation](https://nsd.docs.nlnetlabs.nl/).

## Building

Make sure you have the following installed:
  * C toolchain (the set of tools to compile C such as a compiler, linker, and assembler)
  * OpenSSL, with its include files (usually these are included in the "dev" version of the library)
  * libevent, with its include files (usually these are included in the "dev" version of the library)
  * flex
  * bison

When building from Git, the `configure` script and [simdzone][simdzone]
sources are missing, use the following commands to get started (note that the
`configure` script and sources are included in release tarballs and do not
need to be generated/downloaded):

```
$ git submodule update --init
$ autoreconf -fi
```

> `autoreconf` should install the required auxiliary files (e.g. `config.sub`
> and `config.guess`). Older versions of `autoreconf` may not do so, try
> running `libtoolize -fi -c` first in that case.

Compile and install using:

```
$ ./configure && make && make install
```

## NSD configuration

The configuration options for NSD are described in the man pages, which are
installed (use `man nsd.conf`) and are available on the NSD
[documentation page](https://nsd.docs.nlnetlabs.nl/).

An example configuration file is located in
[nsd.conf.sample](https://github.com/NLnetLabs/nsd/blob/master/nsd.conf.sample.in).


[simdzone]: https://github.com/NLnetLabs/simdzone
