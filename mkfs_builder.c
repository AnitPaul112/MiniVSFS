// Build: gcc -O2 -std=c17 -Wall -Wextra mkfs_minivsfs.c -o mkfs_builder
#define _FILE_OFFSET_BITS 64
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <inttypes.h>
#include <errno.h>
#include <time.h>
#include <assert.h>

#define BS 4096u               
#define INODE_SIZE 128u
#define ROOT_INO 1u

uint64_t g_random_seed = 0; // This should be replaced by seed value from the CLI.

// below contains some basic structures you need for your project

// you are free to create more structures as you require

#pragma pack(push, 1)
typedef struct {
    // CREATE YOUR SUPERBLOCK HERE
    // ADD ALL FIELDS AS PROVIDED BY THE SPECIFICATION
    uint32_t magic;               
    uint32_t version;            
    uint32_t block_size;          
    uint64_t total_blocks;        
    uint64_t inode_count;         
    uint64_t inode_bitmap_start;  
    uint64_t inode_bitmap_blocks; 
    uint64_t data_bitmap_start;   
    uint64_t data_bitmap_blocks;  
    uint64_t inode_table_start;   
    uint64_t inode_table_blocks;  
    uint64_t data_region_start;   
    uint64_t data_region_blocks;  
    uint64_t root_inode;          
    uint64_t mtime_epoch;         
    uint32_t flags;               
    uint32_t checksum;            
} superblock_t;
#pragma pack(pop)
_Static_assert(sizeof(superblock_t) == 116, "superblock must fit in one block");

#pragma pack(push,1)
typedef struct {
    // CREATE YOUR INODE HERE
    // IF CREATED CORRECTLY, THE STATIC_ASSERT ERROR SHOULD BE GONE
    // ---- identity & ownership ----
    uint16_t mode;                
    uint16_t links;               
    uint32_t uid;                
    uint32_t gid;                
    uint64_t size_bytes;         
    uint64_t atime;               
    uint64_t mtime;              
    uint64_t ctime;               
    uint32_t direct[12];         
    uint32_t reserved_0;          
    uint32_t reserved_1;          
    uint32_t reserved_2;          
    uint32_t proj_id;             
    uint32_t uid16_gid16;         
    uint64_t xattr_ptr;           
    uint64_t inode_crc;   

} inode_t;
#pragma pack(pop)
_Static_assert(sizeof(inode_t)==INODE_SIZE, "inode size mismatch");

#pragma pack(push,1)
typedef struct {
    // CREATE YOUR DIRECTORY ENTRY STRUCTURE HERE
    // IF CREATED CORRECTLY, THE STATIC_ASSERT ERROR SHOULD BE GONE
    uint32_t inode_no;            
    uint8_t  type;                
    char     name[58];            
    uint8_t  checksum; 
} dirent64_t;
#pragma pack(pop)
_Static_assert(sizeof(dirent64_t)==64, "dirent size mismatch");


// ==========================DO NOT CHANGE THIS PORTION=========================
// These functions are there for your help. You should refer to the specifications to see how you can use them.
// ====================================CRC32====================================
uint32_t CRC32_TAB[256];
void crc32_init(void){
    for (uint32_t i=0;i<256;i++){
        uint32_t c=i;
        for(int j=0;j<8;j++) c = (c&1)?(0xEDB88320u^(c>>1)):(c>>1);
        CRC32_TAB[i]=c;
    }
}
uint32_t crc32(const void* data, size_t n){
    const uint8_t* p=(const uint8_t*)data; uint32_t c=0xFFFFFFFFu;
    for(size_t i=0;i<n;i++) c = CRC32_TAB[(c^p[i])&0xFF] ^ (c>>8);
    return c ^ 0xFFFFFFFFu;
}
// ====================================CRC32====================================

// WARNING: CALL THIS ONLY AFTER ALL OTHER SUPERBLOCK ELEMENTS HAVE BEEN FINALIZED
static uint32_t superblock_crc_finalize(superblock_t *sb) {
    sb->checksum = 0;
    uint32_t s = crc32((void *) sb, BS - 4);
    sb->checksum = s;
    return s;
}

// WARNING: CALL THIS ONLY AFTER ALL OTHER SUPERBLOCK ELEMENTS HAVE BEEN FINALIZED
void inode_crc_finalize(inode_t* ino){
    uint8_t tmp[INODE_SIZE]; memcpy(tmp, ino, INODE_SIZE);
    // zero crc area before computing
    memset(&tmp[120], 0, 8);
    uint32_t c = crc32(tmp, 120);
    ino->inode_crc = (uint64_t)c; // low 4 bytes carry the crc
}

// WARNING: CALL THIS ONLY AFTER ALL OTHER SUPERBLOCK ELEMENTS HAVE BEEN FINALIZED
void dirent_checksum_finalize(dirent64_t* de) {
    const uint8_t* p = (const uint8_t*)de;
    uint8_t x = 0;
    for (int i = 0; i < 63; i++) x ^= p[i];   // covers ino(4) + type(1) + name(58)
    de->checksum = x;
}

// ----------------- Simple bitmap helpers (beginner‑friendly macros) -----------------
#define BIT_SET(bm, bit)   ((bm)[(bit)>>3] |=  (uint8_t)(1u << ((bit)&7)))
#define BIT_CLEAR(bm, bit) ((bm)[(bit)>>3] &= (uint8_t)~(1u << ((bit)&7)))
#define BIT_TEST(bm, bit)  ((((bm)[(bit)>>3] >> ((bit)&7)) & 1u))

int main(int argc, char **argv) {
    crc32_init();
    // WRITE YOUR DRIVER CODE HERE
    // PARSE YOUR CLI PARAMETERS
    // THEN CREATE YOUR FILE SYSTEM WITH A ROOT DIRECTORY
    // THEN SAVE THE DATA INSIDE THE OUTPUT IMAGE

    const char *image_path = NULL;
    uint64_t size_kib = 0;
    uint64_t inode_count = 0;
    uint64_t seed = 0;

    // CLI parsing 
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--image") == 0 && i+1 < argc)           image_path = argv[++i];
        else if (strcmp(argv[i], "--size-kib") == 0 && i+1 < argc)   size_kib = strtoull(argv[++i], NULL, 10);
        else if (strcmp(argv[i], "--inodes") == 0 && i+1 < argc)     inode_count = strtoull(argv[++i], NULL, 10);
        else if (strcmp(argv[i], "--seed") == 0 && i+1 < argc)       seed = strtoull(argv[++i], NULL, 10);
        else {
            fprintf(stderr, "Usage: %s --image out.img --size-kib <180..4096> --inodes <128..512> [--seed N]\n", argv[0]);
            return 2;
        }
    }
    if (!image_path) { fprintf(stderr, "Error: missing --image\n"); return 2; }
    if (size_kib < 180 || size_kib > 4096 || (size_kib % 4)!=0) {
        fprintf(stderr, "Error: --size-kib must be a multiple of 4 in [180,4096]\n");
        return 2;
    }
    if (inode_count < 128 || inode_count > 512) {
        fprintf(stderr, "Error: --inodes must be in [128,512]\n");
        return 2;
    }
    g_random_seed = seed; 

    // compute layout numbers
    const uint64_t total_blocks = size_kib / 4;              
    const uint64_t inodes_per_block = BS / INODE_SIZE;      
    const uint64_t inode_table_blocks = (inode_count + inodes_per_block - 1) / inodes_per_block;

    const uint64_t super_start = 0;
    const uint64_t ibm_start   = 1;
    const uint64_t dbm_start   = 2;
    const uint64_t itab_start  = 3;
    const uint64_t data_start  = itab_start + inode_table_blocks;

    if (total_blocks <= data_start) {
        fprintf(stderr, "Error: image too small for metadata layout (need more blocks)\n");
        return 2;
    }
    const uint64_t data_blocks = total_blocks - data_start;
    if (data_blocks < 1) {
        fprintf(stderr, "Error: data region must have at least 1 block for the root directory\n");
        return 2;
    }
    if (data_blocks > (BS * 8)) {
        fprintf(stderr, "Error: data region too big for a single-block bitmap\n");
        return 2;
    }

    // allocate the whole image in memory and zero it 
    const uint64_t img_bytes = total_blocks * (uint64_t)BS;
    uint8_t *img = (uint8_t*)calloc(1, img_bytes);
    if (!img) { 
        fprintf(stderr, "Error: out of memory\n"); 
        return 1; 
    }
    #define BLK_PTR(bno) (img + (uint64_t)(bno) * BS)

    // ---------------- Superblock (block 0) ----------------
    superblock_t *sb = (superblock_t*)BLK_PTR(super_start);
    memset(sb, 0, sizeof(*sb));
    sb->magic               = 0x4D565346u;
    sb->version             = 1u;
    sb->block_size          = BS;
    sb->total_blocks        = total_blocks;
    sb->inode_count         = inode_count;
    sb->inode_bitmap_start  = ibm_start;
    sb->inode_bitmap_blocks = 1;
    sb->data_bitmap_start   = dbm_start;
    sb->data_bitmap_blocks  = 1;
    sb->inode_table_start   = itab_start;
    sb->inode_table_blocks  = inode_table_blocks;
    sb->data_region_start   = data_start;
    sb->data_region_blocks  = data_blocks;
    sb->root_inode          = ROOT_INO;
    sb->mtime_epoch         = (uint64_t)time(NULL);
    sb->flags               = 0;
    superblock_crc_finalize(sb);

    // ---------------- Bitmaps ----------------
    uint8_t *ibm = BLK_PTR(ibm_start);
    uint8_t *dbm = BLK_PTR(dbm_start);
    // allocate inode #1 (root) and the first data block for root directory
    BIT_SET(ibm, 0);
    BIT_SET(dbm, 0);

    // ---------------- Inode table ----------------
    inode_t *itab = (inode_t*)BLK_PTR(itab_start);
    inode_t root = {0};
    root.mode       = 0040000;                  
    root.links      = 2;                       
    root.uid        = 0;
    root.gid        = 0;
    root.size_bytes = BS;                       
    root.atime = root.mtime = root.ctime = (uint64_t)time(NULL);
    memset(root.direct, 0, sizeof(root.direct));
    root.direct[0]  = (uint32_t)data_start;     
    root.reserved_0 = root.reserved_1 = root.reserved_2 = 0;
    root.proj_id    = 6;
    root.uid16_gid16= 0;
    root.xattr_ptr  = 0;
    inode_crc_finalize(&root);
    itab[0] = root;                              

    // ---------------- Root directory data ----------------
    uint8_t *rblk = BLK_PTR(data_start);
    dirent64_t dot = {0}, dotdot = {0};
    dot.inode_no = ROOT_INO;  
    dot.type = 2;  
    memset(dot.name, 0, sizeof(dot.name));  
    strncpy(dot.name, ".",  sizeof(dot.name));  
    dirent_checksum_finalize(&dot);

    dotdot.inode_no = ROOT_INO; 
    dotdot.type = 2; 
    memset(dotdot.name, 0, sizeof(dotdot.name)); 
    strncpy(dotdot.name, "..", sizeof(dotdot.name)); 
    dirent_checksum_finalize(&dotdot);

    memcpy(rblk + 0*sizeof(dirent64_t), &dot,    sizeof(dirent64_t));
    memcpy(rblk + 1*sizeof(dirent64_t), &dotdot, sizeof(dirent64_t));


    // ---------------- Write the image to disk ----------------
    FILE *f = fopen(image_path, "wb");
    if (!f) { perror("fopen"); free(img); return 1; }
    size_t nw = fwrite(img, 1, img_bytes, f);
    if (nw != img_bytes) { 
        perror("fwrite"); 
        fclose(f); 
        free(img); 
        return 1; 
    }
    fclose(f);
    free(img);

    fprintf(stderr, "OK: created MiniVSFS image '%s'  blocks=%" PRIu64 "  inode_tbl=%" PRIu64 "  data=%" PRIu64 "\n",
            image_path, total_blocks, inode_table_blocks, data_blocks);
    return 0;
}
