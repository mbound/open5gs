/*
 * Copyright (C) 2026 by Open5GS contributors
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */
#ifndef MME_S10_HANDLER_H
#define MME_S10_HANDLER_H

#include "mme-context.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Receive a decoded GTPv2-C mobility packet from a configured S10 MME.
 * S10 handover is NOT enabled: all requests except Echo are rejected.
 * This path must not use S11 UE/bearer or SGW transactions. */
void mme_s10_handle_message(
        ogs_gtp_node_t *gnode, ogs_gtp2_message_t *message);

#ifdef __cplusplus
}
#endif
#endif
