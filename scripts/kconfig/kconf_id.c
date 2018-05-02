
static struct kconf_id kconf_id_array[] = {
	{ "mainmenu",		T_MAINMENU,		TF_COMMAND },
	{ "menu",		T_MENU,			TF_COMMAND },
	{ "endmenu",		T_ENDMENU,		TF_COMMAND },
	{ "source",		T_SOURCE,		TF_COMMAND },
	{ "choice",		T_CHOICE,		TF_COMMAND },
	{ "endchoice",		T_ENDCHOICE,		TF_COMMAND },
	{ "comment",		T_COMMENT,		TF_COMMAND },
	{ "config",		T_CONFIG,		TF_COMMAND },
	{ "menuconfig",		T_MENUCONFIG,		TF_COMMAND },
	{ "help",		T_HELP,			TF_COMMAND },
	{ "---help---",		T_HELP,			TF_COMMAND },
	{ "if",			T_IF,			TF_COMMAND|TF_PARAM },
	{ "endif",		T_ENDIF,		TF_COMMAND },
	{ "depends",		T_DEPENDS,		TF_COMMAND },
	{ "optional",		T_OPTIONAL,		TF_COMMAND },
	{ "default",		T_DEFAULT,		TF_COMMAND, S_UNKNOWN },
	{ "prompt",		T_PROMPT,		TF_COMMAND },
	{ "tristate",		T_TYPE,			TF_COMMAND, S_TRISTATE },
	{ "def_tristate",	T_DEFAULT,		TF_COMMAND, S_TRISTATE },
	{ "bool",		T_TYPE,			TF_COMMAND, S_BOOLEAN },
	{ "def_bool",		T_DEFAULT,		TF_COMMAND, S_BOOLEAN },
	{ "int",		T_TYPE,			TF_COMMAND, S_INT },
	{ "hex",		T_TYPE,			TF_COMMAND, S_HEX },
	{ "string",		T_TYPE,			TF_COMMAND, S_STRING },
	{ "select",		T_SELECT,		TF_COMMAND },
	{ "imply",		T_IMPLY,		TF_COMMAND },
	{ "range",		T_RANGE,		TF_COMMAND },
	{ "visible",		T_VISIBLE,		TF_COMMAND },
	{ "option",		T_OPTION,		TF_COMMAND },
	{ "on",			T_ON,			TF_PARAM },
	{ "modules",		T_OPT_MODULES,		TF_OPTION },
	{ "defconfig_list",	T_OPT_DEFCONFIG_LIST,	TF_OPTION },
	{ "env",		T_OPT_ENV,		TF_OPTION },
	{ "allnoconfig_y",	T_OPT_ALLNOCONFIG_Y,	TF_OPTION },
};

#define KCONF_ID_ARRAY_SIZE (sizeof(kconf_id_array)/sizeof(struct kconf_id))

static const struct kconf_id *kconf_id_lookup(register const char *str, register unsigned int len)
{
	int i;

	for (i = 0; i < KCONF_ID_ARRAY_SIZE; i++) {
		struct kconf_id *id = kconf_id_array+i;
		int l = strlen(id->name);

		if (len == l && !memcmp(str, id->name, len))
			return id;
	}
	return NULL;
}
