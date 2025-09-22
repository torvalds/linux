struct k 
{
  struct {
    int b;
    int c;
  } a;
};

static struct k l;
static struct k m;

void foo ()
{
  /* This should not be instrumented. */ 
  l.a.b = 5;
}

void bar ()
{
  /* This should not be instrumented. */ 
  m.a.b = 5;
}

int main ()
{
  /* Force TREE_ADDRESSABLE on "l" only.  */
  volatile int *k = & l.a.c;
  *k = 8;
  __mf_set_options ("-mode-violate");
  foo ();
  bar ();
  __mf_set_options ("-mode-check");
}
