/*
 * "$Id: mxml-index.c 426 2011-01-01 23:42:17Z mike $"
 *
 * Index support code for Mini-XML, a small XML-like file parsing library.
 *
 * Copyright 2003-2011 by Michael R Sweet.
 *
 * These coded instructions, statements, and computer programs are the
 * property of Michael R Sweet and are protected by Federal copyright
 * law.  Distribution and use rights are outlined in the file "COPYING"
 * which should have been included with this file.  If this file is
 * missing or damaged, see the license at:
 *
 *     http://www.minixml.org/
 *
 * Contents:
 *
 */

/*
 * Include necessary headers...
 */

#include "config.h"
#include "mxml.h"


/*
 * Sort functions...
 */

static int	index_compare(mxml_index_t *ind, mxml_node_t *first,
		              mxml_node_t *second);
static int	index_find(mxml_index_t *ind, const char *element,
		           const char *value, mxml_node_t *node);
static void	index_sort(mxml_index_t *ind, int left, int right);


/*
 * 'mxmlIndexDelete()' - Delete an index.
 */

void
mxmlIndexDelete(mxml_index_t *ind)	/* I - Index to delete */
{
 /*
  * Range check input..
  */

  if (!ind)
    return;

 /*
  * Free memory...
  */

  if (ind->attr)
    free(ind->attr);

  if (ind->alloc_nodes)
    free(ind->nodes);

  free(ind);
}


/*
 * 'mxmlIndexEnum()' - Return the next node in the index.
 *
 * Nodes are returned in the sorted order of the index.
 */

mxml_node_t *				/* O - Next node or NULL if there is none */
mxmlIndexEnum(mxml_index_t *ind)	/* I - Index to enumerate */
{
 /*
  * Range check input...
  */

  if (!ind)
    return (NULL);

 /*
  * Return the next node...
  */

  if (ind->cur_node < ind->num_nodes)
    return (ind->nodes[ind->cur_node ++]);
  else
    return (NULL);
}


/*
 * 'mxmlIndexFind()' - Find the next matching node.
 *
 * You should call mxmlIndexReset() prior to using this function for
 * the first time with a particular set of "element" and "value"
 * strings. Passing NULL for both "element" and "value" is equivalent
 * to calling mxmlIndexEnum().
 */

mxml_node_t *				/* O - Node or NULL if none found */
mxmlIndexFind(mxml_index_t *ind,	/* I - Index to search */
              const char   *element,	/* I - Element name to find, if any */
	      const char   *value)	/* I - Attribute value, if any */
{
  int		diff,			/* Difference between names */
		current,		/* Current entity in search */
		first,			/* First entity in search */
		last;			/* Last entity in search */


#ifdef DEBUG
  printf("mxmlIndexFind(ind=%p, element=\"%s\", value=\"%s\")\n",
         ind, element ? element : "(null)", value ? value : "(null)");
#endif /* DEBUG */

 /*
  * Range check input...
  */

  if (!ind || (!ind->attr && value))
  {
#ifdef DEBUG
    puts("    returning NULL...");
    printf("    ind->attr=\"%s\"\n", ind->attr ? ind->attr : "(null)");
#endif /* DEBUG */

    return (NULL);
  }

 /*
  * If both element and value are NULL, just enumerate the nodes in the
  * index...
  */

  if (!element && !value)
    return (mxmlIndexEnum(ind));

 /*
  * If there are no nodes in the index, return NULL...
  */

  if (!ind->num_nodes)
  {
#ifdef DEBUG
    puts("    returning NULL...");
    puts("    no nodes!");
#endif /* DEBUG */

    return (NULL);
  }

 /*
  * If cur_node == 0, then find the first matching node...
  */

  if (ind->cur_node == 0)
  {
   /*
    * Find the first node using a modified binary search algorithm...
    */

    first = 0;
    last  = ind->num_nodes - 1;

#ifdef DEBUG
    printf("    find first time, num_nodes=%d...\n", ind->num_nodes);
#endif /* DEBUG */

    while ((last - first) > 1)
    {
      current = (first + last) / 2;

#ifdef DEBUG
      printf("    first=%d, last=%d, current=%d\n", first, last, current);
#endif /* DEBUG */

      if ((diff = index_find(ind, element, value, ind->nodes[current])) == 0)
      {
       /*
        * Found a match, move back to find the first...
	*/

#ifdef DEBUG
        puts("    match!");
#endif /* DEBUG */

        while (current > 0 &&
	       !index_find(ind, element, value, ind->nodes[current - 1]))
	  current --;

#ifdef DEBUG
        printf("    returning first match=%d\n", current);
#endif /* DEBUG */

       /*
        * Return the first match and save the index to the next...
	*/

        ind->cur_node = current + 1;

	return (ind->nodes[current]);
      }
      else if (diff < 0)
	last = current;
      else
	first = current;

#ifdef DEBUG
      printf("    diff=%d\n", diff);
#endif /* DEBUG */
    }

   /*
    * If we get this far, then we found exactly 0 or 1 matches...
    */

    for (current = first; current <= last; current ++)
      if (!index_find(ind, element, value, ind->nodes[current]))
      {
       /*
	* Found exactly one (or possibly two) match...
	*/

#ifdef DEBUG
	printf("    returning only match %d...\n", current);
#endif /* DEBUG */

	ind->cur_node = current + 1;

	return (ind->nodes[current]);
      }

   /*
    * No matches...
    */

    ind->cur_node = ind->num_nodes;

#ifdef DEBUG
    puts("    returning NULL...");
#endif /* DEBUG */

    return (NULL);
  }
  else if (ind->cur_node < ind->num_nodes &&
           !index_find(ind, element, value, ind->nodes[ind->cur_node]))
  {
   /*
    * Return the next matching node...
    */

#ifdef DEBUG
    printf("    returning next match %d...\n", ind->cur_node);
#endif /* DEBUG */

    return (ind->nodes[ind->cur_node ++]);
  }

 /*
  * If we get this far, then we have no matches...
  */

  ind->cur_node = ind->num_nodes;

#ifdef DEBUG
  puts("    returning NULL...");
#endif /* DEBUG */

  return (NULL);
}


/*
 * 'mxmlIndexGetCount()' - Get the number of nodes in an index.
 *
 * @since Mini-XML 2.7@
 */

int					/* I - Number of nodes in index */
mxmlIndexGetCount(mxml_index_t *ind)	/* I - Index of nodes */
{
 /*
  * Range check input...
  */

  if (!ind)
    return (0);

 /*
  * Return the number of nodes in the index...
  */

  return (ind->num_nodes);
}


/*
 * 'mxmlIndexNew()' - Create a new index.
 *
 * The index will contain all nodes that contain the named element and/or
 * attribute. If both "element" and "attr" are NULL, then the index will
 * contain a sorted list of the elements in the node tree.  Nodes are
 * sorted by element name and optionally by attribute value if the "attr"
 * argument is not NULL.
 */

mxml_index_t *				/* O - New index */
mxmlIndexNew(mxml_node_t *node,		/* I - XML node tree */
             const char  *element,	/* I - Element to index or NULL for all */
             const char  *attr)		/* I - Attribute to index or NULL for none */
{
  mxml_index_t	*ind;			/* New index */
  mxml_node_t	*current,		/* Current node in index */
  		**temp;			/* Temporary node pointer array */


 /*
  * Range check input...
  */

#ifdef DEBUG
  printf("mxmlIndexNew(node=%p, element=\"%s\", attr=\"%s\")\n",
         node, element ? element : "(null)", attr ? attr : "(null)");
#endif /* DEBUG */

  if (!node)
    return (NULL);

 /*
  * Create a new index...
  */

  if ((ind = calloc(1, sizeof(mxml_index_t))) == NULL)
  {
    mxml_error("Unable to allocate %d bytes for index - %s",
               sizeof(mxml_index_t), strerror(errno));
    return (NULL);
  }

  if (attr)
    ind->attr = strdup(attr);

  if (!element && !attr)
    current = node;
  else
    current = mxmlFindElement(node, node, element, attr, NULL, MXML_DESCEND);

  while (current)
  {
    if (ind->num_nodes >= ind->alloc_nodes)
    {
      if (!ind->alloc_nodes)
        temp = malloc(64 * sizeof(mxml_node_t *));
      else
        temp = realloc(ind->nodes, (ind->alloc_nodes + 64) * sizeof(mxml_node_t *));

      if (!temp)
      {
       /*
        * Unable to allocate memory for the index, so abort...
	*/

        mxml_error("Unable to allocate %d bytes for index: %s",
	           (ind->alloc_nodes + 64) * sizeof(mxml_node_t *),
		   strerror(errno));

        mxmlIndexDelete(ind);
	return (NULL);
      }

      ind->nodes       = temp;
      ind->alloc_nodes += 64;
    }

    ind->nodes[ind->num_nodes ++] = current;

    current = mxmlFindElement(current, node, element, attr, NULL, MXML_DESCEND);
  }

 /*
  * Sort nodes based upon the search criteria...
  */

#ifdef DEBUG
  {
    int i;				/* Looping var */


    printf("%d node(s) in index.\n\n", ind->num_nodes);

    if (attr)
    {
      printf("Node      Address   Element         %s\n", attr);
      puts("--------  --------  --------------  ------------------------------");

      for (i = 0; i < ind->num_nodes; i ++)
	printf("%8d  %-8p  %-14.14s  %s\n", i, ind->nodes[i],
	       ind->nodes[i]->value.element.name,
	       mxmlElementGetAttr(ind->nodes[i], attr));
    }
    else
    {
      puts("Node      Address   Element");
      puts("--------  --------  --------------");

      for (i = 0; i < ind->num_nodes; i ++)
	printf("%8d  %-8p  %s\n", i, ind->nodes[i],
	       ind->nodes[i]->value.element.name);
    }

    putchar('\n');
  }
#endif /* DEBUG */

  if (ind->num_nodes > 1)
    index_sort(ind, 0, ind->num_nodes - 1);

#ifdef DEBUG
  {
    int i;				/* Looping var */


    puts("After sorting:\n");

    if (attr)
    {
      printf("Node      Address   Element         %s\n", attr);
      puts("--------  --------  --------------  ------------------------------");

      for (i = 0; i < ind->num_nodes; i ++)
	printf("%8d  %-8p  %-14.14s  %s\n", i, ind->nodes[i],
	       ind->nodes[i]->value.element.name,
	       mxmlElementGetAttr(ind->nodes[i], attr));
    }
    else
    {
      puts("Node      Address   Element");
      puts("--------  --------  --------------");

      for (i = 0; i < ind->num_nodes; i ++)
	printf("%8d  %-8p  %s\n", i, ind->nodes[i],
	       ind->nodes[i]->value.element.name);
    }

    putchar('\n');
  }
#endif /* DEBUG */

 /*
  * Return the new index...
  */

  return (ind);
}


/*
 * 'mxmlIndexReset()' - Reset the enumeration/find pointer in the index and
 *                      return the first node in the index.
 *
 * This function should be called prior to using mxmlIndexEnum() or
 * mxmlIndexFind() for the first time.
 */

mxml_node_t *				/* O - First node or NULL if there is none */
mxmlIndexReset(mxml_index_t *ind)	/* I - Index to reset */
{
#ifdef DEBUG
  printf("mxmlIndexReset(ind=%p)\n", ind);
#endif /* DEBUG */

 /*
  * Range check input...
  */

  if (!ind)
    return (NULL);

 /*
  * Set the index to the first element...
  */

  ind->cur_node = 0;

 /*
  * Return the first node...
  */

  if (ind->num_nodes)
    return (ind->nodes[0]);
  else
    return (NULL);
}


/*
 * 'index_compare()' - Compare two nodes.
 */

static int				/* O - Result of comparison */
index_compare(mxml_index_t *ind,	/* I - Index */
              mxml_node_t  *first,	/* I - First node */
              mxml_node_t  *second)	/* I - Second node */
{
  int	diff;				/* Difference */


 /*
  * Check the element name...
  */

  if ((diff = strcmp(first->value.element.name,
                     second->value.element.name)) != 0)
    return (diff);

 /*
  * Check the attribute value...
  */

  if (ind->attr)
  {
    if ((diff = strcmp(mxmlElementGetAttr(first, ind->attr),
                       mxmlElementGetAttr(second, ind->attr))) != 0)
      return (diff);
  }

 /*
  * No difference, return 0...
  */

  return (0);
}


/*
 * 'index_find()' - Compare a node with index values.
 */

static int				/* O - Result of comparison */
index_find(mxml_index_t *ind,		/* I - Index */
           const char   *element,	/* I - Element name or NULL */
	   const char   *value,		/* I - Attribute value or NULL */
           mxml_node_t  *node)		/* I - Node */
{
  int	diff;				/* Difference */


 /*
  * Check the element name...
  */

  if (element)
  {
    if ((diff = strcmp(element, node->value.element.name)) != 0)
      return (diff);
  }

 /*
  * Check the attribute value...
  */

  if (value)
  {
    if ((diff = strcmp(value, mxmlElementGetAttr(node, ind->attr))) != 0)
      return (diff);
  }

 /*
  * No difference, return 0...
  */

  return (0);
}


/*
 * 'index_sort()' - Sort the nodes in the index...
 *
 * This function implements the classic quicksort algorithm...
 */

static void
index_sort(mxml_index_t *ind,		/* I - Index to sort */
           int          left,		/* I - Left node in partition */
	   int          right)		/* I - Right node in partition */
{
  mxml_node_t	*pivot,			/* Pivot node */
		*temp;			/* Swap node */
  int		templ,			/* Temporary left node */
		tempr;			/* Temporary right node */


 /*
  * Loop until we have sorted all the way to the right...
  */

  do
  {
   /*
    * Sort the pivot in the current partition...
    */

    pivot = ind->nodes[left];

    for (templ = left, tempr = right; templ < tempr;)
    {
     /*
      * Move left while left node <= pivot node...
      */

      while ((templ < right) &&
             index_compare(ind, ind->nodes[templ], pivot) <= 0)
	templ ++;

     /*
      * Move right while right node > pivot node...
      */

      while ((tempr > left) &&
             index_compare(ind, ind->nodes[tempr], pivot) > 0)
	tempr --;

     /*
      * Swap nodes if needed...
      */

      if (templ < tempr)
      {
	temp              = ind->nodes[templ];
	ind->nodes[templ] = ind->nodes[tempr];
	ind->nodes[tempr] = temp;
      }
    }

   /*
    * When we get here, the right (tempr) node is the new position for the
    * pivot node...
    */

    if (index_compare(ind, pivot, ind->nodes[tempr]) > 0)
    {
      ind->nodes[left]  = ind->nodes[tempr];
      ind->nodes[tempr] = pivot;
    }

   /*
    * Recursively sort the left partition as needed...
    */

    if (left < (tempr - 1))
      index_sort(ind, left, tempr - 1);
  }
  while (right > (left = tempr + 1));
}


/*
 * End of "$Id: mxml-index.c 426 2011-01-01 23:42:17Z mike $".
 */
