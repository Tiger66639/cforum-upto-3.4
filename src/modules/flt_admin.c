/**
 * \file flt_admin.c
 * \author Christian Kruse, <ckruse@wwwtech.de>
 *
 * This plug-in provides administrator functions
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
#include <unistd.h>
#include <string.h>
#include <time.h>
#include <sys/types.h>

#include "hashlib.h"
#include "utils.h"
#include "configparser.h"
#include "cfcgi.h"
#include "template.h"
#include "readline.h"
#include "clientlib.h"
#include "htmllib.h"
/* }}} */

u_char **flt_admin_Admins = NULL;
static size_t flt_admin_AdminNum = 0;
static int my_errno      = 0;
static int is_admin      = -1;
static int flt_admin_204 = 0;

static u_char *flt_admin_fn = NULL;

/* {{{ flt_admin_is_admin */
int flt_admin_is_admin(const u_char *name) {
  size_t i;

  if(!name) return 0;
  if(is_admin != -1) return is_admin;

  if(flt_admin_Admins) {
    for(i=0;i<flt_admin_AdminNum;i++) {
      if(cf_strcmp(flt_admin_Admins[i],name) == 0) {
        is_admin = 1;
        return 1;
      }
    }
  }

  if(cf_hash_get(GlobalValues,"is_admin",8) != NULL) {
    is_admin = 1;
    return 1;
  }

  is_admin = 0;
  return 0;
}
/* }}} */

/* {{{ flt_admin_gogogo */
#ifndef CF_SHARED_MEM
int flt_admin_gogogo(t_cf_hash *cgi,t_configuration *dc,t_configuration *vc,int sock)
#else
int flt_admin_gogogo(t_cf_hash *cgi,t_configuration *dc,t_configuration *vc,void *ptr)
#endif
{
  #ifdef CF_SHARED_MEM
  int sock;
  #endif
  u_char *action = NULL,*tid,*mid,buff[512],*answer,*mode;
  size_t len;
  rline_t rl;
  int x = 0;

  u_int64_t itid;

  u_char *UserName = cf_hash_get(GlobalValues,"UserName",8);
  u_char *forum_name = cf_hash_get(GlobalValues,"FORUM_NAME",10);

  if(!flt_admin_is_admin(UserName)) return FLT_DECLINE;

  if(cgi) action = cf_cgi_get(cgi,"faa");
  if(action) {
    tid = cf_cgi_get(cgi,"t");
    mid = cf_cgi_get(cgi,"m");

    if(!tid || !mid) return FLT_DECLINE;
    if(str_to_u_int64(tid) == 0 || str_to_u_int64(mid) == 0) return FLT_DECLINE;

    memset(&rl,0,sizeof(rl));

    #ifdef CF_SHARED_MEM
    /* if in shared memory mode, the sock parameter is a pointer to the shared mem segment */
    sock = cf_socket_setup();
    #endif

    len = snprintf(buff,512,"SELECT %s\n",forum_name);
    writen(sock,buff,len);

    answer = readline(sock,&rl);
    if(!answer || ((x = atoi(answer)) != 200)) {
      if(!answer) my_errno = 500;
      else my_errno = x;
    }
    if(answer) free(answer);

    if(x == 0 || x == 200) {
      if(cf_strcmp(action,"del") == 0) {
        len = snprintf(buff,512,"DELETE t%s m%s\nUser-Name: %s\n",tid,mid,UserName);
        writen(sock,buff,len);
      }
      else if(cf_strcmp(action,"undel") == 0) {
        len = snprintf(buff,512,"UNDELETE t%s m%s\nUser-Name: %s\n",tid,mid,UserName);
        writen(sock,buff,len);
      }
      else if(cf_strcmp(action,"archive") == 0) {
        len = snprintf(buff,512,"ARCHIVE THREAD t%s\nUser-Name: %s\n",tid,UserName);
        writen(sock,buff,len);
      }

      answer = readline(sock,&rl);
      if(!answer || ((x = atoi(answer)) != 200)) {
        if(!answer) my_errno = 500;
        else my_errno = x;
      }
      if(answer) free(answer);
    }

    #ifdef CF_SHARED_MEM
    ptr = cf_reget_shm_ptr();
    #endif

    itid = str_to_u_int64(tid);

    #ifdef CF_SHARED_MEM
    writen(sock,"QUIT\n",5);
    close(sock);
    #endif

    if((mode = cf_cgi_get(cgi,"mode")) == NULL || cf_strcmp(mode,"xmlhttp") != 0) {
      cf_hash_entry_delete(cgi,"t",1);
      cf_hash_entry_delete(cgi,"m",1);

      if(flt_admin_204) {
        printf("Status: 204 No Content\015\012\015\012");
        return FLT_EXIT;
      }
    }

    return FLT_OK;
  }

  return FLT_DECLINE;
}
/* }}} */

/* {{{ flt_admin_setvars */
int flt_admin_setvars(t_cf_hash *cgi,t_configuration *dc,t_configuration *vc,t_cf_template *top,t_cf_template *down) {
  u_char *msg,buff[256];
  size_t len,len1;
  u_char *UserName = cf_hash_get(GlobalValues,"UserName",8);
  int ShowInvisible = cf_hash_get(GlobalValues,"ShowInvisible",13) != NULL;
  u_char *fn = cf_hash_get(GlobalValues,"FORUM_NAME",10);
  t_name_value *usejs = cfg_get_first_value(&fo_view_conf,fn,"AdminUseJS");

  if(flt_admin_is_admin(UserName)) {
    cf_tpl_setvalue(top,"admin",TPL_VARIABLE_INT,1);

    if(ShowInvisible) {
      cf_tpl_setvalue(top,"aaf",TPL_VARIABLE_INT,1);
      if(usejs && cf_strcmp(usejs->values[0],"yes") == 0) cf_tpl_setvalue(top,"AdminJS",TPL_VARIABLE_INT,1);
    }

    if(my_errno) {
      len = snprintf(buff,256,"E_FO_%d",my_errno);
      msg = cf_get_error_message(buff,&len1);
      if(msg) {
        cf_tpl_setvalue(top,"flt_admin_errmsg",TPL_VARIABLE_STRING,msg,len1);
        free(msg);
      }
    }

    return FLT_OK;
  }

  return FLT_DECLINE;
}
/* }}} */

/* {{{ flt_admin_posting_setvars */
int flt_admin_posting_setvars(t_cf_hash *head,t_configuration *dc,t_configuration *vc,t_cl_thread *thread,t_cf_template *tpl) {
  u_char *UserName = cf_hash_get(GlobalValues,"UserName",8);
  int ShowInvisible = cf_hash_get(GlobalValues,"ShowInvisible",13) != NULL;

  if(flt_admin_is_admin(UserName)) {
    cf_tpl_setvalue(tpl,"admin",TPL_VARIABLE_INT,1);

    if(ShowInvisible) cf_tpl_setvalue(tpl,"aaf",TPL_VARIABLE_INT,1);

    return FLT_OK;
  }

  return FLT_DECLINE;
}
/* }}} */

/* {{{ flt_admin_setvars_thread */
int flt_admin_setvars_thread(t_cf_hash *head,t_configuration *dc,t_configuration *vc,t_cl_thread *thread,t_message *msg,t_cf_tpl_variable *hash) {
  u_char *UserName = cf_hash_get(GlobalValues,"UserName",8);
  int si = cf_hash_get(GlobalValues,"ShowInvisible",13) != NULL;

  t_cf_post_flag *flag;

  if(flt_admin_is_admin(UserName) && si) {
    cf_tpl_hashvar_setvalue(hash,"ip",TPL_VARIABLE_STRING,msg->remote_addr.content,msg->remote_addr.len);
    if((flag = cf_flag_by_name(&msg->flags,"UserName")) != NULL) cf_tpl_hashvar_setvalue(hash,"uname",TPL_VARIABLE_STRING,flag->val,strlen(flag->val));
    return FLT_OK;
  }

  return FLT_DECLINE;
}
/* }}} */

/* {{{ flt_admin_init */
int flt_admin_init(t_cf_hash *cgi,t_configuration *dc,t_configuration *vc) {
  u_char *forum_name = cf_hash_get(GlobalValues,"FORUM_NAME",10);
  t_name_value *v = cfg_get_first_value(dc,forum_name,"Administrators");
  u_char *val = NULL;
  u_char *UserName = cf_hash_get(GlobalValues,"UserName",8);

  cf_register_mod_api_ent("flt_admin","is_admin",(t_mod_api)flt_admin_is_admin);

  if(!UserName) return FLT_DECLINE;
  if(v) flt_admin_AdminNum = split(v->values[0],",",&flt_admin_Admins);

  if(!cgi)      return FLT_DECLINE;

  val = cf_cgi_get(cgi,"aaf");
  if(!val) return FLT_DECLINE;

  /* ShowInvisible is imported from the client library */
  if(flt_admin_is_admin(UserName) && *val == '1') {
    cf_hash_set(GlobalValues,"ShowInvisible",13,"1",1);
    cf_add_static_uri_flag("aaf","1",0);
  }

  return FLT_OK;
}
/* }}} */

/* {{{ flt_admin_posthandler */
int flt_admin_posthandler(t_cf_hash *cgi,t_configuration *dc,t_configuration *vc,t_message *msg,u_int64_t tid,int mode) {
  u_char *forum_name = cf_hash_get(GlobalValues,"FORUM_NAME",10);  
  u_char buff[256];
  u_char *link;
  size_t l;
  u_char *UserName = cf_hash_get(GlobalValues,"UserName",8);
  int ShowInvisible = cf_hash_get(GlobalValues,"ShowInvisible",13) != NULL;
  cf_readmode_t *rm = cf_hash_get(GlobalValues,"RM",2);
  t_name_value *usejs = cfg_get_first_value(&fo_view_conf,forum_name,"AdminUseJS");

  if(!UserName) return FLT_DECLINE;

  if(flt_admin_is_admin(UserName) && ShowInvisible) {
    cf_tpl_hashvar_setvalue(&msg->hashvar,"admin",TPL_VARIABLE_INT,1);

    cf_tpl_hashvar_setvalue(&msg->hashvar,"aaf",TPL_VARIABLE_INT,1);
    if(usejs && cf_strcmp(usejs->values[0],"yes") == 0 && (mode & CF_MODE_THREADLIST)) cf_tpl_hashvar_setvalue(&msg->hashvar,"AdminJS",TPL_VARIABLE_INT,1);

    link = cf_advanced_get_link(rm->posting_uri[1],tid,msg->mid,NULL,1,&l,"faa","archive");
    cf_tpl_hashvar_setvalue(&msg->hashvar,"archive_link",TPL_VARIABLE_STRING,link,l);
    free(link);

    if(msg->invisible == 0) {
      link = cf_advanced_get_link(rm->posting_uri[1],tid,msg->mid,NULL,1,&l,"faa","del");
      cf_tpl_hashvar_setvalue(&msg->hashvar,"visible",TPL_VARIABLE_STRING,"1",1);
      cf_tpl_hashvar_setvalue(&msg->hashvar,"del_link",TPL_VARIABLE_STRING,link,l);
      free(link);
    }
    else {
      link = cf_advanced_get_link(rm->posting_uri[1],tid,msg->mid,NULL,1,&l,"faa","undel");
      cf_tpl_hashvar_setvalue(&msg->hashvar,"undel_link",TPL_VARIABLE_STRING,link,l);
      free(link);
    }

    return FLT_OK;
  }

  return FLT_DECLINE;
}
/* }}} */

/* {{{ flt_admin_finish */
void flt_admin_finish(void) {
  size_t i;
  if(flt_admin_Admins) {
    for(i=0;i<flt_admin_AdminNum;i++) free(flt_admin_Admins[i]);
    free(flt_admin_Admins);
  }
}
/* }}} */

/* {{{ flt_admin_validator */
#ifndef CF_SHARED_MEM
int flt_admin_validator(t_cf_hash *head,t_configuration *dc,t_configuration *vc,time_t last_modified,int sock)
#else
int flt_admin_validator(t_cf_hash *head,t_configuration *dc,t_configuration *vc,time_t last_modified,void *sock)
#endif
{
  u_char *UserName = cf_hash_get(GlobalValues,"UserName",8);
  int ShowInvisible = cf_hash_get(GlobalValues,"ShowInvisible",13) != NULL;

  if(flt_admin_is_admin(UserName) && ShowInvisible) return FLT_EXIT;
  return FLT_DECLINE;
}
/* }}} */

/* {{{ flt_admin_lm */
#ifndef CF_SHARED_MEM
time_t flt_admin_lm(t_cf_hash *head,t_configuration *dc,t_configuration *vc,int sock)
#else
time_t flt_admin_lm(t_cf_hash *head,t_configuration *dc,t_configuration *vc,void *sock)
#endif
{
  u_char *UserName = cf_hash_get(GlobalValues,"UserName",8);
  int ShowInvisible = cf_hash_get(GlobalValues,"ShowInvisible",13) != NULL;

  if(flt_admin_is_admin(UserName) && ShowInvisible) time(NULL);
  return 0;
}
/* }}} */

/* {{{ flt_admin_handle */
int flt_admin_handle(t_configfile *cfile,t_conf_opt *opt,const u_char *context,u_char **args,size_t argnum) {
  if(flt_admin_fn == NULL) flt_admin_fn = cf_hash_get(GlobalValues,"FORUM_NAME",10);
  if(!context || cf_strcmp(flt_admin_fn,context) != 0) return 0;

  if(cf_strcmp(opt->name,"AdminSend204") == 0) flt_admin_204 = cf_strcmp(args[0],"yes") == 0;

  return 0;
}
/* }}} */

t_conf_opt flt_admin_config[] = {
  { "AdminSend204", flt_admin_handle, CFG_OPT_USER|CFG_OPT_LOCAL, NULL },
  { NULL, NULL, 0, NULL }
};

t_handler_config flt_admin_handlers[] = {
  { CONNECT_INIT_HANDLER, flt_admin_gogogo },
  { INIT_HANDLER,         flt_admin_init },
  { VIEW_INIT_HANDLER,    flt_admin_setvars },
  { VIEW_LIST_HANDLER,    flt_admin_posthandler },
  { POSTING_HANDLER,      flt_admin_posting_setvars },
  { PERPOST_VAR_HANDLER,  flt_admin_setvars_thread },
  { 0, NULL }
};

t_module_config flt_admin = {
  MODULE_MAGIC_COOKIE,
  flt_admin_config,
  flt_admin_handlers,
  NULL,
  flt_admin_validator,
  flt_admin_lm,
  NULL,
  flt_admin_finish
};

/* eof */
