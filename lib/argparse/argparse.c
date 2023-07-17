#include <argparse/argparse.h>

static size_t _argv_size(const char *arg)
{
    // XXX we cannot just use strlen:
    //  - if we find spaces we need to add quotes
    //  - if we find quotes, or backslash, we need to escape
    size_t len      = strlen(arg);
    bool   enquoted = false;  ///< do we need to quote?
    size_t escaped  = 0;
    for (size_t i = 0; i < len; ++i) {
        if (arg[i] == ' ') {
            enquoted = true;
        } else if (arg[i] == '\\' || arg[i] == '"') {
            ++escaped;
        }
    }
    return len + (enquoted ? 2 : 0) + escaped;
}

static size_t _cmdline_size(int argc, const char *argv[])
{
    if (argc == 0) {
        return 1;
    }
    size_t size = 0;
    for (int i = 0; i < argc; ++i) {
        size += _argv_size(argv[i]) + 1;  ///< separating space / trailing NULL
    }
    return size;
}

static void _cmdline_append(char *buf, size_t *size, const char *arg)
{
    bool   enquote = false;
    size_t len     = strlen(arg);
    size_t i = 0, j = 0;
    for (; i < len; ++i, ++j) {
        char c = arg[i];
        if (c == ' ') {
            enquote = true;
        } else if (c == '\\' || c == '"') {
            buf[j++] = '\\';
        }
        buf[j] = arg[i];
    }
    if (enquote) {
        memmove(buf + 1, buf, j);  // shift 1 to the right.
        buf[0]     = '"';
        buf[j + 1] = '"';
        j += 2;
    }
    *size = j;
}

errval_t argv_to_cmdline(int argc, const char *argv[], char **cmdline)
{
    assert(argc > 0);
    *cmdline          = malloc(_cmdline_size(argc, argv));
    char *cmdline_pos = *cmdline;
    for (int i = 0; i < argc; ++i) {
        size_t size = 0;
        _cmdline_append(cmdline_pos, &size, argv[i]);
        cmdline_pos += size;
        *cmdline_pos = ' ';
        cmdline_pos++;
    }
    // null-terminate the string
    *(cmdline_pos - 1) = 0;
    return SYS_ERR_OK;
}

static errval_t _cmdline_argc(const char *cmdline, int *argc, size_t *size)
{
    *size = 1;  ///< size of argv required (trailing NULL)
    if (cmdline == NULL || *cmdline == '\0') {
        *argc = 0;
        return SYS_ERR_OK;
    }
    *argc           = 1;
    size_t len      = strlen(cmdline);
    bool   escaped  = false;
    bool   enquoted = false;
    for (size_t i = 0; i < len; ++i) {
        char c = cmdline[i];
        ++(*size);
        if (escaped) {
            if (!(c == '"' || c == '\\')) {
                return SYS_ERR_GUARD_MISMATCH;
            }
            escaped = false;
            --(*size);  // we can skip the '\'
        } else if (c == '\\') {
            escaped = true;
        } else if (c == ' ' && !enquoted) {
            ++(*argc);
        } else if (c == '"') {
            enquoted = !enquoted;
            if (!enquoted) {
                *size -= 2;  // we can skip the quotes
            }
        }
    }
    if (escaped || enquoted) {
        return SYS_ERR_GUARD_MISMATCH;
    }
    return SYS_ERR_OK;
}

static errval_t _argv_append(char **buf, const char **cmdline)
{
    bool escaped  = false;
    bool enquoted = false;
    for (;;) {
        char c = **cmdline;
        if (c == '\0') {
            **(buf) = '\0';
            (*buf)++, (*cmdline)++;
            return SYS_ERR_OK;
        } else if (escaped) {
            if (!(c == '"' || c == '\\')) {
                return SYS_ERR_GUARD_MISMATCH;
            }
            escaped = false;
            **buf   = **cmdline;
            (*buf)++, (*cmdline)++;
        } else if (c == '\\') {
            ++(*cmdline);
            escaped = true;
        } else if (c == ' ' && !enquoted) {
            **(buf) = '\0';
            (*buf)++, (*cmdline)++;
            return SYS_ERR_OK;
        } else if (c == '"') {
            enquoted = !enquoted;
            if (!enquoted) {
                **(buf) = '\0';
                (*buf)++, (*cmdline) += 2;
                return SYS_ERR_OK;
            }
            (*cmdline)++;
        } else {
            **buf = **cmdline;
            (*buf)++, (*cmdline)++;
        }
    }
    return SYS_ERR_OK;
}

// allocates argv, and there individual arguments in a single block.
errval_t cmdline_to_argv_blk(const char *cmdline, int *argc, char ***argv)
{
    errval_t err = SYS_ERR_OK;
    size_t   size;
    err = _cmdline_argc(cmdline, argc, &size);
    if (err_is_fail(err)) {
        return err;
    }
    *argv = malloc(sizeof(char *) * (*argc + 1) + sizeof(char) * size);

    char *buf = ((char *)*argv) + sizeof(char *) * (*argc + 1);
    for (int index = 0; index < *argc; ++index) {
        (*argv)[index] = buf;
        err            = _argv_append(&buf, &cmdline);
        if (err_is_fail(err)) {
            free(argv);
            return err;
        }
    }

    (*argv)[*argc] = NULL;
    return SYS_ERR_OK;
}

errval_t cmdline_to_argv(const char *cmdline, int *argc, char ***argv) {
    char **blk = NULL;
    errval_t err = cmdline_to_argv_blk(cmdline, argc, &blk);
    if (err_is_fail(err)) {
        return err;
    }
    *argv = malloc(sizeof(char *) * (*argc + 1));
    for (int i = 0; i < *argc; ++i) {
        size_t len = strlen(blk[i]);
        (*argv)[i] =  malloc(sizeof(char) * (len + 1));
        memcpy((*argv)[i], blk[i], len + 1);
    }

    free(blk);
    return SYS_ERR_OK;
}