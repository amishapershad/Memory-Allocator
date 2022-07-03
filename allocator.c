#define _GNU_SOURCE

#include <assert.h>
#include <malloc.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <unistd.h>

// The minimum size returned by malloc
#define MIN_MALLOC_SIZE 16

// Round a value x up to the next multiple of y
#define ROUND_UP(x, y) ((x) % (y) == 0 ? (x) : (x) + ((y) - (x) % (y)))

// The size of a single page of memory, in bytes
#define PAGE_SIZE 0x1000

// Magic number
#define MAGIC 12073110

// Linked list node structure
typedef struct node {
  struct node* next;
} node_t;

// Header structure
typedef struct header {
  int magic;
  size_t size;
} header_t;

// global variable thats holds the free lists
void* lists[8] = {NULL};

// Rounds the given number up to the closest multiple
// of 2.
size_t round_power_2(size_t x) {
  size_t accum = 16;
  size_t i = 0;
  while (accum < x) {
    accum *= 2;
    i++;
  }

  return i + 4;
}

// A utility logging function that definitely does not call malloc or free
void log_message(char* message);

// Throw this here so we can use it
size_t xxmalloc_usable_size(void* ptr);

/**
 * Allocate space on the heap.
 * \param size  The minimium number of bytes that must be allocated
 * \returns     A pointer to the beginning of the allocated space.
 *              This function may return NULL when an error occurs.
 */
void* xxmalloc(size_t size) {
  // If memory is big
  if (size > 2048) {
    // Round the size up to the next multiple of the page size
    size = ROUND_UP(size, PAGE_SIZE);

    // Request memory from the operating system in page-sized chunks
    void* p = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);

    // Check for errors
    if (p == MAP_FAILED) {
      log_message("mmap failed! Giving up.\n");
      exit(2);
    }

    return p;
  }

  // Calculate the power of 2 the size falls under
  size_t list_index = round_power_2(size);  // size of the appropriate free list

  // Get the appropriate list
  node_t* list = lists[list_index - 4];

  // Calculate block size
  size_t block_size = 1 << list_index;

  // If no memory remains on the given free list
  if (list == NULL) {
    // Call mmap to get new page
    void* p = mmap(NULL, PAGE_SIZE, PROT_READ | PROT_WRITE, MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);

    // If mmap fails:
    if (p == MAP_FAILED) {
      log_message("mmap failed\n");
      exit(2);
    }

    // Divide page up into header + n *size* chunks
    int n = PAGE_SIZE / block_size;

    // For each block except the first store the next node
    for (int i = 1; i < n; i++) {
      // beginning of each block of memory
      void* p2 = i * block_size + p;

      // Create a new node in memory
      node_t* node = p2;

      // Point the node to the next block
      node->next = p2 + block_size;

      // If we're in the last block point to NULL
      if (i == n - 1) {
        node->next = NULL;
      }
    }
    // Set the header
    header_t* head = p;
    head->magic = MAGIC;
    head->size = block_size;

    lists[list_index - 4] = p + block_size;
  }

  // Get list again in case of updates
  list = lists[list_index - 4];

  // Take the first element off the list
  node_t* head = list;

  // Get first block
  void* block = head;

  // Update the list
  lists[list_index - 4] = ((node_t*)block)->next;

  return block;
}

/**
 * Free space occupied by a heap object.
 * \param ptr   A pointer somewhere inside the object that is being freed
 */
void xxfree(void* ptr) {
  // Don't free NULL!
  if (ptr == NULL) return;

  // Cast ptr to int
  intptr_t cast_ptr = (intptr_t)ptr;

  // Find the page start as described in lab documents
  header_t* page = (header_t*)(cast_ptr - (cast_ptr % PAGE_SIZE));

  // If not from this allocator do nothing
  if (page->magic != MAGIC) {
    return;
  }

  // Get size of blocks
  size_t block_size = xxmalloc_usable_size(ptr);

  // If getting the size failed
  if (block_size == 0) return;

  // Get correct list by finding power of two:
  int accum = 0;
  int start = block_size;
  while (start != 16) {
    start = start >> 1;
    accum += 1;
  }

  // Get beginning of the block
  node_t* p = (node_t*)((intptr_t)ptr - ((intptr_t)ptr % block_size));

  // Stick block onto list
  node_t* list = lists[accum];

  // Save the current first node
  node_t* cur = list;

  // Make first node be freed node
  lists[accum] = p;

  // Point to old first node
  p->next = cur;
}

/**
 * Get the available size of an allocated object. This function should return the amount of space
 * that was actually allocated by malloc, not the amount that was requested.
 * \param ptr   A pointer somewhere inside the allocated object
 * \returns     The number of bytes available for use in this object
 */
size_t xxmalloc_usable_size(void* ptr) {
  // If ptr is NULL always return zero
  if (ptr == NULL) {
    return 0;
  }

  // Cast ptr to int
  intptr_t p = (intptr_t)ptr;

  // Round p up to next page size then sub page size to get
  //   where the page starts
  header_t* page = (header_t*)(p - (p % PAGE_SIZE));

  // If not from the allocator, fail
  if (page->magic != MAGIC) {
    return 0;
  }

  return page->size;
}

/**
 * Print a message directly to standard error without invoking malloc or free.
 * \param message   A null-terminated string that contains the message to be printed
 */
void log_message(char* message) {
  // Get the message length
  size_t len = 0;
  while (message[len] != '\0') {
    len++;
  }

  // Write the message
  if (write(STDERR_FILENO, message, len) != len) {
    // Write failed. Try to write an error message, then exit
    char fail_msg[] = "logging failed\n";
    write(STDERR_FILENO, fail_msg, sizeof(fail_msg));
    exit(2);
  }
}
