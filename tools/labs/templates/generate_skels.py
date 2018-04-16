#!/usr/bin/python3 -u

import argparse, fnmatch, glob, os.path, re, sys, shutil

parser = argparse.ArgumentParser(description='Generate skeletons sources from full sources')
parser.add_argument('paths', metavar='path', nargs='+', help='list of files to process')
parser.add_argument('--output', help='output dir to copy processed files')
parser.add_argument('--todo', type=int, help='don\'t remove TODOs less then this', default=1)
args = parser.parse_args()

def process_file(p, pattern, end_string=None):
	f = open(p, "r")
	g = open(os.path.join(args.output, p), "w")
	skip_lines = 0
	end_found = True
	for l in f.readlines():
		if end_string and end_found == False:
			g.write(l)
			if end_string in l:
				end_found = True
			continue

		if skip_lines > 0:
			skip_lines -= 1
			m = re.search(pattern, l)
			if m :
				l = "%s%s%s\n" % (m.group(1), m.group(2), m.group(4))
				g.write(l)
			continue
		m = re.search(pattern, l)
		if m:
			todo=1
			if m.group(2):
				todo = int(m.group(2))
			if todo >= args.todo:
				if m.group(3):
					skip_lines = int(m.group(3))
				else:
					skip_lines = 1

			if end_string and end_string not in l:
			    end_found = False

			l = "%s%s%s\n" % (m.group(1), m.group(2), m.group(4))
		g.write(l)

for p in args.paths:
	print("skel %s" % (p), sep = '')
	name=os.path.basename(p)
	try:
		os.makedirs(os.path.join(args.output, os.path.dirname(p)))
	except:
		pass

	copy = False
	end_string = None
	if name == "Kbuild" or name == "Makefile":
		pattern="(^#\s*TODO)([0-9]*)\/?([0-9]*)(:.*)"
	elif fnmatch.fnmatch(name, '*.c') or fnmatch.fnmatch(name, '*.h'):
		pattern="(.*/\*\s*TODO)([ 0-9]*)/?([0-9]*)(:.*)"
		end_string = "*/"
	else:
		copy = True

	if copy:
		shutil.copyfile(p, os.path.join(args.output, p))
	else:
		process_file(p, pattern, end_string)
