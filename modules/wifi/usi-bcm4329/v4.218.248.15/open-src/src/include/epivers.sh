#! /bin/bash
#
# Create the epivers.h file from epivers.h.in
#
# $Id: epivers.sh,v 13.19 2008/01/04 03:47:32 Exp $

# Check for the in file, if not there we're probably in the wrong directory
if [ ! -f epivers.h.in ]; then
	echo No epivers.h.in found
	exit 1
fi

if [ -f epivers.h ]; then
	# If the out file already exists, increment its build number
	build=`grep EPI_BUILD_NUMBER epivers.h | sed -e "s,.*BUILD_NUMBER[ 	]*,,"`
	build=`expr ${build} + 1`
	echo build=${build}
	sed -e "s,.*_BUILD_NUMBER.*,#define EPI_BUILD_NUMBER	${build}," \
		< epivers.h > epivers.h.new
	mv epivers.h epivers.h.prev
	mv epivers.h.new epivers.h
else
	# Otherwise create a new file.

	# CVS will insert the cvs tag name when this file is checked out.
	# If this is a tagged build, use the tag to supply the numbers
	# Tag should be in the form
	#    <NAME>_REL_<MAJ>_<MINOR>
	# or
	#    <NAME>_REL_<MAJ>_<MINOR>_RC<RCNUM>
	# or
	#    <NAME>_REL_<MAJ>_<MINOR>_RC<RCNUM>_<INCREMENTAL>
	#    
 
	CVSTAG="$Name: ROMTERM_REL_4_218_248_15 $"

	# Remove leading cvs "Name: " and trailing " $"
	CVSTAG=${CVSTAG/#*: /}
	CVSTAG=${CVSTAG/% $/}

	# TAG env var is supplied by calling makefile or build process
	#    
	# If the checkout is from a branch tag, cvs checkout or export does
	# not replace rcs keywords. In such instances TAG env variable can
	# be used (by uncommenting following line). TAG env variable format
	# itself needs to be validated for number of fields before being used.
	# (e.g: HEAD is not a valid tag, which results in all '0' values below)
	#
	# TAG=${TAG:-${CVSTAG}}

	TAG=${CVSTAG/HEAD/}

	# Split the tag into an array on underbar or whitespace boundaries.
	IFS="_	     " tag=(${TAG})
	unset IFS

        tagged=1
	if [ ${#tag[*]} -eq 0 ]; then
	   tag=(`date '+TOT REL %Y %m %d 0 %y'`);
	   tagged=0
	fi

	# Allow environment variable to override values.
	# Missing values default to 0
	#
	maj=${EPI_MAJOR_VERSION:-${tag[2]:-0}}
	min=${EPI_MINOR_VERSION:-${tag[3]:-0}}
	rcnum=${EPI_RC_NUMBER:-${tag[4]:-0}}
	incremental=${EPI_INCREMENTAL_NUMBER:-${tag[5]:-0}}
	build=${EPI_BUILD_NUMBER:-0}

	# Strip 'RC' from front of rcnum if present
	rcnum=${rcnum/#RC/}
	
	# strip leading zero off the number (otherwise they look like octal)
	maj=${maj/#0/}
	min=${min/#0/}
	min_router=${min}
	rcnum=${rcnum/#0/}
	incremental=${incremental/#0/}
	build=${build/#0/}

	# some numbers may now be null.  replace with with zero.
	maj=${maj:-0}
	min=${min:-0}
	rcnum=${rcnum:-0}
	incremental=${incremental:-0}
	build=${build:-0}

	if [ ${tagged} -eq 1 ]; then
	    vernum=`printf "0x%02x%02x%02x%02x" ${maj} ${min} ${rcnum} ${incremental}`
	else 
	    vernum=`printf "0x00%02x%02x%02x" ${tag[7]} ${min} ${rcnum}`
	fi


        # PR17029: increment minor number for tagged router builds
        #         with an even minor revision
	if [ ${tagged} -eq 1 -a `expr \( \( ${min} + 1 \) % 2 \)` -eq 1 ]; then
	   min_router=`expr ${min} + 1`
	fi


	# OK, go do it

	echo "maj=${maj}, min=${min}, rc=${rcnum}, inc=${incremental}, build=${build}"
	echo "Router maj=${maj}, min=${min_router}, rc=${rcnum}, inc=${incremental}, build=${build}"
	
	sed \
		-e "s;@EPI_MAJOR_VERSION@;${maj};" \
		-e "s;@EPI_MINOR_VERSION@;${min};" \
		-e "s;@EPI_RC_NUMBER@;${rcnum};" \
		-e "s;@EPI_INCREMENTAL_NUMBER@;${incremental};" \
		-e "s;@EPI_BUILD_NUMBER@;${build};" \
		-e "s;@EPI_VERSION@;${maj}, ${min}, ${rcnum}, ${incremental};" \
		-e "s;@EPI_VERSION_STR@;${maj}.${min}.${rcnum}.${incremental};" \
                -e "s;@EPI_ROUTER_VERSION_STR@;${maj}.${min_router}.${rcnum}.${incremental};" \
                -e "s;@EPI_VERSION_NUM@;${vernum};" \
		< epivers.h.in > epivers.h

fi
