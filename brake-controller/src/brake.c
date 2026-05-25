#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/device.h>
#include <zephyr/drivers/pwm.h>
#include <zephyr/drivers/adc.h>
#include "brake.h"

LOG_MODULE_REGISTER(brake_controller, LOG_LEVEL_INF);

#define BRAKE_THREAD_STACK_SIZE 2048
#define BRAKE_THREAD_PRIORITY   5

K_THREAD_STACK_DEFINE(brake_thread_stack, BRAKE_THREAD_STACK_SIZE);
static struct k_thread brake_thread_data;
static k_tid_t brake_tid = NULL;

#define PWM_NODE DT_NODELABEL(ledc0)
#define PWM_CHANNEL 0
#define PWM_PERIOD PWM_USEC(20000)
const struct device *servo = DEVICE_DT_GET(PWM_NODE);

static const struct adc_dt_spec pot = ADC_DT_SPEC_GET(DT_PATH(zephyr_user));

/* desired brake percentage set with brake_set */
atomic_t desired_percentage = ATOMIC_INIT(0);

static void brake_thread(void *arg1, void *arg2, void *arg3) {
    int error = 0;
    uint32_t count = 0;
    uint16_t adc_buf;
    struct adc_sequence sequence = {
        .buffer = &adc_buf,
        .buffer_size = sizeof(adc_buf),
    };

    while (1) {
        int32_t val_mv;

		pwm_set(servo, PWM_CHANNEL, PWM_PERIOD, PWM_PERIOD / 20, 0); // 50% duty
		k_sleep(K_MSEC(1000));
		pwm_set(servo, PWM_CHANNEL, PWM_PERIOD, PWM_PERIOD / 10, 0); // 10% duty
		k_sleep(K_MSEC(1000));

        LOG_INF("ADC reading[%u]: %s channel %d: ",
                count++, pot.dev->name, pot.channel_id);

        (void)adc_sequence_init_dt(&pot, &sequence);

        error = adc_read_dt(&pot, &sequence);
        if (error < 0) {
            LOG_WRN("Could not read (%d)\n", error);
            k_sleep(K_MSEC(1000));
            continue;
        }

        val_mv = pot.channel_cfg.differential
            ? (int32_t)((int16_t)adc_buf)
            : (int32_t)adc_buf;

        printk("%" PRId32, val_mv);

        error = adc_raw_to_millivolts_dt(&pot, &val_mv);
        if (error < 0) {
            printk(" (value in mV not available)\n");
        } else {
            printk(" = %" PRId32 " mV\n", val_mv);
        }

        k_sleep(K_MSEC(1000));
    }
}

int brake_init(void) {
    int error = 0;

    if (!device_is_ready(servo)) {
        LOG_ERR("Servo device is not ready");
        return -ENODEV;
    }

    if (!adc_is_ready_dt(&pot)) {
        LOG_ERR("Potentiometer ADC device is not ready");
        return -ENODEV;
    }

    error = adc_channel_setup_dt(&pot);
    if (error) {
        LOG_ERR("Could not setup ADC channel (%d)", error);
        return error;
    }

    if (brake_tid != NULL) {
        LOG_WRN("brake_init() called again, thread already running");
        return -EALREADY;
    }

    brake_tid = k_thread_create(
        &brake_thread_data,
        brake_thread_stack,
        K_THREAD_STACK_SIZEOF(brake_thread_stack),
        brake_thread,
        NULL,
        NULL,
        NULL,
        BRAKE_THREAD_PRIORITY,
        0,
        K_NO_WAIT
    );

    if (brake_tid == NULL) {
        LOG_ERR("Failed to create brake thread");
        return -ENOMEM;
    }

    k_thread_name_set(brake_tid, "brake_ctrl");
    LOG_INF("Brake thread started (tid=%p)", brake_tid);

    return 0;
}

int brake_stop(void) {
    if (brake_tid == NULL) {
        LOG_WRN("brake_stop() called but thread is not running");
        return -EINVAL;
    }

    k_thread_abort(brake_tid);

    int error = k_thread_join(brake_tid, K_MSEC(2000));
    if (error) {
        LOG_ERR("Thread did not stop cleanly (%d)", error);
        return error;
    }

    brake_tid = NULL;
    LOG_INF("Brake thread stopped");
    return 0;
}

int brake_set(int percentage) {
    return atomic_set(&desired_percentage, percentage);
}

int brake_get(void) {
    return (int)atomic_get(&desired_percentage);
}
