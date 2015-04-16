/* CMSIS-DAP Interface Firmware
 * Copyright (c) 2009-2013 ARM Limited
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
#include <string.h>

#include "DAP_config.h"
#include "dap.h"
//#include "hid_callback.h"
#include "led.h"
#include "target_flash.h"

#define DBG_LPC1768
#if defined(DBG_LPC1768)
#   define WANTED_SIZE_IN_KB                        (512)
#elif defined(DBG_KL02Z)
#   define WANTED_SIZE_IN_KB                        (32)
#elif defined(DBG_KL05Z)
#   define WANTED_SIZE_IN_KB                        (32)
#elif defined(DBG_K24F256)
#   define WANTED_SIZE_IN_KB                        (256)
#elif defined(DBG_KL25Z)
#   define WANTED_SIZE_IN_KB                        (128)
#elif defined(DBG_KL26Z)
#   define WANTED_SIZE_IN_KB                        (128)
#elif defined(DBG_KL46Z)
#   define WANTED_SIZE_IN_KB                        (256)
#elif defined(DBG_K20D50M)
#   define WANTED_SIZE_IN_KB                        (128)
#elif defined(DBG_K22F)
#   define WANTED_SIZE_IN_KB                        (512)
#elif defined(DBG_K64F)
#   define WANTED_SIZE_IN_KB                        (1024)
#elif defined(DBG_LPC812)
#   define WANTED_SIZE_IN_KB                        (16)
#elif defined(DBG_LPC1114)
#   define WANTED_SIZE_IN_KB                        (32)
#elif defined(DBG_LPC4330)
#   if defined(BOARD_BAMBINO_210E)
#       define WANTED_SIZE_IN_KB                    (8192)
#   else
#       define WANTED_SIZE_IN_KB                    (4096)
#   endif
#elif defined(DBG_LPC1549)
#   define WANTED_SIZE_IN_KB                        (512)
#elif defined(DBG_LPC11U68)
#   define WANTED_SIZE_IN_KB                        (256)
#elif defined(DBG_LPC4337)
#   define WANTED_SIZE_IN_KB                        (1024)
#endif

//------------------------------------------------------------------- CONSTANTS
#define WANTED_SIZE_IN_BYTES        ((WANTED_SIZE_IN_KB + 16 + 8)*1024)
#define WANTED_SECTORS_PER_CLUSTER  (8)

#define FLASH_PROGRAM_PAGE_SIZE         (512)
#define MBR_BYTES_PER_SECTOR            (512)

//--------------------------------------------------------------------- DERIVED

#define MBR_NUM_NEEDED_SECTORS  (WANTED_SIZE_IN_BYTES / MBR_BYTES_PER_SECTOR)
#define MBR_NUM_NEEDED_CLUSTERS (MBR_NUM_NEEDED_SECTORS / WANTED_SECTORS_PER_CLUSTER)

/* Need 3 sectors/FAT for every 1024 clusters */
#define MBR_SECTORS_PER_FAT     1//(3*((MBR_NUM_NEEDED_CLUSTERS + 1023)/1024))

/* Macro to help fill the two FAT tables with the empty sectors without
   adding a lot of test #ifs inside the sectors[] declaration below */
#if (MBR_SECTORS_PER_FAT == 1)
#   define EMPTY_FAT_SECTORS
#elif (MBR_SECTORS_PER_FAT == 2)
#   define EMPTY_FAT_SECTORS  {fat2,0},
#elif (MBR_SECTORS_PER_FAT == 3)
#   define EMPTY_FAT_SECTORS  {fat2,0},{fat2,0},
#elif (MBR_SECTORS_PER_FAT == 6)
#   define EMPTY_FAT_SECTORS  {fat2,0},{fat2,0},{fat2,0},{fat2,0},{fat2,0},
#elif (MBR_SECTORS_PER_FAT == 9)
#   define EMPTY_FAT_SECTORS  {fat2,0},{fat2,0},{fat2,0},{fat2,0},{fat2,0},{fat2,0},{fat2,0},{fat2,0},
#elif (MBR_SECTORS_PER_FAT == 12)
#   define EMPTY_FAT_SECTORS  {fat2,0},{fat2,0},{fat2,0},{fat2,0},{fat2,0},{fat2,0},{fat2,0},{fat2,0},{fat2,0},{fat2,0},{fat2,0},
#else
#   error "Unsupported number of sectors per FAT table"
#endif

#define DIRENTS_PER_SECTOR  (MBR_BYTES_PER_SECTOR / sizeof(FatDirectoryEntry_t))

#define SECTORS_ROOT_IDX        (1 + mbr.num_fats*MBR_SECTORS_PER_FAT)
#define SECTORS_FIRST_FILE_IDX  (SECTORS_ROOT_IDX + 2)
#define SECTORS_SYSTEM_VOLUME_INFORMATION (SECTORS_FIRST_FILE_IDX  + WANTED_SECTORS_PER_CLUSTER)
#define SECTORS_INDEXER_VOLUME_GUID       (SECTORS_SYSTEM_VOLUME_INFORMATION + WANTED_SECTORS_PER_CLUSTER)
#define SECTORS_MBED_HTML_IDX   (SECTORS_INDEXER_VOLUME_GUID + WANTED_SECTORS_PER_CLUSTER)
#define SECTORS_ERROR_FILE_IDX  (SECTORS_MBED_HTML_IDX + WANTED_SECTORS_PER_CLUSTER)

//---------------------------------------------------------------- VERIFICATION

/* Sanity check */
#if (MBR_NUM_NEEDED_CLUSTERS > 4084)
  /* Limited by 12 bit cluster addresses, i.e. 2^12 but only 0x002..0xff5 can be used */
#   error Too many needed clusters, increase WANTED_SECTORS_PER_CLUSTER
#endif

#if ((WANTED_SECTORS_PER_CLUSTER * MBR_BYTES_PER_SECTOR) > 32768)
#   error Cluster size too large, must be <= 32KB
#endif

//-------------------------------------------------------------------- TYPEDEFS

typedef struct {
    uint8_t boot_sector[11];

    /* DOS 2.0 BPB - Bios Parameter Block, 13 bytes */
    uint16_t bytes_per_sector;
    uint8_t  sectors_per_cluster;
    uint16_t reserved_logical_sectors;
    uint8_t  num_fats;
    uint16_t max_root_dir_entries;
    uint16_t total_logical_sectors;            /* If num is too large for 16 bits, set to 0 and use big_sectors_on_drive */
    uint8_t  media_descriptor;
    uint16_t logical_sectors_per_fat;          /* Need 3 sectors/FAT for every 1024 clusters */

    /* DOS 3.31 BPB - Bios Parameter Block, 12 bytes */
    uint16_t physical_sectors_per_track;
    uint16_t heads;
    uint32_t hidden_sectors;
    uint32_t big_sectors_on_drive;             /* Use if total_logical_sectors=0, needed for large number of sectors */

    /* Extended BIOS Parameter Block, 26 bytes */
    uint8_t  physical_drive_number;
    uint8_t  not_used;
    uint8_t  boot_record_signature;
    uint32_t volume_id;
    char     volume_label[11];
    char     file_system_type[8];

    /* bootstrap data in bytes 62-509 */
    uint8_t  bootstrap[448];

    /* Mandatory value at bytes 510-511, must be 0xaa55 */
    uint16_t signature;
} __attribute__((packed))mbr_t;

typedef enum {
    BIN_FILE,
    PAR_FILE,
    DOW_FILE,
    CRD_FILE,
    SPI_FILE,
    UNSUP_FILE, /* Valid extension, but not supported */
    SKIP_FILE,  /* Unknown extension, typically Long File Name entries */
} FILE_TYPE;

typedef struct {
    FILE_TYPE type;
    char extension[3];
    uint32_t flash_offset;
} FILE_TYPE_MAPPING;

typedef struct FatDirectoryEntry {
    uint8_t filename[11];
    uint8_t attributes;
    uint8_t reserved;
    uint8_t creation_time_ms;
    uint16_t creation_time;
    uint16_t creation_date;
    uint16_t accessed_date;
    uint16_t first_cluster_high_16;
    uint16_t modification_time;
    uint16_t modification_date;
    uint16_t first_cluster_low_16;
    uint32_t filesize;
} __attribute__((packed)) FatDirectoryEntry_t;

//------------------------------------------------------------------------- END


typedef struct sector {
    const uint8_t * sect;
    unsigned int length;
} SECTOR;

#define MEDIA_DESCRIPTOR        (0xF0)

static const mbr_t mbr = {
    .boot_sector = {
        0xEB,0x3C, // x86 Jump
        0x90,      // NOP
        'M','S','W','I','N','4','.','1' // OEM Name in text
    },

    .bytes_per_sector         = MBR_BYTES_PER_SECTOR,
    .sectors_per_cluster      = WANTED_SECTORS_PER_CLUSTER,
    .reserved_logical_sectors = 1,
    .num_fats                 = 2,
    .max_root_dir_entries     = 32,
    .total_logical_sectors    = ((MBR_NUM_NEEDED_SECTORS > 32768) ? 0 : MBR_NUM_NEEDED_SECTORS),
    .media_descriptor         = MEDIA_DESCRIPTOR,
    .logical_sectors_per_fat  = MBR_SECTORS_PER_FAT, /* Need 3 sectors/FAT for every 1024 clusters */

    .physical_sectors_per_track = 1,
    .heads = 1,
    .hidden_sectors = 0,
    .big_sectors_on_drive = ((MBR_NUM_NEEDED_SECTORS > 32768) ? MBR_NUM_NEEDED_SECTORS : 0),

    .physical_drive_number = 0,
    .not_used = 0,
    .boot_record_signature = 0x29,
    .volume_id = 0x27021974,
    .volume_label = {'M','b','e','d',' ','U','S','B',' ',' ',' '},
    .file_system_type = {'F','A','T','1','2',' ',' ',' '},

    /* Executable boot code that starts the operating system */
    .bootstrap = {
        0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
        0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
        0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
        0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
        0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
        0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
        0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
        0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
        0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
        0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
        0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
        0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
        0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
        0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
        0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
        0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
        0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
        0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
        0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
        0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
        0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
        0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
        0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
        0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
        0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
        0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
        0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
        0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00
    },
    .signature = 0xAA55,
};


static const uint8_t fat1[] = {
    MEDIA_DESCRIPTOR, 0xFF,
    0xFF, 0xFF,
    0xFF, 0xFF,
    0xFF, 0xFF,
    0xFF, 0x00,
    0x00, 0x00,
    0x00, 0x00,
    0x00, 0x00,
};

static const uint8_t fat2[] = {0};

static const uint8_t fail[] = {
//    'F','A','I','L',' ',' ',' ',' ',                   // Filename
//    'T','X','T',                                       // Filename extension
//    0x20,                                              // File attributes
//    0x18,0xB1,0x74,0x76,0x8E,0x41,0x8E,0x41,0x00,0x00, // Reserved
//    0x8E,0x76,                                         // Time created or last updated
//    0x8E,0x41,                                         // Date created or last updated
//    0x06,0x00,                                         // Starting cluster number for file
//    0x07,0x00,0x00,0x0                                 // File size in bytes
};

// first 16 of the max 32 (mbr.max_root_dir_entries) root dir entries
static const uint8_t root_dir1[] = {
    // volume label "MBED"
    'M', 'B', 'E', 'D', 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x28, 0x0, 0x0, 0x0, 0x0,
    0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x85, 0x75, 0x8E, 0x41, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0,

    // Hidden files to keep mac happy

    // .fseventsd (LFN + normal entry)  (folder, size 0, cluster 2)
//    0x41, 0x2E, 0x0, 0x66, 0x0, 0x73, 0x0, 0x65, 0x0, 0x76, 0x0, 0xF, 0x0, 0xDA, 0x65, 0x0,
//    0x6E, 0x0, 0x74, 0x0, 0x73, 0x0, 0x64, 0x0, 0x0, 0x0, 0x0, 0x0, 0xFF, 0xFF, 0xFF, 0xFF,
//
//    0x46, 0x53, 0x45, 0x56, 0x45, 0x4E, 0x7E, 0x31, 0x20, 0x20, 0x20, 0x12, 0x0, 0x47, 0x7D, 0x75,
//    0x8E, 0x41, 0x8E, 0x41, 0x0, 0x0, 0x7D, 0x75, 0x8E, 0x41, 0x2, 0x0, 0x0, 0x0, 0x0, 0x0,
//
//    // .metadata_never_index (LFN + LFN + normal entry)  (size 0, cluster 0)
//    0x42, 0x65, 0x0, 0x72, 0x0, 0x5F, 0x0, 0x69, 0x0, 0x6E, 0x0, 0xF, 0x0, 0xA8, 0x64, 0x0,
//    0x65, 0x0, 0x78, 0x0, 0x0, 0x0, 0xFF, 0xFF, 0xFF, 0xFF, 0x0, 0x0, 0xFF, 0xFF, 0xFF, 0xFF,
//
//    0x1, 0x2E, 0x0, 0x6D, 0x0, 0x65, 0x0, 0x74, 0x0, 0x61, 0x0, 0xF, 0x0, 0xA8, 0x64, 0x0,
//    0x61, 0x0, 0x74, 0x0, 0x61, 0x0, 0x5F, 0x0, 0x6E, 0x0, 0x0, 0x0, 0x65, 0x0, 0x76, 0x0,
//
//    0x4D, 0x45, 0x54, 0x41, 0x44, 0x41, 0x7E, 0x31, 0x20, 0x20, 0x20, 0x22, 0x0, 0x32, 0x85, 0x75,
//    0x8E, 0x41, 0x8E, 0x41, 0x0, 0x0, 0x85, 0x75, 0x8E, 0x41, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0,
//
//    // .Trashes (LFN + normal entry)  (size 0, cluster 0)
//    0x41, 0x2E, 0x0, 0x54, 0x0, 0x72, 0x0, 0x61, 0x0, 0x73, 0x0, 0xF, 0x0, 0x25, 0x68, 0x0,
//    0x65, 0x0, 0x73, 0x0, 0x0, 0x0, 0xFF, 0xFF, 0xFF, 0xFF, 0x0, 0x0, 0xFF, 0xFF, 0xFF, 0xFF,
//
//    0x54, 0x52, 0x41, 0x53, 0x48, 0x45, 0x7E, 0x31, 0x20, 0x20, 0x20, 0x22, 0x0, 0x32, 0x85, 0x75,
//    0x8E, 0x41, 0x8E, 0x41, 0x0, 0x0, 0x85, 0x75, 0x8E, 0x41, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0,
//
//    // Hidden files to keep windows 8.1 happy
//    0x42, 0x20, 0x00, 0x49, 0x00, 0x6E, 0x00, 0x66, 0x00, 0x6F, 0x00, 0x0F, 0x00, 0x72, 0x72, 0x00,
//    0x6D, 0x00, 0x61, 0x00, 0x74, 0x00, 0x69, 0x00, 0x6F, 0x00, 0x00, 0x00, 0x6E, 0x00, 0x00, 0x00,
//
//    0x01, 0x53, 0x00, 0x79, 0x00, 0x73, 0x00, 0x74, 0x00, 0x65, 0x00, 0x0F, 0x00, 0x72, 0x6D, 0x00,
//    0x20, 0x00, 0x56, 0x00, 0x6F, 0x00, 0x6C, 0x00, 0x75, 0x00, 0x00, 0x00, 0x6D, 0x00, 0x65, 0x00,
//
//    0x53, 0x59, 0x53, 0x54, 0x45, 0x4D, 0x7E, 0x31, 0x20, 0x20, 0x20, 0x16, 0x00, 0xA5, 0x85, 0x8A,
//    0x73, 0x43, 0x73, 0x43, 0x00, 0x00, 0x86, 0x8A, 0x73, 0x43, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00,
//
//    // mbed html file (size 512, cluster 3)
//    'M', 'B', 'E', 'D', 0x20, 0x20, 0x20, 0x20, 'H', 'T', 'M', 0x20, 0x18, 0xB1, 0x74, 0x76,
//    0x8E, 0x41, 0x8E, 0x41, 0x0, 0x0, 0x8E, 0x76, 0x8E, 0x41, 0x05, 0x0, 0x00, 0x02, 0x0, 0x0,
};

// last 16 of the max 32 (mbr.max_root_dir_entries) root dir entries
static const uint8_t root_dir2[] = {0};

static const uint8_t sect5[] = {
    // .   (folder, size 0, cluster 2)
//    0x2E, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x32, 0x0, 0x47, 0x7D, 0x75,
//    0x8E, 0x41, 0x8E, 0x41, 0x0, 0x0, 0x88, 0x75, 0x8E, 0x41, 0x2, 0x0, 0x0, 0x0, 0x0, 0x0,
//
//    // ..   (folder, size 0, cluster 0)
//    0x2E, 0x2E, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x10, 0x0, 0x47, 0x7D, 0x75,
//    0x8E, 0x41, 0x8E, 0x41, 0x0, 0x0, 0x7D, 0x75, 0x8E, 0x41, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0,
//
//    // NO_LOG  (size 0, cluster 0)
//    0x4E, 0x4F, 0x5F, 0x4C, 0x4F, 0x47, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x8, 0x32, 0x85, 0x75,
//    0x8E, 0x41, 0x8E, 0x41, 0x0, 0x0, 0x85, 0x75, 0x8E, 0x41, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0,
};

static const uint8_t sect6[] = {
//    0x2E, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x10, 0x00, 0xA5, 0x85, 0x8A,
//    0x73, 0x43, 0x73, 0x43, 0x00, 0x00, 0x86, 0x8A, 0x73, 0x43, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00,
//
//    0x2E, 0x2E, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x10, 0x00, 0xA5, 0x85, 0x8A,
//    0x73, 0x43, 0x73, 0x43, 0x00, 0x00, 0x86, 0x8A, 0x73, 0x43, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
//
//    // IndexerVolumeGuid (size0, cluster 0)
//    0x42, 0x47, 0x00, 0x75, 0x00, 0x69, 0x00, 0x64, 0x00, 0x00, 0x00, 0x0F, 0x00, 0xFF, 0xFF, 0xFF,
//    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x00, 0xFF, 0xFF, 0xFF, 0xFF,
//
//    0x01, 0x49, 0x00, 0x6E, 0x00, 0x64, 0x00, 0x65, 0x00, 0x78, 0x00, 0x0F, 0x00, 0xFF, 0x65, 0x00,
//    0x72, 0x00, 0x56, 0x00, 0x6F, 0x00, 0x6C, 0x00, 0x75, 0x00, 0x00, 0x00, 0x6D, 0x00, 0x65, 0x00,
//
//    0x49, 0x4E, 0x44, 0x45, 0x58, 0x45, 0x7E, 0x31, 0x20, 0x20, 0x20, 0x20, 0x00, 0xA7, 0x85, 0x8A,
//    0x73, 0x43, 0x73, 0x43, 0x00, 0x00, 0x86, 0x8A, 0x73, 0x43, 0x04, 0x00, 0x4C, 0x00, 0x00, 0x00
};

static const uint8_t sect7[] = {
//	0x7B, 0x00, 0x39, 0x00, 0x36, 0x00, 0x36, 0x00, 0x31, 0x00, 0x39, 0x00, 0x38, 0x00, 0x32, 0x00,
//	0x30, 0x00, 0x2D, 0x00, 0x37, 0x00, 0x37, 0x00, 0x44, 0x00, 0x31, 0x00, 0x2D, 0x00, 0x34, 0x00,
//	0x46, 0x00, 0x38, 0x00, 0x38, 0x00, 0x2D, 0x00, 0x38, 0x00, 0x46, 0x00, 0x35, 0x00, 0x33, 0x00,
//	0x2D, 0x00, 0x36, 0x00, 0x32, 0x00, 0x44, 0x00, 0x39, 0x00, 0x37, 0x00, 0x46, 0x00, 0x35, 0x00,
//	0x46, 0x00, 0x34, 0x00, 0x46, 0x00, 0x46, 0x00, 0x39, 0x00, 0x7D, 0x00, 0x00, 0x00, 0x00, 0x00
};

SECTOR sectors[] = {
    /* Reserved Sectors: Master Boot Record */
    {(const uint8_t *)&mbr , 512},

    /* FAT Region: FAT1 */
    {fat1, sizeof(fat1)},   // fat1, sect1
    EMPTY_FAT_SECTORS

    /* FAT Region: FAT2 */
    {fat2, 0},              // fat2, sect1
    EMPTY_FAT_SECTORS

    /* Root Directory Region */
    {root_dir1, sizeof(root_dir1)}, // first 16 of the max 32 (mbr.max_root_dir_entries) root dir entries
    {root_dir2, sizeof(root_dir2)}, // last 16 of the max 32 (mbr.max_root_dir_entries) root dir entries

    /* Data Region */

    // Section for mac compatibility
    {sect5, sizeof(sect5)},

};


uint32_t InitOffset;
uint32_t TotalLength;


#define SWD_ERROR               0
#define BAD_EXTENSION_FILE      1
#define NOT_CONSECUTIVE_SECTORS 2
#define SWD_PORT_IN_USE         3
#define RESERVED_BITS           4
#define BAD_START_SECTOR        5
#define TIMEOUT                 6

static uint8_t * reason_array[] = {
    "SWD ERROR",
    "BAD EXTENSION FILE",
    "NOT CONSECUTIVE SECTORS",
    "SWD PORT IN USE",
    "RESERVED BITS",
    "BAD START SECTOR",
    "TIMEOUT",
};

/********************************************************************************************************//**
 * @brief     MSC_Flash_Init : Initialization of flash
 * @param[in] dev  : flash device
 * @return    None
************************************************************************************************************/
uint32_t MSC_Flash_Init(void *dev)
{
//  int i;
  MSC_MemorySize = MBR_NUM_NEEDED_SECTORS * MBR_BYTES_PER_SECTOR;
  MSC_BlockSize = 512;
  MSC_BlockCount = MSC_MemorySize/MSC_BlockSize;

//  memcpy(DiskImage, sectors[0].sect, 512);
        return 0;
}


/********************************************************************************************************//**
 * @brief     MSC_Flash_Read : read data to memory
 * @param[in] dev  : flash device
 *            addr : flash address
 *            wbuf : buffer for read
 *            wlen : length for read
 * @return    None
************************************************************************************************************/
uint32_t MSC_Flash_Read(void *dev, uint32_t addr, uint8_t *rbuf, uint32_t rlen)
{
  int i;
  uint32_t block = addr/512;
  uint32_t offset = addr%512;
  if (1) {
       // blink led not permanently
       //main_blink_msd_led(0);
       memset(rbuf, 0, rlen);

       // Handle MBR, FAT1 sectors, FAT2 sectors, root1, root2 and mac file
       if (block <= SECTORS_FIRST_FILE_IDX) {
           if(sectors[block].length >= offset+rlen){
           memcpy(rbuf, &sectors[block].sect[offset], rlen);
           }
           else {
//               memcpy(rbuf, &sectors[block].sect[offset], rlen);
          }
       }
   }
//  if(addr<512)
//  {
////      for (i = 0; i < rlen; i++) {
////          rbuf[addr+i] = DiskImage[addr+i];
////      }
////    memcpy(rbuf, &DiskImage[addr], rlen);
//    memcpy(rbuf, &sectors[0].sect[addr], rlen);
//  }
//  else {
////      memcpy(rbuf, 0, rlen);
//  }
        return 0;
}

static uint8_t reason = 0;
static uint32_t size;
static uint32_t begin_sector;
static uint32_t nb_sector;
static uint8_t good_file =0;
static uint32_t flash_addr_offset = 0;
static uint8_t sector_received_first=0;
static uint8_t root_dir_received_first=0;
static uint8_t erase_chip = 0;
static uint8_t usb_buffer[512];
static uint32_t usb_buffer_len = 0;
static uint8_t flash_flag = 0;
static uint32_t flash_offset = 0;
uint8_t USB_DISConnect_Flag = 0;

static const FILE_TYPE_MAPPING file_type_infos[] = {
    { BIN_FILE, {'B', 'I', 'N'}, 0x00000000 },
    { BIN_FILE, {'b', 'i', 'n'}, 0x00000000 },
    { PAR_FILE, {'P', 'A', 'R'}, 0x00000000 },//strange extension on win IE 9...
    { DOW_FILE, {'D', 'O', 'W'}, 0x00000000 },//strange extension on mac...
    { CRD_FILE, {'C', 'R', 'D'}, 0x00000000 },//strange extension on linux...
    { UNSUP_FILE, {0,0,0},     0            },//end of table marker
};
static FILE_TYPE get_file_type(const FatDirectoryEntry_t* pDirEnt, uint32_t* pAddrOffset) {
    int i;
    char e0 = pDirEnt->filename[8];
    char e1 = pDirEnt->filename[9];
    char e2 = pDirEnt->filename[10];
    char f0 = pDirEnt->filename[0];
    for (i = 0; file_type_infos[i].type != UNSUP_FILE; i++) {
        if ((e0 == file_type_infos[i].extension[0]) &&
            (e1 == file_type_infos[i].extension[1]) &&
            (e2 == file_type_infos[i].extension[2])) {
            *pAddrOffset = file_type_infos[i].flash_offset;
            return file_type_infos[i].type;
        }
    }

    // Now test if the file has a valid extension and a valid name.
    // This is to detect correct but unsupported 8.3 file names.
    if (( ((e0 >= 'a') && (e0 <= 'z')) || ((e0 >= 'A') && (e0 <= 'Z')) ) &&
        ( ((e1 >= 'a') && (e1 <= 'z')) || ((e1 >= 'A') && (e1 <= 'Z')) || (e1 == 0x20) ) &&
        ( ((e2 >= 'a') && (e2 <= 'z')) || ((e2 >= 'A') && (e2 <= 'Z')) || (e2 == 0x20) ) &&
        ( ((f0 >= 'a') && (f0 <= 'z')) || ((f0 >= 'A') && (f0 <= 'Z')) ) &&
           (f0 != '.' &&
           (f0 != '_')) ) {
        *pAddrOffset = 0;
        return UNSUP_FILE;
    }

    *pAddrOffset = 0;
    return SKIP_FILE;
}
// take a look here: http://cs.nyu.edu/~gottlieb/courses/os/kholodov-fat.html
// to have info on fat file system
int search_bin_file(uint8_t * root, uint8_t sector) {
    // idx is a pointer inside the root dir
    // we begin after all the existing entries
    int idx = 0;
    uint8_t found = 0;
    uint32_t i = 0;
    uint32_t move_sector_start = 0, nb_sector_to_move = 0;
    FILE_TYPE file_type;
    uint8_t hidden_file = 0, adapt_th_sector = 0;
    uint32_t offset = 0;

    FatDirectoryEntry_t* pDirEnts = (FatDirectoryEntry_t*)root;


    // first check that we did not receive any directory
    // if we detect a directory -> disconnect / failed
    for (i = 0; i < 2; i++) {
        if (pDirEnts[i].attributes & 0x10) {
            reason = BAD_EXTENSION_FILE;
            //initDisconnect(0);
            return -1;
        }
    }

    // now do the real search for a valid .bin file
    for (i = 0; i < 2; i++) {

        // Determine file type and get the flash offset
        file_type = get_file_type(&pDirEnts[i], &offset);

        if (file_type == BIN_FILE || file_type == PAR_FILE ||
            file_type == DOW_FILE || file_type == CRD_FILE || file_type == SPI_FILE) {

            hidden_file = (pDirEnts[i].attributes & 0x02) ? 1 : 0;

            // compute the size of the file
            size = pDirEnts[i].filesize;

            if (size == 0) {
              // skip empty files
                continue;
            }

            // read the cluster number where data are stored (ignoring the
            // two high bytes in the cluster number)
            //
            // Convert cluster number to sector number by moving past the root
            // dir and fat tables.
            //
            // The cluster numbers start at 2 (0 and 1 are never used).
            begin_sector = (pDirEnts[i].first_cluster_low_16 - 2) * WANTED_SECTORS_PER_CLUSTER + SECTORS_FIRST_FILE_IDX;

            // compute the number of sectors
            nb_sector = (size + MBR_BYTES_PER_SECTOR - 1) / MBR_BYTES_PER_SECTOR;

//            if ( (pDirEnts[i].filename[0] == '_') ||
//                 (pDirEnts[i].filename[0] == '.') ||
//                 (hidden_file && ((pDirEnts[i].filename[0] == '_') || (pDirEnts[i].filename[0] == '.'))) ||
//                 ((pDirEnts[i].filename[0] == 0xE5) && (file_type != CRD_FILE) && (file_type != PAR_FILE))) {
//                if (theoretical_start_sector == begin_sector) {
//                    adapt_th_sector = 1;
//                }
//                size = 0;
//                nb_sector = 0;
//                continue;
//            }

            // if we receive a file with a valid extension
            // but there has been program page error previously
            // we fail / disconnect usb
//            if ((program_page_error == 1) && (maybe_erase == 0) && (start_sector >= begin_sector)) {
//                reason = RESERVED_BITS;
//                initDisconnect(0);
//                return -1;
//            }

//            adapt_th_sector = 0;

            // on mac, with safari, we receive all the files with some more sectors at the beginning
            // we have to move the sectors... -> 2x slower
//            if ((start_sector != 0) && (start_sector < begin_sector) && (current_sector - (begin_sector - start_sector) >= nb_sector)) {
//
//                // we need to copy all the sectors
//                // we don't listen to msd interrupt
//                listen_msc_isr = 0;
//
//                move_sector_start = (begin_sector - start_sector)*MBR_BYTES_PER_SECTOR;
//                nb_sector_to_move = (nb_sector % 2) ? nb_sector/2 + 1 : nb_sector/2;
//                for (i = 0; i < nb_sector_to_move; i++) {
//                    if (!swd_read_memory(move_sector_start + i*FLASH_SECTOR_SIZE, (uint8_t *)usb_buffer, FLASH_SECTOR_SIZE)) {
//                        failSWD();
//                        return -1;
//                    }
//                    if (!target_flash_erase_sector(i)) {
//                        failSWD();
//                        return -1;
//                    }
//                    if (!target_flash_program_page(i*FLASH_SECTOR_SIZE, (uint8_t *)usb_buffer, FLASH_SECTOR_SIZE)) {
//                        failSWD();
//                        return -1;
//                    }
//                }
//                initDisconnect(1);
//                return -1;
//            }

            found = 1;
            idx = i; // this is the file we want
            good_file = 1;
#if defined(BOARD_UBLOX_C027)
            if (0 == memcmp((const char*)pDirEnts[i].filename, "~AUTORST", 8))
                good_file = 2;
            else if (0 == memcmp((const char*)pDirEnts[i].filename, "~AUTOCRP", 8))
                good_file = 3;
#endif
            flash_addr_offset = offset;
            break;
        }
        // if we receive a new file which does not have the good extension
        // fail and disconnect usb
        else if (file_type == UNSUP_FILE) {
            reason = BAD_EXTENSION_FILE;
//            initDisconnect(0);
            return -1;
        }
    }

//    if (adapt_th_sector) {
//        theoretical_start_sector += nb_sector;
//        init(0);
//    }
    return (found == 1) ? idx : -1;
}


//extern const unsigned char data[1148];


static init(int i)
{
   reason = 0;
   size = 0;
   begin_sector = 0;
   nb_sector = 0;
   good_file =0;
   flash_addr_offset = 0;
   sector_received_first=0;
   root_dir_received_first=0;
   erase_chip = 0;
   usb_buffer_len = 0;
   flash_flag = 0;
   flash_offset = 0;
   USB_DISConnect_Flag = 0;

}
static void initDisconnect(uint8_t success) {
#if defined(BOARD_UBLOX_C027)
    int autorst = (good_file == 2) && success;
    int autocrp = (good_file == 3) && success;
    if (autocrp)
    {
        // first we need to discoonect the usb stack
        usbd_connect(0);

        enter_isp();
    }
#else
    int autorst = 0;
#endif
    //drag_success = success;
    if (autorst)
        swd_set_target_state(RESET_RUN);
    //main_blink_msd_led(0);
    init(1);
    //isr_evt_set(MSC_TIMEOUT_STOP_EVENT, msc_valid_file_timeout_task_id);
    if (!autorst)
    {
        // event to disconnect the usb
        //main_usb_disconnect_event();
//        LPC_USBConnect(0);
        USB_DISConnect_Flag = 1;
    }
    semihost_enable();
}

/********************************************************************************************************//**
 * @brief     MSC_Flash_Writev : write data to memory
 * @param[in] dev  : flash device
 *            addr : flash address
 *            wbuf : buffer for write
 *            wlen : length for write
 * @return    None
************************************************************************************************************/
uint32_t MSC_Flash_Write(void *dev, uint32_t addr, uint8_t *rbuf, uint32_t rlen)
{
  USBHID_Dev* devs = (USBHID_Dev*) dev;
  uint32_t block = addr/512;
  uint32_t offset = addr%512;
  int idx_size = 0;
//  if (rlen !=64) {
//      GPIOSetValue(LED_RED_PORT,LED_RED_PIN,0);
//  }
  if ((block == SECTORS_ROOT_IDX) || (block == (SECTORS_ROOT_IDX+1))) {
//    if (!(offset%64 == 0)) {
//        GPIOSetValue(LED_RED_PORT,LED_RED_PIN,0);
//    }
      idx_size = search_bin_file(rbuf, block);
      if (idx_size != -1) {
          if (sector_received_first == 0) {
              root_dir_received_first = 1;
//              GPIOSetValue(LED_RED_PORT,LED_RED_PIN,0);
              erase_chip = 1;
          }
      }

  }
  if (block >= SECTORS_FIRST_FILE_IDX) {
      if (root_dir_received_first == 0) {
          sector_received_first = 1;
//          GPIOSetValue(LED_RED_PORT,LED_RED_PIN,0);
      }
      if (block >= begin_sector) {
          if(erase_chip == 1)
          {
              erase_chip = 0;
              target_set_state(RESET_PROGRAM);
              target_flash_init(50000000);
              target_flash_erase_chip();
//              target_flash_program_page(0, data, 1148);
//              GPIOSetValue(LED_RED_PORT,LED_RED_PIN,1);
          }
//          target_flash_program_page(flash_offset, rbuf, rlen);
//          flash_offset += rlen;
          if(usb_buffer_len < 512)
          {
            memcpy(&usb_buffer[usb_buffer_len], rbuf, rlen);

            usb_buffer_len += rlen;

            if (usb_buffer_len >= 512) {
              flash_flag = 1;
              usb_buffer_len = 0;
//              GPIOSetValue(LED_RED_PORT,LED_RED_PIN,1);
            }
          }
          if(flash_flag == 1)
          {
            target_flash_program_page(flash_offset, usb_buffer, 512);

            flash_flag = 0;
            flash_offset += 512;
//            GPIOSetValue(LED_RED_PORT,LED_RED_PIN,0);
          }
      }
      if ((block >= nb_sector) && (flash_offset >= size))
      {
          initDisconnect(0);
      }
  }
        return 0;
}
