#!/usr/bin/env python
#
# Copyright (c) 2014, Craig Rodrigues <rodrigc@FreeBSD.org>
# All rights reserved.
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions
# are met:
# 1. Redistributions of source code must retain the above copyright
#    notice unmodified, this list of conditions, and the following
#    disclaimer.
# 2. Redistributions in binary form must reproduce the above copyright
#    notice, this list of conditions and the following disclaimer in the
#     documentation and/or other materials provided with the distribution.
#
# THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
# IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
# OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
# IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
# INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
# NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
# DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
# THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
# (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
# THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
#
# $FreeBSD$

# Display SVN log entries for changesets which have files which were
# Added or Deleted.
# This script takes arguments which would normally be
# passed to the "svn log" command.
#
# Examples:
#
#  (1) Display all new changesets in stable/10 branch:  
#
#        list-new-changesets.py --stop-on-copy \
#                        svn://svn.freebsd.org/base/stable/10 
#
#  (2) Display all new changesets between r254153 and r261794 in
#      stable/9 branch:
#
#        list-new-changesets.py  -r254153:261794 \
#                        svn://svn.freebsd.org/base/stable/9

from __future__ import print_function
import os
import subprocess
import sys
import xml.etree.ElementTree

def print_logentry(logentry):
    """Print an SVN log entry.
 
    Take an SVN log entry formatted in XML, and print it out in
    plain text.
    """
    rev = logentry.attrib['revision']
    author = logentry.find('author').text
    date = logentry.find('date').text
    msg = logentry.find('msg').text

    print("-" * 71)
    print("%s | %s | %s" % (rev, author, date))
    print("Changed paths:")
    for paths in logentry.findall('paths'):
        for path in paths.findall('path'):
            print("   %s %s" % (path.attrib['action'], path.text))

    print()
    print(msg.encode('utf-8'))

def main(args):
    """Main function.

    Take command-line arguments which would be passed to 'svn log'.
    Prepend '-v --xml' to get verbose XML formatted output.
    Only display entries which have Added or Deleted files.
    """
    cmd = ["svn", "log", "-v", "--xml"]
    cmd += args[1:] 

    print(" ".join(cmd))

    proc = subprocess.Popen(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    (out, err) = proc.communicate()

    if proc.returncode != 0:
        print(err)
        sys.exit(proc.returncode) 

    displayed_entries = 0
    root = xml.etree.ElementTree.fromstring(out)

    for logentry in root.findall('logentry'):
       show_logentry = False
    
       for paths in logentry.findall('paths'):
           for path in paths.findall('path'):
               if path.attrib['action'] == 'A':
                  show_logentry = True
               elif path.attrib['action'] == 'D':
                  show_logentry = True
           
       if show_logentry == True :
           print_logentry(logentry)
           displayed_entries += 1

    if displayed_entries == 0:
        print("No changesets with Added or Deleted files")

    if displayed_entries > 0:    
        print("-" * 71)


if __name__ == "__main__":
    main(sys.argv)
