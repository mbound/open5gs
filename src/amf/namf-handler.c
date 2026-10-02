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

#include "namf-handler.h"
#include "namf-build.h"
#include "nsmf-handler.h"

#include "nas-path.h"
#include "ngap-path.h"
#include "sbi-path.h"

static int amf_namf_comm_target_gnb_id(
        OpenAPI_ng_ran_target_id_t *TargetId, uint32_t *gnb_id)
{
    OpenAPI_gnb_id_t *GnbId = NULL;
    uint8_t encoded[4] = {0, };
    int encoded_len;
    int unused_bits;
    uint64_t value;

    ogs_assert(TargetId);
    ogs_assert(gnb_id);

    if (!TargetId->ran_node_id ||
        !TargetId->ran_node_id->g_nb_id ||
        !TargetId->ran_node_id->g_nb_id->g_nb_value)
        return OGS_ERROR;

    GnbId = TargetId->ran_node_id->g_nb_id;
    if (GnbId->bit_length < 22 || GnbId->bit_length > 32)
        return OGS_ERROR;

    encoded_len = strlen(GnbId->g_nb_value);
    if (!encoded_len || (encoded_len & 1) ||
        encoded_len / 2 > (int)sizeof(encoded))
        return OGS_ERROR;

    if (ogs_ascii_to_hex_checked(
            GnbId->g_nb_value, encoded_len,
            encoded, encoded_len / 2) != OGS_OK)
        return OGS_ERROR;

    unused_bits = (encoded_len / 2) * 8 - GnbId->bit_length;
    if (unused_bits < 0 || unused_bits > 7)
        return OGS_ERROR;

    value = ogs_buffer_to_uint64(encoded, encoded_len / 2);
    value >>= unused_bits;
    if (value > UINT32_MAX)
        return OGS_ERROR;

    *gnb_id = (uint32_t)value;
    return OGS_OK;
}

static int amf_namf_comm_target_tai(
        OpenAPI_ng_ran_target_id_t *TargetId, ogs_5gs_tai_t *tai)
{
    OpenAPI_plmn_id_t *PlmnId = NULL;
    char *end = NULL;
    unsigned long mcc, mnc;
    int mnc_len;

    ogs_assert(TargetId);
    ogs_assert(tai);

    if (!TargetId->tai || !TargetId->tai->plmn_id ||
        !TargetId->tai->plmn_id->mcc ||
        !TargetId->tai->plmn_id->mnc ||
        !TargetId->tai->tac)
        return OGS_ERROR;

    PlmnId = TargetId->tai->plmn_id;
    mnc_len = strlen(PlmnId->mnc);
    if (strlen(PlmnId->mcc) != 3 || (mnc_len != 2 && mnc_len != 3))
        return OGS_ERROR;

    mcc = strtoul(PlmnId->mcc, &end, 10);
    if (!end || *end || mcc > 999)
        return OGS_ERROR;

    mnc = strtoul(PlmnId->mnc, &end, 10);
    if (!end || *end || mnc > 999)
        return OGS_ERROR;

    memset(tai, 0, sizeof(*tai));
    ogs_plmn_id_build(&tai->plmn_id,
            (uint16_t)mcc, (uint16_t)mnc, (uint16_t)mnc_len);
    tai->tac = ogs_uint24_from_string_hexadecimal(TargetId->tai->tac);

    return OGS_OK;
}

static int amf_namf_comm_decode_ue_context(
        amf_ue_t *amf_ue, OpenAPI_ue_context_t *UeContext,
        bool save_to_release_session_list);

int amf_namf_comm_handle_create_ue_context_request(
        ogs_sbi_stream_t *stream, ogs_sbi_message_t *recvmsg)
{
    int r;
    uint32_t target_gnb_id = 0;
    ogs_5gs_tai_t target_tai;
    OpenAPI_ue_context_create_data_t *CreateData = NULL;
    OpenAPI_n2_sm_information_t *N2SmInformation = NULL;
    OpenAPI_n2_info_content_t *N2InfoContent = NULL;
    OpenAPI_lnode_t *node = NULL;
    ogs_pkbuf_t *source_to_target = NULL;
    ogs_pkbuf_t *handover_required = NULL;
    amf_gnb_t *target_gnb = NULL;
    ran_ue_t *target_ue = NULL;
    amf_ue_t *amf_ue = NULL;
    amf_sess_t *sess = NULL;
    amf_nsmf_pdusession_sm_context_param_t param;
    char *ue_context_id = NULL;

    ogs_assert(stream);
    ogs_assert(recvmsg);

    memset(&param, 0, sizeof(param));

    CreateData = recvmsg->UeContextCreateData;
    if (!CreateData) {
        ogs_error("No UeContextCreateData");
        return OGS_ERROR;
    }

    ue_context_id = recvmsg->h.resource.component[1];
    if (!ue_context_id) {
        ogs_error("No UE Context ID");
        return OGS_ERROR;
    }

    if (!CreateData->ue_context || !CreateData->ue_context->supi) {
        ogs_error("[%s] No ueContext/SUPI", ue_context_id);
        return OGS_ERROR;
    }
    if (strcmp(ue_context_id, CreateData->ue_context->supi) != 0) {
        ogs_error("[%s] UE Context ID does not match SUPI [%s]",
                ue_context_id, CreateData->ue_context->supi);
        return OGS_ERROR;
    }
    if (!CreateData->target_id) {
        ogs_error("[%s] No targetId", ue_context_id);
        return OGS_ERROR;
    }
    if (!CreateData->source_to_target_data ||
        CreateData->source_to_target_data->ngap_ie_type !=
            OpenAPI_ngap_ie_type_SRC_TO_TAR_CONTAINER ||
        !CreateData->source_to_target_data->ngap_data ||
        !CreateData->source_to_target_data->ngap_data->content_id) {
        ogs_error("[%s] Invalid sourceToTargetData", ue_context_id);
        return OGS_ERROR;
    }
    if (!CreateData->pdu_session_list ||
        CreateData->pdu_session_list->count != 1) {
        ogs_error("[%s] M1 requires exactly one PDU session [count:%d]",
                ue_context_id,
                CreateData->pdu_session_list ?
                    CreateData->pdu_session_list->count : 0);
        return OGS_ERROR;
    }
    if (!CreateData->n2_notify_uri) {
        ogs_error("[%s] No n2NotifyUri", ue_context_id);
        return OGS_ERROR;
    }
    if (!CreateData->ngap_cause) {
        ogs_error("[%s] No ngapCause", ue_context_id);
        return OGS_ERROR;
    }
    if (!CreateData->ue_context->seaf_data ||
        !CreateData->ue_context->seaf_data->key_amf ||
        !CreateData->ue_context->seaf_data->key_amf->key_val ||
        !CreateData->ue_context->seaf_data->nh ||
        !CreateData->ue_context->seaf_data->is_ncc) {
        ogs_error("[%s] Incomplete handover security context", ue_context_id);
        return OGS_ERROR;
    }

    source_to_target = ogs_sbi_find_part_by_content_id(
            recvmsg,
            CreateData->source_to_target_data->ngap_data->content_id);
    if (!source_to_target) {
        ogs_error("[%s] Missing Source-to-Target container [%s]",
                ue_context_id,
                CreateData->source_to_target_data->ngap_data->content_id);
        return OGS_ERROR;
    }

    OpenAPI_list_for_each(CreateData->pdu_session_list, node) {
        N2SmInformation = node->data;
        break;
    }
    if (!N2SmInformation ||
        N2SmInformation->pdu_session_id ==
            OGS_NAS_PDU_SESSION_IDENTITY_UNASSIGNED ||
        !N2SmInformation->n2_info_content) {
        ogs_error("[%s] Invalid pduSessionList", ue_context_id);
        return OGS_ERROR;
    }

    N2InfoContent = N2SmInformation->n2_info_content;
    if (N2InfoContent->ngap_ie_type !=
            OpenAPI_ngap_ie_type_HANDOVER_REQUIRED ||
        !N2InfoContent->ngap_data ||
        !N2InfoContent->ngap_data->content_id) {
        ogs_error("[%s:%d] Invalid HandoverRequired N2 information",
                ue_context_id, N2SmInformation->pdu_session_id);
        return OGS_ERROR;
    }

    handover_required = ogs_sbi_find_part_by_content_id(
            recvmsg, N2InfoContent->ngap_data->content_id);
    if (!handover_required) {
        ogs_error("[%s:%d] Missing HandoverRequired binary part [%s]",
                ue_context_id, N2SmInformation->pdu_session_id,
                N2InfoContent->ngap_data->content_id);
        return OGS_ERROR;
    }

    if (amf_namf_comm_target_gnb_id(
            CreateData->target_id, &target_gnb_id) != OGS_OK) {
        ogs_error("[%s] Invalid target gNB ID", ue_context_id);
        return OGS_ERROR;
    }
    if (amf_namf_comm_target_tai(
            CreateData->target_id, &target_tai) != OGS_OK) {
        ogs_error("[%s] Invalid target TAI", ue_context_id);
        return OGS_ERROR;
    }

    target_gnb = amf_gnb_find_by_gnb_id(target_gnb_id);
    if (!target_gnb) {
        ogs_error("[%s] Target gNB not connected [gNB-ID:0x%x]",
                ue_context_id, target_gnb_id);
        return OGS_ERROR;
    }
    if (amf_find_served_tai(&target_tai) < 0) {
        ogs_error("[%s] Target TAI is not served by this AMF "
                "[PLMN:%06x TAC:%d]", ue_context_id,
                ogs_plmn_id_hexdump(&target_tai.plmn_id), target_tai.tac.v);
        return OGS_ERROR;
    }

    if (amf_ue_find_by_supi(CreateData->ue_context->supi)) {
        ogs_error("[%s] Target AMF already has a UE context", ue_context_id);
        return OGS_ERROR;
    }

    target_ue = ran_ue_add(target_gnb, INVALID_UE_NGAP_ID);
    if (!target_ue) {
        ogs_error("[%s] Cannot allocate target RAN UE", ue_context_id);
        return OGS_ERROR;
    }

    memcpy(&target_ue->saved.nr_tai, &target_tai, sizeof(target_tai));
    memcpy(&target_ue->saved.nr_cgi.plmn_id,
            &target_tai.plmn_id, sizeof(target_tai.plmn_id));

    amf_ue = amf_ue_add(target_ue);
    if (!amf_ue) {
        ran_ue_remove(target_ue);
        return OGS_ERROR;
    }
    amf_ue_associate_ran_ue(amf_ue, target_ue);

    r = amf_namf_comm_decode_ue_context(
            amf_ue, CreateData->ue_context, false);
    if (r != OGS_OK)
        goto cleanup;

    if (!SECURITY_CONTEXT_IS_VALID(amf_ue) ||
        !amf_ue->allowed_nssai.num_of_s_nssai) {
        ogs_error("[%s] Transferred handover context is incomplete",
                ue_context_id);
        goto cleanup;
    }

    sess = amf_sess_find_by_psi(
            amf_ue, N2SmInformation->pdu_session_id);
    if (!sess || !SESSION_CONTEXT_IN_SMF(sess)) {
        ogs_error("[%s:%d] No transferred SM context",
                ue_context_id, N2SmInformation->pdu_session_id);
        goto cleanup;
    }

    amf_ue->handover.inter_amf_target = true;
    amf_ue->handover.create_ue_context_stream_id =
        ogs_sbi_id_from_stream(stream);
    amf_ue->handover.n2_notify_uri =
        ogs_strdup(CreateData->n2_notify_uri);
    ogs_assert(amf_ue->handover.n2_notify_uri);

    amf_ue->handover.type = NGAP_HandoverType_intra5gs;
    amf_ue->handover.group = CreateData->ngap_cause->group;
    amf_ue->handover.cause = CreateData->ngap_cause->value;

    OGS_ASN_CLEAR_DATA(&amf_ue->handover.container);
    ogs_asn_buffer_to_OCTET_STRING(
            source_to_target->data, source_to_target->len,
            &amf_ue->handover.container);

    param.n2smbuf = ogs_pkbuf_alloc(NULL, OGS_MAX_SDU_LEN);
    if (!param.n2smbuf)
        goto cleanup;
    ogs_pkbuf_put_data(
            param.n2smbuf, handover_required->data, handover_required->len);
    param.n2SmInfoType = OpenAPI_n2_sm_info_type_HANDOVER_REQUIRED;
    param.hoState = OpenAPI_ho_state_PREPARING;
    param.targetId = CreateData->target_id;
    param.ngApCause.group = CreateData->ngap_cause->group;
    param.ngApCause.value = CreateData->ngap_cause->value;

    r = amf_sess_sbi_discover_and_send_handover(
            OpenAPI_service_name_nsmf_pdusession, NULL,
            amf_nsmf_pdusession_build_update_sm_context,
            target_ue, sess,
            AMF_UPDATE_SM_CONTEXT_INTER_AMF_HANDOVER_REQUIRED, &param);

    ogs_pkbuf_free(param.n2smbuf);
    param.n2smbuf = NULL;

    if (r != OGS_OK)
        goto cleanup;

    ogs_info("[%s] CreateUEContext accepted for target handover "
            "[gNB-ID:0x%x]", ue_context_id, target_gnb_id);

    /*
     * Do not answer the PUT yet.  The 201 Created response is completed
     * only after target NG-RAN handover preparation and the SMF
     * HandoverRequestAcknowledge processing have both succeeded.
     */
    return OGS_OK;

cleanup:
    if (param.n2smbuf)
        ogs_pkbuf_free(param.n2smbuf);
    if (amf_ue)
        amf_ue_remove(amf_ue);
    if (target_ue)
        ran_ue_remove(target_ue);

    return OGS_ERROR;
}

int amf_namf_comm_send_create_ue_context_response(amf_ue_t *amf_ue)
{
    int i, rv = OGS_ERROR;
    ogs_sbi_stream_t *stream = NULL;
    ogs_sbi_message_t sendmsg;
    ogs_sbi_response_t *response = NULL;
    OpenAPI_ue_context_created_data_t CreatedData;
    OpenAPI_ue_context_t UeContext;
    OpenAPI_n2_info_content_t TargetToSourceData;
    OpenAPI_ref_to_binary_data_t TargetToSourceRef;
    OpenAPI_list_t *PduSessionList = NULL;
    OpenAPI_lnode_t *node = NULL;
    amf_sess_t *sess = NULL;

    ogs_assert(amf_ue);

    if (!amf_ue->handover.inter_amf_target ||
        amf_ue->handover.create_ue_context_stream_id < OGS_MIN_POOL_ID ||
        amf_ue->handover.create_ue_context_stream_id > OGS_MAX_POOL_ID) {
        ogs_error("[%s] No pending CreateUEContext transaction",
                amf_ue->supi);
        return OGS_ERROR;
    }

    stream = ogs_sbi_stream_find_by_id(
            amf_ue->handover.create_ue_context_stream_id);
    if (!stream) {
        ogs_error("[%s] CreateUEContext stream has already been removed",
                amf_ue->supi);
        amf_ue->handover.create_ue_context_stream_id = OGS_INVALID_POOL_ID;
        return OGS_NOTFOUND;
    }

    memset(&sendmsg, 0, sizeof(sendmsg));
    memset(&CreatedData, 0, sizeof(CreatedData));
    memset(&UeContext, 0, sizeof(UeContext));
    memset(&TargetToSourceData, 0, sizeof(TargetToSourceData));
    memset(&TargetToSourceRef, 0, sizeof(TargetToSourceRef));

    UeContext.supi = amf_ue->supi;
    CreatedData.ue_context = &UeContext;

    if (!amf_ue->handover.container.buf ||
        !amf_ue->handover.container.size) {
        ogs_error("[%s] No Target-to-Source Transparent Container",
                amf_ue->supi);
        goto cleanup;
    }

    TargetToSourceRef.content_id = (char *)"target-to-source-container";
    TargetToSourceData.ngap_ie_type =
        OpenAPI_ngap_ie_type_TAR_TO_SRC_CONTAINER;
    TargetToSourceData.ngap_data = &TargetToSourceRef;
    CreatedData.target_to_source_data = &TargetToSourceData;

    sendmsg.part[sendmsg.num_of_part].pkbuf =
        ogs_pkbuf_alloc(NULL, OGS_MAX_SDU_LEN);
    if (!sendmsg.part[sendmsg.num_of_part].pkbuf)
        goto cleanup;
    ogs_pkbuf_put_data(sendmsg.part[sendmsg.num_of_part].pkbuf,
            amf_ue->handover.container.buf,
            amf_ue->handover.container.size);
    sendmsg.part[sendmsg.num_of_part].content_id =
        TargetToSourceRef.content_id;
    sendmsg.part[sendmsg.num_of_part].content_type =
        (char *)OGS_SBI_CONTENT_NGAP_TYPE;
    sendmsg.num_of_part++;

    PduSessionList = OpenAPI_list_create();
    ogs_assert(PduSessionList);

    ogs_list_for_each(&amf_ue->sess_list, sess) {
        OpenAPI_n2_sm_information_t *N2SmInformation = NULL;
        OpenAPI_n2_info_content_t *N2InfoContent = NULL;
        OpenAPI_ref_to_binary_data_t *N2Ref = NULL;
        OpenAPI_snssai_t *SNssai = NULL;
        char *content_id = NULL;

        if (!sess->transfer.handover_command)
            continue;

        if (sendmsg.num_of_part >= OGS_SBI_MAX_NUM_OF_PART) {
            ogs_error("[%s] Too many CreateUEContext response parts",
                    amf_ue->supi);
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

        content_id = ogs_msprintf("handover-command-%d", sess->psi);
        ogs_assert(content_id);

        N2Ref->content_id = content_id;
        N2InfoContent->ngap_ie_type = OpenAPI_ngap_ie_type_HANDOVER_CMD;
        N2InfoContent->ngap_data = N2Ref;

        SNssai->sst = sess->s_nssai.sst;
        SNssai->sd = ogs_s_nssai_sd_to_string(sess->s_nssai.sd);

        N2SmInformation->pdu_session_id = sess->psi;
        N2SmInformation->n2_info_content = N2InfoContent;
        N2SmInformation->s_nssai = SNssai;
        OpenAPI_list_add(PduSessionList, N2SmInformation);

        sendmsg.part[sendmsg.num_of_part].pkbuf =
            ogs_pkbuf_copy(sess->transfer.handover_command);
        if (!sendmsg.part[sendmsg.num_of_part].pkbuf)
            goto cleanup;
        sendmsg.part[sendmsg.num_of_part].content_id = content_id;
        sendmsg.part[sendmsg.num_of_part].content_type =
            (char *)OGS_SBI_CONTENT_NGAP_TYPE;
        sendmsg.num_of_part++;
    }

    if (!PduSessionList->count) {
        ogs_error("[%s] No prepared PDU session for CreateUEContext response",
                amf_ue->supi);
        goto cleanup;
    }

    CreatedData.pdu_session_list = PduSessionList;
    sendmsg.UeContextCreatedData = &CreatedData;

    response = ogs_sbi_build_response(
            &sendmsg, OGS_SBI_HTTP_STATUS_CREATED);
    if (!response) {
        ogs_error("[%s] Cannot build CreateUEContext response",
                amf_ue->supi);
        goto cleanup;
    }

    if (ogs_sbi_server_send_response(stream, response) != true) {
        ogs_error("[%s] Cannot send CreateUEContext response",
                amf_ue->supi);
        goto cleanup;
    }

    ogs_info("[%s] Target AMF completed CreateUEContext", amf_ue->supi);
    amf_ue->handover.create_ue_context_stream_id = OGS_INVALID_POOL_ID;
    rv = OGS_OK;

cleanup:
    for (i = 0; i < sendmsg.num_of_part; i++) {
        if (sendmsg.part[i].pkbuf) {
            ogs_pkbuf_free(sendmsg.part[i].pkbuf);
            sendmsg.part[i].pkbuf = NULL;
        }
    }

    if (PduSessionList) {
        OpenAPI_list_for_each(PduSessionList, node) {
            OpenAPI_n2_sm_information_t *n2 = node->data;
            OpenAPI_n2_sm_information_free(n2);
        }
        OpenAPI_list_free(PduSessionList);
    }

    return rv;
}


int amf_namf_comm_handle_create_ue_context_response(
        ogs_sbi_message_t *recvmsg, amf_ue_t *amf_ue)
{
    int r;
    ran_ue_t *source_ue = NULL;
    amf_sess_t *sess = NULL;
    OpenAPI_ue_context_created_data_t *created = NULL;
    OpenAPI_n2_info_content_t *n2_info = NULL;
    OpenAPI_ref_to_binary_data_t *ref = NULL;
    OpenAPI_lnode_t *node = NULL;
    ogs_pkbuf_t *n2buf = NULL;
    NGAP_Cause_t failure_cause;

    ogs_assert(recvmsg);
    ogs_assert(amf_ue);

    source_ue = ran_ue_find_by_id(amf_ue->ran_ue_id);
    if (!source_ue) {
        ogs_error("[%s] Source NG context has already been removed",
                amf_ue->supi);
        return OGS_NOTFOUND;
    }

    if (recvmsg->res_status != OGS_SBI_HTTP_STATUS_CREATED) {
        ogs_error("[%s] CreateUEContext failed [HTTP:%d]",
                amf_ue->supi, recvmsg->res_status);
        goto preparation_failure;
    }

    created = recvmsg->UeContextCreatedData;
    if (!created || !created->ue_context ||
        !created->target_to_source_data ||
        !created->pdu_session_list ||
        created->pdu_session_list->count == 0) {
        ogs_error("[%s] Incomplete UeContextCreatedData", amf_ue->supi);
        goto preparation_failure;
    }

    n2_info = created->target_to_source_data;
    if (n2_info->ngap_ie_type != OpenAPI_ngap_ie_type_TAR_TO_SRC_CONTAINER ||
        !n2_info->ngap_data || !n2_info->ngap_data->content_id) {
        ogs_error("[%s] Invalid Target-to-Source Transparent Container",
                amf_ue->supi);
        goto preparation_failure;
    }

    ref = n2_info->ngap_data;
    n2buf = ogs_sbi_find_part_by_content_id(recvmsg, ref->content_id);
    if (!n2buf) {
        ogs_error("[%s] Missing Target-to-Source binary part [%s]",
                amf_ue->supi, ref->content_id);
        goto preparation_failure;
    }

    OGS_ASN_CLEAR_DATA(&amf_ue->handover.container);
    ogs_asn_buffer_to_OCTET_STRING(
            n2buf->data, n2buf->len, &amf_ue->handover.container);

    /* Do not let a previous handover leave stale Handover Command Transfer. */
    AMF_UE_CLEAR_N2_TRANSFER(amf_ue, handover_command);

    OpenAPI_list_for_each(created->pdu_session_list, node) {
        OpenAPI_n2_sm_information_t *n2sm = node->data;
        ogs_pkbuf_t *copy = NULL;

        if (!n2sm ||
            n2sm->pdu_session_id ==
                OGS_NAS_PDU_SESSION_IDENTITY_UNASSIGNED ||
            !n2sm->n2_info_content) {
            ogs_error("[%s] Invalid PDU session in CreateUEContext response",
                    amf_ue->supi);
            goto preparation_failure;
        }

        sess = amf_sess_find_by_psi(amf_ue, n2sm->pdu_session_id);
        if (!sess) {
            ogs_error("[%s:%d] Unknown PDU session in CreateUEContext response",
                    amf_ue->supi, n2sm->pdu_session_id);
            goto preparation_failure;
        }

        n2_info = n2sm->n2_info_content;
        if (n2_info->ngap_ie_type != OpenAPI_ngap_ie_type_HANDOVER_CMD ||
            !n2_info->ngap_data || !n2_info->ngap_data->content_id) {
            ogs_error("[%s:%d] Invalid Handover Command Transfer",
                    amf_ue->supi, sess->psi);
            goto preparation_failure;
        }

        n2buf = ogs_sbi_find_part_by_content_id(
                recvmsg, n2_info->ngap_data->content_id);
        if (!n2buf) {
            ogs_error("[%s:%d] Missing Handover Command binary part [%s]",
                    amf_ue->supi, sess->psi,
                    n2_info->ngap_data->content_id);
            goto preparation_failure;
        }

        copy = ogs_pkbuf_copy(n2buf);
        if (!copy) {
            ogs_error("[%s:%d] Cannot copy Handover Command Transfer",
                    amf_ue->supi, sess->psi);
            goto preparation_failure;
        }
        AMF_SESS_STORE_N2_TRANSFER(sess, handover_command, copy);
    }

    if (created->failed_session_list &&
            created->failed_session_list->count) {
        /*
         * M1 intentionally targets a single successfully handed-over PDU
         * session.  Do not silently drop Handover Preparation Unsuccessful
         * Transfer IEs; support for the NGAP release list is a later
         * increment.
         */
        ogs_error("[%s] failedSessionList is not supported in M1",
                amf_ue->supi);
        goto preparation_failure;
    }

    r = ngap_send_handover_command(amf_ue);
    if (r != OGS_OK) {
        ogs_error("[%s] Cannot send HandoverCommand [error:%d]",
                amf_ue->supi, r);
        return r;
    }

    ogs_info("[%s] Inter-AMF handover preparation completed at source AMF",
            amf_ue->supi);
    return OGS_OK;

preparation_failure:
    AMF_UE_CLEAR_N2_TRANSFER(amf_ue, handover_command);

    memset(&failure_cause, 0, sizeof(failure_cause));
    failure_cause.present = NGAP_Cause_PR_radioNetwork;
    failure_cause.choice.radioNetwork =
        NGAP_CauseRadioNetwork_ho_failure_in_target_5GC_ngran_node_or_target_system;

    r = ngap_send_handover_preparation_failure(source_ue, &failure_cause);
    ogs_expect(r == OGS_OK);
    return OGS_ERROR;
}

int amf_namf_comm_handle_n1_n2_message_transfer(
        ogs_sbi_stream_t *stream, ogs_sbi_message_t *recvmsg)
{
    int status, r;

    amf_ue_t *amf_ue = NULL;
    ran_ue_t *ran_ue = NULL;
    amf_sess_t *sess = NULL;

    ogs_pkbuf_t *n1buf = NULL;
    ogs_pkbuf_t *n2buf = NULL;

    ogs_pkbuf_t *gmmbuf = NULL;
    ogs_pkbuf_t *ngapbuf = NULL;

    char *supi = NULL;
    uint8_t pdu_session_id = OGS_NAS_PDU_SESSION_IDENTITY_UNASSIGNED;

    ogs_sbi_message_t sendmsg;
    ogs_sbi_response_t *response = NULL;

    OpenAPI_n1_n2_message_transfer_req_data_t *N1N2MessageTransferReqData;
    OpenAPI_n1_n2_message_transfer_rsp_data_t N1N2MessageTransferRspData;
    OpenAPI_n1_message_container_t *n1MessageContainer = NULL;
    OpenAPI_ref_to_binary_data_t *n1MessageContent = NULL;
    OpenAPI_n2_info_container_t *n2InfoContainer = NULL;
    OpenAPI_n2_sm_information_t *smInfo = NULL;
    OpenAPI_n2_info_content_t *n2InfoContent = NULL;
    OpenAPI_ref_to_binary_data_t *ngapData = NULL;

    OpenAPI_ngap_ie_type_e ngapIeType = OpenAPI_ngap_ie_type_NULL;

    ogs_assert(stream);
    ogs_assert(recvmsg);

    N1N2MessageTransferReqData = recvmsg->N1N2MessageTransferReqData;
    if (!N1N2MessageTransferReqData) {
        ogs_error("No N1N2MessageTransferReqData");
        return OGS_ERROR;
    }

    if (N1N2MessageTransferReqData->is_pdu_session_id == false) {
        ogs_error("No PDU Session Identity");
        return OGS_ERROR;
    }
    pdu_session_id = N1N2MessageTransferReqData->pdu_session_id;

    supi = recvmsg->h.resource.component[1];
    if (!supi) {
        ogs_error("No SUPI");
        return OGS_ERROR;
    }

    amf_ue = amf_ue_find_by_supi(supi);
    if (!amf_ue) {
        ogs_error("No UE context [%s]", supi);
        return OGS_ERROR;
    }

    sess = amf_sess_find_by_psi(amf_ue, pdu_session_id);
    if (!sess) {
        ogs_error("[%s] No PDU Session Context [%d]",
                amf_ue->supi, pdu_session_id);
        return OGS_ERROR;
    }

    n1MessageContainer = N1N2MessageTransferReqData->n1_message_container;
    if (n1MessageContainer) {
        n1MessageContent = n1MessageContainer->n1_message_content;
        if (!n1MessageContent || !n1MessageContent->content_id) {
            ogs_error("No n1MessageContent");
            return OGS_ERROR;
        }

        n1buf = ogs_sbi_find_part_by_content_id(
                recvmsg, n1MessageContent->content_id);
        if (!n1buf) {
            ogs_error("[%s] No N1 SM Content", amf_ue->supi);
            return OGS_ERROR;
        }

        /*
         * NOTE : The pkbuf created in the SBI message will be removed
         *        from ogs_sbi_message_free(), so it must be copied.
         */
        n1buf = ogs_pkbuf_copy(n1buf);
        ogs_assert(n1buf);
    }

    n2InfoContainer = N1N2MessageTransferReqData->n2_info_container;
    if (n2InfoContainer) {
        smInfo = n2InfoContainer->sm_info;
        if (!smInfo) {
            ogs_error("No smInfo");
            return OGS_ERROR;
        }

        n2InfoContent = smInfo->n2_info_content;
        if (!n2InfoContent) {
            ogs_error("No n2InfoContent");
            return OGS_ERROR;
        }

        ngapIeType = n2InfoContent->ngap_ie_type;

        ngapData = n2InfoContent->ngap_data;
        if (!ngapData || !ngapData->content_id) {
            ogs_error("No ngapData");
            return OGS_ERROR;
        }
        n2buf = ogs_sbi_find_part_by_content_id(
                recvmsg, ngapData->content_id);
        if (!n2buf) {
            ogs_error("[%s] No N2 SM Content", amf_ue->supi);
            return OGS_ERROR;
        }

        /*
         * NOTE : The pkbuf created in the SBI message will be removed
         *        from ogs_sbi_message_free(), so it must be copied.
         */
        n2buf = ogs_pkbuf_copy(n2buf);
        ogs_assert(n2buf);
    }

    memset(&sendmsg, 0, sizeof(sendmsg));

    status = OGS_SBI_HTTP_STATUS_OK;

    memset(&N1N2MessageTransferRspData, 0, sizeof(N1N2MessageTransferRspData));
    N1N2MessageTransferRspData.cause =
        OpenAPI_n1_n2_message_transfer_cause_N1_N2_TRANSFER_INITIATED;

    sendmsg.N1N2MessageTransferRspData = &N1N2MessageTransferRspData;

    switch (ngapIeType) {
    case OpenAPI_ngap_ie_type_PDU_RES_SETUP_REQ:
        if (!n2buf) {
            ogs_error("[%s] No N2 SM Content", amf_ue->supi);
            return OGS_ERROR;
        }

        if (n1buf) {
            gmmbuf = gmm_build_dl_nas_transport(sess,
                    OGS_NAS_PAYLOAD_CONTAINER_N1_SM_INFORMATION, n1buf, 0, 0);
            ogs_assert(gmmbuf);
        }

        if (gmmbuf) {
            /***********************************
             * 4.3.2 PDU Session Establishment *
             ***********************************/

            ran_ue = ran_ue_find_by_id(sess->ran_ue_id);
            if (ran_ue) {
                if (sess->pdu_session_establishment_accept) {
                    ogs_pkbuf_free(sess->pdu_session_establishment_accept);
                    sess->pdu_session_establishment_accept = NULL;
                }

                if (ran_ue->initial_context_setup_request_sent == true) {
                    ngapbuf =
                        ngap_sess_build_pdu_session_resource_setup_request(
                                ran_ue, sess, gmmbuf, n2buf);
                    ogs_assert(ngapbuf);
                } else {
                    ngapbuf = ngap_sess_build_initial_context_setup_request(
                            ran_ue, sess, gmmbuf, n2buf);
                    ogs_assert(ngapbuf);

                    ran_ue->initial_context_setup_request_sent = true;
                }

                if (SESSION_CONTEXT_IN_SMF(sess)) {
                /*
                 * [1-CLIENT] /nsmf-pdusession/v1/sm-contexts
                 * [2-SERVER] /namf-comm/v1/ue-contexts/{supi}/n1-n2-messages
                 *
                 * If [2-SERVER] arrives after [1-CLIENT],
                 * sm-context-ref is created in [1-CLIENT].
                 * So, the PDU session establishment accpet can be transmitted.
                 */
                    r = ngap_send_to_ran_ue(ran_ue, ngapbuf);
                    ogs_expect(r == OGS_OK);
                    ogs_assert(r != OGS_ERROR);
                } else {
                    sess->pdu_session_establishment_accept = ngapbuf;
                }
            } else {
                ogs_warn("[%s] RAN-NG Context has already been removed",
                            amf_ue->supi);
            }

        } else {
            /*********************************************
             * 4.2.3.3 Network Triggered Service Request *
             *********************************************/

            if (CM_IDLE(amf_ue)) {
                bool rc;
                ogs_sbi_server_t *server = NULL;
                ogs_sbi_header_t header;
                ogs_sbi_client_t *client = NULL;
                OpenAPI_uri_scheme_e scheme = OpenAPI_uri_scheme_NULL;
                char *fqdn = NULL;
                uint16_t fqdn_port = 0;
                ogs_sockaddr_t *addr = NULL, *addr6 = NULL;

                if (!N1N2MessageTransferReqData->n1n2_failure_txf_notif_uri) {
                    ogs_error("[%s:%d] No n1-n2-failure-notification-uri",
                            amf_ue->supi, sess->psi);
                    return OGS_ERROR;
                }

                rc = ogs_sbi_getaddr_from_uri(
                        &scheme, &fqdn, &fqdn_port, &addr, &addr6,
                        N1N2MessageTransferReqData->n1n2_failure_txf_notif_uri);
                if (rc == false || scheme == OpenAPI_uri_scheme_NULL) {
                    ogs_error("[%s:%d] Invalid URI [%s]",
                            amf_ue->supi, sess->psi,
                            N1N2MessageTransferReqData->
                                n1n2_failure_txf_notif_uri);
                    return OGS_ERROR;
                }

                client = ogs_sbi_client_find(
                        scheme, fqdn, fqdn_port, addr, addr6);
                if (!client) {
                    ogs_debug("%s: ogs_sbi_client_add()", OGS_FUNC);
                    client = ogs_sbi_client_add(
                            scheme, fqdn, fqdn_port, addr, addr6);
                    if (!client) {
                        ogs_error("%s: ogs_sbi_client_add() failed", OGS_FUNC);

                        ogs_free(fqdn);
                        ogs_freeaddrinfo(addr);
                        ogs_freeaddrinfo(addr6);

                        return OGS_ERROR;
                    }
                }
                OGS_SBI_SETUP_CLIENT(&sess->paging, client);

                ogs_free(fqdn);
                ogs_freeaddrinfo(addr);
                ogs_freeaddrinfo(addr6);

                status = OGS_SBI_HTTP_STATUS_ACCEPTED;
                N1N2MessageTransferRspData.cause =
                    OpenAPI_n1_n2_message_transfer_cause_ATTEMPTING_TO_REACH_UE;

                /* Location */
                server = ogs_sbi_server_from_stream(stream);
                ogs_assert(server);

                memset(&header, 0, sizeof(header));
                header.service.name =
                    OpenAPI_service_name_ToString(
                            OpenAPI_service_name_namf_comm);
                header.api.version = (char *)OGS_SBI_API_V1;
                header.resource.component[0] =
                    (char *)OGS_SBI_RESOURCE_NAME_UE_CONTEXTS;
                header.resource.component[1] = amf_ue->supi;
                header.resource.component[2] =
                    (char *)OGS_SBI_RESOURCE_NAME_N1_N2_MESSAGES;
                header.resource.component[3] = sess->sm_context_ref;

                sendmsg.http.location = ogs_sbi_server_uri(server, &header);

                /* Store Paging Info */
                AMF_SESS_STORE_PAGING_INFO(
                    sess, sendmsg.http.location,
                    N1N2MessageTransferReqData->n1n2_failure_txf_notif_uri);

                /* Store N2 Transfer message */
                AMF_SESS_STORE_N2_TRANSFER(
                        sess, pdu_session_resource_setup_request, n2buf);

                r = ngap_send_paging(amf_ue);
                ogs_expect(r == OGS_OK);
                ogs_assert(r != OGS_ERROR);

            } else if (CM_CONNECTED(amf_ue)) {
                r = nas_send_pdu_session_setup_request(sess, NULL, n2buf);
                ogs_expect(r == OGS_OK);
                ogs_assert(r != OGS_ERROR);

            } else {

                ogs_fatal("[%s] Invalid AMF-UE state", amf_ue->supi);
                ogs_assert_if_reached();

            }

        }
        break;

    case OpenAPI_ngap_ie_type_PDU_RES_MOD_REQ:
        if (!n1buf) {
            ogs_error("[%s] No N1 SM Content", amf_ue->supi);
            return OGS_ERROR;
        }
        if (!n2buf) {
            ogs_error("[%s] No N2 SM Content", amf_ue->supi);
            return OGS_ERROR;
        }

        if (CM_IDLE(amf_ue)) {
            ogs_sbi_server_t *server = NULL;
            ogs_sbi_header_t header;

            status = OGS_SBI_HTTP_STATUS_ACCEPTED;
            N1N2MessageTransferRspData.cause =
                OpenAPI_n1_n2_message_transfer_cause_ATTEMPTING_TO_REACH_UE;

            /* Location */
            server = ogs_sbi_server_from_stream(stream);
            ogs_assert(server);

            memset(&header, 0, sizeof(header));
            header.service.name =
                OpenAPI_service_name_ToString(
                        OpenAPI_service_name_namf_comm);
            header.api.version = (char *)OGS_SBI_API_V1;
            header.resource.component[0] =
                (char *)OGS_SBI_RESOURCE_NAME_UE_CONTEXTS;
            header.resource.component[1] = amf_ue->supi;
            header.resource.component[2] =
                (char *)OGS_SBI_RESOURCE_NAME_N1_N2_MESSAGES;
            header.resource.component[3] = sess->sm_context_ref;

            sendmsg.http.location = ogs_sbi_server_uri(server, &header);

            /* Store Paging Info */
            AMF_SESS_STORE_PAGING_INFO(sess, sendmsg.http.location, NULL);

            /* Store 5GSM Message */
            AMF_SESS_STORE_5GSM_MESSAGE(sess,
                    OGS_NAS_5GS_PDU_SESSION_MODIFICATION_COMMAND,
                    n1buf, n2buf);

            r = ngap_send_paging(amf_ue);
            ogs_expect(r == OGS_OK);
            ogs_assert(r != OGS_ERROR);

        } else if (CM_CONNECTED(amf_ue)) {
            if (CONTEXT_SETUP_ESTABLISHED(amf_ue)) {
                r = nas_send_pdu_session_modification_command(
                        sess, n1buf, n2buf);
                ogs_expect(r == OGS_OK);
                ogs_assert(r != OGS_ERROR);
            } else {
                /* Store 5GSM Message */
                ogs_warn("[Session MODIFY] Context setup is not established");
                AMF_SESS_STORE_5GSM_MESSAGE(sess,
                        OGS_NAS_5GS_PDU_SESSION_MODIFICATION_COMMAND,
                        n1buf, n2buf);
            }
        } else {
            ogs_fatal("[%s] Invalid AMF-UE state", amf_ue->supi);
            ogs_assert_if_reached();
        }

        break;

    case OpenAPI_ngap_ie_type_PDU_RES_REL_CMD:
        if (!n2buf) {
            ogs_error("[%s] No N2 SM Content", amf_ue->supi);
            return OGS_ERROR;
        }

        if (CM_IDLE(amf_ue)) {
            if (N1N2MessageTransferReqData->is_skip_ind == true &&
                N1N2MessageTransferReqData->skip_ind == true) {

                if (n1buf)
                    ogs_pkbuf_free(n1buf);
                if (n2buf)
                    ogs_pkbuf_free(n2buf);

                N1N2MessageTransferRspData.cause =
                    OpenAPI_n1_n2_message_transfer_cause_N1_MSG_NOT_TRANSFERRED;

            } else {
                ogs_sbi_server_t *server = NULL;
                ogs_sbi_header_t header;

                status = OGS_SBI_HTTP_STATUS_ACCEPTED;
                N1N2MessageTransferRspData.cause =
                    OpenAPI_n1_n2_message_transfer_cause_ATTEMPTING_TO_REACH_UE;

                /* Location */
                server = ogs_sbi_server_from_stream(stream);
                ogs_assert(server);

                memset(&header, 0, sizeof(header));
                header.service.name =
                    OpenAPI_service_name_ToString(
                            OpenAPI_service_name_namf_comm);
                header.api.version = (char *)OGS_SBI_API_V1;
                header.resource.component[0] =
                    (char *)OGS_SBI_RESOURCE_NAME_UE_CONTEXTS;
                header.resource.component[1] = amf_ue->supi;
                header.resource.component[2] =
                    (char *)OGS_SBI_RESOURCE_NAME_N1_N2_MESSAGES;
                header.resource.component[3] = sess->sm_context_ref;

                sendmsg.http.location = ogs_sbi_server_uri(server, &header);

                /* Store Paging Info */
                AMF_SESS_STORE_PAGING_INFO(sess, sendmsg.http.location, NULL);

                /* Store 5GSM Message */
                AMF_SESS_STORE_5GSM_MESSAGE(sess,
                        OGS_NAS_5GS_PDU_SESSION_RELEASE_COMMAND,
                        n1buf, n2buf);

                r = ngap_send_paging(amf_ue);
                ogs_expect(r == OGS_OK);
                ogs_assert(r != OGS_ERROR);
            }

        } else if (CM_CONNECTED(amf_ue)) {
            if (CONTEXT_SETUP_ESTABLISHED(amf_ue)) {
                r = nas_send_pdu_session_release_command(sess, n1buf, n2buf);
                ogs_expect(r == OGS_OK);
                ogs_assert(r != OGS_ERROR);
            } else {
                /* Store 5GSM Message */
                ogs_warn("[Session RELEASE] Context setup is not established");
                AMF_SESS_STORE_5GSM_MESSAGE(sess,
                        OGS_NAS_5GS_PDU_SESSION_RELEASE_COMMAND,
                        n1buf, n2buf);
            }
        } else {
            ogs_fatal("[%s] Invalid AMF-UE state", amf_ue->supi);
            ogs_assert_if_reached();
        }
        break;

    case OpenAPI_ngap_ie_type_NULL:
        /*
         * No n2InfoContainer. According to TS23.502, this means that SMF has
         * encountered an error and is rejecting the session.
         *
         * TS23.502
         * 6.3.1.7 4.3.2.2 UE Requested PDU Session Establishment
         * p100
         * 11.  ...
         * If the PDU session establishment failed anywhere between step 5
         * and step 11, then the Namf_Communication_N1N2MessageTransfer
         * request shall include the N1 SM container with a PDU Session
         * Establishment Reject message ...
         */
        if (!n1buf) {
            ogs_error("[%s] No N1 SM Content", amf_ue->supi);
            return OGS_ERROR;
        }

        ogs_error("[%d:%d] PDU session establishment reject",
                sess->psi, sess->pti);

        r = nas_5gs_send_gsm_reject(
                ran_ue_find_by_id(sess->ran_ue_id), sess,
                OGS_NAS_PAYLOAD_CONTAINER_N1_SM_INFORMATION, n1buf);
        ogs_expect(r == OGS_OK);
        ogs_assert(r != OGS_ERROR);

        amf_sess_remove(sess);
        break;

    default:
        ogs_error("Unsupported ngapIeType[%d]", ngapIeType);
        status = OGS_SBI_HTTP_STATUS_BAD_REQUEST;
        break;
    }

    response = ogs_sbi_build_response(&sendmsg, status);
    ogs_assert(response);
    ogs_assert(true == ogs_sbi_server_send_response(stream, response));

    if (sendmsg.http.location)
        ogs_free(sendmsg.http.location);

    return OGS_OK;
}

int amf_namf_callback_handle_sm_context_status(
        ogs_sbi_stream_t *stream, ogs_sbi_message_t *recvmsg)
{
    int status = OGS_SBI_HTTP_STATUS_NO_CONTENT;

    amf_ue_t *amf_ue = NULL;
    amf_sess_t *sess = NULL;

    uint8_t pdu_session_identity;

    ogs_sbi_message_t sendmsg;
    ogs_sbi_response_t *response = NULL;

    OpenAPI_sm_context_status_notification_t *SmContextStatusNotification;
    OpenAPI_status_info_t *StatusInfo;

    ogs_assert(stream);
    ogs_assert(recvmsg);

    if (!recvmsg->h.resource.component[0]) {
        status = OGS_SBI_HTTP_STATUS_BAD_REQUEST;
        ogs_error("No SUPI");
        goto cleanup;
    }

    amf_ue = amf_ue_find_by_supi(recvmsg->h.resource.component[0]);
    if (!amf_ue) {
        status = OGS_SBI_HTTP_STATUS_NOT_FOUND;
        ogs_error("Cannot find SUPI [%s]", recvmsg->h.resource.component[0]);
        goto cleanup;
    }

    if (!recvmsg->h.resource.component[2]) {
        status = OGS_SBI_HTTP_STATUS_BAD_REQUEST;
        ogs_error("[%s] No PDU Session Identity", amf_ue->supi);
        goto cleanup;
    }

    pdu_session_identity = atoi(recvmsg->h.resource.component[2]);
    if (pdu_session_identity == OGS_NAS_PDU_SESSION_IDENTITY_UNASSIGNED) {
        status = OGS_SBI_HTTP_STATUS_BAD_REQUEST;
        ogs_error("[%s] PDU Session Identity is unassigned", amf_ue->supi);
        goto cleanup;
    }

    sess = amf_sess_find_by_psi(amf_ue, pdu_session_identity);
    if (!sess) {
        status = OGS_SBI_HTTP_STATUS_NOT_FOUND;
        ogs_warn("[%s] Cannot find session", amf_ue->supi);
        goto cleanup;
    }

    SmContextStatusNotification = recvmsg->SmContextStatusNotification;
    if (!SmContextStatusNotification) {
        status = OGS_SBI_HTTP_STATUS_BAD_REQUEST;
        ogs_error("[%s:%d] No SmContextStatusNotification",
                amf_ue->supi, sess->psi);
        goto cleanup;
    }

    StatusInfo = SmContextStatusNotification->status_info;
    if (!StatusInfo) {
        status = OGS_SBI_HTTP_STATUS_BAD_REQUEST;
        ogs_error("[%s:%d] No StatusInfo", amf_ue->supi, sess->psi);
        goto cleanup;
    }

    sess->resource_status = StatusInfo->resource_status;

    /*
     * Race condition for PDU session release complete
     *  - CLIENT : /nsmf-pdusession/v1/sm-contexts/{smContextRef}/modify
     *  - SERVER : /namf-callback/v1/{supi}/sm-context-status/{psi})
     *
     * If NOTIFICATION is received before the CLIENT response is received,
     * CLIENT sync is not finished. In this case, the session context
     * should not be removed.
     *
     * If NOTIFICATION comes after the CLIENT response is received,
     * sync is done. So, the session context can be removed.
     */
    ogs_info("[%s:%d][%d:%d:%s] "
            "/namf-callback/v1/{supi}/sm-context-status/{psi}",
            amf_ue->supi, sess->psi,
            sess->n1_released, sess->n2_released,
            OpenAPI_resource_status_ToString(sess->resource_status));

    if (sess->n1_released == true &&
        sess->n2_released == true &&
        sess->resource_status == OpenAPI_resource_status_RELEASED) {
        amf_nsmf_pdusession_handle_release_sm_context(
                amf_ue, ran_ue_find_by_id(sess->ran_ue_id),
                sess, AMF_RELEASE_SM_CONTEXT_NO_STATE);
    }

cleanup:
    memset(&sendmsg, 0, sizeof(sendmsg));

    response = ogs_sbi_build_response(&sendmsg, status);
    ogs_assert(response);
    ogs_assert(true == ogs_sbi_server_send_response(stream, response));

    return OGS_OK;
}

int amf_namf_callback_handle_dereg_notify(
        ogs_sbi_stream_t *stream, ogs_sbi_message_t *recvmsg)
{
    int r, state, status = OGS_SBI_HTTP_STATUS_NO_CONTENT;

    amf_ue_t *amf_ue = NULL;

    ogs_sbi_message_t sendmsg;
    ogs_sbi_response_t *response = NULL;

    OpenAPI_deregistration_data_t *DeregistrationData;

    ogs_assert(stream);
    ogs_assert(recvmsg);

    if (!recvmsg->h.resource.component[0]) {
        status = OGS_SBI_HTTP_STATUS_BAD_REQUEST;
        ogs_error("No SUPI");
        goto cleanup;
    }

    amf_ue = amf_ue_find_by_supi(recvmsg->h.resource.component[0]);
    if (!amf_ue) {
        status = OGS_SBI_HTTP_STATUS_NOT_FOUND;
        ogs_error("Cannot find SUPI [%s]", recvmsg->h.resource.component[0]);
        goto cleanup;
    }

    DeregistrationData = recvmsg->DeregistrationData;
    if (!DeregistrationData) {
        status = OGS_SBI_HTTP_STATUS_BAD_REQUEST;
        ogs_error("[%s] No DeregistrationData", amf_ue->supi);
        goto cleanup;
    }

    if (DeregistrationData->dereg_reason ==
            OpenAPI_deregistration_reason_NULL) {
        status = OGS_SBI_HTTP_STATUS_BAD_REQUEST;
        ogs_error("[%s] No Deregistraion Reason ", amf_ue->supi);
        goto cleanup;
    }

    if (DeregistrationData->access_type != OpenAPI_access_type_3GPP_ACCESS) {
        status = OGS_SBI_HTTP_STATUS_BAD_REQUEST;
        ogs_error("[%s] Deregistration access type not 3GPP", amf_ue->supi);
        goto cleanup;
    }

    ogs_info("Deregistration notify reason: %s:%s:%s",
        amf_ue->supi,
        OpenAPI_deregistration_reason_ToString(DeregistrationData->dereg_reason),
        OpenAPI_access_type_ToString(DeregistrationData->access_type));

    /*
     * TODO: do not start deregistration if UE has emergency sessions
     * 4.2.2.3.3
     * If the UE has established PDU Session associated with emergency service, the AMF shall not initiate
     * Deregistration procedure. In this case, the AMF performs network requested PDU Session Release for any PDU
     * session associated with non-emergency service as described in clause 4.3.4.
     */

    /*
     * - AMF_NETWORK_INITIATED_EXPLICIT_DE_REGISTERED
     * 1. UDM_UECM_DeregistrationNotification
     * 2. Deregistration request
     * 3. UDM_SDM_Unsubscribe
     * 4. UDM_UECM_Deregisration
     * 5. PDU session release request
     * 6. PDUSessionResourceReleaseCommand +
     *    PDU session release command
     * 7. PDUSessionResourceReleaseResponse
     * 8. AM_Policy_Association_Termination
     * 9.  Deregistration accept
     * 10. Signalling Connecion Release
     */
    if (CM_CONNECTED(amf_ue)) {
        r = nas_5gs_send_de_registration_request(
                amf_ue,
                DeregistrationData->dereg_reason,
                OGS_5GMM_CAUSE_5GS_SERVICES_NOT_ALLOWED);
        ogs_expect(r == OGS_OK);
        ogs_assert(r != OGS_ERROR);

        state = AMF_NETWORK_INITIATED_EXPLICIT_DE_REGISTERED;

    } else if (CM_IDLE(amf_ue)) {
        ogs_error("Not implemented : Use Implicit De-registration");

        state = AMF_NETWORK_INITIATED_IMPLICIT_DE_REGISTERED;

    } else {
        ogs_fatal("Invalid State");
        ogs_assert_if_reached();
    }

    if (UDM_SDM_SUBSCRIBED(amf_ue)) {
        r = amf_ue_sbi_discover_and_send(
                OpenAPI_service_name_nudm_sdm, NULL,
                amf_nudm_sdm_build_subscription_delete,
                amf_ue, state, NULL);
        ogs_expect(r == OGS_OK);
        ogs_assert(r != OGS_ERROR);
    } else if (PCF_AM_POLICY_ASSOCIATED(amf_ue)) {
        r = amf_ue_sbi_discover_and_send(
                OpenAPI_service_name_npcf_am_policy_control,
                NULL,
                amf_npcf_am_policy_control_build_delete,
                amf_ue, state, NULL);
        ogs_expect(r == OGS_OK);
        ogs_assert(r != OGS_ERROR);
    }

cleanup:
    memset(&sendmsg, 0, sizeof(sendmsg));

    response = ogs_sbi_build_response(&sendmsg, status);
    ogs_assert(response);
    ogs_assert(true == ogs_sbi_server_send_response(stream, response));

    return OGS_OK;
}

static int update_rat_res_add_one(cJSON *restriction,
                                  OpenAPI_list_t *restrictions, long index)
{
    void *restr;

    if (!cJSON_IsString(restriction)) {
        ogs_error("Invalid type of ratRestriction element");
        return OGS_ERROR;
    }

    restr = (void *) OpenAPI_rat_type_FromString(cJSON_GetStringValue(restriction));
    if (!restr) {
        ogs_error("No restr");
        return OGS_ERROR;
    }

    if (index == restrictions->count) {
        OpenAPI_list_add(restrictions, restr);
    } else if (restrictions->count < index && index <= 0) {
        OpenAPI_list_insert_prev(
            restrictions, OpenAPI_list_find(restrictions, index), restr);
    } else {
        ogs_error("Can't add RAT restriction to invalid index");
        return OGS_ERROR;
    }
    return OGS_OK;
}

static int update_rat_res_array(cJSON *json_restrictions,
                                OpenAPI_list_t *restrictions)
{
    cJSON *restriction;

    if (!cJSON_IsArray(json_restrictions)) {
        ogs_error("Invalid type of ratRestrictions");
        return OGS_ERROR;
    }

    OpenAPI_list_clear(restrictions);

    cJSON_ArrayForEach(restriction, json_restrictions) {
        if (update_rat_res_add_one(restriction, restrictions,
                                   restrictions->count) != OGS_OK) {
            return OGS_ERROR;
        }
    }
    return OGS_OK;
}

static int update_rat_res(OpenAPI_change_item_t *item_change,
                          OpenAPI_list_t *restrictions)
{

    if (!item_change->path) {
        return OGS_ERROR;
    }

    switch (item_change->op) {
    case OpenAPI_change_type_REPLACE:
    case OpenAPI_change_type_ADD:
    {
        cJSON *json;

        if ((!item_change->new_value) || (!item_change->new_value->json)) {
            ogs_error("No 'new_value' field present");
            return OGS_ERROR;
        }
        json = item_change->new_value->json;

        if (!strcmp(item_change->path, "")) {
            cJSON *json_restrictions;

            if (!cJSON_IsObject(json)) {
                ogs_error("Invalid type of am-data");
            }
            json_restrictions = cJSON_GetObjectItemCaseSensitive(
                                    json, "ratRestrictions");
            if (json_restrictions) {
                return update_rat_res_array(json_restrictions, restrictions);
            } else {
                return OGS_OK;
            }
        } else if (!strcmp(item_change->path, "/ratRestrictions")) {
            return update_rat_res_array(json, restrictions);
        } else if (strstr(item_change->path, "/ratRestrictions/") ==
                   item_change->path) {
            char *index = item_change->path + strlen("/ratRestrictions/");
            long i = strcmp(index, "-") ? atol(index) : restrictions->count;

            return update_rat_res_add_one(json, restrictions, i);
        }
        return OGS_OK;
    }

    case OpenAPI_change_type__REMOVE:
        if (!strcmp(item_change->path, "")) {
            OpenAPI_list_clear(restrictions);
            return OGS_OK;
        } else if (!strcmp(item_change->path, "/ratRestrictions")) {
            OpenAPI_list_clear(restrictions);
            return OGS_OK;
        } else if (strstr(item_change->path, "/ratRestrictions/") ==
                   item_change->path) {
            char *index = item_change->path + strlen("/ratRestrictions/");
            long i = atol(index);

            if (restrictions->count < i && i <= 0) {
            OpenAPI_list_remove(
                restrictions, OpenAPI_list_find(restrictions, i));
            } else {
                ogs_error("Can't add RAT restriction to invalid index");
                return OGS_ERROR;
            }
        }
        return OGS_OK;

    default:
        return OGS_OK;
    }

}

static int update_ambr_check_one(cJSON *obj, uint64_t *limit,
                                 bool *ambr_changed)
{
    if (!cJSON_IsString(obj)) {
        ogs_error("Invalid type of subscribedUeAmbr");
        return OGS_ERROR;
    }
    *limit = ogs_sbi_bitrate_from_string(obj->valuestring);
    *ambr_changed = true;
    return OGS_OK;
}

static int update_ambr_check_obj(cJSON *obj, ogs_bitrate_t *ambr,
                                 bool *ambr_changed)
{
    if (!cJSON_IsObject(obj)) {
        if (obj == NULL || cJSON_IsNull(obj)) {
            /* Limit of 0 means unlimited. */
            ambr->uplink = 0;
            ambr->downlink = 0;
            *ambr_changed = true;
            return OGS_OK;
        } else {
            ogs_error("Invalid type of subscribedUeAmbr");
            return OGS_ERROR;
        }
    }

    if (update_ambr_check_one(
            cJSON_GetObjectItemCaseSensitive(obj, "uplink"),
            &ambr->uplink, ambr_changed)) {
        return OGS_ERROR;
    }
    if (update_ambr_check_one(
            cJSON_GetObjectItemCaseSensitive(obj, "downlink"),
            &ambr->downlink, ambr_changed)) {
        return OGS_ERROR;
    }
    return OGS_OK;
}

static int update_ambr(OpenAPI_change_item_t *item_change,
                       ogs_bitrate_t *ambr, bool *ambr_changed)
{
    cJSON *json = NULL;

    if (!item_change->path) {
        ogs_error("No 'path' field present");
        return OGS_ERROR;
    }

    switch (item_change->op) {
    case OpenAPI_change_type_REPLACE:
    case OpenAPI_change_type_ADD:
        if (!item_change->new_value || !item_change->new_value->json) {
            ogs_error("No 'new_value' field present");
            return OGS_ERROR;
        }

        json = item_change->new_value->json;

        if (!strcmp(item_change->path, "")) {
            if (!cJSON_IsObject(json)) {
                ogs_error("Invalid type of am-data");
                return OGS_ERROR;
            }
            return update_ambr_check_obj(
                    cJSON_GetObjectItemCaseSensitive(
                        json, "subscribedUeAmbr"),
                    ambr, ambr_changed);
        } else if (!strcmp(item_change->path, "/subscribedUeAmbr")) {
            return update_ambr_check_obj(json, ambr, ambr_changed);
        } else if (!strcmp(item_change->path, "/subscribedUeAmbr/uplink")) {
            return update_ambr_check_one(json, &ambr->uplink, ambr_changed);
        } else if (!strcmp(item_change->path, "/subscribedUeAmbr/downlink")) {
            return update_ambr_check_one(json, &ambr->downlink, ambr_changed);
        }
        return OGS_OK;

    case OpenAPI_change_type__REMOVE:
        if (!strcmp(item_change->path, "/subscribedUeAmbr")) {
            return update_ambr_check_obj(NULL, ambr, ambr_changed);
        }
        return OGS_OK;

    default:
        return OGS_OK;
    }
}

int amf_namf_callback_handle_sdm_data_change_notify(
        ogs_sbi_stream_t *stream, ogs_sbi_message_t *recvmsg)
{
    int r, state, status = OGS_SBI_HTTP_STATUS_NO_CONTENT;

    amf_ue_t *amf_ue = NULL;

    ogs_sbi_message_t sendmsg;
    ogs_sbi_response_t *response = NULL;

    OpenAPI_modification_notification_t *ModificationNotification;
    OpenAPI_lnode_t *node;

    char *ueid = NULL;
    char *res_name = NULL;

    bool ambr_changed = false;

    ogs_assert(stream);
    ogs_assert(recvmsg);

    ModificationNotification = recvmsg->ModificationNotification;
    if (!ModificationNotification) {
        status = OGS_SBI_HTTP_STATUS_BAD_REQUEST;
        ogs_error("No ModificationNotification");
        goto cleanup;
    }

    OpenAPI_list_for_each(ModificationNotification->notify_items, node) {
        OpenAPI_notify_item_t *item = node->data;

        char *saveptr = NULL;

        ueid = ogs_sbi_parse_uri(item->resource_id, "/", &saveptr);
        if (!ueid) {
            status = OGS_SBI_HTTP_STATUS_BAD_REQUEST;
            ogs_error("[%s] No UeId", item->resource_id);
            goto cleanup;
        }

        amf_ue = amf_ue_find_by_supi(ueid);
        if (!amf_ue) {
            status = OGS_SBI_HTTP_STATUS_NOT_FOUND;
            ogs_error("Cannot find SUPI [%s]", ueid);
            goto cleanup;
        }

        res_name = ogs_sbi_parse_uri(NULL, "/", &saveptr);
        if (!res_name) {
            status = OGS_SBI_HTTP_STATUS_BAD_REQUEST;
            ogs_error("[%s] No Resource Name", item->resource_id);
            goto cleanup;
        }

        SWITCH(res_name)
        CASE(OGS_SBI_RESOURCE_NAME_AM_DATA)
            OpenAPI_lnode_t *node_ci;

            OpenAPI_list_for_each(item->changes, node_ci) {
                OpenAPI_change_item_t *change_item = node_ci->data;
                if (update_rat_res(change_item, amf_ue->rat_restrictions) ||
                        update_ambr(change_item, &amf_ue->ue_ambr,
                            &ambr_changed)) {
                    status = OGS_SBI_HTTP_STATUS_BAD_REQUEST;
                    goto cleanup;
                }
            }
            break;
        DEFAULT
            status = OGS_SBI_HTTP_STATUS_BAD_REQUEST;
            ogs_error("Unknown Resource Name: [%s]", res_name);
            goto cleanup;
        END

        ogs_free(ueid);
        ogs_free(res_name);

        ueid = NULL;
        res_name = NULL;
    }

    if (amf_ue) {
        ran_ue_t *ran_ue = ran_ue_find_by_id(amf_ue->ran_ue_id);
        if (!ran_ue) {
            ogs_error("NG context has already been removed");
            /* ran_ue is required for amf_ue_is_rat_restricted() */

            ogs_error("Not implemented : Use Implicit De-registration");
            state = AMF_NETWORK_INITIATED_IMPLICIT_DE_REGISTERED;

        } else if (amf_ue_is_rat_restricted(amf_ue)) {
            /*
             * - AMF_NETWORK_INITIATED_EXPLICIT_DE_REGISTERED
             * 1. UDM_UECM_DeregistrationNotification
             * 2. Deregistration request
             * 3. UDM_SDM_Unsubscribe
             * 4. UDM_UECM_Deregistration
             * 5. PDU session release request
             * 6. PDUSessionResourceReleaseCommand +
             *    PDU session release command
             * 7. PDUSessionResourceReleaseResponse
             * 8. AM_Policy_Association_Termination
             * 9.  Deregistration accept
             * 10. Signalling Connection Release
             */
            r = nas_5gs_send_de_registration_request(
                    amf_ue,
                    OpenAPI_deregistration_reason_REREGISTRATION_REQUIRED, 0);
            ogs_expect(r == OGS_OK);
            ogs_assert(r != OGS_ERROR);

            state = AMF_NETWORK_INITIATED_EXPLICIT_DE_REGISTERED;

            if (UDM_SDM_SUBSCRIBED(amf_ue)) {
                r = amf_ue_sbi_discover_and_send(
                        OpenAPI_service_name_nudm_sdm, NULL,
                        amf_nudm_sdm_build_subscription_delete,
                        amf_ue, state, NULL);
                ogs_expect(r == OGS_OK);
                ogs_assert(r != OGS_ERROR);
            } else if (PCF_AM_POLICY_ASSOCIATED(amf_ue)) {
                r = amf_ue_sbi_discover_and_send(
                        OpenAPI_service_name_npcf_am_policy_control,
                        NULL,
                        amf_npcf_am_policy_control_build_delete,
                        amf_ue, state, NULL);
                ogs_expect(r == OGS_OK);
                ogs_assert(r != OGS_ERROR);
            }

        } else if (ambr_changed) {
            ogs_pkbuf_t *ngapbuf;

            ngapbuf = ngap_build_ue_context_modification_request(amf_ue);
            ogs_assert(ngapbuf);

            r = ngap_send_to_ran_ue(ran_ue, ngapbuf);
            ogs_expect(r == OGS_OK);
            ogs_assert(r != OGS_ERROR);
        }
    }

cleanup:
    if (ueid)
        ogs_free(ueid);
    if (res_name)
        ogs_free(res_name);

    memset(&sendmsg, 0, sizeof(sendmsg));

    response = ogs_sbi_build_response(&sendmsg, status);
    ogs_assert(response);
    ogs_assert(true == ogs_sbi_server_send_response(stream, response));

    return OGS_OK;
}

int amf_namf_comm_handle_ue_context_transfer_request(
        ogs_sbi_stream_t *stream, ogs_sbi_message_t *recvmsg)
{
    ogs_sbi_response_t *response = NULL;
    ogs_sbi_message_t sendmsg;
    amf_ue_t *amf_ue = NULL;

    OpenAPI_ambr_t *UeAmbr = NULL;
    OpenAPI_list_t *MmContextList = NULL;
    OpenAPI_mm_context_t *MmContext = NULL;
    OpenAPI_list_t *SessionContextList = NULL;
    OpenAPI_pdu_session_context_t *PduSessionContext = NULL;
    OpenAPI_lnode_t *node = NULL;
    OpenAPI_ue_context_t UeContext;
    OpenAPI_seaf_data_t SeafData;
    OpenAPI_ng_ksi_t Ng_ksi;
    OpenAPI_key_amf_t Key_amf;
    OpenAPI_sc_type_e Tsc_type;

    OpenAPI_ue_context_transfer_req_data_t *UeContextTransferReqData = NULL;
    OpenAPI_ue_context_transfer_rsp_data_t UeContextTransferRspData;

    ogs_sbi_nf_instance_t *pcf_nf_instance = NULL;

    char *encoded_gmm_capability = NULL;
    int status = OGS_SBI_HTTP_STATUS_OK;
    char hxkamf_string[OGS_KEYSTRLEN(OGS_SHA256_DIGEST_SIZE)];
    char *strerror = NULL;

    ogs_assert(stream);
    ogs_assert(recvmsg);

    memset(&UeContextTransferRspData, 0, sizeof(UeContextTransferRspData));
    memset(&UeContext, 0, sizeof(UeContext));
    UeContextTransferRspData.ue_context = &UeContext;

    memset(&sendmsg, 0, sizeof(sendmsg));
    sendmsg.UeContextTransferRspData = &UeContextTransferRspData;

    if (!recvmsg->h.resource.component[1]) {
        status = OGS_SBI_HTTP_STATUS_BAD_REQUEST;
        strerror = ogs_msprintf("No UE context ID");
        goto cleanup;
    }

    amf_ue = amf_ue_find_by_ue_context_id(recvmsg->h.resource.component[1]);
    if (!amf_ue) {
        status = OGS_SBI_HTTP_STATUS_NOT_FOUND;
        strerror = ogs_msprintf("Cannot find Context ID [%s]",
                recvmsg->h.resource.component[1]);
        goto cleanup;
    }

    UeContextTransferReqData = recvmsg->UeContextTransferReqData;
    if (!UeContextTransferReqData) {
        status = OGS_SBI_HTTP_STATUS_BAD_REQUEST;
        strerror = ogs_msprintf("[%s] No UeContextTransferReqData",
                amf_ue->supi ? amf_ue->supi : recvmsg->h.resource.component[1]);
        goto cleanup;
    }

    if (amf_ue->amf_ue_context_transfer_state != UE_CONTEXT_INITIAL_STATE) {
        ogs_warn("Incorrect UE context transfer state");
    }

    if (amf_ue->supi) {
        UeContext.supi = amf_ue->supi;
        if (amf_ue->auth_result !=
                OpenAPI_auth_result_AUTHENTICATION_SUCCESS) {
            UeContext.is_supi_unauth_ind = true;
            UeContext.supi_unauth_ind = amf_ue->auth_result;
        }
    }

    /* TODO UeContext.gpsi_list */

    if (amf_ue->pei) {
        UeContext.pei = amf_ue->pei;
    }

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

    if ((amf_ue->nas.ue.ksi != 0) && (amf_ue->nas.ue.tsc != 0)) {
        memset(&SeafData, 0, sizeof(SeafData));
        Tsc_type = (amf_ue->nas.ue.tsc == 0) ?
            OpenAPI_sc_type_NATIVE : OpenAPI_sc_type_MAPPED;

        memset(&Ng_ksi, 0, sizeof(Ng_ksi));
        SeafData.ng_ksi = &Ng_ksi;
        Ng_ksi.tsc = Tsc_type;
        Ng_ksi.ksi = (int)amf_ue->nas.ue.ksi;

        memset(&Key_amf, 0, sizeof(Key_amf));
        SeafData.key_amf = &Key_amf;
        OpenAPI_key_amf_type_e temp_key_type =
                (OpenAPI_key_amf_type_e)OpenAPI_key_amf_type_KAMF;
        Key_amf.key_type = temp_key_type;
        ogs_hex_to_ascii(amf_ue->kamf, sizeof(amf_ue->kamf),
                hxkamf_string, sizeof(hxkamf_string));
        Key_amf.key_val = hxkamf_string;
        UeContext.seaf_data = &SeafData;
    }

    encoded_gmm_capability =
        amf_namf_comm_base64_encode_5gmm_capability(amf_ue);
    UeContext._5g_mm_capability = encoded_gmm_capability;

    pcf_nf_instance = OGS_SBI_GET_NF_INSTANCE(
            amf_ue->sbi.service_name_array[
            OpenAPI_service_name_npcf_am_policy_control]);
    if (pcf_nf_instance) {
        UeContext.pcf_id = pcf_nf_instance->id;
    } else {
        ogs_warn("No PCF NF Instnace");
    }

    /* TODO UeContext.pcfAmPolicyUri */
    /* TODO UeContext.pcfUePolicyUri */

    MmContextList = amf_namf_comm_encode_ue_mm_context_list(amf_ue);
    UeContext.mm_context_list = MmContextList;

    if (UeContextTransferReqData->reason ==
            OpenAPI_transfer_reason_MOBI_REG) {
        SessionContextList =
	            amf_namf_comm_encode_ue_session_context_list(amf_ue);
        if (SessionContextList->count == 0) {
            OpenAPI_list_free(SessionContextList);
            SessionContextList = NULL;
        }
        UeContext.session_context_list = SessionContextList;
    }

    /* TODO ueRadioCapability */

    response = ogs_sbi_build_response(&sendmsg, status);
    ogs_assert(response);
    ogs_assert(true == ogs_sbi_server_send_response(stream, response));

    amf_ue->amf_ue_context_transfer_state = UE_CONTEXT_TRANSFER_OLD_AMF_STATE;

    if (encoded_gmm_capability)
        ogs_free(encoded_gmm_capability);

    if (UeAmbr)
        OpenAPI_ambr_free(UeAmbr);

    if (SessionContextList) {
        OpenAPI_list_for_each(SessionContextList, node) {
            PduSessionContext = node->data;
            OpenAPI_pdu_session_context_free(PduSessionContext);
        }
        OpenAPI_list_free(SessionContextList);
    }

    if (MmContextList) {
        OpenAPI_list_for_each(MmContextList, node) {
            MmContext = node->data;
            OpenAPI_mm_context_free(MmContext);
        }
        OpenAPI_list_free(MmContextList);
    }

    /*
     * Ue context is transfered, but we must keep the UE context until the
     * registartion status update is received.
     *
     * TS 23.502
     * 4.2.2.2.2 General Registration
     *
     * 10. [Conditional] new AMF to old AMF: Namf_Communication_RegistrationStatusUpdate
     * (PDU Session ID(s) to be released due to slice not supported).
	 * If the authentication/security procedure fails, then the Registration shall be
     * rejected and the new AMF invokes the Namf_Communication_RegistrationStatusUpdate
     * service operation with a reject indication towards the old AMF. The old AMF continues
     * as if the UE context transfer service operation was never received.
     */

    return OGS_OK;

cleanup:
    ogs_assert(strerror);
    ogs_error("%s", strerror);

    ogs_assert(true ==
        ogs_sbi_server_send_error(stream, status, NULL, strerror, NULL, NULL));
    ogs_free(strerror);

    return OGS_ERROR;
}

static ogs_nas_5gmm_capability_t
        amf_namf_comm_base64_decode_5gmm_capability(char *encoded);
static ogs_nas_ue_security_capability_t
        amf_namf_comm_base64_decode_ue_security_capability(char *encoded);
static void amf_namf_comm_decode_ue_mm_context_list(
            amf_ue_t *amf_ue, OpenAPI_list_t *MmContextList);
static void amf_namf_comm_decode_ue_session_context_list(
            amf_ue_t *amf_ue, OpenAPI_list_t *SessionContextList);

static int amf_namf_comm_decode_ue_context(
        amf_ue_t *amf_ue, OpenAPI_ue_context_t *UeContext,
        bool save_to_release_session_list)
{
    ogs_assert(amf_ue);
    ogs_assert(UeContext);

    if (!UeContext->supi) {
        ogs_error("No SUPI");
        return OGS_ERROR;
    }

    amf_ue_set_supi(amf_ue, UeContext->supi);
    if (!UeContext->supi_unauth_ind)
        amf_ue->auth_result = OpenAPI_auth_result_AUTHENTICATION_SUCCESS;

    if (UeContext->pei) {
        if (amf_ue->pei)
            ogs_free(amf_ue->pei);
        amf_ue->pei = ogs_strdup(UeContext->pei);
    }

    if (UeContext->sub_ue_ambr) {
        if (UeContext->sub_ue_ambr->downlink)
            amf_ue->ue_ambr.downlink =
                ogs_sbi_bitrate_from_string(UeContext->sub_ue_ambr->downlink);
        if (UeContext->sub_ue_ambr->uplink)
            amf_ue->ue_ambr.uplink =
                ogs_sbi_bitrate_from_string(UeContext->sub_ue_ambr->uplink);
    }

    if (UeContext->seaf_data) {
        if (!UeContext->seaf_data->ng_ksi ||
            !UeContext->seaf_data->key_amf ||
            !UeContext->seaf_data->key_amf->key_val) {
            ogs_error("[%s] Incomplete SEAF data", UeContext->supi);
            return OGS_ERROR;
        }

        if (UeContext->seaf_data->ng_ksi->tsc != OpenAPI_sc_type_NULL) {
            amf_ue->nas.ue.tsc =
                (UeContext->seaf_data->ng_ksi->tsc ==
                 OpenAPI_sc_type_NATIVE) ? 0 : 1;
            amf_ue->nas.ue.ksi =
                (uint8_t)UeContext->seaf_data->ng_ksi->ksi;

            if (ogs_ascii_to_hex_checked(
                    UeContext->seaf_data->key_amf->key_val,
                    strlen(UeContext->seaf_data->key_amf->key_val),
                    amf_ue->kamf, sizeof(amf_ue->kamf)) != OGS_OK) {
                ogs_error("[%s] Invalid transferred AMF key", UeContext->supi);
                return OGS_ERROR;
            }

            if (UeContext->seaf_data->nh) {
                if (!UeContext->seaf_data->is_ncc ||
                    UeContext->seaf_data->ncc < 0 ||
                    UeContext->seaf_data->ncc > 7 ||
                    ogs_ascii_to_hex_checked(
                        UeContext->seaf_data->nh,
                        strlen(UeContext->seaf_data->nh),
                        amf_ue->nh, sizeof(amf_ue->nh)) != OGS_OK) {
                    ogs_error("[%s] Invalid transferred NH/NCC",
                            UeContext->supi);
                    return OGS_ERROR;
                }
                amf_ue->nhcc = (uint8_t)UeContext->seaf_data->ncc;
            }

            /*
             * A transferred KAMF is a usable 5GS NAS security context.
             * The MM-context decoder restores the selected algorithms and
             * NAS counters; mark the context available only after both
             * pieces have been decoded below.
             */
        }
    }

    if (UeContext->_5g_mm_capability) {
        ogs_nas_5gmm_capability_t gmm_capability;

        gmm_capability = amf_namf_comm_base64_decode_5gmm_capability(
                    UeContext->_5g_mm_capability);
        amf_ue->gmm_capability.lte_positioning_protocol_capability =
                (bool)gmm_capability.lte_positioning_protocol_capability;
        amf_ue->gmm_capability.ho_attach = (bool)gmm_capability.ho_attach;
        amf_ue->gmm_capability.s1_mode = (bool)gmm_capability.s1_mode;
    }

    if (UeContext->pcf_id) {
        /* TODO */
    }

    /* TODO UeContext->pcfAmPolicyUri */
    /* TODO UeContext->pcfUePolicyUri */

    if (UeContext->mm_context_list)
        amf_namf_comm_decode_ue_mm_context_list(
                amf_ue, UeContext->mm_context_list);

    if (UeContext->session_context_list) {
        amf_namf_comm_decode_ue_session_context_list(
                amf_ue, UeContext->session_context_list);

        if (save_to_release_session_list && UeContext->mm_context_list)
            amf_ue_save_to_release_session_list(amf_ue);
    }

    if (UeContext->seaf_data &&
        UeContext->seaf_data->key_amf &&
        UeContext->seaf_data->key_amf->key_val &&
        UeContext->mm_context_list) {
        amf_ue->nas.amf.tsc = amf_ue->nas.ue.tsc;
        amf_ue->nas.amf.ksi = amf_ue->nas.ue.ksi;
        amf_ue->security_context_available = 1;
        amf_ue->mac_failed = 0;
    }

    /* TODO ueRadioCapability */

    return OGS_OK;
}

int amf_namf_comm_handle_ue_context_transfer_response(
        ogs_sbi_message_t *recvmsg, amf_ue_t *amf_ue)
{
    if (!recvmsg->UeContextTransferRspData) {
        ogs_error("No UeContextTransferRspData");
        return OGS_ERROR;
    }

    if (!recvmsg->UeContextTransferRspData->ue_context) {
        ogs_error("No UE context");
        return OGS_ERROR;
    }

    return amf_namf_comm_decode_ue_context(
            amf_ue, recvmsg->UeContextTransferRspData->ue_context, true);
}

static ogs_nas_5gmm_capability_t
        amf_namf_comm_base64_decode_5gmm_capability(char *encoded)
{
    ogs_nas_5gmm_capability_t gmm_capability;
    char *gmm_capability_octets_string = NULL;
    uint8_t gmm_capability_iei = 0;
    int len;

    memset(&gmm_capability, 0, sizeof(gmm_capability));
    gmm_capability_octets_string =
            (char*) ogs_calloc(sizeof(gmm_capability) + 1, sizeof(char));
    ogs_assert(gmm_capability_octets_string);

    len = ogs_base64_decode_to_buffer(
            (uint8_t *)gmm_capability_octets_string,
            sizeof(gmm_capability) + 1, encoded);

    if (len <= 0)
        ogs_error("Gmm capability not decoded");

    ogs_assert(sizeof(gmm_capability_octets_string) <=
            sizeof(gmm_capability) + 1);

    gmm_capability_iei = // not copied anywhere for now
            gmm_capability_octets_string[0];
    if (gmm_capability_iei !=
            OGS_NAS_5GS_REGISTRATION_REQUEST_5GMM_CAPABILITY_TYPE) {
        ogs_error("Type of 5GMM capability IEI is incorrect");
    }
    memcpy(&gmm_capability,
            gmm_capability_octets_string + 1,
            sizeof(gmm_capability));
    if (gmm_capability_octets_string) {
        ogs_free(gmm_capability_octets_string);
    }

    return gmm_capability;
}

static ogs_nas_ue_security_capability_t
        amf_namf_comm_base64_decode_ue_security_capability(char *encoded)
{
    ogs_nas_ue_security_capability_t ue_security_capability;
    char *ue_security_capability_octets_string = NULL;
    uint8_t ue_security_capability_iei = 0;

    memset(&ue_security_capability, 0, sizeof(ue_security_capability));
    ue_security_capability_octets_string =
            (char*) ogs_calloc(sizeof(ue_security_capability) + 1, sizeof(char));
    ogs_assert(ue_security_capability_octets_string);

    ogs_base64_decode_to_buffer(
            (uint8_t *)ue_security_capability_octets_string,
            sizeof(ue_security_capability) + 1, encoded);

    ogs_assert(sizeof(ue_security_capability_octets_string) <=
            sizeof(ogs_nas_ue_security_capability_t) + 1);

    ue_security_capability_iei = // not copied anywhere for now
            ue_security_capability_octets_string[0];
    if (ue_security_capability_iei !=
            OGS_NAS_5GS_REGISTRATION_REQUEST_UE_SECURITY_CAPABILITY_TYPE) {
        ogs_error("UE security capability IEI is incorrect");
    }

    memcpy(&ue_security_capability, ue_security_capability_octets_string + 1,
            sizeof(ue_security_capability));

    if (ue_security_capability_octets_string) {
        ogs_free(ue_security_capability_octets_string);
    }

    return ue_security_capability;
}

static void amf_namf_comm_decode_ue_mm_context_list(
            amf_ue_t *amf_ue, OpenAPI_list_t *MmContextList) {

    OpenAPI_lnode_t *node = NULL;

    OpenAPI_list_for_each(MmContextList, node) {

        OpenAPI_mm_context_t *MmContext = NULL;
        OpenAPI_list_t *AllowedNssaiList = NULL;
        OpenAPI_lnode_t *node1 = NULL;
        OpenAPI_list_t *NssaiMappingList = NULL;
        int num_of_s_nssai = 0;
        int num_of_nssai_mapping = 0;

        MmContext = node->data;
        if (!MmContext) {
            ogs_error("No MmContext");
            continue;
        }

        if (MmContext->access_type != OpenAPI_access_type_NULL)
            amf_ue->nas.access_type = (int)MmContext->access_type;

        if (MmContext->nas_security_mode) {
            amf_ue->selected_enc_algorithm =
                (uint8_t)MmContext->nas_security_mode->ciphering_algorithm;
            amf_ue->selected_int_algorithm =
                (uint8_t)MmContext->nas_security_mode->integrity_algorithm;
        }

        if (MmContext->is_nas_downlink_count)
            amf_ue->dl_count = (uint32_t)MmContext->nas_downlink_count;
        if (MmContext->is_nas_uplink_count)
            amf_ue->ul_count.i32 = (uint32_t)MmContext->nas_uplink_count;

        AllowedNssaiList = MmContext->allowed_nssai;
        NssaiMappingList = MmContext->nssai_mapping_list;

        OpenAPI_list_for_each(AllowedNssaiList, node1) {
            OpenAPI_snssai_t *AllowedNssai = node1->data;

            if (!AllowedNssai) {
                ogs_error("No allowedNssai");
                continue;
            }

            if (num_of_s_nssai >= OGS_MAX_NUM_OF_SLICE) {
                ogs_error("Too many allowedNssai [%d:%d]",
                        num_of_s_nssai + 1, OGS_MAX_NUM_OF_SLICE);
                break;
            }

            amf_ue->allowed_nssai.s_nssai[num_of_s_nssai].sst =
                    (uint8_t)AllowedNssai->sst;
            amf_ue->allowed_nssai.s_nssai[num_of_s_nssai].sd =
                    ogs_s_nssai_sd_from_string(AllowedNssai->sd);

            num_of_s_nssai++;
            amf_ue->allowed_nssai.num_of_s_nssai = num_of_s_nssai;
        }

        OpenAPI_list_for_each(NssaiMappingList, node1) {
            OpenAPI_nssai_mapping_t *NssaiMapping = node1->data;
            OpenAPI_snssai_t *HSnssai = NULL;

            if (!NssaiMapping) {
                ogs_error("No nssaiMapping");
                continue;
            }

            HSnssai = NssaiMapping->h_snssai;
            if (!HSnssai) {
                ogs_error("No hSnssai in nssaiMappingList");
                continue;
            }

            if (num_of_nssai_mapping >= OGS_MAX_NUM_OF_SLICE) {
                ogs_error("Too many nssaiMappingList [%d:%d]",
                        num_of_nssai_mapping + 1, OGS_MAX_NUM_OF_SLICE);
                break;
            }

            amf_ue->allowed_nssai.s_nssai[num_of_nssai_mapping].
                    mapped_hplmn_sst = HSnssai->sst;
            amf_ue->allowed_nssai.s_nssai[num_of_nssai_mapping].
                    mapped_hplmn_sd = ogs_s_nssai_sd_from_string(HSnssai->sd);

            num_of_nssai_mapping++;
        }

        if (MmContext->ue_security_capability) {
            amf_ue->ue_security_capability =
                    amf_namf_comm_base64_decode_ue_security_capability(
                    MmContext->ue_security_capability);
        }
    }

}

static void amf_namf_comm_decode_ue_session_context_list(
            amf_ue_t *amf_ue, OpenAPI_list_t *SessionContextList)
{
    OpenAPI_lnode_t *node = NULL;

    OpenAPI_list_for_each(SessionContextList, node) {
        OpenAPI_pdu_session_context_t *PduSessionContext;
        PduSessionContext = node->data;
        amf_sess_t *sess = NULL;

        int rv;
        ogs_sbi_message_t message;
        ogs_sbi_header_t header;

        bool rc;
        ogs_sbi_client_t *client = NULL;
        OpenAPI_uri_scheme_e scheme = OpenAPI_uri_scheme_NULL;
        char *fqdn = NULL;
        uint16_t fqdn_port = 0;
        ogs_sockaddr_t *addr = NULL, *addr6 = NULL;

        if (!PduSessionContext->sm_context_ref) {
            ogs_error("No smContextRef [PSI:%d]",
                    PduSessionContext->pdu_session_id);
            continue;
        }

        if (!PduSessionContext->s_nssai) {
            ogs_error("No sNSSI [PSI:%d]", PduSessionContext->pdu_session_id);
            continue;
        }

        if (!PduSessionContext->dnn) {
            ogs_error("No DNN [PSI:%d]", PduSessionContext->pdu_session_id);
            continue;
        }

        if (!PduSessionContext->access_type) {
            ogs_error("No accessType [PSI:%d]",
                    PduSessionContext->pdu_session_id);
            continue;
        }

        memset(&header, 0, sizeof(header));
        header.uri = PduSessionContext->sm_context_ref;

        rv = ogs_sbi_parse_header(&message, &header);
        if (rv != OGS_OK) {
            ogs_error("[%d] Cannot parse sm_context_ref [%s]",
                    PduSessionContext->pdu_session_id,
                    PduSessionContext->sm_context_ref);
            continue;
        }

        if (!message.h.resource.component[1]) {
            ogs_error("[%d] No SmContextRef [%s]",
                    PduSessionContext->pdu_session_id,
                    PduSessionContext->sm_context_ref);

            ogs_sbi_header_free(&header);
            continue;
        }

        sess = amf_sess_add(amf_ue, PduSessionContext->pdu_session_id);
        ogs_assert(sess);

        rc = ogs_sbi_getaddr_from_uri(
                &scheme, &fqdn, &fqdn_port, &addr, &addr6, header.uri);
        if (rc == false || scheme == OpenAPI_uri_scheme_NULL) {
            ogs_error("[%s:%d] Invalid URI [%s]",
                    amf_ue->supi, sess->psi, header.uri);

            ogs_sbi_header_free(&header);
            continue;
        }

        client = ogs_sbi_client_find(scheme, fqdn, fqdn_port, addr, addr6);
        if (!client) {
            ogs_debug("[%s:%d] ogs_sbi_client_add()", amf_ue->supi, sess->psi);
            client = ogs_sbi_client_add(scheme, fqdn, fqdn_port, addr, addr6);
            if (!client) {
                ogs_error("[%s:%d] ogs_sbi_client_add() failed",
                        amf_ue->supi, sess->psi);

                ogs_sbi_header_free(&header);

                ogs_free(fqdn);
                ogs_freeaddrinfo(addr);
                ogs_freeaddrinfo(addr6);

                continue;
            }
        }
        OGS_SBI_SETUP_CLIENT(&sess->sm_context, client);

        ogs_free(fqdn);
        ogs_freeaddrinfo(addr);
        ogs_freeaddrinfo(addr6);

        sess->sm_context_resource_uri =
            ogs_strdup(PduSessionContext->sm_context_ref);
        sess->sm_context_ref =
            ogs_strdup(message.h.resource.component[1]);

        memset(&sess->s_nssai, 0, sizeof(sess->s_nssai));

        sess->s_nssai.sst = PduSessionContext->s_nssai->sst;
        sess->s_nssai.sd = ogs_s_nssai_sd_from_string(
                PduSessionContext->s_nssai->sd);

        sess->dnn = ogs_strdup(PduSessionContext->dnn);
        amf_ue->nas.access_type = (int)PduSessionContext->access_type;

        ogs_sbi_header_free(&header);
    }
}

int amf_namf_comm_handle_registration_status_update_request(
        ogs_sbi_stream_t *stream, ogs_sbi_message_t *recvmsg) {

    ogs_sbi_response_t *response = NULL;
    ogs_sbi_message_t sendmsg;
    amf_ue_t *amf_ue = NULL;
    ran_ue_t *ran_ue = NULL;
    amf_sess_t *sess = NULL;

    OpenAPI_ue_reg_status_update_req_data_t *UeRegStatusUpdateReqData = NULL;
    OpenAPI_ue_reg_status_update_rsp_data_t UeRegStatusUpdateRspData;

    int status = 0;
    char *strerror = NULL;

    ogs_assert(stream);
    ogs_assert(recvmsg);

    if (!recvmsg->h.resource.component[1]) {
        status = OGS_SBI_HTTP_STATUS_BAD_REQUEST;
        strerror = ogs_msprintf("No UE context ID");
        goto cleanup;
    }
    amf_ue = amf_ue_find_by_ue_context_id(recvmsg->h.resource.component[1]);
    if (!amf_ue) {
        status = OGS_SBI_HTTP_STATUS_NOT_FOUND;
        strerror = ogs_msprintf("Cannot find Context ID [%s]",
                recvmsg->h.resource.component[1]);
        goto cleanup;
    }

    UeRegStatusUpdateReqData = recvmsg->UeRegStatusUpdateReqData;
    if (!UeRegStatusUpdateReqData) {
        status = OGS_SBI_HTTP_STATUS_BAD_REQUEST;
        strerror = ogs_msprintf("[%s] No UeRegStatusUpdateReqData",
                amf_ue->supi ? amf_ue->supi : recvmsg->h.resource.component[1]);
        goto cleanup;
    }

    if (amf_ue->amf_ue_context_transfer_state != UE_CONTEXT_TRANSFER_OLD_AMF_STATE) {
        status = OGS_SBI_HTTP_STATUS_BAD_REQUEST;
        strerror = ogs_msprintf("Incorrect UE context transfer state");
        goto cleanup;
    }

    memset(&UeRegStatusUpdateRspData, 0, sizeof(UeRegStatusUpdateRspData));
    memset(&sendmsg, 0, sizeof(sendmsg));
    sendmsg.UeRegStatusUpdateRspData = &UeRegStatusUpdateRspData;

    if (UeRegStatusUpdateReqData->transfer_status ==
            OpenAPI_ue_context_transfer_status_TRANSFERRED) {
    /*
    * TS 29.518
    * 5.2.2.2.2 Registration Status Update
    * Once the update is received, the source AMF shall:
    *  -   remove the individual ueContext resource and release any PDU session(s) in the
    *      toReleaseSessionList attribute, if the transferStatus attribute included in the
    *      POST request body is set to "TRANSFERRED" and if the source AMF transferred the
    *      complete UE Context including all MM contexts and PDU Session Contexts.
    */
        UeRegStatusUpdateRspData.reg_status_transfer_complete = 1;

        ran_ue = ran_ue_find_by_id(amf_ue->ran_ue_id);

        if (ran_ue) {
            if (UeRegStatusUpdateReqData->to_release_session_list) {
                OpenAPI_lnode_t *node = NULL;
                OpenAPI_list_for_each(UeRegStatusUpdateReqData->to_release_session_list, node) {
                    /* A double must be read */
                    uint8_t psi = *(double *)node->data;
                    sess = amf_sess_find_by_psi(amf_ue, psi);
                    if (SESSION_CONTEXT_IN_SMF(sess)) {
                        amf_nsmf_pdusession_sm_context_param_t param;

                        memset(&param, 0, sizeof(param));
                        param.ue_location = true;
                        param.ue_timezone = true;

                        amf_sbi_send_release_session(
                                ran_ue, sess,
                                AMF_RELEASE_SM_CONTEXT_NO_STATE, &param);
                    } else {
                        ogs_error("[%s] No Session Context PSI[%d]",
                                amf_ue->supi, psi);
                        UeRegStatusUpdateRspData.reg_status_transfer_complete = 0;
                    }
                }
            }
        }

        /* Clear UE context */
        CLEAR_NG_CONTEXT(amf_ue);
        AMF_UE_CLEAR_PAGING_INFO(amf_ue);
        AMF_UE_CLEAR_N2_TRANSFER(amf_ue, pdu_session_resource_setup_request);
        AMF_UE_CLEAR_5GSM_MESSAGE(amf_ue);
        CLEAR_AMF_UE_ALL_TIMERS(amf_ue);
        OGS_ASN_CLEAR_DATA(&amf_ue->ueRadioCapability);

    } else if (UeRegStatusUpdateReqData->transfer_status ==
            OpenAPI_ue_context_transfer_status_NOT_TRANSFERRED) {
    /*
    * TS 23.502
    * 4.2.2.2.2
    * If the authentication/security procedure fails, then the Registration shall be rejected and
    * the new AMF invokes the Namf_Communication_RegistrationStatusUpdate service operation with
    * a reject indication towards the old AMF. The old AMF continues as if the UE context transfer
    * service operation was never received.
    */
        UeRegStatusUpdateRspData.reg_status_transfer_complete = 0;

    } else {
        status = OGS_SBI_HTTP_STATUS_BAD_REQUEST;
        strerror = ogs_msprintf("Transfer status not supported: [%d]",
                UeRegStatusUpdateReqData->transfer_status);
        goto cleanup;
    }

    status = OGS_SBI_HTTP_STATUS_OK;
    response = ogs_sbi_build_response(&sendmsg, status);
    ogs_assert(response);
    ogs_assert(true == ogs_sbi_server_send_response(stream, response));

    amf_ue->amf_ue_context_transfer_state = UE_CONTEXT_INITIAL_STATE;

    return OGS_OK;

cleanup:
    ogs_assert(strerror);
    ogs_error("%s", strerror);

    ogs_assert(true == ogs_sbi_server_send_error(stream, status, NULL, strerror, NULL, NULL));
    ogs_free(strerror);

    if (amf_ue)
        amf_ue->amf_ue_context_transfer_state = UE_CONTEXT_INITIAL_STATE;

    return OGS_ERROR;
}

int amf_namf_comm_handle_registration_status_update_response(
        ogs_sbi_message_t *recvmsg, amf_ue_t *amf_ue) {

    /* Nothing to do */

    return OGS_OK;
}
