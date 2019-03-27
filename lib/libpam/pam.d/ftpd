#
# $FreeBSD$
#
# PAM configuration for the "ftpd" service
#

# auth
auth		sufficient	pam_opie.so		no_warn no_fake_prompts
auth		requisite	pam_opieaccess.so	no_warn allow_local
#auth		sufficient	pam_krb5.so		no_warn
#auth		sufficient	pam_ssh.so		no_warn try_first_pass
auth		required	pam_unix.so		no_warn try_first_pass

# account
account		required	pam_nologin.so
#account	required	pam_krb5.so
account		required	pam_unix.so

# session
session		required	pam_permit.so
