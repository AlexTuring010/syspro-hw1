#include "TaskQueue.h"
#include <stdlib.h>
#include <string.h>

struct taskNode
{
    Task task;
    struct taskNode *next;
};

struct taskqueue
{
    int count;
    struct taskNode *head;
};

TaskQueue createTaskQueue()
{
    TaskQueue queue = malloc(sizeof(struct taskqueue));
    if (queue == NULL)
        return NULL;
    queue->count = 0;
    queue->head = NULL;
    return queue;
}

int insertTask(TaskQueue queue, Task task)
{
    struct taskNode *nodePtr = queue->head;
    if (nodePtr == NULL)
    {
        queue->head = malloc(sizeof(struct taskNode));
        nodePtr = queue->head;
    }
    else
    {
        while (nodePtr->next != NULL)
        {
            nodePtr = nodePtr->next;
        }
        nodePtr->next = malloc(sizeof(struct taskNode));
        nodePtr = nodePtr->next;
    }
    if (nodePtr == NULL)
        return 0;
    nodePtr->task = task;
    nodePtr->task.source_dir = malloc(strlen(task.source_dir) + 1);
    strcpy(nodePtr->task.source_dir, task.source_dir);
    nodePtr->task.target_dir = malloc(strlen(task.target_dir) + 1);
    strcpy(nodePtr->task.target_dir, task.target_dir);
    nodePtr->task.file_name = malloc(strlen(task.file_name) + 1);
    strcpy(nodePtr->task.file_name, task.file_name);
    nodePtr->next = NULL;
    queue->count += 1;
    return queue->count;
}

Task *readTask(TaskQueue queue)
{
    if (queue->count == 0)
    {
        return NULL;
    }
    return &(queue->head->task);
}

Task *readTaskByPid(TaskQueue queue, pid_t pid)
{
    if (queue == NULL)
        return NULL;

    struct taskNode *nodePtr;
    nodePtr = queue->head;

    while (nodePtr != NULL)
    {
        if (nodePtr->task.pid == pid)
        {
            return &(nodePtr->task);
        }
        nodePtr = nodePtr->next;
    }
    return NULL;
}

void freeTaskNode(struct taskNode *nodePtr)
{
    free(nodePtr->task.file_name);
    free(nodePtr->task.source_dir);
    free(nodePtr->task.target_dir);
    free(nodePtr);
}

void extractTaskByPid(TaskQueue queue, pid_t pid)
{
    // I realize this is less efficient, calling readTaskById() and then
    // looping through the list again to extract the task means traversing twice.
    // It could definitely be optimized into a single pass.
    // That said, the task list is small enough that the performance impact is negligible.
    // If I ever become a perfectionist and want it to be faster I will also use hashtable instead
    if (queue == NULL)
        return;

    struct taskNode *prevPtr = NULL;
    struct taskNode *nodePtr = queue->head;
    while (nodePtr != NULL)
    {
        if (nodePtr->task.pid == pid)
        {
            if (prevPtr == NULL)
            {
                queue->head = nodePtr->next;
                freeTaskNode(nodePtr);
                return;
            }
            else
            {
                prevPtr->next = nodePtr->next;
                freeTaskNode(nodePtr);
                return;
            }
        }
        prevPtr = nodePtr;
        nodePtr = nodePtr->next;
    }
}

void extractTask(TaskQueue queue)
{
    if (queue->count == 0)
    {
        return;
    }
    struct taskNode *tmp;
    tmp = queue->head;
    queue->head = queue->head->next;
    freeTaskNode(tmp);
    queue->count -= 1;
}

int numOfTasks(TaskQueue queue)
{
    return queue->count;
}

void freeQueue(TaskQueue *queue)
{
    if (!(*queue))
        return;
    struct taskNode *nodePtr = (*queue)->head;
    struct taskNode *tmpPtr = NULL;
    while (nodePtr != NULL)
    {
        tmpPtr = nodePtr;
        nodePtr = nodePtr->next;
        freeTaskNode(tmpPtr);
    }
    free(*queue);
    *queue = NULL;
}