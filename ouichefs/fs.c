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

int create_ouichefs_sysfs(struct custom_attribute *filesystem,
			  struct super_block *sb,
			  ssize_t (*show)(struct kobject *kobj,
					  struct kobj_attribute *attr,
					  char *buf),
			  const char *name)
{
	int ret;

	filesystem->sb = sb;
	filesystem->attr.attr.name = name;
	filesystem->attr.attr.mode = 0400;
	filesystem->attr.show = show;
	filesystem->attr.store = NULL;

	ret = sysfs_create_file(partition_kobj, &filesystem->attr.attr);
	if (ret)
		pr_err("failed to create sysfs '%s'\n", name);
	else
		pr_info("sysfs '%s' created \n", name);

	return ret;
}

/*
 * Mount a ouiche_fs partition
 */
struct dentry *ouichefs_mount(struct file_system_type *fs_type, int flags,
			      const char *dev_name, void *data)
{
	struct dentry *dentry = NULL;
	char kobj_partition_name[64];

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

	create_ouichefs_sysfs(&free_blocks, dentry->d_sb, free_blocks_show,
			      "free_blocks");

	create_ouichefs_sysfs(&files, dentry->d_sb, files_show, "files");

	create_ouichefs_sysfs(&total_extents, dentry->d_sb, total_extents_show,
			      "total_extents");

	create_ouichefs_sysfs(&avg_extent_size, dentry->d_sb,
			      avg_extent_size_show, "avg_extent_size");

	create_ouichefs_sysfs(&max_file_size, dentry->d_sb, max_file_size_show,
			      "max_file_size");

	create_ouichefs_sysfs(&fragmentation, dentry->d_sb, fragmentation_show,
			      "fragmentation");

	create_ouichefs_sysfs(&fragmentation, dentry->d_sb,
			      commited_blocks_show, "commited_blocks");

	return dentry;

failed_sysfs_kobject:
	pr_err("failed to create sysfs kobject for '%s'\n", dev_name);

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
