/**
 * \file fo_userconf.c
 * \author Christian Kruse, <cjk@wwwtech.de>
 * \brief The forum userconfig program
 */

/* {{{ Initial comment */
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
#include <dlfcn.h>
#include <errno.h>
#include <ctype.h>

#include <sys/types.h>
#include <sys/file.h>
#include <signal.h>

/* socket includes */
#include <sys/socket.h>
#include <netdb.h>
#include <unistd.h>
#include <sys/un.h>

#include <db.h>

#include "readline.h"
#include "hashlib.h"
#include "utils.h"
#include "cfgcomp.h"
#include "cfcgi.h"
#include "template.h"
#include "clientlib.h"
#include "charconvert.h"

#include "userconf.h"
/* }}} */

/* {{{ signal handler for bad signals */
void sighandler(int segnum) {
  FILE *fd = fopen(PROTOCOL_FILE,"a");
  u_char buff[10],*uname = NULL,*qs = NULL;

  if(fd) {
    qs    = getenv("QUERY_STRING");
    if(GlobalValues) uname = cf_hash_get(GlobalValues,"UserName",8);

    switch(segnum) {
      case SIGSEGV:
        snprintf(buff,10,"SIGSEGV");
        break;
      case SIGILL:
        snprintf(buff,10,"SIGILL");
        break;
      case SIGFPE:
        snprintf(buff,10,"SIGFPE");
        break;
      case SIGBUS:
        snprintf(buff,10,"SIGBUS");
        break;
      default:
        snprintf(buff,10,"UKNOWN");
        break;
    }

    fprintf(fd,"fo_userconf: Got signal %s!\nUsername: %s\nQuery-String: %s\n----\n",buff,uname?uname:(u_char *)"(null)",qs?qs:(u_char *)"(null)");
    fclose(fd);
  }

  exit(0);
}
/* }}} */

int cf_cfg_compare(cf_tree_dataset_t *dt1,cf_tree_dataset_t *dt2);
void destroy_directive_list(cf_tree_dataset_t *dt);

static int inited = 0;
cf_configuration_t glob_config;

/* {{{ handle_userconf_command */
int handle_userconf_command(cf_configfile_t *cfile,const u_char *context,u_char *name,u_char **args,size_t argnum) {
  cf_conf_opt_t opt;
  int ret;
  u_char *fn = cf_hash_get(GlobalValues,"FORUM_NAME",10);

  opt.name  = strdup(name);
  opt.data  = &glob_config;
  opt.flags = CF_CFG_OPT_LOCAL|CF_CFG_OPT_USER|CF_CFG_OPT_CONFIG;

  ret = cf_handle_command(NULL,&opt,fn,args,argnum);

  if(ret != -1) free(opt.name);

  return ret;
}
/* }}} */

/* {{{ show_edit_content */
void show_edit_content(cf_hash_t *head,const u_char *msg,const u_char *source,int saved,cf_array_t *errors) {
  u_char tplname[256],*fn = cf_hash_get(GlobalValues,"FORUM_NAME",10),*tmp,buff[256];

  cf_name_value_t *cval,
    *cs = cf_cfg_get_first_value(&fo_default_conf,fn,"DF:ExternCharset"),
    *tplnv = cf_cfg_get_first_value(&fo_userconf_conf,fn,"Edit"),
    *cats = cf_cfg_get_first_value(&fo_default_conf,fn,"DF:Categories");

  cf_uconf_userconfig_t *modxml;
  cf_template_t tpl;
  cf_uconf_directive_t *directive;
  cf_uconf_argument_t *arg;
  cf_name_value_t *value;
  cf_cgi_param_t *mult;
  cf_string_t val;
  cf_tpl_variable_t array,errmsgs,var1;
  int utf8 = cf_strcmp(cs->values[0],"UTF-8") == 0;
  struct tm tm;
  time_t tval;

  size_t i,j;

  cf_uconf_error_t *error;

  if((modxml = cf_uconf_read_modxml()) == NULL) {
    printf("Status: 500 Internal Server Error\015\012Content-Type: text/html; charset=%s\015\012\015\012",cs->values[0]);
    cf_error_message("E_dontknow",NULL);
    return;
  }

  cf_gen_tpl_name(tplname,256,tplnv->values[0]);
  if(cf_tpl_init(&tpl,tplname) != 0) {
    printf("Status: 500 Internal Server Error\015\012Content-Type: text/html; charset=%s\015\012\015\012",cs->values[0]);
    cf_error_message("E_TPL_NOT_FOUND",NULL);
    return;
  }

  for(i=0;i<modxml->directives.elements;++i) {
    directive = cf_array_element_at(&modxml->directives,i);
    if(directive->flags & CF_UCONF_FLAG_INVISIBLE) continue;

    /* {{{ get value in hierarchy */
    if((source && cf_strcmp(source,"cgi") != 0) || !source) {
      if((value = cf_cfg_get_first_value(&glob_config,fn,directive->name)) == NULL) {
        if((value = cf_cfg_get_first_value(&fo_userconf_conf,fn,directive->name)) == NULL) {
          if((value = cf_cfg_get_first_value(&fo_view_conf,fn,directive->name)) == NULL) {
            value = cf_cfg_get_first_value(&fo_default_conf,fn,directive->name);
          }
        }
      }
    }
    else value = NULL;
    /* }}} */

    for(j=0;j<directive->arguments.elements;++j) {
      arg = cf_array_element_at(&directive->arguments,j);

      /* {{{ set value */
      cf_str_init(&val);
      if(source) {
        /*
         * Source is set, so we check: if it is CGI, we take our value from the CGI
         * environment. If not, we check if value is not null and set one (the right)
         * of the values
         */
        if(cf_strcmp(source,"cgi") == 0) {
          if(head && (mult = cf_cgi_get_multiple(head,arg->param)) != NULL && mult->value.content && *mult->value.content) {
            cf_str_str_set(&val,&mult->value);

            for(mult=mult->next;mult;mult=mult->next) {
              cf_str_char_append(&val,',');
              cf_str_str_append(&val,&mult->value);
            }
          }
          else if(arg->ifnotcommitted) cf_str_char_set(&val,arg->ifnotcommitted,strlen(arg->ifnotcommitted));
        }
        /*
         * Ok, source is config, check if config has the specific
         * value (j < valnum) and set it to the template
         */
        else {
          if(value && j < value->valnum) cf_str_char_set(&val,value->values[j],strlen(value->values[j]));
          else if(arg->deflt) cf_str_char_set(&val,arg->deflt,strlen(arg->deflt));
        }
      }
      else {
        /*
         * source is not set; so try to get the value first from CGI
         * environment (CGI overwrites config), after that look
         * if the specific config value exists.
         */
        if(head && (mult = cf_cgi_get_multiple(head,arg->param)) != NULL) {
          cf_str_str_set(&val,&mult->value);

          for(mult=mult->next;mult;mult=mult->next) {
            cf_str_char_append(&val,',');
            cf_str_str_append(&val,&mult->value);
          }
        }
        else {
          /*
           * A value for this config directive has not been
           * committed by CGI, so set it by config if exists
           */
          if(value && j < value->valnum) cf_str_char_set(&val,value->values[j],strlen(value->values[j]));
          else if(arg->deflt) cf_str_char_set(&val,arg->deflt,strlen(arg->deflt));
        }
      }
      /* }}} */

      if(val.content && *val.content) {
        if(!arg->parse || cf_strcmp(arg->parse,"date") != 0) cf_uconf_to_html(&val);
        else {
          /* date value */
          tval = (time_t)strtoul(val.content,NULL,10);
          localtime_r(&tval,&tm);
          j = snprintf(buff,256,"%02d. %02d. %4d %02d:%02d:%02d",tm.tm_mday,tm.tm_mon+1,tm.tm_year+1900,tm.tm_hour,tm.tm_min,tm.tm_sec);
          cf_str_char_set(&val,buff,j);
        }

        cf_set_variable(&tpl,cs,arg->param,val.content,val.len,1);
        cf_str_cleanup(&val);
      }
    }
  }

  /* {{{ set error message */
  if(msg) {
    if((tmp = cf_get_error_message(msg,&i)) != NULL) {
      cf_set_variable(&tpl,cs,"err",tmp,i,1);
      free(tmp);
    }
    else cf_set_variable(&tpl,cs,"err",msg,strlen(msg),1);

    cf_tpl_setvalue(&tpl,"error",TPL_VARIABLE_INT,1);
  }
  /* }}} */

  /* {{{ set categories */
  cf_tpl_var_init(&array,TPL_VARIABLE_ARRAY);
  for(i=0;i<cats->valnum;++i) {
    if(utf8) {
      tmp = htmlentities(cats->values[i],0);
      j   = strlen(tmp);
    }
    else tmp = charset_convert_entities(cats->values[i],strlen(cats->values[i]),"UTF-8",cs->values[0],&j);

    cf_tpl_var_addvalue(&array,TPL_VARIABLE_STRING,tmp,j);
    free(tmp);
  }
  cf_tpl_setvar(&tpl,"categories",&array);
  /* }}} */

  if(saved) cf_tpl_setvalue(&tpl,"save",TPL_VARIABLE_INT,1);
  if(cf_hash_get(GlobalValues,"is_admin",8) != NULL) cf_tpl_setvalue(&tpl,"save",TPL_VARIABLE_INT,1);

  cval = cf_cfg_get_first_value(&fo_default_conf,fn,"UDF:BaseURL");
  cf_set_variable(&tpl,cs,"forumbase",cval->values[0],strlen(cval->values[0]),1);

  cval = cf_cfg_get_first_value(&fo_default_conf,fn,"UserConfig");
  cf_set_variable(&tpl,cs,"userconfig",cval->values[0],strlen(cval->values[0]),1);
  cf_set_variable(&tpl,cs,"script",cval->values[0],strlen(cval->values[0]),1);

  cval = cf_cfg_get_first_value(&fo_default_conf,fn,"UserManagement");
  cf_set_variable(&tpl,cs,"usermanagement",cval->values[0],strlen(cval->values[0]),1);

  cf_set_variable(&tpl,cs,"charset",cs->values[0],strlen(cs->values[0]),1);

  if(errors && errors->elements) {
    cf_tpl_var_init(&errmsgs,TPL_VARIABLE_ARRAY);

    for(i=0;i<errors->elements;++i) {
      error = cf_array_element_at(errors,i);

      cf_tpl_var_init(&var1,TPL_VARIABLE_ARRAY);
      cf_add_variable(&var1,cs,error->directive,strlen(error->directive),1);
      cf_add_variable(&var1,cs,error->param,strlen(error->param),1);
      cf_add_variable(&var1,cs,error->error,strlen(error->error),1);

      cf_tpl_var_add(&errmsgs,&var1);
    }

    cf_tpl_setvar(&tpl,"errmsgs",&errmsgs);
    cf_tpl_setvalue(&tpl,"error",TPL_VARIABLE_INT,1);
  }

  cf_run_uconf_display_handlers(head,&fo_default_conf,&fo_userconf_conf,&tpl,&glob_config);

  printf("Content-Type: text/html; charset=%s\015\012\015\012",cs->values[0]);
  cf_tpl_parse(&tpl);

  cf_tpl_finish(&tpl);
  cf_uconf_cleanup_modxml(modxml);
}
/* }}} */

/* {{{ do_save */
void do_save(cf_hash_t *head) {
  cf_uconf_userconfig_t *merged;
  u_char *msg,*uname,*ucfg, *forum_name = cf_hash_get(GlobalValues,"FORUM_NAME",10);;
  cf_name_value_t *cs = cf_cfg_get_first_value(&fo_default_conf,forum_name,"DF:ExternCharset");
  cf_array_t errmsgs;

  if((uname = cf_hash_get(GlobalValues,"UserName",8)) == NULL) {
    printf("Status: 403 Forbidden\015\012Content-Type: text/html; charset=%s\015\012\015\012",cs->values[0]);
    cf_error_message("E_MUST_AUTH",NULL);
    return;
  }

  if((ucfg = cf_get_uconf_name(uname)) == NULL) {
    printf("Status: 403 Forbidden\015\012Content-Type: text/html; charset=%s\015\012\015\012",cs->values[0]);
    cf_error_message("E_MUST_AUTH",NULL);
    return;
  }

  memset(&errmsgs,0,sizeof(errmsgs));
  if((merged = cf_uconf_merge_config(head,&glob_config,&errmsgs,1)) != NULL) {
    if(cf_run_uconf_write_handlers(head,&fo_default_conf,&fo_userconf_conf,&glob_config,merged) == FLT_EXIT) {
      cf_uconf_cleanup_modxml(merged);
      free(merged);
      free(ucfg);
      return;
    }

    if((msg = cf_write_uconf(ucfg,merged)) != NULL) cf_error_message(msg,NULL,strerror(errno));
    else show_edit_content(head,NULL,"cgi",1,NULL);

    cf_uconf_cleanup_modxml(merged);
    free(merged);
    free(ucfg);
    return;
  }
  else {
    show_edit_content(head,NULL,"cgi",0,&errmsgs);
    cf_array_destroy(&errmsgs);
  }
}
/* }}} */

/* {{{ normalize_params */
u_char *normalize_params(cf_hash_t *head,const u_char *name) {
  u_char *forum_name = cf_hash_get(GlobalValues,"FORUM_NAME",10),*converted,c;
  register u_char *ptr;
  cf_name_value_t *cs = cf_cfg_get_first_value(&fo_default_conf,forum_name,"DF:ExternCharset");
  cf_hash_keylist_t *key;
  cf_cgi_param_t *param;
  size_t flen;

  cf_string_t str,*str1,*val;


  if((val = cf_cgi_get(head,(u_char *)name)) == NULL) return "E_manipulated";

  /* utf-8? */
  if(*val->content != 0xC3 || *(val->content+1) != 0xBF) {
    /* {{{ transform everything to utf-8... */
    for(key=head->keys.elems;key;key=key->next) {
      for(param = cf_cgi_get_multiple(head,key->key);param;param=param->next) {
        if((converted = charset_convert(param->value.content,param->value.len,cs->values[0],"UTF-8",NULL)) == NULL) return "E_manipulated";

        /* {{{ remove unicode whitespaces */
        cf_str_init(&str);
        for(ptr=converted;*ptr;++ptr) {
          // \xC2\xA0 is nbsp
          if(cf_strncmp(ptr,"\xC2\xA0",2) == 0) {
            cf_str_char_append(&str,' ');
            ++ptr;
          }
          // \xE2\x80[\x80-\x8B\xA8-\xAF] are unicode whitespaces
          else if(cf_strncmp(ptr,"\xE2\x80",2) == 0) {
            c = *(ptr+3);
            if((c >= 0x80 && c <= 0x8B) || (c >= 0xA8 && c <= 0xAF)) {
              cf_str_char_append(&str,' ');
              ptr += 2;
            }
            else cf_str_char_append(&str,*ptr);
          }
          else cf_str_char_append(&str,*ptr);
        }
        /* }}} */

        free(param->value.content);
        param->value.content = str.content;
        param->value.len     = str.len;
        param->value.reserved= str.reserved;

        free(converted);
      }
    }
    /* }}} */
  }
  else {
    /* {{{ input seems to be UTF-8, check if strings are valid UTF-8 */
    for(key=head->keys.elems;key;key=key->next) {
      for(param = cf_cgi_get_multiple(head,key->key);param;param=param->next) {
        if(is_valid_utf8_string(param->value.content,param->value.len) != 0) return "E_manipulated";

        /* {{{ removed unicode whitespaces */
        cf_str_init(&str);
        for(ptr=param->value.content;*ptr;++ptr) {
          // \xC2\xA0 is nbsp
          if(cf_strncmp(ptr,"\xC2\xA0",2) == 0) {
            cf_str_char_append(&str,' ');
            ++ptr;
          }
          // \xE2\x80[\x80-\x8B\xA8-\xAF] are unicode whitespaces
          else if(cf_strncmp(ptr,"\xE2\x80",2) == 0) {
            c = *(ptr+3);
            if((c >= 0x80 && c <= 0x8B) || (c >= 0xA8 && c <= 0xAF)) {
              cf_str_char_append(&str,' ');
              ptr += 2;
            }
            else cf_str_char_append(&str,*ptr);
          }
          else cf_str_char_append(&str,*ptr);
        }
        /* }}} */

        free(param->value.content);
        param->value.content = str.content;
        param->value.len     = str.len;
        param->value.reserved= str.reserved;
      }
    }
    /* }}} */
  }

  /* {{{ remove first two characters */
  if((str1 = cf_cgi_get(head,(u_char *)name)) != NULL) {
    flen      = str1->len;
    converted = cf_alloc(NULL,1,flen-2,CF_ALLOC_MALLOC);

    /* strip character from field */
    memcpy(converted,str1->content+2,flen-2);
    free(str1->content);

    str1->content = converted;
    str1->len = str1->reserved = flen - 2;
    str1->content[flen-2] = '\0';
  }
  /* }}} */

  return NULL;
}
/* }}} */

/* {{{ add_view_directive */
int add_view_directive(cf_configfile_t *cfile,const u_char *context,u_char *name,u_char **args,size_t argnum) {
  cf_conf_opt_t opt;
  int ret;

  opt.name   = strdup(name);
  opt.data   = &fo_view_conf;
  if(context) opt.flags = CF_CFG_OPT_LOCAL;
  else opt.flags = CF_CFG_OPT_GLOBAL;

  opt.flags |= CF_CFG_OPT_USER|CF_CFG_OPT_CONFIG;

  ret = cf_handle_command(NULL,&opt,context,args,argnum);

  if(ret != -1) free(opt.name);

  return ret;
}
/* }}} */

/* {{{ add_uconf_directive */
int add_uconf_directive(cf_configfile_t *cfile,const u_char *context,u_char *name,u_char **args,size_t argnum) {
  cf_conf_opt_t opt;
  int ret;

  u_char *fn = cf_hash_get(GlobalValues,"FORUM_NAME",10);

  opt.name   = strdup(name);
  opt.data   = &fo_userconf_conf;
  opt.flags  = CF_CFG_OPT_LOCAL;

  opt.flags |= CF_CFG_OPT_USER|CF_CFG_OPT_CONFIG;

  ret = cf_handle_command(NULL,&opt,fn,args,argnum);

  if(ret != -1) free(opt.name);

  return ret;
}
/* }}} */

/**
 * Dummy function, for ignoring unknown directives
 */
int ignre(cf_configfile_t *cf,const u_char *context,u_char *name,u_char **args,size_t argnum) {
  return 0;
}

/**
 * The main function of the forum userconfig program. No command line switches
 * used.
 * \param argc The argument count
 * \param argv The argument vector
 * \param env The environment vector
 * \return EXIT_SUCCESS on success, EXIT_FAILURE on error
 */
int main(int argc,char *argv[],char *env[]) {
  /* {{{ initialization */
  static const u_char *wanted[] = {
    "fo_default", "fo_userconf", "fo_view"
  };

  cf_hash_t *head;

  u_char *forum_name,*fname,*uname,*err,*ucfg;

  cf_array_t *cfgfiles;
  cf_configfile_t dconf,conf,vconf,all_conf;

  cf_name_value_t *cs;

  int ret;

  cf_uconf_action_handler_t actionhndl;

  cf_string_t *action;
  /* }}} */

  /* {{{ set signal handler for bad signals (for error reporting) */
  signal(SIGSEGV,sighandler);
  signal(SIGILL,sighandler);
  signal(SIGFPE,sighandler);
  signal(SIGBUS,sighandler);
  /* }}} */

  cf_init();
  init_modules();
  cf_cfg_init();

  cf_tree_init(&glob_config.global_directives,cf_cfg_compare,destroy_directive_list);
  cf_list_init(&glob_config.forums);

  forum_name = cf_hash_get(GlobalValues,"FORUM_NAME",10);
  head       = cf_cgi_new();

  /* {{{ read config files */
  if((cfgfiles = cf_get_conf_file(wanted,3)) == NULL) {
    fprintf(stderr,"Could not find config files!\n");
    return EXIT_FAILURE;
  }

  fname = *((u_char **)cf_array_element_at(cfgfiles,0));
  cf_cfg_init_file(&dconf,fname);
  free(fname);

  fname = *((u_char **)cf_array_element_at(cfgfiles,1));
  cf_cfg_init_file(&conf,fname);
  free(fname);

  fname = *((u_char **)cf_array_element_at(cfgfiles,2));
  cf_cfg_init_file(&vconf,fname);
  free(fname);

  cf_cfg_register_options(&dconf,default_options);
  cf_cfg_register_options(&conf,fo_userconf_options);
  cf_cfg_register_options(&vconf,fo_view_options);

  if(cf_read_config(&dconf,NULL,CF_CFG_MODE_CONFIG) != 0 || cf_read_config(&conf,NULL,CF_CFG_MODE_CONFIG) != 0 || cf_read_config(&vconf,add_view_directive,CF_CFG_MODE_CONFIG|CF_CFG_MODE_NOLOAD) != 0) {
    fprintf(stderr,"config file error!\n");
    cf_cfg_cleanup_file(&dconf);
    cf_cfg_cleanup_file(&conf);
    return EXIT_FAILURE;
  }
  /* }}} */

  /* first action: authorization modules */
  cs  = cf_cfg_get_first_value(&fo_default_conf,forum_name,"DF:ExternCharset");
  ret = cf_run_auth_handlers(head);

  if((uname = cf_hash_get(GlobalValues,"UserName",8)) == NULL) {
    printf("Status: 403 Forbidden\015\012Content-Type: text/html; charset=%s\015\012\015\012",cs->values[0]);
    cf_error_message("E_MUST_AUTH",NULL);
    return EXIT_SUCCESS;
  }

  /* {{{ read user config */
  if((ucfg = cf_get_uconf_name(uname)) == NULL) {
    printf("Status: 403 Forbidden\015\012Content-Type: text/html; charset=%s\015\012\015\012",cs->values[0]);
    cf_error_message("E_MUST_AUTH",NULL);
    return EXIT_SUCCESS;
  }

  free(conf.filename);
  conf.filename = ucfg;

  if(cf_read_config(&conf,add_uconf_directive,CF_CFG_MODE_USER) != 0) {
    fprintf(stderr,"config file error!\n");

    cf_cfg_cleanup_file(&conf);
    cf_cfg_cleanup_file(&dconf);

    return EXIT_FAILURE;
  }

  cf_cfg_init_file(&all_conf,ucfg);

  if(cf_read_config(&all_conf,handle_userconf_command,CF_CFG_MODE_USER) != 0) {
    printf("Status: 500 Internal Server Error\015\012COntent-Type: text/html; charset=%s\015\012\015\012",cs->values[0]);
    cf_error_message("E_CONFIG_BROKEN",NULL);
    free(ucfg);
    return EXIT_FAILURE;
  }

  inited = 1;
  /* }}} */

  ret = cf_run_init_handlers(head);

  if(ret != FLT_EXIT) {
    if(head && cf_cgi_get(head,"cs") != NULL) {
      if((err = normalize_params(head,"cs")) != NULL) {
        printf("Status: 500 Internal Server Error\015\012Status: text/html; charset=%s\015\012\015\012",cs->values[0]);
        cf_error_message(err,NULL);
        return EXIT_SUCCESS;
      }
    }

    if(head) {
      if((action = cf_cgi_get(head,"a")) == NULL) show_edit_content(head,NULL,NULL,0,NULL);
      else {
        if(cf_strcmp(action->content,"save") == 0) do_save(head);
        else if((actionhndl = cf_uconf_get_action_handler(action->content)) != NULL) actionhndl(head,&fo_default_conf,&fo_userconf_conf,&glob_config);
        else show_edit_content(head,NULL,NULL,0,NULL);
      }
    }
    else show_edit_content(head,NULL,NULL,0,NULL);
  }

  if(head) cf_hash_destroy(head);

  /* cleanup source */
  cf_cfg_cleanup_file(&dconf);
  cf_cfg_cleanup_file(&conf);
  cf_cfg_cleanup_file(&all_conf);

  cf_array_destroy(cfgfiles);
  free(cfgfiles);

  cf_cleanup_modules(Modules);
  cf_fini();
  cf_cfg_destroy();

  return EXIT_SUCCESS;
}

/* eof */
