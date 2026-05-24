#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>
#include <zephyr/device.h>
#include <zephyr/drivers/pwm.h>

#define PWM_NODE DT_NODELABEL(ledc0)
#define PWM_CHANNEL 0
#define PWM_PERIOD PWM_USEC(1000) // 1 kHz

void main(void) {
	const struct device *pwm_dev = DEVICE_DT_GET(PWM_NODE);

	if (!device_is_ready(pwm_dev)) {
		return;
	}

	while (1) {
		pwm_set(pwm_dev, PWM_CHANNEL, PWM_PERIOD, PWM_PERIOD / 2, 0); // 50% duty
		k_sleep(K_MSEC(1000));
		pwm_set(pwm_dev, PWM_CHANNEL, PWM_PERIOD, PWM_PERIOD / 10, 0); // 10% duty
		k_sleep(K_MSEC(1000));
	}
}