/*
 This source file is part of the Swift.org open source project

 Copyright (c) 2021 Apple Inc. and the Swift project authors
 Licensed under Apache License v2.0 with Runtime Library Exception

 See http://swift.org/LICENSE.txt for license information
 See http://swift.org/CONTRIBUTORS.txt for Swift project authors
 */

/*
 * Copyright 1995-2020 The OpenSSL Project Authors. All Rights Reserved.
 *
 * Licensed under the Apache License 2.0 (the "License").  You may not use
 * this file except in compliance with the License.  You can obtain a copy
 * in the file LICENSE in the source distribution or at
 * https://www.openssl.org/source/license.html
 */

#include "CPackageCollectionSigning_ocsp.h"
#include "CPackageCollectionSigning_ocsp_local.h"
#include <string.h>

static int ocsp_find_signer(X509 **psigner, OCSP_BASICRESP *bs,
                            STACK_OF(X509) *certs, unsigned long flags);
static X509 *ocsp_find_signer_sk(STACK_OF(X509) *certs, OCSP_RESPID *id);
static int ocsp_check_issuer(OCSP_BASICRESP *bs, STACK_OF(X509) *chain);
static int ocsp_check_ids(STACK_OF(OCSP_SINGLERESP) *sresp,
                          OCSP_CERTID **ret);
static int ocsp_match_issuerid(X509 *cert, OCSP_CERTID *cid,
                               STACK_OF(OCSP_SINGLERESP) *sresp);
static int ocsp_check_delegated(X509 *x);

/* Returns 1 on success, 0 on failure, or -1 on fatal error */
static int ocsp_verify_signer(X509 *signer, int response,
                              X509_STORE *st, unsigned long flags,
                              STACK_OF(X509) *untrusted, STACK_OF(X509) **chain)
{
    X509_STORE_CTX *ctx = X509_STORE_CTX_new();
    X509_VERIFY_PARAM *vp;
    int ret = -1;

    if (ctx == NULL) {
        goto end;
    }
    if (!X509_STORE_CTX_init(ctx, st, signer, untrusted)) {
        goto end;
    }
    if ((vp = X509_STORE_CTX_get0_param(ctx)) == NULL)
        goto end;
    if ((flags & OCSP_PARTIAL_CHAIN) != 0)
        X509_VERIFY_PARAM_set_flags(vp, X509_V_FLAG_PARTIAL_CHAIN);
    if (response
            && X509_get_ext_by_NID(signer, NID_id_pkix_OCSP_noCheck, -1) >= 0)
        /*
         * Locally disable revocation status checking for OCSP responder cert.
         * Done here for CRLs; TODO should be done also for OCSP-based checks.
         */
        X509_VERIFY_PARAM_clear_flags(vp, X509_V_FLAG_CRL_CHECK);
    X509_STORE_CTX_set_purpose(ctx, X509_PURPOSE_OCSP_HELPER);
    X509_STORE_CTX_set_trust(ctx, X509_TRUST_OCSP_REQUEST);
    /* TODO: why is X509_TRUST_OCSP_REQUEST set? Seems to get ignored. */

    ret = X509_verify_cert(ctx);
    if (ret <= 0) {
        goto end;
    }
    if (chain != NULL)
        *chain = X509_STORE_CTX_get1_chain(ctx);

 end:
    X509_STORE_CTX_free(ctx);
    return ret;
}

static int ocsp_verify(OCSP_BASICRESP *bs,
                       X509 *signer, unsigned long flags)
{
    EVP_PKEY *skey;
    int ret = 1;

    if ((flags & OCSP_NOSIGS) == 0) {
        // Note (Yim Lee): BoringSSL has X509_get_pubkey but not X509_get0_pubkey.
        // The only difference between the two is that the key returned by X509_get_pubkey
        // must be freed up after use. We choose to call X509_get_pubkey instead to avoid
        // copying a bunch of code and risk complications.
        if ((skey = X509_get_pubkey(signer)) == NULL) {
            ret = -1;
            goto end;
        }
        ret = OCSP_BASICRESP_verify(bs, skey);
    }
end:
    EVP_PKEY_free(skey);
    return ret;
}

/* Verify a basic response message */
int OCSP_basic_verify(OCSP_BASICRESP *bs, STACK_OF(X509) *certs,
                      X509_STORE *st, unsigned long flags)
{
    X509 *signer, *x;
    STACK_OF(X509) *chain = NULL;
    STACK_OF(X509) *untrusted = NULL;
    int ret = ocsp_find_signer(&signer, bs, certs, flags);
    int i;

    if (ret == 0) {
        goto end;
    }
    if ((ret == 2) && (flags & OCSP_TRUSTOTHER) != 0)
        flags |= OCSP_NOVERIFY;

    if ((ret = ocsp_verify(bs, signer, flags)) <= 0)
        goto end;
    if ((flags & OCSP_NOVERIFY) == 0) {
        ret = -1;
        if ((flags & OCSP_NOCHAIN) != 0) {
            untrusted = NULL;
        } else if (bs->certs != NULL && certs != NULL) {
            untrusted = sk_X509_dup(bs->certs);
            // Note (Yim Lee): BoringSSL doesn't have X509_add_certs. Since here we are just adding contents of
            // one stack to another, we'd rather not copy a bunch of code from OpenSSL and risk complications.
//            if (!X509_add_certs(untrusted, certs, X509_ADD_FLAG_DEFAULT))
//                goto end;
            for (i = 0; i < sk_X509_num(certs); i++) {
                if (!sk_X509_insert(untrusted, sk_X509_value(certs, i), -1))
                    goto end;
            }
        } else if (certs != NULL) {
            untrusted = certs;
        } else {
            untrusted = bs->certs;
        }
        ret = ocsp_verify_signer(signer, 1, st, flags, untrusted, &chain);
        if (ret <= 0)
            goto end;
        if ((flags & OCSP_NOCHECKS) != 0) {
            ret = 1;
            goto end;
        }
        /*
         * At this point we have a valid certificate chain need to verify it
         * against the OCSP issuer criteria.
         */
        ret = ocsp_check_issuer(bs, chain);

        /* If fatal error or valid match then finish */
        if (ret != 0)
            goto end;

        /*
         * Easy case: explicitly trusted. Get root CA and check for explicit
         * trust
         */
        if ((flags & OCSP_NOEXPLICIT) != 0)
            goto end;

        x = sk_X509_value(chain, sk_X509_num(chain) - 1);
        if (X509_check_trust(x, NID_OCSP_sign, 0) != X509_TRUST_TRUSTED) {
            ret = 0;
            goto end;
        }
        ret = 1;
    }

 end:
    sk_X509_pop_free(chain, X509_free);
    if (bs->certs && certs)
        sk_X509_free(untrusted);
    return ret;
}

static int ocsp_find_signer(X509 **psigner, OCSP_BASICRESP *bs,
                            STACK_OF(X509) *certs, unsigned long flags)
{
    X509 *signer;
    OCSP_RESPID *rid = bs->tbsResponseData->responderId;

    if ((signer = ocsp_find_signer_sk(certs, rid)) != NULL) {
        *psigner = signer;
        return 2;
    }
    if ((flags & OCSP_NOINTERN) == 0 &&
        (signer = ocsp_find_signer_sk(bs->certs, rid))) {
        *psigner = signer;
        return 1;
    }
    /* Maybe lookup from store if by subject name */

    *psigner = NULL;
    return 0;
}

static X509 *ocsp_find_signer_sk(STACK_OF(X509) *certs, OCSP_RESPID *id)
{
    int i;
    unsigned char tmphash[SHA_DIGEST_LENGTH], *keyhash;
    X509 *x;

    /* Easy if lookup by name */
    if (id->type == V_OCSP_RESPID_NAME)
        return X509_find_by_subject(certs, id->value.byName);

    /* Lookup by key hash */

    /* If key hash isn't SHA1 length then forget it */
    if (id->value.byKey->length != SHA_DIGEST_LENGTH)
        return NULL;
    keyhash = id->value.byKey->data;
    /* Calculate hash of each key and compare */
    for (i = 0; i < sk_X509_num(certs); i++) {
        x = sk_X509_value(certs, i);
        if (!X509_pubkey_digest(x, EVP_sha1(), tmphash, NULL))
            break;
        if (memcmp(keyhash, tmphash, SHA_DIGEST_LENGTH) == 0)
            return x;
    }
    return NULL;
}

static int ocsp_check_issuer(OCSP_BASICRESP *bs, STACK_OF(X509) *chain)
{
    STACK_OF(OCSP_SINGLERESP) *sresp = bs->tbsResponseData->responses;
    X509 *signer, *sca;
    OCSP_CERTID *caid = NULL;
    int ret;

    if (sk_X509_num(chain) <= 0) {
        return -1;
    }

    /* See if the issuer IDs match. */
    ret = ocsp_check_ids(sresp, &caid);

    /* If ID mismatch or other error then return */
    if (ret <= 0)
        return ret;

    signer = sk_X509_value(chain, 0);
    /* Check to see if OCSP responder CA matches request CA */
    if (sk_X509_num(chain) > 1) {
        sca = sk_X509_value(chain, 1);
        ret = ocsp_match_issuerid(sca, caid, sresp);
        if (ret < 0)
            return ret;
        if (ret != 0) {
            /* We have a match, if extensions OK then success */
            if (ocsp_check_delegated(signer))
                return 1;
            return 0;
        }
    }

    /* Otherwise check if OCSP request signed directly by request CA */
    return ocsp_match_issuerid(signer, caid, sresp);
}

/*
 * Check the issuer certificate IDs for equality. If there is a mismatch with
 * the same algorithm then there's no point trying to match any certificates
 * against the issuer. If the issuer IDs all match then we just need to check
 * equality against one of them.
 */
static int ocsp_check_ids(STACK_OF(OCSP_SINGLERESP) *sresp, OCSP_CERTID **ret)
{
    OCSP_CERTID *tmpid, *cid;
    int i, idcount;

    idcount = (int)sk_OCSP_SINGLERESP_num(sresp);
    if (idcount <= 0) {
        return -1;
    }

    cid = sk_OCSP_SINGLERESP_value(sresp, 0)->certId;

    *ret = NULL;
    for (i = 1; i < idcount; i++) {
        tmpid = sk_OCSP_SINGLERESP_value(sresp, i)->certId;
        /* Check to see if IDs match */
        if (OCSP_id_issuer_cmp(cid, tmpid)) {
            /* If algorithm mismatch let caller deal with it */
            if (OBJ_cmp(tmpid->hashAlgorithm->algorithm,
                        cid->hashAlgorithm->algorithm))
                return 2;
            /* Else mismatch */
            return 0;
        }
    }

    /* All IDs match: only need to check one ID */
    *ret = cid;
    return 1;
}

/*
 * Match the certificate issuer ID.
 * Returns -1 on fatal error, 0 if there is no match and 1 if there is a match.
 */
static int ocsp_match_issuerid(X509 *cert, OCSP_CERTID *cid,
                               STACK_OF(OCSP_SINGLERESP) *sresp)
{
    /* If only one ID to match then do it */
    if (cid != NULL) {
        const EVP_MD *dgst = EVP_get_digestbyobj(cid->hashAlgorithm->algorithm);
        const X509_NAME *iname;
        int mdlen;
        unsigned char md[EVP_MAX_MD_SIZE];

        if (dgst == NULL) {
            return -1;
        }

        mdlen = (int)EVP_MD_size(dgst);
        if (mdlen < 0) {
            return -1;
        }
        if (cid->issuerNameHash->length != mdlen ||
            cid->issuerKeyHash->length != mdlen)
            return 0;
        iname = X509_get_subject_name(cert);
        if (!X509_NAME_digest(iname, dgst, md, NULL)) {
            return -1;
        }
        if (memcmp(md, cid->issuerNameHash->data, mdlen) != 0)
            return 0;
        if (!X509_pubkey_digest(cert, dgst, md, NULL)) {
            return -1;
        }
        if (memcmp(md, cid->issuerKeyHash->data, mdlen) != 0)
            return 0;
    } else {
        /* We have to match the whole lot */
        int i, ret;
        OCSP_CERTID *tmpid;

        for (i = 0; i < sk_OCSP_SINGLERESP_num(sresp); i++) {
            tmpid = sk_OCSP_SINGLERESP_value(sresp, i)->certId;
            ret = ocsp_match_issuerid(cert, tmpid, NULL);
            if (ret <= 0)
                return ret;
        }
    }
    return 1;
}

static int ocsp_check_delegated(X509 *x)
{
    if ((X509_get_extension_flags(x) & EXFLAG_XKUSAGE)
        && (X509_get_extended_key_usage(x) & XKU_OCSP_SIGN))
        return 1;
    return 0;
}