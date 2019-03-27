/*	$OpenBSD: stack.c,v 1.13 2014/12/01 13:13:00 deraadt Exp $	*/

/*
 * Copyright (c) 2003, Otto Moerbeek <otto@drijf.net>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <err.h>
#include <stdlib.h>
#include <string.h>

#include "extern.h"

static __inline bool	 stack_empty(const struct stack *);
static void		 stack_grow(struct stack *);
static struct array	*array_new(void);
static __inline void	 array_free(struct array *);
static struct array	*array_dup(const struct array *);
static __inline void	 array_grow(struct array *, size_t);
static __inline void	 array_assign(struct array *, size_t, const struct value *);
static __inline struct value	*array_retrieve(const struct array *, size_t);

void
stack_init(struct stack *stack)
{

	stack->size = 0;
	stack->sp = -1;
	stack->stack = NULL;
}

static __inline bool
stack_empty(const struct stack *stack)
{
	bool empty = stack->sp == -1;

	if (empty)
		warnx("stack empty");
	return empty;
}

/* Clear number or string, but leave value itself */
void
stack_free_value(struct value *v)
{

	switch (v->type) {
	case BCODE_NONE:
		break;
	case BCODE_NUMBER:
		free_number(v->u.num);
		break;
	case BCODE_STRING:
		free(v->u.string);
		break;
	}
	array_free(v->array);
	v->array = NULL;
}

/* Copy number or string content into already allocated target */
struct value *
stack_dup_value(const struct value *a, struct value *copy)
{

	copy->type = a->type;

	switch (a->type) {
	case BCODE_NONE:
		break;
	case BCODE_NUMBER:
		copy->u.num = dup_number(a->u.num);
		break;
	case BCODE_STRING:
		copy->u.string = strdup(a->u.string);
		if (copy->u.string == NULL)
			err(1, NULL);
		break;
	}

	copy->array = a->array == NULL ? NULL : array_dup(a->array);

	return (copy);
}

size_t
stack_size(const struct stack *stack)
{

	return (stack->sp + 1);
}

void
stack_dup(struct stack *stack)
{
	struct value *value;
	struct value copy;

	value = stack_tos(stack);
	if (value == NULL) {
		warnx("stack empty");
		return;
	}
	stack_push(stack, stack_dup_value(value, &copy));
}

void
stack_swap(struct stack *stack)
{
	struct value copy;

	if (stack->sp < 1) {
		warnx("stack empty");
		return;
	}
	copy = stack->stack[stack->sp];
	stack->stack[stack->sp] = stack->stack[stack->sp-1];
	stack->stack[stack->sp-1] = copy;
}

static void
stack_grow(struct stack *stack)
{
	size_t new_size;

	if (++stack->sp == stack->size) {
		new_size = stack->size * 2 + 1;
		stack->stack = breallocarray(stack->stack,
		    new_size, sizeof(*stack->stack));
		stack->size = new_size;
	}
}

void
stack_pushnumber(struct stack *stack, struct number *b)
{

	stack_grow(stack);
	stack->stack[stack->sp].type = BCODE_NUMBER;
	stack->stack[stack->sp].u.num = b;
	stack->stack[stack->sp].array = NULL;
}

void
stack_pushstring(struct stack *stack, char *string)
{

	stack_grow(stack);
	stack->stack[stack->sp].type = BCODE_STRING;
	stack->stack[stack->sp].u.string = string;
	stack->stack[stack->sp].array = NULL;
}

void
stack_push(struct stack *stack, struct value *v)
{

	switch (v->type) {
	case BCODE_NONE:
		stack_grow(stack);
		stack->stack[stack->sp].type = BCODE_NONE;
		break;
	case BCODE_NUMBER:
		stack_pushnumber(stack, v->u.num);
		break;
	case BCODE_STRING:
		stack_pushstring(stack, v->u.string);
		break;
	}
	stack->stack[stack->sp].array = v->array == NULL ?
	    NULL : array_dup(v->array);
}

struct value *
stack_tos(const struct stack *stack)
{

	if (stack->sp == -1)
		return (NULL);
	return &stack->stack[stack->sp];
}

void
stack_set_tos(struct stack *stack, struct value *v)
{

	if (stack->sp == -1)
		stack_push(stack, v);
	else {
		stack_free_value(&stack->stack[stack->sp]);
		stack->stack[stack->sp] = *v;
		stack->stack[stack->sp].array = v->array == NULL ?
		    NULL : array_dup(v->array);
	}
}

struct value *
stack_pop(struct stack *stack)
{

	if (stack_empty(stack))
		return (NULL);
	return &stack->stack[stack->sp--];
}

struct number *
stack_popnumber(struct stack *stack)
{

	if (stack_empty(stack))
		return (NULL);
	array_free(stack->stack[stack->sp].array);
	stack->stack[stack->sp].array = NULL;
	if (stack->stack[stack->sp].type != BCODE_NUMBER) {
		warnx("not a number"); /* XXX remove */
		return (NULL);
	}
	return stack->stack[stack->sp--].u.num;
}

char *
stack_popstring(struct stack *stack)
{

	if (stack_empty(stack))
		return (NULL);
	array_free(stack->stack[stack->sp].array);
	stack->stack[stack->sp].array = NULL;
	if (stack->stack[stack->sp].type != BCODE_STRING) {
		warnx("not a string"); /* XXX remove */
		return (NULL);
	}
	return stack->stack[stack->sp--].u.string;
}

void
stack_clear(struct stack *stack)
{

	while (stack->sp >= 0)
		stack_free_value(&stack->stack[stack->sp--]);
	free(stack->stack);
	stack_init(stack);
}

void
stack_print(FILE *f, const struct stack *stack, const char *prefix, u_int base)
{
	ssize_t i;

	for (i = stack->sp; i >= 0; i--) {
		print_value(f, &stack->stack[i], prefix, base);
		putc('\n', f);
	}
}


static struct array *
array_new(void)
{
	struct array *a;

	a = bmalloc(sizeof(*a));
	a->data = NULL;
	a->size = 0;
	return a;
}

static __inline void
array_free(struct array *a)
{
	size_t i;

	if (a == NULL)
		return;
	for (i = 0; i < a->size; i++)
		stack_free_value(&a->data[i]);
	free(a->data);
	free(a);
}

static struct array *
array_dup(const struct array *a)
{
	struct array *n;
	size_t i;

	if (a == NULL)
		return (NULL);
	n = array_new();
	array_grow(n, a->size);
	for (i = 0; i < a->size; i++)
		stack_dup_value(&a->data[i], &n->data[i]);
	return (n);
}

static __inline void
array_grow(struct array *array, size_t newsize)
{
	size_t i;

	array->data = breallocarray(array->data, newsize, sizeof(*array->data));
	for (i = array->size; i < newsize; i++) {
		array->data[i].type = BCODE_NONE;
		array->data[i].array = NULL;
	}
	array->size = newsize;
}

static __inline void
array_assign(struct array *array, size_t i, const struct value *v)
{

	if (i >= array->size)
		array_grow(array, i + 1);
	stack_free_value(&array->data[i]);
	array->data[i] = *v;
}

static __inline struct value *
array_retrieve(const struct array *array, size_t i)
{

	if (i >= array->size)
		return (NULL);
	return &array->data[i];
}

void
frame_assign(struct stack *stack, size_t i, const struct value *v)
{
	struct array *a;
	struct value n;

	if (stack->sp == -1) {
		n.type = BCODE_NONE;
		n.array = NULL;
		stack_push(stack, &n);
	}

	a = stack->stack[stack->sp].array;
	if (a == NULL)
		a = stack->stack[stack->sp].array = array_new();
	array_assign(a, i, v);
}

struct value *
frame_retrieve(const struct stack *stack, size_t i)
{
	struct array *a;

	if (stack->sp == -1)
		return (NULL);
	a = stack->stack[stack->sp].array;
	if (a == NULL)
		a = stack->stack[stack->sp].array = array_new();
	return array_retrieve(a, i);
}
