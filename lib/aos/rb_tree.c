
#include <aos/rb_tree.h>
#include <assert.h>
#include <aos/debug.h>

#ifndef MAX
#define MAX(a, b) ((a) > (b) ? (a) : (b))
#endif

// update the max_size of the node
static void _rb_node_update(struct rb_node *node)
{
    node->max_size = node->size;
    if (node->left)
        node->max_size = MAX(node->max_size, node->left->max_size);
    if (node->right)
        node->max_size = MAX(node->max_size, node->right->max_size);
}

// performs a left rotation, the right child of x becomes its parent and x becomes its left child
static void _rb_left_rotate(struct rb_tree *tree, struct rb_node *x)
{
    assert(x != NULL);

    struct rb_node *y = x->right;
    assert(y != NULL);

    // turn y's left subtree into x's right subtree
    x->right = y->left;
    if (x->right != NULL) {
        x->right->parent = x;
    }

    // link x's parent to y
    y->parent = x->parent;
    if (x->parent == NULL)
        // x was the root
        tree->root = y;
    else if (x == x->parent->left)
        x->parent->left = y;
    else
        x->parent->right = y;

    // put x on y's left
    y->left   = x;
    x->parent = y;

    // only x and y should have changed
    _rb_node_update(x);
    _rb_node_update(y);
}

// performs a right rotation, the left child of x becomes its parent and x becomes its right child
static void _rb_right_rotate(struct rb_tree *tree, struct rb_node *x)
{
    assert(x != NULL);

    struct rb_node *y = x->left;
    assert(y != NULL);

    // turn y's right subtree into x's left subtree
    x->left = y->right;
    if (x->left != NULL) {
        x->left->parent = x;
    }

    // link x's parent to y
    y->parent = x->parent;
    if (x->parent == NULL) {
        // x was the root
        tree->root = y;
    } else {
        if (x == x->parent->left)
            x->parent->left = y;
        else
            x->parent->right = y;
    }

    // put x on y's right
    y->right  = x;
    x->parent = y;

    // only x and y should have changed
    _rb_node_update(x);
    _rb_node_update(y);
}

// replaces u by v for u's parent point of view
static void _rb_transplant(struct rb_tree *tree, struct rb_node *u, struct rb_node *v)
{
    if (u->parent == NULL)
        tree->root = v;
    else if (u == u->parent->left)
        u->parent->left = v;
    else
        u->parent->right = v;

    if (v != NULL)
        v->parent = u->parent;
    if (u->parent != NULL)
        _rb_node_update(u->parent);
}

static bool _rb_node_check(struct rb_node *node)
{
    if (node == NULL)
        return true;

    // are the children fine?
    if (!_rb_node_check(node->left) || !_rb_node_check(node->right))
        return false;

    if (node->left != NULL && node->left->parent != node)
        return false;

    if (node->right != NULL && node->right->parent != node)
        return false;

    bool is_leaf          = node->left == NULL && node->right == NULL;
    bool has_two_children = node->left != NULL && node->right != NULL;

    // if the node is red, it is a leaf or both of its children are black
    if (node->is_red) {
        if (!is_leaf && (!has_two_children || node->left->is_red || node->right->is_red))
            return false;
    }

    size_t max_size = node->size;
    if (node->left)
        max_size = MAX(max_size, node->left->max_size);
    if (node->right)
        max_size = MAX(max_size, node->right->max_size);

    return max_size == node->max_size;
}

bool rb_tree_check(struct rb_tree *tree)
{
    return _rb_node_check(tree->root);
}


void rb_tree_init(struct rb_tree *tree)
{
    tree->root = NULL;
}

void rb_tree_insert(struct rb_tree *tree, struct rb_node *z)
{
    // perform the standard tree insertion first
    struct rb_node *y = NULL;
    struct rb_node *x = tree->root;

    while (x != NULL) {
        y = x;
        if (z->start < x->start) {
            x = x->left;
        } else {
            assert(z->start != x->start);
            x = x->right;
        }
    }

    z->parent = y;
    if (y == NULL) {
        // root
        tree->root = z;
    } else if (z->start < y->start) {
        y->left = z;
    } else {
        y->right = z;
    }

    z->max_size = z->size;
    z->left     = NULL;
    z->right    = NULL;
    z->is_red   = true;

    // red-black tree fixup
    while (z->parent != NULL && z->parent->is_red) {
        _rb_node_update(z->parent);
        _rb_node_update(z->parent->parent);

        if (z->parent == z->parent->parent->left) {
            y = z->parent->parent->right;
            if (y != NULL && y->is_red) {
                z->parent->is_red         = false;
                y->is_red                 = false;
                z->parent->parent->is_red = true;
                z                         = z->parent->parent;
            } else {
                if (z == z->parent->right) {
                    z = z->parent;
                    _rb_left_rotate(tree, z);
                }

                z->parent->is_red         = false;
                z->parent->parent->is_red = true;
                _rb_right_rotate(tree, z->parent->parent);
            }
        } else {
            y = z->parent->parent->left;
            if (y != NULL && y->is_red) {
                z->parent->is_red         = false;
                y->is_red                 = false;
                z->parent->parent->is_red = true;
                z                         = z->parent->parent;
            } else {
                if (z == z->parent->left) {
                    z = z->parent;
                    _rb_right_rotate(tree, z);
                }

                z->parent->is_red         = false;
                z->parent->parent->is_red = true;
                _rb_left_rotate(tree, z->parent->parent);
            }
        }
    }

    while (z != NULL) {
        _rb_node_update(z);
        z = z->parent;
    }

    tree->root->is_red = false;
}

// remove z from the tree while keeping all the other properties intact
void rb_tree_delete(struct rb_tree *tree, struct rb_node *z)
{
    struct rb_node *y = z;
    struct rb_node *x;
    bool            y_was_red = y->is_red;
    struct rb_node  dummy_node
        = { .parent = NULL, .left = NULL, .right = NULL, .size = 0, .max_size = 0, .is_red = false };

    if (z->left == NULL) {
        if (z->right == NULL)
            x = &dummy_node;
        else
            x = z->right;

        _rb_transplant(tree, z, x);
    } else if (z->right == NULL) {
        x = z->left;
        _rb_transplant(tree, z, x);
    } else {
        y = z->right;
        while (y->left != NULL)
            y = y->left;

        y_was_red = y->is_red;
        if (y->right == NULL)
            x = &dummy_node;
        else
            x = y->right;

        if (y->parent == z) {
            x->parent = y;
        } else {
            _rb_transplant(tree, y, x);
            y->right         = z->right;
            y->right->parent = y;
        }
        _rb_transplant(tree, z, y);
        y->left         = z->left;
        y->left->parent = y;
        y->is_red       = z->is_red;
    }
    struct rb_node *first_x = x;

    if (y_was_red) {
        // update the nodes on the way up and we're done
        while (x != NULL) {
            _rb_node_update(x);
            x = x->parent;
        }

        assert(dummy_node.left == NULL && dummy_node.right == NULL);
        if (first_x == &dummy_node)
            _rb_transplant(tree, first_x, NULL);
        return;
    }

    // red-black tree fixup
    while (x != tree->root && !x->is_red) {
        _rb_node_update(x);
        if (x == x->parent->left) {
            struct rb_node *w = x->parent->right;
            if (w->is_red) {
                w->is_red         = false;
                x->parent->is_red = true;
                _rb_left_rotate(tree, x->parent);
                w = x->parent->right;
            }

            bool w_right_black = w->right == NULL || !w->right->is_red;
            bool w_left_black  = w->left == NULL || !w->left->is_red;
            if (w_right_black && w_left_black) {
                w->is_red = true;
                x         = x->parent;
            } else {
                if (w_right_black) {
                    w->left->is_red = false;
                    w->is_red       = true;
                    _rb_right_rotate(tree, w);
                    w = x->parent->right;
                }

                w->is_red         = x->parent->is_red;
                x->parent->is_red = false;
                w->right->is_red  = false;
                _rb_left_rotate(tree, x->parent);
                break;
            }
        } else {
            struct rb_node *w = x->parent->left;
            if (w->is_red) {
                w->is_red         = false;
                x->parent->is_red = true;
                _rb_right_rotate(tree, x->parent);
                w = x->parent->left;
            }

            bool w_right_black = w->right == NULL || !w->right->is_red;
            bool w_left_black  = w->left == NULL || !w->left->is_red;
            if (w_right_black && w_left_black) {
                w->is_red = true;
                x         = x->parent;
            } else {
                if (w_left_black) {
                    w->right->is_red = false;
                    w->is_red        = true;
                    _rb_left_rotate(tree, w);
                    w = x->parent->left;
                }

                w->is_red         = x->parent->is_red;
                x->parent->is_red = false;
                w->left->is_red   = false;
                _rb_right_rotate(tree, x->parent);
                break;
            }
        }
    }
    x->is_red = false;

    // update remaining nodes
    while (x != NULL) {
        _rb_node_update(x);
        x = x->parent;
    }

    assert(dummy_node.left == NULL && dummy_node.right == NULL);
    if (first_x == &dummy_node)
        _rb_transplant(tree, first_x, NULL);

    if (tree->root)
        tree->root->is_red = false;
}

static struct rb_node *_rb_node_find_inner(struct rb_node *node, lvaddr_t addr)
{
    if (node == NULL)
        return NULL;

    if (addr < node->start)
        return _rb_node_find_inner(node->left, addr);
    else if (addr >= node->start + node->size)
        return _rb_node_find_inner(node->right, addr);
    else
        return node;
}

struct rb_node *rb_tree_find(struct rb_tree *tree, lvaddr_t addr)
{
    return _rb_node_find_inner(tree->root, addr);
}

static struct rb_node *_rb_node_find_minsize_inner(struct rb_node *node, size_t size)
{
    // apply a worst-fit strategy
    struct rb_node *candidate = NULL;
    size_t          best_size = SIZE_MAX;

    if (node->size >= size) {
        candidate = node;
        best_size = size;
    }

    if (node->left != NULL && node->left->max_size >= size
        && (candidate == NULL || node->left->max_size < best_size)) {
        candidate = node->left;
        best_size = node->left->max_size;
    }

    if (node->right != NULL && node->right->max_size >= size
        && (candidate == NULL || node->right->max_size < best_size)) {
        candidate = node->right;
        best_size = node->right->max_size;
    }

    if (candidate == node)
        return node;
    else
        return _rb_node_find_minsize_inner(candidate, size);
}

struct rb_node *rb_tree_find_minsize(struct rb_tree *tree, size_t size)
{
    // we know immediatly if it is possible or not
    if (tree->root == NULL || tree->root->max_size < size)
        return NULL;

    return _rb_node_find_minsize_inner(tree->root, size);
}

static struct rb_node *_rb_tree_find_greater_innner(struct rb_node *node, lvaddr_t addr)
{
    if (node == NULL)
        return NULL;

    if (node->start >= addr) {
        struct rb_node *candidate = _rb_tree_find_greater_innner(node->left, addr);
        return (candidate != NULL) ? candidate : node;
    } else {
        return _rb_tree_find_greater_innner(node->right, addr);
    }
}

struct rb_node *rb_tree_find_greater(struct rb_tree *tree, lvaddr_t addr)
{
    return _rb_tree_find_greater_innner(tree->root, addr);
}

static struct rb_node *_rb_tree_find_lower_innner(struct rb_node *node, lvaddr_t addr)
{
    if (node == NULL)
        return NULL;

    if (node->start <= addr) {
        struct rb_node *candidate = _rb_tree_find_lower_innner(node->right, addr);
        return (candidate != NULL) ? candidate : node;
    } else {
        return _rb_tree_find_lower_innner(node->left, addr);
    }
}


struct rb_node *rb_tree_find_lower(struct rb_tree *tree, lvaddr_t addr)
{
    return _rb_tree_find_lower_innner(tree->root, addr);
}

struct rb_node *rb_tree_successor(struct rb_node *node)
{
    if (node->right == NULL) {
        while (node->parent != NULL && node == node->parent->right)
            node = node->parent;

        return node->parent;
    }

    node = node->right;
    while (node->left != NULL)
        node = node->left;
    return node;
}

struct rb_node *rb_tree_predecessor(struct rb_node *node)
{
    if (node->left == NULL) {
        while (node->parent != NULL && node == node->parent->left)
            node = node->parent;

        return node->parent;
    }

    node = node->left;
    while (node->right != NULL)
        node = node->right;
    return node;
}

void rb_tree_update_size(struct rb_node *node, size_t size)
{
    assert(node != NULL);
    node->size = size;

    while (node != NULL) {
        _rb_node_update(node);
        node = node->parent;
    }
}

static void _rb_node_print(struct rb_node *node){
    if(node->left)
        _rb_node_print(node->left);

    debug_printf("Node start=%lx, size=%lx\n", node->start, node->size);

    if(node->right)
        _rb_node_print(node->right);
}

void rb_tree_print(struct rb_tree *tree){
    assert(tree != NULL);

    if(tree->root == NULL){
        debug_printf("Tree: empty\n");
        return;
    }

    debug_printf("Tree:\n");
    _rb_node_print(tree->root);
}
