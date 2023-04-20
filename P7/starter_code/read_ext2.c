#include <stdio.h>
#include <string.h>
#include "read_ext2.h"

/* implementations credit to
 * Smith College
 * http://www.science.smith.edu/~nhowe/Teaching/csc262/oldlabs/ext2.html
 */

unsigned int block_size = 1024;         /* default 1kB block size */
unsigned int inodes_per_block = 0;                      /* number of inodes per block */
unsigned int itable_blocks = 0;                         /* size in blocks of the inode table */
unsigned int blocks_per_group = 0;
unsigned int num_groups = 0;
unsigned int inodes_per_group = 0;


int debug = 0;          //turn on/off debug prints

//void read_dir(int fd, struct ext2_inode *inode, struct ext2_dir_entry_2 *dir)
void read_dir(int fd, struct ext2_inode *inode, char * buffer, map* inodemap)
{
		//printf("%ld\n", sizeof(inodemap));		
		lseek(fd, BLOCK_OFFSET(inode->i_block[0]), SEEK_SET);
		read(fd, buffer, 1024);

		uint offset = 0;
		while(offset < inode->i_size) {
			struct ext2_dir_entry_2 * dentry;
			dentry = (struct ext2_dir_entry_2*) & ( buffer[offset] );

			if (dentry->rec_len == 0 || dentry->name_len == 0){
				break;
			}

			int name_len = dentry->name_len & 0xFF; // convert 2 bytes to 4 bytes properly

			char name [EXT2_NAME_LEN];
			strncpy(name, dentry->name, name_len);
			name[name_len] = '\0';

			strcpy(inodemap[dentry->inode].name, name);
			//printf("%s\n", inodemap[dentry->inode].name);
			
			offset += 8 + dentry->name_len;
			offset = ((offset + 4 - 1) / 4) * 4;

		}

}



void read_reg(int fd, int id, struct ext2_inode *inode, char *buffer, char *path, map* inodemap)
{
//	printf("Path: %s\n", path);
	
	lseek(fd, BLOCK_OFFSET(inode->i_block[0]), SEEK_SET);
	read(fd, buffer, 1024);
//	printf("Parsing inode: %d\n", id); 

	int is_jpg = 0;
	if (buffer[0] == (char)0xff &&
		buffer[1] == (char)0xd8 &&
		buffer[2] == (char)0xff &&
		(buffer[3] == (char)0xe0 ||
		buffer[3] == (char)0xe1 ||
		buffer[3] == (char)0xe8)) {
		is_jpg = 1;
	}

	if (is_jpg == 0) {
		return;
	}
	int blocks_left = inode->i_size / 1024;
	int bytesLeft = inode->i_size;
	int bytesToWrite = 1024;

	//printf("Bytes to read: %d\n", bytesLeft);
	//make sure string length isnt a source of error.
	char buff[512];
	char actual[512];
	sprintf(buff, "%s/file-%d.jpg", path, id);
	sprintf(actual, "%s/%s", path, inodemap[id].name);

	int filedesc = open(buff, O_CREAT | O_WRONLY,  0666);
	int otherFile = open(actual, O_CREAT | O_WRONLY,  0666);

	if (bytesLeft < bytesToWrite){
		bytesToWrite = bytesLeft;
	}

	write(filedesc, buffer, bytesToWrite);
	write(otherFile, buffer, bytesToWrite);
	blocks_left -= 1;
	bytesLeft -= bytesToWrite;
	if (blocks_left <= 0 && bytesToWrite <= 0) {
		close(filedesc);
		close(otherFile);
		return;
	}

	// direct
	for (int i = 1; i < 12; i++){
		lseek(fd, BLOCK_OFFSET(inode->i_block[i]), SEEK_SET);
		if (bytesLeft < bytesToWrite){
			bytesToWrite = bytesLeft;
		}
		read(fd, buffer, bytesToWrite);
		write(filedesc, buffer, bytesToWrite);
		write(otherFile, buffer, bytesToWrite);
		blocks_left -= 1;
		bytesLeft -= bytesToWrite;
		if (blocks_left <= 0 && bytesToWrite <= 0) {
            close(filedesc);
            close(otherFile);
            return;
        }
	}


	// single
	lseek(fd, BLOCK_OFFSET(inode->i_block[12]), SEEK_SET);
	uint temp[256];
	read(fd, temp, 1024);
	for (int i = 0; i < 256; i++){
		lseek(fd, BLOCK_OFFSET(temp[i]), SEEK_SET);
		if (bytesLeft < bytesToWrite){
			bytesToWrite = bytesLeft;
		}
		read(fd, buffer, bytesToWrite);
		write(filedesc, buffer, bytesToWrite);
		write(otherFile, buffer, bytesToWrite);
		blocks_left -= 1;
		bytesLeft -= bytesToWrite;
        if (blocks_left <= 0 && bytesToWrite <= 0) {
            close(filedesc);
            close(otherFile);
            return;
        }
	}

	// double
	lseek(fd, BLOCK_OFFSET(inode->i_block[13]), SEEK_SET);
	uint temp2[256];
	read(fd, temp2, 1024);
	for (int i = 0; i < 256; i++){
		lseek(fd, BLOCK_OFFSET(temp2[i]), SEEK_SET);
		uint temp3[256];
		read(fd, temp3, 1024);
		for (int i = 0; i < 256; i++){
			lseek(fd, BLOCK_OFFSET(temp3[i]), SEEK_SET);
			if (bytesLeft < bytesToWrite){
				bytesToWrite = bytesLeft;
			}
			read(fd, buffer, bytesToWrite);
			write(filedesc, buffer, bytesToWrite);
			write(otherFile, buffer, bytesToWrite);
			blocks_left -= 1;
			bytesLeft -= bytesToWrite;
			if (blocks_left <= 0 && bytesToWrite <= 0) {
				close(filedesc);
				close(otherFile);
				return;
			}
		}
	}

	close(filedesc);
	close(otherFile);
	return;
}


/* read the first super block to initialize common variables */
void ext2_read_init( int                      fd)
{
	struct ext2_super_block super;
	lseek(fd, BASE_OFFSET, SEEK_SET);        /* position head above super-block */
        read(fd, &super, sizeof(struct ext2_super_block));              /* read super-block */

        if (super.s_magic != EXT2_SUPER_MAGIC) {
                fprintf(stderr, "read_super_block: Not a Ext2 filesystem\n");
                exit(1);
        }

        block_size = 1024 << super.s_log_block_size;
		inodes_per_block = block_size / sizeof(struct ext2_inode);		/* number of inodes per block */
		itable_blocks = super.s_inodes_per_group / inodes_per_block;		/* size in blocks of the inode table */
		blocks_per_group = super.s_blocks_per_group;
		num_groups = (super.s_blocks_count + blocks_per_group - 1) / blocks_per_group;
		inodes_per_group = super.s_inodes_per_group;

		
		if (debug)
		{
			printf("Reading first super-block from device: \n"
				   "Block size                    : %u\n"
				   "number of inodes in a block   : %u\n"
				   "Inode table size in blocks    : %u\n"
				   "Blocks per group              : %u\n"
				   "number of block groups        : %u\n"
				   ,
				   block_size,
				   inodes_per_block,
				   itable_blocks,
				   blocks_per_group,
				   num_groups);
		}
		
		
}

int isPowerOf(int m, int n) 
{
    while (m != 1) 
	{
		if (m % n != 0)
			return 0;
        else
			m /= n;
    }

    return 1;
}

//how many powers of n are below m?
int powersBelow(int m, int n) 
{
	int cnt = 0;
    while (m != 1) 
	{
		if (m % n != 0)
			return cnt;
        m /= n;
		cnt++;
    }

    return cnt;
}

/* read the first super block; for this project, you will only deal with the first block group */
int read_super_block( int                      fd,        /* the disk image file descriptor */
					   int                      ngroup,        /* which block group to access */
					   struct ext2_super_block *super      /* where to put the super block */
						)
{
	// The groups that have copies of a super block are 0, 1 and powers of 3, 5 and 7
    if (ngroup != 0 &&
		ngroup != 1 &&
		!isPowerOf(ngroup, 3) &&
		!isPowerOf(ngroup, 5) &&
		!isPowerOf(ngroup, 7) 
		)
		{
			if (debug)
				printf("this block does not contain a super block copy");
			return -1;
		}

		/*int num_no_super_copy_blocks = 0;
		num_no_super_copy_blocks+=(ngroup > 0 ? 1 : 0);
		num_no_super_copy_blocks+=(ngroup > 1 ? 1 : 0);
		num_no_super_copy_blocks+=powersBelow(ngroup,3);
		num_no_super_copy_blocks+=powersBelow(ngroup,5);
		num_no_super_copy_blocks+=powersBelow(ngroup,7);*/
    
	
	    lseek(fd, BASE_OFFSET + BLOCK_OFFSET(blocks_per_group * ngroup), SEEK_SET);        /* position head above super-block */
        read(fd, super, sizeof(struct ext2_super_block));              /* read super-block */

        if (super->s_magic != EXT2_SUPER_MAGIC) {
                fprintf(stderr, "read_super_block: Not a Ext2 filesystem\n");
                exit(1);
        }

        block_size = 1024 << super->s_log_block_size;
		
		if (debug)
		{
			printf("Reading super-block from device: \n"
				   "Inodes count            : %u\n"
				   "Blocks count            : %u\n"
				   "First data block        : %u\n"
				   "Block size              : %u\n"
				   "log2(Block size)        : %u\n"
				   "Blocks per group        : %u\n"
				   "Inodes per group        : %u\n"
				   "First non-reserved inode: %u\n"
				   "Size of inode structure : %hu\n"
				   ,
				   super->s_inodes_count,
				   super->s_blocks_count,
				   super->s_first_data_block,
				   block_size,
				   super->s_log_block_size,
				   super->s_blocks_per_group,
				   super->s_inodes_per_group,
				   super->s_first_ino,          /* first non-reserved inode */
				   super->s_inode_size);
		}
		
		
		inodes_per_block = block_size / sizeof(struct ext2_inode);		/* number of inodes per block */
		itable_blocks = super->s_inodes_per_group / inodes_per_block;		/* size in blocks of the inode table */
		
		return 0;
}

/* Read the first group-descriptor in the first block group; you will not be tested with a disk image with more than one block group */
void read_group_desc( int                      fd,        /* the disk image file descriptor */
					  int                      ngroup,        /* which block group to access */
		      struct ext2_group_desc *group     /* where to put the group-descriptor */
					)
{
		if (ngroup != 0 &&
		ngroup != 1 &&
		isPowerOf(ngroup, 3) &&
		isPowerOf(ngroup, 5) &&
		isPowerOf(ngroup, 7) 
		)
		{
			lseek(fd, BASE_OFFSET + BLOCK_OFFSET(blocks_per_group * ngroup), SEEK_SET);
		}
		else
		{
			lseek(fd, BASE_OFFSET + BLOCK_OFFSET(blocks_per_group * ngroup) + block_size, SEEK_SET);
		}
        read(fd, group, sizeof(struct ext2_group_desc));

		if (debug)
		{
			printf("Reading group-descriptor from device:\n"
				   "Blocks bitmap block: %u\n"
				   "Inodes bitmap block: %u\n"
				   "Inodes table block : %u\n"
				   "Free blocks count  : %u\n"
				   "Free inodes count  : %u\n"
				   "Directories count  : %u\n"
				   ,
				   group->bg_block_bitmap,
				   group->bg_inode_bitmap,
				   group->bg_inode_table,
				   group->bg_free_blocks_count,
				   group->bg_free_inodes_count,
				   group->bg_used_dirs_count);    /* directories count */
		}
}

/* calculate the start address of the inode table in the first group */
off_t locate_inode_table( int ngroup, const struct ext2_group_desc *group      /* the first group-descriptor */
							    )
{
		return BLOCK_OFFSET(group->bg_inode_table + blocks_per_group * ngroup);
}

/* calculate the start address of the data blocks in the first group */
off_t locate_data_blocks( int ngroup, const struct ext2_group_desc *group      /* the first group-descriptor */
							    )
{
		return BLOCK_OFFSET(group->bg_inode_table + itable_blocks + blocks_per_group * ngroup);
}

void read_inode(fd, ngroup, offset, inode_no, inode)
     int                            fd;        /* the floppy disk file descriptor */
	 int 							ngroup;
     off_t 			   				offset;    /* offset to the start of the inode table */
     int                            inode_no;  /* the inode number to read  */
     struct ext2_inode             *inode;     /* where to put the inode */
{
        lseek(fd, BLOCK_OFFSET(blocks_per_group * ngroup) + offset + (inode_no-1)*sizeof(struct ext2_inode), SEEK_SET);
        read(fd, inode, sizeof(struct ext2_inode));
}

