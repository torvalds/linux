# SPDX-License-Identifier: GPL-2.0
#
# Runs UML kernel, collects output, and handles errors.
#
# Copyright (C) 2019, Google LLC.
# Author: Felix Guo <felixguoxiuping@gmail.com>
# Author: Brendan Higgins <brendanhiggins@google.com>

import logging
import subprocess
import os
import shutil
import signal
from typing import Iterator

from contextlib import ExitStack

import kunit_config
import kunit_parser

KCONFIG_PATH = '.config'
KUNITCONFIG_PATH = '.kunitconfig'
DEFAULT_KUNITCONFIG_PATH = 'arch/um/configs/kunit_defconfig'
BROKEN_ALLCONFIG_PATH = 'tools/testing/kunit/configs/broken_on_uml.config'
OUTFILE_PATH = 'test.log'

def get_file_path(build_dir, default):
	if build_dir:
		default = os.path.join(build_dir, default)
	return default

class ConfigError(Exception):
	"""Represents an error trying to configure the Linux kernel."""


class BuildError(Exception):
	"""Represents an error trying to build the Linux kernel."""


class LinuxSourceTreeOperations(object):
	"""An abstraction over command line operations performed on a source tree."""

	def make_mrproper(self) -> None:
		try:
			subprocess.check_output(['make', 'mrproper'], stderr=subprocess.STDOUT)
		except OSError as e:
			raise ConfigError('Could not call make command: ' + str(e))
		except subprocess.CalledProcessError as e:
			raise ConfigError(e.output.decode())

	def make_olddefconfig(self, build_dir, make_options) -> None:
		command = ['make', 'ARCH=um', 'olddefconfig']
		if make_options:
			command.extend(make_options)
		if build_dir:
			command += ['O=' + build_dir]
		try:
			subprocess.check_output(command, stderr=subprocess.STDOUT)
		except OSError as e:
			raise ConfigError('Could not call make command: ' + str(e))
		except subprocess.CalledProcessError as e:
			raise ConfigError(e.output.decode())

	def make_allyesconfig(self, build_dir, make_options) -> None:
		kunit_parser.print_with_timestamp(
			'Enabling all CONFIGs for UML...')
		command = ['make', 'ARCH=um', 'allyesconfig']
		if make_options:
			command.extend(make_options)
		if build_dir:
			command += ['O=' + build_dir]
		process = subprocess.Popen(
			command,
			stdout=subprocess.DEVNULL,
			stderr=subprocess.STDOUT)
		process.wait()
		kunit_parser.print_with_timestamp(
			'Disabling broken configs to run KUnit tests...')
		with ExitStack() as es:
			config = open(get_kconfig_path(build_dir), 'a')
			disable = open(BROKEN_ALLCONFIG_PATH, 'r').read()
			config.write(disable)
		kunit_parser.print_with_timestamp(
			'Starting Kernel with all configs takes a few minutes...')

	def make(self, jobs, build_dir, make_options) -> None:
		command = ['make', 'ARCH=um', '--jobs=' + str(jobs)]
		if make_options:
			command.extend(make_options)
		if build_dir:
			command += ['O=' + build_dir]
		try:
			proc = subprocess.Popen(command,
						stderr=subprocess.PIPE,
						stdout=subprocess.DEVNULL)
		except OSError as e:
			raise BuildError('Could not call make command: ' + str(e))
		_, stderr = proc.communicate()
		if proc.returncode != 0:
			raise BuildError(stderr.decode())
		if stderr:  # likely only due to build warnings
			print(stderr.decode())

	def linux_bin(self, params, timeout, build_dir) -> None:
		"""Runs the Linux UML binary. Must be named 'linux'."""
		linux_bin = get_file_path(build_dir, 'linux')
		outfile = get_outfile_path(build_dir)
		with open(outfile, 'w') as output:
			process = subprocess.Popen([linux_bin] + params,
						   stdout=output,
						   stderr=subprocess.STDOUT)
			process.wait(timeout)

def get_kconfig_path(build_dir) -> str:
	return get_file_path(build_dir, KCONFIG_PATH)

def get_kunitconfig_path(build_dir) -> str:
	return get_file_path(build_dir, KUNITCONFIG_PATH)

def get_outfile_path(build_dir) -> str:
	return get_file_path(build_dir, OUTFILE_PATH)

class LinuxSourceTree(object):
	"""Represents a Linux kernel source tree with KUnit tests."""

	def __init__(self, build_dir: str, load_config=True, defconfig=DEFAULT_KUNITCONFIG_PATH) -> None:
		signal.signal(signal.SIGINT, self.signal_handler)

		self._ops = LinuxSourceTreeOperations()

		if not load_config:
			return

		kunitconfig_path = get_kunitconfig_path(build_dir)
		if not os.path.exists(kunitconfig_path):
			shutil.copyfile(defconfig, kunitconfig_path)

		self._kconfig = kunit_config.Kconfig()
		self._kconfig.read_from_file(kunitconfig_path)

	def clean(self) -> bool:
		try:
			self._ops.make_mrproper()
		except ConfigError as e:
			logging.error(e)
			return False
		return True

	def validate_config(self, build_dir) -> bool:
		kconfig_path = get_kconfig_path(build_dir)
		validated_kconfig = kunit_config.Kconfig()
		validated_kconfig.read_from_file(kconfig_path)
		if not self._kconfig.is_subset_of(validated_kconfig):
			invalid = self._kconfig.entries() - validated_kconfig.entries()
			message = 'Provided Kconfig is not contained in validated .config. Following fields found in kunitconfig, ' \
					  'but not in .config: %s' % (
					', '.join([str(e) for e in invalid])
			)
			logging.error(message)
			return False
		return True

	def build_config(self, build_dir, make_options) -> bool:
		kconfig_path = get_kconfig_path(build_dir)
		if build_dir and not os.path.exists(build_dir):
			os.mkdir(build_dir)
		self._kconfig.write_to_file(kconfig_path)
		try:
			self._ops.make_olddefconfig(build_dir, make_options)
		except ConfigError as e:
			logging.error(e)
			return False
		return self.validate_config(build_dir)

	def build_reconfig(self, build_dir, make_options) -> bool:
		"""Creates a new .config if it is not a subset of the .kunitconfig."""
		kconfig_path = get_kconfig_path(build_dir)
		if os.path.exists(kconfig_path):
			existing_kconfig = kunit_config.Kconfig()
			existing_kconfig.read_from_file(kconfig_path)
			if not self._kconfig.is_subset_of(existing_kconfig):
				print('Regenerating .config ...')
				os.remove(kconfig_path)
				return self.build_config(build_dir, make_options)
			else:
				return True
		else:
			print('Generating .config ...')
			return self.build_config(build_dir, make_options)

	def build_um_kernel(self, alltests, jobs, build_dir, make_options) -> bool:
		try:
			if alltests:
				self._ops.make_allyesconfig(build_dir, make_options)
			self._ops.make_olddefconfig(build_dir, make_options)
			self._ops.make(jobs, build_dir, make_options)
		except (ConfigError, BuildError) as e:
			logging.error(e)
			return False
		return self.validate_config(build_dir)

	def run_kernel(self, args=[], build_dir='', timeout=None) -> Iterator[str]:
		args.extend(['mem=1G', 'console=tty'])
		self._ops.linux_bin(args, timeout, build_dir)
		outfile = get_outfile_path(build_dir)
		subprocess.call(['stty', 'sane'])
		with open(outfile, 'r') as file:
			for line in file:
				yield line

	def signal_handler(self, sig, frame) -> None:
		logging.error('Build interruption occurred. Cleaning console.')
		subprocess.call(['stty', 'sane'])
