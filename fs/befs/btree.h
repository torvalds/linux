/*
 * btree.h
 * 
 */

int befs_btree_find(struct super_block *sb, const befs_data_stream *ds,
		    const char *key, befs_off_t *value);

int befs_btree_read(struct super_block *sb, const befs_data_stream *ds,
		    loff_t key_no, size_t bufsize, char *keybuf,
		    size_t *keysize, befs_off_t *value);
