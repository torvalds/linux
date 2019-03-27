[![Build Status](https://travis-ci.org/jedisct1/libsodium.svg?branch=master)](https://travis-ci.org/jedisct1/libsodium?branch=master)
[![Windows build status](https://ci.appveyor.com/api/projects/status/fu8s2elx25il98hj?svg=true)](https://ci.appveyor.com/project/jedisct1/libsodium)
[![Coverity Scan Build Status](https://scan.coverity.com/projects/2397/badge.svg)](https://scan.coverity.com/projects/2397)

![libsodium](https://raw.github.com/jedisct1/libsodium/master/logo.png)
============

Sodium is a new, easy-to-use software library for encryption,
decryption, signatures, password hashing and more.

It is a portable, cross-compilable, installable, packageable
fork of [NaCl](http://nacl.cr.yp.to/), with a compatible API, and an
extended API to improve usability even further.

Its goal is to provide all of the core operations needed to build
higher-level cryptographic tools.

Sodium supports a variety of compilers and operating systems,
including Windows (with MingW or Visual Studio, x86 and x64), iOS, Android,
as well as Javascript and Webassembly.

## Documentation

The documentation is available on Gitbook and built from the [libsodium-doc](https://github.com/jedisct1/libsodium-doc) repository:

* [libsodium documentation](https://download.libsodium.org/doc/) -
online, requires Javascript.
* [offline documentation](https://www.gitbook.com/book/jedisct1/libsodium/details)
in PDF, MOBI and ePUB formats.

## Integrity Checking

The integrity checking instructions (including the signing key for libsodium)
are available in the [installation](https://download.libsodium.org/doc/installation/index.html#integrity-checking)
section of the documentation.

## Community

A mailing-list is available to discuss libsodium.

In order to join, just send a random mail to `sodium-subscribe` {at}
`pureftpd` {dot} `org`.

## License

[ISC license](https://en.wikipedia.org/wiki/ISC_license).
