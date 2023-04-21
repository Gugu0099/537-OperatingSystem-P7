#include "ext2_fs.h"
#include "read_ext2.h"
#include <dirent.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>

#define INDIRECT_BLOCK_COUNT (block_size / sizeof(uint32_t)) // 256


void read_data_from_indirect_block(int fd, uint32_t block_number, char *buffer, FILE *output_file, uint32_t block_size, uint32_t *bytes_left, int indirection_level) {
    if (indirection_level == 0) {
        uint32_t bytes_to_read = block_size;
        if (*bytes_left < block_size) {
            bytes_to_read = *bytes_left;
        }
        off_t offset = BLOCK_OFFSET(block_number);
        lseek(fd, offset, SEEK_SET);
        read(fd, buffer, bytes_to_read);
        fwrite(buffer, 1, bytes_to_read, output_file);
        *bytes_left -= bytes_to_read;
        return;
    }

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
        read_data_from_indirect_block(fd, indirect_block[i], buffer, output_file, block_size, bytes_left, indirection_level - 1);
    }
}

int main(int argc, char **argv)
{
	if (argc != 3)
	{
		printf("expected usage: ./runscan inputfile outputfile\n");
		exit(0);
	}

	/* This is some boilerplate code to help you get started, feel free to modify
	   as needed! */

	int fd;
	fd = open(argv[1], O_RDONLY); /* open disk image */

	ext2_read_init(fd);

	struct ext2_super_block super;
	struct ext2_group_desc group;

	uint32_t inodes_per_group = super.s_inodes_per_group;
	// example read first the super-block and group-descriptor
	// read_super_block(fd, 0, &super);
	// read_group_desc(fd, 0, &group);

	for (uint32_t i = 0; i < num_groups; i++)
	{
		printf("IN THE BLOCK\n");

		read_super_block(fd, i, &super);
		read_group_desc(fd, i, &group);
		off_t inode_table_offset = locate_inode_table(i, &group);

		uint32_t starting_inode_number = i * inodes_per_group;

		// for every inode per group
		for (uint32_t j = 1; j < inodes_per_group; j++)
		{
			// read in the inode
			struct ext2_inode *inode =malloc(sizeof(struct ext2_inode));

			char buffer[block_size];
			read_inode(fd, inode_table_offset, i ,inode,  super.s_inode_size);

			// check if file or directory
			u_int32_t isFile = inode->i_mode;
			uint32_t curr_inode_idx = starting_inode_number + i;
			// check if regular file
			if (S_ISREG(isFile))
			{
				printf("INODE is processed as regular file\n");

				// seek and read the file
				off_t offset = BLOCK_OFFSET(inode->i_block[0]);
				lseek(fd, offset, SEEK_SET);
				read(fd, buffer, block_size);

				// check if jpg
				if (buffer[0] == (char)0xff &&
					buffer[1] == (char)0xd8 &&
					buffer[2] == (char)0xff &&
					(buffer[3] == (char)0xe0 ||
					 buffer[3] == (char)0xe1 ||
					 buffer[3] == (char)0xe8))
				{
					printf("INODE is processed as jpg file\n");
					char temp_buffer[256];
					snprintf(temp_buffer, sizeof(temp_buffer), "%s/file-%u.jpg", argv[2], curr_inode_idx);

					FILE *output_file = fopen(temp_buffer, "w"); // ab or wb?

					if (!output_file)
					{
						perror("fopen");
						exit(1);
					}

					uint32_t temp_inode_size = inode->i_size;
					uint32_t temp_block_size = block_size;
					for (uint32_t i = 0; i < EXT2_NDIR_BLOCKS; i++)
					{
						if (inode->i_block[i] == 0)
						{
							continue;
						}

						if (temp_inode_size == 0)
						{
							break;
						}

						if (temp_inode_size < temp_block_size)
						{
							temp_block_size = temp_inode_size;
						}

						lseek(fd, BLOCK_OFFSET(inode->i_block[i]), SEEK_SET);
						read(fd, buffer, temp_block_size);
						fwrite(buffer, 1, temp_block_size, output_file);
						temp_inode_size = temp_inode_size - temp_block_size;
					}

					// parsing through different levels
					for (int i = 0; i < EXT2_N_BLOCKS; i++)
					{
						if (inode->i_block[i] != 0 && temp_inode_size > 0)
						{
							read_data_from_indirect_block(fd, inode->i_block[i], buffer, output_file, block_size, &temp_inode_size, i >= EXT2_IND_BLOCK ? i - EXT2_IND_BLOCK + 1 : 0);
						}
					}
					fclose(output_file);
				}
			}
		}
	}

	return 0;
}