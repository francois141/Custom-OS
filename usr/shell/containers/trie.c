#include "trie.h"

#include <stdbool.h>
#include <stdlib.h>
#include <assert.h>
#include <string.h>

#include "dynamic_array.h"

void trie_init(struct trie *tr, size_t alpha, trie_encode_fn_t encode, trie_decode_fn_t decode)
{
    tr->root   = NULL;
    tr->alpha  = alpha;
    tr->encode = encode;
    tr->decode = decode;
}

static void _trie_alloc_node(struct trie *tr, struct trie_node **tn, char *key)
{
    assert(*tn == NULL);
    void *mem       = malloc(sizeof(struct trie_node) + tr->alpha * sizeof(struct trie_node **));
    *tn             = mem;
    (*tn)->children = mem + sizeof(struct trie_node);
    for (size_t i = 0; i < tr->alpha; ++i) {
        (*tn)->children[i] = NULL;
    }
    (*tn)->key  = key;
    (*tn)->data = NULL;
}

static struct trie_node **_trie_lookup_node(struct trie *tr, struct trie_node *tn, char c)
{
    assert(tn->children != NULL);
    assert(tr->encode(c) < tr->alpha);
    return &tn->children[tr->encode(c)];
}

static struct trie_node *_trie_lookup_internal(struct trie *tr, struct trie_node *tn, char *key,
                                               size_t len, size_t index, bool force)
{
    if (index == len - 1) {
        return tn;
    }
    struct trie_node **node = _trie_lookup_node(tr, tn, key[index]);
    if (*node == NULL) {
        if (!force)
            return NULL;
        _trie_alloc_node(tr, node, NULL);
    }
    return _trie_lookup_internal(tr, *node, key, len, index + 1, force);
}

void *trie_lookup(struct trie *tr, char *key)
{
    if (tr->root == NULL) {
        return NULL;
    }
    size_t len = strlen(key);
    if (len == 0) {
        return tr->root->data;
    }
    struct trie_node *tn = _trie_lookup_internal(tr, tr->root, key, len,
                                                 /*index=*/0, /*force=*/false);
    if (tn == NULL) {
        return NULL;
    }
    struct trie_node *node = *_trie_lookup_node(tr, tn, key[len - 1]);
    if (node == NULL) {
        return NULL;
    }
    return node->data;
}

bool trie_insert(struct trie *tr, char *key, void *value)
{
    if (tr->root == NULL) {
        _trie_alloc_node(tr, &tr->root, NULL);
    }
    size_t len = strlen(key);
    if (len == 0) {
        bool exists    = tr->root->data != NULL;
        tr->root->data = value;
        return exists;
    }
    struct trie_node *tn = _trie_lookup_internal(tr, tr->root, key, len,
                                                 /*index=*/0, /*force=*/true);
    assert(tn != NULL);
    struct trie_node **node = _trie_lookup_node(tr, tn, key[len - 1]);
    if (*node == NULL) {
        _trie_alloc_node(tr, node, NULL);
    }
    bool exists   = (*node)->data != NULL;
    if (exists) {
        free((*node)->data);
    }
    (*node)->key  = key;
    (*node)->data = value;
    return exists;
}

static void _trie_try_erase_node(struct trie *tr, struct trie_node **tn)
{
    bool remove = (*tn)->data == NULL;
    for (size_t i = 0; i < tr->alpha; ++i) {
        remove &= (*tn)->children[i] == NULL;
        if (!remove)
            break;
    }
    if (remove) {
        free(*tn);
        *tn = NULL;
    }
}

static bool _trie_erase_internal(struct trie *tr, struct trie_node **tn, char *key, size_t len,
                                 size_t index)
{
    if (index == len) {
        bool exists = (*tn)->data != NULL;
        (*tn)->data = NULL;
        _trie_try_erase_node(tr, tn);
        return exists;
    }
    struct trie_node **node = _trie_lookup_node(tr, *tn, key[index]);
    if (*node == NULL) {
        return false;
    }
    bool exists = _trie_erase_internal(tr, node, key, len, index + 1);
    if (exists) {
        _trie_try_erase_node(tr, tn);
    }
    return exists;
}

bool trie_erase(struct trie *tr, char *key)
{
    if (tr->root == NULL) {
        return false;
    }
    size_t len    = strlen(key);
    bool   exists = _trie_erase_internal(tr, &tr->root, key, len, 0);
    return exists;
}

static void _trie_iter_internal(struct trie *tr, struct trie_node *tn, trie_iter_fn_t iter,
                                void *acc)
{
    if (tn->data != NULL) {
        iter(acc, tn->key, tn->data);
    }
    for (size_t i = 0; i < tr->alpha; ++i) {
        if (tn->children[i] != NULL) {
            _trie_iter_internal(tr, tn->children[i], iter, acc);
        }
    }
}

void trie_iter(struct trie *tr, trie_iter_fn_t iter, void *acc)
{
    if (tr->root == NULL) {
        return;
    }
    _trie_iter_internal(tr, tr->root, iter, acc);
}

void trie_iter_from(struct trie *tr, trie_iter_fn_t iter, char *substr, void *acc)
{
    if (tr->root == NULL) {
        return;
    }
    size_t len = strlen(substr);
    if (len == 0) {
        return trie_iter(tr, iter, acc);
    }
    struct trie_node *tn = _trie_lookup_internal(tr, tr->root, substr, len, 0,
                                                 /*force=*/false);
    if (tn == NULL) {
        return;
    }
    struct trie_node *node = *_trie_lookup_node(tr, tn, substr[len - 1]);
    if (node == NULL) {
        return;
    }
    _trie_iter_internal(tr, node, iter, acc);
}

struct trie_find_acc {
    size_t offset;
    char  *value;
    size_t count;
};

static void _trie_find_next_iternal(void *acc, char *key, void *value)
{
    (void)value;
    struct trie_find_acc *tra = acc;
    if (tra->value == NULL && tra->offset == 0) {
        tra->value = key;
    } else {
        --tra->offset;
    }
    ++tra->count;
}

struct trie_find_result trie_find_next(struct trie *tr, char *substr, size_t offset)
{
    struct trie_find_acc acc = { .offset = offset, .value = NULL, .count = 0 };
    trie_iter_from(tr, _trie_find_next_iternal, substr, &acc);
    struct trie_find_result tfr = { .value = acc.value, .count = acc.count };
    return tfr;
}

static void _trie_collect_internal(void *acc, char *key, void *value)
{
    (void)value;
    struct dynamic_array *da = acc;
    da_append(da, sizeof(char *), &key);
}

size_t trie_collect(struct trie *tr, char *substr, char ***opts)
{
    struct dynamic_array da;
    da_init(&da, 16 * sizeof(char *));
    trie_iter_from(tr, _trie_collect_internal, substr, &da);
    size_t size = da.size / sizeof(char *);
    *opts = da.buf; // now we do not need to free the buf.
    return size;
}
