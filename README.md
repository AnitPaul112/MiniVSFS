
# MiniVSFS File System Tools

This project provides two C utilities for working with the MiniVSFS (Mini Virtual Simple File System) disk image format:

- **`mkfs_builder.c`**: Creates a new MiniVSFS disk image with a root directory.
- **`mkfs_adder.c`**: Adds a file from the host system into an existing MiniVSFS disk image.

Both tools are designed for educational purposes and demonstrate low-level file system manipulation in C.

---

## 1. mkfs_builder.c

### Purpose
Creates a new MiniVSFS disk image, initializing all metadata, bitmaps, inode table, and a root directory.

### Build
```sh
gcc -O2 -std=c17 -Wall -Wextra mkfs_builder.c -o mkfs_builder
```

### Usage
```sh
./mkfs_builder --image out.img --size-kib <180..4096> --inodes <128..512> [--seed N]
```

- `--image out.img`: Path to the output image file.
- `--size-kib`: Size of the image in KiB (must be a multiple of 4, between 180 and 4096).
- `--inodes`: Number of inodes (between 128 and 512).
- `--seed N`: (Optional) Random seed for reproducibility.

---

## 2. mkfs_adder.c

### Purpose
Adds a file from the host system into an existing MiniVSFS disk image, updating all relevant metadata and directory entries.

### Build
```sh
gcc -O2 -std=c17 -Wall -Wextra mkfs_adder.c -o mkfs_adder
```

### Usage
```sh
./mkfs_adder --input in.img --output out.img --file <path>
```

- `--input in.img`: Path to the input MiniVSFS image file.
- `--output out.img`: Path to the output MiniVSFS image file (with the new file added).
- `--file <path>`: Path to the file on the host system to add to the image.

---

## File System Structure
- **Superblock**: Contains metadata about the file system.
- **Inode Table**: Stores file metadata (mode, size, timestamps, block pointers, etc.).
- **Bitmaps**: Track used/free inodes and data blocks.
- **Directory Entries**: Map file names to inode numbers.

## Notes
- Only files small enough to fit within the direct block pointers (max 12 blocks) can be added.
- The adder performs first-fit allocation for inodes and data blocks.
- The root directory must have space for new entries.

## Error Handling
Both programs print errors to `stderr` and exit with a non-zero code if any operation fails (e.g., out of memory, no free inode, file too large, etc.).

## License
This project is for educational use. See your course or assignment guidelines for permitted usage.
