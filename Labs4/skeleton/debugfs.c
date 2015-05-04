#include <string.h>
#include <stdio.h>
#include <assert.h>

#include "vfat.h"
#include "debugfs.h"

#define DEBUGFS_MAX_FILE_LEN 1024

#define NEXT_CLUSTER_PATH "/next_cluster"

#define CONSUME_PREFIX(str, prefix) (strncmp(str, prefix, strlen(prefix)) == 0 ? str += strlen(prefix) ,1: 0)

int debugfs_fuse_read(const char *path, char *buf, size_t size, off_t offs,
                      struct fuse_file_info *fi)
{
    char tmpbuf[DEBUGFS_MAX_FILE_LEN];
    char* eof = tmpbuf;
    if (strcmp(path, "/bytes_per_sector")==0) {
        eof += sprintf(eof, "%d", (int) vfat_info.bytes_per_sector);
    } else if (strcmp(path, "/sectors_per_cluster")==0) {
        eof += sprintf(eof, "%d", (int) vfat_info.sectors_per_cluster);
    } else if (strcmp(path, "/reserved_sectors")==0) {
        eof += sprintf(eof, "%d", (int) vfat_info.reserved_sectors);
    } else if (strcmp(path, "/fat_begin_offset")==0) {
        eof += sprintf(eof, "%d", (int) vfat_info.fat_begin_offset);
    } else if (strcmp(path, "/fat_num_entries")==0) {
        eof += sprintf(eof, "%d", (int) vfat_info.fat_entries);
    } else if (CONSUME_PREFIX(path, NEXT_CLUSTER_PATH "/")) {
      unsigned int i;
      if (sscanf(path, "%u", &i) == 1) {
        eof += sprintf(eof, "%u", vfat_next_cluster(i));
      } else {
        eof += sprintf(eof, "ERROR: Could not parse integer from %s", path);
      }
    } else {
      eof += sprintf(eof, "Invalid .debugfs request: path '%s'", path);
    }
    assert(eof >= tmpbuf);
    int len = (eof - tmpbuf) - offs;
    if (len < 0) return 0;
    
    assert(len < 1024);
    if (len > size) {
      len = size;
    }

    memcpy(buf, tmpbuf+offs, len);
    return len;
}

int debugfs_fuse_readdir(
      const char *path, void *callback_data,
      fuse_fill_dir_t callback, off_t unused_offs, struct fuse_file_info *unused_fi)
{
    if (strcmp(path, "") != 0) return 0;
    char* listed_files[] = {
        "bytes_per_sector",
        "sectors_per_cluster",
        "reserved_sectors",
        "fat_begin_offset",
        "fat_num_entries",
        "next_cluster", // directory
        NULL,
    };
    char** name_ptr = listed_files;
    while (*name_ptr) {
        callback(callback_data, *name_ptr, NULL, 0);
        name_ptr++;
    }
    return 0;
}



int debugfs_fuse_getattr(const char *path, struct stat *st) {
    st->st_dev = 0; // Ignored by FUSE
    st->st_ino = 42; // We have magical inodes here ;-)
    st->st_nlink = 1;
    st->st_uid = vfat_info.mount_uid;
    st->st_gid = vfat_info.mount_gid;
    st->st_rdev = 0;
    st->st_size = 5000; // Hey, we lie, but who cares? We anyway report EOF when reading.
    st->st_blksize = 0; // Ignored by FUSE
    st->st_blocks = 1;
    st->st_mode = S_IRWXU | S_IRWXG | S_IRWXO;
    if (strcmp(path, "") == 0
        || strcmp(path, NEXT_CLUSTER_PATH) == 0) {
        st->st_mode |= S_IFDIR; // Directory
    } else {
        st->st_mode |= S_IFREG; // File
    }
    return 0; // You can stat anything, viva silent errors ;-)
}
