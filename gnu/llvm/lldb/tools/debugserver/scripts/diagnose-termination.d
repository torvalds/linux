fbt::exception_deliver:entry
{
   printf("pid %d got an exception of type %d\n", pid, arg1);
   stack();
   ustack();
}

syscall::kill:entry
{
   printf("pid %d called kill(%d, %d)\n", pid, arg0, arg1);
   ustack();
}

syscall::__pthread_kill:entry
{
   printf("pid %d called pthread_kill(%p, %d)\n", pid, arg0, arg1);
   ustack();
}
