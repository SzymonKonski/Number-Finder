#include "utils.h"

#define TEMP_FILE_NAME ".temp"
#define PID_FILE_NAME ".numf_pid"
#define INDEX_FILE_NAME ".numf_index"

void child_work(char* dir_path, int r_flag, int interval, sigset_t mask);
void* indexing_process(void * voidArgs);
void recursive_scan_dir(char* dir_path);
void scan_dir(char* dir_path);
void scan_file(const char* file_path);
char* transform_filePath_relativeTo_numfIndex(const char* file_path);