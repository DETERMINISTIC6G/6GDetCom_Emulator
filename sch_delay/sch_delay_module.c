/**
 * This file contains the core module functionality and all Qdisc functions.
 *
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

#include "sch_delay_module.h"
#include "sch_delay_chrdev.h"

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
#include <linux/rbtree.h>
#include <linux/version.h>

#include <net/netlink.h>
#include <net/pkt_sched.h>
#include <net/inet_ecn.h>

#define VERSION "0.1"

#define DEVICE_PREFIX "sch_delay/"
#define DELAY_FIFO_ELEMENTS 512000
#define DELAY_FIFO_SIZE DELAY_FIFO_ELEMENTS*sizeof(signed long long)

#define DEFAULT_REORDERING true
#define DEFAULT_LIMIT 1000

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Lorenz Grohmann");
MODULE_DESCRIPTION("Delay Qdsic");
MODULE_VERSION(VERSION);

/* Get the next next delay from the internal queue. Returns NULL if queue is empty. */
__always_inline signed long long get_next_delay(struct kfifo *delay_queue) {
    int ret;
    if(likely(!kfifo_is_empty(delay_queue))){
        unsigned char data_buffer[8];
        signed long long delay;
        ret = kfifo_out(delay_queue, data_buffer, 8);
        delay = *(signed long long *) data_buffer;
        return ktime_get_ns() + delay;
    }

    return ktime_get_ns();
}


/*
    Get Pointer to SKB internal data as delay_skb_cb.
    We Use this to store the earliest send time.
*/
static __always_inline struct delay_skb_cb *delay_skb_cb(struct sk_buff *skb) {
	qdisc_cb_private_validate(skb, sizeof(struct delay_skb_cb));
	return (struct delay_skb_cb *)qdisc_skb_cb(skb)->data;    
}


/* 
    Insert a new package into the internal queu (Without Reordering).
    Is called by the Kernel when packages arrive.
*/
static __always_inline int delay_enqueue_fifo(struct sk_buff *skb, struct Qdisc *sch, struct sk_buff **to_free) {
    struct delay_qdisc_data *qdisc_data = qdisc_priv(sch);

    if(likely(sch->q.qlen < sch->limit)) {
        struct delay_skb_cb *cb;
        signed long long delay = get_next_delay(&qdisc_data->delay_queue);
        cb = delay_skb_cb(skb);

        cb->earliest_send = delay;

        return qdisc_enqueue_tail(skb, sch);

    } else {
        printk(KERN_ALERT "sch_delay %s: package queue ist full! Dropping Package\n", qdisc_data->net_dev_name);
        qdisc_drop(skb, sch, to_free);
        return NET_XMIT_DROP;
    }
}


/* 
    Insert a new package into the internal queu (With Reordering).
    Is called by the Kernel when packages arrive.
*/
static __always_inline int delay_enqueue_reorder(struct sk_buff *skb, struct Qdisc *sch, struct  sk_buff **to_free) {
    struct delay_qdisc_data *qdisc_data = qdisc_priv(sch);

    if(likely(sch->q.qlen < sch->limit)) {
        struct delay_skb_cb *cb;
        signed long long delay;
        struct rb_node **node;
        struct rb_node *parent;

        cb = delay_skb_cb(skb);
        delay = get_next_delay(&qdisc_data->delay_queue);

        skb_orphan_partial(skb);
        cb->earliest_send = delay;

	    qdisc_qstats_backlog_inc(sch, skb);
        node = &qdisc_data->package_queue_root.rb_node;
        parent = NULL;

        while (*node) {
            struct sk_buff *skb2;
            parent = *node;
            skb2 = rb_to_skb(parent);

            if(delay>=delay_skb_cb(skb2)->earliest_send) {
                node = &parent->rb_right;
            } else {
                node = &parent->rb_left;
            }
        }

        rb_link_node(&skb->rbnode, parent, node);
        rb_insert_color(&skb->rbnode, &qdisc_data->package_queue_root);
        sch->q.qlen++;
        return NET_XMIT_SUCCESS;

    } else {
        printk(KERN_ALERT "sch_delay %s: package queue ist full! Dropping Package\n", qdisc_data->net_dev_name);
        qdisc_drop(skb, sch, to_free);
        return NET_XMIT_DROP;
    }

}


/* 
    Return the next SKB that will be sent, without removing it.
*/
static struct sk_buff *delay_peek(struct Qdisc *sch) {
    struct delay_qdisc_data *qdisc_data = qdisc_priv(sch);
    if(qdisc_data->reorder_enabled){
        return skb_rb_first(&qdisc_data->package_queue_root);
    } else {
        return qdisc_peek_head(sch);
    }
}


/* 
    Return a SKB to send (without reordering).
    Is called by the Kernel on demand.
    Will return NULL if no package should be sent yet.
*/
static __always_inline struct sk_buff *delay_dequeue_fifo(struct Qdisc *sch) {
    struct delay_qdisc_data *qdisc_data = qdisc_priv(sch);
    if(likely(!sch->q.qlen == 0)) {
        struct sk_buff *skb;
        struct delay_skb_cb *cb;
        u64 now;

        skb = qdisc_peek_head(sch);
        cb = delay_skb_cb(skb);

        now = ktime_get_ns();
        if( cb->earliest_send < now ) {
            //sch->q.qlen--;
            return qdisc_dequeue_head(sch);
        }

        qdisc_watchdog_schedule_ns(&qdisc_data->watchdog, cb->earliest_send);

    }
	return NULL;
}


/* 
    Return a SKB to send (with reordering).
    Is called by the Kernel on demand.
    Will return NULL if no package should be sent yet.
*/
static __always_inline struct sk_buff *delay_dequeue_reorder(struct Qdisc *sch) {

    struct delay_qdisc_data *qdisc_data = qdisc_priv(sch);

    if(likely(!sch->q.qlen == 0)){
        struct sk_buff *skb;
        skb = skb_rb_first(&qdisc_data->package_queue_root);

        if(unlikely(skb != NULL)){
            struct delay_skb_cb *cb;
            u64 now;
            cb = delay_skb_cb(skb);
            now = ktime_get_ns();
            if( cb->earliest_send < now) {
                sch->q.qlen--;
                qdisc_qstats_backlog_dec(sch, skb);
                rb_erase(&skb->rbnode, &qdisc_data->package_queue_root);

                // Restore overwriten pointer to NIC
                skb->dev = qdisc_dev(sch);
                skb->next = NULL;
                skb->prev = NULL;

                return skb;
        } 
        qdisc_watchdog_schedule_ns(&qdisc_data->watchdog, cb->earliest_send);
        }
    }
    return NULL;

}


static int delay_enqueue(struct sk_buff *skb, struct Qdisc *sch, struct sk_buff **to_free) {
    //printk(KERN_INFO "ENQUEUE CALLED!\n");
    struct delay_qdisc_data *qdisc_data = qdisc_priv(sch);
    if(qdisc_data->reorder_enabled) {
        return delay_enqueue_reorder(skb, sch, to_free);
    } else {
        return delay_enqueue_fifo(skb, sch, to_free);
    }
}


static struct sk_buff *delay_dequeue(struct Qdisc *sch) {
    struct delay_qdisc_data *qdisc_data = qdisc_priv(sch);
    if(qdisc_data->reorder_enabled) {
        return delay_dequeue_reorder(sch);
    } else {
        return delay_dequeue_fifo(sch);
    }
}

//static void delay_reset(struct Qdisc *sch) {
//    kfifo_reset(&delay_fifo);
//    kfifo_reset(&packet_fifo);
//    qdisc_reset(sch);
//}

static void delay_reset(struct Qdisc *sch){
    struct delay_qdisc_data *qdisc_data = qdisc_priv(sch);
    sch_tree_lock(sch);
    if (qdisc_data->reorder_enabled){
        struct rb_node *p = rb_first(&qdisc_data->package_queue_root);
        while (p) {
            struct sk_buff *skb = rb_to_skb(p);
            p = rb_next(p);
            rb_erase(&skb->rbnode, &qdisc_data->package_queue_root);
            rtnl_kfree_skbs(skb, skb);
        }
    } else {
        qdisc_reset_queue(sch);
    }
    sch_tree_unlock(sch);
	qdisc_watchdog_cancel(&qdisc_data->watchdog);
}


static int delay_change(struct Qdisc *sch, struct nlattr *opt, struct netlink_ext_ack *extack) {
    struct delay_qdisc_data *qdisc_data = qdisc_priv(sch);
    char reorder_str[50];

	if (opt == NULL) {
        __u32 limit = qdisc_dev(sch)->tx_queue_len;
        sch->limit = limit;
        qdisc_data->reorder_enabled = DEFAULT_REORDERING;

	} else {
		struct tc_delay_qopt *ctl = nla_data(opt);

        if(qdisc_data->reorder_enabled != ctl->reorder){
            delay_reset(sch);
        }
        sch->limit = ctl->limit;
        qdisc_data->reorder_enabled = ctl->reorder;
	}

    if(qdisc_data->reorder_enabled) {
        strcpy(reorder_str, "enabled");
    } else {
        strcpy(reorder_str, "disabled");
    }

    printk(KERN_INFO "sch_delay %s: configuration changed: Reordering: %s, Limit %d\n", qdisc_data->net_dev_name, reorder_str, sch->limit);

    return 0;
};


static struct file_operations sch_delay_file_ops = {
    .read = sch_delay_device_read,
    .write = sch_delay_device_write,
    .open = sch_delay_device_open,
    .release = sch_delay_device_release
};


/*
    Setup internal data for operation.
    Called when the Qdisc is started on a new Interface
*/
static int delay_init(struct Qdisc *sch, struct nlattr *opt, struct netlink_ext_ack *extack) {
    int status;
    struct delay_qdisc_data *qdisc_data = qdisc_priv(sch);
    struct net_device *dev = qdisc_dev(sch);

    strscpy(qdisc_data->net_dev_name, dev->name, MAX_DEVNAME_LEN);

    status = kfifo_alloc(&qdisc_data->delay_queue, DELAY_FIFO_SIZE, GFP_KERNEL);

    qdisc_watchdog_init(&qdisc_data->watchdog, sch);
    qdisc_data->chr_dev_open_count = 0;

    strscpy(qdisc_data->chr_dev_name, DEVICE_PREFIX, MAX_DEVNAME_LEN);
    size_t offs = strlen(qdisc_data->chr_dev_name);
    strscpy(qdisc_data->chr_dev_name+offs, dev->name, MAX_DEVNAME_LEN-offs);

    unsigned int h_min = TC_H_MIN(sch->handle);
    unsigned int h_maj = TC_H_MAJ(sch->handle) >> 16; 
    offs = strlen(qdisc_data->chr_dev_name);
    snprintf(qdisc_data->chr_dev_name+offs, MAX_DEVNAME_LEN-offs, "-%x_%x", h_maj, h_min);
    
    // Register Device
    if((alloc_chrdev_region(&qdisc_data->chr_dev_majour_num, 0, 1, qdisc_data->chr_dev_name)) < 0) {
        printk(KERN_ALERT "sch_delay %s: failed to allocate majour number\n", qdisc_data->net_dev_name);
    }

#if LINUX_VERSION_CODE <= KERNEL_VERSION(6,4,0)
    qdisc_data->dev_class = class_create(THIS_MODULE, qdisc_data->chr_dev_name);
#else
    qdisc_data->dev_class = class_create(qdisc_data->chr_dev_name);
#endif
    
    if(IS_ERR(qdisc_data->dev_class)) {
        printk(KERN_ALERT "sch_delay %s: failed to create struct class for device\n", qdisc_data->net_dev_name);
    }


    if(IS_ERR(device_create(qdisc_data->dev_class, NULL, qdisc_data->chr_dev_majour_num, qdisc_data, qdisc_data->chr_dev_name))) {
        printk(KERN_ALERT "sch_delay %s: failed to create device\n", qdisc_data->net_dev_name);
    }

    cdev_init(&qdisc_data->c_dev, &sch_delay_file_ops);
    if( cdev_add( &qdisc_data->c_dev, qdisc_data->chr_dev_majour_num, 1 ) == -1)
    {
        printk(KERN_ALERT "sch_delay %s: device addition failed\n", qdisc_data->net_dev_name );
        device_destroy(qdisc_data->dev_class, qdisc_data->chr_dev_majour_num);
        class_destroy(qdisc_data->dev_class);
        unregister_chrdev_region(qdisc_data->chr_dev_majour_num, 1);
        return -1;
    }

    delay_change(sch, opt, extack);

    printk(KERN_INFO "sch_delay %s: device registered at: /dev/%s\n", qdisc_data->net_dev_name, qdisc_data->chr_dev_name);
    


    return 0;
}


/*
    Clean up all resources.
    Called when the Qdisc is removed from an Interface.
*/
static void delay_destroy(struct Qdisc *sch) {
    struct delay_qdisc_data *qdisc_data = qdisc_priv(sch);

    kfifo_free(&qdisc_data->delay_queue);

    device_destroy(qdisc_data->dev_class, qdisc_data->chr_dev_majour_num);
    class_destroy(qdisc_data->dev_class);
    unregister_chrdev_region(qdisc_data->chr_dev_majour_num, 1);

    qdisc_watchdog_cancel(&qdisc_data->watchdog);

    printk(KERN_INFO "sch_delay %s: qdisc removed.\n", qdisc_data->net_dev_name);
}


static struct Qdisc_ops delay_qdisc_ops __read_mostly = {
	.id		= "delay",
	.priv_size	= sizeof(struct delay_qdisc_data),
	.enqueue	= delay_enqueue,
	.dequeue	= delay_dequeue,
    .init       = delay_init,
    .change     = delay_change,
    .destroy    = delay_destroy,
//    .reset      = delay_reset,
    .peek       = delay_peek,
	.owner		= THIS_MODULE,
};

/*
   Module INIT
*/


/*
    Entrypoint of Module.
    Registers the Qdisc.
    Called when the Module is started.
*/
static int __init delay_module_init(void) {
    int status;
    
    status = register_qdisc(&delay_qdisc_ops);
    if(status) {
        printk(KERN_ALERT "sch_delay: Failed to register Qdisc\n");
        return status;
    }

    printk(KERN_INFO "sch_delay: module loaded, version: %s\n", VERSION);
	return status;
}


/*
    Exitpoint of Module.
    Unregisters the Qdisc.
    Called when the Module is removed.
    Fails if instances of the Qdisc are still running.
*/
static void __exit delay_module_exit(void) {
    unregister_qdisc(&delay_qdisc_ops);
    printk(KERN_INFO "sch_delay: module removed\n");
}


/*
    Register Module eintry and exit points.
*/
module_init(delay_module_init);
module_exit(delay_module_exit);

