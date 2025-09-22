#include <stdio.h>
#include <stdlib.h>

#define N_FIELDS 7
#define N_FUNCS 128
#define FUNCSPACING 20
#define N_STRUCTS 180 /* 1280 */
#define N_BASES 6
#define COVARIANT 0

const char *simple_types[] = { "bool", "char", "short", "int", "float",
			       "double", "long double", "wchar_t", "void *",
			       "char *"
};

void gl(const char *c) {
  printf("%s\n", c);
}

void g(const char *c) {
  printf("%s", c);
}

void g(int i) {
  printf("%d", i);
}

int uuid = 0;
char base_present[N_STRUCTS][N_STRUCTS];

// The return type for each function when doing covariant testcase generation.
short ret_types[N_STRUCTS][N_FUNCS*FUNCSPACING];

bool is_ambiguous(int s, int base) {
  for (int i = 0; i < N_STRUCTS; ++i) {
    if ((base_present[base][i] & base_present[s][i]) == 1)
      return true;
  }
  return false;
}

void add_bases(int s, int base) {
  for (int i = 0; i < N_STRUCTS; ++i)
    base_present[s][i] |= base_present[base][i];
  if (!COVARIANT)
    return;
  for (int i = 0; i < N_FUNCS*FUNCSPACING; ++i) {
    if (!ret_types[base][i])
      continue;
    if (!ret_types[s][i]) {
      ret_types[s][i] = ret_types[base][i];
      continue;
    }
    if (base_present[ret_types[base][i]][ret_types[s][i]])
      // If the return type of the function from this base dominates
      ret_types[s][i] = ret_types[base][i];
    if (base_present[ret_types[s][i]][ret_types[base][i]])
      // If a previous base dominates
      continue;
    // If neither dominates, we'll use this class.
    ret_types[s][i] = s;
  }
}

// This contains the class that has the final override for
// each class, for each function.
short final_override[N_STRUCTS][N_FUNCS*FUNCSPACING];

void gs(int s) {
  bool polymorphic = false;

  static int bases[N_BASES];
  int i_bases = random() % (N_BASES*2);
  if (i_bases >= N_BASES)
    // PARAM: 1/2 of all clases should have no bases
    i_bases = 0;
  int n_bases = 0;
  bool first_base = true;
  
  // PARAM: 3/4 of all should be class, the rest are structs
  if (random() % 4 == 0)
    g("struct s");
  else
    g("class s");
  g(s);
  int old_base = -1;
  if (s == 0 || s == 1)
    i_bases = 0;
  while (i_bases) {
    --i_bases;
    int base = random() % (s-1) + 1;
    if (!base_present[s][base]) {
      if (is_ambiguous(s, base))
	continue;
      if (first_base) {
	first_base = false;
	g(": ");
      } else
	g(", ");
      int base_type = 1;
      if (random()%8 == 0) {
	// PARAM: 1/8th the bases are virtual
	g("virtual ");
        // We have a vtable and rtti, but technically we're not polymorphic
	// polymorphic = true;
	base_type = 3;
      }
      // PARAM: 1/4 are public, 1/8 are privare, 1/8 are protected, the reset, default
      int base_protection = 0;
      if (!COVARIANT)
        base_protection = random()%8;
      switch (base_protection) {
      case 0:
      case 1:
	g("public "); break;
      case 2:
      case 3:
      case 4:
      case 5:
	break;
      case 6:
	g("private "); break;
      case 7:
	g("protected "); break;
      }
      g("s");
      add_bases(s, base);
      bases[n_bases] = base;
      base_present[s][base] = base_type;
      ++n_bases;
      g(base);
      old_base = base;
    }
  }
  gl(" {");

  /* Fields */
  int n_fields = N_FIELDS == 0 ? 0 : random() % (N_FIELDS*4);
  // PARAM: 3/4 of all structs should have no members
  if (n_fields >= N_FIELDS)
    n_fields = 0;
  for (int i = 0; i < n_fields; ++i) {
    int t = random() % (sizeof(simple_types) / sizeof(simple_types[0]));
    g("  "); g(simple_types[t]); g(" field"); g(i); gl(";");
  }

  /* Virtual functions */
  static int funcs[N_FUNCS*FUNCSPACING];
  // PARAM: 1/2 of all structs should have no virtual functions
  int n_funcs = random() % (N_FUNCS*2);
  if (n_funcs > N_FUNCS)
    n_funcs = 0;
  int old_func = -1;
  for (int i = 0; i < n_funcs; ++i) {
    int fn = old_func + random() % FUNCSPACING + 1;
    funcs[i] = fn;
    int ret_type = 0;
    if (COVARIANT) {
      ret_type = random() % s + 1;
      if (!base_present[s][ret_type]
          || !base_present[ret_type][ret_types[s][fn]])
        if (ret_types[s][fn]) {
          printf("  // Found one for s%d for s%d* fun%d.\n", s,
                 ret_types[s][fn], fn);
          ret_type = ret_types[s][fn];
        } else
          ret_type = s;
      else
        printf("  // Wow found one for s%d for fun%d.\n", s, fn);
      ret_types[s][fn] = ret_type;
    }
    if (ret_type) {
      g("  virtual s"); g(ret_type); g("* fun");
    } else
      g("  virtual void fun");
    g(fn); g("(char *t) { mix(\"vfn this offset\", (char *)this - t); mix(\"vfn uuid\", "); g(++uuid);
    if (ret_type)
      gl("); return 0; }");
    else
      gl("); }");
    final_override[s][fn] = s;
    old_func = fn;
  }

  // Add required overriders for correctness
  for (int i = 0; i < n_bases; ++i) {
    // For each base
    int base = bases[i];
    for (int fn = 0; fn < N_FUNCS*FUNCSPACING; ++fn) {
      // For each possible function
      int new_base = final_override[base][fn];
      if (new_base == 0)
        // If the base didn't have a final overrider, skip
        continue;

      int prev_base = final_override[s][fn];
      if (prev_base == s)
        // Skip functions defined in this class
        continue;

      // If we don't want to change the info, skip
      if (prev_base == new_base)
        continue;
      
      if (prev_base == 0) {
        // record the final override
        final_override[s][fn] = new_base;
        continue;
      }
        
      if (base_present[prev_base][new_base]) {
        // The previous base dominates the new base, no update necessary
        printf("  // No override for fun%d in s%d as s%d dominates s%d.\n",
               fn, s, prev_base, new_base);
        continue;
      }

      if (base_present[new_base][prev_base]) {
        // The new base dominates the old base, no override necessary
        printf("  // No override for fun%d in s%d as s%d dominates s%d.\n",
               fn, s, new_base, prev_base);
        // record the final override
        final_override[s][fn] = new_base;
        continue;
      }

      printf("  // Found we needed override for fun%d in s%d.\n", fn, s);

      // record the final override
      funcs[n_funcs++] = fn;
      if (n_funcs == (N_FUNCS*FUNCSPACING-1))
        abort();
      int ret_type = 0;
      if (COVARIANT) {
        if (!ret_types[s][fn]) {
          ret_types[s][fn] = ret_type = s;
        } else {
          ret_type = ret_types[s][fn];
          if (ret_type != s)
            printf("  // Calculated return type in s%d as s%d* fun%d.\n",
                   s, ret_type, fn);
        }
      }
      if (ret_type) {
        g("  virtual s"); g(ret_type); g("* fun");
      } else
        g("  virtual void fun");
      g(fn); g("(char *t) { mix(\"vfn this offset\", (char *)this - t); mix(\"vfn uuid\", "); g(++uuid);
      if (ret_type)
        gl("); return 0; }");
      else
        gl("); }");
      final_override[s][fn] = s;
    }
  }

  gl("public:");
  gl("  void calc(char *t) {");

  // mix in the type number
  g("    mix(\"type num\", "); g(s); gl(");");
  // mix in the size
  g("    mix(\"type size\", sizeof (s"); g(s); gl("));");
  // mix in the this offset
  gl("    mix(\"subobject offset\", (char *)this - t);");
  if (n_funcs)
    polymorphic = true;
  if (polymorphic) {
    // mix in offset to the complete object under construction
    gl("    mix(\"real top v current top\", t - (char *)dynamic_cast<void*>(this));");
  }

  /* check base layout and overrides */
  for (int i = 0; i < n_bases; ++i) {
    g("    calc_s"); g(bases[i]); gl("(t);");
  }

  if (polymorphic) {
    /* check dynamic_cast to each direct base */
    for (int i = 0; i < n_bases; ++i) {
      g("    if ((char *)dynamic_cast<s"); g(bases[i]); gl("*>(this))");
      g("      mix(\"base dyn cast\", t - (char *)dynamic_cast<s"); g(bases[i]); gl("*>(this));");
      g("    else mix(\"no dyncast\", "); g(++uuid); gl(");");
    }
  }

  /* check field layout */
  for (int i = 0; i < n_fields; ++i) {
    g("    mix(\"field offset\", (char *)&field"); g(i); gl(" - (char *)this);");
  }
  if (n_fields == 0) {
    g("    mix(\"no fields\", "); g(++uuid); gl(");");
  }

  /* check functions */
  for (int i = 0; i < n_funcs; ++i) {
    g("    fun"); g(funcs[i]); gl("(t);");
  }
  if (n_funcs == 0) {
    g("    mix(\"no funcs\", "); g(++uuid); gl(");");
  }

  gl("  }");

  // default ctor
  g("  s"); g(s); g("() ");
  first_base = true;
  for (int i = 0; i < n_bases; ++i) {
    if (first_base) {
      g(": ");
      first_base = false;
    } else
      g(", ");
    g("s"); g(bases[i]); g("((char *)this)");
  }
  gl(" { calc((char *)this); }");
  g("  ~s"); g(s); gl("() { calc((char *)this); }");

 // ctor with this to the complete object
  g("  s"); g(s); gl("(char *t) { calc(t); }");
  g("  void calc_s"); g(s); gl("(char *t) { calc(t); }");
  g("} a"); g(s); gl(";");
}

main(int argc, char **argv) {
  unsigned seed = 0;
  char state[16];
  if (argc > 1)
    seed = atol(argv[1]);

  initstate(seed, state, sizeof(state));
  gl("extern \"C\" int printf(const char *...);");
  gl("");
  gl("long long sum;");
  gl("void mix(const char *desc, long long i) {");
  // If this ever becomes too slow, we can remove this after we improve the
  // mixing function
  gl("  printf(\"%s: %lld\\n\", desc, i);");
  gl("  sum += ((sum ^ i) << 3) + (sum<<1) - i;");
  gl("}");
  gl("");
  // PARAM: Randomly size testcases or large testcases?
  int n_structs = /* random() % */ N_STRUCTS;
  for (int i = 1; i < n_structs; ++i)
    gs(i);
  gl("int main() {");
  gl("  printf(\"%llx\\n\", sum);");
  gl("}");
  return 0;
}
