#include "pathutil.h"

#include <assert.h>
#include <stdbool.h>
#include <string.h>
#include <stdlib.h>

#include <aos/debug.h>
#include <aos/aos_rpc.h>

#include "../containers/dynamic_array.h"

bool pathutil_is_abs_path(const char *path)
{
    return path[0] == '/';
}

bool pathutil_is_rel_path(const char *path)
{
    return !pathutil_is_abs_path(path);
}

bool pathutil_is_rel_directory(const char *abs, const char *path)
{
    char *full_path;
    pathutil_concat_paths(abs, path, &full_path);
    bool is_directory = pathutil_is_directory(full_path);
    free(full_path);
    return is_directory;
}

bool pathutil_is_directory(const char *path)
{
    bool is_directory = false;
    errval_t err = aos_rpc_filesystem_is_directory(aos_rpc_get_filesystem_channel(), path, &is_directory);
    return !err_is_fail(err) && is_directory;
}

bool pathutil_concat_paths(const char *abs, const char *rel, char **res)
{
    assert(pathutil_is_abs_path(abs) && pathutil_is_rel_path(rel));
    string_builder_t sb;
    sb_init(&sb);
    size_t len = strlen(abs);
    sb_append_buf(&sb, len, abs);
    if (abs[len - 1] != FS_PATH_SEPARATOR) {
        sb_append_char(&sb, FS_PATH_SEPARATOR);
    }
    sb_append_str(&sb, rel);
    *res = sb_release_to_cstr(&sb);
    return true;
}

struct str_range {
    char  *beginptr;
    size_t len;
};

static bool _path_segment_append(struct dynamic_array *da, char *abspath, char *beginptr, size_t len, bool end)
{
    if (len == 0) {
        if (beginptr != abspath && !end) {
            da_free(da);
            return false;  ///< zero length "path-segment"
        }
        return true;
    }
    if (len == 2 && strncmp(beginptr, "..", strlen("..")) == 0) {
        if (da->size > 0) {
            da_pop(da, sizeof(struct str_range));
        }
    } else if (len == 1 && strncmp(beginptr, ".", strlen(".")) == 0) {
        /// XXX we don't need to do anything in this case...
    } else {
        struct str_range range = { .beginptr = beginptr, .len = len };
        da_append(da, sizeof(struct str_range), &range);
    }
    // printf("%.*s\n", len, beginptr);
    return true;
}

bool pathutil_sanitize_path(char *abspath, char **sanitized_path)
{
    assert(pathutil_is_abs_path(abspath));
    // printf("abspath: %s\n", abspath);
    char                *beginptr = abspath;
    char                *endptr   = abspath - 1;
    struct dynamic_array da;
    da_init(&da, sizeof(struct str_range) * 5);
    while ((endptr = strchr(endptr + 1, '/')) != NULL) {
        if (!_path_segment_append(&da, abspath, beginptr, (endptr - beginptr), /*end=*/false))
            return false;
        beginptr = endptr + 1;
    }
    if (!_path_segment_append(&da, abspath, beginptr, strlen(beginptr), /*end=*/true))
        return false;

    size_t segments     = da.size / (sizeof(struct str_range));
    size_t total_length = 0;
    for (size_t i = 0; i < segments; ++i) {
        // printf("%d\n", ((struct str_range *)da.buf)[i].len);
        total_length += ((struct str_range *)da.buf)[i].len;
    }
    size_t total_sz = sizeof(char)
                      * (total_length + /*path separators*/ (segments == 0 ? 1 : segments)
                         + /*null_byte*/ 1);
    *sanitized_path      = malloc(total_sz);
    (*sanitized_path)[0] = '/';
    char *curr           = &(*sanitized_path)[1];
    for (size_t i = 0; i < segments; ++i) {
        struct str_range range = ((struct str_range *)da.buf)[i];
        memcpy(curr, range.beginptr, range.len);
        curr += range.len;
        if (i != segments - 1)
            (*curr++) = FS_PATH_SEPARATOR;
    }
    *curr = '\0';
    da_free(&da);
    return true;
}