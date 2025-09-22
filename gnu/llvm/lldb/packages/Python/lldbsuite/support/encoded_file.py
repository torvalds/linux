"""
Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
See https://llvm.org/LICENSE.txt for license information.
SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

Prepares language bindings for LLDB build process.  Run with --help
to see a description of the supported command line arguments.
"""

# Python modules:
import io


def _encoded_write(old_write, encoding):
    def impl(s):
        # If we were asked to write a `bytes` decode it as unicode before
        # attempting to write.
        if isinstance(s, bytes):
            s = s.decode(encoding, "replace")
        # Filter unreadable characters, Python 3 is stricter than python 2 about them.
        import re

        s = re.sub(r"[^\x00-\x7f]", r" ", s)
        return old_write(s)

    return impl


"""
Create a Text I/O file object that can be written to with either unicode strings
or byte strings.
"""


def open(
    file, encoding, mode="r", buffering=-1, errors=None, newline=None, closefd=True
):
    wrapped_file = io.open(
        file,
        mode=mode,
        buffering=buffering,
        encoding=encoding,
        errors=errors,
        newline=newline,
        closefd=closefd,
    )
    new_write = _encoded_write(getattr(wrapped_file, "write"), encoding)
    setattr(wrapped_file, "write", new_write)
    return wrapped_file
