# SPDX-License-Identifier: GPL-2.0
#
# Builds a .config from a kunitconfig.
#
# Copyright (C) 2019, Google LLC.
# Author: Felix Guo <felixguoxiuping@gmail.com>
# Author: Brendan Higgins <brendanhiggins@google.com>

import collections
import re
from typing import List, Set

CONFIG_IS_NOT_SET_PATTERN = r'^# CONFIG_(\w+) is not set$'
CONFIG_PATTERN = r'^CONFIG_(\w+)=(\S+|".*")$'

KconfigEntryBase = collections.namedtuple('KconfigEntry', ['name', 'value'])

class KconfigEntry(KconfigEntryBase):

	def __str__(self) -> str:
		if self.value == 'n':
			return r'# CONFIG_%s is not set' % (self.name)
		else:
			return r'CONFIG_%s=%s' % (self.name, self.value)


class KconfigParseError(Exception):
	"""Error parsing Kconfig defconfig or .config."""


class Kconfig(object):
	"""Represents defconfig or .config specified using the Kconfig language."""

	def __init__(self) -> None:
		self._entries = []  # type: List[KconfigEntry]

	def entries(self) -> Set[KconfigEntry]:
		return set(self._entries)

	def add_entry(self, entry: KconfigEntry) -> None:
		self._entries.append(entry)

	def is_subset_of(self, other: 'Kconfig') -> bool:
		other_dict = {e.name: e.value for e in other.entries()}
		for a in self.entries():
			b = other_dict.get(a.name)
			if b is None:
				if a.value == 'n':
					continue
				return False
			elif a.value != b:
				return False
		return True

	def write_to_file(self, path: str) -> None:
		with open(path, 'w') as f:
			for entry in self.entries():
				f.write(str(entry) + '\n')

	def parse_from_string(self, blob: str) -> None:
		"""Parses a string containing KconfigEntrys and populates this Kconfig."""
		self._entries = []
		is_not_set_matcher = re.compile(CONFIG_IS_NOT_SET_PATTERN)
		config_matcher = re.compile(CONFIG_PATTERN)
		for line in blob.split('\n'):
			line = line.strip()
			if not line:
				continue

			match = config_matcher.match(line)
			if match:
				entry = KconfigEntry(match.group(1), match.group(2))
				self.add_entry(entry)
				continue

			empty_match = is_not_set_matcher.match(line)
			if empty_match:
				entry = KconfigEntry(empty_match.group(1), 'n')
				self.add_entry(entry)
				continue

			if line[0] == '#':
				continue
			else:
				raise KconfigParseError('Failed to parse: ' + line)

	def read_from_file(self, path: str) -> None:
		with open(path, 'r') as f:
			self.parse_from_string(f.read())
