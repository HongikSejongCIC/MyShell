#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <unistd.h>
#include <termio.h>
#include <fcntl.h>
#include <pwd.h>
#include <sys/types.h>
#include <sys/wait.h>

#define MAX_LINE_LENGTH       1 << 10
#define MAX_ARG_LENGTH       1 << 10
#define MAX_ARG_SIZE           100
#define MAX_HIST_SIZE           20

#define INPUT_REDIRECTION     ">"
#define OUTPUT_REDIRECTION   "<"
#define APPEND_REDIRECTION   ">>"
#define PIPELINE                "|"
#define BACKGROUND           "&"

#define ENTER                  10
#define QUIT                   4
#define BACKSPACE             8
#define ESC                    27
#define LSB                    91

#define STATE_NONCANONICAL  0
#define STATE_CANONICAL      1

#define DELIMS                " \t\r\n"

void set_input_mode(void);
void reset_input_mode(void);
int prompt(char*);

void init_history(char**);
void get_history(char**, int);
void print_history(char**, int);
void clear_history(char**);
bool parse(char*, char**, size_t*);
int lookupRedirection(char**, size_t, int*);
bool lookupBackground(char**, size_t);
bool execute(char**, size_t);

struct termios saved_tty;

char* hist[MAX_HIST_SIZE];
int current_cursor = 0;

int main(int argc, char** argv)
{
  char line[MAX_LINE_LENGTH];

  init_history(hist);
  set_input_mode();

  while(true)
  {
    char* arguments[MAX_ARG_LENGTH];
    size_t argument_count = 0;

    if(prompt(line) == 0) continue;

    hist[current_cursor] = strdup(line);
    current_cursor = (current_cursor + 1) % MAX_HIST_SIZE;

    parse(line, arguments, &argument_count);

    if(argument_count == 0) continue;

    if(strcmp(arguments[0], "exit") == 0)
      exit(EXIT_SUCCESS);
    else if((strcmp(arguments[0], "history") == 0))
      print_history(hist, current_cursor);
    else if(strcmp(arguments[0], "help") == 0)
    {
      fprintf(stdout, "간단한 쉘 만들기\n");
      fprintf(stdout, "학번: 201017093\n");
      fprintf(stdout, "이름: 이상윤\n");
    }
    else if(strcmp(arguments[0], "clear") == 0)
      system(arguments[0]);
    else  
      execute(arguments, argument_count);
  }

  reset_input_mode();
  clear_history(hist);

  return EXIT_SUCCESS;
}

/* HISTORY */
void init_history(char** hist_buf)
{
  int i;
  for(i=0; i<MAX_HIST_SIZE; ++i) hist_buf[i] = NULL;
}

void print_history(char** hist_buf, int cursor)
{
  int i = cursor;
  int n = 1;

  do
  {
    if(hist_buf[i])
      fprintf(stdout, "%2d: %s\n", n++, hist_buf[i]);

    i = (i + 1) % MAX_HIST_SIZE;
  }
  while(i != cursor);
}

void clear_history(char** hist_buf)
{
  int i;

  for(i=0; i<MAX_HIST_SIZE; ++i)
  {
    free(hist_buf[i]);
    hist_buf[i] = NULL;
  }
}

/* INPUT - (NON)CANONICAL MODE */
void set_input_mode(void)
{
  struct termios tty;

  if(!isatty(STDIN_FILENO))
  {
    perror("isatty");

    exit(EXIT_FAILURE);
  }

  tcgetattr(STDIN_FILENO, &saved_tty);
  atexit(reset_input_mode);

  tcgetattr(STDIN_FILENO, &tty);
  tty.c_lflag &= ~(ICANON | ECHO);
  tty.c_cc[VMIN] = 1;
  tty.c_cc[VTIME] = 0;
  tcsetattr(STDIN_FILENO, TCSAFLUSH, &tty);
}

void reset_input_mode(void)
{
  tcsetattr(STDIN_FILENO, TCSANOW, &saved_tty);
}

/* SHELL PROMPT */
int prompt(char* line)
{
  int n = 0;
  int len = 0;
  char c;

  int cur = current_cursor;

  fflush(NULL);

  fprintf(stdout, "[%s]$ ", get_current_dir_name());

  while(c = fgetc(stdin))
  {
    if(c == QUIT)
    {
      fputc('\n', stdout);
      return 0;
    }

    if(c == ENTER)
    {
      fputc('\n', stdout);
      break;      
    }

    if((c != BACKSPACE) && (c <= 26)) break;

    switch(c)
    {
      case BACKSPACE:
        if(n == 0) break;

        fputc('\b', stdout);
        fputc(' ', stdout);
        fputc('\b', stdout);

        line[--n] = (char) 0;

        break;

      case  ESC:
        if((c = fgetc(stdin)) != LSB) break;

        switch(fgetc(stdin))
        {
          case 'A':
            --cur;
            if(cur < 0 ) cur = current_cursor - 1;

            fprintf(stdout, "\r%80s", " ");
            fprintf(stdout, "\r[%s]$ %s", get_current_dir_name(), hist[cur]);

            memset(line, '\0', MAX_LINE_LENGTH);
            strcpy(line, hist[cur]);
            n = strlen(line);

            break;

          case 'B':
            ++cur;
              if(cur >= current_cursor) cur = 0;

              fprintf(stdout, "\r%80s", " ");
              fprintf(stdout, "\r[%s]$ %s", get_current_dir_name(), hist[cur]);

              memset(line, '\0', MAX_LINE_LENGTH);
              strcpy(line, hist[cur]);
              n = strlen(line);

              break;
        }

        break;

      default:
        fputc(c, stdout);
        line[n++] = (char) c;

        break;
    }
  }

  line[n] = '\0';

  len = strlen(line);

  if(len == 0) return 0;

  for(n=0; n<len; ++n)
    if(line[n] != ' ' && line[n] != '\t') return 1;

  return 0;
}

int lookupRedirection(char** argv, size_t argc, int* flag)
{
  int i;

  for(i=0; i<(int)argc; ++i)
  {
    if(strcmp(argv[i], INPUT_REDIRECTION) == 0)
    {
      *flag = O_WRONLY | O_CREAT | O_TRUNC;

      break;
    }
    
    if(strcmp(argv[i], APPEND_REDIRECTION) == 0)
    {
      *flag = O_WRONLY | O_CREAT | O_APPEND;

      break;
    }
  }

  return i;
}

bool lookupBackground(char** argv, size_t argc)
{
  return (strcmp(argv[argc - 1], BACKGROUND) == 0);
}

bool parse(char* line, char** argv, size_t* argc)
{
  size_t n = 0;
  char* temp = strtok(line, DELIMS);

  if(temp == NULL) return false;

  while(temp != NULL)
  {
    argv[n++] = temp;

    temp = strtok(NULL, DELIMS);
  }
  argv[n] = NULL;

  *argc = n;

  return true;
}

bool execute(char** argv, size_t argc)
{
  char* params[MAX_ARG_LENGTH];

  int fd = -1;
  int flag = 0;
  int idx = lookupRedirection(argv, argc, &flag);

  bool bg = lookupBackground(argv, argc);

  pid_t pid;
  int status;

  int i;

  if((pid = fork()) == -1)
  {
    perror("fork");

    return false;
  }
  else if(pid == 0)
  {
    for(i=0; i<idx; ++i)
    {
      if(strcmp(argv[i], BACKGROUND) == 0) break;

      params[i] = argv[i];
    }

    if(flag > 0)
    {
      if((fd = open(argv[i+1], flag, 0644)) == -1)
      {
        perror("open");

        return false;
      }

      if(close(STDOUT_FILENO) == -1)
      {
        perror("close");
        
        return false;
      }   

      if(dup2(fd, STDOUT_FILENO) == -1)
      {
        perror("dup2");

        return false;
      }

      if(close(fd) == -1)
      {
        perror("close");

        return false;
      }
    }

    if(execvp(argv[0], params) == -1)
    {
      perror("execvp");

      return false;
    }

    exit(EXIT_SUCCESS);
  }
  else
  {
    if(bg == true)
    {
      return true;
    }
    else
    {
      if((pid = waitpid(pid, &status, 0)) == -1)
      {
        perror("waitpid");
      
        return false;
      }

      return true;
    }
  }
}

