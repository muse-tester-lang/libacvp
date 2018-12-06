/** @file */
/*****************************************************************************
* Copyright (c) 2017, Cisco Systems, Inc.
* All rights reserved.

* Redistribution and use in source and binary forms, with or without modification,
* are permitted provided that the following conditions are met:
*
* 1. Redistributions of source code must retain the above copyright notice,
*    this list of conditions and the following disclaimer.
*
* 2. Redistributions in binary form must reproduce the above copyright notice,
*    this list of conditions and the following disclaimer in the documentation
*    and/or other materials provided with the distribution.
*
* THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
* AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
* IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
* DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
* FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
* DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
* SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
* CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
* OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE
* USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*****************************************************************************/
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "acvp.h"
#include "acvp_lcl.h"
#include "parson.h"

/*
 * Forward prototypes for local functions
 */
static ACVP_RESULT acvp_drbg_output_tc(ACVP_CTX *ctx, ACVP_DRBG_TC *stc, JSON_Object *tc_rsp);

static ACVP_RESULT acvp_drbg_init_tc(ACVP_CTX *ctx,
                                     ACVP_DRBG_TC *stc,
                                     unsigned int tc_id,
                                     const char *additional_input,
                                     const char *entropy_input_pr,
                                     const char *additional_input_1,
                                     const char *entropy_input_pr_1,
                                     const char *perso_string,
                                     const char *entropy,
                                     const char *nonce,
                                     int der_func_enabled,
                                     int pred_resist_enabled,
                                     unsigned int additional_input_len,
                                     unsigned int perso_string_len,
                                     unsigned int entropy_len,
                                     unsigned int nonce_len,
                                     unsigned int drb_len,
                                     ACVP_DRBG_MODE mode_id,
                                     ACVP_CIPHER alg_id);

static ACVP_RESULT acvp_drbg_release_tc(ACVP_DRBG_TC *stc);

ACVP_RESULT acvp_drbg_kat_handler(ACVP_CTX *ctx, JSON_Object *obj) {
    char *json_result = NULL;

    JSON_Value *reg_arry_val = NULL;
    JSON_Object *reg_obj = NULL;
    JSON_Array *reg_arry = NULL;

    JSON_Value *groupval;
    JSON_Object *groupobj = NULL;
    JSON_Value *testval;
    JSON_Object *testobj = NULL;
    JSON_Array *groups;
    JSON_Array *tests;
    JSON_Array *pred_resist_input;
    int i, g_cnt;
    int j, t_cnt;
    JSON_Value *r_vs_val = NULL;
    JSON_Object *r_vs = NULL;
    JSON_Array *r_tarr = NULL, *r_garr = NULL;  /* Response testarray, grouparray */
    JSON_Value *r_tval = NULL, *r_gval = NULL;  /* Response testval, groupval */
    JSON_Object *r_tobj = NULL, *r_gobj = NULL; /* Response testobj, groupobj */
    ACVP_CAPS_LIST *cap;
    ACVP_DRBG_TC stc;
    ACVP_TEST_CASE tc;
    ACVP_RESULT rv;
    const char *alg_str = NULL;
    ACVP_CIPHER alg_id;
    ACVP_DRBG_MODE mode_id;

    if (!ctx) {
        ACVP_LOG_ERR("No ctx for handler operation");
        return ACVP_NO_CTX;
    }

    alg_str = json_object_get_string(obj, "algorithm");
    if (!alg_str) {
        ACVP_LOG_ERR("unable to parse 'algorithm' from JSON");
        return ACVP_MALFORMED_JSON;
    }

    ACVP_LOG_INFO("    DRBG alg: %s", alg_str);

    /*
     * Get a reference to the abstracted test case
     */
    tc.tc.drbg = &stc;

    /*
     * Get the crypto module handler for this DRBG algorithm
     */
    alg_id = acvp_lookup_cipher_index(alg_str);
    if ((alg_id < ACVP_HASHDRBG) || (alg_id > ACVP_CTRDRBG)) {
        ACVP_LOG_ERR("unsupported algorithm (%s)", alg_str);
        return ACVP_UNSUPPORTED_OP;
    }

    cap = acvp_locate_cap_entry(ctx, alg_id);
    if (!cap) {
        ACVP_LOG_ERR("ACVP server requesting unsupported capability");
        return ACVP_UNSUPPORTED_OP;
    }

    /*
     * Create ACVP array for response
     */
    rv = acvp_create_array(&reg_obj, &reg_arry_val, &reg_arry);
    if (rv != ACVP_SUCCESS) {
        ACVP_LOG_ERR("Failed to create JSON response struct. ");
        return rv;
    }

    /*
     * Start to build the JSON response
     */
    rv = acvp_setup_json_rsp_group(&ctx, &reg_arry_val, &r_vs_val, &r_vs, alg_str, &r_garr);
    if (rv != ACVP_SUCCESS) {
        ACVP_LOG_ERR("Failed to setup json response");
        return rv;
    }

    groups = json_object_get_array(obj, "testGroups");
    g_cnt = json_array_get_count(groups);
    ACVP_LOG_INFO("Number of TestGroups: %d", g_cnt);
    for (i = 0; i < g_cnt; i++) {
        int tgId = 0;
        const char *mode_str = NULL;
        int der_func_enabled = 0, pred_resist_enabled = 0;
        unsigned int perso_string_len = 0, entropy_len = 0, nonce_len = 0,
                     drb_len = 0, additional_input_len = 0;
        groupval = json_array_get_value(groups, i);
        groupobj = json_value_get_object(groupval);

        /*
         * Create a new group in the response with the tgid
         * and an array of tests
         */
        r_gval = json_value_init_object();
        r_gobj = json_value_get_object(r_gval);
        tgId = json_object_get_number(groupobj, "tgId");
        if (!tgId) {
            ACVP_LOG_ERR("Missing tgid from server JSON groub obj");
            return ACVP_MALFORMED_JSON;
        }
        json_object_set_number(r_gobj, "tgId", tgId);
        json_object_set_value(r_gobj, "tests", json_value_init_array());
        r_tarr = json_object_get_array(r_gobj, "tests");

        /*
         * Get DRBG Mode index
         */
        mode_str = json_object_get_string(groupobj, "mode");
        if (!mode_str) {
            ACVP_LOG_ERR("Server JSON missing 'mode'");
            return ACVP_MALFORMED_JSON;
        }
        mode_id = acvp_lookup_drbg_mode_index(mode_str);
        if (mode_id == ACVP_DRBG_MODE_END) {
            ACVP_LOG_ERR("unsupported DRBG mode (%s)", mode_str);
            return ACVP_UNSUPPORTED_OP;
        }

        /*
         * Handle Group Params
         */
        pred_resist_enabled = json_object_get_boolean(groupobj, "predResistance");
        if (pred_resist_enabled == -1) {
            ACVP_LOG_ERR("Server JSON missing 'predResistance'");
            return ACVP_MISSING_ARG;
        }

        if (alg_id == ACVP_CTRDRBG) {
            der_func_enabled = json_object_get_boolean(groupobj, "derFunc");
            if (der_func_enabled == -1) {
                ACVP_LOG_ERR("Server JSON missing 'derFunc'");
                return ACVP_MISSING_ARG;
            }
        }

        entropy_len = (unsigned int)json_object_get_number(groupobj, "entropyInputLen");
        if (entropy_len < ACVP_DRBG_ENTPY_IN_BIT_MIN ||
            entropy_len > ACVP_DRBG_ENTPY_IN_BIT_MAX) {
            ACVP_LOG_ERR("Server JSON invalid 'entropyInputLen'(%u)",
                         entropy_len);
            return ACVP_INVALID_ARG;
        }

        nonce_len = (unsigned int)json_object_get_number(groupobj, "nonceLen");
        if (!(alg_id == ACVP_CTRDRBG && !der_func_enabled)) {
            /* Allowed to be 0 when counter mode and not using derivation func */
            if (nonce_len < ACVP_DRBG_NONCE_BIT_MIN ||
                nonce_len > ACVP_DRBG_NONCE_BIT_MAX) {
                ACVP_LOG_ERR("Server JSON invalid 'nonceLen'(%u)",
                             nonce_len);
                return ACVP_INVALID_ARG;
            }
        }

        perso_string_len = (unsigned int)json_object_get_number(groupobj, "persoStringLen");
        if (perso_string_len > ACVP_DRBG_PER_SO_BIT_MAX) {
            ACVP_LOG_ERR("Server JSON invalid 'persoStringLen'(%u)",
                         nonce_len);
            return ACVP_INVALID_ARG;
        }

        drb_len = (unsigned int)json_object_get_number(groupobj, "returnedBitsLen");
        if (!drb_len || drb_len > ACVP_DRB_BIT_MAX) {
            ACVP_LOG_ERR("Server JSON invalid 'returnedBitsLen'(%u)",
                         drb_len);
            return ACVP_INVALID_ARG;
        }

        if (pred_resist_enabled) {
            additional_input_len = json_object_get_number(groupobj, "additionalInputLen");
            if (additional_input_len > ACVP_DRBG_ADDI_IN_BIT_MAX) {
                ACVP_LOG_ERR("Server JSON invalid 'additionalInputLen'(%u)",
                             additional_input_len);
                return ACVP_INVALID_ARG;
            }
        }

        ACVP_LOG_INFO("    Test group:");
        ACVP_LOG_INFO("    DRBG mode: %s", mode_str);
        ACVP_LOG_INFO("    derFunc: %s", der_func_enabled ? "true" : "false");
        ACVP_LOG_INFO("    predResistance: %s", pred_resist_enabled ? "true" : "false");
        ACVP_LOG_INFO("    entropyInputLen: %d", entropy_len);
        if (pred_resist_enabled) {
            ACVP_LOG_INFO("    additionalInputLen: %d", additional_input_len);
        }
        ACVP_LOG_INFO("    persoStringLen: %d", perso_string_len);
        ACVP_LOG_INFO("    nonceLen: %d", nonce_len);
        ACVP_LOG_INFO("    returnedBitsLen: %d", drb_len);

        /*
         * Handle test array
         */
        tests = json_object_get_array(groupobj, "tests");
        t_cnt = json_array_get_count(tests);
        ACVP_LOG_INFO("Number of Tests: %d", t_cnt);
        for (j = 0; j < t_cnt; j++) {
            JSON_Value *pr_input_val = NULL;
            JSON_Object *pr_input_obj = NULL;
            unsigned int tc_id = 0, pr_input_count = 0;
            const char *additional_input = NULL, *entropy_input_pr = NULL,
                       *additional_input_1 = NULL, *entropy_input_pr_1 = NULL,
                       *perso_string = NULL, *entropy = NULL, *nonce = NULL;

            ACVP_LOG_INFO("Found new DRBG test vector...");
            testval = json_array_get_value(tests, j);
            testobj = json_value_get_object(testval);

            json_result = json_serialize_to_string_pretty(testval);
            ACVP_LOG_INFO("json testval count: %d\n %s\n", i, json_result);
            json_free_serialized_string(json_result);

            tc_id = (unsigned int)json_object_get_number(testobj, "tcId");

            perso_string = json_object_get_string(testobj, "persoString");
            if (!perso_string) {
                ACVP_LOG_ERR("Server JSON missing 'persoString'");
                return ACVP_MISSING_ARG;
            }
            if (strnlen(perso_string, ACVP_DRBG_PER_SO_STR_MAX + 1)
                > ACVP_DRBG_PER_SO_STR_MAX) {
                ACVP_LOG_ERR("persoString too long, max allowed=(%d)",
                             ACVP_DRBG_PER_SO_STR_MAX);
                return ACVP_INVALID_ARG;
            }

            entropy = json_object_get_string(testobj, "entropyInput");
            if (!entropy) {
                ACVP_LOG_ERR("Server JSON missing 'entropyInput'");
                return ACVP_MISSING_ARG;
            }
            if (strnlen(entropy, ACVP_DRBG_ENTPY_IN_STR_MAX + 1)
                > ACVP_DRBG_ENTPY_IN_STR_MAX) {
                ACVP_LOG_ERR("entropyInput too long, max allowed=(%d)",
                             ACVP_DRBG_ENTPY_IN_STR_MAX);
                return ACVP_INVALID_ARG;
            }

            nonce = json_object_get_string(testobj, "nonce");
            if (!nonce) {
                ACVP_LOG_ERR("Server JSON missing 'nonce'");
                return ACVP_MISSING_ARG;
            }
            if (strnlen(nonce, ACVP_DRBG_NONCE_STR_MAX + 1)
                > ACVP_DRBG_NONCE_STR_MAX) {
                ACVP_LOG_ERR("nonce too long, max allowed=(%d)",
                             ACVP_DRBG_NONCE_STR_MAX);
                return ACVP_INVALID_ARG;
            }

            ACVP_LOG_INFO("        Test case: %d", j);
            ACVP_LOG_INFO("             tcId: %d", tc_id);
            ACVP_LOG_INFO("             entropyInput: %s", entropy);
            ACVP_LOG_INFO("             perso_string: %s", perso_string);
            ACVP_LOG_INFO("             nonce: %s", nonce);

            /*
             * Handle pred_resist_input array. Has at most 2 elements
             */
            pred_resist_input = json_object_get_array(testobj, "otherInput");
            if (!pred_resist_input) {
                ACVP_LOG_ERR("Server JSON missing 'otherInput'");
                return ACVP_MISSING_ARG;
            }

            pr_input_count = json_array_get_count(pred_resist_input);
            if (!pr_input_count) {
                ACVP_LOG_ERR("Server JSON array 'otherInput' is empty");
                return ACVP_INVALID_ARG;
            }

            ACVP_LOG_INFO("Found new DRBG Prediction Input...");

            /* Get 1st element from the array */
            pr_input_val = json_array_get_value(pred_resist_input, 0);
            pr_input_obj = json_value_get_object(pr_input_val);

            additional_input = json_object_get_string(pr_input_obj, "additionalInput");
            if (!additional_input) {
                ACVP_LOG_ERR("Server JSON in otherInput[%d], missing 'additionalInput'", 0);
                return ACVP_MISSING_ARG;
            }
            if (strnlen(additional_input, ACVP_DRBG_ADDI_IN_STR_MAX + 1)
                > ACVP_DRBG_ADDI_IN_STR_MAX) {
                ACVP_LOG_ERR("In otherInput[%d], additionalInput too long. Max allowed=(%d)",
                             0, ACVP_DRBG_ADDI_IN_STR_MAX);
                return ACVP_INVALID_ARG;
            }

            entropy_input_pr = json_object_get_string(pr_input_obj, "entropyInput");
            if (!entropy_input_pr) {
                ACVP_LOG_ERR("Server JSON in otherInput[%d], missing 'entropyInput'", 0);
                return ACVP_MISSING_ARG;
            }
            if (strnlen(entropy_input_pr, ACVP_DRBG_ENTPY_IN_STR_MAX + 1)
                > ACVP_DRBG_ENTPY_IN_STR_MAX) {
                ACVP_LOG_ERR("In otherInput[%d], entropyInput too long. Max allowed=(%d)",
                             0, ACVP_DRBG_ENTPY_IN_STR_MAX);
                return ACVP_INVALID_ARG;
            }

            if (pr_input_count == 2) {
                /*
                 * Get 2nd element from the array
                 */
                pr_input_val = json_array_get_value(pred_resist_input, 1);
                pr_input_obj = json_value_get_object(pr_input_val);

                additional_input_1 = json_object_get_string(pr_input_obj, "additionalInput");
                if (!additional_input_1) {
                    ACVP_LOG_ERR("Server JSON in otherInput[%d], missing 'additionalInput'", 1);
                    return ACVP_MISSING_ARG;
                }
                if (strnlen(additional_input_1, ACVP_DRBG_ADDI_IN_STR_MAX + 1)
                    > ACVP_DRBG_ADDI_IN_STR_MAX) {
                    ACVP_LOG_ERR("In otherInput[%d], additionalInput too long. Max allowed=(%d)",
                                 1, ACVP_DRBG_ADDI_IN_STR_MAX);
                    return ACVP_INVALID_ARG;
                }

                entropy_input_pr_1 = json_object_get_string(pr_input_obj, "entropyInput");
                if (!entropy_input_pr_1) {
                    ACVP_LOG_ERR("Server JSON in otherInput[%d], missing 'entropyInput'", 1);
                    return ACVP_MISSING_ARG;
                }
                if (strnlen(entropy_input_pr_1, ACVP_DRBG_ENTPY_IN_STR_MAX + 1)
                    > ACVP_DRBG_ENTPY_IN_STR_MAX) {
                    ACVP_LOG_ERR("In otherInput[%d], entropyInput too long. Max allowed=(%d)",
                                 1, ACVP_DRBG_ENTPY_IN_STR_MAX);
                    return ACVP_INVALID_ARG;
                }
            }

            /*
             * Create a new test case in the response
             */
            r_tval = json_value_init_object();
            r_tobj = json_value_get_object(r_tval);

            json_object_set_number(r_tobj, "tcId", tc_id);

            /*
             * Setup the test case data that will be passed down to
             * the crypto module.
             */
            rv = acvp_drbg_init_tc(ctx, &stc, tc_id, additional_input,
                                   entropy_input_pr, additional_input_1,
                                   entropy_input_pr_1, perso_string,
                                   entropy, nonce,
                                   der_func_enabled, pred_resist_enabled,
                                   additional_input_len, perso_string_len,
                                   entropy_len, nonce_len,
                                   drb_len, mode_id, alg_id);

            if (rv != ACVP_SUCCESS) {
                acvp_drbg_release_tc(&stc);
                return rv;
            }

            /* Process the current test vector... */
            if ((cap->crypto_handler)(&tc)) {
                ACVP_LOG_ERR("crypto module failed the operation");
                acvp_drbg_release_tc(&stc);
                return ACVP_CRYPTO_MODULE_FAIL;
            }

            /*
             * Output the test case results using JSON
             */
            rv = acvp_drbg_output_tc(ctx, &stc, r_tobj);
            if (rv != ACVP_SUCCESS) {
                ACVP_LOG_ERR("JSON output failure in DRBG module");
                acvp_drbg_release_tc(&stc);
                return rv;
            }

            /*
             * Release all the memory associated with the test case
             */
            acvp_drbg_release_tc(&stc);

            /* Append the test response value to array */
            json_array_append_value(r_tarr, r_tval);
        }
        json_array_append_value(r_garr, r_gval);
    }
    json_array_append_value(reg_arry, r_vs_val);

    json_result = json_serialize_to_string_pretty(ctx->kat_resp);
    if (ctx->debug == ACVP_LOG_LVL_VERBOSE) {
        printf("\n\n%s\n\n", json_result);
    } else {
        ACVP_LOG_INFO("\n\n%s\n\n", json_result);
    }
    json_free_serialized_string(json_result);

    return ACVP_SUCCESS;
}

/*
 * After the test case has been processed by the DUT, the results
 * need to be JSON formated to be included in the vector set results
 * file that will be uploaded to the server.  This routine handles
 * the JSON processing for a single test case.
 */
static ACVP_RESULT acvp_drbg_output_tc(ACVP_CTX *ctx, ACVP_DRBG_TC *stc, JSON_Object *tc_rsp) {
    ACVP_RESULT rv = ACVP_SUCCESS;
    char *tmp = NULL;

    tmp = calloc(ACVP_DRB_STR_MAX + 1, sizeof(char));
    if (!tmp) {
        ACVP_LOG_ERR("Unable to malloc in acvp_drbg_output_tc");
        return ACVP_MALLOC_FAIL;
    }

    rv = acvp_bin_to_hexstr(stc->drb, stc->drb_len, tmp, ACVP_DRB_STR_MAX);
    if (rv != ACVP_SUCCESS) {
        ACVP_LOG_ERR("hex conversion failure (returnedBits)");
        goto end;
    }
    json_object_set_string(tc_rsp, "returnedBits", tmp);

end:
    if (tmp) free(tmp);

    return rv;
}

static ACVP_RESULT acvp_drbg_init_tc(ACVP_CTX *ctx,
                                     ACVP_DRBG_TC *stc,
                                     unsigned int tc_id,
                                     const char *additional_input,
                                     const char *entropy_input_pr,
                                     const char *additional_input_1,
                                     const char *entropy_input_pr_1,
                                     const char *perso_string,
                                     const char *entropy,
                                     const char *nonce,
                                     int der_func_enabled,
                                     int pred_resist_enabled,
                                     unsigned int additional_input_len,
                                     unsigned int perso_string_len,
                                     unsigned int entropy_len,
                                     unsigned int nonce_len,
                                     unsigned int drb_len,
                                     ACVP_DRBG_MODE mode_id,
                                     ACVP_CIPHER alg_id) {
    ACVP_RESULT rv;

    memset(stc, 0x0, sizeof(ACVP_DRBG_TC));

    stc->drb = calloc(ACVP_DRB_BYTE_MAX, sizeof(unsigned char));
    if (!stc->drb) { return ACVP_MALLOC_FAIL; }
    stc->additional_input = calloc(ACVP_DRBG_ADDI_IN_BYTE_MAX, sizeof(unsigned char));
    if (!stc->additional_input) { return ACVP_MALLOC_FAIL; }
    stc->additional_input_1 = calloc(ACVP_DRBG_ADDI_IN_BYTE_MAX, sizeof(unsigned char));
    if (!stc->additional_input_1) { return ACVP_MALLOC_FAIL; }
    stc->entropy = calloc(ACVP_DRBG_ENTPY_IN_BYTE_MAX, sizeof(unsigned char));
    if (!stc->entropy) { return ACVP_MALLOC_FAIL; }
    stc->entropy_input_pr = calloc(ACVP_DRBG_ENTPY_IN_BYTE_MAX, sizeof(unsigned char));
    if (!stc->entropy_input_pr) { return ACVP_MALLOC_FAIL; }
    stc->entropy_input_pr_1 = calloc(ACVP_DRBG_ENTPY_IN_BYTE_MAX, sizeof(unsigned char));
    if (!stc->entropy_input_pr_1) { return ACVP_MALLOC_FAIL; }
    stc->nonce = calloc(ACVP_DRBG_NONCE_BYTE_MAX, sizeof(unsigned char));
    if (!stc->nonce) { return ACVP_MALLOC_FAIL; }
    stc->perso_string = calloc(ACVP_DRBG_PER_SO_BYTE_MAX, sizeof(unsigned char));
    if (!stc->perso_string) { return ACVP_MALLOC_FAIL; }

    if (additional_input) {
        rv = acvp_hexstr_to_bin(additional_input, stc->additional_input,
                                ACVP_DRBG_ADDI_IN_BYTE_MAX, NULL);
        if (rv != ACVP_SUCCESS) {
            ACVP_LOG_ERR("Hex conversion failure (additional_input)");
            return rv;
        }
    }

    if (entropy_input_pr) {
        rv = acvp_hexstr_to_bin(entropy_input_pr, stc->entropy_input_pr,
                                ACVP_DRBG_ENTPY_IN_BYTE_MAX, NULL);
        if (rv != ACVP_SUCCESS) {
            ACVP_LOG_ERR("Hex conversion failure (entropy_input_pr)");
            return rv;
        }
    }

    if (additional_input_1) {
        rv = acvp_hexstr_to_bin(additional_input_1, stc->additional_input_1,
                                ACVP_DRBG_ADDI_IN_BYTE_MAX, NULL);
        if (rv != ACVP_SUCCESS) {
            ACVP_LOG_ERR("Hex conversion failure (2nd additional_input)");
            return rv;
        }
    }

    if (entropy_input_pr_1) {
        rv = acvp_hexstr_to_bin(entropy_input_pr_1, stc->entropy_input_pr_1,
                                ACVP_DRBG_ENTPY_IN_BYTE_MAX, NULL);
        if (rv != ACVP_SUCCESS) {
            ACVP_LOG_ERR("Hex conversion failure (2nd entropy_input_pr)");
            return rv;
        }
    }

    if (entropy) {
        rv = acvp_hexstr_to_bin(entropy, stc->entropy,
                                ACVP_DRBG_ENTPY_IN_BYTE_MAX, NULL);
        if (rv != ACVP_SUCCESS) {
            ACVP_LOG_ERR("Hex conversion failure (entropy)");
            return rv;
        }
    }

    if (perso_string) {
        rv = acvp_hexstr_to_bin(perso_string, stc->perso_string,
                                ACVP_DRBG_PER_SO_BYTE_MAX, NULL);
        if (rv != ACVP_SUCCESS) {
            ACVP_LOG_ERR("Hex conversion failure (perso_string)");
            return rv;
        }
    }

    if (nonce) {
        rv = acvp_hexstr_to_bin(nonce, stc->nonce,
                                ACVP_DRBG_NONCE_BYTE_MAX, NULL);
        if (rv != ACVP_SUCCESS) {
            ACVP_LOG_ERR("Hex conversion failure (nonce)");
            return rv;
        }
    }

    stc->der_func_enabled = der_func_enabled;
    stc->pred_resist_enabled = pred_resist_enabled;
    stc->additional_input_len = ACVP_BIT2BYTE(additional_input_len);
    stc->perso_string_len = ACVP_BIT2BYTE(perso_string_len);
    stc->entropy_len = ACVP_BIT2BYTE(entropy_len);
    stc->nonce_len = ACVP_BIT2BYTE(nonce_len);
    stc->drb_len = ACVP_BIT2BYTE(drb_len);

    stc->tc_id = tc_id;
    stc->mode = mode_id;
    stc->cipher = alg_id;

    return ACVP_SUCCESS;
}

/*
 * This function simply releases the data associated with
 * a test case.
 */
static ACVP_RESULT acvp_drbg_release_tc(ACVP_DRBG_TC *stc) {
    if (stc->drb) free(stc->drb);
    if (stc->additional_input) free(stc->additional_input);
    if (stc->additional_input_1) free(stc->additional_input_1);
    if (stc->entropy) free(stc->entropy);
    if (stc->entropy_input_pr) free(stc->entropy_input_pr);
    if (stc->entropy_input_pr_1) free(stc->entropy_input_pr_1);
    if (stc->nonce) free(stc->nonce);
    if (stc->perso_string) free(stc->perso_string);

    memset(stc, 0x0, sizeof(ACVP_DRBG_TC));
    return ACVP_SUCCESS;
}
