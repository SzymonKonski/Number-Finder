#define _GNU_SOURCE
#define _XOPEN_SOURCE 500
#include "library/utils.h"
#include "library/indexing.h"

#define MAXFD 20
#define READ_BLOCK_SIZE 4096
#define WAIT_TIME 10
#define ELAPSED(start, end)((end).tv_sec - (start).tv_sec) + (((end).tv_nsec - (start).tv_nsec) * 1.0e-9)

typedef struct indexer {
  pthread_t tid;
  char* tempfile_path;
  char* dir_path;
  char* numf_index_path;
  char* numf_pid_path;
  int recursive_scan;
  pthread_t main_threadId;
  int* process_is_active;
  int* sigusr1_sent_from_thread;
  pthread_mutex_t* is_active_mutex;
  pthread_mutex_t* sigusr1_mutex;
}
indexer_t;

typedef struct scan_cleaner {
  char*temp_index_path;
  char*relative_path;
  char* current_number;
  int* temp_file;
  int* current_file;
}scan_cleaner_t;

typedef struct indexing_timer {
  timespec_t indexing_start_time;
  timespec_t current_time;
} indexing_timer_t;


void cleanup_create_numf_pid_file(void* voidArgs) {
  int* fd = voidArgs;
  if (close(*fd)) ERR("close");
}

void cleanup_scan_file(void* voidArgs) {
  scan_cleaner_t* arg = voidArgs;
  if (TEMP_FAILURE_RETRY(close(*arg->temp_file))) ERR("fclose");
  if (TEMP_FAILURE_RETRY(close(*arg->current_file))) ERR("fclose");
  free(arg->current_number);
  free(arg->temp_index_path);
  free(arg->relative_path);
}

void clean_scan_dir(void* voidArgs) {
  DIR* dirp = voidArgs;
  if (closedir(dirp)) ERR("closedir");
}

void clean_indexing_process(void* voidArgs) {
  indexer_t* args = voidArgs;
  pthread_mutex_lock(args->is_active_mutex);
  *args->process_is_active = 0;
  pthread_mutex_unlock(args->is_active_mutex);

  pthread_mutex_lock(args->sigusr1_mutex);
  *args->sigusr1_sent_from_thread = 1;
  pthread_mutex_unlock(args->sigusr1_mutex);
  if (unlink(args->tempfile_path) && errno != ENOENT) ERR("unlink");
  free(args->tempfile_path);
}


void write_to_temp_file(int* temp_file, char* relative_path,  int* relative_path_len, int* val, int* offset) {
  ssize_t buf;

  // write relative path length
  buf = TEMP_FAILURE_RETRY(bulk_write(*temp_file, relative_path_len, sizeof(int)));
  if (buf != (sizeof(int))) ERR("write");

  // write relative path
  buf = TEMP_FAILURE_RETRY(bulk_write(*temp_file, relative_path,(strlen(relative_path))));
  if (buf != (strlen(relative_path))) ERR("write");

  // write value 
  buf = TEMP_FAILURE_RETRY(bulk_write(*temp_file, val, sizeof(int)));
  if (buf != sizeof(int)) ERR("write");

  // write offset
  buf = TEMP_FAILURE_RETRY(bulk_write(*temp_file, offset, sizeof(int)));
  if (buf != sizeof(int)) ERR("write");
}

void scan_file(const char* file_path) {
  char* temp_index_path = combine_dirPath_with_fileName(global_dir_path, TEMP_FILE_NAME);
  char* relative_path = transform_filePath_relativeTo_numfIndex(file_path);
  int number_of_digits = number_of_digits_in_number(max);
  char* current_number = malloc(number_of_digits + 1);
  if (current_number == relative_path) ERR("malloc");
  int relative_path_len = strlen(relative_path);
  int temp_file;
  int current_file;

  scan_cleaner_t clean;
  clean.current_number = current_number;
  clean.relative_path = relative_path;
  clean.temp_index_path = temp_index_path;

  if ((temp_file = TEMP_FAILURE_RETRY(open(temp_index_path, O_WRONLY | O_CREAT | O_APPEND, 0777))) < 0)
    ERR("open");
  if ((current_file = TEMP_FAILURE_RETRY(open(file_path, O_RDONLY, 0777))) < 0)
    ERR("open");
  clean.temp_file = &temp_file;
  clean.current_file = &current_file;
  pthread_cleanup_push(cleanup_scan_file, &clean);

  memset(current_number, 0, number_of_digits + 1);
  char buffer[READ_BLOCK_SIZE];
  int index = 0;
  int offset = 0;
  bool number_is_too_long = 0;
  char c;
  ssize_t count;

  while (1) {
    // read block of bytes
    if ((count = TEMP_FAILURE_RETRY(bulk_read(current_file, buffer, READ_BLOCK_SIZE))) > 0) {

      // process read block
      for (int j = 0; j < count; j++) {
        c = buffer[j];

        // if character is digit
        if (is_digit(c)) {

          // if read number is too long, mark it as too long
          if (index >= number_of_digits) number_is_too_long = 1;

          if (number_is_too_long == 0) {
            current_number[index] = buffer[j];
            index++;
          }
        } 
        else {
          // if read number was too long, clear buffer
          if (index > 0 && number_is_too_long == 1) {
            index = 0;
            number_is_too_long = 0;
            memset(current_number, 0, (number_of_digits + 1));
          } 
          else if (index > 0) {
            errno = 0;
            int value_offset = offset - index + 1;
            char* ptr;
            int val = strtol(current_number, &ptr, 10);

            if (errno != 0) ERR("strtol");
            if (ptr == current_number) ERR("strtol");

            if ((val >= min) && (val <= max)) {
             write_to_temp_file(&temp_file, relative_path, &relative_path_len, &val, &value_offset);
            }

            memset(current_number, 0, (number_of_digits + 1));
            index = 0;
          }
        }
        offset++;
      }
      memset(buffer, 0, READ_BLOCK_SIZE);

    } 
    else if (count == 0) {

      // check if buffer has some digits
      if (index > 0) {
        errno = 0;
        int value_offset = offset - index + 1;
        char* ptr;
        int val = strtol(current_number, &ptr, 10);

        if (errno != 0) ERR("strtol");
        if (ptr == current_number) ERR("strtol");

        if ((val >= min) && (val <= max)) {
          write_to_temp_file(&temp_file, relative_path, &relative_path_len, &val, &value_offset);
        }
      }
      break;
    } else
      perror("Read failed.");
  }

  if (TEMP_FAILURE_RETRY(close(temp_file))) ERR("fclose");
  if (TEMP_FAILURE_RETRY(close(current_file))) ERR("fclose");
  free(current_number);
  free(temp_index_path);
  free(relative_path);
  pthread_cleanup_pop(0);
}

void scan_dir(char* dir_path) {
  char cwd[PATH_MAX];
  if (getcwd(cwd, sizeof(cwd)) == NULL) ERR("Cwd");
  if (chdir(dir_path)) ERR("chdir");

  DIR* dirp;
  struct dirent* dp;
  struct stat filestat;

  if (NULL == (dirp = opendir("."))) ERR("opendir");
  pthread_cleanup_push(clean_scan_dir, dirp);

  do {
    errno = 0;
    if ((dp = readdir(dirp)) != NULL) {
      if (lstat(dp->d_name, &filestat)) ERR("lstat");

      // make sure scan_file wont scan: numf_pid numf_index temp files
      if (S_ISREG(filestat.st_mode) &&is_path_to_file(dp->d_name, ".numf_pid") == 0 &&is_path_to_file(dp->d_name, ".numf_index") == 0 &&is_path_to_file(dp->d_name, ".temp") == 0) {
        scan_file(dp->d_name);
      }
    }
  } while (dp != NULL);

  if (errno != 0) ERR("readdir");
  if (closedir(dirp)) ERR("closedir");
  if (chdir(cwd)) ERR("chdir");
  pthread_cleanup_pop(0);
}

int walk(const char* file_path, const struct stat* s, int type, struct FTW* f) {
  // make sure scan_file wont scan: .numf_pid;.numf_index;.temp files
  if (type == FTW_F && is_path_to_file(file_path, ".numf_pid") == 0 &&
      is_path_to_file(file_path, ".numf_index") == 0 &&
      is_path_to_file(file_path, ".temp") == 0) {
    scan_file(file_path);
  }

  return 0;
}

void recursive_scan_dir(char* dir_path) {
  if (nftw(dir_path, walk, MAXFD, FTW_PHYS) == 0) {} 
  else
    printf("%s: brak dostepu\n", dir_path);
}

void* indexing_process(void* voidArgs) {
  indexer_t* args = voidArgs;
  char* tempfile_path = combine_dirPath_with_fileName(args->dir_path, TEMP_FILE_NAME);
  args->tempfile_path = tempfile_path;
  pthread_cleanup_push(clean_indexing_process, args);

  if (args->recursive_scan)
    recursive_scan_dir(args->dir_path);
  else
    scan_dir(args->dir_path);

  if (rename(args->tempfile_path, args->numf_index_path)) ERR("rename");

  pthread_mutex_lock(args->is_active_mutex);
  *args->process_is_active = 0;
  pthread_mutex_unlock(args->is_active_mutex);

  pthread_mutex_lock(args->sigusr1_mutex);
  *args->sigusr1_sent_from_thread = 1;
  pthread_mutex_unlock(args->sigusr1_mutex);

  free(args->tempfile_path);
  if (pthread_kill(args->main_threadId, SIGUSR1)) ERR("pthread kill");
  pthread_cleanup_pop(0);

  return NULL;
}

void exit_process(indexer_t* arg, pthread_attr_t* attr) {
  alarm(0);
  short end = 1;

  // cancel indexig if is running
  pthread_mutex_lock(arg->is_active_mutex);
  if (*arg->process_is_active == 1) {
    pthread_mutex_unlock(arg->is_active_mutex);
    printf("Thrad was canceled\n");
    pthread_cancel(arg->tid);
  }
  pthread_mutex_unlock(arg->is_active_mutex);

  // wait for thread to end
  do {
    pthread_mutex_lock(arg->is_active_mutex);
    end = *arg->process_is_active;
    pthread_mutex_unlock(arg->is_active_mutex);
    msleep(WAIT_TIME);
  } while (end);

  if (unlink(arg->numf_pid_path) && errno != ENOENT) ERR("unlink");
  free(arg->numf_index_path);
  free(arg->numf_pid_path);
  pthread_attr_destroy(attr);
  printf("PROCESS with pid %d terminates\n", getpid());
}

void handle_sigusr1(indexer_t* arg, time_t* measured_time, int interval, indexing_timer_t* timer) {
     pthread_mutex_lock(arg->is_active_mutex);
        if (*arg->process_is_active == 0) {
          pthread_mutex_unlock(arg->is_active_mutex);

          pthread_mutex_lock(arg->sigusr1_mutex);
          if (*arg->sigusr1_sent_from_thread == 1) {
            pthread_mutex_unlock(arg->sigusr1_mutex);
            printf("Indexing has ended\n");
            alarm(interval);
            pthread_mutex_lock(arg->sigusr1_mutex);
            *arg->sigusr1_sent_from_thread = 0;
            pthread_mutex_unlock(arg->sigusr1_mutex);
            printf("Alarm is set\n");
          } 
          else {
            pthread_mutex_unlock(arg->sigusr1_mutex);
            printf("Indexing is not active\n");
          }
        } 
        else if (*arg->process_is_active == 1) {
          pthread_mutex_unlock(arg->is_active_mutex);
          if (clock_gettime(CLOCK_REALTIME, &(timer->current_time)))
            ERR("Failed to retrieve time!");
          *measured_time = ELAPSED(timer->indexing_start_time, timer->current_time);
          printf("Elapsed time %ld\n", *measured_time);
        }
        pthread_mutex_unlock(arg->is_active_mutex);
}

void handle_sigusr2(indexer_t* arg, indexing_timer_t* timer, pthread_attr_t* attr) {
  pthread_mutex_lock(arg->is_active_mutex);
  if (*arg->process_is_active == 0) {
    pthread_mutex_unlock(arg->is_active_mutex);
    printf("Indexing started \n");

    pthread_mutex_lock(arg->is_active_mutex);
    *arg->process_is_active = 1;
    pthread_mutex_unlock(arg->is_active_mutex);

    if (clock_gettime(CLOCK_REALTIME, &(timer->indexing_start_time)))
      ERR("Failed to retrieve time!");
    int err = pthread_create(&arg->tid, attr, indexing_process, arg);
    if (err != 0) ERR("Couldn't create thread");
  } 
  else if (*arg->process_is_active == 1) {
    pthread_mutex_unlock(arg->is_active_mutex);
    printf("Indexing is already active\n");
  }
  pthread_mutex_unlock(arg->is_active_mutex);
}

void handle_sigalarm(indexer_t* arg, indexing_timer_t* timer, pthread_attr_t* attr) {
  pthread_mutex_lock(arg->is_active_mutex);
  if (*arg->process_is_active == 0) {
    pthread_mutex_unlock(arg->is_active_mutex);
    printf("Indexing started after alarm\n");

    pthread_mutex_lock(arg->is_active_mutex);
    *arg->process_is_active = 1;
    pthread_mutex_unlock(arg->is_active_mutex);

    if (clock_gettime(CLOCK_REALTIME, &(timer->indexing_start_time)))
      ERR("Failed to retrieve time!");
    int err = pthread_create(&arg->tid, attr, indexing_process, arg);
    if (err != 0) ERR("Couldn't create thread");
  }
  pthread_mutex_unlock(arg->is_active_mutex);
}

void handle_signals(indexer_t* arg, sigset_t mask, int interval, indexing_timer_t* timer, pthread_attr_t* attr) {
  int signo;
  time_t measured_time;
  bool end = 0;
  while (!end) {
    if (sigwait(&mask, &signo)) ERR("sigwait failed.");
    switch (signo) {
      case SIGUSR1:
        handle_sigusr1(arg,&measured_time, interval, timer);
        break;
      case SIGUSR2:
         handle_sigusr2(arg, timer, attr);
        break;
      case SIGALRM:
        handle_sigalarm(arg, timer, attr);
        break;
      case SIGINT:
        exit_process(arg, attr);
        end = 1;
        break;
      default:
        printf("unexpected signal %d\n", signo);
        exit(1);
    }
  }
}

void start_indexing(indexer_t* arg, indexing_timer_t* timer, pthread_attr_t* attr) {
  pthread_mutex_lock(arg->is_active_mutex);
  *arg->process_is_active = 1;
  pthread_mutex_unlock(arg->is_active_mutex);
  printf("Process started\n");
  if (clock_gettime(CLOCK_REALTIME, &(timer->indexing_start_time)))
    ERR("Failed to retrieve time!");
  int err = pthread_create(&(arg->tid), attr, indexing_process, arg);
  if (err != 0) ERR("Couldn't create thread");
}

void create_numf_pid_file(char* file_path) {
  pid_t pid = getpid();
  pid_t id;
  int fd;
  ssize_t buf;

  fd = TEMP_FAILURE_RETRY(open(file_path, O_RDWR | O_CREAT | O_EXCL, 0777));
  pthread_cleanup_push(cleanup_create_numf_pid_file, &fd);
  if (fd < 0 && errno == EEXIST) {
    if ((fd = TEMP_FAILURE_RETRY(open(file_path, O_RDONLY, 0777))) < 0) ERR("open");
    buf = TEMP_FAILURE_RETRY(read(fd, &id, sizeof(pid_t)));
    if (buf != (sizeof(pid_t))) ERR("write");
    printf("Dir is used by: %d\n", id);
    if (close(fd)) ERR("close");
    exit(EXIT_SUCCESS);
  } else {
    buf = TEMP_FAILURE_RETRY(write(fd, &pid, sizeof(pid)));
    if (buf != (sizeof(pid_t))) ERR("write");
    if (close(fd)) ERR("close");
  }
  pthread_cleanup_pop(0);
}

void child_work(char* dir_path, int recursive_scan, int interval, sigset_t mask) {
  if (!dir_path) ERR("Null dir path");
  if (is_directory_empty(dir_path) == 1) ERR("Empty dir");
  global_dir_path = dir_path;
  char* numf_pid_path = combine_dirPath_with_fileName(dir_path, PID_FILE_NAME);
  char* numf_index_path = combine_dirPath_with_fileName(dir_path, INDEX_FILE_NAME);

  create_numf_pid_file(numf_pid_path);

  pthread_attr_t threadAttr;
  indexer_t arg;
  indexing_timer_t timer;
  //prepare_indexing_thread(&arg,  numf_pid_path, dir_path,  numf_index_path,  recursive_scan, &threadAttr);

  pthread_mutex_t sigusr1_mutex = PTHREAD_MUTEX_INITIALIZER;
  pthread_mutex_t is_active_mutex = PTHREAD_MUTEX_INITIALIZER;
  if (pthread_attr_init(&threadAttr)) ERR("Couldn't create pthread_attr_t");
  if (pthread_attr_setdetachstate(&threadAttr, PTHREAD_CREATE_DETACHED))
    ERR("Couldn't setdetachsatate on pthread_attr_t");

  // indexer_t struct initialization
  int sigusr1_sent_from_thread = 0;
  int process_is_active = 0;
  arg.sigusr1_mutex = &sigusr1_mutex;
  arg.is_active_mutex = &is_active_mutex;
  arg.numf_pid_path = numf_pid_path;
  arg.dir_path = dir_path;
  arg.numf_index_path = numf_index_path;
  arg.recursive_scan = recursive_scan;
  arg.sigusr1_sent_from_thread = &sigusr1_sent_from_thread;
  arg.main_threadId = pthread_self();
  arg.process_is_active = &process_is_active;

  // if numf_index doesnt exist, run indexing procedure
  if (!file_exists(numf_index_path)) 
    start_indexing(&arg, &timer, &threadAttr);

  handle_signals(&arg, mask, interval, &timer, &threadAttr);

  return;
}