#ifndef TASK_QUEUE
#define TASK_QUEUE

#include <sys/types.h>

typedef struct taskqueue *TaskQueue;

typedef enum
{
    FULL,
    ADDED,
    MODIFIED,
    DELETED
} TaskType;

typedef struct task
{
    pid_t pid;
    char *source_dir;
    char *target_dir;
    char *file_name;
    TaskType task_type;
    int added_by_console; // Need to know if it was added by console or by config file
} Task;

TaskQueue createTaskQueue(); // Returns Null if malloc fails, probably wont though

// When inserting a Task into the queue, the queue makes its own copies of the strings.
// It does NOT take ownership of the original strings passed by the caller.
// This means:
//   - Any changes to the original strings after insertion will NOT affect the list.
//   - It is the caller's responsibility to free the original strings when no longer needed.
//   - freeQueue() will only free the internal copies stored in the list.
int insertTask(TaskQueue, Task); // Returns 0 if malloc fails, returns list size otherwise

// To read tasks from the queue, first call readTask() to access the current task without removing it.
// If you need to keep the task, make a copy of it yourself before calling extractTask(), which will
// remove and free the original task from the queue.
// This design keeps memory management responsibilities clearly separated between the user and the module.
Task *readTask(TaskQueue queue);   // This reads the next task in queue (doesnt extract it, just gives you a peek)
void extractTask(TaskQueue queue); // This extracts the next task from the queue, it frees any memory used to store it

// Reusing the existing task queue module to manage both unassigned and assigned tasks.
// Rather than creating a new module, we'll distinguish task states using the `pid` field:
// if `pid` is unset, the task is unassigned; if set, it's already assigned to a process.
// The main program will maintain two queues:
// - One for tasks waiting to be assigned.
// - One for tasks that have already been assigned.
//
// This approach also simplifies communication with worker processes — when a worker sends back
// a report, it doesn't need to include additional metadata about the task. The parent process
// can identify the completed task based on the worker's `pid` and match it accordingly.
Task *readTaskByPid(TaskQueue queue, pid_t pid);
void extractTaskByPid(TaskQueue queue, pid_t pid);

int numOfTasks(TaskQueue); // If I ever need to see how many tasks are waiting in the queue

void freeQueue(TaskQueue *); // Always free when done

#endif