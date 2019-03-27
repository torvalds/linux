#!/usr/bin/awk -f

#
# Copyright (c) 2015 M. Warner Losh.
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
#

#
# Takes a meta-log created by installworld and friends, plus
# additions from NanoBSD to augment its build to communicate when
# files move around after installworld / installkernel phase of
# NanoBSD.
#
# All mtree lines from the metafile have a path, followed by
# a number of keywords.
#
# This script recognizes the following new keywords
#
# unlink[=x]	remove the path from the output.
# copy_from=x	create new entry for path copied from
#		the keywords from x.
# move_from=x	create new entry for path copied from
#		the keywords from x. Remove path from
#		the output.
#
# In addition, when path matches a previous entry, the
# new entry and previous entry are merged.
#
# Special note: when uid and uname are both present,
# uid is ignored. Ditto gid/gname.
#
# Also, the paths above have to match exactly, so X
# should start with "./".
#

function die(str)
{
	print str > "/dev/stderr";
	exit 1;
}

function kv(str)
{
	if (split(str, xxx, "=") == 2) {
		kv_key = xxx[1];
		kv_value = xxx[2];
	} else {
		kv_key = str;
		kv_value = nv;
	}
}

# Output the mtree for path based on the kvs.
function mtree_from_kvs(path, kvs)
{
	lv = path " ";
	for (k in kvs) {
		if (kvs[k] == nv)
			lv = lv k " ";
		else
			lv = lv k "=" kvs[k] " ";
	}
	return lv;
}

# Parse the mtree line into path + KVs. Use a sentinal value
# for a bare keyword, which is extremely unlikely to be used
# for real.
function line2kv(kvs, str)
{
	delete kvs;

	n = split(str, yyy, " ");
	for (i = 2; i <= n; i++) {
		s = yyy[i];
		if (split(s, xxx, "=") == 2)
			kvs[xxx[1]] = xxx[2];
		else
			kvs[s] = nv;
	}
}


# old += new
function merge_kvs(old, new)
{
	for (k in new) {
		# uname / uid -- last one wins.
		if (k == "uid" && "uname" in old)
			delete old["uname"]
		if (k == "uname" && "uid" in old)
			delete old["uid"];
		# gname / gid -- last one wins.
		if (k == "gid" && "gname" in old)
			delete old["gname"]
		if (k == "gname" && "gid" in old)
			delete old["gid"];
		# Otherwise newest value wins
		old[k] = new[k];
	}
}

# Process the line we've read in, per the comments below
function process_line(path, new)
{
	# Clear kvs
	line2kv(new_kvs, new);

	if ("unlink" in new_kvs) {
		# A file removed
		# Sanity check to see if tree[path] exists? 
		# Makes sure when foo/bar/baz exists and foo/bar
		# unlinked, baz is gone (for all baz).
		if (path !~ "^\./")
			die("bad path in : " new);
		delete tree[path];		# unlink
		return;
	# } else if (new_kvs["append_from"]) { # not implemented
	} else if ("copy_from" in new_kvs) {
		# A file copied from another location, preserve its
		# attribute for new file.
		# Also merge any new attributes from this line.
		from = new_kvs["copy_from"];
		if (from !~ "^\./")
			die("bad path in : " new);
		delete new_kvs["copy_from"];
		line2kv(old_kvs, tree[from]);	# old_kvs = kv's in entry
		merge_kvs(old_kvs, new_kvs);	# old_kvs += new_kvs
		tree[path] = mtree_from_kvs(path, old_kvs);
	} else if ("move_from" in new_kvs) {
		# A file moved from another location, preserve its
		# attribute for new file, and scrag old location
		# Also merge any new attributes from this line.
		from = new_kvs["move_from"];
		if (from !~ "^\./")
			die("bad path in : " new);
		delete new_kvs["move_from"];
		line2kv(old_kvs, tree[from]);	# old_kvs = kv's in entry
		merge_kvs(old_kvs, new_kvs);	# old_kvs += new_kvs
		tree[path] = mtree_from_kvs(path, old_kvs);
		delete tree[from];		# unlink
	} else if (tree[path]) {	# Update existing entry with new line
		line2kv(old_kvs, tree[path]);	# old_kvs = kv's in entry
		merge_kvs(old_kvs, new_kvs);	# old_kvs += new_kvs
		tree[path] = mtree_from_kvs(path, old_kvs);
	} else {			# Add entry plus defaults
		delete old_kvs;
		merge_kvs(old_kvs, defaults);
		merge_kvs(old_kvs, new_kvs);
		tree[path] = mtree_from_kvs(path, old_kvs);
	}
}

BEGIN {
	nv = "___NO__VALUE___";

	while ((getline < "/dev/stdin") > 0) {
		if ($1 ~ "^#")
			continue;
		if ($1 == "/set") {
			for (i = 2; i <= NF; i++) {
				kv($i);
				defaults[kv_key] = kv_value;
			}
		} else if ($1 == "/unset") {
			for (i = 2; i <= NF; i++) {
				kv($i);
				delete defaults[kv_key];
			}
		} else
			process_line($1, $0);
	}

	# Print the last set of defaults. This will carry
	# over, I think, to makefs' defaults
	print mtree_from_kvs("/set", defaults)
	for (x in tree)
		print tree[x];
}
