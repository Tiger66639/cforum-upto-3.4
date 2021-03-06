/**
 * \file flt_voting.c
 * \author Christian Kruse, <cjk@wwwtech.de>
 *
 * Implementation of the voting protocol stack
 */

/* {{{ Initial comments */
/*
 * $LastChangedDate$
 * $LastChangedRevision$
 * $LastChangedBy$
 *
 */
/* }}} */

/* {{{ Includes */
#include "cfconfig.h"
#include "defines.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <time.h>

#include <pthread.h>

#include <sys/stat.h>
#include <sys/types.h>

struct sockaddr_un;

#include "cf_pthread.h"

#include "hashlib.h"
#include "utils.h"
#include "cfgcomp.h"
#include "readline.h"

#include "serverutils.h"
#include "serverlib.h"
#include "fo_server.h"
/* }}} */

/* {{{ flt_voting_handler */
int flt_voting_handler(int sockfd,forum_t *forum,const u_char **tokens,int tnum,rline_t *tsd) {
  cf_hash_t *infos;
  u_char *ln = NULL,*tmp,*ctid,*cmid;
  u_int64_t tid,mid;
  int err = 0;
  thread_t *t;
  posting_t *p;
  long llen;
  cf_string_t str;

  if(tnum != 2) return FLT_DECLINE;

  infos = cf_hash_new(NULL);

  do {
    ln  = readline(sockfd,tsd);

    if(ln) {
      llen = tsd->rl_len;
      while(ln[llen] != '\n') ln[llen--] = '\0'; /* delete the \n */
      ln[llen--] = '\0';

      tmp = strstr(ln,":");

      if(tmp) {
        cf_hash_set(infos,ln,tmp-ln,tmp+2,llen-(int)(tmp-ln));
      }
      else {
        if(*ln == '\0') break;

        free(ln);
        ln = NULL;
        writen(sockfd,"503 Bad request\n",16);
        err = 1;
        break;
      }

      free(ln);
    }
  } while(ln);

  if(err == 0) {
    if(cf_strcmp(tokens[1],"GOOD") == 0) {
      ctid = cf_hash_get(infos,"Tid",3);
      cmid = cf_hash_get(infos,"Mid",3);

      if(!ctid || !cmid) {
        writen(sockfd,"503 Bad request\n",16);
        cf_hash_destroy(infos);
        return FLT_OK;
      }

      tid = cf_str_to_uint64(ctid);
      mid = cf_str_to_uint64(cmid);

      cf_hash_destroy(infos);

      /* {{{ get thread and posting */
      if((t = cf_get_thread(forum,tid)) == NULL) {
        writen(sockfd,"404 Thread Not Found\n",21);
        return FLT_OK;
      }

      if((p = cf_get_posting(t,mid)) == NULL) {
        writen(sockfd,"404 Posting Not Found\n",22);
        return FLT_OK;
      }
      /* }}} */

      cf_str_init_growth(&str,128);
      cf_str_char_set(&str,"200 Ok\nNum: ",12);

      CF_RW_WR(&t->lock);
      cf_uint32_to_str(&str,++p->votes_good);
      CF_RW_UN(&t->lock);

      cf_str_char_append(&str,'\n');
      writen(sockfd,str.content,str.len);
      cf_str_cleanup(&str);
      cf_generate_cache(forum);
    }
    else if(cf_strcmp(tokens[1],"BAD") == 0) {
      ctid = cf_hash_get(infos,"Tid",3);
      cmid = cf_hash_get(infos,"Mid",3);

      if(!ctid || !cmid) {
        writen(sockfd,"503 Bad request\n",16);
        cf_hash_destroy(infos);
        return FLT_OK;
      }

      tid = cf_str_to_uint64(ctid);
      mid = cf_str_to_uint64(cmid);

      cf_hash_destroy(infos);

      /* {{{ get thread and posting */
      if((t = cf_get_thread(forum,tid)) == NULL) {
        writen(sockfd,"404 Thread Not Found\n",21);
        return FLT_OK;
      }

      if((p = cf_get_posting(t,mid)) == NULL) {
        writen(sockfd,"404 Posting Not Found\n",22);
        return FLT_OK;
      }
      /* }}} */

      cf_str_init_growth(&str,128);
      cf_str_char_set(&str,"200 Ok\nNum: ",12);

      CF_RW_WR(&t->lock);
      cf_uint32_to_str(&str,++p->votes_bad);
      CF_RW_UN(&t->lock);

      cf_str_char_append(&str,'\n');
      writen(sockfd,str.content,str.len);
      cf_str_cleanup(&str);
      cf_generate_cache(forum);
    }
    else {
      cf_hash_destroy(infos);
      return FLT_DECLINE;
    }

    return FLT_OK;
  }

  cf_hash_destroy(infos);
  return FLT_OK;
}
/* }}} */

int flt_voting_register_handlers(int sock) {
  cf_register_protocol_handler("VOTE",flt_voting_handler);
  return FLT_OK;
}

cf_conf_opt_t flt_voting_config[] = {
  { NULL, NULL, 0, NULL }
};

cf_handler_config_t flt_voting_handlers[] = {
  { INIT_HANDLER, flt_voting_register_handlers },
  { 0, NULL }
};

cf_module_config_t flt_voting = {
  MODULE_MAGIC_COOKIE,
  flt_voting_config,
  flt_voting_handlers,
  NULL,
  NULL,
  NULL,
  NULL,
  NULL
};

/* eof */
