#	$NetBSD: mkconf,v 1.1.1.1 1997/07/24 21:20:12 christos Exp $
# $FreeBSD$
# mkconf
# Generate local configuration parameters for amd
#

if [ -e $1 ]; then
	eval `LC_ALL=C egrep '^[A-Z]+=' $1 | grep -v COPYRIGHT`
	OS=`echo ${TYPE} | LC_ALL=C tr 'A-Z' 'a-z'`
	echo '/* Define name and version of host machine (eg. solaris2.5.1) */'
	echo "#define HOST_OS \"${OS}${REVISION}\""
	echo '/* Define only name of host machine OS (eg. solaris2) */'
	echo "#define HOST_OS_NAME \"${OS}${REVISION}\"" \
		| sed -e 's/\.[-._0-9]*//'
	echo '/* Define only version of host machine (eg. 2.5.1) */'
	echo "#define HOST_OS_VERSION \"${REVISION}\""
else
cat << __NO_newvers_sh

/* Define name and version of host machine (eg. solaris2.5.1) */
#define HOST_OS "`uname -s | LC_ALL=C tr 'A-Z' 'a-z'``uname -r`"

/* Define only name of host machine OS (eg. solaris2) */
#define HOST_OS_NAME "`uname -s | LC_ALL=C tr 'A-Z' 'a-z'``uname -r | sed -e 's/\..*$//'`"

/* Define only version of host machine (eg. 2.5.1) */
#define HOST_OS_VERSION "`uname -r | sed -e 's/[-([:alpha:]].*//'`"

__NO_newvers_sh
fi

cat << __EOF

__EOF
