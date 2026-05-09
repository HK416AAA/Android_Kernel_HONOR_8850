// SPDX-License-Identifier: GPL-2.0
/*
 * boot time implementation
 *
 * Copyright (c) 2021-2021 Honor Technologies Co., Ltd.
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 */

#include <linux/device.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/proc_fs.h>
#include <linux/sched.h>
#include <linux/sched/clock.h>
#include <linux/seq_file.h>
#include <linux/types.h>
#include <linux/uaccess.h>
#include <linux/wait.h>

#define BOOT_STR_SIZE 128 // max len of string.
#define BOOT_LOG_NUM 256 // max size of boot log stored.
#define BOOT_50_MS 50000

#define BOOTUP_DONE "[INFOR]_wm_boot_animation_done"
struct boot_log_struct {
	u32 Second;
	u32 mSecond;
	char event[BOOT_STR_SIZE];
} boottime[BOOT_LOG_NUM];

static int boot_log_count;
static DEFINE_MUTEX(boottime_lock);
static int boottime_enabled = 1;

static long long Second(unsigned long long nsec, unsigned long divs)
{
	if ((long long)nsec < 0) {
		nsec = -nsec; /*lint !e501 */
		do_div(nsec, divs);
		return -nsec; /*lint !e501 */
	}
	do_div(nsec, divs);
	return nsec;
}

static unsigned long mSecond(unsigned long long nsec, unsigned long divs)
{
	if ((long long)nsec < 0)
		nsec = -nsec;
	return do_div(nsec, divs);
}

/**
 * log_boot - saving information into local buffer, boottime[].
 *   max size of boottime buffer is BOOT_LOG_NUM.
 * @str: information to be recorded.
 */
void log_boot(char *str)
{
	unsigned long long ts, tmp;

	if (!str)
		return;
	if (!boottime_enabled)
		return;
	ts = sched_clock();
	if (ts == 0) {
		pr_info("[boottime] invalid boottime point\n");
		return;
	}
	if (boot_log_count >= BOOT_LOG_NUM) {
		pr_info("[boottime] no enough boottime buffer\n");
		return;
	}
	if (strncmp(BOOTUP_DONE, str, strlen(BOOTUP_DONE)) == 0) {
		pr_info("BOOTUP_DONE:%s,str:%s\n", str, BOOTUP_DONE);
		boottime_enabled = 0; //zygote start
	}
	mutex_lock(&boottime_lock);
	tmp = ts;
	boottime[boot_log_count].Second = Second(tmp, 1000000000);
	tmp = ts;
	boottime[boot_log_count].mSecond = mSecond(tmp, 1000000000);
	strscpy((char *)&boottime[boot_log_count].event, str, BOOT_STR_SIZE);
	boot_log_count++;
	mutex_unlock(&boottime_lock);
}
EXPORT_SYMBOL(log_boot);

/**
 * do_boottime_initcall - record duration time while calling initcall function.
 *   if the duration time is bigger than the threshold value, this function will save
 *   the duration time into locall buffer.
 * @fn: initcall function.
 *
 * This function returns whatever initcall function returns.
 */
int __init_or_module do_boottime_initcall(initcall_t fn)
{
	int ret;
	unsigned long long duration;
	ktime_t calltime, delta, rettime;
	char log_info[BOOT_STR_SIZE] = {0};

	calltime = ktime_get();
	ret = fn();
	rettime = ktime_get();
	delta = ktime_sub(rettime, calltime);
	duration = (unsigned long long)ktime_to_ns(delta) >> 10;
	if (duration > BOOT_50_MS) {
		snprintf(log_info, sizeof(log_info), "[WARNING] %pS %lld usecs",
			 fn, duration);
		log_boot(log_info);
	}
	return ret;
}
EXPORT_SYMBOL(do_boottime_initcall);

static int boottime_show(struct seq_file *m, void *v)
{
	int i;

	seq_puts(m, "----------- BOOT TIME (sec) -----------\n");
	for (i = 0; i < boot_log_count; i++) {
		seq_printf(m, "%d.%09u s : %s\n", boottime[i].Second,
			   boottime[i].mSecond, boottime[i].event);
	}
	seq_printf(m, "%s\n", boottime_enabled ? "starting..." : "start done");
	return 0;
}

static int boottime_open(struct inode *inode, struct file *file)
{
	return single_open(file, boottime_show, inode->i_private);
}

static ssize_t boottime_write(struct file *filp, const char *ubuf, size_t cnt,
			      loff_t *data)
{
	char buf[BOOT_STR_SIZE] = {0};
	size_t copy_size = cnt;

	if (cnt >= sizeof(buf))
		copy_size = BOOT_STR_SIZE - 1;
	if (copy_from_user(&buf, ubuf, copy_size))
		return -EFAULT;
	if (cnt == 1) {
		if (buf[0] == '0')
			boottime_enabled = 0; // boot up complete
		else if (buf[0] == '1')
			boottime_enabled = 1;
	}
	buf[copy_size] = 0;
	log_boot(buf);
	return cnt;
}

static const struct proc_ops boottime_fops = {
	.proc_open = boottime_open,
	.proc_write = boottime_write,
	.proc_read = seq_read,
	.proc_lseek = seq_lseek,
	.proc_release = single_release,
};

static int boot_time_init(void)
{
	struct proc_dir_entry *pe;

	pe = proc_create("boottime", 0664, NULL, &boottime_fops);
	if (!pe) {
		pr_info("boottime: failed to create '/proc/boottime'\n");
		return -ENOMEM;
	}
	return 0;
}

static void __exit boot_time_exit(void)
{
	remove_proc_entry("bootime", NULL);
}

MODULE_AUTHOR("Honor");
MODULE_DESCRIPTION("Boot Time Recoder");
MODULE_LICENSE("GPL");

module_init(boot_time_init);
module_exit(boot_time_exit);
