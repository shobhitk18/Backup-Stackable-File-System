/*
 * Copyright (c) 1998-2017 Erez Zadok
 * Copyright (c) 2009	   Shrikar Archak
 * Copyright (c) 2003-2017 Stony Brook University
 * Copyright (c) 2003-2017 The Research Foundation of SUNY
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include "bkpfs.h"
#include "linux/splice.h"
#include "linux/bkp_shared.h"

#define DEFAULT_BKP_THRESHOLD 32
#define DEFAULT_MAXVERS 10
#define BKP_MAX_FILENAME 230

/* new apis used for creating backups */
extern struct dentry* bkpfs_get_bkp_dentry(struct dentry *lower_parent_dir, const char* name, int is_neg_dentry);
extern int bkpfs_get_xattr_info(struct dentry *dentry, struct bkpfs_xattr_info* xattr);
extern int bkpfs_set_xattr_info(struct dentry *dentry, struct bkpfs_xattr_info* xattr);

static ssize_t bkpfs_read(struct file *file, char __user *buf,
			   size_t count, loff_t *ppos)
{
	int err;
	struct file *lower_file;
	struct dentry *dentry = file->f_path.dentry;
	
	lower_file = bkpfs_lower_file(file);
	err = vfs_read(lower_file, buf, count, ppos);
	/* update our inode atime upon a successful lower read */
	if (err >= 0)
		fsstack_copy_attr_atime(d_inode(dentry),
					file_inode(lower_file));

	return err;
}


/* @brief:This function will create an inode for the negative dentry 
 * passed direclty as an argument
 */
static int bkpfs_create_bkp_inode(struct inode* dir, struct dentry *bkp_dentry,
                         umode_t mode, bool want_excl)
{
	int err = 0;
	struct dentry *parent_dentry = NULL;

	parent_dentry = lock_parent(bkp_dentry);
	err = vfs_create(d_inode(parent_dentry), bkp_dentry, mode, want_excl);
	if (err) 
		goto out;

	fsstack_copy_attr_times(dir, parent_dentry->d_inode);
	fsstack_copy_inode_size(dir, parent_dentry->d_inode);

out:
	unlock_dir(parent_dentry);
	return err;
	
}

/* @brief: 	This function will create the positive backup dentry and populate 
 * 	   	   	the path structure for the same.
 * Input : 
 * 			f_dentry -> upper dentry for user file
 * 	   		bkp_path -> To be populated by the funct if evrything succeeds
 * 	   		num      -> Backup file num to be created
 * Return: 	err
 */
static ssize_t bkpfs_create_backup(struct dentry *f_dentry, struct dentry **bkp_dentry, unsigned int num)
{
	int err = 0;
	const char *fname;
	char *bkp_fname;
	struct dentry *lower_parent_dentry;
	struct inode *dir;

	fname = f_dentry->d_name.name;
	if(strlen(fname) > BKP_MAX_FILENAME){
		printk(KERN_INFO "ERROR::Input file name too large to create backup file\n");
		err = -ENAMETOOLONG;
		goto out;
	}

	bkp_fname = kmalloc(NAME_MAX, GFP_KERNEL);
	if(!bkp_fname){
		err = -ENOMEM;
		goto out;
	}

	sprintf(bkp_fname,".bkp_%s.%d",fname, num);
	printk(KERN_INFO "Create_Backup::backup file=%s\n", bkp_fname);
	
	/* Get lower parent dentry of the file for which the backup is to be created
 	 * This is required to create the backup file in that directory directly on 
 	 * the lower file system. BKPFS doesn't need to know about bkp file dentries.
 	 */

	lower_parent_dentry = BKPFS_D(f_dentry->d_parent)->lower_path.dentry;
	*bkp_dentry = bkpfs_get_bkp_dentry(lower_parent_dentry, bkp_fname, true); 
 
	/* Get Upper inode and newly created negative dentry to populate with inode*/
	dir = d_inode(f_dentry->d_parent); 

	/* Pass the mode of inode of the file passed by the user instead of hardcoding */
	err = bkpfs_create_bkp_inode(dir, *bkp_dentry, f_dentry->d_inode->i_mode, false);
	if (err)
		goto free;
	
free:
	kfree(bkp_fname);
out:
	return err;	
}

static int delete_backup_file(struct inode* dir, struct dentry *dentry, int ver)
{
	int err = 0;
	struct dentry *p_dentry, *bkp_dentry;
	struct dentry *lower_parent_dentry;
	struct inode *parent_dir_inode;
	char *bkp_fname;
	const char *fname;

	fname = dentry->d_name.name;
	bkp_fname =(char*)kmalloc(NAME_MAX, GFP_KERNEL);
	if(!bkp_fname){
		err = -ENOMEM;
		goto exit;
	}

	sprintf(bkp_fname,".bkp_%s.%d",fname, ver);
	//printk(KERN_INFO "Delete backup file=%s\n", bkp_fname);

	lower_parent_dentry = BKPFS_D(dentry->d_parent)->lower_path.dentry;
	bkp_dentry = bkpfs_get_bkp_dentry(lower_parent_dentry, bkp_fname, false);	
	if(IS_ERR(bkp_dentry)) {
		printk(KERN_INFO "ERROR::Couldn't find dentry for bkp file with vers num=%d\n",ver);
		err = PTR_ERR(bkp_dentry);
		goto out;
	}

	dget(bkp_dentry);
	p_dentry = lock_parent(bkp_dentry);
	parent_dir_inode = d_inode(p_dentry);

	err = vfs_unlink(parent_dir_inode, bkp_dentry, NULL);
	if (err)
		goto out;
	
	//printk(KERN_INFO "Deletion of file successfull\n");
	fsstack_copy_attr_times(dir, parent_dir_inode);
	fsstack_copy_inode_size(dir, parent_dir_inode);

	d_drop(bkp_dentry); /* this is needed, else LTP fails (VFS won't do it) */

out:
	unlock_dir(p_dentry);
	dput(bkp_dentry);
	kfree(bkp_fname);
exit:
	return err;

}

int bkpfs_cleanup_on_delete(struct inode *dir, struct dentry *dentry)
{
	int err = 0;
	struct bkpfs_xattr_info *xattr;
	int s_ver, l_ver, ver;

	xattr = kmalloc(sizeof(struct bkpfs_xattr_info), GFP_KERNEL);
	err = bkpfs_get_xattr_info(dentry, xattr); 
	if(err < 0)
		goto out;
	else
		err = 0;

	s_ver = xattr->start_ver;
	l_ver = xattr->cur_ver - 1;

	for(ver = s_ver; ver <= l_ver; ver++) {
		err = delete_backup_file(dir, dentry, ver);
		if(err < 0){
			// Don't break. Try to delete next version
			printk(KERN_INFO "ERROR:: failed while deleting backup number %d\n", ver-s_ver+1);
		}
	}
out:
	kfree(xattr);
	return err;
}

/* @brief: 	update control information in xattr and does necessary
 * 			removing of old backups when version count exceeds range.
 * input :
 * 			dentry: dentry of user file created inside the mount
 * 			xattr: ptr to xattr info used for tracking bkp versions
 * return:	err 
 */
static int bkpfs_update_after_write(struct dentry *dentry, struct bkpfs_xattr_info *xattr, int maxvers)
{
	int err = 0;
	int cur_ver, start_ver;
	struct inode *dir;
	const char *fname;
	
	cur_ver = xattr->cur_ver;
	start_ver = xattr->start_ver;
	
	if(cur_ver - start_ver >= maxvers) {
		fname = dentry->d_name.name;	
		dir = d_inode(dentry->d_parent);

 		err = delete_backup_file(dir, dentry, start_ver);
		if(err < 0){
			printk(KERN_INFO "ERROR:: Failed while deleting backup version=%d\n", start_ver);
			goto out;
		}
		start_ver += 1;
	}
	
	cur_ver += 1;

	xattr->cur_ver = cur_ver;
	xattr->start_ver = start_ver;
	printk(KERN_INFO "bkpfs_update_after_write:: cur_ver=%d, start_ver=%d\n", xattr->cur_ver , xattr->start_ver);
	
	err = bkpfs_set_xattr_info(dentry, xattr);

out:
	return err;

}

static ssize_t bkpfs_write(struct file *file, const char __user *buf,
			    size_t count, loff_t *ppos)
{
	int err = 0;							// err to return status
	unsigned int bkp_threshold;				// Threshold for creating backup
	unsigned int maxvers;					// Max Versions of backup supported
	struct file *lower_file;				// lower file for user file
	struct file *user_file, *bkp_file;		// file* for bkp file and user file used in splice
	struct path lower_parent_path,lower_path;	// path for parent dir and user file 
	struct path bkp_path;					// bkp_path
	struct dentry *bkp_dentry;				// dentry for bkp_file
	struct dentry *dentry, *p_dentry; 		// dentry for user file and parent dir
	struct vfsmount *lower_parent_mnt;		// mnt of lower_parent 
	struct mnt_opt_info * opts; 			// Used to get mount options 
	struct bkpfs_xattr_info *xattr;			// ptr to xattr information for the user file
	loff_t inpos=0, outpos=0; 				// Used to passs to splice_direct
	loff_t size;							// Size of file used while copying
	loff_t bytes_written;					// Total bytes written to orig user file
	
	opts  = &BKPFS_SB(file->f_inode->i_sb)->mnt_opts;
	dentry = file->f_path.dentry;
	p_dentry = dget_parent(dentry);

	/* Populate data passed during mount options */
	maxvers = opts->maxvers ? opts->maxvers : DEFAULT_MAXVERS;
	bkp_threshold = opts->bkp_threshold ? opts->bkp_threshold : DEFAULT_BKP_THRESHOLD;

	printk(KERN_INFO "max Versions=%d, bkp_threshold=%d\n",maxvers, bkp_threshold);
	printk(KERN_INFO "BEFORE_WRITE::filename=%s, parent dir=%s, count=%ld, offset=%lld\n", \
				dentry->d_name.name, p_dentry->d_name.name, count, *ppos);
	
	lower_file = bkpfs_lower_file(file);
	bytes_written = vfs_write(lower_file, buf, count, ppos);
	if (bytes_written < 0) {
		printk(KERN_INFO "ERROR:: VFS write failed\n");
		err = bytes_written;
		goto exit;
	}	
	/* update our inode times+sizes upon a successful lower write */
	fsstack_copy_inode_size(d_inode(dentry),
				file_inode(lower_file));
	fsstack_copy_attr_times(d_inode(dentry),
				file_inode(lower_file));
	
	printk(KERN_INFO "AFTER_WRITE::filename=%s, parent dir=%s, count=%ld, offset=%lld\n", \
				dentry->d_name.name, p_dentry->d_name.name, count, *ppos);
	
	/* if threshold is not reached or no backups needed, then don't 
	 * create backup just return from here.
	 */
	if(count < bkp_threshold || maxvers == 0)
		goto exit;


	/* Allocate the xattr_info and fetch its value from xtended attributes */
	xattr = kmalloc(sizeof(struct bkpfs_xattr_info), GFP_KERNEL);

	err = bkpfs_get_xattr_info(dentry, xattr);
	if(err < 0)
		goto out_free;
	else
		err = 0;
	printk(KERN_INFO "start version=%d, curr version=%d\n", xattr->start_ver, xattr->cur_ver);
	
	err = bkpfs_create_backup(dentry, &bkp_dentry, xattr->cur_ver);
	if(err < 0) {
		printk(KERN_INFO "ERROR:: bkpfs_create_backup failed\n");
		goto out_free;
	}
	printk(KERN_INFO "INFO::backup file created with num=%d\n", xattr->cur_ver);
	
	/* Get lower parent and user file path structs */
	bkpfs_get_lower_path(p_dentry, &lower_parent_path);
	bkpfs_get_lower_path(dentry, &lower_path); 
	lower_parent_mnt = lower_parent_path.mnt;

	/* We should now open the backup file in write mode and start copying the data. 
 	 * By this time there should be a positive dentry created for the backup file
 	 * Populate the path for the bkp dentry.  	
 	 */ 	
	bkp_path.dentry = bkp_dentry;
	bkp_path.mnt = mntget(lower_parent_mnt);
	
	bkp_file = dentry_open(&bkp_path, O_LARGEFILE | O_WRONLY, current_cred());
	if(IS_ERR(bkp_file)){
		printk(KERN_INFO "ERROR::Failed to open bkp_file\n");
		goto out_put_path;
	}

	user_file = dentry_open(&lower_path, O_RDONLY, current_cred());
	if(IS_ERR(user_file)){
		printk(KERN_INFO "ERROR::Failed to open user_file\n");
		goto out_put_file;
	}  	

	size = i_size_read(dentry->d_inode);
	err = do_splice_direct(user_file, &inpos, bkp_file, &outpos, size, SPLICE_F_MOVE);
	if(err < 0) {
		printk(KERN_INFO "ERROR:: Failed inside do_splice_direct\n");	
		goto out_put_file1;
	}
	
	/* if backup was successfully created, update control info */
	err = bkpfs_update_after_write(dentry, xattr, maxvers);
	if(err < 0)
		printk(KERN_INFO "ERROR:: Failed update after write\n");

out_put_file1:
	fput(user_file);
out_put_file:
	fput(bkp_file);
out_put_path:
	bkpfs_put_lower_path(dentry, &lower_path);
	bkpfs_put_lower_path(p_dentry, &lower_parent_path);
out_free:
	kfree(xattr);
exit:
	dput(p_dentry);
	printk(KERN_INFO "exit bkpfs_write with bytes_written=%lld\n", bytes_written);
	if(err < 0)
		return err;
	else
		return bytes_written;
}

struct bkpfs_getdents_callback {
	struct dir_context ctx;
	struct dir_context *caller;
	struct super_block *sb;
	int filldir_called;
	int entries_written;
};

static int bkpfs_filldir(struct dir_context *ctx, const char *lower_name,
				 int lower_namelen, loff_t offset, u64 ino, unsigned int d_type)
{
	int err;
	struct bkpfs_getdents_callback *buf =
			container_of(ctx, struct bkpfs_getdents_callback, ctx);
	
	buf->filldir_called++;
	printk(KERN_INFO "File=%s\n", lower_name);
	if(lower_name[0] == '.' && strnstr(lower_name, "bkp_", 5) != NULL){
		/* This is backup file. Don't display this */
		printk(KERN_INFO "Hiding display of %s\n", lower_name);
		return 0; 
	}

	buf->caller->pos = buf->ctx.pos;
	err = !dir_emit(buf->caller, lower_name, lower_namelen, ino, d_type);
	if (!err)
		buf->entries_written++;

	return err;
}

static int bkpfs_readdir(struct file *file, struct dir_context *ctx)
{
	int err;
	struct file *lower_file = NULL;
	struct dentry *dentry = file->f_path.dentry;
	/* Hack here to hide displaying of backup files */
	struct bkpfs_getdents_callback buf = {
		.ctx.actor = bkpfs_filldir,
		.caller = ctx,
		.sb = d_inode(dentry)->i_sb,
	};
	
	lower_file = bkpfs_lower_file(file);
	err = iterate_dir(lower_file, &buf.ctx);
	file->f_pos = lower_file->f_pos;
	if(err < 0)
		goto out;
	if (buf.filldir_called && !buf.entries_written)
		goto out;
	if (err >= 0)		/* copy the atime */
		fsstack_copy_attr_atime(d_inode(dentry),
					file_inode(lower_file));
out:
	return err;
}

static long bkpfs_get_max_version(struct file *file, int *val)
{
	int err = 0;
	struct mnt_opt_info * opts;

	opts  = &BKPFS_SB(file->f_inode->i_sb)->mnt_opts;
	*val = opts->maxvers ? opts->maxvers : DEFAULT_MAXVERS;
	return err;
}

static long bkpfs_get_version_info(struct file *file, int* s_ver, int* l_ver)
{
	int err = 0;
	struct dentry *dentry;	
	struct bkpfs_xattr_info *xattr;

	dentry = file->f_path.dentry;
	xattr = kmalloc(sizeof(struct bkpfs_xattr_info), GFP_KERNEL);
	err = bkpfs_get_xattr_info(dentry, xattr); 
	if(err < 0)
		goto out;
	else
		err = 0;

	(*s_ver) = xattr->start_ver;
	(*l_ver) = xattr->cur_ver - 1;
	
out:
	kfree(xattr);
	return err;
}

static long ioctl_verify_copy_args(struct ioctl_args *uarg, struct ioctl_args *karg)
{
	long err = 0;

	if(!uarg) {
		err = -EINVAL;
		goto out;
	}

	if(!access_ok(VERIFY_READ, uarg, sizeof(struct ioctl_args)))
	{   
		printk(KERN_WARNING "Cannot access ioctl_arg structure\n");
		err = -EFAULT;
		goto out;
	}

	if(copy_from_user(karg, uarg, sizeof(struct ioctl_args)))
	{
		printk(KERN_WARNING "copy of ioctl_args from user space to kernel space failed. Check permissions\n");
		err = -EFAULT;
		goto out;
	}

	if(!access_ok(VERIFY_READ, karg->in_arg, karg->in_arg_size))
	{   
		printk(KERN_WARNING "Cannot access in_arg inside ioclt_arg structure\n");
		err = -EFAULT;
		goto out;
	}

out:
	return err;

}

struct file* open_backup_file(struct file *file, int flags, int ver, char *bkp_fname)
{
	long err = 0;
	struct dentry *bkp_dentry, *dentry, *p_dentry;
	struct dentry *lower_parent_dentry;
	struct path lower_parent_path,lower_path;
	struct path bkp_path;
	struct file* bkp_file;
	struct vfsmount *lower_parent_mnt;
	const char *fname;
	UDBG;

	dentry = file->f_path.dentry;
	p_dentry = dget_parent(dentry);

	fname = dentry->d_name.name;

	sprintf(bkp_fname,".bkp_%s.%d",fname, ver);
	printk(KERN_INFO "Read backup file=%s\n", bkp_fname);

	lower_parent_dentry = BKPFS_D(dentry->d_parent)->lower_path.dentry;
	bkp_dentry = bkpfs_get_bkp_dentry(lower_parent_dentry, bkp_fname, false);	
	if(IS_ERR(bkp_dentry)) {
		printk(KERN_INFO "Couldn't find dentry for backup file with version num=%d\n",ver);
		err = PTR_ERR(bkp_dentry);
		goto out;
	}

	/* Get lower parent and user file path structs */
	bkpfs_get_lower_path(p_dentry, &lower_parent_path);
	bkpfs_get_lower_path(dentry, &lower_path); 
	lower_parent_mnt = lower_parent_path.mnt;

	/* We should now open the backup file in write mode and start copying the data. 
 	 * By this time there should be a positive dentry created for the backup file
 	 * Populate the path for the bkp dentry.  	
 	 */ 	
	bkp_path.dentry = bkp_dentry;
	bkp_path.mnt = mntget(lower_parent_mnt);

	bkp_file = dentry_open(&bkp_path, flags, current_cred());
	if(IS_ERR(bkp_file)){
		printk(KERN_INFO "ERROR::Failed to open bkp_file\n");
		err = PTR_ERR(bkp_file);
		goto out1;
	}

out1:
	bkpfs_put_lower_path(dentry, &lower_path);
	bkpfs_put_lower_path(p_dentry, &lower_parent_path);
out:
	 dput(p_dentry);
	if(err < 0)
		return ERR_PTR(err);
	return bkp_file;
	
}

static long read_backup_version(struct file *file, struct ioctl_args *karg, int ver, loff_t pos)
{
	long err = 0, res;
	struct file* bkp_file;
	char *bkp_fname;
	void *buff;
	
	printk(KERN_INFO "INFO::read_backup_version=%d at offset=%lld\n", ver, pos);
	buff = kmalloc(PAGE_SIZE, GFP_KERNEL);
	
	if(!access_ok(VERIFY_WRITE, karg->buff, karg->buff_size))
	{   
		printk(KERN_WARNING "No write access to buffer region\n");
		err = -EFAULT;
		goto out;
	}
	
	bkp_fname =(char*)kmalloc(NAME_MAX, GFP_KERNEL);
	if(!bkp_fname){
		err = -ENOMEM;
		goto out1;
	}

	bkp_file = open_backup_file(file, O_RDONLY, ver, bkp_fname);
	if(IS_ERR(bkp_file)){
		printk(KERN_INFO "ERROR::Failed to open bkp_file\n");
		err = PTR_ERR(bkp_file);
		goto out1;
	}
	
	/* read the backup data in internal buffer first and then copy it to user buf*/
	res = kernel_read(bkp_file, buff, karg->buff_size, &pos);
	if(res != karg->buff_size) {
		printk("Read succeeded partially with # bytes read =%ld\n",res);
		err = -EIO;
		goto out2;
	}

	if(copy_to_user(karg->buff, buff, karg->buff_size))
	{
		printk(KERN_WARNING "copy of data to buffer failed. Check permissions\n");
		err = -EFAULT;
		goto out2;
	}

out2: 
	fput(bkp_file);
out1:
	kfree(bkp_fname);
out:
	kfree(buff);
	return err;
}

static long bkpfs_view_backup(struct file *file, struct ioctl_args *arg)
{
	long err;
	struct ioctl_args *karg;
	struct view_args *in_arg;
	int s_ver, l_ver;
	int version;
	loff_t offset;

	karg = kmalloc(sizeof(struct ioctl_args), GFP_KERNEL);
	if(!karg) {
		err = -ENOMEM;
		goto out;
	}

	/* verify and copy arguments to kernel space. this also 
	 * verifies the in_arg user space buffer for access_ok
	 */
	err = ioctl_verify_copy_args(arg, karg);
	if(err < 0)
		goto out;

	printk(KERN_INFO "INFO::view argument size = %d\n",karg->in_arg_size);
	printk(KERN_INFO "INFO::view buffer size = %d\n",karg->buff_size);

	in_arg = kmalloc(karg->in_arg_size, GFP_KERNEL); 
	if(!in_arg) {
		err = -ENOMEM;
		goto out;
	}

	if(copy_from_user(in_arg, karg->in_arg, karg->in_arg_size))
	{
		printk(KERN_WARNING "copy of args from user space to kernel space failed. check perms\n");
		err = -EFAULT;
		goto out;
	}

	karg->in_arg = in_arg;

	version = in_arg->version;
	offset = in_arg->offset;
	printk(KERN_INFO "INFO::View version=%d from off=%llu\n", version, offset);
	
	err = bkpfs_get_version_info(file, &s_ver, &l_ver);
	if(err < 0)
		goto out;

	if(l_ver - s_ver < 0) {
		printk(KERN_INFO "No backups exists\n");
		err = -ENOENT;
		goto out;
	}

	switch(version) {
		case -1:
			/* Read oldest backup version into the user buffer */ 
			err = read_backup_version(file, karg, s_ver, offset);
			break;
		case 0:
			/* Read the latest backup version into the user buffer */
			err = read_backup_version(file, karg, l_ver, offset);
			break;
		
		default:
			/* Any valid version number less than number of versions present */
			if(version < 0 || version  > l_ver - s_ver + 1) {
				err = -EINVAL;
				goto out;
			}
			err = read_backup_version(file, karg, version+s_ver-1, offset);
	}

out:
	if(in_arg)
		kfree(in_arg);
	if(karg)
		kfree(karg);

	return err;

}

static long bkpfs_delete_backup(struct file *file, struct ioctl_args *arg)
{
	long err;
	struct ioctl_args *karg;
	struct delete_args *in_arg;
	struct bkpfs_xattr_info xattr;
	struct dentry *dentry;
	struct inode *dir;
	int version, s_ver, l_ver;
	int ver;

	karg = kzalloc(sizeof(struct ioctl_args), GFP_KERNEL);
	if(!karg) {
		err = -ENOMEM;
		goto out;
	}
	/* verify and copy arguments to kernel space */
	err = ioctl_verify_copy_args(arg, karg);
	if(err < 0)
		goto out;

	printk(KERN_INFO "INFO::delete argument size = %d\n",karg->in_arg_size);
	printk(KERN_INFO "INFO::delete buffer size = %d\n",karg->buff_size);

	in_arg = kmalloc(karg->in_arg_size, GFP_KERNEL); 
	if(!in_arg) {
		err = -ENOMEM;
		goto out;
	}

	if(copy_from_user(in_arg, karg->in_arg, karg->in_arg_size))
	{
		printk(KERN_WARNING "copy of args from user space to kernel space failed.check perms\n");
		err = -EFAULT;
		goto out;
	}

	karg->in_arg = in_arg;
	version = in_arg->version;

	dentry = file->f_path.dentry;
	dir = d_inode(dentry->d_parent);

	err = bkpfs_get_version_info(file, &s_ver, &l_ver);	
	if(err < 0)
		goto out;
	
	if(l_ver - s_ver < 0) {
		printk(KERN_INFO "No backups exists\n");
		err = -ENOENT;
		goto out;
	}

	switch(version) {
		case -1:
			/* Delete oldest backup version for this file */ 
			printk(KERN_INFO "deleting oldest backup version\n");
			err = delete_backup_file(dir, dentry, s_ver);
			xattr.start_ver = s_ver+1;
			xattr.cur_ver = l_ver+1;
			err = bkpfs_set_xattr_info(dentry, &xattr);
			break;

		case 0:
			/* Delete the latest backup version for this file */	
			printk(KERN_INFO "deleting newest backup version\n");
			err = delete_backup_file(dir, dentry, l_ver);
			xattr.start_ver = s_ver;
			xattr.cur_ver = l_ver;
			err = bkpfs_set_xattr_info(dentry, &xattr);
			break;
		
		case 1:
			/* Delete all backup versions for this file */
			for(ver = s_ver; ver <= l_ver; ver++) {
				err = delete_backup_file(dir, dentry, ver);
				if(err < 0){
					// Don't break. Try to delete next version
					printk(KERN_INFO "ERROR:: failed while deleting backup number %d\n", ver-s_ver+1);
				}
			}
			xattr.start_ver = 1;
			xattr.cur_ver = 1;
			err = bkpfs_set_xattr_info(dentry, &xattr);
			break;

		default:
			/* Any other version number is invalid for delete ioctl*/
			err = -EINVAL;
			printk(KERN_INFO "ERROR::delete called with invalid args\n");
	}

out:
	if(in_arg)
		kfree(in_arg);
	if(karg)
		kfree(karg);

	return err;

}

static long bkpfs_restore_backup(struct file *file, struct ioctl_args *arg)
{
	long err = 0;
	struct ioctl_args *karg;
	struct restore_args *in_arg;
	struct file *user_file, *bkp_file; 
	struct path lower_path;
	struct inode *inode;
	struct dentry *dentry;
	char *bkp_fname;
	int s_ver, l_ver, version;
	loff_t inpos = 0, outpos = 0;
	loff_t size, new_size;

	karg = kmalloc(sizeof(struct ioctl_args), GFP_KERNEL);
	if(!karg) {
		err = -ENOMEM;
		goto out;
	}

	/* verify and copy arguments to kernel space. this also 
	 * verifies the in_arg user space buffer for access_ok
	 */
	err = ioctl_verify_copy_args(arg, karg);
	if(err < 0)
		goto out;

	printk(KERN_INFO "INFO::restore argument size = %d\n",karg->in_arg_size);
	printk(KERN_INFO "INFO::restore buffer size = %d\n",karg->buff_size);

	in_arg = kmalloc(karg->in_arg_size, GFP_KERNEL); 
	if(!in_arg) {
		err = -ENOMEM;
		goto out;
	}

	if(copy_from_user(in_arg, karg->in_arg, karg->in_arg_size))
	{
		printk(KERN_WARNING "copy of args from user space to kernel space failed. check perms\n");
		err = -EFAULT;
		goto out;
	}

	karg->in_arg = in_arg;
	version = in_arg->version;

	err = bkpfs_get_version_info(file, &s_ver, &l_ver);	
	if(err < 0)
		goto out;

	if(version < 0 || (l_ver < s_ver) || (version > l_ver - s_ver + 1)) {
		err = -ENOENT;
		goto out;
	}

	if(version == 0) 					// newest
		version = l_ver;
	else
		version = s_ver + version - 1;	// "N"

	bkp_fname =(char*)kmalloc(NAME_MAX, GFP_KERNEL);
	if(!bkp_fname){
		err = -ENOMEM;
		goto out;
	}

	bkp_file = open_backup_file(file, O_RDONLY, version, bkp_fname);
	if(IS_ERR(bkp_file)){
		printk(KERN_INFO "ERROR::Failed to open bkp_file\n");
		err = PTR_ERR(bkp_file);
		goto out;
	}
	
	dentry = file->f_path.dentry;
	inode = d_inode(dentry);
	bkpfs_get_lower_path(dentry, &lower_path);

	user_file = dentry_open(&lower_path, O_TRUNC | O_WRONLY, current_cred());
	if(IS_ERR(user_file)){
		printk(KERN_INFO "ERROR::Failed to open user_file\n");
		goto put_file;
	}  	
	
	size = i_size_read(bkp_file->f_path.dentry->d_inode);
	printk("size of backup data to be restored=%lld\n", size);
	
	new_size = do_splice_direct(bkp_file, &inpos, user_file, &outpos, size, SPLICE_F_MOVE);
	if(new_size < 0) {
		printk(KERN_INFO "ERROR:: Failed inside do_splice_direct\n");	
		err = new_size;
	}
	else
	{
		printk(KERN_INFO "Update lower inode size after restore\n");
		err = inode_newsize_ok(d_inode(lower_path.dentry), new_size);
		if (!err)
		{	
			truncate_setsize(d_inode(lower_path.dentry), new_size);
			fsstack_copy_inode_size(inode, bkpfs_lower_inode(inode));
			fsstack_copy_attr_all(inode, bkpfs_lower_inode(inode));
		}

	}

	fput(user_file);

put_file:
	fput(bkp_file);
	bkpfs_put_lower_path(dentry, &lower_path);
out:
	if(bkp_fname)
		kfree(bkp_fname);
	if(in_arg)
		kfree(in_arg);
	if(karg)
		kfree(karg);

	return err;
}

static long bkpfs_get_file_size(struct file *file, struct ioctl_args *arg)
{
	long err = 0;
	struct ioctl_args *karg;
	struct dentry *dentry, *lower_parent_dentry;
	struct dentry *bkp_dentry;
	const char *fname;
	char *bkp_fname;
	long long size;
	int version, s_ver, l_ver;

	if(!arg) {
		err = -EFAULT;
		goto out;
	}

	karg = kmalloc(sizeof(struct ioctl_args), GFP_KERNEL);
	if(!karg) {
		err = -ENOMEM;
		goto out;
	}
	/* verify and copy arguments to kernel space */
	err = ioctl_verify_copy_args(arg, karg);
	if(err < 0)
		goto out;

	if(copy_from_user(&version, karg->in_arg, karg->in_arg_size))
	{
		printk(KERN_WARNING "copy of args from user space to kernel space failed.Check perms\n");
		err = -EFAULT;
		goto out;
	}
	
	if(!access_ok(VERIFY_WRITE, karg->buff, karg->buff_size))
	{
		printk(KERN_WARNING "No write access to buffer region\n");
		err = -EFAULT;
		goto out;
	}
	
	dentry = file->f_path.dentry;
	fname = dentry->d_name.name;

	bkp_fname =(char*)kmalloc(NAME_MAX, GFP_KERNEL);
	if(!bkp_fname){
		err = -ENOMEM;
		goto out;
	}
	
	err = bkpfs_get_version_info(file, &s_ver, &l_ver);		
	if(err < 0)
		goto out;
	
	if(l_ver - s_ver < 0) {
		printk(KERN_INFO "No backups exists\n");
		err = -ENOENT;
		goto out;
	}
	
	switch(version) {
		case -1:
			version = s_ver;
			break;
		case 0:
			version = l_ver;
			break;
		default:
			if(version > l_ver - s_ver + 1 ) {
				err = -ENOENT;
				goto out;
			}
			version = version + s_ver - 1;
	}

	sprintf(bkp_fname,".bkp_%s.%d",fname, version);
	
	lower_parent_dentry = BKPFS_D(dentry->d_parent)->lower_path.dentry;
	bkp_dentry = bkpfs_get_bkp_dentry(lower_parent_dentry, bkp_fname, false);	
	if(IS_ERR(bkp_dentry)) {
		printk(KERN_INFO "Couldn't find dentry for backup file with version num=%d\n",version);
		err = PTR_ERR(bkp_dentry);
		goto out;
	}

	size = d_inode(bkp_dentry)->i_size;	
	if(copy_to_user(karg->buff, &size, karg->buff_size))
	{
		printk(KERN_WARNING "copy of data to buffer failed. Check permissions\n");
		err = -EFAULT;
		goto out;
	}

out:
	if(bkp_fname)
		kfree(bkp_fname);
	if(karg)
		kfree(karg);
	return err;
}

static long bkpfs_handle_ioctl(struct file *file, unsigned int cmd, void *arg)
{
	long err;
	int s_ver, l_ver;
	int val;

	if(S_ISDIR(file->f_path.dentry->d_inode->i_mode)) {
		printk(KERN_INFO "ERROR::ioctl on directory\n");
		err = -EISDIR;
		goto out;
	}

	switch(cmd) {
		case IOCTL_GET_MAX_VERS:
			printk(KERN_INFO "INFO::max version number requested\n");
			err = bkpfs_get_max_version(file, &val);
			if(err < 0) {
				printk(KERN_INFO "ERROR::failed in bkpfs_get_max_version\n");
				goto out;
			}
			err = put_user(val, (int*)arg);
			break;

		case IOCTL_GET_NUM_VERS:
			printk(KERN_INFO "INFO::number of versions requested\n");
			err = bkpfs_get_version_info(file, &s_ver, &l_ver);
			if(err < 0){
				printk(KERN_INFO "ERROR::failed in bkpfs_get_num_version\n");
				goto out;
			}
			val = l_ver - s_ver + 1;
			err = put_user(val, (int*)arg);
			break;

		case IOCTL_VIEW_VERS:
			printk(KERN_INFO "INFO::view of version requested\n");
			err = bkpfs_view_backup(file, (struct ioctl_args*)arg);
			if(err < 0)
				printk(KERN_INFO "ERROR::failed in bkpfs_view_version\n");
			break;

		case IOCTL_DELETE_VERS:
			/* We need to handle deletion of version files here */
			printk(KERN_INFO "INFO::deletion of version requested\n");
			err = bkpfs_delete_backup(file, (struct ioctl_args*)arg);
			if(err < 0)
				printk(KERN_INFO "ERROR::failed in bkpfs_delete_version\n");
			break;


		case IOCTL_RESTORE_VERS:
			/* Handle restoring of version files to original file */
			printk(KERN_INFO "INFO::restoring of version requested\n");
			err = bkpfs_restore_backup(file, (struct ioctl_args*)arg);
			if(err < 0)
				printk(KERN_INFO "ERROR::failed in bkpfs_restore_version\n");

			break;

		case IOCTL_GET_FILE_SIZE:
			/* Get total size of the backup file and return to user */
			printk(KERN_INFO "INFO::size of version file requested\n");
			err = bkpfs_get_file_size(file, (struct ioctl_args*)arg);
			if(err < 0)
				printk(KERN_INFO "ERROR::failed in bkpfs_get_file_size\n");
			break;

		default:
			printk(KERN_INFO "Unrecognised option for bkpfs. Pass down to lower fs\n");
			err = -ENOEXEC;
	}

out:
	return err;
}
static long bkpfs_unlocked_ioctl(struct file *file, unsigned int cmd,
				  unsigned long arg)
{
	long err = -ENOTTY;
	struct file *lower_file;

	/* Tap ioctls which are defined for bkpfs specific tasks here */
	err = bkpfs_handle_ioctl(file, cmd, (void*)arg);
	if(err != -ENOEXEC)
		goto out;

	lower_file = bkpfs_lower_file(file);

	/* XXX: use vfs_ioctl if/when VFS exports it */
	if (!lower_file || !lower_file->f_op)
		goto out;
	if (lower_file->f_op->unlocked_ioctl)
		err = lower_file->f_op->unlocked_ioctl(lower_file, cmd, arg);

	/* some ioctls can change inode attributes (EXT2_IOC_SETFLAGS) */
	if (!err)
		fsstack_copy_attr_all(file_inode(file),
				      file_inode(lower_file));
out:
	return err;
}

#ifdef CONFIG_COMPAT
static long bkpfs_compat_ioctl(struct file *file, unsigned int cmd,
				unsigned long arg)
{
	long err = -ENOTTY;
	struct file *lower_file;
	UDBG;

	lower_file = bkpfs_lower_file(file);

	/* XXX: use vfs_ioctl if/when VFS exports it */
	if (!lower_file || !lower_file->f_op)
		goto out;
	if (lower_file->f_op->compat_ioctl)
		err = lower_file->f_op->compat_ioctl(lower_file, cmd, arg);

out:
	return err;
}
#endif

static int bkpfs_mmap(struct file *file, struct vm_area_struct *vma)
{
	int err = 0;
	bool willwrite;
	struct file *lower_file;
	const struct vm_operations_struct *saved_vm_ops = NULL;
	UDBG;

	/* this might be deferred to mmap's writepage */
	willwrite = ((vma->vm_flags | VM_SHARED | VM_WRITE) == vma->vm_flags);

	/*
	 * File systems which do not implement ->writepage may use
	 * generic_file_readonly_mmap as their ->mmap op.  If you call
	 * generic_file_readonly_mmap with VM_WRITE, you'd get an -EINVAL.
	 * But we cannot call the lower ->mmap op, so we can't tell that
	 * writeable mappings won't work.  Therefore, our only choice is to
	 * check if the lower file system supports the ->writepage, and if
	 * not, return EINVAL (the same error that
	 * generic_file_readonly_mmap returns in that case).
	 */
	lower_file = bkpfs_lower_file(file);
	if (willwrite && !lower_file->f_mapping->a_ops->writepage) {
		err = -EINVAL;
		printk(KERN_ERR "bkpfs: lower file system does not "
		       "support writeable mmap\n");
		goto out;
	}

	/*
	 * find and save lower vm_ops.
	 *
	 * XXX: the VFS should have a cleaner way of finding the lower vm_ops
	 */
	if (!BKPFS_F(file)->lower_vm_ops) {
		err = lower_file->f_op->mmap(lower_file, vma);
		if (err) {
			printk(KERN_ERR "bkpfs: lower mmap failed %d\n", err);
			goto out;
		}
		saved_vm_ops = vma->vm_ops; /* save: came from lower ->mmap */
	}

	/*
	 * Next 3 lines are all I need from generic_file_mmap.  I definitely
	 * don't want its test for ->readpage which returns -ENOEXEC.
	 */
	file_accessed(file);
	vma->vm_ops = &bkpfs_vm_ops;

	file->f_mapping->a_ops = &bkpfs_aops; /* set our aops */
	if (!BKPFS_F(file)->lower_vm_ops) /* save for our ->fault */
		BKPFS_F(file)->lower_vm_ops = saved_vm_ops;

out:
	return err;
}

static int bkpfs_open(struct inode *inode, struct file *file)
{
	int err = 0;
	struct file *lower_file = NULL;
	struct path lower_path;
	UDBG;

	/* don't open unhashed/deleted files */
	if (d_unhashed(file->f_path.dentry)) {
		err = -ENOENT;
		goto out_err;
	}

	file->private_data =
		kzalloc(sizeof(struct bkpfs_file_info), GFP_KERNEL);
	if (!BKPFS_F(file)) {
		err = -ENOMEM;
		goto out_err;
	}

	/* open lower object and link bkpfs's file struct to lower's */
	bkpfs_get_lower_path(file->f_path.dentry, &lower_path);
	lower_file = dentry_open(&lower_path, file->f_flags, current_cred());
	path_put(&lower_path);
	if (IS_ERR(lower_file)) {
		err = PTR_ERR(lower_file);
		lower_file = bkpfs_lower_file(file);
		if (lower_file) {
			bkpfs_set_lower_file(file, NULL);
			fput(lower_file); /* fput calls dput for lower_dentry */
		}
	} else {
		bkpfs_set_lower_file(file, lower_file);
	}

	if (err)
		kfree(BKPFS_F(file));
	else
		fsstack_copy_attr_all(inode, bkpfs_lower_inode(inode));
out_err:
	return err;
}

static int bkpfs_flush(struct file *file, fl_owner_t id)
{
	int err = 0;
	struct file *lower_file = NULL;
	UDBG;

	lower_file = bkpfs_lower_file(file);
	if (lower_file && lower_file->f_op && lower_file->f_op->flush) {
		filemap_write_and_wait(file->f_mapping);
		err = lower_file->f_op->flush(lower_file, id);
	}

	return err;
}

/* release all lower object references & free the file info structure */
static int bkpfs_file_release(struct inode *inode, struct file *file)
{
	struct file *lower_file;
	UDBG;
	lower_file = bkpfs_lower_file(file);
	if (lower_file) {
		bkpfs_set_lower_file(file, NULL);
		fput(lower_file);
	}

	kfree(BKPFS_F(file));
	return 0;
}

static int bkpfs_fsync(struct file *file, loff_t start, loff_t end,
			int datasync)
{
	int err;
	struct file *lower_file;
	struct path lower_path;
	struct dentry *dentry = file->f_path.dentry;
	UDBG;

	err = __generic_file_fsync(file, start, end, datasync);
	if (err)
		goto out;
	lower_file = bkpfs_lower_file(file);
	bkpfs_get_lower_path(dentry, &lower_path);
	err = vfs_fsync_range(lower_file, start, end, datasync);
	bkpfs_put_lower_path(dentry, &lower_path);
out:
	return err;
}

static int bkpfs_fasync(int fd, struct file *file, int flag)
{
	int err = 0;
	struct file *lower_file = NULL;
	UDBG;

	lower_file = bkpfs_lower_file(file);
	if (lower_file->f_op && lower_file->f_op->fasync)
		err = lower_file->f_op->fasync(fd, lower_file, flag);

	return err;
}

/*
 * Bkpfs cannot use generic_file_llseek as ->llseek, because it would
 * only set the offset of the upper file.  So we have to implement our
 * own method to set both the upper and lower file offsets
 * consistently.
 */
static loff_t bkpfs_file_llseek(struct file *file, loff_t offset, int whence)
{
	int err;
	struct file *lower_file;
	UDBG;

	err = generic_file_llseek(file, offset, whence);
	if (err < 0)
		goto out;

	lower_file = bkpfs_lower_file(file);
	err = generic_file_llseek(lower_file, offset, whence);

out:
	return err;
}

/*
 * Bkpfs read_iter, redirect modified iocb to lower read_iter
 */
ssize_t
bkpfs_read_iter(struct kiocb *iocb, struct iov_iter *iter)
{
	int err;
	struct file *file = iocb->ki_filp, *lower_file;
	UDBG;

	lower_file = bkpfs_lower_file(file);
	if (!lower_file->f_op->read_iter) {
		err = -EINVAL;
		goto out;
	}

	get_file(lower_file); /* prevent lower_file from being released */
	iocb->ki_filp = lower_file;
	err = lower_file->f_op->read_iter(iocb, iter);
	iocb->ki_filp = file;
	fput(lower_file);
	/* update upper inode atime as needed */
	if (err >= 0 || err == -EIOCBQUEUED)
		fsstack_copy_attr_atime(d_inode(file->f_path.dentry),
					file_inode(lower_file));
out:
	return err;
}

/*
 * Bkpfs write_iter, redirect modified iocb to lower write_iter
 */
ssize_t
bkpfs_write_iter(struct kiocb *iocb, struct iov_iter *iter)
{
	int err;
	struct file *file = iocb->ki_filp, *lower_file;
	UDBG;

	lower_file = bkpfs_lower_file(file);
	if (!lower_file->f_op->write_iter) {
		err = -EINVAL;
		goto out;
	}

	get_file(lower_file); /* prevent lower_file from being released */
	iocb->ki_filp = lower_file;
	err = lower_file->f_op->write_iter(iocb, iter);
	iocb->ki_filp = file;
	fput(lower_file);
	/* update upper inode times/sizes as needed */
	if (err >= 0 || err == -EIOCBQUEUED) {
		fsstack_copy_inode_size(d_inode(file->f_path.dentry),
					file_inode(lower_file));
		fsstack_copy_attr_times(d_inode(file->f_path.dentry),
					file_inode(lower_file));
	}
out:
	return err;
}

const struct file_operations bkpfs_main_fops = {
	.llseek		= generic_file_llseek,
	.read		= bkpfs_read,
	.write		= bkpfs_write,
	.unlocked_ioctl	= bkpfs_unlocked_ioctl,
#ifdef CONFIG_COMPAT
	.compat_ioctl	= bkpfs_compat_ioctl,
#endif
	.mmap		= bkpfs_mmap,
	.open		= bkpfs_open,
	.flush		= bkpfs_flush,
	.release	= bkpfs_file_release,
	.fsync		= bkpfs_fsync,
	.fasync		= bkpfs_fasync,
	.read_iter	= bkpfs_read_iter,
	.write_iter	= bkpfs_write_iter,
};

/* trimmed directory options */
const struct file_operations bkpfs_dir_fops = {
	.llseek		= bkpfs_file_llseek,
	.read		= generic_read_dir,
	.iterate	= bkpfs_readdir,
	.unlocked_ioctl	= bkpfs_unlocked_ioctl,
#ifdef CONFIG_COMPAT
	.compat_ioctl	= bkpfs_compat_ioctl,
#endif
	.open		= bkpfs_open,
	.release	= bkpfs_file_release,
	.flush		= bkpfs_flush,
	.fsync		= bkpfs_fsync,
	.fasync		= bkpfs_fasync,
};
