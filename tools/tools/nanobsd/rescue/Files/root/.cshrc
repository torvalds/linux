# $FreeBSD$
#
#csh .cshrc file

	alias a	alias
	a h	history 25
	a j	jobs -l
	a la	ls -a
	a lf	ls -FA
	a ll	ls -lA
	a lm	'll | less'
	a m	less

set path = (/sbin /bin /usr/sbin /usr/bin /usr/local/sbin /usr/local/bin /usr/X11R6/bin /usr/local/jdk1.6.0/bin /usr/local/jdk1.5.0/bin $HOME/bin)
setenv MANPATH "/usr/share/man:/usr/X11R6/man:/usr/local/man"

setenv	PAGER	less
setenv EDITOR	vi
setenv	BLOCKSIZE	K
setenv	FTP_PASSIVE_MODE YES
#setenv	JAVA_HOME	/usr/local/jdk1.6.0
#setenv	JDK_HOME	/usr/local/jdk1.6.0
#setenv JAVA_VERSION 1.5+
#setenv JAVA_VERSION 1.4
#setenv	LANG		de_DE.UTF-8
setenv	CVS_RSH		ssh

if ($?prompt) then
	# An interactive shell -- set some stuff up
	set prompt = "`hostname -s`# "
	set filec
	set history = 100
	set savehist = 100
	set mail = (/var/mail/$USER)
	set autolist
	set matchbeep=ambiguos
	set autoexpand
	set autocorrect
	set ignoreeof
	set correct=cmd
endif
