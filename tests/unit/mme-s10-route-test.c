/*
 * Copyright (C) 2026 by Open5GS contributors
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */
#include "mme/mme-context.h"
#include "core/abts.h"

/* Peer selection is exact on both PLMN (including 2- vs 3-digit MNC)
 * and 16-bit LTE TAC. There is deliberately no fallback peer. */
static void s10_peer_routing_test(abts_case *tc, void *data)
{
    mme_s10_peer_t first = {0}, second = {0};
    mme_context_t *mme = mme_self();
    ogs_eps_tai_t target = {0};

    ogs_list_init(&mme->s10_peer_list);
    first.num_of_tai = 2;
    ogs_plmn_id_build(&first.tai[0].plmn_id, 999, 70, 2);
    first.tai[0].tac = 2;
    ogs_plmn_id_build(&first.tai[1].plmn_id, 999, 70, 2);
    first.tai[1].tac = 3;

    second.num_of_tai = 1;
    ogs_plmn_id_build(&second.tai[0].plmn_id, 999, 70, 3);
    second.tai[0].tac = 2;
    ogs_list_add(&mme->s10_peer_list, &first);
    ogs_list_add(&mme->s10_peer_list, &second);

    target = first.tai[0];
    ABTS_PTR_EQUAL(tc, &first, mme_s10_peer_find_by_tai(&target));

    target = first.tai[1];
    ABTS_PTR_EQUAL(tc, &first, mme_s10_peer_find_by_tai(&target));

    target = second.tai[0];
    ABTS_PTR_EQUAL(tc, &second, mme_s10_peer_find_by_tai(&target));

    /* Same MCC and numeric MNC, but different MNC digit length. */
    target.tac = 3;
    ABTS_PTR_EQUAL(tc, NULL, mme_s10_peer_find_by_tai(&target));

    /* Missing route must NEVER select an arbitrary configured peer. */
    ogs_plmn_id_build(&target.plmn_id, 999, 71, 2);
    target.tac = 2;
    ABTS_PTR_EQUAL(tc, NULL, mme_s10_peer_find_by_tai(&target));
    ABTS_PTR_EQUAL(tc, NULL, mme_s10_peer_find_by_tai(NULL));

    ogs_list_remove(&mme->s10_peer_list, &second);
    ogs_list_remove(&mme->s10_peer_list, &first);
    ABTS_TRUE(tc, ogs_list_empty(&mme->s10_peer_list));
}

abts_suite *test_mme_s10_route(abts_suite *suite)
{
    suite = ADD_SUITE(suite)
    abts_run_test(suite, s10_peer_routing_test, NULL);
    return suite;
}
