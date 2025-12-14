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

#define I_BUSY 0x1
#define I_VALID 0x2

char *addr;
struct dinode *inode_table;
struct superblock *sb;
uint *block_usage;
uint *dir_ref_count;
uint data_block_start;
uint bitmap_start;


int
is_valid_data_block(uint block_num)
{
    return block_num >= data_block_start && block_num < sb->size;
}

struct dirent 
*get_dirent_block(uint block_num)
{
    return (struct dirent *)(addr + block_num * BLOCK_SIZE);
}

uint
*get_indirect_block(uint block_num)
{
    return (uint *)(addr + block_num * BLOCK_SIZE);
}

int 
is_bit_set_in_bitmap(uint block_num)
{
		uint bit_index = block_num - data_block_start;
    uint bitmap_block = bitmap_start + (bit_index / BPB);
    uchar *bitmap = (uchar *)(addr + bitmap_block * BLOCK_SIZE);
    uint byte_offset = (bit_index % BPB) / 8;
    uint bit_offset = bit_index % 8;
    return (bitmap[byte_offset] >> bit_offset) & 1;
}

int
find_dirent_in_block(uint block_num, char *name)
{
    struct dirent *de = get_dirent_block(block_num);
    int entries = BLOCK_SIZE / sizeof(struct dirent);
    for (int i = 0; i < entries; i++)
    {
        if (de[i].inum != 0 && strncmp(de[i].name, name, DIRSIZ) == 0)
        {
            return de[i].inum;
        }
    }
    return -1;
}

int
check_root_directory()
{
    struct dinode *root = &inode_table[ROOTINO];

    if (root->type != T_DIR)
    {
        fprintf(stderr, "ERROR: root directory does not exist.\n");
        return 1;
    }

    int dot_inum = -1, ddot_inum = -1;

    for (int i = 0; i < NDIRECT && (dot_inum == -1 && ddot_inum == -1); i++)
    {
        if (root->addrs[i] == 0) continue;
        if (dot_inum == -1) dot_inum = find_dirent_in_block(root->addrs[i], ".");
        if (ddot_inum == -1) ddot_inum = find_dirent_in_block(root->addrs[i], "..");
    }
    if (root-> addrs[NDIRECT] != 0 && (dot_inum == -1 || ddot_inum == -1))
    {
        uint *indirect = get_indirect_block(root->addrs[NDIRECT]);
        for (int i = 0; i < NINDIRECT && (dot_inum == -1 || ddot_inum == -1); i++)
        {
            if (indirect[i] == 0) continue;
            if (dot_inum == -1) dot_inum = find_dirent_in_block(indirect[i], ".");
            if (ddot_inum == -1) ddot_inum = find_dirent_in_block(indirect[i], "..");

        }
    }

    if (dot_inum != ROOTINO || ddot_inum != ROOTINO)
    {
        fprintf(stderr, "ERROR: root directory does not exist.\n");
        return 1;
    }
    return 0;
}

void 
count_directory_references()
{
    for (uint i = 0; i < sb->ninodes; i++)
    {
        struct dinode *dip = &inode_table[i];
        if (dip->type != T_DIR) continue;

        for (int j = 0; j < NDIRECT; j++)
        {
            if (dip->addrs[j] == 0) continue;
            struct dirent *de = get_dirent_block(dip->addrs[j]);
            int entries = BLOCK_SIZE / sizeof(struct dirent);
            for (int k = 0; k < entries; k++)
            {
                if (de[k].inum != 0)
                {
                    dir_ref_count[de[k].inum]++;
                }
            }

        }

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
                        dir_ref_count[de[k].inum]++;
                    }
                }
            }
        }
    }
}



// Check 10

int
check_directory_entry_validity()
{
    for (uint i = 0; i < sb->ninodes; i++)
    {
        struct dinode *dip = &inode_table[i];
        if (dip->type != T_DIR) continue;

        for (int j = 0; j < NDIRECT; j++)
        {
            if (dip->addrs[j] == 0) continue;
            struct dirent *de = get_dirent_block(dip->addrs[j]);
            int entries = BLOCK_SIZE / sizeof(struct dirent);
            for (int k = 0; k < entries; k++)
            {
                if (de[k].inum == 0) continue;
                if (inode_table[de[k].inum].type == T_UNALLOC)
                {
                    fprintf(stderr, "ERROR: inode referred to in directory but marked free.\n");
                    return 1;
                }
            }
        }

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
                    if (inode_table[de[k].inum].type == T_UNALLOC)
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


int
main(int argc, char *argv[])
{
  int fsfd;
  struct stat statb;

  if(argc < 2){
    fprintf(stderr, "Usage: sample fs.img ...\n");
    exit(ERROR_CODE);
  }


  fsfd = open(argv[1], O_RDONLY);
  if(fsfd < 0){
    fprintf(stderr, "image not found.\n");
    exit(ERROR_CODE);
  }

  if (fstat(fsfd, &statb) == -1) {
    perror("fstat");
    exit(ERROR_CODE);
  }

  /* Dont hard code the size of file. Use fstat to get the size */
  addr = mmap(NULL, statb.st_size, PROT_READ, MAP_PRIVATE, fsfd, 0);
  if (addr == MAP_FAILED){
	  perror("mmap failed");
	  exit(1);
  }

  /* read the super block */
  sb = (struct superblock *) (addr + 1 * BLOCK_SIZE);

  /* read the inodes */
  inode_table = (struct dinode *) (addr + IBLOCK((uint)0)*BLOCK_SIZE); 

  bitmap_start = BBLOCK(0, sb->ninodes);
	// changing here lol
  //data_block_start = bitmap_start + (sb->size / BPB) + 1;
  uint num_bitmap_blocks = (sb->nblocks / BPB) + 1;
  data_block_start = bitmap_start + num_bitmap_blocks;
	printf("DEBUG: bitmap_start=%d, data_block_start=%d, num_bitmap_blocks=%d\n", bitmap_start, data_block_start, num_bitmap_blocks);
	


  printf("fs.img size: %jd\n", statb.st_size);
  printf("fs size %d, no. of blocks %d, no. of inodes %d\n", sb->size, sb->nblocks, sb->ninodes);
  printf("begin addr%p, begin inode %p, offset %ld\n", addr, inode_table, (char *)inode_table - addr);
  printf("Root inode size %d links %d type %d\n", inode_table[ROOTINO].size, inode_table[ROOTINO].nlink, inode_table[ROOTINO].type);

  if (inode_table[ROOTINO].type == T_DIR && inode_table[ROOTINO].addrs[0] != 0)
  {
      struct dirent *de = get_dirent_block(inode_table[ROOTINO].addrs[0]);
      int n = inode_table[ROOTINO].size / sizeof(struct dirent);
      for (int i = 0; i < n; i++, de++)
      {
          //if (de->inum == 0) continue;
          printf("  inum %d, name %s", de->inum, de->name);
          printf(" -> inode size %d links %d type %d\n", inode_table[de->inum].size, inode_table[de->inum].nlink, inode_table[de->inum].type);
      }
      printf("\n");
  }



  block_usage = calloc(sb->size, sizeof(uint));
  dir_ref_count = calloc(sb->ninodes, sizeof(uint));
  if (!block_usage || !dir_ref_count)
  {
      fprintf(stderr, "Memory allocation failed\n");
      exit(ERROR_CODE);
  }
  //check 3
  if (check_root_directory()) goto cleanup;
  

  // main loop
  for (uint i = 0; i < sb->ninodes; i++)
  {
      struct dinode *dip = &inode_table[i];

      // Check 1
      if (dip->type != T_UNALLOC && dip->type != T_FILE && dip->type != T_DIR && dip->type != T_DEV)
      {
          fprintf(stderr, "ERROR: bad inode.\n");
          goto cleanup;
      }


      // skip unallocated inodes
      if (dip->type == T_UNALLOC) continue;


      for (int j = 0; j < NDIRECT; j++)
      {
          uint block = dip->addrs[j];
          if (block == 0) continue;

          // Check 2
          if (!is_valid_data_block(block))
          {
              fprintf(stderr, "ERROR: bad direct address in inode.\n");
              goto cleanup;
          }

          // Check 7
          if (block_usage[block] > 0)
          {
              fprintf(stderr, "ERROR: direct address used more than once.\n");
              goto cleanup;
          }
          block_usage[block]++;

          // Check 5
          if (!is_bit_set_in_bitmap(block))
          {
              fprintf(stderr, "ERROR: address used by inode but marked free in bitmap.\n");
              goto cleanup;
          }
      }

      
      uint indirect = dip->addrs[NDIRECT];
      if (indirect != 0)
      {
          // Check 2: bad indirect
          if (!is_valid_data_block(indirect))
          {
              fprintf(stderr, "ERROR: bad indirect address in inode.\n");
              goto cleanup;
          }
          
          block_usage[indirect]++;

          // Check 5
          if (!is_bit_set_in_bitmap(indirect))
          {
              fprintf(stderr, "ERROR: address used by inode but marked free in bitmap.\n");
              goto cleanup;
          }

          // Check blocks pointed to by indirect block
          uint *indirect_addrs = get_indirect_block(indirect);
          for (int j = 0; j < NINDIRECT; j++)
          {
              uint block = indirect_addrs[j];
              if (block == 0) continue;

              // Check 2
              if (!is_valid_data_block(block))
              {
                  fprintf(stderr, "ERROR: bad indirect address in inode.\n");
                  goto cleanup;
              }


              // Check 8: indirect address used more than once
              if (block_usage[block] > 0)
              {
                  fprintf(stderr, "ERROR: indirect address used more than once.\n");
                  goto cleanup;
              }
							block_usage[block]++;

              // Check 5
              if (!is_bit_set_in_bitmap(block))
              {
                  fprintf(stderr, "ERROR: address used by inode but marked free in bitmap.\n");
                  goto cleanup;
              }
          }
      }

      // Check 4
      if (dip->type == T_DIR)
      {
          int dot_inum = -1, ddot_inum = -1;

          for (int j = 0; j < NDIRECT && (dot_inum == -1 || ddot_inum == -1); j++)
          {
              if (dip->addrs[j] == 0) continue;
              if (dot_inum == -1) dot_inum = find_dirent_in_block(dip->addrs[j], ".");
              if (ddot_inum == -1) ddot_inum = find_dirent_in_block(dip->addrs[j], "..");
          }
          if (dip->addrs[NDIRECT] != 0 && (dot_inum == -1 || ddot_inum == -1))
          {
              uint *indirect_addrs = get_indirect_block(dip->addrs[NDIRECT]);
              for (int j = 0; j < NINDIRECT && (dot_inum == -1 || ddot_inum == -1); j++)
              {
                if (indirect_addrs[j] == 0) continue;
                if (dot_inum == -1) dot_inum = find_dirent_in_block(indirect_addrs[j], ".");
                if (ddot_inum == -1) dot_inum = find_dirent_in_block(indirect_addrs[j], "..");
              }
          }
          if (dot_inum != i || ddot_inum == -1)
          {
              fprintf(stderr, "ERROR: directory not properly formatted.\n");
              goto cleanup;
          }
      }

      // Check 12
      if (dip->type == T_DIR && dip->nlink > 1)
      {
          fprintf(stderr, "ERROR: directory appears more than once in file system.\n");
          goto cleanup;
      }
  }


  // Check 6
  
	//printf("DEBUG: Blocks marked as used in block_usage array:\n");
	int count = 0;
	for (uint block = 0; block < sb->size; block++)
	{
		if (block_usage[block] > 0)
		{
			//printf("  block %d, used %d times\n", block, block_usage[block]);
			count++;
		}
	}
	//printf("DEBUG: Total blocks tracked as used: %d\n", count);


	uint last_used_block = 0;
	for (uint block = 0; block < sb->size; block++)
	{
		if (block_usage[block] > 0 && block > last_used_block)
		{
			last_used_block = block;
		}
	}

 
	//printf("DEBUG: Checking bitmap consistency from block %d to %d\n", data_block_start, data_block_start + sb->nblocks);
  for (uint block = data_block_start; block <= last_used_block; block++)
  {
      if (is_bit_set_in_bitmap(block) && block_usage[block] == 0)
      {
					//printf("DEBUG: Block %d marked in bitmap but not used\n", block);
          fprintf(stderr, "ERROR: bitmap marks block in use but it is not in use.\n");
          goto cleanup;
      }
  }


  count_directory_references();
  
  // Check 9
  for (uint i = 0; i < sb->ninodes; i++)
  {
      struct dinode *dip = &inode_table[i];
      if (dip->type != T_UNALLOC && dir_ref_count[i] == 0)
      {
          fprintf(stderr, "ERROR: inode marked use but not found in a directory.\n");
          goto cleanup;
      }
  }

  //Check 10
  if (check_directory_entry_validity()) goto cleanup;

  //Check 11

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
  munmap(addr, statb.st_size);
  close(fsfd);
  return 0;


}

