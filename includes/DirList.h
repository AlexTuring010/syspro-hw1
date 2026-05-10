#ifndef DIR_LIST
#define DIR_LIST

#include <time.h>
#include <sys/inotify.h>

typedef struct dirlist *DirList; // Opaque linked list of DirData

typedef enum Status
{
    INACTIVE,
    ACTIVE
} Status;

typedef struct DirData
{
    int watch_descriptor;
    char *source_dir;
    char *target_dir;
    Status status;
    char *last_sync_time;
    int error_count;
    int synchronizing;
} DirData;

DirList createDirList(); // Returns Null if malloc fails, probably wont though

// When inserting a DirData into the list, the list makes its own copies of the strings.
// It does NOT take ownership of the original strings passed by the caller.
// This means:
//   - Any changes to the original strings after insertion will NOT affect the list.
//   - It is the caller's responsibility to free the original strings when no longer needed.
//   - freeList() will only free the internal copies stored in the list.
int insertDirList(DirList, DirData); // Returns 0 if malloc fails, returns list size otherwise

DirData *findBySource(DirList, const char *); // Returns Null if not found
DirData *findByTarget(DirList, const char *); // Returns Null if not found
DirData *findByWD(DirList, int);              // Returns Null if not found
int removeBySource(DirList, const char *);    // Returns 0 if not found
int numOfDirs(DirList);                       // In case I ever need to know how many dirs there are
void freeList(DirList *);                     // Always free when done

typedef struct DirListIterator *DirListIterator; // will need this to iterate the list

DirListIterator createDirListIterator(DirList);
DirData *nextDir(DirListIterator);
void freeDirListIterator(DirListIterator);

#endif