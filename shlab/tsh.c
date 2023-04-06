/*
 * tsh - A tiny shell program with job control
 *
 * <배병욱 2017-11621, peardanny@snu.ac.kr>
 *
 */
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <ctype.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <errno.h>

/* Misc manifest constants */
#define MAXLINE    1024   /* max line size */
#define MAXARGS     128   /* max args on a command line */
#define MAXJOBS      16   /* max jobs at any point in time */
#define MAXJID    1<<16   /* max job ID */

/* Job states */
#define UNDEF 0 /* undefined */
#define FG 1    /* running in foreground */
#define BG 2    /* running in background */
#define ST 3    /* stopped */

/*
 * Jobs states: FG (foreground), BG (background), ST (stopped)
 * Job state transitions and enabling actions:
 *     FG -> ST  : ctrl-z
 *     ST -> FG  : fg command
 *     ST -> BG  : bg command
 *     BG -> FG  : fg command
 * At most 1 job can be in the FG state.
 */

/* Global variables */
extern char **environ;      /* defined in libc */
char prompt[] = "tsh> ";    /* command line prompt (DO NOT CHANGE) */
int verbose = 0;            /* if true, print additional output */
int nextjid = 1;            /* next job ID to allocate */
char sbuf[MAXLINE];         /* for composing sprintf messages */

struct job_t {              /* The job struct */
    pid_t pid;              /* job PID */
    int jid;                /* job ID [1, 2, ...] */
    int state;              /* UNDEF, BG, FG, or ST */
    char cmdline[MAXLINE];  /* command line */
};
struct job_t jobs[MAXJOBS]; /* The job list */
/* End global variables */


/* Function prototypes */

/* Here are the functions that you will implement */
void eval(char *cmdline);
int builtin_cmd(char **argv);
void do_bgfg(char **argv);
void waitfg(pid_t pid);

void sigchld_handler(int sig);
void sigtstp_handler(int sig);
void sigint_handler(int sig);

/* Here are helper routines that we've provided for you */
int parseline(const char *cmdline, char **argv);
void sigquit_handler(int sig);

void clearjob(struct job_t *job);
void initjobs(struct job_t *jobs);
int maxjid(struct job_t *jobs);
int addjob(struct job_t *jobs, pid_t pid, int state, char *cmdline);
int deletejob(struct job_t *jobs, pid_t pid);
pid_t fgpid(struct job_t *jobs);
struct job_t *getjobpid(struct job_t *jobs, pid_t pid);
struct job_t *getjobjid(struct job_t *jobs, int jid);
int pid2jid(pid_t pid);
void listjobs(struct job_t *jobs);

void usage(void);
void unix_error(char *msg);
void app_error(char *msg);
typedef void handler_t(int);
handler_t *Signal(int signum, handler_t *handler);

/*
 * main - The shell's main routine
 */
int main(int argc, char **argv)
{
    char c;
    char cmdline[MAXLINE];
    int emit_prompt = 1; /* emit prompt (default) */

    /* Redirect stderr to stdout (so that driver will get all output
     * on the pipe connected to stdout) */
    dup2(1, 2);

    /* Parse the command line */
    while ((c = getopt(argc, argv, "hvp")) != EOF) {
        switch (c) {
        case 'h':             /* print help message */
            usage();
	    break;
        case 'v':             /* emit additional diagnostic info */
            verbose = 1;
	    break;
        case 'p':             /* don't print a prompt */
            emit_prompt = 0;  /* handy for automatic testing */
	    break;
	default:
            usage();
	}
    }

    /* Install the signal handlers */

    /* These are the ones you will need to implement */
    Signal(SIGINT,  sigint_handler);   /* ctrl-c */
    Signal(SIGTSTP, sigtstp_handler);  /* ctrl-z */
    Signal(SIGCHLD, sigchld_handler);  /* Terminated or stopped child */

    /* This one provides a clean way to kill the shell */
    Signal(SIGQUIT, sigquit_handler);

    /* Initialize the job list */
    initjobs(jobs);

    /* Execute the shell's read/eval loop */
    while (1) {

	/* Read command line */
	if (emit_prompt) {
	    printf("%s", prompt);
	    fflush(stdout);
	}
	if ((fgets(cmdline, MAXLINE, stdin) == NULL) && ferror(stdin))
	    app_error("fgets error");
	if (feof(stdin)) { /* End of file (ctrl-d) */
	    fflush(stdout);
	    exit(0);
	}

	/* Evaluate the command line */
	eval(cmdline);
	fflush(stdout);
	fflush(stdout);
    }

    exit(0); /* control never reaches here */
}

/*
 * eval - Evaluate the command line that the user has just typed in
 *
 * If the user has requested a built-in command (quit, jobs, bg or fg)
 * then execute it immediately. Otherwise, fork a child process and
 * run the job in the context of the child. If the job is running in
 * the foreground, wait for it to terminate and then return.  Note:
 * each child process must have a unique process group ID so that our
 * background children don't receive SIGINT (SIGTSTP) from the kernel
 * when we type ctrl-c (ctrl-z) at the keyboard.
*/
void eval(char *cmdline)
{
    char *argv[MAXARGS]; // Argument list execve()
    char buf[MAXLINE];   // buf command
    int bg;              // Represent whether bg of fg, 1 for bg 0 for fg
    pid_t pid;           // process ID
    sigset_t set;        // set of signals, corresponding to bit

    strcpy(buf, cmdline);
    bg = parseline(buf, argv);  // parse input command line into buffer, check whether bg or fg

    if (argv[0] == NULL) return;  // ignore empty lines

    if(!builtin_cmd(argv))  // input is not built in command
    {
        sigemptyset(&set);                   // initialize signal set
        sigaddset(&set, SIGCHLD);            // add SIGCHLD to signal set
        sigprocmask(SIG_BLOCK, &set, NULL);  // add signals in set to blocked
        if((pid = fork()) == 0) // if child runs user job
        {
            setpgid(0, 0);  // children ID is 0
            sigprocmask(SIG_UNBLOCK, &set, NULL);  // add signals in set to unblocked
            if (execve(argv[0], argv, environ) < 0){  //error
                printf("%s: Command not found.\n", argv[0]);  // eg) ./bogus: Command not found
                exit(0);
            }
        }
        if(!bg)  // Parents waits for foreground job to terminate
        {
            if(addjob(jobs, pid, FG, cmdline) > 0) {   // if it's enable to add fg job in job list
                sigprocmask(SIG_UNBLOCK, &set, NULL);  // add signals in set to unblocked
                waitfg(pid);                           // wait until fg job is finished
            }
            else{  // job list is full kill the child
                kill(-pid, SIGINT);
            }
        }
        else
        {
            if(addjob(jobs, pid, BG, cmdline) > 0) {  // if it's enable to add bg job in job list
                sigprocmask(SIG_UNBLOCK, &set, NULL);  // add signals in set to unblocked
                printf("[%d] (%d) %s", pid2jid(pid), (int)pid, cmdline);  // eg) [1] (26256) ./myspin 2 &

            }
            else{  // job list is full kill the child
                kill(-pid, SIGINT);
            }
        }
    }
    return;
}

/*
 * parseline - Parse the command line and build the argv array.
 *
 * Characters enclosed in single quotes are treated as a single
 * argument.  Return true if the user has requested a BG job, false if
 * the user has requested a FG job.
 */
int parseline(const char *cmdline, char **argv)
{
    static char array[MAXLINE]; /* holds local copy of command line */
    char *buf = array;          /* ptr that traverses command line */
    char *delim;                /* points to first space delimiter */
    int argc;                   /* number of args */
    int bg;                     /* background job? */

    strcpy(buf, cmdline);
    buf[strlen(buf)-1] = ' ';  /* replace trailing '\n' with space */
    while (*buf && (*buf == ' ')) /* ignore leading spaces */
	buf++;

    /* Build the argv list */
    argc = 0;
    if (*buf == '\'') {
	buf++;
	delim = strchr(buf, '\'');
    }
    else {
	delim = strchr(buf, ' ');
    }

    while (delim) {
	argv[argc++] = buf;
	*delim = '\0';
	buf = delim + 1;
	while (*buf && (*buf == ' ')) /* ignore spaces */
	       buf++;

	if (*buf == '\'') {
	    buf++;
	    delim = strchr(buf, '\'');
	}
	else {
	    delim = strchr(buf, ' ');
	}
    }
    argv[argc] = NULL;

    if (argc == 0)  /* ignore blank line */
	return 1;

    /* should the job run in the background? */
    if ((bg = (*argv[argc-1] == '&')) != 0) {
	argv[--argc] = NULL;
    }
    return bg;
}

/*
 * builtin_cmd - If the user has typed a built-in command then execute
 *    it immediately.
 */
int builtin_cmd(char **argv)
{
    if(strcmp(argv[0], "jobs") == 0){  // built-in command is jobs
        listjobs(jobs);  // list jobs
        return 1;
    }
    else if((strcmp(argv[0], "bg") == 0) || (strcmp(argv[0], "fg") == 0)){  // built-in command is bg or fg
        do_bgfg(argv);  // if bg change a stopped job to running, if fg change bg job to running fg job
        return 1;
    }
    else if(strcmp(argv[0], "quit") == 0){
        exit(0); // quit shell
    }
    return 0;     /* not a builtin command */
}

/*
 * do_bgfg - Execute the builtin bg and fg commands
 * bg <job> command restarts <job> by sending SIGCONT signal, then runs it in bg.
 * fg <job> command restarts <job> by sending SIGCONT signal, then runs it in fg.
 * <job> can be PID or JID
 */

void do_bgfg(char **argv)
{
    struct job_t *jobptr;

    if (argv[1] == NULL){
        printf("%s command requires PID or %%jobid argument\n", argv[0]);// eg) bg command requires PID or %jobid argument
        return;
    }
    else if (argv[1][0] == '%'){  // prefix % means jobID
        if (!isdigit(argv[1][1])){  // if %(non integer) is input
            printf("%s: argument must be a PID or %%jobid\n", argv[0]); // eg) fg: argument must be a PID or %jobid
            return;
        }
        int jid = atoi(&argv[1][1]);  // change char argv[1][1] to int
        jobptr = getjobjid(jobs, jid);
        if (!jobptr){
            printf("%s: No such job\n", argv[1]);// eg) %2: No such job
            return;
        }
    }
    else if (isdigit(argv[1][0])){  // this means pid
        pid_t pid = atoi(argv[1]);  // change char argv[1] to int
        jobptr = getjobpid(jobs, pid);
        if(!jobptr){
            printf("(%d): No such process\n", pid);// eg) (9999999): No such process
            return;
        }
    }
    else {
        printf("%s: argument must be a PID or %%jobid\n", argv[0]); // eg) fg: argument must be a PID or %jobid
        return;
    }

    // restart job by sending SICONT signal, if there's error kill
    if (kill(-jobptr->pid, SIGCONT) < 0) unix_error("do_bgfg ERROR");

    if (strcmp(argv[0], "fg") == 0){
        jobptr->state = FG;  // change job state to FG
        waitfg(jobptr->pid);  // wait until fg job is done
    }
    else if (strcmp(argv[0], "bg") == 0){
        jobptr->state = BG;  // change job state to BG, no need to wait when it's BG
        printf("[%d] (%d) %s", jobptr->jid, jobptr->pid, jobptr->cmdline);  // eg) [1] (26256) ./myspin 2 &
    }
    return;
}

/*
 * waitfg - Block until process pid is no longer the foreground process
 */
void waitfg(pid_t pid)
{
    while(1){
        if(pid != fgpid(jobs)) break;  // break if fg job is done
        else sleep(1);  // wait until fg job is done
    }
    return;
}

/*****************
 * Signal handlers
 *****************/

/*
 * sigchld_handler - The kernel sends a SIGCHLD to the shell whenever
 *     a child job terminates (becomes a zombie), or stops because it
 *     received a SIGSTOP or SIGTSTP signal. The handler reaps all
 *     available zombie children, but doesn't wait for any other
 *     currently running children to terminate.
 */
void sigchld_handler(int sig)
{
    pid_t pid;
    int status;
    struct job_t *jobptr;
    /*
     * waitpid function
     * Returns: PID of child if OK, 0 (if WNOHANG), or −1 on error
     * WNOHANG: Return immediately (with a return value of 0)
     * if none of the child processes in the wait set has terminated yet.
     * WUNTRACED. Suspend execution of the calling process
     * until a process in the wait set becomes either terminated or stopped.
     * Return the PID of the terminated or stopped child that caused the return.
     * we'll find zombies by using waitpid function.
     */
     while ((pid = waitpid(-1, &status, WNOHANG | WUNTRACED)) > 0){
        // Returns true if the child terminated normally, via a call to exit or a return.
        if (WIFEXITED(status)){
            deletejob(jobs, pid);
        }
        // Returns true if the child process terminated because of a signal that was not caught.
        else if (WIFSIGNALED(status)){
            jobptr = getjobpid(jobs, pid);
            printf("Job [%d] (%d) terminated by signal 2\n", jobptr->jid, jobptr->pid);  // eg) Job [1] (26263) terminated by signal 2
            deletejob(jobs, pid);
        }
        // Returns true if the child that caused the return is currently stopped.
        else if (WIFSTOPPED(status)){
            jobptr = getjobpid(jobs, pid);
            printf("Job [%d] (%d) stopped by signal 20\n", jobptr->jid, jobptr->pid);// eg) Job [2] (26276) stopped by signal 20
            jobptr->state = ST;
        }
     }
    return;
}

/*
 * sigint_handler - The kernel sends a SIGINT to the shell whenever the
 *    user types ctrl-c at the keyboard.  Catch it and send it along
 *    to the foreground job.
 */
void sigint_handler(int sig)
{
    //SIGINT signal to be sent to the current foreground job & it's descendents of job
    pid_t pid = fgpid(jobs);
    if (pid != 0) {	 // if no fg job, do nothing
		if (kill(-pid, sig) < 0) {
			unix_error("sigint ERROR");
		}
	}
    return;
}

/*
 * sigtstp_handler - The kernel sends a SIGTSTP to the shell whenever
 *     the user types ctrl-z at the keyboard. Catch it and suspend the
 *     foreground job by sending it a SIGTSTP.
 */
void sigtstp_handler(int sig)
{
    //SIGSTP signal to be sent to the current foreground job & it's descendents of job
    pid_t pid = fgpid(jobs);
    if (pid != 0) {	 // if no fg job, do nothing
		if (kill(-pid, sig) < 0) {
			unix_error("sigstp ERROR`");
		}
	}
    return;
}

/*********************
 * End signal handlers
 *********************/

/***********************************************
 * Helper routines that manipulate the job list
 **********************************************/

/* clearjob - Clear the entries in a job struct */
void clearjob(struct job_t *job) {
    job->pid = 0;
    job->jid = 0;
    job->state = UNDEF;
    job->cmdline[0] = '\0';
}

/* initjobs - Initialize the job list */
void initjobs(struct job_t *jobs) {
    int i;

    for (i = 0; i < MAXJOBS; i++)
	clearjob(&jobs[i]);
}

/* maxjid - Returns largest allocated job ID */
int maxjid(struct job_t *jobs)
{
    int i, max=0;

    for (i = 0; i < MAXJOBS; i++)
	if (jobs[i].jid > max)
	    max = jobs[i].jid;
    return max;
}

/* addjob - Add a job to the job list */
int addjob(struct job_t *jobs, pid_t pid, int state, char *cmdline)
{
    int i;

    if (pid < 1)
	return 0;

    for (i = 0; i < MAXJOBS; i++) {
	if (jobs[i].pid == 0) {
	    jobs[i].pid = pid;
	    jobs[i].state = state;
	    jobs[i].jid = nextjid++;
	    if (nextjid > MAXJOBS)
		nextjid = 1;
	    strcpy(jobs[i].cmdline, cmdline);
  	    if(verbose){
	        printf("Added job [%d] %d %s\n", jobs[i].jid, jobs[i].pid, jobs[i].cmdline);
            }
            return 1;
	}
    }
    printf("Tried to create too many jobs\n");
    return 0;
}

/* deletejob - Delete a job whose PID=pid from the job list */
int deletejob(struct job_t *jobs, pid_t pid)
{
    int i;

    if (pid < 1)
	return 0;

    for (i = 0; i < MAXJOBS; i++) {
	if (jobs[i].pid == pid) {
	    clearjob(&jobs[i]);
	    nextjid = maxjid(jobs)+1;
	    return 1;
	}
    }
    return 0;
}

/* fgpid - Return PID of current foreground job, 0 if no such job */
pid_t fgpid(struct job_t *jobs) {
    int i;

    for (i = 0; i < MAXJOBS; i++)
	if (jobs[i].state == FG)
	    return jobs[i].pid;
    return 0;
}

/* getjobpid  - Find a job (by PID) on the job list */
struct job_t *getjobpid(struct job_t *jobs, pid_t pid) {
    int i;

    if (pid < 1)
	return NULL;
    for (i = 0; i < MAXJOBS; i++)
	if (jobs[i].pid == pid)
	    return &jobs[i];
    return NULL;
}

/* getjobjid  - Find a job (by JID) on the job list */
struct job_t *getjobjid(struct job_t *jobs, int jid)
{
    int i;

    if (jid < 1)
	return NULL;
    for (i = 0; i < MAXJOBS; i++)
	if (jobs[i].jid == jid)
	    return &jobs[i];
    return NULL;
}

/* pid2jid - Map process ID to job ID */
int pid2jid(pid_t pid)
{
    int i;

    if (pid < 1)
	return 0;
    for (i = 0; i < MAXJOBS; i++)
	if (jobs[i].pid == pid) {
            return jobs[i].jid;
        }
    return 0;
}

/* listjobs - Print the job list */
void listjobs(struct job_t *jobs)
{
    int i;

    for (i = 0; i < MAXJOBS; i++) {
	if (jobs[i].pid != 0) {
	    printf("[%d] (%d) ", jobs[i].jid, jobs[i].pid);
	    switch (jobs[i].state) {
		case BG:
		    printf("Running ");
		    break;
		case FG:
		    printf("Foreground ");
		    break;
		case ST:
		    printf("Stopped ");
		    break;
	    default:
		    printf("listjobs: Internal error: job[%d].state=%d ",
			   i, jobs[i].state);
	    }
	    printf("%s", jobs[i].cmdline);
	}
    }
}
/******************************
 * end job list helper routines
 ******************************/


/***********************
 * Other helper routines
 ***********************/

/*
 * usage - print a help message
 */
void usage(void)
{
    printf("Usage: shell [-hvp]\n");
    printf("   -h   print this message\n");
    printf("   -v   print additional diagnostic information\n");
    printf("   -p   do not emit a command prompt\n");
    exit(1);
}

/*
 * unix_error - unix-style error routine
 */
void unix_error(char *msg)
{
    fprintf(stdout, "%s: %s\n", msg, strerror(errno));
    exit(1);
}

/*
 * app_error - application-style error routine
 */
void app_error(char *msg)
{
    fprintf(stdout, "%s\n", msg);
    exit(1);
}

/*
 * Signal - wrapper for the sigaction function
 */
handler_t *Signal(int signum, handler_t *handler)
{
    struct sigaction action, old_action;

    action.sa_handler = handler;
    sigemptyset(&action.sa_mask); /* block sigs of type being handled */
    action.sa_flags = SA_RESTART; /* restart syscalls if possible */

    if (sigaction(signum, &action, &old_action) < 0)
	unix_error("Signal error");
    return (old_action.sa_handler);
}

/*
 * sigquit_handler - The driver program can gracefully terminate the
 *    child shell by sending it a SIGQUIT signal.
 */
void sigquit_handler(int sig)
{
    printf("Terminating after receipt of SIGQUIT signal\n");
    exit(1);
}

