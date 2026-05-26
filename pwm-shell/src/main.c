#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/device.h>
#include <zephyr/drivers/pwm.h>
#include <zephyr/shell/shell.h>
#include <stdlib.h>

LOG_MODULE_REGISTER(pwm_shell, LOG_LEVEL_INF);

#define PWM_NODE    DT_NODELABEL(ledc0)
#define PWM_CHANNEL 0
#define PWM_PERIOD  PWM_USEC(2000)  /* 500 Hz */

static const struct device *pwm_dev = DEVICE_DT_GET(PWM_NODE);
static uint32_t current_duty_cycle = 0; /* 0–100 % */

/* ------------------------------------------------------------------ */
/*  helpers                                                            */
/* ------------------------------------------------------------------ */

static int apply_duty(const struct shell *sh, uint32_t percent)
{
    if (percent > 100) {
        shell_error(sh, "Duty cycle must be 0–100 (got %u)", percent);
        return -EINVAL;
    }

    uint32_t pulse = (PWM_PERIOD / 100) * percent;

    int err = pwm_set(pwm_dev, PWM_CHANNEL, PWM_PERIOD, pulse, 0);
    if (err) {
        shell_error(sh, "pwm_set() failed: %d", err);
        return err;
    }

    current_duty_cycle = percent;
    shell_print(sh, "PWM duty cycle set to %u%% (pulse = %u ns)", percent, pulse);
    return 0;
}

/* ------------------------------------------------------------------ */
/*  shell commands                                                     */
/* ------------------------------------------------------------------ */

/* pwm set <0–100> */
static int cmd_pwm_set(const struct shell *sh, size_t argc, char **argv)
{
    char *end;
    long percent = strtol(argv[1], &end, 10);

    if (*end != '\0' || percent < 0 || percent > 100) {
        shell_error(sh, "Invalid value '%s'. Provide an integer 0–100.", argv[1]);
        return -EINVAL;
    }

    return apply_duty(sh, (uint32_t)percent);
}

/* pwm get */
static int cmd_pwm_get(const struct shell *sh, size_t argc, char **argv)
{
    ARG_UNUSED(argc);
    ARG_UNUSED(argv);

    uint32_t pulse = (PWM_PERIOD / 100) * current_duty_cycle;
    shell_print(sh, "Current duty cycle: %u%% (pulse = %u ns, period = %u ns)",
                current_duty_cycle, pulse, PWM_PERIOD);
    return 0;
}

/* pwm off */
static int cmd_pwm_off(const struct shell *sh, size_t argc, char **argv)
{
    ARG_UNUSED(argc);
    ARG_UNUSED(argv);

    int err = pwm_set(pwm_dev, PWM_CHANNEL, PWM_PERIOD, 0, 0);
    if (err) {
        shell_error(sh, "pwm_set() failed: %d", err);
        return err;
    }

    current_duty_cycle = 0;
    shell_print(sh, "PWM output disabled (0%% duty cycle)");
    return 0;
}

/* pwm sweep <start_%> <end_%> <step_%> <delay_ms> */
static int cmd_pwm_sweep(const struct shell *sh, size_t argc, char **argv)
{
    long start   = strtol(argv[1], NULL, 10);
    long end_pct = strtol(argv[2], NULL, 10);
    long step    = strtol(argv[3], NULL, 10);
    long delay   = strtol(argv[4], NULL, 10);

    if (start < 0 || start > 100 || end_pct < 0 || end_pct > 100 ||
        step <= 0 || delay < 0) {
        shell_error(sh, "Usage: pwm sweep <start 0-100> <end 0-100> "
                        "<step 1-100> <delay_ms>");
        return -EINVAL;
    }

    shell_print(sh, "Sweeping %ld%% → %ld%% step %ld%% every %ld ms",
                start, end_pct, step, delay);

    long dir = (end_pct >= start) ? 1 : -1;

    for (long pct = start;
         dir > 0 ? pct <= end_pct : pct >= end_pct;
         pct += dir * step)
    {
        int err = apply_duty(sh, (uint32_t)pct);
        if (err) {
            return err;
        }
        k_sleep(K_MSEC(delay));
    }

    /* Ensure we land exactly on the end value */
    return apply_duty(sh, (uint32_t)end_pct);
}

/* ------------------------------------------------------------------ */
/*  sub-command registration                                           */
/* ------------------------------------------------------------------ */

SHELL_STATIC_SUBCMD_SET_CREATE(pwm_cmds,
    SHELL_CMD_ARG(set,   NULL,
        "Set PWM duty cycle.\n"
        "Usage: pwm set <percent (0-100)>",
        cmd_pwm_set, 2, 0),
    SHELL_CMD_ARG(get,   NULL,
        "Get current PWM duty cycle.\n"
        "Usage: pwm get",
        cmd_pwm_get, 1, 0),
    SHELL_CMD_ARG(off,   NULL,
        "Disable PWM output (0%% duty cycle).\n"
        "Usage: pwm off",
        cmd_pwm_off, 1, 0),
    SHELL_CMD_ARG(sweep, NULL,
        "Sweep duty cycle over a range.\n"
        "Usage: pwm sweep <start_%> <end_%> <step_%> <delay_ms>\n"
        "Example: pwm sweep 0 100 5 100",
        cmd_pwm_sweep, 5, 0),
    SHELL_SUBCMD_SET_END
);

SHELL_CMD_REGISTER(pwm, &pwm_cmds, "PWM duty cycle control commands", NULL);

/* ------------------------------------------------------------------ */
/*  initialisation                                                     */
/* ------------------------------------------------------------------ */

static int pwm_shell_init(void)
{
    if (!device_is_ready(pwm_dev)) {
        LOG_ERR("PWM device not ready");
        return -ENODEV;
    }

    /* Start with output off */
    int err = pwm_set(pwm_dev, PWM_CHANNEL, PWM_PERIOD, 0, 0);
    if (err) {
        LOG_ERR("Failed to initialise PWM output: %d", err);
        return err;
    }

    LOG_INF("PWM shell ready — channel %d, period %u ns", PWM_CHANNEL, PWM_PERIOD);
    return 0;
}

SYS_INIT(pwm_shell_init, APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY);
