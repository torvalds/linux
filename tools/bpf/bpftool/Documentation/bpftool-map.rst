================
bpftool-map
================
-------------------------------------------------------------------------------
tool for inspection and simple manipulation of eBPF maps
-------------------------------------------------------------------------------

:Manual section: 8

SYNOPSIS
========

	**bpftool** [*OPTIONS*] **map** *COMMAND*

	*OPTIONS* := { { **-j** | **--json** } [{ **-p** | **--pretty** }] | { **-f** | **--bpffs** } }

	*COMMANDS* :=
	{ **show** | **list** | **dump** | **update** | **lookup** | **getnext** | **delete**
	| **pin** | **help** }

MAP COMMANDS
=============

|	**bpftool** **map { show | list }**   [*MAP*]
|	**bpftool** **map dump**    *MAP*
|	**bpftool** **map update**  *MAP*  **key** *DATA*   **value** *VALUE* [*UPDATE_FLAGS*]
|	**bpftool** **map lookup**  *MAP*  **key** *DATA*
|	**bpftool** **map getnext** *MAP* [**key** *DATA*]
|	**bpftool** **map delete**  *MAP*  **key** *DATA*
|	**bpftool** **map pin**     *MAP*  *FILE*
|	**bpftool** **map help**
|
|	*MAP* := { **id** *MAP_ID* | **pinned** *FILE* }
|	*DATA* := { [**hex**] *BYTES* }
|	*PROG* := { **id** *PROG_ID* | **pinned** *FILE* | **tag** *PROG_TAG* }
|	*VALUE* := { *DATA* | *MAP* | *PROG* }
|	*UPDATE_FLAGS* := { **any** | **exist** | **noexist** }

DESCRIPTION
===========
	**bpftool map { show | list }**   [*MAP*]
		  Show information about loaded maps.  If *MAP* is specified
		  show information only about given map, otherwise list all
		  maps currently loaded on the system.

		  Output will start with map ID followed by map type and
		  zero or more named attributes (depending on kernel version).

	**bpftool map dump**    *MAP*
		  Dump all entries in a given *MAP*.

	**bpftool map update**  *MAP*  **key** *DATA*   **value** *VALUE* [*UPDATE_FLAGS*]
		  Update map entry for a given *KEY*.

		  *UPDATE_FLAGS* can be one of: **any** update existing entry
		  or add if doesn't exit; **exist** update only if entry already
		  exists; **noexist** update only if entry doesn't exist.

		  If the **hex** keyword is provided in front of the bytes
		  sequence, the bytes are parsed as hexadeximal values, even if
		  no "0x" prefix is added. If the keyword is not provided, then
		  the bytes are parsed as decimal values, unless a "0x" prefix
		  (for hexadecimal) or a "0" prefix (for octal) is provided.

	**bpftool map lookup**  *MAP*  **key** *DATA*
		  Lookup **key** in the map.

	**bpftool map getnext** *MAP* [**key** *DATA*]
		  Get next key.  If *key* is not specified, get first key.

	**bpftool map delete**  *MAP*  **key** *DATA*
		  Remove entry from the map.

	**bpftool map pin**     *MAP*  *FILE*
		  Pin map *MAP* as *FILE*.

		  Note: *FILE* must be located in *bpffs* mount.

	**bpftool map help**
		  Print short help message.

OPTIONS
=======
	-h, --help
		  Print short generic help message (similar to **bpftool help**).

	-v, --version
		  Print version number (similar to **bpftool version**).

	-j, --json
		  Generate JSON output. For commands that cannot produce JSON, this
		  option has no effect.

	-p, --pretty
		  Generate human-readable JSON output. Implies **-j**.

	-f, --bpffs
		  Show file names of pinned maps.

EXAMPLES
========
**# bpftool map show**
::

  10: hash  name some_map  flags 0x0
	key 4B  value 8B  max_entries 2048  memlock 167936B

The following three commands are equivalent:

|
| **# bpftool map update id 10 key hex   20   c4   b7   00 value hex   0f   ff   ff   ab   01   02   03   4c**
| **# bpftool map update id 10 key     0x20 0xc4 0xb7 0x00 value     0x0f 0xff 0xff 0xab 0x01 0x02 0x03 0x4c**
| **# bpftool map update id 10 key       32  196  183    0 value       15  255  255  171    1    2    3   76**

**# bpftool map lookup id 10 key 0 1 2 3**

::

  key: 00 01 02 03 value: 00 01 02 03 04 05 06 07


**# bpftool map dump id 10**
::

  key: 00 01 02 03  value: 00 01 02 03 04 05 06 07
  key: 0d 00 07 00  value: 02 00 00 00 01 02 03 04
  Found 2 elements

**# bpftool map getnext id 10 key 0 1 2 3**
::

  key:
  00 01 02 03
  next key:
  0d 00 07 00

|
| **# mount -t bpf none /sys/fs/bpf/**
| **# bpftool map pin id 10 /sys/fs/bpf/map**
| **# bpftool map del pinned /sys/fs/bpf/map key 13 00 07 00**

SEE ALSO
========
	**bpftool**\ (8), **bpftool-prog**\ (8), **bpftool-cgroup**\ (8)
