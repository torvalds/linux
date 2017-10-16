================
bpftool-map
================
-------------------------------------------------------------------------------
tool for inspection and simple manipulation of eBPF maps
-------------------------------------------------------------------------------

:Manual section: 8

SYNOPSIS
========

	**bpftool** **map** *COMMAND*

	*COMMANDS* :=
	{ show | dump | update | lookup | getnext | delete | pin | help }

MAP COMMANDS
=============

|	**bpftool** map show   [*MAP*]
|	**bpftool** map dump    *MAP*
|	**bpftool** map update  *MAP*  key *BYTES*   value *VALUE* [*UPDATE_FLAGS*]
|	**bpftool** map lookup  *MAP*  key *BYTES*
|	**bpftool** map getnext *MAP* [key *BYTES*]
|	**bpftool** map delete  *MAP*  key *BYTES*
|	**bpftool** map pin     *MAP*  *FILE*
|	**bpftool** map help
|
|	*MAP* := { id MAP_ID | pinned FILE }
|	*VALUE* := { BYTES | MAP | PROGRAM }
|	*UPDATE_FLAGS* := { any | exist | noexist }

DESCRIPTION
===========
	**bpftool map show**   [*MAP*]
		  Show information about loaded maps.  If *MAP* is specified
		  show information only about given map, otherwise list all
		  maps currently loaded on the system.

		  Output will start with map ID followed by map type and
		  zero or more named attributes (depending on kernel version).

	**bpftool map dump**    *MAP*
		  Dump all entries in a given *MAP*.

	**bpftool map update**  *MAP*  **key** *BYTES*   **value** *VALUE* [*UPDATE_FLAGS*]
		  Update map entry for a given *KEY*.

		  *UPDATE_FLAGS* can be one of: **any** update existing entry
		  or add if doesn't exit; **exist** update only if entry already
		  exists; **noexist** update only if entry doesn't exist.

	**bpftool map lookup**  *MAP*  **key** *BYTES*
		  Lookup **key** in the map.

	**bpftool map getnext** *MAP* [**key** *BYTES*]
		  Get next key.  If *key* is not specified, get first key.

	**bpftool map delete**  *MAP*  **key** *BYTES*
		  Remove entry from the map.

	**bpftool map pin**     *MAP*  *FILE*
		  Pin map *MAP* as *FILE*.

		  Note: *FILE* must be located in *bpffs* mount.

	**bpftool map help**
		  Print short help message.

EXAMPLES
========
**# bpftool map show**
::

  10: hash  name some_map  flags 0x0
	key 4B  value 8B  max_entries 2048  memlock 167936B

**# bpftool map update id 10 key 13 00 07 00 value 02 00 00 00 01 02 03 04**

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
	**bpftool**\ (8), **bpftool-prog**\ (8)
