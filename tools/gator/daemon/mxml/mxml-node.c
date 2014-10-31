/*
 * "$Id: mxml-node.c 451 2014-01-04 21:50:06Z msweet $"
 *
 * Node support code for Mini-XML, a small XML-like file parsing library.
 *
 * Copyright 2003-2014 by Michael R Sweet.
 *
 * These coded instructions, statements, and computer programs are the
 * property of Michael R Sweet and are protected by Federal copyright
 * law.  Distribution and use rights are outlined in the file "COPYING"
 * which should have been included with this file.  If this file is
 * missing or damaged, see the license at:
 *
 *     http://www.msweet.org/projects.php/Mini-XML
 */

/*
 * Include necessary headers...
 */

#include "config.h"
#include "mxml.h"


/*
 * Local functions...
 */

static mxml_node_t	*mxml_new(mxml_node_t *parent, mxml_type_t type);


/*
 * 'mxmlAdd()' - Add a node to a tree.
 *
 * Adds the specified node to the parent. If the child argument is not
 * NULL, puts the new node before or after the specified child depending
 * on the value of the where argument. If the child argument is NULL,
 * puts the new node at the beginning of the child list (MXML_ADD_BEFORE)
 * or at the end of the child list (MXML_ADD_AFTER). The constant
 * MXML_ADD_TO_PARENT can be used to specify a NULL child pointer.
 */

void
mxmlAdd(mxml_node_t *parent,		/* I - Parent node */
        int         where,		/* I - Where to add, MXML_ADD_BEFORE or MXML_ADD_AFTER */
        mxml_node_t *child,		/* I - Child node for where or MXML_ADD_TO_PARENT */
	mxml_node_t *node)		/* I - Node to add */
{
#ifdef DEBUG
  fprintf(stderr, "mxmlAdd(parent=%p, where=%d, child=%p, node=%p)\n", parent,
          where, child, node);
#endif /* DEBUG */

 /*
  * Range check input...
  */

  if (!parent || !node)
    return;

#if DEBUG > 1
  fprintf(stderr, "    BEFORE: node->parent=%p\n", node->parent);
  if (parent)
  {
    fprintf(stderr, "    BEFORE: parent->child=%p\n", parent->child);
    fprintf(stderr, "    BEFORE: parent->last_child=%p\n", parent->last_child);
    fprintf(stderr, "    BEFORE: parent->prev=%p\n", parent->prev);
    fprintf(stderr, "    BEFORE: parent->next=%p\n", parent->next);
  }
#endif /* DEBUG > 1 */

 /*
  * Remove the node from any existing parent...
  */

  if (node->parent)
    mxmlRemove(node);

 /*
  * Reset pointers...
  */

  node->parent = parent;

  switch (where)
  {
    case MXML_ADD_BEFORE :
        if (!child || child == parent->child || child->parent != parent)
	{
	 /*
	  * Insert as first node under parent...
	  */

	  node->next = parent->child;

	  if (parent->child)
	    parent->child->prev = node;
	  else
	    parent->last_child = node;

	  parent->child = node;
	}
	else
	{
	 /*
	  * Insert node before this child...
	  */

	  node->next = child;
	  node->prev = child->prev;

	  if (child->prev)
	    child->prev->next = node;
	  else
	    parent->child = node;

	  child->prev = node;
	}
        break;

    case MXML_ADD_AFTER :
        if (!child || child == parent->last_child || child->parent != parent)
	{
	 /*
	  * Insert as last node under parent...
	  */

	  node->parent = parent;
	  node->prev   = parent->last_child;

	  if (parent->last_child)
	    parent->last_child->next = node;
	  else
	    parent->child = node;

	  parent->last_child = node;
        }
	else
	{
	 /*
	  * Insert node after this child...
	  */

	  node->prev = child;
	  node->next = child->next;

	  if (child->next)
	    child->next->prev = node;
	  else
	    parent->last_child = node;

	  child->next = node;
	}
        break;
  }

#if DEBUG > 1
  fprintf(stderr, "    AFTER: node->parent=%p\n", node->parent);
  if (parent)
  {
    fprintf(stderr, "    AFTER: parent->child=%p\n", parent->child);
    fprintf(stderr, "    AFTER: parent->last_child=%p\n", parent->last_child);
    fprintf(stderr, "    AFTER: parent->prev=%p\n", parent->prev);
    fprintf(stderr, "    AFTER: parent->next=%p\n", parent->next);
  }
#endif /* DEBUG > 1 */
}


/*
 * 'mxmlDelete()' - Delete a node and all of its children.
 *
 * If the specified node has a parent, this function first removes the
 * node from its parent using the mxmlRemove() function.
 */

void
mxmlDelete(mxml_node_t *node)		/* I - Node to delete */
{
  int	i;				/* Looping var */


#ifdef DEBUG
  fprintf(stderr, "mxmlDelete(node=%p)\n", node);
#endif /* DEBUG */

 /*
  * Range check input...
  */

  if (!node)
    return;

 /*
  * Remove the node from its parent, if any...
  */

  mxmlRemove(node);

 /*
  * Delete children...
  */

  while (node->child)
    mxmlDelete(node->child);

 /*
  * Now delete any node data...
  */

  switch (node->type)
  {
    case MXML_ELEMENT :
        if (node->value.element.name)
	  free(node->value.element.name);

	if (node->value.element.num_attrs)
	{
	  for (i = 0; i < node->value.element.num_attrs; i ++)
	  {
	    if (node->value.element.attrs[i].name)
	      free(node->value.element.attrs[i].name);
	    if (node->value.element.attrs[i].value)
	      free(node->value.element.attrs[i].value);
	  }

          free(node->value.element.attrs);
	}
        break;
    case MXML_INTEGER :
       /* Nothing to do */
        break;
    case MXML_OPAQUE :
        if (node->value.opaque)
	  free(node->value.opaque);
        break;
    case MXML_REAL :
       /* Nothing to do */
        break;
    case MXML_TEXT :
        if (node->value.text.string)
	  free(node->value.text.string);
        break;
    case MXML_CUSTOM :
        if (node->value.custom.data &&
	    node->value.custom.destroy)
	  (*(node->value.custom.destroy))(node->value.custom.data);
	break;
    default :
        break;
  }

 /*
  * Free this node...
  */

  free(node);
}


/*
 * 'mxmlGetRefCount()' - Get the current reference (use) count for a node.
 *
 * The initial reference count of new nodes is 1. Use the @link mxmlRetain@
 * and @link mxmlRelease@ functions to increment and decrement a node's
 * reference count.
 *
 * @since Mini-XML 2.7@.
 */

int					/* O - Reference count */
mxmlGetRefCount(mxml_node_t *node)	/* I - Node */
{
 /*
  * Range check input...
  */

  if (!node)
    return (0);

 /*
  * Return the reference count...
  */

  return (node->ref_count);
}


/*
 * 'mxmlNewCDATA()' - Create a new CDATA node.
 *
 * The new CDATA node is added to the end of the specified parent's child
 * list. The constant MXML_NO_PARENT can be used to specify that the new
 * CDATA node has no parent. The data string must be nul-terminated and
 * is copied into the new node. CDATA nodes use the MXML_ELEMENT type.
 *
 * @since Mini-XML 2.3@
 */

mxml_node_t *				/* O - New node */
mxmlNewCDATA(mxml_node_t *parent,	/* I - Parent node or MXML_NO_PARENT */
	     const char  *data)		/* I - Data string */
{
  mxml_node_t	*node;			/* New node */


#ifdef DEBUG
  fprintf(stderr, "mxmlNewCDATA(parent=%p, data=\"%s\")\n",
          parent, data ? data : "(null)");
#endif /* DEBUG */

 /*
  * Range check input...
  */

  if (!data)
    return (NULL);

 /*
  * Create the node and set the name value...
  */

  if ((node = mxml_new(parent, MXML_ELEMENT)) != NULL)
    node->value.element.name = _mxml_strdupf("![CDATA[%s]]", data);

  return (node);
}


/*
 * 'mxmlNewCustom()' - Create a new custom data node.
 *
 * The new custom node is added to the end of the specified parent's child
 * list. The constant MXML_NO_PARENT can be used to specify that the new
 * element node has no parent. NULL can be passed when the data in the
 * node is not dynamically allocated or is separately managed.
 *
 * @since Mini-XML 2.1@
 */

mxml_node_t *				/* O - New node */
mxmlNewCustom(
    mxml_node_t              *parent,	/* I - Parent node or MXML_NO_PARENT */
    void                     *data,	/* I - Pointer to data */
    mxml_custom_destroy_cb_t destroy)	/* I - Function to destroy data */
{
  mxml_node_t	*node;			/* New node */


#ifdef DEBUG
  fprintf(stderr, "mxmlNewCustom(parent=%p, data=%p, destroy=%p)\n", parent,
          data, destroy);
#endif /* DEBUG */

 /*
  * Create the node and set the value...
  */

  if ((node = mxml_new(parent, MXML_CUSTOM)) != NULL)
  {
    node->value.custom.data    = data;
    node->value.custom.destroy = destroy;
  }

  return (node);
}


/*
 * 'mxmlNewElement()' - Create a new element node.
 *
 * The new element node is added to the end of the specified parent's child
 * list. The constant MXML_NO_PARENT can be used to specify that the new
 * element node has no parent.
 */

mxml_node_t *				/* O - New node */
mxmlNewElement(mxml_node_t *parent,	/* I - Parent node or MXML_NO_PARENT */
               const char  *name)	/* I - Name of element */
{
  mxml_node_t	*node;			/* New node */


#ifdef DEBUG
  fprintf(stderr, "mxmlNewElement(parent=%p, name=\"%s\")\n", parent,
          name ? name : "(null)");
#endif /* DEBUG */

 /*
  * Range check input...
  */

  if (!name)
    return (NULL);

 /*
  * Create the node and set the element name...
  */

  if ((node = mxml_new(parent, MXML_ELEMENT)) != NULL)
    node->value.element.name = strdup(name);

  return (node);
}


/*
 * 'mxmlNewInteger()' - Create a new integer node.
 *
 * The new integer node is added to the end of the specified parent's child
 * list. The constant MXML_NO_PARENT can be used to specify that the new
 * integer node has no parent.
 */

mxml_node_t *				/* O - New node */
mxmlNewInteger(mxml_node_t *parent,	/* I - Parent node or MXML_NO_PARENT */
               int         integer)	/* I - Integer value */
{
  mxml_node_t	*node;			/* New node */


#ifdef DEBUG
  fprintf(stderr, "mxmlNewInteger(parent=%p, integer=%d)\n", parent, integer);
#endif /* DEBUG */

 /*
  * Create the node and set the element name...
  */

  if ((node = mxml_new(parent, MXML_INTEGER)) != NULL)
    node->value.integer = integer;

  return (node);
}


/*
 * 'mxmlNewOpaque()' - Create a new opaque string.
 *
 * The new opaque node is added to the end of the specified parent's child
 * list. The constant MXML_NO_PARENT can be used to specify that the new
 * opaque node has no parent. The opaque string must be nul-terminated and
 * is copied into the new node.
 */

mxml_node_t *				/* O - New node */
mxmlNewOpaque(mxml_node_t *parent,	/* I - Parent node or MXML_NO_PARENT */
              const char  *opaque)	/* I - Opaque string */
{
  mxml_node_t	*node;			/* New node */


#ifdef DEBUG
  fprintf(stderr, "mxmlNewOpaque(parent=%p, opaque=\"%s\")\n", parent,
          opaque ? opaque : "(null)");
#endif /* DEBUG */

 /*
  * Range check input...
  */

  if (!opaque)
    return (NULL);

 /*
  * Create the node and set the element name...
  */

  if ((node = mxml_new(parent, MXML_OPAQUE)) != NULL)
    node->value.opaque = strdup(opaque);

  return (node);
}


/*
 * 'mxmlNewReal()' - Create a new real number node.
 *
 * The new real number node is added to the end of the specified parent's
 * child list. The constant MXML_NO_PARENT can be used to specify that
 * the new real number node has no parent.
 */

mxml_node_t *				/* O - New node */
mxmlNewReal(mxml_node_t *parent,	/* I - Parent node or MXML_NO_PARENT */
            double      real)		/* I - Real number value */
{
  mxml_node_t	*node;			/* New node */


#ifdef DEBUG
  fprintf(stderr, "mxmlNewReal(parent=%p, real=%g)\n", parent, real);
#endif /* DEBUG */

 /*
  * Create the node and set the element name...
  */

  if ((node = mxml_new(parent, MXML_REAL)) != NULL)
    node->value.real = real;

  return (node);
}


/*
 * 'mxmlNewText()' - Create a new text fragment node.
 *
 * The new text node is added to the end of the specified parent's child
 * list. The constant MXML_NO_PARENT can be used to specify that the new
 * text node has no parent. The whitespace parameter is used to specify
 * whether leading whitespace is present before the node. The text
 * string must be nul-terminated and is copied into the new node.
 */

mxml_node_t *				/* O - New node */
mxmlNewText(mxml_node_t *parent,	/* I - Parent node or MXML_NO_PARENT */
            int         whitespace,	/* I - 1 = leading whitespace, 0 = no whitespace */
	    const char  *string)	/* I - String */
{
  mxml_node_t	*node;			/* New node */


#ifdef DEBUG
  fprintf(stderr, "mxmlNewText(parent=%p, whitespace=%d, string=\"%s\")\n",
          parent, whitespace, string ? string : "(null)");
#endif /* DEBUG */

 /*
  * Range check input...
  */

  if (!string)
    return (NULL);

 /*
  * Create the node and set the text value...
  */

  if ((node = mxml_new(parent, MXML_TEXT)) != NULL)
  {
    node->value.text.whitespace = whitespace;
    node->value.text.string     = strdup(string);
  }

  return (node);
}


/*
 * 'mxmlNewTextf()' - Create a new formatted text fragment node.
 *
 * The new text node is added to the end of the specified parent's child
 * list. The constant MXML_NO_PARENT can be used to specify that the new
 * text node has no parent. The whitespace parameter is used to specify
 * whether leading whitespace is present before the node. The format
 * string must be nul-terminated and is formatted into the new node.
 */

mxml_node_t *				/* O - New node */
mxmlNewTextf(mxml_node_t *parent,	/* I - Parent node or MXML_NO_PARENT */
             int         whitespace,	/* I - 1 = leading whitespace, 0 = no whitespace */
	     const char  *format,	/* I - Printf-style frmat string */
	     ...)			/* I - Additional args as needed */
{
  mxml_node_t	*node;			/* New node */
  va_list	ap;			/* Pointer to arguments */


#ifdef DEBUG
  fprintf(stderr, "mxmlNewTextf(parent=%p, whitespace=%d, format=\"%s\", ...)\n",
          parent, whitespace, format ? format : "(null)");
#endif /* DEBUG */

 /*
  * Range check input...
  */

  if (!format)
    return (NULL);

 /*
  * Create the node and set the text value...
  */

  if ((node = mxml_new(parent, MXML_TEXT)) != NULL)
  {
    va_start(ap, format);

    node->value.text.whitespace = whitespace;
    node->value.text.string     = _mxml_vstrdupf(format, ap);

    va_end(ap);
  }

  return (node);
}


/*
 * 'mxmlRemove()' - Remove a node from its parent.
 *
 * Does not free memory used by the node - use mxmlDelete() for that.
 * This function does nothing if the node has no parent.
 */

void
mxmlRemove(mxml_node_t *node)		/* I - Node to remove */
{
#ifdef DEBUG
  fprintf(stderr, "mxmlRemove(node=%p)\n", node);
#endif /* DEBUG */

 /*
  * Range check input...
  */

  if (!node || !node->parent)
    return;

 /*
  * Remove from parent...
  */

#if DEBUG > 1
  fprintf(stderr, "    BEFORE: node->parent=%p\n", node->parent);
  if (node->parent)
  {
    fprintf(stderr, "    BEFORE: node->parent->child=%p\n", node->parent->child);
    fprintf(stderr, "    BEFORE: node->parent->last_child=%p\n", node->parent->last_child);
  }
  fprintf(stderr, "    BEFORE: node->child=%p\n", node->child);
  fprintf(stderr, "    BEFORE: node->last_child=%p\n", node->last_child);
  fprintf(stderr, "    BEFORE: node->prev=%p\n", node->prev);
  fprintf(stderr, "    BEFORE: node->next=%p\n", node->next);
#endif /* DEBUG > 1 */

  if (node->prev)
    node->prev->next = node->next;
  else
    node->parent->child = node->next;

  if (node->next)
    node->next->prev = node->prev;
  else
    node->parent->last_child = node->prev;

  node->parent = NULL;
  node->prev   = NULL;
  node->next   = NULL;

#if DEBUG > 1
  fprintf(stderr, "    AFTER: node->parent=%p\n", node->parent);
  if (node->parent)
  {
    fprintf(stderr, "    AFTER: node->parent->child=%p\n", node->parent->child);
    fprintf(stderr, "    AFTER: node->parent->last_child=%p\n", node->parent->last_child);
  }
  fprintf(stderr, "    AFTER: node->child=%p\n", node->child);
  fprintf(stderr, "    AFTER: node->last_child=%p\n", node->last_child);
  fprintf(stderr, "    AFTER: node->prev=%p\n", node->prev);
  fprintf(stderr, "    AFTER: node->next=%p\n", node->next);
#endif /* DEBUG > 1 */
}


/*
 * 'mxmlNewXML()' - Create a new XML document tree.
 *
 * The "version" argument specifies the version number to put in the
 * ?xml element node. If NULL, version 1.0 is assumed.
 *
 * @since Mini-XML 2.3@
 */

mxml_node_t *				/* O - New ?xml node */
mxmlNewXML(const char *version)		/* I - Version number to use */
{
  char	element[1024];			/* Element text */


  snprintf(element, sizeof(element), "?xml version=\"%s\" encoding=\"utf-8\"?",
           version ? version : "1.0");

  return (mxmlNewElement(NULL, element));
}


/*
 * 'mxmlRelease()' - Release a node.
 *
 * When the reference count reaches zero, the node (and any children)
 * is deleted via mxmlDelete().
 *
 * @since Mini-XML 2.3@
 */

int					/* O - New reference count */
mxmlRelease(mxml_node_t *node)		/* I - Node */
{
  if (node)
  {
    if ((-- node->ref_count) <= 0)
    {
      mxmlDelete(node);
      return (0);
    }
    else
      return (node->ref_count);
  }
  else
    return (-1);
}


/*
 * 'mxmlRetain()' - Retain a node.
 *
 * @since Mini-XML 2.3@
 */

int					/* O - New reference count */
mxmlRetain(mxml_node_t *node)		/* I - Node */
{
  if (node)
    return (++ node->ref_count);
  else
    return (-1);
}


/*
 * 'mxml_new()' - Create a new node.
 */

static mxml_node_t *			/* O - New node */
mxml_new(mxml_node_t *parent,		/* I - Parent node */
         mxml_type_t type)		/* I - Node type */
{
  mxml_node_t	*node;			/* New node */


#if DEBUG > 1
  fprintf(stderr, "mxml_new(parent=%p, type=%d)\n", parent, type);
#endif /* DEBUG > 1 */

 /*
  * Allocate memory for the node...
  */

  if ((node = calloc(1, sizeof(mxml_node_t))) == NULL)
  {
#if DEBUG > 1
    fputs("    returning NULL\n", stderr);
#endif /* DEBUG > 1 */

    return (NULL);
  }

#if DEBUG > 1
  fprintf(stderr, "    returning %p\n", node);
#endif /* DEBUG > 1 */

 /*
  * Set the node type...
  */

  node->type      = type;
  node->ref_count = 1;

 /*
  * Add to the parent if present...
  */

  if (parent)
    mxmlAdd(parent, MXML_ADD_AFTER, MXML_ADD_TO_PARENT, node);

 /*
  * Return the new node...
  */

  return (node);
}


/*
 * End of "$Id: mxml-node.c 451 2014-01-04 21:50:06Z msweet $".
 */
