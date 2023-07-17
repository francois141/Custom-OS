//
// Created by francois on 09.05.23.
//

#ifndef AOS2023_TEAM09_HAND_IN_FAT32_TESTS_H
#define AOS2023_TEAM09_HAND_IN_FAT32_TESTS_H

#define DEBUG_ON_FILESYSTEM_TEST_LOCAL

#if defined(DEBUG_ON_FILESYSTEM_TEST_LOCAL)
#define DEBUG_FILESYSTEM_FAT_TEST_LOCAL(x...) debug_printf(x)
#else
#define DEBUG_FILESYSTEM_FAT_TEST_LOCAL(x...) ((void)0)
#endif

#include <fs/fat32.h>

void _test_name_len(void);
void _test_name_len(void) {
    const char *name1 = "        .   ";
    assert(name_len(name1) == 0);
    const char *name2 = "TEST    .   ";
    assert(name_len(name2) == 4);
    const char *name3 = "BEEFBEEF.   ";
    assert(name_len(name3) == 8);
}

void _test_extension_len(void);
void _test_extension_len(void) {
    const char *name1 = "FILE   .   ";
    assert(extension_len(name1) == 0);
    const char *name2 = "FILE   .T  ";
    assert(extension_len(name2) == 1);
    const char *name3 = "FILE   .TXT";
    assert(extension_len(name3) == 3);
}

void _test_name_valid(void);
void _test_name_valid(void) {
    // Test directories names
    assert(is_fat32_name_valid("A", true));
    assert(!is_fat32_name_valid("2A", true));
    assert(is_fat32_name_valid("HELLOLLL", true));
    assert(!is_fat32_name_valid("HELLOLLLL", true));
    assert(!is_fat32_name_valid("DIR.TXT", true));

    // Test filenames - examples from here (http://elm-chan.org/docs/fat_e.html)
    assert(is_fat32_name_valid("FILENAME.TXT", false));
    assert(!is_fat32_name_valid("FILENAME.TXT", true));
    assert(is_fat32_name_valid("file.txt", false));
    assert(!is_fat32_name_valid("file.txt", true));
    assert(is_fat32_name_valid("NOEXT", true));
    assert(!is_fat32_name_valid(".cnf", true));
    assert(!is_fat32_name_valid(".cnf", false));
    assert(!is_fat32_name_valid("new file.txt", false));
    assert(!is_fat32_name_valid("new file.txt", true));
    assert(!is_fat32_name_valid("file[1].2+2", false));
    assert(!is_fat32_name_valid("two.dots.txt", false));
    assert(!is_fat32_name_valid("two.dots", true));
    return;
}

void _test_compare_entries_call(const char *name_entry, const char *extension_entry, const char *name, bool expect);
void _test_compare_entries_call(const char *name_entry, const char *extension_entry, const char *name, bool expect) {
    struct fat32_entry entry;
    memset(entry.name, 0x20, 11);
    memcpy(&entry.name, name_entry, strnlen(name_entry,8));
    memcpy(&entry.name[8], extension_entry, strnlen(extension_entry,3));
    assert(compare_filename_with_entry(&entry, name) == expect);
}

void _test_compare_entries(void);
void _test_compare_entries(void) {
    _test_compare_entries_call("TEST","", "TEST", true);
    _test_compare_entries_call("TEST","", "test", true);
    _test_compare_entries_call("TEST","", "TES", false);
    _test_compare_entries_call("TEST","", "TEST()", false);
    _test_compare_entries_call(".","", ".", true);
    _test_compare_entries_call("..","","..", true);
    _test_compare_entries_call("H", "TXT", "H.TXT", true);
    _test_compare_entries_call("H","TXT", "h.TXT", true);
    _test_compare_entries_call("HU","JPG", "Hu.jpg", true);
    _test_compare_entries_call("ASD","FGH", "asd.fg", false);
    _test_compare_entries_call("ASD","FG", "asd.fg", true);
    return;
}

void _test_shortname_to_name_sample(char *input, char *expect);
void _test_shortname_to_name_sample(char *input, char *expect) {
    char *output = NULL;
    assert(err_is_ok(shortname_to_name(input, &output)));
    assert(strcmp(expect, output) == 0);
}

void _test_shortname_to_name(void);
void _test_shortname_to_name(void) {
    _test_shortname_to_name_sample("HELLO   TXT", "HELLO.TXT");
    _test_shortname_to_name_sample(".          ",".");
    _test_shortname_to_name_sample("..         ","..");
    _test_shortname_to_name_sample("TEST       ", "TEST");
    _test_shortname_to_name_sample("A          ", "A");
    _test_shortname_to_name_sample("A23456     ", "A23456");
}

void _test_name_to_shortname_sample(char *input, char *expect);
void _test_name_to_shortname_sample(char *input, char *expect) {
    char *output = calloc(1,12);
    name_to_shortname(input, output);
    assert(strcmp(expect, output) == 0);
}

void _test_name_to_shortname(void);
void _test_name_to_shortname(void) {
    _test_name_to_shortname_sample("hello.txt","HELLO   TXT");
    _test_name_to_shortname_sample("hh","HH         ");
    _test_name_to_shortname_sample("H","H          ");
    _test_name_to_shortname_sample("h1234567.ref","H1234567REF");
}

void _test_resolve_call(struct fat32_filesystem *fs, const char *test_name, const char *path, bool result);
void _test_resolve_call(struct fat32_filesystem *fs, const char *test_name, const char *path, bool result) {
    (void)test_name;
    DEBUG_FILESYSTEM_FAT_TEST_LOCAL("_test_resolve name: %s path : %s, starting\n",test_name, path);

    struct fat32_handle output;
    errval_t err = fat32_resolve_path(fs, path, &output);
    if(result == false) {
        assert(err_is_fail(err));
    } else {
        assert(!err_is_fail(err));
    }

    DEBUG_FILESYSTEM_FAT_TEST_LOCAL("_test_extension_len done\n");
}

void _test_resolve(struct fat32_filesystem *fs);
void _test_resolve(struct fat32_filesystem *fs) {
    _test_resolve_call(fs, "Test resolve 1","/SDCARD/TEST/../TEST/TEST3", true);
    _test_resolve_call(fs, "Test resolve 2", "/SDCARD/TEST/././././././TEST3", true);
    _test_resolve_call(fs, "Test resolve 3", "/SDCARD/TEST/TEST3", true);
    _test_resolve_call(fs, "Test resolve 4", "/SDCARD/TEST/TEST3/../../TEST/././../TEST/TEST3", true);
    _test_resolve_call(fs, "Test resolve 5", "/SDCARD/TESTNO", false);
    _test_resolve_call(fs, "Test resolve 6", "/SDCARD/", true);
    _test_resolve_call(fs, "Test resolve 7", "/SDCARD/TEST6/COUCOU", false);
    _test_resolve_call(fs, "Test resolve 8", "/SDCARD/NOT.TXT", false);
}

void _test_fopen(struct fat32_filesystem *fs, const char *path);
void _test_fopen(struct fat32_filesystem *fs, const char *path) {
    DEBUG_FILESYSTEM_FAT_TEST_LOCAL("_test_fopen / _test_fclose : %s, starting\n", path);

    struct fat32_handle *handle;
    errval_t err = fat32_open(fs, path, &handle);
    assert(!err_is_fail(err));

    struct fs_fileinfo info;
    err = fat32_stat(fs, handle, &info);
    assert(!err_is_fail(err) && info.type == FS_FILE);

    err = fat32_close(fs, handle);
    assert(!err_is_fail(err));

    DEBUG_FILESYSTEM_FAT_TEST_LOCAL("_test_fopen / _test_fclose done\n", path);
}

void _test_freadseek(struct fat32_filesystem *fs, const char *path);
void _test_freadseek(struct fat32_filesystem *fs, const char *path) {
    DEBUG_FILESYSTEM_FAT_TEST_LOCAL("_test_freadseek : %s, starting\n", path);

    struct fat32_handle *handle;
    errval_t err = fat32_open(fs, path, &handle);
    assert(!err_is_fail(err));

    const char *data_to_write = "Test ";
    size_t size = 5;
    size_t nb_bytes_written;
    err = fat32_write(fs, handle, data_to_write, size, &nb_bytes_written);
    assert(!err_is_fail(err));
    assert(nb_bytes_written == 5);

    err =  fat32_seek(fs, handle,FS_SEEK_SET,0);
    assert(!err);

    void *data = calloc(6,1);
    size_t nb_bytes_read;
    err = fat32_read(fs, handle, data, 5, &nb_bytes_read);
    assert(!err && nb_bytes_read == 5);
    const char *expected = "Test ";
    assert(strncmp(data, expected, 5) == 0);

    err =  fat32_seek(fs, handle,FS_SEEK_SET,0);
    assert(!err);
    memset(data,0,6);

    err = fat32_read(fs, handle, data, 5, &nb_bytes_read);
    assert(!err && nb_bytes_read == 5 && strcmp(data,"Test ") == 0);

    DEBUG_FILESYSTEM_FAT_TEST_LOCAL("_test_freadseek, done\n");
}

void _test_fwrite(struct fat32_filesystem *fs, const char *path);
void _test_fwrite(struct fat32_filesystem *fs, const char *path) {
    DEBUG_FILESYSTEM_FAT_TEST_LOCAL("_test_fwrite : %s, starting\n", path);

    struct fat32_handle *handle;
    errval_t err = fat32_open(fs, path, &handle);
    assert(!err_is_fail(err));

    const char *data = "12345";
    size_t size = 5;
    size_t nb_bytes_written;
    err = fat32_write(fs, handle, data, size, &nb_bytes_written);
    assert(!err_is_fail(err));
    assert(nb_bytes_written == 5);

    void *result = calloc(6,1);
    err =  fat32_seek(fs, handle,FS_SEEK_SET,0);
    assert(!err);
    memset(result,0,6);
    size_t nb_bytes_read;
    err = fat32_read(fs, handle, result, 5, &nb_bytes_read);
    assert(!err && nb_bytes_read == 5 && strcmp(result,"12345") == 0);

    err =  fat32_seek(fs, handle,FS_SEEK_SET,0);
    assert(!err);

    const char *data2 = "Test ";
    size = 5;
    err = fat32_write(fs, handle, data2, size, &nb_bytes_written);
    assert(!err_is_fail(err));
    assert(nb_bytes_written == 5);

    DEBUG_FILESYSTEM_FAT_TEST_LOCAL("_test_fwrite, done\n");
}

void _test_fwrite_huge(struct fat32_filesystem *fs, const char *path);
void _test_fwrite_huge(struct fat32_filesystem *fs, const char *path) {
    DEBUG_FILESYSTEM_FAT_TEST_LOCAL("_test_fwrite_huge : %s, starting\n", path);

    struct fat32_handle *handle;
    errval_t err = fat32_open(fs, path, &handle);
    assert(!err_is_fail(err));

    size_t size_to_write = 4096;
    char *data = malloc(size_to_write);
    memset(data, 'A', size_to_write);

    for(int i = 0; i < 5;i++) {
        debug_printf("Round : %d\n", i);
        size_t nb_bytes_written;
        err = fat32_write(fs, handle, data, size_to_write, &nb_bytes_written);
        assert(!err_is_fail(err));
        assert(nb_bytes_written == size_to_write);
    }


    memset(data, 0, size_to_write);
    err =  fat32_seek(fs, handle,FS_SEEK_SET,0);
    assert(!err);

    size_t nb_bytes_read;
    err = fat32_read(fs, handle, data, size_to_write, &nb_bytes_read);
    assert(!err && nb_bytes_read == size_to_write);

    for(size_t i = 0; i < size_to_write;i++) {
        assert(data[i] == 'A');
    }

    DEBUG_FILESYSTEM_FAT_TEST_LOCAL("_test_fwrite_huge, done\n");
}

size_t _get_dir_size(struct fat32_filesystem *fs, const char *dir);
size_t _get_dir_size(struct fat32_filesystem *fs, const char *dir) {
    struct fat32_handle *handle;
    errval_t err = fat32_open_directory(fs, dir, &handle);
    assert(!err_is_fail(err));

    size_t cnt = 0;

    do {
        char *dir_name;
        err = fat32_read_next_directory(fs, handle, &dir_name);
        if (err_no(err) == FS_ERR_INDEX_BOUNDS) {
            break;
        } else if (err_is_fail(err)) {
            assert(false);
        }
        cnt++;
    } while(err_is_ok(err));

    err = fat32_close_directory(fs, handle);
    assert(!err_is_fail(err));

    return cnt;
}

void _test_mk_rm(struct fat32_filesystem *fs, const char *dir);
void _test_mk_rm(struct fat32_filesystem *fs, const char *dir) {
    DEBUG_FILESYSTEM_FAT_TEST_LOCAL("_test_mk_rm in dir % s, start\n", dir);

    assert(_get_dir_size(fs,dir) == 4);

    DEBUG_FILESYSTEM_FAT_TEST_LOCAL("Test 1\n");

    struct fat32_handle *handle;
    errval_t err = fat32_mkdir(fs, "/SDCARD/TEST/TEST1", &handle);
    assert(!err_is_fail(err));

    assert(_get_dir_size(fs,dir) == 5);

    DEBUG_FILESYSTEM_FAT_TEST_LOCAL("Test 2\n");

    err = fat32_mkdir(fs, "/SDCARD/TEST/TEST2", &handle);
    assert(!err_is_fail(err));

    assert(_get_dir_size(fs,dir) == 6);

    DEBUG_FILESYSTEM_FAT_TEST_LOCAL("Test 3\n");

    err = fat32_remove_directory(fs, "/SDCARD/TEST/TEST1");
    assert(!err_is_fail(err));

    assert(_get_dir_size(fs,dir) == 5);

    DEBUG_FILESYSTEM_FAT_TEST_LOCAL("Test 4\n");

    struct fat32_handle *handle_file;
    err = fat32_create(fs, "/SDCARD/TEST/TEST.TXT", &handle_file);
    assert(!err_is_fail(err));

    assert(_get_dir_size(fs,dir) == 6);

    DEBUG_FILESYSTEM_FAT_TEST_LOCAL("Test 5\n");

    err = fat32_remove_directory(fs, "/SDCARD/TEST/TEST2");
    assert(!err_is_fail(err));

    assert(_get_dir_size(fs,dir) == 5);

    DEBUG_FILESYSTEM_FAT_TEST_LOCAL("Test 6\n");

    err = fat32_remove(fs, "/SDCARD/TEST/TEST.TXT");
    assert(!err_is_fail(err));

    assert(_get_dir_size(fs,dir) == 4);

    DEBUG_FILESYSTEM_FAT_TEST_LOCAL("Test 7\n");

    DEBUG_FILESYSTEM_FAT_TEST_LOCAL("_test_mk_rm done\n");
    return;
}

void _test_mk_rm_stress(struct fat32_filesystem *fs);
void _test_mk_rm_stress(struct fat32_filesystem *fs) {
    DEBUG_FILESYSTEM_FAT_TEST_LOCAL("_test_mk_rm_stess start\n");

    struct fat32_handle *handle;
    fat32_mkdir(fs, "/SDCARD/A", &handle);

    const unsigned int size = 64;
    for(unsigned i = 1; i <= size;i++) {
        int cnt = 11;
        int tmp = i;
        while(tmp != 0) {
            cnt++;
            tmp /= 26;
        }

        char *prefix = "/SDCARD/A/";
        char *path = calloc(1, cnt);
        memcpy(path, prefix, 11);
        tmp = i;
        for(int j = cnt-2; j >= 10;j--) {
            *(path + j) = 'A' + (tmp % 26);
            tmp /= 26;
        }

        debug_printf("Add dir round : %d\n", i);
        debug_printf("Path : %s\n", path);
        debug_printf("Error : %d\n", fat32_mkdir(fs, path, &handle));

        free(path);
    }


    for(unsigned int i = 1; i <= size;i++) {
        int cnt = 11;
        int tmp = i;
        while(tmp != 0) {
            cnt++;
            tmp /= 26;
        }

        char *prefix = "/SDCARD/A/";
        char *path = calloc(1, cnt);
        memcpy(path, prefix, 11);
        tmp = i;
        for(int j = cnt-2; j >= 10;j--) {
            *(path + j) = 'A' + (tmp % 26);
            tmp /= 26;
        }

        debug_printf("Remove dir round : %d\n", i);
        debug_printf("Path : %s\n", path);
        debug_printf("Error : %d\n", fat32_remove_directory(fs, path));
    }

    DEBUG_FILESYSTEM_FAT_TEST_LOCAL("_test_mk_rm_stress done\n");
    return;
}

void _test_mkdir_remove_recursive(struct fat32_filesystem *fs);
void _test_mkdir_remove_recursive(struct fat32_filesystem *fs) {
    DEBUG_FILESYSTEM_FAT_TEST_LOCAL("_test_mkdir_remove_recursive start\n");
    struct fat32_handle *handle;

    assert(err_is_ok(fat32_mkdir(fs, "/SDCARD/B", &handle)));
    assert(err_is_ok(fat32_mkdir(fs, "/SDCARD/B/B", &handle)));
    assert(err_is_ok(fat32_mkdir(fs, "/SDCARD/B/B/B", &handle)));
    assert(err_is_ok(fat32_mkdir(fs, "/SDCARD/B/B/B/B", &handle)));
    assert(err_is_ok(fat32_mkdir(fs, "/SDCARD/B/B/B/B/B", &handle)));
    assert(err_is_ok(fat32_mkdir(fs, "/SDCARD/B/B/B/B/B/B", &handle)));
    assert(err_is_ok(fat32_mkdir(fs, "/SDCARD/B/B/B/B/B/B/B", &handle)));
    assert(err_is_ok(fat32_mkdir(fs, "/SDCARD/B/B/B/B/B/B/B/B", &handle)));
    assert(err_is_ok(fat32_mkdir(fs, "/SDCARD/B/B/B/B/B/B/B/B/B", &handle)));
    assert(err_is_ok(fat32_mkdir(fs, "/SDCARD/B/B/B/B/B/B/B/B/B/B", &handle)));
    assert(err_is_ok(fat32_mkdir(fs, "/SDCARD/B/B/B/B/B/B/B/B/B/B/B", &handle)));
    assert(err_is_ok(fat32_mkdir(fs, "/SDCARD/B/B/B/B/B/B/B/B/B/B/B/B", &handle)));
    assert(err_is_ok(fat32_mkdir(fs, "/SDCARD/B/B/B/B/B/B/B/B/B/B/B/B/B", &handle)));

    assert(err_is_ok(fat32_remove_directory(fs, "/SDCARD/B/B/B/B/B/B/B/B/B/B/B/B/B")));
    assert(err_is_ok(fat32_remove_directory(fs, "/SDCARD/B/B/B/B/B/B/B/B/B/B/B/B")));
    assert(err_is_ok(fat32_remove_directory(fs, "/SDCARD/B/B/B/B/B/B/B/B/B/B/B")));
    assert(err_is_ok(fat32_remove_directory(fs, "/SDCARD/B/B/B/B/B/B/B/B/B/B")));
    assert(err_is_ok(fat32_remove_directory(fs, "/SDCARD/B/B/B/B/B/B/B/B/B")));
    assert(err_is_ok(fat32_remove_directory(fs, "/SDCARD/B/B/B/B/B/B/B/B")));
    assert(err_is_ok(fat32_remove_directory(fs, "/SDCARD/B/B/B/B/B/B/B")));
    assert(err_is_ok(fat32_remove_directory(fs, "/SDCARD/B/B/B/B/B/B")));
    assert(err_is_ok(fat32_remove_directory(fs, "/SDCARD/B/B/B/B/B")));
    assert(err_is_ok(fat32_remove_directory(fs, "/SDCARD/B/B/B/B")));
    assert(err_is_ok(fat32_remove_directory(fs, "/SDCARD/B/B/B")));
    assert(err_is_ok(fat32_remove_directory(fs, "/SDCARD/B/B")));
    assert(err_is_ok(fat32_remove_directory(fs, "/SDCARD/B")));

    DEBUG_FILESYSTEM_FAT_TEST_LOCAL("_test_mkdir_remove_recursive done\n");
    return;
}

void _test_create_write_remove(struct fat32_filesystem *fs, const char *path);
void _test_create_write_remove(struct fat32_filesystem *fs, const char *path) {
    DEBUG_FILESYSTEM_FAT_TEST_LOCAL("_test_create_write_remove start\n");

    for(size_t i = 0; i < 5;i++) {
        DEBUG_FILESYSTEM_FAT_TEST_LOCAL("round: %d\n",i);
        struct fat32_handle *handle_file;
        assert(err_is_ok(fat32_create(fs, path, &handle_file)));
        assert(err_is_ok(fat32_remove(fs, path)));
    }

    DEBUG_FILESYSTEM_FAT_TEST_LOCAL("_test_create_write_remove done\n");
}

void run_all_tests(struct fat32_filesystem *fs);
void run_all_tests(struct fat32_filesystem *fs) {
    // Test values
    const char *test_file_path = "/SDCARD/TEST/HELLO.TXT";
    const char *test_file_path2 = "/SDCARD/TEST/TEST.TXT";
    const char *test_dir = "/SDCARD/TEST";

    // Real tests
    _test_name_len();
    _test_extension_len();
    _test_name_valid();
    _test_compare_entries();
    _test_shortname_to_name();
    _test_name_to_shortname();

    _test_resolve(fs);
    _test_fopen(fs, test_file_path);
    _test_freadseek(fs,test_file_path);
    _test_fwrite(fs, test_file_path);
    _test_fwrite_huge(fs, test_file_path);
    _test_mk_rm(fs, test_dir);
    _test_create_write_remove(fs, test_file_path2);
    _test_mk_rm_stress(fs);
    _test_mkdir_remove_recursive(fs);

    return;
}


#endif //AOS2023_TEAM09_HAND_IN_FAT32_TESTS_H
