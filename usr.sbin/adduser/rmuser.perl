#!/usr/bin/perl
# -*- perl -*-
#
# $OpenBSD: rmuser.perl,v 1.7 2005/06/07 05:07:54 millert Exp $
#
# Copyright 1995, 1996 Guy Helmer, Madison, South Dakota 57042.
# All rights reserved.
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions
# are met:
# 1. Redistributions of source code must retain the above copyright
#    notice, this list of conditions and the following disclaimer as
#    the first lines of this file unmodified.
# 2. Redistributions in binary form must reproduce the above copyright
#    notice, this list of conditions and the following disclaimer in the
#    documentation and/or other materials provided with the distribution.
# 3. The name of the author may not be used to endorse or promote products
#    derived from this software without specific prior written permission.
#
# THIS SOFTWARE IS PROVIDED BY GUY HELMER ``AS IS'' AND ANY EXPRESS OR
# IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
# OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
# IN NO EVENT SHALL GUY HELMER BE LIABLE FOR ANY DIRECT, INDIRECT,
# INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
# NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
# DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
# THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
# (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
# THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
#
# rmuser - Perl script to remove users
#
# Guy Helmer <ghelmer@alpha.dsu.edu>, 07/17/96
#
#	$From: rmuser.perl,v 1.2 1996/12/07 21:25:12 ache Exp $

use Fcntl qw(:DEFAULT :flock);

$ENV{"PATH"} = "/bin:/sbin:/usr/bin:/usr/sbin";
umask(022);
$whoami = $0;
$passwd_file = "/etc/master.passwd";
$passwd_tmp = "/etc/ptmp";
$group_file = "/etc/group";
$new_group_file = "${group_file}.new.$$";
$mail_dir = "/var/mail";
$crontab_dir = "/var/cron/tabs";
$atjob_dir = "/var/at/jobs";

#$debug = 1;

END {
    if (-e $passwd_tmp && defined(fileno(NEW_PW))) {
	unlink($passwd_tmp) ||
	    warn "\n${whoami}: warning: couldn't unlink $passwd_tmp ($!)\n\tPlease investigate, as this file should not be left in the filesystem\n";
    }
}

sub cleanup {
    local($sig) = @_;

    print STDERR "Caught signal SIG$sig -- cleaning up.\n";
    exit(0);
}

sub open_files {
    open(GROUP, $group_file) ||
	die "\n${whoami}: Error: couldn't open ${group_file}: $!\n";
    if (!flock(GROUP, LOCK_EX|LOCK_NB)) {
	print STDERR "\n${whoami}: Error: couldn't lock ${group_file}: $!\n";
	exit 1;
    }

    sysopen(NEW_PW, $passwd_tmp, O_RDWR|O_CREAT|O_EXCL, 0600) ||
	die "\n${whoami}: Error: Password file busy\n";

    if (!open(MASTER_PW, $passwd_file)) {
	print STDERR "${whoami}: Error: Couldn't open ${passwd_file}: $!\n";
	exit(1);
    }
}

$SIG{'INT'} = 'cleanup';
$SIG{'QUIT'} = 'cleanup';
$SIG{'HUP'} = 'cleanup';
$SIG{'TERM'} = 'cleanup';

if ($#ARGV > 0) {
    print STDERR "usage: ${whoami} [username]\n";
    exit(1);
}

if ($< != 0) {
    print STDERR "${whoami}: Error: you must be root to use ${whoami}\n";
    exit(1);
}

&open_files;

if ($#ARGV == 0) {
    # Username was given as a parameter
    $login_name = pop(@ARGV);
} else {
    # Get the user name from the user
    $login_name = &get_login_name;
}

if (($pw_ent = &check_login_name($login_name)) eq '0') {
    print STDERR "${whoami}: Error: User ${login_name} not in password database\n";
    exit 1;
}

($name, $password, $uid, $gid, $class, $change, $expire, $gecos, $home_dir,
 $shell) = split(/:/, $pw_ent);

if ($uid == 0) {
    print "${whoami}: Sorry, I'd rather not remove a user with a uid of 0.\n";
    exit 1;
}

print "Matching password entry:\n\n$pw_ent\n\n";

$ans = &get_yn("Is this the entry you wish to remove? ");

if ($ans eq 'N') {
    print "User ${login_name} not removed.\n";
    exit 0;
}

#
# Get owner of user's home directory; don't remove home dir if not
# owned by $login_name

$remove_directory = 1;

if (-l $home_dir) {
    $real_home_dir = &resolvelink($home_dir);
} else {
    $real_home_dir = $home_dir;
}

#
# If home_dir is a symlink and points to something that isn't a directory,
# or if home_dir is not a symlink and is not a directory, don't remove
# home_dir -- seems like a good thing to do, but probably isn't necessary...
if (((-l $home_dir) && ((-e $real_home_dir) && !(-d $real_home_dir))) ||
    (!(-l $home_dir) && !(-d $home_dir))) {
    print STDERR "${whoami}: Home ${home_dir} is not a directory, so it won't be removed\n";
    $remove_directory = 0;
}

if (length($real_home_dir) && -d $real_home_dir) {
    $dir_owner = (stat($real_home_dir))[4]; # UID
    if ($dir_owner != $uid) {
	print STDERR "${whoami}: Home dir ${real_home_dir} is not owned by ${login_name} (uid ${dir_owner})\n";
	$remove_directory = 0;
    }
}

if ($remove_directory) {
    $ans = &get_yn("Remove user's home directory ($home_dir)? ");
    if ($ans eq 'N') {
	$remove_directory = 0;
    }
}

#exit 0 if $debug;

#
# Remove the user's crontab, if there is one
# (probably needs to be done before password databases are updated)

if (-e "$crontab_dir/$login_name") {
    print STDERR "Removing user's crontab:";
    system('/usr/bin/crontab', '-u', $login_name, '-r');
    print STDERR " done.\n";
}

#
# Remove the user's at jobs, if any
# (probably also needs to be done before password databases are updated)

&remove_at_jobs($login_name, $uid);

#
# Copy master password file to new file less removed user's entry

&update_passwd_file;

#
# Remove the user from all groups in /etc/group

&update_group_file($login_name);

#
# Remove the user's home directory

if ($remove_directory) {
    print STDERR "Removing user's home directory ($home_dir):";
    &remove_dir($home_dir);
    print STDERR " done.\n";
}

#
# Remove the user's incoming mail file

if (-e "$mail_dir/$login_name" || -l "$mail_dir/$login_name") {
    print STDERR "Removing user's incoming mail file ($mail_dir/$login_name):";
    unlink "$mail_dir/$login_name" ||
	print STDERR "\n${whoami}: warning: unlink on $mail_dir/$login_name failed ($!) - continuing\n";
    print STDERR " done.\n";
}

#
# All done!

exit 0;

sub get_login_name {
    #
    # Get new user's name
    local($login_name);

    print "Enter login name for user to remove: ";
    $login_name = <>;
    chomp $login_name;

    print "User name is ${login_name}\n" if $debug;
    return($login_name);
}

sub check_login_name {
    #
    # Check to see whether login name is in password file
    local($login_name) = @_;
    local($Mname, $Mpassword, $Muid, $Mgid, $Mclass, $Mchange, $Mexpire,
	  $Mgecos, $Mhome_dir, $Mshell);
    local($i);

    seek(MASTER_PW, 0, 0);
    while ($i = <MASTER_PW>) {
	chomp $i;
	($Mname, $Mpassword, $Muid, $Mgid, $Mclass, $Mchange, $Mexpire,
	 $Mgecos, $Mhome_dir, $Mshell) = split(/:/, $i);
	if ($Mname eq $login_name) {
	    seek(MASTER_PW, 0, 0);
	    return($i);		# User is in password database
	}
    }
    seek(MASTER_PW, 0, 0);

    return '0';			# User wasn't found
}

sub get_yn {
    #
    # Get a yes or no answer; return 'Y' or 'N'
    local($prompt) = @_;
    local($done, $ans);

    for ($done = 0; ! $done; ) {
	print $prompt;
	$ans = <>;
	chomp $ans;
	$ans =~ tr/a-z/A-Z/;
	if (!($ans =~ /^[YN]/)) {
	    print STDERR "Please answer (y)es or (n)o.\n";
	} else {
	    $done = 1;
	}
    }

    return(substr($ans, 0, 1));
}

sub update_passwd_file {
    local($skipped, $i);

    print STDERR "Updating password file,";
    seek(MASTER_PW, 0, 0);
    $skipped = 0;
    while ($i = <MASTER_PW>) {
	chomp($i);
	if ($i ne $pw_ent) {
	    print NEW_PW "$i\n";
	} else {
	    print STDERR "Dropped entry for $login_name\n" if $debug;
	    $skipped = 1;
	}
    }
    close(NEW_PW);
    seek(MASTER_PW, 0, 0);

    if ($skipped == 0) {
	print STDERR "\n${whoami}: Whoops! Didn't find ${login_name}'s entry second time around!\n";
	exit 1;
    }

    #
    # Run pwd_mkdb to install the updated password files and databases

    print STDERR " updating databases,";
    system('/usr/sbin/pwd_mkdb', '-p', ${passwd_tmp});
    print STDERR " done.\n";

    close(MASTER_PW);		# Not useful anymore
}

sub update_group_file {
    local($login_name) = @_;

    local($i, $j, $grmember_list, $new_grent);
    local($grname, $grpass, $grgid, $grmember_list, @grmembers);

    print STDERR "Updating group file:";
    local($group_perms, $group_uid, $group_gid) =
	(stat(GROUP))[2, 4, 5]; # File Mode, uid, gid
    open(NEW_GROUP, ">$new_group_file") ||
	die "\n${whoami}: Error: couldn't open ${new_group_file}: $!\n";
    chmod($group_perms, $new_group_file) ||
	printf STDERR "\n${whoami}: warning: could not set permissions of new group file to %o ($!)\n\tContinuing, but please check permissions of $group_file!\n", $group_perms;
    chown($group_uid, $group_gid, $new_group_file) ||
	print STDERR "\n${whoami}: warning: could not set owner/group of new group file to ${group_uid}/${group_gid} ($!)\n\rContinuing, but please check ownership of $group_file!\n";
    while ($i = <GROUP>) {
	if (!($i =~ /$login_name/)) {
	    # Line doesn't contain any references to the user, so just add it
	    # to the new file
	    print NEW_GROUP $i;
	} else {
	    #
	    # Remove the user from the group
	    chomp $i;
	    ($grname, $grpass, $grgid, $grmember_list) = split(/:/, $i);
	    @grmembers = split(/,/, $grmember_list);
	    undef @new_grmembers;
	    local(@new_grmembers);
	    foreach $j (@grmembers) {
		if ($j ne $login_name) {
		    push(@new_grmembers, $j);
		} elsif ($debug) {
		    print STDERR "Removing $login_name from group $grname\n";
		}
	    }
	    if ($grname eq $login_name && $#new_grmembers == -1) {
		# Remove a user's personal group if empty
		print STDERR "Removing group $grname -- personal group is empty\n";
	    } else {
		$grmember_list = join(',', @new_grmembers);
		$new_grent = join(':', $grname, $grpass, $grgid, $grmember_list);
		print NEW_GROUP "$new_grent\n";
	    }
	}
    }
    close(NEW_GROUP);
    rename($new_group_file, $group_file) || # Replace old group file with new
	die "\n${whoami}: error: couldn't rename $new_group_file to $group_file ($!)\n";
    close(GROUP);			# File handle is worthless now
    print STDERR " done.\n";
}

sub remove_dir {
    # Remove the user's home directory
    local($dir) = @_;
    local($linkdir);

    if (-l $dir) {
	$linkdir = &resolvelink($dir);
	# Remove the symbolic link
	unlink($dir) ||
	    warn "${whoami}: Warning: could not unlink symlink $dir: $!\n";
	if (!(-e $linkdir)) {
	    #
	    # Dangling symlink - just return now
	    return;
	}
	# Set dir to be the resolved pathname
	$dir = $linkdir;
    }
    if (!(-d $dir)) {
	print STDERR "${whoami}: Warning: $dir is not a directory\n";
	unlink($dir) || warn "${whoami}: Warning: could not unlink $dir: $!\n";
	return;
    }
    system('/bin/rm', '-rf', $dir);
}

sub remove_at_jobs {
    local($login_name, $uid) = @_;
    local($i, $owner, $found);

    $found = 0;
    opendir(ATDIR, $atjob_dir) || return;
    while ($i = readdir(ATDIR)) {
	next if $i eq '.';
	next if $i eq '..';
	next if $i eq '.lockfile';

	$owner = (stat("$atjob_dir/$i"))[4]; # UID
	if ($uid == $owner) {
	    if (!$found) {
		print STDERR "Removing user's at jobs:";
		$found = 1;
	    }
	    # Use atrm to remove the job
	    print STDERR " $i";
	    system('/usr/bin/atrm', $i);
	}
    }
    closedir(ATDIR);
    if ($found) {
	print STDERR " done.\n";
    }
}

sub resolvelink {
    local($path) = @_;
    local($l);

    while (-l $path && -e $path) {
	if (!defined($l = readlink($path))) {
	    die "${whoami}: readlink on $path failed (but it should have worked!): $!\n";
	}
	if ($l =~ /^\//) {
	    # Absolute link
	    $path = $l;
	} else {
	    # Relative link
	    $path =~ s/\/[^\/]+\/?$/\/$l/; # Replace last component of path
	}
    }
    return $path;
}
