/*-
 * Copyright (c) 2018 Microsemi Corporation.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

/* $FreeBSD$ */

#include "smartpqi_includes.h"

#ifndef LOCKFREE_STACK

/*
 * Function used to release the tag from taglist.
 */
void pqisrc_put_tag(pqi_taglist_t *taglist, uint32_t elem)
{

	OS_ACQUIRE_SPINLOCK(&(taglist->lock));
	/*DBG_FUNC("IN\n");*/

	ASSERT(taglist->num_elem < taglist->max_elem);

	if (taglist->num_elem < taglist->max_elem) {
		taglist->elem_array[taglist->tail] = elem;
		taglist->num_elem++;
		taglist->tail = (taglist->tail + 1) % taglist->max_elem;
	}

	OS_RELEASE_SPINLOCK(&taglist->lock);

	/*DBG_FUNC("OUT\n");*/
}

/*
 * Function used to get an unoccupied tag from the tag list.
 */
uint32_t pqisrc_get_tag(pqi_taglist_t *taglist)
{
	uint32_t elem = INVALID_ELEM;

	/*DBG_FUNC("IN\n");*/

	OS_ACQUIRE_SPINLOCK(&taglist->lock);

	ASSERT(taglist->num_elem > 0);

	if (taglist->num_elem > 0) {
		elem = taglist->elem_array[taglist->head];
		taglist->num_elem--;
		taglist->head = (taglist->head + 1) % taglist->max_elem;
	}

	OS_RELEASE_SPINLOCK(&taglist->lock);

	/*DBG_FUNC("OUT got %d\n", elem);*/
	return elem;
}

/*
 * Initialize circular queue implementation of tag list.
 */
int pqisrc_init_taglist(pqisrc_softstate_t *softs, pqi_taglist_t *taglist,
				uint32_t max_elem)
{
	int ret = PQI_STATUS_SUCCESS;
	int i = 0;
	
	DBG_FUNC("IN\n");

	taglist->max_elem = max_elem;
	taglist->num_elem = 0;
	taglist->head = 0;
	taglist->tail = 0;
	taglist->elem_array = os_mem_alloc(softs,
			(max_elem * sizeof(uint32_t))); 
	if (!(taglist->elem_array)) {
		DBG_FUNC("Unable to allocate memory for taglist\n");
		ret = PQI_STATUS_FAILURE;
		goto err_out;
	}
 
    	os_strlcpy(taglist->lockname, "tag_lock",  LOCKNAME_SIZE);
    	ret = os_init_spinlock(softs, &taglist->lock, taglist->lockname);
    	if(ret){
        	DBG_ERR("tag lock initialization failed\n");
        	taglist->lockcreated=false;
        	goto err_lock;
	}
    	taglist->lockcreated = true;
    
	/* indices 1 to max_elem are considered as valid tags */
	for (i=1; i <= max_elem; i++) {
		softs->rcb[i].tag = INVALID_ELEM;
		pqisrc_put_tag(taglist, i);
	}
	
	DBG_FUNC("OUT\n");
	return ret;

err_lock:
    os_mem_free(softs, (char *)taglist->elem_array, 
        (taglist->max_elem * sizeof(uint32_t)));
	taglist->elem_array = NULL;
err_out:
	DBG_FUNC("OUT failed\n");
	return ret;
}

/*
 * Destroy circular queue implementation of tag list.
 */
void pqisrc_destroy_taglist(pqisrc_softstate_t *softs, pqi_taglist_t *taglist)
{
	DBG_FUNC("IN\n");
	os_mem_free(softs, (char *)taglist->elem_array, 
		(taglist->max_elem * sizeof(uint32_t)));
	taglist->elem_array = NULL;
    
    	if(taglist->lockcreated==true){
        	os_uninit_spinlock(&taglist->lock);
        	taglist->lockcreated = false;
    	}
    
	DBG_FUNC("OUT\n");
}

#else /* LOCKFREE_STACK */

/*
 * Initialize circular queue implementation of tag list.
 */
int pqisrc_init_taglist(pqisrc_softstate_t *softs, lockless_stack_t *stack,
				uint32_t max_elem)
{
	int ret = PQI_STATUS_SUCCESS;
	int index = 0;
	
	DBG_FUNC("IN\n");
	
	/* indices 1 to max_elem are considered as valid tags */
	stack->num_elements = max_elem + 1; 
	stack->head.data = 0; 
	DBG_INFO("Stack head address :%p\n",&stack->head);
	
	/*Allocate memory for stack*/
	stack->next_index_array = (uint32_t*)os_mem_alloc(softs,
		(stack->num_elements * sizeof(uint32_t)));
	if (!(stack->next_index_array)) {
		DBG_ERR("Unable to allocate memory for stack\n");
		ret = PQI_STATUS_FAILURE;
		goto err_out;
	}	

	/* push all the entries to the stack */
	for (index = 1; index < stack->num_elements ; index++) {
		softs->rcb[index].tag = INVALID_ELEM;
		pqisrc_put_tag(stack, index);
	}
	
	DBG_FUNC("OUT\n");
	return ret;
err_out:
	DBG_FUNC("Failed OUT\n");
	return ret;
}

/*
 * Destroy circular queue implementation of tag list.
 */
void pqisrc_destroy_taglist(pqisrc_softstate_t *softs, lockless_stack_t *stack)
{
	DBG_FUNC("IN\n");
	
	/* de-allocate stack memory */
	if (stack->next_index_array) {
		os_mem_free(softs,(char*)stack->next_index_array,
			(stack->num_elements * sizeof(uint32_t)));
		stack->next_index_array = NULL;
	}
	
	DBG_FUNC("OUT\n");
}

/*
 * Function used to release the tag from taglist.
 */
void pqisrc_put_tag(lockless_stack_t *stack, uint32_t index)
{
   	union head_list cur_head, new_head;

	DBG_FUNC("IN\n");
 	DBG_INFO("push tag :%d\n",index);

	if ( index >= stack->num_elements ) {
		ASSERT(false);
 		DBG_ERR("Pushed Invalid index\n"); /* stack full */               
		return;
	}
	
	if ( stack->next_index_array[index] != 0) {
 		ASSERT(false);
		DBG_ERR("Index already present as tag in the stack\n");
		return;
	}

	do {
		cur_head = stack->head;
		/* increment seq_no */
 		new_head.top.seq_no = cur_head.top.seq_no + 1;
		/* update the index at the top of the stack with the new index */
		new_head.top.index = index;
		/* Create a link to the previous index */
		stack->next_index_array[index] = cur_head.top.index;
	}while(OS_ATOMIC64_CAS(&stack->head.data,cur_head.data,new_head.data)
		!= cur_head.data);
 	DBG_FUNC("OUT\n");
 	return;
}

/*
 * Function used to get an unoccupied tag from the tag list.
 */
uint32_t pqisrc_get_tag(lockless_stack_t *stack)
{
	union head_list cur_head, new_head;

	DBG_FUNC("IN\n");
	do {
		cur_head = stack->head;
		if (cur_head.top.index == 0)    /* stack empty */
			return INVALID_ELEM;
		/* increment seq_no field */
		new_head.top.seq_no = cur_head.top.seq_no + 1;
		/* update the index at the top of the stack with the next index */
		new_head.top.index = stack->next_index_array[cur_head.top.index];
	}while(OS_ATOMIC64_CAS(&stack->head.data,cur_head.data,new_head.data) 
        	!= cur_head.data);
 	stack->next_index_array[cur_head.top.index] = 0;

	DBG_INFO("pop tag: %d\n",cur_head.top.index);
 	DBG_FUNC("OUT\n");
	return cur_head.top.index; /*tag*/
}
#endif /* LOCKFREE_STACK */
