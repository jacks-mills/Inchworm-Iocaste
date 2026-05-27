#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/device.h>
#include <zephyr/drivers/pwm.h>
#include <zephyr/drivers/adc.h>
#include "brake.h"

LOG_MODULE_REGISTER(brake_controller, LOG_LEVEL_NONE);

#define BRAKE_THREAD_STACK_SIZE 2048
#define BRAKE_THREAD_PRIORITY   5

K_THREAD_STACK_DEFINE(brake_thread_stack, BRAKE_THREAD_STACK_SIZE);
static struct k_thread brake_thread_data;
static k_tid_t brake_tid = NULL;

#define PWM_NODE    DT_NODELABEL(pwm0)
#define PWM_CHANNEL 0
#define PWM_PERIOD  PWM_MSEC(20)

#define MILLIVOLTS_TO_PERCENTAGE_MAX 1700
#define MILLIVOLTS_TO_PERCENTAGE_MIN 950

static const struct device *const servo = DEVICE_DT_GET(PWM_NODE);

static const struct adc_dt_spec pot = ADC_DT_SPEC_GET(DT_PATH(zephyr_user));

/* desired brake percentage set with brake_set */
static atomic_t desired_percentage = ATOMIC_INIT(0);
static atomic_t actual_percentage = ATOMIC_INIT(0);

static uint32_t percentage_to_pulse(int pct)
{
    return (pct != 0) ? PWM_PERIOD / 20 : 0;
}

static uint32_t millivolts_to_percentage(uint32_t millivolts)
{
    uint32_t ret = 0;
    LOG_INF("Recieved: %i\n", millivolts);

    if (millivolts <= MILLIVOLTS_TO_PERCENTAGE_MIN) {
        ret = 0;
        LOG_INF("ret: %i", ret);
        return ret;
    }

    if (millivolts >= MILLIVOLTS_TO_PERCENTAGE_MAX) {
        ret = 100;
        LOG_INF("ret: %i", ret);
        return ret;
    }

    ret = ((millivolts - MILLIVOLTS_TO_PERCENTAGE_MIN) * 100) / (MILLIVOLTS_TO_PERCENTAGE_MAX - MILLIVOLTS_TO_PERCENTAGE_MIN);
    LOG_INF("ret: %i", ret);
    return ret;
}

static void brake_thread(void *arg1, void *arg2, void *arg3)
{
    int error = 0;
    uint32_t count = 0;
    uint16_t adc_buf;
    struct adc_sequence sequence = {
        .buffer      = &adc_buf,
        .buffer_size = sizeof(adc_buf),
    };

    (void)adc_sequence_init_dt(&pot, &sequence);

    while (1) {
        int32_t val_raw = 0, val_mv = 0;
        int brake_pct = (int)atomic_get(&desired_percentage);

        LOG_INF("ADC reading[%u]: %s ch%d",
                count++, pot.dev->name, pot.channel_id);

        error = adc_read_dt(&pot, &sequence);
        if (error < 0) {
            LOG_WRN("Could not read ADC (%d)", error);
            k_sleep(K_MSEC(100));
            continue;
        }

        val_raw = pot.channel_cfg.differential
                ? (int32_t)((int16_t)adc_buf)
                : (int32_t)adc_buf;
        val_mv = val_raw;

        error = adc_raw_to_millivolts_dt(&pot, &val_mv);
        if (error < 0) {
            LOG_INF("raw=%" PRId32 " (mV unavailable)", val_raw);
        } else {
            LOG_INF("raw=%" PRId32 " = %" PRId32 " mV", val_raw, val_mv);
        }

        uint32_t pulse = percentage_to_pulse(brake_pct);
        (void) atomic_set(&actual_percentage, (atomic_val_t)millivolts_to_percentage(val_mv));

        error = pwm_set(servo, PWM_CHANNEL, PWM_PERIOD, pulse, 0);
        if (error) {
            LOG_ERR("pwm_set failed [%d]", error);
        }

        k_sleep(K_MSEC(100));
    }
}

int brake_init(void)
{
    int error = 0;

    if (!device_is_ready(servo)) {
        LOG_ERR("PWM servo device is not ready");
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
        LOG_WRN("brake_init() called again – thread already running");
        return -EALREADY;
    }

    brake_tid = k_thread_create(
        &brake_thread_data,
        brake_thread_stack,
        K_THREAD_STACK_SIZEOF(brake_thread_stack),
        brake_thread,
        NULL, NULL, NULL,
        BRAKE_THREAD_PRIORITY, 0, K_NO_WAIT);

    if (brake_tid == NULL) {
        LOG_ERR("Failed to create brake thread");
        return -ENOMEM;
    }

    k_thread_name_set(brake_tid, "brake_ctrl");
    LOG_INF("Brake thread started (tid=%p)", brake_tid);

    return 0;
}

int brake_stop(void)
{
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

int brake_set(int percentage)
{
    return (int)atomic_set(&desired_percentage, (atomic_val_t)percentage);
}

int brake_get(void)
{
    return (int)atomic_get(&actual_percentage);
}
