{
	global:
		eddsa_pk_free;
		eddsa_pk_from_EVP_PKEY;
		eddsa_pk_from_ptr;
		eddsa_pk_new;
		eddsa_pk_to_EVP_PKEY;
		es256_pk_free;
		es256_pk_from_EC_KEY;
		es256_pk_from_ptr;
		es256_pk_new;
		es256_pk_to_EVP_PKEY;
		fido_assert_allow_cred;
		fido_assert_authdata_len;
		fido_assert_authdata_ptr;
		fido_assert_blob_len;
		fido_assert_blob_ptr;
		fido_assert_clientdata_hash_len;
		fido_assert_clientdata_hash_ptr;
		fido_assert_count;
		fido_assert_flags;
		fido_assert_free;
		fido_assert_hmac_secret_len;
		fido_assert_hmac_secret_ptr;
		fido_assert_id_len;
		fido_assert_id_ptr;
		fido_assert_largeblob_key_len;
		fido_assert_largeblob_key_ptr;
		fido_assert_new;
		fido_assert_rp_id;
		fido_assert_set_authdata;
		fido_assert_set_authdata_raw;
		fido_assert_set_clientdata;
		fido_assert_set_clientdata_hash;
		fido_assert_set_count;
		fido_assert_set_extensions;
		fido_assert_set_hmac_salt;
		fido_assert_set_hmac_secret;
		fido_assert_set_options;
		fido_assert_set_rp;
		fido_assert_set_sig;
		fido_assert_set_up;
		fido_assert_set_uv;
		fido_assert_sigcount;
		fido_assert_sig_len;
		fido_assert_sig_ptr;
		fido_assert_user_display_name;
		fido_assert_user_icon;
		fido_assert_user_id_len;
		fido_assert_user_id_ptr;
		fido_assert_user_name;
		fido_assert_verify;
		fido_bio_dev_enroll_begin;
		fido_bio_dev_enroll_cancel;
		fido_bio_dev_enroll_continue;
		fido_bio_dev_enroll_remove;
		fido_bio_dev_get_info;
		fido_bio_dev_get_template_array;
		fido_bio_dev_set_template_name;
		fido_bio_enroll_free;
		fido_bio_enroll_last_status;
		fido_bio_enroll_new;
		fido_bio_enroll_remaining_samples;
		fido_bio_info_free;
		fido_bio_info_max_samples;
		fido_bio_info_new;
		fido_bio_info_type;
		fido_bio_template;
		fido_bio_template_array_count;
		fido_bio_template_array_free;
		fido_bio_template_array_new;
		fido_bio_template_free;
		fido_bio_template_id_len;
		fido_bio_template_id_ptr;
		fido_bio_template_name;
		fido_bio_template_new;
		fido_bio_template_set_id;
		fido_bio_template_set_name;
		fido_cbor_info_aaguid_len;
		fido_cbor_info_aaguid_ptr;
		fido_cbor_info_algorithm_cose;
		fido_cbor_info_algorithm_count;
		fido_cbor_info_algorithm_type;
		fido_cbor_info_extensions_len;
		fido_cbor_info_extensions_ptr;
		fido_cbor_info_free;
		fido_cbor_info_maxmsgsiz;
		fido_cbor_info_maxcredbloblen;
		fido_cbor_info_maxcredcntlst;
		fido_cbor_info_maxcredidlen;
		fido_cbor_info_fwversion;
		fido_cbor_info_new;
		fido_cbor_info_options_len;
		fido_cbor_info_options_name_ptr;
		fido_cbor_info_options_value_ptr;
		fido_cbor_info_protocols_len;
		fido_cbor_info_protocols_ptr;
		fido_cbor_info_transports_len;
		fido_cbor_info_transports_ptr;
		fido_cbor_info_versions_len;
		fido_cbor_info_versions_ptr;
		fido_cred_authdata_len;
		fido_cred_authdata_ptr;
		fido_cred_authdata_raw_len;
		fido_cred_authdata_raw_ptr;
		fido_cred_clientdata_hash_len;
		fido_cred_clientdata_hash_ptr;
		fido_cred_display_name;
		fido_cred_exclude;
		fido_cred_flags;
		fido_cred_largeblob_key_len;
		fido_cred_largeblob_key_ptr;
		fido_cred_sigcount;
		fido_cred_fmt;
		fido_cred_free;
		fido_cred_id_len;
		fido_cred_id_ptr;
		fido_cred_aaguid_len;
		fido_cred_aaguid_ptr;
		fido_credman_del_dev_rk;
		fido_credman_get_dev_metadata;
		fido_credman_get_dev_rk;
		fido_credman_get_dev_rp;
		fido_credman_metadata_free;
		fido_credman_metadata_new;
		fido_credman_rk;
		fido_credman_rk_count;
		fido_credman_rk_existing;
		fido_credman_rk_free;
		fido_credman_rk_new;
		fido_credman_rk_remaining;
		fido_credman_rp_count;
		fido_credman_rp_free;
		fido_credman_rp_id;
		fido_credman_rp_id_hash_len;
		fido_credman_rp_id_hash_ptr;
		fido_credman_rp_name;
		fido_credman_rp_new;
		fido_credman_set_dev_rk;
		fido_cred_new;
		fido_cred_prot;
		fido_cred_pubkey_len;
		fido_cred_pubkey_ptr;
		fido_cred_rp_id;
		fido_cred_rp_name;
		fido_cred_set_authdata;
		fido_cred_set_authdata_raw;
		fido_cred_set_blob;
		fido_cred_set_clientdata;
		fido_cred_set_clientdata_hash;
		fido_cred_set_extensions;
		fido_cred_set_fmt;
		fido_cred_set_id;
		fido_cred_set_options;
		fido_cred_set_prot;
		fido_cred_set_rk;
		fido_cred_set_rp;
		fido_cred_set_sig;
		fido_cred_set_type;
		fido_cred_set_user;
		fido_cred_set_uv;
		fido_cred_set_x509;
		fido_cred_sig_len;
		fido_cred_sig_ptr;
		fido_cred_type;
		fido_cred_user_id_len;
		fido_cred_user_id_ptr;
		fido_cred_user_name;
		fido_cred_verify;
		fido_cred_verify_self;
		fido_cred_x5c_len;
		fido_cred_x5c_ptr;
		fido_dev_build;
		fido_dev_cancel;
		fido_dev_close;
		fido_dev_enable_entattest;
		fido_dev_flags;
		fido_dev_force_fido2;
		fido_dev_force_pin_change;
		fido_dev_force_u2f;
		fido_dev_free;
		fido_dev_get_assert;
		fido_dev_get_cbor_info;
		fido_dev_get_retry_count;
		fido_dev_get_uv_retry_count;
		fido_dev_get_touch_begin;
		fido_dev_get_touch_status;
		fido_dev_has_pin;
		fido_dev_has_uv;
		fido_dev_info_free;
		fido_dev_info_manifest;
		fido_dev_info_manufacturer_string;
		fido_dev_info_new;
		fido_dev_info_path;
		fido_dev_info_product;
		fido_dev_info_product_string;
		fido_dev_info_ptr;
		fido_dev_info_vendor;
		fido_dev_is_fido2;
		fido_dev_is_winhello;
		fido_dev_major;
		fido_dev_make_cred;
		fido_dev_minor;
		fido_dev_new;
		fido_dev_open;
		fido_dev_protocol;
		fido_dev_reset;
		fido_dev_set_io_functions;
		fido_dev_set_pin;
		fido_dev_set_pin_minlen;
		fido_dev_set_sigmask;
		fido_dev_set_transport_functions;
		fido_dev_supports_cred_prot;
		fido_dev_supports_credman;
		fido_dev_supports_permissions;
		fido_dev_supports_pin;
		fido_dev_supports_uv;
		fido_dev_toggle_always_uv;
		fido_dev_largeblob_get;
		fido_dev_largeblob_get_array;
		fido_dev_largeblob_remove;
		fido_dev_largeblob_set;
		fido_dev_largeblob_set_array;
		fido_init;
		fido_set_log_handler;
		fido_strerr;
		rs256_pk_free;
		rs256_pk_from_ptr;
		rs256_pk_from_RSA;
		rs256_pk_new;
		rs256_pk_to_EVP_PKEY;
	local:
		*;
};
