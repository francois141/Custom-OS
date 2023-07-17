#ifndef LIBBARRELFISH_PATHUTIL_H
#define LIBBARRELFISH_PATHUTIL_H

#include <stdbool.h>

#define FS_ROOT_DIRECTORY "/"
#define FS_PATH_SEPARATOR '/'

bool pathutil_is_abs_path(const char *path);
bool pathutil_is_rel_path(const char *path);

bool pathutil_is_rel_directory(const char *abs, const char *path);
bool pathutil_is_directory(const char *path);

bool pathutil_concat_paths(const char *abs, const char *rel, char **res);
bool pathutil_sanitize_path(char *abspath, char **sanitized_path);


#endif  // LIBBARRELFISH_PATHUTIL_H