/*
 * Copyright (C) 2019,2020 by Sukchan Lee <acetcom@gmail.com>
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

#ifndef AMF_NAMF_BUILD_H
#define AMF_NAMF_BUILD_H

#ifdef __cplusplus
extern "C" {
#endif

#include "context.h"

typedef struct amf_namf_comm_create_ue_context_param_s {
    NGAP_TargetID_t *target_id;
    NGAP_PDUSessionResourceListHORqd_t *pdu_session_list;
    NGAP_SourceToTarget_TransparentContainer_t *source_to_target_container;
    NGAP_Cause_t *cause;
} amf_namf_comm_create_ue_context_param_t;

OpenAPI_list_t *amf_namf_comm_encode_ue_session_context_list(
        amf_ue_t *amf_ue);
OpenAPI_list_t *amf_namf_comm_encode_ue_mm_context_list(
        amf_ue_t *amf_ue);
char *amf_namf_comm_base64_encode_5gmm_capability(amf_ue_t *amf_ue);

ogs_sbi_request_t *amf_namf_comm_build_create_ue_context(
        amf_ue_t *amf_ue, void *data);
ogs_sbi_request_t *amf_namf_comm_build_release_ue_context(
        amf_ue_t *amf_ue, void *data);
ogs_sbi_request_t *amf_namf_comm_build_ran_status_transfer(
        amf_ue_t *amf_ue, void *data);
ogs_sbi_request_t *amf_namf_comm_build_ue_context_transfer(
        amf_ue_t *amf_ue, void *data);
ogs_sbi_request_t *amf_namf_comm_build_registration_status_update(
        amf_ue_t *amf_ue, void *data);
ogs_sbi_request_t *amf_namf_callback_build_n2_info_notify(
        amf_ue_t *amf_ue, void *data);

#ifdef __cplusplus
}
#endif

#endif /* AMF_NAMF_BUILD_H */
