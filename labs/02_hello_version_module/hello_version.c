#include <linux/init.h>
#include <linux/module.h>
#include <linux/i2c.h>
#include <linux/utsname.h>

#define DRIVER_AUTHOR "Richard Leitner <me@g0hl1n.net>"
#define DRIVER_DESC   "Hello-Version Test Module"

static char *who = "nobody";
static struct timeval starttime, endtime;

module_param(who, charp, S_IRUGO);
MODULE_PARM_DESC(who, "Your name");

static int __init hello_version_init(void)
{
	struct new_utsname *u;

	/* get start time */
	do_gettimeofday(&starttime);

	/* get kernel version */
	u = utsname();

	printk(KERN_INFO "Hello %s, you are running Linux %s\n",
	       who, u->release);
	return 0;
}

static void __exit hello_version_exit(void)
{
	unsigned long run_ms;

	/* get end time & calc running time */
	do_gettimeofday(&endtime);
	run_ms = (endtime.tv_sec - starttime.tv_sec) * 1000 +
		 (endtime.tv_usec - starttime.tv_usec) / 1000;

	printk(KERN_INFO "Bye %s, the %lums with you were nice!\n", who, run_ms);
}

module_init(hello_version_init);
module_exit(hello_version_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR(DRIVER_AUTHOR);
MODULE_DESCRIPTION(DRIVER_DESC);
