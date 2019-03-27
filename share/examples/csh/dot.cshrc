# Here are some example (t)csh options and configurations that you may find interesting
#
# $FreeBSD$
#

# Sets SSH_AUTH_SOCK to the user's ssh-agent socket path if running
#
# This has a couple caveats, the most notable being that if a user
# has multiple ssh-agent(1) processes running, this will very likely
# set SSH_AUTH_SOCK to point to the wrong file/domain socket.
if (${?SSH_AUTH_SOCK} != "1") then
	setenv SSH_AUTH_SOCK `sockstat -u | awk '/^${USER}.+ ssh-agent/ { print $6 }'`
endif

# Change only root's prompt
if (`id -g` == 0) then
	set prompt="root@%m# "
endif

# This maps the "Delete" key to do the right thing
# Pressing CTRL-v followed by the key of interest will print the shell's
# mapping for the key
bindkey "^[[3~" delete-char-or-list-or-eof

# Make the Ins key work
bindkey "\e[2~" overwrite-mode 

# Some common completions
complete cd		'p/1/d/'
complete chown          'p/1/u/'
complete dd		'c/[io]f=/f/ n/*/"(if of ibs obs bs skip seek count)"/='
complete find 	'n/-fstype/"(nfs 4.2)"/' 'n/-name/f/' \
      	  	'n/-type/(c b d f p l s)/' \
      		'n/-user/u/ n/-group/g/' \
      		'n/-exec/c/' 'n/-ok/c/' \
      		'n/-cpio/f/' \
      		'n/-ncpio/f/' \
      		'n/-newer/f/' \
      	  	'c/-/(fstype name perm prune type user nouser group nogroup size inum atime mtime ctime exec \
      		  ok print ls cpio ncpio newer xdev depth daystart follow maxdepth mindepth noleaf version \
      		  anewer cnewer amin cmin mmin true false uid gid ilname iname ipath iregex links lname empty path \
      		  regex used xtype fprint fprint0 fprintf print0 printf not a and o or)/' \
      		'n/*/d/'
complete fg		'c/%/j/'
complete gpart	'p/1/(add backup bootcode commit create delete destroy modify recover resize restore set show undo unset)/' \
			'n/add/x:-t type [-a alignment] [-b start] [-s size] [-i index] [-l label] -f flags geom/' \
      		'n/backup/x:geom/' \
      		'n/bootcode/x:[-b bootcode] [-p partcode -i index] [-f flags] geom/' \
      		'n/commit/x:geom/' \
      		'n/create/x:-s scheme [-n entries] [-f flags] provider/' \
      		'n/delete/x:-i index [-f flags] geom/' \
      		'n/destroy/x:[-F] [-f flags] geom/' \
      		'n/modify/x:-i index [-l label] [-t type] [-f flags] geom/' \
      		'n/recover/x:[-f flags] geom/' \
      		'n/resize/x:-i index [-a alignment] [-s size] [-f flags] geom/' \
      		'n/restore/x:[-lF] [-f flags] provider [...]/' \
      		'n/set/x:-a attrib -i index [-f flags] geom/' \
      		'n/show/x:[-l | -r] [-p] [geom ...]/' \
      		'n/undo/x:geom/' \
      		'n/unset/x:-a attrib -i index [-f flags] geom/'
complete grep		'c/-*A/x:<#_lines_after>/' \
      		'c/-*B/x:<#_lines_before>/' \
      		'c/--/(extended-regexp fixed-regexp basic-regexp regexp file ignore-case word-regexp line-regexp \
      		  no-messages revert-match version help byte-offset line-number with-filename no-filename quiet silent \
      		  text directories recursive files-without-match files-with-matches count before-context after-context \
      		  context binary unix-byte-offsets)/' \
      		'c/-/(A a B b C c d E e F f G H h i L l n q r s U u V v w x)/' \
      		'p/1/x:<limited_regular_expression>/ N/-*e/f/' \
      		'n/-*e/x:<limited_regular_expression>/' \
      		'n/-*f/f/' \
      		'n/*/f/'
complete ifconfig	'p@1@`ifconfig -l`@' \
      		'n/*/(range phase link netmask mtu vlandev vlan metric mediaopt down delete broadcast arp debug)/' \
      		'c/%/j/' \
      		'n/*/`ps -ax | awk '"'"'{print $1}'"'"'`/'
complete kill		'c/-/S/' 'c/%/j/' 'n/*/`ps -ax | awk '"'"'{print $1}'"'"'`/'
complete killall	'c/-/S/' 'c/%/j/' 'n/*/`ps -ax | awk '"'"'{print $5}'"'"'`/'
complete kldload	'n@*@`ls -1 /boot/modules/ /boot/kernel/ | awk -F/ \$NF\ \~\ \".ko\"\ \{sub\(\/\.ko\/,\"\",\$NF\)\;print\ \$NF\}`@'
complete kldunload	'n@*@`kldstat | awk \{sub\(\/\.ko\/,\"\",\$NF\)\;print\ \$NF\} | grep -v Name`@'
complete make		'p@1@`make -pn | sed -n -E "/^[#_.\/[:blank:]]+/d; /=/d; s/[[:blank:]]*:.*//gp;"`@' \
      			'n@-V@`make -ndv | & grep Global: | sed -E -e "s/^Global://" -e "s/ .*//" -e "/^[[:lower:]]/d" | sort | uniq`@'
complete man		'C/*/c/'
complete netstat	'n@-I@`ifconfig -l`@'
complete pkg_delete     'c/-/(i v D n p d f G x X r)/' 'n@*@`ls /var/db/pkg`@'
complete pkg_info       'c/-/(a b v p q Q c d D f g i I j k K r R m L s o G O x X e E l t V P)/' 'n@*@`\ls -1 /var/db/pkg | sed s%/var/db/pkg/%%`@'
complete ping		'p/1/$hosts/'
complete pkill		'c/-/S/' \
      			'n@*@`ps -axc -o command="" | sort | uniq`@'
complete portmaster	'c/--/(always-fetch check-depends check-port-dbdir clean-distfiles clean-packages delete-build-only \
      		  delete-packages force-config help index index-first index-only list-origins local-packagedir \
      		  no-confirm no-index-fetch no-term-title packages packages-build packages-if-newer packages-local \
      		  packages-only show-work update-if-newer version)/' \
      		'c/-/(a b B C d D e f F g G h H i l L m n o p r R s t u v w x)/' \
      		'n@*@`pkg_info -E \*`@'
complete rsync	"c,*:/,F:/," \
      		"c,*:,F:$HOME," \
      		'c/*@/$hosts/:/'
complete scp	"c,*:/,F:/," \
      		"c,*:,F:$HOME," \
      		'c/*@/$hosts/:/'
complete service  	'c/-/(e l r v)/' 'p/1/`service -l`/' 'n/*/(start stop reload restart status rcvar describe extracommands onestart onestop oneextracommands)/'
complete svn		'C@file:///@`'"${HOME}/etc/tcsh/complete.d/svn"'`@@' \
      		'n@ls@(file:/// svn+ssh:// svn://)@@' \
      		'n@help@(add blame cat checkout cleanup commit copy delete export help import info list ls lock log merge mkdir move propdel \
      		  propedit propget proplist propset resolved revert status switch unlock update)@' 'p@1@(add blame cat checkout cleanup commit \
      		  copy delete export help import info list ls lock log merge mkdir move propdel propedit propget proplist propset resolved \
      		  revert status switch unlock update)@'
complete ssh	'p/1/$hosts/' \
      		'c/-/(l n)/' \
      		'n/-l/u/ N/-l/c/ n/-/c/ p/2/c/ p/*/f/'
complete sysctl 'n/*/`sysctl -Na`/'
complete tmux	'n/*/(attach detach has kill-server kill-session lsc lscm ls lockc locks new refresh rename showmsgs source start suspendc switchc)/'
complete which	'C/*/c/'

if ( -f /etc/printcap ) then
  set printers=(`sed -n -e "/^[^ 	#].*:/s/:.*//p" /etc/printcap`)
  complete lpr	'c/-P/$printers/'
  complete lpq	'c/-P/$printers/'
  complete lprm	'c/-P/$printers/'
endif

# Alternate prompts
set prompt = '#'
set prompt = '%B%m%b%# '
set prompt = '%B%m%b:%c03:%# '
set prompt = '%{\033]0;%n@%m:%/\007%}%B%m%b:%c03:%# '
set prompt = "%n@%m %c04%m%# "
set prompt = "%n@%m:%c04 %# "
set prompt = "[%n@%m]%c04%# "
set ellipsis

# Color ls
alias ll	ls -lAhG
alias ls	ls -G

# Color on many system utilities
setenv CLICOLOR 1

# other autolist options
set		autolist = TAB
