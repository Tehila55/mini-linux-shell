#include <stdio.h>          // For printf, fopen, etc.
#include <stdlib.h>         // For malloc, exit
#include <string.h>         // For string functions
#include <fcntl.h>          // For open
#include <unistd.h>         // For close

// Define the page descriptor struct
typedef struct {
    int V;         // Valid bit
    int D;         // Dirty bit
    int P;         // Permission bit (1 = read-only, 0 = read/write)
    int frame_swap; // Frame number or swap page
} page_descriptor;

// Define the TLB entry struct
typedef struct {
    int page_number;
    int frame_number;
    int valid;
    int timestamp;
} tlb_entry;

// Define the sim_database struct
typedef struct sim_database {
    page_descriptor* page_table; // Array of page descriptors
    int swapfile_fd;             // Swap file descriptor
    int program_fd;              // Program file descriptor
    char* main_memory;           // Pointer to main memory array
    int text_size;               // Text segment size
    int data_size;               // Data segment size
    int bss_size;                // BSS segment size
    int heap_stack_size;         // Heap and stack size
    tlb_entry* tlb;              // TLB array (optional)
    int page_size;               // Page size in bytes
    int num_pages;               // Total number of pages
    int memory_size;             // Main memory size in bytes
    int swap_size;               // Swap file size in bytes
    int num_frames;              // Number of frames
    int tlb_size;                // TLB size (if implemented)
    int *lru_queue;    // Array to hold LRU pages
    int queue_size;    // Number of loaded pages
} sim_database;


int init_system(sim_database *mem_sim, const char *script_file_name);
void load_virtual(sim_database *mem_sim, int address);
void store_virtual(sim_database *mem_sim, int address, char value);
void print_page_table(sim_database* mem_sim);
void print_memory(sim_database* mem_sim);
void print_swap(sim_database* mem_sim);
void print_page_table(sim_database *mem_sim);
void print_memory(sim_database *mem_sim);
void print_swap(sim_database *mem_sim);
void run_vmem(const char *script_filename);


void run_vmem(const char *script_filename) {
    sim_database mem_sim;  // Struct holding simulation data

    // Open the script file for reading commands and initialization
    FILE *script = fopen(script_filename, "r");
    if (!script) {
        fprintf(stderr, "Error: Cannot open script file %s\n", script_filename);
        return;
    }

    // Read the first line with initialization parameters
    char params_line[256];
    if (!fgets(params_line, sizeof(params_line), script)) {
        fclose(script);
        fprintf(stderr, "Error reading script parameters\n");
        return;
    }

    // Initialize the system using the parameters line
    if (init_system(&mem_sim, params_line)) {
        fclose(script);
        return;
    }

    // Print debug marker before processing commands
    printf("[DEBUG] Starting to process commands...\n");

    char line[256]; // Buffer to hold each command line

    // Read each line in the script and process it
    while (fgets(line, sizeof(line), script)) {
        printf("[DEBUG] Raw line read: '%s'\n", line); // Debug print

        line[strcspn(line, "\r\n")] = 0; // Remove trailing newline

        if (strlen(line) == 0)
            continue; // Skip empty lines

        if (strncmp(line, "load", 4) == 0) {
            int addr;
            sscanf(line, "load %d", &addr); // Extract address
            load_virtual(&mem_sim, addr);   // Call load function
        }
        else if (strncmp(line, "store", 5) == 0) {
            int addr;
            char val;
            sscanf(line, "store %d %c", &addr, &val); // Extract address and char
            store_virtual(&mem_sim, addr, val);       // Call store function
        }
        else if (strcmp(line, "print table") == 0) {
            print_page_table(&mem_sim); // Print page table
        }
        else if (strcmp(line, "print ram") == 0) {
            print_memory(&mem_sim); // Print RAM contents
        }
        else if (strcmp(line, "print swap") == 0) {
            print_swap(&mem_sim); // Print swap contents
        }
        else {
            printf("Unknown command: %s\n", line); // Handle unknown command
        }
    }

    // Close the script file when finished
    fclose(script);
}


  /**
 * Initializes the simulation database from the parameters line.
 * No file opening happens here.
 */
int init_system(sim_database *mem_sim, const char *params_line) {
    char prog_name[128], swap_name[128]; // Buffers for file names
    int text_size, data_size, bss_size, heap_stack_size;
    int page_size, num_pages, memory_size, swap_size;

    // Parse the parameters line into variables
    int read_params = sscanf(params_line, "%s %s %d %d %d %d %d %d %d %d",
                             prog_name, swap_name,
                             &text_size, &data_size, &bss_size, &heap_stack_size,
                             &page_size, &num_pages, &memory_size, &swap_size);

    if (read_params != 10) {
        fprintf(stderr, "Error: Invalid script format\n");
        return 1; // Abort on format error
    }

    // Store parsed values in the sim_database struct
    mem_sim->text_size = text_size;
    mem_sim->data_size = data_size;
    mem_sim->bss_size = bss_size;
    mem_sim->heap_stack_size = heap_stack_size;
    mem_sim->page_size = page_size;
    mem_sim->num_pages = num_pages;
    mem_sim->memory_size = memory_size;
    mem_sim->swap_size = swap_size;
    mem_sim->num_frames = memory_size / page_size;
    mem_sim->lru_queue = (int*)malloc(sizeof(int) * mem_sim->num_frames); // Allocate LRU queue
    mem_sim->queue_size = 0;

    // Open the program file
    mem_sim->program_fd = open(prog_name, O_RDONLY);
    if (mem_sim->program_fd < 0) {
        perror("Error opening program file");
        return 1;
    }

    // Open or create the swap file
    mem_sim->swapfile_fd = open(swap_name, O_RDWR | O_CREAT, 0600);
    if (mem_sim->swapfile_fd < 0) {
        perror("Error creating/opening swap file");
        close(mem_sim->program_fd);
        return 1;
    }

    // Allocate the page table
    mem_sim->page_table = (page_descriptor *)malloc(sizeof(page_descriptor) * num_pages);
    if (!mem_sim->page_table) {
        perror("Error allocating page table");
        close(mem_sim->program_fd);
        close(mem_sim->swapfile_fd);
        return 1;
    }

    // Allocate the main memory
    mem_sim->main_memory = (char *)malloc(memory_size);
    if (!mem_sim->main_memory) {
        perror("Error allocating main memory");
        free(mem_sim->page_table);
        close(mem_sim->program_fd);
        close(mem_sim->swapfile_fd);
        return 1;
    }

    // Fill main memory with '-' characters
    memset(mem_sim->main_memory, '-', memory_size);

    // Initialize each page descriptor
    for (int i = 0; i < num_pages; i++) {
        page_descriptor *pd = &mem_sim->page_table[i];
        pd->V = 0; // Not loaded
        pd->D = 0; // Not dirty
        pd->P = (i * page_size < text_size) ? 1 : 0; // Read-only or Read/Write
        pd->frame_swap = -1; // Not mapped yet
    }

    // Print success message
    printf("Loaded program \"%s\" with text=%d, data=%d, bss=%d, heap_stack=%d.\n",
           prog_name, text_size, data_size, bss_size, heap_stack_size);

    return 0; // Initialization successful
}



// This function  reading a value from a virtual address
void load_virtual(sim_database *mem_sim, int address) {

    // Check if the address is within valid range
    if (address < 0 || address >= mem_sim->num_pages * mem_sim->page_size) {
        fprintf(stderr, "Error: Invalid address %d (out of range)\n", address);
        return;
    }

    // Calculate page number and offset within the page
    int page = address / mem_sim->page_size;           // Compute which page
    int offset = address % mem_sim->page_size;         // Compute offset inside page

    page_descriptor *pd = &mem_sim->page_table[page];  // Get pointer to page descriptor

    // If page is already loaded in memory
    if (pd->V == 1) {
        int frame = pd->frame_swap;                    // Frame where page is loaded
        int physical_address = frame * mem_sim->page_size + offset; // Calculate physical address
        char value = mem_sim->main_memory[physical_address];        // Read the value
        printf("Value at address %d = %c\n", address, value);       // Print value
        return;
    }

    // No free frame found yet
    int frame = -1;

    // Try to find a free frame
    for (int i = 0; i < mem_sim->num_frames; i++) {
        int used = 0;                                   // Flag to check if frame is used
        for (int p = 0; p < mem_sim->num_pages; p++) {
            if (mem_sim->page_table[p].V == 1 && mem_sim->page_table[p].frame_swap == i) {
                used = 1;                               // This frame is used by a page
                break;
            }
        }
        if (!used) {
            frame = i;                                  // Found a free frame
            break;
        }
    }

    // If no free frame, evict least recently used page
    if (frame == -1) {
        int victim_page = mem_sim->lru_queue[0];          // Oldest page in LRU queue
        page_descriptor *victim_pd = &mem_sim->page_table[victim_page];
        int victim_frame = victim_pd->frame_swap;

        // If victim page is dirty, write it to swap
        if (victim_pd->D == 1) {
            printf("Page replacement: Evicting page %d to swap\n", victim_page);
            lseek(mem_sim->swapfile_fd, victim_page * mem_sim->page_size, SEEK_SET);
            write(mem_sim->swapfile_fd,
                  mem_sim->main_memory + victim_frame * mem_sim->page_size,
                  mem_sim->page_size);
        }

        victim_pd->V = 0;                               // Mark victim page as not loaded

        // Remove victim page from LRU queue
        for (int i = 1; i < mem_sim->queue_size; i++) {
            mem_sim->lru_queue[i - 1] =mem_sim->lru_queue[i];
        }
        mem_sim->queue_size--;

        frame = victim_frame;                          // Use freed frame
    }

    // Load the page content to the chosen frame
    if (pd->P == 1) {
        printf("Page fault: Loading page %d from program file\n", page);
        lseek(mem_sim->program_fd, page * mem_sim->page_size, SEEK_SET);
        read(mem_sim->program_fd,
             mem_sim->main_memory + frame * mem_sim->page_size,
             mem_sim->page_size);
    }
    else if (pd->D == 1) {
        printf("Page fault: Loading page %d from swap\n", page);
        lseek(mem_sim->swapfile_fd, page * mem_sim->page_size, SEEK_SET);
        read(mem_sim->swapfile_fd,
             mem_sim->main_memory + frame * mem_sim->page_size,
             mem_sim->page_size);
    }
    else if (page >= (mem_sim->text_size / mem_sim->page_size)
             && page < ((mem_sim->text_size + mem_sim->data_size) / mem_sim->page_size)) {
        printf("Page fault: Loading page %d from program file\n", page);
        lseek(mem_sim->program_fd,
              mem_sim->text_size + (page - (mem_sim->text_size / mem_sim->page_size)) * mem_sim->page_size,
              SEEK_SET);
        read(mem_sim->program_fd,
             mem_sim->main_memory + frame * mem_sim->page_size,
             mem_sim->page_size);
    }
    else {
        printf("Page fault: Loading page %d with zeros\n", page);
        memset(mem_sim->main_memory + frame * mem_sim->page_size, 0, mem_sim->page_size);
    }

    // Update the page descriptor after loading
    pd->V = 1;                                        // Mark as loaded
    pd->frame_swap = frame;                           // Store frame number
    pd->D = 0;                                        // Clean after load

    // Add the page to the LRU queue
    mem_sim->lru_queue[mem_sim->queue_size++] = page;

    // Read the requested byte from the newly loaded page
    int physical_address = frame * mem_sim->page_size + offset;
    char value = mem_sim->main_memory[physical_address];
    printf("Value at address %d = %c\n", address, value);
}

// This function  writing a value to a virtual address
void store_virtual(sim_database *mem_sim, int address, char value) {

    // Check if the address is within valid range
    if (address < 0 || address >= mem_sim->num_pages * mem_sim->page_size) {
        fprintf(stderr, "Error: Invalid address %d (out of range)\n", address);
        return;
    }

    // Calculate page number and offset within the page
    int page = address / mem_sim->page_size;           // Compute which page
    int offset = address % mem_sim->page_size;         // Compute offset inside page

    page_descriptor *pd = &mem_sim->page_table[page];  // Get pointer to page descriptor

    // If page is not loaded in memory, load it first
    if (pd->V == 0) {
        load_virtual(mem_sim, address);                // Call load_virtual() to load the page
        // After load_virtual(), pd->V must be 1
    }

    // Check write permissions
    if (pd->P == 1) {
        fprintf(stderr, "Error: Write permission denied at address %d\n", address);
        return;
    }

    // Compute the physical address
    int frame = pd->frame_swap;                        // Frame where page is loaded
    int physical_address = frame * mem_sim->page_size + offset; // Physical memory location

    // Write the value into main memory
    mem_sim->main_memory[physical_address] = value;

    // Mark the page as dirty
    pd->D = 1;

    // Print confirmation
    printf("Stored value '%c' at address %d\n", value, address);
}

void print_page_table(sim_database *mem_sim) {
    // Print table header
    printf("Page Table:\n");
    printf("Page  V D P Frame/Swap\n");

    // Loop over all pages
    for (int i = 0; i < mem_sim->num_pages; i++) {
        page_descriptor *pd = &mem_sim->page_table[i];  // Get pointer to page descriptor

        // Print details of this page
        printf("%-5d %d %d %d %d\n",
               i,             // Page number
               pd->V,         // Valid bit
               pd->D,         // Dirty bit
               pd->P,         // Permission
               pd->frame_swap // Frame number or swap block
        );
    }
}
void print_memory(sim_database *mem_sim) {
    // Print memory header
    printf("Main Memory:\n");

    // Loop over all bytes of main memory
    for (int i = 0; i < mem_sim->memory_size; i++) {
        printf("%c", mem_sim->main_memory[i]);  // Print each character

        // Print a newline every page_size bytes
        if ((i + 1) % mem_sim->page_size == 0) {
            printf("\n");
        }
    }
}
void print_swap(sim_database *mem_sim) {
    // Print swap header
    printf("Swap File Content:\n");

    // Allocate a buffer to read one page at a time
    char *buffer = (char *)malloc(mem_sim->page_size);

    if (!buffer) {
        perror("Error allocating buffer for swap print");
        return;
    }

    // Loop over swap size by pages
    for (int i = 0; i < mem_sim->swap_size / mem_sim->page_size; i++) {
        // Seek to the start of the current page
        lseek(mem_sim->swapfile_fd, i * mem_sim->page_size, SEEK_SET);

        // Read one page into the buffer
        ssize_t read_bytes = read(mem_sim->swapfile_fd, buffer, mem_sim->page_size);

        // Check for read errors
        if (read_bytes != mem_sim->page_size) {
            perror("Error reading swap file");
            free(buffer);
            return;
        }

        // Print the page contents
        for (int j = 0; j < mem_sim->page_size; j++) {
            printf("%c", buffer[j]);
        }
        printf("\n");
    }

    // Free the buffer
    free(buffer);
}



