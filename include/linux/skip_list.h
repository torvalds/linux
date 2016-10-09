#ifndef _LINUX_SKIP_LISTS_H
#define _LINUX_SKIP_LISTS_H
typedef u64 keyType;
typedef void *valueType;

typedef struct nodeStructure skiplist_node;

struct nodeStructure {
	int level;	/* Levels in this structure */
	keyType key;
	valueType value;
	skiplist_node *next[8];
	skiplist_node *prev[8];
};

typedef struct listStructure {
	int entries;
	int level;	/* Maximum level of the list
			(1 more than the number of levels in the list) */
	skiplist_node *header; /* pointer to header */
} skiplist;

void skiplist_init(skiplist_node *slnode);
skiplist *new_skiplist(skiplist_node *slnode);
void free_skiplist(skiplist *l);
void skiplist_node_init(skiplist_node *node);
void skiplist_insert(skiplist *l, skiplist_node *node, keyType key, valueType value, unsigned int randseed);
void skiplist_delete(skiplist *l, skiplist_node *node);

static inline bool skiplist_node_empty(skiplist_node *node) {
	return (!node->next[0]);
}
#endif /* _LINUX_SKIP_LISTS_H */
