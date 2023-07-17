#include "builtin.h"

#include <stdio.h>
#include <stdlib.h>
#include <stddef.h>
#include <ctype.h>

int main(int argc, char *argv[]) {
    builtin_init("wc", argc, argv);
    if (builtin_getargc() != 0) {
        builtin_fail("unexpected number of arguments.");
    }
    size_t lines = 0, words = 0, chars = 0;
    char p = '\0', c;
    bool read = false;
    while ((c = getchar()) != '\0') {
        read = true;
        if (isspace(c) && !isspace(p)) {
            ++words;
        }
        if (c== '\n') {
            ++lines;
        }
        p = c;
        ++chars;
    }
    if (read && !isspace(p)) {
        ++words;
    }
    bool show_lines = builtin_getflag('l');
    bool show_words = builtin_getflag('w');
    bool show_chars = builtin_getflag('c');
    if (!show_lines && !show_chars && !show_words) {
        printf("%7zu %7zu %7zu\n", lines, words, chars);
        return EXIT_SUCCESS;
    }
    if (show_lines) {
        printf("%7zu ", lines);
    }
    if (show_words) {
        printf("%7zu ", words);
    }
    if (show_chars) {
        printf("%7zu ", chars);
    }
    printf("\n");
}