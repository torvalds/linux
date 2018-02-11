#! /bin/bash
# SPDX-License-Identifier: GPL-2.0
# (c) 2015, Quentin Casasnovas <quentin.casasnovas@oracle.com>

obj=$1

file ${obj} | grep -q ELF || (echo "${obj} is not and ELF file." 1>&2 ; exit 0)

# Bail out early if there isn't an __ex_table section in this object file.
objdump -hj __ex_table ${obj} 2> /dev/null > /dev/null
[ $? -ne 0 ] && exit 0

white_list=.text,.fixup

suspicious_relocs=$(objdump -rj __ex_table ${obj}  | tail -n +6 |
			grep -v $(eval echo -e{${white_list}}) | awk '{print $3}')

# No suspicious relocs in __ex_table, jobs a good'un
[ -z "${suspicious_relocs}" ] && exit 0


# After this point, something is seriously wrong since we just found out we
# have some relocations in __ex_table which point to sections which aren't
# white listed.  If you're adding a new section in the Linux kernel, and
# you're expecting this section to contain code which can fault (i.e. the
# __ex_table relocation to your new section is expected), simply add your
# new section to the white_list variable above.  If not, you're probably
# doing something wrong and the rest of this code is just trying to print
# you more information about it.

function find_section_offset_from_symbol()
{
    eval $(objdump -t ${obj} | grep ${1} | sed 's/\([0-9a-f]\+\) .\{7\} \([^ \t]\+\).*/section="\2"; section_offset="0x\1" /')

    # addr2line takes addresses in hexadecimal...
    section_offset=$(printf "0x%016x" $(( ${section_offset} + $2 )) )
}

function find_symbol_and_offset_from_reloc()
{
    # Extract symbol and offset from the objdump output
    eval $(echo $reloc | sed 's/\([^+]\+\)+\?\(0x[0-9a-f]\+\)\?/symbol="\1"; symbol_offset="\2"/')

    # When the relocation points to the begining of a symbol or section, it
    # won't print the offset since it is zero.
    if [ -z "${symbol_offset}" ]; then
	symbol_offset=0x0
    fi
}

function find_alt_replacement_target()
{
    # The target of the .altinstr_replacement is the relocation just before
    # the .altinstr_replacement one.
    eval $(objdump -rj .altinstructions ${obj} | grep -B1 "${section}+${section_offset}" | head -n1 | awk '{print $3}' |
	   sed 's/\([^+]\+\)+\(0x[0-9a-f]\+\)/alt_target_section="\1"; alt_target_offset="\2"/')
}

function handle_alt_replacement_reloc()
{
    # This will define alt_target_section and alt_target_section_offset
    find_alt_replacement_target ${section} ${section_offset}

    echo "Error: found a reference to .altinstr_replacement in __ex_table:"
    addr2line -fip -j ${alt_target_section} -e ${obj} ${alt_target_offset} | awk '{print "\t" $0}'

    error=true
}

function is_executable_section()
{
    objdump -hwj ${section} ${obj} | grep -q CODE
    return $?
}

function handle_suspicious_generic_reloc()
{
    if is_executable_section ${section}; then
	# We've got a relocation to a non white listed _executable_
	# section, print a warning so the developper adds the section to
	# the white list or fix his code.  We try to pretty-print the file
	# and line number where that relocation was added.
	echo "Warning: found a reference to section \"${section}\" in __ex_table:"
	addr2line -fip -j ${section} -e ${obj} ${section_offset} | awk '{print "\t" $0}'
    else
	# Something is definitively wrong here since we've got a relocation
	# to a non-executable section, there's no way this would ever be
	# running in the kernel.
	echo "Error: found a reference to non-executable section \"${section}\" in __ex_table at offset ${section_offset}"
	error=true
    fi
}

function handle_suspicious_reloc()
{
    case "${section}" in
	".altinstr_replacement")
	    handle_alt_replacement_reloc ${section} ${section_offset}
	    ;;
	*)
	    handle_suspicious_generic_reloc ${section} ${section_offset}
	    ;;
    esac
}

function diagnose()
{

    for reloc in ${suspicious_relocs}; do
	# Let's find out where the target of the relocation in __ex_table
	# is, this will define ${symbol} and ${symbol_offset}
	find_symbol_and_offset_from_reloc ${reloc}

	# When there's a global symbol at the place of the relocation,
	# objdump will use it instead of giving us a section+offset, so
	# let's find out which section is this symbol in and the total
	# offset withing that section.
	find_section_offset_from_symbol ${symbol} ${symbol_offset}

	# In this case objdump was presenting us with a reloc to a symbol
	# rather than a section. Now that we've got the actual section,
	# we can skip it if it's in the white_list.
	if [ -z "$( echo $section | grep -v $(eval echo -e{${white_list}}))" ]; then
	    continue;
	fi

	# Will either print a warning if the relocation happens to be in a
	# section we do not know but has executable bit set, or error out.
	handle_suspicious_reloc
    done
}

function check_debug_info() {
    objdump -hj .debug_info ${obj} 2> /dev/null > /dev/null ||
	echo -e "${obj} does not contain debug information, the addr2line output will be limited.\n" \
	     "Recompile ${obj} with CONFIG_DEBUG_INFO to get a more useful output."
}

check_debug_info

diagnose

if [ "${error}" ]; then
    exit 1
fi

exit 0
