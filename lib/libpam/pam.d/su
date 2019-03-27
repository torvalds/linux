#
# $FreeBSD$
#
# PAM configuration for the "su" service
#

# auth
auth		sufficient	pam_rootok.so		no_warn
auth		sufficient	pam_self.so		no_warn
auth		requisite	pam_group.so		no_warn group=wheel root_only fail_safe ruser
auth		include		system

# account
account		include		system

# session
session		required	pam_permit.so
