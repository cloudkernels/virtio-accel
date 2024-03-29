#include <linux/cdev.h>
#include <linux/poll.h>
#include <linux/sched.h>
#include <linux/module.h>
#include <linux/wait.h>
#include <linux/miscdevice.h>
#include <linux/virtio.h>
#include <linux/virtio_config.h>
#include <linux/slab.h>
#include <linux/version.h>

#include "accel.h"
#include "virtio_accel-common.h"

static long accel_dev_ioctl(struct file *filp, unsigned int cmd,
		unsigned long _arg)
{
	void __user *arg = (void __user *)_arg;
	struct virtio_accel_file *vaccel_file = filp->private_data;
	struct virtio_accel *vaccel = vaccel_file->vaccel;
	struct virtio_accel_req *req;
	struct accel_session *sess = NULL;
	int ret;

	req = kzalloc(sizeof(*req), GFP_KERNEL);
	if (!req)
		return -ENOMEM;

	sess = kmalloc(sizeof(*sess), GFP_KERNEL);
	if (!sess) {
		ret = -ENOMEM;
		goto err_req;
	}

	if (unlikely(copy_from_user(sess, arg, sizeof(*sess)))) {
		ret = -EFAULT;
		goto err_req;
	}

	req->usr = arg;
	req->priv = sess;
	req->vaccel = vaccel;

	switch (cmd) {
	case VACCEL_SESS_CREATE:
		ret = virtaccel_req_create_session(req);
		if (ret != -EINPROGRESS)
			goto err_req;
		break;
	case VACCEL_SESS_DESTROY:
		ret = virtaccel_req_destroy_session(req);
		if (ret != -EINPROGRESS)
			goto err_req;
		break;
	case VACCEL_DO_OP:
		ret = virtaccel_req_operation(req);
		if (ret != -EINPROGRESS)
			goto err_req;
		break;
	default:
		pr_err("Invalid IOCTL\n");
		ret = -EFAULT;
		goto err;
	}

	pr_debug("Waiting for request to complete\n");
	wait_for_completion_killable(&req->completion);
	virtaccel_handle_req_result(req);
	virtaccel_clear_req(req);
	reinit_completion(&req->completion);
	pr_debug("Request completed\n");

	ret = req->ret;
#if LINUX_VERSION_CODE < KERNEL_VERSION(5,10,0)
	kzfree(req);
#else
	kfree_sensitive(req);
#endif
	return ret;

err_req:
	kfree(sess);
#if LINUX_VERSION_CODE < KERNEL_VERSION(5,10,0)
	kzfree(req);
#else
	kfree_sensitive(req);
#endif
err:
	return ret;
}

static int accel_dev_open(struct inode *inode, struct file *filp)
{
	struct virtio_accel *vaccel = virtaccel_devmgr_get_first();
	struct virtio_accel_file *vaccel_file;

	if (!vaccel)
		return -ENODEV;

	vaccel_file = kzalloc(sizeof(*vaccel_file), GFP_KERNEL);
	if (!vaccel_file)
		return -ENOMEM;

	vaccel->dev_minor = iminor(inode);

	INIT_LIST_HEAD(&vaccel_file->sessions);
	vaccel_file->vaccel = vaccel;
	filp->private_data = vaccel_file;

	return nonseekable_open(inode, filp);
}

static int accel_dev_release(struct inode *inode, struct file *filp)
{
	struct virtio_accel_file *vaccel_file = filp->private_data;

	kfree(vaccel_file);
	return 0;
}

static const struct file_operations accel_dev_fops = {
	.owner = THIS_MODULE,
	.open = accel_dev_open,
	.release = accel_dev_release,
	.unlocked_ioctl = accel_dev_ioctl,
};

static struct miscdevice accel_dev = {
	.minor = MISC_DYNAMIC_MINOR,
	.name = "accel",
	.fops = &accel_dev_fops,
	.mode = S_IRUSR|S_IWUSR|S_IRGRP|S_IWGRP|S_IROTH|S_IWOTH,
};

int accel_dev_init(void)
{
	int ret;

	pr_debug("Initializing character device...\n");
	ret = misc_register(&accel_dev);
	if (unlikely(ret)) {
		pr_err("registration of /dev/accel failed\n");
		return ret;
	}

	return 0;
}

void accel_dev_destroy(void)
{
	misc_deregister(&accel_dev);
}
