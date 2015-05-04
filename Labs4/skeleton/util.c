#include <sys/types.h>
#include <unistd.h>
#include <stdint.h>
#include <assert.h>
#include <sys/mman.h>
#include <err.h>

#include "util.h"


uintptr_t page_floor(uintptr_t offset) {
    size_t pagesize = sysconf(_SC_PAGESIZE);
    assert(offset>=0);
    return (offset / pagesize) * pagesize;
}

uintptr_t page_ceil(uintptr_t offset) {
    size_t pagesize = sysconf(_SC_PAGESIZE);
    assert(offset>=0);
    return ((offset + pagesize - 1) / pagesize) * pagesize;
}

// mmap file content at given offset
// use unmap to release the mapping
void* mmap_file(int fd, off_t offset, size_t size)
{
    off_t offset_end = offset + size;
    assert(offset >= 0);
    assert(offset_end >= offset); // No overflow
    
    uintptr_t end = page_ceil(offset_end);
    uintptr_t start = page_floor(offset);

    uintptr_t len = end - start;
    void* buf = mmap(NULL, len, PROT_READ, MAP_SHARED, fd, start);
    
    if (buf == MAP_FAILED)
        err(1, "mmap failed");

    return ((void *)((uintptr_t)buf + (offset - start)));
}

// buf: buffer returned by mmap_file()
// size: same size as supplied to the mmap_file()
void unmap(void* buf, size_t size)
{
  assert(buf);
  uintptr_t end = page_ceil((uintptr_t)buf + size);
  uintptr_t start = page_floor((uintptr_t)buf);
  assert(end > (uintptr_t)buf);
  size_t len = end - start;
  if (munmap((void*)start, len) < 0)
      err(1, "munmap failed");
}

