#include <linux/init.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/serial_reg.h>
#include <linux/io.h>
#include <linux/miscdevice.h>
#include <linux/fs.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/interrupt.h>
#include <linux/sched.h>

#include <asm/uaccess.h>

#define SERIAL_BUFSIZE 16

/* ioctl operations */
#define IOCTL_SERIAL_RESET_COUNTER	0
#define IOCTL_SERIAL_GET_COUNTER	1

struct feserial_dev {
	struct miscdevice miscdev;
	void __iomem *regs;
	spinlock_t reg_lock; /* serial register rw lock */
	unsigned long wcounter;
	int irq;
	char serial_buf[SERIAL_BUFSIZE];
	int serial_buf_rd; /* reading index */
	int serial_buf_wr; /* writing index */
	spinlock_t buf_lock; /* serial buffer rw lock */
	wait_queue_head_t serial_wait;
};

static unsigned int reg_read(struct feserial_dev *dev, int off)
{
	unsigned int val;
	unsigned long flags;

	spin_lock_irqsave(&dev->reg_lock, flags);
	val = readl(dev->regs + off*4);
	spin_unlock_irqrestore(&dev->reg_lock, flags);
	return val;
}

static void reg_write(struct feserial_dev *dev, unsigned int val, int off)
{
	unsigned long flags;

	spin_lock_irqsave(&dev->reg_lock, flags);
	writel(val, dev->regs + off*4);
	spin_unlock_irqrestore(&dev->reg_lock, flags);
}

void write_char(struct feserial_dev *dev, char c)
{
	/* busy waiting for UART_LSR_THRE = Transmit-hold-register empty */
	while ((reg_read(dev, UART_LSR) & UART_LSR_THRE) == 0)
		cpu_relax();

	dev->wcounter++; /* increment write counter */

	reg_write(dev, c, UART_TX);
}

static ssize_t feserial_write(struct file *fp, const char __user *buf,
			      size_t len, loff_t *off)
{
	struct feserial_dev *dev;
	struct miscdevice *miscdev;
	char *kbuf;
	int err = 0;

	/* get miscdev from struct file.
	 * to work the patch from Thomas Petazzoni have to be applied! */
	miscdev = fp->private_data;
	if (miscdev == NULL) {
		err = -EFAULT;
		pr_err("unable to get device from write(), is the patch applied?\n");
		goto err_exit;
	}

	/* get feserial_dev from miscdev */
	dev = container_of(fp->private_data, struct feserial_dev, miscdev);
	if (dev == NULL) {
		err = -EFAULT;
		dev_err(miscdev->parent, "unable to get container_of miscdev\n");
		goto err_exit;
	}

	/* allocate kernel memory and copy over buffer */
	kbuf = kmalloc(len, GFP_KERNEL);
	if (!kbuf)
		return -ENOMEM;
	err = copy_from_user(kbuf, buf, len);
	if (err) {
		err = -EFAULT;
		goto err_free;
	}

	/* actually write data */
	for (err = 0; err < len; err++) {
		write_char(dev, kbuf[err]);

		if (kbuf[err] == '\n') /* send also \r if \n was sent */
			write_char(dev, '\r');
	}

err_free:
	kfree(kbuf);
err_exit:
	return err;
}

static ssize_t feserial_read(struct file *fp, char __user *buf, size_t len,
			     loff_t *off)
{
	unsigned long flags;
	struct feserial_dev *dev;
	struct miscdevice *miscdev;
	int err;
	char kbuf;

	/* get miscdev from struct file.
	 * to work the patch from Thomas Petazzoni have to be applied! */
	miscdev = fp->private_data;
	if (miscdev == NULL) {
		pr_err("unable to get device from write(), is the patch applied?\n");
		return -EFAULT;
	}

	dev = container_of(fp->private_data, struct feserial_dev, miscdev);
	if (dev == NULL) {
		dev_err(miscdev->parent, "unable to get container_of miscdev\n");
		return -EFAULT;
	}

	err = wait_event_interruptible(dev->serial_wait,
				      dev->serial_buf_rd != dev->serial_buf_wr);
	if (err)
		return -ERESTARTSYS;

	spin_lock_irqsave(&dev->buf_lock, flags);
	kbuf = dev->serial_buf[dev->serial_buf_rd++];
	spin_unlock_irqrestore(&dev->buf_lock, flags);

	put_user(kbuf, buf);
	return 1;
}

static long feserial_unlocked_ioctl(struct file *fp, unsigned int ioctl_num,
				    unsigned long ioctl_param)
{
	struct feserial_dev *dev;
	struct miscdevice *miscdev;

	/* get miscdev from struct file.
	 * to work the patch from Thomas Petazzoni have to be applied! */
	miscdev = fp->private_data;
	if (miscdev == NULL) {
		pr_err("unable to get device from write(), is the patch applied?\n");
		return -EFAULT;
	}

	/* get feserial_dev from miscdev */
	dev = container_of(fp->private_data, struct feserial_dev, miscdev);
	if (dev == NULL) {
		pr_err("unable to get feserial_dev from miscdevice\n");
		return -EFAULT;
	}

	switch (ioctl_num) {
	case IOCTL_SERIAL_RESET_COUNTER:
		/* reset the counter for the current device
		 * IGNORE the ioctl_param */
		dev->wcounter = 0;
		break;
	case IOCTL_SERIAL_GET_COUNTER:
		/* get the counter for the current device */
		put_user(dev->wcounter, (char *)ioctl_param);
		break;
	default:
		/* error */
		return -EFAULT;
	}

	return 0;
}

static const struct file_operations feserial_fops = {
	.owner		= THIS_MODULE,
	.read		= feserial_read,
	.write		= feserial_write,
	.unlocked_ioctl	= feserial_unlocked_ioctl,
};

irqreturn_t feserial_irq_handler(int irq, void *dev_id)
{
	struct feserial_dev *dev = dev_id;
	char kbuf;
	unsigned long flags;

	kbuf = reg_read(dev, UART_RX);

	spin_lock_irqsave(&dev->buf_lock, flags);
	dev->serial_buf[dev->serial_buf_wr++] = kbuf;
	if (dev->serial_buf_wr >= SERIAL_BUFSIZE)
		dev->serial_buf_wr = 0;
	spin_unlock_irqrestore(&dev->buf_lock, flags);
	wake_up(&dev->serial_wait);

	return IRQ_HANDLED;
}

static int feserial_probe(struct platform_device *pdev)
{
	struct feserial_dev *dev;
	struct resource *res;
	unsigned int baud_divisor, uartclk;
	int err;

	dev = devm_kzalloc(&pdev->dev, sizeof(struct feserial_dev), GFP_KERNEL);
	if (!dev)
		return -ENOMEM;
	dev->wcounter = 0;
	dev->serial_buf_rd = 0;
	dev->serial_buf_wr = 0;
	init_waitqueue_head(&dev->serial_wait);
	spin_lock_init(&dev->reg_lock);
	spin_lock_init(&dev->buf_lock);

	/* get the physical base address from DT */
	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res)
		return -EFAULT;

	/* get virtual address for the device's physical base address */
	dev->regs = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(dev->regs))
		return PTR_ERR(dev->regs);

	/* power management initialization */
	pm_runtime_enable(&pdev->dev);
	pm_runtime_get_sync(&pdev->dev);

	/* configure line and baudrate (from DT) */
	of_property_read_u32(pdev->dev.of_node, "clock-frequency", &uartclk);
	baud_divisor = uartclk / 16 / 115200;

	reg_write(dev, 0x07, UART_OMAP_MDR1);
	reg_write(dev, 0x00, UART_LCR);
	reg_write(dev, UART_LCR_DLAB, UART_LCR);
	reg_write(dev, baud_divisor & 0xff, UART_DLL);
	reg_write(dev, (baud_divisor >> 8) & 0xff, UART_DLM);
	reg_write(dev, UART_LCR_WLEN8, UART_LCR);

	/* request soft reset */
	reg_write(dev, UART_FCR_CLEAR_RCVR || UART_FCR_CLEAR_XMIT, UART_FCR);
	reg_write(dev, 0x00, UART_OMAP_MDR1);

	/* attach private data struct to driver */
	platform_set_drvdata(pdev, dev);

	/* initialize the miscdev */
	dev->miscdev.minor = MISC_DYNAMIC_MINOR;
	dev->miscdev.name = kasprintf(GFP_KERNEL, "feserial-%x", res->start);
	dev->miscdev.fops = &feserial_fops;

	/* enable, get & request iterrupt */
	reg_write(dev, UART_IER_RDI, UART_IER);
	dev->irq = platform_get_irq(pdev, 0);
	err = devm_request_irq(&pdev->dev, dev->irq, feserial_irq_handler, 0,
			       "feserial", dev);
	if (err) {
		dev_err(&pdev->dev, "irq request failed\n");
		goto err_free;
	}

	/* register the device */
	err = misc_register(&dev->miscdev);
	if (err) {
		dev_err(&pdev->dev, "misc device register failed\n");
		goto err_free;
	}

	dev_info(&pdev->dev, "Probed fserial\n");
	return 0;

err_free:
	kfree(dev->miscdev.name);
	return err;
}

static int feserial_remove(struct platform_device *pdev)
{
	struct feserial_dev *dev;
	int err;

	dev = platform_get_drvdata(pdev);

	/* unregister */
	err = misc_deregister(&dev->miscdev);

	/* ower management disable */
	pm_runtime_disable(&pdev->dev);

	/* free */
	kfree(dev->miscdev.name);

	dev_info(&pdev->dev, "Removed fserial\n");
	return 0;
}

static const struct platform_device_id feserial_id[] = {
	{ "feserial", 0 },
	{ }
};
MODULE_DEVICE_TABLE(platform, feserial_id);

#ifdef CONFIG_OF
static const struct of_device_id feserial_dt_ids[] = {
	{ .compatible = "free-electrons,serial", },
	{ }
};
MODULE_DEVICE_TABLE(of, feserial_dt_ids);
#endif

static struct platform_driver feserial_driver = {
	.driver = {
		.name = "feserial",
		.owner = THIS_MODULE,
		.of_match_table = of_match_ptr(feserial_dt_ids),
	},
	.probe = feserial_probe,
	.remove = feserial_remove,
};

module_platform_driver(feserial_driver);
MODULE_LICENSE("GPL");
