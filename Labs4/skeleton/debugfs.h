#ifndef H_TEST_HELPERS
#define H_TEST_HELPERS

#include <fuse.h>

int debugfs_fuse_read(const char *path, char *buf, size_t size, off_t offs,
                      struct fuse_file_info *fi);

int debugfs_fuse_readdir(
      const char *path, void *callback_data,
      fuse_fill_dir_t callback, off_t unused_offs, struct fuse_file_info *unused_fi);

int debugfs_fuse_getattr(const char *path, struct stat *st);

#endif
