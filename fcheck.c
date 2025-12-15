#include <unistd.h>
#include <fcntl.h>
#include <stdio.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/mman.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <stdbool.h>

#include "xv6/include/types.h"
#include "xv6/include/fs.h"

#define BLOCK_SIZE (BSIZE)
#define ERROR_CODE 1
#define ROOTINO 1
#define T_UNALLOC 0
#define T_DIR 1
#define T_FILE 2
#define T_DEV 3

char *addr;                    // Base address of the mapped filesystem
struct dinode *inode_table;    // Pointer to the inode table
struct superblock *sb;         // Pointer to the superblock

// Tracking arrays for validation
uint *block_usage;             // Counts how many times each block is used
uint *dir_ref_count;           // Counts directory references to each inode
uint *parent_count;            // Counts how many parent directories reference each directory

// Filesystem layout information
uint data_block_start;         // First block number where data blocks begin
uint bitmap_start;             // First block number where the bitmap begins

int is_valid_data_block(uint block_num)
{
    // Block must be within bounds and in data region
    return (block_num >= (uint)0 && block_num < sb->size && block_num >= data_block_start);
}

// Get a pointer to directory entries in a given block
struct dirent *get_dirent_block(uint block_num)
{
    return (struct dirent *)(addr + block_num * BLOCK_SIZE);
}

// Get a pointer to an indirect block 
uint *get_indirect_block(uint block_num)
{
    return (uint *)(addr + block_num * BLOCK_SIZE);
}

// Check if a block is marked as in-use in the bitmap
int is_bit_set_in_bitmap(uint block_num)
{
    if (block_num >= sb->size) return 0;
    
    // Find which block of the bitmap contains this bit
    uint bitmap_block = BBLOCK(block_num, sb->ninodes);
    uchar *bitmap = (uchar *)(addr + (bitmap_block * BLOCK_SIZE));
    
    // Find the specific bit within that block
    uint bit_index = block_num % BPB;           // Which bit in the bitmap
    uint byte_offset = bit_index / 8;            // Which byte in the block
    uint bit_offset = bit_index % 8;             // Which bit in the byte
    
    return (bitmap[byte_offset] >> bit_offset) & 1;
}

// Search for a directory entry by name within a block
// Returns the inode number if found, -1 otherwise
int find_dirent_in_block(uint block_num, char *name)
{
    struct dirent *de = get_dirent_block(block_num);
    int entries = BLOCK_SIZE / sizeof(struct dirent);
    
    for (int i = 0; i < entries; i++)
    {
        // Skip empty entries and check if names match
        if (de[i].inum != 0 && strncmp(de[i].name, name, DIRSIZ) == 0)
        {
            return de[i].inum;
        }
    }
    return -1;
}

// Verify that the root directory exists and is properly formatted
int check_root_directory()
{
    struct dinode *root = &inode_table[ROOTINO];
    
    if (root->type != T_DIR)
    {
        fprintf(stderr, "ERROR: root directory does not exist.\n");
        return 1;
    }
    
    int dot_inum = -1, ddot_inum = -1;
    
    // Search for . and .. entries in direct blocks, keep searching until both are found
    for (int i = 0; i < NDIRECT && (dot_inum == -1 || ddot_inum == -1); i++)
    {
        if (root->addrs[i] == 0) continue;
        if (dot_inum == -1) dot_inum = find_dirent_in_block(root->addrs[i], ".");
        if (ddot_inum == -1) ddot_inum = find_dirent_in_block(root->addrs[i], "..");
    }
    
    // Check indirect block if both arent found yet
    if (root->addrs[NDIRECT] != 0 && (dot_inum == -1 || ddot_inum == -1))
    {
        uint *indirect = get_indirect_block(root->addrs[NDIRECT]);
        for (int i = 0; i < NINDIRECT && (dot_inum == -1 || ddot_inum == -1); i++)
        {
            if (indirect[i] == 0) continue;
            if (dot_inum == -1) dot_inum = find_dirent_in_block(indirect[i], ".");
            if (ddot_inum == -1) ddot_inum = find_dirent_in_block(indirect[i], "..");
        }
    }
    
    // For root, both . and .. should point to root itself
    if (dot_inum != ROOTINO || ddot_inum != ROOTINO)
    {
        fprintf(stderr, "ERROR: root directory does not exist.\n");
        return 1;
    }
    
    return 0;
}

// Count how many times each inode is referenced in directories
// Also tracks parent counts for directories, not including . and ..
void count_directory_references()
{
    // Scan through all directory inodes
    for (uint i = 0; i < sb->ninodes; i++)
    {
        struct dinode *dip = &inode_table[i];
        if (dip->type != T_DIR) continue;
        
        // Check all direct blocks
        for (int j = 0; j < NDIRECT; j++)
        {
            if (dip->addrs[j] == 0) continue;
            
            struct dirent *de = get_dirent_block(dip->addrs[j]);
            int entries = BLOCK_SIZE / sizeof(struct dirent);
            
            for (int k = 0; k < entries; k++)
            {
                if (de[k].inum != 0)
                {
                    uint inum = de[k].inum;
                    
                    // Count all directory references
                    if (inum < sb->ninodes) dir_ref_count[inum]++;
                    
                    // For directories, also track parent relationships, skip . and .. for self reference
                    if (inum < sb->ninodes && inode_table[inum].type == T_DIR)
                    {
                        if (strncmp(de[k].name, ".", DIRSIZ) != 0 && 
                            strncmp(de[k].name, "..", DIRSIZ) != 0)
                        {
                            parent_count[inum]++;
                        }
                    }
                }
            }
        }
        
        // Check indirect blocks
        if (dip->addrs[NDIRECT] != 0)
        {
            uint *indirect = get_indirect_block(dip->addrs[NDIRECT]);
            for (int j = 0; j < NINDIRECT; j++)
            {
                if (indirect[j] == 0) continue;
                
                struct dirent *de = get_dirent_block(indirect[j]);
                int entries = BLOCK_SIZE / sizeof(struct dirent);
                
                for (int k = 0; k < entries; k++)
                {
                    if (de[k].inum != 0)
                    {
                        uint inum = de[k].inum;
                        if (inum < sb->ninodes) dir_ref_count[inum]++;
                        
                        if (inum < sb->ninodes && inode_table[inum].type == T_DIR)
                        {
                            if (strncmp(de[k].name, ".", DIRSIZ) != 0 && 
                                strncmp(de[k].name, "..", DIRSIZ) != 0)
                            {
                                parent_count[inum]++;
                            }
                        }
                    }
                }
            }
        }
    }
}

// Verify that all directory entries point to valid, in-use inodes
int check_directory_entry_validity()
{
    for (uint i = 0; i < sb->ninodes; i++)
    {
        struct dinode *dip = &inode_table[i];
        if (dip->type != T_DIR) continue;
        
        // Check direct blocks
        for (int j = 0; j < NDIRECT; j++)
        {
            if (dip->addrs[j] == 0) continue;
            
            struct dirent *de = get_dirent_block(dip->addrs[j]);
            int entries = BLOCK_SIZE / sizeof(struct dirent);
            
            for (int k = 0; k < entries; k++)
            {
                if (de[k].inum == 0) continue;
                
                // Inode number must be valid and the inode must be allocated
                if (de[k].inum >= sb->ninodes || inode_table[de[k].inum].type == T_UNALLOC)
                {
                    fprintf(stderr, "ERROR: inode referred to in directory but marked free.\n");
                    return 1;
                }
            }
        }
        
        // Check indirect blocks
        if (dip->addrs[NDIRECT] != 0)
        {
            uint *indirect = get_indirect_block(dip->addrs[NDIRECT]);
            for (int j = 0; j < NINDIRECT; j++)
            {
                if (indirect[j] == 0) continue;
                
                struct dirent *de = get_dirent_block(indirect[j]);
                int entries = BLOCK_SIZE / sizeof(struct dirent);
                
                for (int k = 0; k < entries; k++)
                {
                    if (de[k].inum == 0) continue;
                    
                    if (de[k].inum >= sb->ninodes || inode_table[de[k].inum].type == T_UNALLOC)
                    {
                        fprintf(stderr, "ERROR: inode referred to in directory but marked free.\n");
                        return 1;
                    }
                }
            }
        }
    }
    return 0;
}

int main(int argc, char *argv[])
{
    int fsfd;
    struct stat statb;
    
    if (argc < 2)
    {
        fprintf(stderr, "Usage: sample fs.img ...\n");
        exit(ERROR_CODE);
    }
    
    fsfd = open(argv[1], O_RDONLY);
    if (fsfd < 0)
    {
        fprintf(stderr, "image not found.\n");
        exit(ERROR_CODE);
    }
    
    if (fstat(fsfd, &statb) == -1)
    {
        perror("fstat");
        exit(ERROR_CODE);
    }
    
    addr = mmap(NULL, statb.st_size, PROT_READ, MAP_PRIVATE, fsfd, 0);
    if (addr == MAP_FAILED)
    {
        perror("mmap failed");
        exit(1);
    }
    
    sb = (struct superblock *)(addr + 1 * BLOCK_SIZE);
    
    // Get pointer to the inode table (starts at block 2)
    inode_table = (struct dinode *)(addr + IBLOCK((uint)0) * BLOCK_SIZE);
    
    // Calculate where the bitmap and data blocks start
    bitmap_start = BBLOCK(0, sb->ninodes);
    uint num_bitmap_blocks = (sb->nblocks + BPB - 1) / BPB;  
    data_block_start = bitmap_start + num_bitmap_blocks;
    
    // Allocate tracking arrays
    block_usage = calloc(sb->size, sizeof(uint));
    dir_ref_count = calloc(sb->ninodes, sizeof(uint));
    parent_count = calloc(sb->ninodes, sizeof(uint));
    
    if (!block_usage || !dir_ref_count || !parent_count)
    {
        fprintf(stderr, "Memory allocation failed\n");
        exit(ERROR_CODE);
    }
    
    // Check 3: Verify root directory exists and is properly set up
    if (check_root_directory()) goto cleanup;
    
    // Main loop: scan through all inodes and check for consistency
    for (uint i = 0; i < sb->ninodes; i++)
    {
        struct dinode *dip = &inode_table[i];
        
        // Check 1: Inode must have a valid type
        if (dip->type != T_UNALLOC && dip->type != T_FILE && 
            dip->type != T_DIR && dip->type != T_DEV)
        {
            fprintf(stderr, "ERROR: bad inode.\n");
            goto cleanup;
        }
        
        // Skip unallocated inodes for the remaining checks
        if (dip->type == T_UNALLOC) continue;
        
        // Check all direct block addresses
        for (int j = 0; j < NDIRECT; j++)
        {
            uint block = dip->addrs[j];
            if (block == 0) continue;  // Skip unused entries
            
            // Check 2: Block address must be valid
            if (!is_valid_data_block(block))
            {
                fprintf(stderr, "ERROR: bad direct address in inode.\n");
                goto cleanup;
            }
            
            // Check 7: Each block should only be used once
            if (block_usage[block] > 0)
            {
                fprintf(stderr, "ERROR: direct address used more than once.\n");
                goto cleanup;
            }
            block_usage[block]++;
            
            // Check 5: Block must be marked as in-use in the bitmap
            if (!is_bit_set_in_bitmap(block))
            {
                fprintf(stderr, "ERROR: address used by inode but marked free in bitmap.\n");
                goto cleanup;
            }
        }
        
        // Check the indirect block 
        uint indirect = dip->addrs[NDIRECT];
        if (indirect != 0)
        {
            // Check 2: Indirect block address must be valid
            if (!is_valid_data_block(indirect))
            {
                fprintf(stderr, "ERROR: bad indirect address in inode.\n");
                goto cleanup;
            }
            
            // Check 5: Indirect block must be marked in bitmap
            if (!is_bit_set_in_bitmap(indirect))
            {
                fprintf(stderr, "ERROR: address used by inode but marked free in bitmap.\n");
                goto cleanup;
            }
            
            // Check 8: Indirect block itself shouldn't be shared
            if (block_usage[indirect] > 0)
            {
                fprintf(stderr, "ERROR: indirect address used more than once.\n");
                goto cleanup;
            }
            block_usage[indirect]++;
            
            // Check all the blocks pointed to by the indirect block
            uint *indirect_addrs = get_indirect_block(indirect);
            for (int j = 0; j < NINDIRECT; j++)
            {
                uint block = indirect_addrs[j];
                if (block == 0) continue;
                
                // Check 2: Each indirect address must be valid
                if (!is_valid_data_block(block))
                {
                    fprintf(stderr, "ERROR: bad indirect address in inode.\n");
                    goto cleanup;
                }
                
                // Check 8: Each indirect address should only be used once
                if (block_usage[block] > 0)
                {
                    fprintf(stderr, "ERROR: indirect address used more than once.\n");
                    goto cleanup;
                }
                block_usage[block]++;
                
                // Check 5: Must be marked in bitmap
                if (!is_bit_set_in_bitmap(block))
                {
                    fprintf(stderr, "ERROR: address used by inode but marked free in bitmap.\n");
                    goto cleanup;
                }
            }
        }
        
        // Check 4: Verify directory formatting
        if (dip->type == T_DIR)
        {
            int dot_inum = -1, ddot_inum = -1;
            
            // Look for . and .. in direct blocks
            for (int j = 0; j < NDIRECT && (dot_inum == -1 || ddot_inum == -1); j++)
            {
                if (dip->addrs[j] == 0) continue;
                if (dot_inum == -1) dot_inum = find_dirent_in_block(dip->addrs[j], ".");
                if (ddot_inum == -1) ddot_inum = find_dirent_in_block(dip->addrs[j], "..");
            }
            
            // Check indirect blocks if both arent found yet
            if (dip->addrs[NDIRECT] != 0 && (dot_inum == -1 || ddot_inum == -1))
            {
                uint *indirect_addrs = get_indirect_block(dip->addrs[NDIRECT]);
                for (int j = 0; j < NINDIRECT && (dot_inum == -1 || ddot_inum == -1); j++)
                {
                    if (indirect_addrs[j] == 0) continue;
                    if (dot_inum == -1) dot_inum = find_dirent_in_block(indirect_addrs[j], ".");
                    if (ddot_inum == -1) ddot_inum = find_dirent_in_block(indirect_addrs[j], "..");
                }
            }
            
            // . must point to this directory, .. must exist and point to a valid directory
            if (dot_inum != (int)i || ddot_inum == -1 || 
                ddot_inum >= sb->ninodes || inode_table[ddot_inum].type != T_DIR)
            {
                fprintf(stderr, "ERROR: directory not properly formatted.\n");
                goto cleanup;
            }
        }
    }
    
    // Check 6: Verify bitmap consistency
    // Any block marked in-use in the bitmap should actually be used by some inode
    uint first_data = data_block_start;
    uint last_data = data_block_start + sb->nblocks;
    
    for (uint block = first_data; block < last_data; block++)
    {
        if (is_bit_set_in_bitmap(block) && block_usage[block] == 0)
        {
            fprintf(stderr, "ERROR: bitmap marks block in use but it is not in use.\n");
            goto cleanup;
        }
    }
    
    // Count all directory references, needed for checks 9, 11, and 12
    count_directory_references();
    
    // Check 12: Directories should only appear in one parent directory
    for (uint i = 0; i < sb->ninodes; i++)
    {
        if (inode_table[i].type == T_DIR)
        {
            // Root is its own parent so skip it
            if (i == ROOTINO) continue;
            
            // Each non-root directory should appear in exactly one parent
            if (parent_count[i] > 1)
            {
                fprintf(stderr, "ERROR: directory appears more than once in file system.\n");
                goto cleanup;
            }
        }
    }
    
    // Check 9: Every in-use inode must be referenced somewhere
    for (uint i = 0; i < sb->ninodes; i++)
    {
        struct dinode *dip = &inode_table[i];
        if (dip->type != T_UNALLOC && dir_ref_count[i] == 0)
        {
            fprintf(stderr, "ERROR: inode marked use but not found in a directory.\n");
            goto cleanup;
        }
    }
    
    // Check 10: All directory entries must point to allocated inodes
    if (check_directory_entry_validity()) goto cleanup;
    
    // Check 11: File reference counts must match actual directory links
    for (uint i = 0; i < sb->ninodes; i++)
    {
        struct dinode *dip = &inode_table[i];
        if (dip->type == T_FILE && dip->nlink != dir_ref_count[i])
        {
            fprintf(stderr, "ERROR: bad reference count for file.\n");
            goto cleanup;
        }
    }
    
cleanup:
    free(block_usage);
    free(dir_ref_count);
    free(parent_count);
    munmap(addr, statb.st_size);
    close(fsfd);
    return 0;
}
