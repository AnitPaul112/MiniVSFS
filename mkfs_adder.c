#define _FILE_OFFSET_BITS 64
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <inttypes.h>
#include <errno.h>
#include <time.h>

#define BS 4096u
#define INODE_SIZE 128u
#define ROOT_INO 1u
#define DIRECT_MAX 12
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
    for (int i = 0; i < 63; i++) x ^= p[i];   
    de->checksum = x;
}

// ----------------- Simple bitmap helpers (beginner‑friendly macros) -----------------
#define BIT_SET(bm, bit)   ((bm)[(bit)>>3] |=  (uint8_t)(1u << ((bit)&7)))
#define BIT_TEST(bm, bit)  ((((bm)[(bit)>>3] >> ((bit)&7)) & 1u))

int main(int argc, char **argv) {
    crc32_init();
    // WRITE YOUR DRIVER CODE HERE
    // PARSE YOUR CLI PARAMETERS
    // THEN ADD THE SPECIFIED FILE TO YOUR FILE SYSTEM
    // UPDATE THE .IMG FILE ON DISK

    const char *in_path = NULL, *out_path = NULL, *file_path = NULL;

    // Simple CLI parsing
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--input") == 0 && i+1 < argc)   in_path  = argv[++i];
        else if (strcmp(argv[i], "--output") == 0 && i+1 < argc) out_path = argv[++i];
        else if (strcmp(argv[i], "--file") == 0 && i+1 < argc)   file_path= argv[++i];
        else {
            fprintf(stderr, "Usage: %s --input in.img --output out.img --file <path>\n", argv[0]);
            return 2;
        }
    }
    if (!in_path || !out_path || !file_path) { fprintf(stderr, "Error: all three flags are required\n"); return 2; }

    // Read input image fully into memory
    FILE *fi = fopen(in_path, "rb");
    if (!fi) { 
        perror("fopen input"); 
        return 1; 
    }
    if (fseek(fi, 0, SEEK_END) != 0) { 
        perror("fseek"); 
        fclose(fi); 
        return 1; 
    }
    long long flen = ftell(fi);
    if (flen < 0) { 
        perror("ftell"); fclose(fi); return 1; 
    }
    if (fseek(fi, 0, SEEK_SET) != 0) { 
        perror("fseek"); 
        fclose(fi); 
        return 1; 
    }
    uint8_t *img = (uint8_t*)malloc((size_t)flen);
    if (!img) { 
        fclose(fi); 
        fprintf(stderr, "Error: OOM\n"); 
        return 1; 
    }
    if (fread(img, 1, (size_t)flen, fi) != (size_t)flen) { 
        perror("fread"); 
        fclose(fi); 
        free(img); 
        return 1; 
    }
    fclose(fi);

    // Parse superblock
    if ((size_t)flen < BS) { 
        free(img); 
        fprintf(stderr, "Error: image too small\n"); 
        return 1; 
    }
    superblock_t *sb = (superblock_t*)(img + 0);
    if (sb->magic != 0x4D565346u || sb->version != 1u || sb->block_size != BS) {
        free(img); 
        fprintf(stderr, "Error: not a MiniVSFS image\n"); 
        return 1; 
    }
    uint64_t total_blocks = sb->total_blocks;
    if ((uint64_t)flen != total_blocks * (uint64_t)BS) {
        free(img); 
        fprintf(stderr, "Error: image length and superblock disagree\n"); 
        return 1; }

    // Map important regions
    uint8_t *ibm = img + sb->inode_bitmap_start * BS;
    uint8_t *dbm = img + sb->data_bitmap_start  * BS;
    inode_t *itab = (inode_t*)(img + sb->inode_table_start * BS);

    // Open the host file to add
    FILE *ff = fopen(file_path, "rb");
    if (!ff) { 
        perror("fopen file"); 
        free(img); 
        return 1; 
    }
    if (fseek(ff, 0, SEEK_END) != 0) { 
        perror("fseek file"); 
        fclose(ff); 
        free(img); 
        return 1; 
    }
    long long fsize_ll = ftell(ff);
    if (fsize_ll < 0) { 
        perror("ftell file"); 
        fclose(ff); 
        free(img); 
        return 1; 
    }
    if (fseek(ff, 0, SEEK_SET) != 0) { 
        perror("fseek file 2"); 
        fclose(ff); 
        free(img); 
        return 1; 
    }
    uint64_t fsize = (uint64_t)fsize_ll;

    uint64_t need_blocks = (fsize + BS - 1) / BS;
    if (need_blocks == 0) need_blocks = 1;
    if (need_blocks > DIRECT_MAX) {
        fprintf(stderr, "Error: file too big for MiniVSFS (needs %" PRIu64 " blocks, max %d)\n", need_blocks, DIRECT_MAX);
        fclose(ff); 
        free(img); 
        return 1;
    }

    // Find a free inode (first-fit)
    uint64_t inum = 0;
    for (uint64_t i = 0; i < sb->inode_count; i++) {
        if (!BIT_TEST(ibm, i)) { 
            inum = i + 1; 
            BIT_SET(ibm, i); 
            break; 
        }
    }
    if (inum == 0) { 
        fclose(ff); 
        free(img); 
        fprintf(stderr, "Error: no free inode\n"); 
        return 1; 
    }

    // Find free data blocks (first-fit)
    uint32_t direct[DIRECT_MAX] = {0};
    uint64_t found = 0;
    for (uint64_t i = 0; i < sb->data_region_blocks && found < need_blocks; i++) {
        if (!BIT_TEST(dbm, i)) {
            BIT_SET(dbm, i);
            direct[found++] = (uint32_t)(sb->data_region_start + i); 
        }
    }
    if (found < need_blocks) { 
        fclose(ff); 
        free(img); 
        fprintf(stderr, "Error: not enough free data blocks\n"); 
        return 1; }

    // Write file data into the newly allocated blocks
    for (uint64_t k = 0; k < need_blocks; k++) {
        uint8_t *blk = img + (uint64_t)direct[k] * BS;
        size_t to_read = (size_t)((k + 1 < need_blocks) ? BS : (fsize - k * BS));
        if (to_read == 0) to_read = BS;
        size_t nr = fread(blk, 1, to_read, ff);
        if (nr != to_read) { 
            perror("fread payload"); 
            fclose(ff); 
            free(img); 
            return 1; 
        }
        if (to_read < BS) memset(blk + to_read, 0, BS - to_read);
    }
    fclose(ff);

    // Create the new inode
    inode_t node; memset(&node, 0, sizeof(node));
    node.mode  = 0100000;  
    node.links = 1;
    node.uid = node.gid = 0;
    node.size_bytes = fsize;
    node.atime = node.mtime = node.ctime = (uint64_t)time(NULL);
    node.proj_id = 6;
    for (uint64_t k = 0; k < need_blocks; k++) node.direct[k] = direct[k];
    inode_crc_finalize(&node);
    itab[inum - 1] = node;

    // Add a directory entry into root
    inode_t *root = &itab[ROOT_INO - 1];
    if (root->direct[0] == 0) { free(img); fprintf(stderr, "Error: corrupt FS (root has no block)\n"); return 1; }
    uint8_t *rblk = img + (uint64_t)root->direct[0] * BS;
    size_t max_entries = BS / sizeof(dirent64_t); 
    size_t pos = 0;
    for (; pos < max_entries; pos++) {
        dirent64_t *de = (dirent64_t*)(rblk + pos * sizeof(dirent64_t));
        if (de->inode_no == 0) break;
    }
    if (pos == max_entries) { free(img); fprintf(stderr, "Error: root directory full\n"); return 1; }

    dirent64_t de; memset(&de, 0, sizeof(de));
    de.inode_no = (uint32_t)inum;
    de.type = 1;
    // place just the base name, truncate to 58 bytes if needed
    const char *slash = strrchr(file_path, '/');
    const char *base = slash ? slash + 1 : file_path;
    strncpy(de.name, base, sizeof(de.name));
    dirent_checksum_finalize(&de);
    memcpy(rblk + pos * sizeof(dirent64_t), &de, sizeof(de));

    // Update root metadata
    if (root->links < 0xFFFF) root->links++;
    root->mtime = (uint64_t)time(NULL);
    inode_crc_finalize(root);

    // Save to output
    FILE *fo = fopen(out_path, "wb");
    if (!fo) { perror("fopen output"); 
        free(img); 
        return 1; 
    }
    size_t nw = fwrite(img, 1, (size_t)flen, fo);
    if (nw != (size_t)flen) { perror("fwrite output"); 
        fclose(fo); free(img); 
        return 1; 
    }
    fclose(fo);
    free(img);

    fprintf(stderr, "OK: added '%s' as inode=%" PRIu64 " (%" PRIu64 " bytes) -> %s\n", base, inum, fsize, out_path);
    return 0;
}
