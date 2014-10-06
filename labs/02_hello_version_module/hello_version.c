#include <linux/init.h>
#include <linux/module.h>
#include <linux/i2c.h>
#include <linux/utsname.h>

#define DRIVER_AUTHOR "Richard Leitner <me@g0hl1n.net>"
#define DRIVER_DESC   "Hello-Version Test Module"

static int __init hello_version_init(void)
{
	struct new_utsname *u;

	u = utsname();
	printk(KERN_INFO "Hi, you are running Linux %s\n", u->release);
	return 0;
}

static void __exit hello_version_exit(void)
{
	printk(KERN_INFO "bye... :-(\n");
}

module_init(hello_version_init);
module_exit(hello_version_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR(DRIVER_AUTHOR);
MODULE_DESCRIPTION(DRIVER_DESC);
