# $OpenBSD: Makefile,v 1.5 2021/07/06 19:48:05 bluhm Exp $

REGRESS_TARGETS=	chmod_abs_0000 chmod_abs_0644
REGRESS_TARGETS+=	chmod_sym_0644 chmod_sym_0000
REGRESS_TARGETS+=	chmod_R_0755 chmod_RP_0755 chmod_RH_0755 chmod_RL_0755
REGRESS_TARGETS+=	chmod_h_abs_0000 chmod_h_abs_0644
REGRESS_TARGETS+=	chmod_h_sym_0000 chmod_h_sym_0644
REGRESS_TARGETS+=	chmod_h_go_minus_r chmod_h_go_minus_rx
REGRESS_TARGETS+=	chmod_h_go_plus_r chmod_h_go_plus_rx
REGRESS_TARGETS+=	chmod_via_link_abs_0000 chmod_via_link_abs_0644
REGRESS_TARGETS+=	chmod_via_link_sym_0000 chmod_via_link_sym_0644
REGRESS_TARGETS+=	chmod_via_link_go_minus_r chmod_via_link_go_minus_rx
REGRESS_TARGETS+=	chmod_via_link_go_plus_r chmod_via_link_go_plus_rx
REGRESS_TARGETS+=	chmod_error

REGRESS_TARGETS+=	chgrp_error

REGRESS_TARGETS+=	chown_error

REGRESS_TARGETS+=	chflags chflags_no
REGRESS_TARGETS+=	chflags_R chflags_RP chflags_RH chflags_RL
REGRESS_TARGETS+=	chflags_via_link
REGRESS_TARGETS+=	chflags_error

REGRESS_ROOT_TARGETS+=	chgrp_id
REGRESS_ROOT_TARGETS+=	chgrp_name
REGRESS_ROOT_TARGETS+=	chgrp_symlink
REGRESS_ROOT_TARGETS+=	chgrp_h_symlink
REGRESS_ROOT_TARGETS+=	chgrp_R
REGRESS_ROOT_TARGETS+=	chgrp_RP
REGRESS_ROOT_TARGETS+=	chgrp_RH
REGRESS_ROOT_TARGETS+=	chgrp_RL
REGRESS_ROOT_TARGETS+=	chown_id
REGRESS_ROOT_TARGETS+=	chown_name
REGRESS_ROOT_TARGETS+=	chown_symlink
REGRESS_ROOT_TARGETS+=	chown_h_symlink
REGRESS_ROOT_TARGETS+=	chown_R
REGRESS_ROOT_TARGETS+=	chown_RP
REGRESS_ROOT_TARGETS+=	chown_RH
REGRESS_ROOT_TARGETS+=	chown_RL

REGRESS_TARGETS+=	${REGRESS_ROOT_TARGETS}

MKDIR=			/bin/mkdir
TOUCH=			/usr/bin/touch
CHMOD=			/bin/chmod
LN=			/bin/ln
CHFLAGS=		/usr/bin/chflags
CHGRP=			${SUDO} /bin/chgrp
CHOWN=			${SUDO} /sbin/chown
STAT=			${SUDO} /usr/bin/stat
DIFF=			/usr/bin/diff -up
RM=			${SUDO} /bin/rm

prepfile: clean
	${MKDIR} -p regress
	${TOUCH} regress/file
	${CHMOD} 000 regress/file
	[ `${STAT} -f '%p' regress/file` == '100000' ]

chmod_prepsymlink: prepfile
	${LN} -s ${.OBJDIR}/regress/file regress/link
	${CHMOD} -h 755 regress/link
	[ `${STAT} -f '%p' regress/link` == '120755' ]

prepdir: clean
	${MKDIR} -p regress/dir
	${TOUCH} regress/dir/file1
	${TOUCH} regress/dir/file2
	${CHMOD} 000 regress/dir/file1
	${CHMOD} 000 regress/dir/file2
	${TOUCH} regress/link_target_file
	${CHMOD} 0600 regress/link_target_file
	[ `${STAT} -f '%p' regress/dir/file1` == '100000' ]
	[ `${STAT} -f '%p' regress/dir/file2` == '100000' ]
	${LN} -s ${.OBJDIR}/regress/link_target_file regress/dir/linked_file
	${LN} -s ${.OBJDIR}/regress/dir regress/linked_dir
	${CHMOD} 000 regress/dir
	[ `${STAT} -f '%p' regress/dir` == '40000' ]
	[ `${STAT} -f '%p' regress/link_target_file` == '100600' ]

chown_prepfile: prepfile
	${CHOWN} root:wheel regress/file
	[ `${STAT} -f '%Su' regress/file` == 'root' ]
	[ `${STAT} -f '%Sg' regress/file` == 'wheel' ]

chown_prepsymlink: chown_prepfile
	${LN} -s ${.OBJDIR}/regress/file regress/symlink
	${CHOWN} -h root:wheel regress/symlink
	[ `${STAT} -f '%Su' regress/symlink` == 'root' ]
	[ `${STAT} -f '%Sg' regress/symlink` == 'wheel' ]

chown_prepdir: prepdir
	${CHMOD} -R 0755 regress
	${CHOWN} -RL root:wheel regress

chflags_prepfile: prepfile
	${CHFLAGS} dump regress/file
	[ `${STAT} -f '%f' regress/file` == '0' ]

chflags_prepdir: prepdir
	${CHMOD} -R 0755 regress
	${CHFLAGS} -RL dump regress

chmod_abs_0000: prepfile
	${CHMOD} 0644 regress/file
	${CHMOD} 0000 regress/file
	[ `${STAT} -f '%p' regress/file` == '100000' ]

chmod_abs_0644: prepfile
	${CHMOD} 0644 regress/file
	[ `${STAT} -f '%p' regress/file` == '100644' ]

chmod_sym_0644: prepfile
	${CHMOD} u=rw,go=r regress/file
	[ `${STAT} -f '%p' regress/file` == '100644' ]

chmod_sym_0000: prepfile
	${CHMOD} 0644 regress/file
	${CHMOD} a-rwx regress/file
	[ `${STAT} -f '%p' regress/file` == '100000' ]

chmod_R_0755: prepdir
	${CHMOD} -R 0755 regress/dir
	[ `${STAT} -f '%p' regress/dir/file1` == '100755' ]
	[ `${STAT} -f '%p' regress/dir/file2` == '100755' ]
	[ `${STAT} -f '%p' regress/dir` == '40755' ]
	[ `${STAT} -f '%p' regress/link_target_file` == '100600' ]

chmod_RP_0755: prepdir
	${CHMOD} -RP 0755 regress/dir
	[ `${STAT} -f '%p' regress/dir/file1` == '100755' ]
	[ `${STAT} -f '%p' regress/dir/file2` == '100755' ]
	[ `${STAT} -f '%p' regress/dir` == '40755' ]
	[ `${STAT} -f '%p' regress/link_target_file` == '100600' ]

chmod_RH_0755: prepdir
	${CHMOD} -RH 0755 regress/linked_dir
	[ `${STAT} -f '%p' regress/dir/file1` == '100755' ]
	[ `${STAT} -f '%p' regress/dir/file2` == '100755' ]
	[ `${STAT} -f '%p' regress/dir` == '40755' ]
	[ `${STAT} -f '%p' regress/link_target_file` == '100600' ]

chmod_RL_0755: prepdir
	${CHMOD} -RL 0755 regress/linked_dir
	[ `${STAT} -f '%p' regress/dir/file1` == '100755' ]
	[ `${STAT} -f '%p' regress/dir/file2` == '100755' ]
	[ `${STAT} -f '%p' regress/dir` == '40755' ]
	[ `${STAT} -f '%p' regress/link_target_file` == '100755' ]

chmod_h_abs_0000: chmod_prepsymlink
	${CHMOD} -h 0000 regress/link
	[ `${STAT} -f '%p' regress/file` == '100000' ]
	[ `${STAT} -f '%p' regress/link` == '120000' ]

chmod_h_abs_0644: chmod_prepsymlink
	${CHMOD} -h 0644 regress/link
	[ `${STAT} -f '%p' regress/file` == '100000' ]
	[ `${STAT} -f '%p' regress/link` == '120644' ]

chmod_h_sym_0000: chmod_prepsymlink
	${CHMOD} -h a-rwx regress/link
	[ `${STAT} -f '%p' regress/file` == '100000' ]
	[ `${STAT} -f '%p' regress/link` == '120000' ]

chmod_h_sym_0644: chmod_prepsymlink
	${CHMOD} -h u=rw,go=r regress/link
	[ `${STAT} -f '%p' regress/file` == '100000' ]
	[ `${STAT} -f '%p' regress/link` == '120644' ]

chmod_h_go_minus_r: chmod_prepsymlink
	${CHMOD} -h go-r regress/link
	[ `${STAT} -f '%p' regress/file` == '100000' ]
	[ `${STAT} -f '%p' regress/link` == '120711' ]

chmod_h_go_minus_rx: chmod_prepsymlink
	${CHMOD} -h go-rx regress/link
	[ `${STAT} -f '%p' regress/file` == '100000' ]
	[ `${STAT} -f '%p' regress/link` == '120700' ]

chmod_h_go_plus_r: chmod_prepsymlink
	${CHMOD} -h go+r regress/link
	[ `${STAT} -f '%p' regress/file` == '100000' ]
	[ `${STAT} -f '%p' regress/link` == '120755' ]

chmod_h_go_plus_rx: chmod_prepsymlink
	${CHMOD} -h go+rx regress/link
	[ `${STAT} -f '%p' regress/file` == '100000' ]
	[ `${STAT} -f '%p' regress/link` == '120755' ]

chmod_via_link_abs_0000: chmod_prepsymlink
	${CHMOD} 0000 regress/link
	[ `${STAT} -f '%p' regress/file` == '100000' ]
	[ `${STAT} -f '%p' regress/link` == '120755' ]

chmod_via_link_abs_0644: chmod_prepsymlink
	${CHMOD} 0644 regress/link
	[ `${STAT} -f '%p' regress/file` == '100644' ]
	[ `${STAT} -f '%p' regress/link` == '120755' ]

chmod_via_link_sym_0000: chmod_prepsymlink
	${CHMOD} a-rwx regress/link
	[ `${STAT} -f '%p' regress/file` == '100000' ]
	[ `${STAT} -f '%p' regress/link` == '120755' ]

chmod_via_link_sym_0644: chmod_prepsymlink
	${CHMOD} u=rw,go=r regress/link
	[ `${STAT} -f '%p' regress/file` == '100644' ]
	[ `${STAT} -f '%p' regress/link` == '120755' ]

chmod_via_link_go_minus_r: chmod_prepsymlink
	${CHMOD} go-r regress/link
	[ `${STAT} -f '%p' regress/file` == '100000' ]
	[ `${STAT} -f '%p' regress/link` == '120755' ]

chmod_via_link_go_minus_rx: chmod_prepsymlink
	${CHMOD} go-rx regress/link
	[ `${STAT} -f '%p' regress/file` == '100000' ]
	[ `${STAT} -f '%p' regress/link` == '120755' ]

chmod_via_link_go_plus_r: chmod_prepsymlink
	${CHMOD} go+r regress/link
	[ `${STAT} -f '%p' regress/file` == '100044' ]
	[ `${STAT} -f '%p' regress/link` == '120755' ]

chmod_via_link_go_plus_rx: chmod_prepsymlink
	${CHMOD} go+rx regress/link
	[ `${STAT} -f '%p' regress/file` == '100055' ]
	[ `${STAT} -f '%p' regress/link` == '120755' ]

chmod_error:
	${CHMOD} foo bar 2>chmod.err || true
	${DIFF} ${.CURDIR}/chmod.error chmod.err

chgrp_id: chown_prepfile
	${CHGRP} 32767 regress/file
	[ `${STAT} -f '%g' regress/file` == 32767 ]

chgrp_name: chown_prepfile
	${CHGRP} nobody regress/file
	[ `${STAT} -f '%Sg' regress/file` == 'nobody' ]

chgrp_symlink: chown_prepsymlink
	${CHGRP} nobody regress/symlink
	[ `${STAT} -f '%Sg' regress/file` == 'nobody' ]
	[ `${STAT} -f '%Sg' regress/symlink` == 'wheel' ]

chgrp_h_symlink: chown_prepsymlink
	${CHGRP} -h nobody regress/symlink
	[ `${STAT} -f '%Sg' regress/file` == 'wheel' ]
	[ `${STAT} -f '%Sg' regress/symlink` == 'nobody' ]

chgrp_R: chown_prepdir
	${CHGRP} -R nobody regress/dir
	[ `${STAT} -f '%Sg' regress/dir/file1` == 'nobody' ]
	[ `${STAT} -f '%Sg' regress/dir/file2` == 'nobody' ]
	[ `${STAT} -f '%Sg' regress/dir` == 'nobody' ]
	[ `${STAT} -f '%Sg' regress/link_target_file` == 'wheel' ]

chgrp_RP: chown_prepdir
	${CHGRP} -RP nobody regress/dir
	[ `${STAT} -f '%Sg' regress/dir/file1` == 'nobody' ]
	[ `${STAT} -f '%Sg' regress/dir/file2` == 'nobody' ]
	[ `${STAT} -f '%Sg' regress/dir` == 'nobody' ]
	[ `${STAT} -f '%Sg' regress/link_target_file` == 'wheel' ]

chgrp_RH: chown_prepdir
	${CHGRP} -RH nobody regress/linked_dir
	[ `${STAT} -f '%Sg' regress/dir/file1` == 'nobody' ]
	[ `${STAT} -f '%Sg' regress/dir/file2` == 'nobody' ]
	[ `${STAT} -f '%Sg' regress/dir` == 'nobody' ]
	[ `${STAT} -f '%Sg' regress/link_target_file` == 'wheel' ]

chgrp_RL: chown_prepdir
	${CHGRP} -RL nobody regress/linked_dir
	[ `${STAT} -f '%Sg' regress/dir/file1` == 'nobody' ]
	[ `${STAT} -f '%Sg' regress/dir/file2` == 'nobody' ]
	[ `${STAT} -f '%Sg' regress/dir` == 'nobody' ]
	[ `${STAT} -f '%Sg' regress/link_target_file` == 'nobody' ]

chgrp_error:
	${CHGRP} __hopefully_not_existing_group bar 2>chgrp.err || true
	${DIFF} ${.CURDIR}/chgrp.error chgrp.err

chown_id: chown_prepfile
	${CHOWN} 32767 regress/file
	[ `${STAT} -f '%u' regress/file` == 32767 ]

chown_name: chown_prepfile
	${CHOWN} nobody regress/file
	[ `${STAT} -f '%Su' regress/file` == 'nobody' ]

chown_symlink: chown_prepsymlink
	${CHOWN} nobody regress/symlink
	[ `${STAT} -f '%Su' regress/file` == 'nobody' ]
	[ `${STAT} -f '%Su' regress/symlink` == 'root' ]

chown_h_symlink: chown_prepsymlink
	${CHOWN} -h nobody regress/symlink
	[ `${STAT} -f '%Su' regress/file` == 'root' ]
	[ `${STAT} -f '%Su' regress/symlink` == 'nobody' ]

chown_R: chown_prepdir
	${CHOWN} -R nobody regress/dir
	[ `${STAT} -f '%Su' regress/dir/file1` == 'nobody' ]
	[ `${STAT} -f '%Su' regress/dir/file2` == 'nobody' ]
	[ `${STAT} -f '%Su' regress/dir` == 'nobody' ]
	[ `${STAT} -f '%Su' regress/link_target_file` != 'nobody' ]

chown_RP: chown_prepdir
	${CHOWN} -RP nobody regress/dir
	[ `${STAT} -f '%Su' regress/dir/file1` == 'nobody' ]
	[ `${STAT} -f '%Su' regress/dir/file2` == 'nobody' ]
	[ `${STAT} -f '%Su' regress/dir` == 'nobody' ]
	[ `${STAT} -f '%Su' regress/link_target_file` != 'nobody' ]

chown_RH: chown_prepdir
	${CHOWN} -RH nobody regress/linked_dir
	[ `${STAT} -f '%Su' regress/dir/file1` == 'nobody' ]
	[ `${STAT} -f '%Su' regress/dir/file2` == 'nobody' ]
	[ `${STAT} -f '%Su' regress/dir` == 'nobody' ]
	[ `${STAT} -f '%Su' regress/link_target_file` != 'nobody' ]

chown_RL: chown_prepdir
	${CHOWN} -RL nobody regress/linked_dir
	[ `${STAT} -f '%Su' regress/dir/file1` == 'nobody' ]
	[ `${STAT} -f '%Su' regress/dir/file2` == 'nobody' ]
	[ `${STAT} -f '%Su' regress/dir` == 'nobody' ]
	[ `${STAT} -f '%Su' regress/link_target_file` == 'nobody' ]

chown_error:
	${CHOWN} __hopefully_not_existing_user bar 2>chown.err || true
	${DIFF} ${.CURDIR}/chown.error chown.err

chflags: chflags_prepfile
	${CHFLAGS} nodump regress/file
	[ `${STAT} -f '%f' regress/file` == '1' ]

chflags_no: chflags_prepfile
	${CHFLAGS} nodump regress/file
	[ `${STAT} -f '%f' regress/file` == '1' ]
	${CHFLAGS} dump regress/file
	[ `${STAT} -f '%f' regress/file` == '0' ]

chflags_R: chflags_prepdir
	${CHFLAGS} -R nodump regress/dir
	[ `${STAT} -f '%f' regress/dir/file1` == '1' ]
	[ `${STAT} -f '%f' regress/dir/file2` == '1' ]
	[ `${STAT} -f '%f' regress/dir` == '1' ]
	[ `${STAT} -f '%f' regress/link_target_file` == '0' ]

chflags_RP: chflags_prepdir
	${CHFLAGS} -RP nodump regress/dir
	[ `${STAT} -f '%f' regress/dir/file1` == '1' ]
	[ `${STAT} -f '%f' regress/dir/file2` == '1' ]
	[ `${STAT} -f '%f' regress/dir` == '1' ]
	[ `${STAT} -f '%f' regress/link_target_file` == '0' ]

chflags_RH: chflags_prepdir
	${CHFLAGS} -RH nodump regress/linked_dir
	[ `${STAT} -f '%f' regress/dir/file1` == '1' ]
	[ `${STAT} -f '%f' regress/dir/file2` == '1' ]
	[ `${STAT} -f '%f' regress/dir` == '1' ]
	[ `${STAT} -f '%f' regress/link_target_file` == '0' ]

chflags_RL: chflags_prepdir
	${CHFLAGS} -RL nodump regress/linked_dir
	[ `${STAT} -f '%f' regress/dir/file1` == '1' ]
	[ `${STAT} -f '%f' regress/dir/file2` == '1' ]
	[ `${STAT} -f '%f' regress/dir` == '1' ]
	[ `${STAT} -f '%f' regress/link_target_file` == '1' ]

chflags_via_link: chflags_prepfile
	${LN} -s ${.OBJDIR}/regress/file regress/link
	chflags -h uchg regress/link
	chflags nodump regress/link
	[ `${STAT} -f '%f' regress/link` == '2' ]
	[ `${STAT} -f '%f' regress/file` == '1' ]
	chflags -h nouchg regress/link
	chflags dump regress/link
	[ `${STAT} -f '%f' regress/link` == '0' ]
	[ `${STAT} -f '%f' regress/file` == '0' ]

chflags_error:
	${CHFLAGS} foo bar 2>chflags.err || true
	${DIFF} ${.CURDIR}/chflags.error chflags.err

clean:
	${RM} -f *.err
	${CHMOD} -R 0700 regress 2>/dev/null || true
	${RM} -rf regress

.PHONY: ${REGRESS_TARGETS} prepfile prepdir chown_prepfile chown_prepsymlink \
	    chown_prepdir chflags_prepfile chflags_prepdir

.include <bsd.regress.mk>
