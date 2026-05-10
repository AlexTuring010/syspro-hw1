#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>

#define BUFFER_SIZE 1000

int main(int argc, char *argv[])
{
    FILE *fss_in = NULL;
    FILE *fss_out = NULL;
    FILE *log = NULL;
    char input[BUFFER_SIZE];
    char output[BUFFER_SIZE];

    if (argc != 3 || strcmp(argv[1], "-l") != 0)
    {
        fprintf(stderr, "Usage: %s -l <logfile>\n", argv[0]);
        return 1;
    }

    log = fopen(argv[2], "w");
    if (log == NULL)
    {
        perror("Error opening logfile");
        return 1;
    }

    fss_in = fopen("fss_in", "w");
    fss_out = fopen("fss_out", "r");

    while (1)
    {
        printf("> ");
        fflush(stdout);

        if (fgets(input, sizeof(input), stdin) == NULL)
        {
            continue;
        }

        // Check if the input was too long (missing newline)
        if (strchr(input, '\n') == NULL)
        {
            printf("Error: input too long (maximum %d characters)\n", BUFFER_SIZE - 1);

            // Clear the rest of the line from stdin
            int c;
            while ((c = getchar()) != '\n' && c != EOF)
                ;
            continue;
        }

        // Remove newline and trailing spaces
        input[strcspn(input, "\n")] = 0; // Remove newline
        size_t len = strlen(input);
        while (len > 0 && (input[len - 1] == ' ' || input[len - 1] == '\t')) // Remove trailing spaces
        {
            input[len - 1] = '\0';
            len--;
        }

        // Remove leading spaces
        char *trimmed_input = input + strspn(input, " \t");

        if (strcmp(trimmed_input, "exit") == 0)
        {
            break;
        }

        if (fss_in == NULL || fss_out == NULL)
        {
            if (fss_in != NULL)
            {
                fclose(fss_in);
            }
            if (fss_out != NULL)
            {
                fclose(fss_out);
            }
            fss_in = fopen("fss_in", "w");
            fss_out = fopen("fss_out", "r");
            if (fss_in == NULL || fss_out == NULL)
            {
                printf("Error: have you opened the fss_manager server?\n");
                continue;
            }
        }

        fprintf(fss_in, "%s\n", trimmed_input);
        fflush(fss_in);

        while (fgets(output, sizeof(output), fss_out))
        {
            size_t size = strlen(output);
            if (size > 0 && output[size - 1] == '\n')
            {
                output[size - 1] = '\0';
            }

            if (strcmp(output, "COMMAND_FINISHED") == 0)
            {
                printf("\n");
                break;
            }

            printf("%s\n", output);
            fprintf(log, "%s\n", output);
            fflush(log);
        }

        if (strcmp(trimmed_input, "shutdown") == 0)
        {
            break;
        }
    }

    if (fss_in != NULL)
    {
        fclose(fss_in);
    }
    if (fss_out != NULL)
    {
        fclose(fss_out);
    }
    fclose(log);
    return 0;
}
