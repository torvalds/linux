#!/bin/sh
# $FreeBSD: src/tools/regression/fstest/tests/chown/00.t,v 1.1 2007/01/17 01:42:08 pjd Exp $

desc="chown changes ownership"

n0=`namegen`
n1=`namegen`
n2=`namegen`

expect 0 mkdir ${n2} 0755
cdir=`pwd`
cd ${n2}

# super-user can always modify ownership
# 2
expect 0 create ${n0} 0644
expect 0 chown ${n0} 123 456
expect 123,456 lstat ${n0} uid,gid
expect 0 chown ${n0} 0 0
expect 0,0 lstat ${n0} uid,gid
expect 0 unlink ${n0}
# 8
expect 0 mkfifo ${n0} 0644
expect 0 chown ${n0} 123 456
expect 123,456 lstat ${n0} uid,gid
expect 0 chown ${n0} 0 0
expect 0,0 lstat ${n0} uid,gid
expect 0 unlink ${n0}
# 14
expect 0 mkdir ${n0} 0755
expect 0 chown ${n0} 123 456
expect 123,456 lstat ${n0} uid,gid
expect 0 chown ${n0} 0 0
expect 0,0 lstat ${n0} uid,gid
expect 0 rmdir ${n0}
# 20
expect 0 create ${n0} 0644
expect 0 symlink ${n0} ${n1}
expect 0 chown ${n1} 123 456
expect 123,456 stat ${n1} uid,gid
expect 123,456 stat ${n0} uid,gid
expect 0 lchown ${n1} 135 579
expect 135,579 lstat ${n1} uid,gid
expect 123,456 stat ${n1} uid,gid
expect 123,456 stat ${n0} uid,gid
expect 0 unlink ${n0}
expect 0 unlink ${n1}

# non-super-user can modify file group if he is owner of a file and
# gid he is setting is in his groups list.
# 31
expect 0 create ${n0} 0644
expect 0 chown ${n0} 65534 65533
expect 65534,65533 lstat ${n0} uid,gid
expect 0 -u 65534 -g 65532,65531 chown ${n0} -1 65532
expect 65534,65532 lstat ${n0} uid,gid
expect 0 -u 65534 -g 65532,65531 chown ${n0} 65534 65531
expect 65534,65531 lstat ${n0} uid,gid
expect 0 unlink ${n0}

# chown(2) return 0 if user is not owner of a file, but chown(2) is called
# with both uid and gid equal to -1.
# 39
expect 0 create ${n0} 0644
expect 0 chown ${n0} 65534 65533
expect 0 -u 65532 -g 65531 chown ${n0} -1 -1
expect 0 unlink ${n0}

# when super-user calls chown(2), set-uid and set-gid bits are removed.
# 43
expect 0 create ${n0} 0644
expect 0 chown ${n0} 65534 65533
expect 0 chmod ${n0} 06555
expect 06555 lstat ${n0} mode
expect 0 chown ${n0} 65532 65531
expect 0555 lstat ${n0} mode
expect 0 unlink ${n0}
# 50
expect 0 create ${n0} 0644
expect 0 chown ${n0} 0 0
expect 0 chmod ${n0} 06555
expect 06555 lstat ${n0} mode
expect 0 chown ${n0} 65534 65533
expect 0555 lstat ${n0} mode
expect 0 unlink ${n0}
# 57
expect 0 create ${n0} 0644
expect 0 chown ${n0} 65534 65533
expect 0 chmod ${n0} 06555
expect 06555 lstat ${n0} mode
expect 0 chown ${n0} 0 0
expect 0555 lstat ${n0} mode
expect 0 unlink ${n0}

# when non-super-user calls chown(2) successfully, set-uid and set-gid bits are
# removed, except when both uid and gid are equal to -1.
# 64
expect 0 create ${n0} 0644
expect 0 chown ${n0} 65534 65533
expect 0 chmod ${n0} 06555
expect 06555 lstat ${n0} mode
expect 0 -u 65534 -g 65533,65532 chown ${n0} 65534 65532
expect 0555,65534,65532 lstat ${n0} mode,uid,gid
expect 0 chmod ${n0} 06555
expect 06555 lstat ${n0} mode
expect 0 -u 65534 -g 65533,65532 chown ${n0} -1 65533
expect 0555,65534,65533 lstat ${n0} mode,uid,gid
expect 0 chmod ${n0} 06555
expect 06555 lstat ${n0} mode
expect 0 -u 65534 -g 65533,65532 chown ${n0} -1 -1
expect 06555,65534,65533 lstat ${n0} mode,uid,gid
expect 0 unlink ${n0}
# 79
expect 0 mkdir ${n0} 0755
expect 0 chown ${n0} 65534 65533
expect 0 chmod ${n0} 06555
expect 06555 lstat ${n0} mode
expect 0 -u 65534 -g 65533,65532 chown ${n0} 65534 65532
expect 0555,65534,65532 lstat ${n0} mode,uid,gid
expect 0 chmod ${n0} 06555
expect 06555 lstat ${n0} mode
expect 0 -u 65534 -g 65533,65532 chown ${n0} -1 65533
expect 0555,65534,65533 lstat ${n0} mode,uid,gid
expect 0 chmod ${n0} 06555
expect 06555 lstat ${n0} mode
expect 0 -u 65534 -g 65533,65532 chown ${n0} -1 -1
expect 06555,65534,65533 lstat ${n0} mode,uid,gid
expect 0 rmdir ${n0}

# successful chown(2) call (except uid and gid equal to -1) updates ctime.
# 109
expect 0 create ${n0} 0644
ctime1=`${FSTEST} stat ${n0} ctime`
sleep 1
expect 0 chown ${n0} 65534 65533
expect 65534,65533 lstat ${n0} uid,gid
ctime2=`${FSTEST} stat ${n0} ctime`
test_check $ctime1 -lt $ctime2
expect 0 unlink ${n0}
# 114
expect 0 mkdir ${n0} 0755
ctime1=`${FSTEST} stat ${n0} ctime`
sleep 1
expect 0 chown ${n0} 65534 65533
expect 65534,65533 lstat ${n0} uid,gid
ctime2=`${FSTEST} stat ${n0} ctime`
test_check $ctime1 -lt $ctime2
expect 0 rmdir ${n0}
# 119
expect 0 mkfifo ${n0} 0644
ctime1=`${FSTEST} stat ${n0} ctime`
sleep 1
expect 0 chown ${n0} 65534 65533
expect 65534,65533 lstat ${n0} uid,gid
ctime2=`${FSTEST} stat ${n0} ctime`
test_check $ctime1 -lt $ctime2
expect 0 unlink ${n0}
# 124
expect 0 symlink ${n1} ${n0}
ctime1=`${FSTEST} lstat ${n0} ctime`
sleep 1
expect 0 lchown ${n0} 65534 65533
expect 65534,65533 lstat ${n0} uid,gid
ctime2=`${FSTEST} lstat ${n0} ctime`
test_check $ctime1 -lt $ctime2
expect 0 unlink ${n0}
# 129
expect 0 create ${n0} 0644
expect 0 chown ${n0} 65534 65533
ctime1=`${FSTEST} stat ${n0} ctime`
sleep 1
expect 0 -u 65534 -g 65532 chown ${n0} 65534 65532
expect 65534,65532 lstat ${n0} uid,gid
ctime2=`${FSTEST} stat ${n0} ctime`
test_check $ctime1 -lt $ctime2
expect 0 unlink ${n0}
# 135
expect 0 mkdir ${n0} 0755
expect 0 chown ${n0} 65534 65533
ctime1=`${FSTEST} stat ${n0} ctime`
sleep 1
expect 0 -u 65534 -g 65532 chown ${n0} 65534 65532
expect 65534,65532 lstat ${n0} uid,gid
ctime2=`${FSTEST} stat ${n0} ctime`
test_check $ctime1 -lt $ctime2
expect 0 rmdir ${n0}
# 141
expect 0 mkfifo ${n0} 0644
expect 0 chown ${n0} 65534 65533
ctime1=`${FSTEST} stat ${n0} ctime`
sleep 1
expect 0 chown ${n0} 65534 65533
expect 0 -u 65534 -g 65532 chown ${n0} 65534 65532
expect 65534,65532 lstat ${n0} uid,gid
ctime2=`${FSTEST} stat ${n0} ctime`
test_check $ctime1 -lt $ctime2
expect 0 unlink ${n0}
# 148
expect 0 symlink ${n1} ${n0}
expect 0 lchown ${n0} 65534 65533
ctime1=`${FSTEST} lstat ${n0} ctime`
sleep 1
expect 0 -u 65534 -g 65532 lchown ${n0} 65534 65532
expect 65534,65532 lstat ${n0} uid,gid
ctime2=`${FSTEST} lstat ${n0} ctime`
test_check $ctime1 -lt $ctime2
expect 0 unlink ${n0}
# 154
expect 0 create ${n0} 0644
ctime1=`${FSTEST} stat ${n0} ctime`
sleep 1
expect 0 chown ${n0} -1 -1
ctime2=`${FSTEST} stat ${n0} ctime`
test_check $ctime1 -eq $ctime2
expect 0 unlink ${n0}
# 158
expect 0 mkdir ${n0} 0644
ctime1=`${FSTEST} stat ${n0} ctime`
sleep 1
expect 0 chown ${n0} -1 -1
ctime2=`${FSTEST} stat ${n0} ctime`
test_check $ctime1 -eq $ctime2
expect 0 rmdir ${n0}
# 162
expect 0 mkfifo ${n0} 0644
ctime1=`${FSTEST} stat ${n0} ctime`
sleep 1
expect 0 chown ${n0} -1 -1
ctime2=`${FSTEST} stat ${n0} ctime`
test_check $ctime1 -eq $ctime2
expect 0 unlink ${n0}
# 166
expect 0 symlink ${n1} ${n0}
ctime1=`${FSTEST} lstat ${n0} ctime`
sleep 1
expect 0 lchown ${n0} -1 -1
ctime2=`${FSTEST} lstat ${n0} ctime`
test_check $ctime1 -eq $ctime2
expect 0 unlink ${n0}

# unsuccessful chown(2) does not update ctime.
# 170
expect 0 create ${n0} 0644
ctime1=`${FSTEST} stat ${n0} ctime`
sleep 1
expect EPERM -u 65534 chown ${n0} 65534 -1
ctime2=`${FSTEST} stat ${n0} ctime`
test_check $ctime1 -eq $ctime2
expect 0 unlink ${n0}
# 174
expect 0 mkdir ${n0} 0755
ctime1=`${FSTEST} stat ${n0} ctime`
sleep 1
expect EPERM -u 65534 -g 65534 chown ${n0} -1 65534
ctime2=`${FSTEST} stat ${n0} ctime`
test_check $ctime1 -eq $ctime2
expect 0 rmdir ${n0}
# 178
expect 0 mkfifo ${n0} 0644
ctime1=`${FSTEST} stat ${n0} ctime`
sleep 1
expect EPERM -u 65534 -g 65534 chown ${n0} 65534 65534
ctime2=`${FSTEST} stat ${n0} ctime`
test_check $ctime1 -eq $ctime2
expect 0 unlink ${n0}
# 182
expect 0 symlink ${n1} ${n0}
ctime1=`${FSTEST} lstat ${n0} ctime`
sleep 1
expect EPERM -u 65534 -g 65534 lchown ${n0} 65534 65534
ctime2=`${FSTEST} lstat ${n0} ctime`
test_check $ctime1 -eq $ctime2
expect 0 unlink ${n0}

# 186
cd ${cdir}
expect 0 rmdir ${n2}
