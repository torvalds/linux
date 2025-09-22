/* Test an odd construct for compilability.  */
static void *fwd;
void *bwd = &fwd;
static void *fwd = &bwd;

int main ()
{
  return 0;
}
