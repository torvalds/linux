Large data sections
===================

When linking very large binaries, lld may report relocation overflows like

::

  relocation R_X86_64_PC32 out of range: 2158227201 is not in [-2147483648, 2147483647]

This happens when running into architectural limitations. For example, in x86-64
PIC code, a reference to a static global variable is typically done with a
``R_X86_64_PC32`` relocation, which is a 32-bit signed offset from the PC. That
means if the global variable is laid out further than 2GB (2^31 bytes) from the
instruction referencing it, we run into a relocation overflow.

lld normally lays out sections as follows:

.. image:: section_layout.png

The largest relocation pressure is usually from ``.text`` to the beginning of
``.rodata`` or ``.text`` to the end of ``.bss``.

Some code models offer a tradeoff between relocation pressure and performance.
For example, x86-64's medium code model splits global variables into small and
large globals depending on if their size is over a certain threshold. Large
globals are placed further away from text and we use 64-bit references to refer
to them.

Large globals are placed in separate sections from small globals, and those
sections have a "large" section flag, e.g. ``SHF_X86_64_LARGE`` for x86-64. The
linker places large sections on the outer edges of the binary, making sure they
do not affect affect the distance of small globals to text. The large versions
of ``.rodata``, ``.bss``, and ``.data`` are ``.lrodata``, ``.lbss``, and
``.ldata``, and they are laid out as follows:

.. image:: large_section_layout_pic.png

We try to keep the number of ``PT_LOAD`` segments to a minimum, so we place
large sections next to the small sections with the same RWX permissions when
possible.

``.lbss`` is right after ``.bss`` so that they are merged together and we
minimize the number of segments with ``p_memsz > p_filesz``.

Note that the above applies to PIC code. For less common non-PIC code with
absolute relocations instead of relative relocations, 32-bit relocations
typically assume that symbols are in the lower 2GB of the address space. So for
non-PIC code, large sections should be placed after all small sections to avoid
``.lrodata`` pushing small symbols out of the lower 2GB of the address space.
``-z lrodata-after-bss`` changes the layout to be:

.. image:: large_section_layout_nopic.png
