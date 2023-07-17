#include "dynamic_array.h"

#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <stdbool.h>

static void _da_resize(struct dynamic_array *da, size_t capacity) {
    da->capacity = capacity;
    da->buf = realloc(da->buf, capacity);
    if (da->buf == NULL && capacity > 0) {
        assert(false); ///< not much we can do in this case...
    }
}

static void _da_ensure_capacity(struct dynamic_array *da, size_t size_inc) {
    if (da->capacity <= da->size + size_inc) {
        _da_resize(da, ((da->size + size_inc + da->increment - 1) 
                        / da->increment) * da->increment);
    }
}

void da_init(struct dynamic_array *da, size_t increment) {
    da->buf = NULL;
    da->capacity = 0;
    da->size = 0;
    da->increment = increment;
}

void da_append(struct dynamic_array *da, size_t size, const void *buf) {
    _da_ensure_capacity(da, size);
    memcpy(da->buf + da->size, buf, size);
    da->size += size;
}

void da_pop(struct dynamic_array *da, size_t size) {
    assert(da->size >= size);
    da->size -= size;
}

void *da_release(struct dynamic_array *da) {
    void *buf = da->buf;
    da_init(da, da->increment);
    return buf;
}

void da_free(struct dynamic_array *da) {
    free(da->buf);
}

// StringBuilder
void sb_init(string_builder_t *sb) {
    da_init(sb, 32);
}

void sb_clear(string_builder_t *sb) {
    ((struct dynamic_array *)sb)->size = 0;
}

void sb_append_char(string_builder_t *sb, char c) {
    da_append(sb, sizeof(char), &c);
}

void sb_append_buf(string_builder_t *sb, size_t size, const char *str) {
    da_append(sb, sizeof(char) * size, str);
}

void sb_append_str(string_builder_t *sb, const char *str) {
    da_append(sb, sizeof(char) * strlen(str), str);
}

void sb_pop(string_builder_t *sb) {
    da_pop(sb, sizeof(char));
}

char *sb_release_to_cstr(string_builder_t *sb) {
    sb_append_char(sb, '\0');
    return (char *) da_release(sb);
}

char *sb_to_cstr(string_builder_t *sb) {
    sb_append_char(sb, '\0');
    --sb->size; // don't keep track of the null character
    return sb->buf;
}