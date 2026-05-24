#include <inttypes.h>
#include <stddef.h>
#include <stdint.h>

#include <zephyr/drivers/adc.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>

static const struct adc_dt_spec pot_dev = ADC_DT_SPEC_GET(DT_PATH(zephyr_user));
int main(void)
{
	int err;
	uint16_t buf;

	// Setup ADC sequence struct
	struct adc_sequence sequence = {
		.buffer = &buf,
		.buffer_size = sizeof(buf),
	};


	//  Configure channel prior to sampling.
	if (!adc_is_ready_dt(&pot_dev)) {
		printk("ADC controller device %s not ready\n", pot_dev.dev->name);
		return -ENODEV;
	}

	err = adc_channel_setup_dt(&pot_dev);
	if (err < 0) {
		printk("Could not setup channel (%d)\n", err);
		return 0;
	}

	printk("ADC initialized successfully. Reading GPIO1...\n");

	while (1) {

		err = adc_sequence_init_dt(&pot_dev, &sequence);
		if (err < 0) {
			printk("Sequence init failed (err %d)\n", err);
			continue;
		}

		err = adc_read_dt(&pot_dev, &sequence);
		if (err < 0) {
			printk("Could not read (%d)\n", err);
			continue;
		} else {
			int32_t val_mv = buf;
			printk("Raw ADC Value: %d", buf);

			err = adc_raw_to_millivolts_dt(&pot_dev, &val_mv);
			if (err < 0) {
				printk("Conversion to mV failed\n");
			} else {
				printk("Voltage: %d mv\n", val_mv);
			}
		}

		k_sleep(K_MSEC(1000));
	}
	return 0;
}