#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <time.h>
#include <dirent.h>
#include <ftw.h>
#include <stddef.h>
#include <pthread.h>
#include <stdarg.h>
#include<limits.h>
#include <stdbool.h>
#include <ctype.h>

#define ERR(source) (fprintf(stderr,"%s:%d\n",__FILE__,__LINE__),\
                     perror(source),kill(0,SIGKILL),\
                                     exit(EXIT_FAILURE))

#define NULL_CHAR 1

typedef unsigned int UINT;
typedef struct timespec timespec_t;

extern int max;
extern int min;
extern char* global_dir_path;  

void usage(char * name); 
bool is_path_to_file(const char *path, const char *name);
bool file_exists(char * filename);
char* combine_dirPath_with_fileName(char* dir_path,  char* filename);
bool is_number(char* number);
void create_numf_pid_file(char * file_path);
bool is_digit(char number);
int number_of_digits_in_number(int nubmer);
int is_directory_empty(char *dirname);
ssize_t bulk_read(int fd, void *buf, size_t count);
ssize_t bulk_write(int fd, void *buf, size_t count);
void msleep(UINT milisec);
char* transform_filePath_relativeTo_numfIndex(const char* file_path);
char* remove_non_digits(char* input);