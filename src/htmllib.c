/**
 * \file htmllib.c
 * \author Christian Kruse
 *
 * This library contains some functions to display messages in HTML
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

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>

#include <sys/types.h>

#include "hashlib.h"
#include "utils.h"
#include "cfgcomp.h"
#include "template.h"
#include "readline.h"
#include "charconvert.h"
#include "clientlib.h"
#include "htmllib.h"
/* }}} */

typedef struct s_directive_callback {
  cf_directive_filter_t callback;
  int type;
} cf_directive_callback_t;

typedef struct s_validator_callback {
  cf_directive_validator_t callback;
  int type;
} cf_validator_callback_t;

typedef struct s_html_stack {
  u_char *begin;
  u_char *name;
  u_char **args;
  size_t argnum;
} cf_html_stack_elem_t;

typedef struct cf_html_tree_s {
  u_char c;
  struct cf_html_tree_s **nodes;
  cf_directive_filter_t cb;
} cf_html_tree_t;

static cf_hash_t *registered_directives = NULL;
static cf_hash_t *registered_validators = NULL;
static cf_html_tree_t *parser_tree[256];


/* {{{ run_content_filters */
void cf_run_content_filters(cf_cfg_config_t *cfg,int mode,cf_cl_thread_t *thr,cf_string_t *content,cf_string_t *cite,const u_char *qchars) {
  int ret = FLT_OK;
  cf_handler_config_t *handler;
  size_t i;
  cf_content_filter_t fkt;

  if(cfg->modules[mode].elements) {
    for(i=0;i<cfg->modules[mode].elements;i++) {
      handler = cf_array_element_at(&cfg->modules[mode],i);
      fkt     = (cf_content_filter_t)handler->func;
      ret     = fkt(cfg,thr,content,cite,qchars);
    }
  }
}
/* }}} */

/* {{{ run_inline_directive_filters */
int cf_run_inline_directive_filters(cf_cfg_config_t *cfg,cf_cl_thread_t *thread,const u_char *directive,const u_char **parameters,cf_string_t *content,cf_string_t *cite,const u_char *qchars,int sig) {
  cf_directive_callback_t *cb;

  if((cb = cf_hash_get(registered_directives,(u_char *)directive,strlen(directive))) == NULL) return FLT_DECLINE;

  if(cb->type & CF_HTML_DIR_TYPE_INLINE) return cb->callback(cfg,thread,directive,parameters,1,NULL,NULL,content,cite,qchars,sig);
  return FLT_DECLINE;
}
/* }}} */

/* {{{ run_validate_inline */
int cf_run_validate_inline(cf_cfg_config_t *cfg,const u_char *directive,const u_char **parameters,cf_tpl_variable_t *var) {
  cf_validator_callback_t *cb;

  if((cb = cf_hash_get(registered_validators,(u_char *)directive,strlen(directive))) == NULL) return FLT_DECLINE;

  if(cb->type & CF_HTML_DIR_TYPE_INLINE) return cb->callback(cfg,directive,parameters,1,var);
  return FLT_DECLINE;
}
/* }}} */

/* {{{ cf_next_line_is_no_quote_line */
static int cf_next_line_is_no_quote_line(const u_char *ptr) {
  int eq;

  for(;*ptr && ((eq = cf_strncmp(ptr,"<br />",6)) == 0 || *ptr == ' ');ptr++) {
    if(eq == 0) ptr += 5;
  }

  if(*ptr == (u_char)127) return 0;
  return 1;
}
/* }}} */

/* {{{ is_open */
static int cf_is_open(const u_char *name,cf_array_t *stack) {
  int i;
  cf_html_stack_elem_t *s_el;

  for(i=stack->elements-1;i>=0;--i) {
    s_el = cf_array_element_at(stack,i);
    if(cf_strcmp(s_el->name,name) == 0) return 1;
  }

  return 0;
}
/* }}} */

/* {{{ run_block_directive_filters */
static int cf_run_block_directive_filters(cf_cfg_config_t *cfg,cf_cl_thread_t *thread,const u_char *directive,const u_char **parameters,size_t len,cf_string_t *bcontent,cf_string_t *bcite,cf_string_t *content,cf_string_t *cite,const u_char *qchars,int sig) {
  cf_directive_callback_t *cb;

  if((cb = cf_hash_get(registered_directives,(u_char *)directive,strlen(directive))) == NULL) return FLT_DECLINE;

  if(cb->type & CF_HTML_DIR_TYPE_BLOCK) return cb->callback(cfg,thread,directive,parameters,len,bcontent,bcite,content,cite,qchars,sig);
  return FLT_DECLINE;
}
/* }}} */

/* {{{ run_validate_block_directive */
static int cf_run_validate_block_directive(cf_cfg_config_t *cfg,const u_char *directive,const u_char **parameters,size_t len,cf_tpl_variable_t *var) {
  cf_validator_callback_t *cb;

  if((cb = cf_hash_get(registered_validators,(u_char *)directive,strlen(directive))) == NULL) return FLT_DECLINE;

  if(cb->type & CF_HTML_DIR_TYPE_BLOCK) return cb->callback(cfg,directive,parameters,len,var);
  return FLT_DECLINE;
}
/* }}} */

/* {{{ parse_message */
static u_char *cf_parse_message(cf_cfg_config_t *cfg,cf_cl_thread_t *thread,u_char *start,cf_array_t *stack,cf_string_t *content,cf_string_t *cite,const u_char *qchars,size_t qclen,int utf8,int xml,int max_sig_lines,int show_sig,int sig,int *qmode,int line) {
  const u_char *ptr,*tmp,*ptr1;
  int rc,run = 1,sb = 0,fail,ending,doit,quotemode = 0;
  u_char *directive,*parameter,*safe,*buff,*retval;
  cf_string_t d_content,d_cite,strtmp;
  cf_html_stack_elem_t stack_elem,*stack_tmp;
  cf_html_tree_t *telem1,*telem2;

  if(qmode) quotemode = *qmode;

  for(ptr=start;*ptr && run;++ptr) {
    switch(*ptr) {
      case '[':
        safe = (u_char *)ptr;
        ending = 0;

        /* [/name] ends a directive */
        if(*(ptr+1) == '/') {
          ending = 1;
          ++ptr;
        }

        /* ok, parse this directive */
        for(ptr1=ptr+1,sb=0;*ptr1 && (isalpha(*ptr1) || (ptr1 != ptr + 1 && isalnum(*ptr1))) && sb == 0 && *ptr1 != '<';++ptr1) {
          sb = *ptr1 == '[';
        }

        if(sb) {
          ptr = safe;
          goto default_action;
        }

        /* {{{ end of a directive */
        if(ending) {
          directive = strndup(ptr+1,ptr1-ptr-1);

          if(cf_is_open(directive,stack)) {
            stack_tmp = cf_array_element_at(stack,stack->elements-1);

            /* nesting is ok */
            if(cf_strcmp(stack_tmp->name,directive) == 0) {
              free(directive);
              return (u_char *)ptr1;
            }
            /* nesting error */
            else {
              if(quotemode) {
                free(directive);
                if(qmode) {
                  *qmode = 0;
                  return (u_char *)ptr-2;
                }
              }
              else {
                free(directive);
                return NULL;
              }
            }
          }
          /* not open, ignore it, user error */
          else {
            free(directive);
            ptr = safe;
            goto default_action;
          }
        }
        /* }}} */

        /* {{{ directive with argument, CForum syntax [name:argument], no ending tag */
        if(*ptr1 == ':') {
          tmp = ptr1;
          cf_str_init(&strtmp);

          /* get directive end, but accept \] as not-end */
          for(++ptr1;*ptr1 && *ptr1 != ']' && *ptr1 != '<';++ptr1) {
            if(*ptr1 == '\\' && *(ptr1+1) == ']') {
              cf_str_char_append(&strtmp,']');
              ++ptr1;
            }
            else cf_str_char_append(&strtmp,*ptr1);
          }

          if(*ptr1 == ']' && strtmp.len) {
            directive = strndup(ptr+1,tmp-ptr-1);
            buff      = strtmp.content;
            parameter = htmlentities_decode(buff,NULL);
            free(buff);

            rc = cf_run_inline_directive_filters(cfg,thread,directive,(const u_char **)&parameter,content,cite,qchars,sig);

            free(directive);
            free(parameter);

            if(rc == FLT_DECLINE) {
              ptr = safe;
              goto default_action;
            }
            else ptr = ptr1;
          }
          else {
            cf_str_cleanup(&strtmp);
            ptr = safe;
            goto default_action;
          }
        }
        /* }}} */

        /* {{{ we got [blub=blub] */
        else if(*ptr1 == '=') {
          tmp = ptr1;

          for(++ptr1;*ptr1 && *ptr1 != ']' && *ptr1 != '<';++ptr1);

          if(*ptr1 == ']') {
            directive = strndup(ptr+1,tmp-ptr-1);

            if(cf_hash_get(registered_directives,directive,tmp-ptr-1) == NULL) {
              free(directive);
              goto default_action;
            }

            if(ptr1 == tmp) {
              parameter = cf_alloc(NULL,1,1,CF_ALLOC_MALLOC);
              *parameter = '\0';
            }
            else parameter = strndup(tmp+1,ptr-tmp-1);

            parameter = strndup(tmp+1,ptr-tmp-1);

            stack_elem.begin   = (u_char *)ptr1;
            stack_elem.name    = directive;
            stack_elem.args    = cf_alloc(NULL,1,sizeof(u_char **),CF_ALLOC_MALLOC);
            stack_elem.args[0] = parameter;
            stack_elem.argnum  = 1;

            cf_array_push(stack,&stack_elem);

            cf_str_init(&d_content);
            cf_str_init(&d_cite);

            retval = cf_parse_message(cfg,thread,(u_char *)ptr1+1,stack,&d_content,cite ? &d_cite : NULL,qchars,qclen,utf8,xml,max_sig_lines,show_sig,sig,&quotemode,line);

            cf_array_pop(stack);

            if(retval == NULL || d_content.len == 0) {
              /* directive is invalid, get defined state */
              free(directive);
              free(parameter);
              cf_str_cleanup(&d_content);
              cf_str_cleanup(&d_cite);
              free(stack_elem.args);
              ptr = safe;
              goto default_action;
            }

            /* ok, go and run directive filters */
            rc = cf_run_block_directive_filters(cfg,thread,directive,(const u_char **)&parameter,1,content,cite,&d_content,cite ? &d_cite : NULL,qchars,sig);

            cf_str_cleanup(&d_content);
            cf_str_cleanup(&d_cite);

            if(rc == FLT_DECLINE) {
              ptr = safe;
              goto default_action;
            }

            ptr = retval;
          }
          else {
            ptr = safe;
            goto default_action;
          }
        }
        /* }}} */

        /* {{{ we got [something something */
        else if(isspace(*ptr1) || *ptr1 == ']') {
          directive = strndup(ptr+1,ptr1-ptr-1);

          if(cf_hash_get(registered_directives,directive,ptr1-ptr-1) == NULL) {
            free(directive);
            goto default_action;
          }

          memset(&stack_elem,0,sizeof(stack_elem));

          sb = 0;
          fail = 0;

          /* ok, we can have multiple arguments in the form of arg=value */
          while(!sb && *ptr1 != ']') {
            /* eat up trailing whitespaces */
            for(++ptr1;isspace(*ptr1) || *ptr1 == '=';++ptr1);

            /* whitespaces are not allowed */
            for(tmp = ptr1;*ptr1 != '=' && *ptr1 != ']' && *ptr1 != '<' && !isspace(*ptr1);++ptr1);

            if(*ptr1 == '<') {
              free(directive);
              if(stack_elem.args) free(stack_elem.args);
              ptr = safe;
              goto default_action;
            }

            stack_elem.args = cf_alloc(stack_elem.args,++stack_elem.argnum,sizeof(*stack_elem.args),CF_ALLOC_REALLOC);
            if(ptr1 == tmp) {
              stack_elem.args[stack_elem.argnum-1] = cf_alloc(NULL,1,1,CF_ALLOC_MALLOC);
              *(stack_elem.args[stack_elem.argnum-1]) = '\0';
            }
            else stack_elem.args[stack_elem.argnum-1] = strndup(tmp,ptr1-tmp);

            sb = *ptr1 == ']';
          }

          if(stack_elem.argnum % 2 != 0) fail = 1;

          if(!fail) {
            stack_elem.name = directive;
            stack_elem.begin = (u_char *)ptr1;
            cf_array_push(stack,&stack_elem);

            cf_str_init(&d_content);
            cf_str_init(&d_cite);

            retval = cf_parse_message(cfg,thread,(u_char *)ptr1+1,stack,&d_content,cite ? &d_cite : NULL,qchars,qclen,utf8,xml,max_sig_lines,show_sig,sig,&quotemode,line);
            cf_array_pop(stack);

            /* directive is invalid (e.g. no content, wrong nesting), get defined state */
            if(retval == NULL || d_content.len == 0) {
              free(directive);
              cf_str_cleanup(&d_content);
              cf_str_cleanup(&d_cite);
              free(stack_elem.args);
              ptr = safe;
              goto default_action;
            }

            /* ok, go and run directive filters */
            rc = cf_run_block_directive_filters(cfg,thread,directive,(const u_char **)stack_elem.args,stack_elem.argnum,content,cite,&d_content,cite ? &d_cite : NULL,qchars,sig);

            cf_str_cleanup(&d_content);
            cf_str_cleanup(&d_cite);

            if(rc == FLT_DECLINE) {
              ptr = safe;
              goto default_action;
            }

            ptr = retval;
          }
          else {
            ptr = safe;
            goto default_action;
          }
        }
        /* }}} */

        else {
          ptr = safe;
          goto default_action;
        }
        break;
      case '<':
        if(cf_strncmp(ptr,"<br />",6) == 0) {
          line++;

          if(cf_strncmp(ptr,"<br />-- <br />",15) == 0 && stack->elements == 0) {
            if(!show_sig) {
              run = 0;
              break;
            }

            if(xml) cf_str_chars_append(content,"<br /><span class=\"sig\">-- ",27);
            else cf_str_chars_append(content,"<br><span class=\"sig\">-- ",25);
            ptr += 9;
            sig = 1;
          }

          if(xml) cf_str_chars_append(content,"<br />",6);
          else    cf_str_chars_append(content,"<br>",4);

          if(sig && max_sig_lines > 0 && line >= max_sig_lines) {
            run = 0;
            break;
          }
          if(sig == 0 && cite) {
            cf_str_chars_append(cite,"\n",1);
            cf_str_chars_append(cite,qchars,qclen);
          }

          if(quotemode) {
            if(qmode) {
              stack_tmp = cf_array_element_at(stack,stack->elements-1);
              if(cf_strcmp(stack_tmp->name,"_QUOTING_") == 0 && cf_next_line_is_no_quote_line(ptr)) {
                *qmode = 0;
                return (u_char *)ptr + 5;
              }
            }
            else quotemode = 0;
          }

          ptr += 5;
        }
        else goto default_action;
        break;

      case 127:
        //if(!quotemode) str_chars_append(content,"<span class=\"q\">",16);
        if(quotemode == 0) {
          quotemode = 1;

          /* start special quoting block (ended by <br />) */
          memset(&stack_elem,0,sizeof(stack_elem));

          stack_elem.name = "_QUOTING_";
          stack_elem.begin = (u_char *)ptr;
          cf_array_push(stack,&stack_elem);

          cf_str_init(&d_content);
          cf_str_init(&d_cite);

          retval = cf_parse_message(cfg,thread,(u_char *)ptr+1,stack,&d_content,cite ? &d_cite : NULL,qchars,qclen,utf8,xml,max_sig_lines,show_sig,sig,&quotemode,line);
          cf_array_pop(stack);
          quotemode = 0;

          /* directive is invalid (e.g. no content, wrong nesting), get defined state */
          if(retval == NULL || d_content.len == 0) {
            ptr += 1;
            cf_str_char_append(content,0x7F);
            if(sig == 0 && cite) cf_str_chars_append(cite,qchars,qclen);
            cf_str_cleanup(&d_content);
            cf_str_cleanup(&d_cite);
            goto default_action;
          }

          cf_str_chars_append(content,"<span class=\"q\">",16);
          cf_str_char_append(content,0x7F);
          if(sig == 0 && cite) cf_str_chars_append(cite,qchars,qclen);

          /* ok, go and run append content */
          cf_str_str_append(content,&d_content);
          cf_str_chars_append(content,"</span>",7);
          if(sig == 0 && cite) cf_str_str_append(cite,&d_cite);

          cf_str_cleanup(&d_content);
          cf_str_cleanup(&d_cite);

          ptr = retval;
        }
        else {
          cf_str_char_append(content,0x7F);
          if(sig == 0 && cite) cf_str_chars_append(cite,qchars,qclen);
        }

        break;

      default:
        default_action:
        doit = 1;
        safe = (u_char *)ptr;

        if(parser_tree[*ptr]) {
          telem2 = telem1 = parser_tree[*ptr];

          while(telem1) {
            ++ptr;
            if(telem1->nodes && telem1->nodes[*ptr]) {
              telem2 = telem1;
              telem1 = telem1->nodes[*ptr];
            }
            else {
              telem2 = telem1;
              telem1 = NULL;
            }
          }

          if(telem2 && telem2->cb) {
            directive = strndup(safe,ptr-safe);
            rc        = telem2->cb(cfg,thread,directive,NULL,0,NULL,NULL,content,cite,qchars,sig);

            if(rc == FLT_OK) {
              doit = 0;
              ptr -= 1;
            }
          }
        }

        if(doit) {
          ptr = safe;
          cf_str_chars_append(content,ptr,1);
          if(sig == 0 && cite) cf_str_chars_append(cite,ptr,1);
        }
    }
  }

  if(sig) cf_str_chars_append(content,"</span>",7);
  if(quotemode && stack->elements == 0) cf_str_chars_append(content,"</span>",7);

  if(quotemode && stack->elements > 0) {
    stack_tmp = cf_array_element_at(stack,stack->elements-1);
    if(cf_strcmp(stack_tmp->name,"_QUOTING_") == 0) return (u_char *)ptr;
  }

  return NULL;
}
/* }}} */

/* {{{ cf_validate_message */
int cf_validate_message(cf_cfg_config_t *cfg,cf_array_t *stack,cf_cl_thread_t *thread,const u_char *msg,u_char **pos,cf_tpl_variable_t *var) {
  const u_char *ptr,*tmp,*ptr1;
  int rc,run = 1,sb = 0,fail,ending,retval,ret = 1;
  u_char *directive,*parameter,*safe,*buff;
  cf_string_t strtmp;
  cf_html_stack_elem_t stack_elem,*stack_tmp;

  for(ptr=(u_char *)msg;*ptr && run;++ptr) {
    switch(*ptr) {
      case '[':
        safe = (u_char *)ptr;
        ending = 0;

        /* [/name] ends a directive */
        if(*(ptr+1) == '/') {
          ending = 1;
          ++ptr;
        }

        /* ok, parse this directive */
        for(ptr1=ptr+1,sb=0;*ptr1 && isalpha(*ptr1) && sb == 0 && *ptr1 != '<';++ptr1) {
          sb = *ptr1 == '[';
        }

        if(sb) {
          ptr = safe;
          goto default_action;
        }

        /* {{{ end of a directive */
        if(ending) {
          directive = strndup(ptr+1,ptr1-ptr-1);

          if(cf_is_open(directive,stack)) {
            stack_tmp = cf_array_element_at(stack,stack->elements-1);

            /* nesting is ok */
            if(cf_strcmp(stack_tmp->name,directive) == 0) {
              free(directive);
              if(pos) *pos = (u_char *)ptr1;
              return FLT_OK;
            }
            /* nesting error */
            else {
              free(directive);
              return FLT_DECLINE;
            }
          }
          /* not open, ignore it, user error */
          else {
            free(directive);
            ptr = safe;
            goto default_action;
          }
        }
        /* }}} */

        /* {{{ directive with argument, CForum syntax [name:argument], no ending tag */
        if(*ptr1 == ':') {
          tmp = ptr1;
          cf_str_init(&strtmp);

          /* get directive end, but accept \] as not-end */
          for(++ptr1;*ptr1 && *ptr1 != ']' && *ptr1 != '<';++ptr1) {
            if(*ptr1 == '\\' && *(ptr1+1) == ']') {
              cf_str_char_append(&strtmp,']');
              ++ptr1;
            }
            else cf_str_char_append(&strtmp,*ptr1);
          }

          if(*ptr1 == ']') {
            directive = strndup(ptr+1,tmp-ptr-1);
            buff      = strtmp.content;

            if(!buff) {
              free(directive);
              ptr = safe;
              goto default_action;
            }

            parameter = htmlentities_decode(buff,NULL);
            free(buff);

            rc = cf_run_validate_inline(cfg,directive,(const u_char **)&parameter,var);

            free(directive);
            free(parameter);

            if(rc == FLT_DECLINE) {
              ptr = safe;
              goto default_action;
            }
            else ptr = ptr1;

            if(rc == FLT_ERROR) ret = FLT_ERROR;
          }
          else {
            cf_str_cleanup(&strtmp);
            ptr = safe;
            goto default_action;
          }
        }
        /* }}} */

        /* {{{ we got [blub=blub] */
        else if(*ptr1 == '=') {
          tmp = ptr1;

          for(++ptr1;*ptr1 && *ptr1 != ']' && *ptr1 != '<';++ptr1);

          if(*ptr1 == ']') {
            directive = strndup(ptr+1,tmp-ptr-1);

            if(cf_hash_get(registered_validators,directive,tmp-ptr-1) == NULL) {
              free(directive);
              goto default_action;
            }

            parameter = strndup(tmp+1,ptr-tmp-1);

            stack_elem.begin   = (u_char *)ptr1;
            stack_elem.name    = directive;
            stack_elem.args    = cf_alloc(NULL,1,sizeof(u_char **),CF_ALLOC_MALLOC);
            stack_elem.args[0] = parameter;
            stack_elem.argnum  = 1;

            cf_array_push(stack,&stack_elem);

            retval = cf_validate_message(cfg,stack,thread,ptr1+1,(u_char **)&ptr,var);

            cf_array_pop(stack);

            if(retval == FLT_ERROR) {
              /* directive is invalid, get defined state */
              free(directive);
              free(parameter);
              free(stack_elem.args);

              ret = FLT_ERROR;
              goto default_action;
            }
            else if(retval == FLT_DECLINE || ret == 1) {
              /* directive is invalid, get defined state */
              free(directive);
              free(parameter);
              free(stack_elem.args);

              goto default_action;
            }

            /* ok, go and run directive filters */
            rc = cf_run_validate_block_directive(cfg,directive,(const u_char **)&parameter,1,var);

            if(rc == FLT_ERROR) ret = FLT_ERROR;
            else if(rc == FLT_DECLINE) {
              ptr = safe;
              goto default_action;
            }
          }
          else {
            ptr = safe;
            goto default_action;
          }
        }
        /* }}} */

        /* {{{ we got [something something */
        else if(isspace(*ptr1) || *ptr1 == ']') {
          directive = strndup(ptr+1,ptr1-ptr-1);

          if(cf_hash_get(registered_validators,directive,ptr1-ptr-1) == NULL) {
            free(directive);
            goto default_action;
          }

          memset(&stack_elem,0,sizeof(stack_elem));

          sb = 0;
          fail = 0;

          /* ok, we can have multiple arguments in the form of arg=value */
          while(!sb && *ptr1 != ']') {
            /* eat up trailing whitespaces */
            for(++ptr1;isspace(*ptr1) || *ptr1 == '=';++ptr1);

            /* whitespaces are not allowed */
            for(tmp = ptr1;*ptr1 != '=' && *ptr1 != ']' && *ptr1 != '<' && !isspace(*ptr1);++ptr1);

            if(*ptr1 == '<') {
              free(directive);
              if(stack_elem.args) free(stack_elem.args);
              ptr = safe;
              goto default_action;
            }

            stack_elem.args = cf_alloc(stack_elem.args,++stack_elem.argnum,sizeof(*stack_elem.args),CF_ALLOC_REALLOC);
            stack_elem.args[stack_elem.argnum-1] = strndup(tmp,ptr1-tmp);

            sb = *ptr1 == ']';
          }

          if(stack_elem.argnum % 2 != 0) fail = 1;

          if(!fail) {
            stack_elem.name = directive;
            stack_elem.begin = (u_char *)ptr1;
            cf_array_push(stack,&stack_elem);

            retval = cf_validate_message(cfg,stack,thread,ptr1+1,(u_char **)&ptr,var);

            cf_array_pop(stack);

            if(retval == FLT_ERROR) {
              /* directive is invalid, get defined state */
              free(directive);
              free(stack_elem.args);
              ptr = safe;
              ret = FLT_ERROR;

              goto default_action;
            }
            else if(retval == FLT_DECLINE || retval == 1) {
              free(directive);
              free(stack_elem.args);
              ptr = safe;

              goto default_action;
            }

            /* ok, go and run directive filters */
            rc = cf_run_validate_block_directive(cfg,directive,(const u_char **)stack_elem.args,stack_elem.argnum,var);

            if(rc == FLT_ERROR) ret = FLT_ERROR;
            else if(rc == FLT_DECLINE) {
              ptr = safe;
              goto default_action;
            }
          }
          else {
            ptr = safe;
            goto default_action;
          }
        }
        /* }}} */

        else {
          ptr = safe;
          goto default_action;
        }
        break;

      default:
        default_action:
        break;
    }
  }

  return ret;
}

/* }}} */


/* {{{ cf_html_register_validator */
int cf_html_register_validator(const u_char *name,cf_directive_validator_t filter,int type) {
  cf_validator_callback_t clbck;
  size_t len = strlen(name);

  if(!registered_validators) registered_validators = cf_hash_new(NULL);
  if(cf_hash_get(registered_validators,(u_char *)name,len) != NULL) return -1;

  clbck.type = type;
  clbck.callback = filter;
  cf_hash_set(registered_validators,(u_char *)name,len,&clbck,sizeof(clbck));

  return 0;
}
/* }}} */

/* {{{ cf_validate_msg */
int cf_validate_msg(cf_cfg_config_t *cfg,cf_cl_thread_t *thread,const u_char *msg,cf_tpl_variable_t *var) {
  cf_array_t my_stack;
  int rc;

  if(registered_validators == NULL) registered_validators = cf_hash_new(NULL);

  cf_array_init(&my_stack,sizeof(cf_html_stack_elem_t),NULL);
  rc = cf_validate_message(cfg,&my_stack,thread,msg,NULL,var);

  cf_array_destroy(&my_stack);

  return rc;
}
/* }}} */

/* {{{ cf_html_register_directive */
int cf_html_register_directive(const u_char *name,cf_directive_filter_t filter,int type) {
  cf_directive_callback_t clbck;
  size_t len = strlen(name);

  if(!registered_directives) registered_directives = cf_hash_new(NULL);
  if(cf_hash_get(registered_directives,(u_char *)name,len) != NULL) return -1;

  clbck.type = type;
  clbck.callback = filter;
  cf_hash_set(registered_directives,(u_char *)name,len,&clbck,sizeof(clbck));

  return 0;
}
/* }}} */

/* {{{ cf_html_register_textfilter */
int cf_html_register_textfilter(const u_char *text,cf_directive_filter_t filter) {
  u_char *ptr;
  cf_html_tree_t *elem,*elem1;

  /* there isn't an entry starting with *text */
  if(parser_tree[*text] == NULL) {
    elem     = cf_alloc(NULL,1,sizeof(*elem),CF_ALLOC_CALLOC);
    elem->c  = *text;
    parser_tree[*text] = elem;
    elem1    = elem;

    for(ptr=(u_char *)text+1;*ptr;++ptr) {
      if(!elem1->nodes) elem1->nodes = cf_alloc(NULL,256,sizeof(*elem->nodes),CF_ALLOC_CALLOC);

      elem        = cf_alloc(NULL,1,sizeof(*elem),CF_ALLOC_CALLOC);
      elem->c     = *ptr;
      elem1->nodes[*ptr] = elem;
      elem1       = elem;
    }

    elem1->cb = filter;
    return 0;
  }
  else {
    elem1 = parser_tree[*text];

    for(ptr=(u_char *)text+1;*ptr;++ptr) {
      if(!elem1->nodes) {
        elem1->nodes = cf_alloc(NULL,256,sizeof(*elem->nodes),CF_ALLOC_CALLOC);
        elem = cf_alloc(NULL,1,sizeof(*elem),CF_ALLOC_CALLOC);
        elem->c = *ptr;
        elem1->nodes[*ptr] = elem;
        elem1 = elem;
      }
      else {
        if(elem1->nodes[*ptr]) elem1 = elem1->nodes[*ptr];
        else {
          elem = cf_alloc(NULL,1,sizeof(*elem),CF_ALLOC_CALLOC);
          elem->c = *ptr;
          elem1->nodes[*ptr] = elem;
          elem1 = elem;
        }
      }
    }

    if(!elem1->cb) {
      elem1->cb = filter;
      return 0;
    }
  }

  return -1;
}
/* }}} */

/* {{{ cf_msg_to_html */
void cf_msg_to_html(cf_cfg_config_t *cfg,cf_cl_thread_t *thread,const u_char *msg,cf_string_t *content,cf_string_t *cite,u_char *quote_chars,int max_sig_lines,int show_sig) {
  cf_cfg_config_value_t *cs   = cf_cfg_get_value(cfg,"DF:ExternCharset");
  cf_cfg_config_value_t *xmlm = cf_cfg_get_value(cfg,"DF:XHTMLMode");
  u_char *qchars,*ptr;
  size_t qclen;
  int utf8 = cf_strcmp(cs->sval,"UTF-8") == 0,xml;
  cf_array_t my_stack;
  cf_string_t content1;

  if(registered_directives == NULL) registered_directives = cf_hash_new(NULL);

  xml = cf_strcmp(xmlm->sval,"yes") == 0;

  if(utf8 || (qchars = htmlentities_charset_convert(quote_chars,"UTF-8",cs->sval,&qclen,0)) == NULL) {
    qchars = htmlentities(quote_chars,0);
    qclen  = strlen(qchars);
  }

  cf_run_content_filters(cfg,PRE_CONTENT_FILTER,thread,content,cite,qchars);

  /* first line has no linebreak, so append quoting chars to cite */
  if(cite) cf_str_chars_append(cite,qchars,qclen);

  cf_array_init(&my_stack,sizeof(cf_html_stack_elem_t),NULL);
  cf_str_init(&content1);

  cf_parse_message(cfg,thread,(u_char *)msg,&my_stack,content,cite,qchars,qclen,utf8,xml,max_sig_lines,show_sig,0,NULL,0);

  /* doin this because of plugins like the syntax parser; they could match quoting chars as operators */
  for(ptr=content1.content;*ptr;++ptr) {
    if(*ptr == 0x7F) cf_str_chars_append(content,qchars,qclen);
    else cf_str_char_append(content,*ptr);
  }

  cf_str_cleanup(&content1);

  cf_run_content_filters(cfg,POST_CONTENT_FILTER,thread,content,cite,qchars);

  free(qchars);
}
/* }}} */

/* {{{ _do_html */
void _do_html(cf_hierarchical_node_t *ary,cf_cl_thread_t *thread,int ShowInvisible,const u_char *linktpl,const u_char *cs,const u_char *dft,const u_char *locale) {
  size_t len,i;
  u_char *date,*link;
  cf_tpl_variable_t ary_tpl;
  cf_string_t tmpstr;
  cf_hierarchical_node_t *ary1;

  date = cf_general_get_time(dft,locale,&len,&ary->msg->date);
  link = cf_get_link(linktpl,thread->tid,ary->msg->mid);

  cf_set_variable_hash(&ary->msg->hashvar,cs,"author",ary->msg->author.content,ary->msg->author.len,1);
  cf_set_variable_hash(&ary->msg->hashvar,cs,"title",ary->msg->subject.content,ary->msg->subject.len,1);

  cf_str_init_growth(&tmpstr,120);
  cf_uint64_to_str(&tmpstr,ary->msg->mid);
  cf_set_variable_hash(&ary->msg->hashvar,cs,"mid",tmpstr.content,tmpstr.len,0);
  cf_str_cleanup(&tmpstr);

  cf_str_init_growth(&tmpstr,120);
  cf_uint64_to_str(&tmpstr,thread->tid);
  cf_set_variable_hash(&ary->msg->hashvar,cs,"tid",tmpstr.content,tmpstr.len,0);
  cf_str_cleanup(&tmpstr);

  if(ary->msg->category.len) cf_set_variable_hash(&ary->msg->hashvar,cs,"category",ary->msg->category.content,ary->msg->category.len,1);

  if(date) {
    cf_set_variable_hash(&ary->msg->hashvar,cs,"time",date,len,1);
    free(date);
  }

  if(link) {
    cf_set_variable_hash(&ary->msg->hashvar,cs,"link",link,strlen(link),1);
    free(link);
  }

  if(ary->childs.elements) {
    cf_tpl_var_init(&ary_tpl,TPL_VARIABLE_ARRAY);

    for(i=0;i<ary->childs.elements;++i) {
      ary1 = cf_array_element_at(&ary->childs,i);

      _do_html(ary1,thread,ShowInvisible,linktpl,cs,dft,locale);
      cf_tpl_var_add(&ary_tpl,&ary1->msg->hashvar);
    }

    cf_tpl_hashvar_setvalue(&ary->msg->hashvar,"has_subposts",TPL_VARIABLE_INT,1);
    cf_tpl_hashvar_set(&ary->msg->hashvar,"subposts",&ary_tpl);
  }
}
/* }}} */

/* {{{ _starthread_tlist */
int _starthread_tlist(cf_cl_thread_t *thread,int ShowInvisible,const u_char *linktpl,const u_char *cs,const u_char *dft,const u_char *locale) {
  cf_hierarchical_node_t ary,*ary1;
  size_t i;
  cf_tpl_variable_t tpl_ary;

  cf_msg_filter_invisible(thread->ht,&ary,ShowInvisible);

  if(ary.msg) {
    _do_html(&ary,thread,ShowInvisible,linktpl,cs,dft,locale);
    cf_tpl_hashvar_setvalue(&ary.msg->hashvar,"visible_posts",TPL_VARIABLE_INT,1);
    return 0;
  }
  else {
    if(ary.childs.elements) {
      cf_tpl_var_init(&tpl_ary,TPL_VARIABLE_ARRAY);

      for(i=0;i<ary.childs.elements;++i) {
        ary1 = cf_array_element_at(&ary.childs,i);

        _do_html(ary1,thread,ShowInvisible,linktpl,cs,dft,locale);
        cf_tpl_var_add(&tpl_ary,&ary1->msg->hashvar);
      }

      cf_tpl_hashvar_set(&thread->messages->hashvar,"posts",&tpl_ary);
      cf_tpl_hashvar_setvalue(&thread->messages->hashvar,"visible_posts",TPL_VARIABLE_INT,1);
      cf_tpl_hashvar_setvalue(&thread->messages->hashvar,"posts_list",TPL_VARIABLE_INT,1);

      return 0;
    }
  }

  return -1;
}
/* }}} */

/* {{{ cf_gen_threadlist */
int cf_gen_threadlist(cf_cfg_config_t *cfg,cf_cl_thread_t *thread,cf_hash_t *head,cf_string_t *threadlist,const u_char *tplname,const u_char *type,const u_char *linktpl,int mode) {
  cf_template_t tpl;
  cf_message_t *msg;
  int ShowInvisible = cf_hash_get(GlobalValues,"ShowInvisible",13) == NULL ? 0 : 1;

  int ret,level;

  cf_cfg_config_value_t *dft = cf_cfg_get_value(cfg,"FV:DateFormatThreadList"),
    *locale = cf_cfg_get_value(cfg,"DF:DateLocale"),
    *cs = cf_cfg_get_value(cfg,"DF:ExternCharset");

  cf_str_init(threadlist);

  if(cf_strcmp(type,"none") != 0) {
    cf_tpl_init(&tpl,tplname);

    /* {{{ hide thread path if in partitial mode */
    if(cf_strcmp(type,"partitial") == 0 && mode == CF_MODE_THREADVIEW) {
      for(msg=thread->messages;msg && msg->mid != thread->threadmsg->mid;msg=msg->next) msg->may_show = 0;

      level = msg->level;
      msg->may_show = 0;

      for(msg=msg->next;msg && msg->level > level;msg=msg->next);
      for(;msg;msg=msg->next) msg->may_show = 0;
    }
    /* }}} */
    /* {{{ set some standard variables */
    else if(mode == CF_MODE_THREADVIEW) {
      cf_tpl_hashvar_setvalue(&thread->threadmsg->hashvar,"active",TPL_VARIABLE_INT,1);
      cf_tpl_setvalue(&tpl,"mode",TPL_VARIABLE_STRING,"threadview",10);
    }
    else if(mode == CF_MODE_THREADLIST) {
      cf_tpl_hashvar_setvalue(&thread->messages->hashvar,"start",TPL_VARIABLE_INT,1);
      cf_tpl_hashvar_setvalue(&thread->messages->hashvar,"msgnum",TPL_VARIABLE_INT,thread->msg_len);
      cf_tpl_hashvar_setvalue(&thread->messages->hashvar,"answers",TPL_VARIABLE_INT,thread->msg_len-1);
      cf_tpl_setvalue(&tpl,"mode",TPL_VARIABLE_STRING,"threadlist",10);
    }
    /* }}} */

    /* {{{ run handlers in pre and post mode */
    if(mode == CF_MODE_THREADLIST) {
      ret = cf_run_view_handlers(cfg,thread,head,CF_MODE_THREADLIST|CF_MODE_PRE);
      if(ret == FLT_OK || ret == FLT_DECLINE || ShowInvisible) {
        for(msg=thread->messages;msg;msg=msg->next) cf_run_view_list_handlers(cfg,msg,head,thread->tid,mode);
        ret = cf_run_view_handlers(cfg,thread,head,mode|CF_MODE_POST);

        if(ret == FLT_EXIT && !ShowInvisible) return FLT_EXIT;
      }
      else return FLT_EXIT;

    }
    else {
      cf_run_view_handlers(cfg,thread,head,mode|CF_MODE_PRE);
      for(msg=thread->messages;msg;msg=msg->next) cf_run_view_list_handlers(cfg,msg,head,thread->tid,mode);
      cf_run_view_handlers(cfg,thread,head,mode|CF_MODE_POST);
    }
    /* }}} */

    if(_starthread_tlist(thread,ShowInvisible,linktpl,cs->sval,dft->sval,locale->sval) == 0) {
      cf_tpl_setvar(&tpl,"thread",&thread->messages->hashvar);
      cf_tpl_parse_to_mem(&tpl);
      threadlist->content  = tpl.parsed.content;
      threadlist->len      = tpl.parsed.len;
      threadlist->reserved = tpl.parsed.reserved;

      memset(&tpl.parsed,0,sizeof(tpl.parsed));
    }

    cf_tpl_finish(&tpl);
  }

  return FLT_OK;
}
/* }}} */

void cf_htmllib_init(void) {
  memset(parser_tree,0,sizeof(parser_tree));
}

/* eof */
