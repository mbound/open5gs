/*
 * Standalone two-PLMN home-routed integration suite: N2 handover and
 * idle-mode mobility registration update.
 */
#include "test-app.h"

abts_suite *test_inter_plmn_hr_n2_handover(abts_suite *suite);
abts_suite *test_inter_plmn_idle_mobility(abts_suite *suite);

const struct testlist {
    abts_suite *(*func)(abts_suite *suite);
} alltests[] = {
    {test_inter_plmn_hr_n2_handover},
    {test_inter_plmn_idle_mobility},
    {NULL},
};

static void terminate(void)
{
    ogs_msleep(50);
    test_child_terminate();
    app_terminate();
    test_5gc_final();
    ogs_app_terminate();
}

static void initialize(const char *const argv[])
{
    int rv;

    rv = ogs_app_initialize(NULL, NULL, argv);
    ogs_assert(rv == OGS_OK);
    test_5gc_init();

    rv = app_initialize(argv);
    ogs_assert(rv == OGS_OK);
}

int main(int argc, const char *const argv[])
{
    int i;
    abts_suite *suite = NULL;

    atexit(terminate);
    test_app_run(argc, argv, "inter-plmn-hr-n2.yaml", initialize);

    for (i = 0; alltests[i].func; i++)
        suite = alltests[i].func(suite);

    return abts_report(suite);
}
