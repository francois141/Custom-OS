#ifndef LIBBARRELFISH_GAP_BUFFER_H
#define LIBBARRELFISH_GAP_BUFFER_H

#include <stdio.h>
#include <stddef.h>

struct gap_buffer {
    char *buf;          ///< pointer to the actual data
    size_t capacity;    ///< number of bytes the buffer could hold
    size_t size;        ///< number of bytes the buffer actually holds
    size_t cursor;      ///< points to the beginning of the gap (inclusive)

    size_t increment;
};

void gb_init(struct gap_buffer *gb);
void gb_resize(struct gap_buffer *gb, size_t capacity);
void gb_reinit_from_cstr(struct gap_buffer *gb, const char *str);

size_t gb_gap_end(struct gap_buffer *gb);

void gb_insert_at(struct gap_buffer *gb, size_t index, size_t len, char *buf);
void gb_erase_at(struct gap_buffer *gb, size_t index, size_t len);

void gb_insert_char_at(struct gap_buffer *gb, size_t index, char c);
void gb_erase_char_at(struct gap_buffer *gb, size_t index);

char *gb_to_cstr(struct gap_buffer *gb);
char *gb_release_to_cstr(struct gap_buffer *gb);

#endif  // LIBBARRELFISH_GAP_BUFFER_H
