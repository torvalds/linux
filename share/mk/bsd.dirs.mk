# $FreeBSD$
#
# Directory permissions management.

.if !target(__<bsd.dirs.mk>__)
__<bsd.dirs.mk>__:
# List of directory variable names to install.  Each variable name's value
# must be a full path.  If non-default permissions are desired, <DIR>_MODE,
# <DIR>_OWN, and <DIR>_GRP may be specified.
DIRS?=

.  for dir in ${DIRS:O:u}
.    if defined(${dir}) && !empty(${dir})
# Set default permissions for a directory
${dir}_MODE?=	0755
${dir}_OWN?=	root
${dir}_GRP?=	wheel
.      if defined(${dir}_FLAGS) && !empty(${dir}_FLAGS)
${dir}_FLAG=	-f ${${dir}_FLAGS}
.      endif

.      if defined(NO_ROOT)
.        if !defined(${dir}TAGS) || ! ${${dir}TAGS:Mpackage=*}
${dir}TAGS+=		package=${${dir}PACKAGE:Uruntime}
.        endif
${dir}TAG_ARGS=	-T ${${dir}TAGS:[*]:S/ /,/g}
.      endif

installdirs: installdirs-${dir}
# Coalesce duplicate destdirs
.      if !defined(_uniquedirs_${${dir}})
_uniquedirs_${${dir}}=	${dir}
_alldirs_${dir}=	${dir}
installdirs-${dir}: .PHONY
	@${ECHO} installing DIRS ${_alldirs_${dir}}
	${INSTALL} ${${dir}TAG_ARGS} -d -m ${${dir}_MODE} -o ${${dir}_OWN} \
		-g ${${dir}_GRP} ${${dir}_FLAG} ${DESTDIR}${${dir}}
.      else
_uniquedir:=		${_uniquedirs_${${dir}}}
_alldirs_${_uniquedir}+=${dir}
# Connect to the single target
installdirs-${dir}: installdirs-${_uniquedir}
# Validate that duplicate dirs have the same metadata.
.        for v in TAG_ARGS _MODE _OWN _GRP _FLAG
.          if ${${dir}${v}:Uunset} != ${${_uniquedir}${v}:Uunset}
.            warning ${RELDIR}: ${dir}${v} (${${dir}${v}:U}) does not match ${_uniquedir}${v} (${${_uniquedir}${v}:U}) but both install to ${${dir}}
.          endif
.        endfor
.      endif	# !defined(_uniquedirs_${${dir}})
.    endif	# defined(${dir}) && !empty(${dir})
.  endfor

realinstall: installdirs

.endif
