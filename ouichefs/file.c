// SPDX-License-Identifier: GPL-2.0
/*
 * ouiche_fs - a simple educational filesystem for Linux
 *
 * Copyright (C) 2018 Redha Gouicem <redha.gouicem@lip6.fr>
 */

#include "linux/types.h"
#define pr_fmt(fmt) "%s:%s: ouichefs:" fmt, KBUILD_MODNAME, __func__

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/buffer_head.h>
#include <linux/mpage.h>

#include "ouichefs.h"
#include "bitmap.h"

static uint32_t ouichefs_extent_get_block(struct ouichefs_extent *extents,
					  uint32_t logical_block)
{
	uint16_t i = 0;
	while (i < OUICHEFS_MAX_EXTENTS && extents[i].count) {
		uint32_t count = extents[i].count;
		if (count < logical_block) {
			logical_block -= count;
			i++;
			continue;
		}
		return extents[i].start + (count - logical_block);
	}
	pr_err("Did not successfully find logical_block #%u in extents.",
	       logical_block);
	return 0;
}

static long get_extents(struct file *file)
{
	struct buffer_head *physical_index;
	struct super_block *sb = file->f_inode->i_sb;
	struct ouichefs_inode_info *fsiinfo = OUICHEFS_INODE(file->f_inode);
	physical_index =
		sb_bread(sb, fsiinfo->index_block); // on récupère l'index
	if (!physical_index) {
		pr_err("impossible de lire le bloc d'index %u\n",
		       fsiinfo->index_block);
		return -EIO;
	}
	struct ouichefs_file_index_block *index_block =
		(struct ouichefs_file_index_block *)physical_index->b_data;
	uint16_t i = 0;
	uint16_t nb_extents = 0;
	struct ouichefs_extent *extents = index_block->extents;
	while (nb_extents < OUICHEFS_MAX_EXTENTS &&
	       extents[nb_extents].count != 0)
		nb_extents++;
	printk("ouichefs: extents for inode %lu: %u %s", file->f_inode->i_ino,
	       nb_extents, nb_extents > 1 ? "extents" : "extent");
	while (i < OUICHEFS_MAX_EXTENTS && extents[i].start != 0) {
		uint32_t start = extents[i].start;
		uint32_t count = extents[i].count;
		if (count == 1)
			printk("  [%u] start=%u count=%u (extent %u)", i, start,
			       count, start);
		else
			printk("  [%u] start=%u count=%u (extents %u-%u)", i,
			       start, count, start, start + count);
		i++;
	}
	brelse(physical_index);
	return 0;
}

static int ouichefs_open(struct inode *inode, struct file *file)
{
	bool wronly = (file->f_flags & O_WRONLY) != 0;
	bool rdwr = (file->f_flags & O_RDWR) != 0;
	bool trunc = (file->f_flags & O_TRUNC) != 0;

	if ((wronly || rdwr) && trunc && (inode->i_size != 0)) {
		struct super_block *sb = inode->i_sb;
		struct ouichefs_sb_info *sbi = OUICHEFS_SB(sb);
		struct ouichefs_inode_info *ci = OUICHEFS_INODE(inode);
		struct ouichefs_file_index_block *index;
		struct buffer_head *bh_index;
		// sector_t iblock;

		/* Read index block from disk */
		bh_index = sb_bread(sb, ci->index_block);
		if (!bh_index)
			return -EIO;
		index = (struct ouichefs_file_index_block *)bh_index->b_data;

		// for (iblock = 0; index->blocks[iblock] != 0; iblock++) {
		struct ouichefs_extent *extents = index->extents;
		uint32_t block = extents->count ? extents->start : 0;
		uint16_t i = 0;
		while (block && i < OUICHEFS_MAX_EXTENTS) {
			put_block(sbi, le32_to_cpu(block));
			block++; // because when count is 1 only start needs to be read
			if (block - extents[i].start >= extents[i].count) {
				extents[i].count = 0;
				i++;
				if (i >= OUICHEFS_MAX_EXTENTS ||
				    extents[i].count == 0)
					break;
				block = extents[i].start; // next extent
			}
		}
		inode->i_size = 0;
		inode->i_blocks = 1;

		mark_buffer_dirty(bh_index);
		brelse(bh_index);
	}

	return 0;
}

ssize_t ouichefs_read(struct file *file, char __user *buffer, size_t count,
		      loff_t *ppos)
{
	struct inode *inode = file->f_inode;
	struct super_block *sb = inode->i_sb;
	struct ouichefs_inode_info *fsiinfo = OUICHEFS_INODE(inode);
	struct buffer_head *buffer_head_index;
	struct buffer_head *buffer_block;

	uint32_t bytes_read = 0;
	uint32_t ret = 0;

	if (*ppos > inode->i_size)
		return 0;

	if (*ppos + count > inode->i_size)
		count = inode->i_size - *ppos;

	buffer_head_index =
		sb_bread(sb, fsiinfo->index_block); // on récupère l'index

	if (!buffer_head_index) {
		pr_err("Impossible de lire le bloc d'index %u\n",
		       fsiinfo->index_block);
		return -EIO;
	}

	while (count > 0) {
		uint32_t logical_block = *ppos / OUICHEFS_BLOCK_SIZE;
		uint32_t offset_in_block = *ppos % OUICHEFS_BLOCK_SIZE;
		uint32_t bytes_to_copy = min(
			count, (size_t)OUICHEFS_BLOCK_SIZE - offset_in_block);

		// on caste les informations du block d'index dans la struct adapté
		struct ouichefs_file_index_block *index_block =
			(struct ouichefs_file_index_block *)
				buffer_head_index->b_data;

		// on récupère le block qu'on veut lire
		uint32_t physical_block = ouichefs_extent_get_block(
			index_block->extents, logical_block);
		if (!physical_block) {
			pr_err("could not find logical_block #%u, in inode %lu",
			       logical_block, file->f_inode->i_ino);
			ret = -EFAULT;
			goto release_head_index;
		}
		buffer_block = sb_bread(sb, physical_block);

		if (!buffer_block) {
			pr_err("Impossible de lire le bloc logique %u\n",
			       physical_block);
			ret = -EIO;
			goto release_head_index;
		}

		if (copy_to_user(buffer + bytes_read,
				 buffer_block->b_data + offset_in_block,
				 bytes_to_copy)) {
			pr_err("Impossible d'afficher le contenu du block\n");
			brelse(buffer_block);
			ret = -EIO;
			goto release_head_index;
		}

		brelse(buffer_block);

		count -= bytes_to_copy;
		*ppos += bytes_to_copy;
		bytes_read += bytes_to_copy;
	}
release_head_index:
	brelse(buffer_head_index);
	if (bytes_read)
		return bytes_read; // if we read 0 and have an error, return error
	return ret;
}

ssize_t ouichefs_write(struct file *file, const char __user *buffer,
		       size_t count, loff_t *ppos)
{
	struct ouichefs_sb_info *sbi = OUICHEFS_SB(file->f_inode->i_sb);

	struct super_block *sb = file->f_inode->i_sb;
	struct ouichefs_inode_info *fsiinfo = OUICHEFS_INODE(file->f_inode);

	struct buffer_head *physical_index;
	struct buffer_head *physical_block;

	uint32_t nr_allocs = 0;
	uint32_t bytes_written = 0;
	uint32_t bytes_to_write = 0;

	if (file->f_flags & O_APPEND) {
		*ppos = file->f_inode->i_size;
	}

	/* Check if the write can be completed (enough space?) */
	if (*ppos + count > OUICHEFS_MAX_FILESIZE)
		return -ENOSPC;

	nr_allocs =
		max(*ppos + count, (unsigned long long)file->f_inode->i_size) /
		OUICHEFS_BLOCK_SIZE;

	if (nr_allocs > file->f_inode->i_blocks - 1)
		nr_allocs -= file->f_inode->i_blocks - 1;
	else
		nr_allocs = 0;

	if (nr_allocs > sbi->nr_free_blocks)
		return -ENOSPC;

	physical_index =
		sb_bread(sb, fsiinfo->index_block); // on récupère l'index

	if (!physical_index) {
		pr_err("Impossible de lire le bloc d'index %u\n",
		       fsiinfo->index_block);
		return -EIO;
	}

	// on caste les informations du block d'index dans la struct adapté
	// struct ouichefs_file_index_block *index_block =
	// 	(struct ouichefs_file_index_block *)physical_index->b_data;

	while (count > 0) {
		// uint32_t logical_block_nb = *ppos / OUICHEFS_BLOCK_SIZE;
		uint32_t offset_in_block = *ppos % OUICHEFS_BLOCK_SIZE;

		bytes_to_write = min(count, (size_t)OUICHEFS_BLOCK_SIZE -
						    offset_in_block);

		// on récupère le block qu'on veut lire
		uint32_t physical_block_number = 0;
		// index_block->blocks[logical_block_nb];

		if (physical_block_number == 0) {
			physical_block_number = get_free_block(sbi);
			if (!physical_block_number) {
				pr_err("pas de blocks disponibles pour l'écriture\n");
				brelse(physical_index);
				return -ENOSPC;
			}
			// index_block->blocks[logical_block_nb] =
			// 	cpu_to_le32(physical_block_number);
			mark_buffer_dirty(physical_index);
		}

		physical_block = sb_bread(sb, physical_block_number);

		if (!physical_block) {
			// pr_err("Impossible de lire le bloc logique %u\n",
			//        index_block->blocks[logical_block_nb]);
			goto release_physical_index;
		}

		if (copy_from_user(physical_block->b_data + offset_in_block,
				   buffer + bytes_written, bytes_to_write)) {
			pr_err("Erreur de copie depuis l'utilisateur vers le block\n");
			brelse(physical_block);
			goto release_physical_index;
		}

		mark_buffer_dirty(physical_block);
		write_dirty_buffer(physical_block, REQ_OP_WRITE); // async write
		brelse(physical_block);

		*ppos += bytes_to_write;
		bytes_written += bytes_to_write;
		count -= bytes_to_write;
	}

	if (*ppos > file->f_inode->i_size) {
		file->f_inode->i_size = *ppos;
		mark_inode_dirty(file->f_inode);
		// TODO: rajouter le file->f_inode->i_blocks ou quelque chose comme ça
	}

release_physical_index: // because some bytes might already be written
	write_dirty_buffer(physical_index, REQ_OP_WRITE);
	brelse(physical_index);
	return bytes_written;
}

#include <linux/ioctl.h> //ou include/uapi/asm-generic/ioctl.h ??

static long ouichefs_ioctl(struct file *file, unsigned int cmd,
			   unsigned long arg)
{
	if (_IOC_TYPE(cmd) != OUICHEFS_MAGIC)
		return -1; //check that is right
	//autres macros pour la gestion d'erreur, à check
	//_IOC_DIR( cmd )
	//_IOC_NR( cmd )
	//_IOC_SIZE( cmd )
	switch (cmd) {
	case OUICHEFS_IOC_GET_EXTENTS:
		return get_extents(file);
		break;
	default:
		pr_warn("unknown ioctl command.");
		return -EINVAL;
	}

	return 0;
}

const struct file_operations ouichefs_file_ops = {
	.owner = THIS_MODULE,
	.open = ouichefs_open,
	.read = ouichefs_read,
	.write = ouichefs_write,
	.unlocked_ioctl = ouichefs_ioctl,
	.llseek = generic_file_llseek,
	// .read_iter = generic_file_read_iter,
	// .write_iter = generic_file_write_iter,
	.fsync = generic_file_fsync,
};
