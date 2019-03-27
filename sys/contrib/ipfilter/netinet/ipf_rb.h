/*
 * Copyright (C) 2012 by Darren Reed.
 *
 * See the IPFILTER.LICENCE file for details on licencing.
 *
 */
typedef enum rbcolour_e {
	C_BLACK = 0,
	C_RED = 1
} rbcolour_t;

#define	RBI_LINK(_n, _t)							\
	struct _n##_rb_link {						\
		struct _t	*left;					\
		struct _t	*right;					\
		struct _t	*parent;				\
		rbcolour_t	colour;					\
	}

#define	RBI_HEAD(_n, _t)						\
struct _n##_rb_head {							\
	struct _t	top;						\
	int		count;						\
	int		(* compare)(struct _t *, struct _t *);		\
}

#define	RBI_CODE(_n, _t, _f, _cmp)					\
									\
typedef	void	(*_n##_rb_walker_t)(_t *, void *);			\
									\
_t *	_n##_rb_delete(struct _n##_rb_head *, _t *);			\
void	_n##_rb_init(struct _n##_rb_head *);				\
void	_n##_rb_insert(struct _n##_rb_head *, _t *);			\
_t *	_n##_rb_search(struct _n##_rb_head *, void *);			\
void	_n##_rb_walktree(struct _n##_rb_head *, _n##_rb_walker_t, void *);\
									\
static void								\
rotate_left(struct _n##_rb_head *head, _t *node)			\
{									\
	_t *parent, *tmp1, *tmp2;					\
									\
	parent = node->_f.parent;					\
	tmp1 = node->_f.right;						\
	tmp2 = tmp1->_f.left;						\
	node->_f.right = tmp2;						\
	if (tmp2 != & _n##_rb_zero)					\
		tmp2->_f.parent = node;					\
	if (parent == & _n##_rb_zero)					\
		head->top._f.right = tmp1;				\
	else if (parent->_f.right == node)				\
		parent->_f.right = tmp1;				\
	else								\
		parent->_f.left = tmp1;					\
	tmp1->_f.left = node;						\
	tmp1->_f.parent = parent;					\
	node->_f.parent = tmp1;						\
}									\
									\
static void								\
rotate_right(struct _n##_rb_head *head, _t *node)			\
{									\
	_t *parent, *tmp1, *tmp2;					\
									\
	parent = node->_f.parent;					\
	tmp1 = node->_f.left;						\
	tmp2 = tmp1->_f.right;						\
	node->_f.left = tmp2;						\
	if (tmp2 != &_n##_rb_zero)					\
		tmp2->_f.parent = node;					\
	if (parent == &_n##_rb_zero)					\
		head->top._f.right = tmp1;				\
	else if (parent->_f.right == node)				\
		parent->_f.right = tmp1;				\
	else								\
		parent->_f.left = tmp1;					\
	tmp1->_f.right = node;						\
	tmp1->_f.parent = parent;					\
	node->_f.parent = tmp1;						\
}									\
									\
void									\
_n##_rb_insert(struct _n##_rb_head *head, _t *node)			\
{									\
	_t *n, *parent, **p, *tmp1, *gparent;				\
									\
	parent = &head->top;						\
	node->_f.left = &_n##_rb_zero;					\
	node->_f.right = &_n##_rb_zero;					\
	p = &head->top._f.right;					\
	while ((n = *p) != &_n##_rb_zero) {				\
		if (_cmp(node, n) < 0)					\
			p = &n->_f.left;				\
		else							\
			p = &n->_f.right;				\
		parent = n;						\
	}								\
	*p = node;							\
	node->_f.colour = C_RED;					\
	node->_f.parent = parent;					\
									\
	while ((node != &_n##_rb_zero) && (parent->_f.colour == C_RED)){\
		gparent = parent->_f.parent;				\
		if (parent == gparent->_f.left) {			\
			tmp1 = gparent->_f.right;			\
			if (tmp1->_f.colour == C_RED) {			\
				parent->_f.colour = C_BLACK;		\
				tmp1->_f.colour = C_BLACK;		\
				gparent->_f.colour = C_RED;		\
				node = gparent;				\
			} else {					\
				if (node == parent->_f.right) {		\
					node = parent;			\
					rotate_left(head, node);	\
					parent = node->_f.parent;	\
				}					\
				parent->_f.colour = C_BLACK;		\
				gparent->_f.colour = C_RED;		\
				rotate_right(head, gparent);		\
			}						\
		} else {						\
			tmp1 = gparent->_f.left;			\
			if (tmp1->_f.colour == C_RED) {			\
				parent->_f.colour = C_BLACK;		\
				tmp1->_f.colour = C_BLACK;		\
				gparent->_f.colour = C_RED;		\
				node = gparent;				\
			} else {					\
				if (node == parent->_f.left) {		\
					node = parent;			\
					rotate_right(head, node);	\
					parent = node->_f.parent;	\
				}					\
				parent->_f.colour = C_BLACK;		\
				gparent->_f.colour = C_RED;		\
				rotate_left(head, parent->_f.parent);	\
			}						\
		}							\
		parent = node->_f.parent;				\
	}								\
	head->top._f.right->_f.colour = C_BLACK;			\
	head->count++;						\
}									\
									\
static void								\
deleteblack(struct _n##_rb_head *head, _t *parent, _t *node)		\
{									\
	_t *tmp;							\
									\
	while ((node == &_n##_rb_zero || node->_f.colour == C_BLACK) &&	\
	       node != &head->top) {					\
		if (parent->_f.left == node) {				\
			tmp = parent->_f.right;				\
			if (tmp->_f.colour == C_RED) {			\
				tmp->_f.colour = C_BLACK;		\
				parent->_f.colour = C_RED;		\
				rotate_left(head, parent);		\
				tmp = parent->_f.right;			\
			}						\
			if ((tmp->_f.left == &_n##_rb_zero ||		\
			     tmp->_f.left->_f.colour == C_BLACK) &&	\
			    (tmp->_f.right == &_n##_rb_zero ||		\
			     tmp->_f.right->_f.colour == C_BLACK)) {	\
				tmp->_f.colour = C_RED;			\
				node = parent;				\
				parent = node->_f.parent;		\
			} else {					\
				if (tmp->_f.right == &_n##_rb_zero ||	\
				    tmp->_f.right->_f.colour == C_BLACK) {\
					_t *tmp2 = tmp->_f.left;	\
									\
					if (tmp2 != &_n##_rb_zero)	\
						tmp2->_f.colour = C_BLACK;\
					tmp->_f.colour = C_RED;		\
					rotate_right(head, tmp);	\
					tmp = parent->_f.right;		\
				}					\
				tmp->_f.colour = parent->_f.colour;	\
				parent->_f.colour = C_BLACK;		\
				if (tmp->_f.right != &_n##_rb_zero)	\
					tmp->_f.right->_f.colour = C_BLACK;\
				rotate_left(head, parent);		\
				node = head->top._f.right;		\
			}						\
		} else {						\
			tmp = parent->_f.left;				\
			if (tmp->_f.colour == C_RED) {			\
				tmp->_f.colour = C_BLACK;		\
				parent->_f.colour = C_RED;		\
				rotate_right(head, parent);		\
				tmp = parent->_f.left;			\
			}						\
			if ((tmp->_f.left == &_n##_rb_zero ||		\
			     tmp->_f.left->_f.colour == C_BLACK) &&	\
			    (tmp->_f.right == &_n##_rb_zero ||		\
			     tmp->_f.right->_f.colour == C_BLACK)) {	\
				tmp->_f.colour = C_RED;			\
				node = parent;				\
				parent = node->_f.parent;		\
			} else {					\
				if (tmp->_f.left == &_n##_rb_zero ||	\
				    tmp->_f.left->_f.colour == C_BLACK) {\
					_t *tmp2 = tmp->_f.right;	\
									\
					if (tmp2 != &_n##_rb_zero)	\
						tmp2->_f.colour = C_BLACK;\
					tmp->_f.colour = C_RED;		\
					rotate_left(head, tmp);		\
					tmp = parent->_f.left;		\
				}					\
				tmp->_f.colour = parent->_f.colour;	\
				parent->_f.colour = C_BLACK;		\
				if (tmp->_f.left != &_n##_rb_zero)	\
					tmp->_f.left->_f.colour = C_BLACK;\
				rotate_right(head, parent);		\
				node = head->top._f.right;		\
				break;					\
			}						\
		}							\
	}								\
	if (node != &_n##_rb_zero)					\
		node->_f.colour = C_BLACK;				\
}									\
									\
_t *									\
_n##_rb_delete(struct _n##_rb_head *head, _t *node)			\
{									\
	_t *child, *parent, *old = node, *left;				\
	rbcolour_t color;						\
									\
	if (node->_f.left == &_n##_rb_zero) {				\
		child = node->_f.right;					\
	} else if (node->_f.right == &_n##_rb_zero) {			\
		child = node->_f.left;					\
	} else {							\
		node = node->_f.right;					\
		while ((left = node->_f.left) != &_n##_rb_zero)		\
			node = left;					\
		child = node->_f.right;					\
		parent = node->_f.parent;				\
		color = node->_f.colour;				\
		if (child != &_n##_rb_zero)				\
			child->_f.parent = parent;			\
		if (parent != &_n##_rb_zero) {				\
			if (parent->_f.left == node)			\
				parent->_f.left = child;		\
			else						\
				parent->_f.right = child;		\
		} else {						\
			head->top._f.right = child;			\
		}							\
		if (node->_f.parent == old)				\
			parent = node;					\
		*node = *old;						\
		if (old->_f.parent != &_n##_rb_zero) {			\
			if (old->_f.parent->_f.left == old)		\
				old->_f.parent->_f.left = node;		\
			else						\
				old->_f.parent->_f.right = node;	\
		} else {						\
			head->top._f.right = child;			\
		}							\
		old->_f.left->_f.parent = node;				\
		if (old->_f.right != &_n##_rb_zero)			\
			old->_f.right->_f.parent = node;		\
		if (parent != &_n##_rb_zero) {				\
			left = parent;					\
		}							\
		goto colour;						\
	}								\
	parent = node->_f.parent;					\
	color= node->_f.colour;						\
	if (child != &_n##_rb_zero)					\
		child->_f.parent = parent;				\
	if (parent != &_n##_rb_zero) {					\
		if (parent->_f.left == node)				\
			parent->_f.left = child;			\
		else							\
			parent->_f.right = child;			\
	} else {							\
		head->top._f.right = child;				\
	}								\
colour:									\
	if (color == C_BLACK)						\
		deleteblack(head, parent, node);			\
	head->count--;							\
	return old;							\
}									\
									\
void									\
_n##_rb_init(struct _n##_rb_head *head)					\
{									\
	memset(head, 0, sizeof(*head));					\
	memset(&_n##_rb_zero, 0, sizeof(_n##_rb_zero));			\
	head->top._f.left = &_n##_rb_zero;				\
	head->top._f.right = &_n##_rb_zero;				\
	head->top._f.parent = &head->top;				\
	_n##_rb_zero._f.left = &_n##_rb_zero;				\
	_n##_rb_zero._f.right = &_n##_rb_zero;				\
	_n##_rb_zero._f.parent = &_n##_rb_zero;				\
}									\
									\
void									\
_n##_rb_walktree(struct _n##_rb_head *head, _n##_rb_walker_t func, void *arg)\
{									\
	_t *prev;							\
	_t *next;							\
	_t *node = head->top._f.right;					\
	_t *base;							\
									\
	while (node != &_n##_rb_zero)					\
		node = node->_f.left;					\
									\
	for (;;) {							\
		base = node;						\
		prev = node;						\
		while ((node->_f.parent->_f.right == node) &&		\
		       (node != &_n##_rb_zero))	{			\
			prev = node;					\
			node = node->_f.parent;				\
		}							\
									\
		node = prev;						\
		for (node = node->_f.parent->_f.right; node != &_n##_rb_zero;\
		     node = node->_f.left)				\
			prev = node;					\
		next = prev;						\
									\
		if (node != &_n##_rb_zero)				\
			func(node, arg);				\
									\
		node = next;						\
		if (node == &_n##_rb_zero)				\
			break;						\
	}								\
}									\
									\
_t *									\
_n##_rb_search(struct _n##_rb_head *head, void *key)			\
{									\
	int	match;							\
	_t	*node;							\
	node = head->top._f.right;					\
	while (node != &_n##_rb_zero) {					\
		match = _cmp(key, node);				\
		if (match == 0)						\
			break;						\
		if (match< 0)						\
			node = node->_f.left;				\
		else							\
			node = node->_f.right;				\
	}								\
	if (node == &_n##_rb_zero || match != 0)			\
		return (NULL);						\
	return (node);							\
}

#define	RBI_DELETE(_n, _h, _v)		_n##_rb_delete(_h, _v)
#define	RBI_FIELD(_n)			struct _n##_rb_link
#define	RBI_INIT(_n, _h)		_n##_rb_init(_h)
#define	RBI_INSERT(_n, _h, _v)		_n##_rb_insert(_h, _v)
#define	RBI_ISEMPTY(_h)			((_h)->count == 0)
#define	RBI_SEARCH(_n, _h, _k)		_n##_rb_search(_h, _k)
#define	RBI_WALK(_n, _h, _w, _a)	_n##_rb_walktree(_h, _w, _a)
#define	RBI_ZERO(_n)			_n##_rb_zero
