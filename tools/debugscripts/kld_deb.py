#!/usr/local/bin/python
#
# Copyright 2004 John-Mark Gurney
# All rights reserved.
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions
# are met:
# 1. Redistributions of source code must retain the above copyright
#    notice, this list of conditions and the following disclaimer.
# 2. Redistributions in binary form must reproduce the above copyright
#    notice, this list of conditions and the following disclaimer in the
#    documentation and/or other materials provided with the distribution.
#
# THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
# ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
# IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
# ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
# FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
# DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
# OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
# HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
# LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
# OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
# SUCH DAMAGE.
#
# $FreeBSD$

from __future__ import print_function
import sys
import os
import popen2
import re

gdb_cmd = 'kgdb %(p)s/kernel.debug %(core)s | tee /tmp/gdb.log' 
#GDB regex
filenamere = re.compile(r'filename\s+=\s+0x[0-9a-f]+\s("(?P<fn>[^"]+)"|(?P<error><[^>]*>))', re.M)
addressre = re.compile(r'address\s+=\s+(?P<ad>0x[0-9a-f]+)', re.M)
nextre = re.compile(r'tqe_next\s+=\s+(?P<ad>0x[0-9a-f]+)', re.M)
printre = re.compile(r'\$\d+\s+=\s+')

#Paths to search for ko's/debugs
kld_debug_paths = []

if len(sys.argv[1:]) < 2:
	print('Usage: prog <kerncomp> <core> [<paths>]')
	sys.exit(1)

#Get the base modules path
pfs = sys.argv[1].split('/')
try:
	i = 0
	while 1:                 
		i = i + pfs[i:].index('sys') + 1
except:
	pass

if i == -1:
	sys.stderr.write("No sys dir in kernel source path: %s\n" % sys.argv[1])
	sys.exit(0)

kld_debug_paths.append('/'.join(pfs[:i] + ['modules']))
kld_debug_paths.append(sys.argv[1])
#kld_debug_paths.append(sys.argv[3:])
gdb_cmd = gdb_cmd % {'p': sys.argv[1], 'core': sys.argv[2] }

#Start gdb
gdb = popen2.popen4(gdb_cmd)

def searchfor(inp, re, j = 0, l = None):
	"""searchfor(inp, re, j, l):  Searches for regex re in inp.  It will
automatically add more lines.  If j is set, the lines will be joined together.
l can provide a starting line to help search against.  Return value is a
tuple of the last line, and the match if any."""
	ret = None
	if not l:
		l = inp.readline()
	ret = re.search(l)
	while l and not ret:
		if j:
			l += inp.readline()
		else:
			l = inp.readline()
		ret = re.search(l)

	return (l, ret)

def get_addresses(inp, out):
	"""get_addresses(inp, out):  It will search for addresses from gdb.
inp and out, are the gdb input and output respectively.  Return value is
a list of tuples.  The tuples contain the filename and the address the
filename was loaded."""
	addr = []
	nxad = 1
	while nxad:
		if nxad == 1:
			out.write("print linker_files.tqh_first[0]\n")
		else:
			out.write("print *(struct linker_file *)%d\n" % nxad)
		out.flush()
		l = searchfor(inp, printre)[0]
		l, fn = searchfor(inp, filenamere, 1, l)
		if not fn.group('fn'):
			sys.stderr.write("got error: %s\n" % fn.group('error'))
			nxad = 0
		else:
			l, ad = searchfor(inp, addressre, 1, l)
			l, nx = searchfor(inp, nextre, 1, l)
			addr.append((fn.group('fn'), long(ad.group('ad'), 16)))
			nxad = long(nx.group('ad'), 16)

	return addr

#Get the addresses
addr = get_addresses(gdb[0], gdb[1])

#Pass through the resulting addresses, skipping the kernel.
for i in addr[1:]:
	for j in kld_debug_paths:
		#Try .debug first.
		p = popen2.popen4('find %s -type f -name "%s.debug"' % (j, i[0]))[0].read().strip()
		if p:
			break
		#Try just .ko if .debug wasn't found.
		p = popen2.popen4('find %s -type f -name "%s"' % (j, i[0]))[0].read().strip()
		if p:
			break

	if not p:
		#Tell our user that we couldn't find it.
		a = i[1]
		sys.stderr.write("Can't find module: %s (addr: %d + header)\n" % (i[0], a))
		print('#add-symbol-file <file>', a, '#add header')
		continue

	#j = popen2.popen4('objdump --section-headers /boot/kernel/%s | grep "\.text"' % i[0])[0].read().strip().split()
	#Output the necessary information
	j = popen2.popen4('objdump --section-headers "%s" | grep "\.text"' % p)[0].read().strip().split()
	try:
		a = int(j[5], 16)
		print('add-symbol-file', p, i[1] + a)
	except IndexError:
		sys.stderr.write('Bad file: %s, address: %d\n' % (i[0], i[1]))
