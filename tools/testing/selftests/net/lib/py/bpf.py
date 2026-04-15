# SPDX-License-Identifier: GPL-2.0

"""
BPF helper utilities for kernel selftests.

Provides common operations for interacting with BPF maps and programs
via bpftool, used by XDP and other BPF-based test files.
"""

from .utils import bpftool

def _format_hex_bytes(value):
    """
    Helper function that converts an integer into a formatted hexadecimal byte string.

    Args:
        value: An integer representing the number to be converted.

    Returns:
        A string representing hexadecimal equivalent of value, with bytes separated by spaces.
    """
    hex_str = value.to_bytes(4, byteorder='little', signed=True)
    return ' '.join(f'{byte:02x}' for byte in hex_str)


def bpf_map_set(map_name, key, value):
    """
    Updates an XDP map with a given key-value pair using bpftool.

    Args:
        map_name: The name of the XDP map to update.
        key: The key to update in the map, formatted as a hexadecimal string.
        value: The value to associate with the key, formatted as a hexadecimal string.
    """
    key_formatted = _format_hex_bytes(key)
    value_formatted = _format_hex_bytes(value)
    bpftool(
        f"map update name {map_name} key hex {key_formatted} value hex {value_formatted}"
    )

def bpf_map_dump(map_id):
    """Dump all entries of a BPF array map.

    Args:
        map_id: Numeric map ID (as returned by bpftool prog show).

    Returns:
        A dict mapping formatted key (int) to formatted value (int).
    """
    raw = bpftool(f"map dump id {map_id}", json=True)
    return {e["formatted"]["key"]: e["formatted"]["value"] for e in raw}


def bpf_prog_map_ids(prog_id):
    """Get the map name-to-ID mapping for a loaded BPF program.

    Args:
        prog_id: Numeric program ID.

    Returns:
        A dict mapping map name (str) to map ID (int).
    """
    map_ids = bpftool(f"prog show id {prog_id}", json=True)["map_ids"]
    maps = {}
    for mid in map_ids:
        name = bpftool(f"map show id {mid}", json=True)["name"]
        maps[name] = mid
    return maps
