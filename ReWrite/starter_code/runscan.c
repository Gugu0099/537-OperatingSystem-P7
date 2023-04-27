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
#include <inttypes.h>

#define INDIRECT_BLOCK_COUNT (block_size / sizeof(uint32_t)) // 256

struct ext2_super_block super;
struct ext2_group_desc group;

void copyFiles(char *output_path, char *output_path2)
{
    FILE *copy = fopen(output_path, "rb");
    FILE *paste = fopen(output_path2, "wb");
    int ch;
    while (1)
    {
        ch = fgetc(copy);
        if (ch == EOF)
            break;
        fputc(ch, paste);
    }
    fclose(paste);
    fclose(copy);
}

void makeDetails(char *details, struct ext2_inode *inode)
{
    FILE *detail_file = fopen(details, "wb");
    fprintf(detail_file, "%u\n%lu\n%u", inode->i_links_count, (unsigned long)inode->i_size, inode->i_uid);
    fclose(detail_file);
}

void read_indirect_blocks(int fd, uint32_t block_number, char *buffer, FILE *output_file, uint32_t block_size, uint32_t *bytes_left, int indirect_level)
{
    if (indirect_level < 1 || indirect_level > 3 || block_number == 0 || *bytes_left == 0)
    {
        return;
    }

    uint32_t indirect_block[INDIRECT_BLOCK_COUNT];
    off_t offset = BLOCK_OFFSET(block_number);
    lseek(fd, offset, SEEK_SET);
    read(fd, indirect_block, block_size);

    for (uint32_t i = 0; i<INDIRECT_BLOCK_COUNT && * bytes_left> 0; i++)
    {
        if (indirect_block[i] == 0)
        {
            continue;
        }

        if (indirect_level == 1)
        {
            uint32_t bytes_to_read = (*bytes_left < block_size) ? *bytes_left : block_size;
            offset = BLOCK_OFFSET(indirect_block[i]);
            lseek(fd, offset, SEEK_SET);
            read(fd, buffer, bytes_to_read);
            fwrite(buffer, 1, bytes_to_read, output_file);
            *bytes_left -= bytes_to_read;
        }
        else
        {
            read_indirect_blocks(fd, indirect_block[i], buffer, output_file, block_size, bytes_left, indirect_level - 1);
        }
    }
}

int find_file_name(int fd, struct ext2_super_block *super, struct ext2_group_desc *group, uint32_t inode_number, char *file_name)
{
    uint32_t i_p_g = super->s_inodes_per_group;
    uint32_t block_size = 1024;
    struct ext2_inode dir_inode;
    int found = 0;

    uint32_t num_groups = (super->s_blocks_count + super->s_blocks_per_group - 1) / super->s_blocks_per_group;

    for (uint32_t j = 0; j < num_groups && !found; j++)
    {
        off_t inode_table_offset = locate_inode_table(j, group);
        for (uint32_t i = 1; i < i_p_g && !found; i++)
        {

            read_inode(fd, inode_table_offset, i, &dir_inode, super->s_inode_size);
            if (S_ISDIR(dir_inode.i_mode))
            {
                char buffer[block_size];
                off_t offset = BLOCK_OFFSET(dir_inode.i_block[0]);
                lseek(fd, offset, SEEK_SET);
                read(fd, buffer, block_size);

                struct ext2_dir_entry_2 *entry = (struct ext2_dir_entry_2 *)buffer;
                while ((char *)entry < buffer + block_size && !found)
                {
                    if (entry->inode == inode_number)
                    {
                        strncpy(file_name, entry->name, entry->name_len);
                        file_name[entry->name_len] = '\0';
                        found = 1;
                        break;
                    }

                    off_t off = 8 + entry->name_len;
                    if (off % 4 != 0)
                    {
                        off = off + 4 - (off % 4);
                    }
                    entry = (struct ext2_dir_entry_2 *)((char *)entry + off);
                }
            }
        }
    }
    return found;
}

int checkJPG(char *buffer)
{
    return (buffer[0] == (char)0xff &&
            buffer[1] == (char)0xd8 &&
            buffer[2] == (char)0xff &&
            (buffer[3] == (char)0xe0 ||
             buffer[3] == (char)0xe1 ||
             buffer[3] == (char)0xe8));
}

void save_jpg(int fd, struct ext2_inode *inode, uint32_t current_inode_number, const char *output_dir)
{
    char buffer[block_size];
    off_t offset = BLOCK_OFFSET(inode->i_block[0]);
    lseek(fd, offset, SEEK_SET);
    read(fd, buffer, block_size);

    if (checkJPG(buffer))
    {
        char output_path[256];
        snprintf(output_path, sizeof(output_path), "%s/file-%u.JPG", output_dir, current_inode_number);

        FILE *output_file = fopen(output_path, "wb");
        if (!output_file)
        {
            perror("fopen: output path");
            exit(1);
        }

        uint32_t bytes_left = inode->i_size;
        uint32_t bytes_to_read = block_size;

        for (uint32_t block_idx = 0; block_idx < EXT2_NDIR_BLOCKS && bytes_left > 0; block_idx++)
        {
            if (bytes_left < block_size)
            {
                bytes_to_read = bytes_left;
            }

            off_t offset = BLOCK_OFFSET(inode->i_block[block_idx]);
            lseek(fd, offset, SEEK_SET);
            read(fd, buffer, bytes_to_read);
            fwrite(buffer, 1, bytes_to_read, output_file);
            bytes_left -= bytes_to_read;
        }

        for (int indirect_level = 1; indirect_level <= 3 && bytes_left > 0; indirect_level++)
        {
            uint32_t block_idx = EXT2_IND_BLOCK + (indirect_level - 1);
            if (inode->i_block[block_idx] != 0)
            {
                read_indirect_blocks(fd, inode->i_block[block_idx], buffer, output_file, block_size, &bytes_left, indirect_level);
            }
        }

        fclose(output_file);
        char file_name[EXT2_NAME_LEN];
        find_file_name(fd, &super, &group, current_inode_number, file_name);
        char output_path2[256];
        snprintf(output_path2, sizeof(output_path2), "%s/%s", output_dir, file_name);

        copyFiles(output_path, output_path2); // This method just copy contents to file name file from inode file

        char details[256];
        snprintf(details, sizeof(details), "%s/file-%u-details.txt", output_dir, current_inode_number);
        makeDetails(details, inode); //This method makes details file
    }
}

void process_inodes(int fd, const char *output_dir)
{
    for (uint32_t group_idx = 0; group_idx < num_groups; group_idx++)
    {
        read_super_block(fd, group_idx, &super);
        read_group_desc(fd, group_idx, &group);
        uint32_t starting_inode_number = group_idx * super.s_inodes_per_group;

        for (uint32_t i = 1; i < super.s_inodes_per_group; i++)
        {
            uint32_t current_inode_number = starting_inode_number + i;
            off_t inode_table_offset = locate_inode_table(group_idx, &group);
            struct ext2_inode inode;
            read_inode(fd, inode_table_offset, i, &inode, super.s_inode_size);

            if (S_ISREG(inode.i_mode))
            {
                save_jpg(fd, &inode, current_inode_number, output_dir);
            }
        }
    }
}

int main(int argc, char **argv)
{
    if (argc != 3)
    {
        printf("expected usage: ./runscan inputfile outputfile\n");
        exit(0);
    }

    DIR *dir = opendir(argv[2]);
    if (dir != NULL)
    {
        printf("Error: Output directory already exists.\n");
        closedir(dir);
        exit(1);
    }

    int fd;
    fd = open(argv[1], O_RDONLY);

    if (stat(argv[2], &(struct stat){0}) == -1 && mkdir(argv[2], 0700) == -1)
    {
        perror("mkdir");
        exit(1);
    }

    ext2_read_init(fd);
    read_super_block(fd, 0, &super);

    process_inodes(fd, argv[2]);

    closedir(dir);
    return 0;
}
