// SPDX-License-Identifier: GPL-2.0
/*
 * ouiche_fs - a simple educational filesystem for Linux
 *
 * Copyright (C) 2018  Redha Gouicem <redha.gouicem@lip6.fr>
 */

#include "linux/buffer_head.h"
#include "linux/kobject.h"
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/fs.h>

#include "ouichefs.h"
#include "sysfs.h"

uint32_t reservation_size = 8;
module_param(reservation_size, uint, 0644);

/*
 * Mount a ouiche_fs partition
 */
struct dentry *ouichefs_mount(struct file_system_type *fs_type, int flags,
			      const char *dev_name, void *data)
{
	struct dentry *dentry = NULL;
	char kobj_partition_name[64];
	int ret;

	dentry =
		mount_bdev(fs_type, flags, dev_name, data, ouichefs_fill_super);
	if (IS_ERR(dentry)) {
		pr_err("'%s' mount failure\n", dev_name);
		return dentry;
	}

	const char *base_name = strrchr(dev_name, '/');
	base_name = base_name ? base_name + 1 : dev_name;

	snprintf(kobj_partition_name, sizeof(kobj_partition_name),
		 "partition_%s", base_name);

	partition_kobj =
		kobject_create_and_add(kobj_partition_name, ouichefs_kobj);
	if (!partition_kobj)
		goto failed_sysfs_kobject;

	pr_info("kobject '%s' created\n", kobj_partition_name);

	// --- FREE_BLOCKS ---
	struct kobj_attribute free_blocks_attr =
		__ATTR(free_blocks, 0400, free_blocks_show, NULL);

	free_blocks.sb = dentry->d_sb;
	free_blocks.attr = free_blocks_attr;

	ret = sysfs_create_file(partition_kobj, &free_blocks.attr.attr);
	if (ret) {
		pr_err("failed to create sysfs 'free_blocks'\n");
		goto failed_sysfs_file;
	}
	pr_info("sysfs 'free_blocks' created in '%s'\n", kobj_partition_name);

	// --- NUMBER OF FILES ---
	struct kobj_attribute nr_files_attr =
		__ATTR(files, 0400, files_show, NULL);

	files.sb = dentry->d_sb;
	files.attr = nr_files_attr;
	ret = sysfs_create_file(partition_kobj, &files.attr.attr);
	if (ret) {
		pr_err("failed to create sysfs 'files'\n");
		goto failed_sysfs_file;
	}
	pr_info("sysfs 'files' created in '%s'\n", kobj_partition_name);

	return dentry;

failed_sysfs_kobject:
	pr_err("failed to create sysfs kobject for '%s'\n", dev_name);

failed_sysfs_file:
	if (IS_ERR(dentry))
		pr_err("'%s' mount failure\n", dev_name);
	else
		pr_info("'%s' mount success\n", dev_name);

	return dentry;
}

/*
 * Unmount a ouiche_fs partition
 */
void ouichefs_kill_sb(struct super_block *sb)
{
	garbage_collection(sb, 0);
	// target 0 forces free of all reserved blocks
	kobject_put(partition_kobj);
	pr_info("put sysfs parition1\n");

	kill_block_super(sb);

	pr_info("unmounted disk\n");
}

static struct file_system_type ouichefs_file_system_type = {
	.owner = THIS_MODULE,
	.name = "ouichefs",
	.mount = ouichefs_mount,
	.kill_sb = ouichefs_kill_sb,
	.fs_flags = FS_REQUIRES_DEV,
	.next = NULL,
};

static int __init ouichefs_init(void)
{
	int ret;

	ret = ouichefs_init_inode_cache();
	if (ret) {
		pr_err("inode cache creation failed\n");
		goto err;
	}

	ret = register_filesystem(&ouichefs_file_system_type);
	if (ret) {
		pr_err("register_filesystem() failed\n");
		goto err_inode;
	}

	ouichefs_kobj = kobject_create_and_add("ouichefs", fs_kobj);
	if (!ouichefs_kobj) {
		ret = -ENOMEM;
		goto err_sysfs;
	}

	pr_info("module loaded\n");
	return 0;

err_sysfs:
	return -ENOMEM;

err_inode:
	ouichefs_destroy_inode_cache();
err:
	return ret;
}

static void __exit ouichefs_exit(void)
{
	int ret;

	ret = unregister_filesystem(&ouichefs_file_system_type);
	if (ret)
		pr_err("unregister_filesystem() failed\n");

	kobject_put(ouichefs_kobj);
	ouichefs_destroy_inode_cache();

	pr_info("module unloaded\n");
}

module_init(ouichefs_init);
module_exit(ouichefs_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Redha Gouicem, <redha.gouicem@rwth-aachen.de>");
MODULE_DESCRIPTION("ouichefs, a simple educational filesystem for Linux");
