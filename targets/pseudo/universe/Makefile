# $FreeBSD$

# This target is "special".
#
# The actual work is done via another target;
# UNIVERSE_TARGET_RELDIR is used to identify it.
#
# We leverage dirdeps.mk to run a sub-make per
# MACHINE/ARCH to each build ${UNIVERSE_TARGET_RELDIR}
# in meta mode.
#
# We simply override the level at which dirdeps.mk normally does
# its thing, and give it a DIRDEPS list that corresponds to
# ${UNIVERSE_TARGET_RELDIR}.${TARGET_SPEC} for each TARGET_SPEC we
# want.
#
# This allows the computation of dependencies for each ${TARGET_SPEC}
# to happen in parallel - avoiding a very long startup time.
# We can even export each sub-make to a different machine if using a
# cluster.
#
# The above works fine so long as our sub-makes remain independent.
# That is; none of them  attempts to visit any directory for a machine
# other than the TARGET_SPEC we gave it.
# For example; to build some tool for the pseudo machine "host".
# To avoid that, our Makefile.depend lists all the hosttools we might
# need and these will thus be built before we launch the
# ${UNIVERSE_TARGET_RELDIR} sub-makes.
# Further we add a filter to skip any "host" dirdeps during those
# sub-makes.
# 
# 

# this is the RELDIR of the target we build
UNIVERSE_TARGET_RELDIR?= targets/pseudo/the-lot

# the list of machines
TARGET_MACHINE_LIST = i386 amd64

# avoid ../Makefile.inc defining this
all:

.include "../Makefile.inc"

.if ${.MAKE.LEVEL} > 0
# ok our prerequisites have been built
.if !defined(UNIVERSE_GUARD)
# just incase we end up back here, 
# we cannot rely on .MAKE.LEVEL as we normally would
UNIVERSE_GUARD=1
.export UNIVERSE_GUARD

# we want to re-use dirdeps
_DIRDEP_USE_LEVEL = ${.MAKE.LEVEL}

# we want our sub-makes to think they are level 0
# and we don't want failure of one to stop all.
DIRDEP_USE_ENV= \
	MAKELEVEL=0 \
	MAKE_JOB_ERROR_TOKEN=no


_target_specs:= ${TARGET_MACHINE_LIST:@m@${TARGET_ARCHES_$m:U$m:@a@$m,$a@}@:O:u}

DIRDEPS:= ${_target_specs:@t@${UNIVERSE_TARGET_RELDIR}.$t@}

all: dirdeps

.include <dirdeps.mk>
.endif
.endif
