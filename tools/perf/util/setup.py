#!/usr/bin/python2

from distutils.core import setup, Extension

perf = Extension('perf',
		  sources = ['util/python.c', 'util/ctype.c', 'util/evlist.c',
			     'util/evsel.c', 'util/cpumap.c', 'util/thread_map.c',
			     'util/util.c', 'util/xyarray.c'],
		  include_dirs = ['util/include'],
		  extra_compile_args = ['-fno-strict-aliasing', '-Wno-write-strings'])

setup(name='perf',
      version='0.1',
      description='Interface with the Linux profiling infrastructure',
      author='Arnaldo Carvalho de Melo',
      author_email='acme@redhat.com',
      license='GPLv2',
      url='http://perf.wiki.kernel.org',
      ext_modules=[perf])
