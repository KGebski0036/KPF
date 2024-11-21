#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/kernel_stat.h>
#include <linux/cpumask.h>
#include <linux/sched/stat.h>
#include <linux/jiffies.h>
#include <linux/delay.h>
#include <linux/kthread.h>
#include <linux/sched.h>

static struct task_struct *cpu_usage_thread; // Thread pointer
static bool stop_thread = false;
static void get_cpu_times(unsigned int* cpu_times);

static int get_cpu_loop(void* data) {
    while (!kthread_should_stop() && !stop_thread) {
        u32 time1[2];
        u32 time2[2];
        get_cpu_times(time1);
        msleep(1000);
        get_cpu_times(time2);
        u32 delta_active = (time2[0] - time1[0]) / 1000;
        u32 delta_idle = (time2[1] - time1[1]) / 1000;
        u32 delta_total = delta_active + delta_idle;
        u64 percentage = delta_active * 100 / delta_total;
        pr_info("CPU Usage: %llu%%", percentage);
        msleep(10000);
    }
    return 0;
}
static void get_cpu_times(unsigned int* cpu_times) {
    int cpu;
    u32 total_active = 0;
    u32 total_idle = 0;
    for_each_possible_cpu(cpu) {
        const struct kernel_cpustat *kstat = &kcpustat_cpu(cpu);

        u64 user_time = kstat->cpustat[CPUTIME_USER];
        u64 nice_time = kstat->cpustat[CPUTIME_NICE];
        u64 system_time = kstat->cpustat[CPUTIME_SYSTEM];
        u64 idle_time = kstat->cpustat[CPUTIME_IDLE];

        total_active += jiffies_to_msecs(user_time);
        total_active += jiffies_to_msecs(nice_time);
        total_active += jiffies_to_msecs(system_time);
        total_idle += jiffies_to_msecs(idle_time);
    }
    cpu_times[0] = total_active;
    cpu_times[1] = total_idle;
}

static int __init mod_init(void) {
    cpu_usage_thread = kthread_run(get_cpu_loop, NULL, "cpu_usage_thread");
    if (IS_ERR(cpu_usage_thread)) {
        pr_err("Failed to create CPU usage thread\n");
        return PTR_ERR(cpu_usage_thread);
    }

    return 0;
}

static void __exit mod_exit(void) {
    printk(KERN_INFO "CPU usage monitor stopped!\n");
}

module_init(mod_init);
module_exit(mod_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("CPU usage monitor");
MODULE_AUTHOR("Michal Bernacki, Karol Gebski");
