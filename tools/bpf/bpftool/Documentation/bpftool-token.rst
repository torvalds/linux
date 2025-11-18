.. SPDX-License-Identifier: (GPL-2.0-only OR BSD-2-Clause)

================
bpftool-token
================
-------------------------------------------------------------------------------
tool for inspection and simple manipulation of eBPF tokens
-------------------------------------------------------------------------------

:Manual section: 8

.. include:: substitutions.rst

SYNOPSIS
========

**bpftool** [*OPTIONS*] **token** *COMMAND*

*OPTIONS* := { |COMMON_OPTIONS| }

*COMMANDS* := { **show** | **list** | **help** }

TOKEN COMMANDS
===============

| **bpftool** **token** { **show** | **list** }
| **bpftool** **token help**
|

DESCRIPTION
===========
bpftool token { show | list }
    List BPF token information for each *bpffs* mount point containing token
    information on the system. Information include mount point path, allowed
    **bpf**\ () system call commands, maps, programs, and attach types for the
    token.

bpftool prog help
    Print short help message.

OPTIONS
========
.. include:: common_options.rst

EXAMPLES
========
|
| **# mkdir -p /sys/fs/bpf/token**
| **# mount -t bpf bpffs /sys/fs/bpf/token** \
|         **-o delegate_cmds=prog_load:map_create** \
|         **-o delegate_progs=kprobe** \
|         **-o delegate_attachs=xdp**
| **# bpftool token list**

::

    token_info  /sys/fs/bpf/token
            allowed_cmds:
              map_create          prog_load
            allowed_maps:
            allowed_progs:
              kprobe
            allowed_attachs:
              xdp
