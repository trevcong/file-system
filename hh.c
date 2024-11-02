#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>
#include <stdint.h>

#define MAX_FILENAME 32
#define MAX_FILES 256
#define MAX_FILE_SIZE 1024
#define BLOCK_SIZE 64
#define MAX_BLOCKS (MAX_FILE_SIZE / BLOCK_SIZE)

// File system structures
typedef struct {
    char name[MAX_FILENAME];
    uint32_t size;
    uint32_t blocks[MAX_BLOCKS];  // Block numbers used by file
    uint8_t num_blocks;
    time_t created;
    time_t modified;
    uint8_t is_directory;
} inode_t;

typedef struct {
    uint8_t data[BLOCK_SIZE];
} block_t;

typedef struct {
    // File system metadata
    uint32_t total_blocks;
    uint32_t free_blocks;
    uint32_t total_inodes;
    uint32_t free_inodes;
    
    // Storage
    block_t* blocks;
    inode_t* inodes;
    uint8_t* block_map;    // 1 = used, 0 = free
    uint8_t* inode_map;    // 1 = used, 0 = free
} filesystem_t;

// Function declarations
filesystem_t* fs_create(void);
void fs_destroy(filesystem_t* fs);
int fs_create_file(filesystem_t* fs, const char* filename);
int fs_write(filesystem_t* fs, const char* filename, const uint8_t* data, size_t size);
int fs_read(filesystem_t* fs, const char* filename, uint8_t* buffer, size_t size);
int fs_delete(filesystem_t* fs, const char* filename);
void fs_list_files(filesystem_t* fs);
int fs_create_directory(filesystem_t* fs, const char* dirname);

// Initialize file system
filesystem_t* fs_create(void) {
    filesystem_t* fs = malloc(sizeof(filesystem_t));
    if (!fs) return NULL;

    // Initialize metadata
    fs->total_blocks = MAX_FILE_SIZE / BLOCK_SIZE;
    fs->free_blocks = fs->total_blocks;
    fs->total_inodes = MAX_FILES;
    fs->free_inodes = MAX_FILES;

    // Allocate storage
    fs->blocks = calloc(fs->total_blocks, sizeof(block_t));
    fs->inodes = calloc(MAX_FILES, sizeof(inode_t));
    fs->block_map = calloc(fs->total_blocks, sizeof(uint8_t));
    fs->inode_map = calloc(MAX_FILES, sizeof(uint8_t));

    if (!fs->blocks || !fs->inodes || !fs->block_map || !fs->inode_map) {
        fs_destroy(fs);
        return NULL;
    }

    return fs;
}

// Clean up file system
void fs_destroy(filesystem_t* fs) {
    if (fs) {
        free(fs->blocks);
        free(fs->inodes);
        free(fs->block_map);
        free(fs->inode_map);
        free(fs);
    }
}

// Find a free inode
static int find_free_inode(filesystem_t* fs) {
    for (uint32_t i = 0; i < fs->total_inodes; i++) {
        if (!fs->inode_map[i]) {
            fs->inode_map[i] = 1;
            fs->free_inodes--;
            return i;
        }
    }
    return -1;
}

// Find a free block
static int find_free_block(filesystem_t* fs) {
    for (uint32_t i = 0; i < fs->total_blocks; i++) {
        if (!fs->block_map[i]) {
            fs->block_map[i] = 1;
            fs->free_blocks--;
            return i;
        }
    }
    return -1;
}

// Create a new file
int fs_create_file(filesystem_t* fs, const char* filename) {
    if (!fs || !filename || strlen(filename) >= MAX_FILENAME) {
        return -1;
    }

    // Check if file already exists
    for (uint32_t i = 0; i < fs->total_inodes; i++) {
        if (fs->inode_map[i] && strcmp(fs->inodes[i].name, filename) == 0) {
            return -1;
        }
    }

    int inode_num = find_free_inode(fs);
    if (inode_num < 0) return -1;

    inode_t* inode = &fs->inodes[inode_num];
    strncpy(inode->name, filename, MAX_FILENAME - 1);
    inode->size = 0;
    inode->num_blocks = 0;
    inode->created = inode->modified = time(NULL);
    inode->is_directory = 0;

    return inode_num;
}

// Write data to a file
int fs_write(filesystem_t* fs, const char* filename, const uint8_t* data, size_t size) {
    if (!fs || !filename || !data || size == 0) return -1;

    // Find the file
    int inode_num = -1;
    for (uint32_t i = 0; i < fs->total_inodes; i++) {
        if (fs->inode_map[i] && strcmp(fs->inodes[i].name, filename) == 0) {
            inode_num = i;
            break;
        }
    }
    if (inode_num < 0) return -1;

    inode_t* inode = &fs->inodes[inode_num];
    
    // Calculate needed blocks
    uint32_t needed_blocks = (size + BLOCK_SIZE - 1) / BLOCK_SIZE;
    if (needed_blocks > MAX_BLOCKS) return -1;

    // Free old blocks if they exist
    for (uint8_t i = 0; i < inode->num_blocks; i++) {
        fs->block_map[inode->blocks[i]] = 0;
        fs->free_blocks++;
    }

    // Allocate new blocks
    for (uint32_t i = 0; i < needed_blocks; i++) {
        int block_num = find_free_block(fs);
        if (block_num < 0) return -1;
        
        inode->blocks[i] = block_num;
        memcpy(fs->blocks[block_num].data, 
               data + (i * BLOCK_SIZE), 
               (i == needed_blocks - 1) ? (size % BLOCK_SIZE) : BLOCK_SIZE);
    }

    inode->size = size;
    inode->num_blocks = needed_blocks;
    inode->modified = time(NULL);

    return size;
}

// Read data from a file
int fs_read(filesystem_t* fs, const char* filename, uint8_t* buffer, size_t size) {
    if (!fs || !filename || !buffer) return -1;

    // Find the file
    int inode_num = -1;
    for (uint32_t i = 0; i < fs->total_inodes; i++) {
        if (fs->inode_map[i] && strcmp(fs->inodes[i].name, filename) == 0) {
            inode_num = i;
            break;
        }
    }
    if (inode_num < 0) return -1;

    inode_t* inode = &fs->inodes[inode_num];
    size_t to_read = (size < inode->size) ? size : inode->size;

    // Read data from blocks
    for (uint8_t i = 0; i < inode->num_blocks; i++) {
        size_t block_read_size = (i == inode->num_blocks - 1) ? 
            (to_read % BLOCK_SIZE) : BLOCK_SIZE;
        memcpy(buffer + (i * BLOCK_SIZE), 
               fs->blocks[inode->blocks[i]].data, 
               block_read_size);
    }

    return to_read;
}

// Delete a file
int fs_delete(filesystem_t* fs, const char* filename) {
    if (!fs || !filename) return -1;

    // Find the file
    int inode_num = -1;
    for (uint32_t i = 0; i < fs->total_inodes; i++) {
        if (fs->inode_map[i] && strcmp(fs->inodes[i].name, filename) == 0) {
            inode_num = i;
            break;
        }
    }
    if (inode_num < 0) return -1;

    inode_t* inode = &fs->inodes[inode_num];

    // Free blocks
    for (uint8_t i = 0; i < inode->num_blocks; i++) {
        fs->block_map[inode->blocks[i]] = 0;
        fs->free_blocks++;
    }

    // Free inode
    fs->inode_map[inode_num] = 0;
    fs->free_inodes++;
    memset(inode, 0, sizeof(inode_t));

    return 0;
}

// List all files
void fs_list_files(filesystem_t* fs) {
    if (!fs) return;

    printf("\nFile Listing:\n");
    printf("%-32s %-10s %-24s %-24s\n", "Name", "Size", "Created", "Modified");
    printf("----------------------------------------------------------------\n");

    for (uint32_t i = 0; i < fs->total_inodes; i++) {
        if (fs->inode_map[i]) {
            inode_t* inode = &fs->inodes[i];
            char created[26], modified[26];
            ctime_r(&inode->created, created);
            ctime_r(&inode->modified, modified);
            created[24] = modified[24] = '\0';  // Remove newline

            printf("%-32s %-10u %-24s %-24s\n",
                   inode->name,
                   inode->size,
                   created,
                   modified);
        }
    }
}
