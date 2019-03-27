zstdgrep(1) -- print lines matching a pattern in zstandard-compressed files
============================================================================

SYNOPSIS
--------

`zstdgrep` [*grep-flags*] [--] _pattern_ [_files_ ...]


DESCRIPTION
-----------
`zstdgrep` runs `grep (1)` on files or stdin, if no files argument is given, after decompressing them with `zstdcat (1)`.

The grep-flags and pattern arguments are passed on to `grep (1)`.  If an `-e` flag is found in the `grep-flags`, `zstdgrep` will not look for a pattern argument.

EXIT STATUS
-----------
In case of missing arguments or missing pattern, 1 will be returned, otherwise 0.

SEE ALSO
--------
`zstd (1)`

AUTHORS
-------
Thomas Klausner <wiz@NetBSD.org>
