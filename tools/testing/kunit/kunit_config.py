# SPDX-License-Identifier: GPL-2.0
#
# Builds a .config from a kunitconfig.
#
# Copyright (C) 2019, Google LLC.
# Author: Felix Guo <felixguoxiuping@gmail.com>
# Author: Brendan Higgins <brendanhiggins@google.com>

from dataclasses import dataclass
import re
from typing import Any, Dict, Iterable, List, Tuple

CONFIG_IS_NOT_SET_PATTERN = r'^# CONFIG_(\w+) is not set$'
CONFIG_PATTERN = r'^CONFIG_(\w+)=(\S+|".*")$'

@dataclass(frozen=True)
class KconfigEntry:
	name: str
	value: str

	def __str__(self) -> str:
		if self.value == 'n':
			return f'# CONFIG_{self.name} is not set'
		return f'CONFIG_{self.name}={self.value}'


class KconfigParseError(Exception):
	"""Error parsing Kconfig defconfig or .config."""


class Kconfig:
	"""Represents defconfig or .config specified using the Kconfig language."""

	def __init__(self) -> None:
		self._entries = {}  # type: Dict[str, str]

	def __eq__(self, other: Any) -> bool:
		if not isinstance(other, self.__class__):
			return False
		return self._entries == other._entries

	def __repr__(self) -> str:
		return ','.join(str(e) for e in self.as_entries())

	def as_entries(self) -> Iterable[KconfigEntry]:
		for name, value in self._entries.items():
			yield KconfigEntry(name, value)

	def add_entry(self, name: str, value: str) -> None:
		self._entries[name] = value

	def is_subset_of(self, other: 'Kconfig') -> bool:
		for name, value in self._entries.items():
			b = other._entries.get(name)
			if b is None:
				if value == 'n':
					continue
				return False
			if value != b:
				return False
		return True

	def conflicting_options(self, other: 'Kconfig') -> List[Tuple[KconfigEntry, KconfigEntry]]:
		diff = []  # type: List[Tuple[KconfigEntry, KconfigEntry]]
		for name, value in self._entries.items():
			b = other._entries.get(name)
			if b and value != b:
				pair = (KconfigEntry(name, value), KconfigEntry(name, b))
				diff.append(pair)
		return diff

	def merge_in_entries(self, other: 'Kconfig') -> None:
		for name, value in other._entries.items():
			self._entries[name] = value

	def write_to_file(self, path: str) -> None:
		with open(path, 'a+') as f:
			for e in self.as_entries():
				f.write(str(e) + '\n')

def parse_file(path: str) -> Kconfig:
	with open(path, 'r') as f:
		return parse_from_string(f.read())

def parse_from_string(blob: str) -> Kconfig:
	"""Parses a string containing Kconfig entries."""
	kconfig = Kconfig()
	is_not_set_matcher = re.compile(CONFIG_IS_NOT_SET_PATTERN)
	config_matcher = re.compile(CONFIG_PATTERN)
	for line in blob.split('\n'):
		line = line.strip()
		if not line:
			continue

		match = config_matcher.match(line)
		if match:
			kconfig.add_entry(match.group(1), match.group(2))
			continue

		empty_match = is_not_set_matcher.match(line)
		if empty_match:
			kconfig.add_entry(empty_match.group(1), 'n')
			continue

		if line[0] == '#':
			continue
		raise KconfigParseError('Failed to parse: ' + line)
	return kconfig
