/
// Created by Xiao Ma on 10/15/21.
//

#include <stdio.h>
#include <libgen.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/wait.h>
#include <signal.h>

#define MAXPATH 1001

// jobsList struct
typedef struct suspendProcess
{
   char jcmd[MAXPATH];
   pid_t jpid[MAXPATH];
   int pipeJobNum;
} sps;

sps jobsList[100];

int jobNum = 0;
int pipeJobNum = 0;
int pidChild[MAXPATH];
char cmdline[MAXPATH];
int pipeTotal = 1;
char currentDirectory[MAXPATH];

void shell_signal_handler()
{
   signal(SIGINT, SIG_IGN);
   signal(SIGQUIT, SIG_IGN);
   signal(SIGTERM, SIG_IGN);
   signal(SIGTSTP, SIG_IGN);
   // write(1, "\n", 1);
}
void program_signal_handler()
{
   signal(SIGINT, SIG_DFL);
   signal(SIGQUIT, SIG_DFL);
   signal(SIGTERM, SIG_DFL);
   signal(SIGTSTP, SIG_DFL);

   // continue;
}

int add_job(pid_t pid)
{
   // printf("line is :%s\n",cmdline); 
   if(strlen(cmdline) == 0){
      return -1;
   }
   jobsList[jobNum].jpid[pipeJobNum] = pid;
   ++pipeJobNum;
   // printf("total: %d\n", pipeTotal);
   // printf("pipejob: %d\n", pipeJobNum);
   // printf("pipejob: %d\n", jobNum);
   if (pipeJobNum == pipeTotal)
   {
      strcpy(jobsList[jobNum].jcmd, cmdline);
      jobsList[jobNum].pipeJobNum = pipeJobNum;
      // for (int i = 0; i < pipeJobNum; i++)
      // {
      //    printf("pipe: %d\n", jobsList[jobNum].jpid[i]);
      // }
      // printf("pipe: %d\n", jobsList[jobNum].pipeJobNum);
      ++jobNum;
      pipeTotal = 1;
      pipeJobNum = 0;
      // strcpy(cmdline, "");
   }
   return 0;
}

int resume_terminate_job(pid_t pid)
{
   int jobIdx = -1;
   for (int i = 0; i < jobNum; i++)
   {
      for (int j = 0; j < jobsList[j].pipeJobNum; j++)
      {
         if (jobsList[i].jpid[j] == pid)
         {
            jobIdx = i;
            break;
         }
      }
   }
   if (jobIdx == -1)
   {
      return -1;
   }
   for (int i = jobIdx; i < jobNum; i++)
   {
      for (int j = 0; j < jobsList[i + 1].pipeJobNum; j++)
      {
         jobsList[i].jpid[j] = jobsList[i + 1].jpid[j];
      }
      strcpy(jobsList[i].jcmd, jobsList[i + 1].jcmd);
      jobsList[i].pipeJobNum = jobsList[i + 1].pipeJobNum;
   }
   jobNum--;
   jobsList[jobNum].pipeJobNum = 0;
   strcpy(jobsList[jobNum].jcmd, "");
   for (int i = 0; i < jobsList[jobNum].pipeJobNum; i++)
   {
      jobsList[jobNum].jpid[i] = 0;
   }
   return 0;
}

void print_job()
{
   // printf("%s","print_job");
   // printf("%d\n",jobNum);
   // printf("%sprint: ",jobsList[1].jcmd);
   for (int i = 0; i < jobNum; i++)
   {
      fprintf(stdout, "[%d] %s\n", i + 1, jobsList[i].jcmd);
      fflush(stdout);
   }
   // fprintf(stdin, "[%d] %s\n", i, jobsList[0].jcmd);
}

void wait_program_pid(pid_t pid)
{
   // refer : https://stackoverflow.com/questions/279729/how-to-wait-until-all-child-processes-called-by-fork-complete
   int status;
   pid_t ret = waitpid(pid, &status, WUNTRACED);
   if(ret == -1){
      fprintf(stderr, "Error: waitpid return error -1\n");
      exit(-1);
   }
   if (WIFCONTINUED(status))
   {
      // resumes a process
      // printf("%s\n", "resume");
      resume_terminate_job(pid);
   }
   else if (WIFSTOPPED(status))
   {
      // stop a process

      // fprintf(stdout,"%s\n","   suspended  ");
      // fflush(stdout);
      add_job(pid);
   }
   else if (WIFSIGNALED(status))
   {
      // terminate a process
      // move_job(pid);
      // printf("%s\n", "terminate");
      resume_terminate_job(pid);
   }
   fflush(stdout);
}

int is_pipe(char *argv[])
{
   int i = 0;
   while (argv[i] != NULL)
   {
      if (strcmp(argv[i], "|") == 0)
      {
         return i + 1; //i is the position of "|"，i+1 is the next command
      }
      ++i;
   }
   return 0;
}
int count_pipe(char *argv[])
{
   int i = 0;
   int j = 0;
   while (argv[i] != NULL)
   {
      if (strcmp(argv[i], "|") == 0)
      {
         ++j; //i is the position of "|"，i+1 is the next command
      }
      ++i;
   }
   return j;
}
int is_input_redirect(char *argv[])
{
   int i = 0;
   while (argv[i] != NULL)
   {
      if (strcmp(argv[i], "<") == 0)
      {
         return 1; //have input redirection
      }
      ++i;
   }
   return 0;
}
int is_output_redirect(char *argv[])
{
   int i = 0;
   while (argv[i] != NULL)
   {
      if (strcmp(argv[i], ">") == 0 || strcmp(argv[i], ">>") == 0)
      {
         return 1; //have input redirection
      }
      ++i;
   }
   return 0;
}
void parse_pipe(char *input[], char *output1[], char *output2[])
{
   int i = 0;
   int size1 = 0;
   int size2 = 0;
   int ret = is_pipe(input); //ret是input数组中管道"|"的下一个位置

   while (strcmp(input[i], "|") != 0)
   {
      output1[size1++] = input[i++];
   }
   output1[size1] = NULL; //将分割出来的两个char*数组都以NULL结尾

   int j = ret; //j指向管道的后面那个字符
   while (input[j] != NULL)
   {
      output2[size2++] = input[j++];
   }
   output2[size2] = NULL;
}
int check_pipe_null(int ret, char *arry[])
{
   if (arry[ret] == NULL)
   {
      fprintf(stderr, "Error: invalid command\n");
      return -1;
      //  exit(-1);
   }
   return 0;
}
int check_redirect(int argc, int len, char *argv[], int countcmd, int donecmd)
{
   if (donecmd + 1 == countcmd)
   {
      // int ipt = is_input_redirect(argv);
      // printf("%s","pl");
      if (is_input_redirect(argv) != 0)
      {
         // printf("%s","ok");
         // fprintf(stderr, "Error: invalid command\n");
         return -1;
         // exit(-1);
      }
   }
   else
   {
      char **rdtpre = (char **)malloc(sizeof(char *) * (argc + 1));
      char **rdtpost = (char **)malloc(sizeof(char *) * (len + 1));
      parse_pipe(argv, rdtpre, rdtpost);

      if (donecmd == 0)
      {
         if (is_output_redirect(rdtpre) != 0)
         {
            // fprintf(stderr, "Error: invalid commandss\n");
            return -1;
            // exit(-1);
         }
      }
      else
      {
         if (is_output_redirect(rdtpre) != 0 || is_input_redirect(rdtpre) != 0)
         {
            // fprintf(stderr, "Error: invalid command\n");
            return -1;
            // exit(-1);
         }
      }
      int rets = is_pipe(rdtpost);
      check_redirect(rets, len - rets, rdtpost, countcmd, donecmd + 1);
      free(rdtpre);
      rdtpre = NULL;
      free(rdtpost);
      rdtpost = NULL;
   }
   return 0;
}

int last_pipe(char *argv[])
{
   int i = 0;
   int j = 0;
   while (argv[i] != NULL)
   {
      if (strcmp(argv[i], "|") == 0)
      {
         j = i;
      }
      ++i;
   }
   return j;
}

int check_pipe_wrongCmd(char *argv[])
{
   int first = is_pipe(argv);
   int last = last_pipe(argv);
   // printf("%d\n", first);
   // printf("%d\n", last);
   char **firstlist = (char **)malloc(sizeof(char *) * (MAXPATH));
   char **lastlist = (char **)malloc(sizeof(char *) * (MAXPATH));
   char **mediumlist = (char **)malloc(sizeof(char *) * (MAXPATH));
   int j = last;
   int size1 = 0;
   int size2 = 0;
   int size3 = 0;
   int i = 0;
   if (first == last)
   {
      while (strcmp(argv[i], "|") != 0)
      {
         firstlist[size1++] = argv[i++];
      }

      while (argv[j] != NULL)
      {
         lastlist[size3++] = argv[j++];
      }
      mediumlist[0] = argv[first - 1];
   }
   else
   {
      while (strcmp(argv[i], "|") != 0)
      {
         firstlist[size1++] = argv[i++];
      }
      // printf("%d",first);
      // printf("%d",last);
      int medium = last - first;
      for (int i = 0; i < medium; i++)
      {
         mediumlist[i] = argv[first + i];
      }

      while (argv[j] != NULL)
      {
         lastlist[size3++] = argv[j++];
      }
   }
   // printf("%s\n", firstlist[0]);

   // printf("%s\n", mediumlist[0]);
   // printf("%s\n", mediumlist[1]);
   // printf("%s\n", mediumlist[2]);

   if (argv[first] == NULL)
   {
      // fprintf(stderr, "Error: invalid commandss\n");
      return -1;
   }
   else if (is_output_redirect(firstlist) != 0)
   {
      // fprintf(stderr, "Error: invalid commandss\n");
      return -1;
      // exit(-1);
   }
   else if (is_input_redirect(lastlist) != 0)
   {
      // printf("%s","ok");
      // fprintf(stderr, "Error: invalid command\n");
      return -1;
      // exit(-1);
   }
   else if (is_output_redirect(mediumlist) != 0 || is_input_redirect(mediumlist) != 0)
   {
      // fprintf(stderr, "Error: invalid command\n");
      return -1;
      // exit(-1);
   }
   free(firstlist);
   free(mediumlist);
   free(lastlist);
   firstlist = NULL;
   mediumlist = NULL;
   lastlist = NULL;
   return 0;
}

void do_simple_cmd(const char *command, char *argv[])
{
   // refer to : https://blog.csdn.net/zxy131072/article/details/107934621
   // printf("%s\n", "Error: invalid >");
   for (int i = 0; argv[i] != NULL; ++i)
   {
      if (strcmp(argv[i], ">") == 0)
      { //output overwrite redirection
         // printf("%s\n", "Error: invalid >");
         if (argv[i + 1] == NULL)
         {
            fprintf(stderr, "Error: invalid command\n");
            // return -1;
            exit(-1);
         }
         else if (argv[i + 2] != NULL && strcmp(argv[i + 2], "<") != 0)
         {
            fprintf(stderr, "Error: invalid command\n");
            // return -1;
            exit(-1);
         }
         argv[i] = NULL;
         int fd = open(argv[i + 1], O_RDWR | O_CREAT | O_TRUNC, 0664);
         if (fd == -1)
         {
            fprintf(stderr, "Error: invalid command\n");
            // return -1;
            exit(-1);
         }
         dup2(fd, 1);
         fflush(stdout);
         close(fd);
      }
      else if (strcmp(argv[i], ">>") == 0)
      { //output append redirection
         // printf("%s\n", "Error: invalid >>");
         if (argv[i + 1] == NULL)
         {
            fprintf(stderr, "Error: invalid command\n");
            // return -1;
            exit(-1);

         }
         else if (argv[i + 2] != NULL && strcmp(argv[i + 2], "<") != 0)
         {
            fprintf(stderr, "Error: invalid command\n");
            // return -1;
            exit(-1);
         }
         argv[i] = NULL;
         int fd = open(argv[i + 1], O_RDWR | O_CREAT | O_APPEND, 0664);
         if (fd == -1)
         {
            fprintf(stderr, "Error: invalid file\n");
            // return -1;
            exit(-1);
         }
         dup2(fd, 1);
         fflush(stdout);
         close(fd);
      }
      else if (strcmp(argv[i], "<") == 0)
      { //input redirection
         // printf("%s\n", "Error: invalid <");
         if (argv[i + 1] == NULL)
         {
            fprintf(stderr, "Error: invalid command\n");
            // return -1;
            exit(-1);
         }
         else if (argv[i + 2] != NULL && strcmp(argv[i + 2], "|") != 0 && strcmp(argv[i + 2], ">") != 0 && strcmp(argv[i + 2], ">>") != 0)
         {
            fprintf(stderr, "Error: invalid command\n");
            // return -1;
            exit(-1);
         }
         argv[i] = NULL;
         int fd = open(argv[i + 1], O_RDONLY, 0664);
         if (fd == -1)
         {
            fprintf(stderr, "Error: invalid file\n");
            // return -1;
            exit(-1);
         }
         dup2(fd, 0);
         fflush(stdin);
         close(fd);
      }
      else if (strcmp(argv[i], "<<") == 0)
      {
         fprintf(stderr, "Error: invalid command\n");
         // return -1;
         exit(-1);
      }
   }
   execvp(command, argv);
   fprintf(stderr, "Error: invalid program\n");
   exit(-1);
}

void my_system(const char *command, char *argv[])
{
   int pid = fork();
   if (pid == 0)
   {
      program_signal_handler();
      do_simple_cmd(command, argv);
      // return -1;
      // exit(-1);
   }
   // waitpid(pid, NULL, WUNTRACED);
   // pidChild[0] = pid;
   wait_program_pid(pid);
   // exit(-1);
   // wait(NULL);
   // return 0;
}
void parse_program_cmd(char **arry, int type)
{
   // printf("%s\n", arry[0]);
   // printf("%s\n", arry[1]);
   // char s[100] = "/bin/";
   // strcat(s, arry[0]);
   // char s2[100] = "/usr/bin/";
   // strcat(s2, arry[0]);
   char s[100];
   strcpy(s, arry[0]);
   if (type == 1)
   {
      do_simple_cmd(s, arry);
   }
   else if (type == 0)
   {
      my_system(s, arry);
   }
   // if (access(s, 0) == 0)
   // {
   //    // printf("is %d\n",type);
   //    // arry[0] = s;
   //    if (type == 1)
   //    {
   //       do_simple_cmd(s, arry);
   //    }
   //    else if (type == 0)
   //    {
   //       my_system(s, arry);
   //    }
   // }
   // else if (access(s2, 0) == 0)
   // {
   //    arry[0] = s2;
   //    if (type == 1)
   //    {
   //       do_simple_cmd(s2, arry);
   //    }
   //    else if (type == 0)
   //    {
   //       my_system(s2, arry);
   //    }
   // }
   else
   {
      fprintf(stderr, "Error: invalid program\n");
   }
}

int do_pipe_cmd(int argc, int len, char *argv[], int ffd, int countcmd, int donecmd)
{
   // refer to : https://blog.csdn.net/weixin_30487201/article/details/96400688?spm=1001.2014.3001.5502
   // refer to : http://www.cs.loyola.edu/~jglenn/702/S2005/Examples/dup2.html
   // refer to : https://blog.csdn.net/xu1105775448/article/details/80311274

   // if(donecmd + 1 == countcmd){
   //    if(is_input_redirect(argv) != 0){
   //       fprintf(stderr, "Error: invalid command\n");
   //    }
   // }
   pipeTotal = countcmd;
   pid_t pgid;
   if (donecmd + 1 == countcmd) //Is last cmd
   {
      // printf("%s","last done");
      // printf("%d",is_input_redirect(argv));
      int pid = fork();
      if (pid < 0)
      {
         fprintf(stderr, "Error:fork() fail\n");
         // return -1;
         exit(-1);
      }
      else if (pid == 0)
      {
         // setpgid(0, pgid);
         program_signal_handler();
         // close(pipefd[1]);

         dup2(ffd, 0);
         parse_program_cmd(argv, 1);
         // return -1;
         exit(-1);
      }
      // waitpid(pid, NULL, WUNTRACED);
      // setpgid(pid, pgid);
      // pidChild[pipeJobNum] = pid;
      // pipeJobNum++;
      // printf("last : %d\n", pid);
      wait_program_pid(pid);
   }
   else
   {
      pid_t pid;
      int pipefd[2];
      char **pipepre = (char **)malloc(sizeof(char *) * (argc + 1));
      char **pipepost = (char **)malloc(sizeof(char *) * (len + 1));
      parse_pipe(argv, pipepre, pipepost);
      pipe(pipefd);

      // if(donecmd == 0){
      //    if(is_output_redirect(pipepre) != 0){
      //       fprintf(stderr, "Error: invalid command\n");
      //       exit(-1);
      //    }
      // }else{
      //    if(is_output_redirect(pipepre) != 0 || is_input_redirect(pipepre) != 0){
      //       fprintf(stderr, "Error: invalid command\n");
      //       exit(-1);
      //    }
      // }

      pid = fork();

      if (pid < 0)
      {
         fprintf(stderr, "Error:fork() fail\n");
         // return -1;
         exit(-1);
      }
      else if (pid == 0)
      {
         // setpgid(0, pgid);
         program_signal_handler();
         close(pipefd[0]);
         dup2(ffd, STDIN_FILENO);
         dup2(pipefd[1], STDOUT_FILENO);
         parse_program_cmd(pipepre, 1);
         // exit(-1);
      }
      else
      {
         int rets = is_pipe(pipepost);

         close(pipefd[1]);
         if (donecmd != 0)
         {
            close(ffd);
         }
         do_pipe_cmd(rets, len - rets, pipepost, pipefd[0], countcmd, donecmd + 1);
         // setpgid(pid, pgid);
         // pidChild[pipeJobNum] = pid;
         // pipeJobNum++;
         // printf("first : %d\n", pid);
         // waitpid(pid, NULL, WUNTRACED);

         wait_program_pid(pid);
      }

      free(pipepre);
      pipepre = NULL;
      free(pipepost);
      pipepost = NULL;
   }
   return 0;
}

// void split_cmd(int argc, int len, char *argv[])
// {
//    char **fds = argv;
//    char **pipepre = (char **)malloc(sizeof(char *) * (argc + 1));
//    for (int i = 0; i < argc; i++)
//    {
//       pipepre[i] = fds[i];
//    }
//    char **pfds = argv + argc + 1;
//    char **pipepost = (char **)malloc(sizeof(char *) * (len + 1));
// }

void prompt()
{
   char pathbuf[MAXPATH];                           //path buffer
   char *path = getcwd(pathbuf, sizeof(pathbuf));   //get current directory
   // printf("%s\n", pathbuf);
   strcpy(currentDirectory, pathbuf);
   fprintf(stdout, "[nyush %s]$ ", basename(path)); //prompt
   fflush(stdout);
}

void parse_cd_cmd(int len, char **arry)
{
   if (len > 2 || len == 1)
   {
      fprintf(stderr, "Error: invalid command\n");
   }
   else
   {
      int chd = chdir(arry[1]);
      if (chd != 0)
      {
         fprintf(stderr, "Error: invalid directory\n");
      }
      // strcpy(cmdline, "");
   }
}

void parse_exit_cmd(int len)
{
   if (len > 1)
   {
      fprintf(stderr, "Error: invalid command\n");
   }
   else
   {
      if (jobNum != 0)
      {
         fprintf(stderr, "Error: there are suspended jobs\n");
         fflush(stdout);
      }
      else
      {
         exit(0);
      }
      
   }
}

void parse_jobs_cmd(int len, char **arry)
{
   if (len > 1)
   {
      fprintf(stderr, "Error: invalid command\n");
   }
   else
   {
      // printf("ok");
      // strcpy(cmdline, "");
      print_job();
      // printf("end");
      // list all suspend jobs
   }
}
void parse_fg_cmd(int len, char **arry)
{
   if (len > 2 || len == 1)
   {
      fprintf(stderr, "Error: invalid command\n");
   }
   else
   {
      int jobIdx = atoi(arry[1]);
      // printf("idx: %d\n",jobIdx);
      if (jobNum < jobIdx)
      {
         fprintf(stderr, "Error: invalid job\n");
      }
      else
      {
         pid_t pid[MAXPATH];
         for (int i = 0; i < jobsList[jobIdx - 1].pipeJobNum; i++)
         {
            pid[i] = jobsList[jobIdx - 1].jpid[i];
            // printf("fg: %d\n", pid[i]);
         }
         strcpy(cmdline, "");
         for (int i = 0; i < jobsList[jobIdx - 1].pipeJobNum; i++)
         {

            kill(pid[i], SIGCONT);
         }
         for (int i = 0; i < jobsList[jobIdx - 1].pipeJobNum; i++)
         {
            wait_program_pid(pid[i]);
         }
         
      }
   }
}

int main()
{
   setenv("PATH", "/bin:/usr/bin", 1);
   // setenv("PATH", "/bin:/usr/bin:", 1);
   
   shell_signal_handler();

   while (1)
   {

      char **arry = (char **)malloc(sizeof(char *) * (MAXPATH));
      fflush(stdout);
      prompt();
      char instbuf[MAXPATH];
      if (fgets(instbuf, MAXPATH, stdin) == NULL)
      {
         fprintf(stdout, "\n"); //prompt
         fflush(stdout);
         exit(0);
      }
      // fgets(instbuf, MAXPATH, stdin);
      instbuf[strlen(instbuf) - 1] = '\0';
      strcpy(cmdline, instbuf);
      char *strk; //split the string
      char *sp;   //strtok_r pointer sp save the remain string
      strk = strtok_r(instbuf, " ", &sp);
      int i = 0;
      int len = 0;
      while (strk != NULL)
      {
         arry[i] = strk;
         i++;
         len++;
         strk = strtok_r(NULL, " ", &sp);
      }
      arry[len] = NULL;
      
      if (len == 0)
      {
         continue;
      }
      else if (strcmp(arry[0], ">") == 0 || strcmp(arry[0], ">>") == 0 || strcmp(arry[0], "<") == 0 || strcmp(arry[0], "|") == 0)
      {
         fprintf(stderr, "Error: invalid command\n");
         continue;
      }
      else if (strcmp(arry[0], "exit") == 0)
      { //exit command
         parse_exit_cmd(len);
      }
      else if (strcmp(arry[0], "cd") == 0)
      { //cd command
         parse_cd_cmd(len, arry);
      }
      else if (strcmp(arry[0], "jobs") == 0)
      { //jobs command
         // printf("%s","job-entry");
         parse_jobs_cmd(len, arry);
      }
      else if (strcmp(arry[0], "fg") == 0)
      { //cd command
         parse_fg_cmd(len, arry);
      }

      else
      { //program command

         // check_pipe_null(ret, arry);
         // check_redirect(ret, len - ret, arry, countcmd, 0);
         int ret = is_pipe(arry);
         int countpipe = count_pipe(arry);
         int countcmd = countpipe + 1;
         int donecmd = 0;
         if (ret != 0)
         {
            if (check_pipe_wrongCmd(arry) != 0)
            {
               fprintf(stderr, "Error: invalid command\n");
               continue;
            }
            do_pipe_cmd(ret, len - ret, arry, STDIN_FILENO, countcmd, 0);
         }
         else
         {
            // int crtpgm = access(arry[0],0);
            // if(crtpgm == 0){

            // }
            // char *p = arry[0];
            // if(p[0] != '.' && p[1] != '/'){
            //    // printf("%s\n", "Error: invalid >");
            //    // printf("%s\n",currentDirectory);
            //    char s[100] = ".";
            //    strcat(s,currentDirectory);
            //    strcat(s,arry[0]);
            //    printf("%s\n", s);
            //    // currentDirectory
            // }
            // printf("%s\n", "Error: invalid >");
            parse_program_cmd(arry, 0);
         }
      }
      free(arry);

      arry = NULL;
   }
}
