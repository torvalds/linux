=================
ld64 vs LLD-MachO
=================

This doc lists all significant deliberate differences in behavior between ld64
and LLD-MachO.

Dead Stripping Duplicate Symbols
********************************
ld64 strips dead code before reporting duplicate symbols. By default, LLD does
the opposite. ld64's behavior hides ODR violations, so we have chosen not
to follow it. But, to make adoption easy, LLD can mimic this behavior via
the ``--dead-strip-duplicates`` flag. Usage of this flag is discouraged, and
this behavior should be fixed in the source. However, for sources that are not
within the user's control, this will mitigate users for adoption.

``-no_deduplicate`` Flag
************************
- ld64: This turns off ICF (deduplication pass) in the linker.
- LLD: This turns off ICF and string merging in the linker.

String Alignment
****************
LLD is `slightly less conservative about aligning cstrings
<https://reviews.llvm.org/D121342>`_, allowing it to pack them more compactly.
This should not result in any meaningful semantic difference.

ObjC Symbols Treatment
**********************
There are differences in how LLD and ld64 handle ObjC symbols loaded from
archives.

- ld64:
   1. Duplicate ObjC symbols from the same archives will not raise an error.
      ld64 will pick the first one.
   2. Duplicate ObjC symbols from different archives will raise a "duplicate
      symbol" error.
- LLD: Duplicate symbols, regardless of which archives they are from, will
  raise errors.

Aliases
=======
ld64 treats all aliases as strong extern definitions. Having two aliases of the
same name, or an alias plus a regular extern symbol of the same name, both
result in duplicate symbol errors. LLD does not check for duplicate aliases;
instead we perform alias resolution first, and only then do we check for
duplicate symbols. In particular, we will not report a duplicate symbol error if
the aliased symbols turn out to be weak definitions, but ld64 will.

``ZERO_AR_DATE`` enabled by default
***********************************
ld64 has a special mode where it sets some stabs modification times to 0 for
hermetic builds, enabled by setting any value for the ``ZERO_AR_DATE``
environment variable. LLD flips this default to prefer hermetic builds, but
allows disabling this behavior by setting ``ZERO_AR_DATE=0``. Any other value
of ``ZERO_AR_DATE`` will be ignored.
