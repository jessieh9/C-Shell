#include <fcntl.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>  // for pid_t
#include <sys/wait.h>
#include <unistd.h>
#define MAX_PROCESS 56
#define MAX 80
#define DEBUG 0

pid_t foreground_pid = 0;  // process id for current process

pid_t child_processes[MAX_PROCESS];  // stores the PID of all child process
int child_process_num = 0;
char original[MAX];  // copy of the user input
int job_counter = 1;
int job_index = 0;
int max_jobs = 0;  // stores the number of jobs, once hits 5, will error
bool foreground;   // determines if it is foreground of background
bool builtin;  // determines if command is builtin or not - used in adding jobs
               // to the array
bool input_redirect;
bool printed;
bool output_redirect;
bool append;
char *file;   // file to be opened
char *file2;  // 2nd file if needed

struct job_control {
  int job_id;
  pid_t job_pid;
  bool foreground;    // true if foreground, false if background
  bool running;       // true if running, false if stopped
  bool terminated;    // true if terminated
  bool show;          // if false don't print, if true, print
  char command[MAX];  // stores the command that was given
};

struct job_control processes[MAX_PROCESS];

void sigint_handler() {
  // handles the CTRL + C (SIGINT) signal
  int delete_index = -1;
  foreground = false;

  if (foreground_pid > 0) {
    // send signal to kill the process
    for (int i = 0; i < MAX_PROCESS; i++) {
      if (processes[i].job_pid == foreground_pid) {
        // decrement max_jobs
        max_jobs--;
        delete_index = i;
        kill(foreground_pid, SIGINT);  // terminate the process
      }
    }

    // remove from jobs
    if (delete_index != -1) {
      // removes the struct and shifts the rest
      for (int i = delete_index; i < MAX_PROCESS - 1; i++) {
        processes[i] = processes[i + 1];
      }
    }
  }

  if (foreground_pid == 0) {
    printf("\nprompt > ");
    fflush(stdout);
  }

  return;
}

void sigtstp_handler() {
  // handles the CTRL + Z (SIGTSTP) signal

  foreground = false;

  if ((foreground_pid > 0) && (builtin == false)) {
    max_jobs++;
    struct job_control job;  // initialize a new job

    // set the attributes
    job.job_id = job_counter;
    job.job_pid = foreground_pid;
    job.running = true;
    job.foreground = true;
    job.terminated = false;
    job.show = true;
    strncpy(job.command, original, MAX);

    job_counter++;  // increment the ID
    // add job to the array
    processes[job_index] = job;
    job_index++;  // increment index
  }

  // add the process in case quit randomly
  child_processes[child_process_num] = foreground_pid;
  child_process_num++;
  if (foreground_pid > 0) {
    for (int i = 0; i < MAX_PROCESS; i++) {
      if (processes[i].job_pid == foreground_pid) {
        processes[i].running = false;
        processes[i].terminated = false;
        processes[i].show = true;
        kill(foreground_pid, SIGTSTP);  // sends stop signal
      }
    }
  }

  if (foreground_pid == 0) {
    printf("\nprompt > ");
    fflush(stdout);
  }

  return;
}

void sigchld_handler() {
  int waitCondition = WUNTRACED | WCONTINUED | WNOHANG;
  // option to track stopped (due to signal), continued, process
  // WNOHANG: waitpid return status info immediately, without waiting for
  // process to terminate;

  int childStatus;
  int delete_index = -1;
  pid_t child_pid;

  // handles when the child is terminated - call waitpid to reap the child
  //  waitpid first arg pid -1 is any child that terminates
  while ((child_pid = waitpid(-1, &childStatus, waitCondition)) > 0) {
    if ((WIFEXITED(childStatus)) && WEXITSTATUS(childStatus)) {
      // error
      foreground = false;
      printf("prompt > ");
      fflush(stdout);
    } else if (WIFEXITED(childStatus)) {
      // regular termiate
      foreground = false;

      for (int i = 0; i < MAX_PROCESS; i++) {
        if (processes[i].job_pid == child_pid) {
          delete_index = i;
          processes[i].terminated = true;
          processes[i].show = false;  // make show false if it is background
          if (max_jobs > 0) {
            max_jobs--;
          }
        }
      }

      if (delete_index != -1) {
        // removes the struct and shifts the rest
        for (int i = delete_index; i < MAX_PROCESS - 1; i++) {
          processes[i] = processes[i + 1];
        }
      }

    } else if (WIFSIGNALED(childStatus)) {
      // ctrl + c
      foreground = false;
      for (int i = 0; i < MAX_PROCESS; i++) {
        if (processes[i].job_pid == child_pid) {
          delete_index = i;
          processes[i].terminated = true;
          processes[i].show = false;  // make show false if it is background
          if (max_jobs > 0) {
            max_jobs--;
          }
        }
      }

      if (delete_index != -1) {
        // removes the struct and shifts the rest
        for (int i = delete_index; i < MAX_PROCESS - 1; i++) {
          processes[i] = processes[i + 1];
        }
      }

      printf("prompt > ");
      fflush(stdout);
    } else if (WIFSTOPPED(childStatus)) {
      //  ctrl + z
      foreground = false;
      printf("prompt > ");
      fflush(stdout);
    }
    if (WIFCONTINUED(childStatus)) {
      foreground = true;
    }
  }
}

int main() {
  /* ----  signal handlers ---- */
  signal(SIGINT, sigint_handler);
  signal(SIGTSTP, sigtstp_handler);
  signal(SIGCHLD, sigchld_handler);

  mode_t mode = S_IRWXU | S_IRWXG | S_IRWXO;  // permission bits

  // continous loop of input until user quits
  while (1) {
    output_redirect = false;
    input_redirect = false;
    append = false;
    if (foreground) {
      // if foreground is true just continue without prompting
      continue;
    }

    /* ----  defining the variables ---- */
    char user_str[MAX];   // defining input string
    char *args[MAX + 1];  // argument array (stores 80 args + 1 for the command)

    char *token;  // first token (command from user)

    /* --- prompting user and parsing input */
    printf("prompt > ");
    fgets(user_str, MAX, stdin);  // using fgets to take input from stdin

    // Remove newline character from input
    size_t len = strlen(user_str);
    if (len > 0 && user_str[len - 1] == '\n') {
      user_str[len - 1] = '\0';
    }

    strncpy(original, user_str, MAX);

    // split the input into args
    int argc = 0;
    char delimiter[] = " \t";
    token = strtok(user_str, delimiter);  // get the first token (user command)
    while (token != NULL) {
      // Remove the newline character if it's present
      args[argc] = token;
      token = strtok(NULL, delimiter);
      argc++;
    }

    int file_delete_index = -1;

    for (int i = 0; i < argc; i++) {
      if (strcmp(args[i], ">") == 0) {
        output_redirect = true;
        if (input_redirect) {
          file2 = args[i + 1];
        }
        file = args[i + 1];
        file_delete_index = i;
      }
      if (strcmp(args[i], "<") == 0) {
        input_redirect = true;
        if (output_redirect) {
          file2 = args[i + 1];
        }
        file = args[i + 1];
        file_delete_index = i;
      }
      if (strcmp(args[i], ">>") == 0) {
        append = true;
        file = args[i + 1];
        file_delete_index = i;
      }
    }

    args[argc] = NULL;  // to ensure execv and execve understand the args when
                        // passed in

    /* ---- executing the commands ---- */
    // execute only if command exist and current jobs is 5 or less
    if (args[0]) {
      if (strcmp(args[0], "cd") == 0) {
        builtin = true;
        // change working directory
        char *change_dir = args[1];  // directory to change to if command is cd
        int change;  // int for chrdir to check if error occurred

        change = chdir(change_dir);

        if (change != 0) {
          fprintf(stderr, "Cannot change to directory %s: ", change_dir);
          perror("");
        }
      } else if (strcmp(args[0], "pwd") == 0) {
        builtin = true;
        if (output_redirect) {
          int fd;
          int original_stdout = dup(1);

          // open file and store into fd
          if ((fd = open(file, O_WRONLY | O_CREAT | O_TRUNC, mode)) < 0) {
            fprintf(stderr, "Cannot open file %s: ", file);
            perror("");
            exit(1);
          }

          // dup the file descriptor to point STDOUT to the file
          dup2(fd, STDOUT_FILENO);
          char working_dir[MAX];  // current working directory
          printf("%s\n", getcwd(working_dir, MAX));
          close(fd);
          dup2(original_stdout, 1);
          close(original_stdout);
          output_redirect = false;
        } else if (append) {
          int fd;
          int original_stdout = dup(1);

          // open file and store into fd
          if ((fd = open(file, O_CREAT | O_APPEND | O_WRONLY, mode)) < 0) {
            fprintf(stderr, "Cannot open file %s: ", file);
            perror("");
            exit(1);
          }

          // dup the file descriptor to point STDOUT to the file
          dup2(fd, STDOUT_FILENO);
          char working_dir[MAX];  // current working directory
          printf("%s\n", getcwd(working_dir, MAX));
          close(fd);
          dup2(original_stdout, 1);
          close(original_stdout);
          append = false;
        } else {
          // show working directory
          char working_dir[MAX];  // current working directory
          printf("%s\n", getcwd(working_dir, MAX));
        }
      } else if (strcmp(args[0], "jobs") == 0) {
        builtin = true;
        // show all the processes (jobs)
        if (output_redirect) {
          int fd;                        // file descriptor
          int original_stdout = dup(1);  // stores original stdout

          // open file and store into fd
          if ((fd = open(file, O_WRONLY | O_CREAT | O_TRUNC, mode)) < 0) {
            fprintf(stderr, "Cannot open file %s: ", file);
            perror("");
            exit(1);
          }
          // dup the file descriptor to point STDOUT to the file
          dup2(fd, STDOUT_FILENO);
          for (int i = 0; i < MAX_PROCESS; i++) {
            char status[MAX];
            // only print if not terminated and show is true
            if ((!processes[i].terminated) && (processes[i].show)) {
              printf("[%i] (%i) ", processes[i].job_id, processes[i].job_pid);
              if (processes[i].running) {
                printf("%s ", "Running ");
              } else {
                printf("%s ", "Stopped ");
              }
              printf("%s\n", processes[i].command);
            }
          }
          close(fd);
          dup2(original_stdout, 1);
          close(original_stdout);
          output_redirect = false;
        } else if (append) {
          int fd;
          int original_stdout = dup(1);

          // open file and store into fd
          if ((fd = open(file, O_CREAT | O_APPEND | O_WRONLY, mode)) < 0) {
            fprintf(stderr, "Cannot open file %s: ", file);
            perror("");
            exit(1);
          }

          // dup the file descriptor to point STDOUT to the file
          dup2(fd, STDOUT_FILENO);
          for (int i = 0; i < MAX_PROCESS; i++) {
            char status[MAX];
            // only print if not terminated and show is true
            if ((!processes[i].terminated) && (processes[i].show)) {
              printf("[%i] (%i) ", processes[i].job_id, processes[i].job_pid);
              if (processes[i].running) {
                printf("%s ", "Running ");
              } else {
                printf("%s ", "Stopped ");
              }
              printf("%s\n", processes[i].command);
            }
          }
          close(fd);
          dup2(original_stdout, 1);
          close(original_stdout);
          append = false;
        } else {
          for (int i = 0; i < MAX_PROCESS; i++) {
            char status[MAX];
            // only print if not terminated and show is true
            if ((!processes[i].terminated) && (processes[i].show)) {
              printf("[%i] (%i) ", processes[i].job_id, processes[i].job_pid);
              if (processes[i].running) {
                printf("%s ", "Running ");
              } else {
                printf("%s ", "Stopped ");
              }
              printf("%s\n", processes[i].command);
            }
          }
        }
      } else if (strcmp(args[0], "kill") == 0) {
        // kill
        builtin = true;
        pid_t change_fg_pid = 0;
        int status;
        int jobID = -1;
        int delete_index = -1;
        if (args[1][0] == '%') {
          // job_id
          jobID = args[1][1] - '0';
          for (int i = 0; i < MAX_PROCESS; i++) {
            if (processes[i].job_id == jobID) {
              change_fg_pid = processes[i].job_pid;
            }
          }
        } else {
          // check if it's pid or job_id
          for (int i = 0; i < MAX_PROCESS; i++) {
            if (processes[i].job_id == atoi(args[1])) {
              printf("%% should be placed before a Job ID.\n");
              break;  // exit with error
            }
            if (processes[i].job_pid == atoi(args[1])) {
              change_fg_pid = atoi(args[1]);
            }
          }
          // if we exit for loop and never found PID/ JID
          if (change_fg_pid == 0) {
            printf(
                "Process ID or Job ID should be an existing process or has "
                "been formatted wrong.\n");
            continue;
          }
        }
        // kill and reap
        kill(change_fg_pid, SIGKILL);
        waitpid(change_fg_pid, &status, 0);
        // remove from jobs array
        for (int i = 0; i < MAX_PROCESS; i++) {
          if (processes[i].job_pid == change_fg_pid) {
            delete_index = i;
            processes[i].terminated = true;
            if (max_jobs > 0) {
              max_jobs--;
            }
            processes[i].show = false;  // make show false if it is background
          }
        }

        if (delete_index != -1) {
          // removes the struct and shifts the rest
          for (int i = delete_index; i < MAX_PROCESS - 1; i++) {
            processes[i] = processes[i + 1];
          }
        }

      } else if (strcmp(args[0], "fg") == 0) {
        builtin = true;
        pid_t change_fg_pid = 0;
        if (args[1][0] == '%') {
          // job_id
          change_fg_pid = args[1][1] - '0';
        } else {
          // check if it's pid or job_id
          for (int i = 0; i < MAX_PROCESS; i++) {
            if (processes[i].job_id == atoi(args[1])) {
              printf("%% should be placed before a Job ID.\n");
              break;  // exit with error
            }
            if (processes[i].job_pid == atoi(args[1])) {
              change_fg_pid = atoi(args[1]);
            }
          }
          // if we exit for loop and never found PID/ JID
          if (change_fg_pid == 0) {
            printf(
                "Process ID or Job ID should be an existing process or has "
                "been formatted wrong.\n");
            continue;
          }
        }

        // changing to fg
        for (int i = 0; i < MAX_PROCESS; i++) {
          if (processes[i].job_pid == change_fg_pid ||
              processes[i].job_id == change_fg_pid) {
            foreground_pid = processes[i].job_pid;
            // Changing a background job to the foreground
            if (!processes[i].foreground) {
              processes[i].foreground = true;
              foreground_pid = processes[i].job_pid;  // Store foreground PID
              foreground = true;
              // Send a SIGCONT signal to resume the job if it's stopped
            }
            if (!processes[i].running) {
              processes[i].running = true;
              foreground = true;
              kill(processes[i].job_pid, SIGCONT);  // Resume process
            }
          }
        }
      } else if (strcmp(args[0], "bg") == 0) {
        builtin = true;
        pid_t change_fg_pid = 0;
        if (args[1][0] == '%') {
          // job_id
          change_fg_pid = args[1][1] - '0';
        } else {
          // check if it's pid or job_id
          for (int i = 0; i < MAX_PROCESS; i++) {
            if (processes[i].job_id == atoi(args[1])) {
              printf("%% should be placed before a Job ID.\n");
              break;  // exit with error
            }
            if (processes[i].job_pid == atoi(args[1])) {
              change_fg_pid = atoi(args[1]);
            }
          }
          // if we exit for loop and never found PID/ JID
          if (change_fg_pid == 0) {
            printf(
                "Process ID or Job ID should be an existing process or has "
                "been formatted wrong.\n");
            continue;
          }
        }

        // changing to bg
        for (int i = 0; i < MAX_PROCESS; i++) {
          if (processes[i].job_pid == change_fg_pid ||
              processes[i].job_id == change_fg_pid) {
            setpgid(processes[i].job_pid, 0);

            // Changing a foreground job to the background
            if ((processes[i].foreground) && (!processes[i].running)) {
              processes[i].foreground = false;
              processes[i].running = true;
              foreground_pid = processes[i].job_pid;  // Store foreground PID
              foreground = false;
              kill(processes[i].job_pid, SIGCONT);  // Resume process
              // Send a SIGCONT signal to resume the job if it's stopped
            }
          }
        }

      } else if (strcmp(args[0], "quit") == 0) {
        builtin = true;
        // terminate all processes and shell processes then break
        for (int i = 0; i < MAX_PROCESS; i++) {
          kill(processes[i].job_pid, SIGKILL);
        }
        break;
      } else if ((args[1] != NULL) && (strcmp(args[argc - 1], "&") == 0) &&
                 (max_jobs < 5)) {
        // BACKGROUND JOB
        pid_t pid = fork();
        if (pid < 0) {
          fprintf(stderr, "Fork failed.");
          // exit failure
          exit(1);
        } else if (pid == 0) {
          setpgid(pid, 0);  // second argument sets my pid to be the pgid

          // if the file is not found exit child process
          if (execvp(args[0], args) < 0) {
            if (execv(args[0], args) < 0) {
              printf("%s file could not be executed.\n", args[0]);
              exit(1);  // exit with error status
            }
          }
          // }

        } else {
          // parent process
          builtin = false;
          foreground = false;
          max_jobs++;
          struct job_control job;  // initialize a new job

          // set the attributes
          job.job_id = job_counter;
          job.job_pid = pid;
          job.running = true;
          job.foreground = false;
          job.terminated = false;
          job.show = true;
          strncpy(job.command, original, MAX);

          job_counter++;  // increment the ID
          // add job to the array
          processes[job_index] = job;
          job_index++;  // increment index

          // add the process to processes array
          child_processes[child_process_num] = pid;
          child_process_num++;
        }
      } else if (max_jobs < 5) {
        // FOREGROUND JOB (local executables)
        pid_t pid = fork();
        if (pid < 0) {
          fprintf(stderr, "Fork failed.");
          // exit failure
          exit(1);
        } else if (pid == 0) {  // child process
          if (output_redirect) {
            int fd;
            int original_stdout = dup(1);

            fd = open(file, O_WRONLY | O_CREAT | O_TRUNC, mode);
            if (fd < 0) {
              fprintf(stderr, "Cannot open file %s: ", file);
              perror("");
              exit(1);
            }

            // dup the file descriptor to point STDOUT to the file
            dup2(fd, STDOUT_FILENO);
            close(fd);

            if (file_delete_index != -1) {
              for (int i = file_delete_index; i < argc - 1; i++) {
                args[i] = args[i + 1];
              }
            }

            // exec
            if (execvp(args[0], args) < 0) {
              if (execv(args[0], args) < 0) {
                printf("%s file could not be executed.\n", args[0]);
                exit(1);  // exit with error status
              }
            }

            dup2(original_stdout, 1);
            close(original_stdout);
            output_redirect = false;
          } else if (append) {
            int fd;
            int original_stdout = dup(1);

            // open file and store into fd
            if ((fd = open(file, O_CREAT | O_APPEND | O_WRONLY, mode)) < 0) {
              fprintf(stderr, "Cannot open file %s: ", file);
              perror("");
              exit(1);
            }

            // dup the file descriptor to point STDOUT to the file
            dup2(fd, STDOUT_FILENO);

            if (file_delete_index != -1) {
              for (int i = file_delete_index; i < argc - 1; i++) {
                args[i] = args[i + 1];
              }
            }

            // exec
            if (execvp(args[0], args) < 0) {
              if (execv(args[0], args) < 0) {
                printf("%s file could not be executed.\n", args[0]);
                exit(1);  // exit with error status
              }
            }

            close(fd);
            dup2(original_stdout, 1);
            close(original_stdout);
            append = false;
          } else if (input_redirect) {
            int fd;
            int original_stdin = dup(0);
            int nbytes;

            // open file and store in fd
            if ((fd = open(file, O_RDONLY, mode)) < 0) {
              fprintf(stderr, "Cannot open file %s: ", file);
              perror("");
              exit(1);
            }

            // dup the file descriptor to point STDIN to the file
            dup2(fd, STDIN_FILENO);

            if (file_delete_index != -1) {
              for (int i = file_delete_index; i < argc; i++) {
                args[i] = args[i + 1];
              }
            }

            // exec
            if (execvp(args[0], args) < 0) {
              if (execv(args[0], args) < 0) {
                printf("%s file could not be executed.\n", args[0]);
                exit(1);  // exit with error status
              }
            }
            close(fd);
            dup2(original_stdin, 0);
            close(original_stdin);

            input_redirect = false;
          } else {
            signal(SIGTSTP, SIG_DFL);  // reset to default signal handler

            // if the file is not found exit child process
            if (execvp(args[0], args) < 0) {
              if (execv(args[0], args) < 0) {
                printf("%s file could not be executed.\n", args[0]);
                exit(1);  // exit with error status
              }
            }
          }
        } else {  // parent process
          foreground = true;
          builtin = false;
          foreground_pid = pid;  // store foreground pid
        }
      } else if (max_jobs >= 5) {
        printf(
            "Maximum number of jobs that can be running concurrently is "
            "5.\n");
      }
    }
  }
  return 0;
}
