#ifndef LIBBARRELFISH_DYNAMIC_ARRAY_H
#define LIBBARRELFISH_DYNAMIC_ARRAY_H

#include <stdio.h>
#include <stddef.h>

struct dynamic_array {
    void *buf;          ///< pointer to the actual data
    size_t capacity;    ///< number of bytes the array could hold
    size_t size;        ///< number of bytes contained in the array
    size_t increment;   ///< number of bytes to extend the buffer by
};

void da_init(struct dynamic_array *da, size_t increment);
void da_append(struct dynamic_array *sb, size_t size, const void *buf);
void da_pop(struct dynamic_array *sb, size_t size);
void *da_release(struct dynamic_array *da);
void da_free(struct dynamic_array *da);

typedef struct dynamic_array string_builder_t;

void sb_init(string_builder_t *sb);
void sb_clear(string_builder_t *sb);
void sb_append_char(string_builder_t*sb, char c);
void sb_append_buf(string_builder_t *sb, size_t size, const char *str);
void sb_append_str(string_builder_t *sb, const char *str);
void sb_pop(string_builder_t *sb);

char *sb_release_to_cstr(string_builder_t *sb);
char *sb_to_cstr(string_builder_t *sb);

#endif  // LIBBARRELFISH_DYNAMIC_ARRAY_H
