#include <linux/init.h>
#include <linux/module.h>
#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/kernel.h>
#include <linux/uaccess.h>
#include <linux/fs.h>
#include <linux/syscalls.h>
#include <linux/string.h>

#define MINIFS_BLOCK_SIZE 1024
#define N_BLOCKS   128
#define N_INODES   128
#define MINIFS_INODE_SIZE 128
#define DISK_SIZE (MINIFS_BLOCK_SIZE * 3 + N_INODES * MINIFS_INODE_SIZE + MINIFS_BLOCK_SIZE * N_BLOCKS)


static int minifs_open(struct inode* inode, struct file* file);
static ssize_t minifs_read(struct file* file, char __user* user, size_t count, loff_t* offset);
static ssize_t minifs_write(struct file* file, const char __user* user, size_t count, loff_t* offset);

static const char disk_path[] = "minifs_disk";

static const struct file_operations minifs_fops = {
    .owner = THIS_MODULE,
    .open  = minifs_open,
    .read  = minifs_read,
    .write = minifs_write
};

static struct class* minifs_class;
static struct cdev minifs_cdev;
static dev_t minifs_dev;

static int minifs_uevent(struct device* dev, struct kobj_uevent_env* env) {
    add_uevent_var(env, "DEVMODE=%#o", 0666);
    return 0;
}


static int __init minifs_init(void) {
    minifs_class = class_create(THIS_MODULE, "minifs");
    minifs_class->dev_uevent = minifs_uevent;
    alloc_chrdev_region(&minifs_dev, 0, 1, "minifs");
    cdev_init(&minifs_cdev, &minifs_fops);
    minifs_cdev.owner = THIS_MODULE;
    cdev_add(&minifs_cdev, minifs_dev, 1);
    device_create(minifs_class, NULL, minifs_dev, NULL, "minifs");
    return 0;
}

static void __exit minifs_exit(void) {
    device_destroy(minifs_class, minifs_dev);
    class_unregister(minifs_class);
    unregister_chrdev_region(minifs_dev, 1);
}

static int minifs_open(struct inode* inode, struct file* file) {
    mm_segment_t old_fs;
    struct file* filp;
    char buf[MINIFS_BLOCK_SIZE];
    int i;
    loff_t offset;

    printk("minifs: open\n");
    old_fs = get_fs();
    set_fs(KERNEL_DS);

    for (i = 0; i < MINIFS_BLOCK_SIZE; ++i) {
        buf[i] = 0;
    }

    filp = filp_open(disk_path, O_WRONLY | O_CREAT, S_IRUSR | S_IWUSR);
    // i couldn't figure out what's 'struct path' passed to vfs_truncate, so let's do it this way
    for (i = 0; i * MINIFS_BLOCK_SIZE < DISK_SIZE; ++i) {
        offset = i * MINIFS_BLOCK_SIZE;
        vfs_write(filp, buf, MINIFS_BLOCK_SIZE, &offset);
    }
    filp_close(filp, NULL);

    set_fs(old_fs);
    return 0;
}

// the daemon specifies the offset on each write, so there's actually no need to update it, but let's do it anyway for idiomaticity
static ssize_t minifs_read(struct file* file, char __user* user, size_t count, loff_t* offset) {
    int bytes_read;
    mm_segment_t old_fs;
    struct file* filp;
    char buf[MINIFS_BLOCK_SIZE];

    printk("minifs: read\n");
    old_fs = get_fs();
    set_fs(KERNEL_DS);

    if (*offset + count > DISK_SIZE) {
        count = DISK_SIZE - *offset;
    }
    filp = filp_open(disk_path, O_RDONLY, 0);
    bytes_read = vfs_read(filp, buf, count, offset);
    if (copy_to_user(user, buf, bytes_read)) {
        filp_close(filp, NULL);
        set_fs(old_fs);
        return -EFAULT;
    }
    filp_close(filp, NULL);

    set_fs(old_fs);
    return bytes_read;
}

static ssize_t minifs_write(struct file* file, const char __user* user, size_t count, loff_t* offset) {
    int bytes_written;
    mm_segment_t old_fs;
    struct file* filp;
    char buf[MINIFS_BLOCK_SIZE];

    printk("minifs: write");
    old_fs = get_fs();
    set_fs(KERNEL_DS);

    if (*offset + count > DISK_SIZE) {
        count = DISK_SIZE - *offset;
    }
    if (copy_from_user(buf, user, count)) {
        return -EFAULT;
    }
    filp = filp_open(disk_path, O_WRONLY, 0);
    bytes_written = vfs_write(filp, buf, count, offset);
    filp_close(filp, NULL);

    set_fs(old_fs);
    return bytes_written;
}

MODULE_LICENSE("GPL");
MODULE_AUTHOR("equocredite@gmail.com");
MODULE_DESCRIPTION("trying to figure out how this works");

module_init(minifs_init);
module_exit(minifs_exit);
