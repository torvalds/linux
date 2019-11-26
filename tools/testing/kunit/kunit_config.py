# SPDX-License-Identifier: GPL-2.0
#
# Builds a .config from a kunitconfig.
#
# Copyright (C) 2019, Google LLC.
# Author: Felix Guo <felixguoxiuping@gmail.com>
# Author: Brendan Higgins <brendanhiggins@google.com>

import collections
import re

CONFIG_IS_NOT_SET_PATTERN = r'^# CONFIG_\w+ is not set$'
CONFIG_PATTERN = r'^CONFIG_\w+=\S+$'

KconfigEntryBase = collections.namedtuple('KconfigEntry', ['raw_entry'])


class KconfigEntry(KconfigEntryBase):

	def __str__(self) -> str:
		return self.raw_entry


class KconfigParseError(Exception):
	"""Error parsing Kconfig defconfig or .config."""


class Kconfig(object):
	"""Represents defconfig or .config specified using the Kconfig language."""

	def __init__(self):
		self._entries = []

	def entries(self):
		return set(self._entries)

	def add_entry(self, entry: KconfigEntry) -> None:
		self._entries.append(entry)

	def is_subset_of(self, other: 'Kconfig') -> bool:
		return self.entries().issubset(other.entries())

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
			elif config_matcher.match(line) or is_not_set_matcher.match(line):
				self._entries.append(KconfigEntry(line))
			elif line[0] == '#':
				continue
			else:
				raise KconfigParseError('Failed to parse: ' + line)

	def read_from_file(self, path: str) -> None:
		with open(path, 'r') as f:
			self.parse_from_string(f.read())
