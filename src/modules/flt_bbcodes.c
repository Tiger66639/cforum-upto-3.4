/**
 * \file flt_bbcodes.c
 * \author Christian Kruse, <ckruse@wwwtech.de>
 *
 * This plugin handles some bbcodes
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
#include "config.h"
#include "defines.h"

#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#include <string.h>
#include <ctype.h>
#include <sys/types.h>

#include <pcre.h>

#include "readline.h"
#include "hashlib.h"
#include "utils.h"
#include "configparser.h"
#include "cfcgi.h"
#include "template.h"
#include "clientlib.h"
#include "charconvert.h"
#include "validate.h"
#include "htmllib.h"
/* }}} */

/* {{{ flt_bbcodes_execute_b */
int flt_bbcodes_execute_b(t_configuration *fdc,t_configuration *fvc,t_cl_thread *thread,const u_char *directive,const u_char **parameters,size_t plen,t_string *bco,t_string *bci,t_string *content,t_string *cite,const u_char *qchars,int sig) {
  str_chars_append(bco,"<strong>",8);
  str_str_append(bco,content);
  str_chars_append(bco,"</strong>",9);

  if(sig && bci && cite) {
    str_chars_append(bci,"[b]",3);
    str_str_append(bci,cite);
    str_chars_append(bci,"[/b]",4);
  }

  return FLT_OK;
}
/* }}} */

/* {{{ flt_bbcodes_execute_i */
int flt_bbcodes_execute_i(t_configuration *fdc,t_configuration *fvc,t_cl_thread *thread,const u_char *directive,const u_char **parameters,size_t plen,t_string *bco,t_string *bci,t_string *content,t_string *cite,const u_char *qchars,int sig) {
  str_chars_append(bco,"<em>",4);
  str_str_append(bco,content);
  str_chars_append(bco,"</em>",5);

  if(sig && bci && cite) {
    str_chars_append(bci,"[i]",3);
    str_str_append(bci,cite);
    str_chars_append(bci,"[/i]",4);
  }

  return FLT_OK;
}
/* }}} */

/* {{{ flt_bbcodes_execute_u */
int flt_bbcodes_execute_u(t_configuration *fdc,t_configuration *fvc,t_cl_thread *thread,const u_char *directive,const u_char **parameters,size_t plen,t_string *bco,t_string *bci,t_string *content,t_string *cite,const u_char *qchars,int sig) {
  str_chars_append(bco,"<span class=\"underlined\">",25);
  str_str_append(bco,content);
  str_chars_append(bco,"</span>",7);

  if(sig && bci && cite) {
    str_chars_append(bci,"[u]",3);
    str_str_append(bci,cite);
    str_chars_append(bci,"[/u]",4);
  }

  return FLT_OK;
}
/* }}} */

/* {{{ flt_bbcodes_execute_q */
int flt_bbcodes_execute_q(t_configuration *fdc,t_configuration *fvc,t_cl_thread *thread,const u_char *directive,const u_char **parameters,size_t plen,t_string *bco,t_string *bci,t_string *content,t_string *cite,const u_char *qchars,int sig) {
  str_chars_append(bco,"<q>",25);
  str_str_append(bco,content);
  str_chars_append(bco,"</q>",7);
  if(parameters && parameters[0]) {
    str_chars_append(bco," <span class=\"author\">",22);
    str_cstr_append(bco,parameters[0]);
    str_chars_append(bco,"</span>",7);
  }

  if(sig && bci && cite) {
    str_chars_append(bci,"[quote",6);
    if(parameters && parameters[0]) {
      str_char_append(bci,'=');
      str_cstr_append(bci,parameters[0]);
    }
    str_char_append(bci,']');
    str_str_append(bci,cite);
    str_chars_append(bci,"[/quote]",8);
  }

  return FLT_OK;
}
/* }}} */

int flt_bbcodes_init(t_cf_hash *cgi,t_configuration *dc,t_configuration *vc) {
  cf_html_register_directive("b",flt_bbcodes_execute_b,CF_HTML_DIR_TYPE_NOARG|CF_HTML_DIR_TYPE_BLOCK);
  cf_html_register_directive("i",flt_bbcodes_execute_i,CF_HTML_DIR_TYPE_NOARG|CF_HTML_DIR_TYPE_BLOCK);
  cf_html_register_directive("u",flt_bbcodes_execute_u,CF_HTML_DIR_TYPE_NOARG|CF_HTML_DIR_TYPE_BLOCK);

  cf_html_register_directive("quote",flt_bbcodes_execute_q,CF_HTML_DIR_TYPE_NOARG|CF_HTML_DIR_TYPE_ARG|CF_HTML_DIR_TYPE_BLOCK);

  return FLT_DECLINE;
}


t_conf_opt flt_bbcodes_config[] = {
  { NULL, NULL, 0, NULL }
};

t_handler_config flt_bbcodes_handlers[] = {
  { INIT_HANDLER, flt_bbcodes_init },
  { 0, NULL }
};

t_module_config flt_bbcodes = {
  MODULE_MAGIC_COOKIE,
  flt_bbcodes_config,
  flt_bbcodes_handlers,
  NULL,
  NULL,
  NULL,
  NULL,
  NULL
};

/* eof */