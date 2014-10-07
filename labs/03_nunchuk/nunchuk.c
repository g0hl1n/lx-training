#include <linux/init.h>
#include <linux/module.h>
#include <linux/i2c.h>
#include <linux/delay.h>

#define DRIVER_AUTHOR "Richard Leitner <me@g0hl1n.net>"
#define DRIVER_DESC   "Nintendo Nunchuk Driver"

#define NUNCHUK_REG_SIZE 6

int nunchuck_read_registers(struct i2c_client *client, u8 *data)
{
	int err;

	/* trigger a register read */
	err = i2c_smbus_write_byte(client, 0x00);
	if (err < 0) {
		dev_err(&client->dev, "write i2c data failed: %d\n", err);
		return err;
	}

	/* the nunchuk needs some time between the command and the recv */
	mdelay(2); /* FIXME determine the minumum time (1ms worked already) */

	i2c_master_recv(client, data, NUNCHUK_REG_SIZE); /* read registers */

	return 0;
}

/*
 * the nunchuk updates it's registers only on reading,
 * so we read it twice to get the current values
 */
int nunchuck_read_current_registers(struct i2c_client *client, u8 *data)
{
	int err;

	/* parse the old data and throw it away */
	err = nunchuck_read_registers(client, data);
	if (err < 0)
		return err;
	memset(data, 0, NUNCHUK_REG_SIZE);

	/* the nunchuk needs some time between the two register reads */
	mdelay(5); /* FIXME determine the minumum time (2ms worked already) */

	err = nunchuck_read_registers(client, data);
	if (err < 0)
		return err;

	dev_dbg(&client->dev, "read reg: 0x%02X%02X%02X%02X%02X%02X\n",
		data[0], data[1], data[2], data[3], data[4], data[5]);
	return 0;
}

static int nunchuk_probe(struct i2c_client *client,
			 const struct i2c_device_id *id)
{
	int err;
	u8 regs[NUNCHUK_REG_SIZE];
	int joystick_x = -1;
	int joystick_y = -1;
	int accel_x = -1;
	int accel_y = -1;
	int accel_z = -1;
	int zpressed = -1;
	int cpressed = -1;

	dev_info(&client->dev, "Probe " DRIVER_DESC "\n");

	/* setup non-encrypted connection */
	err = i2c_smbus_write_byte_data(client, 0xf0, 0x55);
	if (err < 0) {
		dev_err(&client->dev, "write i2c data failed: %d\n", err);
		return err;
	}

	usleep_range(900, 1100); /* wait ~1ms for command completion */

	/* finish initialization */
	err = i2c_smbus_write_byte_data(client, 0xfb, 0x00);
	if (err < 0) {
		dev_err(&client->dev, "write i2c data failed: %d\n", err);
		return err;
	}

	err = nunchuck_read_current_registers(client, regs);
	if (err < 0)
		return err;

	/* get joystick values */
	joystick_x = regs[0] & 0xFF;
	joystick_y = regs[1] & 0xFF;

	/* get accellerometer values,
	 * the lower two bits have to be extracted from the last byte */
	accel_x = regs[2] << 2;
	accel_x += (regs[5] & (BIT(2) | BIT(3))) >> 2;
	accel_y = regs[3] << 2;
	accel_y += (regs[5] & (BIT(4) | BIT(5))) >> 4;
	accel_z = regs[4] << 2;
	accel_z += (regs[5] & (BIT(6) | BIT(7))) >> 6;

	/* get Z state (first bit of last byte) */
	zpressed = 1;
	if (regs[5] & BIT(0)) /* Z is released */
		zpressed = 0;

	/* get Z state (second bit of last byte) */
	cpressed = 1;
	if (regs[5] & BIT(1)) /* C is released */
		cpressed = 0;

	dev_info(&client->dev, "Accellerometer: X=%3d; Y=%3d; Z=%3d\n",
		 accel_x, accel_y, accel_z);
	dev_info(&client->dev, "Joystick:       X=%3d; Y=%3d\n",
		 joystick_x, joystick_y);
	dev_info(&client->dev, "Buttons:        Z=%3d; C=%3d\n",
		 zpressed, cpressed);

	return 0;
}

static int nunchuk_remove(struct i2c_client *client)
{
	dev_info(&client->dev, "Remove " DRIVER_DESC "\n");
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
