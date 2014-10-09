#include <linux/of.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/platform_device.h>

static int feserial_probe(struct platform_device *pdev)
{
	struct resource *res;

	/* get base address from DT */
	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res)
		return -EFAULT;
	dev_info(&pdev->dev, "Probe for device at 0x%X\n", res->start);

	return 0;
}

static int feserial_remove(struct platform_device *pdev)
{
	dev_info(&pdev->dev, "Remove fserial\n");
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
