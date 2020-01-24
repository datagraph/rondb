/* Copyright (c) 2012, 2020, Oracle and/or its affiliates. All rights reserved.

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License, version 2.0,
   as published by the Free Software Foundation.

   This program is also distributed with certain software (including
   but not limited to OpenSSL) that is licensed under separate terms,
   as designated in a particular file or component or in included license
   documentation.  The authors of MySQL hereby grant you an additional
   permission to link the program and your derivative works with the
   separately licensed software that they have included with MySQL.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License, version 2.0, for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301  USA */

#ifndef _WIN32
#include <netdb.h>
#endif
#include <stdlib.h>

#include "xcom/node_no.h"
#include "xcom/server_struct.h"
#include "xcom/simset.h"
#include "xcom/site_struct.h"
#include "xcom/task.h"
#include "xcom/x_platform.h"
#include "xcom/xcom_detector.h"
#include "xcom/xcom_profile.h"
#include "xdr_gen/xcom_vp.h"

#ifdef _WIN32
#include "xcom/sock_probe_win32.c"
#else
#include "xcom/sock_probe_ix.c"
#endif

/* compare two sockaddr */
static bool_t sockaddr_default_eq(struct sockaddr *x, struct sockaddr *y) {
  size_t size_to_compare;
  if (x->sa_family != y->sa_family) return 0;

  size_to_compare = x->sa_family == AF_INET ? sizeof(struct sockaddr_in)
                                            : sizeof(struct sockaddr_in6);

  return 0 == memcmp(x, y, size_to_compare);
}

/* return index of this machine in node list, or -1 if no match */

static port_matcher match_port;
void set_port_matcher(port_matcher x) { match_port = x; }

port_matcher get_port_matcher() { return match_port; }

static inline struct addrinfo *probe_get_addrinfo(char *name) {
#ifdef XCOM_STANDALONE
  return xcom_caching_getaddrinfo(name);
#else
  {
    struct addrinfo *addr = 0;
    checked_getaddrinfo(name, 0, 0, &addr);
    return addr;
  }
#endif
}

static inline void probe_free_addrinfo(struct addrinfo *addr) {
#ifdef XCOM_STANDALONE
  (void)addr;
#else
  if (addr) freeaddrinfo(addr);
#endif
}

node_no xcom_find_node_index(node_list *nodes) {
  node_no i;
  node_no retval = VOID_NODE_NO;
  char name[IP_MAX_SIZE];
  xcom_port port = 0;
  struct addrinfo *addr = 0;
  struct addrinfo *saved_addr = 0;

  sock_probe *s = (sock_probe *)calloc((size_t)1, sizeof(sock_probe));

  if (init_sock_probe(s) < 0) {
    free(s);
    return retval;
  }

  /* For each node in list */
  for (i = 0; i < nodes->node_list_len; i++) {
    /* Get host name from host:port string */
    if (get_ip_and_port(nodes->node_list_val[i].address, name, &port)) {
      G_DEBUG("Error parsing IP and Port. Passing to the next node.");
      continue;
    }

    /* See if port matches first */
    if (match_port) {
      if (!match_port(port)) {
        continue;
      }

      /* Get addresses of host */

      saved_addr = addr = probe_get_addrinfo(name);
      IFDBG(D_NONE, FN; STRLIT("name "); STRLIT(name); PTREXP(addr));
      /* getaddrinfo returns linked list of addrinfo */
      while (addr) {
        int j;
        /* Match sockaddr of host with list of interfaces on this machine. Skip
         * disabled interfaces */
        for (j = 0; j < number_of_interfaces(s); j++) {
          struct sockaddr *tmp_sockaddr = NULL;
          get_sockaddr_address(s, j, &tmp_sockaddr);
          if (sockaddr_default_eq(addr->ai_addr, tmp_sockaddr) &&
              is_if_running(s, j)) {
            retval = i;
            goto end_loop;
          }
        }
        addr = addr->ai_next;
      }
      probe_free_addrinfo(saved_addr);
      saved_addr = 0;
    }
  }
/* Free resources and return result */
end_loop:
  probe_free_addrinfo(saved_addr);
  close_sock_probe(s);
  return retval;
}

node_no xcom_mynode_match(char *name, xcom_port port) {
  node_no retval = 0;
  struct addrinfo *addr = 0;
  struct addrinfo *saved_addr = 0;

  if (match_port && !match_port(port)) return 0;

  {
    sock_probe *s = (sock_probe *)calloc((size_t)1, sizeof(sock_probe));
    if (init_sock_probe(s) < 0) {
      free(s);
      return retval;
    }

    saved_addr = addr = probe_get_addrinfo(name);
    IFDBG(D_NONE, FN; STREXP(name); PTREXP(addr));
    /* getaddrinfo returns linked list of addrinfo */
    while (addr) {
      int j;
      /* Match sockaddr of host with list of interfaces on this machine.
       * Skip disabled interfaces */
      for (j = 0; j < number_of_interfaces(s); j++) {
        struct sockaddr *tmp_sockaddr = NULL;
        get_sockaddr_address(s, j, &tmp_sockaddr);
        if (sockaddr_default_eq(addr->ai_addr, tmp_sockaddr) &&
            is_if_running(s, j)) {
          retval = 1;
          goto end_loop;
        }
      }
      addr = addr->ai_next;
    }
  /* Free resources and return result */
  end_loop:
    probe_free_addrinfo(saved_addr);
    close_sock_probe(s);
  }
  return retval;
}
