#!/bin/sh
#
# SPDX-License-Identifier: BSD-2-Clause-FreeBSD
#
# Copyright (c) 2010-2013 Hudson River Trading LLC
# Written by: John H. Baldwin <jhb@FreeBSD.org>
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

# This is a tool to manage updating files that are not updated as part
# of 'make installworld' such as files in /etc.  Unlike other tools,
# this one is specifically tailored to assisting with mass upgrades.
# To that end it does not require user intervention while running.
#
# Theory of operation:
#
# The most reliable way to update changes to files that have local
# modifications is to perform a three-way merge between the original
# unmodified file, the new version of the file, and the modified file.
# This requires having all three versions of the file available when
# performing an update.
#
# To that end, etcupdate uses a strategy where the current unmodified
# tree is kept in WORKDIR/current and the previous unmodified tree is
# kept in WORKDIR/old.  When performing a merge, a new tree is built
# if needed and then the changes are merged into DESTDIR.  Any files
# with unresolved conflicts after the merge are left in a tree rooted
# at WORKDIR/conflicts.
#
# To provide extra flexibility, etcupdate can also build tarballs of
# root trees that can later be used.  It can also use a tarball as the
# source of a new tree instead of building it from /usr/src.

# Global settings.  These can be adjusted by config files and in some
# cases by command line options.

# TODO:
# - automatable conflict resolution
# - a 'revert' command to make a file "stock"

usage()
{
	cat <<EOF
usage: etcupdate [-npBF] [-d workdir] [-r | -s source | -t tarball]
                 [-A patterns] [-D destdir] [-I patterns] [-L logfile]
                 [-M options]
       etcupdate build [-B] [-d workdir] [-s source] [-L logfile] [-M options]
                 <tarball>
       etcupdate diff [-d workdir] [-D destdir] [-I patterns] [-L logfile]
       etcupdate extract [-B] [-d workdir] [-s source | -t tarball] [-L logfile]
                 [-M options]
       etcupdate resolve [-p] [-d workdir] [-D destdir] [-L logfile]
       etcupdate status [-d workdir] [-D destdir]
EOF
	exit 1
}

# Used to write a message prepended with '>>>' to the logfile.
log()
{
	echo ">>>" "$@" >&3
}

# Used for assertion conditions that should never happen.
panic()
{
	echo "PANIC:" "$@"
	exit 10
}

# Used to write a warning message.  These are saved to the WARNINGS
# file with "  " prepended.
warn()
{
	echo -n "  " >> $WARNINGS
	echo "$@" >> $WARNINGS
}

# Output a horizontal rule using the passed-in character.  Matches the
# length used for Index lines in CVS and SVN diffs.
#
# $1 - character
rule()
{
	jot -b "$1" -s "" 67
}

# Output a text description of a specified file's type.
#
# $1 - file pathname.
file_type()
{
	stat -f "%HT" $1 | tr "[:upper:]" "[:lower:]"
}

# Returns true (0) if a file exists
#
# $1 - file pathname.
exists()
{
	[ -e $1 -o -L $1 ]
}

# Returns true (0) if a file should be ignored, false otherwise.
#
# $1 - file pathname
ignore()
{
	local pattern -

	set -o noglob
	for pattern in $IGNORE_FILES; do
		set +o noglob
		case $1 in
			$pattern)
				return 0
				;;
		esac
		set -o noglob
	done

	# Ignore /.cshrc and /.profile if they are hardlinked to the
	# same file in /root.  This ensures we only compare those
	# files once in that case.
	case $1 in
		/.cshrc|/.profile)
			if [ ${DESTDIR}$1 -ef ${DESTDIR}/root$1 ]; then
				return 0
			fi
			;;
		*)
			;;
	esac

	return 1
}

# Returns true (0) if the new version of a file should always be
# installed rather than attempting to do a merge.
#
# $1 - file pathname
always_install()
{
	local pattern -

	set -o noglob
	for pattern in $ALWAYS_INSTALL; do
		set +o noglob
		case $1 in
			$pattern)
				return 0
				;;
		esac
		set -o noglob
	done

	return 1
}

# Build a new tree
#
# $1 - directory to store new tree in
build_tree()
{
	local destdir dir file make

	make="make $MAKE_OPTIONS -DNO_FILEMON"

	log "Building tree at $1 with $make"
	mkdir -p $1/usr/obj >&3 2>&1
	destdir=`realpath $1`

	if [ -n "$preworld" ]; then
		# Build a limited tree that only contains files that are
		# crucial to installworld.
		for file in $PREWORLD_FILES; do
			dir=`dirname /$file`
			mkdir -p $1/$dir >&3 2>&1 || return 1
			cp -p $SRCDIR/$file $1/$file || return 1
		done
	elif ! [ -n "$nobuild" ]; then
		(cd $SRCDIR; $make DESTDIR=$destdir distrib-dirs &&
    MAKEOBJDIRPREFIX=$destdir/usr/obj $make _obj SUBDIR_OVERRIDE=etc &&
    MAKEOBJDIRPREFIX=$destdir/usr/obj $make everything SUBDIR_OVERRIDE=etc &&
    MAKEOBJDIRPREFIX=$destdir/usr/obj $make DESTDIR=$destdir distribution) \
		    >&3 2>&1 || return 1
	else
		(cd $SRCDIR; $make DESTDIR=$destdir distrib-dirs &&
		    $make DESTDIR=$destdir distribution) >&3 2>&1 || return 1
	fi
	chflags -R noschg $1 >&3 2>&1 || return 1
	rm -rf $1/usr/obj >&3 2>&1 || return 1

	# Purge auto-generated files.  Only the source files need to
	# be updated after which these files are regenerated.
	rm -f $1/etc/*.db $1/etc/passwd $1/var/db/services.db >&3 2>&1 || \
	    return 1

	# Remove empty files.  These just clutter the output of 'diff'.
	find $1 -type f -size 0 -delete >&3 2>&1 || return 1

	# Trim empty directories.
	find -d $1 -type d -empty -delete >&3 2>&1 || return 1
	return 0
}

# Generate a new NEWTREE tree.  If tarball is set, then the tree is
# extracted from the tarball.  Otherwise the tree is built from a
# source tree.
extract_tree()
{
	local files

	# If we have a tarball, extract that into the new directory.
	if [ -n "$tarball" ]; then
		files=
		if [ -n "$preworld" ]; then
			files="$PREWORLD_FILES"
		fi
		if ! (mkdir -p $NEWTREE && tar xf $tarball -C $NEWTREE $files) \
		    >&3 2>&1; then
			echo "Failed to extract new tree."
			remove_tree $NEWTREE
			exit 1
		fi
	else
		if ! build_tree $NEWTREE; then
			echo "Failed to build new tree."
			remove_tree $NEWTREE
			exit 1
		fi
	fi
}

# Forcefully remove a tree.  Returns true (0) if the operation succeeds.
#
# $1 - path to tree
remove_tree()
{

	rm -rf $1 >&3 2>&1
	if [ -e $1 ]; then
		chflags -R noschg $1 >&3 2>&1
		rm -rf $1 >&3 2>&1
	fi
	[ ! -e $1 ]
}

# Return values for compare()
COMPARE_EQUAL=0
COMPARE_ONLYFIRST=1
COMPARE_ONLYSECOND=2
COMPARE_DIFFTYPE=3
COMPARE_DIFFLINKS=4
COMPARE_DIFFFILES=5

# Compare two files/directories/symlinks.  Note that this does not
# recurse into subdirectories.  Instead, if two nodes are both
# directories, they are assumed to be equivalent.
#
# Returns true (0) if the nodes are identical.  If only one of the two
# nodes are present, return one of the COMPARE_ONLY* constants.  If
# the nodes are different, return one of the COMPARE_DIFF* constants
# to indicate the type of difference.
#
# $1 - first node
# $2 - second node
compare()
{
	local first second

	# If the first node doesn't exist, then check for the second
	# node.  Note that -e will fail for a symbolic link that
	# points to a missing target.
	if ! exists $1; then
		if exists $2; then
			return $COMPARE_ONLYSECOND
		else
			return $COMPARE_EQUAL
		fi
	elif ! exists $2; then
		return $COMPARE_ONLYFIRST
	fi

	# If the two nodes are different file types fail.
	first=`stat -f "%Hp" $1`
	second=`stat -f "%Hp" $2`
	if [ "$first" != "$second" ]; then
		return $COMPARE_DIFFTYPE
	fi

	# If both are symlinks, compare the link values.
	if [ -L $1 ]; then
		first=`readlink $1`
		second=`readlink $2`
		if [ "$first" = "$second" ]; then
			return $COMPARE_EQUAL
		else
			return $COMPARE_DIFFLINKS
		fi
	fi

	# If both are files, compare the file contents.
	if [ -f $1 ]; then
		if cmp -s $1 $2; then
			return $COMPARE_EQUAL
		else
			return $COMPARE_DIFFFILES
		fi
	fi

	# As long as the two nodes are the same type of file, consider
	# them equivalent.
	return $COMPARE_EQUAL
}

# Returns true (0) if the only difference between two regular files is a
# change in the FreeBSD ID string.
#
# $1 - path of first file
# $2 - path of second file
fbsdid_only()
{

	diff -qI '\$FreeBSD.*\$' $1 $2 >/dev/null 2>&1
}

# This is a wrapper around compare that will return COMPARE_EQUAL if
# the only difference between two regular files is a change in the
# FreeBSD ID string.  It only makes this adjustment if the -F flag has
# been specified.
#
# $1 - first node
# $2 - second node
compare_fbsdid()
{
	local cmp

	compare $1 $2
	cmp=$?

	if [ -n "$FREEBSD_ID" -a "$cmp" -eq $COMPARE_DIFFFILES ] && \
	    fbsdid_only $1 $2; then
		return $COMPARE_EQUAL
	fi

	return $cmp
}

# Returns true (0) if a directory is empty.
#
# $1 - pathname of the directory to check
empty_dir()
{
	local contents

	contents=`ls -A $1`
	[ -z "$contents" ]
}

# Returns true (0) if one directories contents are a subset of the
# other.  This will recurse to handle subdirectories and compares
# individual files in the trees.  Its purpose is to quiet spurious
# directory warnings for dryrun invocations.
#
# $1 - first directory (sub)
# $2 - second directory (super)
dir_subset()
{
	local contents file

	if ! [ -d $1 -a -d $2 ]; then
		return 1
	fi

	# Ignore files that are present in the second directory but not
	# in the first.
	contents=`ls -A $1`
	for file in $contents; do
		if ! compare $1/$file $2/$file; then
			return 1
		fi

		if [ -d $1/$file ]; then
			if ! dir_subset $1/$file $2/$file; then
				return 1
			fi
		fi
	done
	return 0
}

# Returns true (0) if a directory in the destination tree is empty.
# If this is a dryrun, then this returns true as long as the contents
# of the directory are a subset of the contents in the old tree
# (meaning that the directory would be empty in a non-dryrun when this
# was invoked) to quiet spurious warnings.
#
# $1 - pathname of the directory to check relative to DESTDIR.
empty_destdir()
{

	if [ -n "$dryrun" ]; then
		dir_subset $DESTDIR/$1 $OLDTREE/$1
		return
	fi

	empty_dir $DESTDIR/$1
}

# Output a diff of two directory entries with the same relative name
# in different trees.  Note that as with compare(), this does not
# recurse into subdirectories.  If the nodes are identical, nothing is
# output.
#
# $1 - first tree
# $2 - second tree
# $3 - node name 
# $4 - label for first tree
# $5 - label for second tree
diffnode()
{
	local first second file old new diffargs

	if [ -n "$FREEBSD_ID" ]; then
		diffargs="-I \\\$FreeBSD.*\\\$"
	else
		diffargs=""
	fi

	compare_fbsdid $1/$3 $2/$3
	case $? in
		$COMPARE_EQUAL)
			;;
		$COMPARE_ONLYFIRST)
			echo
			echo "Removed: $3"
			echo
			;;
		$COMPARE_ONLYSECOND)
			echo
			echo "Added: $3"
			echo
			;;
		$COMPARE_DIFFTYPE)
			first=`file_type $1/$3`
			second=`file_type $2/$3`
			echo
			echo "Node changed from a $first to a $second: $3"
			echo
			;;
		$COMPARE_DIFFLINKS)
			first=`readlink $1/$file`
			second=`readlink $2/$file`
			echo
			echo "Link changed: $file"
			rule "="
			echo "-$first"
			echo "+$second"
			echo
			;;
		$COMPARE_DIFFFILES)
			echo "Index: $3"
			rule "="
			diff -u $diffargs -L "$3 ($4)" $1/$3 -L "$3 ($5)" $2/$3
			;;
	esac
}

# Run one-off commands after an update has completed.  These commands
# are not tied to a specific file, so they cannot be handled by
# post_install_file().
post_update()
{
	local args

	# None of these commands should be run for a pre-world update.
	if [ -n "$preworld" ]; then
		return
	fi

	# If /etc/localtime exists and is not a symlink and /var/db/zoneinfo
	# exists, run tzsetup -r to refresh /etc/localtime.
	if [ -f ${DESTDIR}/etc/localtime -a \
	    ! -L ${DESTDIR}/etc/localtime ]; then
		if [ -f ${DESTDIR}/var/db/zoneinfo ]; then
			if [ -n "${DESTDIR}" ]; then
				args="-C ${DESTDIR}"
			else
				args=""
			fi
			log "tzsetup -r ${args}"
			if [ -z "$dryrun" ]; then
				tzsetup -r ${args} >&3 2>&1
			fi
		else
			warn "Needs update: /etc/localtime (required" \
			    "manual update via tzsetup(8))"
		fi
	fi
}

# Create missing parent directories of a node in a target tree
# preserving the owner, group, and permissions from a specified
# template tree.
#
# $1 - template tree
# $2 - target tree
# $3 - pathname of the node (relative to both trees)
install_dirs()
{
	local args dir

	dir=`dirname $3`

	# Nothing to do if the parent directory exists.  This also
	# catches the degenerate cases when the path is just a simple
	# filename.
	if [ -d ${2}$dir ]; then
		return 0
	fi

	# If non-directory file exists with the desired directory
	# name, then fail.
	if exists ${2}$dir; then
		# If this is a dryrun and we are installing the
		# directory in the DESTDIR and the file in the DESTDIR
		# matches the file in the old tree, then fake success
		# to quiet spurious warnings.
		if [ -n "$dryrun" -a "$2" = "$DESTDIR" ]; then
			if compare $OLDTREE/$dir $DESTDIR/$dir; then
				return 0
			fi
		fi

		args=`file_type ${2}$dir`
		warn "Directory mismatch: ${2}$dir ($args)"
		return 1
	fi

	# Ensure the parent directory of the directory is present
	# first.
	if ! install_dirs $1 "$2" $dir; then
		return 1
	fi

	# Format attributes from template directory as install(1)
	# arguments.
	args=`stat -f "-o %Su -g %Sg -m %0Mp%0Lp" $1/$dir`

	log "install -d $args ${2}$dir"
	if [ -z "$dryrun" ]; then
		install -d $args ${2}$dir >&3 2>&1
	fi
	return 0
}

# Perform post-install fixups for a file.  This largely consists of
# regenerating any files that depend on the newly installed file.
#
# $1 - pathname of the updated file (relative to DESTDIR)
post_install_file()
{
	case $1 in
		/etc/mail/aliases)
			# Grr, newaliases only works for an empty DESTDIR.
			if [ -z "$DESTDIR" ]; then
				log "newaliases"
				if [ -z "$dryrun" ]; then
					newaliases >&3 2>&1
				fi
			else
				NEWALIAS_WARN=yes
			fi
			;;
		/etc/login.conf)
			log "cap_mkdb ${DESTDIR}$1"
			if [ -z "$dryrun" ]; then
				cap_mkdb ${DESTDIR}$1 >&3 2>&1
			fi
			;;
		/etc/master.passwd)
			log "pwd_mkdb -p -d $DESTDIR/etc ${DESTDIR}$1"
			if [ -z "$dryrun" ]; then
				pwd_mkdb -p -d $DESTDIR/etc ${DESTDIR}$1 \
				    >&3 2>&1
			fi
			;;
		/etc/motd)
			# /etc/rc.d/motd hardcodes the /etc/motd path.
			# Don't warn about non-empty DESTDIR's since this
			# change is only cosmetic anyway.
			if [ -z "$DESTDIR" ]; then
				log "sh /etc/rc.d/motd start"
				if [ -z "$dryrun" ]; then
					sh /etc/rc.d/motd start >&3 2>&1
				fi
			fi
			;;
		/etc/services)
			log "services_mkdb -q -o $DESTDIR/var/db/services.db" \
			    "${DESTDIR}$1"
			if [ -z "$dryrun" ]; then
				services_mkdb -q -o $DESTDIR/var/db/services.db \
				    ${DESTDIR}$1 >&3 2>&1
			fi
			;;
	esac
}

# Install the "new" version of a file.  Returns true if it succeeds
# and false otherwise.
#
# $1 - pathname of the file to install (relative to DESTDIR)
install_new()
{

	if ! install_dirs $NEWTREE "$DESTDIR" $1; then
		return 1
	fi
	log "cp -Rp ${NEWTREE}$1 ${DESTDIR}$1"
	if [ -z "$dryrun" ]; then
		cp -Rp ${NEWTREE}$1 ${DESTDIR}$1 >&3 2>&1
	fi
	post_install_file $1
	return 0
}

# Install the "resolved" version of a file.  Returns true if it succeeds
# and false otherwise.
#
# $1 - pathname of the file to install (relative to DESTDIR)
install_resolved()
{

	# This should always be present since the file is already
	# there (it caused a conflict).  However, it doesn't hurt to
	# just be safe.
	if ! install_dirs $NEWTREE "$DESTDIR" $1; then
		return 1
	fi

	log "cp -Rp ${CONFLICTS}$1 ${DESTDIR}$1"
	cp -Rp ${CONFLICTS}$1 ${DESTDIR}$1 >&3 2>&1
	post_install_file $1
	return 0
}

# Generate a conflict file when a "new" file conflicts with an
# existing file in DESTDIR.
#
# $1 - pathname of the file that conflicts (relative to DESTDIR)
new_conflict()
{

	if [ -n "$dryrun" ]; then
		return
	fi

	install_dirs $NEWTREE $CONFLICTS $1
	diff --changed-group-format='<<<<<<< (local)
%<=======
%>>>>>>>> (stock)
' $DESTDIR/$1 $NEWTREE/$1 > $CONFLICTS/$1
}

# Remove the "old" version of a file.
#
# $1 - pathname of the old file to remove (relative to DESTDIR)
remove_old()
{
	log "rm -f ${DESTDIR}$1"
	if [ -z "$dryrun" ]; then
		rm -f ${DESTDIR}$1 >&3 2>&1
	fi
	echo "  D $1"
}

# Update a file that has no local modifications.
#
# $1 - pathname of the file to update (relative to DESTDIR)
update_unmodified()
{
	local new old

	# If the old file is a directory, then remove it with rmdir
	# (this should only happen if the file has changed its type
	# from a directory to a non-directory).  If the directory
	# isn't empty, then fail.  This will be reported as a warning
	# later.
	if [ -d $DESTDIR/$1 ]; then
		if empty_destdir $1; then
			log "rmdir ${DESTDIR}$1"
			if [ -z "$dryrun" ]; then
				rmdir ${DESTDIR}$1 >&3 2>&1
			fi
		else
			return 1
		fi

	# If both the old and new files are regular files, leave the
	# existing file.  This avoids breaking hard links for /.cshrc
	# and /.profile.  Otherwise, explicitly remove the old file.
	elif ! [ -f ${DESTDIR}$1 -a -f ${NEWTREE}$1 ]; then
		log "rm -f ${DESTDIR}$1"
		if [ -z "$dryrun" ]; then
			rm -f ${DESTDIR}$1 >&3 2>&1
		fi
	fi

	# If the new file is a directory, note that the old file has
	# been removed, but don't do anything else for now.  The
	# directory will be installed if needed when new files within
	# that directory are installed.
	if [ -d $NEWTREE/$1 ]; then
		if empty_dir $NEWTREE/$1; then
			echo "  D $file"
		else
			echo "  U $file"
		fi
	elif install_new $1; then
		echo "  U $file"
	fi
	return 0
}

# Update the FreeBSD ID string in a locally modified file to match the
# FreeBSD ID string from the "new" version of the file.
#
# $1 - pathname of the file to update (relative to DESTDIR)
update_freebsdid()
{
	local new dest file

	# If the FreeBSD ID string is removed from the local file,
	# there is nothing to do.  In this case, treat the file as
	# updated.  Otherwise, if either file has more than one
	# FreeBSD ID string, just punt and let the user handle the
	# conflict manually.
	new=`grep -c '\$FreeBSD.*\$' ${NEWTREE}$1`
	dest=`grep -c '\$FreeBSD.*\$' ${DESTDIR}$1`
	if [ "$dest" -eq 0 ]; then
		return 0
	fi
	if [ "$dest" -ne 1 -o "$dest" -ne 1 ]; then
		return 1
	fi

	# If the FreeBSD ID string in the new file matches the FreeBSD ID
	# string in the local file, there is nothing to do.
	new=`grep '\$FreeBSD.*\$' ${NEWTREE}$1`
	dest=`grep '\$FreeBSD.*\$' ${DESTDIR}$1`
	if [ "$new" = "$dest" ]; then
		return 0
	fi

	# Build the new file in three passes.  First, copy all the
	# lines preceding the FreeBSD ID string from the local version
	# of the file.  Second, append the FreeBSD ID string line from
	# the new version.  Finally, append all the lines after the
	# FreeBSD ID string from the local version of the file.
	file=`mktemp $WORKDIR/etcupdate-XXXXXXX`
	awk '/\$FreeBSD.*\$/ { exit } { print }' ${DESTDIR}$1 >> $file
	awk '/\$FreeBSD.*\$/ { print }' ${NEWTREE}$1 >> $file
	awk '/\$FreeBSD.*\$/ { ok = 1; next } { if (ok) print }' \
	    ${DESTDIR}$1 >> $file

	# As an extra sanity check, fail the attempt if the updated
	# version of the file has any differences aside from the
	# FreeBSD ID string.
	if ! fbsdid_only ${DESTDIR}$1 $file; then
		rm -f $file
		return 1
	fi

	log "cp $file ${DESTDIR}$1"
	if [ -z "$dryrun" ]; then
		cp $file ${DESTDIR}$1 >&3 2>&1
	fi
	rm -f $file
	post_install_file $1
	echo "  M $1"
	return 0
}

# Attempt to update a file that has local modifications.  This routine
# only handles regular files.  If the 3-way merge succeeds without
# conflicts, the updated file is installed.  If the merge fails, the
# merged version with conflict markers is left in the CONFLICTS tree.
#
# $1 - pathname of the file to merge (relative to DESTDIR)
merge_file()
{
	local res

	# Try the merge to see if there is a conflict.
	diff3 -E -m ${DESTDIR}$1 ${OLDTREE}$1 ${NEWTREE}$1 > /dev/null 2>&3
	res=$?
	case $res in
		0)
			# No conflicts, so just redo the merge to the
			# real file.
			log "diff3 -E -m ${DESTDIR}$1 ${OLDTREE}$1 ${NEWTREE}$1"
			if [ -z "$dryrun" ]; then
				temp=$(mktemp -t etcupdate)
				diff3 -E -m ${DESTDIR}$1 ${OLDTREE}$1 ${NEWTREE}$1 > ${temp}
				# Use "cat >" to preserve metadata.
				cat ${temp} > ${DESTDIR}$1
				rm -f ${temp}
			fi
			post_install_file $1
			echo "  M $1"
			;;
		1)
			# Conflicts, save a version with conflict markers in
			# the conflicts directory.
			if [ -z "$dryrun" ]; then
				install_dirs $NEWTREE $CONFLICTS $1
				log "diff3 -m -A ${DESTDIR}$1 ${CONFLICTS}$1"
				diff3 -m -A -L "yours" -L "original" -L "new" \
				    ${DESTDIR}$1 ${OLDTREE}$1 ${NEWTREE}$1 > \
				    ${CONFLICTS}$1
			fi
			echo "  C $1"
			;;
		*)
			panic "merge failed with status $res"
			;;
	esac
}

# Returns true if a file contains conflict markers from a merge conflict.
#
# $1 - pathname of the file to resolve (relative to DESTDIR)
has_conflicts()
{
	
	egrep -q '^(<{7}|\|{7}|={7}|>{7}) ' $CONFLICTS/$1
}

# Attempt to resolve a conflict.  The user is prompted to choose an
# action for each conflict.  If the user edits the file, they are
# prompted again for an action.  The process is very similar to
# resolving conflicts after an update or merge with Perforce or
# Subversion.  The prompts are modelled on a subset of the available
# commands for resolving conflicts with Subversion.
#
# $1 - pathname of the file to resolve (relative to DESTDIR)
resolve_conflict()
{
	local command junk

	echo "Resolving conflict in '$1':"
	edit=
	while true; do
		# Only display the resolved command if the file
		# doesn't contain any conflicts.
		echo -n "Select: (p) postpone, (df) diff-full, (e) edit,"
		if ! has_conflicts $1; then
			echo -n " (r) resolved,"
		fi
		echo
		echo -n "        (h) help for more options: "
		read command
		case $command in
			df)
				diff -u ${DESTDIR}$1 ${CONFLICTS}$1
				;;
			e)
				$EDITOR ${CONFLICTS}$1
				;;
			h)
				cat <<EOF
  (p)  postpone    - ignore this conflict for now
  (df) diff-full   - show all changes made to merged file
  (e)  edit        - change merged file in an editor
  (r)  resolved    - accept merged version of file
  (mf) mine-full   - accept local version of entire file (ignore new changes)
  (tf) theirs-full - accept new version of entire file (lose local changes)
  (h)  help        - show this list
EOF
				;;
			mf)
				# For mine-full, just delete the
				# merged file and leave the local
				# version of the file as-is.
				rm ${CONFLICTS}$1
				return
				;;
			p)
				return
				;;
			r)
				# If the merged file has conflict
				# markers, require confirmation.
				if has_conflicts $1; then
					echo "File '$1' still has conflicts," \
					    "are you sure? (y/n) "
					read junk
					if [ "$junk" != "y" ]; then
						continue
					fi
				fi

				if ! install_resolved $1; then
					panic "Unable to install merged" \
					    "version of $1"
				fi
				rm ${CONFLICTS}$1
				return
				;;
			tf)
				# For theirs-full, install the new
				# version of the file over top of the
				# existing file.
				if ! install_new $1; then
					panic "Unable to install new" \
					    "version of $1"
				fi
				rm ${CONFLICTS}$1
				return
				;;
			*)
				echo "Invalid command."
				;;
		esac
	done
}

# Handle a file that has been removed from the new tree.  If the file
# does not exist in DESTDIR, then there is nothing to do.  If the file
# exists in DESTDIR and is identical to the old version, remove it
# from DESTDIR.  Otherwise, whine about the conflict but leave the
# file in DESTDIR.  To handle directories, this uses two passes.  The
# first pass handles all non-directory files.  The second pass handles
# just directories and removes them if they are empty.
#
# If -F is specified, and the only difference in the file in DESTDIR
# is a change in the FreeBSD ID string, then remove the file.
#
# $1 - pathname of the file (relative to DESTDIR)
handle_removed_file()
{
	local dest file

	file=$1
	if ignore $file; then
		log "IGNORE: removed file $file"
		return
	fi

	compare_fbsdid $DESTDIR/$file $OLDTREE/$file
	case $? in
		$COMPARE_EQUAL)
			if ! [ -d $DESTDIR/$file ]; then
				remove_old $file
			fi
			;;
		$COMPARE_ONLYFIRST)
			panic "Removed file now missing"
			;;
		$COMPARE_ONLYSECOND)
			# Already removed, nothing to do.
			;;
		$COMPARE_DIFFTYPE|$COMPARE_DIFFLINKS|$COMPARE_DIFFFILES)
			dest=`file_type $DESTDIR/$file`
			warn "Modified $dest remains: $file"
			;;
	esac
}

# Handle a directory that has been removed from the new tree.  Only
# remove the directory if it is empty.
#
# $1 - pathname of the directory (relative to DESTDIR)
handle_removed_directory()
{
	local dir

	dir=$1
	if ignore $dir; then
		log "IGNORE: removed dir $dir"
		return
	fi

	if [ -d $DESTDIR/$dir -a -d $OLDTREE/$dir ]; then
		if empty_destdir $dir; then
			log "rmdir ${DESTDIR}$dir"
			if [ -z "$dryrun" ]; then
				rmdir ${DESTDIR}$dir >/dev/null 2>&1
			fi
			echo "  D $dir"
		else
			warn "Non-empty directory remains: $dir"
		fi
	fi
}

# Handle a file that exists in both the old and new trees.  If the
# file has not changed in the old and new trees, there is nothing to
# do.  If the file in the destination directory matches the new file,
# there is nothing to do.  If the file in the destination directory
# matches the old file, then the new file should be installed.
# Everything else becomes some sort of conflict with more detailed
# handling.
#
# $1 - pathname of the file (relative to DESTDIR)
handle_modified_file()
{
	local cmp dest file new newdestcmp old

	file=$1
	if ignore $file; then
		log "IGNORE: modified file $file"
		return
	fi

	compare $OLDTREE/$file $NEWTREE/$file
	cmp=$?
	if [ $cmp -eq $COMPARE_EQUAL ]; then
		return
	fi

	if [ $cmp -eq $COMPARE_ONLYFIRST -o $cmp -eq $COMPARE_ONLYSECOND ]; then
		panic "Changed file now missing"
	fi

	compare $NEWTREE/$file $DESTDIR/$file
	newdestcmp=$?
	if [ $newdestcmp -eq $COMPARE_EQUAL ]; then
		return
	fi

	# If the only change in the new file versus the destination
	# file is a change in the FreeBSD ID string and -F is
	# specified, just install the new file.
	if [ -n "$FREEBSD_ID" -a $newdestcmp -eq $COMPARE_DIFFFILES ] && \
	    fbsdid_only $NEWTREE/$file $DESTDIR/$file; then
		if update_unmodified $file; then
			return
		else
			panic "Updating FreeBSD ID string failed"
		fi
	fi

	# If the local file is the same as the old file, install the
	# new file.  If -F is specified and the only local change is
	# in the FreeBSD ID string, then install the new file as well.
	if compare_fbsdid $OLDTREE/$file $DESTDIR/$file; then
		if update_unmodified $file; then
			return
		fi
	fi

	# If the file was removed from the dest tree, just whine.
	if [ $newdestcmp -eq $COMPARE_ONLYFIRST ]; then
		# If the removed file matches an ALWAYS_INSTALL glob,
		# then just install the new version of the file.
		if always_install $file; then
			log "ALWAYS: adding $file"
			if ! [ -d $NEWTREE/$file ]; then
				if install_new $file; then
					echo "  A $file"
				fi
			fi
			return
		fi

		# If the only change in the new file versus the old
		# file is a change in the FreeBSD ID string and -F is
		# specified, don't warn.
		if [ -n "$FREEBSD_ID" -a $cmp -eq $COMPARE_DIFFFILES ] && \
		    fbsdid_only $OLDTREE/$file $NEWTREE/$file; then
			return
		fi

		case $cmp in
			$COMPARE_DIFFTYPE)
				old=`file_type $OLDTREE/$file`
				new=`file_type $NEWTREE/$file`
				warn "Remove mismatch: $file ($old became $new)"
				;;
			$COMPARE_DIFFLINKS)
				old=`readlink $OLDTREE/$file`
				new=`readlink $NEWTREE/$file`
				warn \
		"Removed link changed: $file (\"$old\" became \"$new\")"
				;;
			$COMPARE_DIFFFILES)
				warn "Removed file changed: $file"
				;;
		esac
		return
	fi

	# Treat the file as unmodified and force install of the new
	# file if it matches an ALWAYS_INSTALL glob.  If the update
	# attempt fails, then fall through to the normal case so a
	# warning is generated.
	if always_install $file; then
		log "ALWAYS: updating $file"
		if update_unmodified $file; then
			return
		fi
	fi

	# If the only change in the new file versus the old file is a
	# change in the FreeBSD ID string and -F is specified, just
	# update the FreeBSD ID string in the local file.
	if [ -n "$FREEBSD_ID" -a $cmp -eq $COMPARE_DIFFFILES ] && \
	    fbsdid_only $OLDTREE/$file $NEWTREE/$file; then
		if update_freebsdid $file; then
			continue
		fi
	fi

	# If the file changed types between the old and new trees but
	# the files in the new and dest tree are both of the same
	# type, treat it like an added file just comparing the new and
	# dest files.
	if [ $cmp -eq $COMPARE_DIFFTYPE ]; then
		case $newdestcmp in
			$COMPARE_DIFFLINKS)
				new=`readlink $NEWTREE/$file`
				dest=`readlink $DESTDIR/$file`
				warn \
			"New link conflict: $file (\"$new\" vs \"$dest\")"
				return
				;;
			$COMPARE_DIFFFILES)
				new_conflict $file
				echo "  C $file"
				return
				;;
		esac
	else
		# If the file has not changed types between the old
		# and new trees, but it is a different type in
		# DESTDIR, then just warn.
		if [ $newdestcmp -eq $COMPARE_DIFFTYPE ]; then
			new=`file_type $NEWTREE/$file`
			dest=`file_type $DESTDIR/$file`
			warn "Modified mismatch: $file ($new vs $dest)"
			return
		fi
	fi

	case $cmp in
		$COMPARE_DIFFTYPE)
			old=`file_type $OLDTREE/$file`
			new=`file_type $NEWTREE/$file`
			dest=`file_type $DESTDIR/$file`
			warn "Modified $dest changed: $file ($old became $new)"
			;;
		$COMPARE_DIFFLINKS)
			old=`readlink $OLDTREE/$file`
			new=`readlink $NEWTREE/$file`
			warn \
		"Modified link changed: $file (\"$old\" became \"$new\")"
			;;
		$COMPARE_DIFFFILES)
			merge_file $file
			;;
	esac
}

# Handle a file that has been added in the new tree.  If the file does
# not exist in DESTDIR, simply copy the file into DESTDIR.  If the
# file exists in the DESTDIR and is identical to the new version, do
# nothing.  Otherwise, generate a diff of the two versions of the file
# and mark it as a conflict.
#
# $1 - pathname of the file (relative to DESTDIR)
handle_added_file()
{
	local cmp dest file new

	file=$1
	if ignore $file; then
		log "IGNORE: added file $file"
		return
	fi

	compare $DESTDIR/$file $NEWTREE/$file
	cmp=$?
	case $cmp in
		$COMPARE_EQUAL)
			return
			;;
		$COMPARE_ONLYFIRST)
			panic "Added file now missing"
			;;
		$COMPARE_ONLYSECOND)
			# Ignore new directories.  They will be
			# created as needed when non-directory nodes
			# are installed.
			if ! [ -d $NEWTREE/$file ]; then
				if install_new $file; then
					echo "  A $file"
				fi
			fi
			return
			;;
	esac


	# Treat the file as unmodified and force install of the new
	# file if it matches an ALWAYS_INSTALL glob.  If the update
	# attempt fails, then fall through to the normal case so a
	# warning is generated.
	if always_install $file; then
		log "ALWAYS: updating $file"
		if update_unmodified $file; then
			return
		fi
	fi

	case $cmp in
		$COMPARE_DIFFTYPE)
			new=`file_type $NEWTREE/$file`
			dest=`file_type $DESTDIR/$file`
			warn "New file mismatch: $file ($new vs $dest)"
			;;
		$COMPARE_DIFFLINKS)
			new=`readlink $NEWTREE/$file`
			dest=`readlink $DESTDIR/$file`
			warn "New link conflict: $file (\"$new\" vs \"$dest\")"
			;;
		$COMPARE_DIFFFILES)
			# If the only change in the new file versus
			# the destination file is a change in the
			# FreeBSD ID string and -F is specified, just
			# install the new file.
			if [ -n "$FREEBSD_ID" ] && \
			    fbsdid_only $NEWTREE/$file $DESTDIR/$file; then
				if update_unmodified $file; then
					return
				else
					panic \
					"Updating FreeBSD ID string failed"
				fi
			fi

			new_conflict $file
			echo "  C $file"
			;;
	esac
}

# Main routines for each command

# Build a new tree and save it in a tarball.
build_cmd()
{
	local dir

	if [ $# -ne 1 ]; then
		echo "Missing required tarball."
		echo
		usage
	fi

	log "build command: $1"

	# Create a temporary directory to hold the tree
	dir=`mktemp -d $WORKDIR/etcupdate-XXXXXXX`
	if [ $? -ne 0 ]; then
		echo "Unable to create temporary directory."
		exit 1
	fi
	if ! build_tree $dir; then
		echo "Failed to build tree."
		remove_tree $dir
		exit 1
	fi
	if ! tar cfj $1 -C $dir . >&3 2>&1; then
		echo "Failed to create tarball."
		remove_tree $dir
		exit 1
	fi
	remove_tree $dir
}

# Output a diff comparing the tree at DESTDIR to the current
# unmodified tree.  Note that this diff does not include files that
# are present in DESTDIR but not in the unmodified tree.
diff_cmd()
{
	local file

	if [ $# -ne 0 ]; then
		usage
	fi

	# Requires an unmodified tree to diff against.
	if ! [ -d $NEWTREE ]; then
		echo "Reference tree to diff against unavailable."
		exit 1
	fi

	# Unfortunately, diff alone does not quite provide the right
	# level of options that we want, so improvise.
	for file in `(cd $NEWTREE; find .) | sed -e 's/^\.//'`; do
		if ignore $file; then
			continue
		fi

		diffnode $NEWTREE "$DESTDIR" $file "stock" "local"
	done
}

# Just extract a new tree into NEWTREE either by building a tree or
# extracting a tarball.  This can be used to bootstrap updates by
# initializing the current "stock" tree to match the currently
# installed system.
#
# Unlike 'update', this command does not rotate or preserve an
# existing NEWTREE, it just replaces any existing tree.
extract_cmd()
{

	if [ $# -ne 0 ]; then
		usage
	fi

	log "extract command: tarball=$tarball"

	if [ -d $NEWTREE ]; then
		if ! remove_tree $NEWTREE; then
			echo "Unable to remove current tree."
			exit 1
		fi
	fi

	extract_tree
}

# Resolve conflicts left from an earlier merge.
resolve_cmd()
{
	local conflicts

	if [ $# -ne 0 ]; then
		usage
	fi

	if ! [ -d $CONFLICTS ]; then
		return
	fi

	if ! [ -d $NEWTREE ]; then
		echo "The current tree is not present to resolve conflicts."
		exit 1
	fi

	conflicts=`(cd $CONFLICTS; find . ! -type d) | sed -e 's/^\.//'`
	for file in $conflicts; do
		resolve_conflict $file
	done

	if [ -n "$NEWALIAS_WARN" ]; then
		warn "Needs update: /etc/mail/aliases.db" \
		    "(requires manual update via newaliases(1))"
		echo
		echo "Warnings:"
		echo "  Needs update: /etc/mail/aliases.db" \
		    "(requires manual update via newaliases(1))"
	fi
}

# Report a summary of the previous merge.  Specifically, list any
# remaining conflicts followed by any warnings from the previous
# update.
status_cmd()
{

	if [ $# -ne 0 ]; then
		usage
	fi

	if [ -d $CONFLICTS ]; then
		(cd $CONFLICTS; find . ! -type d) | sed -e 's/^\./  C /'
	fi
	if [ -s $WARNINGS ]; then
		echo "Warnings:"
		cat $WARNINGS
	fi
}

# Perform an actual merge.  The new tree can either already exist (if
# rerunning a merge), be extracted from a tarball, or generated from a
# source tree.
update_cmd()
{
	local dir

	if [ $# -ne 0 ]; then
		usage
	fi

	log "update command: rerun=$rerun tarball=$tarball preworld=$preworld"

	if [ `id -u` -ne 0 ]; then
		echo "Must be root to update a tree."
		exit 1
	fi

	# Enforce a sane umask
	umask 022

	# XXX: Should existing conflicts be ignored and removed during
	# a rerun?

	# Trim the conflicts tree.  Whine if there is anything left.
	if [ -e $CONFLICTS ]; then
		find -d $CONFLICTS -type d -empty -delete >&3 2>&1
		rmdir $CONFLICTS >&3 2>&1
	fi
	if [ -d $CONFLICTS ]; then
		echo "Conflicts remain from previous update, aborting."
		exit 1
	fi

	if [ -z "$rerun" ]; then
		# For a dryrun that is not a rerun, do not rotate the existing
		# stock tree.  Instead, extract a tree to a temporary directory
		# and use that for the comparison.
		if [ -n "$dryrun" ]; then
			dir=`mktemp -d $WORKDIR/etcupdate-XXXXXXX`
			if [ $? -ne 0 ]; then
				echo "Unable to create temporary directory."
				exit 1
			fi

			# A pre-world dryrun has already set OLDTREE to
			# point to the current stock tree.
			if [ -z "$preworld" ]; then
				OLDTREE=$NEWTREE
			fi
			NEWTREE=$dir

		# For a pre-world update, blow away any pre-existing
		# NEWTREE.
		elif [ -n "$preworld" ]; then
			if ! remove_tree $NEWTREE; then
				echo "Unable to remove pre-world tree."
				exit 1
			fi

		# Rotate the existing stock tree to the old tree.
		elif [ -d $NEWTREE ]; then
			# First, delete the previous old tree if it exists.
			if ! remove_tree $OLDTREE; then
				echo "Unable to remove old tree."
				exit 1
			fi

			# Move the current stock tree.
			if ! mv $NEWTREE $OLDTREE >&3 2>&1; then
				echo "Unable to rename current stock tree."
				exit 1
			fi
		fi

		if ! [ -d $OLDTREE ]; then
			cat <<EOF
No previous tree to compare against, a sane comparison is not possible.
EOF
			log "No previous tree to compare against."
			if [ -n "$dir" ]; then
				rmdir $dir
			fi
			exit 1
		fi

		# Populate the new tree.
		extract_tree
	fi

	# Build lists of nodes in the old and new trees.
	(cd $OLDTREE; find .) | sed -e 's/^\.//' | sort > $WORKDIR/old.files
	(cd $NEWTREE; find .) | sed -e 's/^\.//' | sort > $WORKDIR/new.files

	# Split the files up into three groups using comm.
	comm -23 $WORKDIR/old.files $WORKDIR/new.files > $WORKDIR/removed.files
	comm -13 $WORKDIR/old.files $WORKDIR/new.files > $WORKDIR/added.files
	comm -12 $WORKDIR/old.files $WORKDIR/new.files > $WORKDIR/both.files

	# Initialize conflicts and warnings handling.
	rm -f $WARNINGS
	mkdir -p $CONFLICTS

	# Ignore removed files for the pre-world case.  A pre-world
	# update uses a stripped-down tree.
	if [ -n "$preworld" ]; then
		> $WORKDIR/removed.files
	fi
	
	# The order for the following sections is important.  In the
	# odd case that a directory is converted into a file, the
	# existing subfiles need to be removed if possible before the
	# file is converted.  Similarly, in the case that a file is
	# converted into a directory, the file needs to be converted
	# into a directory if possible before the new files are added.

	# First, handle removed files.
	for file in `cat $WORKDIR/removed.files`; do
		handle_removed_file $file
	done

	# For the directory pass, reverse sort the list to effect a
	# depth-first traversal.  This is needed to ensure that if a
	# directory with subdirectories is removed, the entire
	# directory is removed if there are no local modifications.
	for file in `sort -r $WORKDIR/removed.files`; do
		handle_removed_directory $file
	done

	# Second, handle files that exist in both the old and new
	# trees.
	for file in `cat $WORKDIR/both.files`; do
		handle_modified_file $file
	done

	# Finally, handle newly added files.
	for file in `cat $WORKDIR/added.files`; do
		handle_added_file $file
	done

	if [ -n "$NEWALIAS_WARN" ]; then
		warn "Needs update: /etc/mail/aliases.db" \
		    "(requires manual update via newaliases(1))"
	fi

	# Run any special one-off commands after an update has completed.
	post_update

	if [ -s $WARNINGS ]; then
		echo "Warnings:"
		cat $WARNINGS
	fi

	if [ -n "$dir" ]; then
		if [ -z "$dryrun" -o -n "$rerun" ]; then
			panic "Should not have a temporary directory"
		fi
		
		remove_tree $dir
	fi
}

# Determine which command we are executing.  A command may be
# specified as the first word.  If one is not specified then 'update'
# is assumed as the default command.
command="update"
if [ $# -gt 0 ]; then
	case "$1" in
		build|diff|extract|status|resolve)
			command="$1"
			shift
			;;
		-*)
			# If first arg is an option, assume the
			# default command.
			;;
		*)
			usage
			;;
	esac
fi

# Set default variable values.

# The path to the source tree used to build trees.
SRCDIR=/usr/src

# The destination directory where the modified files live.
DESTDIR=

# Ignore changes in the FreeBSD ID string.
FREEBSD_ID=

# Files that should always have the new version of the file installed.
ALWAYS_INSTALL=

# Files to ignore and never update during a merge.
IGNORE_FILES=

# Flags to pass to 'make' when building a tree.
MAKE_OPTIONS=

# Include a config file if it exists.  Note that command line options
# override any settings in the config file.  More details are in the
# manual, but in general the following variables can be set:
# - ALWAYS_INSTALL
# - DESTDIR
# - EDITOR
# - FREEBSD_ID
# - IGNORE_FILES
# - LOGFILE
# - MAKE_OPTIONS
# - SRCDIR
# - WORKDIR
if [ -r /etc/etcupdate.conf ]; then
	. /etc/etcupdate.conf
fi

# Parse command line options
tarball=
rerun=
always=
dryrun=
ignore=
nobuild=
preworld=
while getopts "d:nprs:t:A:BD:FI:L:M:" option; do
	case "$option" in
		d)
			WORKDIR=$OPTARG
			;;
		n)
			dryrun=YES
			;;
		p)
			preworld=YES
			;;
		r)
			rerun=YES
			;;
		s)
			SRCDIR=$OPTARG
			;;
		t)
			tarball=$OPTARG
			;;
		A)
			# To allow this option to be specified
			# multiple times, accumulate command-line
			# specified patterns in an 'always' variable
			# and use that to overwrite ALWAYS_INSTALL
			# after parsing all options.  Need to be
			# careful here with globbing expansion.
			set -o noglob
			always="$always $OPTARG"
			set +o noglob
			;;
		B)
			nobuild=YES
			;;
		D)
			DESTDIR=$OPTARG
			;;
		F)
			FREEBSD_ID=YES
			;;
		I)
			# To allow this option to be specified
			# multiple times, accumulate command-line
			# specified patterns in an 'ignore' variable
			# and use that to overwrite IGNORE_FILES after
			# parsing all options.  Need to be careful
			# here with globbing expansion.
			set -o noglob
			ignore="$ignore $OPTARG"
			set +o noglob
			;;
		L)
			LOGFILE=$OPTARG
			;;
		M)
			MAKE_OPTIONS="$OPTARG"
			;;
		*)
			echo
			usage
			;;
	esac
done
shift $((OPTIND - 1))

# Allow -A command line options to override ALWAYS_INSTALL set from
# the config file.
set -o noglob
if [ -n "$always" ]; then
	ALWAYS_INSTALL="$always"
fi

# Allow -I command line options to override IGNORE_FILES set from the
# config file.
if [ -n "$ignore" ]; then
	IGNORE_FILES="$ignore"
fi
set +o noglob

# Where the "old" and "new" trees are stored.
WORKDIR=${WORKDIR:-$DESTDIR/var/db/etcupdate}

# Log file for verbose output from program that are run.  The log file
# is opened on fd '3'.
LOGFILE=${LOGFILE:-$WORKDIR/log}

# The path of the "old" tree
OLDTREE=$WORKDIR/old

# The path of the "new" tree
NEWTREE=$WORKDIR/current

# The path of the "conflicts" tree where files with merge conflicts are saved.
CONFLICTS=$WORKDIR/conflicts

# The path of the "warnings" file that accumulates warning notes from an update.
WARNINGS=$WORKDIR/warnings

# Use $EDITOR for resolving conflicts.  If it is not set, default to vi.
EDITOR=${EDITOR:-/usr/bin/vi}

# Files that need to be updated before installworld.
PREWORLD_FILES="etc/master.passwd etc/group"

# Handle command-specific argument processing such as complaining
# about unsupported options.  Since the configuration file is always
# included, do not complain about extra command line arguments that
# may have been set via the config file rather than the command line.
case $command in
	update)
		if [ -n "$rerun" -a -n "$tarball" ]; then
			echo "Only one of -r or -t can be specified."
			echo
			usage
		fi
		if [ -n "$rerun" -a -n "$preworld" ]; then
			echo "Only one of -p or -r can be specified."
			echo
			usage
		fi
		;;
	build|diff|status)
		if [ -n "$dryrun" -o -n "$rerun" -o -n "$tarball" -o \
		     -n "$preworld" ]; then
			usage
		fi
		;;
	resolve)
		if [ -n "$dryrun" -o -n "$rerun" -o -n "$tarball" ]; then
			usage
		fi
		;;
	extract)
		if [ -n "$dryrun" -o -n "$rerun" -o -n "$preworld" ]; then
			usage
		fi
		;;
esac

# Pre-world mode uses a different set of trees.  It leaves the current
# tree as-is so it is still present for a full etcupdate run after the
# world install is complete.  Instead, it installs a few critical files
# into a separate tree.
if [ -n "$preworld" ]; then
	OLDTREE=$NEWTREE
	NEWTREE=$WORKDIR/preworld
fi

# Open the log file.  Don't truncate it if doing a minor operation so
# that a minor operation doesn't lose log info from a major operation.
if ! mkdir -p $WORKDIR 2>/dev/null; then
	echo "Failed to create work directory $WORKDIR"
fi

case $command in
	diff|resolve|status)
		exec 3>>$LOGFILE
		;;
	*)
		exec 3>$LOGFILE
		;;
esac

${command}_cmd "$@"
