#include "linux/buffer_head.h"
#include "linux/container_of.h"
#include "linux/fs.h"
#include "linux/pagemap.h"
#include "linux/writeback.h"
#include "ouichefs.h"
#include <linux/sysfs.h>
#include <linux/kobject.h>

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

static ssize_t files_show(struct kobject *kobj, struct kobj_attribute *attr,
			  char *buf)
{
	struct custom_attribute *attributes =
		container_of(attr, struct custom_attribute, attr);

	struct ouichefs_sb_info *sbi = OUICHEFS_SB(attributes->sb);
	struct super_block *sb = attributes->sb;

	struct buffer_head *bh;
	struct buffer_head *bh_index;
	unsigned long total_files = 0;
	unsigned long total_dir = 0;

	unsigned long nr_extent = 0;
	u32 block_nr;

	u32 current_inode_index = 0;

	// l'inode store est présent sur le bloc 1
	u32 istore_start_block = 1;
	u32 istore_end_block = istore_start_block + sbi->nr_istore_blocks;

	__le32 inode_index;
	struct ouichefs_file_index_block *index_block;

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

				inode_index = inode_store_block[i].index_block;

				bh_index = sb_bread(sb, inode_index);
				index_block =
					(struct ouichefs_file_index_block *)
						bh_index->b_data;

				// Parcourt des extents de cet inode
				for (size_t i = 0; i < OUICHEFS_MAX_EXTENTS;
				     i++) {
					if (index_block->extents[i].count ==
					    0) {
						break;
					}
					nr_extent++;
				}

				brelse(bh_index);
			}
			current_inode_index++; // On avance l'index global
		}

		brelse(bh);
	}

	return sysfs_emit(
		buf,
		"Number of files: %lu\n Number of directory : %lu\n Number of extents : %lu",
		total_files, total_dir, nr_extent);
}

struct custom_attribute free_blocks;
struct custom_attribute files;

struct kobject *ouichefs_kobj;
struct kobject *partition_kobj;
