#define OBJECT_FORMAT_ELF

#define LOCAL_LABEL_PREFIX "L$"

#undef ASM_GENERATE_INTERNAL_LABEL
#define ASM_GENERATE_INTERNAL_LABEL(LABEL, PREFIX, NUM)		\
     sprintf (LABEL, "*%s%s$%d", LOCAL_LABEL_PREFIX, PREFIX, (int) NUM)

#undef  STARTFILE_SPEC
#define STARTFILE_SPEC	"%{!shared: crt0%O%s} crti%O%s crtbegin%O%s crtlibid%O%s"

#undef  ENDFILE_SPEC
#define ENDFILE_SPEC	"crtend%O%s crtn%O%s"

#undef USER_LABEL_PREFIX
#define USER_LABEL_PREFIX "_"

#ifdef __BFIN_FDPIC__
#define CRT_CALL_STATIC_FUNCTION(SECTION_OP, FUNC)	\
asm (SECTION_OP); \
asm ("P3 = [SP + 20];\n\tcall " USER_LABEL_PREFIX #FUNC ";"); \
asm (TEXT_SECTION_ASM_OP);
#endif

#define NO_IMPLICIT_EXTERN_C
