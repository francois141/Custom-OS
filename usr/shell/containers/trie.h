#ifndef LIBBARRELFISH_TRIE_H
#define LIBBARRELFISH_TRIE_H

#include <stddef.h>
#include <stdbool.h>

struct trie_node {
    struct trie_node **children;  ///< children of the trie node (one for each char of alphabet)
    char              *key;
    void              *data;  ///< data contained in the trie node (is_leaf iff. data != NULL)
};

typedef size_t (*trie_encode_fn_t)(char c);
typedef char (*trie_decode_fn_t)(size_t index);
typedef void (*trie_iter_fn_t)(void *acc, char *key, void *value);

struct trie {
    struct trie_node *root;   ///< root of the trie
    size_t            alpha;  ///< size of the alphabet.

    trie_encode_fn_t encode;  ///< from char of the alphabet to index
    trie_decode_fn_t decode;  ///< from index to char of the alphabet
};

void trie_init(struct trie *tr, size_t alpha_size, trie_encode_fn_t encode, trie_decode_fn_t decode);
void *trie_lookup(struct trie *tr, char *key);
bool  trie_insert(struct trie *tr, char *key, void *value);
bool  trie_erase(struct trie *tr, char *key);

void trie_iter(struct trie *tr, trie_iter_fn_t iter, void *acc);
void trie_iter_from(struct trie *tr, trie_iter_fn_t iter, char *substr, void *acc);

struct trie_find_result {
    char *value;
    size_t count;
};

// finds alphabetically first key with substr as prefix.
struct trie_find_result trie_find_next(struct trie *tr, char *substr, size_t offset);
size_t trie_collect(struct trie *tr, char *substr, char ***opts);

#endif  // LIBBARRELFISH_TRIE_H
