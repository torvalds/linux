/* It is currently impossible to switch between PA32 and PA64 based on a
   runtime compiler switch.  So we might as well lose the overhead with
   checking for TARGET_64BIT.  */
#define TARGET_64BIT 1
#undef TARGET_PA_11
#define TARGET_PA_11 1
#undef TARGET_PA_20
#define TARGET_PA_20 1
