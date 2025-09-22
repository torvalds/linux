#!/usr/bin/perl
#
#	$OpenBSD: adduser.perl,v 1.63 2014/10/01 09:56:36 mpi Exp $
#
# Copyright (c) 1995-1996 Wolfram Schneider <wosch@FreeBSD.org>. Berlin.
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
# $From: adduser.perl,v 1.22 1996/12/07 21:25:12 ache Exp $

use IPC::Open2;
use Fcntl qw(:DEFAULT :flock);

################
# main
#
$check_only = 0;

$SIG{'INT'} = 'cleanup';
$SIG{'QUIT'} = 'cleanup';
$SIG{'HUP'} = 'cleanup';
$SIG{'TERM'} = 'cleanup';

&check_root;			# you must be root to run this script!
&variables;			# initialize variables
&config_read(@ARGV);		# read variables from config-file
&parse_arguments(@ARGV);	# parse arguments

if (!$check_only && $#batch < 0) {
    &hints;
}

# check
$changes = 0;
&variable_check;		# check for valid variables
&passwd_check;			# check for valid passwdb
&shells_read;			# read /etc/shells
&login_conf_read;		# read /etc/login.conf
&passwd_read;			# read /etc/master.passwd
&group_read;			# read /etc/group
&group_check;			# check for incon*
exit 0 if $check_only;		# only check consistence and exit

exit(!&batch(@batch)) if $#batch >= 0; # batch mode

# Interactive:
# main loop for creating new users
&new_users;	     # add new users

#end


# Set adduser "default" variables internally before groking config file
# Adduser.conf supersedes these
sub variables {
    $verbose = 1;		# verbose = [0-2]
    $defaultpasswd = "yes";	# use password for new users
    $dotdir = "/etc/skel";	# copy dotfiles from this dir
    $dotdir_bak = $dotdir;
    $send_message = "no"; 	# send message to new user
    $message_file = "/etc/adduser.message";
    $config = "/etc/adduser.conf"; # config file for adduser
    $config_read = 1;		# read config file
    $logfile = "/var/log/adduser"; # logfile
    $home = "/home";		# default HOME
    $etc_shells = "/etc/shells";
    $etc_passwd = "/etc/master.passwd";
    $etc_ptmp = "/etc/ptmp";
    $group = "/etc/group";
    $etc_login_conf = "/etc/login.conf";
    @pwd_mkdb = ("pwd_mkdb", "-p");	# program for building passwd database
    $encryptionmethod = "auto";

    # List of directories where shells located
    @path = ('/bin', '/usr/bin', '/usr/local/bin');
    # common shells, first element has higher priority
    @shellpref = ('csh', 'sh', 'bash', 'tcsh', 'ksh');

    @encryption_methods = ('auto', 'blowfish' );

    $defaultshell = 'ksh';	# defaultshell if not empty
    $group_uniq = 'USER';
    $defaultgroup = $group_uniq;# login groupname, $group_uniq means username
    $defaultclass = 'default';  # default user login class

    $uid_start = 1000;		# new users get this uid
    $uid_end   = 2147483647;	# max. uid

    # global variables
    # passwd
    %username = ();		# $username{username} = uid
    %uid = ();			# $uid{uid} = username
    %pwgid = ();		# $pwgid{pwgid} = username; gid from passwd db

    $password = '';		# password for new users

    # group
    %groupname = ();		# $groupname{groupname} = gid
    %groupmembers = ();		# $groupmembers{gid} = members of group/kommalist
    %gid = ();			# $gid{gid} = groupname;    gid from group db

    # shell
    %shell = ();		# $shell{`basename sh`} = sh

    umask 022;			# don't give login group write access

    # regexs used in determining user supplied yes/no
    $yes = qr/^(yes|YES|y|Y)$/;
    $no = qr/^(no|NO|n|N)$/;

    $ENV{'PATH'} = "/sbin:/bin:/usr/sbin:/usr/bin";
    @passwd_backup = ();
    @group_backup = ();
    @message_buffer = ();
    @login_classes = ();
    @user_variable_list = ();	# user variables in /etc/adduser.conf
    $do_not_delete = '## DO NOT DELETE THIS LINE!';
}

sub login_conf_read {
     foreach (`getcap -f $etc_login_conf -a -s localcipher`) {
	chomp;
	s/:.*//;
	push(@login_classes, $_);
     }
}

# read shell database, see also: shells(5)
sub shells_read {
    local($sh);
    local($err) = 0;

    print "Reading $etc_shells\n" if $verbose;
    open(S, $etc_shells) || die "$etc_shells: $!\n";

    while(<S>) {
	if (/^\s*\//) {
	    s/^\s*//; s/\s+.*//; # chop
	    $sh = $_;
	    if (-x  $sh) {
		$shell{&basename($sh)} = $sh;
	    } else {
		warn "Shell: $sh not executable!\n";
		$err++;
	    }
	}
    }
    close(S);

    push(@list, "/sbin/nologin");
    &shell_pref_add("nologin");
    $shell{"nologin"} = "/sbin/nologin";

    return $err;
}

# add new shells if possible
sub shells_add {
    local($sh,$dir,@list);

    return 1 unless $verbose;

    foreach $sh (@shellpref) {
	# all known shells
	if (!$shell{$sh}) {
	    # shell $sh is not defined as login shell
	    foreach $dir (@path) {
		if (-x "$dir/$sh") {
		    # found shell
		    if (&confirm_yn("Found shell: $dir/$sh. Add to $etc_shells?", "yes")) {
			push(@list, "$dir/$sh");
			&shell_pref_add("$sh");
			$shell{&basename("$dir/$sh")} = "$dir/$sh";
			$changes++;
		    }
		}
	    }
	}
    }
    &append_file($etc_shells, @list) if $#list >= 0;
}

# add shell to preference list without duplication
sub shell_pref_add {
    local($new_shell) = @_;
    local($shell);

    foreach $shell (@shellpref) {
	return if ($shell eq $new_shell);
    }
    push(@shellpref, $new_shell);
}

# choose your favourite shell and return the shell
sub shell_default {
    local($e,$i,$new_shell);
    local($sh);

    $sh = &shell_default_valid($defaultshell);
    return $sh unless $verbose;

    $new_shell = &confirm_list("Enter your default shell:", 0,
		       $sh, sort(keys %shell));
    print "Your default shell is: $new_shell -> $shell{$new_shell}\n";
    $changes++ if $new_shell ne $sh;
    return $new_shell;
}

sub shell_default_valid {
    local($sh) = @_;
    local($s,$e);

    return $sh if $shell{$sh};

    foreach $e (@shellpref) {
	$s = $e;
	last if defined($shell{$s});
    }
    $s = "sh" unless $s;
    warn "Shell ``$sh'' is undefined, use ``$s''\n";
    return $s;
}

# return default home partition (e.g. "/home")
# create base directory if necessary
sub home_partition {
    local($home) = @_;
    $home = &stripdir($home);
    local($h) = $home;

    return $h if !$verbose && $h eq &home_partition_valid($h);

    while(1) {
	$h = &confirm_list("Enter your default HOME partition:", 1, $home, "");
	$h = &stripdir($h);
	last if $h eq &home_partition_valid($h);
    }

    $changes++ if $h ne $home;
    return $h;
}

sub home_partition_valid {
    local($h) = @_;

    $h = &stripdir($h);
    # all right (I hope)
    return $h if $h =~ "^/" && -e $h && -w _ && (-d _ || -l $h);

    # Errors or todo
    if ($h !~ "^/") {
	warn "Please use absolute path for home: ``$h''.\a\n";
	return 0;
    }

    if (-e $h) {
	warn "$h exists, but is not a directory or symlink!\n"
	    unless -d $h || -l $h;
	warn "$h is not writable!\n"
	    unless -w $h;
	return 0;
    } else {
	# create home partition
	return $h if &mkdir_home($h);
    }
    return 0;
}

# check for valid passwddb
sub passwd_check {
    system(@pwd_mkdb, "-c", $etc_passwd);
    die "\nInvalid $etc_passwd - cannot add any users!\n" if $?;
}

# read /etc/passwd
sub passwd_read {
    local($p_username, $pw, $p_uid, $p_gid, $sh);

    print "Check $etc_passwd\n" if $verbose;
    open(P, "$etc_passwd") || die "$etc_passwd: $!\n";

    # we only use this to lock the password file
    sysopen(PTMP, $etc_ptmp, O_RDWR|O_CREAT|O_EXCL, 0600) ||
	die "Password file busy\n";

    while(<P>) {
	chop;
	push(@passwd_backup, $_);
	($p_username, $pw, $p_uid, $p_gid, $sh) = (split(/:/, $_))[0..3,9];

	print "$p_username already exists with uid: $username{$p_username}!\n"
	    if $username{$p_username} && $verbose;
	$username{$p_username} = $p_uid;
	print "User $p_username: uid $p_uid exists twice: $uid{$p_uid}\n"
	    if $uid{$p_uid} && $verbose && $p_uid;    # don't warn for uid 0
	print "User $p_username: illegal shell: ``$sh''\n"
	    if ($verbose && $sh &&
		!$shell{&basename($sh)} &&
		$p_username !~ /^(news|xten|bin|nobody|uucp)$/ &&
		$sh !~ /\/pppd$/);
	$uid{$p_uid} = $p_username;
	$pwgid{$p_gid} = $p_username;
    }
    close P;
}

# read /etc/group
sub group_read {
    local($g_groupname,$pw,$g_gid, $memb);

    print "Check $group\n" if $verbose;
    open(G, "$group") || die "$group: $!\n";
    while(<G>) {
	chop;
	push(@group_backup, $_);
	($g_groupname, $pw, $g_gid, $memb) = (split(/:/, $_))[0..3];

	$groupmembers{$g_gid} = $memb;
	warn "Groupname exists twice: $g_groupname:$g_gid -> $g_groupname:$groupname{$g_groupname}\n"
	    if $groupname{$g_groupname} && $verbose;
	$groupname{$g_groupname} = $g_gid;
	warn "Groupid exists twice:   $g_groupname:$g_gid -> $gid{$g_gid}:$g_gid\n"
	    if $gid{$g_gid} && $verbose;
	$gid{$g_gid} = $g_groupname;
    }
    close G;
}

# check gids /etc/passwd <-> /etc/group
sub group_check {
    local($c_gid, $c_username, @list);

    foreach $c_gid (keys %pwgid) {
	if (!$gid{$c_gid}) {
	    $c_username = $pwgid{$c_gid};
	    warn "User ``$c_username'' has gid $c_gid but a group with this " .
		"gid does not exist.\n" if $verbose;
	}
    }
}

#
# main loop for creating new users
#

# return username
sub new_users_name {
    local($name);

    while(1) {
	$name = &confirm_list("Enter username", 1, "", "");
	if (length($name) > 31) {
	    warn "Username is longer than 31 characters\a\n";
	    next;
	}
	last if (&new_users_name_valid($name) eq $name);
    }
    return $name;
}

sub new_users_name_valid {
    local($name) = @_;

    if ($name !~ /^[a-zA-Z0-9_\.][a-zA-Z0-9_\.\-]*\$?$/ || $name eq "") {
	warn "Illegal username. " .
	    "Please see the restrictions section of the man page.\a\n";
	return 0;
    } elsif ($username{$name}) {
	warn "Username ``$name'' already exists!\a\n"; return 0;
    }
    return $name;
}

# return full name
sub new_users_fullname {
    local($name) = @_;
    local($fullname);

    while(1) {
	$fullname = &confirm_list("Enter full name", 1, "", "");
	last if $fullname eq &new_users_fullname_valid($fullname);
    }
    $fullname = $name unless $fullname;
    return $fullname;
}

sub new_users_fullname_valid {
    local($fullname) = @_;

    return $fullname if $fullname !~ /:/;

    warn "``:'' is not allowed!\a\n";
    return 0;
}

# return shell (full path) for user
sub new_users_shell {
    local($sh);

    $sh = &confirm_list("Enter shell", 0, $defaultshell, keys %shell);
    return $shell{$sh};
}

sub new_users_login_class {
    local($log_cl);
	
    $log_cl = &confirm_list("Login class", 0, $defaultclass, @login_classes);
    return($log_cl);
}

# return free uid and gid
sub new_users_id {
    local($name) = @_;
    local($u_id, $g_id) = &next_id($name);
    local($u_id_tmp, $e);

    while(1) {
	$u_id_tmp = &confirm_list("Uid", 1, $u_id, "");
	last if $u_id_tmp =~ /^[0-9]+$/ && $u_id_tmp <= $uid_end &&
		! $uid{$u_id_tmp};
	if ($uid{$u_id_tmp}) {
	    warn "Uid ``$u_id_tmp'' in use!\a\n";
	} else {
	    warn "Wrong uid.\a\n";
	}
    }
    # use calculated uid
    return ($u_id_tmp, $g_id) if $u_id_tmp eq $u_id;
    # recalculate gid
    $uid_start = $u_id_tmp;
    return &next_id($name);
}

# add user to group
sub add_group {
    local($gid, $name) = @_;

    return 0 if
	$groupmembers{$gid} =~ /^(.*,)?$name(,.*)?$/;

    $groupmembers_bak{$gid} = $groupmembers{$gid};
    $groupmembers{$gid} .= "," if $groupmembers{$gid};
    $groupmembers{$gid} .= "$name";

    local(@l) = split(',', $groupmembers{$gid});
    # group(5): A group cannot have more than 200 members.
    # The maximum line length of /etc/group is 1024 characters.
    # Longer lines will be skipped.
    if ($#l >= 200 ||
	length($groupmembers{$gid}) > 1024 - 50) { # 50 is for group name
	warn "WARNING, group line ``$gid{$gid}'' is either too long or has\n" .
	    "too many users in the group, see group(5)\a\n";
    }
    return $name;
}


# return login group
sub new_users_grplogin {
    local($name, $defaultgroup, $new_users_ok) = @_;
    local($group_login, $group);

    $group = $name;
    $group = $defaultgroup if $defaultgroup ne $group_uniq;

    if ($new_users_ok) {
	# clean up backup
	foreach $e (keys %groupmembers_bak) { delete $groupmembers_bak{$e}; }
    } else {
	# restore old groupmembers, user was not accept
	foreach $e (keys %groupmembers_bak) {
	    $groupmembers{$e} = $groupmembers_bak{$e};
	}
    }

    while(1) {
	$group_login = &confirm_list("Login group", 1, $group,
				     ($name, $group));
	last if $group_login eq $group;
	last if $group_login eq $name;
	last if defined $groupname{$group_login};
	if ($group_login eq $group_uniq) {
	    $group_login = $name; last;
	}

	if (defined $gid{$group_login}) {
	    # convert numeric groupname (gid) to groupname
	    $group_login = $gid{$group_login};
	    last;
	}
	warn "Group does not exist!\a\n";
    }

    #if (defined($groupname{$group_login})) {
    #	&add_group($groupname{$group_login}, $name);
    #}

    return ($group_login, $group_uniq) if $group_login eq $name;
    return ($group_login, $group_login);
}

# return login group
sub new_users_grplogin_batch {
    local($name, $defaultgroup) = @_;
    local($group_login, $group);

    $group_login = $name;
    $group_login = $defaultgroup if $defaultgroup ne $group_uniq;

    if (defined $gid{$group_login}) {
	# convert numeric groupname (gid) to groupname
	$group_login = $gid{$group_login};
    }

    # if (defined($groupname{$group_login})) {
    #	&add_group($groupname{$group_login}, $name);
    # }

    return $group_login
	if defined($groupname{$group_login}) || $group_login eq $name;
    warn "Group ``$group_login'' does not exist\a\n";
    return 0;
}

# return other groups (string)
sub new_users_groups {
    local($name, $other_groups) = @_;
    local($string) =
	"Login group is ``$group_login''. Invite $name into other groups:";
    local($e, $flag);
    local($new_groups,$groups);

    $other_groups = "no" unless $other_groups;

    while(1) {
	$groups = &confirm_list($string, 1, $other_groups,
				("no", $other_groups, "guest"));
	# no other groups
	return "" if $groups eq "no";

	($flag, $new_groups) = &new_users_groups_valid($groups);
	last unless $flag;
    }
    $new_groups =~ s/\s*$//;
    return $new_groups;
}

sub new_users_groups_valid {
    local($groups) = @_;
    local($e, $new_groups);
    local($flag) = 0;

    foreach $e (split(/[,\s]+/, $groups)) {
	# convert numbers to groupname
	if ($e =~ /^[0-9]+$/ && $gid{$e}) {
	    $e = $gid{$e};
	}
	if (defined($groupname{$e})) {
	    if ($e eq $group_login) {
		# do not add user to a group if this group
		# is also the login group.
	    } elsif (&add_group($groupname{$e}, $name)) {
		$new_groups .= "$e ";
	    } else {
		warn "$name is already member of group ``$e''\n";
	    }
	} else {
	    warn "Group ``$e'' does not exist\a\n"; $flag++;
	}
    }
    return ($flag, $new_groups);
}

# your last chance
sub new_users_ok {

    print <<EOF;

Name:	     $name
Password:    ****
Fullname:    $fullname
Uid:	     $u_id
Gid:	     $g_id ($group_login)
Groups:	     $group_login $new_groups
Login Class: $log_cl
HOME:	     $home/$name
Shell:	     $sh
EOF

    return &confirm_yn("OK?", "yes");
}

# make password database
sub new_users_pwdmkdb {
    local($last) = @_;
    local($user);

    $user = (split(/:/, $last))[0];
    system(@pwd_mkdb, "-u", $user, $etc_passwd);
    if ($?) {
	warn "$last\n";
	warn "``pwd_mkdb'' failed\n";
	exit($? >> 8);
    }
}

# update group database
sub new_users_group_update {
    local($e, $n, $a, @a);

    # Add *new* group
    if (!defined($groupname{$group_login}) && !defined($gid{$g_id})) {
	push(@group_backup, "$group_login:*:$g_id:");
	$groupname{$group_login} = $g_id;
	$gid{$g_id} = $group_login;
	# $groupmembers{$g_id} = $group_login;
    }

    if ($new_groups || defined($groupname{$group_login}) ||
	defined($gid{$groupname{$group_login}}) &&
		$gid{$groupname{$group_login}} ne "+") {
	# new user is member of some groups
	# new login group is already in name space
	rename($group, "$group.bak");
	#warn "$group_login $groupname{$group_login} $groupmembers{$groupname{$group_login}}\n";
	foreach (@group_backup) {
            ($n, $e) = (split(/:/, $_))[0,2];
	    # special handling of YP entries
	    if (substr($n, 0, 1) eq "+") {
		# remember and skip the empty group
		if (length($n) == 1) {
			$a = $_;
			next;
		}
		# pass other groups
		push(@a, $_);
	    }
	    # group membership might have changed
	    else {
		push(@a, "$gid{$e}:*:$e:$groupmembers{$e}");
	    }
	}
	# append empty YP group
	if ($a) {
	    push(@a, $a);
	}
	&append_file($group, @a);
    } else {
	&append_file($group, "$group_login:*:$g_id:");
    }

}

sub new_users_passwd_update {
    # update passwd/group variables
    push(@passwd_backup, $new_entry);
    $username{$name} = $u_id;
    $uid{$u_id} = $name;
    $pwgid{$g_id} = $name;
}

# send message to new user
sub new_users_sendmessage {
    return 1 if $send_message eq "no";

    return 1 if !&confirm_yn("Send welcome message to ``$name''", "yes");

    @message_buffer = ();
    message_read ($message_file);

    local($e);

    foreach $e (@message_buffer) {
	print eval "\"$e\"";
    }
    print "\n";

    local(@message_buffer_append) = ();
    if (!&confirm_yn("Add anything to the message", "no")) {
	print "Use ``.'' or ^D alone on a line to finish your message.\n";
	push(@message_buffer_append, "\n");
	while($read = <STDIN>) {
	    last if $read eq "\.\n";
	    push(@message_buffer_append, $read);
	}
    }
    local($cc) =
	&confirm_list("Copy message to another user?:",
		      1, "no", ("root", "second_mail_address",
		      "no"));
    $cc = "" if $cc eq "no";

    &sendmessage("$name $cc", (@message_buffer, @message_buffer_append));
}

sub sendmessage {
    local($to, @message) = @_;
    local($e);

    if (!open(M, "| mail -s Welcome $to")) {
	warn "Cannot send mail to: $to!\n";
	return 0;
    } else {
	foreach $e (@message) {
	    print M eval "\"$e\"";
	}
	close M;
	print "Mail sent!\n" if $verbose;
    }
}


sub new_users_password {

    # empty password
    return "" if $defaultpasswd ne "yes";

    local($password);

    while(1) {
	system("stty", "-echo");
	$password = &confirm_list("Enter password", 1, "", "");
	system("stty", "echo");
	print "\n";
	if ($password ne "") {
	    system("stty", "-echo");
	    $newpass = &confirm_list("Enter password again", 1, "", "");
	    system("stty", "echo");
	    print "\n";
	    last if $password eq $newpass;
	    print "They didn't match, please try again\n";
	}
	elsif (!&confirm_yn("Disable password logins for the user?", "no")) {
	    last;
	}
    }

    return $password;
}


sub new_users {

    print "\n" if $verbose;
    print "Ok, let's go.\n" .
	  "Don't worry about mistakes. There will be a chance later to " .
	  "correct any input.\n" if $verbose;

    # name: Username
    # fullname: Full name
    # sh: shell
    # u_id: user id
    # g_id: group id
    # group_login: groupname of g_id
    # new_groups: some other groups
    # log_cl: login class
    local($name, $group_login, $fullname, $sh, $u_id, $g_id, $new_groups,
	$log_cl);
    local($groupmembers_bak, $cryptpwd);
    local($new_users_ok) = 1;


    $new_groups = "no" unless $groupname{$new_groups};

    while(1) {
	$name = &new_users_name;
	$fullname = &new_users_fullname($name);
	$sh = &new_users_shell;
	($u_id, $g_id) = &new_users_id($name);
	($group_login, $defaultgroup) =
	    &new_users_grplogin($name, $defaultgroup, $new_users_ok);
	# do not use uniq username and login group
	$g_id = $groupname{$group_login} if (defined($groupname{$group_login}));

	$new_groups = &new_users_groups($name, $new_groups);
	$log_cl = &new_users_login_class;
	$password = &new_users_password;


	if (&new_users_ok) {
	    $new_users_ok = 1;

	    $cryptpwd = "*";	# Locked by default
	    $cryptpwd = encrypt($password, &salt) if ($password ne "");
	    $log_cl = "" if ($log_cl eq "default");

	    # obscure perl bug
	    $new_entry = "$name\:" . "$cryptpwd" .
		"\:$u_id\:$g_id\:$log_cl:0:0:$fullname:$home/$name:$sh";
	    &append_file($etc_passwd, "$new_entry");
	    &new_users_pwdmkdb("$new_entry");
	    &new_users_group_update;
	    &new_users_passwd_update;  print "Added user ``$name''\n";
	    &adduser_log("$name:*:$u_id:$g_id($group_login):$fullname");
	    &home_create($name, $group_login);
	    &new_users_sendmessage;
	} else {
	    $new_users_ok = 0;
	}
	if (!&confirm_yn("Add another user?", "yes")) {
	    print "Goodbye!\n" if $verbose;
	    last;
	}
	print "\n" if !$verbose;
    }
}

sub batch {
    local($name, $groups, $fullname, $password) = @_;
    local($sh);

    $defaultshell = &shell_default_valid($defaultshell);
    return 0 unless $home = &home_partition_valid($home);
    return 0 if $dotdir ne &dotdir_default_valid($dotdir);
    $message_file = &choosetxt_yn_default($send_message, $message_file);
    $send_message = &message_default;

    return 0 if $name ne &new_users_name_valid($name);
    $sh = $shell{$defaultshell};
    ($u_id, $g_id) = &next_id($name);
    $group_login = &new_users_grplogin_batch($name, $defaultgroup);
    return 0 unless $group_login;
    $g_id = $groupname{$group_login} if (defined($groupname{$group_login}));
    ($flag, $new_groups) = &new_users_groups_valid($groups);
    return 0 if $flag;
    $log_cl = ($defaultclass eq "default") ? "" : $defaultclass;

    $cryptpwd = "*";	# Locked by default
    if ($password ne "" && $password ne "*") {
	if($unencrypted)	{ $cryptpwd = encrypt($password, &salt) }
	else			{ $cryptpwd = $password }
    }
    # obscure perl bug
    $new_entry = "$name\:" . "$cryptpwd" .
	"\:$u_id\:$g_id\:$log_cl:0:0:$fullname:$home/$name:$sh";
    &append_file($etc_passwd, "$new_entry");
    &new_users_pwdmkdb("$new_entry");
    &new_users_group_update;
    &new_users_passwd_update;  print "Added user ``$name''\n";
    &sendmessage($name, @message_buffer) if $send_message ne "no";
    &adduser_log("$name:*:$u_id:$g_id($group_login):$fullname");
    &home_create($name, $group_login);
}

# ask for password usage
sub password_default {
    local($p) = $defaultpasswd;
    if ($verbose) {
	$p = &confirm_yn("Prompt for passwords by default", $defaultpasswd);
	$changes++ unless $p;
    }
    return "yes" if (($defaultpasswd eq "yes" && $p) ||
		     ($defaultpasswd eq "no" && !$p));
    return "no";    # otherwise
}

# get default encryption method
sub encryption_default {
    local($m) = "";
    if ($verbose) {
	while (&encryption_check($m) == 0) {
            $m = &confirm_list("Default encryption method for passwords:", 1,
                              $encryption_methods[0], @encryption_methods);
	}
    }
    return($m);
}

sub class_default {
    local($c) = $defaultclass;

    if ($verbose) {
	$c = &confirm_list("Default login class:", 0,
		$defaultclass, @login_classes);
	$changes++ if $c ne $defaultclass;
    }
    return($c);
}

# Confirm that we have a valid encryption method
sub encryption_check {
    local($m) = $_[0];

    foreach $i (@encryption_methods) {
        if ($m eq $i) { return 1; }
    }

    if ($m =~ /^blowfish,(\d+)$/) { return 1; }
    return 0;
}

# misc
sub check_root {
    die "You are not root!\n" if $<;
}

sub usage {
    warn <<USAGE;
usage: adduser
    [-batch username [group[,group]...] [fullname] [password]]
    [-check_only]
    [-config_create]
    [-dotdir dotdir]
    [-e|-encryption method]
    [-group login_group]
    [-class login_class]
    [-h|-help]
    [-home home]
    [-message message_file]
    [-noconfig]
    [-shell shell]
    [-s|-silent|-q|-quiet]
    [-uid_start uid_start]
    [-uid_end uid_end]
    [-unencrypted]
    [-v|-verbose]

home=$home shell=$defaultshell dotdir=$dotdir login_group=$defaultgroup
login_class=$defaultclass uid_start=$uid_start uid_end=$uid_end 
send_message=$send_message message_file=$message_file
USAGE
    exit 1;
}

# uniq(1)
sub uniq {
    local(@list) = @_;
    local($e, $last = "", @array);

    foreach $e (sort @list) {
	push(@array, $e) unless $e eq $last;
	$last = $e;
    }
    return @array;
}

# Generate an appropriate argument to encrypt()
# That may be a DES salt or a blowfish rotation count
sub salt {
    local($salt);		# initialization
    if ($encryptionmethod eq "auto") {
        $salt = "";
    } elsif ($encryptionmethod =~ /^blowfish/ ) {
        ($encryptionmethod, $salt) = split(/\,/, $encryptionmethod);
	$salt = 7 unless $salt;		# default rounds if unspecified
    } else {
        warn "$encryptionmethod encryption method invalid\n" if ($verbose > 0);
	warn "Falling back to blowfish,7...\n" if ($verbose > 0);
	$encryptionmethod = "blowfish";
	$salt = 7;
    }

    warn "Salt is: $salt\n" if $verbose > 1;

    return $salt;
}

# Encrypt a password using the selected method
sub encrypt {
    local($pass, $salt) = ($_[0], $_[1]);
    local(@args, $crypt);

    if ($encryptionmethod eq "blowfish") {
        @args = ("-b", $salt);
    } elsif ($encryptionmethod eq "auto") {
        @args = ("-c", $log_cl);
    }

    open2(\*ENCRD, \*ENCWR, "/usr/bin/encrypt", @args);
    print ENCWR "$pass\n";
    close ENCWR;
    $crypt = <ENCRD>;
    close ENCRD;
    chomp $crypt;
    die "encrypt failed" if (wait == -1 || $? != 0);
    return($crypt);
}

# hints
sub hints {
    if ($verbose) {
	print "Use option ``-silent'' if you don't want to see " .
	      "all warnings and questions.\n\n";
    }
}

#
sub parse_arguments {
    local(@argv) = @_;

    while ($_ = $argv[0], /^-/) {
	shift @argv;
	last if /^--$/;
	if    (/^--?(v|verbose)$/)	{ $verbose = 1 }
	elsif (/^--?(s|silent|q|quiet)$/)  { $verbose = 0 }
	elsif (/^--?(debug)$/)	    { $verbose = 2 }
	elsif (/^--?(h|help|\?)$/)	{ &usage }
	elsif (/^--?(home)$/)	 { $home = $argv[0]; shift @argv }
	elsif (/^--?(shell)$/)	 { $defaultshell = $argv[0]; shift @argv }
	elsif (/^--?(class)$/)	 { $defaultclass = $argv[0]; shift @argv }
	elsif (/^--?(dotdir)$/)	 { $dotdir = $argv[0]; shift @argv }
	elsif (/^--?(uid_start)$/)	 { $uid_start = $argv[0]; shift @argv }
	elsif (/^--?(uid_end)$/)	 { $uid_end = $argv[0]; shift @argv }
	elsif (/^--?(group)$/)	 { $defaultgroup = $argv[0]; shift @argv }
	elsif (/^--?(check_only)$/) { $check_only = 1 }
	elsif (/^--?(message)$/) {
	    $send_message = $argv[0]; shift @argv;
	    $message_file = &choosetxt_yn_default($send_message, $message_file);
	}
	elsif (/^--?(unencrypted)$/)	{ $unencrypted = 1 }
	elsif (/^--?(batch)$/)	 {
	    @batch = splice(@argv, 0, 4); $verbose = 0;
	    die "batch: too few arguments\n" if $#batch < 0;
	}
	# see &config_read
	elsif (/^--?(config_create)$/)	{ &hints; &create_conf; exit(0); }
	elsif (/^--?(noconfig)$/)	{ $config_read = 0; }
	elsif (/^--?(e|encryption)$/) {
	    $encryptionmethod = $argv[0];
	    shift @argv;
	}
	else			    { &usage }
    }
    #&usage if $#argv < 0;
}

sub basename {
    local($name) = @_;
    $name =~ s|/+$||;
    $name =~ s|.*/+||;
    return $name;
}

sub dirname {
    local($name) = @_;
    $name = &stripdir($name);
    $name =~ s|/+[^/]+$||;
    $name = "/" unless $name;	# dirname of / is /
    return $name;
}

# return 1 if $file is a readable file or link
sub filetest {
    local($file, $verbose) = @_;

    if (-e $file) {
	if (-f $file || -l $file) {
	    return 1 if -r _;
	    warn "$file unreadable\n" if $verbose;
	} else {
	    warn "$file is not a plain file or link\n" if $verbose;
	}
    }
    return 0;
}

# create or recreate configuration file prompting for values
sub create_conf {
    $create_conf = 1;

    &shells_read;			# Pull in /etc/shells info
    &shells_add;			# maybe add some new shells
    $defaultshell = &shell_default;	# enter default shell
    &login_conf_read;			# read /etc/login.conf
    $defaultclass = &class_default;	# default login.conf class
    $home = &home_partition($home);	# find HOME partition
    $dotdir = &dotdir_default;		# check $dotdir
    $send_message = &message_default;   # send message to new user
    $defaultpasswd = &password_default; # maybe use password
    $defaultencryption = &encryption_default;	# Encryption method

    &config_write(1);
}

# log for new user in /var/log/adduser
sub adduser_log {
    local($string) = @_;
    local($e);

    return 1 if $logfile eq "no";

    local($sec, $min, $hour, $mday, $mon, $year) = localtime;
    $year += 1900;
    $mon++;

    foreach $e ('sec', 'min', 'hour', 'mday', 'mon') {
	# '7' -> '07'
	eval "\$$e = 0 . \$$e" if (eval "\$$e" < 10);
    }

    &append_file($logfile, "$year/$mon/$mday $hour:$min:$sec $string");
}

# create HOME directory, copy dotfiles from $dotdir to $HOME
sub home_create {
    local($name, $group) = @_;
    local($homedir) = "$home/$name";

    if (-e "$homedir") {
	warn "HOME Directory ``$homedir'' already exists\a\n";
	return 0;
    }

    if ($dotdir eq 'no') {
	if (!mkdir("$homedir", 0755)) {
	    warn "mkdir $homedir: $!\n"; return 0;
	}
	system 'chown', "$name:$group", $homedir;
	return !$?;
    }

    # copy files from  $dotdir to $homedir
    # rename 'dot.foo' files to '.foo'
    print "Copy files from $dotdir to $homedir\n" if $verbose;
    system("cp", "-R", $dotdir, $homedir);
    system("chmod", "-R", "u+wrX,go-w", $homedir);
    system("chown", "-R", "$name:$group", $homedir);

    # security
    opendir(D, $homedir);
    foreach $file (readdir(D)) {
	if ($file =~ /^dot\./ && -f "$homedir/$file") {
	    $file =~ s/^dot\././;
	    rename("$homedir/dot$file", "$homedir/$file");
	}
	chmod(0600, "$homedir/$file")
	    if ($file =~ /^\.(rhosts|Xauthority|kermrc|netrc)$/);
	chmod(0700, "$homedir/$file")
	    if ($file =~ /^(Mail|prv|\.(iscreen|term))$/);
    }
    closedir D;
    return 1;
}

# makes a directory hierarchy
sub mkdir_home {
    local($dir) = @_;
    $dir = &stripdir($dir);
    local($user_partition) = "/usr";
    local($dirname) = &dirname($dir);


    -e $dirname || &mkdirhier($dirname);

    if (((stat($dirname))[0]) == ((stat("/"))[0])){
	# home partition is on root partition
	# create home partition on $user_partition and make
	# a symlink from $dir to $user_partition/`basename $dir`
	# For instance: /home -> /usr/home

	local($basename) = &basename($dir);
	local($d) = "$user_partition/$basename";


	if (-d $d) {
	    warn "Oops, $d already exists\n" if $verbose;
	} else {
	    print "Create $d\n" if $verbose;
	    if (!mkdir("$d", 0755)) {
		warn "$d: $!\a\n"; return 0;
	    }
	}

	unlink($dir);		# symlink to nonexist file
	print "Create symlink: $dir -> $d\n" if $verbose;
	if (!symlink("$d", $dir)) {
	    warn "Symlink $d: $!\a\n"; return 0;
	}
    } else {
	print "Create $dir\n" if $verbose;
	if (!mkdir("$dir", 0755)) {
	    warn "Directory ``$dir'': $!\a\n"; return 0;
	}
    }
    return 1;
}

sub mkdirhier {
    local($dir) = @_;
    local($d,$p);

    $dir = &stripdir($dir);

    foreach $d (split('/', $dir)) {
	$dir = "$p/$d";
	$dir =~ s|^//|/|;
	if (! -e "$dir") {
	    print "Create $dir\n" if $verbose;
	    if (!mkdir("$dir", 0755)) {
		warn "$dir: $!\n"; return 0;
	    }
	}
	$p .= "/$d";
    }
    return 1;
}

# stript unused '/'
# e.g.: //usr///home// -> /usr/home
sub stripdir {
    local($dir) = @_;

    $dir =~ s|/+|/|g;		# delete double '/'
    $dir =~ s|/$||;		# delete '/' at end
    return $dir if $dir ne "";
    return '/';
}

# Read one of the elements from @list. $confirm is the default.
# If !$allow then accept only elements from @list.
sub confirm_list {
    local($message, $allow, $confirm, @list) = @_;
    local($read, $c, $print);

    $print = "$message" if $message;
    $print .= " " unless $message =~ /\n$/ || $#list == 0;

    $print .= join($", &uniq(@list)); #"
    $print .= " " unless $message =~ /\n$/ && $#list == 0;
    print "$print";
    print "\n" if (length($print) + length($confirm)) > 60;
    print "[$confirm]: ";

    chop($read = <STDIN>);
    $read =~ s/^\s*//;
    $read =~ s/\s*$//;
    return $confirm if $read eq "";
    return "$read" if $allow;

    foreach $c (@list) {
	return $read if $c eq $read;
    }
    warn "$read: is not allowed!\a\n";
    return &confirm_list($message, $allow, $confirm, @list);
}

# YES, NO, DEFAULT or userstring
# 1. return "" if "no" or no string is provided by the user.
# 2. return the $default parameter if "yes" or "default" provided.
# otherwise return user provided string.
sub confirm_yn_default {
    local($message, $confirm, $default) = @_;

    print "$message [$confirm]: ";
    chop($read = <STDIN>);
    $read =~ s/^\s*//;
    $read =~ s/\s*$//;
    return "" unless $read;

    return choosetxt_yn_default($read, $default);
}

sub choosetxt_yn_default {
    local($read, $default) = @_;

    if ($read =~ "$no") {
	return "";
    }
    if ($read eq "default") {
	return $default;
    }
    if ($read =~ "$yes") {
	if ($verbose == 1) {
	    return $read;
	}
	return $default;
    }
    return $read;
}

# YES or NO question
# return 1 if &confirm("message", "yes") and answer is yes
#	or if &confirm("message", "no") and answer is no
# otherwise return 0
sub confirm_yn {
    local($message, $confirm) = @_;
    local($read, $c);

    if ($confirm && ($confirm =~ "$yes" || $confirm == 1)) {
	$confirm = "y";
    } else {
	$confirm = "n";
    }
    print "$message (y/n) [$confirm]: ";
    chop($read = <STDIN>);
    $read =~ s/^\s*//;
    $read =~ s/\s*$//;
    return 1 unless $read;

    if (($confirm eq "y" && $read =~ "$yes") ||
	($confirm eq "n" && $read =~ "$no")) {
	return 1;
    }

    if ($read !~ "$yes" && $read !~ "$no") {
	warn "Wrong value. Enter again!\a\n";
	return &confirm_yn($message, $confirm);
    }
    return 0;
}

# test if $dotdir exist
# return "no" if $dotdir not exist or dotfiles should not copied
sub dotdir_default {
    local($dir) = $dotdir;

    return &dotdir_default_valid($dir) unless $verbose;
    while($verbose) {
	$dir = &confirm_list("Copy dotfiles from:", 1,
	    $dir, ("no", $dotdir_bak, $dir));
	last if $dir eq &dotdir_default_valid($dir);
    }
    warn "Do not copy dotfiles.\n" if $verbose && $dir eq "no";

    $changes++ if $dir ne $dotdir;
    return $dir;
}

sub dotdir_default_valid {
    local($dir) = @_;

    return $dir if (-e $dir && -r _ && (-d _ || -l $dir) && $dir =~ "^/");
    return $dir if $dir eq "no";
    warn "Dotdir ``$dir'' is not a directory\a\n";
    return "no";
}

# ask for messages to new users
sub message_default {
    local($tmp_message_file) = $message_file;

    while($verbose) {
	$send_message = "no";

	$message_file = &confirm_yn_default(
			    "Send welcome message?: /path/file default no",
				"no", $tmp_message_file);
	if ($message_file eq "") {
	    $message_file = $tmp_message_file;
	    last;
	}
	if ($message_file =~ $yes) {
	    $message_file = &confirm_yn_default(
		 	     "Really? Type the filepath, 'default' or 'no'",
			     "no", $tmp_message_file);
	    if ($message_file eq "") {
	        $message_file = $tmp_message_file;
	        last;
	    }
	}

	# try and create the message file
	if (&filetest($message_file, 0)) {
	    if (&confirm_yn("File ``$message_file'' exists. Overwrite?:",
			    "no")) {
	        print "Retry: choose a different location\n";
	        next;
	    }
	    if (&message_create($message_file)) {
		print "Message file ``$message_file'' overwritten\n"
		    if $verbose;
	    }
	} else {
	    if (&message_create($message_file)) {
		print "Message file ``$message_file'' created\n" if $verbose;
	    }
	}

	if (&filetest($message_file, 0)) {
	    $send_message = "yes";
	    last;
	}
	last if !&confirm_yn("Unable to create ``$message_file'', try again?",
			     "yes");
    }

    if ($send_message eq "no" || !&filetest($message_file, 0)) {
	warn "Do not send message(s)\n" if $verbose;
	$send_message = "no";
    } else {
	&message_read($message_file);
    }

    $changes++ if $tmp_message_file ne $message_file && $verbose;
    return $send_message;
}

# create message file
sub message_create {
    local($file) = @_;

    rename($file, "$file.bak");
    if (!open(M, "> $file")) {
	warn "Messagefile ``$file'': $!\n"; return 0;
    }
    print M <<EOF;
#
# Message file for adduser(8)
#   comment: ``#''
#   default variables: \$name, \$fullname, \$password
#   other variables:  see /etc/adduser.conf after
#		     line  ``$do_not_delete''
#

\$fullname,

your account ``\$name'' was created.
Have fun!

See also chpass(1), finger(1), passwd(1)
EOF
    close M;
    return 1;
}

# read message file into buffer
sub message_read {
    local($file) = @_;
    @message_buffer = ();

    if (!open(R, "$file")) {
	warn "File ``$file'':$!\n"; return 0;
    }
    while(<R>) {
	push(@message_buffer, $_) unless /^\s*#/;
    }
    close R;
}

# write @list to $file with file-locking
sub append_file {
    local($file,@list) = @_;
    local($e);

    open(F, ">> $file") || die "$file: $!\n";
    print "Lock $file.\n" if $verbose > 1;
    while(!flock(F, LOCK_EX | LOCK_NB)) {
	warn "Cannot lock file: $file\a\n";
	die "Sorry, gave up\n"
	    unless &confirm_yn("Try again?", "yes");
    }
    print F join("\n", @list) . "\n";
    print "Unlock $file.\n" if $verbose > 1;
    flock(F, LOCK_UN);
    close F;
}

# return free uid+gid
# uid == gid if possible
sub next_id {
    local($group) = @_;

    $uid_start = 1000 if ($uid_start <= 0 || $uid_start >= $uid_end);
    # looking for next free uid
    while($uid{$uid_start}) {
	$uid_start++;
	$uid_start = 1000 if $uid_start >= $uid_end;
	print "$uid_start\n" if $verbose > 1;
    }

    local($gid_start) = $uid_start;
    # group for user (username==groupname) already exist
    if ($groupname{$group}) {
	$gid_start = $groupname{$group};
    }
    # gid is in use, looking for another gid.
    # Note: uid and gid are not equal
    elsif ($gid{$uid_start}) {
	while($gid{$gid_start} || $uid{$gid_start}) {
	    $gid_start--;
	    $gid_start = $uid_end if $gid_start < 100;
	}
    }
    return ($uid_start, $gid_start);
}

# read config file - typically /etc/adduser.conf
sub config_read {
    local($opt) = join " ", @_;
    local($user_flag) = 0;

    # don't read config file
    return 1 if $opt =~ /-(noconfig|config_create)/ || !$config_read;

    if (!-f $config) {
        warn("Couldn't find $config: creating a new adduser configuration file\n");
        &create_conf;
    }

    if (!open(C, "$config")) {
	warn "$config: $!\n"; return 0;
    }

    while(<C>) {
	# user defined variables
	/^$do_not_delete/ && $user_flag++;
	# found @array or $variable
	if (s/^(\w+\s*=\s*\()/\@$1/ || s/^(\w+\s*=)/\$$1/) {
	    eval $_;
	    #warn "$_";
	}
	next if /^$/;
	# lines with '^##' are not saved
	push(@user_variable_list, $_)
	    if $user_flag && !/^##/ && (s/^[\$\@]// || /^[#\s]/);
    }
    #warn "X @user_variable_list X\n";
    close C;
}


# write config file
sub config_write {
    local($silent) = @_;

    # nothing to do
    return 1 unless ($changes || ! -e $config || !$config_read || $silent);

    if (!$silent) {
	if (-e $config) {
	    return 1 if &confirm_yn("\nWrite your changes to $config?", "no");
	} else {
	    return 1 unless
		&confirm_yn("\nWrite your configuration to $config?", "yes");
	}
    }

    rename($config, "$config.bak");
    open(C, "> $config") || die "$config: $!\n";

    # prepare some variables
    $send_message = "no" unless $send_message;
    $defaultpasswd = "no" unless $defaultpasswd;
    local($shpref) = "'" . join("', '", @shellpref) . "'";
    local($shpath) = "'" . join("', '", @path) . "'";
    local($user_var) = join('', @user_variable_list);
    local($def_lc) = "'" . join("', '", @login_classes) . "'";

    print C <<EOF;
#
# $config - automatic generated by adduser(8)
#
# Note: adduser reads *and* writes this file.
#	You may change values, but don't add new things before the
#	line ``$do_not_delete''
#	Also, unquoted strings may cause warnings
#

# verbose = [0-2]
verbose = $verbose

# Get new password for new users
# defaultpasswd =  yes | no
defaultpasswd = "$defaultpasswd"

# Default encryption method for user passwords
# Methods are all those listed in login.conf(5)
encryptionmethod = "$defaultencryption"

# copy dotfiles from this dir ("/etc/skel" or "no")
dotdir = "$dotdir"

# send message to user? ("yes" or "no")
send_message = "$send_message"

# send this file to new user ("/etc/adduser.message")
message_file = "$message_file"

# config file for adduser ("/etc/adduser.conf")
config = "$config"

# logfile ("/var/log/adduser" or "no")
logfile = "$logfile"

# default HOME directory ("/home")
home = "$home"

# List of directories where shells located
# path = ('/bin', '/usr/bin', '/usr/local/bin')
path = ($shpath)

# common shell list, first element has higher priority
# shellpref = ('bash', 'tcsh', 'ksh', 'csh', 'sh')
shellpref = ($shpref)

# defaultshell if not empty ("bash")
defaultshell = "$defaultshell"

# defaultgroup ('USER' for same as username or any other valid group)
defaultgroup = "$defaultgroup"

# new users get this uid
uid_start = $uid_start
uid_end = $uid_end

# default login.conf(5) login class
defaultclass = "$defaultclass"

# login classes available from login.conf(5)
# login_classes = ('default', 'daemon', 'staff')
login_classes = ($def_lc)

$do_not_delete
## your own variables, see /etc/adduser.message
EOF
    print C "$user_var\n" if ($user_var ne '');
    print C "\n## end\n";
    close C;
}

# check for sane variables
sub variable_check {
	# Check uid_start & uid_end
	warn "WARNING: uid_start < 1000!\n" if($uid_start < 1000);
	die "ERROR: uid_start >= uid_end!\n" if($uid_start >= $uid_end);
	# unencrypted really only usable in batch mode
	warn "WARNING: unencrypted only effective in batch mode\n"
	    if($#batch < 0 && $unencrypted);
}

sub cleanup {
    local($sig) = @_;

    print STDERR "Caught signal SIG$sig -- cleaning up.\n";
    system("stty", "echo");
    exit(0);
}

END {
    if (-e $etc_ptmp && defined(fileno(PTMP))) {
	    close PTMP;
	    unlink($etc_ptmp) || warn "Error: unable to remove $etc_ptmp: $!\nPlease verify that $etc_ptmp no longer exists!\n";
    }
}
