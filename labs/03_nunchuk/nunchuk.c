#include <linux/init.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/i2c.h>
#include <linux/delay.h>
#include <linux/input-polldev.h>

#define DRIVER_AUTHOR "Richard Leitner <me@g0hl1n.net>"
#define DRIVER_DESC   "Nintendo Nunchuk Driver"

#define NUNCHUK_REG_SIZE 6

#define NUNCHUK_CMD_REG_READ	0x00
#define NUNCHUK_ADR_SETUP_UNENC	0xf0 /* address */
#define NUNCHUK_CMD_SETUP_UNENC	0x55 /*     and command for unencrypted setup */
#define NUNCHUK_ADR_SETUP_END	0xfb /* address */
#define NUNCHUK_CMD_SETUP_END	0x00 /*     and command for ending the setup */

struct nunchuk_regs {
	u8 joystick_x;
	u8 joystick_y;
	u16 accel_x;
	u16 accel_y;
	u16 accel_z;
	bool zpressed;
	bool cpressed;
};

struct nunchuk_dev {
	struct input_polled_dev *poll_dev;
	struct i2c_client *i2c_client;
};

int nunchuck_read_registers(struct i2c_client *client, struct nunchuk_regs *reg)
{
	int err;
	u8 data[NUNCHUK_REG_SIZE];

	/* trigger a register read */
	err = i2c_smbus_write_byte(client, NUNCHUK_CMD_REG_READ);
	if (err < 0) {
		dev_err(&client->dev, "write i2c data failed: %d\n", err);
		return err;
	}

	/* the nunchuk needs some time between the command and the recv */
	mdelay(2); /* FIXME determine the minumum time (1ms worked already) */

	i2c_master_recv(client, data, NUNCHUK_REG_SIZE); /* read registers */

	/* we don't need double reading at a polling interface */

	dev_dbg(&client->dev, "read reg: 0x%02X%02X%02X%02X%02X%02X\n",
		data[0], data[1], data[2], data[3], data[4], data[5]);

	/* get joystick values */
	reg->joystick_x = data[0] & 0xFF;
	reg->joystick_y = data[1] & 0xFF;

	/* get accellerometer values,
	 * the lower two bits have to be extracted from the last byte */
	reg->accel_x = data[2] << 2;
	reg->accel_x += (data[5] & (BIT(2) | BIT(3))) >> 2;
	reg->accel_y = data[3] << 2;
	reg->accel_y += (data[5] & (BIT(4) | BIT(5))) >> 4;
	reg->accel_z = data[4] << 2;
	reg->accel_z += (data[5] & (BIT(6) | BIT(7))) >> 6;

	/* get Z state (first bit of last byte) */
	reg->zpressed = 1;
	if (data[5] & BIT(0)) /* Z is released */
		reg->zpressed = 0;

	/* get Z state (second bit of last byte) */
	reg->cpressed = 1;
	if (data[5] & BIT(1)) /* C is released */
		reg->cpressed = 0;
	return 0;
}

void nunchuk_poll(struct input_polled_dev *dev)
{
	int err;
	struct nunchuk_dev *nunchuk = dev->private;
	struct input_dev *input = nunchuk->poll_dev->input;
	struct nunchuk_regs regs;

	err = nunchuck_read_registers(nunchuk->i2c_client, &regs);
	if (err)
		return;

	/* write values to input subsystem */
	input_report_key(input, BTN_Z, regs.zpressed);
	input_report_key(input, BTN_C, regs.cpressed);

	input_report_abs(input, ABS_X, regs.joystick_x);
	input_report_abs(input, ABS_Y, regs.joystick_y);
}

static int nunchuk_probe(struct i2c_client *client,
			 const struct i2c_device_id *id)
{
	int err;
	struct input_polled_dev *poll_dev;
	struct input_dev *input;
	struct nunchuk_dev *nunchuk;

	dev_info(&client->dev, "Probe " DRIVER_DESC "\n");

	/* allocate our memory */
	nunchuk = devm_kzalloc(&client->dev, sizeof(struct nunchuk_dev),
			       GFP_KERNEL);
	if (!nunchuk)
		return -ENOMEM;

	poll_dev = input_allocate_polled_device();
	if (!poll_dev) {
		err = -ENOMEM;
		goto exit_err;
	}

	/* set the i2c client and the polling device for the nunchuk */
	nunchuk->i2c_client = client;
	nunchuk->poll_dev = poll_dev;

	/* backlink from polldev to nunchuk */
	poll_dev->private = nunchuk;

	/* link the nunchuk to the i2c client */
	i2c_set_clientdata(client, nunchuk);

	/* set the i2c client as parent of the input device */
	input = poll_dev->input;
	input->dev.parent = &client->dev;

	/* input subsystem information */
	input->name = "Wii Nunchuk";
	input->id.bustype = BUS_I2C;

	set_bit(EV_KEY, input->evbit);
	set_bit(BTN_C, input->keybit);
	set_bit(BTN_Z, input->keybit);

	set_bit(EV_ABS, input->evbit);
	input_set_abs_params(input, ABS_X, 0, 255, 0, 0);
	input_set_abs_params(input, ABS_Y, 0, 255, 0, 0);

	/* poll setup */
	poll_dev->poll = nunchuk_poll;
	poll_dev->poll_interval = 50;

	/* setup non-encrypted connection */
	err = i2c_smbus_write_byte_data(client, NUNCHUK_ADR_SETUP_UNENC,
					NUNCHUK_CMD_SETUP_UNENC);
	if (err < 0) {
		dev_err(&client->dev, "write i2c data failed: %d\n", err);
		goto exit_with_free;
	}

	usleep_range(900, 1100); /* wait ~1ms for command completion */

	/* finish initialization */
	err = i2c_smbus_write_byte_data(client, NUNCHUK_ADR_SETUP_END,
					NUNCHUK_CMD_SETUP_END);
	if (err < 0) {
		dev_err(&client->dev, "write i2c data failed: %d\n", err);
		goto exit_with_free;
	}

	/* register the device */
	err = input_register_polled_device(poll_dev);
	if (err)
		goto exit_with_free;

	return 0; /* exit with success */

exit_with_free:
	input_free_polled_device(poll_dev);
exit_err:
	return err;
}

static int nunchuk_remove(struct i2c_client *client)
{
	struct nunchuk_dev *nunchuk;
	struct input_polled_dev *poll_dev;

	dev_info(&client->dev, "Remove " DRIVER_DESC "\n");

	nunchuk = i2c_get_clientdata(client);
	/* TODO check rc */

	poll_dev = nunchuk->poll_dev;

	input_unregister_polled_device(poll_dev);

	input_free_polled_device(poll_dev);
	return 0;
}

static const struct i2c_device_id nunchuk_id[] = {
	{ "nunchuk", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, nunchuk_id);

#ifdef CONFIG_OF
static const struct of_device_id nunchuk_dt_ids[] = {
	{ .compatible = "nintendo,nunchuk", },
	{ }
};
MODULE_DEVICE_TABLE(of, nunchuk_dt_ids);
#endif

static struct i2c_driver nunchuk_driver = {
	.probe		= nunchuk_probe,
	.remove		= nunchuk_remove,
	.id_table	= nunchuk_id,
	.driver = {
		.name	= "nunchuk",
		.owner	= THIS_MODULE,
		.of_match_table = of_match_ptr(nunchuk_dt_ids),
	},
};

/* macro for i2c init & exit functions */
module_i2c_driver(nunchuk_driver);

MODULE_LICENSE("GPL");
MODULE_AUTHOR(DRIVER_AUTHOR);
MODULE_DESCRIPTION(DRIVER_DESC);
