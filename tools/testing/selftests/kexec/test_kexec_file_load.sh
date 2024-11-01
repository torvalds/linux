#!/bin/sh
# SPDX-License-Identifier: GPL-2.0
#
# Loading a kernel image via the kexec_file_load syscall can verify either
# the IMA signature stored in the security.ima xattr or the PE signature,
# both signatures depending on the IMA policy, or none.
#
# To determine whether the kernel image is signed, this test depends
# on pesign and getfattr.  This test also requires the kernel to be
# built with CONFIG_IKCONFIG enabled and either CONFIG_IKCONFIG_PROC
# enabled or access to the extract-ikconfig script.

TEST="KEXEC_FILE_LOAD"
. ./kexec_common_lib.sh

trap "{ rm -f $IKCONFIG ; }" EXIT

# Some of the IMA builtin policies may require the kexec kernel image to
# be signed, but these policy rules may be replaced with a custom
# policy.  Only CONFIG_IMA_APPRAISE_REQUIRE_KEXEC_SIGS persists after
# loading a custom policy.  Check if it is enabled, before reading the
# IMA runtime sysfs policy file.
# Return 1 for IMA signature required and 0 for not required.
is_ima_sig_required()
{
	local ret=0

	kconfig_enabled "CONFIG_IMA_APPRAISE_REQUIRE_KEXEC_SIGS=y" \
		"IMA kernel image signature required"
	if [ $? -eq 1 ]; then
		log_info "IMA signature required"
		return 1
	fi

	# The architecture specific or a custom policy may require the
	# kexec kernel image be signed.  Policy rules are walked
	# sequentially.  As a result, a policy rule may be defined, but
	# might not necessarily be used.  This test assumes if a policy
	# rule is specified, that is the intent.

	# First check for appended signature (modsig), then xattr
	if [ $ima_read_policy -eq 1 ]; then
		check_ima_policy "appraise" "func=KEXEC_KERNEL_CHECK" \
			"appraise_type=imasig|modsig"
		ret=$?
		if [ $ret -eq 1 ]; then
			log_info "IMA or appended(modsig) signature required"
		else
			check_ima_policy "appraise" "func=KEXEC_KERNEL_CHECK" \
				"appraise_type=imasig"
			ret=$?
			[ $ret -eq 1 ] && log_info "IMA signature required";
		fi
	fi
	return $ret
}

# The kexec_file_load_test() is complicated enough, require pesign.
# Return 1 for PE signature found and 0 for not found.
check_for_pesig()
{
	which pesign > /dev/null 2>&1 || log_skip "pesign not found"

	pesign -i $KERNEL_IMAGE --show-signature | grep -q "No signatures"
	local ret=$?
	if [ $ret -eq 1 ]; then
		log_info "kexec kernel image PE signed"
	else
		log_info "kexec kernel image not PE signed"
	fi
	return $ret
}

# The kexec_file_load_test() is complicated enough, require getfattr.
# Return 1 for IMA signature found and 0 for not found.
check_for_imasig()
{
	local ret=0

	which getfattr > /dev/null 2>&1
	if [ $?	-eq 1 ]; then
		log_skip "getfattr not found"
	fi

	line=$(getfattr -n security.ima -e hex --absolute-names $KERNEL_IMAGE 2>&1)
	echo $line | grep -q "security.ima=0x03"
	if [ $? -eq 0 ]; then
		ret=1
		log_info "kexec kernel image IMA signed"
	else
		log_info "kexec kernel image not IMA signed"
	fi
	return $ret
}

# Return 1 for appended signature (modsig) found and 0 for not found.
check_for_modsig()
{
	local module_sig_string="~Module signature appended~"
	local ret=0

	tail --bytes $((${#module_sig_string} + 1)) $KERNEL_IMAGE | \
		grep -q "$module_sig_string"
	if [ $? -eq 0 ]; then
		ret=1
		log_info "kexec kernel image modsig signed"
	else
		log_info "kexec kernel image not modsig signed"
	fi
	return $ret
}

kexec_file_load_test()
{
	local succeed_msg="kexec_file_load succeeded"
	local failed_msg="kexec_file_load failed"
	local key_msg="try enabling the CONFIG_INTEGRITY_PLATFORM_KEYRING"

	line=$(kexec --load --kexec-file-syscall $KERNEL_IMAGE 2>&1)

	if [ $? -eq 0 ]; then
		kexec --unload --kexec-file-syscall

		# In secureboot mode with an architecture  specific
		# policy, make sure either an IMA or PE signature exists.
		if [ $secureboot -eq 1 ] && [ $arch_policy -eq 1 ] && \
			[ $ima_signed -eq 0 ] && [ $pe_signed -eq 0 ] \
			  && [ $ima_modsig -eq 0 ]; then
			log_fail "$succeed_msg (missing sig)"
		fi

		if [ $kexec_sig_required -eq 1 -o $pe_sig_required -eq 1 ] \
		     && [ $pe_signed -eq 0 ]; then
			log_fail "$succeed_msg (missing PE sig)"
		fi

		if [ $ima_sig_required -eq 1 ] && [ $ima_signed -eq 0 ] \
		     && [ $ima_modsig -eq 0 ]; then
			log_fail "$succeed_msg (missing IMA sig)"
		fi

		if [ $pe_sig_required -eq 0 ] && [ $ima_appraise -eq 1 ] \
		    && [ $ima_sig_required -eq 0 ] && [ $ima_signed -eq 0 ] \
	            && [ $ima_read_policy -eq 0 ]; then
			log_fail "$succeed_msg (possibly missing IMA sig)"
		fi

		if [ $pe_sig_required -eq 0 ] && [ $ima_appraise -eq 0 ]; then
			log_info "No signature verification required"
		elif [ $pe_sig_required -eq 0 ] && [ $ima_appraise -eq 1 ] \
		    && [ $ima_sig_required -eq 0 ] && [ $ima_signed -eq 0 ] \
	            && [ $ima_read_policy -eq 1 ]; then
			log_info "No signature verification required"
		fi

		log_pass "$succeed_msg"
	fi

	# Check the reason for the kexec_file_load failure
	echo $line | grep -q "Required key not available"
	if [ $? -eq 0 ]; then
		if [ $platform_keyring -eq 0 ]; then
			log_pass "$failed_msg (-ENOKEY), $key_msg"
		else
			log_pass "$failed_msg (-ENOKEY)"
		fi
	fi

	if [ $kexec_sig_required -eq 1 -o $pe_sig_required -eq 1 ] \
	     && [ $pe_signed -eq 0 ]; then
		log_pass "$failed_msg (missing PE sig)"
	fi

	if [ $ima_sig_required -eq 1 ] && [ $ima_signed -eq 0 ]; then
		log_pass "$failed_msg (missing IMA sig)"
	fi

	if [ $pe_sig_required -eq 0 ] && [ $ima_appraise -eq 1 ] \
	    && [ $ima_sig_required -eq 0 ] && [ $ima_read_policy -eq 0 ] \
	    && [ $ima_signed -eq 0 ]; then
		log_pass "$failed_msg (possibly missing IMA sig)"
	fi

	log_pass "$failed_msg"
	return 0
}

# kexec requires root privileges
require_root_privileges

# get the kernel config
get_kconfig

kconfig_enabled "CONFIG_KEXEC_FILE=y" "kexec_file_load is enabled"
if [ $? -eq 0 ]; then
	log_skip "kexec_file_load is not enabled"
fi

# Determine which kernel config options are enabled
kconfig_enabled "CONFIG_IMA_APPRAISE=y" "IMA enabled"
ima_appraise=$?

kconfig_enabled "CONFIG_IMA_ARCH_POLICY=y" \
	"architecture specific policy enabled"
arch_policy=$?

kconfig_enabled "CONFIG_INTEGRITY_PLATFORM_KEYRING=y" \
	"platform keyring enabled"
platform_keyring=$?

kconfig_enabled "CONFIG_IMA_READ_POLICY=y" "reading IMA policy permitted"
ima_read_policy=$?

kconfig_enabled "CONFIG_KEXEC_SIG_FORCE=y" \
	"kexec signed kernel image required"
kexec_sig_required=$?

kconfig_enabled "CONFIG_KEXEC_BZIMAGE_VERIFY_SIG=y" \
	"PE signed kernel image required"
pe_sig_required=$?

is_ima_sig_required
ima_sig_required=$?

get_secureboot_mode
secureboot=$?

# Are there pe and ima signatures
if [ "$(get_arch)" == 'ppc64le' ]; then
	pe_signed=0
else
	check_for_pesig
	pe_signed=$?
fi

check_for_imasig
ima_signed=$?

check_for_modsig
ima_modsig=$?

# Test loading the kernel image via kexec_file_load syscall
kexec_file_load_test
