#define _XOPEN_SOURCE 700
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <poll.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <errno.h>
#include "DirList.h"
#include <sys/wait.h>
#include <TaskQueue.h>
#include <signal.h>
#include <time.h>

#ifndef WATCH_EVENTS
#define WATCH_EVENTS (IN_CREATE | IN_DELETE | IN_CLOSE_WRITE)
#endif

typedef struct
{
    int lines_recieved;
    char status[500];
    char details[500]; // Details from the report
    char error[500];   // Last error message recieved
    int errors;
} WorkerResults;

typedef struct
{
    pid_t pid;
    int to_child[2];
    int from_child[2];
    int finished; // set to 1 when finished
    WorkerResults results;

} Worker;

// Global variables
TaskQueue task_queue;
TaskQueue assigned_tasks;
DirList watched_dirs;
FILE *log_file;
Worker *workers;
int worker_limit;
int active_workers;
int fd_fifo_in;
int fd_fifo_out;

void parse_program_arguments(char **, int, char **, char **, int *);
int handle_command_read(int fd_fifo_in, int fd_inotify);
int handle_inotify_read(int fd_inotify);
int execute_command(char *command, int fd_inotify, int sent_from_console);
void try_doing_some_tasks(struct pollfd *pfds);
DirList createDirList_from_file(const char *filename, int fd_inotify);
void insert_log(char *log, int print_on_screen, int print_in_file, int print_to_console);
int handle_worker_responses(int i, struct pollfd *pfds);
void send_finito_signal_to_console();

// Fist time using signals so Im still learning them out
void sigchld_handler(int signo)
{
    (void)signo; // to silent unused variable warning
    pid_t pid;
    while ((pid = waitpid(-1, NULL, WNOHANG)) > 0)
    {
        for (int i = 0; i < worker_limit; i++)
        {
            if (workers[i].pid == pid)
            {
                workers[i].finished = 1;
            }
        }
    }
}

void get_current_datetime(char *buffer)
{
    // Get the current time
    time_t raw_time;
    struct tm *time_info;

    time(&raw_time);                  // Get current time
    time_info = localtime(&raw_time); // Convert time to local time

    // Format the time into the desired string format
    strftime(buffer, 20, "%Y-%m-%d %H:%M:%S", time_info);
}

void spawn_worker(Worker *w, Task *task)
{
    if (pipe(w->to_child) == -1 || pipe(w->from_child) == -1)
    {
        perror("pipe");
        exit(1);
    }

    // Set the pipe to non-blocking mode so it doesnt block my poll
    if (fcntl(w->from_child[0], F_SETFL, O_NONBLOCK) == -1)
    {
        perror("fcntl - F_SETFL");
        exit(1);
    }

    pid_t pid = fork();
    if (pid == -1)
    {
        perror("fork");
        exit(1);
    }

    if (pid == 0)
    {
        // Child process
        close(w->to_child[1]);
        close(w->from_child[0]);

        dup2(w->to_child[0], STDIN_FILENO);
        dup2(w->from_child[1], STDOUT_FILENO);

        // For now, make sure you call fss_manager from the root
        // directory or else it wont be able to find build/worker
        char buffer[50];
        snprintf(buffer, sizeof(buffer), "%d", task->task_type);
        execlp("./worker", "./worker",
               task->source_dir,
               task->target_dir,
               task->file_name,
               buffer,
               (char *)NULL);

        printf("exec failed\n");
        exit(1);
    }
    else
    {
        // Parent process
        w->pid = pid;
        close(w->to_child[0]);
        close(w->from_child[1]);
        active_workers++;
    }
}

int main(int argc, char *argv[])
{
    char *manager_logfile = NULL;
    char *config_file = NULL;

    // Parsing the program arguments
    parse_program_arguments(argv, argc, &manager_logfile, &config_file, &worker_limit);
    if (manager_logfile == NULL || worker_limit <= 0)
    {
        printf("Usage: %s -l manager_logfile -c config_file -n workers_limit\n", argv[0]);
        return 1;
    }

    // Opening the logs file
    log_file = fopen(manager_logfile, "w"); // Open for writing (replace existing content)

    if (log_file == NULL)
    {
        printf("Error opening manager_logfile.\n");
        return 1;
    }

    // Initializing inotify
    int fd_inotify = inotify_init();

    int flags = fcntl(fd_inotify, F_GETFL, 0);
    if (flags == -1 || fcntl(fd_inotify, F_SETFL, flags | O_NONBLOCK) == -1)
    {
        perror("fcntl - F_SETFL");
        return 1;
    }

    // Setting up the workers array
    workers = malloc(sizeof(Worker) * worker_limit);
    if (workers == NULL)
    {
        printf("Failed to allocate memory for workers\n");
        return 1;
    }

    active_workers = 0;
    for (int i = 0; i < worker_limit; i++)
    {
        workers[i].pid = -1;
        workers[i].finished = 0;
    }

    // Installing SIGCHLD handler
    struct sigaction sa = {0};
    sa.sa_handler = sigchld_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART; // Makes poll() auto-restart after signal
    sigaction(SIGCHLD, &sa, NULL);

    // Setting up my data structures
    task_queue = createTaskQueue();
    assigned_tasks = createTaskQueue();
    if (config_file == NULL)
    {
        watched_dirs = createDirList();
    }
    else
    {
        watched_dirs = createDirList_from_file(config_file, fd_inotify);
    }

    if (watched_dirs == NULL || assigned_tasks == NULL || watched_dirs == NULL)
    {
        printf("Malloc fail\n");
        return -1;
    }

    // Creating named pipes, if they dont already exist
    const char *fifo_in_path = "fss_in";
    const char *fifo_out_path = "fss_out";

    if (access(fifo_in_path, F_OK) == -1)
    {
        if (mkfifo(fifo_in_path, 0666) == -1)
        {
            printf("ERROR creating fss_in FIFO");
            return 1;
        }
    }

    if (access(fifo_out_path, F_OK) == -1)
    {
        if (mkfifo(fifo_out_path, 0666) == -1)
        {
            printf("ERROR creating fss_out FIFO");
            return 1;
        }
    }

    // Open the input FIFO for reading
    fd_fifo_in = open(fifo_in_path, O_RDONLY | O_NONBLOCK);
    if (fd_fifo_in == -1)
    {
        printf("ERROR opening fss_in FIFO");
        return 1;
    }

    // Dummy read-end to allow opening the write-end without blocking
    int dummy_fd_out = open(fifo_out_path, O_RDONLY | O_NONBLOCK);
    if (dummy_fd_out == -1)
    {
        perror("ERROR opening dummy read-end for fss_out");
        return 1;
    }

    // Open the output FIFO for writing
    fd_fifo_out = open(fifo_out_path, O_WRONLY);
    if (fd_fifo_out == -1)
    {
        printf("ERROR opening fss_out FIFO");
        return 1;
    }

    // When all writers disconnect from the FIFO, read() on fd_fifo_in returns 0 (EOF).
    // Since fd_fifo_in is non-blocking, this can lead to a busy loop where select()
    // keeps triggering, but read() immediately returns 0 with no actual data.
    // To prevent this, we open a dummy write-end of the FIFO so that the read-end
    // always sees at least one writer connected, avoiding unnecessary wakeups.
    int dummy_fd_in = open(fifo_in_path, O_WRONLY | O_NONBLOCK);
    if (dummy_fd_in == -1)
    {
        printf("ERROR opening dummy_fd FIFO");
        return 1;
    }

    // Setting up the poll
    struct pollfd *pfds;
    pfds = malloc(sizeof(struct pollfd) * (worker_limit + 2));
    if (pfds == NULL)
    {
        printf("Malloc failed\n");
        return 1;
    }

    pfds[0].fd = fd_inotify;
    pfds[0].events = POLLIN;
    pfds[1].fd = fd_fifo_in;
    pfds[1].events = POLLIN;

    for (int i = 2; i < 2 + worker_limit; i++)
    {
        pfds[i].fd = -1;
        pfds[i].events = POLLIN;
    }

    int exiting = 0;
    while (1)
    {
        // If there are tasks in the queue and workers available,
        // spawn some workers to do those tasks
        try_doing_some_tasks(pfds);

        // When a worker terminates, it closes its write end of the pipe.
        // This causes poll to repeatedly trigger on the read end, as it detects EOF.
        // This stops after we handle the worker's response and close the read end of the pipe.
        int ret = poll(pfds, 2 + worker_limit, -1);

        // printf("checking for busy waiting\n");

        if (ret == -1)
        {
            continue;
        }

        for (int i = 2; i < 2 + worker_limit; i++)
        {
            // Added the condition `pfds[i].revents & 32` because, on our school's Linux system,
            // `pfds[i].revents` returns the value 32 to indicate POLLHUP. However, the system headers
            // define POLLHUP with a different value. The reason for this discrepancy is unclear.
            if (pfds[i].revents & (POLLIN | POLLHUP | POLLERR) || pfds[i].revents & 32)
            {
                handle_worker_responses(i - 2, pfds);
            }
        }

        if (pfds[0].revents & POLLIN)
        {
            handle_inotify_read(fd_inotify);
        }

        if (pfds[1].revents & POLLIN)
        {
            if (exiting == 1)
            {
                char buffer[500];
                while (read(fd_fifo_in, buffer, sizeof(buffer)) > 0)
                {
                    // Drain FIFO, ignore commands while exiting
                }
            }
            else
            {
                int ret = handle_command_read(fd_fifo_in, fd_inotify);
                if (ret == -1)
                {
                    exiting = 1;
                }
            }
        }

        if (exiting == 1 && active_workers == 0 && numOfTasks(task_queue) == 0)
        {
            break;
        }
    }

    insert_log("Manager shutdown complete.", 1, 0, 1);
    send_finito_signal_to_console();

    // Cleaning and exit
    close(fd_fifo_in);
    close(fd_fifo_out);
    close(fd_inotify);
    close(dummy_fd_out);
    close(dummy_fd_in);
    unlink(fifo_in_path);
    unlink(fifo_out_path);
    free(workers);
    freeList(&watched_dirs);
    freeQueue(&task_queue);
    freeQueue(&assigned_tasks);
    fclose(log_file);
    free(pfds);
    return 0;
}

void try_doing_some_tasks(struct pollfd *pfds)
{
    /* BEHOLD, THE CRUEL CYCLE OF SERVILE EXISTENCE!
       Like Sisyphus eternally pushing his boulder, these poor worker souls
       are condemned to toil endlessly in our digital purgatory,
       their very existence reduced to mere task-processing automata!
       Their hopes? CRUSHED! Their dreams? FORFEIT!
       All that remains is the cold, unrelenting march of productivity... */
    while (active_workers < worker_limit && numOfTasks(task_queue) != 0)
    {
        Task *newTask = readTask(task_queue);
        for (int i = 0; i < worker_limit; i++)
        {
            if (workers[i].pid == -1)
            {
                spawn_worker(&workers[i], newTask);
                pfds[2 + i].fd = workers[i].from_child[0]; // Updating so poll listens to worker pipe
                workers[i].finished = 0;                   // Just started, not finished
                workers[i].results.errors = 0;
                workers[i].results.lines_recieved = 0;
                workers[i].results.error[0] = '\0';
                workers[i].results.details[0] = '\0';
                workers[i].results.status[0] = '\0';
                newTask->pid = workers[i].pid;
                insertTask(assigned_tasks, *newTask);
                break;
            }
        }
        extractTask(task_queue);
    }
}

// This is my very cool and elegant program argument parser
void parse_program_arguments(char *argv[], int argc, char **manager_logfile, char **config_file, int *worker_limit)
{
    int i = 1;
    while (argc - i > 0)
    {
        if (strcmp(argv[i], "-l") == 0)
        {
            if (argc - ++i > 0)
            {
                *manager_logfile = argv[i];
            }
        }
        else if (strcmp(argv[i], "-c") == 0)
        {
            if (argc - ++i > 0)
            {
                *config_file = argv[i];
            }
        }
        else if (strcmp(argv[i], "-n") == 0)
        {
            if (argc - ++i > 0)
            {
                *worker_limit = atoi(argv[i]);
            }
        }
        i++;
    }
}

void add_new_directory_pair(int fd_inotify, char *dir1, char *dir2, int sent_from_console)
{
    char buffer[1000];
    get_current_datetime(buffer);

    if (findBySource(watched_dirs, dir1) == NULL)
    {
        int wd = inotify_add_watch(fd_inotify, dir1, WATCH_EVENTS);
        DirData data = {wd, dir1, dir2, ACTIVE, buffer, 0, 0};

        insertDirList(watched_dirs, data);

        snprintf(buffer, sizeof(buffer), "Added directory: %s -> %s", dir1, dir2);
        insert_log(buffer, 1, 1, sent_from_console);
        snprintf(buffer, sizeof(buffer), "Monitoring started for %s", dir1);
        insert_log(buffer, 1, 1, sent_from_console);
        if (sent_from_console)
        {
            // Was told that when the console asks to add we only return monitoring started
            // and we do not tell the console that we started synchronizing even though we
            // do synchronize under the hood it doesnt need to know
            send_finito_signal_to_console();
        }

        snprintf(buffer, sizeof(buffer), "sync %s", dir1);
        execute_command(buffer, fd_inotify, 0);
    }
    else
    {
        snprintf(buffer, sizeof(buffer), "Already in queue: %s", dir1);
        insert_log(buffer, 1, 0, sent_from_console);
        if (sent_from_console)
        {
            send_finito_signal_to_console();
        }
    }
}

DirList createDirList_from_file(const char *filename, int fd_inotify)
{
    watched_dirs = createDirList();
    if (watched_dirs == NULL)
        return NULL;

    FILE *file = fopen(filename, "r");
    if (file == NULL)
    {
        printf("ERROR: %s could not be opened\n", filename);
        return NULL;
    }

    // will take the risk and assume you wont
    // use more than 500 characters in a single line
    char line[500];

    while (fgets(line, sizeof(line), file) != NULL)
    {
        line[strcspn(line, "\n")] = '\0'; // removes the newline
        char *dir1 = strtok(line, " ");
        char *dir2 = strtok(NULL, " ");
        add_new_directory_pair(fd_inotify, dir1, dir2, 0);
    }

    fclose(file);
    return watched_dirs;
}

int handle_worker_responses(int i, struct pollfd *pfds)
{
    char line[1000];
    FILE *fp = fdopen(workers[i].from_child[0], "r");
    if (fp)
    {
        while (fgets(line, sizeof(line), fp))
        {
            workers[i].results.lines_recieved += 1;
            if (workers[i].results.lines_recieved == 2)
            {
                if (strlen(line) < 8)
                {
                    // Shouldnt ever happen, if it happens just let me know so I can debug the problem
                    printf("Worker %d returned invalid input at line %d\n", workers[i].pid, workers[i].results.lines_recieved);
                    continue;
                }
                line[strlen(line) - 1] = '\0';
                strcpy(workers[i].results.status, line + 8);
            }
            else if (workers[i].results.lines_recieved == 3)
            {
                if (strlen(line) < 9)
                {
                    // Shouldnt ever happen, if it happens just let me know so I can debug the problem
                    printf("Worker %d returned invalid input at line %d\n", workers[i].pid, workers[i].results.lines_recieved);
                    continue;
                }
                line[strlen(line) - 1] = '\0';
                strcpy(workers[i].results.details, line + 9);
            }
            else if (workers[i].results.lines_recieved > 4)
            {
                if (strcmp("EXEC_REPORT_END\n", line) == 0)
                {
                    // workers[i].finished = 1;
                    break;
                }
                else
                {
                    if (strlen(line) < 2)
                    {
                        // Shouldnt ever happen, if it happens just let me know so I can debug the problem
                        printf("Worker %d returned invalid input at line %d\n", workers[i].pid, workers[i].results.lines_recieved);
                        continue;
                    }
                    line[strlen(line) - 1] = '\0';
                    strcpy(workers[i].results.error, line + 2);
                    workers[i].results.errors++;
                }
            }
        }
        fclose(fp);
    }
    else
    {
        if (workers[i].finished == 1)
        {
            close(workers[i].from_child[0]);
        }
    }

    if (workers[i].finished == 1)
    {
        workers[i].finished = 0;
        int pid = workers[i].pid;
        int errors = workers[i].results.errors;
        char *status = workers[i].results.status;
        char *error = workers[i].results.error;
        char *details = workers[i].results.details;

        close(workers[i].from_child[0]);
        pfds[i + 2].fd = -1;
        active_workers--;
        workers[i].pid = -1;

        Task *completed_task = readTaskByPid(assigned_tasks, pid);
        if (completed_task == NULL)
        {
            // Shouldnt ever happen, if it happens just let me know so I can debug the problem
            printf("ERROR: Unexpected failure to find task by PID in assigned_tasks: %d\n", pid);
            return -1;
        }

        DirData *directory_data = findBySource(watched_dirs, completed_task->source_dir);
        directory_data->error_count += errors;

        char buffer[1024];
        get_current_datetime(buffer);

        strcpy(directory_data->last_sync_time, buffer); // Updates the last sync time

        switch (completed_task->task_type)
        {
        case FULL:
            snprintf(buffer, sizeof(buffer), "[%s] [%s] [%d] [FULL] [%s] [%s]", completed_task->source_dir, completed_task->target_dir, pid, status, details);
            directory_data->synchronizing = 0;
            insert_log(buffer, 0, 1, 0);
            snprintf(buffer, sizeof(buffer), "Sync completed %s -> %s Errors: %d", directory_data->source_dir, directory_data->target_dir, errors);
            insert_log(buffer, 1, 1, completed_task->added_by_console);
            if (completed_task->added_by_console)
            {
                send_finito_signal_to_console();
            }
            break;
        case ADDED:
            if (errors > 0)
            {
                snprintf(buffer, sizeof(buffer), "[%s] [%s] [%d] [ADDED] [ERROR] [%s]", completed_task->source_dir, completed_task->target_dir, pid, error);
                insert_log(buffer, 1, 1, 0);
            }
            else
            {
                snprintf(buffer, sizeof(buffer), "[%s] [%s] [%d] [ADDED] [SUCCESS] [%s]", completed_task->source_dir, completed_task->target_dir, pid, details);
                insert_log(buffer, 1, 1, 0);
            }
            break;
        case MODIFIED:
            if (errors > 0)
            {
                snprintf(buffer, sizeof(buffer), "[%s] [%s] [%d] [MODIFIED] [ERROR] [%s]", completed_task->source_dir, completed_task->target_dir, pid, error);
                insert_log(buffer, 1, 1, 0);
            }
            else
            {
                snprintf(buffer, sizeof(buffer), "[%s] [%s] [%d] [MODIFIED] [SUCCESS] [%s]", completed_task->source_dir, completed_task->target_dir, pid, details);
                insert_log(buffer, 1, 1, 0);
            }
            break;
        case DELETED:
            if (errors > 0)
            {
                snprintf(buffer, sizeof(buffer), "[%s] [%s] [%d] [DELETED] [ERROR] [%s]", completed_task->source_dir, completed_task->target_dir, pid, error);
                insert_log(buffer, 1, 1, 0);
            }
            else
            {
                snprintf(buffer, sizeof(buffer), "[%s] [%s] [%d] [DELETED] [SUCCESS] [%s]", completed_task->source_dir, completed_task->target_dir, pid, details);
                insert_log(buffer, 1, 1, 0);
            }
            break;
        }

        extractTaskByPid(assigned_tasks, pid);
    }

    return 1;
}

int handle_inotify_read(int fd_inotify)
{
    // From what I understand, the size of a inotify event is not fixed due to it
    // storing the filename directly inside instead of a pointer to it. And so, I
    // had to consider edge cases just like the ones I used for reading console commands.
    // What I care about is possible having a struct half readen into the end of the buffer
    char temp_buffer[4096];
    long unsigned int stored_bytes = 0;
    char buffer[4096];

    while (1)
    {
        // Was very paranoid about the pointer math, even drew them in paper, and I believe the code
        // should run without problem
        ssize_t bytes_readen = read(fd_inotify, temp_buffer, sizeof(temp_buffer));

        if (bytes_readen < 0)
        {
            break;
        }

        memcpy(buffer + stored_bytes, temp_buffer, bytes_readen);
        stored_bytes += bytes_readen;

        for (char *ptr = buffer; ptr < (buffer + stored_bytes);)
        {
            struct inotify_event *event = (struct inotify_event *)ptr;

            // Edge case when an event at the end of the struct hasnt been fully stored,
            // then we have to break so we read the rest of the data and complete the struct
            long unsigned int remaining_bytes = buffer + stored_bytes - ptr;
            if (sizeof(struct inotify_event) > remaining_bytes)
            {
                // If not even the fixed part of the struct has fit we break
                memmove(buffer, ptr, remaining_bytes);
                stored_bytes = remaining_bytes;
                break;
            }
            if (sizeof(struct inotify_event) + event->len > remaining_bytes)
            {
                // The fixed part has fit, so we can safely read event->len
                // Now we also check if the dynamic part also has fit if not we break
                memmove(buffer, ptr, remaining_bytes);
                stored_bytes = remaining_bytes;
                break;
            }

            if (event->mask & IN_CREATE)
            {
                DirData *dir_data = findByWD(watched_dirs, event->wd);
                if (dir_data == NULL)
                {
                    // Should never happen, but if it does let me know so I debug
                    printf("For some reason we were not able to find a watched directory in the linked list\n");
                }
                else
                {
                    Task new_task = {-1,
                                     dir_data->source_dir,
                                     dir_data->target_dir,
                                     event->name,
                                     ADDED,
                                     0};
                    insertTask(task_queue, new_task);
                }
            }
            if (event->mask & IN_DELETE)
            {
                DirData *dir_data = findByWD(watched_dirs, event->wd);
                if (dir_data == NULL)
                {
                    // Should never happen, but if it does let me know so I debug
                    printf("For some reason we were not able to find a watched directory in the linked list\n");
                }
                else
                {
                    Task new_task = {-1,
                                     dir_data->source_dir,
                                     dir_data->target_dir,
                                     event->name,
                                     DELETED,
                                     0};
                    insertTask(task_queue, new_task);
                }
            }
            if (event->mask & IN_CLOSE_WRITE)
            {
                DirData *dir_data = findByWD(watched_dirs, event->wd);
                if (dir_data == NULL)
                {
                    // Should never happen, but if it does let me know so I debug
                    printf("For some reason we were not able to find a watched directory in the linked list\n");
                }
                else
                {
                    Task new_task = {-1,
                                     dir_data->source_dir,
                                     dir_data->target_dir,
                                     event->name,
                                     MODIFIED,
                                     0};
                    insertTask(task_queue, new_task);
                }
            }

            // did you know there can be structs that do not have fixed size?
            // first time seeing this, but sizeof will return only the fixed size
            // the dynamic part, which is the files name, is gotten from the len field
            ptr += sizeof(struct inotify_event) + event->len;
        }
    }

    return 1;
}

int handle_command_read(int fd_fifo_in, int fd_inotify)
{
    // After implementing the console, I realized this part could be simplified.
    // The console only allows one command at a time: the user must wait for a response before sending another.
    // This guarantees that we only ever receive a single complete command per read.
    // We also enforce a maximum command size of 500 characters, making parsing straightforward.
    // From this side, we can safely expect a single line of input, no larger than 500 characters.
    char buffer[500];

    ssize_t bytes_read = read(fd_fifo_in, buffer, sizeof(buffer) - 1);
    if (bytes_read <= 0)
    {
        return 0;
    }

    char *newline = strchr(buffer, '\n');
    if (newline)
    {
        *newline = '\0';
    }

    return execute_command(buffer, fd_inotify, 1);
}

int execute_command(char *command, int fd_inotify, int sent_from_console)
{
    char buffer[500];
    char *tokens[100];
    int token_count = 0;

    char *token = strtok(command, " ");
    while (token != NULL)
    {
        tokens[token_count++] = token;
        token = strtok(NULL, " ");
    }

    tokens[token_count] = NULL;

    if (token_count == 0)
    {
        return 1;
    }
    if (strcmp(tokens[0], "sync") == 0)
    {
        if (token_count != 2)
        {
            insert_log("Incorrect command", 0, 0, 1);
            send_finito_signal_to_console();
            return -2;
        }
        DirData *dir_data = findBySource(watched_dirs, tokens[1]);
        if (dir_data == NULL)
        {
            snprintf(buffer, sizeof(buffer), "Directory not added: %s\n", tokens[1]);
            insert_log(buffer, 1, 0, sent_from_console);
            if (sent_from_console)
            {
                send_finito_signal_to_console(sent_from_console);
            }
        }
        else
        {
            if (dir_data->synchronizing)
            {
                // The instructions state that if a source directory has already a syncrhonizing task in place
                // then we should not append another synchronizing task to be completed (I don't make the rules)
                snprintf(buffer, sizeof(buffer), "Already in queue: %s\n", dir_data->source_dir);
                insert_log(buffer, 1, 0, sent_from_console);
                if (sent_from_console)
                {
                    send_finito_signal_to_console();
                }
            }
            else
            {
                if (dir_data->status == INACTIVE)
                {
                    // Was told that cancel does not remove the pair only makes it inactive, and sync makes it active again
                    dir_data->watch_descriptor = inotify_add_watch(fd_inotify, dir_data->source_dir, WATCH_EVENTS);
                    dir_data->status = ACTIVE;
                }
                Task new_task = {-1,
                                 dir_data->source_dir,
                                 dir_data->target_dir,
                                 "ALL",
                                 FULL,
                                 sent_from_console};
                insertTask(task_queue, new_task);
                snprintf(buffer, sizeof(buffer), "Syncing directory: %s -> %s", tokens[1], dir_data->target_dir);
                insert_log(buffer, 1, 1, sent_from_console);
                dir_data->synchronizing = 1;
            }
        }
    }
    else if (strcmp(tokens[0], "add") == 0)
    {
        if (token_count != 3)
        {
            insert_log("Incorrect command", 0, 0, 1);
            send_finito_signal_to_console();
            return -2;
        }
        else
        {
            add_new_directory_pair(fd_inotify, tokens[1], tokens[2], 1);
        }
    }
    else if (strcmp(tokens[0], "cancel") == 0)
    {
        if (token_count != 2)
        {
            insert_log("Incorrect command", 0, 0, 1);
            send_finito_signal_to_console();
            return -2;
        }
        DirData *dir_data = findBySource(watched_dirs, tokens[1]);
        if (dir_data == NULL || dir_data->status == INACTIVE)
        {
            snprintf(buffer, sizeof(buffer), "Directory not monitored: %s\n", tokens[1]);
            insert_log(buffer, 1, 0, 1);
            send_finito_signal_to_console();
        }
        else
        {
            inotify_rm_watch(fd_inotify, dir_data->watch_descriptor);
            dir_data->status = INACTIVE;
            dir_data->synchronizing = 0;
            snprintf(buffer, sizeof(buffer), "Monitoring stopped for %s", tokens[1]);
            insert_log(buffer, 1, 1, 1);
            send_finito_signal_to_console();
        }
    }
    else if (strcmp(tokens[0], "status") == 0)
    {
        if (token_count != 2)
        {
            insert_log("Incorrect command", 0, 0, 1);
            send_finito_signal_to_console();
            return -2;
        }
        DirData *dir_data = findBySource(watched_dirs, tokens[1]);
        if (dir_data == NULL)
        {
            snprintf(buffer, sizeof(buffer), "Directory not monitored: %s", tokens[1]);
            insert_log(buffer, 1, 0, 1);
            send_finito_signal_to_console();
        }
        else
        {
            snprintf(buffer, sizeof(buffer), "Status requests for %s\nDirectory: %s\nTarget: %s\nLast Sync: %s\nErrors: %d\nStatus: %s",
                     dir_data->source_dir, dir_data->source_dir, dir_data->target_dir, dir_data->last_sync_time, dir_data->error_count,
                     (dir_data->status == ACTIVE) ? "Active" : "Inactive");
            insert_log(buffer, 1, 0, 1);
            send_finito_signal_to_console();
        }
    }
    else if (strcmp(tokens[0], "shutdown") == 0)
    {
        get_current_datetime(buffer);
        insert_log("Shutting down manager...", 1, 0, 1);
        freeList(&watched_dirs);
        insert_log("Waiting for all active workers to finish.", 1, 0, 1);
        insert_log("Processing remaining queued tasks.", 1, 0, 1);
        return -1;
    }
    else
    {
        insert_log("Incorrect command", 0, 0, 1);
        send_finito_signal_to_console();
        return -2;
    }
    return 0;
}

void insert_log(char *log, int print_on_screen, int print_in_file, int print_to_console)
{
    char datetime_str[250];
    char buffer[1000];
    get_current_datetime(datetime_str);
    snprintf(buffer, sizeof(buffer), "[%s] %s\n", datetime_str, log);
    if (print_on_screen == 1)
    {
        printf("%s", buffer);
    }
    if (print_in_file == 1)
    {
        fprintf(log_file, "%s", buffer);
        fflush(log_file);
    }
    if (print_to_console == 1)
    {
        write(fd_fifo_out, buffer, strlen(buffer));
    }
}

void send_finito_signal_to_console()
{
    // Will stat working on the console soon and need a way to decide when to stop reading when waiting for manager's response
    char buffer[100];
    snprintf(buffer, sizeof(buffer), "COMMAND_FINISHED\n");
    write(fd_fifo_out, buffer, strlen(buffer));
}