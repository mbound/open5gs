/*
 * Copyright (C) 2026 by Open5GS contributors
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */
#include "mme/mme-s10-handler.h"
#include "core/abts.h"

#if !defined(_WIN32)
#include <sys/socket.h>
#include <sys/time.h>
#include <netinet/in.h>
#include <arpa/inet.h>

/* Exercises production S10 rejection/Echo handling on a UDP loopback socket.
 * This validates actual wire packet type, TEID, cause, xid and duplicate
 * transaction replay, independently of the full EPC deployment. */
static void s10_loopback_test(abts_case *tc, void *data)
{
    ogs_timer_mgr_t *saved_timers = ogs_app()->timer_mgr;
    uint64_t saved_xacts = ogs_app()->pool.xact;
    ogs_app_local_conf_t saved_conf = *ogs_local_conf();
    ogs_gtp_node_t peer = {0};
    ogs_sock_t sender = {0};
    ogs_gtp2_message_t m;
    ogs_gtp2_header_t *hdr;
    struct sockaddr_in destination = {0};
    struct timeval timeout = {0};
    socklen_t addrlen = sizeof(destination);
    int receiver = -1, sender_fd = -1;
    ssize_t len, first_len;
    uint8_t buffer[256], first[256];
    uint8_t fteid[] = { 0x80, 0x11, 0x22, 0x33, 0x44,
                        0x7f, 0x00, 0x00, 0x01 };

    receiver = socket(AF_INET, SOCK_DGRAM, 0);
    sender_fd = socket(AF_INET, SOCK_DGRAM, 0);
    ABTS_TRUE(tc, receiver >= 0 && sender_fd >= 0);
    if (receiver < 0 || sender_fd < 0)
        goto cleanup;

    destination.sin_family = AF_INET;
    destination.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    destination.sin_port = 0;
    ABTS_INT_EQUAL(tc, 0, bind(receiver,
                (struct sockaddr *)&destination, sizeof(destination)));
    ABTS_INT_EQUAL(tc, 0, getsockname(receiver,
                (struct sockaddr *)&destination, &addrlen));
    timeout.tv_sec = 1;
    ABTS_INT_EQUAL(tc, 0, setsockopt(receiver, SOL_SOCKET,
                SO_RCVTIMEO, &timeout, sizeof(timeout)));

    sender.fd = sender_fd;
    sender.family = AF_INET;
    peer.sock = &sender;
    peer.addr.sin = destination;
    ogs_list_init(&peer.local_list);
    ogs_list_init(&peer.remote_list);
    ogs_app()->pool.xact = 8;
    ogs_app()->timer_mgr = ogs_timer_mgr_create(32);
    ogs_local_conf()->time.message.gtp.t3_response_duration =
        ogs_time_from_sec(5);
    ogs_local_conf()->time.message.gtp.t3_holding_duration =
        ogs_time_from_sec(60);
    ogs_local_conf()->time.message.gtp.n3_response_rcount = 1;
    ogs_local_conf()->time.message.gtp.n3_holding_rcount = 1;
    ABTS_INT_EQUAL(tc, OGS_OK, ogs_gtp_xact_init());

    /* GTPv2 Echo Request uses a TEID-less eight-byte header. */
    memset(&m, 0, sizeof(m));
    m.h.type = OGS_GTP2_ECHO_REQUEST_TYPE;
    m.h.sqn_only = OGS_GTP2_XID_TO_SQN(0x1201);
    m.echo_request.recovery.presence = 1;
    m.echo_request.recovery.u8 = 23;
    mme_s10_handle_message(&peer, &m);
    len = recv(receiver, buffer, sizeof(buffer), 0);
    ABTS_TRUE(tc, len > 8);
    if (len > 8) {
        hdr = (ogs_gtp2_header_t *)buffer;
        ABTS_INT_EQUAL(tc, OGS_GTP2_ECHO_RESPONSE_TYPE, hdr->type);
        ABTS_INT_EQUAL(tc, 0, hdr->teid_presence);
        ABTS_INT_EQUAL(tc, m.h.sqn_only, hdr->sqn_only);
    }
    /* The transaction is deliberately held to replay duplicate Echoes. */
    ABTS_INT_EQUAL(tc, 1, ogs_list_count(&peer.remote_list));
    ogs_gtp_xact_delete_all(&peer);
    ABTS_TRUE(tc, ogs_list_empty(&peer.remote_list));

    /* Forward Relocation unsupported: response must use SENDER F-TEID,
     * rather than the incoming GTP-C header's local TEID. */
    memset(&m, 0, sizeof(m));
    m.h.type = OGS_GTP2_FORWARD_RELOCATION_REQUEST_TYPE;
    m.h.teid_presence = 1;
    m.h.teid = 0xdeadbeef; /* Must never be copied as remote TEID. */
    m.h.sqn = OGS_GTP2_XID_TO_SQN(0x1202);
    m.forward_relocation_request.sender_s_f_teid_for_control_plane.presence = 1;
    m.forward_relocation_request.sender_s_f_teid_for_control_plane.data = fteid;
    m.forward_relocation_request.sender_s_f_teid_for_control_plane.len =
        sizeof(fteid);
    mme_s10_handle_message(&peer, &m);
    len = recv(receiver, buffer, sizeof(buffer), 0);
    ABTS_TRUE(tc, len >= OGS_GTPV2C_HEADER_LEN + 6);
    if (len >= OGS_GTPV2C_HEADER_LEN + 6) {
        hdr = (ogs_gtp2_header_t *)buffer;
        ABTS_INT_EQUAL(tc, OGS_GTP2_FORWARD_RELOCATION_RESPONSE_TYPE, hdr->type);
        ABTS_INT_EQUAL(tc, 1, hdr->teid_presence);
        ABTS_INT_EQUAL(tc, 0x11223344, be32toh(hdr->teid));
        ABTS_INT_EQUAL(tc, m.h.sqn, hdr->sqn);
        ABTS_INT_EQUAL(tc, OGS_GTP2_CAUSE_TYPE,
                buffer[OGS_GTPV2C_HEADER_LEN]);
        ABTS_INT_EQUAL(tc, OGS_GTP2_CAUSE_SERVICE_NOT_SUPPORTED,
                buffer[OGS_GTPV2C_HEADER_LEN + 4]);
    }
    /* A final response is cached for T3-HOLDING duplicate replay. */
    ABTS_INT_EQUAL(tc, 1, ogs_list_count(&peer.remote_list));
    ogs_gtp_xact_delete_all(&peer);

    /* Context Request -> rejected Context Response -> duplicate retry ->
     * Context Acknowledge. The response must be replayed, not duplicated
     * in memory, and ACK must clean up the remote-originated transaction. */
    memset(&m, 0, sizeof(m));
    m.h.type = OGS_GTP2_CONTEXT_REQUEST_TYPE;
    m.h.teid_presence = 1;
    m.h.sqn = OGS_GTP2_XID_TO_SQN(0x1203);
    m.context_request.s3_s16_s10_n26_address_and_teid_for_control_plane.presence = 1;
    m.context_request.s3_s16_s10_n26_address_and_teid_for_control_plane.data =
        fteid;
    m.context_request.s3_s16_s10_n26_address_and_teid_for_control_plane.len =
        sizeof(fteid);
    mme_s10_handle_message(&peer, &m);
    len = recv(receiver, buffer, sizeof(buffer), 0);
    ABTS_TRUE(tc, len >= OGS_GTPV2C_HEADER_LEN + 6);
    if (len >= OGS_GTPV2C_HEADER_LEN + 6) {
        hdr = (ogs_gtp2_header_t *)buffer;
        ABTS_INT_EQUAL(tc, OGS_GTP2_CONTEXT_RESPONSE_TYPE, hdr->type);
        ABTS_INT_EQUAL(tc, 0x11223344, be32toh(hdr->teid));
        ABTS_INT_EQUAL(tc, OGS_GTP2_CAUSE_SERVICE_NOT_SUPPORTED,
                buffer[OGS_GTPV2C_HEADER_LEN + 4]);
    }
    first_len = len;
    if (len > 0) memcpy(first, buffer, len);
    ABTS_INT_EQUAL(tc, 1, ogs_list_count(&peer.remote_list));
    mme_s10_handle_message(&peer, &m);
    len = recv(receiver, buffer, sizeof(buffer), 0);
    ABTS_INT_EQUAL(tc, first_len, len);
    if (len > 0 && first_len == len)
        ABTS_INT_EQUAL(tc, 0, memcmp(first, buffer, len));
    ABTS_INT_EQUAL(tc, 1, ogs_list_count(&peer.remote_list));

    memset(&m, 0, sizeof(m));
    m.h.type = OGS_GTP2_CONTEXT_ACKNOWLEDGE_TYPE;
    m.h.teid_presence = 1;
    m.h.sqn = OGS_GTP2_XID_TO_SQN(0x1203);
    mme_s10_handle_message(&peer, &m);
    ABTS_TRUE(tc, ogs_list_empty(&peer.remote_list));

    ogs_gtp_xact_delete_all(&peer);
    ogs_gtp_xact_final();
    ogs_timer_mgr_destroy(ogs_app()->timer_mgr);
    ogs_app()->timer_mgr = saved_timers;
    ogs_app()->pool.xact = saved_xacts;
    *ogs_local_conf() = saved_conf;

cleanup:
    if (receiver >= 0) close(receiver);
    if (sender_fd >= 0) close(sender_fd);
}
#endif

abts_suite *test_mme_s10_handler(abts_suite *suite)
{
    suite = ADD_SUITE(suite)
#if !defined(_WIN32)
    abts_run_test(suite, s10_loopback_test, NULL);
#endif
    return suite;
}
