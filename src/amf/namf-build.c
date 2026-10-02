/*
 * Copyright (C) 2019-2025 by Sukchan Lee <acetcom@gmail.com>
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

#include "namf-build.h"
#include "nsmf-build.h"

char *amf_namf_comm_base64_encode_ue_security_capability(
        ogs_nas_ue_security_capability_t ue_security_capability)
{
    char *enc = NULL;
    int enc_len = 0;

    char num_of_octets =
            ue_security_capability.length +
            sizeof(ue_security_capability.length) +
            sizeof((uint8_t)OGS_NAS_5GS_REGISTRATION_REQUEST_UE_SECURITY_CAPABILITY_TYPE);
    /*
     * size [sizeof(ue_security_capability) + 1] is a sum of lengths:
     *        ue_security_capability (9 octets) +
     *        type (1 octet)
     */
    char security_octets_string[sizeof(ue_security_capability) + 1];

    /* Security guarantee */
    num_of_octets = ogs_min(
            num_of_octets, sizeof(ue_security_capability) + 1);
    enc_len = ogs_base64_encoded_size(num_of_octets);

    enc = ogs_calloc(1, enc_len);
    ogs_assert(enc);

    security_octets_string[0] = (uint8_t)
        OGS_NAS_5GS_REGISTRATION_REQUEST_UE_SECURITY_CAPABILITY_TYPE;
    memcpy(security_octets_string + 1, &ue_security_capability, num_of_octets);
    ogs_assert(ogs_base64_encode_from_buffer(enc, enc_len,
            (const uint8_t *)security_octets_string, num_of_octets) > 0);

    return enc;
}

char *amf_namf_comm_base64_encode_5gmm_capability(amf_ue_t *amf_ue)
{
    ogs_nas_5gmm_capability_t nas_gmm_capability;
    int enc_len = 0;
    char *enc = NULL;

    memset(&nas_gmm_capability, 0, sizeof(nas_gmm_capability));

    /* 1 octet is mandatory, n.3 from TS 24.501 V16.12.0, 9.11.3.1 */
    nas_gmm_capability.length = 1;
    nas_gmm_capability.lte_positioning_protocol_capability =
            amf_ue->gmm_capability.lte_positioning_protocol_capability;
    nas_gmm_capability.ho_attach = amf_ue->gmm_capability.ho_attach;
    nas_gmm_capability.s1_mode = amf_ue->gmm_capability.s1_mode;

    uint8_t num_of_octets;

    char gmm_capability_octets_string[sizeof(ogs_nas_5gmm_capability_t) + 1];

    num_of_octets =
            nas_gmm_capability.length +
            sizeof(nas_gmm_capability.length) +
            sizeof((uint8_t)
                    OGS_NAS_5GS_REGISTRATION_REQUEST_5GMM_CAPABILITY_TYPE);

    /* Security guarantee. + 1 stands for 5GMM capability IEI */
    num_of_octets = ogs_min(
            num_of_octets, sizeof(ogs_nas_5gmm_capability_t) + 1);

    enc_len = ogs_base64_encoded_size(num_of_octets);
    enc = ogs_calloc(1, enc_len);
    ogs_assert(enc);

    /* Fill the bytes of data */
    gmm_capability_octets_string[0] =
            (uint8_t)OGS_NAS_5GS_REGISTRATION_REQUEST_5GMM_CAPABILITY_TYPE;
    memcpy(gmm_capability_octets_string + 1,
            &nas_gmm_capability, num_of_octets);
    ogs_assert(ogs_base64_encode_from_buffer(enc, enc_len,
            (const uint8_t *)gmm_capability_octets_string, num_of_octets) > 0);

    return enc;
}

OpenAPI_list_t *amf_namf_comm_encode_ue_session_context_list(
        amf_ue_t *amf_ue)
{
    ogs_assert(amf_ue);

    amf_sess_t *sess = NULL;
    OpenAPI_list_t *PduSessionList = NULL;
    OpenAPI_pdu_session_context_t *PduSessionContext = NULL;
    OpenAPI_snssai_t *sNSSAI = NULL;

    PduSessionList = OpenAPI_list_create();
    ogs_assert(PduSessionList);

    ogs_list_for_each(&amf_ue->sess_list, sess) {
        PduSessionContext = ogs_calloc(1, sizeof(*PduSessionContext));
        ogs_assert(PduSessionContext);

        sNSSAI = ogs_calloc(1, sizeof(*sNSSAI));
        ogs_assert(sNSSAI);

        PduSessionContext->pdu_session_id = sess->psi;
        ogs_assert(sess->sm_context_resource_uri);
        PduSessionContext->sm_context_ref =
            ogs_strdup(sess->sm_context_resource_uri);

        sNSSAI->sst = sess->s_nssai.sst;
        sNSSAI->sd = ogs_s_nssai_sd_to_string(sess->s_nssai.sd);
        PduSessionContext->s_nssai = sNSSAI;

        ogs_assert(sess->dnn);
        PduSessionContext->dnn = ogs_strdup(sess->dnn);
        PduSessionContext->access_type =
            (OpenAPI_access_type_e)amf_ue->nas.access_type;

        OpenAPI_list_add(PduSessionList, PduSessionContext);
    }

    return PduSessionList;
}

OpenAPI_list_t *amf_namf_comm_encode_ue_mm_context_list(amf_ue_t *amf_ue)
{
    OpenAPI_list_t *MmContextList = NULL;
    OpenAPI_mm_context_t *MmContext = NULL;

    int i;

    ogs_assert(amf_ue);

    MmContextList = OpenAPI_list_create();
    ogs_assert(MmContextList);

    MmContext = ogs_malloc(sizeof(*MmContext));
    ogs_assert(MmContext);
    memset(MmContext, 0, sizeof(*MmContext));

    MmContext->access_type = (OpenAPI_access_type_e)amf_ue->nas.access_type;

    if ((OpenAPI_ciphering_algorithm_e)amf_ue->selected_enc_algorithm &&
        (OpenAPI_integrity_algorithm_e)amf_ue->selected_int_algorithm) {

        OpenAPI_nas_security_mode_t *NasSecurityMode;

        NasSecurityMode = ogs_calloc(1, sizeof(*NasSecurityMode));
        ogs_assert(NasSecurityMode);

        NasSecurityMode->ciphering_algorithm =
                (OpenAPI_ciphering_algorithm_e)amf_ue->selected_enc_algorithm;
        NasSecurityMode->integrity_algorithm =
                (OpenAPI_integrity_algorithm_e)amf_ue->selected_int_algorithm;

        MmContext->nas_security_mode = NasSecurityMode;
    }

    if (amf_ue->dl_count > 0) {
        MmContext->is_nas_downlink_count = true;
        MmContext->nas_downlink_count = amf_ue->dl_count;
    }

    if (amf_ue->ul_count.i32 > 0) {
        MmContext->is_nas_uplink_count = true;
        MmContext->nas_uplink_count = amf_ue->ul_count.i32;
    }

    if (amf_ue->ue_security_capability.length > 0) {
        MmContext->ue_security_capability =
                amf_namf_comm_base64_encode_ue_security_capability(
                amf_ue->ue_security_capability);
    }

    if (amf_ue->allowed_nssai.num_of_s_nssai) {

        OpenAPI_list_t *AllowedNssaiList;

        /* This IE shall be present if the source AMF and the target AMF are
        *  in the same PLMN and if available. When present, this IE shall
        * contain the allowed NSSAI for the access type.
        */
        AllowedNssaiList = OpenAPI_list_create();

        ogs_assert(AllowedNssaiList);

        for (i = 0; i < amf_ue->allowed_nssai.num_of_s_nssai; i++) {
            OpenAPI_snssai_t *AllowedNssai;

            AllowedNssai = ogs_calloc(1, sizeof(*AllowedNssai));
            ogs_assert(AllowedNssai);

            AllowedNssai->sst = amf_ue->allowed_nssai.s_nssai[i].sst;
            AllowedNssai->sd = ogs_s_nssai_sd_to_string(
                    amf_ue->allowed_nssai.s_nssai[i].sd);

            OpenAPI_list_add(AllowedNssaiList, AllowedNssai);
        }

        MmContext->allowed_nssai = AllowedNssaiList;
    }

    OpenAPI_list_add(MmContextList, MmContext);

    return MmContextList;
}



static char* ogs_guti_to_string(ogs_nas_5gs_guti_t *nas_guti)
{
    ogs_plmn_id_t plmn_id;
    char plmn_id_buff[OGS_PLMNIDSTRLEN];
    char *amf_id = NULL;
    char *tmsi = NULL;
    char *guti = NULL;

    ogs_assert(nas_guti);

    memset(&plmn_id, 0, sizeof(plmn_id));
    ogs_nas_to_plmn_id(&plmn_id, &nas_guti->nas_plmn_id);
    amf_id = ogs_amf_id_to_string(&nas_guti->amf_id);
    tmsi = ogs_uint32_to_0string(nas_guti->m_tmsi);

    guti = ogs_msprintf("5g-guti-%s%s%s",
            ogs_plmn_id_to_string(&plmn_id, plmn_id_buff),
            amf_id,
            tmsi);

    /* TS29.518 6.1.3.2.2 Guti pattern (27 or 28 characters):
    "5g-guti-[0-9]{5,6}[0-9a-fA-F]{14}" */
    ogs_assert(strlen(guti) == (OGS_MAX_5G_GUTI_LEN - 1) ||
            (strlen(guti)) == OGS_MAX_5G_GUTI_LEN);

    ogs_free(amf_id);
    ogs_free(tmsi);

    return guti;
}

static char* amf_ue_to_context_id(amf_ue_t *amf_ue)
{
    char *ue_context_id = NULL;

    if (amf_ue->supi) {
        ue_context_id = ogs_strdup(amf_ue->supi);
    } else {
        ue_context_id = ogs_guti_to_string(&amf_ue->old_guti);
    }

    return ue_context_id;
}


ogs_sbi_request_t *amf_namf_comm_build_create_ue_context(
        amf_ue_t *amf_ue, void *data)
{
    amf_namf_comm_create_ue_context_param_t *param = data;
    ogs_sbi_message_t message;
    ogs_sbi_request_t *request = NULL;
    ogs_sbi_header_t callback_header;
    ogs_sbi_server_t *server = NULL;

    OpenAPI_ue_context_create_data_t UeContextCreateData;
    OpenAPI_ue_context_t UeContext;
    OpenAPI_ambr_t *UeAmbr = NULL;
    OpenAPI_seaf_data_t SeafData;
    OpenAPI_ng_ksi_t NgKsi;
    OpenAPI_key_amf_t KeyAmf;
    OpenAPI_list_t *MmContextList = NULL;
    OpenAPI_list_t *SessionContextList = NULL;
    OpenAPI_list_t *PduSessionList = NULL;
    OpenAPI_lnode_t *node = NULL;
    OpenAPI_n2_info_content_t SourceToTargetData;
    OpenAPI_ref_to_binary_data_t SourceToTargetRef;
    OpenAPI_ng_ap_cause_t NgapCause;

    char hxkamf_string[OGS_KEYSTRLEN(OGS_SHA256_DIGEST_SIZE)];
    char hxnh_string[OGS_KEYSTRLEN(OGS_SHA256_DIGEST_SIZE)];
    char *encoded_gmm_capability = NULL;
    int i;

    ogs_assert(amf_ue);
    ogs_assert(amf_ue->supi);
    ogs_assert(param);
    ogs_assert(param->target_id);
    ogs_assert(param->pdu_session_list);
    ogs_assert(param->source_to_target_container);

    memset(&message, 0, sizeof(message));
    memset(&UeContextCreateData, 0, sizeof(UeContextCreateData));
    memset(&UeContext, 0, sizeof(UeContext));
    memset(&SeafData, 0, sizeof(SeafData));
    memset(&NgKsi, 0, sizeof(NgKsi));
    memset(&KeyAmf, 0, sizeof(KeyAmf));
    memset(&SourceToTargetData, 0, sizeof(SourceToTargetData));
    memset(&SourceToTargetRef, 0, sizeof(SourceToTargetRef));
    memset(&NgapCause, 0, sizeof(NgapCause));
    memset(&callback_header, 0, sizeof(callback_header));

    message.h.method = (char *)OGS_SBI_HTTP_METHOD_PUT;
    message.h.service.name =
        OpenAPI_service_name_ToString(OpenAPI_service_name_namf_comm);
    message.h.api.version = (char *)OGS_SBI_API_V1;
    message.h.resource.component[0] =
        (char *)OGS_SBI_RESOURCE_NAME_UE_CONTEXTS;
    message.h.resource.component[1] = amf_ue->supi;

    UeContextCreateData.ue_context = &UeContext;
    UeContext.supi = amf_ue->supi;
    if (amf_ue->auth_result != OpenAPI_auth_result_AUTHENTICATION_SUCCESS) {
        UeContext.is_supi_unauth_ind = true;
        UeContext.supi_unauth_ind = amf_ue->auth_result;
    }
    if (amf_ue->pei)
        UeContext.pei = amf_ue->pei;

    if ((amf_ue->ue_ambr.uplink > 0) || (amf_ue->ue_ambr.downlink > 0)) {
        UeAmbr = ogs_calloc(1, sizeof(*UeAmbr));
        ogs_assert(UeAmbr);

        if (amf_ue->ue_ambr.uplink > 0)
            UeAmbr->uplink = ogs_sbi_bitrate_to_string(
                    amf_ue->ue_ambr.uplink, OGS_SBI_BITRATE_KBPS);
        if (amf_ue->ue_ambr.downlink > 0)
            UeAmbr->downlink = ogs_sbi_bitrate_to_string(
                    amf_ue->ue_ambr.downlink, OGS_SBI_BITRATE_KBPS);
        UeContext.sub_ue_ambr = UeAmbr;
    }

    /*
     * TS 29.518 UeContext carries the SEAF data needed by the target AMF.
     * Unlike the older registration-transfer encoder, KSI=0 and native
     * security context (TSC=0) are both valid values, so gate this on the
     * actual validity of the security context rather than on non-zero fields.
     */
    if (SECURITY_CONTEXT_IS_VALID(amf_ue)) {
        NgKsi.tsc = amf_ue->nas.ue.tsc ?
            OpenAPI_sc_type_MAPPED : OpenAPI_sc_type_NATIVE;
        NgKsi.ksi = (int)amf_ue->nas.ue.ksi;

        KeyAmf.key_type = OpenAPI_key_amf_type_KAMF;
        ogs_hex_to_ascii(amf_ue->kamf, sizeof(amf_ue->kamf),
                hxkamf_string, sizeof(hxkamf_string));
        KeyAmf.key_val = hxkamf_string;

        SeafData.ng_ksi = &NgKsi;
        SeafData.key_amf = &KeyAmf;

        /*
         * TS 29.518 SeafData carries the current NH and NCC.  They are
         * required by the target AMF to construct the NGAP SecurityContext
         * in HandoverRequest without restarting the NH chain.
         */
        ogs_hex_to_ascii(amf_ue->nh, sizeof(amf_ue->nh),
                hxnh_string, sizeof(hxnh_string));
        SeafData.nh = hxnh_string;
        SeafData.is_ncc = true;
        SeafData.ncc = amf_ue->nhcc;

        UeContext.seaf_data = &SeafData;
    }

    encoded_gmm_capability =
        amf_namf_comm_base64_encode_5gmm_capability(amf_ue);
    UeContext._5g_mm_capability = encoded_gmm_capability;

    MmContextList = amf_namf_comm_encode_ue_mm_context_list(amf_ue);
    UeContext.mm_context_list = MmContextList;

    SessionContextList =
        amf_namf_comm_encode_ue_session_context_list(amf_ue);
    if (SessionContextList && SessionContextList->count)
        UeContext.session_context_list = SessionContextList;

    UeContextCreateData.target_id =
        amf_nsmf_pdusession_build_target_id(param->target_id);
    if (!UeContextCreateData.target_id) {
        ogs_error("[%s] Cannot build targetId", amf_ue->supi);
        goto cleanup;
    }

    /*
     * Source-to-Target Transparent Container is a distinct N2 IE from each
     * PDU-session Handover Required Transfer.  Give every binary part its own
     * Content-Id so multipart correlation is unambiguous.
     */
    SourceToTargetRef.content_id = (char *)"source-to-target-container";
    SourceToTargetData.ngap_ie_type =
        OpenAPI_ngap_ie_type_SRC_TO_TAR_CONTAINER;
    SourceToTargetData.ngap_data = &SourceToTargetRef;
    UeContextCreateData.source_to_target_data = &SourceToTargetData;

    if (message.num_of_part >= OGS_SBI_MAX_NUM_OF_PART) {
        ogs_error("[%s] No multipart capacity for source-to-target container",
                amf_ue->supi);
        goto cleanup;
    }
    message.part[message.num_of_part].pkbuf =
        ogs_pkbuf_alloc(NULL, OGS_MAX_SDU_LEN);
    if (!message.part[message.num_of_part].pkbuf) {
        ogs_error("[%s] Cannot allocate source-to-target container",
                amf_ue->supi);
        goto cleanup;
    }
    ogs_pkbuf_put_data(message.part[message.num_of_part].pkbuf,
            param->source_to_target_container->buf,
            param->source_to_target_container->size);
    message.part[message.num_of_part].content_id =
        SourceToTargetRef.content_id;
    message.part[message.num_of_part].content_type =
        (char *)OGS_SBI_CONTENT_NGAP_TYPE;
    message.num_of_part++;

    PduSessionList = OpenAPI_list_create();
    ogs_assert(PduSessionList);

    for (i = 0; i < OGS_ASN_LIST_COUNT(param->pdu_session_list); i++) {
        NGAP_PDUSessionResourceItemHORqd_t *item = NULL;
        amf_sess_t *sess = NULL;
        OCTET_STRING_t *transfer = NULL;
        OpenAPI_n2_sm_information_t *N2SmInformation = NULL;
        OpenAPI_n2_info_content_t *N2InfoContent = NULL;
        OpenAPI_ref_to_binary_data_t *N2Ref = NULL;
        OpenAPI_snssai_t *SNssai = NULL;
        char *content_id = NULL;

        item = (NGAP_PDUSessionResourceItemHORqd_t *)
            OGS_ASN_LIST_GET(param->pdu_session_list, i);
        if (!item) {
            ogs_error("[%s] No PDUSessionResourceItemHORqd", amf_ue->supi);
            goto cleanup;
        }

        sess = amf_sess_find_by_psi(amf_ue, item->pDUSessionID);
        if (!sess || !SESSION_CONTEXT_IN_SMF(sess)) {
            ogs_error("[%s:%ld] No SM context for inter-AMF handover",
                    amf_ue->supi, item->pDUSessionID);
            goto cleanup;
        }

        transfer = &item->handoverRequiredTransfer;
        if (!transfer->buf || !transfer->size) {
            ogs_error("[%s:%d] Empty HandoverRequiredTransfer",
                    amf_ue->supi, sess->psi);
            goto cleanup;
        }

        if (message.num_of_part >= OGS_SBI_MAX_NUM_OF_PART) {
            ogs_error("[%s] Too many N2 multipart parts [%d]",
                    amf_ue->supi, message.num_of_part + 1);
            goto cleanup;
        }

        N2SmInformation = ogs_calloc(1, sizeof(*N2SmInformation));
        N2InfoContent = ogs_calloc(1, sizeof(*N2InfoContent));
        N2Ref = ogs_calloc(1, sizeof(*N2Ref));
        SNssai = ogs_calloc(1, sizeof(*SNssai));
        ogs_assert(N2SmInformation);
        ogs_assert(N2InfoContent);
        ogs_assert(N2Ref);
        ogs_assert(SNssai);

        content_id = ogs_msprintf("handover-required-%d", sess->psi);
        ogs_assert(content_id);

        N2Ref->content_id = content_id;
        N2InfoContent->ngap_ie_type =
            OpenAPI_ngap_ie_type_HANDOVER_REQUIRED;
        N2InfoContent->ngap_data = N2Ref;

        SNssai->sst = sess->s_nssai.sst;
        SNssai->sd = ogs_s_nssai_sd_to_string(sess->s_nssai.sd);

        N2SmInformation->pdu_session_id = sess->psi;
        N2SmInformation->n2_info_content = N2InfoContent;
        N2SmInformation->s_nssai = SNssai;
        N2SmInformation->is_subject_to_ho = true;
        N2SmInformation->subject_to_ho = 1;
        OpenAPI_list_add(PduSessionList, N2SmInformation);

        message.part[message.num_of_part].pkbuf =
            ogs_pkbuf_alloc(NULL, OGS_MAX_SDU_LEN);
        if (!message.part[message.num_of_part].pkbuf) {
            ogs_error("[%s:%d] Cannot allocate HandoverRequiredTransfer",
                    amf_ue->supi, sess->psi);
            goto cleanup;
        }
        ogs_pkbuf_put_data(message.part[message.num_of_part].pkbuf,
                transfer->buf, transfer->size);
        message.part[message.num_of_part].content_id = content_id;
        message.part[message.num_of_part].content_type =
            (char *)OGS_SBI_CONTENT_NGAP_TYPE;
        message.num_of_part++;
    }

    if (!PduSessionList->count) {
        ogs_error("[%s] Empty pduSessionList for CreateUEContext",
                amf_ue->supi);
        goto cleanup;
    }
    UeContextCreateData.pdu_session_list = PduSessionList;

    /*
     * The callback endpoint is installed in a later increment; the URI is
     * nevertheless mandatory in CreateUEContext and is stable now so that
     * the target AMF can retain it with the handover context.
     */
    callback_header.service.name = (char *)OGS_SBI_SERVICE_NAME_NAMF_CALLBACK;
    callback_header.api.version = (char *)OGS_SBI_API_V1;
    callback_header.resource.component[0] = amf_ue->supi;
    callback_header.resource.component[1] = (char *)"n2-info-notify";

    server = ogs_sbi_server_first();
    if (!server) {
        ogs_error("[%s] No SBI server for n2NotifyUri", amf_ue->supi);
        goto cleanup;
    }
    UeContextCreateData.n2_notify_uri =
        ogs_sbi_server_uri(server, &callback_header);
    if (!UeContextCreateData.n2_notify_uri) {
        ogs_error("[%s] Cannot build n2NotifyUri", amf_ue->supi);
        goto cleanup;
    }

    if (param->cause) {
        NgapCause.group = param->cause->present;
        NgapCause.value = param->cause->choice.radioNetwork;
        UeContextCreateData.ngap_cause = &NgapCause;
    }

    /*
     * TS 29.518 6.1.6.2.41 requires a source AMF complying with current
     * releases to indicate the current Serving Network.
     */
    UeContextCreateData.serving_network =
        ogs_sbi_build_plmn_id_nid(&amf_ue->nr_tai.plmn_id);
    if (!UeContextCreateData.serving_network) {
        ogs_error("[%s] Cannot build servingNetwork", amf_ue->supi);
        goto cleanup;
    }

    message.UeContextCreateData = &UeContextCreateData;
    message.http.accept = (char *)(OGS_SBI_CONTENT_JSON_TYPE ","
            OGS_SBI_CONTENT_NGAP_TYPE "," OGS_SBI_CONTENT_PROBLEM_TYPE);

    request = ogs_sbi_build_request(&message);
    if (!request)
        ogs_error("[%s] Cannot build CreateUEContext request", amf_ue->supi);

cleanup:
    for (i = 0; i < message.num_of_part; i++) {
        if (message.part[i].pkbuf) {
            ogs_pkbuf_free(message.part[i].pkbuf);
            message.part[i].pkbuf = NULL;
        }
    }

    if (UeContextCreateData.target_id)
        amf_nsmf_pdusession_free_target_id(UeContextCreateData.target_id);

    if (UeContextCreateData.n2_notify_uri)
        ogs_free(UeContextCreateData.n2_notify_uri);

    if (UeContextCreateData.serving_network)
        ogs_sbi_free_plmn_id_nid(UeContextCreateData.serving_network);

    if (PduSessionList) {
        OpenAPI_list_for_each(PduSessionList, node) {
            OpenAPI_n2_sm_information_t *n2 = node->data;
            OpenAPI_n2_sm_information_free(n2);
        }
        OpenAPI_list_free(PduSessionList);
    }

    if (SessionContextList) {
        OpenAPI_list_for_each(SessionContextList, node) {
            OpenAPI_pdu_session_context_t *pdu = node->data;
            OpenAPI_pdu_session_context_free(pdu);
        }
        OpenAPI_list_free(SessionContextList);
    }

    if (MmContextList) {
        OpenAPI_list_for_each(MmContextList, node) {
            OpenAPI_mm_context_t *mm = node->data;
            OpenAPI_mm_context_free(mm);
        }
        OpenAPI_list_free(MmContextList);
    }

    if (UeAmbr)
        OpenAPI_ambr_free(UeAmbr);
    if (encoded_gmm_capability)
        ogs_free(encoded_gmm_capability);

    return request;
}

ogs_sbi_request_t *amf_namf_comm_build_ue_context_transfer(
        amf_ue_t *amf_ue, void *data)
{
    ogs_sbi_message_t message;
    ogs_sbi_request_t *request = NULL;
    OpenAPI_ue_context_transfer_req_data_t UeContextTransferReqData;
    char *ue_context_id = NULL;

    ogs_assert(amf_ue);
    ogs_assert(amf_ue->nas.access_type);
    ogs_assert(amf_ue->nas.registration.value);

    ue_context_id = amf_ue_to_context_id(amf_ue);
    ogs_assert(ue_context_id);

    memset(&UeContextTransferReqData, 0, sizeof(UeContextTransferReqData));
    UeContextTransferReqData.access_type = amf_ue->nas.access_type;
    UeContextTransferReqData.reason = amf_ue->nas.registration.value;

    memset(&message, 0, sizeof(message));
    message.h.method = (char *)OGS_SBI_HTTP_METHOD_POST;
    message.h.service.name =
        OpenAPI_service_name_ToString(OpenAPI_service_name_namf_comm);
    message.h.api.version = (char *)OGS_SBI_API_V1;
    message.h.resource.component[0] = (char *)OGS_SBI_RESOURCE_NAME_UE_CONTEXTS;
    message.h.resource.component[1] = ue_context_id;
    message.h.resource.component[2] = (char *)OGS_SBI_RESOURCE_NAME_TRANSFER;
    message.UeContextTransferReqData = &UeContextTransferReqData;

    request = ogs_sbi_build_request(&message);
    ogs_expect(request);

    if (ue_context_id)
        ogs_free(ue_context_id);

    return request;
}

ogs_sbi_request_t *amf_namf_comm_build_registration_status_update(
        amf_ue_t *amf_ue, void *data)
{
    ogs_sbi_message_t message;
    ogs_sbi_request_t *request = NULL;

    OpenAPI_ue_reg_status_update_req_data_t UeRegStatusUpdateReqData;
    char *ue_context_id = NULL;

    ogs_assert(amf_ue);
    ogs_assert(data);

    ue_context_id = ogs_guti_to_string(&amf_ue->old_guti);
    ogs_assert(ue_context_id);

    memset(&message, 0, sizeof(message));
    message.h.method = (char *)OGS_SBI_HTTP_METHOD_POST;
    message.h.service.name =
        OpenAPI_service_name_ToString(OpenAPI_service_name_namf_comm);
    message.h.api.version = (char *)OGS_SBI_API_V1;
    message.h.resource.component[0] =
            (char *)OGS_SBI_RESOURCE_NAME_UE_CONTEXTS;
    message.h.resource.component[1] = ue_context_id;
    message.h.resource.component[2] =
            (char *)OGS_SBI_RESOURCE_NAME_TRANSFER_UPDATE;
    message.UeRegStatusUpdateReqData = &UeRegStatusUpdateReqData;

    memset(&UeRegStatusUpdateReqData, 0, sizeof(UeRegStatusUpdateReqData));

    UeRegStatusUpdateReqData.transfer_status = OGS_POINTER_TO_UINT(data);
    /*
     * TS 29.518
     * 5.2.2.2.2 Registration Status Update
     * If any network slice(s) become no longer available and there are PDU
     * Session(s) associated with them, the target AMF shall include these
     * PDU session(s) in the toReleaseSessionList attribute in the payload.
     */
    if (UeRegStatusUpdateReqData.transfer_status ==
                OpenAPI_ue_context_transfer_status_TRANSFERRED) {
        ogs_assert(amf_ue->to_release_session_list); /* For safety */
        if (amf_ue->to_release_session_list->count) {
            UeRegStatusUpdateReqData.to_release_session_list =
                    amf_ue->to_release_session_list;
        }
    }

    request = ogs_sbi_build_request(&message);
    ogs_expect(request);

    if (ue_context_id)
        ogs_free(ue_context_id);

    return request;
}
