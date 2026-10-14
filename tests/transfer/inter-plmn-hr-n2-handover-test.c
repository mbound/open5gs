/*
 * Inter-PLMN home-routed N2 handover integration test.
 *
 * HPLMN 999-70 -> VPLMN 001-01.  The PDU session starts directly anchored
 * at the H-SMF/H-UPF, then connected-mode N2 handover inserts the target
 * V-SMF/V-UPF and N9 while retaining the H-UPF PSA.  The UE then hands
 * back to the HPLMN, removing the V-SMF.  Inter-PLMN SBI is routed through
 * the configured SCP/SEPP/N32 path.
 */

#include "test-common.h"

static void inter_plmn_hr_n2_handover_func(abts_case *tc, void *data)
{
    int rv;
    ogs_socknode_t *ngap1, *ngap2;
    ogs_socknode_t *gtpu1, *gtpu2;
    ogs_pkbuf_t *gmmbuf;
    ogs_pkbuf_t *gsmbuf;
    ogs_pkbuf_t *nasbuf;
    ogs_pkbuf_t *sendbuf;
    ogs_pkbuf_t *recvbuf;

    ogs_nas_5gs_mobile_identity_suci_t mobile_identity_suci;
    test_ue_t *test_ue = NULL;
    test_sess_t *sess = NULL;
    test_bearer_t *qos_flow = NULL;

    uint64_t ran_ue_ngap_id;
    uint64_t amf_ue_ngap_id;
    ogs_5gs_tai_t target_tai;

    bson_t *doc = NULL;

    /* Setup Test UE & Session Context */
    memset(&mobile_identity_suci, 0, sizeof(mobile_identity_suci));

    mobile_identity_suci.h.supi_format = OGS_NAS_5GS_SUPI_FORMAT_IMSI;
    mobile_identity_suci.h.type = OGS_NAS_5GS_MOBILE_IDENTITY_SUCI;
    mobile_identity_suci.routing_indicator1 = 0;
    mobile_identity_suci.routing_indicator2 = 0xf;
    mobile_identity_suci.routing_indicator3 = 0xf;
    mobile_identity_suci.routing_indicator4 = 0xf;
    mobile_identity_suci.protection_scheme_id = OGS_PROTECTION_SCHEME_NULL;
    mobile_identity_suci.home_network_pki_value = 0;

    test_ue = test_ue_add_by_suci(&mobile_identity_suci, "0000000002");
    ogs_assert(test_ue);

    test_ue->nr_cgi.cell_id = 0x40001;

    test_ue->nas.registration.tsc = 0;
    test_ue->nas.registration.ksi = OGS_NAS_KSI_NO_KEY_IS_AVAILABLE;
    test_ue->nas.registration.follow_on_request = 1;
    test_ue->nas.registration.value = OGS_NAS_5GS_REGISTRATION_TYPE_INITIAL;

    test_ue->k_string = "465b5ce8b199b49faa5f0a2ee238a6bc";
    test_ue->opc_string = "e8ed289deba952e4283b54e88e6183ca";

    /* Two gNB connects to AMF */
    ngap1 = testngap_client(1, AF_INET);
    ABTS_PTR_NOTNULL(tc, ngap1);

    ngap2 = testngap_client(2, AF_INET);
    ABTS_PTR_NOTNULL(tc, ngap2);

    /* Two gNB connects to UPF */
    gtpu1 = test_gtpu_server(1, AF_INET);
    ABTS_PTR_NOTNULL(tc, gtpu1);

    gtpu2 = test_gtpu_server(2, AF_INET);
    ABTS_PTR_NOTNULL(tc, gtpu2);

    /* NG-Setup Reqeust/Response for Source gNB */
    sendbuf = testngap_build_ng_setup_request_by_tai_plmn(
            0x4000, 28, 0, 0);
    ABTS_PTR_NOTNULL(tc, sendbuf);
    rv = testgnb_ngap_send(ngap1, sendbuf);
    ABTS_INT_EQUAL(tc, OGS_OK, rv);

    recvbuf = testgnb_ngap_read(ngap1);
    ABTS_PTR_NOTNULL(tc, recvbuf);
    testngap_recv(test_ue, recvbuf);

    /* NG-Setup Reqeust/Response for Target gNB */
    sendbuf = testngap_build_ng_setup_request_by_tai_plmn(
            0x4001, 28, 1, 1);
    ABTS_PTR_NOTNULL(tc, sendbuf);
    rv = testgnb_ngap_send(ngap2, sendbuf);
    ABTS_INT_EQUAL(tc, OGS_OK, rv);

    recvbuf = testgnb_ngap_read(ngap2);
    ABTS_PTR_NOTNULL(tc, recvbuf);
    testngap_recv(test_ue, recvbuf);

    /********** Insert Subscriber in Database */
    doc = test_db_new_simple(test_ue);
    ABTS_PTR_NOTNULL(tc, doc);
    /* NR(LEO) restricted; both TAs are terrestrial NR */
    BCON_APPEND(doc, "rat_restrictions", "[", BCON_UTF8("NR_LEO"), "]");
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
    /* Mobility Restriction List: nR-LEO (bit 3) for the Serving PLMN */
    ABTS_TRUE(tc, test_ue->mrl.present);
    ABTS_TRUE(tc, memcmp(&test_ue->mrl.serving_plmn_id,
                &test_ue->nr_tai.plmn_id, OGS_PLMN_ID_LEN) == 0);
    ABTS_INT_EQUAL(tc, 0x10, test_ue->mrl.rat_restriction_information);
    ABTS_INT_EQUAL(tc, 0, test_ue->mrl.extended_primary);
    memset(&test_ue->mrl, 0, sizeof(test_ue->mrl));

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

    /* Send PDU session establishment request */
    sess = test_sess_add_by_dnn_and_psi(test_ue, "internet", 5);
    ogs_assert(sess);

    sess->ul_nas_transport_param.request_type =
        OGS_NAS_5GS_REQUEST_TYPE_INITIAL;
    sess->ul_nas_transport_param.dnn = 1;
    sess->ul_nas_transport_param.s_nssai = 1;

    sess->pdu_session_establishment_param.ssc_mode = 1;
    sess->pdu_session_establishment_param.epco = 1;

    gsmbuf = testgsm_build_pdu_session_establishment_request(sess);
    ABTS_PTR_NOTNULL(tc, gsmbuf);
    gmmbuf = testgmm_build_ul_nas_transport(sess,
            OGS_NAS_PAYLOAD_CONTAINER_N1_SM_INFORMATION, gsmbuf);
    ABTS_PTR_NOTNULL(tc, gmmbuf);
    sendbuf = testngap_build_uplink_nas_transport(test_ue, gmmbuf);
    ABTS_PTR_NOTNULL(tc, sendbuf);
    rv = testgnb_ngap_send(ngap1, sendbuf);
    ABTS_INT_EQUAL(tc, OGS_OK, rv);

    /* Receive PDUSessionResourceSetupRequest +
     * DL NAS transport +
     * PDU session establishment accept */
    recvbuf = testgnb_ngap_read(ngap1);
    ABTS_PTR_NOTNULL(tc, recvbuf);
    testngap_recv(test_ue, recvbuf);
    ABTS_INT_EQUAL(tc,
            NGAP_ProcedureCode_id_PDUSessionResourceSetup,
            test_ue->ngap_procedure_code);

    /* Send PDUSessionResourceSetupResponse */
    sendbuf = testngap_sess_build_pdu_session_resource_setup_response(sess);
    ABTS_PTR_NOTNULL(tc, sendbuf);
    rv = testgnb_ngap_send(ngap1, sendbuf);
    ABTS_INT_EQUAL(tc, OGS_OK, rv);

    /* Target AMF serves VPLMN 001-01 / TAC 2. */
    target_tai = test_ue->nr_tai;
    memcpy(&target_tai.plmn_id,
            &test_self()->plmn_support[1].plmn_id, OGS_PLMN_ID_LEN);
    target_tai.tac.v = 2;

    /* Send HandoverRequired to a gNB served by the second AMF. */
    sendbuf = testngap_build_handover_required_to_tai(
            test_ue, NGAP_HandoverType_intra5gs,
            0x4001, 28, &target_tai,
            NGAP_Cause_PR_radioNetwork,
            NGAP_CauseRadioNetwork_handover_desirable_for_radio_reason,
            true);
    ABTS_PTR_NOTNULL(tc, sendbuf);
    rv = testgnb_ngap_send(ngap1, sendbuf);
    ABTS_INT_EQUAL(tc, OGS_OK, rv);

    /* Receive HandoverRequest */
    recvbuf = testgnb_ngap_read(ngap2);
    ABTS_PTR_NOTNULL(tc, recvbuf);
    testngap_recv(test_ue, recvbuf);
    ABTS_INT_EQUAL(tc,
            NGAP_ProcedureCode_id_HandoverResourceAllocation,
            test_ue->ngap_procedure_code);
    /*
     * The target AMF has the RAT restrictions from the N14 UeContext
     * (restrictedRatList) and sends them for the target PLMN
     */
    ABTS_TRUE(tc, test_ue->mrl.present);
    ABTS_TRUE(tc, memcmp(&test_ue->mrl.serving_plmn_id,
                &target_tai.plmn_id, OGS_PLMN_ID_LEN) == 0);
    ABTS_INT_EQUAL(tc, 0x10, test_ue->mrl.rat_restriction_information);
    ABTS_INT_EQUAL(tc, 0, test_ue->mrl.extended_primary);

    /* Send HandoverRequestAcknowledge */
    ogs_list_for_each(&test_ue->sess_list, sess)
        sess->gnb_n3_addr = test_self()->gnb2_addr;

    sendbuf = testngap_build_handover_request_ack(test_ue);
    ABTS_PTR_NOTNULL(tc, sendbuf);
    rv = testgnb_ngap_send(ngap2, sendbuf);
    ABTS_INT_EQUAL(tc, OGS_OK, rv);

    /* Receive HandoverCommand */
    recvbuf = testgnb_ngap_read(ngap1);
    ABTS_PTR_NOTNULL(tc, recvbuf);
    testngap_recv(test_ue, recvbuf);
    ABTS_INT_EQUAL(tc,
            NGAP_ProcedureCode_id_HandoverPreparation,
            test_ue->ngap_procedure_code);

    /* Send UplinkRANStatusTransfer */
    sendbuf = testngap_build_uplink_ran_status_transfer(test_ue);
    ABTS_PTR_NOTNULL(tc, sendbuf);
    rv = testgnb_ngap_send(ngap1, sendbuf);
    ABTS_INT_EQUAL(tc, OGS_OK, rv);

    /* Receive DownlinkRANStatusTransfer */
    recvbuf = testgnb_ngap_read(ngap2);
    ABTS_PTR_NOTNULL(tc, recvbuf);
    testngap_recv(test_ue, recvbuf);
    ABTS_INT_EQUAL(tc,
            NGAP_ProcedureCode_id_DownlinkRANStatusTransfer,
            test_ue->ngap_procedure_code);

    /* Send HandoverNotify from the target TAI/gNB. */
    test_ue->nr_tai = target_tai;
    memcpy(&test_ue->nr_cgi.plmn_id,
            &target_tai.plmn_id, OGS_PLMN_ID_LEN);
    test_ue->nr_cgi.cell_id = 0x40011;
    sendbuf = testngap_build_handover_notify(test_ue);
    ABTS_PTR_NOTNULL(tc, sendbuf);
    rv = testgnb_ngap_send(ngap2, sendbuf);
    ABTS_INT_EQUAL(tc, OGS_OK, rv);

    /* Receive End Mark */
    recvbuf = test_gtpu_read(gtpu1);
    ABTS_PTR_NOTNULL(tc, recvbuf);
    ogs_pkbuf_free(recvbuf);

    /* Receive UEContextReleaseCommand */
    amf_ue_ngap_id = test_ue->amf_ue_ngap_id;
    ran_ue_ngap_id = test_ue->ran_ue_ngap_id;

    recvbuf = testgnb_ngap_read(ngap1);
    ABTS_PTR_NOTNULL(tc, recvbuf);
    testngap_recv(test_ue, recvbuf);
    ABTS_INT_EQUAL(tc,
            NGAP_ProcedureCode_id_UEContextRelease,
            test_ue->ngap_procedure_code);

    /* Send UEContextReleaseComplete */
    sendbuf = testngap_build_ue_context_release_complete(test_ue);
    ABTS_PTR_NOTNULL(tc, sendbuf);
    rv = testgnb_ngap_send(ngap1, sendbuf);
    ABTS_INT_EQUAL(tc, OGS_OK, rv);

    test_ue->amf_ue_ngap_id = amf_ue_ngap_id;
    test_ue->ran_ue_ngap_id = ran_ue_ngap_id;

    /*
     * Verify HR continuity through target gNB N3.  A successful ping here
     * traverses target gNB -> V-UPF -> N9 -> retained H-UPF/PSA and back.
     */
    sess = test_sess_find_by_psi(test_ue, 5);
    ogs_assert(sess);
    qos_flow = test_qos_flow_find_by_qfi(sess, 1);
    ogs_assert(qos_flow);
    rv = test_gtpu_send_ping(gtpu2, qos_flow, TEST_PING_IPV4);
    ABTS_INT_EQUAL(tc, OGS_OK, rv);
    recvbuf = testgnb_gtpu_read(gtpu2);
    ABTS_PTR_NOTNULL(tc, recvbuf);
    ogs_pkbuf_free(recvbuf);

    /*
     * Optional (TEST_HR_MRU set): Mobility Registration Update at the target
     * V-AMF after the handover into the new TA (TS 23.502 4.9.1.3.3 step 12,
     * TS 24.501 5.5.1.3). The V-AMF then creates the AM policy association
     * with a V-PCF, which configs/inter-plmn-hr-n2.yaml does not have, so
     * the meson suite leaves it off; docker/inter-plmn-hr turns it on.
     */
    if (getenv("TEST_HR_MRU")) {
        test_ue->nas.registration.value =
            OGS_NAS_5GS_REGISTRATION_TYPE_MOBILITY_UPDATING;
        memset(&test_ue->registration_request_param, 0,
                sizeof(test_ue->registration_request_param));
        test_ue->registration_request_param.guti = 1;
        test_ue->registration_request_param.requested_nssai = 1;
        test_ue->registration_request_param.last_visited_registered_tai = 1;
        /* plain copy for the NAS message container of SMC complete */
        nasbuf = testgmm_build_registration_request(
                test_ue, NULL, false, false);
        ABTS_PTR_NOTNULL(tc, nasbuf);
        gmmbuf = testgmm_build_registration_request(
                test_ue, NULL, true, true);
        ABTS_PTR_NOTNULL(tc, gmmbuf);
        sendbuf = testngap_build_uplink_nas_transport(test_ue, gmmbuf);
        ABTS_PTR_NOTNULL(tc, sendbuf);
        rv = testgnb_ngap_send(ngap2, sendbuf);
        ABTS_INT_EQUAL(tc, OGS_OK, rv);

        /*
         * The V-AMF got the SUPI but no SUCI with the UE context (N14), so
         * it may ask for the identity and re-authenticate the UE (TS 23.502
         * 4.2.2.2.2 steps 8-9) before the Registration accept.
         */
        for (;;) {
            recvbuf = testgnb_ngap_read(ngap2);
            ABTS_PTR_NOTNULL(tc, recvbuf);
            testngap_recv(test_ue, recvbuf);

            if (test_ue->gmm_message_type == OGS_NAS_5GS_IDENTITY_REQUEST)
                gmmbuf = testgmm_build_identity_response(test_ue);
            else if (test_ue->gmm_message_type ==
                    OGS_NAS_5GS_AUTHENTICATION_REQUEST)
                gmmbuf = testgmm_build_authentication_response(test_ue);
            else if (test_ue->gmm_message_type ==
                    OGS_NAS_5GS_SECURITY_MODE_COMMAND)
                gmmbuf = testgmm_build_security_mode_complete(
                        test_ue, nasbuf);
            else
                break;

            ABTS_PTR_NOTNULL(tc, gmmbuf);
            sendbuf = testngap_build_uplink_nas_transport(test_ue, gmmbuf);
            ABTS_PTR_NOTNULL(tc, sendbuf);
            rv = testgnb_ngap_send(ngap2, sendbuf);
            ABTS_INT_EQUAL(tc, OGS_OK, rv);
        }

        /* Receive Registration accept */
        ABTS_INT_EQUAL(tc, OGS_NAS_5GS_REGISTRATION_ACCEPT,
                test_ue->gmm_message_type);

        /* Send Registration complete */
        gmmbuf = testgmm_build_registration_complete(test_ue);
        ABTS_PTR_NOTNULL(tc, gmmbuf);
        sendbuf = testngap_build_uplink_nas_transport(test_ue, gmmbuf);
        ABTS_PTR_NOTNULL(tc, sendbuf);
        rv = testgnb_ngap_send(ngap2, sendbuf);
        ABTS_INT_EQUAL(tc, OGS_OK, rv);

        /* Receive Configuration update command */
        recvbuf = testgnb_ngap_read(ngap2);
        ABTS_PTR_NOTNULL(tc, recvbuf);
        testngap_recv(test_ue, recvbuf);
        ABTS_INT_EQUAL(tc, OGS_NAS_5GS_CONFIGURATION_UPDATE_COMMAND,
                test_ue->gmm_message_type);

        /*
         * The home path still carries traffic: expect a G-PDU (the echo
         * reply), not e.g. an Error Indication for a released session.
         */
        rv = test_gtpu_send_ping(gtpu2, qos_flow, TEST_PING_IPV4);
        ABTS_INT_EQUAL(tc, OGS_OK, rv);
        recvbuf = testgnb_gtpu_read(gtpu2);
        ABTS_PTR_NOTNULL(tc, recvbuf);
        ABTS_INT_EQUAL(tc, OGS_GTPU_MSGTYPE_GPDU,
                ((ogs_gtp2_header_t *)recvbuf->data)->type);
        ogs_pkbuf_free(recvbuf);
    }

    /*
     * Hand back to gNB1 in the HPLMN (999-70, TAC 1, AMF-h). AMF-h removes
     * the V-SMF (TS 23.502 4.23.7.3, V-SMF removal per 4.23.1): the H-SMF
     * takes the session over and switches the H-UPF from N9 to N3 towards
     * gNB1, the V-SMF/V-UPF context is released.
     */
    target_tai.tac.v = 1;
    memcpy(&target_tai.plmn_id,
            &test_self()->plmn_support[0].plmn_id, OGS_PLMN_ID_LEN);

    sendbuf = testngap_build_handover_required_to_tai(
            test_ue, NGAP_HandoverType_intra5gs,
            0x4000, 28, &target_tai,
            NGAP_Cause_PR_radioNetwork,
            NGAP_CauseRadioNetwork_handover_desirable_for_radio_reason,
            true);
    ABTS_PTR_NOTNULL(tc, sendbuf);
    rv = testgnb_ngap_send(ngap2, sendbuf);
    ABTS_INT_EQUAL(tc, OGS_OK, rv);

    recvbuf = testgnb_ngap_read(ngap1);
    ABTS_PTR_NOTNULL(tc, recvbuf);
    testngap_recv(test_ue, recvbuf);
    ABTS_INT_EQUAL(tc,
            NGAP_ProcedureCode_id_HandoverResourceAllocation,
            test_ue->ngap_procedure_code);

    ogs_list_for_each(&test_ue->sess_list, sess)
        sess->gnb_n3_addr = test_self()->gnb1_addr;

    sendbuf = testngap_build_handover_request_ack(test_ue);
    ABTS_PTR_NOTNULL(tc, sendbuf);
    rv = testgnb_ngap_send(ngap1, sendbuf);
    ABTS_INT_EQUAL(tc, OGS_OK, rv);

    recvbuf = testgnb_ngap_read(ngap2);
    ABTS_PTR_NOTNULL(tc, recvbuf);
    testngap_recv(test_ue, recvbuf);
    ABTS_INT_EQUAL(tc,
            NGAP_ProcedureCode_id_HandoverPreparation,
            test_ue->ngap_procedure_code);

    sendbuf = testngap_build_uplink_ran_status_transfer(test_ue);
    ABTS_PTR_NOTNULL(tc, sendbuf);
    rv = testgnb_ngap_send(ngap2, sendbuf);
    ABTS_INT_EQUAL(tc, OGS_OK, rv);

    recvbuf = testgnb_ngap_read(ngap1);
    ABTS_PTR_NOTNULL(tc, recvbuf);
    testngap_recv(test_ue, recvbuf);
    ABTS_INT_EQUAL(tc,
            NGAP_ProcedureCode_id_DownlinkRANStatusTransfer,
            test_ue->ngap_procedure_code);

    test_ue->nr_tai = target_tai;
    memcpy(&test_ue->nr_cgi.plmn_id,
            &target_tai.plmn_id, OGS_PLMN_ID_LEN);
    test_ue->nr_cgi.cell_id = 0x40001;
    sendbuf = testngap_build_handover_notify(test_ue);
    ABTS_PTR_NOTNULL(tc, sendbuf);
    rv = testgnb_ngap_send(ngap1, sendbuf);
    ABTS_INT_EQUAL(tc, OGS_OK, rv);

    amf_ue_ngap_id = test_ue->amf_ue_ngap_id;
    ran_ue_ngap_id = test_ue->ran_ue_ngap_id;

    recvbuf = testgnb_ngap_read(ngap2);
    ABTS_PTR_NOTNULL(tc, recvbuf);
    testngap_recv(test_ue, recvbuf);
    ABTS_INT_EQUAL(tc,
            NGAP_ProcedureCode_id_UEContextRelease,
            test_ue->ngap_procedure_code);

    sendbuf = testngap_build_ue_context_release_complete(test_ue);
    ABTS_PTR_NOTNULL(tc, sendbuf);
    rv = testgnb_ngap_send(ngap2, sendbuf);
    ABTS_INT_EQUAL(tc, OGS_OK, rv);

    test_ue->amf_ue_ngap_id = amf_ue_ngap_id;
    test_ue->ran_ue_ngap_id = ran_ue_ngap_id;

    /*
     * Mobility Registration Update at AMF-h after the handover into the
     * new TA (TS 23.502 4.9.1.3.3 step 12): the session stays active.
     */
    test_ue->nas.registration.value =
        OGS_NAS_5GS_REGISTRATION_TYPE_MOBILITY_UPDATING;
    memset(&test_ue->registration_request_param, 0,
            sizeof(test_ue->registration_request_param));
    test_ue->registration_request_param.guti = 1;
    test_ue->registration_request_param.requested_nssai = 1;
    test_ue->registration_request_param.last_visited_registered_tai = 1;
    test_ue->registration_request_param.pdu_session_status = 1;
    test_ue->registration_request_param.psimask.pdu_session_status =
        1 << 5;
    nasbuf = testgmm_build_registration_request(
            test_ue, NULL, false, false);
    ABTS_PTR_NOTNULL(tc, nasbuf);
    gmmbuf = testgmm_build_registration_request(
            test_ue, NULL, true, true);
    ABTS_PTR_NOTNULL(tc, gmmbuf);
    sendbuf = testngap_build_uplink_nas_transport(test_ue, gmmbuf);
    ABTS_PTR_NOTNULL(tc, sendbuf);
    rv = testgnb_ngap_send(ngap1, sendbuf);
    ABTS_INT_EQUAL(tc, OGS_OK, rv);

    for (;;) {
        recvbuf = testgnb_ngap_read(ngap1);
        ABTS_PTR_NOTNULL(tc, recvbuf);
        testngap_recv(test_ue, recvbuf);

        if (test_ue->gmm_message_type == OGS_NAS_5GS_IDENTITY_REQUEST)
            gmmbuf = testgmm_build_identity_response(test_ue);
        else if (test_ue->gmm_message_type ==
                OGS_NAS_5GS_AUTHENTICATION_REQUEST)
            gmmbuf = testgmm_build_authentication_response(test_ue);
        else if (test_ue->gmm_message_type ==
                OGS_NAS_5GS_SECURITY_MODE_COMMAND)
            gmmbuf = testgmm_build_security_mode_complete(test_ue, nasbuf);
        else
            break;

        ABTS_PTR_NOTNULL(tc, gmmbuf);
        sendbuf = testngap_build_uplink_nas_transport(test_ue, gmmbuf);
        ABTS_PTR_NOTNULL(tc, sendbuf);
        rv = testgnb_ngap_send(ngap1, sendbuf);
        ABTS_INT_EQUAL(tc, OGS_OK, rv);
    }

    ABTS_INT_EQUAL(tc, OGS_NAS_5GS_REGISTRATION_ACCEPT,
            test_ue->gmm_message_type);
    ABTS_INT_EQUAL(tc, 0x2000, test_ue->pdu_session_status);

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

    /* Same UE IP, now straight through the H-UPF N3 tunnel to gNB1 */
    sess = test_sess_find_by_psi(test_ue, 5);
    ogs_assert(sess);
    qos_flow = test_qos_flow_find_by_qfi(sess, 1);
    ogs_assert(qos_flow);
    rv = test_gtpu_send_ping(gtpu1, qos_flow, TEST_PING_IPV4);
    ABTS_INT_EQUAL(tc, OGS_OK, rv);
    for (;;) {
        /* skip an end marker of the old path, if any */
        recvbuf = testgnb_gtpu_read(gtpu1);
        ABTS_PTR_NOTNULL(tc, recvbuf);
        if (((ogs_gtp2_header_t *)recvbuf->data)->type !=
                OGS_GTPU_MSGTYPE_END_MARKER)
            break;
        ogs_pkbuf_free(recvbuf);
    }
    ABTS_INT_EQUAL(tc, OGS_GTPU_MSGTYPE_GPDU,
            ((ogs_gtp2_header_t *)recvbuf->data)->type);
    ogs_pkbuf_free(recvbuf);

    /*
     * The relocation state of the session is done with: a new PDU session
     * establishment with the same PSI (old session released as duplicate)
     * is an ordinary one, not another V-SMF removal.
     */
    sess->ul_nas_transport_param.request_type =
        OGS_NAS_5GS_REQUEST_TYPE_INITIAL;
    sess->ul_nas_transport_param.dnn = 1;
    sess->ul_nas_transport_param.s_nssai = 1;
    sess->pdu_session_establishment_param.ssc_mode = 1;
    sess->pdu_session_establishment_param.epco = 1;

    gsmbuf = testgsm_build_pdu_session_establishment_request(sess);
    ABTS_PTR_NOTNULL(tc, gsmbuf);
    gmmbuf = testgmm_build_ul_nas_transport(sess,
            OGS_NAS_PAYLOAD_CONTAINER_N1_SM_INFORMATION, gsmbuf);
    ABTS_PTR_NOTNULL(tc, gmmbuf);
    sendbuf = testngap_build_uplink_nas_transport(test_ue, gmmbuf);
    ABTS_PTR_NOTNULL(tc, sendbuf);
    rv = testgnb_ngap_send(ngap1, sendbuf);
    ABTS_INT_EQUAL(tc, OGS_OK, rv);

    /* N2 SM information from the H-SMF, not a rejected 5GSM message */
    recvbuf = testgnb_ngap_read(ngap1);
    ABTS_PTR_NOTNULL(tc, recvbuf);
    testngap_recv(test_ue, recvbuf);
    ABTS_TRUE(tc, test_ue->ngap_procedure_code ==
                NGAP_ProcedureCode_id_PDUSessionResourceSetup ||
            test_ue->ngap_procedure_code ==
                NGAP_ProcedureCode_id_InitialContextSetup);

    if (test_ue->ngap_procedure_code ==
            NGAP_ProcedureCode_id_InitialContextSetup)
        sendbuf = testngap_build_initial_context_setup_response(
                test_ue, true);
    else
        sendbuf = testngap_sess_build_pdu_session_resource_setup_response(
                sess);
    ABTS_PTR_NOTNULL(tc, sendbuf);
    rv = testgnb_ngap_send(ngap1, sendbuf);
    ABTS_INT_EQUAL(tc, OGS_OK, rv);

    /* Release the NG context at AMF-h cleanly. */
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

    ogs_msleep(300);

    /********** Remove Subscriber in Database */
    ABTS_INT_EQUAL(tc, OGS_OK, test_db_remove_ue(test_ue));

    /* Two gNB disonncect from UPF */
    testgnb_gtpu_close(gtpu1);
    testgnb_gtpu_close(gtpu2);

    /* Two gNB disonncect from AMF */
    testgnb_ngap_close(ngap1);
    testgnb_ngap_close(ngap2);

    /* Clear Test UE Context */
    test_ue_remove(test_ue);
}


abts_suite *test_inter_plmn_hr_n2_handover(abts_suite *suite)
{
    suite = ADD_SUITE(suite)
    abts_run_test(suite, inter_plmn_hr_n2_handover_func, NULL);
    return suite;
}
