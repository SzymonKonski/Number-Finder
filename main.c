#define _GNU_SOURCE
#include "library/utils.h"
#include "library/indexing.h"

#define MAX_PATH 101
#define MAX_LINE 100
#define INTERVAL 600
#define RFLAG 0

typedef struct query {
  pthread_t tid;
  char** values;
  char** dirs;
  int values_count;
  int dirs_count;
}
query_t;

typedef struct query_dto {
  pthread_t signal_tid;
  pthread_t *query_tid;
  char** dirs;
  int dirs_count;
}
query_dto_t;


void cleanup_values(void* voidArgs) {
  query_t* arg = voidArgs;
  for (int j = 0; j < arg->values_count; j++) {
   free(arg->values[j]);
  }
}

void cleanup_current_file(void* voidArgs) {
  int* current_file = voidArgs;
  close(*current_file);
}

void cleanup_numf_index(void* voidArgs) {
  char* numf_index = voidArgs;
  free(numf_index);
}

void cleanup_file_name(void* voidArgs) {
  char* file_name = voidArgs;
  free(file_name);
}

void reset_values(bool* val_is_equal, int* value, bool* write_file) {
  *val_is_equal = 0;
  *value = 0;
  *write_file = 1;
}

void read_path(int path_len, int* current_file, char* file_name) {
  ssize_t count;   
  count = TEMP_FAILURE_RETRY(bulk_read(*current_file, file_name, path_len));
  if (count != path_len) ERR("read");
}

void read_offset(int* current_file, int* offset, bool *val_is_equal) {
  ssize_t count; 
  count = TEMP_FAILURE_RETRY(bulk_read(*current_file, offset, sizeof(int)));
  if (count != sizeof(int)) ERR("read");
  if (*val_is_equal == 1) printf("%d ", *offset);
}


void* search_numf_index_files(void* voidArgs) {
  query_t* args = voidArgs;
  pthread_cleanup_push(cleanup_values, args);
  int current_file;
  ssize_t count;
  bool val_is_equal = 0;
  bool write_file = 1;
  int path_len = 0;
  int value = 0;
  int offset = 0;

  for (int i = 0; i < args->values_count; i++) {
    printf("Number %d occurrences: \n", atoi(args->values[i]));
    for (int j = 0; j < args->dirs_count; j++) {
      char* numf_index = combine_dirPath_with_fileName(args->dirs[j], INDEX_FILE_NAME);
      pthread_cleanup_push(cleanup_numf_index, numf_index);      
      current_file = TEMP_FAILURE_RETRY(open(numf_index, O_RDONLY, 0777));
      pthread_cleanup_push(cleanup_current_file,&current_file);

      // check if .numf_index file exists
      if (current_file < 0 && errno == ENOENT) {
        if (errno == ENOENT) {
          printf("%s\n", args->dirs[j]);
          printf(".numf_index file does not exist in this folder");
        } 
        else ERR("open");
      } 
      else {
        while (1) {
          if ((count = TEMP_FAILURE_RETRY(bulk_read(current_file, &path_len, sizeof(int)))) > 0) {

            // check if new path is read
            if (write_file == 0) {
              write_file = 1;
              printf("\n");
            }
            char* file_name = malloc(path_len + NULL_CHAR);
            pthread_cleanup_push(cleanup_file_name,file_name);
            read_path(path_len, &current_file, file_name);

            count = TEMP_FAILURE_RETRY(bulk_read(current_file, &value, sizeof(int)));
            if (count != (sizeof(int))) ERR("read");

            if (value == atoi(args->values[i])) {
              if(write_file == 1)
              {
                printf("%s/%s: ", args->dirs[j], file_name);
                write_file = 0;
              }
              val_is_equal = 1;
            }
            else
            val_is_equal = 0;  
            read_offset(&current_file, &offset, &val_is_equal);
            free(file_name);
            pthread_cleanup_pop(0);
          }
          else if (count == 0) {
            if(write_file == 0) printf("\n");
            break;
          }
          else perror("Read failed.");
        }
      }
      close(current_file);
      pthread_cleanup_pop(0);
      free(numf_index);
      pthread_cleanup_pop(0);
      reset_values(&val_is_equal, &value, &write_file);
    }
    reset_values(&val_is_equal, &value, &write_file);
  }

  for (int j = 0; j < args->values_count; j++) {
   free(args->values[j]);
  }
  pthread_cleanup_pop(0);
  return NULL;
}

void cleanup_read_query(void* voidArgs) {
  query_t* query = voidArgs;
  for (int j = 0; j < query->values_count; j++) {
    free(query->values[j]);
  }
}

void read_query(char command[], query_dto_t* arg, pthread_attr_t*threadAttr ) {
  char* ch;
  char* values[MAX_LINE];
  char* numbers_from_query = remove_non_digits(command);
  int values_count = 0;
  query_t query;

  ch = strtok(numbers_from_query, " ,.-");
  while (ch != NULL) {
    values[values_count] = malloc(strlen(ch) + NULL_CHAR);
    strcpy(values[values_count], ch);
    ch = strtok(NULL, " ,.-");
    values_count++;
  }

  query.dirs = arg->dirs;
  query.values = values;
  query.values_count = values_count;
  query.dirs_count = arg->dirs_count;
  pthread_cleanup_push(cleanup_read_query, &query);

  if (pthread_create(&query.tid, threadAttr, search_numf_index_files, &query))
    ERR("Couldn't create query thread!");

  pthread_cleanup_pop(0);
}

bool query_is_valid(char* command) {
  char query_command[] = "query ";
  short query_len = 6;

  for (int i = 0; i < query_len; i++) {
    if (command[i] != query_command[i]) return 0;
  }

  return 1;
}

void cleanup_commands_handling(void* voidArgs) {
  pthread_attr_t* threadAttr = voidArgs;
  pthread_attr_destroy(threadAttr);
}

void* commands_handling(void* voidArgs) {
  query_dto_t* args = voidArgs;
  char command[MAX_LINE];
  int command_len;
  pthread_attr_t threadAttr;
  if(pthread_attr_init(&threadAttr)) ERR("Couldn't create pthread_attr_t");
  if(pthread_attr_setdetachstate(&threadAttr, PTHREAD_CREATE_DETACHED)) ERR("Couldn't setdetachsatate on pthread_attr_t");
  pthread_cleanup_push(cleanup_commands_handling, &threadAttr);


  while (fgets(command, MAX_LINE, stdin) != NULL) {
    command_len = strlen(command);
    if (command_len > 0) {
      if (command[command_len - 1] == '\n')
        command[command_len - 1] = '\0';
      else
        ERR("kill");
    }
    if (strcmp(command, "status") == 0) {
      if (kill(0, SIGUSR1) < 0) ERR("kill");
    } else if (strcmp(command, "index") == 0) {
      if (kill(0, SIGUSR2) < 0) ERR("kill");
    } else if (strcmp(command, "exit") == 0) {
      if (kill(0, SIGINT) < 0) ERR("kill");
      break;
    } else {
      if (query_is_valid(command)) read_query(command, args, &threadAttr);
    }
  }

  pthread_attr_destroy(&threadAttr);
  pthread_cleanup_pop(0);
  return NULL;
}

void sigchld_handler() {
  pid_t pid;
  for(;;){
    pid=waitpid(0, NULL, WNOHANG);
    if(pid==0) return;
    if(pid<=0) {
      if(errno==ECHILD) return;
      ERR("waitpid");
    }
  }
}

void handle_main_commands(sigset_t mask, pthread_t tid) {
  bool quit = 0;
  int signo;
  while (!quit) {
    if (sigwait(&mask, &signo)) ERR("sigwait failed.");
    switch (signo) {
      case SIGINT:
        pthread_cancel(tid);
        quit = 1;
        break;

      case SIGUSR1:
        break;

      case SIGUSR2:
        break;

      case SIGALRM:
        break;

      case SIGCHLD:
        sigchld_handler();
        break;

      default:
        printf("unexpected signal %d\n", signo);
        exit(1);
    }
  }

  if (pthread_join(tid, NULL))
    ERR("Can't join with 'signal handling' thread");

}

void create_indexing_process(char* dir_path, int r_flag, int interval,sigset_t mask) {
  switch (fork()) {
    case 0:
      child_work(dir_path, r_flag, interval, mask);
      exit(EXIT_SUCCESS);
    case -1:
      perror("Fork:");
      exit(EXIT_FAILURE);
  }
}

void read_arguments(int argc, char** argv, int* r_flag, int* interval) {
  int c;
  while ((c = getopt(argc, argv, "rm:M:i:")) != -1) {
    switch (c) {
      case 'r':
        *r_flag = 1;
        break;
      case 'm':
        if (!is_number(optarg)) usage(argv[0]);
        min = atoi(optarg);
        break;
      case 'M':
        if (!is_number(optarg)) usage(argv[0]);
        max = atoi(optarg);
        break;
      case 'i':
        if (!is_number(optarg)) usage(argv[0]);
        *interval = atoi(optarg);
        break;
      default:
        usage(argv[0]);
    }
  }
}

void set_signal_maks(sigset_t* mask) {
  if (sigemptyset(mask)) ERR("sigsetempty");
  if (sigaddset(mask, SIGUSR1)) ERR("sigaddset");
  if (sigaddset(mask, SIGUSR2)) ERR("sigaddset");
  if (sigaddset(mask, SIGINT)) ERR("sigaddset");
  if (sigaddset(mask, SIGALRM)) ERR("sigaddset");
}

int main(int argc, char **argv) {
  int interval = INTERVAL;
  int r_flag = RFLAG;
  read_arguments(argc, argv, &r_flag, &interval);

  sigset_t mask;
  set_signal_maks(&mask);
  if (pthread_sigmask(SIG_BLOCK, &mask, NULL)) ERR("SIG_BLOCK error");

  // no directories were passed
  int dir_count = argc - optind;
  if (dir_count == 0) usage(argv[0]);

  char *dirs[dir_count];
  int j = 0;
  int len;
  for (int i = optind; i < argc; i++) {
    len = strlen(argv[i]);
    dirs[j] = malloc(len + NULL_CHAR);
    strncpy(dirs[j], argv[i], len);
    create_indexing_process(argv[i], r_flag, interval, mask);
    j++;
  }
  query_dto_t dto;
  dto.dirs = dirs;
  dto.dirs_count = dir_count;

  if (pthread_create(&dto.signal_tid, NULL, commands_handling, &dto))
    ERR("Couldn't create signal handling thread!");

  handle_main_commands(mask, dto.signal_tid);


  while (wait(NULL) > 0)
  if (pthread_sigmask(SIG_UNBLOCK, &mask, NULL)) ERR("SIG_BLOCK error");

  for (int i = 0; i < dir_count; i++) {
    free(dirs[i]);
  }
  return EXIT_SUCCESS;
}