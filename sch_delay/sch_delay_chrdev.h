/**
 * Copyright 2024 The NetworkDelayEmulator authors as listed in file AUTHORS.
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

#ifndef SCH_DELAY_CHRDEV_H
#define SCH_DELAY_CHRDEV_H

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


ssize_t sch_delay_device_read(struct file *file, char *buffer, size_t len, loff_t *offset);

ssize_t sch_delay_device_write(struct file *file, const char *buffer, size_t len, loff_t *offset);

int sch_delay_device_open(struct inode *inode, struct file *file);

int sch_delay_device_release(struct inode *inode, struct file *file);

#endif
