//
// Created by francois on 29.04.23.
//

#include <fs/fat32.h>
#include "tests/fat32_tests.h"
#include "tests/fat32_benchmark.h"
// #define DEBUG_ON_FILESYSTEM

#if defined(DEBUG_ON_FILESYSTEM)
#define DEBUG_FILESYSTEM(x...) debug_printf(x)
#else
#define DEBUG_FILESYSTEM(x...) ((void)0)
#endif
/*
errval_t lookup(const char *abs_path, uint8t return_value);
errval_t lookup_relative(struct fat32_entry, const char *path, uint8t return_value);
*/

static struct fat32_filesystem *current_filesystem;

// Attribute management
bool is_directory(const struct fat32_entry *entry) {
    assert(entry != NULL);
    return entry->attribute & ATTR_DIRECTORY;
}

void set_directory(struct fat32_entry *entry) {
    assert(entry != NULL);
    entry->attribute |= ATTR_DIRECTORY;
}

bool is_file(const struct fat32_entry *entry) {
    assert(entry != NULL);
    return !is_directory(entry);
}

// Name management
size_t name_len(const char *shortname) {
    for (size_t idx = 0; idx < 8; idx++) {
        if (shortname[idx] == ' ') {
            return idx;
        }
    }

    return 8;
}

size_t extension_len(const char *shortname) {
    for (size_t idx = 8; idx < 11; idx++) {
        if (shortname[idx] == ' ') {
            return idx - 8;
        }
    }

    return 3;
}

bool check_chars(const char *name, size_t from, size_t to) {
    for(size_t i = from; i < to;i++) {
        bool curr = isalnum(name[i]);
        curr |= name[i] == '!';
        curr |= name[i] == '#';
        curr |= name[i] == '$';
        curr |= name[i] == '%';
        curr |= name[i] == '&';
        curr |= name[i] == '\'';
        curr |= name[i] == '(';
        curr |= name[i] == ')';
        curr |= name[i] == '-';
        curr |= name[i] == '@';
        curr |= name[i] == '^';
        curr |= name[i] == '_';
        curr |= name[i] == '`';
        curr |= name[i] == '{';
        curr |= name[i] == '}';
        curr |= name[i] == '~';
        if(!curr) {
            return false;
        }
    }
    return true;
}

bool is_fat32_name_valid(const char *name, bool is_directory) {
    size_t len = strnlen(name, 13);
    // Filter out bad strings
    bool is_length_null = len == 0;
    bool is_length_too_big = len > 12;
    bool is_start_digit = isdigit(name[0]);
    bool is_start_dot = name[0] == '.';
    if (is_length_null || is_length_too_big || is_start_digit || is_start_dot) {
        return false;
    }

    // Split file and extension
    char *split = strrchr(name, '.');

    if(split != NULL && is_directory) {
        return false;
    }
    if(!split && len > 8) {
        return false;
    }

    if (split == NULL) {
        return check_chars(name, 0, strnlen(name,8));
    }

    size_t size_name = split - name;
    split++;
    size_t size_extension = strnlen(split, 5);
    if (size_name > 8 || size_extension > 3) {
        return false;
    }

    bool c_name = check_chars(name, 0, size_name);
    bool c_extension = check_chars(name, size_name + 1, size_name + 1 + size_extension);

    return c_name && c_extension;
}

void name_to_shortname(const char *input_name, char *output_shortname) {
    memset(output_shortname, ' ', 11);

    char *split = strchr(input_name, '.');
    if (split) {
        size_t size_name = split - input_name;
        size_t idx = 0;
        for(size_t i = 0; i < size_name;i++) {
            output_shortname[idx++] = toupper(input_name[i]);
        }
        size_t size_extension = strnlen(split + 1, 3);
        idx = 8;
        for(size_t i = size_name + 1; i < size_name + 1 + size_extension;i++) {
            output_shortname[idx++] = toupper(input_name[i]);
        }
    } else {
        size_t size_name = strnlen(input_name, 8);
        for (size_t i = 0; i < size_name; i++) {
            output_shortname[i] = toupper(input_name[i]);
        }
    }

    return;
}

errval_t shortname_to_name(char *input_shortname, char **output_name) {
    *output_name = calloc(sizeof(char), 12);
    if (*output_name == NULL) {
        return LIB_ERR_MALLOC_FAIL;
    }

    size_t size_name = name_len(input_shortname);
    size_t size_extension = extension_len(input_shortname);

    size_t curr = 0;
    for (; curr < size_name; curr++) {
        (*output_name)[curr] = input_shortname[curr];
    }

    if(size_extension != 0) {
        (*output_name)[curr++] = '.';

        for (size_t i = 0; i < size_extension; i++) {
            (*output_name)[curr + i] = input_shortname[8+i];
        }
    }

    return SYS_ERR_OK;
}

bool compare_filename_with_entry(struct fat32_entry *entry, const char *path) {
    if (strcmp(path, "..") == 0) {
        return entry->name[0] == '.' && entry->name[1] == '.';
    }
    if (strcmp(path, ".") == 0) {
        return entry->name[0] == '.';
    }

    const char *name = entry->name;
    const char *separation = strchr(path, '.');

    size_t len_name_1 = 0;
    size_t len_extension_1 = 0;

    size_t len_name_2 = 0;
    size_t len_extension_2 = 0;

    if (separation == NULL) {
        len_name_1 = name_len(name);
        len_name_2 = strlen(path);
    } else {
        len_name_1 = name_len(name);
        len_name_2 = separation - path;
        separation++;
        len_extension_1 = extension_len(name);
        len_extension_2 = strlen(separation);
    }

    if (len_name_1 != len_name_2 || len_extension_1 != len_extension_2) {
        return false;
    }

    if (strncasecmp(name, path, len_name_1) != 0) {
        return false;
    }
    if (separation != NULL && strncasecmp(separation, name + 8, len_extension_1) != 0) {
        return false;
    }

    return true;
}

// Directory management
bool is_directory_used(struct fat32_entry
                       *entry) {
    return entry->name[0] != DIRECTORY_FREE_VALUE && entry->name[0] != END_DIRECTORY;
}

bool is_directory_free(struct fat32_entry
                       *entry) {
    return !is_directory_used(entry);
}

bool is_end_directory(struct fat32_entry
                      *entry) {
    return entry->name[0] == END_DIRECTORY;
}

bool is_entry_valid(struct fat32_entry *entry) {
    bool condition_1 = ('A' <= entry->name[0] && entry->name[0] <= 'Z');
    bool condition_2 = entry->name[0] == '.';
    return condition_1 || condition_2;
}


void create_directory_entry(struct fat32_entry *entry, const char *name, uint32_t cluster_number) {
    memset((void*)entry->name, ' ', 11);
    memcpy((void*)entry->name, name, strlen(entry->name));
    set_cluster_number(entry, cluster_number);
    set_directory(entry);
}

// Handle management
void close_handle(struct fat32_handle *handle) {
    assert(handle != NULL && handle->path != NULL);
    free(handle->path);
    free(handle);
}

// Cluster management
uint32_t get_cluster_number(const struct fat32_entry
                            *entry) {
    uint32_t next_cluster_number = ((uint32_t) entry->cluster_high << 16) | (entry->cluster_low);
    assert(next_cluster_number != BAD_CLUSTER);
    return next_cluster_number;
}

void set_cluster_number(struct fat32_entry
                        *entry, uint32_t cluster_number) {
    entry->cluster_low = cluster_number & 0xFFFF;
    entry->cluster_high = cluster_number >> 16;
}

bool is_end_cluster(uint32_t cluster_number) {
    // Security assertion
    assert(cluster_number != 0);
    return cluster_number >= END_CLUSTER;
}

bool is_cluster_free(uint32_t cluster_number){
    return !(cluster_number & 0x0fffffff);
}

bool index_in_new_cluster(struct fat32_filesystem *fs, uint32_t index) {
    return (index + 1) >= fs->sectors_per_cluster * NUMBER_DIRECTORY_PER_BLOCK;
}

uint32_t get_number_additional_clusters(struct fat32_filesystem *fs, uint32_t new_size, uint32_t nb_clusters_current) {
    const uint32_t bytes_per_cluster = FAT_BLOCK_SIZE * fs->sectors_per_cluster;
    return (ROUND_UP(new_size, bytes_per_cluster) / bytes_per_cluster) - nb_clusters_current;
}

// Address management
uint32_t get_lba_from_cluster(const struct fat32_filesystem *fat_fs, uint32_t cluster_number) {
    uint32_t base = fat_fs->cluster_begin_data;
    uint32_t offset = (cluster_number - 2) * fat_fs->sectors_per_cluster;
    // Edge case
    if (cluster_number == 0) {
        offset = 0;
    }
    return base + offset;
}

errval_t fat32_get_next_cluster(const struct fat32_filesystem *fat_mount, uint32_t cluster_number, uint32_t *ret_cluster_number) {
    errval_t err = SYS_ERR_OK;

    // Edge case with the root
    if(cluster_number == 0) {
        *ret_cluster_number = END_CLUSTER;
        return SYS_ERR_OK;
    }

    uint8_t data[FAT_BLOCK_SIZE];
    uint32_t lba = fat_mount->numbers_sectors_reserved + cluster_number / FAT_ENTRIES_PER_SECTOR;

    err = read_block(fat_mount->b_driver, lba, (void *) data);
    if (err_is_fail(err)) {
        return err;
    }

    *ret_cluster_number = *(uint32_t * )(data + (cluster_number % FAT_ENTRIES_PER_SECTOR) * 4);
    return SYS_ERR_OK;
}

// Walk functions
errval_t fat32_read_directory(struct fat32_filesystem *fs, const struct fat32_entry *entry, size_t *nb_entries, struct fat32_entry **out_directories) {
    errval_t err = SYS_ERR_OK;

    if (is_directory(entry)) {
        return FS_ERR_NOTFILE;
    }

    size_t bytes_to_read = ROUND_UP(entry->file_size, FAT_BLOCK_SIZE);
    if(bytes_to_read == 0) bytes_to_read = 512;

    *nb_entries = bytes_to_read / sizeof(struct fat32_entry);
    *out_directories = malloc(bytes_to_read);
    if (*out_directories == NULL) {
        return LIB_ERR_MALLOC_FAIL;
    }

    size_t current_size = 0;

    uint32_t current_cluster_number = get_cluster_number(entry);
    uint32_t lba_start = get_lba_from_cluster(fs, current_cluster_number);

    while (current_size < bytes_to_read) {
        for (int j = 0; j < fs->sectors_per_cluster; j++) {
            uint8_t data[FAT_BLOCK_SIZE];
            err = read_block(fs->b_driver, lba_start + j, (void *) data);
            if (err_is_fail(err)) {
                return err;
            }

            memcpy(*((void**)out_directories) + current_size, data, FAT_BLOCK_SIZE);

            current_size += 512;
            if (current_size >= bytes_to_read) {
                break;
            }
        }

        uint32_t next_cluster_number = 0;
        fat32_get_next_cluster(fs, current_cluster_number, &next_cluster_number);

        if (next_cluster_number >= END_CLUSTER) {
            break;
        }
    }

    return SYS_ERR_OK;
}

errval_t fat32_read_file(struct fat32_filesystem *fs, const struct fat32_entry *entry, size_t *size, void **out_data) {
    errval_t err = SYS_ERR_OK;

    if (is_directory(entry)) {
        return FS_ERR_NOTFILE;
    }

    size_t bytes_to_read = ROUND_UP(entry->file_size, BASE_PAGE_SIZE);

    *size = bytes_to_read;
    *out_data = malloc(bytes_to_read);
    if (*out_data == NULL) {
        return LIB_ERR_MALLOC_FAIL;
    }

    size_t current_size = 0;

    uint32_t current_cluster_number = get_cluster_number(entry);
    uint32_t lba_start = get_lba_from_cluster(fs, current_cluster_number);

    while (current_size < bytes_to_read) {
        for (int j = 0; j < fs->sectors_per_cluster; j++) {
            uint8_t data[FAT_BLOCK_SIZE];
            err = read_block(fs->b_driver, lba_start + j, (void *) data);
            if (err_is_fail(err)) {
                return err;
            }

            memcpy(*out_data + current_size, data, FAT_BLOCK_SIZE);

            current_size += 512;
            if (current_size >= bytes_to_read) {
                break;
            }
        }

        uint32_t next_cluster_number = 0;
        fat32_get_next_cluster(fs, current_cluster_number, &next_cluster_number);

        if (next_cluster_number >= END_CLUSTER) {
            break;
        }
    }

    return SYS_ERR_OK;
}

errval_t fat32_find_directory(const struct fat32_filesystem *fs,int arg, struct fat32_entry *entry, const char *path,
                        struct fat32_entry *new_entry, uint32_t *parent_cluster_number, uint32_t *parent_cluster_offset) {
    errval_t err = SYS_ERR_OK;

    // Security check - path can be null
    assert(fs && entry && new_entry && parent_cluster_number && parent_cluster_offset);

    uint8_t next = 1;

    uint32_t current_cluster_number = get_cluster_number(entry);
    uint8_t data[FAT_BLOCK_SIZE];

    while (next) {
        uint32_t lba_start = get_lba_from_cluster(fs, current_cluster_number);
        // Parse the content
        for (int j = 0; j < fs->sectors_per_cluster; j++) {
            err = read_block(fs->b_driver, lba_start + j, (void *) data);
            if (err_is_fail(err)) {
                return err;
            }

            for (int i = 0; i < NUMBER_DIRECTORY_PER_BLOCK; i++) {
                struct fat32_entry
                        current_entry = *((struct fat32_entry
                *) (data + 32 * i));
                if(arg == FIND_NAME_MATCH) {
                    if (current_entry.name[0] != 0xe5 && current_entry.name[0] != 0x00) {
                        if (compare_filename_with_entry(&current_entry, path)) {
                            *new_entry = current_entry;
                            *parent_cluster_number = current_cluster_number;
                            *parent_cluster_offset = i + j*NUMBER_DIRECTORY_PER_BLOCK;
                            return SYS_ERR_OK;
                        }
                    }
                } else if(arg == FIND_FREE_ENTRY) {
                    if(current_entry.name[0] == 0xe5 || current_entry.name[0] == 0x00) {
                        *new_entry = current_entry;
                        *parent_cluster_number = current_cluster_number;
                        *parent_cluster_offset = i + j*NUMBER_DIRECTORY_PER_BLOCK;
                        return SYS_ERR_OK;
                    }
                } else if(arg == FIND_USED_ENTRY) {
                    if(current_entry.name[0] != 0xe5 && current_entry.name[0] != 0x00 && current_entry.name[0] != '.') {
                        // Found used entry
                        *new_entry = current_entry;
                        *parent_cluster_number = current_cluster_number;
                        *parent_cluster_offset = i + j*NUMBER_DIRECTORY_PER_BLOCK;
                        return SYS_ERR_OK;
                    }
                }

                if (current_entry.name[0] == 0x00) {
                    return FS_ERR_NOTFOUND;
                }
            }
        }

        uint32_t next_cluster_number = 0;
        err = fat32_get_next_cluster(fs, current_cluster_number, &next_cluster_number);
        if(err_is_fail(err)) {
            return err;
        }

        if (next_cluster_number >= END_CLUSTER) {
            next = 0;
        } else {
            current_cluster_number = next_cluster_number;
        }
    }

    new_entry = NULL;

    return FS_ERR_NOTFOUND;
}

errval_t fat32_resolve_path(const struct fat32_filesystem *fat_mount, const char *path,
                      struct fat32_handle *output) {
    errval_t err = SYS_ERR_OK;

    // Security assertions
    assert(path[0] == FS_PATH_SEP);
    assert(fat_mount);
    assert(path);
    assert(output);

#define MOUNT_SUPPORT
#ifdef MOUNT_SUPPORT
    // We traverse the mounting point here
    if(!(strnlen(path, 7) >= 7 && strncasecmp("/SDCARD/", path,7) == 0)) {
        return VFS_ERR_MOUNTPOINT_NOTFOUND;
    }
    if(strlen(path) == 7) {
        path ="/";
    } else {
        path += strlen("/SDCARD");
    }
#endif

    int idx = 1;
    struct fat32_entry current_entry = fat_mount->root_directory;

    uint32_t parent_cluster_number = 0;
    uint32_t parent_cluster_offset = 0;

    while (path[idx] != '\0') {
        // Step 1) Break the path into two parts (if applicable)
        char *separation = strchr(path + idx, FS_PATH_SEP);
        bool is_last = false;

        size_t current_size;
        if (separation == NULL) {
            current_size = strlen(path + idx);
            is_last = true;
        } else {
            current_size = separation - (path + idx);
        }

        char current_path[current_size + 1];
        memcpy(current_path, path + idx, current_size);
        current_path[current_size] = 0;

        // Step 2) Find the fat32 directory associated with it
        struct fat32_entry new_entry;
        err = fat32_find_directory(fat_mount,FIND_NAME_MATCH, &current_entry, current_path, &new_entry, &parent_cluster_number, &parent_cluster_offset);
        if(err == FS_ERR_NOTFOUND) {
            return FS_ERR_NOTFOUND;
        } else if (err_is_fail(err)) {
            return err;
        }

        // Step 3) Check if directory isn't the last one
        if (is_file(&new_entry) && !is_last) {
            return FS_ERR_NOTDIR;
        }

        // Step 4) Move to the next part
        current_entry = new_entry;
        if (is_last) {
            break;
        }
        idx += current_size + 1;
    }

    // Fill handle
    output->entry = current_entry;
    output->current_cluster = get_cluster_number(&current_entry);
    output->relative_sector_from_cluster = 0;
    output->directory_offset = 0;
    output->path = malloc(strlen(path));
    memcpy(output->path, path, strlen(path));
    output->is_directory = is_directory(&current_entry);
    output->parent_cluster_number = parent_cluster_number;
    output->parent_cluster_offset = parent_cluster_offset;

    // Return the output -> NULL in case of failure and a value otherwise
    return SYS_ERR_OK;
}

errval_t fat32_exits(struct fat32_filesystem *fs, const char *path, struct fat32_handle *handle) {
    return fat32_resolve_path(fs, path, handle);
}

errval_t fat32_handle_valid(struct fat32_filesystem *fs, const struct fat32_handle *handle) {
    errval_t err = SYS_ERR_OK;

    uint8_t data[FAT_BLOCK_SIZE];

    uint32_t lba = get_lba_from_cluster(fs, handle->parent_cluster_number) + handle->parent_cluster_offset / NUMBER_DIRECTORY_PER_BLOCK;
    err = read_block(fs->b_driver, lba, (void *) data);
    if (err_is_fail(err)) {
        return err;
    }

    // Edge case, root handle is always valid
    if(get_cluster_number(&handle->entry) == 0) {
        return SYS_ERR_OK;
    }

    struct fat32_entry *e = (struct fat32_entry*)(data + ((32*handle->parent_cluster_offset) % 512));

    for(int i = 0; i < 11;i++) {
        if(e->name[i] != handle->entry.name[i]) {
            return FS_ERR_INVALID_FH;
        }
    }

    return SYS_ERR_OK;
}


// File functions - read
errval_t fat32_open(struct fat32_filesystem *fs, const char *path, struct fat32_handle **handle) {
    errval_t err = SYS_ERR_OK;

    if(fs == NULL) {
        return VFS_ERR_UNKNOWN_FILESYSTEM;
    }

    assert(path[0] == FS_PATH_SEP);

    *handle = malloc(sizeof(struct fat32_handle));
    if(handle == NULL) {
        return LIB_ERR_MALLOC_FAIL;
    }

    err = fat32_resolve_path(fs, path, *handle);
    if(err_is_fail(err)) {
        return err;
    }

    if((*handle)->is_directory)  {
        return FS_ERR_NOTFILE;
    }

    return SYS_ERR_OK;
}

errval_t fat32_close(struct fat32_filesystem *fs, struct fat32_handle *handle) {
    if(fs == NULL) {
        return VFS_ERR_UNKNOWN_FILESYSTEM;
    }

    if(handle->is_directory) {
        return FS_ERR_NOTFILE;
    }
    close_handle(handle);
    return SYS_ERR_OK;
}

errval_t fat32_stat(struct fat32_filesystem *fs, const struct fat32_handle *handle, struct fs_fileinfo *file_info) {
    errval_t err = SYS_ERR_OK;

    if(fs == NULL) {
        return VFS_ERR_UNKNOWN_FILESYSTEM;
    }

    err = fat32_handle_valid(fs, handle);
    if(err_is_fail(err)) {
        return err;
    }

    assert(file_info != NULL && handle != NULL);
    file_info->size = handle->entry.file_size;
    file_info->type = handle->is_directory ? FS_DIRECTORY : FS_FILE;

    return SYS_ERR_OK;
}

errval_t fat32_read(struct fat32_filesystem *fs, struct fat32_handle *handle, void *data, size_t size, size_t *nb_bytes_read) {
    errval_t err = SYS_ERR_OK;

    if(fs == NULL) {
        return VFS_ERR_UNKNOWN_FILESYSTEM;
    }

    // Security assertion
    assert(fs && handle && data && nb_bytes_read);

    if(handle->is_directory) {
        return FS_ERR_NOTFILE;
    }

    err = fat32_handle_valid(fs, handle);
    if(err_is_fail(err)) {
        return err;
    }

    size_t bytes_read_until_now = 0;

    // Adapt size we can read from position
    if(handle->entry.file_size < handle->file_position) {
        size = 0;
    }

    size = MIN(size,handle->entry.file_size - handle->file_position);
    uint8_t buffer[FAT_BLOCK_SIZE];

    while(bytes_read_until_now < size) {
        // Step 1) Go to next_cluster if required
        if(handle->relative_sector_from_cluster >= fs->sectors_per_cluster) {
            uint32_t next_cluster_number = 0;
            err = fat32_get_next_cluster(fs, handle->current_cluster, &next_cluster_number);
            if(err_is_fail(err)) {
                return err;
            }
            assert(next_cluster_number < BAD_CLUSTER && next_cluster_number > 0);
            handle->current_cluster = next_cluster_number;
            handle->relative_sector_from_cluster = 0;
        }

        // Step 2) Read block
        err = read_block(fs->b_driver, get_lba_from_cluster(fs, handle->current_cluster) + handle->relative_sector_from_cluster, (void *) buffer);
        if (err_is_fail(err)) {
            return err;
        }

        // Step 3) Copy content into output
        uint32_t buffer_start = handle->file_position % FAT_BLOCK_SIZE;
        uint32_t size_to_copy = MIN(FAT_BLOCK_SIZE - buffer_start, size - bytes_read_until_now);
        memcpy(data + bytes_read_until_now, buffer + buffer_start, size_to_copy);

        // Step 4) Update variables
        uint32_t nb_sectors_read = (handle->file_position + size_to_copy) / FAT_BLOCK_SIZE - handle->file_position / FAT_BLOCK_SIZE;
        bytes_read_until_now += size_to_copy;
        handle->file_position += size_to_copy;
        handle->relative_sector_from_cluster += nb_sectors_read;
    }

    assert(size == bytes_read_until_now);
    *nb_bytes_read = bytes_read_until_now;

    return SYS_ERR_OK;
}

errval_t fat32_write(struct fat32_filesystem *fs, struct fat32_handle *handle, const void *data, size_t size, size_t *nb_bytes_written) {
    errval_t err = SYS_ERR_OK;

    if(fs == NULL) {
        return VFS_ERR_UNKNOWN_FILESYSTEM;
    }

    // Security assertions
    assert(nb_bytes_written);

    if(handle->is_directory) {
        return FS_ERR_NOTFILE;
    }

    err = fat32_handle_valid(fs, handle);
    if(err_is_fail(err)) {
        return err;
    }

    const size_t new_size = handle->file_position + size;
    if(handle->entry.file_size < new_size) {
        uint32_t nb_clusters_required;
        uint32_t last_cluster_number;
        // Compute number of clusters we need to add
        err = fat32_get_number_of_required_clusters(fs, handle, new_size + 1, &nb_clusters_required, &last_cluster_number);
        if(err_is_fail(err)) {
            return err;
        }
        // Increase the fat chain
        err = fat32_increase_chain(fs, last_cluster_number, nb_clusters_required);
        if(err_is_fail(err)) {
            return err;
        }
    }

    // Perform read and writes
    uint8_t bloc[FAT_BLOCK_SIZE];
    uint32_t bytes_written_current = 0;

    while(bytes_written_current < size) {
        // Get parameters
        uint32_t current_offset = handle->file_position % FAT_BLOCK_SIZE;
        uint32_t bytes_to_write = MIN(FAT_BLOCK_SIZE - current_offset, size - bytes_written_current);
        uint32_t lba = get_lba_from_cluster(fs, handle->current_cluster) + handle->relative_sector_from_cluster;

        // Read
        if(bytes_to_write < 512) {
            err = read_block(fs->b_driver, lba, (void *) bloc);
            if (err_is_fail(err)) {
                return err;
            }
        }

        // Copy into memory
        memcpy(bloc + current_offset, data + bytes_written_current, bytes_to_write);
        // Write
        err = write_block(fs->b_driver, lba, (void *) bloc);
        if (err_is_fail(err)) {
            return err;
        }

        // Update parameters
        if((handle->file_position % 512) + bytes_to_write >= 512) {
            handle->relative_sector_from_cluster++;
        }
        bytes_written_current += bytes_to_write;
        handle->file_position += bytes_to_write;
        if(handle->relative_sector_from_cluster >= fs->sectors_per_cluster) {
            handle->relative_sector_from_cluster -= fs->sectors_per_cluster;
            err = fat32_get_next_cluster(fs, handle->current_cluster, &handle->current_cluster);
            if(err_is_fail(err)) {
                return err;
            }

        }
        // Safety assertion
        assert(0 < handle->current_cluster && handle->current_cluster < BAD_CLUSTER);
    }

    if(handle->file_position > handle->entry.file_size) {
        handle->entry.file_size = handle->file_position;
        err = fat32_update_directory(fs, handle);
        if(err_is_fail(err)) {
            return err;
        }
    }

    *nb_bytes_written = bytes_written_current;

    return SYS_ERR_OK;
}

errval_t fat32_file_seek(struct fat32_filesystem *fs, struct fat32_handle *handle, uint32_t pos) {
    errval_t err = SYS_ERR_OK;

    assert(handle->is_directory == false);

    err = fat32_handle_valid(fs, handle);
    if(err_is_fail(err)) {
        return err;
    }

    uint32_t position = MIN(pos,handle->entry.file_size);
    uint32_t current_cluster = get_cluster_number(&handle->entry);
    uint32_t nb_clusters = position / (FAT_BLOCK_SIZE * fs->sectors_per_cluster);

    uint32_t current_cluster_count = 0;
    if(handle->file_position / (FAT_BLOCK_SIZE * fs->sectors_per_cluster) <= nb_clusters) {
        current_cluster_count = handle->file_position / (FAT_BLOCK_SIZE * fs->sectors_per_cluster);
        current_cluster = handle->current_cluster;
    }

    while(current_cluster_count < nb_clusters) {
        assert(current_cluster < BAD_CLUSTER);
        err = fat32_get_next_cluster(fs, current_cluster, &current_cluster);
        if(err_is_fail(err)) {
            return err;
        }

        current_cluster_count++;
    }

    // Update the handle
    handle->current_cluster = current_cluster;
    handle->relative_sector_from_cluster = (pos / FAT_BLOCK_SIZE) % fs->sectors_per_cluster;
    handle->file_position = pos;

    return SYS_ERR_OK;
}

errval_t fat32_seek(struct fat32_filesystem *fs, struct fat32_handle *handle,enum fs_seekpos whence,off_t offset) {
    struct fat32_handle *h = handle;
    errval_t err;

    if(fs == NULL) {
        return VFS_ERR_UNKNOWN_FILESYSTEM;
    }

    switch (whence) {
        case FS_SEEK_SET:
            assert(offset >= 0);
            if (h->is_directory) {
                assert(!"NYI");
            } else {
                err = fat32_file_seek(fs, handle, offset);
                if(err_is_fail(err)) {
                    return err;
                }
            }
            break;

        case FS_SEEK_CUR:
            if (h->is_directory) {
                assert(!"NYI");
            } else {
                assert(offset >= 0 || -offset <= h->file_position);
                err = fat32_file_seek(fs, handle, (int32_t)h->file_position + (int32_t)offset);
                if(err_is_fail(err)) {
                    return err;
                }
            }

            break;

        case FS_SEEK_END:
            if (h->is_directory) {
                assert(!"NYI");
            } else {
                assert(offset >= 0 || -offset <= (int32_t )h->entry.file_size);
                err = fat32_file_seek(fs, handle, (int32_t)h->entry.file_size - (int32_t)offset);
                if(err_is_fail(err)) {
                    return err;
                }
            }
            break;

        default:
            USER_PANIC("invalid whence argument to ramfs seek");
    }

    return SYS_ERR_OK;
}

errval_t fat32_tell(struct fat32_filesystem *fs, struct fat32_handle *handle, size_t *pos) {
    (void)fs;
    if (handle->is_directory) {
        *pos = 0;
    } else {
        *pos = handle->file_position;
    }
    return SYS_ERR_OK;
}

// Directory functions - read
errval_t fat32_open_directory(struct fat32_filesystem *fs, const char *path, struct fat32_handle **handle) {
    errval_t err = SYS_ERR_OK;

    if(fs == NULL) {
        return VFS_ERR_UNKNOWN_FILESYSTEM;
    }

    // Security assertion
    assert(path[0] == FS_PATH_SEP);

    *handle = malloc(sizeof(struct fat32_handle));
    if(*handle == NULL) {
        return LIB_ERR_MALLOC_FAIL;
    }

    err = fat32_resolve_path(fs, path, *handle);
    if(err_is_fail(err)) {
        return SYS_ERR_OK;
    }

    if(!(*handle)->is_directory) {
        return FS_ERR_NOTDIR;
    }

    return SYS_ERR_OK;
}

errval_t fat32_close_directory(struct fat32_filesystem *fs, struct fat32_handle *handle) {
    if(fs == NULL) {
        return VFS_ERR_UNKNOWN_FILESYSTEM;
    }

    if(!handle->is_directory) {
        return FS_ERR_NOTDIR;
    }
    //close_handle(handle);
    return SYS_ERR_OK;
}

errval_t fat32_increment_directory(struct fat32_filesystem *fs, struct fat32_handle *handle) {
    errval_t err = SYS_ERR_OK;

    handle->directory_offset++;

    // Nothing we need to update
    if(handle->directory_offset < NUMBER_DIRECTORY_PER_BLOCK) {
        return SYS_ERR_OK;
    }

    handle->directory_offset = 0;
    handle->relative_sector_from_cluster++;

    if(handle->relative_sector_from_cluster < fs->sectors_per_cluster) {
        return SYS_ERR_OK;
    }

    handle->relative_sector_from_cluster = 0;

    err = fat32_get_next_cluster(fs, handle->current_cluster, &handle->current_cluster);
    if(err_is_fail(err)) {
        return err;
    }

    return SYS_ERR_OK;
}

errval_t fat32_read_next_directory(struct fat32_filesystem *fs, struct fat32_handle *handle, char **new_name) {
    errval_t err = SYS_ERR_OK;

    if(fs == NULL) {
        return VFS_ERR_UNKNOWN_FILESYSTEM;
    }

    err = fat32_handle_valid(fs,handle);
    if(err_is_fail(err)) {
        return err;
    }

    // Security assertion
    assert(new_name != NULL);

    if(!handle->is_directory) {
        return FS_ERR_NOTDIR;
    }

    bool found = false;
    uint8_t data[FAT_BLOCK_SIZE];

    while(!found) {
        size_t lba = get_lba_from_cluster(fs, handle->current_cluster) + handle->relative_sector_from_cluster;
        err = read_block(fs->b_driver, lba, (void *) data);
        if(err_is_fail(err)) {
            return err;
        }

        struct fat32_entry *entry = ((struct fat32_entry *) data) + handle->directory_offset;

        if(is_end_directory(entry)) {
            return FS_ERR_INDEX_BOUNDS;
        }

        if(is_entry_valid(entry)) {
            found = true;
            err = shortname_to_name(entry->name, new_name);
            if(err_is_fail(err)) {
                return err;
            }
        }

        // Last step -> Go to the next entry
        err = fat32_increment_directory(fs, handle);
        if(err_is_fail(err)) {
            return err;
        }
    }

    return SYS_ERR_OK;
}

errval_t fat32_setup_empty_directory(struct fat32_filesystem *fs, struct fat32_handle *handle)  {
    errval_t err = SYS_ERR_OK;

    set_directory(&handle->entry);

    // Read bloc
    uint32_t lba = get_lba_from_cluster(fs, get_cluster_number(&handle->entry));
    uint8_t data[FAT_BLOCK_SIZE];
    err = read_block(fs->b_driver, lba, (void *) data);
    if (err_is_fail(err)) {
        return err;
    }
    // Clear the entry
    memset(data, 0, FAT_BLOCK_SIZE);
    // Create '.'
    const char *name = ".";
    create_directory_entry((struct fat32_entry*)data, name, get_cluster_number(&handle->entry));
    // Create '..'
    const char *name2 = "..";
    create_directory_entry((struct fat32_entry*)(data+32), name2, handle->parent_cluster_number);
    // Write bloc
    err = write_block(fs->b_driver, lba, (void *) data);
    if (err_is_fail(err)) {
        return err;
    }

    return SYS_ERR_OK;
}

errval_t fat32_is_directory(struct fat32_filesystem *fs, const char *path, bool *is_path_directory) {
    errval_t err = SYS_ERR_OK;

    if(fs == NULL) {
        return VFS_ERR_UNKNOWN_FILESYSTEM;
    }

    struct fat32_handle handle;
    err = fat32_resolve_path(fs, path, &handle);
    if(err_is_fail(err)) {
        return err;
    }

    *is_path_directory = is_directory(&handle.entry);

    return SYS_ERR_OK;
}

// Cluster functions
errval_t fat32_write_fat_table(struct fat32_filesystem *fs, uint32_t idx, void *block) {
    errval_t err = SYS_ERR_OK;

    for(int i = 0; i < fs->fat32_number;i++) {
        err = write_block(fs->b_driver, i * fs->sectors_per_fat + idx, block);
        if(err_is_fail(err)) {
            return err;
        }
    }

    return SYS_ERR_OK;
}

errval_t fat32_cluster_clean(struct fat32_filesystem *fs, uint32_t cluster_number) {
    errval_t err = SYS_ERR_OK;

    uint8_t cleaned_block[FAT_BLOCK_SIZE];
    memset(cleaned_block, 0, FAT_BLOCK_SIZE);

    const size_t base_lba = get_lba_from_cluster(fs, cluster_number);
    for(int offset = 0; offset < fs->sectors_per_cluster; offset++) {
        err = write_block(fs->b_driver, base_lba + offset, (void*)cleaned_block);
        if(err_is_fail(err)) {
            return err;
        }
    }

    return err;
}

errval_t fat32_cluster_malloc(struct fat32_filesystem *fs, uint32_t number_clusters_to_allocate, uint32_t *first_cluster_number) {
    errval_t err = SYS_ERR_OK;

    // Safety assertion
    assert(number_clusters_to_allocate > 0);

    uint32_t nb_allocated_sectors = 0;
    uint32_t start_sector =  fs->next_free_cluster_hint / FAT_ENTRIES_PER_SECTOR - 1;
    uint32_t current_sector = fs->next_free_cluster_hint / FAT_ENTRIES_PER_SECTOR;
    uint32_t fat_table[FAT_ENTRIES_PER_SECTOR];

    uint32_t last_cluster_number = END_CLUSTER;

    while(current_sector != start_sector && nb_allocated_sectors < number_clusters_to_allocate) {

        const uint32_t current_lba = fs->numbers_sectors_reserved + current_sector;

        err = read_block(fs->b_driver, current_lba, (void *) fat_table);
        if(err_is_fail(err)) {
            return err;
        }

        for(int i = 0; i < FAT_ENTRIES_PER_SECTOR;i++) {
            // Edge case
            if(current_sector == 0 && i < 2) {
                continue;
            }
            // Used -> skip
            if(!is_cluster_free(fat_table[i])) {
                continue;
            }

            fat_table[i] = last_cluster_number;
            last_cluster_number = i + FAT_ENTRIES_PER_SECTOR * current_sector;

            err = fat32_cluster_clean(fs, get_lba_from_cluster(fs, last_cluster_number));
            if(err_is_fail(err)) {
                return err;
            }

            nb_allocated_sectors++;

            if(nb_allocated_sectors == number_clusters_to_allocate) {
                break;
            }
        }

        err = fat32_write_fat_table(fs, current_lba, (void*)fat_table);
        if(err_is_fail(err)) {
            assert(false);
            return err;
        }

        current_sector = (current_sector + 1) % fs->sectors_per_fat;
    }

    if(nb_allocated_sectors < number_clusters_to_allocate) {
        fat32_remove_chain(fs, last_cluster_number);
        return LIB_ERR_MALLOC_FAIL;
    }

    *first_cluster_number = last_cluster_number;
    fs->next_free_cluster_hint = last_cluster_number + 1;

    return SYS_ERR_OK;
}

errval_t fat32_traverse_chain(struct fat32_filesystem *fs, uint32_t start_cluster_number, uint32_t *nb_clusters_traversed, uint32_t *last_cluster_number) {
    errval_t err = SYS_ERR_OK;

    // Security assertions
    assert(nb_clusters_traversed && last_cluster_number);

    uint32_t fat_table[FAT_ENTRIES_PER_SECTOR];
    uint32_t current_cluster_number = start_cluster_number;
    uint32_t prev_cluster_number = 0;
    uint32_t count_nb_clusters_traversed = 0;
    uint32_t lba = fs->numbers_sectors_reserved + current_cluster_number / FAT_ENTRIES_PER_SECTOR;
    uint32_t prev_lba = lba-1;

    while(!is_end_cluster(current_cluster_number)) {
        // New block -> read
        if(lba != prev_lba) {
            err = read_block(fs->b_driver, lba, (void *) fat_table);
            if (err_is_fail(err)) {
                return err;
            }
        }
        // Update sector
        prev_cluster_number = current_cluster_number;
        current_cluster_number = fat_table[current_cluster_number % FAT_ENTRIES_PER_SECTOR];
        // Update lba address
        prev_lba = lba;
        lba = fs->numbers_sectors_reserved + current_cluster_number / FAT_ENTRIES_PER_SECTOR;
        // Update number of clusters traversed
        count_nb_clusters_traversed++;
    }

    *nb_clusters_traversed = count_nb_clusters_traversed;
    *last_cluster_number = prev_cluster_number;

    return SYS_ERR_OK;
}

errval_t fat32_remove_chain(struct fat32_filesystem *fs, uint32_t cluster_number) {
    errval_t err = SYS_ERR_OK;

    uint32_t fat_table[FAT_ENTRIES_PER_SECTOR];

    uint32_t lba = fs->numbers_sectors_reserved + cluster_number / FAT_ENTRIES_PER_SECTOR;
    uint32_t lba_last = lba - 1;

    while(!is_end_cluster(cluster_number)) {
        if(lba_last != lba) {
            err = read_block(fs->b_driver, lba, (void*) fat_table);
            if (err_is_fail(err)) {
                return err;
            }
        }

        uint32_t new_cluster_number = fat_table[cluster_number % FAT_ENTRIES_PER_SECTOR];
        fat_table[cluster_number % FAT_ENTRIES_PER_SECTOR] = 0;
        cluster_number = new_cluster_number;

        lba_last = lba;
        lba = fs->numbers_sectors_reserved + cluster_number / FAT_ENTRIES_PER_SECTOR;

        if(lba_last != lba || is_end_cluster(cluster_number)) {
            err = fat32_write_fat_table(fs, lba_last, (void*)fat_table);
            if(err_is_fail(err)) {
                return err;
            }
        }
    }

    return SYS_ERR_OK;
}

errval_t fat32_increase_chain(struct fat32_filesystem *fs, uint32_t cluster_number, uint32_t size) {
    errval_t err = SYS_ERR_OK;

    // This is a valid edge case
    if(size == 0) {
        return SYS_ERR_OK;
    }

    uint32_t other_chain_start_number;
    err = fat32_cluster_malloc(fs, size, &other_chain_start_number);
    if(err_is_fail(err)) {
        return err;
    }

    uint32_t lba_address = fs->numbers_sectors_reserved + cluster_number / FAT_ENTRIES_PER_SECTOR;
    uint32_t fat_table[FAT_ENTRIES_PER_SECTOR];
    uint32_t offset = cluster_number % FAT_ENTRIES_PER_SECTOR;

    err = read_block(fs->b_driver, lba_address, (void *) fat_table);
    if (err_is_fail(err)) {
        return err;
    }

    assert(fat_table[offset] >= END_CLUSTER);
    fat_table[offset] = other_chain_start_number;

    err = fat32_write_fat_table(fs, lba_address, (void*)fat_table);
    if(err_is_fail(err)) {
        return err;
    }

    return SYS_ERR_OK;
}

errval_t fat32_get_number_of_required_clusters(struct fat32_filesystem *fs, struct fat32_handle *handle, const uint32_t new_size, uint32_t *nb_clusters_required, uint32_t *last_cluster_number) {
    errval_t err = SYS_ERR_OK;

    // Security assertions
    assert(nb_clusters_required && last_cluster_number);

    uint32_t start_cluster_number = get_cluster_number(&handle->entry);
    uint32_t nb_clusters_traversed;
    err = fat32_traverse_chain(fs, start_cluster_number, &nb_clusters_traversed, last_cluster_number);
    if(err_is_fail(err)) {
        return err;
    }

    // Calculate the number of required clusters
    *nb_clusters_required = get_number_additional_clusters(fs, new_size, nb_clusters_traversed);

    return SYS_ERR_OK;
}

// Delete functions
errval_t fat32_update_directory(struct fat32_filesystem *fs, struct fat32_handle *handle) {
    errval_t err = SYS_ERR_OK;

    uint8_t data[FAT_BLOCK_SIZE];

    uint32_t lba = get_lba_from_cluster(fs, handle->parent_cluster_number) + handle->parent_cluster_offset / NUMBER_DIRECTORY_PER_BLOCK;
    err = read_block(fs->b_driver, lba, (void *) data);
    if (err_is_fail(err)) {
        return err;
    }

    struct fat32_entry *e = (struct fat32_entry*)(data + ((32*handle->parent_cluster_offset) % 512));
    *e = handle->entry;

    err = write_block(fs->b_driver, lba, (void *) data);
    if (err_is_fail(err)) {
        return err;
    }

    return SYS_ERR_OK;
}

errval_t fat32_remove(struct fat32_filesystem *fs, const char *path) {
    errval_t err = SYS_ERR_OK;

    if(fs == NULL) {
        return VFS_ERR_UNKNOWN_FILESYSTEM;
    }

    assert(path && path[0] == FS_PATH_SEP);

    struct fat32_handle handle;

    err = fat32_resolve_path(fs, path, &handle);
    if (err_is_fail(err)) {
        return err;
    }

    if (handle.is_directory) {
        return FS_ERR_NOTFILE;
    }

    err = fat32_remove_chain(fs, get_cluster_number(&handle.entry));
    if (err_is_fail(err)) {
        return err;
    }

    handle.entry.name[0] = DIRECTORY_FREE_VALUE;
    fat32_update_directory(fs, &handle);

    return SYS_ERR_OK;
}

errval_t fat32_check_directory_empty(struct fat32_filesystem *fs, struct fat32_handle *handle) {
    errval_t err = SYS_ERR_OK;

    if(fs == NULL) {
        return VFS_ERR_UNKNOWN_FILESYSTEM;
    }

    struct fat32_entry tmp;
    uint32_t tmp2, tmp3;
    err = fat32_find_directory(fs, FIND_USED_ENTRY, &handle->entry, NULL, &tmp, &tmp2, &tmp3);
    if(err == FS_ERR_NOTFOUND) {
        return SYS_ERR_OK;
    } else if(err_is_fail(err)) {
        return err;
    } else {
        return FS_ERR_NOTEMPTY;
    }
}

errval_t fat32_remove_directory(struct fat32_filesystem *fs, const char *path) {
    errval_t err = SYS_ERR_OK;

    if(fs == NULL) {
        return VFS_ERR_UNKNOWN_FILESYSTEM;
    }

    // Security assertion
    assert(path && path[0] == FS_PATH_SEP);

    struct fat32_handle handle;

    err = fat32_resolve_path(fs, path, &handle);
    if(err_is_fail(err)) {
        return err;
    }

    if(!handle.is_directory) {
        return FS_ERR_NOTDIR;
    }

#ifdef FORCE_EMPTY_DELETE
    err = fat32_check_directory_empty(fs, &handle);
    if(err_is_fail(err)) {
        return err;
    }
#endif

    err = fat32_remove_chain(fs, get_cluster_number(&handle.entry));
    if(err_is_fail(err)) {
        return err;
    }

    handle.entry.name[0] = DIRECTORY_FREE_VALUE;
    err = fat32_update_directory(fs, &handle);
    if(err_is_fail(err)) {
        return err;
    }

    return SYS_ERR_OK;
}

// Creation functions
errval_t fat32_get_free_entry(struct fat32_filesystem *fs, struct fat32_handle *parent_handle, struct fat32_handle *handle) {
    errval_t err = SYS_ERR_OK;

    // Step 1) Get directory
    struct fat32_entry new_entry;
    uint32_t parent_cluster_number;
    uint32_t parent_cluster_offset;
    char tmp[1];
    tmp[0] = 0;
    err = fat32_find_directory(fs,FIND_FREE_ENTRY,&parent_handle->entry,tmp,&new_entry, &parent_cluster_number, &parent_cluster_offset);
    if(err_is_fail(err)) {
        return FAT_ERR_CLUSTER_NOT_FREE;
    }

    // Create the file handle
    handle->entry = new_entry;
    handle->current_cluster = 0;
    handle->relative_sector_from_cluster = 0;
    handle->parent_cluster_number = parent_cluster_number;
    handle->parent_cluster_offset = parent_cluster_offset;

    // In this case, we can simply increase the chain and it is enough
    if(index_in_new_cluster(fs,parent_cluster_offset)) {
        err = fat32_increase_chain(fs, parent_cluster_number, 1);
        if(err_is_fail(err)) {
            return err;
        }
        return SYS_ERR_OK;
    }

    return SYS_ERR_OK;
}

errval_t fat32_create_entry(struct fat32_filesystem *fs, const char *path, bool is_directory, struct fat32_handle **handle) {
    errval_t err = SYS_ERR_OK;

    // Security assertion
    assert(fs && path && handle);
    assert(path[0] == FS_PATH_SEP);

    // Create handle
    *handle = malloc(sizeof(struct fat32_handle));
    if(*handle == NULL) {
        return LIB_ERR_MALLOC_FAIL;
    }

    // Check if exists
    err = fat32_exits(fs, path, *handle);
    if(err_is_ok(err)) {
        return FS_ERR_EXISTS;
    }

    // Split parent and child path
    char *separation = strrchr(path, FS_PATH_SEP);
    if(separation == NULL) {
        return FS_ERR_NOTFOUND;
    }

    uint32_t parent_string_size = separation - path + 1;
    if(parent_string_size == 0)  {
        parent_string_size = 1;
    }
    char *parent_path = (char*)malloc(sizeof(char)*(parent_string_size + 1));

    memcpy(parent_path, path, parent_string_size);
    parent_path[parent_string_size] = 0;
    const char *child_name = separation + 1;

    if(!is_fat32_name_valid(child_name, is_directory)) {
        return FAT_ERR_BAD_FILENAME;
    }

    // Check parents exits
    struct fat32_handle parent_handle;
    err = fat32_resolve_path(fs, parent_path, &parent_handle);
    if(err_is_fail(err)) {
        return err;
    }

    if(!parent_handle.is_directory) {
        return FS_ERR_NOTDIR;
    }

    // Get free entry
    struct fat32_handle *child_handle = malloc(sizeof(struct fat32_handle));
    err = fat32_get_free_entry(fs, &parent_handle, child_handle);
    if(err_is_fail(err)) {
        return err;
    }

    // Allocate cluster for the free entry
    uint32_t cluster_number;
    err = fat32_cluster_malloc(fs, 1, &cluster_number);
    if(err_is_fail(err)) {
        return err;
    }

    // Set the values inside of struct fat32_entry
    memset(&child_handle->entry, 0, sizeof(struct fat32_entry));

    name_to_shortname(child_name, (char*)&(child_handle->entry.name));

    set_cluster_number(&child_handle->entry, cluster_number);
    if(is_directory) {
        err = fat32_setup_empty_directory(fs, child_handle);
        if(err_is_fail(err)) {
            return err;
        }
    }

    // Save values from handle into the disk
    err = fat32_update_directory(fs, child_handle);
    if(err_is_fail(err)) {
        return err;
    }

    *handle = child_handle;

    // Free alloacted memory
    free(parent_path);
    return err;
}

errval_t fat32_create(struct fat32_filesystem *fs, const char *path, struct fat32_handle **handle) {
    if(fs == NULL) {
        return VFS_ERR_UNKNOWN_FILESYSTEM;
    } else {
        return fat32_create_entry(fs, path, false, handle);
    }
}

errval_t fat32_mkdir(struct fat32_filesystem *fs, const char *path, struct fat32_handle **handle) {
    if(fs == NULL) {
        return VFS_ERR_UNKNOWN_FILESYSTEM;
    } else {
        return fat32_create_entry(fs, path, true, handle);
    }
}

// Helpers functions
void print_filesystem(struct fat32_filesystem *fat_mount) {

    debug_printf("==================================\n");
    debug_printf("Informations about the filesystem\n");
    debug_printf("This is a fat32 filesystem\n");
    debug_printf("==================================\n");
    debug_printf("First cluster root directory : %d\n", fat_mount->first_cluster_root_directory);
    debug_printf("Sectors per cluster          : %d\n", fat_mount->sectors_per_cluster);
    debug_printf("Number reserved sectors      : %d\n", fat_mount->numbers_sectors_reserved);
    debug_printf("Number of fat tables         : %d\n", fat_mount->fat32_number);
    debug_printf("Sectors per fat tables       : %d\n", fat_mount->sectors_per_fat);
    debug_printf("First data cluster           : %d\n", fat_mount->cluster_begin_data);
    debug_printf("==================================\n");

    return;
}

errval_t print_directory(uint32_t cluster_number, uint32_t lba_start, struct fat32_filesystem *fs) {

    errval_t err = SYS_ERR_OK;

    uint8_t next = 1;

    uint32_t current_cluster_number = cluster_number;

    while (next) {
        // Parse the content
        for (int j = 0; j < fs->sectors_per_cluster; j++) {
            uint8_t data[FAT_BLOCK_SIZE];

            err = read_block(fs->b_driver, lba_start + j, (void *) data);
            if (err_is_fail(err)) {
                return err;
            }

            for (int i = 0; i < 16; i++) {
                struct fat32_entry
                        test = *((struct fat32_entry
                *) (data + 32 * i));
                if ((test.name[0] != 0xe5 && test.name[0] != 0x00)) {
                    debug_printf("====================\n");
                    debug_printf("Name : %s\n", test.name);
                    debug_printf("====================\n");
                    debug_printf("First cluster : %d\n", get_cluster_number(&test));
                    debug_printf("Size : %p\n", test.file_size);
                    uint32_t first_cluster = get_cluster_number(&test);

                    debug_printf("Lba address : %d\n", get_lba_from_cluster(fs, first_cluster));
                    debug_printf("Current cluster nb : %d\n", first_cluster);

                    if (test.attribute & ATTR_DIRECTORY) debug_printf("Is directory\n");
                    debug_printf("====================\n");
                }

                if (test.name[0] == 0x00) {
                    next = 0;
                }
            }
        }

        uint32_t next_cluster_number = 0;
        fat32_get_next_cluster(fs, current_cluster_number, &next_cluster_number);

        if (next_cluster_number >= END_CLUSTER) {
            next = 0;
        }
    }

    return SYS_ERR_OK;
}

// Mount filesystem
struct fat32_filesystem *get_mounted_filesystem(void) {
    return current_filesystem;
}

errval_t mount_filesystem(void) {
    errval_t err = SYS_ERR_OK;

    // Step 0) Launch the block driver
    struct block_driver *b_driver = malloc(sizeof(struct block_driver)); // TOOD: Put in fa32
    err = launch_driver(b_driver);
    if(err_is_fail(err)) {
        debug_printf("Failed to launch the block driver\n");
    }

    // Step 1) Allocate fat32 data structure
    current_filesystem = malloc(sizeof(struct fat32_filesystem));
    current_filesystem->b_driver = b_driver;

    // Step 2) Read the first block
    uint8_t first_block[FAT_BLOCK_SIZE];

    err = read_block(b_driver, 0, (void *) first_block);
    if (err_is_fail(err)) {
        return err;
    }

    debug_printf("Read first block with success\n");

    // Step 3) Check magic number && bytes 0:2
    assert(first_block[510] == 0x55);
    assert(first_block[511] == 0xAA);

    debug_printf("Disk contains boot magic number\n");

    if(first_block[0] != 0xeb) {
        return FAT_ERR_BAD_FS;
    }

    if(*(uint16_t * )(first_block + BYTES_PER_SECTOR_OFFSET) != FAT_BLOCK_SIZE) {
        return FAT_ERR_BAD_FS;
    }

    // Step 4) Fill fat32 data structure
    current_filesystem->first_cluster_root_directory = *(uint32_t * )(first_block + ROOT_CLUSTER_START_OFFSET);
    current_filesystem->sectors_per_cluster = *(uint16_t * )(first_block + SECTOR_PER_CLUSTER_OFFSET);
    current_filesystem->numbers_sectors_reserved = *(uint16_t * )(first_block + RESERVED_SECTORS_OFFSET);
    current_filesystem->fat32_number = *(uint8_t * )(first_block + NUMBER_FAT_OFFSET);
    current_filesystem->sectors_per_fat = *(uint16_t * )(first_block + SECTORS_PER_FAT_OFFSET);
    current_filesystem->cluster_begin_data =
        current_filesystem->numbers_sectors_reserved + (current_filesystem->fat32_number * current_filesystem->sectors_per_fat);
    set_cluster_number(&current_filesystem->root_directory, 0);
    set_directory(&current_filesystem->root_directory);

    // Step 5) Mount the filesystem
    print_filesystem(current_filesystem);

#ifdef TESTS_FAT
    // STEP 6) OPT - run the tests to make sure the filesystem works
    //test_driver(b_driver);
    //run_all_tests(current_filesystem);
    //run_benchmarks(current_filesystem);
#endif
    return SYS_ERR_OK;
}