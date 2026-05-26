// SPDX-License-Identifier: GPL-2.0
/*
 * ouiche_fs - a simple educational filesystem for Linux
 *
 * Copyright (C) 2018 Redha Gouicem <redha.gouicem@lip6.fr>
 */

#include "linux/printk.h"
#include "linux/types.h"
#define pr_fmt(fmt) "%s:%s: ouichefs:" fmt, KBUILD_MODNAME, __func__

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/buffer_head.h>
#include <linux/mpage.h>

#include "ouichefs.h"
#include "bitmap.h"

static long get_extents(struct file *file)
{
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
	// TODO: ajouter un max d'itération parce qu'on peut remplir au max un index_block
	while (index_block[nb_extents]->count != 0)
		nb_extents++;
	printk("ouichefs: extents for inode %u: %u %s", file->f_inode->i_ino,
	       nb_extents, nb_extents > 1 ? "extents" : "extent");
	while (index_block[i].start != 0) {
		uint32_t start = index_block[i].start;
		uint32_t count = index_block[i].count;
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

/*
 * Map the buffer_head passed in argument with the iblock-th block of the file
 * represented by inode. If the requested block is not allocated and create is
 * true, allocate a new block on disk and map it.
 */
static int ouichefs_file_get_block(struct inode *inode, sector_t iblock,
				   struct buffer_head *bh_result, int create)
{
	struct super_block *sb = inode->i_sb;
	struct ouichefs_sb_info *sbi = OUICHEFS_SB(sb);
	struct ouichefs_inode_info *ci = OUICHEFS_INODE(inode);
	struct ouichefs_file_index_block *index;
	struct buffer_head *bh_index;
	int ret = 0, bno;

	/* If block number exceeds filesize, fail */
	if (iblock >= OUICHEFS_BLOCK_SIZE >> 2)
		return -EFBIG;

	/* Read index block from disk */
	bh_index = sb_bread(sb, ci->index_block);
	if (!bh_index)
		return -EIO;
	index = (struct ouichefs_file_index_block *)bh_index->b_data;

	/*
	 * Check if iblock is already allocated. If not and create is true,
	 * allocate it. Else, get the physical block number.
	 */
	if (index->blocks[iblock] == 0) {
		if (!create) {
			ret = 0;
			goto brelse_index;
		}
		bno = get_free_block(sbi);
		if (!bno) {
			ret = -ENOSPC;
			goto brelse_index;
		}
		index->blocks[iblock] = cpu_to_le32(bno);
		mark_buffer_dirty(bh_index);
	} else {
		bno = le32_to_cpu(index->blocks[iblock]);
	}

	/* Map the physical block to the given buffer_head */
	map_bh(bh_result, sb, bno);

brelse_index:
	brelse(bh_index);

	return ret;
}

/*
 * Called by the page cache to read a page from the physical disk and map it in
 * memory.
 */
static void ouichefs_readahead(struct readahead_control *rac)
{
	mpage_readahead(rac, ouichefs_file_get_block);
}

/*
 * Called by the page cache to write a dirty page to the physical disk (when
 * sync is called or when memory is needed).
 */
static int ouichefs_writepage(struct page *page, struct writeback_control *wbc)
{
	return block_write_full_page(page, ouichefs_file_get_block, wbc);
}

/*
 * Called by the VFS when a write() syscall occurs on file before writing the
 * data in the page cache. This functions checks if the write will be able to
 * complete and allocates the necessary blocks through block_write_begin().
 */
static int ouichefs_write_begin(struct file *file,
				struct address_space *mapping, loff_t pos,
				unsigned int len, struct page **pagep,
				void **fsdata)
{
	struct ouichefs_sb_info *sbi = OUICHEFS_SB(file->f_inode->i_sb);
	int err;
	uint32_t nr_allocs = 0;

	/* Check if the write can be completed (enough space?) */
	if (pos + len > OUICHEFS_MAX_FILESIZE)
		return -ENOSPC;
	nr_allocs = max(pos + len, file->f_inode->i_size) / OUICHEFS_BLOCK_SIZE;
	if (nr_allocs > file->f_inode->i_blocks - 1)
		nr_allocs -= file->f_inode->i_blocks - 1;
	else
		nr_allocs = 0;
	if (nr_allocs > sbi->nr_free_blocks)
		return -ENOSPC;

	/* prepare the write */
	err = block_write_begin(mapping, pos, len, pagep,
				ouichefs_file_get_block);
	/* if this failed, reclaim newly allocated blocks */
	if (err < 0) {
		pr_err("%s:%d: newly allocated blocks reclaim not implemented yet\n",
		       __func__, __LINE__);
	}
	return err;
}

/*
 * Called by the VFS after writing data from a write() syscall to the page
 * cache. This functions updates inode metadata and truncates the file if
 * necessary.
 */
static int ouichefs_write_end(struct file *file, struct address_space *mapping,
			      loff_t pos, unsigned int len, unsigned int copied,
			      struct page *page, void *fsdata)
{
	int ret;
	struct inode *inode = file->f_inode;
	struct ouichefs_inode_info *ci = OUICHEFS_INODE(inode);
	struct super_block *sb = inode->i_sb;

	/* Complete the write() */
	ret = generic_write_end(file, mapping, pos, len, copied, page, fsdata);
	if (ret < len) {
		pr_err("%s:%d: wrote less than asked... what do I do? nothing for now...\n",
		       __func__, __LINE__);
	} else {
		uint32_t nr_blocks_old = inode->i_blocks;

		/* Update inode metadata */
		inode->i_blocks = (roundup(inode->i_size, OUICHEFS_BLOCK_SIZE) /
				   OUICHEFS_BLOCK_SIZE) +
				  1;
		inode->i_mtime = inode->i_ctime = current_time(inode);
		mark_inode_dirty(inode);

		/* If file is smaller than before, free unused blocks */
		if (nr_blocks_old > inode->i_blocks) {
			int i;
			struct buffer_head *bh_index;
			struct ouichefs_file_index_block *index;

			/* Free unused blocks from page cache */
			truncate_pagecache(inode, inode->i_size);

			/* Read index block to remove unused blocks */
			bh_index = sb_bread(sb, ci->index_block);
			if (!bh_index) {
				pr_err("failed truncating '%s'. we just lost %llu blocks\n",
				       file->f_path.dentry->d_name.name,
				       nr_blocks_old - inode->i_blocks);
				goto end;
			}
			index = (struct ouichefs_file_index_block *)
					bh_index->b_data;

			for (i = inode->i_blocks - 1; i < nr_blocks_old - 1;
			     i++) {
				put_block(OUICHEFS_SB(sb),
					  le32_to_cpu(index->blocks[i]));
				index->blocks[i] = 0;
			}
			mark_buffer_dirty(bh_index);
			brelse(bh_index);
		}
	}
end:
	return ret;
}

const struct address_space_operations ouichefs_aops = {
	.readahead = ouichefs_readahead,
	.writepage = ouichefs_writepage,
	.write_begin = ouichefs_write_begin,
	.write_end = ouichefs_write_end
};

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
		sector_t iblock;

		/* Read index block from disk */
		bh_index = sb_bread(sb, ci->index_block);
		if (!bh_index)
			return -EIO;
		index = (struct ouichefs_file_index_block *)bh_index->b_data;

		for (iblock = 0; index->blocks[iblock] != 0; iblock++) {
			put_block(sbi, le32_to_cpu(index->blocks[iblock]));
			index->blocks[iblock] = 0;
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
		buffer_block = sb_bread(sb, index_block->blocks[logical_block]);

		if (!buffer_block) {
			pr_err("Impossible de lire le bloc logique %u\n",
			       index_block->blocks[logical_block]);
			goto release_head_index;
		}

		if (copy_to_user(buffer + bytes_read,
				 buffer_block->b_data + offset_in_block,
				 bytes_to_copy)) {
			pr_err("Impossible d'afficher le contenu du block\n");
			brelse(buffer_block);
			goto release_head_index;
		}

		brelse(buffer_block);

		count -= bytes_to_copy;
		*ppos += bytes_to_copy;
		bytes_read += bytes_to_copy;
	}
release_head_index:
	brelse(buffer_head_index);
	return bytes_read;
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
	struct ouichefs_file_index_block *index_block =
		(struct ouichefs_file_index_block *)physical_index->b_data;

	while (count > 0) {
		uint32_t logical_block_nb = *ppos / OUICHEFS_BLOCK_SIZE;
		uint32_t offset_in_block = *ppos % OUICHEFS_BLOCK_SIZE;

		bytes_to_write = min(count, (size_t)OUICHEFS_BLOCK_SIZE -
						    offset_in_block);

		// on récupère le block qu'on veut lire
		uint32_t physical_block_number =
			index_block->blocks[logical_block_nb];

		if (physical_block_number == 0) {
			physical_block_number = get_free_block(sbi);
			if (!physical_block_number) {
				pr_err("pas de blocks disponibles pour l'écriture\n");
				brelse(physical_index);
				return -ENOSPC;
			}
			index_block->blocks[logical_block_nb] =
				cpu_to_le32(physical_block_number);
			mark_buffer_dirty(physical_index);
		}

		physical_block = sb_bread(sb, physical_block_number);

		if (!physical_block) {
			pr_err("Impossible de lire le bloc logique %u\n",
			       index_block->blocks[logical_block_nb]);
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

//TODO: mettre en tête de fichier
#include <linux/ioctl.h> //ou include/uapi/asm-generic/ioctl.h ??
#define IOC_MAGIC //cf ci-dessous
//TODO: choisir le numéro magique selon cette liste (scroller un peu)
//https://www.kernel.org/doc/html/latest/userspace-api/ioctl/ioctl-number.html
#define OUICHEFS_IOC_GET_EXTENTS _IO(IOC_MAGIC, todo_num)
//TODO: choisir un numéro non-utilisé à la place de todo_num

static int ouichefs_ioctl(struct file *file, unsigned int cmd,
			  unsigned long arg)
{
	if (_IOC_TYPE(cmd) != IOC_MAGIC)
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

const struct file_operations ouichefs_file_ops = { .owner = THIS_MODULE,
						   .open = ouichefs_open,
						   .read = ouichefs_read,
						   .write = ouichefs_write,
						   .unlocked_ioctl =
							   ouichefs_ioctl;
.llseek = generic_file_llseek,
	// .read_iter = generic_file_read_iter,
	// .write_iter = generic_file_write_iter,
	.fsync = generic_file_fsync,
}
;
