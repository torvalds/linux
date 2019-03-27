#
# $FreeBSD$
#
# PAM configuration for the "login" service
#

# auth
auth		sufficient	pam_self.so		no_warn
auth		include		system

# account
account		requisite	pam_securetty.so
account		required	pam_nologin.so
account		include		system

# session
session		include		system

# password
password	include		system
