Using LLDB On AArch64 Linux
===========================

This page explains the details of debugging certain AArch64 extensions using
LLDB. If something is not mentioned here, it likely works as you would expect.

This is not a replacement for ptrace and Linux Kernel documentation. This covers
how LLDB has chosen to use those things and how that effects your experience as
a user.

Scalable Vector Extension (SVE)
-------------------------------

See `here <https://developer.arm.com/Architectures/Scalable%20Vector%20Extensions>`__
to learn about the extension and `here <https://kernel.org/doc/html/latest/arch/arm64/sve.html>`__
for the Linux Kernel's handling of it.

In LLDB you will be able to see the following new registers:

* ``z0-z31`` vector registers, each one has size equal to the vector length.
* ``p0-p15`` predicate registers, each one containing 1 bit per byte in the vector
  length. Making each one vector length / 8 sized.
* ``ffr`` the first fault register, same size as a predicate register.
* ``vg``, the vector length in "granules". Each granule is 8 bytes.

.. code-block::

       Scalable Vector Extension Registers:
             vg = 0x0000000000000002
             z0 = {0x01 0x01 0x01 0x01 0x01 0x01 0x01 0x01 <...> }
           <...>
             p0 = {0xff 0xff}
           <...>
            ffr = {0xff 0xff}

The example above has a vector length of 16 bytes. Within LLDB you will always
see "vg" as in the ``vg`` register, which is 2 in this case (8*2 = 16).
Elsewhere in kernel code or applications, you may see "vq" which is the vector
length in quadwords (16 bytes). Where you see "vl", it is in bytes.

While you can count the size of a P or Z register, it is intended that ``vg`` be
used to find the current vector length.

Changing the Vector Length
..........................

The ``vg`` register can be written during a debug session. Writing the current
vector length changes nothing. If you increase the vector length, the registers
will likely be reset to 0. If you decrease it, LLDB will truncate the Z
registers but everything else will be reset to 0.

You should not assume that SVE state after changing the vector length is in any
way the same as it was previously. Whether that is done from within the
debuggee, or by LLDB. If you need to change the vector length, do so before a
function's first use of SVE.

Z Register Presentation
.......................

LLDB makes no attempt to predict how SVE Z registers will be used. Since LLDB
does not know what sort of elements future instructions will interpret the
register as. It therefore does not change the visualisation of the register
and always defaults to showing a vector of byte sized elements.

If you know what format you are going to use, give a format option::

  (lldb) register read z0 -f uint32_t[]
      z0 = {0x01010101 0x01010101 0x01010101 0x01010101}

FPSIMD and SVE Modes
....................

Prior to the debugee's first use of SVE, it is in what the Linux Kernel terms
SIMD mode. Only the FPU is being used. In this state LLDB will still show the
SVE registers however the values are simply the FPU values zero extended up to
the vector length.

On first access to SVE, the process goes into SVE mode. Now the Z values are
in the real Z registers.

You can also trigger this with LLDB by writing to an SVE register. Note that
there is no way to undo this change from within LLDB. However, the debugee
itself could do something to end up back in SIMD mode.

Expression evaluation
.....................

If you evaluate an expression, all SVE state is saved prior to, and restored
after the expression has been evaluated. Including the register values and
vector length.

Scalable Matrix Extension (SME)
-------------------------------

See `here <https://community.arm.com/arm-community-blogs/b/architectures-and-processors-blog/posts/scalable-matrix-extension-armv9-a-architecture>`__
to learn about the extension and `here <https://kernel.org/doc/html/latest/arch/arm64/sme.html>`__
for the Linux Kernel's handling of it.

SME adds a "Streaming Mode" to SVE, and this mode has its own vector length
known as the "Streaming Vector Length".

In LLDB you will see the following new registers:

* ``tpidr2``, an extra per thread pointer reserved for use by the SME ABI.
  This is not scalable, just pointer sized aka 64 bit.
* ``z0-z31`` streaming SVE registers. These have the same names as the
  non-streaming registers and therefore you will only see the active set in
  LLDB. You cannot read or write the inactive mode's registers. Their size
  is the same as the streaming vector length.
* ``za`` the Array Storage register. The "Matrix" part of "Scalable Matrix
  Extension". This is a square made up of rows of length equal to the streaming
  vector length (svl). Meaning that the total size is svl * svl.
* ``svcr`` the Streaming Vector Control Register. This is actually a pseduo
  register but it matches the content of the architecturaly defined ``SVCR``.
  This is the register you should use to check whether streaming mode and/or
  ``za`` is active. This register is read only.
* ``svg`` the streaming vector length in granules. This value is not connected
  to the vector length of non-streaming mode and may change independently. This
  register is read only.

.. note::
  While in non-streaming mode, the ``vg`` register shows the non-streaming
  vector length, and the ``svg`` register shows the streaming vector length.
  When in streaming mode, both ``vg`` and ``svg`` show the streaming mode vector
  length. Therefore it is not possible at this time to read the non-streaming
  vector length within LLDB, while in streaming mode. This is a limitation of
  the LLDB implementation not the architecture, which stores both lengths
  independently.

In the example below, the streaming vector length is 16 bytes and we are in
streaming mode. Note that bits 0 and 1 of ``svcr`` are set, indicating that we
are in streaming mode and ZA is active. ``vg`` and ``svg`` report the same value
as ``vg`` is showing the streaming mode vector length::

  Scalable Vector Extension Registers:
        vg = 0x0000000000000002
        z0 = {0x01 0x01 0x01 0x01 0x01 0x01 0x01 0x01 0x01 0x01 0x01 <...> }
        <...>
        p0 = {0xff 0xff}
        <...>
       ffr = {0xff 0xff}

  <...>

  Thread Local Storage Registers:
       tpidr = 0x0000fffff7ff4320
      tpidr2 = 0x1122334455667788

  Scalable Matrix Extension Registers:
         svg = 0x0000000000000002
        svcr = 0x0000000000000003
          za = {0x00 <...> 0x00}

Changing the Streaming Vector Length
....................................

To reduce complexity for LLDB, ``svg`` is read only. This means that you can
only change the streaming vector length using LLDB when the debugee is in
streaming mode.

As for non-streaming SVE, doing so will essentially make the content of the SVE
registers undefined. It will also disable ZA, which follows what the Linux
Kernel does.

Visibility of an Inactive ZA Register
.....................................

LLDB does not handle registers that can come and go at runtime (SVE changes
size but it does not dissappear). Therefore when ``za`` is not enabled, LLDB
will return a block of 0s instead. This block will match the expected size of
``za``::

  (lldb) register read za svg svcr
      za = {0x00 0x00 0x00 0x00 0x00 0x00 0x00 0x00 0x00 <...> }
     svg = 0x0000000000000002
    svcr = 0x0000000000000001

Note that ``svcr`` bit 2 is not set, meaning ``za`` is inactive.

If you were to write to ``za`` from LLDB, ``za`` will be made active. There is
no way from within LLDB to reverse this change. As for changing the vector
length, the debugee could still do something that would disable ``za`` again.

If you want to know whether ``za`` is active or not, refer to bit 2 of the
``svcr`` register, otherwise known as ``SVCR.ZA``.

ZA Register Presentation
........................

As for SVE, LLDB does not know how the debugee will use ``za``, and therefore
does not know how it would be best to display it. At any time any given
instrucion could interpret its contents as many kinds and sizes of data.

So LLDB will default to showing  ``za`` as one large vector of individual bytes.
You can override this with a format option (see the SVE example above).

Expression Evaluation
.....................

The mode (streaming or non-streaming), streaming vector length and ZA state will
be restored after expression evaluation. On top of all the things saved for SVE
in general.

Scalable Matrix Extension (SME2)
--------------------------------

The Scalable Matrix Extension 2 is documented in the same architecture
specification as SME, and covered by the same kernel documentation page as SME.

SME2 adds 1 new register, ``zt0``. This register is a fixed size 512 bit
register that is used by new instructions added in SME2. It is shown in LLDB in
the existing SME register set.

``zt0`` can be active or inactive, as ``za`` can. The same ``SVCR.ZA`` bit
controls this. An inactive ``zt0`` is shown as 0s, like ``za`` is. Though in
``zt0``'s case, LLDB does not need to fake the value. Ptrace already returns a
block of 0s for an inactive ``zt0``.

Like ``za``, writing to an inactive ``zt0`` will enable it and ``za``. This can
be done from within LLDB. If the write is instead to ``za``, ``zt0`` becomes
active but with a value of all 0s.

Since ``svcr`` is read only, there is no way at this time to deactivate the
registers from within LLDB (though of course a running process can still do
this).

To check whether ``zt0`` is active, refer to ``SVCR.ZA`` and not to the value of
``zt0``.

ZT0 Register Presentation
.........................

As for ``za``, the meaning of ``zt0`` depends on the instructions used with it,
so LLDB does not attempt to guess this and defaults to showing it as a vector of
bytes.

Expression Evaluation
.....................

``zt0``'s value and whether it is active or not will be saved prior to
expression evaluation and restored afterwards.