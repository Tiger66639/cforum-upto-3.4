/**
 * \file flt_cgiconfig.c
 * \author Christian Kruse, <cjk@wwwtech.de>
 *
 * This filter can changes several config options by cgi
 * parameters
 *
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
#include <time.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/file.h>

#include <db.h>

#include "readline.h"
#include "hashlib.h"
#include "utils.h"
#include "cfgcomp.h"
#include "cfcgi.h"
#include "template.h"
#include "clientlib.h"
/* }}} */

/* {{{ flt_cgiconfig_post */
int flt_cgiconfig_post(cf_hash_t *head,cf_configuration_t *cfg,cf_cl_thread_t *thread,cf_template_t *tpl) {
  u_char *UserName = cf_hash_get(GlobalValues,"UserName",8);
  u_char *link;
  cf_string_t *tmp = NULL;

  size_t l;
  int x = UserName ? 1 : 0;

  cf_cfg_config_value_t *cs = cf_cfg_get_value(cfg,"DF:ExternCharset");
  cf_readmode_t *rm = cf_hash_get(GlobalValues,"RM",2);

  cf_cfg_config_value_t *threaded_uri = cf_cfg_get_value(cfg,"DF:PostingURL");
  cf_cfg_config_value_t *list_uri = cf_cfg_get_first_value(cfg,"DF:PostingURL_List");
  cf_cfg_config_value_t *nested_uri = cf_cfg_get_first_value(cfg,"DF:PostingURL_Nested");

  /* {{{ ShowThread links */
  if((tmp = cf_cgi_get(head,"showthread")) != NULL) cf_remove_static_uri_flag("showthread");

  link = cf_advanced_get_link(rm->posting_uri[x],thread->tid,thread->threadmsg->mid,NULL,1,&l,"showthread","part");
  cf_set_variable(tpl,cs,"showthread_part",link,l,1);
  free(link);

  link = cf_advanced_get_link(rm->posting_uri[x],thread->tid,thread->threadmsg->mid,NULL,1,&l,"showthread","none");
  cf_set_variable(tpl,cs,"showthread_none",link,l,1);
  free(link);

  link = cf_advanced_get_link(rm->posting_uri[x],thread->tid,thread->threadmsg->mid,NULL,1,&l,"showthread","full");
  cf_set_variable(tpl,cs,"showthread_full",link,l,1);
  free(link);

  if(tmp) cf_add_static_uri_flag("showthread",tmp->content,0);
  /* }}} */

  /* {{{ ReadMode links */
  if((tmp = cf_cgi_get(head,"readmode")) != NULL) cf_remove_static_uri_flag("readmode");

  link = cf_advanced_get_link(list_uri->avals[x].sval,thread->tid,thread->threadmsg->mid,NULL,1,&l,"readmode","list");
  cf_set_variable(tpl,cs,"readmode_list",link,l,1);
  free(link);

  link = cf_advanced_get_link(nested_uri->avals[x].sval,thread->tid,thread->threadmsg->mid,NULL,1,&l,"readmode","nested");
  cf_set_variable(tpl,cs,"readmode_nested",link,l,1);
  free(link);

  link = cf_advanced_get_link(threaded_uri->avals[x].sval,thread->tid,thread->threadmsg->mid,NULL,1,&l,"readmode","thread");
  cf_set_variable(tpl,cs,"readmode_thread",link,l,1);
  free(link);

  if(tmp) cf_add_static_uri_flag("readmode",tmp->content,0);
  /* }}} */

  return FLT_OK;
}
/* }}} */

/* {{{ flt_cgiconfig_init_handler */
int flt_cgiconfig_init_handler(cf_hash_t *head,cf_configuration_t *cfg) {
  cf_cfg_config_value_t *v;
  cf_string_t *val;

  if(head) {
    if((val = cf_cgi_get(head,"showthread")) != NULL) {
      v = cf_cfg_get_value(cfg,"FV:ShowThread");

      if(cf_strcmp(val->content,"part") == 0) {
        free(v->sval);
        v->sval = strdup("partitial");
      }
      else if(cf_strcmp(val->content,"none") == 0) {
        free(v->sval);
        v->sval = strdup("none");
      }
      else if(cf_strcmp(val->content,"full") == 0) {
        free(v->sval);
        v->sval = strdup("full");
      }

      cf_add_static_uri_flag("showthread",val->content,0);
    }

    if((val = cf_cgi_get(head,"readmode")) != NULL) {
      v = cf_cfg_get_value(cfg,"DF:ReadMode");

      if(cf_strcmp(val->content,"list") == 0) {
        free(v->sval);
        v->sval = strdup("list");
      }
      else if(cf_strcmp(val->content,"nested") == 0) {
        free(v->sval);
        v->sval = strdup("nested");
      }
      else if(cf_strcmp(val->content,"thread") == 0) {
        free(v->sval);
        v->sval = strdup("thread");
      }

      cf_add_static_uri_flag("readmode",val->content,0);
    }
  }

  return FLT_DECLINE;
}
/* }}} */

cf_handler_config_t flt_cgiconfig_handlers[] = {
  { POSTING_HANDLER,      flt_cgiconfig_post },
  { INIT_HANDLER,         flt_cgiconfig_init_handler },
  { 0, NULL }
};

cf_module_config_t flt_cgiconfig = {
  MODULE_MAGIC_COOKIE,
  flt_cgiconfig_handlers,
  NULL,
  NULL,
  NULL,
  NULL,
  NULL
};



/* eof */
