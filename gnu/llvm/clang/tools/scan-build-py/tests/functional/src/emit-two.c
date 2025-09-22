
int bad_guy(int * i)
{
    *i = 9;
    return *i;
}

void bad_guy_test()
{
    int * ptr = 0;

    bad_guy(ptr);
}
