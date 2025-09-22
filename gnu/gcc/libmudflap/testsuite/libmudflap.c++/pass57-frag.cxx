#include <vector>
#include <string>

class fitscolumn
  {
  private:
    std::string name_, unit_;
   int i, t;
  public:
    fitscolumn (const std::string &nm, const std::string &un,int i1,int t1)
      : name_(nm), unit_(un), i(i1), t(t1){}
  };

void init_bintab(std::vector<fitscolumn> & columns_)
{
  char ttype[81], tunit[81], tform[81];
  long repc;
  int typecode;
  columns_.push_back (fitscolumn (ttype,tunit,1,typecode));
}

int main ()
{
  return 0;
}
