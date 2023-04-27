CC = gcc
CFLAGS = -Wall -Wextra -Werror
DFLAGS = -g
DEPENDENCIES.C = read_ext2.c
EXEC = runscan
MAIN.C = runscan.c

default: main

clean:
	rm -f $(EXEC)

main: $(MAIN.C)
	$(CC) $(CFLAGS) $(DFLAGS) $(MAIN.C) $(DEPENDENCIES.C) -o $(EXEC)
	runscan ../../P7/test_disk_images/test_3/03.img output3
	runscan ../../P7/test_disk_images/test_4/04.img output4
	runscan ../../P7/test_disk_images/test_7/07.img output7

	python3 ../../P7/rcheck.py output3 ../../P7/test_disk_images/test_3/output
	python3 ../../P7/rcheck.py output4 ../../P7/test_disk_images/test_4/output
	python3 ../../P7/rcheck.py output7 ../../P7/test_disk_images/test_7/output
