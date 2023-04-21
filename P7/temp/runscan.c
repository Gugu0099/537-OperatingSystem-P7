#include "ext2_fs.h"
#include "read_ext2.h"
#include <dirent.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdint.h>
#include <stdlib.h>

#define INDIRECT_BLOCK_COUNT (block_size / sizeof(uint32_t)) // 256

void read_single_indirect_block(int fd, uint32_t block_number, char *buffer, FILE *output_file, uint32_t block_size, uint32_t *bytes_left) {
 
  uint32_t indirect_block[INDIRECT_BLOCK_COUNT];
  off_t offset = BLOCK_OFFSET(block_number);
  lseek(fd, offset, SEEK_SET);
  read(fd, indirect_block, block_size);
  
  for (uint32_t i = 0; i < INDIRECT_BLOCK_COUNT; i++) {
    if (indirect_block[i] == 0) {
      continue;
    }
    if (*bytes_left == 0) {
      break;
    }
    uint32_t bytes_to_read = block_size;
    if (*bytes_left < block_size) {
      bytes_to_read = *bytes_left;
    }
    offset = BLOCK_OFFSET(indirect_block[i]);
    lseek(fd, offset, SEEK_SET);
    read(fd, buffer, bytes_to_read);    
    fwrite(buffer, 1, bytes_to_read, output_file);
    *bytes_left -= bytes_to_read;
  }
}

void read_double_indirect_block(int fd, uint32_t block_number, char *buffer, FILE *output_file, uint32_t block_size, uint32_t *bytes_left) {
    
  uint32_t indirect_block[INDIRECT_BLOCK_COUNT];
  off_t offset = BLOCK_OFFSET(block_number);
  lseek(fd, offset, SEEK_SET);
  read(fd, indirect_block, block_size);
  for (uint32_t i = 0; i < INDIRECT_BLOCK_COUNT; i++) {
    if (indirect_block[i] == 0) {
      continue;
    }
    if (*bytes_left == 0) {
      break;
    }
    read_single_indirect_block(fd, indirect_block[i], buffer, output_file, block_size, bytes_left);
  }
}

void read_triple_indirect_block(int fd, uint32_t block_number, char *buffer, FILE *output_file, uint32_t block_size, uint32_t *bytes_left) {

  uint32_t indirect_block[INDIRECT_BLOCK_COUNT];
  off_t offset = BLOCK_OFFSET(block_number);
  lseek(fd, offset, SEEK_SET);
  read(fd, indirect_block, block_size);
  for (uint32_t i = 0; i < INDIRECT_BLOCK_COUNT; i++) {
    if (indirect_block[i] == 0) {
      continue;
    }
    if (*bytes_left == 0) {
      break;
    }
    read_double_indirect_block(fd, indirect_block[i], buffer, output_file, block_size, bytes_left);
  }
}


int main(int argc, char **argv) 
{
  if (argc != 3) 
    {
      printf("expected usage: ./runscan inputfile outputfile\n");
      exit(0);
    }

  int fd;
  fd = open(argv[1], O_RDONLY);

  
  // without this code to make a directory, it doesn't open the file
  struct stat st = {0};
  if (stat(argv[2], &st) == -1) {
    if (mkdir(argv[2], 0700) == -1) {
      perror("mkdir");
      exit(1);
    }
  } 
		    

  ext2_read_init(fd);
  struct ext2_super_block super;
  struct ext2_group_desc group;

  // example read first the super-block and group-descriptor
  read_super_block(fd, 0, &super);
  read_group_desc(fd, 0, &group);

  // if not setting this prior, when it goes to the other blocks, it will be overwritten to 0
  uint32_t i_p_g = super.s_inodes_per_group;
  uint32_t size_inode_struct = super.s_inode_size;
		    
  
  for (uint32_t j = 0; j < num_groups; ++j)
    {
      printf("-------IN BLOCK %u--------\n", j);
	       
      read_super_block(fd, j, &super);
      read_group_desc(fd, j, &group);
      off_t inode_table_offset = locate_inode_table(j, &group);
      // char buffer[block_size];
	
      // to be able to track which inode we are at
      uint32_t starting_inode_number = j * i_p_g;
	
      for (uint32_t i = 1; i < i_p_g; i++)
	{	    
	  
	  char buffer[block_size];
	  struct ext2_inode inode;
	   
	  read_inode(fd, inode_table_offset, i, &inode, size_inode_struct);

	  uint32_t mode = inode.i_mode;

	  if (S_ISREG(mode)) {
	    printf("-------Current iteration: %u, Inode mode: %u----------\n", i, inode.i_mode);
	  }

	  // to be able to track which inode we are at
	  uint32_t current_inode_number = starting_inode_number + i;
	  
	  // if (S_ISDIR(inode->i_mode)) {}
	  if (S_ISREG(mode))
	    {
	      printf("-------Processing inode %u as a regular file-------\n", i);

	      off_t offset = BLOCK_OFFSET(inode.i_block[0]);
	      lseek(fd, offset, SEEK_SET);
	      read(fd, buffer, block_size);
  
			
	      if (buffer[0] == (char)0xff &&
		  buffer[1] == (char)0xd8 &&
		  buffer[2] == (char)0xff &&
		  (buffer[3] == (char)0xe0 ||
		   buffer[3] == (char)0xe1 ||
		   buffer[3] == (char)0xe8)) 
		{

		    printf("---------------------------\nprocessing inode %u as a jpeg file\n------------------------------------------------\n", i);
		    
		    char output_path[256];
		    snprintf(output_path, sizeof(output_path), "%s/file-%u.jpg", argv[2], current_inode_number);
		    
		    FILE *output_file = fopen(output_path, "wb"); // ab or wb?

		    if (!output_file)
		      {
			perror("fopen");
			exit(1);
		      }
		  
		    uint32_t bytes_left = inode.i_size; 
		    uint32_t bytes_to_read = block_size; 
		    
		    // reading the direct blocks
		    for (uint32_t block_idx = 0; block_idx < EXT2_NDIR_BLOCKS; block_idx++) {
		      if (inode.i_block[block_idx] == 0) {
			continue;
		      }

		      if (bytes_left == 0) {
			break;
		      }
	 
		      if (bytes_left < block_size) {
			bytes_to_read = bytes_left;
		      }
		    
		      offset = BLOCK_OFFSET(inode.i_block[block_idx]);
		      lseek(fd, offset, SEEK_SET);
		      read(fd, buffer, bytes_to_read);
		      fwrite(buffer, 1, bytes_to_read, output_file);
		      bytes_left = bytes_left - bytes_to_read;
		    }
		    
		    // reading the indirect blocks
		    if (inode.i_block[EXT2_IND_BLOCK] != 0 && bytes_left > 0) {
		      read_single_indirect_block(fd, inode.i_block[12], buffer, output_file, block_size, &bytes_left);
		    }

		    if (inode.i_block[EXT2_DIND_BLOCK] != 0 && bytes_left > 0) {
		      read_double_indirect_block(fd, inode.i_block[13], buffer, output_file, block_size, &bytes_left);
		    }

		    if (inode.i_block[EXT2_TIND_BLOCK] != 0 && bytes_left > 0) {
		      read_triple_indirect_block(fd, inode.i_block[14], buffer, output_file, block_size, &bytes_left);
		    }

		    fclose(output_file);
		  
		}
	    }
	  }
    }
    return 0;
}
