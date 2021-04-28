#include "library/utils.h"

int max = 1000, min = 10;
char* global_dir_path;

void usage(char* name) {
  fprintf(
      stderr,
      "USAGE: %s  [-r] [-m min=10] [-M max=1000] [-i interval=600] dir1 ... \n",
      name);
  exit(EXIT_FAILURE);
}

bool is_path_to_file(const char* path, const char* filename) {
  int path_len = strlen(path);
  int filename_len = strlen(filename);

  if (filename_len > path_len) return 0;

  int j = 0;
  int length_diff = path_len - filename_len;

  for (int i = length_diff; i < path_len; i++) {
    if (path[i] != filename[j]) return 0;
    j++;
  }

  return 1;
}

bool file_exists(char* filename) {
  struct stat buffer;
  return (stat(filename, &buffer) == 0);
}

char* combine_dirPath_with_fileName(char* dir_path, char* filename) {
  int slash = 1;
  int dir_path_len = strlen(dir_path);
  int file_path_len = strlen(filename);
  char* file_path;

  // if dir_path has no slash at the end
  if (dir_path[dir_path_len - 1] != '/') {
    file_path = malloc(dir_path_len + slash + file_path_len + NULL_CHAR);
    if (NULL == file_path) ERR("malloc");
    strcpy(file_path, dir_path);
    strcat(file_path, "/");
    strcat(file_path, filename);
  } 
  else {
    file_path = malloc(dir_path_len + file_path_len + NULL_CHAR);
    if (NULL == file_path) ERR("malloc");
    strcpy(file_path, dir_path);
    strcat(file_path, filename);
  }
  
  return file_path;
}

bool is_number(char* number) {
  int number_leng = strlen(number);
  for (size_t i = 0; i < number_leng; i++) {
    if (number[i] > '9' || number[i] < '0') return 0;
  }
  return 1;
}

bool is_digit(char digit) {
  if ((digit >= '0') && (digit <= '9')) return 1;
  return 0;
}

int is_directory_empty(char* dirname) {
  int files_count = 0;
  struct dirent* d;
  DIR* dir = opendir(dirname);

  // Not a directory or doesn't exist
  if (dir == NULL) return 1;
  while ((d = readdir(dir)) != NULL) {
    if (files_count++ > 2) break;
  }
  closedir(dir);

  // Directory Empty
  if (files_count <= 2)
    return 1;
  else
    return 0;
}

int number_of_digits_in_number(int nubmer) {
  int number_of_digits = 0;
  while (nubmer != 0) {
    nubmer /= 10;
    ++number_of_digits;
  }

  return number_of_digits;
}

ssize_t bulk_read(int fd, void* buf, size_t count) {
  ssize_t c;
  ssize_t len = 0;
  do {
    c = TEMP_FAILURE_RETRY(read(fd, buf, count));
    if (c < 0) return c;
    if (c == 0) return len;  // EOF
    buf += c;
    len += c;
    count -= c;
  } while (count > 0);
  return len;
}

ssize_t bulk_write(int fd, void* buf, size_t count) {
  ssize_t c;
  ssize_t len = 0;
  do {
    c = TEMP_FAILURE_RETRY(write(fd, buf, count));
    if (c < 0) return c;
    buf += c;
    len += c;
    count -= c;
  } while (count > 0);
  return len;
}

void msleep(UINT milisec) {
  time_t sec = (int)(milisec / 1000);
  milisec = milisec - (sec * 1000);
  timespec_t req = {0};
  req.tv_sec = sec;
  req.tv_nsec = milisec * 1000000L;
  if (nanosleep(&req, &req)) ERR("nanosleep");
}

void cleanup_transform_filePath_relativeTo_numfIndex(void* voidArgs) {
  char* relative_path = voidArgs;
  free(relative_path);
}

char* transform_filePath_relativeTo_numfIndex(const char* file_path) {
  int dir_path_len = strlen(global_dir_path);
  int file_path_len = strlen(file_path);
  char* relative_path;
  short slash = 1;

  if (file_path_len > dir_path_len) {
    if (global_dir_path[dir_path_len - 1] != '/') {
      relative_path = malloc(file_path_len - dir_path_len + NULL_CHAR);
      if (NULL == relative_path) ERR("malloc");
      pthread_cleanup_push(cleanup_transform_filePath_relativeTo_numfIndex,relative_path);
      strncpy(relative_path, file_path + dir_path_len + slash,file_path_len - dir_path_len);
      pthread_cleanup_pop(0);
    } 
    else {
      relative_path = malloc(file_path_len - dir_path_len + NULL_CHAR);
      if (NULL == relative_path) ERR("malloc");
      pthread_cleanup_push(cleanup_transform_filePath_relativeTo_numfIndex,relative_path);
      strncpy(relative_path, file_path + dir_path_len,file_path_len - dir_path_len + slash);
      pthread_cleanup_pop(0);
    }
  } 
  else {
    relative_path = malloc(file_path_len + NULL_CHAR);
    if (NULL == relative_path) ERR("malloc");
    pthread_cleanup_push(cleanup_transform_filePath_relativeTo_numfIndex,relative_path);
    strcpy(relative_path, file_path);
    pthread_cleanup_pop(0);
  }

  return relative_path;
}

char* remove_non_digits(char* input) {
  char* dest = input;
  char* src = input;

  while (*src) {
    if (isalpha(*src)) {
      src++;
      continue;
    }

    *dest++ = *src++;
  }

  *dest = '\0';
  return input;
}