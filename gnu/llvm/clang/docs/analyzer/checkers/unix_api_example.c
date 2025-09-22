
// Currently the check is performed for apple targets only.
void test(const char *path) {
  int fd = open(path, O_CREAT);
    // warn: call to 'open' requires a third argument when the
    // 'O_CREAT' flag is set
}

void f();

void test() {
  pthread_once_t pred = {0x30B1BCBA, {0}};
  pthread_once(&pred, f);
    // warn: call to 'pthread_once' uses the local variable
}

void test() {
  void *p = malloc(0); // warn: allocation size of 0 bytes
}

void test() {
  void *p = calloc(0, 42); // warn: allocation size of 0 bytes
}

void test() {
  void *p = malloc(1);
  p = realloc(p, 0); // warn: allocation size of 0 bytes
}

void test() {
  void *p = alloca(0); // warn: allocation size of 0 bytes
}

void test() {
  void *p = valloc(0); // warn: allocation size of 0 bytes
}

