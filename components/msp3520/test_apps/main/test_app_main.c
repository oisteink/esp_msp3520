#include "unity.h"
#include "unity_test_utils.h"
#include "msp3520.h"

/* Shared handle — accessible from test files via extern */
msp3520_handle_t test_handle;

#define TEST_MEMORY_LEAK_THRESHOLD 450

void setUp(void)
{
    unity_utils_record_free_mem();
}

void tearDown(void)
{
    unity_utils_evaluate_leaks_direct(TEST_MEMORY_LEAK_THRESHOLD);
}

void app_main(void)
{
    /* Initialize the full MSP3520 component */
    msp3520_config_t cfg = MSP3520_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(msp3520_create(&cfg, &test_handle));

    unity_run_menu();
}
