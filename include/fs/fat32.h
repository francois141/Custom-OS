//
// Created by francois on 30.04.23.
//

#ifndef AOS2023_TEAM09_HAND_IN_FAT_H
#define AOS2023_TEAM09_HAND_IN_FAT_H

#include <stdio.h>
#include <ctype.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdint.h>

#include <aos/aos.h>

#include <fs/fs.h>


#include "../block_driver/blockdriver.h"

#define FAT_BLOCK_SIZE 512

// Definitions for the partition boot sector
#define BYTES_PER_SECTOR_OFFSET 0x0B
#define SECTOR_PER_CLUSTER_OFFSET 0x0D
#define RESERVED_SECTORS_OFFSET 0x0E
#define NUMBER_FAT_OFFSET 0x10
#define ROOT_ENTRIES_OFFSET 0x11
#define SMALL_SECTORS_OFFSET 0x13
#define MEDIA_TYPE_OFFSET 0x15
#define SECTORS_PER_TRACK_OFFSET 0x18
#define NUMBER_OF_HEADS_OFFSET 0x1a
#define HIDDENS_SECTORS_OFFSET 0x1C
#define LARGE_SECTORS_OFFSET 0x20
#define ROOT_CLUSTER_START_OFFSET 0x2C
#define SECTORS_PER_FAT_OFFSET 0x24
#define SIGNATURE_OFFSET 0x26
#define VOLUME_SERIAL_NUMBER_OFFSET 0x27
#define VOLUME_LABEL_OFFSET 0x2B
#define SYSTEM_ID_OFFSET 0x36
#define MAGIC_NUMBER_OFFSET 0x1FE

#define MAGIC_NUMBER 0x55AA

#define BAD_CLUSTER 0xffffff7
#define END_CLUSTER 0xffffff8

#define DIRECTORY_FREE_VALUE 0xE5
#define END_DIRECTORY 0x00

#define ATTR_DIRECTORY 0x10

#define FS_PATH_SEP '/'

#define NUMBER_DIRECTORY_PER_BLOCK 16
#define FAT_ENTRIES_PER_SECTOR 128

#define FIND_NAME_MATCH 0
#define FIND_FREE_ENTRY 1
#define FIND_USED_ENTRY 2

struct fat32_entry {
    char name[11];
    uint8_t attribute;
    uint8_t reserved_zero_entry;
    uint8_t creation_time;
    uint8_t empty[6];
    uint16_t cluster_high;
    uint16_t last_modifier_time;
    uint16_t last_modified_date;
    uint16_t cluster_low;
    uint32_t file_size;
} __attribute__((packed));

struct fat32_handle {
    // Current entry
    struct fat32_entry entry;
    // Information about the cluster
    uint32_t current_cluster;
    uint32_t relative_sector_from_cluster;
    // Information for the c library on top of it
    union {
        uint32_t directory_offset;
        uint32_t file_position;
    };
    char *path;
    bool is_directory;
    // Entries about the parent - required to modify entry
    uint32_t parent_cluster_number;
    uint32_t parent_cluster_offset;
};

struct fat32_filesystem {
    struct block_driver *b_driver;
    struct fat32_entry root_directory;
    uint32_t cluster_begin_data;
    uint32_t sectors_per_fat;
    uint32_t first_cluster_root_directory;
    uint8_t  sectors_per_cluster;
    uint16_t numbers_sectors_reserved;
    uint8_t  fat32_number;
    uint32_t next_free_cluster_hint;
};

// Attribute management
bool is_directory(const struct fat32_entry *entry);
void set_directory(struct fat32_entry *entry);
bool is_file(const struct fat32_entry *entry);

// Name management
size_t name_len(const char *shortname);
size_t extension_len(const char *shortname);
bool check_chars(const char *name, size_t from, size_t to);
bool is_fat32_name_valid(const char *name, bool is_directory);
void name_to_shortname(const char *input_name, char *output_shortname);
errval_t shortname_to_name(char *input_shortname, char **output_name);
bool compare_filename_with_entry(struct fat32_entry *entry, const char *path);

// Directory management
bool is_directory_used(struct fat32_entry *entry);
bool is_directory_free(struct fat32_entry *entry);
bool is_end_directory(struct fat32_entry *entry);
bool is_entry_valid(struct fat32_entry *entry);
void create_directory_entry(struct fat32_entry *entry, const char *name, uint32_t cluster_number);

// Handle management
void close_handle(struct fat32_handle *handle);

// Cluster management
uint32_t get_cluster_number(const struct fat32_entry *entry);
void set_cluster_number(struct fat32_entry *entry, uint32_t cluster_number);
bool is_end_cluster(uint32_t cluster_number);
bool is_cluster_free(uint32_t cluster_number);
bool index_in_new_cluster(struct fat32_filesystem *fs, uint32_t index);
uint32_t get_number_additional_clusters(struct fat32_filesystem *fs, uint32_t new_size, uint32_t nb_clusters_current);

// Address management
uint32_t get_lba_from_cluster(const struct fat32_filesystem *fat_fs, uint32_t cluster_number);
errval_t fat32_get_next_cluster(const struct fat32_filesystem *fat_mount, uint32_t cluster_number, uint32_t *ret_cluster_number);

// Walk functions
errval_t fat32_read_directory(struct fat32_filesystem *fs, const struct fat32_entry *entry, size_t *nb_entries, struct fat32_entry **out_directories);
errval_t fat32_read_file(struct fat32_filesystem *fs, const struct fat32_entry *entry, size_t *size, void **out_data);
errval_t fat32_find_directory(const struct fat32_filesystem *fs,int arg, struct fat32_entry *entry, const char *path, struct fat32_entry *new_entry, uint32_t *parent_cluster_number, uint32_t *parent_cluster_offset);
errval_t fat32_resolve_path(const struct fat32_filesystem *fat_mount, const char *path,struct fat32_handle *output);
errval_t fat32_exits(struct fat32_filesystem *fs, const char *path, struct fat32_handle *handle);
errval_t fat32_handle_valid(struct fat32_filesystem *fs, const struct fat32_handle *handle);

// File functions - read
errval_t fat32_open(struct fat32_filesystem *fs, const char *path, struct fat32_handle **handle);
errval_t fat32_close(struct fat32_filesystem *fs, struct fat32_handle *handle);
errval_t fat32_stat(struct fat32_filesystem *fs, const struct fat32_handle *handle, struct fs_fileinfo *info);
errval_t fat32_read(struct fat32_filesystem *fs, struct fat32_handle *handle, void *data, size_t size, size_t *nb_bytes_read);
errval_t fat32_write(struct fat32_filesystem *fs, struct fat32_handle *handle, const void *data, size_t size, size_t *nb_bytes_written);

errval_t fat32_file_seek(struct fat32_filesystem *fs, struct fat32_handle *handle, uint32_t pos);
errval_t fat32_seek(struct fat32_filesystem *fs, struct fat32_handle *handle,enum fs_seekpos whence,off_t offset);
errval_t fat32_tell(struct fat32_filesystem *fs, struct fat32_handle *handle, size_t *pos);

// Directory functions - read
errval_t fat32_open_directory(struct fat32_filesystem *fs, const char *path, struct fat32_handle **handle);
errval_t fat32_close_directory(struct fat32_filesystem *fs, struct fat32_handle *handle);
errval_t fat32_increment_directory(struct fat32_filesystem *fs, struct fat32_handle *handle);
errval_t fat32_read_next_directory(struct fat32_filesystem *fs, struct fat32_handle *handle, char **new_name);
errval_t fat32_setup_empty_directory(struct fat32_filesystem *fs, struct fat32_handle *handle);
errval_t fat32_is_directory(struct fat32_filesystem *fs, const char *path, bool *is_path_directory);

// Cluster functions
errval_t fat32_write_fat_table(struct fat32_filesystem *fs, uint32_t idx, void *block);
errval_t fat32_cluster_clean(struct fat32_filesystem *fs, uint32_t cluster_number);
errval_t fat32_cluster_malloc(struct fat32_filesystem *fs, uint32_t number_cluster, uint32_t *first_cluster_number);
errval_t fat32_traverse_chain(struct fat32_filesystem *fs, uint32_t start_cluster_number, uint32_t *nb_clusters_traversed, uint32_t *last_cluster_number);
errval_t fat32_remove_chain(struct fat32_filesystem *fs, uint32_t cluster_number);
errval_t fat32_increase_chain(struct fat32_filesystem *fs, uint32_t cluster_number, uint32_t size);
errval_t fat32_get_number_of_required_clusters(struct fat32_filesystem *fs, struct fat32_handle *handle, const uint32_t new_size, uint32_t *nb_clusters_required, uint32_t *last_cluster_number);

// Delete functions
errval_t fat32_update_directory(struct fat32_filesystem *fs, struct fat32_handle *handle);
errval_t fat32_remove(struct fat32_filesystem *fs, const char *path);
errval_t fat32_check_directory_empty(struct fat32_filesystem *fs, struct fat32_handle *handle);
errval_t fat32_remove_directory(struct fat32_filesystem *fs, const char *path);

// Creation functions
errval_t fat32_get_free_entry(struct fat32_filesystem *fs, struct fat32_handle *parent_handle, struct fat32_handle *handle);
errval_t fat32_create_entry(struct fat32_filesystem *fs, const char *path, bool is_directory, struct fat32_handle **handle);
errval_t fat32_create(struct fat32_filesystem *fs, const char *path, struct fat32_handle **handle);
errval_t fat32_mkdir(struct fat32_filesystem *fs, const char *path, struct fat32_handle **handle);

// Helpers functions
void print_filesystem(struct fat32_filesystem *fat_mount);
errval_t print_directory(uint32_t cluster_number, uint32_t lba_start, struct fat32_filesystem *fs);

// Mount filesystem
struct fat32_filesystem* get_mounted_filesystem(void);
errval_t mount_filesystem(void);

#endif  // AOS2023_TEAM09_HAND_IN_FAT_H
