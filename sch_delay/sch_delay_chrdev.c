/**
 * Copyright 2024 The NetworkDelayEmulator authors as listed in file AUTHORS.
 *
 * Copyright 2024 Lorenz Grohmann (st161568@stud.uni-stuttgart.de)
 *
 * This file is part of NetworkDelayEmulator.
 *
 * NetworkDelayEmulator is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 * 
 * NetworkDelayEmulator is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with NetworkDelayEmulator. If not, see <http://www.gnu.org/licenses/>.
 */

#include "sch_delay_chrdev.h"
#include "sch_delay_module.h"

#include <linux/init.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/skbuff.h>
#include <linux/fs.h>
#include <linux/device.h>
#include <linux/kdev_t.h>
#include <linux/err.h>
#include <linux/cdev.h>
#include <linux/kfifo.h>
#include <linux/delay.h>
#include <linux/ktime.h>
#include <linux/string.h>


/*
    Called when a proccess reads from the Device.
    Returns the available space in the internal delay queue as an 8 byte unsigned long
*/
ssize_t sch_delay_device_read(struct file *file, char *buffer, size_t len, loff_t *offset) {
    struct delay_qdisc_data *qdisc_data = file->private_data;

    unsigned long data = kfifo_avail(&qdisc_data->delay_queue)/8;
    put_user((data >> (8*0)) & 0xff, buffer++);
    put_user((data >> (8*1)) & 0xff, buffer++);
    put_user((data >> (8*2)) & 0xff, buffer++);
    put_user((data >> (8*3)) & 0xff, buffer++);
    *offset += 4;
    return 4;
}


/*
    Called when a process is writing to the Device
    Interprets all data written as list of unsigned longs and stores them in the internal delay queue.
*/
ssize_t sch_delay_device_write(struct file *file, const char *buffer, size_t len, loff_t *offset) {
    struct delay_qdisc_data *qdisc_data = file->private_data;
    int i;
    int ret;
	unsigned int copied;

    for (i = 0; i < len; i=i+8) {
        ret = kfifo_from_user(&qdisc_data->delay_queue, buffer+i, 8, &copied);
    }

    return len;
}


/* 
    Called when a process opens the Device.
    Checks if Device is already open, if not sets nescessary private data.
*/
int sch_delay_device_open(struct inode *inode, struct file *file) {
	struct delay_qdisc_data *qdisc_data = container_of(inode->i_cdev,
						struct delay_qdisc_data, c_dev);

    file->private_data = qdisc_data;

    if (qdisc_data->chr_dev_open_count) {
        return -EBUSY;
    }
    qdisc_data->chr_dev_open_count++;
    try_module_get(THIS_MODULE);
    printk(KERN_INFO "sch_delay %s: dev file opened\n", qdisc_data->net_dev_name);

    return 0;
}


/*
    Called when a process closes the Device.
    Cleans up file data.
*/
int sch_delay_device_release(struct inode *inode, struct file *file) {
	struct delay_qdisc_data *qdisc_data = container_of(inode->i_cdev,
						struct delay_qdisc_data, c_dev);

    file->private_data = NULL;
    qdisc_data->chr_dev_open_count--;

    module_put(THIS_MODULE);
    printk(KERN_INFO "sch_delay %s: dev file released\n", qdisc_data->net_dev_name);

    return 0;
}
