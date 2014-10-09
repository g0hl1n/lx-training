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

struct feserial_dev {
	struct miscdevice miscdev;
	void __iomem *regs;
};

static unsigned int reg_read(struct feserial_dev *dev, int off)
{
	return readl(dev->regs + off*4);
}

static void reg_write(struct feserial_dev *dev, unsigned int val, int off)
{
	writel(val, dev->regs + off*4);
}

void write_char(struct feserial_dev *dev, char c)
{
	/* busy waiting for UART_LSR_THRE = Transmit-hold-register empty */
	while ((reg_read(dev, UART_LSR) & UART_LSR_THRE) == 0)
		cpu_relax();

	reg_write(dev, c, UART_TX);
}

static ssize_t feserial_write(struct file *fp, const char __user *buf,
			      size_t len, loff_t *off)
{
	return -EINVAL;
}

static ssize_t feserial_read(struct file *fp, char __user *buf, size_t len,
			     loff_t *off)
{
	return -EINVAL;
}

static const struct file_operations feserial_fops = {
	.owner	= THIS_MODULE,
	.read	= feserial_read,
	.write	= feserial_write,
};

static int feserial_probe(struct platform_device *pdev)
{
	struct feserial_dev *dev;
	struct resource *res;
	unsigned int baud_divisor, uartclk;
	int err;

	dev = devm_kzalloc(&pdev->dev, sizeof(struct feserial_dev), GFP_KERNEL);
	if (!dev)
		return -ENOMEM;

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

	err = misc_register(&dev->miscdev);
	if (err) {
		dev_err(&pdev->dev, "misc device register failed\n");
		goto err_free;
	}

	dev_info(&pdev->dev, "Probed fserial\n");
	return 0;

err_free:
	kfree(dev->miscdev.name);
err_exit:
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
