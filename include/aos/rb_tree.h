
#ifndef LIBBARRELFISH_RB_TREE_H
#define LIBBARRELFISH_RB_TREE_H

#include <aos/types.h>
#include <stdbool.h>

// This is a self-contained red-black tree implementation
// A red black tree is a balanced binary tree, meaning that all the functions
// have a logarithmic worst case complexity
// This red-back tree is augmented: each node as a max_size field which keeps
// track of the max size among the size fields of all nodes in a given subtree

/// @brief A node in the red-black tree, if in the tree, its fields should only be read
/// The field start can be modified given that it does not modify the node position relative
/// to the other nodes. The field size should only be modified using the rb_tree_update_size method
/// The max_size field is automatically maintained
struct rb_node {
    struct rb_node *parent;
    struct rb_node *left;
    struct rb_node *right;

    lvaddr_t start;
    size_t   size;

    size_t max_size;
    bool   is_red;
};

/// @brief An augmented red-back tree
struct rb_tree {
    struct rb_node *root;
};

// Initialize the red-black tree, call before using the tree for the first time
void rb_tree_init(struct rb_tree *tree);

// Insert a node in the tree
// z should have been externally allocated and have its start and size fields already set
void rb_tree_insert(struct rb_tree *tree, struct rb_node *z);

// Remove a node from the tree
// z should point to a node already in the tree
void rb_tree_delete(struct rb_tree *tree, struct rb_node *z);

// Return the node containing the given address or NULL if none was found
struct rb_node *rb_tree_find(struct rb_tree *tree, lvaddr_t addr);

// Return a node of size at least size or NULL if none exists
// A worst-fit strategy is used
struct rb_node *rb_tree_find_minsize(struct rb_tree *tree, size_t size);

// return the first node wich starts at an addres greater or equal to addr
// or NULL if no such node exists
struct rb_node *rb_tree_find_greater(struct rb_tree *tree, lvaddr_t addr);

// return the first node wich starts at an addres lower or equal to addr
// or NULL if no such node exists
struct rb_node *rb_tree_find_lower(struct rb_tree *tree, lvaddr_t addr);

struct rb_node *rb_tree_successor(struct rb_node *node);

struct rb_node *rb_tree_predecessor(struct rb_node *node);

// Update the size of a node, without having to remove and re-insert it
void rb_tree_update_size(struct rb_node *node, size_t size);

// Helper function to check if a tree was corrupted
// Return true if the tree is fine
bool rb_tree_check(struct rb_tree *tree);

// Print the content of the tree using debug_printf
// The nodes are printed using an inorder walk
void rb_tree_print(struct rb_tree *tree);

#endif  // LIBBARRELFISH_RB_TREE_H