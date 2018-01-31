/* SPDX-License-Identifier: GPL-2.0 */
/*---------------------------------------------------------------------------------------------------------
 * kernel/include/linux/goodix_queue.h
 *
 * Copyright(c) 2010 Goodix Technology Corp. All rights reserved.      
 * Author: Eltonny
 * Date: 2010.11.11                                    
 *                                                                                                         
 *---------------------------------------------------------------------------------------------------------*/
 
/* 用于管理手指序列的伪队列操作函数，
 * 适用于Goodix的Guitar小屏驱动
 * 调整手指上报顺序以避免出现手指ID交换现象
 * 在大屏驱动中，该功能将被调整
 */ 
#ifndef _LINUX_GOODIX_QUEUE_H
#define	_LINUX_GOODIX_QUEUE_H
#include <linux/goodix_touch.h>

struct point_node
{
	uint8_t num;
	uint8_t state;
	uint8_t pressure;
	unsigned int x;
	unsigned int y;
};

struct point_queue
{
	int length;
	struct point_node pointer[MAX_FINGER_NUM];
};


/*******************************************************	
功能：
	删除手指队列中松键的手指
参数：
	point_list
********************************************************/
static void del_point(struct point_queue *point_list)
{
	int count = point_list->length-1;
	int position;
	for(; count >= 0; count--)		//note: must search from tail to head
    	if(point_list->pointer[count].state == FLAG_UP)
        {             	
			if(point_list->length == 0 )
				return ;
			position = count;
			for(; position < MAX_FINGER_NUM -1; position++)
				point_list->pointer[position] = point_list->pointer[position+1];
			point_list->length--;
        }
}

/*******************************************************	
功能：
	在队列尾中加入新增的手指
参数：
	point_list
	num：手指标号
return：
	是否成功增加手指
********************************************************/
static int add_point(struct point_queue *point_list, int num)
{
	if(point_list->length >= MAX_FINGER_NUM || num < 0 )
		return -1;
	point_list->pointer[point_list->length].num = num;
	point_list->pointer[point_list->length].state = FLAG_DOWN;
	point_list->length++;
	return 0;
}

/*******************************************************	
功能：
	查找指定标号的手指位置
参数：
	point_list
	num：手指标号
return：
	返回找到的手指在队列中的位置
********************************************************/
static int search_point(struct point_queue *point_list, int num)
{
	int count = 0;
	if(point_list->length <= 0 || num < 0 || num > MAX_FINGER_NUM)
		return -1;	//no data
	for(; count < point_list->length; count++)
		if(point_list->pointer[count].num == num)
			return count;
		else continue;
	return -1;
}

/*******************************************************	
功能：
	查找松键的手指并设置标志位为FLAG_UP
参数：
	point_list
	num：手指标号
return：
	是否成功设置手指标志位
********************************************************/
static int set_up_point(struct point_queue *point_list, int num)
{
	int number = 0;
	if(point_list->length <= 0 || num < 0 || num > MAX_FINGER_NUM)
		return -1;	//no data
	number = search_point(point_list, num);
	if(num >= 0)
	{
		point_list->pointer[number].state = FLAG_UP;
		return 0;
	}
	return -1;
}

#endif /* _LINUX_GOODIX_QUEUE_H */
