#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/types.h>
#include <string.h>
#include <stdio.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdlib.h>

#define TLB_SIZE 16
#define PAGES 64
#define PAGE_MASK 63

#define PAGE_SIZE 256
#define OFFSET_BITS 8
#define OFFSET_MASK 255

#define MEMORY_SIZE PAGES * PAGE_SIZE

// Max number of characters per line of input file to read.
#define BUFFER_SIZE 10

int FIFO = 0;
int LRU = 1;

typedef struct Queue {
    struct Node *first;
    struct Node *last;
    int maxSize;
    int currentSize;
} Queue;

typedef struct Node {
    struct Node *next;
    struct Node *prev;
    int physicalPage;
    int logicalPage;
} Node;

void enqueue(Queue *queue, Node *node, int mode) {
    int alreadyInQueue = 0;

    Node *currentNode = queue->first;

    while (currentNode) {
        if (currentNode == node) {
            if (mode == FIFO) {
                return;
            }

            alreadyInQueue = 1;
            break;
        }

        currentNode = currentNode->next;
    }

    if (alreadyInQueue) {
        if (queue->last == node) {
            printf("Node %d is already the last element of queue\n", node->physicalPage);
            return;
        } else {

            if (node->prev) {
                if (node->next) {
                    node->next->prev = node->prev;
                }
            } else {
                queue->first = node->next;
                if (node->next) {
                    node->next->prev = NULL;
                }
            }

            if (node->next) {
                if (node->prev) {
                    node->prev->next = node->next;
                }
            } else {
                if (node->prev)
                    node->prev->next = NULL;
            }

            queue->last->next = node;
            node->prev = queue->last;
            queue->last = node;
            node->next = NULL;

            printf("Node %d is already in the queue, placed to the end\n", node->physicalPage);
        }

    } else {

        if (queue->currentSize == 0) {
            queue->first = node;
            queue->last = node;
        } else {
            queue->last->next = node;
            node->prev = queue->last;
            queue->last = node;
        }
        queue->currentSize++;
    }
}

void dequeue(Queue *queue) {
    if (queue->currentSize == 0) {
        printf("Attempt to remove from empty queue fix this.\n");
        return;
    } else if (queue->currentSize == 1) {
        printf("Dequeueing %d\n", queue->first->physicalPage);
        queue->first = NULL;
        queue->last = NULL;
        queue->currentSize = 0;
        return;
    }

    printf("Dequeueing %d\n", queue->first->physicalPage);
    queue->first->next->prev = NULL;
    queue->first = queue->first->next;
    queue->currentSize--;
}

int getPhysicalPageByLogicalPage(Queue *queue, int logicalPage) {
    Node *currentNode = queue->first;

    while (currentNode) {
        if (currentNode->logicalPage == logicalPage) {
            return currentNode->physicalPage;
        }

        currentNode = currentNode->next;
    }

    return -1;
}

struct tlbentry {
    unsigned char logical;
    unsigned char physical;
};

// TLB is kept track of as a circular array, with the oldest element being overwritten once the TLB is full.
struct tlbentry tlb[TLB_SIZE];
// number of inserts into TLB that have been completed. Use as tlbindex % TLB_SIZE for the index of the next TLB line to use.
int tlbindex = 0;

// pagetable[logical_page] is the physical page number for logical page. Value is -1 if that logical page isn't yet in the table.

signed char main_memory[MEMORY_SIZE];

// Pointer to memory mapped backing file
signed char *backing;

int max(int a, int b) {
    if (a > b)
        return a;
    return b;
}

/* Returns the physical address from TLB or -1 if not present. */
int search_tlb(unsigned char logical_page) {
    int i;
    for (i = max((tlbindex - TLB_SIZE), 0); i < tlbindex; i++) {
        struct tlbentry *entry = &tlb[i % TLB_SIZE];

        if (entry->logical == logical_page) {
            return entry->physical;
        }
    }

    return -1;
}

/* Adds the specified mapping to the TLB, replacing the oldest mapping (FIFO replacement). */
void add_to_tlb(unsigned char logical, unsigned char physical) {
    struct tlbentry *entry = &tlb[tlbindex % TLB_SIZE];
    tlbindex++;
    entry->logical = logical;
    entry->physical = physical;
}

int main(int argc, const char *argv[]) {
    Queue *myQueue = (Queue *) malloc(sizeof(Queue));
    myQueue->maxSize = 64;

    if (argc != 5) {
        fprintf(stderr, "Usage ./virtmem backingstore input -p mode(0/1)\n");
        exit(1);
    }

    int mode;

    if (strcmp(argv[3], "-p") == 0) {
        char *modeStr = (char *) argv[4];
        if (strcmp(modeStr, "0") == 0) {
            mode = FIFO;
        } else if (strcmp(modeStr, "1") == 0) {
            mode = LRU;
        } else {
            fprintf(stderr, "Usage ./virtmem backingstore input -p mode(0/1)\n");
            exit(1);
        }
    } else {
        fprintf(stderr, "Usage ./virtmem backingstore input -p mode(0/1)\n");
        exit(1);
    }

    printf("Mode : %d\n", mode);

    const char *backing_filename = argv[1];
    int backing_fd = open(backing_filename, O_RDONLY);
    backing = mmap(0, MEMORY_SIZE, PROT_READ, MAP_PRIVATE, backing_fd, 0);

    const char *input_filename = argv[2];
    FILE *input_fp = fopen(input_filename, "r");

    // Fill page table entries with -1 for initially empty table.

    // Character buffer for reading lines of input file.
    char buffer[BUFFER_SIZE];

    // Data we need to keep track of to compute stats at end.
    int total_addresses = 0;
    int tlb_hits = 0;
    int page_faults = 0;

    // Number of the next unallocated physical page in main memory
    int free_page = 0;

    while (fgets(buffer, BUFFER_SIZE, input_fp) != NULL) {
        total_addresses++;
        int logical_address = atoi(buffer);
        int offset = logical_address & OFFSET_MASK;
        int logical_page = (logical_address >> OFFSET_BITS) & PAGE_MASK;

        int physical_page = search_tlb(logical_page);

        // TLB hit
        if (physical_page != -1) {
            tlb_hits++;
            // TLB miss
        } else {

            // Page fault
            physical_page = getPhysicalPageByLogicalPage(myQueue, logical_page);

            if (physical_page == -1) {
                page_faults++;

                printf("MISSSSSSSSSSSSSSSSSSSSSSSSSS!\n");
                Node *node = (Node *) malloc(sizeof(Node));

                if (myQueue->currentSize == myQueue->maxSize) {
                    printf("AHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHH\n\n\n\n\n\n");
                    printf("Replacing old physical page %d, virtual page %d with new physical page %d, virtual page %d\n",
                           myQueue->first->physicalPage, myQueue->first->logicalPage,
                           myQueue->first->physicalPage, logical_page);
                    physical_page = myQueue->first->physicalPage;
                    dequeue(myQueue);
                    node->physicalPage = physical_page;
                    node->logicalPage = logical_page;
                } else if (myQueue->currentSize < myQueue->maxSize) {
                    printf("free_page=%d\n", free_page);
                    physical_page = free_page;
                    free_page++;
                    node->physicalPage = physical_page;
                    node->logicalPage = logical_page;
                } else {
                    printf("Something went horribly wrong!\n");
                }

                enqueue(myQueue, node, mode);



                // Copy page from backing file into physical memory
                memcpy(main_memory + physical_page * PAGE_SIZE, backing + logical_page * PAGE_SIZE, PAGE_SIZE);

            }
            add_to_tlb(logical_page, physical_page);
        }

        int physical_address = (physical_page << OFFSET_BITS) | offset;
        signed char value = main_memory[physical_page * PAGE_SIZE + offset];

        printf("Virtual address: %d Physical address: %d Physical Page: %d, Value: %d\n", logical_address,
               physical_address, physical_page, value);
    }

    printf("Number of Translated Addresses = %d\n", total_addresses);
    printf("Page Faults = %d\n", page_faults);
    printf("Page Fault Rate = %.3f\n", page_faults / (1. * total_addresses));
    printf("TLB Hits = %d\n", tlb_hits);
    printf("TLB Hit Rate = %.3f\n", tlb_hits / (1. * total_addresses));

    return 0;

}
