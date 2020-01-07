#ifndef _AFFS_FS_SB
#define _AFFS_FS_SB

/*
 * super-block data in memory
 *
 * Block numbers are adjusted for their actual size
 *
 */

#include <linux/amigaffs.h>

#define MAX_ZONES		8
#define AFFS_DATA_MIN_FREE	30	/* Percentage of free blocks needed for a data zone */
#define AFFS_HDR_MIN_FREE	10	/* Same for header blocks */

struct affs_bm_info {
	struct buffer_head *bm_bh;	/* Buffer for bitmap. */
	int bm_free;			/* Free blocks. */
	int bm_size;			/* Size in bits, rounded to multiple of 32. */
	int bm_firstblk;		/* Block number of first bit in this map */
};

struct affs_zone {
	unsigned long z_ino;		/* Associated inode number */
	struct affs_bm_info *z_bm;	/* Zone lies in this bitmap */
	int z_start;			/* Index of first word in bitmap */
	int z_zone_no;			/* Zone number */
	unsigned long z_lru_time;	/* Time of last usage */
};

struct affs_sb_info {
	int s_partition_size;		/* Partition size in blocks. */
	int s_root_block;		/* FFS root block number. */
	int s_hashsize;			/* Size of hash table. */
	unsigned long s_flags;		/* See below. */
	short s_uid;			/* uid to override */
	short s_gid;			/* gid to override */
	umode_t s_mode;			/* mode to override */
	int s_reserved;			/* Number of reserved blocks. */
	struct buffer_head *s_root_bh;	/* Cached root block. */
	struct affs_bm_info *s_bitmap;	/* Bitmap infos. */
	int s_bm_count;			/* Number of bitmap blocks. */
	int s_nextzone;			/* Next zone to look for free blocks. */
	int s_num_zones;		/* Total number of zones. */
	struct affs_zone *s_zones;	/* The zones themselfes. */
	char *s_zonemap;		/* Bitmap for zones. */
	char *s_prefix;			/* Prefix for volumes and assignes. */
	int s_prefix_len;		/* Length of prefix. */
	char s_volume[32];		/* Volume prefix for absolute symlinks. */
};

#define SF_INTL		0x0001		/* International filesystem. */
#define SF_BM_VALID	0x0002		/* Bitmap is valid. */
#define SF_IMMUTABLE	0x0004		/* Protection bits cannot be changed */
#define SF_QUIET	0x0008		/* chmod errors will be not reported */
#define SF_SETUID	0x0010		/* Ignore Amiga uid */
#define SF_SETGID	0x0020		/* Ignore Amiga gid */
#define SF_SETMODE	0x0040		/* Ignore Amiga protection bits */
#define SF_USE_MP	0x0080		/* Use uid and gid from mount point */
#define SF_MUFS		0x0100		/* Use MUFS uid/gid mapping */
#define SF_OFS		0x0200		/* Old filesystem */
#define SF_PREFIX	0x0400		/* Buffer for prefix is allocated */
#define SF_VERBOSE	0x0800		/* Talk about fs when mounting */

#endif
