#ifdef __NR_semop
DO_TEST(semop, __NR_semop)
#endif

#ifdef __NR_semget
DO_TEST(semget, __NR_semget)
#endif

#ifdef __NR_semctl
DO_TEST(semctl, __NR_semctl)
#endif

#ifdef __NR_semtimedop
DO_TEST(semtimedop, __NR_semtimedop)
#endif

#ifdef __NR_msgsnd
DO_TEST(msgsnd, __NR_msgsnd)
#endif

#ifdef __NR_msgrcv
DO_TEST(msgrcv, __NR_msgrcv)
#endif

#ifdef __NR_msgget
DO_TEST(msgget, __NR_msgget)
#endif

#ifdef __NR_msgctl
DO_TEST(msgctl, __NR_msgctl)
#endif

#ifdef __NR_shmat
DO_TEST(shmat, __NR_shmat)
#endif

#ifdef __NR_shmdt
DO_TEST(shmdt, __NR_shmdt)
#endif

#ifdef __NR_shmget
DO_TEST(shmget, __NR_shmget)
#endif

#ifdef __NR_shmctl
DO_TEST(shmctl, __NR_shmctl)
#endif
