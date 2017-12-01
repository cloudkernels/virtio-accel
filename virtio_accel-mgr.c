 /* Management for virtio crypto devices (refer to adf_dev_mgr.c)
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

#include <linux/mutex.h>
#include <linux/list.h>
#include <linux/module.h>

#include "virtio_accel.h"
#include "virtio_accel_common.h"

static LIST_HEAD(virtio_accel_table);
static uint32_t num_devices;

/* The table_lock protects the above global list and num_devices */
static DEFINE_MUTEX(table_lock);

#define VIRTIO_ACCEL_MAX_DEVICES 32


/*
 * virtio_accel_devmgr_add_dev() - Add vaccel_dev to the acceleration
 * framework.
 * @vaccel_dev:  Pointer to virtio accel device.
 *
 * Function adds virtio accel device to the global list.
 * To be used by virtio accel device specific drivers.
 *
 * Return: 0 on success, error code othewise.
 */
int virtio_accel_devmgr_add_dev(struct virtio_accel *vaccel_dev)
{
	struct list_head *itr;

	mutex_lock(&table_lock);
	if (num_devices == VIRTIO_ACCEL_MAX_DEVICES) {
		pr_info("virtio_accel: only support up to %d devices\n",
			    VIRTIO_ACCEL_MAX_DEVICES);
		mutex_unlock(&table_lock);
		return -EFAULT;
	}

	list_for_each(itr, &virtio_accel_table) {
		struct virtio_accel *ptr =
				list_entry(itr, struct virtio_accel, list);

		if (ptr == vaccel_dev) {
			mutex_unlock(&table_lock);
			return -EEXIST;
		}
	}
	atomic_set(&vaccel_dev->ref_count, 0);
	list_add_tail(&vaccel_dev->list, &virtio_accel_table);
	vaccel_dev->dev_id = num_devices++;
	mutex_unlock(&table_lock);
	return 0;
}

struct list_head *virtio_accel_devmgr_get_head(void)
{
	return &virtio_accel_table;
}

/*
 * virtio_accel_devmgr_rm_dev() - Remove vaccel_dev from the acceleration
 * framework.
 * @vaccel_dev:  Pointer to virtio crypto device.
 *
 * Function removes virtio crypto device from the acceleration framework.
 * To be used by virtio crypto device specific drivers.
 *
 * Return: void
 */
void virtio_accel_devmgr_rm_dev(struct virtio_accel *vaccel_dev)
{
	mutex_lock(&table_lock);
	list_del(&vaccel_dev->list);
	num_devices--;
	mutex_unlock(&table_lock);
}

/*
 * virtio_accel_devmgr_get_first()
 *
 * Function returns the first virtio crypto device from the acceleration
 * framework.
 *
 * To be used by virtio crypto device specific drivers.
 *
 * Return: pointer to vaccel_dev or NULL if not found.
 */
struct virtio_accel *virtio_accel_devmgr_get_first(void)
{
	struct virtio_accel *dev = NULL;

	mutex_lock(&table_lock);
	if (!list_empty(&virtio_accel_table))
		dev = list_first_entry(&virtio_accel_table,
					struct virtio_accel,
				    list);
	mutex_unlock(&table_lock);
	return dev;
}

/*
 * virtio_accel_dev_in_use() - Check whether vaccel_dev is currently in use
 * @vaccel_dev: Pointer to virtio crypto device.
 *
 * To be used by virtio crypto device specific drivers.
 *
 * Return: 1 when device is in use, 0 otherwise.
 */
int virtio_accel_dev_in_use(struct virtio_accel *vaccel_dev)
{
	return atomic_read(&vaccel_dev->ref_count) != 0;
}

/*
 * virtio_accel_dev_get() - Increment vaccel_dev reference count
 * @vaccel_dev: Pointer to virtio crypto device.
 *
 * Increment the vaccel_dev refcount and if this is the first time
 * incrementing it during this period the vaccel_dev is in use,
 * increment the module refcount too.
 * To be used by virtio crypto device specific drivers.
 *
 * Return: 0 when successful, EFAULT when fail to bump module refcount
 */
int virtio_accel_dev_get(struct virtio_accel *vaccel_dev)
{
	if (atomic_add_return(1, &vaccel_dev->ref_count) == 1)
		if (!try_module_get(vaccel_dev->owner))
			return -EFAULT;
	return 0;
}

/*
 * virtio_accel_dev_put() - Decrement vaccel_dev reference count
 * @vaccel_dev: Pointer to virtio crypto device.
 *
 * Decrement the vaccel_dev refcount and if this is the last time
 * decrementing it during this period the vaccel_dev is in use,
 * decrement the module refcount too.
 * To be used by virtio crypto device specific drivers.
 *
 * Return: void
 */
void virtio_accel_dev_put(struct virtio_accel *vaccel_dev)
{
	if (atomic_sub_return(1, &vaccel_dev->ref_count) == 0)
		module_put(vaccel_dev->owner);
}

/*
 * virtio_accel_dev_started() - Check whether device has started
 * @vaccel_dev: Pointer to virtio crypto device.
 *
 * To be used by virtio crypto device specific drivers.
 *
 * Return: 1 when the device has started, 0 otherwise
 */
int virtio_accel_dev_started(struct virtio_accel *vaccel_dev)
{
	return (vaccel_dev->status & VIRTIO_CRYPTO_S_HW_READY);
}

/*
 * virtio_accel_get_dev_node() - Get vaccel_dev on the node.
 * @node:  Node id the driver works.
 *
 * Function returns the virtio crypto device used fewest on the node.
 *
 * To be used by virtio crypto device specific drivers.
 *
 * Return: pointer to vaccel_dev or NULL if not found.
 */
struct virtio_accel *virtio_accel_get_dev_node(int node)
{
	struct virtio_accel *vaccel_dev = NULL, *tmp_dev;
	unsigned long best = ~0;
	unsigned long ctr;

	mutex_lock(&table_lock);
	list_for_each_entry(tmp_dev, virtio_accel_devmgr_get_head(), list) {

		if ((node == dev_to_node(&tmp_dev->vdev->dev) ||
		     dev_to_node(&tmp_dev->vdev->dev) < 0) &&
		    virtio_accel_dev_started(tmp_dev)) {
			ctr = atomic_read(&tmp_dev->ref_count);
			if (best > ctr) {
				vaccel_dev = tmp_dev;
				best = ctr;
			}
		}
	}

	if (!vaccel_dev) {
		pr_info("virtio_accel: Could not find a device on node %d\n",
				node);
		/* Get any started device */
		list_for_each_entry(tmp_dev,
				virtio_accel_devmgr_get_head(), list) {
			if (virtio_accel_dev_started(tmp_dev)) {
				vaccel_dev = tmp_dev;
				break;
			}
		}
	}
	mutex_unlock(&table_lock);
	if (!vaccel_dev)
		return NULL;

	virtio_accel_dev_get(vaccel_dev);
	return vaccel_dev;
}

/*
 * virtio_accel_dev_start() - Start virtio crypto device
 * @vaccel:    Pointer to virtio crypto device.
 *
 * Function notifies all the registered services that the virtio crypto device
 * is ready to be used.
 * To be used by virtio crypto device specific drivers.
 *
 * Return: 0 on success, EFAULT when fail to register algorithms
 */
int virtio_accel_dev_start(struct virtio_accel *vaccel)
{
	if (virtio_accel_algs_register()) {
		pr_err("virtio_accel: Failed to register crypto algs\n");
		return -EFAULT;
	}

	return 0;
}

/*
 * virtio_accel_dev_stop() - Stop virtio crypto device
 * @vaccel:    Pointer to virtio crypto device.
 *
 * Function notifies all the registered services that the virtio crypto device
 * is ready to be used.
 * To be used by virtio crypto device specific drivers.
 *
 * Return: void
 */
void virtio_accel_dev_stop(struct virtio_accel *vaccel)
{
	virtio_accel_algs_unregister();
}