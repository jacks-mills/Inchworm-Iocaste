#include <stdint.h>
#include <stddef.h>
#include <inttypes.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>
#include <zephyr/device.h>
#include <zephyr/drivers/pwm.h>
#include <zephyr/drivers/adc.h>


#define PWM_NODE DT_NODELABEL(ledc0)
#define PWM_CHANNEL 0
#define PWM_PERIOD PWM_USEC(1000)

#define SERVO_THREAD_STACK_SIZE 1024
#define SERVO_THREAD_PRIORITY 7

K_MSGQ_DEFINE(servo_msgq, sizeof(), 8, 1);


const struct decice *servo = DEVICE_DT_GET(PWM_NODE);
static const struct adc_dt_spec pot_dev = ADC_DT_SPEC_GET(DT_PATH(zephyr_user));

static void servo_thread(void *arg1, void *arg2, void *arg3) {
    if (!device_is_ready(servo)) {
        return;
    }

    uint16_t buf;

    // Setup ADC sequence struct
	struct adc_sequence sequence = {
		.buffer = &buf,
		.buffer_size = sizeof(buf),
	};

    err = adc_channel_setup_dt(&pot_dev);
	if (err < 0) {
		printk("Could not setup channel (%d)\n", err);
		return;
	}

    printk("ADC initialized successfully. Reading GPIO1...\n");

    while (1) {
        int rc = k_msgq_get(&servo_msgq, , K_MSEC(10));
        if (rc == 0) {
            pwm_set(servo, PWM_CHANNEL, PWM_PERIOD, , 0);
            k_sleep(K_MSEC(50));

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
        }
    }
}

K_THREAD_DEFINE(led_smf, SERVO_THREAD_STACK_SIZE, servo_thread, NULL, NULL, NULL,
    SERVO_THREAD_PRIORITY, 0, 0);