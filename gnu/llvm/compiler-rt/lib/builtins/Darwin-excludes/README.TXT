This folder contains list of symbols that should be excluded from the builtin
libraries for Darwin. There are two reasons symbols are excluded:

(1) They aren't supported on Darwin
(2) They are contained within the OS on the minimum supported target

The builtin libraries must contain all symbols not provided by the lowest
supported target OS. Meaning if minimum deployment target is iOS 6, all builtins
not included in the ios6-<arch>.txt files need to be included. The one catch is
that this is per-architecture. Since iOS 6 doesn't support arm64, when supporting
iOS 6, the minimum deployment target for arm64 binaries is iOS 7.
