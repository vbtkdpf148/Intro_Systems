
/*
 * tsh - A tiny shell program with job control
 * Author : Kun Woo Yoo (kunwooy)
 */

#include "tsh_helper.h"

/*
 * If DEBUG is defined, enable contracts and printing on dbg_printf.
 */
#ifdef DEBUG
/* When debugging is enabled, these form aliases to useful functions */
#define dbg_printf(...) printf(__VA_ARGS__)
#define dbg_requires(...) assert(__VA_ARGS__)
#define dbg_assert(...) assert(__VA_ARGS__)
#define dbg_ensures(...) assert(__VA_ARGS__)
#else
/* When debugging is disabled, no code gets generated for these */
#define dbg_printf(...)
#define dbg_requires(...)
#define dbg_assert(...)
#define dbg_ensures(...)
#endif

/* Function prototypes */
void eval(const char *cmdline);
int builtin_command(struct cmdline_tokens *token);
void foreground_processing(pid_t pid);
void waitfg(pid_t pid);
void sigchld_handler(int sig);
void sigtstp_handler(int sig);
void sigint_handler(int sig);
void sigquit_handler(int sig);



/*
 * the main function sets up environment and signals for tsh,
 * then starts running on an infinite loop (while(true))
 * each time setup "tsh> " and waits for input.
 * Then, the input gets interpreted and evaluated.
 */
int main(int argc, char **argv)
{
    char c;
    char cmdline[MAXLINE_TSH];  // Cmdline for fgets
    bool emit_prompt = true;    // Emit prompt (default)

    // Redirect stderr to stdout (so that driver will get all output
    // on the pipe connected to stdout)
    Dup2(STDOUT_FILENO, STDERR_FILENO);

    // Parse the command line
    while ((c = getopt(argc, argv, "hvp")) != EOF)
    {
        switch (c)
        {
        case 'h':                   // Prints help message
            usage();
            break;
        case 'v':                   // Emits additional diagnostic info
            verbose = true;
            break;
        case 'p':                   // Disables prompt printing
            emit_prompt = false;
            break;
        default:
            usage();
        }
    }

    // Create environment variable
    if (putenv("MY_ENV=42") < 0) {
        perror("putenv");
        exit(1);
    }


    // Install the signal handlers
    Signal(SIGINT,  sigint_handler);   // Handles ctrl-c
    Signal(SIGTSTP, sigtstp_handler);  // Handles ctrl-z
    Signal(SIGCHLD, sigchld_handler);  // Handles terminated or stopped child

    Signal(SIGTTIN, SIG_IGN);
    Signal(SIGTTOU, SIG_IGN);

    Signal(SIGQUIT, sigquit_handler);

    // Initialize the job list
    initjobs(job_list);

    // Execute the shell's read/eval loop
    while (true)
    {
        if (emit_prompt)
        {
            printf("%s", prompt);
            fflush(stdout);
        }

        if ((fgets(cmdline, MAXLINE_TSH, stdin) == NULL) && ferror(stdin))
        {
            app_error("fgets error");
        }

        if (feof(stdin))
        {
            // End of file (ctrl-d)
            printf ("\n");
            fflush(stdout);
            fflush(stderr);
            return 0;
        }

        // Remove the trailing newline
        cmdline[strlen(cmdline)-1] = '\0';

        // Evaluate the command line
        eval(cmdline);

        fflush(stdout);
    }

    return -1; // control never reaches here
}


/* Handy guide for eval:
 *
 * If the user has requested a built-in command (quit, jobs, bg or fg),
 * then execute it immediately. Otherwise, fork a child process and
 * run the job in the context of the child. If the job is running in
 * the foreground, wait for it to terminate and then return.
 * Note: each child process must have a unique process group ID so that our
 * background children don't receive SIGINT (SIGTSTP) from the kernel
 * when we type ctrl-c (ctrl-z) at the keyboard.
 */

/*
 * eval takes in command line as its argument, parse the argument,
 * check if the command is builtin (and execute if it is),
 * and create child process and execute the command if not.
 */
void eval(const char *cmdline) {
    sigset_t mask; //mask for SIGCHLD and SIGINT & SIGTSTP
    parseline_return parse_result; // result of parsing command line
    job_state state; // state of the current job
    pid_t pid; // PID of current job
    struct cmdline_tokens token;
    int fd, temp; // file descripters
    // Parse command line
    parse_result = parseline(cmdline, &token);

    if (parse_result == PARSELINE_ERROR || parse_result == PARSELINE_EMPTY)
    {
        return;
    }

    if (!builtin_command(&token)) {
        if (token.argv[0] == NULL) return;

        /* initialize signal set and block SIGCHLD, SIGINT and SIGTSTP */
        Sigemptyset(&mask);
        Sigaddset(&mask, SIGCHLD);
        Sigaddset(&mask, SIGINT);
        Sigaddset(&mask, SIGTSTP);
        Sigprocmask(SIG_BLOCK, &mask, NULL);


        /* setup for I/O redirection */

        /* now create child process */
        if ((pid = fork()) < 0) { // at error
            unix_error("error: fork");
        }

        else if (pid == 0) { // run file in child process
            Sigprocmask(SIG_UNBLOCK, &mask, NULL);
            Setpgid(0,0);

            /* setup I/O redirection before executing */
            if (token.infile) {
                fd = Open(token.infile, O_RDONLY, 0);
                dup2(fd, STDIN_FILENO);
            }
            if (token.outfile) {
                temp = Open(token.outfile, O_RDONLY, 0); // place to save STDOUT
                Dup2(STDOUT_FILENO, temp);
                fd = Open(token.outfile, O_WRONLY, 0);
                Dup2(fd, STDOUT_FILENO);
            }

            if (execve(token.argv[0], token.argv, environ) == -1) {
                printf("%s : Invalid command\n", token.argv[0]);
                exit(0);
            }
        }
         // on parent
        else {
            state = (parse_result == PARSELINE_FG) ? FG : BG;

            if(addjob(job_list, pid, state, cmdline)) {
                /* finally, wait if FG for foreground to finish processing,
                * and print if BG */
                if (parse_result == PARSELINE_FG) {
                    foreground_processing(pid);
                } else {
                    printf("[%d] (%d) %s\n",
                            pid2jid(job_list, pid), pid, cmdline);
                }
            }
            Sigprocmask(SIG_UNBLOCK, &mask, NULL);
        }
    }

    return;
}

/* builtin_command takes in the builtin_state as the argument,
 * processess the command if it is a builtin command and return 1,
 * return 0 otherwise */
int builtin_command(struct cmdline_tokens *token) {
    builtin_state builtin = token->builtin; // builtin state
    int jid;
    pid_t pid;
    int fd; // file descriptor
    sigset_t mask; // mask for blocking signals
    struct job_t *target_job;

    /* for quit */
    if (builtin == BUILTIN_QUIT) exit(0);

    /* for jobs */
    else if (builtin == BUILTIN_JOBS) {

        Sigemptyset(&mask);
        Sigaddset(&mask, SIGCHLD);
        Sigaddset(&mask, SIGINT);
        Sigaddset(&mask, SIGTSTP);
        Sigprocmask(SIG_BLOCK, &mask, NULL);

        /* jobs should support I/O redirection */
        if (token->outfile) {
            fd = Open(token->outfile, O_WRONLY, 0);
            listjobs(job_list, fd);
            Close(fd);
        } else {
            listjobs(job_list, STDOUT_FILENO);
        }

        Sigprocmask(SIG_UNBLOCK, &mask, NULL);
        return 1;
    }

    /* for bg or fg */
    else if (builtin == BUILTIN_FG || builtin == BUILTIN_BG) {
        char *arg = token->argv[1];

        Sigemptyset(&mask);
        Sigaddset(&mask, SIGCHLD);
        Sigaddset(&mask, SIGINT);
        Sigaddset(&mask, SIGTSTP);
        Sigprocmask(SIG_BLOCK, &mask, NULL);

        /* find out pid */
        if (arg[0] == '%') {
            jid = atoi(&arg[1]);
            target_job = getjobjid(job_list, jid);
            pid = target_job->pid;
        }
        else {
            pid = atoi(arg);
            target_job = getjobpid(job_list, pid);
        }
        Kill(-pid, SIGCONT);

        if (builtin == BUILTIN_FG) {
            target_job->state = FG;
            foreground_processing(pid);
        }
        else {
            target_job->state = BG;
        }
        Sigprocmask(SIG_UNBLOCK, &mask, NULL);
        return 1;
    }
    else {
        return 0;
    }
}

/* foreground_processing is implemented to make the foreground wait
 * until the process terminates
 */
void foreground_processing(pid_t pid) {
    sigset_t mask;
    Sigemptyset(&mask);
    Sigaddset(&mask, SIGCHLD);
    Sigaddset(&mask, SIGINT);
    Sigaddset(&mask, SIGTSTP);
    Sigprocmask(SIG_BLOCK, &mask, NULL);

    struct job_t *target = getjobpid(job_list, pid);
    Sigprocmask(SIG_UNBLOCK, &mask, NULL);

    while (target->state == FG) {
        sleep(1);
    }
    return;
}


/*****************
 * Signal handlers
 *****************/

/*
 * sigchld_handler handles sigchld signals by
 * 1. when child stopped by unnoticed signal, changes the state and prints
 * 2. when child terminated by unnoticed signal, reaps the child and prints
 * 3. when child has terminated, reaps the child
 */
void sigchld_handler(int sig) {
    pid_t pid;
    int status;
    sigset_t mask;
    int jid;
    struct job_t *target_job;


    while((pid = waitpid(-1, &status, WUNTRACED | WNOHANG)) > 0) {

        // first setup mask set
        Sigemptyset(&mask);
        Sigaddset(&mask, SIGCHLD);
        Sigaddset(&mask, SIGINT);
        Sigaddset(&mask, SIGTSTP);
        Sigprocmask(SIG_BLOCK, &mask, NULL);

        /* check if the child has stopped or terminated */
        if (WIFSTOPPED(status)) {
            target_job = getjobpid(job_list, pid);
            target_job->state = ST;
            jid = target_job->jid;
            // now print
            Sio_puts("Job [");
            Sio_putl(jid);
            Sio_puts("] (");
            Sio_putl((long) pid);
            Sio_puts(") stopped by signal ");
            Sio_putl((long) WSTOPSIG(status));
            Sio_puts("\n");
        }

        else if (WIFSIGNALED(status)) { // when signaled to terminate unnoticed
            target_job = getjobpid(job_list, pid);
            jid = target_job->jid;
            deletejob(job_list, pid);
            Sio_puts("Job [");
            Sio_putl(jid);
            Sio_puts("] (");
            Sio_putl((long) pid);
            Sio_puts(") terminated by signal ");
            Sio_putl((long) WTERMSIG(status));
            Sio_puts("\n");
        }

        else if (WIFEXITED(status)) { // terminated
            deletejob(job_list, pid);
        }
        Sigprocmask(SIG_UNBLOCK, &mask, NULL);
    }

    return;
}

/*
 * sigint_handler sends SIGINT signal to the shell's foreground job
 * and all children if they exist.
 */
void sigint_handler(int sig) {
    sigset_t mask;
    Sigemptyset(&mask);
    Sigaddset(&mask, SIGCHLD);
    Sigaddset(&mask, SIGINT);
    Sigaddset(&mask, SIGTSTP);
    Sigprocmask(SIG_BLOCK, &mask, NULL);

    pid_t pid = fgpid(job_list);

    if (pid) { // if there is one fg job
        kill(-pid, SIGINT);
    }
    Sigprocmask(SIG_UNBLOCK, &mask, NULL);
    return;
}

/*
 * sigstp_handler sends SIGSTIP signal to the shell's foreground job
 * and all children if they exist.
 */
void sigtstp_handler(int sig) {
    sigset_t mask;
    Sigemptyset(&mask);
    Sigaddset(&mask, SIGCHLD);
    Sigaddset(&mask, SIGINT);
    Sigaddset(&mask, SIGTSTP);
    Sigprocmask(SIG_BLOCK, &mask, NULL);

    pid_t pid = fgpid(job_list);
    if (pid) { // if there is one fg job
        kill(-pid, SIGTSTP);
    }
    Sigprocmask(SIG_UNBLOCK, &mask, NULL);
    return;
}

