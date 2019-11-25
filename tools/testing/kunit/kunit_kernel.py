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

import kunit_config

KCONFIG_PATH = '.config'
KUNITCONFIG_PATH = 'kunitconfig'

class ConfigError(Exception):
	"""Represents an error trying to configure the Linux kernel."""


class BuildError(Exception):
	"""Represents an error trying to build the Linux kernel."""


class LinuxSourceTreeOperations(object):
	"""An abstraction over command line operations performed on a source tree."""

	def make_mrproper(self):
		try:
			subprocess.check_output(['make', 'mrproper'])
		except OSError as e:
			raise ConfigError('Could not call make command: ' + e)
		except subprocess.CalledProcessError as e:
			raise ConfigError(e.output)

	def make_olddefconfig(self, build_dir):
		command = ['make', 'ARCH=um', 'olddefconfig']
		if build_dir:
			command += ['O=' + build_dir]
		try:
			subprocess.check_output(command)
		except OSError as e:
			raise ConfigError('Could not call make command: ' + e)
		except subprocess.CalledProcessError as e:
			raise ConfigError(e.output)

	def make(self, jobs, build_dir):
		command = ['make', 'ARCH=um', '--jobs=' + str(jobs)]
		if build_dir:
			command += ['O=' + build_dir]
		try:
			subprocess.check_output(command)
		except OSError as e:
			raise BuildError('Could not call execute make: ' + e)
		except subprocess.CalledProcessError as e:
			raise BuildError(e.output)

	def linux_bin(self, params, timeout, build_dir):
		"""Runs the Linux UML binary. Must be named 'linux'."""
		linux_bin = './linux'
		if build_dir:
			linux_bin = os.path.join(build_dir, 'linux')
		process = subprocess.Popen(
			[linux_bin] + params,
			stdin=subprocess.PIPE,
			stdout=subprocess.PIPE,
			stderr=subprocess.PIPE)
		process.wait(timeout=timeout)
		return process


def get_kconfig_path(build_dir):
	kconfig_path = KCONFIG_PATH
	if build_dir:
		kconfig_path = os.path.join(build_dir, KCONFIG_PATH)
	return kconfig_path

class LinuxSourceTree(object):
	"""Represents a Linux kernel source tree with KUnit tests."""

	def __init__(self):
		self._kconfig = kunit_config.Kconfig()
		self._kconfig.read_from_file(KUNITCONFIG_PATH)
		self._ops = LinuxSourceTreeOperations()

	def clean(self):
		try:
			self._ops.make_mrproper()
		except ConfigError as e:
			logging.error(e)
			return False
		return True

	def build_config(self, build_dir):
		kconfig_path = get_kconfig_path(build_dir)
		if build_dir and not os.path.exists(build_dir):
			os.mkdir(build_dir)
		self._kconfig.write_to_file(kconfig_path)
		try:
			self._ops.make_olddefconfig(build_dir)
		except ConfigError as e:
			logging.error(e)
			return False
		validated_kconfig = kunit_config.Kconfig()
		validated_kconfig.read_from_file(kconfig_path)
		if not self._kconfig.is_subset_of(validated_kconfig):
			logging.error('Provided Kconfig is not contained in validated .config!')
			return False
		return True

	def build_reconfig(self, build_dir):
		"""Creates a new .config if it is not a subset of the kunitconfig."""
		kconfig_path = get_kconfig_path(build_dir)
		if os.path.exists(kconfig_path):
			existing_kconfig = kunit_config.Kconfig()
			existing_kconfig.read_from_file(kconfig_path)
			if not self._kconfig.is_subset_of(existing_kconfig):
				print('Regenerating .config ...')
				os.remove(kconfig_path)
				return self.build_config(build_dir)
			else:
				return True
		else:
			print('Generating .config ...')
			return self.build_config(build_dir)

	def build_um_kernel(self, jobs, build_dir):
		try:
			self._ops.make_olddefconfig(build_dir)
			self._ops.make(jobs, build_dir)
		except (ConfigError, BuildError) as e:
			logging.error(e)
			return False
		used_kconfig = kunit_config.Kconfig()
		used_kconfig.read_from_file(get_kconfig_path(build_dir))
		if not self._kconfig.is_subset_of(used_kconfig):
			logging.error('Provided Kconfig is not contained in final config!')
			return False
		return True

	def run_kernel(self, args=[], timeout=None, build_dir=None):
		args.extend(['mem=256M'])
		process = self._ops.linux_bin(args, timeout, build_dir)
		with open('test.log', 'w') as f:
			for line in process.stdout:
				f.write(line.rstrip().decode('ascii') + '\n')
				yield line.rstrip().decode('ascii')
