#!/bin/sh
#
# $FreeBSD$
#

main() {
	desc=
	comment=
	debug=
	uclsource=
	while getopts "do:s:u:" arg; do
		case ${arg} in
		d)
			debug=1
			;;
		o)
			outname="${OPTARG}"
			origname="${OPTARG}"
			;;
		s)
			srctree="${OPTARG}"
			;;
		u)
			uclfile="${OPTARG}"
			;;
		*)
			echo "Unknown argument"
			;;
		esac
	done

	shift $(( ${OPTIND} - 1 ))

	outname="$(echo ${outname} | tr '-' '_')"

	case "${outname}" in
		runtime)
			outname="runtime"
			uclfile="${uclfile}"
			;;
		runtime_manuals)
			outname="${origname}"
			pkgdeps="runtime"
			;;
		runtime_*)
			outname="${origname}"
			uclfile="${outname##*}${uclfile}"
			pkgdeps="runtime"
			_descr="$(make -C ${srctree}/release/packages -f Makefile.package -V ${outname}_DESCR)"
			;;
		jail_*)
			outname="${origname}"
			uclfile="${outname##*}${uclfile}"
			pkgdeps="runtime"
			_descr="$(make -C ${srctree}/release/packages -f Makefile.package -V ${outname}_DESCR)"
			;;
		*_lib32_development)
			outname="${outname%%_lib32_development}"
			_descr="32-bit Libraries, Development Files"
			pkgdeps="${outname}"
			;;
		*_lib32_debug)
			outname="${outname%%_lib32_debug}"
			_descr="32-bit Libraries, Debugging Symbols"
			pkgdeps="${outname}"
			;;
		*_lib32_profile)
			outname="${outname%%_lib32_profile}"
			_descr="32-bit Libraries, Profiling"
			pkgdeps="${outname}"
			;;
		*_lib32)
			outname="${outname%%_lib32}"
			_descr="32-bit Libraries"
			pkgdeps="${outname}"
			;;
		*_development)
			outname="${outname%%_development}"
			_descr="Development Files"
			pkgdeps="${outname}"
			;;
		*_profile)
			outname="${outname%%_profile}"
			_descr="Profiling Libraries"
			pkgdeps="${outname}"
			;;
		*_debug)
			outname="${outname%%_debug}"
			_descr="Debugging Symbols"
			pkgdeps="${outname}"
			;;
		${origname})
			pkgdeps="runtime"
			;;
		*)
			uclfile="${outname##*}${origname}"
			outname="${outname##*}${origname}"
			;;
	esac

	outname="${outname%%_*}"

	desc="$(make -C ${srctree}/release/packages -f Makefile.package -V ${outname}_DESC)"
	comment="$(make -C ${srctree}/release/packages -f Makefile.package -V ${outname}_COMMENT)"

	uclsource="${srctree}/release/packages/${outname}.ucl"
	if [ ! -e "${uclsource}" ]; then
		uclsource="${srctree}/release/packages/template.ucl"
	fi

	if [ ! -z "${debug}" ]; then
		echo ""
		echo "==============================================================="
		echo "DEBUG:"
		echo "_descr=${_descr}"
		echo "outname=${outname}"
		echo "origname=${origname}"
		echo "srctree=${srctree}"
		echo "uclfile=${uclfile}"
		echo "desc=${desc}"
		echo "comment=${comment}"
		echo "cp ${uclsource} -> ${uclfile}"
		echo "==============================================================="
		echo ""
		echo ""
		echo ""
	fi

	[ -z "${comment}" ] && comment="${outname} package"
	[ ! -z "${_descr}" ] && comment="${comment} (${_descr})"
	[ -z "${desc}" ] && desc="${outname} package"

	cp "${uclsource}" "${uclfile}"
	cap_arg="$( make -f ${srctree}/share/mk/bsd.endian.mk -VCAP_MKDB_ENDIAN )"
	sed -i '' -e "s/%VERSION%/${PKG_VERSION}/" \
		-e "s/%PKGNAME%/${origname}/" \
		-e "s/%COMMENT%/${comment}/" \
		-e "s/%DESC%/${desc}/" \
		-e "s/%CAP_MKDB_ENDIAN%/${cap_arg}/g" \
		-e "s/%PKGDEPS%/${pkgdeps}/" \
		${uclfile}
	return 0
}

main "${@}"
