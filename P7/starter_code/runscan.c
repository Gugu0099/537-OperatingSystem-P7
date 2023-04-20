#include <stdio.h>
#include <dirent.h>
#include <string.h>
#include <sys/types.h>
#include "ext2_fs.h"
#include "read_ext2.h"



int main(int argc, char **argv) {
	if (argc != 3) {
		printf("expected usage: ./runscan inputfile outputfile\n");
		exit(0);
	}
	
	int fd;

	fd = open(argv[1], O_RDONLY);    /* open disk image */


	DIR *dir = opendir(argv[2]);
	if(dir){
		printf("Err: directory already exists.\n"); 
//		exit(1);
	}
	mkdir(argv[2], 0777);

	ext2_read_init(fd);

	struct ext2_super_block super;
	struct ext2_group_desc group;
	
	// example read first the super-block and group-descriptor
	

//	printf("Number of groups in file system: %d\n", num_groups);
	map fileMap[(num_groups * itable_blocks * inodes_per_block) + 1]; //For storing file correspondance.	
	//iterate the first inode block

	for(unsigned int j = 0; j < num_groups; j++){
		read_super_block(fd, j, &super);
		read_group_desc(fd, j, &group);
		off_t start_inode_table = locate_inode_table(j, &group);
		for (unsigned int i = 0; i < super.s_inodes_per_group; i++) {
	//            printf("inode %u: \n", i);
			struct ext2_inode *inode = malloc(sizeof(struct ext2_inode));
			read_inode(fd, 0, start_inode_table, i, inode);
			if(S_ISDIR(inode->i_mode) == 0 && S_ISREG(inode->i_mode) == 0){
				continue;
			}

			if(S_ISDIR(inode->i_mode)){
				char buffer[1024];
				read_dir(fd, inode, buffer, fileMap);
			}

            free(inode);
		}	
	}
	struct ext2_super_block super2;
	struct ext2_group_desc group2;

	for(unsigned int j = 0; j < num_groups; j++){
		read_super_block(fd, j, &super2);
		read_group_desc(fd, j, &group2);
		off_t start_inode_table = locate_inode_table(j, &group2);

		for (unsigned int i = 0; i < super.s_inodes_per_group; i++) {
	//            printf("inode %u: \n", i);
			struct ext2_inode *inode = malloc(sizeof(struct ext2_inode));
			read_inode(fd, 0, start_inode_table, i, inode);
			if(S_ISDIR(inode->i_mode) == 0 && S_ISREG(inode->i_mode) == 0){
				continue;
			}

			if(S_ISREG(inode->i_mode)){
				char buffer[1024];
				read_reg(fd, i, inode, buffer, argv[2], fileMap);
			}

            free(inode);
		}	

	}	
	close(fd);
}