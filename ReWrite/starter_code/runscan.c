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

struct ext2_super_block super;
struct ext2_group_desc group;

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

// int find_file_name(int fd, struct ext2_super_block *super, struct ext2_group_desc *group, uint32_t inode_number, char *file_name)
// {
//     uint32_t i_p_g = super->s_inodes_per_group;
//     uint32_t block_size = 1024 << super->s_log_block_size;
//     struct ext2_inode dir_inode;

//     for (uint32_t j = 0; j < num_groups; j++)
//     {
//         off_t inode_table_offset = locate_inode_table(j, group);
//         // uint32_t starting_inode_number = j * i_p_g;

//         for (uint32_t i = 1; i < i_p_g; i++)
//         {
//             // uint32_t current_inode_number = starting_inode_number + i;
//             read_inode(fd, inode_table_offset, i, &dir_inode, super->s_inode_size);

//             if (S_ISDIR(dir_inode.i_mode))
//             {
//                 for (uint32_t block_idx = 0; block_idx < EXT2_NDIR_BLOCKS; block_idx++)
//                 {
//                     if (dir_inode.i_block[block_idx] == 0)
//                     {
//                         continue;
//                     }

//                     char buffer[block_size];
//                     off_t offset = BLOCK_OFFSET(dir_inode.i_block[block_idx]);
//                     lseek(fd, offset, SEEK_SET);
//                     read(fd, buffer, block_size);

//                     struct ext2_dir_entry_2 *entry = (struct ext2_dir_entry_2 *)buffer;
//                     while ((char *)entry < buffer + block_size)
//                     {
//                         if (entry->inode == inode_number)
//                         {
//                             strncpy(file_name, entry->name, entry->name_len);
//                             file_name[entry->name_len] = '\0';
//                             return 1;
//                         }

//                         entry = (struct ext2_dir_entry_2 *)((char *)entry + entry->rec_len);
//                     }
//                 }
//             }
//         }
//     }

//     return 0;
// }

void process_directory_blocks(int fd, uint32_t block_number, uint32_t block_size, uint32_t inode_number, char *file_name, int *found, int indirect_level)
{
    if (block_number == 0 || *found || indirect_level < 0)
    {
        return;
    }

    if (indirect_level == 0)
    {
        char buffer[block_size];
        off_t offset = BLOCK_OFFSET(block_number);
        lseek(fd, offset, SEEK_SET);
        read(fd, buffer, block_size);

        struct ext2_dir_entry_2 *entry = (struct ext2_dir_entry_2 *)buffer;
        while ((char *)entry < buffer + block_size && !*found)
        {
            if (entry->inode == inode_number)
            {
                strncpy(file_name, entry->name, entry->name_len);
                file_name[entry->name_len] = '\0';
                *found = 1;
                break;
            }

            entry = (struct ext2_dir_entry_2 *)((char *)entry + entry->rec_len);
        }
    }
    else
    {
        uint32_t indirect_block[INDIRECT_BLOCK_COUNT];
        off_t offset = BLOCK_OFFSET(block_number);
        lseek(fd, offset, SEEK_SET);
        read(fd, indirect_block, block_size);

        for (uint32_t i = 0; i < INDIRECT_BLOCK_COUNT && !*found; i++)
        {
            process_directory_blocks(fd, indirect_block[i], block_size, inode_number, file_name, found, indirect_level - 1);
        }
    }
}

int find_file_name(int fd, struct ext2_super_block *super, struct ext2_group_desc *group, uint32_t inode_number, char *file_name)
{
    uint32_t i_p_g = super->s_inodes_per_group;
    uint32_t block_size = 1024 << super->s_log_block_size;
    struct ext2_inode dir_inode;
    int found = 0;

    for (uint32_t j = 0; j < num_groups && !found; j++)
    {
        off_t inode_table_offset = locate_inode_table(j, group);
        // uint32_t starting_inode_number = j * i_p_g;

        for (uint32_t i = 1; i < i_p_g && !found; i++)
        {
       //     uint32_t current_inode_number = starting_inode_number + i;
            read_inode(fd, inode_table_offset, i, &dir_inode, super->s_inode_size);
            if (S_ISDIR(dir_inode.i_mode))
            {
                for (uint32_t block_idx = 0; block_idx < EXT2_NDIR_BLOCKS && !found; block_idx++)
                {
                    process_directory_blocks(fd, dir_inode.i_block[block_idx], block_size, inode_number, file_name, &found, 0);
                }

                // Read indirect blocks
                for (int indirect_level = 1; indirect_level <= 3 && !found; indirect_level++)
                {
                    uint32_t block_idx = EXT2_IND_BLOCK + (indirect_level - 1);
                    if (dir_inode.i_block[block_idx] != 0)
                    {
                        process_directory_blocks(fd, dir_inode.i_block[block_idx], block_size, inode_number, file_name, &found, indirect_level);
                    }
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

    // without this code to make a directory, it doesn't open the file
    struct stat st = {0};
    if (stat(argv[2], &st) == -1)
    {
        if (mkdir(argv[2], 0700) == -1)
        {
            perror("mkdir");
            exit(1);
        }
    }

    ext2_read_init(fd);

    // example read first the super-block and group-descriptor
    read_super_block(fd, 0, &super);
    uint32_t i_p_g = super.s_inodes_per_group;
    uint32_t size_inode_struct = super.s_inode_size;
    uint32_t j = 0;

    while (j < num_groups)
    {

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

            // to be able to track which inode we are at
            uint32_t current_inode_number = starting_inode_number + i;

            if (S_ISREG(inode.i_mode))
            {
                off_t offset = BLOCK_OFFSET(inode.i_block[0]);
                lseek(fd, offset, SEEK_SET);
                read(fd, buffer, block_size);

                if (checkJPG(buffer))
                {
                    char output_path[256];
                    // char temp_name[256];
                    // find_file_name(fd, &super, &group, current_inode_number, temp_name);
                    // printf("This is the file name: %s\n", temp_name);
                    snprintf(output_path, sizeof(output_path), "%s/file-%u.jpg", argv[2], current_inode_number);
                    // printf("This is the file name: %s\n", output_path);
                    char file_name[EXT2_NAME_LEN + 1];
                    find_file_name(fd, &super, &group, current_inode_number, file_name);

                    char output_path2[256];
                    snprintf(output_path2, sizeof(output_path2) + 1, "%s/%s", argv[2], file_name);
                    printf("This is the file name: %s\n", output_path2);

                    FILE *output_file = fopen(output_path, "w");
                    FILE *output_file2 = fopen(output_path2, "w");

                    if (!output_file)
                    {
                        perror("fopen");
                        exit(1);
                    }
                    if (!output_file2)
                    {
                        perror("fopen");
                        exit(1);
                    }

                    uint32_t bytes_left = inode.i_size;
                    uint32_t bytes_to_read = block_size;

                    for (uint32_t block_idx = 0; block_idx < EXT2_NDIR_BLOCKS; block_idx++)
                    {
                        if (inode.i_block[block_idx] == 0)
                        {
                            continue;
                        }

                        if (bytes_left == 0)
                        {
                            break;
                        }

                        if (bytes_left < block_size)
                        {
                            bytes_to_read = bytes_left;
                        }

                        offset = BLOCK_OFFSET(inode.i_block[block_idx]);
                        lseek(fd, offset, SEEK_SET);
                        read(fd, buffer, bytes_to_read);
                        fwrite(buffer, 1, bytes_to_read, output_file);
                        fwrite(buffer, 1, bytes_to_read, output_file2);
                        bytes_left = bytes_left - bytes_to_read;
                    }

                    // reading the indirect blocks
                    for (int indirect_level = 1; indirect_level <= 3 && bytes_left > 0; indirect_level++)
                    {
                        uint32_t block_idx = EXT2_IND_BLOCK + (indirect_level - 1);
                        if (inode.i_block[block_idx] != 0)
                        {
                            read_indirect_blocks(fd, inode.i_block[block_idx], buffer, output_file, block_size, &bytes_left, indirect_level);
                        }
                    }
                    /*
                         if (argc != 3)
                         {
                             printf("expected usage: ./runscan inputfile outputfile\n");
                             exit(0);
                         }

                         int fd;
                         fd = open(argv[1], O_RDONLY);

                         DIR *dir = opendir(argv[2]);
                         if (dir)
                         {
                             printf("Error: Output directory already exists.\n");
                             closedir(dir);
                             exit(1);
                         }
                         struct stat st = {0};
                         if (stat(argv[2], &st) == -1)
                         {
                             if (mkdir(argv[2], 0700) == -1)
                             {
                                 perror("mkdir");
                                 exit(1);
                             }
                         }

                         ext2_read_init(fd);
                         struct ext2_super_block super;
                         struct ext2_group_desc group;

                         read_super_block(fd, 0, &super);
                         read_group_desc(fd, 0, &group);

                         uint32_t i_p_g = super.s_inodes_per_group;
                         uint32_t size_inode_struct = super.s_inode_size;
                         uint32_t j = 0;

                         while (j < num_groups)
                         {
                             read_super_block(fd, j, &super);
                             read_group_desc(fd, j, &group);
                             off_t inode_table_offset = locate_inode_table(j, &group);

                             uint32_t starting_inode_number = j * i_p_g;

                             for (uint32_t i = 1; i < i_p_g; i++)
                             {
                                 char buffer[block_size];
                                 struct ext2_inode inode;

                                 read_inode(fd, inode_table_offset, i, &inode, size_inode_struct);

                                 uint32_t current_inode_number = starting_inode_number + i;

                                 if (S_ISREG(inode.i_mode))
                                 {
                                     off_t offset = BLOCK_OFFSET(inode.i_block[0]);
                                     lseek(fd, offset, SEEK_SET);
                                     read(fd, buffer, block_size);

                                     if (checkJPG(buffer))
                                     {
                                         char file_name[EXT2_NAME_LEN + 1];
                                         find_file_name(fd, &super, &group, current_inode_number, file_name);

                                         char output_path[256];
                                         snprintf(output_path, sizeof(output_path) + 1, "%s/%s", argv[2], file_name);
                                         printf("This is the file name: %s\n", output_path);

                                         FILE *output_file = fopen(output_path, "w");

                                         if (!output_file)
                                         {
                                             perror("fopen");
                                             exit(1);
                                         }

                                         uint32_t bytes_left = inode.i_size;
                                         uint32_t bytes_to_read = block_size;

                                         for (uint32_t block_idx = 0; block_idx < EXT2_NDIR_BLOCKS; block_idx++)
                                         {
                                             if (inode.i_block[block_idx] == 0)
                                             {
                                                 continue;
                                             }

                                             if (bytes_left == 0)
                                             {
                                                 break;
                                             }

                                             if (bytes_left < block_size)
                                             {
                                                 bytes_to_read = bytes_left;
                                             }

                                             offset = BLOCK_OFFSET(inode.i_block[block_idx]);
                                             lseek(fd, offset, SEEK_SET);
                                             read(fd, buffer, bytes_to_read);
                                             fwrite(buffer, 1, bytes_to_read, output_file);
                                             bytes_left = bytes_left - bytes_to_read;
                                         }

                                         for (int indirect_level = 1; indirect_level <= 3 && bytes_left > 0; indirect_level++)
                                         {
                                             uint32_t block_idx = EXT2_IND_BLOCK + (indirect_level - 1);
                                             if (inode.i_block[block_idx] != 0)
                                             {
                                                 read_indirect_blocks(fd, inode.i_block[block_idx], buffer, output_file, block_size, &bytes_left, indirect_level);
                                             }
                                         }
                                         */

                    fclose(output_file);
                }
            }
        }
        j++;
    }
    return 0;
}
