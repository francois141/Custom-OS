#include "gap_buffer.h"

#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <stdbool.h>

#define GB_DEBUG 0
#define GB_DEFAULT_INCREMENT 63

size_t gb_gap_end(struct gap_buffer *gb) {
    size_t excess = gb->capacity - gb->size;
    return gb->cursor + excess;
}

static void _gb_dbg_print(struct gap_buffer *gb) {
#if GB_DEBUG
    printf("size: %zu, capacity: %zu, cusor: %zu, end: %zu |", gb->size, gb->capacity, 
           gb->cursor, gb_gap_end(gb));
    if (gb->buf == NULL) {
        printf("(empty)\n");
        return;
    }
    size_t gap_beg = gb->cursor;
    size_t gap_end = gb_gap_end(gb);
    for (size_t i = 0; i < gb->capacity; ++i) {
        if (i == gap_beg)
            printf("[");
        if (i >= gap_beg && i < gap_end) {
            printf(" ");
            // printf("%c", gb->buf[i]);
            // printf("%d: %c", i, gb->buf[i]);
        } else { 
            printf("%c", gb->buf[i]);
            // printf("%d: %c", i, gb->buf[i]);
        }
        if (i + 1 == gap_end)
            printf("]");
    }
    printf("|\n");
#else
    (void) gb;
#endif
}

void gb_resize(struct gap_buffer *gb, size_t capacity) {
    gb->capacity = capacity;
    gb->buf = realloc(gb->buf, capacity);
    if (gb->buf == NULL && capacity > 0) {
        assert(false); ///< not much we can do in this case...
    }
}

static void _gb_move_cursor(struct gap_buffer *gb, size_t position) {
    assert(position <= gb->size);
    if (position < gb->cursor) {
        size_t len = gb->cursor - position;
        memcpy(gb->buf + (gb_gap_end(gb) - len), gb->buf + (gb->cursor - len), len);
    } else if (position > gb->cursor) {
        memcpy(gb->buf + gb->cursor, gb->buf + gb_gap_end(gb),
               position - gb->cursor);
    }
    gb->cursor = position;
}

static inline void _gb_ensure_capacity(struct gap_buffer *gb, size_t size_inc) {
    if (gb->capacity <= gb->size + size_inc) {
        _gb_move_cursor(gb, gb->size);
        gb_resize(gb, ((gb->size + size_inc + gb->increment - 1)
                        / gb->increment) * gb->increment);
    }
}

void gb_init(struct gap_buffer *gb) {
    gb->buf = NULL;
    gb->capacity = 0;
    gb->size = 0;
    gb->cursor = 0;
    gb->increment = GB_DEFAULT_INCREMENT;
}

void gb_reinit_from_cstr(struct gap_buffer *gb, const char *str) {
    size_t len = strlen(str);
    assert(len <= gb->capacity || gb->buf == NULL);
    if (len <= gb->capacity) {
        memcpy(gb->buf, str, len);
        gb->size = len;
        gb->cursor = len;
        return;
    }
    // XXX will edit the string right away, there is now point in initializing with the same size
    gb->buf = malloc(sizeof(char) * (len + GB_DEFAULT_INCREMENT));
    memcpy(gb->buf, str, len);
    gb->size = len;
    gb->capacity = len + GB_DEFAULT_INCREMENT;
    gb->cursor = len;
    gb->increment = GB_DEFAULT_INCREMENT;
}

void gb_insert_at(struct gap_buffer *gb, size_t index, size_t len, char *buf) {
    assert(index <= gb->size);
    _gb_ensure_capacity(gb, 1);
    _gb_move_cursor(gb, index);
    memcpy(gb->buf + gb->cursor, buf, len);
    gb->cursor += len;
    gb->size += len;

    _gb_dbg_print(gb);
}

void gb_erase_at(struct gap_buffer *gb, size_t index, size_t len) {
    assert(index <= gb->size - len);
    _gb_move_cursor(gb, index + len);
    gb->cursor -= len;
    gb->size -= len;
    
    _gb_dbg_print(gb);
}

void gb_insert_char_at(struct gap_buffer *gb, size_t index, char c) {
    gb_insert_at(gb, index, 1, &c);
}

void gb_erase_char_at(struct gap_buffer *gb, size_t index) {
    assert(index < gb->size);
    _gb_move_cursor(gb, index + 1);
    --gb->cursor;
    --gb->size;

}

char *gb_to_cstr(struct gap_buffer *gb) {
    gb_insert_char_at(gb, gb->size, '\0');
    --gb->size; // don't count the NULL towards the size
    return gb->buf;
}

char *gb_release_to_cstr(struct gap_buffer *gb) {
    gb_to_cstr(gb);
    gb_resize(gb, gb->size + 1);
    char *str = gb->buf;
    gb_init(gb);
    return str;
}