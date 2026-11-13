/*
 * Copyright (C) 2026 by Open5GS contributors
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */
#include "mme-s10-handler.h"

/* A response's TEID is the sender's control-plane TEID, NOT the TEID in
 * the incoming GTP-C header. Zero is used if no valid sender F-TEID is
 * available (TS 29.274 §5.5.2); never fabricate a remote TEID. */
static uint32_t sender_teid(const ogs_gtp2_tlv_f_teid_t *ie)
{
    uint32_t network_teid;

    if (!ie || !ie->presence || !ie->data ||
            ie->len < OGS_GTP2_F_TEID_HDR_LEN)
        return 0;

    memcpy(&network_teid, (const uint8_t *)ie->data + 1,
            sizeof(network_teid));
    return be32toh(network_teid);
}

void mme_s10_handle_message(
        ogs_gtp_node_t *gnode, ogs_gtp2_message_t *message)
{
    ogs_gtp_xact_t *xact = NULL;
    uint8_t response_type = 0;
    uint32_t remote_teid = 0;
    int rv;

    ogs_assert(gnode);
    ogs_assert(message);

    /* No context transfer, bearer switching or handover activation until
     * end-to-end S10 procedure support is implemented. */
    switch (message->h.type) {
    case OGS_GTP2_ECHO_REQUEST_TYPE:
        break;
    case OGS_GTP2_ECHO_RESPONSE_TYPE:
        /* Echo transmission/response correlation can be enabled when
         * proactive S10 peer liveness probing is implemented. */
        ogs_debug("Unsolicited S10 Echo Response");
        return;
    case OGS_GTP2_IDENTIFICATION_REQUEST_TYPE:
        response_type = OGS_GTP2_IDENTIFICATION_RESPONSE_TYPE;
        break;
    case OGS_GTP2_CONTEXT_REQUEST_TYPE:
        response_type = OGS_GTP2_CONTEXT_RESPONSE_TYPE;
        remote_teid = sender_teid(&message->context_request.
                s3_s16_s10_n26_address_and_teid_for_control_plane);
        break;
    case OGS_GTP2_FORWARD_RELOCATION_REQUEST_TYPE:
        response_type = OGS_GTP2_FORWARD_RELOCATION_RESPONSE_TYPE;
        remote_teid = sender_teid(&message->forward_relocation_request.
                sender_s_f_teid_for_control_plane);
        break;
    case OGS_GTP2_FORWARD_RELOCATION_COMPLETE_NOTIFICATION_TYPE:
        response_type = OGS_GTP2_FORWARD_RELOCATION_COMPLETE_ACKNOWLEDGE_TYPE;
        break;
    case OGS_GTP2_FORWARD_ACCESS_CONTEXT_NOTIFICATION_TYPE:
        response_type = OGS_GTP2_FORWARD_ACCESS_CONTEXT_ACKNOWLEDGE_TYPE;
        break;
    case OGS_GTP2_RELOCATION_CANCEL_REQUEST_TYPE:
        response_type = OGS_GTP2_RELOCATION_CANCEL_RESPONSE_TYPE;
        break;
    case OGS_GTP2_CONTEXT_ACKNOWLEDGE_TYPE:
        /* A rejecting Context Response remains in the transaction engine
         * until the initiator sends Context Acknowledge or it times out.
         * Correlate the ACK and close the context transaction promptly. */
        break;
    default:
        /* Do not allocate a transaction for an unsolicited response:
         * pending S10 local-originator procedures do not exist yet. */
        ogs_warn("Ignoring unsupported S10 type [%d]", message->h.type);
        return;
    }

    rv = ogs_gtp_xact_receive(gnode, &message->h, &xact);
    if (rv == OGS_RETRY)
        return; /* Duplicate: transaction engine resends previous response */
    if (rv != OGS_OK || !xact) {
        ogs_warn("Unable to correlate S10 transaction type [%d]",
                message->h.type);
        return;
    }

    if (message->h.type == OGS_GTP2_ECHO_REQUEST_TYPE) {
        ogs_gtp2_send_echo_response(xact,
                message->echo_request.recovery.presence ?
                message->echo_request.recovery.u8 : 0, 0);
    } else if (message->h.type == OGS_GTP2_CONTEXT_ACKNOWLEDGE_TYPE) {
        ogs_gtp_xact_commit(xact);
    } else {
        ogs_warn("S10 mobility type [%d] not enabled; rejecting",
                message->h.type);
        ogs_gtp2_send_error_message(xact, remote_teid, response_type,
                OGS_GTP2_CAUSE_SERVICE_NOT_SUPPORTED);
    }
}
