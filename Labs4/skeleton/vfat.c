// vim: noet:ts=4:sts=4:sw=4:et
#define FUSE_USE_VERSION 26
#define _GNU_SOURCE

#include <assert.h>
#include <endian.h>
#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <fuse.h>
#include <iconv.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>
#include <time.h>

#include "vfat.h"
#include "util.h"
#include "debugfs.h"

#define DEBUG_PRINT(...) printf(__VA_ARGS)

iconv_t iconv_utf16;
char* DEBUGFS_PATH = "/.debug";

uint32_t FirstSectorofCluster(uint32_t N)
{
    return ((N - 2) * vfat_info.sectors_per_cluster) + vfat_info.spec_FirstDataSector;
}

uint8_t* ClusterMapped(uint32_t N)
{
    return (uint8_t*)mmap_file(vfat_info.fd, FirstSectorofCluster(N)*vfat_info.bytes_per_sector, vfat_info.cluster_size);
}

void ClusterUnmap(uint8_t* cluster)
{
    unmap((void*)cluster, vfat_info.cluster_size);
}

time_t BuildTime(uint16_t inputDate, uint16_t inputTime, uint8_t inputTenth)
{
    // Build temporary time structure
    struct tm preciseTime;
    preciseTime.tm_year = (inputDate >> 9) + 80;
    preciseTime.tm_mon = ((inputDate >> 5) & 0x000F) - 1;
    preciseTime.tm_mday = inputDate & 0x001F;
    preciseTime.tm_hour = inputTime >> 11;
    preciseTime.tm_min = (inputTime >> 5) & 0x003F;
    preciseTime.tm_sec = (inputTime & 0x001F) * 2 + (inputTenth / 10);

    // Build final time structure
    time_t result = mktime(&preciseTime);

    return result;
}

void print_boot_sector(struct fat_boot_header s)
{
    printf("General FAT fields :\n");
    printf("   BS_jmpBoot : %X%X%X\n", s.jmp_boot[0], s.jmp_boot[1], s.jmp_boot[2]);
    printf("   BS_OEMName : %c%c%c%c%c%c%c%c\n", s.oemname[0], s.oemname[1], s.oemname[2], s.oemname[3], s.oemname[4], s.oemname[5], s.oemname[6], s.oemname[7]);
    printf("   BPB_BytesPerSec : %d\n", s.bytes_per_sector);
    printf("   BPB_SecPerClus : %d\n", s.sectors_per_cluster);
    printf("   BPB_RsvdSecCnt : %d\n", s.reserved_sectors);
    printf("   BPB_NumFATs : %d\n", s.fat_count);
    printf("   BPB_RootEntCnt : %d\n", s.root_max_entries);
    printf("   BPB_TotSec16 : %d\n", s.total_sectors_small);
    printf("   BPB_Media : %X\n", s.media_info);
    printf("   BPB_FATSz16 : %d\n", s.sectors_per_fat_small);
    printf("   BPB_SecPerTrk : %d\n", s.sectors_per_track);
    printf("   BPB_NumHeads : %d\n", s.head_count);
    printf("   BPB_HiddSec : %d\n", s.fs_offset);
    printf("   BPB_TotSec32 : %d\n", s.total_sectors);

    printf("FAT32 specific fields :\n");
    printf("   BPB_FATSz32 : %d\n", s.sectors_per_fat);
    printf("   BPB_ExtFlags : %X\n", s.fat_flags);
    printf("   BPB_FSVer : %X\n", s.version);
    printf("   BPB_RootClus : %d\n", s.root_cluster);
    printf("   BPB_FSInfo : %d\n", s.fsinfo_sector);
    printf("   BPB_BkBootSec : %d\n", s.backup_sector);
    printf("   BPB_Reserved : %X%X%X%X%X%X%X%X%X%X%X%X\n", s.reserved2[0], s.reserved2[1], s.reserved2[2], s.reserved2[3], s.reserved2[4], s.reserved2[5], s.reserved2[6], s.reserved2[7], s.reserved2[8], s.reserved2[9], s.reserved2[10], s.reserved2[11]);
    printf("   BS_DrvNum : %d\n", s.drive_number);
    printf("   BS_Reserved1 : %X\n", s.reserved3);
    printf("   BS_BootSig : %X\n", s.ext_sig);
    printf("   BS_VolID : %X\n", s.serial);
    printf("   BS_VolLab : %c%c%c%c%c%c%c%c%c%c%c\n", s.label[0], s.label[1], s.label[2], s.label[3], s.label[4], s.label[5], s.label[6], s.label[7], s.label[8], s.label[9], s.label[10]);
    printf("   BS_FilSysType : %c%c%c%c%c%c%c%c\n", s.fat_name[0], s.fat_name[1], s.fat_name[2], s.fat_name[3], s.fat_name[4], s.fat_name[5], s.fat_name[6], s.fat_name[7]);

    printf("Signature record (bytes 510 and 511) :\n");
    uint8_t* b510 = (uint8_t*)&(s.signature);
    uint8_t* b511 = b510 + 1;
    printf("   [510] %X\n", *b510);
    printf("   [511] %X\n", *b511);
}

void check_is_fat32(struct fat_boot_header s, struct vfat_data i)
{
    // Check BPB_BytesPerSec
    if (s.bytes_per_sector != 512 && s.bytes_per_sector != 1024 && s.bytes_per_sector != 2048 && s.bytes_per_sector != 4096)
    {
        err(1, "BPB_BytesPerSec = %d, must be 512, 1024, 2048 or 4096", s.bytes_per_sector);
    }

    // Check BPB_SecPerClus
    if (s.sectors_per_cluster != 1 && s.sectors_per_cluster != 2 && s.sectors_per_cluster != 4 && s.sectors_per_cluster != 8 && s.sectors_per_cluster != 16 && s.sectors_per_cluster != 32 && s.sectors_per_cluster != 64 && s.sectors_per_cluster != 128)
    {
        err(1, "BPB_SecPerClus = %d, must be 1,2,4,8,16,32,64 or 128", s.sectors_per_cluster);
    }

    // Check BPB_NumFATs
    if (s.fat_count != 2)
    {
        err(1, "BPB_NumFATs = %d, must be 2", s.fat_count);
    }

    // Check BPB_RootEntCnt
    if (s.root_max_entries != 0)
    {
        err(1, "BPB_RootEntCnt = %d, must be 0", s.root_max_entries);
    }

    // Check BPB_TotSec16
    if (s.total_sectors_small != 0)
    {
        err(1, "BPB_TotSec16 = %d, must be 0", s.total_sectors_small);
    }
    
    // Check BPB_FatSz16
    if (s.sectors_per_fat_small != 0)
    {
        err(1, "BPB_FATSz16 = %d, must be 0", s.sectors_per_fat_small);
    }

    // Check signature
    uint8_t* b510 = (uint8_t*)&(s.signature);
    uint8_t* b511 = b510 + 1;
    if (*b510 != 0x55 || *b511 != 0xAA)
    {
        err(1, "signature = %X%X, must be 55AA", *b510, *b511);
    }

    // Check count of clusters
    if (i.spec_CountofClusters < 65525)
    {
        err(1, "CountofClusters = %d, must be >= 65525", i.spec_CountofClusters);
    }
}

static void
vfat_init(const char *dev)
{
    struct fat_boot_header s;

    iconv_utf16 = iconv_open("utf-8", "utf-16"); // from utf-16 to utf-8
    // These are useful so that we can setup correct permissions in the mounted directories
    vfat_info.mount_uid = getuid();
    vfat_info.mount_gid = getgid();

    // Use mount time as mtime and ctime for the filesystem root entry (e.g. "/")
    vfat_info.mount_time = time(NULL);

    vfat_info.fd = open(dev, O_RDONLY);
    if (vfat_info.fd < 0)
        err(1, "open(%s)", dev);
    if (pread(vfat_info.fd, &s, sizeof(s), 0) != sizeof(s))
        err(1, "read super block");

    // Print infos about fat_boot_header s
    //print_boot_sector(s);

    // Compute specification values
    vfat_info.spec_RootDirSectors = 0;
    vfat_info.spec_FirstDataSector = s.reserved_sectors + (s.fat_count * s.sectors_per_fat) + vfat_info.spec_RootDirSectors;
    vfat_info.spec_DataSec = s.total_sectors - vfat_info.spec_FirstDataSector;
    vfat_info.spec_CountofClusters = vfat_info.spec_DataSec / s.sectors_per_cluster;

    // Check volume is FAT32
    check_is_fat32(s, vfat_info);

    // Populate other vfat_info fields
    vfat_info.sectors_per_fat = s.sectors_per_fat;

    // Populate .debug
    vfat_info.bytes_per_sector = s.bytes_per_sector;
    vfat_info.sectors_per_cluster = s.sectors_per_cluster;
    vfat_info.reserved_sectors = s.reserved_sectors;
    vfat_info.fat_begin_offset = vfat_info.reserved_sectors * vfat_info.bytes_per_sector;
    vfat_info.fat_entries = vfat_info.sectors_per_fat * vfat_info.bytes_per_sector / 4;

    // Populate other vfat_info fields
    vfat_info.cluster_size = vfat_info.bytes_per_sector * vfat_info.sectors_per_cluster;
    vfat_info.fat_size = vfat_info.fat_entries * 4;
    vfat_info.cluster_begin_offset = s.root_cluster;
    vfat_info.direntry_per_cluster = vfat_info.cluster_size / 32;

    // Load FAT table from disk
    vfat_info.fat = (uint32_t*)mmap_file(vfat_info.fd, vfat_info.fat_begin_offset, vfat_info.fat_size);

    // Set root inode infos
    vfat_info.root_inode.st_ino = le32toh(s.root_cluster);
    vfat_info.root_inode.st_mode = 0555 | S_IFDIR;
    vfat_info.root_inode.st_nlink = 1;
    vfat_info.root_inode.st_uid = vfat_info.mount_uid;
    vfat_info.root_inode.st_gid = vfat_info.mount_gid;
    vfat_info.root_inode.st_size = 0;
    vfat_info.root_inode.st_atime = vfat_info.root_inode.st_mtime = vfat_info.root_inode.st_ctime = vfat_info.mount_time;
}

// Gives the number of next cluster, corresponding to input cluster number c
int vfat_next_cluster(uint32_t c)
{
    return vfat_info.fat[c];
}

int vfat_readdir(uint32_t first_cluster, fuse_fill_dir_t callback, void *callbackdata)
{
    // We can reuse same stat entry over and over again
    struct stat st;
    memset(&st, 0, sizeof(st));
    st.st_uid = vfat_info.mount_uid;
    st.st_gid = vfat_info.mount_gid;
    st.st_nlink = 1;

    // Buffer for storing long names
    size_t longNameSize = 0;
    unsigned char* longName = NULL;
    unsigned char* longNamePart = (unsigned char*)calloc(13, sizeof(unsigned char));

    // Buffer for storing short names
    unsigned char* shortName = (unsigned char*)calloc(13, sizeof(unsigned char));
    int j;

    // Cluster variable
    uint32_t clusterId = (first_cluster & 0x0FFFFFFF);

    // Loop on clusters
    while ((clusterId > 0x00000001) && (clusterId < 0x0FFFFFF0))
    {
        // Load cluster data
        uint8_t* cluster = ClusterMapped(clusterId);

        // Interpret cluster as fat32_direntry and fat32_direntry_long
        struct fat32_direntry* direntries = (struct fat32_direntry*)cluster;
        struct fat32_direntry_long* direntrieslong = (struct fat32_direntry_long*)cluster;

        // Marker of deletion
        int isDeleted = 0;

        // Go through direntries of the cluster
        size_t i;
        for (i=0; i<vfat_info.direntry_per_cluster; i++)
        {
            // If directory entry is empty
            if (direntries[i].name[0] == 0xE5)
            {
                continue;
            }

            // If directory entry is empty and no other is full
            if (direntries[i].name[0] == 0x00)
            {
                break;
            }

            // If directory entry is long name
            if ((direntries[i].attr & 0x0F) == 0x0F)
            {
                // Read long name parts
                longNamePart[0] = direntrieslong[i].name1[0];
                longNamePart[1] = direntrieslong[i].name1[1];
                longNamePart[2] = direntrieslong[i].name1[2];
                longNamePart[3] = direntrieslong[i].name1[3];
                longNamePart[4] = direntrieslong[i].name1[4];
                longNamePart[5] = direntrieslong[i].name2[0];
                longNamePart[6] = direntrieslong[i].name2[1];
                longNamePart[7] = direntrieslong[i].name2[2];
                longNamePart[8] = direntrieslong[i].name2[3];
                longNamePart[9] = direntrieslong[i].name2[4];
                longNamePart[10] = direntrieslong[i].name2[5];
                longNamePart[11] = direntrieslong[i].name3[0];
                longNamePart[12] = direntrieslong[i].name3[1];

                // Search for \0
                size_t zeropos = 0;
                for (zeropos = 0; zeropos < 13; zeropos++)
                {
                    if (longNamePart[zeropos] == '\0')
                    {
                        break;
                    }
                }
                ssize_t currpos = 0;

                // Extend long name storage
                longNameSize += zeropos;
                longName = realloc(longName, longNameSize * sizeof(unsigned char));

                // Shift all previous content to right
                for (currpos = longNameSize - zeropos - 1; (currpos >= 0) && (longNameSize - zeropos > 0); currpos--)
                {
                    longName[currpos + zeropos] = longName[currpos];
                }

                // Copy all chars before \0
                for (currpos = 0; currpos < zeropos; currpos++)
                {
                    longName[currpos] = longNamePart[currpos];
                }
            }

            // If directory entry is VOLUME_ID directory entry
            else if ((direntries[i].attr & 0x08) == 0x08)
            {
                // Do nothing
            }

            // If directory entry is standard directory entry
            else
            {
                // Fill in status infos
                st.st_ino = (((uint32_t)(direntries[i].cluster_hi)) << 16) | ((uint32_t)(direntries[i].cluster_lo));
                st.st_mode = 0555;
                if ((direntries[i].attr & 0x10) == 0x10)
                {
                    st.st_mode = st.st_mode | S_IFDIR;
                }
                else
                {
                    st.st_mode = st.st_mode | S_IFREG;
                }
                st.st_size = direntries[i].size;

                // Convert dates
                st.st_atime = BuildTime(direntries[i].atime_date, 0, 0);
                st.st_mtime = BuildTime(direntries[i].mtime_date, direntries[i].mtime_time, 0);
                st.st_ctime = BuildTime(direntries[i].ctime_date, direntries[i].ctime_time, direntries[i].ctime_ms);

                // Define name pointer
                unsigned char* name = NULL;

                // Build short name
                ssize_t sizeCnt = 0;
                while ((sizeCnt < 8) && (direntries[i].name[sizeCnt] != ' '))
                {
                    shortName[sizeCnt] = direntries[i].name[sizeCnt];
                    sizeCnt++;
                }
                if (direntries[i].ext[0] != ' ')
                {
                    shortName[sizeCnt] = '.';
                    sizeCnt++;
                }
                ssize_t sizeName = sizeCnt;
                while (((sizeCnt - sizeName) < 3) && (direntries[i].ext[sizeCnt-sizeName] != ' '))
                {
                    shortName[sizeCnt] = direntries[i].ext[sizeCnt-sizeName];
                    sizeCnt++;
                }
                shortName[sizeCnt] = '\0';

                // Check for deletion in short name
                for (j=0; j<sizeCnt; j++)
                {
                    if (shortName[j] == 0xE5)
                    {
                        isDeleted = 1;
                    }
                }

                // If long name
                if (longName != NULL)
                {
                    longName = realloc(longName, (longNameSize+1) * sizeof(unsigned char));
                    longName[longNameSize] = '\0';
                    for (j=0; j<longNameSize; j++)
                    {
                        if (longName[j] == 0xE5)
                        {
                            isDeleted = 1;
                        }
                    }
                    name = longName;
                }

                // If short name
                else
                {
                    name = shortName;
                }

                // Callback
                if (isDeleted == 0)
                {
                    callback(callbackdata, (const char*)name, &st, 0);
                }
                isDeleted = 0;

                // Free long name
                if (longName != NULL)
                {
                    free(longName);
                    longName = NULL;
                    longNameSize = 0;
                }
            }
        }

        // Unmap cluster
        ClusterUnmap(cluster);

        // Go to next cluster
        clusterId = vfat_next_cluster(clusterId);
    }

    return 0;
}


// Used by vfat_search_entry()
struct vfat_search_data {
    const char*  name;
    int          found;
    struct stat* st;
};


// You can use this in vfat_resolve as a callback function for vfat_readdir
// This way you can get the struct stat of the subdirectory/file.
int vfat_search_entry(void *data, const char *name, const struct stat *st, off_t offs)
{
    struct vfat_search_data *sd = data;

    if (strcmp(sd->name, name) != 0) return 0;

    sd->found = 1;
    *sd->st = *st;

    return 1;
}

/**
 * Fills in stat info for a file/directory given the path
 * @path full path to a file, directories separated by slash
 * @st file stat structure
 * @returns 0 iff operation completed succesfully -errno on error
*/
int vfat_resolve(const char *path, struct stat *st)
{
    // Temporary stat structure to fill in, initialized with root inode
    struct stat myStat;
    myStat = vfat_info.root_inode;

    // Temporary vfat_search_data structure
    struct vfat_search_data searchData;
    searchData.name = NULL;
    searchData.found = 0;
    searchData.st = (struct stat*)malloc(sizeof(struct stat));

    // Tokenize the string
    char* token;
    token = strtok((char*)path, "/");

    // For each token ("folder")
    while (token != NULL)
    {
        // Read the parent dir and search for it
        searchData.name = token;
        vfat_readdir(myStat.st_ino, vfat_search_entry, (void*)(&searchData));

        // If dest found
        if (searchData.found == 1)
        {
            // Copy stat into my stat
            myStat = *(searchData.st);

            // Cancel found
            searchData.found = 0;

            // If it is a file
            if ((myStat.st_mode & S_IFDIR) == 0)
            {
                // Check next token is NULL
                token = strtok(NULL, "/");
                if (token == NULL)
                {
                    break;
                }
                else
                {
                    free(searchData.st);
                    return -ENOTDIR;
                }
            }
        }

        // If dest not found
        else
        {
            return -ENOENT;
        }

        // Next token
        token = strtok(NULL, "/");
    }

    // Put stat in output
    free(searchData.st);
    *st = myStat;
    return 0;
}

// Get file attributes
int vfat_fuse_getattr(const char *path, struct stat *st)
{
    // Virtual debug filesystem
    if (strncmp(path, DEBUGFS_PATH, strlen(DEBUGFS_PATH)) == 0) {
        return debugfs_fuse_getattr(path + strlen(DEBUGFS_PATH), st);
    }
    
    // Real FAT filesystem
    else
    {
        return vfat_resolve(path, st);
    }
}

// Extended attributes useful for debugging
int vfat_fuse_getxattr(const char *path, const char* name, char* buf, size_t size)
{
    struct stat st;
    int ret = vfat_resolve(path, &st);
    if (ret != 0) return ret;
    if (strcmp(name, "debug.cluster") != 0) return -ENODATA;

    if (buf == NULL) {
        ret = snprintf(NULL, 0, "%u", (unsigned int) st.st_ino);
        if (ret < 0) err(1, "WTF?");
        return ret + 1;
    } else {
        ret = snprintf(buf, size, "%u", (unsigned int) st.st_ino);
        if (ret >= size) return -ERANGE;
        return ret;
    }
}

int vfat_fuse_readdir(
        const char *path, void *callback_data,
        fuse_fill_dir_t callback, off_t unused_offs, struct fuse_file_info *unused_fi)
{
    // Virtual debug filesystem
    if (strncmp(path, DEBUGFS_PATH, strlen(DEBUGFS_PATH)) == 0) {
        return debugfs_fuse_readdir(path + strlen(DEBUGFS_PATH), callback_data, callback, unused_offs, unused_fi);
    }

    // Real FAT filesystem
    else
    {
        struct stat dirStat;

        // If path can be resolved to a stat structure
        if (vfat_resolve(path, &dirStat) == 0)
        {
            return vfat_readdir(dirStat.st_ino, callback, callback_data);
        }

        // If path cannot be resolved
        else
        {
            return -errno;
        }
    }
    return 0;
}

int vfat_fuse_read(
        const char *path, char *buf, size_t size, off_t offs,
        struct fuse_file_info *unused)
{
    // Virtual debug filesystem
    if (strncmp(path, DEBUGFS_PATH, strlen(DEBUGFS_PATH)) == 0)
    {
        return debugfs_fuse_read(path + strlen(DEBUGFS_PATH), buf, size, offs, unused);
    }

    // Real FAT filesystem
    else
    {
        struct stat fileStat;

        // If path can be resolved to a stat structure
        if (vfat_resolve(path, &fileStat) == 0)
        {
            // Determine theoretical cluster # in clusters chain
            size_t startClusterNumber = offs / vfat_info.cluster_size;

            // Find cluster number
            uint32_t clusterId = fileStat.st_ino & 0x0FFFFFFF;
            size_t clusterNumber;
            for (clusterNumber = 0; clusterNumber < startClusterNumber; clusterNumber++)
            {
                clusterId = vfat_next_cluster(clusterId) & 0x0FFFFFFF;

                // Reached end cluster
                if ((clusterId <= 0x00000001) || (clusterId >= 0x0FFFFFF0))
                {
                    return 0;
                }
            }

            // Prepare buffer
            char* tmpbuff = (char*)calloc(size, sizeof(char));

            // Compute offset inside cluster
            off_t innerOffset = offs % vfat_info.cluster_size;

            // Read size
            size_t readSize = 0;

            // Loop on clusters
            while ((clusterId > 0x00000001) && (clusterId < 0x0FFFFFF0))
            {
                // Load cluster data
                uint8_t* cluster = ClusterMapped(clusterId);

                // Start reading at innerOffset
                while ((innerOffset < vfat_info.cluster_size) && (readSize < size) && (offs + readSize < fileStat.st_size))
                {
                    tmpbuff[readSize] = (char)(cluster[innerOffset]);
                    readSize++;
                    innerOffset++;
                }

                // Unmap cluster
                ClusterUnmap(cluster);

                // If there is still room for data
                if (readSize < size)
                {
                    innerOffset = 0;
                    clusterId = vfat_next_cluster(clusterId);
                }

                // If buffer is full
                else
                {
                    clusterId = 0x00000000;
                }
            }

            // Copy chars into output buffer
            memcpy(buf, tmpbuff, readSize * sizeof(char));
            free(tmpbuff);
            return readSize;
        }

        // If path cannot be resolved
        else
        {
            return -ENOENT;
        }
    }
}

////////////// No need to modify anything below this point
int
vfat_opt_args(void *data, const char *arg, int key, struct fuse_args *oargs)
{
    if (key == FUSE_OPT_KEY_NONOPT && !vfat_info.dev) {
        vfat_info.dev = strdup(arg);
        return (0);
    }
    return (1);
}

struct fuse_operations vfat_available_ops = {
    .getattr = vfat_fuse_getattr,
    .getxattr = vfat_fuse_getxattr,
    .readdir = vfat_fuse_readdir,
    .read = vfat_fuse_read,
};

int main(int argc, char **argv)
{
    struct fuse_args args = FUSE_ARGS_INIT(argc, argv);

    fuse_opt_parse(&args, NULL, NULL, vfat_opt_args);

    if (!vfat_info.dev)
        errx(1, "missing file system parameter");

    vfat_init(vfat_info.dev);
    return (fuse_main(args.argc, args.argv, &vfat_available_ops, NULL));
}
