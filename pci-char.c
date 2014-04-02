/*
 * ==========================================================
 *
 * A generic driver for reading and writing PCI(e) BARs via character device files
 * Copyright (C) 2012-2014  Andre Richter
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
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 * ==========================================================
 *
 * The driver is basically a crossover of Linux' msr.c and pci-stub.c
 * and borrows some code from them.
 *
 * You can dynamically add PCI(e) devices like it is done for pci-stub, e.g.:
 *
 * echo "10ee 7014"  > /sys/bus/pci/drivers/pci_char/new_id 
 * echo 0000:20:00.0 > /sys/bus/pci/devices/0000\:20\:00.0/driver/unbind
 * echo 0000:20:00.0 > /sys/bus/pci/drivers/pci_char/bind
 *
 * Or via parameter at module probing, e.g.:
 *
 * insmod pci-char ids=10ee:7014
 *
 * The driver creates a character device file for _each_
 * _memory_ BAR it finds on the PCI(e) device, e.g.:
 *
 * /dev/pci-char/01:00.01/bar0
 * /dev/pci-char/01:00.01/bar3
 *
 * You can read from and write to these files in 32bit
 * chunks, aka 4byte aligned. Accessing memory addresses
 * within the bar is realized by setting an offset into
 * the file via the (l)lseek() system call.
 *
 * ==========================================================
 *
 * Author(s):
 *    Andre Richter, andre.o.richter @t gmail_com
 */

#include <linux/module.h>
#include <linux/pci.h>
#include <linux/fs.h>
#include <linux/types.h>
#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/kernel.h>
#include <linux/uaccess.h>

static char ids[1024] __initdata;

module_param_string(ids, ids, sizeof(ids), 0);
MODULE_PARM_DESC(ids, "Initial PCI IDs to add to the stub driver, format is "
                 "\"vendor:device[:subvendor[:subdevice[:class[:class_mask]]]]\""
		 " and multiple comma separated entries can be specified");

/* Base Address register */
struct bar_t {
	resource_size_t len;
	void __iomem *addr;
};

/* Private structure */
struct pci_char {
	struct bar_t bar[6];
	dev_t major;
	struct cdev cdev;
};

static struct class *pchar_class;

static int dev_open(struct inode *inode, struct file *file)
{
	unsigned int num = iminor(file->f_path.dentry->d_inode);
	struct pci_char *pchar = container_of(inode->i_cdev, struct pci_char,
					      cdev);

	if (num > 5)
		return -ENXIO;

	if (pchar->bar[num].len == 0)
		return -EIO; /* BAR not in use or not memory type */

	file->private_data = pchar;

	return 0;
};

static loff_t dev_seek(struct file *file, loff_t offset, int whence)
{
	struct inode *inode = file->f_mapping->host;
	unsigned int num = iminor(inode);
	struct pci_char *pchar = file->private_data;
	loff_t new_pos;

	mutex_lock(&inode->i_mutex);
	switch (whence) {
	case SEEK_SET: /* SEEK_SET = 0 */
		new_pos = offset;
		break;
	case SEEK_CUR: /* SEEK_CUR = 1 */
		new_pos = file->f_pos + offset;
		break;
	default:
		new_pos = -EINVAL;
	}
	mutex_unlock(&inode->i_mutex);

	if (new_pos % 4)
		return -EINVAL; /* Only allow 4 byte alignment */

	if ((new_pos < 0) || (new_pos > pchar->bar[num].len - 4))
		return -EINVAL;

	file->f_pos = new_pos;
	return file->f_pos;
}

static ssize_t dev_read(struct file *file, char __user *buf,
			size_t count, loff_t *ppos)
{
	struct pci_char *pchar = file->private_data;
	u32 __user *tmp = (u32 __user *) buf;
	u32 data;
	u32 offset = *ppos;
	unsigned int num = iminor(file->f_path.dentry->d_inode);
	int err = 0;
	ssize_t bytes = 0;

	if (count % 4)
		return -EINVAL; /* Only allow 32 bit reads */

	for (; count; count -= 4) {
		data = readl(pchar->bar[num].addr + offset);
		if (copy_to_user(tmp, &data, 4)) {
			err = -EFAULT;
			break;
		}
		tmp += 1;
		bytes += 4;
	}

	return bytes ? bytes : err;
};

static ssize_t dev_write(struct file *file, const char __user *buf,
			 size_t count, loff_t *ppos)
{
	struct pci_char *pchar = file->private_data;
	const u32 __user *tmp = (const u32 __user *)buf;
	u32 data;
	u32 offset = *ppos;
	unsigned int num = iminor(file->f_path.dentry->d_inode);
	int err = 0;
	ssize_t bytes = 0;

	if (count % 4)
		return -EINVAL; /* Only allow 32 bit writes */

	for (; count; count -= 4) {
		if (copy_from_user(&data, tmp, 4)) {
			err = -EFAULT;
			break;
		}
		writel(data, pchar->bar[num].addr + offset);
		tmp += 1;
		bytes += 4;
	}

	return bytes ? bytes : err;
};

static const struct file_operations fops = {
	.owner	 = THIS_MODULE,
	.llseek  = dev_seek,
	.open	 = dev_open,
	.read	 = dev_read,
	.write	 = dev_write,
};

static int pci_probe(struct pci_dev *pdev, const struct pci_device_id *id)
{
	int err = 0, i;
	int mem_bars;
	struct pci_char *pchar;
	struct device *dev;
	dev_t dev_num;

	pchar = kmalloc(sizeof(struct pci_char), GFP_KERNEL);
	if (!pchar) {
		err = -ENOMEM;
		goto failure_kmalloc;
	}

	err = pci_enable_device_mem(pdev);
	if (err)
		goto failure_pci_enable;

	/* Request only the BARs that contain memory regions */
	mem_bars = pci_select_bars(pdev, IORESOURCE_MEM);
	err = pci_request_selected_regions(pdev, mem_bars, "pci-char");
	if (err)
		goto failure_pci_regions;

	/* Memory Map BARs for MMIO */
	for (i = 0; i < 6; i++) {
		if (mem_bars & (1 << i)) {
			pchar->bar[i].addr = ioremap(pci_resource_start(pdev, i),
						     pci_resource_len(pdev, i));
			if (IS_ERR(pchar->bar[i].addr)) {
				err = PTR_ERR(pchar->bar[i].addr);
				break;
			} else
				pchar->bar[i].len = pci_resource_len(pdev, i);
		} else {
			pchar->bar[i].addr = NULL;
			pchar->bar[i].len = 0;
		}
	}

	if (err) {
		for (i--; i >= 0; i--)
			if (pchar->bar[i].len)
				iounmap(pchar->bar[i].addr);
		goto failure_ioremap;
	}

	/* Get device number range */
	err = alloc_chrdev_region(&dev_num, 0, 6, "pci-char");
	if (err)
		goto failure_alloc_chrdev_region;

	pchar->major = MAJOR(dev_num);

	/* connect cdev with file operations */
	cdev_init(&pchar->cdev, &fops);
	pchar->cdev.owner = THIS_MODULE;

	/* add major/min range to cdev */
	err = cdev_add(&pchar->cdev, MKDEV(pchar->major, 0), 6);
	if (err)
		goto failure_cdev_add;

	/* create /dev/ nodes via udev */
	for (i = 0; i < 6; i++) {
		if (pchar->bar[i].len) {
			dev = device_create(pchar_class, &pdev->dev,
					    MKDEV(pchar->major, i),
					    NULL, "b%xd%xf%x_bar%d",
					    pdev->bus->number,
					    PCI_SLOT(pdev->devfn),
					    PCI_FUNC(pdev->devfn), i);
			if (IS_ERR(dev)) {
				err = PTR_ERR(dev);
				break;
			}
		}
	}

	if (err) {
		for (i--; i >= 0; i--)
			if (pchar->bar[i].len)
				device_destroy(pchar_class,
					       MKDEV(pchar->major, i));
		goto failure_device_create;
	}

	pci_set_drvdata(pdev, pchar);
	dev_info(&pdev->dev, "claimed by pci-char\n");

	return 0;

failure_device_create:
	cdev_del(&pchar->cdev);

failure_cdev_add:
	unregister_chrdev_region(MKDEV(pchar->major, 0), 6);

failure_alloc_chrdev_region:
	for (i = 0; i < 6; i++)
		if (pchar->bar[i].len)
			iounmap(pchar->bar[i].addr);

failure_ioremap:
	pci_release_selected_regions(pdev,
				     pci_select_bars(pdev, IORESOURCE_MEM));

failure_pci_regions:
	pci_disable_device(pdev);

failure_pci_enable:
	kfree(pchar);

failure_kmalloc:
	return err;
}

static void pci_remove(struct pci_dev *pdev)
{
	int i;
	struct pci_char *pchar = pci_get_drvdata(pdev);

	for (i = 0; i < 6; i++)
		if (pchar->bar[i].len)
			device_destroy(pchar_class,
				       MKDEV(pchar->major, i));

	cdev_del(&pchar->cdev);

	unregister_chrdev_region(MKDEV(pchar->major, 0), 6);

	for (i = 0; i < 6; i++)
		if (pchar->bar[i].len)
			iounmap(pchar->bar[i].addr);

	pci_release_selected_regions(pdev,
				     pci_select_bars(pdev, IORESOURCE_MEM));
	pci_disable_device(pdev);
	kfree(pchar);
}

static struct pci_driver pchar_driver = {
	.name		= "pci-char",
	.id_table	= NULL,	/* only dynamic id's */
	.probe		= pci_probe,
	.remove         = pci_remove,
};

static char *pci_char_devnode(struct device *dev, umode_t *mode)
{
	struct pci_dev *pdev = to_pci_dev(dev->parent);
	return kasprintf(GFP_KERNEL, "pci-char/%02x:%02x.%02x/bar%d",
			 pdev->bus->number,
			 PCI_SLOT(pdev->devfn),
			 PCI_FUNC(pdev->devfn),
			 MINOR(dev->devt));
}

static int __init pci_init(void)
{
	int err;
	char *p, *id;

	pchar_class = class_create(THIS_MODULE, "pci-char");
	if (IS_ERR(pchar_class)) {
		err = PTR_ERR(pchar_class);
		return err;
	}
	pchar_class->devnode = pci_char_devnode;

	err = pci_register_driver(&pchar_driver);
	if (err)
		goto failure_register_driver;

	/* no ids passed actually */
	if (ids[0] == '\0')
		return 0;
 
	/* add ids specified in the module parameter */
	p = ids;
	while ((id = strsep(&p, ","))) {
		unsigned int vendor, device, subvendor = PCI_ANY_ID,
			subdevice = PCI_ANY_ID, class=0, class_mask=0;
		int fields;
 
		if (!strlen(id))
			continue;
 
		fields = sscanf(id, "%x:%x:%x:%x:%x:%x",
				&vendor, &device, &subvendor, &subdevice,
				&class, &class_mask);
 
		if (fields < 2) {
			pr_warn("pci-char: invalid id string \"%s\"\n", id);
			continue;
		}

		pr_info("pci-char: add %04X:%04X sub=%04X:%04X cls=%08X/%08X\n",
			vendor, device, subvendor, subdevice, class, class_mask);
 
		err = pci_add_dynid(&pchar_driver, vendor, device,
				   subvendor, subdevice, class, class_mask, 0);
		if (err)
			pr_warn("pci-char: failed to add dynamic id (%d)\n", err);
	}

	return 0;

failure_register_driver:
	class_destroy(pchar_class);

	return err;	
}

static void __exit pci_exit(void)
{
	pci_unregister_driver(&pchar_driver);
	class_destroy(pchar_class);
}

module_init(pci_init);
module_exit(pci_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("generic pci to chardev driver");
MODULE_AUTHOR("Andre Richter <andre.o.richter @t gmail_com>");

