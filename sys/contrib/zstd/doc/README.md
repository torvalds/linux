Zstandard Documentation
=======================

This directory contains material defining the Zstandard format,
as well as detailed instructions to use `zstd` library.

__`zstd_manual.html`__ : Documentation of `zstd.h` API, in html format.
Click on this link: [http://zstd.net/zstd_manual.html](http://zstd.net/zstd_manual.html)
to display documentation of latest release in readable format within a browser.

__`zstd_compression_format.md`__ : This document defines the Zstandard compression format.
Compliant decoders must adhere to this document,
and compliant encoders must generate data that follows it.

Should you look for ressources to develop your own port of Zstandard algorithm,
you may find the following ressources useful :

__`educational_decoder`__ : This directory contains an implementation of a Zstandard decoder,
compliant with the Zstandard compression format.
It can be used, for example, to better understand the format,
or as the basis for a separate implementation of Zstandard decoder.

[__`decode_corpus`__](https://github.com/facebook/zstd/tree/dev/tests#decodecorpus---tool-to-generate-zstandard-frames-for-decoder-testing) :
This tool, stored in `/tests` directory, is able to generate random valid frames,
which is useful if you wish to test your decoder and verify it fully supports the specification.
