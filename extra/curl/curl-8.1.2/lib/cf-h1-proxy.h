#ifndef HEADER_CURL_H1_PROXY_H
#define HEADER_CURL_H1_PROXY_H
/***************************************************************************
 *                                  _   _ ____  _
 *  Project                     ___| | | |  _ \| |
 *                             / __| | | | |_) | |
 *                            | (__| |_| |  _ <| |___
 *                             \___|\___/|_| \_\_____|
 *
 * Copyright (C) Daniel Stenberg, <daniel@haxx.se>, et al.
 *
 * This software is licensed as described in the file COPYING, which
 * you should have received as part of this distribution. The terms
 * are also available at https://curl.se/docs/copyright.html.
 *
 * You may opt to use, copy, modify, merge, publish, distribute and/or sell
 * copies of the Software, and permit persons to whom the Software is
 * furnished to do so, under the terms of the COPYING file.
 *
 * This software is distributed on an "AS IS" basis, WITHOUT WARRANTY OF ANY
 * KIND, either express or implied.
 *
 * SPDX-License-Identifier: curl
 *
 ***************************************************************************/

#include "curl_setup.h"

<<<<<<<< HEAD:extra/curl/curl-7.88.1/lib/vquic/vquic.h
#ifdef ENABLE_QUIC
struct Curl_cfilter;
struct Curl_easy;
struct connectdata;
struct Curl_addrinfo;

void Curl_quic_ver(char *p, size_t len);

CURLcode Curl_qlogdir(struct Curl_easy *data,
                      unsigned char *scid,
                      size_t scidlen,
                      int *qlogfdp);


CURLcode Curl_cf_quic_create(struct Curl_cfilter **pcf,
                             struct Curl_easy *data,
                             struct connectdata *conn,
                             const struct Curl_addrinfo *ai,
                             int transport);

bool Curl_conn_is_http3(const struct Curl_easy *data,
                        const struct connectdata *conn,
                        int sockindex);

extern struct Curl_cftype Curl_cft_http3;

#else /* ENABLE_QUIC */

#define Curl_conn_is_http3(a,b,c)   FALSE

#endif /* !ENABLE_QUIC */

CURLcode Curl_conn_may_http3(struct Curl_easy *data,
                             const struct connectdata *conn);
========
#if !defined(CURL_DISABLE_PROXY) && !defined(CURL_DISABLE_HTTP)
>>>>>>>> 057f5c9509c6c9ea3ce3acdc619f3353c09e6ec6:extra/curl/curl-8.1.2/lib/cf-h1-proxy.h

CURLcode Curl_cf_h1_proxy_insert_after(struct Curl_cfilter *cf,
                                       struct Curl_easy *data);

extern struct Curl_cftype Curl_cft_h1_proxy;


#endif /* !CURL_DISABLE_PROXY && !CURL_DISABLE_HTTP */

#endif /* HEADER_CURL_H1_PROXY_H */
