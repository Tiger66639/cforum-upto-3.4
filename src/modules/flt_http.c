/**
 * \file flt_http.c
 * \author Christian Kruse, <cjk@wwwtech.de>
 *
 * This plugin sends enhanced HTTP headers and handles If-Modified-Since
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

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/time.h>

#include "hashlib.h"
#include "readline.h"
#include "utils.h"
#include "cfgcomp.h"
#include "cfcgi.h"
#include "template.h"
#include "clientlib.h"

/* }}} */

struct {
  int send_last_modified;
  long send_expires;
  int expires_set;
  int handle_last_modified_since;
} http_config = { 0, 0, 0, 0 };

static u_char *flt_http_fn = NULL;

/* {{{ flt_http_gmt_diff */
time_t flt_http_gmt_diff(void) {
  struct timeval now;
  time_t t1, t2;
  struct tm t;

  gettimeofday(&now, NULL);
  t1 = now.tv_sec;
  t2 = 0;

  t = *gmtime(&t1);

  t.tm_isdst = 0; /* we know this GMT time isn't daylight-savings */
  t2 = mktime(&t);
  return (time_t)difftime(t1, t2);
}
/* }}} */

/* {{{ flt_http_get_month */
int flt_http_get_month(const u_char *m) {
  if(cf_strncmp(m,"Jan",3) == 0) return 0;
  else if(cf_strncmp(m,"Feb",3) == 0) return 1;
  else if(cf_strncmp(m,"Mar",3) == 0) return 2;
  else if(cf_strncmp(m,"Apr",3) == 0) return 3;
  else if(cf_strncmp(m,"May",3) == 0) return 4;
  else if(cf_strncmp(m,"Jun",3) == 0) return 5;
  else if(cf_strncmp(m,"Jul",3) == 0) return 6;
  else if(cf_strncmp(m,"Aug",3) == 0) return 7;
  else if(cf_strncmp(m,"Sep",3) == 0) return 8;
  else if(cf_strncmp(m,"Oct",3) == 0) return 9;
  else if(cf_strncmp(m,"Nov",3) == 0) return 10;
  else if(cf_strncmp(m,"Dec",3) == 0) return 11;

  return -1;
}
/* }}} */

/* {{{ flt_http_get_lm */
#ifndef CF_SHARED_MEM
long flt_http_get_lm(int sock) {
  rline_t tsd;
  u_char *line,buff[512];
  long ret = 0;
  size_t len;
  u_char *forum_name = cf_hash_get(GlobalValues,"FORUM_NAME",10);

  memset(&tsd,0,sizeof(tsd));

  len = snprintf(buff,512,"SELECT %s\n",forum_name);
  writen(sock,buff,len);

  line = readline(sock,&tsd);
  if(!line || ((ret = atoi(line)) != 200)) {
    if(line) free(line);
    return 0;
  }

  ret = 0;
  free(line);

  if(writen(sock,"GET LASTMODIFIED 0\n",19) > 0) {
    if((line = readline(sock,&tsd)) != NULL) {
      ret = strtol(line,NULL,0);
      free(line);
    }
  }

  return ret;
}
#else
long flt_http_get_lm(void *ptr) {
  return *((time_t *)ptr);
}
#endif
/* }}} */

/* {{{ flt_http_validate_cache */
#ifndef CF_SHARED_MEM
int flt_http_validate_cache(cf_hash_t *head,cf_configuration_t *dc,cf_configuration_t *vc,time_t lm,int sock) {
#else
int flt_http_validate_cache(cf_hash_t *head,cf_configuration_t *dc,cf_configuration_t *vc,time_t lm,void *sock) {
#endif
  cf_module_t *mod;
  cf_cache_revalidator_t fkt;
  size_t i;
  int ret = FLT_DECLINE;

  if(Modules[0].elements) {
    for(i=0;i<Modules[0].elements && (ret == FLT_OK || ret == FLT_DECLINE);i++) {
      mod = cf_array_element_at(&Modules[0],i);

      if(mod->cfg->revalidator) {
        fkt     = (cf_cache_revalidator_t)mod->cfg->revalidator;
        ret     = fkt(head,dc,vc,lm,sock);
      }
    }
  }

  return ret;
}
/* }}} */

/* {{{ flt_http_header_callbacks */
#ifndef CF_SHARED_MEM
int flt_http_header_callbacks(cf_hash_t *head,cf_hash_t *header_table,cf_configuration_t *dc,cf_configuration_t *vc,int sock) {
#else
int flt_http_header_callbacks(cf_hash_t *head,cf_hash_t *header_table,cf_configuration_t *dc,cf_configuration_t *vc,void *sock) {
#endif
  cf_module_t *mod;
  cf_header_hook_t fkt;
  size_t i;
  int ret = FLT_DECLINE;
  int retret = FLT_OK;

  if(Modules[0].elements) {
    for(i=0;i<Modules[0].elements;i++) {
      mod = cf_array_element_at(&Modules[0],i);
      if(mod->cfg->header_hook) {
        fkt = (cf_header_hook_t)mod->cfg->header_hook;
        ret = fkt(head,header_table,dc,vc,sock);

        if(ret == FLT_EXIT) retret = FLT_EXIT;
      }
    }
  }

  return retret;
}
/* }}} */

/* {{{ flt_http_lm_callbacks */
#ifndef CF_SHARED_MEM
time_t flt_http_lm_callbacks(cf_hash_t *head,cf_configuration_t *dc,cf_configuration_t *vc,int sock,time_t t1) {
#else
time_t flt_http_lm_callbacks(cf_hash_t *head,cf_configuration_t *dc,cf_configuration_t *vc,void *sock,time_t t1) {
#endif
  cf_module_t *mod;
  cf_last_modified_t fkt;
  size_t i;
  time_t ret;

  if(Modules[0].elements) {
    for(i=0;i<Modules[0].elements;i++) {
      mod = cf_array_element_at(&Modules[0],i);
      if(mod->cfg->last_modified) {
        fkt = (cf_last_modified_t)mod->cfg->last_modified;
        ret = fkt(head,dc,vc,sock);

        if(ret == (time_t)-1) continue;
        if(ret > t1) {
          #ifdef DEBUG
          printf("X-Debug: got it from %zu\n",i);
          #endif
          t1 = ret;
        }
      }
    }
  }

  return t1;
}
/* }}} */

/* {{{ flt_http_execute */
#ifndef CF_SHARED_MEM
int flt_http_execute(cf_hash_t *head,cf_configuration_t *dc,cf_configuration_t *vc,int sock)
#else
int flt_http_execute(cf_hash_t *head,cf_configuration_t *dc,cf_configuration_t *vc,void *sock)
#endif
{
  u_char buff[100];
  time_t t,t1;
  struct tm *tm;
  struct tm tm_lm;
  u_char *lm;
  int ret;
  cf_hash_t *header_table = cf_hash_new(NULL);
  cf_hash_keylist_t *key;

  /* {{{ http header management */
  ret = flt_http_header_callbacks(head,header_table,dc,vc,sock);

  cf_hash_entry_delete(header_table,"Last-Modified",14);

  for(key=header_table->keys.elems;key;key=key->next) {
    printf("%s: %s\015\012",key->key,(char *)cf_hash_get(header_table,key->key,strlen(key->key)+1));
  }

  cf_hash_destroy(header_table);

  if(ret == FLT_EXIT) return FLT_EXIT;
  /* }}} */

  if(http_config.handle_last_modified_since || http_config.send_last_modified) {
    t1 = flt_http_get_lm(sock);
    t1 = flt_http_lm_callbacks(head,dc,vc,sock,t1);
    tm = gmtime(&t1);

    if(http_config.send_last_modified) {
      strftime(buff,100,"%a, %d %b %Y %H:%M:%S GMT",tm);
      printf("Last-Modified: %s\015\012",buff);
    }

    if(http_config.handle_last_modified_since) {
      if((lm = getenv("HTTP_IF_MODIFIED_SINCE")) != NULL && *lm != '\0') {
        tm_lm.tm_mday  = atoi(lm+5);
        tm_lm.tm_mon   = flt_http_get_month(lm+8);
        tm_lm.tm_year  = atoi(lm+12) - 1900;
        tm_lm.tm_wday  = 0;
        tm_lm.tm_hour  = atoi(lm+17);
        tm_lm.tm_min   = atoi(lm+20);
        tm_lm.tm_sec   = atoi(lm+23);

        tm_lm.tm_yday  = 0;
        tm_lm.tm_isdst = 0;

        if((t = timegm(&tm_lm)) >= 0) {
          #ifdef DEBUG
          printf("X-Debug: last modified from forum: %ld, last modified from browser: %ld\n",t1,t);
          printf("X-Debug: forum: %s",ctime(&t1));
          printf("X-Debug: browser: %s",ctime(&t));
          #endif

          if(t >= t1) {
            ret = flt_http_validate_cache(head,dc,vc,t,sock);

            if(ret != FLT_EXIT) {
              printf("Status: 304 Not Modified\015\012\015\012");
              return FLT_EXIT;
            }
          }
        }
      }
    }
  }

  return FLT_DECLINE;
}
/* }}} */

/* {{{ flt_http_handle_command */
int flt_http_handle_command(cf_configfile_t *cfile,cf_conf_opt_t *opt,const u_char *context,u_char **args,size_t argnum) {
  u_char *ptr = NULL;

  if(!flt_http_fn) flt_http_fn = cf_hash_get(GlobalValues,"FORUM_NAME",10);
  if(!context || cf_strcmp(flt_http_fn,context) != 0) return 0;

  if(argnum == 1) {
    if(cf_strcmp(opt->name,"Http:SendLastModified") == 0) {
      http_config.send_last_modified = cf_strcasecmp(args[0],"yes") == 0;
    }
    else if(cf_strcmp(opt->name,"Http:SendExpires") == 0) {
      http_config.expires_set  = 1;
      http_config.send_expires = strtol(args[0],(char **)&ptr,10);

      if(ptr) {
        if(toupper(*ptr) == 'M') {
          http_config.send_expires *= 60L;
        }
        else if(toupper(*ptr) == 'H') {
          http_config.send_expires *= 60L * 60L;
        }
      }

    }
    else if(cf_strcmp(opt->name,"Http:HandleLastModifiedSince") == 0) {
      if(cf_strcmp(args[0],"yes") == 0) {
        http_config.handle_last_modified_since = 1;
      }
    }
  }
  else {
    fprintf(stderr,"flt_http: Expecting one argument for directive %s!\n",opt->name);
    return 1;
  }

  return 0;
}
/* }}} */

/* {{{ flt_http_sehead_ters */
#ifndef CF_SHARED_MEM
int flt_http_sehead_ters(cf_hash_t *cgi,cf_hash_t *header_table,cf_configuration_t *dc,cf_configuration_t *vc,int sock) {
#else
int flt_http_sehead_ters(cf_hash_t *cgi,cf_hash_t *header_table,cf_configuration_t *dc,cf_configuration_t *vc,void *sock) {
#endif
  char buff[BUFSIZ];
  size_t len;
  time_t t;
  struct tm *tm;

  if(http_config.expires_set) {
    t  = time(NULL) + http_config.send_expires;
    tm = gmtime(&t);

    if(tm) {
      len = snprintf(buff,BUFSIZ,"public, max-age=%ld",http_config.send_expires);
      cf_hash_set(header_table,"Cache-Control",14,buff,len+1);

      if((len = strftime(buff,BUFSIZ,"%a, %d %b %Y %H:%M:%S GMT",tm))) {
        cf_hash_set(header_table,"Expires",8,buff,len+1);
      }
    }
  }

  return FLT_OK;
}
/* }}} */

/* {{{ flt_http_validate */
#ifndef CF_SHARED_MEM
int flt_http_validate(cf_hash_t *head,cf_configuration_t *dc,cf_configuration_t *vc,time_t last_modified,int sock) {
#else
int flt_http_validate(cf_hash_t *head,cf_configuration_t *dc,cf_configuration_t *vc,time_t last_modified,void *sock) {
#endif
  u_char *uname = cf_hash_get(GlobalValues,"UserName",8);
  u_char *uconffile = NULL;
  struct stat st;
  int ret = FLT_DECLINE;

  if(uname) {
    uconffile = cf_get_uconf_name(uname);

    if(uconffile) {
      if(stat(uconffile,&st) != -1) {
        if(st.st_mtime <= last_modified) ret = FLT_OK;
        else {
          printf("X-I-am-it: flt_http\015\012");
          ret = FLT_EXIT;
        }
      }

      free(uconffile);
    }
  }

  return ret;
}
/* }}} */

/* {{{ flt_http_lm */
#ifndef CF_SHARED_MEM
time_t flt_http_lm(cf_hash_t *head,cf_configuration_t *dc,cf_configuration_t *vc,int sock) {
#else
time_t flt_http_lm(cf_hash_t *head,cf_configuration_t *dc,cf_configuration_t *vc,void *sock) {
#endif
  u_char *uname = cf_hash_get(GlobalValues,"UserName",8);
  u_char *uconffile = NULL;
  struct stat st;
  time_t ret = -1;

  if(uname) {
    uconffile = cf_get_uconf_name(uname);

    if(uconffile) {
      if(stat(uconffile,&st) != -1) ret = st.st_mtime;
      free(uconffile);
    }
  }

  return ret;
}
/* }}} */

cf_conf_opt_t flt_http_config[] = {
  { "Http:SendLastModified",        flt_http_handle_command, CF_CFG_OPT_CONFIG|CF_CFG_OPT_LOCAL, NULL },
  { "Http:SendExpires",             flt_http_handle_command, CF_CFG_OPT_CONFIG|CF_CFG_OPT_LOCAL, NULL },
  { "Http:HandleLastModifiedSince", flt_http_handle_command, CF_CFG_OPT_CONFIG|CF_CFG_OPT_LOCAL, NULL },
  { NULL, NULL, 0, NULL }
};

cf_handler_config_t flt_http_handlers[] = {
  { CONNECT_INIT_HANDLER, flt_http_execute },
  { 0, NULL }
};

cf_module_config_t flt_http = {
  MODULE_MAGIC_COOKIE,
  flt_http_config,
  flt_http_handlers,
  NULL,
  flt_http_validate,
  flt_http_lm,
  flt_http_sehead_ters,
  NULL
};

/* eof */
