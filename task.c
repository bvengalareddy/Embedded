#include <linux/module.h>
#include <linux/kernel.h> /* printk() */
#include <linux/errno.h>   /* error codes */
#include <linux/sched.h>
#include "seq_file.c"

MODULE_LICENSE("GPL");

/* Declaration of functions */
void device_exit(void);
int device_init(void);

/* Declaration of the init and exit routines */
module_init(device_init);
module_exit(device_exit);

int device_init(void)
{
	struct task_struct *task = current; // getting global current pointer
	printk(KERN_NOTICE "assignment: current process: %s, PID: %d State:%d starttime:%ul",task->comm, task->pid,task->state,task->start_time);
	do
	{
		task = task->parent;
		printk(KERN_NOTICE "assignment: parent process: %s, PID: %d , state:%d starttime:%ul",task->comm, task->pid,task->state,task->start_time);
	} while (task->pid != 0);
	return 0;
}

void device_exit(void) {
	printk(KERN_NOTICE "assignment: exiting module");
}
