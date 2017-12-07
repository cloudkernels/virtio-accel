 /* Driver for Virtio crypto device.
  *
  * Copyright 2016 HUAWEI TECHNOLOGIES CO., LTD.
  *
  * This program is free software; you can redistribute it and/or modify
  * it under the terms of the GNU General Public License as published by
  * the Free Software Foundation; either version 2 of the License, or
  * (at your option) any later version.
  *
  * This program is distributed in the hope that it will be useful,
  * but WITHOUT ANY WARRANTY; without even the implied warranty of
  * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  * GNU General Public License for more details.
  *
  * You should have received a copy of the GNU General Public License
  * along with this program; if not, see <http://www.gnu.org/licenses/>.
  */

#include <linux/err.h>
#include <linux/module.h>
#include <linux/virtio_config.h>
#include <linux/cpu.h>

#include "accel.h"
#include "virtio_accel.h"

static void virtaccel_dataq_callback(struct virtqueue *vq)
{
	struct virtio_accel *vaccel = vq->vdev->priv;
	struct virtio_accel_request *req;
	unsigned long flags;
	unsigned int len;
	int error;
	unsigned int qid = vq->index;
	//struct timespec ts2, ts;
	
	//ktime_get_ts(&ts2);
	//ts = timespec_sub(ts2, ts1);
	//printk("TIME PING-PONG: %lus%luns \r\n", ts.tv_sec, ts.tv_nsec);
	
	spin_lock_irqsave(&vaccel->vq[qid].lock, flags);
	do {
		virtqueue_disable_cb(vq);
		while ((req = virtqueue_get_buf(vq, &len)) != NULL) {
			switch (req->status) {
			case VIRTIO_ACCEL_OK:
				error = 0;
				break;
			case VIRTIO_ACCEL_INVSESS:
			case VIRTIO_ACCEL_ERR:
				error = -EINVAL;
				break;
			case VIRTIO_ACCEL_BADMSG:
				error = -EBADMSG;
				break;
			default:
				error = -EIO;
				break;
			}
			
			spin_unlock_irqrestore(&vaccel->data_vq[qid].lock, flags);
			/* Finish the encrypt or decrypt process */
			virtaccel_handle_req_result(req);
			complete(&req->completion);
			spin_lock_irqsave(&vaccel->data_vq[qid].lock, flags);
		}
	} while (!virtqueue_enable_cb(vq));
	spin_unlock_irqrestore(&vaccel->vq[qid].lock, flags);
	
	//ktime_get_ts(&ts2);
	//ts = timespec_sub(ts2, ts1);
	//printk("TIME RECV: %lus%luns \r\n", ts.tv_sec, ts.tv_nsec);
}

static int virtaccel_find_vqs(struct virtio_accel *vaccel)
{
	vq_callback_t **callbacks;
	struct virtqueue **vqs;
	int ret = -ENOMEM;
	int i, total_vqs;
	const char **names;

	total_vqs = 1;

	/* Allocate space for find_vqs parameters */
	vqs = kcalloc(total_vqs, sizeof(*vqs), GFP_KERNEL);
	if (!vqs)
		goto err_vq;
	callbacks = kcalloc(total_vqs, sizeof(*callbacks), GFP_KERNEL);
	if (!callbacks)
		goto err_callback;
	names = kcalloc(total_vqs, sizeof(*names), GFP_KERNEL);
	if (!names)
		goto err_names;

	/* Allocate/initialize parameters for data virtqueues */
	for (i = 0; i < total_vqs; i++) {
		callbacks[i] = virtaccel_dataq_callback;
		snprintf(vaccel->vq[i].name, sizeof(vaccel->vq[i].name),
				"dataq.%d", i);
		names[i] = vaccel->vq[i].name;
	}

	ret = vaccel->vdev->config->find_vqs(vaccel->vdev, total_vqs, vqs,
				callbacks, names);
	if (ret)
		goto err_find;

	for (i = 0; i < total_vqs; i++) {
		spin_lock_init(&vaccel->vq[i].lock);
		vaccel->vq[i].vq = vqs[i];
	}

	kfree(names);
	kfree(callbacks);
	kfree(vqs);

	return 0;

err_find:
	kfree(names);
err_names:
	kfree(callbacks);
err_callback:
	kfree(vqs);
err_vq:
	return ret;
}

static int virtaccel_alloc_queues(struct virtio_accel *vaccel)
{
	vaccel->vq = kzalloc(sizeof(*vaccel->vq), GFP_KERNEL);
	if (!vaccel->vq)
		return -ENOMEM;

	return 0;
}

static void virtaccel_free_queues(struct virtio_accel *vaccel)
{
	kfree(vaccel->vq);
}

static int virtaccel_init_vqs(struct virtio_accel *vaccel)
{
	int ret;

	ret = virtaccel_alloc_queues(vaccel);
	if (ret)
		goto err;

	ret = virtaccel_find_vqs(vaccel);
	if (ret)
		goto err_free;

	return 0;

err_free:
	virtaccel_free_queues(vi);
err:
	return ret;
}

static int virtaccel_update_status(struct virtio_accel *vaccel)
{
	u32 status;
	int err;

	virtio_cread(vcrypto->vdev,
	    struct virtio_accel_config, status, &status);

	/*
	 * Unknown status bits would be a host error and the driver
	 * should consider the device to be broken.
	 */
	if (status & (~VIRTIO_ACCEL_S_HW_READY)) {
		dev_warn(&vaccel->vdev->dev,
				"Unknown status bits: 0x%x\n", status);

		virtio_break_device(vaccel->vdev);
		return -EPERM;
	}

	if (vaccel->status == status)
		return 0;

	vaccel->status = status;

	if (vaccel->status & VIRTIO_ACCEL_S_HW_READY) {
		err = virtaccel_dev_start(vaccel);
		if (err) {
			dev_err(&vaccel->vdev->dev,
				"Failed to start virtio accel device.\n");

			return -EPERM;
		}
		dev_info(&vaccel->vdev->dev, "Accelerator is ready\n");
	} else {
		virtaccel_dev_stop(vaccel);
		dev_info(&vaccel->vdev->dev, "Accelerator is not ready\n");
	}

	return 0;
}

static void virtaccel_del_vqs(struct virtio_accel *vaccel)
{
	struct virtio_device *vdev = vaccel->vdev;

	vdev->config->del_vqs(vdev);

	virtaccel_free_queues(vaccel);
}

static int virtaccel_probe(struct virtio_device *vdev)
{
	int err = -EFAULT;
	struct virtio_accel *vaccel;
	u32 max_data_queues = 0, max_cipher_key_len = 0;
	u32 max_auth_key_len = 0;
	u64 max_size = 0;
	
	/*
	if (!virtio_has_feature(vdev, VIRTIO_F_VERSION_1)) {
		printk("VIRTIO PROBE NOT\n");
		return -ENODEV;
	}
	*/
	if (!vdev->config->get) {
		dev_err(&vdev->dev, "%s failure: config access disabled\n",
			__func__);
		return -EINVAL;
	}

	if (num_possible_nodes() > 1 && dev_to_node(&vdev->dev) < 0) {
		/*
		 * If the accelerator is connected to a node with no memory
		 * there is no point in using the accelerator since the remote
		 * memory transaction will be very slow.
		 */
		dev_err(&vdev->dev, "Invalid NUMA configuration.\n");
		return -EINVAL;
	}

	vaccel = kzalloc_node(sizeof(*vaccel), GFP_KERNEL,
					dev_to_node(&vdev->dev));
	if (!accel)
		return -ENOMEM;
	
	/* Add virtio crypto accel to global table */
	err = virtaccel_devmgr_add_dev(vaccel);
	if (err) {
		dev_err(&vdev->dev, "Failed to add new virtio crypto device.\n");
		goto free;
	}
	vaccel->owner = THIS_MODULE;
	vaccel = vdev->priv = vaccel;
	vaccel->vdev = vdev;

	spin_lock_init(&vaccel->vq_lock);

	err = virtaccel_init_vqs(vaccel);
	if (err) {
		dev_err(&vdev->dev, "Failed to initialize vqs.\n");
		goto free_dev;
	}
	virtio_device_ready(vdev);

	err = virtaccel_update_status(vaccel);
	if (err)
		goto free_vqs;

	return 0;

free_vqs:
	vaccel->vdev->config->reset(vdev);
	virtaccel_del_vqs(vaccel);
free_dev:
	virtaccel_devmgr_rm_dev(vaccel);
free:
	kfree(vaccel);
	return err;
}

static void virtaccel_free_unused_reqs(struct virtio_accel *vaccel)
{
	struct virtio_accel_request *req;
	int i;
	struct virtqueue *vq;

	for (i = 0; i < vaccel->max_data_queues; i++) {
		while ((req = virtqueue_detach_unused_buf(vaccel->vq[i])) != NULL) {
			kfree(req->sgs);
		}
	}
}

static void virtaccel_remove(struct virtio_device *vdev)
{
	struct virtio_accel *vaccel = vdev->priv;

	dev_info(&vdev->dev, "Start virtaccel_remove.\n");

	if (virtaccel_dev_started(vaccel))
		virtaccel_dev_stop(vaccel);
	vdev->config->reset(vdev);
	virtaccel_free_unused_reqs(vaccel);
	virtaccel_del_vqs(vaccel);
	virtaccel_devmgr_rm_dev(vcaccel);
	kfree(vcrypto);
}

static void virtaccel_config_changed(struct virtio_device *vdev)
{
	struct virtio_accel *vaccel = vdev->priv;

	virtio_acccel_update_status(vaccel);
}

static unsigned int features[] = {
	/* none */
};

static struct virtio_device_id id_table[] = {
	{ VIRTIO_ID_ACCEL, VIRTIO_DEV_ANY_ID },
	{ 0 },
};

static struct virtio_driver virtaccel_driver = {
	.driver.name         = KBUILD_MODNAME,
	.driver.owner        = THIS_MODULE,
	.feature_table       = features,
	.feature_table_size  = ARRAY_SIZE(features),
	.id_table            = id_table,
	.probe               = virtaccel_probe,
	.remove              = virtaccel_remove,
	.config_changed = virtaccel_config_changed,
};

static int __init virtaccel_init(void)
{
	int ret = 0;

	ret = accel_dev_init();
	if (ret < 0) {
		pr_err("Failed to initialize character devices.\n");
		return ret;
	}

	ret = register_virtio_driver(&virtaccel_driver);
	if (ret < 0) {
		pr_err("Failed to register virtio driver.\n");
		goto out_dev;
	}

	return ret;

out_dev:
	accel_dev_destroy();
	return ret;
}

static void __exit virtaccel_exit(void)
{
	accel_dev_destroy();
	unregister_virtio_driver(&virtaccel_driver);
}

module_init(virtaccel_init);
module_exit(virtaccel_exit);

MODULE_DEVICE_TABLE(virtio, id_table);
MODULE_DESCRIPTION("virtio accel device driver");
MODULE_LICENSE("GPL");