#!/bin/sh
# $FreeBSD$
#
# ypinit.sh - setup a master or slave server.
# (Taken from OpenBSD and modified for FreeBSD.)
#
DOMAINNAME=/bin/domainname
HOSTNAME=/bin/hostname
YPWHICH=/usr/bin/ypwhich
YPXFR=/usr/libexec/ypxfr
YP_DIR=/var/yp
MAKEDBM=/usr/sbin/yp_mkdb
MAPLIST="master.passwd.byname master.passwd.byuid passwd.byname passwd.byuid \
	 group.byname group.bygid hosts.byname hosts.byaddr services.byname \
	 rpc.byname rpc.bynumber networks.byname networks.byaddr netgroup \
	 netgroup.byuser netgroup.byhost netid.byname publickey.byname \
	 bootparams ethers.byname ethers.byaddr eui64.byname eui64.byid \
	 amd.host mail.aliases ypservers protocols.byname protocols.bynumber \
	 netmasks.byaddr"

ERROR_EXISTS="NO"
umask 077

#set -xv

ERROR=USAGE				# assume usage error

if [ $# -eq 1 ]
then
	if [ $1 = "-m" ]		# ypinit -m
	then
		DOMAIN=`${DOMAINNAME}`
		SERVERTYPE=MASTER
		ERROR=
	fi

	if [ $1 = "-u" ]		# ypinit -u
	then
		DOMAIN=`${DOMAINNAME}`
		SERVERTYPE=UPDATE
		ERROR=
	fi
fi

if [ $# -eq 2 ]
then
	if [ $1 = "-m" ]		# ypinit -m domainname
	then
		DOMAIN=${2}
		SERVERTYPE=MASTER
		ERROR=
	fi

	if [ $1 = "-s" ]		# ypinit -s master_server
	then
		DOMAIN=`${DOMAINNAME}`
		SERVERTYPE=SLAVE
		MASTER=${2}
		ERROR=
	fi

	if [ $1 = "-u" ]		# ypinit -u domainname
	then
		DOMAIN=${2}
		SERVERTYPE=UPDATE
		ERROR=
	fi
fi

if [ $# -eq 3 ]
then
	if [ $1 = "-s" ]		# ypinit -s master_server domainname
	then
		DOMAIN=${3}
		SERVERTYPE=SLAVE
		MASTER=${2}
		ERROR=
	fi
fi

if [ "${ERROR}" = "USAGE" ]; then
	cat << \__usage 1>&2
usage: ypinit -m [domainname]
       ypinit -s master_server [domainname]
       ypinit -u [domainname]

The `-m' flag builds a master YP server, and the `-s' flag builds
a slave YP server.  When building a slave YP server, `master_server'
must be an existing, reachable YP server.
The `-u' is for updating the ypservers map on a master server.
__usage

	exit 1
fi

# Check if domainname is set, don't accept an empty domainname
if [ -z "${DOMAIN}" ]; then
	cat << \__no_domain 1>&2
The local host's YP domain name has not been set.  Please set it with
the domainname(1) command or pass the domain as an argument to ypinit(8).
__no_domain

	exit 1
fi

# Check if hostname is set, don't accept an empty hostname
HOST=`${HOSTNAME}`
if [ -z "${HOST}" ]; then
	cat << \__no_hostname 1>&2
The local host's hostname has not been set.  Please set it with the
hostname(1) command.
__no_hostname

	exit 1
fi

# Check if we have contact with master.
# If we can't list the maps on the master, then we fake it with a
# hard-coded list of maps. The FreeBSD ypxfr command will work even
# if ypbind isn't running or if we are bound to ourselves instead of
# the master (the slave should be bound to itself, but since it has
# no maps yet, we can't get a maplist from it).
if [ "${SERVERTYPE}" = "SLAVE" ];
then
	COUNT=`${YPWHICH} -d ${DOMAIN} -m 2>/dev/null | grep -i ${MASTER} | wc -l | tr -d " "`
	if [ "$COUNT" = "0" ]
	then
		echo "Can't enumerate maps from ${MASTER}. Please check that it is running." 1>&2
		echo "Note: using hardcoded maplist for map transfers." 1>&2
		YPMAPLIST=${MAPLIST}
	else
		YPMAPLIST=`${YPWHICH} -d ${DOMAIN} -m | cut -d\  -f1`
	fi
	echo "" 1>&2
fi

# Check if user is root
ID=`id -u`
if [ "${ID}" != "0" ]; then
	echo "You have to be the superuser to run this.  Please login as root." 1>&2
	exit 1
fi

# Check if the YP directory exists.

if [ ! -d ${YP_DIR} -o -f ${YP_DIR} ]
then
	echo "The directory ${YP_DIR} doesn't exist.  Restore it from the distribution." 1>&2
	exit 1

fi

echo -n "Server Type: ${SERVERTYPE} Domain: ${DOMAIN}"
if [ "${SERVERTYPE}" = "SLAVE" ]; then
	echo -n " Master: ${MASTER}"
fi
echo ""

if [ "${SERVERTYPE}" != "UPDATE" ];
then
	cat << \__notice1

Creating an YP server will require that you answer a few questions.
Questions will all be asked at the beginning of the procedure.

__notice1

	echo -n "Do you want this procedure to quit on non-fatal errors? [y/n: n]  "
	read DOEXIT

	case ${DOEXIT} in
	y*|Y*)
		ERROR_EXIT="YES"
		;;

	*)	ERROR_EXIT="NO"
		echo ""
		echo "Ok, please remember to go back and redo manually whatever fails."
		echo "If you don't, something might not work. "
		;;
	esac

	if [ -d "${YP_DIR}/${DOMAIN}" ]; then
		echo ""
		echo -n "Can we destroy the existing ${YP_DIR}/${DOMAIN} and its contents? [y/n: n]  "
		read KILL

		ERROR=
		case ${KILL} in
		y*|Y*)
			ERROR="DELETE"
			;;

		*)	ERROR=
			;;
		esac

		if [ "${ERROR}" = "DELETE" ]; then
			if ! rm -rf ${YP_DIR}/${DOMAIN}; then
				echo "Can't clean up old directory ${YP_DIR}/${DOMAIN}." 1>&2
				exit 1
			fi
		else
			echo "OK, please clean it up by hand and start again.  Bye"
			exit 0
		fi
	fi

	if ! mkdir "${YP_DIR}/${DOMAIN}"; then
		echo "Can't make new directory ${YP_DIR}/${DOMAIN}." 1>&2
		exit 1
	fi
fi

if [ "${SERVERTYPE}" = "MASTER" ];
then

	if [ ! -f ${YP_DIR}/Makefile ]
	then
		if [ ! -f ${YP_DIR}/Makefile.dist ]
		then
			echo "Can't find ${YP_DIR}/Makefile.dist. " 1>&2
			exit 1
		fi
		cp ${YP_DIR}/Makefile.dist ${YP_DIR}/Makefile
	fi

fi

if [ "${SERVERTYPE}" = "SLAVE" ];
then

	echo "There will be no further questions. The remainder of the procedure"
	echo "should take a few minutes, to copy the databases from ${MASTER}."

	for MAP in ${YPMAPLIST}
	do
		echo "Transferring ${MAP}..."
		if ! ${YPXFR} -p ${YP_DIR} -h ${MASTER} -c -d ${DOMAIN} ${MAP}; then
			echo "Can't transfer map ${MAP}." 1>&2
			ERROR_EXISTS="YES"
			if [ "${ERROR_EXIT}" = "YES" ]; then
				exit 1
			fi
		fi
	done

	echo ""
	if [ "${ERROR_EXISTS}" = "YES"  ]; then
		echo "${HOST} has been setup as an YP slave server with errors. " 1>&2
		echo "Please remember fix any problem that occurred." 1>&2
	else
		echo "${HOST} has been setup as an YP slave server without any errors. "
	fi

	echo "Don't forget to update map ypservers on ${MASTER}."
	exit 0
fi

LIST_OK="NO"

while [ "${LIST_OK}" = "NO" ];
do
	if [ "${SERVERTYPE}" = "MASTER" ];
	then
		HOST_LIST="${HOST}"
		echo ""
		echo "At this point, we have to construct a list of this domains YP servers."
		echo "${HOST} is already known as master server."
		echo "Please continue to add any slave servers, one per line. When you are"
		echo "done with the list, type a <control D>."
		echo "	master server   :  ${HOST}"
	fi

	if [ "${SERVERTYPE}" = "UPDATE" ];
	then
		HOST_LIST="${HOST}"
		NEW_LIST=""
		MASTER_NAME=""
		SHORT_HOST=`echo ${HOST} | cut -d. -f1`
		if [ -f ${YP_DIR}/${DOMAIN}/ypservers ];
		then
			for srv in `${MAKEDBM} -u ${YP_DIR}/${DOMAIN}/ypservers | grep -v "^YP" | tr "\t" " " | cut -d\  -f1`;
			do
				short_srv=`echo ${srv} | cut -d. -f1`
				if [ "${SHORT_HOST}" != "${short_srv}" ]
				then
					if [ "${NEW_LIST}" = "" ];
					then
						NEW_LIST="${srv}"
					else
						NEW_LIST="${NEW_LIST} ${srv}"
					fi
				fi
			done;
			MASTER_NAME=`${MAKEDBM} -u ${YP_DIR}/${DOMAIN}/ypservers | grep "^YP_MASTER_NAME" | tr "\t" " " | cut -d\  -f2`
		fi
		echo ""
		echo "Update the list of hosts running YP servers in domain ${DOMAIN}."
		echo "Master for this domain is ${MASTER_NAME}."
		echo ""
		echo "First verify old servers, type \\\\ to remove a server."
		echo "Then add new servers, one per line. When done type a <control D>."
		echo ""
		echo "	master server   :  ${HOST}"
		if [ "${NEW_LIST}" != "" ]; then
			for node in $NEW_LIST; do
				echo -n "	verify host     : [${node}] "
				read verify
				if [ "${verify}" != "\\" ]; then
					HOST_LIST="${HOST_LIST} ${node}"
				fi
			done;
		fi
	fi

	echo -n "	next host to add:  "

	while read h
	do
		echo -n "	next host to add:  "
		HOST_LIST="${HOST_LIST} ${h}"
	done

	echo ""
	echo "The current list of NIS servers looks like this:"
	echo ""

	for h in `echo ${HOST_LIST}`;
	do
		echo ${h}
	done

	echo ""
	echo -n "Is this correct?  [y/n: y]  "
	read hlist_ok

	case $hlist_ok in
	n*)	echo "Let's try the whole thing again...";;
	N*)	echo "Let's try the whole thing again...";;
	*)	LIST_OK="YES";;
	esac

done

echo "Building ${YP_DIR}/${DOMAIN}/ypservers..."
rm -f ${YP_DIR}/ypservers
touch -f ${YP_DIR}/ypservers
rm -f ${YP_DIR}/${DOMAIN}/ypservers
for host in ${HOST_LIST};
do
	echo "${host} ${host}" >> ${YP_DIR}/ypservers
	echo "${host} ${host}"
done | ${MAKEDBM} - ${YP_DIR}/${DOMAIN}/ypservers

if [ $? -ne 0 ]; then
	echo "" 1>&2
	echo "Couldn't build yp data base ${YP_DIR}/${DOMAIN}/ypservers." 1>&2
	ERROR_EXISTS="YES"
	if [ "${ERROR_EXIT}" = "YES" ]; then
		exit 1
	fi
fi

if [ "${SERVERTYPE}" = "MASTER" ]; then
	CUR_PWD=`pwd`
	cd ${YP_DIR}
	echo "Running ${YP_DIR}/Makefile..."
	if ! make NOPUSH=True UPDATE_DOMAIN=${DOMAIN} YP_DIR=${YP_DIR}; then
		echo "" 1>&2
		echo "Error running Makefile." 1>&2
		ERROR_EXISTS="YES"
		if [ "${ERROR_EXIT}" = "YES" ]; then
			exit 1
		fi
	fi

	cd ${CUR_PWD}

	echo ""
	if [ "${ERROR_EXISTS}" = "YES" ]; then
		echo "${HOST} has been setup as an YP master server with errors. " 1>&2
		echo "Please remember fix any problem that occurred." 1>&2
	else
		echo "${HOST} has been setup as an YP master server without any errors. "
	fi
fi
