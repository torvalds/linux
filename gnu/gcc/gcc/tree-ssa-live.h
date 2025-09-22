/* Routines for liveness in SSA trees.
   Copyright (C) 2003, 2004, 2005 Free Software Foundation, Inc.
   Contributed by Andrew MacLeod  <amacleod@redhat.com>

This file is part of GCC.

GCC is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2, or (at your option)
any later version.

GCC is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with GCC; see the file COPYING.  If not, write to
the Free Software Foundation, 51 Franklin Street, Fifth Floor,
Boston, MA 02110-1301, USA.  */


#ifndef _TREE_SSA_LIVE_H
#define _TREE_SSA_LIVE_H 1

#include "partition.h"
#include "vecprim.h"

/* Used to create the variable mapping when we go out of SSA form.  */
typedef struct _var_map
{
  /* The partition of all variables.  */
  partition var_partition;

  /* Vector for compacting partitions.  */
  int *partition_to_compact;
  int *compact_to_partition;

  /* Mapping of partition numbers to vars.  */
  tree *partition_to_var;

  /* Current number of partitions.  */
  unsigned int num_partitions;

  /* Original partition size.  */
  unsigned int partition_size;

  /* Reference count, if required.  */
  int *ref_count;
} *var_map;

#define VAR_ANN_PARTITION(ann) (ann->partition)
#define VAR_ANN_ROOT_INDEX(ann) (ann->root_index)

#define NO_PARTITION		-1

/* Flags to pass to compact_var_map  */

#define VARMAP_NORMAL		0
#define VARMAP_NO_SINGLE_DEFS	1

extern var_map init_var_map (int);
extern void delete_var_map (var_map);
extern void dump_var_map (FILE *, var_map);
extern int var_union (var_map, tree, tree);
extern void change_partition_var (var_map, tree, int);
extern void compact_var_map (var_map, int);
#ifdef ENABLE_CHECKING
extern void register_ssa_partition_check (tree ssa_var);
#endif

static inline unsigned num_var_partitions (var_map);
static inline tree var_to_partition_to_var (var_map, tree);
static inline tree partition_to_var (var_map, int);
static inline int var_to_partition (var_map, tree);
static inline tree version_to_var (var_map, int);
static inline int version_ref_count (var_map, tree);
static inline void register_ssa_partition (var_map, tree, bool);

#define SSA_VAR_MAP_REF_COUNT	 0x01
extern var_map create_ssa_var_map (int);

/* Number of partitions in MAP.  */

static inline unsigned
num_var_partitions (var_map map)
{
  return map->num_partitions;
}


/* Return the reference count for SSA_VAR's partition in MAP.  */

static inline int
version_ref_count (var_map map, tree ssa_var)
{
  int version = SSA_NAME_VERSION (ssa_var);
  gcc_assert (map->ref_count);
  return map->ref_count[version];
}
 

/* Given partition index I from MAP, return the variable which represents that 
   partition.  */
 
static inline tree
partition_to_var (var_map map, int i)
{
  if (map->compact_to_partition)
    i = map->compact_to_partition[i];
  i = partition_find (map->var_partition, i);
  return map->partition_to_var[i];
}


/* Given ssa_name VERSION, if it has a partition in MAP,  return the var it 
   is associated with.  Otherwise return NULL.  */

static inline tree version_to_var (var_map map, int version)
{
  int part;
  part = partition_find (map->var_partition, version);
  if (map->partition_to_compact)
    part = map->partition_to_compact[part];
  if (part == NO_PARTITION)
    return NULL_TREE;
  
  return partition_to_var (map, part);
}
 

/* Given VAR, return the partition number in MAP which contains it.  
   NO_PARTITION is returned if it's not in any partition.  */

static inline int
var_to_partition (var_map map, tree var)
{
  var_ann_t ann;
  int part;

  if (TREE_CODE (var) == SSA_NAME)
    {
      part = partition_find (map->var_partition, SSA_NAME_VERSION (var));
      if (map->partition_to_compact)
	part = map->partition_to_compact[part];
    }
  else
    {
      ann = var_ann (var);
      if (ann->out_of_ssa_tag)
	part = VAR_ANN_PARTITION (ann);
      else
        part = NO_PARTITION;
    }
  return part;
}


/* Given VAR, return the variable which represents the entire partition
   it is a member of in MAP.  NULL is returned if it is not in a partition.  */

static inline tree
var_to_partition_to_var (var_map map, tree var)
{
  int part;

  part = var_to_partition (map, var);
  if (part == NO_PARTITION)
    return NULL_TREE;
  return partition_to_var (map, part);
}


/* This routine registers a partition for SSA_VAR with MAP.  IS_USE is used 
   to count references.  Any unregistered partitions may be compacted out 
   later.  */ 

static inline void
register_ssa_partition (var_map map, tree ssa_var, bool is_use)
{
  int version;

#if defined ENABLE_CHECKING
  register_ssa_partition_check (ssa_var);
#endif

  version = SSA_NAME_VERSION (ssa_var);
  if (is_use && map->ref_count)
    map->ref_count[version]++;

  if (map->partition_to_var[version] == NULL_TREE)
    map->partition_to_var[SSA_NAME_VERSION (ssa_var)] = ssa_var;
}


/*  ---------------- live on entry/exit info ------------------------------  

    This structure is used to represent live range information on SSA based
    trees. A partition map must be provided, and based on the active partitions,
    live-on-entry information and live-on-exit information can be calculated.
    As well, partitions are marked as to whether they are global (live 
    outside the basic block they are defined in).

    The live-on-entry information is per variable. It provide a bitmap for 
    each variable which has a bit set for each basic block that the variable
    is live on entry to that block.

    The live-on-exit information is per block. It provides a bitmap for each
    block indicating which partitions are live on exit from the block.

    For the purposes of this implementation, we treat the elements of a PHI 
    as follows:

       Uses in a PHI are considered LIVE-ON-EXIT to the block from which they
       originate. They are *NOT* considered live on entry to the block
       containing the PHI node.

       The Def of a PHI node is *not* considered live on entry to the block.
       It is considered to be "define early" in the block. Picture it as each
       block having a stmt (or block-preheader) before the first real stmt in 
       the block which defines all the variables that are defined by PHIs.
   
    -----------------------------------------------------------------------  */


typedef struct tree_live_info_d
{
  /* Var map this relates to.  */
  var_map map;

  /* Bitmap indicating which partitions are global.  */
  bitmap global;

  /* Bitmap of live on entry blocks for partition elements.  */
  bitmap *livein;

  /* Number of basic blocks when live on exit calculated.  */
  int num_blocks;

  /* Bitmap of what variables are live on exit for a basic blocks.  */
  bitmap *liveout;
} *tree_live_info_p;


extern tree_live_info_p calculate_live_on_entry (var_map);
extern void calculate_live_on_exit (tree_live_info_p);
extern void delete_tree_live_info (tree_live_info_p);

#define LIVEDUMP_ENTRY	0x01
#define LIVEDUMP_EXIT	0x02
#define LIVEDUMP_ALL	(LIVEDUMP_ENTRY | LIVEDUMP_EXIT)
extern void dump_live_info (FILE *, tree_live_info_p, int);

static inline int partition_is_global (tree_live_info_p, int);
static inline bitmap live_entry_blocks (tree_live_info_p, int);
static inline bitmap live_on_exit (tree_live_info_p, basic_block);
static inline var_map live_var_map (tree_live_info_p);
static inline void live_merge_and_clear (tree_live_info_p, int, int);
static inline void make_live_on_entry (tree_live_info_p, basic_block, int);


/*  Return TRUE if P is marked as a global in LIVE.  */

static inline int
partition_is_global (tree_live_info_p live, int p)
{
  gcc_assert (live->global);
  return bitmap_bit_p (live->global, p);
}


/* Return the bitmap from LIVE representing the live on entry blocks for 
   partition P.  */

static inline bitmap
live_entry_blocks (tree_live_info_p live, int p)
{
  gcc_assert (live->livein);
  return live->livein[p];
}


/* Return the bitmap from LIVE representing the live on exit partitions from
   block BB.  */

static inline bitmap
live_on_exit (tree_live_info_p live, basic_block bb)
{
  gcc_assert (live->liveout);
  gcc_assert (bb != ENTRY_BLOCK_PTR);
  gcc_assert (bb != EXIT_BLOCK_PTR);

  return live->liveout[bb->index];
}


/* Return the partition map which the information in LIVE utilizes.  */

static inline var_map 
live_var_map (tree_live_info_p live)
{
  return live->map;
}


/* Merge the live on entry information in LIVE for partitions P1 and P2. Place
   the result into P1.  Clear P2.  */

static inline void 
live_merge_and_clear (tree_live_info_p live, int p1, int p2)
{
  bitmap_ior_into (live->livein[p1], live->livein[p2]);
  bitmap_zero (live->livein[p2]);
}


/* Mark partition P as live on entry to basic block BB in LIVE.  */

static inline void 
make_live_on_entry (tree_live_info_p live, basic_block bb , int p)
{
  bitmap_set_bit (live->livein[p], bb->index);
  bitmap_set_bit (live->global, p);
}


/* A tree_partition_associator (TPA)object is a base structure which allows
   partitions to be associated with a tree object.

   A varray of tree elements represent each distinct tree item.
   A parallel int array represents the first partition number associated with 
   the tree.
   This partition number is then used as in index into the next_partition
   array, which returns the index of the next partition which is associated
   with the tree. TPA_NONE indicates the end of the list.  
   A varray paralleling the partition list 'partition_to_tree_map' is used
   to indicate which tree index the partition is in.  */

typedef struct tree_partition_associator_d
{
  VEC(tree,heap) *trees;
  VEC(int,heap) *first_partition;
  int *next_partition;
  int *partition_to_tree_map;
  int num_trees;
  int uncompressed_num;
  var_map map;
} *tpa_p;

/* Value returned when there are no more partitions associated with a tree.  */
#define TPA_NONE		-1

static inline tree tpa_tree (tpa_p, int);
static inline int tpa_first_partition (tpa_p, int);
static inline int tpa_next_partition (tpa_p, int);
static inline int tpa_num_trees (tpa_p);
static inline int tpa_find_tree (tpa_p, int);
static inline void tpa_decompact (tpa_p);
extern void tpa_delete (tpa_p);
extern void tpa_dump (FILE *, tpa_p);
extern void tpa_remove_partition (tpa_p, int, int);
extern int tpa_compact (tpa_p);


/* Return the number of distinct tree nodes in TPA.  */

static inline int
tpa_num_trees (tpa_p tpa)
{
  return tpa->num_trees;
}


/* Return the tree node for index I in TPA.  */

static inline tree
tpa_tree (tpa_p tpa, int i)
{
  return VEC_index (tree, tpa->trees, i);
}


/* Return the first partition associated with tree list I in TPA.  */

static inline int
tpa_first_partition (tpa_p tpa, int i)
{
  return VEC_index (int, tpa->first_partition, i);
}


/* Return the next partition after partition I in TPA's list.  */

static inline int
tpa_next_partition (tpa_p tpa, int i)
{
  return tpa->next_partition[i];
}


/* Return the tree index from TPA whose list contains partition I.  
   TPA_NONE is returned if I is not associated with any list.  */

static inline int 
tpa_find_tree (tpa_p tpa, int i)
{
  int index;

  index = tpa->partition_to_tree_map[i];
  /* When compressed, any index higher than the number of tree elements is 
     a compressed element, so return TPA_NONE.  */
  if (index != TPA_NONE && index >= tpa_num_trees (tpa))
    {
      gcc_assert (tpa->uncompressed_num != -1);
      index = TPA_NONE;
    }

  return index;
}


/* This function removes any compaction which was performed on TPA.  */

static inline void 
tpa_decompact(tpa_p tpa)
{
  gcc_assert (tpa->uncompressed_num != -1);
  tpa->num_trees = tpa->uncompressed_num;
}


/* Once a var_map has been created and compressed, a complementary root_var
   object can be built.  This creates a list of all the root variables from
   which ssa version names are derived.  Each root variable has a list of 
   which partitions are versions of that root.  

   This is implemented using the tree_partition_associator.

   The tree vector is used to represent the root variable.
   The list of partitions represent SSA versions of the root variable.  */

typedef tpa_p root_var_p;

static inline tree root_var (root_var_p, int);
static inline int root_var_first_partition (root_var_p, int);
static inline int root_var_next_partition (root_var_p, int);
static inline int root_var_num (root_var_p);
static inline void root_var_dump (FILE *, root_var_p);
static inline void root_var_remove_partition (root_var_p, int, int);
static inline void root_var_delete (root_var_p);
static inline int root_var_find (root_var_p, int);
static inline int root_var_compact (root_var_p);
static inline void root_var_decompact (tpa_p);

extern root_var_p root_var_init (var_map);

/* Value returned when there are no more partitions associated with a root
   variable.  */
#define ROOT_VAR_NONE		TPA_NONE


/* Return the number of distinct root variables in RV.  */

static inline int 
root_var_num (root_var_p rv)
{
  return tpa_num_trees (rv);
}


/* Return root variable I from RV.  */

static inline tree
root_var (root_var_p rv, int i)
{
  return tpa_tree (rv, i);
}


/* Return the first partition in RV belonging to root variable list I.  */

static inline int
root_var_first_partition (root_var_p rv, int i)
{
  return tpa_first_partition (rv, i);
}


/* Return the next partition after partition I in a root list from RV.  */

static inline int
root_var_next_partition (root_var_p rv, int i)
{
  return tpa_next_partition (rv, i);
}


/* Send debug info for root_var list RV to file F.  */

static inline void
root_var_dump (FILE *f, root_var_p rv)
{
  fprintf (f, "\nRoot Var dump\n");
  tpa_dump (f, rv);
  fprintf (f, "\n");
}


/* Destroy root_var object RV.  */

static inline void
root_var_delete (root_var_p rv)
{
  tpa_delete (rv);
}


/* Remove partition PARTITION_INDEX from root_var list ROOT_INDEX in RV.  */

static inline void
root_var_remove_partition (root_var_p rv, int root_index, int partition_index)
{
  tpa_remove_partition (rv, root_index, partition_index);
}


/* Return the root_var list index for partition I in RV.  */

static inline int
root_var_find (root_var_p rv, int i)
{
  return tpa_find_tree (rv, i);
}


/* Hide single element lists in RV.  */

static inline int 
root_var_compact (root_var_p rv)
{
  return tpa_compact (rv);
}


/* Expose the single element lists in RV.  */

static inline void
root_var_decompact (root_var_p rv)
{
  tpa_decompact (rv);
}


/* A TYPE_VAR object is similar to a root_var object, except this associates 
   partitions with their type rather than their root variable.  This is used to 
   coalesce memory locations based on type.  */

typedef tpa_p type_var_p;

static inline tree type_var (type_var_p, int);
static inline int type_var_first_partition (type_var_p, int);
static inline int type_var_next_partition (type_var_p, int);
static inline int type_var_num (type_var_p);
static inline void type_var_dump (FILE *, type_var_p);
static inline void type_var_remove_partition (type_var_p, int, int);
static inline void type_var_delete (type_var_p);
static inline int type_var_find (type_var_p, int);
static inline int type_var_compact (type_var_p);
static inline void type_var_decompact (type_var_p);

extern type_var_p type_var_init (var_map);

/* Value returned when there is no partitions associated with a list.  */
#define TYPE_VAR_NONE		TPA_NONE


/* Return the number of distinct type lists in TV.  */

static inline int 
type_var_num (type_var_p tv)
{
  return tpa_num_trees (tv);
}


/* Return the type of list I in TV.  */

static inline tree
type_var (type_var_p tv, int i)
{
  return tpa_tree (tv, i);
}


/* Return the first partition belonging to type list I in TV.  */

static inline int
type_var_first_partition (type_var_p tv, int i)
{
  return tpa_first_partition (tv, i);
}


/* Return the next partition after partition I in a type list within TV.  */

static inline int
type_var_next_partition (type_var_p tv, int i)
{
  return tpa_next_partition (tv, i);
}


/* Send debug info for type_var object TV to file F.  */

static inline void
type_var_dump (FILE *f, type_var_p tv)
{
  fprintf (f, "\nType Var dump\n");
  tpa_dump (f, tv);
  fprintf (f, "\n");
}


/* Delete type_var object TV.  */

static inline void
type_var_delete (type_var_p tv)
{
  tpa_delete (tv);
}


/* Remove partition PARTITION_INDEX from type list TYPE_INDEX in TV.  */

static inline void
type_var_remove_partition (type_var_p tv, int type_index, int partition_index)
{
  tpa_remove_partition (tv, type_index, partition_index);
}


/* Return the type index in TV for the list partition I is in.  */

static inline int
type_var_find (type_var_p tv, int i)
{
  return tpa_find_tree (tv, i);
}


/* Hide single element lists in TV.  */

static inline int 
type_var_compact (type_var_p tv)
{
  return tpa_compact (tv);
}


/* Expose single element lists in TV.  */

static inline void
type_var_decompact (type_var_p tv)
{
  tpa_decompact (tv);
}

/* This set of routines implements a coalesce_list. This is an object which
   is used to track pairs of partitions which are desirable to coalesce
   together at some point.  Costs are associated with each pair, and when 
   all desired information has been collected, the object can be used to 
   order the pairs for processing.  */

/* This structure defines a pair for coalescing.  */

typedef struct partition_pair_d
{
  int first_partition;
  int second_partition;
  int cost;
  struct partition_pair_d *next;
} *partition_pair_p;

/* This structure maintains the list of coalesce pairs.  
   When add_mode is true, list is a triangular shaped list of coalesce pairs.
   The smaller partition number is used to index the list, and the larger is
   index is located in a partition_pair_p object. These lists are sorted from 
   smallest to largest by 'second_partition'.  New coalesce pairs are allowed
   to be added in this mode.
   When add_mode is false, the lists have all been merged into list[0]. The
   rest of the lists are not used. list[0] is ordered from most desirable
   coalesce to least desirable. pop_best_coalesce() retrieves the pairs
   one at a time.  */

typedef struct coalesce_list_d 
{
  var_map map;
  partition_pair_p *list;
  bool add_mode;
} *coalesce_list_p;

extern coalesce_list_p create_coalesce_list (var_map);
extern void add_coalesce (coalesce_list_p, int, int, int);
extern int coalesce_cost (int, bool, bool);
extern void sort_coalesce_list (coalesce_list_p);
extern void dump_coalesce_list (FILE *, coalesce_list_p);
extern void delete_coalesce_list (coalesce_list_p);

#define NO_BEST_COALESCE	-1

extern conflict_graph build_tree_conflict_graph (tree_live_info_p, tpa_p,
						 coalesce_list_p);
extern void coalesce_tpa_members (tpa_p tpa, conflict_graph graph, var_map map,
				  coalesce_list_p cl, FILE *);


#endif /* _TREE_SSA_LIVE_H  */
