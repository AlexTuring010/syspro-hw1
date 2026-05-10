#!/bin/bash

path=""
command=""

while getopts "p:c:" opt; do
    case $opt in
        p)
            path=$OPTARG
            ;;
        c)
            command=$OPTARG
            ;;
        *)
            usage
            ;;
    esac
done

if [ -z "$path" ] || [ -z "$command" ]; then
    echo "Usage: $0 -p <path> -c <command>"
    exit 1
fi

case $command in
    listAll)
        if [ ! -f "$path" ]; then
            echo "Error: The log file does not exist at the specified path: $path"
            exit 1
        fi

        awk '
            # Match lines with exactly 7 fields enclosed in square brackets
            /\[[^\]]+\] \[[^\]]+\] \[[^\]]+\] \[[^\]]+\] \[[^\]]+\] \[[^\]]+\] \[[^\]]+\]/ {
                # Process the line to extract fields
                line = $0
                count = 0
                while (match(line, /\[([^\]]+)\]/)) {
                    fields[++count] = substr(line, RSTART+1, RLENGTH-2)
                    line = substr(line, RSTART + RLENGTH)
                }
                
                if (count >= 6) {
                    timestamp = fields[1]
                    source_dir = fields[2]
                    target_dir = fields[3]
                    status = fields[6]
        
                    # Store the most recent entry for each source_dir
                    last_entry[source_dir] = sprintf("%s -> %s [Last Sync: %s] [%s]", source_dir, target_dir, timestamp, status)
                }
            }
            END {
                # After processing all lines, print the last entry for each source_dir
                for (dir in last_entry) {
                    print last_entry[dir]
                }
            }
        ' "$path"
        ;;
    listMonitored)
        if [ ! -f "$path" ]; then
            echo "Error: The log file does not exist at the specified path: $path"
            exit 1
        fi
    
        awk '
            BEGIN {
                delete monitored
            }
    
            # Match "Monitoring started for" lines
            /Monitoring started for/ {
                # Extract the directory path
                dir = substr($0, index($0, "for ") + 4)
                monitored[dir] = 1  # Mark as monitored
            }
    
            # Match "Monitoring stopped for" lines
            /Monitoring stopped for/ {
                # Extract the directory path
                dir = substr($0, index($0, "for ") + 4)
                monitored[dir] = 0  # Mark as not monitored
            }
    
            # Match sync completion lines (with 7 fields in square brackets)
            /\[[^\]]+\] \[[^\]]+\] \[[^\]]+\] \[[^\]]+\] \[[^\]]+\] \[[^\]]+\] \[[^\]]+\]/ {
                # Process the line to extract fields
                line = $0
                count = 0
                while (match(line, /\[([^\]]+)\]/)) {
                    fields[++count] = substr(line, RSTART+1, RLENGTH-2)
                    line = substr(line, RSTART + RLENGTH)
                }
                
                if (count >= 6) {
                    timestamp = fields[1]
                    source_dir = fields[2]
                    target_dir = fields[3]
                    status = fields[6]
        
                    # Store the most recent entry for each source_dir
                    last_entry[source_dir] = sprintf("%s -> %s [Last Sync: %s] [%s]", source_dir, target_dir, timestamp, status)
                }
            }
    
            END {
                # Print only directories that are currently monitored
                for (dir in monitored) {
                    if (monitored[dir] && (dir in last_entry)) {
                        print last_entry[dir]
                    }
                }
            }
        ' "$path"
        ;;
    listStopped)
        if [ ! -f "$path" ]; then
            echo "Error: The log file does not exist at the specified path: $path"
            exit 1
        fi
    
        awk '
            BEGIN {
                delete monitored
            }
    
            # Match "Monitoring started for" lines
            /Monitoring started for/ {
                # Extract the directory path
                dir = substr($0, index($0, "for ") + 4)
                monitored[dir] = 1  # Mark as monitored
            }
    
            # Match "Monitoring stopped for" lines
            /Monitoring stopped for/ {
                # Extract the directory path
                dir = substr($0, index($0, "for ") + 4)
                monitored[dir] = 0  # Mark as not monitored
            }
    
            # Match sync completion lines (with 7 fields in square brackets)
            /\[[^\]]+\] \[[^\]]+\] \[[^\]]+\] \[[^\]]+\] \[[^\]]+\] \[[^\]]+\] \[[^\]]+\]/ {
                # Process the line to extract fields
                line = $0
                count = 0
                while (match(line, /\[([^\]]+)\]/)) {
                    fields[++count] = substr(line, RSTART+1, RLENGTH-2)
                    line = substr(line, RSTART + RLENGTH)
                }
                
                if (count >= 6) {
                    timestamp = fields[1]
                    source_dir = fields[2]
                    target_dir = fields[3]
                    status = fields[6]
        
                    # Store the most recent entry for each source_dir
                    last_entry[source_dir] = sprintf("%s -> %s [Last Sync: %s] [%s]", source_dir, target_dir, timestamp, status)
                }
            }
    
            END {
                # Print only directories that are currently monitored
                for (dir in monitored) {
                    if (monitored[dir] == 0 && (dir in last_entry)) {
                        print last_entry[dir]
                    }
                }
            }
        ' "$path"
        ;;
    purge)
        if [ -f "$path" ]; then
            echo "Purging a log-file:"
            echo "Deleting $path..."
            rm -f "$path"
            echo "Purge complete."
        elif [ -d "$path" ]; then
            echo "Purging backup directory:"
            echo "Deleting $path..."
            rm -rf "$path"/* "$path"/.[!.]* "$path"/..?* 2>/dev/null
            echo "Purge complete."
        fi
        ;;
    *)
        echo "Error: Invalid command"
        exit 1
        ;;
esac

exit 0

