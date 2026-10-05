/*
 * Thin wrapper for ngap-handler-body.inc.
 *
 * The body is the pre-existing ngap-handler.c blob. Keeping it as an include
 * lets this branch inject the Release-19 NTN UserLocationInformationNR
 * extension handling without rewriting the very large source file through
 * the GitHub Contents API. This can be folded back into ngap-handler.c when
 * the branch is rebased/edited from a normal local checkout.
 */

#include "ngap-handler.h"
#include "ngap-path.h"
#include "sbi-path.h"
#include "namf-build.h"
#include "nas-path.h"

static void ngap_store_nr_ntn_tai_information(
        NGAP_UserLocationInformationNR_t *location, ogs_nr_cgi_t *nr_cgi)
{
    int i, j;
    ran_ue_t *ran_ue = NULL;
    amf_ue_t *amf_ue = NULL;
    amf_nr_ntn_tai_info_t *ntn_tai = NULL;
    NGAP_ProtocolExtensionContainer_14713P459_t *extensions = NULL;

    ogs_assert(location);
    ogs_assert(nr_cgi);

    ran_ue = (ran_ue_t *)((char *)nr_cgi -
            offsetof(ran_ue_t, saved.nr_cgi));
    ntn_tai = &ran_ue->saved.nr_ntn_tai;
    memset(ntn_tai, 0, sizeof(*ntn_tai));

    if (!location->iE_Extensions)
        return;

    extensions = (NGAP_ProtocolExtensionContainer_14713P459_t *)
        location->iE_Extensions;

    for (i = 0; i < OGS_ASN_LIST_COUNT(extensions); i++) {
        NGAP_UserLocationInformationNR_ExtIEs_t *ext = NULL;
        NGAP_NRNTNTAIInformation_t *info = NULL;

        ext = (NGAP_UserLocationInformationNR_ExtIEs_t *)
            OGS_ASN_LIST_GET(extensions, i);
        if (!ext || ext->id != NGAP_ProtocolIE_ID_id_NRNTNTAIInformation)
            continue;

        if (ext->extensionValue.present !=
                NGAP_UserLocationInformationNR_ExtIEs__extensionValue_PR_NRNTNTAIInformation ||
            !ext->extensionValue.choice.NRNTNTAIInformation) {
            ogs_warn("Invalid NRNTNTAIInformation extension");
            return;
        }

        info = ext->extensionValue.choice.NRNTNTAIInformation;
        if (info->servingPLMN.size != OGS_PLMN_ID_LEN ||
            !info->servingPLMN.buf || !info->tACListInNRNTN) {
            ogs_warn("Malformed NRNTNTAIInformation");
            return;
        }

        memcpy(&ntn_tai->serving_plmn,
                info->servingPLMN.buf, OGS_PLMN_ID_LEN);

        ntn_tai->num_of_tac = OGS_ASN_LIST_COUNT(info->tACListInNRNTN);
        if (ntn_tai->num_of_tac > AMF_MAX_NUM_OF_NTN_TAC) {
            ogs_warn("NR NTN TAC list truncated [%d -> %d]",
                    ntn_tai->num_of_tac, AMF_MAX_NUM_OF_NTN_TAC);
            ntn_tai->num_of_tac = AMF_MAX_NUM_OF_NTN_TAC;
        }

        for (j = 0; j < ntn_tai->num_of_tac; j++) {
            NGAP_TAC_t *tac = (NGAP_TAC_t *)
                OGS_ASN_LIST_GET(info->tACListInNRNTN, j);
            if (!tac || tac->size != 3 || !tac->buf) {
                ogs_warn("Invalid TAC in NRNTNTAIInformation [%d]", j);
                memset(ntn_tai, 0, sizeof(*ntn_tai));
                return;
            }
            ogs_asn_OCTET_STRING_to_uint24(tac, &ntn_tai->tac[j]);
        }

        if (info->uELocationDerivedTACInNRNTN) {
            if (info->uELocationDerivedTACInNRNTN->size != 3 ||
                !info->uELocationDerivedTACInNRNTN->buf) {
                ogs_warn("Invalid UE-location-derived TAC in "
                        "NRNTNTAIInformation");
                memset(ntn_tai, 0, sizeof(*ntn_tai));
                return;
            }
            ogs_asn_OCTET_STRING_to_uint24(
                    info->uELocationDerivedTACInNRNTN,
                    &ntn_tai->derived_tac);
            ntn_tai->derived_tac_presence = true;
        }

        ntn_tai->presence = true;

        /*
         * TS 38.300 NTN mobility: when NRNTNTAIInformation is present,
         * NR-CGI.nRCellIdentity represents the NTN Mapped Cell ID.
         */
        ogs_debug("    NTN MappedCellID[0x%llx] NTN-TACs[%d]",
                (long long)nr_cgi->cell_id, ntn_tai->num_of_tac);

        if (ran_ue->amf_ue_id >= OGS_MIN_POOL_ID &&
            ran_ue->amf_ue_id <= OGS_MAX_POOL_ID) {
            amf_ue = amf_ue_find_by_id(ran_ue->amf_ue_id);
            if (amf_ue)
                memcpy(&amf_ue->nr_ntn_tai, ntn_tai,
                        sizeof(amf_ue->nr_ntn_tai));
        }
        return;
    }
}

static int ngap_sess_sbi_discover_and_send(
        OpenAPI_service_name_e service_name,
        ogs_sbi_discovery_option_t *discovery_option,
        ogs_sbi_request_t *(*build)(amf_sess_t *sess, void *data),
        ran_ue_t *ran_ue, amf_sess_t *sess, int state, void *data)
{
    if (state == AMF_UPDATE_SM_CONTEXT_HANDOVER_NOTIFY && data) {
        amf_nsmf_pdusession_sm_context_param_t *param = data;
        param->ue_location = true;
    }

    return (amf_sess_sbi_discover_and_send)(
            service_name, discovery_option, build,
            ran_ue, sess, state, data);
}

/*
 * ngap-handler-body.inc has exactly four NR-CGI decode sites and each has
 * the local UserLocationInformationNR variable. Interpose only in this
 * translation unit so all NR user-location update paths retain the NTN
 * extension alongside the ordinary NR-CGI/TAI.
 */
#define ogs_ngap_ASN_to_nr_cgi(__src, __dst) \
    do { \
        (ogs_ngap_ASN_to_nr_cgi)((__src), (__dst)); \
        ngap_store_nr_ntn_tai_information( \
                UserLocationInformationNR, (__dst)); \
    } while (0)

#define amf_sess_sbi_discover_and_send(...) \
    ngap_sess_sbi_discover_and_send(__VA_ARGS__)

#include "ngap-handler-body.inc"

#undef amf_sess_sbi_discover_and_send
#undef ogs_ngap_ASN_to_nr_cgi
