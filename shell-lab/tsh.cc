// 
// tsh - A tiny shell program with job control
// 
// <Aanvi Guliani>
// <aagu5057>

using namespace std;

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <ctype.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <errno.h>
#include <string>

#include "globals.h"
#include "jobs.h"
#include "helper-routines.h"

//
// Needed global variable definitions
//

static char prompt[] = "tsh> ";
int verbose = 0;

//
// You need to implement the functions eval, builtin_cmd, do_bgfg,
// waitfg, sigchld_handler, sigstp_handler, sigint_handler
//
// The code below provides the "prototypes" for those functions
// so that earlier code can refer to them. You need to fill in the
// function bodies below.
// 

void eval(char *cmdline);
int builtin_cmd(char **argv);
void do_bgfg(char **argv);
void waitfg(pid_t pid);

void sigchld_handler(int sig);
void sigtstp_handler(int sig);
void sigint_handler(int sig);

//
// main - The shell's main routine 
//
int main(int argc, char **argv) 
{
  int emit_prompt = 1; // emit prompt (default)

  //
  // Redirect stderr to stdout (so that driver will get all output
  // on the pipe connected to stdout)
  //
  dup2(1, 2);

  /* Parse the command line */
  char c;
  while ((c = getopt(argc, argv, "hvp")) != EOF) {
    switch (c) {
    case 'h':             // print help message
      usage();
      break;
    case 'v':             // emit additional diagnostic info
      verbose = 1;
      break;
    case 'p':             // don't print a prompt
      emit_prompt = 0;  // handy for automatic testing
      break;
    default:
      usage();
    }
  }

  //
  // Install the signal handlers
  //

  //
  // These are the ones you will need to implement
  //
  Signal(SIGINT,  sigint_handler);   // ctrl-c
  Signal(SIGTSTP, sigtstp_handler);  // ctrl-z
  Signal(SIGCHLD, sigchld_handler);  // Terminated or stopped child

  //
  // This one provides a clean way to kill the shell
  //
  Signal(SIGQUIT, sigquit_handler); 

  //
  // Initialize the job list
  //
  initjobs(jobs);

  //
  // Execute the shell's read/eval loop
  //
  for(;;) {
    //
    // Read command line
    //
    if (emit_prompt) {
      printf("%s", prompt);
      fflush(stdout);
    }

    char cmdline[MAXLINE];

    if ((fgets(cmdline, MAXLINE, stdin) == NULL) && ferror(stdin)) {
      app_error("fgets error");
    }
    //
    // End of file? (did user type ctrl-d?)
    //
    if (feof(stdin)) {
      fflush(stdout);
      exit(0);
    }

    //
    // Evaluate command line
    //
    eval(cmdline);
    fflush(stdout);
    fflush(stdout);
  } 

  exit(0); //control never reaches here
}
  
/////////////////////////////////////////////////////////////////////////////
//
// eval - Evaluate the command line that the user has just typed in
// 
// If the user has requested a built-in command (quit, jobs, bg or fg)
// then execute it immediately. Otherwise, fork a child process and
// run the job in the context of the child. If the job is running in
// the foreground, wait for it to terminate and then return.  Note:
// each child process must have a unique process group ID so that our
// background children don't receive SIGINT (SIGTSTP) from the kernel
// when we type ctrl-c (ctrl-z) at the keyboard.
//
void eval(char *cmdline) 
{
  /* Parse command line */
  //
  // The 'argv' vector is filled in by the parseline
  // routine below. It provides the arguments needed
  // for the execve() routine, which you'll need to
  // use below to launch a process.
  //
  char *argv[MAXARGS];
  
  pid_t pid;
  struct job_t* j;
  
  //
  // The 'bg' variable is TRUE if the job should run
  // in background mode or FALSE if it should run in FG
  //
  int bg = parseline(cmdline, argv); 
  if (argv[0] == NULL)  
    return;   /* ignore empty lines */
  
  sigset_t sigMask;
  
  
  if(!builtin_cmd(argv)){ //if not a built in command, fork and execute child process, else go to builtin_cmd function
    pid = fork(); //fork process
    setpgid(0,0); //set group id for fg processes so it doesn't associate w/ bg processes
    if(pid == 0){ //if fork returned 0, we are in child process
      execv(argv[0],argv); //create child process, pid doesn't change (execv replaces any program executing corrently with new program image)
      printf("%s: command not found \n",argv[0]); //if the command entered isn't found when exec, exit to avoid running multiple instances of shell
      exit(0); //exit
    }
    else{ //if parent
      if(!bg){ //if we are in FG process
        addjob(jobs,pid,FG,cmdline); //add a job to the struct with state, fg
        waitfg(pid); //wait for child process and reap it to avoid zombie processes
      }
      else{
        addjob(jobs,pid,BG,cmdline); //add job to struct with state bg
        j = getjobpid(jobs,pid); //get job id of recently added job
        printf("[%d] (%d %s)",j->jid,pid,cmdline);
      }
    }
    sigprocmask(SIG_UNBLOCK,&sigMask,NULL); //unblock any signals we blocked before added jobs

  }

  return;
}



/////////////////////////////////////////////////////////////////////////////
//
// builtin_cmd - If the user has typed a built-in command then execute
// it immediately. The command name would be in argv[0] and
// is a C string. We've cast this to a C++ string type to simplify
// string comparisons; however, the do_bgfg routine will need 
// to use the argv array as well to look for a job number.
//
int builtin_cmd(char **argv) 
{
  string cmd(argv[0]);
  
  if (cmd == "quit"){ //buit in command -> quit
    exit(0);
  }
  
  else if (cmd == "fg" || cmd == "bg"){ //do bgfg if command is fg or bg
    do_bgfg(argv);
    return 1;
  }
  
  else if (cmd == "jobs"){ //go to list jobs with command jobs
    listjobs(jobs);
    return 1;
  }
  return 0;     /* not a builtin command */
  
}

/////////////////////////////////////////////////////////////////////////////
//
// do_bgfg - Execute the builtin bg and fg commands
//
void do_bgfg(char **argv) 
{
  struct job_t *jobp=NULL;
    
  /* Ignore command if no argument */
  if (argv[1] == NULL) {
    printf("%s command requires PID or %%jobid argument\n", argv[0]);
    return;
  }
    
  /* Parse the required PID or %JID arg */
  if (isdigit(argv[1][0])) {
    pid_t pid = atoi(argv[1]);
    if (!(jobp = getjobpid(jobs, pid))) {
      printf("(%d): No such process\n", pid);
      return;
    }
  }
  else if (argv[1][0] == '%') {
    int jid = atoi(&argv[1][1]);
    if (!(jobp = getjobjid(jobs, jid))) {
      printf("%s: No such job\n", argv[1]);
      return;
    }
  }	    
  else {
    printf("%s: argument must be a PID or %%jobid\n", argv[0]);
    return;
  }

  //
  // You need to complete rest. At this point,
  // the variable 'jobp' is the job pointer
  // for the job ID specified as an argument.
  //
  // Your actions will depend on the specified command
  // so we've converted argv[0] to a string (cmd) for
  // your benefit.
  //
  string cmd(argv[0]);
  
  if(cmd == "bg"){
    jobp ->state = BG; //change state to bg
    kill(-jobp -> pid, SIGCONT); //run job again
    printf("[%d] (%d) %s", jobp -> jid,jobp -> pid,jobp->cmdline);
  }
  
  else if(cmd == "fg"){
    jobp->state = FG; //change state to fg
    kill(-jobp -> pid, SIGCONT); //run job again
    waitfg(jobp->pid); // wait fg to ensure only one fg process running at a time
  }

  return;
}

/////////////////////////////////////////////////////////////////////////////
//
// waitfg - Block until process pid is no longer the foreground process
//
void waitfg(pid_t pid)
{
  //when pid is an fg pid, don't do anything, stop sleeping when it is not an fg pid
  while(pid == fgpid(jobs)){
    sleep(1);
  }
  return;
}

/////////////////////////////////////////////////////////////////////////////
//
// Signal handlers
//


/////////////////////////////////////////////////////////////////////////////
//
// sigchld_handler - The kernel sends a SIGCHLD to the shell whenever
//     a child job terminates (becomes a zombie), or stops because it
//     received a SIGSTOP or SIGTSTP signal. The handler reaps all
//     available zombie children, but doesn't wait for any other
//     currently running children to terminate.  
//
void sigchld_handler(int sig) 
{
  int oldErr = errno;
  int status;
  sigset_t mask, mask2; // masks to be used for blocking signals
  pid_t pid;
  
  sigfillset(&mask);
  sigprocmask(SIG_BLOCK,&mask,&mask2);
  
  while((pid = waitpid(-1,&status,WNOHANG | WUNTRACED)) > 0){ // while there are child processes to kill
    //-1: set of all processes to kill
    //WNOHANG: if waitpid should return immediately (no child ready)
    //WUNTRACED: status of terminated child processes
    //WNOHANG | WUNTRACED : return if none of the children in wait set have terminated/stopped or with return value equal to pid of stopped or terminated
    
    if(WIFSIGNALED(status)){ // if signal was raised that caused it to exit
      string output = "Job [" + to_string(pid2jid(pid)) + "] (" + to_string(pid) + ") terminated by signal " + to_string(WTERMSIG(status)) + " \n";
      write(STDOUT_FILENO, output.c_str(), output.size()); //use write function instead of printf for asynchronous signal safety
    }
    
    else if (WIFSTOPPED(status)){
      string output = "Job [" + to_string(pid2jid(pid)) + "] (" + to_string(pid) + ") stopped by signal " + to_string(WSTOPSIG(status)) + " \n";
      write(STDOUT_FILENO, output.c_str(), output.size()); //Printing with printf is unsafe
      getjobpid(jobs, pid)->state = ST; //change state to st
      return;
    }
    
    deletejob(jobs,pid);
    sigprocmask(SIG_SETMASK,&mask2,NULL); //signal blocks for race condition
  }
  errno = oldErr;
  return;
}

/////////////////////////////////////////////////////////////////////////////
//
// sigint_handler - The kernel sends a SIGINT to the shell whenver the
//    user types ctrl-c at the keyboard.  Catch it and send it along
//    to the foreground job.  
//
void sigint_handler(int sig) 
{
  pid_t pid = fgpid(jobs);
  if (pid != 0){ //if pid != 0, that means forground process exists
    kill(-pid,SIGINT); //negative pid to kill all processes in group
  }
  return;
}

/////////////////////////////////////////////////////////////////////////////
//
// sigtstp_handler - The kernel sends a SIGTSTP to the shell whenever
//     the user types ctrl-z at the keyboard. Catch it and suspend the
//     foreground job by sending it a SIGTSTP.  
//
void sigtstp_handler(int sig) 
{
  pid_t pid = fgpid(jobs);
  if(pid != 0){ //foreground process found
    kill(-pid, sig); //negative pid to kill process
  }
  return;
}

/*********************
 * End signal handlers
 *********************/




