#include <unistd.h>     // for fork(), execvp()
#include <sys/wait.h>   // for wait()
#include <stdlib.h>     // for exit()
#include <stdio.h>      // for printf(), perror()
#include <string.h>     // for string functions
#include <sys/time.h>   // for time
#include <fcntl.h>  // for open, O_WRONLY, O_CREAT, O_TRUNC
#include <string.h>     // for strstr
#include <sys/resource.h>
#include <ctype.h> // for isdigit
#include <pthread.h> // Make sure this is included!





#define MAX_DANGEROUS 100
#define MAX_LINE_LENGTH 1024
#define MAX_MATRICES 100


// Function declarations
int read_user_input(char *input, char *clean_copy);
void parse_command(char *input, char *args[]);
void execute_command(char *args[], char *original_cmd, int background);
int is_dangerous_command(char *input, char *command_name, char dangerous_commands[][MAX_LINE_LENGTH], int dangerous_count);
int is_pipe_command(char *input_line);
void handle_pipe_command(char *input_line);
void update_time_and_log(char *cmd_str, char *log_filename);
void handle_my_tee(char *filename, int append);
int is_stderr_redirect(char *input_line);
void handle_stderr_redirect(char *input_line);
void handle_my_tee_command(char *input_line);

typedef struct {
    int rows;       // Number of rows
    int cols;       // Number of columns
    int *data;      // Pointer to matrix data stored as a flat 1D array
} Matrix;
typedef struct {
    Matrix *m1;        // Pointer to the first input matrix
    Matrix *m2;        // Pointer to the second input matrix
    Matrix *result;    // Pointer to the output matrix (calculated result)
    int op;            // Operation: 0 for ADD, 1 for SUB
} ThreadData;

void handle_stderr_redirect(char *input_line);
int load_matrices(int argc, char *argv[], Matrix matrices[], int *count);
int parse_matrix(char *str, Matrix *m);
int load_matrices(int argc, char *argv[], Matrix matrices[], int *count);
void *matrix_worker(void *arg);
int compute_matrices(Matrix matrices[], int count, char *operation, Matrix *result);
void print_matrix(Matrix *m);
void handle_mcalc_command(char *line);
int validate_input(int argc, char *argv[]);
void run_vmem(const char *script_filename);



// for calculating the time
int command_count = 0;        // Total number of legal commands executed
double last_cmd_time = 0.0;   // Last command runtime in seconds

char *log_file = NULL; //


struct timeval start_time;
struct timeval end_time;

double avg_time = 0.0;
double min_time = -1.0;
double max_time = 0.0;

char left_cmd[MAX_LINE_LENGTH];   //the left side of the pipe
char right_cmd[MAX_LINE_LENGTH];  // the right side of the pipe

int main(int argc, char *argv[])
{
    // Check that two files were provided: dangerous list and log file
    if (argc != 3) {
        printf("Usage: %s <dangerous_commands_file> <log_file>\n", argv[0]);
        return 1;
    }

    // File paths from command line arguments
    char *dangerous_commands_file = argv[1];
    log_file = argv[2];

    // Load dangerous commands into array
    char dangerous_commands[MAX_DANGEROUS][MAX_LINE_LENGTH];
    int dangerous_count = 0;

    FILE *dangerous_fp = fopen(dangerous_commands_file, "r");
    if (!dangerous_fp) {
        perror("Error opening dangerous commands file");
        return 1;
    }

    while (fgets(dangerous_commands[dangerous_count], MAX_LINE_LENGTH, dangerous_fp)) {
        dangerous_commands[dangerous_count][strcspn(dangerous_commands[dangerous_count], "\n")] = 0;
        dangerous_count++;
    }
    fclose(dangerous_fp);

    // Prepare user input and tracking variables
    char input[MAX_LINE_LENGTH];
    char input_copy[MAX_LINE_LENGTH];
    char *args[10];
    int dangerous_blocked = 0;

    Matrix matrices[MAX_MATRICES];
int matrix_count;


    // Start shell loop
    while (1) {
        printf("#cmd:%d|#dangerous_cmd_blocked:%d|last_cmd_time:%.5f|avg_time:%.5f|min_time:%.5f|max_time:%.5f>> ",
               command_count, dangerous_blocked, last_cmd_time, avg_time, (min_time < 0 ? 0.0 : min_time), max_time);  // print 0.0 if no command yet

        if (!read_user_input(input, input_copy))
            continue;

        input_copy[strcspn(input_copy, "\n")] = '\0';  // remove newline for accurate strcmp

        // check for "done" before parsing and executing
        if (strcmp(input_copy, "done\n") == 0 || strcmp(input_copy, "done") == 0) {
            printf("%d\n", dangerous_blocked);
            break;
        }

        if (is_pipe_command(input_copy)) {// Check if the command contains a pipe
            handle_pipe_command(input_copy);
            continue;
        }

        if (strstr(input_copy, "2>") != NULL) {
            handle_stderr_redirect(input_copy);
            continue;
        }

        if (strncmp(input_copy, "my_tee ", 7) == 0) {
            char *filename = input_copy + 7;
            handle_my_tee_command(input_copy);
            continue;
        }
        if (strncmp(input_copy, "mcalc", 5) == 0) {
            handle_mcalc_command(input_copy);
            continue;
        }
        // Handle "vmem" command: run virtual memory simulation
        if (strncmp(input_copy, "vmem ", 5) == 0) {
            char filename[128]; // Buffer for script file name
            sscanf(input_copy, "vmem %s", filename); // Extract file name
            run_vmem(filename); // Call vmem handler
            continue; // Skip rest of loop
        }






        parse_command(input, args);

        int danger = is_dangerous_command(input_copy, args[0], dangerous_commands, dangerous_count);

        if (danger == 1) {
            printf("ERR: Dangerous command detected (\"%s\"). Execution prevented.\n", input_copy);
            dangerous_blocked++;
            continue;
        } else if (danger == 2) {
            printf("WARNING: Command similar to dangerous command (\"%s\"). Proceed with caution.\n", input_copy);
        }

        int background = 0;
        int len = strlen(input_copy);
        if (len > 0 && input_copy[len - 1] == '&') {
            background = 1;
            input_copy[len - 1] = '\0';  // Remove '&'
            if (len > 1 && input_copy[len - 2] == ' ')
                input_copy[len - 2] = '\0';  // Remove space before '&'
        }


        parse_command(input, args);

        gettimeofday(&start_time, NULL);
        execute_command(args, input_copy, background);


        //Matrix matrices[MAX_MATRICES];
        int matrix_count;

        if (!load_matrices(argc, argv, matrices, &matrix_count)) {
            return 1;  // Error while parsing matrices
        }


    }

    return 0;
}

// Reads input, copies it to a backup, and checks validity
int read_user_input(char *input, char *clean_copy) {
    if (fgets(input, MAX_LINE_LENGTH, stdin) == NULL) return 0;
    strcpy(clean_copy, input);

    if (strchr(input, '\n') == NULL) {
        printf("ERR: command too long\n");
        int ch;
        while ((ch = getchar()) != '\n' && ch != EOF);
        return 0;
    }

    for (int i = 0; input[i] != '\0'; i++) {
        if (input[i] == ' ' && input[i + 1] == ' ') {
            printf("ERR_SPACE\n");
            return 0;
        }
    }

    int count = 0;
    char temp[MAX_LINE_LENGTH];
    strcpy(temp, input);
    char *token = strtok(temp, " \n");
    while (token != NULL) {
        count++;
        token = strtok(NULL, " \n");
    }

    if (strncmp(clean_copy, "rlimit set", 10) != 0 && count > 7) {
        printf("ERR_ARGS\n");
        return 0;
    }


    return 1;
}

// Splits input into arguments for execvp
void parse_command(char *input, char *args[]) {
    int i = 0;
    char *token = strtok(input, " \n");
    while (token != NULL) {
        args[i++] = token;
        token = strtok(NULL, " \n");
    }
    args[i] = NULL;
}

// Fork and run command with execvp
void execute_command(char *args[], char *original_cmd, int background) {
    pid_t pid = fork();  // Create a child process
    if (pid < 0) {
        perror("fork failed");
        printf("ERR\n");
        exit(1);
    }
    if (pid == 0) {
        // Child process: execute the command
        execvp(args[0], args);

        // If execvp fails, print error and exit with code 1
        perror("execvp failed");
        exit(1);
    } else {
        if (!background) {
            int status;
            pid_t waited_pid = waitpid(pid, &status, 0);

            if (waited_pid == -1) {
                perror("waitpid failed");
                return;
            }

            // Check if child terminated normally
            if (WIFEXITED(status)) {
                int exit_code = WEXITSTATUS(status);
                if (exit_code != 0) {
                    // Command returned error code
                    printf("Command exited with error code: %d\n", exit_code);
                    return; // Do not update time or log
                }
            } else if (WIFSIGNALED(status)) {
                int sig = WTERMSIG(status);
                printf("Command was terminated by signal: %s\n", strsignal(sig));
                return; // Do not update time or log
            }

            // If we got here, the command succeeded – now measure time and update log
            gettimeofday(&end_time, NULL);
            update_time_and_log(original_cmd, log_file);
        } else {
            // For background processes: do not measure time or update log
            // (per project instructions, only foreground + success updates log)
        }
    }
}



void update_time_and_log(char *cmd_str, char *log_filename) {
    gettimeofday(&end_time, NULL);

    last_cmd_time = (end_time.tv_sec - start_time.tv_sec) + (end_time.tv_usec - start_time.tv_usec) / 1000000.0;

    command_count++;
    avg_time = ((avg_time * (command_count - 1)) + last_cmd_time) / command_count;

    if (min_time < 0 || last_cmd_time < min_time)
        min_time = last_cmd_time;

    if (last_cmd_time > max_time)
        max_time = last_cmd_time;

    FILE *log_fp = fopen(log_filename, "a");
    if (log_fp != NULL) {
        fprintf(log_fp, "%s : %.5f sec\n", cmd_str, last_cmd_time);
        fclose(log_fp);
    } else {
        perror("Failed to open log file");
    }
}


// Checks if the input matches a dangerous command
int is_dangerous_command(char *input, char *command_name, char dangerous_commands[][MAX_LINE_LENGTH], int dangerous_count) {
    for (int i = 0; i < dangerous_count; i++) {
        if (strcmp(input, dangerous_commands[i]) == 0) return 1;

        char copy[MAX_LINE_LENGTH];
        strcpy(copy, dangerous_commands[i]);
        char *dangerous_cmd = strtok(copy, " ");
        if (dangerous_cmd != NULL && strcmp(dangerous_cmd, command_name) == 0) {
            return 2;
        }
    }
    return 0;
}


int is_pipe_command(char *input_line) {
    return strstr(input_line, " | ") != NULL;
}

void handle_pipe_command(char *input_line)
{
    char *left_args[10];   // Arguments for the left-side command
    char *right_args[10];  // Arguments for the right-side command

    // Find the position of the pipe delimiter " | "
    char *pipe_ptr = strstr(input_line, " | ");
    if (pipe_ptr == NULL) {
        printf("ERROR: Invalid pipe format.\n");
        return;
    }

    *pipe_ptr = '\0';                      // Split the input into two strings
    strcpy(left_cmd, input_line);         // Store left command
    strcpy(right_cmd, pipe_ptr + 3);      // Store right command (skip " | ")

    gettimeofday(&start_time, NULL);      // Start measuring time

    parse_command(left_cmd, left_args);   // Tokenize left command
    parse_command(right_cmd, right_args); // Tokenize right command

    int pipefd[2];                         // pipefd[0] is read end, pipefd[1] is write end
    if (pipe(pipefd) == -1) {              // Create pipe
        perror("pipe");
        return;
    }

    pid_t pid1 = fork();                   // Fork first child (left command)
    if (pid1 < 0) {
        perror("fork");
        return;
    }

    if (pid1 == 0) {
        // In child 1 (left-side command)
        close(pipefd[0]);                 // Close unused read end
        dup2(pipefd[1], STDOUT_FILENO);   // Redirect stdout to pipe
        close(pipefd[1]);                 // Close original write end

        execvp(left_args[0], left_args);  // Execute left command
        perror("execvp failed");          // Only prints if exec fails
        exit(1);
    }

    pid_t pid2 = fork();                   // Fork second child (right command)
    if (pid2 < 0) {
        perror("fork");
        return;
    }

    if (pid2 == 0) {
        // In child 2 (right-side command)
        close(pipefd[1]);                 // Close unused write end
        dup2(pipefd[0], STDIN_FILENO);    // Redirect stdin from pipe
        close(pipefd[0]);                 // Close original read end

        // Handle internal my_tee command
        if (strcmp(right_args[0], "my_tee") == 0) {
            // connect stdin of my_tee to pipe
            dup2(pipefd[0], STDIN_FILENO);
            close(pipefd[0]);

            int append = 0;
            char *filename = NULL;

            if (strcmp(right_args[1], "-a") == 0) {
                append = 1;
                filename = right_args[2];
            } else {
                filename = right_args[1];
            }

            handle_my_tee(filename, append);
            exit(0);
        }


        execvp(right_args[0], right_args);           // Execute right command
        perror("execvp failed");
        exit(1);
    }

    // Parent process continues here
    close(pipefd[0]);               // Close both ends of the pipe
    close(pipefd[1]);

    waitpid(pid1, NULL, 0);         // Wait for first child
    waitpid(pid2, NULL, 0);         // Wait for second child

    gettimeofday(&end_time, NULL);  // Stop measuring time

    char full_cmd[MAX_LINE_LENGTH * 4];  // Build full command string
    snprintf(full_cmd, sizeof(full_cmd), "%s | %s", left_cmd, right_cmd);

    update_time_and_log(full_cmd, log_file); // Log_
}

void handle_my_tee(char *filename, int append) {
    gettimeofday(&start_time, NULL);

    int flags = O_WRONLY | O_CREAT;
    if (append)
        flags |= O_APPEND;
    else
        flags |= O_TRUNC;

    int out_fd = open(filename, flags, 0644);
    if (out_fd < 0) {
        perror("open failed");
        return;
    }

    char buffer[1024];
    ssize_t bytes_read;

    while ((bytes_read = read(STDIN_FILENO, buffer, sizeof(buffer))) > 0) {
        if (write(STDOUT_FILENO, buffer, bytes_read) != bytes_read) {
            perror("write to stdout failed");
            close(out_fd);
            return;
        }
        if (write(out_fd, buffer, bytes_read) != bytes_read) {
            perror("write to file failed");
            close(out_fd);
            return;
        }
    }

    if (bytes_read < 0) {
        perror("read failed");
    }

    close(out_fd);
    gettimeofday(&end_time, NULL);
    update_time_and_log(append ? "my_tee -a" : "my_tee", log_file);
}

void handle_my_tee_command(char *input_line) {
    // Parse the input to extract arguments
    char *args[4];  // Expecting at most: my_tee -a filename + NULL
    int count = 0;

    char *token = strtok(input_line, " \n");
    while (token != NULL && count < 3) {
        args[count++] = token;
        token = strtok(NULL, " \n");
    }
    args[count] = NULL;

    if (count < 2) {
        printf("Usage: my_tee [-a] <output_file>\n");
        return;
    }

    int append = 0;
    char *filename;

    if (strcmp(args[1], "-a") == 0) {
        // Append mode
        if (count < 3) {
            printf("ERR: missing output filename after -a\n");
            return;
        }
        append = 1;
        filename = args[2];
    } else {
        // No -a → regular overwrite
        filename = args[1];
    }

    handle_my_tee(filename, append);
}





// Check if the command contains a '2>' for stderr redirection
void handle_stderr_redirect(char *input_line)
{
    // Find the redirection symbol "2>"
    char *redir_ptr = strstr(input_line, "2>");
    if (redir_ptr == NULL) {
        printf("ERR: Invalid redirection format.\n");
        return;
    }

    // Split the line into command and file
    *redir_ptr = '\0';
    char *command_part = input_line;
    char *file_part = redir_ptr + 2;

    while (*file_part == ' ') file_part++;

    char *args[10];
    parse_command(command_part, args);

    pid_t pid = fork();
    if (pid < 0) {
        perror("fork failed");
        return;
    }

    if (pid == 0) {
        // Child
        int err_fd = open(file_part, O_WRONLY | O_CREAT | O_APPEND, 0644);
        if (err_fd < 0) {
            perror("open failed");
            exit(1);
        }
        dup2(err_fd, STDERR_FILENO);
        close(err_fd);

        execvp(args[0], args);
        perror("execvp failed");
        exit(1);
    } else {
        // Parent
        int status;
        pid_t waited_pid = waitpid(pid, &status, 0);

        if (waited_pid == -1) {
            perror("waitpid failed");
            return;
        }

        if (WIFEXITED(status)) {
            int exit_code = WEXITSTATUS(status);
            if (exit_code != 0) {
                printf("Command exited with error code: %d\n", exit_code);
                return;  // Don't log failed command
            }
        } else if (WIFSIGNALED(status)) {
            int sig = WTERMSIG(status);
            printf("Command was terminated by signal: %s\n", strsignal(sig));
            return;  // Don't log signal-terminated command
        }

        // Only if command succeeded, log it
        gettimeofday(&end_time, NULL);
        update_time_and_log(input_line, log_file);

    }
}

void handle_mcalc_command(char *line) {
    char *args[100];             // Array to hold the parsed arguments
    int argc = 0;                // Argument counter

    char *copy = strdup(line);   // Make a modifiable copy of the input line
    if (copy == NULL) {
        printf("ERR: strdup failed\n");
        return;
    }

    // Skip the first token ("mcalc") — assume it's always there
    char *p = copy;
    while (isspace(*p)) p++;          // Skip leading spaces
    while (*p && !isspace(*p)) p++;   // Skip over "mcalc"
    while (isspace(*p)) p++;          // Skip space after "mcalc"

    // Parse the rest using support for quoted strings
    while (*p != '\0') {
        while (isspace(*p)) p++;  // Skip spaces before argument

        if (*p == '"') {
            p++;  // Skip opening quote
            char *start = p;
            while (*p && *p != '"') p++;  // Read until closing quote
            if (*p == '\0') break;
            *p = '\0';  // Null-terminate the argument
            args[argc++] = start;
            p++;  // Skip the closing quote
        } else {
            char *start = p;
            while (*p && !isspace(*p)) p++;  // Read until space
            if (*p) *p = '\0';               // Null-terminate
            args[argc++] = start;
            p++;
        }
    }



    // Create fake argv with "mcalc" as argv[0]
    char *fake_argv[101];
    fake_argv[0] = "mcalc";  // Dummy program name
    for (int i = 0; i < argc; i++) {
        fake_argv[i + 1] = args[i];
    }

    // Validate the input (with argc+1)
    if (!validate_input(argc + 1, fake_argv)) {
        free(copy);
        return;
    }

    // Load matrices from the arguments
    Matrix matrices[MAX_MATRICES];
    int matrix_count;

    if (!load_matrices(argc + 1, fake_argv, matrices, &matrix_count)) {
        free(copy);
        return;
    }

    // Perform the matrix computation
    Matrix result;
    if (!compute_matrices(matrices, matrix_count, fake_argv[argc], &result)) {
        printf("ERR: Computation failed\n");
        for (int i = 0; i < matrix_count; i++) {
    free(matrices[i].data);  // Free memory allocated for each matrix
}

        free(copy);
        return;
    }

    // Print the result
    print_matrix(&result);

    // Free memory
    free(result.data);
    free(copy);
}



int compute_matrices(Matrix matrices[], int count, char *operation, Matrix *result) {
    int op_code;

    // Convert operation string to a numeric code (0 for ADD, 1 for SUB)
    if (strcmp(operation, "ADD") == 0) {
        op_code = 0;
    } else if (strcmp(operation, "SUB") == 0) {
        op_code = 1;
    } else {
        printf("ERR: Unknown operation\n");
        return 0;
    }

    int current_count = count;         // Number of matrices to work with
    Matrix *current = matrices;        // Pointer to current array of matrices

    Matrix temp_results[MAX_MATRICES]; // Temporary array to store results per round

    // Repeat until only one result remains
    while (current_count > 1) {
        int thread_count = current_count / 2; // Number of thread pairs
        pthread_t threads[thread_count];      // Array of threads
        ThreadData thread_data[thread_count]; // Data for each thread
        int result_index = 0;

        // Launch threads for each pair of matrices
        for (int i = 0; i < thread_count; i++) {
            thread_data[i].m1 = &current[i * 2];          // First matrix of the pair
            thread_data[i].m2 = &current[i * 2 + 1];       // Second matrix of the pair
            thread_data[i].result = &temp_results[result_index]; // Where to store result
            thread_data[i].op = op_code;                  // Operation code (0 or 1)

            pthread_create(&threads[i], NULL, matrix_worker, &thread_data[i]);
            result_index++;
        }

        // If there’s an unpaired matrix, move it to next round as-is
        if (current_count % 2 != 0) {
            temp_results[result_index] = current[current_count - 1];
            result_index++;
        }

        // Wait for all threads to complete
        for (int i = 0; i < thread_count; i++) {
            pthread_join(threads[i], NULL);
        }

        // Move to next round with updated matrix list
        current = temp_results;
        current_count = result_index;
    }

    // Copy final result into provided Matrix pointer safely
    result->rows = current[0].rows;
    result->cols = current[0].cols;
    int size = result->rows * result->cols;

    result->data = (int *)malloc(sizeof(int) * size);  // Allocate memory
    if (result->data == NULL) {
        perror("malloc failed");
        return 0;
    }

    // Copy data from the final computed matrix into result
    for (int i = 0; i < size; i++) {
        result->data[i] = current[0].data[i];
    }

    return 1; // Success
}

int parse_matrix(char *str, Matrix *m) {
    int len = strlen(str);  // Get length of the input string

    if (str[0] != '(' || str[len - 1] != ')') {
        return 0;  // Matrix must start with '(' and end with ')'
    }

    // Copy the inside part of the string (excluding the parentheses)
    char inner[1024];
    strncpy(inner, str + 1, len - 2);   // Copy from str[1] up to before last ')'
    inner[len - 2] = '\0';              // Add null terminator to make it a proper string

    // Split the string by ':' into size and values
    char *size_part = strtok(inner, ":");      // First part: "rows,cols"
    char *values_part = strtok(NULL, ":");     // Second part: "val1,val2,..."

    if (size_part == NULL || values_part == NULL) {
        return 0;  // Invalid format – missing parts
    }

    // Split size_part into row and column strings
    char *row_str = strtok(size_part, ",");     // Get rows
    char *col_str = strtok(NULL, ",");          // Get columns

    if (row_str == NULL || col_str == NULL) {
        return 0;  // Invalid size format
    }

    m->rows = atoi(row_str);  // Convert rows from string to integer
    m->cols = atoi(col_str);  // Convert cols from string to integer

    int total_values = m->rows * m->cols;       // How many numbers we expect

    // Allocate memory for matrix data (1D array)
    m->data = (int *)malloc(sizeof(int) * total_values);
    if (m->data == NULL) {
        perror("malloc failed");                // Print system error message
        return 0;                                // Memory allocation failed
    }

    // Parse and store the values into the matrix
    int index = 0;
    char *val_str = strtok(values_part, ",");  // Get first value
    while (val_str != NULL && index < total_values) {
        m->data[index++] = atoi(val_str);      // Convert and store the value
        val_str = strtok(NULL, ",");           // Move to next value
    }

    if (index != total_values) {
        free(m->data);                         // Free allocated memory if failed
        return 0;                              // Wrong number of values
    }

    return 1;  // Matrix parsed successfully
}



int load_matrices(int argc, char *argv[], Matrix matrices[], int *count) {
    int matrix_count = argc - 2;  // Total number of matrices (excluding program name and operation)

    if (matrix_count > MAX_MATRICES) {
        printf("ERR: Too many matrices\n");
        return 0;
    }

    for (int i = 0; i < matrix_count; i++) {
        if (!parse_matrix(argv[i + 1], &matrices[i])) {
            printf("ERR: Failed to parse matrix %d\n", i + 1);
            return 0;  // Stop if any matrix is invalid
        }
    }

    *count = matrix_count;  // Return number of matrices successfully parsed
    return 1;  // Success
}

// Thread function that performs matrix addition or subtraction
void *matrix_worker(void *arg) {
    ThreadData *data = (ThreadData *)arg;  // Cast the input argument to our struct

    int rows = data->m1->rows;             // Get number of rows from first matrix
    int cols = data->m1->cols;             // Get number of columns
    int size = rows * cols;                // Total number of elements in matrix

    // Allocate memory for result matrix
    data->result->rows = rows;             // Set result matrix rows
    data->result->cols = cols;             // Set result matrix cols
    data->result->data = (int *)malloc(sizeof(int) * size);  // Allocate flat array

    if (data->result->data == NULL) {      // Check for memory allocation failure
        perror("malloc failed");
        pthread_exit(NULL);                // Exit the thread
    }

    // Perform the operation: ADD or SUB
    for (int i = 0; i < size; i++) {
        if (data->op == 0) {               // If operation is ADD
            data->result->data[i] = data->m1->data[i] + data->m2->data[i];
        } else {                           // Otherwise, perform SUB
            data->result->data[i] = data->m1->data[i] - data->m2->data[i];
        }
    }

    pthread_exit(NULL);  // Exit the thread after calculation
}




int validate_input(int argc, char *argv[]) {
    if (argc < 4) {
        printf("ERR_MAT_INPUT\n");      // Must have at least 2 matrices + 1 operation
        return 0;
    }

    char *operation = argv[argc - 1];
    if (strcmp(operation, "ADD") != 0 && strcmp(operation, "SUB") != 0) {
        printf("ERR_MAT_INPUT\n");      // Operation must be ADD or SUB
        return 0;
    }

    int expected_rows = -1;             // Will store size from the first matrix
    int expected_cols = -1;

    for (int i = 1; i < argc - 1; i++) {
        char *matrix_str = argv[i];     // Get the matrix string
        int len = strlen(matrix_str);

        if (strchr(matrix_str, ' ') != NULL) {
            printf("ERR_MAT_INPUT\n");  // Matrix string contains illegal space
            return 0;
        }

        if (matrix_str[0] != '(' || matrix_str[len - 1] != ')') {
            printf("ERR_MAT_INPUT\n");  // Matrix must start with ( and end with )
            return 0;
        }

        for (int j = 0; j < len; j++) {
            char c = matrix_str[j];
            if (!(isdigit(c) || c == ',' || c == ':' || c == '(' || c == ')')) {
                printf("ERR_MAT_INPUT\n");  // Contains illegal character
                return 0;
            }
        }

        char inner_str[1024];
        strncpy(inner_str, matrix_str + 1, len - 2);   // Copy without parentheses
        inner_str[len - 2] = '\0';

        char *size_part = strtok(inner_str, ":");      // Part before ':' is size
        char *values_part = strtok(NULL, ":");         // Part after ':' is values

        if (size_part == NULL || values_part == NULL) {
            printf("ERR_MAT_INPUT\n");  // Missing size or values
            return 0;
        }

        char *row_str = strtok(size_part, ",");        // Get rows string
        char *col_str = strtok(NULL, ",");             // Get cols string

        if (row_str == NULL || col_str == NULL) {
            printf("ERR_MAT_INPUT\n");  // Invalid size format
            return 0;
        }

        int rows = atoi(row_str);       // Convert to int
        int cols = atoi(col_str);

        if (i == 1) {
            expected_rows = rows;       // Save the size from first matrix
            expected_cols = cols;
        } else {
            if (rows != expected_rows || cols != expected_cols) {
                printf("ERR_MAT_INPUT\n");  // Sizes don't match
                return 0;
            }
        }

        int value_count = 0;
        char *val = strtok(values_part, ",");   // Count number of values
        while (val != NULL) {
            value_count++;
            val = strtok(NULL, ",");
        }

        if (value_count != rows * cols) {
            printf("ERR_MAT_INPUT\n");  // Wrong number of values
            return 0;
        }
    }

    return 1;  // All inputs are valid
}
void print_matrix(Matrix *m) {
    int rows = m->rows;       // Get number of rows from the matrix
    int cols = m->cols;       // Get number of columns
    int size = rows * cols;   // Total number of values in the matrix

    printf("(%d,%d:", rows, cols);  // Print the size part of the format

    // Print all the values one by one
    for (int i = 0; i < size; i++) {
        printf("%d", m->data[i]);   // Print the value

        if (i < size - 1) {
            printf(",");            // Print comma between values (not after the last one)
        }
    }

    printf(")\n");  // Close the format with ')'
}


