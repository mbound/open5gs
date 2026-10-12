/*
 * Inter-PLMN idle-mode mobility registration update with N14 context transfer.
 *
 * A UE of HPLMN 999-70 registers through gNB1 (999-70, TAC 1, AMF-h) and
 * goes idle. It then appears on gNB2 (001-01, TAC 2, AMF-v) with a
 * Registration Request of type "mobility registration updating" and its
 * 5G-GUTI from AMF-h (TS 24.501 5.5.1.3.2). AMF-v finds the old AMF from
 * the GUAMI of that 5G-GUTI in the other PLMN and fetches the UE context
 * through the SEPPs (TS 23.502 4.2.2.2.2 steps 4-5, TS 29.518
 * Namf_Communication_UEContextTransfer), so it knows the SUPI and goes
 * straight to authentication. Without the context it would have to ask for
 * the identity (steps 6-7). The UE then goes idle again and returns to gNB1
 * with the 5G-GUTI of AMF-v: the same in the other direction.
 */

#include "test-common.h"

static void inter_plmn_idle_mobility_func(abts_case *tc, void *data)
{
    int rv;
    ogs_socknode_t *ngap1, *ngap2;
    ogs_pkbuf_t *gmmbuf;
    ogs_pkbuf_t *nasbuf;
    ogs_pkbuf_t *sendbuf;
    ogs_pkbuf_t *recvbuf;

    ogs_nas_5gs_mobile_identity_suci_t mobile_identity_suci;
    test_ue_t *test_ue = NULL;
    ogs_5gs_tai_t target_tai;

    bson_t *doc = NULL;

    memset(&mobile_identity_suci, 0, sizeof(mobile_identity_suci));

    mobile_identity_suci.h.supi_format = OGS_NAS_5GS_SUPI_FORMAT_IMSI;
    mobile_identity_suci.h.type = OGS_NAS_5GS_MOBILE_IDENTITY_SUCI;
    mobile_identity_suci.routing_indicator1 = 0;
    mobile_identity_suci.routing_indicator2 = 0xf;
    mobile_identity_suci.routing_indicator3 = 0xf;
    mobile_identity_suci.routing_indicator4 = 0xf;
    mobile_identity_suci.protection_scheme_id = OGS_PROTECTION_SCHEME_NULL;
    mobile_identity_suci.home_network_pki_value = 0;

    test_ue = test_ue_add_by_suci(&mobile_identity_suci, "0000000003");
    ogs_assert(test_ue);

    test_ue->nr_cgi.cell_id = 0x40001;

    test_ue->nas.registration.tsc = 0;
    test_ue->nas.registration.ksi = OGS_NAS_KSI_NO_KEY_IS_AVAILABLE;
    test_ue->nas.registration.follow_on_request = 1;
    test_ue->nas.registration.value = OGS_NAS_5GS_REGISTRATION_TYPE_INITIAL;

    test_ue->k_string = "465b5ce8b199b49faa5f0a2ee238a6bc";
    test_ue->opc_string = "e8ed289deba952e4283b54e88e6183ca";

    ngap1 = testngap_client(1, AF_INET);
    ABTS_PTR_NOTNULL(tc, ngap1);
    ngap2 = testngap_client(2, AF_INET);
    ABTS_PTR_NOTNULL(tc, ngap2);

    /* NG Setup: gNB1 999-70 TAC 1 (AMF-h), gNB2 001-01 TAC 2 (AMF-v) */
    sendbuf = testngap_build_ng_setup_request_by_tai_plmn(0x4000, 28, 0, 0);
    ABTS_PTR_NOTNULL(tc, sendbuf);
    rv = testgnb_ngap_send(ngap1, sendbuf);
    ABTS_INT_EQUAL(tc, OGS_OK, rv);
    recvbuf = testgnb_ngap_read(ngap1);
    ABTS_PTR_NOTNULL(tc, recvbuf);
    testngap_recv(test_ue, recvbuf);

    sendbuf = testngap_build_ng_setup_request_by_tai_plmn(0x4001, 28, 1, 1);
    ABTS_PTR_NOTNULL(tc, sendbuf);
    rv = testgnb_ngap_send(ngap2, sendbuf);
    ABTS_INT_EQUAL(tc, OGS_OK, rv);
    recvbuf = testgnb_ngap_read(ngap2);
    ABTS_PTR_NOTNULL(tc, recvbuf);
    testngap_recv(test_ue, recvbuf);

    /********** Insert Subscriber in Database */
    doc = test_db_new_simple(test_ue);
    ABTS_PTR_NOTNULL(tc, doc);
    ABTS_INT_EQUAL(tc, OGS_OK, test_db_insert_ue(test_ue, doc));

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
    rv = testgnb_ngap_send(ngap1, sendbuf);
    ABTS_INT_EQUAL(tc, OGS_OK, rv);

    /* Receive Identity request */
    recvbuf = testgnb_ngap_read(ngap1);
    ABTS_PTR_NOTNULL(tc, recvbuf);
    testngap_recv(test_ue, recvbuf);

    /* Send Identity response */
    gmmbuf = testgmm_build_identity_response(test_ue);
    ABTS_PTR_NOTNULL(tc, gmmbuf);
    sendbuf = testngap_build_uplink_nas_transport(test_ue, gmmbuf);
    ABTS_PTR_NOTNULL(tc, sendbuf);
    rv = testgnb_ngap_send(ngap1, sendbuf);
    ABTS_INT_EQUAL(tc, OGS_OK, rv);

    /* Receive Authentication request */
    recvbuf = testgnb_ngap_read(ngap1);
    ABTS_PTR_NOTNULL(tc, recvbuf);
    testngap_recv(test_ue, recvbuf);

    /* Send Authentication response */
    gmmbuf = testgmm_build_authentication_response(test_ue);
    ABTS_PTR_NOTNULL(tc, gmmbuf);
    sendbuf = testngap_build_uplink_nas_transport(test_ue, gmmbuf);
    ABTS_PTR_NOTNULL(tc, sendbuf);
    rv = testgnb_ngap_send(ngap1, sendbuf);
    ABTS_INT_EQUAL(tc, OGS_OK, rv);

    /* Receive Security mode command */
    recvbuf = testgnb_ngap_read(ngap1);
    ABTS_PTR_NOTNULL(tc, recvbuf);
    testngap_recv(test_ue, recvbuf);

    /* Send Security mode complete */
    gmmbuf = testgmm_build_security_mode_complete(test_ue, nasbuf);
    ABTS_PTR_NOTNULL(tc, gmmbuf);
    sendbuf = testngap_build_uplink_nas_transport(test_ue, gmmbuf);
    ABTS_PTR_NOTNULL(tc, sendbuf);
    rv = testgnb_ngap_send(ngap1, sendbuf);
    ABTS_INT_EQUAL(tc, OGS_OK, rv);

    /* Receive InitialContextSetupRequest +
     * Registration accept */
    recvbuf = testgnb_ngap_read(ngap1);
    ABTS_PTR_NOTNULL(tc, recvbuf);
    testngap_recv(test_ue, recvbuf);
    ABTS_INT_EQUAL(tc,
            NGAP_ProcedureCode_id_InitialContextSetup,
            test_ue->ngap_procedure_code);

    /* Send UERadioCapabilityInfoIndication */
    sendbuf = testngap_build_ue_radio_capability_info_indication(test_ue);
    ABTS_PTR_NOTNULL(tc, sendbuf);
    rv = testgnb_ngap_send(ngap1, sendbuf);
    ABTS_INT_EQUAL(tc, OGS_OK, rv);

    /* Send InitialContextSetupResponse */
    sendbuf = testngap_build_initial_context_setup_response(test_ue, false);
    ABTS_PTR_NOTNULL(tc, sendbuf);
    rv = testgnb_ngap_send(ngap1, sendbuf);
    ABTS_INT_EQUAL(tc, OGS_OK, rv);

    /* Send Registration complete */
    gmmbuf = testgmm_build_registration_complete(test_ue);
    ABTS_PTR_NOTNULL(tc, gmmbuf);
    sendbuf = testngap_build_uplink_nas_transport(test_ue, gmmbuf);
    ABTS_PTR_NOTNULL(tc, sendbuf);
    rv = testgnb_ngap_send(ngap1, sendbuf);
    ABTS_INT_EQUAL(tc, OGS_OK, rv);

    /* Receive Configuration update command */
    recvbuf = testgnb_ngap_read(ngap1);
    ABTS_PTR_NOTNULL(tc, recvbuf);
    testngap_recv(test_ue, recvbuf);

    /* The UE goes idle: UE context release in gNB1 */
    sendbuf = testngap_build_ue_context_release_request(test_ue,
            NGAP_Cause_PR_radioNetwork,
            NGAP_CauseRadioNetwork_user_inactivity, false);
    ABTS_PTR_NOTNULL(tc, sendbuf);
    rv = testgnb_ngap_send(ngap1, sendbuf);
    ABTS_INT_EQUAL(tc, OGS_OK, rv);

    recvbuf = testgnb_ngap_read(ngap1);
    ABTS_PTR_NOTNULL(tc, recvbuf);
    testngap_recv(test_ue, recvbuf);
    ABTS_INT_EQUAL(tc, NGAP_ProcedureCode_id_UEContextRelease,
            test_ue->ngap_procedure_code);

    sendbuf = testngap_build_ue_context_release_complete(test_ue);
    ABTS_PTR_NOTNULL(tc, sendbuf);
    rv = testgnb_ngap_send(ngap1, sendbuf);
    ABTS_INT_EQUAL(tc, OGS_OK, rv);

    ogs_msleep(100);

    /* The idle UE camps on gNB2: VPLMN 001-01, TAC 2 */
    target_tai = test_ue->nr_tai;
    memcpy(&target_tai.plmn_id,
            &test_self()->plmn_support[1].plmn_id, OGS_PLMN_ID_LEN);
    target_tai.tac.v = 2;
    test_ue->nr_tai = target_tai;
    memcpy(&test_ue->nr_cgi.plmn_id, &target_tai.plmn_id, OGS_PLMN_ID_LEN);
    test_ue->nr_cgi.cell_id = 0x40011;

    /*
     * Mobility registration update with the 5G-GUTI of AMF-h. The UE uses
     * 5G-EA0 and selected a PLMN that is neither registered nor equivalent,
     * so it sends cleartext IEs only, unprotected (TS 24.501 4.4.6); the
     * complete message follows in the SMC complete.
     */
    test_ue->nas.registration.value =
        OGS_NAS_5GS_REGISTRATION_TYPE_MOBILITY_UPDATING;
    test_ue->nas.registration.ksi = OGS_NAS_KSI_NO_KEY_IS_AVAILABLE;
    memset(&test_ue->registration_request_param, 0,
            sizeof(test_ue->registration_request_param));
    test_ue->registration_request_param.guti = 1;
    gmmbuf = testgmm_build_registration_request(test_ue, NULL, false, false);
    ABTS_PTR_NOTNULL(tc, gmmbuf);

    test_ue->registration_request_param.gmm_capability = 1;
    test_ue->registration_request_param.requested_nssai = 1;
    test_ue->registration_request_param.last_visited_registered_tai = 1;
    nasbuf = testgmm_build_registration_request(test_ue, NULL, false, false);
    ABTS_PTR_NOTNULL(tc, nasbuf);

    sendbuf = testngap_build_initial_ue_message(test_ue, gmmbuf,
                NGAP_RRCEstablishmentCause_mo_Signalling, false, true);
    ABTS_PTR_NOTNULL(tc, sendbuf);
    rv = testgnb_ngap_send(ngap2, sendbuf);
    ABTS_INT_EQUAL(tc, OGS_OK, rv);

    /*
     * The UE context came from AMF-h over N14: AMF-v knows the SUPI and
     * authenticates (TS 23.502 4.2.2.2.2 step 9), no Identity Request.
     */
    recvbuf = testgnb_ngap_read(ngap2);
    ABTS_PTR_NOTNULL(tc, recvbuf);
    testngap_recv(test_ue, recvbuf);
    ABTS_INT_EQUAL(tc, OGS_NAS_5GS_AUTHENTICATION_REQUEST,
            test_ue->gmm_message_type);

    gmmbuf = testgmm_build_authentication_response(test_ue);
    ABTS_PTR_NOTNULL(tc, gmmbuf);
    sendbuf = testngap_build_uplink_nas_transport(test_ue, gmmbuf);
    ABTS_PTR_NOTNULL(tc, sendbuf);
    rv = testgnb_ngap_send(ngap2, sendbuf);
    ABTS_INT_EQUAL(tc, OGS_OK, rv);

    recvbuf = testgnb_ngap_read(ngap2);
    ABTS_PTR_NOTNULL(tc, recvbuf);
    testngap_recv(test_ue, recvbuf);
    ABTS_INT_EQUAL(tc, OGS_NAS_5GS_SECURITY_MODE_COMMAND,
            test_ue->gmm_message_type);

    gmmbuf = testgmm_build_security_mode_complete(test_ue, nasbuf);
    ABTS_PTR_NOTNULL(tc, gmmbuf);
    sendbuf = testngap_build_uplink_nas_transport(test_ue, gmmbuf);
    ABTS_PTR_NOTNULL(tc, sendbuf);
    rv = testgnb_ngap_send(ngap2, sendbuf);
    ABTS_INT_EQUAL(tc, OGS_OK, rv);

    /* Registration accept, complete, configuration update command */
    recvbuf = testgnb_ngap_read(ngap2);
    ABTS_PTR_NOTNULL(tc, recvbuf);
    testngap_recv(test_ue, recvbuf);
    ABTS_INT_EQUAL(tc, OGS_NAS_5GS_REGISTRATION_ACCEPT,
            test_ue->gmm_message_type);

    gmmbuf = testgmm_build_registration_complete(test_ue);
    ABTS_PTR_NOTNULL(tc, gmmbuf);
    sendbuf = testngap_build_uplink_nas_transport(test_ue, gmmbuf);
    ABTS_PTR_NOTNULL(tc, sendbuf);
    rv = testgnb_ngap_send(ngap2, sendbuf);
    ABTS_INT_EQUAL(tc, OGS_OK, rv);

    recvbuf = testgnb_ngap_read(ngap2);
    ABTS_PTR_NOTNULL(tc, recvbuf);
    testngap_recv(test_ue, recvbuf);
    ABTS_INT_EQUAL(tc, OGS_NAS_5GS_CONFIGURATION_UPDATE_COMMAND,
            test_ue->gmm_message_type);

    /* Idle again: UE context release in gNB2 */
    sendbuf = testngap_build_ue_context_release_request(test_ue,
            NGAP_Cause_PR_radioNetwork,
            NGAP_CauseRadioNetwork_user_inactivity, false);
    ABTS_PTR_NOTNULL(tc, sendbuf);
    rv = testgnb_ngap_send(ngap2, sendbuf);
    ABTS_INT_EQUAL(tc, OGS_OK, rv);

    recvbuf = testgnb_ngap_read(ngap2);
    ABTS_PTR_NOTNULL(tc, recvbuf);
    testngap_recv(test_ue, recvbuf);
    ABTS_INT_EQUAL(tc, NGAP_ProcedureCode_id_UEContextRelease,
            test_ue->ngap_procedure_code);

    sendbuf = testngap_build_ue_context_release_complete(test_ue);
    ABTS_PTR_NOTNULL(tc, sendbuf);
    rv = testgnb_ngap_send(ngap2, sendbuf);
    ABTS_INT_EQUAL(tc, OGS_OK, rv);

    ogs_msleep(100);

    /*
     * Back on gNB1 (999-70, TAC 1) with the 5G-GUTI of AMF-v (001-01).
     * AMF-h fetches the context from AMF-v, then authenticates the UE with
     * the AUSF of the UE's home PLMN, which is 999-70 from the SUPI, not
     * the PLMN of the 5G-GUTI.
     */
    test_ue->nr_tai.tac.v = 1;
    memcpy(&test_ue->nr_tai.plmn_id,
            &test_self()->plmn_support[0].plmn_id, OGS_PLMN_ID_LEN);
    memcpy(&test_ue->nr_cgi.plmn_id,
            &test_ue->nr_tai.plmn_id, OGS_PLMN_ID_LEN);
    test_ue->nr_cgi.cell_id = 0x40001;

    test_ue->nas.registration.ksi = OGS_NAS_KSI_NO_KEY_IS_AVAILABLE;
    memset(&test_ue->registration_request_param, 0,
            sizeof(test_ue->registration_request_param));
    test_ue->registration_request_param.guti = 1;
    gmmbuf = testgmm_build_registration_request(test_ue, NULL, false, false);
    ABTS_PTR_NOTNULL(tc, gmmbuf);

    test_ue->registration_request_param.gmm_capability = 1;
    test_ue->registration_request_param.requested_nssai = 1;
    test_ue->registration_request_param.last_visited_registered_tai = 1;
    nasbuf = testgmm_build_registration_request(test_ue, NULL, false, false);
    ABTS_PTR_NOTNULL(tc, nasbuf);

    sendbuf = testngap_build_initial_ue_message(test_ue, gmmbuf,
                NGAP_RRCEstablishmentCause_mo_Signalling, false, true);
    ABTS_PTR_NOTNULL(tc, sendbuf);
    rv = testgnb_ngap_send(ngap1, sendbuf);
    ABTS_INT_EQUAL(tc, OGS_OK, rv);

    recvbuf = testgnb_ngap_read(ngap1);
    ABTS_PTR_NOTNULL(tc, recvbuf);
    testngap_recv(test_ue, recvbuf);
    ABTS_INT_EQUAL(tc, OGS_NAS_5GS_AUTHENTICATION_REQUEST,
            test_ue->gmm_message_type);

    gmmbuf = testgmm_build_authentication_response(test_ue);
    ABTS_PTR_NOTNULL(tc, gmmbuf);
    sendbuf = testngap_build_uplink_nas_transport(test_ue, gmmbuf);
    ABTS_PTR_NOTNULL(tc, sendbuf);
    rv = testgnb_ngap_send(ngap1, sendbuf);
    ABTS_INT_EQUAL(tc, OGS_OK, rv);

    recvbuf = testgnb_ngap_read(ngap1);
    ABTS_PTR_NOTNULL(tc, recvbuf);
    testngap_recv(test_ue, recvbuf);
    ABTS_INT_EQUAL(tc, OGS_NAS_5GS_SECURITY_MODE_COMMAND,
            test_ue->gmm_message_type);

    gmmbuf = testgmm_build_security_mode_complete(test_ue, nasbuf);
    ABTS_PTR_NOTNULL(tc, gmmbuf);
    sendbuf = testngap_build_uplink_nas_transport(test_ue, gmmbuf);
    ABTS_PTR_NOTNULL(tc, sendbuf);
    rv = testgnb_ngap_send(ngap1, sendbuf);
    ABTS_INT_EQUAL(tc, OGS_OK, rv);

    recvbuf = testgnb_ngap_read(ngap1);
    ABTS_PTR_NOTNULL(tc, recvbuf);
    testngap_recv(test_ue, recvbuf);
    ABTS_INT_EQUAL(tc, OGS_NAS_5GS_REGISTRATION_ACCEPT,
            test_ue->gmm_message_type);

    gmmbuf = testgmm_build_registration_complete(test_ue);
    ABTS_PTR_NOTNULL(tc, gmmbuf);
    sendbuf = testngap_build_uplink_nas_transport(test_ue, gmmbuf);
    ABTS_PTR_NOTNULL(tc, sendbuf);
    rv = testgnb_ngap_send(ngap1, sendbuf);
    ABTS_INT_EQUAL(tc, OGS_OK, rv);

    recvbuf = testgnb_ngap_read(ngap1);
    ABTS_PTR_NOTNULL(tc, recvbuf);
    testngap_recv(test_ue, recvbuf);
    ABTS_INT_EQUAL(tc, OGS_NAS_5GS_CONFIGURATION_UPDATE_COMMAND,
            test_ue->gmm_message_type);

    ogs_msleep(300);

    /********** Remove Subscriber in Database */
    ABTS_INT_EQUAL(tc, OGS_OK, test_db_remove_ue(test_ue));

    testgnb_ngap_close(ngap1);
    testgnb_ngap_close(ngap2);

    test_ue_remove(test_ue);
}

abts_suite *test_inter_plmn_idle_mobility(abts_suite *suite)
{
    suite = ADD_SUITE(suite)
    abts_run_test(suite, inter_plmn_idle_mobility_func, NULL);
    return suite;
}
