#include "linux/bitmap.h"
#include "linux/buffer_head.h"
#include "linux/container_of.h"
#include "linux/fs.h"
#include "linux/pagemap.h"
#include "linux/writeback.h"
#include "ouichefs.h"
#include <linux/sysfs.h>
#include <linux/kobject.h>

#define ouichefs_for_each_inode(sb, sbi, block_nr, bh, oinode)                       \
	for (block_nr = 1, bh = NULL; block_nr < 1 + (sbi)->nr_istore_blocks;        \
	     brelse(bh), block_nr++)                                                 \
		if (!((bh) = sb_bread((sb), block_nr))) {                            \
			pr_err("ouichefs: Impossible de lire le bloc d'inodes %u\n", \
			       block_nr);                                            \
		} else                                                               \
			for (int i = 0, oinode = (struct ouichefs_inode *)(bh)       \
							 ->b_data;                   \
			     i < OUICHEFS_INODES_PER_BLOCK; i++, oinode++)

struct custom_attribute {
	struct kobj_attribute attr;
	struct super_block *sb;
};

static ssize_t free_blocks_show(struct kobject *kobj,
				struct kobj_attribute *attr, char *buf)
{
	struct custom_attribute *attributes =
		container_of(attr, struct custom_attribute, attr);

	struct ouichefs_sb_info *sbi = OUICHEFS_SB(attributes->sb);

	unsigned long *bfree_bitmap = sbi->bfree_bitmap;

	unsigned long free_block =
		find_next_bit(bfree_bitmap, sbi->nr_blocks, 0);

	uint32_t nr_free_blocks_sbi = 0;
	uint32_t nr_free_blocks = 0;

	while (free_block != sbi->nr_blocks) {
		nr_free_blocks++;
		free_block = find_next_bit(bfree_bitmap, sbi->nr_blocks,
					   free_block + 1);
	}

	if (sbi)
		nr_free_blocks_sbi = sbi->nr_free_blocks;

	return sysfs_emit(
		buf,
		"Number of free blocks sbi : %d, Number of free blocks bitmap : %d\n",
		nr_free_blocks_sbi, nr_free_blocks);
}

bool in_interval(uint32_t block, uint32_t extent_start, uint32_t extent_end)
{
	if (extent_start <= block && extent_start + extent_end >= block)
		return true;
	else
		return false;
}

uint32_t nr_reserved_block_in_extent(unsigned long *bitmap,
				     uint32_t extent_start, uint32_t extent_end,
				     struct ouichefs_sb_info *sbi)
{
	unsigned long reserved_block =
		find_next_zero_bit(bitmap, sbi->nr_blocks, 0);

	uint32_t nr_reserved_block = 0;

	while (reserved_block != sbi->nr_blocks) {
		if (in_interval(reserved_block, extent_start, extent_end)) {
			bitmap_set(bitmap, reserved_block, 1);
			nr_reserved_block++;
		}
		reserved_block = find_next_bit(bitmap, sbi->nr_blocks,
					       reserved_block + 1);
	}

	return nr_reserved_block;
}

static ssize_t commited_blocks_show(struct kobject *kobj,
				    struct kobj_attribute *attr, char *buf)
{
	struct custom_attribute *attributes =
		container_of(attr, struct custom_attribute, attr);

	struct ouichefs_sb_info *sbi = OUICHEFS_SB(attributes->sb);
	struct super_block *sb = attributes->sb;

	struct buffer_head *bh;
	struct buffer_head *bh_index;

	u32 block_nr;
	uint32_t nr_commited_blocks = 0;

	// On veut pouvoir modifier la bitmap
	uint32_t nr_bits = sbi->nr_blocks;

	unsigned long *local_bitmap = kmalloc_array(
		BITS_TO_LONGS(nr_bits), sizeof(unsigned long), GFP_KERNEL);
	if (!local_bitmap) {
		return -ENOMEM;
	}
	bitmap_copy(local_bitmap, sbi->bfree_bitmap, nr_bits);

	// l'inode store est présent sur le bloc 1
	u32 istore_start_block = 1;
	u32 istore_end_block = istore_start_block + sbi->nr_istore_blocks;

	__le32 inode_index;
	struct ouichefs_file_index_block *index_block;

	// -- FLUSH DES INODES DIRTY EN MEMOIRE ---
	struct inode *inode;
	struct inode *tmp;

	list_for_each_entry_safe(inode, tmp, &sb->s_inodes, i_sb_list) {
		write_inode_now(inode, WB_SYNC_ALL);
	}

	// --- on parcourt les inodes store blocks ---
	for (block_nr = istore_start_block; block_nr < istore_end_block;
	     block_nr++) {
		bh = sb_bread(sb, block_nr);
		if (!bh) {
			pr_err("ouichefs: Impossible de lire le bloc d'inodes %u\n",
			       block_nr);
			continue;
		}

		struct ouichefs_inode *inode_store_block =
			(struct ouichefs_inode *)bh->b_data;

		// -- on parcourt tous les inodes du block --
		for (size_t i = 0; i < OUICHEFS_INODES_PER_BLOCK; i++) {
			if (inode_store_block[i].i_mode != 0) {
				if (!S_ISREG(inode_store_block[i].i_mode))
					break;

				inode_index = inode_store_block[i].index_block;

				bh_index = sb_bread(sb, inode_index);
				index_block =
					(struct ouichefs_file_index_block *)
						bh_index->b_data;

				// - Parcourt des extents de cet inode -
				for (size_t i = 0; i < OUICHEFS_MAX_EXTENTS;
				     i++) {
					if (index_block->extents[i].count == 0)
						break;

					nr_commited_blocks +=
						nr_reserved_block_in_extent(
							local_bitmap,
							index_block->extents[i]
								.start,
							index_block->extents[i]
								.count,
							sbi);
				}

				brelse(bh_index);
			}
		}

		brelse(bh);
	}

	kfree(local_bitmap);

	return sysfs_emit(buf, "Number of commited blocks : %d\n",
			  nr_commited_blocks);
}

static ssize_t files_show(struct kobject *kobj, struct kobj_attribute *attr,
			  char *buf)
{
	struct custom_attribute *attributes =
		container_of(attr, struct custom_attribute, attr);

	struct ouichefs_sb_info *sbi = OUICHEFS_SB(attributes->sb);
	struct super_block *sb = attributes->sb;
	struct buffer_head *bh;

	unsigned long total_files = 0;
	unsigned long total_dir = 0;

	u32 block_nr;

	// l'inode store est présent sur le bloc 1
	u32 istore_start_block = 1;
	u32 istore_end_block = istore_start_block + sbi->nr_istore_blocks;

	// -- FLUSH DES INODES DIRTY EN MEMOIRE
	struct inode *inode;
	struct inode *tmp;

	list_for_each_entry_safe(inode, tmp, &sb->s_inodes, i_sb_list) {
		write_inode_now(inode, WB_SYNC_ALL);
	}

	// on parcourt tous les block istore
	for (block_nr = istore_start_block; block_nr < istore_end_block;
	     block_nr++) {
		bh = sb_bread(sb, block_nr);
		if (!bh) {
			pr_err("ouichefs: Impossible de lire le bloc d'inodes %u\n",
			       block_nr);
			continue;
		}

		struct ouichefs_inode *inode_store_block =
			(struct ouichefs_inode *)bh->b_data;

		// on parcourt tous les inodes du block
		for (size_t i = 0; i < OUICHEFS_INODES_PER_BLOCK; i++) {
			if (inode_store_block[i].i_mode != 0) {
				if (S_ISREG(inode_store_block[i].i_mode)) {
					total_files++;
				} else if (S_ISDIR(inode_store_block[i].i_mode)) {
					total_dir++;
				}
			}
		}
		brelse(bh);
	}

	return sysfs_emit(buf,
			  "Number of files: %lu\n Number of directory : %lu\n",
			  total_files, total_dir);
}

static ssize_t max_file_size_show(struct kobject *kobj,
				  struct kobj_attribute *attr, char *buf)
{
	struct custom_attribute *attributes =
		container_of(attr, struct custom_attribute, attr);

	struct ouichefs_sb_info *sbi = OUICHEFS_SB(attributes->sb);
	struct super_block *sb = attributes->sb;
	struct buffer_head *bh;

	unsigned long max_file_size = 0;

	u32 block_nr;

	// l'inode store est présent sur le bloc 1
	u32 istore_start_block = 1;
	u32 istore_end_block = istore_start_block + sbi->nr_istore_blocks;

	// -- FLUSH DES INODES DIRTY EN MEMOIRE
	struct inode *inode;
	struct inode *tmp;

	list_for_each_entry_safe(inode, tmp, &sb->s_inodes, i_sb_list) {
		write_inode_now(inode, WB_SYNC_ALL);
	}

	// on parcourt tous les block istore
	for (block_nr = istore_start_block; block_nr < istore_end_block;
	     block_nr++) {
		bh = sb_bread(sb, block_nr);
		if (!bh) {
			pr_err("ouichefs: Impossible de lire le bloc d'inodes %u\n",
			       block_nr);
			continue;
		}

		struct ouichefs_inode *inode_store_block =
			(struct ouichefs_inode *)bh->b_data;

		// on parcourt tous les inodes du block
		for (size_t i = 0; i < OUICHEFS_INODES_PER_BLOCK; i++) {
			if (inode_store_block[i].i_mode != 0) {
				if (S_ISREG(inode_store_block[i].i_mode)) {
					if (inode_store_block[i].i_size >
					    max_file_size)
						max_file_size =
							inode_store_block[i]
								.i_size;
				}
			}
		}
		brelse(bh);
	}

	return sysfs_emit(buf, "Max file size : %lu bytes\n", max_file_size);
}

static ssize_t total_extents_show(struct kobject *kobj,
				  struct kobj_attribute *attr, char *buf)
{
	struct custom_attribute *attributes =
		container_of(attr, struct custom_attribute, attr);

	struct ouichefs_sb_info *sbi = OUICHEFS_SB(attributes->sb);
	struct super_block *sb = attributes->sb;

	struct buffer_head *bh;
	struct buffer_head *bh_index;

	unsigned long nr_extent = 0;

	u32 block_nr;

	// l'inode store est présent sur le bloc 1
	u32 istore_start_block = 1;
	u32 istore_end_block = istore_start_block + sbi->nr_istore_blocks;

	__le32 inode_index;
	struct ouichefs_file_index_block *index_block;

	// -- FLUSH DES INODES DIRTY EN MEMOIRE ---
	struct inode *inode;
	struct inode *tmp;

	list_for_each_entry_safe(inode, tmp, &sb->s_inodes, i_sb_list) {
		write_inode_now(inode, WB_SYNC_ALL);
	}

	// --- on parcourt les inodes store blocks ---
	for (block_nr = istore_start_block; block_nr < istore_end_block;
	     block_nr++) {
		bh = sb_bread(sb, block_nr);
		if (!bh) {
			pr_err("ouichefs: Impossible de lire le bloc d'inodes %u\n",
			       block_nr);
			continue;
		}

		struct ouichefs_inode *inode_store_block =
			(struct ouichefs_inode *)bh->b_data;

		// -- on parcourt tous les inodes du block --
		for (size_t i = 0; i < OUICHEFS_INODES_PER_BLOCK; i++) {
			if (inode_store_block[i].i_mode != 0) {
				if (S_ISREG(inode_store_block[i].i_mode)) {
					inode_index =
						inode_store_block[i].index_block;

					bh_index = sb_bread(sb, inode_index);
					index_block =
						(struct ouichefs_file_index_block
							 *)bh_index->b_data;

					// - Parcourt des extents de cet inode -
					for (size_t i = 0;
					     i < OUICHEFS_MAX_EXTENTS; i++) {
						if (index_block->extents[i]
							    .count == 0) {
							break;
						}
						nr_extent++;
					}

					brelse(bh_index);
				}
			}
		}

		brelse(bh);
	}

	return sysfs_emit(buf, "Total number of extents : %lu\n", nr_extent);
}

static ssize_t avg_extent_size_show(struct kobject *kobj,
				    struct kobj_attribute *attr, char *buf)
{
	struct custom_attribute *attributes =
		container_of(attr, struct custom_attribute, attr);

	struct ouichefs_sb_info *sbi = OUICHEFS_SB(attributes->sb);
	struct super_block *sb = attributes->sb;

	struct buffer_head *bh;
	struct buffer_head *bh_index;

	unsigned long nr_extent = 0;
	unsigned long total_extent_size = 0;
	unsigned long avg_extent_size;

	u32 block_nr;

	// l'inode store est présent sur le bloc 1
	u32 istore_start_block = 1;
	u32 istore_end_block = istore_start_block + sbi->nr_istore_blocks;

	__le32 inode_index;
	struct ouichefs_file_index_block *index_block;

	// -- FLUSH DES INODES DIRTY EN MEMOIRE ---
	struct inode *inode;
	struct inode *tmp;

	list_for_each_entry_safe(inode, tmp, &sb->s_inodes, i_sb_list) {
		write_inode_now(inode, WB_SYNC_ALL);
	}

	// --- on parcourt les inodes store blocks ---
	for (block_nr = istore_start_block; block_nr < istore_end_block;
	     block_nr++) {
		bh = sb_bread(sb, block_nr);
		if (!bh) {
			pr_err("ouichefs: Impossible de lire le bloc d'inodes %u\n",
			       block_nr);
			continue;
		}

		struct ouichefs_inode *inode_store_block =
			(struct ouichefs_inode *)bh->b_data;

		// -- on parcourt tous les inodes du block --
		for (size_t i = 0; i < OUICHEFS_INODES_PER_BLOCK; i++) {
			if (inode_store_block[i].i_mode != 0) {
				if (S_ISREG(inode_store_block[i].i_mode)) {
					inode_index =
						inode_store_block[i].index_block;

					bh_index = sb_bread(sb, inode_index);
					index_block =
						(struct ouichefs_file_index_block
							 *)bh_index->b_data;

					// - Parcourt des extents de cet inode -
					for (size_t i = 0;
					     i < OUICHEFS_MAX_EXTENTS; i++) {
						if (index_block->extents[i]
							    .count == 0) {
							break;
						}
						nr_extent++;

						total_extent_size +=
							index_block->extents[i]
								.count;
					}

					brelse(bh_index);
				}
			}
		}
		brelse(bh);
	}

	avg_extent_size = (total_extent_size * 100) / (nr_extent * 100);

	return sysfs_emit(buf, "Average size of extents : %lu\n",
			  avg_extent_size);
}

static ssize_t fragmentation_show(struct kobject *kobj,
				  struct kobj_attribute *attr, char *buf)
{
	struct custom_attribute *attributes =
		container_of(attr, struct custom_attribute, attr);

	struct ouichefs_sb_info *sbi = OUICHEFS_SB(attributes->sb);
	struct super_block *sb = attributes->sb;

	struct buffer_head *bh;
	struct buffer_head *bh_index;

	unsigned long nr_files = 0;
	unsigned long nr_extent = 0;
	unsigned long avg_extent_per_file;

	u32 block_nr;

	// l'inode store est présent sur le bloc 1
	u32 istore_start_block = 1;
	u32 istore_end_block = istore_start_block + sbi->nr_istore_blocks;

	__le32 inode_index;
	struct ouichefs_file_index_block *index_block;

	// -- FLUSH DES INODES DIRTY EN MEMOIRE ---
	struct inode *inode;
	struct inode *tmp;

	list_for_each_entry_safe(inode, tmp, &sb->s_inodes, i_sb_list) {
		write_inode_now(inode, WB_SYNC_ALL);
	}

	// --- on parcourt les inodes store blocks ---
	for (block_nr = istore_start_block; block_nr < istore_end_block;
	     block_nr++) {
		bh = sb_bread(sb, block_nr);
		if (!bh) {
			pr_err("ouichefs: Impossible de lire le bloc d'inodes %u\n",
			       block_nr);
			continue;
		}

		struct ouichefs_inode *inode_store_block =
			(struct ouichefs_inode *)bh->b_data;

		// -- on parcourt tous les inodes du block --
		for (size_t i = 0; i < OUICHEFS_INODES_PER_BLOCK; i++) {
			if (inode_store_block[i].i_mode != 0) {
				if (S_ISREG(inode_store_block[i].i_mode)) {
					nr_files++;

					inode_index =
						inode_store_block[i].index_block;

					bh_index = sb_bread(sb, inode_index);
					index_block =
						(struct ouichefs_file_index_block
							 *)bh_index->b_data;

					// - Parcourt des extents de cet inode -
					for (size_t i = 0;
					     i < OUICHEFS_MAX_EXTENTS; i++) {
						if (index_block->extents[i]
							    .count == 0) {
							break;
						}
						nr_extent++;
					}

					brelse(bh_index);
				}
			}
		}
		brelse(bh);
	}

	avg_extent_per_file = (nr_extent * 100) / (nr_files * 100);

	return sysfs_emit(buf, "Fragmentation : %lu extents per file\n",
			  avg_extent_per_file);
}

struct custom_attribute free_blocks;
struct custom_attribute files;
struct custom_attribute total_extents;
struct custom_attribute avg_extent_size;
struct custom_attribute max_file_size;
struct custom_attribute fragmentation;
struct custom_attribute commited_blocks;

struct kobject *ouichefs_kobj;
struct kobject *partition_kobj;
