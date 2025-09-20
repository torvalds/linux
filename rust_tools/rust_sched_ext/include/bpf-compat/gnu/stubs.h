/*
 * Dummy gnu/stubs.h. clang can end up including /usr/include/gnu/stubs.h when
 * compiling BPF files although its content doesn't play any role. The file in
 * turn includes stubs-64.h or stubs-32.h depending on whether __x86_64__ is
 * defined. When compiling a BPF source, __x86_64__ isn't set and thus
 * stubs-32.h is selected. However, the file is not there if the system doesn't
 * have 32bit glibc devel package installed leading to a build failure.
 *
 * The problem is worked around by making this file available in the include
 * search paths before the system one when building BPF.
 */
