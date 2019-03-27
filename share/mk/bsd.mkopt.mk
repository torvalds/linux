#
# $FreeBSD$
#
# Generic mechanism to deal with WITH and WITHOUT options and turn
# them into MK_ options.
#
# For each option FOO in __DEFAULT_YES_OPTIONS, MK_FOO is set to
# "yes", unless WITHOUT_FOO is defined, in which case it is set to
# "no".
#
# For each option FOO in __DEFAULT_NO_OPTIONS, MK_FOO is set to "no",
# unless WITH_FOO is defined, in which case it is set to "yes".
#
# If both WITH_FOO and WITHOUT_FOO are defined, WITHOUT_FOO wins and
# MK_FOO is set to "no" regardless of which list it was in.
#
# Both __DEFAULT_YES_OPTIONS and __DEFAULT_NO_OPTIONS are undef'd
# after all this processing, allowing this file to be included
# multiple times with different lists.
#
# Other parts of the build system will set BROKEN_OPTIONS to a list
# of options that are broken on this platform. This will not be unset
# before returning. Clients are expected to always += this variable.
#
# Users should generally define WITH_FOO or WITHOUT_FOO, but the build
# system should use MK_FOO={yes,no} when it needs to override the
# user's desires or default behavior.
#

#
# MK_* options which default to "yes".
#
.for var in ${__DEFAULT_YES_OPTIONS}
.if !defined(MK_${var})
.if defined(WITHOUT_${var})			# WITHOUT always wins
MK_${var}:=	no
.else
MK_${var}:=	yes
.endif
.else
.if ${MK_${var}} != "yes" && ${MK_${var}} != "no"
.error "Illegal value for MK_${var}: ${MK_${var}}"
.endif
.endif # !defined(MK_${var})
.endfor
.undef __DEFAULT_YES_OPTIONS

#
# MK_* options which default to "no".
#
.for var in ${__DEFAULT_NO_OPTIONS}
.if !defined(MK_${var})
.if defined(WITH_${var}) && !defined(WITHOUT_${var}) # WITHOUT always wins
MK_${var}:=	yes
.else
MK_${var}:=	no
.endif
.else
.if ${MK_${var}} != "yes" && ${MK_${var}} != "no"
.error "Illegal value for MK_${var}: ${MK_${var}}"
.endif
.endif # !defined(MK_${var})
.endfor
.undef __DEFAULT_NO_OPTIONS

#
# MK_* options which are always no, usually because they are
# unsupported/badly broken on this architecture.
#
.for var in ${BROKEN_OPTIONS}
MK_${var}:=	no
.endfor

.for vv in ${__DEFAULT_DEPENDENT_OPTIONS}
.if defined(WITH_${vv:H}) && defined(WITHOUT_${vv:H})
MK_${vv:H}?= no
.elif defined(WITH_${vv:H})
MK_${vv:H}?= yes
.elif defined(WITHOUT_${vv:H})
MK_${vv:H}?= no
.else
MK_${vv:H}?= ${MK_${vv:T}}
.endif
MK_${vv:H}:= ${MK_${vv:H}}
.endfor
.undef __DEFAULT_DEPENDENT_OPTIONS
