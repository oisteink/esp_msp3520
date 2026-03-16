#pragma once

#include <stdbool.h>
#include <stdint.h>

/* Reboot test phases — stored in RTC slow memory */
enum reboot_test_phase {
    REBOOT_NONE = 0,       /* no reboot test pending */
    REBOOT_FADEIN = 1,     /* expecting fade-in observation on next boot */
};

/* Reboot test results */
enum reboot_test_result {
    REBOOT_RESULT_NONE = 0,
    REBOOT_RESULT_PASS = 1,
    REBOOT_RESULT_FAIL = 2,
};

/* Call from app_main after component init, before unity_run_menu().
 * Returns true if a reboot test was executed (results stored in RTC). */
bool reboot_tests_run(void);

/* Read and clear the stored result for a given phase.
 * Returns REBOOT_RESULT_NONE if no result is stored for that phase. */
uint8_t reboot_test_get_result(uint8_t phase);

/* Set RTC flag and reboot. Does not return. */
void reboot_test_trigger(uint8_t phase);
