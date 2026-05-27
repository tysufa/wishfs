// SPDX-License-Identifier: GPL-2.0
/*
 * ouiche_fs - a simple educational filesystem for Linux
 *
 * Copyright (C) 2018 Redha Gouicem <redha.gouicem@lip6.fr>
 */

#include "linux/bitmap.h"
#include "linux/minmax.h"
#define pr_fmt(fmt) "%s:%s: " fmt, KBUILD_MODNAME, __func__

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/buffer_head.h>
#include <linux/mpage.h>

#include "ouichefs.h"
#include "bitmap.h"

void garbage_collection(struct super_block *sb, uint32_t target)
{
	struct inode *inode;
	uint32_t early_stop = 0;
	struct ouichefs_sb_info *sbi = OUICHEFS_SB(sb);

	sbi->nr_gc_pass++;
	spin_lock(&sb->s_inode_list_lock);
	list_for_each_entry(inode, &sb->s_inodes, i_sb_list) {
		spin_lock(&inode->i_lock);
		struct ouichefs_inode_info *fsiinfo = OUICHEFS_INODE(inode);
		if (fsiinfo->i_reserved_count > 0) {
			early_stop = fsiinfo->i_reserved_count > target &&
				     target > 0;
			release_extent_block(sb, fsiinfo->i_reserved_start,
					     fsiinfo->i_reserved_count, 0);
			// no block cleaning becuase blocks were never used
			fsiinfo->i_reserved_count = 0;
		}
		spin_unlock(&inode->i_lock);
		if (early_stop)
			break;
		// quand on a déjà trouvé suffisament de blocs continus
	}
	spin_unlock(&sb->s_inode_list_lock);
}

void release_extent_block(struct super_block *sb, uint32_t start,
			  uint32_t count, uint8_t clean)
{
	struct ouichefs_sb_info *sbi = OUICHEFS_SB(sb);
	struct buffer_head *bh = NULL;
	char *block;
	uint32_t block_number = start;
	if (!start)
		return;

	while (block_number < start + count) {
		if (clean) {
			bh = sb_bread(sb, le32_to_cpu(block_number));
			if (bh) {
				block = (char *)bh->b_data;
				memset(block, 0, OUICHEFS_BLOCK_SIZE);
				mark_buffer_dirty(bh);
				brelse(bh);
			}
		}
		put_block(sbi, le32_to_cpu(block_number));
		block_number++;
	}
}

static uint32_t ouichefs_extent_get_block(struct ouichefs_extent *extents,
					  uint32_t logical_block)
{
	uint16_t i = 0;
	while (i < OUICHEFS_MAX_EXTENTS && extents[i].count) {
		uint32_t count = extents[i].count;
		if (count <= logical_block) {
			logical_block -= count;
			i++;
			continue;
		}
		if (extents[i].start)
			return extents[i].start + logical_block;
		return OUICHEFS_HOLE_BLOCK;
	}
	return 0;
}

static long get_extents(struct file *file, long force)
{
	struct super_block *sb = file->f_inode->i_sb;
	struct ouichefs_inode_info *fsiinfo = OUICHEFS_INODE(file->f_inode);

	struct buffer_head *physical_index =
		sb_bread(sb, fsiinfo->index_block); // on récupère l'index

	if (!physical_index) {
		pr_err("impossible de lire le bloc d'index %u\n",
		       fsiinfo->index_block);
		return -EIO;
	}

	struct ouichefs_file_index_block *index_block =
		(struct ouichefs_file_index_block *)physical_index->b_data;

	struct ouichefs_extent *extents = index_block->extents;
	uint16_t i = 0;
	uint16_t nb_extents = 0;

	while (nb_extents < OUICHEFS_MAX_EXTENTS &&
	       extents[nb_extents].count != 0)
		nb_extents++;

	printk(KERN_INFO "ouichefs: extents for inode %lu: %u %s\n",
	       file->f_inode->i_ino, nb_extents,
	       nb_extents > 1 ? "extents" : "extent");
	printk(KERN_INFO "  reserved start: %u count: %u\n",
	       fsiinfo->i_reserved_start, fsiinfo->i_reserved_count);

	while (i < OUICHEFS_MAX_EXTENTS && (extents[i].start != 0 || force)) {
		uint32_t start = extents[i].start;
		uint32_t count = extents[i].count;

		if (count <= 1)
			printk(KERN_INFO
			       "  [%u] start=%u count=%u (extent %u)\n",
			       i, start, count, start);
		else
			printk(KERN_INFO
			       "  [%u] start=%u count=%u (extents %u-%u)\n",
			       i, start, count, start, start + count - 1);
		i++;
	}

	brelse(physical_index);
	return 0;
}

static uint32_t write_in_hole(struct ouichefs_extent *extents,
			      uint32_t block_number, uint32_t futur_block)
{
	uint32_t hole_before = 0;
	uint32_t hole_after = 0;
	uint16_t i = 0;
	while (i < OUICHEFS_MAX_EXTENTS && block_number) {
		if (block_number < extents[i].count)
			break;
		block_number -= extents[i].count;
		i++;
	}
	uint8_t next_extent =
		i ? (extents[i - 1].start ?
			     (extents[i - 1].start + extents[i - 1].count) :
			     0) :
		    0;
	if (i && next_extent == futur_block)
		return i; // no change, will automatically be added to previous extent
	hole_before = block_number;
	hole_after = extents[i].count - (block_number + 1);
	// at least one extent to add
	uint8_t shift = (hole_before > 0 ? 1 : 0) + (hole_after > 0 ? 1 : 0);
	if (shift == 0)
		goto shift_done;
	// if (i + shift >= OUICHEFS_MAX_EXTENTS)
	// 	return NULL; // no space
	// 	// will be verified just next
	uint16_t last = i;
	while (last < OUICHEFS_MAX_EXTENTS && extents[last].count)
		last++;
	if (last == 0 || last + shift - 1 >= OUICHEFS_MAX_EXTENTS) {
		pr_err("Could not fill hole because no more extents are available.\n");
		return -ENOSPC; // no space
	}
	last--;
	while (shift && last > i) {
		extents[last + shift].start = extents[last].start;
		extents[last + shift].count = extents[last].count;
		last--;
	}
	if (!hole_before && hole_after) {
		extents[last + 1].start = extents[last].start;
		extents[last + 1].count = extents[last].count;
	}
	if (hole_before) {
		extents[i].count = hole_before;
		extents[i].start = 0;
	}
	if (hole_after) {
		extents[i + shift].count = hole_after;
		extents[i + shift].start = 0;
	}
	extents[hole_before ? i + 1 : i].count = 0;
	extents[hole_before ? i + 1 : i].start = 0;
shift_done:
	return 1;
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

		release_extent_block(sb, ci->i_reserved_start,
				     ci->i_reserved_count, /*clean=*/1);
		OUICHEFS_INODE(inode)->i_reserved_count = 0;

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

uint32_t ouichefs_alloc_contiguous(struct super_block *sb, uint32_t requested,
				   uint32_t *block, uint32_t next)
{
	struct ouichefs_sb_info *sbi = sb->s_fs_info;

	unsigned long *bfree_bitmap = sbi->bfree_bitmap;

	unsigned long first_free_bloc_contiguous =
		find_next_bit(bfree_bitmap, sbi->nr_blocks, 0);
	unsigned long current_free_block = first_free_bloc_contiguous;
	unsigned long last_free_bloc = current_free_block;
	uint32_t block_allocated = 0;
	uint32_t max_blocks_found = 0;

	while (current_free_block != sbi->nr_blocks &&
	       block_allocated < requested) {
		current_free_block = find_next_bit(bfree_bitmap, sbi->nr_blocks,
						   current_free_block + 1);

		if (current_free_block != last_free_bloc + 1) {
			// pr_info("current_free_block : %lu, last_free_bloc : %lu\n",
			// current_free_block, last_free_bloc);

			if (next && current_free_block == next) {
				first_free_bloc_contiguous = current_free_block;
				max_blocks_found = block_allocated;
				break; // sortie pour continuer le précédent
			}
			if (max_blocks_found < block_allocated) {
				first_free_bloc_contiguous = current_free_block;
				max_blocks_found = block_allocated;
			}
			block_allocated = 0;
		} else {
			block_allocated++;
		}

		last_free_bloc = current_free_block;
	}

	// pr_info("blocks allocated : %lu - %lu\n", first_free_bloc_contiguous,
	// current_free_block);

	// TODO: rajouter un lock pour pas se faire manger des blocks pendant qu'on clear
	bitmap_clear(bfree_bitmap, first_free_bloc_contiguous, block_allocated);
	sbi->nr_free_blocks -= block_allocated;

	*block = first_free_bloc_contiguous;

	return block_allocated;
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
		} else if (physical_block == OUICHEFS_HOLE_BLOCK) {
			if (clear_user(buffer + bytes_read, bytes_to_copy)) {
				pr_err("Impossible de mettre le contenu du hole.\n");
				brelse(buffer_block);
				ret = -EIO;
			}
		} else {
			// pr_info("Reading physical block %u", physical_block);
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
		}

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

#define NR_ALLOCS (nr_allocs & ~(1 << 31))
#define SET_ALLOC nr_allocs |= 1 << 31
#define ISSET_ALLOC (nr_allocs & (1 << 31))
#define VALUE_NR(value) (nr_allocs = ISSET_ALLOC | (value & ~(1 << 31)))
#define DEC_NR(value)              \
	(nr_allocs = ISSET_ALLOC | \
		     (NR_ALLOCS - value < 0 ? 0 : NR_ALLOCS - value))

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
	uint16_t i = 0;

	if (file->f_flags & O_APPEND) {
		*ppos = file->f_inode->i_size;
	}

	nr_allocs =
		max(*ppos + count, (unsigned long long)file->f_inode->i_size) /
		OUICHEFS_BLOCK_SIZE;
	if (nr_allocs > file->f_inode->i_blocks - 1)
		nr_allocs -= file->f_inode->i_blocks - 1;
	else
		nr_allocs = 0;
	// si on sait d'avance qu'on ne pourra jamais allouer
	// if (nr_allocs > sbi->nr_free_blocks)
	// 	return -ENOSPC;
	// 	// plus correct car on a le GC maintenant

	// on récupère l'index
	physical_index =
		sb_bread(sb, fsiinfo->index_block); // on récupère l'index
	if (!physical_index) {
		pr_err("Impossible de lire le bloc d'index %u\n",
		       fsiinfo->index_block);
		return -EIO;
	}
	// on caste les informations du block d'index dans la struct adapté
	struct ouichefs_file_index_block *index_block =
		(struct ouichefs_file_index_block *)physical_index->b_data;
	struct ouichefs_extent *extents = index_block->extents;
	// Gestion des trous
	if (*ppos <= file->f_inode->i_size)
		goto write_data;
	{ // changing scope
		uint32_t void_size = (*ppos - 1) - file->f_inode->i_size;
		void_size -= file->f_inode->i_size % OUICHEFS_BLOCK_SIZE;
		// removing 0s already in last block but not written.
		uint32_t void_blocks = void_size / OUICHEFS_BLOCK_SIZE;
		i = 0;
		while (i < OUICHEFS_MAX_EXTENTS && extents[i].count)
			i++; // i se trouve sur la prochaine extent de libre
		if (i >= OUICHEFS_MAX_EXTENTS)
			goto release_physical_index;
		if (i != 0 && extents[i - 1].start == 0)
			i--;
		extents[i].start = 0;
		extents[i].count = void_blocks;
		DEC_NR(void_blocks);
	}

write_data:
	while (count > 0) {
		i = 0;
		uint32_t next_extent = 0;
		uint32_t logical_block_nb = *ppos / OUICHEFS_BLOCK_SIZE;
		uint32_t offset_in_block = *ppos % OUICHEFS_BLOCK_SIZE;

		bytes_to_write = min(count, (size_t)OUICHEFS_BLOCK_SIZE -
						    offset_in_block);

		// on récupère le block qu'on veut lire
		uint32_t physical_block_number = ouichefs_extent_get_block(
			index_block->extents, logical_block_nb);
		// pr_info("Got block %u\n", physical_block_number);
		uint32_t new_count = 0;
		// si on doit allouer de nouveaux blocs
		uint32_t fill = 0;
		if (physical_block_number == OUICHEFS_HOLE_BLOCK) {
			fill = 1;
			// fill = write_in_hole(extents, logical_block_nb);
			// if (fill & (1 << 31)) {
			// 	pr_err("Could not split hole open. No space.");
			// 	bytes_written = fill; // error number
			// 	goto release_physical_index;
			// }
			physical_block_number = 0;
		} else if (physical_block_number)
			goto block_number_found;

		// making new reservation if possible
		while (i < OUICHEFS_MAX_EXTENTS && extents[i].count)
			i++; // i se trouve sur la prochaine extent de libre
		next_extent =
			i ? (extents[i - 1].start + extents[i - 1].count) : 0;
		spin_lock(&fsiinfo->vfs_inode.i_lock);
		if (fsiinfo->i_reserved_count == 0) {
			if (sbi->nr_bfree_blocks < NR_ALLOCS) {
				spin_unlock(&fsiinfo->vfs_inode.i_lock);
				garbage_collection(
					sb, NR_ALLOCS + reservation_size);
				spin_lock(&fsiinfo->vfs_inode.i_lock);
			}
			fsiinfo->i_reserved_count = ouichefs_alloc_contiguous(
				sb, NR_ALLOCS + reservation_size,
				&(fsiinfo->i_reserved_start),
				/*next=*/next_extent);
		}
		if (fsiinfo->i_reserved_count > 0) {
			// on a des blocs de réservés
			physical_block_number = fsiinfo->i_reserved_start;
			fsiinfo->i_reserved_start++;
			fsiinfo->i_reserved_count--;
			new_count = 1;
		} else if (physical_block_number == 0) {
			// fallback if for any reason theres a problem
			pr_warn("Using legacy block allocation. This should not happen.");
			physical_block_number = get_free_block(sbi);
			new_count = 1;
		}
		spin_unlock(&fsiinfo->vfs_inode.i_lock);
		DEC_NR(new_count);
		if (!physical_block_number || !new_count) {
			pr_err("pas de blocks disponibles pour l'écriture\n");
			if (physical_block_number)
				put_block(sbi,
					  le32_to_cpu(physical_block_number));
			bytes_written = -ENOSPC;
			goto release_physical_index;
		}
		if (fill) { // si on doit fill un hole
			fill = write_in_hole(extents, logical_block_nb,
					     physical_block_number);
			if (fill & (1 << 31)) {
				pr_err("Could not split hole open. No space.");
				put_block(sbi,
					  le32_to_cpu(physical_block_number));
				bytes_written = fill; // error number
				goto release_physical_index;
			}
		}

		file->f_inode->i_blocks += new_count;
		SET_ALLOC;
		i = 0;
		while (i < OUICHEFS_MAX_EXTENTS && extents[i].count)
			i++; // i se trouve sur la prochaine extent de libre
		next_extent = i ? (extents[i - 1].start ?
					   (extents[i - 1].start +
					    extents[i - 1].count) :
					   0) :
				  0;
		if (i != 0 && physical_block_number == next_extent) {
			extents[i - 1].count +=
				new_count; // si le bloc est continu
			// pr_info("Nouveau bloc continu\n");
		} else if (i >= OUICHEFS_MAX_EXTENTS) {
			release_extent_block(sb, physical_block_number,
					     new_count, /*clean=*/0);
			// no cleaning because not written yet
			pr_err("No more space in index.\n");
			bytes_written = -ENOSPC;
			goto release_physical_index;
		} else { // si on doit ajouter un nouvel extent
			// pr_info("Creating new extent in index\n");
			extents[i].count = new_count;
			extents[i].start = physical_block_number;
		}
		mark_buffer_dirty(physical_index);

block_number_found:
		// pr_info("reading block %u\n", physical_block_number);
		physical_block = sb_bread(sb, physical_block_number);

		if (!physical_block) {
			pr_err("Impossible de lire le bloc logique %u\n",
			       physical_block_number);
			goto release_physical_index;
		}

		if (copy_from_user(physical_block->b_data + offset_in_block,
				   buffer + bytes_written, bytes_to_write)) {
			pr_err("Erreur de copie depuis l'utilisateur vers le block\n");
			brelse(physical_block);
			goto release_physical_index;
		}

		mark_buffer_dirty(physical_block);
		write_dirty_buffer(physical_block,
				   REQ_OP_WRITE); // async write
		brelse(physical_block);
		*ppos += bytes_to_write;
		bytes_written += bytes_to_write;
		count -= bytes_to_write;
	}

release_physical_index: // because some bytes might already be written
	if (*ppos > file->f_inode->i_size) {
		file->f_inode->i_size = *ppos;
		SET_ALLOC;
	}
	if (ISSET_ALLOC)
		mark_inode_dirty(file->f_inode);
	write_dirty_buffer(physical_index, REQ_OP_WRITE); // async
	// sync_dirty_buffer(physical_index); // sync
	brelse(physical_index);
	return bytes_written;
}

static long ouichefs_ioctl(struct file *file, unsigned int cmd,
			   unsigned long arg)
{
	//if (_IOC_TYPE(cmd) != OUICHEFS_MAGIC)
	//	return -EINVAL;

	switch (cmd) {
	case OUICHEFS_IOC_GET_EXTENTS:
		return get_extents(file, arg);
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
