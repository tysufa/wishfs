#include "linux/bitmap.h"
#include "linux/buffer_head.h"
#include "linux/container_of.h"
#include "linux/fs.h"
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

	uint32_t nr_free_blocks = 0;
	if (sbi)
		nr_free_blocks = sbi->nr_free_blocks;

	return sysfs_emit(buf, "Number of free blocks : %d\n", nr_free_blocks);
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

	// Calcul du nombre d'inodes par bloc de votre système de fichiers
	u32 current_inode_index = 0;

	// D'après votre layout, l'inode store commence au bloc 1 (le superblock étant au bloc 0)
	u32 istore_start_block = 1;
	u32 istore_end_block = istore_start_block + sbi->nr_istore_blocks;

	for (block_nr = istore_start_block; block_nr < istore_end_block;
	     block_nr++) {
		// 1. Lecture du bloc depuis le périphérique vers le cache du noyau
		bh = sb_bread(sb, block_nr);
		if (!bh) {
			pr_err("ouichefs: Impossible de lire le bloc d'inodes %u\n",
			       block_nr);
			continue;
		}

		struct ouichefs_inode *dip =
			(struct ouichefs_inode *)bh->b_data;

		// 2. Parcours de tous les inodes contenus dans ce bloc
		for (size_t i = 0; i < OUICHEFS_INODES_PER_BLOCK; i++) {
			// Un inode avec un mode à 0 est considéré comme libre/inutilisé
			if (dip[i].i_mode != 0) {
				if (S_ISREG(dip[i].i_mode)) {
					total_files++;
				} else if (S_ISDIR(dip[i].i_mode)) {
					total_dir++;
				}

				// VOILA VOTRE INDEX !
				// u32 fichier_index = current_inode_index;

				// pr_info("ouichefs: Fichier valide trouvé à l'index %u\n", fichier_index);

				/* * C'est ici que vous pourrez faire vos "autres raisons plus tard"
                 * en manipulant directement dip[i] (les métadonnées du fichier).
                 */
			}
			current_inode_index++; // On avance l'index global
		}

		// 3. Toujours libérer le buffer_head après utilisation
		brelse(bh);
	}

	return sysfs_emit(buf,
			  "Number of files: %lu\n Number of directory : %lu\n",
			  total_files, total_dir);
}

struct custom_attribute free_blocks;
struct custom_attribute files;

struct kobject *ouichefs_kobj;
struct kobject *partition_kobj;
