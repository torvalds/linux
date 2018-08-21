// SPDX-License-Identifier: GPL-2.0+
/*
 * (C) Copyright 2008-2015 Fuzhou Rockchip Electronics Co., Ltd
 */

#include <errno.h>
#include <memory.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <sys/stat.h>
#include <time.h>

//#define DEBUG

static bool g_debug =
#ifdef DEBUG
	true;
#else
	false;
#endif //DEBUG

#define LOGE(fmt, args...)                                                     \
	fprintf(stderr, "E/%s(%d): " fmt "\n", __func__, __LINE__, ##args)
#define LOGD(fmt, args...)                                                     \
	do {                                                                   \
    if (g_debug) \
			fprintf(stderr, "D/%s(%d): " fmt "\n", __func__,       \
				__LINE__, ##args);                             \
} while (0)

//sync with ./board/rockchip/rk30xx/rkloader.c #define FDT_PATH
#define FDT_PATH                    "rk-kernel.dtb"
#define DTD_SUBFIX                  ".dtb"

#define DEFAULT_IMAGE_PATH          "resource.img"
#define DEFAULT_UNPACK_DIR          "out"
#define BLOCK_SIZE                  512

#define RESOURCE_PTN_HDR_SIZE       1
#define INDEX_TBL_ENTR_SIZE         1

#define RESOURCE_PTN_VERSION        0
#define INDEX_TBL_VERSION           0

#define RESOURCE_PTN_HDR_MAGIC      "RSCE"
typedef struct {
	char magic[4];		//tag, "RSCE"
	uint16_t resource_ptn_version;
	uint16_t index_tbl_version;
	uint8_t header_size;	//blocks, size of ptn header.
	uint8_t tbl_offset;	//blocks, offset of index table.
	uint8_t tbl_entry_size;	//blocks, size of index table's entry.
	uint32_t tbl_entry_num;	//numbers of index table's entry.
} resource_ptn_header;

#define INDEX_TBL_ENTR_TAG          "ENTR"
#define MAX_INDEX_ENTRY_PATH_LEN    256
typedef struct {
	char tag[4];		//tag, "ENTR"
	char path[MAX_INDEX_ENTRY_PATH_LEN];
	uint32_t content_offset;	//blocks, offset of resource content.
	uint32_t content_size;	//bytes, size of resource content.
} index_tbl_entry;

#define OPT_VERBOSE         "--verbose"
#define OPT_HELP            "--help"
#define OPT_VERSION         "--version"
#define OPT_PRINT           "--print"
#define OPT_PACK            "--pack"
#define OPT_UNPACK          "--unpack"
#define OPT_TEST_LOAD       "--test_load"
#define OPT_TEST_CHARGE     "--test_charge"
#define OPT_IMAGE           "--image="
#define OPT_ROOT            "--root="

#define VERSION             "2014-5-31 14:43:42"

typedef struct {
	char path[MAX_INDEX_ENTRY_PATH_LEN];
	uint32_t content_offset;	//blocks, offset of resource content.
	uint32_t content_size;		//bytes, size of resource content.
	void *load_addr;
} resource_content;

typedef struct {
	int max_level;
	int num;
	int delay;
	char prefix[MAX_INDEX_ENTRY_PATH_LEN];
} anim_level_conf;

#define DEF_CHARGE_DESC_PATH        "charge_anim_desc.txt"

#define OPT_CHARGE_ANIM_DELAY       "delay="
#define OPT_CHARGE_ANIM_LOOP_CUR    "only_current_level="
#define OPT_CHARGE_ANIM_LEVELS      "levels="
#define OPT_CHARGE_ANIM_LEVEL_CONF  "max_level="
#define OPT_CHARGE_ANIM_LEVEL_NUM   "num="
#define OPT_CHARGE_ANIM_LEVEL_PFX   "prefix="

static char image_path[MAX_INDEX_ENTRY_PATH_LEN] = "\0";

static int fix_blocks(size_t size)
{
	return (size + BLOCK_SIZE - 1) / BLOCK_SIZE;
}

static const char *fix_path(const char *path)
{
	if (!memcmp(path, "./", 2)) {
		return path + 2;
	}
	return path;
}

static uint16_t switch_short(uint16_t x)
{
	uint16_t val;
	uint8_t *p = (uint8_t *)(&x);

	val = (*p++ & 0xff) << 0;
	val |= (*p & 0xff) << 8;

	return val;
}

static uint32_t switch_int(uint32_t x)
{
	uint32_t val;
	uint8_t *p = (uint8_t *)(&x);

	val = (*p++ & 0xff) << 0;
	val |= (*p++ & 0xff) << 8;
	val |= (*p++ & 0xff) << 16;
	val |= (*p & 0xff) << 24;

	return val;
}

static void fix_header(resource_ptn_header *header)
{
	//switch for be.
	header->resource_ptn_version =
		switch_short(header->resource_ptn_version);
	header->index_tbl_version = switch_short(header->index_tbl_version);
	header->tbl_entry_num = switch_int(header->tbl_entry_num);
}

static void fix_entry(index_tbl_entry *entry)
{
	//switch for be.
	entry->content_offset = switch_int(entry->content_offset);
	entry->content_size = switch_int(entry->content_size);
}

static int inline get_ptn_offset(void)
{
	return 0;
}

static bool StorageWriteLba(int offset_block, void *data, int blocks)
{
	bool ret = false;
	FILE *file = fopen(image_path, "rb+");
	if (!file)
		goto end;
	int offset = offset_block * BLOCK_SIZE;
	fseek(file, offset, SEEK_SET);
	if (offset != ftell(file)) {
		LOGE("Failed to seek %s to %d!", image_path, offset);
		goto end;
	}
	if (!fwrite(data, blocks * BLOCK_SIZE, 1, file)) {
		LOGE("Failed to write %s!", image_path);
		goto end;
	}
	ret = true;
end:
	if (file)
		fclose(file);
	return ret;
}

static bool StorageReadLba(int offset_block, void *data, int blocks)
{
	bool ret = false;
	FILE *file = fopen(image_path, "rb");
	if (!file)
		goto end;
	int offset = offset_block * BLOCK_SIZE;
	fseek(file, offset, SEEK_SET);
	if (offset != ftell(file)) {
		goto end;
	}
	if (!fread(data, blocks * BLOCK_SIZE, 1, file)) {
		goto end;
	}
	ret = true;
end:
	if (file)
		fclose(file);
	return ret;
}

static bool write_data(int offset_block, void *data, size_t len)
{
	bool ret = false;
	if (!data)
		goto end;
	int blocks = len / BLOCK_SIZE;
	if (blocks && !StorageWriteLba(offset_block, data, blocks)) {
		goto end;
	}
	int left = len % BLOCK_SIZE;
	if (left) {
		char buf[BLOCK_SIZE] = "\0";
		memcpy(buf, data + blocks * BLOCK_SIZE, left);
		if (!StorageWriteLba(offset_block + blocks, buf, 1))
			goto end;
	}
	ret = true;
end:
	return ret;
}

/**********************load test************************/
static int load_file(const char *file_path, int offset_block, int blocks);

static int test_load(int argc, char **argv)
{
	if (argc < 1) {
		LOGE("Nothing to load!");
		return -1;
	}
	const char *file_path;
	int offset_block = 0;
	int blocks = 0;
	if (argc > 0) {
		file_path = (const char *)fix_path(argv[0]);
		argc--, argv++;
	}
	if (argc > 0) {
		offset_block = atoi(argv[0]);
		argc--, argv++;
	}
	if (argc > 0) {
		blocks = atoi(argv[0]);
	}
	return load_file(file_path, offset_block, blocks);
}

static void free_content(resource_content *content)
{
	if (content->load_addr) {
		free(content->load_addr);
		content->load_addr = 0;
	}
}

static void tests_dump_file(const char *path, void *data, int len)
{
	FILE *file = fopen(path, "wb");
	if (!file)
		return;
	fwrite(data, len, 1, file);
	fclose(file);
}

static bool load_content(resource_content *content)
{
	if (content->load_addr)
		return true;
	int blocks = fix_blocks(content->content_size);
	content->load_addr = malloc(blocks * BLOCK_SIZE);
	if (!content->load_addr)
		return false;
	if (!StorageReadLba(get_ptn_offset() + content->content_offset,
			    content->load_addr, blocks)) {
		free_content(content);
		return false;
	}

	tests_dump_file(content->path, content->load_addr,
			content->content_size);
	return true;
}

static bool load_content_data(resource_content *content, int offset_block,
			      void *data, int blocks)
{
	if (!StorageReadLba(get_ptn_offset() + content->content_offset +
				    offset_block,
			    data, blocks)) {
		return false;
	}
	tests_dump_file(content->path, data, blocks * BLOCK_SIZE);
	return true;
}

static bool get_entry(const char *file_path, index_tbl_entry *entry)
{
	bool ret = false;
	char buf[BLOCK_SIZE];
	resource_ptn_header header;
	if (!StorageReadLba(get_ptn_offset(), buf, 1)) {
		LOGE("Failed to read header!");
		goto end;
	}
	memcpy(&header, buf, sizeof(header));

	if (memcmp(header.magic, RESOURCE_PTN_HDR_MAGIC, sizeof(header.magic))) {
		LOGE("Not a resource image(%s)!", image_path);
		goto end;
	}
	//test on pc, switch for be.
	fix_header(&header);

	//TODO: support header_size & tbl_entry_size
	if (header.resource_ptn_version != RESOURCE_PTN_VERSION ||
	    header.header_size != RESOURCE_PTN_HDR_SIZE ||
	    header.index_tbl_version != INDEX_TBL_VERSION ||
	    header.tbl_entry_size != INDEX_TBL_ENTR_SIZE) {
		LOGE("Not supported in this version!");
		goto end;
	}

	int i;
	for (i = 0; i < header.tbl_entry_num; i++) {
		//TODO: support tbl_entry_size
		if (!StorageReadLba(get_ptn_offset() + header.header_size +
					    i * header.tbl_entry_size,
				    buf, 1)) {
			LOGE("Failed to read index entry:%d!", i);
			goto end;
		}
		memcpy(entry, buf, sizeof(*entry));

		if (memcmp(entry->tag, INDEX_TBL_ENTR_TAG, sizeof(entry->tag))) {
			LOGE("Something wrong with index entry:%d!", i);
			goto end;
		}

		if (!strncmp(entry->path, file_path, sizeof(entry->path)))
			break;
	}
	if (i == header.tbl_entry_num) {
		LOGE("Cannot find %s!", file_path);
		goto end;
	}
	//test on pc, switch for be.
	fix_entry(entry);

	printf("Found entry:\n\tpath:%s\n\toffset:%d\tsize:%d\n",
	       entry->path, entry->content_offset, entry->content_size);

	ret = true;
end:
	return ret;
}

static bool get_content(resource_content *content)
{
	bool ret = false;
	index_tbl_entry entry;
	if (!get_entry(content->path, &entry))
		goto end;
	content->content_offset = entry.content_offset;
	content->content_size = entry.content_size;
	ret = true;
end:
	return ret;
}

static int load_file(const char *file_path, int offset_block, int blocks)
{
	printf("Try to load:%s", file_path);
	if (blocks) {
		printf(", offset block:%d, blocks:%d\n", offset_block, blocks);
	} else {
		printf("\n");
	}
	bool ret = false;
	resource_content content;
	snprintf(content.path, sizeof(content.path), "%s", file_path);
	content.load_addr = 0;
	if (!get_content(&content)) {
		goto end;
	}
	if (!blocks) {
		if (!load_content(&content)) {
			goto end;
		}
	} else {
		void *data = malloc(blocks * BLOCK_SIZE);
		if (!data)
			goto end;
		if (!load_content_data(&content, offset_block, data, blocks)) {
			goto end;
		}
	}
	ret = true;
end:
	free_content(&content);
	return ret;
}

/**********************load test end************************/
/**********************anim test************************/

static bool parse_level_conf(const char *arg, anim_level_conf *level_conf)
{
	memset(level_conf, 0, sizeof(anim_level_conf));
	char *buf = NULL;
	buf = strstr(arg, OPT_CHARGE_ANIM_LEVEL_CONF);
	if (buf) {
		level_conf->max_level =
			atoi(buf + strlen(OPT_CHARGE_ANIM_LEVEL_CONF));
	} else {
		LOGE("Not found:%s", OPT_CHARGE_ANIM_LEVEL_CONF);
		return false;
	}
	buf = strstr(arg, OPT_CHARGE_ANIM_LEVEL_NUM);
	if (buf) {
		level_conf->num = atoi(buf + strlen(OPT_CHARGE_ANIM_LEVEL_NUM));
		if (level_conf->num <= 0) {
			return false;
		}
	} else {
		LOGE("Not found:%s", OPT_CHARGE_ANIM_LEVEL_NUM);
		return false;
	}
	buf = strstr(arg, OPT_CHARGE_ANIM_DELAY);
	if (buf) {
		level_conf->delay = atoi(buf + strlen(OPT_CHARGE_ANIM_DELAY));
	}
	buf = strstr(arg, OPT_CHARGE_ANIM_LEVEL_PFX);
	if (buf) {
		snprintf(level_conf->prefix, sizeof(level_conf->prefix),
			 "%s", buf + strlen(OPT_CHARGE_ANIM_LEVEL_PFX));
	} else {
		LOGE("Not found:%s", OPT_CHARGE_ANIM_LEVEL_PFX);
		return false;
	}

	LOGD("Found conf:\nmax_level:%d, num:%d, delay:%d, prefix:%s",
	     level_conf->max_level, level_conf->num,
	     level_conf->delay, level_conf->prefix);
	return true;
}

static int test_charge(int argc, char **argv)
{
	const char *desc;
	if (argc > 0) {
		desc = argv[0];
	} else {
		desc = DEF_CHARGE_DESC_PATH;
	}

	resource_content content;
	snprintf(content.path, sizeof(content.path), "%s", desc);
	content.load_addr = 0;
	if (!get_content(&content)) {
		goto end;
	}
	if (!load_content(&content)) {
		goto end;
	}

	char *buf = (char *)content.load_addr;
	char *end = buf + content.content_size - 1;
	*end = '\0';
	LOGD("desc:\n%s", buf);

	int pos = 0;
	while (1) {
		char *line = (char *)memchr(buf + pos, '\n', strlen(buf + pos));
		if (!line)
			break;
		*line = '\0';
		LOGD("splite:%s", buf + pos);
		pos += (strlen(buf + pos) + 1);
	}

	int delay = 900;
	int only_current_level = false;
	anim_level_conf *level_confs = NULL;
	int level_conf_pos = 0;
	int level_conf_num = 0;

	while (true) {
		if (buf >= end)
			break;
		const char *arg = buf;
		buf += (strlen(buf) + 1);

		LOGD("parse arg:%s", arg);
		if (!memcmp(arg, OPT_CHARGE_ANIM_LEVEL_CONF,
			    strlen(OPT_CHARGE_ANIM_LEVEL_CONF))) {
			if (!level_confs) {
				LOGE("Found level conf before levels!");
				goto end;
			}
			if (level_conf_pos >= level_conf_num) {
				LOGE("Too many level confs!(%d >= %d)",
				     level_conf_pos, level_conf_num);
				goto end;
			}
			if (!parse_level_conf(arg,
					      level_confs + level_conf_pos)) {
				LOGE("Failed to parse level conf:%s", arg);
				goto end;
			}
			level_conf_pos++;
		} else if (!memcmp(arg, OPT_CHARGE_ANIM_DELAY,
				   strlen(OPT_CHARGE_ANIM_DELAY))) {
			delay = atoi(arg + strlen(OPT_CHARGE_ANIM_DELAY));
			LOGD("Found delay:%d", delay);
		} else if (!memcmp(arg, OPT_CHARGE_ANIM_LOOP_CUR,
				   strlen(OPT_CHARGE_ANIM_LOOP_CUR))) {
			only_current_level =
				!memcmp(arg + strlen(OPT_CHARGE_ANIM_LOOP_CUR),
					"true", 4);
			LOGD("Found only_current_level:%d", only_current_level);
		} else if (!memcmp(arg, OPT_CHARGE_ANIM_LEVELS,
				   strlen(OPT_CHARGE_ANIM_LEVELS))) {
			if (level_conf_num) {
				goto end;
			}
			level_conf_num =
				atoi(arg + strlen(OPT_CHARGE_ANIM_LEVELS));
			if (!level_conf_num) {
				goto end;
			}
			level_confs =
			    (anim_level_conf *) malloc(level_conf_num *
						       sizeof(anim_level_conf));
			LOGD("Found levels:%d", level_conf_num);
		} else {
			LOGE("Unknown arg:%s", arg);
			goto end;
		}
	}

	if (level_conf_pos != level_conf_num || !level_conf_num) {
		LOGE("Something wrong with level confs!");
		goto end;
	}

	int i = 0, j = 0;
	for (i = 0; i < level_conf_num; i++) {
		if (!level_confs[i].delay) {
			level_confs[i].delay = delay;
		}
		if (!level_confs[i].delay) {
			LOGE("Missing delay in level conf:%d", i);
			goto end;
		}
		for (j = 0; j < i; j++) {
			if (level_confs[j].max_level ==
			    level_confs[i].max_level) {
				LOGE("Dup level conf:%d", i);
				goto end;
			}
			if (level_confs[j].max_level > level_confs[i].max_level) {
				anim_level_conf conf = level_confs[i];
				memmove(level_confs + j + 1, level_confs + j,
					(i - j) * sizeof(anim_level_conf));
				level_confs[j] = conf;
			}
		}
	}

	printf("Parse anim desc(%s):\n", desc);
	printf("only_current_level=%d\n", only_current_level);
	printf("level conf:\n");
	for (i = 0; i < level_conf_num; i++) {
		printf("\tmax=%d, delay=%d, num=%d, prefix=%s\n",
		       level_confs[i].max_level, level_confs[i].delay,
		       level_confs[i].num, level_confs[i].prefix);
	}

end:
	free_content(&content);
	return 0;
}

/**********************anim test end************************/
/**********************append file************************/

static const char *PROG = NULL;
static resource_ptn_header header;
static bool just_print = false;
static char root_path[MAX_INDEX_ENTRY_PATH_LEN] = "\0";

static void version(void)
{
	printf("%s (cjf@rock-chips.com)\t" VERSION "\n", PROG);
}

static void usage(void)
{
	printf("Usage: %s [options] [FILES]\n", PROG);
	printf("Tools for Rockchip's resource image.\n");
	version();
	printf("Options:\n");
	printf("\t" OPT_PACK "\t\t\tPack image from given files.\n");
	printf("\t" OPT_UNPACK "\t\tUnpack given image to current dir.\n");
	printf("\t" OPT_IMAGE "path" "\t\tSpecify input/output image path.\n");
	printf("\t" OPT_PRINT "\t\t\tJust print informations.\n");
	printf("\t" OPT_VERBOSE "\t\tDisplay more runtime informations.\n");
	printf("\t" OPT_HELP "\t\t\tDisplay this information.\n");
	printf("\t" OPT_VERSION "\t\tDisplay version information.\n");
	printf("\t" OPT_ROOT "path" "\t\tSpecify resources' root dir.\n");
}

static int pack_image(int file_num, const char **files);
static int unpack_image(const char *unpack_dir);

enum ACTION {
	ACTION_PACK,
	ACTION_UNPACK,
	ACTION_TEST_LOAD,
	ACTION_TEST_CHARGE,
};

int main(int argc, char **argv)
{
	PROG = fix_path(argv[0]);

	enum ACTION action = ACTION_PACK;

	argc--, argv++;
	while (argc > 0 && argv[0][0] == '-') {
		//it's a opt arg.
		const char *arg = argv[0];
		argc--, argv++;
		if (!strcmp(OPT_VERBOSE, arg)) {
			g_debug = true;
		} else if (!strcmp(OPT_HELP, arg)) {
			usage();
			return 0;
		} else if (!strcmp(OPT_VERSION, arg)) {
			version();
			return 0;
		} else if (!strcmp(OPT_PRINT, arg)) {
			just_print = true;
		} else if (!strcmp(OPT_PACK, arg)) {
			action = ACTION_PACK;
		} else if (!strcmp(OPT_UNPACK, arg)) {
			action = ACTION_UNPACK;
		} else if (!strcmp(OPT_TEST_LOAD, arg)) {
			action = ACTION_TEST_LOAD;
		} else if (!strcmp(OPT_TEST_CHARGE, arg)) {
			action = ACTION_TEST_CHARGE;
		} else if (!memcmp(OPT_IMAGE, arg, strlen(OPT_IMAGE))) {
			snprintf(image_path, sizeof(image_path),
				 "%s", arg + strlen(OPT_IMAGE));
		} else if (!memcmp(OPT_ROOT, arg, strlen(OPT_ROOT))) {
			snprintf(root_path, sizeof(root_path),
				 "%s", arg + strlen(OPT_ROOT));
		} else {
			LOGE("Unknown opt:%s", arg);
			usage();
			return -1;
		}
	}

	if (!image_path[0]) {
		snprintf(image_path, sizeof(image_path), "%s",
			 DEFAULT_IMAGE_PATH);
	}

	switch (action) {
	case ACTION_PACK:
		{
			int file_num = argc;
			const char **files = (const char **)argv;
			if (!file_num) {
				LOGE("No file to pack!");
				return 0;
			}
			LOGD("try to pack %d files.", file_num);
			return pack_image(file_num, files);
		}
	case ACTION_UNPACK:
		{
			return unpack_image(argc >
					    0 ? argv[0] : DEFAULT_UNPACK_DIR);
		}
	case ACTION_TEST_LOAD:
		{
			return test_load(argc, argv);
		}
	case ACTION_TEST_CHARGE:
		{
			return test_charge(argc, argv);
		}
	}
	//not reach here.
	return -1;
}

/************unpack code****************/
static bool mkdirs(char *path)
{
	char *tmp = path;
	char *pos = NULL;
	char buf[MAX_INDEX_ENTRY_PATH_LEN];
	bool ret = true;
	while ((pos = memchr(tmp, '/', strlen(tmp)))) {
		strcpy(buf, path);
		buf[pos - path] = '\0';
		tmp = pos + 1;
		LOGD("mkdir:%s", buf);
		if (!mkdir(buf, 0755)) {
			ret = false;
		}
	}
	if (!ret)
		LOGD("Failed to mkdir(%s)!", path);
	return ret;
}

static bool dump_file(FILE *file, const char *unpack_dir, index_tbl_entry entry)
{
	LOGD("try to dump entry:%s", entry.path);
	bool ret = false;
	FILE *out_file = NULL;
	long int pos = 0;
	char path[MAX_INDEX_ENTRY_PATH_LEN * 2 + 1];
	if (just_print) {
		ret = true;
		goto done;
	}

	pos = ftell(file);
	snprintf(path, sizeof(path), "%s/%s", unpack_dir, entry.path);
	mkdirs(path);
	out_file = fopen(path, "wb");
	if (!out_file) {
		LOGE("Failed to create:%s", path);
		goto end;
	}
	long int offset = entry.content_offset * BLOCK_SIZE;
	fseek(file, offset, SEEK_SET);
	if (offset != ftell(file)) {
		LOGE("Failed to read content:%s", entry.path);
		goto end;
	}
	char buf[BLOCK_SIZE];
	int n;
	int len = entry.content_size;
	while (len > 0) {
		n = len > BLOCK_SIZE ? BLOCK_SIZE : len;
		if (!fread(buf, n, 1, file)) {
			LOGE("Failed to read content:%s", entry.path);
			goto end;
		}
		if (!fwrite(buf, n, 1, out_file)) {
			LOGE("Failed to write:%s", entry.path);
			goto end;
		}
		len -= n;
	}
done:
	ret = true;
end:
	if (out_file)
		fclose(out_file);
	if (pos)
		fseek(file, pos, SEEK_SET);
	return ret;
}

static int unpack_image(const char *dir)
{
	FILE *image_file = NULL;
	bool ret = false;
	char unpack_dir[MAX_INDEX_ENTRY_PATH_LEN];
	if (just_print)
		dir = ".";
	snprintf(unpack_dir, sizeof(unpack_dir), "%s", dir);
	if (!strlen(unpack_dir)) {
		goto end;
	} else if (unpack_dir[strlen(unpack_dir) - 1] == '/') {
		unpack_dir[strlen(unpack_dir) - 1] = '\0';
	}

	mkdir(unpack_dir, 0755);
	image_file = fopen(image_path, "rb");
	char buf[BLOCK_SIZE];
	if (!image_file) {
		LOGE("Failed to open:%s", image_path);
		goto end;
	}
	if (!fread(buf, BLOCK_SIZE, 1, image_file)) {
		LOGE("Failed to read header!");
		goto end;
	}
	memcpy(&header, buf, sizeof(header));

	if (memcmp(header.magic, RESOURCE_PTN_HDR_MAGIC, sizeof(header.magic))) {
		LOGE("Not a resource image(%s)!", image_path);
		goto end;
	}
	//switch for be.
	fix_header(&header);

	printf("Dump header:\n");
	printf("partition version:%d.%d\n",
	       header.resource_ptn_version, header.index_tbl_version);
	printf("header size:%d\n", header.header_size);
	printf("index tbl:\n\toffset:%d\tentry size:%d\tentry num:%d\n",
	       header.tbl_offset, header.tbl_entry_size, header.tbl_entry_num);

	//TODO: support header_size & tbl_entry_size
	if (header.resource_ptn_version != RESOURCE_PTN_VERSION ||
	    header.header_size != RESOURCE_PTN_HDR_SIZE ||
	    header.index_tbl_version != INDEX_TBL_VERSION ||
	    header.tbl_entry_size != INDEX_TBL_ENTR_SIZE) {
		LOGE("Not supported in this version!");
		goto end;
	}

	printf("Dump Index table:\n");
	index_tbl_entry entry;
	int i;
	for (i = 0; i < header.tbl_entry_num; i++) {
		//TODO: support tbl_entry_size
		if (!fread(buf, BLOCK_SIZE, 1, image_file)) {
			LOGE("Failed to read index entry:%d!", i);
			goto end;
		}
		memcpy(&entry, buf, sizeof(entry));

		if (memcmp(entry.tag, INDEX_TBL_ENTR_TAG, sizeof(entry.tag))) {
			LOGE("Something wrong with index entry:%d!", i);
			goto end;
		}
		//switch for be.
		fix_entry(&entry);

		printf("entry(%d):\n\tpath:%s\n\toffset:%d\tsize:%d\n",
		       i, entry.path, entry.content_offset, entry.content_size);
		if (!dump_file(image_file, unpack_dir, entry)) {
			goto end;
		}
	}
	printf("Unack %s to %s successed!\n", image_path, unpack_dir);
	ret = true;
end:
	if (image_file)
		fclose(image_file);
	return ret ? 0 : -1;
}

/************unpack code end****************/
/************pack code****************/

static inline size_t get_file_size(const char *path)
{
	LOGD("try to get size(%s)...", path);
	struct stat st;
	if (stat(path, &st) < 0) {
		LOGE("Failed to get size:%s", path);
		return -1;
	}
	LOGD("path:%s, size:%ld", path, st.st_size);
	return st.st_size;
}

static int write_file(int offset_block, const char *src_path)
{
	LOGD("try to write file(%s) to offset:%d...", src_path, offset_block);
	char buf[BLOCK_SIZE];
	int ret = -1;
	size_t file_size;
	int blocks;
	FILE *src_file = fopen(src_path, "rb");
	if (!src_file) {
		LOGE("Failed to open:%s", src_path);
		goto end;
	}

	file_size = get_file_size(src_path);
	if (file_size < 0) {
		goto end;
	}
	blocks = fix_blocks(file_size);

	int i;
	for (i = 0; i < blocks; i++) {
		memset(buf, 0, sizeof(buf));
		if (!fread(buf, 1, BLOCK_SIZE, src_file)) {
			LOGE("Failed to read:%s", src_path);
			goto end;
		}
		if (!write_data(offset_block + i, buf, BLOCK_SIZE)) {
			goto end;
		}
	}
	ret = blocks;
end:
	if (src_file)
		fclose(src_file);
	return ret;
}

static bool write_header(const int file_num)
{
	LOGD("try to write header...");
	memcpy(header.magic, RESOURCE_PTN_HDR_MAGIC, sizeof(header.magic));
	header.resource_ptn_version = RESOURCE_PTN_VERSION;
	header.index_tbl_version = INDEX_TBL_VERSION;
	header.header_size = RESOURCE_PTN_HDR_SIZE;
	header.tbl_offset = header.header_size;
	header.tbl_entry_size = INDEX_TBL_ENTR_SIZE;
	header.tbl_entry_num = file_num;

	//switch for le.
	resource_ptn_header hdr = header;
	fix_header(&hdr);
	return write_data(0, &hdr, sizeof(hdr));
}

static bool write_index_tbl(const int file_num, const char **files)
{
	LOGD("try to write index table...");
	bool ret = false;
	bool foundFdt = false;
	int offset = header.header_size +
		     header.tbl_entry_size * header.tbl_entry_num;
	index_tbl_entry entry;
	memcpy(entry.tag, INDEX_TBL_ENTR_TAG, sizeof(entry.tag));
	int i;
	for (i = 0; i < file_num; i++) {
		size_t file_size = get_file_size(files[i]);
		if (file_size < 0)
			goto end;
		entry.content_size = file_size;
		entry.content_offset = offset;

		if (write_file(offset, files[i]) < 0)
			goto end;

		LOGD("try to write index entry(%s)...", files[i]);

		//switch for le.
		fix_entry(&entry);
		memset(entry.path, 0, sizeof(entry.path));
		const char *path = files[i];
		if (root_path[0]) {
			if (!strncmp(path, root_path, strlen(root_path))) {
				path += strlen(root_path);
				if (path[0] == '/')
					path++;
			}
		}
		path = fix_path(path);
		if (!strcmp(files[i] + strlen(files[i]) - strlen(DTD_SUBFIX),
			    DTD_SUBFIX)) {
			if (!foundFdt) {
				//use default path.
				LOGD("mod fdt path:%s -> %s...", files[i],
				     FDT_PATH);
				path = FDT_PATH;
				foundFdt = true;
			}
		}
		snprintf(entry.path, sizeof(entry.path), "%s", path);
		offset += fix_blocks(file_size);
		if (!write_data(header.header_size + i * header.tbl_entry_size,
				&entry, sizeof(entry)))
			goto end;
	}
	ret = true;
end:
	return ret;
}

static int pack_image(int file_num, const char **files)
{
	bool ret = false;
	FILE *image_file = fopen(image_path, "wb");
	if (!image_file) {
		LOGE("Failed to create:%s", image_path);
		goto end;
	}
	fclose(image_file);

	//prepare files
	int i = 0;
	int pos = 0;
	const char *tmp;
	for (i = 0; i < file_num; i++) {
		if (!strcmp(files[i] + strlen(files[i]) - strlen(DTD_SUBFIX),
			    DTD_SUBFIX)) {
			//dtb files for kernel.
			tmp = files[pos];
			files[pos] = files[i];
			files[i] = tmp;
			pos++;
		} else if (!strcmp(fix_path(image_path), fix_path(files[i]))) {
			//not to pack image itself!
			tmp = files[file_num - 1];
			files[file_num - 1] = files[i];
			files[i] = tmp;
			file_num--;
		}
	}

	if (!write_header(file_num)) {
		LOGE("Failed to write header!");
		goto end;
	}
	if (!write_index_tbl(file_num, files)) {
		LOGE("Failed to write index table!");
		goto end;
	}
	printf("Pack to %s successed!\n", image_path);
	ret = true;
end:
	return ret ? 0 : -1;
}

/************pack code end****************/
