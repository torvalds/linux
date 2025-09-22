typedef struct { char *name; } dummy;
extern dummy d[];

int
main (void)
{
  dummy *pd = d;

  while (pd->name)
    {
      printf ("%s\n", pd->name);
      pd++;
    }

  return 0;
}
