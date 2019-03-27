#! /bin/sh
# Make sure all these programs work properly
# when invoked with --help or --version.

# Copyright (C) 2000-2016 Free Software Foundation, Inc.

# This program is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 3 of the License, or
# (at your option) any later version.

# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.

# You should have received a copy of the GNU General Public License
# along with this program.  If not, see <http://www.gnu.org/licenses/>.

# Ensure that $SHELL is set to *some* value and exported.
# This is required for dircolors, which would fail e.g., when
# invoked via debuild (which removes SHELL from the environment).
test "x$SHELL" = x && SHELL=/bin/sh
export SHELL

. "${srcdir=.}/init.sh"; path_prepend_ .

expected_failure_status_chroot=125
expected_failure_status_env=125
expected_failure_status_nice=125
expected_failure_status_nohup=125
expected_failure_status_stdbuf=125
expected_failure_status_su=125
expected_failure_status_timeout=125
expected_failure_status_printenv=2
expected_failure_status_tty=3
expected_failure_status_sort=2
expected_failure_status_expr=3
expected_failure_status_lbracket=2
expected_failure_status_dir=2
expected_failure_status_ls=2
expected_failure_status_vdir=2

expected_failure_status_cmp=2
expected_failure_status_zcmp=2
expected_failure_status_sdiff=2
expected_failure_status_diff3=2
expected_failure_status_diff=2
expected_failure_status_zdiff=2
expected_failure_status_zgrep=2
expected_failure_status_zegrep=2
expected_failure_status_zfgrep=2

expected_failure_status_grep=2
expected_failure_status_egrep=2
expected_failure_status_fgrep=2

test "$built_programs" \
  || fail_ "built_programs not specified!?!"

test "$VERSION" \
  || fail_ "set envvar VERSION; it is required for a PATH sanity-check"

# Extract version from --version output of the first program
for i in $built_programs; do
  v=$(env $i --version | sed -n '1s/.* //p;q')
  break
done

# Ensure that it matches $VERSION.
test "x$v" = "x$VERSION" \
  || fail_ "--version-\$VERSION mismatch"

for lang in C fr da; do
  for i in $built_programs; do

    # Skip `test'; it doesn't accept --help or --version.
    test $i = test && continue;

    # false fails even when invoked with --help or --version.
    if test $i = false; then
      env LC_MESSAGES=$lang $i --help >/dev/null && fail=1
      env LC_MESSAGES=$lang $i --version >/dev/null && fail=1
      continue
    fi

    args=

    # The just-built install executable is always named `ginstall'.
    test $i = install && i=ginstall

    # Make sure they exit successfully, under normal conditions.
    eval "env \$i $args --help    > h-\$i   " || fail=1
    eval "env \$i $args --version >/dev/null" || fail=1

    # Make sure they mention the bug-reporting address in --help output.
    grep "$PACKAGE_BUGREPORT" h-$i > /dev/null || fail=1
    rm -f h-$i

    # Make sure they fail upon `disk full' error.
    if test -w /dev/full && test -c /dev/full; then
      eval "env \$i $args --help    >/dev/full 2>/dev/null" && fail=1
      eval "env \$i $args --version >/dev/full 2>/dev/null" && fail=1
      status=$?
      test $i = [ && prog=lbracket || prog=$i
      eval "expected=\$expected_failure_status_$prog"
      test x$expected = x && expected=1
      if test $status = $expected; then
        : # ok
      else
        fail=1
        echo "*** $i: bad exit status \`$status' (expected $expected)," 1>&2
        echo "  with --help or --version output redirected to /dev/full" 1>&2
      fi
    fi
  done
done

bigZ_in=bigZ-in.Z
zin=zin.gz
zin2=zin2.gz

tmp=tmp-$$
tmp_in=in-$$
tmp_in2=in2-$$
tmp_dir=dir-$$
tmp_out=out-$$
mkdir $tmp || fail=1
cd $tmp || fail=1

comm_setup () { args="$tmp_in $tmp_in"; }
csplit_setup () { args="$tmp_in //"; }
cut_setup () { args='-f 1'; }
join_setup () { args="$tmp_in $tmp_in"; }
tr_setup () { args='a a'; }

chmod_setup () { args="a+x $tmp_in"; }
# Punt on these.
chgrp_setup () { args=--version; }
chown_setup () { args=--version; }
mkfifo_setup () { args=--version; }
mknod_setup () { args=--version; }
# Punt on uptime, since it fails (e.g., failing to get boot time)
# on some systems, and we shouldn't let that stop `make check'.
uptime_setup () { args=--version; }

# Create a file in the current directory, not in $TMPDIR.
mktemp_setup () { args=mktemp.XXXX; }

cmp_setup () { args="$tmp_in $tmp_in2"; }

# Tell dd not to print the line with transfer rate and total.
# The transfer rate would vary between runs.
dd_setup () { args=status=noxfer; }

zdiff_setup () { args="$args $zin $zin2"; }
zcmp_setup () { zdiff_setup; }
zcat_setup () { args="$args $zin"; }
gunzip_setup () { zcat_setup; }
zmore_setup () { zcat_setup; }
zless_setup () { zcat_setup; }
znew_setup () { args="$args $bigZ_in"; }
zforce_setup () { zcat_setup; }
zgrep_setup () { args="$args z $zin"; }
zegrep_setup () { zgrep_setup; }
zfgrep_setup () { zgrep_setup; }
gzexe_setup () { args="$args $tmp_in"; }

# We know that $tmp_in contains a "0"
grep_setup () { args="0 $tmp_in"; }
egrep_setup () { args="0 $tmp_in"; }
fgrep_setup () { args="0 $tmp_in"; }

diff_setup () { args="$tmp_in $tmp_in2"; }
sdiff_setup () { args="$tmp_in $tmp_in2"; }
diff3_setup () { args="$tmp_in $tmp_in2 $tmp_in2"; }
cp_setup () { args="$tmp_in $tmp_in2"; }
ln_setup () { args="$tmp_in ln-target"; }
ginstall_setup () { args="$tmp_in $tmp_in2"; }
mv_setup () { args="$tmp_in $tmp_in2"; }
mkdir_setup () { args=$tmp_dir/subdir; }
rmdir_setup () { args=$tmp_dir; }
rm_setup () { args=$tmp_in; }
shred_setup () { args=$tmp_in; }
touch_setup () { args=$tmp_in2; }
truncate_setup () { args="--reference=$tmp_in $tmp_in2"; }

basename_setup () { args=$tmp_in; }
dirname_setup () { args=$tmp_in; }
expr_setup () { args=foo; }

# Punt, in case GNU `id' hasn't been installed yet.
groups_setup () { args=--version; }

pathchk_setup () { args=$tmp_in; }
yes_setup () { args=--version; }
logname_setup () { args=--version; }
nohup_setup () { args=--version; }
printf_setup () { args=foo; }
seq_setup () { args=10; }
sleep_setup () { args=0; }
su_setup () { args=--version; }
stdbuf_setup () { args="-oL true"; }
timeout_setup () { args=--version; }

# I'd rather not run sync, since it spins up disks that I've
# deliberately caused to spin down (but not unmounted).
sync_setup () { args=--version; }

test_setup () { args=foo; }

# This is necessary in the unusual event that there is
# no valid entry in /etc/mtab.
df_setup () { args=/; }

# This is necessary in the unusual event that getpwuid (getuid ()) fails.
id_setup () { args=-u; }

# Use env to avoid invoking built-in sleep of Solaris 11's /bin/sh.
kill_setup () {
  env sleep 10m &
  args=$!
}

link_setup () { args="$tmp_in link-target"; }
unlink_setup () { args=$tmp_in; }

readlink_setup () {
  ln -s . slink
  args=slink;
}

stat_setup () { args=$tmp_in; }
unlink_setup () { args=$tmp_in; }
lbracket_setup () { args=": ]"; }

# Ensure that each program "works" (exits successfully) when doing
# something more than --help or --version.
for i in $built_programs; do
  # Skip these.
  case $i in chroot|stty|tty|false|chcon|runcon) continue;; esac

  rm -rf $tmp_in $tmp_in2 $tmp_dir $tmp_out $bigZ_in $zin $zin2
  echo z |gzip > $zin
  cp $zin $zin2
  cp $zin $bigZ_in

  # This is sort of kludgey: use numbers so this is valid input for factor,
  # and two tokens so it's valid input for tsort.
  echo 2147483647 0 > $tmp_in
  # Make $tmp_in2 identical. Then, using $tmp_in and $tmp_in2 as arguments
  # to the likes of cmp and diff makes them exit successfully.
  cp $tmp_in $tmp_in2
  mkdir $tmp_dir
  # echo ================== $i
  test $i = [ && prog=lbracket || prog=$i
  args=
  if type ${prog}_setup > /dev/null 2>&1; then
    ${prog}_setup
  fi
  if eval "env \$i $args < \$tmp_in > \$tmp_out"; then
    : # ok
  else
    echo FAIL: $i
    fail=1
  fi
  rm -rf $tmp_in $tmp_in2 $tmp_out $tmp_dir
done

Exit $fail
