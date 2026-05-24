#include <zephyr/kernel.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/device.h>
#include <zephyr/drivers/pwm.h>
#include <zephyr/drivers/adc.h>

#include "brake.h"

#define BRAKE_THREAD_STACK_SIZE 2048
#define BRAKE_THREAD_PRIORITY   5

//K_THREAD_STACK_DEFINE(brake_thread_stack, BRAKE_THREAD_STACK_SIZE);
//static struct k_thread brake_thread_data;


/* desired brake percentage set with brake_set */
atomic_t desired_percentage = ATOMIC_INIT(0);


//static void brake_thread(void *arg1, void *arg2, void *arg3) {
//    return;
//}

int brake_init(void) {
    return 0;
}

int brake_set(int percentage) {
   return atomic_set(&desired_percentage, percentage);
}

int brake_get(void) {
   return (int) atomic_get(&desired_percentage);
}
