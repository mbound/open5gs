/*
 * Thin wrapper for nsmf-build-body.inc.
 *
 * The body is the pre-existing nsmf-build.c blob. This wrapper augments every
 * NR user location built by the AMF with the Release-19 NTN TAI information
 * retained from NGAP UserLocationInformationNR.
 */

#include "nsmf-build.h"

static bool amf_nsmf_add_ntn_tai_info(
        OpenAPI_nr_location_t *nr_location,
        const amf_nr_ntn_tai_info_t *ntn_tai)
{
    int i;
    ogs_plmn_id_t serving_plmn;
    OpenAPI_ntn_tai_info_t *NtnTaiInfo = NULL;

    ogs_assert(nr_location);
    ogs_assert(ntn_tai);

    if (!ntn_tai->presence)
        return true;

    NtnTaiInfo = ogs_calloc(1, sizeof(*NtnTaiInfo));
    if (!NtnTaiInfo)
        return false;

    memcpy(&serving_plmn, &ntn_tai->serving_plmn, sizeof(serving_plmn));
    NtnTaiInfo->plmn_id = ogs_sbi_build_plmn_id_nid(&serving_plmn);
    if (!NtnTaiInfo->plmn_id)
        goto error;

    NtnTaiInfo->tac_list = OpenAPI_list_create();
    if (!NtnTaiInfo->tac_list)
        goto error;

    for (i = 0; i < ntn_tai->num_of_tac; i++) {
        char *tac = ogs_uint24_to_0string(ntn_tai->tac[i]);
        if (!tac)
            goto error;
        OpenAPI_list_add(NtnTaiInfo->tac_list, tac);
    }

    if (!NtnTaiInfo->tac_list->count) {
        ogs_error("NR NTN TAI information has an empty TAC list");
        goto error;
    }

    if (ntn_tai->derived_tac_presence) {
        NtnTaiInfo->derived_tac =
            ogs_uint24_to_0string(ntn_tai->derived_tac);
        if (!NtnTaiInfo->derived_tac)
            goto error;
    }

    nr_location->ntn_tai_info = NtnTaiInfo;
    return true;

error:
    OpenAPI_ntn_tai_info_free(NtnTaiInfo);
    return false;
}

static OpenAPI_nr_location_t *amf_nsmf_build_nr_location_with_ntn(
        ogs_5gs_tai_t *tai, ogs_nr_cgi_t *nr_cgi, amf_ue_t *amf_ue)
{
    OpenAPI_nr_location_t *nr_location = NULL;

    ogs_assert(amf_ue);

    nr_location = (ogs_sbi_build_nr_location)(tai, nr_cgi);
    if (!nr_location)
        return NULL;

    if (!amf_nsmf_add_ntn_tai_info(
            nr_location, &amf_ue->nr_ntn_tai)) {
        ogs_sbi_free_nr_location(nr_location);
        return NULL;
    }

    return nr_location;
}

/*
 * nsmf-build-body.inc has three calls to ogs_sbi_build_nr_location(), and
 * each owning function has the corresponding amf_ue in scope.
 */
#define ogs_sbi_build_nr_location(__tai, __cgi) \
    amf_nsmf_build_nr_location_with_ntn((__tai), (__cgi), amf_ue)

#include "nsmf-build-body.inc"

#undef ogs_sbi_build_nr_location
