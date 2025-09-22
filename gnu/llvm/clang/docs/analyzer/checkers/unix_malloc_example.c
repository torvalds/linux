
void test() {
  int *p = malloc(1);
  free(p);
  free(p); // warn: attempt to free released memory
}

void test() {
  int *p = malloc(sizeof(int));
  free(p);
  *p = 1; // warn: use after free
}

void test() {
  int *p = malloc(1);
  if (p)
    return; // warn: memory is never released
}

void test() {
  int a[] = { 1 };
  free(a); // warn: argument is not allocated by malloc
}

void test() {
  int *p = malloc(sizeof(char));
  p = p - 1;
  free(p); // warn: argument to free() is offset by -4 bytes
}

