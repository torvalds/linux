#!/usr/bin/env python3

import os.path
import re
import sys

WINDOWS = [
  # --------------------- ----------------- #
  # Runtime ID            Platform          #
  # --------------------- ----------------- #
  ( 'win-x64',            'x64'             ),
  ( 'win-x86',            'Win32'           ),
  # --------------------- ----------------- #
]

MACOS = [
  # --------------------- ----------------- #
  # Runtime ID            Codename          #
  # --------------------- ----------------- #
  ( 'osx-x64',            'sierra'          ),
  # --------------------- ----------------- #
]

LINUX = [
  # --------------------- ----------------- #
  # Runtime ID            Docker Image      #
  # --------------------- ----------------- #
  ( 'linux-x64',          'debian:stretch'  ),
  # --------------------- ----------------- #
]

EXTRAS = [ 'LICENSE', 'AUTHORS', 'ChangeLog' ]

PROPSFILE = 'libsodium.props'
MAKEFILE = 'Makefile'
BUILDDIR = 'build'
CACHEDIR = 'cache'
TEMPDIR = 'temp'

PACKAGE = 'libsodium'
LIBRARY = 'libsodium'

DOCKER = 'sudo docker'

class Version:

  def __init__(self, libsodium_version, package_version):
    self.libsodium_version = libsodium_version
    self.package_version = package_version

    self.builddir = os.path.join(BUILDDIR, libsodium_version)
    self.tempdir = os.path.join(TEMPDIR, libsodium_version)
    self.projfile = os.path.join(self.builddir, '{0}.{1}.pkgproj'.format(PACKAGE, package_version))
    self.propsfile = os.path.join(self.builddir, '{0}.props'.format(PACKAGE))
    self.pkgfile = os.path.join(BUILDDIR, '{0}.{1}.nupkg'.format(PACKAGE, package_version))

class WindowsItem:

  def __init__(self, version, rid, platform):
    self.url = 'https://download.libsodium.org/libsodium/releases/libsodium-{0}-msvc.zip'.format(version.libsodium_version)
    self.cachefile = os.path.join(CACHEDIR, re.sub(r'[^A-Za-z0-9.]', '-', self.url))
    self.packfile = os.path.join(version.builddir, 'runtimes', rid, 'native', LIBRARY + '.dll')
    self.itemfile = '{0}/Release/v140/dynamic/libsodium.dll'.format(platform)
    self.tempdir = os.path.join(version.tempdir, rid)
    self.tempfile = os.path.join(self.tempdir, os.path.normpath(self.itemfile))

  def make(self, f):
    f.write('\n')
    f.write('{0}: {1}\n'.format(self.packfile, self.tempfile))
    f.write('\t@mkdir -p $(dir $@)\n')
    f.write('\tcp -f $< $@\n')
    f.write('\n')
    f.write('{0}: {1}\n'.format(self.tempfile, self.cachefile))
    f.write('\t@mkdir -p $(dir $@)\n')
    f.write('\tcd {0} && unzip -q -DD -o {1} \'{2}\'\n'.format(
      self.tempdir,
      os.path.relpath(self.cachefile, self.tempdir),
      self.itemfile
    ))

class MacOSItem:

  def __init__(self, version, rid, codename):
    self.url = 'https://bintray.com/homebrew/bottles/download_file?file_path=libsodium-{0}.{1}.bottle.tar.gz'.format(version.libsodium_version, codename)
    self.cachefile = os.path.join(CACHEDIR, re.sub(r'[^A-Za-z0-9.]', '-', self.url))
    self.packfile = os.path.join(version.builddir, 'runtimes', rid, 'native', LIBRARY + '.dylib')
    self.itemfile = 'libsodium/{0}/lib/libsodium.dylib'.format(version.libsodium_version)
    self.tempdir = os.path.join(version.tempdir, rid)
    self.tempfile = os.path.join(self.tempdir, os.path.normpath(self.itemfile))

  def make(self, f):
    f.write('\n')
    f.write('{0}: {1}\n'.format(self.packfile, self.tempfile))
    f.write('\t@mkdir -p $(dir $@)\n')
    f.write('\tcp -f $< $@\n')
    f.write('\n')
    f.write('{0}: {1}\n'.format(self.tempfile, self.cachefile))
    f.write('\t@mkdir -p $(dir $@)\n')
    f.write('\tcd {0} && tar xzmf {1} \'{2}\'\n'.format(
      self.tempdir,
      os.path.relpath(self.cachefile, self.tempdir),
      os.path.dirname(self.itemfile)
    ))

class LinuxItem:

  def __init__(self, version, rid, docker_image):
    self.url = 'https://download.libsodium.org/libsodium/releases/libsodium-{0}.tar.gz'.format(version.libsodium_version)
    self.cachefile = os.path.join(CACHEDIR, re.sub(r'[^A-Za-z0-9.]', '-', self.url))
    self.packfile = os.path.join(version.builddir, 'runtimes', rid, 'native', LIBRARY + '.so')
    self.tempdir = os.path.join(version.tempdir, rid)
    self.tempfile = os.path.join(self.tempdir, 'libsodium.so')
    self.docker_image = docker_image
    self.recipe = rid

  def make(self, f):
    recipe = self.recipe
    while not os.path.exists(os.path.join('recipes', recipe)):
      m = re.fullmatch(r'([^.-]+)((([.][^.-]+)*)[.][^.-]+)?([-].*)?', recipe)
      if m.group(5) is None:
        recipe = 'build'
        break
      elif m.group(2) is None:
        recipe = m.group(1)
      else:
        recipe = m.group(1) + m.group(3) + m.group(5)

    f.write('\n')
    f.write('{0}: {1}\n'.format(self.packfile, self.tempfile))
    f.write('\t@mkdir -p $(dir $@)\n')
    f.write('\tcp -f $< $@\n')
    f.write('\n')
    f.write('{0}: {1}\n'.format(self.tempfile, self.cachefile))
    f.write('\t@mkdir -p $(dir $@)\n')
    f.write('\t{0} run --rm '.format(DOCKER) +
            '-v $(abspath recipes):/io/recipes ' +
            '-v $(abspath $<):/io/libsodium.tar.gz ' +
            '-v $(abspath $(dir $@)):/io/output ' +
            '{0} sh -x -e /io/recipes/{1}\n'.format(self.docker_image, recipe))

class ExtraItem:

  def __init__(self, version, filename):
    self.url = 'https://download.libsodium.org/libsodium/releases/libsodium-{0}.tar.gz'.format(version.libsodium_version)
    self.cachefile = os.path.join(CACHEDIR, re.sub(r'[^A-Za-z0-9.]', '-', self.url))
    self.packfile = os.path.join(version.builddir, filename)
    self.itemfile = 'libsodium-{0}/{1}'.format(version.libsodium_version, filename)
    self.tempdir = os.path.join(version.tempdir, 'extras')
    self.tempfile = os.path.join(self.tempdir, os.path.normpath(self.itemfile))

  def make(self, f):
    f.write('\n')
    f.write('{0}: {1}\n'.format(self.packfile, self.tempfile))
    f.write('\t@mkdir -p $(dir $@)\n')
    f.write('\tcp -f $< $@\n')
    f.write('\n')
    f.write('{0}: {1}\n'.format(self.tempfile, self.cachefile))
    f.write('\t@mkdir -p $(dir $@)\n')
    f.write('\tcd {0} && tar xzmf {1} \'{2}\'\n'.format(
      self.tempdir,
      os.path.relpath(self.cachefile, self.tempdir),
      self.itemfile
    ))

def main(args):
  m = re.fullmatch(r'((\d+\.\d+\.\d+)(\.\d+)?)(?:-(\w+(?:[_.-]\w+)*))?', args[1]) if len(args) == 2 else None

  if m is None:
    print('Usage:')
    print('       python3 prepare.py <version>')
    print()
    print('Examples:')
    print('       python3 prepare.py 1.0.16-preview-01')
    print('       python3 prepare.py 1.0.16-preview-02')
    print('       python3 prepare.py 1.0.16-preview-03')
    print('       python3 prepare.py 1.0.16')
    print('       python3 prepare.py 1.0.16.1-preview-01')
    print('       python3 prepare.py 1.0.16.1')
    print('       python3 prepare.py 1.0.16.2')
    return 1

  version = Version(m.group(2), m.group(0))

  items = [ WindowsItem(version, rid, platform)   for (rid, platform) in WINDOWS   ] + \
          [ MacOSItem(version, rid, codename)     for (rid, codename) in MACOS     ] + \
          [ LinuxItem(version, rid, docker_image) for (rid, docker_image) in LINUX ] + \
          [ ExtraItem(version, filename)          for filename in EXTRAS           ]

  downloads = {item.cachefile: item.url for item in items}

  with open(MAKEFILE, 'w') as f:
    f.write('all: {0}\n'.format(version.pkgfile))

    for download in sorted(downloads):
      f.write('\n')
      f.write('{0}:\n'.format(download))
      f.write('\t@mkdir -p $(dir $@)\n')
      f.write('\tcurl -f#Lo $@ \'{0}\'\n'.format(downloads[download]))

    for item in items:
      item.make(f)

    f.write('\n')
    f.write('{0}: {1}\n'.format(version.propsfile, PROPSFILE))
    f.write('\t@mkdir -p $(dir $@)\n')
    f.write('\tcp -f $< $@\n')

    f.write('\n')
    f.write('{0}: {1}\n'.format(version.projfile, version.propsfile))
    f.write('\t@mkdir -p $(dir $@)\n')
    f.write('\techo \'' +
            '<Project Sdk="Microsoft.NET.Sdk">' +
            '<Import Project="{0}" />'.format(os.path.relpath(version.propsfile, os.path.dirname(version.projfile))) +
            '<PropertyGroup>' +
            '<Version>{0}</Version>'.format(version.package_version) +
            '</PropertyGroup>' +
            '</Project>\' > $@\n')

    f.write('\n')
    f.write('{0}:'.format(version.pkgfile))
    f.write(' \\\n\t\t{0}'.format(version.projfile))
    f.write(' \\\n\t\t{0}'.format(version.propsfile))
    for item in items:
      f.write(' \\\n\t\t{0}'.format(item.packfile))
    f.write('\n')
    f.write('\t@mkdir -p $(dir $@)\n')
    f.write('\t{0} run --rm '.format(DOCKER) +
            '-v $(abspath recipes):/io/recipes ' +
            '-v $(abspath $(dir $<)):/io/input ' +
            '-v $(abspath $(dir $@)):/io/output ' +
            '{0} sh -x -e /io/recipes/{1} {2}\n'.format('microsoft/dotnet:2.0-sdk', 'pack', os.path.relpath(version.projfile, version.builddir)))

    f.write('\n')
    f.write('test: {0}\n'.format(version.pkgfile))
    f.write('\t{0} run --rm '.format(DOCKER) +
            '-v $(abspath recipes):/io/recipes ' +
            '-v $(abspath $(dir $<)):/io/packages ' +
            '{0} sh -x -e /io/recipes/{1} "{2}"\n'.format('microsoft/dotnet:2.0-sdk', 'test', version.package_version))

  print('prepared', MAKEFILE, 'to make', version.pkgfile, 'for libsodium', version.libsodium_version)
  return 0

if __name__ == '__main__':
  sys.exit(main(sys.argv))
