#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <errno.h>
#include <dirent.h>
#include <stdlib.h>
#include <sys/file.h>
#include "TaskQueue.h"
// For now I have only tested running this independently from the main program
// just to make sure there are no errors, that it compiles, and the report looks good

void do_the_synchronization(char *source_directory, char *target_directory);
void added_file(char *source_directory, char *target_directory, char *filename);
void modified_file(char *source_directory, char *target_directory, char *filename);
void deleted_file(char *target_directory, char *filename);

int main(int argc, char *argv[])
{
    if (argc < 5)
    {
        printf("Not enough arguments\n");
        return -1;
    }
    char *source_directory = argv[1];
    char *target_directory = argv[2];
    char *filename = argv[3];
    int operation = atoi(argv[4]);

    switch (operation)
    {
    case FULL:
        do_the_synchronization(source_directory, target_directory);
        break;
    case ADDED:
        added_file(source_directory, target_directory, filename);
        break;
    case MODIFIED:
        modified_file(source_directory, target_directory, filename);
        break;
    case DELETED:
        deleted_file(target_directory, filename);
        break;
    }

    return 0;
}

// Decided to implement a quick ErrorList API
// this will be useful for storing error messages

typedef struct ErrorNode
{
    char error_message[500];
    struct ErrorNode *next;
} ErrorNode;

typedef ErrorNode *ErrorList;

ErrorList createErrorList()
{
    return NULL;
}

ErrorList insertErrorList(ErrorList list, char *error)
{
    if (strlen(error) > 500)
    {
        error[499] = '\0';
    }
    if (list == NULL)
    {
        ErrorList newList = malloc(sizeof(ErrorNode));
        strcpy(newList->error_message, error);
        newList->next = NULL;
        return newList;
    }
    ErrorList nodePtr = list;
    while (nodePtr->next != NULL)
    {
        nodePtr = nodePtr->next;
    }
    nodePtr->next = malloc(sizeof(ErrorNode));
    strcpy(nodePtr->next->error_message, error);
    return list;
}

ErrorList destroyErrorList(ErrorList list)
{
    while (list != NULL)
    {
        ErrorList tmp = list;
        list = list->next;
        free(tmp);
    }
    return NULL;
}

// And bellow is a lot of code for synchronizing, locking and unlocking files, copying files,
// tried to catch errors using strerror like written in the instructions

int lock_file(int fd)
{
    // Try to acquire an exclusive lock on the file (blocking)
    if (flock(fd, LOCK_EX) == -1)
    {
        return -1;
    }
    return 0;
}

int unlock_file(int fd)
{
    // Unlock the file
    if (flock(fd, LOCK_UN) == -1)
    {
        return -1;
    }
    return 0;
}

int copy_file(char *source, char *target, ErrorList *error_list)
{
    int src_fd = open(source, O_RDONLY);
    if (src_fd == -1)
    {
        char error_message[500];
        snprintf(error_message, sizeof(error_message), "File %s: %s", source, strerror(errno));
        *error_list = insertErrorList(*error_list, error_message);
        return -1;
    }

    if (lock_file(src_fd) == -1)
    {
        char error_message[500];
        snprintf(error_message, sizeof(error_message), "File %s: Unable to lock for reading", source);
        *error_list = insertErrorList(*error_list, error_message);
        close(src_fd);
        return -1;
    }

    int tgt_fd = open(target, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (tgt_fd == 1)
    {
        char error_message[500];
        snprintf(error_message, sizeof(error_message), "File %s: %s", target, strerror(errno));
        *error_list = insertErrorList(*error_list, error_message);
        unlock_file(src_fd);
        close(src_fd);
        return -1;
    }

    if (lock_file(tgt_fd) == -1)
    {
        char error_message[500];
        snprintf(error_message, sizeof(error_message), "File %s: Unable to lock for writing", target);
        *error_list = insertErrorList(*error_list, error_message);
        unlock_file(src_fd);
        close(src_fd);
        close(tgt_fd);
        return -1;
    }

    char buffer[4096];
    ssize_t bytes_read, bytes_written;
    while ((bytes_read = read(src_fd, buffer, sizeof(buffer))) > 0)
    {
        bytes_written = write(tgt_fd, buffer, bytes_read);
        if (bytes_written == -1)
        {
            char error_message[500];
            snprintf(error_message, sizeof(error_message), "File %s: %s", target, strerror(errno));
            *error_list = insertErrorList(*error_list, error_message);
            unlock_file(src_fd);
            unlock_file(tgt_fd);
            close(src_fd);
            close(tgt_fd);
            return -1;
        }
    }

    if (bytes_read == -1)
    {
        char error_message[500];
        snprintf(error_message, sizeof(error_message), "File %s: %s", source, strerror(errno));
        *error_list = insertErrorList(*error_list, error_message);
        unlock_file(src_fd);
        unlock_file(tgt_fd);
        close(src_fd);
        close(tgt_fd);
        return -1;
    }

    unlock_file(src_fd);
    unlock_file(tgt_fd);
    close(src_fd);
    close(tgt_fd);
    return 0;
}

int delete_file(char *target, ErrorList *error_list)
{
    // Attempt to delete the file
    if (unlink(target) == -1)
    {
        char error_message[500];
        snprintf(error_message, sizeof(error_message), "File %s: %s", target, strerror(errno));
        *error_list = insertErrorList(*error_list, error_message);
        return -1;
    }

    return 0;
}

void deleted_file(char *target_directory, char *filename)
{
    ErrorList error_list = createErrorList();

    char target_path[1024];
    snprintf(target_path, sizeof(target_path), "%s/%s", target_directory, filename);

    delete_file(target_path, &error_list);

    if (error_list == NULL)
    {
        printf("EXEC_REPORT_START\n");
        printf("STATUS: SUCCESS\n");
        printf("DETAILS: File: %s\n", filename);
        printf("ERRORS:\n");
        printf("EXEC_REPORT_END\n");
    }
    else
    {
        printf("EXEC_REPORT_START\n");
        printf("STATUS: ERROR\n");
        printf("DETAILS: no file was deleted\n");
        printf("ERRORS:\n");
        printf("- %s\n", error_list->error_message);
        printf("EXEC_REPORT_END\n");
    }

    destroyErrorList(error_list);
}

void modified_file(char *source_directory, char *target_directory, char *filename)
{
    ErrorList error_list = createErrorList();

    char source_path[1024], target_path[1024];
    snprintf(source_path, sizeof(source_path), "%s/%s", source_directory, filename);
    snprintf(target_path, sizeof(target_path), "%s/%s", target_directory, filename);

    copy_file(source_path, target_path, &error_list);

    if (error_list == NULL)
    {
        printf("EXEC_REPORT_START\n");
        printf("STATUS: SUCCESS\n");
        printf("DETAILS: File: %s\n", filename);
        printf("ERRORS:\n");
        printf("EXEC_REPORT_END\n");
    }
    else
    {
        printf("EXEC_REPORT_START\n");
        printf("STATUS: ERROR\n");
        printf("DETAILS: no file was modified\n");
        printf("ERRORS:\n");
        printf("- %s\n", error_list->error_message);
        printf("EXEC_REPORT_END\n");
    }

    destroyErrorList(error_list);
}

void added_file(char *source_directory, char *target_directory, char *filename)
{
    ErrorList error_list = createErrorList();

    char source_path[1024], target_path[1024];
    snprintf(source_path, sizeof(source_path), "%s/%s", source_directory, filename);
    snprintf(target_path, sizeof(target_path), "%s/%s", target_directory, filename);

    copy_file(source_path, target_path, &error_list);

    if (error_list == NULL)
    {
        printf("EXEC_REPORT_START\n");
        printf("STATUS: SUCCESS\n");
        printf("DETAILS: File: %s\n", filename);
        printf("ERRORS:\n");
        printf("EXEC_REPORT_END\n");
    }
    else
    {
        printf("EXEC_REPORT_START\n");
        printf("STATUS: ERROR\n");
        printf("DETAILS: no file was created\n");
        printf("ERRORS:\n");
        printf("- %s\n", error_list->error_message);
        printf("EXEC_REPORT_END\n");
    }

    destroyErrorList(error_list);
}

void do_the_synchronization(char *source_directory, char *target_directory)
{
    int copied = 0;
    int skipped = 0;
    ErrorList error_list = createErrorList();

    DIR *dir = opendir(source_directory);
    if (!dir)
    {
        char error_message[500];
        snprintf(error_message, sizeof(error_message), "Directory %s: %s", source_directory, strerror(errno));
        printf("EXEC_REPORT_START\n");
        printf("STATUS: ERROR\n");
        printf("DETAILS: %s\n", error_message);
        printf("ERRORS:\n");
        printf("- %s\n", error_message);
        printf("EXEC_REPORT_END\n");
        error_list = destroyErrorList(error_list);
        return;
    }

    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL)
    {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
        {
            continue;
        }

        char source_path[1024], target_path[1024];
        snprintf(source_path, sizeof(source_path), "%s/%s", source_directory, entry->d_name);
        snprintf(target_path, sizeof(target_path), "%s/%s", target_directory, entry->d_name);

        if (copy_file(source_path, target_path, &error_list) == -1)
        {
            skipped++;
        }
        else
        {
            copied++;
        }
    }

    closedir(dir);

    // Output the report
    printf("EXEC_REPORT_START\n");
    if (error_list == NULL)
    {
        printf("STATUS: SUCCESS\n");
        printf("DETAILS: %d files copied, %d skipped\n", copied, skipped);
        printf("ERRORS:\n");
    }
    else
    {
        printf("STATUS: PARTIAL\n");
        printf("DETAILS: %d files copied, %d skipped\n", copied, skipped);
        printf("ERRORS:\n");

        ErrorList node = error_list;
        while (node != NULL)
        {
            printf("- %s\n", node->error_message);
            node = node->next;
        }
    }
    printf("EXEC_REPORT_END\n");

    error_list = destroyErrorList(error_list);
}
