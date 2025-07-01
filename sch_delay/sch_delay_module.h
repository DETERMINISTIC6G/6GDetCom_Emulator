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

#ifndef SCH_DELAY_MODULE_H
#define SCH_DELAY_MODULE_H

#include <linux/pkt_sched.h>
#include <linux/skbuff.h>
#include <linux/kfifo.h>
#include <linux/kdev_t.h>
#include <linux/cdev.h>
#include <linux/rbtree.h>
#include <net/pkt_sched.h>

#define TRUE 1
#define FALSE 0

#define MAX_DEVNAME_LEN 50

signed long long get_next_delay(struct kfifo *delay_queue);

/*
    Private data of the qdisc, holds instace-specific information.
*/
struct delay_qdisc_data {
	struct qdisc_watchdog watchdog;

    struct rb_root package_queue_root;

    struct kfifo delay_queue;

    bool reorder_enabled;
    char net_dev_name[50];

    // Chardev
    int chr_dev_majour_num;
    char chr_dev_name[MAX_DEVNAME_LEN];
    struct class *dev_class;
    int chr_dev_open_count;


    // Needs to be last element for containe_of!
    struct cdev c_dev; 
};

/*
    Config Structure for communication with "tc".
*/
struct tc_delay_qopt {
	bool	 reorder;
    __u32      limit;
};

/*
    Data inside SKBs.
*/
struct delay_skb_cb {
    signed long long earliest_send;
};

#endif
