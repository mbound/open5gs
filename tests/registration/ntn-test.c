/*
 * Copyright (C) 2026 by Sukchan Lee <acetcom@gmail.com>
 *
 * This file is part of Open5GS.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Affero General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

/*
 * NR satellite access (TS 23.501 5.4.10)
 *
 * The gNB signals the RAT Information per Supported TA in NG Setup or
 * RAN Configuration Update (TS 38.413 9.3.1.125). The AMF derives the RAT
 * Type of the UE's serving TA and enforces the subscriber's RAT restrictions
 * (TS 29.503 ratRestrictions, provisioned as "rat_restrictions").
 */

#include "test-common.h"

static test_ue_t *ntn_test_ue_add(const char *scheme_output)
{
    ogs_nas_5gs_mobile_identity_suci_t mobile_identity_suci;
    test_ue_t *test_ue = NULL;

    memset(&mobile_identity_suci, 0, sizeof(mobile_identity_suci));

    mobile_identity_suci.h.supi_format = OGS_NAS_5GS_SUPI_FORMAT_IMSI;
    mobile_identity_suci.h.type = OGS_NAS_5GS_MOBILE_IDENTITY_SUCI;
    mobile_identity_suci.routing_indicator1 = 0;
    mobile_identity_suci.routing_indicator2 = 0xf;
    mobile_identity_suci.routing_indicator3 = 0xf;
    mobile_identity_suci.routing_indicator4 = 0xf;
    mobile_identity_suci.protection_scheme_id = OGS_PROTECTION_SCHEME_NULL;
    mobile_identity_suci.home_network_pki_value = 0;

    test_ue = test_ue_add_by_suci(&mobile_identity_suci, scheme_output);
    ogs_assert(test_ue);

    test_ue->nr_cgi.cell_id = 0x40001;

    test_ue->nas.registration.tsc = 0;
    test_ue->nas.registration.ksi = OGS_NAS_KSI_NO_KEY_IS_AVAILABLE;
    test_ue->nas.registration.follow_on_request = 1;
    test_ue->nas.registration.value = OGS_NAS_5GS_REGISTRATION_TYPE_INITIAL;

    test_ue->k_string = "465b5ce8b199b49faa5f0a2ee238a6bc";
    test_ue->opc_string = "e8ed289deba952e4283b54e88e6183ca";

    return test_ue;
}

static void ntn_db_insert_ue(abts_case *tc,
        test_ue_t *test_ue, const char *rat_restriction)
{
    bson_t *doc = test_db_new_simple(test_ue);
    ABTS_PTR_NOTNULL(tc, doc);
    BCON_APPEND(doc,
            "rat_restrictions", "[", BCON_UTF8(rat_restriction), "]");
    ABTS_INT_EQUAL(tc, OGS_OK, test_db_insert_ue(test_ue, doc));
}

static void ntn_ng_setup(abts_case *tc, ogs_socknode_t *ngap,
        test_ue_t *test_ue, long rat_information)
{
    int rv;
    ogs_pkbuf_t *sendbuf;
    ogs_pkbuf_t *recvbuf;

    /* Send NG-Setup Reqeust */
    if (rat_information >= 0)
        sendbuf = testngap_build_ng_setup_request_rat_information(
                0x4000, 22, rat_information);
    else
        sendbuf = testngap_build_ng_setup_request(0x4000, 22);
    ABTS_PTR_NOTNULL(tc, sendbuf);
    rv = testgnb_ngap_send(ngap, sendbuf);
    ABTS_INT_EQUAL(tc, OGS_OK, rv);

    /* Receive NG-Setup Response */
    recvbuf = testgnb_ngap_read(ngap);
    ABTS_PTR_NOTNULL(tc, recvbuf);
    testngap_recv(test_ue, recvbuf);
}

/*
 * Initial Registration up to Security Mode Complete. On return, the
 * AMF's answer (InitialContextSetupRequest + Registration Accept, or
 * Registration Reject) has been received.
 */
static void ntn_registration(abts_case *tc,
        ogs_socknode_t *ngap, test_ue_t *test_ue)
{
    int rv;
    ogs_pkbuf_t *gmmbuf;
    ogs_pkbuf_t *nasbuf;
    ogs_pkbuf_t *sendbuf;
    ogs_pkbuf_t *recvbuf;

    memset(&test_ue->registration_request_param, 0,
            sizeof(test_ue->registration_request_param));
    test_ue->registration_reject_cause = 0;
    test_ue->nas.registration.ksi = OGS_NAS_KSI_NO_KEY_IS_AVAILABLE;
    memset(&test_ue->mrl, 0, sizeof(test_ue->mrl));

    /* Send Registration request */
    test_ue->registration_request_param.guti = 1;
    gmmbuf = testgmm_build_registration_request(test_ue, NULL, false, false);
    ABTS_PTR_NOTNULL(tc, gmmbuf);

    test_ue->registration_request_param.gmm_capability = 1;
    test_ue->registration_request_param.s1_ue_network_capability = 1;
    test_ue->registration_request_param.requested_nssai = 1;
    test_ue->registration_request_param.last_visited_registered_tai = 1;
    test_ue->registration_request_param.ue_usage_setting = 1;
    nasbuf = testgmm_build_registration_request(test_ue, NULL, false, false);
    ABTS_PTR_NOTNULL(tc, nasbuf);

    sendbuf = testngap_build_initial_ue_message(test_ue, gmmbuf,
                NGAP_RRCEstablishmentCause_mo_Signalling, false, true);
    ABTS_PTR_NOTNULL(tc, sendbuf);
    rv = testgnb_ngap_send(ngap, sendbuf);
    ABTS_INT_EQUAL(tc, OGS_OK, rv);

    /* Receive Identity request */
    recvbuf = testgnb_ngap_read(ngap);
    ABTS_PTR_NOTNULL(tc, recvbuf);
    testngap_recv(test_ue, recvbuf);

    /* Send Identity response */
    gmmbuf = testgmm_build_identity_response(test_ue);
    ABTS_PTR_NOTNULL(tc, gmmbuf);
    sendbuf = testngap_build_uplink_nas_transport(test_ue, gmmbuf);
    ABTS_PTR_NOTNULL(tc, sendbuf);
    rv = testgnb_ngap_send(ngap, sendbuf);
    ABTS_INT_EQUAL(tc, OGS_OK, rv);

    /* Receive Authentication request */
    recvbuf = testgnb_ngap_read(ngap);
    ABTS_PTR_NOTNULL(tc, recvbuf);
    testngap_recv(test_ue, recvbuf);

    /* Send Authentication response */
    gmmbuf = testgmm_build_authentication_response(test_ue);
    ABTS_PTR_NOTNULL(tc, gmmbuf);
    sendbuf = testngap_build_uplink_nas_transport(test_ue, gmmbuf);
    ABTS_PTR_NOTNULL(tc, sendbuf);
    rv = testgnb_ngap_send(ngap, sendbuf);
    ABTS_INT_EQUAL(tc, OGS_OK, rv);

    /* Receive Security mode command */
    recvbuf = testgnb_ngap_read(ngap);
    ABTS_PTR_NOTNULL(tc, recvbuf);
    testngap_recv(test_ue, recvbuf);

    /* Send Security mode complete */
    gmmbuf = testgmm_build_security_mode_complete(test_ue, nasbuf);
    ABTS_PTR_NOTNULL(tc, gmmbuf);
    sendbuf = testngap_build_uplink_nas_transport(test_ue, gmmbuf);
    ABTS_PTR_NOTNULL(tc, sendbuf);
    rv = testgnb_ngap_send(ngap, sendbuf);
    ABTS_INT_EQUAL(tc, OGS_OK, rv);

    /* Receive InitialContextSetupRequest + Registration accept,
     * or Registration reject */
    recvbuf = testgnb_ngap_read(ngap);
    ABTS_PTR_NOTNULL(tc, recvbuf);
    testngap_recv(test_ue, recvbuf);
}

/*
 * Mobility Restriction List in InitialContextSetupRequest (TS 38.413
 * 9.3.1.85): RAT Restrictions for the Serving PLMN. RAT Restriction
 * Information has nR-LEO at bit 3 and nR-GEO at bit 5; Extended RAT
 * Restriction Information (9.3.1.126) is only needed for E-UTRA satellite
 * access, e.g. e-UTRA-GEO at primary bit 9 (16-bit bitmap).
 */
static void ntn_check_mobility_restriction_list(abts_case *tc,
        test_ue_t *test_ue, uint8_t rat_restriction, uint16_t extended)
{
    ABTS_TRUE(tc, test_ue->mrl.present);
    ABTS_TRUE(tc, memcmp(&test_ue->mrl.serving_plmn_id,
                &test_ue->nr_tai.plmn_id, OGS_PLMN_ID_LEN) == 0);
    ABTS_INT_EQUAL(tc, rat_restriction,
            test_ue->mrl.rat_restriction_information);
    ABTS_INT_EQUAL(tc, extended, test_ue->mrl.extended_primary);
    ABTS_INT_EQUAL(tc, 0, test_ue->mrl.extended_secondary);
}

/* Registration Reject is followed by the release of the NG association */
static void ntn_registration_rejected(abts_case *tc,
        ogs_socknode_t *ngap, test_ue_t *test_ue, uint8_t gmm_cause)
{
    int rv;
    ogs_pkbuf_t *sendbuf;
    ogs_pkbuf_t *recvbuf;

    ABTS_INT_EQUAL(tc,
            OGS_NAS_5GS_REGISTRATION_REJECT, test_ue->gmm_message_type);
    ABTS_INT_EQUAL(tc, gmm_cause, test_ue->registration_reject_cause);

    /* Receive UEContextReleaseCommand */
    recvbuf = testgnb_ngap_read(ngap);
    ABTS_PTR_NOTNULL(tc, recvbuf);
    testngap_recv(test_ue, recvbuf);
    ABTS_INT_EQUAL(tc,
            NGAP_ProcedureCode_id_UEContextRelease,
            test_ue->ngap_procedure_code);

    /* Send UEContextReleaseComplete */
    sendbuf = testngap_build_ue_context_release_complete(test_ue);
    ABTS_PTR_NOTNULL(tc, sendbuf);
    rv = testgnb_ngap_send(ngap, sendbuf);
    ABTS_INT_EQUAL(tc, OGS_OK, rv);
}

/* Complete an accepted Registration, then De-register (switch-off) */
static void ntn_registration_accepted(abts_case *tc,
        ogs_socknode_t *ngap, test_ue_t *test_ue)
{
    int rv;
    ogs_pkbuf_t *gmmbuf;
    ogs_pkbuf_t *sendbuf;
    ogs_pkbuf_t *recvbuf;

    ABTS_INT_EQUAL(tc,
            NGAP_ProcedureCode_id_InitialContextSetup,
            test_ue->ngap_procedure_code);
    ABTS_INT_EQUAL(tc,
            OGS_NAS_5GS_REGISTRATION_ACCEPT, test_ue->gmm_message_type);

    /* Send UERadioCapabilityInfoIndication */
    sendbuf = testngap_build_ue_radio_capability_info_indication(test_ue);
    ABTS_PTR_NOTNULL(tc, sendbuf);
    rv = testgnb_ngap_send(ngap, sendbuf);
    ABTS_INT_EQUAL(tc, OGS_OK, rv);

    /* Send InitialContextSetupResponse */
    sendbuf = testngap_build_initial_context_setup_response(test_ue, false);
    ABTS_PTR_NOTNULL(tc, sendbuf);
    rv = testgnb_ngap_send(ngap, sendbuf);
    ABTS_INT_EQUAL(tc, OGS_OK, rv);

    /* Send Registration complete */
    gmmbuf = testgmm_build_registration_complete(test_ue);
    ABTS_PTR_NOTNULL(tc, gmmbuf);
    sendbuf = testngap_build_uplink_nas_transport(test_ue, gmmbuf);
    ABTS_PTR_NOTNULL(tc, sendbuf);
    rv = testgnb_ngap_send(ngap, sendbuf);
    ABTS_INT_EQUAL(tc, OGS_OK, rv);

    /* Receive Configuration update command */
    recvbuf = testgnb_ngap_read(ngap);
    ABTS_PTR_NOTNULL(tc, recvbuf);
    testngap_recv(test_ue, recvbuf);

    /* Send De-registration request */
    gmmbuf = testgmm_build_de_registration_request(test_ue, 1, true, true);
    ABTS_PTR_NOTNULL(tc, gmmbuf);
    sendbuf = testngap_build_uplink_nas_transport(test_ue, gmmbuf);
    ABTS_PTR_NOTNULL(tc, sendbuf);
    rv = testgnb_ngap_send(ngap, sendbuf);
    ABTS_INT_EQUAL(tc, OGS_OK, rv);

    /* Receive UEContextReleaseCommand */
    recvbuf = testgnb_ngap_read(ngap);
    ABTS_PTR_NOTNULL(tc, recvbuf);
    testngap_recv(test_ue, recvbuf);
    ABTS_INT_EQUAL(tc,
            NGAP_ProcedureCode_id_UEContextRelease,
            test_ue->ngap_procedure_code);

    /* Send UEContextReleaseComplete */
    sendbuf = testngap_build_ue_context_release_complete(test_ue);
    ABTS_PTR_NOTNULL(tc, sendbuf);
    rv = testgnb_ngap_send(ngap, sendbuf);
    ABTS_INT_EQUAL(tc, OGS_OK, rv);

    ogs_msleep(300);
}

/* NR(LEO) TA, NR(LEO) restricted: Registration Reject #15 */
static void test1_func(abts_case *tc, void *data)
{
    ogs_socknode_t *ngap;
    test_ue_t *test_ue = ntn_test_ue_add("0000203190");

    ngap = testngap_client(1, AF_INET);
    ABTS_PTR_NOTNULL(tc, ngap);

    ntn_ng_setup(tc, ngap, test_ue, NGAP_RAT_Information_nR_LEO);
    ntn_db_insert_ue(tc, test_ue, "NR_LEO");

    ntn_registration(tc, ngap, test_ue);
    ntn_registration_rejected(tc, ngap, test_ue,
            OGS_5GMM_CAUSE_NO_SUITABLE_CELLS_IN_TRACKING_AREA);
    /*
     * The reject follows Security Mode Complete: secure exchange of NAS
     * messages is established, so it is integrity protected
     * (TS 24.501 4.4.4.1) or the UE discards it (TS 24.501 4.4.4.2).
     */
    ABTS_INT_EQUAL(tc,
            OGS_NAS_SECURITY_HEADER_INTEGRITY_PROTECTED_AND_CIPHERED,
            test_ue->gmm_security_header_type);

    ogs_msleep(300);

    ABTS_INT_EQUAL(tc, OGS_OK, test_db_remove_ue(test_ue));
    testgnb_ngap_close(ngap);
    test_ue_remove(test_ue);
}

/*
 * NR(LEO) TA, only NR(GEO) restricted: the RAT Type is NR(LEO), accepted;
 * the NG-RAN gets the NR(GEO) restriction in the Mobility Restriction List
 */
static void test2_func(abts_case *tc, void *data)
{
    ogs_socknode_t *ngap;
    test_ue_t *test_ue = ntn_test_ue_add("0000203190");

    ngap = testngap_client(1, AF_INET);
    ABTS_PTR_NOTNULL(tc, ngap);

    ntn_ng_setup(tc, ngap, test_ue, NGAP_RAT_Information_nR_LEO);
    ntn_db_insert_ue(tc, test_ue, "NR_GEO");

    ntn_registration(tc, ngap, test_ue);
    ntn_check_mobility_restriction_list(tc, test_ue, 0x80 >> 5, 0);
    ntn_registration_accepted(tc, ngap, test_ue);

    ABTS_INT_EQUAL(tc, OGS_OK, test_db_remove_ue(test_ue));
    testgnb_ngap_close(ngap);
    test_ue_remove(test_ue);
}

/*
 * Terrestrial TA (no RAT Information), NR(LEO) restricted: accepted, with
 * the NR(LEO) restriction in the Mobility Restriction List.
 * The gNB then reports the TA as NR(LEO) in RAN Configuration Update and
 * the next Registration is rejected with #15 from the stored subscription.
 */
static void test3_func(abts_case *tc, void *data)
{
    int rv;
    ogs_socknode_t *ngap;
    ogs_pkbuf_t *gmmbuf;
    ogs_pkbuf_t *sendbuf;
    ogs_pkbuf_t *recvbuf;
    test_ue_t *test_ue = ntn_test_ue_add("0000203190");

    ngap = testngap_client(1, AF_INET);
    ABTS_PTR_NOTNULL(tc, ngap);

    ntn_ng_setup(tc, ngap, test_ue, -1);
    ntn_db_insert_ue(tc, test_ue, "NR_LEO");

    ntn_registration(tc, ngap, test_ue);
    ntn_check_mobility_restriction_list(tc, test_ue, 0x80 >> 3, 0);
    ntn_registration_accepted(tc, ngap, test_ue);

    /* Send RANConfigurationUpdate */
    sendbuf = testngap_build_ran_configuration_update_rat_information(
            NGAP_RAT_Information_nR_LEO);
    ABTS_PTR_NOTNULL(tc, sendbuf);
    rv = testgnb_ngap_send(ngap, sendbuf);
    ABTS_INT_EQUAL(tc, OGS_OK, rv);

    /* Receive RANConfigurationUpdateAcknowledge */
    recvbuf = testgnb_ngap_read(ngap);
    ABTS_PTR_NOTNULL(tc, recvbuf);
    ogs_pkbuf_free(recvbuf);

    /*
     * Send Registration request: the AMF still holds the subscription
     * and rejects the UE right away, now that its TA is NR(LEO)
     */
    memset(&test_ue->registration_request_param, 0,
            sizeof(test_ue->registration_request_param));
    test_ue->registration_reject_cause = 0;
    test_ue->registration_request_param.guti = 1;
    gmmbuf = testgmm_build_registration_request(test_ue, NULL, false, false);
    ABTS_PTR_NOTNULL(tc, gmmbuf);
    sendbuf = testngap_build_initial_ue_message(test_ue, gmmbuf,
                NGAP_RRCEstablishmentCause_mo_Signalling, false, true);
    ABTS_PTR_NOTNULL(tc, sendbuf);
    rv = testgnb_ngap_send(ngap, sendbuf);
    ABTS_INT_EQUAL(tc, OGS_OK, rv);

    /* Receive Registration reject */
    recvbuf = testgnb_ngap_read(ngap);
    ABTS_PTR_NOTNULL(tc, recvbuf);
    testngap_recv(test_ue, recvbuf);

    ntn_registration_rejected(tc, ngap, test_ue,
            OGS_5GMM_CAUSE_NO_SUITABLE_CELLS_IN_TRACKING_AREA);
    /*
     * The Registration Request opened a new NAS signalling connection
     * without integrity protection, so no secure exchange is established on
     * it and the reject goes out plain, although the AMF still holds the
     * UE's security context (TS 24.501 4.4.4.2 e).
     */
    ABTS_INT_EQUAL(tc, OGS_NAS_SECURITY_HEADER_PLAIN_NAS_MESSAGE,
            test_ue->gmm_security_header_type);

    ogs_msleep(300);

    ABTS_INT_EQUAL(tc, OGS_OK, test_db_remove_ue(test_ue));
    testgnb_ngap_close(ngap);
    test_ue_remove(test_ue);
}

/*
 * Terrestrial TA, WB_E_UTRAN_GEO restricted: accepted; only the Extended RAT
 * Restriction Information can carry it (e-UTRA-GEO, primary bit 9)
 */
static void test4_func(abts_case *tc, void *data)
{
    ogs_socknode_t *ngap;
    test_ue_t *test_ue = ntn_test_ue_add("0000203190");

    ngap = testngap_client(1, AF_INET);
    ABTS_PTR_NOTNULL(tc, ngap);

    ntn_ng_setup(tc, ngap, test_ue, -1);
    ntn_db_insert_ue(tc, test_ue, "WB_E_UTRAN_GEO");

    ntn_registration(tc, ngap, test_ue);
    ntn_check_mobility_restriction_list(tc, test_ue, 0, 0x8000 >> 9);
    ntn_registration_accepted(tc, ngap, test_ue);

    ABTS_INT_EQUAL(tc, OGS_OK, test_db_remove_ue(test_ue));
    testgnb_ngap_close(ngap);
    test_ue_remove(test_ue);
}

abts_suite *test_ntn(abts_suite *suite)
{
    suite = ADD_SUITE(suite)

    abts_run_test(suite, test1_func, NULL);
    abts_run_test(suite, test2_func, NULL);
    abts_run_test(suite, test3_func, NULL);
    abts_run_test(suite, test4_func, NULL);

    return suite;
}
