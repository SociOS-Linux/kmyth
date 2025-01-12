/**
 * @file ecdh_util.c
 *
 * @brief Provides implementation for functionality directly supporting
 *        elliptic curve Diffe-Hellman key agreement within SGX applications
 *        employing kmyth.
 */

#include "ecdh_util.h"


/*****************************************************************************
 * create_ecdh_ephemeral_keypair()
 ****************************************************************************/
int create_ecdh_ephemeral_keypair(EVP_PKEY ** ephemeral_key_pair)
{
  // create parameter generation context for creating ephemeral key pair
  EVP_PKEY_CTX *pctx = EVP_PKEY_CTX_new_id(EVP_PKEY_EC, NULL);
  if (pctx == NULL)
  {
    kmyth_sgx_log(LOG_ERR, "failed to create parameter generation context");
    return EXIT_FAILURE;
  }

  // initialize parameter generation context
  if (EVP_PKEY_paramgen_init(pctx) != 1)
  {
    kmyth_sgx_log(LOG_ERR, "failed to initialize params generation context");
    EVP_PKEY_CTX_free(pctx);
    return EXIT_FAILURE;
  }

  // configure parameter generation context for desired curve
  if (EVP_PKEY_CTX_set_ec_paramgen_curve_nid(pctx, KMYTH_EC_NID) != 1)
  {
    kmyth_sgx_log(LOG_ERR, "failed to configure parameter generation context");
    EVP_PKEY_CTX_free(pctx);
    return EXIT_FAILURE;
  }

  // create empty parameters object
  EVP_PKEY *params = EVP_PKEY_new();
  if (params == NULL)
  {
    kmyth_sgx_log(LOG_ERR, "failed to create empty parameters object")
  }

  // generate parameters
  if (EVP_PKEY_paramgen(pctx, &params) != 1)
  {
    kmyth_sgx_log(LOG_ERR, "parameter generation failed");
    EVP_PKEY_free(params);
    EVP_PKEY_CTX_free(pctx);
    return EXIT_FAILURE;
  }

  // clean-up parameter generation context
  EVP_PKEY_CTX_free(pctx);

  // create key generation context using parameters
  EVP_PKEY_CTX *kctx = EVP_PKEY_CTX_new(params, NULL);
  if (kctx == NULL)
  {
    kmyth_sgx_log(LOG_ERR, "create key generation context failed");
    EVP_PKEY_free(params);
    return EXIT_FAILURE;
  }
  EVP_PKEY_free(params);

  // initialize key generation context
  if (EVP_PKEY_keygen_init(kctx) != 1)
  {
    kmyth_sgx_log(LOG_ERR, "initialize key generation context failed");
    EVP_PKEY_CTX_free(kctx);
    return EXIT_FAILURE;
  }

  // generate new key pair
  if (EVP_PKEY_keygen(kctx, ephemeral_key_pair) != 1)
  {
    kmyth_sgx_log(LOG_ERR, "key generation failed");
    EVP_PKEY_CTX_free(kctx);
    return EXIT_FAILURE;
  }

  // clean-up key generation context
  EVP_PKEY_CTX_free(kctx);

  return EXIT_SUCCESS;
}

/*****************************************************************************
 * compute_ecdh_shared_secret()
 ****************************************************************************/
int compute_ecdh_shared_secret(EVP_PKEY *local_eph_keypair,
                               EVP_PKEY *peer_eph_pubkey,
                               unsigned char **shared_secret,
                               size_t *shared_secret_len)
{
  EVP_PKEY_CTX *ctx = NULL;
  int retval = 0;
  
  // create the context for the shared secret derivation
  ctx = EVP_PKEY_CTX_new(local_eph_keypair, NULL);
  if (ctx == NULL)
  {
    kmyth_sgx_log(LOG_ERR, "error creating shared secret derivation context");
    return EXIT_FAILURE;
  }

  // initialize the newly created context
  retval = EVP_PKEY_derive_init(ctx);
  if (retval != 1)
  {
    kmyth_sgx_log(LOG_ERR, "init error for shared secret derivation context");
    EVP_PKEY_CTX_free(ctx);
    return EXIT_FAILURE;
  }

  // provide peer's public key
  retval = EVP_PKEY_derive_set_peer(ctx, peer_eph_pubkey);
  if (retval != 1)
  {
    kmyth_sgx_log(LOG_ERR, "error setting peer's public key in context");
    EVP_PKEY_CTX_free(ctx);
    return EXIT_FAILURE;
  }

  // compute buffer size required for shared secret
  retval = EVP_PKEY_derive(ctx, NULL, shared_secret_len);
  if ((retval != 1) || (*shared_secret_len <= 0))
  {
    kmyth_sgx_log(LOG_ERR, "error computing required buffer size");
    EVP_PKEY_CTX_free(ctx);
    return EXIT_FAILURE;
  }

  // allocate buffer to hold shared secret
  *shared_secret = calloc(*shared_secret_len, sizeof(unsigned char));
  if (*shared_secret == NULL)
  {
    kmyth_sgx_log(LOG_ERR, "error allocating buffer for shared secret");
    EVP_PKEY_CTX_free(ctx);
    return EXIT_FAILURE;
  }

  // derive the 'shared secret' value
  retval = EVP_PKEY_derive(ctx, *shared_secret, shared_secret_len);
  if (retval != 1)
  {
    kmyth_sgx_log(LOG_ERR, "error deriving shared secret value");
    kmyth_clear_and_free(*shared_secret, *shared_secret_len);
    *shared_secret_len = 0;
    EVP_PKEY_CTX_free(ctx);
    return EXIT_FAILURE;
  }

  EVP_PKEY_CTX_free(ctx);

  return EXIT_SUCCESS;
}

/*****************************************************************************
 * compute_ecdh_session_key()
 ****************************************************************************/
int compute_ecdh_session_key(unsigned char * secret_in_bytes,
                             size_t secret_in_len,
                             unsigned char * msg1_in_bytes,
                             size_t msg1_in_len,
                             unsigned char * msg2_in_bytes,
                             size_t msg2_in_len,
                             unsigned char ** key1_out_bytes,
                             size_t * key1_out_len,
                             unsigned char ** key2_out_bytes,
                             size_t * key2_out_len)
{
  if(secret_in_len > INT_MAX)
  {
    kmyth_sgx_log(LOG_ERR, "secret too long for HKDF context");
    return EXIT_FAILURE;
  }
  char msg[MAX_LOG_MSG_LEN] = { 0 };

  EVP_PKEY_CTX *pctx;

  pctx = EVP_PKEY_CTX_new_id(EVP_PKEY_HKDF, NULL);
  if (pctx == NULL)
  {
    kmyth_sgx_log(LOG_ERR, "error creating HKDF context");
    return EXIT_FAILURE;
  }


  // initialize HKDF context
  if (EVP_PKEY_derive_init(pctx) != 1)
  {
    kmyth_sgx_log(LOG_ERR, "failed to initialize HKDF context");
    EVP_PKEY_CTX_free(pctx);
    return EXIT_FAILURE;
  }

  // set message digest for HKDF
  if (EVP_PKEY_CTX_set_hkdf_md(pctx, KMYTH_ECDH_MD) != 1)
  {
    kmyth_sgx_log(LOG_ERR, "failed to set HKDF message digest");
    EVP_PKEY_CTX_free(pctx);
    return EXIT_FAILURE;
  }

  // set 'salt' value for HKDF
  if (EVP_PKEY_CTX_set1_hkdf_salt(pctx, "kmyth", 5) != 1)
  {
    kmyth_sgx_log(LOG_ERR, "failed to set HKDF 'salt' value");
    EVP_PKEY_CTX_free(pctx);
    return EXIT_FAILURE;
  }

  // set input key value for HKDF
  if (EVP_PKEY_CTX_set1_hkdf_key(pctx, secret_in_bytes, (int)secret_in_len) != 1)
  {
    kmyth_sgx_log(LOG_ERR, "failed to set HKDF input key bytes");
    EVP_PKEY_CTX_free(pctx);
    return EXIT_FAILURE;
  }

  // set additional information input for HKDF - use concatenated msg inputs
  size_t addl_info_len = msg1_in_len + msg2_in_len;
  if(addl_info_len > INT_MAX)
  {
    kmyth_sgx_log(LOG_ERR, "HKDF additional info too long");
    EVP_PKEY_CTX_free(pctx);
    return EXIT_FAILURE;
  }
  unsigned char *addl_info = calloc(addl_info_len, sizeof(unsigned char));
  memcpy(addl_info, msg1_in_bytes, msg1_in_len);
  memcpy(addl_info+msg1_in_len, msg2_in_bytes, msg2_in_len);
  if (EVP_PKEY_CTX_add1_hkdf_info(pctx, addl_info, (int)addl_info_len) != 1)
  {
    kmyth_sgx_log(LOG_ERR, "failed to set HKDF additional information input");
    free(addl_info);
    EVP_PKEY_CTX_free(pctx);
    return EXIT_FAILURE;
  }
  free(addl_info);

  // derive key bits
  unsigned char kdf_out[KMYTH_ECDH_KDF_OUTPUT_SIZE];
  size_t kdf_out_len = sizeof(kdf_out);

  if (EVP_PKEY_derive(pctx, kdf_out, &kdf_out_len) != 1)
  {
    kmyth_sgx_log(LOG_ERR, "HKDF extract and expand operation failed");
    EVP_PKEY_CTX_free(pctx);
    return EXIT_FAILURE;
  }

  snprintf(msg, MAX_LOG_MSG_LEN,
           "KDF Output: 0x%02X%02X...%02X%02X (%ld bytes)",
           kdf_out[0], kdf_out[1],
           kdf_out[kdf_out_len - 2],
           kdf_out[kdf_out_len - 1],
           kdf_out_len);
  kmyth_sgx_log(LOG_DEBUG, msg);

  EVP_PKEY_CTX_free(pctx);

  // assign first half of key bytes generated to first output session key
  *key1_out_len = KMYTH_ECDH_SESSION_KEY_SIZE;
  if ((2 * (*key1_out_len)) > kdf_out_len)
  {
    kmyth_sgx_log(LOG_ERR, "KDF configuration error");
    return EXIT_FAILURE;
  }

  *key1_out_bytes = calloc(*key1_out_len, sizeof(unsigned char));
  if (NULL == *key1_out_bytes)
  {
    kmyth_sgx_log(LOG_ERR, "failed to allocate buffer for session key #1");
    return EXIT_FAILURE;
  }
  memcpy(*key1_out_bytes, kdf_out, *key1_out_len);

  // assign second half of key bytes generated to second output session key
  *key2_out_len = *key1_out_len;
  *key2_out_bytes = calloc(*key2_out_len, sizeof(unsigned char));
  if (NULL == *key2_out_bytes)
  {
    kmyth_sgx_log(LOG_ERR, "failed to allocate buffer for session key #2");
    free(*key1_out_bytes);
    return EXIT_FAILURE;
  }
  memcpy(*key2_out_bytes, kdf_out+*key1_out_len, *key2_out_len);

  return EXIT_SUCCESS;
}

/*****************************************************************************
 * ec_sign_buffer()
 ****************************************************************************/
int ec_sign_buffer(EVP_PKEY * ec_sign_pkey,
                   unsigned char *buf_in, size_t buf_in_len,
                   unsigned char **sig_out, unsigned int *sig_out_len)
{
  // create message digest context
  EVP_MD_CTX *mdctx = EVP_MD_CTX_new();

  if (mdctx == NULL)
  {
    kmyth_sgx_log(LOG_ERR, "creation of message digest context failed");
    return EXIT_FAILURE;
  }

  // configure signing context
  if (EVP_SignInit(mdctx, KMYTH_ECDH_MD) != 1)
  {
    kmyth_sgx_log(LOG_ERR, "config of message digest signature context failed");
    EVP_MD_CTX_free(mdctx);
    return EXIT_FAILURE;
  }

  // hash data into the signature context
  if (EVP_SignUpdate(mdctx, buf_in, buf_in_len) != 1)
  {
    kmyth_sgx_log(LOG_ERR, "error hashing data into signature context");
    EVP_MD_CTX_free(mdctx);
    return EXIT_FAILURE;
  }

  // allocate memory for signature
  int max_sig_len = EVP_PKEY_size(ec_sign_pkey);

  if (max_sig_len <= 0)
  {
    kmyth_sgx_log(LOG_ERR, "invalid value for maximum signature length");
    EVP_MD_CTX_free(mdctx);
    return EXIT_FAILURE;
  }
  *sig_out = calloc((size_t)max_sig_len, sizeof(unsigned char));
  if (*sig_out == NULL)
  {
    kmyth_sgx_log(LOG_ERR, "malloc of signature buffer failed");
    EVP_MD_CTX_free(mdctx);
    return EXIT_FAILURE;
  }

  // sign the data (create signature)
  if (EVP_SignFinal(mdctx, *sig_out,
                    (unsigned int *) sig_out_len, ec_sign_pkey) != 1)
  {
    kmyth_sgx_log(LOG_ERR, "signature creation failed");
    free(*sig_out);
    EVP_MD_CTX_free(mdctx);
    return EXIT_FAILURE;
  }

  // done - clean-up context
  EVP_MD_CTX_free(mdctx);

  return EXIT_SUCCESS;
}

/*****************************************************************************
 * ec_verify_buffer()
 ****************************************************************************/
int ec_verify_buffer(EVP_PKEY * ec_verify_pkey,
                     unsigned char *buf_in, size_t buf_in_len,
                     unsigned char *sig_in, unsigned int sig_in_len)
{
  // create message digest context
  EVP_MD_CTX *mdctx = EVP_MD_CTX_new();

  if (mdctx == NULL)
  {
    kmyth_sgx_log(LOG_ERR, "creation of message digest context failed");
    return EXIT_FAILURE;
  }

  // 'initialize' (e.g., load public key)
  if (EVP_DigestVerifyInit(mdctx, NULL, KMYTH_ECDH_MD, NULL,
                                        ec_verify_pkey) != 1)
  {
    kmyth_sgx_log(LOG_ERR, "initialization of message digest context failed");
    EVP_MD_CTX_free(mdctx);
    return EXIT_FAILURE;
  }

  // 'update' with signed data
  if (EVP_DigestVerifyUpdate(mdctx, buf_in, buf_in_len) != 1)
  {
    kmyth_sgx_log(LOG_ERR,
                  "message digest context update with signed data failed");
    EVP_MD_CTX_free(mdctx);
    return EXIT_FAILURE;
  }

  // check signature
  unsigned char *sig_ptr = sig_in;

  if (EVP_DigestVerifyFinal(mdctx, sig_ptr, sig_in_len) != 1)
  {
    kmyth_sgx_log(LOG_ERR, "signature verification failed");
    EVP_MD_CTX_free(mdctx);
    return EXIT_FAILURE;
  }

  // done - clean-up context
  EVP_MD_CTX_free(mdctx);

  return EXIT_SUCCESS;
}
