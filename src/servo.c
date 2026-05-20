#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>
#include <zephyr/device.h>
#include <zephyr/drivers/pwm.h>
#include 

static const struct pwm_dt_spec servo = PWM_DT_SPEC_GET(DT_NODELABEL(servo));
static const uint32_t min_pulse = DT_PROP(DT_NODELABEL(servo), min_pulse);
static const uint32_t max_pulse = DT_PROP(DT_NODELABEL(servo), max_pulse);

#define STEP PWM_USEC(100)

enum direction {
    CLOCK, 
    COUNTER,
};

int main(void) 
{
    uint32_t pulse_width = min_pulse;
    enum direction dir = UP; 
    int ret; 

    printk("Servomotor control\n");

    if (!pwm_is_ready_dt(&servo)) {
        printk("Error: PWM device %s is not ready\n", servo.dev->name);
        return 0; 
    }

    return 0;
}