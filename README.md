# syspro-hw1

A directory-mirroring daemon written in C: keeps backup copies of source directories synchronised with the originals, with all the systems-programming primitives you'd expect — `fork`/`exec` workers, `pipe` IPC, `poll`-driven event loop, `inotify` for change detection, custom task queue.

First homework of the **System Programming** course at the University of Athens (Department of Informatics & Telecommunications). Solo project.

## What it does

Configure source→destination directory pairs in `config_file.txt`. The daemon (`fss_manager`) keeps each destination synchronised with its source. The user-facing CLI (`fss_console`) sends commands over a named pipe.

```
config_file.txt          ┌──────────────────────┐
   src1/ -> dest1/  ───▶ │     fss_manager      │ ◀── fss_console
   src2/ -> dest2/       │  (single process)    │      (commands)
                         │                      │
                         │  inotify watches  ──┐│
                         │  task queue       │ ││
                         │  poll(2) loop  ──┘  ││
                         │       │             │
                         │       ▼             │
                         │  fork worker(s) ────┼─▶ pipe ─▶ sync result
                         └──────────────────────┘
```

Commands the console supports:

- `add <source> <target>` — start watching a new pair.
- `cancel <source>` — stop watching a pair.
- `status <source>` — show current sync state, last sync time, error count.
- `sync <source>` — trigger a full re-sync.
- `shutdown` — drain in-flight workers, then exit.

## Architecture choices

- **`fss_manager` is a single process** with a `poll(2)`-driven event loop. It multiplexes three input sources: `inotify` events from watched directories, messages from the console pipe, and exit notifications from worker children. No threads — concurrency comes from the worker pool.
- **Workers are forked children** that run `worker.c`. Each does one sync task (full-sync of a directory pair, or apply-delta for a single file change) and exits. Configurable `--worker_limit` caps how many can run at once; surplus tasks queue up in `TaskQueue`.
- **Workers report back over a `pipe`** — line-by-line, one record per file. `fss_manager` reads worker stdout and waits for `SIGCHLD` to confirm exit before freeing the slot.
- **`inotify` for change detection** — no busy-polling the filesystem. The manager registers watches at startup (or when `add` arrives) and queues sync tasks reactively when events fire.
- **Worker can run standalone.** Pass it the right CLI args and it'll do a one-off sync without the manager — useful for debugging and surfaced in the assignment brief.

## Modules

| File | Purpose |
|---|---|
| `src/main/fss_manager.c` | Event loop, worker dispatch, command handling |
| `src/main/fss_console.c` | CLI frontend, talks to manager over pipe |
| `src/main/worker.c` | Sync engine — full-sync and per-file modes |
| `src/modules/DirList.c` | Linked-list of watched directory pairs (status, last_sync_time, error_count, …) |
| `src/modules/TaskQueue.c` | FIFO queue of pending sync tasks, separate from active workers |

Tests use the [Unity](https://www.throwtheswitch.org/unity) C testing framework (vendored under `includes/`).

## Build & run

```bash
make                                          # produces fss_manager, fss_console, worker
./fss_manager -l manager_logs.txt -c config_file.txt -n 5 &
./fss_console -l console_logs.txt
```

Sample data ships in `source_dirs/source[1-4]/` and `dest_dirs/dest[1-3]/`. The Bash post-processing script `fss_script.sh` parses `manager_logs.txt` according to the assignment brief.

## Notable artefacts

- **`DevLogs.md`** — a personal development journal (in Greek) kept while building this. Captures the design decisions, dead ends, and the moment I realised inotify was the right tool. Left in as-is.
- **`CompletionReport.pdf`** — the formal report that accompanied the homework submission.

## Sequence

Part of a two-piece System Programming arc:

1. **syspro-hw1** *(you are here)* — single-machine, process-pool, inotify-driven
2. [syspro-hw2](https://github.com/AlexTuring010/syspro-hw2) — NFS-style: distributed across machines, thread-pool, custom PUSH/PULL TCP protocol. Reuses the `TaskQueue` and `DirList` modules from this repo.

## License

[MIT](LICENSE) — applies to my own code in this repo. Assignment-distributed materials and vendored libraries (Unity test framework) retain their original licenses.
