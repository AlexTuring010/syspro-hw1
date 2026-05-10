#include <stdlib.h>
#include "DirList.h"
#include "string.h"
#include <stdio.h>

struct listNode
{
    DirData data;
    struct listNode *next;
};

struct dirlist
{
    int count;
    struct listNode *head;
};

DirList createDirList()
{
    DirList list = malloc(sizeof(struct dirlist));
    if (list == NULL)
        return NULL;
    list->count = 0;
    list->head = NULL;
    return list;
}

int insertDirList(DirList list, DirData data)
{
    struct listNode *newNode = malloc(sizeof(struct listNode));
    if (newNode == NULL)
        return 0;

    // Copy the data into the new node
    newNode->data = data;
    newNode->data.source_dir = malloc(strlen(data.source_dir) + 1);
    strcpy(newNode->data.source_dir, data.source_dir);
    newNode->data.target_dir = malloc(strlen(data.target_dir) + 1);
    strcpy(newNode->data.target_dir, data.target_dir);
    newNode->data.last_sync_time = malloc(strlen(data.last_sync_time) + 1);
    strcpy(newNode->data.last_sync_time, data.last_sync_time);

    // Insert the new node at the head of the list
    newNode->next = list->head;
    list->head = newNode;

    list->count += 1;
    return list->count;
}

DirData *findBySource(DirList list, const char *source)
{
    if (!list || !source)
        return NULL;
    struct listNode *nodePtr = list->head;
    while (nodePtr != NULL)
    {
        if (strcmp(nodePtr->data.source_dir, source) == 0)
        {
            return &(nodePtr->data);
        }
        nodePtr = nodePtr->next;
    }
    return NULL;
}

DirData *findByTarget(DirList list, const char *target)
{
    if (!list || !target)
        return NULL;
    struct listNode *nodePtr = list->head;
    while (nodePtr != NULL)
    {
        if (strcmp(nodePtr->data.target_dir, target) == 0)
        {
            return &(nodePtr->data);
        }
        nodePtr = nodePtr->next;
    }
    return NULL;
}

DirData *findByWD(DirList list, int wd)
{
    if (!list)
        return NULL;
    struct listNode *nodePtr = list->head;
    while (nodePtr != NULL)
    {
        if (nodePtr->data.watch_descriptor == wd)
        {
            return &(nodePtr->data);
        }
        nodePtr = nodePtr->next;
    }
    return NULL;
}

void freeNode(struct listNode *nodePtr)
{
    free(nodePtr->data.source_dir);
    free(nodePtr->data.target_dir);
    free(nodePtr->data.last_sync_time);
    free(nodePtr);
}

int removeBySource(DirList list, const char *source)
{
    if (!list || !source)
        return 0;
    struct listNode *prevPtr = NULL;
    struct listNode *nodePtr = list->head;
    while (nodePtr != NULL)
    {
        if (strcmp(nodePtr->data.source_dir, source) == 0)
        {
            if (prevPtr == NULL)
            {
                list->head = nodePtr->next;
            }
            else
            {
                prevPtr->next = nodePtr->next;
            }
            freeNode(nodePtr);
            list->count -= 1;
            return list->count;
        }
        prevPtr = nodePtr;
        nodePtr = nodePtr->next;
    }
    return 0;
}

int numOfDirs(DirList list)
{
    return list->count;
}

void freeList(DirList *list)
{
    if (!(*list))
        return;
    struct listNode *nodePtr = (*list)->head;
    struct listNode *tmpPtr = NULL;
    while (nodePtr != NULL)
    {
        tmpPtr = nodePtr;
        nodePtr = nodePtr->next;
        freeNode(tmpPtr);
    }
    free(*list);
    *list = NULL;
}

struct DirListIterator
{
    struct listNode *next;
};

DirListIterator createDirListIterator(DirList list)
{
    DirListIterator iterator = malloc(sizeof(struct DirListIterator));
    iterator->next = list->head;
    return iterator;
}

DirData *nextDir(DirListIterator iterator)
{
    if (iterator->next == NULL)
        return NULL;
    DirData *data = &(iterator->next->data);
    iterator->next = iterator->next->next;
    return data;
}

void freeDirListIterator(DirListIterator iterator)
{
    free(iterator);
}